// ==========================================
//  banner.inc -- ".bnr" banner renderer body
// ==========================================
//  "banner <file> <row> <col>" draws the custom-glyph banner file
//  <file>.bnr on the shell's shared screen, then exits. The .bnr
//  format is: byte 0 = c (1-40 columns), byte 1 = r (1-25 rows), then
//  c*r glyph definitions of 8 bytes each, row-major.
//
//  The row buffer is a 2-page kMalloc block -- the header's 1-byte
//  data_size caps instance buffers at 255B, but a 40-column row needs
//  320B. kMalloc blocks are task-owned and freed automatically on
//  exit. Glyph data streams from the REU FS one row at a time via
//  kReuToC64 (like type/runbatch). Glyphs are blitted with kPrintGlyph,
//  which also writes the color nibble (the shell's text color).
//
//  Errors print a message and exit without drawing:
//    usage        ? banner <file> <row> <col>
//    no fs        no fs
//    no memory    ? no memory              (kMalloc failed)
//    not found    ? not found
//    extension    ? banner: no extension
//    c/r invalid  ? banner: bad cols / bad rows
//    placement    ? banner: column / row   (arg out of range or wrap)
//    file short   ? banner: bad file / truncated
//
//  taskParent/reuFs*/cmdArgsBuf come from src/tasks/kernel-data.inc
//  (imported globally by tools/assemble-tasks.asm).

.const BNR_C      = 0          // columns (from file)
.const BNR_R      = 1          // rows (from file)
.const BNR_SC     = 2          // start column (arg)
.const BNR_SR     = 3          // start row (arg)
.const BNR_ROW    = 4          // current row counter
.const BNR_COL    = 5          // current column counter
.const BNR_RBLO   = 6          // row bytes = 8*c
.const BNR_RBHI   = 7
.const BNR_OFFLO  = 8          // file data offset
.const BNR_OFFHI  = 9
.const BNR_POSLO  = 10         // file position (offset + 2 + row*RB)
.const BNR_POSHI  = 11
.const BNR_SIZE   = 12         // file size
.const BNR_SIZEHI = 13
.const BNR_SLOT   = 14         // directory scan slot
.const BNR_BUFLO  = 15         // kMalloc row buffer pointer
.const BNR_BUFHI  = 16
.const BNR_GLO    = 17         // current glyph pointer within the row
.const BNR_GHI    = 18
.const BNR_VAL    = 19         // decimal parse scratch (16-bit)
.const BNR_VALHI  = 20
.const BNR_SVC    = 21         // shell cursor col at entry (restored on exit)
.const BNR_SVR    = 22         // shell cursor row at entry
.const BNR_OFF2   = 23         // file data offset hi (24-bit)
.const BNR_POS2   = 24         // file position hi (24-bit)
.const BNR_SLOTHI = 25         // directory scan slot hi
.const BNR_NAMMAX = 7          // base name <= 7 chars (".bnr" fills 11)

// error indices into bnMsgLo/bnMsgHi
.const BN_USE     = 0
.const BN_NOFS    = 1
.const BN_NOMEM   = 2
.const BN_NO      = 3
.const BN_EXT     = 4
.const BN_BADFILE = 5
.const BN_BADCOLS = 6
.const BN_BADROWS = 7
.const BN_BADCOL  = 8
.const BN_BADROW  = 9
.const BN_TRUNC   = 10

banner:
    // owner = taskParent[own slot] (the shell) -- share its screen
    lda #SYS_GETTID
    jsr syscall                 // gettid -> A
    tax
    lda taskParent,x            // shell slot
    tax
    jsr kShareScreen            // X = shell (preserved); C=0 ok
    bcc bnScr
    lda #SYS_EXIT
    jsr syscall                 // no screen -- exit
bnScr:
    jsr kLockScreen
    // remember the shell's cursor position (where the prompt belongs)
    // and put it back after drawing
    jsr kGetCursor              // X = col, Y = row
    tya
    ldy #BNR_SVR
    sta (dataPtr),y
    txa
    ldy #BNR_SVC
    sta (dataPtr),y
    lda reuFsEnabled
    cmp #$A5
    beq bnFs
    lda #BN_NOFS
    jmp bnErr
bnFs:
    // parse "<file> <row> <col>" -- name first, then the two numbers.
    // The name stays in cmdArgsBuf (srcPtr points at it); the parsed
    // numbers are stashed in the instance buffer, then ".bnr" is
    // appended over the trailing args.
    ldy #0
    jsr bnName                  // srcPtr = name, Y = terminator offset
    bcc bnNameOk
    lda cmdArgsBuf,y            // which failure?
    cmp #'.'
    bne bnUse2
    lda #BN_EXT
    jmp bnErr
bnUse2:
    lda #BN_USE
    jmp bnErr
bnNameOk:
    sty kArg+2                  // remember name-end offset (append later)
    jsr bnSkipSp                // Y = row arg
    bcc bnRowParse
    lda #BN_USE
    jmp bnErr
bnRowParse:
    jsr bnParseDec              // VAL = row, Y past digits
    bcc bnRowVal
    lda #BN_USE
    jmp bnErr
bnRowVal:
    sty kArg+7                  // save offset past row digits (Y is
                                // clobbered by the buffer loads below)
    ldy #BNR_VALHI
    lda (dataPtr),y
    beq bnRowSt
    lda #BN_USE
    jmp bnErr                   // > 255
bnRowSt:
    ldy #BNR_VAL
    lda (dataPtr),y
    ldy #BNR_SR
    sta (dataPtr),y
    ldy kArg+7                  // restore offset past row digits
    jsr bnSkipSp                // Y = col arg
    bcc bnColParse
    lda #BN_USE
    jmp bnErr
bnColParse:
    jsr bnParseDec
    bcc bnColVal
    lda #BN_USE
    jmp bnErr
bnColVal:
    ldy #BNR_VALHI
    lda (dataPtr),y
    beq bnColSt
    lda #BN_USE
    jmp bnErr
bnColSt:
    ldy #BNR_VAL
    lda (dataPtr),y
    ldy #BNR_SC
    sta (dataPtr),y
    // append ".bnr" at cmdArgsBuf[nameEnd] -- row/col are stashed, so
    // overwriting the rest of the args is safe
    ldy kArg+2
    lda #'.'
    sta cmdArgsBuf,y
    iny
    lda #'b'
    sta cmdArgsBuf,y
    iny
    lda #'n'
    sta cmdArgsBuf,y
    iny
    lda #'r'
    sta cmdArgsBuf,y
    iny
    lda #0
    sta cmdArgsBuf,y
    // 2 kMalloc pages for the row buffer (also the entry-name buffer)
    lda #2
    jsr kMalloc
    bcc bnBufOk
    lda #BN_NOMEM
    jmp bnErr
bnBufOk:
    lda kPtr
    ldy #BNR_BUFLO
    sta (dataPtr),y
    lda kPtr+1
    ldy #BNR_BUFHI
    sta (dataPtr),y
    // directory scan -- find the .bnr file
    ldy #BNR_SLOT
    lda #1
    sta (dataPtr),y
    ldy #BNR_SLOTHI
    lda #0
    sta (dataPtr),y
bnScan:
    ldy #BNR_SLOTHI
    lda (dataPtr),y
    cmp #2
    bne bnScanGo
    lda #BN_NO                  // exhausted (idx == 512) -- not found
    jmp bnErr
bnScanGo:
    ldy #BNR_BUFLO
    lda (dataPtr),y
    sta kPtr
    ldy #BNR_BUFHI
    lda (dataPtr),y
    sta kPtr+1                  // kPtr = row buffer (entry-name buffer)
    ldy #BNR_SLOTHI
    lda (dataPtr),y
    tax                         // X = slot hi
    ldy #BNR_SLOT
    lda (dataPtr),y             // A = slot lo
    jsr kReuDirEntry
    bcs bnNext                  // empty/deleted slot
    ldy #0
bnCmpLp:
    lda (kPtr),y
    cmp (srcPtr),y
    bne bnNext
    cmp #0
    beq bnFound                 // both terminated -- match
    iny
    cpy #12
    bcc bnCmpLp
bnFound:
    // stash size + offset (kReuDmaRange/prints would clobber kArg)
    ldy #BNR_SIZE
    lda kArg
    sta (dataPtr),y
    iny
    lda kArg+1
    sta (dataPtr),y
    ldy #BNR_OFFLO
    lda kArg+3
    sta (dataPtr),y
    iny
    lda kArg+4
    sta (dataPtr),y
    ldy #BNR_OFF2
    lda kArg+5
    sta (dataPtr),y
    jmp bnHeader
bnNext:
    ldy #BNR_SLOT
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    ldy #BNR_SLOTHI
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y
    jmp bnScan
bnHeader:
    // file must be at least 2 bytes (c + r)
    ldy #BNR_SIZEHI
    lda (dataPtr),y
    bne bnHdrGo                 // >= 256 bytes
    ldy #BNR_SIZE
    lda (dataPtr),y
    cmp #2
    bcs bnHdrGo
    lda #BN_BADFILE
    jmp bnErr
bnHdrGo:
    // DMA the 2 header bytes into the row buffer
    ldy #BNR_BUFLO
    lda (dataPtr),y
    sta kPtr
    ldy #BNR_BUFHI
    lda (dataPtr),y
    sta kPtr+1
    sei
    ldy #BNR_OFFLO
    lda (dataPtr),y
    sta reuFsPtr
    ldy #BNR_OFFHI
    lda (dataPtr),y
    sta reuFsPtr+1
    ldy #BNR_OFF2
    lda (dataPtr),y
    sta reuFsPtr+2
    lda #2
    sta kArg+6
    lda #0
    sta kArg+7
    jsr kReuDmaRange
    cli
    // c = buf[0] (1-40), r = buf[1] (1-25)
    ldy #BNR_BUFLO
    lda (dataPtr),y
    sta srcPtr
    ldy #BNR_BUFHI
    lda (dataPtr),y
    sta srcPtr+1                // srcPtr = buffer (name compare done)
    ldy #0
    lda (srcPtr),y
    beq bnColsBad
    cmp #41
    bcc bnColsOk
bnColsBad:
    lda #BN_BADCOLS
    jmp bnErr
bnColsOk:
    ldy #BNR_C
    sta (dataPtr),y
    ldy #1
    lda (srcPtr),y
    beq bnRowsBad
    cmp #26
    bcc bnRowsOk
bnRowsBad:
    lda #BN_BADROWS
    jmp bnErr
bnRowsOk:
    ldy #BNR_R
    sta (dataPtr),y
    // rowBytes = 8*c (<= 320)
    lda #0
    sta kArg+1
    ldy #BNR_C
    lda (dataPtr),y
    asl
    rol kArg+1
    asl
    rol kArg+1
    asl
    rol kArg+1
    ldy #BNR_RBLO
    sta (dataPtr),y
    ldy #BNR_RBHI
    lda kArg+1
    sta (dataPtr),y
    // placement: SC <= 39 and SC + C <= 40 (no wrap)
    ldy #BNR_SC
    lda (dataPtr),y
    cmp #40
    bcc bnColAdd
    lda #BN_BADCOL
    jmp bnErr
bnColAdd:
    ldy #BNR_C
    clc
    adc (dataPtr),y             // SC + C <= 79, no wrap
    cmp #41
    bcc bnRowChk
    lda #BN_BADCOL
    jmp bnErr
bnRowChk:
    // placement: SR <= 24 and SR + R <= 25 (no scroll)
    ldy #BNR_SR
    lda (dataPtr),y
    cmp #25
    bcc bnRowAdd
    lda #BN_BADROW
    jmp bnErr
bnRowAdd:
    ldy #BNR_R
    clc
    adc (dataPtr),y             // SR + R <= 49, no wrap
    cmp #26
    bcc bnSizeChk
    lda #BN_BADROW
    jmp bnErr
bnSizeChk:
    // expected = 2 + 8*c*r (16-bit) -- file must hold at least that
    lda #0
    ldy #BNR_VAL
    sta (dataPtr),y
    iny
    sta (dataPtr),y             // VAL = 0
    ldy #BNR_R
    lda (dataPtr),y
    tax                         // r iterations (r >= 1)
    ldy #BNR_C
    lda (dataPtr),y
    sta kArg+4                  // c
bnMulLp:
    ldy #BNR_VAL
    lda (dataPtr),y
    clc
    adc kArg+4
    sta (dataPtr),y
    ldy #BNR_VALHI
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y
    dex
    bne bnMulLp
    ldy #BNR_VAL                // x8
    lda (dataPtr),y
    asl
    sta (dataPtr),y
    ldy #BNR_VALHI
    lda (dataPtr),y
    rol
    sta (dataPtr),y
    ldy #BNR_VAL
    lda (dataPtr),y
    asl
    sta (dataPtr),y
    ldy #BNR_VALHI
    lda (dataPtr),y
    rol
    sta (dataPtr),y
    ldy #BNR_VAL
    lda (dataPtr),y
    asl
    sta (dataPtr),y
    ldy #BNR_VALHI
    lda (dataPtr),y
    rol
    sta (dataPtr),y
    ldy #BNR_VAL                // +2
    lda (dataPtr),y
    clc
    adc #2
    sta (dataPtr),y
    ldy #BNR_VALHI
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y
    // size >= expected?
    ldy #BNR_SIZEHI
    lda (dataPtr),y
    ldy #BNR_VALHI
    cmp (dataPtr),y
    bcc bnTruncErr
    bne bnDraw                  // sizeHi > expHi
    ldy #BNR_SIZE
    lda (dataPtr),y
    ldy #BNR_VAL
    cmp (dataPtr),y
    bcs bnDraw
bnTruncErr:
    lda #BN_TRUNC
    jmp bnErr
bnDraw:
    // POS = OFF + 2 (glyph data starts after the 2 header bytes)
    ldy #BNR_OFFLO
    lda (dataPtr),y
    clc
    adc #2
    ldy #BNR_POSLO
    sta (dataPtr),y
    ldy #BNR_OFFHI
    lda (dataPtr),y
    adc #0
    ldy #BNR_POSHI
    sta (dataPtr),y
    ldy #BNR_OFF2
    lda (dataPtr),y
    adc #0
    ldy #BNR_POS2
    sta (dataPtr),y
    ldy #BNR_ROW
    lda #0
    sta (dataPtr),y
bnRowLp:
    // kSetCursor(SC, SR + ROW) -- load X (col) first, then Y (row)
    ldy #BNR_SC
    lda (dataPtr),y
    tax                         // X = col
    ldy #BNR_SR
    lda (dataPtr),y
    ldy #BNR_ROW
    clc
    adc (dataPtr),y
    tay                         // Y = row
    jsr kSetCursor
    // DMA one row: reuFsPtr = POS, len = rowBytes
    ldy #BNR_BUFLO
    lda (dataPtr),y
    sta kPtr
    ldy #BNR_BUFHI
    lda (dataPtr),y
    sta kPtr+1
    sei
    ldy #BNR_POSLO
    lda (dataPtr),y
    sta reuFsPtr
    ldy #BNR_POSHI
    lda (dataPtr),y
    sta reuFsPtr+1
    ldy #BNR_POS2
    lda (dataPtr),y
    sta reuFsPtr+2
    ldy #BNR_RBLO
    lda (dataPtr),y
    sta kArg+6
    ldy #BNR_RBHI
    lda (dataPtr),y
    sta kArg+7
    jsr kReuDmaRange
    cli
    // blit the row: glyph pointer = rowBuf, col = 0
    ldy #BNR_BUFLO
    lda (dataPtr),y
    ldy #BNR_GLO
    sta (dataPtr),y
    ldy #BNR_BUFHI
    lda (dataPtr),y
    ldy #BNR_GHI
    sta (dataPtr),y
    ldy #BNR_COL
    lda #0
    sta (dataPtr),y
bnGlyphLp:
    ldy #BNR_GLO
    lda (dataPtr),y
    sta kPtr
    ldy #BNR_GHI
    lda (dataPtr),y
    sta kPtr+1
    jsr kPrintGlyph
    ldy #BNR_GLO                // glyph pointer += 8
    lda (dataPtr),y
    clc
    adc #8
    sta (dataPtr),y
    ldy #BNR_GHI
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y
    ldy #BNR_COL                // col++
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    ldy #BNR_C
    cmp (dataPtr),y
    bcc bnGlyphLp
    // row done -- POS += rowBytes (24-bit)
    ldy #BNR_RBLO
    lda (dataPtr),y
    ldy #BNR_POSLO
    clc
    adc (dataPtr),y
    sta (dataPtr),y
    ldy #BNR_RBHI
    lda (dataPtr),y
    ldy #BNR_POSHI
    adc (dataPtr),y
    sta (dataPtr),y
    ldy #BNR_POS2
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y
    ldy #BNR_ROW                // ROW++
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    ldy #BNR_R
    cmp (dataPtr),y
    bcs bnDrawDone
    jmp bnRowLp
bnDrawDone:
    // restore the shell's cursor -- the prompt lands where it belongs
    ldy #BNR_SVC
    lda (dataPtr),y
    tax
    ldy #BNR_SVR
    lda (dataPtr),y
    tay
    jsr kSetCursor              // X = col, Y = row
    jmp bnExit

// --- bnErr -- print error A + newline, then exit ---
bnErr:
    tax
    lda bnMsgLo,x
    sta kPtr
    lda bnMsgHi,x
    sta kPtr+1
    jsr kPrintString
    jsr kNewline
bnExit:
    jsr kUnlockScreen
    lda #SYS_EXIT
    jsr syscall                 // exit -- never returns

// --- bnSkipSp -- skip spaces in cmdArgsBuf from Y ---
//  Output: C=0 + Y = offset of the next non-space, non-NUL char
//          C=1 end of args. Clobbers: A
bnSkipSp:
    lda cmdArgsBuf,y
    beq bnSpEnd
    cmp #$20
    bne bnSpOk
    iny
    bne bnSkipSp
bnSpEnd:
    sec
    rts
bnSpOk:
    clc
    rts

// --- bnName -- scan the filename in cmdArgsBuf[Y] (Y=0) ---
//  Output: C=0: srcPtr = name start in cmdArgsBuf, Y = offset of the
//          terminating space/NUL, name <= 7 chars
//          C=1: empty / too long / contains '.' (caller re-checks the
//               char at cmdArgsBuf[Y] for the exact error)
//  Clobbers: A, Y, srcPtr
bnName:
bnNmSkip:
    lda cmdArgsBuf,y
    beq bnNmBad                 // empty -- usage
    cmp #$20
    bne bnNmStart
    iny
    bne bnNmSkip
bnNmStart:
    tya
    sta kArg+7                  // name start offset (kArg free here)
    clc
    adc #<cmdArgsBuf
    sta srcPtr
    lda #>cmdArgsBuf
    adc #0
    sta srcPtr+1                // srcPtr = name start
    ldy #0
bnNmScan:
    lda (srcPtr),y
    beq bnNmEnd
    cmp #$20
    beq bnNmEnd
    cmp #'.'
    beq bnNmBad                 // explicit extension -- refuse
    iny
    cpy #BNR_NAMMAX
    bcc bnNmScan
    // exactly 7 chars consumed -- the next byte must terminate the name
    // (a real 8th character means the name is too long)
    lda (srcPtr),y
    beq bnNmEnd
    cmp #$20
    beq bnNmEnd
bnNmBad:
    sec
    rts
bnNmEnd:
    // Y = name length (1..7); terminator offset in cmdArgsBuf = start + Y
    cpy #0
    beq bnNmBad
    tya
    clc
    adc kArg+7                  // + start offset
    tay
    clc
    rts

// --- bnParseDec -- parse decimal from cmdArgsBuf[Y] into VAL/VALHI ---
//  Input:  Y = cmdArgsBuf offset of the first digit
//  Output: C=0: VAL/VALHI = value, Y = offset past the last digit
//          C=1: no digits / non-digit. Clobbers: A, Y, kArg+4..kArg+7
bnParseDec:
    sty kArg+4                  // save cmdArgsBuf offset (Y = input)
    lda #0
    ldy #BNR_VAL
    sta (dataPtr),y
    iny
    sta (dataPtr),y             // VAL = 0
    pha                         // digits-seen flag = 0 (stack)
    ldy kArg+4                  // restore offset
bnPdLp:
    lda cmdArgsBuf,y
    beq bnPdDone
    cmp #$20
    beq bnPdDone
    sec
    sbc #'0'
    bcc bnPdBad
    cmp #10
    bcs bnPdBad
    sta kArg+5                  // digit
    sty kArg+4                  // save offset
    tsx
    lda #1
    sta $0101,x                 // digits-seen = 1 (tsx clobbers X -- unused)
    // VAL = VAL*10 + digit (16-bit, scratch kArg+6/7)
    ldy #BNR_VAL
    lda (dataPtr),y
    asl
    sta kArg+6
    ldy #BNR_VALHI
    lda (dataPtr),y
    rol
    sta kArg+7                  // x2
    asl kArg+6
    rol kArg+7                  // x4
    asl kArg+6
    rol kArg+7                  // x8
    ldy #BNR_VAL
    lda (dataPtr),y
    clc
    adc kArg+6
    sta kArg+6
    ldy #BNR_VALHI
    lda (dataPtr),y
    adc kArg+7
    sta kArg+7                  // x10
    lda kArg+5
    clc
    adc kArg+6
    sta kArg+6
    lda kArg+7
    adc #0
    sta kArg+7                  // + digit
    lda kArg+6
    ldy #BNR_VAL
    sta (dataPtr),y
    lda kArg+7
    ldy #BNR_VALHI
    sta (dataPtr),y
    ldy kArg+4                  // restore offset
    iny
    bne bnPdLp
bnPdDone:
    pla                         // digits-seen
    bne bnPdOk
    sec
    rts
bnPdOk:
    clc
    rts
bnPdBad:
    pla                         // balance the flag
    sec
    rts

// --- error message pointers (lo/hi tables -- the fixup table relocates ---
//     the hi bytes like any other absolute reference) ---
bnMsgLo:
    .byte <bnUseStr, <bnNoFsStr, <bnNoMemStr, <bnNoStr, <bnExtStr
    .byte <bnBadFileStr, <bnBadColsStr, <bnBadRowsStr, <bnBadColStr
    .byte <bnBadRowStr, <bnTruncStr
bnMsgHi:
    .byte >bnUseStr, >bnNoFsStr, >bnNoMemStr, >bnNoStr, >bnExtStr
    .byte >bnBadFileStr, >bnBadColsStr, >bnBadRowsStr, >bnBadColStr
    .byte >bnBadRowStr, >bnTruncStr

.encoding "screencode_mixed"
bnUseStr:     .text "? banner <file> <row> <col>"; .byte 0
bnNoFsStr:    .text "no fs"; .byte 0
bnNoMemStr:   .text "? no memory"; .byte 0
bnNoStr:      .text "? not found"; .byte 0
bnExtStr:     .text "? banner: no extension"; .byte 0
bnBadFileStr: .text "? banner: bad file"; .byte 0
bnBadColsStr: .text "? banner: bad cols"; .byte 0
bnBadRowsStr: .text "? banner: bad rows"; .byte 0
bnBadColStr:  .text "? banner: column"; .byte 0
bnBadRowStr:  .text "? banner: row"; .byte 0
bnTruncStr:   .text "? banner: truncated"; .byte 0
