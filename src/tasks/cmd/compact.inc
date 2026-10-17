// ==========================================
//  compact.inc -- REU-FS compaction body
// ==========================================
//
//  Pass 0  refuse unless the FS is provably idle
//  Pass 1  read-only verify + measure      (aborts before ANY write)
//  Pass 2  per entry: move the data, then re-point that entry -- so every
//          prefix of the work leaves a valid filesystem
//
//  HARD CONSTRAINT: all logic lives in this file. There is no
//  kernel space left -- filesys.lib must not grow (a resident task declares it
//  and then holds its pages for the session) -- so this adds no kernel call,
//  jump-table entry, kernel-data byte or lib entry. libMask = 0: no library is loaded.
//
//  ---------------------------------------------------------------------
//  CLOBBER DISCIPLINE (Rule 3). Nothing is held in A/X/Y across a call, and
//  every routine used is listed with its verified contract:
//    kPrintString    A X Y kPtr kArg       (kPtr ends PAST the NUL)
//    kPrintChar      A X Y kPtr kArg srcPtr
//    kNewline        A X Y kPtr kArg srcPtr
//    kSetCursor      nothing               (X=col, Y=row -- only preserving routine)
//    kGetCursor      X=col Y=row           (A untouched)
//    kByteToDec      A X Y                 (in A=value, kPtr=buf; out Y=len)
//    kLockScreen     A X Y                 (semWait may yield, then saves kPtr/kArg)
//    kUnlockScreen   A X Y                 (restores kPtr/kArg)
//    kShareScreen    A Y                   (X preserved = input slot)
//    kReuToC64       A Y                   (X preserved; kPtr/kArg are read-only inputs)
//    kReuFromC64     A Y                   (as above)
//    kReuDirPageDma  A                     (X, Y preserved; CALLER MUST SEI)
//    kGetTaskName    A Y kPtr              (X preserved)
//    syscall         A X Y
//  kByteToDec is unused here -- decimal formatting is done in-place so that
//  16-bit counts (0..511) can be printed.
//
//  kPrintChar/kPrintString/kNewline all finish with `cli`, so they are NEVER
//  called inside an SEI window. Inside an SEI window only RAM work and the
//  page DMA happen (kReuDirPageDma is php...plp-balanced and preserves the
//  caller's I flag).
//
//  ALL state lives in the instance buffer, reached only through dataPtr --
//  never in the kArg block. kArg is used solely as the DMA parameter block
//  (kArg..kArg+5), re-set immediately before every DMA, so no routine can
//  corrupt another's locals.
//
//  INSTANCE ACCESS: an instance offset is NOT an address. Every access goes
//  through (dataPtr),y via the macros below -- `lda CMP_FOO` would read
//  absolute address CMP_FOO, which is a bug. Only lda/sta/cmp/adc/sbc/and/
//  ora/eor have a (zp),y mode, so X/Y transfers go through A.
//
//  FS offset O maps to REU addr (lo = O.lo, hi = O.mid + reuFsDataBaseHi)
//  and bank (O.hi + reuFsDataBaseBank + carry) -- the same arithmetic as
//  reuDmaRange, but read from kernel data instead of re-hardcoding $D000/5.
//  ---------------------------------------------------------------------

// --- instance buffer layout (offsets from dataPtr) ---
.const CMP_BUF      = 0     // 128B bounce buffer
.const CMP_LINE     = 128   // 38B status line + 1 NUL byte (kPrintString)
.const CMP_SELF     = 167   // own task slot
.const CMP_SHELL    = 168   // parent (shell) slot
.const CMP_ROW      = 169   // status-line row
.const CMP_IDX      = 170   // 2B: entry index (1..511)
.const CMP_FLAGS    = 172   // 1B: captured flags byte
.const CMP_OFF      = 173   // 3B: captured data offset
.const CMP_SIZE     = 176   // 2B: captured size
.const CMP_NAME     = 178   // 11B: captured name (space-padded)
.const CMP_DEST     = 189   // 3B: running destination offset
.const CMP_OLD      = 192   // 3B: pre-run nextFreeOffset
.const CMP_DBHI     = 195   // cached reuFsDataBaseHi
.const CMP_DBBANK   = 196   // cached reuFsDataBaseBank
.const CMP_CAP      = 197   // 3B: cached capacity
.const CMP_FILES    = 200   // 2B: live file count
.const CMP_DEAD     = 202   // 2B: tombstone count
.const CMP_MOVED    = 204   // 2B
.const CMP_PURGED   = 206   // 2B
.const CMP_DONE     = 208   // 2B: live entries committed in pass 2
.const CMP_BLOCKERS = 210   // 1B: blocking task count (<= 23)
.const CMP_SHOWN    = 211   // 1B: blockers listed so far
.const CMP_EOFF     = 212   // 1B: entry byte offset within its page
.const CMP_PG       = 213   // 1B: page holding the captured entry
.const CMP_MSRC     = 214   // 3B: cmMove source offset
.const CMP_MDST     = 217   // 3B: cmMove destination offset
.const CMP_MLEN     = 220   // 2B: cmMove remaining length
.const CMP_CHUNK    = 222   // 2B: cmMove current chunk
.const CMP_DEC      = 224   // 2B: decimal value (lo, hi)
.const CMP_STR      = 226   // 8B: ASCII scratch (<= 6 chars + NUL)
.const CMP_CUR      = 234   // 1B: cmpEmitCh's write cursor into CMP_LINE (0-37)
.const CMP_T0       = 235   // 1B: scratch
.const CMP_T1       = 236   // 1B: scratch
.const CMP_T2       = 237   // 1B: scratch
.const CMP_DIG      = 238   // 1B: cmpDec16 digit counter -- must NOT be CMP_T0:
                            //     Pass 0 keeps its scan slot in CMP_T0, and
                            //     cmpPrintBlocker prints the id via cmpDec16,
                            //     so a clobber there truncates the "? busy" list
.const CMP_END      = 239

.assert "compact: instance layout exceeds CMP_DATA_SIZE", (CMP_END <= CMP_DATA_SIZE) ? 1 : 0, 1

.const CMP_MAXSHOW  = 6     // blockers listed before "... and N more"
.const CMP_BUFSZ    = 128   // bounce buffer / max chunk
.const CMP_LINEW    = 38    // line width -- col 40 is where printChar wraps
// REU_FS_SIG below is compact's own local name for kernel-shared.inc's
// REU_FS_SIGNATURE (imported globally via kernel-data.inc); MAX_TASKS,
// TA_DEAD, TA_SWAPPED come from the same shared file.
.const REU_FS_SIG   = REU_FS_SIGNATURE

// --- instance accessors (an instance offset is not an address) ---
.macro ldC(off)  { ldy #off
                   lda (dataPtr),y }
.macro stC(off)  { ldy #off
                   sta (dataPtr),y }
.macro cpC(off)  { ldy #off
                   cmp (dataPtr),y }
.macro adC(off)  { ldy #off
                   adc (dataPtr),y }
.macro sbC(off)  { ldy #off
                   sbc (dataPtr),y }
.macro orC(off)  { ldy #off
                   ora (dataPtr),y }
.macro ldCY(off) { ldy #off
                   lda (dataPtr),y
                   tay }
.macro ldCX(off) { ldy #off
                   lda (dataPtr),y
                   tax }
.macro inC(off)  { ldy #off
                   lda (dataPtr),y
                   clc
                   adc #1
                   sta (dataPtr),y }

// --- message macro (kPtr = string; print) ---
.macro cmpMsg(lbl) {
    lda #<lbl
    sta kPtr
    lda #>lbl
    sta kPtr+1
    jsr kPrintString
}

// --- cmCapFree -- field := CMP_CAP - field (24-bit) ---
//  The report's "free"/"was" figures must mean what `dir` means by "free"
//  (capacity - nextFreeOffset), not the raw offset: an offset is the space
//  IN USE, so printing it as "free" makes the figure fall after a compaction
//  -- contradicting the "reclaim" line, which is by definition the increase
//  in free space. Expanded rather than built from the ldC/sbC macros so the
//  borrow chain is visible: ldy/lda/sta leave C alone, so a single `sec`
//  chains through lo, mid and hi (the same shape as dirPrint24).
.macro cmCapFree(field) {
    sec
    ldy #CMP_CAP
    lda (dataPtr),y
    ldy #field
    sbc (dataPtr),y
    sta (dataPtr),y
    ldy #CMP_CAP+1
    lda (dataPtr),y
    ldy #field+1
    sbc (dataPtr),y
    sta (dataPtr),y
    ldy #CMP_CAP+2
    lda (dataPtr),y
    ldy #field+2
    sbc (dataPtr),y
    sta (dataPtr),y
}

// ==========================================
//  Entry -- screen, then Pass 0
// ==========================================
compact:
    lda #SYS_GETTID
    jsr syscall                 // gettid -> A = own slot
    :stC(CMP_SELF)
    tax                         // X = own slot
    lda taskParent,x            // shell slot
    :stC(CMP_SHELL)
    tax                         // X = shell slot (kShareScreen input)
    jsr kShareScreen            // C=0 ok; X preserved
    bcc cmpScreen
    lda #SYS_EXIT
    jsr syscall                 // no screen -> exit silently
cmpScreen:
    jsr kLockScreen             // may yield; saves kPtr/kArg per task

    // --- Pass 0a: FS formatted? ---
    lda reuFsEnabled
    cmp #REU_FS_SIG
    beq cmpGuard
    :cmpMsg(cmsgNoFs)
    jsr kNewline
    jmp cmpExit

    // --- Pass 0b: quiescence guard (two scans -- count, then list) ---
cmpGuard:
    lda #0
    :stC(CMP_BLOCKERS)
    :stC(CMP_SHOWN)
    :stC(CMP_T0)                // scan slot
cmpGrdLp:
    :ldC(CMP_T0)
    cmp #MAX_TASKS
    bcs cmpGrdDone
    jsr cmpBlkTest              // C=1 iff slot CMP_T0 is a blocker
    bcc cmpGrdNext
    :ldC(CMP_BLOCKERS)
    clc
    adc #1
    :stC(CMP_BLOCKERS)
cmpGrdNext:
    :inC(CMP_T0)
    jmp cmpGrdLp
cmpGrdDone:
    :ldC(CMP_BLOCKERS)
    beq cmpGrdClear             // clear -> Pass 1 (jmp: cmpMeasure is far)
    jmp cmpBusy
cmpGrdClear:
    jmp cmpMeasure

// --- cmpBlkTest -- C=1 iff task slot CMP_T0 is a blocker ---
//  A blocker is any slot that is neither self nor the parent shell and is
//  not DEAD or SWAPPED. TA_SLEEPING COUNTS (it can wake and touch the FS);
//  TA_SWAPPED does not (an evicted task cannot run).
//  Clobbers A, X, Y.
cmpBlkTest:
    :ldC(CMP_SELF)
    :cpC(CMP_T0)
    beq cmpBlkNo
    :ldC(CMP_SHELL)
    :cpC(CMP_T0)
    beq cmpBlkNo
    :ldCX(CMP_T0)
    lda taskState,x
    beq cmpBlkNo                // TA_DEAD
    cmp #TA_SWAPPED
    beq cmpBlkNo
    sec
    rts
cmpBlkNo:
    clc
    rts

// ==========================================
//  Busy report -- nothing is written to the REU
// ==========================================
cmpBusy:
    :cmpMsg(msgBusy)            // "? busy - "
    :ldC(CMP_BLOCKERS)
    jsr cmpPrintDec
    :cmpMsg(msgBusy2)           // " task(s) must be killed"
    jsr kNewline
    lda #0
    :stC(CMP_T0)
cmpBusyLp:
    :ldC(CMP_T0)
    cmp #MAX_TASKS
    bcs cmpBusyTail
    jsr cmpBlkTest
    bcc cmpBusyNext
    :ldC(CMP_SHOWN)
    cmp #CMP_MAXSHOW
    bcs cmpBusyTail             // listed enough -- go to the trailer
    clc
    adc #1
    :stC(CMP_SHOWN)
    jsr cmpPrintBlocker
cmpBusyNext:
    :inC(CMP_T0)
    jmp cmpBusyLp
cmpBusyTail:
    :ldC(CMP_BLOCKERS)
    cmp #CMP_MAXSHOW
    bcs cmpBusyMore             // some blockers were not listed
    jmp cmpExit                 // every blocker was listed (jmp: cmpExit is far)
cmpBusyMore:
    sec
    sbc #CMP_MAXSHOW
    jsr cmpPrintDec
    :cmpMsg(msgMore2)           // " more (see ps)"
    jsr kNewline
    jmp cmpExit

// --- cmpPrintBlocker -- "  name (id N)" for slot CMP_T0 ---
cmpPrintBlocker:
    lda #$20
    jsr kPrintChar
    lda #$20
    jsr kPrintChar
    // kGetTaskName DMAs the REU for thread tasks and is NOT internally
    // SEI-protected, so it runs inside one (the scheduler's context switch
    // drives the same $DF00-$DF0A registers). Returns kPtr = name.
    sei
    :ldCX(CMP_T0)
    jsr kGetTaskName
    lda kPtr
    :stC(CMP_T1)
    lda kPtr+1
    :stC(CMP_T2)
    cli
    :ldC(CMP_T1)
    sta kPtr
    :ldC(CMP_T2)
    sta kPtr+1
    jsr kPrintString
    :cmpMsg(msgId)              // " (id "
    :ldC(CMP_T0)
    jsr cmpPrintDec
    :cmpMsg(msgClose)           // ")"
    jsr kNewline
    rts

// --- cmpPrintDec -- print A (0-255) as decimal at the cursor ---
cmpPrintDec:
    :stC(CMP_DEC)
    lda #0
    :stC(CMP_DEC+1)
    jsr cmpDec16
    jmp cmpPrintStr

// --- cmpPrintDec16 -- print A(lo)/X(hi) as decimal at the cursor ---
cmpPrintDec16:
    :stC(CMP_DEC)
    txa
    :stC(CMP_DEC+1)
    jsr cmpDec16
    jmp cmpPrintStr

// --- cmpPrintStr -- print the NUL-terminated string at CMP_STR ---
cmpPrintStr:
    lda dataPtr
    clc
    adc #CMP_STR
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    jsr kPrintString
    rts

// --- cmpDec16 -- CMP_DEC (16-bit, 0..511) -> NUL-terminated ASCII at CMP_STR
//  Leading zeros are suppressed except for a lone "0". Clobbers A, X, Y.
//  The digit counter is CMP_DIG, never CMP_T0 -- cmpPrintBlocker prints a task
//  id through here while cmpBusy holds its scan slot in CMP_T0.
cmpDec16:
    lda #CMP_STR
    :stC(CMP_T1)
    // ---- hundreds ----
    lda #$2F
    :stC(CMP_DIG)
cd16H:
    :inC(CMP_DIG)
    sec
    :ldC(CMP_DEC)
    sbc #100
    :stC(CMP_DEC)
    :ldC(CMP_DEC+1)
    sbc #0
    :stC(CMP_DEC+1)
    bcs cd16H
    clc
    :ldC(CMP_DEC)
    adc #100
    :stC(CMP_DEC)
    :ldC(CMP_DEC+1)
    adc #0
    :stC(CMP_DEC+1)
    :ldC(CMP_DIG)
    cmp #$30
    beq cd16NoH                 // hundreds == 0 -> suppress
    jsr cd16Put
cd16NoH:
    // ---- tens ----
    lda #$2F
    :stC(CMP_DIG)
cd16T:
    :inC(CMP_DIG)
    sec
    :ldC(CMP_DEC)
    sbc #10
    :stC(CMP_DEC)
    bcs cd16T
    clc
    adc #10
    :stC(CMP_DEC)               // remainder 0..9 (hi is already 0)
    :ldC(CMP_DIG)
    cmp #$30
    bne cd16Tens                // tens != 0 -> emit
    :ldC(CMP_T1)
    cmp #CMP_STR
    beq cd16NoT                 // nothing emitted yet -> suppress
cd16Tens:
    :ldC(CMP_DIG)
    jsr cd16Put
cd16NoT:
    // ---- ones (always emitted) ----
    :ldC(CMP_DEC)
    ora #$30
    jsr cd16Put
    // NUL-terminate at the cursor
    :ldCY(CMP_T1)
    lda #0
    sta (dataPtr),y
    rts

// --- cd16Put -- store A at CMP_STR[CMP_T1], advance the cursor ---
//  Uses CMP_T1 (not CMP_CUR) -- CMP_CUR is a separate cursor owned by
//  cmpEmitCh for the CMP_LINE status-line buffer; cmpEmitDec16 calls
//  through this routine while mid-line, so the two must not alias.
cd16Put:
    pha                          // save the digit char -- ldCY clobbers A
    :ldCY(CMP_T1)
    pla
    sta (dataPtr),y
    :inC(CMP_T1)
    rts

// ==========================================
//  Pass 1 -- read-only verify + measure
// ==========================================
cmpMeasure:
    // cache FS geometry (read-only use of existing kernel data)
    lda reuFsDataBaseHi
    :stC(CMP_DBHI)
    lda reuFsDataBaseBank
    :stC(CMP_DBBANK)
    lda reuFsCapacityLo
    :stC(CMP_CAP)
    lda reuFsCapacityHi
    :stC(CMP_CAP+1)
    lda reuFsCapacity2
    :stC(CMP_CAP+2)

    // oldFree = slot0.nextFreeOffset
    lda #0
    jsr cmpDirRead              // page 0 -> reuDirBuf
    lda reuDirBuf+17
    :stC(CMP_OLD)
    lda reuDirBuf+18
    :stC(CMP_OLD+1)
    lda reuDirBuf+19
    :stC(CMP_OLD+2)

    // dest = 0 ; files = 0 ; dead = 0
    lda #0
    :stC(CMP_DEST)
    :stC(CMP_DEST+1)
    :stC(CMP_DEST+2)
    :stC(CMP_FILES)
    :stC(CMP_FILES+1)
    :stC(CMP_DEAD)
    :stC(CMP_DEAD+1)

    // idx = 1 (slot 0 is the allocator record)
    lda #1
    :stC(CMP_IDX)
    lda #0
    :stC(CMP_IDX+1)

cmpM1Lp:
    :ldC(CMP_IDX+1)
    cmp #2
    bcc cmpM1Go
    jmp cmpM1Done
cmpM1Go:
    jsr cmpLoadEntry

    :ldC(CMP_SIZE)
    :stC(CMP_T0)
    :ldC(CMP_SIZE+1)
    :orC(CMP_T0)
    beq cmpM1Next               // never used

    :ldC(CMP_FLAGS)
    and #$80
    beq cmpM1Live
    ldy #CMP_DEAD
    jsr cmInc16
    jmp cmpM1Next
cmpM1Live:
    jsr cmpOffGeDest
    bcc cmpM1Corrupt            // off < dest -- layout breaks the invariant
    jsr cmpBounds               // C=1 iff off + size > capacity
    bcs cmpM1Corrupt
    jsr cmpDestAddSize
    ldy #CMP_FILES
    jsr cmInc16
cmpM1Next:
    jsr cmIncIdx
    jmp cmpM1Lp

cmpM1Done:
    // no-op fast path: nothing to move and no tombstone to purge
    :ldC(CMP_DEST)
    :cpC(CMP_OLD)
    bne cmpGo
    :ldC(CMP_DEST+1)
    :cpC(CMP_OLD+1)
    bne cmpGo
    :ldC(CMP_DEST+2)
    :cpC(CMP_OLD+2)
    bne cmpGo
    :ldC(CMP_DEAD)
    :stC(CMP_T0)
    :ldC(CMP_DEAD+1)
    :orC(CMP_T0)
    beq cmpNoop
cmpGo:
    jmp cmpPreFlight

cmpM1Corrupt:
    :cmpMsg(msgCorrupt)         // "? fs corrupt at slot "
    :ldC(CMP_IDX)
    pha
    :ldCX(CMP_IDX+1)
    pla
    jsr cmpPrintDec16
    jsr kNewline
    :cmpMsg(msgCorrupt2)        // "off $"
    ldx #CMP_OFF
    jsr cmpPrintHex24
    :cmpMsg(msgCorrupt3)        // " < $"
    ldx #CMP_DEST
    jsr cmpPrintHex24
    jsr kNewline
    jmp cmpExit

cmpNoop:
    :cmpMsg(msgNoop)
    jsr kNewline
    jmp cmpExit

// --- cmpOffGeDest -- C=1 iff CMP_OFF >= CMP_DEST (24-bit). Clobbers A, Y ---
cmpOffGeDest:
    :ldC(CMP_OFF+2)
    :cpC(CMP_DEST+2)
    bcc cogdNo
    bne cogdYes
    :ldC(CMP_OFF+1)
    :cpC(CMP_DEST+1)
    bcc cogdNo
    bne cogdYes
    :ldC(CMP_OFF)
    :cpC(CMP_DEST)
    bcc cogdNo
cogdYes:
    sec
    rts
cogdNo:
    clc
    rts

// --- cmpOffEqDest -- C=1 iff CMP_OFF == CMP_DEST (24-bit). Clobbers A, Y ---
cmpOffEqDest:
    :ldC(CMP_OFF)
    :cpC(CMP_DEST)
    bne coeqNo
    :ldC(CMP_OFF+1)
    :cpC(CMP_DEST+1)
    bne coeqNo
    :ldC(CMP_OFF+2)
    :cpC(CMP_DEST+2)
    bne coeqNo
    sec
    rts
coeqNo:
    clc
    rts

// --- cmpBounds -- C=1 iff CMP_OFF + CMP_SIZE > CMP_CAP (24-bit) ---
//  Guards against moving data outside the FS data region (which would land
//  in the bitmap-slot banks or the malloc shadow). Clobbers A, Y.
cmpBounds:
    clc
    :ldC(CMP_OFF)
    :adC(CMP_SIZE)
    :stC(CMP_DEC)               // end lo
    :ldC(CMP_OFF+1)
    :adC(CMP_SIZE+1)
    :stC(CMP_DEC+1)             // end mid
    :ldC(CMP_OFF+2)
    adc #0
    :stC(CMP_T0)                // end hi
    bcs cbdBad                  // 24-bit overflow
    :ldC(CMP_T0)
    :cpC(CMP_CAP+2)
    bcc cbdOk
    bne cbdBad
    :ldC(CMP_DEC+1)
    :cpC(CMP_CAP+1)
    bcc cbdOk
    bne cbdBad
    :ldC(CMP_DEC)
    :cpC(CMP_CAP)
    bcc cbdOk
    beq cbdOk                   // exact fit is allowed
cbdBad:
    sec
    rts
cbdOk:
    clc
    rts

// --- cmpDestAddSize -- CMP_DEST += CMP_SIZE (24-bit). Clobbers A, Y ---
cmpDestAddSize:
    clc
    :ldC(CMP_SIZE)
    :adC(CMP_DEST)
    :stC(CMP_DEST)
    :ldC(CMP_SIZE+1)
    :adC(CMP_DEST+1)
    :stC(CMP_DEST+1)
    lda #0
    :adC(CMP_DEST+2)
    :stC(CMP_DEST+2)
    rts

// --- cmInc16 -- increment the 16-bit counter at instance offset Y ---
//  (inc has no (zp),y mode: lda/adc/sta). Clobbers A, Y.
cmInc16:
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    bcc cmIncRts
    iny
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y
cmIncRts:
    rts

// --- cmIncIdx -- CMP_IDX++ (16-bit) ---
cmIncIdx:
    :ldC(CMP_IDX)
    clc
    adc #1
    :stC(CMP_IDX)
    bcc cmIdxRts
    :ldC(CMP_IDX+1)
    adc #0
    :stC(CMP_IDX+1)
cmIdxRts:
    rts

// --- cmpDirRead -- SEI-read directory page A into reuDirBuf ---
//  Clobbers A. Preserves X, Y.
cmpDirRead:
    sei
    clc
    jsr kReuDirPageDma
    cli
    rts

// --- cmpLoadEntry -- read the page holding CMP_IDX and capture that entry ---
//  Fills CMP_PG, CMP_EOFF, CMP_FLAGS, CMP_OFF, CMP_SIZE and CMP_NAME.
//  The page read is one short SEI window: the scheduler's context switch
//  drives the same REU registers, and dlPeekLibPages shares reuDirBuf.
//  Clobbers A, X, Y.
cmpLoadEntry:
    // page = (idx_lo >> 3) + (idx_hi ? 32 : 0)
    :ldC(CMP_IDX)
    lsr
    lsr
    lsr
    :stC(CMP_PG)
    :ldC(CMP_IDX+1)
    beq clePage
    :ldC(CMP_PG)
    clc
    adc #32
    :stC(CMP_PG)
clePage:
    // eoff = (idx_lo & 7) * 32
    :ldC(CMP_IDX)
    and #$07
    asl
    asl
    asl
    asl
    asl
    :stC(CMP_EOFF)
    // ---- SEI window: read the page, capture the entry ----
    sei
    :ldC(CMP_PG)
    clc
    jsr kReuDirPageDma          // X, Y preserved
    :ldCX(CMP_EOFF)
    lda reuDirBuf,x
    :stC(CMP_FLAGS)
    lda reuDirBuf+17,x
    :stC(CMP_OFF)
    lda reuDirBuf+18,x
    :stC(CMP_OFF+1)
    lda reuDirBuf+19,x
    :stC(CMP_OFF+2)
    lda reuDirBuf+20,x
    :stC(CMP_SIZE)
    lda reuDirBuf+21,x
    :stC(CMP_SIZE+1)
    inx                         // X = eoff + 1 (name field)
    ldy #CMP_NAME
cleName:
    lda reuDirBuf,x
    sta (dataPtr),y
    inx
    iny
    cpy #CMP_NAME+11
    bcc cleName
    cli
    rts

// ==========================================
//  Pre-flight report + Pass 2 setup
// ==========================================
cmpPreFlight:
    :cmpMsg(msgPre1)            // "compact: "
    :ldC(CMP_FILES)
    pha
    :ldCX(CMP_FILES+1)
    pla
    jsr cmpPrintDec16
    :cmpMsg(msgPre1b)           // " files, "
    :ldC(CMP_DEAD)
    pha
    :ldCX(CMP_DEAD+1)
    pla
    jsr cmpPrintDec16
    :cmpMsg(msgPre1c)           // " dead slots"
    jsr kNewline
    :cmpMsg(msgPre2)            // "reclaim $"
    sec
    :ldC(CMP_OLD)
    :sbC(CMP_DEST)
    :stC(CMP_MSRC)
    :ldC(CMP_OLD+1)
    :sbC(CMP_DEST+1)
    :stC(CMP_MSRC+1)
    :ldC(CMP_OLD+2)
    :sbC(CMP_DEST+2)
    :stC(CMP_MSRC+2)
    ldx #CMP_MSRC
    jsr cmpPrintHex24
    jsr kNewline
    // status-line row = the fresh row the cursor is on
    jsr kGetCursor              // X = col, Y = row
    tya
    :stC(CMP_ROW)
    // clear the pass-2 counters
    lda #0
    :stC(CMP_MOVED)
    :stC(CMP_MOVED+1)
    :stC(CMP_PURGED)
    :stC(CMP_PURGED+1)
    :stC(CMP_DONE)
    :stC(CMP_DONE+1)
    // dest = 0 ; idx = 1
    :stC(CMP_DEST)
    :stC(CMP_DEST+1)
    :stC(CMP_DEST+2)
    lda #1
    :stC(CMP_IDX)
    lda #0
    :stC(CMP_IDX+1)
    jmp cmpP2Lp

// ==========================================
//  Pass 2 -- move and re-point, one entry at a time
// ==========================================
cmpP2Lp:
    :ldC(CMP_IDX+1)
    cmp #2
    bcc cmpP2Go
    jmp cmpP2End
cmpP2Go:
    jsr cmpLoadEntry

    :ldC(CMP_SIZE)
    :stC(CMP_T0)
    :ldC(CMP_SIZE+1)
    :orC(CMP_T0)
    bne cmpP2Used               // size == 0 -> never used
    jmp cmpP2Next
cmpP2Used:
    :ldC(CMP_FLAGS)
    and #$80
    beq cmpP2Live
    jmp cmpP2Tomb               // tombstone (far -> jmp)
cmpP2Live:
    jsr cmpOffGeDest
    bcs cmpP2Ge
    jmp cmpP2Stop               // defensive: pass 1 already verified this
cmpP2Ge:
    jsr cmpOffEqDest
    bcc cmpP2Move
    jmp cmpP2Commit             // already in place
cmpP2Move:
    // stage the move (src, dst, len)
    :ldC(CMP_OFF)
    :stC(CMP_MSRC)
    :ldC(CMP_OFF+1)
    :stC(CMP_MSRC+1)
    :ldC(CMP_OFF+2)
    :stC(CMP_MSRC+2)
    :ldC(CMP_DEST)
    :stC(CMP_MDST)
    :ldC(CMP_DEST+1)
    :stC(CMP_MDST+1)
    :ldC(CMP_DEST+2)
    :stC(CMP_MDST+2)
    :ldC(CMP_SIZE)
    :stC(CMP_MLEN)
    :ldC(CMP_SIZE+1)
    :stC(CMP_MLEN+1)
    jsr cmpMove
    // re-point the entry -- SEI: read, patch, write back as ONE window
    sei
    :ldC(CMP_PG)
    clc
    jsr kReuDirPageDma
    :ldCX(CMP_EOFF)
    :ldC(CMP_DEST)
    sta reuDirBuf+17,x
    :ldC(CMP_DEST+1)
    sta reuDirBuf+18,x
    :ldC(CMP_DEST+2)
    sta reuDirBuf+19,x
    :ldC(CMP_PG)
    sec
    jsr kReuDirPageDma
    cli
    ldy #CMP_MOVED
    jsr cmInc16
cmpP2Commit:
    jsr cmpDestAddSize
    ldy #CMP_DONE
    jsr cmInc16
cmpP2Next:
    jsr cmIncIdx
    :ldC(CMP_IDX)
    and #$07
    beq cmpP2Line
    jmp cmpP2Lp                 // (jmp: cmpP2Lp is far)
cmpP2Line:
    jsr cmpShowLine             // throttled: every 8 entries
    jmp cmpP2Lp

// --- tombstone: zero the 32-byte entry (SEI: read, patch, write = one window) ---
cmpP2Tomb:
    sei
    :ldC(CMP_PG)
    clc
    jsr kReuDirPageDma
    :ldCX(CMP_EOFF)
    ldy #0
    lda #0
cmtZero:
    sta reuDirBuf,x
    inx
    iny
    cpy #32
    bcc cmtZero
    :ldC(CMP_PG)
    sec
    jsr kReuDirPageDma
    cli
    ldy #CMP_PURGED
    jsr cmInc16
    jmp cmpP2Next

cmpP2Stop:
    :cmpMsg(msgStop)
    jsr kNewline
    jmp cmpExit

// ==========================================
//  Pass 2 end -- commit nextFreeOffset, then report
// ==========================================
cmpP2End:
    // SEI window: page 0 was committed during the pass, so re-read it, set
    // nextFreeOffset = dest, and write it back.
    sei
    lda #0
    clc
    jsr kReuDirPageDma
    :ldC(CMP_DEST)
    sta reuDirBuf+17
    :ldC(CMP_DEST+1)
    sta reuDirBuf+18
    :ldC(CMP_DEST+2)
    sta reuDirBuf+19
    lda #0
    sec
    jsr kReuDirPageDma
    cli

    // replace the status line with the summary
    :ldCY(CMP_ROW)
    ldx #0
    jsr kSetCursor
    :cmpMsg(msgDone)            // "moved "
    :ldC(CMP_MOVED)
    pha
    :ldCX(CMP_MOVED+1)
    pla
    jsr cmpPrintDec16
    :cmpMsg(msgDone2)           // ", purged "
    :ldC(CMP_PURGED)
    pha
    :ldCX(CMP_PURGED+1)
    pla
    jsr cmpPrintDec16
    jsr kNewline
    // Report free space, not the allocator pointer: the slot-0 commit above
    // has already consumed CMP_DEST and neither field is live afterwards, so
    // convert both in place to what `dir` prints. free - was == reclaim.
    :cmCapFree(CMP_DEST)
    :cmCapFree(CMP_OLD)
    :cmpMsg(msgFree)            // "free $"
    ldx #CMP_DEST
    jsr cmpPrintHex24
    :cmpMsg(msgWas)             // " (was $"
    ldx #CMP_OLD
    jsr cmpPrintHex24
    :cmpMsg(msgClose)           // ")"
    jsr kNewline
    jsr kNewline

// ==========================================
//  Exit -- release the screen and terminate
// ==========================================
cmpExit:
    jsr kUnlockScreen
    lda #SYS_EXIT
    jsr syscall                 // exit -- never returns

// ==========================================
//  Status line -- built in CMP_LINE, printed in place at (0, CMP_ROW)
// ==========================================
//  Fixed 38 columns, space-filled first, so a short name never leaves
//  fragments of a longer previous one and the line can never wrap
//  (printChar wraps at col 40 and a wrap at the region bottom scrolls).
//  Redrawing is throttled by the caller: each redraw is 38 character
//  blits and the font blitter DMAs every glyph from the REU.
cmpShowLine:
    // kPtr = CMP_LINE ; space-fill 38
    lda dataPtr
    clc
    adc #CMP_LINE
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    ldy #0
    lda #$20
cslFill:
    sta (kPtr),y
    iny
    cpy #CMP_LINEW
    bcc cslFill
    lda #0
    sta (kPtr),y                // NUL: the fill loop exits with Y = CMP_LINEW
    lda #0
    :stC(CMP_CUR)
    // "DONE/FILES"
    :ldC(CMP_DONE)
    pha
    :ldCX(CMP_DONE+1)
    pla
    jsr cmpEmitDec16
    lda #'/'
    jsr cmpEmitCh
    :ldC(CMP_FILES)
    pha
    :ldCX(CMP_FILES+1)
    pla
    jsr cmpEmitDec16
    lda #$20
    jsr cmpEmitCh
    // name (already space-padded to 11 in the directory)
    lda #CMP_NAME
    :stC(CMP_T0)
cslName:
    :ldCY(CMP_T0)
    lda (dataPtr),y
    jsr cmpEmitCh
    :inC(CMP_T0)
    :ldC(CMP_T0)
    cmp #CMP_NAME+11
    bcc cslName
    // " $DEST"
    lda #$20
    jsr cmpEmitCh
    lda #'$'
    jsr cmpEmitCh
    ldx #CMP_DEST
    jsr cmpEmitHex24
    // print the 38-char line in place
    :ldCY(CMP_ROW)
    ldx #0
    jsr kSetCursor
    lda dataPtr
    clc
    adc #CMP_LINE
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    jsr kPrintString
    rts

// --- cmpEmitCh -- append A to the line at CMP_CUR. Clobbers A, Y ---
cmpEmitCh:
    pha
    :ldCY(CMP_CUR)              // Y = cursor
    pla
    sta (kPtr),y
    :inC(CMP_CUR)
    rts

// --- cmpEmitStr -- append the NUL-terminated string at CMP_STR to the line ---
//  Clobbers A, X, Y.
cmpEmitStr:
    lda #CMP_STR
    :stC(CMP_T1)
cesLp:
    :ldCY(CMP_T1)
    lda (dataPtr),y
    beq cesRts
    jsr cmpEmitCh
    :inC(CMP_T1)
    jmp cesLp
cesRts:
    rts

// --- cmpEmitDec16 -- A(lo)/X(hi) -> decimal, appended to the line ---
cmpEmitDec16:
    :stC(CMP_DEC)
    txa
    :stC(CMP_DEC+1)
    jsr cmpDec16
    jmp cmpEmitStr

// --- cmpEmitHex24 -- append 6 hex chars for the field at instance offset X ---
cmpEmitHex24:
    jsr cmpHex24Str
    jmp cmpEmitStr

// --- cmpPrintHex24 -- print 6 hex chars for the field at instance offset X ---
cmpPrintHex24:
    jsr cmpHex24Str
    jmp cmpPrintStr

// --- cmpHex24Str -- X = field offset (lo/mid/hi) -> CMP_STR = 6 hex + NUL ---
//  Clobbers A, X, Y. (cmpHexByte and cmpHexPut preserve CMP_T2.)
cmpHex24Str:
    txa
    :stC(CMP_T2)                // field offset
    lda #CMP_STR
    :stC(CMP_T1)                // output cursor
    :ldC(CMP_T2)
    clc
    adc #2
    tay
    lda (dataPtr),y             // hi byte
    jsr cmpHexByte
    :ldC(CMP_T2)
    clc
    adc #1
    tay
    lda (dataPtr),y             // mid byte
    jsr cmpHexByte
    :ldCY(CMP_T2)
    lda (dataPtr),y             // lo byte
    jsr cmpHexByte
    :ldCY(CMP_T1)
    lda #0
    sta (dataPtr),y             // NUL-terminate at the output cursor
    rts

// --- cmpHexByte -- append A as 2 hex chars to CMP_STR. Clobbers A, X, Y ---
cmpHexByte:
    pha
    lsr
    lsr
    lsr
    lsr
    tax
    lda cmpHexChars,x
    jsr cmpHexPut
    pla
    and #$0f
    tax
    lda cmpHexChars,x
    jmp cmpHexPut

// --- cmpHexPut -- store A at CMP_STR[CMP_T1], advance ---
cmpHexPut:
    pha                          // save the hex char -- ldCY clobbers A
    :ldCY(CMP_T1)
    pla
    sta (dataPtr),y
    :inC(CMP_T1)
    rts

// ==========================================
//  cmMove -- copy CMP_MLEN bytes from CMP_MSRC down to CMP_MDST through the
//  CMP_BUF bounce buffer. Downward moves only (dst <= src) and forward
//  chunk order: a whole chunk is read into the buffer before it is written
//  back, so a write can never overtake unread source bytes. Interrupts stay
//  ENABLED (kReuToC64/kReuFromC64 self-SEI); each chunk is clamped so it
//  cannot cross a bank, keeping every transfer a single DMA.
//  Clobbers A, X, Y, kPtr, kArg..kArg+5.
// ==========================================
cmpMove:
cmvLoop:
    :ldC(CMP_MLEN)
    :stC(CMP_T0)
    :ldC(CMP_MLEN+1)
    :orC(CMP_T0)
    bne cmvGo                   // len == 0 -> done
    rts
cmvGo:
    // chunk = CMP_BUFSZ (or len if smaller)
    lda #CMP_BUFSZ
    :stC(CMP_CHUNK)
    lda #0
    :stC(CMP_CHUNK+1)
    :ldC(CMP_MLEN+1)
    bne cmvChunk                // len >= 256 -> keep 128
    :ldC(CMP_MLEN)
    cmp #CMP_BUFSZ
    bcs cmvChunk
    :stC(CMP_CHUNK)
cmvChunk:
    ldx #CMP_MSRC
    jsr cmvBank                 // clamp to the end of the source bank
    ldx #CMP_MDST
    jsr cmvBank                 // clamp to the end of the destination bank
    // ---- read: REU -> C64 ----
    ldx #CMP_MSRC
    jsr cmvSetArgs
    :ldC(CMP_CHUNK)
    sta kArg+3
    :ldC(CMP_CHUNK+1)
    sta kArg+4
    jsr cmvBufPtr
    jsr kReuToC64
    // ---- write: C64 -> REU ----
    ldx #CMP_MDST
    jsr cmvSetArgs
    :ldC(CMP_CHUNK)
    sta kArg+3
    :ldC(CMP_CHUNK+1)
    sta kArg+4
    lda #0
    sta kArg+5
    jsr cmvBufPtr
    jsr kReuFromC64
    // ---- src += chunk ----
    clc
    :ldC(CMP_CHUNK)
    :adC(CMP_MSRC)
    :stC(CMP_MSRC)
    :ldC(CMP_CHUNK+1)
    :adC(CMP_MSRC+1)
    :stC(CMP_MSRC+1)
    lda #0
    :adC(CMP_MSRC+2)
    :stC(CMP_MSRC+2)
    // ---- dst += chunk ----
    clc
    :ldC(CMP_CHUNK)
    :adC(CMP_MDST)
    :stC(CMP_MDST)
    :ldC(CMP_CHUNK+1)
    :adC(CMP_MDST+1)
    :stC(CMP_MDST+1)
    lda #0
    :adC(CMP_MDST+2)
    :stC(CMP_MDST+2)
    // ---- len -= chunk ----
    sec
    :ldC(CMP_MLEN)
    :sbC(CMP_CHUNK)
    :stC(CMP_MLEN)
    :ldC(CMP_MLEN+1)
    :sbC(CMP_CHUNK+1)
    :stC(CMP_MLEN+1)
    jmp cmvLoop

// --- cmvBufPtr -- kPtr = CMP_BUF ---
cmvBufPtr:
    lda dataPtr
    clc
    adc #CMP_BUF
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    rts

// --- cmvSetArgs -- X = instance offset of a 3-byte FS offset field ---
//  Sets kArg/kArg+1/kArg+2 = REU addr lo/hi/bank for that offset.
//  Clobbers A, Y. Preserves X.
cmvSetArgs:
    txa
    :stC(CMP_T0)
    :ldCY(CMP_T0)
    lda (dataPtr),y             // offset lo -> REU addr lo
    sta kArg
    iny
    lda (dataPtr),y             // offset mid
    ldy #CMP_DBHI
    clc
    adc (dataPtr),y             // REU addr hi = mid + DBHI  (C = carry out)
    sta kArg+1
    lda #0
    adc #0                      // A = the carry (0/1)
    :stC(CMP_T1)
    :ldCY(CMP_T0)
    iny
    iny
    lda (dataPtr),y             // offset hi
    ldy #CMP_DBBANK
    clc
    adc (dataPtr),y             // bank = offset hi + DBBANK
    sta kArg+2
    :ldC(CMP_T1)
    beq cmvSaRts
    inc kArg+2                  // plus the carry out of the mid byte
cmvSaRts:
    rts

// --- cmvBank -- X = instance offset of a 3-byte FS offset field ---
//  Clamps CMP_CHUNK so the transfer cannot cross that field's bank edge.
//  inBank = (offset.lo, offset.mid + DBHI); if inBank == 0 there is a full
//  64KB left so no clamp is needed, otherwise chunk = min(chunk, -inBank).
//  Clobbers A, Y. Preserves X.
cmvBank:
    txa
    :stC(CMP_T0)
    :ldCY(CMP_T0)
    lda (dataPtr),y             // inBank lo
    :stC(CMP_T1)
    iny
    lda (dataPtr),y             // offset mid
    ldy #CMP_DBHI
    clc
    adc (dataPtr),y             // inBank hi
    :stC(CMP_T2)
    :ldC(CMP_T1)
    :stC(CMP_T0)                // (T0 reused -- the field offset is done with)
    :ldC(CMP_T2)
    :orC(CMP_T0)
    beq cmvBkRts                // inBank == 0 -> no clamp
    // -inBank (16-bit)
    sec
    lda #0
    :sbC(CMP_T0)
    :stC(CMP_T1)                // negated inBank lo
    lda #0
    :sbC(CMP_T2)                // negated inBank hi
    bne cmvBkRts                // >= 256 > chunk -> no clamp
    :ldC(CMP_T1)
    :cpC(CMP_CHUNK)
    bcs cmvBkRts
    :stC(CMP_CHUNK)
    lda #0
    :stC(CMP_CHUNK+1)
cmvBkRts:
    rts

// ==========================================
//  Strings (screencode_mixed: a-z = $01-$1A)
// ==========================================
.encoding "screencode_mixed"
cmsgNoFs:     .text "no fs"; .byte 0
msgBusy:      .text "? busy - "; .byte 0
msgBusy2:     .text " task(s) must be killed"; .byte 0
msgMore2:     .text " more (see ps)"; .byte 0
msgId:        .text " (id "; .byte 0
msgClose:     .text ")"; .byte 0

msgCorrupt:   .text "? fs corrupt at slot "; .byte 0
msgCorrupt2:  .text "off $"; .byte 0
msgCorrupt3:  .text " < $"; .byte 0
msgNoop:      .text "already compact - nothing to reclaim"; .byte 0

msgPre1:      .text "compact: "; .byte 0
msgPre1b:     .text " files, "; .byte 0
msgPre1c:     .text " dead slots"; .byte 0
msgPre2:      .text "reclaim $"; .byte 0

msgStop:      .text "? stopped - rerun to finish"; .byte 0
msgDone:      .text "moved "; .byte 0
msgDone2:     .text ", purged "; .byte 0
msgFree:      .text "free $"; .byte 0
msgWas:       .text " (was $"; .byte 0

cmpHexChars:  .text "0123456789abcdef"
