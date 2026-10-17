// ==========================================
//  SCREEN I/O
// ==========================================

// --- clearScreen -- clear current task's surface, cursor home ---
clearScreen:
    sei                         // atomic: an F-key display switch must
                                // not land between the framebuffer fills
                                // (they'd wipe the incoming surface)
    lda cursorOn; pha
    lda #0; sta cursorOn
    ldx curTask
    jsr surfDisplayed
    bcc csBg
    lda #0
    sta cursorVisible           // phase reset: the fills wipe the XOR
                                // block, so the blink's saved copy is
                                // stale -- next blink must be a fresh ON
    // focused: clear framebuffer pixels + video matrix (hires color)
    lda #<$2000; sta kPtr
    lda #>$2000; sta kPtr+1
    lda #<$2000; sta kArg       // 8192 bytes
    lda #>$2000; sta kArg+1
    lda #0
    jsr kReuFillC64             // pixels -> 0
    lda #GFX_MATRIX_BASE_LO; sta kPtr
    lda #GFX_MATRIX_BASE_HI; sta kPtr+1
    lda #<$03E8; sta kArg
    lda #>$03E8; sta kArg+1     // 1000 bytes -- preserve the cursor tail at
                                // $07e8-$07f7: filling it with bg0 ($00) would
                                // kill the blink until a print re-enables it
    ldx curTask
    lda taskGfxBg0,x
    jsr kReuFillC64             // matrix -> bg0 (bg nibble), tail preserved
    lda #0
    sta GFX_MATRIX_BASE + GFX_CURSOR_COL   // mirror the home below -- the
    sta GFX_MATRIX_BASE + GFX_CURSOR_ROW   // tail otherwise lags one save
                                            // and the blink draws at the
                                            // stale cell meanwhile
    jmp csHome
csBg:
    ldx curTask                 // surfDisplayed clobbered X
    jsr surfClearSlot           // background -> clear slot (X=task)
csHome:
    lda #0; sta cursorCol; sta cursorRow
    ldx curTask
    lda taskShadowFlag,x
    beq csNoShadow
    lda #0; sta kArg
    lda taskShadowHi,x; sta kArg+1
    lda #0; sta kArg+2
    lda #<$3E8; sta kArg+3
    lda #>$3E8; sta kArg+4
    lda #$20
    jsr kReuFillReu             // shadow = spaces (X preserved)
csNoShadow:
    pla; sta cursorOn
    cli
    rts

// --- surfDisplayed -- C=1 iff task X's surface is the displayed one ---
//  all-bitmap: the framebuffer-vs-slot decision keys on the SURFACE,
//  not keyboard focus. resolve(t) = (screenOwner[t] == $FF) ? t :
//  screenOwner[t] -- the task that actually renders the surface t uses.
//  Task X's output is visible iff resolve(X) == resolve(kbFocus) and
//  kbFocus != $FF; otherwise it lands in X's REU slot (shown after a
//  surfLoad on view-switch).
//  Input:  X = task index
//  Output: C=1 displayed (framebuffer), C=0 background (REU slot)
//  Clobbers: A, X, Y. Callers that still need curTask reload it.
//  WARNING: never write the kArg block here. The scheduler calls this from the
//  timer IRQ on every switch, and sysExit calls it while another task is parked
//  in a syscall, so a kArg byte would destroy that task's live kArg. It keeps
//  resolve(kbFocus) in its own byte (surfResolved) instead; an IRQ landing
//  between the store and the compare can only mis-state THIS call's answer.
surfDisplayed:
    ldy kbFocus
    cpy #$ff
    beq sdNotDisplayed          // blank display -- nothing is displayed
    lda screenOwner,y
    cmp #$ff
    beq sdYOwn
    tay                         // Y = resolve(kbFocus)
sdYOwn:
    tya
    sta surfResolved            // resolve(kbFocus)
    lda screenOwner,x
    cmp #$ff
    beq sdXOwn
    tax                         // X = resolve(curTask)
sdXOwn:
    txa
    cmp surfResolved            // Z=1 iff resolve(X) == resolve(kbFocus)
    beq sdDisplayed             // equal -> displayed
sdNotDisplayed:
    clc                         // C=0 -> background (REU slot / blank)
    rts
sdDisplayed:
    sec                         // C=1 -> displayed (framebuffer)
    rts

// --- printChar -- print A (screen code) at cursor, advance ---
//  Disables cursor during write so IRQ blink can't overwrite.
//  Bitmap blitter path (all-bitmap): glyph blit to the framebuffer when
//  this task's surface is displayed, else to its REU slot (section 3.8 --
//  surfDisplayed keys on the surface, not keyboard focus). Color nibble
//  written too.
printChar:
    sei                         // atomic vs display switches: a swap
                                // mid-blit would write one cell (and the
                                // cursor un-XOR) onto the incoming surface
    sta kArg+6                  // char
    ldx curTask
    lda taskShadowFlag,x
    beq pcNoShadow
    jsr pcShadowChar            // opt-in char-shadow mirror (SEI held)
pcNoShadow:
    lda cursorOn
    pha
    lda #0
    sta cursorOn
    ldx curTask
    lda taskTextColor,x
    sta kArg+5                  // color nibble
    ldx curTask
    jsr surfDisplayed
    bcs pcFocused               // shared surface is displayed -> framebuffer
    // background surface -> blit glyph + color to REU slot
    lda kArg+6
    ldx cursorCol
    ldy cursorRow
    jsr blitCharSlot
    jmp pcAdvance
pcFocused:
    // Erase the blink cursor if it's ON, so it can't strand a phantom:
    // eraseCursor restores the saved glyph + matrix byte at the blink's
    // SAVED position, never the live cursor.
    jsr eraseCursor
pcNoCur:
    // displayed surface -> CPU glyph blit to framebuffer
    lda kArg+6
    ldx cursorCol
    ldy cursorRow
    jsr blitChar
    // video-matrix cell (hires color) = $0400 + row*40 + col
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
    and #$0F                    // preserve background (low nibble)
    pha                         // bg nibble on stack (no kArg+7 clobber --
                                // callers like pcDir stash values there)
    ldx curTask
    lda taskTextColor,x         // font DMA clobbers kArg+5
    asl
    asl
    asl
    asl                         // << 4 -> foreground (high nibble)
    tsx
    ora $0101,x                 // combine with pushed bg nibble
    sta (kPtr),y                // matrix byte = (fg<<4) | bg
    pla                         // balance stack
pcAdvance:
    inc cursorCol
    lda cursorCol
    cmp #SCREEN_COLS
    bcc pcWrapDone
    lda #0
    sta cursorCol
    inc cursorRow
    jsr wrapClamp               // region clamp + conditional scroll
pcWrapDone:
    pla
    sta cursorOn
    cli
    rts

// --- printGlyph -- print a custom 8-byte glyph at (kPtr), advance cursor ---
//  Like printChar, but the glyph source is the C64 buffer at kPtr/kPtr+1
//  (8 bytes) instead of the font indexed by a screen code. Mirrors a
//  SPACE ($20) into the char shadow (if enabled) so read-back shows a
//  blank rather than a stale cell. Text colour, cursor, wrap and scroll
//  behaviour match printChar.
printGlyph:
    sei
    ldx curTask
    lda taskShadowFlag,x
    beq pgNoShadow
    lda #$20
    sta kArg+6                  // shadow char = space
    jsr pcShadowChar            // opt-in char-shadow mirror (SEI held)
pgNoShadow:
    lda cursorOn
    pha
    lda #0
    sta cursorOn
    ldx curTask
    lda taskTextColor,x
    sta kArg+5                  // color nibble
    lda kPtr
    sta srcPtr
    lda kPtr+1
    sta srcPtr+1                // srcPtr = glyph bytes
    ldx curTask
    jsr surfDisplayed
    bcs pgFocused
    // background surface -> blit glyph + color to REU slot
    ldx cursorCol
    ldy cursorRow
    jsr blitGlyphSlot
    jmp pgAdvance
pgFocused:
    jsr eraseCursor
    ldx cursorCol
    ldy cursorRow
    jsr blitGlyphFb
    // video-matrix cell (hires color) = $0400 + row*40 + col
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
    and #$0F                    // preserve background (low nibble)
    pha
    lda kArg+5                  // text color
    asl
    asl
    asl
    asl
    tsx
    ora $0101,x
    sta (kPtr),y
    pla
pgAdvance:
    inc cursorCol
    lda cursorCol
    cmp #SCREEN_COLS
    bcc pgWrapDone
    lda #0
    sta cursorCol
    inc cursorRow
    jsr wrapClamp               // region clamp + conditional scroll
pgWrapDone:
    pla
    sta cursorOn
    cli
    rts

// --- printString -- print null-terminated string at (kPtr) ---
//  kPtr is consumed (advanced past terminator).
//  Cursor disabled for entire string to prevent IRQ blink artifacts.
printString:
    lda cursorOn
    pha
    lda #0
    sta cursorOn
    ldy #0
psLoop:
    lda (kPtr),y
    beq psDone                  // null terminator
    sta kArg                    // save char (kPtr clobbered by printChar)
    tya
    pha                         // save Y
    lda kPtr
    pha
    lda kPtr+1
    pha                         // save kPtr
    lda kArg                    // char -> A for printChar
    jsr printChar
    pla
    sta kPtr+1
    pla
    sta kPtr                    // restore kPtr
    pla
    tay                         // restore Y
    iny
    bne psLoop
psDone:
    // advance kPtr past terminator
    tya
    clc
    adc kPtr
    sta kPtr
    bcc psRts
    inc kPtr+1
psRts:
    pla
    sta cursorOn
    rts

// --- setCursor -- X = column (0-39), Y = row (0-24) ---
setCursor:
    stx cursorCol
    sty cursorRow
    rts

// --- getCursor -- returns column in X, row in Y ---
getCursor:
    ldx cursorCol
    ldy cursorRow
    rts

// --- getCursorOn -- returns cursorOn in A (A-only; X/Y preserved) ---
getCursorOn:
    lda cursorOn
    rts

// --- newline -- cursor to col 0, next row; scroll if at bottom ---
newline:
    sei                         // atomic: the inc->clamp window must not
                                // expose a transient bottom+1 row to the
                                // scheduler's cursor save on shared surfaces
    lda cursorOn
    pha
    lda #0
    sta cursorOn
    sta cursorCol               // A still 0 -- saves a byte, keeps size net-zero
    inc cursorRow
    jsr wrapClamp               // region clamp + conditional scroll
nlDone:
    pla
    sta cursorOn
    cli
    rts

// --- enableCursor / disableCursor ---
//  All-bitmap: cursorOn is per-surface. Sets the running task's copy AND
//  syncs it to the surface's matrix tail immediately (framebuffer tail
//  when the surface is displayed, else slot+$23ea via 1-byte REU DMA) --
//  a lazy scheduler sync could otherwise resurrect the seeded ON=1 tail
//  for a task that disables its cursor in the same slice as kSwitchView,
//  leaving a stray (0,0) cursor. The sync body lives in gfx.inc
//  (csrSyncTail): this flow has no spare bytes.
enableCursor:
    lda #1
    bne csrBoth
disableCursor:
    lda #0
csrBoth:                        // A = new cursorOn value
    sta cursorOn
    jmp csrSyncTail             // tail-jmp -- rts returns to our caller

// --- scrollScreen -- scroll up one text row (dispatcher) ---
//  Displayed surface -> scrollScreenFocused (framebuffer region);
//  background -> scrollScreenSlot (REU slot; the stub no-ops).
//  Owns the SEI + cursorOn; the sub-routines run with cursorOn = 0 and
//  must NOT cli or rts at top level.
scrollScreen:
    sei                         // atomic vs F-key display switches -- a
                                // swap between the DMAs made the bottom-
                                // row clears hit the INCOMING surface
    lda cursorOn
    pha
    lda #0
    sta cursorOn
    ldx curTask
    lda screenAlloc,x
    beq ssDispDone              // no surface -- nothing to scroll
    jsr surfDisplayed
    bcs ssDispFocused
    jsr scrollScreenSlot        // background -> REU slot (reloads X itself)
    jmp ssDispDone
ssDispFocused:
    jsr scrollScreenFocused     // displayed -> framebuffer region
ssDispDone:
    pla
    sta cursorOn
    cli
    rts

// --- cursorAddr -- kPtr = framebuffer cell at cursor ---
//  Cell = $2000 + (row*40 + col)*8 (8 contiguous glyph bytes).
//  X and Y preserved (contract: kCursorAddr).
cursorAddr:
    lda cursorRow
    jsr rowAddr                 // kPtr = $2000 + row*320
    lda cursorCol
    asl
    asl
    asl                         // col*8 lo, C = col*8 hi
    bcc caLo                    // col*8 hi = 0?
    inc kPtr+1                  // kPtr_hi += col*8 hi
caLo:
    clc
    adc kPtr                    // kPtr_lo += col*8 lo
    sta kPtr
    bcc caDone                  // no lo-carry?
    inc kPtr+1                  // kPtr_hi += lo-carry
caDone:
    rts

// --- rowAddr -- kPtr = $2000 + A*320 (pixel row start) ---
//  X and Y preserved; clobbers A and kPtr.
rowAddr:
    sta kPtr                    // stash row (kPtr reused below as result)
    tya
    pha                         // save Y
    ldy kPtr                    // Y = row
    lda gfxRowLutLo,y           // $2000 + row*320
    sta kPtr
    lda gfxRowLutHi,y
    sta kPtr+1
    pla
    tay                         // restore Y
    rts

