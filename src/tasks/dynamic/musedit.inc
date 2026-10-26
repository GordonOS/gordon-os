// ==========================================
//  musedit.inc -- the body of the .mus tracker editor
// ==========================================
//  Owns the screen, the layout, the eight-octave piano and the key loop.
//
//  Instance data is reached ONLY as `ldy #MU_x` + `(dataPtr),y`; the MU_* names
//  are OFFSETS, never addresses. Everything else (the name copy, the loop
//  counters, the drain counter) lives in the code image, which the loader copies
//  per instance, so it is per-instance RAM.

// --- instance data: offsets into the 53-byte instance buffer ---
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
.const MU_LEN       = 41      // bytes in that image (load address included)
.const MU_FLEN      = 43      // the on-disk size kReuOpen reported
.const MU_FREC      = 45      // 5-byte file record: size lo/hi, base lo/mid/hi
.const MU_STATE_TOP = MU_FREC+4  // the highest state byte: the zero loop starts here

// --- screen layout ---
.const MU_GRID_TOP  = 2
.const MU_GRID_BOT  = 20
.const MU_PIANO_ROWS = 3      // keyboard rows (the glyphs are generated for this many)
.const MU_PIANO_TOP = 21
.const MU_PIANO_BOT = 23
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
.const MU_C_DIM     = 15      // light grey

// --- the simulator ---
.const MU_INIT_TEM  = $90     // sidplay.inc's sidInitSong starts here
.const MU_LEN_MAX   = 100     // the most events one written length may take
.const MU_INIT_UL   = 12      // ...and its WorkUTLValue
.const MU_INIT_UV   = 1       // ...and WorkUTVValues
.const MU_CMD_TEM   = $06     // TEM: the global tempo
.const MU_CMD_UTL   = $16     // UTL: the global utility length
.const MU_CMD_UTV   = $F6     // UTV: this voice's utility length
.const MU_CMD_HED   = $36     // HED: the head of a repeat section
.const MU_OPT_TAL   = $0f     // TAL: back to the head, or the section ends
.const MU_OPT_END   = $2f     // END: return from a phrase
.const MU_OPT_HLT   = $4f     // HLT: this voice stops
.const MU_FRAMES    = 5       // call-stack frames per voice
.const MU_SPIN      = 255     // commands one step may run before it gives up
.const MU_SIM_CAP   = 6000    // events a whole run may execute
.const MU_TRACE_N   = 18      // events the trace key prints, one per grid row
.const MU_GRID_ROWS = 19      // the rows a window shows
.const MU_REST_CH   = '.'     // a rest
.const MU_CMD_CH    = '*'     // the event arrived with commands
.const MU_OFF_CH    = '?'     // its onset fell between rows
.const MU_GRID_STEPS = 3000    // events one redraw may walk
.const MU_ERR_SPIN  = 1       // a run of commands with no note behind it
.const MU_ERR_LEN   = 2       // a length this tempo cannot express
.const MU_ERR_STK   = 3       // the call stack is full
.const MU_ERR_PHR   = 4       // a call to a phrase that was never defined
.const MU_ERR_TAL   = 5       // a TAL whose HED belongs to another phrase
.const MU_UNDO_SIZE = 2048    // the undo stack, in bytes (many small actions)
.const MU_MUS_DELAY = 1       // one player tick per jiffy
.const MU_MUS_REPEATS = 1     // one pass through the tune: the end stops it
.const MU_MUS_REAP  = 60      // slices to wait for the music child to leave
.const MU_POS_BLOCK = $54A2   // the SID player's published per-voice position (sid.asm SID_POS_BLOCK)
.const MU_FOLLOW_CENTER = 8   // rows below the window top the playhead is pinned to
.const MU_PH_SLOW       = 4   // rows per extra tick of the player's own work
.const MU_FOLLOW_WALK   = MU_GRID_ROWS // lines one pass may walk: a whole band, so a slow pass can always re-centre
.const MU_HIST      = 32      // pointer -> row slots per voice (power of two)
.const MU_WIN_PAGES  = 6       // the window service: SIX 256-byte, page-aligned
                               //   windows -- 0-2 are the editor's, one per voice, and
                               //   3-5 are the player's, handed over in kSidPlay's
                               //   config block so the child pages the same file the
                               //   editor edits (see muWinSeat)
.const MU_SCR_PAGES  = 255     // <base>.wrk holds this many 256-byte pages: 65280
                               //   bytes is the whole of a 16-bit file size, and the
                               //   scratch is created once and then reused, because
                               //   kReuDelete is a tombstone that frees nothing

// --- the status line and the control bar ---
.const MU_SCREEN_COLS = 40    // the surface's width, in cells
.const MU_STAT_FRAMES = 50    // about a second of displayed frames
.const MU_DUR_COLS = 7        // the duration columns the timing line prints
.const MU_UNIT_N   = 7        // the row unit's candidates, shortest first
.const MU_BAR_Q     = 2       // hit-map code: this one quits
.const MU_BAR_SAVE  = 3       // ...and this one writes the image back
.const MU_BAR_PLAY  = 4       // ...and this one plays or stops

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
    jsr muSidCheck              // C=1: another task is using sid.lib -- report it
    bcc muSidFree               //   on OUR screen, before any library is touched
    muStr(muSNoPlayer)
    jmp muFatal
muSidFree:
    jsr muParseName             // C=1: no name, or longer than 7 characters
    bcs muBadName
    jsr muNameSetup             // <base>.mus, in screen codes
    jsr muPtrInit               // the pointer, or a refusal when there is no mouse
    jsr muWinInit               // the three read windows, taken once for the session
    bcc muWinTaken
    muStr(muSNoWin)             // no pool room for them: without windows the grid
    jmp muPtrRefuse             //   cannot read the image at all
muWinTaken:
    jsr muLoad                  // the tune, or a refusal when it cannot be had
    bcc muMainGo
    jmp muPtrRefuse             // muLoad left the reason in MU_STR
muMainGo:
    jsr muWinReset              // the windows outlived the last image: forget it
    jsr muSidCfg                // the child's kSidPlay config: the file and its windows
    jsr muFindTempo             // the tempo the first note will hear
    jsr muUnitNow               // the row unit that tempo falls into
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

// --- muNameSetup -- the FS names: the base name with .mus and .wrk on the end ---
//  FS names are SCREEN CODES, so the letters come from character literals under
//  the file's encoding, not from a PETSCII string. A 7-character base reaches
//  exactly the 11 the filesystem allows, so both names fit.
muNameSetup:
    ldx #0
muNsLp:
    cpx muNameLen
    beq muNsExt
    lda muName,x
    sta muFsName,x
    sta muSFName,x
    inx
    bne muNsLp                  // a 7-character name ends the loop, never wraps
muNsExt:
    lda #'.'
    sta muFsName,x
    sta muSFName,x
    lda #'m'
    sta muFsName+1,x
    lda #'w'
    sta muSFName+1,x
    lda #'u'
    sta muFsName+2,x
    lda #'r'
    sta muSFName+2,x
    lda #'s'
    sta muFsName+3,x
    lda #'k'
    sta muSFName+3,x
    lda #0
    sta muFsName+4,x
    sta muSFName+4,x
    rts

// ==========================================
//  The tune image
// ==========================================
//  The whole file -- load address, the three length words, the three streams and
//  the description after them -- lives in one kMalloc block for the task's life:
//  playback hands those very bytes to sid.lib, and a save writes them back
//  unchanged, which is what makes an untouched file round-trip byte for byte.
//  MU_LEN is the image's length; MU_FLEN is what the file on disk holds, which
//  includes the slack a created file carries.

// --- muFsArg -- kArg = the FS name ---
muFsArg:
    lda #<muFsName
    sta kArg
    lda #>muFsName
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
    sta muWinOfs+1
    ldx #0
    jsr muWinSeat
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
//  A window holds a COPY of a page of <base>.wrk. The READ path is here; the
//  write entry lands with the edit path, because a write must also reach the page
//  it came from.
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
    txa
    asl
    tay
    lda muWinOfs                // page-aligned, so the offset's LOW byte IS the
    sta kPtr                    //   address's
    lda muWin+1,y
    sta kPtr+1
    clc
    rts

// --- muWinFill -- voice X's window := the scratch file's page muWinPage ---
//  The working image is <base>.wrk, so a page is one kReuReadAt of 256 bytes and
//  the window is page-aligned, which makes the position `page x 256` -- a high
//  byte and a zero.
muWinFill:
    stx muScrV                  // kReuReadAt clobbers X
    jsr muSFrecArg
    lda #0
    sta kArg+2
    lda muWinPage
    sta kArg+3                  // pos = page x 256
    lda #0
    sta kArg+6
    lda #1
    sta kArg+7                  // len = 256
    ldx muScrV
    txa
    asl
    tay
    lda muWin,y
    sta kPtr
    lda muWin+1,y
    sta kPtr+1
    jsr kReuReadAt
    ldx muScrV                  // the seat still needs the voice
    rts

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
    lda #0
    sta kArg+2
    lda muWinPg,x
    sta kArg+3                  // pos = page x 256
    lda #0
    sta kArg+6
    lda #1
    sta kArg+7                  // len = 256
    txa
    asl
    tay
    lda muWin,y
    sta kPtr
    lda muWin+1,y
    sta kPtr+1
    jsr kReuWriteAt
    ldx muScrV
    rts

// ==========================================
//  The scratch file
// ==========================================
//  The working image is <base>.wrk in the REU filesystem: MU_LEN bytes of tune
//  plus the zeroes that pad it to a whole page. It is created ONCE at its full
//  MU_SCR_PAGES and then reused for ever, because a delete is a tombstone that
//  frees neither the directory slot nor the data -- a create/delete cycle would
//  leak the filesystem a tune at a time. The real <base>.mus is untouched until
//  SAVE, so "quit without saving" stays safe.

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
    muStr(muSNoFs)
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
    muStr(muSNoScr)
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
    lda #0
    sta kArg+2
    lda muScrPg
    sta kArg+3                  // pos = page x 256
    lda #0
    sta kArg+6
    lda #1
    sta kArg+7                  // len = 256
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

// --- muLdCheck -- a file too short to hold even the header is not a tune ---
muLdCheck:
    ldy #MU_LEN+1
    lda (dataPtr),y
    bne muLdGood
    ldy #MU_LEN
    lda (dataPtr),y
    cmp #8
    bcs muLdGood
    muStr(muSNoTune)
    sec
    rts
muLdGood:
    clc
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
    lda #0
    sta kArg+2
    lda muScrPg
    sta kArg+3
    lda #0
    sta kArg+6
    lda #1
    sta kArg+7
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
    muStr(muSNoWrite)
    sec
    rts

muSvCreate:
    //  --- pages for the image; and the room for them BEFORE anything is deleted ---
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
    bcs muSvNoFs
    lda muFreeHi
    bne muSvRoom
    lda muFreeMid
    cmp muNeedPage
    bcc muSvNoRoom
muSvRoom:
    jsr muFsArg
    jsr kReuDelete              // a tombstone frees neither slot nor data, so it costs nothing
    jsr muFsArg
    lda #0
    sta kArg+2
    lda muNeedPage
    sta kArg+3                  // size = pages x 256
    jsr kReuCreate
    bcs muSvCreateBad
    jsr muFsArg
    jsr muFrecPt
    jsr kReuOpen                // it was just made, so this cannot fail
    bcs muSvCreateBad
    lda #0
    sta muTmpL
    lda muNeedPage
    sta muTmpH                  // len = pages x 256
    jsr muSvWrite
    bcs muSvWriteBad
    ldy #MU_FLEN                // the disk holds whole pages now
    lda #0
    sta (dataPtr),y
    ldy #MU_FLEN+1
    lda muNeedPage
    sta (dataPtr),y
    clc
    rts
muSvCreateBad:
    lda kArg+6
    cmp #1
    beq muSvNoRoom
    cmp #2
    beq muSvDirFull
    muStr(muSNoWrite)
    sec
    rts
muSvNoFs:
    muStr(muSNoFs)
    sec
    rts
muSvNoRoom:
    muStr(muSNoRoom)
    sec
    rts
muSvDirFull:
    muStr(muSDirFull)
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
    jsr muSaveNow
    bcc muSvTold
    jmp muStatus                // MU_STR is the reason
muSvTold:
    lda #0                      // a saved tune is clean again
    sta muDirty
    sta muWarn
    muStr(muSSaved)
    jmp muStatus

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
muUnitApply:
    lda muUnitRef
    beq muUaPlain
    cmp #1
    beq muUaHalf
    cmp #2
    beq muUaThird
    //  --- one jiffy per row: the bar is plainU * plainBar rows ---
    lda #1
    sta muUnitVal
    lda #0
    sta muUnitBar
    sta muUnitBarHi
    ldx muPlainBar
    beq muUaRts                 // a bar of no rows never happens
muUaMulLp:
    lda muUnitBar
    clc
    adc muPlainU
    sta muUnitBar
    bcc muUaMulN
    inc muUnitBarHi
muUaMulN:
    dex
    bne muUaMulLp
muUaRts:
    rts
muUaPlain:
    lda muPlainU
    sta muUnitVal
    lda muPlainBar
    sta muUnitBar
    lda #0
    sta muUnitBarHi
    rts
muUaHalf:
    lda muPlainU
    lsr
    sta muUnitVal
    lda muPlainBar
    asl
    sta muUnitBar
    lda #0
    rol
    sta muUnitBarHi
    rts
muUaThird:
    lda muPlainU
    ldx #0
muUaT3Lp:
    cmp #3
    bcc muUaT3D
    sec
    sbc #3
    inx
    jmp muUaT3Lp
muUaT3D:
    stx muUnitVal
    lda muPlainBar
    asl                         // the bar is plainBar * 3, and plainBar is a power
    clc                         //   of two, so the doubled value cannot carry
    adc muPlainBar
    sta muUnitBar
    lda #0
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
    sta muWinOfs+1
    ldx #0
    jsr muWinSeat
    pla
    tax
    rts

// ==========================================
//  The timing line (a development aid)
// ==========================================
// --- muTimingShow -- B: the durations and the row unit for the tempo in force ---
//  Printed in the same form tools/check-mus-timing.py prints them, so the two
//  can be compared line for line: a `-` is a length this tempo cannot express.
muTimingShow:
    lda muTempo
    sta muJifTem                // the tune's tempo, not the simulator's
    jsr muClearRow0
    lda #0
    sta muTimIdx
muTsLp:
    ldx muTimIdx
    lda muDurTagLo,x
    ldy #MU_STRLO
    sta (dataPtr),y
    lda muDurTagHi,x
    ldy #MU_STRHI
    sta (dataPtr),y
    jsr muTextAt                // the tag, at the cursor
    lda #':'
    jsr muPutC
    ldx muTimIdx
    lda muDurTab,x
    jsr muNoteJiffies
    bcs muTsDash
    ldx #0
    jsr muNum16                 // A/X = the length
    jmp muTsNext
muTsDash:
    lda #'-'
    jsr muPutC
muTsNext:
    inc muTimIdx
    lda muTimIdx
    cmp #MU_DUR_COLS
    beq muTsRow1
    lda #' '
    jsr muPutC
    jmp muTsLp
muTsRow1:
    lda #1
    sta muClrRow
    jsr muClearRow
    muSet(MU_CURCOL, 0)
    muSet(MU_CURROW, 1)
    muStr(muSTem)
    jsr muTextAt
    lda muTempo
    jsr muHex8
    muStr(muSTq)
    jsr muTextAt
    lda #$90                    // class 4 (a quarter) + bit 7: the triplet
    jsr muNoteJiffies
    bcs muTsTqDash
    ldx #0
    jsr muNum16
    jmp muTsUnit
muTsTqDash:
    lda #'-'
    jsr muPutC
muTsUnit:
    muStr(muSUnit)
    jsr muTextAt
    jsr muUnit                 // A = the unit, X = the rows a bar has
    sta muUnitVal
    stx muUnitBar
    ldx #0
    jsr muNum16
    muStr(muSRowBar)
    jsr muTextAt
    lda muUnitBar
    ldx #0
    jsr muNum16
    jsr muStatArm
    rts

// --- muHex8 -- print A as two hex digits ---
muHex8:
    pha
    lsr
    lsr
    lsr
    lsr
    jsr muHexDig
    pla
    and #$0f
muHexDig:
    cmp #10
    bcc muHexNum
    adc #6                      // carry is set here: 10..15 -> 'A'..'F'
muHexNum:
    adc #'0'
    jmp muPutC

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
    ldx muSv
    inc muCmdN,x                // this event arrives with a command in front of it
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
    lda #0
    sta muJifOut+1
    sta muCmdN,x                // the commands belong to the event just read
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
    lda muSopt
    lsr
    lsr
    lsr
    lsr
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
    lda muSopt
    lsr
    lsr
    lsr
    lsr
    sta muSph
    jmp muCmCall
muCmD15:
    lda muSopt
    lsr
    lsr
    lsr
    lsr
    sta muSph
    jmp muCmDef

muCmTem:
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
    jmp muSimStop               // the outermost frame: the song ends
muCmEndGo2:
    dec muDep,x
    lda muDep,x
    tay
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
    lda #$ff                    // the sentinel: this frame returns to nothing, and
    sta muPhSt,y                //   $ff is the only value a real frame cannot hold
    inc muDep,x
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
    sec
    rts

// --- muSimRun -- walk the whole tune, up to MU_SIM_CAP events ---
//  Out: C=1 when the cap stopped it rather than the voices stopping.
muSimRun:
    lda #<MU_SIM_CAP
    sta muCapLo
    lda #>MU_SIM_CAP
    sta muCapHi
muRunLp:
    jsr muStep
    bcs muRunEnd
    lda muCapLo
    sec
    sbc #1
    sta muCapLo
    lda muCapHi
    sbc #0
    sta muCapHi
    lda muCapLo
    ora muCapHi
    bne muRunLp
    sec
    rts
muRunEnd:
    clc
    rts

// --- muSimShow -- V: what the walk totals, from tick 0 ---
//  Printed in the form tools/mus-sim.py reports, so the two are compared line
//  for line, and the run stops at MU_SIM_CAP so a looping tune still answers.
muSimShow:
    jsr muClearRow0
    muSet(MU_CURROW, 0)
    muSet(MU_CURCOL, 0)
    muStr(muSSim)
    jsr muTextAt                // the label goes up before the walk, which is a
    jsr muSimInit               // second or two of work on a long tune
    jsr muSimRun
    php
    lda muClkLo
    ldx muClkHi
    jsr muNum16
    muStr(muSSimE)
    jsr muTextAt
    jsr muSimCounts
    muSet(MU_CURROW, 1)
    muSet(MU_CURCOL, 0)
    muStr(muSSimT)
    jsr muTextAt
    lda muSimT
    jsr muHex8
    muStr(muSSimU)
    jsr muTextAt
    lda muUl
    ldx #0
    jsr muNum16
    muStr(muSSimOn)
    jsr muTextAt
    lda muOn
    clc
    adc muOn+1
    adc muOn+2
    ldx #0
    jsr muNum16
    plp
    bcc muShowStopped
    muStr(muSSimCap)
    jsr muTextAt
    jmp muShowFrame
muShowStopped:
    lda muErr
    beq muShowHlt
    pha
    muStr(muSSimErr)
    jsr muTextAt
    pla
    clc
    adc #'0'
    jsr muPutC                  // the code says which stop it was
    jmp muShowFrame
muShowHlt:
    muStr(muSSimHlt)
    jsr muTextAt
muShowFrame:
    jsr muStatArm
    rts

// --- muSimCounts -- the three event counts, separated by '/' ---
//  Unrolled over exactly three voices: muNum16 wants the voice's bytes in A
//  and X, and muPutC owns both, so an index byte would have to be reloaded
//  around every digit anyway.
muSimCounts:
    lda muEvLo
    ldx muEvHi
    jsr muNum16
    lda #'/'
    jsr muPutC
    lda muEvLo+1
    ldx muEvHi+1
    jsr muNum16
    lda #'/'
    jsr muPutC
    lda muEvLo+2
    ldx muEvHi+2
    jmp muNum16

// --- muSimTrace -- X: the first events, in the order the walk plays them ---
//  One row each, in the shape tools/mus-sim.py's --trace prints.
muSimTrace:
    jsr muSimInit
    jsr muUnit                  // the row unit, so the pre-roll's spans match the grid
    sta muUnitVal
    ldx #2
muTrZero:
    lda #0
    sta muVRow,x
    sta muVRowHi,x
    sta muVRem,x
    dex
    bpl muTrZero
    //  Pre-roll to the row the view starts at: the lines then describe the stretch
    //  on screen instead of the tune's first events. The step cap is the grid's,
    //  so a voice that stops early cannot hold the key for ever.
    lda #<MU_GRID_STEPS
    sta muGdSteps
    lda #>MU_GRID_STEPS
    sta muGdSteps+1
muTrPre:
    jsr muStep
    bcs muTrPreEnd
    jsr muGdSpan
    jsr muGdAdv
    ldy #MU_VOICE
    lda (dataPtr),y
    tax
    lda muVRowHi,x
    cmp muTop+1
    bcc muTrPreNext
    bne muTrPreEnd
    lda muVRow,x
    cmp muTop
    bcs muTrPreEnd
muTrPreNext:
    lda muGdSteps
    sec
    sbc #1
    sta muGdSteps
    lda muGdSteps+1
    sbc #0
    sta muGdSteps+1
    lda muGdSteps
    ora muGdSteps+1
    bne muTrPre
muTrPreEnd:
    lda #0
    sta muSidx
muTrLp:
    jsr muStep
    bcc muTrGo
    jmp muTrDone
muTrGo:
    lda muSidx
    clc
    adc #MU_GRID_TOP
    sta muClrRow
    jsr muClearRow
    //  The pair is read out of the voice's window and copied to muTmp2, because
    //  muNum16 borrows muTmpL while it divides down the digits.
    ldx muSv
    lda muPtrLo,x               // the event is the pair behind the pointer, and an
    sec                         //   offset minus 2 is still an offset
    sbc #2
    sta muWinOfs
    sta muAccLo                 // the printed offset, kept because muWinSeat owns muWinOfs
    lda muPtrHi,x
    sbc #0
    sta muWinOfs+1
    sta muAccHi
    jsr muWinSeat               // kPtr = the event's own bytes
    ldy #0
    lda (kPtr),y
    sta muTmp2
    ldy #1
    lda (kPtr),y
    sta muTmp2+1
    //  ...and MU_ACC is already an image offset, which is how tools/mus-sim.py names it
    muStr(muSTrk)
    jsr muTextAt
    lda muClkLo
    ldx muClkHi
    jsr muNum16
    muStr(muSVc)
    jsr muTextAt
    lda muSv
    clc
    adc #'1'
    jsr muPutC
    muStr(muSOff)
    jsr muTextAt
    lda muAccHi
    jsr muHex8
    lda muAccLo
    jsr muHex8
    lda #MU_SPACE
    jsr muPutC
    lda muTmp2
    jsr muHex8
    lda #MU_SPACE
    jsr muPutC
    lda muTmp2+1
    jsr muHex8
    inc muSidx
    lda muSidx
    cmp #MU_TRACE_N
    bcs muTrDone
    jmp muTrLp
muTrDone:
    jsr muStatArm
    rts

// --- muHistShow -- the events the grid walk recorded, for the cursor's voice ---
//  SHIFT+L. One line per recorded event, newest first, as `RRRR OOOO OO`: the
//  row the walk gave the event, its image offset and its option byte. Rows that
//  step by the length the file gives mean the walk read the bytes correctly; a
//  row that jumps far further means a length was decoded wrong, and the offset
//  on that line is the byte to look at; no lines at all means the walk stopped
//  for that voice.
muHistShow:
    jsr muGridClear
    ldy #MU_VOICE
    lda (dataPtr),y
    sta muTmpH
    asl
    asl
    asl
    asl
    asl
    sta muHsBase
    ldx muTmpH
    lda muHistN,x
    beq muHsNone
    sec
    sbc #1
    and #MU_HIST-1
    sta muHsSlot
    lda #MU_TRACE_N
    sta muHsN
    lda #MU_GRID_TOP
    sta muClrRow
muHsLp:
    jsr muClearRow
    lda muHsSlot
    clc
    adc muHsBase
    tay
    lda muHistRLo,y
    sta muHsRow
    lda muHistRHi,y
    sta muHsRow+1
    lda muHistPLo,y
    sta muHsOfs
    lda muHistPHi,y
    sta muHsOfs+1
    lda muHistOpt,y
    sta muHsOpt
    lda muHsRow+1
    jsr muHex8
    lda muHsRow
    jsr muHex8
    lda #MU_SPACE
    jsr muPutC
    lda muHsOfs+1
    jsr muHex8
    lda muHsOfs
    jsr muHex8
    lda #MU_SPACE
    jsr muPutC
    lda muHsOpt
    jsr muHex8
    inc muClrRow
    lda muHsSlot
    sec
    sbc #1
    and #MU_HIST-1
    sta muHsSlot
    dec muHsN
    bne muHsLp
muHsNone:
    jmp muStatArm

// ==========================================
//  Drawing
// ==========================================
muDrawAll:
    lda #MU_C_TEXT
    jsr kSetTextColor
    jsr muDrawHead
    jsr muDrawLanes
    jsr muDrawMode
    jsr muDrawGrid
    jsr muDrawPiano
    jsr muBarPaint
    jsr muCursorShow
    rts

// --- muDrawHead -- the name, the tempo, the bar and the cursor's row ---
//  BAR is the bar the cursor is in and R its row, both counted in the unit the
//  grid is drawn in, which is what makes a scrolled window's position legible.
muDrawHead:
    jsr muClearRow0
    jsr muNameAt
    muStr(muSTem2)
    jsr muTextAt
    lda muTempo
    ldx #0
    jsr muNum16
    ldy #MU_CURCOL              // the tail -- BAR, the bar, R, the row -- starts here,
    lda (dataPtr),y             //   and the playhead rewrites it from here
    sta muHeadBarCol
    muStr(muSBar2)
    jsr muTextAt
    jsr muCursorAbs             // A/X = the cursor's absolute row
    jsr muBarOf                 // A/X = that row's bar, 1-based
    sta muHeadBar
    stx muHeadBar+1
    jsr muNum16
    muStr(muSRow2)
    jsr muTextAt
    ldy #MU_CURCOL              // where the row number goes, for the row update
    lda (dataPtr),y
    sta muHeadRowCol
    jsr muCursorAbs
    jsr muNum16
    muSet(MU_HOV, $ff)          // no pitch yet: three blanks, until a hover says otherwise
    jmp muHovDraw

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
    jmp muNum16

// --- muPhHead -- the head's BAR and R fields follow the playhead ---
//  The row moves every line and the bar once every N lines, so the bar field is
//  rewritten only when its number really changes. Both fields are rewritten
//  together when it does: the row field's column follows the bar's width.
muPhHead:
    jsr muCursorAbs
    jsr muBarOf
    cmp muHeadBar
    bne muPhBar
    cpx muHeadBar+1
    bne muPhBar
    jmp muDrawHeadRow           // the bar has not moved: only the row number
muPhBar:
    sta muHeadBar
    stx muHeadBar+1
    ldy #MU_CURROW
    lda #0
    sta (dataPtr),y
    ldy #MU_CURCOL
    lda muHeadBarCol
    sta (dataPtr),y
    muStr(muSBar2)
    jsr muTextAt
    jsr muCursorAbs
    jsr muBarOf
    jsr muNum16
    muStr(muSRow2)              // R and the row, laid out as the head draw does
    jsr muTextAt
    ldy #MU_CURCOL              // the row field moved with the bar's width
    lda (dataPtr),y
    sta muHeadRowCol
    jsr muCursorAbs
    jmp muNum16

// --- muCursorAbs -- A/X = the cursor's absolute timeline row ---
muCursorAbs:
    ldy #MU_ROW
    lda (dataPtr),y
    sec
    sbc #MU_GRID_TOP
    sta muTmpL
    lda #0
    sta muTmpH
    lda muTop
    clc
    adc muTmpL
    sta muTmpL
    lda muTop+1
    adc muTmpH
    sta muTmpH
    lda muTmpL
    ldx muTmpH
    rts

// --- muBarOf -- A/X = row; out A/X = the bar it falls in, 1-based ---
muBarOf:
    sta kArg
    stx kArg+1
    lda muUnitBar
    sta kArg+2
    lda muUnitBarHi
    sta kArg+3
    jsr kDiv16
    lda kArg+4                  // the quotient (kResult is kArg+4..+7 in a task)
    clc
    adc #1
    sta muTmpL
    lda kArg+5
    adc #0
    sta muTmpH
    lda muTmpL
    ldx muTmpH
    rts

muDrawLanes:
    muAt(0, 1)
    muStr(muSLanes)
    jmp muTextAt

// --- muDrawMode -- OVR or INS at the right of row 1 (outside the scroll area) ---
muDrawMode:
    lda muInsMode
    beq muDmOvr
    lda #'I'
    ldx #37
    ldy #1
    jsr kPrintCharAt
    lda #'N'
    ldx #38
    ldy #1
    jsr kPrintCharAt
    lda #'S'
    ldx #39
    ldy #1
    jsr kPrintCharAt
    rts
muDmOvr:
    lda #'O'
    ldx #37
    ldy #1
    jsr kPrintCharAt
    lda #'V'
    ldx #38
    ldy #1
    jsr kPrintCharAt
    lda #'R'
    ldx #39
    ldy #1
    jsr kPrintCharAt
    rts

// --- muDrawGrid -- the window at MU_TOP: notes, rests, bars and the row numbers ---
//  The row unit is fixed for the whole view, so rows are counted in jiffies
//  whatever a later TEM does; a length that does not divide the unit lands on
//  the row it falls in, marked. Each voice is walked once, and a note's cells
//  run from its onset row to the row its length reaches, which is what makes a
//  lane read as a bar chart.
muDrawGrid:
    lda muGdClip
    bne muDgWalk
    jsr muGridClear
muDgWalk:
    jsr muSimInit
    lda #<MU_GRID_STEPS
    sta muGdSteps
    lda #>MU_GRID_STEPS
    sta muGdSteps+1
    ldx #2
muGdInitLp:
    lda #0
    sta muVRow,x
    sta muVRowHi,x
    sta muVRem,x
    sta muCmdN,x
    dex
    bpl muGdInitLp
muGdLp:
    jsr muStep
    bcs muGdRows
    jsr muGdSpan
    jsr muHistAdd
    jsr muGdEvent
    jsr muGdAdv
    jsr muGdPast
    bcs muGdRows
    lda muGdSteps               // a tune that never passes the window must
    sec                         //   not hold the redraw for ever
    sbc #1
    sta muGdSteps
    lda muGdSteps+1
    sbc #0
    sta muGdSteps+1
    lda muGdSteps
    ora muGdSteps+1
    bne muGdLp
muGdRows:
    jsr muGdFlag
    jmp muGdNums

// --- muGdFlag -- '?' on the row whose length the tempo in force refused ---
//  Drawn only when the walk stopped with MU_ERR_LEN. The row is the refused
//  event's onset, captured at the refusal, and the cell is the voice's first
//  lane column. muPutC owns the window clip, so a flag outside a clipped band
//  costs nothing -- and the flag is re-derived by every redraw, so it clears
//  as soon as the cause goes.
muGdFlag:
    lda muErr
    cmp #MU_ERR_LEN
    bne muGfRts
    lda muErrRow                // screen row = row - muTop + MU_GRID_TOP
    sec
    sbc muTop
    sta muTmpL
    lda muErrRow+1
    sbc muTop+1
    bne muGfRts                 // above the window, or 256+ rows below
    lda muTmpL
    cmp #MU_GRID_ROWS
    bcs muGfRts
    clc
    adc #MU_GRID_TOP
    ldy #MU_CURROW
    sta (dataPtr),y
    ldx muErrV
    lda muLaneCol,x
    ldy #MU_CURCOL
    sta (dataPtr),y
    lda #'?'
    jmp muPutC
muGfRts:
    rts

// --- muRedrawRows -- draw the grid for a band of screen rows only ---
//  In: A = the first screen row, X = one past the last. The walk still runs, but
//  a cell outside the band is not written, so an edit repaints its own rows
//  instead of the whole window.
muRedrawRows:
    sta muGdFrom
    stx muGdTo
    lda #1
    sta muGdClip
    lda muGdFrom
    sta muClrRow
muRrClr:
    jsr muClearRow
    inc muClrRow
    lda muClrRow
    cmp muGdTo
    bcc muRrClr
    jsr muDrawGrid
    rts

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
//  short notes is spread over rows without ever losing a fraction.
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
    sbc muUnitVal
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

// --- muGdPast -- C=1 once every voice that is still playing is past the window ---
muGdPast:
    lda muTop
    clc
    adc #MU_GRID_ROWS
    sta muLimLo
    lda muTop+1
    adc #0
    sta muLimHi
    ldx #0
muGdPLp:
    lda muOn,x
    beq muGdPNext               // a halted voice can produce no more rows
    lda muVRowHi,x
    cmp muLimHi
    bcc muGdPOut
    bne muGdPNext
    lda muVRow,x
    cmp muLimLo
    bcc muGdPOut
muGdPNext:
    inx
    cpx #3
    bcc muGdPLp
    sec
    rts
muGdPOut:
    clc
    rts

// --- muGdEvent -- the cells of the event that just played ---
//  Only the onset row carries anything: a note is its pitch and its duration, and
//  the rows it covers are blank. A '*' in the lane's last cell says the event
//  arrived with commands, a '?' that its onset fell between rows.
muGdEvent:
    ldx muSv
    lda muVRow,x
    sta muGdRowLo
    lda muVRowHi,x
    sta muGdRowHi
    jsr muGdVisible
    bcs muGdEvRts
    jmp muGdCells
muGdEvRts:
    rts

// --- muGdVisible -- C=0 and MU_GDSROW set when MU_GDROW is in the window ---
muGdVisible:
    lda muGdRowHi
    cmp muTop+1
    bcc muGdVNo
    bne muGdVIn
    lda muGdRowLo
    cmp muTop
    bcc muGdVNo
muGdVIn:
    lda muGdRowLo
    sec
    sbc muTop
    sta muTmpL
    lda muGdRowHi
    sbc muTop+1
    bne muGdVNo                 // a window is MU_GRID_ROWS rows: a carry is past it
    lda muTmpL
    cmp #MU_GRID_ROWS
    bcs muGdVNo
    clc
    adc #MU_GRID_TOP
    sta muGdSRow
    clc
    rts
muGdVNo:
    sec
    rts

// --- muGdCells -- the onset row of the current event ---
//  A note reads as its pitch, a space and its duration in the format's own
//  numbers: C#6 16 is a sixteenth, G-4 4 a quarter. A rest is three dots and the
//  same number. Only the onset row is written: the rows the event covers stay
//  blank, so a scroll has nothing to redraw under a sounding note.
muGdCells:
    ldx muSv
    lda muVoiceCol,x            // the lane wears its voice's colour, the same one
    jsr kSetTextColor           //   the dot for that voice uses
    ldx muSv
    lda muLaneCol,x
    sta muGdCol
    jsr muGdAtCell
    lda muSopt
    and #$07
    bne muGdCPitch
    lda #MU_REST_CH
    jsr muPutC
    lda #MU_REST_CH
    jsr muPutC
    lda #MU_REST_CH
    jsr muPutC
    jmp muGdCDur
muGdCPitch:
    jsr muGdPitch
muGdCDur:
    lda #MU_SPACE
    jsr muPutC
    ldx muSv
    lda muVDur,x
    lsr
    lsr
    and #$07                    // the length class, 0-7
    tay
    lda muDurNum,y
    beq muGdCMark               // a utility length has no number
    jsr muNumDur
muGdCMark:
    ldx muSv
    lda muCmdN,x
    beq muGdCOff
    lda #MU_CMD_CH
    jmp muGdCMarkSet
muGdCOff:
    lda muSpan
    ora muSpan+1
    bne muGdCDone               // a whole row or more: nothing is squeezed
    lda #MU_OFF_CH
muGdCMarkSet:
    pha
    ldx muSv
    lda muLaneCol,x
    clc
    adc muLaneW,x
    sec
    sbc #1
    sta muGdCol
    jsr muGdAtCell
    pla
    jsr muPutC
muGdCDone:
    lda #MU_C_TEXT              // everything else on the row is plain white
    jmp kSetTextColor

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

// --- muGdAtCell -- the write cursor goes to (MU_GDCOL, MU_GDSROW) ---
muGdAtCell:
    lda muGdSRow
    ldy #MU_CURROW
    sta (dataPtr),y
    lda muGdCol
    ldy #MU_CURCOL
    sta (dataPtr),y
    rts

// --- muGdPitch -- three cells: the note's name and its octave ---
//  The option byte stores the octave inverted (field = 7 - octave), because the
//  player's table starts at octave 7 and halves per field.
muGdPitch:
    lda muSopt
    and #$07
    tay
    lda muLetterSem,y
    sta muSem
    lda muSopt
    and #$c0
    cmp #$40                    // 01 is a sharp, 11 a flat, 10 plain
    bne muGdPNotSharp
    inc muSem
    jmp muGdPSem
muGdPNotSharp:
    cmp #$c0
    bne muGdPSem
    dec muSem
muGdPSem:
    lda muSem
    asl
    sta muTmp
    ldx muTmp
    lda muNoteNames,x
    jsr muPutC
    ldx muTmp
    lda muNoteNames+1,x
    jsr muPutC
    lda muSopt
    lsr
    lsr
    lsr
    and #$07
    eor #7                      // the field is stored inverted: field = 7 - octave
    clc
    adc #'0'
    jmp muPutC

// --- muGdNums -- the row numbers, counted from each bar's first row ---
//  A bar is 8/16/32/64 rows normally, so two digits cover it; the bar itself is
//  in the head, where the cursor's position is spelled out in full.
muGdNums:
    lda #0                      // the numbers are renumbered for the whole band,
    sta muGdClip                //   so no row clip applies to them
    sta muGdIdx
    lda muTop                   // the top row's number: one divide for the band
    sta kArg
    lda muTop+1
    sta kArg+1
    lda muUnitBar
    sta kArg+2
    lda muUnitBarHi
    sta kArg+3
    jsr kDiv16
    lda kArg+6                  // the remainder: the row's offset in its bar
    clc
    adc #1
    sta muGdNum
muGdNLp:
    lda muGdIdx
    cmp #MU_GRID_ROWS
    bcs muGdNRts
    jsr muGdNum1
    inc muGdIdx
    jmp muGdNLp
muGdNRts:
    lda #0
    sta muGdClip
    rts

// --- muGdNum1 -- the number and separators of one row, muGdIdx = 0..17 ---
//  Numbered from the top row's value and incremented, so a scroll costs one
//  divide for the whole band rather than one per row.
muGdNum1:
    lda muGdIdx
    clc
    adc #MU_GRID_TOP
    sta muClrRow                // the screen row
    lda muClrRow                // the screen row, then the number
    ldy #MU_CURROW
    sta (dataPtr),y
    lda #1
    ldy #MU_CURCOL
    sta (dataPtr),y
    lda muGdNum
    jsr muNum2
muGdNumWrap:
    inc muGdNum                 // the next row's number, wrapped at the bar
    lda muGdNum
    cmp muUnitBar
    bcc muGdNumRts
    beq muGdNumRts
    lda #1
    sta muGdNum
muGdNumRts:
    rts

// --- muDrawPiano -- the generated keyboard rows ---
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
    lda #0
    sta muBarC
muBpLp:
    ldy muBarC
    cpy #38
    bcs muBpKeep
    lda muBarMap,y
    beq muBpNx
    cmp muBarHov
    beq muBpRev
    cmp muBarAct
    beq muBpRev
    cmp muBarHovP               // reversed by the last paint: put its cell back,
    beq muBpNorm                //   background nibble included
    cmp muBarActP
    bne muBpNx
muBpNorm:
    lda muSBar,y
    ldx muBarC
    ldy #MU_BAR_ROW
    jsr kBlitChar
    jmp muBpNx
muBpRev:
    lda muSBar,y                // the character at this column
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

muDrawBar:
    muAt(0, MU_BAR_ROW)
    muStr(muSBar)
    jmp muTextAt

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
    pha                         // the character, while the clip is tested
    lda muGdClip
    beq muPutDraw
    ldy #MU_CURROW
    lda (dataPtr),y
    cmp muGdFrom
    bcc muPutSkip               // above the band: advance, do not draw
    cmp muGdTo
    bcs muPutSkip               // at or below its end
muPutDraw:
    ldy #MU_CURCOL
    lda (dataPtr),y
    tax
    ldy #MU_CURROW
    lda (dataPtr),y
    tay
    pla
    jsr kPrintCharAt
    jmp muPutAdv
muPutSkip:
    pla                         // drop the character: the cell keeps what it has
muPutAdv:
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
    lda #0
    sta muClrRow
muClearRow:
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
    cmp #'b'                    // B: the timing line (a development aid)
    bne muKeyNotB
    jsr muTimingShow
    jmp muKeyIdle
muKeyNotB:
    cmp #'v'                    // V: what the simulated walk totals
    bne muKeyNotV
    jsr muSimShow
    jmp muKeyIdle
muKeyNotV:
    cmp #$58                    // SHIFT+X: the first events, as the walk plays them
    bne muKeyNotX
    jsr muSimTrace
    jmp muKeyIdle
muKeyNotX:
    cmp #$4c                    // SHIFT+L: what the walk recorded for this voice
    bne muKeyNotL
    jsr muHistShow
    jmp muKeyIdle
muKeyNotL:
    cmp #$55                    // SHIFT+U: a finer row unit
    bne muKeyNotU
    jsr muUnitCycle
    jmp muCurRedraw
muKeyNotU:
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
    jsr muKeyNote               // a note key enters its pitch at the cursor
    bcc muKeyNotNote
    jsr muPlaying               // it IS a note key: refuse while the player runs
    bcs muKndIdle
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
    jsr muDelKey
muKndlIdle:
    jmp muKeyIdle
muKeyNotDel:
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
    lda muUnitBar               // the rows a bar has at the grid's tempo
    jsr muInsertTime
muKnibIdle:
    jmp muKeyIdle
muKeyNotInsBar:
    cmp #'m'                    // M: toggle insert/overwrite mode
    bne muKeyNotMode
    lda muInsMode
    eor #1
    sta muInsMode
    jsr muDrawMode
    jmp muKeyIdle
muKeyNotMode:
    cmp #MU_SPACE               // SPACE: toggle play/stop
    bne muKeyNotSpace
    jsr muPlayToggle
    jmp muKeyIdle
muKeyNotSpace:
    cmp #$84                    // CRSR down
    bne muKeyN1
    jmp muCurDown
muKeyN1:
    cmp #$85                    // CRSR up
    bne muKeyN2
    jmp muCurUp
muKeyN2:
    cmp #$86                    // CRSR right: next voice
    bne muKeyN3
    jmp muCurRight
muKeyN3:
    cmp #$87                    // CRSR left: previous voice
    bne muKeyIdle
    jmp muCurLeft
muKeyIdle:
    jsr muPointerTick
    jsr muStatusTick
    jsr muPlayTick
    lda #SYS_YIELD
    jsr syscall
    jmp muKeyLoop

muCurDown:
    ldy #MU_ROW
    lda (dataPtr),y
    cmp #MU_GRID_BOT
    bne muCurDownIn
    //  at the bottom: the window moves instead of the cursor
    inc muTop
    bne muCurRedraw
    inc muTop+1
    jmp muCurRedraw
muCurDownIn:
    jsr muCursorHide
    ldy #MU_ROW
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    jsr muCursorShow
    jsr muDrawHead
    jmp muKeyIdle

muCurUp:
    ldy #MU_ROW
    lda (dataPtr),y
    cmp #MU_GRID_TOP
    bne muCurUpIn
    lda muTop
    ora muTop+1
    beq muCurDone               // already at the tune's first row
    lda muTop
    bne muCurUpDec
    dec muTop+1
muCurUpDec:
    dec muTop
    jmp muCurRedraw
muCurUpIn:
    jsr muCursorHide
    ldy #MU_ROW
    lda (dataPtr),y
    sec
    sbc #1
    sta (dataPtr),y
    jsr muCursorShow
    jsr muDrawHead
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
    lda #0
    sta muGdClip
    jsr muDrawGrid
    jmp muCursorShow

muCurRight:
    ldy #MU_VOICE
    lda (dataPtr),y
    cmp #2
    beq muCurDone
    clc
    adc #1
    sta (dataPtr),y
    jmp muKeyIdle

muCurLeft:
    ldy #MU_VOICE
    lda (dataPtr),y
    beq muCurDone
    sec
    sbc #1
    sta (dataPtr),y
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
    sta muWinOfs+1
    ldx #0
    jsr muWinSeat
    pla
    tax
    rts

// --- muNoteClick -- a piano click enters its pitch on the cursor's row ---
//  The hover has already put the clicked key's linear note in MU_HOV. The armed
//  length is checked against the tempo in force at that row first, so an entry
//  the player could not express writes nothing.
muNoteClick:
    jsr muPlaying               // a mouse click must not edit while playing
    bcc muNcGo
    rts
muNcGo:
    ldy #MU_HOV
    lda (dataPtr),y
    cmp #$ff
    beq muNcRts                 // nothing under the pointer
    cmp #$fe
    beq muNcRts
    jsr muNoteOpt               // A = the note option byte for that key
    jsr muSeek
    lda muSekTem                // the tempo in force at the cursor's row
    sta muJifTem
    lda muDurArm
    jsr muNoteJiffies
    bcs muNcIllegal
    sta muDurJif                // the armed length, for the ones below
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
    lda muSekLo                 // the undo record: 2 bytes at the onset...
    sta muRecOff
    lda muSekHi
    sta muRecOff+1
    lda muSkV
    sta muRecV
    lda #2
    sta muRecOldN
    sta muRecNewN
    jsr muCapOld
    jsr muSeekKPtr
    ldy #0
    lda muDurArm
    sta (kPtr),y
    iny
    lda muNewOpt
    sta (kPtr),y
    jsr muWinFlush0             // the pair reaches its page before the record reads it
    jsr muPushEdit             // ...become 2 new bytes there
    ldy #MU_ROW                 // the pitch is the only thing that moved
    lda (dataPtr),y
    tax
    inx
    jmp muRedrawRows            // tail: its rts balances the jsr that entered
muNcIllegal:
    muStr(muSIllegal)
    jmp muStatus
muNcRts:
    rts


// --- muRowsToJif -- A = rows x the row unit, in jiffies ---
//  In: A/X = rows. Out: A = the jiffies, also left in muValLo; C=1 the value
//  does not fit one byte, so no duration can express it.
muRowsToJif:
    sta muCountLo
    stx muCountHi
    lda #0
    sta muValLo
muRjLp:
    lda muCountLo
    ora muCountHi
    beq muRjDone
    lda muValLo
    clc
    adc muUnitVal
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

// --- muSplitPlan -- the two halves of a split, and how many events they take ---
//  In:  MU_SEKDUR/OPT (the pair under the cursor), MU_SEKROW/TGT (its rows)
//  Out: C=0 muLenJ = J2 (the part from the cursor on), muLenJ2 = J (the whole
//       event), muN1/muN2 = the events each half takes; C=1 the tempo cannot
//       express the event or the two halves.
muSplitPlan:
    lda muSekDur
    jsr muNoteJiffies
    bcc muPlHave
muPlHave:
    sta muLenJ2                 // the event's whole length, in jiffies
    lda #0
    sta muLenJ2+1
    //  --- J1 = the rows before the cursor, times the row unit ---
    lda muTgtLo
    sec
    sbc muSekRow
    sta muCountLo
    lda muTgtHi
    sbc muSekRow+1
    sta muCountHi
    lda muCountLo
    ldx muCountHi
    jsr muRowsToJif
    bcc muPlFix                 // it fits: muValLo holds the jiffies
    lda muLenJ2                 // too long for any length: clamp below
    sec
    sbc #1
    beq muPlOne
    sta muValLo
    jmp muPlFix
muPlOne:
    lda #1
    sta muValLo
muPlFix:
    lda muLenJ2                 // J1 has to leave room for the new note
    sec
    sbc #1
    beq muPlOne2
    sta muLpJ
    lda muValLo
    cmp #1
    bcs muPlHi
    lda #1
    jmp muPlSet
muPlHi:
    cmp muLpJ
    bcc muPlSet
    lda muLpJ
    jmp muPlSet
muPlOne2:
    lda #1
muPlSet:
    sta muLenJ
    lda #0
    sta muLenJ+1
    //  --- how many events each half takes ---
    lda #1
    sta muLenTie                // a same-pitch run is tied, not re-struck
    lda #0
    sta muLenMode               // counting pass: nothing is written yet
    lda muSekOpt
    sta muLenOpt
    jsr muLenEmit
    bcc muPlOne2Ok
muPlOne2Ok:
    sta muN1
    lda muLenJ2                 // J2 = J - J1
    sec
    sbc muLenJ
    sta muLenJ
    lda muLenJ2+1
    sbc muLenJ+1
    sta muLenJ+1
    lda muNewOpt
    sta muLenOpt
    jsr muLenEmit
    bcc muPlTwoOk
muPlTwoOk:
    sta muN2
    clc
    adc muN1
    cmp #MU_LEN_MAX
    bcs muPlRts
    clc
muPlRts:
    rts

// --- muNcSplit -- a note starts at the cursor inside a longer event ---
//  The event is replaced by two runs: the first covers the rows before the cursor
//  on the old pitch, the second starts on the cursor's row on the new one. A run
//  that is not one legal length becomes several tied events, so the two runs
//  together last exactly as long as the event did.
muNcSplit:
    jsr muSeekKPtr
    ldy #0
    lda (kPtr),y
    sta muSekDur                // the pair being replaced, read before it moves
    iny
    lda (kPtr),y
    sta muSekOpt
    lda muSekLo                 // the undo record: 2 bytes at the onset become...
    sta muRecOff
    lda muSekHi
    sta muRecOff+1
    lda muSkV
    sta muRecV
    lda #2
    sta muRecOldN
    jsr muCapOld
    jsr muSplitPlan
    bcc muNsGo
    jmp muNcIllegal
muNsGo:
    lda muN1                    // k-1 pairs open behind the pair being replaced
    clc
    adc muN2
    sec
    sbc #1
    asl
    sta muInsN
    lda muSekLo
    clc
    adc #2
    sta muInsLo
    lda muSekHi
    adc #0
    sta muInsHi
    lda muSkV
    jsr muInsGap
    bcc muNsWrite
    jmp muStatus                // muInsGap left the reason in MU_STR
muNsWrite:
    lda muLenJ
    sta muJ2Lo                  // the plan left J2 in muLenJ; keep it across the run
    lda muLenJ2
    sec
    sbc muJ2Lo
    sta muLenJ
    lda muLenJ2+1
    sbc #0
    sta muLenJ+1
    jsr muSeekKPtr
    lda #1
    sta muLenMode
    lda muSekOpt
    sta muLenOpt
    jsr muLenEmit
    lda muJ2Lo                  // ...and the new note's run, where it stopped
    sta muLenJ
    lda #0
    sta muLenJ+1
    lda muNewOpt
    sta muLenOpt
    jsr muLenEmit
    lda muInsN                  // ...2 + the opened bytes, pushed as one action
    clc
    adc #2
    sta muRecNewN
    jsr muPushEdit
    //  --- only the rows from the cursor to the old event's end changed ---
    lda muSekEnd
    sec
    sbc muTgtLo
    sta muN1                    // the rows the new note occupies
    ldy #MU_ROW
    lda (dataPtr),y
    sta muGdFrom                // the band starts on the cursor's row
    clc
    adc muN1
    sta muGdTo                  // ...and ends where the old event did
    tax                         // X = one past the last row
    lda muGdFrom                // A = the first
    jmp muRedrawRows            // tail: its rts balances the jsr that entered

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
    sta muLenJ
    lda #0
    sta muLenJ+1
    jsr muLenEmit
    bcs muFpRts
    sta muN1
    lda #1
    sta muLenTie                // a note longer than one event ties its own tail
    lda muNewOpt
    sta muLenOpt
    lda muDurJif
    sta muLenJ
    lda #0
    sta muLenJ+1
    jsr muLenEmit
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
muFillNote:
    jsr muFillPlan
    bcc muFgGo
    jmp muNcIllegal
muFgGo:
    lda muN1                    // k pairs at two bytes each, behind the event
    clc
    adc muN2
    asl
    sta muInsN
    lda muSekLo
    clc
    adc #2
    sta muInsLo
    lda muSekHi
    adc #0
    sta muInsHi
    lda muInsLo                 // the undo record: 0 bytes become muInsN, after
    sta muRecOff                //   the event that runs past the row
    lda muInsHi
    sta muRecOff+1
    lda muSkV
    sta muRecV
    lda #0
    sta muRecOldN
    lda muSkV
    jsr muInsGap
    bcc muFgW
    jmp muStatus                // muInsGap left the reason in MU_STR
muFgW:
    jsr muSeekKPtr
    lda muSekLo                 // step over the event that is already there
    clc
    adc #2
    sta muSekLo
    bcc muFgRest
    inc muSekHi
muFgRest:
    lda #1
    sta muLenMode
    lda #0
    sta muLenTie
    sta muLenOpt
    lda muGapJ
    sta muLenJ
    lda #0
    sta muLenJ+1
    jsr muLenEmit               // the rests
    lda #1
    sta muLenTie
    lda muNewOpt
    sta muLenOpt
    lda muDurJif
    sta muLenJ
    lda #0
    sta muLenJ+1
    jsr muLenEmit               // ...and the note
    lda muInsN                  // the opened bytes, pushed as one action
    sta muRecNewN
    jsr muPushEdit
    ldy #MU_ROW                 // rows inserted: redraw from the cursor down
    lda (dataPtr),y
    ldx #MU_GRID_BOT+1
    jmp muRedrawRows

// ==========================================
//  Inserting time -- open rows of silence in all three voices at once
// ==========================================
//  The cursor's row is a shared boundary T0; MU_INSROWS rows of silence open
//  there in every voice, so the three lanes stay in step. Each voice is seeked
//  and spliced in turn -- a splice shifts the later voices' offsets, so the next
//  voice is re-seeked on the grown image -- and the three splices are one grouped
//  undo action. A voice that has stopped before T0 contributes nothing.

// --- muInsertTime -- A = the rows of silence to open at the cursor row ---
//  The I / SHIFT+I entry: every voice gets rests (muInsNoteV = $ff). Insert mode
//  enters at muInsGo with muInsNoteV = the cursor voice and muInsJif = the note.
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
muInsGo:
    lda #1
    sta muInsFirst              // the first splice opens the grouped action
    lda #0
    sta muItV
muItVLoop:
    lda muItV                   // the note voice writes the note, the rest rests
    cmp muInsNoteV
    bne muItVRest
    lda muNewOpt
    sta muInsOpt
    lda #1
    sta muInsTie
    jmp muItVDo
muItVRest:
    lda #0
    sta muInsOpt
    sta muInsTie
muItVDo:
    jsr muInsOneVoice
    inc muItV
    lda muItV
    cmp #3
    bcc muItVLoop
    jmp muRedrawNoHead
muItIllegal:
    muStr(muSIllegal)
    jmp muStatus

// --- muNoteInsMode -- insert mode: open the note in all voices, note in ours ---
//  The cursor voice gets the armed note (muNewOpt), the other two get rests of
//  the same length, so the lanes stay in step and the tune grows from the top.
muNoteInsMode:
    jsr muCursorAbs
    sta muInsT0
    stx muInsT0+1
    lda muDurJif
    sta muInsJif
    ldy #MU_VOICE
    lda (dataPtr),y
    sta muInsNoteV
    jmp muInsGo

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
    lda muSekTem
    sta muJifTem
    jsr muFillPlan              // muGapJ rests, then muN2 note events
    bcc muIvAppFit
    rts
muIvAppFit:
    lda muN1
    clc
    adc muN2
    asl
    sta muInsN
    lda muSekLo
    clc
    adc #2
    sta muInsLo
    lda muSekHi
    adc #0
    sta muInsHi
    lda muInsLo
    sta muRecOff
    lda muInsHi
    sta muRecOff+1
    lda muSkV
    sta muRecV
    lda #0
    sta muRecOldN
    lda muSkV
    jsr muInsGap
    bcs muIvAppRts
    jsr muSeekKPtr
    lda muSekLo                 // step past the event that ran before T0
    clc
    adc #2
    sta muSekLo
    bcc muIvApR
    inc muSekHi
muIvApR:
    lda #1
    sta muLenMode
    lda #0
    sta muLenTie
    sta muLenOpt
    lda muGapJ
    sta muLenJ
    lda #0
    sta muLenJ+1
    jsr muLenEmit               // the rests up to the cursor
    lda #1
    sta muLenTie
    lda muNewOpt
    sta muLenOpt
    lda muDurJif
    sta muLenJ
    lda #0
    sta muLenJ+1
    jsr muLenEmit               // ...and the note
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
    sta muLenJ
    lda #0
    sta muLenJ+1
    jsr muLenEmit
    bcs muIvRts                 // this tempo cannot express the silence: skip
    asl
    sta muInsN                  // the rest events, in bytes
    lda muSekLo
    sta muRecOff
    lda muSekHi
    sta muRecOff+1
    lda muSkV
    sta muRecV
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
    sta muLenJ
    lda #0
    sta muLenJ+1
    jsr muLenEmit               // the inserted content
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
    sta muLenJ
    lda #0
    sta muLenJ+1
    jsr muLenEmit
    bcs muIvsBail
    sta muItN1
    lda #0
    sta muLenTie
    sta muLenOpt
    lda muInsJif
    sta muLenJ
    lda #0
    sta muLenJ+1
    jsr muLenEmit
    bcs muIvsBail
    sta muItNr
    lda muLenJ2                 // J2 = eventJ - J1
    sec
    sbc muItJ1
    sta muItJ2
    lda #1
    sta muLenTie
    lda muSekOpt
    sta muLenOpt
    lda muItJ2
    sta muLenJ
    lda #0
    sta muLenJ+1
    jsr muLenEmit
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
    lda muSekLo
    clc
    adc #2
    sta muInsLo
    lda muSekHi
    adc #0
    sta muInsHi
    lda muSekLo
    sta muRecOff
    lda muSekHi
    sta muRecOff+1
    lda muSkV
    sta muRecV
    lda #2
    sta muRecOldN
    jsr muCapOld                // the old pair, for undo
    lda muSkV
    jsr muInsGap
    bcs muIvsBail
    jsr muSeekKPtr
    lda #1
    sta muLenMode
    lda #1
    sta muLenTie
    lda muSekOpt
    sta muLenOpt
    lda muItJ1
    sta muLenJ
    lda #0
    sta muLenJ+1
    jsr muLenEmit               // the front
    lda muInsTie                // the middle: silence, or the note in insert mode
    sta muLenTie
    lda muInsOpt
    sta muLenOpt
    lda muInsJif
    sta muLenJ
    lda #0
    sta muLenJ+1
    jsr muLenEmit               // the silence
    lda #1
    sta muLenTie
    lda muSekOpt
    sta muLenOpt
    lda muItJ2
    sta muLenJ
    lda #0
    sta muLenJ+1
    jsr muLenEmit               // the remainder, a fresh attack
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
    muStr(muSNoEvent)
    jmp muStatus
muRkGo:
    lda muSekLo                 // the undo record: 2 bytes at the onset
    sta muRecOff
    lda muSekHi
    sta muRecOff+1
    lda muSkV
    sta muRecV
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
    ldy #MU_ROW                 // only the rested row changed
    lda (dataPtr),y
    tax
    inx
    jmp muRedrawRows

// --- muDelKey -- remove the event at the cursor and close the gap ---
//  Only an onset answers, and the HLT pair is never a timed event, so the
//  terminator cannot be deleted and the voice keeps its last pair.
muDelKey:
    jsr muSeek
    lda muSekKind
    beq muDkGo
    muStr(muSNoEvent)
    jmp muStatus
muDkGo:
    lda muSekLo                 // the undo record: 2 bytes at the onset go away
    sta muRecOff
    lda muSekHi
    sta muRecOff+1
    lda muSkV
    sta muRecV
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
    jsr muPushEdit
    ldy #MU_ROW                 // the gap closed: redraw from the cursor down
    lda (dataPtr),y
    ldx #MU_GRID_BOT+1
    jmp muRedrawRows

// ==========================================
//  Undo -- a stack of range-diff records
// ==========================================
//  One record per user action: at MU_RECOFF, MU_RECOLDN image bytes became
//  MU_RECNEWN. The record carries the voice (so the gap op can fix its length
//  word), the old bytes and the new bytes, so undo and redo are each a single
//  range replace -- no multi-step offsets to keep in step as a gap shifts the
//  tail. Records grow up muUndoBuf; a trailing length byte lets undo walk back.
//  Layout: off_lo, off_hi, voice, oldN, newN, old[oldN], new[newN], total.

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
    sta muWinOfs+1
    ldx #0
    jsr muWinSeat
    pla
    tax
    rts

// --- muUndoKPtr -- srcPtr = muUndoBuf + A/X (lo/hi offset) ---
muUndoKPtr:
    clc
    adc #<muUndoBuf
    sta srcPtr
    txa
    adc #>muUndoBuf
    sta srcPtr+1
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
    lda muUndoTop               // srcPtr = muUndoBuf + muUndoTop
    ldx muUndoTop+1
    jsr muUndoKPtr
    ldy #0
    lda muRecOff
    sta (srcPtr),y
    iny
    lda muRecOff+1
    sta (srcPtr),y
    iny
    lda muRecV                  // the voice, with bit 7 = group start
    ldx muRecCont
    bne muPeVCont
    ora #$80
muPeVCont:
    sta (srcPtr),y
    iny
    lda muRecOldN
    sta (srcPtr),y
    iny
    lda muRecNewN
    sta (srcPtr),y
    iny
    ldx #0                      // the old bytes
muPeOld:
    cpx muRecOldN
    bcs muPeOldDone
    lda muRecOld,x
    sta (srcPtr),y
    iny
    inx
    bne muPeOld
muPeOldDone:
    sty muRecTmp                // save the write cursor: muRecKPtr clobbers Y
    jsr muRecKPtr               // the new bytes, from the image -- and this SEAT
    lda muUndoTop               //   destroys srcPtr, so it is derived again here
    ldx muUndoTop+1
    jsr muUndoKPtr
    ldx #0
muPeNew:
    cpx muRecNewN
    bcs muPeNewDone
    txa
    tay
    lda (kPtr),y                // image[off + x]
    ldy muRecTmp
    sta (srcPtr),y
    inc muRecTmp
    inx
    bne muPeNew
muPeNewDone:
    ldy muRecTmp                // the trailing total
    lda muRecTot
    sta (srcPtr),y
    //  advance the top past the record
    lda muUndoTop
    clc
    adc muRecTot
    sta muUndoTop
    lda muUndoTop+1
    adc #0
    sta muUndoTop+1
    lda muUndoTop               // the top is the redo limit until a new edit
    sta muUndoLim
    lda muUndoTop+1
    sta muUndoLim+1
    lda #0                      // the next push starts a new action unless told
    sta muRecCont
    rts

// --- muRecLoad -- read the record whose first byte is at offset A/X ---
//  Sets MU_RECOFF/V/OLDN/NEWN and muRecGrpStart (bit 7 of the voice byte, set on
//  an action's first record). muRecTot = its total length; muRecV is 0..2.
muRecLoad:
    jsr muUndoKPtr
    ldy #0
    lda (srcPtr),y
    sta muRecOff
    iny
    lda (srcPtr),y
    sta muRecOff+1
    iny
    lda (srcPtr),y              // the voice byte carries the group-start flag
    pha
    and #$7f
    sta muRecV
    pla
    and #$80
    sta muRecGrpStart
    iny
    lda (srcPtr),y
    sta muRecOldN
    iny
    lda (srcPtr),y
    sta muRecNewN
    lda muRecOldN
    clc
    adc muRecNewN
    adc #6
    sta muRecTot
    rts

// --- muRangeReplace -- the image at MU_RECOFF: haveN bytes become wantN ---
//  In: muRrHave, muRrWant, muRecV set, and muRrSrc = the wantN source bytes'
//  offset in muUndoBuf. Opens or closes the difference (which clobbers srcPtr),
//  then copies the source in -- so srcPtr is set only after the gap op.
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
    jsr muUndoKPtr              // srcPtr = muUndoBuf + muRrSrc
    ldy #0
muRrLp:
    cpy muRrWant
    bcs muRrDone
    lda (srcPtr),y
    sta (kPtr),y
    iny
    bne muRrLp
muRrDone:
    jmp muWinFlush0            // the pair reaches the page it lives on

// --- muUndo -- Z: take back the newest action (its whole group) ---
muUndo:
    lda muUndoTop
    ora muUndoTop+1
    bne muUzLoop
    muStr(muSNoUndo)
    jmp muStatus
muUzLoop:
    lda muUndoTop               // srcPtr = &buf[top-1]: the trailing length
    sec
    sbc #1
    tay
    lda muUndoTop+1
    sbc #0
    tax
    tya
    jsr muUndoKPtr
    ldy #0
    lda (srcPtr),y
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
    muStr(muSNoRedo)
    jmp muStatus
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
    lda muUndoTop               // the top climbs past the record just redone
    clc
    adc muRecTot
    sta muUndoTop
    lda muUndoTop+1
    adc #0
    sta muUndoTop+1
    lda muUndoTop               // the group ends at the limit...
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

// --- muNoteOpt -- A = the option byte for the linear note in MU_HOV ---
//  The format stores a letter (1..7), an accidental (10 plain, 01 sharp) and an
//  octave, and it stores the octave INVERTED: the player's frequency table starts
//  at octave 7 and is halved once per octave field, so field = 7 - octave. A black
//  key is written as the sharp of the letter below it.
muNoteOpt:
    ldy #MU_HOV
    lda (dataPtr),y
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
    lda muLenCnt
    cmp #1
    beq muLsPlain           // the first event carries no tie
    lda muLenTie
    beq muLsPlain
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
    lda muSekLo                 // on to the next pair
    clc
    adc #2
    sta muSekLo
    bcc muLsRts
    inc muSekHi
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
    muStr(muSNoRoom)
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
//  In: MU_INSLO/HI the offset (an event boundary), MU_INSN the byte count,
//      A = the voice whose length word shrinks. The tail behind the gap -- the
//      rest of this voice, the later voices, the description -- moves down as
//      one piece, so nothing in the image has to be patched.
muDelGap:
    sta muInsV
    lda #0
    sta muGapUp                 // the tail moves DOWN: the pages are walked forwards
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
    lda #0
    sta kArg+2
    lda muScrPg
    sta kArg+3                  // pos = page x 256
    lda #0
    sta kArg+6
    lda #1
    sta kArg+7                  // len = 256
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
//  In: A = the screen code of the key.  Out: C=1 when it is a note key, and
//  MU_HOV then holds the linear note it plays.
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
    ldy #MU_HOV
    sta (dataPtr),y
    sec
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
.const MU_DOT_KEYS  = $e3         // slots 5, 6, 7 (and the pointer's two)

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
    tay
    lda muLetterSem,y
    sta muDotSem
    lda muDotOpt                // 01 sharp, 11 flat, 10 plain
    and #$c0
    cmp #$40
    bne muDvNotSharp
    inc muDotSem
    jmp muDvOct
muDvNotSharp:
    cmp #$c0
    beq muDvFlat
    lda #1                      // no accidental: a white key
    sta muDotNat
    jmp muDvOct
muDvFlat:
    dec muDotSem
muDvOct:
    lda muDotOpt
    lsr
    lsr
    lsr
    and #$07
    eor #7                      // the field is stored inverted: field = 7 - octave
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
    sta muHRow
    cmp #MU_BAR_ROW
    bne muHovNotBar
    jmp muHovBar
muHovNotBar:
    lda #0                      // off the bar: any highlight there ends
    jsr muBarHover
    lda muHRow
    cmp #MU_PIANO_TOP
    bcc muHovOff
    cmp #MU_PIANO_TOP+MU_PIANO_ROWS
    bcs muHovOff
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
// The control bar: the pointer's column picks the item out of the hit map, so a
// highlight can never disagree with what a click there would do.
muHovBar:
    jsr muHovNone               // off the keyboard: its hover and readout end
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
    cmp #MU_BAR_SAVE
    beq muBarSave
    cmp #MU_BAR_PLAY
    beq muBarPlay
    cmp #MU_BAR_Q
    bne muClkNotBarQ
    jmp muQuit                  // out of branch range: invert and jump
muClkNotBarQ:
    muStr(muSNotYet)            // a labelled cell with nothing behind it yet
    jmp muStatus
muBarSave:
    jsr muSaveTry
    rts
muBarPlay:
    jmp muPlayToggle
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
muClkLow:
    cmp #MU_PIANO_TOP            // the piano: the click enters that pitch
    bcc muClkRts
    cmp #MU_PIANO_TOP+MU_PIANO_ROWS
    bcs muClkRts
    jmp muNoteClick
muClkRts:
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
    sta muPhCurLo               // the tune plays from its first row
    sta muPhCurLo+1
    sta muPhAcc
    sta muPhAcc+1
    sta muPhPtrLo               // nothing anchored yet
    sta muPhPtrHi
    lda #$ff
    sta muPhBaseLo
    sta muPhBaseLo+1
    jsr muPlayHome              // the view goes home BEFORE the music starts: a
                                //   redraw must not run while the tune does, or the
                                //   clock begins rows behind and the first pointer
                                //   match is refused as stale
    jsr muPhPer                 // how many ticks a row really takes
    lda #MU_BAR_PLAY
    jsr muBarActive             // the bar shows PLAY before the clock is stamped
    lda #1
    sta muPlayOn                // the music starts here, clock and view ready
    lda tickCount
    sta muPhLast
    rts
muPtNoChild:
    muStr(muSNoPlayer)
    jmp muStatus

// --- muPhPer -- ticks one row really takes: unit jiffies x the player's delay ---
//  A row is the tempo's unit of jiffies, and a jiffy is MU_MUS_DELAY ticks scaled
//  by the machine's speed -- exactly the scaling kSidPlay applies through
//  sidScaleDelay, because a faster machine's jiffies are shorter and the player
//  sleeps MORE ticks per jiffy to keep the tune's tempo in real time. The clock
//  has to scale the same way or the cursor runs ahead of the music on a fast
//  machine and behind it on a slow one.
muPhPer:
    lda #MU_MUS_DELAY
    ldy sysSpeedShift
    beq muPtUnit
    bmi muPtDown
muPtUp:
    asl
    bcs muPtMax
    dey
    bne muPtUp
    beq muPtUnit
muPtMax:
    lda #255
    jmp muPtUnit
muPtDown:
    lsr
    iny
    bne muPtDown
    cmp #1
    bcs muPtUnit
    lda #1
muPtUnit:
    ldy muUnitVal               // the jiffies a row runs for
    bne muPtMul0
    ldy #1
muPtMul0:
    sta muTmp
    sta muPhRowT
    dey
    beq muPtRet
muPtMul:
    lda muPhRowT
    clc
    adc muTmp
    sta muPhRowT
    dey
    bne muPtMul
muPtRet:
    lda muPhRowT                // sidTick's own work is spent on top of the sleep
    sta muPhNeed                //   the player asks for, so every MU_PH_SLOW'th row
    lda #MU_PH_SLOW             //   is one tick longer than delay x unit
    sta muPhFrac
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
//  Reached when the child reports a natural end (muPlayTick) and when a new pass
//  starts (muPtStart): the player is spawned for a single pass and always plays
//  from the tune's first row, so the clock and the view restart with it.
muPlayRewind:
    lda #0
    sta muBarAct                // the pass is over: PLAY goes plain in this redraw
    sta muPhCurLo
    sta muPhCurLo+1
    sta muPhAcc
    sta muPhAcc+1
    jsr muRewindView
    lda tickCount               // the clock restarts from here
    sta muPhLast
    rts

// --- muPlayHome -- take the view home for a pass, unless it is already there ---
//  A window at row 0 with the cursor on the first row is already showing the row 0
//  score -- a fresh editor, or a pass that has just ended -- so PLAY only has to
//  start the music. Anything else (a stop mid-tune, a scrolled window) has to be
//  repainted, which is what makes the notes on screen the notes being played.
muPlayHome:
    lda muTop
    ora muTop+1
    bne muPHomeRedraw           // the window is below the first row: it has to move
    ldy #MU_ROW
    lda (dataPtr),y
    cmp #MU_GRID_TOP
    beq muPHomeRts              // the cursor is home too: nothing to draw at all
    jsr muCursorHide            // only the marker: the window already holds row 0
    muSet(MU_ROW, MU_GRID_TOP)
    jsr muCursorShow
    jmp muDrawHead              // its BAR and R follow the cursor
muPHomeRts:
    rts
muPHomeRedraw:
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
    sta muHistN
    sta muHistN+1
    sta muHistN+2
    sta muPhPtrLo
    sta muPhPtrHi
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
    lda #0                      // nothing is playing: the redraw below drops it
    sta muBarAct
    muStr(muSNoPlayer)
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
    lda #0                      // the child clears muPlayOn itself when the tune
    jsr muBarActive             //   ends, so the highlight is dropped here
    lda muPlayHush               // the cursor stays where the music stopped
    bne muPtRts                 // the second hush already landed
    inc muPlayHush
    jmp muSidHushSafe
muPtRts:
    rts

// --- muPlayhead -- the playhead is the clock ---
//  The tune plays from its first row, and the player consumes one row per note
//  unit of ticks, so the row under the playhead is the ticks since the start
//  divided by that unit. Nothing is looked up and nothing can jump: the cursor
//  walks down one row at a time, and the window follows it a line at a time.
muPlayhead:
    jsr muPhSync                // the row is the player's own song tick / the unit
    jsr muPhFollow
    jmp muPhCursor

// --- muPhSync -- the playhead row, read from the player's own clock ---
//  The player publishes a 16-bit count of the jiffies it has played in this pass, and
//  a grid row is a fixed number of jiffies (muUnitVal), so the row is that count
//  divided by the unit -- the same division the walk makes when it draws a span.
//  Nothing is predicted and nothing is cached, so there is no rate to drift and no
//  window to fall outside of, and a loop restarts the count with the music.
//  Clobbers A, X, Y, kArg and kResult (kArg+4..).
muPhSync:
    lda muTempo                 // the row unit in force NOW, not whatever a previous
    sta muJifTem                //   draw or readout left behind: the divisor and the
    jsr muUnit                  //   grid must agree or the row rate is wrong
    sta muUnitVal
    bne muPsHaveUnit
    lda #1                      // a tempo with no unit: one row per jiffy
muPsHaveUnit:
    sta kArg+2
    lda #0
    sta kArg+3
    lda MU_POS_BLOCK+8          // SID_SONG_TICK is $54A9 = MU_POS_BLOCK + 7, so the
    sta kArg+1                  //   high byte is +8 (NOT +7: +6 is tickCount)
    lda MU_POS_BLOCK+7
    sta kArg
    jsr kDiv16
    lda kArg+4                  // the quotient is the row
    sta muPhCurLo
    lda kArg+5
    sta muPhCurLo+1
muPsRts:
    rts

// --- muPhStep -- add the ticks that went by, one row per `unit` of them ---
muPhStep:
    lda tickCount
    sec
    sbc muPhLast
    beq muPhStRts               // no tick since the last look
    sta muTmpL                  // elapsed ticks
    lda tickCount
    sta muPhLast
    lda muPhAcc
    clc
    adc muTmpL
    sta muPhAcc
    bcc muPhStLp
    inc muPhAcc+1
muPhStLp:
    lda muPhAcc+1
    bne muPhStGo
    lda muPhAcc
    cmp muPhNeed
    bcc muPhStRts
muPhStGo:
    lda muPhAcc
    sec
    sbc muPhNeed
    sta muPhAcc
    lda muPhAcc+1
    sbc #0
    sta muPhAcc+1
    lda muPhRowT                // the next row's length: longer once every
    sta muPhNeed                //   MU_PH_SLOW rows
    dec muPhFrac
    bne muPhStepRow
    lda #MU_PH_SLOW
    sta muPhFrac
    inc muPhNeed
muPhStepRow:
    inc muPhCurLo
    bne muPhStLp
    inc muPhCurLo+1
    jmp muPhStLp
muPhStRts:
    rts


// --- muPhCursor -- the cursor marks the sounding row ---
//  One indicator, not two: the cursor's screen row is derived from the placed
//  voice's row exactly as the grid draws it, and stop leaves it where it is.
//  The cursor's voice wins; once that voice has halted, the first voice still
//  placed is used.
muPhCursor:
    jsr muCursorHide            // the stored row always holds the glyph: take it up
    lda muPhCurLo               // the row the playhead is on
    sec
    sbc muTop
    sta muTmpL
    lda muPhCurLo+1
    sbc muTop+1
    bne muPcGone                // above the window, or 256+ rows below it
    lda muTmpL
    cmp #MU_GRID_ROWS
    bcs muPcGone
    clc
    adc #MU_GRID_TOP
    sta muTmpH
    ldy #MU_ROW
    sta (dataPtr),y
    jsr muCursorShow
    jmp muPhHead                // the bar and the row both follow the playhead
muPcGone:
    lda #MU_GRID_TOP            // out of the window: park it inside so the next
    ldy #MU_ROW                 //   hide lands on a real cell
    sta (dataPtr),y
    rts

// --- muPhFollow -- recentre muTop on the playhead when it leaves the window ---
//  The primary row is the cursor voice's, or the first voice placed. A redraw
//  happens only when the window actually moves, so a settled playhead costs one
//  walk per frame and nothing more.
muPhFollow:
    lda muPhCurLo               // the row the cursor is on
    sta muValLo
    lda muPhCurLo+1
    sta muValHi
    lda muValLo                 // delta = primary - muTop
    sec
    sbc muTop
    sta muTmpL
    lda muValHi
    sbc muTop+1
    bne muPfMove                // negative, or 256+ below: off the window
    lda muTmpL
    cmp #(MU_FOLLOW_CENTER + 1)
    bcc muPfRts                 // on or above the centre line: it stays there
muPfMove:
    lda muValLo                 // newTop = primary - MU_FOLLOW_CENTER, clamped >= 0
    sec
    sbc #MU_FOLLOW_CENTER
    sta muTmpL
    lda muValHi
    sbc #0
    bcs muPfClamped
    lda #0
    sta muTmpL
    sta muValHi                 // reuse muValHi as the new high byte
    beq muPfApply
muPfClamped:
    sta muValHi
muPfApply:
    lda muTmpL                  // no move if the top is already there
    cmp muTop
    bne muPfSet
    lda muValHi
    cmp muTop+1
    beq muPfRts
muPfSet:
    //  The window only ever walks forward, a line at a time: the band shifts up
    //  through the kernel's REU row move and only the line that arrived at the
    //  bottom is drawn. There is no repaint here -- a repaint would re-walk the
    //  tune and cost far more than every line of the band put together -- so a
    //  window that is behind walks several lines in this pass, up to the cap.
    lda muTmpL
    sec
    sbc muTop
    sta muTmpH                  // lines to walk, low byte
    lda muValHi
    sbc muTop+1
    bmi muPfRts                 // behind the window: leave it alone
    bne muPfMax                 // 256+ lines: walk the cap
    lda muTmpH
    beq muPfRts                 // already there
    cmp #MU_FOLLOW_WALK+1
    bcs muPfMax
    sta muPfN
    bne muPfStep
muPfMax:
    lda #MU_FOLLOW_WALK
    sta muPfN
muPfStep:
    lda muTop
    clc
    adc #1
    sta muTop
    lda muTop+1
    adc #0
    sta muTop+1
    jsr muScrollBand
    dec muPfN
    bne muPfStep
muPfRts:
    rts

// --- muScrollBand -- advance the grid one line without repainting it ---
//  kScrollScreen moves the band's pixels and matrix up one line through
//  REU_BOUNCE (the REU does the move) and blanks the line at the bottom; this
//  then draws only that line -- plus the row numbers, which are bar-relative and
//  so change on every scroll. Nothing else in the band is touched.
muScrollBand:
    jsr muCursorHide            // the glyph must not travel with the band
    lda #MU_GRID_TOP
    ldx #MU_GRID_BOT
    jsr kScrollScreen
    lda #MU_GRID_TOP+MU_FOLLOW_CENTER
    ldy #MU_ROW                 // the cursor holds the middle row
    sta (dataPtr),y
    jmp muGdLine                // the line that appeared, lanes and number

// --- muGdLine -- paint one line: the row that just scrolled in ---
//  The walker is not restarted: it stands at the row past the window's bottom, so
//  the line is painted by stepping every voice that is behind it (a window that
//  moved several lines catches up here) and drawing each event's onset -- the
//  only row of an event that carries anything. The clip means the walk cannot
//  write above MU_GRID_BOT, and the row's number goes on last, over the lanes.
muGdLine:
    lda muTop
    clc
    adc #MU_GRID_ROWS-1
    sta muLnRow
    lda muTop+1
    adc #0
    sta muLnRow+1
    lda #1                      // only this line is drawn
    sta muGdClip
    lda #MU_GRID_BOT
    sta muGdFrom
    lda #MU_GRID_BOT+1
    sta muGdTo
    lda #64                     // a bound on the catch-up steps of one line
    sta muGdStepN
    ldx #0
muLnEvLp:
    lda muOn,x
    beq muLnEvNx
    lda muVRowHi,x             // the walker is behind this line: take its events
    cmp muLnRow+1              //   until it reaches it, so a window that moved
    bcc muLnStep               //   more than one line still catches up
    bne muLnEvNx
    lda muVRow,x
    cmp muLnRow
    bcc muLnStep
    beq muLnStep
muLnEvNx:
    inx
    cpx #3
    bcc muLnEvLp
    jmp muLnRts
muLnStep:
    stx muSv
    dec muGdStepN
    beq muLnRts
    jsr muStep                 // the walker's next row
    bcs muLnRts
    jsr muGdSpan
    jsr muHistAdd
    jsr muGdEvent
    jsr muGdAdv
    bcs muLnRts
    ldx muSv
    jmp muLnEvLp
muLnRts:
    lda #0                      // the clip belongs to this line only
    sta muGdClip
    jmp muGdNumLine             // the arriving row's number, last: the lanes paint
                                //   over the number column and it goes on top

// --- muGdNumLine -- the number of the row that just scrolled in ---
//  A row's number belongs to the row, so the band carries it up and only the
//  arriving row needs a number drawn. One divide for the pass.
muGdNumLine:
    lda muLnRow
    sta kArg
    lda muLnRow+1
    sta kArg+1
    lda muUnitBar
    sta kArg+2
    lda muUnitBarHi
    sta kArg+3
    jsr kDiv16
    lda #MU_GRID_BOT
    ldy #MU_CURROW
    sta (dataPtr),y
    lda #1
    ldy #MU_CURCOL
    sta (dataPtr),y
    lda kArg+6
    clc
    adc #1
    jmp muNum2

// --- muHistAdd -- remember the event the walk just took: pointer -> row ---
//  In: X = the voice (muSv), muVRow = the onset row of the event just stepped,
//  muPtr = the pointer past it. The renderer calls this for every event it walks;
//  the playhead reads the record back instead of walking the tune again.
//  WARNING: one entry describes ONE event -- its offset, the row it sounds on and
//  its pitch. muPtr has already moved to the next event by the time the renderer
//  calls this, so the offset is muPtr - 2, the same conversion muSkRecord makes;
//  pairing the next event's offset with this event's row puts every anchor one
//  event early, which a tune of short notes hides and a whole note exposes.
//  The offset is a FILE OFFSET, not an address: the player publishes an offset (it
//  may be reading the tune out of the REU, where it has no address at all), so the
//  two are comparable as they stand.
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
    lda muPtrLo,x               // the event is the pair behind the pointer
    sec
    sbc #2
    sta muHistPLo,y
    lda muPtrHi,x
    sbc #0
    sta muHistPHi,y
    lda muVRow,x
    sta muHistRLo,y
    lda muVRowHi,x
    sta muHistRHi,y
    lda muSopt                  // the pitch, for the keyboard dots
    sta muHistOpt,y
    rts

// --- muHistFind -- C=1 and muValLo/muValHi = the row, for voice muHistV ---
//  A phrase BODY is one offset that the renderer draws once per call, so a single
//  offset maps to MANY rows and "the newest entry" is whichever call was drawn last
//  -- not the one that is playing. Resolving to it pulls the playhead onto another
//  call's row, and the further into a tune full of repeats the music gets, the more
//  occurrences the ring holds and the further the playhead is dragged.
//  So the match is the SMALLEST row at or after the playhead: that is this call,
//  because the calls the player has already made are behind it. When every match is
//  behind (a section loop, or the song starting again) the LARGEST of those is the
//  one the music just wrapped to.
muHistFind:
    lda muHistV
    asl
    asl
    asl
    asl
    asl
    sta muTmpH                  // base = voice x MU_HIST
    lda #0
    sta muHistF                 // nothing at or after the playhead yet
    sta muHistLow               // ...and nothing before it
    ldx muHistV
    lda muHistN,x
    sec
    sbc #1
    and #MU_HIST-1
    sta muHistS
    lda #MU_HIST
    sta muHistC
muHfSkip:
    jmp muHfNx                  // the loop head's branches cannot reach the tail
muHfLp:
    lda muHistS
    clc
    adc muTmpH
    tay
    ldx muHistV
    lda muHistPLo,y
    cmp muPhTgtLo,x
    bne muHfSkip
    lda muHistPHi,y
    cmp muPhTgtHi,x
    bne muHfSkip
    lda muHistRHi,y             // a match: is its row at or after the playhead?
    cmp muPhCurLo+1
    bcc muHfBelow
    bne muHfAbove
    lda muHistRLo,y
    cmp muPhCurLo
    bcc muHfBelow
muHfAbove:
    lda muHistF                 // row >= the playhead: keep the SMALLEST
    bne muHfCmpBest
    lda #1
    sta muHistF
    lda muHistRLo,y
    sta muBestLo
    lda muHistRHi,y
    sta muBestHi
    jmp muHfNx
muHfCmpBest:
    lda muHistRHi,y
    cmp muBestHi
    bcc muHfTake
    bne muHfNx
    lda muHistRLo,y
    cmp muBestLo
    bcs muHfNx
muHfTake:
    lda muHistRLo,y
    sta muBestLo
    lda muHistRHi,y
    sta muBestHi
    jmp muHfNx
muHfBelow:
    lda muHistLow               // row is behind: keep the LARGEST of those
    bne muHfCmpLow
    lda #1
    sta muHistLow
    lda muHistRLo,y
    sta muTmp2
    lda muHistRHi,y
    sta muTmp2+1
    jmp muHfNx
muHfCmpLow:
    lda muHistRHi,y
    cmp muTmp2+1
    bcc muHfNx
    bne muHfLowTake
    lda muHistRLo,y
    cmp muTmp2
    bcc muHfNx
muHfLowTake:
    lda muHistRLo,y
    sta muTmp2
    lda muHistRHi,y
    sta muTmp2+1
muHfNx:
    lda muHistS
    sec
    sbc #1
    and #MU_HIST-1
    sta muHistS
    dec muHistC
    beq muHfDone
    jmp muHfLp                  // out of branch range: the loop body grew
muHfDone:
    lda muHistF
    beq muHfLowOnly
    lda muBestLo
    sta muValLo
    lda muBestHi
    sta muValHi
    sec
    rts
muHfLowOnly:
    lda muHistLow
    beq muHfNone
    lda muTmp2
    sta muValLo
    lda muTmp2+1
    sta muValHi
    sec
    rts
muHfNone:
    clc
    rts

// --- muPlaying -- C=1 and a "? stop first" status when the player is on ---
muPlaying:    lda muPlayOn
    beq muPlNo
    muStr(muSStopFirst)
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
    lda muDirty                 // unsaved changes warn once; the next Q discards
    beq muQuitGo
    lda muWarn
    bne muQuitGo
    inc muWarn
    muStr(muSUnsaved)
    jmp muStatus
muQuitGo:
    lda #16                     // drain by CAPACITY: a release folds to 0
    sta muDrain
muQuitDrain:
    jsr kReadKey
    dec muDrain
    bne muQuitDrain
    jsr muMusicBye              // stop and reap the child before its page goes
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
muSLanes:   .text "ROW VOICE 1     VOICE 2     VOICE 3"
            .byte 0
muSTem2:    .text " TEM "
            .byte 0
muSBar2:    .text " BAR "
            .byte 0
muSRow2:    .text " R"
            .byte 0
// One letter per note index 1..7 (C D E F G A B), in semitones from C.
muLetterSem: .byte 0, 0, 2, 4, 5, 7, 9, 11
    // The piano's geometry: an octave is 40 pixels, and each semitone's key is
    // where the piano drawing puts it (the black keys are offset on purpose).
    muDotBit:   .byte 32, 64, 128 // the dot slots in the $D015 mask
muDotTab:   // octave x 12 + semitone -> the dot's x LOW byte, in raster
            // coordinates (key centre + 24 for the window's left edge, less the
            // 11 pixels the dot sits inside its shape)
    .byte 17, 19, 22, 25, 27, 33, 36, 39, 42, 45, 48, 50
    .byte 56, 59, 62, 65, 67, 73, 76, 79, 82, 85, 88, 90
    .byte 96, 99, 102, 105, 107, 113, 116, 119, 122, 125, 128, 130
    .byte 136, 139, 142, 145, 147, 153, 156, 159, 162, 165, 168, 170
    .byte 176, 179, 182, 185, 187, 193, 196, 199, 202, 205, 208, 210
    .byte 216, 219, 222, 225, 227, 233, 236, 239, 242, 245, 248, 250
    .byte 0, 3, 6, 9, 11, 17, 20, 23, 26, 29, 32, 34
    .byte 40, 43, 46, 49, 51, 54, 54, 54, 54, 54, 54, 54
muDotTabHi: // ...and its ninth bit, which kSpritePos takes in C
    .byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    .byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    .byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    .byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    .byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    .byte 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    .byte 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
    .byte 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
            // (the key's centre + 24 for the display window's left edge, less
            //  the 11 pixels the dot sits inside its 24-wide shape)
    .byte 17, 19, 22, 25, 27, 33, 36, 39, 42, 45, 48, 50
    .byte 56, 59, 62, 65, 67, 73, 76, 79, 82, 85, 88, 90
    .byte 96, 99, 102, 105, 107, 113, 116, 119, 122, 125, 128, 130
    .byte 136, 139, 142, 145, 147, 153, 156, 159, 162, 165, 168, 170
    .byte 176, 179, 182, 185, 187, 193, 196, 199, 202, 205, 208, 210
    .byte 216, 219, 222, 225, 227, 233, 236, 239, 242, 245, 248, 250
    .byte 256, 259, 262, 265, 267, 273, 276, 279, 282, 285, 288, 290
    .byte 296, 299, 302, 305, 307, 310, 310, 310, 310, 310, 310, 310
            // (key centre + 24 for the display window's left edge, less the 12
            //  pixels the dot sits inside its 24-wide shape)
    .byte 17, 18, 21, 24, 26, 32, 35, 38, 41, 44, 47, 49
    .byte 55, 58, 61, 64, 66, 72, 75, 78, 81, 84, 87, 89
    .byte 95, 98, 101, 104, 106, 112, 115, 118, 121, 124, 127, 129
    .byte 135, 138, 141, 144, 146, 152, 155, 158, 161, 164, 167, 169
    .byte 175, 178, 181, 184, 186, 192, 195, 198, 201, 204, 207, 209
    .byte 215, 218, 221, 224, 226, 232, 235, 238, 241, 244, 247, 249
    .byte 255, 258, 261, 264, 266, 272, 275, 278, 281, 284, 287, 289
    .byte 295, 298, 301, 304, 306, 310, 310, 310, 310, 310, 310, 310
muLaneCol:  .byte MU_LANE1_COL, MU_LANE1_COL+MU_LANE_STEP, MU_LANE1_COL+2*MU_LANE_STEP
muLaneW:    .byte MU_LANE_W, MU_LANE_W, MU_LANE_W
muVoiceCol: .byte 2, 7, 3       // red, yellow, cyan -- the dot colours, voice 1..3
muSBar:     .text "DUR 16  TIE-  PLAY  CMD  UNDO  SAVE  Q"
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
muSIllegal: .text "? illegal duration"
            .byte 0
muSNoEvent: .text "? no event here"
            .byte 0
muSNoUndo:  .text "? nothing to undo"
            .byte 0
muSNoRedo:  .text "? nothing to redo"
            .byte 0
muSNoPlayer: .text "? no player"
            .byte 0
muSStopFirst: .text "? stop first"
            .byte 0
muSUnsaved: .text "? unsaved - q again"
            .byte 0
// A black key is written as the sharp of the letter below it: one letter and a
// sharp flag per semitone, indexed by the semitone inside the octave.
muSemLetter: .byte 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6, 7
muSemSharp:  .byte 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0
muSSaved:   .text "? saved"
            .byte 0
muSNoRoom:  .text "? no space"
            .byte 0
muSNoWin:   .text "? no windows"
            .byte 0
muSNoFs:    .text "? no fs"
            .byte 0
muSNoRead:  .text "? unreadable"
            .byte 0
muSNoWrite: .text "? write failed"
            .byte 0
muSNoTune:  .text "? not a tune"
            .byte 0
muSNoScr:   .text "? no scratch"
            .byte 0
muSDirFull: .text "? dir full"
            .byte 0

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
muSFrec:    .fill 5, 0          // its record: size lo/hi, base lo/mid/hi
muScrPg:    .byte 0             // the page a scratch walk is on
muScrCnt:   .byte 0             // ...and a counter for it
muScrV:     .byte 0             // muWinFill/muWinFlush: the voice, across a call
muGapUp:    .byte 0             // muGapMove: 1 = the tail moves up
muScrN:     .byte 0             // ...the image's page count
muScrSh:    .byte 0             // ...the run's start inside the page
muScrLen:   .word 0             // ...and its length (256 is stored as a high byte of 1)
muDstLo:    .byte 0             // ...and where it goes, as an image offset
muDstHi:    .byte 0
muDirName:  .fill 12, 0         // kReuDirEntry's name buffer (slot 0 only)
// The powers muNum16 subtracts: five cover every 16-bit value.
muPowLo:    .byte <10000, <1000, <100, <10, <1
muPowHi:    .byte >10000, >1000, >100, >10, >1

// The duration columns the timing line prints, W .. 64th, and the class each
// one wears. The 64th last: it is the shortest, but it is only a length at the
// tempos whose whole note is a few jiffies.
muDurTab:   .byte $08, $0c, $10, $14, $18, $1c, $00   // W H Q 8 16 32 64
// The DUR field's value cells, by class 0-7 (class 1 is UL/UV, drawn apart).
muArmCh1:   .byte '6', 0, 'W', 'H', 'Q', '8', '1', '3'
muArmCh2:   .byte '4', 0, ' ', ' ', ' ', ' ', '6', '2'
// The dot cell by (dur & $a0) >> 5: plain, dotted, -, -, triplet, double-dotted.
muDotCh:    .byte ' ', '.', 0, 0, '3', ':'
muDurTagLo: .byte <muT0, <muT1, <muT2, <muT3, <muT4, <muT5, <muT6
muDurTagHi: .byte >muT0, >muT1, >muT2, >muT3, >muT4, >muT5, >muT6
muT0:       .text "W"
            .byte 0
muT1:       .text "H"
            .byte 0
muT2:       .text "Q"
            .byte 0
muT3:       .text "8"
            .byte 0
muT4:       .text "16"
            .byte 0
muT5:       .text "32"
            .byte 0
muT6:       .text "64"
            .byte 0
// The same seven, as the row unit's candidates, shortest first.
muUnitTab:  .byte $00, $1c, $18, $14, $10, $0c, $08
muUnitRows: .byte 64, 32, 16, 8, 4, 2, 1
muSTem:     .text "TEM $"
            .byte 0
muSTq:      .text " TQ="
            .byte 0
muSUnit:    .text " UNIT="
            .byte 0
muSRowBar:  .text " ROW/BAR="
            .byte 0
// The simulator's debug pages, in the shape tools/mus-sim.py prints.
muSSim:     .text "SIM T="
            .byte 0
muSSimE:    .text " E="
            .byte 0
muSSimT:    .text "TEM $"
            .byte 0
muSSimU:    .text " UTL="
            .byte 0
muSSimOn:   .text " ON="
            .byte 0
muSSimHlt:  .text " HLT"
            .byte 0
muSSimCap:  .text " CAP"
            .byte 0
muSSimErr:  .text " ERR"
            .byte 0
muSTrk:     .text "t="
            .byte 0
muSVc:      .text " v"
            .byte 0
muSOff:     .text " o=$"
            .byte 0
muBarC:     .byte 0             // the column the bar painter is on
muTid:      .byte 0             // this task's id, for the display-owner test
muBarHov:   .byte 0             // the item under the pointer, 0 = none
muBarAct:   .byte 0             // the item doing something now, 0 = none
muBarHovP:  .byte 0             // the items the last paint reversed: those cells
muBarActP:  .byte 0             //   carry a swapped background nibble
// The control bar's hit map: one code per column of row 24, tested by column.
// The click route acts on 2, 3 and 4; 5-8 name the items that answer "? not yet"
// today. Hover and the active highlight read the same map, so a highlight and a
// click can never disagree. A blank cell between two labels is deliberately 0,
// so nothing answers there.
muBarMap:
    .byte 5,5,5, 0, 0,0,0, 0            // 0-7    DUR (the value is a readout)
    .byte 6,6,6,6, 0,0, 4,4,4,4, 0,0    // 8-19   TIE-, PLAY
    .byte 7,7,7, 0,0, 8,8,8,8, 0,0      // 20-30  CMD, UNDO
    .byte 3,3,3,3, 0,0, 2, 0,0          // 31-39  SAVE, Q
muBarMapEnd:
// A code per column, all 40 of them, and the text ends one column before the
// map's last entry, where Q is -- so an edit to either has to keep up with the
// other or the build stops.
.assert "bar map covers the row", (muBarMapEnd - muBarMap) == MU_SCREEN_COLS ? 1 : 0, 1
.assert "bar text ends at the Q column", (muSBarEnd - muSBar) == 38 ? 1 : 0, 1
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
muNameLen:  .byte 0
muNameIdx:  .byte 0
muGRow:     .byte 0
muPRow:     .byte 0
muPCell:    .byte 0
muPCol:     .byte 0
muNumOnes:  .byte 0
muNumVal:   .word 0
muNumDig:   .byte 0
muNumPow:   .byte 0
muNumSquash: .byte 0
muTmpL:     .byte 0
muTmpH:     .byte 0
muPfN:      .byte 0             // lines the follow path walks in one pass
muHeadRowCol: .byte 0           // the head's row number, for the row-only update
muHeadBarCol: .byte 0           // where the head's BAR field starts, for the playhead
muHeadBar:  .word 0
muPhPtrLo:  .byte 0             // the cursor voice's published pointer, last pass
muPhPtrHi:  .byte 0
muPhNeed:   .byte 1             // the current row's length in ticks
muPhFrac:   .byte 1             // rows left before that length grows by one
muPhRowT:   .byte 1             // ticks one row takes, machine speed included
muPhCand:   .fill 2, 0          // the row the last rejected rewind named
muNeedPage: .byte 0
muFreeMid:  .byte 0
muFreeHi:   .byte 0
muClrRow:   .byte 0
muTempo:    .byte 0             // the tempo in force at the first note
muDotNat:   .byte 0             // 1 = the note is a white key
muDotTmp:   .byte 0
muDotS:     .byte 0             // the ring slot being read
muDotC:     .byte 0             // entries left to scan
muDotBase:  .byte 0
muDotMask:  .byte 0             // the enable mask the dots last asked for
muDotV:     .byte 0
muDotOpt:   .byte 0
muDotSem:   .byte 0
muDotXHi:   .byte 0             // 1 = the dot's x is 256 or more
muDotX:     .byte 0
muVDur:     .fill 3, 0          // each voice's duration byte, for the grid
muDurNum:   .byte 64, 0, 1, 2, 4, 8, 16, 32   // by length class: the 64th, a utility, then whole..32nd
muDurByte:  .byte 0
muJifCls:   .byte 0
muJifSt:    .byte 0
muJifBase:  .byte 0
muJifT:     .byte 0
muTimIdx:   .byte 0
muUnIdx:    .byte 0
muUnitVal:  .byte 0
muUnitBar:  .byte 0
muScanV:    .byte 0
muScanOff:  .word 0
muScanLen:  .word 0
muScanN:    .word 0
muDrain:    .byte 0
muStatT:    .byte 0             // displayed frames left before the head returns
muStatF:    .byte 0             // frameCount at the last counted frame
muGdClip:   .byte 0             // 1 while only MU_GDFROM..MU_GDTO is drawn
muGdStepN:  .byte 0             // catch-up steps left on the line being drawn
muGdFrom:   .byte 0             // the band, in screen rows (the end exclusive)
muGdTo:     .byte 0
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

// --- the grid's state: the window, and each voice's place in the walk ---
// --- the window service. Code-image state: the music child never reads it, and
//     the windows outlive any one tune, so a set is taken once (muWinInit).
muWin:      .fill 12, 0         // the six window pointers, lo/hi interleaved
muWinPg:    .fill 6, 0          // the page each window holds
muWinOk:    .fill 6, 0          // 1 = that window has been filled
muWinOfs:   .word 0             // muWinSeat in: the offset wanted
muWinPage:  .byte 0             // its page, while the seat works

muTop:      .word 0             // the timeline row at the top of the window
muVRow:     .fill 3,0          // each voice's next event row
muVRowHi:   .fill 3,0
muVRem:     .fill 3,0          // jiffies carried into that voice's next row
muCmdN:     .fill 3,0          // commands seen since that voice's last note
muSpan:     .word 0            // rows the current event covers
muJifOut:   .word 0            // the length muStep just consumed
muGdRowLo:  .byte 0
muGdRowHi:  .byte 0
muGdSRow:   .byte 0
muGdCol:    .byte 0
muGdEnd:    .byte 0
muGdFirst:  .byte 0
muGdIdx:    .byte 0
muGdSteps:  .word 0

// --- editing state ---
muDurArm:   .byte 0             // the length a new note is written with
muDurJif:   .byte 0             // ...and that length in jiffies, at the cursor's tempo
muGapJ:     .byte 0             // the gap a fill has to cross, in jiffies
muKbdOct:   .byte 0             // the octave the note keys play in
muNewOpt:   .byte 0             // the option byte being written
muTgtLo:    .byte 0             // the row the cursor is on
muTgtHi:    .byte 0
muSekKind:  .byte 0             // 0 onset, 1 inside, 2 past the end
muSekLo:    .byte 0             // that event's offset in the image
muSekHi:    .byte 0
muSekSpan:  .word 0
muSekRow:   .word 0             // the row the recorded event starts on
muSekEnd:   .word 0             // ...and the row it ends on
muSekDur:   .byte 0             // its duration byte, and
muSekOpt:   .byte 0             // its option byte
muLenJ2:    .word 0             // the event's whole length, for a split
muN1:       .byte 0             // the events a split writes before the cursor
muN2:       .byte 0             // ...and from the cursor on
muSekTem:   .byte 0             // the tempo in force at the recorded event
muPrtLo:    .byte 0             // a recorded event's pointer, mid-computation
muPrtHi:    .byte 0
muJ2Lo:     .byte 0             // a split's second half, kept across the first run
muSkV:      .byte 0
muGdNum:    .byte 0
muValLo:    .byte 0
muValHi:    .byte 0
muLenJ:     .word 0             // the length being written, in jiffies
muLenOpt:   .byte 0             // its option byte
muLenTie:   .byte 0             // 1: tie every event after the first
muLenMode:  .byte 0             // 0 count, 1 write
muLenCnt:   .byte 0
muLpIdx:    .byte 0
muLpJ:      .byte 0
muLpDur:    .byte 0
muInsLo:    .byte 0             // where a gap opens, as an image offset
muInsHi:    .byte 0
muInsN:     .byte 0             // how many bytes
muInsV:     .byte 0             // the voice whose length word grows
muNeedLo:   .byte 0             // MU_LEN + MU_INSN
muNeedHi:   .byte 0
muCountLo:  .byte 0             // a byte count, 16 bits
muCountHi:  .byte 0
muLimLo:    .byte 0
muLimHi:    .byte 0
muPlainU:   .byte 0
muPlainBar: .byte 0
muUnitRef:  .byte 0             // 0 plain, 1 half, 2 third, 3 one jiffy
muUnitBarHi: .byte 0

// --- the simulator's state, in the code image so it is per instance ---
muBaseTab:  .byte 0, 5, 10      // the first call-stack frame of each voice
muPtrLo:    .fill 3, 0          // where this voice's next event is, as a FILE OFFSET
muPtrHi:    .fill 3, 0
muTkLo:     .fill 3, 0          // the tick that event plays on
muTkHi:     .fill 3, 0
muOn:       .fill 3, 0          // a HLT clears this
muUtv:      .fill 3, 0
muId:       .fill 3, 0          // the phrase identity, as WorkVoiceForCallPhrase
muHedN:     .fill 3, 0          // HED's repeat count
muHedLo:    .fill 3, 0          // ...and the offset just after it
muHedHi:    .fill 3, 0
muHedId:    .fill 3, 0
muDep:      .fill 3, 0          // the next free call-stack frame
muPhLo:     .fill 15, 0         // the call stack, five frames per voice
muPhHi:     .fill 15, 0
muPhSt:     .fill 15, 0
muDfLo:     .fill 24, 0         // phrase definitions, as PhraseDef
muDfHi:     .fill 24, 0
muDfId:     .fill 24, $ff       // $ff = no DEF has named this phrase: the defined
                                //   test, because an offset's high byte can be 0
muEvLo:     .fill 3, 0          // events each voice has played
muEvHi:     .fill 3, 0
muClkLo:    .byte 0             // the tick of the latest event
muClkHi:    .byte 0
muSimT:     .byte 0             // the tempo the walk is in
muJifTem:   .byte 0             // ...and the copy muNoteJiffies reads
muUl:       .byte 0
muUtvC:     .byte 0
muErr:      .byte 1
muErrV:     .byte 0             // the voice whose length the tempo refused...
muErrRow:   .word 0             // ...and the onset row it died on
muSv:       .byte 0
muSidx:     .byte 0
muScmd:     .byte 0
muSopt:     .byte 0
muSph:      .byte 0
muSpun:     .byte 0
muAccLo:    .byte 0
muAccHi:    .byte 0
muBestLo:   .byte 0
muBestHi:   .byte 0
muCapLo:    .byte 0
muCapHi:    .byte 0
muTmp2:     .byte 0, 0

// --- playback: the parent and its music child share these code bytes ---
muPlayOn:   .byte 0             // 1 = play, 0 = be silent  (parent -- child)
muPlayEnd:  .byte 0             // the child reached the tune's last row
muPlayQuit: .byte 0             // 1 = leave for good       (parent -- child)
muPlaySlot: .byte $ff           // the child's slot, $ff = no child
muPlayHush: .byte 0             // the stop second-hush guard
muPlayRefd: .byte 0             // the child sets this when the SID is busy
muSidOk:    .byte 0             // a play attempt was made (sid.lib may be loaded)
muReapCnt:  .byte 0             // countdown while waiting for the child to exit
muSidCfgB:  .fill 9, 0          // kSidPlay mode 1: the file's FS offset, then windows 3-5
// --- the playhead: the walk maps each voice's published pointer to a row ---
muPhFrame:  .byte 0             // frameCount at the last playhead update
muPhTgtLo:  .fill 3, 0          // each voice's published next-command pointer
muPhTgtHi:  .fill 3, 0
muPhRowLo:  .fill 3, 0          // the timeline row the walk matched the pointer on
muPhRowHi:  .fill 3, 0
muPhHave:   .fill 3, 0          // 1 = that voice's pointer was matched this frame
muPhCurLo:  .fill 2, 0          // the row the cursor is on while playing (lo, hi)
muPhBaseLo: .fill 2, 0          // the onset row of the event it is anchored to
muPhAcc:    .fill 2, 0          // ticks accumulated towards the next row
muPhLast:   .byte 0             // tickCount at the last look
muHistN:    .fill 3, 0          // next slot per voice
muHistV:    .byte 0             // the voice a lookup is for
muHistS:    .byte 0             // its slot cursor
muHistC:    .byte 0             // its scan countdown
muHistF:    .byte 0             // muHistFind: a match at or after the playhead
muHistLow:  .byte 0             // ...and one behind it
muHsBase:   .byte 0             // muHistShow: voice x MU_HIST
muHsSlot:   .byte 0             // its slot cursor
muHsN:      .byte 0             // its line countdown
muHsRow:    .fill 2, 0          // the row of the line being printed
muHsOfs:    .fill 2, 0          // ...and its image offset
muHsOpt:    .byte 0             // ...and its option byte
muLnRow:    .word 0             // the timeline row the bottom line shows
//  pointer -> row, per voice, as the renderer walks: the playhead asks this
//  table instead of re-walking the tune every frame. MU_HIST slots a voice,
//  a power of two so the index is a shift.
muHistPLo:  .fill 3*MU_HIST, 0
muHistPHi:  .fill 3*MU_HIST, 0
muHistOpt:  .fill 3*MU_HIST, 0  // the option byte of each recorded event
muHistRLo:  .fill 3*MU_HIST, 0
muHistRHi:  .fill 3*MU_HIST, 0
muFollow:   .byte 1             // the window follows the playhead, always

// --- the undo stack and the dirty guard ---
muDirty:    .byte 0             // an image byte changed since the last save
muWarn:     .byte 0             // Q warned once; the next Q discards
muUndoTop:  .word 0             // the next free byte in muUndoBuf (the stack top)
muUndoLim:  .word 0             // the redo limit: actions undone are redoable to here
muRecOff:   .word 0             // the record being built/applied: image offset...
muRecV:     .byte 0             // ...voice...
muRecOldN:  .byte 0             // ...bytes it replaces...
muRecNewN:  .byte 0             // ...bytes it writes...
muRecOld:   .byte 0, 0          // ...and the old bytes (oldN is 0 or 2)
muRecTot:   .byte 0             // the record's total length
muRecTmp:   .byte 0             // a write cursor while a record is built
muRecStart: .word 0             // the record being undone starts here
muRecCont:  .byte 0             // 1 = the next push appends to the action in flight
muRecGrpStart: .byte 0          // bit 7 of the loaded record's voice byte
muInsRows:  .byte 0             // insert time: rows of silence to open...
muInsT0:    .word 0             // ...at this absolute row...
muInsJif:   .byte 0             // ...which is this many jiffies...
muItV:      .byte 0             // ...spliced into this voice...
muInsFirst: .byte 0             // ...the first splice opens the grouped action
muItJ1:     .byte 0             // straddle: the front jiffies before T0
muItJ2:     .byte 0             // ...and the remainder from T0 on
muItN1:     .byte 0             // the events the front, silence...
muItNr:     .byte 0             // ...
muItN2:     .byte 0             // ...and remainder take
muInsOpt:   .byte 0             // the opt the splice writes (0 rest, note in INS)
muInsTie:   .byte 0             // ...and whether its run ties
muInsNoteV: .byte $ff           // the voice that gets the note ($ff = all rests)
muInsMode:  .byte 0             // 0 overwrite, 1 insert (note entry pushes down)
muRrHave:   .byte 0             // range replace: bytes there now...
muRrWant:   .byte 0             // ...and bytes wanted
muRrSrc:    .word 0             // ...from this offset in muUndoBuf
muUndoBuf:  .fill MU_UNDO_SIZE, 0


