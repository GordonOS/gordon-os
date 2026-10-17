// ==========================================
//  prio.inc -- task-priority body
// ==========================================
//  "prio <id> <n>" sets task <id>'s priority (0=pause, 1-5 valid, >5
//  clamped by the kernel), reading args from cmdArgsBuf.
//  taskParent/cmdArgsBuf/totalTasks/taskState come from
//  src/tasks/kernel-data.inc (imported globally by assemble-tasks.asm).

.const PRIO_SLOT   = 0          // parsed task ID
.const PRIO_PRIO   = 1          // parsed priority value
.const PRIO_PARENT = 2          // shell slot (refuse prio 0 on it)
.const PRIO_NAMLO  = 3          // saved name pointer lo
.const PRIO_NAMHI  = 4          // saved name pointer hi
.const PRIO_DECBUF = 5          // 5 bytes: decimal conversion buffer

prio:
    lda #SYS_GETTID
    jsr syscall                 // gettid -> A
    tax
    lda taskParent,x            // shell slot
    ldy #PRIO_PARENT
    sta (dataPtr),y             // save parent slot
    tax
    jsr kShareScreen            // X = shell (preserved); C=0 ok
    bcc prioScr
    lda #SYS_EXIT
    jsr syscall                 // no screen -- exit
prioScr:
    jsr kLockScreen
    jsr prioParseSlot           // parse ID -> PRIO_SLOT
    bcc prioPrioArg
    jmp prioBad
prioPrioArg:
    jsr prioParsePrio           // parse priority -> PRIO_PRIO
    bcc prioChk
    jmp prioBad
prioChk:
    ldy #PRIO_SLOT
    lda (dataPtr),y
    tax                         // X = slot
    cpx totalTasks
    bcc prioChk2
    jmp prioBad
prioChk2:
    lda taskState,x
    bne prioChkParent
    jmp prioBad
prioChkParent:
    // prio 0 pauses the target -- refuse to pause the shell
    ldy #PRIO_PRIO
    lda (dataPtr),y
    bne prioGo                  // non-zero -> safe
    stx kArg                    // target slot
    ldy #PRIO_PARENT
    lda (dataPtr),y             // shell slot
    cmp kArg
    bne prioGo
    jmp prioBad
prioGo:
    jsr kGetTaskName            // kPtr = name ptr (X preserved)
    lda kPtr
    ldy #PRIO_NAMLO
    sta (dataPtr),y
    lda kPtr+1
    ldy #PRIO_NAMHI
    sta (dataPtr),y
    ldy #PRIO_PRIO
    lda (dataPtr),y
    sta kArg+1                  // priority for sysSetPrio
    lda #SYS_SETPRIO
    jsr syscall                 // sysSetPrio (X = target slot)
    // "priority set"
    lda #<prioOk
    sta kPtr
    lda #>prioOk
    sta kPtr+1
    jsr kPrintString
    lda #$20
    jsr kPrintChar
    lda dataPtr; clc; adc #PRIO_DECBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    ldy #PRIO_SLOT
    lda (dataPtr),y
    jsr kByteToDec
    lda #0
    sta (kPtr),y                // null-terminate digits
    lda dataPtr; clc; adc #PRIO_DECBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    jsr kPrintString
    // " (name)"
    ldy #PRIO_NAMLO
    lda (dataPtr),y
    sta kPtr
    ldy #PRIO_NAMHI
    lda (dataPtr),y
    sta kPtr+1
    lda kPtr
    ora kPtr+1
    beq prioNoName              // null name -- skip
    lda #$20; jsr kPrintChar
    lda #'('; jsr kPrintChar
    // kPrintChar clobbers kPtr -- re-point before kPrintString
    ldy #PRIO_NAMLO
    lda (dataPtr),y
    sta kPtr
    ldy #PRIO_NAMHI
    lda (dataPtr),y
    sta kPtr+1
    jsr kPrintString
    lda #')'; jsr kPrintChar
prioNoName:
    jsr kNewline
    jsr kNewline
    jmp prioUnlock
prioBad:
    lda #<prioBadStr
    sta kPtr
    lda #>prioBadStr
    sta kPtr+1
    jsr kPrintString
    jsr kNewline
    jsr kNewline
prioUnlock:
    jsr kUnlockScreen
    lda #SYS_EXIT
    jsr syscall                 // exit -- never returns

// prioParseSlot -- parse decimal task ID from cmdArgsBuf.
//  Output: C=0 + PRIO_SLOT = value, Y past last digit; C=1 on error.
prioParseSlot:
    ldy #0
slSkip:
    lda cmdArgsBuf,y
    beq slBad
    cmp #$20
    bne slFirst
    iny
    bne slSkip
slFirst:
    sec
    sbc #'0'
    bcc slBad
    cmp #10
    bcs slBad
    sty kArg+2
    lda #0
    ldy #PRIO_SLOT
    sta (dataPtr),y
    ldy kArg+2
slLoop:
    lda cmdArgsBuf,y
    beq slDone
    cmp #$20
    beq slDone
    sec
    sbc #'0'
    bcc slBad
    cmp #10
    bcs slBad
    sta kArg
    sty kArg+2
    ldy #PRIO_SLOT
    lda (dataPtr),y
    asl; asl; clc; adc (dataPtr),y  // x5
    asl; clc; adc kArg              // x10 + digit
    sta (dataPtr),y
    ldy kArg+2
    iny
    bne slLoop
slDone:
    clc
    rts
slBad:
    sec
    rts

// prioParsePrio -- parse decimal priority from cmdArgsBuf[Y].
//  Skips leading spaces. Output: C=0 + PRIO_PRIO = value; C=1 on error.
prioParsePrio:
ppSkip:
    lda cmdArgsBuf,y
    beq ppBad
    cmp #$20
    bne ppFirst
    iny
    bne ppSkip
ppFirst:
    sec
    sbc #'0'
    bcc ppBad
    cmp #10
    bcs ppBad
    sty kArg+2
    lda #0
    ldy #PRIO_PRIO
    sta (dataPtr),y
    ldy kArg+2
ppLoop:
    lda cmdArgsBuf,y
    beq ppDone
    cmp #$20
    beq ppDone
    sec
    sbc #'0'
    bcc ppBad
    cmp #10
    bcs ppBad
    sta kArg
    sty kArg+2
    ldy #PRIO_PRIO
    lda (dataPtr),y
    asl; asl; clc; adc (dataPtr),y  // x5
    asl; clc; adc kArg              // x10 + digit
    sta (dataPtr),y
    ldy kArg+2
    iny
    bne ppLoop
ppDone:
    clc
    rts
ppBad:
    sec
    rts

.encoding "screencode_mixed"
prioOk:
    .text "priority set"
    .byte 0
prioBadStr:
    .text "? prio: id 0-5 (0=pause)"
    .byte 0
