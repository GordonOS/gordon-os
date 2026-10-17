// ==========================================
//  GordonOS -- "edit" task (editor body)
// ==========================================
//  Editor with an optional filename argument. `run edit <file>` opens
//  the file (loading it if it exists, or starting blank for a new one).
//  `run edit` with no argument prints a usage error on the shell's
//  screen (shared -- no surface allocated) and exits.
//  Document model: a growable line table -- fixed 41-byte
//  records (record[0] = length 0..40, record[1..40] = screen codes)
//  packed 24 per 4-page kMalloc chunk, up to 8 chunks (192 lines).
//  The screen is a window over lines winTop..winTop+23 (scroll moves winTop);
//  row 24 is a reverse-video status bar. The char
//  shadow is NOT used -- the body is drawn with kBlitChar and the cursor
//  positioned with kSetCursor (blink moved via kBlinkReset).
//  Load: kReuOpen + windowed kReuReadAt streaming (filesys.lib).
//  Save: streamed kReuWriteAt writes -- the high-water fixed-size model.
//  STOP + q quits (confirms if modified), STOP + x saves-and-exits,
//  STOP + s saves, STOP + w writes-as (save under a new filename),
//  STOP + c clears, STOP + m toggles mark mode, STOP + y copies the
//  selection, STOP + k cuts it, STOP + p pastes; STOP again or any other
//  key cancels. F1/F3/F5/F7 still switch back to the shell.

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg defined in tools/assemble-tasks.asm)

// record/chunk geometry
.const REC_SIZE       = 41
.const RECS_PER_CHUNK = 24           // 24 x 41 = 984 <= 4 pages (1024)
.const CHUNK_PAGES    = 4
.const MAX_CHUNKS     = 8
.const MAX_LINES      = 192
.const ED_REU_BOUNCE  = REU_BOUNCE   // REU scroll bounce (kernel-shared.inc, via kernel-data.inc)

// instance buffer (delivered args live at offset 0 -- the filename)
.const ED_LINES  = 12            // 2: line count
.const ED_CURL   = 14            // 2: cursor line
.const ED_CURC   = 16            // 1: cursor col 0..39
.const ED_PREFC  = 17            // 1: preferred column (5.7)
.const ED_WIN    = 18            // 2: window top line
.const ED_CHUNKS = 20            // 1: chunk count
.const ED_INSM   = 21            // 1: 0=insert, 1=overwrite
.const ED_SIZE   = 22            // 2: total characters
.const ED_FREC   = 24            // 5: kReuOpen file record
.const ED_POS    = 29            // 2: stream position
.const ED_NUL    = 31            // 2: NUL scratch (tail fill source)
.const ED_MOD    = 33            // 1: document modified since load/save
.const ED_CMD    = 34            // 1: 0=edit, 1=command, 2=confirm-quit, 3=name, 4=confirm-clear
.const ED_STCOL  = 35            // 1: status bar blit column scratch
.const ED_STIDX  = 36            // 1: status bar filename index
.const ED_DIG    = 37            // 3: kByteToDec digit buffer
.const ED_DCNT   = 40            // 1: digit count
.const ED_DIDX   = 41            // 1: digit index
.const ED_PTR    = 42            // 2: string-pointer scratch
.const ED_CARRY  = 44            // 1: pending byte across read windows
.const ED_FLAG   = 45            // 1: redraw pending after an edit
.const ED_REC    = 46            // 2: record-address scratch
.const ED_SCR    = 48            // 2: generic 16-bit scratch
.const ED_TMPL   = 50            // 1: temp
.const ED_TMPH   = 51            // 1: temp
.const ED_TCHAR  = 52            // 1: temp char/count
.const ED_HASFILE = 53           // 1: 1 = file exists (record valid)
.const ED_CHKTAB = 54            // 16: 8 x chunk base addresses
.const ED_WINBUF = 70            // 128: kReuReadAt window scratch
.const ED_CASC   = 198           // 1: backspace cascade delete col +1 (0 = off)
.const ED_TASKID = 199           // 1: own task slot (kSurfDisplayed gates)
.const ED_MARK   = 200           // 1: mark mode (0=off, 1=on)
.const ED_ANCL   = 201           // 1: anchor line
.const ED_ANCC   = 202           // 1: anchor col
.const ED_CB     = 203           // 2: clipboard base (0 = none)
.const ED_CBSZ   = 205           // 2: clipboard size (bytes)
.const ED_PAD    = 207           // 1: status-field pad end col (edBlitPad)
.const ED_DATA_SIZE = 208

edit:
    ldy #0
    lda (dataPtr),y              // filename (args delivered at offset 0)
    bne editScr                  // has a filename -> own screen
    // no argument -- usage error on the shell's screen (share, no alloc).
    // Printed at fixed cells via kPrintCharAt: `run` doesn't wait for us,
    // so the shell's prompt races our shared cursor and kPrintString would
    // start mid-line and wrap.
    lda kbFocus
    cmp #$ff
    beq editExit                 // nothing focused -- just exit
    tax
    jsr kShareScreen             // share the shell's screen (X preserved)
    bcs editExit                 // no screen to share -- exit
    jsr kLockScreen
    jsr kNewline                 // fresh line
    jsr kGetCursor               // Y = row for the message
    tya
    ldy #ED_SCR
    sta (dataPtr),y              // row scratch
    lda #<editUse
    sta kPtr
    lda #>editUse
    sta kPtr+1
    lda kPtr
    ldy #ED_PTR
    sta (dataPtr),y              // string ptr lo
    lda kPtr+1
    iny
    sta (dataPtr),y              // string ptr hi
    lda #0
    ldy #ED_STCOL
    sta (dataPtr),y              // col = 0
edUseLp:
    ldy #ED_PTR
    lda (dataPtr),y
    sta kPtr
    iny
    lda (dataPtr),y
    sta kPtr+1                   // kPtr = string ptr
    ldy #0
    lda (kPtr),y                 // char
    beq edUseDone
    pha                          // save char (kPrintCharAt clobbers A)
    inc kPtr
    bne edUseSave
    inc kPtr+1
edUseSave:
    lda kPtr
    ldy #ED_PTR
    sta (dataPtr),y              // save advanced ptr lo
    lda kPtr+1
    iny
    sta (dataPtr),y              // save advanced ptr hi
    ldy #ED_STCOL
    lda (dataPtr),y              // col
    tax                          // X = col
    ldy #ED_SCR
    lda (dataPtr),y              // row
    tay                          // Y = row
    pla                          // A = char
    jsr kPrintCharAt             // cell at (col,row) -- no cursor advance
    ldy #ED_STCOL
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y              // col++
    jmp edUseLp
edUseDone:
    jsr kNewline
    jsr kNewline                // extra newline -- no filename supplied
    jsr kUnlockScreen
editExit:
    lda #SYS_EXIT
    jsr syscall                  // exit -- never returns

editScr:
    // own blank screen: kArg = border/bg0/bg1/bg2, A = 0 (hires text)
    // same colors as the shell -- the editor must not change the look
    lda #$0b
    sta kArg                     // border = dark grey
    lda #$00
    sta kArg+1                   // bg0 = black
    lda #0
    sta kArg+2                   // bg1 unused in hires
    lda #0
    sta kArg+3                   // bg2 unused in hires
    lda #0
    jsr kAllocScreen             // fresh surface (no char shadow needed)
    bcc editOk                   // got a screen -- continue
    lda #SYS_EXIT
    jsr syscall                  // exit -- no screen available
editOk:
    lda #SYS_GETTID
    jsr syscall                  // gettid -> A = own slot
    tax
    jsr kSwitchView              // display our screen + take kbFocus
    lda #0                       // scroll region: body rows 0..23, status row 24
    ldx #23
    jsr kSetScrollRegion
    // zero-init the instance fields (offsets 12..data_size-1)
    ldy #ED_DATA_SIZE
    lda #0
edInitZero:
    dey
    cpy #11
    bcc edInitZeroDone
    sta (dataPtr),y
    jmp edInitZero
edInitZeroDone:
    lda #SYS_GETTID
    jsr syscall                  // gettid -> A = own slot
    ldy #ED_TASKID
    sta (dataPtr),y              // stored for the kSurfDisplayed draw gates
    // chunk 0 (the document starts as one blank line)
    lda #CHUNK_PAGES
    jsr kMalloc
    bcc edInitChunk
    jmp edInitFail               // no pool page -> give up cleanly
edInitChunk:
    lda kPtr
    ldy #ED_CHKTAB
    sta (dataPtr),y
    lda kPtr+1
    iny
    sta (dataPtr),y
    lda #1
    ldy #ED_CHUNKS
    sta (dataPtr),y
    ldy #ED_LINES
    sta (dataPtr),y              // one blank line
    // blank record 0 (kMalloc does not zero its pages)
    jsr edRecCur                 // ED_CURL = 0 after the zero-init
    ldy #REC_SIZE
    lda #0
edInitRec:
    dey
    sta (kPtr),y
    bne edInitRec
    jsr edDrawStatus             // status bar first -- visible during load
    jsr editLoad                 // load the file into records (blank if none)
    jsr edDrawSize               // refresh the size (drawn as 0 before load)
    jsr edDrawAll                // render the window
    jsr edSyncCursor
    jmp editLoop
// --- edNoRoom -- the pool could not give us a chunk ---------------------
//  Reported on OUR OWN surface: every caller reaches this while we own the
//  display, and it matters because the failure is otherwise invisible from the
//  shell -- `run edit` can allocate the surface, fail to get its 4-page document
//  chunk and exit without a word. edShowMsg is the shared
//  "message at cols 12..21 of row 24" helper, and it no-ops when our
//  surface is not the displayed one.
edNoRoom:
    lda #<edNoSpaceMsg
    sta kPtr
    lda #>edNoSpaceMsg
    sta kPtr+1
    jsr edShowMsg
    rts

edInitFail:
    jsr edNoRoom                // say why, on our own screen
    jsr kWaitKey                // ...and hold it until acknowledged
    jsr edGiveDisplay           // hand the display back before freeing it
    jsr kFreeScreen
    lda #SYS_EXIT
    jsr syscall

// --- editing loop: records are the document ---
editLoop:
    jsr edIsDisplayed            // C=1 iff our surface is displayed
    bcs edLoopFocused            // displayed -> proceed
    jmp editIdle                 // background -> idle (far)
edLoopFocused:
    ldy #ED_FLAG                 // focus return with a partial frame?
    lda (dataPtr),y
    beq edLoopScan               // frame complete -> scan keys
    lda #0
    sta (dataPtr),y              // clear the redraw flag
    jsr edDrawStatus
    jsr edDrawAll
    jsr edSyncCursor
edLoopScan:
    jsr kScanKeyboard            // A = key or $FF (empty OR unfocused)
    cmp #$ff
    bne edNotIdle
    jmp editIdle                 // branch too far for beq
edNotIdle:
    pha                          // save key -- the ED_CMD read clobbers A
    ldy #ED_CMD
    lda (dataPtr),y              // state: 0=edit, 1=cmd, 2=quit, 3=name, 4=clear
    beq edModeEdit               // 0 -> normal keys
    cmp #2
    beq edModeConfirm            // 2 -> confirm-quit
    cmp #3
    beq edModeName               // 3 -> name input (write-as)
    cmp #4
    beq edModeClearConfirm       // 4 -> confirm-clear
    pla                          // restore key
    jmp edCmdDispatch            // 1 -> command mode (key in A)
edModeEdit:
    pla                          // restore key
    jmp edNotPending             // normal keys (key in A)
edModeConfirm:
    pla                          // restore key
    jmp edCmdConfirm             // confirm-quit (key in A)
edModeName:
    pla                          // restore key
    jmp edNameInput              // name input (key in A)
edModeClearConfirm:
    pla                          // restore key
    jmp edCmdClearConfirm        // confirm-clear (key in A)
edNotPending:
    cmp #$80                    // DEL keeps the cascade going
    beq edNotClrDone
    pha                          // preserve key (lda below clobbers A)
    lda #0                      // any other key ends the cascade
    ldy #ED_CASC
    sta (dataPtr),y
    pla                          // restore key
edNotClrDone:
    cmp #$83                    // STOP -> enter command mode
    bne edNotStop
    lda #1
    ldy #ED_CMD
    sta (dataPtr),y             // pending = 1
    jsr kDisableCursor          // modal prompt: keep the IRQ cursor fully parked
    jsr kBlinkReset             // command-bar redraw must not leave a live block behind
    jsr edDrawCmd               // show the command prompt
    jmp editLoop
edNotStop:
    cmp #$88                    // INST -> toggle insert/overwrite (not an edit)
    bne edNotInst
    ldy #ED_INSM
    lda (dataPtr),y
    eor #1
    sta (dataPtr),y
    jsr edDrawMode               // update the INS/OVR indicator
    jmp editLoop
edNotInst:
    cmp #$81                    // RETURN -> split the line at the cursor
    bne edNotRet
    jsr edReturn
    jsr edAfterEdit
    jmp editLoop
edNotRet:
    cmp #$80                    // DEL -> erase left of the cursor
    bne edNotDel
    jsr edDelDispatch
    jsr edAfterEdit
    jmp editLoop
edNotDel:
    cmp #$87                    // <-
    bne edNotLeft
    jsr edLeft
    jsr edMoveSync
    jmp editLoop
edNotLeft:
    cmp #$86                    // ->
    bne edNotRight
    jsr edRight
    jsr edMoveSync
    jmp editLoop
edNotRight:
    cmp #$85                    // ^
    bne edNotUp
    jsr edUp
    jsr edMoveSync
    jmp editLoop
edNotUp:
    cmp #$84                    // v
    bne edNotDown
    jsr edDown
    jsr edMoveSync
    jmp editLoop
edNotDown:
    cmp #$82                    // HOME
    bne edNotHome
    jsr edHome
    jsr edMoveSync
    jmp editLoop
edNotHome:
    cmp #$00                    // F-key pass-through -- ignore
    bne edNotFKey               // not an F-key -> print
    jmp editLoop                // F-key -- ignore (branch too far for beq)
edNotFKey:
    pha                         // preserve the typed char across the mode read
    ldy #ED_INSM                // insert vs overwrite
    lda (dataPtr),y
    bne edOvrPath
    pla                         // A = typed char
    jsr edInsChar
    jmp edCharDone
edOvrPath:
    pla                         // A = typed char
    jsr edOvChar
edCharDone:
    jsr edAfterEdit
    jmp editLoop
editIdle:
    lda #SYS_YIELD
    jsr syscall                 // yield -- never spin
    jmp editLoop

// --- command mode dispatch (STOP prefix) ---
edCmdDispatch:
    cmp #$83                    // STOP again -> cancel (toggle)
    bne edCmdNotStop
    jmp edCmdCancel
edCmdNotStop:
    cmp #$13                    // s -> save (stay)
    bne edCmdNotSave
    jsr edSave
    lda #0
    ldy #ED_CMD
    sta (dataPtr),y             // back to editing -- "saved" stays on the bar
    jsr kEnableCursor
    jsr edDrawMode              // restore INS/OVR (edDrawCmd cleared it)
    jsr edDrawSize              // restore the document size
    jmp editLoop
edCmdNotSave:
    cmp #$18                    // x -> save + exit
    bne edCmdNotExit
    jsr edSave
    bcc edCmdSaveExitOk
    jmp edCmdCancel
edCmdSaveExitOk:
    jmp edQuit
edCmdNotExit:
    cmp #$11                    // q -> quit
    bne edCmdNotQuit
    jmp edQuitCheck             // refuse if the document was modified
edCmdNotQuit:
    cmp #$03                    // c -> clear document (keep filename)
    bne edCmdNotClear
    jmp edClearCheck
edDoClear:
    jsr edClear
    lda #0
    ldy #ED_CMD
    sta (dataPtr),y             // back to editing
    jsr kEnableCursor
    jsr edAfterEdit             // body redraw + cursor + l/c + size (clears ED_FLAG)
    jsr edDrawMode              // restore INS/OVR (edDrawCmd wiped it)
    jsr edDrawHint              // restore the hint
    jmp editLoop
edCmdNotClear:
    cmp #$0D                    // m -> toggle mark mode
    bne edCmdNotMark
    jmp edMarkToggle
edCmdNotMark:
    cmp #$19                    // y -> copy (yank) selection
    bne edCmdNotYank
    jmp edCopy
edCmdNotYank:
    cmp #$10                    // p -> paste clipboard
    bne edCmdNotPaste
    jmp edPaste
edCmdNotPaste:
    cmp #$0B                    // k -> cut (copy + delete) selection
    bne edCmdNotCut
    jmp edCut
edCmdNotCut:
    cmp #'w'                    // w -> write-as (save under a new filename)
    bne edCmdCancel             // unknown -> cancel (key not inserted)
    jmp edWriteAs
edQuitCheck:
    ldy #ED_MOD
    lda (dataPtr),y             // modified?
    bne edQuitConfirm           // modified -> ask first
    jmp edQuit                  // clean -> quit
edQuitConfirm:
    lda #2
    ldy #ED_CMD
    sta (dataPtr),y             // confirm-quit pending
    jsr kBlinkReset             // same redraw class as STOP command entry
    jsr edDrawConfirm           // show "modified! q to quit"
    jmp editLoop
edClearCheck:
    lda #4
    ldy #ED_CMD
    sta (dataPtr),y             // confirm-clear pending
    jsr kBlinkReset
    jsr edDrawClearConfirm      // show "press c again to clear"
    jmp editLoop
edCmdConfirm:
    cmp #$11                    // q -> quit for real
    bne edCmdCancel             // any other key -> cancel
    jmp edQuit
edCmdClearConfirm:
    cmp #$03                    // c -> clear for real
    bne edCmdCancel             // any other key -> cancel
    jmp edDoClear
edCmdCancel:
    lda #0
    ldy #ED_CMD
    sta (dataPtr),y             // pending = 0
    jsr kEnableCursor
    jsr edDrawPos               // restore the l/c section
    jsr edDrawMode              // restore the INS/OVR indicator
    jsr edDrawSize              // restore the document size
    jsr edDrawHint              // restore the hint
    jmp editLoop

// ==========================================
//  Write-as (STOP + w) -- save under a new filename
// ==========================================
//  ED_CMD = 3 is the name-input line on the status bar. The typed name
//  accumulates in ED_WINBUF (null-terminated, max 11 chars; length in
//  ED_STIDX). RETURN refuses an EXISTING file ("exists") and otherwise
//  saves via edSave with the typed name as a temporary target; the bound
//  filename only changes after a successful save. STOP cancels, DEL erases.

// --- edWriteAs -- enter the name-input line ---
edWriteAs:
    lda #0
    ldy #ED_STIDX
    sta (dataPtr),y             // input length = 0
    ldy #ED_WINBUF
    sta (dataPtr),y             // buffer[0] = 0 (empty)
    lda #3
    ldy #ED_CMD
    sta (dataPtr),y             // name-input mode
    jsr kBlinkReset             // draw the modal prompt only after clearing any live block
    jsr edDrawWriteAs           // prompt + echo
    jmp editLoop

// --- edNameInput -- write-as name input (ED_CMD = 3, A = key) ---
edNameInput:
    cmp #$83                    // STOP -> cancel
    bne edNiNotStop
    jmp edCmdCancel
edNiNotStop:
    cmp #$81                    // RETURN -> confirm
    bne edNiNotRet
    jmp edNameConfirm
edNiNotRet:
    cmp #$80                    // DEL -> erase the last char
    bne edNiNotDel
    jsr edNameDel
    jsr edDrawWriteAs
    jmp editLoop
edNiNotDel:
    cmp #$00                    // F-key -> ignore
    beq edNiRts
    cmp #$20                    // space -> ignore (names are space-trimmed)
    beq edNiRts
    cmp #$82                    // HOME -> ignore
    beq edNiRts
    cmp #$84                    // arrows/INST $84..$88 -> ignore
    bcc edNiAdd
    cmp #$89
    bcc edNiRts
edNiAdd:
    jsr edNameAdd
    jsr edDrawWriteAs
edNiRts:
    jmp editLoop

// --- edNameConfirm -- RETURN: refuse an existing file, else save ---
edNameConfirm:
    ldy #ED_STIDX
    lda (dataPtr),y             // input length
    bne edNcName
    jmp edCmdCancel             // empty name -> cancel
edNcName:
    ldy #ED_STIDX               // null-terminate the buffer
    lda (dataPtr),y
    clc
    adc #ED_WINBUF
    tay
    lda #0
    sta (dataPtr),y             // buffer[len] = 0
    lda dataPtr                 // existence check: kReuOpen(buffer)
    clc
    adc #ED_WINBUF
    sta kArg
    lda dataPtr+1
    adc #0
    sta kArg+1                  // name = buffer
    lda dataPtr
    clc
    adc #ED_WINBUF+16
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1                  // record scratch (ED_FREC must survive)
    jsr kReuOpen
    bcs edNcGo                  // not found -> create + save
    lda #<edExistsMsg           // file exists -> refuse
    sta kPtr
    lda #>edExistsMsg
    sta kPtr+1
    jsr edShowMsg               // "exists" in cols 12..21
    lda #0
    ldy #ED_CMD
    sta (dataPtr),y             // back to editing
    jsr kEnableCursor
    jsr edDrawMode              // restore INS/OVR
    jsr edDrawSize              // restore size
    jsr edDrawHint              // restore hint
    jmp editLoop
edNcGo:
    ldy #0                      // preserve the old binding across edSave
edNcPushNameLp:
    lda (dataPtr),y
    pha
    iny
    cpy #12
    bcc edNcPushNameLp
    ldy #ED_FREC
edNcPushRecLp:
    lda (dataPtr),y
    pha
    iny
    cpy #ED_FREC+5
    bcc edNcPushRecLp
    ldy #ED_HASFILE
    lda (dataPtr),y
    pha
    lda dataPtr                 // copy buffer -> filename (dataPtr[0..])
    clc
    adc #ED_WINBUF
    sta srcPtr
    lda dataPtr+1
    adc #0
    sta srcPtr+1                // srcPtr = buffer
    ldy #0
edNcCopy:
    lda (srcPtr),y              // char
    sta (dataPtr),y             // filename[y]
    beq edNcCopyDone            // copied the NUL terminator
    iny
    jmp edNcCopy
edNcCopyDone:
    lda #0
    ldy #ED_HASFILE
    sta (dataPtr),y             // new file -> force the create path
    jsr edSave                  // save (shows "saved" / an error)
    bcc edNcKeep
    pla
    ldy #ED_HASFILE
    sta (dataPtr),y
    ldy #ED_FREC+4
edNcRestoreRecLp:
    pla
    sta (dataPtr),y
    dey
    cpy #ED_FREC-1
    bne edNcRestoreRecLp
    ldy #11
edNcRestoreNameLp:
    pla
    sta (dataPtr),y
    dey
    cpy #$FF
    bne edNcRestoreNameLp
    jmp edNcDone
edNcKeep:
    ldx #18
edNcDropLp:
    pla
    dex
    bne edNcDropLp
edNcDone:
    lda #0
    ldy #ED_CMD
    sta (dataPtr),y             // back to editing
    jsr kEnableCursor
    jsr edDrawName              // redraw the filename (cols 0..10)
    jsr edDrawMode              // restore INS/OVR
    jsr edDrawSize              // restore size
    jmp editLoop

// --- edNameDel -- erase the last input char (no-op at len 0) ---
edNameDel:
    ldy #ED_STIDX
    lda (dataPtr),y             // len
    beq edNdRts
    sec
    sbc #1
    sta (dataPtr),y             // len--
    clc
    adc #ED_WINBUF
    tay
    lda #0
    sta (dataPtr),y             // buffer[len] = 0
edNdRts:
    rts

// --- edNameAdd -- append A to the buffer (max 11, null-terminated) ---
edNameAdd:
    pha                         // [char]
    ldy #ED_STIDX
    lda (dataPtr),y             // len
    cmp #11
    bcc edNaOk
    pla                         // full -> discard
    rts
edNaOk:
    clc
    adc #ED_WINBUF              // Y = ED_WINBUF + len
    tay
    pla                         // char
    sta (dataPtr),y             // buffer[len] = char
    iny
    lda #0
    sta (dataPtr),y             // buffer[len+1] = 0
    ldy #ED_STIDX
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y             // len++
    rts

// --- edGiveDisplay -- hand the display back BEFORE releasing our surface ---
//  `edit` owns the framebuffer while it runs (kSwitchView at startup). If we
//  free that surface first and then exit, sysExit's transferFocusAway runs
//  with a surfaceless dying task -- and when nothing else looks ALIVE-owned at
//  that instant (e.g. the shell is swapped out by a pool eviction) it blanks
//  the display and leaves kbFocus = $ff. The shell then renders into its REU
//  slot with nothing on screen ("shell responses are not echoed").
//  Switching the view to our parent (the shell) BEFORE kFreeScreen moves the
//  display and kbFocus while both surfaces are still intact, so the hand-off
//  cannot fail -- it works even when the shell is swapped out, because the
//  framebuffer decision keys on the SURFACE (surfDisplayed/resolve).
//  No-op when we do not hold the keyboard focus. Clobbers A, X.
edGiveDisplay:
    lda #SYS_GETTID
    jsr syscall                 // A = own slot
    cmp kbFocus
    bne edGdRts                 // we do not hold the display -- nothing to hand back
    tax
    lda taskParent,x            // our spawner = the shell
    cmp #$ff
    beq edGdRts                 // no parent recorded -- leave it to sysExit's hand-off
    tax
    lda taskState,x
    beq edGdRts                 // parent is dead -- sysExit's hand-off decides
    jsr kSwitchView             // display + kbFocus move to the parent
edGdRts:
    rts

edQuit:
    jsr edGiveDisplay           // hand the display back before freeing it
    jsr edFreeChunks            // release every document chunk
    jsr kFreeScreen             // release the surface (clean exit)
    lda #SYS_EXIT
    jsr syscall                 // exit -- never returns

// --- edFreeChunks -- kFree every chunk in the table ---
edFreeChunks:
    ldy #ED_CHUNKS
    lda (dataPtr),y
    ldy #ED_TMPL
    sta (dataPtr),y              // count
edFreeLp:
    ldy #ED_TMPL
    lda (dataPtr),y
    beq edFreeRts
    sec
    sbc #1
    sta (dataPtr),y              // index = count-1
    asl
    clc
    adc #ED_CHKTAB
    tay
    lda (dataPtr),y
    sta kPtr
    iny
    lda (dataPtr),y
    sta kPtr+1
    jsr kFree
    jmp edFreeLp
edFreeRts:
    rts

// --- edClear -- collapse the document to one blank line (keep the filename) ---
//  Frees chunks 1..N-1 (chunk 0 stays), resets the line/cursor/window state,
//  blanks record 0, marks modified and sets ED_FLAG for a full redraw.
//  Clobbers A, X, Y, kPtr.
edClear:
    ldy #ED_CHUNKS              // free chunks ED_CHUNKS-1 .. 1
    lda (dataPtr),y
    sec
    sbc #1
    ldy #ED_TMPL
    sta (dataPtr),y             // index = highest chunk to free
edClearLp:
    ldy #ED_TMPL
    lda (dataPtr),y
    beq edClearKeep             // 0 -> only chunk 0 left
    asl
    clc
    adc #ED_CHKTAB
    tay
    lda (dataPtr),y
    sta kPtr
    iny
    lda (dataPtr),y
    sta kPtr+1
    jsr kFree
    ldy #ED_TMPL
    lda (dataPtr),y
    sec
    sbc #1
    sta (dataPtr),y
    jmp edClearLp
edClearKeep:
    lda #1                      // one blank line in chunk 0
    ldy #ED_CHUNKS
    sta (dataPtr),y
    ldy #ED_LINES
    sta (dataPtr),y
    lda #0
    ldy #ED_SIZE
    sta (dataPtr),y
    iny
    sta (dataPtr),y             // size = 0
    ldy #ED_CURL
    sta (dataPtr),y             // cursor (0,0)
    ldy #ED_CURC
    sta (dataPtr),y
    ldy #ED_PREFC
    sta (dataPtr),y
    ldy #ED_WIN
    sta (dataPtr),y
    iny
    sta (dataPtr),y             // winTop = 0
    jsr edRecCur                // kPtr = record 0
    ldy #REC_SIZE
    lda #0
edClearRec:
    dey
    sta (kPtr),y
    bne edClearRec
    jsr edModSet                // clearing is an edit
    lda #1
    ldy #ED_FLAG
    sta (dataPtr),y             // full redraw
    rts

// ==========================================
//  Record layer
// ==========================================
//  edRecAddr -- kPtr = address of record `line` (0..191)
//  Resolved through the chunk table on every call -- never cached.
//  Clobbers A, X, Y, kPtr.
edRecAddr:
    tax                          // X = line
    ldy #0                       // chunk index
edRecChunk:
    cpx #RECS_PER_CHUNK
    bcc edRecHave                // line < 24 -> chunk found
    iny
    txa
    sec
    sbc #RECS_PER_CHUNK
    tax
    jmp edRecChunk
edRecHave:
    txa                          // offset = line x 41 (16-bit -> kPtr)
    lsr
    lsr
    lsr
    sta kPtr+1                   // hi = line >> 3 (line < 24 -> 0..2)
    txa
    asl
    asl
    asl
    asl
    asl
    sta kPtr                     // lo = (line << 5) & $FF
    txa
    asl
    asl
    asl                          // line x 8
    clc
    adc kPtr
    sta kPtr
    bcc edRecC1
    inc kPtr+1
edRecC1:
    txa
    clc
    adc kPtr
    sta kPtr
    bcc edRecC2
    inc kPtr+1
edRecC2:
    tya                          // + chunk base from the table
    asl
    clc
    adc #ED_CHKTAB
    tay
    lda (dataPtr),y
    clc
    adc kPtr
    sta kPtr
    iny
    lda (dataPtr),y
    adc kPtr+1
    sta kPtr+1
    rts

// edRecCur -- kPtr = record at the cursor line (clobbers A, X, Y, kPtr)
edRecCur:
    ldy #ED_CURL
    lda (dataPtr),y
    jmp edRecAddr

// edLastLine -- A = last document line = ED_LINES - 1
edLastLine:
    ldy #ED_LINES
    lda (dataPtr),y
    sec
    sbc #1
    rts

// edSizeInc / edSizeDec / edModSet -- small document-state helpers
edModSet:
    lda #1
    ldy #ED_MOD
    sta (dataPtr),y
    rts
edSizeInc:
    ldy #ED_SIZE
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    bcc edSizeRts
    iny
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y
edSizeRts:
    rts
edSizeDec:
    ldy #ED_SIZE+1
    lda (dataPtr),y
    bne edSizeDecHi
    ldy #ED_SIZE
    lda (dataPtr),y
    beq edSizeRts                // 0 -> stay
    sec
    sbc #1
    sta (dataPtr),y
    rts
edSizeDecHi:
    ldy #ED_SIZE
    lda (dataPtr),y
    sec
    sbc #1
    sta (dataPtr),y
    ldy #ED_SIZE+1
    lda (dataPtr),y
    sbc #0
    sta (dataPtr),y
    rts

// ==========================================
//  Edit core
// ==========================================
// edInsChar -- A = char; insert at (ED_CURL, ED_CURC).
//  Full line: the typed char lands at the cursor; the last char cascades
//  to the line below (at the last column the cursor follows it down).
//  Not-full line at the last column: append + wrap to a fresh line below.
//  Clobbers A, X, Y, kPtr, srcPtr.
edInsChar:
    ldy #ED_TCHAR
    sta (dataPtr),y              // stash the char
    jsr edClampCurCol
    ldy #0
    lda (kPtr),y                 // len
    cmp #40
    bcc edInsNotFull             // len < 40 -> normal insert / append
    ldy #ED_CURC
    lda (dataPtr),y
    cmp #39
    bcc edInsOverflowGo          // full, col < 39 -> cascade in place
    lda #39
    sta (dataPtr),y              // col 39+ -> clamp to the last column
edInsOverflowGo:
    jmp edInsOverflow            // far target -- branch out of range
edInsNotFull:
    ldy #ED_CURC
    lda (dataPtr),y
    cmp #39
    bne edInsShift               // mid-line -> normal insert
    jmp edInsAppend              // last column, was empty -> append + wrap
edInsShift:
    ldy #0                       // i = min(len, 39)
    lda (kPtr),y
    cmp #40
    bcc edInsShSet
    lda #39                      // full line -> the 40th char drops
edInsShSet:
    ldy #ED_TMPL
    sta (dataPtr),y
edInsShLp:                       // while i > col: rec[i+1] = rec[i]
    ldy #ED_TMPL
    lda (dataPtr),y
    ldy #ED_CURC
    cmp (dataPtr),y              // i vs col
    beq edInsShDone              // i == col -> done
    bcc edInsShDone              // i < col -> done
    ldy #ED_TMPL
    lda (dataPtr),y
    tay
    lda (kPtr),y
    iny
    sta (kPtr),y
    ldy #ED_TMPL
    lda (dataPtr),y
    sec
    sbc #1
    sta (dataPtr),y
    jmp edInsShLp
edInsShDone:
    ldy #ED_TCHAR               // rec[col+1] = char
    lda (dataPtr),y
    pha
    ldy #ED_CURC
    lda (dataPtr),y
    tay
    iny
    pla
    sta (kPtr),y
    ldy #0
    lda (kPtr),y                 // len++ (unless full)
    cmp #40
    beq edInsLenOk
    clc
    adc #1
    sta (kPtr),y
edInsLenOk:
    jsr edSizeInc
    jsr edModSet
    ldy #ED_CURC                 // col++ (clamp 39)
    lda (dataPtr),y
    cmp #39
    beq edInsDone
    clc
    adc #1
    sta (dataPtr),y
edInsDone:
    ldy #ED_CURC
    lda (dataPtr),y
    ldy #ED_PREFC
    sta (dataPtr),y
    rts

// edInsAppend -- append at the last column of a NOT-full line: the char
//  lands at col 39 (line becomes full), then a blank line is opened below
//  and the cursor moves down to it. Clobbers A, X, Y, kPtr, srcPtr.
edInsAppend:
    jsr edInsShift               // append at col 39 (line becomes full)
    ldy #ED_CURL
    lda (dataPtr),y
    clc
    adc #1
    jsr edInsLine                // blank line below; C=1 -> refused
    bcc edInsAppendGo
    rts                          // pool full -> cursor stays on the full line
edInsAppendGo:
    lda #1
    ldy #ED_FLAG
    sta (dataPtr),y              // lines shifted -> full redraw
    ldy #ED_CURL
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y              // ED_CURL++
    lda #0
    ldy #ED_CURC
    sta (dataPtr),y              // col = 0
    ldy #ED_PREFC
    sta (dataPtr),y
    rts

// --- edInsOverflow -- insert into a FULL line (len=40) ---
//  The typed char lands at the cursor; the tail shifts right and its
//  last char cascades down to the next line's col 0 (recurring through
//  full lines; a new line is created at the bottom if needed). At the
//  last column the cursor follows the displaced char down to the line below.
//  Clobbers A, X, Y, kPtr, srcPtr.
edInsOverflow:
    ldy #ED_CURC
    lda (dataPtr),y              // original col (0..39)
    pha                          // save across the cascade
    ldy #40
    lda (kPtr),y                 // overflow = the line's last char
    ldy #ED_TMPH
    sta (dataPtr),y
    jsr edInsShift               // insert + shift (len stays 40)
    ldy #ED_CURL                 // L = line below
    lda (dataPtr),y
    clc
    adc #1
    ldy #ED_TMPL
    sta (dataPtr),y
edInsCascLp:
    ldy #ED_TMPL
    lda (dataPtr),y              // L
    ldy #ED_LINES
    cmp (dataPtr),y
    bcc edInsCascHave            // L < ED_LINES -> line exists
    ldy #ED_TMPL                 // create a new line at L
    lda (dataPtr),y              // A = L
    pha                          // save L -- edInsLine clobbers ED_TMPL
    jsr edInsLine
    pla                          // restore L (PLA preserves carry)
    bcs edInsCascDone            // refused -> drop overflow (pool full)
    jsr edRecAddr                // kPtr = record L
    ldy #0
    lda #1
    sta (kPtr),y                 // len = 1
    ldy #ED_TMPH
    lda (dataPtr),y              // overflow
    ldy #1
    sta (kPtr),y
    jmp edInsCascDone
edInsCascHave:
    ldy #ED_TMPL
    lda (dataPtr),y              // A = L
    jsr edRecAddr                // kPtr = record at L
    ldy #0
    lda (kPtr),y                 // len
    cmp #40
    bcs edInsCascFull            // full -> cascade further
    tay                          // Y = len
    beq edInsCascPut             // empty line -> no shift
edInsCascShift:                  // rec[j+1] = rec[j], j = len..1
    lda (kPtr),y
    iny
    sta (kPtr),y
    dey
    dey
    bne edInsCascShift
edInsCascPut:
    ldy #ED_TMPH
    lda (dataPtr),y              // overflow
    ldy #1
    sta (kPtr),y                 // rec[1] = overflow
    ldy #0
    lda (kPtr),y
    clc
    adc #1
    sta (kPtr),y                 // len++
    jmp edInsCascDone
edInsCascFull:
    ldy #40
    lda (kPtr),y                 // new overflow
    pha
    ldy #39
edInsCascFullShift:              // rec[j+1] = rec[j], j = 39..1
    lda (kPtr),y
    iny
    sta (kPtr),y
    dey
    dey
    bne edInsCascFullShift
    ldy #ED_TMPH
    lda (dataPtr),y              // old overflow
    ldy #1
    sta (kPtr),y                 // rec[1] = old overflow
    pla                          // new overflow
    ldy #ED_TMPH
    sta (dataPtr),y              // ED_TMPH = new overflow
    ldy #ED_TMPL
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y              // L++
    jmp edInsCascLp
edInsCascDone:
    pla                          // original col
    cmp #39
    bne edInsCascStay            // mid-line insert -> cursor stays (C+1)
    ldy #ED_CURL                 // last column -> cursor follows char down
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    lda #0                       // land ON the displaced char (col 0)
    ldy #ED_CURC
    sta (dataPtr),y
    ldy #ED_PREFC
    sta (dataPtr),y
edInsCascStay:
    lda #1
    ldy #ED_FLAG
    sta (dataPtr),y              // full redraw
    rts

// edOvChar -- A = char; overwrite at (ED_CURL, ED_CURC).
edOvChar:
    ldy #ED_TCHAR
    sta (dataPtr),y
    jsr edClampCurCol
    ldy #ED_CURC
    lda (dataPtr),y              // col
    ldy #0
    cmp (kPtr),y                 // col vs len
    bcc edOvIn
    lda (kPtr),y                 // col == len -> append
    clc
    adc #1
    sta (kPtr),y
    jsr edSizeInc
edOvIn:
    ldy #ED_TCHAR
    lda (dataPtr),y
    pha
    ldy #ED_CURC
    lda (dataPtr),y
    tay
    iny
    pla
    sta (kPtr),y                 // rec[col+1] = char
    jsr edModSet
    ldy #ED_CURC
    lda (dataPtr),y
    cmp #39
    bcc edOvInc
    ldy #0                       // col 39: wrap only off a full line
    lda (kPtr),y
    cmp #40
    bne edOvRts
    jsr edLastLine               // A = last line
    ldy #ED_CURL
    cmp (dataPtr),y              // last line vs cursor
    beq edOvNewLine              // at the last line -> grow a fresh line
    bcc edOvRts                  // defensive: cursor past the last line
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y              // ED_CURL++
    lda #1
    ldy #ED_FLAG
    sta (dataPtr),y              // overwrote the previous line -> full redraw
    lda #0
    ldy #ED_CURC
    sta (dataPtr),y
    jmp edPrefSet
edOvNewLine:
    ldy #ED_CURL                 // no line below -> insert a blank one
    lda (dataPtr),y
    clc
    adc #1
    jsr edInsLine                // C=1 -> refused (pool full)
    bcc edOvNewGo
    rts                          // refused -> cursor stays on the full line
edOvNewGo:
    lda #1
    ldy #ED_FLAG
    sta (dataPtr),y              // lines shifted -> full redraw
    ldy #ED_CURL
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y              // ED_CURL++
    lda #0
    ldy #ED_CURC
    sta (dataPtr),y
    jmp edPrefSet
edOvInc:
    clc
    adc #1
    ldy #ED_CURC
    sta (dataPtr),y
edPrefSet:
    ldy #ED_CURC
    lda (dataPtr),y
    ldy #ED_PREFC
    sta (dataPtr),y
edOvRts:
    rts

// edDelDispatch -- DEL: continue the cascade if active, else normal delete.
edDelDispatch:
    ldy #ED_CASC
    lda (dataPtr),y
    beq edDelNormal
    jmp edCascadeStep
edDelNormal:
    jmp edDelChar

// edDelChar -- DEL: erase the cell left of the cursor (record semantics).
edDelChar:
    jsr edClampCurCol
    ldy #ED_CURC
    lda (dataPtr),y
    bne edDelMid                 // col > 0 -> delete char at col-1
    ldy #ED_CURL
    lda (dataPtr),y
    beq edDelHome                // (0,0) -> nothing
    jmp edDelJoin
edDelHome:
    rts
edDelMid:
    jsr edRecCur
    ldy #ED_CURC                 // i = col
    lda (dataPtr),y
    ldy #ED_TMPL
    sta (dataPtr),y
    ldy #0
    lda (kPtr),y                 // ED_SCR = len (loop bound)
    ldy #ED_SCR
    sta (dataPtr),y
edDelShLp:                       // while i <= len: rec[i] = rec[i+1]
    ldy #ED_TMPL
    lda (dataPtr),y
    ldy #ED_SCR
    cmp (dataPtr),y
    beq edDelShGo
    bcc edDelShGo
    jmp edDelShDone
edDelShGo:
    ldy #ED_TMPL
    lda (dataPtr),y
    tay
    iny
    lda (kPtr),y
    dey
    sta (kPtr),y
    ldy #ED_TMPL
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    jmp edDelShLp
edDelShDone:
    ldy #0
    lda (kPtr),y
    sec
    sbc #1
    sta (kPtr),y                 // len--
    jsr edSizeDec
    jsr edModSet
    ldy #ED_CURC
    lda (dataPtr),y
    sec
    sbc #1
    sta (dataPtr),y              // col--
    ldy #ED_PREFC
    sta (dataPtr),y
    rts
edDelJoin:                       // col 0: join with the previous line
    jsr edRecCur                 // kPtr = current record
    ldy #0
    lda (kPtr),y                 // curLen -> ED_SCR
    ldy #ED_SCR
    sta (dataPtr),y
    ldy #ED_CURL
    lda (dataPtr),y
    sec
    sbc #1
    jsr edRecAddr                // kPtr = previous record
    ldy #0
    lda (kPtr),y                 // prevLen
    ldy #ED_SCR+1
    sta (dataPtr),y              // save join-point column
    ldy #ED_SCR
    clc
    adc (dataPtr),y              // prevLen + curLen
    cmp #41
    bcs edRefFull                // > 40 -> cascade up
    jmp edJoinOk                 // <= 40 -> join
edRefFull:
    // > 40 -> cascade: pull the below line's chars up into the line
    // above, one per press. Move the cursor to the line above and run
    // the first step (edCascadeStep handles fill vs roll).
    ldy #ED_CURL
    lda (dataPtr),y
    sec
    sbc #1
    sta (dataPtr),y              // ED_CURL = line above
    ldy #ED_SCR+1                // prevLen
    lda (dataPtr),y
    ldy #ED_CASC
    sta (dataPtr),y              // ED_CASC = prevLen (delete col = prevLen-1)
    jmp edCascadeStep
edJoinOk:
    lda kPtr                     // ED_REC = previous record
    ldy #ED_REC
    sta (dataPtr),y
    lda kPtr+1
    iny
    sta (dataPtr),y
    jsr edRecCur                 // srcPtr = current record + 1
    lda kPtr
    sta srcPtr
    lda kPtr+1
    sta srcPtr+1
    inc srcPtr
    bne edJoin1
    inc srcPtr+1
edJoin1:
    ldy #ED_REC                  // kPtr = previous record, at rec[1+prevLen]
    lda (dataPtr),y
    sta kPtr
    iny
    lda (dataPtr),y
    sta kPtr+1
    ldy #0
    lda (kPtr),y                 // prevLen
    clc
    adc kPtr
    sta kPtr
    bcc edJoin2
    inc kPtr+1
edJoin2:
    inc kPtr                     // append after the length byte
    bne edJoin2a
    inc kPtr+1
edJoin2a:
    ldy #ED_SCR                  // count = curLen
    lda (dataPtr),y
    ldy #ED_TMPL
    sta (dataPtr),y
edJoinLp:
    ldy #ED_TMPL
    lda (dataPtr),y
    beq edJoinDone
    ldy #0
    lda (srcPtr),y
    sta (kPtr),y
    inc srcPtr
    bne edJoin3
    inc srcPtr+1
edJoin3:
    inc kPtr
    bne edJoin4
    inc kPtr+1
edJoin4:
    ldy #ED_TMPL
    lda (dataPtr),y
    sec
    sbc #1
    sta (dataPtr),y
    jmp edJoinLp
edJoinDone:
    ldy #ED_REC                  // prev.len = prevLen + curLen
    lda (dataPtr),y
    sta kPtr
    iny
    lda (dataPtr),y
    sta kPtr+1
    ldy #0
    lda (kPtr),y
    ldy #ED_SCR
    clc
    adc (dataPtr),y
    ldy #0
    sta (kPtr),y
    ldy #ED_CURL                 // delete the current line
    lda (dataPtr),y
    jsr edDelLine
    jsr edModSet
    lda #1
    ldy #ED_FLAG
    sta (dataPtr),y              // lines shifted -> full redraw
    ldy #ED_CURL
    lda (dataPtr),y
    sec
    sbc #1
    sta (dataPtr),y              // cursor -> joined line
    ldy #ED_SCR+1                // col = original previous length
    lda (dataPtr),y
    cmp #40
    bcc edJoinColOk              // < 40 -> use as-is
    lda #39                      // full previous line -> last valid column
edJoinColOk:
    ldy #ED_CURC
    sta (dataPtr),y
    ldy #ED_PREFC
    sta (dataPtr),y
    rts

// --- edCascadeStep -- one step of the backspace cascade ---
//  Upper line U = ED_CURL, below line B = U+1. Pulls B's first char up
//  into U's end: appends while U has room, otherwise deletes U's char at
//  the cascade column (ED_CASC-1) and rolls. Deletes B when it empties.
//  Clobbers A, X, Y, kPtr, srcPtr.
edCascadeStep:
    jsr edRecCur                 // kPtr = U's record
    lda kPtr
    ldy #ED_REC
    sta (dataPtr),y
    lda kPtr+1
    iny
    sta (dataPtr),y              // ED_REC = U's record
    ldy #ED_CURL                 // srcPtr = B's record
    lda (dataPtr),y
    clc
    adc #1
    jsr edRecAddr
    lda kPtr
    sta srcPtr
    lda kPtr+1
    sta srcPtr+1
    ldy #1
    lda (srcPtr),y               // C0 = B's first char
    ldy #ED_TCHAR
    sta (dataPtr),y
    ldy #ED_REC                  // kPtr = U's record
    lda (dataPtr),y
    sta kPtr
    iny
    lda (dataPtr),y
    sta kPtr+1
    ldy #0
    lda (kPtr),y                 // U's len
    cmp #40
    bcc edCascAppend             // < 40 -> A = old len (cursor col)
    ldy #ED_CASC                 // roll: delete U's col K
    lda (dataPtr),y
    sec
    sbc #1                       // A = K
    tay
    iny                          // Y = K+1 (record offset)
edCascDelLp:                     // rec[Y] = rec[Y+1] while Y < 40
    cpy #40
    bcs edCascDelDone
    iny
    lda (kPtr),y
    dey
    sta (kPtr),y
    iny
    jmp edCascDelLp
edCascDelDone:
    ldy #ED_CASC
    lda (dataPtr),y
    sec
    sbc #1
    sta (dataPtr),y              // K--
    ldy #0
    lda (kPtr),y
    sec
    sbc #1
    sta (kPtr),y                 // U's len-- (the deleted char)
    jsr edSizeDec                // the deleted char is lost
    ldy #ED_CASC
    lda (dataPtr),y              // A = K (cursor follows the rolled char)
edCascAppend:
    pha                          // [cursor col]
    ldy #ED_TCHAR
    lda (dataPtr),y              // A = C0
    pha
    ldy #0
    lda (kPtr),y                 // U's len
    clc
    adc #1
    sta (kPtr),y                 // U's len++
    tay                          // Y = new len (record offset)
    pla                          // A = C0
    sta (kPtr),y                 // rec[new len] = C0
    ldy #0
    lda (srcPtr),y               // B's len
    ldy #ED_TMPL
    sta (dataPtr),y              // ED_TMPL = B's len (shift bound)
    ldx #1
edCascShiftB:                    // rec[j] = rec[j+1] for j = 1..len-1
    txa
    ldy #ED_TMPL
    cmp (dataPtr),y              // j vs len
    bcs edCascShiftBDone
    txa
    tay
    iny
    lda (srcPtr),y
    dey
    sta (srcPtr),y
    inx
    jmp edCascShiftB
edCascShiftBDone:
    ldy #0
    lda (srcPtr),y
    sec
    sbc #1
    sta (srcPtr),y               // B's len--
    bne edCascSetCur             // B not empty -> continue
    ldy #ED_CURL                 // B empty -> delete it, end cascade
    lda (dataPtr),y
    clc
    adc #1
    jsr edDelLine
    lda #0
    ldy #ED_CASC
    sta (dataPtr),y
edCascSetCur:
    jsr edModSet
    jsr edRecCur                 // kPtr = U's record
    pla                          // A = cursor col
    ldy #ED_CURC
    sta (dataPtr),y              // col = cursor col
    ldy #ED_PREFC
    sta (dataPtr),y
    lda #1
    ldy #ED_FLAG
    sta (dataPtr),y              // full redraw
    rts

// edReturn -- split the current line at the cursor (plan 5.4).
edReturn:
    ldy #ED_CURL
    lda (dataPtr),y
    clc
    adc #1
    jsr edInsLine                // insert blank record below; C=1 -> refused
    bcc edRetGo
    rts
edRetGo:
    jsr edClampCurCol            // splitting past EOL should behave as EOL
    ldy #ED_CURC
    lda (dataPtr),y              // col
    ldy #0
    cmp (kPtr),y                 // col vs len
    bcc edRetSplit               // col < len -> split
    jmp edRetMove                // col >= len -> nothing to move (far)
edRetSplit:
    ldy #ED_CURC
    lda (dataPtr),y
    ldy #ED_TMPH
    sta (dataPtr),y              // splitPos = col
    ldy #0
    lda (kPtr),y                 // count = len - splitPos
    ldy #ED_TMPH
    sec
    sbc (dataPtr),y
    ldy #ED_SCR
    sta (dataPtr),y              // save count; ED_TMPL is consumed by the copy loop
    ldy #ED_TMPL
    sta (dataPtr),y
    ldy #ED_TMPH                 // srcPtr = current record + splitPos + 1
    lda (dataPtr),y
    clc
    adc #1
    clc
    adc kPtr
    sta srcPtr
    lda kPtr+1
    adc #0
    sta srcPtr+1
    ldy #ED_CURL                 // kPtr = new record + 1
    lda (dataPtr),y
    clc
    adc #1
    jsr edRecAddr
    inc kPtr
    bne edRet2
    inc kPtr+1
edRet2:
edRetCpLp:
    ldy #ED_TMPL
    lda (dataPtr),y
    beq edRetCpDone
    ldy #0
    lda (srcPtr),y
    sta (kPtr),y
    inc srcPtr
    bne edRet3
    inc srcPtr+1
edRet3:
    inc kPtr
    bne edRet4
    inc kPtr+1
edRet4:
    ldy #ED_TMPL
    lda (dataPtr),y
    sec
    sbc #1
    sta (dataPtr),y
    jmp edRetCpLp
edRetCpDone:
    ldy #ED_CURL                 // new.len = count
    lda (dataPtr),y
    clc
    adc #1
    jsr edRecAddr
    ldy #ED_SCR
    lda (dataPtr),y
    ldy #0
    sta (kPtr),y
    jsr edRecCur                 // cur.len = splitPos
    ldy #ED_TMPH
    lda (dataPtr),y
    ldy #0
    sta (kPtr),y
edRetMove:
    jsr edModSet
    lda #1
    ldy #ED_FLAG
    sta (dataPtr),y              // lines shifted -> full redraw
    ldy #ED_CURL
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y              // ED_CURL++
    lda #0
    ldy #ED_CURC
    sta (dataPtr),y
    ldy #ED_PREFC
    sta (dataPtr),y
    rts

// ==========================================
//  Grow / shrink
// ==========================================
// --- edM41 -- kPtr = X x 41 (X preserved; A, kPtr clobbered) ---
//  X is 0..23 -> result 0..943 (16-bit).
edM41:
    txa
    lsr
    lsr
    lsr
    sta kPtr+1                   // hi = X >> 3
    txa
    asl
    asl
    asl
    asl
    asl
    sta kPtr                     // lo = (X << 5) & $FF
    txa
    asl
    asl
    asl
    clc
    adc kPtr
    sta kPtr
    bcc edM41A
    inc kPtr+1
edM41A:
    txa
    clc
    adc kPtr
    sta kPtr
    bcc edM41B
    inc kPtr+1
edM41B:
    rts

// --- edRunDn -- shift `count` records down one slot within a chunk ---
//  Input:  ED_REC = chunk base, ED_DIDX = lo (first record),
//          ED_TCHAR = count (records to move).
//  Moves recs lo..lo+count-1 -> lo+1..lo+count via the REU bounce+64.
//  Caller holds SEI. Clobbers A, X, Y, kPtr, kArg..kArg+5; no ED_* field is
//  touched, so a caller may hold instance state across it.
edRunDn:
    ldy #ED_TCHAR                // count
    lda (dataPtr),y
    beq edRunDnRts               // count 0 -> nothing to move (REU len 0 = 64KB!)
    tax
    jsr edM41                    // kPtr = count x 41 = DMA length
    lda kPtr
    pha
    lda kPtr+1
    pha                          // park the length on the task stack (kPtr is reused)
    ldy #ED_DIDX                 // kPtr = base + lox41 (source)
    lda (dataPtr),y
    tax
    jsr edM41
    ldy #ED_REC
    lda (dataPtr),y
    clc
    adc kPtr
    sta kPtr
    iny
    lda (dataPtr),y
    adc kPtr+1
    sta kPtr+1
    lda #<(ED_REU_BOUNCE+64)     // source -> bounce+64
    sta kArg
    lda #>(ED_REU_BOUNCE+64)
    sta kArg+1
    lda #0
    sta kArg+2
    sta kArg+5
    pla
    sta kArg+4
    pla
    sta kArg+3
    jsr kReuFromC64
    lda kPtr                     // dest = source + 41
    clc
    adc #REC_SIZE
    sta kPtr
    bcc edRunDnA
    inc kPtr+1
edRunDnA:
    lda #<(ED_REU_BOUNCE+64)     // bounce+64 -> dest
    sta kArg
    lda #>(ED_REU_BOUNCE+64)
    sta kArg+1
    lda #0
    sta kArg+2
    sta kArg+5                   // kArg+3/+4 still hold the length
    jsr kReuToC64
edRunDnRts:
    rts

// --- edRunUp -- shift `count` records up one slot within a chunk ---
//  Input:  ED_REC = chunk base, ED_DIDX = lo (first record),
//          ED_TCHAR = count (records to move).
//  Moves recs lo+1..lo+count -> lo..lo+count-1 via the REU bounce+64.
//  Caller holds SEI. Clobbers A, X, Y, kPtr, kArg..kArg+5; no ED_* field is
//  touched, so a caller may hold instance state across it.
edRunUp:
    ldy #ED_TCHAR                // count
    lda (dataPtr),y
    bne edRunUpGo                // count > 0 -> shift
    jmp edRunUpRts               // count 0 -> nothing to move (REU len 0 = 64KB!)
edRunUpGo:
    tax
    jsr edM41                    // kPtr = count x 41 = DMA length
    lda kPtr
    pha
    lda kPtr+1
    pha                          // park the length on the task stack (kPtr is reused)
    ldy #ED_DIDX                 // kPtr = base + lox41 (dest)
    lda (dataPtr),y
    tax
    jsr edM41
    ldy #ED_REC
    lda (dataPtr),y
    clc
    adc kPtr
    sta kPtr
    iny
    lda (dataPtr),y
    adc kPtr+1
    sta kPtr+1
    lda kPtr                     // source = dest + 41
    clc
    adc #REC_SIZE
    sta kPtr
    bcc edRunUpA
    inc kPtr+1
edRunUpA:
    lda #<(ED_REU_BOUNCE+64)     // source -> bounce+64
    sta kArg
    lda #>(ED_REU_BOUNCE+64)
    sta kArg+1
    lda #0
    sta kArg+2
    sta kArg+5
    pla
    sta kArg+4
    pla
    sta kArg+3
    jsr kReuFromC64
    lda kPtr                     // dest = source - 41
    sec
    sbc #REC_SIZE
    sta kPtr
    bcs edRunUpB
    dec kPtr+1
edRunUpB:
    lda #<(ED_REU_BOUNCE+64)     // bounce+64 -> dest
    sta kArg
    lda #>(ED_REU_BOUNCE+64)
    sta kArg+1
    lda #0
    sta kArg+2
    sta kArg+5                   // kArg+3/+4 still hold the length
    jsr kReuToC64
edRunUpRts:
    rts

// edInsLine -- A = insertion index (0..192): insert a blank record there.
//  Shifts records down, grows the chunk table when full, blanks the new
//  record, ED_LINES++. C=1 -> refused (pool full / 8 chunks), nothing
//  changed. Clobbers A, X, Y, kPtr, kArg.
edInsLine:
    ldy #ED_SCR
    sta (dataPtr),y              // insertion line
    ldy #ED_CHUNKS               // capacity = chunks x 24
    lda (dataPtr),y
    asl
    asl
    asl
    ldy #ED_TMPL
    sta (dataPtr),y
    asl
    clc
    adc (dataPtr),y
    ldy #ED_LINES
    cmp (dataPtr),y              // capacity vs lines
    beq edInsGrowChk
    jmp edInsNoGrow
edInsGrowChk:
    ldy #ED_CHUNKS
    lda (dataPtr),y
    cmp #MAX_CHUNKS
    bcc edInsTryGrow
    jmp edInsFail                // 8 chunks -> refuse
edInsTryGrow:
    lda #CHUNK_PAGES
    jsr kMalloc
    bcc edInsGrowOk
    jmp edInsFail
edInsGrowOk:
    ldy #ED_CHUNKS               // append the new chunk base
    lda (dataPtr),y
    asl
    clc
    adc #ED_CHKTAB
    tay
    lda kPtr
    sta (dataPtr),y
    iny
    lda kPtr+1
    sta (dataPtr),y
    ldy #ED_CHUNKS
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
edInsNoGrow:
    // --- chunk-granular REU shift ---
    //  Zero-shift guard: L == ED_LINES (appending at the end) moves nothing.
    //  Otherwise compute lastChunk/lastRec and firstChunk/firstRec, then walk
    //  the chunks top-down shifting each run through the REU bounce; the
    //  rec-23 overflow of chunk C carries to rec 0 of chunk C+1.
    ldy #ED_SCR
    lda (dataPtr),y              // insertion line L
    ldy #ED_LINES
    cmp (dataPtr),y              // L vs ED_LINES
    bcc edInsBounds              // L < ED_LINES -> shift
    jmp edInsRecDone             // L == ED_LINES -> nothing to move
edInsBounds:
    ldy #ED_LINES
    lda (dataPtr),y
    sec
    sbc #1                       // A = last line
    ldy #0
edInsLastC:
    cmp #RECS_PER_CHUNK
    bcc edInsLastCDone
    iny
    sec
    sbc #RECS_PER_CHUNK
    jmp edInsLastC
edInsLastCDone:
    tax                          // X = lastRec
    tya                          // A = lastChunk
    ldy #ED_DIG+1
    sta (dataPtr),y              // ED_DIG+1 = lastChunk
    txa                          // A = lastRec
    ldy #ED_DIG
    sta (dataPtr),y              // ED_DIG = lastRec
    ldy #ED_SCR
    lda (dataPtr),y              // insertion line L
    ldy #0
edInsFirstC:
    cmp #RECS_PER_CHUNK
    bcc edInsFirstCDone
    iny
    sec
    sbc #RECS_PER_CHUNK
    jmp edInsFirstC
edInsFirstCDone:
    tax                          // X = firstRec
    tya                          // A = firstChunk
    ldy #ED_DCNT
    sta (dataPtr),y              // ED_DCNT = firstChunk
    txa                          // A = firstRec
    ldy #ED_DIG+2
    sta (dataPtr),y              // ED_DIG+2 = firstRec
    ldy #ED_DIG+1
    lda (dataPtr),y              // A = lastChunk
    ldy #ED_TMPL
    sta (dataPtr),y              // ED_TMPL = C = lastChunk
    sei                          // REU bounce is shared -- atomic
edInsChunkLp:
    ldy #ED_TMPL                 // chunk base -> ED_REC
    lda (dataPtr),y
    asl
    clc
    adc #ED_CHKTAB
    tay
    lda (dataPtr),y              // chunkTable[C] lo
    tax                          // X = base lo
    iny
    lda (dataPtr),y              // chunkTable[C] hi
    ldy #ED_REC+1
    sta (dataPtr),y              // ED_REC+1 = base hi
    txa                          // A = base lo
    ldy #ED_REC
    sta (dataPtr),y              // ED_REC = base lo
    ldy #ED_TMPL                 // lo = firstRec at the first chunk, else 0
    lda (dataPtr),y
    ldy #ED_DCNT
    cmp (dataPtr),y
    bne edInsLo0
    ldy #ED_DIG+2
    lda (dataPtr),y
    jmp edInsLoSet
edInsLo0:
    lda #0
edInsLoSet:
    ldy #ED_DIDX
    sta (dataPtr),y              // ED_DIDX = lo
    ldy #ED_TMPL                 // hi = lastRec at the last chunk, else 23
    lda (dataPtr),y
    ldy #ED_DIG+1
    cmp (dataPtr),y
    bne edInsHi23
    ldy #ED_DIG
    lda (dataPtr),y
    jmp edInsHiSet
edInsHi23:
    lda #23
edInsHiSet:
    ldy #ED_CARRY
    sta (dataPtr),y              // ED_CARRY = hi
    cmp #23                      // carry? rec 23 -> next chunk's rec 0
    bne edInsNoCarry
    ldy #ED_REC                  // kPtr = rec 23 of C (offset 23x41 = 943)
    lda (dataPtr),y
    clc
    adc #<943
    sta kPtr
    iny
    lda (dataPtr),y
    adc #>943
    sta kPtr+1
    lda #<ED_REU_BOUNCE
    sta kArg
    lda #>ED_REU_BOUNCE
    sta kArg+1
    lda #0
    sta kArg+2
    sta kArg+5
    lda #REC_SIZE
    sta kArg+3
    lda #0
    sta kArg+4
    jsr kReuFromC64              // rec 23 -> bounce+0
edInsNoCarry:
    ldy #ED_CARRY                // count = hi-lo+1 (or 23-lo if carry)
    lda (dataPtr),y
    cmp #23
    beq edInsCnt23
    ldy #ED_DIDX
    sec
    sbc (dataPtr),y
    clc
    adc #1
    jmp edInsCntSet
edInsCnt23:
    lda #23
    ldy #ED_DIDX
    sec
    sbc (dataPtr),y
edInsCntSet:
    ldy #ED_TCHAR
    sta (dataPtr),y              // ED_TCHAR = count
    jsr edRunDn                  // shift the run down (bounce+64)
    ldy #ED_CARRY                // restore carry? bounce+0 -> C+1 rec 0
    lda (dataPtr),y
    cmp #23
    bne edInsChunkNext
    ldy #ED_TMPL                 // kPtr = chunk base of C+1
    lda (dataPtr),y
    clc
    adc #1
    asl
    clc
    adc #ED_CHKTAB
    tay
    lda (dataPtr),y
    sta kPtr
    iny
    lda (dataPtr),y
    sta kPtr+1
    lda #<ED_REU_BOUNCE
    sta kArg
    lda #>ED_REU_BOUNCE
    sta kArg+1
    lda #0
    sta kArg+2
    sta kArg+5
    lda #REC_SIZE
    sta kArg+3
    lda #0
    sta kArg+4
    jsr kReuToC64                // bounce+0 -> rec 0 of C+1
edInsChunkNext:
    ldy #ED_TMPL
    lda (dataPtr),y              // C vs firstChunk
    ldy #ED_DCNT
    cmp (dataPtr),y
    beq edInsShiftDone
    ldy #ED_TMPL
    lda (dataPtr),y
    sec
    sbc #1
    sta (dataPtr),y              // C--
    jmp edInsChunkLp
edInsShiftDone:
    cli
edInsRecDone:
    ldy #ED_SCR
    lda (dataPtr),y              // line
    jsr edRecAddr                // blank the inserted record
    lda #0
    tay
edInsZeroLp:
    sta (kPtr),y
    iny
    cpy #REC_SIZE
    bcc edInsZeroLp
    ldy #ED_LINES
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y              // ED_LINES++
    clc
    rts
edInsFail:
    sec
    rts

// edDelLine -- A = line: delete its record, shift up, shrink (5.5).
//  Clobbers A, X, Y, kPtr, kArg.
edDelLine:
    ldy #ED_SCR
    sta (dataPtr),y              // deletion line
    ldy #ED_LINES
    lda (dataPtr),y
    sec
    sbc #1                       // A = last line
    ldy #ED_SCR
    cmp (dataPtr),y              // last vs line (A preserved)
    bcc edDelZero                // last < line -> zero shift (defensive)
    beq edDelZero                // last == line -> zero shift
    jmp edDelBounds              // last > line -> shift
edDelZero:
    jmp edDelRecDone             // nothing to move -- straight to shrink
edDelBounds:
    ldy #0
edDelLastC:
    cmp #RECS_PER_CHUNK
    bcc edDelLastCDone
    iny
    sec
    sbc #RECS_PER_CHUNK
    jmp edDelLastC
edDelLastCDone:
    tax                          // X = lastRec
    tya                          // A = lastChunk
    ldy #ED_DIG+1
    sta (dataPtr),y              // ED_DIG+1 = lastChunk
    txa                          // A = lastRec
    ldy #ED_DIG
    sta (dataPtr),y              // ED_DIG = lastRec
    ldy #ED_SCR
    lda (dataPtr),y              // deletion line
    ldy #0
edDelFirstC:
    cmp #RECS_PER_CHUNK
    bcc edDelFirstCDone
    iny
    sec
    sbc #RECS_PER_CHUNK
    jmp edDelFirstC
edDelFirstCDone:
    tax                          // X = firstRec
    tya                          // A = firstChunk
    ldy #ED_DCNT
    sta (dataPtr),y              // ED_DCNT = firstChunk
    txa                          // A = firstRec
    ldy #ED_DIG+2
    sta (dataPtr),y              // ED_DIG+2 = firstRec
    ldy #ED_DCNT
    lda (dataPtr),y              // A = firstChunk
    ldy #ED_TMPL
    sta (dataPtr),y              // ED_TMPL = C = firstChunk
    sei                          // REU bounce is shared -- atomic
edDelChunkLp:
    ldy #ED_TMPL                 // chunk base -> ED_REC
    lda (dataPtr),y
    asl
    clc
    adc #ED_CHKTAB
    tay
    lda (dataPtr),y              // chunkTable[C] lo
    tax                          // X = base lo
    iny
    lda (dataPtr),y              // chunkTable[C] hi
    ldy #ED_REC+1
    sta (dataPtr),y              // ED_REC+1 = base hi
    txa                          // A = base lo
    ldy #ED_REC
    sta (dataPtr),y              // ED_REC = base lo
    ldy #ED_TMPL                 // lo = firstRec at the first chunk, else 0
    lda (dataPtr),y
    ldy #ED_DCNT
    cmp (dataPtr),y
    bne edDelLo0
    ldy #ED_DIG+2
    lda (dataPtr),y
    jmp edDelLoSet
edDelLo0:
    lda #0
edDelLoSet:
    ldy #ED_DIDX
    sta (dataPtr),y              // ED_DIDX = lo
    ldy #ED_TMPL                 // full chunk? C < lastChunk -> all 24 shift
    lda (dataPtr),y
    ldy #ED_DIG+1
    cmp (dataPtr),y
    bne edDelFullChunk
    jmp edDelLastChunk           // C == lastChunk -> no carry (too far to branch)
edDelFullChunk:
    // full chunk: count = 23 - lo; carry next chunk's rec 0 -> rec 23
    lda #23
    ldy #ED_DIDX
    sec
    sbc (dataPtr),y
    ldy #ED_TCHAR
    sta (dataPtr),y              // ED_TCHAR = count
    ldy #ED_TMPL                 // kPtr = chunk base of C+1 (rec 0)
    lda (dataPtr),y
    clc
    adc #1
    asl
    clc
    adc #ED_CHKTAB
    tay
    lda (dataPtr),y
    sta kPtr
    iny
    lda (dataPtr),y
    sta kPtr+1
    lda #<ED_REU_BOUNCE
    sta kArg
    lda #>ED_REU_BOUNCE
    sta kArg+1
    lda #0
    sta kArg+2
    sta kArg+5
    lda #REC_SIZE
    sta kArg+3
    lda #0
    sta kArg+4
    jsr kReuFromC64              // rec 0 of C+1 -> bounce+0
    jsr edRunUp                  // shift the run up (bounce+64)
    ldy #ED_REC                  // kPtr = rec 23 of C (offset 23x41 = 943)
    lda (dataPtr),y
    clc
    adc #<943
    sta kPtr
    iny
    lda (dataPtr),y
    adc #>943
    sta kPtr+1
    lda #<ED_REU_BOUNCE
    sta kArg
    lda #>ED_REU_BOUNCE
    sta kArg+1
    lda #0
    sta kArg+2
    sta kArg+5
    lda #REC_SIZE
    sta kArg+3
    lda #0
    sta kArg+4
    jsr kReuToC64                // bounce+0 -> rec 23 of C
    jmp edDelChunkNext
edDelLastChunk:
    // last chunk: count = lastRec - lo; rec lastRec is the top source (no carry)
    ldy #ED_DIG
    lda (dataPtr),y              // lastRec
    ldy #ED_DIDX
    sec
    sbc (dataPtr),y              // count = lastRec - lo
    ldy #ED_TCHAR
    sta (dataPtr),y
    jsr edRunUp                  // shift the run up (bounce+64)
edDelChunkNext:
    ldy #ED_TMPL
    lda (dataPtr),y              // C vs lastChunk
    ldy #ED_DIG+1
    cmp (dataPtr),y
    beq edDelShiftDone
    ldy #ED_TMPL
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y              // C++
    jmp edDelChunkLp
edDelShiftDone:
    cli
edDelRecDone:
    ldy #ED_LINES                // never below 1 line
    lda (dataPtr),y
    cmp #1
    beq edDelKeep
    sec
    sbc #1
    sta (dataPtr),y
edDelKeep:
    jsr edShrinkChunks
    rts

// edShrinkChunks -- free the trailing chunks that are surplus (5.5)
edShrinkChunks:
edShrinkLp:
    ldy #ED_CHUNKS
    lda (dataPtr),y
    cmp #1
    beq edShrinkRts              // only chunk 0 -- keep
    sec
    sbc #1                       // chunks - 1
    asl
    asl
    asl
    ldy #ED_TMPL
    sta (dataPtr),y
    asl
    clc
    adc (dataPtr),y              // (chunks-1) x 24
    ldy #ED_LINES
    cmp (dataPtr),y              // capacity vs lines
    bcc edShrinkRts              // still needed
    ldy #ED_CHUNKS               // free the last chunk
    lda (dataPtr),y
    sec
    sbc #1
    asl
    clc
    adc #ED_CHKTAB
    tay
    lda (dataPtr),y
    sta kPtr
    iny
    lda (dataPtr),y
    sta kPtr+1
    jsr kFree
    ldy #ED_CHUNKS
    lda (dataPtr),y
    sec
    sbc #1
    sta (dataPtr),y
    jmp edShrinkLp
edShrinkRts:
    rts

// ==========================================
//  Cursor movement (records + window; scrolling arrives in 5.6)
// ==========================================
// edSyncCursor -- position the kernel cursor + blink at ED_CURL/ED_CURC
edSyncCursor:
    ldy #ED_CURC                 // X = col, Y = line - winTop
    lda (dataPtr),y
    tax
    ldy #ED_CURL
    lda (dataPtr),y
    ldy #ED_WIN
    sec
    sbc (dataPtr),y
    tay
    jsr kSetCursor               // X,Y preserved
    jmp kBlinkReset

// edClampCurCol -- clamp ED_CURC to the current record length and keep
// ED_PREFC in sync. Returns with kPtr = current record.
edClampCurCol:
    jsr edRecCur
    ldy #0
    lda (kPtr),y                 // A = len
    ldy #ED_CURC
    cmp (dataPtr),y
    bcs edClampRts               // len >= col -> already valid
    sta (dataPtr),y              // col = len
    ldy #ED_PREFC
    sta (dataPtr),y
edClampRts:
    rts

edLeft:
    ldy #ED_CURC
    lda (dataPtr),y
    bne edLeftDec
    ldy #ED_CURL                 // col 0 -> end of the previous line
    lda (dataPtr),y
    beq edMovRts
    sec
    sbc #1
    sta (dataPtr),y
    jsr edEnsureVisible          // scroll down if the cursor left the window
    jsr edRecCur
    ldy #0
    lda (kPtr),y                 // len -> col (clamp 39)
    cmp #40
    bcc edLeftSet
    lda #39
    jmp edLeftSet
edLeftDec:
    sec
    sbc #1
edLeftSet:
    ldy #ED_CURC
    sta (dataPtr),y
    ldy #ED_PREFC
    sta (dataPtr),y
edMovRts:
    rts

edRight:
    jsr edClampCurCol
    ldy #0
    lda (kPtr),y                 // len
    ldy #ED_CURC
    cmp (dataPtr),y
    beq edRightWrap              // at end of line -> next line col 0
    bcc edRightWrap              // defensive: stale col > len behaves the same
    lda (dataPtr),y
    clc
    adc #1
    jmp edLeftSet
edRightWrap:
    jsr edLastLine
    ldy #ED_CURL
    cmp (dataPtr),y
    beq edMovRts
    bcc edMovRts
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    jsr edEnsureVisible          // scroll up if the cursor left the window
    lda #0
    ldy #ED_CURC
    sta (dataPtr),y
    ldy #ED_PREFC
    sta (dataPtr),y
    rts

edUp:
    ldy #ED_CURL
    lda (dataPtr),y
    beq edMovRts
    sec
    sbc #1
    sta (dataPtr),y
    jsr edEnsureVisible          // scroll down if the cursor left the window
    jmp edContentCol             // col = min(ED_PREFC, lastNonSpace)

edDown:
    jsr edLastLine
    ldy #ED_CURL
    cmp (dataPtr),y
    beq edMovRts
    bcc edMovRts
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    jsr edEnsureVisible          // scroll up if the cursor left the window
    jmp edContentCol             // col = min(ED_PREFC, lastNonSpace)

edHome:
    lda #0
    ldy #ED_CURC
    sta (dataPtr),y
    ldy #ED_PREFC
    sta (dataPtr),y
    rts

// edEnsureVisible -- scroll so the cursor line is inside the window.
//  ED_CURL < ED_WIN -> scroll down + redraw the new top row;
//  ED_CURL > ED_WIN+23 -> scroll up + redraw the new bottom row.
//  Clobbers A, X, Y, kPtr.
edEnsureVisible:
    ldy #ED_CURL
    lda (dataPtr),y              // cursor line
    ldy #ED_WIN
    cmp (dataPtr),y              // cursor vs winTop
    bcs edEvBelow
    ldy #ED_WIN                  // cursor above window -> scroll down
    lda (dataPtr),y
    sec
    sbc #1
    sta (dataPtr),y              // winTop--
    jsr edScrollDown
    ldy #ED_WIN
    lda (dataPtr),y
    jsr edDrawLine               // redraw the new top row
    rts
edEvBelow:
    ldy #ED_WIN
    lda (dataPtr),y
    clc
    adc #23
    ldy #ED_TMPL
    sta (dataPtr),y              // bottom = winTop + 23
    ldy #ED_CURL
    lda (dataPtr),y
    ldy #ED_TMPL
    cmp (dataPtr),y              // cursor vs bottom
    bcc edEvRts                  // visible
    beq edEvRts                  // visible
    ldy #ED_WIN                  // cursor below window -> scroll up
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y              // winTop++
    jsr kScrollScreen
    ldy #ED_WIN
    lda (dataPtr),y
    clc
    adc #23
    jsr edDrawLine               // redraw the new bottom row
edEvRts:
    rts

// --- edScrollDown -- scroll the editor's body (rows 0..23) down one row ---
//  Editor-local: `kScrollScreen` scrolls a region up only, and only the editor
//  wants a down-scroll. The editor scrolls only while displayed (key input
//  needs kbFocus) and uses no char shadow, so it DMAs the framebuffer and
//  matrix directly: rows 0..22 -> 1..23 via the REU bounce, row 0 blanked.
//  SEI'd; erases the live cursor block first (edSyncCursor repositions it).
//  Clobbers A, X, Y, kPtr, kArg.
edScrollDown:
    sei                          // atomic vs the 60 Hz blink + F-key switches
    jsr kBlinkReset              // erase the live cursor block
    lda #<ED_REU_BOUNCE
    sta kArg
    lda #>ED_REU_BOUNCE
    sta kArg+1                   // bounce (REU bank 0)
    lda #0
    sta kArg+2                   // bank 0
    lda #<$1CC0                  // 23 rows x 320 pixels
    sta kArg+3
    lda #>$1CC0
    sta kArg+4
    lda #<$2000
    sta kPtr
    lda #>$2000
    sta kPtr+1                   // source = framebuffer row 0
    jsr kReuFromC64              // framebuffer -> bounce
    lda #<$2140
    sta kPtr
    lda #>$2140
    sta kPtr+1                   // dest = framebuffer row 1
    jsr kReuToC64                // bounce -> framebuffer+320
    lda #<$2000                  // blank pixel row 0 (black)
    sta kPtr
    lda #>$2000
    sta kPtr+1
    lda #<$0140
    sta kArg
    lda #>$0140
    sta kArg+1
    lda #0
    jsr kReuFillC64
    lda #<ED_REU_BOUNCE          // matrix: 23 rows x 40 bytes
    sta kArg
    lda #>ED_REU_BOUNCE
    sta kArg+1
    lda #0
    sta kArg+2
    sta kArg+5
    lda #<$0398
    sta kArg+3
    lda #>$0398
    sta kArg+4
    lda #<$0400
    sta kPtr
    lda #>$0400
    sta kPtr+1                   // source = matrix row 0
    jsr kReuFromC64
    lda #<$0428
    sta kPtr
    lda #>$0428
    sta kPtr+1                   // dest = matrix row 1
    jsr kReuToC64
    lda #<$0400                  // blank matrix row 0 (bg0 = black)
    sta kPtr
    lda #>$0400
    sta kPtr+1
    lda #40
    sta kArg
    lda #0
    sta kArg+1
    lda #$00
    jsr kReuFillC64
    cli
    rts

// edLastNonSpace -- A = append position of the cursor line (column after the
//  last non-space char; 0 if blank). Clobbers A, X, Y, kPtr.
edLastNonSpace:
    jsr edRecCur                 // kPtr = record
    ldy #0
    lda (kPtr),y                 // len
    beq elnsZero
    tay                          // Y = len (record index of the last char)
elnsLoop:
    lda (kPtr),y
    cmp #$20
    bne elnsFound
    dey
    bne elnsLoop
elnsZero:
    lda #0
    rts
elnsFound:
    tya                          // A = record index (append position)
    rts

// edContentCol -- col = min(ED_PREFC, append position) on the cursor line.
//  Vertical moves never change ED_PREFC. Clobbers A, X, Y, kPtr.
edContentCol:
    jsr edLastNonSpace           // A = append position
    ldy #ED_PREFC
    cmp (dataPtr),y              // append vs pref
    bcs edCcPref                 // append >= pref -> use pref
    ldy #ED_CURC
    sta (dataPtr),y              // col = append
    rts
edCcPref:
    ldy #ED_PREFC
    lda (dataPtr),y
    ldy #ED_CURC
    sta (dataPtr),y              // col = pref
    rts

// --- edAfterEdit -- redraw the affected line(s) + cursor + status ---
//  ED_FLAG: 0 = redraw the cursor line only; 1 = structural edit -- redraw
//  from the cursor line (minus one, to cover a split/insert that moved the
//  cursor down) to the bottom of the window. Structural edits never touch
//  lines above the cursor, so the rows above stay valid.
edAfterEdit:
    ldy #ED_MARK
    lda (dataPtr),y
    beq edAfterNoMark
    lda #0
    sta (dataPtr),y              // editing cancels an active selection
    lda #0
    ldy #ED_FLAG
    sta (dataPtr),y              // edDrawAll below covers any flagged line-shift too
    jsr edEnsureVisible
    jsr edDrawAll                // full redraw clears the stale highlight everywhere
    jmp edAfterSync
edAfterNoMark:
    jsr edEnsureVisible          // keep the cursor in the window
    ldy #ED_FLAG
    lda (dataPtr),y
    beq edAfterLine
    lda #0
    sta (dataPtr),y
    // start row = ED_CURL-1-winTop, clamped to 0
    ldy #ED_CURL
    lda (dataPtr),y
    beq edAfterFrom0             // cursor line 0 -> start at the top row
    sec
    sbc #1                       // A = ED_CURL-1
    ldy #ED_WIN
    sec
    sbc (dataPtr),y              // A = ED_CURL-1-winTop
    bcc edAfterFrom0             // cursor at the top row -> start at row 0
    jsr edDrawFrom
    jmp edAfterSync
edAfterFrom0:
    lda #0
    jsr edDrawFrom
    jmp edAfterSync
edAfterLine:
    ldy #ED_CURL
    lda (dataPtr),y
    jsr edDrawLine
edAfterSync:
    jsr edSyncCursor
    jsr edDrawPos
    jsr edDrawSize               // document size changed
    rts

// --- edIsDisplayed -- C=1 iff this task's surface is displayed ---
//  The editor draws with kBlitChar/kBlitCharRev (framebuffer-only), so
//  every draw routine gates on this to avoid writing onto a surface the
//  F-key switch just swapped in. Clobbers A, X, Y (surfDisplayed).
edIsDisplayed:
    ldy #ED_TASKID
    lda (dataPtr),y
    tax
    jmp kSurfDisplayed           // tail -- rts returns to our caller

// --- edDirtyRts -- an aborted draw left a partial frame in the slot ---
//  Flag a full redraw (ED_FLAG=1); editLoop picks it up on focus return.
edDirtyRts:
    lda #1
    ldy #ED_FLAG
    sta (dataPtr),y
    rts

// --- edStatusGate -- start a row-24 write at a frame edge ---
//  Cell row R is scanned on rasters 51+8R .. 58+8R (the mapping grknoid
//  quantises its motion clock with), so row 24 is on rasters 243-250: a
//  field written while the beam is inside the row is sampled half-done.
//  rasterCommit bumps frameCount at raster 0, so waiting for it to change
//  puts the beam at the top of the frame -- 243 rasters (15 ms) before the
//  row is scanned, which covers the longest field this task rewrites (the
//  eleven cells of edDrawPos, ~7 ms). No kernel support is needed: reading
//  $D012 and frameCount is free; WRITING $D012 would move the kernel's
//  raster interrupt. frameCount is 8-bit and wraps, so the wait compares
//  for CHANGE, never for elapsed frames.
//  A pass that other tasks preempt past raster 243 shows a partly updated
//  field -- which is why the fields are padded to a fixed width: a clear
//  pass on a reverse-video bar is a solid block, a partial one is not.
//  Clobbers A. Callers have already established the surface is displayed.
edStatusGate:
    lda frameCount
edSgWait:
    cmp frameCount
    beq edSgWait                // wait for the next frame edge
    rts

// --- edDrawAll -- render the whole window (rows 0..23) ---
edDrawAll:
    lda #0
// --- edDrawFrom -- render rows A..23 (partial redraw) ---
//  A = start row (0..23). Row counter lives in ED_CARRY (unused outside
//  the load phase) because edDrawLine clobbers ED_TMPL (record length)
//  and ED_TMPH (row). Clobbers A, X, Y, kPtr.
edDrawFrom:
    ldy #ED_CARRY
    sta (dataPtr),y              // row = A (saved before edIsDisplayed)
    jsr edIsDisplayed
    bcs edFromGo
    jmp edDirtyRts
edFromGo:
    ldy #ED_CARRY
    lda (dataPtr),y
    cmp #24
    bcs edFromRts
    jsr edDrawRow                // A = row
    ldy #ED_CARRY
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    jmp edFromGo
edFromRts:
    rts

// --- edDrawRow -- row A: line = row + ED_WIN -> record, else blank ---
edDrawRow:
    ldy #ED_SCR
    sta (dataPtr),y              // stash the row (blanking uses it)
    ldy #ED_WIN
    clc
    adc (dataPtr),y              // A = line = row + winTop
    ldy #ED_LINES
    cmp (dataPtr),y              // line vs ED_LINES
    bcs edBlankRow               // past the last line -> blank
    jmp edDrawLine               // A = line
edBlankRow:
    jsr edIsDisplayed
    bcs edBlankGo
    jmp edDirtyRts
edBlankGo:
    ldy #ED_SCR                  // row (stashed by edDrawRow)
    lda (dataPtr),y
    ldy #ED_TMPH
    sta (dataPtr),y              // ED_TMPH = row
    ldx #0
    jmp edFillTail               // REU-fill the whole row (tail -- rts returns)

// --- edDrawLine -- A = line: blit its record at row line - ED_WIN ---
//  Clobbers A, X, Y, kPtr.
edDrawLine:
    pha                          // save line (edIsDisplayed clobbers A)
    jsr edIsDisplayed
    pla                          // restore line (PLA preserves carry)
    bcs edLineGo
    jmp edDirtyRts
edLineGo:
    ldy #ED_SCR
    sta (dataPtr),y              // stash line
    jsr edSelCache               // normalize the selection into ED_WINBUF (A clobbered)
    ldy #ED_SCR
    lda (dataPtr),y              // reload line
    jsr edRecAddr                // kPtr = record
    lda kPtr
    ldy #ED_REC
    sta (dataPtr),y
    lda kPtr+1
    iny
    sta (dataPtr),y              // ED_REC = record
    ldy #0
    lda (kPtr),y
    ldy #ED_TMPL
    sta (dataPtr),y              // ED_TMPL = len
    ldy #ED_SCR                  // row = line - ED_WIN
    lda (dataPtr),y
    ldy #ED_WIN
    sec
    sbc (dataPtr),y
    ldy #ED_TMPH
    sta (dataPtr),y              // ED_TMPH = row
    jsr edSelRange               // selection lo/hi -> ED_DIDX/ED_DCNT (mark-aware)
    ldx #0
edDrawLp:                        // X = col
    txa
    cmp #40
    bcs edDrawTail               // col >= 40 -> fill is a no-op
    ldy #ED_TMPL
    txa
    cmp (dataPtr),y              // col vs len
    bcs edDrawTail               // col >= len -> REU-fill the tail
    // selection: col in [lo,hi) -> reverse video
    txa
    ldy #ED_DIDX
    cmp (dataPtr),y              // col vs lo
    bcc edDrawBlit
    ldy #ED_DCNT
    cmp (dataPtr),y              // col vs hi
    bcs edDrawBlit
    jmp edDrawRev                // selected -> reverse
edDrawBlit:
    ldy #ED_REC
    lda (dataPtr),y
    sta kPtr
    iny
    lda (dataPtr),y
    sta kPtr+1                   // kPtr = record
    txa
    tay
    iny
    lda (kPtr),y                 // record[col+1]
    pha                          // [char]
    txa
    pha                          // [char, col]
    ldy #ED_STCOL
    sta (dataPtr),y              // save the loop col before kBlitChar clobbers X
    ldy #ED_TMPH
    lda (dataPtr),y              // row
    tay
    pla
    tax                          // col
    pla                          // char
    jsr kBlitChar
    jmp edDrawNext
edDrawRev:
    ldy #ED_REC
    lda (dataPtr),y
    sta kPtr
    iny
    lda (dataPtr),y
    sta kPtr+1                   // kPtr = record
    txa
    tay
    iny
    lda (kPtr),y                 // record[col+1]
    pha                          // [char]
    txa
    pha                          // [char, col]
    ldy #ED_STCOL
    sta (dataPtr),y              // save the loop col before kBlitChar clobbers X
    ldy #ED_TMPH
    lda (dataPtr),y              // row
    tay
    pla
    tax                          // col
    pla                          // char
    jsr kBlitCharRev
edDrawNext:
    ldy #ED_STCOL
    lda (dataPtr),y
    clc
    adc #1
    tax
    jmp edDrawLp
edDrawTail:
    jmp edFillTail               // X = start col -> REU-fill [X..39]

// --- edFillTail -- blank cells [X..39] of row ED_TMPH via REU fill ---
//  Replaces the per-cell space blits: two kReuFillC64 fills (framebuffer
//  then matrix) blank the run in one shot. SEI-guarded; erases a live
//  blink block first so the fill never overwrites the cursor glyph
//  (edSyncCursor repositions it). Input: X = start col (0..39),
//  ED_TMPH = row (0..23). Clobbers A, X, Y, kPtr, kArg..kArg+2.
edFillTail:
    cpx #40
    bcc edFillGo                 // X < 40 -> proceed
    rts                          // X >= 40 -> nothing left to fill
edFillGo:
    ldy #ED_DIG
    txa
    sta (dataPtr),y              // ED_DIG = start col (X clobbered below)
    sei                          // atomic vs the 60 Hz blink during the fills
    jsr kBlinkReset              // erase a live blink block over the fill range
    // cellIndex = row*40 + col (0..959) -> kArg+4 (lo) / kArg+5 (hi)
    ldy #ED_TMPH
    lda (dataPtr),y              // row
    asl
    asl
    asl                          // row*8 (row < 32)
    sta kArg+4                   // cellIndex lo (row*8)
    ldy #ED_TMPH
    lda (dataPtr),y              // row
    asl
    asl
    asl
    asl
    asl                          // (row*32) & $FF
    clc
    adc kArg+4                   // + row*8 lo
    sta kArg+4                   // cellIndex lo = (row*40) & $FF
    lda #0
    adc #0                       // A = carry out of the lo add (0/1)
    sta kArg+5
    ldy #ED_TMPH
    lda (dataPtr),y              // row
    lsr
    lsr
    lsr                          // row>>3 = (row*40) >> 8 (pre-carry)
    clc
    adc kArg+5                   // + carry
    sta kArg+5                   // cellIndex hi
    lda kArg+4
    ldy #ED_DIG
    clc
    adc (dataPtr),y              // + col
    sta kArg+4
    bcc edFillHi
    inc kArg+5
edFillHi:
    // matrix dest = $0400 + cellIndex
    lda kArg+4
    clc
    adc #<$0400
    sta kPtr
    lda kArg+5
    adc #>$0400
    sta kPtr+1
    // matrix length = 40 - col
    lda #40
    ldy #ED_DIG
    sec
    sbc (dataPtr),y
    sta kArg
    lda #0
    sta kArg+1
    lda #$00
    jsr kReuFillC64              // blank the matrix run
    // pixel dest = $2000 + cellIndex*8 (cellIndex survives in kArg+4/+5)
    lda kArg+4
    sta kPtr
    lda kArg+5
    sta kPtr+1
    asl kPtr
    rol kPtr+1
    asl kPtr
    rol kPtr+1
    asl kPtr
    rol kPtr+1                   // kPtr = cellIndex*8
    lda kPtr
    clc
    adc #<$2000
    sta kPtr
    lda kPtr+1
    adc #>$2000
    sta kPtr+1
    // pixel length = (40 - col) x 8
    lda #0
    sta kArg+1
    lda #40
    ldy #ED_DIG
    sec
    sbc (dataPtr),y
    asl
    rol kArg+1
    asl
    rol kArg+1
    asl
    rol kArg+1
    sta kArg
    lda #$00
    jsr kReuFillC64              // blank the pixel run
    cli
    rts

// ==========================================
//  Save -- streamed, high-water fixed-size model
// ==========================================
//  Pass 1 walks the records for the exact image size and the last
//  non-blank line; pass 2 streams each line (trimmed chars + CR + NUL)
//  via kReuWriteAt. Fits in place -> NUL-fill the tail; outgrew -> write a
//  temp file, rename the old file aside, swap the temp into place, then
//  drop the backup. Empty document maps to delete-only. C=0 success,
//  C=1 failure with the current binding preserved.
//  Clobbers A, X, Y, kPtr, srcPtr, kArg.
edSave:
    // --- pass 1: size + last non-blank line ---
    lda #$FF
    ldy #ED_TMPH
    sta (dataPtr),y              // lastNonBlank = none
    lda #0
    ldy #ED_POS
    sta (dataPtr),y
    iny
    sta (dataPtr),y              // size = 0
    ldy #ED_TMPL
    sta (dataPtr),y              // line = 0
esSizeLp:
    ldy #ED_TMPL
    lda (dataPtr),y
    ldy #ED_LINES
    cmp (dataPtr),y
    bcs esSizeDone
    ldy #ED_TMPL
    lda (dataPtr),y
    jsr edRecAddr                // kPtr = record
    ldy #0
    lda (kPtr),y                 // len
    beq esCrNul                  // blank line -> CR+NUL only
    tay                          // trim trailing spaces -> y
esTrimLp:
    cpy #0
    beq esTrimDone
    lda (kPtr),y
    cmp #$20
    bne esTrimDone
    dey
    jmp esTrimLp
esTrimDone:
    tya
    beq esCrNul                  // all spaces -> CR+NUL only
    ldy #ED_POS                  // size += tlen
    clc
    adc (dataPtr),y
    sta (dataPtr),y
    bcc esSizeC1
    iny
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y
esSizeC1:
    ldy #ED_TMPL                 // lastNonBlank = line
    lda (dataPtr),y
    ldy #ED_TMPH
    sta (dataPtr),y
esCrNul:                         // size += 2 (CR+NUL) for every line
    ldy #ED_POS
    lda (dataPtr),y
    clc
    adc #2
    sta (dataPtr),y
    bcc esSizeNext
    iny
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y
esSizeNext:
    ldy #ED_TMPL
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    jmp esSizeLp
esSizeDone:
    ldy #ED_TMPH                 // empty document -> delete only
    lda (dataPtr),y
    cmp #$FF
    bne esNotEmpty
    lda dataPtr
    sta kArg
    lda dataPtr+1
    sta kArg+1
    jsr kReuDelete
    bcc esEmptyOk
    jmp esFailMsg
esEmptyOk:
    lda #0
    ldy #ED_HASFILE
    sta (dataPtr),y
    jmp esSaved
esNotEmpty:
    // size excludes the trailing blank lines (pass 2 stops at the last
    // non-blank line): subtract 2 x (ED_LINES-1 - lastNonBlank)
    ldy #ED_LINES
    lda (dataPtr),y              // ED_LINES
    sec
    sbc #1                       // ED_LINES-1
    ldy #ED_TMPH
    sec
    sbc (dataPtr),y              // trailing = (ED_LINES-1) - lastNonBlank
    beq esNoTrail                // none -> nothing to trim
    asl                          // 2xtrailing low
    ldy #ED_SCR
    sta (dataPtr),y
    lda #0
    rol                          // 2xtrailing high
    iny
    sta (dataPtr),y              // ED_SCR = 2xtrailing
    ldy #ED_POS                  // size -= 2xtrailing
    lda (dataPtr),y
    sec
    ldy #ED_SCR
    sbc (dataPtr),y
    ldy #ED_POS
    sta (dataPtr),y
    ldy #ED_POS+1
    lda (dataPtr),y
    ldy #ED_SCR+1
    sbc (dataPtr),y
    ldy #ED_POS+1
    sta (dataPtr),y
esNoTrail:
    // --- pick the write path ---
    ldy #ED_HASFILE
    lda (dataPtr),y
    bne esHaveFile
    jsr esDoCreate               // new file -- create the exact size
    bcs esCreateFail
    jmp esAfterOpen
esCreateFail:
    jmp esRet
esHaveFile:
    ldy #ED_FREC                 // fits in place?
    lda (dataPtr),y
    ldy #ED_POS
    cmp (dataPtr),y
    ldy #ED_FREC+1
    lda (dataPtr),y
    ldy #ED_POS+1
    sbc (dataPtr),y
    bcc esGrow
    jsr esWrite                  // size <= file size -> in place
    bcs esHaveWriteFail
    jmp esSaved
esHaveWriteFail:
    jmp esRet
esGrow:
    ldy #ED_FREC                // preserve the live record across temp save
esGrowPushRecLp:
    lda (dataPtr),y
    pha
    iny
    cpy #ED_FREC+5
    bcc esGrowPushRecLp
    lda #'n'
    jsr esMakeTempName
    jsr esCreateBuf
    bcs esGrowCreateFail
    jsr esOpenBufRec
    bcs esGrowTempFail
    jsr esWrite
    bcs esGrowTempFail
    lda #'o'
    jsr esMakeTempName
    jsr esRenameCurToBuf
    bcs esGrowPromoteFail
    lda #'n'
    jsr esMakeTempName
    jsr esRenameBufToCur
    bcs esGrowRollback
    lda #'o'
    jsr esMakeTempName
    jsr esDeleteBuf              // best-effort cleanup after the swap
    jsr esOpenRec
    bcs esGrowOpenFail
    ldx #5
esGrowDropRecLp:
    pla
    dex
    bne esGrowDropRecLp
    jmp esSaved
esGrowCreateFail:
    ldx #5
esGrowCreateDropLp:
    pla
    dex
    bne esGrowCreateDropLp
    jmp esRet
esGrowTempFail:
    lda #'n'
    jsr esMakeTempName
    jsr esDeleteBuf              // ignore delete failure; preserve the original file
    ldy #ED_FREC+4
esGrowTempPopLp:
    pla
    sta (dataPtr),y
    dey
    cpy #ED_FREC-1
    bne esGrowTempPopLp
    jmp esRet
esGrowPromoteFail:
    lda #'n'
    jsr esMakeTempName
    jsr esDeleteBuf              // temp write succeeded but swap never started
    ldy #ED_FREC+4
esGrowPromotePopLp:
    pla
    sta (dataPtr),y
    dey
    cpy #ED_FREC-1
    bne esGrowPromotePopLp
    jmp esRet
esGrowRollback:
    lda #'o'
    jsr esMakeTempName
    jsr esRenameBufToCur         // restore the original binding if the swap failed
    lda #'n'
    jsr esMakeTempName
    jsr esDeleteBuf
    ldy #ED_FREC+4
esGrowRollbackPopLp:
    pla
    sta (dataPtr),y
    dey
    cpy #ED_FREC-1
    bne esGrowRollbackPopLp
    jmp esRet
esAfterOpen:
    jsr esOpenRec
    bcs esOpenFail
    lda #1
    ldy #ED_HASFILE
    sta (dataPtr),y
    jsr esWrite
    bcs esNewWriteFail
    jmp esSaved
esNewWriteFail:
    jmp esRet
esOpenFail:
    jmp esFailMsg
esGrowOpenFail:
    ldx #5
esGrowOpenDropLp:
    pla
    dex
    bne esGrowOpenDropLp
    jsr esFailMsgOnly
    sec
    rts
esWrite:
    // --- pass 2: stream lines 0..lastNonBlank ---
    lda #0
    ldy #ED_SCR
    sta (dataPtr),y
    iny
    sta (dataPtr),y              // pos = 0
    ldy #ED_TMPL
    sta (dataPtr),y              // line = 0
esWriteLp:
    ldy #ED_TMPL
    lda (dataPtr),y              // line vs lastNonBlank
    ldy #ED_TMPH
    cmp (dataPtr),y
    beq esWriteLine
    bcc esWriteLine
    jmp esWriteDone
esWriteLine:
    ldy #ED_TMPL
    lda (dataPtr),y
    jsr edRecAddr                // kPtr = record
    ldy #0
    lda (kPtr),y                 // len -> trimmed
    tay
esWTrimLp:
    cpy #0
    beq esWTrimDone
    lda (kPtr),y
    cmp #$20
    bne esWTrimDone
    dey
    jmp esWTrimLp
esWTrimDone:
    tya                          // tlen -> ED_TCHAR
    ldy #ED_TCHAR
    sta (dataPtr),y
    lda dataPtr                  // srcPtr = line image (ED_WINBUF)
    clc
    adc #ED_WINBUF
    sta srcPtr
    lda dataPtr+1
    adc #0
    sta srcPtr+1
    ldx #0                       // copy record[1..tlen] -> image[0..]
esCopyLp:
    txa
    ldy #ED_TCHAR
    cmp (dataPtr),y              // index vs tlen
    bcs esCopyDone
    txa
    tay
    iny
    lda (kPtr),y                 // record[index+1]
    dey
    sta (srcPtr),y               // image[index]
    inx
    jmp esCopyLp
esCopyDone:
    txa                          // image[tlen] = CR, [tlen+1] = NUL
    tay
    lda #$0D
    sta (srcPtr),y
    iny
    lda #$00
    sta (srcPtr),y
    tya
    clc
    adc #1
    sta kArg+6                   // len = tlen + 2
    lda #0
    sta kArg+7
    lda dataPtr                  // kReuWriteAt(record, pos, len, image)
    clc
    adc #ED_FREC
    sta kArg
    lda dataPtr+1
    adc #0
    sta kArg+1
    ldy #ED_SCR
    lda (dataPtr),y
    sta kArg+2
    iny
    lda (dataPtr),y
    sta kArg+3                   // pos
    lda dataPtr
    clc
    adc #ED_WINBUF
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    jsr kReuWriteAt
    bcc esWriteOk
    jmp esFailMsg
esWriteOk:
    ldy #ED_TCHAR                // pos += tlen + 2
    lda (dataPtr),y
    clc
    adc #2
    ldy #ED_SCR
    clc
    adc (dataPtr),y
    sta (dataPtr),y
    bcc esPosC1
    iny
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y
esPosC1:
    ldy #ED_TMPL
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y              // line++
    jmp esWriteLp
esWriteDone:
    // NUL-fill the tail (file size - image size), <= 254 bytes per call
    ldy #ED_FREC
    lda (dataPtr),y
    ldy #ED_POS
    sec
    sbc (dataPtr),y              // fsizeLo - sizeLo
    ldy #ED_SCR
    sta (dataPtr),y
    ldy #ED_FREC+1
    lda (dataPtr),y
    ldy #ED_POS+1
    sbc (dataPtr),y              // fsizeHi - sizeHi
    ldy #ED_SCR+1
    sta (dataPtr),y
    ldy #ED_SCR                  // rem == 0 -> done
    lda (dataPtr),y
    ldy #ED_SCR+1
    ora (dataPtr),y
    bne esFillInit
    clc
    rts
esFillInit:
    ldy #ED_POS                  // offset = size
    lda (dataPtr),y
    ldy #ED_TMPL
    sta (dataPtr),y
    ldy #ED_POS+1
    lda (dataPtr),y
    ldy #ED_TMPH
    sta (dataPtr),y
esFillLp:
    ldy #ED_SCR                  // rem == 0 -> done
    lda (dataPtr),y
    ldy #ED_SCR+1
    ora (dataPtr),y
    bne esFillGo
    clc
    rts
esFillGo:
    ldy #ED_SCR+1                // chunk = min(rem, 2) -- ED_NUL is 2 bytes
    lda (dataPtr),y
    bne esFillMax
    ldy #ED_SCR
    lda (dataPtr),y
    cmp #2
    bcc esFillUse
esFillMax:
    lda #2
esFillUse:
    ldy #ED_TCHAR
    sta (dataPtr),y              // chunk
    sta kArg+6
    lda #0
    sta kArg+7
    lda dataPtr
    clc
    adc #ED_FREC
    sta kArg
    lda dataPtr+1
    adc #0
    sta kArg+1
    ldy #ED_TMPL
    lda (dataPtr),y
    sta kArg+2
    ldy #ED_TMPH
    lda (dataPtr),y
    sta kArg+3                   // offset
    lda dataPtr
    clc
    adc #ED_NUL
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    jsr kReuWriteAt
    bcc esFillWriteOk
    jmp esFailMsg
esFillWriteOk:
    ldy #ED_TCHAR                // offset += chunk
    lda (dataPtr),y
    ldy #ED_TMPL
    clc
    adc (dataPtr),y
    sta (dataPtr),y
    bcc esFill2
    ldy #ED_TMPH
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y
esFill2:
    ldy #ED_TCHAR                // rem -= chunk
    lda (dataPtr),y
    pha                          // [chunk]
    ldy #ED_SCR
    lda (dataPtr),y
    sec
    tsx
    sbc $0101,x                  // remLo - chunk
    sta (dataPtr),y
    ldy #ED_SCR+1
    lda (dataPtr),y
    sbc #0
    sta (dataPtr),y
    pla
    jmp esFillLp
esSaved:
    lda #0
    ldy #ED_MOD
    sta (dataPtr),y
    lda #<edSavedMsg
    sta kPtr
    lda #>edSavedMsg
    sta kPtr+1
    jsr edShowMsg
    jsr edDrawHint
    clc
esRet:
    rts

esFailMsg:
    jsr esFailMsgOnly
    sec
    rts

esFailMsgOnly:
    lda #<edSaveFailMsg
    sta kPtr
    lda #>edSaveFailMsg
    sta kPtr+1
    jsr edShowMsg
    jsr edDrawHint
    rts

// --- esDoCreate -- kReuCreate with the size in ED_POS; error -> status + C=1
esDoCreate:
    lda dataPtr
    sta kArg
    lda dataPtr+1
    sta kArg+1                   // name
    ldy #ED_POS
    lda (dataPtr),y
    sta kArg+2
    iny
    lda (dataPtr),y
    sta kArg+3                   // size
    jsr kReuCreate
    bcs esCreateErr
    clc
    rts
esCreateErr:
    lda kArg+6
    cmp #1
    beq esErrSpace
    cmp #2
    beq esErrDir
    lda #<edSaveFailMsg
    sta kPtr
    lda #>edSaveFailMsg
    sta kPtr+1
    jmp esErrShow
esErrSpace:
    lda #<edNoSpaceMsg
    sta kPtr
    lda #>edNoSpaceMsg
    sta kPtr+1
    jmp esErrShow
esErrDir:
    lda #<edDirFullMsg
    sta kPtr
    lda #>edDirFullMsg
    sta kPtr+1
esErrShow:
    jsr edShowMsg
    jsr edDrawHint
    sec
    rts

// --- esMakeTempName -- ED_WINBUF = target-prefix + tag + taskid-hex ---
//  A = tag char ('n' new image, 'o' old backup). Result is null-terminated.
esMakeTempName:
    pha
    lda dataPtr
    sta srcPtr
    lda dataPtr+1
    sta srcPtr+1
    lda dataPtr
    clc
    adc #ED_WINBUF
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    ldy #0
esMtnCopyLp:
    cpy #8
    bcs esMtnTag
    lda (srcPtr),y
    beq esMtnTag
    sta (kPtr),y
    iny
    bne esMtnCopyLp
esMtnTag:
    pla
    sta (kPtr),y
    iny
    tya
    ldy #ED_STIDX
    sta (dataPtr),y             // suffix write offset
    lda dataPtr
    clc
    adc #ED_WINBUF
    ldy #ED_STIDX
    adc (dataPtr),y
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    ldy #ED_TASKID
    lda (dataPtr),y
    jsr kByteToHex
    ldy #ED_STIDX
    lda (dataPtr),y
    clc
    adc #ED_WINBUF+2
    tay
    lda #0
    sta (dataPtr),y
    rts

// --- buffer-name FS wrappers (ED_WINBUF = temp/backup name) ---
esCreateBuf:
    lda dataPtr
    clc
    adc #ED_WINBUF
    sta kArg
    lda dataPtr+1
    adc #0
    sta kArg+1
    jmp esCreateWithArg

esOpenBufRec:
    lda dataPtr
    clc
    adc #ED_WINBUF
    sta kArg
    lda dataPtr+1
    adc #0
    sta kArg+1
    lda dataPtr
    clc
    adc #ED_FREC
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    jsr kReuOpen
    rts

esDeleteBuf:
    lda dataPtr
    clc
    adc #ED_WINBUF
    sta kArg
    lda dataPtr+1
    adc #0
    sta kArg+1
    jsr kReuDelete
    rts

esRenameCurToBuf:
    lda dataPtr
    sta kArg
    lda dataPtr+1
    sta kArg+1
    lda dataPtr
    clc
    adc #ED_WINBUF
    sta kArg+2
    lda dataPtr+1
    adc #0
    sta kArg+3
    jsr kReuRename
    rts

esRenameBufToCur:
    lda dataPtr
    clc
    adc #ED_WINBUF
    sta kArg
    lda dataPtr+1
    adc #0
    sta kArg+1
    lda dataPtr
    sta kArg+2
    lda dataPtr+1
    sta kArg+3
    jsr kReuRename
    rts

// --- esOpenRec -- kReuOpen into ED_FREC; C=1 on failure ---
esOpenRec:
    lda dataPtr
    sta kArg
    lda dataPtr+1
    sta kArg+1
    lda dataPtr
    clc
    adc #ED_FREC
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    jsr kReuOpen
    rts

esCreateWithArg:
    ldy #ED_POS
    lda (dataPtr),y
    sta kArg+2
    iny
    lda (dataPtr),y
    sta kArg+3                   // size
    jsr kReuCreate
    bcc esCreateArgOk
    jmp esCreateErr
esCreateArgOk:
    clc
    rts

// ==========================================
//  Load -- kReuOpen + windowed kReuReadAt streaming
// ==========================================
//  Parses CR+NUL (newline), CR+non-NUL (literal 'm' glyph), NUL (skip);
//  hard-wraps at 40; stops at 192 lines (the rest is discarded). The
//  filename is trimmed to 11 chars in place. Clobbers A, X, Y, kPtr,
//  srcPtr, kArg.
//  elGrowChunk -- allocate the next 4-page chunk during load when the
//  current line count reaches chunk capacity. C=1 -> refused, unchanged.
elGrowChunk:
    ldy #ED_CHUNKS
    lda (dataPtr),y
    cmp #MAX_CHUNKS
    bcc elGrowTry
    sec
    rts
elGrowTry:
    lda #CHUNK_PAGES
    jsr kMalloc
    bcc elGrowOk
    jsr edNoRoom                // no pool room for another chunk -- say so
    sec
    rts
elGrowOk:
    ldy #ED_CHUNKS
    lda (dataPtr),y
    asl
    clc
    adc #ED_CHKTAB
    tay
    lda kPtr
    sta (dataPtr),y
    iny
    lda kPtr+1
    sta (dataPtr),y
    ldy #ED_CHUNKS
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    clc
    rts

editLoad:
    lda dataPtr                  // srcPtr = filename
    sta srcPtr
    lda dataPtr+1
    sta srcPtr+1
    ldy #0
    lda (srcPtr),y
    bne elTrim                   // has a name -> trim it
    rts                          // empty name -> blank document
elTrim:                          // null at first space / after 11 chars
    lda (srcPtr),y
    beq elTrimDone
    cmp #$20
    beq elTrimDone
    iny
    cpy #11
    bcc elTrim
elTrimDone:
    lda #0
    sta (srcPtr),y               // trim trailing spaces, cap 11 chars
    lda dataPtr                  // kReuOpen: existence + 5-byte record
    sta kArg
    lda dataPtr+1
    sta kArg+1
    lda dataPtr
    clc
    adc #ED_FREC
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    jsr kReuOpen
    bcc elOpened
    rts                          // not found / FS disabled -> blank
elOpened:
    lda #1
    ldy #ED_HASFILE
    sta (dataPtr),y
    lda #0
    ldy #ED_POS
    sta (dataPtr),y
    iny
    sta (dataPtr),y              // pos = 0
    ldy #ED_CARRY
    sta (dataPtr),y              // no pending byte
    ldy #ED_TMPL
    sta (dataPtr),y              // current line = 0
    ldy #ED_TMPH
    sta (dataPtr),y              // col = 0
    // (record 0 was blanked at init; ED_LINES = 1)
    jmp elRefill

elRefill:                        // read the next 128-byte window
    lda dataPtr
    clc
    adc #ED_FREC
    sta kArg
    lda dataPtr+1
    adc #0
    sta kArg+1
    ldy #ED_POS
    lda (dataPtr),y
    sta kArg+2
    iny
    lda (dataPtr),y
    sta kArg+3                   // pos
    lda #128
    sta kArg+6
    lda #0
    sta kArg+7                   // len
    lda dataPtr
    clc
    adc #ED_WINBUF
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    jsr kReuReadAt               // actual bytes -> kArg+6/7
    bcc elReadOk
    lda #<edLoadFailMsg
    sta kPtr
    lda #>edLoadFailMsg
    sta kPtr+1
    jsr edShowMsg
    jsr edDrawHint
    rts
elReadOk:
    lda kArg+6
    ora kArg+7
    bne elWinHave
    rts                          // EOF
elWinHave:
    lda kArg+6                   // actual -> ED_SCR, idx -> ED_SCR+1
    ldy #ED_SCR
    sta (dataPtr),y
    lda #0
    ldy #ED_SCR+1
    sta (dataPtr),y
elByteLp:
    ldy #ED_CARRY
    lda (dataPtr),y
    bne elCarryByte
    ldy #ED_SCR+1                // idx vs actual
    lda (dataPtr),y
    ldy #ED_SCR
    cmp (dataPtr),y
    bcc elReadByte
    // window exhausted -> pos += actual, refill
    ldy #ED_POS
    lda (dataPtr),y
    clc
    adc kArg+6
    sta (dataPtr),y
    iny
    lda (dataPtr),y
    adc kArg+7
    sta (dataPtr),y
    jmp elRefill
elReadByte:
    ldy #ED_SCR+1
    lda (dataPtr),y
    clc
    adc #ED_WINBUF
    tay
    lda (dataPtr),y              // byte
    ldy #ED_SCR+1
    pha                          // [byte]
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y              // idx++
    pla
    jmp elHaveByte
elCarryByte:
    lda (dataPtr),y              // pending byte -- idx already past it
    pha
    lda #0
    sta (dataPtr),y              // clear carry
    pla
elHaveByte:
    beq elByteLp                 // NUL -> skip
    cmp #$0D
    bne elChar
    // CR -- inspect the following byte
    ldy #ED_SCR+1
    lda (dataPtr),y
    ldy #ED_SCR
    cmp (dataPtr),y              // idx vs actual
    bcc elPeek
    lda #$0D                     // window exhausted -> CR pending, refill
    ldy #ED_CARRY
    sta (dataPtr),y
    ldy #ED_POS
    lda (dataPtr),y
    clc
    adc kArg+6
    sta (dataPtr),y
    iny
    lda (dataPtr),y
    adc kArg+7
    sta (dataPtr),y
    jmp elRefill
elPeek:
    ldy #ED_SCR+1
    lda (dataPtr),y
    clc
    adc #ED_WINBUF
    tay
    lda (dataPtr),y              // following byte
    bne elCrGlyph
    ldy #ED_SCR+1                // CR+NUL -> consume the NUL too
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    jmp elNewline
elCrGlyph:
    lda #'m'                     // CR + non-NUL -> literal 'm' glyph
elChar:
    ldy #ED_TCHAR                // stash the char
    sta (dataPtr),y
    ldy #ED_TMPL                 // record = current line
    lda (dataPtr),y
    jsr edRecAddr
    ldy #ED_TMPH                 // col vs 40
    lda (dataPtr),y
    cmp #40
    bcc elPutChar
    ldy #ED_LINES                // hard wrap: 192 lines max
    lda (dataPtr),y
    cmp #MAX_LINES
    bcc elWrapOk
    rts                          // doc full -- stop (discard the rest)
elWrapOk:
    pha                          // preserve ED_LINES for the increment
    jsr elGrowChunkIfNeeded
    bcc elWrapGrowOk
    pla
    rts
elWrapGrowOk:
    pla
    clc
    adc #1
    ldy #ED_LINES                // elGrowChunkIfNeeded clobbered Y
    sta (dataPtr),y              // ED_LINES++
    ldy #ED_TMPL
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y              // line++
    jsr edRecAddr                // blank the new record
    ldy #REC_SIZE
    lda #0
elBlankRec:
    dey
    sta (kPtr),y
    bne elBlankRec
    lda #0
    ldy #ED_TMPH
    sta (dataPtr),y              // col = 0
elPutChar:
    ldy #ED_TCHAR
    lda (dataPtr),y
    pha                          // [char]
    ldy #ED_TMPH
    lda (dataPtr),y
    tay
    iny
    pla
    sta (kPtr),y                 // record[col+1] = char
    tya
    ldy #0
    sta (kPtr),y                 // len = col+1
    jsr edSizeInc
    ldy #ED_TMPH
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y              // col++
    jmp elByteLp
elNewline:
    ldy #ED_LINES                // 192 lines max
    lda (dataPtr),y
    cmp #MAX_LINES
    bcc elNlOk
    rts                          // doc full -- stop (discard the rest)
elNlOk:
    pha                          // preserve ED_LINES for the increment
    jsr elGrowChunkIfNeeded
    bcc elNlGrowOk
    pla
    rts
elNlGrowOk:
    pla
    clc
    adc #1
    ldy #ED_LINES                // elGrowChunkIfNeeded clobbered Y
    sta (dataPtr),y              // ED_LINES++
    ldy #ED_TMPL
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y              // line++
    jsr edRecAddr                // blank the new record
    ldy #REC_SIZE
    lda #0
elBlankRec2:
    dey
    sta (kPtr),y
    bne elBlankRec2
    lda #0
    ldy #ED_TMPH
    sta (dataPtr),y              // col = 0
    jmp elByteLp

//  elGrowChunkIfNeeded -- if ED_LINES has reached chunk capacity, append a
//  chunk before the caller increments ED_LINES. C=1 -> no space/limit.
elGrowChunkIfNeeded:
    ldy #ED_CHUNKS
    lda (dataPtr),y
    asl
    asl
    asl
    ldy #ED_REC
    sta (dataPtr),y
    asl
    clc
    adc (dataPtr),y              // capacity = chunks * 24
    ldy #ED_LINES
    cmp (dataPtr),y              // capacity vs ED_LINES
    beq elNeedGrow
    clc
    rts
elNeedGrow:
    jmp elGrowChunk

// ==========================================
//  Status bar (row 24, reverse video via kBlitCharRev)
// ==========================================
//  edDrawStatus -- full draw once: fill the row, filename, l/c section,
//  hint. edDrawPos redraws only the "l:NNN c:NN" section (cleared first)
//  on every cursor move -- the position comes from the document state
//  (ED_CURL/ED_CURC), not the kernel cursor; the decimal digits from
//  kByteToDec.
edDrawStatus:
    jsr edIsDisplayed
    bcs edStatusGo
    jmp edDirtyRts
edStatusGo:
    ldx #39
edFill:
    txa
    pha                         // save col (kBlitCharRev clobbers X)
    lda #$20
    ldy #24
    jsr kBlitCharRev
    pla
    tax
    dex
    bpl edFill
    jsr edDrawName              // filename (cols 0..10)
    jsr edDrawPos               // l:NNN c:NN
    jsr edDrawMode              // INS/OVR
    jsr edDrawSize              // size
    jsr edDrawHint              // hint
    rts

// --- edDrawName -- redraw the filename (cols 0..10) ---
edDrawName:
    jsr edIsDisplayed
    bcs edNameGo
    jmp edDirtyRts
edNameGo:
    ldx #0                      // filename at cols 0..10
    ldy #0
edNameLp:
    cpy #11
    bcs edNameDone
    lda (dataPtr),y             // filename char
    bne edNameChar
    lda #$20                    // NUL -> pad space
edNameChar:
    pha                         // save char
    tya
    ldy #ED_STIDX
    sta (dataPtr),y             // save filename index
    pla                         // A = char
    jsr edBlitChar              // blit at (X=col, 24); X=col+1
    ldy #ED_STIDX
    lda (dataPtr),y
    clc
    adc #1
    tay                         // filename index++
    jmp edNameLp
edNameDone:
    rts

// --- edDrawPos -- draw the "l:NNN c:NN" section (cols 11..21) ---
//  Fixed width: all eleven cells are written on every draw -- the row owns
//  cols 13..15 and the column 19..20, each padded out with spaces -- so the
//  section is never cleared. A clear pass on a reverse-video bar is a solid
//  BLOCK, and that is what made a cursor move flash. The field ends on a
//  space (col 21), which keeps it clear of the INS/OVR indicator at col 22,
//  and it covers the message area (cols 12..21), so overwriting every cell
//  erases the short message a save leaves on the bar.
edDrawPos:
    jsr edIsDisplayed
    bcs edPosGo
    jmp edDirtyRts
edPosGo:
    jsr edStatusGate
    ldx #11                     // "l:" at col 11
    lda #'l'; jsr edBlitChar    // X=12
    lda #':'; jsr edBlitChar    // X=13
    ldy #ED_STCOL               // row digits at col 13
    txa
    sta (dataPtr),y
    ldy #ED_CURL
    lda (dataPtr),y             // document line
    pha
    ldy #ED_STCOL
    lda (dataPtr),y
    tax
    pla
    jsr edBlitNum               // X = col+len
    lda #16                     // pad the row field (cols 13..15)
    jsr edBlitPad
    lda #' '; jsr edBlitChar    // " c:" at cols 16..18
    lda #'c'; jsr edBlitChar
    lda #':'; jsr edBlitChar
    ldy #ED_STCOL               // col digits
    txa
    sta (dataPtr),y
    ldy #ED_CURC
    lda (dataPtr),y             // document col
    pha
    ldy #ED_STCOL
    lda (dataPtr),y
    tax
    pla
    jsr edBlitNum               // X = col+len
    lda #22                     // pad the col field (digits at 19..20, and
    jsr edBlitPad               //   col 21 always a space: the gap before INS)
    rts

// --- edDrawMode -- redraw the INS/OVR indicator (cols 22..24) ---
edDrawMode:
    jsr edIsDisplayed
    bcs edModeGo
    jmp edDirtyRts
edModeGo:
    jsr edStatusGate
    ldx #22                     // INS/OVR -- 3 cells, always fully written
    ldy #ED_INSM
    lda (dataPtr),y
    bne edModeOvr
    lda #<edInsMsg
    sta kPtr
    lda #>edInsMsg
    sta kPtr+1
    jsr edBlitString
    rts
edModeOvr:
    lda #<edOvrMsg
    sta kPtr
    lda #>edOvrMsg
    sta kPtr+1
    jsr edBlitString
    rts

// --- edDrawSize -- draw the document size (cols 26..31) ---
//  ED_SIZE (16-bit) -> decimal via kDiv16; digits land reversed in
//  ED_WINBUF (free during editing), blitted left-aligned at col 26 and
//  then space-padded to col 32, so the six-cell field is fully rewritten
//  and needs no clear pass.
edDrawSize:
    jsr edIsDisplayed
    bcs edSizeGo
    jmp edDirtyRts
edSizeGo:
    jsr edStatusGate
    lda #0
    ldy #ED_DCNT
    sta (dataPtr),y              // digit count = 0
    ldy #ED_SIZE
    lda (dataPtr),y
    sta kArg                     // value lo
    iny
    lda (dataPtr),y
    sta kArg+1                   // value hi
    lda kArg                     // empty document (no chars) -> show 0
    ora kArg+1
    beq edSizeDiv
    // + 2 x ED_LINES (CR+NUL per line, so blank lines count)
    ldy #ED_LINES
    lda (dataPtr),y              // lines
    asl                          // lines*2 lo
    clc
    adc kArg
    sta kArg
    bcc edSizeLnHi
    inc kArg+1
edSizeLnHi:
    ldy #ED_LINES
    lda (dataPtr),y              // lines
    bpl edSizeTrInit             // lines < 128 -> lines*2 hi = 0
    inc kArg+1                   // lines >= 128 -> +256
edSizeTrInit:
    // - 2 x trailing blank lines (save stops at the last non-blank line)
    lda #0
    sta kArg+2                   // trailing = 0
    ldy #ED_LINES
    lda (dataPtr),y
    sec
    sbc #1
    sta kArg+3                   // line = ED_LINES-1
edSizeTrLp:
    lda kArg+3
    beq edSizeTrDone             // line 0 -> stop (never trim line 0)
    jsr edRecAddr                // kPtr = record
    ldy #0
    lda (kPtr),y                 // len
    beq edSizeTrBlank            // len 0 -> blank
    tay                          // Y = len (index of the last char)
edSizeTrTrim:
    lda (kPtr),y                 // char
    cmp #$20
    bne edSizeTrDone             // non-space -> not blank -> stop
    dey
    bne edSizeTrTrim             // more chars -> keep checking
edSizeTrBlank:
    inc kArg+2                   // trailing++
    dec kArg+3                   // line--
    jmp edSizeTrLp
edSizeTrDone:
    lda kArg+2                   // trailing
    asl                          // trailing*2 lo
    sta kArg+3
    lda #0
    adc #0                       // trailing*2 hi
    sta kArg+4
    lda kArg
    sec
    sbc kArg+3                   // value -= 2xtrailing
    sta kArg
    lda kArg+1
    sbc kArg+4
    sta kArg+1
edSizeDiv:
    lda #10
    sta kArg+2
    lda #0
    sta kArg+3                   // divisor = 10
    jsr kDiv16                   // q=kArg+4/+5, r=kArg+6/+7 (clobbers A,X,Y)
    lda kArg+6                   // remainder -> digit
    clc
    adc #'0'
    pha                          // [digit]
    ldy #ED_DCNT
    lda (dataPtr),y
    clc
    adc #ED_WINBUF
    tay
    pla                          // digit
    sta (dataPtr),y              // buf[count] = digit
    ldy #ED_DCNT
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y              // count++
    lda kArg+4
    sta kArg
    lda kArg+5
    sta kArg+1                   // value = quotient
    lda kArg
    ora kArg+1
    bne edSizeDiv
    ldx #26                      // blit digits (count-1 .. 0), left-aligned
edSizeBlit:
    ldy #ED_DCNT
    lda (dataPtr),y
    beq edSizeDone               // no digits left
    sec
    sbc #1
    sta (dataPtr),y              // count--
    clc
    adc #ED_WINBUF
    tay
    lda (dataPtr),y              // digit
    jsr edBlitChar               // blit at X; X=col+1
    jmp edSizeBlit
edSizeDone:
    lda #32                     // pad the size field (cols 26..31)
    jsr edBlitPad
    rts

// --- edDrawHint -- clear cols 33..39, draw the short hint ---
edDrawHint:
    jsr edIsDisplayed
    bcs edHintGo
    jmp edDirtyRts
edHintGo:
    ldx #39
edHintClear:
    txa
    pha
    lda #$20
    ldy #24
    jsr kBlitCharRev
    pla
    tax
    dex
    cpx #32
    bcs edHintClear
    ldx #33
    lda #<edHint
    sta kPtr
    lda #>edHint
    sta kPtr+1
    jsr edBlitString
    rts

// --- edDrawCmd -- clear cols 12..39, draw the command prompt ---
edDrawCmd:
    jsr edIsDisplayed
    bcs edCmdGo
    jmp edDirtyRts
edCmdGo:
    ldx #39
edCmdClear:
    txa
    pha
    lda #$20
    ldy #24
    jsr kBlitCharRev
    pla
    tax
    dex
    cpx #11
    bcs edCmdClear
    ldx #12
    ldy #ED_MARK
    lda (dataPtr),y
    bne edCmdHintMark
    lda #<edCmdHint
    sta kPtr
    lda #>edCmdHint
    sta kPtr+1
    jmp edCmdHintGo
edCmdHintMark:
    lda #<edCmdMarkHint
    sta kPtr
    lda #>edCmdMarkHint
    sta kPtr+1
edCmdHintGo:
    jsr edBlitString
    rts

// --- edDrawConfirm -- clear cols 12..39, draw the confirm prompt ---
edDrawConfirm:
    jsr edIsDisplayed
    bcs edConfirmGo
    jmp edDirtyRts
edConfirmGo:
    ldx #39
edConfirmClear:
    txa
    pha
    lda #$20
    ldy #24
    jsr kBlitCharRev
    pla
    tax
    dex
    cpx #11
    bcs edConfirmClear
    ldx #12
    lda #<edConfirmHint
    sta kPtr
    lda #>edConfirmHint
    sta kPtr+1
    jsr edBlitString
    rts

// --- edDrawClearConfirm -- clear cols 12..39, draw the clear prompt ---
edDrawClearConfirm:
    jsr edIsDisplayed
    bcs edClrConfirmGo
    jmp edDirtyRts
edClrConfirmGo:
    ldx #39
edClrConfirmClear:
    txa
    pha
    lda #$20
    ldy #24
    jsr kBlitCharRev
    pla
    tax
    dex
    cpx #11
    bcs edClrConfirmClear
    ldx #12
    lda #<edClearConfirmHint
    sta kPtr
    lda #>edClearConfirmHint
    sta kPtr+1
    jsr edBlitString
    rts

// --- edDrawWriteAs -- draw the name-input line (cols 12..39) ---
edDrawWriteAs:
    jsr edIsDisplayed
    bcs edWAsGo
    jmp edDirtyRts
edWAsGo:
    ldx #39
edWAsClear:
    txa
    pha
    lda #$20
    ldy #24
    jsr kBlitCharRev
    pla
    tax
    dex
    cpx #11
    bcs edWAsClear              // clear cols 39..12
    ldx #12
    lda #<edWriteAsMsg
    sta kPtr
    lda #>edWriteAsMsg
    sta kPtr+1
    jsr edBlitString            // X = col after "write as:"
    lda #$20
    jsr edBlitChar              // separator -> X = name col
    lda #0
    ldy #ED_DIDX
    sta (dataPtr),y             // index = 0
edWAsEcho:
    ldy #ED_DIDX
    lda (dataPtr),y             // index
    ldy #ED_STIDX
    cmp (dataPtr),y             // index vs len
    bcs edWAsCur
    ldy #ED_DIDX
    lda (dataPtr),y
    clc
    adc #ED_WINBUF
    tay
    lda (dataPtr),y             // buffer[index]
    jsr edBlitChar              // blit at X; X=col+1
    ldy #ED_DIDX
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y             // index++
    jmp edWAsEcho
edWAsCur:
    lda #$20                    // reverse-space cursor at the end
    jsr edBlitChar
    rts

// --- edShowMsg -- clear cols 12..21, blit the string at kPtr ---
//  The clear loop calls kBlitCharRev which clobbers kPtr, so the string
//  pointer is kept in ED_PTR across the loop (the same scratch edBlitString
//  uses -- it re-saves kPtr on entry, so no conflict).
edShowMsg:
    jsr edIsDisplayed
    bcs edShowGo
    jmp edDirtyRts
edShowGo:
    lda kPtr
    ldy #ED_PTR
    sta (dataPtr),y             // save string ptr lo
    lda kPtr+1
    iny
    sta (dataPtr),y             // save string ptr hi
    ldx #21
esMsgClear:
    txa
    pha
    lda #$20
    ldy #24
    jsr kBlitCharRev
    pla
    tax
    dex
    cpx #11
    bcs esMsgClear
    ldx #12
    ldy #ED_PTR
    lda (dataPtr),y             // restore string ptr lo
    sta kPtr
    iny
    lda (dataPtr),y
    sta kPtr+1                  // restore string ptr hi
    jsr edBlitString
    rts

// --- edBlitChar -- blit A at (X=col, row 24) in reverse; X = col+1 ---
edBlitChar:
    pha                         // save char
    ldy #ED_STCOL
    txa
    sta (dataPtr),y             // save col
    pla                         // A = char
    ldy #24
    jsr kBlitCharRev            // A=char, X=col, Y=24 (clobbers A,X,Y)
    ldy #ED_STCOL
    lda (dataPtr),y             // col
    clc
    adc #1
    tax                         // X = col+1
    rts

// --- edBlitPad -- space-pad to a fixed field end col (reverse row 24) ---
//  Input: A = end col, X = current col; returns X = end. Writes one reverse
//  space per cell until X reaches the end, so a fixed-width field is fully
//  rewritten and never needs a clear pass. A no-op when X >= end, so a
//  number that somehow overruns its field cannot push the layout right.
//  The end col lives in ED_PAD: everything in kArg is clobbered by the blit.
edBlitPad:
    ldy #ED_PAD
    sta (dataPtr),y             // end col
edBlitPadLp:
    txa
    ldy #ED_PAD
    cmp (dataPtr),y
    bcs edBlitPadDone           // X >= end -> the field is complete
    lda #$20
    jsr edBlitChar              // X = col+1
    jmp edBlitPadLp
edBlitPadDone:
    rts

// --- edBlitNum -- blit A (0-255) as decimal at (X=col, 24); X=col+len ---
edBlitNum:
    pha                         // save value (txa clobbers A)
    ldy #ED_STCOL
    txa
    sta (dataPtr),y             // save col
    lda dataPtr
    clc
    adc #ED_DIG
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1                  // kPtr = digit buffer
    pla                         // A = value
    jsr kByteToDec              // digits at kPtr; Y = len (1-3)
    tya
    ldy #ED_DCNT
    sta (dataPtr),y             // ED_DCNT = len
    lda #0
    ldy #ED_DIDX
    sta (dataPtr),y             // ED_DIDX = 0
    ldy #ED_STCOL
    lda (dataPtr),y             // col
    tax                         // X = col
edBlitNumLp:
    ldy #ED_DIDX
    lda (dataPtr),y             // index
    ldy #ED_DCNT
    cmp (dataPtr),y             // index vs len
    bcs edBlitNumDone
    ldy #ED_DIDX
    lda (dataPtr),y             // index
    clc
    adc #ED_DIG
    tay                         // Y = ED_DIG + index
    lda (dataPtr),y             // digit char
    jsr edBlitChar              // blit at X; X=col+1
    ldy #ED_DIDX
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y             // index++
    jmp edBlitNumLp
edBlitNumDone:
    rts

// --- edBlitString -- blit a NUL-terminated .text string at (X=col, 24) ---
//  Input: kPtr = string, X = start col; returns X = col+len. The pointer
//  is kept in ED_PTR because kBlitCharRev clobbers kPtr.
edBlitString:
    lda kPtr
    ldy #ED_PTR
    sta (dataPtr),y             // save ptr lo
    lda kPtr+1
    iny
    sta (dataPtr),y             // save ptr hi
edBlitStrLp:
    ldy #ED_PTR
    lda (dataPtr),y
    sta kPtr
    iny
    lda (dataPtr),y
    sta kPtr+1                  // kPtr = saved ptr
    ldy #0
    lda (kPtr),y                // char
    beq edBlitStrDone
    pha                         // save char
    inc kPtr
    bne edBlitStrSave
    inc kPtr+1
edBlitStrSave:
    lda kPtr
    ldy #ED_PTR
    sta (dataPtr),y             // save advanced ptr lo
    lda kPtr+1
    iny
    sta (dataPtr),y             // save advanced ptr hi
    pla                         // A = char
    jsr edBlitChar              // blit at X; X=col+1
    jmp edBlitStrLp
edBlitStrDone:
    rts

edHint:  .byte 0
edInsMsg: .text "INS"; .byte 0
edOvrMsg: .text "OVR"; .byte 0
edCmdHint: .text "s(w) save(as) x exit q quit c clear m mark"; .byte 0
edCmdMarkHint: .text "y copy k cut p paste m done"; .byte 0
edConfirmHint: .text "modified! q to quit"; .byte 0
edClearConfirmHint: .text "press c again to clear"; .byte 0
edWriteAsMsg: .text "save as:"; .byte 0
edExistsMsg: .text "exists"; .byte 0
edSavedMsg: .text "saved"; .byte 0
edNoSpaceMsg: .text "no space"; .byte 0
edDirFullMsg: .text "dir full"; .byte 0
edLoadFailMsg: .text "load fail"; .byte 0
edSaveFailMsg: .text "save fail"; .byte 0

editUse:  .text "? edit <filename>"; .byte 0

// ==========================================
//  Mark / copy / cut / paste (region selection)
// ==========================================
//  STOP/M toggles mark mode (anchor = cursor on entry, cursor drags the
//  active end, STOP/M again clears). STOP/Y copies the selection to a
//  kMalloc clipboard (CR+NUL line separators, same as the save format);
//  STOP/K cuts it (same copy, then edCutDelete removes the span -- joining
//  the prefix/suffix onto one line when they fit in 40 cols, else keeping
//  the break); STOP/P pastes the clipboard at the cursor with insert
//  semantics. The selection highlight is drawn during edDrawLine via
//  edSelCache/edSelRange.

// --- edMoveSync -- after a cursor move: blink, l/c, and (mark mode) redraw ---
edMoveSync:
    jsr edSyncCursor
    jsr edDrawPos
    ldy #ED_MARK
    lda (dataPtr),y
    beq edMoveSyncRts
    jsr edDrawAll               // update the selection highlight
    jsr edSyncCursor            // edDrawAll erased the blink
edMoveSyncRts:
    rts

// --- edSelCache -- normalize (anchor, cursor) -> sl/sc/el/ec in ED_WINBUF ---
//  ED_WINBUF+0=sl, +1=sc, +2=el, +3=ec. Clobbers A, Y.
edSelCache:
    ldy #ED_ANCL
    lda (dataPtr),y             // anchor line
    ldy #ED_CURL
    cmp (dataPtr),y             // vs cursor line
    bcc escAnchor
    bne escCursor
    ldy #ED_ANCC
    lda (dataPtr),y             // anchor col
    ldy #ED_CURC
    cmp (dataPtr),y
    bcc escAnchor
    beq escAnchor
escCursor:
    ldy #ED_CURL
    lda (dataPtr),y
    ldy #ED_WINBUF
    sta (dataPtr),y             // sl = cursor
    ldy #ED_CURC
    lda (dataPtr),y
    ldy #ED_WINBUF+1
    sta (dataPtr),y             // sc
    ldy #ED_ANCL
    lda (dataPtr),y             // anchor line (can go stale after structural edits)
    ldy #ED_LINES
    cmp (dataPtr),y             // anchor vs ED_LINES
    bcc escElOk                 // anchor < ED_LINES -> ok
    lda (dataPtr),y             // ED_LINES
    sec
    sbc #1                      // clamp to ED_LINES-1
escElOk:
    ldy #ED_WINBUF+2
    sta (dataPtr),y             // el = min(anchor, ED_LINES-1)
    ldy #ED_ANCC
    lda (dataPtr),y
    ldy #ED_WINBUF+3
    sta (dataPtr),y             // ec
    rts
escAnchor:
    ldy #ED_ANCL
    lda (dataPtr),y
    ldy #ED_WINBUF
    sta (dataPtr),y             // sl = anchor
    ldy #ED_ANCC
    lda (dataPtr),y
    ldy #ED_WINBUF+1
    sta (dataPtr),y             // sc
    ldy #ED_CURL
    lda (dataPtr),y
    ldy #ED_WINBUF+2
    sta (dataPtr),y             // el = cursor
    ldy #ED_CURC
    lda (dataPtr),y
    ldy #ED_WINBUF+3
    sta (dataPtr),y             // ec
    rts

// --- edSelRange -- line ED_SCR: selection col range -> ED_DIDX (lo),
//  ED_DCNT (hi). A col is selected iff lo <= col < hi. Mark-off or a
//  line outside the selection yields lo=40/hi=0. Clobbers A, Y.
edSelRange:
    ldy #ED_MARK
    lda (dataPtr),y
    bne esrOn
    jmp esrNone
esrOn:
    ldy #ED_SCR
    lda (dataPtr),y             // line
    ldy #ED_WINBUF
    cmp (dataPtr),y             // vs sl
    bcc esrNone
    bne esrMid
    ldy #ED_WINBUF+1
    lda (dataPtr),y             // sc
    ldy #ED_DIDX
    sta (dataPtr),y             // lo = sc
    jmp esrHi
esrMid:
    lda #0
    ldy #ED_DIDX
    sta (dataPtr),y             // lo = 0
esrHi:
    ldy #ED_SCR
    lda (dataPtr),y             // line
    ldy #ED_WINBUF+2
    cmp (dataPtr),y             // vs el
    bcc esrHi40
    bne esrNone
    ldy #ED_WINBUF+3
    lda (dataPtr),y             // ec
    clc
    adc #1
    ldy #ED_DCNT
    sta (dataPtr),y             // hi = ec+1
    rts
esrHi40:
    lda #40
    ldy #ED_DCNT
    sta (dataPtr),y             // hi = 40
    rts
esrNone:
    lda #40
    ldy #ED_DIDX
    sta (dataPtr),y             // lo = 40
    lda #0
    ldy #ED_DCNT
    sta (dataPtr),y             // hi = 0
    rts

// --- edMarkToggle -- STOP/M: toggle mark mode; capture/clear the anchor ---
edMarkToggle:
    ldy #ED_MARK
    lda (dataPtr),y
    bne edMarkOff
    lda #1
    sta (dataPtr),y             // mark on
    ldy #ED_CURL
    lda (dataPtr),y
    ldy #ED_ANCL
    sta (dataPtr),y             // anchor line = cursor line
    ldy #ED_CURC
    lda (dataPtr),y
    ldy #ED_ANCC
    sta (dataPtr),y             // anchor col = cursor col
    jmp edMarkRedraw
edMarkOff:
    lda #0
    sta (dataPtr),y             // mark off
edMarkRedraw:
    jsr edDrawAll               // selection highlight appears/disappears
    jsr edSyncCursor
    jmp edCmdCancel

// --- edCbFree -- free the clipboard block (if any); ED_CB/ED_CBSZ = 0 ---
//  Clobbers A, Y, kPtr.
edCbFree:
    ldy #ED_CB
    lda (dataPtr),y
    iny
    ora (dataPtr),y             // base == 0?
    bne edCbFreeGo
    rts
edCbFreeGo:
    ldy #ED_CB
    lda (dataPtr),y
    sta kPtr
    iny
    lda (dataPtr),y
    sta kPtr+1
    jsr kFree
    lda #0
    ldy #ED_CB
    sta (dataPtr),y
    iny
    sta (dataPtr),y
    ldy #ED_CBSZ
    sta (dataPtr),y
    iny
    sta (dataPtr),y
    rts

// --- edCopyLineCount -- line ED_CARRY: ED_TCHAR = startCol, ED_STCOL = count ---
//  Selection bounds sl/sc/el/ec live in ED_WINBUF (from edSelCache).
//  A = count (0 if none). Clobbers A, X, Y, kPtr.
edCopyLineCount:
    ldy #ED_CARRY
    lda (dataPtr),y
    jsr edRecAddr               // kPtr = record
    ldy #0
    lda (kPtr),y                // len
    ldy #ED_DCNT
    sta (dataPtr),y             // ED_DCNT = len
    ldy #ED_CARRY
    lda (dataPtr),y             // line
    ldy #ED_WINBUF
    cmp (dataPtr),y             // vs sl
    bne eclSc0
    ldy #ED_WINBUF+1
    lda (dataPtr),y             // sc
    jmp eclScSet
eclSc0:
    lda #0
eclScSet:
    ldy #ED_TCHAR
    sta (dataPtr),y             // ED_TCHAR = startCol
    ldy #ED_DCNT
    cmp (dataPtr),y             // startCol vs len
    bcs eclNone
    ldy #ED_CARRY
    lda (dataPtr),y             // line
    ldy #ED_WINBUF+2
    cmp (dataPtr),y             // vs el
    bne eclEndLen
    ldy #ED_WINBUF+3
    lda (dataPtr),y             // ec
    jmp eclEndSet
eclEndLen:
    ldy #ED_DCNT
    lda (dataPtr),y
    sec
    sbc #1                      // len-1
eclEndSet:
    ldy #ED_STCOL
    sta (dataPtr),y             // ED_STCOL = endCol
    ldy #ED_DCNT
    cmp (dataPtr),y             // endCol vs len
    bcc eclEndOk
    ldy #ED_DCNT
    lda (dataPtr),y
    sec
    sbc #1
    ldy #ED_STCOL
    sta (dataPtr),y             // clamp endCol = len-1
eclEndOk:
    ldy #ED_STCOL
    lda (dataPtr),y             // endCol
    ldy #ED_TCHAR
    sec
    sbc (dataPtr),y             // - startCol
    clc
    adc #1
    ldy #ED_STCOL
    sta (dataPtr),y             // ED_STCOL = count
    rts                          // A = count
eclNone:
    lda #0
    ldy #ED_STCOL
    sta (dataPtr),y
    rts

// --- edCopyClip -- copy the current selection into a fresh kMalloc
//  clipboard. Normalizes the selection into ED_WINBUF itself (callers
//  need not call edSelCache first). On success returns normally with the
//  clipboard filled (ED_WINBUF sl/sc/el/ec left intact for a caller that
//  also needs the bounds, e.g. edCut). On kMalloc failure jumps straight
//  to edCopyFail ("no space", cancel back to editing) and never returns.
//  Returns C=1 (no clipboard, nothing freed) for a zero-width selection
//  (sl==el && sc==ec); C=0 otherwise. Shared by STOP/Y (copy) and STOP/K
//  (cut).
edCopyClip:
    jsr edSelCache              // normalize -> ED_WINBUF
    ldy #ED_WINBUF
    lda (dataPtr),y             // sl
    ldy #ED_WINBUF+2
    cmp (dataPtr),y             // vs el
    bne edCopyClipGo
    ldy #ED_WINBUF+1
    lda (dataPtr),y             // sc
    ldy #ED_WINBUF+3
    cmp (dataPtr),y             // vs ec
    bne edCopyClipGo
    sec                          // C=1 -> zero-width selection -> no-op
    rts
edCopyClipGo:
    jsr edCbFree                // release the old clipboard
    // size pass: total bytes -> ED_SCR
    lda #0
    ldy #ED_SCR
    sta (dataPtr),y
    iny
    sta (dataPtr),y
    ldy #ED_WINBUF
    lda (dataPtr),y             // sl
    ldy #ED_CARRY
    sta (dataPtr),y             // line = sl
edCopySizeLp:
    ldy #ED_CARRY
    lda (dataPtr),y
    ldy #ED_WINBUF+2
    cmp (dataPtr),y             // line vs el
    bcc edCopySizeGo
    beq edCopySizeGo
    jmp edCopySizeDone
edCopySizeGo:
    jsr edCopyLineCount         // ED_STCOL = count
    ldy #ED_CARRY
    lda (dataPtr),y             // line
    ldy #ED_WINBUF+2
    cmp (dataPtr),y             // vs el
    beq edCopySizeLast          // last line -> chars only, no separator
    ldy #ED_STCOL
    lda (dataPtr),y
    clc
    adc #2                      // + CR + NUL (separator before the next line)
    jmp edCopySizeAdd
edCopySizeLast:
    ldy #ED_STCOL
    lda (dataPtr),y             // last line: no trailing CR+NUL
edCopySizeAdd:
    ldy #ED_SCR
    clc
    adc (dataPtr),y
    sta (dataPtr),y
    bcc edCopySizeC1
    iny
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y
edCopySizeC1:
    ldy #ED_CARRY
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y             // line++
    jmp edCopySizeLp
edCopySizeDone:
    ldy #ED_SCR+1
    lda (dataPtr),y             // total hi
    tax
    ldy #ED_SCR
    lda (dataPtr),y             // total lo
    beq edCopyPages
    inx                          // lo > 0 -> pages = hi + 1
edCopyPages:
    txa
    bne edCopyAlloc             // pages > 0 -> allocate
    clc                          // C=0 -> nothing to copy (defensive)
    rts
edCopyAlloc:
    jsr kMalloc
    bcc edCopyAllocOk
    jmp edCopyFail
edCopyAllocOk:
    lda kPtr
    ldy #ED_CB
    sta (dataPtr),y
    iny
    lda kPtr+1
    sta (dataPtr),y             // ED_CB = base
    ldy #ED_SCR
    lda (dataPtr),y
    ldy #ED_CBSZ
    sta (dataPtr),y
    ldy #ED_SCR+1
    lda (dataPtr),y
    ldy #ED_CBSZ+1
    sta (dataPtr),y             // ED_CBSZ = total
    lda kPtr
    ldy #ED_PTR
    sta (dataPtr),y
    lda kPtr+1
    iny
    sta (dataPtr),y             // ED_PTR = dest
    ldy #ED_WINBUF
    lda (dataPtr),y
    ldy #ED_CARRY
    sta (dataPtr),y             // line = sl
edCopyPassLp:
    ldy #ED_CARRY
    lda (dataPtr),y
    ldy #ED_WINBUF+2
    cmp (dataPtr),y             // line vs el
    bcc edCopyPassGo
    beq edCopyPassGo
    jmp edCopyPassDone
edCopyPassGo:
    jsr edCopyLineCount         // ED_TCHAR=startCol, ED_STCOL=count, kPtr=record
    ldy #ED_TCHAR
    lda (dataPtr),y             // startCol
    clc
    adc kPtr
    sta srcPtr
    lda kPtr+1
    adc #0
    sta srcPtr+1
    inc srcPtr
    bne edCopyPass1
    inc srcPtr+1                // srcPtr = record + startCol + 1
edCopyPass1:
    ldy #ED_PTR
    lda (dataPtr),y
    sta kPtr
    iny
    lda (dataPtr),y
    sta kPtr+1                  // kPtr = dest
    ldy #ED_STCOL
    lda (dataPtr),y
    ldy #ED_DCNT
    sta (dataPtr),y             // ED_DCNT = count
edCopyCharLp:
    ldy #ED_DCNT
    lda (dataPtr),y
    beq edCopyEol
    ldy #0
    lda (srcPtr),y
    sta (kPtr),y
    inc srcPtr
    bne edCopyChar1
    inc srcPtr+1
edCopyChar1:
    inc kPtr
    bne edCopyChar2
    inc kPtr+1
edCopyChar2:
    ldy #ED_DCNT
    lda (dataPtr),y
    sec
    sbc #1
    sta (dataPtr),y
    jmp edCopyCharLp
edCopyEol:
    ldy #ED_CARRY
    lda (dataPtr),y              // line
    ldy #ED_WINBUF+2
    cmp (dataPtr),y              // vs el
    beq edCopyEolLast            // last line -> no CR+NUL separator
    ldy #0
    lda #$0D
    sta (kPtr),y
    iny
    lda #$00
    sta (kPtr),y
    lda kPtr
    clc
    adc #2
    sta kPtr
    bcc edCopyEol1
    inc kPtr+1
edCopyEol1:
edCopyEolLast:
    lda kPtr
    ldy #ED_PTR
    sta (dataPtr),y
    lda kPtr+1
    iny
    sta (dataPtr),y             // ED_PTR = advanced dest
    ldy #ED_CARRY
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y             // line++
    jmp edCopyPassLp
edCopyPassDone:
    clc                          // C=0 -> clipboard filled (bounds still in ED_WINBUF)
    rts
edCopyFail:
    lda #<edNoSpaceMsg
    sta kPtr
    lda #>edNoSpaceMsg
    sta kPtr+1
    jsr edShowMsg
    jsr edDrawHint
    lda #0
    ldy #ED_CMD
    sta (dataPtr),y
    jsr kEnableCursor
    jsr edDrawMode
    jsr edDrawSize
    jmp editLoop

// --- edCopy -- STOP/Y: copy the selection to the clipboard ---
edCopy:
    ldy #ED_MARK
    lda (dataPtr),y
    bne edCopyMarked            // mark on -> proceed
    jmp edCmdCancel             // no mark -> no-op
edCopyMarked:
    jsr edCopyClip              // C=1 -> zero-width; no-return on kMalloc failure
    bcc edCopyFilled
    jmp edCmdCancel             // zero-width selection -> no-op (mark stays)
edCopyFilled:
    lda #0
    ldy #ED_MARK
    sta (dataPtr),y             // yank ends the selection -- drop the highlight
    jsr edDrawAll               // redraw the body without the selection
    jsr edSyncCursor            // edDrawAll erased the blink
    jmp edCmdCancel

// --- edCut -- STOP/K: cut (copy + delete) the selection ---
edCut:
    ldy #ED_MARK
    lda (dataPtr),y
    bne edCutMarked              // mark on -> proceed
    jmp edCmdCancel              // no mark -> no-op
edCutMarked:
    jsr edCopyClip                // C=1 -> zero-width; no-return on kMalloc failure
    bcc edCutFilled
    jmp edCmdCancel               // zero-width selection -> no-op (mark stays)
edCutFilled:
    jsr edCutDelete                // removes the selected span from the document
    lda #0
    ldy #ED_MARK
    sta (dataPtr),y               // cut ends the selection -- drop the highlight
    lda #0
    ldy #ED_CMD
    sta (dataPtr),y               // back to editing
    jsr kEnableCursor
    jsr edAfterEdit                // full redraw (ED_FLAG) + cursor + l/c + size
    jsr edDrawMode                 // restore INS/OVR (edDrawCmd wiped it)
    jsr edDrawHint                 // restore the hint
    jmp editLoop

// --- edCutDelete -- remove the selection (ED_WINBUF sl/sc/el/ec) from the
//  document. Keeps sl's prefix [0..sc) and el's suffix (ec+1..len), joining
//  them onto one line when they fit in 40 cols, else keeping the break
//  (Decision #8's no-reflow rule) with el's trimmed suffix becoming the
//  line right after sl. Fully-consumed lines between them are removed via
//  edDelLine (chunk grow/shrink already handled there). Cursor lands at
//  (sl, sc); ED_SIZE is reduced by the exact selected char count (computed
//  up front via edCopyLineCount, the same per-line counter edCopyClip
//  uses). Clobbers A, X, Y, kPtr, srcPtr.
edCutDelete:
    // --- selected char count (excludes CR/NUL -- those aren't real chars
    //  in the document) -> ED_SCR (16-bit) ---
    lda #0
    ldy #ED_SCR
    sta (dataPtr),y
    iny
    sta (dataPtr),y
    ldy #ED_WINBUF
    lda (dataPtr),y              // sl
    ldy #ED_CARRY
    sta (dataPtr),y              // line = sl
edCutCntLp:
    ldy #ED_CARRY
    lda (dataPtr),y
    ldy #ED_WINBUF+2
    cmp (dataPtr),y              // line vs el
    bcc edCutCntGo
    beq edCutCntGo
    jmp edCutCntDone
edCutCntGo:
    jsr edCopyLineCount          // ED_STCOL = count for this line
    ldy #ED_STCOL
    lda (dataPtr),y
    ldy #ED_SCR
    clc
    adc (dataPtr),y
    sta (dataPtr),y
    bcc edCutCntC1
    iny
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y
edCutCntC1:
    ldy #ED_CARRY
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y              // line++
    jmp edCutCntLp
edCutCntDone:
    // --- el's suffix: elLen -> ED_TMPL, el's record -> ED_REC ---
    ldy #ED_WINBUF+2
    lda (dataPtr),y              // el
    jsr edRecAddr                 // kPtr = el's record (stable -- nothing deleted yet)
    ldy #0
    lda (kPtr),y                  // elLen
    ldy #ED_TMPL
    sta (dataPtr),y               // TMPL = elLen
    lda kPtr
    ldy #ED_REC
    sta (dataPtr),y
    lda kPtr+1
    iny
    sta (dataPtr),y               // REC = el's record address
    ldy #ED_WINBUF+3
    lda (dataPtr),y               // ec
    clc
    adc #1
    ldy #ED_TCHAR
    sta (dataPtr),y               // TCHAR = ec+1 (suffix start col in el)
    ldy #ED_TMPL
    lda (dataPtr),y               // elLen
    sec
    ldy #ED_TCHAR
    sbc (dataPtr),y               // elLen - (ec+1)
    bcs edCutSufOk
    lda #0                         // underflow (stale bounds) -> clamp 0
edCutSufOk:
    ldy #ED_DCNT
    sta (dataPtr),y               // DCNT = suffixLen
    ldy #ED_WINBUF+1
    lda (dataPtr),y               // sc
    clc
    ldy #ED_DCNT
    adc (dataPtr),y               // sc + suffixLen
    ldy #ED_DIDX
    sta (dataPtr),y               // DIDX = combined length
    cmp #41
    bcc edCutMerge                // <= 40 -> merge onto sl
    jmp edCutBreak                // > 40 -> keep the break
edCutMerge:
    // src = el.rec + 1 + (ec+1) = REC + TCHAR + 1
    ldy #ED_REC
    lda (dataPtr),y
    ldy #ED_TCHAR
    clc
    adc (dataPtr),y
    sta srcPtr
    ldy #ED_REC+1
    lda (dataPtr),y
    adc #0
    sta srcPtr+1
    inc srcPtr
    bne edCutM1
    inc srcPtr+1
edCutM1:
    // dest = sl.rec + 1 + sc
    ldy #ED_WINBUF
    lda (dataPtr),y                // sl
    jsr edRecAddr                   // kPtr = sl's record
    ldy #ED_WINBUF+1
    lda (dataPtr),y                 // sc
    clc
    adc kPtr
    sta kPtr
    bcc edCutM2
    inc kPtr+1
edCutM2:
    inc kPtr
    bne edCutM3
    inc kPtr+1
edCutM3:
edCutMCopyLp:
    ldy #ED_DCNT
    lda (dataPtr),y
    beq edCutMCopyDone
    ldy #0
    lda (srcPtr),y
    sta (kPtr),y
    inc srcPtr
    bne edCutMCopy1
    inc srcPtr+1
edCutMCopy1:
    inc kPtr
    bne edCutMCopy2
    inc kPtr+1
edCutMCopy2:
    ldy #ED_DCNT
    lda (dataPtr),y
    sec
    sbc #1
    sta (dataPtr),y
    jmp edCutMCopyLp
edCutMCopyDone:
    ldy #ED_WINBUF
    lda (dataPtr),y                 // sl
    jsr edRecAddr                    // kPtr = sl's record (fresh)
    ldy #ED_DIDX
    lda (dataPtr),y                  // combined length
    ldy #0
    sta (kPtr),y                     // sl.len = sc + suffixLen
    ldy #ED_WINBUF+2
    lda (dataPtr),y                  // el
    ldy #ED_WINBUF
    sec
    sbc (dataPtr),y                  // el - sl = lines to delete
    ldy #ED_STCOL
    sta (dataPtr),y
    jmp edCutSizeLp
edCutBreak:
    // truncate sl in place: sl.len = sc (prefix bytes untouched)
    ldy #ED_WINBUF
    lda (dataPtr),y
    jsr edRecAddr                    // kPtr = sl's record
    ldy #ED_WINBUF+1
    lda (dataPtr),y                  // sc
    ldy #0
    sta (kPtr),y                     // sl.len = sc
    // trim el in place: shift [ec+1..elLen) down to [0..suffixLen)
    ldy #ED_REC
    lda (dataPtr),y
    ldy #ED_TCHAR
    clc
    adc (dataPtr),y                  // REC + (ec+1)
    sta srcPtr
    ldy #ED_REC+1
    lda (dataPtr),y
    adc #0
    sta srcPtr+1
    inc srcPtr
    bne edCutB1
    inc srcPtr+1
edCutB1:
    ldy #ED_REC
    lda (dataPtr),y
    sta kPtr
    ldy #ED_REC+1
    lda (dataPtr),y
    sta kPtr+1
    inc kPtr
    bne edCutB2
    inc kPtr+1
edCutB2:
edCutBCopyLp:
    ldy #ED_DCNT
    lda (dataPtr),y
    beq edCutBCopyDone
    ldy #0
    lda (srcPtr),y
    sta (kPtr),y
    inc srcPtr
    bne edCutBCopy1
    inc srcPtr+1
edCutBCopy1:
    inc kPtr
    bne edCutBCopy2
    inc kPtr+1
edCutBCopy2:
    ldy #ED_DCNT
    lda (dataPtr),y
    sec
    sbc #1
    sta (dataPtr),y
    jmp edCutBCopyLp
edCutBCopyDone:
    ldy #ED_DIDX
    lda (dataPtr),y                  // combined = sc + suffixLen
    ldy #ED_WINBUF+1
    sec
    sbc (dataPtr),y                  // suffixLen
    pha
    ldy #ED_REC
    lda (dataPtr),y
    sta kPtr
    ldy #ED_REC+1
    lda (dataPtr),y
    sta kPtr+1
    pla
    ldy #0
    sta (kPtr),y                     // el.len = suffixLen
    ldy #ED_WINBUF+2
    lda (dataPtr),y                  // el
    ldy #ED_WINBUF
    sec
    sbc (dataPtr),y                  // el - sl
    sec
    sbc #1                            // - 1 -> middle-only count
    ldy #ED_STCOL
    sta (dataPtr),y
edCutSizeLp:
    ldy #ED_SCR
    lda (dataPtr),y
    ldy #ED_SCR+1
    ora (dataPtr),y
    beq edCutDelLines
    jsr edSizeDec
    ldy #ED_SCR
    lda (dataPtr),y
    bne edCutSizeDecLo
    ldy #ED_SCR+1
    lda (dataPtr),y
    sec
    sbc #1
    sta (dataPtr),y
    lda #$FF
    ldy #ED_SCR
    sta (dataPtr),y
    jmp edCutSizeLp
edCutSizeDecLo:
    sec
    sbc #1
    ldy #ED_SCR
    sta (dataPtr),y
    jmp edCutSizeLp
edCutDelLines:
    ldy #ED_STCOL
    lda (dataPtr),y
    beq edCutDelLinesDone
edCutDelLinesLp:
    ldy #ED_WINBUF
    lda (dataPtr),y
    clc
    adc #1                            // sl+1 (fixed target -- later lines shift into it)
    jsr edDelLine
    ldy #ED_STCOL
    lda (dataPtr),y
    sec
    sbc #1
    sta (dataPtr),y
    bne edCutDelLinesLp
edCutDelLinesDone:
    ldy #ED_WINBUF
    lda (dataPtr),y
    ldy #ED_CURL
    sta (dataPtr),y
    ldy #ED_WINBUF+1
    lda (dataPtr),y
    ldy #ED_CURC
    sta (dataPtr),y
    ldy #ED_PREFC
    sta (dataPtr),y
    jsr edModSet
    lda #1
    ldy #ED_FLAG
    sta (dataPtr),y
    rts

// --- edPasteAdv -- advance ED_PTR and decrement ED_STCOL/ED_STIDX ---
//  ED_PTR survives edInsChar/edReturn/edInsLine (ED_REC does not --
//  edInsLine reuses it as the chunk-base scratch). Clobbers A, Y.
edPasteAdv:
    ldy #ED_PTR
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    bcc edPaRem
    iny
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y
edPaRem:
    ldy #ED_STCOL
    lda (dataPtr),y
    bne edPaLo
    ldy #ED_STIDX
    lda (dataPtr),y
    bne edPaHi
    rts
edPaHi:
    sec
    sbc #1
    sta (dataPtr),y
    lda #$FF
    ldy #ED_STCOL
    sta (dataPtr),y
    rts
edPaLo:
    ldy #ED_STCOL
    lda (dataPtr),y
    sec
    sbc #1
    sta (dataPtr),y
    rts

// --- edPaste -- STOP/P: insert the clipboard at the cursor (insert mode) ---
edPaste:
    ldy #ED_CB
    lda (dataPtr),y
    iny
    ora (dataPtr),y             // base == 0?
    bne edPasteGo
    jmp edCmdCancel             // empty clipboard -> no-op
edPasteGo:
    ldy #ED_CB
    lda (dataPtr),y
    ldy #ED_PTR
    sta (dataPtr),y
    ldy #ED_CB+1
    lda (dataPtr),y
    ldy #ED_PTR+1
    sta (dataPtr),y             // ED_PTR = read ptr (survives edReturn/edInsLine)
    ldy #ED_CBSZ
    lda (dataPtr),y
    ldy #ED_STCOL
    sta (dataPtr),y
    ldy #ED_CBSZ+1
    lda (dataPtr),y
    ldy #ED_STIDX
    sta (dataPtr),y             // ED_STCOL/STIDX = remaining
edPasteLp:
    ldy #ED_STCOL
    lda (dataPtr),y
    ldy #ED_STIDX
    ora (dataPtr),y
    bne edPasteChar
    jmp edPasteDone
edPasteChar:
    ldy #ED_PTR
    lda (dataPtr),y
    sta kPtr
    iny
    lda (dataPtr),y
    sta kPtr+1
    ldy #0
    lda (kPtr),y                // A = byte
    cmp #$0D
    bne edPasteNotCr
    jsr edPasteAdv              // skip CR
    jsr edPasteAdv              // skip NUL
    jsr edReturn                // split the line at the cursor
    jmp edPasteLp
edPasteNotCr:
    cmp #$00
    bne edPasteIns
    jsr edPasteAdv              // skip a stray NUL
    jmp edPasteLp
edPasteIns:
    pha                          // save char (edPasteAdv clobbers A)
    jsr edPasteAdv
    pla                          // A = char
    jsr edInsChar
    jmp edPasteLp
edPasteDone:
    lda #0
    ldy #ED_MARK
    sta (dataPtr),y             // content moved -- the old anchor is stale
    jsr edDrawAll
    jsr edSyncCursor
    jmp edCmdCancel

.assert "edit: 24 records fit a 4-page chunk", (REC_SIZE * RECS_PER_CHUNK <= CHUNK_PAGES * POOL_PAGE_SIZE) ? 1 : 0, 1
.assert "edit: instance buffer fits data_size", (ED_WINBUF + 128 <= ED_DATA_SIZE) ? 1 : 0, 1
.assert "edit: clipboard fields fit data_size", (ED_CBSZ + 1 < ED_DATA_SIZE) ? 1 : 0, 1
