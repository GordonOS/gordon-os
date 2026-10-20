// ==========================================
//  musedit-tables.inc -- GENERATED, do not edit by hand.
//  Source: tools/gen-musedit-tables.py   (run it, or -Check in the build)
// ==========================================
//  src/libs/sidplay.inc's DataTempoTable, byte for byte: the triplet base
//  the player indexes with tempo >> 3. A triplet length is this byte shifted
//  right, so a tempo whose block is 0 has no triplet lengths at all.
muTempoTable:
    .byte $00,$00,$00,$10   // tempo $00, $08, $10, $18
    .byte $00,$00,$20,$00   // tempo $20, $28, $30, $38
    .byte $00,$30,$00,$00   // tempo $40, $48, $50, $58
    .byte $40,$00,$00,$50   // tempo $60, $68, $70, $78
    .byte $00,$00,$60,$00   // tempo $80, $88, $90, $98
    .byte $00,$70,$00,$00   // tempo $A0, $A8, $B0, $B8
    .byte $80,$00,$00,$90   // tempo $C0, $C8, $D0, $D8
    .byte $00,$00,$A0,$00   // tempo $E0, $E8, $F0, $F8
