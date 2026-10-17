// ==========================================
//  setfont.inc -- REU-FS font loader body
// ==========================================
//  "setfont <name>" loads <name>.fnt from the REU FS -- updates the
//  kernel's REU font record (the blitter DMA's glyphs on demand).
//  ($BC00), switching the font the whole system uses. The base name is
//  read from cmdArgsBuf (the shell's pcFgPs copies the args), extended
//  with ".fnt", and loaded via kReuLoad -- the same name-extension
//  pattern as the shell's runbatch (".bat"). No instance buffer: the
//  name is parsed + null-terminated in place in cmdArgsBuf.
//  FONT_REC_*/cmdArgsBuf/taskParent come from src/tasks/kernel-data.inc.

.const SF_MAX = 7              // max base-name length (".fnt" fills 11)

setfont:
    // owner = taskParent[own slot] (the shell) -- share its screen
    lda #SYS_GETTID
    jsr syscall                 // gettid -> A
    tax
    lda taskParent,x            // shell slot
    tax
    jsr kShareScreen            // X = shell (preserved); C=0 ok
    bcc sfScr
    lda #SYS_EXIT
    jsr syscall                 // no screen -- exit
sfScr:
    jsr kLockScreen
    lda reuFsEnabled
    cmp #$A5
    beq sfFs
    lda #<sfNoFsStr; sta kPtr
    lda #>sfNoFsStr; sta kPtr+1
    jsr kPrintString
    jsr kNewline
    jmp sfExit
sfFs:
    // srcPtr = filename start in cmdArgsBuf (skip leading spaces)
    ldy #0
sfSkSp:
    lda cmdArgsBuf,y
    bne sfChkSp
    jmp sfUse                   // empty -- usage error
sfChkSp:
    cmp #$20
    bne sfName
    iny
    bne sfSkSp
sfName:
    tya
    clc
    adc #<cmdArgsBuf
    sta srcPtr
    lda #>cmdArgsBuf
    adc #0
    sta srcPtr+1                // srcPtr = name start
    ldy #0
sfScan:
    lda (srcPtr),y
    beq sfAppend                // end of line
    cmp #'.'
    beq sfBadExt                // explicit extension -> refuse
    cmp #$20
    beq sfAppend                // trailing space
    iny
    cpy #SF_MAX                 // name <= 7 chars
    bcc sfScan
sfAppend:                       // append ".fnt\0"
    lda #'.'; sta (srcPtr),y
    iny
    lda #'f'; sta (srcPtr),y
    iny
    lda #'n'; sta (srcPtr),y
    iny
    lda #'t'; sta (srcPtr),y
    iny
    lda #0; sta (srcPtr),y
    // resolve <name>.fnt -> REU font record (REU-only font)
    lda srcPtr; sta kArg
    lda srcPtr+1; sta kArg+1
    jsr kReuFindFile
    bcs sfNotFound
    lda reuFsRemain+1
    cmp #8                      // >= 2048 bytes (2KB font)?
    bcc sfNotFound
    sei                         // atomic record update (blitter reads it)
    :FSCopy(reuFsPtr, FONT_REC_LO)   // 3 bytes -- never 2 of 3
    cli
    lda #<sfOkStr; sta kPtr
    lda #>sfOkStr; sta kPtr+1
    jsr kPrintString
    jsr kNewline
    jmp sfExit
sfNotFound:
    lda #<sfNoStr; sta kPtr
    lda #>sfNoStr; sta kPtr+1
    jsr kPrintString
    jsr kNewline
    jmp sfExit
sfBadExt:
    lda #<sfExtStr; sta kPtr
    lda #>sfExtStr; sta kPtr+1
    jsr kPrintString
    jsr kNewline
    jmp sfExit
sfUse:
    lda #<sfUseStr; sta kPtr
    lda #>sfUseStr; sta kPtr+1
    jsr kPrintString
    jsr kNewline
sfExit:
    jsr kUnlockScreen
    lda #SYS_EXIT
    jsr syscall                 // exit -- never returns

.encoding "screencode_mixed"
sfOkStr:   .text "font set"; .byte 0
sfNoStr:   .text "? not found"; .byte 0
sfNoFsStr: .text "no fs"; .byte 0
sfExtStr:  .text "? setfont: no extension"; .byte 0
sfUseStr:  .text "? setfont <filename>"; .byte 0
