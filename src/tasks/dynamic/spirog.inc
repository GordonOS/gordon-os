// ==========================================
//  spirog.inc -- integer spirograph (hypotrochoid) drawing
// ==========================================
//  spirog.asm carries the maths, the argument syntax and the ZP map.
//  Flow: parse args -> clamp -> validate -> derive the curve -> allocate a
//  bitmap -> trace N segments -> idle.  One shape uses the hires bitmap,
//  preset 10 the multicolour one.  'q' exits from either state.  Invalid
//  arguments draw the usage line instead of a curve.
//
//  Layout: spirogStart must be the FIRST body label -- the header's code
//  size is spirogEnd - spirogStart and the loader enters there.  Helpers
//  and the sine table follow the main flow.

.encoding "screencode_mixed"

// The no-argument shape: a 9-lobed rosette (R=80 r=35 d=45).
.const SG_DEF_R   = 80
.const SG_DEF_RR  = 35
.const SG_DEF_D   = 45
.const SG_DEF_COL = 1

// kDiv16 returns its quotient and remainder in kResult, which is kArg+4 in
// the ABI -- the task namespace names kArg but not kResult.
.const SG_KR = kArg + 4

spirogStart:
    lda #SG_DEF_R
    sta SG_R
    lda #SG_DEF_RR
    sta SG_RR
    lda #SG_DEF_D
    sta SG_D
    lda #SG_DEF_COL
    sta SG_COL

    // --- parse up to four decimal args (R r d col) ---
    //  The loader leaves `run spirog <args>` at (dataPtr),0, NUL-terminated
    //  (capped at 64 chars).  A missing arg keeps its default.
    lda dataPtr
    ora dataPtr+1
    beq sgClamp
    ldy #0
    lda (dataPtr),y
    beq sgClamp
    jsr sgNum
    bcc sgClamp
    sta SG_R
    jsr sgNum
    bcs sgArgRR                // a second number -> r, d, col
    // only one number: 1..10 picks a preset (a lone R that small is never
    // valid geometry, since r defaults to 35 and must stay smaller)
    lda SG_R
    cmp #11
    bcs sgClamp
    cmp #10
    beq sgMontageGo            // 10 = the random montage
    jsr sgPreset
    jmp sgClamp
sgArgRR:
    sta SG_RR
    jsr sgNum
    bcc sgClamp
    sta SG_D
    jsr sgNum
    bcc sgClamp
    sta SG_COL

sgClamp:
    // --- clamp: R,r,d <= 127, r >= 1, colour 1..15 ---
    lda SG_R
    cmp #128
    bcc sgClR
    lda #127
sgClR:
    sta SG_R
    lda SG_RR
    cmp #128
    bcc sgClRr
    lda #127
sgClRr:
    sta SG_RR
    cmp #1
    bcs sgClD
    lda #1
    sta SG_RR
sgClD:
    lda SG_D
    cmp #128
    bcc sgClDok
    lda #127
sgClDok:
    sta SG_D
    lda SG_COL
    cmp #16
    bcc sgClCok
    lda #15
sgClCok:
    cmp #1
    bcs sgClCset
    lda #1
sgClCset:
    sta SG_COL

    // --- the ring must exceed the rolling circle ---
    lda SG_R
    cmp SG_RR
    bcc sgBad
    beq sgBad
    jsr sgDerive
    jsr sgDraw                 // one hires shape: surface + trace
    jmp sgIdle
sgMontageGo:
    lda #1
    jsr sgSurface              // mode 1 = multicolour, 160 x 200
    jsr sgMontInk              // three colours for the whole montage,
    jsr sgMontPalette          // given to every cell of the screen
    jsr kRandom
    sta SG_SPIN                // random phase for the montage's bag cursor
    lda #1
    sta SG_INK                 // figures take 1, 2, 3, 1, 2, 3, ...
    jsr sgMontage
    jmp sgIdle
sgBad:
    lda #0
    jsr sgSurface              // mode 0 = hires: the usage text takes its
    jsr sgUsage                // pixel colour from the matrix nibble
    // fall through to the idle loop

// --- sgIdle -- picture up: hold it until 'q' ------------------------------
sgIdle:
    jsr sgKey
    jmp sgIdle

// --- sgDraw -- the single shape: allocate the hires bitmap and trace it ---
sgDraw:
    lda #160
    sta SG_CX
    lda #0
    sta SG_CX+1
    lda #100
    sta SG_CY
    lda #1
    sta SG_INK                 // hires: any non-zero value sets the pixel
    lda #0
    jsr sgSurface              // mode 0 = hires
    jsr sgTrace
    rts

// --- sgTrace -- trace one closed curve; theta and phi start at zero -------
//  phi runs at P/Q times theta's rate, so a PHASE OFFSET between the two is
//  not a rotation -- it draws a different, distorted curve.  Every curve
//  therefore starts here, at zero.
sgTrace:
    lda #0
    sta SG_TH
    sta SG_TH+1
    sta SG_PH
    sta SG_PH+1
    jsr sgPoint
    lda SG_SX
    sta SG_PX
    lda SG_SXH
    sta SG_PXH
    lda SG_SY
    sta SG_PY
    lda #16
    sta SG_CNT

sgSeg:
    // advance both angles (16-bit, wrapping at one turn)
    clc
    lda SG_TH
    adc SG_DTH
    sta SG_TH
    lda SG_TH+1
    adc SG_DTH+1
    sta SG_TH+1
    clc
    lda SG_PH
    adc SG_DPH
    sta SG_PH
    lda SG_PH+1
    adc SG_DPH+1
    sta SG_PH+1
    jsr sgPoint
    jsr sgSegment
    // N -= 1; the curve is closed when it reaches 0
    lda SG_N
    sec
    sbc #1
    sta SG_N
    lda SG_N+1
    sbc #0
    sta SG_N+1
    lda SG_N
    ora SG_N+1
    beq sgTraceDone            // the curve is closed
    dec SG_CNT
    bne sgSeg
    lda #16
    sta SG_CNT
    jsr sgKey
    jmp sgSeg
sgTraceDone:
    rts

// --- sgPoint -- (theta, phi) -> the screen point SX, SY -------------------
//  Four table lookups and four signed 8x8 multiplies:
//      x = (A*cos(t) + B*cos(phi)) >> 7 + centre x
//      y = (A*sin(t) - B*sin(phi)) >> 7 + centre y
//  The centre is SG_CX/SG_CY -- 160,100 for a single shape, and the montage
//  moves it -- so x can pass 255 and carries a high byte, while y always
//  lands inside 0..199 and does not.  In multicolour the x excursion is
//  halved, because a multicolour pixel is twice as wide as it is tall.
sgPoint:
    // sin/cos of theta: cos comes from the same table 64 entries along --
    // the byte wrap in `adc #64` is the modulo-256 index arithmetic.
    lda SG_TH+1
    tax
    lda sgSin,x
    sta SG_ST
    txa
    clc
    adc #64
    tax
    lda sgSin,x
    sta SG_CT
    // sin/cos of phi
    lda SG_PH+1
    tax
    lda sgSin,x
    sta SG_SP
    txa
    clc
    adc #64
    tax
    lda sgSin,x
    sta SG_CP
    // x term
    lda SG_CT
    ldx SG_A
    jsr sgMul
    lda SG_PR
    sta SG_S1
    lda SG_PR+1
    sta SG_S1+1
    lda SG_CP
    ldx SG_B
    jsr sgMul
    clc
    lda SG_S1
    adc SG_PR
    sta SG_S1
    lda SG_S1+1
    adc SG_PR+1
    sta SG_S1+1
    jsr sgS7
    // A multicolour pixel is twice as wide as it is tall, so the x excursion
    // is halved and the figure stays round on screen.  This is an ARITHMETIC
    // shift -- `cmp #$80` puts bit 7 in C and `ror` shifts it back in -- since
    // a logical lsr would turn a small negative offset into a large positive
    // one.  Hires (SG_MODE 0) must not shift at all.
    ldx SG_MODE
    beq sgSxNoHalf
    cmp #$80
    ror
sgSxNoHalf:
    // x = centre + the signed offset, as a 16-bit sum.  The offset is
    // sign-extended first: the carry out of an 8-bit add with a negative
    // byte is not the high byte (160 + (-12) = 148 yet it carries), so only
    // a real 16-bit add can set SG_SXH.
    sta SG_T0
    lda #0
    ldx SG_T0
    bpl sgSxExt
    lda #$ff
sgSxExt:
    sta SG_T1
    clc
    lda SG_T0
    adc SG_CX
    sta SG_SX
    lda SG_T1
    adc SG_CX+1
    sta SG_SXH
    // y term
    lda SG_ST
    ldx SG_A
    jsr sgMul
    lda SG_PR
    sta SG_S1
    lda SG_PR+1
    sta SG_S1+1
    lda SG_SP
    ldx SG_B
    jsr sgMul
    sec
    lda SG_S1
    sbc SG_PR
    sta SG_S1
    lda SG_S1+1
    sbc SG_PR+1
    sta SG_S1+1
    jsr sgS7
    clc
    adc SG_CY
    sta SG_SY
    rts

// --- sgSegment -- kLine (PX,PY) -> (SX,SY), then prev = cur ---------------
//  Two hazards, each of which draws a stray line:
//
//  kLine cannot clip a NEGATIVE x.  Its endpoints are unsigned 16-bit, so a
//  segment running off the left edge is walked the long way round -- 65,000
//  steps with SEI held -- and whenever the low byte passes below 64 the pixel
//  is plotted at column 256+lo, far right of that row.  A figure that
//  overhangs the left edge must therefore have those segments SKIPPED, not
//  clipped.  x >= 0 has a high byte of 0 and only a negative one has bit 7
//  set, so the test is exact; the point is still made the previous point, so
//  the trace resumes where the curve comes back on screen.
//
//  kArg is per-call scratch, and no interrupt path writes it: the F-key display
//  (the display change is applied from the scheduler's switch path for exactly that reason).  The window below is held anyway, as cheap insurance
//  against a future writer -- kLine holds SEI for its whole loop, so it only
//  extends that window backwards over the two loads.
sgSegment:
    lda SG_SXH
    bmi sgSegPrev              // off the left edge: kLine would misdraw it
    lda SG_PXH
    bmi sgSegPrev
    php
    sei
    lda SG_PX
    sta kArg
    lda SG_PXH
    sta kArg+1
    lda SG_PY
    sta kArg+2
    lda SG_SX
    sta kArg+4
    lda SG_SXH
    sta kArg+5
    lda SG_SY
    sta kArg+6
    lda SG_INK                 // hires: non-zero sets the pixel; multicolour:
    jsr kLine                  // 1..3 picks one of the screen's three colours
    plp
sgSegPrev:
    lda SG_SX
    sta SG_PX
    lda SG_SXH
    sta SG_PXH
    lda SG_SY
    sta SG_PY
    rts

// --- sgS7 -- A = SG_S1 (signed 16-bit) >> 7, floor -----------------------
//  hi*2 + (low byte >= 128) is the floor of the signed value over 128;
//  |SG_S1| <= 95*127, so the result fits one signed byte.
sgS7:
    lda SG_S1+1
    asl
    sta SG_T0
    lda SG_S1
    bpl sgS7Done
    inc SG_T0
sgS7Done:
    lda SG_T0
    rts

// --- sgMul -- SG_PR = A (signed table value) * X (radius 0..127) ---------
//  Shift-and-add over the magnitudes, then the table value's sign.
sgMul:
    sta SG_MV
    stx SG_MR
    lda #0
    sta SG_MR+1
    sta SG_PR
    sta SG_PR+1
    lda SG_MV
    bpl sgMulMag
    eor #$ff
    clc
    adc #1                     // A = |table value|
sgMulMag:
    sta SG_T1
    ldx #8
sgMulLoop:
    lsr SG_T1
    bcc sgMulSkip
    clc
    lda SG_PR
    adc SG_MR
    sta SG_PR
    lda SG_PR+1
    adc SG_MR+1
    sta SG_PR+1
sgMulSkip:
    asl SG_MR
    rol SG_MR+1
    dex
    bne sgMulLoop
    lda SG_MV
    bpl sgMulDone
    sec                        // two's complement the product
    lda #0
    sbc SG_PR
    sta SG_PR
    lda #0
    sbc SG_PR+1
    sta SG_PR+1
sgMulDone:
    rts

// --- sgDerive -- R, r, d -> A, B, the angle steps, N and m ---------------
//  P/Q = (R-r)/r in lowest terms.  N = 65536>>m segments closes the curve
//  exactly: t ends Q turns on and phi P turns on.  m starts at 7 (512
//  segments) and drops while the faster of the two waves would get fewer
//  than 40 samples per cycle, floored at m = 4 (4096 segments).  Then
//  (R-r) and d are halved together -- a pure scale, so the shape is
//  unchanged -- until they fit inside the bitmap.
sgDerive:
    // Euclid on (R-r, r): the loop leaves gcd in SG_T0
    lda SG_R
    sec
    sbc SG_RR
    sta SG_T0                  // a = R-r
    lda SG_RR
    sta SG_T1                  // b = r
sgGcdLoop:
    lda SG_T1
    beq sgGcdDone
    lda SG_T0
    sta kArg
    lda #0
    sta kArg+1
    lda SG_T1
    sta kArg+2
    lda #0
    sta kArg+3
    jsr kDiv16
    lda SG_KR+2                // a mod b
    ldx SG_T1                  // the old b
    sta SG_T1                  // new b
    stx SG_T0                  // new a
    jmp sgGcdLoop
sgGcdDone:
    // P = (R-r)/g, Q = r/g
    lda SG_R
    sec
    sbc SG_RR
    sta kArg
    lda #0
    sta kArg+1
    lda SG_T0
    sta kArg+2
    lda #0
    sta kArg+3
    jsr kDiv16
    lda SG_KR
    sta SG_T2                  // P
    lda SG_RR
    sta kArg
    lda #0
    sta kArg+1
    lda SG_T0
    sta kArg+2
    lda #0
    sta kArg+3
    jsr kDiv16
    lda SG_KR
    sta SG_T3                  // Q
    // big = max(P,Q): the shorter of the two wave periods
    lda SG_T2
    cmp SG_T3
    bcs sgBigP
    lda SG_T3
sgBigP:
    sta SG_BIG
    // m: the first shift that still gives the faster wave 40 samples/cycle
    lda #7
    sta SG_M
sgPickLoop:
    lda #7
    sec
    sbc SG_M
    tax
    lda SG_BIG
    cmp sgBigMax,x
    bcs sgPickNext             // too fussy for this m
    jmp sgPickDone
sgPickNext:
    lda SG_M
    cmp #4
    beq sgPickDone             // m = 4 is the floor
    dec SG_M
    jmp sgPickLoop
sgPickDone:
    // N = 65536>>m -- a multiple of 256, so only the high byte matters
    lda #7
    sec
    sbc SG_M
    tax
    lda sgNHi,x
    sta SG_N+1
    lda #0
    sta SG_N
    // theta step = Q<<m, phi step = P<<m (both exact)
    lda SG_T3
    jsr sgShiftM
    sta SG_DTH
    stx SG_DTH+1
    lda SG_T2
    jsr sgShiftM
    sta SG_DPH
    stx SG_DPH+1
    // A = R-r, B = d, halved together until the drawing fits
    lda SG_R
    sec
    sbc SG_RR
    sta SG_A
    lda SG_D
    sta SG_B
sgScale:
    lda SG_A
    clc
    adc SG_B
    cmp #96
    bcc sgScaleDone
    lsr SG_A
    lsr SG_B
    jmp sgScale
sgScaleDone:
    rts

// --- sgShiftM -- A = value -> A/X = value << SG_M (16-bit) ---------------
sgShiftM:
    sta SG_T0
    lda #0
    sta SG_T1
    ldx SG_M
    beq sgShiftDone
sgShiftLoop:
    asl SG_T0
    rol SG_T1
    dex
    bne sgShiftLoop
sgShiftDone:
    lda SG_T0
    ldx SG_T1
    rts

// --- sgSurface -- allocate the bitmap, clear it, and take the display -----
//  A = mode (0 hires, 1 multicolour).  Hires is one bit per pixel, so the
//  colour is the matrix's high nibble and the whole screen is painted with
//  it.  Multicolour takes its colours from each cell -- the matrix's two
//  nibbles and that cell's colour RAM -- so the matrix is left as
//  kClearBitmap zeroed it and sgMontPalette fills every cell afterwards (a
//  zeroed cell shows the background colour, $D021).
sgSurface:
    sta SG_MODE
    ldx #0
    stx kArg                   // border = black
    stx kArg+1
    stx kArg+2
    stx kArg+3
    jsr kAllocBitmap
    bcc sgSurfOk
    lda #SYS_EXIT
    jsr syscall                // no surface slot -> exit
sgSurfOk:
    jsr kDisableCursor         // graphics task -- no blink cursor
    lda #0
    jsr kClearBitmap           // pixels <- 0 (the matrix is zeroed too)
    lda SG_MODE
    bne sgSurfMc
    lda SG_COL
    asl
    asl
    asl
    asl                        // hires: the pixel colour is the matrix nibble
    jsr kFillMatrix
sgSurfMc:
    lda #SYS_GETTID
    jsr syscall
    tax
    //  TAKE THE DISPLAY WITHOUT TAKING THE OUTGOING CURSOR BLOCK WITH IT.
    //  The blink block is drawn INTO the framebuffer pixels, and kSwitchView
    //  copies the whole framebuffer into the outgoing surface's REU slot
    //  (surfSave) -- so a block that is drawn at this instant is baked into the
    //  shell's saved image, and dgsDone then clears cursorVisible, so nothing
    //  will ever restore that cell. The shell shows a phantom cursor where its
    //  cursor was, and only a pixel write removes it. kBlinkReset erases the
    //  block at the DISPLAYED surface's saved position, which at this moment is
    //  the shell's. The SEI is required: an IRQ tick between the erase and the
    //  switch would draw the block straight back. X must survive the call
    //  (kBlinkReset clobbers A, X, Y, kPtr, kArg+7), and no syscall may run
    //  inside the window -- the dispatcher re-enables IRQs.
    php
    sei
    txa
    pha
    jsr kBlinkReset            // un-draw a live blink block (no-op if OFF)
    pla
    tax
    jsr kSwitchView            // display our surface and take kbFocus
    plp
    rts

// --- sgUsage -- report the argument syntax in the bitmap -----------------
sgUsage:
    lda #<sgUseMsg
    sta kPtr
    lda #>sgUseMsg
    sta kPtr+1
    lda #8
    sta kArg                   // x lo
    lda #0
    sta kArg+1                 // x hi
    lda #96
    sta kArg+2                 // y
    lda #1                     // set pixel
    jsr kGfxString
    lda #<sgUseMsg2
    sta kPtr
    lda #>sgUseMsg2
    sta kPtr+1
    lda #8                     // kGfxString clobbers the whole kArg block,
    sta kArg                   // so the position is set again for this line
    lda #0
    sta kArg+1
    lda #112
    sta kArg+2
    lda #1
    jsr kGfxString
    rts

// --- sgKey -- quit on 'q', else yield once -------------------------------
//  kReadKey is focus-guarded: a background spirog never eats the shell's
//  keystrokes, so the key is only seen while our surface is displayed.
sgKey:
    jsr kReadKey
    cmp #'q'
    beq sgQuit
    lda #SYS_YIELD
    jsr syscall
    rts
sgQuit:
    lda #SYS_EXIT
    jsr syscall

// --- sgPreset -- A = 1..9 loads that preset (R, r, d) --------------------
//  A preset is one row of sgPresets; the colour keeps its default.
sgPreset:
    sec
    sbc #1                     // 0..8
    sta SG_T3
    asl                        // index*2
    clc
    adc SG_T3                  // index*3
    tax
    lda sgPresets,x
    sta SG_R
    lda sgPresets+1,x
    sta SG_RR
    lda sgPresets+2,x
    sta SG_D
    rts

// --- sgMontage -- preset 10: 25 random shapes ----------------------------
//  Each shape comes from the montage's bag of presets, shrunk by the scale
//  bag's halvings, given a uniform random origin, and drawn in ONE of the
//  montage's three colours -- which alternate, so no two figures in a row
//  share one.  sgTrace checks the keyboard, so 'q' abandons it mid-shape.
sgMontage:
    // stir the RNG so repeat runs differ: frameCount advances every frame
    lda frameCount
    and #$1f
    beq sgMontSeed
    tax
sgMontStir:
    jsr kRandom
    dex
    bne sgMontStir
sgMontSeed:
    lda #25
    sta SG_LEFT
sgMontNext:
    jsr sgMontShape            // random preset + scale into SG_R/RR/D/EXTRA
    jsr sgDerive               // A, B, the steps and N, exactly as for one shape
    jsr sgMontFit              // extra halvings, then a random origin
    jsr sgTrace
    // the next figure takes the next palette entry, so the three colours
    // alternate evenly and no two figures in a row share one
    inc SG_INK
    lda SG_INK
    cmp #4
    bcc sgMontInkOk
    lda #1
    sta SG_INK
sgMontInkOk:
    dec SG_LEFT
    bne sgMontNext
    rts

// --- sgMontShape -- the figure's scale and shape --------------------------
//  Both bags are WALKED with a counter, not sampled: a masked random draw at
//  the montage's stride clusters badly over twenty-five figures (fifteen of
//  them came out at the same size), so the cursor is the figure count plus a
//  random phase.  The ratios are then exact, the four sizes are all present,
//  and the phase keeps the order different between runs.
//
//  sgMontScales favours the FEWEST halvings -- 0 is the commonest and draws
//  the figure at the size the single-shape presets use, filling the screen
//  height.  A quarter-size or smaller figure takes the 5-lobe preset whatever
//  the bag says: eleven hoops at that radius are two or three pixels each and
//  draw as a smear rather than as a shape.
sgMontShape:
    lda SG_LEFT
    clc
    adc SG_SPIN
    sta SG_T3                  // this figure's bag cursor
    and #$07
    tax
    lda sgMontScales,x
    sta SG_EXTRA
    lda SG_T3
    and #$0f
    tax
    lda sgMontPicks,x
    ldx SG_EXTRA
    cpx #2
    bcc sgMontGot              // full size or half: the bag's own shape
    lda #1                     // quarter size or less: the 5-lobe preset
sgMontGot:
    jsr sgPreset               // loads SG_R, SG_RR, SG_D
    rts

// --- sgMontFit -- shrink the figure, then give it an origin ------------- -
//  Entry: SG_A/SG_B from sgDerive and SG_EXTRA = further halvings.  Halving
//  both radii together is a pure scale, so the shape is unchanged.
//
//  Both origins are drawn UNIFORMLY, never from a window narrowed by the
//  figure's own extent -- that bunches every shape toward the middle of the
//  screen:
//    x over the whole 160 columns -- a figure near an edge simply overhangs
//      it, because x is a 16-bit coordinate and kLine clips;
//    y over the 200-2*extent rows the figure fits in, because kLine takes y
//      as ONE byte, so a negative row would wrap to the BOTTOM of the screen
//      instead of clipping.
sgMontFit:
    ldx SG_EXTRA
    beq sgMontScaled           // 0 halvings: full size (dex/bne would run 256)
sgMontHalf:
    lsr SG_A
    lsr SG_B
    dex
    bne sgMontHalf
sgMontScaled:
    lda SG_A
    clc
    adc SG_B
    sta SG_T0                  // the extent either side of the centre
sgMontCx:
    jsr sgRnd
    cmp #160
    bcs sgMontCx               // 160..255 -- draw again, so x is uniform
    sta SG_CX
    lda #0
    sta SG_CX+1                // a 16-bit x, so an overhang clips safely
    lda #200
    sec
    sbc SG_T0
    sbc SG_T0                  // 200 - 2*extent (106 at the fewest halvings)
    sta SG_T1
sgMontCy:
    jsr sgRnd
    cmp SG_T1
    bcs sgMontCy
    clc
    adc SG_T0                  // shifted up by the extent: the whole
    sta SG_CY                  // figure lands on the screen
    rts

// --- sgMontInk -- the montage's three colours in SG_C1/C2/C3 -------------
//  Each is drawn at random in 1..15 (0 is the background) and re-drawn if it
//  repeats one already taken, so the three are random AND distinct -- and
//  never a fixed distance apart.
sgMontInk:
sgInk1:
    jsr sgRnd
    and #$0f
    bne sgInk1a
    lda #1
sgInk1a:
    sta SG_C1
sgInk2:
    jsr sgRnd
    and #$0f
    bne sgInk2a
    lda #1
sgInk2a:
    cmp SG_C1
    beq sgInk2
    sta SG_C2
sgInk3:
    jsr sgRnd
    and #$0f
    bne sgInk3a
    lda #1
sgInk3a:
    cmp SG_C1
    beq sgInk3
    cmp SG_C2
    beq sgInk3
    sta SG_C3
    rts

// --- sgMontPalette -- give the whole screen the montage's three colours ---
//  A multicolour pixel takes its colour from its cell: the matrix's high
//  nibble is value 1, its low nibble value 2, and that cell's colour RAM
//  value 3.  Filling every cell ONCE, up front, is what lets each figure be
//  drawn in a single value and still show three colours on the screen, and
//  because two figures sharing a cell read the same palette an overlap no
//  longer disturbs either of them.  We hold the display, so these are the
//  surface's own matrix and colour RAM.
sgMontPalette:
    lda SG_C1
    asl
    asl
    asl
    asl
    ora SG_C2
    jsr kFillMatrix            // every cell: (colour1<<4) | colour2
    lda SG_C3
    jsr kFillColorRam          // every cell: colour3
    rts


// --- sgRnd -- A = kRandom, tempered --------------------------------------
//  kRandom shifts LEFT, so bit i of one value is bit i-1 of the previous one:
//  a masked draw on a steady stride reads a sliding window of the same
//  register and comes out badly skewed.  `& $03` never once returned 3 in
//  1500 picks, and the montage's bags were inverted by it.  XORing three
//  fields of the byte decorrelates them.  Uses SG_T2/SG_T3.
sgRnd:
    jsr kRandom
    sta SG_T3                  // v
    lsr
    lsr
    lsr                        // v>>3
    eor SG_T3                  // v ^ (v>>3)
    sta SG_T2
    lda SG_T3
    lsr
    lsr
    lsr
    lsr
    lsr
    lsr                        // v>>6
    eor SG_T2                  // v ^ (v>>3) ^ (v>>6)
    rts

// --- sgNum -- the next decimal number in (dataPtr),y ---------------------
//  Leading spaces are skipped and a non-digit ENDS the number, leaving Y
//  on it, so the next call resumes there.  C = 1 and A = the value if a
//  digit was found, C = 0 at the end of the args.  A value reaching 25
//  saturates at 255 (every caller clamps), which keeps the accumulate
//  below 250 and therefore free of carries.
sgNum:
    ldx #0                     // value
    lda #0
    sta SG_T3                  // no digits yet
sgNumSp:                       // phase 1: skip the spaces before the number
    lda (dataPtr),y
    beq sgNumDone              // NUL -> end of the args
    cmp #$20
    bne sgNumDig               // first non-space -> the digits start here
    iny
    bne sgNumSp
    jmp sgNumDone
sgNumDig:                      // phase 2: digits; a non-digit ends the number
    lda (dataPtr),y
    beq sgNumDone
    cmp #$30
    bcc sgNumDone              // below '0' -> not a digit
    cmp #$3a
    bcs sgNumDone              // above '9' -> not a digit
    cpx #25
    bcs sgNumSat               // 25*10+9 would pass 255 -> saturate
    sec
    sbc #$30                   // digit
    sta SG_T2
    txa
    asl
    sta SG_T1                  // value*2
    asl
    asl                        // value*8   (value <= 24 -> at most 192)
    clc
    adc SG_T1                  // 10*value  (at most 240)
    adc SG_T2                  // + digit   (at most 249 -- no carry out)
    tax
    lda #1
    sta SG_T3
    iny
    bne sgNumDig               // a space or the NUL ends the number here
    jmp sgNumDone
sgNumSat:
    ldx #255
    lda #1
    sta SG_T3
    iny
    bne sgNumDig
sgNumDone:
    txa
    ldx SG_T3
    beq sgNumNone
    sec
    rts
sgNumNone:
    clc
    rts

// --- data ----------------------------------------------------------------
//  sgSin[i] = round(127*sin(2*pi*i/256)) in two's complement, so the
//  drawing loop reads sin(theta) as a signed byte.  The index is the
//  angle's high byte (the table covers exactly one turn), and
//  sin(i + 64) is cos(i) because 64 entries are a quarter turn.
sgSin:
    .byte $00,$03,$06,$09,$0C,$10,$13,$16,$19,$1C,$1F,$22,$25,$28,$2B,$2E
    .byte $31,$33,$36,$39,$3C,$3F,$41,$44,$47,$49,$4C,$4E,$51,$53,$55,$58
    .byte $5A,$5C,$5E,$60,$62,$64,$66,$68,$6A,$6B,$6D,$6F,$70,$71,$73,$74
    .byte $75,$76,$78,$79,$7A,$7A,$7B,$7C,$7D,$7D,$7E,$7E,$7E,$7F,$7F,$7F
    .byte $7F,$7F,$7F,$7F,$7E,$7E,$7E,$7D,$7D,$7C,$7B,$7A,$7A,$79,$78,$76
    .byte $75,$74,$73,$71,$70,$6F,$6D,$6B,$6A,$68,$66,$64,$62,$60,$5E,$5C
    .byte $5A,$58,$55,$53,$51,$4E,$4C,$49,$47,$44,$41,$3F,$3C,$39,$36,$33
    .byte $31,$2E,$2B,$28,$25,$22,$1F,$1C,$19,$16,$13,$10,$0C,$09,$06,$03
    .byte $00,$FD,$FA,$F7,$F4,$F0,$ED,$EA,$E7,$E4,$E1,$DE,$DB,$D8,$D5,$D2
    .byte $CF,$CD,$CA,$C7,$C4,$C1,$BF,$BC,$B9,$B7,$B4,$B2,$AF,$AD,$AB,$A8
    .byte $A6,$A4,$A2,$A0,$9E,$9C,$9A,$98,$96,$95,$93,$91,$90,$8F,$8D,$8C
    .byte $8B,$8A,$88,$87,$86,$86,$85,$84,$83,$83,$82,$82,$82,$81,$81,$81
    .byte $81,$81,$81,$81,$82,$82,$82,$83,$83,$84,$85,$86,$86,$87,$88,$8A
    .byte $8B,$8C,$8D,$8F,$90,$91,$93,$95,$96,$98,$9A,$9C,$9E,$A0,$A2,$A4
    .byte $A6,$A8,$AB,$AD,$AF,$B2,$B4,$B7,$B9,$BC,$BF,$C1,$C4,$C7,$CA,$CD
    .byte $CF,$D2,$D5,$D8,$DB,$DE,$E1,$E4,$E7,$EA,$ED,$F0,$F4,$F7,$FA,$FD

// The largest max(P,Q) each shift still samples well: 40*big <= 512/1024/
// 2048/4096 for m = 7/6/5/4.
sgBigMax:
    .byte 12, 25, 51, 102

// N = 65536>>m leaves a multiple of 256, so this table is its high byte.
sgNHi:
    .byte $02, $04, $08, $10

// The 1..9 presets, as (R, r, d) rows.  Each closes exactly, and each is
// sized so the drawn curve uses the full screen height: after the (R-r)+d
// scaling in sgDerive, every one of them spans rows 5-194.
sgPresets:
    .byte 120,45,116
    .byte 116,44,119
    .byte 122,46,115
    .byte 118,44,117
    .byte 126,48,17
    .byte 114,42,23
    .byte 120,18,89
    .byte 120,10,80
    .byte 126,46,15

// --- preset 10's shape bag and scale bag ---------------------------------
//  Both are walked with a cursor rather than sampled; see sgMontShape.
//  sgMontPicks holds the four presets with the LOWEST lobe counts -- 1 draws
//  5 lobes, 8 draws 11, 6 draws 12, 5 draws 13 -- weighted so the roundest is
//  the commonest.  sgMontScales is the extra halvings, weighted to the TOP of
//  the size range: 0 takes half the bag, 1 a quarter, and the two smallest
//  sizes share the rest.
sgMontPicks:
    .byte 1,1,1,1,1,1,1,1,8,8,8,8,6,6,5,5
sgMontScales:
    .byte 0,0,0,0,1,1,2,3

sgUseMsg:
    .text "use: spirog R r d col (R>r)"
    .byte 0

sgUseMsg2:
    .text "1-9 presets, 10 random"
    .byte 0
