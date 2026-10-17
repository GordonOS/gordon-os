// ==========================================
//  fpdemo.inc -- fp.lib showcase: sine + cosine, pixel by pixel
// ==========================================
//  theta = 2*pi*x/320 (theta += pi/160 per pixel).
//  y = c - c*trig(theta), c = 99.5 -> both waves hit y=0 and y=199
//  exactly at their peaks/troughs (F2FX truncation keeps 0..199).
//  Draw order per x: sine then cosine. kPlot does its own sei/cli --
//  no SEI/CLI in this task. Per-task ZP state is declared in
//  fpdemo.asm (global symbols FPD_*).
//
//  Layout: fpdemoStart MUST be the first body label -- the header's
//  code-size field is fpdemoEnd - fpdemoStart and the loader spawns
//  the task at fpdemoStart, so helpers live AFTER the main body.

.encoding "screencode_mixed"

fpdemoStart:
    // --- allocate a hires bitmap surface ---
    lda #0                     // mode = std
    ldx #0
    stx kArg                   // border = black
    stx kArg+1
    stx kArg+2
    stx kArg+3
    jsr kAllocBitmap
    bcc fpdOk
    lda #SYS_EXIT
    jsr syscall                // no surface -> exit
fpdOk:
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

    // --- theta = 0, x = 0 ---
    ldx #0
    ldy #0
    lda #FP_AYFC
    jsr fpdGo                    // fac1 = 0
    lda #<FPD_THETA
    sta kPtr
    lda #>FPD_THETA
    sta kPtr+1
    lda #FP_FSTORE
    jsr fpdGo                    // theta = 0
    lda #0
    sta FPD_XLO
    sta FPD_XHI

// --- draw: per x, plot sine then cosine, then theta += pi/160 ---
fpdLoop:
    jsr fpThetaIn
    lda #FP_SIN
    jsr fpWave
    jsr fpPlot                  // sine pixel at x

    jsr fpThetaIn
    lda #FP_COS
    jsr fpWave
    jsr fpPlot                  // cosine pixel at x

    jsr fpThetaIn
    lda #<fpDel
    sta kPtr
    lda #>fpDel
    sta kPtr+1
    lda #FP_FLOAD
    jsr fpdGo                    // fac2 = delta
    lda #FP_ADD
    jsr fpdGo                    // fac1 = theta + delta
    lda #<FPD_THETA
    sta kPtr
    lda #>FPD_THETA
    sta kPtr+1
    lda #FP_FSTORE
    jsr fpdGo

    inc FPD_XLO
    bne fpdYld
    inc FPD_XHI
fpdYld:
    jsr fpdKey                 // yield every pixel; 'q' exits (like sprdemo)

    lda FPD_XHI
    cmp #1
    bcc fpdLoop                // x < 256 -> more
    lda FPD_XLO
    cmp #$40
    bcc fpdLoop                // x < 320 -> more

fpdIdle:                       // done -- idle so the bitmap stays on screen
    jsr fpdKey                 // ...and 'q' exits
    jmp fpdIdle

// --- fpdKey -- yield once, and quit cleanly if 'q' was pressed -------------
//  WARNING: `kReadKey` is FOCUS-GUARDED (scanKeyboard returns $FF for a task that does
//  not hold kbFocus, without touching the ring buffer), so a background fpdemo
//  never eats the shell's keystrokes -- the poll only sees keys while our own
//  surface is displayed.
//  Each wait point is a single `jsr` with no branch to a distant exit label
//  (Rule 4).
//  SYS_EXIT frees the bitmap and hands the display back.
fpdKey:
    jsr kReadKey
    cmp #'q'
    beq fpdQuit
    lda #SYS_YIELD
    jsr syscall
    rts
fpdQuit:
    lda #SYS_EXIT
    jsr syscall

// --- fp constants (4-byte floats, external format) ---
fpCen:
    .byte $87, $47, $00, $00   // 99.5 -- center/amplitude (peaks touch 0 & 199)
fpDel:
    .byte $7B, $20, $D9, $7C   // pi/160 -- angle step per pixel (2*pi/320)

// --- helpers (after the main body -- fpdemoStart must stay first) ---

// --- fp.lib dispatcher ---
fpdGo:
    jmp kFpCall

// --- reload fac1 = theta from the ZP buffer ---
fpThetaIn:
    lda #<FPD_THETA
    sta kPtr
    lda #>FPD_THETA
    sta kPtr+1
    lda #FP_FLOAD
    jsr fpdGo                    // fac2 = theta
    ldx #0
    ldy #0
    lda #FP_AYFC
    jsr fpdGo                    // fac1 = 0
    lda #FP_ADD
    jsr fpdGo                    // fac1 = theta
    rts

// --- one wave value: entry A = FP_SIN / FP_COS, fac1 = theta ---
//     FPD_YTMP = trunc(c - c*trig(theta)) -- always 0..199
fpWave:
    sta FPD_TRIG
    lda #<FPD_THETA
    sta kPtr
    lda #>FPD_THETA
    sta kPtr+1
    lda #FP_FSTORE
    jsr fpdGo                    // save theta (rounded)
    lda FPD_TRIG
    jsr fpdGo                    // fac1 = trig(theta)
    lda #<fpCen
    sta kPtr
    lda #>fpCen
    sta kPtr+1
    lda #FP_FLOAD
    jsr fpdGo                    // fac2 = c
    lda #FP_MUL
    jsr fpdGo                    // fac1 = c*trig
    lda #<fpCen
    sta kPtr
    lda #>fpCen
    sta kPtr+1
    lda #FP_FLOAD
    jsr fpdGo                    // fac2 = c
    lda #FP_SUB
    jsr fpdGo                    // fac1 = c - c*trig
    lda #FP_F2FX
    jsr fpdGo                    // A=hi, Y=lo
    tya
    sta FPD_YTMP
    rts

// --- plot (XLO, YTMP) in color 1; Carry = col hi ---
fpPlot:
    lda FPD_XHI
    clc
    beq fpPlotGo                // col < 256 -> carry clear
    sec                         // col 256..319 -> carry set
fpPlotGo:
    ldx FPD_XLO
    ldy FPD_YTMP
    lda #1
    jsr kPlot
    rts
