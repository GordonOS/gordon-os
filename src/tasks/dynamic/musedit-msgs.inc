// ==========================================
//  musedit-msgs.inc -- GENERATED, do not edit by hand.
//  Sources: assets/ui/musedit-msgs.txt, assets/ui/musedit-mnems.txt
//           (tools/gen-musedit-msgs.py)
// ==========================================
//  One index per string musedit prints. The text is not here: the REU image
//  holds it as `msgs`, one MU_MSG_SLOT-byte record per index, in this order,
//  each NUL terminated. muMsg DMAs a record into its buffer and points the
//  text at it, so a string costs no code page at all.
.const MU_MSG_COUNT = 38
.const MU_MSG_SLOT = 64
.const M_LANES      = 0
.const M_TEM2       = 1
.const M_BAR2       = 2
.const M_ROW2       = 3
.const M_BAR        = 4
.const M_USAGE      = 5
.const M_USAGE2     = 6
.const M_NO_ART     = 7
.const M_NO_MOUSE   = 8
.const M_ILLEGAL    = 9
.const M_NO_EVENT   = 10
.const M_KEEP_HLT   = 11
.const M_NO_UNDO    = 12
.const M_NO_REDO    = 13
.const M_NO_PLAYER  = 14
.const M_STOP_FIRST = 15
.const M_UNSAVED    = 16
.const M_SAVED      = 17
.const M_NO_ROOM    = 18
.const M_NO_WIN     = 19
.const M_NO_FS      = 20
.const M_NO_WRITE   = 21
.const M_NO_TUNE    = 22
.const M_NO_SCR     = 23
.const M_DIR_FULL   = 24
.const M_NOT_PHRASE = 25
.const M_NO_DEF     = 26
.const M_NO_CALL    = 27
.const M_CMD        = 28
.const M_IN_PHRASE  = 29
.const M_BAD_PHRASE = 30
.const M_SPAN1      = 31
.const M_SPAN2      = 32
.const M_SPAN3      = 33
.const M_NO_BAR     = 34
.const M_NO_CMD     = 35
.const M_UNDONE     = 36
.const M_BAD_TEMPO  = 37

// The length of each message, for a layout that depends on it: the bar's
// labels sit in fixed cells, so their width is not decoration.
.const M_LANES_LEN  = 35
.const M_TEM2_LEN   = 5
.const M_BAR2_LEN   = 5
.const M_ROW2_LEN   = 2
.const M_BAR_LEN    = 40
.const M_USAGE_LEN  = 27
.const M_USAGE2_LEN = 20
.const M_NO_ART_LEN = 16
.const M_NO_MOUSE_LEN = 29
.const M_ILLEGAL_LEN = 18
.const M_NO_EVENT_LEN = 15
.const M_KEEP_HLT_LEN = 20
.const M_NO_UNDO_LEN = 17
.const M_NO_REDO_LEN = 17
.const M_NO_PLAYER_LEN = 11
.const M_STOP_FIRST_LEN = 12
.const M_UNSAVED_LEN = 19
.const M_SAVED_LEN  = 7
.const M_NO_ROOM_LEN = 10
.const M_NO_WIN_LEN = 12
.const M_NO_FS_LEN  = 7
.const M_NO_WRITE_LEN = 14
.const M_NO_TUNE_LEN = 23
.const M_NO_SCR_LEN = 12
.const M_DIR_FULL_LEN = 10
.const M_NOT_PHRASE_LEN = 14
.const M_NO_DEF_LEN = 15
.const M_NO_CALL_LEN = 9
.const M_CMD_LEN    = 4
.const M_IN_PHRASE_LEN = 6
.const M_BAD_PHRASE_LEN = 8
.const M_SPAN1_LEN  = 33
.const M_SPAN2_LEN  = 33
.const M_SPAN3_LEN  = 33
.const M_NO_BAR_LEN = 13
.const M_NO_CMD_LEN = 25
.const M_UNDONE_LEN = 27
.const M_BAD_TEMPO_LEN = 15

// The command mnemonics, appended to the same file after the slots and on a
// page boundary: a field is MU_MN_FIELD bytes and id N sits at (N - 1) * 4,
// so the read's offset is that and its page is M_MN_BASE's own.
.const M_MN_BASE = 2560
.const MU_MN_COUNT = 49
.const MU_MN_FIELD = 4
.const MU_MN_TEM    = 1
.const MU_MN_UTL    = 2
.const MU_MN_UTV    = 3
.const MU_MN_HED    = 4
.const MU_MN_DEF    = 5
.const MU_MN_CALL   = 6
.const MU_MN_TAL    = 7
.const MU_MN_HLT    = 8
.const MU_MN_END    = 9
.const MU_MN_FC     = 10
.const MU_MN_PNT    = 11
.const MU_MN_RTP    = 12
.const MU_MN_HLD    = 13
.const MU_MN_PS     = 14
.const MU_MN_FS     = 15
.const MU_MN_SCA    = 16
.const MU_MN_VDP    = 17
.const MU_MN_VRT    = 18
.const MU_MN_AUT    = 19
.const MU_MN_TPS    = 20
.const MU_MN_PVD    = 21
.const MU_MN_PVR    = 22
.const MU_MN_MAX    = 23
.const MU_MN_PW     = 24
.const MU_MN_DTN    = 25
.const MU_MN_POR    = 26
.const MU_MN_ATK    = 27
.const MU_MN_DCY    = 28
.const MU_MN_SUS    = 29
.const MU_MN_REL    = 30
.const MU_MN_VOL    = 31
.const MU_MN_RES    = 32
.const MU_MN_WAV    = 33
.const MU_MN_FM     = 34
.const MU_MN_BMP    = 35
.const MU_MN_FLT    = 36
.const MU_MN_RNG    = 37
.const MU_MN_SNC    = 38
.const MU_MN_FX     = 39
.const MU_MN_3O     = 40
.const MU_MN_LFO    = 41
.const MU_MN_PNV    = 42
.const MU_MN_RUP    = 43
.const MU_MN_RDN    = 44
.const MU_MN_SRC    = 45
.const MU_MN_DST    = 46
.const MU_MN_MS     = 47
.const MU_MN_FLG    = 48
.const MU_MN_AUX    = 49
