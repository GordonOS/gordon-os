// ==========================================
//  sidplay.asm -- .MUS music player task (relocatable)
// ==========================================
//  Plays a COMPUTE!'s Enhanced Sidplayer .mus file from the REU FS.
//  Single-instance (NOT re-runnable): re-running gives ? already running.
//  No screen, no keyboard.
//
//  Usage: run sidplay <name> [delay] [repeats] [transpose]
//      <name>      base name of the .mus file, <= 7 chars (".mus" appended)
//      [delay]     scheduler ticks slept per jiffy AT 100% (default 1; 0 is
//                  forced to 1). sid.lib scales it by the machine's speed, so
//                  this is a slow-down relative to the file's own tempo and not
//                  a correction for the emulator: 2 plays any tune at half the
//                  tempo its file was written at, on any machine.
//      [repeats]   0 = repeat forever, 1 = play once (default), 2-255 = N plays
//      [transpose] semitones, signed, -12..+12 (default 0) -- ON TOP OF the
//                  machine's own compensation, which sid.lib applies from
//                  sysSpeedShift. Use it for the music's own reasons (a tune
//                  written for another register, a deliberate octave); the
//                  emulator's pitch shift is not the caller's to answer.
//
//  This task is only a WRAPPER around sid.lib (lib 5): the player body lives
//  there, behind kSidPlay (call 33) and kSidHush (call 34). The wrapper does
//  what a library cannot -- it finds the tune in the REU FS, kMallocs a buffer
//  for it and loads it (a library's libMask is structurally 0, so a library
//  that loaded a tune would hold no filesys reference), hands sid.lib the
//  buffer plus the delay and repeat count, then frees the buffer when the lib
//  returns. The lib owns the SID while it plays, so a second player is refused
//  (C=1) instead of the two fighting over the chip.
//
//  The song buffer is kMalloc'd to fit the file -- ceil(size/256) pages (min
//  1), capped by the kernel's MAX_MALLOC_PAGES -- and its pointer is stashed in
//  the instance data at offset 16/17, because kReuFindFile/kReuLoad/kMalloc all
//  clobber kPtr. The parse scratch (spDelay/spRepeats/spTmp) lives in the code
//  image instead: the loader copies the code per instance, so those bytes are
//  per-instance writable RAM.
//
//  sid.lib drives the Sidplayer out of ZP $02-$0A, so this task declares the
//  ZP window SID_ZP_BASE/SID_ZP_LEN. libMask: filesys (bit 1) for the load
//  above and sid (bit 5) for the player. The filesys reference is dropped with
//  kLibHold as soon as the tune is in RAM, so playing costs sid.lib's pages only.

.encoding "screencode_mixed"

// (syscall + dataPtr/kPtr/kArg defined in tools/assemble-tasks.asm)

//  sid.lib drives the Sidplayer out of ZP $02-$0A, so the window is the
//  LIBRARY's requirement -- declared here because a library cannot declare ZP
//  for itself (the scheduler preserves ZP per task, from the task's header).
.const SID_ZP_BASE = $02
.const SID_ZP_LEN  = 9

sidplayBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE | KF_TAKES_ARGS  // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word sidplayEnd - sidplayMain  // +5: code size
    .byte 18, SID_ZP_BASE, SID_ZP_LEN  // +7: data size, ZP base, ZP len
    .word (1 << LIB_FILESYS) | (1 << LIB_SID)  // +10: libMask
    .byte sidplayNameEnd - sidplayName  // +12: name len
sidplayName:
    .text "sidplay"
sidplayNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - sidplayBinStart), 0

// --- Task entry (first label in the code body = spawn address) ---
sidplayMain:
    // Parse "<name>[ <delay>[ <repeats>]]" -- base name is <= 7 chars.
    ldy #0
spScan:
    lda (dataPtr),y
    beq spNameEnd                // null terminator
    cmp #$20
    beq spNameEnd                // space -- name done
    iny
    cpy #7
    bcc spScan                   // cap base name at 7 chars
spNameEnd:
    tya
    tax                          // X = name length (0-7), kept across the parse

    jsr spNext                   // delay
    bcc spDelSet
    lda #1                       // absent -> 1 tick per jiffy
spDelSet:
    sta spDelay
    jsr spNext                   // repeats
    bcc spRepSet
    lda #1                       // absent -> play once
spRepSet:
    sta spRepeats
    jsr spNext                   // transpose, in semitones
    bcc spPitchSet
    lda #0                       // absent -> no transpose
spPitchSet:
    sta spPitch
    cmp #13                      // the player's own wrap moves ONE octave per field,
    bcc spPitchOk                //   so a value outside -12..+12 would land outside
    cmp #$f4                     //   the octave it is meant for ($f4 = -12)
    bcs spPitchOk
    lda #0                       // out of range: ignore it rather than shift the
    sta spPitch                  //   tune the wrong way
spPitchOk:

    // Append ".mus" + null to the name (X = name length).
    txa
    tay
    lda #$2e                     // '.'
    sta (dataPtr),y
    iny
    lda #$0d                     // 'm' (screen code)
    sta (dataPtr),y
    iny
    lda #$15                     // 'u'
    sta (dataPtr),y
    iny
    lda #$13                     // 's'
    sta (dataPtr),y
    iny
    lda #0
    sta (dataPtr),y              // null-terminate

    // Find the .mus file first to learn its size (no DMA yet).
    lda dataPtr
    sta kArg
    lda dataPtr+1
    sta kArg+1
    jsr kReuFindFile
    bcs spFailTune               // not found -> say which file, then exit

    // Allocate ceil(size/256) pages (min 1). reuFsRemain/reuFsRemain+1
    // hold the 16-bit file size (set by kReuFindFile); kMalloc fails
    // gracefully if the song is too big for the pool.
    lda reuFsRemain
    clc
    adc #$ff                     // carry = 1 if low byte != 0
    lda reuFsRemain+1
    adc #0                       // A = hi + carry = ceil(size/256)
    bcc spAlloc                  // no overflow (size < 65280)
    jmp spExit                   // absurd size -- give up
spAlloc:
    bne spDoMalloc
    lda #1                       // empty file -> 1 page minimum
spDoMalloc:
    jsr kMalloc
    bcc spMemOk
    jmp spFailMem                // no space -> say so, then exit (long branch:
spMemOk:                         //   the kLibHold call below pushed it past 127)
    ldy #16
    lda kPtr
    sta (dataPtr),y              // stash buffer lo
    iny
    lda kPtr+1
    sta (dataPtr),y              // stash buffer hi

    //  Load the .mus file. Both inputs are re-installed first: an earlier call
    //  can have clobbered them -- `kMalloc` clobbers kArg, and the tick's
    //  sprite install clobbers kPtr whenever the display owner changes. kPtr
    //  is the DMA DESTINATION, so a lost one still reports success while
    //  writing elsewhere, leaving the tune buffer unwritten for the player.
    jsr spLoadPtr
    lda dataPtr
    sta kArg
    lda dataPtr+1
    sta kArg+1
    jsr kReuLoad
    bcs spFailLoad               // unreadable -> free, say so, exit

    //  The tune is in RAM and filesys.lib has done its job -- but a declared lib
    //  stays resident until the task EXITS, so it would sit in the pool for the
    //  whole song (~11 pages) and make the loader evict/restore other tasks
    //  around the player: the gaps in the music that the inline player never had.
    //  Hold only sid.lib and hand the rest back now.
    lda #(1 << LIB_SID)          // lib 5 = sid.lib (the one we still need)
    jsr kLibHold

    // Hand the tune to sid.lib: kPtr = image, kArg = delay, kArg+1 = repeats.
    // kSidPlay blocks until the tune has played its repeats (0 = forever) and
    // hushes the SID before returning; C=1 means another task owns the chip.
    jsr spLoadPtr
    lda spDelay
    sta kArg
    lda spRepeats
    sta kArg+1
    lda spPitch                  // semitones: the player's own wrap turns a whole
    sta kArg+3                   //   +/-12 into the octave step for us
    lda #0                       // no octave term of our own
    sta kArg+2
    jsr kSidPlay
    bcs spFailPlayer             // C=1: refused, or sid.lib would not load

spFreeExit:
    jsr spFreeBuf
spExit:
    lda #SYS_EXIT
    jsr syscall                  // exit -- never returns

// --- spFreeBuf -- release the tune buffer (pointer stashed at data+16/+17) ---
spFreeBuf:
    jsr spLoadPtr
    jsr kFree                    // release the song buffer
    rts

// ==========================================
//  Failure reports
// ==========================================
//  A task with no screen can show nothing, and `libLoadFail`'s message lands
//  nowhere either, so a silent instant exit is the default -- the player returns
//  "normally" having played nothing, and the user sees no music and no message.
//  So a failure takes a screen, says what went wrong, waits for a key and hands
//  the display back -- edit.inc's edGiveDisplay recipe, plus the rule that the
//  live blink block must be un-drawn (SEI'd, no syscall in the window) before
//  kSwitchView takes the display.
spFailTune:                      // "? tune <name>"
    lda #<spMsgTune
    sta spMsgLo
    lda #>spMsgTune
    sta spMsgHi
    lda dataPtr                  // the name we built: "<name>.mus"
    sta spMsg2Lo
    lda dataPtr+1
    sta spMsg2Hi
    jmp spReport

spFailLoad:                      // "? load <name>"
    jsr spFreeBuf
    lda #<spMsgLoad
    sta spMsgLo
    lda #>spMsgLoad
    sta spMsgHi
    lda dataPtr
    sta spMsg2Lo
    lda dataPtr+1
    sta spMsg2Hi
    jmp spReport

spFailMem:                       // "? no memory"
    lda #<spMsgMem
    sta spMsgLo
    lda #>spMsgMem
    sta spMsgHi
    jmp spFailNo2

spFailPlayer:                    // "? no player" (busy, or sid.lib absent)
    lda #<spMsgPlayer
    sta spMsgLo
    lda #>spMsgPlayer
    sta spMsgHi
spFailNo2:
    lda #0
    sta spMsg2Lo
    sta spMsg2Hi
    jmp spReport

//  spReport -- show spMsg (+ spMsg2 when non-zero) on a fresh screen, wait for a
//  key, hand the display back, free the surface and exit. Clobbers everything.
spReport:
    lda #$0b                     // DEFAULT_BORDER_COLOR (dark grey)
    sta kArg
    lda #$00                     // DEFAULT_BG_COLOR (black)
    sta kArg+1
    sta kArg+2
    sta kArg+3
    lda #0                       // mode 0 = hires text
    jsr kAllocScreen
    bcs spExit                   // no free surface: nothing we can do
    ldx #0
    ldy #0
    jsr kSetCursor               // (0,0) -- the only X/Y-preserving routine
    //  Take the display. Un-draw the blink block first, SEI-wrapped,
    //  with no syscall inside the window (the dispatcher re-enables IRQs).
    lda #SYS_GETTID
    jsr syscall
    tax
    php
    sei
    txa
    pha
    jsr kBlinkReset              // clobbers A, X, Y, kPtr, kArg+7
    pla
    tax
    jsr kSwitchView              // display + keyboard focus move to us
    plp
    lda spMsgLo                  // kAllocScreen and kBlinkReset killed kPtr
    sta kPtr
    lda spMsgHi
    sta kPtr+1
    jsr kPrintString
    lda spMsg2Lo
    ora spMsg2Hi
    beq spReportWait
    lda spMsg2Lo
    sta kPtr
    lda spMsg2Hi
    sta kPtr+1
    jsr kPrintString             // the file name, when there is one
spReportWait:
    jsr kWaitKey                 // hold the message on screen until acknowledged
    //  Hand the display back BEFORE freeing our surface (edGiveDisplay).
    lda #SYS_GETTID
    jsr syscall
    cmp kbFocus
    bne spReportFree             // we do not hold it -- nothing to hand back
    tax
    lda taskParent,x
    cmp #$ff
    beq spReportFree
    tax
    lda taskState,x
    beq spReportFree             // parent is dead -- sysExit's hand-off decides
    jsr kSwitchView
spReportFree:
    jsr kFreeScreen
    jmp spExit

// --- spLoadPtr -- kPtr = the stashed buffer (data+16/+17) ---
//  A kernel or library call between the kMalloc that returned the buffer and
//  its next use may have replaced kPtr: the tick's sprite install does so
//  whenever the display owner changes. Callers therefore re-install it rather
//  than assume it survived.
spLoadPtr:
    ldy #16
    lda (dataPtr),y
    sta kPtr
    iny
    lda (dataPtr),y
    sta kPtr+1
    rts

// ==========================================
//  spNext -- parse the optional decimal at (dataPtr),y
// ==========================================
//  Out: A = value, C = 0 if a number was present, C = 1 if not (A = 0).
//       Y left past any digits. A permissive 8-bit parse: digits wrap mod 256.
//  Clobbers A, Y, spTmp. X is preserved (it carries the name length).
spNext:
    lda (dataPtr),y
    cmp #$20
    bne spNextNum
    iny
    jmp spNext                   // skip spaces between fields
spNextNum:
    lda (dataPtr),y
    cmp #$2d                     // '-' -> a signed field
    bne spNextPos
    iny
    lda (dataPtr),y              // reload: `iny` moved past the '-' but A still holds it,
    jsr spNextPos                //   and the digit parse consumes A
    bcs spNextNone               // nothing after the '-': not a number
    eor #$ff
    clc
    adc #1                       // negate the magnitude
    clc                          // spNext reports "absent" with C = 1, so a parsed
    rts                          //   number must return C = 0 -- sec here meant the
                                 //   caller discarded the value and used 0
spNextPos:
    sec
    sbc #$30                     // '0'
    cmp #10
    bcs spNextNone               // not a digit (or the null terminator)
    // A digit is present: accumulate *10 + digit.
    lda #0
    sta spTmp
spNumLp:
    lda (dataPtr),y
    sec
    sbc #$30
    bcc spNumDone
    cmp #10
    bcs spNumDone
    pha                          // keep the digit
    lda spTmp
    asl
    sta spTmp
    asl
    asl
    clc
    adc spTmp                    // *8
    sta spTmp
    pla
    clc
    adc spTmp                    // *8 + *2 = *10, plus the digit
    sta spTmp
    iny
    jmp spNumLp
spNumDone:
    lda spTmp
    clc                          // C=0: a number was present
    rts
spNextNone:
    lda #0
    sec                          // C=1: absent -> the caller uses its default
    rts

spDelay:   .byte 1               // ticks slept per jiffy AT 100% (0 -> 1 in sid.lib)
spRepeats: .byte 1               // 0 = forever, else plays remaining
spPitch:   .byte 0               // transpose in semitones, -12..+12
spTmp:     .byte 0               // decimal accumulator
spMsgLo:   .byte 0               // failure report: message pointer
spMsgHi:   .byte 0
spMsg2Lo:  .byte 0               // ...and an optional second string (the name)
spMsg2Hi:  .byte 0

spMsgTune:   .text "? tune ";     .byte 0
spMsgLoad:   .text "? load ";     .byte 0
spMsgMem:    .text "? no memory"; .byte 0
spMsgPlayer: .text "? no player"; .byte 0

.label sidplayEnd = *
