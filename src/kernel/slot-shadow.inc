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
//  WARNING: The kArg shadow (REU_KARG_BASE + slot x 8) has the same per-slot-stale
//  shape and is deliberately NOT cleared here -- BENIGN, not an oversight: kArg
//  is a calling-convention scratch that every kernel call documents as
//  clobbered, so a task must set the fields a call reads before making it, and
//  nothing may depend on the initial contents. The save/restore gates keep the
//  block private per task, and a spawn runs SEI'd, so no switch can land between
//  it and the loader's next kArg read. Clearing it waits on the byte budget: the
//  fill is ~28B and this pocket has 9B.
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

//  NOTE: a new task's PER-TASK SPRITE state needs the same treatment, and it
//  lives in `clearSlotSprite` -- in src/kernel/sprite.inc, beside the sprite
//  code that reads it, because this band is exactly full (see main.asm's
//  `slot-shadow helpers fit below scrollScreenSlot` assert).
