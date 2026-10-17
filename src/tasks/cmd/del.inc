// ==========================================
//  del.inc -- REU-FS delete body (wildcard support)
// ==========================================
//  "del <pattern>" deletes every FS entry whose name matches the
//  wildcard pattern ('*' any run incl. empty, '?' any single char),
//  then exits. Iterates the directory (kReuDirEntry), matches each
//  entry against the pattern (string.lib kStrcmp, wildcard mode), and
//  kReuDelete's each match. Prints "deleted" (>=1 match) or
//  "? not found" (none). A pattern with no wildcards deletes exactly
//  that file (kStrcmp wildcard does exact matching when '*'/'?' are
//  absent).
//  taskParent/reuFsEnabled/cmdArgsBuf come from src/tasks/kernel-data.inc
//  (imported globally by tools/assemble-tasks.asm).

.const DEL_NAM   = 0    // 12 bytes: entry name buffer
.const DEL_IDX   = 12   // 1 byte:  directory scan index lo
.const DEL_IDXHI = 13   // 1 byte:  directory scan index hi
.const DEL_CNT   = 14   // 1 byte:  deleted count
.const DEL_PAT   = 15   // 12 bytes: wildcard pattern + null

del:
    // owner = taskParent[own slot] (the shell)
    lda #SYS_GETTID
    jsr syscall                 // gettid -> A
    tax
    lda taskParent,x            // shell slot
    tax
    jsr kShareScreen            // X = shell (preserved); C=0 ok
    bcc delScr
    lda #SYS_EXIT
    jsr syscall                 // no screen -- exit
delScr:
    jsr kLockScreen
    lda reuFsEnabled
    cmp #$A5
    beq delFs
    lda #<delNoFs
    sta kPtr
    lda #>delNoFs
    sta kPtr+1
    jsr kPrintString
    jmp delNewlines
delFs:
    // --- parse pattern (skip spaces, copy <=11 chars, reject longer) ---
    ldx #0
delSk:
    lda cmdArgsBuf,x
    bne delSkChk
    jmp delBad                   // no pattern
delSkChk:
    cmp #$20
    bne delCopy
    inx
    bne delSk
delCopy:
    lda dataPtr; clc; adc #DEL_PAT; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    ldy #0
delCopyLp:
    lda cmdArgsBuf,x
    beq delCopyDone
    cmp #$20
    beq delCopyDone
    cpy #11
    bcc delCopyStore             // Y < 11 -> copy it
    jmp delBad                   // 12th char -> pattern > 11 chars
delCopyStore:
    sta (kPtr),y
    inx
    iny
    bne delCopyLp
delCopyDone:
    lda #0
    sta (kPtr),y                 // null-terminate pattern
    // --- init scan (entry 1..511; slot 0 is the allocator) ---
    ldy #DEL_IDX
    lda #1
    sta (dataPtr),y              // idx lo = 1
    ldy #DEL_IDXHI
    lda #0
    sta (dataPtr),y              // idx hi = 0
    ldy #DEL_CNT
    lda #0
    sta (dataPtr),y              // count = 0
delLoop:
    ldy #DEL_IDXHI
    lda (dataPtr),y
    cmp #2
    bne delEntry
    jmp delDone                  // idx == 512 -> exhausted
delEntry:
    lda dataPtr; clc; adc #DEL_NAM; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    ldy #DEL_IDXHI
    lda (dataPtr),y
    tax                          // X = idx hi
    ldy #DEL_IDX
    lda (dataPtr),y              // A = idx lo
    jsr kReuDirEntry
    bcc delMatch
    jmp delNext                  // empty/deleted -> skip
delMatch:
    // wildcard match: kPtr = name, srcPtr = pattern
    lda dataPtr; clc; adc #DEL_NAM; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    lda dataPtr; clc; adc #DEL_PAT; sta srcPtr
    lda dataPtr+1; adc #0; sta srcPtr+1
    lda #1
    sta kArg                     // wildcard mode
    jsr kStrcmp
    bne delNext                  // Z=0 no match
    // delete the matching entry
    lda dataPtr; clc; adc #DEL_NAM; sta kArg
    lda dataPtr+1; adc #0; sta kArg+1
    jsr kReuDelete
    ldy #DEL_CNT
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y              // count++
delNext:
    ldy #DEL_IDX
    lda (dataPtr),y
    clc
    adc #1
    sta (dataPtr),y
    ldy #DEL_IDXHI
    lda (dataPtr),y
    adc #0
    sta (dataPtr),y
    jmp delLoop
delDone:
    ldy #DEL_CNT
    lda (dataPtr),y
    beq delNotFound
    lda #<delOk
    sta kPtr
    lda #>delOk
    sta kPtr+1
    jmp delExit
delNotFound:
    lda #<delNf
    sta kPtr
    lda #>delNf
    sta kPtr+1
    jmp delExit
delBad:
    lda #<delBadStr
    sta kPtr
    lda #>delBadStr
    sta kPtr+1
    jmp delExit
delExit:
    jsr kPrintString
delNewlines:
    jsr kNewline
    jsr kNewline
delUnlock:
    jsr kUnlockScreen
    lda #SYS_EXIT
    jsr syscall                 // exit -- never returns

.encoding "screencode_mixed"
delOk:     .text "deleted";     .byte 0
delNf:     .text "? not found"; .byte 0
delBadStr: .text "? bad name";  .byte 0
delNoFs:   .text "no fs";       .byte 0
