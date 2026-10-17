// ==========================================
//  gfx.asm -- dynamic library "gfx.lib" (lib 2, 13 entries)
// ==========================================
//  Bitmap geometry: plot, line, box, fillBox, circle, ellipse, floodFill,
//  getPixel, clearBitmap, setGfxColor, char, string, fillMatrix -- in the
//  lib, not the kernel.
//  Entry order = call-index order (base 12): plot(12) line(13) box(14)
//  fillBox(15) circle(16) ellipse(17) floodFill(18) getPixel(19)
//  clearBitmap(20) setGfxColor(21) char(22) string(23) fillMatrix(24).
//  The lib calls the resident surfDisplayed through kSurfDisplayed ($E0C0)
//  and reads/writes shared gfx state + LUTs at fixed kernel addresses
//  (declared below -- keep in sync with gfx.inc / main.asm asserts).
//  All labels are prefixed gx* (global KickAssembler namespace).

.encoding "screencode_mixed"

// --- kernel state aliases (source of truth: gfx.inc / kernel-data.inc) ---
//  (taskGfxReu*/screenKind/curTask come from kernel-data.inc, imported
//   globally by assemble-tasks.asm -- do NOT re-declare them here.)
.const gfxPixelMasks    = $ECE9
.const gfxCol           = $ED40
.const gfxColHi         = $ED41
.const gfxRow           = $ED42
.const gfxTemp          = $ED43
.const gfxColor         = $ED44
.const dispIsBitmap     = $ED45
.const kLineX1Lo        = $ED46
.const kLineX1Hi        = $ED47
.const kLineY1          = $ED48
.const kLineX2Lo        = $ED49
.const kLineX2Hi        = $ED4A
.const kLineY2          = $ED4B
.const kLineDx          = $ED4C
.const kLineDxHi        = $ED4D
.const kLineDy          = $ED4E
.const kLineStepX       = $ED4F
.const kLineStepY       = $ED50
.const kLineErr         = $ED51
.const kLineErrHi       = $ED52
.const kLineMajor       = $ED53
.const kLineCount       = $ED54
.const kLineCountHi     = $ED55
.const kBoxColor        = $ED56
.const kBoxX1Lo         = $ED57
.const kBoxX1Hi         = $ED58
.const kBoxY1           = $ED59
.const kBoxY2           = $ED5A
.const kBoxX2Lo         = $ED5B
.const kBoxX2Hi         = $ED5C
.const gfxOp            = $ED5D   // 0 = set/clear on gfxColor, 1 = XOR (see gfx.inc)
.const gfxRowLutLo      = $1977
.const gfxRowLutHi      = $1990
.const gfxCellRowLutLo  = $19A9
.const gfxCellRowLutHi  = $19C2

.const BMPMODE_STD      = 2
.const BMPMODE_MC       = 3
// FONT_REC_LO/MID/HI come from kernel-data.inc (imported globally).
.const FONT_GLYPHS      = $19E2   // 8-byte glyph DMA scratch (kernel)
.const GFX_MATRIX_BASE_LO = $00
.const GFX_MATRIX_BASE_HI = $04

// --- kCircle dedicated scratch (kernel.asm, $FF70-$FF7D) ---
//  The tail of the contiguous gfx.lib scratch block $FC3F-$FF7D:
//  ff* + kEll* ($FF49-$FF6F) + kCircle*, which flows straight after
//  kEllTemp; pinned by
//  exact-address .asserts in main.asm (kCircleCxLo, kCircleTemp).
.const kCircleCxLo    = $FF70
.const kCircleCxHi    = $FF71
.const kCircleCy      = $FF72
.const kCircleR       = $FF73
.const kCircleX       = $FF74
.const kCircleY       = $FF75
.const kCircleErrLo   = $FF76
.const kCircleErrHi   = $FF77
.const kCirclePxLo    = $FF78
.const kCirclePxHi    = $FF79
.const kCirclePy      = $FF7A
.const kCircleTmpLo   = $FF7B
.const kCircleTmpHi   = $FF7C
.const kCircleTemp    = $FF7D

// --- kEllipse dedicated scratch (kernel.asm, $FF49-$FF6F) ---
.const kEllCxLo    = $FF49
.const kEllCxHi    = $FF4A
.const kEllCy      = $FF4B
.const kEllRx      = $FF4C
.const kEllRy      = $FF4D
.const kEllA       = $FF4E
.const kEllB       = $FF50
.const kEllTwoA    = $FF52
.const kEllTwoB    = $FF55
.const kEllX       = $FF58
.const kEllY       = $FF59
.const kEllDx      = $FF5A
.const kEllDy      = $FF5E
.const kEllD1      = $FF62
.const kEllD2      = $FF66
.const kEllSqLo    = $FF6A
.const kEllSqHi    = $FF6B
.const kEllPxLo    = $FF6C
.const kEllPxHi    = $FF6D
.const kEllPy      = $FF6E
.const kEllTemp    = $FF6F

// --- kFloodFill dedicated scratch (main.asm $190D-$1C15) ---
.const ffStkXLo    = $FC3F   // 256 B span-seed X lo stack
.const ffStkXHi    = $FD3F   // 256 B span-seed X hi stack (0/1)
.const ffStkY      = $FE3F   // 256 B span-seed Y stack
.const ffXLo       = $FF3F
.const ffXHi       = $FF40
.const ffY         = $FF41
.const ffLeftLo    = $FF42
.const ffLeftHi    = $FF43
.const ffRightLo   = $FF44
.const ffRightHi   = $FF45
.const ffStkPtr    = $FF46
.const ffSeedBit   = $FF47
.const ffFillMc    = $FF48   // 0=hires, 1=mc (flood-fill mode flag)

gfxBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word gfxEnd - gfxCode       // +5:  code size
    .byte 0, 0, 0                // +7:  data size, ZP base, ZP len
    .word 0                      // +10: reserved (libs consume no libMask)
    .byte gfxNameEnd - gfxName   // +12: name len
gfxName:
    .text "gfx"
gfxNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - gfxBinStart), 0

gfxCode:
    .word gxPlot, gxLine, gxBox, gxFillBox, gxCircle, gxEllipse, gxFloodFill, gxGetPixel, gxClear, gxSetGfxColor, gxChar, gxString, gxFillMatrix

// ==========================================
//  gxPlot -- kPlot: set/clear a pixel (std mode, LUT-accelerated)
// ==========================================
//  Input: X = col lo, Y = row, A = color (0=clear), Carry = col hi.
gxPlot:
    sei
    stx gfxCol
    sty gfxRow
    tax
    php
    pla
    and #$01
    sta gfxColHi
    txa
    sta gfxColor

    ldx curTask
    lda screenKind,x
    cmp #BMPMODE_STD
    beq gxPiGo
    cmp #BMPMODE_MC
    bne gxPiBadSkip
    jmp gxPiMc                 // MC
gxPiBadSkip:
    jmp gxPiBad

gxPiGo:
    lda gfxRow
    lsr; lsr; lsr
    tay
    lda gfxRowLutLo,y
    sta kPtr
    lda gfxRowLutHi,y
    sta kPtr+1

    ldx gfxCol
    lda gfxColHi
    beq gxPiColLo
    txa
    and #$F8
    clc
    adc kPtr
    sta kPtr
    lda #1
    adc kPtr+1
    sta kPtr+1
    jmp gxPiAddRow
gxPiColLo:
    txa
    and #$F8
    clc
    adc kPtr
    sta kPtr
    lda #0
    adc kPtr+1
    sta kPtr+1

gxPiAddRow:
    lda gfxRow
    and #7
    clc
    adc kPtr
    sta kPtr
    bcc gxPiPtrOk
    inc kPtr+1
gxPiPtrOk:

    lda dispIsBitmap
    beq gxPiPixelReu
    ldx curTask
    lda screenKind,x
    cmp #BMPMODE_STD
    bcc gxPiPixelReu
    jsr kSurfDisplayed
    bcc gxPiPixelReu

    // displayed pixel (live C64)
    lda gfxCol
    and #7
    tax
    lda gfxPixelMasks,x
    ldy #0
    ldx gfxColor
    beq gxPiC64Clear
    ora (kPtr),y
    sta (kPtr),y
    jmp gxPiMatrix
gxPiC64Clear:
    eor #$ff
    and (kPtr),y
    sta (kPtr),y
    jmp gxPiMatrix

    // REU pixel (RMW DMA)
gxPiPixelReu:
    ldx curTask
    lda kPtr
    sec
    sbc #<$2000
    clc
    adc taskGfxReuLo,x
    sta kArg
    lda kPtr+1
    sbc #>$2000
    adc taskGfxReuHi,x
    sta kArg+1
    lda taskGfxReuBank,x
    adc #0
    sta kArg+2
    lda #1; sta kArg+3
    lda #0; sta kArg+4
    lda #<gfxTemp; sta kPtr
    lda #>gfxTemp; sta kPtr+1
    lda #0; sta kArg+5
    jsr kReuToC64
    lda gfxCol
    and #7
    tax
    lda gfxPixelMasks,x
    ldx gfxColor
    beq gxPiReuClear
    ora gfxTemp; sta gfxTemp
    jmp gxPiReuWrite
gxPiReuClear:
    eor #$ff; and gfxTemp; sta gfxTemp
gxPiReuWrite:
    lda #<gfxTemp; sta kPtr
    lda #>gfxTemp; sta kPtr+1
    lda #0; sta kArg+5
    jsr kReuFromC64

    // matrix nibble (SET only)
gxPiMatrix:
    lda gfxColor
    bne gxPimGo
    jmp gxPiDone
gxPimGo:
    lda gfxRow
    lsr; lsr; lsr
    tax
    lda gfxCellRowLutLo,x
    sta kArg
    lda gfxCellRowLutHi,x
    sta kArg+1

    lda gfxCol
    lsr; lsr; lsr
    ldx gfxColHi
    beq gxPimNoHi
    clc; adc #32
gxPimNoHi:
    clc
    adc kArg
    sta kArg
    lda #0
    adc kArg+1
    sta kArg+1

    ldx curTask
    lda screenKind,x
    cmp #BMPMODE_STD
    bcc gxPiMatrixReu
    lda dispIsBitmap
    beq gxPiMatrixReu
    jsr kSurfDisplayed
    bcc gxPiMatrixReu

    lda kArg
    clc; adc #GFX_MATRIX_BASE_LO
    sta kPtr
    lda kArg+1
    adc #GFX_MATRIX_BASE_HI
    sta kPtr+1
    ldy #0
    lda (kPtr),y
    and #$0F
    sta gfxTemp
    lda gfxColor
    asl; asl; asl; asl
    ora gfxTemp
    sta (kPtr),y
    cli; rts
gxPiMatrixReu:
    ldx curTask
    lda kArg
    clc; adc #<$2000
    sta kArg
    lda kArg+1
    adc #>$2000
    sta kArg+1
    lda kArg
    clc; adc taskGfxReuLo,x
    sta kArg
    lda kArg+1
    adc taskGfxReuHi,x
    sta kArg+1
    lda taskGfxReuBank,x
    adc #0
    sta kArg+2
    lda #1; sta kArg+3
    lda #0; sta kArg+4
    lda #<gfxTemp; sta kPtr
    lda #>gfxTemp; sta kPtr+1
    lda #0; sta kArg+5
    jsr kReuToC64
    lda gfxTemp
    and #$0F
    sta gfxTemp
    lda gfxColor
    asl; asl; asl; asl
    ora gfxTemp
    sta gfxTemp
    lda #<gfxTemp; sta kPtr
    lda #>gfxTemp; sta kPtr+1
    lda #0; sta kArg+5
    jsr kReuFromC64

gxPiDone:
    cli; rts
gxPiBad:
    cli; rts

// ==========================================
//  gxClear -- kClearBitmap: fill pixels + matrix via REU
// ==========================================
//  Input: A = fill byte ($00=clear, $FF=all set).
gxClear:
    sei
    sta gfxTemp
    ldx curTask
    lda screenKind,x
    cmp #BMPMODE_STD
    bcs gxCbiGo
    cli; rts

gxCbiGo:
    jsr kSurfDisplayed
    bcc gxCbiSlot

    lda #<$2000; sta kPtr
    lda #>$2000; sta kPtr+1
    lda #<$2000; sta kArg
    lda #>$2000; sta kArg+1
    lda gfxTemp
    jsr kReuFillC64
    lda #GFX_MATRIX_BASE_LO; sta kPtr
    lda #GFX_MATRIX_BASE_HI; sta kPtr+1
    lda #<$03E8; sta kArg
    lda #>$03E8; sta kArg+1
    lda #0
    jsr kReuFillC64
    ldx curTask
    lda screenKind,x
    cmp #BMPMODE_MC
    beq gxCbiMcFill
    jmp gxCbiDone
gxCbiMcFill:
    lda #<$d800; sta kPtr
    lda #>$d800; sta kPtr+1
    lda #<$0400; sta kArg
    lda #>$0400; sta kArg+1
    lda #0
    jsr kReuFillC64
    jmp gxCbiDone

gxCbiSlot:
    ldx curTask
    lda taskGfxReuLo,x; sta kArg
    lda taskGfxReuHi,x; sta kArg+1
    lda taskGfxReuBank,x; sta kArg+2
    lda #<$2000; sta kArg+3
    lda #>$2000; sta kArg+4
    lda gfxTemp
    jsr kReuFillReu

    ldx curTask
    lda taskGfxReuLo,x
    clc; adc #<$2000; sta kArg
    lda taskGfxReuHi,x
    adc #>$2000; sta kArg+1
    lda taskGfxReuBank,x
    adc #0; sta kArg+2
    lda #<$03E8; sta kArg+3
    lda #>$03E8; sta kArg+4
    lda #0
    jsr kReuFillReu

    ldx curTask
    lda screenKind,x
    cmp #BMPMODE_MC
    bne gxCbiDone
    lda taskGfxReuLo,x
    clc; adc #<$2400; sta kArg
    lda taskGfxReuHi,x
    adc #>$2400; sta kArg+1
    lda taskGfxReuBank,x
    adc #0; sta kArg+2
    lda #<$0400; sta kArg+3
    lda #>$0400; sta kArg+4
    lda #0
    jsr kReuFillReu

gxCbiDone:
    cli; rts

// ==========================================
//  gxLine -- kLine: Bresenham (all octants, calls gxPix per pixel)
// ==========================================
gxLine:
    sei
    sta gfxColor

    lda kArg;   sta kLineX1Lo
    lda kArg+1; sta kLineX1Hi
    lda kArg+2; sta kLineY1
    lda kArg+4; sta kLineX2Lo
    lda kArg+5; sta kLineX2Hi
    lda kArg+6; sta kLineY2

    lda #1; sta kLineStepX
    lda kLineX2Hi
    cmp kLineX1Hi
    bcc gxLiX1Bigger
    bne gxLiX2Bigger
    lda kLineX2Lo
    cmp kLineX1Lo
    bcc gxLiX1Bigger
    beq gxLiDxZero
gxLiX2Bigger:
    sec
    lda kLineX2Lo; sbc kLineX1Lo; sta kLineDx
    lda kLineX2Hi; sbc kLineX1Hi; sta kLineDxHi
    jmp gxLiDyCalc
gxLiX1Bigger:
    lda #$ff; sta kLineStepX
    sec
    lda kLineX1Lo; sbc kLineX2Lo; sta kLineDx
    lda kLineX1Hi; sbc kLineX2Hi; sta kLineDxHi
    jmp gxLiDyCalc
gxLiDxZero:
    lda #0; sta kLineDx; sta kLineDxHi

gxLiDyCalc:
    lda #1; sta kLineStepY
    lda kLineY2
    cmp kLineY1
    bcc gxLiY1Bigger
    sec; sbc kLineY1; sta kLineDy
    jmp gxLiMajor
gxLiY1Bigger:
    lda #$ff; sta kLineStepY
    lda kLineY1
    sec; sbc kLineY2; sta kLineDy

gxLiMajor:
    lda #0; sta kLineMajor
    lda kLineDxHi
    bne gxLiXMaj
    lda kLineDx
    cmp kLineDy
    bcs gxLiXMaj
    inc kLineMajor

gxLiXMaj:
    lda kLineMajor
    beq gxLiXMajGo
    jmp gxLiYMaj
gxLiXMajGo:
    lda kLineDx; clc; adc #1; sta kLineCount
    lda kLineDxHi; adc #0
    bne gxLiXMajLong

    lda kLineDx; lsr; sta kLineErr
    lda #0; sta kLineErrHi

gxLiXLoop:
    lda kLineX1Hi; cmp #1; ldx kLineX1Lo
    ldy kLineY1; jsr gxPix

    dec kLineCount
    bne gxLiXCont
    jmp gxLiDone
gxLiXCont:
    lda kLineStepX; bmi gxLiXDec
    inc kLineX1Lo; bne gxLiXErr
    inc kLineX1Hi; jmp gxLiXErr
gxLiXDec:
    lda kLineX1Lo; bne gxLiXDecGo
    dec kLineX1Hi
gxLiXDecGo:
    dec kLineX1Lo

gxLiXErr:
    sec
    lda kLineErr; sbc kLineDy; sta kLineErr
    lda kLineErrHi; sbc #0; sta kLineErrHi
    bpl gxLiXLoop
    clc
    lda kLineErr; adc kLineDx; sta kLineErr
    lda kLineErrHi; adc kLineDxHi; sta kLineErrHi
    lda kLineStepY; bmi gxLiYDec
    inc kLineY1; jmp gxLiXLoop
gxLiYDec:
    dec kLineY1; jmp gxLiXLoop

gxLiXMajLong:
    lda kLineDx; clc; adc #1; sta kLineCount
    lda kLineDxHi; adc #0; sta kLineCountHi
    lda kLineDx; lsr; sta kLineErr
    lda #0; sta kLineErrHi

gxLiXLongLoop:
    lda kLineX1Hi; cmp #1; ldx kLineX1Lo
    ldy kLineY1; jsr gxPix

    lda kLineCount; bne gxLiXLongCnt
    lda kLineCountHi; bne gxLiXLongNotDone
    jmp gxLiDone
gxLiXLongNotDone:
    dec kLineCountHi
gxLiXLongCnt:
    dec kLineCount

    lda kLineStepX; bmi gxLiXLongDec
    inc kLineX1Lo; bne gxLiXLongErr
    inc kLineX1Hi; jmp gxLiXLongErr
gxLiXLongDec:
    lda kLineX1Lo; bne gxLiXLongDecGo
    dec kLineX1Hi
gxLiXLongDecGo:
    dec kLineX1Lo

gxLiXLongErr:
    sec
    lda kLineErr; sbc kLineDy; sta kLineErr
    lda kLineErrHi; sbc #0; sta kLineErrHi
    bpl gxLiXLongLoop
    clc
    lda kLineErr; adc kLineDx; sta kLineErr
    lda kLineErrHi; adc kLineDxHi; sta kLineErrHi
    lda kLineStepY; bmi gxLiYLongDec
    inc kLineY1; jmp gxLiXLongLoop
gxLiYLongDec:
    dec kLineY1; jmp gxLiXLongLoop

gxLiDone:
    cli
    rts

gxLiYMaj:
    lda kLineDy; clc; adc #1; sta kLineCount
    lda kLineDy; lsr; sta kLineErr
    lda #0; sta kLineErrHi

gxLiYLoop:
    lda kLineX1Hi; cmp #1; ldx kLineX1Lo
    ldy kLineY1; jsr gxPix

    dec kLineCount; bne gxLiYLoopNotDone
    jmp gxLiDone
gxLiYLoopNotDone:
    lda kLineStepY; bmi gxLiYDec2
    inc kLineY1; jmp gxLiYErr
gxLiYDec2:
    dec kLineY1

gxLiYErr:
    sec; lda kLineErr; sbc kLineDx; sta kLineErr
    lda kLineErrHi; sbc kLineDxHi; sta kLineErrHi
    bpl gxLiYLoop
    clc; lda kLineErr; adc kLineDy; sta kLineErr
    lda kLineErrHi; adc #0; sta kLineErrHi
    lda kLineStepX; bmi gxLiXDec2
    inc kLineX1Lo; bne gxLiYLoop
    inc kLineX1Hi; jmp gxLiYLoop
gxLiXDec2:
    lda kLineX1Lo; bne gxLiXDec2Go
    dec kLineX1Hi
gxLiXDec2Go:
    dec kLineX1Lo; jmp gxLiYLoop

// --- gxPix -- inline pixel write (displayed C64 / REU slot, no matrix) ---
//  Input: X=col lo, Y=row, Carry=col hi. Uses gfxColor. Caller must SEI.
//  WARNING: the two drop tests here are written as a short branch onto a `jmp` because
//  gxPixDrop sits past the 8-bit range from this point -- the pixel-op test below is
//  enough to put a plain `beq` over the limit.
gxPix:
    stx gfxTemp
    sty kArg+7
    php; pla; and #1
    pha

    lda dispIsBitmap
    bne gxPixBmp
    jmp gxPixDrop
gxPixBmp:
    ldx curTask
    lda screenKind,x
    cmp #BMPMODE_STD
    bcc gxPixDrop
    beq gxPixStd
    jmp gxPixMc               // MC (3)

gxPixStd:
    lda kArg+7
    cmp #200; bcs gxPixDrop

    jsr kSurfDisplayed
    bcs gxPixC64
    jmp gxPixReu

gxPixC64:
    ldx gfxTemp
    lda kArg+7
    lsr; lsr; lsr
    tay
    lda gfxRowLutLo,y; sta kPtr
    lda gfxRowLutHi,y; sta kPtr+1
    ldy kArg+7
    pla; beq gxPixColLo
    cpx #64; bcs gxPixRts
    txa
    and #$F8
    clc; adc kPtr; sta kPtr
    lda #1
    adc kPtr+1; sta kPtr+1
    jmp gxPixRow
gxPixColLo:
    txa
    and #$F8
    clc; adc kPtr; sta kPtr
    lda #0
    adc kPtr+1; sta kPtr+1
gxPixRow:
    tya; and #7; clc; adc kPtr; sta kPtr
    bcc gxPixGo; inc kPtr+1
gxPixGo:
    lda gfxTemp
    and #7
    tax
    lda gfxPixelMasks,x
    ldy gfxOp
    bne gxPixXor              // XOR: the colour is not consulted at all
    ldx gfxColor; beq gxPixClr
    ldy #0; ora (kPtr),y; sta (kPtr),y
gxPixRts:
    rts
gxPixDrop:
    pla; rts
gxPixClr:
    eor #$ff; ldy #0; and (kPtr),y; sta (kPtr),y; rts
//  XOR the pixel mask in A into the byte. The mask is the pixel's own bit, so the op
//  toggles exactly the pixel the call named -- which is what makes a rectangle drawn
//  twice come back off the picture. It sits AFTER gxPixDrop on purpose: the drop tests
//  above branch to it, and anything placed in between eats their range.
gxPixXor:
    ldy #0; eor (kPtr),y; sta (kPtr),y; rts

// --- gxPixReu -- background pixel: RMW DMA into curTask's slot ---
gxPixReu:
    pla
    beq gxPixReuLo
    lda gfxTemp
    cmp #64
    bcs gxPixRts
    lda #1
    bne gxPixReuHi
gxPixReuLo:
    lda #0
gxPixReuHi:
    pha
    lda kArg+7
    lsr; lsr; lsr
    tay
    lda gfxRowLutLo,y; sta kPtr
    lda gfxRowLutHi,y; sta kPtr+1
    lda gfxTemp
    and #$F8
    clc; adc kPtr; sta kPtr
    pla
    adc kPtr+1; sta kPtr+1
    lda kArg+7
    and #7
    clc; adc kPtr; sta kPtr
    bcc gxPixReuPtr
    inc kPtr+1
gxPixReuPtr:
    lda gfxTemp
    and #7
    tax
    lda gfxPixelMasks,x
    sta kArg+6
    ldx curTask
    lda kPtr
    sec
    sbc #<$2000
    clc
    adc taskGfxReuLo,x
    sta kArg
    lda kPtr+1
    sbc #>$2000
    adc taskGfxReuHi,x
    sta kArg+1
    lda taskGfxReuBank,x
    adc #0
    sta kArg+2
    lda #1; sta kArg+3
    lda #0; sta kArg+4
    lda #<gfxTemp; sta kPtr
    lda #>gfxTemp; sta kPtr+1
    lda #0; sta kArg+5
    jsr kReuToC64
    lda kArg+6
    ldy gfxOp
    bne gxPixReuXor
    ldx gfxColor
    beq gxPixReuClr
    ora gfxTemp
    sta gfxTemp
    jmp gxPixReuWr
gxPixReuXor:
    eor gfxTemp
    sta gfxTemp
    jmp gxPixReuWr
gxPixReuClr:
    eor #$ff
    and gfxTemp
    sta gfxTemp
gxPixReuWr:
    lda #<gfxTemp; sta kPtr
    lda #>gfxTemp; sta kPtr+1
    lda #0; sta kArg+5
    jsr kReuFromC64
    rts

// ==========================================
//  gxBox -- kBox: rectangle outline (calls gxPix)
// ==========================================
gxBox:
    sei
    sta gfxColor
    lda kArg;   sta kBoxX1Lo
    lda kArg+1; sta kBoxX1Hi
    lda kArg+2; sta kBoxY1
    lda kArg+4; sec; sbc #1; sta kBoxX2Lo
    lda kArg+5; sbc #0; sta kBoxX2Hi
    clc
    lda kBoxX2Lo; adc kBoxX1Lo; sta kBoxX2Lo
    lda kBoxX2Hi; adc kBoxX1Hi; sta kBoxX2Hi
    lda kArg+6; sec; sbc #1; sta kBoxY2
    clc; adc kBoxY1; sta kBoxY2

    lda kBoxX1Lo; sta gfxCol
    lda kBoxX1Hi; sta gfxColHi
gxBoxHTop:
    lda gfxColHi; cmp kBoxX2Hi
    bcc gxBoxHTopPlot; bne gxBoxHTopDone
    lda gfxCol; cmp kBoxX2Lo
    bcc gxBoxHTopPlot; beq gxBoxHTopPlot
    jmp gxBoxHTopDone
gxBoxHTopPlot:
    ldx gfxCol; lda gfxColHi; cmp #1
    ldy kBoxY1; jsr gxPix
    inc gfxCol; bne gxBoxHTop
    inc gfxColHi; jmp gxBoxHTop
gxBoxHTopDone:

    lda kBoxX1Lo; sta gfxCol
    lda kBoxX1Hi; sta gfxColHi
gxBoxHBot:
    lda gfxColHi; cmp kBoxX2Hi
    bcc gxBoxHBotPlot; bne gxBoxHBotDone
    lda gfxCol; cmp kBoxX2Lo
    bcc gxBoxHBotPlot; beq gxBoxHBotPlot
    jmp gxBoxHBotDone
gxBoxHBotPlot:
    ldx gfxCol; lda gfxColHi; cmp #1
    ldy kBoxY2; jsr gxPix
    inc gfxCol; bne gxBoxHBot
    inc gfxColHi; jmp gxBoxHBot
gxBoxHBotDone:

    lda kBoxY1; clc; adc #1; sta gfxRow
gxBoxVLeft:
    lda gfxRow; cmp kBoxY2
    bcs gxBoxVLeftDone
    ldx kBoxX1Lo; lda kBoxX1Hi; cmp #1
    ldy gfxRow; jsr gxPix
    inc gfxRow; jmp gxBoxVLeft
gxBoxVLeftDone:

    lda kBoxY1; clc; adc #1; sta gfxRow
gxBoxVRight:
    lda gfxRow; cmp kBoxY2
    bcs gxBoxVRightDone
    ldx kBoxX2Lo; lda kBoxX2Hi; cmp #1
    ldy gfxRow; jsr gxPix
    inc gfxRow; jmp gxBoxVRight
gxBoxVRightDone:

    cli; rts

// ==========================================
//  gxFillBox -- kFillBox: filled rectangle
// ==========================================
gxFillBox:
    sei
    sta gfxColor
    lda kArg;   sta kBoxX1Lo
    lda kArg+1; sta kBoxX1Hi
    lda kArg+2; sta kBoxY1
    lda kArg+4; sec; sbc #1; sta kBoxX2Lo
    lda kArg+5; sbc #0; sta kBoxX2Hi
    clc
    lda kBoxX2Lo; adc kBoxX1Lo; sta kBoxX2Lo
    lda kBoxX2Hi; adc kBoxX1Hi; sta kBoxX2Hi
    lda kArg+6; sec; sbc #1; sta kBoxY2
    clc; adc kBoxY1; sta kBoxY2

gxFbY:
    lda kBoxX1Lo; sta gfxCol
    lda kBoxX1Hi; sta gfxColHi
gxFbX:
    lda gfxColHi; cmp kBoxX2Hi
    bcc gxFbXPlot; bne gxFbXRowDone
    lda gfxCol; cmp kBoxX2Lo
    bcc gxFbXPlot; beq gxFbXPlot
    jmp gxFbXRowDone
gxFbXPlot:
    ldx gfxCol; lda gfxColHi; cmp #1
    ldy kBoxY1; jsr gxPix
    inc gfxCol; bne gxFbX
    inc gfxColHi; jmp gxFbX
gxFbXRowDone:

    inc kBoxY1
    lda kBoxY1; cmp kBoxY2
    bcc gxFbY
    beq gxFbY
    cli; rts

// ==========================================
//  gxCircle -- kCircle: midpoint circle, 8-way symmetry (hires)
// ==========================================
//  Input: A = color (0=clear, 1-15=set)
//         kArg   = CX lo,  kArg+1 = CX hi
//         kArg+2 = CY (0-199),  kArg+3 = R (radius)
//  16-bit decision term (the reference 8-bit term wraps for R >= 66).
//  Writes the pixel bit only (no matrix nibble), so the caller pre-fills the
//  matrix. Off-screen pixels are clipped in gxCirclePix.
//  Dedicated scratch (kCircle*), SEI'd for the whole routine -> atomic.
//  Clobbers: A, X, Y, kPtr, kArg block.
gxCircle:
    sei
    sta gfxColor
    lda kArg
    sta kCircleCxLo
    lda kArg+1
    sta kCircleCxHi
    lda kArg+2
    sta kCircleCy
    lda kArg+3
    sta kCircleR

    // R = 0 -> single centre pixel
    lda kCircleR
    bne gxCircleInit
    lda kCircleCxLo
    sta kCirclePxLo
    lda kCircleCxHi
    sta kCirclePxHi
    lda kCircleCy
    sta kCirclePy
    jsr gxCirclePix
    jmp gxCircleDone

gxCircleInit:
    // X = 0, Y = R
    lda #0
    sta kCircleX
    lda kCircleR
    sta kCircleY
    // d = 3 - 2R (16-bit signed)
    lda kCircleR
    asl
    sta kCircleTmpLo            // 2R lo
    lda #0
    rol
    sta kCircleTmpHi            // 2R hi (0 or 1)
    lda #3
    sec
    sbc kCircleTmpLo
    sta kCircleErrLo
    lda #0
    sbc kCircleTmpHi
    sta kCircleErrHi

gxCircleLoop:
    lda kCircleY
    cmp kCircleX
    bcs gxCircleCont            // Y >= X -> X <= Y -> plot
    jmp gxCircleDone            // X > Y -> done
gxCircleCont:

    jsr gxCircle8               // plot (X, Y)

    lda kCircleErrHi
    bmi gxCircleE               // d < 0 -> East

    // SE: d += 4(x-y)+10 using OLD x/y, then y--, x++
    lda kCircleX
    sec
    sbc kCircleY                // old x - old y (8-bit, two's complement)
    sta kCircleTmpLo
    lda #0
    sbc #0                      // sign-extend -> $00 / $FF
    sta kCircleTmpHi
    lda kCircleTmpLo
    asl
    rol kCircleTmpHi
    asl
    rol kCircleTmpHi
    sta kCircleTmpLo            // tmp = 4(x-y)
    lda kCircleTmpLo
    clc
    adc #10
    sta kCircleTmpLo
    lda kCircleTmpHi
    adc #0
    sta kCircleTmpHi            // tmp = 4(x-y)+10
    lda kCircleErrLo
    clc
    adc kCircleTmpLo
    sta kCircleErrLo
    lda kCircleErrHi
    adc kCircleTmpHi
    sta kCircleErrHi            // d += tmp
    dec kCircleY
    inc kCircleX
    jmp gxCircleLoop

gxCircleE:
    // E: d += 4x+6 using OLD x, then x++
    lda #0
    sta kCircleTmpHi
    lda kCircleX
    asl
    rol kCircleTmpHi
    asl
    rol kCircleTmpHi
    sta kCircleTmpLo            // tmp = 4x
    lda kCircleTmpLo
    clc
    adc #6
    sta kCircleTmpLo
    lda kCircleTmpHi
    adc #0
    sta kCircleTmpHi            // tmp = 4x+6
    lda kCircleErrLo
    clc
    adc kCircleTmpLo
    sta kCircleErrLo
    lda kCircleErrHi
    adc kCircleTmpHi
    sta kCircleErrHi            // d += tmp
    inc kCircleX
    jmp gxCircleLoop

gxCircleDone:
    cli
    rts

// --- gxCircle8 -- plot the 8 symmetric points of (kCircleX, kCircleY) ---
//  Clobbers: A, X, Y, kPtr (via gxCirclePix)
gxCircle8:
    // (+X,+Y), (+X,-Y)
    lda kCircleCxLo
    clc
    adc kCircleX
    sta kCirclePxLo
    lda kCircleCxHi
    adc #0
    sta kCirclePxHi
    lda kCircleCy
    clc
    adc kCircleY
    bcc gxCircle8Y1             // no overflow -> Py valid
    lda #$FF                    // CY+Y > 255 -> off-screen
gxCircle8Y1:
    sta kCirclePy
    jsr gxCirclePix
    lda kCircleCy
    sec
    sbc kCircleY
    bcs gxCircle8Y2             // no underflow -> Py valid
    lda #$FF                    // CY-Y < 0 -> off-screen
gxCircle8Y2:
    sta kCirclePy
    jsr gxCirclePix

    // (-X,+Y), (-X,-Y)
    lda kCircleCxLo
    sec
    sbc kCircleX
    sta kCirclePxLo
    lda kCircleCxHi
    sbc #0
    sta kCirclePxHi
    lda kCircleCy
    clc
    adc kCircleY
    bcc gxCircle8Y3
    lda #$FF
gxCircle8Y3:
    sta kCirclePy
    jsr gxCirclePix
    lda kCircleCy
    sec
    sbc kCircleY
    bcs gxCircle8Y4
    lda #$FF
gxCircle8Y4:
    sta kCirclePy
    jsr gxCirclePix

    // (+Y,+X), (+Y,-X)
    lda kCircleCxLo
    clc
    adc kCircleY
    sta kCirclePxLo
    lda kCircleCxHi
    adc #0
    sta kCirclePxHi
    lda kCircleCy
    clc
    adc kCircleX
    bcc gxCircle8Y5
    lda #$FF
gxCircle8Y5:
    sta kCirclePy
    jsr gxCirclePix
    lda kCircleCy
    sec
    sbc kCircleX
    bcs gxCircle8Y6
    lda #$FF
gxCircle8Y6:
    sta kCirclePy
    jsr gxCirclePix

    // (-Y,+X), (-Y,-X)
    lda kCircleCxLo
    sec
    sbc kCircleY
    sta kCirclePxLo
    lda kCircleCxHi
    sbc #0
    sta kCirclePxHi
    lda kCircleCy
    clc
    adc kCircleX
    bcc gxCircle8Y7
    lda #$FF
gxCircle8Y7:
    sta kCirclePy
    jsr gxCirclePix
    lda kCircleCy
    sec
    sbc kCircleX
    bcs gxCircle8Y8
    lda #$FF
gxCircle8Y8:
    sta kCirclePy
    jsr gxCirclePix
    rts

// --- gxCirclePix -- plot pixel at (kCirclePxLo/Hi, kCirclePy) ---
//  Displayed surface -> framebuffer; otherwise -> REU slot (RMW DMA).
//  Honours gfxOp: 0 sets/clears on gfxColor, 1 XORs the pixel, whatever gfxColor says.
//  Clips X<0, X>=320, Y>=200 (Y<0 wraps to a large value -> clipped).
//  Clobbers: A, X, Y, kPtr, kArg block.
gxCirclePix:
    lda kCirclePxHi
    beq gxCirclePixLoOk
    cmp #1
    bne gxCirclePixDrop         // hi = $FF (negative) or > 1 -> off-screen
    lda kCirclePxLo
    cmp #64
    bcs gxCirclePixDrop         // X >= 320
    lda #1
    bne gxCirclePixPush         // always
gxCirclePixLoOk:
    lda #0
gxCirclePixPush:
    pha                         // push col-hi flag (0 or 1)
    lda kCirclePy
    cmp #200
    bcs gxCirclePixDropP
    lda dispIsBitmap
    beq gxCirclePixDropP
    ldx curTask
    lda screenKind,x
    cmp #BMPMODE_STD
    bcc gxCirclePixDropP
    beq gxCirclePixHires
    lda kCirclePxLo            // MC: stage coords for the shared writer
    sta gfxTemp
    lda kCirclePy
    sta kArg+7
    jmp gxPixMc
gxCirclePixHires:
    jsr kSurfDisplayed
    bcs gxCirclePixC64
    jmp gxCirclePixReu

gxCirclePixDropP:
    pla
gxCirclePixDrop:
    rts

gxCirclePixC64:
    lda kCirclePy
    lsr; lsr; lsr
    tay
    lda gfxRowLutLo,y
    sta kPtr
    lda gfxRowLutHi,y
    sta kPtr+1
    ldy kCirclePy              // restore row
    pla                        // col-hi flag
    beq gxCirclePixColLo
    lda kCirclePxLo
    and #$F8
    clc
    adc kPtr
    sta kPtr
    lda #1
    adc kPtr+1
    sta kPtr+1
    jmp gxCirclePixRow
gxCirclePixColLo:
    lda kCirclePxLo
    and #$F8
    clc
    adc kPtr
    sta kPtr
    lda #0
    adc kPtr+1
    sta kPtr+1
gxCirclePixRow:
    tya
    and #7
    clc
    adc kPtr
    sta kPtr
    bcc gxCirclePixGo
    inc kPtr+1
gxCirclePixGo:
    lda kCirclePxLo
    and #7
    tax
    lda gfxPixelMasks,x
    ldy gfxOp
    bne gxCirclePixXor
    ldx gfxColor
    beq gxCirclePixClr
    ldy #0
    ora (kPtr),y
    sta (kPtr),y
    rts
//  XOR the pixel mask in A into the byte. The mask is the pixel's own bit, so the op
//  toggles exactly the pixel the call named -- which is what lets a live preview put a
//  shape on the canvas and take it off again with the same call.
gxCirclePixXor:
    ldy #0
    eor (kPtr),y
    sta (kPtr),y
    rts
gxCirclePixClr:
    eor #$ff
    ldy #0
    and (kPtr),y
    sta (kPtr),y
    rts

gxCirclePixReu:
    pla                        // col-hi flag
    beq gxCirclePixReuLo
    lda #1
    bne gxCirclePixReuHi       // always
gxCirclePixReuLo:
    lda #0
gxCirclePixReuHi:
    pha                        // hi byte (0 or 1)
    lda kCirclePy
    lsr; lsr; lsr
    tay
    lda gfxRowLutLo,y
    sta kPtr
    lda gfxRowLutHi,y
    sta kPtr+1
    lda kCirclePxLo
    and #$F8
    clc
    adc kPtr
    sta kPtr
    pla                        // hi byte
    adc kPtr+1
    sta kPtr+1
    lda kCirclePy
    and #7
    clc
    adc kPtr
    sta kPtr
    bcc gxCirclePixReuPtr
    inc kPtr+1
gxCirclePixReuPtr:
    lda kCirclePxLo
    and #7
    tax
    lda gfxPixelMasks,x
    sta kArg+6                 // mask (survives kReuToC64)
    ldx curTask
    lda kPtr
    sec
    sbc #<$2000
    clc
    adc taskGfxReuLo,x
    sta kArg
    lda kPtr+1
    sbc #>$2000
    adc taskGfxReuHi,x
    sta kArg+1
    lda taskGfxReuBank,x
    adc #0
    sta kArg+2
    lda #1
    sta kArg+3
    lda #0
    sta kArg+4
    lda #<kCircleTemp
    sta kPtr
    lda #>kCircleTemp
    sta kPtr+1
    lda #0
    sta kArg+5
    jsr kReuToC64
    lda kArg+6                 // mask
    ldy gfxOp
    bne gxCirclePixReuXor
    ldx gfxColor
    beq gxCirclePixReuClr
    ora kCircleTemp
    sta kCircleTemp
    jmp gxCirclePixReuWr
gxCirclePixReuXor:
    eor kCircleTemp
    sta kCircleTemp
    jmp gxCirclePixReuWr
gxCirclePixReuClr:
    eor #$ff
    and kCircleTemp
    sta kCircleTemp
gxCirclePixReuWr:
    lda #<kCircleTemp
    sta kPtr
    lda #>kCircleTemp
    sta kPtr+1
    lda #0
    sta kArg+5
    jsr kReuFromC64
    rts

// --- gxEllipse 32-bit helpers (clobber A only) ---
gxEllDxAddTwoB:            // dx += 2b (32-bit += 24-bit)
    clc
    lda kEllDx+0; adc kEllTwoB+0; sta kEllDx+0
    lda kEllDx+1; adc kEllTwoB+1; sta kEllDx+1
    lda kEllDx+2; adc kEllTwoB+2; sta kEllDx+2
    lda kEllDx+3; adc #0; sta kEllDx+3
    rts

gxEllDySubTwoA:            // dy -= 2a (32-bit -= 24-bit)
    sec
    lda kEllDy+0; sbc kEllTwoA+0; sta kEllDy+0
    lda kEllDy+1; sbc kEllTwoA+1; sta kEllDy+1
    lda kEllDy+2; sbc kEllTwoA+2; sta kEllDy+2
    lda kEllDy+3; sbc #0; sta kEllDy+3
    rts

gxEllD1AddDx:              // d1 += dx
    clc
    lda kEllD1+0; adc kEllDx+0; sta kEllD1+0
    lda kEllD1+1; adc kEllDx+1; sta kEllD1+1
    lda kEllD1+2; adc kEllDx+2; sta kEllD1+2
    lda kEllD1+3; adc kEllDx+3; sta kEllD1+3
    rts

gxEllD1SubDy:              // d1 -= dy
    sec
    lda kEllD1+0; sbc kEllDy+0; sta kEllD1+0
    lda kEllD1+1; sbc kEllDy+1; sta kEllD1+1
    lda kEllD1+2; sbc kEllDy+2; sta kEllD1+2
    lda kEllD1+3; sbc kEllDy+3; sta kEllD1+3
    rts

gxEllD1AddB:               // d1 += b (16-bit)
    clc
    lda kEllD1+0; adc kEllB+0; sta kEllD1+0
    lda kEllD1+1; adc kEllB+1; sta kEllD1+1
    lda kEllD1+2; adc #0; sta kEllD1+2
    lda kEllD1+3; adc #0; sta kEllD1+3
    rts

gxEllD2AddA:               // d2 += a (16-bit)
    clc
    lda kEllD2+0; adc kEllA+0; sta kEllD2+0
    lda kEllD2+1; adc kEllA+1; sta kEllD2+1
    lda kEllD2+2; adc #0; sta kEllD2+2
    lda kEllD2+3; adc #0; sta kEllD2+3
    rts

gxEllD2SubDy:              // d2 -= dy
    sec
    lda kEllD2+0; sbc kEllDy+0; sta kEllD2+0
    lda kEllD2+1; sbc kEllDy+1; sta kEllD2+1
    lda kEllD2+2; sbc kEllDy+2; sta kEllD2+2
    lda kEllD2+3; sbc kEllDy+3; sta kEllD2+3
    rts

gxEllD2AddDx:              // d2 += dx
    clc
    lda kEllD2+0; adc kEllDx+0; sta kEllD2+0
    lda kEllD2+1; adc kEllDx+1; sta kEllD2+1
    lda kEllD2+2; adc kEllDx+2; sta kEllD2+2
    lda kEllD2+3; adc kEllDx+3; sta kEllD2+3
    rts

// ==========================================
//  gxEllipse -- kEllipse: midpoint ellipse, 4-way symmetry (hires)
// ==========================================
//  Input: A = color; kArg=CX lo, kArg+1=CX hi, kArg+2=CY,
//         kArg+3=RX, kArg+4=RY (all 8-bit).
//  32-bit decision terms -- exact for all radii 0-255. ry=0 draws a
//  horizontal line; rx=0 falls out of the general path (vertical line).
//  Pixel bit only (no matrix nibble) -- pre-fill the matrix.
//  Clobbers: A, X, Y, kPtr, kArg block.
gxEllipse:
    sei
    sta gfxColor
    lda kArg
    sta kEllCxLo
    lda kArg+1
    sta kEllCxHi
    lda kArg+2
    sta kEllCy
    lda kArg+3
    sta kEllRx
    lda kArg+4
    sta kEllRy

    lda kEllRy
    bne gxEllInit               // ry > 0 -> normal ellipse

    // --- ry = 0 -> horizontal line (cx-rx .. cx+rx, cy) ---
    //  Count DOWN from rx: an up-counter would wrap at rx=255 and hang.
    lda kEllRx
    sta kEllX
gxEllHLine:
    lda kEllCxLo
    clc
    adc kEllX
    sta kEllPxLo
    lda kEllCxHi
    adc #0
    sta kEllPxHi
    lda kEllCy
    sta kEllPy
    jsr gxEllipsePix
    lda kEllCxLo
    sec
    sbc kEllX
    sta kEllPxLo
    lda kEllCxHi
    sbc #0
    sta kEllPxHi
    lda kEllCy
    sta kEllPy
    jsr gxEllipsePix
    dec kEllX
    lda kEllX
    cmp #$FF
    bne gxEllHLine              // include x = rx ... until X wraps past 0
    jmp gxEllDone

gxEllInit:
    // a = rx^2
    lda kEllRx
    sta kArg
    lda #0
    sta kArg+1
    sta kArg+3
    lda kEllRx
    sta kArg+2
    jsr kMul16
    lda kArg+4
    sta kEllA
    lda kArg+5
    sta kEllA+1

    // b = ry^2
    lda kEllRy
    sta kArg
    lda #0
    sta kArg+1
    sta kArg+3
    lda kEllRy
    sta kArg+2
    jsr kMul16
    lda kArg+4
    sta kEllB
    lda kArg+5
    sta kEllB+1

    // 2a, 2b (24-bit)
    lda kEllA
    asl
    sta kEllTwoA
    lda kEllA+1
    rol
    sta kEllTwoA+1
    lda #0
    rol
    sta kEllTwoA+2
    lda kEllB
    asl
    sta kEllTwoB
    lda kEllB+1
    rol
    sta kEllTwoB+1
    lda #0
    rol
    sta kEllTwoB+2

    // X = 0, Y = ry
    lda #0
    sta kEllX
    lda kEllRy
    sta kEllY

    // dx = 0
    lda #0
    sta kEllDx
    sta kEllDx+1
    sta kEllDx+2
    sta kEllDx+3

    // dy = 2-a-ry  (a-ry via kMul16, then << 1)
    lda kEllA
    sta kArg
    lda kEllA+1
    sta kArg+1
    lda kEllRy
    sta kArg+2
    lda #0
    sta kArg+3
    jsr kMul16
    clc
    lda kArg+4
    rol
    sta kEllDy
    lda kArg+5
    rol
    sta kEllDy+1
    lda kArg+6
    rol
    sta kEllDy+2
    lda kArg+7
    rol
    sta kEllDy+3

    // d1 = b - a-ry + (a>>2)
    sec
    lda #0
    sbc kArg+4
    sta kEllD1
    lda #0
    sbc kArg+5
    sta kEllD1+1
    lda #0
    sbc kArg+6
    sta kEllD1+2
    lda #0
    sbc kArg+7
    sta kEllD1+3
    // + b
    clc
    lda kEllD1
    adc kEllB
    sta kEllD1
    lda kEllD1+1
    adc kEllB+1
    sta kEllD1+1
    lda kEllD1+2
    adc #0
    sta kEllD1+2
    lda kEllD1+3
    adc #0
    sta kEllD1+3
    // + (a>>2)
    lda kEllA+1
    lsr
    sta kEllSqHi
    lda kEllA
    ror
    sta kEllSqLo
    lda kEllSqHi
    lsr
    sta kEllSqHi
    lda kEllSqLo
    ror
    sta kEllSqLo
    clc
    lda kEllD1
    adc kEllSqLo
    sta kEllD1
    lda kEllD1+1
    adc kEllSqHi
    sta kEllD1+1
    lda kEllD1+2
    adc #0
    sta kEllD1+2
    lda kEllD1+3
    adc #0
    sta kEllD1+3

gxEllRegion1:
    lda kEllX
    cmp kEllRx
    bcs gxEllRegion2Init        // x >= rx -> done

    // if dx >= dy -> region 2
    lda kEllDx+3
    cmp kEllDy+3
    bcc gxEllR1Plot
    bne gxEllRegion2Init
    lda kEllDx+2
    cmp kEllDy+2
    bcc gxEllR1Plot
    bne gxEllRegion2Init
    lda kEllDx+1
    cmp kEllDy+1
    bcc gxEllR1Plot
    bne gxEllRegion2Init
    lda kEllDx+0
    cmp kEllDy+0
    bcc gxEllR1Plot
    jmp gxEllRegion2Init        // dx >= dy -> region 2

gxEllR1Plot:
    jsr gxEllipse4
    inc kEllX
    jsr gxEllDxAddTwoB
    lda kEllD1+3
    bmi gxEllR1E                // d1 < 0 -> East
    // SE
    dec kEllY
    jsr gxEllDySubTwoA
    jsr gxEllD1AddDx
    jsr gxEllD1SubDy
    jsr gxEllD1AddB
    jmp gxEllRegion1
gxEllR1E:
    jsr gxEllD1AddDx
    jsr gxEllD1AddB
    jmp gxEllRegion1

gxEllRegion2Init:
    // d2 = b-(x+1)^2 + a-(y-1)^2 - a-b   (integer approximation)
    // (x+1)^2 -- (x+1) as 16-bit (can be 256)
    lda kEllX
    clc
    adc #1
    sta kArg
    lda #0
    adc #0
    sta kArg+1
    lda kArg
    sta kArg+2
    lda kArg+1
    sta kArg+3
    jsr kMul16
    // t1 = b - (x+1)^2
    lda kArg+6                  // (x+1)^2 high byte (0 or 1)
    bne gxEllSqBig
    lda kArg+4
    sta kArg
    lda kArg+5
    sta kArg+1
    lda kEllB
    sta kArg+2
    lda kEllB+1
    sta kArg+3
    jsr kMul16
    lda kArg+4
    sta kEllD1
    lda kArg+5
    sta kEllD1+1
    lda kArg+6
    sta kEllD1+2
    lda kArg+7
    sta kEllD1+3
    jmp gxEllAbMult
gxEllSqBig:                     // (x+1)^2 = $010000 -> t1 = b << 16
    lda #0
    sta kEllD1
    sta kEllD1+1
    lda kEllB
    sta kEllD1+2
    lda kEllB+1
    sta kEllD1+3
gxEllAbMult:
    // ab = a - b
    lda kEllA
    sta kArg
    lda kEllA+1
    sta kArg+1
    lda kEllB
    sta kArg+2
    lda kEllB+1
    sta kArg+3
    jsr kMul16
    // d2 = t1 - ab
    sec
    lda kEllD1
    sbc kArg+4
    sta kEllD2
    lda kEllD1+1
    sbc kArg+5
    sta kEllD2+1
    lda kEllD1+2
    sbc kArg+6
    sta kEllD2+2
    lda kEllD1+3
    sbc kArg+7
    sta kEllD2+3
    // (y-1)^2 -- y may be 0 here (flat ellipse reached y=0 in region 1),
    // in which case (y-1)^2 = 1. The unsigned sbc would wrap to 255.
    lda kEllY
    bne gxEllYm1
    lda #1
    sta kArg
    lda #0
    sta kArg+1
    sta kArg+3
    jmp gxEllYm1Sq
gxEllYm1:
    sec
    sbc #1
    sta kArg
    lda #0
    sta kArg+1
    sta kArg+3
gxEllYm1Sq:
    lda kArg
    sta kArg+2
    jsr kMul16
    // t2 = a - (y-1)^2
    lda kArg+4
    sta kArg
    lda kArg+5
    sta kArg+1
    lda kEllA
    sta kArg+2
    lda kEllA+1
    sta kArg+3
    jsr kMul16
    // d2 += t2
    clc
    lda kEllD2
    adc kArg+4
    sta kEllD2
    lda kEllD2+1
    adc kArg+5
    sta kEllD2+1
    lda kEllD2+2
    adc kArg+6
    sta kEllD2+2
    lda kEllD2+3
    adc kArg+7
    sta kEllD2+3

gxEllRegion2:
    lda kEllY
    bmi gxEllDone               // y < 0 -> done
    jsr gxEllipse4
    dec kEllY
    jsr gxEllDySubTwoA
    lda kEllD2+3
    bmi gxEllR2Neg              // d2 < 0 -> x++ branch
    // d2 == 0?
    lda kEllD2+0
    ora kEllD2+1
    ora kEllD2+2
    ora kEllD2+3
    beq gxEllR2Neg              // d2 == 0 -> x++ branch
    // d2 > 0: d2 += a - dy
    jsr gxEllD2AddA
    jsr gxEllD2SubDy
    jmp gxEllRegion2
gxEllR2Neg:
    // d2 <= 0: x++, dx += 2b, d2 += dx - dy + a
    inc kEllX
    jsr gxEllDxAddTwoB
    jsr gxEllD2AddDx
    jsr gxEllD2SubDy
    jsr gxEllD2AddA
    jmp gxEllRegion2

gxEllDone:
    cli
    rts

// --- gxEllipse4 -- plot 4 symmetric points of (kEllX, kEllY) ---
gxEllipse4:
    lda kEllCxLo
    clc
    adc kEllX
    sta kEllPxLo
    lda kEllCxHi
    adc #0
    sta kEllPxHi
    lda kEllCy
    clc
    adc kEllY
    bcc gxEll4Y1
    lda #$FF
gxEll4Y1:
    sta kEllPy
    jsr gxEllipsePix
    lda kEllCxLo
    sec
    sbc kEllX
    sta kEllPxLo
    lda kEllCxHi
    sbc #0
    sta kEllPxHi
    lda kEllCy
    clc
    adc kEllY
    bcc gxEll4Y2
    lda #$FF
gxEll4Y2:
    sta kEllPy
    jsr gxEllipsePix
    lda kEllCxLo
    clc
    adc kEllX
    sta kEllPxLo
    lda kEllCxHi
    adc #0
    sta kEllPxHi
    lda kEllCy
    sec
    sbc kEllY
    bcs gxEll4Y3
    lda #$FF
gxEll4Y3:
    sta kEllPy
    jsr gxEllipsePix
    lda kEllCxLo
    sec
    sbc kEllX
    sta kEllPxLo
    lda kEllCxHi
    sbc #0
    sta kEllPxHi
    lda kEllCy
    sec
    sbc kEllY
    bcs gxEll4Y4
    lda #$FF
gxEll4Y4:
    sta kEllPy
    jsr gxEllipsePix
    rts

// --- gxEllipsePix -- plot pixel at (kEllPxLo/Hi, kEllPy) ---
//  Honours gfxOp: 0 sets/clears on gfxColor, 1 XORs the pixel, whatever gfxColor says.
gxEllipsePix:
    lda kEllPxHi
    beq gxEllipsePixLoOk
    cmp #1
    bne gxEllipsePixDrop
    lda kEllPxLo
    cmp #64
    bcs gxEllipsePixDrop
    lda #1
    bne gxEllipsePixPush
gxEllipsePixLoOk:
    lda #0
gxEllipsePixPush:
    pha
    lda kEllPy
    cmp #200
    bcs gxEllipsePixDropP
    lda dispIsBitmap
    beq gxEllipsePixDropP
    ldx curTask
    lda screenKind,x
    cmp #BMPMODE_STD
    bcc gxEllipsePixDropP
    beq gxEllipsePixHires
    lda kEllPxLo              // MC: stage coords for the shared writer
    sta gfxTemp
    lda kEllPy
    sta kArg+7
    jmp gxPixMc
gxEllipsePixHires:
    jsr kSurfDisplayed
    bcs gxEllipsePixC64
    jmp gxEllipsePixReu

gxEllipsePixDropP:
    pla
gxEllipsePixDrop:
    rts

gxEllipsePixC64:
    lda kEllPy
    lsr; lsr; lsr
    tay
    lda gfxRowLutLo,y
    sta kPtr
    lda gfxRowLutHi,y
    sta kPtr+1
    ldy kEllPy
    pla
    beq gxEllipsePixColLo
    lda kEllPxLo
    and #$F8
    clc
    adc kPtr
    sta kPtr
    lda #1
    adc kPtr+1
    sta kPtr+1
    jmp gxEllipsePixRow
gxEllipsePixColLo:
    lda kEllPxLo
    and #$F8
    clc
    adc kPtr
    sta kPtr
    lda #0
    adc kPtr+1
    sta kPtr+1
gxEllipsePixRow:
    tya
    and #7
    clc
    adc kPtr
    sta kPtr
    bcc gxEllipsePixGo
    inc kPtr+1
gxEllipsePixGo:
    lda kEllPxLo
    and #7
    tax
    lda gfxPixelMasks,x
    ldy gfxOp
    bne gxEllipsePixXor
    ldx gfxColor
    beq gxEllipsePixClr
    ldy #0
    ora (kPtr),y
    sta (kPtr),y
    rts
//  XOR the pixel mask in A into the byte -- the same op the circle's writer takes, so
//  the ellipse previews and erases the way a rectangle does.
gxEllipsePixXor:
    ldy #0
    eor (kPtr),y
    sta (kPtr),y
    rts
gxEllipsePixClr:
    eor #$ff
    ldy #0
    and (kPtr),y
    sta (kPtr),y
    rts

gxEllipsePixReu:
    pla
    beq gxEllipsePixReuLo
    lda #1
    bne gxEllipsePixReuHi
gxEllipsePixReuLo:
    lda #0
gxEllipsePixReuHi:
    pha
    lda kEllPy
    lsr; lsr; lsr
    tay
    lda gfxRowLutLo,y
    sta kPtr
    lda gfxRowLutHi,y
    sta kPtr+1
    lda kEllPxLo
    and #$F8
    clc
    adc kPtr
    sta kPtr
    pla
    adc kPtr+1
    sta kPtr+1
    lda kEllPy
    and #7
    clc
    adc kPtr
    sta kPtr
    bcc gxEllipsePixReuPtr
    inc kPtr+1
gxEllipsePixReuPtr:
    lda kEllPxLo
    and #7
    tax
    lda gfxPixelMasks,x
    sta kArg+6
    ldx curTask
    lda kPtr
    sec
    sbc #<$2000
    clc
    adc taskGfxReuLo,x
    sta kArg
    lda kPtr+1
    sbc #>$2000
    adc taskGfxReuHi,x
    sta kArg+1
    lda taskGfxReuBank,x
    adc #0
    sta kArg+2
    lda #1
    sta kArg+3
    lda #0
    sta kArg+4
    lda #<kEllTemp
    sta kPtr
    lda #>kEllTemp
    sta kPtr+1
    lda #0
    sta kArg+5
    jsr kReuToC64
    lda kArg+6
    ldy gfxOp
    bne gxEllipsePixReuXor
    ldx gfxColor
    beq gxEllipsePixReuClr
    ora kEllTemp
    sta kEllTemp
    jmp gxEllipsePixReuWr
gxEllipsePixReuXor:
    eor kEllTemp
    sta kEllTemp
    jmp gxEllipsePixReuWr
gxEllipsePixReuClr:
    eor #$ff
    and kEllTemp
    sta kEllTemp
gxEllipsePixReuWr:
    lda #<kEllTemp
    sta kPtr
    lda #>kEllTemp
    sta kPtr+1
    lda #0
    sta kArg+5
    jsr kReuFromC64
    rts

// ==========================================
//  gxFloodFill -- kFloodFill: scanline flood fill (hires)
// ==========================================
//  Input: A = fill color (0=clear bit, nonzero=set bit)
//         kArg = seed X lo, kArg+1 = seed X hi, kArg+2 = seed Y
//  Fills the contiguous region whose pixels match the seed pixel's bit,
//  replacing them with the fill bit. Boundary = the other bit value
//  (hires: 1 bit/pixel). 256-entry span-seed stack in kernel data
//  (ffStk*). SEI'd throughout -> atomic. Pixel bit only (no matrix
//  nibble), so the caller pre-fills the matrix.
//  Clobbers: A, X, Y, kPtr, kArg block.

// --- gxFFRead -- read pixel at (ffXLo,ffXHi,ffY) -> A = value ---
//  A = 0/1 (hires) or 0-3 (MC). Caller must SEI. Assumes valid bitmap
//  surface + in-bounds coords. Clobbers A, X, Y, kPtr, kArg (REU path).
gxFFRead:
    ldx curTask
    lda screenKind,x
    cmp #BMPMODE_MC
    bne gxFFReadStd
    jmp gxFFReadMc
gxFFReadStd:
    lda ffY
    lsr; lsr; lsr
    tay
    lda gfxRowLutLo,y
    sta kPtr
    lda gfxRowLutHi,y
    sta kPtr+1
    ldx ffXLo
    txa
    and #$F8                    // (col & $F8) low byte
    clc
    adc kPtr
    sta kPtr
    lda ffXHi                   // bit 8 of (col & $F8): 0 or 1
    adc kPtr+1
    sta kPtr+1
    lda ffY
    and #7
    clc
    adc kPtr
    sta kPtr
    bcc gxFFReadGo
    inc kPtr+1
gxFFReadGo:
    ldx curTask
    jsr kSurfDisplayed          // C=1 displayed (clobbers A,X,Y)
    bcc gxFFReadReu
gxFFReadC64:
    lda ffXLo
    and #7
    tax
    lda gfxPixelMasks,x
    ldy #0
    and (kPtr),y
    jmp gxFFReadNorm
gxFFReadReu:
    lda ffXLo
    and #7
    tax
    lda gfxPixelMasks,x
    sta kArg+6
    ldx curTask
    lda kPtr
    sec
    sbc #<$2000
    clc
    adc taskGfxReuLo,x
    sta kArg
    lda kPtr+1
    sbc #>$2000
    adc taskGfxReuHi,x
    sta kArg+1
    lda taskGfxReuBank,x
    adc #0
    sta kArg+2
    lda #1; sta kArg+3
    lda #0; sta kArg+4
    lda #<gfxTemp; sta kPtr
    lda #>gfxTemp; sta kPtr+1
    lda #0; sta kArg+5
    jsr kReuToC64
    lda kArg+6
    and gfxTemp
gxFFReadNorm:
    beq gxFFReadNormRts
    lda #1
gxFFReadNormRts:
    rts

// --- gxFFMatch -- Z=1 iff pixel at (ffX,ffY) matches ffSeedBit ---
//  (i.e. the pixel should be filled). Clobbers A, X, Y, kPtr, kArg.
gxFFMatch:
    jsr gxFFRead
    cmp ffSeedBit
    rts

// --- gxFFScanRow -- scan row ffY over [ffLeft..ffRight]; push span seeds ---
//  Pushes the leftmost matching pixel of each open span onto ffStk*.
//  Clobbers A, X, Y, kPtr, kArg.
gxFFScanRow:
    lda ffLeftLo; sta ffXLo
    lda ffLeftHi; sta ffXHi
gxFFScanRowLoop:
    lda ffXHi
    cmp ffRightHi
    bcc gxFFScanRowGo           // X < right
    bne gxFFScanRowRts          // X > right
    lda ffXLo
    cmp ffRightLo
    bcc gxFFScanRowGo
    beq gxFFScanRowGo           // X == right -> include
    rts                         // X > right
gxFFScanRowGo:
    jsr gxFFMatch
    beq gxFFSpanFound           // open -> push + skip span
    inc ffXLo                   // solid -> advance
    bne gxFFScanRowLoop
    inc ffXHi
    bne gxFFScanRowLoop
gxFFSpanFound:
    ldx ffStkPtr                // push (ffXLo, ffXHi, ffY)
    cpx #255
    bcs gxFFScanRowRts          // stack full -> drop
    lda ffXLo
    sta ffStkXLo,x
    lda ffXHi
    sta ffStkXHi,x
    lda ffY
    sta ffStkY,x
    inc ffStkPtr
gxFFSpanSkip:                   // skip the rest of this span
    inc ffXLo
    bne gxFFSpanSkipCmp
    inc ffXHi
gxFFSpanSkipCmp:
    lda ffXHi
    cmp ffRightHi
    bcc gxFFSpanSkipGo
    bne gxFFScanRowRts
    lda ffXLo
    cmp ffRightLo
    bcc gxFFSpanSkipGo
    bne gxFFScanRowRts
gxFFSpanSkipGo:
    jsr gxFFMatch
    beq gxFFSpanSkip            // still open -> keep skipping
    inc ffXLo                   // solid -> advance past, continue scan
    bne gxFFSpanSkipAdv
    inc ffXHi
gxFFSpanSkipAdv:
    jmp gxFFScanRowLoop         // (too far for a branch)
gxFFScanRowRts:
    rts

gxFloodFill:
    sei
    sta gfxColor

    lda dispIsBitmap
    beq gxFFQuit
    ldx curTask
    lda screenKind,x
    cmp #BMPMODE_STD
    bcc gxFFQuit
    beq gxFFHires
    lda #1
    sta ffFillMc                // MC
    jmp gxFFSeedY
gxFFHires:
    lda #0
    sta ffFillMc
    jmp gxFFSeedY
gxFFQuit:                      // local exit -- gxFFExit is too far to branch
    jmp gxFFExit
gxFFSeedY:
    lda kArg+2                  // seed Y
    cmp #200
    bcs gxFFQuit
    lda ffFillMc
    bne gxFFSeedMc
    // hires: X < 320
    lda kArg+1                  // seed X hi
    cmp #1
    bcc gxFFSeedOk              // hi=0 -> any lo valid
    bne gxFFQuit                // hi>1 -> off-screen
    lda kArg
    cmp #64
    bcs gxFFQuit                // hi=1 & lo>=64 -> off-screen
    jmp gxFFSeedOk
gxFFSeedMc:
    // MC: X < 160
    lda kArg+1
    bne gxFFQuit                // hi != 0 -> off-screen
    lda kArg
    cmp #160
    bcs gxFFQuit                // lo >= 160 -> off-screen
gxFFSeedOk:
    lda kArg
    sta ffXLo
    lda kArg+1
    sta ffXHi
    lda kArg+2
    sta ffY
    jsr gxFFRead                // seed value -> A (0/1 hires, 0-3 MC)
    sta ffSeedBit
    lda ffFillMc
    bne gxFFFillMc
    lda gfxColor
    beq gxFFFillHiresClr
    lda #1
    jmp gxFFFillCmp
gxFFFillHiresClr:
    lda #0
    jmp gxFFFillCmp
gxFFFillMc:
    lda gfxColor
    and #3
gxFFFillCmp:
    cmp ffSeedBit
    beq gxFFQuit                // fill == seed -> nothing to do

    // push seed (entry 0)
    lda #0
    sta ffStkPtr
    lda ffXLo
    sta ffStkXLo
    lda ffXHi
    sta ffStkXHi
    lda ffY
    sta ffStkY
    inc ffStkPtr
    jmp gxFFMainLoop

gxFFExit:
    cli
    rts

gxFFMainLoop:
    lda ffStkPtr
    beq gxFFExit                // stack empty -> done
    dec ffStkPtr
    ldx ffStkPtr
    lda ffStkXLo,x
    sta ffXLo
    lda ffStkXHi,x
    sta ffXHi
    lda ffStkY,x
    sta ffY

    jsr gxFFMatch               // drop if already filled / boundary
    bne gxFFMainLoop

    // --- scan left ---
    lda ffXLo
    sta ffLeftLo
    lda ffXHi
    sta ffLeftHi
gxFFScanLeft:
    lda ffLeftLo
    ora ffLeftHi
    beq gxFFLeftDone            // left==0 -> edge
    lda ffLeftLo
    bne gxFFLeftDec
    dec ffLeftHi
gxFFLeftDec:
    dec ffLeftLo
    lda ffLeftLo
    sta ffXLo
    lda ffLeftHi
    sta ffXHi
    jsr gxFFMatch
    beq gxFFScanLeft            // match -> continue
    inc ffLeftLo                // boundary -> step back
    bne gxFFLeftDone
    inc ffLeftHi
gxFFLeftDone:

    // --- scan right ---
    lda ffXLo
    sta ffRightLo
    lda ffXHi
    sta ffRightHi
gxFFScanRight:
    lda ffFillMc
    beq gxFFScanRightHires
    // MC: right == 159?
    lda ffRightHi
    bne gxFFRightInc
    lda ffRightLo
    cmp #159
    beq gxFFRightDone           // right==159 -> edge
    bne gxFFRightInc
gxFFScanRightHires:
    lda ffRightHi
    cmp #1
    bne gxFFRightInc            // hi!=1 -> not at 319
    lda ffRightLo
    cmp #63
    beq gxFFRightDone           // right==319 -> edge
gxFFRightInc:
    inc ffRightLo
    bne gxFFRightCheck
    inc ffRightHi
gxFFRightCheck:
    lda ffRightLo
    sta ffXLo
    lda ffRightHi
    sta ffXHi
    jsr gxFFMatch
    beq gxFFScanRight           // match -> continue
    lda ffRightLo               // boundary -> step back
    bne gxFFRightDec
    dec ffRightHi
gxFFRightDec:
    dec ffRightLo
gxFFRightDone:

    // --- fill segment [left..right] ---
    lda ffLeftLo
    sta ffXLo
    lda ffLeftHi
    sta ffXHi
    jsr gxFFCellGo              // the span's LEFT cell: its first pixel is the edge,
                                 //   which is not a cell boundary, so the gate below
                                 //   would never reach it
 gxFFFillLoop:
    ldx ffXLo
    ldy ffY
    lda ffXHi
    cmp #1                      // carry = col hi for gxPix
    jsr gxPix
    jsr gxFFCell                // ...and claim the cell this pixel starts
    lda ffXLo
    cmp ffRightLo
    bne gxFFFillNext
    lda ffXHi
    cmp ffRightHi
    beq gxFFNeighbors           // reached right -> done
gxFFFillNext:
    inc ffXLo
    bne gxFFFillLoop
    inc ffXHi
    bne gxFFFillLoop            // (always: hi 0->1)

    // --- adjacent rows ---
gxFFNeighbors:
    lda ffY
    beq gxFFBelow               // Y==0 -> no row above
    dec ffY
    jsr gxFFScanRow
    inc ffY
gxFFBelow:
    lda ffY
    cmp #199
    bcs gxFFLoopBack            // Y>=199 -> no row below
    inc ffY
    jsr gxFFScanRow
    dec ffY
gxFFLoopBack:
    jmp gxFFMainLoop

// --- gxFFCell -- claim the cell a filled pixel sits in, once per cell ---
//  kPlot writes a cell's colour and gxPix does not, so without this a fill leaves the
//  region showing the colour its cells already carried -- the shape tools have their own
//  walks for the same reason. The fill walks its span one pixel at a time, so the write
//  is gated on the pixel being the FIRST of a cell: one write per cell, eight pixels
//  apart, and the span's own left cell is claimed by gxFFCellGo before the loop, because
//  the edge the scan stopped at is not a cell boundary.
//  The write itself is gxPiMatrix -- the block gxPlot uses -- so the cell address, the
//  displayed/REU choice and the nibble packing are not repeated here. It ends with a
//  `cli`, so the fill's SEI is put back straight after: the write is already done.
//  WARNING: MC writes a 2-bit PIXEL value and its nibbles are palette entries, so the
//  cells are left alone there -- gxPiMatrix is a hires path and gxPlot only reaches it
//  from STD.
gxFFCell:
    lda ffXLo
    and #$07
    bne gxFFCellRts             // not the first pixel of the cell
gxFFCellGo:
    lda ffFillMc
    bne gxFFCellRts             // MC: the value IS the pixel, never a colour
    lda ffY
    sta gfxRow
    lda ffXLo
    sta gfxCol
    lda ffXHi
    sta gfxColHi
    jsr gxPiMatrix
    sei                         // gxPlot's matrix block ends with a cli
gxFFCellRts:
    rts

// ==========================================
//  gxGetPixel -- kGetPixel: read a pixel (hires)
// ==========================================
//  Input: X=col lo, Y=row (0-199), Carry=col hi.
//  Output: A=0 (clear) or nonzero (set); Carry=1 if no surface or
//  off-screen (A=0 then). SEI'd throughout. Uses ffXLo/Hi/Y transiently
//  as the coordinate staging (safe -- never nests with the flood fill).
//  Clobbers: A, X, Y, kPtr, kArg block (REU path).
gxGetPixel:
    sei
    stx ffXLo                   // col lo
    sty ffY                     // row
    lda #0
    rol                         // A = carry (col hi: 0 or 1)
    sta ffXHi

    lda dispIsBitmap
    beq gxGpBad
    ldx curTask
    lda screenKind,x
    cmp #BMPMODE_STD
    bcc gxGpBad
    beq gxGpStd
    jmp gxGpMc                 // MC (3)
gxGpStd:
    lda ffY
    cmp #200
    bcs gxGpBad
    lda ffXHi
    beq gxGpAddr                // col < 256 -> valid
    lda ffXLo
    cmp #64
    bcs gxGpBad                 // col >= 320 -> off-screen
    jmp gxGpAddr                // col 256..319 -> valid
gxGpBad:
    lda #0
    sec                         // C=1 invalid
    cli
    rts
gxGpAddr:
    lda ffY
    lsr; lsr; lsr
    tay
    lda gfxRowLutLo,y
    sta kPtr
    lda gfxRowLutHi,y
    sta kPtr+1
    lda ffXLo
    and #$F8
    clc
    adc kPtr
    sta kPtr
    lda ffXHi
    adc kPtr+1
    sta kPtr+1
    lda ffY
    and #7
    clc
    adc kPtr
    sta kPtr
    bcc gxGpRead
    inc kPtr+1
gxGpRead:
    ldx curTask
    jsr kSurfDisplayed          // C=1 displayed
    bcc gxGpReu
gxGpC64:
    lda ffXLo
    and #7
    tax
    lda gfxPixelMasks,x
    ldy #0
    and (kPtr),y
    clc                         // C=0 valid
    cli
    rts
gxGpReu:
    lda ffXLo
    and #7
    tax
    lda gfxPixelMasks,x
    sta kArg+6
    ldx curTask
    lda kPtr
    sec
    sbc #<$2000
    clc
    adc taskGfxReuLo,x
    sta kArg
    lda kPtr+1
    sbc #>$2000
    adc taskGfxReuHi,x
    sta kArg+1
    lda taskGfxReuBank,x
    adc #0
    sta kArg+2
    lda #1; sta kArg+3
    lda #0; sta kArg+4
    lda #<gfxTemp; sta kPtr
    lda #>gfxTemp; sta kPtr+1
    lda #0; sta kArg+5
    jsr kReuToC64
    lda kArg+6
    and gfxTemp
    clc                         // C=0 valid
    cli
    rts

// --- gxSetGfxColor -- kSetGfxColor: per-task VIC colour set, or the pixel op ---
//  Input: X = selector, A = value.
//         X=0..3 -> a per-task VIC colour (0=border/$D020, 1=bg0/$D021,
//         2=bg1/$D022, 3=bg2/$D023), A = colour (0-15). X=2/3 require
//         screenKind[curTask] == BMPMODE_MC (else no-op).
//         X=4 -> the pixel write op, which is not a VIC register: A=0 sets or
//         clears on the colour, A non-zero XORs the pixel.
//         X >= 5 -> no-op: there is no such VIC register and no per-task slot
//         for it -- the per-task offset is selector*24, so it would leave the
//         colour block.
//  Writes taskGfx*[curTask]; if curTask == kbFocus, the hardware
//  register is updated immediately.
gxSetGfxColor:
    sei
    sta gfxTemp                 // colour
    stx gfxCol                  // selector
    ldx curTask
    lda gfxCol
    cmp #4
    bne gxGcNotOp
    //  --- selector 4 is not a VIC register: it is the pixel write op ---
    //  The three registers below take a per-task colour, which is why the
    //  colour selectors are 0-3 and 4 is spare. The op wants a call of its own
    //  rather than a naked kernel byte, and A carries the value in every other
    //  selector, so it rides A too: 0 = set/clear (the default), non-zero = XOR.
    lda gfxTemp
    sta gfxOp
    jmp gxGcDone
gxGcNotOp:
    bcs gxGcDone                // selector > 4 -> no-op
    cmp #2
    bcc gxGcSet                 // border/bg0 -> always allowed
    lda screenKind,x
    cmp #BMPMODE_MC
    bne gxGcDone                // bg1/bg2 in non-MC -> no-op
gxGcSet:
    // per-task byte = taskGfxBorder + selector*24 + curTask (offset < 256)
    lda gfxCol
    asl; asl; asl               // selector*8
    sta gfxRow                  // stash *8
    asl                         // *16
    clc; adc gfxRow             // *24
    sta gfxRow                  // stash *24
    txa                         // A = curTask
    clc; adc gfxRow             // + *24 -> offset
    clc; adc #<taskGfxBorder    // max $65+$5F = $C4, no carry
    sta kPtr
    lda #>taskGfxBorder
    sta kPtr+1
    ldy #0
    lda gfxTemp
    sta (kPtr),y                // store per-task colour

    lda kbFocus                 // immediate hardware update only when the
    cmp curTask                 // CALLING task is the focused one
    bne gxGcDone
    ldx gfxCol                  // X = selector
    lda gfxTemp
    sta $D020,x                 // immediate hardware write
gxGcDone:
    cli
    rts

// ==========================================
//  Multicolor (MC) pixel core -- 2 bits/pixel, 160x200
// ==========================================
//  gfxNibMasks[n]: 2-bit mask at nibble n (col&3): $C0,$30,$0C,$03.
//  gfxNibShift[n]: right-shift to normalise that nibble: 6,4,2,0.
//  MC byte address = $2000 + (row>>3)*320 + ((col&$FC)<<1) + (row&7)
//  (same cell-interleaved layout as hires; 40 bytes/row in both modes).

gfxNibMasks:  .byte $C0, $30, $0C, $03
gfxNibShift:  .byte 6, 4, 2, 0

// --- gxMcAddr -- MC pixel byte address -> kPtr ---
//  Input: X = col (0-159), Y = row (0-199). Caller SEI'd.
//  Output: kPtr = MC byte address. Clobbers A, X, Y, kArg+5/+6.
gxMcAddr:
    tya
    pha                       // row on stack
    lsr; lsr; lsr
    tay                       // Y = row>>3
    lda gfxRowLutLo,y; sta kPtr
    lda gfxRowLutHi,y; sta kPtr+1
    txa
    and #$FC
    asl                       // (col&$FC)<<1 lo, C = bit 8
    sta kArg+6
    lda #0
    rol                       // bit 8
    sta kArg+5
    clc
    lda kPtr
    adc kArg+6
    sta kPtr
    lda kPtr+1
    adc kArg+5
    sta kPtr+1
    pla                       // row
    and #7
    clc
    adc kPtr
    sta kPtr
    bcc gxMcAddrRts
    inc kPtr+1
gxMcAddrRts:
    rts

// --- gxNibShiftA -- A >>= gfxNibShift[X] (nibble -> 0-3) ---
//  Input: A = byte & mask, X = n (0-3). Output: A = 0-3. Clobbers Y.
gxNibShiftA:
    ldy gfxNibShift,x
    cpy #0
    beq gxNibShiftARts
gxNibShiftALoop:
    lsr
    dey
    bne gxNibShiftALoop
gxNibShiftARts:
    rts

// --- gxPixMc -- MC pixel write (framebuffer / REU slot) ---
//  Input: gfxTemp = col lo, kArg+7 = row, gfxColor = 2-bit value,
//  col-hi pushed on stack (must be 0). Caller SEI'd.
//  Honours gfxOp: 0 writes the 2-bit value, non-zero XORs it into the nibble the pixel
//  owns. XOR on a value restores that value exactly on the second pass, which is what a
//  preview, a caret and a delete need; which COLOUR the toggled pixel shows is another
//  matter -- see the op's own note in gfx.inc.
gxPixMc:
    lda kArg+7
    cmp #200
    bcs gxPixMcDrop           // row >= 200
    pla                       // col-hi
    bne gxPixMcRts            // col-hi=1 -> off-screen
    lda gfxTemp
    cmp #160
    bcs gxPixMcRts            // col >= 160
    jsr kSurfDisplayed
    bcs gxPixMcC64
    jmp gxPixMcReu
gxPixMcDrop:
    pla
    rts
gxPixMcRts:
    rts

gxPixMcC64:
    ldx gfxTemp
    ldy kArg+7
    jsr gxMcAddr              // kPtr = MC addr (clobbers A,X,Y)
    lda gfxTemp
    and #3
    tax                       // X = n
    lda gfxNibMasks,x
    sta kArg+6                // mask
    lda gfxNibShift,x
    tax                       // X = S
    lda gfxColor
    and #3
    cpx #0
    beq gxPixMcC64Val
gxPixMcC64Shl:
    asl
    dex
    bne gxPixMcC64Shl
gxPixMcC64Val:
    sta kBoxColor              // shifted value (gfxRow is gxBox's row counter!)
    ldy gfxOp
    bne gxPixMcC64Xor
    lda kArg+6
    eor #$ff                  // ~mask
    ldy #0
    and (kPtr),y              // byte & ~mask
    ora kBoxColor
    sta (kPtr),y
    rts
gxPixMcC64Xor:
    ldy #0
    lda kBoxColor
    eor (kPtr),y              // the op: the pixel's own two bits, toggled
    sta (kPtr),y
    rts

gxPixMcReu:
    ldx gfxTemp
    ldy kArg+7
    jsr gxMcAddr
    ldx curTask
    lda kPtr
    sec; sbc #<$2000
    clc; adc taskGfxReuLo,x
    sta kArg
    lda kPtr+1
    sbc #>$2000
    adc taskGfxReuHi,x
    sta kArg+1
    lda taskGfxReuBank,x
    adc #0
    sta kArg+2
    lda gfxTemp
    and #3
    tax
    lda gfxNibMasks,x
    sta kArg+6                // mask (survives DMA)
    lda gfxNibShift,x
    tax
    lda gfxColor
    and #3
    cpx #0
    beq gxPixMcReuVal
gxPixMcReuShl:
    asl
    dex
    bne gxPixMcReuShl
gxPixMcReuVal:
    sta kBoxColor              // shifted value (survives DMA; gfxRow is in use)
    lda #1; sta kArg+3
    lda #0; sta kArg+4
    lda #<gfxTemp; sta kPtr
    lda #>gfxTemp; sta kPtr+1
    lda #0; sta kArg+5
    jsr kReuToC64
    ldy gfxOp
    bne gxPixMcReuXor
    lda kArg+6
    eor #$ff
    and gfxTemp
    ora kBoxColor
    jmp gxPixMcReuPut
gxPixMcReuXor:
    lda kBoxColor
    eor gfxTemp
gxPixMcReuPut:
    sta gfxTemp
    lda #<gfxTemp; sta kPtr
    lda #>gfxTemp; sta kPtr+1
    lda #0; sta kArg+5
    jsr kReuFromC64
    rts

// --- gxGpMc -- MC pixel read (kGetPixel) ---
gxGpMc:
    lda ffY
    cmp #200
    bcs gxGpMcBad             // row >= 200
    lda ffXHi
    bne gxGpMcBad             // col hi must be 0
    lda ffXLo
    cmp #160
    bcs gxGpMcBad             // col >= 160
    ldx ffXLo
    ldy ffY
    jsr gxMcAddr
    ldx curTask
    jsr kSurfDisplayed
    bcc gxGpMcReu
    lda ffXLo
    and #3
    tax
    lda gfxNibMasks,x
    ldy #0
    and (kPtr),y
    jsr gxNibShiftA
    clc                       // C=0 valid
    cli
    rts
gxGpMcReu:
    ldx curTask
    lda kPtr
    sec; sbc #<$2000
    clc; adc taskGfxReuLo,x
    sta kArg
    lda kPtr+1
    sbc #>$2000
    adc taskGfxReuHi,x
    sta kArg+1
    lda taskGfxReuBank,x
    adc #0
    sta kArg+2
    lda #1; sta kArg+3
    lda #0; sta kArg+4
    lda #<gfxTemp; sta kPtr
    lda #>gfxTemp; sta kPtr+1
    lda #0; sta kArg+5
    jsr kReuToC64
    lda ffXLo
    and #3
    tax
    lda gfxNibMasks,x
    and gfxTemp
    jsr gxNibShiftA
    clc
    cli
    rts
gxGpMcBad:
    lda #0
    sec                       // C=1 invalid
    cli
    rts

// --- gxFFReadMc -- MC pixel read for flood fill (ffXLo/Hi/Y) ---
gxFFReadMc:
    ldx ffXLo
    ldy ffY
    jsr gxMcAddr
    ldx curTask
    jsr kSurfDisplayed
    bcs gxFFReadMcC64
    ldx curTask
    lda kPtr
    sec; sbc #<$2000
    clc; adc taskGfxReuLo,x
    sta kArg
    lda kPtr+1
    sbc #>$2000
    adc taskGfxReuHi,x
    sta kArg+1
    lda taskGfxReuBank,x
    adc #0
    sta kArg+2
    lda #1; sta kArg+3
    lda #0; sta kArg+4
    lda #<gfxTemp; sta kPtr
    lda #>gfxTemp; sta kPtr+1
    lda #0; sta kArg+5
    jsr kReuToC64
    lda ffXLo
    and #3
    tax
    lda gfxNibMasks,x
    and gfxTemp
    jmp gxFFReadMcNorm
gxFFReadMcC64:
    lda ffXLo
    and #3
    tax
    lda gfxNibMasks,x
    ldy #0
    and (kPtr),y
gxFFReadMcNorm:
    jsr gxNibShiftA           // A = 0-3
    rts

// --- gxPiMc -- MC plot (kPlot): 2-bit write, no per-pixel matrix ---
gxPiMc:
    lda gfxColHi
    bne gxPiMcDone             // col hi != 0 -> off-screen (X >= 256)
    ldx gfxCol
    ldy gfxRow
    clc                       // col hi = 0
    jsr gxPix
gxPiMcDone:
    cli
    rts

// ==========================================
//  gxChar -- kPrintChar: glyph at pixel (x,y) via gxPix
// ==========================================
//  Input: X = char (screen code), A = colour (hires: 1-15 set;
//         MC: 0-3), kArg = x lo, kArg+1 = x hi, kArg+2 = y.
//  Draws the 8x8 glyph top-left at (x,y), OR-ing set pixels only
//  (clear pixels leave the surface untouched). MC: height doubled
//  (each glyph row drawn twice) to preserve aspect ratio. Off-screen
//  pixels clip via gxPix. Preserves the caller's I flag (php/plp) so
//  gxString stays atomic. Clobbers: A, X, Y, kPtr, kArg block.
gxChar:
    php
    sei
    sta gfxColor
    stx kLineErr                 // char
    lda kArg;   sta kLineX2Lo    // base x lo
    lda kArg+1; sta kLineX2Hi    // base x hi
    lda kArg+2; sta kLineY2      // base y
    // mode flag: kLineStepX = 0 hires, 1 MC
    lda #0
    sta kLineStepX
    ldx curTask
    lda screenKind,x
    cmp #BMPMODE_MC
    bne gxCharGlyph
    inc kLineStepX               // 0 -> 1
gxCharGlyph:
    // glyph ptr = REU font record + char*8 -> 8-byte DMA into FONT_GLYPHS
    lda kLineErr                 // char
    sta kArg
    lda #0
    sta kArg+1
    asl kArg
    rol kArg+1
    asl kArg
    rol kArg+1
    asl kArg
    rol kArg+1                   // char*8
    lda FONT_REC_LO
    clc
    adc kArg
    sta reuFsPtr
    lda FONT_REC_MID
    adc kArg+1
    sta reuFsPtr+1
    lda FONT_REC_HI
    adc #0
    sta reuFsPtr+2               // reuFsPtr = glyph offset (24-bit)
    lda #8; sta kArg+6
    lda #0; sta kArg+7
    lda #<FONT_GLYPHS; sta kPtr
    lda #>FONT_GLYPHS; sta kPtr+1
    jsr kReuDmaRange             // gxChar holds SEI -- atomic
    lda #<FONT_GLYPHS; sta srcPtr
    lda #>FONT_GLYPHS; sta srcPtr+1
                                  // srcPtr = glyph bytes

    ldy #0                       // glyph row (0-7)
gxCharRow:
    lda (srcPtr),y               // glyph byte
    sta kLineMajor
    lda #0
    sta kLineDy                  // bit counter (col offset 0-7)
gxCharBit:
    lda kLineMajor
    asl
    sta kLineMajor               // bit7 -> C
    bcc gxCharBitNext            // clear -> skip (transparent)
    ldx kLineDy                  // X = col offset
    tya
    pha                          // save row (gxCharPlot clobbers Y)
    jsr gxCharPlot
    pla
    tay                          // restore row
gxCharBitNext:
    inc kLineDy
    lda kLineDy
    cmp #8
    bcc gxCharBit
    iny
    cpy #8
    bcc gxCharRow
    plp
    rts

// --- gxCharPlot -- plot glyph pixel (col offset X, glyph row Y) ---
//  Reads kLineX2Lo/Hi (base x), kLineY2 (base y), kLineStepX (mode).
//  Hires: 1 pixel at (baseX+X, baseY+Y). MC: 2 pixels at
//  (baseX+X, baseY+Y*2) and +1 (height doubled). Clobbers A,X,Y,kPtr,kArg.
gxCharPlot:
    // pixel x = baseX + X
    txa
    clc
    adc kLineX2Lo
    sta kLineErr
    lda #0
    adc kLineX2Hi
    sta kLineErrHi
    lda kLineStepX
    bne gxCharPlotMc
    // hires: y = baseY + row
    tya
    clc
    adc kLineY2
    sta kLineStepY
    lda kLineErrHi
    cmp #1                      // carry = col hi
    ldx kLineErr
    ldy kLineStepY
    jsr gxPix
    rts
gxCharPlotMc:
    // MC: y = baseY + row*2, plot y and y+1
    tya
    asl
    clc
    adc kLineY2
    sta kLineStepY
    ldx kLineErr
    ldy kLineStepY
    clc                         // col hi = 0
    jsr gxPix
    ldx kLineErr
    ldy kLineStepY
    iny
    clc
    jsr gxPix
    rts

// ==========================================
//  gxString -- kPrintString: screen-coded string via gxChar
// ==========================================
//  Input: A = colour, kArg = x lo, kArg+1 = x hi, kArg+2 = y,
//         kPtr = null-terminated screen-coded string.
//  Prints each char at 8-pixel horizontal pitch; clipping via gxChar.
//  Preserves the caller's I flag (atomic for the whole string).
//  Clobbers: A, X, Y, kPtr, kArg block.
gxString:
    php
    sei
    sta gfxColor
    lda kPtr;   sta dataPtr      // string lo (ZP -- (zp),y needs a ZP pointer)
    lda kPtr+1; sta dataPtr+1    // string hi
    lda kArg;   sta kLineY1      // x lo
    lda kArg+1; sta kLineDx      // x hi
    lda kArg+2; sta kLineDxHi    // y
gxStringLoop:
    ldy #0
    lda (dataPtr),y              // char
    beq gxStringDone
    tax                          // X = char
    lda kLineY1;   sta kArg      // x lo
    lda kLineDx;   sta kArg+1    // x hi
    lda kLineDxHi; sta kArg+2    // y
    lda gfxColor
    jsr gxChar
    inc dataPtr
    bne gxStringAdv
    inc dataPtr+1
gxStringAdv:
    lda kLineY1
    clc
    adc #8
    sta kLineY1
    lda kLineDx
    adc #0
    sta kLineDx
    jmp gxStringLoop
gxStringDone:
    plp
    rts

// ==========================================
//  gxFillMatrix -- kFillMatrix: fill the whole video matrix
// ==========================================
//  Input: A = raw byte (packed matrix value). Fills all 1000 matrix slots
//  with A unchanged. Callers pack the nibbles themselves -- for MC bitmap
//  the byte is (value1<<4)|value2. Displayed surface -> framebuffer $0400;
//  background -> slot+$2000. SEI/CLI wrapped. Clobbers A, X, Y, kPtr, kArg.
gxFillMatrix:
    sei
    sta gfxColor                // colour
    ldx curTask
    jsr kSurfDisplayed          // C=1 displayed (clobbers A,X,Y)
    bcc gxFmSlot
    lda #<$0400; sta kPtr       // displayed -> framebuffer matrix
    lda #>$0400; sta kPtr+1
    lda #<$03E8; sta kArg
    lda #>$03E8; sta kArg+1
    lda gfxColor
    jsr kReuFillC64
    cli; rts
gxFmSlot:
    ldx curTask
    lda taskGfxReuLo,x          // background -> slot matrix (+$2000)
    clc; adc #<$2000; sta kArg
    lda taskGfxReuHi,x
    adc #>$2000; sta kArg+1
    lda taskGfxReuBank,x
    adc #0; sta kArg+2
    lda #<$03E8; sta kArg+3
    lda #>$03E8; sta kArg+4
    lda gfxColor
    jsr kReuFillReu
    cli; rts

.label gfxEnd = *
