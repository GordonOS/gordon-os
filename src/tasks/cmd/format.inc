// ==========================================
//  format.inc -- interactive REU-FS format body
// ==========================================
//  "format" prompts "format? y/n", reads one key, and formats the FS on
//  'y'. Grabs kbFocus with kSwitchView so kReadString works -- its
//  surface is the shared shell surface, so the display doesn't change.
//  taskParent/cmdArgsBuf come from src/tasks/kernel-data.inc (imported
//  globally by assemble-tasks.asm).

.const FORMAT_SLOT = 0          // own slot (for kSwitchView)
.const FORMAT_BUF   = 1         // 2 bytes: kReadString buffer

format:
    lda #SYS_GETTID
    jsr syscall                 // gettid -> A = own slot
    ldy #FORMAT_SLOT
    sta (dataPtr),y             // save own slot
    tax
    lda taskParent,x            // shell slot
    tax
    jsr kShareScreen            // X = shell (preserved); C=0 ok
    bcc formatScr
    lda #SYS_EXIT
    jsr syscall                 // no screen -- exit
formatScr:
    // grab kbFocus -- kReadString returns empty when curTask != kbFocus
    ldy #FORMAT_SLOT
    lda (dataPtr),y
    tax
    jsr kSwitchView             // kbFocus = self (display unchanged: shared)
    jsr kLockScreen
    // "format? y/n "
    lda #<fmtPrompt
    sta kPtr
    lda #>fmtPrompt
    sta kPtr+1
    jsr kPrintString
    lda #$20; jsr kPrintChar
    // read 1 key into FORMAT_BUF
    lda dataPtr; clc; adc #FORMAT_BUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    ldx #1
    jsr kReadString             // Y = length (0 = empty)
    tya
    pha                         // save length (kNewline clobbers Y)
    jsr kNewline
    pla
    tay
    cpy #0
    beq fmtCancel               // empty -> cancel
    ldy #FORMAT_BUF
    lda (dataPtr),y
    cmp #'y'
    beq fmtDoFormat
fmtCancel:
    lda #<fmtCancelStr
    sta kPtr
    lda #>fmtCancelStr
    sta kPtr+1
    jsr kPrintString
    jsr kNewline
    jsr kNewline
    jmp formatUnlock
fmtDoFormat:
    jsr kReuFormat
    lda #<fmtDoneStr
    sta kPtr
    lda #>fmtDoneStr
    sta kPtr+1
    jsr kPrintString
    jsr kNewline
    jsr kNewline
formatUnlock:
    jsr kUnlockScreen
    lda #SYS_EXIT
    jsr syscall                 // exit -- never returns

.encoding "screencode_mixed"
fmtPrompt:
    .text "format? y/n"
    .byte 0
fmtDoneStr:
    .text "formatted"
    .byte 0
fmtCancelStr:
    .text "cancelled"
    .byte 0
