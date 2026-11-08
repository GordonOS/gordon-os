// ==========================================
//  musedit.inc -- the body of the .mus tracker editor
// ==========================================
//  Owns the screen, the layout, the eight-octave piano and the key loop.
//
//  Instance data is reached ONLY as `ldy #MU_x` + `(dataPtr),y`; the MU_* names
//  are OFFSETS, never addresses. Everything else (the name copy, the loop
//  counters, the drain counter) lives in the code image, which the loader copies
//  per instance, so it is per-instance RAM.

// --- zero page: the state the code touches most, out of the image ---
//  A task keeps $02-$EF across a switch (the scheduler DMAs the region the
//  header declares), so the scalars and arrays the walk, the draw and the key
//  loop touch hardest live here instead of in the code image -- which is pool
//  pages -- and every reference to one is a byte shorter (zp,X not abs,X).
//  $02-$0A stays clear: that is sid.lib's scratch, used by the parent's hush and
//  declared by the music child in its own SPAWN arguments.
//  Only zero-initialised state may live here: the boot path zeroes the per-slot
//  shadow, so a variable that starts at a value has to stay in the image.
//  Address-taken labels stay in the image too -- a pointer to a ZP byte is not
//  relocatable -- as does anything the music child reads, because zero page is
//  per SLOT and a thread child would see its own copy, not the parent's.
.label muScrPg      = MU_ZP_BASE+0  // the page a scratch walk is on
.label muScrV       = MU_ZP_BASE+1  // muWinFill/muWinFlush: the voice, across a call
.label muGapUp      = MU_ZP_BASE+2  // muGapMove: 1 = the tail moves up
.label muScrN       = MU_ZP_BASE+3  // ...the image's page count
.label muScrSh      = MU_ZP_BASE+4  // ...the run's start inside the page
.label muDstLo      = MU_ZP_BASE+5  // ...and where it goes, as an image offset
.label muDstHi      = MU_ZP_BASE+6
.label muBarC       = MU_ZP_BASE+7  // the column the bar painter is on
.label muTid        = MU_ZP_BASE+8  // this task's id, for the display-owner test
.label muBarHov     = MU_ZP_BASE+9  // the item under the pointer, 0 = none
.label muBarAct     = MU_ZP_BASE+10  // the item doing something now, 0 = none
.label muNameLen    = MU_ZP_BASE+11
.label muNameIdx    = MU_ZP_BASE+12
.label muPRow       = MU_ZP_BASE+13
.label muPCell      = MU_ZP_BASE+14
.label muPCol       = MU_ZP_BASE+15
.label muNumDig     = MU_ZP_BASE+16
.label muNumPow     = MU_ZP_BASE+17
.label muNumSquash  = MU_ZP_BASE+18
.label muTmpL       = MU_ZP_BASE+19
.label muTmpH       = MU_ZP_BASE+20
.label muHeadRowCol = MU_ZP_BASE+21  // the head's row number, for the row-only update
.label muHeadEnd    = MU_ZP_BASE+22  // ...and the column just past its last digit
.label muHeadTem    = MU_ZP_BASE+23  // the tempo the head's TEM field is showing
.label muNeedPage   = MU_ZP_BASE+24
.label muClrRow     = MU_ZP_BASE+25
.label muTempo      = MU_ZP_BASE+26  // the tempo in force at the first note
.label muDotNat     = MU_ZP_BASE+27  // 1 = the note is a white key
.label muDotS       = MU_ZP_BASE+28  // the ring slot being read
.label muDotMask    = MU_ZP_BASE+29  // the enable mask the dots last asked for
.label muDotV       = MU_ZP_BASE+30
.label muDotOpt     = MU_ZP_BASE+31
.label muDotSem     = MU_ZP_BASE+32
.label muDurByte    = MU_ZP_BASE+33
.label muJifCls     = MU_ZP_BASE+34
.label muJifBase    = MU_ZP_BASE+35
.label muUnIdx      = MU_ZP_BASE+36
.label muScanV      = MU_ZP_BASE+37
.label muStatT      = MU_ZP_BASE+38  // displayed frames left before the head returns
.label muStatF      = MU_ZP_BASE+39  // frameCount at the last counted frame
.label muAudOn      = MU_ZP_BASE+40  // 1 while our own note is sounding on voice 1
.label muHdOn       = MU_ZP_BASE+41  // 1 while the hover dot is on the keyboard
.label muHdOld      = MU_ZP_BASE+42  // the key it is on, and the voice it is that voice's
.label muHdOldV     = MU_ZP_BASE+43
.label muNoteLin    = MU_ZP_BASE+44  // the pitch being entered: set by both writers
.label muHdV        = MU_ZP_BASE+45  // the voice being placed
.label muAudLo      = MU_ZP_BASE+46  // its SID frequency, low and high
.label muAudHi      = MU_ZP_BASE+47
.label muGdClip     = MU_ZP_BASE+48  // 1 while only MU_GDLo..MU_GDHi (rows) is drawn
.label muGdFrom     = MU_ZP_BASE+49  // the band being repainted, in screen rows
.label muGdTo       = MU_ZP_BASE+50  //   (the end exclusive)
.label muGdHi       = MU_ZP_BASE+51  // 2 bytes  (the end exclusive)
.label muFindId     = MU_ZP_BASE+53  // the mnemonic wanted
.label muFindVal    = MU_ZP_BASE+54  // ...and its operand
.label muFindAny    = MU_ZP_BASE+55  // 1 = any row, 0 = only one past MU_FINDAFTER
.label muFindAfter  = MU_ZP_BASE+56  // 2 bytes  the row to pass
.label muCmdVal     = MU_ZP_BASE+58  // the operand of the command under the cursor
.label muCmdRow     = MU_ZP_BASE+59  // 2 bytes  ...and the row its line belongs to
.label muObjId      = MU_ZP_BASE+61  // the command being authored: mnemonic id 1..9
.label muObjVal     = MU_ZP_BASE+62  // ...its operand
.label muObjMax     = MU_ZP_BASE+63  // ...and the largest operand it takes
.label muObjValHi   = MU_ZP_BASE+224  // the high byte of each, for the 14-bit commands
.label muObjMaxHi   = MU_ZP_BASE+225
.label muCmdValHi   = MU_ZP_BASE+226  // the high half of a command's operand
.label muObjKind    = MU_ZP_BASE+64  // 0 = a pair to add, 1 = the pair at MU_OBJLO/HI
.label muObjPhase   = MU_ZP_BASE+65  // 0 = picking the mnemonic, 1 = typing the operand
.label muObjFresh   = MU_ZP_BASE+66  // 0 = the next digit starts the operand afresh
.label muObjMatch   = MU_ZP_BASE+67  // 1 = a scan matches the operand as well as the id
.label muObjLo      = MU_ZP_BASE+68  // the pair being found, replaced or added
.label muObjHi      = MU_ZP_BASE+69
.label muObjOptB    = MU_ZP_BASE+70
.label muLinkW      = MU_ZP_BASE+71  // 1 while a search has walked the producer
.label muToff       = MU_ZP_BASE+72  // lines into MU_TOP's group the band's top line is
.label muRevText    = MU_ZP_BASE+73  // 1 while muPutC draws reversed (command lines)
.label muGrpRow     = MU_ZP_BASE+74  // 2 bytes  the row the open group belongs to
.label muGrpSt      = MU_ZP_BASE+76  // 1 = a group is open, 0 = the next line opens one
.label muLnVLo      = MU_ZP_BASE+77  // 2 bytes  the value muLnDec rebuilt for the line being drawn
.label muLnVHi      = MU_ZP_BASE+79
.label muDecLo      = MU_ZP_BASE+80  // muDec16's operand and its running remainder
.label muDecHi      = MU_ZP_BASE+81
.label muDecDig     = MU_ZP_BASE+82  // the digit muDec16 is building
.label muDecIdx     = MU_ZP_BASE+83  // which decimal weight, or which digit of
.label muDecAny     = MU_ZP_BASE+84  //   muPutC clobbers X; 1 once a digit is printed
.label muLogN       = MU_ZP_BASE+85  // 6 bytes  how many that voice and class has logged
.label muLogSnd     = MU_ZP_BASE+91  // the row's sound lines: the structural ones follow
.label muLogLns     = MU_ZP_BASE+92  // the sound lines plus the structural, command lines
.label muLogIx      = MU_ZP_BASE+93  // the command line being drawn
.label muLnDraw     = MU_ZP_BASE+94  // the line is inside the band being repainted
.label muLnV        = MU_ZP_BASE+95  // the voice a note line is drawing
.label muNoteOut    = MU_ZP_BASE+96  // 1 = muStep returned a note, 0 = it halted a voice
.label muTmp        = MU_ZP_BASE+97
.label muHRow       = MU_ZP_BASE+98
.label muXl         = MU_ZP_BASE+99
.label muXh         = MU_ZP_BASE+100
.label muOct        = MU_ZP_BASE+101
.label muSem        = MU_ZP_BASE+102
.label muHovNew     = MU_ZP_BASE+103
.label muPrevLeft   = MU_ZP_BASE+104
.label muRowNew     = MU_ZP_BASE+105
.label muWinPage    = MU_ZP_BASE+106  // its page, while the seat works
.label muVRow       = MU_ZP_BASE+107  // 3 bytes  each voice's next event row
.label muVRowHi     = MU_ZP_BASE+110  // 3 bytes
.label muSegUnit    = MU_ZP_BASE+113  // the segment's row unit, in jiffies
.label muFitU       = MU_ZP_BASE+114  // muFit's answer: the unit at a tempo, and
.label muFitBar     = MU_ZP_BASE+115  //   the rows a bar has there (with the refinement)
.label muFitBarHi   = MU_ZP_BASE+116
.label muFitP       = MU_ZP_BASE+117  // muRefine's inputs: the plain unit and bar
.label muFitPb      = MU_ZP_BASE+118
.label muGdCol      = MU_ZP_BASE+119
.label muDurArm     = MU_ZP_BASE+120  // the length a new note is written with
.label muDurJif     = MU_ZP_BASE+121  // ...and that length in jiffies, at the cursor's tempo
.label muGapJ       = MU_ZP_BASE+122  // the gap a fill has to cross, in jiffies
.label muKbdOct     = MU_ZP_BASE+123  // the octave the note keys play in
.label muNewOpt     = MU_ZP_BASE+124  // the option byte being written
.label muTgtLo      = MU_ZP_BASE+125  // the row the cursor is on
.label muTgtHi      = MU_ZP_BASE+126
.label muSekKind    = MU_ZP_BASE+127  // 0 onset, 1 inside, 2 past the end
.label muSekLo      = MU_ZP_BASE+128  // that event's offset in the image
.label muSekHi      = MU_ZP_BASE+129
.label muSekDur     = MU_ZP_BASE+130  // its duration byte, and
.label muSekOpt     = MU_ZP_BASE+131  // its option byte
.label muN1         = MU_ZP_BASE+132  // the events a fill's rests, or a split's runs, take
.label muN2         = MU_ZP_BASE+133  // ...and the note's own
.label muSekTem     = MU_ZP_BASE+134  // the tempo in force at the recorded event
.label muSkV        = MU_ZP_BASE+136
.label muValLo      = MU_ZP_BASE+137
.label muValHi      = MU_ZP_BASE+138
.label muLenOpt     = MU_ZP_BASE+139  // its option byte
.label muLenTie     = MU_ZP_BASE+140  // 1: tie every event after the first
.label muLenMode    = MU_ZP_BASE+141  // 0 count, 1 write
.label muLenCnt     = MU_ZP_BASE+142
.label muLpIdx      = MU_ZP_BASE+143
.label muLpJ        = MU_ZP_BASE+144
.label muLpDur      = MU_ZP_BASE+145
.label muInsLo      = MU_ZP_BASE+146  // where a gap opens, as an image offset
.label muInsHi      = MU_ZP_BASE+147
.label muInsN       = MU_ZP_BASE+148  // how many bytes
.label muInsV       = MU_ZP_BASE+149  // the voice whose length word grows
.label muCountLo    = MU_ZP_BASE+150  // a byte count, 16 bits
.label muCountHi    = MU_ZP_BASE+151
.label muPlainU     = MU_ZP_BASE+152
.label muUnitRef    = MU_ZP_BASE+153  // 0 plain, 1 half, 2 third, 3 one jiffy
.label muPtrLo      = MU_ZP_BASE+154  // 3 bytes  where this voice's next event is, as a FILE OFFSET
.label muPtrHi      = MU_ZP_BASE+157  // 3 bytes
.label muOn         = MU_ZP_BASE+160  // 3 bytes  a HLT clears this
.label muDep        = MU_ZP_BASE+163  // 3 bytes  the next free call-stack frame
.label muPhDf       = MU_ZP_BASE+166  // 3 bytes  the phrase each voice is executing, +1 (0 = the stream)
.label muPhNo       = MU_ZP_BASE+169  // what the status last named, +1 -- 0 = the top level
.label muPhPend     = MU_ZP_BASE+170  // a body the cursor has just arrived in, to announce
.label muPhBad      = MU_ZP_BASE+171  // the phrase an edit was refused for, +1
.label muPhCnt      = MU_ZP_BASE+172  // 2 bytes  the CALL sites counted for it...
.label muPhCur      = MU_ZP_BASE+174  // 2 bytes  ...and the scan's own cursor: an image offset...
.label muPhEnd      = MU_ZP_BASE+176  // 2 bytes  ...and where the three streams end
.label muClkLo      = MU_ZP_BASE+178  // the tick of the latest event
.label muClkHi      = MU_ZP_BASE+179
.label muSimT       = MU_ZP_BASE+180  // the tempo the walk is in
.label muJifTem     = MU_ZP_BASE+181  // ...and the copy muNoteJiffies reads
.label muUl         = MU_ZP_BASE+182
.label muErrV       = MU_ZP_BASE+183  // the voice whose length the tempo refused...
.label muSv         = MU_ZP_BASE+184
.label muScmd       = MU_ZP_BASE+185
.label muSopt       = MU_ZP_BASE+186
.label muSph        = MU_ZP_BASE+187
.label muAccLo      = MU_ZP_BASE+188
.label muAccHi      = MU_ZP_BASE+189
.label muBestLo     = MU_ZP_BASE+190
.label muBestHi     = MU_ZP_BASE+191
.label muTmp2       = MU_ZP_BASE+192  // 2 bytes
.label muPlayHush   = MU_ZP_BASE+194  // the stop second-hush guard
.label muPhCurLo    = MU_ZP_BASE+195  // 2 bytes  the row the cursor is on while playing (lo, hi)
.label muPhWait     = MU_ZP_BASE+197  // 1 until the player publishes THIS pass's count
//  The pointers below are `.const` rather than `.label` so that the indirect lint can
//  prove they are zero page: it resolves a .const chain, and an unresolvable pointer
//  is a failure there, not a warning.
.const muLogIdP     = MU_ZP_BASE+198  // 2 bytes  pointers into the instance buffer: the row's
.const muLogLoP     = MU_ZP_BASE+200  // 2 bytes  logged commands (muHeapPtrs fills them)
.label muDurTie     = MU_ZP_BASE+202  // 1 = the next new note is tied to the one after
.label muDirty      = MU_ZP_BASE+203  // an image byte changed since the last save
.label muWarn       = MU_ZP_BASE+204  // Q warned once; the next Q discards
.label muRecV       = MU_ZP_BASE+205  // ...voice...
.label muRecOldN    = MU_ZP_BASE+206  // ...bytes it replaces...
.label muRecNewN    = MU_ZP_BASE+207  // ...bytes it writes...
.label muRecTot     = MU_ZP_BASE+208  // the record's total length
.label muRecCont    = MU_ZP_BASE+209  // 1 = the next push appends to the action in flight
.label muRecGrpStart = MU_ZP_BASE+210  // bit 7 of the loaded record's voice byte
.label muInsJif     = MU_ZP_BASE+211  // ...which is this many jiffies...
.label muItV        = MU_ZP_BASE+212  // ...spliced into this voice...
.label muInsFirst   = MU_ZP_BASE+213  // ...the first splice opens the grouped action
.label muItJ1       = MU_ZP_BASE+214  // straddle: the front jiffies before T0
.label muInsOpt     = MU_ZP_BASE+215  // the opt the splice writes (0 rest, note in INS)
.label muInsTie     = MU_ZP_BASE+216  // ...and whether its run ties
.label muInsMode    = MU_ZP_BASE+217  // 0 overwrite, 1 insert (note entry pushes down)
.label muRrHave     = MU_ZP_BASE+218  // range replace: bytes there now...
.label muRrWant     = MU_ZP_BASE+219  // ...and bytes wanted
.label muUcOf       = MU_ZP_BASE+220  // the stack cursor's offset inside its page
.label muUcPg       = MU_ZP_BASE+221  // ...and its page
.label muDrRows     = MU_ZP_BASE+222  // the rows a delete still has to take out
.label muUcTmp      = MU_ZP_BASE+223  // the one byte a stack transfer carries
.label muInsFrom    = MU_ZP_BASE+227  // 2 bytes  the first row a splice touched
.label MU_ZP_END    = MU_ZP_BASE+229
.assert "the zero page region fits the declared window", (MU_ZP_END - MU_ZP_BASE) <= MU_ZP_LEN ? 1 : 0, 1
.assert "the declared window covers the last variable", (MU_ZP_END - MU_ZP_BASE) >= 229 ? 1 : 0, 1
.assert "the zero page region stays below the kernel's IRQ bytes", (MU_ZP_BASE + MU_ZP_LEN) <= $f0 ? 1 : 0, 1

// --- instance data: offsets into the 53-byte instance buffer ---
//  Offsets 0-11 keep the loader's argument bytes; the name is copied out into
//  the code image at entry, so state starts at 12.
.const MU_ROW       = 12      // cursor row (a screen row, 2..19)
.const MU_VOICE     = 13
.const MU_CURCOL    = 14      // text cursor column (transient)
.const MU_CURROW    = 15      // text cursor row (transient)
.const MU_TMPIDX    = 16      // string walker (transient)
.const MU_STRLO     = 18      // the string muTextAt is printing: muMsgBuf, or
.const MU_STRHI     = 19      //   muSFallback before muMsgOpen has run
.const MU_POS       = 25      // pointer x lo/hi, y lo/hi (16-bit each)
.const MU_WX        = 29
.const MU_WY        = 30
.const MU_XH        = 31
.const MU_TRK_X     = 32      // uPrev, sRun, sMin
.const MU_TRK_Y     = 35
.const MU_FRAME     = 38      // frameCount at the last sample
.const MU_BTN       = 39      // MOUSE_READ's button byte (active low)
.const MU_HOV       = 40      // pitch under the pointer, $ff = none
.const MU_LEN       = 41      // bytes in that image (load address included)
.const MU_FLEN      = 43      // the on-disk size kReuOpen reported
.const MU_FREC      = 45      // 5-byte file record: size lo/hi, base lo/mid/hi
.const MU_STATE_TOP = MU_DATA_SIZE-1  // the highest state byte: the zero loop starts here

// --- instance data: the row's logged commands, one block per voice and class ---
//  These are the largest arrays the editor holds and the least often touched, so
//  two of the three live in the instance buffer (which costs pool pages nothing)
//  rather than in the code image, and a zero-page pointer each keeps the addressing
//  at a byte per reference. The buffer holds 255 bytes, so the third array stays in
//  the image -- the assert below is what keeps that honest.
.const MU_LOGID     = 53
.const MU_LOGLO     = MU_LOGID+16*6
.const MU_LOGID_END = MU_LOGLO+16*6
.assert "the logged-command arrays fit the instance buffer", MU_LOGID_END <= MU_DATA_SIZE ? 1 : 0, 1
.assert "the instance buffer is one byte", MU_DATA_SIZE <= 255 ? 1 : 0, 1

// --- screen layout ---
.const MU_GRID_TOP  = 2
.const MU_GRID_BOT  = 20
.const MU_MODE_ROW  = 1       // the lane-header row carries the mode readout...
.const MU_MODE_COL  = 37      // ...and these three cells, which are its button
.const MU_PIANO_ROWS = 3      // keyboard rows (the glyphs are generated for this many)
.const MU_PIANO_TOP = 21
.const MU_BAR_ROW   = 24
.const MU_PIANO_COLS = 40
// The three lanes are one width and one column apart, and the first starts at
// MU_LANE1_COL -- the cell just under its header. Both the drawing and the click
// hit test read these, so a note and the cell that edits it cannot disagree.
.const MU_LANE1_COL = 4
.const MU_LANE_W    = 11
.const MU_LANE_STEP = MU_LANE_W+1

// --- screen codes and colours ---
.const MU_SPACE     = $20
.const MU_C_TEXT    = 1       // white

// --- the pointer ---
.const MU_PTR_READS = 8       // MOUSE_READ calls per displayed frame
.const MU_NOISE     = 16      // the most a frame's POT noise can come to
.const MU_WIN_X     = 24      // sprite x = screen x + 24
.const MU_WIN_Y     = 50      // sprite y = screen y + 50
.const MU_BTN_LEFT  = %00010000 // PA4 of $DC00: the fire line, active low
.const MU_READOUT_COL = 36    // row 0, the top-right corner: the pitch under the pointer

// --- the simulator ---
.const MU_INIT_TEM  = $90     // sidplay.inc's sidInitSong starts here
.const MU_LEN_MAX   = 100     // the most events one written length may take
.const MU_SPLIT_MAX = 128     // the most events a split's three runs may take
                              //   (its byte count is twice that, less the pair
                              //   the first run replaces: under 256)
.const MU_INIT_UL   = 12      // ...and its WorkUTLValue
.const MU_CMD_TEM   = $06     // TEM: the global tempo
.const MU_CMD_UTL   = $16     // UTL: the global utility length
.const MU_CMD_UTV   = $F6     // UTV: this voice's utility length
.const MU_CMD_HED   = $36     // HED: the head of a repeat section
.const MU_OPT_TAL   = $0f     // TAL: back to the head, or the section ends
.const MU_OPT_END   = $2f     // END: return from a phrase
.const MU_OPT_HLT   = $4f     // HLT: this voice stops
.const MU_FRAMES    = 5       // call-stack frames per voice
.const MU_SPIN      = 255     // commands one step may run before it gives up
.const MU_GRID_ROWS = 19      // the rows a window shows
.const MU_REST_CH   = '.'     // a rest
.const MU_OFF_CH    = '?'     // its onset fell between rows
.const MU_ERR_SPIN  = 1       // a run of commands with no note behind it
.const MU_ERR_LEN   = 2       // a length this tempo cannot express
.const MU_ERR_STK   = 3       // the call stack is full
.const MU_ERR_PHR   = 4       // a call to a phrase that was never defined
.const MU_ERR_TAL   = 5       // a TAL whose HED belongs to another phrase
.const MU_UNDO_SIZE = 2048    // the undo stack, in bytes (many small actions)
.const MU_UNDO_PAGES = MU_UNDO_SIZE / 256   // ...and the REU file's size, one page each
.const MU_MUS_DELAY = 1       // one player tick per jiffy
.const MU_MUS_REPEATS = 1     // one pass through the tune: the end stops it
.const MU_MUS_REAP  = 60      // slices to wait for the music child to leave
.const MU_POS_BLOCK = $54A2   // the SID player's published per-voice position (sid.asm SID_POS_BLOCK)
.const MU_FOLLOW_CENTER = 8   // lines below the window top the playhead is pinned to
.const MU_FOLLOW_WALK   = MU_GRID_ROWS-1   // the most lines one pass may walk: the walk
                              //   stops as soon as the playhead's line is on target, so
                              //   this only bounds a catch-up
.const MU_HIST      = 32      // pointer -> row slots per voice (power of two)
.const MU_LOG_SLOTS = 16      // command pairs one voice may log for one row: the
                              //   longest run a bundled tune has is twelve
.const MU_MN_TEM    = 1       // ids 1..9 are the structural commands, 10..46 the
.const MU_MN_UTL    = 2       //   sound ones and 47..49 the player's own three. The
.const MU_MN_UTV    = 3       //   mnemonic table is one five-character field per id,
.const MU_MN_HED    = 4       //   in this order; TAL, HLT and END take no operand and
.const MU_MN_DEF    = 5       //   print no value.
.const MU_MN_CALL   = 6
.const MU_MN_TAL    = 7
.const MU_MN_HLT    = 8
.const MU_MN_END    = 9
.const MU_MN_FC     = 10      // filter cutoff
.const MU_MN_PNT    = 11      // release point
.const MU_MN_RTP    = 12      // relative transpose
.const MU_MN_HLD    = 13      // hold time
.const MU_MN_PS     = 14      // pulse sweep
.const MU_MN_FS     = 15      // filter sweep
.const MU_MN_SCA    = 16      // modulation scale
.const MU_MN_VDP    = 17      // vibrato depth
.const MU_MN_VRT    = 18      // vibrato rate
.const MU_MN_AUT    = 19      // auto filter
.const MU_MN_TPS    = 20      // transpose
.const MU_MN_PVD    = 21      // pulse-width vibrato depth
.const MU_MN_PVR    = 22      // pulse-width vibrato rate
.const MU_MN_MAX    = 23      // modulation maximum
.const MU_MN_PW     = 24      // pulse width
.const MU_MN_DTN    = 25      // detune
.const MU_MN_POR    = 26      // portamento
.const MU_MN_ATK    = 27
.const MU_MN_DCY    = 28
.const MU_MN_SUS    = 29
.const MU_MN_VOL    = 31
.const MU_MN_RES    = 32
.const MU_MN_WAV    = 33
.const MU_MN_FM     = 34      // filter mode
.const MU_MN_BMP    = 35      // bump volume
.const MU_MN_FLT    = 36      // filter through
.const MU_MN_RNG    = 37      // ring modulation
.const MU_MN_SNC    = 38      // oscillator sync
.const MU_MN_FX     = 39      // filter external
.const MU_MN_3O     = 40      // voice 3 off
.const MU_MN_LFO    = 41
.const MU_MN_PNV    = 42      // portamento + vibrato
.const MU_MN_RUP    = 43      // LFO ramp up
.const MU_MN_RDN    = 44      // LFO ramp down
.const MU_MN_SRC    = 45      // modulation source
.const MU_MN_DST    = 46      // modulation destination
.const MU_MN_MS     = 47      // measure number, 0-999: the command byte's top two
                              //   bits are its high byte
.const MU_MN_FLG    = 48      // the player's flag byte
.const MU_MN_AUX    = 49      // reserved: the player has no hook to run
.const MU_MN_LAST   = MU_MN_AUX // the last id the picker offers
.const MQ_WIDE      = 49      // where mqOptB's second block starts: one per id
.const MU_WIN_PAGES  = 6       // the window service: SIX 256-byte, page-aligned
                               //   windows -- 0-2 are the editor's, one per voice, and
                               //   3-5 are the player's, handed over in kSidPlay's
                               //   config block so the child pages the same file the
                               //   editor edits (see muWinSeat)
.const MU_SCR_PAGES  = 255     // <base>.wrk holds this many 256-byte pages: 65280
                               //   bytes is the whole of a 16-bit file size, and a
                               //   write past EOF is refused, so the image must have
                               //   room to grow through an edit before it is written

// --- the status line and the control bar ---
.const MU_SCREEN_COLS = 40    // the surface's width, in cells
.const MU_STAT_FRAMES = 50    // about a second of displayed frames
.const MU_AUD_FRAMES  = 5     // the audition's length, in displayed frames (100 ms)
.const MU_UNIT_N   = 7        // the row unit's candidates, shortest first
.const MU_BAR_Q     = 2       // hit-map code: this one quits
.const MU_BAR_SAVE  = 3       // ...and this one writes the image back
.const MU_BAR_PLAY  = 4       // ...and this one plays or stops
.const MU_BAR_DUR   = 5       // ...and this one steps the armed length on
.const MU_BAR_TIE   = 6       // ...and this one arms or drops the tie
.const MU_BAR_CMD   = 7       // ...and this one authors the row's command line
.const MU_BAR_UNDO  = 8       // ...and this one undoes the last action
.const MU_BAR_REST  = 9       // ...and this one turns the event into a rest
.const MU_BAR_DEL   = 10      // ...and this one removes the event, closing the gap
.const MU_BAR_INS   = 11      // ...and this one opens one row of time in every voice

// --- tiny instance helpers (offsets are constants, so these stay inline) ---
.macro muSet(field, value) {
    ldy #field
    lda #value
    sta (dataPtr),y
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
    jsr muMsgOpen               // the printed text is the REU file `msgs`, not code
    bcc muMsgReady
    muSet(MU_STRLO, <muSFallback)
    muSet(MU_STRHI, >muSFallback)
    jmp muFatal                 // with no file there is nothing else to say
muMsgReady:
    jsr muUndoOpen              // the undo stack's pages, in the REU as well
    jsr muSidCheck              // C=1: another task is using sid.lib -- report it
    bcc muSidFree               //   on OUR screen, before any library is touched
    lda #M_NO_PLAYER
    jsr muMsgSet
    jmp muFatal
muSidFree:
    jsr muParseName             // C=1: no name, or longer than 7 characters
    bcs muBadName
    jsr muNameSetup             // <base>.mus, in screen codes
    jsr muPtrInit               // the pointer, or a refusal when there is no mouse
    jsr muWinInit               // the three read windows, taken once for the session
    bcc muWinTaken
    lda #M_NO_WIN               // no pool room for them: without windows the grid
    jsr muMsgSet
    jmp muPtrRefuse             //   cannot read the image at all
muWinTaken:
    jsr muHeapPtrs              // the pointers into the instance buffer's arrays
    jsr muLoad                  // the tune, or a refusal when it cannot be had
    bcc muMainGo
    jmp muPtrRefuse             // muLoad left the reason in MU_STR
muMainGo:
    jsr muWinReset              // the windows outlived the last image: forget it
    jsr muSidCfg                // the child's kSidPlay config: the file and its windows
    jsr muFindTempo             // the tempo the first note will hear
    jsr muUnitNow               // the row unit that tempo falls into
    jsr muTrScan                // a triplet or second dot anywhere means the rows
    bcc muUnitAuto              //   must be thirds, or the onsets fall between them
    lda #2
    sta muUnitRef
    jsr muUcValid
    bcs muUnitAuto              // this tempo's plain unit does not divide by three
    jsr muUnitApply
muUnitAuto:
    jsr muMusicInit             // spawn the music child
    lda #$18                    // the armed length: a 16th until the bar says
    sta muDurArm                //   otherwise
    lda #4
    sta muKbdOct                // the note keys start at the middle octave
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
    jsr kDisableCursor          // our surface carries no blinking text cursor
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
    jsr kClearScreen            // a recycled slot keeps the previous bitmap
    // zero the instance state from MU_ROW upward
    ldy #MU_STATE_TOP
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

// --- muNameSetup -- the FS names: the base name with .mus, .wrk and .tmp on the end ---
//  FS names are SCREEN CODES, so the letters come from character literals under
//  the file's encoding, not from a PETSCII string. A 7-character base reaches
//  exactly the 11 the filesystem allows, so all three names fit.
muNameSetup:
    ldx #0
muNsLp:
    cpx muNameLen
    beq muNsExt
    lda muName,x
    sta muFsName,x
    sta muSFName,x
    sta muTmpName,x
    inx
    bne muNsLp                  // a 7-character name ends the loop, never wraps
muNsExt:
    lda #'.'
    sta muFsName,x
    sta muSFName,x
    sta muTmpName,x
    lda #'m'
    sta muFsName+1,x
    lda #'w'
    sta muSFName+1,x
    lda #'t'
    sta muTmpName+1,x
    lda #'u'
    sta muFsName+2,x
    lda #'r'
    sta muSFName+2,x
    lda #'m'
    sta muTmpName+2,x
    lda #'s'
    sta muFsName+3,x
    lda #'k'
    sta muSFName+3,x
    lda #'p'
    sta muTmpName+3,x
    lda #0
    sta muFsName+4,x
    sta muSFName+4,x
    sta muTmpName+4,x
    rts

// ==========================================
//  The tune image
// ==========================================
//  The image is <base>.wrk in the REU filesystem -- load address, the three length
//  words, the three streams and the description after them -- reached through three
//  page-aligned 256-byte read windows (one per voice) and written back a page at a
//  time, so its length costs the task no pool pages and the player pages the same
//  file through the same windows (kSidPlay mode 1).
//  MU_LEN is the image's length; MU_FLEN is what the real file on disk holds, which
//  includes the slack a whole-page file carries.

// --- muFsArg -- kArg = the tune's own name ---
muFsArg:
    lda #<muFsName
    sta kArg
    lda #>muFsName
    sta kArg+1
    rts

// --- muTmpArg -- kArg = the scratch name a replacement is built under ---
muTmpArg:
    lda #<muTmpName
    sta kArg
    lda #>muTmpName
    sta kArg+1
    rts

// --- muFrecPt -- kPtr = the five-byte file record in the instance buffer ---
muFrecPt:
    lda dataPtr
    clc
    adc #MU_FREC
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    rts

// --- muRecArg -- kArg = the same record, which is what readAt/writeAt take ---
muRecArg:
    lda dataPtr
    clc
    adc #MU_FREC
    sta kArg
    lda dataPtr+1
    adc #0
    sta kArg+1
    rts

// --- muImgSeat -- kPtr = the image's own first byte, in window 0 ---
//  The header -- load address and the three length words -- is eight bytes, so it
//  is always page 0. Clobbers A, X, Y, kPtr, srcPtr.
muImgSeat:
    lda #0
    sta muWinOfs
    jsr muWinSeat0
    rts

// ==========================================
//  The window service
// ==========================================
//  Every read of the tune image goes through one of THREE windows: 256 bytes each,
//  PAGE-ALIGNED, one per voice. Page-aligned is what turns an offset into an
//  address with no arithmetic at all -- the address is `winHi : offsetLo` -- and it
//  is safe for commands because a command is a 2-byte pair at an EVEN offset, so a
//  command and its option byte can never straddle the end of a window: a voice that
//  crosses a page refills its window before its next command is read.
//
//  A window holds a COPY of a page of <base>.wrk, so a write has to reach the page it
//  came from: muWinFlush writes the held page back.
//
//  In:  X = the voice (0-2), muWinOfs/muWinOfs+1 = the offset wanted.
//  Out: C=0 and kPtr = the address of that offset inside voice X's window.
//  Clobbers: A, Y, kPtr, srcPtr (X, dataPtr and each voice's own state survive).
//  WARNING: kPtr is only good for the bytes of THAT page. Any kernel call may take
//  it, and a second seat may refill the window, so derive it again per read.
muWinSeat:
    lda muWinOfs+1              // the page the offset sits on
    sta muWinPage
    lda muWinOk,x               // a page tag CANNOT say "never filled" -- every value
    beq muWsFill                //   0-255 is a real page -- so a flag says it instead
    lda muWinPg,x
    cmp muWinPage
    beq muWsHave
muWsFill:
    lda muWinPage
    sta muWinPg,x
    lda #1
    sta muWinOk,x
    jsr muWinFill
muWsHave:
    jsr muWinPtr                // kPtr = that offset inside window X
    lda muWinOfs                // page-aligned, so the offset's LOW byte IS the
    sta kPtr                    //   address's
    clc
    rts

// --- muWinSeat0 -- kPtr = the image at MU_WINOFS, in window 0 ---
//  In: A = the high byte of the offset, MU_WINOFS its low byte. Window 0 is the one
//  the editor reads structural bytes through. Clobbers A, X, Y, kPtr, srcPtr.
muWinSeat0:
    sta muWinOfs+1
    ldx #0
    jmp muWinSeat

// --- muWinPtr -- kPtr = window X's base ---
//  X is the voice on the way in and on the way out; Y and A are scratch. The three
//  sites that need a window's address come through here rather than repeat the
//  five instructions that turn a voice into a window high byte.
muWinPtr:
    txa
    asl
    tay
    lda muWin,y
    sta kPtr
    lda muWin+1,y
    sta kPtr+1
    rts

// --- muWinOut -- the voice back in X, and a refused transfer is reported ---
//  kReuReadAt and kReuWriteAt answer C=1 when the range is outside the file. A
//  refused READ leaves the previous page in the window and the band draws another
//  page's bytes; a refused WRITE loses the edit in the scratch copy, which is
//  contained (the .mus is not touched until a save) but means the save would later
//  report success for a file that does not hold the edit. Neither should happen --
//  the image never grows past MU_SCR_PAGES and every page written is one that was
//  read -- so the answer is the diagnosis, not a repair. Reporting it costs kArg
//  and kPtr (muMsgSet moves a slot), which is harmless here: the transfer it
//  describes is over.
muWinOut:
    bcc mwOutX
    lda #M_NO_WRITE             // the scratch copy refused the page
    jsr muMsgSet
mwOutX:
    ldx muScrV
    rts

// --- muPgArg -- the kArg block for a 256-byte REU transfer of page A ---
//  A page is a high byte and a zero, so the position is the page itself. Every
//  read and write of a whole page in this task goes through here: the caller
//  picks the direction (kReuReadAt or kReuWriteAt) and kPtr's window.
//  Clobbers A only. X and Y survive, and kArg+0/+1 (the file record) is the
//  caller's, so a site can set it before or after this call.
muPgArg:
    sta kArg+3
    lda #0
    sta kArg+2
    sta kArg+6
    lda #1
    sta kArg+7                  // len = 256
    rts

// --- muWinFill -- voice X's window := the scratch file's page muWinPage ---
//  The working image is <base>.wrk, so a page is one kReuReadAt of 256 bytes and
//  the window is page-aligned, which makes the position `page x 256` -- a high
//  byte and a zero.
muWinFill:
    stx muScrV                  // kReuReadAt clobbers X
    jsr muSFrecArg
    lda muWinPage
    jsr muPgArg
    jsr muWinPtr
    jsr kReuReadAt
    jmp muWinOut

// --- muWinInit -- take the three windows, once, at startup ---
//  Out: C=0 ready; C=1 no room (the caller refuses like any other shortage).
//  They are held for the task's life: the kernel frees a dyn task's blocks on exit,
//  and the windows are independent of whichever tune is open, so one set serves
//  every tune and no open un-does another.
muWinInit:
    lda #MU_WIN_PAGES
    jsr kMalloc
    bcs muWiRts
    ldy #0
    ldx #0
muWiLp:
    lda kPtr                    // every window's low byte is the block's own: the
    sta muWin,y                 //   block is page-aligned, so they differ only in
    iny                         //   the high byte
    txa
    clc
    adc kPtr+1
    sta muWin,y
    iny
    inx
    cpx #MU_WIN_PAGES
    bne muWiLp
    clc                         // the loop ends with C=1 (its own compare), and the
muWiRts:                        //   caller tests this carry for "no room"
    rts

// --- muWinReset -- forget what the windows hold (a new image) ---
muWinReset:
    ldx #0
    lda #0
muWrLp:
    sta muWinOk,x
    inx
    cpx #MU_WIN_PAGES
    bne muWrLp
    rts

// --- muWinFlush -- window X's page goes back to the working file ---
//  A window is a COPY, so a write has to reach the page it came from. One
//  kReuWriteAt of 256 bytes; muWinPg,x is the page the window holds.
//  Clobbers A, X, Y, kArg+0..7, srcPtr -- and X comes back as it went in.
muWinFlush:
    stx muScrV
    jsr muSFrecArg
    lda muWinPg,x
    jsr muPgArg
    jsr muWinPtr
    jsr kReuWriteAt
    jmp muWinOut

// ==========================================
//  The scratch file
// ==========================================
//  The working image is <base>.wrk in the REU filesystem: MU_LEN bytes of tune
//  plus the zeroes that pad it to a whole page. It is created at its full
//  MU_SCR_PAGES because kReuWriteAt refuses a write past EOF, so the image can
//  grow through an edit without the file being recreated, and it is dropped on
//  exit (muScrDrop) so a session leaves nothing behind. The real <base>.mus is
//  untouched until SAVE, so "quit without saving" stays safe.

// --- muSFArg -- kArg = the scratch name ---
muSFArg:
    lda #<muSFName
    sta kArg
    lda #>muSFName
    sta kArg+1
    rts

// --- muSFrecArg -- kArg = the scratch's five-byte record ---
muSFrecArg:
    lda #<muSFrec
    sta kArg
    lda #>muSFrec
    sta kArg+1
    rts

// --- muScrOpen -- kReuOpen <base>.wrk, creating it the first time ---
//  Out: C=0 muSFrec holds its record; C=1 it is not there and could not be made.
muScrOpen:
    jsr muSFArg
    lda #<muSFrec
    sta kPtr
    lda #>muSFrec
    sta kPtr+1
    jsr kReuOpen
    bcc muSoRts                 // it is there: reuse it, at whatever size it has
    jsr muSFArg
    lda #0
    sta kArg+2
    lda #MU_SCR_PAGES
    sta kArg+3                  // size = pages x 256
    jsr kReuCreate
    bcs muSoRts                 // no room, or the FS refused it
    jsr muSFArg                 // kReuCreate hands back no record, so open it again
    lda #<muSFrec
    sta kPtr
    lda #>muSFrec
    sta kPtr+1
    jsr kReuOpen
muSoRts:
    rts
// --- muSidCheck -- C=1 when another task is using sid.lib ---
//  libStartPage is $FF until the library is actually LOADED, and only a load
//  puts pages there; musedit declares sid.lib but does not call it until it
//  plays, so a loaded sid.lib means some other task is playing. libRefCount
//  cannot answer this: the loader counts EVERY declared library, musedit's own
//  declaration included. The shell refuses `run` of a task that declares
//  sid.lib while it is loaded, so this covers the spawn paths that bypass the
//  shell -- another task's kDynLoad.
muSidCheck:
    lda libStartPage+LIB_SID
    cmp #$ff
    beq muScFree
    sec
    rts
muScFree:
    clc
    rts

// --- muFatal -- say why we cannot run, hold it until a key, then leave ---
//  grpaint's gpFail/sidplay's spReport recipe: the message stays up until it is
//  acknowledged, and the exit goes through muQuit, which drains the release,
//  hands the display back and frees the surface.
muFatal:
    jsr kClearScreen
    ldx #0
    ldy #0
    jsr kSetCursor              // the only X/Y-preserving routine
    ldy #MU_STRLO
    lda (dataPtr),y
    sta kPtr
    ldy #MU_STRHI
    lda (dataPtr),y
    sta kPtr+1
    jsr kPrintString
    jsr kNewline
    jsr kWaitKey                // hold the reason on screen until acknowledged
    jmp muQuit

// --- muLoad -- the tune the name stands for, or a new one ---
//  Out: C=0 the working file holds the image; C=1 MU_STR holds the reason.
muLoad:
    lda reuFsEnabled            // kReuOpen's code 6 means not found OR no FS, so
    cmp #REU_FS_SIGNATURE       // presence has to be its own test -- otherwise every
    beq muLdTry                 // new tune would be refused as a missing filesystem
    lda #M_NO_FS
    jsr muMsgSet
    sec
    rts
muLdTry:
    jsr muFsArg
    jsr muFrecPt
    jsr kReuOpen
    bcc muLdOpen
    jmp muLdNew                 // not there: a new tune
muLdOpen:
    //  --- the file's size is the record's first two bytes ---
    jsr muFrecPt
    ldy #0
    lda (kPtr),y
    ldy #MU_FLEN
    sta (dataPtr),y
    ldy #1
    lda (kPtr),y
    ldy #MU_FLEN+1
    sta (dataPtr),y
    //  --- the image is the whole file ---
    ldy #MU_FLEN
    lda (dataPtr),y
    ldy #MU_LEN
    sta (dataPtr),y
    ldy #MU_FLEN+1
    lda (dataPtr),y
    ldy #MU_LEN+1
    sta (dataPtr),y
    jsr muScrOpen
    bcs muLdNoScr
    jsr muLdCopy                // the tune's pages -> the working file
    bcs muLdNoScr
    jmp muLdCheck
muLdNoScr:
    lda #M_NO_SCR
    jsr muMsgSet
    sec
    rts

// --- muLdCopy -- the tune file's pages go into the working file ---
//  Window 0 is the bounce buffer: page p of <base>.mus is read into it and written
//  to page p of <base>.wrk. A short last page is zeroed behind the data, which is
//  what a save writes back, so an untouched file still round-trips byte for byte.
muLdCopy:
    jsr muLdPages               // A = the pages MU_LEN needs, C=1 too big
    bcs muLcBad
    sta muScrCnt
    lda #0
    sta muScrPg
muLcLp:
    jsr muRecArg
    lda muScrPg
    jsr muPgArg
    jsr muWin0Ptr
    jsr kReuReadAt
    bcs muLcBad
    lda kArg+7                  // a full page: nothing to zero
    bne muLcWrote
    ldy kArg+6                  // a short one: zero from the end of the data, and
    lda #0                      //   the wrap at 256 ends the loop
muLcZero:
    sta (kPtr),y
    iny
    bne muLcZero
muLcWrote:
    lda muScrPg
    sta muWinPg                 // window 0 now holds this page of the working file
    ldx #0
    jsr muWinFlush
    inc muScrPg
    dec muScrCnt
    bne muLcLp
    clc
    rts
muLcBad:
    sec
    rts

// --- muWin0Ptr -- kPtr = window 0 (the bounce buffer) ---
muWin0Ptr:
    lda muWin
    sta kPtr
    lda muWin+1
    sta kPtr+1
    rts

// --- muLdPages -- A = the pages MU_LEN needs; C=1 more than the scratch has ---
muLdPages:
    ldy #MU_LEN
    lda (dataPtr),y
    clc
    adc #$ff
    ldy #MU_LEN+1
    lda (dataPtr),y
    adc #0
    bcs muLdPgBad               // len + 255 is past 16 bits: far bigger than the scratch
    bne muLdPgChk
    lda #1
muLdPgChk:
    cmp #MU_SCR_PAGES
    beq muLdPgOk
    bcs muLdPgBad
muLdPgOk:
    clc
    rts
muLdPgBad:
    sec
    rts

// --- muLdNew -- the blank tune a name with no file stands for ---
muLdNew:
    jsr muScrOpen
    bcc muLnGo
    jmp muLdNoScr               // out of branch range
muLnGo:
    jsr muWin0Ptr
    ldy #0
muLdNewLp:
    lda muNewTune,y
    sta (kPtr),y
    iny
    cpy #(muNewTuneEnd - muNewTune)
    bne muLdNewLp
    lda #0                      // the page behind the header is zeroes
muLdNewZ:
    sta (kPtr),y
    iny
    bne muLdNewZ
    lda #0
    sta muWinPg                 // window 0 holds page 0 of the working file
    ldx #0
    jsr muWinFlush
    ldy #MU_LEN
    lda #<(muNewTuneEnd - muNewTune)
    sta (dataPtr),y
    lda #0
    ldy #MU_LEN+1
    sta (dataPtr),y
    ldy #MU_FLEN                // nothing on disk yet
    sta (dataPtr),y
    ldy #MU_FLEN+1
    sta (dataPtr),y
    jmp muLdCheck

// --- muLdCheck -- the image has to be a tune before anything reads it ---
//  A voice is a stream of two byte commands ended by the pair 01 4F. A voice whose
//  last pair is something else has lost or gained a byte somewhere, so every pair
//  after that point is shifted and the walk draws commands the tune never wrote.
//  One test covers the whole family: a length that is not even has no last pair at
//  all, and a file too short to hold three terminated streams reads the zeroes the
//  copy left behind its data and fails the same way.
muLdCheck:
    jsr muLdVoEnds
    bcs muLdNotTune
    clc
    rts
muLdNotTune:
    lda #M_NO_TUNE
    jsr muMsgSet
    sec
    rts

// --- muLdVoEnds -- every voice ends on its HLT pair ---
//  In: the working file holds the image, window 0 free. Out: C=1 when a voice does
//  not end there. MU_SCANOFF is the running "voice start less two", so adding a
//  length lands on that voice's LAST PAIR and the sum is already the next voice's
//  "start less two" -- one add does both. Parity goes first because a stream of
//  pairs fills an even number of bytes, so an odd length has no last pair; testing
//  it first also keeps the offset that is read even, and so inside one window.
//  Clobbers A, X, Y, kPtr, MU_SCANOFF/V and the window state.
muLdVoEnds:
    lda #2
    sta muScanV                 // the size words sit at 2, 4 and 6: counted up
    lda #6
    sta muScanOff               // voice 1's start, 8, less two
    lda #0
    sta muScanOff+1
mveVoice:
    jsr muImgSeat               // window 0 := page 0, where the size words live
    ldy muScanV
    lda (kPtr),y
    and #1                      // an odd length has no last pair: refused here
    bne mveBad
    lda muScanOff
    clc
    adc (kPtr),y
    sta muScanOff
    iny
    lda muScanOff+1
    adc (kPtr),y
    sta muScanOff+1
    jsr muScanSeat              // window 0 := the page that pair lives on
    ldy #1
    lda (kPtr),y
    cmp #$4f                    // HLT is the pair 01 4F
    bne mveBad
    dey
    lda (kPtr),y
    cmp #1
    bne mveBad
    inc muScanV
    inc muScanV
    lda muScanV
    cmp #8
    bcc mveVoice
    clc
    rts
mveBad:
    sec
    rts

// --- muSvWrite -- MU_TMPL/H bytes of the working file go into the target ---
//  muFrec holds the target's record. Page by page: window 0 takes page p of the
//  working file and those bytes go out to the target, with a SHORT last write
//  because kReuWriteAt rejects a range that runs past the file's end.
//  Out: C=0 written; C=1 the filesystem refused it.
muSvWrite:
    lda muTmpL
    sta muCountLo
    lda muTmpH
    sta muCountHi
    lda #0
    sta muScrPg
muSwLp:
    lda muCountLo
    ora muCountHi
    beq muSwOk
    lda #0                      // len = 256, unless this is the last, short page
    sta muTmp2
    lda #1
    sta muTmp2+1
    lda muCountHi
    bne muSwLen
    lda muCountLo
    sta muTmp2
    lda #0
    sta muTmp2+1
muSwLen:
    jsr muSFrecArg              // window 0 := page muScrPg of the working file
    lda muScrPg
    jsr muPgArg
    jsr muWin0Ptr
    jsr kReuReadAt
    bcs muSwBad
    jsr muRecArg                // and out to the target at the same position
    lda #0
    sta kArg+2
    lda muScrPg
    sta kArg+3
    lda muTmp2
    sta kArg+6
    lda muTmp2+1
    sta kArg+7
    jsr muWin0Ptr
    jsr kReuWriteAt
    bcs muSwBad
    lda muCountLo               // what went is what was left
    sec
    sbc muTmp2
    sta muCountLo
    lda muCountHi
    sbc muTmp2+1
    sta muCountHi
    inc muScrPg
    jmp muSwLp
muSwOk:
    clc
    rts
muSwBad:
    sec
    rts

// --- muSaveNow -- write the working file back to <base>.mus ---
//  In place when the file can hold it -- the record already points at it -- and
//  otherwise created at a whole number of pages, so a later edit that grows the
//  image still saves in place.
//  Out: C=0 saved; C=1 MU_STR holds the reason.
muSaveNow:
    jsr muFsArg
    jsr muFrecPt
    jsr kReuOpen
    bcs muSvCreate
    //  --- the record's size is the file's: does the image fit? ---
    ldy #0
    lda (kPtr),y
    sta muTmpL
    ldy #1
    lda (kPtr),y
    sta muTmpH
    ldy #MU_LEN+1
    lda (dataPtr),y
    cmp muTmpH
    bcc muSvInPlace             // a smaller high byte: it fits
    bne muSvCreate              // ...or a larger one: it does not
    ldy #MU_LEN
    lda (dataPtr),y
    cmp muTmpL
    bcc muSvInPlace
    bne muSvCreate
muSvInPlace:
    jsr muSvWrite               // the file's whole length, out of the working file
    bcs muSvWriteBad
    clc
    rts
muSvWriteBad:
    lda #M_NO_WRITE
    jsr muMsgSet
    sec
    rts

muSvCreate:
    //  --- pages for the image; and the room for them BEFORE anything is written ---
    ldy #MU_LEN
    lda (dataPtr),y
    clc
    adc #$ff
    ldy #MU_LEN+1
    lda (dataPtr),y
    adc #0
    bne muSvPages
    lda #1
muSvPages:
    sta muNeedPage
    jsr muFreeSpace
    bcc muSvFsOk
    jmp muSvNoFs
muSvFsOk:
    lda muFreeHi
    bne muSvRoom
    lda muFreeMid
    cmp muNeedPage
    bcc muSvNoRoom
muSvRoom:
    //  The replacement is built under a scratch name and the tune is deleted only
    //  once that file is whole. A tombstone frees neither slot nor data, so the
    //  delete cannot make room for the create -- a directory with no free slot
    //  would take the tune and give nothing back.
    jsr muTmpArg
    jsr kReuDelete              // a scratch left by a failed rename is not in the way
    jsr muTmpArg
    lda #0
    sta kArg+2
    lda muNeedPage
    sta kArg+3                  // size = pages x 256
    jsr kReuCreate
    bcs muSvCreateBad           // nothing has been written and nothing deleted
    jsr muTmpArg
    jsr muFrecPt
    jsr kReuOpen                // it was just made, so this cannot fail
    bcs muSvScrBad
    lda #0
    sta muTmpL
    lda muNeedPage
    sta muTmpH                  // len = pages x 256
    jsr muSvWrite
    bcs muSvScrBad
    jsr muFsArg
    jsr kReuDelete              // the tune goes only now, its replacement whole
    lda #<muTmpName             // and the replacement takes its name
    sta kArg
    lda #>muTmpName
    sta kArg+1
    lda #<muFsName
    sta kArg+2
    lda #>muFsName
    sta kArg+3
    jsr kReuRename
    bcs muSvCreateBad           // the scratch holds the only copy: leave it there
    ldy #MU_FLEN                // the disk holds whole pages now
    lda #0
    sta (dataPtr),y
    ldy #MU_FLEN+1
    lda muNeedPage
    sta (dataPtr),y
    clc
    rts
muSvScrBad:
    jsr muTmpArg                // the half-made replacement goes; the tune is intact
    jsr kReuDelete
    jmp muSvWriteBad
muSvCreateBad:
    lda kArg+6
    cmp #1
    beq muSvNoRoom
    cmp #2
    beq muSvDirFull
    lda #M_NO_WRITE
    jsr muMsgSet
    sec
    rts
muSvNoFs:
    lda #M_NO_FS
    jsr muMsgSet
    sec
    rts
muSvNoRoom:
    lda #M_NO_ROOM
    jsr muMsgSet
    sec
    rts
muSvDirFull:
    lda #M_DIR_FULL
    jsr muMsgSet
    sec
    rts

// --- muFreeSpace -- capacity - nextFreeOffset, as far as a page count can tell ---
//  A delete frees neither slot nor data, so what a new file can use is the
//  capacity minus the allocator's high-water mark -- and that has to cover the new
//  file BEFORE the old one is deleted, or the tune is gone and its replacement
//  never arrives. Every file this task creates is a whole number of pages, so only
//  the two upper bytes of the difference can decide, and the low byte is dropped.
//  Out: C=0 ok (MU_FREEMID/MU_FREEHI); C=1 the filesystem is not there.
muFreeSpace:
    lda #<muDirName
    sta kPtr
    lda #>muDirName
    sta kPtr+1
    lda #0
    ldx #0
    jsr kReuDirEntry            // slot 0: kArg+3/+4/+5 = nextFreeOffset
    bcs muFsRts
    sec
    lda reuFsCapacityLo
    sbc kArg+3
    lda reuFsCapacityHi
    sbc kArg+4
    sta muFreeMid
    lda reuFsCapacity2
    sbc kArg+5
    sta muFreeHi
    clc
muFsRts:
    rts

// --- muSaveTry -- SAVE, from the key or from the bar ---
//  A `jsr` entry: it ends in a tail jump into the status line, whose last routine
//  returns, so the frame it returns through has to be this one.
muSaveTry:
    jsr muPlaying               // a save sets kArg up around its own REU transfers, and
    bcs muSvRts                 //   the player's window refill writes the same block from
    jsr muSaveNow               //   the other task: stop first (? stop first)
    bcc muSvTold
    jmp muStatus                // MU_STR is the reason
muSvTold:
    lda #0                      // a saved tune is clean again
    sta muDirty
    sta muWarn
    lda #M_SAVED
    jmp muMsgGo
muSvRts:
    rts

// ==========================================
//  Timing
// ==========================================
//  Every row on the grid is one row unit, and a row unit is the shortest note
//  the tempo can express. The arithmetic here is the player's own (sidplay.inc's
//  calculateNoteDuration), because a length musedit draws has to be a length the
//  player will play. tools/check-mus-timing.py re-implements the same routine
//  and sweeps all 8,192 tempo/class/state combinations.

// --- muTempoBase -- A = the triplet base for the tempo in force ---
//  DataTempoTable[tempo >> 3], the table musedit carries a copy of.
muTempoBase:
    lda muJifTem
    lsr
    lsr
    lsr
    tay
    lda muTempoTable,y
    rts

// --- muNoteJiffies -- how long the note with this duration byte plays ---
//  In:  A = the duration byte (bits 2-4 class, 5 dotted, 7 triplet)
//       muJifTem = the tempo in force, muUl / muUtvC the utility lengths
//  Out: C=0 A = jiffies; C=1 the tempo cannot express that length.
//  A length that would drop a bit off the end of a byte is refused, which is why
//  a 32nd at TEM $90 is not a value at all rather than 4.5 jiffies.
muNoteJiffies:
    sta muDurByte
    and #$1c                    // the class, bits 2-4
    lsr
    lsr
    bne muNjClass
    //  --- class 0: the 64th, the one length that is rotated rather than
    //      shifted, because at the tempos that allow it the whole note is only a
    //      few jiffies long ---
    lda muDurByte
    bmi muNjTri64
    lda muJifTem
    and #$3c
    bne muNjBad
    lda muJifTem
    asl
    rol
    rol
    bne muNjOk
    lda #4
muNjOk:
    clc
    rts
muNjTri64:
    jsr muTempoBase
    beq muNjBad
    sta muJifT
    and #$3f
    bne muNjBad
    lda muJifT
    asl
    rol
    rol
    bne muNjOk
muNjBad:
    sec
    rts
muNjClass:
    cmp #1
    bne muNjShift
    //  --- class 1: a utility length. Bit 5 picks UTV (this voice) over UTL
    //      (all voices), and the dots and the triplet bit play no part ---
    lda muDurByte
    and #$20
    bne muNjUtv
    lda muUl
    clc
    rts
muNjUtv:
    lda muUtvC
    clc
    rts
muNjShift:
    //  --- classes 2..7: the whole note down to the 32nd ---
    sta muJifCls
    lda muDurByte
    and #$a0
    cmp #$80
    beq muNjTri
    sta muJifSt                 // dotted bits: $00, $20 (dotted) or $A0
    clc
    lda muJifTem
    bne muNjRot
    sec                         // tempo 0 seeds the carry: that is what makes
muNjRot:                        //   its half note 128 rather than 0
    ldy muJifCls
    dey
    dey
    beq muNjRotDone
muNjRotLp:
    ror
    bcs muNjBad                 // a bit fell off the end: not a length
    dey
    bne muNjRotLp
muNjRotDone:
    ldy muJifSt
    sta muJifBase
    beq muNjOk                  // a zero base is a zero length
    lsr muJifBase
    bcs muNjBad                 // a dotted length needs an even base
    beq muNjBad
    adc muJifBase               // dotted: base + base/2
    bcs muNjBad
    iny
    bpl muNjOk                  // $20 became $21: a single dot
    lsr muJifBase               // double dotted adds base/4 as well
    bcs muNjBad
    adc muJifBase
    bcc muNjOk
    jmp muNjBad
muNjTri:
    jsr muTempoBase
    beq muNjBad
    ldy muJifCls
    dey
    dey
    beq muNjOk
muNjTriLp:
    lsr
    bcs muNjBad
    dey
    bne muNjTriLp
    clc
    rts

// --- muUnit -- A = the row unit in jiffies, X = the rows a bar has ---
//  The shortest length the tempo can express wins, and the rows a bar has follow
//  from which class won: a whole note is one bar, so the 8th is eight rows and
//  the 64th sixty-four. The 64th is tried first because it is the shortest of
//  them wherever the tempo allows it at all.
muUnit:
    lda #0
    sta muUnIdx
muUnLp:
    ldy muUnIdx                 // re-read: muNoteJiffies owns Y
    lda muUnitTab,y
    jsr muNoteJiffies
    bcc muUnHit
    inc muUnIdx
    lda muUnIdx
    cmp #MU_UNIT_N
    bcc muUnLp
    lda #0                      // unreachable: every tempo has a length
    ldx #0
    sec
    rts
muUnHit:
    ldy muUnIdx
    ldx muUnitRows,y
    clc
    rts

// --- muUnitNow -- the plain row unit, then apply the armed refinement ---
//  Plain is the shortest length the tempo can express, so a bar is 8/16/32/64
//  rows. SHIFT+U refines it: half, a third, then a single jiffy, each step only
//  where it divides exactly (muUnitCycle skips the ones that do not).
muUnitNow:
    lda muTempo
    sta muJifTem
    jsr muUnit                 // A = the unit, X = the rows a bar has
    sta muPlainU
    stx muPlainBar
    lda #0
    sta muUnitRef
    jmp muUnitApply             // the opening segment's unit and bar, which the walk
                                //   seeds from: a tempo with no refinement still
                                //   needs them set

// --- muRefine -- A = plain unit, X = plain rows a bar -> MU_FITU, MU_FITBAR ---
//  The armed refinement, applied where it divides: a half needs an even unit, a
//  third a multiple of three, and one jiffy a row always fits, so its bar is
//  plainU * plainBar rows. A tempo whose plain unit does not divide keeps plain,
//  which is what a mid-tune change can do to a refinement chosen elsewhere.
muRefine:
    sta muFitP
    stx muFitPb
    lda #0
    sta muFitBarHi
    lda muUnitRef
    beq mrPlain
    cmp #1
    beq mrHalf
    cmp #2
    beq mrThird
    //  --- one jiffy per row ---
    lda #1
    sta muFitU
    lda #0
    sta muFitBar
    ldx muFitPb
    beq mrRts                   // a bar of no rows never happens
mrMulLp:
    lda muFitBar
    clc
    adc muFitP
    sta muFitBar
    bcc mrMulN
    inc muFitBarHi
mrMulN:
    dex
    bne mrMulLp
mrRts:
    rts
mrPlain:
    lda muFitP
    sta muFitU
    lda muFitPb
    sta muFitBar
    rts
mrHalf:
    lda muFitP
    lsr
    bcs mrPlain                 // an odd unit has no half
    sta muFitU
    lda muFitPb
    asl
    sta muFitBar
    lda #0
    rol
    sta muFitBarHi
    rts
mrThird:
    lda muFitP
    ldx #0
mrT3Lp:
    cmp #3
    bcc mrT3D
    sec
    sbc #3
    inx
    jmp mrT3Lp
mrT3D:
    bne mrPlain                 // a remainder: no third at this tempo
    stx muFitU
    lda muFitPb
    asl                         // the bar is plainBar * 3, and plainBar is a power
    clc                         //   of two, so the doubled value cannot carry
    adc muFitPb
    sta muFitBar
    lda #0
    sta muFitBarHi
    rts

// --- muFit -- A = a tempo -> MU_FITU, MU_FITBAR: the grid as it counts there ---
//  The grid's own MU_UNITVAL/MU_UNITBAR are left alone, because the walk carries
//  one segment and this answers for a tempo it met somewhere else in the tune.
//  Clobbers MU_JIFTEM and muNoteJiffies' scratch.
muFit:
    sta muJifTem
    jsr muUnit                 // A = the plain unit, X = the rows a bar has
    jmp muRefine

// --- muUnitApply -- the opening segment's unit and bar, from MU_PLAINU/PLAINBAR ---
muUnitApply:
    lda muPlainU
    ldx muPlainBar
    jsr muRefine
    lda muFitU
    sta muUnitVal
    lda muFitBar
    sta muUnitBar
    lda muFitBarHi
    sta muUnitBarHi
    rts

// --- muUnitCycle -- SHIFT+U: the next finer unit that divides exactly ---
muUnitCycle:
    lda muUnitRef
    cmp #3
    bcs muUcWrap
    inc muUnitRef
    jsr muUcValid
    bcs muUcSkip
    jmp muUnitApply             // exact: apply it
muUcSkip:
    jmp muUnitCycle             // skip to the next one
muUcWrap:
    lda #0
    sta muUnitRef
    jmp muUnitApply
muUcValid:                      // C=1 when this refinement does not divide
    lda muUnitRef
    beq muUcOk
    cmp #1
    beq muUcHalf
    cmp #2
    beq muUcThird
muUcOk:
    clc
    rts
muUcHalf:
    lda muPlainU
    and #1
    beq muUcOk
    sec
    rts
muUcThird:
    lda muPlainU
muUcT3Lp:
    cmp #3
    bcc muUcT3D
    sec
    sbc #3
    jmp muUcT3Lp
muUcT3D:
    cmp #0                      // nothing left over: the third is exact
    bne muUcNo
    clc
    rts
muUcNo:
    sec
    rts

// --- muTrScan -- does any length in the streams carry bit 7? ---
//  A pair's FIRST byte is its length only when its low two bits are 00: the player
//  routes on them (01, 10 and 11 are commands), and a command byte can carry bit 7
//  as part of its own value, so bit 7 alone is not a length. With bit 7 set on a
//  length the note is a triplet (or, with bit 5, the second dot).
//  The three streams are contiguous: voice 1 starts at 8 and each length word adds
//  the size of the voice before the next, so the range is [8, 8 + the three words)
//  and every second byte in it, counting from 8, is a pair's first byte.
//  Out: C=1 a triplet or second dot was found. Clobbers A, X, Y, kPtr, srcPtr and
//  muWinOfs -- it is called before the first draw, when nothing holds either.
muTrScan:
    jsr muImgSeat               // kPtr = the image, for the three length words
    lda #8
    sta muTrCur
    sta muTrEnd
    lda #0
    sta muTrCur+1
    sta muTrEnd+1
    ldy #2
muTsSum:
    lda (kPtr),y
    clc
    adc muTrEnd
    sta muTrEnd
    iny
    lda (kPtr),y
    adc muTrEnd+1
    sta muTrEnd+1
    iny
    cpy #8
    bcc muTsSum
muTsPair:
    lda muTrCur
    cmp muTrEnd
    lda muTrCur+1
    sbc muTrEnd+1
    bcs muTsNo
    lda muTrCur
    sta muWinOfs
    lda muTrCur+1
    sta muWinOfs+1
    lda #0
    tax
    jsr muWinSeat               // any window holds any page: voice 0's is free here
    ldy #0
    lda (kPtr),y
    and #$83                    // bit 7 and the two that say "not a command"
    cmp #$80
    beq muTsYes
    lda muTrCur                 // the next PAIR is two bytes on
    clc
    adc #2
    sta muTrCur
    bcc muTsPair
    inc muTrCur+1
    jmp muTsPair
muTsYes:
    sec
    rts
muTsNo:
    clc
    rts

// --- muFindTempo -- the tempo in force when the first note plays ---
//  The player runs each voice's leading commands before any of them reaches a
//  note, so the tempo the first note hears is the LAST TEM of that leading run,
//  taken in voice order. A note (bits 0-1 clear) or a HLT ends a voice's run.
muFindTempo:
    lda #MU_INIT_TEM            // the player's own default, for a tune with no TEM
    sta muTempo
    sta muJifTem
    lda #0
    sta muScanV
    sta muScanOff+1
    lda #8
    sta muScanOff               // voice 1 starts right after the header
muFtVoice:
    //  the voice's size word: image offset 2 + 2 * voice, always page 0
    jsr muImgSeat
    lda muScanV
    asl
    clc
    adc #2
    tay
    lda (kPtr),y
    sta muScanLen
    iny
    lda (kPtr),y
    sta muScanLen+1
    lda muScanLen
    sta muScanN
    lda muScanLen+1
    sta muScanN+1
muFtWalk:
    lda muScanN
    ora muScanN+1
    beq muFtVoiceDone           // the stream ran out
    jsr muScanSeat              // kPtr = the 2-byte event about to be read
    ldy #0
    lda (kPtr),y
    and #$03
    beq muFtVoiceDone           // a note: the leading run is over
    lda (kPtr),y
    cmp #$01
    beq muFtVoiceDone           // HLT
    cmp #$06
    bne muFtSkip
    ldy #1
    lda (kPtr),y                // the TEM option
    sta muTempo
muFtSkip:
    lda muScanOff
    clc
    adc #2
    sta muScanOff
    bcc muFtNoHi
    inc muScanOff+1
muFtNoHi:
    lda muScanN
    sec
    sbc #2
    sta muScanN
    bcs muFtWalk
    dec muScanN+1
    dec muScanN                 // the low byte borrowed
    jmp muFtWalk
muFtVoiceDone:
    //  the next voice starts right after this one
    lda muScanOff
    clc
    adc muScanLen
    sta muScanOff
    lda muScanOff+1
    adc muScanLen+1
    sta muScanOff+1
    inc muScanV
    lda muScanV
    cmp #3
    bcs muFtRts                 // out of branch range: invert and jump
    jmp muFtVoice
muFtRts:
    rts

// --- muScanSeat -- kPtr = the image at MU_SCANOFF, in window 0 ---
//  muFindTempo's walk: every stream starts on an even offset and every event is
//  two bytes, so the pair it reads can never straddle a page.
muScanSeat:
    txa
    pha
    lda muScanOff
    sta muWinOfs
    lda muScanOff+1
    jsr muWinSeat0
    pla
    tax
    rts

// ==========================================
//  The simulator
// ==========================================
//  The player's control flow, resumed rather than restarted: every voice has a
//  pointer into the image and the tick its next event plays on, and a step
//  executes whichever voice is earliest (ties to the lower voice, the order
//  sidplay.inc processes a tick in). Commands cost no time, so a step runs
//  through them until a note or rest has been consumed and its length added to
//  that voice's tick.
//
//  The structural commands behave exactly as sidplay.inc's do: HED records the
//  repeat count and the address after it, TAL jumps back while the count lasts,
//  DEF records a phrase and pushes a sentinel frame so its body plays inline,
//  CALL pushes a frame and jumps, END pops (and ends the song at the outermost
//  frame), HLT stops the voice. A player error (an illegal length, a call with
//  no phrase, a full stack, a TAL in another phrase) stops every voice, which is
//  what SID_STATUS does.
//
//  All of it lives in the code image, which the loader copies per instance.

// --- muSimInit -- rewind the walk to tick 0 over the whole image ---
muSimInit:
    lda #MU_INIT_TEM
    sta muSimT
    lda #MU_INIT_UL
    sta muUl
    lda #0
    sta muClkLo
    sta muClkHi
    sta muErr
    ldx #23
muSiDfLp:
    lda #0
    sta muDfHi,x                // no phrase is defined until a DEF says so
    lda #$ff
    sta muDfId,x
    dex
    bpl muSiDfLp
    ldx #14
muSiFrLp:
    lda #$ff
    sta muPhSt,x                // every frame reads as the DEF sentinel
    lda #0
    sta muPhHi,x
    dex
    bpl muSiFrLp
    ldx #2
muSiVoLp:
    lda #1
    sta muOn,x
    sta muUtv,x
    lda muBaseTab,x
    sta muDep,x                 // the next free frame for this voice
    txa
    sta muId,x                  // WorkVoiceForCallPhrase starts as the voice #
    sta muHedId,x
    lda #0
    sta muTkLo,x
    sta muTkHi,x
    sta muPhDf,x                // every voice starts at the top level stream
    sta muHedN,x
    sta muEvLo,x
    sta muEvHi,x
    dex
    bpl muSiVoLp
    //  --- the three stream starts: image + 8, then each length word ---
    jsr muImgSeat               // for the length words below, which are read straight
    lda #8                      //   out of the image
    sta muAccLo                 // the song proper starts at file offset 8: 2 bytes of
    lda #0                      //   load address and three size words ahead of the
    sta muAccHi                 //   first event, so this is where voice 1 begins
    ldx #0
muSiPtrLp:
    lda muAccLo
    sta muPtrLo,x
    sta muHedLo,x               // HED's return address starts at the stream
    lda muAccHi
    sta muPtrHi,x
    sta muHedHi,x
    cpx #2
    beq muSiPtrRts
    txa
    asl
    clc
    adc #2
    tay                         // the length word's offset in the header
    lda (kPtr),y
    clc
    adc muAccLo
    sta muAccLo
    iny
    lda (kPtr),y                // lda leaves the carry of the add above alone
    adc muAccHi
    sta muAccHi
    inx
    jmp muSiPtrLp
muSiPtrRts:
    //  --- the tempo timeline starts at tick 0 with the opening tempo's segment ---
    lda muUnitVal               // the unit the grid opens with, from MU_TEMPO
    sta muSegUnit
    lda muUnitBar
    sta muSegBar
    lda muUnitBarHi
    sta muSegBar+1
    lda #0
    sta muSegTick
    sta muSegTick+1
    sta muSegRow
    sta muSegRow+1
    sta muBarStart
    sta muBarStart+1
    lda #1
    sta muBarNo
    lda #0
    sta muBarNo+1
    rts

// --- muSegRows -- MU_VALLO/HI = jiffies -> MU_SPAN = rows, MU_VALLO = remainder ---
//  Repeated subtraction, and the dividend is one event's length at most, so the
//  loop turns once per row the event covers rather than once per segment.
muSegRows:
    lda #0
    sta muSpan
    sta muSpan+1
msrLp:
    lda muValHi
    bne msrSub
    lda muValLo
    cmp muSegUnit
    bcc msrDone
msrSub:
    lda muValLo
    sec
    sbc muSegUnit
    sta muValLo
    bcs msrInv
    dec muValHi
msrInv:
    inc muSpan
    bne msrLp
    inc muSpan+1
    jmp msrLp
msrDone:
    rts

// --- muSegOpen -- a tempo change opens the next segment of the row map ---
//  In: A = the new tempo, MU_CLKLO/HI = the tick it takes effect at, MU_SV = the
//      voice that executed it. That voice's own row is the row of that tick: a
//      command takes no time, so the voice's next event still starts there, which
//      is what makes the new segment begin exactly where the old one ended.
//  Every live voice is re-seeded: its row so far was accumulated under the old
//  unit, and its next event may be after the change, so the row of that tick has
//  to be asked of the new segment.
muSegOpen:
    jsr muFit                   // A = the tempo -> MU_FITU, MU_FITBAR
    lda muFitU
    sta muSegUnit
    lda muFitBar
    sta muSegBar
    lda muFitBarHi
    sta muSegBar+1
    lda muClkLo
    sta muSegTick
    lda muClkHi
    sta muSegTick+1
    ldx muSv
    lda muVRow,x                // the row this tick is drawn on
    sta muSegRow
    lda muVRowHi,x
    sta muSegRow+1
    lda muSegRow                // a change ON a bar line has already closed one, so
    cmp muBarStart              //   the count rises only when it lands inside a bar
    bne msoCut
    lda muSegRow+1
    cmp muBarStart+1
    beq msoKeep
msoCut:
    inc muBarNo
    bne msoKeep
    inc muBarNo+1
msoKeep:
    lda muSegRow                // the next bar starts here, so a cut one is short
    sta muBarStart
    lda muSegRow+1
    sta muBarStart+1
    ldx #2
msoLp:
    lda muOn,x
    beq msoNext
    lda muTkLo,x
    sec
    sbc muSegTick
    sta muValLo
    lda muTkHi,x
    sbc muSegTick+1
    sta muValHi
    jsr muSegRows               // the rows from the change to that event
    lda muSegRow
    clc
    adc muSpan
    sta muVRow,x
    lda muSegRow+1
    adc muSpan+1
    sta muVRowHi,x
    lda muValLo
    sta muVRem,x
msoNext:
    dex
    bpl msoLp
    rts

// --- muSimNext -- the voice whose next event is earliest ---
//  Out: X = the voice, C=0; C=1 when no voice is playing any more.
muSimNext:
    lda #$ff
    sta muSv
    sta muBestHi
    sta muBestLo
    ldx #0
muSnLp:
    lda muOn,x
    beq muSnNext
    lda muTkHi,x
    cmp muBestHi
    bcc muSnTake
    bne muSnNext
    lda muTkLo,x
    cmp muBestLo
    bcs muSnNext                // equal ticks keep the lower voice
muSnTake:
    lda muTkHi,x
    sta muBestHi
    lda muTkLo,x
    sta muBestLo
    stx muSv
muSnNext:
    inx
    cpx #3
    bcc muSnLp
    ldx muSv
    cpx #$ff
    beq muSnNone
    clc
    rts
muSnNone:
    sec
    rts

// --- muPtrToK -- kPtr = the address of voice X's next event ---
//  X = the voice. The pointer is a FILE OFFSET, so the address comes from that
//  voice's window (see muWinSeat): the editor never holds the image in one piece.
//  Clobbers A, Y, kPtr, srcPtr -- and C comes back as 0.
muPtrToK:
    lda muPtrLo,x
    sta muWinOfs
    lda muPtrHi,x
    sta muWinOfs+1
    jmp muWinSeat               // tail call: X is already the voice

// --- muPtrBump -- voice X's pointer moves on to the next event ---
muPtrBump:
    lda muPtrLo,x
    clc
    adc #2
    sta muPtrLo,x
    bcc muPtrBumpR
    inc muPtrHi,x
muPtrBumpR:
    rts

// --- muStep -- execute the earliest voice's next timed event ---
//  Out: C=1 when every voice has stopped. muClkLo/Hi hold the tick the event
//  played on and MU_SV the voice that played it.
muStep:
    lda #0
    sta muNoteOut               // a HLT returns without one
    jsr muSimNext
    bcc muStepGo
    sec
    rts
muStepGo:
    stx muSv
    lda muTkLo,x
    sta muClkLo
    lda muTkHi,x
    sta muClkHi
    lda #MU_SPIN
    sta muSpun
muStepRun:
    ldx muSv
    jsr muPtrToK
    ldy #0
    lda (kPtr),y
    sta muScmd
    iny
    lda (kPtr),y
    sta muSopt
    jsr muPtrBump
    lda muScmd
    and #$03
    beq muStepNote
    jsr muLnLog                 // the grid draws it on a line of its own
    dec muSpun
    bne muSpunOk
    lda #MU_ERR_SPIN
    sta muErr
    jmp muSimStop               // a run of commands with no note behind it
muSpunOk:
    jmp muSimCmd
muStepNote:
    ldx muSv
    lda muUtv,x
    sta muUtvC
    lda muSimT
    sta muJifTem
    lda muScmd
    jsr muNoteJiffies
    bcc muStepLen
    lda #MU_ERR_LEN             // the player refuses this length and stops
    sta muErr
    ldx muSv                    // the flag: the voice and the onset row it died on
    stx muErrV                  //   (the row is live only under the grid's walk,
    lda muVRow,x                //   the one consumer that reads it)
    sta muErrRow
    lda muVRowHi,x
    sta muErrRow+1
    jmp muSimStop
muStepLen:
    ldx muSv
    sta muJifOut                // the length, so a caller can measure the note
    lda muScmd                  // the duration byte, for the grid's readout
    sta muVDur,x
    lda #1
    sta muNoteOut               // a note came back: the caller may measure it
    lda #0
    sta muJifOut+1
    lda muJifOut
    bne muStepSome
    inc muJifOut+1              // a zero countdown is 256 jiffies
muStepSome:
    lda muJifOut
    clc
    adc muTkLo,x
    sta muTkLo,x
    bcc muStepCount
    inc muTkHi,x
muStepCount:
    inc muEvLo,x
    bne muStepCountR
    inc muEvHi,x
muStepCountR:
    clc
    rts

// --- muSoptHi -- A = the current option byte high nibble ---
//  The value-wide commands carry their top bits there (see muCmdHED above), and the
//  phrase commands take their number from it.
muSoptHi:
    lda muSopt
    lsr
    lsr
    lsr
    lsr
    rts

// --- muSimCmd -- run one command for the voice in MU_SV, then walk on ---
//  Every dispatch here is a jump: the handlers sit a few hundred bytes below,
//  which is past a branch's reach.
muSimCmd:
    lda muScmd
    cmp #MU_CMD_HED
    bne muCmN1
    jmp muCmHed
muCmN1:
    cmp #MU_CMD_TEM
    bne muCmN2
    jmp muCmTem
muCmN2:
    cmp #MU_CMD_UTL
    bne muCmN3
    jmp muCmUtl
muCmN3:
    cmp #MU_CMD_UTV
    bne muCmN4
    jmp muCmUtv
muCmN4:
    lda muScmd
    and #$03
    cmp #1
    beq muCmOpt
    jmp muStepRun               // a 2/3-type command: no effect on timing
muCmOpt:
    lda muSopt
    cmp #MU_OPT_HLT
    bne muCmO1
    jmp muCmHlt
muCmO1:
    cmp #MU_OPT_TAL
    bne muCmO2
    jmp muCmTal
muCmO2:
    cmp #MU_OPT_END
    bne muCmO3
    jmp muCmEnd
muCmO3:
    and #$07
    cmp #$03
    bne muCmCd15
    lda muSopt
    bmi muCmPhr                 // bit 7 set: the 16..23 phrase form
    jmp muStepRun               // otherwise the mnemonic group has no timing effect
muCmPhr:
    jsr muSoptHi
    and #$07
    ora #16                     // phrases 16..23, the form that grows bit 3
    sta muSph
    lda muSopt
    and #$08
    bne muCmCallG
    jmp muCmDef
muCmCallG:
    jmp muCmCall
muCmCd15:
    lda muSopt
    and #$0f
    cmp #$02
    beq muCmC15
    cmp #$06
    beq muCmD15
    jmp muStepRun
muCmC15:
    jsr muSoptHi
    sta muSph
    jmp muCmCall
muCmD15:
    jsr muSoptHi
    sta muSph
    jmp muCmDef

muCmTem:
    lda muSopt
    cmp muSimT                  // the same tempo opens no segment and re-seeds
    beq muCmTemSame             //   nothing, which a tune that sets TEM at the top
    jsr muSegOpen               //   of every pass does once a pass
muCmTemSame:
    lda muSopt
    sta muSimT
    jmp muStepRun
muCmUtl:
    lda muSopt
    sta muUl
    jmp muStepRun
muCmUtv:
    ldx muSv
    lda muSopt
    sta muUtv,x
    jmp muStepRun

muCmHed:
    ldx muSv
    lda muSopt
    sta muHedN,x
    lda muPtrLo,x
    sta muHedLo,x
    lda muPtrHi,x
    sta muHedHi,x
    lda muId,x
    sta muHedId,x
    jmp muStepRun
muCmTal:
    ldx muSv
    lda muHedN,x
    beq muCmTalBack             // a count of 0 repeats for ever
    dec muHedN,x
    bne muCmTalBack
    jmp muStepRun               // the count just ran out: carry on past the TAL
muCmTalBack:
    ldx muSv
    lda muId,x
    cmp muHedId,x
    beq muCmTalGo
    lda #MU_ERR_TAL             // a TAL whose HED belongs to another phrase
    sta muErr
    jmp muSimStop
muCmTalGo:
    lda muHedLo,x
    sta muPtrLo,x
    lda muHedHi,x
    sta muPtrHi,x
    jmp muStepRun
muCmHlt:
    ldx muSv
    lda #0
    sta muOn,x
    clc
    rts
muCmEnd:
    ldx muSv
    lda muBaseTab,x
    cmp muDep,x
    bne muCmEndGo2
    jmp muStepRun               // the outermost frame: the player's stopSong sets
                                //   SID_STATUS bit 5 and RETURNS, so the voice
                                //   carries on at the pair after the END. Stopping
                                //   the walk here ends the grid at the END while the
                                //   music plays on
muCmEndGo2:
    dec muDep,x
    lda muDep,x
    tay
    lda muPhStk,y               // the frame's own enclosing phrase: whichever kind of
    sta muPhDf,x                //   body is ending, this is the one we return to
    lda muPhSt,y                // WARNING: $ff, not a zero high byte -- a return
    cmp #$ff                    //   address is a file OFFSET, so a return into the
    beq muCmEndSent             //   first 256 bytes has a zero muPhHi
    lda muPhHi,y
    sta muPtrHi,x
    lda muPhLo,y
    sta muPtrLo,x
    lda muPhSt,y
    sta muId,x
    jmp muStepRun
muCmEndSent:
    jmp muStepRun               // the DEF sentinel: carry on after the END

muCmCall:
    ldx muSv
    lda muBaseTab,x
    clc
    adc #MU_FRAMES
    cmp muDep,x
    bne muCmCallGo
    lda #MU_ERR_STK             // the call stack is full: the PLAYER flags it
    sta muErr                   //   (MaxInnerCallsError) and drops the call, so the
    jmp muStepRun               //   voice simply carries on
muCmCallGo:
    //  --- the return frame is pushed BEFORE the phrase is looked at, which is what
    //      the player does: it stores the return address, and only then finds out
    //      the phrase was never defined. So an undefined call leaves the frame
    //      behind, and this voice's next END pops it and jumps back to just after
    //      the call. Skipping the push here puts the walk one frame out of step
    //      with the player for the rest of the tune -- the voice is then walked
    //      down the wrong branch: its notes land on rows that are not shown, the
    //      playhead stalls on one of them, and after that it drifts.
    lda muDep,x
    tay
    lda muPtrLo,x
    sta muPhLo,y
    lda muPtrHi,x
    sta muPhHi,y
    lda muId,x
    sta muPhSt,y
    lda muPhDf,x                // the phrase this call is made from: END comes back
    sta muPhStk,y               //   to it, and a push never leaves it behind
    inc muDep,x
    ldy muSph
    lda muDfId,y                // WARNING: $ff, never a zero high byte -- these are
    cmp #$ff                    //   file OFFSETS, so every body below offset 256
                                //   has a zero muDfHi and would read as undefined
    bne muCmCallGo2
    lda #MU_ERR_PHR             // a phrase that was never defined: the PLAYER only
    sta muErr                   //   records it (NoSuchPhraseError -> SID_STATUS) and
    jmp muStepRun               //   carries on, leaving the frame it just pushed
muCmCallGo2:
    lda muDfLo,y
    sta muPtrLo,x
    lda muDfHi,y
    sta muPtrHi,x
    lda muDfId,y
    sta muId,x
    lda muSph
    clc
    adc #1                      // the phrase this voice is now in, as the stored form
    sta muPhDf,x
    jmp muStepRun

muCmDef:
    ldx muSv
    ldy muSph
    lda muPtrLo,x
    sta muDfLo,y
    lda muPtrHi,x
    sta muDfHi,y
    lda muId,x
    sta muDfId,y
    lda muBaseTab,x
    clc
    adc #MU_FRAMES
    cmp muDep,x
    bne muCmDefGo
    lda #MU_ERR_STK             // the call stack is full: the PLAYER flags it
    sta muErr                   //   (MaxInnerCallsError) and goes on without the
    jmp muStepRun               //   frame, so the body is still played inline
muCmDefGo:
    lda muDep,x
    tay
    lda #0
    sta muPhHi,y
    lda muPhDf,x                // the phrase this DEF sits in: its END comes back to it
    sta muPhStk,y
    lda #$ff                    // the sentinel: this frame returns to nothing, and
    sta muPhSt,y                //   $ff is the only value a real frame cannot hold
    inc muDep,x
    lda muSph                   // the phrase this voice is now in, as the stored form
    clc
    adc #1
    sta muPhDf,x
    jmp muStepRun

// --- muSimStop -- silence every voice: the song's end, or an error the player
//     treats as fatal ---
//  MU_ERR says which one it was (0 = the walk simply ran out of events). The
//  player's phrase errors are NOT among them: it records those in SID_STATUS and
//  keeps playing, so the walk flags them and carries on instead (see muCmCall).
//  C=1: the stopping event never played, so the caller must not measure it.
muSimStop:
    lda #0
    sta muOn
    sta muOn+1
    sta muOn+2
    sta muNoteOut               // the walk stopped on an error: no note came back
    sec
    rts

// ==========================================
//  The command log: which pairs the grid draws, and what they read as
// ==========================================
//  Every pair the walk reads passes through muLnLog, which asks muLnDec what it is
//  and stores (id, value) in THIS VOICE's block -- a line carries one command from
//  each lane, so the k-th command of every voice goes on the row's k-th command
//  line. A pair the grid does not draw stores nothing.
//
//  muLnDec mirrors the player's own routing (RouteCommand in sidplay.inc): the
//  command byte decides the family, the option byte picks the command, and the
//  tests are in the order that keeps the overlapping patterns straight.

// --- muLnLog -- record the pair the walk just read, if the grid draws it ---
//  In: MU_SCMD/MU_SOPT (the pair), MU_SV (the voice).
muLnLog:
    jsr muLnDec                 // A = the id, value in MU_LNVLO/HI; 0 = not shown
    beq muLlRts
    sta muTmpH                  // the id
    cmp #MU_MN_FC               // ids 1..9 are the structural nine
    lda muSv
    bcs mlbSound
    asl                         // block = voice * 2 + class: the two classes never
    jmp mlbHave                 //   share a line, so each voice has two queues
mlbSound:
    asl
    ora #1
mlbHave:
    tax
    lda muLogN,x
    cmp #MU_LOG_SLOTS
    bcs muLlRts                 // full: the pair has no line this row
    clc
    adc muLogBase,x             // slot = this voice's block + how many it has
    tay
    lda muTmpH
    sta (muLogIdP),y
    lda muLnVLo
    sta (muLogLoP),y
    lda muLnVHi
    sta muLogHi,y
    inc muLogN,x
muLlRts:
    rts

// --- muHeapPtrs -- the pointers into the instance buffer's logged-command arrays ---
//  The three arrays are the largest thing the editor kept in its code image (108
//  bytes, which is pool pages); they belong to the instance buffer instead, which
//  costs no pool pages at all. One zero-page pointer per array keeps every access a
//  single zp,Y instruction -- a byte shorter than the abs,Y it replaced. dataPtr is
//  the task's own and does not move, so this runs once, at entry.
muHeapPtrs:
    lda dataPtr
    clc
    adc #MU_LOGID
    sta muLogIdP
    lda dataPtr+1
    adc #0
    sta muLogIdP+1
    lda dataPtr
    clc
    adc #MU_LOGLO
    sta muLogLoP
    lda dataPtr+1
    adc #0
    sta muLogLoP+1
    rts

// --- muLnDec -- A = the mnemonic id for MU_SCMD/MU_SOPT, 0 = the grid hides it ---
//  Out: A = the id, MU_LNVLO/MU_LNVHI = the value to show (a nibble, a byte, or a
//  14-bit portamento). Clobbers A, X, Y and MU_TMPH.
muLnDec:
    lda muScmd
    and #$07
    cmp #$06
    beq muDecExact
    lda muScmd                  // P-W and DTN keep their high bits in the command
    and #$0f                    //   byte; POR its top six
    cmp #$02
    beq muDecHi12
    cmp #$0a
    beq muDecDtn12
    lda muScmd
    and #$03
    cmp #$03
    beq muDecPor
    cmp #$01
    bne muDecHide               // a 2-type command: nothing this grid shows
    jmp muDecOpt                // the $01 family: the OPTION picks the command
muDecExact:
    lda muScmd                  // each of these commands is one value of the low
    lsr                         //   three bits of the command byte, and the whole
    lsr                         //   option is its value
    lsr
    tay
    lda muDecTab,y
    beq muDecHide               // a value of that eight that names no command
    sta muTmpH
    lda muSopt
    sta muLnVLo
    lda #0
    sta muLnVHi
    lda muScmd                  // MS# alone keeps its value's high byte in the
    and #$3f                    //   command byte's top two bits
    cmp #$1e
    bne mdeRts
    lda muScmd
    lsr
    lsr
    lsr
    lsr
    lsr
    lsr
    sta muLnVHi
mdeRts:
    lda muTmpH
    rts
muDecHi12:
    lda muScmd                  // P-W: high nibble of the command, then the option
    lsr
    lsr
    lsr
    lsr
    sta muLnVHi
    lda muSopt
    sta muLnVLo
    lda #MU_MN_PW
    rts
muDecDtn12:
    lda muScmd
    lsr
    lsr
    lsr
    lsr
    sta muLnVHi
    lda muSopt
    sta muLnVLo
    lda #MU_MN_DTN
    rts
muDecPor:
    lda muScmd                  // POR: six bits of the command, then the option
    lsr
    lsr
    sta muLnVHi
    lda muSopt
    sta muLnVLo
    lda #MU_MN_POR
    rts
muDecHide:
    lda #0
    rts

// --- the $01 family: the option byte says which command, and its value ---
//  The tests follow the player's own routing, so a pattern that could name two
//  commands lands on the one the player executes. The test chain branches only to
//  the stubs directly below it; the bodies past those are reached by jmp.
muDecOpt:
    lda muSopt
    and #$07
    cmp #$03
    bne moChain                 // not the mnemonic group
    lda muSopt
    bpl moChain                 // bit 7 clear: an ordinary sound command there
    jsr muSoptHi                // the wide phrase form: bits 4-6 are phrase - 16, and
                                //   bit 3 picks CALL from DEF. muObjEnc writes it, so
                                //   the renderer has to read it
    and #$07
    ora #16
    sta muLnVLo
    lda #0
    sta muLnVHi
    lda muSopt
    and #$08
    bne moWideCall
    lda #MU_MN_DEF
    rts
moWideCall:
    lda #MU_MN_CALL
    rts
moChain:
    lda muSopt
    cmp #$0f
    beq moTal
    cmp #$2f
    beq moEnd
    cmp #$4f
    beq moHlt
    lda muSopt
    and #$07
    cmp #$01
    beq moOneS
    cmp #$05
    beq moOneS
    cmp #$02
    beq moPair2
    cmp #$06
    beq moPair6
    cmp #$04
    beq moAtk
    cmp #$07
    beq moWav
    cmp #$03
    beq moSwS
    lda #MU_MN_DCY               // opt & 7 == 0: DCY
    jmp moNib
moOneS:
    jmp moOne
moSwS:
    jmp moSw
moTal:
    lda #MU_MN_TAL
    jmp moNoVal
moEnd:
    lda #MU_MN_END
    jmp moNoVal
moHlt:
    lda #MU_MN_HLT
    jmp moNoVal
moPair2:                    // CALL or RES
    lda muSopt
    and #$08
    beq moCall2
    lda #MU_MN_RES
    jmp moNib
moCall2:
    lda #MU_MN_CALL
    jmp moNib
moPair6:                    // DEF or VOL
    lda muSopt
    and #$08
    beq moDef2
    lda #MU_MN_VOL
    jmp moNib
moDef2:
    lda #MU_MN_DEF
    jmp moNib
moAtk:                      // ATK or SUS: bit 7 says which
    lda muSopt
    and #$80
    beq moAtk2
    lda #MU_MN_SUS
    jmp moNib
moAtk2:
    lda #MU_MN_ATK
    jmp moNib
moWav:                      // opt & 7 == 7: WAV, F-M, or the 1111 group
    lda muSopt
    and #$0f
    cmp #$0f
    beq moNibF
    lda muSopt
    and #$1f
    cmp #$17
    beq moFm
    lda #MU_MN_WAV
    jmp moTop3
moFm:
    lda #MU_MN_FM
    jmp moTop3
moNibF:                     // bit 4 clear is DST, set is SRC; TAL, END and HLT
    lda muSopt              //   were the exact bytes caught above
    and #$10
    beq moDst
    lda #MU_MN_SRC
    jmp moTop3
moDst:
    lda #MU_MN_DST
    sta muTmpH
    jsr muSoptHi
    lsr
    and #$03                // off, frequency, pulse width, cutoff
    jmp moNibV
moOne:                      // A = opt & 7: 1 = RUP, 5 = RDN
    sta muTmpH
    lda muSopt
    lsr
    lsr
    lsr
    and #$1f                // the rate rides the top five bits
    sta muLnVLo
    lda #0
    sta muLnVHi
    lda muTmpH
    cmp #$05
    beq moRdn
    lda #MU_MN_RUP
    rts
moRdn:
    lda #MU_MN_RDN
    rts
moSw:                       // opt & 7 == 3: the eight, or a 16-23 phrase
    jsr muSoptHi
    tay                     // option / 16 names the command
    cpy #8
    bcs moPhrase
    lda #0
    sta muLnVHi
    lda muSopt              // bit 3 is the n every one of the eight acts on
    and #$08
    lsr
    lsr
    lsr
    sta muLnVLo
    lda muDecSm,y           // a zero entry is a command the grid hides
    rts
moPhrase:
    lda muSopt
    and #$08                // bit 3: CALL or DEF
    beq moPhraseDef
    lda #MU_MN_CALL
    jmp moPhraseV
moPhraseDef:
    lda #MU_MN_DEF
moPhraseV:
    sta muTmpH
    jsr muSoptHi
    and #$07
    clc
    adc #16                 // phrases 16..23
    jmp moNibV
moNib:                      // A = the id; value = the option's high nibble
    sta muTmpH
    jsr muSoptHi
moNibV:
    sta muLnVLo
    lda #0
    sta muLnVHi
    lda muTmpH
    rts
moNoVal:                    // A = the id; no operand to show
    sta muTmpH
    lda #0
    jmp moNibV
moTop3:                     // A = the id; value = the option's top three bits
    sta muTmpH
    jsr muSoptHi
    lsr
    jmp moNibV

// ==========================================
//  Drawing
// ==========================================
muDrawAll:
    lda #MU_C_TEXT
    jsr kSetTextColor
    jsr muDrawGrid             // the walk fills the line table the head reads its
    jsr muDrawHead             //   tempo and its bar from, so the grid goes first
    jsr muDrawLanes
    jsr muDrawMode
    jsr muDrawPiano
    jsr muBarPaint
    jsr muCursorShow
    rts

// --- muDrawHead -- the name, the tempo, the bar and the cursor's row ---
//  BAR is the bar the cursor is in and R its row, both counted in the unit the
//  grid is drawn in, which is what makes a scrolled window's position legible.
//  The tempo is the one in force at the cursor's row, so a tune that changes tempo
//  says so as the cursor crosses the change.
muDrawHead:
    jsr muClearRow0
    jsr muNameAt
    lda #M_TEM2
    jsr muMsg
    jsr muCurTem               // the tempo in force at the cursor's own row: a tune
    sta muHeadTem               //   that changes tempo says so here
    ldx #0
    jsr muNum16
    ldy #MU_CURCOL              // the tail -- BAR, the bar, R, the row -- starts here,
    lda (dataPtr),y             //   and a cursor move or the playhead rewrites it
    sta muHeadBarCol            //   from here with muHeadTail
    jsr muHeadBarRow
    ldy #MU_CURCOL              // where the row number ends: a shorter one blanks
    lda (dataPtr),y             //   the cells back to here
    sta muHeadEnd
    muSet(MU_HOV, $ff)          // no pitch yet: three blanks, until a hover says otherwise
    jmp muHovDraw

// --- muHeadBarRow -- the head tail: BAR and the bar, then R and the row ---
//  In: MU_CURCOL is positioned where the tail starts. Both the whole-row draw and
//  the in-place tail rewrite print these two fields, and neither the bar nor the row
//  comes from a register: muBarOf reads the cursor line table and muCursorAbs the
//  walk, so the pair is read again here rather than handed in.
muHeadBarRow:
    lda #M_BAR2
    jsr muMsg
    jsr muBarOf
    sta muHeadBar
    stx muHeadBar+1
    jsr muNum16
    lda #M_ROW2
    jsr muMsg
    ldy #MU_CURCOL
    lda (dataPtr),y
    sta muHeadRowCol
    jsr muCursorAbs
    jmp muNum16

// --- muHeadTail -- rewrite the head's BAR and R from where the tail starts ---
//  A cursor move changes the position fields only: the name and the tempo are
//  already the right characters in the right cells, so clearing the row and
//  printing them again is work with nothing to show for it. The tail is
//  rewritten in place, and whatever a shorter number gave up is blanked.
//  The tempo is the one exception: it belongs to the row, so a cursor that has
//  crossed a change brings the whole row back.
muHeadTail:
    lda muStatT                 // a status owns row 0 until it retires: it is not
    bne muDrawHead              //   ours to write into, so the whole row comes back
    jsr muCurTem
    cmp muHeadTem
    beq mhtTail
    jmp muDrawHead
mhtTail:
    ldy #MU_CURROW
    lda #0
    sta (dataPtr),y
    ldy #MU_CURCOL
    lda muHeadBarCol
    sta (dataPtr),y
    jsr muHeadBarRow
    jmp muHeadPad

// --- muHeadPad -- blank the head from the text cursor to the old field end ---
//  A number that lost a digit would leave the old one behind (the cursor moving
//  up across 1000 -> 999, or a bar boundary going the other way), so the cells up
//  to where the field ended last time are blanked, and the end that results is
//  recorded -- a field that gained a digit moves it.
muHeadPad:
    ldy #MU_CURCOL
    lda (dataPtr),y
    cmp muHeadEnd
    bcs muHpStore
    lda #MU_SPACE
    jsr muPutC
    jmp muHeadPad
muHpStore:
    sta muHeadEnd
    rts

// --- muDrawHeadRow -- just the row number of the head ---
//  The playhead moves the row every line, and rewriting the whole head for it
//  is what made row 0 flicker. The column was recorded when the head was drawn.
muDrawHeadRow:
    ldy #MU_CURROW
    lda #0
    sta (dataPtr),y
    ldy #MU_CURCOL
    lda muHeadRowCol
    sta (dataPtr),y
    jsr muCursorAbs
    jsr muNum16
    jmp muHeadPad

// --- muPhHead -- the head's BAR and R fields follow the playhead ---
//  The row moves every line and the bar once every N lines, so the bar field is
//  rewritten only when its number really changes. Both fields are rewritten
//  together when it does: the row field's column follows the bar's width.
muPhHead:
    jsr muCurTem
    cmp muHeadTem
    bne muPhFull                // the tempo changed: the whole row comes back
    jsr muBarOf
    cmp muHeadBar
    bne muPhBar
    cpx muHeadBar+1
    beq muDrawHeadRow           // the bar has not moved: only the row number
muPhBar:
    jmp muHeadTail
muPhFull:
    jmp muDrawHead

// --- muCursorIdx -- the cursor screen row as an index into the line table ---
//  Out: A = MU_ROW - MU_GRID_TOP, the index muLnTb*, muLnKind and the rest are read
//  with. The row itself stays in the instance; this is only where it points.
muCursorIdx:
    ldy #MU_ROW
    lda (dataPtr),y
    sec
    sbc #MU_GRID_TOP
    rts

// --- muCursorAbs -- A/X = the cursor's absolute timeline row ---
//  The screen row names a LINE, and a line may be a command's: MU_LNTB says which
//  row the cursor's line holds.
muCursorAbs:
    jsr muCursorIdx
    tay
    lda muLnTbLo,y
    sta muTmpL
    lda muLnTbHi,y
    sta muTmpH
    lda muTmpL
    ldx muTmpH
    rts

// --- muCurLine -- Y = the cursor's line; C=1 when that line holds no row ---
//  A line below the tune's last one still has a screen row and its table entry is
//  $ffff, so everything that reads a line's record asks this first.
muCurLine:
    jsr muCursorIdx
    tay
    lda muLnTbHi,y
    cmp #$ff
    beq mclNone
    clc
    rts
mclNone:
    sec
    rts

// --- muBarOf -- A/X = the cursor's bar, 1-based ---
//  The bar is the line's, read from the walk: a tempo change cuts a bar short, so
//  the count cannot be divided out of the row index any more.
muBarOf:
    jsr muCurLine
    bcs mboNone
    lda muLnBarLo,y
    ldx muLnBarHi,y
    rts
mboNone:
    lda #1
    ldx #0
    rts

// --- muCurTem -- A = the tempo in force at the cursor's row ---
muCurTem:
    jsr muCurLine
    bcs mctOpen
    lda muLnTem,y
    rts
mctOpen:
    lda muTempo                 // below the tune's last line: the opening tempo
    rts

// --- muCurBar -- A = the rows a bar has at the cursor's row ---
muCurBar:
    jsr muCurTem
    jsr muFit
    lda muFitBar
    rts

muDrawLanes:
    muAt(0, 1)
    lda #1                      // the lane headers read as one bar
    sta muRevText
    lda #M_LANES
    jsr muMsg
    lda #0
    sta muRevText
    rts

// --- muInsToggle -- insert/overwrite mode, from the M key or the mode button ---
muInsToggle:
    lda muInsMode
    eor #1
    sta muInsMode
    jmp muDrawMode

// --- muDrawMode -- OVR or INS at the right of row 1 (outside the scroll area) ---
//  The three cells ARE the mode button: the pointer reverses them and a click there
//  toggles, so the readout and what the next note entry does cannot disagree.
muDrawMode:
    lda muInsMode
    beq muDmOvr
    lda #'I'
    ldx #MU_MODE_COL
    jsr muModePut
    lda #'N'
    ldx #MU_MODE_COL+1
    jsr muModePut
    lda #'S'
    ldx #MU_MODE_COL+2
    jmp muModePut
muDmOvr:
    lda #'O'
    ldx #MU_MODE_COL
    jsr muModePut
    lda #'V'
    ldx #MU_MODE_COL+1
    jsr muModePut
    lda #'R'
    ldx #MU_MODE_COL+2
    jmp muModePut

// --- muModeHover -- A = 1 while the pointer is on the mode button; repaint on change ---
muModeHover:
    cmp muModeHov
    beq muMhRts
    sta muModeHov
    jmp muDrawMode
muMhRts:
    rts

// --- muModePut -- A = the character, X = its column: one mode cell, text then background ---
//  The text writer puts the character down -- the shadow has to carry it, or a task
//  switch would bring the three cells back blank -- and it keeps the cell's background
//  nibble, which a reversed cell has as the text colour. So the background is given
//  its nibble afterwards by a blit: reversed under the pointer, plain when it leaves.
//  The blits are framebuffer writes and belong to the displayed task.
muModePut:
    stx muTmpL                  // every writer takes A = char, X = col, Y = row
    pha
    ldy #MU_MODE_ROW
    jsr kPrintCharAt
    lda muTid
    cmp kbFocus
    bne muMpDrop                // the display is elsewhere: the text is all we write
    lda muModeHov
    beq muMpPlain
    ldx muTmpL
    ldy #MU_MODE_ROW
    pla
    jsr kBlitCharRev
    rts
muMpPlain:
    ldx muTmpL
    ldy #MU_MODE_ROW
    pla
    jsr kBlitChar
    rts
muMpDrop:
    pla
    rts

// ==========================================
//  The structure's links
// ==========================================
//  The phrase commands are the format's own control flow: a CALL plays a phrase
//  whose DEF sits somewhere else in the same voice, and a tune like `immig` has
//  sixty-five of them. Reading one means finding the other, and the row to find is
//  the row the band would draw it on -- so the search walks the tune with the same
//  producer the grid uses rather than decoding the image a second way.

// --- muWalkStart -- the producer back at the tune's start ---
//  The rows a window shows are reached by generating and discarding the ones above
//  them, so every walk from the beginning starts here. MU_LNSCR is $ff: each line
//  is discarded, so a search draws nothing and leaves the band's table alone.
muWalkStart:
    jsr muSimInit
    ldx #2
mwsZero:
    lda #0
    sta muVRow,x
    sta muVRowHi,x
    sta muVRem,x
    dex
    bpl mwsZero
    lda #$ff                    // the producer starts one row before the first, so
    sta muGrpRow                //   the group it opens first is row 0
    sta muGrpRow+1
    sta muLnScr
    lda #0
    sta muGrpSt
    sta muLogN
    sta muLogIx
    rts

// --- muFindCmd -- the first row that draws a given command ---
//  In: MU_FINDID (the mnemonic id), MU_FINDVAL (its operand), MU_FINDANY (1 = any
//      row), MU_FINDAFTER (the 16-bit row to pass when MU_FINDANY is 0).
//  Out: C=0 with MU_FNDROW = that row and MU_FNDLINE = the command's line index
//      inside the row's group (0 is the row's top line); C=1 when the tune has no
//      such command.
//  A row's commands are logged per voice and class, so the search covers all six
//  blocks: the caller picks its lane from the depth afterwards.
muFindCmd:
    jsr muWalkStart
mfcRow:
    jsr muGrpOpen
    bcc mfcScan
    jmp mfcNone                 // the tune ended: no such command
mfcScan:
    lda muFindAny               // a row at or above MU_FINDAFTER cannot match when
    bne mfcBlocks               //   the caller wants the next one; skip its scan
    lda muGrpRow+1
    cmp muFindAfter+1
    bcc mfcNextRow
    bne mfcBlocks
    lda muGrpRow
    cmp muFindAfter
    bcc mfcNextRow
    beq mfcNextRow
mfcBlocks:
    ldx #0                      // the structural blocks only: CALL and DEF are ids
mfcBlock:                       //   below MU_MN_FC, so they are never logged as sound
    lda muLogN,x
    beq mfcBlkNext
    lda muLogBase,x
    sta muTmp2                  // the slot, from the block's first
    lda #0
    sta muTmpH                  // ...and its depth in the block
mfcSlot:
    ldy muTmp2
    lda (muLogIdP),y
    cmp muFindId
    bne mfcStep
    lda muLogHi,y
    bne mfcStep                 // a 16-bit operand is never a phrase number
    lda (muLogLoP),y
    cmp muFindVal
    beq mfcMaybe
mfcStep:
    inc muTmp2
    inc muTmpH
    lda muTmpH
    cmp muLogN,x
    bcc mfcSlot
mfcBlkNext:
    inx
    inx                         // block = voice * 2 + class, and the class is 0
    cpx #6
    bcc mfcBlock
mfcNextRow:
    lda muOn                    // the producer reports no row for a tune that has
    ora muOn+1                  //   ended, so its own halt flags are the end test
    ora muOn+2
    beq mfcNone
    jmp mfcRow
mfcMaybe:
    lda muFindAny
    bne mfcTake
    lda muGrpRow+1              // only a row past MU_FINDAFTER will do
    cmp muFindAfter+1
    bcc mfcStep
    bne mfcTake
    lda muGrpRow
    cmp muFindAfter
    bcc mfcStep
    beq mfcStep                 // the cursor's own row is not after itself
mfcTake:
    txa
    lsr                         // the block's voice is its top bit
    sta muFndVoice
    lda muLogSnd                // a structural line sits below the row's sound lines
    clc
    adc muTmpH
    sta muFndLine
    lda muGrpRow
    sta muFndRow
    lda muGrpRow+1
    sta muFndRow+1
    clc
    rts
mfcNone:
    sec
    rts

// --- muBarFind -- the first row that falls in bar MU_OBJVAL ---
//  In: MU_OBJVAL/MU_OBJVALHI = the bar wanted, 1-based, and no walk state to preserve:
//  the walk never touches those two bytes, which is what lets them carry the target.
//  Out: C=0 with MU_FNDROW, MU_FNDLINE and MU_FNDVOICE set for mlkJump -- the line is
//  the row's OWN line, the last of its group -- or C=1 when the tune ends first. The
//  producer is left parked where the search stopped, so a refusing caller must rebuild
//  the band (muBandRebuild) before drawing anything.
muBarFind:
    jsr muWalkStart
mbfRow:
    jsr muGrpOpen
    bcs mbfNone                 // every voice halted: the tune ends inside that bar
    lda muBarNo+1               // the bar the row just opened falls in
    cmp muObjValHi
    bcc mbfNext
    bne mbfTake
    lda muBarNo
    cmp muObjVal
    bcs mbfTake
mbfNext:
    lda muOn                    // the halt flags are the walk's own end test
    ora muOn+1
    ora muOn+2
    bne mbfRow
mbfNone:
    sec
    rts
mbfTake:
    lda muGrpRow
    sta muFndRow
    lda muGrpRow+1
    sta muFndRow+1
    lda muLogLns                // the group's command lines come first, so the row's
    sta muFndLine               //   own line is the one after all of them
    ldy #MU_VOICE               // the jump keeps the cursor's voice
    lda (dataPtr),y
    sta muFndVoice
    clc
    rts

// --- muBarGo -- jump to bar MU_OBJVAL, or say there is no such bar ---
muBarGo:
    jsr muBarFind
    bcc mbgGo
    jsr muBandRebuild           // the search walked the producer: put it back
    lda #M_NO_BAR
    jsr muMsgGo                 // this path returns: the dispatch pushed no frame
    jmp muKeyIdle
mbgGo:
    jmp mlkJump

// --- muBarNext -- the bar after the cursor's ---
muBarNext:
    jsr muBarOf                 // A/X = the cursor's bar
    clc
    adc #1
    sta muObjVal
    txa
    adc #0
    sta muObjValHi
    jmp muBarGo

// --- muBarPrev -- the bar before the cursor's, when there is one ---
muBarPrev:
    jsr muBarOf
    sec
    sbc #1
    sta muObjVal
    txa
    sbc #0
    sta muObjValHi
    ora muObjVal
    bne muBarGo
    rts                         // bar 1 has nothing before it

// --- muBarPrompt -- row 0: type the bar to go to ---
//  The object panel's operand entry with a different last step: RETURN jumps to the
//  bar and STOP leaves the view alone. The value starts at the cursor's own bar, so
//  the prompt opens showing where you are, and the first digit typed replaces it.
muBarPrompt:
    jsr muPlaying               // a jump would fight the playhead's follow
    bcs mbtOut                  // "? stop first" owns row 0 until it retires
    jsr muBarOf
    sta muObjVal
    stx muObjValHi
    lda #0
    sta muObjFresh              // the first digit starts the number afresh
mbtDraw:
    jsr muClearRow0
    lda #M_BAR2
    jsr muMsg
    lda muObjVal
    ldx muObjValHi
    jsr muNum16
mbtKey:
    jsr kReadKey
    beq mbtWait
    cmp #$81                    // RETURN: go there
    beq mbtTake
    cmp #$83                    // STOP: leave the view alone
    beq mbtRts
    cmp #$80                    // INST/DEL: back one digit
    beq mbtBack
    sec
    sbc #'0'
    bcc mbtKey                  // not a digit
    cmp #10
    bcs mbtKey
    jsr muValAcc                // ...so the number builds as the panel's does
    jmp mbtDraw
mbtWait:
    lda #SYS_YIELD
    jsr syscall
    jmp mbtKey
mbtBack:
    jsr muValDiv10
    jmp mbtDraw
mbtTake:
    lda muObjVal
    ora muObjValHi
    beq mbtRts                  // bar 0 does not exist
    jmp muBarGo
mbtRts:
    jsr muDrawHead              // the prompt only touched row 0: the head takes it back
    jmp muKeyIdle
mbtOut:
    jmp muKeyIdle               // "? stop first" already owns row 0

// ==========================================
//  Phrase bodies
// ==========================================
//  A DEF body is walked INLINE, so it is not a region of the grid: every row of it
//  is drawn once, where the definition stands. What the rows of a body do share is
//  that an edit to them changes every call site, and that is what the walk can say
//  exactly -- muPhDf,x is the phrase each voice is executing, muPhStk the enclosing
//  one per call frame, and the two are maintained wherever a frame is pushed or
//  popped. Phrase n is therefore stored as n + 1, because 0 means the top level
//  stream and phrase 0 is a phrase like any other.

// --- muPhWatch -- name the body the cursor's row is in, when that changes ---
//  A cursor move does NOT walk the tune: the grid is already on screen and only the
//  glyph moves, so the answer has to come from what the last draw recorded. muLnPh
//  holds each visible line's phrase per voice, which is why this costs three loads
//  and no walk at all.
muPhWatch:
    lda muPlayOn
    bne muPwRts                 // the player owns row 0 while it runs
    jsr muCursorIdx
    cmp #MU_GRID_ROWS
    bcs muPwRts                 // below the band: no line was recorded there
    tax                         // X = the line's index
    ldy #MU_VOICE
    lda (dataPtr),y
    tay
    txa
    clc
    adc muLnPhBase,y            // this voice's block of the table
    tax
    lda muLnPh,x
    beq muPwGone
    cmp muPhNo
    beq muPwRts                 // the same answer as last time: nothing to say
    sta muPhNo
    sta muPhPend                // a body: muPhTick puts the name up
    rts
muPwGone:
    sta muPhNo                  // back at the top level: the next body is news again
muPwRts:
    rts

// --- muPhTick -- say which phrase body the cursor's row is in, once per arrival ---
//  Runs from the idle pass: muGrpOpen only ARMS this, because the walk is in the
//  middle of drawing and both the message and row 0 want the REU windows.
muPhTick:
    lda muPhPend
    beq muPtDone
    sta muPhBad
    lda #0
    sta muPhPend
    lda #M_IN_PHRASE
    jsr muMsgSet
    jsr muStatus
    jmp muPhNum
muPtDone:
    rts

// --- muPhRefuse -- an edit that would change every call site, refused ---
//  In: MU_PHBAD = the phrase, + 1. Returns with the status line showing it.
muPhRefuse:
    lda #M_BAD_PHRASE
    jsr muMsgSet
    jsr muStatus
// --- muPhNum -- " n x c" at the text cursor, for MU_PHBAD ---
//  muStatus leaves the cursor right after its message, so the numbers simply follow
//  it; muNum16 already prints at the text cursor and squashes a leading zero.
muPhNum:
    lda #' '
    jsr muPutC
    lda muPhBad
    sec
    sbc #1
    ldx #0
    jsr muNum16
    lda #' '
    jsr muPutC
    lda #'x'
    jsr muPutC
    jmp muPhCount               // A/X = the sites, printed as the last number

// --- muPhCount -- A/X = how many CALL sites name the phrase MU_PHBAD is ---
//  The count is structural, not a tally of the walk: a TAL loop would execute the
//  same CALL on every pass and inflate it. Every event is a 2-byte pair and the
//  three streams are contiguous, so one pass over [8, 8 + the three lengths) sees
//  each CALL byte exactly once -- whichever voice's stream it belongs to, because a
//  phrase body is global and any voice's CALL can jump into it.
//  The pair is decoded by the two patterns muSimCmd routes on: a CALL of 0-15 is a
//  $01-family option with bit 3 clear, and a CALL of 16-23 is the mnemonic group's
//  form that carries bit 7.
muPhCount:
    lda #0
    sta muPhCnt
    sta muPhCnt+1
    lda #8
    sta muPhCur
    sta muPhEnd
    lda #0
    sta muPhCur+1
    sta muPhEnd+1
    jsr muImgSeat               // kPtr = the image, for the three length words
    ldy #2
muPcSum:
    lda (kPtr),y
    clc
    adc muPhEnd
    sta muPhEnd
    iny
    lda (kPtr),y
    adc muPhEnd+1
    sta muPhEnd+1
    iny
    cpy #8
    bcc muPcSum
muPcPair:
    lda muPhCur
    cmp muPhEnd
    lda muPhCur+1
    sbc muPhEnd+1
    bcs muPcDone
    lda muPhCur
    sta muWinOfs
    lda muPhCur+1
    sta muWinOfs+1
    lda #0
    tax
    jsr muWinSeat               // kPtr = the page this pair lives on
    ldy #0
    lda (kPtr),y
    and #$03
    cmp #1
    bne muPcNext                // not the $01 family: no phrase command
    ldy #1
    lda (kPtr),y
    and #$07
    cmp #$03
    beq muPcWide                // the mnemonic group: 16-23 when bit 7 is set
    cmp #$02
    bne muPcNext
    lda (kPtr),y                // the CALL/RES pair: bit 3 makes it RES
    and #$08
    bne muPcNext
    lda (kPtr),y
    lsr
    lsr
    lsr
    lsr
    jmp muPcCmp
muPcWide:
    lda (kPtr),y
    bpl muPcNext                // bit 7 clear: a sound command, not a phrase one
    and #$08
    beq muPcNext                // bit 3 clear: that is a DEF
    lda (kPtr),y
    lsr
    lsr
    lsr
    lsr
    and #$07
    ora #16
muPcCmp:
    clc
    adc #1                      // ...as the stored form, which is phrase + 1
    cmp muPhBad
    bne muPcNext
    inc muPhCnt
    bne muPcNext
    inc muPhCnt+1
muPcNext:
    lda muPhCur                 // the next PAIR is two bytes on
    clc
    adc #2
    sta muPhCur
    bcc muPcPair
    inc muPhCur+1
    jmp muPcPair
muPcDone:
    lda muPhCnt
    ldx muPhCnt+1
    jmp muNum16

// --- muPhBlock -- C=1 when an insert at the cursor's row would cut into a body ---
//  In: the cursor's row. The walk advances whichever voice is earliest, so one seek
//  leaves every voice at-or-after that row: MU_PHDF then says, for each voice, which
//  phrase body the boundary in front of it belongs to. A voice that has halted takes
//  no insert at all, so its body cannot change and it does not count.
muPhBlock:
    jsr muSeek
    ldx #0
muPbLp:
    lda muPhDf,x
    beq muPbNext
    lda muOn,x
    beq muPbNext
    lda muPhDf,x
    sta muPhBad
    sec
    rts
muPbNext:
    inx
    cpx #3
    bcc muPbLp
    clc
    rts

// --- muPhArm -- say which body the row just edited belongs to, when it is in one ---
//  Called by a note entry, which is the edit that is ALLOWED inside a body: it
//  changes every call site, and that is what the message says.
muPhArm:
    jsr muPhHere
    bcc muPaRts
    lda muPhBad
    sta muPhPend
muPaRts:
    rts

// --- muPhHere -- C=1 when the seek's own boundary is inside a body ---
//  For the paths that remove time: MU_SKV is the voice the seek ran for.
muPhHere:
    ldx muSkV
    lda muPhDf,x
    sta muPhBad
    beq muPhHereNo
    sec
    rts
muPhHereNo:
    lda #0
    sta muPhBad
    clc
    rts

// --- muCmdHere -- the command the cursor's line holds ---
//  Out: C=0 with A = its mnemonic id, MU_CMDVAL = the operand and MU_CMDROW = the
//      row the line belongs to; C=1 when the cursor is not on a command line, or
//      its lane has no command on that line -- a line carries one per lane.
//  The line's index inside its row's group is taken from the group's LENGTH and the
//  lines BELOW it, which are the ones that are certainly on screen: the row's own
//  line is always below a command line. Counting the lines above instead reads
//  whatever the window happens to start on -- scroll three lines into a row of ten
//  commands and three of them are gone, so the index comes out three short.
muCmdHere:
    jsr muCursorIdx
    tay
    cpy #MU_GRID_ROWS
    bcc mchIn
    sec                         // the cursor is below the band
    rts
mchIn:
    lda muLnKind,y              // 1 = the row's own line, which holds no command
    beq mchCmd
    sec
    rts
mchCmd:
    lda muLnTbLo,y
    sta muCmdRow
    lda muLnTbHi,y
    sta muCmdRow+1
    lda #1
    sta muLinkW                 // the producer is about to move: a refusal must
    jsr muWalkStart             //   put it back (muBandRebuild)
mchRow:
    jsr muGrpOpen
    lda muGrpRow+1
    cmp muCmdRow+1
    bne mchNext
    lda muGrpRow
    cmp muCmdRow
    bne mchNext
    jmp mchFound
mchNext:
    lda muOn                    // the producer has no row past the tune's end, so
    ora muOn+1                  //   its halt flags are the end test
    ora muOn+2
    bne mchRow
    sec
    rts
mchFound:
    jsr muCursorIdx             // where the line is in the band again: the walk
                                //   clobbers every temp it was in
    tay
    ldx #0                      // the group's command lines below it
mchDown:
    iny
    cpy #MU_GRID_ROWS
    bcs mchDepth                // the band ends first: take what is on screen
    lda muLnKind,y
    bne mchDepth                // the row's own line ends the group
    inx
    jmp mchDown
mchDepth:
    stx muTmp2
    lda muLogLns                // k = the group's command lines - 1 - those below
    sec
    sbc muTmp2
    bcc mchNone
    beq mchNone
    sec
    sbc #1
    sta muCmdK
    ldy #MU_VOICE               // the lane the cursor stands in
    lda (dataPtr),y
    asl                         // block = voice * 2 + class
    sta muTmpL
    lda muCmdK
    cmp muLogSnd
    bcc mchSound
    sec                         // a structural line, counted from the row upwards
    sbc muLogSnd
    sta muTmp2
    lda muTmpL
    jmp mchBlock
mchSound:
    sta muTmp2                  // a sound line: the depth is the line index
    lda muTmpL
    ora #1
mchBlock:
    tax
    lda muTmp2
    cmp muLogN,x
    bcs mchNone                 // the cursor's lane has no command on the line
    clc
    adc muLogBase,x
    tay
    lda (muLogLoP),y
    sta muCmdVal
    lda muLogHi,y               // the operands that need two bytes (P-W, POR, MS#)
    sta muCmdValHi
    lda (muLogIdP),y
    clc
    rts
mchNone:
    sec
    rts

// --- muLinkKey -- SHIFT+D: from a call to its definition, and back to a call ---
//  On a CALL's line the cursor goes to that phrase's DEF; on a DEF's line it goes
//  to the next call of the same phrase below the cursor, and round to the first
//  one when there is none below, so repeated presses tour the call sites. The
//  cursor lands on the line that carries the command, not on the row's own line,
//  and the window puts that row at its top.
muLinkKey:
    lda #0                      // nothing has walked the producer yet
    sta muLinkW
    jsr muCmdHere
    bcc mlkHave
    lda #M_NOT_PHRASE
    jmp mlkSay
mlkHave:
    cmp #MU_MN_CALL
    bne mlkNotCall
    lda #MU_MN_DEF              // a definition is anywhere: the first one
    sta muFindId
    lda muCmdVal
    sta muFindVal
    lda #1
    sta muFindAny
    jsr muFindCmd
    bcc mlkJump
    lda #M_NO_DEF
    jmp mlkSay
mlkNotCall:
    cmp #MU_MN_DEF
    beq mlkDef
    lda #M_NOT_PHRASE
    jmp mlkSay
mlkDef:
    lda #MU_MN_CALL             // the next call below the cursor, if there is one
    sta muFindId
    lda muCmdVal
    sta muFindVal
    lda #0
    sta muFindAny
    lda muCmdRow
    sta muFindAfter
    lda muCmdRow+1
    sta muFindAfter+1
    jsr muFindCmd
    bcc mlkJump
    lda #1                      // none below: round to the tune's first call
    sta muFindAny
    jsr muFindCmd
    bcc mlkJump
    lda #M_NO_CALL
mlkSay:
    pha                         // the message index, across the walk back below
    lda muLinkW                 // only a search moved the producer: a refusal on a
    beq mlkMsg                  //   note's line walked nothing
    jsr muBandRebuild
mlkMsg:
    pla
    jsr muMsgGo                 // this path returns: the dispatch pushed no frame
    jmp muKeyIdle
mlkJump:
    lda muFndRow
    sta muTop
    lda muFndRow+1
    sta muTop+1
    lda #0                      // the search names a row, so the band starts at its
    sta muToff                  //   first line and the cursor brings the line in
    ldy #MU_VOICE               // the cursor follows the command into its lane
    lda muFndVoice
    sta (dataPtr),y
    lda muFndLine               // the line carrying the command, clamped to the band
    clc
    adc #MU_GRID_TOP
    cmp #MU_GRID_BOT+1
    bcc mlkSet
    lda #MU_GRID_BOT
mlkSet:
    ldy #MU_ROW
    sta (dataPtr),y
    jmp muCurRedraw

// --- muWalkToTop -- the producer at the line the window's top row starts at ---
//  In: MU_TOP (the window's row) and MU_TOFF (how many lines into that row's
//      group the band's own top line is).
//  Out: C=1 when the tune ended above the window (there is nothing to draw).
//  Nothing is drawn on the way: MU_LNSCR is $ff, so every line this passes is
//  discarded and the band's table is left alone.
muWalkToTop:
mwtSkip:
    lda muGrpSt
    bne mwtGo                   // a group is open: its next line is this row's
    lda muGrpRow                // the group the next line would open
    clc
    adc #1
    sta muTmpL
    lda muGrpRow+1
    adc #0
    sta muTmpH
    lda muTmpH
    cmp muTop+1
    bne mwtGo
    lda muTmpL
    cmp muTop
    bne mwtGo
    lda muToff                  // the band starts MU_TOFF lines into this row's
    beq mwtYes                  //   group: emit and discard exactly those
mwtOff:
    jsr muLnNext
    bcs mwtRts
    lda muGrpSt
    beq mwtShort                // the row lost lines since: MU_TOFF is now past it
    lda muLogIx
    cmp muToff
    bcc mwtOff
mwtYes:
    clc
mwtRts:
    rts
mwtShort:
    inc muGrpRow                // the row's group is shorter than the window is into
    bne mwtShort1               //   it, so the band's top line is the next row's
    inc muGrpRow+1              //   first one, and the window follows it there
mwtShort1:
    lda muGrpRow
    sta muTop
    lda muGrpRow+1
    sta muTop+1
    lda #0
    sta muToff
    clc
    rts
mwtGo:
    jsr muLnNext
    bcs mwtRts
    jmp mwtSkip

// --- muDrawGrid -- fill the band at MU_TOP/MU_TOFF from the producer's lines ---
//  A row is the finest length the tempo in force can express, and a bar is one
//  whole note counted in those rows, so the grid follows a mid-tune TEM: the walk
//  opens a segment wherever the tempo changes and each line records the tempo and
//  the bar it falls in. The band is filled from the TOP line down, each line
//  generated in order -- the walk runs once, and the lines it does not reach (a
//  tune shorter than the band) stay blank.
//  The window is a ROW plus how many lines into that row's group the band starts,
//  so a redraw lands on the line the band is showing rather than on the row's
//  first one: the caller owns both halves and a draw never changes them.
//  With MU_GDCLIP set the caller is repainting a band of screen rows: the walk and
//  the lines are the same, and only the lines whose row falls in the caller's band
//  are drawn, which is what an edit repaints.
muDrawGrid:
    lda muGdClip
    bne muDgWalk
    jsr muGridClear
    ldx #MU_GRID_ROWS-1         // a line the walk never reaches is not a row
    lda #$ff
muDgTbLp:
    sta muLnTbLo,x
    sta muLnTbHi,x
    lda #0
    sta muLnKind,x
    sta muLnPh,x
    sta muLnPh+MU_GRID_ROWS,x
    sta muLnPh+2*MU_GRID_ROWS,x
    sta muLnBarLo,x
    sta muLnBarHi,x
    sta muLnTem,x
    sta muLnTick,x
    sta muLnTickHi,x
    lda #$ff
    dex
    bpl muDgTbLp
muDgWalk:
    jsr muWalkStart
    jsr muWalkToTop
    bcs muDgEnd                 // the tune ended above the window: nothing to draw
muDgTop:
    lda #MU_GRID_TOP
    sta muLnScr
muDgDraw:
    jsr muLnNext
    bcs muDgEnd
    inc muLnScr
    lda muLnScr
    cmp #MU_GRID_BOT+1
    bcc muDgDraw
muDgEnd:
    lda #0                      // the clip lasts exactly one draw: the lines the
    sta muGdClip                //   next scroll pulls in are outside it and must be
    rts                         //   drawn

// --- muBandRebuild -- put the producer back where the band was drawn from ---
//  A search walks the tune with the producer itself, so it leaves MU_GRPST and the
//  open group at whatever row it stopped on and the next scroll would emit the
//  wrong lines. This walks back to the window's top and re-emits the band's own
//  lines, which is the state the draw left -- and draws nothing, because the screen
//  is already right. It also refills the walk's history ring with the band's own
//  events, which the search had replaced.
//  The countdown lives on the STACK because muLnNext clobbers X -- and a counter it
//  eats never reaches zero: the loop re-emitted lines until the machine was switched
//  off. X is no register to count in across a call.
muBandRebuild:
    jsr muWalkStart
    jsr muWalkToTop
    bcs mbrRts
    lda #MU_GRID_ROWS
    pha
mbrLp:
    jsr muLnNext
    bcs mbrDrop
    tsx
    dec $0101,x                 // the byte just pushed, one under the return address
    bne mbrLp
mbrDrop:
    pla
mbrRts:
    rts

// --- muGdFlag -- '?' on the row whose length the tempo in force refused ---
// --- muRedrawRows -- draw the grid for a band of screen rows only ---
//  In: A = the first screen row, X = one past the last. An edit repaints the rows
//  it touched rather than the whole window: those rows are cleared, and the lines
//  whose row they hold are the only ones the producer draws (MU_GDLo/MU_GDHi).
//  The walk runs either way -- a window deep in a tune has to be walked from the
//  start -- so the saving is the clear and the cells, not the walk. The table is
//  rebuilt for every line the walk reaches, drawn or not, so a caller may read it
//  afterwards.
muRedrawRows:
    sta muGdFrom
    stx muGdTo
    lda #1
    sta muGdClip
    lda muGdFrom                // the first row the band holds
    sec
    sbc #MU_GRID_TOP
    tay
    lda muLnTbLo,y
    sta muGdLo
    lda muLnTbHi,y
    sta muGdLo+1
    lda muGdTo                  // ...and one past the last
    sec
    sbc #MU_GRID_TOP
    sec
    sbc #1
    tay
    lda muLnTbHi,y
    cmp #$ff                    // a line past the tune's end holds $ffff: the band
    beq muRrNoEnd               //   then runs on to the end of the tune, not to row 0
    lda muLnTbLo,y
    sta muGdHi
    lda muLnTbHi,y
    sta muGdHi+1
    lda muGdHi                  // one past, 16-bit
    clc
    adc #1
    sta muGdHi
    bcc muRrClr
    inc muGdHi+1
    jmp muRrClr
muRrNoEnd:
    lda #$ff                    // no row is past this, so the band has no end
    sta muGdHi
    sta muGdHi+1
muRrClr:
    lda muGdFrom
    sta muClrRow
muRrClrLp:
    jsr muClearRow
    inc muClrRow
    lda muClrRow
    cmp muGdTo
    bcc muRrClrLp
    jsr muDrawGrid
    jsr muDrawHead              // the length moved, so the bar and the tempo under
    jmp muCursorShow            //   the cursor may have: the head reads both from
                                //   the walk. Its rts balances the jsr that entered

// --- muCurSettle -- leave the cursor on row A/X's own line after a repaint ---
//  The cursor is a SCREEN row, and an edit changes how many lines the rows above it
//  draw -- a voice's HLT line moves down to the row the note now precedes, so the
//  band re-flows under a cursor that has not moved and the next key would act on a
//  different row. This finds the line the edited row draws and puts the cursor on it.
//  A row that is not on screen leaves the cursor where it was.
muCurSettle:
    sta muTmpL
    stx muTmpH
    ldy #0
mcsLp:
    lda muLnTbHi,y
    cmp muTmpH
    bne mcsNext
    lda muLnTbLo,y
    cmp muTmpL
    bne mcsNext
    lda muLnKind,y              // the row's own line, not one of its command lines
    bne mcsFound
mcsNext:
    iny
    cpy #MU_GRID_ROWS
    bcc mcsLp
mcsRts:
    rts
mcsFound:
    tya
    clc
    adc #MU_GRID_TOP
    sta muTmpL
    ldy #MU_ROW
    lda (dataPtr),y
    cmp muTmpL
    beq mcsRts
    jsr muCursorHide
    lda muTmpL
    ldy #MU_ROW
    sta (dataPtr),y
    jsr muHeadTail              // the head's BAR and R follow the cursor
    jmp muCursorShow

// --- muCurSettleNext -- settle on the row a write changed, then stand on the next row ---
//  In: A/X = the row the write landed on, as muCurSettle takes it.
//  Overwrite mode only: insert mode's own entry leaves the cursor on the row its note
//  starts on (muNoteInsMode -> muInsGoStep), so a second step would skip a row.
//  The move is muCurSettle's own scan: the first line of that row which is the row's
//  OWN line -- a row's command lines come above it, and a command takes no time, so the
//  next note belongs on the line below them. The scan is bounded by the band's size and
//  runs nothing else, so a next row that is not drawn yet (the tune's last row, or the
//  row below the band's bottom line) leaves the cursor where the write put it. Nothing
//  here touches the walk's producer: the band moves when the cursor is taken past its
//  bottom line, exactly as CRSR-DOWN does.
muCurSettleNext:
    jsr muCurSettle
    lda muInsMode
    bne mcsnRts
    lda muTgtLo                 // the next row's own line, if the band holds it
    clc
    adc #1
    sta muCnT0
    lda muTgtHi
    adc #0
    sta muCnT0+1
    lda muCnT0
    ldx muCnT0+1
    jmp muCurSettle
mcsnRts:
    rts

// --- muBandRow -- A = the screen row of the first line of the group holding row A/X ---
//  In: A/X = the row the edit touched. Out: A = the first screen row of its group.
//  The repaint starts at the first row the EDIT touched, not at the cursor's row: a
//  gap fill writes its rests from the row after the event it found, which can be
//  several rows ABOVE the cursor, and the line that changes can be any of that row's
//  lines -- an event's command lines come before its own. The walk's table is what
//  says which screen lines hold the row, so the first one that does is the group's
//  first line. A row that is not on screen repaints from the top of the band.
muBandRow:
    sta muTmpL
    stx muTmpH
    ldy #0
mbwLp:
    lda muLnTbHi,y
    cmp muTmpH
    bne mbwNext
    lda muLnTbLo,y
    cmp muTmpL
    beq mbwDone
mbwNext:
    iny
    cpy #MU_GRID_ROWS
    bcc mbwLp
    ldy #0
mbwDone:
    tya
    clc
    adc #MU_GRID_TOP
    rts

// --- muRedrawBand -- repaint from row A/X own line down to the band last row ---
//  In: A/X = the row an edit touched. muRedrawRows wants a range of SCREEN rows, and
//  which screen line holds that row is the walk answer (muBandRow), so a row that
//  is not on screen repaints from the top of the band.
muRedrawBand:
    jsr muBandRow
    ldx #MU_GRID_BOT+1
    jmp muRedrawRows

// --- muGridClear -- blank the eighteen grid rows ---
muGridClear:
    lda #MU_GRID_TOP
    sta muClrRow
muGdClLp:
    jsr muClearRow
    inc muClrRow
    lda muClrRow
    cmp #MU_GRID_BOT+1
    bcc muGdClLp
    rts

// --- muGdSpan -- rows the event just consumed covers, into MU_SPAN ---
//  The leftover jiffies ride into the next event of the same voice, so a run of
//  short notes is spread over rows without ever losing a fraction. The divisor is
//  the segment's unit: a tempo change re-seeds every voice's remainder, so the
//  carry never crosses one.
muGdSpan:
    ldx muSv
    lda muJifOut
    clc
    adc muVRem,x
    sta muValLo
    lda muJifOut+1
    adc #0
    sta muValHi
    lda #0
    sta muSpan
    sta muSpan+1
muGdSdLp:
    lda muValLo
    sec
    sbc muSegUnit
    tay
    lda muValHi
    sbc #0
    bcc muGdSdEnd
    sta muValHi
    tya
    sta muValLo
    inc muSpan
    bne muGdSdLp
    inc muSpan+1
    jmp muGdSdLp
muGdSdEnd:
    lda muValLo
    sta muVRem,x
    rts

// --- muGdAdv -- the voice's next event row ---
muGdAdv:
    ldx muSv
    lda muVRow,x
    clc
    adc muSpan
    sta muVRow,x
    lda muVRowHi,x
    adc muSpan+1
    sta muVRowHi,x
    rts

// --- muNumDur -- print A (1..64) without a leading zero ---
muNumDur:
    cmp #10
    bcc muNdOnes
    ldx #0
muNdTens:
    cmp #10
    bcc muNdRest
    sbc #10
    inx
    jmp muNdTens
muNdRest:
    pha
    txa
    clc
    adc #'0'
    jsr muPutC
    pla
muNdOnes:
    clc
    adc #'0'
    jmp muPutC

// --- muGdAtCell -- the write cursor goes to (MU_GDCOL, MU_LNSCR) ---
muGdAtCell:
    lda muLnScr
    ldy #MU_CURROW
    sta (dataPtr),y
    lda muGdCol
    ldy #MU_CURCOL
    sta (dataPtr),y
    rts

// --- muPitchSplit -- an option byte -> the pitch it sounds ---
//  In:  A = the option byte (a note, never a rest)
//  Out: A = the semitone (0-11), X = the octave (0-7), C = 1 when the accidental
//       rolled the pitch into the neighbouring octave
//  A sharp on B is the next octave's C and a flat on C the octave below's B, and
//  the player's note path rolls the semitone into 0-11 and steps the octave for
//  both, so B#5 is what a C-6 is and has to read as one. The octave field is
//  stored inverted (field = 7 - octave) because the player's table starts at
//  octave 7 and halves per field. At either end of that field a roll has no
//  octave to move into, so only the semitone rolls -- which is what the player
//  itself does with such an option.
//  Clobbers A, X, Y, muTmpH, muSem.
muPitchSplit:
    sta muTmpH                  // the option, read again for its octave
    lsr
    lsr
    lsr
    lsr
    lsr
    lsr                         // the type bits (6-7) now sit in 0-3
    tay
    lda muAccMod,y              // +1 sharp, 0 plain, -1 flat
    sta muSem
    lda muTmpH
    and #$07
    tay
    lda muLetterSem,y           // the letter's own semitone, C = 0 .. B = 11
    clc
    adc muSem                   // ...with the accidental on top of it
    sta muSem
    lda muTmpH
    lsr
    lsr
    lsr
    and #$07
    eor #7
    tax                         // X = the octave
    lda muSem
    bmi muPsBelow
    cmp #12
    bcc muPsDone                // inside the octave: nothing rolled, C = 0
    sbc #12                     // a sharp on B: the next octave's C
    inx
    cpx #8                      // ...if the field has a next octave at all
    bcc muPsRoll
    ldx #7
muPsRoll:
    sec
    rts
muPsBelow:
    clc
    adc #12                     // a flat on C: the octave below's B
    dex
    bpl muPsRoll                // ...if there is an octave below to move to
    inx
    jmp muPsRoll
muPsDone:
    rts

// --- muPutNoteName -- the two cells of a semitone's name ("C-", "C#") ---
//  In: A = the semitone, 0-11. muNoteNames holds two characters per semitone, so the
//  doubled index picks them. The octave is the caller's: it prints after this.
muPutNoteName:
    asl                         // two characters per semitone name
    sta muTmp
    ldx muTmp
    lda muNoteNames,x
    jsr muPutC
    ldx muTmp
    lda muNoteNames+1,x
    jmp muPutC

// --- muGdPitch -- three cells: the note's name and its octave ---
//  In: A = the option byte. It prints the pitch the option sounds, so a rolled
//  accidental shows as the note and octave the player turns it into.
muGdPitch:
    jsr muPitchSplit
    stx muTmpH                  // the octave waits: the name is two cells first
    jsr muPutNoteName
    lda muTmpH
    clc
    adc #'0'
    jmp muPutC

// --- muDrawPiano -- the generated keyboard rows ---
//  The pattern repeats every five cells (one octave is 40 px = 5 cells), so the
//  cell is chosen by its position inside the octave, not by the column. A cell is
//  one byte of data -- keys are vertical bars, so every pixel row of a cell is the
//  same pattern -- and muGlyFill expands it to the eight bytes a blit wants.
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
    tay
    lda muPianoCell,y
    ldx #7
muPFillLp:
    sta muGlyBuf,x
    dex
    bpl muPFillLp
    lda #<muGlyBuf
    sta srcPtr
    lda #>muGlyBuf
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
    cmp #MU_PIANO_ROWS
    bcc muPRowLp
    rts

// --- muBarPaint -- the bar, with the hovered and the active item in reverse ---
//  Called when either state changes, never per frame: the text goes down normally
//  and then the cells of the highlighted items are redrawn reversed. The item
//  comes from the hit map, so a highlight can never disagree with what a click
//  there would do.
muBarPaint:
    jsr muDrawBar
    jsr muDrawArm
    lda muTid                   // the reverse cells go straight to the framebuffer,
    cmp kbFocus                 //   which belongs to the displayed task
    beq muBpGo
    jmp muBpKeep
muBpGo:
    lda #M_BAR                  // the bar's text, read back out of the message file
    jsr muMsgSet
    lda #0
    sta muBarC
muBpLp:
    ldy muBarC
    cpy #M_BAR_LEN
    bcs muBpKeep
    lda muBarMap,y
    beq muBpNx
    cmp #MU_BAR_TIE             // the tie cell shows its own flag, always: the light IS
    bne muBpOther               //   the flag, so it needs no history and no share of
    lda muDurTie                //   muBarAct -- pressing PLY cannot lose it
    bne muBpRev
    jmp muBpNorm
muBpOther:
    cmp muBarHov
    beq muBpRev
    cmp muBarAct
    beq muBpRev
    cmp muBarHovP               // reversed by the last paint: put its cell back,
    beq muBpNorm                //   background nibble included
    cmp muBarActP
    bne muBpNx
muBpNorm:
    lda muMsgBuf,y
    ldx muBarC
    ldy #MU_BAR_ROW
    jsr kBlitChar
    jmp muBpNx
muBpRev:
    lda muMsgBuf,y              // the character at this column
    ldx muBarC
    ldy #MU_BAR_ROW
    jsr kBlitCharRev
muBpNx:
    inc muBarC
    jmp muBpLp
muBpKeep:
    lda muBarHov                // what the next paint has to restore
    sta muBarHovP
    lda muBarAct
    sta muBarActP
    rts

// --- muBarHover -- A = the item under the pointer, 0 = none ---
//  Repaints only when the answer changes, so moving the mouse across the bar
//  costs one redraw per item boundary.
muBarHover:
    cmp #MU_BAR_Q               // the lowest code with a handler, and ...
    bcs muBhHigh
    lda #0                      // ...0 and 1 are not items: nothing lights
muBhHigh:
    cmp #MU_BAR_INS+1           // MU_BAR_INS is the highest, so nothing above it
    bcc muBhLive                //   lights either -- the bar never invites a click
    lda #0                      //   that its dispatch chain would ignore
muBhLive:
    cmp muBarHov
    beq muBhRts
    sta muBarHov
    jmp muBarPaint
muBhRts:
    rts

// --- muBarActive -- A = the item doing something now (PLAY while it runs) ---
muBarActive:
    cmp muBarAct
    beq muBaRts
    sta muBarAct
    jmp muBarPaint
muBaRts:
    rts

// --- muBarPlayOff -- drop the PLAY highlight, leaving any other item lit ---
//  muBarAct holds the bar's one lit item, and the tie is a sticky one: it stays lit
//  until it is clicked off. The stopped path runs every frame, so clearing the byte
//  outright would wipe the tie's own light the frame after it was set.
muBarPlayOff:
    lda muBarAct
    cmp #MU_BAR_PLAY
    bne mbpRts
    lda #0
    jmp muBarActive
mbpRts:
    rts

muDrawBar:
    muAt(0, MU_BAR_ROW)
    lda #M_BAR
    jmp muMsg

// --- muDrawArm -- the bar's DUR field: the value cells and the dot cell ---
//  Columns 4-6 of the bar row, through kPrintCharAt -- the fixed-cell writer,
//  so the bottom row cannot scroll. The dot cell shows ' ' plain, '.' dotted,
//  ':' double-dotted, '3' triplet.
muDrawArm:
    lda muDurArm
    and #$1c
    lsr
    lsr
    cmp #1
    bne muDaPlain
    lda muDurArm                // class 1: UL or UV, bit 5 picks
    and #$20
    beq muDaUl
    lda #'V'
    bne muDaHave
muDaUl:
    lda #'L'
muDaHave:
    sta muTmp                   // the second cell's character
    lda #'U'
    bne muDaC1
muDaPlain:
    tax
    lda muArmCh2,x
    sta muTmp
    lda muArmCh1,x
muDaC1:
    ldx #4
    ldy #MU_BAR_ROW
    jsr kPrintCharAt
    lda muTmp
    ldx #5
    ldy #MU_BAR_ROW
    jsr kPrintCharAt
    lda muDurArm                // the dot cell: bits 5 and 7, packed to 0-5
    and #$a0
    lsr
    lsr
    lsr
    lsr
    lsr
    tax
    lda muDotCh,x
    ldx #6
    ldy #MU_BAR_ROW
    jsr kPrintCharAt
    rts

muDrawRefuse:
    muAt(0, 0)
    lda #M_USAGE
    jsr muMsg
    muAt(0, 1)
    lda #M_USAGE2
    jmp muMsg

// ==========================================
//  Text helpers
// ==========================================
// --- muPutC -- print A at the text cursor and advance one cell ---
//  The blits clobber A, X and Y, so the character rides the task's stack and the
//  column is re-read afterwards. The cell is always written: whether a line is
//  drawn at all is decided once, when the line is produced (muLnSpot).
//  MU_REVTEXT draws the cell reversed (a solid bar in the text colour with the
//  glyph in bg0): the command lines and the lane header read as bars, so they
//  cannot be mistaken for notes. Both blits write the FRAMEBUFFER only, so they
//  are used only while this task owns the display -- a background task keeps the
//  routed writer and lands in its own surface, unreversed, until it is back.
//  A plain cell goes through kBlitChar, which sets the paper nibble back to bg0:
//  a bar leaves the paper on the text colour and a routed write keeps whatever
//  paper nibble it finds, so a cleared bar would stay a solid block of colour.
muPutC:
    pha
    ldy #MU_CURCOL
    lda (dataPtr),y
    tax
    ldy #MU_CURROW
    lda (dataPtr),y
    tay
    lda muTid
    cmp kbFocus
    bne muPcSlot
    lda muRevText
    beq muPcPlain
    pla
    jsr kBlitCharRev
    jmp muPcAdv
muPcPlain:
    pla
    jsr kBlitChar
    jmp muPcAdv
muPcSlot:
    pla
    jsr kPrintCharAt
muPcAdv:
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

// --- muNum16 -- print A/X (lo/hi) in decimal, without leading zeroes ---
//  Repeated subtraction against a five-entry power table: there is no divide on
//  the 6502, and five digits cover every 16-bit value.
muNum16:
    sta muNumVal
    stx muNumVal+1
    lda #0
    sta muNumSquash             // 0 = no digit printed yet
    sta muNumPow
muN16Pow:
    lda #0
    sta muNumDig
muN16Sub:
    ldx muNumPow
    lda muNumVal
    sec
    sbc muPowLo,x
    sta muTmpL
    lda muNumVal+1
    sbc muPowHi,x
    bcc muN16Done               // it borrowed: the value stands as it was
    sta muNumVal+1
    lda muTmpL
    sta muNumVal
    inc muNumDig
    jmp muN16Sub
muN16Done:
    lda muNumDig
    ora muNumSquash
    bne muN16Print
    lda muNumPow
    cmp #4                      // the ones always print
    bne muN16Next
muN16Print:
    lda muNumDig
    clc
    adc #'0'
    jsr muPutC
    lda #1
    sta muNumSquash
muN16Next:
    inc muNumPow
    lda muNumPow
    cmp #5
    bcc muN16Pow
    rts

// --- muTextAt -- print the NUL-terminated string at (MU_STRLO/MU_STRHI) ---
//  srcPtr is reloaded before every character because kPrintCharAt clobbers it,
//  and the index lives in the instance because it clobbers Y as well.
// --- muTextAt -- print MU_STR at the text cursor ---
//  One cell at a time through muPutC, so a caller that wants the text reversed
//  (the lane header) gets it without a second writer. srcPtr is re-derived every
//  pass because muPutC's kPrintCharAt clobbers it.
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
    jsr muPutC
    ldy #MU_TMPIDX
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    jmp muTextLp
muTextDone:
    rts

// --- muMsg -- load one message and print it at the text cursor ---
//  In: A = a M_* index (musedit-msgs.inc). What most sites want: the message goes
//  where the cursor is, exactly as muTextAt would put it.
muMsg:
    jsr muMsgSet
    jmp muTextAt

// --- muMsgGo -- one message, straight onto the head row ---
//  In: A = a M_* index. Wherever a refusal or a done-notice is the last thing a
//  handler says, it says it through here: the set and the print are one tail.
muMsgGo:
    jsr muMsgSet
    jmp muStatus

// --- muMsgSet -- load one message and point MU_STR at it, without printing ---
//  In: A = a M_* index. The file is one MU_MSG_SLOT-byte record per index, so the
//  offset is index * 64 and needs no table; ONE kReuReadAt brings the whole message
//  in, and MU_STR points at that buffer -- the text is never copied. The buffer holds
//  the message until the next one is read, so a caller prints before it reports
//  another message.
//  Clobbers kArg..kArg+7, kPtr and srcPtr (kReuReadAt), so it must not be called
//  where a kernel argument is already in hand.
muMsgSet:
    pha
    lsr
    lsr                         // the offset's high byte: index / 4
    sta kArg+3
    pla
    and #$03
    asl
    asl
    asl
    asl
    asl
    asl                         // ...and its low byte: (index AND 3) * 64
    sta kArg+2
    ldx #<muMsgRec
    ldy #>muMsgRec
    stx kArg
    sty kArg+1
    ldx #<muMsgBuf
    ldy #>muMsgBuf
    stx kPtr
    sty kPtr+1
    lda #MU_MSG_SLOT
    sta kArg+6
    lda #0
    sta kArg+7                  // len = one record
    jsr kReuReadAt
    lda #0
    sta muMsgBuf+MU_MSG_SLOT-1  // a read clamped by the end of the file must still
                                //   terminate the message
    muSet(MU_STRLO, <muMsgBuf)
    muSet(MU_STRHI, >muMsgBuf)
    rts

// --- muMsgOpen -- resolve the message file, once, before anything is printed ---
//  Out: C=0 muMsgRec holds its record. C=1 there is no file, or no filesystem: the
//  caller can only leave, because there is then no text to say why.
muMsgOpen:
    lda #<muMsgName
    sta kArg
    lda #>muMsgName
    sta kArg+1
    ldx #<muMsgRec
    ldy #>muMsgRec
    stx kPtr
    sty kPtr+1
    jmp kReuOpen

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
    jsr muPutC
    inc muNameIdx
    jmp muNameLp
muNameDone:
    rts

// --- muClearRow0 -- forty spaces across the head row, fixed-cell ---
//  The column counter lives in the instance and the loop index on the stack,
//  because kPrintCharAt clobbers A, X and Y.
muClearRow0:
    lda #0
    sta muClrRow
muClearRow:
    lda muClrRow
    cmp muCurScr                // blanking the row the glyph is in forgets it
    bne muClrKeep
    lda #$ff
    sta muCurScr
muClrKeep:
    lda muClrRow
    ldy #MU_CURROW
    sta (dataPtr),y
    muSet(MU_CURCOL, 0)
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
    jsr muStatArm
    jmp muTextAt

// --- muStatArm -- start the status countdown ---
//  Its lifetime is counted in DISPLAYED FRAMES, not in idle passes: the idle
//  loop runs many times per frame, so a pass-based countdown retires a message
//  in milliseconds and nothing can ever be read. FrameCount is 8-bit and wraps,
//  so it is compared for change, never subtracted.
muStatArm:
    lda #MU_STAT_FRAMES
    sta muStatT
    lda frameCount
    sta muStatF
    rts

// --- muStatusTick -- once per idle pass: retire the status when it is due ---
muStatusTick:
    lda muStatT
    beq muStatTickRts
    lda frameCount
    cmp muStatF
    beq muStatTickRts           // not a new frame yet
    sta muStatF
    dec muStatT
    bne muStatTickRts
    jsr muDrawHead              // the name, the lengths and the readout are back
    jmp muDrawLanes             // ...and the lane headers, which a status may cover
muStatTickRts:
    rts

// ==========================================
//  Cursor
// ==========================================
//  MU_CURSCR is the screen row the glyph was actually drawn at, $ff when it is not
//  drawn. That is what a hide blanks, so hiding twice does nothing: during playback
//  the band scrolls the glyph away, and a hide that re-derived the cell from MU_ROW
//  after the scroll would blank a cell of the line that had just arrived.
//  Both routines END in `rts`, not in a tail jump: they are called from more
//  than one place, and a tail jump into kPrintCharAt would let its `rts` pop
//  whichever return address happens to be on top -- the caller's, or an earlier
//  jsr's -- which is a stack walk, not a call.
// --- muCurCol -- X = the cursor's column: the free cell before its lane ---
//  The glyph sits in the gap left of the cursor's lane, so it says which voice the
//  cursor will act on as well as which line it is on. Each lane starts two cells
//  past the previous one's last, so that gap holds nothing else.
muCurCol:
    ldy #MU_VOICE
    lda (dataPtr),y
    tax
    lda muLaneCol,x
    tax
    dex
    rts

muCursorShow:
    ldy #MU_ROW
    lda (dataPtr),y
    sta muCurScr
    jsr muCurCol                // X = the column the glyph goes in
    stx muCurX                  // remembered: a hide blanks THAT cell, and the voice
                                //   may have changed by the time it runs
    lda #'>'
    ldy muCurScr
    jsr kPrintCharAt
    rts

muCursorHide:
    lda muCurScr
    cmp #$ff
    beq muChRts                 // not drawn: there is no cell to take it out of
    ldx muCurX
    lda #MU_SPACE
    ldy muCurScr
    jsr kPrintCharAt
    lda #$ff
    sta muCurScr
muChRts:
    rts

// ==========================================
//  The key loop
// ==========================================
//  kReadKey returns a SCREEN CODE and 0 for an empty queue (a key-UP pushes $ff,
//  which it folds to 0). The special codes are the kernel's: $84 CRSR down,
//  $85 up, $86 right, $87 left (SHIFT picks the second pair).
muKeyLoop:
    jsr kReadKey
    bne muKeyHas
    jmp muKeyIdle               // out of branch range: invert and jump
muKeyHas:
    cmp #'q'
    bne muKeyNotQ
    jsr muQuit                  // returns only on the dirty warning
    jmp muKeyIdle
muKeyNotQ:
    cmp #$53                    // SHIFT+S: save
    bne muKeyNotSave
    jsr muSaveTry
    jmp muKeyIdle
muKeyNotSave:
    cmp #$55                    // SHIFT+U: a finer row unit
    bne muKeyNotU2
    jsr muPlaying               // the grid is drawn in the OLD unit: refining it while
    bcs muKnUIdle               //   the playhead runs leaves the two rows apart
    jsr muUnitCycle
    jmp muCurRedraw
muKnUIdle:
    jmp muKeyIdle
muKeyNotU2:
    cmp #$44                    // SHIFT+D: the call and its definition
    bne muKeyNotLink
    jsr muPlaying               // a jump would fight the playhead's follow
    bcs muKnkIdle               // out of branch range: the idle loop is far below
    jmp muLinkKey               // muLinkKey ends in the idle loop, not in an rts
muKnkIdle:
    jmp muKeyIdle
muKeyNotLink:
    cmp #'-'                    // octave down for the note keys
    bne muKeyNotDash
    lda muKbdOct
    beq muKnDashDone
    dec muKbdOct
muKnDashDone:
    jmp muKeyIdle
muKeyNotDash:
    cmp #'+'                    // octave up
    bne muKeyNotPlus
    lda muKbdOct
    cmp #7
    bcs muKnPlusDone
    inc muKbdOct
muKnPlusDone:
    jmp muKeyIdle
muKeyNotPlus:
    cmp #'1'                    // 1-9: arm the duration a note is written with
    bcc muKeyNotArm
    cmp #'9'+1
    bcs muKeyNotArm
    jsr muArmKey
    jmp muKeyIdle
muKeyNotArm:
    cmp #'.'                    // cycle the dots on the armed duration
    bne muKeyNotDot
    jsr muDotCycle
    jmp muKeyIdle
muKeyNotDot:
    cmp #','                    // arm or drop the tie, as the bar's TIE cell does
    bne muKeyNotTie
    jsr muTieToggle
    jmp muKeyIdle
muKeyNotTie:
    jsr muKeyNote               // a note key enters its pitch at the cursor
    bcc muKeyNotNote
    sta muNoteLin               // the pitch to write, not MU_HOV -- the pointer owns that
    jsr muPlaying               // it IS a note key: refuse while the player runs
    bcs muKndIdle
    lda muNoteLin
    jsr muAudNote               // it sounds as it arrives
    jsr muNoteClick
muKndIdle:
    jmp muKeyIdle
muKeyNotNote:
    lda muTmp2                  // the key as it came in
    cmp #$80                    // INST/DEL: the event at the cursor becomes a rest
    bne muKeyNotRest
    jsr muPlaying
    bcs muKnrIdle
    jsr muRestKey
muKnrIdle:
    jmp muKeyIdle
muKeyNotRest:
    cmp #$88                    // SHIFT+INST/DEL: delete the event, close the gap
    bne muKeyNotDel
    jsr muPlaying
    bcs muKndlIdle
    jsr muDelKey2
muKndlIdle:
    jmp muKeyIdle
muKeyNotDel:
    cmp #'c'                    // C: author the command on this row
    beq muKeyCmd
    cmp #$43                    // ...and SHIFT+C does the same
    bne muKeyNotCmd
muKeyCmd:
    jsr muObjKey
    jmp muKeyIdle
muKeyNotCmd:
    cmp #'z'                    // Z: undo
    bne muKeyNotZ
    jsr muPlaying
    bcs muKnzIdle
    jsr muUndo
muKnzIdle:
    jmp muKeyIdle
muKeyNotZ:
    cmp #'x'                    // X: redo
    bne muKeyNotRedo
    jsr muPlaying
    bcs muKnrdIdle
    jsr muRedo
muKnrdIdle:
    jmp muKeyIdle
muKeyNotRedo:
    cmp #'i'                    // I: insert one row at the cursor, all voices
    bne muKeyNotIns
    jsr muPlaying
    bcs muKniIdle
    lda #1
    jsr muInsertTime
muKniIdle:
    jmp muKeyIdle
muKeyNotIns:
    cmp #$49                    // SHIFT+I: insert a whole bar
    bne muKeyNotInsBar
    jsr muPlaying
    bcs muKnibIdle
    jsr muCurBar                // the rows a bar has at the cursor's tempo
    jsr muInsertTime
muKnibIdle:
    jmp muKeyIdle
muKeyNotInsBar:
    cmp #'r'                    // R: delete one row from every voice
    bne muKeyNotDelRow
    jsr muPlaying
    bcs muDrRowIdle
    lda #1
    jsr muDelRows
muDrRowIdle:
    jmp muKeyIdle
muKeyNotDelRow:
    cmp #$52                    // SHIFT+R: delete a whole bar
    bne muKeyNotDelBar
    jsr muPlaying
    bcs muDrBarIdle
    jsr muCurBar                // the rows a bar has at the cursor's tempo
    jsr muDelRows
muDrBarIdle:
    jmp muKeyIdle
muKeyNotDelBar:
    cmp #'m'                    // M: toggle insert/overwrite mode
    bne muKeyNotMode
    jsr muInsToggle
    jmp muKeyIdle
muKeyNotMode:
    cmp #MU_SPACE               // SPACE: toggle play/stop
    bne muKeyNotSpace
    jsr muPlayToggle
    jmp muKeyIdle
muKeyNotSpace:
    cmp #$84                    // CRSR down
    bne muKeyN1
    lda muPlayOn                // while the player runs the cursor IS the playhead:
    beq muKnDownGo              //   the follow would drag it back, so up and down are
    jmp muKeyIdle               //   refused outright. Left and right still work --
muKnDownGo:                     //   they point the dots at another voice.
    jmp muCurDown
muKeyN1:
    cmp #$85                    // CRSR up
    bne muKeyN2
    lda muPlayOn
    beq muKnUpGo
    jmp muKeyIdle
muKnUpGo:
    jmp muCurUp
muKeyN2:
    cmp #$86                    // CRSR right: next voice
    bne muKeyN3
    jmp muCurRight
muKeyN3:
    cmp #$87                    // CRSR left: previous voice
    bne muKeyPgUp
    jmp muCurLeft
muKeyPgUp:
    cmp #'<'                    // SHIFT+, -- the window a page back
    bne muKeyPgDn
    jsr muPlaying
    bcs muKeyIdle
    jmp muCurUpPage
muKeyPgDn:
    cmp #'>'                    // SHIFT+. -- the window a page on
    bne muKeyBar
    jsr muPlaying
    bcs muKeyIdle
    jmp muPageDown
muKeyBar:
    cmp #$42                    // SHIFT+B -- type the bar to go to
    bne muKeyBarNext
    jmp muBarPrompt
muKeyBarNext:
    cmp #$4e                    // SHIFT+N -- the bar after this one
    bne muKeyBarPrev
    jmp muBarNext
muKeyBarPrev:
    cmp #$50                    // SHIFT+P -- the bar before this one
    bne muKeyIdle
    jmp muBarPrev
muKeyIdle:
    jsr muPointerTick
    jsr muStatusTick
    jsr muPlayTick
    jsr muAudTick
    jsr muHovDot
    jsr muPhWatch
    jsr muPhTick
    lda #SYS_YIELD
    jsr syscall
    jmp muKeyLoop

// --- muPageDown -- the window a page on ---
//  The band's own bottom line names the row it holds, and the walk reached it, so
//  that row is a page forward and the band it starts cannot be empty. An empty
//  entry there is the tune ending inside the band, which has no page to show.
muPageDown:
    lda muLnTbHi+MU_GRID_ROWS-1
    cmp #$ff
    bne muPdGo
    jmp muKeyIdle
muPdGo:
    sta muTop+1
    lda muLnTbLo+MU_GRID_ROWS-1
    sta muTop
    lda #0
    sta muToff
    jmp muCurRedraw

muCurDown:
    jsr muCurDownGo
    jmp muKeyIdle

// --- muCurDownGo -- one line down, scrolling the band when the cursor is at the
//  bottom. The key handler and insert mode's cursor step both walk through this.
muCurDownGo:
    ldy #MU_ROW
    lda (dataPtr),y
    cmp #MU_GRID_BOT
    bne muCdgIn
    //  at the bottom: the window moves instead of the cursor, the band shifts up
    //  through the kernel and only the line that arrives at the bottom is drawn.
    //  muScrollBand brings muTop with it, because the line's row is what the window
    //  now starts on -- a command's line does not advance the row at all. The glyph
    //  comes out before the band moves and goes back on after it.
    jsr muCursorHide
    jsr muScrollBand
    jsr muHeadTail
    jmp muCursorShow
muCdgIn:
    jsr muCursorHide
    ldy #MU_ROW
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    jsr muCursorShow
    jmp muHeadTail

muCurUp:
    ldy #MU_ROW
    lda (dataPtr),y
    cmp #MU_GRID_TOP
    bne muCurUpIn
    lda muTop
    ora muTop+1
    bne muCurUpPage
    lda muToff
    beq muCurDone               // the tune's first line is already at the top
muCurUpPage:
    //  at the top: the window moves a whole page, so the grid is walked again
    //  once per page rather than once per row. The line above the window has
    //  never been walked -- the interpreter only runs forward -- so a repaint is
    //  the only way to see it, and a page amortises it over MU_GRID_ROWS rows.
    //  A page is counted in ROWS, so the window lands on a row's first line and
    //  the line offset starts again from there.
    lda muTop
    sec
    sbc #MU_GRID_ROWS
    tay
    lda muTop+1
    sbc #0
    bcs muCurUpSet
    lda #0                      // less than a page in: the first row is the new top
    tay
muCurUpSet:
    sta muTop+1
    sty muTop
    lda #0
    sta muToff
    jmp muCurRedraw             // the cursor stays on the TOP line: the window moves
                                //   under it, so holding UP walks back a page at a
                                //   time and the clamp to row 0 leaves the cursor on
                                //   the tune's first line, not a page below it
muCurUpIn:
    jsr muCursorHide
    ldy #MU_ROW
    lda (dataPtr),y
    sec
    sbc #1
    sta (dataPtr),y
    jsr muCursorShow
    jsr muHeadTail
    jmp muKeyIdle

// --- muCurRedraw -- the window moved: draw it again, head and cursor last ---
muCurRedraw:
    jsr muRedrawAll
    jmp muKeyIdle

// --- muRedrawAll -- the grid, the head and the cursor, ending balanced ---
muRedrawAll:
    jsr muDrawGrid
    jsr muDrawHead
    jmp muCursorShow

// --- muRedrawNoHead -- a full grid repaint that leaves the header alone ---
//  Undo and redo repaint through this: the header shows the cursor's bar and
//  row, which an undo never moves, so only the grid and the cursor are redrawn.
muRedrawNoHead:
    jsr muDrawGrid
    jmp muCursorShow

muCurRight:
    ldy #MU_VOICE
    lda (dataPtr),y
    cmp #2
    beq muCurDone
    jsr muCursorHide            // out of the lane it is leaving, in the new one
    ldy #MU_VOICE
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    jsr muCursorShow
    jmp muKeyIdle

muCurLeft:
    ldy #MU_VOICE
    lda (dataPtr),y
    beq muCurDone
    jsr muCursorHide
    ldy #MU_VOICE
    lda (dataPtr),y
    sec
    sbc #1
    sta (dataPtr),y
    jsr muCursorShow
muCurDone:
    jmp muKeyIdle

// ==========================================
//  Editing: seeking and the image write
// ==========================================
// --- muSeek -- where the cursor's row falls in the cursor's voice ---
//  Walks the same simulator the grid does, so "the row under the cursor" means
//  the row the player would be playing.
//  Out: MU_SEKKIND = 0 an event starts on that row (replace it),
//                   1 the row is inside an event (split it),
//                   2 the row is in a gap or past the last timed event (fill it);
//       MU_SEKLO/HI = that event's offset in the image, MU_SEKSPAN its rows.
//  The record is primed with a virtual zero-length event at the voice's start,
//  so an empty voice reports every row as a gap measured from row 0 and the
//  fill inserts at the voice's first pair.
muSeek:
    ldy #MU_VOICE
    lda (dataPtr),y
    sta muSkV
    jsr muCursorAbs
    sta muTgtLo
    stx muTgtHi
// --- muSeekAt -- where a given voice's row MU_TGT falls, like muSeek ---
//  In: muSkV = voice, muTgtLo/Hi = the absolute timeline row. Out: as muSeek.
//  Insert seeks each of the three voices at the cursor's row through this.
muSeekAt:
    jsr muSimInit
    ldx #2
muSkZero:
    lda #0
    sta muVRow,x
    sta muVRowHi,x
    sta muVRem,x
    dex
    bpl muSkZero
    lda muSkV
    sta muSv                    // the prime: row 0, span 0, at the voice's start
    lda #0
    sta muSpan
    sta muSpan+1
    sta muTmpL
    sta muTmpH
    jsr muSkRecord
muSkLp:
    jsr muStep
    bcs muSkCovered             // the walk ran out: the row is inside the last
                                //   event, or past it -- the same test decides
    lda muNoteOut
    beq muSkLp                  // a voice halted: nothing to record, walk on
    jsr muGdSpan
    ldx muSv
    cpx muSkV
    bne muSkAdv
    lda muOn,x
    beq muSkCovered             // the voice halted: gap or inside its last event
    lda muVRow,x
    sta muTmpL
    lda muVRowHi,x
    sta muTmpH
    lda muTmpL
    sec
    sbc muTgtLo
    lda muTmpH
    sbc muTgtHi
    bcs muSkAtOrAfter
    jsr muSkRecord              // an event before the row: it may be the one covering it
    jmp muSkAdv
muSkAtOrAfter:
    lda muTmpL
    cmp muTgtLo
    bne muSkCovered
    lda muTmpH
    cmp muTgtHi
    bne muSkCovered
    jsr muSkRecord
    lda #0
    sta muSekKind               // an onset on this very row
    rts
muSkCovered:
    //  --- the recorded event may have ended before this row: that is a gap,
    //      which takes inserted rests rather than a split ---
    lda muTgtLo
    sec
    sbc muSekEnd
    lda muTgtHi
    sbc muSekEnd+1
    bcs muSkGap
    lda #1
    sta muSekKind               // the recorded event runs past this row
    rts
muSkGap:
    lda #2
    sta muSekKind
    rts
muSkAdv:
    jsr muGdAdv
    jmp muSkLp

// --- muSkRecord -- remember the event that just played, as an image offset ---
muSkRecord:
    ldx muSv
    lda muPtrLo,x               // the event is the pair behind the pointer, and the
    sec                         //   pointer is already an image offset -- which is
    sbc #2                      //   what the seek table wants
    sta muSekLo
    lda muPtrHi,x
    sbc #0
    sta muSekHi
    lda muSpan
    sta muSekSpan
    lda muSpan+1
    sta muSekSpan+1
    lda muTmpL                  // the row the event starts on
    sta muSekRow
    lda muTmpH
    sta muSekRow+1
    lda muTmpL                  // ...and the row it ends on
    clc
    adc muSpan
    sta muSekEnd
    lda muTmpH
    adc muSpan+1
    sta muSekEnd+1
    lda muSimT                  // the tempo the event played at
    sta muSekTem
    lda #1
    sta muSekKind               // provisionally inside: muSkCovered decides gap vs split
    rts

// --- muSeekKPtr -- kPtr = the image at MU_SEKLO/HI, in window 0 ---
//  The window copies one page, so kPtr is good for that page only -- and an event
//  is a 2-byte pair at an EVEN offset, so a pair never straddles one.
//  Clobbers A, Y, kPtr, srcPtr; C comes back 0 and X survives.
muSeekKPtr:
    txa
    pha
    lda muSekLo
    sta muWinOfs
    lda muSekHi
    jsr muWinSeat0
    pla
    tax
    rts

// ==========================================
//  Audition -- a note sounds as it is entered
// ==========================================
//  The editor holds no player while it is stopped, so the pitch is written to SID
//  voice 1 directly and released a few frames later. It is NOT known whether
//  another task owns the chip (sid.lib keeps that in its own pages, which a caller
//  cannot read), so the gate bits are the test: any voice gated means somebody is
//  playing and the audition stays silent rather than fighting it.
//  A player that is mid-tick when we look can still be interrupted once; it
//  rewrites its voice controls every tick, so the loss is one tick of one voice.
// --- muAudFreq -- A = the linear note (0-95) -> muAudLo/muAudHi, the SID frequency ---
//  The table is the player's PAL one, an octave down: octave 7 doubles it (B clamps
//  at $ffff) and every octave below halves it once per step.
//  The machine's declared speed then shifts the result, exactly as sid.lib shifts a
//  tune's pitch: at 200% the SID's accumulator runs twice as fast, so a register value
//  that sounds C-4 at 100% sounds C-5. `speed.com` is what sets MU_SPEEDSHIFT, and the
//  image's boot.bat declares the emulator's speed, so an audition written without this
//  term is one octave high on every machine that declares anything but 100%.
muAudFreq:
    ldx #0
muAfDiv:
    cmp #12
    bcc muAfDivDone
    sec
    sbc #12
    inx
    jmp muAfDiv
muAfDivDone:
    sta muTmp                   // the semitone inside the octave
    stx muTmp2                  // ...and the octave
    lda #7
    sec
    sbc muTmp2                  // stored as the option byte is: 0 is octave 7
    beq muAfTop
    tay
    dey                         // octave 7 is the table's own octave once doubled,
    ldx muTmp                   //   so every step below halves it one time fewer
    lda muAudLoTab,x
    sta muAudLo
    lda muAudHiTab,x
    sta muAudHi
muAfHalve:
    cpy #0
    beq muAfRts
    lsr muAudHi
    ror muAudLo
    dey
    jmp muAfHalve
muAfTop:
    ldx muTmp
    lda muAudLoTab,x
    sta muAudLo
    asl
    sta muAudLo
    lda muAudHiTab,x
    rol
    bcc muAfTopOk
    lda #$ff                    // B: $106D0 does not fit, so it clamps
    sta muAudHi
muAfTopOk:
    sta muAudHi
muAfRts:
    ldy sysSpeedShift           // the machine's speed, in octaves: +1 = 200% = one
    beq muAfDone                //   octave down, -1 = 50% = one up
    bmi muAfUp
muAfDn:
    lsr muAudHi
    ror muAudLo
    dey
    bne muAfDn
    rts
muAfUp:
    asl muAudLo
    rol muAudHi
    bcc muAfUpOk
    lda #$ff                    // a clamp, as above: it cannot go higher
    sta muAudHi
muAfUpOk:
    iny
    bne muAfUp
muAfDone:
    rts

// --- muAudNote -- sound the linear note in A on voice 1, unless another player has the chip ---
//  A is parked on the stack because the gate test reads three registers first, and a
//  refused note has to leave with the same stack it arrived with.
muAudNote:
    pha
    lda muAudOn
    bne muAudGo                 // our own note: retrigger it
    lda $d404                   // three gates, and only when no note of ours sounds
    and #$01
    bne muAudNo
    lda $d40b
    and #$01
    bne muAudNo
    lda $d412
    and #$01
    bne muAudNo
muAudGo:
    pla
    jsr muAudFreq
    lda muAudLo
    sta $d400
    lda muAudHi
    sta $d401
    lda #0
    sta $d402                   // a sawtooth ignores the pulse width, but a player
    sta $d403                   //   that resumes should not find it stale
    sta $d405                   // attack 2 ms, decay 6 ms
    lda #$f0                    // sustain 15, release 6 ms
    sta $d406
    lda $d418                   // keep the filter bits, make sure there is volume
    and #$f0
    ora #$0f
    sta $d418
    lda #$21                    // sawtooth, gate on
    sta $d404
    lda frameCount
    sta muAudStart
    lda #1
    sta muAudOn
    rts
muAudNo:
    pla                         // another task's note: the pitch is dropped, unsounded
    rts

// --- muAudTick -- release the audition once its frames are up ---
//  Runs from the idle pass, and measures ELAPSED frames rather than waiting for one
//  to be reached: a pass is slower than a frame, so an equality test would step over
//  its own deadline and the note would drone until the counter wrapped.
muAudTick:
    lda muAudOn
    beq muAuRts
    lda muPlayOn
    beq muAuChk
    lda #0                      // the player owns the chip now: drop ours silently,
    sta muAudOn                 //   because clearing a gate under it would cut a note
    rts
muAuChk:
    lda frameCount
    sec
    sbc muAudStart
    cmp #MU_AUD_FRAMES
    bcc muAuRts
    lda #0
    sta muAudOn
    lda $d404                   // gate off: the release phase ends the note
    and #$fe
    sta $d404
muAuRts:
    rts


// --- muNoteClick -- a piano click enters its pitch on the cursor's row ---
//  The pitch arrives in muNoteLin: the pointer publishes the key it is over in
//  MU_HOV, but a note key also calls this, and MU_HOV belongs to the pointer. The
//  armed length is checked against the tempo in force at that row first, so an
//  entry the player could not express writes nothing.
//  A command has a line of its own above the row it belongs to, and that line shares
//  the row without holding a note: a note belongs on the row's OWN line, so a key
//  pressed on a command line is refused rather than rewriting the event below it,
//  which would change a length the cursor is not pointing at.
muNoteClick:
    jsr muPlaying               // a mouse click must not edit while playing
    bcc muNcGo
    rts
muNcGo:
    jsr muCursorIdx             // the kind of line the cursor stands on
    tay
    lda muLnKind,y
    beq muNcNoEvt
    lda muNoteLin
    cmp #$fe
    bcs muNcRts                 // nothing under the pointer
    jsr muNoteOpt               // A = the note option byte for that key
    jsr muSeek
    lda muSekTem                // the tempo in force at the cursor's row
    sta muJifTem
    lda muDurArm
    jsr muNoteJiffies
    bcs muNcIllegal
    sta muDurJif                // the armed length, for the ones below
    jsr muPhArm                 // a body's bytes are every call site's bytes
    lda muInsMode
    beq muNcOvr
    jmp muNoteInsMode           // insert mode: push the rows below down
muNcOvr:
    lda muSekKind
    beq muNcReplace            // an onset on this row: the pair is replaced
    cmp #1
    bne muNcKind2
    jmp muNcSplit
muNcKind2:
    jmp muFillNote              // a gap, or past the voice's last event
muNcReplace:
    jsr muRecAtSek  // the undo record: 2 bytes at the onset...
    lda #2
    sta muRecOldN
    sta muRecNewN
    jsr muCapOld
    jsr muSeekKPtr
    ldy #0
    lda muDurArm                // the armed length and tie, whatever the event there
    ldx muDurTie                //   was: the bar's DUR is what the next note written is
    beq muNcStore               //   measured with, so it replaces a note's length too
    ora #$40                    // bit 6: the player holds it into the next note
muNcStore:
    sta (kPtr),y
    iny
    lda muNewOpt
    sta (kPtr),y
    jsr muWinFlush0             // the pair reaches its page before the record reads it
    jsr muPushEdit             // ...become 2 new bytes there
    lda muTgtLo                 // the note's length may differ, so every row below
    ldx muTgtHi                 //   moves: repaint from the note's own row down
    jsr muRedrawBand
    lda muTgtLo                 // ...and leave the cursor on the row that changed
    ldx muTgtHi
    jmp muCurSettleNext
muNcIllegal:
    lda #M_ILLEGAL
    jmp muMsgGo
muNcNoEvt:
    lda #M_NO_EVENT             // a command's line holds no note to write
    jmp muMsgGo
muNcRts:
    rts


// --- muRowsToJif -- A = rows x the row unit at the cursor, in jiffies ---
//  In: A/X = rows. Out: A = the jiffies, also left in muValLo; C=1 the value
//  does not fit one byte, so no duration can express it.
//  The unit is the one the grid draws the cursor's row with, which a mid-tune TEM
//  makes local to that row. MU_JIFTEM is handed back because muFit borrows it and
//  a caller may be holding a tempo of its own for muNoteJiffies.
muRowsToJif:
    sta muCountLo
    stx muCountHi
    lda muJifTem
    pha
    jsr muCurTem
    jsr muFit
    pla
    sta muJifTem
    lda #0
    sta muValLo
muRjLp:
    lda muCountLo
    ora muCountHi
    beq muRjDone
    lda muValLo
    clc
    adc muFitU
    bcs muRjBig
    sta muValLo
    lda muCountLo
    bne muRjDec
    dec muCountHi
muRjDec:
    dec muCountLo
    jmp muRjLp
muRjDone:
    lda muValLo
    clc
    rts
muRjBig:
    lda #$ff
    sec
    rts

// --- muSplitLens -- the three runs of a split, in jiffies ---
//  In:  MU_SEKDUR (the pair under the cursor), MU_SEKROW (the row it starts on),
//       MU_TGT (the cursor's row), MU_DURJIF (the armed length)
//  Out: muSplitH = the rows before the cursor, muSplitN = the new note,
//       muSplitT = what the event has left after the note (0 = no tail).
//  The head and the tail are on the event's own option, so the three together
//  last exactly as long as the event did. A tail under two jiffies is dropped and
//  the note takes it: one jiffy is not a length any tempo writes.
muSplitLens:
    lda muSekDur
    jsr muNoteJiffies           // the event's whole length
    bcc muSlHave
    sec                         // the tune's own pair is not a length: refuse
    rts
muSlHave:
    sta muLenJ2
    lda #0
    sta muLenJ2+1
    lda muTgtLo                 // ...and the rows of it before the cursor
    sec
    sbc muSekRow
    sta muCountLo
    lda muTgtHi
    sbc muSekRow+1
    sta muCountHi
    lda muCountLo
    ldx muCountHi
    jsr muRowsToJif
    bcc muSlHead
    lda #$ff                    // past every length: the clamp below takes it
muSlHead:
    sta muValLo
    lda muLenJ2                 // the head cannot take the whole event
    sec
    sbc #1
    bcs muSlCap
    lda #0
muSlCap:
    cmp muValLo                 // A = the most the head may take; the smaller of
    bcc muSlSet                 //   that and the rows before the cursor wins
    lda muValLo
muSlSet:
    sta muSplitH
    lda #0
    sta muSplitH+1
    lda muLenJ2                 // the jiffies from the cursor to the event's end
    sec
    sbc muSplitH
    sta muCountLo
    lda muLenJ2+1
    sbc muSplitH+1
    sta muCountHi
    lda muDurJif                // the armed note, unless less than that is left
    beq muSlAll
    ldx muCountHi
    bne muSlNote
    cmp muCountLo
    bcc muSlNote
    lda muCountLo
muSlNote:
    sta muSplitN
    lda #0
    sta muSplitN+1
    lda muCountLo               // the tail is what the note leaves
    sec
    sbc muSplitN
    sta muSplitT
    lda muCountHi
    sbc muSplitN+1
    sta muSplitT+1
    lda muSplitT+1
    bne muSlRts                 // 256 or more: a run of its own
    lda muSplitT
    cmp #2
    bcs muSlRts
muSlAll:
    lda muCountLo               // too short to write: the note takes it
    sta muSplitN
    lda muCountHi
    sta muSplitN+1
    lda #0
    sta muSplitT
    sta muSplitT+1
muSlRts:
    clc                         // the compares above leave C set
    rts

// --- muSplitPlan -- the runs a split writes, and how many bytes they need ---
//  In:  MU_SEKDUR/OPT (the pair under the cursor), MU_SEKROW (its first row),
//       MU_TGT (the cursor's row), MU_DURJIF (the armed length)
//  Out: C=0 muInsN = the bytes to open behind the pair, and the runs themselves
//       in muSplitH/N/T; C=1 a run is not expressible at this tempo, or the
//       three take more events than the byte count can hold.
muSplitPlan:
    jsr muSplitLens
    bcs muPlRts
    lda #0
    sta muLenMode               // counting pass: nothing is written yet
    sta muN1                    // the events the three runs take
    jsr muLenHead
    ldy #0
    jsr muPlRun                 // the rows before the cursor
    bcs muPlRts
    lda muNewOpt
    sta muLenOpt
    ldy #2
    jsr muPlRun                 // the note
    bcs muPlRts
    lda muSekOpt
    sta muLenOpt
    ldy #4
    jsr muPlRun                 // ...and what the event leaves after it
    bcs muPlRts
    lda muN1                    // k events replace the pair that is there
    bne muPlHave
    sec                         // nothing to write at all: not a split
    rts
muPlHave:
    sec
    sbc #1
    asl
    sta muInsN
    clc
muPlRts:
    rts

// --- muPlRun -- count one run of the split into MU_N1's running total ---
//  In: Y = the run's word in muSplitH/N/T (0, 2 or 4); muLenOpt set.
//  Out: C=0 counted; C=1 the tempo cannot express it, or the split is too big.
muPlRun:
    lda muSplitH,y
    sta muLenJ
    lda muSplitH+1,y
    sta muLenJ+1
    jsr muLenEmit
    bcs muPlRunRts
    clc
    adc muN1
    bcs muPlBad
    cmp #MU_SPLIT_MAX
    bcs muPlBad
    sta muN1
    clc
muPlRunRts:
    rts
muPlBad:
    sec
    rts

// --- muNcSplit -- a note starts at the cursor inside a longer event ---
//  The event is replaced by three runs: the rows before the cursor on the old
//  pitch, the new note at the armed length, and what the event had left after it
//  on the old pitch again. The three together last exactly as long as the event
//  did, so nothing below moves. A run that is not one legal length becomes several
//  tied events.
muNcSplit:
    jsr muSeekKPtr
    ldy #0
    lda (kPtr),y
    sta muSekDur                // the pair being replaced, read before it moves
    iny
    lda (kPtr),y
    sta muSekOpt
    jsr muRecAtSek  // the undo record: 2 bytes at the onset become...
    lda #2
    sta muRecOldN
    jsr muCapOld
    jsr muSplitPlan
    bcc muNsGo
    jmp muNcIllegal
muNsGo:
    jsr muInsSek2
    lda muSkV
    jsr muInsGap
    bcc muNsWrite
    jmp muStatus                // muInsGap left the reason in MU_STR
muNsWrite:
    jsr muSeekKPtr
    lda #1
    sta muLenMode
    sta muLenTie                // a same-pitch run is tied, not re-struck
    lda muDurTie                // the armed tie is the user's, so it comes back
    pha                         //   once the three runs are written
    lda muSekDur                // the head is the event's own start: it keeps the
    and #$40                    //   tie bit that start already carried
    sta muDurTie
    lda muSekOpt
    sta muLenOpt
    ldy #0
    jsr muNsRun
    pla
    sta muDurTie
    lda muNewOpt
    sta muLenOpt
    ldy #2
    jsr muNsRun                 // the note, at the armed length
    lda muSplitT
    ora muSplitT+1
    beq muNsRec                 // no tail: the note took the rest of the event
    lda muDurTie
    pha
    lda #0
    sta muDurTie                // the tail is another pitch: never tied to the note
    lda muSekOpt
    sta muLenOpt
    ldy #4
    jsr muNsRun
    pla
    sta muDurTie
muNsRec:
    lda muInsN                  // ...2 + the opened bytes, pushed as one action
    clc
    adc #2
    sta muRecNewN
    jsr muPushEdit
    //  --- the rows from the event's first row to its end changed: the head is
    //      the event's own first pair, so its line carries a new length ---
    lda muSekRow
    ldx muSekRow+1
    jsr muBandRow               // the first line the event draws on
    sta muGdFrom
    lda muSekEnd                // the rows the event covers, added to that line
    sec
    sbc muSekRow
    clc
    adc muGdFrom
    cmp #MU_GRID_BOT+1          // the event may run past the band's last row: the
    bcc muNsBand                //   repaint is bounded by the band, never by the
    lda #MU_GRID_BOT+1          //   tune's row count
muNsBand:
    sta muGdTo                  // ...and ends where the old event did
    tax                         // X = one past the last row
    lda muGdFrom                // A = the first
    jsr muRedrawRows
    lda muTgtLo                 // the cursor stays on the row it split
    ldx muTgtHi
    jmp muCurSettleNext

// --- muNsRun -- write one run of a split at the seek's pair ---
//  In: Y = the run's word in muSplitH/N/T (0, 2 or 4); muLenOpt, muLenTie and
//      muDurTie set. MU_SEEKLO/HI advances past the pairs written.
muNsRun:
    lda muSplitH,y
    sta muLenJ
    lda muSplitH+1,y
    sta muLenJ+1
    jmp muLenEmit

// --- muFillPlan -- the gap a fill has to cross, and how many events it takes ---
//  In:  MU_SEKEND (the row after the recorded event), MU_TGT, MU_DURJIF
//  Out: C=0 muGapJ = the gap in jiffies, muN1 = the rest events, muN2 = the
//       note's; C=1 the tempo cannot express one of them.
muFillPlan:
    lda muTgtLo
    sec
    sbc muSekEnd
    sta muCountLo
    lda muTgtHi
    sbc muSekEnd+1
    sta muCountHi
    lda muCountLo
    ldx muCountHi
    jsr muRowsToJif
    bcs muFpRts
    sta muGapJ
    lda #0
    sta muLenOpt
    sta muLenTie
    sta muLenMode               // counting pass
    lda muGapJ
    jsr muLenOne                  // the rests
    bcs muFpRts
    sta muN1
    jsr muLenNote
    bcs muFpRts
    sta muN2
    clc
    adc muN1
    cmp #MU_LEN_MAX
    bcs muFpRts
    clc
muFpRts:
    rts

// --- muFillNote -- the row is a gap: rests to reach it, then the note ---
//  The rests go in right after the event that ran past the row - before the HLT
//  when that event was the voice's last - so the note lands on the cursor's row
//  and every later event keeps its timing.
// --- muGapOpen -- open the bytes a note needs and record them for undo ---
//  In: muN1 + muN2 = the pairs the write needs, muSkV = the voice, muGapJ its
//  silence. MU_INSN is their byte count; the undo record replaces 0 bytes with that
//  many, behind the event that runs past the cursor row. Out: C=1 when muInsGap
//  refused (MU_STR holds the reason).
muGapOpen:
    lda muN1
    clc
    adc muN2
    asl                         // two bytes a pair
    sta muInsN
    jsr muInsSek2
    jsr muRecAtIns
    lda #0
    sta muRecOldN
    lda muSkV
    jmp muInsGap

muFillNote:
    jsr muFillPlan
    bcc muFgGo
    jmp muNcIllegal
muFgGo:
    jsr muGapOpen
    bcc muFgW
    jmp muStatus                // muInsGap left the reason in MU_STR
muFgW:
    jsr muSeekKPtr
    jsr muSekAdv2
muFgRest:
    jsr muLenTwo
    lda muInsN                  // the opened bytes, pushed as one action
    sta muRecNewN
    jsr muPushEdit
    lda muSekEnd                // the fill's own first row: the rests start where the
    ldx muSekEnd+1              //   event it found ended, which is often above the
    jsr muRedrawBand            //   cursor, and its line carries the first rest
    lda muTgtLo                 // the cursor stays on the row the note went to
    ldx muTgtHi
    jmp muCurSettleNext

// ==========================================
//  Inserting time -- open rows of silence in all three voices at once
// ==========================================
//  The cursor's row is a shared boundary T0; MU_INSROWS rows of silence open
//  there in every voice, so the three lanes stay in step. Each voice is seeked
//  and spliced in turn -- a splice shifts the later voices' offsets, so the next
//  voice is re-seeked on the grown image -- and the splices are one grouped undo
//  action. A voice that has stopped before T0 contributes nothing.
//  Insert MODE shares this machinery for a note, and splices the cursor's voice
//  alone: a note is that voice's own edit, and the other two lanes keep exactly the
//  events they had.

// --- muInsertTime -- A = the rows of silence to open at the cursor row ---
//  The I / SHIFT+I entry: every voice gets rests (muInsNoteV = $ff). Insert mode
//  enters muInsLoop with muInsNoteV = the cursor voice and muInsJif = the note.
muInsertTime:
    sta muInsRows
    jsr muCursorAbs
    sta muInsT0
    stx muInsT0+1
    lda muInsRows               // the silence, in jiffies = rows x the row unit
    ldx #0
    jsr muRowsToJif
    bcs muItIllegal
    sta muInsJif
    lda #$ff
    sta muInsNoteV
    jsr muPhBlock               // a body's timing is every call site's timing
    bcs muItPhrase
    jsr muInsLoop
    lda muInsT0                 // the boundary every voice was opened at: nothing above
    ldx muInsT0+1               //   it changes, so its group is where the band starts
    jsr muRedrawBand
    lda muInsT0                 // ...and the cursor stays on the opened row
    ldx muInsT0+1
    jmp muCurSettle
muItIllegal:
    lda #M_ILLEGAL
    jmp muMsgGo
muItPhrase:
    jmp muPhRefuse              // out of branch range: the refusal prints the phrase

// --- muInsLoop -- the splices, as one grouped action; the caller repaints ---
//  MU_INSNOTEV names the voice that gets the note: $ff is insert time, where every
//  voice gets rests and none gets a note, and a real voice is insert mode, where the
//  other lanes are not touched at all.
muInsLoop:
    lda #1
    sta muInsFirst              // the first splice opens the grouped action
    lda #0
    sta muItV
muItVLoop:
    lda muItV
    cmp muInsNoteV
    beq muItVNote               // the note voice: the note, or a rest in insert time
    lda muInsNoteV              // the others: insert time rests every voice, while a
    cmp #$ff                    //   note insert is the cursor voice's edit alone
    bne muItVNext
    lda #0
    sta muInsOpt
    sta muInsTie
    jmp muItVDo
muItVNote:
    lda muNewOpt
    sta muInsOpt
    lda #1
    sta muInsTie
muItVDo:
    jsr muInsOneVoice
muItVNext:
    inc muItV
    lda muItV
    cmp #3
    bcc muItVLoop
    rts

// --- muNoteInsMode -- insert mode: the note goes AFTER the event the cursor is on ---
//  Only the cursor's voice is spliced -- a note is that lane's own edit, so the other
//  two keep exactly the events they had. The boundary is the END of the event the
//  cursor's line holds, not the line itself, and the cursor then follows the note: a
//  run of entries in one place writes the tune forward instead of stacking every note
//  in front of the last. A gap, a row past the last event and a row inside a longer
//  event have nothing below them to reorder, so those are entered on the cursor's row.
muNoteInsMode:
    jsr muCursorAbs
    sta muInsT0
    stx muInsT0+1
    lda muSekKind
    bne muNimSet
    lda muSekEnd                // the event's onset plus its rows: the next boundary
    sta muInsT0
    lda muSekEnd+1
    sta muInsT0+1
muNimSet:
    lda muDurJif
    sta muInsJif
    ldy #MU_VOICE
    lda (dataPtr),y
    sta muInsNoteV
    jsr muInsLoop
    lda muInsFrom               // the first row the splice touched, which for a fill is
    ldx muInsFrom+1             //   where the event before the gap ended
    jsr muRedrawBand
    lda muInsT0                 // then put the cursor on the row the note starts on
    ldx muInsT0+1
    jsr muCurSettle
    jmp muInsGoStep

// --- muInsGoStep -- leave the cursor on the row the new note starts on ---
//  A LINE at a time, because one row can hold command lines of its own, and the
//  cursor goes on the row's OWN line -- that is where a note is, and where the next
//  edit can act on it. The grid is redrawn first, so the walk's line table and the
//  band agree with the image the boundary was measured in and a step taken at the
//  bottom scrolls the window. A line past the tune's end holds $ffff, which is past
//  every boundary and names no own line, so the walk cannot run away.
muInsGoStep:
    jsr muCursorAbs             // A/X = the cursor's row, also left in muTmpL/H
    cmp muInsT0
    lda muTmpH
    sbc muInsT0+1
    bcc muIgsDown
    lda muTmpH
    cmp #$ff
    beq muIgsRts                // past the tune's end: there is no own line to reach
    jsr muCursorIdx
    tay
    lda muLnKind,y              // the row is reached, but this may be one of its
    beq muIgsDown               //   command lines: the note is on the row's own
muIgsRts:
    rts
muIgsDown:
    jsr muCurDownGo
    jmp muInsGoStep

// --- muInsPush -- push the splice just written, first as a group start ---
muInsPush:
    lda #1
    sta muRecCont
    lda muInsFirst
    beq muIpGo
    lda #0                      // the first splice starts the action
    sta muRecCont
    sta muInsFirst
muIpGo:
    jmp muPushEdit

// --- muInsOneVoice -- splice MU_INSJIF jiffies of silence into MU_ITV at T0 ---
muInsOneVoice:
    lda muItV
    sta muSkV
    lda muInsT0
    sta muTgtLo
    lda muInsT0+1
    sta muTgtHi
    jsr muSeekAt
    lda muSekRow                // the first row this voice's splice can touch: the
    sta muInsFrom               //   event's own row, or (in a gap) where the event
    lda muSekRow+1              //   before it ended -- see muIvAppend
    sta muInsFrom+1
    lda muSekKind
    bne muIvNot0
    jmp muIvOnset
muIvNot0:
    cmp #1
    bne muIvNot1
    jmp muIvStraddle
muIvNot1:
    lda muItV                   // kind 2: a rest voice has nothing to push...
    cmp muInsNoteV
    beq muIvAppend              // ...but the note voice appends the note here
    rts

// --- muIvAppend -- INS mode in an empty/past-end voice: fill, then the note ---
muIvAppend:
    lda muSekEnd                // the rests start where the event before the gap
    sta muInsFrom               //   ended, which is above the cursor
    lda muSekEnd+1
    sta muInsFrom+1
    lda muSekTem
    sta muJifTem
    jsr muFillPlan              // muGapJ rests, then muN2 note events
    bcc muIvAppFit
    rts
muIvAppFit:
    jsr muGapOpen
    bcs muIvAppRts
    jsr muSeekKPtr
    jsr muSekAdv2
muIvApR:
    jsr muLenTwo
    lda muInsN
    sta muRecNewN
    jsr muInsPush
muIvAppRts:
    rts

// --- muIvOnset -- an event starts exactly at T0: rests go in before it ---
muIvOnset:
    lda muSekTem                // the tempo in force, for the rest durations
    sta muJifTem
    lda #0
    sta muLenOpt
    sta muLenTie
    sta muLenMode               // count the rest events
    lda muInsJif
    jsr muLenOne                  // the silence
    bcs muIvRts                 // this tempo cannot express the silence: skip
    asl
    sta muInsN                  // the rest events, in bytes
    jsr muRecAtSek
    lda #0
    sta muRecOldN               // nothing is replaced
    lda muSekLo
    sta muInsLo
    lda muSekHi
    sta muInsHi
    lda muSkV
    jsr muInsGap
    bcs muIvRts                 // no room: skip this voice
    jsr muSeekKPtr              // kPtr = the gap just opened
    lda #1
    sta muLenMode
    lda muInsTie                // rests, or the note in insert mode
    sta muLenTie
    lda muInsOpt
    sta muLenOpt
    lda muInsJif
    jsr muLenOne                  // the remainder, a fresh attack
    lda muInsN
    sta muRecNewN
    jsr muInsPush
muIvRts:
    rts

// --- muIvStraddle -- T0 is inside an event: front, then rests, then remainder ---
//  The event is replaced by three runs: the part up to T0 on its own pitch, the
//  inserted silence, and the part from T0 on (its remainder) with a fresh attack.
//  The three together last exactly the event's length plus the inserted silence.
muIvStraddle:
    jsr muSeekKPtr
    ldy #0
    lda (kPtr),y
    sta muSekDur                // the straddling pair, read before it moves
    iny
    lda (kPtr),y
    sta muSekOpt
    lda muSekTem
    sta muJifTem
    //  --- the event's length and the front J1 (rows before T0) ---
    lda muSekDur
    jsr muNoteJiffies           // the walk already played it: always a length
    sta muLenJ2                 // eventJ
    lda #0
    sta muLenJ2+1
    lda muTgtLo                 // J1 rows = T0 - the event's onset row
    sec
    sbc muSekRow
    ldx #0
    jsr muRowsToJif
    bcs muIvsBail
    sta muItJ1                  // J1 jiffies
    //  --- count: front(J1) + rests(insJif) + remainder(eventJ - J1) ---
    lda #1
    sta muLenTie
    lda #0
    sta muLenMode
    lda muSekOpt
    sta muLenOpt
    lda muItJ1
    jsr muLenOne
    bcs muIvsBail
    sta muItN1
    lda #0
    sta muLenTie
    sta muLenOpt
    lda muInsJif
    jsr muLenOne
    bcs muIvsBail
    sta muItNr
    lda muLenJ2                 // J2 = eventJ - J1
    sec
    sbc muItJ1
    sta muItJ2
    jsr muLenHead
    lda muItJ2
    jsr muLenOne
    bcs muIvsBail
    sta muItN2
    jmp muIvsWrite
muIvsBail:
    rts
muIvsWrite:
    //  --- open the extra bytes and write the three runs at the event ---
    clc
    adc muItN1
    adc muItNr                  // total events
    asl                         // ...in bytes
    sec
    sbc #2                      // the event already holds one pair
    sta muInsN
    jsr muInsSek2
    jsr muRecAtSek
    lda #2
    sta muRecOldN
    jsr muCapOld                // the old pair, for undo
    lda muSkV
    jsr muInsGap
    bcs muIvsBail
    jsr muSeekKPtr
    lda #1
    sta muLenMode
    jsr muLenHead
    lda muItJ1
    jsr muLenOne
    lda muInsTie                // the middle: silence, or the note in insert mode
    sta muLenTie
    lda muInsOpt
    sta muLenOpt
    lda muInsJif
    jsr muLenOne
    jsr muLenHead
    lda muItJ2
    jsr muLenOne
    lda muItN1                  // the new byte count = all three runs x 2
    clc
    adc muItNr
    adc muItN2
    asl
    sta muRecNewN
    jsr muInsPush
    rts


//  Only an onset answers. The duration byte stays, so the length -- and the dots
//  and the triplet with it -- is unchanged and nothing after the event moves;
//  the tie bit is cleared (a rest is silence, not a held note) and the option
//  byte becomes 0, the pair the player reads as a rest.
muRestKey:
    jsr muSeek
    lda muSekKind
    beq muRkGo
    lda #M_NO_EVENT
    jmp muMsgGo
muRkGo:
    jsr muRecAtSek  // the undo record: 2 bytes at the onset
    lda #2
    sta muRecOldN
    sta muRecNewN
    jsr muCapOld
    jsr muSeekKPtr
    ldy #0
    lda (kPtr),y
    and #$bf                    // bit 6 is the tie
    sta (kPtr),y
    iny
    lda #0
    sta (kPtr),y
    jsr muWinFlush0             // the pair reaches its page before the record reads it
    jsr muPushEdit
    lda muTgtLo                 // only the rested row changed, and it is the cursor's
    ldx muTgtHi
    jsr muRedrawBand
    lda muTgtLo
    ldx muTgtHi
    jmp muCurSettleNext

// --- muDelPair -- splice the 2-byte event at MU_SEKLO/HI out of MU_SKV ---
//  One undo record, so a caller that splices several voices decides the grouping
//  itself through muRecCont. Clobbers A, X, Y, kPtr.
muDelPair:
    jsr muRecAtSek  // the undo record: 2 bytes at the onset go away
    lda #2
    sta muRecOldN
    lda #0
    sta muRecNewN
    jsr muCapOld
    lda #2
    sta muInsN
    lda muSekLo
    sta muInsLo
    lda muSekHi
    sta muInsHi
    lda muSkV
    jsr muDelGap
    jmp muPushEdit

// --- muDelKey -- remove the event at the cursor and close the gap ---
//  Only an onset answers, and the HLT pair is never a timed event, so the
//  terminator cannot be deleted and the voice keeps its last pair.
muDelKey:
    jsr muSeek
    lda muSekKind
    beq muDkGo
    lda #M_NO_EVENT
    jmp muMsgGo
muDkGo:
    jsr muPhHere                // a body's timing is every call site's timing
    bcs muDkPhrase
    jsr muDelPair
    lda muSekRow                // the row the event was on: its line, and the HLT line
    ldx muSekRow+1              //   above it when this was the voice's first event
    jsr muRedrawBand
    lda muTgtLo                 // the cursor stays on the row it deleted from
    ldx muTgtHi
    jmp muCurSettle
muDkPhrase:
    jmp muPhRefuse

// --- muDelRows -- delete the A rows at the cursor row from every voice ---
//  A row of music is what all three voices play on it, so a row delete takes those
//  rows' events out of every voice. The voices need not tile the rows the same way
//  -- a whole-bar rest in one lane over sixteen 16ths in another is the same bar of
//  music -- so each voice is walked on its own, consuming an event by its own length
//  until the window is empty.
//  Every voice is walked once BEFORE any of them is spliced, and an event that runs
//  past either end of the window is refused with that voice named: a row boundary
//  inside a note is not a row to remove. A refusal leaves the image untouched, and
//  however many splices the delete takes, it is one undo action.
//  In: A = rows to delete, 1 or more. Clobbers A, X, Y, kPtr.
muDelRows:
    sta muDrRows
    jsr muPhBlock               // a body's timing is every call site's timing
    bcc muDrOpen
    jmp muDrPhrase              // out of branch range: the refusal prints the phrase
muDrOpen:
    jsr muCursorAbs             // the window starts on the cursor's row
    sta muDrT0
    stx muDrT0+1
    lda #0
    sta muDrPass
    sta muRecCont               // the first splice opens the undo action
    jmp muDrPassGo              // (the refusal below is reached by branch only)
muDrSpan:
    lda muSkV                   // the message names the voice that blocks the window
    clc
    adc #M_SPAN1
    jmp muMsgGo
muDrPassGo:
    lda #0
    sta muSkV
muDrVoice:
    lda muDrT0                  // every voice is walked from the top of the window
    sta muTgtLo
    lda muDrT0+1
    sta muTgtHi
    clc                         // muDrEnd = the row the window ends on
    lda muTgtLo
    adc muDrRows
    sta muDrEnd
    lda muTgtHi
    adc #0
    sta muDrEnd+1
muDrEvent:
    jsr muSeekAt
    lda muSekKind
    cmp #2
    beq muDrVoiceNext           // the voice ends inside the window: nothing more
    cmp #0
    bne muDrSpan                // an event that starts above the window...
    lda muDrEnd                 // ...or one that runs past the bottom of it
    sec
    sbc muSekEnd
    lda muDrEnd+1
    sbc muSekEnd+1
    bcc muDrSpan
    lda muDrPass
    bne muDrTake
    lda muSekEnd                // checking: step to the event that follows this one
    sta muTgtLo
    lda muSekEnd+1
    sta muTgtHi
    jmp muDrMore
muDrTake:
    jsr muDelPair               // the pair goes and the voice's tail closes up
    lda #1                      // the next splice appends to the action in flight
    sta muRecCont
    lda muDrEnd                 // the window closes by the rows that event held
    sec
    sbc muSekSpan
    sta muDrEnd
    lda muDrEnd+1
    sbc muSekSpan+1
    sta muDrEnd+1
muDrMore:
    lda muTgtLo                 // rows are left while the walk is above the end
    cmp muDrEnd
    lda muTgtHi
    sbc muDrEnd+1
    bcc muDrEvent
muDrVoiceNext:
    inc muSkV
    lda muSkV
    cmp #3
    bcc muDrVoice
    lda muDrPass
    bne muDrDone
    lda #1
    sta muDrPass                // every voice fitted: walk them again, splicing
    jmp muDrPassGo
muDrDone:
    lda muRecCont               // nothing was spliced: the rows held no event
    beq muDrNone
    lda #0
    sta muRecCont               // the action is closed: the next edit starts one
    lda muDrT0
    ldx muDrT0+1
    jsr muRedrawBand
    lda muDrT0                  // the cursor stays on the rows it emptied
    ldx muDrT0+1
    jmp muCurSettle
muDrNone:
    lda #M_NO_EVENT
    jmp muMsgGo
muDrPhrase:
    jmp muPhRefuse

// --- muDelKey2 -- SHIFT+INST/DEL: the command on this line, else the row's event ---
muDelKey2:
    jsr muCmdHere
    bcc mdkCmd                  // a command on this line: that is what goes
    jsr muCursorIdx
    tay
    cpy #MU_GRID_ROWS
    bcs mdkEvent
    lda muLnKind,y
    bne mdkEvent                // the row's own line: the event's own delete
    jsr muBandRebuild           // a command line another lane owns: nothing here
    lda #M_NO_EVENT
    jmp muMsgGo
mdkEvent:
    jmp muDelKey
mdkKeep:
    lda #M_KEEP_HLT
    jmp muMsgGo
mdkCmd:
    cmp #MU_MN_HLT              // the voice's last pair is its terminator: remove it
    beq mdkKeep                 //   and the walk has no end, so the voice cannot be
    sta muObjId                 //   read again. The id names the pair: a removal is
    lda muCmdVal                //   the nearest one of that id at this boundary, so a
    sta muObjVal                //   value wider than a byte cannot hide it
    lda #0
    sta muObjMatch
    jsr muBandRebuild
    jsr muSeek
    lda muSekKind
    bne mdkGone                 // the row's event started above: no boundary
    jsr muObjFind
    bcs mdkGone                 // not in the run: nothing to remove
    jsr muRecAtObj              // the undo record: two bytes at MU_OBJLO/HI
    lda #2
    sta muRecOldN
    lda #0
    sta muRecNewN
    jsr muCapOld
    lda #2
    sta muInsN
    lda muObjLo
    sta muInsLo
    lda muObjLo+1
    sta muInsHi
    lda muSkV
    jsr muDelGap
    jsr muPushEdit
    lda muCmdRow                // the line came out of the group, so everything from
    ldx muCmdRow+1              //   the command's own row down has moved up
    jsr muRedrawBand
    lda muCmdRow                // the cursor stays on the row the command was on
    ldx muCmdRow+1
    jmp muCurSettle
mdkGone:
    jsr muBandRebuild
    lda #M_NO_EVENT
    jmp muMsgGo

// ==========================================
//  Command lines -- create, replace and remove
// ==========================================
//  A command pair takes no time: the player applies it when it reaches it, which
//  is the onset of the next note or rest in that voice. So a command belongs to the
//  boundary in FRONT of an event, its line sits above that row, and there is no
//  boundary inside a duration to put one at -- those rows are refused rather than
//  split, because the format has no way to split a note.
//  Writing a pair moves bytes and never time: every row stays where it is, so the
//  undo record is the pair alone and no row needs renumbering.
//  The nine structural commands are the authoring surface: their operands are the
//  tune's own settings, where a sound command's operand is a timing or a waveform
//  value that belongs to the pattern rather than to a line somebody types.

// --- muObjKey -- SHIFT+C, and the bar's CMD cell ---
muObjKey:
    jsr muPlaying
    bcs mqRts                   // "? stop first" is already up
    jsr muCmdHere               // a command on the cursor's line?
    bcc mqHave                  // yes: A = its mnemonic id
    jsr muCursorIdx             // no: only a row's OWN line can take a new one
    tay
    cpy #MU_GRID_ROWS
    bcs mqRefuse
    lda muLnKind,y
    bne mqRefuse                // no: only a row's OWN line can take a new one
    jsr muBandRebuild           // a command line this lane has nothing on, and
    lda #M_NO_CMD               //   muCmdHere walked the producer to find out
    jmp muMsgGo
    jsr muSeek
    lda muSekKind
    bne mqRefuse                // the event started above: no boundary here
    lda #MU_MN_TEM
    sta muObjId
    lda #0
    sta muObjVal
    sta muObjValHi
    sta muObjKind               // 0 = a pair to be added at the boundary
    jmp mqPick
mqHave:
    sta muObjId                 // every mnemonic the picker offers has an encoder,
                                //   so a structural id and a sound one take the same path
    lda muCmdVal
    sta muObjVal
    lda muCmdValHi
    sta muObjValHi
    lda #1                      // 1 = the pair exists: its two bytes are replaced
    sta muObjKind
    sta muObjMatch
    jsr muBandRebuild           // the walk left the producer parked
    jsr muSeek
    lda muSekKind
    bne mqGone                  // the row's event started above: no boundary
    jsr muObjFind               // its offset, by mnemonic and operand
    bcc mqPick
mqGone:
    jmp mqRefuse
mqRefuse:
    jsr muBandRebuild
    lda #M_NO_EVENT
    jmp muMsgGo
mqRts:
    rts

// --- mqPick -- the row-0 prompt: pick the mnemonic, then type the operand ---
mqPick:
    lda #0
    sta muObjPhase
    sta muObjFresh
mqDraw:
    jsr muObjEnc                // the operand's maximum, and the encoded pair
    jsr mqClamp                 // a mnemonic picked after a value was typed keeps it
    jsr muClearRow0
    lda #M_CMD
    jsr muMsg
    lda #1                      // the mnemonic reads as a bar, as it does in the grid
    sta muRevText
    lda muObjId
    jsr muMnPrint               // leaves MU_REVTEXT at 0
    lda muObjId                 // TAL, HLT and END take no operand
    cmp #MU_MN_TAL
    beq mqDrawEnd
    cmp #MU_MN_HLT
    beq mqDrawEnd
    cmp #MU_MN_END
    beq mqDrawEnd
    lda #MU_SPACE
    jsr muPutC
    lda muObjVal
    ldx muObjValHi
    jsr muNum16
mqDrawEnd:
    lda muObjPhase
    bne mqValKey
    jmp mqIdKey

// --- the mnemonic step: CRSR up and down walk the nine, RETURN takes one ---
mqIdKey:
    jsr kReadKey
    beq mqIdWait
    cmp #$84                    // CRSR down: the next mnemonic
    beq mqIdDown
    cmp #$85                    // CRSR up: the previous one
    beq mqIdUp
    cmp #$81                    // RETURN: take it
    beq mqIdTake
    cmp #$83                    // STOP: cancel
    bne mqIdKey
    jmp mqCancel
mqIdWait:
    lda #SYS_YIELD
    jsr syscall
    jmp mqIdKey
mqIdDown:
    lda muObjId
    cmp #MU_MN_LAST
    bcc mqIdInc
    lda #MU_MN_TEM-1            // past the last: wrap to the first
mqIdInc:
    clc
    adc #1
    sta muObjId
    jmp mqDraw
mqIdUp:
    lda muObjId
    cmp #MU_MN_TEM
    bne mqIdDec
    lda #MU_MN_LAST+1           // before the first: wrap to the last
mqIdDec:
    sec
    sbc #1
    sta muObjId
    jmp mqDraw
mqIdTake:
    lda muObjId
    cmp #MU_MN_TAL
    bne mqIt1
    jmp mqTake
mqIt1:
    cmp #MU_MN_HLT
    bne mqIt2
    jmp mqTake
mqIt2:
    cmp #MU_MN_END
    bne mqIt3
    jmp mqTake
mqIt3:
    lda #1
    sta muObjPhase
    lda #0
    sta muObjFresh
    jmp mqDraw

// --- the operand step: digits build the value, INST/DEL backs one out ---
mqValKey:
    jsr kReadKey
    bne mqValGot
    jmp mqValWait
mqValGot:
    cmp #$81                    // RETURN: write the pair
    beq mqValTake
    cmp #$83                    // STOP: cancel
    bne mqValDigit
    jmp mqCancel
mqValTake:
    jmp mqTake
mqValDigit:
    cmp #$80                    // INST/DEL: back one digit
    beq mqValBack
    sec
    sbc #'0'
    bcc mqValKey                // not a digit
    cmp #10
    bcs mqValKey
    sta muTmpL
    lda muObjFresh              // the first digit typed starts the value afresh, so
    bne mqValAcc                //   an operand already there is not extended
    lda #1
    sta muObjFresh
    lda #0
    sta muObjVal
    sta muObjValHi
mqValAcc:
    lda muTmpL                  // the digit
    jsr muValAcc
    jsr mqClamp                 // never past the maximum
    jmp mqDraw

// --- mqClamp -- MU_OBJVAL := min(MU_OBJVAL, MU_OBJMAX), both 16-bit ---
mqClamp:
    lda muObjValHi
    cmp muObjMaxHi
    bcc mqClRts                 // below the maximum
    bne mqClSet                 // above it
    lda muObjVal
    cmp muObjMax
    bcc mqClRts
mqClSet:
    lda muObjMax
    sta muObjVal
    lda muObjMaxHi
    sta muObjValHi
mqClRts:
    rts
mqValBack:
    lda muObjVal
    ora muObjValHi
    bne mqVbGo                  // a value: drop its last digit
    lda #0                      // nothing left to delete: back to the mnemonic
    sta muObjPhase
    jmp mqDraw
mqVbGo:
    sta muObjFresh              // a digit typed now extends what is left
    jsr muValDiv10
    jmp mqDraw
mqValWait:
    lda #SYS_YIELD
    jsr syscall
    jmp mqValKey

// --- muValAcc -- MU_OBJVAL := MU_OBJVAL * 10 + A, the digit typed ---
//  Both numeric prompts type their number this way. kMul16 does the arithmetic in 16
//  bits, so a value above 255 is exact and one byte of entry can carry two digits.
muValAcc:
    sta muTmpL
    lda muObjVal
    sta kArg
    lda muObjValHi
    sta kArg+1
    lda #10
    sta kArg+2
    lda #0
    sta kArg+3
    jsr kMul16
    lda kArg+4                  // the product's low two bytes
    clc
    adc muTmpL
    sta muObjVal
    lda kArg+5
    adc #0
    sta muObjValHi
    rts

// --- muValDiv10 -- MU_OBJVAL := MU_OBJVAL / 10, its last digit dropped ---
muValDiv10:
    lda muObjVal
    sta kArg
    lda muObjValHi
    sta kArg+1
    lda #10
    sta kArg+2
    lda #0
    sta kArg+3
    jsr kDiv16
    lda kArg+4
    sta muObjVal
    lda kArg+5
    sta muObjValHi
    rts

// --- mqTake -- encode the pair and write it ---
mqTake:
    lda muErr                   // was the tune walkable before this write? MU_OBJFRESH
    sta muObjFresh              //   holds the answer until mqDone reads it
    jsr muObjEnc
    lda muObjKind
    bne mqPut                   // the pair exists: replace it in place
    lda muInsMode
    bne mqIns                   // insert mode: a second pair, same command or not
    lda #0
    sta muObjMatch              // overwrite mode: the same command at this boundary?
    jsr muObjFind
    bcs mqIns
mqPut:
    jsr muRecAtObj              // the undo record: two bytes at MU_OBJLO/HI
    lda #2
    sta muRecOldN
    sta muRecNewN
    jsr muCapOld
    jsr mqPutPair
    jmp mqDone
mqIns:
    lda muSekLo                 // the new pair goes where the event's own pair is:
    sta muObjLo                 //   the gap opens there and the event moves up
    lda muSekHi
    sta muObjLo+1
    lda #0
    sta muRecOldN
    lda #2
    sta muInsN
    lda muObjLo
    sta muInsLo
    lda muObjLo+1
    sta muInsHi
    jsr muRecAtIns              // after the offset: the record copies MU_INSLO/HI
    lda muSkV
    jsr muInsGap
    bcs mqStatus                // muInsGap left the reason in MU_STR
    lda #2
    sta muRecNewN
    jsr mqPutPair
mqDone:
    jsr muPushEdit
    jsr muRedrawNoHead          // the repaint walks the tune, and that walk is the first
    lda muErr                   //   to see a pair the player cannot play
    beq mqFine
    ldx muObjFresh              // a tune that was already unwalkable is not this pair's
    bne mqFine                  //   doing: leave it alone, so its marks stay editable
    cmp #MU_ERR_LEN             // a length this tempo cannot express is its own answer:
    beq mqBadTempo              //   the tempo is what the user just changed
    lda #M_UNDONE
    bne mqUndoGo
mqBadTempo:
    lda #M_BAD_TEMPO
mqUndoGo:
    pha                         // muUndo repaints, and that walk clears MU_ERR, so the
    jsr muUndo                  //   message is chosen first: the edit is taken back here,
    lda muUndoTop               //   nothing is left for the user to undo, and the grid
    sta muUndoLim               //   comes back as it was
    lda muUndoTop+1             // the redo limit follows the top down, because a redo
    sta muUndoLim+1             //   would only write the refused pair again
    pla
    jmp muMsgGo
mqFine:
    jmp muStatArm               // the prompt stands a moment, then row 0 is the head's
mqStatus:
    jmp muStatus
mqCancel:
    jmp muDrawHead              // the prompt only touched row 0

// --- mqPutPair -- the encoded pair into the image at MU_OBJLO/HI ---
mqPutPair:
    lda muObjLo
    sta muWinOfs
    lda muObjLo+1
    jsr muWinSeat0
    ldy #0
    lda muObjCmdB
    sta (kPtr),y
    iny
    lda muObjOptB
    sta (kPtr),y
    jmp muWinFlush0             // the pair reaches the page it lives on

// --- muRecAtObj -- the undo record points at the pair in MU_OBJLO/HI ---
//  In: MU_OBJLO/HI, MU_SKV. Clobbers A.
muRecAtObj:
    lda muObjLo
    sta muRecOff
    lda muObjLo+1
    sta muRecOff+1
    lda muSkV
    sta muRecV
    rts

// --- muObjEnc -- MU_OBJID and MU_OBJVAL -> the command pair and the operand's
//  maximum. Ids 1..9 are the structural nine, so the tables are indexed by id - 1.
//  The phrase commands take 0-15 in the option's high nibble; 16-23 are the eight
//  whose high nibble also sets the option's top bit and whose low nibble names the
//  command, which is why they carry their own base.
//  mqMax holds the low half of the maximum, so the high half is cleared here on
//  every pass: only the shapes that can exceed a byte (mqEncWide) set it, and a
//  stale one would make mqClamp skip the clamp for every mnemonic picked after it.
muObjEnc:
    lda muObjId
    sec
    sbc #1
    tay
    lda mqCmdB,y
    sta muObjCmdB
    lda mqOptB,y
    sta muObjOptB
    lda mqMax,y
    sta muObjMax
    lda #0
    sta muObjMaxHi
    lda muObjId
    cmp #MU_MN_DEF
    beq mqEncPh
    cmp #MU_MN_CALL
    bne mqEncShape
mqEncPh:
    lda muObjVal
    cmp #16
    bcc mqEncNib
    sec
    sbc #16
    clc
    adc #8
    asl
    asl
    asl
    asl
    ora mqOptB+MQ_WIDE,y       // the phrase low nibble: $03 for DEF, $0B for CALL
    sta muObjOptB
    rts
mqEncNib:
    lda muObjVal                // 0-15: the option's HIGH nibble, base in the low
    asl
    asl
    asl
    asl
    ora muObjOptB
    sta muObjOptB
    rts
mqEncShape:
    lda mqShape,y
    beq mqEncVal                // 0: the whole option byte is the operand
    cmp #5
    bcs mqEncWide               // 5-7: the high byte rides the command byte
    tay                         // the shape, to index the packer's parameters
    lda mqShMask,y
    and muObjVal
    sta muTmp                   // the bits to move, kept across the next read
    lda mqShShift,y
    tax                         // the shift count
    lda muTmp
mqShLp:
    dex
    bmi mqShOr
    asl
    jmp mqShLp
mqShOr:
    ora muObjOptB
    sta muObjOptB
    rts

// --- mqEncWide -- shapes 5-7: the operand's HIGH byte packs into the command byte ---
//  In: A = the shape, Y = id - 1. The low byte is the option. The maximum comes
//  from the shape rather than the table, because only these shapes can exceed a
//  byte: P-W and DTN 4095, POR 16383, MS# 999.
mqEncWide:
    tax                         // the shape (5, 6 or 7)
    lda mqShMaxHi-5,x
    sta muObjMaxHi
    lda mqShMaxLo-5,x
    sta muObjMax
    lda mqShCmdShift-5,x
    sta muTmp2
    lda mqShCmdMask-5,x
    and muObjValHi
mqWwLp:
    dec muTmp2
    bmi mqWwOr
    asl
    jmp mqWwLp
mqWwOr:
    ora mqCmdB,y
    sta muObjCmdB
    lda muObjVal
    sta muObjOptB
    rts
mqEncNoShape:
    rts
mqEncVal:
    lda muObjVal                // the whole option byte is the operand
    ora muObjOptB
    sta muObjOptB
    rts

// --- muObjFind -- the boundary's command pair, by mnemonic (and operand) ---
//  In: MU_SEKLO/HI = the pair of the event that starts at the boundary, MU_OBJID,
//      MU_OBJVAL, MU_OBJMATCH (1 = the operand must match as well).
//  Out: C=0 and MU_OBJLO/HI = that command's pair; C=1 when the run has none.
//  A boundary's command pairs are CONTIGUOUS and sit immediately before the event's
//  own pair, so they are found by stepping back two bytes at a time until a pair
//  that consumes time -- a note or a rest -- ends the run. The walk keeps the
//  candidate in MU_OBJLO/HI itself: the decoder clobbers every temp.
muObjFind:
    lda muSekLo
    sta muObjLo
    lda muSekHi
    sta muObjHi
mqfLp:
    lda muObjLo
    sec
    sbc #2
    sta muObjLo
    bcs mqfGo
    dec muObjHi
mqfGo:
    lda muObjHi                 // the image starts at 8: there is nothing before it
    cmp #>8
    bcc mqfNo
    bne mqfRead
    lda muObjLo
    cmp #8
    bcc mqfNo
mqfRead:
    lda muObjLo
    sta muWinOfs
    lda muObjHi
    jsr muWinSeat0
    ldy #0
    lda (kPtr),y
    sta muScmd
    iny
    lda (kPtr),y
    sta muSopt
    lda muScmd
    and #$03
    beq mqfNo                   // a note or a rest: the run ended before this pair
    jsr muLnDec                 // A = the mnemonic, MU_LNVLO/HI = the operand
    cmp muObjId
    bne mqfLp
    lda muObjMatch
    beq mqfHit
    lda muLnVLo
    cmp muObjVal
    bne mqfLp
    lda muLnVHi                 // the wide operands need both halves compared
    cmp muObjValHi
    bne mqfLp
mqfHit:
    clc
    rts
mqfNo:
    sec
    rts

// --- muMnPrint -- print the mnemonic of command id A ---
//  A field is four bytes: three characters and a space, except CALL, which fills it
//  -- so the print stops at that space or at the fourth cell, and the padding is
//  never drawn. The space between the mnemonic and its operand is the caller's.
//  MU_REVTEXT is left at 0. Clobbers A, X and Y (muPutC).
muMnPrint:
    sec
    sbc #1
    asl
    asl
    sta muTmpL                  // (id - 1) * 4
    lda #0
    sta muTmpH
mqMnLp:
    lda muTmpL
    clc
    adc muTmpH
    tay
    lda muMnTab,y
    cmp #' '
    beq mqMnEnd
    jsr muPutC
    inc muTmpH
    lda muTmpH
    cmp #4
    bcc mqMnLp
mqMnEnd:
    lda #0
    sta muRevText
    rts

//  The encoder's three tables, one entry per structural command in id order: the
//  command byte, the option byte's base and the largest operand it takes. The
//  option table's second nine are the PHRASE bases: a phrase the option's top bit
//  names puts its own low nibble there, where the value 0-15 keeps the first nine.
mqCmdB:     .byte $06, $16, $F6, $36, $01, $01, $01, $01, $01
            .byte $0E, $26, $2E, $4E, $56, $66, $6E, $76, $86
            .byte $96, $A6, $C6, $D6, $E6
            .byte $02, $0A, $03        // P-W, DTN, POR: the command byte's low bits
            .byte $01, $01, $01, $01, $01, $01, $01, $01
            .byte $01, $01, $01, $01, $01, $01, $01, $01
            .byte $01, $01, $01, $01, $1e, $46, $b6   // ...RUP RDN SRC DST MS# FLG AUX
mqOptB:     .byte $00, $00, $00, $00, $06, $02, $0F, $4F, $2F
            .byte $00, $00, $00, $00, $00, $00, $00, $00, $00
            .byte $00, $00, $00, $00, $00
            .byte $00, $00, $00
            .byte $04, $00, $84, $08, $0E, $0A, $07, $17
            .byte $03, $13, $23, $33, $43, $53, $63, $73  // the eight, index = opt >> 4
            .byte $01, $05, $1f, $8f, $00, $00, $00   // RUP RDN SRC DST MS# FLG AUX
            .byte $00, $00, $00, $00, $03, $0B, $00, $00, $00
mqMax:      .byte 255,  255,  255,  255,  23,   23,   0,    0,    0
            .byte 255,  255,  255,  255,  255,  255,  255,  127,  127
            .byte 255,  255,  255,  255,  255
            .byte 0,    0,    0
            .byte 15,   15,   15,   15,   15,   15,   7,    7
            .byte 1,    1,    1,    1,    1,    1,    1,    1
            .byte 31,   31,   7,    3,    0,    255,  255
//  How a value reaches the pair, one byte per id. 0 is "the whole option byte is
//  the operand"; 1-3 pack it into the option's top bits at 4, 3 and 5; 4 is one bit
//  at bit 3; 5-7 put the value's HIGH byte in the command byte; 9 has no encoder.
mqShape:    .byte 0,    0,    0,    0,    0,    0,    0,    0,    0
            .byte 0,    0,    0,    0,    0,    0,    0,    0,    0
            .byte 0,    0,    0,    0,    0
            .byte 5,    5,    6
            .byte 2,    1,    2,    1,    1,    1,    3,    3
            .byte 4,    4,    4,    4,    4,    4,    4,    4
            .byte 4,    4,    3,    3,    7,    0,    0
//  The packer's parameters, indexed by shape: the bits of the value it keeps, and
//  how far left they move. Shapes 1-3 are the only packing ones so far.
mqShMask:   .byte $00, $0f, $0f, $07, $1f
mqShShift:  .byte 0,    4,    3,    5,    3
//  Shapes 5-7 put the operand's HIGH byte in the command byte, so they carry their
//  own maximum and their own place for those bits: P-W and DTN share shape 5, POR
//  is 6 and MS# is 7. MS# stops at $03E7 = 999, the format's own limit -- its
//  fourth block runs $00-$E7.
.const MQ_WMAX_PWDTN = $0f      // the largest HIGH byte each wide shape carries
.const MQ_WMAX_POR   = $3f
.const MQ_WMAX_MS    = $03
.const MQ_WMASK_PWDTN = $0f     // ...and the bits of the command byte it packs into
.const MQ_WMASK_POR   = $3f
.const MQ_WMASK_MS    = $03
mqShMaxLo:    .byte $ff, $ff, $e7
mqShMaxHi:    .byte MQ_WMAX_PWDTN, MQ_WMAX_POR, MQ_WMAX_MS
mqShCmdMask:  .byte MQ_WMASK_PWDTN, MQ_WMASK_POR, MQ_WMASK_MS
mqShCmdShift: .byte 4,    2,    6
//  A maximum its mask cannot hold is truncated on the way into the command byte, so
//  the prompt would show a number the pair does not carry.
.assert "P-W/DTN: the maximum fits the command byte's mask", (MQ_WMAX_PWDTN <= MQ_WMASK_PWDTN) ? 1 : 0, 1
.assert "POR: the maximum fits the command byte's mask", (MQ_WMAX_POR <= MQ_WMASK_POR) ? 1 : 0, 1
.assert "MS#: the maximum fits the command byte's mask", (MQ_WMAX_MS <= MQ_WMASK_MS) ? 1 : 0, 1

// ==========================================
//  Undo -- a stack of range-diff records
// ==========================================
//  One record per user action: at MU_RECOFF, MU_RECOLDN image bytes became
//  MU_RECNEWN. The record carries the voice (so the gap op can fix its length
//  word), the old bytes and the new bytes, so undo and redo are each a single
//  range replace -- no multi-step offsets to keep in step as a gap shifts the
//  tail. Records grow up the stack; a trailing length byte lets undo walk back.
//  Layout: off_lo, off_hi, voice, oldN, newN, old[oldN], new[newN], total.

// --- muRecAtSek -- the undo record points at the pair the seek found ---
//  In: MU_SEKLO/MU_SEKHI, MU_SKV. A comes back as MU_SKV, as the inline copies
//  left it. Clobbers A only.
muRecAtSek:
    lda muSekLo
    sta muRecOff
    lda muSekHi
    sta muRecOff+1
    lda muSkV
    sta muRecV
    rts

// --- muRecAtIns -- the undo record points at the pair being inserted ---
//  The insert moves the seek's own pair, so the record's offset is the one the
//  insertion works on. Clobbers A only.
muRecAtIns:
    lda muInsLo
    sta muRecOff
    lda muInsHi
    sta muRecOff+1
    lda muSkV
    sta muRecV
    rts

// --- muCapOld -- MU_RECOLD = the MU_RECOLDN image bytes at MU_RECOFF ---
//  Called before an edit writes, so the old bytes survive it. oldN is 0 or 2.
muCapOld:
    jsr muRecKPtr
    ldy #0
muCoLp:
    cpy muRecOldN
    bcs muCoRts
    lda (kPtr),y
    sta muRecOld,y
    iny
    bne muCoLp
muCoRts:
    rts

// --- muRecKPtr -- kPtr = the image at MU_RECOFF, in window 0 ---
//  Same one-page rule as muSeekKPtr: a record's old/new byte count is at most 2.
//  Clobbers A, Y, kPtr, srcPtr -- the seat fills a window, so srcPtr does NOT
//  survive it: a caller that needs srcPtr must derive it after the call.
muRecKPtr:
    txa
    pha
    lda muRecOff
    sta muWinOfs
    lda muRecOff+1
    jsr muWinSeat0
    pla
    tax
    rts

// ==========================================
//  The stack: one byte at a time, nothing resident
// ==========================================
//  The stack is MU_UNDO_SIZE bytes of the REU file `musedit.und`, and NO part of it
//  lives in the image: every byte is one kReuReadAt or kReuWriteAt of length 1 at the
//  cursor's position, so the task carries no window page, no dirty flag and no
//  page-boundary arithmetic. The cursor (muUcPg, muUcOf) is the virtual offset of the
//  byte being read or written -- the page IS the position's high byte, so seating it
//  is two loads -- and it advances one byte per access.
//  A record is 6 + oldN + newN bytes and cannot exceed 255 (the total is one byte), so
//  an edit costs one transfer per record byte: tens of DMAs for a push, each a single
//  1-byte range with no directory scan, which is nothing next to the edit itself.
//  The file is created zeroed at open and dropped at exit, so a session always starts
//  with an empty stack.

// --- muUndoAt -- kArg = the stack file at the cursor, len 1, kPtr = MU_UCTMP ---
//  kReuReadAt/kReuWriteAt take the record pointer, the position, the length and the
//  C64 end in the kArg block with kPtr; here the position is the cursor and the C64
//  end is the one byte MU_UCTMP holds.
//  Clobbers A, X, Y, kArg..kArg+7, kPtr.
muUndoAt:
    lda muUcPg
    sta kArg+3                  // pos = page * 256 + offset: the page IS the high byte
    lda muUcOf
    sta kArg+2
    lda #1
    sta kArg+6                  // len = 1
    lda #0
    sta kArg+7
    ldx #<muUndoRec
    ldy #>muUndoRec
    stx kArg
    sty kArg+1
    ldx #<muUcTmp
    ldy #>muUcTmp
    stx kPtr
    sty kPtr+1
    rts

// --- muUndoSet -- the stack cursor := A/X (lo/hi of the virtual offset) ---
muUndoSet:
    sta muUcOf
    stx muUcPg
    rts

// --- muUndoNext -- the cursor advances one byte ---
muUndoNext:
    inc muUcOf
    bne muUnRts
    inc muUcPg
muUnRts:
    rts

// --- muUndoGet -- A = the stack byte at the cursor; the cursor advances ---
//  X, Y and kPtr survive, so a copy loop can index the image with them across the
//  call: the transfer puts its own pointers in kArg and kPtr.
muUndoGet:
    txa
    pha
    tya
    pha
    lda kPtr
    pha
    lda kPtr+1
    pha
    jsr muUndoAt
    jsr kReuReadAt
    jsr muUndoNext
    pla
    sta kPtr+1
    pla
    sta kPtr
    pla
    tay
    pla
    tax
    lda muUcTmp
    rts

// --- muUndoPut -- the stack byte at the cursor := A; the cursor advances ---
//  X, Y and kPtr survive, as above.
muUndoPut:
    sta muUcTmp                 // the value: the transfer reads it from here
    txa
    pha
    tya
    pha
    lda kPtr
    pha
    lda kPtr+1
    pha
    jsr muUndoAt
    jsr kReuWriteAt
    pla
    sta kPtr+1
    pla
    sta kPtr
    pla
    tay
    pla
    tax
    jmp muUndoNext

// --- muUndoArg -- kArg/kArg+1 = the stack file's name ---
muUndoArg:
    lda #<muUndoName
    sta kArg
    lda #>muUndoName
    sta kArg+1
    rts

// --- muUndoOpen -- the stack's file, made the first time ---
//  Out: C=0 muUndoRec holds it and muUndoOk is 1. C=1 there is no stack: the editor
//  still runs and still edits, and undo reports that there is nothing to undo.
muUndoOpen:
    jsr muUndoArg
    ldx #<muUndoRec
    ldy #>muUndoRec
    stx kPtr
    sty kPtr+1
    jsr kReuOpen
    bcc muUoYes
    jsr muUndoArg               // not there yet: make it, zeroed
    lda #0
    sta kArg+2
    lda #MU_UNDO_PAGES
    sta kArg+3
    jsr kReuCreate
    bcs muUoNo
    jsr muUndoArg               // kReuCreate hands back no record, so open it again
    ldx #<muUndoRec
    ldy #>muUndoRec
    stx kPtr
    sty kPtr+1
    jsr kReuOpen
    bcs muUoNo
muUoYes:
    lda #1
    sta muUndoOk
    clc
    rts
muUoNo:
    sec
    rts

// --- muPushEdit -- append the record the fields describe, mark the tune dirty ---
//  Reads the new bytes from the image at MU_RECOFF. muRecCont = 0 starts a new
//  action (the record is flagged a group start, bit 7 of the voice byte, and the
//  redo chain is cut); muRecCont = 1 appends to the action being built, so a
//  multi-voice insert undoes and redoes as one step. An overflow drops the whole
//  history, but only at an action's start, so a group is never torn.
muPushEdit:
    lda muRecCont
    bne muPeAppend
    lda #1
    sta muDirty
    lda #0
    sta muWarn
muPeAppend:
    lda muRecOldN               // total = 6 + oldN + newN
    clc
    adc muRecNewN
    adc #6
    sta muRecTot
    lda muRecCont               // the overflow reset runs only at a group start
    bne muPeFit
    lda muUndoTop               // would it overflow the buffer?
    clc
    adc muRecTot
    tay
    lda muUndoTop+1
    adc #0
    cmp #>MU_UNDO_SIZE
    bcc muPeFit
    bne muPeReset
    cpy #<MU_UNDO_SIZE
    bcc muPeFit
muPeReset:
    lda #0                      // no room: forget the history, keep this action
    sta muUndoTop
    sta muUndoTop+1
muPeFit:
    lda muUndoOk                // no stack file: the edit still happens, undo cannot
    bne muPeGo
    lda #0
    sta muRecCont
    rts
muPeGo:
    lda muUndoTop
    ldx muUndoTop+1
    jsr muUndoSet
    lda muRecOff
    jsr muUndoPut
    lda muRecOff+1
    jsr muUndoPut
    lda muRecV                  // the voice, with bit 7 = group start
    ldx muRecCont
    bne muPeVCont
    ora #$80
muPeVCont:
    jsr muUndoPut
    lda muRecOldN
    jsr muUndoPut
    lda muRecNewN
    jsr muUndoPut
    ldx #0                      // the old bytes
muPeOld:
    cpx muRecOldN
    bcs muPeOldDone
    lda muRecOld,x
    jsr muUndoPut
    inx
    bne muPeOld
muPeOldDone:
    jsr muRecKPtr               // the new bytes, from the image
    ldx #0
muPeNew:
    cpx muRecNewN
    bcs muPeNewDone
    txa
    tay
    lda (kPtr),y                // image[off + x]
    jsr muUndoPut
    inx
    bne muPeNew
muPeNewDone:
    lda muRecTot                // the trailing total
    jsr muUndoPut
    //  advance the top past the record
    jsr muUndoAdv
    sta muUndoLim
    lda muUndoTop+1
    sta muUndoLim+1
    lda #0                      // the next push starts a new action unless told
    sta muRecCont
    rts

// --- muUndoAdv -- climb the redo top past one record ---
//  Out: A = the new top low byte, which is also the redo limit until an edit opens
//  a new action. MU_RECTOT is the record length just written.
muUndoAdv:
    lda muUndoTop
    clc
    adc muRecTot
    sta muUndoTop
    lda muUndoTop+1
    adc #0
    sta muUndoTop+1
    lda muUndoTop
    rts

// --- muRecLoad -- read the record whose first byte is at offset A/X ---
//  Sets MU_RECOFF/V/OLDN/NEWN and muRecGrpStart (bit 7 of the voice byte, set on
//  an action's first record). muRecTot = its total length; muRecV is 0..2.
muRecLoad:
    jsr muUndoSet
    jsr muUndoGet
    sta muRecOff
    jsr muUndoGet
    sta muRecOff+1
    jsr muUndoGet
    pha
    and #$7f
    sta muRecV
    pla
    and #$80
    sta muRecGrpStart
    jsr muUndoGet
    sta muRecOldN
    jsr muUndoGet
    sta muRecNewN
    lda muRecOldN
    clc
    adc muRecNewN
    adc #6
    sta muRecTot
    rts

// --- muRangeReplace -- the image at MU_RECOFF: haveN bytes become wantN ---
//  In: muRrHave, muRrWant, muRecV set, and muRrSrc = the wantN source bytes'
//  offset in the stack. Opens or closes the difference first, because that moves the
//  image under kPtr, and only then reads the source in.
muRangeReplace:
    lda muRrWant
    cmp muRrHave
    beq muRrWrite
    bcc muRrShrink
    sec                         // grow: open (want - have) bytes after have
    sbc muRrHave
    sta muInsN
    lda muRecOff
    clc
    adc muRrHave
    sta muInsLo
    lda muRecOff+1
    adc #0
    sta muInsHi
    lda muRecV
    jsr muInsGap
    jmp muRrWrite
muRrShrink:
    lda muRrHave                // shrink: close (have - want) bytes after want
    sec
    sbc muRrWant
    sta muInsN
    lda muRecOff
    clc
    adc muRrWant
    sta muInsLo
    lda muRecOff+1
    adc #0
    sta muInsHi
    lda muRecV
    jsr muDelGap
muRrWrite:
    jsr muRecKPtr               // kPtr = image + off
    lda muRrSrc
    ldx muRrSrc+1
    jsr muUndoSet               // the stack cursor := the record's new bytes
    ldy #0
muRrLp:
    cpy muRrWant
    bcs muRrDone
    jsr muUndoGet
    sta (kPtr),y
    iny
    bne muRrLp
muRrDone:
    jmp muWinFlush0            // the pair reaches the page it lives on

// --- muSekAdv2 -- MU_SEKLO/HI on by two: past one pair ---
//  Every event is a pair, so landing past one means stepping two bytes in 16 bits.
muSekAdv2:
    lda muSekLo
    clc
    adc #2
    sta muSekLo
    bcc msaRts
    inc muSekHi
msaRts:
    rts

// --- muUndo -- Z: take back the newest action (its whole group) ---
muUndo:
    lda muUndoTop
    ora muUndoTop+1
    bne muUzLoop
    lda #M_NO_UNDO
    jmp muMsgGo
muUzLoop:
    lda muUndoTop               // the byte below the top: the trailing length
    sec
    sbc #1
    tay
    lda muUndoTop+1
    sbc #0
    tax
    tya
    jsr muUndoSet
    jsr muUndoGet
    sta muRecTot
    lda muUndoTop               // start = top - total
    sec
    sbc muRecTot
    sta muRecStart
    lda muUndoTop+1
    sbc #0
    sta muRecStart+1
    lda muRecStart
    ldx muRecStart+1
    jsr muRecLoad
    lda muRecStart              // the old bytes sit at start + 5
    clc
    adc #5
    sta muRrSrc
    lda muRecStart+1
    adc #0
    sta muRrSrc+1
    lda muRecNewN               // image has newN now; make it oldN (the old)
    sta muRrHave
    lda muRecOldN
    sta muRrWant
    jsr muRangeReplace
    lda muRecStart              // the top drops to the record just undone
    sta muUndoTop
    lda muRecStart+1
    sta muUndoTop+1
    lda muRecGrpStart           // a group start ends the action; otherwise go on
    bne muUzDone
    lda muUndoTop
    ora muUndoTop+1
    bne muUzLoop
muUzDone:
    jmp muRedrawNoHead

// --- muRedo -- X: replay the action most recently taken back (its group) ---
muRedo:
    lda muUndoTop
    cmp muUndoLim
    bne muUxLoop
    lda muUndoTop+1
    cmp muUndoLim+1
    bne muUxLoop
    lda #M_NO_REDO
    jmp muMsgGo
muUxLoop:
    lda muUndoTop
    ldx muUndoTop+1
    jsr muRecLoad
    lda muUndoTop               // the new bytes sit at top + 5 + oldN
    clc
    adc #5
    adc muRecOldN
    sta muRrSrc
    lda muUndoTop+1
    adc #0
    sta muRrSrc+1
    lda muRecOldN               // image has oldN now; make it newN (the new)
    sta muRrHave
    lda muRecNewN
    sta muRrWant
    jsr muRangeReplace
    jsr muUndoAdv
    cmp muUndoLim
    bne muUxPeek
    lda muUndoTop+1
    cmp muUndoLim+1
    beq muUxDone
muUxPeek:
    lda muUndoTop               // ...or when the next record starts a new action
    ldx muUndoTop+1
    jsr muRecLoad
    lda muRecGrpStart
    beq muUxLoop
muUxDone:
    jmp muRedrawNoHead

// --- muNoteOpt -- A = the linear note -> A = its option byte, in muNewOpt ---
//  The format stores a letter (1..7), an accidental (10 plain, 01 sharp) and an
//  octave, and it stores the octave INVERTED: the player's frequency table starts
//  at octave 7 and is halved once per octave field, so field = 7 - octave. A black
//  key is written as the sharp of the letter below it.
muNoteOpt:
    ldx #0
muNoDiv:
    cmp #12
    bcc muNoDivDone
    sec
    sbc #12
    inx
    jmp muNoDiv
muNoDivDone:
    sta muTmp                   // the semitone inside the octave
    stx muTmp2                  // ...and the octave
    ldx muTmp
    lda muSemSharp,x
    beq muNoPlain
    lda #$40
    jmp muNoAcc
muNoPlain:
    lda #$80
muNoAcc:
    sta muNewOpt
    ldx muTmp
    lda muSemLetter,x
    ora muNewOpt
    sta muNewOpt
    lda muTmp2
    eor #7                      // the field is stored inverted: field = 7 - octave
    asl
    asl
    asl
    ora muNewOpt
    sta muNewOpt
    rts

// ==========================================
//  Editing: lengths and gaps in the image
// ==========================================
//  The image IS the file: byte 0 is the load address and the three length words
//  at +2/+4/+6 are the file's own. An insert moves the tail up as one piece, so
//  every offset the player computes while it walks (a DEF's phrase address, a
//  CALL's return) follows with it and nothing in the image has to be patched.

// --- muLenHead -- the head run of a split event: tied, with the event's own option ---
//  A split's first run is the part of the event that is already there, so it keeps the
//  pair's own option byte and its tie bit is set.
muLenHead:
    lda #1
    sta muLenTie
    lda muSekOpt
    sta muLenOpt
    rts

// --- muLenNote -- the note run: tied, its option, MU_DURJIF jiffies ---
//  In: muLenMode says whether the events are counted or written. C comes back from
//  muLenEmit, so a caller may test it as it would after muLenOne.
muLenNote:
    lda #1
    sta muLenTie
    lda muNewOpt
    sta muLenOpt
    lda muDurJif
    jmp muLenOne

// --- muLenTwo -- two runs into the kPtr: MU_GAPJ, then the note ---
//  The first run is untermed and carries no option (rests in a gap fill, the opened
//  bytes in an insert); the second is the note itself, so it is tied and written with
//  MU_NEWOPT.
muLenTwo:
    lda #1
    sta muLenMode
    lda #0
    sta muLenTie
    sta muLenOpt
    lda muGapJ
    jsr muLenOne
    jmp muLenNote

// --- muLenOne -- a 16-bit length with no high byte, then muLenEmit ---
//  The lengths this task builds are all one byte (a row's slice of a note, a
//  rest, an inserted event), so the high byte is cleared here instead of at each
//  site. A = the jiffies; C comes back from muLenEmit unchanged.
muLenOne:
    sta muLenJ
    lda #0
    sta muLenJ+1
    jmp muLenEmit

// --- muLenEmit -- turn MU_LENJ jiffies into events ---
//  Greedy over muDurTab, longest first; every entry is a power of two, so any
//  length the tempo can express decomposes and never takes more events than it
//  needs. A same-pitch continuation is a tied event: the player keeps the note
//  sounding, and the option is repeated so the grid still shows the pitch.
//  In:  MU_LENJ = the length in jiffies; MU_LENOPT = the option byte;
//       muLenTie = 1 to tie every event after the first;
//       muLenMode = 0 count only, 1 write the pairs at kPtr (which advances).
//  Out: A = the number of events; C=1 the tempo cannot express it, or it needs
//       more than MU_LEN_MAX events. A failed count has written nothing.
muLenEmit:
    lda muLenJ
    sta muValLo
    lda muLenJ+1
    sta muValHi
    lda #0
    sta muLenCnt
muLeLp:
    lda muValLo
    ora muValHi
    beq muLeDone
    lda #0
    sta muLpIdx
muLeTry:
    ldx muLpIdx
    cpx #7
    bcs muLeFail            // nothing in the table is short enough
    lda muDurTab,x
    jsr muNoteJiffies
    bcs muLeNext            // not a length at this tempo
    sta muLpJ
    cmp #0                  // a store leaves the flags alone: test the value
    beq muLeNext            // a zero-jiffy length would never subtract: skip it
    lda muValHi
    bne muLeFit             // 256 or more left: the longest legal length fits
    lda muLpJ
    cmp muValLo
    beq muLeFit
    bcs muLeNext
muLeFit:
    lda muValLo
    sec
    sbc muLpJ
    sta muValLo
    bcs muLeSub
    dec muValHi
muLeSub:
    inc muLenCnt
    lda muLenCnt
    cmp #MU_LEN_MAX
    bcs muLeFail
    lda muLenMode
    beq muLeLp
    jsr muLeStore
    jmp muLeLp
muLeNext:
    inc muLpIdx
    jmp muLeTry
muLeDone:
    lda muLenCnt
    clc
    rts
muLeFail:
    sec
    rts

// --- muLeStore -- the pair for the pick just made, at kPtr ---
muLeStore:
    ldx muLpIdx
    lda muDurTab,x
    sta muLpDur
    lda muLenOpt                // a rest never carries a tie: it is silence
    beq muLsPlain
    lda muLenCnt
    cmp #1
    bne muLsCont
    lda muDurTie                // the armed tie rides on a new note's FIRST pair,
    beq muLsPlain               //   which is what the bar's TIE cell and the ',' key set
    jmp muLsTie
muLsCont:
    lda muLenTie                // a continuation ties into the one before it
    beq muLsPlain
muLsTie:
    lda muLpDur
    ora #$40
    sta muLpDur
muLsPlain:
    jsr muSeekKPtr              // kPtr = the page the pair lives on, at MU_SEKLO/HI
    ldy #0
    lda muLpDur
    sta (kPtr),y
    iny
    lda muLenOpt
    sta (kPtr),y
    jsr muWinFlush0            // the pair reaches the page it came from
    jsr muSekAdv2
muLsRts:
    rts

// --- muImgRoom -- C=0 when MU_INSN more bytes fit behind MU_LEN ---
//  The working file is MU_SCR_PAGES pages however long the tune is, so growing the
//  image is only a length check: nothing is allocated and nothing moves.
//  C=1 MU_STR holds the reason.
muImgRoom:
    jsr muImgNeed
    lda muNeedHi
    cmp #MU_SCR_PAGES
    bcc muIrOk
    bne muIrBad
    lda muNeedLo
    beq muIrOk              // exactly to the end of the file
muIrBad:
    lda #M_NO_ROOM
    jsr muMsgSet
    sec
    rts
muIrOk:
    clc
    rts

// --- muImgNeed -- MU_NEED = MU_LEN + MU_INSN ---
muImgNeed:
    ldy #MU_LEN
    lda (dataPtr),y
    clc
    adc muInsN
    sta muNeedLo
    ldy #MU_LEN+1
    lda (dataPtr),y
    adc #0
    sta muNeedHi
    rts

// --- muInsSek2 -- MU_INSLO/HI = the seek's own pair + 2 ---
//  An insertion at the seek's onset opens behind that pair, so the new bytes start
//  two past the offset the seek found. Clobbers A and C only.
muInsSek2:
    lda muSekLo
    clc
    adc #2
    sta muInsLo
    lda muSekHi
    adc #0
    sta muInsHi
    rts

// --- muInsGap -- open MU_INSN bytes at MU_INSLO/HI ---
//  In: MU_INSLO/HI the offset (an event boundary), MU_INSN the byte count,
//      A = the voice whose length word grows.
//  Out: C=0 the gap is open; C=1 MU_STR the reason and the image is unchanged.
muInsGap:
    sta muInsV
    jsr muImgRoom
    bcs muInRts
    lda #1
    sta muGapUp                 // the tail moves UP: the pages are walked backwards
    jsr muGapMove
    bcs muInRts
    jsr muImgSeat               // page 0: the length words live there
    lda muInsV                  // the voice's word, at image offset 2 + 2 * voice
    asl
    clc
    adc #2
    tay
    lda (kPtr),y
    clc
    adc muInsN
    sta (kPtr),y
    bcc muInLenHi
    iny
    lda (kPtr),y
    adc #0
    sta (kPtr),y
muInLenHi:
    jsr muWinFlush0
    ldy #MU_LEN                 // and the image's own length
    lda (dataPtr),y
    clc
    adc muInsN
    sta (dataPtr),y
    bcc muInOk
    ldy #MU_LEN+1
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y
muInOk:
    clc
muInRts:
    rts

// --- muDelGap -- close MU_INSN bytes at MU_INSLO/HI ---
//  In: MU_INSLO/HI the offset the tail lands on, MU_INSN the byte count,
//      A = the voice whose length word shrinks. The tail behind the gap -- the
//      rest of this voice, the later voices, the description -- moves down as
//      one piece, so nothing in the image has to be patched.
//  MU_GAPMOVE treats MU_INSLO/HI as the start of the run it shifts, and its
//  destination is that start minus MU_INSN: handing it the gap's own offset would
//  copy the doomed bytes MU_INSN lower and drop the voice's last MU_INSN bytes
//  instead of closing anything, so the run starts AFTER the bytes being closed.
muDelGap:
    sta muInsV
    lda #0
    sta muGapUp                 // the tail moves DOWN: the pages are walked forwards
    lda muInsLo
    clc
    adc muInsN
    sta muInsLo
    bcc muDgGo
    inc muInsHi
muDgGo:
    jsr muGapMove
    bcs muDgRts
    jsr muImgSeat
    lda muInsV
    asl
    clc
    adc #2
    tay
    lda (kPtr),y
    sec
    sbc muInsN
    sta (kPtr),y
    bcs muDgLenHi
    iny
    lda (kPtr),y
    sbc #0
    sta (kPtr),y
muDgLenHi:
    jsr muWinFlush0
    ldy #MU_LEN
    lda (dataPtr),y
    sec
    sbc muInsN
    sta (dataPtr),y
    bcs muDgOk
    ldy #MU_LEN+1
    lda (dataPtr),y
    sbc #0
    sta (dataPtr),y
muDgOk:
    clc
muDgRts:
    rts

// --- muGapMove -- shift the image's tail from MU_INSLO/HI by MU_INSN bytes ---
//  MU_GAPUP = 1 opens a gap (up, so the pages are walked BACKWARDS and no source
//  byte is overwritten before it has moved) and 0 closes one (forwards). Window 0
//  is the page buffer, and the move stays inside the working file, whose size never
//  changes -- so nothing is copied in RAM and no page count is involved.
//  Out: C=0 moved; C=1 the filesystem refused a page.
muGapMove:
    jsr muLdPages               // A = the image's pages, never 0
    sta muScrN
    lda muGapUp
    beq muGmDown
    lda muScrN                  // up: from the last page down to the gap's own
    sec
    sbc #1
    sta muScrPg
muGmUpLp:
    jsr muGapPage
    bcs muGmRts
    lda muScrPg
    cmp muInsHi
    bcc muGmOk
    beq muGmOk
    dec muScrPg
    jmp muGmUpLp
muGmDown:
    lda muInsHi                 // down: from the gap's page up to the last one
    sta muScrPg
muGmDnLp:
    jsr muGapPage
    bcs muGmRts
    inc muScrPg
    lda muScrPg
    cmp muScrN
    bcc muGmDnLp
muGmOk:
    jsr muWinReset              // the pages behind every cache tag just moved
    clc
muGmRts:
    rts

// --- muGapPage -- move the run of bytes that belongs to page MU_SCRPG ---
//  The page the gap starts on is only moved from the gap's low byte onwards; every
//  other page moves whole. A run is 256 bytes, so a length of 256 is stored as a
//  high byte of 1.
muGapPage:
    lda #0
    sta muScrSh
    lda muScrPg
    cmp muInsHi
    bne muGpLen
    lda muInsLo
    sta muScrSh                 // the gap starts part way into this page
muGpLen:
    lda #0
    sta muScrLen+1
    sec
    sbc muScrSh
    sta muScrLen
    bne muGpLoad
    lda #1
    sta muScrLen+1              // start was 0: the whole page moves
muGpLoad:
    jsr muPgLoad                // window 0 := page muScrPg
    bcs muGpRts
    lda muScrSh                 // destination = page x 256 + start, then +/- MU_INSN
    sta muDstLo
    lda muScrPg
    sta muDstHi
    lda muGapUp
    beq muGpSub
    lda muDstLo
    clc
    adc muInsN
    sta muDstLo
    bcc muGpOut
    inc muDstHi
muGpSub:
    lda muDstLo
    sec
    sbc muInsN
    sta muDstLo
    bcs muGpOut
    dec muDstHi
muGpOut:
    jsr muSFrecArg
    lda muDstLo
    sta kArg+2
    lda muDstHi
    sta kArg+3
    lda muScrLen
    sta kArg+6
    lda muScrLen+1
    sta kArg+7
    lda muWin                  // kPtr = window 0 + the run's start
    clc
    adc muScrSh
    sta kPtr
    lda muWin+1
    adc #0
    sta kPtr+1
    jmp kReuWriteAt
muGpRts:
    rts

// --- muPgLoad -- window 0 := page MU_SCRPG of the working file ---
muPgLoad:
    jsr muSFrecArg
    lda muScrPg
    jsr muPgArg
    jsr muWin0Ptr
    jmp kReuReadAt

// --- muWinFlush0 -- window 0 goes back to the page it holds, and forget the rest ---
//  WARNING: the page is the one the window RECORDS in muWinPg, never an assumed 0 --
//  only the header lives on page 0.
muWinFlush0:
    ldx #0
    jsr muWinFlush
    jmp muWinReset              // the move changed the pages behind every cache tag

// --- muKeyNote -- a note key, or nothing ---
//  The home row plays the white keys and W E T Y U O P the black ones, an octave
//  up or down with - and +. A note key enters its pitch at the cursor through
//  the same writer a piano click uses.
//  In: A = the screen code of the key.  Out: C=1 when it is a note key, and A then
//  holds the linear note it plays. MU_HOV is left alone: that is the pointer's.
muKeyNote:
    sta muTmp2                  // the key, while the table is walked
    ldx #0
muKnLp:
    lda muKeyNotes,x
    beq muKnNone                // the table ends with a zero key
    cmp muTmp2
    beq muKnHit
    inx
    inx
    jmp muKnLp
muKnHit:
    inx
    lda muKeyNotes,x            // the semitone offset inside the octave
    sta muTmp
    lda muKbdOct                // octave x 12 = octave x 8 + octave x 4
    asl
    asl
    sta muTmpL
    lda muKbdOct
    asl
    asl
    asl
    clc
    adc muTmpL
    clc
    adc muTmp
    cmp #96
    bcs muKnNone                // above B7 there is no note to enter
    sec                         // A = the linear note
    rts
muKnNone:
    clc
    rts

muKeyNotes:
    .byte 'a',$00, 'w',$01, 's',$02, 'e',$03, 'd',$04, 'f',$05
    .byte 't',$06, 'g',$07, 'y',$08, 'h',$09, 'u',$0a, 'j',$0b
    .byte 'k',$0c, 'o',$0d, 'l',$0e, 'p',$0f, ';',$10
    .byte 0

// --- muArmKey -- '1'..'9' arms the duration a new note is written with ---
//  1-7 are the plain classes (whole ... 64th, muDurTab's order) and the dot
//  state rides along; 8 and 9 are the utility lengths UTL and UTV -- class 1,
//  where bit 5 picks UTV, so they carry no dots at all.
muArmKey:
    sec
    sbc #'1'
    cmp #7
    bcs muAkUtil
    tax
    lda muDurArm
    and #$a0
    ora muDurTab,x
    sta muDurArm
    jmp muDrawArm
muAkUtil:
    cmp #8
    beq muAkUtv
    lda #$04                    // UTL
    bne muAkSet
muAkUtv:
    lda #$24                    // UTV
muAkSet:
    sta muDurArm
    jmp muDrawArm

// --- muDotCycle -- '.': none -> dotted -> double-dotted -> triplet -> none ---
//  The utility class does not cycle: its bit 5 picks UTV, not a dot.
muDotCycle:
    lda muDurArm
    and #$1c
    cmp #$04                    // class 1
    beq muDcRts
    lda muDurArm
    and #$a0
    beq muDcDot
    cmp #$20
    beq muDcDd
    cmp #$a0
    beq muDcTri
    lda #$00                    // triplet -> plain
    beq muDcSet
muDcDot:
    lda #$20
    bne muDcSet
muDcDd:
    lda #$a0
    bne muDcSet
muDcTri:
    lda #$80
muDcSet:
    sta muTmp
    lda muDurArm
    and #$1c
    ora muTmp
    sta muDurArm
    jmp muDrawArm
muDcRts:
    rts

// ==========================================
//  The playing-note dots
// ==========================================
//  Three red dots, one per voice, pinned on the piano keys that are sounding.
//  The player publishes each voice's next-command pointer, so the option byte of
//  the note currently sounding is simply the byte before it: no history, no
//  second walk, and it is exact for a whole note and a 64th alike.
.const MU_DOT_SLOT0 = 5           // slots 0 and 1 are the pointer
.const MU_DOT_Y     = 216         // on a black key: the upper three key rows
.const MU_DOT_YN    = 226         // a natural sits in the white-key row below them

// --- muDotInit -- resolve the dot art, once ---
muDotInit:
    sprColour(MU_DOT_SLOT0+0, 2)  // voice 1: red
    sprColour(MU_DOT_SLOT0+1, 7)  // voice 2: yellow
    sprColour(MU_DOT_SLOT0+2, 3)  // voice 3: cyan
    sprPriority(0)                // in front: a dot on a black key still shows
    lda #<muSDot
    sta kPtr
    lda #>muSDot
    sta kPtr+1
    lda #MU_DOT_SLOT0             // a sheet record is per sprite, so three calls
    jsr kSpriteSheet
    lda #MU_DOT_SLOT0+1
    jsr kSpriteSheet
    lda #MU_DOT_SLOT0+2
    jsr kSpriteSheet
    ldx #0                        // frame 0: the one dot
    lda #MU_DOT_SLOT0
    jsr kSpriteFrame
    ldx #0
    lda #MU_DOT_SLOT0+1
    jsr kSpriteFrame
    ldx #0
    lda #MU_DOT_SLOT0+2
    jmp kSpriteFrame

// --- muDots -- one pass: put each sounding voice's dot on its key ---
//  Called once per displayed frame. kSpritePos writes the shadow only, so the
//  dots move with the music and the kernel's raster commit publishes them.
muDots:
    lda #0
    sta muDotMask
    sta muDotV
muDvLp:
    ldx muDotV                  // the newest event at or above the playhead: the
    lda muHistN,x               //   ring is walked newest first, so the first row
    sec                         //   that is not below the playhead's is the one
    sbc #1                      //   that is sounding
    and #MU_HIST-1
    sta muDotS
    lda #MU_HIST
    sta muDotC
    txa
    asl
    asl
    asl
    asl
    asl
    sta muDotBase               // voice x MU_HIST
muDvScan:
    lda muDotS
    clc
    adc muDotBase
    tay
    lda muHistRHi,y
    cmp muPhCurLo+1
    bcc muDvFound               // a row below the playhead's high byte: it is at or
    beq muDvRowCmp              //   before the playhead, so it is the one sounding
    jmp muDvScanNx
muDvRowCmp:
    lda muHistRLo,y
    cmp muPhCurLo
    bcc muDvFound
    beq muDvFound
muDvScanNx:
    dec muDotS
    lda muDotS
    and #MU_HIST-1
    sta muDotS
    dec muDotC
    bne muDvScan
    jmp muDvNext                // nothing recorded yet for this voice
muDvFound:
    lda muHistOpt,y
    sta muDotOpt
    lda #0                      // a natural reads one row lower than an accidental
    sta muDotNat
    lda muDotOpt
    and #$07
    beq muDvNext                // a rest: no dot
    lda muDotOpt
    jsr muPitchSplit            // A = the semitone, X = the octave, C = 1 if rolled
    sta muDotSem
    bcs muDvNatKey              // a rolled pitch is the neighbouring octave's natural
    lda muDotOpt
    and #$c0
    cmp #$80                    // 10 is plain: a white key as well
    bne muDvOct
muDvNatKey:
    lda #1
    sta muDotNat
muDvOct:
    txa
    sta muDotTmp
    asl
    clc
    adc muDotTmp                // octave x 3 ...
    asl
    asl                         // ... x 4 = x 12, the table's row
    clc
    adc muDotSem
    tay
    lda muDotTabHi,y            // the ninth bit: a dot above x 255 is real
    sta muDotXHi
    lda muDotTab,y
    sta muDotX
    ldx muDotV                  // this voice's slot bit in the enable mask
    lda muDotBit,x
    ora muDotMask
    sta muDotMask
    lda muDotV                  // kSpritePos: X = sprite, A = x lo, Y = y, C = 0
    clc
    adc #MU_DOT_SLOT0
    tax
    ldy #MU_DOT_Y               // an accidental: up on the black key
    lda muDotNat
    beq muDvY
    ldy #MU_DOT_YN              // a natural: below the black keys
muDvY:
    lda muDotXHi                // C = the ninth bit of x (kSpritePos takes it in C)
    lsr
    lda muDotX                  // ...and A = the low byte. lda/ldx/ldy leave C alone
    jsr kSpritePos
muDvNext:
    inc muDotV
    lda muDotV
    cmp #3
    bcs muDvEnd
    jmp muDvLp
muDvEnd:
    lda muDotMask
    ora #%00000011              // the pointer's two slots stay on
    ldx #SPR_SHD_ENABLE
    jmp kSpriteReg

// --- muDotsOff -- hide the dots (the tune stopped, or ended) ---
muDotsOff:
    lda #%00000011
    ldx #SPR_SHD_ENABLE
    jmp kSpriteReg

// --- muCurPos -- the pointer x into MU_XL/MU_XH ---
//  MU_POS holds x lo, x hi, y lo, y hi, so the column work divides MU_XH:MU_XL by 8.
muCurPos:
    ldy #MU_POS
    lda (dataPtr),y
    sta muXl
    ldy #MU_POS+1
    lda (dataPtr),y
    sta muXh
    rts

// --- muHovDot -- the cursor voice's dot on the key under the pointer ---
//  The pointer says which pitch a click would enter, and this says which VOICE it
//  would enter it on: the dot is the same one that voice gets during playback, in
//  its own colour, parked on the key -- and it sounds as it arrives, so a tune can
//  be found by ear before anything is written.
//  Playback owns the dot slots while the tune runs, and MU_HOV is not a key when the
//  pointer is anywhere else, so both cases end in no dot at all.
muHovDot:
    lda muPlayOn
    beq muHdIdle
    ldy #0                      // playback's dots own the keys: forget ours, so the
    sty muHdOn                  //   first pass after a stop places it again
    ldy #$ff
    sty muHdOld
    rts
muHdIdle:
    ldy #MU_HOV
    lda (dataPtr),y
    cmp #$fe
    bcc muHdKey
    lda muHdOn                  // off the keyboard: our dot goes, and the key it was
    bne muHdGone                //   on is forgotten so a return to it sounds again
    rts
muHdGone:
    lda #0
    sta muHdOn
    lda #$ff                    // no key: the next place on a key is always new
    sta muHdOld
    jmp muDotsOff
muHdKey:
    cmp muHdOld                 // the same key on the same voice: nothing to redo
    bne muHdNew
    ldy #MU_VOICE
    lda (dataPtr),y
    cmp muHdOldV
    beq muHdRts
muHdNew:
    ldy #MU_VOICE               // the voice the cursor acts on
    lda (dataPtr),y
    sta muHdV
    ldy #MU_HOV
    lda (dataPtr),y
    sta muHdOld
    jsr muAudFreq               // A = the linear note -> muTmp = the semitone
    ldx muTmp
    lda muSemSharp,x
    beq muHdNat
    ldy #MU_DOT_Y               // a black key: the dot sits up on it
    bne muHdHaveY
muHdNat:
    ldy #MU_DOT_YN              // a natural: below the black keys
muHdHaveY:
    sty muHdY
    ldy muHdOld
    lda muDotTabHi,y            // the dot's x, and its ninth bit
    sta muTmp2
    lda muDotTab,y
    sta muTmpL
    lda muHdV
    clc
    adc #MU_DOT_SLOT0
    tax                         // X = that voice's dot slot
    ldy muHdY
    lda muTmp2
    lsr                         // C = the ninth bit of x, which kSpritePos reads
    lda muTmpL
    jsr kSpritePos
    lda muHdOld
    jsr muAudNote               // it sounds as it comes under the pointer
    lda muHdV
    sta muHdOldV
    lda #1
    sta muHdOn
    ldx muHdV                   // this voice's dot alone: the pointer's slots stay on
    lda muDotBit,x
    ora #%00000011
    ldx #SPR_SHD_ENABLE
    jmp kSpriteReg
muHdRts:
    rts


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
    lda #M_NO_MOUSE
    jsr muMsgSet
    jmp muPtrRefuse             // out of branch range: invert and jump
muPtrGo:
    muSet(MU_HOV, $fe)          // no pitch yet: the first hover must redraw
    lda #$ff                    // no key on no voice: the first one placed is new even
    sta muHdOld                 //   when it is the first key of the keyboard
    sta muHdOldV
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
    lda #SYS_GETTID
    jsr syscall
    sta muTid                   // who we are, for the display-owner test
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
    jsr muDotInit               // the playing-note dots share the sprite state
    jmp muPlace                 // show it before the first sample

muNoArt:
    lda #M_NO_ART
    jsr muMsgSet
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
    sta muHRow
    cmp #MU_BAR_ROW
    bne muHovNotBar
    jmp muHovBar
muHovNotBar:
    lda #0                      // off the bar: any highlight there ends
    jsr muBarHover
    lda muHRow
    cmp #MU_MODE_ROW
    beq muHovMode               // the header row: the mode button
    lda #0
    jsr muModeHover             // anywhere else: its highlight ends
    lda muHRow
    cmp #MU_PIANO_TOP
    bcc muHovOff
    cmp #MU_PIANO_TOP+MU_PIANO_ROWS
    bcs muHovOff
    sec
    sbc #MU_PIANO_TOP
    sta muHRow
    jsr muCurPos
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
    cmp muKeyBlackRows
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
muHovOff:
    jmp muHovNone
// The mode readout: its three cells reverse under the pointer, and a click there
// toggles. The row is not the keyboard, so the pitch readout goes blank.
// --- muPtrCol -- A = the pointer cell column, x / 8 ---
//  Out: A = MU_XL = the column the pointer is in; MU_XH is left zero by the divide.
muPtrCol:
    jsr muCurPos
    lsr muXh
    ror muXl
    lsr muXh
    ror muXl
    lsr muXh
    ror muXl
    lda muXl
    rts

muHovMode:
    jsr muPtrCol
    cmp #MU_MODE_COL
    bcc muHovModeOff
    cmp #MU_MODE_COL+3
    bcs muHovModeOff
    lda #1
    bne muHovModeSet
muHovModeOff:
    lda #0
muHovModeSet:
    jsr muModeHover
    jmp muHovOff
// The control bar: the pointer's column picks the item out of the hit map, so a
// highlight can never disagree with what a click there would do.
muHovBar:
    jsr muHovNone               // off the keyboard: its hover and readout end
    lda #0                      // ...and the mode button's highlight, if it had one
    jsr muModeHover
    ldy #MU_POS+1
    lda (dataPtr),y             // x hi
    beq muHovBarLo
    cmp #1                      // x >= 256 is the bar's right-hand end
    bne muHovBarOff
    ldy #MU_POS
    lda (dataPtr),y
    lsr
    lsr
    lsr
    clc
    adc #32
    cmp #40
    bcs muHovBarOff
    tay
    lda muBarMap,y
    jmp muBarHover
muHovBarLo:
    ldy #MU_POS
    lda (dataPtr),y
    lsr
    lsr
    lsr
    tay
    lda muBarMap,y
    jmp muBarHover
muHovBarOff:
    lda #0
    jmp muBarHover
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
    jsr muPutNoteName
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
    jsr muPtrCol
    lda muRowNew
    cmp #MU_MODE_ROW
    bne muClkNotModeRow
    jmp muClkMode               // the mode button
muClkNotModeRow:
    cmp #MU_BAR_ROW
    bne muClkGrid
    ldx muXl                    // the bar: one code per column
    lda muBarMap,x
    bne muClkHave               // a blank cell answers nothing
    jmp muClkRts
muClkHave:
    cmp #MU_BAR_SAVE
    beq muBarSave
    cmp #MU_BAR_PLAY
    beq muBarPlay
    cmp #MU_BAR_CMD
    beq muBarCmd
    cmp #MU_BAR_Q
    bne muClkNotBarQ
    jmp muQuit                  // out of branch range: invert and jump
muClkNotBarQ:
    cmp #MU_BAR_UNDO
    bne muClkNotU
    jmp muBarUndo
muClkNotU:
    cmp #MU_BAR_DUR
    bne muClkNotD
    jmp muBarDur
muClkNotD:
    cmp #MU_BAR_TIE
    bne muClkNotT
    jmp muBarTie
muClkNotT:
    cmp #MU_BAR_REST
    bne muClkNotRu
    jmp muBarRest
muClkNotRu:
    cmp #MU_BAR_DEL
    bne muClkNotDel
    jmp muBarDel
muClkNotDel:
    cmp #MU_BAR_INS
    bne muClkRts                // a code with no handler: MU_BAR_INS is the last
    jmp muBarIns                //   live one, and the hover lights no other
muBarSave:
    jsr muSaveTry
    rts
muBarPlay:
    jmp muPlayToggle
muBarCmd:
    jmp muObjKey

// --- muClkMode -- the mode readout is a button: its three cells toggle the mode ---
muClkMode:
    lda muXl                    // the header is inside the first 40 columns
    cmp #MU_MODE_COL
    bcc muClkRts
    cmp #MU_MODE_COL+3
    bcs muClkRts
    jmp muInsToggle

muClkGrid:
    cmp #MU_GRID_TOP
    bcc muClkLow
    cmp #MU_GRID_BOT+1
    bcs muClkLow
    lda muXl
    cmp #MU_LANE1_COL                       // lane 1
    bcc muClkRts
    cmp #MU_LANE1_COL+MU_LANE_W
    bcc muClkV0
    cmp #MU_LANE1_COL+MU_LANE_STEP          // the column between lanes
    bcc muClkRts
    cmp #MU_LANE1_COL+MU_LANE_STEP+MU_LANE_W
    bcc muClkV1
    cmp #MU_LANE1_COL+2*MU_LANE_STEP        // the column between lanes
    bcc muClkRts
    cmp #MU_LANE1_COL+2*MU_LANE_STEP+MU_LANE_W
    bcc muClkV2
    bcs muClkRts
muClkV0:
    lda #0
    jmp muClkSetVoice
muClkV1:
    lda #1
    jmp muClkSetVoice
muClkV2:
    lda #2
muClkSetVoice:
    ldy #MU_VOICE
    sta (dataPtr),y
    jsr muCursorHide
    lda muRowNew
    ldy #MU_ROW
    sta (dataPtr),y
    jsr muHeadTail              // the click moved the cursor: its BAR and R follow
    jmp muCursorShow
muClkLow:
    cmp #MU_PIANO_TOP            // the piano: the click enters that pitch
    bcc muClkRts
    cmp #MU_PIANO_TOP+MU_PIANO_ROWS
    bcs muClkRts
    ldy #MU_HOV                 // the key the pointer is over
    lda (dataPtr),y
    sta muNoteLin
    jmp muNoteClick
muClkRts:
    rts

// --- muBarUndo -- the UNDO cell is the Z key ---
//  Undo repaints the grid and the head itself, so this only has to refuse while the
//  music runs, exactly as the key does.
muBarUndo:
    jsr muPlaying
    bcs mbuRts
    jmp muUndo
mbuRts:
    rts

// --- muBarDur -- the DUR cell steps the armed length on ---
//  The order is muDurTab's -- whole, half, quarter, 8th, 16th, 32nd, 64th -- and the
//  dot or triplet mark rides along, so the cell only ever changes the class. A
//  utility length (class 1) is not in that table, so it starts the cycle at the
//  whole note rather than stepping to something that is not a length.
muBarDur:
    jsr muPlaying
    bcs mbdRts
    ldx #0
mbdFind:
    lda muDurArm
    and #$1c
    cmp muDurTab,x
    beq mbdNext
    inx
    cpx #7
    bcc mbdFind
    ldx #7                      // a utility length: the next step is the whole note
mbdNext:
    inx
    cpx #7
    bcc mbdHave
    ldx #0
mbdHave:
    lda muDurTab,x
    sta muTmp
    lda muDurArm
    and #$a0                    // the dot or triplet mark is kept
    ora muTmp
    sta muDurArm
    jmp muDrawArm
mbdRts:
    rts

// --- muTieToggle -- arm or drop the tie, from the bar's TIE cell or the `,` key ---
//  A tie is the duration byte's bit 6, and the player holds the note into the one
//  after it. It is armed here and stays armed -- like the length it rides with -- so
//  the cell's light is the flag itself (muBarPaint reads MU_DURTIE for that cell), and
//  no other item on the bar can take it away. A rest never carries one: muRestKey
//  clears the bit, and the fill path writes rests with it clear.
muTieToggle:
    lda muDurTie
    eor #1
    sta muDurTie
    jmp muBarPaint

// --- muBarTie -- the TIE cell is the `,` key ---
muBarTie:
    jmp muTieToggle

// --- muBarRest -- the REST cell is the INST/DEL key ---
//  A rest keeps the event's length and its option byte becomes zero, which is
//  muRestKey's work; all this adds is the same refusal the key has while the music
//  runs.
muBarRest:
    jsr muPlaying
    bcs mbrtRts
    jmp muRestKey
mbrtRts:
    rts

// --- muBarDel -- the DEL cell is SHIFT+INST/DEL: the command on the line, else the
//  event, and the gap closes ---
muBarDel:
    jsr muPlaying
    bcs mbdlRts
    jmp muDelKey2
mbdlRts:
    rts

// --- muBarIns -- the INS cell is the I key: one row of silence in all three voices ---
//  The INST of INST/DEL, beside the cell that rests an event and the one that deletes
//  it. It edits, so it refuses while the music runs, as the key does.
muBarIns:
    jsr muPlaying
    bcs mbisRts
    lda #1                      // one row
    jmp muInsertTime
mbisRts:
    rts

// ==========================================
//  Playback
// ==========================================
//  The music runs in a thread CHILD. It shares this code page, so it reads the
//  parent's bytes (muPlayOn, muPlayQuit, muSidCfgB) and never the instance buffer --
//  a thread child does not share dataPtr. kSidPlay is asked for MODE 1, so the child
//  pages <base>.wrk through windows 3-5 (which the editor never seats) and a tune
//  costs no RAM however long it is: the child reads the same file the editor edits,
//  so an edit is heard on the next pass with no copy at all.

// --- muSidCfg -- the 9-byte kSidPlay config: the file's offset, then three windows ---
muSidCfg:
    lda muSFrec+2               // the working file's filesystem offset, lo/mid/hi
    sta muSidCfgB
    lda muSFrec+3
    sta muSidCfgB+1
    lda muSFrec+4
    sta muSidCfgB+2
    ldx #6                      // windows 3, 4, 5: muWin is lo/hi pairs, so x = 6, 8, 10
    ldy #3
muScLp:
    lda muWin,x
    sta muSidCfgB,y
    iny
    lda muWin+1,x
    sta muSidCfgB,y
    iny
    inx
    inx
    cpx #12
    bne muScLp
    rts

// --- muMusicInit -- spawn the child once, at startup ---
muMusicInit:
    lda #<muMusicThread
    sta spawnPtr
    lda #>muMusicThread
    sta spawnPtr+1
    lda #$02                    // the child's ZP window: sid.lib's scratch $02-$0a
    sta kArg+6
    lda #9
    sta kArg+7
    lda #SYS_THREADSPAWN
    jsr syscall
    bcs muMiNone                // no free slot: run without music
    stx muPlaySlot
    rts
muMiNone:
    lda #$ff
    sta muPlaySlot
    rts

// --- muPlayToggle -- SPACE: start when stopped, stop when playing ---
muPlayToggle:
    lda muPlayOn
    beq muPtStart
    jmp muPlayStop
muPtStart:
    lda muPlaySlot
    cmp #$ff
    beq muPtNoChild             // the player is gone (see muPlayTick): nothing can play
    lda #0
    sta muPlayHush              // arm a fresh stop
    jsr muPlayBase              // the tick the cursor's row starts at
    lda MU_POS_BLOCK+6          // the published count is the last pass's until the
    sta muPhOld                 //   player restarts it, so the playhead holds the
    lda #1                      //   cursor's row until the music reaches it
    sta muPhWait
    lda #MU_BAR_PLAY
    jsr muBarActive             // the bar shows PLAY
    lda #1
    sta muPlayOn                // the music starts here, the view ready
    rts
muPtNoChild:
    lda #M_NO_PLAYER
    jmp muMsgGo

// --- muPlayBase -- the tick the cursor's row starts at, into MU_PHBASE ---
//  The walk records every drawn line's tick, so the row under the cursor is the
//  number of jiffies the player must play, silently, before the music is heard. The
//  playhead's row is preset to the cursor's own, so the head reads the row that will
//  sound from the moment PLAY is pressed, and the view does not move at all.
//  A line past the tune's end holds no row: the pass starts at the beginning instead.
//  Clobbers A, X, Y.
muPlayBase:
    lda #0
    sta muPhBaseLo
    sta muPhBaseHi
    jsr muCursorIdx
    tax
    lda muLnTbHi,x
    cmp #$ff
    beq muPbCur
    lda muLnTick,x
    sta muPhBaseLo
    lda muLnTickHi,x
    sta muPhBaseHi
muPbCur:
    jsr muCursorAbs
    sta muPhCurLo
    stx muPhCurLo+1
    rts

// --- muSidHushSafe -- hush only when sid.lib is loaded and the child is there ---
//  A lazy library that fails to load does not return: the kernel prints
//  "could not load <lib>.lib" and exits the CALLING task. So an unguarded
//  kSidHush in the parent would take musedit down with it. MU_SIDOK records that
//  a play attempt was made (the child sets it just before kSidPlay), and a live
//  child proves the stub did not kill it.
muSidHushSafe:
    lda muSidOk
    beq muShsRts
    ldx muPlaySlot
    cpx #$ff
    beq muShsRts
    lda taskState,x
    beq muShsRts
    jmp kSidHush
muShsRts:
    rts

// --- muPlayStop -- silence the chip and ask the child to idle ---
//  The child may be about to claim the SID inside kSidPlay; a single hush can
//  land in that sliver. A second hush a slice later catches it (muKeyIdle).
muPlayStop:
    lda #0
    sta muPlayOn
    sta muPlayHush
    jsr muDotsOff
    jmp muSidHushSafe

// --- muPlayRewind -- a pass is over: the playhead restarts at the beginning ---
//  Reached when the child reports a natural end (muPlayTick). The player is spawned
//  for a single pass, and it clears its own jiffy count as it starts (sidInitSong),
//  so the view restarts with it.
muPlayRewind:
    jsr muBarPlayOff            // the pass is over: PLAY goes plain in this redraw
    lda #0
    sta muPhCurLo
    sta muPhCurLo+1
    jmp muRewindView

// --- muRewindView -- the score and the cursor go back to the first row ---
//  The player always begins a pass at the tune's first row, so the view has to
//  begin there too: the window, the history ring and the cursor all go home and
//  the score is redrawn from row 0. Nothing else rewinds, so a stopped cursor is
//  still where the editing is -- until the next SPACE rewinds it with the music.
muRewindView:
    lda #0
    sta muTop
    sta muTop+1
    sta muToff
    sta muHistN
    sta muHistN+1
    sta muHistN+2
    muSet(MU_ROW, MU_GRID_TOP)  // the cursor, and with it the head's BAR and R
    jsr muDotsOff
    jmp muRedrawAll             // grid, head and cursor, in that order

// --- muPlayTick -- once per idle pass: refusal report, playhead, second hush ---
muPlayTick:
    lda muPlayEnd               // the child reached the tune's last row: rewind
    beq muPtRefd
    lda #0
    sta muPlayEnd
    jmp muPlayRewind
muPtRefd:
    lda muPlayRefd              // the child met a busy SID: say so once
    beq muPtAlive
    lda #0
    sta muPlayRefd
    sta muPlayOn
    jmp muPtNoPlayer
muPtAlive:
    lda muPlayOn
    beq muPtStopped
    lda muPlaySlot              // $FF is "no player at all": it must not reach
    cmp #$ff                    //   taskState,x, which is a 24-slot table
    beq muPtLost
    tax
    lda taskState,x             // a failed lazy load exits the child, so the
    bne muPtPlaying             //   parent must notice or PLAY looks inert
muPtLost:
    lda #0
    sta muSidOk
    sta muPlayOn                // STOPPED, not $FF: a reported player is not a
    lda #$ff                    //   playing one, and $FF here left the playhead
    sta muPlaySlot              //   walking silently for ever
muPtNoPlayer:
    jsr muBarPlayOff            // nothing is playing: the redraw below drops it
    lda #M_NO_PLAYER
    jsr muMsgSet
    jsr muStatus
    jmp muRedrawAll             // the kernel's load-failure text landed in our grid
muPtPlaying:
    lda frameCount              // playing: move the playhead once per displayed frame
    cmp muPhFrame
    beq muPtRts
    sta muPhFrame
    jsr muDots
    jmp muPlayhead
muPtStopped:
    jsr muBarPlayOff            // the child clears muPlayOn itself when the tune
                                //   ends, so the PLAY highlight is dropped here
    lda muPlayHush               // the cursor stays where the music stopped
    bne muPtRts                 // the second hush already landed
    inc muPlayHush
    jmp muSidHushSafe
muPtRts:
    rts

// --- muPlayhead -- the playhead is the clock ---
//  The player publishes the jiffies it has played in this pass, and the walk records
//  the tick each line's row starts at, so the row under the playhead is read from the
//  player's own clock through the map the walk drew: it is the row the music is inside.
//  The cursor walks down one row at a time, and the window follows it a line at a time.
muPlayhead:
    jsr muPhSync                // the row is the player's own song tick, mapped
    jsr muPhFollow
    jmp muPhCursor

// --- muPhSync -- the playhead row, found by the tick the line was walked at ---
//  The player publishes a 16-bit count of the jiffies it has played in this pass,
//  and every drawn line records the tick its row starts at (MU_LNTICK), so the
//  playhead's row is that line's row: the deepest line whose tick the music has
//  reached, walked back to the first line that carries that tick. Nothing is
//  extrapolated, which is what makes a tune that changes tempo -- or that changes it
//  more than once, or inside a phrase body -- follow exactly what the walk drew,
//  because it IS what the walk drew.
//  The walk back matters where a row spans several lines: a row's own line and the
//  command lines above it carry the row's tick, and the cursor must stand on the line
//  that shows the notes. Every ROW carries its own tick -- one unit past the row above
//  it -- so a note that lasts several rows gives each of them a distinct tick and the
//  playhead walks through the note one row at a time, exactly as the music does.
//  A tick above every line clamps to the band's last row, which is still below the
//  follow's target, so the band scrolls and the next line answers properly.
//  The count is one jiffy ahead of the row being played: the player starts every
//  voice's counter at 1 and increments before it processes.
//  Clobbers A, X, Y and kArg.
muPhSync:
    lda MU_POS_BLOCK+6          // SID_POS_TICK = MU_POS_BLOCK + 6: the block is
    sec                         //   contiguous, so the low byte is +6 and the high +7
    sbc #1
    sta kArg
    lda MU_POS_BLOCK+7
    sbc #0
    cmp #$ff                    // a count of zero wraps: no jiffy has played yet
    bne muPsPub
    lda #0
    sta kArg
muPsPub:
    sta kArg+1
    lda muPhWait                // the count belongs to ONE pass, and a stop leaves it
    beq muPsLook                //   where the music stopped: until the player publishes
    lda MU_POS_BLOCK+6          //   a different value the view must not chase it. The
    cmp muPhOld                 //   published byte, against the one recorded at the
    beq muPsRts                 //   start -- the adjusted low byte below is a different
    lda kArg+1                  //   number and would never match. Then the player is
    cmp muPhBaseHi              //   still fast-forwarding to the row PLAY started from,
    bcc muPsRts                 //   which the count reaches before any sound is heard
    bne muPsGo
    lda kArg
    cmp muPhBaseLo
    bcc muPsRts
muPsGo:
    lda #0
    sta muPhWait
muPsLook:
    ldx #MU_GRID_ROWS-1
muPsLp:
    lda muLnTbHi,x              // a line past the tune's end holds no row
    cmp #$ff
    beq muPsNext
    lda kArg+1                  // has the music reached this line's tick?
    cmp muLnTick+MU_GRID_ROWS,x
    bcc muPsNext
    bne muPsTake
    lda kArg
    cmp muLnTick,x
    bcc muPsNext
muPsTake:
    cpx #0                      // a row's own line and the command lines above it
    beq muPsSet                 //   carry the row's tick, so the deepest qualifying
    lda muLnTickHi-1,x          //   line is the row's last one: walk back to the
    cmp muLnTickHi,x            //   row's first line, which is the row itself
    bne muPsSet
    lda muLnTick-1,x
    cmp muLnTick,x
    bne muPsSet
    lda muLnTbHi-1,x            // a line past the tune's end holds no row
    cmp #$ff
    beq muPsSet
    dex
    jmp muPsTake
muPsSet:
    lda muLnTbLo,x
    sta muPhCurLo
    lda muLnTbHi,x
    sta muPhCurLo+1
    rts
muPsNext:
    dex
    bpl muPsLp
    lda muLnTbLo                // before the band's first line: the top row
    sta muPhCurLo
    lda muLnTbHi
    sta muPhCurLo+1
muPsRts:
    rts

// --- muLnFind -- X = the visible index of the playhead's own line, or $ff ---
//  The scan runs from the bottom up and skips the command lines (MU_LNKIND): a
//  row can hold several lines and only the one with the notes may take the cursor.
muLnFind:
    ldx #MU_GRID_ROWS-1
mlfLp:
    lda muLnKind,x
    beq mlfNext
    lda muLnTbHi,x
    cmp muPhCurLo+1
    bne mlfNext
    lda muLnTbLo,x
    cmp muPhCurLo
    beq mlfRts
mlfNext:
    dex
    bpl mlfLp
    ldx #$ff
mlfRts:
    rts

// --- muPhCursor -- the cursor marks the sounding row ---
//  Whatever muLnFind reports, the cursor is one indicator and stopping leaves it
//  where it is.
muPhCursor:
    jsr muCursorHide            // already out when the follow moved the band, so
                                //   this only matters for a playhead that stays put
    jsr muLnFind
    cpx #$ff
    beq muPcGone
    txa
    clc
    adc #MU_GRID_TOP
    ldy #MU_ROW
    sta (dataPtr),y
    jsr muCursorShow
    jmp muPhHead                // the bar and the row both follow the playhead
muPcGone:
    rts                         // the sounding row is not in the window: the cursor
                                //   simply stays out rather than jumping to the top

// --- muPhFollow -- hold the playhead's own line MU_FOLLOW_CENTER above the bottom ---
//  The test is in LINES, never in rows. A row with three commands is four lines, so
//  "the bottom line ten rows below the playhead" can be more lines than the band
//  holds: that target is unreachable, and a row-based follow scrolls to its cap on
//  every pass and runs the window away from the music while the cursor's row falls
//  out of it. muLnFind gives the playhead's line, and the band scrolls until that
//  line sits MU_FOLLOW_CENTER from the bottom -- always reachable, one line a scroll.
//  A settled playhead costs one scan per frame and nothing more.
//  The walk ends when the playhead's line is AT the target, not after a fixed number
//  of lines: a row occupies as many lines as it has commands above it, so a tune whose
//  rows carry commands asks the band for several lines per pass, and a one-line walk
//  falls behind for as long as that lasts. MU_FOLLOW_WALK is the most lines one pass
//  may walk, so a long catch-up (a seek, a pass the producer stalled on) still ends.
//  WARNING: the target is MU_FOLLOW1, not the constant: a row's own line sits as many
//  lines down as that row has commands above it, so the tune's FIRST row can already
//  be deeper than the centre. Aiming at the centre there scrolls the view off the
//  tune's own opening before a note has played, and the music then walks back up to
//  where the pencil already is. muLnRowDraw raises the target to whatever the opening
//  turns out to be, for any tune, and muScrollBand puts it back the moment the band
//  leaves the opening -- so the centre is where the playhead lives once the music, and
//  not the opening's commands, is what fills the band.
muPhFollow:
    jsr muCursorHide            // out before the band can move: one hide for the
                                //   whole pass, however many lines it walks, and
                                //   muPhCursor puts it back where the row now is
    lda #MU_FOLLOW_WALK
    sta muPfN
muPfStep:
    jsr muLnFind
    cpx #$ff
    beq muPfBlind
    cpx muFollow1               // settled once the line is at or above the target
    bcc muPfRts                 //   the playhead is pinned to, counted from the top
muPfMove:
    jsr muScrollBand            // one line: the band moves, only it is drawn
    dec muPfN
    bne muPfStep
muPfRts:
    rts
muPfBlind:
    lda muLnTbHi+MU_GRID_ROWS-1 // the playhead's line is not in the band
    cmp muPhCurLo+1
    bcc muPfMove                // the band is behind it: scroll on
    bne muPfRts                 // it is above the band: never scroll away from it
    lda muLnTbLo+MU_GRID_ROWS-1
    cmp muPhCurLo
    bcc muPfMove
    bne muPfRts
    lda muGrpSt                 // the band has reached the row and is still emitting
    bne muPfMove                //   it: finish the row so its own line can be found
    rts

// ==========================================
//  The grid's lines
// ==========================================
//  The band is a list of LINES, not of time rows. A **time row** is one line and
//  carries every voice's note that starts on it, so the three lanes line up. A
//  **structural command** -- TEM, UTL, UTV, HED, DEF, CALL, TAL, HLT, END -- moves
//  no time at all, so it is a line of its own, above its row, with the other two
//  lanes empty. The producer below emits those lines strictly in order, one per
//  call, which is what lets the band draw only the line that arrives: a row with
//  three commands is four lines, and the playhead walks past all of them.
//
//  A screen row is not a time row, because a command line sits between them, so
//  MU_LNTBLO/HI record which row each visible line holds -- that is what the
//  cursor, the header's BAR/R and the seek read.

// --- muLnNext -- emit the next line at MU_LNSCR, or discard it if that is $ff ---
//  Out: C=1 when the tune has no more lines. Draws what it emits.
muLnNext:
    lda muGrpSt
    bne muLnHave
    jsr muGrpOpen
    bcs muLnNone
muLnHave:
    lda muLogIx
    cmp muLogLns
    bcs muLnRowLine
    jsr muLnCmdDraw
    inc muLogIx
    clc
    rts
muLnRowLine:
    jsr muLnRowDraw
    lda #0
    sta muGrpSt                 // the next call opens the next row
    clc
    rts
muLnNone:
    sec
    rts

// --- muGrpOpen -- fill the next row's group: its commands and its notes ---
//  Every event that starts on the row is walked: muStep executes it, the command
//  handlers log the structural ones, and the notes are held in MU_PEND* because
//  they cannot be drawn as they arrive -- a note from voice 1 and a command from
//  voice 2 share the row, and the row's own line comes after both.
//  Out: C=1 when every voice has halted, i.e. there is no such row.
//  The group's end is also where the row's PHRASE body state is read: a DEF and the
//  body's first event take no time, so they land on the same row, and the row is an
//  edit target that lands inside the body. Reading it here rather than at the group's
//  start is what makes such a row read as the body, and an END's own row as the
//  stream it returns to -- which is the answer an insert at that row needs.
// --- muGrpBar -- the bar the row being opened falls in ---
//  A bar is one whole note counted in the segment's own rows, so its size is the
//  walk's and it changes with the tempo; stepping the count here, once per row, is
//  what replaces dividing a row by one bar size for the whole tune.
muGrpBar:
    lda muSegBar
    ora muSegBar+1
    beq mgbRts                  // a bar of no rows never happens
    lda muGrpRow
    sec
    sbc muBarStart
    sta muValLo
    lda muGrpRow+1
    sbc muBarStart+1
    sta muValHi
    lda muValHi
    cmp muSegBar+1
    bcc mgbRts
    bne mgbStep
    lda muValLo
    cmp muSegBar
    bcc mgbRts
mgbStep:
    inc muBarNo
    bne mgbN1
    inc muBarNo+1
mgbN1:
    lda muBarStart
    clc
    adc muSegBar
    sta muBarStart
    lda muBarStart+1
    adc muSegBar+1
    sta muBarStart+1
    jmp muGrpBar                // a row that jumped skips on; muGrpBar ends it
mgbRts:
    rts

muGrpOpen:
    inc muGrpRow
    bne mgpFresh
    inc muGrpRow+1
mgpFresh:
    jsr muGrpBar                // the bar this row falls in, for BAR and the numbers
    lda #0
    sta muLogIx
    sta muLogN
    sta muLogN+1
    sta muLogN+2
    sta muLogN+3
    sta muLogN+4
    sta muLogN+5
    sta muPendOn
    sta muPendOn+1
    sta muPendOn+2
    lda #1
    sta muGrpSt
    lda #64                     // a row that never advances must not spin
    sta muGdStepN
mgpLoop:
    jsr muSimNext
    bcs mgpDone
    ldx muSv
    lda muVRowHi,x              // the group takes every event that starts on its
    cmp muGrpRow+1              //   row: a voice whose next event is later has
    bcc mgpStep                 //   nothing more for this row
    bne mgpDone
    lda muVRow,x
    cmp muGrpRow
    bcc mgpStep
    bne mgpDone
mgpStep:
    dec muGdStepN
    beq mgpDone
    jsr muStep
    bcs mgpDone
    lda muNoteOut
    beq mgpLoop                 // a voice halted on this row: no note to record
    jsr muGdSpan
    jsr muHistAdd
    ldx muSv
    lda muSopt
    sta muPendOpt,x
    lda muVDur,x
    sta muPendDur,x
    lda muSpan
    ora muSpan+1
    beq mgpOff
    lda #1
mgpOff:
    sta muPendSp,x
    lda #1
    sta muPendOn,x
    jsr muGdAdv
    jmp mgpLoop
mgpDone:
    lda muLogN+1                // the sound commands are drawn first: their counts
    cmp muLogN+3                //   are the ODD blocks (voice * 2 + 1, class 1), so a
    bcs mgpSnd2                 //   line carries one class only -- a tempo and an
    lda muLogN+3                //   envelope step are not comparable events
mgpSnd2:
    cmp muLogN+5
    bcs mgpSnd3
    lda muLogN+5
mgpSnd3:
    sta muLogSnd                // the row's sound lines, from the top
    lda muLogN                  // ...and the structural nine, the EVEN blocks,
    cmp muLogN+2                //   sit nearest the row, where they were before the
    bcs mgpStr2                 //   sound commands had lines of their own
    lda muLogN+2
mgpStr2:
    cmp muLogN+4
    bcs mgpStr3
    lda muLogN+4
mgpStr3:
    clc
    adc muLogSnd
    sta muLogLns
    clc
    rts

// --- muLnSpot -- where this line goes: drawn or not, which row it holds ---
//  In: A = 1 for the row's own line, 0 for a command's. Out: MU_LNDRAW (0 = the
//  line is outside a partial repaint) and the table entry for its screen row. A
//  line the caller discards is not recorded.
//  The kind matters because a row can occupy several lines and the playhead may
//  only stand on the one that shows the notes: a command line shares its row but
//  carries no note, so the cursor must pass over it.
muLnSpot:
    sta muTmpL                  // the kind, while the index is formed
    lda #1
    sta muLnDraw
    lda muLnScr
    cmp #$ff
    beq mlsNoDraw
    sec
    sbc #MU_GRID_TOP
    tay
    lda muGrpRow
    sta muLnTbLo,y
    lda muGrpRow+1
    sta muLnTbHi,y
    lda muTmpL
    sta muLnKind,y
    lda muBarNo                 // the bar this line's row falls in, and the tempo in
    sta muLnBarLo,y             //   force on it: neither is a division of the row
    lda muBarNo+1
    sta muLnBarHi,y
    lda muSimT
    sta muLnTem,y
    jsr muLnTkSet               // the tick this line's row starts at
    lda muPhDf
    sta muLnPh,y                // the group is complete by now, so this is the phrase
    lda muPhDf+1                //   each voice is in at the END of the row -- which is
    sta muLnPh+MU_GRID_ROWS,y   //   what an insert at the row would land in
    lda muPhDf+2
    sta muLnPh+2*MU_GRID_ROWS,y
    lda muGdClip
    beq mlsRts
    lda muGrpRow+1
    cmp muGdHi+1
    bcc mlsIn
    bne mlsNoDraw
    lda muGrpRow
    cmp muGdHi
    bcs mlsNoDraw
mlsIn:
    lda muGrpRow+1
    cmp muGdLo+1
    bcc mlsNoDraw
    bne mlsRts
    lda muGrpRow
    cmp muGdLo
    bcc mlsNoDraw
mlsRts:
    rts
mlsNoDraw:
    lda #0
    sta muLnDraw
    rts

// --- muLnTkSet -- the tick line Y's row starts at ---
//  The segment's own map, MU_SEGTK + (row - MU_SEGROW) * MU_SEGU, derived on every
//  line rather than accumulated along the walk, so it cannot drift: one row's tick
//  is one unit past the row above it, a note that lasts several rows gives every one
//  of them its own time, and the playhead walks through the note instead of standing
//  on its first row. Y is the table index and survives (kMul16 keeps it).
muLnTkSet:
    lda muGrpRow
    sec
    sbc muSegRow
    sta kArg
    lda muGrpRow+1
    sbc muSegRow+1
    sta kArg+1
    lda muSegUnit
    sta kArg+2
    lda #0
    sta kArg+3
    jsr kMul16                  // A and X: the product is kArg+4..kArg+7
    lda kArg+4
    clc
    adc muSegTick
    sta muLnTick,y
    lda kArg+5
    adc muSegTick+1
    sta muLnTickHi,y
    rts

// --- muLnCmdDraw -- the row's k-th command line: one command per lane ---
//  A lane can hold one command, so the line carries every voice's k-th command at
//  once, in the voice's own colour: three commands on the same row take one line,
//  not three. The two classes never share a line -- the row's sound commands take
//  the lines above MU_LOGSND and the structural nine the lines below it, nearest
//  the row's own line. The row column stays blank -- the line is not a point in
//  time -- and the row's own line, below, is the one with a number.
muLnCmdDraw:
    lda #0                      // a command's line: the playhead passes over it
    jsr muLnSpot
    lda muLnDraw
    bne mlcGo
    rts
mlcGo:
    lda #0
    sta muLnV
mlcVoices:
    lda #1                      // a sound line: its depth is the line index
    sta muTmpL
    lda muLogIx
    cmp muLogSnd
    bcc mlcHave                 // ix < the row's sound lines: this is a sound one
    lda #0
    sta muTmpL                  // a structural line, counted from the row upwards
    lda muLogIx
    sec
    sbc muLogSnd
mlcHave:
    sta muTmpH
    lda muLnV
    asl
    ora muTmpL
    tax                         // the block: voice * 2 + class
    lda muTmpH
    cmp muLogN,x
    bcc mlcHasCmd               // this voice has a command this deep
    jmp mlcNext
mlcHasCmd:
    lda muLogBase,x
    clc
    adc muTmpH
    sta muTmp2                  // the slot, while the lane is drawn
    ldx muLnV
    lda muVoiceCol,x
    jsr kSetTextColor
    ldx muLnV
    lda muLaneCol,x
    sta muGdCol
    jsr muGdAtCell
    lda #1                      // the MNEMONIC reads as a bar; the operand below
    sta muRevText               //   stays plain so the value is easy to read
    ldy muTmp2
    lda (muLogIdP),y
    jsr muMnPrint               // leaves MU_REVTEXT at 0
    ldy muTmp2                  // TAL, HLT and END take no operand
    lda (muLogIdP),y
    cmp #MU_MN_TAL
    beq mlcNext
    cmp #MU_MN_HLT
    beq mlcNext
    cmp #MU_MN_END
    beq mlcNext
    lda #' '                    // one space, then the operand
    jsr muPutC
    ldy muTmp2
    lda muLogHi,y               // a byte for every command but the three wide
    bne mlcWide                 //   ones: P-W, DTN and POR
    lda (muLogLoP),y               // every operand is a count, so it prints in decimal
    jsr muByteDec
    jmp mlcNext
mlcWide:
    sta muDecHi                 // the high byte is already in A
    lda (muLogLoP),y
    sta muDecLo
    jsr muDec16
mlcNext:
    inc muLnV
    lda muLnV
    cmp #3
    bcs mlcDone
    jmp mlcVoices
mlcDone:
    lda #MU_C_TEXT
    jmp kSetTextColor

// --- muByteDec -- print A (0-255) in decimal ---
//  The kernel's kByteToDec does the conversion: it formats 1-3 digits at (kPtr)
//  and returns the length in Y, and it never emits a leading zero. The digits then
//  go out through muPutC, whose kPrintCharAt clobbers X and Y, so the read index
//  lives in MU_DECIDX.
muByteDec:
    pha                         // the value rides the stack while kPtr is set up
    lda #<muDecBuf
    sta kPtr
    lda #>muDecBuf
    sta kPtr+1
    pla
    jsr kByteToDec              // digits at (kPtr), Y = length
    sty muDecCnt
    lda #0
    sta muDecIdx
mdbLp:
    ldy muDecIdx
    cpy muDecCnt
    bcs mdbRts
    lda muDecBuf,y              // the buffer's OWN address: muPutC clobbers kPtr
    jsr muPutC
    inc muDecIdx
    bne mdbLp                   // at most three digits, so never off the end
mdbRts:
    rts

// --- muDec16 -- print MU_DECHI:MU_DECLO in decimal, without leading zeroes ---
//  For the three operands that are wider than a byte -- P-W, DTN, POR -- so the
//  value is never zero and the first digit is always printed. Repeated subtraction
//  over the decimal weights: a 6502 has no divide, and the widest of the three is
//  a portamento's 14 bits, so five weights cover it.
//  WARNING: the weight index lives in MU_DECIDX, not in X. muPutC prints through
//  kPrintCharAt, which clobbers X, so an index in X is destroyed by the first
//  digit printed -- the loop then walks off the weight table and never ends.
muDec16:
    lda #0
    sta muDecAny                // 0 = nothing has been printed yet
    sta muDecIdx
mdLp:
    ldx muDecIdx
    lda #0
    sta muDecDig
mdTry:
    lda muDecLo
    sec
    sbc muDecWLo,x
    tay
    lda muDecHi
    sbc muDecWHi,x
    bcc mdNext                  // the weight does not fit: the digit is done
    sta muDecHi
    sty muDecLo
    inc muDecDig
    jmp mdTry
mdNext:
    lda muDecDig
    ora muDecAny
    beq mdSkip                  // a leading zero prints nothing
    lda #1
    sta muDecAny
    lda muDecDig
    ora #'0'
    jsr muPutC
mdSkip:
    inc muDecIdx
    lda muDecIdx
    cmp #5
    bne mdLp
    rts

muDecWLo:   .byte <10000, <1000, <100, <10, <1
muDecWHi:   .byte >10000, >1000, >100, >10, >1

// --- muLnRowDraw -- the row's own line: every lane's note, then its number ---
muLnRowDraw:
    lda muGrpRow                // the tune's first row: its own line sits as deep as
    ora muGrpRow+1              //   the opening's command count, and while the band
    bne mlrSpot                 //   still holds that opening the follow may not ask
    lda muLogLns                //   for a line above it. The command lines that came
    clc                         //   before this row's own line, in this very walk, are
    adc #1                      //   that depth; muScrollBand gives the centre back once
    cmp muFollow1               //   the opening scrolls away
    bcc mlrSpot
    sta muFollow1
mlrSpot:
    lda #1                      // the row's own line: the one with the notes, and
    jsr muLnSpot                //   the only line the cursor may stand on
    lda muLnDraw
    bne mlrGo
    rts
mlrGo:
    ldx #0
mlrVoices:
    stx muLnV
    lda muPendOn,x
    beq mlrNext
    jsr muLnNote
mlrNext:
    ldx muLnV
    inx
    cpx #3
    bcc mlrVoices
    jsr muLnQmark
    jmp muLnNum                 // the number belongs to the row's own line, every
                                //   time: a row's command lines sit above it and are
                                //   not points in time, so they never carry one

// --- muLnNum -- the row's number in its own column, bar-relative ---
//  The bar's first row is the walk's, not a multiple of one bar size: a tempo
//  change starts a bar wherever it lands, so the count is a subtraction.
muLnNum:
    lda muGrpRow
    sec
    sbc muBarStart
    sta muDecLo
    lda muGrpRow+1
    sbc muBarStart+1
    sta muDecHi
    lda muLnScr
    ldy #MU_CURROW
    sta (dataPtr),y
    lda #1
    ldy #MU_CURCOL
    sta (dataPtr),y
    lda muDecLo
    clc
    adc #1
    sta muDecLo                 // the row within the bar, one-based
    lda muDecHi
    adc #0
    sta muDecHi
    lda muDecHi                 // a bar of a refined grid can hold 100 rows or more,
    bne mlrThree                //   and muNum2 prints exactly TWO: ten tens would print
    lda muDecLo                 //   a colon. So the width is decided first, in 16 bits
    cmp #100
    bcs mlrThree
    lda muDecLo
    jmp muNum2                  // under a hundred: two digits from column 1
mlrThree:
    ldx #0
mlrHLp:
    lda muDecHi
    bne mlrHSub
    lda muDecLo
    cmp #100
    bcc mlrHDone
mlrHSub:
    lda muDecLo
    sec
    sbc #100
    sta muDecLo
    bcs mlrHNext
    dec muDecHi
mlrHNext:
    inx
    jmp mlrHLp
mlrHDone:
    stx muTmp                   // the hundreds digit, as muPutC owns A, X and Y
    lda #0                      // column 0, so three digits end at column 2 and the
    ldy #MU_CURCOL              //   cursor's own cell at column 3 stays free
    sta (dataPtr),y
    lda muTmp
    clc
    adc #'0'
    jsr muPutC
    lda muDecLo
    jmp muNum2                  // the last two digits, tens then ones

// --- muLnQmark -- '?' in the lane of the row whose length the tempo refused ---
//  The refused event is never walked, so it is not in MU_PEND: the flag comes from
//  the refusal's own record, on the one line whose row it died on.
muLnQmark:
    lda muErr
    cmp #MU_ERR_LEN
    bne mlqRts
    lda muErrRow+1
    cmp muGrpRow+1
    bne mlqRts
    lda muErrRow
    cmp muGrpRow
    bne mlqRts
    ldx muErrV
    lda muVoiceCol,x
    jsr kSetTextColor
    ldx muErrV
    lda muLaneCol,x
    sta muGdCol
    jsr muGdAtCell
    lda #'?'
    jsr muPutC
    lda #MU_C_TEXT
    jmp kSetTextColor
mlqRts:
    rts

// --- muLnNote -- one voice's note cells on the row's own line ---
//  In: X = the voice, MU_LNSCR = the line. The pitch, a space, the duration in the
//  format's own numbers (C#6 16 is a sixteenth, G-4 4 a quarter), then the lane's
//  last cell: '?' when its onset fell between rows. A rest is three dots and the
//  same number.
//  Only the onset row carries anything -- the rows a note covers stay blank, which
//  is what makes a scroll cost nothing under a sounding note.
muLnNote:
    lda muVoiceCol,x            // the lane wears its voice's colour, the same one
    jsr kSetTextColor           //   the dot for that voice uses
    ldx muLnV
    lda muLaneCol,x
    sta muGdCol
    jsr muGdAtCell
    ldx muLnV
    lda muPendOpt,x
    and #$07
    bne mlnPitch
    lda #MU_REST_CH
    jsr muPutC
    lda #MU_REST_CH
    jsr muPutC
    lda #MU_REST_CH
    jsr muPutC
    jmp mlnDur
mlnPitch:
    ldx muLnV
    lda muPendOpt,x
    jsr muGdPitch
mlnDur:
    lda #MU_SPACE
    jsr muPutC
    ldx muLnV
    lda muPendDur,x
    lsr
    lsr
    and #$07                    // the length class, 0-7
    tay
    lda muDurNum,y
    beq mlnDot                  // a utility length has no number
    jsr muNumDur
mlnDot:
    ldx muLnV                   // the dot cell: bit 5 is a dot, bit 7 a triplet or,
    lda muPendSp,x              //   with bit 5, the second dot -- so the value alone
    beq mlnTie                  //   does not say which length the note is. A squeezed
    lda muPendDur,x             //   event takes no mark: its lane ends in the '?'
    and #$a0                    //   that says the row is not where it starts
    beq mlnTie
    lsr
    lsr
    lsr
    lsr
    lsr
    tay
    lda muDotCh,y
    beq mlnTie
    jsr muPutC
mlnTie:
    ldx muLnV                   // the format's tie flag: bit 6 of the duration
    lda muPendOpt,x             //   byte. A rest has no note to carry on from, so
    and #$07                    //   the option's note index is asked first.
    beq mlnMark
    lda muPendDur,x
    and #$40
    beq mlnMark
    lda #MU_SPACE               // " TIE", one character at a time: the writer
    jsr muPutC                  //   clobbers A, X and Y, so nothing indexes with
    lda #'T'                    //   them
    jsr muPutC
    lda #'I'
    jsr muPutC
    lda #'E'
    jsr muPutC
mlnMark:
    ldx muLnV
    lda muPendSp,x
    bne mlnDone                 // a whole row or more: nothing is squeezed
    lda muLaneCol,x
    clc
    adc muLaneW,x
    sec
    sbc #1
    sta muGdCol
    jsr muGdAtCell
    lda #MU_OFF_CH
    jsr muPutC
mlnDone:
    lda #MU_C_TEXT
    jmp kSetTextColor

// --- muScrollBand -- advance the grid one line without repainting it ---
//  kScrollScreen moves the band's pixels and matrix up one line through
//  REU_BOUNCE (the REU does the move) and blanks the line at the bottom; the line
//  that arrives is drawn, and MU_LNTB moves with the band so a screen row still
//  names the row it holds. Nothing else in the band is touched.
//  MU_TOFF follows the line that leaves the top: a command line's successor is the
//  next line of the same row's group, one further in, and the row's own line is
//  the group's last, so whatever arrives above is the next row's first line.
//  The cursor is NOT touched here: it belongs to the caller, which takes it out
//  once before the scroll and puts it back once after -- the follow scrolls several
//  lines in a pass, and a hide per line would blank a cell of the line that had
//  just arrived.
muScrollBand:
    lda muLnKind                // the line the band is about to drop
    bne msbNewRow
    inc muToff
    bne msbGo
msbNewRow:
    lda #0
    sta muToff
msbGo:
    lda #MU_GRID_TOP
    ldx #MU_GRID_BOT
    jsr kScrollScreen
    ldx #0
msbShift:
    lda muLnTbLo+1,x
    sta muLnTbLo,x
    lda muLnTbHi+1,x
    sta muLnTbHi,x
    lda muLnKind+1,x
    sta muLnKind,x
    lda muLnPh+1,x
    sta muLnPh,x
    lda muLnPh+MU_GRID_ROWS+1,x
    sta muLnPh+MU_GRID_ROWS,x
    lda muLnPh+2*MU_GRID_ROWS+1,x
    sta muLnPh+2*MU_GRID_ROWS,x
    lda muLnBarLo+1,x
    sta muLnBarLo,x
    lda muLnBarHi+1,x
    sta muLnBarHi,x
    lda muLnTem+1,x
    sta muLnTem,x
    lda muLnTick+1,x
    sta muLnTick,x
    lda muLnTickHi+1,x
    sta muLnTickHi,x
    inx
    cpx #MU_GRID_ROWS-1
    bcc msbShift
    lda #MU_GRID_BOT
    sta muLnScr
    jsr muLnNext
    bcc msbGot
    lda #$ff                    // the tune has no more lines: the tail stays empty
    sta muLnTbLo+MU_GRID_ROWS-1
    sta muLnTbHi+MU_GRID_ROWS-1
    lda #0
    sta muLnKind+MU_GRID_ROWS-1
    sta muLnPh+MU_GRID_ROWS-1
    sta muLnPh+MU_GRID_ROWS+MU_GRID_ROWS-1
    sta muLnPh+2*MU_GRID_ROWS+MU_GRID_ROWS-1
    sta muLnBarLo+MU_GRID_ROWS-1
    sta muLnBarHi+MU_GRID_ROWS-1
    sta muLnTem+MU_GRID_ROWS-1
    sta muLnTick+MU_GRID_ROWS-1
    sta muLnTickHi+MU_GRID_ROWS-1
msbGot:
    lda muLnTbLo                // the window's top is the row now at the top
    sta muTop
    lda muLnTbHi
    sta muTop+1
    lda #MU_FOLLOW_CENTER+1     // the opening's own depth was only ever in force
    sta muFollow1               //   while the band still held the opening, and it is
                                //   leaving now: the playhead goes back to the
                                //   centre, one line a pass, so the view glides
    rts                         //   rather than jumping

// --- muHistAdd -- remember the event the walk just took: its row and pitch ---
//  In: X = the voice (muSv), muVRow = the onset row of the event just stepped.
//  The renderer calls this for every event it walks; the keyboard dots read the
//  record back instead of walking the tune again, so a voice's dot sits on the
//  key of the note that is sounding rather than the one that starts next.
muHistAdd:
    ldx muSv
    lda muHistN,x
    and #MU_HIST-1
    sta muTmpL                  // the slot
    inc muHistN,x
    txa
    asl
    asl
    asl
    asl
    asl                         // voice x MU_HIST
    clc
    adc muTmpL
    tay
    lda muVRow,x
    sta muHistRLo,y
    lda muVRowHi,x
    sta muHistRHi,y
    lda muSopt                  // the pitch, for the keyboard dots
    sta muHistOpt,y
    rts

// --- muPlaying -- C=1 and a "? stop first" status when the player is on ---
muPlaying:    lda muPlayOn
    beq muPlNo
    lda #M_STOP_FIRST
    jsr muMsgSet
    jsr muStatus
    sec
    rts
muPlNo:
    clc
    rts

// --- muMusicBye -- leave the player for good and reap the child ---
//  Called before kFreeScreen: the child can still be inside kSidPlay holding the
//  tune pointer, so it must be gone before the surface or the block go.
muMusicBye:
    lda #1
    sta muPlayQuit
    lda #0
    sta muPlayOn
    jsr muSidHushSafe           // never an unguarded hush: a missing sid.lib
                                //   would exit THIS task instead of the child
    ldx muPlaySlot
    cpx #$ff
    beq muMbDone
    lda #MU_MUS_REAP
    sta muReapCnt
muMbWait:
    lda taskState,x
    beq muMbGone                // the child reached SYS_EXIT
    lda #SYS_YIELD
    jsr syscall                 // X survives a yield; A and Y do not
    dec muReapCnt
    bne muMbWait
    ldx muPlaySlot
    lda #SYS_KILL
    jsr syscall                 // it never woke: force it off our code page
muMbGone:
    lda #$ff
    sta muPlaySlot
muMbDone:
    rts

// --- muMusicThread -- the child ---
//  Reached ONLY through spawnPtr. It reads the parent's bytes and writes only
//  muPlayOn/muPlayRefd (to drop a request it cannot honour); the two share this
//  page and never run at once.
muMusicThread:
    lda muPlayQuit
    bne muMtExit
    lda muPlayOn
    beq muMtIdle
    lda #<muSidCfgB               // mode 1: this is the 9-byte config, not the tune
    sta kPtr
    lda #>muSidCfgB
    sta kPtr+1
    lda #MU_MUS_DELAY
    sta kArg                    // ticks per jiffy
    lda #MU_MUS_REPEATS
    sta kArg+1                  // play it once: the end is the end
    lda #0
    sta kArg+2                  // no transpose of our own: kSidPlay adds the
    sta kArg+3                  //   machine's pitch shift to this itself
    lda muPhBaseLo              // the jiffies to play silently first: the row the
    sta kArg+4                  //   parent's playhead is starting on
    lda muPhBaseHi
    sta kArg+5
    lda #1                      // a play attempt is made: kSidHush from the parent
    sta muSidOk                 //   is only safe once sid.lib has loaded
    lda #1                      // mode 1: <base>.wrk paged through windows 3-5. A
    jsr kSidPlay                //   wrong mode reads the config as a tune image
    bcs muMtRefused             // refused: another task owns the SID
    lda muPlayOn
    beq muMusicThread           // we were hushed: the parent asked for the stop
    lda #0                      // the tune ran to its last row: stop here and let
    sta muPlayOn                //   the parent rewind the score
    lda #1
    sta muPlayEnd
    jmp muMusicThread
muMtRefused:
    lda #0
    sta muPlayOn
    lda #1
    sta muPlayRefd
    jmp muMusicThread
muMtIdle:
    lda #SYS_YIELD
    jsr syscall
    jmp muMusicThread
muMtExit:
    lda #SYS_EXIT
    jsr syscall
    jmp muMtExit                // SYS_EXIT never returns; this is the guard

// ==========================================
//  Exit
// ==========================================
muQuit:
    jsr muPlaying               // the pass must not be pulled out from under the
    bcc muQuitChk               //   player: stop first (? stop first)
    rts
muQuitChk:
    lda muDirty                 // unsaved changes warn once; the next Q discards
    beq muQuitGo
    lda muWarn
    bne muQuitGo
    inc muWarn
    lda #M_UNSAVED
    jmp muMsgGo
muQuitGo:
    lda #16                     // drain by CAPACITY: a release folds to 0
    sta muDrain
muQuitDrain:
    jsr kReadKey
    dec muDrain
    bne muQuitDrain
    jsr muMusicBye              // stop and reap the child before its page goes
    lda #0
    sta muAudOn                 // an audition must not outlive the editor
    lda $d404
    and #$fe
    sta $d404
    jsr muScrDrop               // the scratch goes with it: the next load rebuilds it
    lda #MOUSE_DISABLE
    jsr kMouseCall              // stop tracking before the surface goes
    sprOn(0)                    // the pointer goes while we still own the display
    jsr muGiveDisplay
    jsr kFreeScreen
    lda #SYS_EXIT
    jsr syscall
    jmp muQuit                  // never reached

// --- muScrDrop -- remove the two working files, which the next load rebuilds ---
//  Called from the exit path AFTER the music child is reaped: the player pages
//  the same <base>.wrk through its own windows, so it must not be dropped under it.
//  The stack file goes with it -- a session's undo history is that session's, and a
//  file left behind would be a 2 KB tombstone in the directory. A delete is a
//  tombstone, so the space is freed by `compact`, not here; the test on the library
//  matters because kReuDelete's cold stub leaves the task through libFailExit when
//  filesys.lib cannot load.
muScrDrop:
    lda libStartPage+LIB_FILESYS
    cmp #$ff
    beq muSdRts                 // the library never loaded: there are no files to drop
    jsr muSFArg
    jsr kReuDelete
    jsr muUndoArg
    jmp kReuDelete
muSdRts:
    rts

// --- muGiveDisplay -- hand the display back BEFORE releasing our surface ---
//  edit.inc's rule: sysExit's hand-off runs after the task is DEAD, so a dying
//  task with no surface and no parent can blank the screen.
//  The switch is bare, like every other task's: the surface being saved here is
//  ours and kFreeScreen discards it a few instructions later, so a block in it
//  goes with it. The SEI-wrapped kBlinkReset belongs to TAKING the display, where
//  the surface that gets saved is the previous owner's and outlives us.
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
    jsr kSwitchView             // display + kbFocus move to the parent
muGdRts:
    rts

// ==========================================
//  The message file, and the rest of the code-image state
// ==========================================
//  Every printed string lives in ONE file in the REU filesystem, not in here: a
//  character of text in a relocatable task costs a pool byte, and a site that
//  pointed MU_STR at a literal cost twelve code bytes. The file holds fixed
//  MU_MSG_SLOT-byte records, one per M_* index (musedit-msgs.inc, generated from
//  assets/ui/musedit-msgs.txt), so a message's offset is index * 64 and one read
//  brings the whole message in.
muMsgName:  .text "msgs"
            .byte 0
muMsgRec:   .fill 5, 0          // kReuOpen's record: size lo/hi, base lo/mid/hi
muMsgBuf:   .fill MU_MSG_SLOT, 0  // one message, printed before the next is read
// The one string that cannot move: with no file there is nothing to print, not even
// an error, so this is what the editor leaves on the screen.
muSFallback: .text "? no msgs"
            .byte 0
// One letter per note index 1..7 (C D E F G A B), in semitones from C.
muLetterSem: .byte 0, 0, 2, 4, 5, 7, 9, 11
// The accidental, indexed by the option's type bits 6-7 once they are shifted down
// to 0-3: 1 = sharp, 2 = plain, 3 = flat. Entry 0 is the type the format never stores.
muAccMod:   .byte 0, 1, 0, -1
    // The piano's geometry: an octave is 40 pixels, and each semitone's key is
    // where the piano drawing puts it (the black keys are offset on purpose).
    muDotBit:   .byte 32, 64, 128 // the dot slots in the $D015 mask
muDotTab:   // octave x 12 + semitone -> the dot's x LOW byte, in raster
            // coordinates (key centre + 24 for the window's left edge, less the
            // 11 pixels the dot sits inside its shape). The window is 320 wide at
            // raster 24, so its last column is 343 and the dot's own pixels are at
            // x + 10: the top note's 330 is still on screen, and nothing is clamped.
    .byte 17, 19, 22, 25, 27, 33, 36, 39, 42, 45, 48, 50
    .byte 56, 59, 62, 65, 67, 73, 76, 79, 82, 85, 88, 90
    .byte 96, 99, 102, 105, 107, 113, 116, 119, 122, 125, 128, 130
    .byte 136, 139, 142, 145, 147, 153, 156, 159, 162, 165, 168, 170
    .byte 176, 179, 182, 185, 187, 193, 196, 199, 202, 205, 208, 210
    .byte 216, 219, 222, 225, 227, 233, 236, 239, 242, 245, 248, 250
    .byte 0, 3, 6, 9, 11, 17, 20, 23, 26, 29, 32, 34
    .byte 40, 43, 46, 49, 51, 57, 60, 63, 66, 69, 72, 74
muDotTabHi: // ...and its ninth bit, which kSpritePos takes in C
    .byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    .byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    .byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    .byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    .byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    .byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    .byte 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
    .byte 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
.assert "the top octave's dots are not clamped", (muDotTab+95 > muDotTab+90) ? 1 : 0, 1
muLaneCol:  .byte MU_LANE1_COL, MU_LANE1_COL+MU_LANE_STEP, MU_LANE1_COL+2*MU_LANE_STEP
muLaneW:    .byte MU_LANE_W, MU_LANE_W, MU_LANE_W
muVoiceCol: .byte 2, 7, 3       // red, yellow, cyan -- the dot colours, voice 1..3
// A black key is written as the sharp of the letter below it: one letter and a
// sharp flag per semitone, indexed by the semitone inside the octave.
muSemLetter: .byte 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6, 7
muSemSharp:  .byte 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0

// The image a name with no file stands for: load address, the three voice
// lengths, three one-event streams (HLT each) and an empty description.
muNewTune:
    .byte $00,$c0               // load address $C000 (the format ignores it)
    .byte 2,0, 2,0, 2,0         // one HLT each
    .byte $01,$4f               // HLT, voice 1
    .byte $01,$4f               // HLT, voice 2
    .byte $01,$4f               // HLT, voice 3
    .byte 0                     // an empty description
muNewTuneEnd:
.assert "the new-tune image", (muNewTuneEnd - muNewTune) == 15 ? 1 : 0, 1

muFsName:   .fill 12, 0         // <base>.mus in screen codes + NUL
muSFName:   .fill 12, 0         // <base>.wrk, the working image
muTmpName:  .fill 12, 0         // <base>.tmp, the name a replacement is built under
muSFrec:    .fill 5, 0          // its record: size lo/hi, base lo/mid/hi
//  (the state that lives in zero page is declared at the top of this file)
muScrCnt:   .byte 0             // ...and a counter for it
muScrLen:   .word 0             // ...and its length (256 is stored as a high byte of 1)
muDirName:  .fill 12, 0         // kReuDirEntry's name buffer (slot 0 only)
// The powers muNum16 subtracts: five cover every 16-bit value.
muPowLo:    .byte <10000, <1000, <100, <10, <1
muPowHi:    .byte >10000, >1000, >100, >10, >1

// The duration columns, W .. 64th, and the class each one wears. The 64th last: it is
// the shortest, but it is only a length at the tempos whose whole note is a few
// jiffies.
muDurTab:   .byte $08, $0c, $10, $14, $18, $1c, $00   // W H Q 8 16 32 64
// The DUR field's value cells, by class 0-7 (class 1 is UL/UV, drawn apart).
muArmCh1:   .byte '6', 0, 'W', 'H', 'Q', '8', '1', '3'
muArmCh2:   .byte '4', 0, ' ', ' ', ' ', ' ', '6', '2'
// The dot cell by (dur & $a0) >> 5: plain, dotted, -, -, triplet, double-dotted.
//  A triplet is `*` and NOT a digit: it is written straight after the duration, so a
//  `3` there would read as part of the number (`83` for a triplet eighth).
muDotCh:    .byte ' ', '.', 0, 0, '*', ':'
// The same seven, as the row unit's candidates, shortest first.
muUnitTab:  .byte $00, $1c, $18, $14, $10, $0c, $08
muUnitRows: .byte 64, 32, 16, 8, 4, 2, 1
// The bar painter's own bytes: the column, this task's id, and the items
muBarHovP:  .byte 0             // the items the last paint reversed: those cells
muBarActP:  .byte 0             //   carry a swapped background nibble
// The control bar's hit map: one code per column of row 24, tested by column, and
// every code in it has a handler. Hover and the active highlight read the same
// map, so a highlight and a click can never disagree. A blank cell between two
// labels -- and the DUR value and dot cells -- are deliberately 0, so nothing
// answers there.
muBarMap:
    .byte 5,5,5, 0, 0,0,0, 6,6,6      // 0-9    DUR (the value is a readout), TIE
    .byte 0, 4,4,4, 0, 9,9,9, 0, 11   // 10-19  PLY, RST, INS
    .byte 11,11, 0, 10,10,10, 0, 7,7,7 // 20-29 INS, DEL, CMD
    .byte 0, 8,8,8, 0, 3,3,3, 0, 2    // 30-39  UND, SAV, Q
muBarMapEnd:
// A code per column, all 40 of them, and the text fills the row to the Q column --
// so an edit to either has to keep up with the other or the build stops.
// A non-zero code is an ITEM and lies in MU_BAR_Q..MU_BAR_INS: that is the range
// the click chain dispatches and the only range the hover lights. 0 is a blank
// column, and a code outside the range is inert in both paths -- the bar never
// invites a click its dispatch chain would ignore.
.assert "bar map covers the row", (muBarMapEnd - muBarMap) == MU_SCREEN_COLS ? 1 : 0, 1
.assert "bar text fills the row to Q", M_BAR_LEN == 40 ? 1 : 0, 1
// The three lane headers in row 1 are "ROW VOICE 1     VOICE 2     VOICE 3", so
// the lanes start at 4, 16 and 28: a note always sits under its own header.
.assert "voice 1 lane starts under its header", (MU_LANE1_COL == 4) ? 1 : 0, 1
.assert "voice 2 lane starts under its header", (MU_LANE1_COL+MU_LANE_STEP == 16) ? 1 : 0, 1
.assert "voice 3 lane starts under its header", (MU_LANE1_COL+2*MU_LANE_STEP == 28) ? 1 : 0, 1
.assert "the last lane ends inside the row", (MU_LANE1_COL+2*MU_LANE_STEP+MU_LANE_W <= MU_SCREEN_COLS) ? 1 : 0, 1
muSDot:     .text "dot.spr"
    .byte 0
muSPointer: .text "pointer.spr"
            .byte 0
// Two characters per semitone, C- .. B-: the readout appends the octave digit.
muNoteNames: .text "C-C#D-D#E-F-F#G-G#A-A#B-"
// The clamp bounds, x then y: 320 x 200.
muBoundLo:  .byte <320, <200
muBoundHi:  .byte >320, >200

muSlot:     .byte 0
muName:     .fill 9, 0          // base name + NUL (never more than 7 + 1)
muNumOnes:  .byte 0
muNumVal:   .word 0
muPfN:      .byte 0             // lines the follow path walks in one pass
muFollow1:  .byte MU_FOLLOW_CENTER+1  // the line the playhead is pinned to, +1: the
                                //   opening raises it to its own depth (muLnRowDraw)
muHeadBarCol: .byte 0           // where the head's BAR field starts, for the playhead
muHeadBar:  .word 0
muFreeMid:  .byte 0
muFreeHi:   .byte 0
muDotTmp:   .byte 0
muDotC:     .byte 0             // entries left to scan
muDotBase:  .byte 0
muDotXHi:   .byte 0             // 1 = the dot's x is 256 or more
muDotX:     .byte 0
muVDur:     .fill 3, 0          // each voice's duration byte, for the grid
muDurNum:   .byte 64, 0, 1, 2, 4, 8, 16, 32   // by length class: the 64th, a utility, then whole..32nd
muGlyBuf:   .fill 8, 0          // one keyboard cell, expanded to the 8 bytes a blit reads
//  One four-byte field per mnemonic id, so the id indexes the table with a shift by
//  two and a command line prints exactly one field: three characters and a space,
//  except CALL, which fills the field. The order is MU_MN_TEM..MU_MN_AUX; ids 1..9
//  are the structural nine.
muMnTab:    .text "TEM UTL UTV HED DEF CALLTAL HLT END F-C "
            .text "PNT RTP HLD P-S F-S SCA VDP VRT AUT TPS "
            .text "PVD PVR MAX P-W DTN POR ATK DCY SUS REL "
            .text "VOL RES WAV F-M BMP FLT RNG SNC F-X 3-O "
            .text "LFO P&V RUP RDN SRC DST MS# FLG AUX "
muMnEnd:
.assert "the mnemonic table is four bytes per id", (muMnEnd - muMnTab) == 49*4 ? 1 : 0, 1
//  The commands with a fixed command byte, indexed by command byte / 8. A zero is
//  a value of that eight that names no command, or one the grid does not draw. MS#
//  takes four indices, one per value of its high byte; JIF takes none, because its
//  value is the option * 4 plus those two bits.
muDecTab:   .byte MU_MN_TEM, MU_MN_FC,  MU_MN_UTL, MU_MN_MS
            .byte MU_MN_PNT, MU_MN_RTP, MU_MN_HED, 0
            .byte MU_MN_FLG, MU_MN_HLD, MU_MN_PS,  MU_MN_MS
            .byte MU_MN_FS,  MU_MN_SCA, MU_MN_VDP, 0
            .byte MU_MN_VRT, 0,         MU_MN_AUT, MU_MN_MS
            .byte MU_MN_TPS, 0,         MU_MN_AUX, 0
            .byte MU_MN_PVD, 0,         MU_MN_PVR, MU_MN_MS
            .byte MU_MN_MAX, 0,         MU_MN_UTV, 0
//  The eight commands whose option ends in ...011, indexed by option / 8. The
//  index is what the player's own shift chain tests, which is why RNG sits at 2
//  even though its comment at the source reads 0101n011.
muDecSm:    .byte MU_MN_BMP, MU_MN_FLT, MU_MN_RNG, MU_MN_SNC
            .byte MU_MN_FX,  MU_MN_3O,  MU_MN_LFO, MU_MN_PNV
//  Each voice has a block per class of command, so a line never mixes a structural
//  command with a sound one: block = voice * 2 + class, class 0 = structural.
muLogBase:  .byte 0, MU_LOG_SLOTS, 2*MU_LOG_SLOTS
            .byte 3*MU_LOG_SLOTS, 4*MU_LOG_SLOTS, 5*MU_LOG_SLOTS
muJifSt:    .byte 0
muJifT:     .byte 0
muUnitVal:  .byte 0
muUnitBar:  .byte 0
muScanOff:  .word 0
muScanLen:  .word 0
muScanN:    .word 0
muDrain:    .byte 0
muAudStart: .byte 0             // frameCount when it gated on
muHdY:      .byte 0             // its y on the keyboard
// The player's own PAL note table, an octave down (see muAudFreq): semitones C..B.
muAudLoTab: .byte $9C,$C0,$23,$C8,$B4,$EB,$72,$4C,$80,$12,$08,$68
muAudHiTab: .byte $45,$49,$4E,$52,$57,$5C,$62,$68,$6E,$75,$7C,$83
muGdStepN:  .byte 0             // events left to walk on the row being opened
muGdLo:     .fill 2, 0          // ...and the rows those screen lines hold
// --- searching the walk for the command on a line, and for a phrase's partner ---
muFndRow:   .fill 2, 0          // where it was found
muFndLine:  .byte 0             // ...and its line index inside that row's group
muFndVoice: .byte 0             // ...and the voice whose command it is
muCmdK:     .byte 0             // ...and its index in that row's command lines
muObjCmdB:  .byte 0             // the encoded pair, ready to write
// --- the line producer: a time row, or one structural command ---
//  (muLogId and muLogLo are MU_LOGID/MU_LOGLO in the instance buffer; muLogHi is here
//  because the buffer has no room for all three)
muLogHi:    .fill MU_LOG_SLOTS*6, 0  // the row's logged operands' high byte
muDecCnt:   .byte 0             //   muByteDec's output; both a variable, because
muDecBuf:   .fill 3, 0          // kByteToDec's digits
muPendOn:   .fill 3, 0          // that voice has a note on the open group's row
muPendOpt:  .fill 3, 0          // its option byte
muPendDur:  .fill 3, 0          // its duration byte
muPendSp:   .fill 3, 0          // it lasts a whole row or more
muLnScr:    .byte $ff           // the screen row the next line goes at ($ff: discard)
muCurScr:   .byte $ff           // the screen row the cursor glyph is drawn at
muCurX:     .byte 0             // ...and the column, which a hide must blank: the
                                //   voice can change before the hide runs
muModeHov:  .byte 0             // the pointer is on the mode button: its cells are
                                //   drawn reversed
muLnTbLo:   .fill MU_GRID_ROWS, 0   // the row each visible line holds...
muLnTbHi:   .fill MU_GRID_ROWS, 0   //   ...16-bit; $ffff = past the tune's end
muLnKind:   .fill MU_GRID_ROWS, 0   // 1 = the row's own line, 0 = a command's
muLnPh:     .fill MU_GRID_ROWS*3, 0  // each line's phrase body per voice, +1 (0 = the
                                     //   stream); the draw records it, so naming the
                                     //   cursor's row costs a table read, not a walk
muLnPhBase: .byte 0, MU_GRID_ROWS, 2*MU_GRID_ROWS   // where each voice's block starts
muLnBarLo:  .fill MU_GRID_ROWS, 0   // the bar each line's row falls in, from the
muLnBarHi:  .fill MU_GRID_ROWS, 0   //   walk: a tempo change cuts a bar short, so
                                    //   the bar is not a division of the row
muLnTem:    .fill MU_GRID_ROWS, 0   // the tempo in force on each line's row
muCnT0:     .word 0             // the row after the one a write landed on
muLnTick:   .fill MU_GRID_ROWS, 0   // the song tick each line was walked at: the
muLnTickHi: .fill MU_GRID_ROWS, 0   //   playhead's row is the deepest line whose
                                    //   tick the music has reached (muPhSync)
muFoldN:    .byte 0

// --- the grid's state: the window, and each voice's place in the walk ---
// --- the window service. Code-image state: the music child never reads it, and
//     the windows outlive any one tune, so a set is taken once (muWinInit).
muWin:      .fill 12, 0         // the six window pointers, lo/hi interleaved
muWinPg:    .fill 6, 0          // the page each window holds
muWinOk:    .fill 6, 0          // 1 = that window has been filled
muWinOfs:   .word 0             // muWinSeat in: the offset wanted

muTop:      .word 0             // the timeline row at the top of the window
muVRem:     .fill 3,0          // jiffies carried into that voice's next row

// --- the tempo timeline: a row is a function of the song tick ---
//  row(tick) = MU_SEGROW + (tick - MU_SEGTICK) / MU_SEGUNIT, one segment per tempo.
//  A TEM that changes the tempo opens the next segment where it is executed, so
//  the map is continuous at every change and the walk, the head and the playhead
//  all read the same function.
muSegTick:  .word 0             // the tick the current segment began at
muSegRow:   .word 0             // ...and the row that tick is drawn on
muSegBar:   .word 0             // ...and the rows a bar has in it
muBarNo:    .word 1             // the bar the row being opened falls in, 1-based
muBarStart: .word 0             // ...and its first row

muSpan:     .word 0             // rows the current event covers
muJifOut:   .word 0            // the length muStep just consumed

// --- editing state ---
muSekSpan:  .word 0
muSekRow:   .word 0             // the row the recorded event starts on
muSekEnd:   .word 0             // ...and the row it ends on
muLenJ2:    .word 0             // the event's whole length, for a split
muLenJ:     .word 0             // the length being written, in jiffies
muSplitH:   .word 0             // a split's three runs, in this order (muNsRun and
muSplitN:   .word 0             //   muPlRun index them with Y): the rows before
muSplitT:   .word 0             //   the cursor, the new note, and what is left
muNeedLo:   .byte 0             // MU_LEN + MU_INSN
muNeedHi:   .byte 0
muPlainBar: .byte 0
muTrCur:    .word 0             // muTrScan: the pair whose length is being tested...
muTrEnd:    .word 0             // ...and one past the last byte of the three streams
muUnitBarHi: .byte 0

// --- the simulator's state, in the code image so it is per instance ---
muBaseTab:  .byte 0, 5, 10      // the first call-stack frame of each voice
muTkLo:     .fill 3, 0          // the tick that event plays on
muTkHi:     .fill 3, 0
muUtv:      .fill 3, 0
muId:       .fill 3, 0          // the phrase identity, as WorkVoiceForCallPhrase
muHedN:     .fill 3, 0          // HED's repeat count
muHedLo:    .fill 3, 0          // ...and the offset just after it
muHedHi:    .fill 3, 0
muHedId:    .fill 3, 0
muPhLo:     .fill 15, 0         // the call stack, five frames per voice
muPhHi:     .fill 15, 0
muPhSt:     .fill 15, 0
muPhStk:    .fill 15, 0         // ...and the phrase each frame was entered from, +1
muDfLo:     .fill 24, 0         // phrase definitions, as PhraseDef
muDfHi:     .fill 24, 0
muDfId:     .fill 24, $ff       // $ff = no DEF has named this phrase: the defined
                                //   test, because an offset's high byte can be 0
muEvLo:     .fill 3, 0          // events each voice has played
muEvHi:     .fill 3, 0
muUtvC:     .byte 0
muErr:      .byte 1
muErrRow:   .word 0             // ...and the onset row it died on
muSpun:     .byte 0

// --- playback: the parent and its music child share these code bytes ---
muPlayOn:   .byte 0             // 1 = play, 0 = be silent  (parent -- child)
muPlayEnd:  .byte 0             // the child reached the tune's last row
muPlayQuit: .byte 0             // 1 = leave for good       (parent -- child)
muPlaySlot: .byte $ff           // the child's slot, $ff = no child
muPlayRefd: .byte 0             // the child sets this when the SID is busy
muSidOk:    .byte 0             // a play attempt was made (sid.lib may be loaded)
muReapCnt:  .byte 0             // countdown while waiting for the child to exit
muSidCfgB:  .fill 9, 0          // kSidPlay mode 1: the file's FS offset, then windows 3-5
// --- the playhead: the player publishes its own count, the walk rings the dots ---
muPhFrame:  .byte 0             // frameCount at the last playhead update
muPhOld:    .byte 0             // ...the count the pass started with, to tell them apart
muPhBaseLo: .byte 0             // the tick the pass starts at: the row PLAY was
muPhBaseHi: .byte 0             //   pressed on, which the player fast-forwards to
muHistN:    .fill 3, 0          // next slot per voice
//  The row an event sounds on and its pitch, per voice, as the renderer walks:
//  the keyboard dots ask this table instead of re-walking the tune. MU_HIST slots
//  a voice, a power of two so the index is a shift.
muHistOpt:  .fill 3*MU_HIST, 0  // the option byte of each recorded event
muHistRLo:  .fill 3*MU_HIST, 0
muHistRHi:  .fill 3*MU_HIST, 0

// --- the undo stack and the dirty guard ---
muUndoTop:  .word 0             // the next free byte in the stack (the stack top)
muUndoLim:  .word 0             // the redo limit: actions undone are redoable to here
muRecOff:   .word 0             // the record being built/applied: image offset...
muRecOld:   .byte 0, 0          // ...and the old bytes (oldN is 0 or 2)
muRecStart: .word 0             // the record being undone starts here
muInsRows:  .byte 0             // insert time: rows of silence to open...
muInsT0:    .word 0             // ...at this absolute row...
muItJ2:     .byte 0             // ...and the remainder from T0 on
muItN1:     .byte 0             // the events the front, silence...
muItNr:     .byte 0             // ...
muItN2:     .byte 0             // ...and remainder take
muInsNoteV: .byte $ff           // the voice that gets the note ($ff = all rests)

// --- delete rows: the window being taken out, and which pass is walking it ---
muDrT0:     .word 0             // the first row of the window...
muDrEnd:    .word 0             // ...and the row it ends on, per voice
muDrPass:   .byte 0             // 0 = checking every voice, 1 = splicing
muRrSrc:    .word 0             // ...from this offset in the stack
muUndoOk:   .byte 0             // the stack file resolved (0 = edits are not undoable)
muUndoRec:  .fill 5, 0          // kReuOpen's record: size lo/hi, base lo/mid/hi
muUndoName: .text "musedit.und"
muUndoNameEnd:
            .byte 0
// The FS holds a 1-11 character name and the kernel compares at most that many: a
// longer one is stored but never found, so the editor would make a new one every run.
.assert "the stack file's name fits the FS", (muUndoNameEnd - muUndoName) == 11 ? 1 : 0, 1
.assert "the undo stack is whole pages", (MU_UNDO_SIZE / 256) * 256 == MU_UNDO_SIZE ? 1 : 0, 1


