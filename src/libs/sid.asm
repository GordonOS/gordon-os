// ==========================================
//  sid.asm -- dynamic library "sid.lib" (lib 5, 2 entries, lazy)
// ==========================================
//  The COMPUTE!'s Enhanced Sidplayer, packaged as a library so a task can play
//  a tune without carrying a copy of the player:
//
//      kSidPlay   call 33 -- play a tune, BLOCKING, at a given speed
//      kSidHush   call 34 -- silence the SID and stop a running player
//
//  `kSidPlay` has ONE entry but TWO modes, because the call index space has no
//  room for a third (its tables are flush against the gfx LUTs -- the same reason
//  fp.lib is a single dispatching entry): `A` selects where the tune comes from.
//       A = 0   kPtr = the `.mus` image in RAM
//       A = 1   kPtr = a 9-byte config block naming an image in the REU filesystem
//
//  The player body itself is `sidplay.inc`, which
//  is why the entry points are thin: this file owns the *policy* -- who may play,
//  how many times, how to stop -- and the body owns the music.
//
//  WARNING: THE CALLER OWNS THE TUNE BUFFER, AND THIS LIBRARY NEVER ALLOCATES.
//  Mode 0 takes a pointer to a `.mus` image already in RAM (the task's
//  `kMalloc` block, or a tune linked into the task's own image). The library
//  cannot load it itself: a library's `libMask` is structurally 0, so if it
//  called `kReuFindFile`/`kReuLoad` those filesys references would be held by
//  nobody, and filesys.lib could be freed under a running player. Keeping the
//  load in the task keeps this library free of library dependencies.
//
//  WARNING: MODE 1 IS HOW A TUNE STOPS NEEDING A BUFFER AT ALL, and it
//  keeps the same rule. The CALLER still owns the file: it calls `kReuFindFile`
//  (filesys.lib) and passes the 24-bit offset that call returns, so this library
//  still references no filesystem entry. The image is then paged from the REU on
//  demand -- a page-aligned 256-byte window per voice, refilled when a voice
//  crosses into a new page -- so tune size is unbounded and the cost is three
//  pages of task RAM however long the file is. The page fetch goes through
//  `kReuDmaRange`, a KERNEL entry, not through filesys.lib. The config block the
//  caller builds at `kPtr` is 9 bytes:
//
//      +0 +1 +2   the file's FS offset (lo, mid, hi) -- from `kReuFindFile`
//      +3 +4      voice 1's window pointer (lo, hi)
//      +5 +6      voice 2's
//      +7 +8      voice 3's
//
//  Each window is 256 bytes and MUST be page-aligned (`lo` = 0): the player
//  addresses a command as `winHi:offsetLo`, so a window whose low byte is not 0
//  would need arithmetic on every command. The windows must not overlap and must
//  not move while the tune plays -- the player keeps no other record of them.
//
//  WARNING: THE CALLER MUST DECLARE ZP $02-$0A. The player's scratch is remapped into
//  that range by the port (see `sidplay.inc`), and a library cannot declare ZP --
//  only a task header has `zp_base`/`zp_len`. `$02`/len 9 is what makes the
//  player's state survive the yields in the play loop; with any other region the
//  scheduler swaps it out from under a running song.
//
//  WARNING: ONE PLAYER AT A TIME, AND A STALE OWNER HEALS. `sidOwner` holds the task
//  slot of the running player ($FF = free) and `sidStop` asks it to stop. The
//  claim is a check-and-set with interrupts off -- no syscall may run inside that
//  window, since the syscall dispatcher switches tasks with `I` set -- and a
//  recorded owner whose slot is not ALIVE is taken over, because a killed
//  player never runs its hush and a plain boolean flag would lock the SID for
//  the rest of the session.
//
//  WARNING: THE REPEATS ARGUMENT. `0` = repeat forever (kSidPlay then returns only
//  when hush asks it to stop), `1` = play once (the default, and what the task
//  passes when the user gives no third argument), `2`-`255` = that many plays.
//  Hush stops the player in every case, whatever the repeat value.
//
//  WARNING: THE MACHINE'S SPEED IS COMPENSATED HERE, NOT BY THE CALLER. VICE's
//  `-speed` runs the whole machine -- the SID's clock included -- faster than
//  real time, so at 200% every pitch doubles and a jiffy is half as long. Both
//  follow from `sysSpeedShift` (which `speed.com` sets, one step per doubling):
//  the tune's pitch moves one octave per step and the ticks-per-jiffy delay is
//  shifted by the same exponent. So a caller states the tempo and the transpose
//  it wants ON A REAL MACHINE, and this library cancels whatever emulator it is
//  running on. On a 100% machine both are no-ops.
//
//  WARNING: SET BOTH TRANSPOSE BYTES, AND KNOW WHY YOU WANT ONE. kArg+2/kArg+3 are
//  read at the call and an unset byte is whatever the last task left in the kArg
//  block, so a caller that wants no transpose must pass 0 in both. They are added
//  AFTER the tune's own TPS and after the speed compensation, so a tune that
//  transposes keeps it and the caller's offset rides on top. The two bytes have
//  OPPOSITE sign conventions, because each is added where the player already had a
//  field: kArg+2 is added to the HALVING COUNT, so +1 is one octave DOWN and $FF is
//  one octave UP; kArg+3 is added to the note's half-step total, so -12 is an octave
//  down (the player's own wrap carries it). Use them for the MUSIC's own reasons --
//  a tune written for a different register, an octave to suit a game's arrangement --
//  never to answer the emulator, which is this library's job.
// ==========================================

.encoding "screencode_mixed"

// (taskState/curTask/kArg/kPtr/SYS_* come from kernel-data.inc + jumptable.inc,
//  imported globally by assemble-tasks.asm -- do not re-declare them here.)

sidBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word sidEnd - sidCode       // +5:  code size
    .byte 0, 0, 0                // +7:  data size, ZP base, ZP len (callers
                                 //      declare ZP $02-$0A -- see the header)
    .word 0                      // +10: reserved (libs consume no libMask)
    .byte sidNameEnd - sidName   // +12: name len
sidName:
    .text "sid"
sidNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - sidBinStart), 0

sidCode:
    // Entry table, in call-index order: base 33, so index 33 = sidPlay and
    // 34 = sidHush. `libLoad` patches each of those jump entries to the
    // address in the matching slot here.
    .word sidPlay, sidHush

// ==========================================
//  sidPlay -- kSidPlay: play a tune, blocking
// ==========================================
//  Input:  A           = mode: 0 = the image is in RAM at kPtr
//                        1 = kPtr points at the 9-byte REU-file config block
//          kPtr/kPtr+1 = mode 0: the .mus image, INCLUDING its 2-byte load
//                        address (the song proper starts at +2)
//                        mode 1: the config block described in the header
//          kArg        = delay: scheduler ticks slept per jiffy AT 100% (0 -> 1)
//                        -- this library scales it by the machine's speed
//          kArg+1      = repeats: 0 = forever, 1 = once, 2-255 = that many
//          kArg+2      = transpose, octaves: signed, 0 = none, +1 = ONE OCTAVE DOWN
//                        ($FF = one octave up: added to the player's halving count)
//                        ON TOP OF the automatic speed compensation
//          kArg+3      = transpose, semitones: signed, 0 = none, -12 = one octave down
//  Output: C = 0 the tune was played and the SID hushed
//          C = 1 refused -- another task is already playing
//  Clobbers: A, X, Y, kArg+0/+1 (the loop sleeps through kArg+1)
//  Returns with the SID silent and `sidOwner` released.
//  WARNING: NOTHING may touch the player's state until the SID is OURS. The mode byte and the
//  config copy below are library data that a RUNNING player dereferences on every command, so
//  a caller that is about to be refused must not have written them -- the owner test comes
//  first, and the mode rides on the stack across it. (The refusal path is ordinary: `gortris`,
//  `godzi` and `grknoid` all retry a busy SID, `grknoid` every ~1 s.)
sidPlay:
    pha                          // the mode, across the ownership test
    php
    sei
    lda sidOwner
    cmp #$FF
    beq sidClaim
    tax
    lda taskState,x              // the recorded owner: is it still ALIVE?
    beq sidClaim                 // no -- it died without husking: take over
    plp
    pla                          // refused: drop the parked mode and say so
    sec
    rts                          // C=1: somebody else owns the SID
sidClaim:
    lda curTask
    sta sidOwner
    lda #0
    sta sidStop                  // a fresh player has nothing to stop for
    plp
    pla                          // A = the mode, now that it may be acted on
    and #1                       //   0 = image in RAM, 1 = paged from the REU
    sta sidplay.StreamMode
    beq sidPlayRam
sidPlayStream:
    lda #0                       // a paged image has no RAM address, and no window
    sta sidplay.SidBaseLo        //   has been filled yet (the page tag alone cannot
    sta sidplay.SidBaseHi        //   say that: every value 0-255 is a real page)
    sta sidplay.StreamValid
    sta sidplay.StreamValid+1
    sta sidplay.StreamValid+2
    ldy #8
spsCopy:
    lda (kPtr),y                 // +0..+8: the file offset, then the three windows
    sta sidplay.StreamPtrLo,y
    dey
    bpl spsCopy
    jmp sidPlayGo
sidPlayRam:
    lda kPtr
    sta sidTuneLo
    sta sidplay.SidBaseLo
    lda kPtr+1
    sta sidTuneHi
    sta sidplay.SidBaseHi
sidPlayGo:
    //  Latch the arguments into library data: kArg+1 is the sleep's own
    //  argument, so it cannot double as the repeat counter.
    lda kArg
    bne sidDelayOk
    lda #1                       // a delay of 0 ticks would be a tight loop
sidDelayOk:
    jsr sidScaleDelay            // ...and the machine's speed scales it
    sta sidDelay
    lda kArg+1
    sta sidRepeats
    lda kArg+2                   // transpose, in addition to the tune's own TPS
    clc
    adc sysSpeedShift            // ...and to the machine's own pitch shift,
    sta sidplay.DataPitchOct     //   whose +1 in this field is one octave down
    lda kArg+3
    sta sidplay.DataPitchSemi
    jsr sidplay.sidDetectVideo  // which video standard's clock this machine has -- the
                                //   archive's table is the NTSC one, so a PAL machine needs
                                //   the PAL copy (measured once, so this is free after it)
    jsr sidSongInit              // the first play needs INIT_SONG too -- see below
    //  WARNING: The song MUST be initialised BEFORE the first tick. The loop's
    //  `SID_STATUS & 7` test reads the voice state INIT_SONG sets up, so
    //  without this the very first jiffy looks like "all three voices halted"
    //  and a player called with repeats 1 stops immediately, silently. The
    //  restart path has its own INIT_SONG call, so this one is easy to lose when
    //  the body moves -- it must stay here, on the first-play path.
sidLoop:
    lda sidStop
    bne sidStopNow               // hush asked us to stop: do not touch the SID
    jsr sidplay.sidTick          // one jiffy of the song
    lda sidStop
    bne sidStopNow
    lda sidplay.SID_STATUS
    and #$07
    bne sidSing                  // voices still sounding
    //  The song has finished. One fewer repeat, or round again.
    lda sidRepeats
    beq sidRestart               // 0 = forever
    dec sidRepeats
    beq sidStopNow               // that was the last play
sidRestart:
    jsr sidSongInit
    jmp sidLoop
sidSing:
    lda sidDelay
    sta kArg+1                   // sysSleep's count (kArg+1)
    lda #SYS_SLEEP
    jsr syscall                  // never a tight loop -- this also makes the
    jmp sidLoop                  //   ZP contract above load-bearing
sidStopNow:
    jsr sidplay.sidHush          // silence, whatever the repeat value was
    lda #$FF
    sta sidOwner                 // released only here, by the player itself
    clc
    rts

//  sidScaleDelay -- A = ticks slept per jiffy at 100% (never 0) -> A scaled
//  by the machine's speed, so the tune's tempo is the same in REAL time on
//  any machine. A faster machine's jiffies are shorter, so it must sleep MORE
//  ticks per jiffy: a positive sysSpeedShift is a left shift. The result
//  saturates at 255 and is floored at 1 -- a delay of 0 would be a tight loop,
//  which is the one thing this loop must never become.
sidScaleDelay:
    ldy sysSpeedShift
    beq sdsRet
    bmi sdsDown
sdsUp:
    asl
    bcs sdsMax
    dey
    bne sdsUp
    rts
sdsMax:
    lda #255
    rts
sdsDown:
    lsr
    iny
    bne sdsDown
    cmp #1
    bcs sdsRet
    lda #1
sdsRet:
    rts

//  Start (or restart) the song: X/Y = the song's first byte.
sidSongInit:
    lda sidplay.StreamMode
    beq ssiRam
    jsr sidplay.sidStreamBegin   // X/Y = the size words, at file offset 2
    jmp ssiGo
ssiRam:
    lda sidTuneLo
    clc
    adc #2
    tax
    lda sidTuneHi
    adc #0
    tay
ssiGo:
    jsr sidplay.sidInitSong      // X/Y = song start
    lda #$07
    sta sidplay.SID_STATUS       // enable all three voices
    rts

// ==========================================
//  sidHush -- kSidHush: silence the SID, and stop a running player
// ==========================================
//  Input: none.  Output: none.  Clobbers: A, X, Y (the body's hush writes the
//  SID registers and nothing else).
//
//  Safe to call with no tune loaded, from any task, at any time: with no player
//  running it is a plain silence, which is what `sidrst`-style cleanup and a
//  demo's exit path want. With a player running it also raises `sidStop`, and
//  the player hushes and releases the SID as it leaves the loop.
//
//  WARNING: The owner is NOT cleared here. Clearing it would let a third task claim
//  the player while the current one is still inside the loop -- the owner is
//  released by the player itself, or reclaimed by liveness in `sidPlay`.
sidHush:
    php
    sei
    lda sidOwner
    cmp #$FF
    beq sidHushIdle              // nobody playing: just silence the chip
    lda #1
    sta sidStop
sidHushIdle:
    plp
    jmp sidplay.sidHush          // tail call -- silences all three voices

// ==========================================
//  Library data (one copy, shared by every instance -- which is what makes the
//  exclusivity above possible, and why only one task may play at a time)
// ==========================================
sidOwner:    .byte $FF           // playing task's slot, $FF = the SID is free
sidStop:     .byte 0             // non-zero -> the player should stop and hush
sidTuneLo:   .byte 0             // the tune image (the caller's buffer)
sidTuneHi:   .byte 0
sidDelay:    .byte 1             // ticks slept per jiffy
sidRepeats:  .byte 1             // 0 = forever, else plays remaining

.import source "sidplay.inc"

.label sidEnd = *
