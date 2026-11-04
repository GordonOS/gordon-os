// ==========================================
//  musedit-msgs.inc -- GENERATED, do not edit by hand.
//  Source: assets/ui/musedit-msgs.txt   (tools/gen-musedit-msgs.py)
// ==========================================
//  One index per string musedit prints. The text is not here: the REU image
//  holds it as `msgs`, one MU_MSG_SLOT-byte record per index, in this order,
//  each NUL terminated. muMsg DMAs a record into its buffer and points the
//  text at it, so a string costs no code page at all.
.const MU_MSG_COUNT = 31
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
.const M_NOT_YET    = 9
.const M_ILLEGAL    = 10
.const M_NO_EVENT   = 11
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

// The length of each message, for a layout that depends on it: the bar's
// labels sit in fixed cells, so their width is not decoration.
.const M_LANES_LEN  = 35
.const M_TEM2_LEN   = 5
.const M_BAR2_LEN   = 5
.const M_ROW2_LEN   = 2
.const M_BAR_LEN    = 37
.const M_USAGE_LEN  = 27
.const M_USAGE2_LEN = 20
.const M_NO_ART_LEN = 16
.const M_NO_MOUSE_LEN = 29
.const M_NOT_YET_LEN = 9
.const M_ILLEGAL_LEN = 18
.const M_NO_EVENT_LEN = 15
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
.const M_NO_TUNE_LEN = 12
.const M_NO_SCR_LEN = 12
.const M_DIR_FULL_LEN = 10
.const M_NOT_PHRASE_LEN = 14
.const M_NO_DEF_LEN = 15
.const M_NO_CALL_LEN = 9
.const M_CMD_LEN    = 4
.const M_IN_PHRASE_LEN = 6
.const M_BAD_PHRASE_LEN = 8
