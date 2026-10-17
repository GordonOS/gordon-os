// ==========================================
//  grview.inc -- body of the "grview" task
// ==========================================
//  Puts a .pic file on the display: the 32-byte header supplies the mode and
//  the palette, then the three payload regions go straight to the homes the VIC
//  reads ($2000 pixels, $0400 matrix, $D800 colour RAM). 'm' shows the same
//  bytes in the other mode; any other key exits.
//
//  The header's label is NOT printed: a 1-bit glyph blit is unreadable on a
//  multicolour surface, and either mode would have it overwrite the picture it
//  came with. It stays metadata.
//
//  A failure is reported on a screen of our own and held until a key is pressed
//  -- a task with no screen can show nothing, and on the shell's screen the
//  message would go wherever the shell's cursor happens to be.

.encoding "screencode_mixed"

// The kernel's bitmap-mode tags: they are what screenKind holds, and they are
// the mode byte of a .pic -- kAllocScreen and kSetGfxMode take the mode as a
// 0/1 offset from the hires tag. (GRV_ prefix: imported task files share one
// assembler namespace.)
.const GRV_MODE_STD = 2          // hires (320x200) -- kernel BMPMODE_STD
.const GRV_MODE_MC  = 3          // multicolour (160x200) -- kernel BMPMODE_MC

// The .pic format (docs/pic-format.md)
.const PIC_BYTES    = 10032      // header + 8000 pixels + 1000 matrix + 1000 cram
.const PIC_VERSION  = 1
.const PIC_HDR_LAST = 31         // its last byte: zero padding, so always 0
.const PIC_PIX_POS  = 32
.const PIC_PIX_LEN  = 8000
.const PIC_MAT_POS  = 8032
.const PIC_MAT_LEN  = 1000
.const PIC_CRAM_POS = 9032
.const PIC_CRAM_LEN = 1000

grview:                          // ENTRY -- the first label in the body
    // --- the first argument word is the base name, and the suffix is appended
    //     to it in place: `run grview mc` opens mc.pic. A base name is at most
    //     7 characters. ---
    ldy #0
grvScan:
    lda (dataPtr),y
    beq grvNameEnd               // end of the arguments
    cmp #$20
    beq grvNameEnd               // the next token starts here
    iny
    cpy #7
    bcc grvScan
grvNameEnd:
    tya
    bne grvAppend
    lda #0
    sta (dataPtr),y              // nothing to open, and no name to report
    lda #<grvMsgUse
    ldx #>grvMsgUse
    jmp grvRefuse
grvAppend:
    //  The suffix is part of the FS name, so it is written as screen codes
    //  like every other name -- '.', 'p', 'i', 'c' and the terminator.
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
    sta (dataPtr),y              // null-terminate
    // --- resolve the file. Every refusal below is reached with NO surface, so
    //     none of them takes the display or disturbs the shell's screen ---
    jsr grvRec                   // kPtr = the record buffer
    lda dataPtr
    sta kArg
    lda dataPtr+1
    sta kArg+1                   // kArg = the name (offset 0 of the buffer)
    jsr kReuOpen
    bcc grvFound
    lda #<grvMsgNoFile
    ldx #>grvMsgNoFile
    jmp grvRefuse

grvFound:
    jsr grvCheck                 // C=1: not a picture
    bcc grvHdrOk
    lda #<grvMsgBad
    ldx #>grvMsgBad
    jmp grvRefuse

    // --- the picture's own mode and palette: kAllocScreen takes the colours in
    //     kArg..kArg+3 and the mode in A (0 = hires, 1 = MC) ---
grvHdrOk:
    jsr grvHdr                   // kPtr = the header
    ldy #12
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
    ldy #5
    lda (kPtr),y
    sec
    sbc #GRV_MODE_STD            // the file's tag -> the call's mode
    jsr kAllocScreen
    bcc grvScreen
    lda #<grvMsgNoScr
    ldx #>grvMsgNoScr
    jmp grvRefuse                // out of surface slots

grvScreen:
    jsr kDisableCursor           // a picture shows no blink block

    //  TAKE THE DISPLAY WITHOUT TAKING THE OUTGOING CURSOR BLOCK WITH IT.
    //  The blink block is drawn INTO the framebuffer pixels, and kSwitchView
    //  copies the framebuffer into the outgoing surface's REU slot (surfSave),
    //  so a block drawn at this instant is baked into the shell's saved image
    //  and dgsDone then clears cursorVisible: nothing restores that cell, and
    //  the shell shows a phantom cursor where its cursor was. kBlinkReset
    //  erases the block at the SAVED position of the displayed surface, which
    //  is still the shell's. The SEI keeps an IRQ tick from drawing it straight
    //  back, and no syscall may run inside the window (the dispatcher
    //  re-enables IRQs). kBlinkReset clobbers A, X, Y, kPtr, kArg+7.
    lda #SYS_GETTID
    jsr syscall
    tax                          // X = our slot: kSwitchView displays it
    php
    sei
    txa
    pha
    jsr kBlinkReset
    pla
    tax
    jsr kSwitchView
    plp

    // --- the payload, straight to the display's own homes: three kReuReadAt
    //     calls, no staging buffer. X carries the region index (6 bytes per
    //     entry) and is parked on the stack because kReuReadAt clobbers X --
    //     PLA leaves the carry alone, so the error branch still sees it. ---
    ldx #0
grvLoadLp:
    txa
    pha                          // park the region index: kReuReadAt clobbers X
    jsr grvRec
    txa                          // A = the same index -- the parked byte is read
    tay                          //   ONCE, by the pla below, never here
    lda grvRegionTab,y
    sta kArg+2                   // pos lo
    iny
    lda grvRegionTab,y
    sta kArg+3                   // pos hi
    iny
    lda grvRegionTab,y
    sta kArg+6                   // len lo
    iny
    lda grvRegionTab,y
    sta kArg+7                   // len hi
    iny
    lda grvRegionTab,y
    sta kPtr                     // dest lo
    iny
    lda grvRegionTab,y
    sta kPtr+1                   // dest hi
    jsr kReuReadAt
    pla
    tax                          // the region index back
    bcs grvExit                  // a read failed: leave, say nothing
    txa
    clc
    adc #6
    tax
    cpx #18
    bne grvLoadLp

    // --- the label is metadata and is not printed ---

grvLoop:
    jsr kReadKey                 // focus-guarded: only keys aimed at us arrive
    beq grvIdle
    cmp #'m'                     // 'm' is a screen code, like every typed key
    beq grvMode
    jmp grvQuit                  // any other key exits
grvIdle:
    lda #SYS_YIELD
    jsr syscall
    jmp grvLoop

// --- grvMode -- reinterpret the same bytes in the other mode ---
//  kSetGfxMode keeps the pixels, the matrix, the colour RAM and the palette;
//  only the mode changes, which is what makes the tag a tag and not a
//  conversion.
grvMode:
    lda #SYS_GETTID
    jsr syscall
    tax
    lda screenKind,x             // 2 = hires, 3 = MC (per-task, kernel data)
    eor #1                       // 2 <-> 3
    sec
    sbc #GRV_MODE_STD            // -> 0 / 1 for the call
    jsr kSetGfxMode
    jmp grvLoop

// --- grvQuit -- empty the key queue, then exit ---
//  A press leaves its RELEASE behind, and a release is a byte the queue holds
//  like any other; kScanKeyboard reports an empty queue and a release
//  identically, so "empty" cannot be tested for -- drain a fixed 16 reads,
//  which is the queue's capacity. The counter rides the stack because kReadKey
//  clobbers X and Y.
grvQuit:
    ldx #16
grvDrainLp:
    txa
    pha
    jsr kReadKey
    pla
    tax
    dex
    bne grvDrainLp
    // SYS_EXIT frees the surface and hands the display back
grvExit:
    lda #SYS_EXIT
    jsr syscall

// --- grvRefuse -- report on a screen of our own and exit ---
//  A task with no surface can show nothing, so a failure takes one: the message
//  and the name it failed on, held until a key is pressed. Then the display goes
//  back to the parent BEFORE our surface is freed (a surfless exit blanks the
//  display), and the live blink block is un-drawn SEI-wrapped first -- the same
//  handover the viewer itself uses. In: A/X = message lo/hi, and dataPtr is the
//  name we built.
grvRefuse:
    ldy #GRV_MSGL
    sta (dataPtr),y              // The message pointer waits in our own buffer: the
    txa                          // syscall dispatcher puts a handler address in
    iny                          // srcPtr and the FS library walks file records
    sta (dataPtr),y              // with it, so srcPtr carries nothing across them.
    lda #$0b                     // DEFAULT_BORDER_COLOR (dark grey)
    sta kArg
    lda #$00                     // DEFAULT_BG_COLOR (black)
    sta kArg+1
    sta kArg+2
    sta kArg+3
    lda #0                       // mode 0: hires text
    jsr kAllocScreen
    bcs grvExit                  // no free surface -- nothing to say it on
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
    jsr kSwitchView              // display and keyboard focus move to us
    plp
    ldy #GRV_MSGL
    lda (dataPtr),y
    sta kPtr
    iny
    lda (dataPtr),y
    sta kPtr+1
    jsr kPrintString
    lda dataPtr                  // the name, as built: "<name>.pic"
    sta kPtr
    lda dataPtr+1
    sta kPtr+1
    jsr kPrintString
    jsr kWaitKey                 // hold the message until it is acknowledged
    lda #SYS_GETTID
    jsr syscall
    cmp kbFocus
    bne grvRefuseFree            // we do not hold the display: nothing to hand back
    tax
    lda taskParent,x
    cmp #$ff
    beq grvRefuseFree            // no parent -- SYS_EXIT's hand-off decides
    tax
    lda taskState,x
    beq grvRefuseFree            // the parent is dead -- same
    jsr kSwitchView              // X = the parent
grvRefuseFree:
    jsr kFreeScreen
    jmp grvExit

// --- grvCheck -- validate the file and read its header into our buffer ---
//  In:  kPtr = the file record (kReuOpen just wrote it)
//  Out: C=0 valid and GRV_HDR filled; C=1 not a picture.
//  The format is fixed-size, so a record of any other size is refused before a
//  byte of it is read. The header is addressed through kPtr and re-derived
//  before every use: kReuReadAt uses srcPtr to walk the file record, so a
//  pointer kept in srcPtr across the call does not survive it.
//  Clobbers A, X, Y, kPtr, kArg.
grvCheck:
    ldy #0
    lda (kPtr),y
    cmp #<PIC_BYTES
    bne grvChkBad
    iny
    lda (kPtr),y
    cmp #>PIC_BYTES
    bne grvChkBad
    jsr grvRec                   // kArg = the record (kReuReadAt's view)
    lda #0
    sta kArg+2
    sta kArg+3                   // pos = 0
    lda #32
    sta kArg+6
    lda #0
    sta kArg+7                   // len = the header
    jsr grvHdr                   // kPtr = the header buffer = the DMA dest
    jsr kReuReadAt
    bcs grvChkBad
    jsr grvHdr                   // kPtr is ours to re-derive after the call
    ldy #0
grvChkMagic:
    lda (kPtr),y
    cmp grvMagicTab,y
    bne grvChkBad
    iny
    cpy #4
    bne grvChkMagic
    ldy #4
    lda (kPtr),y
    cmp #PIC_VERSION
    bne grvChkBad
    ldy #5
    lda (kPtr),y                 // mode: GRV_MODE_STD (2) or GRV_MODE_MC (3)
    cmp #GRV_MODE_STD
    bcc grvChkBad
    cmp #GRV_MODE_MC + 1
    bcs grvChkBad
    ldy #PIC_HDR_LAST
    lda (kPtr),y                 // the label field is zero-padded, so its last
    bne grvChkBad                // byte is 0 -- the print cannot run off it
    clc
    rts
grvChkBad:
    sec
    rts

// --- grvHdr -- kPtr = the 32-byte header buffer in our instance buffer ---
//  Never srcPtr: the FS library uses srcPtr to walk the file record. Clobbers A.
grvHdr:
    lda dataPtr
    clc
    adc #<GRV_HDR
    sta kPtr
    lda dataPtr+1
    adc #>GRV_HDR
    sta kPtr+1
    rts

// --- grvRec -- kArg = kPtr = the file record in our instance buffer ---
//  kReuReadAt takes the record in kArg; kReuOpen takes the name in kArg and
//  the BUFFER in kPtr, so its call site points kArg at the name afterwards.
//  Every call clobbers kArg, so it is rebuilt from dataPtr at each call site.
//  Clobbers A.
grvRec:
    lda dataPtr
    clc
    adc #GRV_REC
    sta kArg
    sta kPtr
    lda dataPtr+1
    adc #0
    sta kArg+1
    sta kPtr+1
    rts

//  pos / len / dest for the three payload regions. A task that owns the display
//  has its surface AT those destinations, so nothing has to be staged.
grvRegionTab:
    .word PIC_PIX_POS,  PIC_PIX_LEN,  $2000
    .word PIC_MAT_POS,  PIC_MAT_LEN,  $0400
    .word PIC_CRAM_POS, PIC_CRAM_LEN, $D800

grvMagicTab:
    .byte $47, $50, $49, $43     // 'G','P','I','C' -- ASCII, not screen codes

grvMsgUse:    .text "? grview <file.pic>"; .byte 0
grvMsgNoFile: .text "? not found ";         .byte 0
grvMsgBad:    .text "? not a picture ";     .byte 0
grvMsgNoScr:  .text "? no screen";         .byte 0
