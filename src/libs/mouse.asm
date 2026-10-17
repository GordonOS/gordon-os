// ==========================================
//  mouse.asm -- dynamic library "mouse.lib" (lib 8, 1 entry)
// ==========================================
//  The Commodore 1351 on CONTROL PORT 2: the SID's analog POT lines, read
//  through the PRA $DC00 multiplexer. The protocol is settled and documented in
//  docs/lib-mouse.md; this file is the reader, and it is DEVICE ONLY -- it never
//  consults `mouseKind`, which stays the consuming task's business.
//
//      kMouseCall   call 41 -- A = opcode
//        MOUSE_READ     -> kArg+0..3 = raw x, raw y, signed delta x, signed
//                          delta y; A = the buttons (PA5..PA0, 1 = released);
//                          C=0. C=1 = the reader is disabled and nothing moved.
//        MOUSE_ENABLE   -> start tracking; the next read primes the position
//                          and reports delta 0, so a consumer never sees the
//                          jump from a stale sample.
//        MOUSE_DISABLE  -> stop tracking (MOUSE_READ then refuses).
//      Anything else in A: C=1, nothing changed.
//
//  THE STATE IS PER TASK. A library's `.byte` variables are one copy for every task
//  that declares it, so the reader's record -- the flags, the last position each axis
//  was sampled at, and the button byte that sample read -- is one 4-byte record PER
//  TASK, indexed by `curTask`. That is what lets two tasks read the mouse at once: a
//  pot reports an ABSOLUTE position, so each reader differences against its OWN last
//  sample and neither one consumes the other's movement. The stream is still one
//  device -- two readers see the same travel, which is the truth of one mouse.
//  A consumer that needs its own 16-bit total still integrates the deltas itself
//  (which is what lets it clamp), exactly as `mousetest` does.
//
//  WARNING: THE DELTA IS SIGNED. The 1351 reports a wrapping position mod 64, so
//  the distance between two samples is only 6 bits wide and the direction is
//  ambiguous at 32: values 33..63 are one step BACK, and the reader folds them to
//  -31..-1 (the usual 6-bit signed conversion). `raw & $3f` differences recover
//  the unsigned view a consumer may want for a display.
//
//  WARNING: A READ TAKES CIA #1 PORT A FOR ~570 CYCLES. The buttons share that
//  port with the PRA bits that select the port, and the kernel's keyboard scan
//  WRITES $DC00 to drive the matrix columns, so the read has to make DDRA per bit
//  and restore BOTH registers -- all of it inside SEI, because a scan inside the
//  window would drive nothing and see a phantom "no keys". The cost is one
//  keyboard scan and possibly one raster commit (its deadline guard skips, and
//  the next tick commits); the SID resamples its pot lines every 512 cycles, so
//  the wait is not optional -- a shorter one reads an unsettled value.
//
//  EAGER, like gfx / fp / sprite: no cold stub, so libInitLo/Hi points the entry
//  at `libTrap` and dynLoad loads this library at spawn.
//  All labels prefixed mouse* (one global KickAssembler namespace).

.encoding "screencode_mixed"

// (kernel aliases + mouse.inc constants are imported globally by
//  tools/assemble-tasks.asm -- do not re-declare them here.)

mouseBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word mouseEnd - mouseCode   // +5:  code size
    .byte 0, 0, 0                // +7:  data size, ZP base, ZP len (libs: none)
    .word 0                      // +10: reserved (libs consume no libMask)
    .byte mouseNameEnd - mouseName  // +12: name len
mouseName:
    .text "mouse"
mouseNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - mouseBinStart), 0

mouseCode:
    .word mouseExec              // entry table -- 1 entry = call 41 (kMouseCall)

// --- the port select, its direction byte, and the button mask ---
//  PRA bit 7 sets the pot mask that routes control port 2 to the SID's paddles
//  (`set_joyport_pot_mask((b >> 6) & 3)`, mask 2 = port 2); bit 6 would be port 1.
//  DDRA = %11000000 makes PA7/PA6 outputs (the select) and PA5..PA0 inputs (the
//  buttons) -- per bit, because a whole-port flip cannot leave the buttons read.
.const MOUSE_SEL_PORT2 = $80
.const MOUSE_SEL_BOTH  = $c0    // PA7 and PA6: written FIRST, so the select CHANGES
                                //   before port 2 is chosen. The SID resamples its
                                //   pot lines on a change of the port mask, so a
                                //   program that always writes the same byte never
                                //   sees the mouse move -- and a task that owns the
                                //   display has no keyboard scan rewriting $DC00 to
                                //   provide that change for it.
.const MOUSE_DDRA      = %11000000
.const MOUSE_BTN_MASK  = %00111111

// --- the wait after the select, in `dey`/`bne` iterations of 5 cycles each ---
//  110 x 5 = 550 cycles: past the 512-cycle resample the pot lines are latched
//  on, and the shortest wait that is enough.
.const MOUSE_HOLD = 110

// --- the per-task record's flag bits ---
.const MOUSE_FLAG_ON     = %00000001  // this task is tracking
.const MOUSE_FLAG_PRIMED = %00000010  // ...and has a previous sample to difference

// ==========================================
//  mouseExec -- kMouseCall: the library's one call entry
// ==========================================
//  A = opcode (mouse.inc). An unknown opcode returns C=1 and changes nothing,
//  which is what tells a wrong libMask from a wrong opcode: a task that never
//  declared LIB_MOUSE reaches `libTrap` instead, and this entry is never patched.
mouseExec:
    cmp #MOUSE_READ
    beq mouseRead
    cmp #MOUSE_ENABLE
    beq mouseEnable
    cmp #MOUSE_DISABLE
    beq mouseDisable
    sec                          // unknown opcode: refuse
    rts

// --- mouseEnable -- start tracking THIS task ---
mouseEnable:
    ldx curTask
    txa
    asl
    asl                          // curTask * 4: this task's record
    tay
    lda #MOUSE_FLAG_ON           // the priming bit stays CLEAR: the next read records
    sta mouseState,y             //   the position and reports a zero delta
    clc
    rts

// --- mouseDisable -- stop tracking THIS task ---
//  Nothing is uninstalled: a read takes the CIA port for itself each time and
//  hands it back, so there is no state outside this library to put right.
mouseDisable:
    ldx curTask
    txa
    asl
    asl
    tay
    lda #0
    sta mouseState,y
    clc
    rts

// ==========================================
//  mouseRead -- MOUSE_READ: sample the hardware, publish the deltas
// ==========================================
//  Output: kArg+MOUSE_RAW_X/Y, kArg+MOUSE_DELTA_X/Y, A = buttons, C=0.
//          C=1 (and kArg untouched) while the reader is disabled.
//  Clobbers: A, X, Y. kPtr is NOT touched -- only $DC00/$DC02 and this library's
//  own bytes are written, and both port registers are handed back as found.
mouseRead:
    ldy curTask
    tya
    asl
    asl                          // curTask * 4: this task's record
    tay
    lda mouseState,y
    and #MOUSE_FLAG_ON
    bne mouseSample              // this task is tracking
    sec                          // disabled: read nothing, publish nothing
    rts

mouseSample:
    php
    sei
    lda $dc02
    pha
    lda $dc00
    pha
    lda #MOUSE_DDRA
    sta $dc02
    lda #MOUSE_SEL_BOTH          // a CHANGE of the mask is what makes the SID
    sta $dc00                    //   resample its pot lines, so select the other
    lda #MOUSE_SEL_PORT2         //   port first and then the one wanted
    sta $dc00
    jsr mouseHold                // let the SID complete a conversion
    lda $d419                    // POTX -- the selected port's x axis
    sta kArg                     // ...published as it is read: the raw pair IS the
    lda $d41a                    //   caller's kArg+0/+1
    sta kArg+1
    //  The buttons go into this task's OWN record, because the axes below run with
    //  the interrupts on and a shared byte could be rewritten under them.
    ldy curTask
    tya
    asl
    asl
    tay                          // this task's record
    lda $dc00                    // the buttons, PA5..PA0, active low
    and #MOUSE_BTN_MASK
    sta mouseState+3,y
    pla
    sta $dc00                    // both registers back as they were found
    pla
    sta $dc02
    plp

    lda mouseState,y             // the flags
    and #MOUSE_FLAG_PRIMED
    beq mouseFirst               // no previous sample yet: record it, report a zero

    //  --- per axis: difference against THIS task's own last sample. A pot reports an
    //      absolute position, so this neither consumes nor is consumed by another
    //      task reading the same device ---
    iny                          // -> this record's x position byte
    ldx #0                       // the axis
mouseAxis:
    lda kArg,x                   // the raw byte the sample published
    lsr
    and #MOUSE_DEC_AND           // the position now, mod 64
    pha                          // parked: Y is this record's index
    sec
    sbc mouseState,y             // (now - last) & $3f: the distance moved
    and #MOUSE_DEC_AND
    cmp #33                      // 0..32 is forward, 33..63 is one step back
    bcc mouseStored
    sec
    sbc #64                      // -> -32..-1, two's complement
mouseStored:
    sta kArg+MOUSE_DELTA_X,x
    pla
    sta mouseState,y             // the position this sample is measured from
    iny                          // -> the y axis's position byte
    inx
    cpx #2
    bcc mouseAxis
    lda mouseState,y             // the buttons (Y is the record's last byte by now)
    clc
    rts

    //  --- the first read after an enable: the position is recorded and BOTH deltas are
    //      published as zero, so `enable` cannot hand a consumer the jump from a stale
    //      sample. WARNING: kArg is shared scratch, so a delta left unwritten is not
    //      "no delta" -- it is whatever the last kernel call put there.
mouseFirst:
    lda mouseState,y
    ora #MOUSE_FLAG_PRIMED
    sta mouseState,y
    lda kArg                     // raw x
    lsr
    and #MOUSE_DEC_AND
    sta mouseState+1,y
    lda kArg+1                   // raw y
    lsr
    and #MOUSE_DEC_AND
    sta mouseState+2,y
    lda #0
    sta kArg+MOUSE_DELTA_X
    sta kArg+MOUSE_DELTA_Y
    lda mouseState+3,y           // the buttons the sample read
    clc
    rts

// --- mouseHold -- burn the >512 cycles a settled conversion needs ---
//  WARNING: this runs inside the read's SEI window, so the kernel's tick is held
//  for the whole of it (~570 cycles including the call overhead): one keyboard
//  scan is skipped, and a raster commit that lands past its deadline skips a
//  frame. Both are recovered on the next tick, and the wait is what buys a
//  settled ADC value.
mouseHold:
    ldy #MOUSE_HOLD
mouseHoldLp:
    dey
    bne mouseHoldLp
    rts

// ==========================================
//  the reader's state -- ONE record PER TASK
// ==========================================
//  4 bytes a task, indexed by curTask: +0 the flags, +1 the x position mod 64, +2 the
//  y position, +3 the buttons the last sample read. Per TASK rather than one shared
//  copy because a pot reports an ABSOLUTE position: each reader differences against its
//  own last sample, so two tasks reading the mouse at once each see the whole of the
//  movement instead of taking turns at it.
mouseState: .fill MAX_TASKS * 4, 0

mouseEnd:
