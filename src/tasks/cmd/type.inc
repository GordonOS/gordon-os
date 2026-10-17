// ==========================================
//  type.inc -- REU-FS file dump body
// ==========================================
//  Prints a file's raw screen-code bytes on the shell's shared screen.
//  Full filename comes from cmdArgsBuf (the shell's pcFgPs copies the
//  args); it is NOT extended -- "type boot.bat" shows boot.bat.
//  Streaming mirrors runbatch: 80-byte DMA windows via kReuToC64,
//  pos/size/offset kept in the instance buffer. Rendering rules:
//  CR followed by NUL = line terminator -> newline; CR followed by
//  anything else = the screen code for 'm' -> printed as a glyph; NUL
//  is skipped. A CR at the last window byte is deferred (TYPE_PEND) --
//  its follower lives in the next window. The byte cursor lives in the
//  instance buffer (not srcPtr) because kPrintChar -> kScrollScreen
//  clobbers srcPtr mid-dump.
//  taskParent/reuFs*/cmdArgsBuf come from src/tasks/kernel-data.inc
//  (imported globally by tools/assemble-tasks.asm).

.const TYPE_WIN  = 0           // 80 bytes: DMA window / entry-name buffer
.const TYPE_POS  = 80          // 2 bytes: position within the file
.const TYPE_SIZE = 82          // 2 bytes: file size
.const TYPE_OFF  = 84          // 3 bytes: file FS data offset (lo/mid/hi)
.const TYPE_SLOT = 86          // 1 byte:  directory scan slot lo
.const TYPE_LEN  = 87          // 1 byte:  window length
.const TYPE_END  = 88          // 2 bytes: window end pointer
.const TYPE_CUR  = 90          // 2 bytes: print byte cursor
.const TYPE_PEND = 92          // 1 byte:  CR deferred across a window edge
.const TYPE_OFF2 = 93          // 1 byte:  file FS data offset hi
.const TYPE_SLOTHI = 94        // 1 byte:  directory scan slot hi

type:
    // owner = taskParent[own slot] (the shell)
    lda #SYS_GETTID
    jsr syscall                 // gettid -> A
    tax
    lda taskParent,x            // shell slot
    tax
    jsr kShareScreen            // X = shell (preserved); C=0 ok
    bcc tyScr
    lda #SYS_EXIT
    jsr syscall                 // no screen -- exit
tyScr:
    jsr kLockScreen
    lda reuFsEnabled
    cmp #$A5
    beq tyFs
    lda #<tyNoFs
    sta kPtr
    lda #>tyNoFs
    sta kPtr+1
    jsr kPrintString
    jsr kNewline; jsr kNewline
    jmp tyExit
tyFs:
    // srcPtr = filename in cmdArgsBuf (skip spaces, cap 11 chars)
    ldy #0
tySkSp:
    lda cmdArgsBuf,y
    bne tySkChk
    jmp tyBad                   // no filename
tySkChk:
    cmp #$20
    bne tyName
    iny
    bne tySkSp
tyName:
    tya
    clc
    adc #<cmdArgsBuf
    sta srcPtr
    lda #>cmdArgsBuf
    adc #0
    sta srcPtr+1                // srcPtr = name start
    ldy #0
tyLenLp:
    lda (srcPtr),y
    beq tyHave
    cmp #$20
    beq tyHave                  // trailing spaces
    iny
    cpy #11
    bcc tyLenLp
tyHave:
    lda #0
    sta (srcPtr),y              // null-terminate (trims trailing spaces)
    // directory scan -- kPtr = entry-name buffer (reuse the window)
    ldy #TYPE_SLOT
    lda #1
    sta (dataPtr),y
    ldy #TYPE_SLOTHI
    lda #0
    sta (dataPtr),y
tyScan:
    ldy #TYPE_SLOTHI
    lda (dataPtr),y
    cmp #2
    bne tyScanGo
    jmp tyNotFound              // exhausted (idx == 512): not found
tyScanGo:
    lda dataPtr; clc; adc #TYPE_WIN; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    ldy #TYPE_SLOTHI
    lda (dataPtr),y
    tax                         // X = slot hi
    ldy #TYPE_SLOT
    lda (dataPtr),y             // A = slot lo
    jsr kReuDirEntry
    bcs tyNext                  // empty/deleted slot
    ldy #0
tyCmpLp:
    lda (kPtr),y
    cmp (srcPtr),y
    bne tyNext
    cmp #0
    beq tyFound                 // both terminated -- match
    iny
    cpy #12
    bcc tyCmpLp
tyFound:
    // stash size + offset (kReuDirEntry outputs; prints clobber kArg)
    ldy #TYPE_SIZE
    lda kArg
    sta (dataPtr),y
    iny
    lda kArg+1
    sta (dataPtr),y
    ldy #TYPE_OFF
    lda kArg+3
    sta (dataPtr),y
    iny
    lda kArg+4
    sta (dataPtr),y
    ldy #TYPE_OFF2
    lda kArg+5
    sta (dataPtr),y
    jmp tyRun
tyNext:
    ldy #TYPE_SLOT
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    ldy #TYPE_SLOTHI
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y
    jmp tyScan
tyRun:
    ldy #TYPE_POS
    lda #0
    sta (dataPtr),y
    iny
    sta (dataPtr),y             // pos = 0
tyLine:
    // done when pos >= size (16-bit)
    ldy #TYPE_POS+1
    lda (dataPtr),y
    ldy #TYPE_SIZE+1
    cmp (dataPtr),y
    bcc tyMore
    bne tyDone
    ldy #TYPE_POS
    lda (dataPtr),y
    ldy #TYPE_SIZE
    cmp (dataPtr),y
    bcc tyMore
tyDone:
    jsr kNewline
    jsr kNewline
    jmp tyExit
tyMore:
    // window len = min(80, size - pos)
    ldy #TYPE_SIZE+1
    lda (dataPtr),y
    ldy #TYPE_POS+1
    sec
    sbc (dataPtr),y
    bne tyLen80                 // >= 256 left -> full window
    ldy #TYPE_SIZE
    lda (dataPtr),y
    ldy #TYPE_POS
    sec
    sbc (dataPtr),y
    cmp #80
    bcc tyLenSet
tyLen80:
    lda #80
tyLenSet:
    ldy #TYPE_LEN
    sta (dataPtr),y             // save length
    sei
    sta kArg+6                  // length lo
    lda #0
    sta kArg+7                  // length hi
    // reuFsPtr = offset(3) + pos(2)
    ldy #TYPE_OFF
    lda (dataPtr),y             // offset lo
    ldy #TYPE_POS
    clc
    adc (dataPtr),y             // + pos lo (carry -> mid)
    sta reuFsPtr
    ldy #TYPE_OFF+1
    lda (dataPtr),y             // offset mid
    ldy #TYPE_POS+1
    adc (dataPtr),y             // + pos hi (carry from lo chain)
    sta reuFsPtr+1
    ldy #TYPE_OFF2
    lda (dataPtr),y             // offset hi
    adc #0                      // + carry
    sta reuFsPtr+2
    lda dataPtr
    sta kPtr                    // dest = window at (dataPtr)
    lda dataPtr+1
    sta kPtr+1
    jsr kReuDmaRange
    cli
    // print the window: cursor = window start, end = start + len.
    // The cursor lives in the instance buffer because kPrintChar ->
    // kScrollScreen clobbers srcPtr/kPtr.
    lda dataPtr
    ldy #TYPE_CUR
    sta (dataPtr),y
    lda dataPtr+1
    iny
    sta (dataPtr),y             // cursor = dataPtr
    ldy #TYPE_LEN
    lda (dataPtr),y
    clc
    adc dataPtr
    ldy #TYPE_END
    sta (dataPtr),y             // end lo
    lda dataPtr+1
    adc #0
    iny
    sta (dataPtr),y             // end hi
tyPrLp:
    ldy #TYPE_CUR+1
    lda (dataPtr),y             // cursor hi
    ldy #TYPE_END+1
    cmp (dataPtr),y             // end hi
    beq tyPrChkLo
    jmp tyPrGo                  // cursor hi < end hi -> print (jmp: out of range)
tyPrChkLo:
    ldy #TYPE_CUR
    lda (dataPtr),y             // cursor lo
    ldy #TYPE_END
    cmp (dataPtr),y             // end lo
    beq tyPrDone
    jmp tyPrGo                  // cursor lo < end lo -> print
tyPrDone:
    jmp tyNextWin               // cursor == end -> window done
tyPrGo:
    ldy #TYPE_CUR
    lda (dataPtr),y
    sta kPtr
    ldy #TYPE_CUR+1
    lda (dataPtr),y
    sta kPtr+1                  // kPtr = cursor
    ldy #0
    lda (kPtr),y                // byte
    pha
    ldy #TYPE_PEND
    lda (dataPtr),y             // CR deferred from the previous window?
    beq tyPrGoByte
    lda #0
    sta (dataPtr),y
    pla
    bne tyPendM                 // pending CR was followed by real text
    jmp tyNewline               // CR+NUL split at the window edge
tyPendM:                        // pending CR was an 'm'
    pha
    lda #$0D
    jsr kPrintChar
    pla
    jmp tyPrintIt
tyPrGoByte:
    pla
    cmp #$0D
    bne tyChkNul
    // CR -- the window's LAST byte cannot be decided here (its follower
    // is in the next window) -> defer it.
    ldy #TYPE_CUR
    lda (dataPtr),y
    clc
    adc #1
    ldy #TYPE_END
    cmp (dataPtr),y
    bne tyCrLook
    ldy #TYPE_CUR+1
    lda (dataPtr),y
    adc #0
    ldy #TYPE_END+1
    cmp (dataPtr),y
    bne tyCrLook
    ldy #TYPE_PEND
    lda #1
    sta (dataPtr),y
    jmp tyAdvance
tyCrLook:                       // the next byte decides the CR's meaning
    ldy #1
    lda (kPtr),y
    beq tyNewline               // CR+NUL = line terminator
    ldy #0
    lda (kPtr),y                // reload the CR -- print IT, not the lookahead
    jmp tyPrintIt               // CR = 'm' glyph
tyChkNul:
    cmp #0
    beq tyAdvance               // NUL -> skip
tyPrintIt:
    jsr kPrintChar
    jmp tyAdvance
tyNewline:
    jsr kNewline
tyAdvance:
    // cursor++
    ldy #TYPE_CUR
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    ldy #TYPE_CUR+1
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y
    jmp tyPrLp
tyNextWin:
    // pos += len
    ldy #TYPE_POS
    lda (dataPtr),y
    ldy #TYPE_LEN
    clc
    adc (dataPtr),y
    ldy #TYPE_POS
    sta (dataPtr),y
    ldy #TYPE_POS+1
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y
    jmp tyLine
tyBad:
    lda #<tyUse
    sta kPtr
    lda #>tyUse
    sta kPtr+1
    jsr kPrintString
    jsr kNewline; jsr kNewline
    jmp tyExit
tyNotFound:
    lda #<tyNf
    sta kPtr
    lda #>tyNf
    sta kPtr+1
    jsr kPrintString
    jsr kNewline; jsr kNewline
tyExit:
    jsr kUnlockScreen
    lda #SYS_EXIT
    jsr syscall
    // never returns

tyNoFs:  .text "no fs"; .byte 0
tyUse:   .text "? type <filename>"; .byte 0
tyNf:    .text "? not found"; .byte 0
