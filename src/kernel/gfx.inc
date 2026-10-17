//  Anchored: this file's data addresses ($ECE9+) are ABI -- gfx.lib hardcodes
//  them (gfxPixelMasks/gfxCol/dispIsBitmap/kLineX1Lo/kBoxColor) and main.asm
//  asserts them -- so the file must not drift when the code ahead of it
//  (scroll-region.inc) changes size. Growth ahead is absorbed by the gap above
//  the anchor; growing into the anchor is an overlap error, not a silent move.
// ==========================================
.pc = $ECDC

// --- Bitmap slot free-list ---
gfxSlotFreeMap: .fill 4, 0
gfxSlotFirstFree: .byte 0

// ==========================================
//  BITMAP GRAPHICS -- REU slot allocator
// ==========================================
//  gfxSlotBitMasks -- bit mask lookup for slot alloc/free
gfxSlotBitMasks:
    .byte $01, $02, $04, $08, $10, $20, $40, $80
gfxPixelMasks:
    .byte $80, $40, $20, $10, $08, $04, $02, $01

// --- gfxSlotAlloc -- find and allocate a free bitmap slot ---
//  Output: A = slot index (0-27), C=0 on success; C=1 if none free.
//  Clobbers: A, X, Y
//  Updates: gfxSlotFreeMap (4 bytes), gfxSlotFirstFree
gfxSlotAlloc:
    ldx gfxSlotFirstFree        // start search at last freed slot
gsaLoop:
    txa
    pha                         // save slot on stack
    lsr; lsr; lsr               // A = slot / 8 (byte index)
    tay
    lda gfxSlotFreeMap,y
    sta kArg                    // temp: copy of map byte
    txa                         // A = slot (X still holds it)
    and #7                      // bit within byte
    tax
    lda gfxSlotBitMasks,x
    and kArg                    // test: 0=free, non-zero=allocated
    beq gsaFound
    pla                         // A = slot number (discard from stack)
    tax                         // X = slot number (restore from bit-index clobber)
    inx                         // advance to next slot
    cpx #GFX_SLOT_COUNT
    bcc gsaNext
    ldx #0
gsaNext:
    cpx gfxSlotFirstFree
    bne gsaLoop
    sec
    rts                         // full circle, none free
gsaFound:
    lda gfxSlotBitMasks,x
    ora gfxSlotFreeMap,y        // mark as allocated (set bit=1)
    sta gfxSlotFreeMap,y
    pla                         // A = slot index
    clc
    rts

// --- gfxSlotFree -- free a bitmap slot ---
//  Input: A = slot index (0-27)
//  Clobbers: A, X, Y
//  Updates: gfxSlotFreeMap, gfxSlotFirstFree
gfxSlotFree:
    sta gfxSlotFirstFree        // hint for next alloc
    pha
    lsr; lsr; lsr               // byte index
    tay
    pla
    and #7                      // bit within byte
    tax
    lda gfxSlotBitMasks,x
    eor #$ff                    // invert -> mask to clear
    and gfxSlotFreeMap,y
    sta gfxSlotFreeMap,y
    rts

// ==========================================
//  BITMAP GRAPHICS -- kPlot + kClearBitmap
// ==========================================

gfxCol:     .byte 0             // col lo byte (0-255)
gfxColHi:   .byte 0             // col hi bit  (internal: derived from Carry)
gfxRow:     .byte 0             // saved row
gfxTemp:    .byte 0             // 1-byte REU RMW buffer
gfxColor:   .byte 0             // kPlot color (shared by pixel + matrix paths)
dispIsBitmap: .byte 0          // 1 = live display is a bitmap (set by doGfxSwitch)

// --- kLine state (16 bytes, after all code) ---
kLineX1Lo:  .byte 0
kLineX1Hi:  .byte 0
kLineY1:    .byte 0
kLineX2Lo:  .byte 0
kLineX2Hi:  .byte 0
kLineY2:    .byte 0
kLineDx:    .byte 0
kLineDxHi:  .byte 0
kLineDy:    .byte 0
kLineStepX: .byte 0
kLineStepY: .byte 0
kLineErr:   .byte 0
kLineErrHi: .byte 0
kLineMajor: .byte 0
kLineCount: .byte 0
kLineCountHi: .byte 0

// --- kBox/kFillBox state (7 bytes) ---
kBoxColor:  .byte 0             // fill/outline color
kBoxX1Lo:   .byte 0
kBoxX1Hi:   .byte 0
kBoxY1:     .byte 0             // also used as fill Y counter
kBoxY2:     .byte 0
kBoxX2Lo:   .byte 0
kBoxX2Hi:   .byte 0

// --- the pixel write op (1 byte) ---
//  WARNING: it sits AFTER the block above, and it has to stay there. gfx.lib
//  hardcodes every address from gfxColor to kBoxX2Hi, so a byte inserted anywhere
//  in between would move one of them -- appended here it moves nothing, and the
//  library reaches it as gfxOp ($ED5D).
//  0 = set/clear on gfxColor, which is what every drawing call has always done;
//  1 = XOR, which is what a preview needs: the same call over the same pixels takes
//  them back off again. It is honoured by every pixel writer in both modes. In MC the
//  XOR is on the pixel's 2-bit VALUE, so the second pass restores it exactly -- what the
//  first pass SHOWS is another matter: over background it is the pen's value (0 xor k = k,
//  as in hires), over drawn work it is whichever of the cell's other sources the
//  arithmetic lands on, and it is nothing at all where two palette entries share a colour.
gfxOp:      .byte 0

// ==========================================
//  surface switching (unified)
// ==========================================

// --- surfSave -- DMA framebuffer -> task X's slot (10 KB) ---
//  Input: X = task index (slot from taskGfxReu*); X preserved
//  Clobbers: A, Y, kPtr, kArg..kArg+4
surfSave:
    // pixels: $2000-$3FFF -> slot+0
    lda taskGfxReuLo,x; sta kArg
    lda taskGfxReuHi,x; sta kArg+1
    lda taskGfxReuBank,x; sta kArg+2
    lda #<$2000; sta kPtr
    lda #>$2000; sta kPtr+1
    lda #<$2000; sta kArg+3
    lda #>$2000; sta kArg+4
    lda #0; sta kArg+5
    jsr kReuFromC64
    // matrix: $0400-$07FF -> slot+$2000 (kArg lo + kArg+5 survive the DMA)
    lda taskGfxReuHi,x; clc; adc #>$2000; sta kArg+1
    lda taskGfxReuBank,x; adc #0; sta kArg+2
    lda #<$0400; sta kPtr
    lda #>$0400; sta kPtr+1
    lda #<$0400; sta kArg+3
    lda #>$0400; sta kArg+4
    jsr kReuFromC64
    // color RAM: $D800-$DBFF -> slot+$2400 (len $0400 survives too)
    lda taskGfxReuHi,x; clc; adc #>$2400; sta kArg+1
    lda taskGfxReuBank,x; adc #0; sta kArg+2
    lda #<$d800; sta kPtr
    lda #>$d800; sta kPtr+1
    jsr kReuFromC64
    rts

// --- surfLoad -- DMA task X's slot -> framebuffer + set VIC mode/colors ---
//  Input: X = task index (slot from taskGfxReu*); X preserved
//  Clobbers: A, Y, kPtr, kArg..kArg+4
surfLoad:
    // pixels: slot+0 -> $2000-$3FFF
    lda taskGfxReuLo,x; sta kArg
    lda taskGfxReuHi,x; sta kArg+1
    lda taskGfxReuBank,x; sta kArg+2
    lda #<$2000; sta kPtr
    lda #>$2000; sta kPtr+1
    lda #<$2000; sta kArg+3
    lda #>$2000; sta kArg+4
    lda #0; sta kArg+5
    jsr kReuToC64
    // matrix: slot+$2000 -> $0400-$07FF (kArg lo + kArg+5 survive the DMA)
    lda taskGfxReuHi,x; clc; adc #>$2000; sta kArg+1
    lda taskGfxReuBank,x; adc #0; sta kArg+2
    lda #<$0400; sta kPtr
    lda #>$0400; sta kPtr+1
    lda #<$0400; sta kArg+3
    lda #>$0400; sta kArg+4
    jsr kReuToC64
    // color RAM: slot+$2400 -> $D800-$DBFF (len $0400 survives too)
    lda taskGfxReuHi,x; clc; adc #>$2400; sta kArg+1
    lda taskGfxReuBank,x; adc #0; sta kArg+2
    lda #<$d800; sta kPtr
    lda #>$d800; sta kPtr+1
    jsr kReuToC64
    rts

// --- doGfxSwitch -- switch VIC display between surfaces (unified) ---
//  Input: kArg = fromTask index, X = toTask index
//  Save framebuffer -> fromTask's slot, load toTask's slot -> framebuffer,
//  set VIC mode + colors. Handles from/to tasks with no surface (blank).
//  Clobbers: A, X, Y, kPtr, kArg
//  Preserves caller's I flag (called from task and IRQ context).
doGfxSwitch:
    php                         // save caller's I flag
    sei
    // Blank the display for the whole hand-over. The 20 KB of surfSave +
    // surfLoad rewrites the framebuffer while the beam scans it, so without
    // this the switch shows a torn screen. DEN=0 also stops Bad Lines, and
    // the c-accesses that come with them are the only thing that reads
    // colour RAM -- so the VIC leaves colour RAM alone while it is blanked.
    // The mode/colour block below restores DEN with the mode bits.
    lda $d011
    and #$ef                    // DEN=0
    sta $d011
    stx kArg+6                  // toTask (gfxSurfPrep's eraseCursor clobbers kArg+7)
    ldy kArg                    // fromTask
    cpy #MAX_TASKS
    bcs dgsNoFrom               // no/invalid focus ($FF) -- nothing to save
    lda screenAlloc,y
    beq dgsNoFrom
    tya; tax                    // X = fromTask (surfSave contract)
    cpy curTask
    bne dgsNoCurSave            // running task keeps its cursor (already synced)
    jsr gfxSurfPrep             // un-draw the live block, then save cursor state
dgsNoCurSave:
    jsr surfSave                // X preserved
dgsNoFrom:
    ldx kArg+6                  // X = toTask
    lda screenAlloc,x
    beq dgsBlank
    jsr surfLoad                // X preserved (toTask)
    jsr loadSlotRegion          // restore toTask's region ZP (X preserved)
    // VIC mode + colors (DEN comes back on with BMM)
    lda $d011; ora #$30; sta $d011   // bitmap on, display on
    lda screenKind,x            // X = toTask (preserved through both calls)
    cmp #BMPMODE_MC
    bne dgsStd
    lda $d016; ora #$10; sta $d016   // multicolor on
    bne dgsColors               // always
dgsStd:
    lda $d016; and #$ef; sta $d016   // multicolor off
dgsColors:
    lda #GFX_D018; sta $d018
    lda taskGfxBorder,x; sta $d020   // X = toTask
    lda taskGfxBg0,x;    sta $d021
    lda taskGfxBg1,x;    sta $d022
    lda taskGfxBg2,x;    sta $d023
    jmp dgsDone
dgsBlank:
    // no surface -- blank framebuffer, hires bitmap, default colors
    lda #<$2000; sta kPtr
    lda #>$2000; sta kPtr+1
    lda #<$2000; sta kArg
    lda #>$2000; sta kArg+1
    lda #0
    jsr kReuFillC64             // blank pixels
    lda #<$0400; sta kPtr
    lda #>$0400; sta kPtr+1
    lda #<$0400; sta kArg
    lda #>$0400; sta kArg+1
    lda #DEFAULT_BG_COLOR
    jsr kReuFillC64             // blank matrix (bg nibble = default bg)
    lda #<$d800; sta kPtr
    lda #>$d800; sta kPtr+1
    lda #DEFAULT_BG_COLOR       // kArg/kArg+1 = $0400 still (from matrix fill)
    jsr kReuFillC64             // fill color RAM with background color
    lda $d011; ora #$30; sta $d011   // bitmap on, display on
    lda $d016; and #$ef; sta $d016
    lda #GFX_D018; sta $d018
    lda #DEFAULT_BORDER_COLOR; sta $d020
    lda #DEFAULT_BG_COLOR; sta $d021
dgsDone:
    lda #1; sta dispIsBitmap    // VIC is always in bitmap mode now
    lda #0; sta cursorVisible   // next blink = ON -> fresh save of the
                                // incoming surface's cell (a stale saved
                                // glyph from the old surface must never
                                // be restored onto the new one)
    lda blinkRate; sta blinkTick
    plp; rts                    // restore caller's I flag

// --- csrSyncTail -- persist cursorOn to the surface's matrix tail ---
//  Tail-call target of csrBoth (screen-io.inc -- the pre-$F063 flow has
//  no spare bytes; this file does). cursorOn already holds the new
//  value. Displayed surface -> framebuffer tail $07ea; background ->
//  slot+$23ea via 1-byte REU DMA, driven straight from $df02-$df0a.
//  Clobbers A, X, Y.
csrSyncTail:
    php
    sei                         // atomic: the tail write must not race a
                                // display switch or the IRQ's REU traffic
    ldx curTask
    lda screenAlloc,x
    beq csrNoSurface            // no surface -- nothing to persist
    jsr surfDisplayed           // C=1 -> surface is displayed
    bcs csrTailFb
    // background -> slot+$23ea (1-byte REU DMA)
    ldx curTask                 // surfDisplayed clobbered X
    lda taskGfxReuLo,x
    clc
    adc #<(GFX_SLOT_MATRIX + GFX_CURSOR_ON)
    sta $df04
    lda taskGfxReuHi,x
    adc #>(GFX_SLOT_MATRIX + GFX_CURSOR_ON)
    sta $df05
    lda taskGfxReuBank,x
    adc #0
    sta $df06
    lda #<cursorOn
    sta $df02
    lda #>cursorOn
    sta $df03
    lda #1; sta $df07
    lda #0; sta $df08
    sta $df0a
    lda #$90; sta $df01         // C64->REU, immediate
    jsr reuWait
csrNoSurface:
    plp
    rts
csrTailFb:
    lda cursorOn
    sta GFX_MATRIX_BASE + GFX_CURSOR_ON
    plp
    rts

// --- readRow -- read a screen row back from the char shadow ---
//  Input:  A = row (0-24), kPtr = caller's 40-byte dest (not advanced)
//  Output: C=0 row copied; C=1 no surface/shadow -> dest = 40 spaces
//  Clobbers: A, X, Y, kArg..kArg+5
readRow:
    sei                         // shadow belongs to curTask -- atomic vs switch
    pha                         // save row
    ldx curTask
    lda screenAlloc,x
    beq rrUnavail
    lda taskShadowFlag,x
    beq rrUnavail
    jsr shadowHiOf              // A = shadow page hi (clobbers A,X,Y,kArg..kArg+3,kResult)
    sta kArg+5                  // shadow hi (kArg+5 free now)
    pla                         // row
    tay
    lda gfxCellRowLutLo,y       // row*40 lo
    sta $df04
    lda gfxCellRowLutHi,y       // row*40 hi
    clc
    adc kArg+5                  // + shadow page hi
    sta $df05
    lda #0
    sta $df06                   // bank 0
    lda kPtr                    // dest = caller's buffer
    sta $df02
    lda kPtr+1
    sta $df03
    lda #SCREEN_COLS            // one matrix row = the row's screen codes
    sta $df07
    lda #0
    sta $df08
    sta $df0a
    lda #$91                    // REU -> C64, immediate
    sta $df01
    jsr reuWait
    cli
    clc
    rts
rrUnavail:
    pla                         // discard saved row
    ldy #0
    lda #$20
rrFill:
    sta (kPtr),y
    iny
    cpy #SCREEN_COLS
    bcc rrFill
    cli
    sec
    rts

// --- printCharAt -- write glyph + matrix nibble at (X=col, Y=row) ---
//  Like printChar but with NO cursor advance, wrap or scroll: writes exactly
//  one cell. paRoute picks the target -- the task's surface displayed or its
//  REU slot -- and mirrors the char shadow; the framebuffer path below also
//  erases a live blink block first, restoring at the blink's SAVED position,
//  never the live cursor, so the next blink OFF can't resurrect stale glyph
//  bytes over the freshly written cell. Saves/restores the live cursor.
//  Caller must disable the cursor first (blink can't fire with cursorOn=0).
//  SEI-guarded (shadow DMA + blit are atomic vs display switches).
//  Input:  A = char (screencode), X = col (0-39), Y = row (0-24)
//  Clobbers: A, X, Y, kPtr, srcPtr, kArg..kArg+7
//  printGlyphAt joins the tail at paNibble and leaves through paDone, so both
//  entry points share the matrix-nibble write and the cursor restore. That
//  costs nothing here (labels only) and is what keeps the glyph writer off the
//  $ECDC-$FC3E chain's full code band. The stack is the contract between them:
//  paDone pops cursorRow and then cursorCol, so a caller entering at paNibble
//  must have pushed col first and then row.
printCharAt:
    sei
    sta kArg+6                  // char (pcShadowChar source)
    lda cursorCol
    pha
    lda cursorRow
    pha
    stx cursorCol               // X still = col
    sty cursorRow               // Y still = row
    ldx curTask
    lda taskTextColor,x
    sta kArg+5                  // color (blitCharSlot and blitChar read it)
    jsr paRoute                 // C=1 displayed, C=0 -> cell already in the slot
    bcc paDone
    jsr eraseCursor             // un-draw a live blink block (saved pos)
paBlit:
    ldx cursorCol               // col
    ldy cursorRow               // row
    lda kArg+6
    jsr blitChar                // glyph to framebuffer (X preserved)
paNibble:                       // printGlyphAt's join point: its glyph is already
                                //   written, so only the nibble and the restore
                                //   remain -- see the header for the stack contract
    // matrix nibble = (taskTextColor<<4) | existing bg nibble
    ldy cursorRow
    lda gfxCellRowLutLo,y
    clc
    adc cursorCol
    sta kPtr
    lda gfxCellRowLutHi,y
    adc #>$0400
    sta kPtr+1
    ldy #0
    lda (kPtr),y
    and #$0F                    // keep bg nibble
    pha
    ldx curTask
    lda taskTextColor,x         // font DMA clobbers kArg+5
    asl
    asl
    asl
    asl                         // << 4 -> foreground nibble
    tsx
    ora $0101,x                 // combine with pushed bg nibble
    sta (kPtr),y
    pla                         // balance bg-nibble push
paDone:
    pla
    sta cursorRow
    pla
    sta cursorCol
    cli
    rts

// --- eraseCursor -- un-draw a live blink block at its SAVED position ---
//  If cursorVisible, restore the saved glyph + matrix nibble at
//  (GFX_CURSOR_SCOL, GFX_CURSOR_SROW) and clear cursorVisible so the
//  next blink is a fresh ON. Caller must hold SEI with cursorOn=0.
//  Restores at the SAVED position, never the live cursor.
//  Clobbers: A, X, Y, kPtr, kArg+7.
eraseCursor:
    lda cursorVisible
    beq ecRts
    lda GFX_MATRIX_BASE + GFX_CURSOR_SROW
    jsr rowAddr                 // kPtr = $2000 + savedRow*320
    lda GFX_MATRIX_BASE + GFX_CURSOR_SCOL
    sta kArg+7                  // saved col
    asl; asl; asl               // col*8 lo, C = col*8 hi
    bcc ecAddLo                 // col*8 hi = 0?
    inc kPtr+1                  // kPtr_hi += col*8 hi
ecAddLo:
    clc; adc kPtr; sta kPtr     // kPtr_lo += col*8 lo
    bcc ecGlyph                 // no lo-carry?
    inc kPtr+1                  // kPtr_hi += lo-carry
ecGlyph:
    ldy #7
ecRestLp:
    lda GFX_MATRIX_BASE + GFX_CURSOR_GLYPH,y
    sta (kPtr),y
    dey
    bpl ecRestLp
    lda GFX_MATRIX_BASE + GFX_CURSOR_SROW
    tay
    lda gfxCellRowLutLo,y; clc; adc kArg+7; sta kPtr
    lda gfxCellRowLutHi,y; adc #>$0400; sta kPtr+1
    ldy #0
    lda GFX_MATRIX_BASE + GFX_CURSOR_NIB
    sta (kPtr),y                // restore pre-blink matrix nibble
    lda #0; sta cursorVisible
ecRts:
    rts

