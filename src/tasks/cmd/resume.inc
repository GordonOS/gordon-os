// ==========================================
//  resume.inc -- task-resume body
// ==========================================
//  "resume <id>" resumes a paused task, reading the arg from
//  cmdArgsBuf. taskParent/cmdArgsBuf/totalTasks/taskState come from
//  src/tasks/kernel-data.inc (imported globally by assemble-tasks.asm).

.const RESUME_SLOT   = 0         // parsed task ID
.const RESUME_DECBUF = 1         // 5 bytes: decimal conversion buffer
.const RESUME_NAMLO  = 6         // 1 byte:  saved name pointer lo
.const RESUME_NAMHI  = 7         // 1 byte:  saved name pointer hi

resume:
    lda #SYS_GETTID
    jsr syscall                 // gettid -> A
    tax
    lda taskParent,x            // shell slot
    tax
    jsr kShareScreen            // X = shell (preserved); C=0 ok
    bcc resumeScr
    lda #SYS_EXIT
    jsr syscall                 // no screen -- exit
resumeScr:
    jsr kLockScreen
    ldy #0
    jsr resumeParseDec
    bcc resumeChk
    jmp resumeBad
resumeChk:
    ldy #RESUME_SLOT
    lda (dataPtr),y
    tax                         // X = slot
    cpx totalTasks
    bcc resumeChk2
    jmp resumeBad
resumeChk2:
    lda taskState,x
    cmp #2                      // TA_PAUSED -- only resume paused tasks
    beq resumeGo
    jmp resumeBad
resumeGo:
    jsr kGetTaskName            // kPtr = name ptr (X preserved)
    lda kPtr
    ldy #RESUME_NAMLO
    sta (dataPtr),y
    lda kPtr+1
    ldy #RESUME_NAMHI
    sta (dataPtr),y
    lda #SYS_RESUME
    jsr syscall                 // sysResume (X = target slot)
    // "resumed"
    lda #<resumeOk
    sta kPtr
    lda #>resumeOk
    sta kPtr+1
    jsr kPrintString
    lda #$20
    jsr kPrintChar
    lda dataPtr; clc; adc #RESUME_DECBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    ldy #RESUME_SLOT
    lda (dataPtr),y
    jsr kByteToDec
    lda #0
    sta (kPtr),y                // null-terminate digits
    lda dataPtr; clc; adc #RESUME_DECBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    jsr kPrintString
    // " (name)"
    ldy #RESUME_NAMLO
    lda (dataPtr),y
    sta kPtr
    ldy #RESUME_NAMHI
    lda (dataPtr),y
    sta kPtr+1
    lda kPtr
    ora kPtr+1
    beq resumeNoName            // null name -- skip
    lda #$20; jsr kPrintChar
    lda #'('; jsr kPrintChar
    // kPrintChar clobbers kPtr -- re-point before kPrintString
    ldy #RESUME_NAMLO
    lda (dataPtr),y
    sta kPtr
    ldy #RESUME_NAMHI
    lda (dataPtr),y
    sta kPtr+1
    jsr kPrintString
    lda #')'; jsr kPrintChar
resumeNoName:
    jsr kNewline
    jsr kNewline
    jmp resumeUnlock
resumeBad:
    lda #<resumeBadStr
    sta kPtr
    lda #>resumeBadStr
    sta kPtr+1
    jsr kPrintString
    jsr kNewline
    jsr kNewline
resumeUnlock:
    jsr kUnlockScreen
    lda #SYS_EXIT
    jsr syscall                 // exit -- never returns

// resumeParseDec -- parse decimal task ID from cmdArgsBuf[Y].
//  Input:  Y = cmdArgsBuf offset (0)
//  Output: C=0 + RESUME_SLOT = parsed value (0-255)
//          C=1 on error.
resumeParseDec:
rsSkip:
    lda cmdArgsBuf,y
    beq rsBad
    cmp #$20
    bne rsFirst
    iny
    bne rsSkip
rsFirst:
    sec
    sbc #'0'
    bcc rsBad
    cmp #10
    bcs rsBad
    sty kArg+2
    lda #0
    ldy #RESUME_SLOT
    sta (dataPtr),y
    ldy kArg+2
rsLoop:
    lda cmdArgsBuf,y
    beq rsDone
    cmp #$20
    beq rsDone
    sec
    sbc #'0'
    bcc rsBad
    cmp #10
    bcs rsBad
    sta kArg
    sty kArg+2
    ldy #RESUME_SLOT
    lda (dataPtr),y
    asl; asl; clc; adc (dataPtr),y  // x5
    asl; clc; adc kArg              // x10 + digit
    sta (dataPtr),y
    ldy kArg+2
    iny
    bne rsLoop
rsDone:
    clc
    rts
rsBad:
    sec
    rts

.encoding "screencode_mixed"
resumeOk:
    .text "resumed"
    .byte 0
resumeBadStr:
    .text "? cannot resume"
    .byte 0
