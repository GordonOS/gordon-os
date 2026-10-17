// ==========================================
//  KEYBOARD
// ==========================================

// --- kbPoll -- call from IRQ; scans hardware, pushes to buffer ---
kbPoll:
    lda kPtr
    pha
    lda kPtr+1
    pha                         // save task's kPtr

    // pre-check shift state (single source of truth for shifted chars).
    // scanKeyboardHw masks the shift keys out, so this flag is the ONLY
    // thing that decides shifted vs. unshifted below.
    lda #0; sta keybShift
    lda #$fd; sta $dc00         // PA1 low (LSHIFT column)
    lda $dc01; lda $dc01; and #$80; beq kbShSet   // LSHIFT = PB7 ($80)
    lda #$bf; sta $dc00         // PA6 low (RSHIFT column)
    lda $dc01; lda $dc01; and #$10; bne kbShDone   // RSHIFT = PB4 ($10), NOT PB3
kbShSet:
    lda #1; sta keybShift
kbShDone:

    jsr scanKeyboardHw          // A = keyTable[x] or $ff; X = index; shifts masked out
    cmp #$ff
    beq kbNotMod                // no key -> handle release/idle
    cmp #$00
    beq kbMaybeFKey             // $00 -> F-key or unmapped key
    // regular character key -- apply shift modifier if held
    ldy keybShift
    beq kbNotMod                // no shift -> A already holds keyTable[x]
    lda keyTableShift,x         // shift held -> shifted screen code
    jmp kbNotMod

kbMaybeFKey:
    // F-keys map to $00 in keyTable; identify them by index
    // (1=F5, 2=F3, 3=F1, 4=F7). Everything else that maps to $00
    // (e.g. @, C=, CTRL) is passed through unchanged.
    cpx #5
    bcc kbFKey                  // index 1-4 = F-key (index 0 never maps to $00)
    lda #$00                    // not an F-key -> pass $00 through
    jmp kbNotMod

kbFKey:
    // F-key screen switch. Base screen from table; shift selects base+1.
    //  WARNING: NEVER write the screen number into `keybLast`. `keybLast` holds a KEY
    //  CODE, and the screen numbers 0-7 collide with the screen codes for
    //  `@ A B C D E F G`: after an F-key switch, pressing that one key is read
    //  as "same key still held", so the press is swallowed (keybTimer == 0) or
    //  answered with a stale repeat of the previous key.
    //  No dedup is needed here: `kbSwitchToScreen` ends in its own
    //  `cpy kbFocus / beq kbsDone` guard, so a held F-key costs one scan per
    //  tick and changes nothing, and a fresh press re-asserts the screen
    //  (idempotent through that guard) rather than trusting a stale byte.
    lda fKeyToScreen-1,x
    clc
    adc keybShift               // shift held (1) -> shifted screen; else base
    //  --- RECORDED, not applied. The switch itself is a display change that runs
    //      from the scheduler's switch path, at the one point in the tick where the
    //      running task's kArg is already saved and the incoming task's is not yet
    //      put back -- see kbScreenGate. This scan only decides WHICH screen. ---
    sta kbScreenReq
    jmp kbDone
    //  ADDRESS-STABILITY SLACK -- same trick as kernel-vars' 16B `hexChars`
    //  block. This block's SIZE is what holds the band still: the addresses after
    //  it are pinned ABI -- `keyrpt.com` pokes `keybRepeatLda`/`keybInitialLda`,
    //  and gfx.lib's `ffStk*`/`kCircle*`/`kEll*` scratch addresses sit above the
    //  band, each one asserted in main.asm. The F-key branch above records the
    //  screen instead of switching it, which is one byte shorter, and this sled
    //  takes that byte back.
    //  Nothing reaches this run (the `jmp kbDone` above never falls into it) and
    //  `$ea` is a NOP -- `$00` would be a BRK -- so kernel data bytes ride here
    //  for free.
    .fill 7, $ea

// --- mouseKind -- the pointer device this machine has ---
//  0 = off, 1 = 1351. The `mouse` command is the only writer; the task that
//  talks to the device is the reader. mouse.lib reads the hardware, not this
//  byte, so a machine the OS believes has no mouse can still be diagnosed.
//  `0` is the real machine, so a warm reboot needs no repair.
mouseKind:
    .byte 0

// --- sysSpeedShift -- the machine's speed relative to real time ---
//  Signed exponent: +1 = the whole machine (CPU, VIC, SID) runs at 200% of
//  real time, 0 = 100%, -1 = 50%, +2 = 400%. `speed.com` is the only
//  writer, and it accepts only powers of two -- the pitch compensation is a
//  whole octave and the rest are shifts, so an in-between speed could not be
//  compensated exactly.
//  Readers: the kernel's blink (blinkRate) and key-repeat immediates, which
//  `speed` sets from BLINK_RATE/KEYB_INITIAL/KEYB_REPEAT; sid.lib, which
//  shifts the tune's pitch and its ticks-per-jiffy delay by it; and a task
//  that paces itself in displayed frames, which shifts its own durations by
//  it. `0` is the real machine, so a warm reboot needs no repair.
sysSpeedShift:
    .byte 0

// --- fKeyToScreen: keyTable index -> base screen (shift adds 1) ---
//  F1->0, Shift+F1->1, F3->2, Shift+F3->3, F5->4, Shift+F5->5, F7->6, Shift+F7->7
fKeyToScreen:
    .byte 4, 2, 0, 6           // index 1(F5)->4, 2(F3)->2, 3(F1)->0, 4(F7)->6

kbNotMod:
    cmp keybLast
    bne kbChanged               // key changed -> handle transition
    // same key as last tick
    cmp #$ff
    beq kbDone                  // still no key, nothing to do
    // key still held -- handle repeat
    lda keybTimer
    beq kbDone                  // timer not running (shouldn't happen)
    dec keybTimer
    bne kbDone                  // not zero yet
keybRepeatLda:
    lda #KEYB_REPEAT
    sta keybTimer               // reset to repeat rate
    lda keybHeld                // re-push the key
    jmp kbPush
kbChanged:
    sta keybLast                // update last-seen
    cmp #$ff
    beq kbReleased
    // key pressed -- push immediately, start initial delay
    sta keybHeld
keybInitialLda:
    lda #KEYB_INITIAL
    sta keybTimer
    lda keybHeld
    jmp kbPush
kbReleased:
    lda #$ff
    sta keybHeld
    lda #0
    sta keybTimer
    lda #$ff                    // push release event
    // fall through to kbPush
kbPush:
    ldx keybCount
    cpx #16
    bcs kbDone                  // buffer full
    ldy keybHead
    sta keybBuffer,y
    iny
    tya
    and #$0f
    tay
    sty keybHead
    inc keybCount
kbDone:
    // cursor blink -- toggle every BLINK_RATE ticks
    dec blinkTick
    beq kbDoBlink
    lda keybHeld
    cmp #$ff
    bne kbHeldBlink             // key held -> keep cursor ON now
    jmp kbRts                   // no key held -> idle
kbHeldBlink:
    lda cursorVisible
    beq kbDoBlink               // held but OFF -> redraw ON immediately
    jmp kbRts                   // held and ON -> stay solid
kbDoBlink:
    // Block cursor with save/restore: ON saves the cursor POSITION
    // ($07EC/$07ED), the cell's 8 glyph bytes ($07F0-$07F7) and its
    // matrix byte ($07EB) -- all in the framebuffer matrix tail -- then
    // XORs the cell and writes the cursor-color nibble. OFF restores
    // everything at the SAVED position, never at the live cursor, so a
    // cursor that moved between phases can't strand phantoms or recolor
    // characters.
    ldx curTask
    cpx kbFocus
    bne kbTailOn
    lda cursorOn                // focused task running -> LIVE value
    jmp kbOnCheck               // ($07ea lags one tick and would let the
                                //  blink fire mid-print)
kbTailOn:
    lda GFX_MATRIX_BASE + GFX_CURSOR_ON   // displayed surface's owner not running
kbOnCheck:
    bne kbBlinkOn               // cursor enabled -> proceed
    lda #1
    sta blinkTick               // retry next tick (no 256-tick underflow)
    jmp kbRts                   // cursor disabled (kbRts too far to branch)
kbBlinkOn:
    lda kbFocus
    cmp #$ff
    bne kbBlinkGo
    lda #1
    sta blinkTick               // retry next tick
    jmp kbRts                   // no focused surface -- no cursor
kbBlinkGo:
    lda blinkRate
    sta blinkTick
    lda keybHeld
    cmp #$ff
    beq kbBlinkTog              // no key held -> normal blink toggle
    lda cursorVisible
    beq kbBlinkTog              // key held but cursor OFF -> turn it ON
    jmp kbRts                   // key held and cursor ON -> keep it solid
kbBlinkTog:
    lda cursorVisible
    eor #1
    sta cursorVisible
    // kPtr = framebuffer cell at cursor
    ldx curTask
    cpx kbFocus
    beq kbLive                  // running task owns display -> live cursor
    ldx kbFocus                 // X = displayed task (for cursor color)
    lda GFX_MATRIX_BASE + GFX_CURSOR_ROW   // displayed surface's saved row (matrix tail)
    pha                         // save row for matrix calc
    jsr rowAddr                 // kPtr = $2000 + row*320 (X,Y preserved)
    lda GFX_MATRIX_BASE + GFX_CURSOR_COL   // displayed surface's saved col
    jmp kbGo
kbLive:
    lda cursorRow
    pha                         // save row for matrix calc
    jsr rowAddr                 // kPtr = $2000 + row*320 (X,Y preserved)
    lda cursorCol
kbGo:
    sta kbTemp                  // col
    asl
    asl
    asl                         // A = col*8 lo, C = col*8 hi
    bcc kbGoLo                  // col*8 hi = 0?
    inc kPtr+1                  // kPtr_hi += col*8 hi
kbGoLo:
    clc
    adc kPtr
    sta kPtr                    // kPtr_lo += col*8 lo
    bcc kbGoHi                  // no lo-carry?
    inc kPtr+1                  // kPtr_hi += lo-carry
kbGoHi:
    // tmpPtr = matrix cell ($0400 + row*40 + col; IRQ-owned ZP)
    pla
    tay
    lda gfxCellRowLutLo,y
    clc
    adc kbTemp
    sta tmpPtr
    lda gfxCellRowLutHi,y
    adc #>$0400
    sta tmpPtr+1
    lda cursorVisible
    beq kbRestore               // OFF phase -> restore at SAVED position
    // --- ON: save position + cell + matrix nibble, then draw block ---
    lda kbTemp
    sta GFX_MATRIX_BASE + GFX_CURSOR_SCOL  // saved col
    sty GFX_MATRIX_BASE + GFX_CURSOR_SROW  // saved row (Y still holds it)
    ldy #7
kbSaveLp:
    lda (kPtr),y
    sta GFX_MATRIX_BASE + GFX_CURSOR_GLYPH,y
    dey
    bpl kbSaveLp
    ldy #0
    lda (tmpPtr),y
    sta GFX_MATRIX_BASE + GFX_CURSOR_NIB   // saved matrix byte
    jmp kbDraw
kbRestore:
    // --- OFF: recompute the SAVED cell, restore glyph + matrix there ---
    lda GFX_MATRIX_BASE + GFX_CURSOR_SROW  // saved row
    jsr rowAddr                 // kPtr = $2000 + row*320 (X,Y preserved)
    lda GFX_MATRIX_BASE + GFX_CURSOR_SCOL  // saved col
    sta kbTemp
    asl
    asl
    asl                         // A = col*8 lo, C = col*8 hi
    bcc kbRestLo                // col*8 hi = 0?
    inc kPtr+1                  // kPtr_hi += col*8 hi
kbRestLo:
    clc
    adc kPtr
    sta kPtr                    // kPtr_lo += col*8 lo
    bcc kbRestHi                // no lo-carry?
    inc kPtr+1                  // kPtr_hi += lo-carry
kbRestHi:
    ldy GFX_MATRIX_BASE + GFX_CURSOR_SROW
    lda gfxCellRowLutLo,y
    clc
    adc kbTemp
    sta tmpPtr
    lda gfxCellRowLutHi,y
    adc #>$0400
    sta tmpPtr+1
    ldy #7
kbRestLp:
    lda GFX_MATRIX_BASE + GFX_CURSOR_GLYPH,y
    sta (kPtr),y
    dey
    bpl kbRestLp
    ldy #0
    lda GFX_MATRIX_BASE + GFX_CURSOR_NIB
    sta (tmpPtr),y
    jmp kbRts
kbDraw:
    // XOR the 8 contiguous glyph bytes
    ldy #7
kbXorLoop:
    lda (kPtr),y
    eor #$ff
    sta (kPtr),y
    dey
    bpl kbXorLoop
    // matrix byte: (cursorColor<<4) | old bg nibble
    ldy #0
    lda (tmpPtr),y
    and #$0f                    // preserve bg nibble
    sta kbTemp                  // col is consumed
    lda taskCursorColor,x       // X = curTask (live) / kbFocus (tail)
    asl
    asl
    asl
    asl                         // cursor color << 4
    ora kbTemp
    sta (tmpPtr),y
kbRts:
    pla
    sta kPtr+1
    pla
    sta kPtr                    // restore task's kPtr
    rts

// --- kScanKeyboard -- pop from buffer, returns key or $FF ---
//  Returns $FF if current task does not own keyboard focus.
scanKeyboard:
    ldx curTask
    cpx kbFocus
    bne skNotFocused            // not our keyboard, return $FF
    lda keybCount
    beq skEmpty
    dec keybCount
    ldy keybTail
    lda keybBuffer,y
    iny
    cpy #16
    bcc skWrap
    ldy #0
skWrap:
    sty keybTail
    rts
skEmpty:
    lda #$ff
    rts
skNotFocused:
    lda #$ff
    rts

// --- readKey -- pop from buffer, 0 if empty or release ---
readKey:
    jsr scanKeyboard
    cmp #$ff
    bne rkDone
    lda #0
rkDone:
    rts

// --- waitKey -- block until keypress ---
waitKey:
    jsr scanKeyboard
    cmp #$ff
    beq waitKey                 // empty or release, keep waiting
    rts

// --- scanKeyboardHw -- raw hardware scan ---
scanKeyboardHw:
    ldx #7
skCol:
    lda colMask,x
    sta $dc00
    nop                         // CIA propagation delay
    lda $dc01
    lda $dc01                   // real read
    // Mask out the SHIFT keys so the scan never returns a shift itself.
    // Shift state is read separately in kbPoll (keybShift); masking here
    // lets a single downstream path handle shifted chars + F-keys, and
    // also lets keys sharing a shift's column register correctly.
    cpx #1
    bne skNotLShift
    ora #%10000000              // LSHIFT = col 1, PB7 -> force "not pressed"
skNotLShift:
    cpx #6
    bne skRead
    ora #%00010000              // RSHIFT = col 6, PB4 -> force "not pressed"
skRead:
    cmp #$ff
    bne skFound
    dex
    bpl skCol
    lda #$ff
    rts
skFound:
    ldy #7
skRow:
    lsr
    bcc skGotRow
    dey
    bpl skRow
    lda #$ff
    rts
skGotRow:
    // rowx8 + col -> X, using stack as temp (no ZP conflict)
    txa
    asl
    asl
    asl                         // row x 8
    sta kbTemp                  // temp in IRQ-only ZP
    tya
    clc
    adc kbTemp
    tax
    lda keyTable,x
    rts

// Column select masks (PA0-7 output to keyboard matrix)
colMask:
    .byte %11111110, %11111101, %11111011, %11110111
    .byte %11101111, %11011111, %10111111, %01111111

// keyTable -- 64 entries (rowx8 + col), screen codes. $80+ = special.
// C64 matrix: col via PA ($DC00), row via PB ($DC01)
keyTable:
    // Row 0 (PB0): CRSRv  F5     F3     F1     F7     CRSR->  RETURN DEL
    .byte $84, $00, $00, $00, $00, $86, $81, $80
    // Row 1 (PB1): LSHIFT E      S      Z      4      A      W      3
    .byte $00, $05, $13, $1a, $34, $01, $17, $33
    // Row 2 (PB2): X      T      F      C      6      D      R      5
    .byte $18, $14, $06, $03, $36, $04, $12, $35
    // Row 3 (PB3): V      U      H      B      8      G      Y      7
    .byte $16, $15, $08, $02, $38, $07, $19, $37
    // Row 4 (PB4): N      O      K      M      0      J      I      9
    .byte $0e, $0f, $0b, $0d, $30, $0a, $09, $39
    // Row 5 (PB5): ,      @      :      .      -      L      P      +
    .byte $2c, $00, $3a, $2e, $2d, $0c, $10, $2b
    // Row 6 (PB6): /      ^      =      RSHIFT HOME   ;      *      POUND
    .byte $2f, $1e, $3d, $00, $82, $3b, $2a, $1c
    // Row 7 (PB7): STOP   Q      C=     SPACE  2      CTRL   <-      1
    .byte $83, $11, $00, $20, $32, $00, $1f, $31

// keyTableShift -- shifted screen codes for each matrix position
//  Letters: unshifted $01-$1A -> shifted $41-$5A (uppercase)
//  Numbers: shifted symbols (! " # $ % & ' ( ) )
//  Symbols: shifted alternates (< > ? [ ] etc.)
//  Special keys ($80+): unchanged
keyTableShift:
    // Row 0: CRSRv  F5     F3     F1     F7     CRSR->  RETURN DEL
    .byte $85, $00, $00, $00, $00, $87, $81, $88
    // Row 1: LSHIFT E      S      Z      4      A      W      3
    .byte $00, $45, $53, $5a, $24, $41, $57, $23
    // Row 2: X      T      F      C      6      D      R      5
    .byte $58, $54, $46, $43, $26, $44, $52, $25
    // Row 3: V      U      H      B      8      G      Y      7
    .byte $56, $55, $48, $42, $28, $47, $59, $27
    // Row 4: N      O      K      M      0      J      I      9
    .byte $4e, $4f, $4b, $4d, $30, $4a, $49, $29
    // Row 5: ,      @      :      .      -      L      P      +
    .byte $3c, $00, $1b, $3e, $2d, $4c, $50, $2b
    // Row 6: /      ^      =      RSHIFT HOME   ;      *      POUND
    .byte $3f, $1e, $3d, $00, $13, $1d, $2a, $1c
    // Row 7: STOP   Q      C=     SPACE  2      CTRL   <-      1
    .byte $83, $51, $00, $20, $22, $00, $1f, $21

// --- kbSwitchToScreen -- switch VIC display to the Nth owned screen ---
//  Input:  X = screen number (0-based)
//  Output: --
//  Clobbers: A, X, Y
kbSwitchToScreen:
    ldy #1                     // slots 1..MAX_TASKS-1 only; slot 0 (boot) not used for spawn
kbsLoop:
    cpy totalTasks; bcs kbsDone
    lda screenAlloc,y; beq kbsNext
    lda screenOwner,y; cmp #$ff; bne kbsNext
    cpx #0; beq kbsFound
    dex
kbsNext: iny; bne kbsLoop
kbsDone: rts
kbsFound:
    cpy kbFocus
    beq kbsDone                 // already displayed -- nothing to switch
    tya; tax; jsr switchView    // sets cursorVisible & blinkTick internally
    rts


// ==========================================
readString:
    ldy curTask
    cpy kbFocus
    beq rsGo                    // we own the keyboard
    ldy #0; rts                 // not focused, return empty
rsGo:
    stx readStrMax              // save maxLen (kPrintChar clobbers X)
    lda kPtr; sta readStrBuf    // save buffer pointer (kPtr clobbered by other tasks)
    lda kPtr+1; sta readStrBuf+1
    lda #0; sta readStrLen      // length = 0
    sta readStrPos              // position = 0
    jsr kGetCursor              // X=col, Y=row
    sty readStrStartRow         // save start row for HOME
    jmp rsLoop
rsLoop:
    ldy curTask
    cpy kbFocus
    beq rsFocused               // still have focus -> continue
    jmp rsLost                  // lost focus while waiting -- return empty
rsFocused:
    jsr kScanKeyboard
    cmp #$ff; beq rsLoop        // no key yet
    sta readStrChar             // save key
    // Restore char AND original color under the cursor BEFORE dispatching.
    // Block the blink IRQ so it can't redraw the cursor mid-restore.
    // php/plp preserves the original I flag in case the caller already
    // had IRQs disabled (e.g., from another kernel path).
    php; sei
    ldx cursorOn
    txa
    pha                         // old cursorOn on stack
    lda #0; sta cursorOn
    // Redraw the char under the cursor via blitChar -- overwrites any XOR
    // cursor block and restores the clean glyph (or space if past end).
    lda readStrBuf; sta kPtr
    lda readStrBuf+1; sta kPtr+1
    ldy readStrPos; cpy readStrLen
    lda #$20                    // default: space (cursor at end)
    bcs rsEraseStore            // pos >= len -> use space
    lda (kPtr),y                // get actual char from buffer
rsEraseStore:
    pha                         // char on stack (above old cursorOn)
    lda cursorCol
    tax
    ldy cursorRow
    pla                         // char
    jsr blitChar                // X preserved (=col)
    // Restore the matrix nibble under the cursor: the blink left the fg
    // nibble at the cursor color, so re-apply the task's text color
    // (fg<<4) over the preserved bg nibble. (sei held -- blink can't race.)
    ldx curTask
    lda taskTextColor,x
    asl
    asl
    asl
    asl                         // text color << 4
    sta kArg+7
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
    and #$0f                    // preserve bg nibble
    ora kArg+7
    sta (kPtr),y                // matrix byte = (textColor<<4) | bg
    pla                         // old cursorOn
    sta cursorOn
    // Hide the cursor state so the blink redraws fresh at the (possibly
    // moved) position.
    lda #0; sta cursorVisible
    lda #0; sta blinkTick
    plp                         // restore original interrupt state
    lda readStrChar
    // cursor key dispatch (codes $82-$87)
    cmp #$82; bne rsNotHome; jmp rsHome   // HOME
rsNotHome:
    cmp #$84; bne rsNotDown; jmp rsNoop   // v -- no-op
rsNotDown:
    cmp #$85; bne rsNotUp; jmp rsNoop     // ^ -- no-op
rsNotUp:
    cmp #$86; bne rsNotRight; jmp rsRight // ->
rsNotRight:
    cmp #$87; bne rsNotLeft; jmp rsLeft   // <-
rsNotLeft:
    // existing dispatch
    cmp #$81; bne rsNotRet; jmp rsReturn  // RETURN
rsNotRet:
    cmp #$80; bne rsNotDel; jmp rsDel     // DEL
rsNotDel:
    cmp #$80                    // $80+ = special (STOP $83, etc.) -- never echo
    bcc rsCharOk
    jmp rsNoop
rsCharOk:
    // regular char -- write at readStrPos, overwrite if mid-line
    ldy readStrPos; cpy readStrMax; bcc rsRoom  // room available
    jmp rsNoop                                   // at max, no room
rsRoom:
    lda readStrBuf; sta kPtr    // restore kPtr (clobbered by cursorAddr)
    lda readStrBuf+1; sta kPtr+1
    lda readStrChar
    sta (kPtr),y                // store at current position
    cpy readStrLen              // pos vs len
    bcc rsCharDone              // pos < len -> overwrite, don't inc len
    inc readStrLen              // pos == len -> append
rsCharDone:
    inc readStrPos
    lda readStrChar
    jsr kPrintChar              // writes char, advances cursor
rsNoop:
    jsr blinkReset
    jmp rsLoop

// --- rsHome -- move cursor to start of input ---
rsHome:
    lda #0; sta readStrPos
    ldx #0
    ldy readStrStartRow
    jsr kSetCursor
    jsr blinkReset
    jmp rsLoop

// --- rsRight -- move cursor right within typed text ---
rsRight:
    lda readStrPos; cmp readStrLen; bcc rsRightGo  // not at end
    jmp rsNoop                                      // at end, no-op
rsRightGo:
    inc readStrPos
    inc cursorCol
    lda cursorCol; cmp #SCREEN_COLS; bcc rsRightDone  // no wrap needed
    lda #0; sta cursorCol
    inc cursorRow
rsRightDone:
    jsr blinkReset
    jmp rsLoop

// --- rsLeft -- move cursor left within typed text ---
rsLeft:
    lda readStrPos; bne rsLeftGo  // not at start
    jmp rsNoop                    // at start, no-op
rsLeftGo:
    dec readStrPos
    lda cursorCol; bne rsLeftDec
    // at column 0 -- reverse-wrap to previous line
    lda cursorRow; bne rsLeftWrap
    jmp rsNoop                   // at home, nowhere to go
rsLeftWrap:
    dec cursorRow
    lda #SCREEN_LAST_COL; sta cursorCol
    jsr blinkReset
    jmp rsLoop
rsLeftDec:
    dec cursorCol
    jsr blinkReset
    jmp rsLoop

// --- rsDel -- delete character before cursor ---
rsDel:
    lda readStrPos; bne rsDelOk  // something before cursor
    jmp rsNoop                   // nothing before cursor
rsDelOk:
    cmp readStrLen              // pos vs len
    bcc rsDelMid                // pos < len -> delete mid-line
    // pos == len -> delete last char. Cursor is PAST the last char
    // (at the next type position), so move back FIRST, then erase.
    dec readStrLen
    dec readStrPos
    lda cursorCol; bne rsDelGo
    lda cursorRow; bne rsDelWrap
    jmp rsNoop                   // at home (shouldn't happen: readStrPos > 0)
rsDelWrap:
    dec cursorRow; lda #SCREEN_LAST_COL; sta cursorCol
    bne rsDelErase
rsDelGo:
    dec cursorCol
rsDelErase:
    lda cursorOn
    pha                         // old cursorOn
    lda #0; jsr csrBoth         // disable cursor
    lda cursorCol
    tax
    ldy cursorRow
    lda #$20
    jsr blitChar                // erase (blit space glyph)
    sei                         // atomic: curTask + cursor globals + the
                                // REU DMA must not interleave with a switch
    ldx curTask
    lda taskShadowFlag,x
    beq rsDelNoShadow
    lda #$20
    sta kArg+6
    jsr pcShadowChar            // mirror the erase too (cursor = erased cell)
rsDelNoShadow:
    cli
    pla
    jsr csrBoth                 // restore cursorOn
    jsr blinkReset
    jmp rsLoop

// --- rsDelMid -- delete at mid-line, shift buffer left, redraw tail ---
rsDelMid:
    dec readStrPos              // position of char to delete
    dec readStrLen
    // shift buffer left: buf[i] = buf[i+1] for i=pos..len-1
    lda readStrBuf; sta kPtr
    lda readStrBuf+1; sta kPtr+1
    ldy readStrPos
rsDelShiftLoop:
    cpy readStrLen; bcs rsDelRedraw
    iny                         // y = i+1
    lda (kPtr),y               // buf[i+1]
    dey                         // y = i
    sta (kPtr),y               // buf[i] = buf[i+1]
    iny                         // y = i+1 -> next i
    jmp rsDelShiftLoop
rsDelRedraw:
    // Cursor is already at the screen position of the deleted character
    // (cursorCol/Row still reflect old readStrPos). Redraw forward from here:
    // the first kPrintChar overwrites the deleted char with the shifted one.
    // kPrintChar handles line-wrap, so this works across line boundaries.
rsDelMidRedraw:
    // redraw from current position through end of line
    lda cursorOn; pha           // save cursorOn (kPrintChar clobbers X)
    lda #0; jsr csrBoth         // disable + tail sync
    lda readStrBuf; sta kPtr
    lda readStrBuf+1; sta kPtr+1
    ldy readStrPos
rsDelRedrawLoop:
    cpy readStrLen; bcs rsDelClearTail
    lda (kPtr),y
    tya; pha                    // kPrintChar clobbers Y (Rule 3)
    jsr kPrintChar              // writes char, advances cursor
    pla; tay
    iny
    jmp rsDelRedrawLoop
rsDelClearTail:
    lda #$20; jsr kPrintChar    // erase trailing char
    // reposition cursor to readStrPos
    ldx #0
    ldy readStrStartRow
    jsr kSetCursor              // cursor to start of input
    ldy readStrPos; beq rsDelMidDone
rsDelMidAdvance:
    inc cursorCol
    lda cursorCol; cmp #SCREEN_COLS; bcc rsDelMidAdvSkip
    lda #0; sta cursorCol
    inc cursorRow
    lda cursorRow; cmp #SCREEN_ROWS; bcc rsDelMidAdvSkip  // clamp to last row
    dec cursorRow
rsDelMidAdvSkip:
    dey; bne rsDelMidAdvance
rsDelMidDone:
    pla
    jsr csrBoth                 // restore + tail sync
    jsr blinkReset
    jmp rsLoop
rsReturn:
    jsr blinkReset               // reset blink for clean restart
    lda readStrBuf; sta kPtr    // restore buffer pointer
    lda readStrBuf+1; sta kPtr+1
    ldy readStrLen              // Y = return value
    lda #0; sta readStrLen      // clear global so stale state isn't saved by switchView
    lda #0; sta (kPtr),y        // null-terminate
    // advance kPtr past terminator (like kPrintString convention)
    tya; clc; adc kPtr; sta kPtr
    bcc rsRts; inc kPtr+1
rsRts:
    rts
rsLost:
    // State already saved atomically by switchView when kbFocus changed.
    // rsLost just returns empty so the caller yields and retries.
    ldy #0; rts                 // lost focus -- return empty, caller retries

// --- blinkReset -- reset blink state so the next IRQ tick redraws the cursor ---
//  Clobbers A. Shared by every readString path that moves/erases the cursor.
blinkReset:
    lda #0; sta cursorVisible
    lda #1; sta blinkTick
    rts

// --- blinkResetK -- kernel call (kBlinkReset): erase any live blink block at
//  its saved position, then force a fresh ON draw at the current cursor on the
//  next IRQ tick. Tasks (edit, basic editor) call it after moving the cursor so
//  the block follows. SEI-guarded; clobbers A, X, Y, kPtr, kArg+7.
blinkResetK:
    php
    sei
    jsr eraseCursor             // restore saved glyph at saved pos (no-op if OFF)
    lda #1
    sta blinkTick               // fresh ON at the current cursor next tick
    plp
    rts


