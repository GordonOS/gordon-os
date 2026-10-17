// ==========================================
//  time.inc -- clock show/set body
// ==========================================
//  "time" (no args) prints the current time; "time hhmmss [am|pm]" sets
//  it. Reads its args from cmdArgsBuf.
//  taskParent/cmdArgsBuf come from src/tasks/kernel-data.inc (imported
//  globally by tools/assemble-tasks.asm). No dataPtr scratch needed.

time:
    // owner = taskParent[own slot] (the shell)
    lda #SYS_GETTID
    jsr syscall                 // gettid -> A
    tax
    lda taskParent,x            // shell slot
    tax
    jsr kShareScreen            // X = shell (preserved); C=0 ok
    bcc timeScr
    lda #SYS_EXIT
    jsr syscall                 // no screen -- exit
timeScr:
    jsr kLockScreen
    lda cmdArgsBuf              // any args?
    beq timeShow
    jmp timeSet
timeShow:
    jsr kGetCursor              // X=col, Y=row (shell's cursor)
    jsr kPrintTime              // blit time at (X,Y) -- kBlitChar erases the blink block itself
    jsr kNewline
    jsr kNewline
    jmp timeUnlock

timeSet:
    // parse "hhmmss" (6 digits) from cmdArgsBuf into kArg[0..5]
    ldy #0
    ldx #0
timeDig:
    cpx #6                      // already have 6 digits?
    beq timeDone
    lda cmdArgsBuf,y
    beq timeDone
    sec
    sbc #'0'
    bcs tmDig1
    jmp timeBad                 // < '0'
tmDig1:
    cmp #10
    bcc tmDig2
    jmp timeBad                 // > '9'
tmDig2:
    sta kArg,x
    inx
    iny
    bne timeDig
timeDone:
    cpx #6
    beq tmChkAmPm
    jmp timeBad                 // need exactly 6 digits

// --- optional AM/PM suffix ---
tmChkAmPm:
    lda #0
    sta kArg+6                  // AM/PM flag: $00=AM, $80=PM (CIA TOD bit 7)
    lda cmdArgsBuf,y            // char after 6th digit
    cmp #'a'; beq timeAmPm
    cmp #'p'; beq timeAmPm
    cmp #'A'; beq timeAmPm
    cmp #'P'; beq timeAmPm
    jmp timeConv                // no AM/PM suffix
timeAmPm:
    cmp #'p'; beq timeSetPm
    cmp #'P'; beq timeSetPm
    jmp timeChkM                // 'a' or 'A' -> AM (flag stays 0)
timeSetPm:
    lda #$80
    sta kArg+6                  // PM: bit 7 set
timeChkM:
    iny
    lda cmdArgsBuf,y
    cmp #'m'; beq timeChkEnd
    cmp #'M'; beq timeChkEnd
    jmp timeBad                 // not 'm' or 'M'
timeChkEnd:
    iny
    lda cmdArgsBuf,y
    beq timeConv                // end of line -> convert
    jmp timeBad                 // extra chars after am/pm

timeConv:
    // convert pairs to BCD: hh, mm, ss
    lda kArg
    asl; asl; asl; asl
    ora kArg+1
    sta kArg                    // hours BCD
    lda kArg+2
    asl; asl; asl; asl
    ora kArg+3
    sta kArg+2                  // minutes BCD
    lda kArg+4
    asl; asl; asl; asl
    ora kArg+5
    sta kArg+1                  // seconds BCD
    lda kArg                    // hours BCD
    ora kArg+6                  // OR in PM flag ($80 if PM)
    sta kArg+3                  // move to kArg+3 for kSetTime
    lda #0
    sta kArg                    // 1/10 sec = 0
    jsr kSetTime
    lda #<timeOk
    sta kPtr
    lda #>timeOk
    sta kPtr+1
    jsr kPrintString
    jsr kNewline
    jsr kNewline
    jmp timeUnlock

timeBad:
    lda #<timeBadStr
    sta kPtr
    lda #>timeBadStr
    sta kPtr+1
    jsr kPrintString
    jsr kNewline
    jsr kNewline
timeUnlock:
    jsr kUnlockScreen
    lda #SYS_EXIT
    jsr syscall                 // exit -- never returns

.encoding "screencode_mixed"
timeOk:
    .text "time set"
    .byte 0
timeBadStr:
    .text "? time: use hhmmss [am/pm]"
    .byte 0
