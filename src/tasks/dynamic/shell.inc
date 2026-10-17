// ==========================================
//  GordonOS --  shell / Console task
// ==========================================
//  On startup: prints OS banner + " [x]"
//  where x is the task ID, then enters the REPL.
//  Block-cursor input with line buffer.
//  On RETURN, passes accumulated input to
//  processCommand for interpretation.
//
//  Re-entrant: all mutable state at (dataPtr),y offsets.
//  dataPtr ($F1-$F2) is saved/restored per-task by scheduler.

// --- Data buffer offsets (105 bytes total) ---
.const LINEBUF  = 0      // 80 bytes: line input buffer
.const BATERR   = 80     // 1 byte:  batch error flag (0=ok, 1=error)
.const PCSLOT   = 81     // 1 byte:  resolved task slot
.const DECBUF   = 82     // 5 bytes: decimal/hex conversion buffer
.const CMDBASE  = 87     // 2 bytes: cmds/messages block base (runtime)
.const CODEBASE = 89     // 2 bytes: own code base (runtime)
.const BATPOS   = 91     // 2 bytes: batch position within the file
.const BATSIZE  = 93     // 2 bytes: batch file size
.const BATOFF   = 95     // 2 bytes: batch file FS data offset
.const POOLID   = 97     // 1 byte:  pool listing -- owner task id
.const POOLPG   = 98     // 1 byte:  pool listing -- start page
.const POOLLEN  = 99     // 1 byte:  pool listing -- page count
.const POOLM    = 100    // 1 byte:  pool listing -- 0=task, 1=malloc, 2=lib
.const POOLRS   = 103    // 1 byte:  pool listing -- residency records already counted
.const BATOFF2  = 101    // 1 byte:  batch file FS data offset hi (24-bit)
.const BATHI    = 102    // 1 byte:  runbatch directory scan slot hi
.const PCFG     = 104    // 1 byte:  foreground .com child slot (pcFgSpawned/pcFgWait)

//  The instance buffer must cover the whole offset block above. This is the
//  check that keeps SHELL_DATA_SIZE (shell.asm, emitted in the header) honest:
//  add an offset without enlarging the buffer and the build stops here.
.assert "shell: data size covers the offset block", SHELL_DATA_SIZE, PCFG + 1

//  pcPool's shared-record dedup packs one bit per residency record into the
//  single-byte POOLRS bitmask (pcPoolDynBit) -- MAX_RESIDENTS must stay <= 8
//  or the bitmask silently wraps and two records alias the same bit.
.assert "shell: MAX_RESIDENTS fits POOLRS's 1-byte bitmask", (MAX_RESIDENTS <= 8) ? 1 : 0, 1

.const CMD_COUNT     = 14     // internal command-table entries -- any
                              // other word falls through to the ".com"
                              // loader path (pcFgPs) via pcUnknown
// MAX_MALLOC_BLOCKS, MAX_LIBS, dyn flags: src/kernel/kernel-shared.inc /
// src/tasks/kernel-data.inc (imported globally)

// --- printMsg(off) -- print the string at cmdsBlock + off ---
//  Inline macro: computes kPtr from the runtime block base + a
//  compile-time offset. Zero fixups (every offset is an immediate), so the
//  table costs no relocation work at load. Clobbers A, X, Y, kPtr, kArg
//  (kPrintString clobbers X and kArg) -- identical to the
//  `lda #<x / lda #>x / jsr kPrintString` sequence it replaces.
.macro printMsg(off) {
    ldy #CMDBASE
    lda (dataPtr),y
    clc
    adc #<off
    sta kPtr
    iny
    lda (dataPtr),y
    adc #>off
    sta kPtr+1
    jsr kPrintString
}

shell:
    // Runtime bases for the fixup-free string/dispatch tables -- set
    // FIRST so even the no-screen error path can print (printMsg
    // reads cmdsBase; the instance buffer is zeroed by spawnCore).
    ldy #CMDBASE
    lda #<cmdsBlock; sta (dataPtr),y
    iny
    lda #>cmdsBlock; sta (dataPtr),y
    ldy #CODEBASE
    lda #<shell; sta (dataPtr),y
    iny
    lda #>shell; sta (dataPtr),y
    // gettid first -- both the fresh-spawn and the restored path need it
    lda #SYS_GETTID; jsr syscall         // gettid -> A
    ldy #DECBUF
    sta (dataPtr),y             // save task ID for later
    tax
    lda screenAlloc,x
    beq shellAllocScreen
    jmp shellKeepScreen         // cold restart -- surface was preserved
shellAllocScreen:
    // surface colors (all-bitmap: kAllocScreen takes border/bg0/bg1/bg2 in kArg)
    lda #$0b; sta kArg           // border = dark grey (DEFAULT_BORDER_COLOR)
    lda #$00
    sta kArg+1                   // bg0 = black (DEFAULT_BG_COLOR)
    sta kArg+2                   // bg1 (unused for hires)
    sta kArg+3                   // bg2 (unused for hires)
    lda #0; jsr kAllocScreen     // hires-text surface
    bcc shellHasScreen
    // no screens available -- print error and halt this instance
    lda #$00; sta screenBase
    lda #$20; sta screenBase+1       // safe fallback for error print
    :printMsg(msgNoScreen - cmdsBlock)
shellHalt:
    lda #SYS_YIELD; jsr syscall               // yield forever
    jmp shellHalt
shellHasScreen:
    // (the OS name/version banner is the built-in `ver` command)

    // Print task name + " [x]" (shared with the built-in `me` command)
    jsr pcMe

    // Switch to this screen
    ldy #DECBUF
    lda (dataPtr),y             // restore task ID
    tax
    jsr kSwitchView
    jsr kEnableCursor
    // Startup command -- "run shell <cmd>" delivers <cmd> into the line
    // buffer (KF_TAKES_ARGS); execute it once, then enter the prompt.
    ldy #LINEBUF
    lda (dataPtr),y
    beq shellMain
    jsr processCommand
    jmp shellMain
shellKeepScreen:
    // Restored instance -- its surface (and content) survived eviction
    // and the loader handed the display back. Don't allocate a new
    // surface and don't steal the view; startup args are gone (restore
    // re-zeroes the instance buffer), so land directly on the prompt.
    // The eviction froze the surface-tail cursor at the last scheduler
    // preemption -- mid-command (e.g. right after the typed "run basic"
    // line). Park it on a fresh row: the pre-eviction blank lines are
    // already on screen. Safe in background too (no scroll there).
    jsr kNewline
    jsr kEnableCursor
shellMain:
    // kPtr = dataPtr + LINEBUF (pointer to line buffer)
    lda dataPtr; clc; adc #LINEBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    ldx #79                     // 80-char buffer (2 screen lines)
    jsr kReadString             // Y = length, buffer null-terminated
    cpy #0
    bne shellHasInput
    lda #SYS_YIELD
    jsr syscall                 // yield if not focused / empty
    jmp shellMain
shellHasInput:
    jsr kNewline
    jsr kNewline                // blank line before command output
    jsr processCommand
    jsr waitRelease
    jmp shellMain

// --- processCommand -- interpret line at (dataPtr)+LINEBUF ---
processCommand:
    ldy #LINEBUF
    lda (dataPtr),y
    bne pcNotEmpty
    jmp pcRts
pcNotEmpty:
    ldy #BATERR
    lda #0
    sta (dataPtr),y             // batch: clear error flag
    ldx #0
pcTry:
    cpx #CMD_COUNT
    bcs pcUnknown
    // kPtr = cmdsBase + cmdOffTab[x]. Command strings live in the
    // first 256 bytes of cmdsBlock, so offsets fit in one byte and
    // this loop is fixup-free.
    lda cmdOffTab,x
    clc
    ldy #CMDBASE
    adc (dataPtr),y
    sta kPtr
    lda #0
    iny
    adc (dataPtr),y
    sta kPtr+1
    jsr strCmp                 // clobbers A,Y only -- X survives
    bcs cmdFound
    inx
    bne pcTry
pcUnknown:
    // Not an internal command -- treat the first word as a "<word>.com"
    // command on the REU FS (pcFgPs loads + runs + waits). Blank or
    // space-only lines return silently (an empty word would become
    // ".com").
    ldy #0
    lda (dataPtr),y
    beq pcRts
    cmp #$20
    beq pcRts
    jmp pcFgPs
pcRts:
    rts
cmdFound:
    jmp cmdDispatch

// --- pcFgPs -- run the first word as an external .com command ---
//  Entry point for any non-internal command word (ps, kill, time, dir,
//  banner, user-saved .com files...). Null-terminates the word, copies
//  args to cmdArgsBuf, appends ".com", kDynLoads it, links taskParent
//  so the child can share our screen, waits for it to exit. Errors
//  reuse pcRun's rfErr dispatch.
pcFgPs:
    ldy #0
pcFgTerm:
    lda (dataPtr),y
    beq pcFgNoArgs             // end of line -- no args
    cmp #$20
    beq pcFgHasArgs
    iny
    cpy #12
    bcc pcFgTerm
pcFgNoArgs:
    tya
    sta kArg+4                 // word length -- a kArg scratch is fine HERE: the
                               // next section reads it before any call, so no
                               // yield, DMA or kernel call can intervene
    jmp pcFgArgsDone           // no args -- share the empty-args tail
pcFgHasArgs:
    tya
    sta kArg+4                 // word length (as above)
    lda #0
    sta (dataPtr),y            // null-terminate word
    iny                        // Y = first arg char
pcFgSkip:
    lda (dataPtr),y
    beq pcFgArgsDone
    cmp #$20
    bne pcFgCopy
    iny
    bne pcFgSkip
pcFgArgsDone:
    lda #0
    sta cmdArgsBuf             // empty args (none, or only spaces)
    jmp pcFgAppend
pcFgCopy:
    ldx #0
pcFgCopyLp:
    lda (dataPtr),y
    beq pcFgCopyDone
    sta cmdArgsBuf,x
    inx
    iny
    cpx #37
    bcc pcFgCopyLp
pcFgCopyDone:
    lda #0
    sta cmdArgsBuf,x
pcFgAppend:
    ldy kArg+4                 // word length
    lda #'.'
    sta (dataPtr),y
    iny
    lda #'c'
    sta (dataPtr),y
    iny
    lda #'o'
    sta (dataPtr),y
    iny
    lda #'m'
    sta (dataPtr),y
    iny
    lda #0
    sta (dataPtr),y
    iny
    lda #0
    sta (dataPtr),y            // empty args field -- stale tail must not
                               // be mistaken for args
    lda dataPtr; sta kPtr        // kPtr = lineBuf ("<name>.com")
    lda dataPtr+1; sta kPtr+1
    jsr kDynLoad
    bcc pcFgSpawned
    jmp rfErr                    // error -- reuse run's dispatch
pcFgSpawned:
    txa                          // A = child slot (kDynLoad returns it in X)
    ldy #PCFG
    sta (dataPtr),y              // per-instance: kArg is per-call scratch, so a
                                 // slot that has to survive to the next call
                                 // cannot live there
    lda #SYS_GETTID
    jsr syscall                  // gettid -> A = own slot; X = child (preserved)
    sta taskParent,x             // taskParent[child] = own slot
pcFgWait:
    ldy #PCFG
    lda (dataPtr),y
    tax                          // X = child slot
    lda taskState,x
    beq pcFgDone
    lda #SYS_YIELD
    jsr syscall                  // yield -- never tight-loop (Rule 1)
    jmp pcFgWait
pcFgDone:
    rts

pcHelp:
    :printMsg(msgHelp - cmdsBlock)
    jsr kNewline
    jsr kNewline
    rts

// --- pcVer -- print the OS name + version (the startup banner) ---
pcVer:
    lda #<osName; sta kPtr
    lda #>osName; sta kPtr+1
    jsr kPrintString
    lda #$20; jsr kPrintChar   // space
    lda #<osVersion; sta kPtr
    lda #>osVersion; sta kPtr+1
    jsr kPrintString
    jsr kNewline
    jsr kNewline
    rts

// --- pcMe -- print own task name + id: "shell [x]" ---
//  The same line the shell prints on startup (shellHasScreen jsr's
//  here). Fetches a FRESH gettid -- DECBUF+0 is scratch that other
//  commands overwrite (decimal/hex conversions, runbatch lengths),
//  so it cannot be trusted after startup. Zero new fixups.
pcMe:
    lda #SYS_GETTID
    jsr syscall                  // gettid -> A (fresh, X preserved)
    ldy #DECBUF
    sta (dataPtr),y              // also refreshes startup's saved copy
    tax
    jsr kGetTaskName            // kPtr = task name (X preserved)
    jsr kPrintString
    lda #$20; jsr kPrintChar   // space
    lda #'['; jsr kPrintChar
    lda dataPtr
    clc
    adc #DECBUF+1
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1                  // kPtr = decBuf+1 (3 digits + null)
    ldy #DECBUF
    lda (dataPtr),y             // task ID -> A
    jsr kByteToDec              // decimal at (kPtr), Y=length
    lda #0
    sta (kPtr),y                // null-terminate for kPrintString
    jsr kPrintString
    lda #']'; jsr kPrintChar
    jsr kNewline
    jsr kNewline
    rts

// --- pcPrint -- echo the rest of the line (print <text>) ---
pcPrint:
    ldy #5                      // skip "print"
pcPrSkSp:
    lda (dataPtr),y
    beq pcPrNewl                // no argument -- just a blank line
    cmp #$20
    bne pcPrArg
    iny
    bne pcPrSkSp
pcPrArg:
    // kPtr = lineBuf + Y (argument start, printed verbatim)
    tya
    clc
    adc dataPtr
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    jsr kPrintString
pcPrNewl:
    jsr kNewline
    jsr kNewline
    rts

// --- pcView -- switch VIC to task's screen: view <id> ---
pcView:
    ldy #4
    lda (dataPtr),y; beq pcViewRts  // no arg -- silent no-op
    jsr parseIntArg; bcc pcViewGo2
    jmp pcViewBad
pcViewGo2:
    ldy #PCSLOT; lda (dataPtr),y; tax
    lda screenAlloc,x
    bne pcViewGo                // has a screen, proceed
    :printMsg(msgViewNoScr - cmdsBlock)
    jsr kNewline; jsr kNewline
    rts
pcViewGo:
    :printMsg(msgViewOk - cmdsBlock)
    lda #$20; jsr kPrintChar
    ldy #PCSLOT; lda (dataPtr),y; tax
    jsr kGetTaskName; jsr kPrintString
    jsr kNewline; jsr kNewline
    ldy #PCSLOT; lda (dataPtr),y; tax
    jsr kSwitchView             // switch display + transfer keyboard focus
pcViewRts:
    rts
pcViewBad:
    ldy #BATERR
    lda #1
    sta (dataPtr),y
    :printMsg(msgViewBad - cmdsBlock)
    jsr kNewline; jsr kNewline
    rts

// (pcApps + the known-task registry died with the static shell;
//  "run" is the former runf path.)

pcClear:
    jsr kClearScreen
    rts

pcExit:
    jsr kFreeScreen             // release screen before exit
    lda #SYS_EXIT
    jsr syscall
    // never returns

// --- parseDecimal -- parse decimal number from lineBuf+Y into pcSlot ---
//  Input:  Y = offset into lineBuf (after command word)
//  Output: pcSlot = parsed value (0-255), Y advanced past last digit
//          carry clear = success, carry set = no valid number
//  Clobbers: A, kArg, kArg+2
parseDecimal:
    // skip leading spaces
pdSkipSp:
    lda (dataPtr),y
    beq pdBad
    cmp #$20; bne pdFirst
    iny; bne pdSkipSp
pdFirst:
    sec; sbc #'0'; bcc pdBad
    cmp #10; bcs pdBad
    sty kArg+2                  // save lineBuf Y
    lda #0
    ldy #PCSLOT
    sta (dataPtr),y             // result = 0
    ldy kArg+2                  // restore lineBuf Y
pdLoop:
    lda (dataPtr),y
    beq pdDone
    cmp #$20; beq pdDone
    sec; sbc #'0'; bcc pdBad
    cmp #10; bcs pdBad
    sta kArg                    // save digit
    sty kArg+2                  // save lineBuf Y
    ldy #PCSLOT
    lda (dataPtr),y             // result
    asl; asl; clc; adc (dataPtr),y  // x5
    asl; clc; adc kArg          // x10 + digit
    sta (dataPtr),y
    ldy kArg+2                  // restore lineBuf Y
    iny
    bne pdLoop
pdDone:
    clc
    rts
pdBad:
    sec
    rts

// --- parseIntArg -- parse task ID arg, skip spaces, validate slot ---
//  Input:  Y = offset into lineBuf (after command word)
//  Output: X = task slot in pcSlot, carry clear = valid alive task
//          carry set + kArg+3 = error ($ff=not found, else bad)
//  Prints nothing -- caller handles errors.
parseIntArg:
    jsr parseDecimal
    bcc piaChk
    lda #$ff; sta kArg+3; sec; rts
piaChk:
    sty kArg+2                  // save lineBuf Y from parseDecimal
    ldy #PCSLOT; lda (dataPtr),y; tax
    ldy kArg+2                  // restore lineBuf Y
    cpx totalTasks; bcs piaBad
    lda taskState,x; beq piaBad
    clc; rts
piaBad:
    sec; rts

// --- strCmp -- compare (kPtr) with lineBuf, skip trailing space ---
//  Returns carry set if match, clear if not.
//  Clobbers: A, Y
strCmp:
    ldy #0
stLoop:
    lda (kPtr),y
    beq stCheckEnd
    cmp (dataPtr),y             // lineBuf at offset Y (LINEBUF=0)
    bne stFail
    iny
    bne stLoop
stCheckEnd:
    lda (dataPtr),y             // lineBuf at offset Y
    cmp #$20
    beq stOk
    cmp #0
    beq stOk
stFail:
    clc
    rts
stOk:
    sec
    rts

// --- cmdDispatch -- computed jump to the handler for command X ---
//  Input: X = command index (set by the dispatcher loop)
//  handler = codeBase + handlerOffTab[x] -- offsets are compile-time
//  constants, so this whole mechanism is fixup-free (one jmp in).
cmdDispatch:
    txa
    asl
    tax
    lda handlerOffTab,x
    sta kArg
    lda handlerOffTab+1,x
    sta kArg+1
    ldy #CODEBASE
    lda (dataPtr),y
    clc
    adc kArg
    sta kPtr
    iny
    lda (dataPtr),y
    adc kArg+1
    sta kPtr+1
    jmp (kPtr)                 // handler's RTS returns to the caller
                               // of processCommand
handlerOffTab:
    .word pcHelp - shell      // "help"
    .word pcClear - shell     // "clear"
    .word pcRun - shell       // "run"
    .word pcView - shell      // "view"
    .word pcExit - shell      // "exit"
    .word pcFsinfo - shell    // "fsinfo"
    .word pcSave - shell      // "save"
    .word pcLoad - shell      // "load"
    .word pcPrint - shell     // "print"
    .word pcRunbatch - shell  // "runbatch"
    .word pcPool - shell      // "pool"
    .word pcVer - shell       // "ver"
    .word pcMe - shell        // "me"
    .word pcWait - shell      // "wait"
cmdOffTab:
    .byte cmdHelp - cmdsBlock
    .byte cmdClear - cmdsBlock
    .byte cmdRun - cmdsBlock
    .byte cmdView - cmdsBlock
    .byte cmdExit - cmdsBlock
    .byte cmdFsinfo - cmdsBlock
    .byte cmdSave - cmdsBlock
    .byte cmdLoad - cmdsBlock
    .byte cmdPrint - cmdsBlock
    .byte cmdRunbatch - cmdsBlock
    .byte cmdPool - cmdsBlock
    .byte cmdVer - cmdsBlock
    .byte cmdMe - cmdsBlock
    .byte cmdWait - cmdsBlock

// --- pcWait -- print the prompt, wait for any keypress, then return ---
//  kReadKey returns 0 when no key is pending (or the shell doesn't own
//  keyboard focus), so the loop yields to the scheduler instead of
//  busy-spinning. shellMain's waitRelease absorbs the key-up afterwards.
pcWait:
    :printMsg(msgPressKey - cmdsBlock)
pcWaitLoop:
    jsr kReadKey
    bne pcWaitKey
    lda #SYS_YIELD
    jsr syscall                // yield -- never tight-loop
    jmp pcWaitLoop
pcWaitKey:
    jsr kNewline
    jsr kNewline
    rts

// --- pcFsinfo -- filesystem info ---
pcFsinfo:
    lda reuFsEnabled
    cmp #$A5
    beq pcfActive
    jmp pcfNoFs
pcfActive:
    // FS active -- print info
    :printMsg(msgFsActive - cmdsBlock)
    // Print bank count (16-bit) as 4 hex digits
    lda #'$'; jsr kPrintChar
    lda dataPtr; clc; adc #DECBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    lda reuTotalBanks+1; jsr kByteToHex
    lda dataPtr; clc; adc #DECBUF+2; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    lda reuTotalBanks; jsr kByteToHex
    lda dataPtr; clc; adc #DECBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    ldy #4
    lda #0; sta (kPtr),y
    jsr kPrintString
    // Print size
    :printMsg(msgSize - cmdsBlock)
    // Get size from slot 0: capacity - nextFreeOffset
    lda dataPtr; clc; adc #LINEBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    lda #0; ldx #0; jsr kReuDirEntry   // slot 0 -> kArg+3/+4/+5 = nextFreeOffset
    bcs pcfDone                 // slot 0 unreadable (or filesys.lib failed --
                                //   that message is out already): report no size
                                //   rather than a figure built from leftovers
    // free = reuFsCapacity(3) - nextFreeOffset(3); build the 6 hex chars
    // (MSB first) in LINEBUF, then print once -- kPrintString clobbers kArg.
    sec
    lda reuFsCapacityLo; sbc kArg+3; sta kArg+3
    lda reuFsCapacityHi; sbc kArg+4; sta kArg+4
    lda reuFsCapacity2;  sbc kArg+5; sta kArg+5
    lda dataPtr; sta kPtr
    lda dataPtr+1; sta kPtr+1
    lda kArg+5; jsr kByteToHex           // +0,+1
    lda dataPtr; clc; adc #2; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    lda kArg+4; jsr kByteToHex           // +2,+3
    lda dataPtr; clc; adc #4; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    lda kArg+3; jsr kByteToHex           // +4,+5
    lda dataPtr; sta kPtr
    lda dataPtr+1; sta kPtr+1
    lda #0; ldy #6; sta (kPtr),y         // null at +6
    jsr kPrintString
pcfDone:
    jsr kNewline; jsr kNewline
    rts
pcfNoFs:
    :printMsg(msgNoFs - cmdsBlock)
    jsr kNewline; jsr kNewline
    rts

// --- pcPool -- dynamic-loader memory pool status ---
pcPool:
    // Used/free are accumulated from the rows below (tasks, program-RAM,
    // malloc, libraries) into PCSLOT and printed at the BOTTOM, so the
    // totals always match the listing above them.
    ldy #PCSLOT
    lda #0
    sta (dataPtr),y
    ldy #POOLRS
    sta (dataPtr),y
    // Header: "$base-$last, N pages" computed from POOL_BASE/POOL_LAST/POOL_PAGES
    lda #'$'; jsr kPrintChar
    lda #>POOL_BASE; jsr dyHex
    lda #<POOL_BASE; jsr dyHex
    lda #'-'; jsr kPrintChar
    lda #'$'; jsr kPrintChar
    lda #>POOL_LAST; jsr dyHex
    lda #<POOL_LAST; jsr dyHex
    lda #','; jsr kPrintChar
    lda #$20; jsr kPrintChar
    lda #POOL_PAGES
    jsr pcDecA
    :printMsg(msgPoolPages - cmdsBlock)
    jsr kNewline
    jsr kNewline
    :printMsg(msgPoolHdr2 - cmdsBlock)
    jsr kNewline
    :printMsg(msgListSep - cmdsBlock)
    jsr kNewline
    jsr kNewline
    // code regions (dyn tasks)
    ldx #0
pcPoolDynLp:
    cpx #MAX_DYN_TASKS
    bcs pcPoolDynDone
    lda dynTaskId,x
    cmp #$ff
    beq pcPoolDynNext
    lda dynTaskFlags,x
    and #DYN_FLAG_SWAPPED
    bne pcPoolDynNext           // swapped out -- no pool pages
    lda dynTaskId,x
    ldy #POOLID; sta (dataPtr),y
    txa
    pha                          // save dyn slot (pcRegLine clobbers X)
    lda dynTaskResidency,x
    cmp #$ff
    beq pcPoolDynSingle
    tax                          // X = residency index
    lda resStartPage,x
    ldy #POOLPG; sta (dataPtr),y
    lda resPageCount,x
    ldy #POOLLEN; sta (dataPtr),y
    // Shared reentrant code: count the pages once per residency record.
    // Bit = 1 << resIdx marks the record in POOLRS; extra instances add 0.
    ldy #1
pcPoolDynBit:
    dex
    bmi pcPoolDynBitOk
    tya
    asl
    tay
    jmp pcPoolDynBit
pcPoolDynBitOk:
    tya                          // A = bit mask
    pha                          // save it across the test
    ldy #POOLRS
    and (dataPtr),y
    bne pcPoolDynSeen            // already counted -> 0 pages
    pla                          // A = bit mask
    ldy #POOLRS
    ora (dataPtr),y
    sta (dataPtr),y              // mark this residency counted
    jmp pcPoolDynRow
pcPoolDynSeen:
    pla                          // discard bit mask
    ldy #POOLLEN
    lda #0
    sta (dataPtr),y              // shared pages already counted -- show 0
    jmp pcPoolDynRow
pcPoolDynSingle:
    lda dynTaskStartPage,x       // X still = dyn slot
    ldy #POOLPG; sta (dataPtr),y
    lda dynTaskPageCount,x
    ldy #POOLLEN; sta (dataPtr),y
pcPoolDynRow:
    ldy #POOLM; lda #0; sta (dataPtr),y
    jsr pcPoolAdd
    jsr pcRegLine
    pla
    tax                          // restore dyn slot
pcPoolDynNext:
    inx
    bne pcPoolDynLp
pcPoolDynDone:
    // program-RAM blocks (KF_MALLOC_RAM) -- one per dyn slot
    ldx #0
pcPoolRamLp:
    cpx #MAX_DYN_TASKS
    bcs pcPoolRamDone
    lda dynTaskId,x
    cmp #$ff
    beq pcPoolRamNext
    lda dynTaskRamPages,x
    beq pcPoolRamNext
    lda dynTaskRamStartPage,x
    cmp #$ff
    beq pcPoolRamNext           // swapped out -- no pool pages
    lda dynTaskId,x
    ldy #POOLID; sta (dataPtr),y
    lda dynTaskRamStartPage,x
    ldy #POOLPG; sta (dataPtr),y
    lda dynTaskRamPages,x
    ldy #POOLLEN; sta (dataPtr),y
    ldy #POOLM; lda #1; sta (dataPtr),y
    jsr pcPoolAdd
    txa
    pha                          // save dyn slot (pcRegLine clobbers X)
    jsr pcRegLine
    pla
    tax
pcPoolRamNext:
    inx
    bne pcPoolRamLp
pcPoolRamDone:
    // malloc regions
    ldx #0
pcPoolMallocLp:
    cpx #MAX_MALLOC_BLOCKS
    bcs pcPoolLibLp
    lda mallocOwner,x
    cmp #$ff
    beq pcPoolMallocNext
    lda mallocSwapped,x
    bne pcPoolMallocNext        // swapped out -- no pool pages
    lda mallocOwner,x
    ldy #POOLID; sta (dataPtr),y
    lda mallocStartPage,x
    ldy #POOLPG; sta (dataPtr),y
    lda mallocPages,x
    ldy #POOLLEN; sta (dataPtr),y
    ldy #POOLM; lda #1; sta (dataPtr),y
    jsr pcPoolAdd
    txa
    pha                          // save malloc slot (pcRegLine clobbers X)
    jsr pcRegLine
    pla
    tax
pcPoolMallocNext:
    inx
    bne pcPoolMallocLp
    // libraries (dynamic kernel libraries)
pcPoolLibLp:
    ldx #0
pcPoolLibScan:
    cpx #MAX_LIBS
    bcs pcPoolDone
    lda libStartPage,x
    cmp #$ff
    beq pcPoolLibNext           // not loaded
    txa
    ldy #POOLID; sta (dataPtr),y   // POOLID = lib id
    lda libStartPage,x
    ldy #POOLPG; sta (dataPtr),y
    lda libPages,x
    ldy #POOLLEN; sta (dataPtr),y
    ldy #POOLM; lda #2; sta (dataPtr),y   // type 2 = lib
    jsr pcPoolAdd
    txa
    pha                          // save lib id (pcRegLib clobbers X)
    jsr pcRegLib
    pla
    tax
pcPoolLibNext:
    inx
    bne pcPoolLibScan
pcPoolDone:
    // bottom totals -- accumulated from the rows above
    jsr kNewline
    jsr kNewline
    ldy #PCSLOT
    lda (dataPtr),y
    jsr pcDecA
    :printMsg(msgPoolUsed - cmdsBlock)
    ldy #PCSLOT
    lda (dataPtr),y
    sta kArg+6
    lda #POOL_PAGES
    sec
    sbc kArg+6
    jsr pcDecA
    :printMsg(msgPoolFree - cmdsBlock)
    jsr kNewline
    jsr kNewline
    rts

// --- pcPoolAdd -- accumulate the row's page count (POOLLEN) into PCSLOT ---
//  Clobbers A, Y.
pcPoolAdd:
    ldy #POOLLEN
    lda (dataPtr),y
    ldy #PCSLOT
    clc
    adc (dataPtr),y
    sta (dataPtr),y
    rts

// --- pcRegLine -- print one pool region row: "pg len type   owner (id)" ---
//  POOLPG = start page, POOLLEN = page count, POOLID = owner task id,
//  POOLM = 0 (task) / 1 (malloc). Clobbers A, X, Y, kPtr.
pcRegLine:
    ldy #POOLPG
    lda (dataPtr),y
    jsr pcDec3Z                 // pg (zero-padded 3 -- see pcDec3Z)
    lda #$20; jsr kPrintChar
    ldy #POOLLEN
    lda (dataPtr),y
    jsr pcDec3                  // len (right-aligned 3)
    lda #$20; jsr kPrintChar
    ldy #POOLM
    lda (dataPtr),y
    bne pcRegMalloc
    :printMsg(msgPoolType - cmdsBlock)         // "task   "
    jmp pcRegName
pcRegMalloc:
    :printMsg(msgPoolType + 8 - cmdsBlock)     // "malloc "
pcRegName:
    ldy #POOLID
    lda (dataPtr),y
    tax
    jsr kGetTaskName            // kPtr = name ptr (X preserved)
    lda kPtr
    ora kPtr+1
    beq pcRegParen              // null name -- just "(id)"
    jsr kPrintString
pcRegParen:
    lda #$20; jsr kPrintChar
    lda #'('; jsr kPrintChar
    ldy #POOLID
    lda (dataPtr),y
    jsr pcDecA                  // id (no padding)
    lda #')'; jsr kPrintChar
    jsr kNewline
    rts

// --- pcRegLib -- print one library row: "pg len lib   <name> (ref N)" ---
//  POOLPG/POOLLEN/POOLID as usual; POOLID = lib id (NOT a task slot).
//  Clobbers A, X, Y, kPtr.
pcRegLib:
    ldy #POOLPG
    lda (dataPtr),y
    jsr pcDec3Z                 // pg (zero-padded 3 -- see pcDec3Z)
    lda #$20; jsr kPrintChar
    ldy #POOLLEN
    lda (dataPtr),y
    jsr pcDec3                  // len (right-aligned 3)
    lda #$20; jsr kPrintChar
    :printMsg(msgPoolType + 16 - cmdsBlock)    // "lib    "
    ldy #POOLID
    lda (dataPtr),y
    tax                         // X = lib id
    lda libNameLo,x; sta kPtr
    lda libNameHi,x; sta kPtr+1
    ldy #POOLPG
    lda #0
    sta (dataPtr),y             // POOLPG = name index (pg display done)
pcRegLibName:
    ldy #POOLPG
    lda (dataPtr),y             // index
    tay
    lda (kPtr),y                // name char
    beq pcRegLibNameDone        // null -> done
    cmp #'.'                    // '.' -> strip the ".lib" extension
    beq pcRegLibNameDone
    jsr kPrintChar              // clobbers A/X/Y/kPtr
    ldy #POOLID
    lda (dataPtr),y             // lib id
    tax
    lda libNameLo,x; sta kPtr   // re-point kPtr
    lda libNameHi,x; sta kPtr+1
    ldy #POOLPG
    lda (dataPtr),y             // index
    clc; adc #1
    sta (dataPtr),y             // index++
    jmp pcRegLibName
pcRegLibNameDone:
    lda #$20; jsr kPrintChar
    lda #'('; jsr kPrintChar
    lda #'r'; jsr kPrintChar
    lda #'e'; jsr kPrintChar
    lda #'f'; jsr kPrintChar
    lda #$20; jsr kPrintChar
    ldy #POOLID
    lda (dataPtr),y
    tax
    lda libRefCount,x
    jsr pcDecA                  // refcount (no padding)
    lda #')'; jsr kPrintChar
    jsr kNewline
    rts

// --- pcDecA -- print A as decimal (no padding) ---
//  Clobbers A, X, Y, kPtr.
pcDecA:
    pha
    lda dataPtr; clc; adc #DECBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    pla
    jsr kByteToDec              // Y = length
    lda #0; sta (kPtr),y
    jsr kPrintString
    rts

// --- pcDec3Z -- print A as decimal, ZERO-padded to 3 chars (pool `pg`) ---
//  A page number is 0-108, so a space-padded 2-char field would print three
//  digits from 100 up and push the rest of the row -- and the `owner` header
//  above it -- one column to the right. A fixed-width zero-padded field keeps
//  every row and the header aligned whatever the pages are.
//  Clobbers A, X, Y, kPtr (via pcDecA).
pcDec3Z:
    cmp #100
    bcs pcDecA                  // 3 digits -- nothing to pad
    pha
    lda #'0'; jsr kPrintChar
    pla
    cmp #10
    bcs pcDecA                  // 2 digits -- one pad
    pha
    lda #'0'; jsr kPrintChar
    pla
    jmp pcDecA

// --- pcDec3 -- print A as decimal, space-padded to 3 chars ---
pcDec3:
    cmp #100
    bcs pcDec3Go
    pha
    lda #$20; jsr kPrintChar
    pla
    cmp #10
    bcs pcDec3Go
    pha
    lda #$20; jsr kPrintChar
    pla
pcDec3Go:
    jmp pcDecA

// --- pcSave -- save <name> <start> <end> ---
pcSave:
    // Skip "save" (4 chars)
    ldy #4
pcSaveSkSp:
    lda (dataPtr),y; bne pcSaveNotEnd
    jmp pcSaveBadName
pcSaveNotEnd:
    cmp #$20; bne pcSaveName
    iny; bne pcSaveSkSp
pcSaveName:
    // Y = start of name. Save offset, null-terminate in lineBuf
    sty kArg+6                  // save nameStart
pcSaveNmEnd:
    lda (dataPtr),y; beq pcSaveNmDone
    cmp #$20; beq pcSaveNmDone
    iny; bne pcSaveNmEnd
pcSaveNmDone:
    lda #0; sta (dataPtr),y     // null-terminate name in lineBuf
    iny                         // skip to next arg
    // Parse start address
pcSaveSkSp2:
    lda (dataPtr),y; bne pcSaveSkSp2Ok
    jmp pcSaveBadAddr
pcSaveSkSp2Ok:
    cmp #$20; bne pcSaveStart
    iny; bne pcSaveSkSp2
pcSaveStart:
    jsr pcParseHex16
    bcc pcSaveStartOk
    jmp pcSaveBadAddr
pcSaveStartOk:
    lda kArg+2; sta kPtr
    lda kArg+3; sta kPtr+1
pcSaveSkSp3:
    lda (dataPtr),y; bne pcSaveSkSp3Ok
    jmp pcSaveBadAddr
pcSaveSkSp3Ok:
    cmp #$20; bne pcSaveEnd
    iny; bne pcSaveSkSp3
pcSaveEnd:
    jsr pcParseHex16
    bcc pcSaveEndOk
    jmp pcSaveBadAddr
pcSaveEndOk:
    // Validate end > start (zero-length or negative rejects)
    lda kArg+2; cmp kPtr        // end lo vs start lo
    lda kArg+3; sbc kPtr+1      // end hi vs start hi
    bcs pcSaveSz1
    jmp pcSaveBadAddr
pcSaveSz1:
    bne pcSaveSizeOk            // end hi > start hi -> OK
    lda kArg+2; cmp kPtr        // end lo == start lo?
    bne pcSaveSizeOk
    jmp pcSaveBadAddr
pcSaveSizeOk:
    // Build name pointer in ZP via $f1/$f2 for valid (indirect),y
    lda kArg+6                  // nameStart
    clc; adc $f1; sta kArg      // kArg = dataPtr + nameStart (lo)
    lda $f2; adc #0; sta kArg+1 // kArg+1 = hi
    // kPtr=start, kArg/kArg+1=name, kArg+2/kArg+3=end
    jsr kReuSave
    bcs psSaveErr               // error path (bcc to pcSaveOk too far)
    jmp pcSaveOk
psSaveErr:
    ldy #BATERR
    lda #1
    sta (dataPtr),y
    lda reuFsEnabled; cmp #$A5
    bne pcSaveNoFs
    lda kArg+6                  // error code
    beq pcSaveExists            // 0 = duplicate
    cmp #1
    beq pcSaveNoSpace           // 1 = no space
    cmp #3
    bne psChk4
    jmp pcSaveBadName           // 3 = name too long
psChk4:
    cmp #4
    bne pcSaveFail
    jmp pcSaveBadName           // 4 = empty name
pcSaveFail:
    :printMsg(msgSaveFail - cmdsBlock)
    jsr kNewline; jsr kNewline
    rts
pcSaveExists:
    :printMsg(msgSaveExists - cmdsBlock)
    jsr kNewline; jsr kNewline
    rts
pcSaveNoSpace:
    :printMsg(msgSaveNoSpace - cmdsBlock)
    jsr kNewline; jsr kNewline
    rts
pcSaveNoFs:
    :printMsg(msgNoFs - cmdsBlock)
    jsr kNewline; jsr kNewline
    rts
pcSaveOk:
    :printMsg(msgSaved - cmdsBlock)
    lda #$20; jsr kPrintChar
    lda #'$'; jsr kPrintChar
    lda dataPtr; clc; adc #DECBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    lda kArg+5; jsr kByteToHex
    lda dataPtr; clc; adc #DECBUF+2; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    lda kArg+4; jsr kByteToHex
    lda #0; ldy #2; sta (kPtr),y
    lda dataPtr; clc; adc #DECBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    jsr kPrintString
    jsr kNewline; jsr kNewline
    rts
pcSaveBadName:
    ldy #BATERR
    lda #1
    sta (dataPtr),y
    :printMsg(msgSaveBadName - cmdsBlock)
    jsr kNewline; jsr kNewline
    rts
pcSaveBadAddr:
    ldy #BATERR
    lda #1
    sta (dataPtr),y
    :printMsg(msgSaveBadAddr - cmdsBlock)
    jsr kNewline; jsr kNewline
    rts

// --- pcParseHex16 -- parse 4 hex digits from lineBuf+Y into kArg+2/+3 ---
//  Input:  Y = offset in lineBuf
//  Output: kArg+2/kArg+3 = 16-bit value, Y advanced past 4 digits
//          C=1 if parse error
//  Clobbers: A, X
pcParseHex16:
    lda #0; sta kArg+2; sta kArg+3  // result = 0
    ldx #4                      // 4 hex digits
phLoop:
    lda (dataPtr),y; beq phFail
    jsr kHexDigit; bcs phFail
    // result = result * 16 + digit
    // Shift result left 4 bits (x16)
    pha                         // save digit on stack (avoid clobbering kArg+6)
    lda kArg+2; asl; rol kArg+3; asl; rol kArg+3
    asl; rol kArg+3; asl; rol kArg+3  // x16
    sta kArg+2
    pla                         // restore digit
    clc; adc kArg+2; sta kArg+2
    lda kArg+3; adc #0; sta kArg+3
    iny
    dex; bne phLoop
    clc; rts
phFail:
    sec; rts

// --- pcLoad -- load <name> <addr> ---
pcLoad:
    ldy #4
    jsr pcSkipSpaces
    bcc pcLoadParse
    jmp pcLoadBad
pcLoadParse:
    // Parse name: null-terminate in lineBuf, pointer in kArg/kArg+1
    tya; clc; adc $f1; sta kArg
    lda $f2; adc #0; sta kArg+1
pcLoadNmEnd:
    lda (dataPtr),y; beq pcLoadNmDone
    cmp #$20; beq pcLoadNmDone
    iny; bne pcLoadNmEnd
pcLoadNmDone:
    lda #0; sta (dataPtr),y
    iny
    // Parse dest addr
    jsr pcSkipSpaces2
    bcc pcLoadDest
    jmp pcLoadBad
pcLoadDest:
    jsr pcParseHex16
    bcc pcLoadGo
    jmp pcLoadBad
pcLoadGo:
    lda kArg+2; sta kPtr
    lda kArg+3; sta kPtr+1
    lda reuFsEnabled
    cmp #$A5
    bne pcLoadNoFs
    jsr kReuLoad
    bcs pcLoadNotFnd
    jmp pcLoadOk
pcLoadNotFnd:
    ldy #BATERR
    lda #1
    sta (dataPtr),y
    :printMsg(msgNotFound - cmdsBlock)
    jsr kNewline; jsr kNewline
    rts
pcLoadNoFs:
    ldy #BATERR
    lda #1
    sta (dataPtr),y
    :printMsg(msgNoFs - cmdsBlock)
    jsr kNewline; jsr kNewline
    rts
pcLoadOk:
    :printMsg(msgLoaded - cmdsBlock)
    lda #$20; jsr kPrintChar
    lda #'$'; jsr kPrintChar
    lda dataPtr; clc; adc #DECBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    lda kArg+3; jsr kByteToHex
    lda dataPtr; clc; adc #DECBUF+2; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    lda kArg+2; jsr kByteToHex
    lda #0; ldy #2; sta (kPtr),y
    lda dataPtr; clc; adc #DECBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    jsr kPrintString
    jsr kNewline; jsr kNewline
    rts
pcLoadBad:
    ldy #BATERR
    lda #1
    sta (dataPtr),y
    :printMsg(msgSaveBadAddr - cmdsBlock)
    jsr kNewline; jsr kNewline
    rts

// --- pcSkipSpaces helper ---
pcSkipSpaces:
    lda (dataPtr),y; beq pcSkSpEnd
    cmp #$20; bne pcSkSpDone
    iny; bne pcSkipSpaces
pcSkSpDone:
    clc; rts
pcSkSpEnd:
    sec; rts

// --- pcSkipSpaces2: like pcSkipSpaces but reuses the same logic ---
pcSkipSpaces2:
    lda (dataPtr),y; beq pcSkSp2End
    cmp #$20; bne pcSkSp2Done
    iny; bne pcSkipSpaces2
pcSkSp2Done:
    clc; rts
pcSkSp2End:
    sec; rts

// --- pcRun -- run <name> [args]: load + spawn task from REU FS ---
//  The name gets ".tsk" appended; args (if any) are moved to sit right
//  after the filename's NUL -- the loader copies them into the new task's
//  instance buffer for KF_TAKES_ARGS binaries (e.g. "run shell ps").
pcRun:
    lda #0
    sta kArg+2                  // default: no RAM pages (loader errors for KF_MALLOC_RAM)
    ldy #3                      // skip "run"
pcRunSkSp:
    lda (dataPtr),y
    bne rfChkSp
    jmp pcRunBad               // end of line -> no filename (branch too far)
rfChkSp:
    cmp #$20
    bne pcRunName
    iny
    bne pcRunSkSp
pcRunName:
    sty kArg+6                  // filename offset (kDynLoad clobbers kArg anyway)
    tya                         // kPtr = lineBuf + filename offset
    clc
    adc dataPtr
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    // scan the name: '.' -> refuse, space -> args follow, NUL -> no args
    ldy #0
rfEndScan:
    lda (kPtr),y
    beq rfAppend                // end of line -> no args
    cmp #'.'
    bne rfExtOk
    jmp rfBadExt                // explicit extension -> refuse (too far to branch)
rfExtOk:
    cmp #$20
    beq rfHasArgs               // space -> args follow
    iny
    cpy #11                     // scan the full 11-byte FS name
    bcc rfEndScan
rfAppend:                       // no args -- append ".tsk\0"
    lda #'.'
    sta (kPtr),y
    iny
    lda #'t'
    sta (kPtr),y
    iny
    lda #'s'
    sta (kPtr),y
    iny
    lda #'k'
    sta (kPtr),y
    iny
    lda #0
    sta (kPtr),y
    iny
    lda #0
    sta (kPtr),y                // empty args field -- stale lineBuf tail
                                // must not be mistaken for args
    jmp rfLoad
rfHasArgs:
    // Y = name length; args run from Y+1 to the line's NUL. Shift them
    // right 4 bytes so ".tsk\0" fits between name and args.
    sty kArg+7                  // name length
    lda kPtr
    clc
    adc kArg+7
    adc #1
    sta srcPtr                  // srcPtr = args start (kPtr + nameLen + 1)
    lda kPtr+1
    adc #0
    sta srcPtr+1
    iny                         // Y = index of the first args byte
rfArgsEnd:
    lda (kPtr),y
    beq rfShift                 // Y = index of the line's NUL
    iny
    bne rfArgsEnd
rfShift:
    tya
    sec
    sbc kArg+7
    tay                         // Y = bytes to move (args + terminator)
    lda srcPtr
    clc
    adc #4
    sta kPtr                    // kPtr = dest = args start + 4
    lda srcPtr+1
    adc #0
    sta kPtr+1
rfShiftLp:
    dey
    lda (srcPtr),y
    sta (kPtr),y
    tya
    bne rfShiftLp
    // insert ".tsk\0" at the name end (srcPtr - 1)
    lda srcPtr
    sec
    sbc #1
    sta srcPtr
    bcs rfInsExt
    dec srcPtr+1
rfInsExt:
    ldy #0
    lda #'.'; sta (srcPtr),y
    iny
    lda #'t'; sta (srcPtr),y
    iny
    lda #'s'; sta (srcPtr),y
    iny
    lda #'k'; sta (srcPtr),y
    iny
    lda #0; sta (srcPtr),y
    // reconstruct kPtr = filename (dataPtr + saved offset)
    lda dataPtr
    clc
    adc kArg+6
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    // parse an optional leading page count from the args (KF_MALLOC_RAM)
    lda kArg+6
    clc
    adc kArg+7
    adc #5                     // args offset = nameOffset + nameLen + 5 (".tsk\0")
    tay
    jsr parseDecimal           // C=0 ok (pcSlot), C=1 no digits
    bcc rfHasPages
    jmp rfLoad
rfHasPages:
    ldy #PCSLOT
    lda (dataPtr),y
    sta kArg+2
    jmp rfLoad
rfBadExt:
    :printMsg(msgDotC - cmdsBlock)
    jsr kNewline; jsr kNewline
    rts
rfLoad:
    lda kArg+2
    sta kArg+7                 // stash RAM pages (kDisableCursor clobbers kArg..+3)
    jsr kDisableCursor         // no phantom cursor while the spawned task
                               // takes its own surface (F-key swaps)
    lda kArg+7
    sta kArg+2                 // restore RAM pages
    lda dataPtr
    clc
    adc kArg+6                 // filename offset -- kDisableCursor clobbered kPtr
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    jsr kDynLoad
    bcs rfErr                   // error path (bcc to pcRunOk too far)
    jmp pcRunOk
rfErr:
    lda kArg
    sta kArg+7                 // stash error code (kEnableCursor clobbers kArg..+3)
    jsr kEnableCursor          // restore the shell cursor before printing
    lda kArg+7
    sta kArg
    // error dispatch on kArg (0=slots, 1=magic, 2=not relocatable, 3=overlap,
    // 4=no space, 5=nofs, 8=align, 9=pages required, 10=res full,
    // 11=already running, 12=instance heap full, 13=no task slots)
    ldy #BATERR
    lda #1
    sta (dataPtr),y
    lda kArg
    beq rfFull
    cmp #1
    beq rfBad
    cmp #2
    beq rfNoReloc
    cmp #3
    beq rfOverlap
    cmp #4
    beq rfNoSpace
    cmp #8
    beq rfAlign
    cmp #9
    beq rfPages
    cmp #10
    beq rfResFull
    cmp #11
    beq rfDup
    cmp #12
    beq rfHeap
    cmp #13
    beq rfTaskSlots
rfNofs:
    ldx #<msgDynNoFs - cmdsBlock
    ldy #>msgDynNoFs - cmdsBlock
    jmp rfPrint
rfFull:
    ldx #<msgDynFull - cmdsBlock
    ldy #>msgDynFull - cmdsBlock
    jmp rfPrint
rfBad:
    ldx #<msgDynBad - cmdsBlock
    ldy #>msgDynBad - cmdsBlock
    jmp rfPrint
rfNoReloc:
    ldx #<msgDynNoReloc - cmdsBlock
    ldy #>msgDynNoReloc - cmdsBlock
    jmp rfPrint
rfOverlap:
    ldx #<msgDynOverlap - cmdsBlock
    ldy #>msgDynOverlap - cmdsBlock
    jmp rfPrint
rfNoSpace:
    ldx #<msgDynNoSpace - cmdsBlock
    ldy #>msgDynNoSpace - cmdsBlock
    jmp rfPrint
rfHeap:
    ldx #<msgDynHeap - cmdsBlock
    ldy #>msgDynHeap - cmdsBlock
    jmp rfPrint
rfTaskSlots:
    ldx #<msgDynTaskSlots - cmdsBlock
    ldy #>msgDynTaskSlots - cmdsBlock
    jmp rfPrint
rfAlign:
    ldx #<msgDynAlign - cmdsBlock
    ldy #>msgDynAlign - cmdsBlock
    jmp rfPrint
rfPages:
    ldx #<msgDynPages - cmdsBlock
    ldy #>msgDynPages - cmdsBlock
    jmp rfPrint
rfDup:
    ldx #<msgDynDup - cmdsBlock
    ldy #>msgDynDup - cmdsBlock
    jmp rfPrint
rfResFull:
    ldx #<msgDynResFull - cmdsBlock
    ldy #>msgDynResFull - cmdsBlock
rfPrint:
    // inline printMsg(X,Y = offset) + newlines
    stx kArg
    sty kArg+1
    ldy #CMDBASE
    lda (dataPtr),y
    clc
    adc kArg
    sta kPtr
    iny
    lda (dataPtr),y
    adc kArg+1
    sta kPtr+1
    jsr kPrintString
    jsr kNewline; jsr kNewline
    rts
pcRunOk:
    jsr kEnableCursor          // restore the shell cursor after the spawn
    jsr kNewline                // one blank line before the prompt
    rts
pcRunBad:
    ldy #BATERR
    lda #1
    sta (dataPtr),y
    :printMsg(msgRunUse - cmdsBlock)
    jsr kNewline; jsr kNewline
    rts

// --- pcRunbatch -- runbatch <name>: execute a .bat file line by line ---
//  Lines are CR+NUL terminated: CR ($0D) is the DISPLAY separator that
//  `type` renders as a newline, NUL is the real terminator that this
//  loop splits on -- so lines may contain 'm' ($0D is its screen code)
//  and every command (run maze, time, format...) works. A trailing CR is
//  stripped before execution. Streamed from the REU FS in 80-byte
//  windows (no file-size limit). Each line runs through processCommand
//  like typed input; on the first error the failing handler prints its
//  normal message and the batch stops silently (BATERR). Outer batch
//  state is saved on the stack so nested runbatch works.
pcRunbatch:
    // stash outer batch state (BATPOS/BATSIZE/BATOFF) on the stack
    ldy #BATOFF2;  lda (dataPtr),y; pha
    ldy #BATOFF+1; lda (dataPtr),y; pha
    ldy #BATOFF;   lda (dataPtr),y; pha
    ldy #BATSIZE+1; lda (dataPtr),y; pha
    ldy #BATSIZE;  lda (dataPtr),y; pha
    ldy #BATPOS+1; lda (dataPtr),y; pha
    ldy #BATPOS;   lda (dataPtr),y; pha
    ldy #9                      // skip "runbatch"
rbSkSp:
    lda (dataPtr),y
    bne rbChkSp
    jmp rbBad                   // end of line -- no filename
rbChkSp:
    cmp #$20
    bne rbName
    iny
    bne rbSkSp
rbName:
    tya                         // name offset within lineBuf
    ldy #DECBUF
    sta (dataPtr),y             // remember it (scan calls clobber kArg)
    ldy #DECBUF
    lda (dataPtr),y
    clc
    adc dataPtr
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    ldy #0
rbEndScan:
    lda (kPtr),y
    beq rbAppend                // end of line
    cmp #'.'
    bne rbExtOk
    jmp rbBadExt                // explicit extension -> refuse
rbExtOk:
    cmp #$20
    beq rbAppend                // trailing spaces
    iny
    cpy #7                      // name <= 7 chars (".bat" fills the rest)
    bcc rbEndScan
rbAppend:                       // append ".bat\0"
    lda #'.'; sta (kPtr),y
    iny
    lda #'b'; sta (kPtr),y
    iny
    lda #'a'; sta (kPtr),y
    iny
    lda #'t'; sta (kPtr),y
    iny
    lda #0; sta (kPtr),y
    lda reuFsEnabled
    cmp #$A5
    beq rbFsOk
    jmp rbNoFs
rbFsOk:
    ldy #PCSLOT
    lda #1
    sta (dataPtr),y             // scan from slot 1 (slot 0 = allocator)
    ldy #BATHI
    lda #0
    sta (dataPtr),y
rbScan:
    ldy #BATHI
    lda (dataPtr),y
    cmp #2
    bne rbScanGo
    jmp rbNotFound              // exhausted (idx == 512): not found
rbScanGo:
    // kPtr = lineBuf+40 (entry-name buffer; our name lives at lineBuf+0)
    lda dataPtr
    clc
    adc #40
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    ldy #BATHI
    lda (dataPtr),y
    tax                         // X = slot hi
    ldy #PCSLOT
    lda (dataPtr),y             // A = slot lo
    jsr kReuDirEntry
    bcs rbScanNext              // empty/deleted slot
    // compare names: (kPtr) = entry name, (srcPtr) = name in lineBuf
    lda dataPtr
    clc
    ldy #DECBUF
    adc (dataPtr),y             // + name offset
    sta srcPtr
    lda dataPtr+1
    adc #0
    sta srcPtr+1
    ldy #0
rbCmpLp:
    lda (kPtr),y
    cmp (srcPtr),y
    bne rbScanNext
    cmp #0
    beq rbFound                 // both terminated -- match
    iny
    cpy #12
    bcc rbCmpLp
rbFound:
    // stash size + offset (kReuDirEntry outputs; nothing clobbers them
    // between here and rbRun, but the next call would)
    ldy #BATSIZE
    lda kArg
    sta (dataPtr),y
    iny
    lda kArg+1
    sta (dataPtr),y
    ldy #BATOFF
    lda kArg+3
    sta (dataPtr),y
    iny
    lda kArg+4
    sta (dataPtr),y
    ldy #BATOFF2
    lda kArg+5
    sta (dataPtr),y
    jmp rbRun
rbScanNext:
    ldy #PCSLOT
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y             // slot++
    ldy #BATHI
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y
    jmp rbScan
rbRun:
    ldy #BATPOS                 // pos = 0
    lda #0
    sta (dataPtr),y
    iny
    sta (dataPtr),y
rbLine:
    // done when pos >= size (16-bit)
    ldy #BATPOS+1
    lda (dataPtr),y
    ldy #BATSIZE+1
    cmp (dataPtr),y
    bcc rbMore
    bne rbDone
    ldy #BATPOS
    lda (dataPtr),y
    ldy #BATSIZE
    cmp (dataPtr),y
    bcc rbMore
rbDone:
rbPop:
    // restore the saved outer-batch state (LIFO of the entry pushes)
    ldy #BATPOS
    pla
    sta (dataPtr),y
    ldy #BATPOS+1
    pla
    sta (dataPtr),y
    ldy #BATSIZE
    pla
    sta (dataPtr),y
    ldy #BATSIZE+1
    pla
    sta (dataPtr),y
    ldy #BATOFF
    pla
    sta (dataPtr),y
    ldy #BATOFF+1
    pla
    sta (dataPtr),y
    ldy #BATOFF2
    pla
    sta (dataPtr),y
    rts
rbMore:
    // window len = min(80, size - pos)
    ldy #BATSIZE+1
    lda (dataPtr),y
    ldy #BATPOS+1
    sec
    sbc (dataPtr),y
    bne rbLen80                 // >= 256 left -> full window
    ldy #BATSIZE
    lda (dataPtr),y
    ldy #BATPOS
    sec
    sbc (dataPtr),y
    cmp #80
    bcc rbLenSet
rbLen80:
    lda #80
rbLenSet:
    ldy #DECBUF
    sta (dataPtr),y             // save length (DMA clobbers kArg)
    sei
    sta kArg+6                  // length lo
    lda #0
    sta kArg+7                  // length hi
    // reuFsPtr = offset(3) + pos(2)
    ldy #BATOFF
    lda (dataPtr),y             // offset lo
    ldy #BATPOS
    clc
    adc (dataPtr),y             // + pos lo (carry -> mid)
    sta reuFsPtr
    ldy #BATOFF+1
    lda (dataPtr),y             // offset mid
    ldy #BATPOS+1
    adc (dataPtr),y             // + pos hi (carry from lo chain)
    sta reuFsPtr+1
    ldy #BATOFF2
    lda (dataPtr),y             // offset hi
    adc #0                      // + carry
    sta reuFsPtr+2
    lda dataPtr
    sta kPtr                    // dest = lineBuf
    lda dataPtr+1
    sta kPtr+1
    jsr kReuDmaRange
    cli
    // find the NUL terminator in the window (CR is display-only)
    ldy #DECBUF
    lda (dataPtr),y
    sta kArg+5                  // len (scratch)
    ldy #0
rbFindCr:
    cpy kArg+5
    bcs rbCrEnd                 // no terminator in the window -- Y = len
    lda (kPtr),y
    cmp #0
    beq rbExec                  // Y = NUL index
    iny
    bne rbFindCr
rbCrEnd:                        // Y = len, no terminator found
    cpy #80
    bne rbExec                  // final short chunk -- last line
    // full window with no terminator -- error unless this IS the final chunk
    ldy #BATSIZE+1
    lda (dataPtr),y
    ldy #BATPOS+1
    sec
    sbc (dataPtr),y
    bne rbTooLong               // > 256 left -> more data follows
    ldy #BATSIZE
    lda (dataPtr),y
    ldy #BATPOS
    sec
    sbc (dataPtr),y
    cmp #80
    bne rbTooLong               // > 80 left -> line too long
rbExec:                         // Y = line-end index (NUL or len)
    lda #0
    sta (kPtr),y                // terminate
    tya
    clc
    adc #1
    sta kArg+5                  // advance = Y+1 (scratch)
    // strip the display CR -- exactly one (the format guarantees CR sits
    // right before the NUL; stripping all would eat a line ending in 'm')
    dey                         // Y = last line char
    bmi rbPosUp                 // empty line -- nothing to trim
    lda (kPtr),y
    cmp #$0D
    bne rbPosUp                 // no CR (final line without separator)
    lda #0
    sta (kPtr),y                // line now ends at its real content
rbPosUp:
    ldy #BATPOS
    lda (dataPtr),y
    clc
    adc kArg+5
    sta (dataPtr),y
    ldy #BATPOS+1
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y
    // skip empty / space-only lines
    ldy #0
rbSkipSp:
    lda (kPtr),y
    bne rbSkipChk
    jmp rbLine
rbSkipChk:
    cmp #$20
    bne rbExecLine
    iny
    bne rbSkipSp
rbExecLine:
    jsr processCommand
    ldy #BATERR
    lda (dataPtr),y
    beq rbCont
    jmp rbPop                   // error -- handler already printed it
rbCont:
    jmp rbLine
rbTooLong:
    ldx #<msgBatLong - cmdsBlock
    ldy #>msgBatLong - cmdsBlock
    jmp rbPrintErr
rbBad:
    ldx #<msgRbUse - cmdsBlock
    ldy #>msgRbUse - cmdsBlock
    jmp rbPrintErr
rbBadExt:
    ldx #<msgRbExt - cmdsBlock
    ldy #>msgRbExt - cmdsBlock
    jmp rbPrintErr
rbNoFs:
    ldx #<msgNoFs - cmdsBlock
    ldy #>msgNoFs - cmdsBlock
    jmp rbPrintErr
rbNotFound:
    ldx #<msgNotFound - cmdsBlock
    ldy #>msgNotFound - cmdsBlock
rbPrintErr:
    stx kArg
    sty kArg+1
    ldy #CMDBASE
    lda (dataPtr),y
    clc
    adc kArg
    sta kPtr
    iny
    lda (dataPtr),y
    adc kArg+1
    sta kPtr+1
    jsr kPrintString
    jsr kNewline; jsr kNewline
    jmp rbPop

// --- dyHex -- print A as 2 hex digits via decBuf ---
dyHex:
    ldy #DECBUF
    sta (dataPtr),y
    lda dataPtr; clc; adc #DECBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    ldy #DECBUF
    lda (dataPtr),y
    jsr kByteToHex
    lda #0; ldy #2; sta (kPtr),y
    jsr kPrintString
    rts

// --- waitRelease -- spin until key up ---
waitRelease:
    jsr kScanKeyboard
    cmp #$ff
    bne waitRelease
    rts

// --- command strings + messages (screen codes) ---
//  ONE contiguous block: string references are computed at runtime as
//  cmdsBlock + compile-time offset (printMsg macro / dispatcher), so
//  this block costs zero fixups. cmd* strings must stay in the first
//  256 bytes (cmdOffTab holds 1-byte offsets).
.encoding "screencode_mixed"
cmdsBlock:
cmdHelp:   .text "help";  .byte 0
cmdClear:  .text "clear"; .byte 0
cmdRun:      .text "run";      .byte 0
cmdView:     .text "view";     .byte 0
cmdExit:     .text "exit";     .byte 0
cmdFsinfo:   .text "fsinfo";   .byte 0
cmdSave:     .text "save";     .byte 0
cmdLoad:     .text "load";     .byte 0
cmdPrint:   .text "print"; .byte 0
cmdRunbatch: .text "runbatch"; .byte 0
cmdPool:    .text "pool";    .byte 0
cmdVer:     .text "ver";     .byte 0
cmdMe:      .text "me";      .byte 0
cmdWait:    .text "wait";    .byte 0
msgHelp:   .text "help clear view run exit print runbatch fsinfo save load pool ver me wait"; .byte 0
msgPaused:   .text " (pau)"; .byte 0
msgSleeping: .text " (slp)"; .byte 0
msgViewOk:   .text "viewing"; .byte 0
msgViewBad:  .text "? view: id"; .byte 0
msgViewNoScr:.text "? no screen"; .byte 0
msgNoScreen:.text "? no screens available"; .byte 0
msgNoFs:      .text "no fs"; .byte 0
msgListHdr: .text "id pri  addr  size name"; .byte 0
msgListSep: .text "------------------------------"; .byte 0
msgFsActive:  .text "FS active  banks:"; .byte 0
msgSize:      .text " size:$"; .byte 0
msgDynFull:   .text "? no free dyn slots"; .byte 0
msgDynHeap:   .text "? instance heap full"; .byte 0
msgDynTaskSlots: .text "? no free task slots"; .byte 0
msgDynBad:    .text "? bad task file"; .byte 0
msgDynNoReloc: .text "? not relocatable"; .byte 0
msgDynOverlap: .text "? address in use"; .byte 0
msgDynNoSpace: .text "? no space in pool"; .byte 0
msgDynNoFs:   .text "? no fs or not found"; .byte 0
msgDynAlign:  .text "? origin not aligned"; .byte 0
msgDynPages:  .text "? pages required"; .byte 0
msgDynResFull: .text "? too many shared tasks"; .byte 0
msgDynDup:    .text "? already running"; .byte 0
msgRunUse:    .text "? run <filename>"; .byte 0
msgShared:    .text " (shr)"; .byte 0
msgSaved:     .text "saved"; .byte 0
msgSaveBadName: .text "? bad name"; .byte 0
msgSaveBadAddr: .text "? bad addr"; .byte 0
msgSaveFail:  .text "? save failed"; .byte 0
msgSaveExists: .text "? exists"; .byte 0
msgSaveNoSpace: .text "? no space"; .byte 0
msgLoaded:    .text "loaded"; .byte 0
msgNotFound:  .text "? not found"; .byte 0
msgDotC:      .text "? run: no extension"; .byte 0
msgRbUse:     .text "? runbatch <filename>"; .byte 0
msgRbExt:     .text "? runbatch: no extension"; .byte 0
msgBatLong:   .text "? line too long"; .byte 0
msgPoolPages: .text " pages"; .byte 0
msgPoolUsed:  .text " used, "; .byte 0
msgPoolFree:  .text " free"; .byte 0
msgPoolHdr2:  .text "pg  len type   owner"; .byte 0
msgPoolType: .text "task   "; .byte 0; .text "malloc "; .byte 0; .text "lib    "; .byte 0
msgPressKey: .text "press any key to continue..."; .byte 0

.assert "shell: cmd strings must fit in 256 bytes", (cmdWait - cmdsBlock < 256) ? 1 : 0, 1
