// ==========================================
//  gfxdemo.inc -- "spider web" line weave demo
// ==========================================
//  Four symmetric fans of kLine strokes (top/bottom + left/right):
//  symmetric under a horizontal fold and a vertical fold.
//  x and y are coupled so they reach their limits at the same
//  time: y steps by the arg (default 5), x steps by y*8/5
//  (= 320/200 coupling). Bounces once then idles (yield).
//  Usage: run gfxdemo [step]   step = y interval (1..25).
//
//    top-right    (x,0)      -> (319,y)
//    top-left     (319-x,0)  -> (0,y)
//    bottom-right (x,199)    -> (319,199-y)
//    bottom-left  (319-x,199)-> (0,199-y)

.encoding "screencode_mixed"

// per-task ZP state -- declared via header zp_base/zp_len (gfxdemo.asm).
// The scheduler DMAs ZP[base..base+len) on every context switch, so these
// survive preemption and are per-instance. GFX_XLO..GFX_YM are defined in
// gfxdemo.asm (global symbols).

gfxdemoStart:
    // --- parse the step arg: y interval (default 5, clamp 1..25) ---
    lda #5
    sta GFX_STEP                // default
    lda dataPtr
    ora dataPtr+1
    beq gfxStepDone             // no buffer -> default
    ldy #0
    ldx #0                      // accumulator
    lda (dataPtr),y
    beq gfxStepDone             // empty args -> default
gfxDig:
    lda (dataPtr),y
    beq gfxParsed               // NUL -> done
    cmp #$30
    bcc gfxParsed               // < '0' -> done
    cmp #$3a
    bcs gfxParsed               // > '9' -> done
    sec
    sbc #$30                    // digit 0-9
    sta kArg+4
    txa                         // value
    asl
    sta kArg+5                  // value*2
    asl
    asl                         // value*8
    clc
    adc kArg+5                  // value*10
    adc kArg+4                  // + digit
    tax
    iny
    bne gfxDig
gfxParsed:
    txa
    beq gfxStepDone             // 0 -> default
    cmp #26
    bcc gfxStepSet              // 1..25 ok
    lda #25                     // clamp high
gfxStepSet:
    sta GFX_STEP
gfxStepDone:
    // --- x interval = step * 8 / 5 (kernel mul/div) ---
    lda GFX_STEP
    sta kArg
    lda #0
    sta kArg+1
    lda #8
    sta kArg+2
    lda #0
    sta kArg+3
    jsr kMul16                  // product = step*8 -> kArg+4..kArg+7
    lda kArg+4
    sta kArg
    lda kArg+5
    sta kArg+1
    lda #5
    sta kArg+2
    lda #0
    sta kArg+3
    jsr kDiv16                  // quotient = step*8/5 -> kArg+4..kArg+5
    lda kArg+4
    sta GFX_XSTEP

    // --- x = 0, y = 0, increasing ---
    lda #0
    sta GFX_XLO
    sta GFX_XHI
    sta GFX_Y
    lda #1
    sta GFX_DIR

    // --- allocate a hires bitmap surface ---
    lda #0                     // mode = std
    ldx #0; stx kArg           // border = black
    ldx #0; stx kArg+1; stx kArg+2; stx kArg+3
    jsr kAllocBitmap
    bcc gfxdOk
    lda #SYS_EXIT
    jsr syscall                // no surface -> exit
gfxdOk:
    jsr kDisableCursor         // graphics task -- no blink cursor

    // --- clear pixels + paint the matrix white-on-black ---
    lda #SYS_GETTID; jsr syscall; tax   // A = own slot
    lda taskGfxReuLo,x; sta kArg
    lda taskGfxReuHi,x; sta kArg+1
    lda taskGfxReuBank,x; sta kArg+2
    lda #<$2000; sta kArg+3
    lda #>$2000; sta kArg+4
    lda #0; jsr kReuFillReu    // pixels <- 0  (8000 bytes)

    lda #SYS_GETTID; jsr syscall; tax
    lda taskGfxReuLo,x
    clc; adc #<$2000; sta kArg
    lda taskGfxReuHi,x
    adc #>$2000; sta kArg+1
    lda taskGfxReuBank,x
    adc #0; sta kArg+2
    lda #<$03E8; sta kArg+3
    lda #>$03E8; sta kArg+4
    lda #$10; jsr kReuFillReu  // matrix <- $10 (white on black, 1000 bytes)

    lda #SYS_GETTID; jsr syscall; tax
    jsr kSwitchView            // display our surface + take kbFocus

// --- draw the web: x and y bounce independently ---
gfxdLoop:
    jsr gfxKey                 // yield every frame; 'q' exits (like sprdemo)
    // ym = 199 - y (bottom mirror of y)
    lda #199
    sec
    sbc GFX_Y
    sta GFX_YM

    // 1. top-right: (x,0) -> (319,y)
    lda GFX_XLO; sta kArg
    lda GFX_XHI; sta kArg+1
    lda #0; sta kArg+2
    lda #$3F; sta kArg+4
    lda #$01; sta kArg+5
    lda GFX_Y; sta kArg+6
    lda #1; jsr kLine

    // 2. top-left: (319-x,0) -> (0,y)
    lda #$3F
    sec
    sbc GFX_XLO               // 63 - xlo -> X1lo
    sta kArg
    lda #$01
    sbc GFX_XHI               // 1 - xhi - borrow -> X1hi
    sta kArg+1
    lda #0
    sta kArg+2                 // Y1 = 0
    sta kArg+4                 // X2 = 0
    sta kArg+5
    lda GFX_Y; sta kArg+6
    lda #1; jsr kLine

    // 3. bottom-right: (x,199) -> (319,ym)
    lda GFX_XLO; sta kArg
    lda GFX_XHI; sta kArg+1
    lda #199; sta kArg+2
    lda #$3F; sta kArg+4
    lda #$01; sta kArg+5
    lda GFX_YM; sta kArg+6
    lda #1; jsr kLine

    // 4. bottom-left: (319-x,199) -> (0,ym)
    lda #$3F
    sec
    sbc GFX_XLO               // 63 - xlo -> X1lo
    sta kArg
    lda #$01
    sbc GFX_XHI               // 1 - xhi - borrow -> X1hi
    sta kArg+1
    lda #199; sta kArg+2
    lda #0; sta kArg+4
    sta kArg+5
    lda GFX_YM; sta kArg+6
    lda #1; jsr kLine

    // --- advance both (coupled bounce, once: y += step, x += x_step) ---
    lda GFX_DIR
    bne gfxUp                  // increasing
    // decreasing
    lda GFX_Y
    bne gfxDec
    jmp gfxIdle                // y back at 0 -> drawn; idle so the surface stays on

gfxUp:                         // increasing
    lda GFX_Y
    clc
    adc GFX_STEP
    cmp #200
    bcc gfxInc                 // y + step <= 199 -> increment
    lda #0
    sta GFX_DIR                // would exceed 199 -> decreasing
    jmp gfxDec

gfxInc:                        // y += step, x += x_step
    lda GFX_Y
    clc
    adc GFX_STEP
    sta GFX_Y
    lda GFX_XLO
    clc
    adc GFX_XSTEP
    sta GFX_XLO
    lda GFX_XHI
    adc #0
    sta GFX_XHI
    jmp gfxdLoop

gfxDec:                        // y -= step, x -= x_step
    lda GFX_Y
    sec
    sbc GFX_STEP
    sta GFX_Y
    lda GFX_XLO
    sec
    sbc GFX_XSTEP
    sta GFX_XLO
    lda GFX_XHI
    sbc #0
    sta GFX_XHI
    jmp gfxdLoop

gfxIdle:                       // done drawing -- idle, keep the surface, 'q' exits
    jsr gfxKey
    jmp gfxIdle

// --- gfxKey -- yield once, and quit cleanly if 'q' was pressed -------------
//  WARNING: `kReadKey` is FOCUS-GUARDED (scanKeyboard returns $FF for a task that does
//  not hold kbFocus, without touching the ring buffer), so a background gfxdemo
//  never eats the shell's keystrokes -- the poll only sees keys while our own
//  surface is displayed.
//  Each wait point is a single `jsr` with no branch to a distant exit label
//  (Rule 4).
//  SYS_EXIT frees the bitmap and hands the display back.
gfxKey:
    jsr kReadKey
    cmp #'q'
    beq gfxQuit
    lda #SYS_YIELD
    jsr syscall
    rts
gfxQuit:
    lda #SYS_EXIT
    jsr syscall


