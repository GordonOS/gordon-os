// ==========================================
//  grpaint.inc -- surface, display, pointer (steps 4.1-4.2)
// ==========================================
//  The canvas, the hand-over, the 1351-driven pointer sprite, the palette band with its
//  tools and its two arrows, the shape and text tools, the .pic save and load, and the
//  undo/redo ring.
//
//  Five things this file is deliberately careful about, all of them documented
//  bugs or hardware facts:
//
//  1. THE OUTGOING BLINK BLOCK MUST BE UN-DRAWN FIRST. The block cursor is drawn
//     into the framebuffer PIXELS, and kSwitchView copies the framebuffer
//     verbatim into the outgoing surface's REU slot -- so a block drawn at that
//     instant is baked into the shell's saved image, dgsDone then clears
//     cursorVisible, and the shell ends up with a phantom cursor at that cell
//     kBlinkReset erases the block at the SAVED position of
//     the displayed surface, which is still the shell's. The SEI keeps an IRQ
//     tick from drawing it back, and no syscall may run inside that window (the
//     dispatcher re-enables IRQs).
//
//  2. ONLY SYS_EXIT AT THE END -- never kFreeScreen. sysExit's transferFocusAway
//     runs while the surface still exists, so it hands the display to another
//     owner and frees the slot. Exiting *surfaceless* is the broken state: the
//     display blanks and kbFocus goes to $FF.
//
//  3. A FAILED kAllocScreen MUST STILL SAY SOMETHING. There are 28 surface slots
//     and the shell holds one, so this is reachable -- and by 4.2's leak box it is
//     the symptom a leaked slot produces. Our own surface is exactly what we
//     could not get, so the message goes on the SHELL's screen instead, the way
//     every .com does. A failure AFTER the canvas exists hands the display back
//     first, for the same reason in reverse: ours is what is on screen, so a
//     message written anywhere else could not be seen.
//
//  4. THE POINTER IS A SPRITE, AND SPRITES ARE THE DISPLAY OWNER'S. Every sprite
//     call opens with `curTask == kbFocus` and writes nothing otherwise, so the
//     whole setup sits AFTER kSwitchView -- and a task that never holds the
//     display cannot drive one at all.
//
//  5. THE POINTER MOVES ON THE DISPLAY'S CLOCK, NOT THE SCHEDULER'S. The tick is
//     59.2 Hz against a 50.1 Hz display, so a per-tick pointer is sampled 1.18
//     times a frame -- some frames two steps, the next none. `frameCount` is the
//     frame clock (published at raster 0), and it is compared for CHANGE: it is
//     8-bit and wraps, so elapsed-frame arithmetic on it is wrong.
//
//  6. NO SINGLE POT SAMPLE IS TRUSTWORTHY. A sample can read one count high: the
//     pot noise lands in the raw byte's low bit -- the 1351's qualifier, the one
//     the protocol says to drop -- except that a +1 on an odd byte carries into
//     the position field. The noise only ever ADDS, so the SMALLEST reading in a
//     frame is the true one, and a frame moves the pointer by the change in that
//     minimum rather than by its last sample.
//
//  7. THE STROKE IS DRAWN ONCE PER DISPLAYED FRAME, NOT ONCE PER READ. The
//     frame's endpoint is the one the noise tracker has corrected, whereas the
//     per-read positions still carry the +-1 jitter -- so painting from them would
//     bake into the canvas exactly what the tracker keeps out of the pointer.
//     kPlot is what draws it: it sets the pixel AND its cell's colour, which in
//     hires is what that pixel will show.

grpaint:
    //  --- the picture's name, out of the argument, FIRST ---
    //  Not tidiness: the loader delivers the arguments into the instance buffer's
    //  first 65 bytes, and the fields they sit over are the ones gpCanvas and gpInit
    //  are about to write -- so the name is copied up to GP_NAME before either runs.
    jsr gpArg
    //  --- and the colours and the mode, before anything needs either ---
    //  gpCols resolves <base>.pic: a picture that EXISTS is a load and its own colours
    //  then win, while a new one takes the colours from the rest of the command line.
    //  It runs here, before the canvas, so the canvas is allocated in its FINAL mode.
    jsr gpCols
    //  --- the gate: with no pointer declared there is no input at all ---
    //  A 1351 is the only drawing input, by decision, so with mouseKind off this
    //  program could hold a canvas and never draw on it. It says so on the
    //  SHELL's screen and leaves WITHOUT taking a surface -- the shell's display
    //  is untouched, which is the whole point of refusing.
    lda mouseKind
    bne gpOpenUi
    lda #<gpMsgNoMouse
    ldx #>gpMsgNoMouse
    jmp gpFail

gpOpenUi:
    //  --- the UI's own image: resolved ONCE, for BOTH modes, read on every right click ---
    //  The palette and the tools are a FILE, not code. kReuOpen scans the
    //  directory and leaves a 5-byte record (size then base) in our instance
    //  buffer, which is all kReuReadAt needs to drop the image straight into the
    //  band -- so the right click reads the file instead of composing the band,
    //  and the code image holds no picture at all. Resolving them here rather than on
    //  the gesture keeps the directory scan off it: this is a name lookup, not a
    //  read, and the contents are not touched until the band needs them.
    //  BOTH MODES, because the MODE tile would otherwise re-resolve on its press -- and
    //  a scan is a frame long, a frame the pointer is not sampled over, which is a
    //  press that never happened. The record's own size byte is
    //  cleared first, so a mode with no band file is one whose record reads back zero.
    //  A missing file for the mode we are IN is an install error like the missing mouse
    //  above, so it is reported on the SHELL's screen and leaves before any surface is
    //  taken; the other mode's is not fatal, and the MODE tile simply refuses to go
    //  there.
    //  --- the band's own face, for the mode the picture is in ---
    //  The mode and the palette are gpCols's business: they are settled from the
    //  arguments or from the file before the canvas exists, so GP_MODE is what says
    //  which record is the one in hand.
    lda #0
    ldy #GP_UI
    sta (dataPtr),y              // the hires band: no record...
    ldy #GP_UI_MC
    sta (dataPtr),y              // ...and no MC one
    ldy #GP_MODE
    lda (dataPtr),y
    jsr gpUiOpen                 // the mode in hand: a failure here is fatal
    bcc gpOpenUiAlt
    lda #<gpMsgNoUi
    ldx #>gpMsgNoUi
    jmp gpFail
gpOpenUiAlt:
    ldy #GP_MODE
    lda (dataPtr),y
    eor #1                       // 2 <-> 3: the other mode's band, resolved now too so
    jsr gpUiOpen                 //   that the MODE tile never has to
    jmp gpAlloc

gpAlloc:
    //  --- the canvas, in the picture's own colours and mode ---
    //  kAllocScreen takes the four VIC colours in kArg..kArg+3 and the mode in A, and
    //  it hands back a slot already cleared (pixels = 0, matrix bg = bg0), so a blank
    //  canvas needs no clear pass. The colours are gpCols's: the border, then the
    //  picture's bg0, bg1 and bg2 -- which in MC are the c1/c2 the matrix carries, the
    //  convention the .pic header uses too, and in hires the paper's OWN record, since
    //  a clear pixel shows its cell's low nibble rather than $D021 (VIC-II 3.7.3.3).
    //  The mode is the task's own tag, so a picture being LOADED is allocated in the
    //  mode its file declares instead of being re-tagged once the picture is in.
    lda gpColBord
    sta kArg                     // border
    ldy #GP_MB
    lda (dataPtr),y
    sta kArg+1                   // bg0 -- the paper (hires) / the value-0 colour (MC)
    iny
    lda (dataPtr),y
    sta kArg+2                   // bg1 / c1
    iny
    lda (dataPtr),y
    sta kArg+3                   // bg2 / c2
    ldy #GP_MODE
    lda (dataPtr),y
    sec
    sbc #GP_MODE_STD             // the tag -> the allocator's own 0 / 1
    jsr kAllocScreen
    bcc gpCanvas
    lda #<gpMsgNoCanvas          // all 28 slots are taken
    ldx #>gpMsgNoCanvas
    jmp gpFail

gpCanvas:
    //  --- the cells start at the palette, so a draw on a fresh canvas is visible ---
    //  In hires a cell's colour IS its nibble, and the canvas is black, so the low
    //  nibble is 0 and the high one is the ink; in MC it is (c1<<4)|c2 with c3 in the
    //  colour RAM. gpCells is the one place that knows, and it is what every path that
    //  changes the palette or the ink's meaning calls.
    jsr gpCells

    jsr kDisableCursor           // a canvas shows no blink block

    //  --- show it: kSwitchView displays our surface AND gives us kbFocus ---
    lda #SYS_GETTID
    jsr syscall
    tax                          // X = our slot
    php
    sei
    txa
    pha
    jsr kBlinkReset              // clobbers A, X, Y, kPtr, kArg+7
    pla
    tax
    jsr kSwitchView
    plp

    //  --- the picture the name already stands for, if there is one ---
    //  After the hand-over, because the load writes the canvas's own addresses and
    //  they are the canvas only while it is the surface on screen.
    jsr gpLoadStart

    //  --- and the history starts from HERE: the picture the load left is the first
    //      state, so the very first stroke has something to step back to ---
    jsr gpUndoInit

    //  --- the pointer: art, colours, enable, and a centred start ---
    //  A routine rather than an inline block so that its failure path (gpNoArt,
    //  which hands the display back) sits within branch range of the calls that
    //  can fail -- and so that the main flow stays readable.
    jsr gpInit

    //  --- and the band starts UP ---
    //  The pointer arrives over the canvas centre with nothing to say what the palette
    //  and the tiles are, so the UI opens with the program. This is the right press's
    //  own path, so it comes up exactly as it does on a click: gpPickOpen checks the
    //  display is ours, cancels nothing (no drag can be in progress yet), reads the
    //  image in and records the open. A left press that lands on the canvas then
    //  dismisses the band and draws, which is the model the picker already has.
    //  AFTER gpInit, because the marks gpPickShow draws read the tool and the dirty
    //  flag, which gpInit is what sets.
    jsr gpPickOpen

    //  --- the main loop. 'q' leaves; every other key is ignored for now, because
    //      the tool keys arrive with the tools. ---
gpLoop:
    jsr kReadKey                 // focus-guarded: only keys aimed at us arrive
    beq gpIdle
    sta gpChr                    // the key IS the character the pen would write
    ldy #GP_TEXT
    lda (dataPtr),y
    beq gpLoopQuit               // no pen down: the keyboard is the program's again
    jsr gpTextKey                // ...while it is, every key is text -- 'q' included.
    jmp gpLoop                   //    A CALL, not a jump: gpTextKey and its helpers are
                                 //    tail-called (jmp gpTextPut), so their rts returns
                                 //    to whoever called the handler -- and a jmp here
                                 //    would leave them popping the frame gpInit's jsr
                                 //    put on the stack, i.e. resuming gpCanvas

//  --- the loop's key, once no pen is down ---
//  'q' leaves, 'u' and 'r' step through the history. While the pen IS down every key is
//  text, so a 'u' types a u and never reaches here -- which is the only way a text tool
//  can have a u in it.
gpLoopQuit:
    lda gpChr
    cmp #'q'
    beq gpQuitAsk
    cmp #'u'
    beq gpLoopBack
    cmp #'r'
    beq gpLoopFwd
    jmp gpLoop
gpLoopBack:
    jsr gpUndoBack               // the same step the band's left arrow takes
    jmp gpLoop
gpLoopFwd:
    jsr gpUndoFwd
    jmp gpLoop
gpIdle:
    lda #SYS_YIELD               // the frame counter is the clock, not licence
    jsr syscall                  // to spin
    ldy #GP_FRAME
    lda frameCount
    cmp (dataPtr),y
    beq gpIdle                   // the display has not advanced a frame
    sta (dataPtr),y
    //  --- and the band's state as the frame BEGAN, before the picker can put it away:
    //      the flood fill reads it, so a press that lands while the UI is up is told
    //      from one made with the band already down ---
    ldy #GP_MENU
    lda (dataPtr),y
    ldy #GP_MENU0
    sta (dataPtr),y
    //  --- the buttons as the last frame saw them, for the click edge the line tool
    //      counts on (gpReadFold below is about to overwrite GP_BTN) ---
    ldy #GP_BTN
    lda (dataPtr),y
    ldy #GP_PREV
    sta (dataPtr),y
    jsr gpSample
    jsr gpPicker                 // the right button's toggle, and the picks from the band
    jsr gpFlash                  // the quit warning's flash, while it is on the table
    jsr gpDraw                   // the left button: the pencil while it is held
    jmp gpLoop

// --- gpQuitAsk -- 'q': leave, unless there is work only this machine has ---
//  A press with the canvas as the file holds it leaves outright. With something drawn
//  since the last save it ASKS instead: the band comes up if it is not up already, and
//  the SAVE tile flashes (gpFlash, gpMark) until the user either saves -- which is the
//  tile's own press and leaves the program where it is -- or presses 'q' again, which
//  leaves with whatever the canvas holds.
//  THE SECOND PRESS ONLY COUNTS WHILE THE QUESTION IS ON SCREEN: the warning IS the band
//  and its flashing tile, so a band that has been dismissed is a question nobody can see
//  any more -- and 'q' then asks it again, raising the band, rather than leaving on a
//  warning the user never had in view. GP_WARN goes on meaning that a 'q' has warned.
gpQuitAsk:
    ldy #GP_DIRTY
    lda (dataPtr),y
    beq gpQuit                   // nothing unsaved: there is no question to ask
    ldy #GP_MENU
    lda (dataPtr),y
    beq gpQuitAskWarn            // the band is DOWN: whatever was asked is not on screen
    ldy #GP_WARN
    lda (dataPtr),y
    bne gpQuit                   // asked, and still on screen: this press answers it
gpQuitAskWarn:
    lda #1
    ldy #GP_WARN
    sta (dataPtr),y              // ask -- or ask again, the band having been dismissed
    ldy #GP_MENU
    lda (dataPtr),y
    bne gpLoop                   // the band is up: the flash is the warning
    jsr gpPickOpen               // ...otherwise it comes back up, which is the warning
    jmp gpLoop

// --- gpQuit -- empty the key queue, then exit ---
//  A press leaves its RELEASE behind, and a release is a byte the queue holds like
//  any other; kScanKeyboard reports an empty queue and a release identically, so
//  "empty" cannot be tested for -- drain a fixed 16 reads, which is the queue's
//  capacity. The counter rides the stack because kReadKey clobbers X and Y.
gpQuit:
    ldx #16
gpDrainLp:
    txa
    pha
    jsr kReadKey
    pla
    tax
    dex
    bne gpDrainLp
    //  --- release the pointer. The enable byte is OURS while we hold the display
    //      (the shadow belongs to the displayed task), and clearing it is what
    //      makes the hand-over show no pointer at all. ---
    sprOn(0)
    //  SYS_EXIT frees the surface and hands the display back -- it is the only
    //  exit this task uses (see 2 in the header).
gpExit:
    lda #SYS_EXIT
    jsr syscall

// --- gpFail -- report on a screen of our OWN, hold it, then exit ---
//  In: A/X = the message's lo/hi.
//  THIS SHAPE IS sidplay's, and it is not a style choice: a task with no screen of
//  its own can print nothing, so a failure that just returns to the prompt is
//  indistinguishable from a crash. The message goes on a screen we allocate, and
//  it waits for a key so it can be read.
//  Every colour is set EXPLICITLY, and the border is dark grey
//  (DEFAULT_BORDER_COLOR, as in grview's refusal) so the screen can never be
//  mistaken for the canvas -- an implicitly coloured screen is how a message ends
//  up invisible.
//  kPtr cannot carry the message across the calls -- a syscall may clobber it -- so
//  the pair rides the task's own stack, which a context switch cannot reach.
//  If the allocation fails there is no screen to say anything on and the task
//  leaves silently: it cannot print, so there is nothing else it can do.
//  gpSay is the same report as a CALLABLE routine (it returns once a key has been
//  pressed); gpFail is the exit form every caller uses.
gpSay:
    pha                          // park the message's lo...
    txa
    pha                          // ...and its hi above it
    lda #$0b                     // border: dark grey
    sta kArg
    lda #$00                     // bg0/bg1/bg2: black
    sta kArg+1
    sta kArg+2
    sta kArg+3
    lda #0                       // mode 0: hires
    jsr kAllocScreen
    bcs gpSayRet                 // no free slot: nothing to say it on
    ldx #0
    ldy #0
    jsr kSetCursor               // the message prints at home
    lda #SYS_GETTID
    jsr syscall
    tax
    php
    sei
    txa
    pha
    jsr kBlinkReset              // clobbers A, X, Y, kPtr, kArg+7
    pla
    tax
    jsr kSwitchView              // the display and the keyboard focus move to us
    plp
    pla                          // the message's hi
    sta kPtr+1
    pla                          // ...and its lo
    sta kPtr
    jsr kPrintString
gpSayRet:
    rts

//  --- gpFail -- report and exit. In: A/X = the message's lo/hi. ---
//  The wait lives HERE, not in gpSay: a caller that has more to print would
//  otherwise hold the screen before it could print it.
gpFail:
    jsr gpSay
    jsr kNewline
    jsr kWaitKey                 // hold it until it is acknowledged
gpFailOut:
    jmp gpQuit                   // ...which drains the release, and exits

// --- gpInit -- bring the pointer sprite up ---
//  The pointer is a SPRITE from the start, not a marker drawn into the canvas:
//  anything drawn in would have to be UN-drawn, and un-drawing is the snapshot
//  machinery (step 4.10). A sprite is removed by disabling it.
//  Every call here is display-owner guarded, which is why this runs after the
//  switch. kSpriteSheet is the expensive one (~1,000-3,000 cycles, SEI'd) and is
//  resolved ONCE, never in the frame loop.
gpInit:
    //  --- the colours FIRST, before any art is resolved: a sprite.lib setter is
    //      what makes the kernel install OUR live sprite state in place of the
    //      previous owner's (sprInstall, idempotent, at the top of every setter).
    //      A record written before that install lands in the PREVIOUS owner's
    //      state and is saved into that task's REU block instead of ours, so the
    //      art would be lost at the next hand-over. It also means kPtr is free
    //      for the filename below, because sprInstall clobbers it.
    //      The interior takes the INK and the ring a FIXED colour: the ring is the
    //      pointer's outline, so it cannot follow the ink -- 0 is a legal ink and the
    //      canvas's own background, and an outline drawn in it erases itself. The
    //      interior is the ink's readout with the band down, and the swatch's colour
    //      with it up.
    //      Both colours are set EXPLICITLY: an implicitly coloured sprite is
    //      drawn perfectly and is invisible ---
    sprColour(GP_SL_FILL, GP_INK_DEF)  // the interior: the ink, and its readout
    sprColour(GP_SL_OUT, GP_RING_COL)  // the ring: fixed, whatever the ink is
    sprPriority(0)               // in front of the picture: both of them

    //  --- the art: ONE file, TWO records. A sheet record is per SPRITE, so the
    //      two sprites each need their own kSpriteSheet call even though they show
    //      frames of the same file (kSpriteSheet preserves kPtr, so one pointer
    //      serves both) ---
    lda #<gpPointerFile
    sta kPtr
    lda #>gpPointerFile
    sta kPtr+1
    lda #GP_SL_FILL
    jsr kSpriteSheet
    bcs gpNoArt
    lda #GP_SL_OUT
    jsr kSpriteSheet
    bcs gpNoArt
    //  One frame each, loaded once: the pointer animates by position, and a
    //  frame rewrite mid-flight is the one thing here that can tear.
    lda #GP_SL_FILL
    ldx #GP_FR_FILL
    jsr kSpriteFrame
    bcs gpNoArt
    lda #GP_SL_OUT
    ldx #GP_FR_OUT
    jsr kSpriteFrame
    bcs gpNoArt
    sprOn(%00000011)             // both slots, once their art is in place:
                                 //   0 the fill, 1 the ring

    //  --- the pointer starts centred, and its first read reports a zero delta
    //      (the library primes on enable), so it starts exactly where it is put
    //      -- and where the CANVAS is, not where the shell's cursor was ---
    lda #MOUSE_ENABLE
    jsr kMouseCall               // start tracking
    ldy #GP_POS
    lda #<GP_CX
    sta (dataPtr),y
    iny
    lda #>GP_CX
    sta (dataPtr),y
    iny
    lda #<GP_CY
    sta (dataPtr),y
    iny
    lda #>GP_CY
    sta (dataPtr),y
    ldy #GP_FRAME
    lda frameCount
    sta (dataPtr),y              // the first frame must count as a change
    //  --- the frame tracker starts at rest ---
    //  No previous frame has risen above a minimum, and both offsets begin the
    //  frame at zero, so the first settle corrects nothing.
    ldy #GP_TRK_X
    lda #0
gpInitTrk:
    sta (dataPtr),y              // uPrev, sRun and sMin, for x and then for y
    iny
    cpy #GP_TRK_Y + 3
    bcc gpInitTrk
    //  --- and no stroke is in progress until the button says so ---
    ldy #GP_DRAW
    sta (dataPtr),y              // A is still the tracker loop's 0
    //  --- the picker starts down, and gpCanvas opens the band with gpPickOpen once
    //      the pointer is up -- so the interface is there from the first frame ---
    ldy #GP_MENU
    sta (dataPtr),y
    ldy #GP_TEXT
    sta (dataPtr),y              // A is still the tracker loop's 0: no pen is down yet
    ldy #GP_TWROTE
    sta (dataPtr),y              // ...and no text entry has written anything yet
    ldy #GP_U_DRAW
    sta (dataPtr),y              // ...and no unrecorded mark is on the canvas
    ldy #GP_MSEL
    lda #$ff
    sta (dataPtr),y
    //  --- the pencil is the tool a fresh canvas starts with, and every button counts
    //      as RELEASED until a frame says otherwise, so the first click is a click ---
    ldy #GP_TOOL
    lda #GP_TOOL_PEN
    sta (dataPtr),y
    ldy #GP_PREV
    lda #$ff
    sta (dataPtr),y
    ldy #GP_BTN                  // WARNING: the FIRST read of GP_BTN is the frame's
    sta (dataPtr),y              //   copy into GP_PREV, before gpSample has written it --
                                 //   and with KF_TAKES_ARGS the loader has left the
                                 //   picture's name bytes there. $ff is every button up.
    jmp gpPlace                  // and the pointer shown before any frame elapses

// --- gpNoArt -- the pointer's art did not resolve ---
//  The canvas exists and is on screen, so it goes first: the display back to the
//  shell (BEFORE the free -- a surfless exit blanks the display, edit's rule),
//  then the canvas is freed, and then the report takes a screen of its own like
//  every other failure.
gpNoArt:
    lda #SYS_GETTID
    jsr syscall
    tax
    lda taskParent,x
    tax
    jsr kSwitchView              // display and keyboard focus back to the shell
    jsr kFreeScreen              // ours, and not the displayed surface
    lda #<gpMsgNoArt
    ldx #>gpMsgNoArt
    jmp gpFail

// --- gpSample -- one pointer update, once per DISPLAYED frame ---
//  mouse.lib owns the last position and returns SIGNED deltas (-32..+31), so the
//  16-bit total is ours -- which is exactly what lets us clamp it to the canvas,
//  something the library cannot know about.
gpSample:
    ldy #GP_READS                // reads per frame
    sty gpFoldN
gpSampleLoop:
    jsr gpReadFold
    dec gpFoldN
    bne gpSampleLoop
    //  --- settle: take back what the frame's LAST sample overstated ---
    //  Every delta of the frame has gone onto the position, so the position now
    //  carries the noise on the last of them. gpSettle works out how much that is
    //  and writes it into kArg for gpAxis to fold in, left of the clamp.
    ldx #MOUSE_DELTA_X
    ldy #GP_TRK_X
    jsr gpSettle
    ldx #MOUSE_DELTA_Y
    ldy #GP_TRK_Y
    jsr gpSettle
    ldx #MOUSE_DELTA_X
    ldy #GP_POS
    jsr gpAxis
    ldx #MOUSE_DELTA_Y
    ldy #GP_POS + 2
    jsr gpAxis
    jmp gpClamp                  // clamp the moved position, then publish it

// --- gpReadFold -- one MOUSE_READ, folded into the position and the tracker ---
//  Called several times per frame rather than once: the 1351 reports movement
//  against a wrapping position, so the more often it is polled the smaller each
//  delta is, the less a fast flick can be mistaken for a shorter one, and the
//  more of the motion it is asked about while it is happening.
gpReadFold:
    lda #MOUSE_READ
    jsr kMouseCall
    bcc gpSampleLive             // C=1 = the library refused: nothing was written
    jmp gpSampleMiss
gpSampleLive:
    ldy #GP_BTN
    sta (dataPtr),y              // the buttons, ACTIVE LOW; PA4 is the left one
    //  WARNING: mouse.lib's result bytes live in kArg, which any later call
    //  clobbers, so both axes are handled before anything else runs.
    ldx #MOUSE_DELTA_X
    ldy #GP_TRK_X
    jsr gpTrack
    ldy #GP_POS
    jsr gpAxis
    //  The 1351's y axis runs opposite to the screen's, so the delta is negated
    //  before it is folded: pushing the mouse away moves the pointer up.
    lda kArg+MOUSE_DELTA_Y
    eor #$ff
    clc
    adc #1
    sta kArg+MOUSE_DELTA_Y
    ldx #MOUSE_DELTA_Y
    ldy #GP_TRK_Y
    jsr gpTrack
    ldy #GP_POS + 2
    jsr gpAxis
    rts                          // ...back to the per-frame read loop

//  The read loop's count, and gpSettle's scratch. Bytes rather than the stack:
//  the loop body re-enters the library, which parks its own bytes on the caller's
//  stack. gpTmp is touched only between a gpSettle's entry and its rts, which makes
//  no calls at all -- and grpaint runs ONE instance, so the code image is its own.
gpFoldN:   .byte 0
gpTmp:     .byte 0

//  The palette loop's selector. A byte rather than X: kSetGfxColor re-enters the
//  library and does `ldx curTask`, so X comes back as a task slot rather than the
//  selector -- and an `inx` on that lands on the same value every time, which is
//  a loop that never ends. Load-time only, and the task runs one instance.
gpPicIdx:  .byte 0

//  The text pen's characters: gpChr is the character the key in hand would write, and
//  gpPutC parks it while gpTextPut builds the position kGfxChar wants, since that call
//  wants it in X.
gpChr:     .byte 0
gpPutC:    .byte 0

//  What the entry's line holds, one byte per GLYPH: the character written there, which is
//  what DEL needs -- it takes a glyph back by writing the same one again, and only the
//  character knows which shape that was. gpTextIdx is the slot: the pen's x minus the
//  entry's starting x, over eight, which is how many glyphs along the line this one is. So
//  DEL steps back as often as the line has glyphs and every one of them is exact; a new
//  line overwrites the slots it reaches, and a wrap or a RETURN leaves the pen at the
//  line's first x, where there is nothing of its own to undo.
gpLine:    .fill 40, 0
gpIdx:     .byte 0

//  The matrix byte the text pen writes into every cell a glyph lands in: (ink << 4) | bg0,
//  built once per key by gpTextInk. The same byte holds that routine's row on the way in.
gpInk:     .byte 0

//  The low byte of the cell offset gpTextInk builds -- a task byte, not the kernel's shared
//  kArg block, which any other task's kernel call may overwrite under an interrupt.
gpOff:     .byte 0

// --- gpClamp -- keep the moved position on the canvas, then publish it ---
//  Reached by `jmp` from gpSample; the two bytes above are data, never executed.
gpClamp:
    ldy #GP_POS + 1               // the x axis's high byte
    ldx #0
    jsr gpClamp1
    ldy #GP_POS + 3               // ...and the y axis's
    ldx #1
    jsr gpClamp1
    //  fall through
gpPlace:
    //  The window position, computed ONCE for both sprites: sprite x = canvas
    //  x + 24 with its 9th bit kept separately, and sprite y = canvas y + 50,
    //  because the window's own first raster line is 50. It lands in the instance
    //  buffer rather than a register because kSpritePos clobbers all three.
    ldy #GP_POS + 2
    lda (dataPtr),y              // canvas y (the clamp keeps it under 200)
    clc
    adc #GP_WIN_Y
    ldy #GP_WY
    sta (dataPtr),y
    ldy #GP_POS
    lda (dataPtr),y              // canvas x LOW
    clc
    adc #GP_WIN_X                // + 24, C = the carry into the high byte
    ldy #GP_WX
    sta (dataPtr),y
    ldy #GP_POS + 1
    lda (dataPtr),y              // canvas x HIGH -- the half a low-byte-only add
    adc #0                       //   drops, which sends x >= 256 back to the left
    ldy #GP_XH                   //   edge. This byte IS the sprite's bit 8.
    sta (dataPtr),y
    ldx #GP_SL_FILL
    jsr gpPut
    ldx #GP_SL_OUT
    jmp gpPut                    // and the ring, at the same position

// --- gpPut -- place one sprite at the pointer's window position ---
//  In: X = the sprite. kSpritePos wants A = x lo, Y = y and the 9th bit in the
//  carry, and clobbers all three, so each sprite re-reads the position. X is
//  untouched in here, and the parked byte rides the task's own stack, which a
//  context switch cannot reach.
//  WARNING: three of the four arguments come from the SAME indexed read, so the
//  loads are ordered to survive each other -- the 9th bit FIRST (it is the only
//  one that needs a register other than A), then the y into Y, and the x low byte
//  LAST out of the stack, because `tay` and `pla` leave the carry alone and any
//  further `ldy #GP_*` would destroy the y again.
gpPut:
    ldy #GP_WX
    lda (dataPtr),y              // the window x low byte
    pha                          // park it: Y is needed for the other two
    //  Bit 8 of the sprite's x is bit 8 of the WINDOW x, which gpPlace stores as
    //  the high byte of canvas x + 24. `cmp #1` sets the carry for "A >= 1", and
    //  the loads after it (`ldy`, `lda`, `tay`, `pla`) are all carry-neutral.
    ldy #GP_XH
    lda (dataPtr),y
    cmp #1
    ldy #GP_WY
    lda (dataPtr),y
    tay                          // Y = the window y
    pla                          // A = x lo
    jmp kSpritePos               // tail call
gpSampleMiss:
    rts                          // the reader refused: leave the pointer where it is

// --- gpAxis -- fold one axis's delta into its 16-bit position ---
//  In: X = the kArg offset of the delta (MOUSE_DELTA_X/Y), Y = that axis's offset
//      in the instance buffer (GP_POS / GP_POS + 2).
//  The delta is SIGNED: bit 7 set means the value is one step BACK, so the high
//  byte takes the sign extension ($ff) plus the carry out of the low byte. That
//  keeps the whole thing register-only -- no scratch a preemption could clobber.
//  Called once per sample with the library's delta and once per frame with
//  gpSettle's correction, which is why that delta always arrives in kArg rather
//  than in a register.
gpAxis:
    lda kArg,x
    bmi gpAxisNeg
    clc
    adc (dataPtr),y              // the delta, plus the position's low byte
    sta (dataPtr),y
    iny
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y
    rts
gpAxisNeg:
    clc
    adc (dataPtr),y
    sta (dataPtr),y
    iny
    lda (dataPtr),y
    adc #$ff                     // the sign extension, carrying the low byte's carry
    sta (dataPtr),y
    rts

// --- gpTrack -- follow one axis's offset from the frame's first sample ---
//  In: X = the axis's kArg delta offset (MOUSE_DELTA_X/Y), Y = its GP_TRK_* base.
//  sRun is the movement since the frame began and sMin is its smallest value; the
//  difference between them is what the frame's noise correction is made of.
gpTrack:
    lda kArg,x                   // the signed delta
    iny                          // -> sRun
    clc
    adc (dataPtr),y
    sta (dataPtr),y              // sRun += delta
    iny                          // -> sMin
    sec
    sbc (dataPtr),y              // sRun - sMin: N is its sign unless V is set
    bvc gpTrackS
    eor #$80                     // ...and a signed compare turns N round on overflow
gpTrackS:
    bpl gpTrackRet               // not below the minimum: it stands
    dey                          // -> sRun
    lda (dataPtr),y
    iny                          // -> sMin
    sta (dataPtr),y              // sMin = sRun
gpTrackRet:
    rts

//  The most a frame's noise can come to: eight reads of + or - one each, either side
//  of the comparison, is what this filter is asked to remove. Anything larger is not
//  noise and not a correction -- see gpSettle.
.const GP_NOISE = 16

// --- gpSettle -- turn one axis's frame into the noise correction ---
//  In: X = the axis's kArg delta offset, Y = its GP_TRK_* base.
//  Out: kArg,x = the signed amount to add to the position, and the tracker reset
//  ready for the next frame.
//  uPrev - sRun + sMin is the correction. sRun - sMin is how far the frame's last
//  sample sat above the frame's smallest -- which is the noise that sample put on
//  the position -- and uPrev is the same figure for the frame before, which is
//  already subtracted and so has to come back. Steady movement makes the two equal
//  and the correction zero.
//  WARNING: THE TRACKER'S THREE BYTES ARE SIGNED AND A FRAME'S MOVEMENT IS NOT BOUNDED
//  BY ONE. Eight reads of a fast flick are up to +/-32 each, so sRun wraps past 127 --
//  and the minimum test, which reads a signed difference, then latches the WRAPPED value
//  as the frame's smallest. The correction that comes out is the previous frame's rise
//  replayed as movement: the pointer carries on after the mouse stops, further the
//  faster the flick was. A correction larger than a frame of noise can be is therefore
//  not a correction at all, and is taken as zero.
gpSettle:
    lda (dataPtr),y              // uPrev
    iny                          // -> sRun
    sec
    sbc (dataPtr),y
    iny                          // -> sMin
    clc
    adc (dataPtr),y              // A = the correction
    cmp #GP_NOISE + 1
    bcc gpSetSmall               // 0..+N: a correction, and it stands
    pha                          // ...otherwise test the magnitude, KEEPING the sign:
    eor #$ff                     //   the negate below is only a test, and PLA restores
    clc                          //   the value it was handed (and leaves the carry alone)
    adc #1
    cmp #GP_NOISE + 1
    pla
    bcc gpSetSmall               // -N..-1: a correction too
    lda #0                       // the scratch has wrapped: correct nothing
gpSetSmall:
    sta kArg,x
    lda (dataPtr),y              // sMin, before it is cleared
    sta gpTmp
    lda #0
    sta (dataPtr),y              // sMin = 0 for the frame coming
    dey                          // -> sRun
    lda (dataPtr),y
    sec
    sbc gpTmp                    // sRun - sMin: this frame's rise, for the next
    dey                          // -> uPrev
    sta (dataPtr),y
    lda #0
    iny
    sta (dataPtr),y              // sRun = 0 -- the next frame starts here
    iny
    sta (dataPtr),y              // ...and so does its minimum
    rts

// --- gpClamp1 -- keep one axis inside the canvas ---
//  In: Y = that axis's HIGH byte offset, X = its bound's index in gpBoundLo/Hi.
//  The bounds are the window's own (0..319 by 0..199), and they are what stops a
//  fast flick carrying the pointer off the screen with no way back. The position
//  is signed here -- a delta can be negative -- so a negative value clamps to 0
//  as well, and a value at or past the bound clamps to bound - 1.
gpClamp1:
    lda (dataPtr),y
    bmi gpClampZero              // negative: the whole value is below 0
    cmp gpBoundHi,x
    bcc gpClampOk                // below the bound's high byte
    bne gpClampMax               // above it
    dey                          // equal: the low byte decides
    lda (dataPtr),y
    cmp gpBoundLo,x
    iny
    bcc gpClampOk
gpClampMax:
    lda gpBoundLo,x
    sec
    sbc #1
    dey
    sta (dataPtr),y
    iny
    lda gpBoundHi,x
    sbc #0                       // the borrow rides into the high byte
    sta (dataPtr),y
    rts
gpClampZero:
    lda #0
    dey
    sta (dataPtr),y
    iny
    sta (dataPtr),y
gpClampOk:
    rts

// ==========================================
//  The stroke -- the left button paints
// ==========================================

// --- gpDrawUp -- the release ends whatever gesture the tool was making ---
//  The PENCIL: the stroke is over, and the movement up to the release is still part of
//  it, so the last segment is drawn before the flag drops.
//  The LINE: nothing. Its GP_DRAW is an anchor waiting for its second click, and
//  committing it here would draw from the anchor to wherever the pointer was when the
//  PREVIOUS click was released -- and leave the next click to set a new anchor instead.
//  The RECTANGLES: the release ends a DRAG -- the live preview comes off the canvas and
//  the box it was showing is drawn for real, or dropped when the press never moved.
//  WARNING: it sits BEFORE gpDraw deliberately. gpDraw's release test reaches it with
//  one short branch, and the shape routines sit between the two in the file.
gpDrawUp:
    ldy #GP_TOOL
    lda (dataPtr),y
    beq gpDrawUpPen              // the pencil is tool 0
    cmp #GP_TOOL_LINE
    beq gpDrawUpRts              // a line's anchor waits for its second click
    sec                          // the BRUSHES end a stroke exactly as the pencil does:
    sbc #GP_TOOL_BRUSH1          //   freehand is one gesture whichever mark it leaves
    cmp #3
    bcc gpDrawUpPen
    ldy #GP_DRAW                 // ...and only a RECTANGLE has a drag left to release
    lda (dataPtr),y
    beq gpDrawUpRts              // no drag is live
    jmp gpDgRelease
gpDrawUpRts:
    rts
gpDrawUpPen:
    ldy #GP_DRAW
    lda (dataPtr),y
    beq gpDrawUpRts              // no stroke was in progress
    lda #0
    sta (dataPtr),y
    jsr gpSeg                    // the movement up to the release is still a stroke
    jmp gpUndoPush               // ...and the whole stroke is one step of the history

// --- gpDraw -- turn this frame's movement into a stroke ---
//  The button byte is ACTIVE LOW and PA4 (the fire line) is the left button, so
//  "held" is that bit reading CLEAR.
//  A press is an EVENT, not a level: a click that begins and ends between two
//  displayed frames moves nothing of its own, so the press draws the pixel it
//  landed on and starts the stroke there. A release ends the stroke, and until the
//  next press a moved pointer draws nothing -- which is why GP_LAST is seeded on
//  the press: a new stroke has to start where the press happened, not run from
//  wherever the pointer was when the last one ended.
gpDraw:
    //  --- the band is up: the left button is the BAND's, and its press picks the entry
    //      under the pointer (gpPickTake). Nothing is drawn by that press -- a selection
    //      is not a stroke, and the band covers the canvas rows a stroke would land in
    //      -- and the release needs no handling either, because nothing can be live
    //      under the band: a pencil stroke keeps the band down, and a rectangle's drag
    //      is cancelled as the band goes up ---
    ldy #GP_MENU
    lda (dataPtr),y
    bne gpDrawRts
    ldy #GP_BTN
    lda (dataPtr),y
    and #GP_BTN_LEFT
    bne gpDrawUp                 // 1 = released
gpDrawGo:
    //  --- which tool the left button drives. The pencil paints on the LEVEL; every
    //      other tool is a shape that runs on the EDGE of a click, so the split is
    //      here, before either. The constant is compared, not assumed ---
    //      The BRUSHES are pencils: they paint on the level as well, and the only thing
    //      that differs is the mark gpDot leaves, so they belong on this side of the
    //      split rather than with the shapes ---
    ldy #GP_TOOL
    lda (dataPtr),y
    cmp #GP_TOOL_PEN
    beq gpDrawPencil
    cmp #GP_TOOL_TEXT
    beq gpTextPlace              // the text tool: a press places a pen, and paints nothing
    sec
    sbc #GP_TOOL_BRUSH1
    cmp #3
    bcc gpDrawPencil             // a brush: freehand, with a blob for its mark
    jmp gpShape
gpDrawRts:
    rts
gpDrawPencil:
    ldy #GP_DRAW
    lda (dataPtr),y
    beq gpDrawFresh              // nothing in progress: this press begins a stroke
    jmp gpSeg                    // painting already: draw this frame's segment
 gpDrawFresh:
    //  --- a stroke BEGINS on a press, never on the level. A level cannot tell a fresh
    //      press from one that has already been spent, and the frame after a selection
    //      would otherwise paint: the press that took the entry is still down, GP_DRAW
    //      is clear, so the pencil would read it as a new stroke and mark the canvas
    //      where the band has just been. gpPickAway clears the bit for exactly this ---
    ldy #GP_PREV
    lda (dataPtr),y
    and #GP_BTN_LEFT
    beq gpDrawRts                // the button was already down: not a press
    ldy #GP_DRAW
    lda #1
    sta (dataPtr),y
    jsr gpSeek
    jmp gpDot                    // the press marks exactly one pixel

// --- gpTextPlace -- the TEXT tool's press: put the pen down, and paint nothing ---
//  A press does not draw. It puts the pen where the pointer IS -- a PIXEL position, not a
//  cell: kGfxChar takes the glyph's top-left corner, so text can start anywhere on the
//  canvas. The one thing that depends on the video matrix is the COLOUR a glyph's pixels
//  take, since in hires a pixel's colour IS its cell's -- so text placed on an eight-pixel
//  boundary comes out in one colour, and text placed off one can straddle two cells and
//  show both, exactly as a stroke drawn across the same boundary does.
//  The pen is SHOWN: a full block (GP_CARET) XORed at it, which the next move takes back by
//  writing it again -- nothing is stored to undo it with, because XOR is its own inverse.
gpTextPlace:
    jsr gpTextEnd                // a pen already down is put away first: its caret is an
                                 //   XOR on the canvas, and moving the pen without taking
                                 //   it off leaves a block behind where the old one was
    ldy #GP_TWROTE
    lda #0
    sta (dataPtr),y              // the entry that starts here has written nothing yet
    ldy #GP_POS
    lda (dataPtr),y
    ldy #GP_PEN_X
    sta (dataPtr),y              // the pen's x, low...
    ldy #GP_PEN_X0
    sta (dataPtr),y              // ...which is also where a wrapped line comes back to
    ldy #GP_POS + 1
    lda (dataPtr),y
    ldy #GP_PEN_X + 1
    sta (dataPtr),y              // ...and its high byte
    ldy #GP_PEN_X0 + 1
    sta (dataPtr),y
    ldy #GP_POS + 2
    lda (dataPtr),y
    ldy #GP_PEN_Y
    sta (dataPtr),y              // ...and the y
    ldy #GP_TEXT
    lda #1
    sta (dataPtr),y              // the pen is down: every key is text from here
    ldx #GP_CARET
    jmp gpTextPut                // ...and it is shown, a block at a time

// --- gpTextPut -- one glyph at the pen, XORed. In: X = the character (a screen code) ---
//  A typed key IS a screen code, which is what kGfxChar takes, so nothing is converted.
//  WARNING: THE PIXEL OP IS KERNEL STATE, and the scheduler clears it on every context
//  switch -- so the op, the glyph and the clear have to be one uninterruptible run, or a
//  switch landing in the gap would write the glyph SET instead of XORed.
//  kGfxChar clobbers A, X, Y, kPtr and the whole kArg block, so the position is set up
//  immediately before the call and nothing of ours is kept there.
//  Clobbers A, X, Y, kPtr, kArg.
gpTextPut:
    php
    sei
    stx gpPutC                   // the character, out of the way of the loads below
    lda #1
    sta GP_GFXOP                 // 1 = XOR: the glyph inverts what is under it
    ldy #GP_PEN_X
    lda (dataPtr),y
    tax                          // X = the pen's screen x
    iny
    lda (dataPtr),y              // A = its high byte
    jsr gpHalfX                  // A = the pen's canvas x, C = its 9th bit
    sta kArg                     // x lo
    lda #0
    rol
    sta kArg+1                   // x hi
    ldy #GP_PEN_Y
    lda (dataPtr),y
    sta kArg+2                   // y
    jsr gpColour                 // in MC the glyph's pixels carry the pen's VALUE
    ldx gpPutC                   // the character
    jsr kGfxChar
    lda #0
    sta GP_GFXOP                 // ordinary drawing again, whatever the glyph did
    plp
    rts

// --- gpTextIdx -- the pen's slot in this line's history, in A ---
//  (x - the entry's starting x) / the glyph's pitch: a glyph advances the pen by the
//  pitch, so the slot is which glyph of the line this one is -- and it is what DEL reads
//  to know which CHARACTER to write again. The difference is 16-bit and a whole number of
//  pitches, so its low byte shifts down for the slot's low bits while the high byte -- 0 or
//  1, since x is under 320 -- supplies the bit above them: eight in hires (forty slots to
//  a line), sixteen in MC (twenty).
//  Clobbers A, X, Y, gpTmp, gpIdx, and reads the pen.
gpTextIdx:
    ldy #GP_PEN_X
    lda (dataPtr),y
    ldy #GP_PEN_X0
    sec
    sbc (dataPtr),y
    sta gpTmp
    ldy #GP_PEN_X + 1
    lda (dataPtr),y
    ldy #GP_PEN_X0 + 1
    sbc (dataPtr),y              // the borrow rides in, as it must
    sta gpIdx                    // the difference's high byte, 0 or 1
    ldy #GP_MODE
    lda (dataPtr),y
    cmp #GP_MODE_MC
    beq gpTextIdxMc
    lda gpIdx
    asl
    asl
    asl
    asl
    asl                          // hi x32: the slot's bit 5 and up
    sta gpIdx
    lda gpTmp
    lsr
    lsr
    lsr                          // ...and lo / 8
    ora gpIdx
    rts
gpTextIdxMc:
    lda gpIdx
    asl
    asl
    asl
    asl                          // hi x16: the slot's bit 4 and up
    sta gpIdx
    lda gpTmp
    lsr
    lsr
    lsr
    lsr                          // ...and lo / 16
    ora gpIdx
    rts

// --- gpTextAdv -- the pen one glyph on, wrapping to the entry's first x ---
//  One glyph's pitch is the font's own screen step, so a line is a run of glyphs with
//  nothing between them. Past the right edge -- the last x a whole glyph still starts at is
// 320 - the pitch -- the pen goes back to the x the entry started at and one glyph row
//  down; on the last row it stops, because there is nowhere further to write.
gpTextAdv:
    jsr gpGlyphW
    sta gpTmp                    // the pitch, parked: Y has to point at the pen below
    ldy #GP_PEN_X
    lda (dataPtr),y
    clc
    adc gpTmp
    sta (dataPtr),y
    iny
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y              // the pen one glyph on, in 16 bits
    //  --- and the right edge: 321 - the pitch is the first x past the last whole glyph ---
    lda #<321
    sec
    sbc gpTmp                    // 313 in hires, 305 in MC
    sta gpIdx
    lda #>321
    sbc #0
    sta gpOff                    // ...the edge, in 16 bits
    ldy #GP_PEN_X
    lda (dataPtr),y
    cmp gpIdx
    ldy #GP_PEN_X + 1
    lda (dataPtr),y
    sbc gpOff
    bcc gpTextAdvRts             // x < the edge: inside, so the pen stays where it is
gpTextAdvWrap:
    ldy #GP_PEN_Y
    lda (dataPtr),y
    clc
    adc gpTmp                    // ...one glyph row down, when a row fits
    cmp #201                     // 200 is the canvas's height
    bcs gpTextAdvRts             // the pen stays at the edge: the next glyph there is
                                 //   clipped, which is all there is left to do
    sta (dataPtr),y
    ldy #GP_PEN_X0
    lda (dataPtr),y
    ldy #GP_PEN_X
    sta (dataPtr),y
    ldy #GP_PEN_X0 + 1
    lda (dataPtr),y
    ldy #GP_PEN_X + 1
    sta (dataPtr),y              // ...back to the x the entry started at
gpTextAdvRts:
    rts

// --- gpCellOff -- kPtr = the video-matrix cell a screen position is in ---
//  In: X = the instance-buffer offset of the position's x LOW byte -- its high byte is at
//  +1 and its row at +2. Out: kPtr = the cell's byte in the video matrix, gpTmp = its
//  column, 0-39.
//  A cell is eight SCREEN pixels in both modes -- in MC a canvas pixel is two screen
//  pixels across, so a cell holds four of them -- which is what lets one routine serve
//  the text pen and a drawing mark alike. The column is x >> 3, whose bit 5 is x's own bit
//  8 (x is under 320). The row's contribution is row*40: row*8 plus (row*32), whose low
//  byte comes from the row itself and whose bit 8 -- the one a left shift would drop -- is
//  row>>3, added to the high byte with the carry.
//  Clobbers A, X, Y, kPtr, gpTmp, gpInk, gpOff.
gpCellOff:
    stx gpOff                    // the x field's offset, held for the three reads
    inx                          // ...whose high byte is the next one
    txa
    tay
    lda (dataPtr),y
    asl
    asl
    asl
    asl
    asl
    sta gpTmp
    ldy gpOff
    lda (dataPtr),y
    lsr
    lsr
    lsr
    ora gpTmp
    sta gpTmp                    // the column, 0-39
    ldy gpOff
    iny
    iny
    lda (dataPtr),y              // the row, 0-24
    lsr
    lsr
    lsr
    sta gpInk                    // held until the offset is built
    asl
    asl
    asl                          // row*8
    sta gpOff                    // the offset low, to start
    lda gpInk
    asl
    asl
    asl
    asl
    asl                          // (row*32) & $FF
    clc
    adc gpOff                    // ...+ row*8 = row*40, low
    sta gpOff
    lda #0
    adc #0                       // ...and the bit 8 it carried
    sta kPtr+1                   // the high byte so far
    lda gpInk
    lsr
    lsr
    lsr                          // (row*32) >> 8
    clc
    adc kPtr+1                   // + the carried bit = row*40, high
    sta kPtr+1
    lda gpOff
    clc
    adc gpTmp                    // + the column: the cell's offset in the matrix, low
    sta kPtr
    lda kPtr+1                   // (the lda leaves C, the column's carry, alone)
    adc #$04                     // ...and the matrix's own page
    sta kPtr+1
    rts

// --- gpTextInk -- colour the cells the glyph will land in, to the ink ---
//  In hires a pixel's colour IS its cell's nibble, and kGfxChar plots through gxPix, which
//  writes pixels only -- so a glyph's colour would otherwise be whatever the cells already
//  carried. THIS IS WHERE THE INK GOES: the same (ink << 4) | paper the pencil's kPlot
//  leaves in every cell it enters (kPlot keeps the low nibble and ORs the ink over it).
//  The XOR glyph then inverts those cells' own pixels, so the character shows in the chosen
//  colour -- and over a cell that is already the ink, which is most of a drawn canvas,
//  nothing changes at all.
//  The box a glyph inks is its SCREEN footprint, measured in cells: one across and one
//  down in hires, two of each in MC (the pitch), plus one more each way when the pen is OFF
//  an eight-pixel boundary -- the pixels past that boundary land in the next cell -- which
//  is why each is counted rather than assumed. The matrix is forty bytes a row, so the box
//  is a run of 40-based offsets from kPtr and no per-cell arithmetic is needed. It is
//  CLAMPED at the matrix's own right edge and last row: a pen placed hard against either
//  would otherwise colour cells of the row below.
//  Clobbers A, X, Y, kPtr, gpTmp, gpInk, gpOff, gpIdx. Nothing of the caller's is live
//  here: gpTextIdx writes its own gpTmp and gpIdx after this runs.
gpTextInk:
    ldx #GP_PEN_X
    jsr gpCellOff                // kPtr = the pen's cell, gpTmp = its column
    //  --- the cell byte every cell written below takes. It follows the MODE, because the
    //      pen's pixels carry the INK in hires but the VALUE in MC: the ink goes into the
    //      high nibble there, while in MC the byte is the palette the values read --
    //      (c1 << 4) | c2, exactly what gpCells lays a canvas out with. Colour RAM is not
    //      touched here: it colours value 3 for the whole canvas, and gpCells has already
    //      put c3 there, so a byte written in front of a glyph would only fight it ---
    ldy #GP_MODE
    lda (dataPtr),y
    cmp #GP_MODE_MC
    beq gpTextInkMc
    ldy #GP_INK
    lda (dataPtr),y
    asl
    asl
    asl
    asl                          // the ink into the high nibble, and the PAPER below it:
    ldy #GP_MB                   //   a hires cell carries both, and the box a glyph inks
    ora (dataPtr),y              //   must keep the paper it lands on
    sta gpInk
    jmp gpTextInkCell
gpTextInkMc:
    ldy #GP_MB+2
    lda (dataPtr),y              // c2
    sta gpInk
    ldy #GP_MB+1
    lda (dataPtr),y              // c1
    asl
    asl
    asl
    asl
    ora gpInk
    sta gpInk                    // (c1 << 4) | c2
gpTextInkCell:
    //  --- the cells across: the pitch / 8, which is one in hires and two in MC, plus one
    //      when the pen is off an eight-pixel boundary. gpTmp still holds the pen's cell
    //      column, 0-39, built above ---
    jsr gpGlyphW
    lsr
    lsr
    lsr                          // pitch / 8
    sta gpIdx                    // the cells across
    ldy #GP_PEN_X
    lda (dataPtr),y
    and #$07
    beq gpTextInkClampX
    inc gpIdx                    // off the boundary: one cell further across
gpTextInkClampX:
    lda gpTmp                    // the pen's cell column, 0-39, built above
    clc
    adc gpIdx
    cmp #41
    bcc gpTextInkRows            // the box ends inside the last column
    lda #40
    sec
    sbc gpTmp                    // ...or at column 39 (never fewer than one cell: the
    sta gpIdx                    //   pen's own column is at most 39)
gpTextInkRows:
    //  --- and the cells down, the same way, clamped at the matrix's last row ---
    jsr gpGlyphW
    lsr
    lsr
    lsr                          // pitch / 8
    sta gpTmp                    // the cells down
    ldy #GP_PEN_Y
    lda (dataPtr),y
    and #$07
    beq gpTextInkClampY
    inc gpTmp
gpTextInkClampY:
    ldy #GP_PEN_Y
    lda (dataPtr),y
    lsr
    lsr
    lsr                          // the pen's cell row, 0-24
    sta gpOff
    lda #25
    sec
    sbc gpOff                    // the rows left, the pen's own included (never 0)
    cmp gpTmp
    bcs gpTextInkGo
    sta gpTmp
gpTextInkGo:
    //  --- the box: gpIdx cells across, gpTmp rows down, forty bytes a row ---
    ldy #0                       // kPtr is the box's first cell
gpTextInkRowLp:
    ldx gpIdx
    lda gpInk
gpTextInkColLp:
    sta (kPtr),y
    iny
    dex
    bne gpTextInkColLp
    tya
    clc
    adc #40
    sec
    sbc gpIdx                    // the next row's first cell: +40, less the cells across
    tay
    dec gpTmp
    bne gpTextInkRowLp
    rts

// --- gpTextKey -- one key for the pen. In: GP_CHR = what kReadKey returned ---
//  A PRINTABLE key is a screen code, and "printable" is not a range from $20: the letters
//  are $01-$1A (and $41-$5A with SHIFT held), so a `>= $20` test would silently drop
//  every unshifted letter. $00 is the '@' key, shared with the F-keys, and $1B-$1F are the
//  cursor codes. The two controls are the special keys: RETURN starts a line, DEL takes
//  the last glyph back.
gpTextKey:
    lda gpChr
    cmp #$81
    beq gpTextNl
    cmp #$80
    beq gpTextDel
    cmp #$20
    bcs gpTextKeyGo              // $20-$7F: the font's printable range
    cmp #$01
    bcc gpTextKeyRts             // $00: not a character
    cmp #$1B
    bcs gpTextKeyRts             // $1B-$1F: the cursor codes
gpTextKeyGo:
    ldy #GP_DIRTY                // kGfxChar writes no pixel through gpDot, so the flag
    lda #1                       //   that says "there is something to save" is set here
    sta (dataPtr),y
    ldy #GP_TWROTE
    sta (dataPtr),y              // ...and this entry has put a glyph on the canvas
    jsr gpTextInk                // ...and the ink goes into the CELLS first: a glyph's
                                 //   colour is its cell's nibble, and gxPix writes pixels
                                 //   only, so this is the only place the colour can enter
    ldx #GP_CARET                // the caret off the cell this glyph is about to take
    jsr gpTextPut
    ldx gpChr                    // ...and the glyph itself
    jsr gpTextPut
    //  --- and remember it at this position's slot, which is what DEL reads to write the
    //      same glyph a second time ---
    jsr gpTextIdx
    tax
    lda gpChr
    sta gpLine,x
    jsr gpTextAdv
    ldx #GP_CARET                // the caret back on, one glyph along
    jmp gpTextPut
gpTextKeyRts:
    rts

// --- gpTextNl -- RETURN: the pen to the start of the line, one row down ---
gpTextNl:
    ldx #GP_CARET
    jsr gpTextPut                // the caret off the line it was on
    jsr gpGlyphW
    sta gpTmp
    ldy #GP_PEN_Y
    lda (dataPtr),y
    clc
    adc gpTmp                    // ...one glyph row down, when a row fits
    cmp #201                     // 200 is the canvas's height
    bcs gpTextKeyRts             // the last row: no line below to start
    sta (dataPtr),y
    ldy #GP_PEN_X0
    lda (dataPtr),y
    ldy #GP_PEN_X
    sta (dataPtr),y
    ldy #GP_PEN_X0 + 1
    lda (dataPtr),y
    ldy #GP_PEN_X + 1
    sta (dataPtr),y              // ...back to the x the entry started at
    ldx #GP_CARET
    jmp gpTextPut                // the caret on the new line

// --- gpTextDel -- DEL: take the last glyph back ---
//  Exact, because XOR is its own inverse: the same glyph again restores the pixels the
//  first one toggled -- and gxPix never writes the matrix, so no cell's colour moved.
//  WHICH glyph is the line's history (gpLine), not the last key: the pen is one cell past
//  what it is taking back, and a second DEL must take back the one before that. Within the
//  entry's own line -- the column says whether anything has been typed on this one.
gpTextDel:
    ldy #GP_DIRTY                // an erase changes the canvas as much as a glyph does,
    lda #1                       //   and kGfxChar goes nowhere near gpDot
    sta (dataPtr),y
    ldy #GP_PEN_X + 1
    lda (dataPtr),y
    ldy #GP_PEN_X0 + 1
    cmp (dataPtr),y
    bne gpTextDelGo
    ldy #GP_PEN_X
    lda (dataPtr),y
    ldy #GP_PEN_X0
    cmp (dataPtr),y
    beq gpTextKeyRts             // the entry's own starting x: nothing to take back yet
gpTextDelGo:
    ldy #GP_TWROTE
    lda #1
    sta (dataPtr),y              // an erase changes the canvas as much as a glyph does
    ldx #GP_CARET
    jsr gpTextPut                // the caret off
    jsr gpGlyphW
    sta gpTmp                    // the pitch: gpTextIdx's own scratch is written below
    ldy #GP_PEN_X
    lda (dataPtr),y
    sec
    sbc gpTmp
    sta (dataPtr),y
    iny
    lda (dataPtr),y
    sbc #0
    sta (dataPtr),y              // ...the pen one glyph back
    jsr gpTextIdx
    tax
    lda gpLine,x                 // ...the character written there...
    tax
    jsr gpTextPut                // ...written again, which takes it back exactly
    ldx #GP_CARET
    jmp gpTextPut                // ...and the caret on where the next key lands

// --- gpTextEnd -- the pen put away: the caret comes off the canvas ---
//  Reached when the band opens, which is where the keyboard goes back to being the
//  program's: 'q' quits again, and a tool or a colour can be taken.
gpTextEnd:
    ldy #GP_TEXT
    lda (dataPtr),y
    beq gpTextEndRts
    ldx #GP_CARET
    jsr gpTextPut                // the cell goes back, byte for byte
    ldy #GP_TEXT
    lda #0
    sta (dataPtr),y
    //  --- an entry that wrote something is one step of the history ---
    //  AFTER the caret is off, and that order is the point: the caret is an XOR on the
    //  canvas, so a snapshot that caught it would paint a block into the picture every
    //  time that state came back.
    ldy #GP_TWROTE
    lda (dataPtr),y
    beq gpTextEndRts             // nothing was written: there is no step to leave
    jmp gpUndoPush
gpTextEndRts:
    rts

// --- gpShape -- the shape tools' gestures: two of them, by tool ---
//  The LINE is a pair of CLICKS: a click anchors, the next one draws, and the end it
//  drew to anchors the next, so a run of lines is one click each. What it leaves behind
//  is its own business -- the anchor it consumed is replaced by the end it drew to.
//  The RECTANGLES are a PRESS-DRAG-RELEASE: the press anchors, the drag walks a live
//  XOR preview of the outline, and the release draws the box -- or drops it when the
//  press never moved, which is what stops a stray click leaving a shape behind.
//  XOR is why the preview is affordable: the kernel's pixel writer toggles the pixel
//  the call names instead of setting it, so the same kBox over the same rectangle takes
//  the band off again -- no copy of the canvas, and no second way to draw in here.
//  A LIVE DRAG IS DRIVEN BY THE LEVEL, the press that starts it by the EDGE: the drag has
//  to run on every frame the button is held, and a level cannot tell a press from a button
//  that was already down. GP_DRAW says which of the two is live.
gpShape:
    ldy #GP_TOOL
    lda (dataPtr),y
    cmp #GP_TOOL_LINE
    beq gpShapeClick
    cmp #GP_TOOL_FLOOD
    beq gpShapeFlood
    //  --- a rectangle ---
    ldy #GP_DRAW
    lda (dataPtr),y
    beq gpShapePress             // nothing live: only a press can follow
    jmp gpDgFrame                // a drag is live: this frame's preview


// --- gpShapeFlood -- the flood fill: one press, one library call ---
//  The fill is not a gesture: the region is decided by the pixels, so there is nothing
//  to drag, nothing to preview and nothing to commit later. The press EDGE is all of it
//  -- the same test the line's first click makes, and for the same reason, since a level
//  cannot tell a press from a button already held. kFloodFill takes A = the colour,
//  kArg+0/1 = seed X (lo/hi) and kArg+2 = seed Y, and it reads the surface itself:
//  the pixel the seed lands on is what decides which region is filled.
//  WARNING: it holds SEI for the whole fill, so a large region stops the timer IRQ for
//  as long as it runs.
gpShapeFlood:
    //  --- and the UI must have been AWAY when the press landed. The fill decides its
    //      region from the CANVAS's own pixels, and the band covers the canvas's bottom
    //      rows while it is up -- so a press made with the UI up is the press that puts
    //      it away, and filling through it would read the palette where the picture is.
    //      GP_MENU0 is the band's state as the frame began, which is the state the press
    //      arrived in: the dismissal draws nothing, and the NEXT press fills ---
    ldy #GP_MENU0
    lda (dataPtr),y
    bne gpShapeFloodNo           // the band was up: this press is the dismissal
    ldy #GP_PREV
    lda (dataPtr),y
    and #GP_BTN_LEFT
    bne gpShapeFloodGo           // the bit was SET, so the button came up: a press
gpShapeFloodNo:
    jmp gpShapeRts               // ...already down, or a dismissal, so not one
                                 //   (gpShapeRts is out of branch range from here)
gpShapeFloodGo:
    ldy #GP_DIRTY                // kFloodFill is the one tool that writes no pixel
    lda #1                       //   through gpDot, so it sets the flag itself
    sta (dataPtr),y
    jsr gpSeek                   // GP_LAST = where the pointer is
    ldy #GP_LAST
    lda (dataPtr),y
    tax                          // X = the seed's screen x
    ldy #GP_LAST + 1
    lda (dataPtr),y
    jsr gpHalfX                  // A = the seed's canvas x, C = its 9th bit
    sta kArg                     // seed X low
    lda #0
    rol                          // the 9th bit, as a byte
    sta kArg+1
    ldy #GP_LAST + 2
    lda (dataPtr),y
    sta kArg+2                   // seed Y -- a row is the same in both modes
    jsr gpColour                 // A = what the fill writes (the ink, or GP_VAL)
    jsr kFloodFill
    jmp gpUndoPush               // the fill is one step: one press, one call
gpShapePress:
    ldy #GP_PREV
    lda (dataPtr),y
    and #GP_BTN_LEFT
    beq gpShapeRts               // was already down: not a press
    jmp gpDgStart
gpShapeClick:
    //  --- a CLICK is this frame's press: gpDraw only sends us here with the button
    //      DOWN, so what is left to test is the previous frame, and that test is the
    //      whole click. A level cannot tell the second click of a pair from the first
    //      one still being held, which is why GP_PREV exists ---
    ldy #GP_PREV
    lda (dataPtr),y
    and #GP_BTN_LEFT
    beq gpShapeRts               // was already down: not a fresh one
    ldy #GP_DRAW
    lda (dataPtr),y
    bne gpShapeLine              // the anchor is waiting: this click draws
    //  --- the first click of a run: the anchor lands where it did ---
    lda #1
    sta (dataPtr),y
    jsr gpSeek                   // GP_LAST = where the pointer is
    ldy #GP_LAST
    lda (dataPtr),y
    ldy #GP_ANCH
    sta (dataPtr),y
    ldy #GP_LAST + 1
    lda (dataPtr),y
    ldy #GP_ANCH + 1
    sta (dataPtr),y
    ldy #GP_LAST + 2
    lda (dataPtr),y
    ldy #GP_ANCH + 2
    sta (dataPtr),y
    jmp gpDot                    // the click marks exactly one pixel, and that is all
                                 //   it is: the LINE is the step, so the mark is left for
                                 //   the second click's push to record -- and a step asked
                                 //   for before then takes the mark off (gpDropMark)
gpShapeLine:
    //  --- the line: the anchor becomes the walk's start and the pointer its end.
    //      gpSeg is the walk the pencil uses too, so there is one Bresenham here ---
    ldy #GP_ANCH
    lda (dataPtr),y
    ldy #GP_LAST
    sta (dataPtr),y
    ldy #GP_ANCH + 1
    lda (dataPtr),y
    ldy #GP_LAST + 1
    sta (dataPtr),y
    ldy #GP_ANCH + 2
    lda (dataPtr),y
    ldy #GP_LAST + 2
    sta (dataPtr),y
    jsr gpSeg
    jsr gpUndoPush               // the line is one step, from its anchor to here
 gpShapeAnchor:
    //  --- and where the line ended becomes the next line's anchor, so a run of lines is
    //      one click each rather than a fresh pair every time. GP_DRAW stays SET ---
    ldy #GP_POS
    lda (dataPtr),y
    ldy #GP_ANCH
    sta (dataPtr),y
    ldy #GP_POS + 1
    lda (dataPtr),y
    ldy #GP_ANCH + 1
    sta (dataPtr),y
    ldy #GP_POS + 2
    lda (dataPtr),y
    ldy #GP_ANCH + 2
    sta (dataPtr),y
gpShapeRts:
    rts

// ==========================================
//  The rectangle drag -- press, live outline, release
// ==========================================

// --- gpDgCancel -- take a live band off the canvas and drop the drag ---
//  The right button's gesture opens the palette over the canvas, and the band's own rows
//  are part of the picture the palette saves and puts back -- so a band still up when the
//  palette goes up would be saved with that picture and returned with it. GP_DRAW_DRAG is
//  what keeps this off the pencil's stroke and off the line's waiting anchor: both hold 1
//  in GP_DRAW, and neither is a live preview that this may end.
gpDgCancel:
    ldy #GP_DRAW
    lda (dataPtr),y
    cmp #GP_DRAW_DRAG
    bne gpDgCancelRts            // the pencil's stroke, or the line's waiting anchor
    lda #0
    sta (dataPtr),y
    jmp gpDgBand                 // the band off -- GP_WK_* still describes it
gpDgCancelRts:
    rts

// --- gpDgOurs -- Z=1 while OUR surface is the one on screen ---
//  The band is written with the pixel op set to XOR, so the write lands in whichever
//  surface the kernel is drawing into: the framebuffer while ours is displayed, our REU
//  slot while it is not. A band drawn into a hidden surface is invisible and would only
//  come off on a later frame, so the drag does not run at all until we are on screen --
//  the same test the palette band makes, for the same reason, and one comparison per
//  frame rather than one per pixel.
//  WARNING: rts leaves the flags alone, so the caller's bne is testing the cmp in here.
gpDgOurs:
    lda kbFocus
    cmp curTask
    rts

// --- gpDgStart -- the press: the anchor, then the first preview ---
//  A drag only begins while our surface is displayed. Started on a screen nobody is looking
//  at, it would put a band into it that no eye can see, and only a later frame -- this one --
//  would ever take it off again. A press while another screen is up is simply ignored.
gpDgStart:
    jsr gpDgOurs
    bne gpDgStartRts
    ldy #GP_POS
    lda (dataPtr),y
    ldy #GP_ANCH
    sta (dataPtr),y
    ldy #GP_POS + 1
    lda (dataPtr),y
    ldy #GP_ANCH + 1
    sta (dataPtr),y
    ldy #GP_POS + 2
    lda (dataPtr),y
    ldy #GP_ANCH + 2
    sta (dataPtr),y
    ldy #GP_DRAW
    lda #GP_DRAW_DRAG
    sta (dataPtr),y              // the drag is live
    jsr gpDgSort                 // this gesture's geometry...
    jmp gpDgBand                 // ...and the one-point band the press shows itself with
gpDgStartRts:
    rts

// --- gpDgFrame -- a drag frame: the band off, then this frame's on ---
//  What comes off is the box GP_WK_* describes -- the one the LAST frame drew -- and what
//  goes on is the anchor against the pointer as it is now.
//  HIDDEN SURFACE: while another task's screen is displayed the framebuffer is not ours, so
//  nothing is drawn and nothing is erased; the drag stays live and GP_WK_* still describes
//  the band waiting in our own surface. The first frame we are visible again puts that band
//  off, so an F-key press in the middle of a drag leaves no band in the picture.
gpDgFrame:
    jsr gpDgOurs
    bne gpDgFrameRts
    ldy #GP_BTN
    lda (dataPtr),y
    and #GP_BTN_LEFT             // WARNING: the buttons are ACTIVE LOW, so a
    bne gpDgRelease              //   SET bit is the button UP -- `beq` here would
                                 //   end every drag on its first held frame
    jsr gpDgBand                // the previous band: off...
    jsr gpDgSort                // ...this frame's geometry...
    jmp gpDgBand                 // ...and on
gpDgFrameRts:
    rts

// --- gpDgRelease -- the release: the band off, then the box or nothing ---
//  A press that never moved is NOT a shape: a bare click would otherwise leave a one-point
//  rectangle behind on the canvas -- and a cell's colour with it -- so the release compares
//  the pointer against the anchor and drops that case. The comparison is the whole position:
//  x in two bytes and the row in a third.
//  WARNING: the release is only acted on while our surface is displayed, the rule the preview
//  follows too, so a release landing while another screen is up is handled on the first frame
//  we are visible again -- by which time the pointer says where the box goes.
gpDgRelease:
    jsr gpDgOurs
    bne gpDgReleaseRts
    jsr gpDgBand                 // the band off
    ldy #GP_DRAW
    lda #0
    sta (dataPtr),y              // the drag is over either way
    ldy #GP_POS
    lda (dataPtr),y
    ldy #GP_ANCH
    cmp (dataPtr),y
    bne gpDgCommit
    ldy #GP_POS + 1
    lda (dataPtr),y
    ldy #GP_ANCH + 1
    cmp (dataPtr),y
    bne gpDgCommit
    ldy #GP_POS + 2
    lda (dataPtr),y
    ldy #GP_ANCH + 2
    cmp (dataPtr),y
    bne gpDgCommit
gpDgReleaseRts:
    rts
gpDgCommit:
    ldy #GP_TOOL
    lda (dataPtr),y
    cmp #GP_TOOL_CIRCLE
    bne gpDgCommitBox
    jsr gpCircleDraw
    jmp gpUndoPush               // the circle is one step, its cells included
gpDgCommitBox:
    jsr gpBox                    // the corners sorted again, then the real rectangle
    jmp gpUndoPush               // ...and the rectangle is another one

// --- gpCircleDraw -- the circle, in the ink, from the anchor and the pointer ---
//  The pixels are the library's: kCircle walks the rim and clips it, so nothing here
//  can write where it should not. The COLOUR is not, yet -- see the WARNING below.
//  The radius is sorted afresh, exactly as gpBox sorts the corners before it draws: the
//  band that came off a moment ago was the LAST frame's, and this circle is where the
//  pointer is now.
//  WARNING: this draws PIXELS ONLY. In hires a cell's colour is its matrix nibble and
//  only kPlot writes that, so a circle drawn over cells holding another colour takes
//  that colour. Every cell of a fresh canvas is at the ink, so a circle on new work
//  comes out right; the cell walk that colours the rest is not written yet.
gpCircleDraw:
    jsr gpCircleSort
    jsr gpCircleArgs
    jsr gpColour                 // A = what a mark claims (the ink, or GP_VAL)
    jsr gpCircCall               // the same call the live outline makes
    jmp gpCircleCells            // ...and colour every cell the rim entered

// --- gpCircleCells -- the cell walk for the rim that was just drawn ---
//  kCircle draws the rim's PIXELS and writes no cell byte. In hires a cell's colour IS
//  that byte, so a circle drawn over artwork takes the colour the cells already carry --
//  which is why the box has gpBoxWalk. The walk runs per CELL rather than per pixel: a
//  radius-100 rim is some 570 pixels, and a cell's byte only changes when the cell does.
//  An arc advances x by 1 a step, so the columns it visits are every value between the
//  first and the last in a row, and this visits exactly those. That is also why the walk
//  is the SAME midpoint recursion gxCircle runs: the cells belong to the rim that was
//  just drawn, not to a second guess at the geometry.
//  BOTH halves are walked and BOTH arcs of each: the reflection of a pixel is not the
//  reflection of its cell, so only the left/right mirror is free -- and that one is taken
//  from the mirrored PIXEL, 2*CX - PX, never from a mirrored column.
//  gpCellMark decides what a cell costs: in hires the plot that writes its nibble, in MC
//  the claim that gives it the pen's colour -- and in MC NO pixel, because the rim's
//  pixels are the library's and a second rasterisation of a curve leaves stray dots.
//  WARNING: every GP_CC_* byte is reached with (dataPtr),y. A GP_ constant is an offset
//  into the instance buffer and never an address.
gpCircleCells:
    ldy #GP_ANCH                 // the centre and the radius, into scratch
    lda (dataPtr),y
    ldy #GP_CC_CX
    sta (dataPtr),y
    ldy #GP_ANCH + 1
    lda (dataPtr),y
    ldy #GP_CC_CX + 1
    sta (dataPtr),y
    ldy #GP_ANCH + 2
    lda (dataPtr),y
    ldy #GP_CC_CY
    sta (dataPtr),y
    ldy #GP_WK_RAD
    lda (dataPtr),y
    ldy #GP_CC_R
    sta (dataPtr),y
    bne gpCcHalves               // R = 0: the rim is the centre pixel's own cell
    ldy #GP_CC_CX
    lda (dataPtr),y
    ldy #GP_LAST
    sta (dataPtr),y
    ldy #GP_CC_CX + 1
    lda (dataPtr),y
    ldy #GP_LAST + 1
    sta (dataPtr),y
    ldy #GP_CC_CY
    lda (dataPtr),y
    ldy #GP_LAST + 2
    sta (dataPtr),y
    jmp gpDot
gpCcHalves:
    lda #$ff
    ldy #GP_CC_INV
    sta (dataPtr),y              // the top half first: its rows are CY - v
    lda #1
    ldy #GP_CC_CIN
    sta (dataPtr),y              // ...which is CY + (v ^ $ff) + 1
    lda #0
    ldy #GP_CC_ARC
    sta (dataPtr),y
gpCcHalf:
    jsr gpCcInit                 // x = 0, y = R, d = 3 - 2R
    lda #$ff
    ldy #GP_CC_ROW
    sta (dataPtr),y              // no row plotted yet on this arc
    ldy #GP_CC_RCOL
    sta (dataPtr),y
    ldy #GP_CC_LCOL
    sta (dataPtr),y
gpCcStep:
    ldy #GP_CC_Y                 // the quadrant runs while X <= Y, which is gxCircle's
    lda (dataPtr),y              //   test the SAME WAY ROUND: Y >= X, never X >= Y --
    ldy #GP_CC_X                 //   A = x against y with bcs ends the walk on its first
    cmp (dataPtr),y              //   step, at x = 0 and y = R
    bcs gpCcInRange
    jmp gpCcArcDone
gpCcInRange:
    jsr gpCcPoint
    jsr gpCcPlot
    ldy #GP_CC_D + 1             // d's sign is the HIGH byte's bit 7
    lda (dataPtr),y
    bmi gpCcEast
    //  --- SE: d += 4(x - y) + 10 with the OLD x and y, then y--, x++ ---
    ldy #GP_CC_X
    lda (dataPtr),y
    ldy #GP_CC_Y
    sec
    sbc (dataPtr),y              // old x - old y, as a two's complement byte
    ldy #GP_CC_TMP
    sta (dataPtr),y
    lda #0
    sbc #0                       // ...sign-extended into the high byte
    ldy #GP_CC_TMP + 1
    sta (dataPtr),y
    jsr gpCcTmpTimesFour         // tmp = 4(x - y)
    ldy #GP_CC_TMP
    lda (dataPtr),y
    clc
    adc #10
    sta (dataPtr),y
    ldy #GP_CC_TMP + 1
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y              // tmp = 4(x - y) + 10
    jsr gpCcDAddTmp
    ldy #GP_CC_Y
    lda (dataPtr),y
    sec
    sbc #1
    sta (dataPtr),y
    jmp gpCcIncX
gpCcEast:
    //  --- E: d += 4x + 6 with the OLD x, then x++ ---
    ldy #GP_CC_X
    lda (dataPtr),y
    asl
    ldy #GP_CC_TMP
    sta (dataPtr),y
    lda #0
    rol
    ldy #GP_CC_TMP + 1
    sta (dataPtr),y              // tmp = 2x
    jsr gpCcTmpTimesTwo          // tmp = 4x
    ldy #GP_CC_TMP
    lda (dataPtr),y
    clc
    adc #6
    sta (dataPtr),y
    ldy #GP_CC_TMP + 1
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y              // tmp = 4x + 6
    jsr gpCcDAddTmp
gpCcIncX:
    ldy #GP_CC_X
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    jmp gpCcStep
gpCcArcDone:
    ldy #GP_CC_ARC
    lda (dataPtr),y
    bne gpCcArcsDone             // ARC = 1: both arcs of this half are walked
    lda #1
    sta (dataPtr),y              // ...the shallow arc next, from the same quadrant
    jmp gpCcHalf
gpCcArcsDone:
    lda #$ff
    ldy #GP_CC_INV
    cmp (dataPtr),y
    bne gpCcHalfDone             // INV = 0: the bottom half has been walked as well
    lda #0
    ldy #GP_CC_INV
    sta (dataPtr),y              // the bottom half: CY + v, with nothing to invert
    ldy #GP_CC_CIN
    sta (dataPtr),y
    ldy #GP_CC_ARC
    sta (dataPtr),y              // ...and the steep arc again: both halves walk both
    jmp gpCcHalf
gpCcHalfDone:
    rts

// --- gpCcInit -- x = 0, y = R, d = 3 - 2R (16-bit) ---
gpCcInit:
    ldy #GP_CC_X
    lda #0
    sta (dataPtr),y
    ldy #GP_CC_R
    lda (dataPtr),y
    ldy #GP_CC_Y
    sta (dataPtr),y
    asl                          // 2R is up to 510, so it takes two bytes
    ldy #GP_CC_TMP
    sta (dataPtr),y
    lda #0
    rol
    ldy #GP_CC_TMP + 1
    sta (dataPtr),y
    lda #3
    sec
    ldy #GP_CC_TMP
    sbc (dataPtr),y
    ldy #GP_CC_D
    sta (dataPtr),y
    lda #0
    ldy #GP_CC_TMP + 1
    sbc (dataPtr),y
    ldy #GP_CC_D + 1
    sta (dataPtr),y
    rts

// --- gpCcTmpTimesTwo -- tmp = tmp * 2, 16-bit ---
gpCcTmpTimesTwo:
    ldy #GP_CC_TMP
    lda (dataPtr),y
    asl
    sta (dataPtr),y
    ldy #GP_CC_TMP + 1
    lda (dataPtr),y
    rol
    sta (dataPtr),y
    rts

// --- gpCcTmpTimesFour -- tmp = tmp * 4, 16-bit ---
gpCcTmpTimesFour:
    jsr gpCcTmpTimesTwo
    jmp gpCcTmpTimesTwo

// --- gpCcDAddTmp -- d += tmp, 16-bit ---
gpCcDAddTmp:
    ldy #GP_CC_D
    lda (dataPtr),y
    clc
    ldy #GP_CC_TMP
    adc (dataPtr),y
    ldy #GP_CC_D
    sta (dataPtr),y
    ldy #GP_CC_D + 1
    lda (dataPtr),y
    ldy #GP_CC_TMP + 1
    adc (dataPtr),y
    ldy #GP_CC_D + 1
    sta (dataPtr),y
    rts

// --- gpCcPoint -- GP_CC_PX = this step's x, GP_CC_NROW = its cell row ---
//  The steep arc takes its row from y and its x from x; the shallow one swaps them,
//  which is the 8-way symmetry the library's own circle is built on.
//  The row is CY +/- v, and the sign is a byte pair rather than a branch: INV is $ff on
//  the top half and CIN is then 1, so CY + (v ^ $ff) + 1 is CY - v.
gpCcPoint:
    ldy #GP_CC_ARC
    lda (dataPtr),y
    bne gpCcPtB
    ldy #GP_CC_Y
    lda (dataPtr),y
    ldy #GP_CC_V
    sta (dataPtr),y
    ldy #GP_CC_X
    lda (dataPtr),y
    ldy #GP_CC_W
    sta (dataPtr),y
    jmp gpCcPtRow
gpCcPtB:
    ldy #GP_CC_X
    lda (dataPtr),y
    ldy #GP_CC_V
    sta (dataPtr),y
    ldy #GP_CC_Y
    lda (dataPtr),y
    ldy #GP_CC_W
    sta (dataPtr),y
gpCcPtRow:
    ldy #GP_CC_CIN               // the carry the add below needs: 1 on the top half, so
    lda (dataPtr),y              //   the complemented v adds up to CY - v, and 0 after,
    lsr                          //   where v adds up to CY + v. LSRA, LDA, LDY and EOR
    ldy #GP_CC_V                 //   all leave C alone, so it survives the three loads
    lda (dataPtr),y
    ldy #GP_CC_INV
    eor (dataPtr),y
    ldy #GP_CC_CY
    adc (dataPtr),y              // A = the point's row
    php
    ldy #GP_CC_PROW
    sta (dataPtr),y              // ...the pixel to plot: the rim's own, so the walk
    pla                          //   cannot set one the rim never drew
    and #$01                     // its carry out is a wrap unless it matches CIN --
    ldy #GP_CC_CIN               //   cy - v going negative, or cy + v passing 255
    cmp (dataPtr),y
    beq gpCcRowOff
    lda #$ff                     // a row no cell can have, so the plot drops it: kPlot
    ldy #GP_CC_PROW              //   would take a wrapped row for a real one and ink a
    sta (dataPtr),y              //   cell inside the circle
gpCcRowOff:
    ldy #GP_CC_PROW
    lda (dataPtr),y
    lsr
    lsr
    lsr                          // ...and this is its cell
    ldy #GP_CC_NROW
    sta (dataPtr),y
    ldy #GP_CC_W                 // PX = CX + w, sixteen bits because the canvas is 320
    lda (dataPtr),y
    clc
    ldy #GP_CC_CX
    adc (dataPtr),y
    ldy #GP_CC_PX
    sta (dataPtr),y
    lda #0
    ldy #GP_CC_CX + 1
    adc (dataPtr),y
    ldy #GP_CC_PX + 1
    sta (dataPtr),y
    rts

// --- gpCcPlot -- plot both cells of this point, each when it is new ---
gpCcPlot:
    ldy #GP_CC_ROW
    lda (dataPtr),y
    ldy #GP_CC_NROW
    cmp (dataPtr),y
    beq gpCcPlSameRow
    lda #$ff                     // a new row is new cells either side, whatever the
    ldy #GP_CC_RCOL              //   columns were -- a column is only meaningful with
    sta (dataPtr),y              //   its row
    ldy #GP_CC_LCOL
    sta (dataPtr),y
    ldy #GP_CC_NROW
    lda (dataPtr),y
    ldy #GP_CC_ROW
    sta (dataPtr),y
gpCcPlSameRow:
    //  --- the right cell ---
    ldy #GP_CC_PX + 1            // column = (PX >> 3), nine bits of it
    lda (dataPtr),y
    asl
    asl
    asl
    asl
    asl
    ldy #GP_CC_COL
    sta (dataPtr),y
    ldy #GP_CC_PX
    lda (dataPtr),y
    lsr
    lsr
    lsr
    ldy #GP_CC_COL
    ora (dataPtr),y
    sta (dataPtr),y
    ldy #GP_CC_RCOL
    lda (dataPtr),y
    ldy #GP_CC_COL
    cmp (dataPtr),y
    beq gpCcPlLeft
    lda (dataPtr),y
    ldy #GP_CC_RCOL
    sta (dataPtr),y              // remember it
    jsr gpCcRdot
    jmp gpCcPlLeft

// --- gpCcRdot -- plot the cell of GP_CC_PX ---
//  WARNING: an x off the canvas is dropped HERE, not handed to kPlot. gpDot passes the
//  9th bit as bit 0 of the hi byte, which is an "x >= 256" flag only for 0 and 1: a
//  mirrored point's $ff reads as set and a far-right 2 reads as clear, and either becomes
//  a column on the wrong side of the screen.
gpCcRdot:
    ldy #GP_CC_PX + 1
    lda (dataPtr),y
    beq gpCcRXok                 // 0..255: on the canvas whatever the low byte
    cmp #1
    bne gpCcRDone                // $ff (behind the left edge) or 2+ (past the right)
    ldy #GP_CC_PX
    lda (dataPtr),y
    cmp #64
    bcs gpCcRDone                // 320..511
    bne gpCcRXok                 // always
gpCcRXok:
    ldy #GP_CC_PX                // the rim's own pixel: kPlot SETS what it colours, so
    lda (dataPtr),y              //   a cell's corner would leave a stray dot off the rim
    ldy #GP_LAST
    sta (dataPtr),y
    ldy #GP_CC_PX + 1
    lda (dataPtr),y
    ldy #GP_LAST + 1
    sta (dataPtr),y
    jmp gpCcRowSet
gpCcRDone:
    rts

// --- gpCcLdot -- plot the cell of GP_CC_LPX, the left-hand mirror ---
gpCcLdot:
    ldy #GP_CC_LPX + 1
    lda (dataPtr),y
    beq gpCcLXok
    cmp #1
    bne gpCcLDone
    ldy #GP_CC_LPX
    lda (dataPtr),y
    cmp #64
    bcs gpCcLDone
gpCcLXok:
    ldy #GP_CC_LPX
    lda (dataPtr),y              // the mirrored rim pixel, for the same reasons
    ldy #GP_LAST
    sta (dataPtr),y
    ldy #GP_CC_LPX + 1
    lda (dataPtr),y
    ldy #GP_LAST + 1
    sta (dataPtr),y
gpCcRowSet:
    ldy #GP_CC_PROW             // the mark is at (GP_LAST, this row) -- the point itself
    lda (dataPtr),y
    cmp #200
    bcs gpCcLDone                // off the canvas: the mark would be handed a wrapped row
    ldy #GP_LAST + 2
    sta (dataPtr),y
    jmp gpCellMark
gpCcLDone:
    rts

gpCcPlLeft:
    //  --- the left cell: the mirror of the PIXEL, 2*CX - PX ---
    ldy #GP_CC_CX
    lda (dataPtr),y
    asl
    ldy #GP_CC_LPX
    sta (dataPtr),y
    ldy #GP_CC_CX + 1
    lda (dataPtr),y
    rol
    ldy #GP_CC_LPX + 1
    sta (dataPtr),y              // LPX = 2*CX
    ldy #GP_CC_LPX
    lda (dataPtr),y
    sec
    ldy #GP_CC_PX
    sbc (dataPtr),y
    ldy #GP_CC_LPX
    sta (dataPtr),y
    ldy #GP_CC_LPX + 1
    lda (dataPtr),y
    ldy #GP_CC_PX + 1
    sbc (dataPtr),y
    ldy #GP_CC_LPX + 1
    sta (dataPtr),y              // LPX = 2*CX - PX
    ldy #GP_CC_LPX + 1           // its column, the same way round as the right's
    lda (dataPtr),y
    asl
    asl
    asl
    asl
    asl
    ldy #GP_CC_COL
    sta (dataPtr),y
    ldy #GP_CC_LPX
    lda (dataPtr),y
    lsr
    lsr
    lsr
    ldy #GP_CC_COL
    ora (dataPtr),y
    sta (dataPtr),y
    ldy #GP_CC_LCOL
    lda (dataPtr),y
    ldy #GP_CC_COL
    cmp (dataPtr),y
    beq gpCcPlDone
    lda (dataPtr),y
    ldy #GP_CC_LCOL
    sta (dataPtr),y
    jmp gpCcLdot
gpCcPlDone:
    rts

// --- gpDgBand -- XOR the gesture's outline, from the anchor and the pointer ---
//  The KERNEL writes these pixels: kBox walks the rectangle's four edges and kCircle the
//  circle's rim, and the pixel op is set to XOR around the call -- so one pass puts the
//  shape on the canvas and a second pass over the same shape takes it off again. That is
//  the whole preview: no copy of the canvas, no band of saved bytes, and no address
//  arithmetic in here. The shape arithmetic and the clipping are the library's, so a
//  point off the canvas is dropped instead of written.
//  WHICH shape is the tool's: the rectangles take the sorted edges with the width and
//  height gpBoxArgs marshals, and the circle takes the ANCHOR as its centre -- centre
//  out, so the press puts the centre down and the drag pulls the rim.
//  The op is set first and cleared last, so it cannot outlive the call that wanted it --
//  and it is set and cleared with the interrupts OFF, because the SWITCH PATH clears
//  it: a switch landing between the store and the shape call would strip the XOR and
//  have the shape written SET, which leaves those pixels in the picture for good. The
//  shape routines hold SEI for their own loops anyway, so the extra window is the
//  marshalling alone.
//  WARNING: do NOT put the set back behind kSetGfxColor -- it ends with a cli, which
//  opens exactly that window.
//  kBox and kCircle take their geometry in kArg, which is not where the op lives, so the
//  two do not interfere.
gpDgBand:
    sei                          // the op must not be cleared under us: a switch landing
    lda #1                       //   between this store and the shape call would have
    sta GP_GFXOP                 //   the shape written SET instead of XORed
    ldy #GP_TOOL
    lda (dataPtr),y
    cmp #GP_TOOL_CIRCLE
    beq gpDgBandCirc
    jsr gpBoxArgs                // kArg = X1/Y1/W/H, in CANVAS pixels
    jsr gpColour                 // A = what the outline claims (the ink, or GP_VAL)
    jsr kBox
    jmp gpDgBandDone
gpDgBandCirc:
    jsr gpCircleArgs             // kArg = centre/radius, in SCREEN pixels
    jsr gpColour                 // A = what the outline claims
    jsr gpCircCall               // ...and the circle the mode really draws
gpDgBandDone:
    lda #0
    sta GP_GFXOP                 // ...and ordinary drawing again, whatever the shape did
    cli
    rts

// --- gpCircCall -- draw the circle kArg describes, in the shape the mode really uses ---
//  BOTH the live outline and the commit come through here, and that is the point: the
//  preview erases and redraws it twice a frame, so an outline drawn differently from the
//  shape it promises is the one thing this cannot do -- kCircle against kEllipse leaves
//  the ring at twice the width of the circle that lands under it.
//  gpCircleArgs leaves the CENTRE and the RADIUS in SCREEN pixels -- the pointer the user
//  drags with works in those -- so the conversion lives here and nowhere else.
//  MC: a canvas pixel is TWO screen pixels wide and one tall, so a circle drawn on the
//  SCREEN is an ELLIPSE on the canvas: the x radius halves and the y radius does not.
//  kEllipse is the call that takes the two separately, which is exactly why it exists.
//  In: A = the colour. Clobbers per the calls below, and kArg+3/+4.
gpCircCall:
    pha                          // park the colour: the mode has to be read in A
    ldy #GP_MODE
    lda (dataPtr),y
    cmp #GP_MODE_MC
    beq gpCircCallMc
    pla                          // hires: a true circle, and the one radius
    jsr kCircle
    rts
gpCircCallMc:
    ldy #GP_WK_RAD
    lda (dataPtr),y              // the radius the user dragged, in screen pixels
    sta kArg+4                   // RY: unchanged -- a row is a row in both modes
    lsr
    sta kArg+3                   // RX: half of it
    pla                          // A = the colour
    jsr kEllipse
    rts

// --- gpDgSort -- save THIS frame's gesture geometry, for the band to draw from ---
//  A rectangle is described by its sorted edges (gpBoxSort) and a circle by its radius
//  (gpCircleSort), and BOTH are refreshed here so gpDgBand can read one description
//  twice: the erase pass wants the shape of the frame before, the draw pass the shape of
//  this one, and the only thing between them is this call.
//  The radius is computed for the circle alone, so a rectangle or a stroke drag never
//  runs gpDgRadius and never touches its scratch.
gpDgSort:
    jsr gpBoxSort
    ldy #GP_TOOL
    lda (dataPtr),y
    cmp #GP_TOOL_CIRCLE
    beq gpCircleSort
    rts

// --- gpCircleSort -- (GP_ANCH, GP_POS) -> GP_WK_RAD ---
//  Saved for the same reason the box's edges are: the drag takes the LAST frame's circle
//  off before it puts this frame's on, so the erase has to read a radius that this call
//  has not overwritten yet.
gpCircleSort:
    jsr gpDgRadius
    ldy #GP_WK_RAD
    sta (dataPtr),y
    rts

// --- gpCircleArgs -- kArg = the circle in GP_ANCH and GP_WK_RAD, in kCircle's form ---
//  kCircle takes kArg+0/1 centre X (lo/hi), +2 centre Y and +3 the radius. The centre is
//  the ANCHOR, because the press put it down; the radius is the SAVED one, so that the
//  band's two passes differ by exactly one gpDgSort.
gpCircleArgs:
    ldy #GP_ANCH
    lda (dataPtr),y
    tax
    ldy #GP_ANCH + 1
    lda (dataPtr),y
    jsr gpHalfX                  // A = the centre's canvas x, C = its 9th bit
    sta kArg                     // centre X lo
    lda #0
    rol
    sta kArg+1                   // ...and its high byte
    ldy #GP_ANCH + 2
    lda (dataPtr),y
    sta kArg+2                   // centre Y -- a row is the same in both modes
    ldy #GP_WK_RAD
    lda (dataPtr),y
    sta kArg+3                   // the radius, in SCREEN pixels: which call takes it and
                                 // whether it halves is gpCircleDraw's business
    rts

// --- gpDgRadius -- the longer of the two axis distances, capped at 255 ---
//  Out: A = the radius. The rim tracks the pointer exactly along either axis, which is
//  what makes the drag read as pulling the circle out; on a diagonal the rim is further
//  than the pointer, which is the price of not carrying a square root.
//  WARNING: the x difference is 16-BIT -- the canvas is 320 wide -- while the rows span
//  0-199, so their difference is one byte and its SIGN has to come from the comparison:
//  0 minus 199 is $39, which reads positive.
//  WARNING: |dx| lives in GP_WK_RJ and is reached with (dataPtr),y. A GP_ constant is
//  an OFFSET into the instance buffer and never an address, and a byte the task has no
//  claim on is not private either: the switch DMAs another task's ZP window in, so a
//  preemption between the store and the read returns a different radius -- and a radius
//  the two passes disagree on is a ring left on the canvas.
gpDgRadius:
    //  --- |dy| first: one byte, and it is the comparison that gives its sign ---
    ldy #GP_POS + 2
    lda (dataPtr),y
    ldy #GP_ANCH + 2
    cmp (dataPtr),y              // the pointer's row against the anchor's
    bcc gpDgRadYBack             // the pointer is above: the anchor's row is the lower
    sbc (dataPtr),y              // |dy| = POS.y - ANCH.y, the carry still set
    jmp gpDgRadYGot
gpDgRadYBack:
    ldy #GP_ANCH + 2
    lda (dataPtr),y
    ldy #GP_POS + 2
    sec
    sbc (dataPtr),y              // |dy| = ANCH.y - POS.y
gpDgRadYGot:
    pha                          // parked: |dx| needs the two scratch bytes
    //  --- dx, 16-bit, its sign in bit 7 of the high byte ---
    sec
    ldy #GP_POS
    lda (dataPtr),y
    ldy #GP_ANCH
    sbc (dataPtr),y
    ldy #GP_WK_RJ
    sta (dataPtr),y              // dx lo
    ldy #GP_POS + 1
    lda (dataPtr),y
    ldy #GP_ANCH + 1
    sbc (dataPtr),y              // ...the ldy/sta between the two keep the carry
    ldy #GP_WK_RJ + 1
    sta (dataPtr),y              // dx hi
    lda (dataPtr),y              // WARNING: read it BACK before testing its sign. LDY
    bpl gpDgRadBoth              //   sets N from the immediate it loads, so a `bpl`
                                 //   straight after `ldy #GP_WK_RJ + 1` tests the offset
                                 //   58 -- which is positive -- and never sees dx at all.
                                 //   STA and LDA keep nothing either; only C survives an
                                 //   intervening load, so the carry tests above are safe.
    //  --- negative: |dx| = -dx ---
    sec
    ldy #GP_WK_RJ
    lda #0
    sbc (dataPtr),y
    tax                          // |dx| lo
    ldy #GP_WK_RJ + 1
    lda #0
    sbc (dataPtr),y
    sta (dataPtr),y              // |dx| hi
    ldy #GP_WK_RJ
    txa
    sta (dataPtr),y              // |dx| lo
gpDgRadBoth:
    ldy #GP_WK_RJ + 1
    lda (dataPtr),y
    bne gpDgRadMax               // |dx| of 256 or more is past any radius
    pla                          // |dy|
    ldy #GP_WK_RJ
    cmp (dataPtr),y
    bcs gpDgRadRts               // |dy| is the longer one, and A is already it
    lda (dataPtr),y              // ...otherwise |dx| is
gpDgRadRts:
    rts
gpDgRadMax:
    pla                          // drop the parked |dy|
    lda #255                     // the radius is one byte, so this is as long as it gets
    rts

// --- gpBox -- the rectangles' one entry: sort the corners, then draw the rectangle ---
//  The anchor and the other corner are opposite corners, and either may be the higher,
//  the further left or the further right -- so the pair is sorted into a left/right edge
//  and a top/bottom row (gpBoxSort), which is the form kBox takes: kArg+0/1 X1, +2 Y1,
//  +4/5 width, +6 height, all 1-based.
//  The sort writes the WALK's own state (GP_WK_*) rather than the argument block,
//  because the pixel routines clobber it: gxPix builds REU addresses in kArg, and so
//  does kPlot, which the walk calls once per cell. Nothing of the rectangle may live
//  there between two of those calls. The live preview borrows the same sort, and the
//  same state, so a drag and its commit cannot disagree about which box is on screen.
gpBox:
    jsr gpBoxSort
    jmp gpBoxSize

// --- gpBoxSort -- (GP_ANCH, GP_POS) -> GP_WK_X1/X2/Y1/Y2 ---
//  The y pair has two paths -- the anchor's row may be above, below or level with the
//  other corner's -- and BOTH converge on the cell counts at the end: the counts are what
//  the live preview and the colour walk agree through, so a path that skipped them would
//  outline one rectangle and colour another.
//  WARNING: the x pair is a signed 16-BIT difference -- the canvas is 320 wide, so a
//  single byte cannot hold it -- while the y pair is an UNSIGNED compare of the two
//  rows. A signed byte difference is what would break: the rows span 0-199, and 0 minus
//  199 is $39, which reads POSITIVE.
gpBoxSort:
    //  --- the x pair: the sign of the difference says which corner is the left one ---
    sec
    ldy #GP_POS
    lda (dataPtr),y
    ldy #GP_ANCH
    sbc (dataPtr),y              // A = click.x - anchor.x, low byte
    sta kArg
    ldy #GP_POS + 1
    lda (dataPtr),y
    ldy #GP_ANCH + 1
    sbc (dataPtr),y              // ...and its high byte: bit 7 is its sign
    sta kArg+1
    bmi gpBoxXClickLeft          // the click is the left one
    //  --- the anchor is the left edge, the click the right one ---
    ldy #GP_ANCH
    lda (dataPtr),y
    ldy #GP_WK_X1
    sta (dataPtr),y
    ldy #GP_ANCH + 1
    lda (dataPtr),y
    ldy #GP_WK_X1 + 1
    sta (dataPtr),y
    ldy #GP_POS
    lda (dataPtr),y
    ldy #GP_WK_X2
    sta (dataPtr),y
    ldy #GP_POS + 1
    lda (dataPtr),y
    ldy #GP_WK_X2 + 1
    sta (dataPtr),y
    jmp gpBoxRows
gpBoxXClickLeft:
    ldy #GP_POS
    lda (dataPtr),y
    ldy #GP_WK_X1
    sta (dataPtr),y
    ldy #GP_POS + 1
    lda (dataPtr),y
    ldy #GP_WK_X1 + 1
    sta (dataPtr),y
    ldy #GP_ANCH
    lda (dataPtr),y
    ldy #GP_WK_X2
    sta (dataPtr),y
    ldy #GP_ANCH + 1
    lda (dataPtr),y
    ldy #GP_WK_X2 + 1
    sta (dataPtr),y
gpBoxRows:
    //  --- the y pair: an unsigned compare, the rows being 0-199 ---
    ldy #GP_ANCH + 2
    lda (dataPtr),y
    ldy #GP_POS + 2
    cmp (dataPtr),y              // the anchor's row against the click's
    bcc gpBoxYAnchorTop          // the anchor's row is the upper one
    beq gpBoxYAnchorTop          // ...or they are the same row
    ldy #GP_POS + 2              // the click's row is the upper one
    lda (dataPtr),y
    ldy #GP_WK_Y1
    sta (dataPtr),y
    ldy #GP_ANCH + 2
    lda (dataPtr),y
    ldy #GP_WK_Y2
    sta (dataPtr),y
    jmp gpBoxCells               // both y paths converge on the counts below
gpBoxYAnchorTop:
    ldy #GP_ANCH + 2
    lda (dataPtr),y
    ldy #GP_WK_Y1
    sta (dataPtr),y
    ldy #GP_POS + 2
    lda (dataPtr),y
    ldy #GP_WK_Y2
    sta (dataPtr),y
gpBoxCells:
    //  --- the cell counts, which BOTH the colour walk and the live preview need. They
    //      belong to the sort rather than to either of its users: the cells an edge falls
    //      in are cell = (x >> 3), and a canvas 320 wide has a SECOND bank of 32 cells at
    //      x >= 256 -- the high byte is the 32 -- which the walk and the preview must
    //      agree about or they colour and outline different rectangles ---
    ldy #GP_WK_X2
    lda (dataPtr),y
    lsr
    lsr
    lsr
    ldy #GP_WK_T
    sta (dataPtr),y              // X2's cell
    ldy #GP_WK_X2 + 1
    lda (dataPtr),y
    beq gpBoxCxLeft
    ldy #GP_WK_T
    lda (dataPtr),y
    clc
    adc #32
    sta (dataPtr),y
gpBoxCxLeft:
    ldy #GP_WK_X1
    lda (dataPtr),y
    lsr
    lsr
    lsr
    ldy #GP_WK_T + 1
    sta (dataPtr),y              // X1's cell
    ldy #GP_WK_X1 + 1
    lda (dataPtr),y
    beq gpBoxCxDiff
    ldy #GP_WK_T + 1
    lda (dataPtr),y
    clc
    adc #32
    sta (dataPtr),y
gpBoxCxDiff:
    sec
    ldy #GP_WK_T
    lda (dataPtr),y
    ldy #GP_WK_T + 1
    sbc (dataPtr),y
    clc
    adc #1
    ldy #GP_WK_CX
    sta (dataPtr),y              // the cells across
    //  --- and the cells down, the cell rows being one byte of 0-199 over 8 ---
    ldy #GP_WK_Y2
    lda (dataPtr),y
    lsr
    lsr
    lsr
    ldy #GP_WK_T
    sta (dataPtr),y              // Y2's cell row
    ldy #GP_WK_Y1
    lda (dataPtr),y
    lsr
    lsr
    lsr
    ldy #GP_WK_T + 1
    sta (dataPtr),y              // Y1's
    sec
    ldy #GP_WK_T
    lda (dataPtr),y
    ldy #GP_WK_T + 1
    sbc (dataPtr),y
    clc
    adc #1
    ldy #GP_WK_CY
    sta (dataPtr),y
    //  --- the sort ends here, so the preview can walk the edges without drawing ---
    rts
// --- gpBoxArgs -- kArg = the rectangle in GP_WK_*, in the form kBox takes ---
//  kBox takes kArg+0/1 X1, +2 Y1, +4/5 the width and +6 the height, all 1-based, and W is
//  sixteen bits because the canvas is 320 wide. BOTH the commit and the live preview come
//  through here, so the box that is previewed and the box that is drawn cannot be
//  marshalled differently.
gpBoxArgs:
    //  --- the two x edges in CANVAS pixels: halved in MC, where the canvas is 160 across.
    //      The ROWS are the same in both modes, and the WALK keeps using GP_WK_X1/X2 in
    //      SCREEN pixels -- it marks through gpDot, which halves for it ---
    ldy #GP_WK_X1
    lda (dataPtr),y
    tax
    ldy #GP_WK_X1 + 1
    lda (dataPtr),y
    jsr gpHalfX                  // A = X1's canvas x, C = its 9th bit
    sta kArg                     // X1 low
    lda #0
    rol
    sta kArg+1                   // ...and its high byte
    ldy #GP_WK_X2
    lda (dataPtr),y
    tax
    ldy #GP_WK_X2 + 1
    lda (dataPtr),y
    jsr gpHalfX                  // A = X2's canvas x, C = its 9th bit
    sta kArg+4                   // X2, until W has been worked out of it
    lda #0
    rol
    sta kArg+5
    //  --- W = X2 - X1 + 1, which is 1-320 and so 16-bit ---
    sec
    lda kArg+4
    sbc kArg
    sta kArg+4
    lda kArg+5
    sbc kArg+1
    sta kArg+5
    clc
    lda kArg+4
    adc #1
    sta kArg+4                   // W low
    lda kArg+5
    adc #0
    sta kArg+5                   // ...and its high, which the carry rides into
    //  --- H = Y2 - Y1 + 1, which is 1-200 and so one byte ---
    ldy #GP_WK_Y2
    lda (dataPtr),y
    ldy #GP_WK_Y1
    sec
    sbc (dataPtr),y
    clc
    adc #1
    sta kArg+6                   // H
    ldy #GP_WK_Y1
    lda (dataPtr),y
    sta kArg+2                   // Y1
    rts

// --- gpBoxSize -- the commit: kBox's pixels, then every cell they enter ---
gpBoxSize:
    jsr gpBoxArgs
    //  --- the rectangle's own pixels. Both go through kBox/kFillBox, which write the
    //      PIXEL only -- the matrix, and so the colour the cells take, is the walk's
    //      job, because it is kPlot that writes the nibble ---
    ldy #GP_TOOL
    lda (dataPtr),y
    cmp #GP_TOOL_FILLBOX
    beq gpBoxFilled
    jsr gpColour                 // A = what a mark claims (the ink, or GP_VAL)
    jsr kBox
    jmp gpBoxWalk
gpBoxFilled:
    jsr gpColour
    jsr kFillBox

// --- gpBoxWalk -- colour every cell the rectangle enters, one mark per cell ---
//  The walk visits the CELL grid, one point per cell -- and gpCellMark decides what a cell
//  costs: in hires the kPlot that writes its nibble, in MC the claim, with no pixel at all.
//  Where it does plot, the points have to be ones kBox or kFillBox already drew: every span
//  runs along the edge it belongs to (X1, X2, Y1, Y2) and steps to the following cell
//  starts, which keeps the walk inside the rectangle -- a point outside it, or inside an
//  outline, would show as a stray dot.
//  The outline is its two edge columns and its two edge rows -- which closes the coloured
//  cells around the pixels -- and the fill is every cell row, whose boundary points kPlot
//  simply sets again.
//  The cursor IS GP_LAST -- x lo, x hi, row -- the address gpDot plots, and the walk
//  borrows it the same way a stroke does. The counts and the rectangle's edges live in
//  GP_WK_*, never in kArg: gpDot is kPlot, and kPlot clobbers kArg, as gxBox/gxFillBox
//  clobber it on their own REU paths. The COUNTS are the sort's, not this walk's, which
//  is what stops it and the live preview disagreeing about which rectangle they are on.
gpBoxWalk:
    ldy #GP_TOOL
    lda (dataPtr),y
    cmp #GP_TOOL_FILLBOX
    beq gpBoxFill
    //  --- the outline: the two edge columns, then the two edge rows. Each span sets
    //      the coordinate it does NOT walk itself, because the span before it left that
    //      coordinate one cell past its last point ---
    ldy #GP_WK_X1
    lda (dataPtr),y
    ldy #GP_LAST
    sta (dataPtr),y
    ldy #GP_WK_X1 + 1
    lda (dataPtr),y
    ldy #GP_LAST + 1
    sta (dataPtr),y              // x = the left edge
    jsr gpBoxSpanY
    ldy #GP_WK_X2
    lda (dataPtr),y
    ldy #GP_LAST
    sta (dataPtr),y
    ldy #GP_WK_X2 + 1
    lda (dataPtr),y
    ldy #GP_LAST + 1
    sta (dataPtr),y              // x = the right edge
    jsr gpBoxSpanY
    ldy #GP_WK_Y1
    lda (dataPtr),y
    ldy #GP_LAST + 2
    sta (dataPtr),y              // y = the top edge
    jsr gpBoxSpanX
    ldy #GP_WK_Y2
    lda (dataPtr),y
    ldy #GP_LAST + 2
    sta (dataPtr),y              // y = the bottom edge
    jmp gpBoxSpanX               // whose rts returns to the shape's caller
gpBoxFill:
    //  --- the fill: every cell row of the rectangle, counted down in GP_WK_R. Each
    //      pass plots its own row, so the counter reaching zero IS the last row -- there
    //      is nothing left to draw after the loop ---
    ldy #GP_WK_Y1
    lda (dataPtr),y
    ldy #GP_LAST + 2
    sta (dataPtr),y              // y = the top edge: the first row's cells
    ldy #GP_WK_CY
    lda (dataPtr),y
    ldy #GP_WK_R
    sta (dataPtr),y
gpBoxFillRow:
    jsr gpBoxSpanX
    ldy #GP_LAST + 2
    lda (dataPtr),y
    clc
    adc #8
    and #$f8                     // the next cell row's first row
    sta (dataPtr),y
    ldy #GP_WK_R
    lda (dataPtr),y
    sec
    sbc #1
    sta (dataPtr),y
    bne gpBoxFillRow             // every pass drew its row, so this is the last one
    rts

// --- gpBoxSpanX -- one cell row: the cells across, at the row the caller set ---
//  The first point is the rectangle's LEFT EDGE itself, and the steps after it go to the
//  following cells' first columns. A step of 8 from the edge would run past X2 in the
//  last cell, and stepping from the first cell's own start would plot left of the edge
//  before it -- and kPlot SETS the pixel it colours, so either would leave a stray dot.
//  The counter is reloaded from the saved count for every span, so the walk needs no
//  bounds test: the cell is plotted, then the counter is tested.
//  WARNING: GP_WK_N is shared with gpBoxSpanY. They are never nested, but a call to one
//  from inside the other would lose the outer walk's count.
gpBoxSpanX:
    ldy #GP_WK_X1
    lda (dataPtr),y
    ldy #GP_LAST
    sta (dataPtr),y              // x = the left edge
    ldy #GP_WK_X1 + 1
    lda (dataPtr),y
    ldy #GP_LAST + 1
    sta (dataPtr),y
    ldy #GP_WK_CX
    lda (dataPtr),y
    ldy #GP_WK_N
    sta (dataPtr),y
gpBoxSpanXCell:
    jsr gpCellMark               // the cell's colour: the nibble in hires, the claim in MC
    clc
    ldy #GP_LAST
    lda (dataPtr),y
    adc #8
    and #$f8                     // the next cell's first column
    sta (dataPtr),y
    bcc gpBoxSpanXCount
    ldy #GP_LAST + 1
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y              // ...and its 9th bit, which the carry rides into
gpBoxSpanXCount:
    ldy #GP_WK_N
    lda (dataPtr),y
    sec
    sbc #1
    sta (dataPtr),y
    bne gpBoxSpanXCell
    rts

// --- gpBoxSpanY -- one cell column: the cells down, at the column the caller set ---
//  As gpBoxSpanX, down the column: the first point is the TOP EDGE itself and the steps
//  after it go to the following cells' first rows.
gpBoxSpanY:
    ldy #GP_WK_Y1
    lda (dataPtr),y
    ldy #GP_LAST + 2
    sta (dataPtr),y              // y = the top edge
    ldy #GP_WK_CY
    lda (dataPtr),y
    ldy #GP_WK_N
    sta (dataPtr),y
gpBoxSpanYCell:
    jsr gpCellMark
    ldy #GP_LAST + 2
    lda (dataPtr),y
    clc
    adc #8
    and #$f8                     // the next cell row's first row
    sta (dataPtr),y
    ldy #GP_WK_N
    lda (dataPtr),y
    sec
    sbc #1
    sta (dataPtr),y
    bne gpBoxSpanYCell
    rts

// --- gpSeek -- GP_LAST = GP_POS: a stroke begins where the pointer is ---
gpSeek:
    ldy #GP_POS
    lda (dataPtr),y
    ldy #GP_LAST
    sta (dataPtr),y
    ldy #GP_POS + 1
    lda (dataPtr),y
    ldy #GP_LAST + 1
    sta (dataPtr),y
    ldy #GP_POS + 2
    lda (dataPtr),y
    ldy #GP_LAST + 2
    sta (dataPtr),y
    rts

// --- gpSeg -- draw the segment from GP_LAST to GP_POS, colouring as it goes ---
//  kPlot is called for every pixel rather than kLine plus a colouring pass: it is
//  the only public call that commits a cell's matrix high nibble, and in hires a
//  cell's colour is what every set pixel in it shows -- so one call per pixel both
//  draws the stroke and gives it its ink, and no second path can disagree with the
//  first about which cells the line crosses.
//  The walk is the classic Bresenham with the error held BELOW the major span. A
//  steep line falls out of that: when the minor span is the longer one, the
//  catch-up loop steps the minor axis -- and plots -- several times per major step,
//  so a line steeper than 45 degrees comes out as the run of pixels it really is
//  instead of a dotted diagonal.
//  Both endpoints come from gpClamp1, so the whole segment is on the canvas and
//  nothing here needs a clip. No plotted pixel can leave the canvas either: the
//  walk only ever steps the two axes TOWARDS the destination.
gpSeg:
    //  --- dx = x1 - x0, and from it |dx| and the x step ---
    ldy #GP_POS
    lda (dataPtr),y
    ldy #GP_LAST
    sec
    sbc (dataPtr),y
    ldy #GP_AJ
    sta (dataPtr),y              // dx lo
    ldy #GP_POS + 1
    lda (dataPtr),y
    ldy #GP_LAST + 1
    sbc (dataPtr),y              // dx hi; N is its sign
    bmi gpSegXBack
    ldy #GP_AJ + 1
    sta (dataPtr),y              // |dx| = dx
    ldy #GP_SX
    lda #1
    sta (dataPtr),y
    ldy #GP_SXH
    lda #0
    sta (dataPtr),y
    jmp gpSegY
gpSegXBack:
    ldy #GP_AJ + 1
    sta (dataPtr),y              // dx hi, before the negation consumes it
    ldy #GP_AJ
    lda #0
    sec
    sbc (dataPtr),y
    sta (dataPtr),y
    ldy #GP_AJ + 1
    lda #0
    sbc (dataPtr),y
    sta (dataPtr),y              // |dx| = -dx
    ldy #GP_SX
    lda #$ff
    sta (dataPtr),y
    ldy #GP_SXH
    sta (dataPtr),y              // ...and the step is -1, sign extension included
gpSegY:
    //  --- dy = y1 - y0, and from it |dy| and the y step ---
    ldy #GP_POS + 2
    lda (dataPtr),y
    ldy #GP_LAST + 2
    sec
    sbc (dataPtr),y              // dy; N is its sign
    bmi gpSegYBack
    ldy #GP_AI
    sta (dataPtr),y
    ldy #GP_AI + 1
    lda #0
    sta (dataPtr),y              // |dy| = dy
    ldy #GP_SY
    lda #1
    sta (dataPtr),y
    jmp gpSegPick
gpSegYBack:
    eor #$ff
    clc
    adc #1                       // |dy| = -dy
    ldy #GP_AI
    sta (dataPtr),y
    ldy #GP_AI + 1
    lda #0
    sta (dataPtr),y
    ldy #GP_SY
    lda #$ff
    sta (dataPtr),y
gpSegPick:
    //  --- the longer span is the major axis: it is what the walk counts on ---
    ldy #GP_AJ
    lda (dataPtr),y
    ldy #GP_AI
    cmp (dataPtr),y
    ldy #GP_AJ + 1
    lda (dataPtr),y
    ldy #GP_AI + 1
    sbc (dataPtr),y              // |dx| - |dy|: C set when x is the major axis
    bcs gpSegXMaj
    //  y is the major axis: the two spans swap, parking the old major in CNT
    ldy #GP_AJ
    lda (dataPtr),y
    ldy #GP_CNT
    sta (dataPtr),y
    ldy #GP_AJ + 1
    lda (dataPtr),y
    ldy #GP_CNT + 1
    sta (dataPtr),y
    ldy #GP_AI
    lda (dataPtr),y
    ldy #GP_AJ
    sta (dataPtr),y
    ldy #GP_AI + 1
    lda (dataPtr),y
    ldy #GP_AJ + 1
    sta (dataPtr),y              // the major span is |dy|
    ldy #GP_CNT
    lda (dataPtr),y
    ldy #GP_AI
    sta (dataPtr),y
    ldy #GP_CNT + 1
    lda (dataPtr),y
    ldy #GP_AI + 1
    sta (dataPtr),y              // ...and the minor span is |dx|
    ldy #GP_MJ
    lda #1
    sta (dataPtr),y
    bne gpSegStart
gpSegXMaj:
    ldy #GP_MJ
    lda #0
    sta (dataPtr),y
gpSegStart:
    //  --- a pointer that has not moved writes NOTHING, not even the pixel it is
    //      on: a held button with a still hand is not a stroke ---
    ldy #GP_AJ
    lda (dataPtr),y
    ldy #GP_AJ + 1
    ora (dataPtr),y
    ldy #GP_AI
    ora (dataPtr),y
    ldy #GP_AI + 1
    ora (dataPtr),y
    beq gpSegRts
    //  --- CNT counts major steps, and the walk plots span+1 pixels ---
    ldy #GP_AJ
    lda (dataPtr),y
    ldy #GP_CNT
    sta (dataPtr),y
    ldy #GP_AJ + 1
    lda (dataPtr),y
    ldy #GP_CNT + 1
    sta (dataPtr),y
    //  --- the error starts at zero, so the line hugs the start of each span ---
    lda #0
    ldy #GP_ERR
    sta (dataPtr),y
    ldy #GP_ERR + 1
    sta (dataPtr),y
gpSegLoop:
    jsr gpDot                    // the pixel the walk is on
    ldy #GP_CNT
    lda (dataPtr),y
    ldy #GP_CNT + 1
    ora (dataPtr),y
    beq gpSegRts                 // no step left: the destination is plotted
    jsr gpSegMaj
    jsr gpSegAdv
    ldy #GP_CNT
    lda (dataPtr),y
    sec
    sbc #1
    sta (dataPtr),y
    ldy #GP_CNT + 1
    lda (dataPtr),y
    sbc #0
    sta (dataPtr),y              // the low byte's borrow rides in
    jmp gpSegLoop
gpSegRts:
    rts

// --- gpSegMaj -- step the axis the walk counts on ---
gpSegMaj:
    ldy #GP_MJ
    lda (dataPtr),y
    beq gpSegMajX
    jmp gpStepY
gpSegMajX:
    jmp gpStepX

// --- gpSegAdv -- add the minor span to the error, and step the minor axis while
//      the error is at or past the major span, plotting each pixel that lands on ---
//  The error is kept below the major span, so it never needs more than the span's
//  width does; the carry out of the add carries into the high byte exactly as the
//  16-bit comparison below expects.
gpSegAdv:
    ldy #GP_ERR
    lda (dataPtr),y
    ldy #GP_AI
    clc
    adc (dataPtr),y
    ldy #GP_ERR
    sta (dataPtr),y
    ldy #GP_AI + 1
    lda (dataPtr),y
    ldy #GP_ERR + 1
    adc (dataPtr),y              // the low byte's carry rides in
    sta (dataPtr),y
gpSegAdvTest:
    ldy #GP_ERR + 1
    lda (dataPtr),y
    ldy #GP_AJ + 1
    cmp (dataPtr),y
    bcc gpSegAdvRts              // lower in the high byte: err is below the span
    bne gpSegAdvRed              // higher: reduce
    ldy #GP_ERR
    lda (dataPtr),y
    ldy #GP_AJ
    cmp (dataPtr),y
    bcc gpSegAdvRts
gpSegAdvRed:
    ldy #GP_ERR
    lda (dataPtr),y
    ldy #GP_AJ
    sec
    sbc (dataPtr),y
    ldy #GP_ERR
    sta (dataPtr),y
    ldy #GP_ERR + 1
    lda (dataPtr),y
    ldy #GP_AJ + 1
    sbc (dataPtr),y
    ldy #GP_ERR + 1
    sta (dataPtr),y
    jsr gpSegMin                 // the minor axis advances...
    jsr gpDot                    // ...and the pixel it lands on is plotted
    jmp gpSegAdvTest             // a steeper-than-45 line steps it more than once
gpSegAdvRts:
    rts

// --- gpSegMin -- step the axis the walk does not count on ---
gpSegMin:
    ldy #GP_MJ
    lda (dataPtr),y
    beq gpSegMinY
    jmp gpStepX
gpSegMinY:
    jmp gpStepY

// --- gpStepX -- move the walk's x by one pixel ---
//  The step is held as a signed byte and so is its sign extension, so the 16-bit
//  move is two adds and the carry between them rather than a direction test.
gpStepX:
    ldy #GP_SX
    lda (dataPtr),y
    ldy #GP_LAST
    clc
    adc (dataPtr),y
    sta (dataPtr),y
    ldy #GP_SXH
    lda (dataPtr),y
    ldy #GP_LAST + 1
    adc (dataPtr),y
    sta (dataPtr),y
    rts

// --- gpStepY -- move the walk's y by one pixel ---
gpStepY:
    ldy #GP_SY
    lda (dataPtr),y
    ldy #GP_LAST + 2
    clc
    adc (dataPtr),y
    sta (dataPtr),y
    rts

// --- gpDot -- plot the pixel the walk is on, in the ink ---
//  kPlot wants X = col lo, Y = row, A = the ink and Carry = col hi. LDX, LDY, LDA,
//  TAX and TAY all leave the carry alone, so the 9th bit is set first and survives
//  the three loads.
//  EVERY mark in the task comes through gpDotGo below, so every mark is PLOTTED and then
//  CLAIMED: in MC the plotted value alone would show whichever colour the cell already
//  carried, and gpClaim gives the cell the pen's colour.
gpDot:
    ldy #GP_DIRTY                // every mark that lands on the canvas comes through
    lda #1                       //   here, so this is where "there is something to
    sta (dataPtr),y              //   save" is set: the pencil, the brushes, the line
                                 //   and every shape's cell walk
    ldy #GP_U_DRAW
    sta (dataPtr),y              // ...and where a mark says it is not in a state yet:
                                 //   the line's anchor click marks its pixel and the
                                 //   LINE is the step, so a step taken before the second
                                 //   click takes the dot off instead (gpDropMark)
    ldy #GP_TOOL                 // a brush marks a blob of its own, not one pixel -- and
    lda (dataPtr),y              //   the test is here, in the one routine every mark goes
    sec                          //   through, so the pencil, its segment walk and the
    sbc #GP_TOOL_BRUSH1          //   shape walks all inherit it unchanged
    cmp #3
    bcs gpDotGo
    jmp gpBrush
gpDotGo:
    jsr gpColour                 // A = the colour a mark claims (the ink, or GP_VAL)
    pha                          // park it: A and Y are needed for the pixel
    ldy #GP_LAST                 // X = the pixel's screen x, low byte
    lda (dataPtr),y
    tax
    ldy #GP_LAST + 1
    lda (dataPtr),y              // A = the x's high byte -- its bit 0 is the 9th bit
    jsr gpHalfX                  // A = the CANVAS x, C = its 9th bit
    tax                          // ...which is where kPlot wants it; C survives
    ldy #GP_LAST + 2
    lda (dataPtr),y
    tay                          // Y = the row
    pla                          // A = the colour. None of these touch the carry.
    jsr kPlot                    // the pixel itself...
    jmp gpClaim                  // ...and then the cell it landed in takes the colour

// --- gpClaim -- the cell a mark lands in takes the pen's colour ---
//  EVERY mark comes through gpDotGo, so the pencil, the three brushes, the line, a single
//  click and a shape's cell walk all inherit this. In MC a pixel is a 2-bit VALUE and the
//  colour it shows is its CELL's byte -- and a loaded picture's cells each carry their own
//  two colours -- so without this a stroke shows the colours the cells already had,
//  changing from cell to cell. This is what the HIRES mark gets free from kPlot writing
//  the ink nibble: the cell's colour for the pen's value becomes the pen's colour.
//  It writes ONE nibble, so the cell's other colour survives -- exactly as the hires mark
//  keeps the paper. Value 0 writes nothing at all: it is $D021, one register for the whole
//  picture, and a slot-0 swatch press is what sets that.
//  A stroke crosses a few cells and marks many pixels, so the last cell claimed is
//  remembered -- the cell AND the value -- and a mark inside it costs a compare. Nothing
//  that fills the whole canvas (gpCells: a fresh canvas, CLEAR, a palette press, a mode
//  toggle) needs that cache cleared, because every one of them writes the very palette
//  byte this writes.
//  WARNING: the byte written is the FRAMEBUFFER's, so it is written only while OUR surface
//  is on screen (gpDgOurs) -- the guard every band writer makes. A mark made with another
//  task's screen up goes into our REU slot through kPlot and keeps the colours that slot's
//  cells already had, which is invisible while it happens.
//  In: GP_LAST = the pixel the mark is on (its row at +2). Clobbers A, X, Y, kPtr.
gpClaim:
    ldy #GP_MODE
    lda (dataPtr),y
    cmp #GP_MODE_MC
    bne gpClaimRts               // hires: kPlot has already written the cell's ink
    jsr gpDgOurs                 // WARNING: clobbers A, so the value is read below
    bne gpClaimRts
    ldy #GP_VAL
    lda (dataPtr),y              // the pen's value, 0-3
    beq gpClaimRts               // 0 is the background: a register, not a cell byte
    sta gpClaimV                 // ...and the value is the cache's third field
    ldx #GP_LAST
    jsr gpCellOff                // kPtr = the mark's cell, gpTmp = its column
                                 //   (then: is this the cell claimed a moment ago?)
    lda kPtr
    cmp gpClaimCc
    bne gpClaimGo
    lda kPtr+1
    cmp gpClaimCc+1
    bne gpClaimGo
    lda gpClaimV
    cmp gpClaimVc
    beq gpClaimRts               // the byte below is already what it would write
gpClaimGo:
    lda kPtr                     // remember the cell...
    sta gpClaimCc
    lda kPtr+1
    sta gpClaimCc+1
    lda gpClaimV
    sta gpClaimVc                // ...and the value it was claimed for
    tay
    lda gpClaimEnt,y             // = GP_MB + the value: the colour that value reads
    tay
    lda (dataPtr),y              // the pen's colour for that value
    ldy gpClaimV
    cpy #3
    beq gpClaimCram              // 3 reads the colour RAM, not the matrix
    cpy #1
    bne gpClaimLow               // 2: the LOW nibble takes the colour as it stands
    asl
    asl
    asl
    asl                          // 1: the pen's colour into the HIGH nibble...
    sta gpClaimV
    ldy #0
    lda (kPtr),y
    and #$0f                     // ...keeping the cell's own low one
    ora gpClaimV
    sta (kPtr),y
    rts
gpClaimLow:
    sta gpClaimV
    ldy #0
    lda (kPtr),y
    and #$f0                     // the cell's own high nibble stays
    ora gpClaimV
    sta (kPtr),y
    rts
gpClaimCram:
    //  --- value 3 reads the colour RAM: the same cell, one page family along ---
    sta gpClaimV
    lda kPtr+1
    clc
    adc #$d4                     // $D800 - $0400: the colour RAM's own page
    sta kPtr+1
    ldy #0
    lda gpClaimV
    sta (kPtr),y
gpClaimRts:
    rts

// --- gpCellMark -- one CELL of a shape's cell walk ---
//  Both walks come through here. In HIRES the cell's colour IS the nibble kPlot writes,
//  so the walk plots the cell's own pixel -- the commit's only chance to colour a cell,
//  since kBox and kCircle write pixels only. In MC the walk CLAIMS the cell and plots
//  nothing: the rim's pixels are the library's, and kPlot SETS what it colours, so this
//  walk's own rasterisation of a curve leaves stray dots on the circumference.
//  GP_DIRTY is set HERE, because a shape's pixels are the library's and the library knows
//  nothing about saving. The preview is the one other user of kBox/kCircle and must NOT
//  set it: it draws under the XOR op and comes off again on the next frame.
//  In: GP_LAST = the cell's own pixel (its row at +2). Clobbers A, X, Y, kPtr.
gpCellMark:
    ldy #GP_DIRTY
    lda #1
    sta (dataPtr),y
    ldy #GP_MODE
    lda (dataPtr),y
    cmp #GP_MODE_MC
    bne gpCellMarkHires
    jmp gpClaim
gpCellMarkHires:
    jmp gpDot

//  The claim's cache and its table. Code-image scratch: the task runs ONE instance, and
//  nothing here outlives the call. gpClaimEnt is indexed by the pen's VALUE, which is why
//  entry 0 is never read -- value 0 has already returned.
gpClaimCc:  .fill 2, 0               // the cell claimed last: low byte, then high
gpClaimVc:  .byte 0                  // ...and the value it was claimed for
gpClaimV:   .byte 0
gpClaimEnt: .byte 0, GP_MB+1, GP_MB+2, GP_MB+3

// --- gpGlyphW -- A = one glyph's pitch, in SCREEN pixels ---
//  A font glyph is EIGHT CANVAS pixels each way, and in MC a canvas pixel is TWO screen
//  pixels across while gxCharPlotMc also plots each glyph row TWICE (which is what keeps
//  a character's shape when its pixels are twice as wide). So both pitches are the same
//  number on the screen: 8 in hires and 16 in MC -- what the pen steps across a line, what
//  it steps down one, and what the box a glyph inks is measured in. The CANVAS pitch stays
//  eight in both modes; it is the screen pitch the pen works in, and every use of it here
//  is a screen coordinate or a screen distance.
//  Clobbers A, Y.
gpGlyphW:
    ldy #GP_MODE
    lda (dataPtr),y
    cmp #GP_MODE_MC
    beq gpGlyphWMc
    lda #8
    rts
gpGlyphWMc:
    lda #16
    rts

// --- gpColour -- A = the colour a mark claims ---
//  Hires: a pixel IS a colour and the picker holds one, so a mark draws in the ink.
//  MC: a pixel is a two-bit VALUE selecting one of the cell's four colour sources, and the
//  picker holds which SOURCE a mark claims (GP_VAL -- the slot a chip selected). So a mark
//  writes the value, and the colour it shows is whichever source that value names. Value 0
//  is the background: a pen on it marks nothing at all, an XOR with 0 toggling no bit.
//  Clobbers A, Y.
gpColour:
    ldy #GP_MODE
    lda (dataPtr),y
    cmp #GP_MODE_MC
    beq gpColourMc
    ldy #GP_INK
    lda (dataPtr),y
    rts
gpColourMc:
    ldy #GP_VAL
    lda (dataPtr),y
    rts

// --- gpHalfX -- A = the x in (A = its high byte, X = its low byte), in CANVAS pixels ---
//  THE ONE PLACE A SCREEN COORDINATE BECOMES A LIBRARY ONE. Hires: the canvas IS the
//  screen, 320 across, so the coordinate passes through. MC: the canvas is 160 FAT pixels
//  across against the screen's 320, so the x is halved -- and halved ONCE, here, never
//  anywhere the task works: GP_POS, the clamp, the band's hit test and the pointer sprite
//  all stay in screen pixels, which is what keeps a drag and the marks it makes agreeing
//  with the pointer the user sees.
//  Out: A = the canvas x low byte, C = its 9th bit -- exactly the pair kPlot takes. The
//  x is under 512, so its 9th bit is bit 0 of the high byte. Clobbers A, C, Y.
gpHalfX:
    pha                          // park the high byte: the mode has to be read in A
    ldy #GP_MODE
    lda (dataPtr),y
    cmp #GP_MODE_MC
    beq gpHalfXMc
    pla                          // hires: nothing to halve
    lsr                          // C = the 9th bit
    txa                          // A = the low byte, past the shift untouched
    rts
gpHalfXMc:
    pla                          // A = the high byte
    lsr                          // C = the 9th bit
    txa                          // A = the low byte
    ror                          // (the 9th bit << 7) | (lo >> 1): the x, halved
    clc                          // 160 wide: the halved x has no 9th bit
    rts

// --- gpHalf1 -- A = a screen DISTANCE in canvas pixels ---
//  A radius measures across the canvas, so it halves for the same reason a coordinate
//  does. Hires: the canvas is the screen, so the distance passes through.
//  Clobbers A, C, Y.
gpHalf1:
    pha                          // park the distance: the mode has to be read in A
    ldy #GP_MODE
    lda (dataPtr),y
    cmp #GP_MODE_MC
    beq gpHalf1Mc
    pla
    rts
gpHalf1Mc:
    pla
    lsr
    rts

// --- gpBrush -- a brush tool's mark: the selected blob, centred on the walk's pixel ---
//  A pencil marks one pixel and a brush marks a blob. The glyphs are the 8x8 form
//  kPrintGlyphAt takes, but blitted HERE with kPlot, at arbitrary pixels. The kernel's
//  glyph blitter is cell-granular, and kGfxChar reads the one GLOBAL font (fontRec* is a
//  single 24-bit record the whole machine shares), so either would have put brush art
//  into every screen on the system rather than just this canvas.
//  kPlot is the pencil's own call, so a brush paints in the ink AND claims the cells'
//  colour, the two things the other tools have to do separately.
//  WARNING: kPlot does NOT clip, and the pixel it is handed here is the BLOB's own, up
//  to four pixels outside the pointer -- so every mark below is tested against the
//  canvas first.
//  Centred by starting four pixels up and left: that is the glyph's own middle, so the
//  three sizes grow around the pointer instead of away from it.
//  WARNING: one kPlot per SET pixel of the glyph, and gpSeg calls this once per step, so
//  the largest brush is by far the most expensive thing in this task.
gpBrush:
    asl                          // the sbc/cmp that sent us here left A = the brush
    asl                          //   number, 0..2, so the glyph is glyphs + n*8
    asl
    clc
    adc #<gpBrushGlyphs
    sta srcPtr
    lda #>gpBrushGlyphs
    adc #0
    sta srcPtr+1                 // srcPtr = the selected blob. kPlot clobbers kPtr and
                                 //   kArg, never srcPtr, so it survives the marks below
    ldy #GP_LAST                 // WARNING: park the walk's cursor. gpDot only READS
    lda (dataPtr),y              //   GP_LAST -- it is the cursor the segment walk steps
    ldy #GP_BR_SL                //   from -- so a routine called in its place must put
    sta (dataPtr),y              //   it back, or the walk resumes from the last blob
    ldy #GP_LAST + 1             //   pixel instead of where it had reached
    lda (dataPtr),y
    ldy #GP_BR_SH
    sta (dataPtr),y
    ldy #GP_LAST + 2
    lda (dataPtr),y
    ldy #GP_BR_SY
    sta (dataPtr),y
    ldy #GP_LAST                 // the blit starts four up and left of the pixel
    lda (dataPtr),y
    sec
    sbc #4
    ldy #GP_BR_X
    sta (dataPtr),y
    ldy #GP_BR_XS
    sta (dataPtr),y              // ...and the row-start copy each row reloads from
    ldy #GP_LAST + 1
    lda (dataPtr),y
    sbc #0
    ldy #GP_BR_X + 1
    sta (dataPtr),y
    ldy #GP_BR_XS + 1
    sta (dataPtr),y
    ldy #GP_LAST + 2
    lda (dataPtr),y
    sec
    sbc #4
    ldy #GP_BR_Y
    sta (dataPtr),y
    lda #0
    ldy #GP_BR_R
    sta (dataPtr),y              // the glyph's first row
gpBrRow:
    ldy #GP_BR_R
    lda (dataPtr),y
    tay
    lda (srcPtr),y               // this row's eight columns, bit 7 leftmost
    ldy #GP_BR_B
    sta (dataPtr),y
    ldy #GP_BR_N
    lda #8
    sta (dataPtr),y              // eight columns to walk
gpBrBit:
    ldy #GP_BR_B
    lda (dataPtr),y
    asl
    sta (dataPtr),y              // the next column's bit is now in the carry
    bcc gpBrNext                 // clear column: nothing to mark
    jsr gpBrClip                 // the blob's pixel, tested against the canvas: kPlot
    bcc gpBrNext                 //   does not clip, and this pixel is not the pointer's
    ldy #GP_BR_X
    lda (dataPtr),y
    ldy #GP_LAST
    sta (dataPtr),y
    ldy #GP_BR_X + 1
    lda (dataPtr),y
    ldy #GP_LAST + 1
    sta (dataPtr),y
    ldy #GP_BR_Y
    lda (dataPtr),y
    ldy #GP_LAST + 2
    sta (dataPtr),y
    jsr gpDotGo                  // the mark itself: gpDot's body, never its brush test
gpBrNext:
    ldy #GP_BR_X                 // the next column across
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    ldy #GP_BR_X + 1
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y
    ldy #GP_BR_N
    lda (dataPtr),y
    sec
    sbc #1
    sta (dataPtr),y
    bne gpBrBit
    ldy #GP_BR_XS                // the next row: back to its first column, one row down
    lda (dataPtr),y
    ldy #GP_BR_X
    sta (dataPtr),y
    ldy #GP_BR_XS + 1
    lda (dataPtr),y
    ldy #GP_BR_X + 1
    sta (dataPtr),y
    ldy #GP_BR_Y
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    ldy #GP_BR_R
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    cmp #8
    bcc gpBrRow
    ldy #GP_BR_SL                // ...and the walk's cursor back where it had reached
    lda (dataPtr),y
    ldy #GP_LAST
    sta (dataPtr),y
    ldy #GP_BR_SH
    lda (dataPtr),y
    ldy #GP_LAST + 1
    sta (dataPtr),y
    ldy #GP_BR_SY
    lda (dataPtr),y
    ldy #GP_LAST + 2
    sta (dataPtr),y
    rts

// --- gpBrClip -- is the blob's pixel on the canvas? Carry SET means it is ---
//  kPlot does NOT clip, and the pixel it is handed here is the BLOB's own, up to four
//  pixels outside the pointer. Its X = col lo / Carry = col hi pair is an ADD 256 to a
//  cell row only 320 bytes wide, so x 320..511 lands at the START of the next cell row
//  -- the far left, eight lines down -- and its matrix nibble lands a cell below; a row
//  of 200 or more indexes the 25-entry row LUT past its end. The pointer itself is
//  always on the canvas (gpClamp1), so the brush's mark is the only one that can be
//  handed a point outside it; the circle's own cells test themselves (gpCcRdot).
//  Clobbers A, Y.
gpBrClip:
    ldy #GP_BR_Y
    lda (dataPtr),y
    cmp #200
    bcs gpBrClipNo               // below the canvas, or negative and wrapped past it
    ldy #GP_BR_X + 1
    lda (dataPtr),y
    beq gpBrClipYes              // x 0..255: on the canvas
    cmp #1
    bne gpBrClipNo               // negative (a blob at the left edge), or past 511
    ldy #GP_BR_X
    lda (dataPtr),y
    cmp #64
    bcs gpBrClipNo               // 320..511: past the right edge
gpBrClipYes:
    sec
    rts
gpBrClipNo:
    clc
    rts

// --- gpBrushGlyphs -- the three brushes: one blob each, centred on the glyph ---
//  Round, and centred on the glyph's own middle, so the four-pixel blit offset puts all
//  three sizes on the same centre as the pointer.
gpBrushGlyphs:
    .byte $00, $00, $18, $3c, $3c, $18, $00, $00   // small
    .byte $00, $18, $3c, $7e, $7e, $3c, $18, $00   // medium
    .byte $3c, $7e, $ff, $ff, $ff, $ff, $7e, $3c   // large

// ==========================================
//  The UI band -- the palette along the bottom
// ==========================================

//  The band is the canvas's bottom TWO cell rows, full width, and putting it there is
//  the whole point: a bitmap's cells ARE its memory, so a cell row of the screen is a
//  CONTIGUOUS 320-byte run and the band's two are one run of 640 -- one transfer for
//  the pixels at $3CC0 and one for the matrix at $0798. Four of them cover the whole
//  right-button gesture: two to keep the band, two to put it back.
//  What goes IN is a FILE (GP_UI_FILE): the band's own 640 pixel bytes followed by its
//  80 matrix bytes, ready to drop in -- so the task composes nothing, holds no copy of
//  the band anywhere, and the REU keeps only the saved band it must have to restore.
//  WARNING: WHAT IS NOT CONTIGUOUS IS A RASTER ROW. Eight bytes are one 8x8 cell, so a
//  raster row of the band is forty bytes eight apart and a run of eight is eight raster
//  rows of one cell. That is the order the file's pixel half is in.
.assert "the UI band's pixels start at $3CC0", GP_PK_SCR0, $3cc0
.assert "the UI band's matrix starts at $0798", GP_PK_MAT0, $0798
.assert "the band is two cell rows: two 320-byte runs, one after the other", GP_PK_LEN, GP_PK_CR*GP_PK_PH
.assert "...and its matrix is the two rows' 80 bytes", GP_PK_MLEN, 80
.assert "the band's saved copy is in bank 0", GP_REU_BANK, 0
.assert "the palette fills the band's left cells in both rows", GP_PK_SW, GP_PK_PW*GP_PK_PH
//  gpPickT is the band cell row the pointer is on, times eight, while the picker works
//  the cell out: its two halves are computed at different points in gpPickSel.
gpPickT:  .byte 0

// --- gpDma -- the band's pixels or its matrix, and the copy kept in the REU ---
//  In: A = the transfer. Bit 0 is the image -- 0 the 640 pixel bytes at $3CC0, 1 the
//  80 matrix bytes at $0798 -- and bit 1 is the direction (0 the band into the REU, 1
//  the REU into the band). So the four transfers the gesture needs are 0 and 1 to keep
//  the band and 2 and 3 to put it back.
//  One routine for all four, because they differ only in the endpoints: the table
//  holds each image's C64 end, its length and its REU address, and the direction
//  picks the call. Its two halves go to different places -- the C64 end is kPtr and
//  the REU block is kArg -- so they are stored in table order rather than as a
//  block, and a row is eight bytes so the row index is one shift.
//  The parameter block is written and the call made with nothing in between, and no
//  IRQ-free window is needed for it: the block is per task (the scheduler DMAs all 8
//  bytes out on switch-out and back in on switch-in), and the timer's own display
//  change -- the F-key path -- is applied from the switch path, between that save and
//  that restore, so it cannot land in a block a running task has live. Nothing else
//  the tick does writes kArg.
//  X carries the transfer through the table lookups; the REU calls preserve it.
gpDma:
    tax
    and #1                       // the table bit: which of the band's two images
    asl
    asl
    asl                          // 8 bytes a row
    tay
    lda gpDmaImg,y
    sta kPtr
    lda gpDmaImg+1,y
    sta kPtr+1
    lda gpDmaImg+2,y
    sta kArg+3                   // the length, low byte then high
    lda gpDmaImg+3,y
    sta kArg+4
    lda gpDmaImg+4,y             // the REU address: low, high, then the bank
    sta kArg
    lda gpDmaImg+5,y
    sta kArg+1
    lda gpDmaImg+6,y
    sta kArg+2
    txa
    and #2
    beq gpDmaOut
    jsr kReuToC64                // the REU into the band
    jmp gpDmaDone
gpDmaOut:
    jsr kReuFromC64              // the band into the REU
gpDmaDone:
    rts

//  Each row: the C64 end, the length, the REU address (low, high, bank) and a spare
//  byte that keeps the row eight long.
gpDmaImg:
    .byte <GP_PK_SCR0, >GP_PK_SCR0, <GP_PK_LEN, >GP_PK_LEN, <GP_REU_SAV_PIX, >GP_REU_SAV_PIX, GP_REU_BANK, 0
    .byte <GP_PK_MAT0, >GP_PK_MAT0, <GP_PK_MLEN, >GP_PK_MLEN, <GP_REU_SAV_MAT, >GP_REU_SAV_MAT, GP_REU_BANK, 0

// --- gpUiRecFor -- kArg = the band record for mode A ---
//  A record is 5 bytes of the instance buffer, filled by kReuOpen: size lo/hi, then base
//  lo/mid/hi. GP_MODE is the tag (2 or 3), so the index is one subtraction -- and having
//  a record PER MODE is what lets the record follow the mode wherever it changes, a load
//  or a step included, with nothing to keep in step by hand.
//  Clobbers A, Y.
gpUiRecFor:
    sec
    sbc #GP_MODE_STD             // the tag -> the table index: 2/3 -> 0/1
    tay
    lda gpUiRecOfs,y
    clc
    adc dataPtr
    sta kArg
    lda dataPtr+1
    adc #0
    sta kArg+1
    rts

// --- gpUiRec -- kArg = the mode's own record, which is kReuReadAt's view of it ---
//  kReuReadAt walks the record through srcPtr, so anything needing it again re-derives
//  the pointer rather than keeping one across the call.
//  Clobbers A, Y.
gpUiRec:
    ldy #GP_MODE
    lda (dataPtr),y
    jmp gpUiRecFor

// --- gpUiName -- kArg = the band file's name for mode A ---
//  Clobbers A, Y.
gpUiName:
    sec
    sbc #GP_MODE_STD             // the tag -> the table index: 2/3 -> 0/1
    tay
    lda gpUiNameLo,y
    sta kArg
    lda gpUiNameHi,y
    sta kArg+1
    rts

// --- gpUiOpen -- resolve mode A's band file into that mode's record ---
//  kReuOpen is a DIRECTORY SCAN, and a scan is not work for a PRESS: both modes are
//  resolved here, at spawn, and nothing re-resolves afterwards. That is what makes the
//  MODE tile cheap -- its press is a whole frame's work if it has to scan, and a frame
//  the pointer is not sampled over is a press that never happened.
//  C=1 when the filesystem has no such file. The record is then whatever the spawn
//  cleared it to, which is how "this mode has no band" is read afterwards.
//  Clobbers A, X, Y, kPtr, kArg.
gpUiOpen:
    pha                          // the mode, for the record: the lookup needs A
    jsr gpUiName                 // kArg = that mode's file name
    pla
    sec
    sbc #GP_MODE_STD
    tay
    lda gpUiRecOfs,y
    clc
    adc dataPtr
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1                   // kPtr = that mode's record, in the instance buffer
    jmp kReuOpen

// --- gpUiRead -- the UI image straight out of its file, straight into the band ---
//  Two reads, because the file holds the band the way the band is addressed: 640 pixel
//  bytes and then the 80 matrix bytes. Each lands in its own home -- the band's first
//  pixel byte, then its first matrix byte -- so there is nothing to stage and nothing
//  to compose. kReuReadAt clobbers kArg+0..+7, so the record, the position and the
//  length are all set again for each call. A read can only fail if the filesystem has
//  gone since the open, and there is nothing useful to do about that here: the band
//  keeps whatever the last transfer left on it.
//  gpUiMat is the matrix half on its own, which is what a tool change re-reads: the
//  image is where the OLD tool's mark goes back to before gpMark colours the new one.
gpUiRead:
    jsr gpUiRec                  // kArg = the record
    lda #<GP_PK_SCR0
    sta kPtr
    lda #>GP_PK_SCR0
    sta kPtr+1
    lda #0
    sta kArg+2
    sta kArg+3                   // pos = 0: the pixels are the file's first half
    lda #<GP_PK_LEN
    sta kArg+6
    lda #>GP_PK_LEN
    sta kArg+7                   // 640 of them
    jsr kReuReadAt
gpUiMat:
    jsr gpUiRec                  // the call clobbered kArg, so the record comes first
    lda #<GP_PK_MAT0
    sta kPtr
    lda #>GP_PK_MAT0
    sta kPtr+1
    lda #<GP_PK_LEN
    sta kArg+2
    lda #>GP_PK_LEN
    sta kArg+3                   // pos = 640, where the matrix half starts
    lda #<GP_PK_MLEN
    sta kArg+6
    lda #>GP_PK_MLEN
    sta kArg+7                   // 80 of them
    jmp kReuReadAt

// --- gpMark -- colour the cells that carry state ---
//  Every tile carries the same border, so a tile is told apart by COLOUR rather than
//  by shape: the four cells of its 2x2 tile go white, where the image holds them grey.
//  Written on every open, after the UI image has come in, which is what stops it ever
//  having to be taken off -- the next open starts from the image.
//  TWO tiles can be marked, and they mean different things: the TOOL IN USE, and the
//  SAVE tile, drawn only while the canvas has changed since the last save -- a press
//  on an unmarked save tile does nothing at all (gpPickTakeSave). The canvas cannot
//  change while the band is up (gpDraw leaves the frame to the band), so a flag this
//  reads can only have been set since the band last came up.
//  The band's two ARROWS are marked here as well, and they mark the RING and nothing
//  else: undo is white while the canvas is not the oldest state, redo while it is not the
//  newest. A mark on the canvas that no state holds -- the line's anchor click, a stroke
//  still under the button -- is NOT a step: it colours no arrow, and both the band's own
//  opening and a step press take it off without moving the ring (gpDropMark).
gpMark:
    ldy #GP_TOOL
    lda (dataPtr),y
    jsr gpMarkTile
    ldy #GP_DIRTY
    lda (dataPtr),y
    beq gpMarkArrows             // nothing to write: no save tile to mark
    ldy #GP_WARN
    lda (dataPtr),y
    beq gpMarkSave               // no quit warning: the unsaved mark is simply ON
    ldy #GP_FLASH
    lda (dataPtr),y              // a warning flashes it: this half of the flash is dark
    beq gpMarkArrows
gpMarkSave:
    lda #GP_TOOL_SAVE
    jsr gpMarkTile               // ...and the save, while there is something to write
gpMarkArrows:
    ldy #GP_U_OLD
    lda (dataPtr),y
    ldy #GP_U_HEAD
    cmp (dataPtr),y              // the oldest state is the one on screen...
    beq gpMarkRedo               //   ...so undo has nowhere to go
    lda #GP_MK_WHITE
    ldy #0                       // the undo arrow, the band's top cell row
    jsr gpMarkArrow
gpMarkRedo:
    ldy #GP_U_NEW
    lda (dataPtr),y
    ldy #GP_U_HEAD
    cmp (dataPtr),y              // ...and the newest is the one on screen...
    beq gpMarkRts                //   ...so redo has nowhere to go
    lda #GP_MK_WHITE
    ldy #GP_PK_CW                // the redo arrow, one band cell row down
    jsr gpMarkArrow
gpMarkRts:
    rts

// --- gpFlash -- the quit warning's own frame: the save tile's half of the flash ---
//  Runs once per frame and only while the warning is on the table AND the band is up,
//  because the tile is only on screen then. The phase is one BIT of the display's frame
//  counter -- the display's own clock, never the scheduler's tick -- so the tile is dark
//  for exactly as long as it is marked.
//  A change of phase re-marks the band: the matrix is read back from the image, which is
//  where the mark goes off, and then gpMark puts the marks the state calls for back on --
//  the same three calls a tool change makes, and for the same reason. Nothing here is
//  written while another task's screen is the one displayed.
gpFlash:
    ldy #GP_WARN
    lda (dataPtr),y
    beq gpFlashRts               // no warning: nothing is flashing
    ldy #GP_MENU
    lda (dataPtr),y
    beq gpFlashRts               // the band is down: the tile is not on screen
    lda frameCount
    and #GP_FLASH_BIT            // the phase: bit 5, so 32 frames marked and 32 dark
    ldy #GP_FLASH
    cmp (dataPtr),y
    beq gpFlashRts               // the phase has not changed this frame
    sta (dataPtr),y              // it has: remember it, and re-mark
    jsr gpDgOurs
    bne gpFlashRts               // not our surface: those bytes are not the band
    jsr gpUiMat
    jsr gpChips
    jmp gpMark
gpFlashRts:
    rts

// --- gpMarkArrow -- one arrow's two cells white ---
//  In: A = the mark's colour byte, Y = the arrow's band cell row: 0 the undo, at the
//  band's top, and GP_PK_CW the redo. An arrow is TWO cells wide, so it is the pair of
//  matrix bytes that starts at its own row's first cell.
gpMarkArrow:
    sta GP_PK_ICON_M,y
    sta GP_PK_ICON_M+1,y
    rts

// --- gpMarkTile -- colour tile A's four cells white ---
//  A tile is two cells across, so its first cell is two in from the palette's edge,
//  and a cell row is forty bytes of matrix.
gpMarkTile:
    asl                          // two cells across
    clc
    adc #<GP_PK_MAT0+GP_PK_PW    // past the band's first byte and the palette's cells
    sta kPtr
    lda #>GP_PK_MAT0             // the band's matrix stays inside $07xx
    adc #0                       // the carry that add may have left
    sta kPtr+1
    ldx #2                       // the tile's two cell rows
gpMarkRow:
    ldy #0
    lda #GP_MK_WHITE             // white
    sta (kPtr),y                 // the tile's first cell...
    iny
    sta (kPtr),y                 // ...and its second
gpMarkNext:
    lda kPtr
    clc
    adc #GP_PK_CW                // 40: the next cell row of the matrix
    sta kPtr
    bcc gpMarkDown
    inc kPtr+1
gpMarkDown:
    dex
    bne gpMarkRow
    rts

// --- gpPickShow -- put the palette up ---
//  The band's own bytes go out to the REU first, so what comes back on the way out is
//  the picture as it was; then the UI's own image is read in over them, out of its file.
//  Two transfers each way: 640 pixel bytes and 80 matrix bytes.
gpPickShow:
    lda #0                       // the band into its saved copy...
    jsr gpDma
    lda #1
    jsr gpDma
    jsr gpUiRead                 // ...and the UI's own image over it
    jsr gpChips                  // the chips carry RUNTIME colour, so the image alone is
                                 //   not the band: it has the file's placeholders
    jmp gpMark                   // with the tool in use coloured

// --- gpPickHide -- put the band back the way it was ---
gpPickHide:
    lda #2
    jsr gpDma
    lda #3
    jmp gpDma                    // ...and its matrix

// --- gpCram -- the colour RAM, with the palette's third source ---
//  In MC value 3 shows this byte, so it belongs with the matrix fill and nothing else
//  writes it. Hires never reads it, so the call is MC's alone. In A = the colour.
gpCram:
    ldy #GP_MB+3
    lda (dataPtr),y              // c3
    jmp kFillColorRam

// --- gpCells -- every cell of the canvas, from the palette that applies now ---
//  A cell's byte is what its two pixel VALUES show. Hires carries TWO per-cell colours, so
//  the high nibble is the ink and the low one the paper -- what a clear pixel shows, which
//  is NOT $D021 (VIC-II 3.7.3.3, and the byte img2pic.py writes); MC has three sources, so
//  it is (c1<<4)|c2 with c3 in the colour RAM. A fresh canvas, a clear and a mode toggle all
//  fill from here, which is what keeps their three answers the same one.
//  Clobbers A, X, Y, kPtr, kArg (kFillMatrix and kFillColorRam own the whole block).
gpCells:
    ldy #GP_MODE
    lda (dataPtr),y
    cmp #GP_MODE_MC
    beq gpCellsMc
    ldy #GP_INK
    lda (dataPtr),y
    asl
    asl
    asl
    asl                          // (the ink << 4) ...
    ldy #GP_MB
    ora (dataPtr),y              // ...| the paper, which in hires is the cell's LOW
    jmp kFillMatrix              //   nibble: what a clear pixel shows
gpCellsMc:
    jsr gpCram                    // value 3 first: kFillMatrix clobbers kArg, not this
    ldy #GP_MB+2
    lda (dataPtr),y              // c2
    sta gpPickT                  // parked for the OR: gpPickT is the picker's scratch
    ldy #GP_MB+1
    lda (dataPtr),y              // c1
    asl
    asl
    asl
    asl
    ora gpPickT
    jmp kFillMatrix              // (c1 << 4) | c2

// --- gpChips -- the four slot chips' own colour, in the band's matrix ---
//  A chip is ONE cell whose picture is its pixel values: value 1 is the solid block and
//  value 2 the frame around it. In MC a cell's byte is (value 1's colour << 4) | value
//  2's, so the block shows the slot's own colour and the frame a fixed one -- which is
//  what makes all four readable at a glance, the background slot included.
//  With the marks, these four bytes are the ONLY runtime writes into the band's matrix.
//  Hires leaves the cells alone: those four columns are empty band there, and a colour
//  written into an empty cell shows nothing.
//  Clobbers A, X, Y.
gpChips:
    ldy #GP_MODE
    lda (dataPtr),y
    cmp #GP_MODE_MC
    bne gpChipsRts
    ldy #GP_SLOT
    lda (dataPtr),y
    sta GP_CHIP_SEL              // which chip a swatch press assigns to: the ring is what
                                 //   makes that visible, and without it the picker's
                                 //   target cannot be seen at all
    ldy #GP_MB
    ldx #0
gpChipLp:
    lda (dataPtr),y
    asl
    asl
    asl
    asl                          // the block's colour, which is the cell's HIGH nibble
    sta gpPickT                  // parked: the ring is chosen next
    lda #0                       // nothing on a chip that is not the selected one...
    cpx GP_CHIP_SEL
    bne gpChipRing
    lda #GP_CHIP_FRAME           // ...and the ring on the one that IS, so the slot a
                                 //   swatch press would assign to can be seen
gpChipRing:
    ora gpPickT
    sta GP_PK_CHIP_M,x
    iny
    inx
    cpx #GP_PK_CN
    bne gpChipLp
gpChipsRts:
    rts

// --- gpPicker -- the right button TOGGLES the band; the left one selects from it ---
//  The band is not a press-slide-release gesture: a right press puts it UP and it
//  stays up, the pointer following it, until something puts it away again -- another
//  right press, or a left press with the pointer OFF the band's entries. A left press
//  on an entry is the selection and leaves the band up, so a colour and then a tool
//  can be taken one after the other without reopening it in between.
//  Nothing here draws. While the band is up the left button belongs to it, so a
//  selection can never also mark the canvas (gpDraw leaves the frame to the band),
//  and a stroke in progress keeps the band down, so the band can never be drawn over.
gpPicker:
    ldy #GP_MENU
    lda (dataPtr),y
    bne gpPickBand               // up: this frame belongs to the band
    //  --- the pencil draws wherever the pointer goes, so the band may not open over
    //      a stroke in progress. A shape tool leaves nothing in the canvas for the
    //      band to be drawn over: a line holds an anchor and no pixels, and a
    //      rectangle's live band is taken off first, by gpPickOpen's gpDgCancel ---
    ldy #GP_TOOL
    lda (dataPtr),y
    cmp #GP_TOOL_PEN
    bne gpPickRead
    ldy #GP_DRAW
    lda (dataPtr),y
    bne gpPickRts                // a stroke is in progress: the right button waits
gpPickRead:
    //  --- the right button's PRESS: down now, up on the frame before. A LEVEL cannot
    //      tell a press from a button already held, and a toggle is made of exactly
    //      that difference. The mask is the one button: MOUSE_READ hands back the whole
    //      port, and the other lines it uses move with the mouse ---
    ldy #GP_BTN
    lda (dataPtr),y
    and #GP_BTN_RIGHT
    bne gpPickRts                // released: no press to read
    ldy #GP_PREV
    lda (dataPtr),y
    and #GP_BTN_RIGHT
    beq gpPickRts                // ...and it was up last frame: this frame is the press
    jmp gpPickOpen               // the press puts the band up
gpPickRts:
    rts

// --- gpPickBand -- one frame with the band up: the pointer, then the pair of presses ---
//  The entry under the pointer is taken FIRST, from THIS frame's position, and only then
//  is a press acted on. GP_MSEL is otherwise whatever the frame before sampled, and the
//  pointer can cross the band, and the canvas, between two frames -- the 1351 reports up
//  to 248 pixels of travel in one -- so a press judged against the stale entry picks from
//  a part of the band the pointer has already left. That is the shape a drag comes in: a
//  flick off the tile and down onto the canvas, which is exactly the frame the press
//  lands on. It took the tile again and left the band UP, so the drag the press was meant
//  to start never began and the outline only arrived on the press after it.
//  Both buttons are read here because both dismiss the band, and one frame can only hold
//  one decision. The press mask is "down now, up on the frame before" for the two button
//  lines alone -- the same edge the shapes make, done in one pass.
gpPickBand:
    jsr gpPickSel                // the entry under the pointer, positioned now
    ldy #GP_BTN
    lda (dataPtr),y
    eor #$ff                     // the button lines that are DOWN now...
    ldy #GP_PREV
    and (dataPtr),y
    and #(GP_BTN_LEFT | GP_BTN_RIGHT)   // ...and were UP on the frame before: the presses
    beq gpPickBandRts            // neither button: the pointer has had this frame
    lsr                          // bit 0 into the carry: the RIGHT button's press
    bcc gpPickBandLeft           // the left: it picks from the band (gpPickTake)
    jmp gpPickAway               // the right: the band goes, taking nothing with it
                                 // A jmp rather than a branch: the press handlers sit
                                 // between here and gpPickAway, and a 6502 branch
                                 // cannot reach that far. A jmp has no reach to run out of.
gpPickBandLeft:
    jmp gpPickTake
gpPickBandRts:
    rts

// --- gpPickOpen -- the band out of the way, then the palette in ---
gpPickOpen:
    lda kbFocus
    cmp curTask
    bne gpPickRts                // not our surface: the framebuffer is not ours
    jsr gpTextEnd                // the pen is put away: its caret is an XOR on our canvas
    jsr gpDgCancel               // a drag in progress comes off before the band goes up
    //  --- and a LINE's waiting anchor comes off with them. It is a mark and not a step,
    //      and it must not outlive the band's arrival: the arrows mark the RING alone, so
    //      a canvas that had run ahead of that state would be one whose arrows describe
    //      something else. The LINE is the only tool that can leave such a mark behind --
    //      a pencil's or a brush's dots belong to a stroke whose button is still DOWN, and
    //      that is not abandoned here, while a rectangle's live preview is gpDgCancel's --
    //      so the tool is the test, and it costs no second flag ---
    ldy #GP_TOOL
    lda (dataPtr),y
    cmp #GP_TOOL_LINE
    bne gpPickOpenShow
    jsr gpDropMark
gpPickOpenShow:
    jsr gpPickShow
    ldy #GP_MENU
    lda #1
    sta (dataPtr),y
    ldy #GP_MSEL
    lda #$ff
    sta (dataPtr),y              // nothing is under the pointer yet
    jmp gpPickSel                // preview whatever the pointer is already on

// --- gpPickTake -- the left press on the band: take the entry under the pointer ---
//  GP_MSEL is the entry. Off the entries it holds $ff, and a press there is the one
//  thing that puts the band away besides the right button -- a click outside the band
//  is how the band is dismissed without reaching for the other button.
//  A press ON an entry leaves it UP: the pick is shown where the eye is -- the pointer
//  for a colour, the tile's own colour for a tool -- and the band then stays for the
//  next pick, which is what makes setting a colour and a tool one gesture.
//  The CLEAR tile is the exception, and it is not a tool at all: it is a COMMAND, run
//  on the press that picks it (gpPickTakeClear).
gpPickTake:
    ldy #GP_MSEL
    lda (dataPtr),y
    cmp #GP_PK_ENT
    bcc gpPickTakeEntry
    jmp gpPickAway               // past the entries: nothing to take, so the band goes
                                 // A jmp rather than a branch: the press handlers sit
                                 // between here and gpPickAway, and a branch cannot reach
gpPickTakeEntry:
    cmp #GP_PK_TLINE
    bcc gpPickTakeInk            // 0-15: a swatch, and the entry IS the colour
    cmp #GP_PK_CHIP0
    bcc gpPickTakeTiles
    cmp #GP_PK_ICON0
    bcc gpPickTakeChipJmp        // 29-32: a slot chip -- a value, and its target
    jmp gpUndoTake               // 33-34: an ARROW -- a command, run on the press
                                 // Both go by jmp: the press handlers below sit between
                                 // this test and their own code, and the history sits at
                                 // the end of the file, so neither is in branch range
gpPickTakeChipJmp:
    jmp gpPickTakeChip
gpPickTakeTiles:
    //  --- a tile, whose entry number IS its tool number (see GP_PK_TLINE) ---
    sec
    sbc #GP_PK_TLINE
    cmp #GP_TOOL_CLEAR
    beq gpPickTakeClear          // the clear runs now rather than being a mode
    cmp #GP_TOOL_MODET
    bne gpPickTakeSaveChk
    jmp gpPickTakeMode           // ...and so does the MODE tile: the other command
gpPickTakeSaveChk:
    cmp #GP_TOOL_SAVE
    bne gpPickTakeTool
    jmp gpPickTakeSave           // ...and so does the save
    //  A jmp rather than a branch: this handler is far enough down the file that a
    //  6502 branch cannot reach it, and a jmp has no reach to run out of.
gpPickTakeTool:
    ldy #GP_TOOL
    sta (dataPtr),y
    //  --- and a half-made line is abandoned with the change: GP_DRAW is the anchor
    //      the line tool is waiting on, and the pencil would read it as a stroke in
    //      progress and draw a segment nobody asked for ---
    ldy #GP_DRAW
    lda #0
    sta (dataPtr),y
    //  --- the tile in use is told apart by COLOUR, so the change has to show on the
    //      spot: the band's matrix goes back to the UI image, which is what takes the
    //      OLD mark off, and gpMark puts the new one on. Both write the band, so both
    //      make the test gpPickAway makes -- the band can be up while another task's
    //      screen is displayed, and those bytes are then that screen's ---
    jsr gpDgOurs
    bne gpPickTakeRts
    jsr gpUiMat
    jsr gpChips                  // ...the chips' runtime colour with it (gpChips)
    jmp gpMark
gpPickTakeRts:
    rts

// --- gpPickTakeClear -- the CLEAR tile: blank the canvas, on the press ---
//  There is nothing to set up and nothing to keep, so the clear is not a tool: the tile
//  is never "in use", and the tool and the ink are left exactly as they were -- the tile
//  blanks the canvas and nothing else, so the next press draws in the same ink with the
//  same tool rather than having a fresh page's defaults forced on it.
//  kClearBitmap takes A = the pixel fill byte and zeroes the video matrix with it, and
//  IT IS THE MATRIX THAT NEEDS PUTTING BACK: in hires a cell's colour IS its matrix
//  nibble, so at $00 every set pixel is drawn in colour 0 -- the blank page and anything
//  drawn on it are the same black, and the drag's live outline, which writes pixels and
//  no matrix, cannot be seen at all except in the cells a walk has coloured since, which
//  is what a half-shown rectangle looks like. gpCanvas fills this same byte at spawn for
//  the same reason: a page whose cells are not the ink cannot show a drawn pixel.
//  It writes whichever of the two surfaces is ours, so the clear needs no test.
//  The palette is written back over the blank rows afterwards, because the band's own
//  rows were part of what was blanked and the band stays up. WARNING: it holds SEI for
//  the whole clear.
gpPickTakeClear:
    //  --- a half-made line is abandoned by the clear: GP_DRAW is the anchor the line tool
    //      is waiting on, and the canvas that anchor was placed on is gone. A run of lines
    //      starts again from the next click ---
    ldy #GP_DRAW
    lda #0
    sta (dataPtr),y
    lda #0
    jsr kClearBitmap             // the canvas, blank
    ldy #GP_DIRTY                // ...and kClearBitmap is the other one: it writes no
    lda #1                       //   pixel through gpDot either
    sta (dataPtr),y
    //  --- and the cells back to the palette, which is the half a clear does not do ---
    //  gpCells is the one place that knows a cell's byte in either mode, and it takes no
    //  argument: it reads the palette itself
    jsr gpCells                  // ...the byte gpCanvas and the walks both write
    jsr gpDgOurs
    beq gpPickTakeClearShow      // our surface: the palette goes back over the blank rows
    jmp gpPickAway               // not ours: the band goes with the clear, since the
                                 //   palette cannot be written into that screen
                                 // A jmp rather than a branch: the press handlers sit
                                 // between here and gpPickAway, and a branch cannot reach
gpPickTakeClearShow:
    //  --- and the clear is one step of the history ---
    //  The band comes down for the snapshot and back up after it: its own two rows hold
    //  the palette, not the picture, while it is up (gpUndoPushBand).
    jmp gpUndoPushBand

gpPickTakeInk:
    ldy #GP_MODE
    lda (dataPtr),y
    cmp #GP_MODE_MC
    beq gpPickInkMc
    ldy #GP_MSEL
    lda (dataPtr),y
    ldy #GP_INK
    sta (dataPtr),y              // hires: the swatch IS the colour, 0-15 is the palette
    jmp gpPickColour             // ...and the pointer's interior shows it
gpPickInkMc:
    //  --- in MC a swatch is a PALETTE ENTRY, not a pen: the sixteen colours are the
    //      cell's four sources, so the press assigns the colour to the SELECTED SLOT and
    //      that slot's chip takes the colour. The pen's VALUE does not move -- a colour
    //      and a value are two different decisions, and picking one must not silently
    //      change the other ---
    ldy #GP_MSEL
    lda (dataPtr),y
    tax                          // parked: the slot index is needed next, and no call runs
    ldy #GP_SLOT
    lda (dataPtr),y
    clc
    adc #GP_MB
    tay                          // Y = the selected slot's palette entry
    txa
    sta (dataPtr),y              // the slot takes the colour
    //  --- and SLOT 0 is the hardware background. In MC value 0 selects $D021, which is a
    //      VIC register and not a matrix nibble, so gpCells cannot carry it: a pen on
    //      value 0 would keep the black the surface started with however often slot 0 was
    //      re-sworn. kSetGfxColor writes the task's own copy AND the hardware while we own
    //      the display, and the same colour goes into the header's palette byte so a save
    //      carries it. Slots 1-3 need none of that: their colours are the matrix and the
    //      colour RAM, which gpCells writes ---
    ldy #GP_SLOT
    lda (dataPtr),y
    bne gpPickInkCells           // not slot 0: the matrix already carries its colour
    ldy #GP_MB
    lda (dataPtr),y              // bg0, the colour just assigned to it
    ldy #GP_HDR + 13             // the header's palette: border, bg0, bg1, bg2
    sta (dataPtr),y
    ldx #1                       // selector 1 = bg0
    jsr kSetGfxColor             // WARNING: clobbers X -- it does ldx curTask
gpPickInkCells:
    lda #1
    ldy #GP_DIRTY
    sta (dataPtr),y              // the file's matrix is not what a save would find
    jsr gpCells                  // the picture re-colours: in MC a cell's byte IS the
                                 // palette, so every cell showing that value takes the
                                 // new colour and not one pixel moves
    //  --- and the BAND back over it. The band's own two rows are canvas cells like any
    //      others, so that fill has just re-coloured the palette and the tiles as well:
    //      the image is what puts them back, exactly as a tool change does ---
    jsr gpDgOurs
    bne gpPickInkMcRts           // not our surface: the band is not ours to rewrite
    //  --- the band, and then the press as one step ---
    //  The whole matrix was just refilled from the palette, the band's own two rows
    //  included. gpUndoPushBand takes the band down for the snapshot -- its rows are the
    //  canvas's bottom two, and while it is up they hold the palette -- and gpPickShow
    //  puts the image, the chips and the marks back over the state that arrived.
    jmp gpUndoPushBand
gpPickInkMcRts:
    rts

// --- gpPickTakeChip -- a slot chip: which source a mark claims, and where a colour goes ---
//  A chip press makes its slot CURRENT: it is both the palette entry a swatch press
//  assigns and the value a mark claims -- one index for both, because a pixel's value N
//  IS the cell's source N (0 the background, 1-3 its three colours).
//  In HIRES those four columns are empty band, so the hit test offers no entry there
//  (gpPickSelChip) and a press is a press off the UI -- and this refuses as well, so a
//  chip press that ever reached here in hires would still take nothing.
gpPickTakeChip:
    ldy #GP_MODE
    lda (dataPtr),y
    cmp #GP_MODE_MC
    bne gpPickTakeChipRts
    ldy #GP_MSEL
    lda (dataPtr),y
    sec
    sbc #GP_PK_CHIP0
    ldy #GP_SLOT
    sta (dataPtr),y
    ldy #GP_VAL
    sta (dataPtr),y
    //  --- and the RING moves with the selection. The chips are the band's only cells
    //      whose colour is runtime state, and the ring on one of them is the only thing
    //      that shows which slot a swatch press would assign to -- so it is redrawn here
    //      rather than on the next open (which is where it used to arrive). The band is
    //      written only while our surface is the one on screen, the rule every band
    //      writer follows: those bytes are somebody else's otherwise ---
    jsr gpDgOurs
    bne gpPickTakeChipRts
    jsr gpChips
    jmp gpPickColour             // the interior shows the slot that is now current
gpPickTakeChipRts:
    rts

// --- gpPickTakeMode -- the MODE tile: the other mode, in place ---
//  kSetGfxMode keeps the pixels, the matrix, the colour RAM and the palette and changes
//  only the tag, so the picture survives as PIXELS -- which is what makes a mode a tag
//  rather than a conversion. The CELLS do not survive: a hires cell holds (ink<<4) in one
//  nibble where MC needs (c1<<4)|c2 plus colour RAM, so the toggle REFILLS them from the
//  palette that now applies, in both directions. A loaded picture's per-cell colours are
//  lost by that -- the documented price of the toggle; reloading the file brings them back.
//  The band's art is a file per mode, and the record for the mode in hand is what the
//  read uses (gpUiRec), so the press neither resolves nor reads anything: the tile was
//  pressed from the band, so the band stays UP and shows the face for the mode just
//  arrived at. A mode with no band file is refused outright above.
gpPickTakeMode:
    jsr gpDgOurs
    bne gpPickTakeModeRts        // not our surface: nothing may be written into it
    ldy #GP_MODE
    lda (dataPtr),y
    eor #1                       // 2 <-> 3: hires <-> multicolour -- the mode this goes TO
    //  --- and the mode does not switch without the band file for it. Both records were
    //      resolved at spawn, so this is a size test and no scan: re-resolving here would
    //      put a directory scan inside the press, and a press the pointer is not sampled
    //      over is a press that never happened ---
    pha                          // the target mode, parked across the test
    sec
    sbc #GP_MODE_STD
    tay
    lda gpUiRecOfs,y
    tay
    lda (dataPtr),y              // the target record's size lo: 0 = there is no band file
    beq gpPickTakeModeNo
    pla
    ldy #GP_MODE
    sta (dataPtr),y              // our own tag first: everything below reads it
    ldy #GP_HDR + 5
    sta (dataPtr),y              // ...and the header's own mode byte, which is what a
                                 //   save writes into the file: without this a picture
                                 //   saved in MC would carry the hires tag and read back
                                 //   as garbage. The rest of the header's palette layout
                                 //   is a load's business (step 4.13.5)
    ldy #GP_MODE
    sec
    sbc #GP_MODE_STD             // -> 0 / 1, which is what the call takes
    jsr kSetGfxMode
    jsr gpCells                  // the cells, from the palette that now applies
    lda #1
    ldy #GP_DIRTY
    sta (dataPtr),y              // the cells are not what a save would find in the file
    //  --- and the mode is one step of the history ---
    //  gpUndoPushBand takes the band down for the snapshot and draws it back after it,
    //  which is also what reads the face for the mode just arrived at in -- and the band
    //  is one FILE per mode, whose record the mode itself selects (gpUiRec) ---
    jmp gpUndoPushBand
gpPickTakeModeNo:
    pla                          // the parked mode, discarded: the stack must balance
    rts
gpPickTakeModeRts:
    rts

// --- gpPickAway -- the band away, taking nothing, with the ink back in the pointer ---
//  The right button's dismissal, and the left button's when it lands off the entries --
//  which is the click that starts using what was just picked, so nothing is spent here:
//  gpDraw runs later in the same frame and reads GP_PREV to tell a fresh press from a
//  held button, and this press IS fresh and IS meant to land on the canvas.
//  Reached only with the band UP (both callers read GP_MENU first), and the band's own
//  bytes are only written back while our surface is the one on screen: a hide would put
//  the saved copy into a framebuffer that is not ours, so the state alone is dropped
//  and the saved copy stands until we are visible again.
//  The pointer's interior goes back to the ink on the way out: the swatch it may have
//  been previewing is not under it in any sense.
gpPickAway:
    lda kbFocus
    cmp curTask
    bne gpPickAwayDown           // the display moved away: the saved band stands
    jsr gpPickHide
gpPickAwayDown:
    ldy #GP_MENU
    lda #0
    sta (dataPtr),y              // the band is down
    jmp gpPickColour             // the interior goes back to the ink

// --- gpPickTakeSave -- the SAVE tile: write the picture, if there is anything to write ---
//  Reached by a jmp (see gpPickTake) and placed clear of the dispatcher's own branches:
//  a press handler wedged between them pushes gpPickAway out of a 6502 branch's reach.
//  The tile is DRAWN only while the canvas has changed since the last save (gpMark), so
//  a press on an unmarked one has nothing to do -- and that IS the whole of the
//  feedback: there is no message on success, because the mark going out says it.
//  Nothing here runs without the display. The band is up only while we own it, but a
//  picture written out of a framebuffer that is somebody else's would be their screen.
gpPickTakeSave:
    ldy #GP_DIRTY
    lda (dataPtr),y
    beq gpPickTakeSaveRts        // nothing has changed: the tile does nothing at all
    jsr gpDgOurs
    bne gpPickTakeSaveRts        // not our surface: those pixels are not the picture
    jsr gpPickAway               // the band down, so what is written is the picture
    jsr gpSave
    bcs gpSaveFailed             // C=1: nothing was written -- say so, and KEEP the canvas
    lda #0
    ldy #GP_DIRTY
    sta (dataPtr),y              // saved: nothing to write until the canvas changes
    ldy #GP_WARN
    sta (dataPtr),y              // ...and the quit warning is answered
    ldy #GP_MENU
    lda #1
    sta (dataPtr),y              // ...and the band back up
    jmp gpPickShow               // re-drawn, with the save tile's mark now gone
gpPickTakeSaveRts:
    rts

// --- gpSaveFailed -- the reason on the band's rows, the canvas KEPT ---
//  A failed save must not take the picture with it. The cause is almost always the
//  filesystem -- full, or off -- and the canvas is the only copy of the work, so the
//  task stays up with its drawing intact and the SAVE tile still MARKED (gpMark draws
//  it while GP_DIRTY is set), which is both "still unsaved" and the press that
//  retries. The reason goes over the band's own two rows: UI real estate, which the
//  image is simply read back into.
//  The band comes back UP first, and its gpPickShow is what saves the CANVAS's rows --
//  so printing here can never reach the picture, and the wipe below is the image read
//  again rather than a second gpPickShow, which would save the MESSAGE as the canvas's
//  rows and hand it back when the band next closed.
gpSaveFailed:
    ldy #GP_MENU
    lda #1
    sta (dataPtr),y              // the band up: the tile is pressed again from it
    jsr gpPickShow
    ldx #1
    ldy #GP_PK_ROW               // its first cell row (kSetCursor takes col, then row)
    jsr kSetCursor               // the one kernel routine that preserves X and Y
    lda #<gpMsgSave
    ldx #>gpMsgSave
    jsr kPrintString             // WARNING: clobbers kArg -- nothing is live here
    jsr kWaitKey
    jsr gpUiRead                 // the image back over the message...
    jsr gpChips                  // ...the chips' runtime colour with it...
    jsr gpMark                   // ...and the marks with it, the save tile's included
    rts

// --- gpPickColour -- the pointer's interior carries the ink ---
//  The interior, NOT the ring: the ring is the pointer's outline, and ink 0 is the
//  canvas's own background, so an ink-coloured outline would erase itself. The ring
//  keeps the fixed colour gpInit gave it. A sprite setter is display-owner guarded, so
//  a call made while another task's screen is up is refused -- the ink itself is
//  already stored, and the interior catches up the next time it is set.
gpPickColour:
    ldy #GP_MODE
    lda (dataPtr),y
    cmp #GP_MODE_MC
    beq gpPickColMc
    ldy #GP_INK
    lda (dataPtr),y              // hires: the interior is the ink's own readout
// --- gpPickSetFill -- the pointer's interior, in A (a palette colour 0-15) ---
gpPickSetFill:
    ldx #SPR_SHD_COLOUR + GP_SL_FILL
    jmp kSpriteReg
gpPickColMc:
    //  --- in MC the ink is a VALUE and not a colour, so what the interior can show is
    //      the colour of the slot a mark claims -- the palette's entry for it ---
    ldy #GP_SLOT
    lda (dataPtr),y
    clc
    adc #GP_MB
    tay
    lda (dataPtr),y
    jmp gpPickSetFill

// --- gpPickSel -- which band entry the pointer is on, and the pointer's preview ---
//  NOTHING in the band is written. A swatch has to keep showing the colour it
//  selects, and in hires a cell's colour IS its matrix nibble -- so any highlight
//  drawn in the band draws over the colour it is marking. The feedback is the
//  pointer instead: its interior takes the swatch's colour while the pointer is on
//  the palette, and the current ink everywhere else -- including over a tool tile,
//  which has no colour of its own to show. GP_MSEL carries the entry the left press
//  takes: 0-15 a colour, GP_PK_TLINE and up a tool, $ff "not an entry".
//  The band is two cell rows, so an entry is a cell ACROSS plus a row's worth of
//  cells -- and the tiles that follow the palette are two cells across, so a tile is
//  that distance halved. Either row of a tile lands on the same tile.
gpPickSel:
    ldy #GP_POS + 2
    lda (dataPtr),y              // the canvas row
    cmp #GP_PK_SY0               // 184: the band's first row
    bcc gpPickSelOff
    cmp #GP_PK_SY0 + GP_PK_SH
    bcs gpPickSelOff             // ...and the band is two cell rows tall
    sec
    sbc #GP_PK_SY0
    lsr
    lsr
    lsr                          // the cell row within the band, 0-1
    asl
    asl
    asl                          // eight swatches a row
    sta gpPickT
    //  --- the column: the band is full, so past x = 256 every cell is an entry -- a
    //      tile, a chip or an arrow. The cell across is the x DIVIDED BY EIGHT, so it
    //      needs BOTH of the x's bytes: the low byte alone is right only to x = 255, and
    //      past that it wraps to cell 0 -- so a press on the MODE tile would quietly
    //      take a swatch instead (colour 0, nothing to see). The x's HIGH byte is the
    //      half's own marker, and GP_PK_HALF is the cell column x = 256 stands at. ---
    ldy #GP_POS + 1
    lda (dataPtr),y
    beq gpPickSelLo              // x < 256: cells 0-31, the palette and the first tiles
    cmp #1
    bne gpPickSelOff             // over the canvas width: impossible, but exact
    ldy #GP_POS
    lda (dataPtr),y
    lsr
    lsr
    lsr                          // the cell across within that half, 0-7
    clc
    adc #GP_PK_HALF              // 32: the cell column x = 256 stands at
    jmp gpPickSelCells           // ...and no row term: 32 and up is a tile, a chip or
                                 //   an arrow
gpPickSelLo:
    ldy #GP_POS
    lda (dataPtr),y              // the column's low byte, known now to be below 256
    lsr
    lsr
    lsr                          // the cell across, 0-31
    cmp #GP_PK_PW
    bcs gpPickSelCells
    clc
    adc gpPickT                  // a swatch: its column, plus eight a row
    jmp gpPickSelEntry
gpPickSelCells:
    sec
    sbc #GP_PK_PW                // the cell column past the palette, 0-29
    cmp #GP_PK_TC                // 26: the tiles' own cells
    bcs gpPickSelChip
    lsr                          // two cells a tile
    clc
    adc #GP_PK_TLINE
    jmp gpPickSelEntry
    //  --- the chips and the two arrows: the band's ONE-CELL-ROW entries, so the tile
    //      arithmetic's halving does not apply and the column past the tiles IS the
    //      entry. The four chips come first, then the STACKED PAIR -- undo across the
    //      band's top cell row and redo across the bottom one, either of them two cells
    //      wide, so which one a press takes is the ROW and not the column ---
gpPickSelChip:
    sec
    sbc #GP_PK_TC
    cmp #GP_PK_CN                // the chips' own count
    bcs gpPickSelArrow
    clc
    adc #GP_PK_CHIP0             // the chip's entry
    //  --- IN HIRES THOSE FOUR COLUMNS ARE EMPTY BAND: nothing is DRAWN there, so a press
    //      on one is a press off the UI and puts the band away like any other. An entry
    //      with nothing on it is not an entry, and a press that silently took nothing was
    //      a band that would not close ---
    pha                          // the entry, parked across the mode test
    ldy #GP_MODE
    lda (dataPtr),y
    cmp #GP_MODE_MC
    beq gpPickSelChipMc
    pla                          // hires: nothing is drawn there, so the entry is dropped
    jmp gpPickSelOff
gpPickSelChipMc:
    pla                          // MC: the chips are drawn, so this is a slot pick
    jmp gpPickSelEntry
gpPickSelArrow:
    lda gpPickT                  // the band's cell row, times eight
    lsr
    lsr
    lsr                          // 0 = the top row, 1 = the bottom: undo, then redo
    clc
    adc #GP_PK_ICON0
gpPickSelEntry:
    ldy #GP_MSEL
    cmp (dataPtr),y              // against the entry that was under the pointer
    beq gpPickSelRts             // unchanged: the preview is already right
    sta (dataPtr),y              // the release takes this one
    cmp #GP_PK_TLINE             // an entry from here UP is a TOOL or a chip. The bound
    bcc gpPickSelInk             //   is the FIRST tile, not the end of the entries: a
    jmp gpPickColour             //   bound at GP_PK_ENT would fall through to the swatch
                                 //   path and set the sprite's colour to the ENTRY
                                 //   NUMBER -- 16 and up, which no colour register
                                 //   holds. A swatch IS its colour, so only the
                                 //   palette's own 0-15 falls here.
                                 // A jmp: gpPickColour is behind this and out of reach
gpPickSelInk:
    jmp gpPickSetFill
gpPickSelOff:
    ldy #GP_MSEL
    lda (dataPtr),y
    cmp #GP_PK_ENT
    bcs gpPickSelRts             // off the entries, and nothing was previewed
    lda #$ff
    sta (dataPtr),y              // not an entry: the release takes nothing
    jmp gpPickColour             // ...and the interior goes back to the ink
gpPickSelRts:
    rts

//  The pointer's interior takes the ink (gpInit), and the picker keeps it there.
gpPointerFile:
    .text "pointer.spr"
    .byte 0
//  The UI band, as pixels then matrix: 640 + 80 bytes, exactly what the band holds.
//  ONE FILE PER MODE, chosen by GP_MODE: the tiles' ART is what differs -- the MODE tile
//  reads "to MC" in the hires band and "to hires" in the MC one, and only the MC band
//  carries the chips (in hires those four columns are empty band and the hit test offers
//  no entry for them). Built by tools/gen-ui.py from
//  assets/ui/gpui.txt and seeded into the REU FS by tools/build-reu.ps1.
gpUiNameLo:  .byte <gpUiFileStd, <gpUiFileMc
gpUiNameHi:  .byte >gpUiFileStd, >gpUiFileMc
//  ...and the pair of records the two names resolve into, in the same order.
gpUiRecOfs:  .byte GP_UI, GP_UI_MC
gpUiFileStd:
    .text "gpui.bin"
    .byte 0
gpUiFileMc:
    .text "gpui_mc.bin"
    .byte 0

// ==========================================
//  The picture file -- "<name>.pic"
// ==========================================
//  The format is a FIXED 10,032 bytes, uncompressed and mode-tagged, so both ends are
//  a DMA of a fixed run at a fixed offset and neither needs a decoder. The spec is
//  docs/pic-format.md, tools/img2pic.py writes it and
//  grview reads it -- so a save here is "produce the file the viewer already reads".
//  THE PAYLOAD IS IN THE DISPLAY'S OWN ORDER -- 8000 pixel bytes, then 1000 matrix,
//  then 1000 colour RAM -- which is what lets a load go straight to $2000 / $0400 /
//  $D800 while the canvas is the surface on screen, and a save read from exactly there.
//  Nothing is staged anywhere: 10,032 bytes fit nowhere in this task.
//  GPIC_ prefix throughout: the task build assembles every task into ONE namespace,
//  and grview.inc already owns PIC_BYTES, PIC_PIX_POS and the rest of that set.
.const GPIC_BYTES    = 10032     // the whole file
.const GPIC_VERSION  = 1
.const GPIC_MODE     = 2         // BMPMODE_STD: the MODE the table's header carries
.const GPIC_HDR_LEN  = 32
.const GPIC_PIX_POS  = 32
.const GPIC_PIX_LEN  = 8000
.const GPIC_MAT_POS  = 8032
.const GPIC_MAT_LEN  = 1000
.const GPIC_CRAM_POS = 9032
.const GPIC_CRAM_LEN = 1000
.const GPIC_FIXED    = 12        // the header's fixed half: the magic to the cell grid
.const GPIC_MODE_OFF = 5         // ...and the byte in it that the FILE chooses: 2 or 3
.const GPIC_PAL_OFF  = 12        // border, bg0, bg1, bg2 -- the file's four colours

//  The header's fixed half, copied into GP_HDR once at spawn. The magic is ASCII -- it
//  is read in a hex dump, never printed -- while the LABEL is screen-coded like an FS
//  name, so a task can hand it straight to kPrintString.
//  The PALETTE (at +12) and the LABEL (at +16) are NOT here: a load reads the file's
//  header over GP_HDR, so those two are whatever the file carried, which is what
//  carries a picture's colours and its title through to the next save.
gpPicHdr:
    .byte $47, $50, $49, $43     // 'G','P','I','C'
    .byte GPIC_VERSION, GPIC_MODE, 1, 0 // version, mode, full screen, pad
    .byte 40, 0, 25, 0           // cells across, cells down
    .fill 4, 0                   // palette: border, bg0, bg1, bg2
    .fill 16, 0                  // label

//  pos / len / buffer for the three payload regions, in the file's own order. ONE table
//  serves both directions: kReuReadAt's destination and kReuWriteAt's source are the
//  same kind of thing, a C64 pointer, and both ends name the same three addresses --
//  because the canvas IS the framebuffer while it is displayed.
gpPicTab:
    .word GPIC_PIX_POS,  GPIC_PIX_LEN,  $2000
    .word GPIC_MAT_POS,  GPIC_MAT_LEN,  $0400
    .word GPIC_CRAM_POS, GPIC_CRAM_LEN, $D800

// --- gpArg -- the picture's name, and the colours after it ---
//  "run grpaint <base> [border bg fg | border bg c1 c2 c3]" is the whole command line:
//  the name opens <base>.pic, and what follows it is the colours a NEW picture starts
//  with (step 4.14). The base name is capped at 7 characters -- an FS name is 11, and
//  ".pic" plus the terminator are the other four. An explicit extension is refused rather
//  than silently mangled into one, and so is an empty name: the command line is the only
//  place the file can be named, so getting it wrong has to be said out loud.
//  THIS RUNS FIRST, before gpCols/gpCanvas/gpInit: the loader delivered the arguments
//  into the instance buffer's first 65 bytes, and those are exactly the fields gpInit is
//  about to write. The name is copied up to GP_NAME, which nothing else touches.
gpArg:
    ldy #0
gpArgScan:
    lda (dataPtr),y
    beq gpArgEnd                 // the end of the arguments
    cmp #$20
    beq gpArgEnd                 // the name ends here: the colours follow
    cmp #$2e
    beq gpArgBad                 // a '.' -- the suffix is not ours to be told
    iny
    cpy #8                       // seven characters fit; an eighth is one too many
    bcc gpArgScan
gpArgBad:
    lda #<gpMsgName
    ldx #>gpMsgName
    jmp gpFail
gpArgBadCols:
    lda #<gpMsgCols
    ldx #>gpMsgCols
    jmp gpFail
gpArgEnd:
    tya
    bne gpArgVals
    lda #<gpMsgName              // no name at all
    ldx #>gpMsgName
    jmp gpFail
gpArgVals:
    //  --- the colours, if any, BEFORE the suffix is written over them ---
    //  The values sit in the bytes gpArgSuffix is about to replace, so they are read out
    //  first, into the code image. That is safe by construction rather than by luck: the
    //  parse runs once, at spawn, before a frame exists, in a task that is
    //  single-instance -- so nothing else can ever see those bytes.
    tya
    pha
    jsr gpValParse               // gpColN + gpColV, or gpArgBadCols
    pla
    tay
gpArgSuffix:
    //  The suffix is part of the FS name, so it is written as screen codes like every
    //  other name: '.', 'p', 'i', 'c' and the terminator.
    lda #$2e                     // '.'
    sta (dataPtr),y
    iny
    lda #$10                     // 'p'
    sta (dataPtr),y
    iny
    lda #$09                     // 'i'
    sta (dataPtr),y
    iny
    lda #$03                     // 'c'
    sta (dataPtr),y
    iny
    lda #0
    sta (dataPtr),y              // ...and the terminator
    //  --- and the name up above the argument area, where gpInit cannot reach it ---
    lda dataPtr
    clc
    adc #GP_NAME
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1                   // kPtr = GP_NAME. No call intervenes, so it holds.
    ldy #0
gpArgCopy:
    lda (dataPtr),y              // the name as built at offset 0...
    sta (kPtr),y                 // ...to GP_NAME
    iny
    cpy #12                      // 7 + ".pic" + the NUL
    bne gpArgCopy
    rts

// --- gpValParse -- the colour values that follow the name ---
//  In: Y = where the name ended (a space, or the NUL when nothing follows it).
//  Out: gpColN = how many values there were (0-5), gpColV = them, in the order given.
//  A value is decimal, one or two digits, 0-15: a TYPED digit is its ASCII value, because
//  the screen codes agree for $30-$39. Runs of spaces are one separator, so a double space
//  is not a failure, and Y is clobbered.
gpValParse:
    ldx #0
gpVpLoop:
    lda (dataPtr),y
    beq gpVpDone                 // the end of the arguments
    cmp #$20
    bne gpVpValue
    iny
    bne gpVpLoop                 // the terminator is above, so Y cannot wrap here
gpVpValue:
    cpx #5
    bcs gpArgBadCols             // a sixth value: no mode has that many colours
    jsr gpVpNum
    bcs gpArgBadCols             // not a number, or past 15
    sta gpColV,x
    inx
    jmp gpVpLoop
gpVpDone:
    stx gpColN
    rts

// --- gpVpNum -- one decimal value at (dataPtr),y ---
//  Out: A = the value (0-15) and Y past it, C=0; C=1 = not a value at all. A THIRD digit
//  is not a value either -- "123" would otherwise read as 12 followed by 3, and a typed
//  run of digits would quietly become a legal number of colours.
gpVpNum:
    lda (dataPtr),y
    sec
    sbc #$30
    cmp #10
    bcs gpVpBad                  // not a digit
    sta gpValAcc
    iny
    lda (dataPtr),y
    sec
    sbc #$30
    cmp #10
    bcs gpVpOne                  // one digit: the accumulator IS the value
    sta gpValD
    lda gpValAcc
    asl
    asl
    asl
    sta gpValT                   // the accumulator x 8
    lda gpValAcc
    asl                          // ...x 2
    clc
    adc gpValT                   // ...x 10
    clc
    adc gpValD                   // ...and the second digit
    cmp #16
    bcs gpVpBad                  // 16 and up is not a colour
    iny
    sta gpValAcc                 // the value, parked while the third digit is tested
    lda (dataPtr),y
    sec
    sbc #$30
    cmp #10
    bcc gpVpBad                  // a third digit
    lda gpValAcc
    clc
    rts
gpVpOne:
    lda gpValAcc
    clc
    rts
gpVpBad:
    sec
    rts

//  The parser's own state, and the colours it read. Code-image scratch, and safe there
//  for one reason: the parse runs once, at spawn, before a frame exists, in a
//  single-instance task -- so nothing but the code reading them can ever see these bytes.
//  gpColN is the count (0, 3 or 5 by the time it is acted on), gpColV the values in the
//  order they were given, and gpColBord the border, which kAllocScreen wants in kArg and
//  the .pic header wants at +12.
gpColN:    .byte 0
gpColV:    .fill 5, 0
gpColBord: .byte 0
gpValAcc:  .byte 0
gpValD:    .byte 0
gpValT:    .byte 0

// --- gpCols -- the picture's colours, and the mode they are in ---
//  A NEW picture takes them from the arguments: three values for hires (border, paper,
//  ink) or five for MC (border, background, c1, c2, c3), or none at all for the defaults
//  the task starts with. A picture that EXISTS takes them from the FILE: its header
//  carries the mode and the palette, so a colour argument has nothing to say about it and
//  is refused rather than dropped.
//  It runs BEFORE the canvas is allocated, which is what lets the canvas be allocated in
//  the picture's FINAL colours -- the file's for a load, the arguments' for a new picture
//  -- so nothing is re-coloured after the fact. Every one of these fields is read by
//  gpAlloc, and every one of them is inside the argument area the loader has just filled,
//  so all of them are written here whichever arm is taken: a stale byte in one of them is
//  a VIC register and a thousand cells set from whatever was typed.
//  Clobbers A, X, Y, kPtr, kArg, GP_FREC and the parser's own bytes.
gpCols:
    jsr gpPicNames               // kArg = the name, kPtr = the record buffer
    jsr kReuOpen
    bcs gpColsNew                // no such file: a NEW picture
    //  --- the file is there, so this run is a LOAD and its colours are the file's ---
    lda gpColN
    beq gpColsMode
    lda #<gpMsgHasCols
    ldx #>gpMsgHasCols
    jmp gpFail
gpColsMode:
    //  gpPicCheck is the read AND the validation: the size, the magic, and the whole
    //  32-byte header into GP_HDR. The mode byte it guards is the one field the format
    //  lets a file choose, and it comes out first because the canvas is allocated in it.
    jsr gpPicCheck
    bcs gpColsDefaults           // not ours: the canned palette, and gpLoadStart says why
    ldy #GP_HDR + GPIC_MODE_OFF
    lda (dataPtr),y              // the file's mode byte: 2 hires, 3 MC
    ldy #GP_MODE
    sta (dataPtr),y
    //  --- and its four colours, which the canvas is allocated in ---
    //  gpPicPal applies these again when the payload lands, and that is not redundancy:
    //  here they are what kAllocScreen and gpCells are given, so the surface is never
    //  coloured with anything else -- not even for the two frames before a load finishes.
    ldy #GP_HDR + GPIC_PAL_OFF
    lda (dataPtr),y              // the border
    sta gpColBord                //   ...which kArg takes directly, so it goes in ours
    ldy #GP_HDR + GPIC_PAL_OFF + 1
    lda (dataPtr),y              // bg0: the paper
    ldy #GP_MB
    sta (dataPtr),y
    ldy #GP_HDR + GPIC_PAL_OFF + 2
    lda (dataPtr),y              // bg1: c1 in MC -- and the INK in a hires picture, which
    ldy #GP_MB + 1               //   is where img2pic.py and our own save record it
    sta (dataPtr),y
    ldy #GP_INK
    sta (dataPtr),y              //   ...so the same byte is the pen's own colour
    ldy #GP_HDR + GPIC_PAL_OFF + 3
    lda (dataPtr),y              // bg2: c2. c3 is in the payload's colour RAM, so it can
    ldy #GP_MB + 2               //   only arrive with the picture; gpPicPalMc reads it
    sta (dataPtr),y
    jmp gpColsPen                // the pen's first claim, for a load as for a new picture

gpColsNew:
    //  --- the ink FIRST, for both arms ---
    //  It is a hires field, but it is set here rather than in the hires arm because the
    //  mode tile can arrive at hires later, and an unset ink is a leftover argument byte:
    //  an ink over 15 is a colour no cell nibble can hold.
    lda #GP_INK_DEF
    ldy #GP_INK
    sta (dataPtr),y
    //  --- the arguments name the mode AND the colours ---
    lda gpColN
    beq gpColsDefaults           // no values: the defaults, exactly as shipped
    cmp #3
    beq gpColsDefaults
    cmp #5
    beq gpColsMc
    jmp gpArgBadCols             // 1, 2, 4, 6 and up: no mode has that many colours
                                 // A jmp rather than a branch: the parse is far enough up
                                 // the file that a 6502 branch cannot reach it (Rule 4)
gpColsMc:
    //  --- MC: border, background, c1, c2, c3 ---
    lda #GP_MODE_MC
    ldy #GP_MODE
    sta (dataPtr),y
    lda gpColV
    sta gpColBord
    ldx #1                       // the source: values 2..5 are bg0, c1, c2 and c3
    ldy #GP_MB                   // ...and this is where the task keeps them
gpColsMcLp:
    lda gpColV,x
    sta (dataPtr),y
    inx
    iny
    cpy #GP_MB + 4
    bne gpColsMcLp
    jmp gpColsPen
gpColsDefaults:
    //  --- hires: the canned palette, and the arguments over it if there are any ---
    //  bg0 IS the paper here: in this mode a clear pixel shows its cell's LOW nibble
    //  rather than $D021 (VIC-II 3.7.3.3), so the paper is a per-cell colour and GP_MB+0
    //  is what gpCells fills every cell with. c1/c2/c3 are the palette the MODE tile would
    //  arrive at, and they are DISTINCT on purpose -- the caret and the live previews are
    //  value permutations, and two entries holding one colour make one of those
    //  permutations invisible.
    lda #GP_MODE_STD
    ldy #GP_MODE
    sta (dataPtr),y
    lda #0
    sta gpColBord
    ldy #GP_MB
    sta (dataPtr),y              // bg0: black paper
    lda #1
    iny
    sta (dataPtr),y              // c1: white
    lda #11
    iny
    sta (dataPtr),y              // c2: dark grey
    lda #7
    iny
    sta (dataPtr),y              // c3: yellow
    lda gpColN
    beq gpColsPen                // no values: the palette above IS the picture's
    lda gpColV
    sta gpColBord
    lda gpColV+1
    ldy #GP_MB
    sta (dataPtr),y              // the paper
    lda gpColV+2
    ldy #GP_INK
    sta (dataPtr),y              // ...and the ink
gpColsPen:
    //  --- and the pen its first claim, the same whether the picture is new or loaded:
    //      slot 1, which for MC is c1, the first of the cell's own three colours. Slot 0
    //      claims nothing at all: a mark in value 0 is an XOR with 0, so a caret drawn in
    //      it would be invisible ---
    ldy #GP_SLOT
    lda #1
    sta (dataPtr),y
    ldy #GP_VAL
    sta (dataPtr),y
    rts

// --- gpPicNames -- kArg = the name, kPtr = the record buffer ---
//  The pair kReuOpen wants: the name in kArg, the 5-byte record it fills in kPtr.
//  kReuCreate and kReuDelete take only the name, and the spare kPtr costs them
//  nothing. Clobbers A.
gpPicNames:
    lda dataPtr
    clc
    adc #GP_NAME
    sta kArg
    lda dataPtr+1
    adc #0
    sta kArg+1
    lda dataPtr
    clc
    adc #GP_FREC
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    rts

// --- gpPicRec -- kArg = the record in our instance buffer ---
//  Where kReuReadAt and kReuWriteAt both take it. Re-derived from dataPtr every time:
//  the FS library walks the record with srcPtr, and both calls clobber kArg.
//  Clobbers A.
gpPicRec:
    lda dataPtr
    clc
    adc #GP_FREC
    sta kArg
    lda dataPtr+1
    adc #0
    sta kArg+1
    rts

// --- gpPicHdrRec -- kPtr = the header buffer in our instance buffer ---
//  Never srcPtr, for the same reason. Clobbers A.
gpPicHdrRec:
    lda dataPtr
    clc
    adc #GP_HDR
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    rts

// --- gpPicCheck -- is this one of ours, and read its header in ---
//  In: GP_FREC = the record kReuOpen left. Out: C=0 valid, its header in GP_HDR.
//  The format is FIXED, so the size IS the layout, and a file of any other length is
//  refused before a byte of it is read. The header then goes into GP_HDR -- the same
//  buffer a save writes from -- and its first 12 bytes are compared against the
//  constant half of the table: the magic, the version, the flags and the 40x25 cell
//  grid. The MODE byte is the one that may differ, because it IS the file's: 2 is
//  hires and 3 is multicolour, and either loads -- gpPicMode adopts it afterwards.
//  Clobbers A, X, Y, kPtr, kArg.
gpPicCheck:
    ldy #0
    lda (kPtr),y
    cmp #<GPIC_BYTES
    bne gpPicBad
    iny
    lda (kPtr),y
    cmp #>GPIC_BYTES
    bne gpPicBad
    jsr gpPicRec                 // kArg = the record, kReuReadAt's view of it
    lda #0
    sta kArg+2
    sta kArg+3                   // pos = 0
    lda #GPIC_HDR_LEN
    sta kArg+6
    lda #0
    sta kArg+7                   // len = the header
    jsr gpPicHdrRec              // kPtr = GP_HDR = the DMA's destination
    jsr kReuReadAt
    bcs gpPicBad
    jsr gpPicHdrRec              // kPtr is ours to re-derive after the call
    ldy #0
gpPicChkFixed:
    lda (kPtr),y
    cmp gpPicHdr,y
    bne gpPicChkMode
    iny
    cpy #GPIC_FIXED
    bne gpPicChkFixed
    clc
    rts
gpPicChkMode:
    cpy #GPIC_MODE_OFF           // the mode byte: the file's to choose
    bne gpPicBad
    lda (kPtr),y
    cmp #GP_MODE_STD
    beq gpPicChkOn
    cmp #GP_MODE_MC
    bne gpPicBad
gpPicChkOn:
    iny
    cpy #GPIC_FIXED
    bne gpPicChkFixed
    clc
    rts
gpPicBad:
    sec
    rts

// --- gpPicMode -- adopt the loaded picture's mode ---
//  The file's tag becomes OURS, and the surface is re-tagged with it -- so a hires .pic
//  into a task that was drawing multicolour, or the other way round, is the picture's
//  decision and not the spawn's. The band's own face is a FILE per mode, and the record
//  for it is selected by GP_MODE itself (gpUiRec), so nothing is resolved here; the read
//  is left to gpPickOpen, which is what reads it.
//  THE CELLS ARE NOT REFILLED: in MC a cell's byte IS the palette, and the file's own is
//  exactly what has to survive -- gpCells is for a NEW canvas, and a load has the
//  picture's. gpPicPalMc reads the palette back out of it afterwards.
//  Clobbers A, X, Y, kPtr, kArg.
gpPicMode:
    ldy #GP_HDR + GPIC_MODE_OFF
    lda (dataPtr),y              // the file's mode byte: 2 hires, 3 MC
    ldy #GP_MODE
    sta (dataPtr),y              // ...now ours
    sec
    sbc #GP_MODE_STD             // -> 0 / 1, which is what kSetGfxMode takes
    jsr kSetGfxMode              // keeps the pixels, the matrix, the colour RAM, palette
    jmp gpUiOpen                 // ...and the band's file for the mode just adopted

// --- gpPicPalMc -- the MC palette, read back out of the picture that was just loaded ---
//  A .pic header carries four colours -- border, bg0, bg1, bg2 -- and in MC only bg0 is
//  one of them: the cell's other two colours live in its matrix byte and the third in
//  the colour RAM, which the payload has just put back. So the palette a mark reads is
//  taken from where those colours ARE rather than left at whatever this task spawned
//  with -- otherwise the chips would show colours the picture does not use, and a clear
//  would refill the canvas from them.
//  The canvas is the surface on screen while a load runs, so its matrix and colour RAM
//  are the framebuffer's own cell 0 and $d800 -- the same addresses the payload was read
//  into, which is why no address arithmetic is needed here. Hires has no such palette:
//  its four colours are the header's, and gpPicPal has set them.
//  Clobbers A, Y.
gpPicPalMc:
    ldy #GP_MODE
    lda (dataPtr),y
    cmp #GP_MODE_MC
    bne gpPicPalRts
    //  bg0 is +13, NOT +0: the header's four palette bytes are border, bg0, bg1, bg2 in
    //  both modes, so every read of them has to name the field it means -- the paper and
    //  the ink are +13 and +14, and gpPicPal walks the same four for the registers
    ldy #GP_HDR + GPIC_PAL_OFF + 1
    lda (dataPtr),y
    ldy #GP_MB
    sta (dataPtr),y
    lda $0400                    // the canvas's own first cell
    pha
    lsr
    lsr
    lsr
    lsr
    ldy #GP_MB + 1
    sta (dataPtr),y              // c1 = its high nibble
    pla
    and #$0f
    ldy #GP_MB + 2
    sta (dataPtr),y              // c2 = its low nibble
    lda $d800                    // ...and c3 is the colour RAM's own byte there
    ldy #GP_MB + 3
    sta (dataPtr),y
gpPicPalRts:
    rts

// --- gpPicPal -- the picture's own palette, applied to the task's colours ---
//  A picture without its palette would load in whatever colours the surface happened to
//  carry: taskGfxBorder/Bg0/Bg1/Bg2 are per-task state, and the header's four bytes are
//  the whole of what gets them back. kSetGfxColor takes the colour in A and the selector
//  in X (0 = border, 1 = bg0, 2 = bg1, 3 = bg2), and writes the hardware too while we
//  are the display owner -- which here we are.
//  Clobbers A, X, Y, kPtr.
gpPicPal:
    lda #0
    sta gpPicIdx                 // the selector counts in MEMORY (see gpPicIdx)
gpPicPalLp:
    lda gpPicIdx
    clc
    adc #12                      // the palette's offset in the header
    tay
    jsr gpPicHdrRec              // kPtr = GP_HDR (this leaves X alone)
    lda (kPtr),y                 // A = the colour
    ldx gpPicIdx                 // X = the selector -- reloaded, never carried
    jsr kSetGfxColor             // WARNING: clobbers X -- it does ldx curTask
    inc gpPicIdx
    lda gpPicIdx
    cmp #4
    bne gpPicPalLp
    //  --- and the task's OWN copy of two of them, which no register holds ---
    //  bg0 is the PAPER a clear refills from: in hires a clear pixel shows its cell's low
    //  nibble rather than $D021, so a CLEAR or a mode toggle after a load has to know what
    //  that nibble was. bg1 is the INK, and for a hires picture that is where the ink is
    //  recorded: img2pic.py writes the commonest ink there and grpaint's own save writes
    //  the pen there (gpLoadNew). Without this read a loaded picture's first stroke would
    //  draw in the spawn ink, which on a white sheet is white.
    ldy #GP_HDR + GPIC_PAL_OFF + 1
    lda (dataPtr),y
    ldy #GP_MB
    sta (dataPtr),y              // the paper
    ldy #GP_HDR + GPIC_PAL_OFF + 2
    lda (dataPtr),y
    ldy #GP_INK
    sta (dataPtr),y              // ...and the ink
    rts

// --- gpPicLoadGo -- the three payload regions, straight to their homes ---
//  Out: C=0 all three read; C=1 a read failed. The size check has already passed, so
//  only a filesystem that went away can do that -- and a half-loaded canvas is worth
//  refusing, because the alternative is a picture that looks loaded.
//  X carries the region index, 6 bytes an entry, and is parked on the stack because
//  kReuReadAt clobbers X. PLA leaves the carry alone, so the error branch still sees it.
//  The table's third field is the DESTINATION here, and the SOURCE for a save.
gpPicLoadGo:
    ldx #0
gpPicLoadLp:
    txa
    pha
    jsr gpPicRec
    txa
    tay
    lda gpPicTab,y
    sta kArg+2                   // pos lo
    iny
    lda gpPicTab,y
    sta kArg+3                   // ...and hi
    iny
    lda gpPicTab,y
    sta kArg+6                   // len lo
    iny
    lda gpPicTab,y
    sta kArg+7                   // ...and hi
    iny
    lda gpPicTab,y
    sta kPtr                     // dest lo
    iny
    lda gpPicTab,y
    sta kPtr+1                   // ...and hi
    jsr kReuReadAt
    pla
    tax                          // the region index back
    bcs gpPicLoadRts             // a read failed: C=1 goes back to the caller
    txa
    clc
    adc #6
    tax
    cpx #18
    bne gpPicLoadLp
    clc                          // ...all three read
gpPicLoadRts:
    rts

// --- gpPicWriteGo -- the same three regions, the other way ---
//  Out: C=0 all three written; C=1 a write failed. The table's third field is the
//  SOURCE here: kReuWriteAt reads from a C64 address exactly as kReuReadAt writes to
//  one, so a single table describes both directions.
gpPicWriteGo:
    ldx #0
gpPicWriteLp:
    txa
    pha
    jsr gpPicRec
    txa
    tay
    lda gpPicTab,y
    sta kArg+2
    iny
    lda gpPicTab,y
    sta kArg+3
    iny
    lda gpPicTab,y
    sta kArg+6
    iny
    lda gpPicTab,y
    sta kArg+7
    iny
    lda gpPicTab,y
    sta kPtr
    iny
    lda gpPicTab,y
    sta kPtr+1
    jsr kReuWriteAt
    pla
    tax
    bcs gpPicWriteRts
    txa
    clc
    adc #6
    tax
    cpx #18
    bne gpPicWriteLp
    clc
gpPicWriteRts:
    rts

// --- gpLoadStart -- the picture the name already stands for ---
//  "run grpaint <base>" opens <base>.pic: if it is there it is loaded, and if it is not,
//  the blank canvas the allocator just handed us IS the new picture -- so starting one
//  says nothing at all.
//  This runs with the canvas DISPLAYED, and that is not a convenience: the reads go to
//  $2000 / $0400 / $D800, which are the canvas only while it is the surface on screen.
//  10,032 bytes fit nowhere in the task, so there is no other way to load one.
//  A file that is there but is not a hires .pic of ours is refused on a screen of our
//  own: the name was right, so the mistake is the file's, and a canvas that quietly
//  ignored it would look exactly like a load that had worked.
gpLoadStart:
    //  --- the header buffer starts as the constant table ---
    //  A save writes GP_HDR out, so a picture that has never been loaded has to get its
    //  header from somewhere: the instance buffer's bytes above the loader's argument
    //  area are the previous occupant's leftovers, and a file saved out of those would
    //  have neither a magic nor a palette. gpPicCheck DMAs a file's own 32 bytes over
    //  this, so a load replaces it whole -- the palette and the label with it.
    lda dataPtr
    clc
    adc #GP_HDR
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    ldy #0
gpLoadHdrLp:
    lda gpPicHdr,y
    sta (kPtr),y
    iny
    cpy #GPIC_HDR_LEN
    bne gpLoadHdrLp
    jsr gpPicNames               // kArg = the name, kPtr = the record buffer
    jsr kReuOpen
    bcs gpLoadNew                // no such file: a new picture
    jsr gpPicCheck               // C=1: not ours (the mode byte may be either)
    bcs gpLoadBad
    jsr gpPicMode                // the file's mode is the picture's, not the spawn's
    jsr gpPicPal                 // its palette, before anything is drawn in it
    jsr gpPicLoadGo
    bcs gpLoadBad
    //  --- and the palette a MARK reads, out of the picture that has just landed ---
    //  gpPicPal sets the four VIC colours; in MC the cell's other three are in the
    //  matrix and the colour RAM the payload has just brought, so they are read back
    //  from there. The cells themselves are left exactly as the file wrote them.
    jsr gpPicPalMc
gpLoadNew:
    //  --- a NEW picture: its header is built HERE, because a save is what writes it ---
    //  The palette bytes and the mode byte are the picture's own colours, and for a new
    //  picture those are the ones it was started with. They cannot come from the constant
    //  table gpLoadStart copied in: its four palette bytes are zero, so a fresh picture
    //  would be saved with a black palette that the next load would then apply.
    ldy #GP_MODE
    lda (dataPtr),y
    ldy #GP_HDR + GPIC_MODE_OFF
    sta (dataPtr),y              // the mode byte, as gpPickTakeMode keeps it
    lda gpColBord
    ldy #GP_HDR + GPIC_PAL_OFF
    sta (dataPtr),y              // the border...
    ldy #GP_MB
    lda (dataPtr),y
    ldy #GP_HDR + GPIC_PAL_OFF + 1
    sta (dataPtr),y              // ...then bg0 -- the paper -- which both modes agree on
    //  --- and bg1, which the two modes read differently ---
    //  MC's bg1 is c1, and its c2 follows. Hires has no c1 in the picture at all: its
    //  second colour is the INK, which is the one thing a load reads back OUT of these
    //  bytes (gpPicPal), so a hires picture has to put its ink here or a reload comes
    //  back with the spawn pen -- white, on a sheet the arguments just made white. bg2
    //  then carries the first colour a mode toggle would arrive at; nothing reads it
    //  while the file is hires.
    ldy #GP_MODE
    lda (dataPtr),y
    cmp #GP_MODE_MC
    beq gpLoadNewMc
    ldy #GP_INK
    lda (dataPtr),y
    ldy #GP_HDR + GPIC_PAL_OFF + 2
    sta (dataPtr),y              // hires: bg1 = the ink
    ldy #GP_MB + 1
    lda (dataPtr),y
    ldy #GP_HDR + GPIC_PAL_OFF + 3
    sta (dataPtr),y              // ...and bg2 = c1
    jmp gpLoadNewDirty
gpLoadNewMc:
    ldy #GP_MB + 1
    lda (dataPtr),y
    ldy #GP_HDR + GPIC_PAL_OFF + 2
    sta (dataPtr),y              // c1: the cell's high nibble...
    ldy #GP_MB + 2
    lda (dataPtr),y
    ldy #GP_HDR + GPIC_PAL_OFF + 3
    sta (dataPtr),y              // ...and c2: its low nibble (c3 is in the colour RAM)
gpLoadNewDirty:
    lda #0
    ldy #GP_DIRTY
    sta (dataPtr),y              // ...and nothing has changed since it was written
    rts
gpLoadBad:
    lda #<gpMsgPic
    ldx #>gpMsgPic
    jmp gpFail

// --- gpSave -- write the picture to <name>.pic ---
//  The band must be DOWN when this runs: the canvas's bottom two rows are part of the
//  picture, and while the band is up they hold the palette. gpPickTakeSave takes it
//  down first and puts it back afterwards (gpPickShow), which is also what re-draws the
//  marks -- with the save tile's now gone.
//  IT WRITES IN PLACE. kReuCreate refuses a name that already exists, and getting past
//  that with a delete costs a directory slot AND the whole 10,032 bytes, because a
//  delete is a tombstone that frees neither. So the file is opened first: one of the
//  right size is written through the record already in hand, and only a missing one --
//  or one of some other length -- is created.
gpSave:
    jsr gpPicNames               // kArg = the name, kPtr = the record buffer
    jsr kReuOpen
    bcs gpSaveNew                // no such file
    ldy #0
    lda (kPtr),y
    cmp #<GPIC_BYTES
    bne gpSaveRecreate
    iny
    lda (kPtr),y
    cmp #>GPIC_BYTES
    beq gpSaveWrite              // the right size: the record in hand is the target
gpSaveRecreate:
    jsr gpPicNames
    jsr kReuDelete               // the name only; the tombstone is the FS's business
gpSaveNew:
    jsr gpPicNames
    lda #<GPIC_BYTES
    sta kArg+2
    lda #>GPIC_BYTES
    sta kArg+3                   // 10,032: the format's whole length
    jsr kReuCreate
    bcs gpSaveFail
    //  --- and open it: kReuCreate takes the name and the size and hands back no
    //      record, so the one to write through has to be resolved the way a load
    //      resolves it. It cannot fail -- the file was just made --
    //      so its refusal is the same failure as the create's.
    jsr gpPicNames
    jsr kReuOpen
    bcs gpSaveFail
gpSaveWrite:
    jsr gpPicRec                 // kArg = the record, kReuWriteAt's view of it
    lda #0
    sta kArg+2
    sta kArg+3                   // pos = 0
    lda #GPIC_HDR_LEN
    sta kArg+6
    lda #0
    sta kArg+7                   // len = the header
    jsr gpPicHdrRec              // kPtr = GP_HDR = the source
    jsr kReuWriteAt
    bcs gpSaveFail
    jsr gpPicWriteGo
    bcs gpSaveFail
    clc                          // C=0: the picture is written
    rts
//  Out: C=1, nothing written. The caller reports it and keeps the canvas -- this is
//  the one failure in the task that MUST NOT exit, because the picture is unsaved.
gpSaveFail:
    sec
    rts

gpMsgNoMouse:
    .text "? mouse off - run 'mouse 1351'"
    .byte 0

// ==========================================
//  The history -- undo and redo, in the reserved bank
// ==========================================
//  A STATE is the canvas plus the two things a picture owns that its pixels do not carry.
//  The pixels are a .pic payload -- gpPicTab's three regions, at the same offsets a file
//  holds them at, so a snapshot is three copies of the canvas's own addresses and no
//  second table is needed -- and behind them go the mode and the four palette colours.
//  Ten thousand and thirty-nine bytes, $2800 apart in the REU's top full bank.
//  The ring holds SIX states, and that is what makes REDO free: it keeps the states
//  before the one on screen as well as the ones after it, so undo and redo are the same
//  walk in opposite directions and neither needs a stack of its own. Six states are five
//  steps each way; the seventh push drops the oldest instead of growing.
//  GP_U_HEAD is the state the canvas on screen IS, and that is the invariant the rest is
//  built on: a push stores the canvas at the state after HEAD and moves HEAD onto it, and
//  a step moves HEAD and shows the state it lands on. Nothing else writes HEAD.
//  EVERY entry point is display-guarded. A snapshot READS the framebuffer, the video
//  matrix and the colour RAM, which are the canvas only while the canvas is the surface on
//  screen -- so an action taken while another task's screen is up is not a step, and a
//  step cannot be taken while it is. That is the price of a snapshot that costs the task
//  no memory at all: 10KB fits nowhere in it.

// --- gpUndoInit -- the ring's first state: the canvas the load left ---
gpUndoInit:
    jsr gpUndoCap
    bcs gpUndoInitRts            // no bank for the ring: no history, and none promised
    lda #0
    ldy #GP_U_HEAD
    sta (dataPtr),y
    iny
    sta (dataPtr),y              // GP_U_NEW: the canvas is the newest state...
    iny
    sta (dataPtr),y              // GP_U_OLD: ...and, being the only one, the oldest
    sec                          // C=1: the canvas INTO the ring
    jmp gpUndoStore              // ...as state 0
gpUndoInitRts:
    rts

// --- gpUndoCap -- C=1 when this machine has no bank for the ring ---
//  The ring is the top FULL bank, and a machine with fewer banks than that has none -- a
//  1750 has eight. The check is what keeps the ring out of the filesystem's data region,
//  which on such a machine is everything above the capacity it UNDER-reports.
gpUndoCap:
    lda reuTotalBanks+1
    bne gpUndoCapRts             // more banks than a byte holds: the top one exists
    lda reuTotalBanks
    cmp #GP_U_BANK + 1           // C=1 when the bank the ring wants is not there
    rts
gpUndoCapRts:
    clc
    rts

// --- gpUndoNext / gpUndoPrev -- the neighbouring state's index, round the six ---
//  In: A = a state index. Out: A = the next one, or the one before. Clobbers the flags.
gpUndoNext:
    clc
    adc #1
    cmp #GP_U_N
    bcc gpUndoNextRts
    lda #0
gpUndoNextRts:
    rts
gpUndoPrev:
    sec
    sbc #1
    bcs gpUndoPrevRts            // no borrow: A was not 0
    lda #GP_U_N - 1
gpUndoPrevRts:
    rts

// --- gpUndoPayload -- a state's three payload regions, one way or the other ---
//  In: A = the state index, C = 1 the canvas goes into the ring, C = 0 the state comes
//  back. The regions, their lengths and their canvas ends are gpPicTab's -- the table a
//  save and a load walk -- so a state is a .pic payload in the file's own order, and the
//  state's base only shifts its REU end. 8000 pixel bytes, 1000 of matrix, 1000 of colour
//  RAM: the framebuffer, the video matrix and the colour RAM, which are the canvas only
//  while the canvas is the surface on screen. That is why every caller is guarded.
//  The DMA calls preserve X and Y, so the table row is parked on the stack across them --
//  the same shape kReuReadAt's own loop uses. The carry the caller set IS the direction,
//  and `lda #0 / adc #0` is what carries it into the byte the loop branches on, because a
//  PLA would clear it.
gpUndoPayload:
    sta gpUTmp                   // the state index: the loop's own X is the table row
    tax
    lda gpURingHi,x
    sta gpUHi                    // ...and its high byte is this (its low byte is 0)
    lda #0
    adc #0                       // the caller's carry: 0 a read, 1 a write
    sta gpUDir
    ldx #0
gpUndoPayLp:
    txa
    pha                          // park the row: the call below would clobber it
    txa
    tay
    lda gpPicTab,y
    sta kArg                     // the region's offset within the state...
    iny
    lda gpPicTab,y
    clc
    adc gpUHi
    sta kArg+1                   // ...and its high byte, over the state's base
    iny
    lda gpPicTab,y
    sta kArg+3                   // the length, low...
    iny
    lda gpPicTab,y
    sta kArg+4                   // ...and high
    iny
    lda gpPicTab,y
    sta kPtr                     // the canvas end: the destination of a read, the
    iny                          //   source of a write
    lda gpPicTab,y
    sta kPtr+1
    lda #GP_U_BANK
    sta kArg+2
    lda gpUDir
    bne gpUndoPayOut
    jsr kReuToC64                // the state onto the canvas
    jmp gpUndoPayNext
gpUndoPayOut:
    jsr kReuFromC64              // ...and the canvas into the state
gpUndoPayNext:
    pla
    clc
    adc #6                       // six bytes a table entry
    tax
    cpx #18
    bne gpUndoPayLp
    rts

// --- gpUExtAddr -- kArg/kArg+1/kArg+2 = state gpUTmp's own extras block ---
//  Clobbers A.
gpUExtAddr:
    lda gpUHi
    clc
    adc #>GP_U_EXT
    sta kArg+1
    lda #<GP_U_EXT
    sta kArg
    lda #GP_U_BANK
    sta kArg+2
    rts

// --- gpUndoStore -- the state in A takes the canvas ---
//  In: A = the state index, the canvas DISPLAYED. The payload's three regions go first,
//  then the mode and the palette: seven bytes of the instance buffer, of which the slot
//  and the value are along for the ride because they sit between them. A step reads back
//  only the mode and the colours -- the pen is not the picture, so a step must not put an
//  old pen setting back.
gpUndoStore:
    sec                          // the canvas INTO the ring
    jsr gpUndoPayload            // A is the state index: it must still be in A here
    lda #0
    ldy #GP_U_DRAW
    sta (dataPtr),y              // whatever has been drawn is IN this state now
    jsr gpUExtAddr
    lda #GP_U_EXT_LEN
    sta kArg+3
    lda #0
    sta kArg+4                   // seven of them
    lda dataPtr
    clc
    adc #GP_MODE
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    jmp kReuFromC64              // ...and the carry it returns is not the caller's

// --- gpUndoShow -- the state HEAD names goes onto the canvas ---
//  In: the canvas DISPLAYED. The mode and the palette come out of the state with the
//  pixels, because a mode is a tag and a palette is what a mark claims: a restore that
//  left either behind would show the picture the state describes in the wrong colours.
//  The palette's first entry goes to the hardware as well -- in MC value 0 IS $d021,
//  which no cell carries -- and a mode the state does not agree with the one we were in
//  is re-tagged, and the band's own face follows the mode by itself (gpUiRec).
//  THE CELLS ARE THE PAYLOAD'S: a restore never refills them, because in MC a cell's byte
//  IS the palette and a loaded picture's own cells are the picture.
//  What lands is not what the file holds, so the picture counts as unsaved.
gpUndoShow:
    ldy #GP_MODE
    lda (dataPtr),y
    sta gpTmp                    // the mode being left, for the re-tag at the end
    ldy #GP_U_HEAD
    lda (dataPtr),y              // the state to show: HEAD, which the caller has set
    clc                          // the ring INTO the canvas
    jsr gpUndoPayload
    jsr gpUExtAddr               // kArg = the extras block
    lda #1
    sta kArg+3
    lda #0
    sta kArg+4                   // one byte: the mode
    lda dataPtr
    clc
    adc #GP_MODE
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    jsr kReuToC64
    jsr gpUExtAddr
    lda #GP_U_PAL_OFF
    clc
    adc kArg
    sta kArg                     // ...three bytes along: the palette, four long
    lda #4
    sta kArg+3
    lda #0
    sta kArg+4
    lda dataPtr
    clc
    adc #GP_MB
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    jsr kReuToC64
    ldy #GP_MB
    lda (dataPtr),y
    ldx #1                       // selector 1 = bg0
    jsr kSetGfxColor             // WARNING: clobbers X -- it does ldx curTask
    ldy #GP_MODE
    lda (dataPtr),y
    cmp gpTmp
    beq gpUndoShown              // the same mode: nothing to re-tag or to re-resolve
    ldy #GP_HDR + GPIC_MODE_OFF
    sta (dataPtr),y              // the header's own mode byte, which a save writes
    sec
    sbc #GP_MODE_STD
    jsr kSetGfxMode              // ...and the VIC. The band's own face needs nothing:
                                 //   its record is selected by the mode itself (gpUiRec)
gpUndoShown:
    lda #1
    ldy #GP_DIRTY
    sta (dataPtr),y              // the canvas is not what the file holds any more
    rts

// --- gpUndoPush -- the ring gains a state: the canvas as it is NOW ---
//  Every action that changes the canvas ends here, which is what keeps the state HEAD
//  names the canvas on screen -- a step never needs a "before" copy.
//  THE STATE GOES AFTER HEAD, and that is what discards the redo tail: whatever lies
//  between HEAD and NEW was drawn before an undo, so a new action makes it unreachable
//  (and the next pushes walk over its slots). Advancing NEW instead would leave those
//  states in the ring, and a step back after a new action would show one of them.
//  A push whose state would land on the OLDEST one drops it instead: that collision IS
//  "the ring is full", so the wrap and the size test are the same comparison.
gpUndoPush:
    jsr gpDgOurs
    bne gpUndoPushRts            // not our surface: the framebuffer is not the canvas
    jsr gpUndoCap
    bcs gpUndoPushRts            // no bank for the ring on this machine
    ldy #GP_U_HEAD
    lda (dataPtr),y
    jsr gpUndoNext
    ldy #GP_U_HEAD
    sta (dataPtr),y              // the canvas is about to be stored THERE...
    ldy #GP_U_NEW
    sta (dataPtr),y              // ...and that is the ring's newest state
    ldy #GP_U_OLD
    cmp (dataPtr),y
    bne gpUndoPushGo             // the oldest is not in the way: the ring has room
    lda (dataPtr),y
    jsr gpUndoNext
    sta (dataPtr),y              // the oldest state is the one being overwritten
gpUndoPushGo:
    ldy #GP_U_HEAD
    lda (dataPtr),y
    jmp gpUndoStore
gpUndoPushRts:
    rts

// --- gpDropMark -- a mark that no state holds comes off the canvas ---
//  The line tool's anchor click marks the pixel it lands on and the LINE is what becomes
//  a step; a stroke still under the button has its dots the same way. GP_U_DRAW is what
//  says so -- set by gpDot on every mark, cleared by the store that records one -- and the
//  marks it covers are NOT STEPS: they are in no state, so they never colour an arrow and
//  they must never reach the ring.
//  Showing HEAD again IS the canvas without them: HEAD does not contain them -- that is
//  what having run ahead MEANS -- so this takes them off, leaves the ring exactly where it
//  is, and reports the press spent (C=1) for a caller that was taking a step.
//  THE GESTURE GOES WITH THE MARK (GP_DRAW): a mark that has been taken off is not a
//  stroke or an anchor any more, and the line's next click has to be a new ANCHOR rather
//  than a line drawn from an anchor nobody can see.
//  Callers: a step press (gpUndoBack/gpUndoFwd stop on C=1), and the band's own opening
//  (gpPickOpen), so what the band's arrows describe is never a canvas that has run ahead
//  of the state those tests read.
//  Out: C=1 when a mark came off, C=0 when there was none.
gpDropMark:
    ldy #GP_U_DRAW
    lda (dataPtr),y
    beq gpDropMarkNone
    jsr gpDgOurs
    bne gpDropMarkNone           // not our surface: HEAD is not what is on screen there
    jsr gpUndoShow               // HEAD back over the marks, which are not in it
    lda #0
    ldy #GP_U_DRAW
    sta (dataPtr),y              // ...the canvas is HEAD's own again
    ldy #GP_DRAW
    sta (dataPtr),y              // ...and no gesture owns it any more
    sec
    rts
gpDropMarkNone:
    clc
    rts

// --- gpUndoPushBand -- an action taken with the band UP: the state is the CANVAS ---
//  The band's own two cell rows are the canvas's bottom two, and while it is up they hold
//  the palette rather than the picture -- so a snapshot taken with it up would keep the
//  band as part of the canvas, and a step back would paint it into the picture. The band
//  comes down for the snapshot and goes straight back up, which is also what re-saves the
//  canvas's own rows (gpPickShow) and re-draws the chips and the marks.
//  Reached only with the band up and the display ours: the CLEAR tile, a palette press and
//  the MODE tile, each of which has already refused to act otherwise.
gpUndoPushBand:
    jsr gpPickAway               // the band down, and the canvas's own rows back
    jsr gpUndoPush
    ldy #GP_MENU
    lda #1
    sta (dataPtr),y
    jmp gpPickShow               // ...and the band up, over the state just stored

// --- gpUndoBack / gpUndoFwd -- one step through the ring ---
//  The ends of the history are a no-op rather than a corruption: BACK does nothing while
//  the canvas IS the oldest state and FWD nothing while it is the newest, which is what
//  makes a press on an arrow with nothing to do harmless. A mark that no state holds is
//  not a step and never becomes one: the press takes it off first and STOPS (gpDropMark,
//  C=1), so a step never takes off more than the one action it is meant to, and the mark
//  never reaches the ring.
//  The band is drawn back afterwards when it is up: a step writes over its own rows, and
//  the mark a consumed press takes off was drawn over them too -- so gpPickShow re-saves
//  the rows the state arrived with and reads the image in again. That hangs off the
//  SHARED tail and not off the step itself, because the press that ends in a no-op (C=1,
//  or a step the ends of the history refuse) has to put the band back just the same.
gpUndoBack:
    jsr gpDropMark
    bcs gpUndoStepRts            // the press took off the unfinished mark
    ldy #GP_U_OLD
    lda (dataPtr),y
    ldy #GP_U_HEAD
    cmp (dataPtr),y
    beq gpUndoStepRts            // the oldest state is the one on screen
    lda (dataPtr),y
    jsr gpUndoPrev
    jmp gpUndoMove
gpUndoFwd:
    jsr gpDropMark
    bcs gpUndoStepRts            // the press took off the unfinished mark
    ldy #GP_U_NEW
    lda (dataPtr),y
    ldy #GP_U_HEAD
    cmp (dataPtr),y
    beq gpUndoStepRts            // the newest state is the one on screen
    lda (dataPtr),y
    jsr gpUndoNext
gpUndoMove:
    sta (dataPtr),y              // HEAD moves onto the state about to be shown
    jsr gpDgOurs
    bne gpUndoStepRts            // not our surface: those pixels are not the canvas
    jsr gpUndoShow
gpUndoStepRts:
    //  --- and the band, if it was up: a step, or the mark a consumed press took off,
    //      wrote over the band's own rows, and GP_MENU is the band being up, so this
    //      covers the paths that end in no step as well. The display is tested again
    //      because the no-op paths reach here without having tested it, and those bytes
    //      are only a band while OUR surface is the one on screen ---
    ldy #GP_MENU
    lda (dataPtr),y
    beq gpUndoStepDone           // the band is down: nothing to draw back over it
    jsr gpDgOurs
    bne gpUndoStepDone           // not our surface: the band is not ours to draw
    jmp gpPickShow
gpUndoStepDone:
    rts

// --- gpUndoTake -- the band's two arrows: a step back, or a step forward ---
//  COMMANDS like the CLEAR tile and the MODE tile, never tools: neither arrow is ever "in
//  use", so neither is marked, and the press runs it where it is. The band stays up --
//  gpUndoMove draws it again over the state that arrives.
//  The pair is STACKED, which is why the entries are the band's ROWS here: the top one
//  is the undo arrow and the bottom one the redo.
gpUndoTake:
    cmp #GP_PK_ICON0
    bne gpUndoTakeFwd
    jmp gpUndoBack               // the undo arrow: the band's top cell row
gpUndoTakeFwd:
    jmp gpUndoFwd                // ...and the redo arrow below it

//  The history's own scratch, in the code image: the state whose payload is being walked,
//  the direction it is walked in, and that state's high byte (its low byte is always 0).
//  gpURingHi is the six state bases, in the bank-they-all-share form the DMA wants.
gpUTmp:  .byte 0
gpUDir:  .byte 0
gpUHi:   .byte 0
gpURingHi:
    .byte 0, GP_U_STRIDE/256, GP_U_STRIDE*2/256, GP_U_STRIDE*3/256, GP_U_STRIDE*4/256, GP_U_STRIDE*5/256

gpMsgNoUi:
    .text "? no gpui.bin - the UI band's image"
    .byte 0
gpMsgNoCanvas:
    .text "? no canvas - all surface slots are taken"
    .byte 0
gpMsgNoArt:
    .text "? no pointer.spr"
    .byte 0
gpMsgName:
    .text "? grpaint <name> - 7 chars, no .pic"
    .byte 0
//  The colours: what the values are FOR, and the two ways they cannot be used. Three
//  values or five, because that is how many colours the two modes need; and none at all
//  with a picture that already exists, because the file's own colours are the picture's.
gpMsgCols:
    .text "? 3 colours for hires, 5 for mc"
    .byte 0
gpMsgHasCols:
    .text "? the picture has its own colours"
    .byte 0
gpMsgPic:
    .text "? not a .pic picture"
    .byte 0
gpMsgSave:
    .text "? save failed - press a key"
    .byte 0

//  The bounds, exclusive: 320 and 200. The clamp uses bound - 1 as the last legal
//  value. Index 0 is x (used with the x high-byte offset) and 1 is y.
gpBoundLo:
    .byte <GP_W, <GP_H
gpBoundHi:
    .byte >GP_W, >GP_H
