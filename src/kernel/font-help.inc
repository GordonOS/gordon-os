// ==========================================
//  font-help.inc -- REU-only font helpers
// ==========================================
//  Pinned by main.asm in the $5340-$54AA loader-headroom pocket (between
//  the loader's band end at $533F and scrollScreenSlot at $54AB). The
//  font lives in the REU FS (256 glyphs x 8 bytes, screencode order);
//  bootFontResolve records its FS data-area offset in fontRecLo/Mid/Hi
//  and fontGlyph DMA's one glyph into FONT_GLYPHS on demand.
//  The seeder (tools/build-reu.ps1) bank-aligns .fnt files, but both
//  paths use reuDmaRange/reuFindFile, which are multi-bank safe anyway.

// --- bootFontResolve -- resolve gordon.fnt, store the record ---
//  Input:  bootFontName (global label, boot.inc)
//  Output: C=0 -> fontRecLo/Mid/Hi = 24-bit FS data-area offset
//          C=1 -> not found or < 2048 bytes
//  Clobbers: A, X, Y, kArg..kArg+1, reuFsPtr/reuFsRemain (kernel globals)
//  NOTE: called by the boot task before any other task exists, so the
//  3-byte record write needs no SEI (no task can blit mid-write).
bootFontResolve:
    lda #<bootFontName; sta kArg
    lda #>bootFontName; sta kArg+1
    jsr kReuFindFile            // C=0: reuFsPtr(3B) = offset, reuFsRemain = size
    bcs bootFontBad             // not found -> fail (C=1)
    lda reuFsRemain+1
    cmp #8                      // size >= 2048 (2KB font)?
    bcc bootFontBad             // too short -> fail (C=1)
    :FSCopy(reuFsPtr, fontRecLo)   // the 3-byte record -- never 2 of 3
    clc                         // success -> C=0
    rts
bootFontBad:
    sec                         // fail -> C=1
    rts

// --- fontGlyph -- DMA the 8-byte glyph for char A into FONT_GLYPHS ---
//  Input:  A = char (screencode)
//  Output: srcPtr = FONT_GLYPHS (glyph bytes)
//  Clobbers: A, Y, kArg..kArg+1, kArg+4, kArg+5, kPtr, reuFsPtr
//  Preserves: X, kArg+2, kArg+3, kArg+6, kArg+7, and the caller's flags
//  (callers' col/row/matrixCell survive the REU DMA)
//  Atomic: php/sei wraps the scratch + reuFsPtr stash + DMA -- callers that
//  run with IRQs enabled (rsDelErase) must not race another task's glyph.
fontGlyph:
    php                         // save caller flags (incl. I)
    sei                         // make scratch + reuFsPtr + DMA atomic
    sta fontChar                // stash the char -- the stash loads below
                                // clobber A (bug: asl shifted kArg+7)
    lda kArg+2; sta FONT_C2    // preserve col
    lda kArg+3; sta FONT_C3    // preserve row
    lda kArg+6; sta FONT_C6    // preserve matrixCell lo
    lda kArg+7; sta FONT_C7    // preserve kArg+7
    // char*8 -> kArg (lo) / kArg+1 (hi)
    lda #0
    sta kArg+1
    lda fontChar
    asl
    rol kArg+1
    asl
    rol kArg+1
    asl
    rol kArg+1
    sta kArg
    // reuFsPtr = fontRec + char*8 (24-bit FS data-area offset)
    :FSAdd16(reuFsPtr, FONT_REC_LO, kArg, kArg+1)
    // 8-byte DMA -> FONT_GLYPHS (multi-bank safe)
    lda #8; sta kArg+6
    lda #0; sta kArg+7
    lda #<FONT_GLYPHS; sta kPtr
    lda #>FONT_GLYPHS; sta kPtr+1
    jsr reuDmaRange
    // restore the preserved caller state
    lda FONT_C2; sta kArg+2
    lda FONT_C3; sta kArg+3
    lda FONT_C6; sta kArg+6
    lda FONT_C7; sta kArg+7
    lda #<FONT_GLYPHS; sta srcPtr
    lda #>FONT_GLYPHS; sta srcPtr+1
    plp                         // restore caller flags (incl. I)
    rts

.label fontHelpEnd = *
