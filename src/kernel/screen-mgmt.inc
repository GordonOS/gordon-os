// ==========================================
//  SCREEN MANAGEMENT -- unified bitmap surfaces
// ==========================================
//  Every "screen" is a 10 KB REU bitmap slot. screenKind:
//    0 = none, 2 = hires-text (glyph blits + kPlot), 3 = mc-graphics.
//  The framebuffer ($2000-$3FFF + $0400 matrix + $D800 color) always
//  holds exactly one surface -- the focused one. All others live in
//  their REU slots. cursorState/cursorOn are global (single XOR block
//  on the displayed surface); colors and readString state are per-task
//  arrays (see main.asm).

// --- surfSlotToReu -- compute taskGfxReu* for task X from slot A ---
//  Input:  A = slot (0-27), X = task index
//  Output: taskGfxReuBank/Hi/Lo[x] set; X preserved
//  Clobbers: A, kArg..kArg+3 (kMul16)
surfSlotToReu:
    sta kArg
    lda #0; sta kArg+1
    lda #<$28; sta kArg+2
    lda #>$28; sta kArg+3
    txa; pha                    // save task index
    jsr kMul16                  // kResult = slot * $28
    pla; tax
    lda kResult+1; clc; adc #GFX_SLOT_BASE_BANK; sta taskGfxReuBank,x
    lda kResult;   sta taskGfxReuHi,x
    lda #0;        sta taskGfxReuLo,x
    rts

// --- surfFreeSlot -- free task X's slot (X must own it) ---
//  Clobbers: A, X, Y, kArg..kArg+3
surfFreeSlot:
    lda taskGfxReuBank,x
    sec; sbc #GFX_SLOT_BASE_BANK
    sta kArg+1                  // hi = bank - base
    lda taskGfxReuHi,x
    sta kArg                    // lo = REU hi byte
    lda #<$28; sta kArg+2
    lda #>$28; sta kArg+3
    txa; pha
    jsr kDiv16                  // kResult = slot
    pla; tax
    lda kResult
    jmp gfxSlotFree             // tail-call (clobbers A,X,Y)

// --- surfClearSlot -- blank task X's surface slot ---
//  pixels = 0, matrix = 0, color RAM = taskTextColor[X]
//  Input:  X = task index (X preserved)
//  Clobbers: A, Y, kArg..kArg+4
surfClearSlot:
    lda taskTextColor,x
    pha                         // color nibble on stack
    lda taskGfxReuLo,x; sta kArg
    lda taskGfxReuHi,x; sta kArg+1
    lda taskGfxReuBank,x; sta kArg+2
    lda #<$2000; sta kArg+3
    lda #>$2000; sta kArg+4
    lda #0
    jsr kReuFillReu             // pixels = 0
    lda taskGfxReuLo,x; sta kArg
    lda taskGfxReuHi,x; clc; adc #>$2000; sta kArg+1
    lda taskGfxReuBank,x; adc #0; sta kArg+2
    lda #<$03E8; sta kArg+3
    lda #>$03E8; sta kArg+4     // 1000 bytes -- preserve the slot cursor
                                // tail at +$23e8 (fill over it would kill
                                // cursorOn for a black bg0)
    lda taskGfxBg0,x
    jsr kReuFillReu             // matrix = bg0 (bg nibble), tail preserved
    lda taskGfxReuLo,x; sta kArg
    lda taskGfxReuHi,x; clc; adc #>$2400; sta kArg+1
    lda taskGfxReuBank,x; adc #0; sta kArg+2
    lda #<$0400; sta kArg+3
    lda #>$0400; sta kArg+4
    pla                         // text color nibble
    jsr kReuFillReu             // color RAM = text color
    rts

// --- allocScreen -- allocate a bitmap surface for curTask ---
//  Input:  A = mode (0 = hires-text, 1 = mc-graphics)
//          kArg = border, kArg+1 = bg0, kArg+2 = bg1, kArg+3 = bg2
//  Output: C=0 success (screenKind=2/3, screenAlloc=1, owner=$FF);
//          C=1 no free slot or bad mode
//  Clobbers: A, X, Y, kPtr, kArg..kArg+4
allocScreen:
    sei
    sta kArg+5                  // mode (survives the surface checks below)
    cmp #2
    bcc alModeOk
    sec; cli; rts               // invalid mode
alModeOk:
    ldx curTask
    lda screenAlloc,x
    beq asFresh                 // no surface -- fresh path (asReuse is
                                // beyond branch range below)
    jmp asReuse                 // restored task re-running its entry --
                                // keep the preserved surface
asFresh:
    lda kArg+5                  // mode
    clc
    adc #BMPMODE_STD            // 2=hires-text, 3=mc
    pha                         // screenKind
    lda kArg; pha               // border
    lda kArg+1; pha             // bg0
    lda kArg+2; pha             // bg1
    lda kArg+3; pha             // bg2
    jsr gfxSlotAlloc            // A = slot, C=1 if none
    bcc asSlotOk
    pla; pla; pla; pla; pla     // discard 5
    sec; cli; rts
asSlotOk:
    ldx curTask
    jsr surfSlotToReu           // A=slot, X=curTask -> taskGfxReu* (X preserved)
    pla; sta taskGfxBg2,x
    pla; sta taskGfxBg1,x
    pla; sta taskGfxBg0,x
    pla; sta taskGfxBorder,x
    pla; sta screenKind,x
    lda #1; sta screenAlloc,x
    lda #$ff; sta screenOwner,x
    lda #DEFAULT_TEXT_COLOR;   sta taskTextColor,x
    lda #DEFAULT_CURSOR_COLOR; sta taskCursorColor,x
    jsr surfClearSlot           // X = curTask (preserved)
    // fresh surface -- blank its char-shadow region (bank 0) too: a
    // reused gfx slot otherwise exposes the previous owner's text to
    // kCharAt on cells the new task never printed
    jsr shadowHiOf              // A = shadow page hi for curTask's slot
    sta kArg+1
    lda #0
    sta kArg
    sta kArg+2                  // bank 0
    lda #<$3E8; sta kArg+3
    lda #>$3E8; sta kArg+4      // 1000 bytes
    lda #$20
    jsr kReuFillReu             // shadow = spaces
    lda #0; sta cursorCol
    lda #0; sta cursorRow       // fresh surface prints at home
    lda #1; sta cursorOn        // and shows its cursor
    ldx curTask
    lda screenKind,x
    cmp #BMPMODE_STD
    bne asNoCursor              // mc graphics -- no text cursor
    jsr seedSlotCursorTail      // slot tail = (0,0) + ON -- a reused slot
                                // holds stale tail bytes (surfClearSlot
                                // preserves them) and svRestCurOn would
                                // gate the blink off on a stale ON=0
    lda #0                      // X = curTask (seedSlotCursorTail preserved)
    sta taskScrollTop,x
    lda #SCREEN_LAST_ROW
    sta taskScrollBottom,x
    jsr seedSlotRegion          // slot tail +$23EE = 0/24
asNoCursor:
    clc; cli; rts
asReuse:
    // Pool-eviction restore: the task already owns (or shares) a
    // surface and its REU slot survived the swap -- allocating a fresh
    // one would leak a slot on every restore. Refresh the caller's
    // colors; slot, screenKind and content stay as preserved.
    lda kArg; sta taskGfxBorder,x
    lda kArg+1; sta taskGfxBg0,x
    lda kArg+2; sta taskGfxBg1,x
    lda kArg+3; sta taskGfxBg2,x
    jsr loadSlotRegion          // restore region from slot tail (X=curTask)
    clc; cli; rts

// --- shareScreen -- attach curTask to target X's surface ---
//  Input:  X = target task (must have a surface)
//  Output: C=1 if target has no surface
//  Clobbers: A, Y. X preserved (the target slot) -- a caller holding a
//  task index across this call keeps it.
shareScreen:
    sei
    lda screenAlloc,x
    bne shGo
    sec; cli; rts
shGo:
    ldy curTask
    lda screenAlloc,y
    beq shCopy
    // curTask already owns a surface -- free its slot first
    lda screenOwner,y
    cmp #$ff
    bne shCopy
    tya; pha                    // save curTask
    txa; pha                    // save target
    tya; tax                    // X = curTask (owner)
    jsr surfFreeSlot
    pla; tax                    // restore target
    pla; tay                    // restore curTask
shCopy:
    txa; sta screenOwner,y      // curTask shares target
    lda #1; sta screenAlloc,y
    lda taskGfxReuLo,x; sta taskGfxReuLo,y
    lda taskGfxReuHi,x; sta taskGfxReuHi,y
    lda taskGfxReuBank,x; sta taskGfxReuBank,y
    lda screenKind,x; sta screenKind,y
    lda taskGfxBorder,x; sta taskGfxBorder,y
    lda taskGfxBg0,x; sta taskGfxBg0,y
    lda taskGfxBg1,x; sta taskGfxBg1,y
    lda taskGfxBg2,x; sta taskGfxBg2,y
    lda taskTextColor,x; sta taskTextColor,y
    lda taskCursorColor,x; sta taskCursorColor,y
    lda taskScrollTop,x; sta taskScrollTop,y
    lda taskScrollBottom,x; sta taskScrollBottom,y
    lda #0; sta taskShadowFlag,y   // surface changed -- shadow is stale
    // Seed the global cursor from the framebuffer tail if the shared
    // surface is displayed -- prevents stale cursor from the previous
    // task corrupting the first svCurSave for this sharer.
    lda kbFocus
    cmp #$ff
    beq shDone
    lda screenOwner,y           // = X (just set by shCopy)
    cmp kbFocus                 // X == kbFocus? (common case: ps shares shell)
    bne shDone
    lda GFX_MATRIX_BASE + GFX_CURSOR_COL; sta cursorCol
    lda GFX_MATRIX_BASE + GFX_CURSOR_ROW; sta cursorRow
shDone:
    clc; cli; rts

// --- takeScreenOwnership -- transfer source X's surface to curTask ---
//  Input:  X = source task (must own: screenAlloc=1, screenOwner=$FF)
//  Output: C=1 if source has no owned surface
//  Clobbers: A, Y. X preserved (the source slot)
takeScreenOwnership:
    sei
    lda taskState,x
    beq tsoOk                   // DEAD -> OK
    jmp tsoFail
tsoOk:
    lda screenAlloc,x
    bne tsoHasAlloc
    jmp tsoFail
tsoHasAlloc:
    lda screenOwner,x
    cmp #$ff
    beq tsoHasOwn               // source owns -- continue
    jmp tsoFail                 // source doesn't own -- fail (far: jmp)
tsoHasOwn:
    ldy curTask
    lda screenAlloc,y; beq tsoCopy
    lda screenOwner,y; cmp #$ff; bne tsoCopy
    // taker owns a surface -- free its slot
    tya; pha                    // save curTask
    txa; pha                    // save source
    tya; tax                    // X = curTask (taker)
    jsr surfFreeSlot
    pla; tax                    // restore source
    pla; tay                    // restore curTask
tsoCopy:
    lda taskGfxReuLo,x; sta taskGfxReuLo,y
    lda taskGfxReuHi,x; sta taskGfxReuHi,y
    lda taskGfxReuBank,x; sta taskGfxReuBank,y
    lda screenKind,x; sta screenKind,y
    lda taskGfxBorder,x; sta taskGfxBorder,y
    lda taskGfxBg0,x; sta taskGfxBg0,y
    lda taskGfxBg1,x; sta taskGfxBg1,y
    lda taskGfxBg2,x; sta taskGfxBg2,y
    lda taskTextColor,x; sta taskTextColor,y
    lda taskCursorColor,x; sta taskCursorColor,y
    lda taskScrollTop,x; sta taskScrollTop,y
    lda taskScrollBottom,x; sta taskScrollBottom,y
    lda #1; sta screenAlloc,y
    lda #$ff; sta screenOwner,y
    lda #0; sta taskShadowFlag,y   // surface changed -- shadow is stale
    lda #0; sta screenAlloc,x
    sta screenOwner,x
    sta screenKind,x
    sta taskGfxReuLo,x
    sta taskGfxReuHi,x
    sta taskGfxReuBank,x
    clc; cli; rts
tsoFail:
    sec; cli; rts

// --- setTextColor -- set current surface's text (foreground) color ---
//  Input: A = color (0-15)
//  Clobbers: A, X (X = curTask)
setTextColor:
    ldx curTask
    sta taskTextColor,x
    rts

// --- setCursorColor -- set current surface's cursor color (stored; the
//  XOR block cursor ignores it for now) ---
//  Input: A = color (0-15)
//  Clobbers: A, X (X = curTask)
setCursorColor:
    ldx curTask
    sta taskCursorColor,x
    rts

// --- freeScreen -- release curTask's surface ---
//  If owner, frees the slot + detaches sharers. If sharing, detaches.
freeScreen:
    sei
    ldx curTask
    lda screenAlloc,x
    beq fsDone
    lda screenOwner,x
    cmp #$ff
    bne fsDetach                // sharing, just detach self
    // owner: free slot + detach all sharers
    jsr surfFreeSlot            // X = curTask (clobbers X)
    ldx curTask                 // restore
    ldy #0
fsDetachSh:
    cpy totalTasks; bcs fsDetach
    cpy curTask; beq fsNext     // skip self
    lda screenOwner,y; cmp curTask; bne fsNext
    lda #0; sta screenAlloc,y
    sta screenOwner,y
    sta screenKind,y
    sta taskShadowFlag,y        // shadow follows the surface -- detach clears it
fsNext: iny; bne fsDetachSh
fsDetach:
    ldx curTask
    lda #0
    sta screenAlloc,x
    sta screenOwner,x
    sta screenKind,x
    sta taskShadowFlag,x
fsDone:
    cli
    rts

// --- viewScreen -- switch VIC display to X's surface (kbFocus unchanged) ---
//  Input: X = task index (preserved)
viewScreen:
    lda kbFocus; sta kArg       // fromTask = current focus
    txa; pha                    // save toTask
    jsr doGfxSwitch             // unified surface switch
    pla; tax                    // restore X (toTask)
    clc; rts

// --- switchView -- change what VIC displays + transfer kbFocus ---
//  Input: X = target task index (preserved)
switchView:
    lda kbFocus
    pha                         // save old focus (for doGfxSwitch fromTask)
    php; sei                    // atomic drain + focus set
    cpx kbFocus
    beq svAlreadyFocused        // already displaying it -- nothing to do
    lda #0; sta keybCount
    sta keybHead
    sta keybTail
    stx kbFocus
    plp                         // restore original I flag
    pla; sta kArg               // fromTask = old kbFocus
    txa; pha                    // save toTask (doGfxSwitch clobbers X)
    jsr doGfxSwitch
    pla; tax                    // restore X (toTask)
    clc; rts
svAlreadyFocused:
    plp
    pla                         // discard saved old focus
    clc; rts

// --- setGfxMode -- switch curTask's surface between hires and MC in place ---
//  Input:  A = mode (0 = hires/std, 1 = multicolor)
//  Output: C=0 success (screenKind=2/3; VIC updated iff this surface is
//          displayed). Colours are PRESERVED -- MODE only switches the
//          pixel mode, it does not reset border/bg0/bg1/bg2.
//  Clobbers: A, X, Y, kArg+4 (surfDisplayed scratch)
setGfxMode:
    sei
    tay                         // mode (Y is clobbered per contract)
    cmp #2
    bcc sgmOk
    sec
    cli
    rts
sgmOk:
    ldx curTask
    lda screenAlloc,x
    bne sgmHave
    sec
    cli
    rts
sgmHave:
    tya                         // mode
    clc
    adc #BMPMODE_STD            // 2 or 3
    sta screenKind,x
    jsr surfDisplayed           // C=1 displayed (clobbers A,X,Y)
    bcs sgmApply
    clc
    cli
    rts
sgmApply:
    ldx curTask
    lda screenKind,x
    cmp #BMPMODE_MC
    bne sgmStd
    lda $d016; ora #$10; sta $d016
    bne sgmCols                 // always
sgmStd:
    lda $d016; and #$ef; sta $d016
sgmCols:
    lda taskGfxBorder,x; sta $d020
    lda taskGfxBg0,x;    sta $d021
    lda taskGfxBg1,x;    sta $d022
    lda taskGfxBg2,x;    sta $d023
    clc
    cli
    rts

// --- fillColorRam -- fill the whole colour RAM with a colour ---
//  Input: A = colour (0-15). Colour RAM is 4-bit: the VIC reads the LOW
//  nibble (the MC bitmap value-3 colour), so the raw colour is written
//  -- NOT shifted. Displayed surface -> $D800-$DBFF; background ->
//  slot+$2400. SEI/CLI wrapped. Clobbers A, X, Y, kPtr, kArg.
fillColorRam:
    sei
    sta gfxTemp                 // colour (gfx.inc resident scratch)
    ldx curTask
    jsr surfDisplayed           // C=1 displayed (clobbers A,X,Y)
    bcc fcrSlot
    lda #<$d800; sta kPtr       // displayed -> colour RAM
    lda #>$d800; sta kPtr+1
    lda #<$0400; sta kArg
    lda #>$0400; sta kArg+1
    lda gfxTemp
    jsr kReuFillC64
    cli; rts
fcrSlot:
    ldx curTask
    lda taskGfxReuLo,x          // background -> slot+$2400
    sta kArg
    lda taskGfxReuHi,x
    clc
    adc #>$2400
    sta kArg+1
    lda taskGfxReuBank,x
    adc #0; sta kArg+2
    lda #<$0400; sta kArg+3
    lda #>$0400; sta kArg+4
    lda gfxTemp
    jsr kReuFillReu
    cli; rts
