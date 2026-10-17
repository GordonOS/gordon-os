// ==========================================
//  granim.inc -- body of the "granim" task
// ==========================================
//  Plays a film that lives in the REU filesystem: a manifest plus chunk files,
//  each a run of whole .pic records. The manifest supplies the rate, the
//  palette, the frame count, the chunk shape and the first chunk's name, so the
//  task holds no knowledge of any particular film.
//
//  Frames are read straight into the display's own homes -- $0400 matrix, $D800
//  colour RAM, $2000 pixels -- so nothing is staged and no decoder runs on the
//  machine. A whole frame goes out inside ONE display frame.
//
//  THE TRANSFER ORDER IS MATRIX, COLOUR RAM, THEN PIXELS FROM THE TOP ROW DOWN,
//  and that order is what makes a whole-screen rewrite tear-free: the beam reads
//  a line's matrix and colour before it reads that line's pixels, so the bytes it
//  needs first are the bytes written first. Every write then lands thousands of
//  cycles AHEAD of the beam instead of behind it, and no part of the frame shows
//  last frame's content -- which is also why the frame does not have to be split
//  across two display frames (it would not fit the border window, but it does
//  not need to: 10,032 bytes of DMA is 10,000 cycles against a 12,600-cycle
//  visible period, and the beam consumes 40 bytes a line where this writes 63).
//
//  The clock is frameCount, the display's own 50.1 Hz, never the scheduler tick:
//  the tick is 59.2 Hz, so a tick-driven film would be stepped more often than it
//  is shown, and no speed constant removes that stutter.
//
//  Losing the display (an F-key switch) PAUSES the film rather than writing a
//  frame into another task's screen, and it resumes where it stopped: the frame
//  on show is the one the outgoing surface holds.

.encoding "screencode_mixed"

// --- the .pic record and the manifest (docs/pic-format.md, tools/gen-anim.py)
.const GRN_MODE_MC   = 3         // BMPMODE_MC -- the mode a manifest stores
.const GRN_ALLOC_MC  = 1         // kAllocScreen's mode argument for MC
.const GRN_PIC_BYTES = 10032
.const GRN_MAN_BYTES = 64
.const GRN_VERSION   = 1

.const GRN_MF_VERSION = 4
.const GRN_MF_MODE    = 5
.const GRN_MF_PER     = 6
.const GRN_MF_DIGITS  = 7
.const GRN_MF_FRAMES  = 8        // 2 bytes, little-endian
.const GRN_MF_FPS     = 10       // 2 bytes: the rate x 256
.const GRN_MF_BORDER  = 12
.const GRN_MF_BG0     = 13
.const GRN_MF_BG1     = 14
.const GRN_MF_BG2     = 15
.const GRN_MF_FBYTES  = 16       // 4 bytes; the low two are the size
.const GRN_MF_NAME    = 24       // 12: screen-coded, NUL padded

// The display's line rate in whole lines a second. PAL is 50.125, so the
// accumulator runs 0.25% fast -- 0.04 s across this film.
.const GRN_DISPLAY_HZ = 50
//  The +/- keys shift the accumulator's step one bit per step away from the
//  film's own rate, so the range is a quarter speed to double speed.
.const GRN_SPEED_BASE = 2        // the film's own rate -- the default
.const GRN_SPEED_MIN  = 0        // a quarter speed
.const GRN_SPEED_MAX  = 3        // double speed

granim:                          // ENTRY -- the first label in the body
    // --- the manifest's name: the argument word is the film's base name and the
    //     suffix is appended to it in place, so a base name is at most 7
    //     characters (`run granim llama` opens llama.inf) ---
    ldy #0
grnArgScan:
    lda (dataPtr),y
    beq grnArgEnd
    cmp #$20
    beq grnArgEnd
    iny
    cpy #7
    bcc grnArgScan
grnArgEnd:
    tya
    bne grnArgSuffix
    lda #0
    sta (dataPtr),y              // nothing to open, and no name to report
    lda #<grnMsgUse
    ldx #>grnMsgUse
    jmp grnRefuse
grnArgSuffix:
    lda #$2e                     // '.'
    sta (dataPtr),y
    iny
    lda #$09                     // 'i'
    sta (dataPtr),y
    iny
    lda #$0e                     // 'n'
    sta (dataPtr),y
    iny
    lda #$06                     // 'f'
    sta (dataPtr),y
    iny
    lda #0
    sta (dataPtr),y              // null-terminate

    // --- resolve the manifest, then read it whole. Every refusal above and
    //     below this point is reached with NO surface, so none of them takes the
    //     display or disturbs the shell's screen ---
    jsr grnRec
    lda dataPtr
    sta kArg                     // kArg = the name at offset 0
    lda dataPtr+1
    sta kArg+1
    jsr kReuOpen
    bcc grnFound
    lda #<grnMsgNoFile
    ldx #>grnMsgNoFile
    jmp grnRefuse

grnFound:
    jsr grnRec
    lda #0
    sta kArg+2
    sta kArg+3                   // pos = 0
    lda #GRN_MAN_BYTES
    sta kArg+6
    lda #0
    sta kArg+7                   // len = the manifest
    jsr grnMan                   // kPtr = the manifest buffer = the DMA dest
    jsr kReuReadAt
    bcs grnBad
    lda kArg+6                   // the bytes actually read
    cmp #GRN_MAN_BYTES
    bne grnBad
    lda kArg+7
    bne grnBad
    jsr grnCheckMan
    bcc grnManOk
grnBad:
    lda #<grnMsgBad
    ldx #>grnMsgBad
    jmp grnRefuse

grnManOk:
    // --- a surface with the manifest's own palette. The paper and the border
    //     are the surface's, not a cell's, so the film carries them rather than
    //     letting each frame choose and flash the screen ---
    jsr grnMan
    ldy #GRN_MF_BORDER
    lda (kPtr),y
    sta kArg                     // border
    iny
    lda (kPtr),y
    sta kArg+1                   // bg0
    iny
    lda (kPtr),y
    sta kArg+2                   // bg1
    iny
    lda (kPtr),y
    sta kArg+3                   // bg2
    lda #GRN_ALLOC_MC
    jsr kAllocScreen
    bcc grnScreen
    lda #<grnMsgNoScr
    ldx #>grnMsgNoScr
    jmp grnRefuse

grnScreen:
    jsr kDisableCursor           // a picture shows no blink block

    //  TAKE THE DISPLAY WITHOUT TAKING THE OUTGOING CURSOR BLOCK WITH IT.
    //  The blink block is drawn INTO the framebuffer pixels and kSwitchView
    //  copies the framebuffer into the outgoing surface's REU slot, so a block
    //  drawn at this instant is baked into the shell's saved screen and nothing
    //  restores that cell. kBlinkReset erases it at the displayed surface's
    //  saved position. The SEI keeps an IRQ tick from drawing it straight back,
    //  and no syscall may run inside the window (the dispatcher re-enables IRQs).
    //  kBlinkReset clobbers A, X, Y, kPtr, kArg+7.
    lda #SYS_GETTID
    jsr syscall
    tax
    php
    sei
    txa
    pha
    jsr kBlinkReset
    pla
    tax
    jsr kSwitchView
    plp

    // --- the film's own state. Every byte is set before it is read: the loader
    //     leaves the spawn's argument bytes across the low offsets, so a field
    //     left alone reads back as part of the command line ---
    jsr grnTake                  // the manifest's scalars
    lda #GRN_SPEED_BASE
    ldy #GRN_SPEED
    sta (dataPtr),y
    lda #0
    ldy #GRN_PAUSE
    sta (dataPtr),y
    ldy #GRN_ACC
    sta (dataPtr),y
    ldy #GRN_POS
    sta (dataPtr),y
    iny
    sta (dataPtr),y              // pos = 0
    ldy #GRN_FRAME
    sta (dataPtr),y
    iny
    sta (dataPtr),y              // frame = 0
    lda frameCount
    ldy #GRN_LAST
    sta (dataPtr),y

    // --- the first chunk. The manifest carries its name screen-coded, so the
    //     digits are bumped in place and no number is ever formatted here ---
    jsr grnCopyName
    jsr grnOpenChunk
    bcc grnLoop
    lda #<grnMsgNoChunk
    ldx #>grnMsgNoChunk
    jmp grnRefuse

// --- the play loop -------------------------------------------------------
grnLoop:
    lda frameCount
    ldy #GRN_LAST
    cmp (dataPtr),y
    beq grnWait                  // no new display frame yet
    sta (dataPtr),y              // compared for CHANGE only: 8-bit, and it wraps

    //  The display can be taken away by an F-key switch, and then the framebuffer
    //  is another task's surface. A frame that is not ours to draw is simply not
    //  transferred; the film resumes from the next frame, because the one on show
    //  is the one that went into our slot on the way out.
    lda #SYS_GETTID
    jsr syscall
    tax
    jsr kSurfDisplayed           // C=1 displayed (clobbers A, X, Y)
    bcc grnWait
    ldy #GRN_PAUSE
    lda (dataPtr),y
    bne grnWait                  // held by the spacebar

    //  THE FILM'S LENGTH IS A REAL-TIME LENGTH, so the clock steps at the
    //  machine's declared speed: a faster machine draws 2^sysSpeedShift times as
    //  many frames per real second, and stepping the clock on one frame in that
    //  many gives the film the same length on screen whatever `speed` says. This
    //  is the games' own motion clock (docs/programmers-guide.md: gkStill,
    //  gtPace16). A SLOWER declaration is deliberately NOT compensated, exactly
    //  as gkStill does not compensate it -- fewer frames a second is fewer
    //  frames a second, and the film is longer in real time.
    ldx sysSpeedShift
    beq grnStepAcc
    bmi grnStepAcc
    lda #1
grnStepMsk:
    asl                          // 2^shift: the frames between two clock steps
    dex
    bne grnStepMsk
    sec
    sbc #1                       // the phase bits that must be zero
    and frameCount
    bne grnWait                  // not a motion frame: the clock holds

grnStepAcc:
    // --- the film clock: the film's rate against the display's own, counted in
    //     clock steps. The accumulator carries the fraction, so 24 fps over 50.1
    //     Hz alternates two and three steps per film frame instead of stepping on
    //     a fixed count and drifting into a stutter ---
    ldy #GRN_ACC
    lda (dataPtr),y
    ldy #GRN_ADD
    clc
    adc (dataPtr),y
    cmp #GRN_DISPLAY_HZ
    bcc grnNoStep
    sec
    sbc #GRN_DISPLAY_HZ
    ldy #GRN_ACC
    sta (dataPtr),y
    jsr grnReadFrame
    bcs grnStop                  // a short read: the film is not what it claims
    jsr grnAdvance
    bcs grnStop
    jmp grnWait
grnStop:
    jmp grnQuit                  // grnQuit is too far for a branch from here
grnNoStep:
    ldy #GRN_ACC
    sta (dataPtr),y

grnWait:
    lda #SYS_YIELD
    jsr syscall
    jsr grnKeys
    jmp grnLoop

// --- grnReadFrame -- the frame at GRN_POS, straight to the display ---
//  Out: C=1 if any region did not deliver the length asked for.
//  Six reads, so no single SEI window (the FS library holds interrupts off for
//  the whole of a transfer) masks the timer for more than 2,000 bytes.
//  X carries the region index and is parked on the stack because kReuReadAt
//  clobbers X; PLA leaves the carry alone, so the error branch still sees it.
grnReadFrame:
    ldx #0
grnReadLp:
    txa
    pha                          // park the region index
    jsr grnRec                   // kArg = kPtr = the record (clobbers A only)
    //  pos = the frame's position + this region's offset in the record
    ldy #GRN_POS
    lda grnRegionTab,x
    clc
    adc (dataPtr),y
    sta kArg+2                   // pos lo
    iny
    lda grnRegionTab+1,x
    adc (dataPtr),y
    sta kArg+3                   // pos hi
    lda grnRegionTab+2,x
    sta kArg+6                   // len lo
    lda grnRegionTab+3,x
    sta kArg+7                   // len hi
    lda grnRegionTab+4,x
    sta kPtr                     // dest lo
    lda grnRegionTab+5,x
    sta kPtr+1                   // dest hi
    jsr kReuReadAt
    pla
    tax                          // the region index back
    bcs grnReadBad
    //  A chunk that is short, or a film whose record size is not what the
    //  manifest says, shows up here rather than as a torn or frozen picture.
    lda kArg+6
    cmp grnRegionTab+2,x
    bne grnReadBad
    lda kArg+7
    cmp grnRegionTab+3,x
    bne grnReadBad
    txa
    clc
    adc #6
    tax
    cpx #36
    bne grnReadLp
    clc
    rts
grnReadBad:
    sec
    rts

// --- grnAdvance -- one film frame on: position, counters, the chunk seam ---
//  The film loops: reaching the frame count returns to the first chunk, whose
//  name the manifest still holds unmodified.
//  Out: C=1 if the next chunk could not be opened.
grnAdvance:
    // pos += one frame
    ldy #GRN_POS
    lda (dataPtr),y
    clc
    ldy #GRN_FS
    adc (dataPtr),y
    ldy #GRN_POS
    sta (dataPtr),y
    iny
    lda (dataPtr),y
    ldy #GRN_FS+1
    adc (dataPtr),y
    ldy #GRN_POS+1
    sta (dataPtr),y
    // frame += 1
    ldy #GRN_FRAME
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    iny
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y
    // left -= 1
    ldy #GRN_LEFT
    lda (dataPtr),y
    sec
    sbc #1
    sta (dataPtr),y
    //  The film's own end is tested BEFORE the chunk's, because the last chunk of
    //  a film whose frame count is not a multiple of the chunk size is short --
    //  opening the chunk after it would ask the filesystem for a file that was
    //  never written.
    ldy #GRN_FRAME
    lda (dataPtr),y
    ldy #GRN_TOTAL
    cmp (dataPtr),y
    bne grnAdvSeam
    ldy #GRN_FRAME+1
    lda (dataPtr),y
    ldy #GRN_TOTAL+1
    cmp (dataPtr),y
    bne grnAdvSeam
    jsr grnCopyName              // the film starts again
    bcs grnAdvFail
    jsr grnOpenChunk
    bcs grnAdvFail
    lda #0
    ldy #GRN_FRAME
    sta (dataPtr),y
    iny
    sta (dataPtr),y
    jmp grnAdvRestart
grnAdvSeam:
    ldy #GRN_LEFT
    lda (dataPtr),y
    bne grnAdvDone               // frames left in this chunk: nothing to open
    jsr grnBump                  // the next chunk: its number, bumped in place
    jsr grnOpenChunk
    bcs grnAdvFail
grnAdvRestart:
    lda #0
    ldy #GRN_POS
    sta (dataPtr),y
    iny
    sta (dataPtr),y              // pos = 0
    ldy #GRN_PER
    lda (dataPtr),y
    ldy #GRN_LEFT
    sta (dataPtr),y              // a whole chunk of frames is ahead
grnAdvDone:
    clc
    rts
grnAdvFail:
    sec
    rts

// --- grnBump -- the current chunk's number, plus one, in place ---
//  The name is <base><digits>.anm, so the digits end at the '.' and are
//  GRN_DIGITS wide. A carry out of the field leaves every digit '0', which names
//  a chunk that is not there: the open then fails and the film stops, rather
//  than playing some other chunk. Clobbers A, X, Y, kPtr.
grnBump:
    jsr grnFname                 // kPtr = the name
    ldx #0
grnBumpFind:
    txa
    tay                          // ((zp),y) is the only indexed-indirect form
    lda (kPtr),y
    cmp #$2e                     // '.'
    beq grnBumpGo
    inx
    cpx #12
    bcc grnBumpFind
    rts                          // no '.': leave it, and the open will refuse it
grnBumpGo:                       // X = the '.' index, so X-1 is the last digit
    //  kPtr walks back one digit at a time with Y pinned at 0, which is what lets
    //  the carry reach the digit above without a second index. grnCheckMan has
    //  already refused a width of 0, so the walk cannot run off the name.
    dex
    txa
    clc
    adc kPtr
    sta kPtr
    lda kPtr+1
    adc #0
    sta kPtr+1                   // kPtr = the last digit
    ldy #GRN_DIGITS
    lda (dataPtr),y
    tax                          // X = the field's width, from the manifest
    ldy #0
grnBumpLp:
    lda (kPtr),y
    cmp #$39                     // '9'
    bne grnBumpInc
    lda #$30                     // '9' -> '0', carry to the digit above
    sta (kPtr),y
    lda kPtr                     // kPtr one digit towards the front
    bne grnBumpStep
    dec kPtr+1
grnBumpStep:
    dec kPtr
    dex
    bne grnBumpLp
    rts                          // the field carried out: the open will refuse it
grnBumpInc:
    clc
    adc #1
    sta (kPtr),y
    rts

// --- grnCopyName -- GRN_FNAME = the manifest's first chunk name ---
//  The manifest's own copy is never written: it is what restarts the film when
//  the frame count runs out. srcPtr is the destination because it is the only
//  other pointer free here, and this is a leaf -- no kernel call intervenes, so
//  nothing clobbers it. Clobbers A, Y, kPtr, srcPtr.
grnCopyName:
    //  Both pointers are moved onto the fields they copy between, so ONE Y
    //  indexes the source and the destination alike. "(zp),x" is NOT a 6502
    //  addressing mode: KickAssembler assembles it as a plain ZERO PAGE access,
    //  so "sta (srcPtr),x" becomes "sta srcPtr,x" -- which would write the name
    //  over srcPtr, the IRQ-owned tmpPtr, curTask, saveSP and the stack page.
    jsr grnMan                   // kPtr = the manifest
    lda kPtr
    clc
    adc #GRN_MF_NAME
    sta kPtr
    lda kPtr+1
    adc #0
    sta kPtr+1                   // kPtr = the manifest's first-chunk name
    lda dataPtr
    clc
    adc #GRN_FNAME
    sta srcPtr
    lda dataPtr+1
    adc #0
    sta srcPtr+1                 // srcPtr = our own copy of it
    ldy #0
    lda (kPtr),y
    beq grnCopyBad               // an empty name can never be opened
grnCopyLp:
    lda (kPtr),y
    sta (srcPtr),y
    beq grnCopyDone              // the manifest's name is NUL padded
    iny
    cpy #11
    bcc grnCopyLp
    lda #0
    sta (srcPtr),y               // a name that filled the field ends here
grnCopyDone:
    clc
    rts
grnCopyBad:
    sec
    rts

// --- grnOpenChunk -- resolve GRN_FNAME into GRN_REC ---
//  Out: C=0 opened; C=1 no such file. Clobbers A, X, Y, kArg, kPtr, srcPtr.
grnOpenChunk:
    jsr grnRec                   // kPtr = the record buffer
    jsr grnName                  // kArg = the name (kReuOpen's own split)
    jsr kReuOpen
    bcs grnOpenBad
    clc
    rts
grnOpenBad:
    sec
    rts

// --- grnTake -- the manifest's scalars, into the instance buffer ---
//  The play loop then touches only dataPtr, and the manifest is reread only for
//  the first chunk's name. Clobbers A, Y, kPtr.
grnTake:
    jsr grnMan
    ldy #GRN_MF_FBYTES
    lda (kPtr),y
    ldy #GRN_FS
    sta (dataPtr),y
    ldy #GRN_MF_FBYTES+1
    lda (kPtr),y
    ldy #GRN_FS+1
    sta (dataPtr),y
    ldy #GRN_MF_FRAMES
    lda (kPtr),y
    ldy #GRN_TOTAL
    sta (dataPtr),y
    ldy #GRN_MF_FRAMES+1
    lda (kPtr),y
    ldy #GRN_TOTAL+1
    sta (dataPtr),y
    ldy #GRN_MF_PER
    lda (kPtr),y
    ldy #GRN_PER
    sta (dataPtr),y
    ldy #GRN_LEFT
    sta (dataPtr),y              // the first chunk is whole
    ldy #GRN_MF_DIGITS
    lda (kPtr),y
    ldy #GRN_DIGITS
    sta (dataPtr),y
    ldy #GRN_MF_FPS+1
    lda (kPtr),y                 // the rate x 256, so the high byte IS the rate
    ldy #GRN_ADD
    sta (dataPtr),y
    rts

// --- grnCheckMan -- a manifest this player can use? C=1 if not ---
//  Only what the player depends on: the magic, the format version, MC, and the
//  frame size the read table is built for. Clobbers A, Y, kPtr.
grnCheckMan:
    jsr grnMan
    ldy #0
grnChkMagic:
    lda (kPtr),y
    cmp grnMagicTab,y
    bne grnChkBad
    iny
    cpy #4
    bne grnChkMagic
    ldy #GRN_MF_VERSION
    lda (kPtr),y
    cmp #GRN_VERSION
    bne grnChkBad
    ldy #GRN_MF_MODE
    lda (kPtr),y
    cmp #GRN_MODE_MC
    bne grnChkBad
    ldy #GRN_MF_FBYTES
    lda (kPtr),y
    cmp #<GRN_PIC_BYTES
    bne grnChkBad
    iny
    lda (kPtr),y
    cmp #>GRN_PIC_BYTES
    bne grnChkBad
    ldy #GRN_MF_DIGITS
    lda (kPtr),y
    beq grnChkBad                // a width of 0 would walk grnBump off the name
    cmp #9
    bcs grnChkBad                // and a width over the name it addresses cannot
    clc
    rts
grnChkBad:
    sec
    rts

// --- grnKeys -- act on a keystroke, if one is waiting ---
//  kReadKey reports 0 for an empty queue. A typed key is a SCREEN CODE, so the
//  letters are $01-$1A and 'q' is not ASCII here. Clobbers A, X, Y.
grnKeys:
    jsr kReadKey
    beq grnKeysDone
    cmp #'q'
    bne grnKeysSpace
    jmp grnQuit
grnKeysSpace:
    cmp #' '
    bne grnKeysPlus
    ldy #GRN_PAUSE
    lda (dataPtr),y
    eor #1
    sta (dataPtr),y
grnKeysDone:
    rts
grnKeysPlus:
    cmp #$2b                     // '+'
    bne grnKeysMinus
    ldy #GRN_SPEED
    lda (dataPtr),y
    cmp #GRN_SPEED_MAX
    bcs grnKeysDone              // already the fastest
    clc
    adc #1
    sta (dataPtr),y
    jmp grnSetSpeed
grnKeysMinus:
    cmp #$2d                     // '-'
    bne grnKeysDone
    ldy #GRN_SPEED
    lda (dataPtr),y
    cmp #GRN_SPEED_MIN
    beq grnKeysDone              // already the slowest
    sec
    sbc #1
    sta (dataPtr),y
grnSetSpeed:
    //  The accumulator's step is the film's rate shifted one bit for each step
    //  away from GRN_SPEED_BASE -- LEFT to run faster, RIGHT to run slower. The
    //  rate is 24 whole frames, so both directions stay exact.
    jsr grnMan
    ldy #GRN_MF_FPS+1
    lda (kPtr),y                 // the film's rate, in whole frames a second
    pha
    ldy #GRN_SPEED
    lda (dataPtr),y
    sec
    sbc #GRN_SPEED_BASE
    beq grnSetDone               // the film's own rate: no shift at all
    bmi grnSetSlow
    tax                          // X = the steps FASTER
    jmp grnSetFast

grnSetSlow:
    eor #$ff
    clc
    adc #1                       // A = the steps SLOWER, as a positive count
    tax
grnSetSlwLp:
    pla
    lsr                          // a slower rate: the step gets smaller
    pha
    dex
    bne grnSetSlwLp
    jmp grnSetDone

grnSetFast:
grnSetFastLp:
    pla
    asl                          // a faster rate: the step gets larger
    pha
    dex
    bne grnSetFastLp

grnSetDone:
    pla
    ldy #GRN_ADD
    sta (dataPtr),y
    rts

// --- grnQuit -- empty the key queue, then exit ---
//  A press leaves its RELEASE behind, and kScanKeyboard reports an empty queue
//  and a release byte identically, so "empty" cannot be tested for: drain the
//  queue's whole capacity. The counter rides the stack because kReadKey clobbers
//  X and Y.
grnQuit:
    ldx #16
grnDrainLp:
    txa
    pha
    jsr kReadKey
    pla
    tax
    dex
    bne grnDrainLp
    // SYS_EXIT frees the surface and hands the display back
    lda #SYS_EXIT
    jsr syscall

// --- grnRefuse -- report on a screen of our own and exit ---
//  A task with no surface can show nothing, so a failure takes one: the message
//  and the name it failed on, held until a key is pressed. Then the display goes
//  back to the parent BEFORE our surface is freed (a surfless exit blanks the
//  display), and the live blink block is un-drawn SEI-wrapped first -- the same
//  handover the film itself uses. In: A/X = message lo/hi, dataPtr = the name.
grnRefuse:
    ldy #GRN_MSGL
    sta (dataPtr),y              // the message pointer waits in our own buffer:
    txa                          // the syscall dispatcher puts a handler address
    iny                          // in srcPtr and the filesystem walks records
    sta (dataPtr),y              // with it, so srcPtr carries nothing across them
    lda #$0b                     // DEFAULT_BORDER_COLOR (dark grey)
    sta kArg
    lda #$00                     // DEFAULT_BG_COLOR (black)
    sta kArg+1
    sta kArg+2
    sta kArg+3
    lda #0                       // mode 0: hires text
    jsr kAllocScreen
    bcs grnRefuseExit            // no free surface -- nothing to say it on
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
    jsr kSwitchView
    plp
    ldy #GRN_MSGL
    lda (dataPtr),y
    sta kPtr
    iny
    lda (dataPtr),y
    sta kPtr+1
    jsr kPrintString
    lda dataPtr                  // the name, as built: "<name>.inf"
    sta kPtr
    lda dataPtr+1
    sta kPtr+1
    jsr kPrintString
    jsr kWaitKey                 // hold the message until it is acknowledged
    lda #SYS_GETTID
    jsr syscall
    cmp kbFocus
    bne grnRefuseFree            // we do not hold the display: nothing to hand back
    tax
    lda taskParent,x
    cmp #$ff
    beq grnRefuseFree            // no parent -- SYS_EXIT's hand-off decides
    tax
    lda taskState,x
    beq grnRefuseFree            // the parent is dead -- same
    jsr kSwitchView
grnRefuseFree:
    jsr kFreeScreen
grnRefuseExit:
    lda #SYS_EXIT
    jsr syscall

// --- pointers into our own instance buffer ---
//  Every kernel call that touches the filesystem clobbers kArg, kPtr, srcPtr and
//  kArg+0..+7, so a pointer is rebuilt from dataPtr at the site that needs it
//  rather than being held. Each routine below clobbers A and the carry only.

// grnRec -- kArg = kPtr = the record buffer
grnRec:
    lda dataPtr
    clc
    adc #GRN_REC
    sta kArg
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kArg+1
    sta kPtr+1
    rts

// grnMan -- kPtr = the manifest
grnMan:
    lda dataPtr
    clc
    adc #GRN_MAN
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    rts

// grnName -- kArg = the current chunk's name
grnName:
    lda dataPtr
    clc
    adc #GRN_FNAME
    sta kArg
    lda dataPtr+1
    adc #0
    sta kArg+1
    rts

// grnFname -- kPtr = the current chunk's name
grnFname:
    lda dataPtr
    clc
    adc #GRN_FNAME
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kPtr+1
    rts

//  The six regions of a .pic record, in the order they are transferred.
//  MATRIX AND COLOUR RAM COME FIRST: the beam reads a line's matrix and colour
//  before that line's pixels, so writing them first keeps every write ahead of
//  the beam -- which is what stops a whole-screen rewrite from showing a seam.
//  2,000 bytes at a time is the largest window the filesystem holds interrupts
//  off for, and each pixels entry is exactly 50 rows ($2000 onward in four steps
//  of $7D0), so a region never straddles the rows it paints.
grnRegionTab:
    .word 8032, 1000, $0400      // the matrix: 40 bytes a line, 25 lines
    .word 9032, 1000, $D800      // colour RAM, the same shape
    .word   32, 2000, $2000      // pixels, rows 0-49
    .word 2032, 2000, $27D0      // pixels, rows 50-99
    .word 4032, 2000, $2FA0      // pixels, rows 100-149
    .word 6032, 2000, $3770      // pixels, rows 150-199

grnMagicTab:
    .byte $47, $41, $4e, $4d     // 'G','A','N','M' -- ASCII, not screen codes

grnMsgUse:     .text "? granim <film>";  .byte 0
grnMsgNoFile:  .text "? not found ";     .byte 0
grnMsgBad:     .text "? not a film ";    .byte 0
grnMsgNoScr:   .text "? no screen";      .byte 0
grnMsgNoChunk: .text "? chunk missing "; .byte 0
