// ==========================================
//  musedit.inc -- the body of the .mus tracker editor
// ==========================================
//  Owns the screen, the layout, the eight-octave piano and the key loop.
//
//  Instance data is reached ONLY as `ldy #MU_x` + `(dataPtr),y`; the MU_* names
//  are OFFSETS, never addresses. Everything else (the name copy, the loop
//  counters, the drain counter) lives in the code image, which the loader copies
//  per instance, so it is per-instance RAM.

// --- instance data: offsets into the 41-byte instance buffer ---
//  Offsets 0-11 keep the loader's argument bytes; the name is copied out into
//  the code image at entry, so state starts at 12.
.const MU_ROW       = 12      // cursor row (a screen row, 2..19)
.const MU_VOICE     = 13
.const MU_CURCOL    = 14      // text cursor column (transient)
.const MU_CURROW    = 15      // text cursor row (transient)
.const MU_TMPIDX    = 16      // string walker (transient)
.const MU_INK       = 17
.const MU_STRLO     = 18
.const MU_STRHI     = 19
.const MU_DUR       = 20
.const MU_DOT       = 21
.const MU_TIE       = 22
.const MU_DIRTY     = 23
.const MU_WARN      = 24
.const MU_POS       = 25      // pointer x lo/hi, y lo/hi (16-bit each)
.const MU_WX        = 29
.const MU_WY        = 30
.const MU_XH        = 31
.const MU_TRK_X     = 32      // uPrev, sRun, sMin
.const MU_TRK_Y     = 35
.const MU_FRAME     = 38      // frameCount at the last sample
.const MU_BTN       = 39      // MOUSE_READ's button byte (active low)
.const MU_HOV       = 40      // pitch under the pointer, $ff = none

// --- screen layout ---
.const MU_GRID_TOP  = 2
.const MU_GRID_BOT  = 19
.const MU_PIANO_TOP = 20
.const MU_PIANO_BOT = 23
.const MU_BAR_ROW   = 24
.const MU_SEP1      = 15
.const MU_SEP2      = 27
.const MU_PIANO_COLS = 40

// --- screen codes and colours ---
.const MU_SEP_CHAR  = $5d     // '|' in every bundled font
.const MU_SPACE     = $20
.const MU_C_TEXT    = 1       // white

// --- the pointer ---
.const MU_PTR_READS = 8       // MOUSE_READ calls per displayed frame
.const MU_NOISE     = 16      // the most a frame's POT noise can come to
.const MU_WIN_X     = 24      // sprite x = screen x + 24
.const MU_WIN_Y     = 50      // sprite y = screen y + 50
.const MU_BTN_LEFT  = %00010000 // PA4 of $DC00: the fire line, active low
.const MU_READOUT_COL = 36    // row 0, the top-right corner: the pitch under the pointer
.const MU_C_DIM     = 15      // light grey

// --- the status line and the control bar ---
.const MU_SCREEN_COLS = 40    // the surface's width, in cells
.const MU_STAT_FRAMES = 50    // about a second of displayed frames
.const MU_BAR_TODO  = 1       // hit-map code: nothing is behind that cell yet
.const MU_BAR_Q     = 2       // ...and this one quits

// --- tiny instance helpers (offsets are constants, so these stay inline) ---
.macro muSet(field, value) {
    ldy #field
    lda #value
    sta (dataPtr),y
}
.macro muStr(label) {
    muSet(MU_STRLO, <label)
    muSet(MU_STRHI, >label)
}
.macro muAt(col, row) {
    muSet(MU_CURCOL, col)
    muSet(MU_CURROW, row)
}

// ==========================================
//  Entry
// ==========================================
museditMain:
    jsr muScreen                // allocate, take the display, clear, zero state
    jsr muParseName             // C=1: no name, or longer than 7 characters
    bcs muBadName
    jsr muPtrInit               // the pointer, or a refusal when there is no mouse
    jsr muDrawAll
    jmp muKeyLoop
muBadName:
    jsr muDrawRefuse
muRefWait:
    jsr kReadKey                // any key leaves
    beq muRefNext
    jmp muQuit                  // out of branch range: invert and jump
muRefNext:
    lda #SYS_YIELD
    jsr syscall
    jmp muRefWait

// --- muScreen -- a surface of our own, the display, and a clear screen ---
//  The refusal screen and the editor share this: the screen is allocated before
//  the name is checked, so a bad name has somewhere to be printed.
muScreen:
    // kArg = border, bg0, bg1, bg2; A = 0 -> hires text (edit.inc's colours)
    lda #$0b
    sta kArg
    lda #0
    sta kArg+1
    sta kArg+2
    sta kArg+3
    lda #0
    jsr kAllocScreen
    bcc muScrOk
    jmp muQuit                  // no surface at all: leave without drawing
muScrOk:
    lda #SYS_GETTID
    jsr syscall
    sta muSlot
    php
    sei
    jsr kBlinkReset             // un-draw the shell's live block before taking over
    ldx muSlot
    jsr kSwitchView             // display + keyboard focus to us
    plp
    lda #MU_GRID_TOP
    ldx #MU_GRID_BOT
    jsr kSetScrollRegion        // only the grid scrolls; the piano cannot move
    jsr kDisableCursor
    jsr kClearScreen            // a recycled slot keeps the previous bitmap
    // zero the instance state from MU_ROW upward
    ldy #MU_HOV
    lda #0
muScrZero:
    sta (dataPtr),y
    dey
    cpy #MU_ROW-1
    bne muScrZero
    muSet(MU_ROW, MU_GRID_TOP)
    rts

// --- muParseName -- copy the base name out of the argument area ---
//  Out: C=1 when there is no name or it is 8+ characters. The loader copied the
//  command line into the instance buffer's first 64 bytes, so it is read before
//  any field is written.
muParseName:
    ldy #0
muPnScan:
    lda (dataPtr),y
    beq muPnEnd                 // NUL terminator
    cmp #MU_SPACE
    beq muPnEnd                 // first argument ends
    iny
    cpy #8
    bcc muPnScan
muPnEnd:
    cpy #8
    bcs muPnBad
    cpy #0
    beq muPnBad
    sty muNameLen
    ldy #0
muPnCopy:
    cpy muNameLen
    beq muPnDone
    lda (dataPtr),y
    sta muName,y
    iny
    jmp muPnCopy
muPnDone:
    lda #0
    sta muName,y
    clc
    rts
muPnBad:
    sec
    rts

// ==========================================
//  Drawing
// ==========================================
muDrawAll:
    lda #MU_C_TEXT
    jsr kSetTextColor
    jsr muDrawHead
    jsr muDrawLanes
    jsr muDrawGrid
    jsr muDrawPiano
    jsr muDrawBar
    jsr muCursorShow
    rts

muDrawHead:
    jsr muClearRow0
    jsr muNameAt
    muStr(muSHead)
    jmp muTextAt

muDrawLanes:
    muAt(0, 1)
    muStr(muSLanes)
    jmp muTextAt

// --- muDrawGrid -- 18 empty rows: number, lane separators ---
muDrawGrid:
    lda #MU_GRID_TOP
    sta muGRow
muGridLp:
    lda muGRow
    ldy #MU_CURROW
    sta (dataPtr),y
    muSet(MU_CURCOL, 0)
    lda #MU_SPACE
    jsr muPutC
    lda muGRow
    sec
    sbc #MU_GRID_TOP
    jsr muNum2
    lda #MU_SPACE
    jsr muPutC
    muSet(MU_CURCOL, MU_SEP1)
    lda #MU_SEP_CHAR
    jsr muPutC
    muSet(MU_CURCOL, MU_SEP2)
    lda #MU_SEP_CHAR
    jsr muPutC
    inc muGRow
    lda muGRow
    cmp #MU_GRID_BOT+1
    bcc muGridLp
    rts

// --- muDrawPiano -- four rows of the generated glyphs ---
//  The pattern repeats every five cells (one octave is 40 px = 5 cells), so the
//  glyph is chosen by the cell's position inside the octave, not by the column.
muDrawPiano:
    lda #0
    sta muPRow
muPRowLp:
    lda #0
    sta muPCell
    sta muPCol
muPColLp:
    lda muPRow
    asl
    asl
    clc
    adc muPRow                  // row * 5
    clc
    adc muPCell                 // + cell
    asl
    asl
    asl                         // * 8 (bytes per glyph)
    clc
    adc #<muPianoGlyphs
    sta srcPtr
    lda #>muPianoGlyphs
    adc #0
    sta srcPtr+1
    ldx muPCol
    lda muPRow
    clc
    adc #MU_PIANO_TOP
    tay
    jsr kPrintGlyphAt
    inc muPCell
    lda muPCell
    cmp #5
    bcc muPNoWrap
    lda #0
    sta muPCell
muPNoWrap:
    inc muPCol
    lda muPCol
    cmp #MU_PIANO_COLS
    bcc muPColLp
    inc muPRow
    lda muPRow
    cmp #4
    bcc muPRowLp
    rts

muDrawBar:
    muAt(0, MU_BAR_ROW)
    muStr(muSBar)
    jmp muTextAt

muDrawRefuse:
    muAt(0, 0)
    muStr(muSUsage)
    jsr muTextAt
    muAt(0, 1)
    muStr(muSUsage2)
    jmp muTextAt

// ==========================================
//  Text helpers
// ==========================================
// --- muPutC -- print A at (MU_CURCOL, MU_CURROW) and advance the column ---
//  kPrintCharAt clobbers A, X and Y, so the character rides the task's stack
//  and the column is re-read afterwards.
muPutC:
    pha
    ldy #MU_CURCOL
    lda (dataPtr),y
    tax
    ldy #MU_CURROW
    lda (dataPtr),y
    tay
    pla
    jsr kPrintCharAt
    ldy #MU_CURCOL
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    rts

// --- muNum2 -- print A as two decimal digits and advance ---
muNum2:
    ldx #0
muNum2L:
    cmp #10
    bcc muNum2D
    sbc #10                     // C is set by the cmp that failed
    inx
    jmp muNum2L
muNum2D:
    sta muNumOnes
    txa
    clc
    adc #'0'
    jsr muPutC
    lda muNumOnes
    clc
    adc #'0'
    jmp muPutC

// --- muTextAt -- print the NUL-terminated string at (MU_STRLO/MU_STRHI) ---
//  srcPtr is reloaded before every character because kPrintCharAt clobbers it,
//  and the index lives in the instance because it clobbers Y as well.
muTextAt:
    muSet(MU_TMPIDX, 0)
    lda #MU_C_TEXT
    jsr kSetTextColor
muTextLp:
    ldy #MU_STRLO
    lda (dataPtr),y
    sta srcPtr
    ldy #MU_STRHI
    lda (dataPtr),y
    sta srcPtr+1
    ldy #MU_TMPIDX
    lda (dataPtr),y
    tay
    lda (srcPtr),y
    beq muTextDone
    pha
    ldy #MU_CURCOL
    lda (dataPtr),y
    tax
    ldy #MU_CURROW
    lda (dataPtr),y
    tay
    pla
    jsr kPrintCharAt
    ldy #MU_TMPIDX
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    ldy #MU_CURCOL
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    jmp muTextLp
muTextDone:
    rts

// --- muNameAt -- print the copied base name at the text cursor ---
muNameAt:
    lda #MU_C_TEXT
    jsr kSetTextColor
    lda #0
    sta muNameIdx
muNameLp:
    ldy muNameIdx
    lda muName,y
    beq muNameDone
    pha
    ldy #MU_CURCOL
    lda (dataPtr),y
    tax
    ldy #MU_CURROW
    lda (dataPtr),y
    tay
    pla
    jsr kPrintCharAt
    ldy #MU_CURCOL
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    inc muNameIdx
    jmp muNameLp
muNameDone:
    rts

// --- muClearRow0 -- forty spaces across the head row, fixed-cell ---
//  The column counter lives in the instance and the loop index on the stack,
//  because kPrintCharAt clobbers A, X and Y.
muClearRow0:
    muSet(MU_CURCOL, 0)
    muSet(MU_CURROW, 0)
    ldx #MU_SCREEN_COLS
muClrLp:
    txa
    pha
    lda #MU_SPACE
    jsr muPutC
    pla
    tax
    dex
    bne muClrLp
    muSet(MU_CURCOL, 0)
    rts

// --- muStatus -- a message on the head row for a moment ---
//  In: MU_STRLO/MU_STRHI point at the message. The head replaces it when the
//  countdown runs out, so a status never outlives the next state change.
muStatus:
    jsr muClearRow0
    lda #MU_STAT_FRAMES
    sta muStatT
    jmp muTextAt

// --- muStatusTick -- once per idle pass: retire the status when it is due ---
muStatusTick:
    lda muStatT
    beq muStatTickRts
    dec muStatT
    bne muStatTickRts
    jsr muDrawHead              // the name and the head are back
    muSet(MU_HOV, $ff)          // ...and so is the readout, on the next change
    jmp muHovDraw
muStatTickRts:
    rts

// ==========================================
//  Cursor
// ==========================================
//  Both routines END in `rts`, not in a tail jump: they are called from more
//  than one place, and a tail jump into kPrintCharAt would let its `rts` pop
//  whichever return address happens to be on top -- the caller's, or an earlier
//  jsr's -- which is a stack walk, not a call.
muCursorShow:
    ldy #MU_ROW
    lda (dataPtr),y
    tay
    ldx #0
    lda #'>'
    jsr kPrintCharAt
    rts

muCursorHide:
    ldy #MU_ROW
    lda (dataPtr),y
    tay
    ldx #0
    lda #MU_SPACE
    jsr kPrintCharAt
    rts

// ==========================================
//  The key loop
// ==========================================
//  kReadKey returns a SCREEN CODE and 0 for an empty queue (a key-UP pushes $ff,
//  which it folds to 0). The special codes are the kernel's: $84 CRSR down,
//  $85 up, $86 right, $87 left (SHIFT picks the second pair).
muKeyLoop:
    jsr kReadKey
    beq muKeyIdle
    cmp #'q'
    bne muKeyNotQ
    jmp muQuit                  // out of branch range: invert and jump
muKeyNotQ:
    cmp #$84                    // CRSR down
    beq muCurDown
    cmp #$85                    // CRSR up
    beq muCurUp
    cmp #$86                    // CRSR right: next voice
    beq muCurRight
    cmp #$87                    // CRSR left: previous voice
    beq muCurLeft
muKeyIdle:
    jsr muPointerTick
    jsr muStatusTick
    lda #SYS_YIELD
    jsr syscall
    jmp muKeyLoop

muCurDown:
    ldy #MU_ROW
    lda (dataPtr),y
    cmp #MU_GRID_BOT
    beq muKeyIdle
    jsr muCursorHide
    ldy #MU_ROW
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    jsr muCursorShow
    jmp muKeyIdle

muCurUp:
    ldy #MU_ROW
    lda (dataPtr),y
    cmp #MU_GRID_TOP
    beq muKeyIdle
    jsr muCursorHide
    ldy #MU_ROW
    lda (dataPtr),y
    sec
    sbc #1
    sta (dataPtr),y
    jsr muCursorShow
    jmp muKeyIdle

muCurRight:
    ldy #MU_VOICE
    lda (dataPtr),y
    cmp #2
    beq muKeyIdle
    clc
    adc #1
    sta (dataPtr),y
    jmp muKeyIdle

muCurLeft:
    ldy #MU_VOICE
    lda (dataPtr),y
    beq muKeyIdle
    sec
    sbc #1
    sta (dataPtr),y
    jmp muKeyIdle

// ==========================================
//  The pointer
// ==========================================
// --- muPtrInit -- the two-sprite pointer, or a refusal ---
//  The pointer is not optional: entering a note is a click on a key, so with no
//  mouse there is no editor to run. mouseKind ($E530) is the whole test.
//  The colours go down BEFORE any art: a sprite.lib setter is what installs this
//  task's live sprite state in place of the previous owner's, so a record written
//  before that install lands in the wrong block.
muPtrInit:
    lda mouseKind
    bne muPtrGo
    muStr(muSNoMouse)
    jmp muPtrRefuse             // out of branch range: invert and jump
muPtrGo:
    muSet(MU_HOV, $fe)          // no pitch yet: the first hover must redraw
    sprColour(0, 0)             // the arrow's interior: black, to read on a white key
    sprColour(1, 1)             // its ring: white, to read on a black one
    sprPriority(0)              // in front
    lda #<muSPointer
    sta kPtr
    lda #>muSPointer
    sta kPtr+1
    lda #0
    jsr kSpriteSheet            // a sheet record is per sprite: two calls, one file
    bcs muNoArt
    lda #1
    jsr kSpriteSheet
    bcs muNoArt
    lda #0
    ldx #0                      // frame 0: the interior
    jsr kSpriteFrame
    bcs muNoArt
    lda #1
    ldx #1                      // frame 1: the ring
    jsr kSpriteFrame
    bcs muNoArt
    sprOn(%00000011)            // both slots, now that their art is in place
    lda #MOUSE_ENABLE
    jsr kMouseCall              // the first read reports a zero delta
    muSet(MU_POS, 160)          // start centred: 160,100
    muSet(MU_POS+1, 0)
    muSet(MU_POS+2, 100)
    muSet(MU_POS+3, 0)
    ldy #MU_TRK_X               // the tracker at rest: uPrev, sRun, sMin = 0
    lda #0
muPtrZero:
    sta (dataPtr),y
    iny
    cpy #MU_TRK_Y+3
    bcc muPtrZero
    lda frameCount              // the first displayed frame counts as a change
    ldy #MU_FRAME
    sta (dataPtr),y
    jmp muPlace                 // show it before the first sample

muNoArt:
    muStr(muSNoArt)
// --- muPtrRefuse -- say why the editor cannot run, wait for a key, leave ---
//  In: MU_STRLO/MU_STRHI point at the message. The screen is blank at this point:
//  the pointer comes up before anything is drawn.
muPtrRefuse:
    jsr muClearRow0
    jsr muTextAt
muRefuseWait:
    jsr kReadKey
    beq muRefuseNext
    jmp muQuit                  // out of branch range: invert and jump
muRefuseNext:
    lda #SYS_YIELD
    jsr syscall
    jmp muRefuseWait

// --- muPointerTick -- one update per DISPLAYED frame ---
//  The display's clock, not the scheduler tick: the two differ (50.1 Hz against
//  59.2 Hz), and a pointer moved on the wrong one stutters.
muPointerTick:
    lda frameCount
    ldy #MU_FRAME
    cmp (dataPtr),y
    beq muTickDone
    sta (dataPtr),y
    jsr muSample
    jsr muHover
    jmp muClick
muTickDone:
    rts

// --- muSample -- the frame's reads, the noise correction, and the publish ---
muSample:
    lda #MU_PTR_READS
    sta muFoldN
muSampleLp:
    jsr muReadFold
    dec muFoldN
    bne muSampleLp
    ldx #MOUSE_DELTA_X
    ldy #MU_TRK_X
    jsr muSettle
    ldx #MOUSE_DELTA_Y
    ldy #MU_TRK_Y
    jsr muSettle
    ldx #MOUSE_DELTA_X
    ldy #MU_POS
    jsr muAxis
    ldx #MOUSE_DELTA_Y
    ldy #MU_POS+2
    jsr muAxis
    jsr muClamp
    jmp muPlace

// --- muReadFold -- one MOUSE_READ folded into the position and the tracker ---
//  The kArg result block is a per-call scratch, so both axes are taken before
//  anything else runs.
muReadFold:
    lda #MOUSE_READ
    jsr kMouseCall
    bcc muReadLive
    rts                         // the reader refused: nothing was written
muReadLive:
    ldy #MU_BTN
    sta (dataPtr),y
    ldx #MOUSE_DELTA_X
    ldy #MU_TRK_X
    jsr muTrack
    ldy #MU_POS
    jsr muAxis
    lda kArg+MOUSE_DELTA_Y      // the 1351 runs its y the other way up
    eor #$ff
    clc
    adc #1
    sta kArg+MOUSE_DELTA_Y
    ldx #MOUSE_DELTA_Y
    ldy #MU_TRK_Y
    jsr muTrack
    ldy #MU_POS+2
    jmp muAxis

// --- muTrack -- follow one axis's offset from the frame's first sample ---
//  sRun is the movement since the frame began, sMin its smallest value; the
//  difference is the frame's noise, and muSettle takes it back out.
muTrack:
    lda kArg,x
    iny
    clc
    adc (dataPtr),y
    sta (dataPtr),y
    iny
    sec
    sbc (dataPtr),y
    bvc muTrackS
    eor #$80                    // a signed compare turns N round on overflow
muTrackS:
    bpl muTrackRet
    dey
    lda (dataPtr),y
    iny
    sta (dataPtr),y
muTrackRet:
    rts

// --- muSettle -- turn one axis's frame into the noise correction ---
//  uPrev - sRun + sMin. Steady movement makes the two equal and the correction
//  zero; a figure larger than a frame of noise can hold is not a correction at
//  all (the tracker wrapped), and is taken as zero.
muSettle:
    lda (dataPtr),y
    iny
    sec
    sbc (dataPtr),y
    iny
    clc
    adc (dataPtr),y
    cmp #MU_NOISE+1
    bcc muSetSmall
    pha
    eor #$ff
    clc
    adc #1
    cmp #MU_NOISE+1
    pla
    bcc muSetSmall
    lda #0
muSetSmall:
    sta kArg,x
    lda (dataPtr),y
    sta muTmp
    lda #0
    sta (dataPtr),y
    dey
    lda (dataPtr),y
    sec
    sbc muTmp
    dey
    sta (dataPtr),y
    lda #0
    iny
    sta (dataPtr),y
    iny
    sta (dataPtr),y
    rts

// --- muAxis -- fold one axis's signed delta into its 16-bit position ---
muAxis:
    lda kArg,x
    bmi muAxisNeg
    clc
    adc (dataPtr),y
    sta (dataPtr),y
    iny
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y
    rts
muAxisNeg:
    clc
    adc (dataPtr),y
    sta (dataPtr),y
    iny
    lda (dataPtr),y
    adc #$ff                    // the sign extension, carrying the low byte's carry
    sta (dataPtr),y
    rts

// --- muClamp -- keep the pointer on the screen ---
muClamp:
    ldy #MU_POS+1
    ldx #0
    jsr muClamp1
    ldy #MU_POS+3
    ldx #1
    jmp muClamp1

muClamp1:
    lda (dataPtr),y
    bmi muClampZero
    cmp muBoundHi,x
    bcc muClampOk
    bne muClampMax
    dey
    lda (dataPtr),y
    cmp muBoundLo,x
    iny
    bcc muClampOk
muClampMax:
    lda muBoundLo,x
    sec
    sbc #1
    dey
    sta (dataPtr),y
    iny
    lda muBoundHi,x
    sbc #0
    sta (dataPtr),y
    rts
muClampZero:
    lda #0
    dey
    sta (dataPtr),y
    iny
    sta (dataPtr),y
muClampOk:
    rts

// --- muPlace -- publish both sprites at the pointer's window position ---
muPlace:
    ldy #MU_POS+2
    lda (dataPtr),y
    clc
    adc #MU_WIN_Y
    ldy #MU_WY
    sta (dataPtr),y
    ldy #MU_POS
    lda (dataPtr),y
    clc
    adc #MU_WIN_X               // C carries into the x high byte
    ldy #MU_WX
    sta (dataPtr),y
    ldy #MU_POS+1
    lda (dataPtr),y
    adc #0
    ldy #MU_XH
    sta (dataPtr),y
    ldx #0
    jsr muPut
    ldx #1
    jmp muPut

// --- muPut -- one sprite at (MU_WX/MU_WY, bit 8 in MU_XH) ---
muPut:
    ldy #MU_WX
    lda (dataPtr),y
    pha
    ldy #MU_XH
    lda (dataPtr),y
    cmp #1                      // C = the 9th bit
    ldy #MU_WY
    lda (dataPtr),y
    tay
    pla
    jmp kSpritePos

// ==========================================
//  Hover and click
// ==========================================
// --- muHover -- name the pitch under the pointer on row 0 ---
//  Rows 20-23 are the keyboard; the top three use the black-key table and the
//  bottom row the white-only one. The octave is x/40 and the pixel inside it is
//  x%40 -- both by subtraction, once per frame at most.
muHover:
    ldy #MU_POS+2
    lda (dataPtr),y
    lsr
    lsr
    lsr
    cmp #MU_PIANO_TOP
    bcc muHovNone
    cmp #MU_PIANO_TOP+4
    bcs muHovNone
    sec
    sbc #MU_PIANO_TOP
    sta muHRow
    ldy #MU_POS
    lda (dataPtr),y
    sta muXl
    ldy #MU_POS+1
    lda (dataPtr),y
    sta muXh
    lda #0
    sta muOct
muHovDiv:
    lda muXh
    bne muHovSub
    lda muXl
    cmp #40
    bcc muHovDivDone
muHovSub:
    lda muXl
    sec
    sbc #40
    sta muXl
    lda muXh
    sbc #0
    sta muXh
    inc muOct
    jmp muHovDiv
muHovDivDone:
    ldx muXl                    // the pixel inside the octave
    lda muHRow
    cmp #3
    bcs muHovLow
    lda muKeyHi,x
    jmp muHovTable
muHovLow:
    lda muKeyLo,x
muHovTable:
    and #$7f                    // the semitone; bit 7 was the black-key flag
    sta muSem
    lda muOct                   // note = octave * 12 + semitone
    asl
    asl
    sta muTmp
    lda muOct
    asl
    asl
    asl
    clc
    adc muTmp
    clc
    adc muSem
    jmp muHovSet
muHovNone:
    lda #$ff
muHovSet:
    sta muHovNew
    ldy #MU_HOV
    lda (dataPtr),y
    cmp muHovNew
    beq muHovDone
    lda muHovNew
    sta (dataPtr),y
    jmp muHovDraw
muHovDone:
    rts

// --- muHovDraw -- the readout: "C#4", or three blanks ---
muHovDraw:
    lda #MU_C_TEXT
    jsr kSetTextColor           // clobbers X and Y, so before the indexing
    muSet(MU_CURCOL, MU_READOUT_COL)
    muSet(MU_CURROW, 0)
    ldy #MU_HOV
    lda (dataPtr),y
    cmp #$ff
    beq muHovBlank
    ldx #0                      // note / 12 -> octave, note % 12 -> semitone
muHovD12:
    cmp #12
    bcc muHovD12d
    sec
    sbc #12
    inx
    jmp muHovD12
muHovD12d:
    sta muSem
    stx muOct
    lda muSem                   // two characters per semitone name
    asl
    sta muTmp
    ldx muTmp
    lda muNoteNames,x
    jsr muPutC
    ldx muTmp
    lda muNoteNames+1,x
    jsr muPutC
    lda muOct
    clc
    adc #'0'
    jmp muPutC
muHovBlank:
    lda #MU_SPACE
    jsr muPutC
    lda #MU_SPACE
    jsr muPutC
    lda #MU_SPACE
    jmp muPutC

// --- muClick -- act on the left button's press edge ---
//  MOUSE_READ's button bits are ACTIVE LOW: PA4 clear is a press.
muClick:
    ldy #MU_BTN
    lda (dataPtr),y
    and #MU_BTN_LEFT
    bne muClkUp
    lda muPrevLeft
    bne muClkDone               // still held: no second action
    lda #1
    sta muPrevLeft
    jmp muClickAct
muClkUp:
    lda #0
    sta muPrevLeft
muClkDone:
    rts

// --- muClickAct -- a press acts on the control bar, or places the grid cursor ---
muClickAct:
    ldy #MU_POS+2
    lda (dataPtr),y
    lsr
    lsr
    lsr                         // the cell row
    sta muRowNew
    ldy #MU_POS                 // cell column = x / 8, 16-bit
    lda (dataPtr),y
    sta muXl
    ldy #MU_POS+1
    lda (dataPtr),y
    sta muXh
    lsr muXh
    ror muXl
    lsr muXh
    ror muXl
    lsr muXh
    ror muXl
    lda muRowNew
    cmp #MU_BAR_ROW
    bne muClkGrid
    ldx muXl                    // the bar: one code per column
    lda muBarMap,x
    beq muClkRts                // a blank cell answers nothing
    cmp #MU_BAR_Q
    beq muQuit
    muStr(muSNotYet)            // a labelled cell with nothing behind it yet
    jmp muStatus
muClkGrid:
    cmp #MU_GRID_TOP
    bcc muClkRts
    cmp #MU_GRID_BOT+1
    bcs muClkRts
    lda muXl
    cmp #4                      // lane 1: columns 4-14
    bcc muClkRts
    cmp #15
    bcc muClkV0
    cmp #16                     // column 15 is a separator
    bcc muClkRts
    cmp #27                     // lane 2: 16-26
    bcc muClkV1
    cmp #28                     // column 27 is a separator
    bcc muClkRts
    cmp #39                     // lane 3: 28-38; 39 is the spare column
    bcc muClkV2
    bcs muClkRts
muClkV0:
    lda #0
    beq muClkSetVoice
muClkV1:
    lda #1
    beq muClkSetVoice
muClkV2:
    lda #2
muClkSetVoice:
    ldy #MU_VOICE
    sta (dataPtr),y
    jsr muCursorHide
    lda muRowNew
    ldy #MU_ROW
    sta (dataPtr),y
    jmp muCursorShow
muClkRts:
    rts

// ==========================================
//  Exit
// ==========================================
muQuit:
    lda #16                     // drain by CAPACITY: a release folds to 0
    sta muDrain
muQuitDrain:
    jsr kReadKey
    dec muDrain
    bne muQuitDrain
    lda #MOUSE_DISABLE
    jsr kMouseCall              // stop tracking before the surface goes
    sprOn(0)                    // the pointer goes while we still own the display
    jsr muGiveDisplay
    jsr kFreeScreen
    lda #SYS_EXIT
    jsr syscall
    jmp muQuit                  // never reached

// --- muGiveDisplay -- hand the display back BEFORE releasing our surface ---
//  edit.inc's rule: sysExit's hand-off runs after the task is DEAD, so a dying
//  task with no surface and no parent can blank the screen.
muGiveDisplay:
    lda #SYS_GETTID
    jsr syscall
    cmp kbFocus
    bne muGdRts                 // we do not hold the display
    tax
    lda taskParent,x
    cmp #$ff
    beq muGdRts
    tax
    lda taskState,x
    beq muGdRts                 // the parent is dead: let sysExit decide
    jsr kSwitchView
muGdRts:
    rts

// ==========================================
//  Strings and code-image state
// ==========================================
muSHead:    .text "  TEM---  BAR 1  ROW 000"
            .byte 0
muSLanes:   .text "ROW VOICE 1    |VOICE 2    |VOICE 3"
            .byte 0
muSBar:     .text "DUR 16. TIE-  PLAY  CMD  UNDO  SAVE  Q"
muSBarEnd:  .byte 0
muSUsage:   .text "? name - run musedit <name>"
            .byte 0
muSUsage2:  .text "  up to 7 characters"
            .byte 0
muSNoArt:   .text "? no pointer art"
            .byte 0
muSNoMouse: .text "? no mouse - mouse 1351 first"
            .byte 0
muSNotYet:  .text "? not yet"
            .byte 0
// The control bar's hit map: one code per column of row 24, tested by column.
// A blank cell between two labels is deliberately 0, so nothing answers there.
muBarMap:
    .byte 1,1,1, 0, 1,1,1, 0            // 0-7    DUR, the value, the dot
    .byte 1,1,1,1, 0,0, 1,1,1,1, 0,0    // 8-19   TIE-, PLAY
    .byte 1,1,1, 0,0, 1,1,1,1, 0,0      // 20-30  CMD, UNDO
    .byte 1,1,1,1, 0,0, 2, 0,0          // 31-39  SAVE, Q
muBarMapEnd:
// A code per column, all 40 of them, and the text ends one column before the
// map's last entry, where Q is -- so an edit to either has to keep up with the
// other or the build stops.
.assert "bar map covers the row", (muBarMapEnd - muBarMap) == MU_SCREEN_COLS ? 1 : 0, 1
.assert "bar text ends at the Q column", (muSBarEnd - muSBar) == 38 ? 1 : 0, 1
muSPointer: .text "pointer.spr"
            .byte 0
// Two characters per semitone, C- .. B-: the readout appends the octave digit.
muNoteNames: .text "C-C#D-D#E-F-F#G-G#A-A#B-"
// The clamp bounds, x then y: 320 x 200.
muBoundLo:  .byte <320, <200
muBoundHi:  .byte >320, >200

muSlot:     .byte 0
muName:     .fill 9, 0          // base name + NUL (never more than 7 + 1)
muNameLen:  .byte 0
muNameIdx:  .byte 0
muGRow:     .byte 0
muPRow:     .byte 0
muPCell:    .byte 0
muPCol:     .byte 0
muNumOnes:  .byte 0
muDrain:    .byte 0
muStatT:    .byte 0             // displayed frames left before the head returns
muFoldN:    .byte 0
muTmp:      .byte 0
muHRow:     .byte 0
muXl:       .byte 0
muXh:       .byte 0
muOct:      .byte 0
muSem:      .byte 0
muHovNew:   .byte 0
muPrevLeft: .byte 0
muRowNew:   .byte 0
