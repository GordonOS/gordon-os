// ==========================================
//  GordonOS -- Maze task (10 PRINT)
// ==========================================
//  Classic C64 10 PRINT: CHR$(205.5+RND(1)) -- the \ and / diagonals.
//  One char per tick. Cursor auto-wraps.
//  The lowercase font has no diagonal glyphs, so this
//  task carries its own two 8-byte glyphs and draws them via kPrintGlyph
//  (the loader's fixup table relocates the glyph pointers).
//  all-bitmap: hires-text surface (mode 0).

maze:
    lda #0
    ldx #$0b; stx kArg           // border dark grey
    ldx #0;   stx kArg+1         // bg0 black
    stx kArg+2; stx kArg+3
    jsr kAllocScreen
    bcc mzHasScreen
    // no screens -- silently halt
    lda #SYS_YIELD; jsr syscall         // yield forever
    jmp *-3
mzHasScreen:
    jsr kDisableCursor          // no cursor on maze screen
    lda #SYS_GETTID
    jsr syscall                 // gettid -> A = own slot
    tax
    jsr kSwitchView             // display our screen + take kbFocus
mzLoop:
    jsr kRandom
    and #$01
    bne mzFwd
    lda #<mzBackslash
    sta kPtr
    lda #>mzBackslash
    sta kPtr+1
    bne mzPrint                 // always taken: #>glyph != 0 -- unconditional branch
mzFwd:
    lda #<mzSlash
    sta kPtr
    lda #>mzSlash
    sta kPtr+1
mzPrint:
    jsr kPrintGlyph
    jsr mzKey                    // yield once; 'q' exits (like sprdemo/godzi)
    jmp mzLoop

// --- mzKey -- yield once, and quit cleanly if 'q' was pressed -------------
//  WARNING: `kReadKey` is FOCUS-GUARDED (scanKeyboard returns $FF for a task that does
//  not hold kbFocus, without touching the ring buffer), so a maze running in the
//  background never eats the shell's keystrokes -- the poll only sees keys while
//  our own screen is the displayed one.
//  Folding the check and the yield into one call keeps every wait point a single
//  `jsr`, with no branch to a distant exit label (Rule 4) and no extra code at
//  the call sites. SYS_EXIT frees the surface and hands the display back.
mzKey:
    jsr kReadKey
    cmp #'q'
    beq mzQuit
    lda #SYS_YIELD
    jsr syscall
    rts
mzQuit:
    lda #SYS_EXIT
    jsr syscall

.encoding "screencode_mixed"
nameMaze:
    .text "maze"
    .byte 0

// --- custom glyphs (8 bytes each): the 10 PRINT diagonals ---
// Each .byte is one pixel row;
// bit 7 = leftmost pixel. '1' = ink, '0' = background.
mzBackslash:                     // \ -- ink runs top-left -> bottom-right
    .byte %11000000
    .byte %11100000
    .byte %01110000
    .byte %00111000
    .byte %00011100
    .byte %00001110
    .byte %00000111
    .byte %00000011
mzSlash:                         // / -- ink runs top-right -> bottom-left
    .byte %00000011
    .byte %00000111
    .byte %00001110
    .byte %00011100
    .byte %00111000
    .byte %01110000
    .byte %11100000
    .byte %11000000
