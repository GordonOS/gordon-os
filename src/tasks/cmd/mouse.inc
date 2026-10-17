// ==========================================
//  mouse.inc -- "mouse": declare the pointer device
// ==========================================
//  "mouse 1351" / "mouse off" write the kernel's mouseKind byte (0 = off,
//  1 = 1351). With no argument the command prints the current setting; with a
//  word it does not recognise it prints the accepted ones and leaves the byte
//  alone.
//
//  mouseKind is a MACHINE-wide value, not task state: the writer is a command
//  that exits, and the reader is a task that starts later (grpaint's start-up
//  gate). It rides in keyboard.inc's address-stability sled, so it costs the
//  kernel no space and no task declares it -- src/tasks/kernel-data.inc aliases
//  it for tasks.
//
//  Its reset value is 0, and that is the deliberate default
//  A machine that cannot do 1351 must not
//  leave a paint program holding a frozen pointer, so a 1351 owner opts in by
//  adding `mouse 1351` to boot.bat -- exactly how a 200% machine adds
//  `speed 200`. Nothing persists the value beyond that line.
//
//  Silent on success, like speed/blink/keyrpt. This is policy and nothing else:
//  it declares no libraries and touches no hardware.

mouse:
    //  A .com prints on the SHELL's screen: the shell owns the display, so
    //  without the share this command's output lands on its own surface and is
    //  never seen. Same prologue as dir/time/speed -- the owner is
    //  taskParent[own].
    lda #SYS_GETTID
    jsr syscall                 // gettid -> A
    tax
    lda taskParent,x            // shell slot
    tax
    jsr kShareScreen            // X = shell (preserved); C=0 ok
    bcc mgScr
    lda #SYS_EXIT
    jsr syscall                 // no screen -- exit
mgScr:
    jsr kLockScreen             // serialise screen access against other tasks

    //  --- the first word of the arguments, past any leading spaces ---
    //  cmdArgsBuf is null-terminated by the shell, so the skip cannot run off,
    //  and a terminator FIRST is the "no argument" case -- it has to be tested
    //  before the space test, because `cmp #$20` leaves Z=0 for a zero byte.
    ldx #0
mgSkip:
    lda cmdArgsBuf,x
    beq mgShow                  // nothing given: report the setting
    cmp #$20
    bne mgFirst
    inx
    jmp mgSkip
mgFirst:
    lda #<mgStr1351
    sta kPtr
    lda #>mgStr1351
    sta kPtr+1
    jsr mgMatch
    bcc mgSetOn
    lda #<mgStrOff
    sta kPtr
    lda #>mgStrOff
    sta kPtr+1
    jsr mgMatch
    bcc mgSetOff
    //  --- unrecognised: usage, and the byte is NOT touched ---
    lda #<mgStrUse
    sta kPtr
    lda #>mgStrUse
    sta kPtr+1
    jsr mgPrint
    jmp mgExit
mgSetOn:
    lda #1
    bne mgStore                 // always taken: A is 1, never zero
mgSetOff:
    lda #0
mgStore:
    sta mouseKind
    jmp mgExit

    //  --- "mouse <state>": the label, then the state's own word ---
mgShow:
    lda #<mgStrName
    sta kPtr
    lda #>mgStrName
    sta kPtr+1
    jsr kPrintString
    lda mouseKind
    beq mgShowOff
    lda #<mgStr1351
    sta kPtr
    lda #>mgStr1351
    sta kPtr+1
    jmp mgPrint
mgShowOff:
    lda #<mgStrOff
    sta kPtr
    lda #>mgStrOff
    sta kPtr+1
mgPrint:
    //  The output line, then a blank line -- the same spacing a refused
    //  argument gets, and what `speed`/`dir` leave under their own output.
    jsr kPrintString
    jsr kNewline
    jsr kNewline

mgExit:
    jsr kUnlockScreen
    lda #SYS_EXIT
    jsr syscall                 // exit

// --- mgMatch -- does cmdArgsBuf[X..] start with the word at (kPtr)? ---
//  C=0 match, C=1 no match. WARNING: the word's END is checked too, so `13513`
//  and `offside` are refused rather than read as `1351` and `off` -- a bad
//  argument must not change the byte. X is left past the word, Y is clobbered.
mgMatch:
    ldy #0
mgMatchLp:
    lda (kPtr),y
    beq mgMatchEnd              // the word ended: everything matched
    cmp cmdArgsBuf,x
    bne mgMatchNo
    inx
    iny
    bne mgMatchLp
mgMatchEnd:
    lda cmdArgsBuf,x
    beq mgMatchYes              // ...and nothing follows it
    cmp #$20
    beq mgMatchYes              // ...or a further word does
mgMatchNo:
    sec
    rts
mgMatchYes:
    clc
    rts

mgStrName:  .text "mouse ";                                 .byte 0
mgStr1351:  .text "1351";                                   .byte 0
mgStrOff:   .text "off";                                    .byte 0
mgStrUse:   .text "? mouse 1351 off";                       .byte 0
