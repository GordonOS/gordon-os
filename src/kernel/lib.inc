// ==========================================
//  Dynamic Kernel Library Loader
// ==========================================
//  Loads a .lib binary into the pool on demand, shared + refcounted.
//  Lives in the high region right after poolevict.inc,
//  before the gfx scratch. The jump-table stubs call libLoad on
//  the cold path; dynLoad refcounts eager libs at spawn.
//
//  A lib's code body begins with an entry table: entryCount absolute
//  .word addresses, relocated by dlLoadCore's fixup pass (the words
//  are auto-detected as fixups by the double-assembly pipeline).
//  libLoad patches each call's 3-byte kernel jump entry in place to
//  'jmp <entry>' via the read-only libJumpLo/Hi + libInitLo/Hi tables.
//
//  Rules (see copilot-instructions.md):
//    - libLoad/libFree run SEI-atomic; libLoad preserves the caller's I
//      flag (php/plp before EVERY rts) -- never cli unconditionally.
//    - never use kArg+8 (aliases savedSP).
//    - reuse reuFindFile/reuDmaRange/dynParseHeader/dlLoadCore.

.pc = *

// --- loader-local scratch (dlLibId survives dlLoadCore's clobbers) ---
dlLibId:    .byte 0          // lib id stash
dlLibMask:  .byte 0          // libMask lo (spawn refcount loop)
dlLibMaskHi:.byte 0          // libMask hi
dlLibTmp:   .byte 0          // entry count / mask-byte scratch

// --- libTrap -- initial value for eager jump entries (fail-safe) ---
libTrap:
    sec
    rts

// ==========================================
//  libLoad -- ensure library <A> is resident, patch its jump entries
// ==========================================
//  Input:  A = lib id (0..MAX_LIBS-1)
//  Output: C=0 loaded (or already loaded); C=1 error, kArg = error code
//  Clobbers: A, X, Y, kPtr, srcPtr, kArg block
libLoad:
    php                         // preserve caller's flags (incl. I flag)
    sei
    sta dlLibId
    ldx dlLibId
    lda libStartPage,x
    cmp #$FF
    beq llFind                  // not loaded -> load it
    jmp llAlready               // already loaded (far -- jmp, not branch)
llFind:
    // find "<name>.lib" in the FS (reuFindFile reads the name from kArg)
    lda libNameLo,x
    sta kArg
    lda libNameHi,x
    sta kArg+1
    jsr dlFindMaster            // C=0 found; also sets the loader's cursor
    bcc llFound
    lda #5; sta kArg            // error 5 = not found
    plp
    sec
    rts
llFound:
    sei                         // defensive (stay atomic through the load)
    // DMA the 256-byte header page into scratchBuf
    lda #0; sta kArg+6
    lda #1; sta kArg+7
    lda #<$C400; sta kPtr
    lda #>$C400; sta kPtr+1
    jsr reuDmaRange             // preserves caller's I flag
    sei
    // dynParseHeader writes the parsed name to (dataPtr) -- point it at
    // a safe scratch buffer (a task's dataPtr may be 0, corrupting ZP
    // $0000-$000D -- the dlRestore/boot pitfall).
    lda #<dynNameBuf
    sta dataPtr
    lda #>dynNameBuf
    sta dataPtr+1
    // parse header -> kArg=origin, +2/+3=size, +4=flags, dlFixCountLo/Hi
    lda #<$C400; sta kPtr
    lda #>$C400; sta kPtr+1
    jsr dynParseHeader
    bcc llHdrOk
    plp
    sec
    rts
llHdrOk:
    lda #0
    sta dlRamPages              // a lib takes no program RAM
    lda #1
    sta dlNoSelfEvict           // a lib load must never evict the calling task
    lda dlLibId
    pha                         // stash lib id -- dlLoadCore's eviction can
                                //   reach libFree, whose `stx dlLibId` clobbers
                                //   it (fp recorded as string.lib: warm-hang
                                //   on any later kByteToHex)
    // relocate + copy code into the pool -> kPtr=load_addr, dlStartPage/dlPages
    // dlLibCore = dlLoadCore with the pool direction set to HIGHEST fit, so a
    // library always lands above every task block and can never strand the
    // task space. Same size as a plain jsr -- the flag setup and the
    // unconditional clear live in pool-top.inc.
    jsr dlLibCore
    bcc llCoreOk
    pla                         // balance the stash (fail path)
    plp
    sec
    rts
llCoreOk:
    pla                         // restore the lib id (survived the load)
    sta dlLibId                 //   libEntryBase reads dlLibId
    tax                         //   X = lib id for the table stores below
    lda dlStartPage; sta libStartPage,x
    lda dlPages;     sta libPages,x
    lda libEntryCount,x
    sta dlLibTmp                // entry count
    jsr libEntryBase            // A = base call index (clobbers X)
    sta dlLibMaskHi             // call index (base) -- free during libLoad
llEntryLp:
    lda dlLibTmp
    beq llEntriesDone
    ldx dlLibMaskHi             // call index
    lda libJumpLo,x
    sta srcPtr                  // srcPtr = this call's jump entry
    lda libJumpHi,x
    sta srcPtr+1
    ldy #0
    lda #$4C
    sta (srcPtr),y              // 'jmp' opcode
    lda (kPtr),y                // entry lo (kPtr = entry table)
    iny
    sta (srcPtr),y              // jmp addr lo
    lda (kPtr),y                // entry hi
    iny
    sta (srcPtr),y              // jmp addr hi
    clc                         // kPtr += 2 (next entry word)
    lda kPtr
    adc #2
    sta kPtr
    bcc llEntryAdv
    inc kPtr+1
llEntryAdv:
    inc dlLibMaskHi
    dec dlLibTmp
    jmp llEntryLp
llEntriesDone:
    plp
    clc
    rts
llAlready:
    plp
    clc
    rts

// ==========================================
//  libEntryBase -- sum libEntryCount[0..id-1] -> A (base call index)
// ==========================================
//  Input:  dlLibId = lib id
//  Output: A = base index. Clobbers A, X.
libEntryBase:
    lda #0
    ldx #0
lebLoop:
    cpx dlLibId
    bcs lebDone
    clc
    adc libEntryCount,x
    inx
    jmp lebLoop
lebDone:
    rts

// ==========================================
//  libFree -- release library <X>'s pool pages (refcount already 0)
// ==========================================
//  Input:  X = lib id
//  Clobbers: A, X, Y, srcPtr
libFree:
    stx dlLibId
    lda libStartPage,x
    cmp #$FF
    beq lfDone                  // not loaded
    pha                         // startPage
    lda libPages,x
    tay                         // Y = pages
    pla
    tax                         // X = startPage (freeFromPool: X=start,Y=pages)
    jsr freeFromPool
    ldx dlLibId
    lda #$FF; sta libStartPage,x
    lda #0;   sta libPages,x
    lda libEntryCount,x
    sta dlLibTmp
    jsr libEntryBase            // A = base (clobbers X)
    sta dlLibMaskHi             // call index
lfClrLp:
    lda dlLibTmp
    beq lfDone
    ldx dlLibMaskHi
    lda libJumpLo,x
    sta srcPtr                  // srcPtr = this call's jump entry
    lda libJumpHi,x
    sta srcPtr+1
    ldy #0
    lda #$4C
    sta (srcPtr),y              // 'jmp' opcode
    lda libInitLo,x             // initial target lo (libTrap / cold stub)
    iny
    sta (srcPtr),y
    lda libInitHi,x
    iny
    sta (srcPtr),y
    inc dlLibMaskHi
    dec dlLibTmp
    jmp lfClrLp
lfDone:
    clc
    rts

// ==========================================
//  Library name strings ("<name>.lib\0" -- found via libNameLo/Hi)
// ==========================================
libNameTime:    .text "time.lib";    .byte 0
libNameFilesys: .text "filesys.lib"; .byte 0
libNameGfx:     .text "gfx.lib";     .byte 0
libNameFp:      .text "fp.lib";      .byte 0
libNameString:  .text "string.lib";  .byte 0
//  WARNING: `libNameSid` and `libNameIpc` are NOT here. This block ends at $FF7D, and
//  the pocket above it ($FF7E-$FFF9) is the library-entry block -- sid.lib's and
//  ipc.lib's call entries, cold stubs and name strings. Eight more bytes here
//  would push the block end into that pocket and shift the gfx scratch variables
//  declared after it (whose addresses are hard-coded in gfx.asm). Both strings
//  are in src/main.asm's tail block instead.
