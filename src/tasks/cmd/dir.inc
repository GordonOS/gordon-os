// ==========================================
//  dir.inc -- REU-FS directory listing body
// ==========================================
//  Prints name + size for each FS entry (skipping slot 0), then a
//  footer "N file(s) $xxxx free". Uses dataPtr scratch instead
//  of kArg+7 so it does not depend on kArg surviving kReuDirEntry
//  (which clobbers kArg+2..kArg+7).
//  Options (from cmdArgsBuf):
//    /w -- wide: two 17-char columns per line, two spaces between
//    /p -- paged: every 24 lines print "press any key to continue..."
//         and wait for a keypress (consumed from the keyboard ring
//         buffer under SEI -- a command task never owns kbFocus, so
//         the kernel's kWaitKey would spin forever)
//    <pattern> -- wildcard filter ('*' any run, '?' any single char);
//         options and pattern combine in any order, e.g. "dir /w *.com"
//         or "dir *.com /p"; matching delegates to string.lib's
//         kStrcmp (wildcard mode, Z=1 match)
//  taskParent/reuFs*/cmdArgsBuf/keybCount/keybTail/keybBuffer come from
//  src/tasks/kernel-data.inc (imported globally by assemble-tasks.asm).

.const DIR_IDX     = 0          // entry index (1..255)
.const DIR_LINEBUF = 1          // 12 bytes: entry name buffer
.const DIR_DECBUF  = 13         // 6 bytes: hex/decimal conversion buffer
.const DIR_PAD     = 19         // 1 byte:  filename pad count
.const DIR_SZLO    = 20         // 1 byte:  saved size lo
.const DIR_SZHI    = 21         // 1 byte:  saved size hi
.const DIR_MODE    = 22         // 0=plain, 1=wide (/w), 2=paged (/p)
.const DIR_LINES   = 23         // lines printed since last /p pause
.const DIR_PATTERN = 24         // 12 bytes: wildcard pattern + null
.const DIR_IDXHI   = 36         // 1 byte:  entry index hi (0..1)

dir:
    // owner = taskParent[own slot] (the shell)
    lda #SYS_GETTID
    jsr syscall                 // gettid -> A
    tax
    lda taskParent,x            // shell slot
    tax
    jsr kShareScreen            // X = shell (preserved); C=0 ok
    bcc dirScr
    lda #SYS_EXIT
    jsr syscall                 // no screen -- exit
dirScr:
    jsr kLockScreen
    lda reuFsEnabled
    cmp #$A5
    beq dirGo
    lda #<dirNoFs
    sta kPtr
    lda #>dirNoFs
    sta kPtr+1
    jsr kPrintString
    jsr kNewline
    jsr kNewline
    jmp dirUnlock
dirGo:
    // Args: options /w and /p plus an optional wildcard pattern, in
    // any order (e.g. "dir *.com /p" or "dir /w *.com"). Repeated
    // options are fine (last one wins); the first non-option token is
    // the pattern.
    ldy #DIR_MODE
    lda #0
    sta (dataPtr),y             // mode = plain
    ldy #DIR_LINES
    sta (dataPtr),y             // page line counter = 0
    ldy #DIR_PATTERN
    sta (dataPtr),y             // pattern[0] = 0 -> match everything
    ldx #0                      // X = cmdArgsBuf index
dirArgSk:
    lda cmdArgsBuf,x
    beq dirInit                 // nothing left
    cmp #$20
    bne dirArgTok
    inx
    bne dirArgSk
dirArgTok:                      // A = first char of token
    cmp #'/'
    bne dirArgPat
    inx                         // char after '/'
    lda cmdArgsBuf,x
    cmp #'w'
    beq dirArgOptW
    cmp #'p'
    beq dirArgOptP
    dex                         // unknown option -- treat as a pattern token
    jmp dirArgPat
dirArgOptW:
    ldy #DIR_MODE
    lda #1
    sta (dataPtr),y
    jmp dirArgSkipTok
dirArgOptP:
    ldy #DIR_MODE
    lda #2
    sta (dataPtr),y
dirArgSkipTok:                  // consume the rest of the token
    inx
    lda cmdArgsBuf,x
    beq dirInit
    cmp #$20
    bne dirArgSkipTok
    jmp dirArgSk
dirArgPat:                      // non-option token -- pattern (first one wins)
    ldy #DIR_PATTERN
    lda (dataPtr),y
    bne dirArgSkipTok           // already have a pattern -- skip
dirArgCopy:
    // copy the pattern (<=11 chars) from cmdArgsBuf+X to DIR_PATTERN
    lda dataPtr; clc; adc #DIR_PATTERN; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    ldy #0
dirArgCpLp:
    lda cmdArgsBuf,x
    beq dirArgCpDone
    cmp #$20
    beq dirArgCpDone
    cpy #11
    bcs dirArgCpDone
    sta (kPtr),y
    inx
    iny
    bne dirArgCpLp
dirArgCpDone:
    lda #0
    sta (kPtr),y
    jmp dirArgSk               // scan for more tokens (options)
dirInit:
    ldy #DIR_IDX
    lda #1
    sta (dataPtr),y             // start at 1 (skip slot 0)
    ldy #DIR_IDXHI
    lda #0
    sta (dataPtr),y
    lda #0
    pha                         // file count on stack
    jmp dirLoop
dirLoop:
    ldy #DIR_MODE
    lda (dataPtr),y
    cmp #1
    bne dirSingle
    jmp dirWide                 // wide listing
dirSingle:
    jsr dirFindNext
    bcc dirShow
    jmp dirDone
dirShow:
    jsr dirPrintEntry
    jsr kNewline
    // /p: pause after every 24 printed lines
    ldy #DIR_MODE
    lda (dataPtr),y
    cmp #2
    bne dirSingle
    ldy #DIR_LINES
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    cmp #24
    bne dirSingle
    lda #0
    sta (dataPtr),y             // reset the page counter
    lda #<dirPressKey
    sta kPtr
    lda #>dirPressKey
    sta kPtr+1
    jsr kPrintString
    jsr dirWaitKey              // wait for (and consume) a keypress
    jsr kNewline
    jmp dirSingle

dirDone:
    // Footer: N file(s) $xxxx free
    jsr kNewline                // blank line before the count
    lda dataPtr; clc; adc #DIR_DECBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    pla                         // pull file count from stack
    jsr kByteToDec
    lda #0; sta (kPtr),y
    lda dataPtr; clc; adc #DIR_DECBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    jsr kPrintString
    lda #<dirFileS
    sta kPtr
    lda #>dirFileS
    sta kPtr+1
    jsr kPrintString            // " file(s) $"
    // free space from slot 0
    lda dataPtr; clc; adc #DIR_LINEBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    lda #0
    ldx #0
    jsr kReuDirEntry
    jsr dirPrint24
dirFoot:
    lda #<dirFree
    sta kPtr
    lda #>dirFree
    sta kPtr+1
    jsr kPrintString            // " free"
    jsr kNewline
    jsr kNewline
dirUnlock:
    jsr kUnlockScreen
    lda #SYS_EXIT
    jsr syscall                 // exit -- never returns

// dirPrint24 -- print free space as 6 hex digits (MSB first).
//  Input: kArg+3/kArg+4/kArg+5 = nextFreeOffset (set by kReuDirEntry slot 0)
//  Computes free = reuFsCapacity(3) - nextFreeOffset(3).
dirPrint24:
    sec
    lda reuFsCapacityLo; sbc kArg+3; sta kArg+3   // byte 0
    lda reuFsCapacityHi; sbc kArg+4; sta kArg+4   // byte 1
    lda reuFsCapacity2;  sbc kArg+5; sta kArg+5   // byte 2
    // Build the 6-char hex string first (MSB first) -- kPrintString
    // clobbers kArg, so every kByteToHex must happen before any print.
    lda dataPtr; clc; adc #DIR_DECBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    lda kArg+5; jsr kByteToHex           // +0,+1
    lda dataPtr; clc; adc #DIR_DECBUF+2; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    lda kArg+4; jsr kByteToHex           // +2,+3
    lda dataPtr; clc; adc #DIR_DECBUF+4; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    lda kArg+3; jsr kByteToHex           // +4,+5
    lda dataPtr; clc; adc #DIR_DECBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    lda #0; ldy #6; sta (kPtr),y         // null at +6 (DIR_PAD, unused now)
    jsr kPrintString                     // print all 6 chars at once
    rts

// --- dirWide (/w): two 17-char columns, two spaces between ---
dirWide:
    jsr dirFindNext
    bcc dirWideCol1
    jmp dirDone
dirWideCol1:
    jsr dirPrintEntry
    jsr dirFindNext
    bcs dirWideEol              // odd count -- no second column
    lda #$20; jsr kPrintChar
    lda #$20; jsr kPrintChar
    jsr dirPrintEntry
dirWideEol:
    jsr kNewline
    jmp dirWide

// --- dirFindNext -- next non-empty entry matching the pattern ---
//  C=0: name at DIR_LINEBUF, size at DIR_SZLO/HI, idx advanced past it.
//  C=1: exhausted (index reached 512).
//  Clobbers: A, X, Y, kPtr, kArg, srcPtr
dirFindNext:
    ldy #DIR_IDXHI
    lda (dataPtr),y
    cmp #2
    bne dirFnGo                 // hi != 2 -> continue
    jmp dirFnExh                // hi == 2 -> exhausted (idx == 512)
dirFnGo:
    lda dataPtr; clc; adc #DIR_LINEBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    ldy #DIR_IDXHI
    lda (dataPtr),y
    tax                         // X = idx hi
    ldy #DIR_IDX
    lda (dataPtr),y             // A = idx lo
    jsr kReuDirEntry
    bcc dirFnCheck
dirFnSkip:                      // empty slot -- advance and retry
    ldy #DIR_IDX
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    ldy #DIR_IDXHI
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y
    jmp dirFindNext
dirFnExh:
    sec
    rts
dirFnCheck:
    // stash the entry size (kReuDirEntry left it in kArg/kArg+1) --
    // kStrcmp (wildcard mode) clobbers kArg.
    ldy #DIR_SZLO
    lda kArg
    sta (dataPtr),y
    ldy #DIR_SZHI
    lda kArg+1
    sta (dataPtr),y
    // wildcard filter -- skip entries that don't match the pattern.
    // Delegates to string.lib's kStrcmp (wildcard mode, Z=1 match).
    ldy #DIR_PATTERN
    lda (dataPtr),y
    beq dirFnFound              // no pattern -- accept
    lda dataPtr; clc; adc #DIR_LINEBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    lda dataPtr; clc; adc #DIR_PATTERN; sta srcPtr
    lda dataPtr+1; adc #0; sta srcPtr+1
    lda #1
    sta kArg                    // wildcard mode
    jsr kStrcmp
    beq dirFnFound              // Z=1 match
    jmp dirFnSkip               // no match -- skip it
dirFnFound:
    ldy #DIR_IDX
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    ldy #DIR_IDXHI
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y
    clc
    rts

// --- dirPrintEntry -- print one entry (name padded to 11 + " $xxxx") ---
//  No newline. Name at DIR_LINEBUF, size at DIR_SZLO/HI. count++.
//  Clobbers: A, Y, kPtr, kArg
dirPrintEntry:
    // count++ -- the count lives on the stack UNDER this routine's own
    // return address (pushed by the caller's jsr), so address it via SP.
    tsx
    lda $0103,x
    clc
    adc #1
    sta $0103,x                 // file count++
    lda dataPtr; clc; adc #DIR_LINEBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    ldy #0
dirPeLen:
    lda (kPtr),y
    beq dirPeGot
    iny
    cpy #11
    bcc dirPeLen
dirPeGot:
    tya                         // A = name len
    ldy #DIR_PAD
    sta (dataPtr),y
    lda #11
    sec
    sbc (dataPtr),y             // A = 11 - len
    sta (dataPtr),y
    jsr kPrintString            // print name (clobbers kArg; kPtr = lineBuf)
dirPePad:
    ldy #DIR_PAD
    lda (dataPtr),y
    beq dirPePadDone
    sec
    sbc #1
    sta (dataPtr),y
    lda #$20
    jsr kPrintChar
    jmp dirPePad
dirPePadDone:
    lda #$20; jsr kPrintChar    // single space before size
    lda #'$'; jsr kPrintChar
    lda dataPtr; clc; adc #DIR_DECBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    ldy #DIR_SZHI
    lda (dataPtr),y
    jsr kByteToHex              // size hi
    lda dataPtr; clc; adc #DIR_DECBUF+2; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    ldy #DIR_SZLO
    lda (dataPtr),y
    jsr kByteToHex              // size lo
    lda #0; ldy #2; sta (kPtr),y
    lda dataPtr; clc; adc #DIR_DECBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    jsr kPrintString
    rts

// --- dirWaitKey -- wait for any keypress, consuming it ---
//  A command task never owns kbFocus, so the kernel's kWaitKey would
//  spin forever. Pops the IRQ keyboard ring buffer directly under SEI
//  (same semantics as kScanKeyboard + kWaitKey without the focus
//  check); release events ($ff) are discarded.
dirWaitKey:
    sei
    lda keybCount
    beq dirWkNone
    dec keybCount
    ldy keybTail
    lda keybBuffer,y
    iny
    cpy #16
    bcc dirWkTail
    ldy #0
dirWkTail:
    sty keybTail
    cli
    cmp #$ff
    beq dirWaitKey              // release event -- keep waiting
    rts
dirWkNone:
    cli
    lda #SYS_YIELD
    jsr syscall                 // yield -- never tight-loop
    jmp dirWaitKey

.encoding "screencode_mixed"
dirNoFs:
    .text "no fs"
    .byte 0
dirPressKey:
    .text "press any key to continue..."
    .byte 0
dirFileS:
    .text " file(s) $"
    .byte 0
dirFree:
    .text " free"
    .byte 0
