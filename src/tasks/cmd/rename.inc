// ==========================================
//  rename.inc -- REU-FS rename body
// ==========================================
//  "rename <old> <new>" renames an FS entry on the shell's shared
//  screen, then exits. Parses two names from cmdArgsBuf (shell's
//  pcFgPs copies the args), calls kReuRename ($E0BA), maps its
//  kArg+6 error codes (0=old not found, 1=new exists, 2=bad name)
//  to messages.
//  taskParent/reuFsEnabled/cmdArgsBuf come from src/tasks/kernel-data.inc
//  (imported globally by tools/assemble-tasks.asm).

rename:
    // owner = taskParent[own slot] (the shell)
    lda #SYS_GETTID
    jsr syscall                 // gettid -> A
    tax
    lda taskParent,x            // shell slot
    tax
    jsr kShareScreen            // X = shell (preserved); C=0 ok
    bcc renScr
    lda #SYS_EXIT
    jsr syscall                 // no screen -- exit
renScr:
    jsr kLockScreen
    lda reuFsEnabled
    cmp #$A5
    beq renFs
    lda #<renNoFsStr; sta kPtr
    lda #>renNoFsStr; sta kPtr+1
    jsr kPrintString
    jmp renUnlock
renFs:
    // --- parse old name from cmdArgsBuf ---
    ldy #0
renSk1:
    lda cmdArgsBuf,y
    bne renSk1Chk
    jmp renBadName              // no args
renSk1Chk:
    cmp #$20
    bne renN1
    iny
    bne renSk1
renN1:
    tya
    clc
    adc #<cmdArgsBuf
    sta kArg                    // kArg/kArg+1 = old name ptr
    lda #>cmdArgsBuf
    adc #0
    sta kArg+1
renN1Scan:
    lda cmdArgsBuf,y
    bne renN1Chk
    jmp renBadName              // line ended -- no new name
renN1Chk:
    cmp #$20
    beq renN1Got
    iny
    cpy #39
    bcc renN1Scan
renN1Got:
    lda #0
    sta cmdArgsBuf,y            // null-terminate old name
    iny
    // --- parse new name ---
renSk2:
    cpy #40
    bcc renSk2Chk
    jmp renBadName              // buffer exhausted -- no room for a name
renSk2Chk:
    lda cmdArgsBuf,y
    bne renSk2Sp
    jmp renBadName              // missing new name
renSk2Sp:
    cmp #$20
    bne renN2
    iny
    bne renSk2
renN2:
    tya
    clc
    adc #<cmdArgsBuf
    sta kArg+2                  // kArg+2/kArg+3 = new name ptr
    lda #>cmdArgsBuf
    adc #0
    sta kArg+3
renN2Scan:
    lda cmdArgsBuf,y
    beq renN2Got
    cmp #$20
    beq renN2Got
    iny
    cpy #39
    bcc renN2Scan
renN2Got:
    lda #0
    sta cmdArgsBuf,y            // null-terminate new name
    jsr kReuRename
    bcc renOk
    lda kArg+6                  // error code survives in kArg+6
    beq renNotFound
    cmp #1
    beq renExists
    jmp renBadName
renOk:
    lda #<renOkStr; sta kPtr
    lda #>renOkStr; sta kPtr+1
    jsr kPrintString
    jmp renDone
renNotFound:
    lda #<renNfStr; sta kPtr
    lda #>renNfStr; sta kPtr+1
    jsr kPrintString
    jmp renDone
renExists:
    lda #<renExStr; sta kPtr
    lda #>renExStr; sta kPtr+1
    jsr kPrintString
    jmp renDone
renBadName:
    lda #<renBadStr; sta kPtr
    lda #>renBadStr; sta kPtr+1
    jsr kPrintString
renDone:
    jsr kNewline
    jsr kNewline
renUnlock:
    jsr kUnlockScreen
    lda #SYS_EXIT
    jsr syscall                 // exit -- never returns

.encoding "screencode_mixed"
renOkStr:
    .text "renamed"
    .byte 0
renNfStr:
    .text "? not found"
    .byte 0
renExStr:
    .text "? exists"
    .byte 0
renBadStr:
    .text "? bad name"
    .byte 0
renNoFsStr:
    .text "no fs"
    .byte 0
