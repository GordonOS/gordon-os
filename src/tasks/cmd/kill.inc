// ==========================================
//  kill.inc -- task-kill body
// ==========================================
//  "kill <id>" kills task <id>, reading the arg from cmdArgsBuf.
//  taskParent/cmdArgsBuf/totalTasks/taskState come from
//  src/tasks/kernel-data.inc (imported globally by assemble-tasks.asm).

.const KILL_SLOT   = 0          // parsed task ID
.const KILL_DECBUF = 1          // 5 bytes: decimal conversion buffer
.const KILL_NAMLO  = 6          // 1 byte:  saved name pointer lo
.const KILL_NAMHI  = 7          // 1 byte:  saved name pointer hi
.const KILL_SIDUSE = 8          // 1 byte:  1 = the victim (or its spawner) uses sid.lib

//  sid.lib's bit in a task's 16-bit library mask (lib id 5 -- see
//  docs/dynamic-libraries.md). Bits 0-7 live in `dynTaskLibMaskLo` and the
//  highest lib id in the tree is 6, so the LOW byte is the one to test.
.const LIB_MASK_SID = 1 << LIB_SID   // sid.lib's bit in a task's 16-bit library mask

kill:
    // owner = taskParent[own slot] (the shell)
    lda #SYS_GETTID
    jsr syscall                 // gettid -> A
    tax
    lda taskParent,x            // shell slot
    tax
    jsr kShareScreen            // X = shell (preserved); C=0 ok
    bcc killScr
    lda #SYS_EXIT
    jsr syscall                 // no screen -- exit
killScr:
    jsr kLockScreen
    ldy #0
    jsr killParseDec
    bcc killChk
    jmp killBad
killChk:
    ldy #KILL_SLOT
    lda (dataPtr),y
    tax                         // X = slot
    cpx totalTasks
    bcc killChk2
    jmp killBad
killChk2:
    lda taskState,x
    bne killGo
    jmp killBad
killGo:
    jsr kGetTaskName            // kPtr = name ptr (X preserved)
    lda kPtr
    ldy #KILL_NAMLO
    sta (dataPtr),y
    lda kPtr+1
    ldy #KILL_NAMHI
    sta (dataPtr),y
    //  Does the victim -- or the task that spawned it -- use sid.lib? Ask NOW,
    //  before sysKill frees the victim's dyn entry, because that is where a
    //  task's library mask lives (killSidUser explains). The answer is stashed
    //  in our own scratch and the chip is only poked AFTER the kill: silencing
    //  it while the player still runs would be undone by its next tick.
    ldy #KILL_SLOT
    lda (dataPtr),y
    jsr killSidUser
    ldy #KILL_SIDUSE
    lda #0
    bcc killSidNo
    lda #1
killSidNo:
    sta (dataPtr),y
    ldy #KILL_SLOT
    lda (dataPtr),y
    tax                         // X = target (killSidUser clobbered X)
    //  WARNING: dataPtr MUST be saved across the kill. sysKill -> dynFree ends with
    //  `jsr dlRestore`, which re-loads a swapped-out task and points dataPtr at
    //  the loader's own scratch (dynload.inc's dlRestore lists dataPtr as
    //  clobbered; dlrNameSetup stores dynTaskNameLo/Hi into it). Everything
    //  below -- the name, the decimal id, the sid flag -- reads (dataPtr), so
    //  without this the command prints and writes the wrong memory whenever a
    //  restore happens to fit. The task's own stack survives the syscall (it is
    //  per-task, and only the VICTIM is being torn down).
    lda dataPtr
    pha
    lda dataPtr+1
    pha
    lda #SYS_KILL
    jsr syscall                 // sysKill (X = target slot)
    pla; sta dataPtr+1
    pla; sta dataPtr
    //  A killed music task never runs its own hush, so the SID is left holding
    //  the last note its player wrote -- the drone `sidrst` exists to clear.
    //  Clear it here, on the chip's 25 writeable registers, exactly as
    //  sidrst.com does. No library call: kill declares none, and cold-loading
    //  sid.lib (13 pool pages) to zero 25 hardware bytes would be absurd.
    ldy #KILL_SIDUSE
    lda (dataPtr),y
    beq killNoHush
    jsr killSidSilence
killNoHush:
    // "killed"
    lda #<killOk
    sta kPtr
    lda #>killOk
    sta kPtr+1
    jsr kPrintString
    lda #$20
    jsr kPrintChar
    lda dataPtr; clc; adc #KILL_DECBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    ldy #KILL_SLOT
    lda (dataPtr),y
    jsr kByteToDec
    lda #0
    sta (kPtr),y                // null-terminate digits
    lda dataPtr; clc; adc #KILL_DECBUF; sta kPtr
    lda dataPtr+1; adc #0; sta kPtr+1
    jsr kPrintString
    // " (name)"
    ldy #KILL_NAMLO
    lda (dataPtr),y
    sta kPtr
    ldy #KILL_NAMHI
    lda (dataPtr),y
    sta kPtr+1
    lda kPtr
    ora kPtr+1
    beq killNoName              // null name -- skip
    lda #$20; jsr kPrintChar
    lda #'('; jsr kPrintChar
    // kPrintChar clobbers kPtr -- re-point before kPrintString
    ldy #KILL_NAMLO
    lda (dataPtr),y
    sta kPtr
    ldy #KILL_NAMHI
    lda (dataPtr),y
    sta kPtr+1
    jsr kPrintString
    lda #')'; jsr kPrintChar
killNoName:
    jsr kNewline
    jsr kNewline
    jmp killUnlock
killBad:
    lda #<killBadStr
    sta kPtr
    lda #>killBadStr
    sta kPtr+1
    jsr kPrintString
    jsr kNewline
    jsr kNewline
killUnlock:
    jsr kUnlockScreen
    lda #SYS_EXIT
    jsr syscall                 // exit -- never returns

// killParseDec -- parse decimal task ID from cmdArgsBuf[Y].
//  Input:  Y = cmdArgsBuf offset (0)
//  Output: C=0 + KILL_SLOT = parsed value (0-255), Y past last digit
//          C=1 on error (no/leading non-digit).
//  Clobbers: A, Y, kArg, kArg+2 (kArg safe -- no print/syscall in loop)
killParseDec:
kdSkip:
    lda cmdArgsBuf,y
    beq kdBad
    cmp #$20
    bne kdFirst
    iny
    bne kdSkip
kdFirst:
    sec
    sbc #'0'
    bcc kdBad
    cmp #10
    bcs kdBad
    sty kArg+2                  // save cmdArgsBuf offset
    lda #0
    ldy #KILL_SLOT
    sta (dataPtr),y             // result = 0
    ldy kArg+2                  // restore offset
kdLoop:
    lda cmdArgsBuf,y
    beq kdDone
    cmp #$20
    beq kdDone
    sec
    sbc #'0'
    bcc kdBad
    cmp #10
    bcs kdBad
    sta kArg                    // digit
    sty kArg+2                  // save offset
    ldy #KILL_SLOT
    lda (dataPtr),y             // result
    asl; asl; clc; adc (dataPtr),y  // x5
    asl; clc; adc kArg              // x10 + digit
    sta (dataPtr),y
    ldy kArg+2                  // restore offset
    iny
    bne kdLoop
kdDone:
    clc
    rts
kdBad:
    sec
    rts

// killSidUser -- does this task use sid.lib (or does the task that spawned it)?
//  Input:  A = task slot
//  Output: C=1 = yes -- the SID should be silenced after this kill
//  Clobbers: A, X, Y, kArg+6, kArg+7
//
//  The question cannot be put to the SID library: its owner byte lives in the
//  library's own data, relocated into pool pages, and the only task-visible
//  trace of a player is the DECLARATION in the dyn table -- `dynTaskLibMaskLo`
//  (bit 5 = sid.lib) with `dynTaskId` holding the task slot, which is exactly
//  what the declaration survives: `kLibHold` rewrites the mask to the libraries
//  it kept, so a music task's mask ends up as the libraries it still holds
//  ({sid}, plus the sprite library if it drives sprites). That is also the right
//  question for a
//  task whose music never started (refused the chip): it declared the player
//  and killed it without a hush, so the chip must be cleared either way.
//
//  The PARENT is asked too, because a music demo may play through a thread
//  child: the child is the SID owner but has no dyn entry
//  at all -- the declaration is on the parent -- so killing either end is "a
//  music task died". A `run`-loaded task's parent is the shell, which declares
//  filesys+string only, so that lookup is a harmless no-op (and it is what
//  makes `kill <demo.0>` work as well as `kill <demo>`).
killSidUser:
    sta kArg+6                  // the slot we are asking about
    jsr killSidDecl
    bcs ksuYes
    ldx kArg+6
    lda taskParent,x
    cmp #$ff
    beq ksuNo                   // no spawner (a root task): nothing to ask
    jsr killSidDecl
    bcs ksuYes
ksuNo:
    clc
    rts
ksuYes:
    sec
    rts

// killSidDecl -- A = task slot -> C=1 if that task's dyn entry declares sid.lib.
//  C=0 for a task with no dyn entry (a thread child, the boot task).
killSidDecl:
    sta kArg+7
    ldx #0
ksdLoop:
    lda dynTaskId,x
    cmp kArg+7
    beq ksdFound
    inx
    cpx #8                      // MAX_DYN_TASKS
    bcc ksdLoop
    clc
    rts                         // no dyn entry -- not a declarer
ksdFound:
    lda dynTaskLibMaskLo,x
    and #LIB_MASK_SID
    beq ksdNo
    sec
    rts
ksdNo:
    clc
    rts

// killSidSilence -- zero all 25 writeable SID registers.
//  The same write set as the hush inside `kSidHush`/`kSidPlay`:
//  $D400-$D418 (voice freq/pulse/control, ADSR, filter, volume). $D419-$D41C
//  are read-only (pots/OSC3/ENV3) and are never written. This pokes the chip
//  itself rather than the library, so it works with the player's state gone
//  and sid.lib possibly unloaded (the kill released the victim's refcount).
killSidSilence:
    ldx #0
    lda #0
kssLoop:
    sta $d400,x                 // absolute,X -- fixed hardware address
    inx
    cpx #25                     // $D400..$D418 = 25 writeable registers
    bcc kssLoop
    rts

.encoding "screencode_mixed"
killOk:
    .text "killed"
    .byte 0
killBadStr:
    .text "? invalid task"
    .byte 0
