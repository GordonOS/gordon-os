// ==========================================
//  ps.inc -- task table listing body
// ==========================================
//  Prints a header, then id/pri/addr/name per ALIVE task, then exits.
//  taskParent/taskState/totalTasks/taskPriority/taskLo/taskHi come from
//  src/tasks/kernel-data.inc (imported globally by tools/assemble-tasks.asm).

.const PS_IDX    = 0
.const PS_SLOT   = 1
.const PS_DECBUF = 2

ps:
    // owner = taskParent[own slot] (the shell)
    lda #SYS_GETTID
    jsr syscall                 // gettid -> A
    tax
    lda taskParent,x            // shell slot
    tax
    jsr kShareScreen            // X = shell (preserved); C=0 ok
    bcc psScr
    lda #SYS_EXIT
    jsr syscall                 // no screen -- exit
psScr:
    jsr kLockScreen
    // header
    lda #<psHdr; sta kPtr
    lda #>psHdr; sta kPtr+1
    jsr kPrintString
    jsr kNewline
    lda #<psSep; sta kPtr
    lda #>psSep; sta kPtr+1
    jsr kPrintString
    jsr kNewline
    // idx = 0
    ldy #PS_IDX
    lda #0
    sta (dataPtr),y
psLoop:
    ldy #PS_IDX
    lda (dataPtr),y
    tax
    lda taskState,x
    bne psShow
    jmp psNext                  // DEAD -- skip
psShow:
    ldy #PS_SLOT
    txa
    sta (dataPtr),y
    cmp #10
    bcs psIdGo
    lda #'0'
    jsr kPrintChar              // zero-pad 1-digit id
psIdGo:
    // id (decimal)
    lda dataPtr; clc; adc #PS_DECBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    ldy #PS_SLOT
    lda (dataPtr),y
    jsr kByteToDec              // digits at decBuf, Y=len
    lda #0
    sta (kPtr),y                // null-terminate
    lda dataPtr; clc; adc #PS_DECBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    jsr kPrintString
    lda #' '; jsr kPrintChar
    // priority (3-digit zero-padded)
    ldy #PS_SLOT
    lda (dataPtr),y
    tax
    lda taskPriority,x
    pha
    lda dataPtr; clc; adc #PS_DECBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    pla
    jsr kByteToDec
    tya; pha                    // save length
    cpy #3; beq psPri3
    cpy #2; beq psPri2
    lda #'0'; jsr kPrintChar
psPri2:
    lda #'0'; jsr kPrintChar
psPri3:
    lda dataPtr; clc; adc #PS_DECBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    pla; tay
    lda #0; sta (kPtr),y
    jsr kPrintString
    lda #' '; jsr kPrintChar
    // $ + 4-hex address
    lda #'$'; jsr kPrintChar
    ldy #PS_SLOT
    lda (dataPtr),y
    tax
    lda dataPtr; clc; adc #PS_DECBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    lda taskHi,x
    jsr kByteToHex
    ldy #PS_SLOT
    lda (dataPtr),y
    tax
    lda dataPtr; clc; adc #PS_DECBUF+2; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    lda taskLo,x
    jsr kByteToHex
    lda #0
    ldy #PS_DECBUF+4
    sta (dataPtr),y
    lda dataPtr; clc; adc #PS_DECBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    jsr kPrintString
    lda #' '; jsr kPrintChar
    // name
    ldy #PS_SLOT
    lda (dataPtr),y
    tax
    jsr kGetTaskName            // kPtr = name ptr (X preserved)
    lda kPtr
    ora kPtr+1
    bne psNameOk
    jmp psNext                  // null name -- skip
psNameOk:
    jsr kPrintString
    // " (shr)" for reentrant resident instances
    ldy #PS_SLOT
    lda (dataPtr),y
    sta kArg+6                  // task id for dyn scan
    ldy #0
psDynScan:
    cpy #8                      // MAX_DYN_TASKS
    bcs psStateChk
    lda dynTaskId,y
    cmp kArg+6
    beq psDynFnd
    iny
    jmp psDynScan
psDynFnd:
    lda dynTaskResidency,y
    cmp #$ff
    beq psStateChk              // single-instance -> no marker
    //  WARNING: ATTACHED is not the same as SHARED. A LONE instance can
    //  still be attached to a record: if the record's origin is killed while a
    //  co-instance lives, the survivor (and anything that attaches to that record
    //  afterwards) owns no pages of its own -- the record does, and `pool` counts
    //  them there -- so a single attached instance would be flagged `(shr)` while
    //  being the only one running. The marker's meaning is "this code is shared
    //  with another instance", so require the record to have more than one.
    tay
    lda resRefCount,y
    cmp #2
    bcc psStateChk              // 0 or 1 instance -> not shared
    lda #<psShr
    sta kPtr
    lda #>psShr
    sta kPtr+1
    jsr kPrintString            // " (shr)"
psStateChk:
    ldy #PS_SLOT
    lda (dataPtr),y
    tax
    lda taskState,x
    cmp #2                      // TA_PAUSED
    bne psChkSleep
    lda #<psPau
    sta kPtr
    lda #>psPau
    sta kPtr+1
    jsr kPrintString
    jmp psNl
psChkSleep:
    cmp #3                      // TA_SLEEPING
    bne psChkSwap
    lda #<psSlp
    sta kPtr
    lda #>psSlp
    sta kPtr+1
    jsr kPrintString
    jmp psNl
psChkSwap:
    cmp #4                      // TA_SWAPPED
    bne psNl
    lda #<psSwp
    sta kPtr
    lda #>psSwp
    sta kPtr+1
    jsr kPrintString
psNl:
    jsr kNewline
psNext:
    ldy #PS_IDX
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    ldy #PS_IDX
    lda (dataPtr),y
    cmp totalTasks
    bcs psDone
    jmp psLoop
psDone:
    jsr kNewline
    jsr kUnlockScreen
    lda #SYS_EXIT
    jsr syscall                 // exit -- never returns

.encoding "screencode_mixed"
psHdr:
    .text "id pri addr name"
    .byte 0
psSep:
    .text "------------------"
    .byte 0
psPau:
    .text " (pau)"
    .byte 0
psSlp:
    .text " (slp)"
    .byte 0
psShr:
    .text " (shr)"
    .byte 0
psSwp:
    .text " (swp)"
    .byte 0
