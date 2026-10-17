// ==========================================
//  GordonOS -- Bitmap text blitter
// ==========================================
//  CPU glyph blitter for bitmap-backed text. C64 hires layout is
//  char-cell interleaved: byte = $2000 + (row*40+col)*8 -- each 8x8
//  cell is 8 contiguous bytes. The font is REU-only:
//  256 glyphs x 8 bytes live in the REU FS; fontGlyph (kernel
//  font-help.inc, $5340 pocket) DMA's the current glyph into the
//  FONT_GLYPHS 8-byte scratch.
//
//  Only the displayed-surface path exists: blitChar and blitGlyphFb write
//  straight into the framebuffer.

// --- blitChar -- draw glyph A at cell (X=col, Y=row) in the framebuffer ---
//  Input:  A = char (screencode), X = col (0-39), Y = row (0-24)
//  Output: X preserved
//  Clobbers: A, Y, kArg..kArg+3, srcPtr, kPtr
blitChar:
    sta kArg                    // char
    stx kArg+2                  // col
    sty kArg+3                  // row
    jsr fontGlyph               // srcPtr = glyph bytes (8-byte REU DMA)
blitFbCommon:                   // shared: srcPtr = glyph, kArg+2 = col, kArg+3 = row
    // kPtr = $2000 + row*320 + col*8
    ldy kArg+3                  // row
    lda gfxRowLutLo,y           // $2000 + row*320
    sta kPtr
    lda gfxRowLutHi,y
    sta kPtr+1
    lda kArg+2                  // col
    asl; asl; asl               // col*8 lo, C = col*8 hi
    bcc bfcLo                   // col*8 hi = 0?
    inc kPtr+1                  // kPtr_hi += col*8 hi
bfcLo:
    clc
    adc kPtr                    // kPtr_lo += col*8 lo
    sta kPtr
    bcc bfcDone                 // no lo-carry?
    inc kPtr+1                  // kPtr_hi += lo-carry
bfcDone:
    // copy 8 contiguous glyph bytes into the cell
    ldy #0
bcLoop:
    lda (srcPtr),y
    sta (kPtr),y
    iny
    cpy #8
    bcc bcLoop
    ldx kArg+2                  // restore col (contract: X preserved)
    rts

// --- blitGlyphFb -- framebuffer glyph blitter: kernel.asm, $CDF5 pocket ---
//  It lives outside this file because the $ECDC-$FC3E chain ends against the
//  $FC3F gfx.lib anchor, so whatever leaves the chain is the chain's growth
//  room. Its tail, blitFbCommon, stays here.

// --- blitCharSlot -- draw glyph A + color nibble into curTask's REU slot ---
//  Input: A = char (screencode), X = col, Y = row, kArg+5 = color nibble
//  Output: X preserved
//  Clobbers: A, Y, kArg..kArg+6, srcPtr, kPtr
//  Scratch: kArg+6 (matrixCell lo, per-task saved) + the task's own
//  stack for the matrix byte and matrixCell hi -- all preemption-safe.
//  kArg+7 is NOT touched -- callers (e.g. pcDir) stash values there.
blitCharSlot:
    stx kArg+2                  // col
    sty kArg+3                  // row
    sta kArg+1                  // char (before txa clobbers A)
    txa
    pha                         // save col (restore X at end)
    // matrix byte = (fg<<4) | bg0 -- computed once, held on the stack
    // (stack is per-task; survives the balanced REU subroutines).
    ldx curTask
    lda taskGfxBg0,x
    sta kArg+6
    lda kArg+5
    asl
    asl
    asl
    asl                         // fg << 4
    ora kArg+6
    pha                         // matrix byte on stack

    // srcPtr = glyph bytes (REU font DMA; X = curTask preserved)
    lda kArg+1                  // char
    jsr fontGlyph
blitSlotCommon:                 // shared: srcPtr = glyph, col/row in kArg+2/3, matrix byte pushed
    // matrixCell = row*40 + col -> kArg+6 (lo), kArg+4 (hi)
    ldy kArg+3
    lda gfxCellRowLutLo,y
    sta kArg+6
    lda gfxCellRowLutHi,y
    sta kArg+4
    lda kArg+6
    clc
    adc kArg+2                  // + col
    sta kArg+6
    lda kArg+4
    adc #0
    sta kArg+4                  // matrixCell: lo kArg+6, hi kArg+4
    lda kArg+4
    pha                         // matrixCell hi on stack (kArg+4 is
                                // clobbered by the glyph DMA args)

    // cellOffset = matrixCell * 8 -> kArg (lo), kArg+1 (hi)
    lda kArg+6
    asl
    sta kArg
    lda kArg+4
    rol
    sta kArg+1
    asl kArg
    rol kArg+1
    asl kArg
    rol kArg+1                  // cellOffset in kArg/kArg+1

    // --- glyph DMA: C64 (srcPtr) -> slot pixels (base + cellOffset) ---
    lda srcPtr
    sta kPtr
    lda srcPtr+1
    sta kPtr+1                  // C64 source
    lda taskGfxReuLo,x          // X = curTask (kReuFromC64 preserves X)
    clc
    adc kArg
    sta kArg
    lda taskGfxReuHi,x
    adc kArg+1
    sta kArg+1
    lda taskGfxReuBank,x
    adc #0
    sta kArg+2                  // REU dest
    lda #8
    sta kArg+3
    lda #0
    sta kArg+4                  // 8 bytes
    jsr kReuFromC64

    // --- color DMA: stack byte -> slot matrix (base + $2000 + matrixCell) ---
    lda taskGfxReuLo,x          // X = curTask still
    clc
    adc kArg+6                  // matrixCell lo
    sta kArg
    pla                         // matrixCell hi
    adc taskGfxReuHi,x          // + carry from lo add
    adc #>$2000
    sta kArg+1
    lda taskGfxReuBank,x
    adc #0
    sta kArg+2                  // REU dest
    // kPtr = $0100 + (SP+1) -- the pushed matrix byte is now on top
    tsx
    inx
    stx kPtr
    lda #$01
    sta kPtr+1
    lda #1
    sta kArg+3
    lda #0
    sta kArg+4                  // 1 byte
    jsr kReuFromC64

    pla                         // discard matrix byte (balance)
    pla
    tax                         // restore col (contract: X preserved)
    rts

// --- blitGlyphSlot -- blit 8 bytes from (srcPtr) + color into the REU slot ---
//  Input: srcPtr = glyph bytes, X = col, Y = row, kArg+5 = color nibble
//  Output: X preserved
//  Clobbers: A, Y, kArg..kArg+6, kPtr
blitGlyphSlot:
    stx kArg+2                  // col
    sty kArg+3                  // row
    txa
    pha                         // save col (restore X at end)
    // matrix byte = (fg<<4) | bg0 -- held on the stack (same as blitCharSlot)
    ldx curTask
    lda taskGfxBg0,x
    sta kArg+6
    lda kArg+5
    asl
    asl
    asl
    asl                         // fg << 4
    ora kArg+6
    pha                         // matrix byte on stack
    jmp blitSlotCommon

