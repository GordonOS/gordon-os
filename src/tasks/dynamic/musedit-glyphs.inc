// ==========================================
//  musedit-glyphs.inc -- GENERATED, do not edit by hand.
//  Source: tools/gen-musedit-glyphs.py   (run it, or -Check in the build)
// ==========================================
//  Eight octaves over 40 cells: one octave is 40 px = 5 cells, so the
//  pattern repeats every five cells and the octave boundary is a cell edge.
//  White-key edges: 0, 6, 11, 17, 23, 29, 34
//  Black keys: 4-7, 10-13, 21-24, 27-30, 33-36

// 15 cells x 1 byte; index = row * 5 + cell. A cell is ONE byte: every
// pixel row of it is the same horizontal pattern, because keys are vertical
// bars, so the task expands it to the 8 bytes a glyph blit wants.
muPianoCell:
    .byte $70   // row 0 cell 0
    .byte $c3   // row 0 cell 1
    .byte $b8   // row 0 cell 2
    .byte $61   // row 0 cell 3
    .byte $87   // row 0 cell 4
    .byte $70   // row 1 cell 0
    .byte $c3   // row 1 cell 1
    .byte $b8   // row 1 cell 2
    .byte $61   // row 1 cell 3
    .byte $87   // row 1 cell 4
    .byte $7d   // row 2 cell 0
    .byte $ef   // row 2 cell 1
    .byte $be   // row 2 cell 2
    .byte $fb   // row 2 cell 3
    .byte $df   // row 2 cell 4

// 40 bytes per row of 5 cells: the top three rows (black keys)
muKeyHi:
    .byte $00,$00,$00,$00,$81,$81,$81,$81
    .byte $02,$02,$83,$83,$83,$83,$04,$04
    .byte $04,$05,$05,$05,$05,$86,$86,$86
    .byte $86,$07,$07,$88,$88,$88,$88,$09
    .byte $09,$8a,$8a,$8a,$8a,$0b,$0b,$0b

// ...and the bottom row (white keys only)
muKeyLo:
    .byte $00,$00,$00,$00,$00,$00,$02,$02
    .byte $02,$02,$02,$04,$04,$04,$04,$04
    .byte $04,$05,$05,$05,$05,$05,$05,$07
    .byte $07,$07,$07,$07,$07,$09,$09,$09
    .byte $09,$09,$0b,$0b,$0b,$0b,$0b,$0b

// How many keyboard rows the black keys cover; the rest is white.
muKeyBlackRows:
    .byte 2
