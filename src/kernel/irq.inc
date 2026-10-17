// ==========================================
//  GordonOS -- IRQ handler
// ==========================================
//
//  Entry point from hardware vector ($FFFE).
//  CIA #1 Timer A triggers this ~60 times/sec.
//
//  On entry the 6502 has already pushed
//  PCH, PCL, SR (hardware). We save the
//  remaining registers, ack the CIA, and
//  call the scheduler. The scheduler never
//  returns to this invocation -- it RTS's
//  into the NEXT task's irqRet below.

irqHandler:
    cld                         // ensure BCD off for kernel math
    pha                         // save A
    txa
    pha                         // save X
    tya
    pha                         // save Y

    // --- Dispatch. Two interrupt sources now: VIC raster + CIA Timer A. ---
    //  $D019 is cleared by WRITING BACK what was read; $DC0D by READING it.
    //  Each ack happens exactly once, and neither may be skipped -- a missing
    //  VIC ack means the interrupt re-fires forever and the machine locks up.
    lda $d019
    beq irqNotVic
    sta $d019                   // ack raster (also clears any collision flag)
    jsr rasterCommit            // sprite state -> $D000-$D02E in vblank.
                                // Runs with I=1: no CLI, no kernel calls, no
                                // kernel ZP.

irqNotVic:
    lda $dc0d                   // ack CIA #1 Timer A
    and #$01                    // ONLY Timer A may reach the scheduler --
    beq irqResume               // a raster-only interrupt must not switch

    jsr kbPoll                  // scan keyboard -> ring buffer

    // --- process sleep ticks: decrement SLEEPING tasks ---
    ldx #0
irqSleepLp:
    cpx totalTasks
    bcs irqSleepDone
    lda taskState,x
    cmp #TA_SLEEPING
    bne irqSleepNext
    dec taskSleepTicks,x
    bne irqSleepNext            // still sleeping
    lda #TA_ALIVE
    sta taskState,x             // wake up
irqSleepNext:
    inx
    bne irqSleepLp              // always branches (totalTasks <= MAX_TASKS=24)

irqSleepDone:
    jsr switchTask              // context switch (does NOT return)

// ==========================================
//  irqResume -- a raster-only interrupt resumes
//  The interrupted code continues; no context switch. Same epilogue the
//  scheduler's RTS falls into (section 4.3) -- there is nothing new to write: entry
//  pushed PCH/PCL/SR (hardware) then A/X/Y (software), so irqRet unwinds it.
// ==========================================
irqResume:
    jmp irqRet

// ==========================================
//  irqRet -- all tasks resume here
//  The scheduler's RTS lands here after
//  restoring a task's stack. We pop the
//  saved registers and RTI into the task.
// ==========================================
irqRet:
    pla                         // -> Y
    tay
    pla                         // -> X
    tax
    pla                         // -> A
    rti                         // pop SR, PCL, PCH -> task
