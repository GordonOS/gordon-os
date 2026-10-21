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
.const MU_IMG       = 41      // the kMalloc block holding the whole file
.const MU_LEN       = 43      // bytes in that image (load address included)
.const MU_FLEN      = 45      // the on-disk size kReuOpen reported
.const MU_PAGES     = 47      // pages the block has, from kMalloc's kArg+4 echo
.const MU_FREC      = 48      // 5-byte file record: size lo/hi, base lo/mid/hi
.const MU_STATE_TOP = MU_FREC+4  // the highest state byte: the zero loop starts here

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
.const MU_GRID_ROWS = 18      // the rows a window shows
.const MU_BAR_CH    = '='     // a note's length, one cell per row
.const MU_REST_CH   = '.'     // a rest
.const MU_CMD_CH    = '*'     // the event arrived with commands
.const MU_OFF_CH    = '?'     // its onset fell between rows
.const MU_GRID_STEPS = 3000    // events one redraw may walk
.const MU_ERR_SPIN  = 1       // a run of commands with no note behind it
.const MU_ERR_LEN   = 2       // a length this tempo cannot express
.const MU_ERR_STK   = 3       // the call stack is full
.const MU_ERR_PHR   = 4       // a call to a phrase that was never defined
.const MU_ERR_TAL   = 5       // a TAL whose HED belongs to another phrase

// --- the status line and the control bar ---
.const MU_SCREEN_COLS = 40    // the surface's width, in cells
.const MU_STAT_FRAMES = 50    // about a second of displayed frames
.const MU_DUR_COLS = 7        // the duration columns the timing line prints
.const MU_UNIT_N   = 7        // the row unit's candidates, shortest first
.const MU_BAR_TODO  = 1       // hit-map code: nothing is behind that cell yet
.const MU_BAR_Q     = 2       // ...and this one quits
.const MU_BAR_SAVE  = 3       // ...and this one writes the image back

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
    jsr muNameSetup             // <base>.mus, in screen codes
    jsr muPtrInit               // the pointer, or a refusal when there is no mouse
    jsr muLoad                  // the tune, or a refusal when it cannot be had
    bcc muMainGo
    jmp muPtrRefuse             // muLoad left the reason in MU_STR
muMainGo:
    jsr muFindTempo             // the tempo the first note will hear
    jsr muUnitNow               // the row unit that tempo falls into
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

// --- muNameSetup -- the FS name: the base name with .mus on the end ---
//  FS names are SCREEN CODES, so the letters come from character literals under
//  the file's encoding, not from a PETSCII string. A 7-character base reaches
//  exactly the 11 the filesystem allows.
muNameSetup:
    ldx #0
muNsLp:
    cpx muNameLen
    beq muNsExt
    lda muName,x
    sta muFsName,x
    inx
    bne muNsLp                  // a 7-character name ends the loop, never wraps
muNsExt:
    lda #'.'
    sta muFsName,x
    lda #'m'
    sta muFsName+1,x
    lda #'u'
    sta muFsName+2,x
    lda #'s'
    sta muFsName+3,x
    lda #0
    sta muFsName+4,x
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

// --- muImgToKPtr -- kPtr = the image block ---
//  Re-derived every time: every FS call and every kMalloc clobbers kPtr.
muImgToKPtr:
    ldy #MU_IMG
    lda (dataPtr),y
    sta kPtr
    ldy #MU_IMG+1
    lda (dataPtr),y
    sta kPtr+1
    rts

// --- muLoad -- the tune the name stands for, or a new one ---
//  Out: C=0 the image is in the block; C=1 MU_STR holds the reason.
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
    //  --- pages = ceil(size / 256), never zero ---
    ldy #MU_FLEN
    lda (dataPtr),y
    clc
    adc #$ff
    ldy #MU_FLEN+1
    lda (dataPtr),y
    adc #0
    bne muLdPages
    lda #1
muLdPages:
    jsr kMalloc
    bcs muLdNoRoom
    //  kPtr's low byte is ALWAYS 0 -- a block is page-aligned -- so the address
    //  has to be taken as a pair; the high byte is the whole of it.
    ldy #MU_IMG
    lda kPtr
    sta (dataPtr),y
    ldy #MU_IMG+1
    lda kPtr+1
    sta (dataPtr),y
    ldy #MU_PAGES
    lda kArg+4                  // the page echo: never a register, allocFromPool clobbers X
    sta (dataPtr),y
    //  --- and the image itself ---
    jsr muFsArg
    jsr muImgToKPtr
    jsr kReuLoad
    bcs muLdRead
    //  --- MU_LEN is the file's length, and the slack behind it becomes zeroes ---
    ldy #MU_FLEN
    lda (dataPtr),y
    ldy #MU_LEN
    sta (dataPtr),y
    ldy #MU_FLEN+1
    lda (dataPtr),y
    ldy #MU_LEN+1
    sta (dataPtr),y
    jsr muBlockPad
    jmp muLdCheck
muLdRead:
    muStr(muSNoRead)
    sec
    rts
muLdNoRoom:
    muStr(muSNoRoom)
    sec
    rts

// --- muLdNew -- the blank tune a name with no file stands for ---
muLdNew:
    lda #1
    jsr kMalloc
    bcs muLdNoRoom
    ldy #MU_IMG                 // the pair again: kPtr's low byte is always 0
    lda kPtr
    sta (dataPtr),y
    ldy #MU_IMG+1
    lda kPtr+1
    sta (dataPtr),y
    ldy #MU_PAGES
    lda kArg+4
    sta (dataPtr),y
    ldy #MU_LEN
    lda #<(muNewTuneEnd - muNewTune)
    sta (dataPtr),y
    lda #0
    ldy #MU_LEN+1
    sta (dataPtr),y
    ldy #MU_FLEN
    sta (dataPtr),y
    ldy #MU_FLEN+1
    sta (dataPtr),y             // nothing on disk yet
    jsr muImgToKPtr
    ldy #0
muLdNewLp:
    lda muNewTune,y
    sta (kPtr),y
    iny
    cpy #(muNewTuneEnd - muNewTune)
    bne muLdNewLp
    jmp muBlockPad

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

// --- muBlockPad -- zero the block from the image's end to its last page ---
//  A save writes a whole file out of this block, slack included, so the bytes
//  behind the image have to be zeroes rather than whatever the pool handed back.
muBlockPad:
    ldy #MU_IMG
    lda (dataPtr),y
    ldy #MU_LEN
    clc
    adc (dataPtr),y
    sta kPtr
    ldy #MU_IMG+1
    lda (dataPtr),y
    ldy #MU_LEN+1
    adc (dataPtr),y
    sta kPtr+1
    ldy #MU_PAGES
    lda (dataPtr),y
    sta muTmpH                  // pages: the count's high byte before the borrow
    lda #0
    ldy #MU_LEN
    sec
    sbc (dataPtr),y
    sta kArg                    // count lo
    lda muTmpH
    ldy #MU_LEN+1
    sbc (dataPtr),y
    sta kArg+1                  // count hi
    lda kArg
    ora kArg+1
    beq muPadDone
    lda #0
    jsr kReuFillC64
muPadDone:
    clc
    rts

// --- muSaveNow -- write the image back to <base>.mus ---
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
    //  --- the file's whole length, out of the block ---
    jsr muRecArg
    lda #0
    sta kArg+2
    sta kArg+3                  // pos = 0
    lda muTmpL
    sta kArg+6
    lda muTmpH
    sta kArg+7                  // len = the file's size
    jsr muImgToKPtr
    jsr kReuWriteAt
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
    jsr muBlockPad              // the pages behind the image go out as zeroes
    //  --- the new file's whole length, out of the block ---
    jsr muRecArg
    lda #0
    sta kArg+2
    sta kArg+3
    sta kArg+6                  // len lo = 0
    lda muNeedPage
    sta kArg+7                  // len = pages x 256
    jsr muImgToKPtr
    jsr kReuWriteAt
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
    //  the voice's size word: image + 2 + 2 * voice
    lda muScanV
    asl
    clc
    adc #2
    sta muJifT
    ldy #MU_IMG
    lda (dataPtr),y
    clc
    adc muJifT
    sta srcPtr
    ldy #MU_IMG+1
    lda (dataPtr),y
    adc #0
    sta srcPtr+1
    ldy #0
    lda (srcPtr),y
    sta muScanLen
    iny
    lda (srcPtr),y
    sta muScanLen+1
    lda muScanLen
    sta muScanN
    lda muScanLen+1
    sta muScanN+1
    //  and the stream itself
    ldy #MU_IMG
    lda (dataPtr),y
    clc
    adc muScanOff
    sta srcPtr
    ldy #MU_IMG+1
    lda (dataPtr),y
    adc muScanOff+1
    sta srcPtr+1
muFtWalk:
    lda muScanN
    ora muScanN+1
    beq muFtVoiceDone           // the stream ran out
    ldy #0
    lda (srcPtr),y
    and #$03
    beq muFtVoiceDone           // a note: the leading run is over
    ldy #0
    lda (srcPtr),y
    cmp #$01
    beq muFtVoiceDone           // HLT
    cmp #$06
    bne muFtSkip
    ldy #1
    lda (srcPtr),y              // the TEM option
    sta muTempo
muFtSkip:
    lda srcPtr
    clc
    adc #2
    sta srcPtr
    bcc muFtNoHi
    inc srcPtr+1
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
    lda #0
    sta muPhHi,x                // every frame reads as the DEF sentinel
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
    jsr muImgToKPtr
    lda kPtr
    clc
    adc #8
    sta muAccLo
    lda kPtr+1
    adc #0
    sta muAccHi
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

// --- muPtrToK -- kPtr = the event address of voice X ---
muPtrToK:
    lda muPtrLo,x
    sta kPtr
    lda muPtrHi,x
    sta kPtr+1
    rts

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
    jmp muSimStop
muStepLen:
    ldx muSv
    sta muJifOut                // the length, so a caller can measure the note
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
    lda muPhHi,y
    bne muCmEndGo
    jmp muStepRun               // the DEF sentinel: carry on after the END
muCmEndGo:
    sta muPtrHi,x
    lda muPhLo,y
    sta muPtrLo,x
    lda muPhSt,y
    sta muId,x
    jmp muStepRun

muCmCall:
    ldx muSv
    lda muBaseTab,x
    clc
    adc #MU_FRAMES
    cmp muDep,x
    bne muCmCallGo
    lda #MU_ERR_STK             // the call stack is full
    sta muErr
    jmp muSimStop
muCmCallGo:
    ldy muSph
    lda muDfHi,y
    bne muCmCallGo2
    lda #MU_ERR_PHR             // a phrase that was never defined
    sta muErr
    jmp muSimStop
muCmCallGo2:
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
    lda #MU_ERR_STK
    sta muErr
    jmp muSimStop
muCmDefGo:
    lda muDep,x
    tay
    lda #0
    sta muPhHi,y                // the sentinel: this frame returns to nothing
    lda muId,x
    sta muPhSt,y
    inc muDep,x
    jmp muStepRun

// --- muSimStop -- silence every voice: an error, or the song's own end ---
//  A player error stops all three voices, which is what SID_STATUS does, and
//  MU_ERR says which one it was (0 = the walk simply ran out of events).
muSimStop:
    lda #0
    sta muOn
    sta muOn+1
    sta muOn+2
    clc
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
    //  The event's own address goes in muAccLo/Hi, which only muSimInit uses,
    //  because muNum16 borrows muTmpL while it divides down the digits.
    ldx muSv
    lda muPtrLo,x               // the event is the pair behind the pointer
    sec
    sbc #2
    sta muAccLo
    lda muPtrHi,x
    sbc #0
    sta muAccHi
    lda muAccLo
    sta kPtr
    lda muAccHi
    sta kPtr+1
    ldy #0
    lda (kPtr),y
    sta muTmp2
    ldy #1
    lda (kPtr),y
    sta muTmp2+1
    //  ...and as an offset into the image, which is how tools/mus-sim.py names it
    ldy #MU_IMG
    lda muAccLo
    sec
    sbc (dataPtr),y
    sta muAccLo
    iny
    lda muAccHi
    sbc (dataPtr),y
    sta muAccHi
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

// --- muDrawHead -- the name, the tempo, the bar and the cursor's row ---
//  BAR is the bar the cursor is in and R its row, both counted in the unit the
//  grid is drawn in, which is what makes a scrolled window's position legible.
muDrawHead:
    jsr muClearRow0
    jsr muNameAt
    muStr(muSTem2)
    jsr muTextAt
    lda muTempo
    jsr muHex8
    muStr(muSBar2)
    jsr muTextAt
    jsr muCursorAbs             // A/X = the cursor's absolute row
    jsr muBarOf                 // A/X = that row's bar, 1-based
    jsr muNum16
    muStr(muSRow2)
    jsr muTextAt
    jsr muCursorAbs
    jsr muNum16
    muSet(MU_HOV, $ff)          // no pitch yet: three blanks, until a hover says otherwise
    jmp muHovDraw

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
    jmp muGdNums

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
//  The onset row carries the pitch (or a rest) and the cells to the lane's end;
//  the rows the length covers carry a bar. A '*' in the lane's last cell says
//  the event arrived with commands, a '?' that its onset fell between rows.
muGdEvent:
    ldx muSv
    lda muSopt
    and #$07
    bne muGdEvNote
    lda #1
    sta muGdRest
    bne muGdEvGo
muGdEvNote:
    lda #0
    sta muGdRest
muGdEvGo:
    ldx muSv
    lda muVRow,x
    sta muGdRowLo
    lda muVRowHi,x
    sta muGdRowHi
    lda #1
    sta muGdFirst
    jsr muGdVisible
    bcs muGdEvSpan
    jsr muGdCells
muGdEvSpan:
    inc muGdRowLo               // the rows under the onset
    bne muGdEvS2
    inc muGdRowHi
muGdEvS2:
    lda #0
    sta muGdFirst
    lda muSpan
    ora muSpan+1
    beq muGdEvDone              // shorter than a row: the onset is all there is
    lda muSpan
    sec
    sbc #1
    sta muGdLeft
    lda muSpan+1
    sbc #0
    sta muGdLeft+1
    lda muGdLeft
    ora muGdLeft+1
    beq muGdEvDone
muGdEvLp:
    jsr muGdVisible
    bcs muGdEvNext
    jsr muGdCells
muGdEvNext:
    inc muGdRowLo
    bne muGdEvN2
    inc muGdRowHi
muGdEvN2:
    lda muGdLeft
    sec
    sbc #1
    sta muGdLeft
    lda muGdLeft+1
    sbc #0
    sta muGdLeft+1
    lda muGdLeft
    ora muGdLeft+1
    beq muGdEvDone
    jsr muGdPastRow
    bcc muGdEvLp
muGdEvDone:
    rts

// --- muGdPastRow -- C=1 when MU_GDROW is at or past the window's end ---
muGdPastRow:
    lda muGdRowHi
    cmp muLimHi
    bcc muGdPrNo
    bne muGdPrYes
    lda muGdRowLo
    cmp muLimLo
    bcc muGdPrNo
muGdPrYes:
    sec
    rts
muGdPrNo:
    clc
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
    bne muGdVNo                 // a window is 18 rows: any carry means past it
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

// --- muGdCells -- one row of the current event ---
muGdCells:
    ldx muSv
    lda muLaneCol,x
    sta muGdCol
    jsr muGdAtCell
    lda muGdFirst
    beq muGdCCont
    lda muSopt
    and #$07
    bne muGdCPitch
    lda #1
    sta muGdRest                // a rest: three dots, and no bar behind them
    lda #MU_REST_CH
    jsr muPutC
    lda #MU_REST_CH
    jsr muPutC
    lda #MU_REST_CH
    jsr muPutC
    jmp muGdCMark
muGdCCont:
    lda muGdRest
    bne muGdCDone               // the rows under a rest stay blank
    jmp muGdCBar
muGdCPitch:
    jsr muGdPitch
    jmp muGdCBar
muGdCBar:
    ldx muSv
    lda muLaneCol,x
    clc
    adc muLaneW,x
    sta muGdEnd
muGdCBarLp:
    ldy #MU_CURCOL
    lda (dataPtr),y
    cmp muGdEnd
    bcs muGdCBarDone
    lda #MU_BAR_CH
    jsr muPutC
    jmp muGdCBarLp
muGdCBarDone:
    lda muGdFirst
    beq muGdCDone
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
    rts

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
    clc
    adc #'0'
    jmp muPutC

// --- muGdNums -- the row numbers, counted from each bar's first row ---
//  A bar is 8/16/32/64 rows normally, so two digits cover it; the bar itself is
//  in the head, where the cursor's position is spelled out in full.
muGdNums:
    lda #0
    sta muGdIdx
muGdNLp:
    lda muGdIdx
    cmp #MU_GRID_ROWS
    bcs muGdNRts
    clc
    adc #MU_GRID_TOP
    sta muClrRow                // the screen row
    lda muGdIdx
    clc
    adc muTop
    sta kArg
    lda muTop+1
    adc #0
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
    lda muClrRow                // the screen row, and the two separators
    ldy #MU_CURROW
    sta (dataPtr),y
    lda #1
    ldy #MU_CURCOL
    sta (dataPtr),y
    lda muGdNum
    jsr muNum2
    lda muClrRow
    ldy #MU_CURROW
    sta (dataPtr),y
    lda #MU_SEP1
    ldy #MU_CURCOL
    sta (dataPtr),y
    lda #MU_SEP_CHAR
    jsr muPutC
    lda muClrRow
    ldy #MU_CURROW
    sta (dataPtr),y
    lda #MU_SEP2
    ldy #MU_CURCOL
    sta (dataPtr),y
    lda #MU_SEP_CHAR
    jsr muPutC
    inc muGdIdx
    jmp muGdNLp
muGdNRts:
    lda #0
    sta muGdClip
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
    jmp muQuit                  // out of branch range: invert and jump
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
    cmp #'x'                    // X: the first events, as the walk plays them
    bne muKeyNotX
    jsr muSimTrace
    jmp muKeyIdle
muKeyNotX:
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
    jsr muKeyNote               // a note key enters its pitch at the cursor
    bcc muKeyNotNote
    jsr muNoteClick
    jmp muKeyIdle
muKeyNotNote:
    lda muTmp2                  // the key as it came in
    cmp #$80                    // INST/DEL: the event at the cursor becomes a rest
    bne muKeyNotRest
    jsr muRestKey
    jmp muKeyIdle
muKeyNotRest:
    cmp #$88                    // SHIFT+INST/DEL: delete the event, close the gap
    bne muKeyNotDel
    jsr muDelKey
    jmp muKeyIdle
muKeyNotDel:
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
    jsr muSimInit
    ldx #2
muSkZero:
    lda #0
    sta muVRow,x
    sta muVRowHi,x
    sta muVRem,x
    dex
    bpl muSkZero
    ldy #MU_VOICE
    lda (dataPtr),y
    sta muSkV
    sta muSv                    // the prime: row 0, span 0, at the voice's start
    lda #0
    sta muSpan
    sta muSpan+1
    sta muTmpL
    sta muTmpH
    jsr muSkRecord
    jsr muCursorAbs
    sta muTgtLo
    stx muTgtHi
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
    lda muPtrLo,x
    sec
    sbc #2
    sta muPrtLo
    lda muPtrHi,x
    sbc #0
    sta muPrtHi
    ldy #MU_IMG
    lda muPrtLo
    sec
    sbc (dataPtr),y
    sta muSekLo
    iny
    lda muPrtHi
    sbc (dataPtr),y
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

// --- muSeekKPtr -- kPtr = the image at MU_SEKLO/HI ---
muSeekKPtr:
    ldy #MU_IMG
    lda (dataPtr),y
    clc
    adc muSekLo
    sta kPtr
    ldy #MU_IMG+1
    lda (dataPtr),y
    adc muSekHi
    sta kPtr+1
    rts

// --- muNoteClick -- a piano click enters its pitch on the cursor's row ---
//  The hover has already put the clicked key's linear note in MU_HOV. The armed
//  length is checked against the tempo in force at that row first, so an entry
//  the player could not express writes nothing.
muNoteClick:
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
    lda muSekKind
    beq muNcReplace            // an onset on this row: the pair is replaced
    cmp #1
    bne muNcKind2
    jmp muNcSplit
muNcKind2:
    jmp muFillNote              // a gap, or past the voice's last event
muNcReplace:
    jsr muSeekKPtr
    ldy #0
    lda muDurArm
    sta (kPtr),y
    iny
    lda muNewOpt
    sta (kPtr),y
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
    lda muSkV
    jsr muInsGap
    bcc muFgW
    jmp muStatus                // muInsGap left the reason in MU_STR
muFgW:
    jsr muSeekKPtr
    lda kPtr                    // step over the event that is already there
    clc
    adc #2
    sta kPtr
    bcc muFgRest
    inc kPtr+1
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
    jmp muRedrawAll             // tail: its rts balances the jsr that entered

// --- muRestKey -- the event at the cursor becomes a rest of the same length ---
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
    jsr muSeekKPtr
    ldy #0
    lda (kPtr),y
    and #$bf                    // bit 6 is the tie
    sta (kPtr),y
    iny
    lda #0
    sta (kPtr),y
    jmp muRedrawAll

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
    lda #2
    sta muInsN
    lda muSekLo
    sta muInsLo
    lda muSekHi
    sta muInsHi
    lda muSkV
    jsr muDelGap
    jmp muRedrawAll

// --- muNoteOpt -- A = the option byte for the linear note in MU_HOV ---
//  The format stores a letter (1..7), an accidental (10 plain, 01 sharp) and an
//  octave; a black key is written as the sharp of the letter below it.
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
    ldy #0
    lda muLpDur
    sta (kPtr),y
    iny
    lda muLenOpt
    sta (kPtr),y
    lda kPtr
    clc
    adc #2
    sta kPtr
    bcc muLsRts
    inc kPtr+1
muLsRts:
    rts

// --- muImgRoom -- C=0 when MU_INSN more bytes fit behind MU_LEN ---
//  Grows the block by one spare page when they do not, so a run of small
//  inserts does not reallocate on every one. C=1 MU_STR holds the reason.
muImgRoom:
    jsr muImgNeed
    ldy #MU_PAGES
    lda (dataPtr),y
    sta muRoomHi
    lda muNeedHi
    cmp muRoomHi
    bcc muIrOk
    bne muImgGrow
    lda muNeedLo
    beq muIrOk              // exactly to the end of the block
    jmp muImgGrow
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

// --- muImgGrow -- a bigger block, the image copied across, the old one freed ---
//  Two blocks live at once, so the pool has to hold both for a moment; if it
//  cannot, nothing is freed and the edit is refused.
muImgGrow:
    lda muNeedHi            // pages = MU_NEED / 256, rounded up, plus a spare
    sta muGrowPages
    lda muNeedLo
    beq muIgPages
    inc muGrowPages
muIgPages:
    inc muGrowPages
    lda muGrowPages
    jsr kMalloc
    bcs muIgRoom
    lda kPtr
    sta muNewLo
    lda kPtr+1
    sta muNewHi
    lda kArg+4              // the page echo: never a register
    sta muGrowPages
    jsr muImgToKPtr         // src = the old block
    lda kPtr
    sta srcPtr
    lda kPtr+1
    sta srcPtr+1
    lda muNewLo             // dst = the new one
    sta kPtr
    lda muNewHi
    sta kPtr+1
    jsr muImgCopy
    lda srcPtr              // kFree takes the block in kPtr
    sta kPtr
    lda srcPtr+1
    sta kPtr+1
    jsr kFree
    ldy #MU_IMG
    lda muNewLo
    sta (dataPtr),y
    ldy #MU_IMG+1
    lda muNewHi
    sta (dataPtr),y
    ldy #MU_PAGES
    lda muGrowPages
    sta (dataPtr),y
    jsr muImgToKPtr
    clc
    rts
muIgRoom:
    muStr(muSNoRoom)
    sec
    rts

// --- muImgCopy -- MU_LEN bytes from srcPtr to kPtr, both advancing ---
muImgCopy:
    ldy #MU_LEN
    lda (dataPtr),y
    sta muCountLo
    ldy #MU_LEN+1
    lda (dataPtr),y
    sta muCountHi
muIcLp:
    lda muCountLo
    ora muCountHi
    beq muIcRts
    ldy #0
    lda (srcPtr),y
    sta (kPtr),y
    inc srcPtr
    bne muIcS
    inc srcPtr+1
muIcS:
    inc kPtr
    bne muIcD
    inc kPtr+1
muIcD:
    lda muCountLo
    bne muIcN
    dec muCountHi
muIcN:
    dec muCountLo
    jmp muIcLp
muIcRts:
    rts

// --- muInsGap -- open MU_INSN bytes at MU_INSLO/HI ---
//  In: MU_INSLO/HI the offset (an event boundary), MU_INSN the byte count,
//      A = the voice whose length word grows.
//  Out: C=0 the gap is open, kPtr = the image; C=1 MU_STR the reason and the
//       image is unchanged.
muInsGap:
    sta muInsV
    jsr muImgRoom
    bcc muInGo
    sec                     // out of branch range: the tail is far away
    rts
muInGo:
    ldy #MU_LEN             // MU_LEN into muCount, so the arithmetic is one pair
    lda (dataPtr),y
    sta muCountLo
    ldy #MU_LEN+1
    lda (dataPtr),y
    sta muCountHi
    jsr muImgToKPtr
    lda kPtr                // src = image + MU_LEN - 1
    clc
    adc muCountLo
    sta srcPtr
    lda kPtr+1
    adc muCountHi
    sta srcPtr+1
    lda srcPtr
    bne muInSd
    dec srcPtr+1
muInSd:
    dec srcPtr
    lda srcPtr              // dst = src + MU_INSN
    clc
    adc muInsN
    sta kPtr
    lda srcPtr+1
    adc #0
    sta kPtr+1
    lda muCountLo           // count = MU_LEN - MU_INSLO
    sec
    sbc muInsLo
    sta muCountLo
    lda muCountHi
    sbc muInsHi
    sta muCountHi
muInLp:
    lda muCountLo
    ora muCountHi
    beq muInMoved
    ldy #0
    lda (srcPtr),y
    sta (kPtr),y
    lda srcPtr
    bne muInS
    dec srcPtr+1
muInS:
    dec srcPtr
    lda kPtr
    bne muInD
    dec kPtr+1
muInD:
    dec kPtr
    lda muCountLo
    bne muInC
    dec muCountHi
muInC:
    dec muCountLo
    jmp muInLp
muInMoved:
    //  --- the voice's length word, at image offset 2 + 2 * voice ---
    jsr muImgToKPtr             // first: it sets Y itself
    lda muInsV
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
    //  --- and the image length ---
    ldy #MU_LEN
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
    jsr muImgToKPtr
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
    jsr muImgToKPtr
    lda kPtr                // srcPtr = image + MU_INSLO: where the tail lands
    clc
    adc muInsLo
    sta srcPtr
    lda kPtr+1
    adc muInsHi
    sta srcPtr+1
    lda srcPtr              // kPtr = past the gap: where the tail is now
    clc
    adc muInsN
    sta kPtr
    lda srcPtr+1
    adc #0
    sta kPtr+1
    ldy #MU_LEN             // count = MU_LEN - MU_INSLO - MU_INSN
    lda (dataPtr),y
    sec
    sbc muInsLo
    sta muCountLo
    ldy #MU_LEN+1
    lda (dataPtr),y
    sbc muInsHi
    sta muCountHi
    lda muCountLo
    sec
    sbc muInsN
    sta muCountLo
    bcs muDgLp
    dec muCountHi
muDgLp:
    lda muCountLo
    ora muCountHi
    beq muDgMoved
    ldy #0
    lda (kPtr),y
    sta (srcPtr),y
    inc kPtr
    bne muDgS
    inc kPtr+1
muDgS:
    inc srcPtr
    bne muDgD
    inc srcPtr+1
muDgD:
    lda muCountLo
    bne muDgC
    dec muCountHi
muDgC:
    dec muCountLo
    jmp muDgLp
muDgMoved:
    //  --- the voice's length word, at image offset 2 + 2 * voice ---
    jsr muImgToKPtr             // first: it sets Y itself
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
    //  --- and the image length ---
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
    rts

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
    cmp #MU_BAR_SAVE
    beq muBarSave
    cmp #MU_BAR_Q
    beq muQuit
    muStr(muSNotYet)            // a labelled cell with nothing behind it yet
    jmp muStatus
muBarSave:
    jsr muSaveTry
    rts
muClkGrid:
    cmp #MU_GRID_TOP
    bcc muClkLow
    cmp #MU_GRID_BOT+1
    bcs muClkLow
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
muClkLow:
    cmp #MU_PIANO_TOP            // the piano: the click enters that pitch
    bcc muClkRts
    cmp #MU_PIANO_TOP+4
    bcs muClkRts
    jmp muNoteClick
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
muSLanes:   .text "ROW VOICE 1    |VOICE 2    |VOICE 3"
            .byte 0
muSTem2:    .text " TEM$"
            .byte 0
muSBar2:    .text " BAR "
            .byte 0
muSRow2:    .text " R"
            .byte 0
// One letter per note index 1..7 (C D E F G A B), in semitones from C.
muLetterSem: .byte 0, 0, 2, 4, 5, 7, 9, 11
muLaneCol:  .byte 3, 16, 28    // the first lane cell of each voice
muLaneW:    .byte 12, 11, 11   // ...and how many cells it has
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
muSIllegal: .text "? illegal duration"
            .byte 0
muSNoEvent: .text "? no event here"
            .byte 0
// A black key is written as the sharp of the letter below it: one letter and a
// sharp flag per semitone, indexed by the semitone inside the octave.
muSemLetter: .byte 1, 1, 2, 2, 3, 4, 4, 5, 5, 6, 6, 7
muSemSharp:  .byte 0, 1, 0, 1, 0, 0, 1, 0, 1, 0, 1, 0
muSSaved:   .text "? saved"
            .byte 0
muSNoRoom:  .text "? no space"
            .byte 0
muSNoFs:    .text "? no fs"
            .byte 0
muSNoRead:  .text "? unreadable"
            .byte 0
muSNoWrite: .text "? write failed"
            .byte 0
muSNoTune:  .text "? not a tune"
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
muDirName:  .fill 12, 0         // kReuDirEntry's name buffer (slot 0 only)
// The powers muNum16 subtracts: five cover every 16-bit value.
muPowLo:    .byte <10000, <1000, <100, <10, <1
muPowHi:    .byte >10000, >1000, >100, >10, >1

// The duration columns the timing line prints, W .. 64th, and the class each
// one wears. The 64th last: it is the shortest, but it is only a length at the
// tempos whose whole note is a few jiffies.
muDurTab:   .byte $08, $0c, $10, $14, $18, $1c, $00   // W H Q 8 16 32 64
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
// The control bar's hit map: one code per column of row 24, tested by column.
// A blank cell between two labels is deliberately 0, so nothing answers there.
muBarMap:
    .byte 1,1,1, 0, 1,1,1, 0            // 0-7    DUR, the value, the dot
    .byte 1,1,1,1, 0,0, 1,1,1,1, 0,0    // 8-19   TIE-, PLAY
    .byte 1,1,1, 0,0, 1,1,1,1, 0,0      // 20-30  CMD, UNDO
    .byte 3,3,3,3, 0,0, 2, 0,0          // 31-39  SAVE, Q
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
muNumVal:   .word 0
muNumDig:   .byte 0
muNumPow:   .byte 0
muNumSquash: .byte 0
muTmpL:     .byte 0
muTmpH:     .byte 0
muNeedPage: .byte 0
muFreeMid:  .byte 0
muFreeHi:   .byte 0
muClrRow:   .byte 0
muTempo:    .byte 0             // the tempo in force at the first note
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
muTop:      .word 0             // the timeline row at the top of the window
muVRow:     .fill 3,0          // each voice's next event row
muVRowHi:   .fill 3,0
muVRem:     .fill 3,0          // jiffies carried into that voice's next row
muCmdN:     .fill 3,0          // commands seen since that voice's last note
muSpan:     .word 0            // rows the current event covers
muJifOut:   .word 0            // the length muStep just consumed
muGdRowLo:  .byte 0
muGdRowHi:  .byte 0
muGdLeft:   .word 0
muGdSRow:   .byte 0
muGdCol:    .byte 0
muGdEnd:    .byte 0
muGdFirst:  .byte 0
muGdRest:   .byte 0
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
muRoomHi:   .byte 0             // MU_PAGES * 256, high byte only
muGrowPages: .byte 0
muNewLo:    .byte 0
muNewHi:    .byte 0
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
muPtrLo:    .fill 3, 0          // where this voice's next event is
muPtrHi:    .fill 3, 0
muTkLo:     .fill 3, 0          // the tick that event plays on
muTkHi:     .fill 3, 0
muOn:       .fill 3, 0          // a HLT clears this
muUtv:      .fill 3, 0
muId:       .fill 3, 0          // the phrase identity, as WorkVoiceForCallPhrase
muHedN:     .fill 3, 0          // HED's repeat count
muHedLo:    .fill 3, 0          // ...and the address just after it
muHedHi:    .fill 3, 0
muHedId:    .fill 3, 0
muDep:      .fill 3, 0          // the next free call-stack frame
muPhLo:     .fill 15, 0         // the call stack, five frames per voice
muPhHi:     .fill 15, 0
muPhSt:     .fill 15, 0
muDfLo:     .fill 24, 0         // phrase definitions, as PhraseDef
muDfHi:     .fill 24, 0
muDfId:     .fill 24, 0
muEvLo:     .fill 3, 0          // events each voice has played
muEvHi:     .fill 3, 0
muClkLo:    .byte 0             // the tick of the latest event
muClkHi:    .byte 0
muSimT:     .byte 0             // the tempo the walk is in
muJifTem:   .byte 0             // ...and the copy muNoteJiffies reads
muUl:       .byte 0
muUtvC:     .byte 0
muErr:      .byte 1
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
