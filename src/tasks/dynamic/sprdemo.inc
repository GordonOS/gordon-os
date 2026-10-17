// ==========================================
//  GordonOS -- sprdemo: bouncing balls
// ==========================================
//  Eight coloured balls -- one per sprite slot -- launched from random positions
//  at random speeds in random directions. They bounce off the screen edges and
//  off each other, and run forever: q exits, and the shell can kill the task
//  like any other. No text: it is a picture, and its only asset is ball.spr --
//  one filled 24x21 disc; godzi.tsk ships boat.spr and godzi.spr.
//
//  DEMO FIRST, TEST SECOND. What it proves, in continuous motion: all eight
//  slots across both VIC bands, one sheet record per slot,
//  kSpritePos's ninth bit as balls cross x=256, and per-sprite colour. The nine
//  test-vector modes live in removed/sprdemo-modes.inc -- including mode 7, the
//  tear procedure, the only standing way to re-measure the commit deadline
//  guard.
//
//  PHYSICS IS FIXED POINT, never fp.lib: positions and velocities are
//  (fraction, integer) byte pairs, velocities are SIGNED, so integrating is an
//  `adc` chain and a bounce is a two's-complement negation. Nothing multiplies,
//  divides or takes a square root.
//
//  WARNING: x NEEDS THREE BYTES, y TWO. x runs 24..320 (see the arena note below),
//  which does not fit in one byte, so x is
//      XH (integer high, 0/1) : XL (integer low) : XF (fraction)
//  with a matching signed velocity (VXH sign-extended, VXL, VXF). y never
//  crosses a byte boundary (50..229), so y is (YI, YF) and its velocity needs
//  only (VyI, VyF).
//  The hit test carries a subtler x-width hazard: dx = -256 has a low byte of 0,
//  so |dx| must be bounded BEFORE the negation or it reads as 0 (see
//  sprdHdxNeg). If a ball ever moves along one axis only, or two balls intersect
//  without bouncing, suspect a width mismatch.
//
//  WARNING: COLLISION IS SOFTWARE, NOT $D01E -- and NOT because $D01E is crude. The
//  VIC's sprite-sprite collision is PIXEL-ACCURATE: it is detected when two
//  sprite data sequencers output a non-transparent pixel at the same time
//  (VIC-II article section 3.8.2), so for these balls it is exactly "the drawn ellipses
//  overlap". What the register cannot give is the two things this demo's
//  response needs:
//    * pair identity -- $D01E is one bit per slot, OR-accumulated until read, so
//      it says "slot 3 touched something", not "slot 3 touched slot 5". The
//      physics needs the pair to reflect the right two velocities.
//    * geometry -- which axis is deeper, i.e. which component to reverse.
//  So the hardware test would report a collision and still leave both questions
//  to software. The 28 pairs are therefore tested here against the BALL'S OWN
//  ELLIPSE (sprdHitT). $D01E is per-pixel, NOT a rectangle test over the 24x21
//  box -- a hardware assertion of this kind must be read, never recalled
//  (Rule 9).
//
//  WARNING: THE HIT SHAPE IS THE DRAWN ELLIPSE, not a bounding box. A box as big as
//  the sprite (24x21) bounces balls apart with a gap on a diagonal approach,
//  because the box corners are empty pixels; a smaller box lets the centres
//  close past the art's own edge. The ellipse table (sprdHitT) removes the
//  judgement call: it says "touch" exactly when the drawn ellipses do.
//
//  WARNING: A COLLISION CHANGES DIRECTION ONLY -- never speed. The response reflects
//  the deeper axis for BOTH balls, so each keeps the speed it was launched with;
//  exchanging the two velocity vectors (the textbook equal-mass answer) would
//  swap their speeds instead, which is not what this demo is for. The reflection
//  is guarded by an "are they closing?" test on that axis: without it, two slow
//  balls resting in contact would re-reflect every tick and buzz instead of
//  separating.
//
//  WARNING: ONE frame load per ball, at entry, and none afterwards. A frame load
//  rewrites a slot while the VIC is fetching it, which can split that sprite for
//  a single frame (~8% per load), so eight balls reloading on
//  a schedule would put seams on screen several times a second. The balls hold
//  one frame each and the motion is entirely position.
//
//  Every iteration yields (Rule 13), so the shell stays usable while it runs.
// ==========================================

//  The arena is the DISPLAY WINDOW, not sprite-coordinate 0..320. The 40-column
//  window's left edge is at sprite x = 24 and its right edge at x = 344, so a
//  ball's left edge belongs in 24..320 (320 + its own 24 px = the right edge).
//  Below 24 the ball reads half off the left border; above 320 it sits past the
//  window's right edge. y needs no such offset -- a sprite at y = 50 is displayed
//  from raster line 51, which IS the window's first line.
.const SPRD_BALLS  = 8
.const SPRD_BXMIN  = 24         // the display window's left edge
.const SPRD_BXMAX  = 320        // 344 minus a ball's own 24 px width
.const SPRD_BYMIN  = 50         // the visible window opens at raster line 51
.const SPRD_BYMAX  = 229        // last y that keeps a 21-line ball on screen
//  Ball-ball contact: the balls are ellipses spanning the whole 24x21 box -- the
//  art (assets/sprites/ball.txt) is a filled disc of radii 12 x 10.5 px, so it
//  touches all four edges -- which means they touch when
//      (dx/24)^2 + (dy/21)^2 = 1
//  and sprdHitT holds, for each |dx|, the largest |dy| that still satisfies it,
//  plus one -- the test is `cmp #limit / bcs out`. One 24-byte lookup per pair,
//  no multiply, exact to the pixel. To re-derive the 24 numbers: for each
//  |dx| = 0..23 take the largest |dy| with 441-dx^2 + 576-dy^2 < 254016, then add
//  one; the first eight rows are therefore 21 (a ball directly above another
//  touches until |dy| = 20, i.e. until the 21-line ellipses overlap).
.const SPRD_HITN   = 24         // no contact is possible at |dx| >= this
.const SPRD_HITNEG = 256 - SPRD_HITN + 1   // ...so the most negative dx that can
                                // still touch is -23, whose low byte is 233: a
                                // negative dx has |dx| = 256 - low byte
//  Spawn jitter inside one x slot. A ball starts at BXMIN + 32*SLOT + 0..this,
//  so the closest a pair can START in x is 32 - this = 25 px, which is past
//  SPRD_HITN -- and sprdHit rejects a pair on that first compare, before it reads
//  the table. Nothing else then has to be checked, and no seed can defeat it.
.const SPRD_SPAWNJIT = 7

sprdemo:
    //  Take a surface AND the display: the sprite API is focus-owner-only, so
    //  nothing would be accepted until kSwitchView has made us the owner.
    lda #0
    ldx #$0b; stx kArg           // border: dark grey
    ldx #0;   stx kArg+1         // bg0: BLACK
    stx kArg+2; stx kArg+3
    //  WARNING: Both colours are set EXPLICITLY: a sprite whose colour matches the
    //  background is drawn perfectly and is completely invisible, and every
    //  register still looks right.
    jsr kAllocScreen
    bcs sprdNoScreen
    jmp sprdScreen
sprdNoScreen:
    lda #SYS_YIELD; jsr syscall  // no screens: idle forever
    jmp *-3

sprdScreen:
    jsr kDisableCursor           // a graphics surface: no cursor, and no text
    lda #SYS_GETTID
    jsr syscall
    tax
    //  WARNING: TAKE THE DISPLAY WITHOUT TAKING THE OUTGOING CURSOR BLOCK WITH IT.
    //  The blink cursor is drawn INTO the framebuffer pixels, and kSwitchView
    //  copies the whole framebuffer into the outgoing surface's REU slot
    //  (doGfxSwitch -> surfSave) -- so if the block happens to be drawn at that
    //  instant it is baked into the shell's saved image, and dgsDone then clears
    //  cursorVisible, so nothing will ever restore that cell. The result is a
    //  phantom cursor at the cell the shell's cursor was on: it sits where the
    //  user pressed ENTER on `run sprdemo`, and only a screen clear removes it,
    //  since only a pixel write can erase a baked block.
    //  kBlinkReset un-draws the block at the DISPLAYED surface's saved position,
    //  which at this moment is the shell's. The SEI is not decoration: between
    //  the erase and the switch an IRQ tick would draw the block straight back.
    //  X must survive the call (kBlinkReset clobbers A, X, Y, kPtr, kArg+7), and
    //  no syscall may be used inside the window -- the dispatcher re-enables IRQs.
    php
    sei
    txa
    pha
    jsr kBlinkReset              // un-draw a live blink block (no-op if OFF)
    pla
    tax
    jsr kSwitchView              // display it + take kbFocus
    plp
    jsr sprdSeed                 // move the kernel RNG on before drawing numbers
    jsr sprdLaunch               // art, positions, velocities, colours, enable
sprdLoop:
    jsr kReadKey                 // non-blocking: A = screen code, 0 = none
    cmp #'q'
    beq sprdExit
    //  WARNING: ONE UPDATE PER FRAME, not per scheduler tick -- this is what makes the
    //  motion smooth. The tick runs at 59.2 Hz and the display samples at 50.1 Hz,
    //  so a per-tick animation gets 1.18 updates per frame: some frames show two
    //  steps and the next shows none, which reads as a stutter no matter how good
    //  the speed arithmetic is. `frameCount` is incremented by the kernel's raster
    //  commit at line 0, so it is the display's own clock; gating on it makes the
    //  displayed motion exactly the sequence computed here -- one step per frame,
    //  published by that frame's commit.
    lda frameCount
    cmp SPRD_LAST
    beq sprdNoStep               // same frame as last time -> nothing to do
    sta SPRD_LAST
    jsr sprdTick                 // integrate, bounce, collide
sprdNoStep:
    lda #SYS_YIELD
    jsr syscall                  // never starve the scheduler
    jmp sprdLoop
sprdExit:
    lda #SYS_EXIT
    jsr syscall

// ==========================================
//  sprdSeed -- move the kernel RNG an unpredictable number of steps
// ==========================================
//  kRandom is a Galois LFSR whose state lives in the kernel (`randomSeed`,
//  seeded $42 at boot). A fixed seed is fine *within* a run, but it makes the
//  first run after every boot identical, so we advance the sequence by a count
//  taken from SID voice 3's noise output.
//
//  WARNING: ADVANCE, DO NOT WRITE. The state variable is kernel-owned and not exported
//  to tasks, and **0 is the LFSR's lock-up state** -- a task writing it could
//  silently stop randomness for the whole OS, not just here. Advancing costs at
//  most 255 cheap calls, touches nothing shared, and cannot lock anything.
//
//  WARNING: Voice 3's registers are SAVED AND RESTORED because sidplay.tsk owns the
//  SID when it runs (Rule 13). $D418 (volume) is never touched, and voice 3 is
//  gated off again within a few hundred cycles, so no sound is produced.
//
//  The two delays are counted off $D012 rather than being fixed, so the noise
//  value depends on WHEN the task was started, not just on the chip's power-on
//  state -- which is what makes this work under an emulator that resets the SID
//  identically every time.
sprdSeed:
    lda $d40e; pha               // save voice 3 frequency lo/hi + control
    lda $d40f; pha
    lda $d412; pha
    lda #$ff
    sta $d40e                    // frequency $ffff -- the fastest noise rate
    sta $d40f
    lda #$81                     // noise waveform + GATE (the oscillator only
    sta $d412                    // runs while the gate is set)
    ldy $d012                    // the beam position at entry: this is what
sprdSeedD1:
    dey                          // varies run to run. ~5 cycles an iteration, so
    bne sprdSeedD1               // the delay tracks the beam, not a constant.
    lda $d41b                    // OSC3 output -- the noise byte
    sta SPRD_T2                  // first sample
    ldy $d012
sprdSeedD2:
    dey
    bne sprdSeedD2
    lda $d41b
    eor SPRD_T2                  // second sample, mixed with the first
    sta SPRD_BI                  // 0..255 steps to advance
    lda #0
    sta $d412                    // voice 3 OFF -- gate down, silent
    pla; sta $d412               // then the caller's values back, in reverse
    pla; sta $d40f
    pla; sta $d40e
    lda SPRD_BI
    beq sprdSeedDone             // a zero count is legal, just no extra variety
sprdSeedLoop:
    jsr kRandom                  // clobbers A only -- X and Y survive
    dec SPRD_BI
    bne sprdSeedLoop
sprdSeedDone:
    rts

// ==========================================
//  sprdLaunch -- art, positions, velocities, colours, enable
// ==========================================
//  ball.spr into all eight slots: one sheet record PER SLOT, at
//  ~1,000-3,000 cycles each and SEI'd. That is the API's rule -- a slot's art
//  location is indexed by slot, so resolving once leaves the other slots holding
//  stale RAM that reads as "garbage sprite" -- and it is paid once.
sprdLaunch:
    lda #<sprdBallName
    sta kPtr
    lda #>sprdBallName
    sta kPtr+1
    lda #0
    sta SPRD_SLOT
sprdLSheet:
    lda SPRD_SLOT                // A = this ball's slot = its index
    jsr kSpriteSheet
    inc SPRD_SLOT
    lda SPRD_SLOT
    cmp #SPRD_BALLS
    bcc sprdLSheet
    lda #0
    sta SPRD_SLOT
sprdLFrame:
    ldx #0                       // X = frame 0 -- the whole motion is position
    lda SPRD_SLOT
    jsr kSpriteFrame
    inc SPRD_SLOT
    lda SPRD_SLOT
    cmp #SPRD_BALLS
    bcc sprdLFrame
    //  Per ball: a spawn slot, a random y, a random speed and direction per
    //  axis, and its own colour. kRandom clobbers A only, so the loop index stays
    //  in ZP and X carries the ball index across every call.
    lda #0
    sta SPRD_BI
sprdLInit:
    ldx SPRD_BI
    //  x comes from ONE OF EIGHT 32-px SPAWN SLOTS -- slot = the ball index -- so
    //  no two balls can share a slot, and the closest a pair can start in x is
    //  32 - SPRD_SPAWNJIT = 25 px. That is past SPRD_HITN, so sprdHit rejects the
    //  pair on its first compare and NO two balls can overlap at launch, whatever
    //  the seed. A free draw over the whole window cannot promise that.
    lda SPRD_BI
    asl
    asl
    asl
    asl
    asl                          // x32 -> the slot's left edge offset
    clc
    adc #SPRD_BXMIN              // 24 + 32*slot = 24, 56 ... 248
    sta sprdXL,x
    jsr kRandom
    and #SPRD_SPAWNJIT
    clc
    adc sprdXL,x                 // + 0..7 -> 24..255, so the 9th bit stays 0
    sta sprdXL,x
    lda #0
    sta sprdXF,x                 // the fraction fills in as the ball moves
    sta sprdXH,x
    //  y is drawn freely over the whole window. It needs no rule of its own:
    //  the slots have already separated the eight in x.
    jsr kRandom
    and #$7f
    clc
    adc #SPRD_BYMIN              // y = 50..177; bouncing reaches the rest
    sta sprdYI,x
    lda #0
    sta sprdYF,x
    //  vx: a fraction and an integer part from two draws, giving 0.31..1.81
    //  px per FRAME -- some fast, some slow, none stalled, and none moving so
    //  fast that the once-per-frame step is a blur -- then a sign from a third.
    jsr kRandom
    and #$7f
    clc
    adc #$50                     // fraction $50..$CF
    sta sprdVxF,x
    jsr kRandom
    and #1                       // integer 0 or 1 -> the top half of the range
    sta sprdVxL,x
    lda #0
    sta sprdVxH,x                // sign extension: +1 is $00:$01
    jsr kRandom
    and #$80
    beq sprdLVxPos
    jsr sprdNegVX                // X = ball index
sprdLVxPos:
    //  vy: three INDEPENDENT draws, so the slopes vary instead of every ball
    //  running at 45 degrees
    jsr kRandom
    and #$7f
    clc
    adc #$50
    sta sprdVyF,x
    jsr kRandom
    and #1
    sta sprdVyI,x
    jsr kRandom
    and #$80
    beq sprdLVyPos
    jsr sprdNegVY
sprdLVyPos:
    //  colour: one palette entry per ball, so they all look different. The
    //  shadow offset is per-slot, and the slot is the ball's index.
    ldy SPRD_BI
    lda sprdPal,y
    pha                          // stash the colour: X is rebuilt below
    tya
    clc
    adc #SPR_SHD_COLOUR
    tax
    pla
    jsr kSpriteReg
    inc SPRD_BI
    lda SPRD_BI
    cmp #SPRD_BALLS
    bcs sprdLInitEnd
    jmp sprdLInit                // out of branch range: the window offset and
sprdLInitEnd:                    // the sign handling grew the loop body (Rule 4)
    //  place them where the random numbers put them, and light all eight
    lda #0
    sta SPRD_BI
sprdLPlace:
    ldx SPRD_BI
    jsr sprdPut
    inc SPRD_BI
    lda SPRD_BI
    cmp #SPRD_BALLS
    bcc sprdLPlace
    //  The control bytes are plain shadow writes: the byte that is live belongs
    //  to the displayed task alone, so a mask is this task's own set of eight
    //  and nothing has to be merged. The colour write above is per-slot-indexed
    //  for the same reason -- every slot is ours.
    sprPriority(%00000000)       // in front of anything they cross
    sprOn(%11111111)             // all eight balls
    rts

// ==========================================
//  sprdTick -- integrate, bounce, collide
// ==========================================
//  Called once per FRAME (see the main loop), which is what the velocities are
//  expressed in. One frame = 50.1 Hz, so 1.0 px/frame ~= 50 px/s.
sprdTick:
    //  1. MOVE -- every ball. The loop index lives in ZP: kSpritePos takes its
    //  arguments in A, X and Y, so no index may be held in a register.
    lda #0
    sta SPRD_BI
sprdTMove:
    ldx SPRD_BI
    lda sprdXF,x                 // x += vx, three bytes: fraction, integer low,
    clc                          // then the integer high carrying the velocity's
    adc sprdVxF,x                // sign extension, so a negative velocity borrows
    sta sprdXF,x                 // straight through $0100 and $0000 with no
    lda sprdXL,x                 // special case at all
    adc sprdVxL,x
    sta sprdXL,x
    lda sprdXH,x
    adc sprdVxH,x
    sta sprdXH,x
    lda sprdYF,x                 // y += vy, two bytes: y never crosses a byte
    clc                          // boundary (50..229), so the integer part is a
    adc sprdVyF,x                // plain 8-bit signed add
    sta sprdYF,x
    lda sprdYI,x
    adc sprdVyI,x
    sta sprdYI,x
    jsr sprdBounce               // clamp at a wall, reversing that component
    jsr sprdPut                  // then show it there
    inc SPRD_BI
    lda SPRD_BI
    cmp #SPRD_BALLS
    bcc sprdTMove
    //  2. COLLIDE -- every pair (i<j) exactly once: 28 tests for eight balls
    lda #0
    sta SPRD_BI
sprdTOuter:
    lda SPRD_BI
    clc
    adc #1
    sta SPRD_BJ
sprdTInner:
    jsr sprdHit
    inc SPRD_BJ
    lda SPRD_BJ
    cmp #SPRD_BALLS
    bcc sprdTInner
    inc SPRD_BI
    lda SPRD_BI
    cmp #SPRD_BALLS-1
    bcc sprdTOuter
    rts

// ==========================================
//  Helpers
// ==========================================

//  Place one ball. X = ball index. kSpritePos takes the sprite in X, x in A with
//  the 9th bit in the CARRY, and y in Y -- and clobbers all three.
//  sprdBounce has already run, so XH is 0 or 1 here.
sprdPut:
    //  X = ball index on entry, and kSpritePos wants the SLOT in X -- which is
    //  the same number here, so X is already right and the carry is untouched
    //  until the `lsr` below sets it from the x high byte.
    lda sprdXL,x                 // x low byte
    pha
    lda sprdYI,x                 // y
    pha
    lda sprdXH,x                 // x high byte (0 or 1)
    lsr                          // C = x bit 8 (XH is only ever 0 or 1)
    pla
    tay                          // Y = y
    pla                          // A = x low byte
    jmp kSpritePos               // tail call
//  Clamp one ball inside the arena, reversing whichever component hit a wall.
//  X = ball index. Clobbers A, Y.
sprdBounce:
    jsr sprdBounceX
    //  ---- y: 50..229 ----
    lda sprdYI,x
    cmp #SPRD_BYMIN
    bcc sprdByLow
    cmp #SPRD_BYMAX+1
    bcs sprdByHigh
    rts
sprdByLow:
    lda #SPRD_BYMIN
    sta sprdYI,x
    lda #0
    sta sprdYF,x
    jmp sprdNegVY                // tail call -- its rts returns to our caller
sprdByHigh:
    lda #SPRD_BYMAX
    sta sprdYI,x
    lda #0
    sta sprdYF,x
    jmp sprdNegVY

//  ---- x: 24..320. BOTH bounds need the 9th bit: 24 is below any wrap and 320
//  (=$0140) is above 255, so the high byte is what decides which side we are on.
//  Each exit is a `jmp` to the negate helper, whose rts returns to the y check
//  above -- so a ball can hit an x wall and a y wall in the same tick.
sprdBounceX:
    lda sprdXH,x
    beq sprdBxLowSide            // 0..255 -> only the left bound can be broken
    cmp #$ff
    beq sprdBxLow                // negative -> past the left wall
    cmp #1
    bne sprdBxHigh               // 2+ -> impossibly far past the right wall
    lda sprdXL,x
    cmp #SPRD_BXMAX-$100+1       // 65: an integer above 64 means x > 320
    bcc sprdBxOk
sprdBxHigh:
    lda #SPRD_BXMAX-$100         // pin to 320 = $0140 and turn around
    sta sprdXL,x
    lda #0
    sta sprdXF,x
    lda #1
    sta sprdXH,x
    jmp sprdNegVX
sprdBxLowSide:
    lda sprdXL,x
    cmp #SPRD_BXMIN
    bcs sprdBxOk                 // 24..255 -- inside
sprdBxLow:
    lda #SPRD_BXMIN              // pin to 24 and turn around
    sta sprdXL,x
    lda #0
    sta sprdXF,x
    sta sprdXH,x
    jmp sprdNegVX
sprdBxOk:
    rts

//  Reverse one velocity component: an exact two's-complement negation, so a
//  ball's speed is identical either side of a bounce and nothing drifts over a
//  long run. X = ball index. Clobbers A.
sprdNegVX:
    lda sprdVxF,x
    eor #$ff
    clc
    adc #1
    sta sprdVxF,x
    lda sprdVxL,x
    eor #$ff
    adc #0
    sta sprdVxL,x
    lda sprdVxH,x
    eor #$ff
    adc #0
    sta sprdVxH,x
    rts
sprdNegVY:
    lda sprdVyF,x
    eor #$ff
    clc
    adc #1
    sta sprdVyF,x
    lda sprdVyI,x
    eor #$ff
    adc #0
    sta sprdVyI,x
    rts

//  Overlap test for the pair (SPRD_BI, SPRD_BJ), and the bounce if they touch.
//  Clobbers A, X, Y.
sprdHit:
    ldx SPRD_BI
    ldy SPRD_BJ
    lda sprdXL,x                 // dx = xi - xj, worked in the integer bytes
    sec
    sbc sprdXL,y
    sta SPRD_T0
    lda sprdXH,x
    sbc sprdXH,y                 // A = the integer-high difference. Two values in
    beq sprdHdxPos               // 0..320 can only give 0, 1, 2, $ff or $fe here,
    cmp #$ff                     // so the test is short: $fe (or 1, 2) means
    bne sprdHout                 // |dx| >= 257 -- nowhere near. $ff means
sprdHdxNeg:                      // dx is -256..-1, and only -1..-23 can touch -- so
    lda SPRD_T0                  // |dx| = 256 - the low byte, and the bound is the
    cmp #SPRD_HITNEG             // low byte: 233 or more. It also drops dx = -256,
    bcc sprdHout                 // whose low byte is 0 (|dx| would read as 0).
    eor #$ff
    clc
    adc #1                       // A = |dx|
    jmp sprdHdxAbs
sprdHdxPos:
    lda SPRD_T0                  // dx = 0..255, and the low byte IS dx
sprdHdxAbs:
    cmp #SPRD_HITN               // |dx| >= 24 -> the ellipses cannot reach
    bcs sprdHout
    sta SPRD_T1                  // keep |dx|: the deeper axis decides which way
    tax                          // X = |dx| -- the ball indices are in ZP, and Y
    lda sprdHitT,x               // must keep holding j -> A = the |dy| limit + 1
    sta SPRD_T2                  // the limit, stashed: both X and Y are busy now
    ldx SPRD_BI
    lda sprdYI,x                 // dy = yi - yj, integer bytes (a sub-pixel gap
    sec                          // is not worth a second compare)
    sbc sprdYI,y
    bcs sprdHdyPos
    eor #$ff
    clc
    adc #1                       // A = |dy|
sprdHdyPos:
    cmp SPRD_T2                  // |dy| past the limit -> the ellipses miss
    bcs sprdHout
    //  They touch. Reflect the DEEPER axis for both balls, so each keeps the
    //  speed it was launched with -- but only if they are closing on that axis,
    //  or two slow balls in contact would re-reflect every tick and buzz.
    cmp SPRD_T1                  // A = |dy| against |dx|
    bcs sprdHaxisY               // |dy| >= |dx| -> stacked -> bounce in y
    jsr sprdClosingX             // side by side -> bounce in x
    bcs sprdHout                 // C=1: not closing, leave them alone
    ldx SPRD_BI
    jsr sprdNegVX
    ldx SPRD_BJ
    jsr sprdNegVX
sprdHout:
    rts
sprdHaxisY:
    jsr sprdClosingY
    bcs sprdHout
    ldx SPRD_BI
    jsr sprdNegVY
    ldx SPRD_BJ
    jsr sprdNegVY
    rts

//  Is ball i closing on ball j along x? C=0 if it is, C=1 if it is not (so the
//  caller skips the reflection).
//  WARNING: Closing is a RELATIVE test: the pair approaches only if the left ball's
//  vx exceeds the right ball's, so both velocities are subtracted and the sign
//  of the difference decides. Reading only ball i's own sign is wrong twice
//  over -- an overtake (both moving the same way, the rear one faster: they pass
//  straight through each other) reads as "not closing", and a separation (both
//  moving the same way, the rear one slower) reads as "closing" and buzzes. The
//  difference can never overflow at these speeds (|vx| < 2), so the N flag of
//  the subtraction IS the signed comparison.
//  WARNING: BOTH bytes of the integer part decide who is on the left -- NOT XL alone.
//  x is XH:XL:XF over 24..320, so a ball at x=262 has XL=6 while one at x=250 has
//  XL=250. Comparing XL alone INVERTS this verdict for every pair that straddles
//  x=256.
//  X = i, Y = j on entry, and neither is touched.
sprdClosingX:
    lda sprdXH,x
    cmp sprdXH,y
    bne sprdClxHi                // the 9th bit decides, 0 or 1 only
    lda sprdXL,x
    cmp sprdXL,y                 // same high byte -> the low byte decides
    bcc sprdClxLeft
sprdClxRight:
    jsr sprdVxCmp                // i to the right: closing iff vx_i < vx_j
    bmi sprdClosing
    sec
    rts
sprdClxHi:
    bcs sprdClxRight             // XH_i > XH_j -> i is the right one
sprdClxLeft:
    jsr sprdVxCmp                // i to the left: closing iff vx_i >= vx_j
    bpl sprdClosing
    sec
    rts
sprdClosing:
    clc
    rts

//  vx_i - vx_j (three bytes). N = 1 iff vx_i < vx_j -- the signed compare, safe
//  because the difference can never overflow. X, Y preserved.
sprdVxCmp:
    lda sprdVxF,x
    sec
    sbc sprdVxF,y
    lda sprdVxL,x
    sbc sprdVxL,y
    lda sprdVxH,x
    sbc sprdVxH,y                // N = sign of the difference
    rts

//  Is ball i closing on ball j along y? C=0 yes / C=1 no. Same relative test:
//  the upper ball's vy must exceed the lower ball's (screen y grows downward).
//  X = i, Y = j on entry, and neither is touched.
sprdClosingY:
    lda sprdYI,x
    cmp sprdYI,y
    bcc sprdClyUp
    jsr sprdVyCmp                // i below: closing iff vy_i < vy_j (moving up)
    bmi sprdClosing
    sec
    rts
sprdClyUp:
    jsr sprdVyCmp                // i above: closing iff vy_i >= vy_j (moving down)
    bpl sprdClosing
    sec
    rts

//  vy_i - vy_j (two bytes). N = 1 iff vy_i < vy_j. X, Y preserved.
sprdVyCmp:
    lda sprdVyF,x
    sec
    sbc sprdVyF,y
    lda sprdVyI,x
    sbc sprdVyI,y                // N = sign of the difference
    rts

// ==========================================
//  Data
// ==========================================
//  Per ball. x is three bytes (integer high, integer low, fraction) because it
//  runs to 296; y is two (integer, fraction) because it never leaves 50..229.
//  Velocities are the matching signed fixed-point values -- vx needs the
//  sign-extended high byte so a borrow propagates out of $0100, vy does not.
sprdXF:  .fill SPRD_BALLS, 0
sprdXL:  .fill SPRD_BALLS, 0
sprdXH:  .fill SPRD_BALLS, 0
sprdYI:  .fill SPRD_BALLS, 0
sprdYF:  .fill SPRD_BALLS, 0
sprdVxF: .fill SPRD_BALLS, 0
sprdVxL: .fill SPRD_BALLS, 0
sprdVxH: .fill SPRD_BALLS, 0
sprdVyF: .fill SPRD_BALLS, 0
sprdVyI: .fill SPRD_BALLS, 0
//  The ball-ball contact table (see sprdHit): for each |dx| of 0..23, the
//  largest |dy| that still has the two ellipses touching, PLUS ONE -- the test
//  is `cmp #limit / bcs out`. Row 0 is |dx| = 0, so the first eight entries say
//  a ball directly above another touches until |dy| = 20, i.e. until the 21-line
//  ellipses actually overlap.
//
//  These 24 numbers are solutions of (dx/24)^2 + (dy/21)^2 < 1, so they are
//  DERIVED, not chosen -- the recipe is in the comment above the test. They are
//  committed here as literals because they describe the committed art
//  (assets/sprites/ball.txt); if the art's shape ever changes, re-derive them
//  rather than nudging individual entries.
sprdHitT:
    .byte 21, 21, 21, 21, 21, 21, 21, 21, 20, 20, 20, 19
    .byte 19, 18, 18, 17, 16, 15, 14, 13, 12, 11,  9,  6
//  One colour per ball -- eight distinct entries, none of them black (bg0), so
//  every ball is visible against the background.
sprdPal: .byte 7, 3, 10, 13, 1, 5, 8, 15

//  The assert lives down here, not next to the table, because KickAssembler
//  evaluates `.assert` in the FIRST parse -- it cannot see a label that the
//  assembler has not reached yet. sprdPal bounds sprdHitT, so both must precede
//  it. (Three-argument form: the two-argument form is a parser trap.)
.assert "sprdemo: sprdHitT must cover |dx| 0..23", (sprdPal - sprdHitT == SPRD_HITN) ? 1 : 0, 1

sprdBallName: .text "ball.spr"
           .byte 0
