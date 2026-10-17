// ==========================================
//  pause.inc -- task-pause body
// ==========================================
//  "pause <id>" pauses task <id>, reading the arg from cmdArgsBuf.
//  taskParent/cmdArgsBuf/totalTasks/taskState come from
//  src/tasks/kernel-data.inc (imported globally by assemble-tasks.asm).

.const PAUSE_SLOT   = 0          // parsed task ID
.const PAUSE_DECBUF = 1          // 5 bytes: decimal conversion buffer
.const PAUSE_NAMLO  = 6          // 1 byte:  saved name pointer lo
.const PAUSE_NAMHI  = 7          // 1 byte:  saved name pointer hi
.const PAUSE_PARENT = 8          // 1 byte:  shell slot (refuse to pause it)

pause:
    lda #SYS_GETTID
    jsr syscall                 // gettid -> A
    tax
    lda taskParent,x            // shell slot
    ldy #PAUSE_PARENT
    sta (dataPtr),y             // save parent slot (refuse to pause it)
    tax
    jsr kShareScreen            // X = shell (preserved); C=0 ok
    bcc pauseScr
    lda #SYS_EXIT
    jsr syscall                 // no screen -- exit
pauseScr:
    jsr kLockScreen
    ldy #0
    jsr pauseParseDec
    bcc pauseChk
    jmp pauseBad
pauseChk:
    ldy #PAUSE_SLOT
    lda (dataPtr),y
    tax                         // X = slot
    cpx totalTasks
    bcc pauseChk2
    jmp pauseBad
pauseChk2:
    lda taskState,x
    cmp #1                      // TA_ALIVE -- only pause running tasks
    beq pauseChkParent
    jmp pauseBad
pauseChkParent:
    stx kArg                    // target slot
    ldy #PAUSE_PARENT
    lda (dataPtr),y             // A = shell slot
    cmp kArg                    // pausing the shell hangs it
    bne pauseGo
    jmp pauseBad
pauseGo:
    jsr kGetTaskName            // kPtr = name ptr (X preserved)
    lda kPtr
    ldy #PAUSE_NAMLO
    sta (dataPtr),y
    lda kPtr+1
    ldy #PAUSE_NAMHI
    sta (dataPtr),y
    lda #SYS_PAUSE
    jsr syscall                 // sysPause (X = target slot)
    // "paused"
    lda #<pauseOk
    sta kPtr
    lda #>pauseOk
    sta kPtr+1
    jsr kPrintString
    lda #$20
    jsr kPrintChar
    lda dataPtr; clc; adc #PAUSE_DECBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    ldy #PAUSE_SLOT
    lda (dataPtr),y
    jsr kByteToDec
    lda #0
    sta (kPtr),y                // null-terminate digits
    lda dataPtr; clc; adc #PAUSE_DECBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    jsr kPrintString
    // " (name)"
    ldy #PAUSE_NAMLO
    lda (dataPtr),y
    sta kPtr
    ldy #PAUSE_NAMHI
    lda (dataPtr),y
    sta kPtr+1
    lda kPtr
    ora kPtr+1
    beq pauseNoName             // null name -- skip
    lda #$20; jsr kPrintChar
    lda #'('; jsr kPrintChar
    // kPrintChar clobbers kPtr -- re-point before kPrintString
    ldy #PAUSE_NAMLO
    lda (dataPtr),y
    sta kPtr
    ldy #PAUSE_NAMHI
    lda (dataPtr),y
    sta kPtr+1
    jsr kPrintString
    lda #')'; jsr kPrintChar
pauseNoName:
    jsr kNewline
    jsr kNewline
    jmp pauseUnlock
pauseBad:
    lda #<pauseBadStr
    sta kPtr
    lda #>pauseBadStr
    sta kPtr+1
    jsr kPrintString
    jsr kNewline
    jsr kNewline
pauseUnlock:
    jsr kUnlockScreen
    lda #SYS_EXIT
    jsr syscall                 // exit -- never returns

// pauseParseDec -- parse decimal task ID from cmdArgsBuf[Y].
//  Input:  Y = cmdArgsBuf offset (0)
//  Output: C=0 + PAUSE_SLOT = parsed value (0-255)
//          C=1 on error.
pauseParseDec:
paSkip:
    lda cmdArgsBuf,y
    beq paBad
    cmp #$20
    bne paFirst
    iny
    bne paSkip
paFirst:
    sec
    sbc #'0'
    bcc paBad
    cmp #10
    bcs paBad
    sty kArg+2
    lda #0
    ldy #PAUSE_SLOT
    sta (dataPtr),y
    ldy kArg+2
paLoop:
    lda cmdArgsBuf,y
    beq paDone
    cmp #$20
    beq paDone
    sec
    sbc #'0'
    bcc paBad
    cmp #10
    bcs paBad
    sta kArg
    sty kArg+2
    ldy #PAUSE_SLOT
    lda (dataPtr),y
    asl; asl; clc; adc (dataPtr),y  // x5
    asl; clc; adc kArg              // x10 + digit
    sta (dataPtr),y
    ldy kArg+2
    iny
    bne paLoop
paDone:
    clc
    rts
paBad:
    sec
    rts

.encoding "screencode_mixed"
pauseOk:
    .text "paused"
    .byte 0
pauseBadStr:
    .text "? cannot pause"
    .byte 0
