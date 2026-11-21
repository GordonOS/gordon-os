// ==========================================
//  scrollScreenSlot -- background slot scroll, below the pool
// ==========================================
//  This routine lives at $54AB, right after libstubs.inc, in the pages below
//  POOL_BASE (`.assert scrollSlotEnd <= POOL_BASE` guards the boundary).
//  Called via the scrollScreen dispatcher under SEI with cursorOn = 0.
.pc = $54AB

// --- scrollScreenSlot -- scroll the BACKGROUND surface's slot region ---
//  Called from scrollScreen under SEI with cursorOn = 0. The slot lives
//  in REU, so each row round-trips through C64 scratchBuf (REU can't do
//  REU->REU). Moves the region's rows one place, up or down per the
//  direction, and blanks the row the content vacated. R == 0 blanks that
//  row only. X = curTask (reloaded -- the dispatcher's surfDisplayed
//  clobbers X on the sharee path). Clobbers A, X, Y, kPtr, kArg,
//  scrollSeedBuf.
scrollScreenSlot:
    ldx curTask                 // surfDisplayed clobbers X on the sharee path
    lda taskScrollTop,x
    sta kArg+6                  // T
    lda taskScrollBottom,x
    sec
    sbc kArg+6
    sta kArg+7                  // R = B - T
    jsr scrollRows              // scrSrc/scrDst/scrBlank from T, R and the direction
    //  The char shadow mirrors the same three rows, and R is needed to move them,
    //  so it is done HERE -- the row loop below counts kArg+7 down to zero.
    lda taskShadowFlag,x
    beq sssNoShadow
    jsr pcShadowScroll          // X = curTask, preserved
sssNoShadow:
    ldy kArg+7
    bne sssMove                 // R != 0 -> do the move
    jmp sssClear                // R == 0 -> blank the vacated row only (far)
sssMove:
    //  The loop walks DESTINATIONS and derives each source as dest - scrollStep.
    //  The walk must start at the far end of the move and head back towards the
    //  vacated row -- T upwards when scrolling up, B downwards when scrolling
    //  down -- so every source row is read before a move can overwrite it.
    //  Walking sources from scrSrc instead would, on a down scroll, read row r+1
    //  after it had already been overwritten with row r's content.
    ldy scrDst                  // up: T
    lda scrollStep
    bmi sssStart                // $FF -> up, scrDst is the far end
    lda kArg+6                  // down: start at B = T + R
    clc
    adc kArg+7
    tay
sssStart:
    sty sssRow
sssLoop:
    // --- pixel row dst-src -> row dst (320B via scratchBuf) ---
    ldy sssRow
    tya
    sec
    sbc scrollStep              // the source row for this destination
    tay
    lda gfxRowLutLo,y           // r*320 lo ($2000 lo byte = 0)
    sta scrollSeedBuf
    lda gfxRowLutHi,y           // r*320 hi + $20
    sec
    sbc #>$2000
    sta scrollSeedBuf+1         // r*320
    jsr sssAddr                 // kArg = slot + r*320 (source)
    lda #<$0140; sta kArg+3
    lda #>$0140; sta kArg+4
    lda #0; sta kArg+5
    lda #<$C400; sta kPtr
    lda #>$C400; sta kPtr+1
    jsr kReuToC64               // slot source -> scratchBuf
    ldy sssRow
    lda gfxRowLutLo,y
    sta scrollSeedBuf
    lda gfxRowLutHi,y
    sec
    sbc #>$2000
    sta scrollSeedBuf+1
    jsr sssAddr                 // kArg = slot + dst*320 (dest)
    lda #<$0140; sta kArg+3
    lda #>$0140; sta kArg+4
    lda #0; sta kArg+5
    lda #<$C400; sta kPtr
    lda #>$C400; sta kPtr+1
    jsr kReuFromC64             // scratchBuf -> slot dest

    // --- matrix row dst-src -> row dst (40B via scratchBuf) ---
    ldy sssRow
    tya
    sec
    sbc scrollStep
    tay
    lda gfxCellRowLutLo,y
    sta scrollSeedBuf
    lda gfxCellRowLutHi,y
    sta scrollSeedBuf+1         // r*40
    jsr sssAddrPlane            // kArg = slot + $2000 + r*40 (source)
    lda #SCREEN_COLS; sta kArg+3
    lda #0; sta kArg+4
    sta kArg+5
    lda #<$C400; sta kPtr
    lda #>$C400; sta kPtr+1
    jsr kReuToC64
    ldy sssRow
    lda gfxCellRowLutLo,y
    sta scrollSeedBuf
    lda gfxCellRowLutHi,y
    sta scrollSeedBuf+1         // dst*40
    jsr sssAddrPlane            // kArg = slot + $2000 + dst*40 (dest)
    lda #SCREEN_COLS; sta kArg+3
    lda #0; sta kArg+4
    sta kArg+5
    lda #<$C400; sta kPtr
    lda #>$C400; sta kPtr+1
    jsr kReuFromC64

    // --- next destination row, R rows in all ---
    lda sssRow
    sec
    sbc scrollStep              // towards the vacated row
    sta sssRow
    dec kArg+7
    beq sssClear                // forward and close: the loop itself is behind us
    jmp sssLoop

sssClear:
    // --- blank the vacated pixel row (B scrolling up, T down) ---
    ldy scrBlank
    lda gfxRowLutLo,y           // B*320 lo ($2000 lo byte = 0)
    sta scrollSeedBuf
    lda gfxRowLutHi,y           // B*320 hi + $20
    sec
    sbc #>$2000
    sta scrollSeedBuf+1         // B*320
    jsr sssAddr                 // kArg = slot + B*320
    lda #<$0140; sta kArg+3
    lda #>$0140; sta kArg+4
    lda #0
    jsr kReuFillReu             // blank that row's pixels (X preserved)

    // --- and its matrix row ---
    ldy scrBlank
    lda gfxCellRowLutLo,y
    sta scrollSeedBuf
    lda gfxCellRowLutHi,y
    sta scrollSeedBuf+1         // B*40
    jsr sssAddrPlane            // kArg = slot + $2000 + B*40
    lda #SCREEN_COLS; sta kArg+3
    lda #0; sta kArg+4
    lda taskGfxBg0,x
    jsr kReuFillReu             // blank that row's matrix (bg0)
sssDone:
    rts

// --- sssAddr -- kArg/kArg+1/kArg+2 = slot base + a byte offset ---
//  In:  scrollSeedBuf/scrollSeedBuf+1 = offset within the surface's REU block
//  Out: kArg/kArg+1 = REU address, kArg+2 = bank. X (curTask) preserved; Y
//       preserved, so a caller's row index survives.
//  Clobbers: A, kArg..kArg+2
sssAddr:
    lda taskGfxReuLo,x
    clc
    adc scrollSeedBuf
    sta kArg
    lda taskGfxReuHi,x
    adc scrollSeedBuf+1
    sta kArg+1
    lda taskGfxReuBank,x
    adc #0
    sta kArg+2
    rts

// --- sssAddrPlane -- sssAddr, then the $2000 cell-matrix plane ---
//  The matrix sits $2000 into the slot, so the plane goes on the address hi
//  byte. A ripple out of that add is the bank and must be carried into it: a
//  slot base is slot x $2800, so its hi byte can be $E0 or more and the plane
//  takes it past $FF.
//  Clobbers: A, kArg..kArg+2
sssAddrPlane:
    jsr sssAddr
    lda kArg+1
    clc
    adc #>$2000
    sta kArg+1
    bcc sapDone
    inc kArg+2
sapDone:
    rts

// ==========================================
//  Scroll direction, and the rows a scroll uses
// ==========================================
//  A scroll moves a region's rows one place up or one place down. Up moves
//  rows T+1..B to T..B-1 and blanks B; down moves T..B-1 to T+1..B and blanks
//  T. Three surfaces follow that move -- the framebuffer or the slot, and the
//  char shadow -- and all three work from the same three row numbers, derived
//  once per scroll by scrollRows:
//
//      scrSrc    the first row to move FROM
//      scrDst    the first row to move TO
//      scrBlank  the row the content vacated (blanked after the move)
//
//  scrollStep is the signed row offset from a source row to its destination:
//  $FF for an up scroll, $01 for a down one. It is the DEFAULT for an up scroll
//  and the kScrollRegionDown entry sets it to $01 for the length of one scroll,
//  clearing it again once scrollScreen has returned -- the whole thing runs
//  under SEI, so no task can observe the value in between, and a direction left
//  standing would turn every later up scroll into a down one. It must NOT be
//  cleared by scrollRows: scrollScreenSlot reads it to decide which way to walk
//  the rows, and clearing it there made every background scroll move UP.
//  The block copies (the displayed surface and the char shadow) move scrSrc to
//  scrDst in one go through the REU bounce, which is overlap-safe either way.
//  scrollScreenSlot cannot do that -- REU cannot copy to REU -- so it walks the
//  rows one at a time, starting at the far end of the move (T upwards when
//  scrolling up, B downwards when scrolling down) and working back towards the
//  vacated row, deriving each source as dest - scrollStep. A source row is then
//  always read before a move can overwrite it.
scrollStep:   .byte $FF
scrSrc:       .byte 0
scrDst:       .byte 0
scrBlank:     .byte 0
sssRow:       .byte 0

// --- scrollRows -- derive scrSrc/scrDst/scrBlank from the region ---
//  In:  kArg+6 = T, kArg+7 = R (= B - T), scrollStep = the direction.
//  Out: scrSrc, scrDst, scrBlank. X and Y preserved. Clobbers: A.
//  scrollStep is read, not consumed -- the caller owns it (see the note above).
scrollRows:
    tya
    pha
    ldy scrollStep              // the direction
    lda kArg+6                  // T
    cpy #$80                    // $FF (up) is >= $80; $01 (down) is not
    bcs srUp
    // --- down: source T, dest T+1, blank T ---
    sta scrSrc
    sta scrBlank
    clc
    adc #1
    sta scrDst
    pla
    tay
    rts
srUp:
    // --- up: source T+1, dest T, blank B ---
    sta scrDst
    clc
    adc #1
    sta scrSrc
    lda kArg+6
    clc
    adc kArg+7                  // T + R = B
    sta scrBlank
    pla
    tay
    rts

// --- scrollRegionDown -- kScrollRegionDown entry ($FFF6) ---
//  Scroll the current task's region DOWN one row, the mirror of kScrollScreen:
//  rows T..B-1 move to T+1..B and row T is blanked. No arguments, like
//  kScrollScreen -- the region is the task's own, and the cursor is left where
//  the caller put it, because there is no wrap to clamp.
//  The whole thing runs under SEI: that closes the window in which this task
//  could be killed with scrollStep set, and it lets the entry decide whether a
//  scroll will happen at all before it sets the direction.
//  Clobbers A, X, Y, kPtr, the kArg block (kScrollScreen's own contract).
scrollRegionDown:
    sei
    ldx curTask
    lda screenAlloc,x
    beq srdDone                 // no surface -> nothing to scroll: do not set a direction
    lda #1
    sta scrollStep
    jsr scrollScreen            // reads scrollStep; SEI'd inside
    lda #$ff
    sta scrollStep              // back to the up-scroll default, still under SEI
srdDone:
    clc
    cli
    rts

.label scrollSlotEnd = *
