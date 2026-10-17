// ==========================================
//  ipctest.inc -- ipc.lib four-channel self-test body
// ==========================================
//  The ipc.lib runtime self-test. Kept in the tree like `fstest` (filesys.lib):
//  after ANY change to src/libs/ipc.asm, type `ipctest` and expect `ipc PASS`.
//
//  All the scratch here is absolute labels in THIS task's code image, never
//  (dataPtr): the two producers are thread children, and a thread has no data
//  buffer, so dataPtr is 0 in them ($0000/$0001 are the 6510 port registers).
//  Thread children share this task's code page, so a child and the parent can
//  hand results over through these labels; every label is written by ONE role
//  only.
//
//  Checks, in order -- each prints its own verdict, so a failure names itself
//  and no debugger is needed:
//
//    1  guard    kIpcPost with X = 4 (one past the last channel) is refused
//                with C=1 and stores NOTHING (channel 0 must still be empty)
//    2  empty    all four channels are empty at startup
//    3  pair A   a thread producer posts 0,1,2,... onto channel 1 while the
//                parent consumes and compares every byte in order
//    4  pair B   a second thread posts 255,254,... (its counter EOR $FF) onto
//                channel 3, consumed by the same parent INTERLEAVED with A
//
//  The interleave is the point of the test. Both producers are faster than the
//  one consumer, so both rings fill and both producers block inside kIpcPost's
//  backpressure wait -- the path where SYS_SLEEP returns with X = curTask, i.e.
//  where the channel has to be recovered from the producer's own stack. A
//  channel mix-up cannot pass this test: it either mismatches (the two
//  patterns are different) or stalls until the timeout. Running the pairs on
//  channel 1 and channel 3 also covers the case where a task's slot number
//  differs from its channel number, which is what made the stale-X bug
//  invisible in a single-pair, single-channel test.
//
//  The wait is bounded (itcPolls) so a broken library reports FAIL instead of
//  hanging the machine, and the parent outlives both children on every path:
//  a thread reaches ipc.lib only through this task's libMask, and SYS_EXIT
//  does not reap thread children (by design -- only a forcible kill does), so
//  itcReap kills them explicitly before the parent exits.
//
//  WARNING: Keep this in step with src/libs/ipc.asm: if IPC_CHANNELS stops being 4,
//  ITC_CH_END and the two channel numbers move with it, and pair B's pattern
//  (its counter EOR $FF) must stay different from pair A's.

.const ITC_CH_A   = 1           // pair A's channel
.const ITC_CH_B   = 3           // pair B's channel
.const ITC_CH_END = 4           // one past the last channel (IPC_CHANNELS in ipc.asm)
.const ITC_TIME_HI = $10        // give up after 4096 empty passes (~=68 s at a tick each)

// --- inline string print: kPtr = string, then kPrintString ---
.macro itcSay(str) {
    lda #<str
    sta kPtr
    lda #>str
    sta kPtr+1
    jsr kPrintString
}

ipctest:
    // --- attach to the shell's screen (ps/fstest protocol) ---
    lda #SYS_GETTID
    jsr syscall
    tax
    lda taskParent,x            // the shell
    tax
    jsr kShareScreen            // C=0 ok
    bcc itcScr
    lda #SYS_EXIT
    jsr syscall
itcScr:
    jsr kLockScreen
    itcSay(itcSHead)
    jsr kNewline

    // ================= 1: the range guard =================
    itcSay(itcSGuard)
    ldx #ITC_CH_END             // 4 -- out of range
    lda #$5a
    jsr kIpcPost                // must be refused: C=1, and the byte thrown away
    bcc itcGuardBad
    ldx #0                      // ...and nothing may have landed on channel 0
    jsr kIpcPick
    bcs itcGuardOk
itcGuardBad:
    lda #1
    sta itcBad
    itcSay(itcSBad)
    jmp itcEmptyGo
itcGuardOk:
    itcSay(itcSOk)

    // ================= 2: every channel starts empty =================
itcEmptyGo:
    itcSay(itcSEmpty)
    lda #0
    sta itcCh
itcEmptyLp:
    ldx itcCh
    jsr kIpcPick
    bcc itcEmptyBad             // C=0 -> this channel had a byte in it already
    inc itcCh
    lda itcCh
    cmp #ITC_CH_END
    bcc itcEmptyLp
    itcSay(itcSOk)
    jmp itcSpawnGo
itcEmptyBad:
    lda #1
    sta itcBad
    itcSay(itcSBad)

    // ================= 3/4: the two thread producers =================
itcSpawnGo:
    jsr kNewline
    //  WARNING: kArg+6/kArg+7 are the CALLER's to set (the kernel does not clear them,
    //  so a thread can declare a ZP window). Neither
    //  child wants one, but they must be zeroed explicitly or a child
    //  inherits whatever those scratch bytes held last.
    lda #<itcProdA
    sta spawnPtr
    lda #>itcProdA
    sta spawnPtr+1
    lda #0
    sta kArg+6                  // zpBase = 0
    sta kArg+7                  // zpLen  = 0 -> no ZP save for the child
    lda #SYS_THREADSPAWN
    jsr syscall                 // C=0 spawned
    bcs itcSpawnBad
    lda #<itcProdB
    sta spawnPtr
    lda #>itcProdB
    sta spawnPtr+1
    lda #0
    sta kArg+6
    sta kArg+7
    lda #SYS_THREADSPAWN
    jsr syscall
    bcc itcConsume
itcSpawnBad:
    lda #1
    sta itcBad
    itcSay(itcSSpawn)
    jmp itcVerdict

    // ================= consume both channels, interleaved =================
itcConsume:
    lda #0
    sta itcN1                   // expected value on channel A (0,1,2,...)
    sta itcN3                   // pair B's counter (its pattern is this EOR $FF)
    sta itcGot1
    sta itcGot3
    sta itcPolls
    sta itcPolls+1
itcPoll:
    ldx #ITC_CH_A
    jsr kIpcPick
    bcs itcPollB                // C=1: A is empty this pass
    cmp itcN1                   // ---- pair A: the byte must be the counter
    bne itcBadA
    inc itcN1
    lda #1
    sta itcGot1
itcPollB:
    ldx #ITC_CH_B
    jsr kIpcPick
    bcs itcPollDone
    sta itcTmp                  // ---- pair B: the byte must be ~counter
    lda itcN3
    eor #$FF
    cmp itcTmp
    bne itcBadB
    inc itcN3
    lda #1
    sta itcGot3
itcPollDone:
    // A channel is complete when its counter has wrapped back to 0 AND at
    // least one byte arrived (both counters start at 0, so the flag is what
    // orders the two facts).
    lda itcN1
    bne itcSleep                // A has not wrapped yet
    lda itcGot1
    beq itcSleep                // ...and nothing has arrived yet
    lda itcN3
    bne itcSleep                // B has not wrapped yet
    lda itcGot3
    beq itcSleep
    itcSay(itcSCh1)             // both complete: 256 bytes each, in order
    itcSay(itcSOk)
    itcSay(itcSCh3)
    itcSay(itcSOk)
    jmp itcVerdict

itcBadA:
    lda #1
    sta itcBad
    itcSay(itcSCh1)
    itcSay(itcSBad)
    jmp itcVerdict
itcBadB:
    lda #1
    sta itcBad
    itcSay(itcSCh3)
    itcSay(itcSBad)
    jmp itcVerdict

    // Empty pass on both channels. Count it and give up eventually: a broken
    // library must fail this test, not hang the machine.
itcSleep:
    inc itcPolls
    bne itcSleepGo
    inc itcPolls+1
    lda itcPolls+1
    cmp #ITC_TIME_HI
    bcs itcTimeout
itcSleepGo:
    lda #1                      // one tick: a sleep still switches tasks
    sta kArg+1
    lda #SYS_SLEEP
    jsr syscall
    jmp itcPoll
itcTimeout:
    lda #1
    sta itcBad
    itcSay(itcSTime)
    jmp itcVerdict

    // ================= verdict, reap, exit =================
itcVerdict:
    jsr kNewline
    lda itcBad
    bne itcFail
    itcSay(itcSPass)
    jmp itcDone
itcFail:
    itcSay(itcSFail)
itcDone:
    jsr kNewline
    jsr itcReap
    lda #SYS_EXIT
    jsr syscall

// ==========================================
//  itcReap -- kill this task's thread children
// ==========================================
//  SYS_EXIT does not cascade (only a forcible kill reaps a thread subtree), and
//  a child blocked in kIpcPost's backpressure wait would sit in its task slot
//  for good -- the ring it is waiting on is never drained once this task stops
//  consuming. So walk the task table and kill every ALIVE-or-not slot whose
//  taskParent is us: the parent link is the only handle we have (SYS_THREADSPAWN
//  returns no id), and killing an already-DEAD slot is explicitly allowed.
//  Never kills self. Clobbers what syscall clobbers.
itcReap:
    lda #SYS_GETTID
    jsr syscall
    sta itcSelf
    lda #1                      // slot 0 is the boot task -- never a child
    sta itcSlot
itcReapLp:
    ldx itcSlot
    cpx totalTasks
    bcs itcReapDone
    cpx itcSelf
    beq itcReapNext             // never kill self
    lda taskParent,x
    cmp itcSelf
    bne itcReapNext             // not one of ours (the shell's parent is boot)
    lda #SYS_KILL
    jsr syscall                 // X = the child's slot
itcReapNext:
    inc itcSlot
    bne itcReapLp
itcReapDone:
    rts

// ==========================================
//  Thread children -- one producer per channel
// ==========================================
//  A thread starts at the address in spawnPtr, so the entry label IS the role.
//  Both share this image; each owns its own counter byte below.
//  kIpcPost returns only once the byte is buffered, so a child parks itself in
//  the backpressure wait whenever its ring is full -- the behaviour under test.

// --- pair A: posts 0..255 onto channel 1 ---
itcProdA:
    lda #0
    sta itcPA
itcProdALp:
    lda #ITC_CH_A
    tax
    lda itcPA
    jsr kIpcPost
    inc itcPA
    bne itcProdALp              // wraps to 0 after the 256th byte
    lda #SYS_EXIT
    jsr syscall

// --- pair B: posts 255..0 (counter EOR $FF) onto channel 3 ---
itcProdB:
    lda #0
    sta itcPB
itcProdBLp:
    lda #ITC_CH_B
    tax
    lda itcPB
    eor #$FF                    // pair B's pattern, distinguishable from A's
    jsr kIpcPost
    inc itcPB
    bne itcProdBLp
    lda #SYS_EXIT
    jsr syscall

// ==========================================
//  Test scratch + strings
// ==========================================
itcBad:      .byte 0            // parent: nonzero = at least one check failed
itcCh:       .byte 0            // parent: channel index for the empty scan
itcN1:       .byte 0            // parent: expected value on channel A
itcN3:       .byte 0            // parent: pair B's counter (pattern = EOR $FF)
itcGot1:     .byte 0            // parent: a byte arrived on channel A
itcGot3:     .byte 0            // parent: a byte arrived on channel B
itcTmp:      .byte 0            // parent: picked byte, for the pair B compare
itcPolls:    .byte 0, 0         // parent: empty passes -- the give-up counter
itcSelf:     .byte 0            // parent's own slot (itcReap)
itcSlot:     .byte 0            // itcReap's scan cursor
itcPA:       .byte 0            // child A's counter (child-owned)
itcPB:       .byte 0            // child B's counter (child-owned)

itcSHead:   .text "ipc test";    .byte 0
itcSGuard:  .text " guard ";     .byte 0
itcSEmpty:  .text " 4 empty ";   .byte 0
itcSCh1:    .text " ch1 ";       .byte 0
itcSCh3:    .text " ch3 ";       .byte 0
itcSOk:     .text "ok";          .byte 0
itcSBad:    .text "FAIL";        .byte 0
itcSTime:   .text " TIMEOUT";    .byte 0
itcSSpawn:  .text " spawn FAIL"; .byte 0
itcSPass:   .text "ipc PASS";    .byte 0
itcSFail:   .text "ipc FAIL";    .byte 0
