// ==========================================
//  REMOVED kernel routine -- kept intact for reference, NOT built.
//  Origin: src/kernel/dynload.inc.
//  The jump-table vector kIndirectCall ($E09F) is not in kernel.asm: the
//  vector-table dispatch mode is not used, and the routine has no call sites.
// ==========================================
//  indirectCall -- stack-based indirect JSR via task vector table
// ==========================================
//  Input:  A = vector index (0..vecCount-1)
//          Vector table at (dataPtr) stores (target - 1) per entry
//  Output: execution lands on target; target's RTS returns to
//          the original caller of kIndirectCall
//  Clobbers: A, Y (X preserved). Uses 2 bytes of the task's own
//          stack (popped by the jump; balanced by the target's RTS).
//  IRQ-safe: stack is per-task (scheduler swaps it), no shared
//          staging memory -- unlike the old kArg-based design.
indirectCall:
    asl                        // index x 2 -> word offset
    tay
    iny                        // Y -> high byte of (target-1)
    lda (dataPtr),y            // push hi byte first
    pha
    dey                        // Y -> low byte
    lda (dataPtr),y
    pha
    rts                        // RTS pops (target-1), adds 1, lands on target
