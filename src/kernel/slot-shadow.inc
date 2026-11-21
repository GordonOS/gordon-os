// ==========================================
//  slot-shadow.inc -- zero the ZP shadow a NEW task would otherwise inherit
// ==========================================
//  The per-slot REU ZP shadow (REU_ZP_BASE + slot x MAX_ZP_LEN, addressed by
//  taskZpReuLo/Hi) is per SLOT, but its CONTENT is written only by switchTask's
//  save DMA -- so it belongs to whoever last ran in that slot and it OUTLIVES
//  that task. The restore gate in switchTask is unconditional, so without this
//  routine a task spawned into a used slot would have THAT OCCUPANT'S bytes
//  DMA'd into its own declared ZP window on its first switch-in. A player task
//  that lives in ZP ($02-$0A holds a tune's live state) leaves exactly that
//  behind for whatever lands in its slot next, and a task whose ZP use is not a
//  full re-initialisation then inherits another task's pointers.
//
//  Zeros are the only correct starting state: they are the one value that is not
//  some other task's data, and no task may rely on undocumented stale ZP.
//
//  The stack image does NOT have this problem, and that is the contrast worth
//  keeping in mind: the spawn path DMAs the new task's initial frame over the
//  slot's stack image (initTCB_ptr), so the stack is the new task's from its
//  first tick. The ZP shadow had no such initialisation -- this is it.
//
//  WARNING: A task with no declared window (taskZpLen = 0) does NO DMA at all -- the
//  zero-overhead rule the per-task ZP feature was written with.
//
//  The kArg shadow (REU_KARG_BASE + slot x 8) has the same per-slot-stale shape
//  and is cleared too -- by clearKArgShadow, in this file's second fragment,
//  because this pocket has no room for a second fill. `kArg` is a
//  calling-convention scratch that every kernel call documents as clobbered, so
//  no task may rely on its CONTENTS; the clear is about the STARTING state being
//  defined rather than whatever the last occupant of the slot was passing.
//
//  Called from scSetAlive (syscall.inc) with X = the new task's slot, AFTER
//  taskZpBase/taskZpLen are stored, so the declared window is known. Runs SEI'd
//  (the whole spawn path is), so no IRQ can start a competing REU DMA. X is
//  preserved by kReuFillReu -- the caller's slot is still live.
//
//  Clobbers: A, kArg..kArg+5 (kReuFillReu stages its fill byte at kArg+5 --
//  dead here: the spawn path takes its inputs in kArg+6/+7 and writes only
//  kArg+2 afterwards).

clearSlotShadows:
    lda taskZpReuLo,x           // this slot's shadow address
    sta kArg
    lda taskZpReuHi,x
    sta kArg+1
    lda taskZpLen,x             // the declared window (<= 240 -> hi byte is 0)
    beq cssRts                  // none declared -> no DMA (see header)
    sta kArg+3                  // length lo
    lda #0
    sta kArg+2                  // bank 0 -- the ZP shadow lives in bank 0
    sta kArg+4                  // length hi
    jsr kReuFillReu             // A = 0 -> fill the window with zeros
cssRts:
    rts
.label slotShadowEnd = *

// --- the scheduler's tick counter, published to tasks ---
//  One byte AFTER the SID player's published block, incremented once per CIA Timer A
//  interrupt (src/kernel/irq.inc). It is the clock `kSidPlay` sleeps against -- a
//  tick per jiffy -- which a display frame count cannot express (50.1 Hz against
//  59.2). Tasks name it through src/tasks/kernel-data.inc; 8-bit and wrapping, so
//  subtract two readings for the elapsed ticks.
.pc = $54AA
    .byte 0

// --- the tune's flag byte, published to tasks ---
//  The operand of the last FLG command the running tune executed. The SID player
//  (src/libs/sidplay.inc) is its only writer and `kSidPlay` zeroes it when it claims
//  the SID; nothing in the kernel reads it. It is published here, beside the tick
//  counter, because both are bytes the kernel owns and tasks read at a fixed address
//  -- a reader is not the task that plays the tune, which is why the byte cannot live
//  in the player's own work memory (one shared copy per library).
//  It takes the last byte of the free run below the sprite state, so the rest of
//  that run ($CF96-$CFA2) stays one piece.
.pc = $CFA3
sidFlagByte:     .byte 0

//  NOTE: a new task's PER-TASK SPRITE state needs the same treatment, and it
//  lives in `clearSlotSprite` -- in src/kernel/sprite.inc, beside the sprite
//  code that reads it, because this band is exactly full (see main.asm's
//  `slot-shadow helpers fit below scrollScreenSlot` assert).

// ==========================================
//  clearKArgShadow -- zero the kArg shadow a recycled slot would hand down
// ==========================================
//  REU_KARG_BASE + slot x 8 is addressed by slot and only ever WRITTEN by
//  switchTask's save DMA, while its restore gate is unconditional -- so a task
//  spawned into a used slot has the previous occupant's kArg/kResult block DMA'd
//  in on its first switch-in. Zeros are the defined start: no kernel call may
//  read a field it did not set, and a zeroed block makes a caller that leaves
//  one unset fail visibly instead of inheriting another task's argument.
//
//  It sits here, not beside clearSlotShadows, because that pocket has no room for
//  a second fill: this is the TOP of the core chain's growth run, so whatever is
//  left of the run below stays one piece.
//
//  Called from scSetAlive (syscall.inc) with X = the new task's slot, after the
//  ZP shadow's clear -- which leaves X live (kReuFillReu preserves it). Runs
//  SEI'd, as the whole spawn path does, so no IRQ can start a competing DMA.
//
//  Clobbers: A, kArg..kArg+5 (kReuFillReu stages its fill byte at kArg+5)
.pc = $0F00 - 29                // 29B, parked at the TOP so the run below stays whole
clearKArgShadow:
    txa
    asl
    asl
    asl                         // slot x 8: the base is page-aligned, so this IS the
    sta kArg                    //   low byte (MAX_TASKS x 8 <= 256 is asserted)
    lda #>REU_KARG_BASE
    sta kArg+1
    lda #8
    sta kArg+3                  // length lo
    lda #0
    sta kArg+2                  // bank 0 -- the kArg shadow lives in bank 0
    sta kArg+4                  // length hi
    jsr kReuFillReu             // A = 0: the fill byte, so eight zeros
    rts
.label clearKArgShadowEnd = *
.assert "clearKArgShadow fits the top of the core growth run", (clearKArgShadowEnd <= $0F00) ? 1 : 0, 1
