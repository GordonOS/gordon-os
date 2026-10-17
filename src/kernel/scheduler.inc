// ==========================================
//  GordonOS -- Scheduler (context switch)
// ==========================================
//
//  All context switches use REU DMA (~256 cycles).
//  REU is required -- boot halts if not detected.
//
//  Stack images stored in REU at task x 256.
//  reschedule() skips save, used by exit/kill/yield.

// ==========================================
//  switchTask -- full save + advance + restore
// ==========================================
switchTask:
    tsx
    stx saveSP

    ldx curTask
    lda saveSP
    sta savedSP,x               // savedSP[curTask] = SP

    // save kernel ZP so scrollScreen/clearScreen survive preemption
    lda kPtr; sta taskKPtrLo,x
    lda kPtr+1; sta taskKPtrHi,x
    lda srcPtr; sta taskSrcPtrLo,x
    lda srcPtr+1; sta taskSrcPtrHi,x
    // save kArg/kResult to REU (8 bytes at $4000)
    //  `task x 8` IS the low byte: REU_KARG_BASE is page-aligned and
    //  MAX_TASKS x 8 <= 256 (asserted), so no add and no carry chain is needed
    //  -- and none is possible to get wrong, so the base MUST stay page-aligned:
    //  a low-byte add would cost `clc / adc #<base` here plus 2 more in the
    //  hi-byte add.
    txa; asl; asl; asl            // task x 8
    sta $df04
    lda #>REU_KARG_BASE
    sta $df05
    lda #0; sta $df06
    lda #<kArg; sta $df02
    lda #>kArg; sta $df03
    lda #8; sta $df07
    lda #0; sta $df08; sta $df0a
    lda #$90; sta $df01           // C64->REU, immediate
svKargWait:
    lda $df00; and #$40; beq svKargWait

    //  --- the pixel op does not cross a switch (gfxOp) ---
    //  It is kernel state that a drawing call reads, so it must not outlive the task
    //  that set it: left standing, the next task to draw would inherit a XOR from a
    //  preview that was interrupted mid-band. Clearing it IS the hand-over -- gfx.lib
    //  re-reads the byte for every pixel, and a task re-asserts its own op each time
    //  it draws -- so nothing needs saving per slot. A per-slot REU shadow would
    //  remove the one residue this leaves (a band cut in half by a preemption has its
    //  tail SET rather than XORed, so erasing it can flip a few pixels of the picture
    //  underneath. A per-slot REU shadow would remove that, but a
    //  second 1-byte DMA is ~20 bytes at each of the two sites and the core's headroom
    //  is 15.
    lda #0
    sta gfxOp

    // save task-private data pointer (re-entrant task support)
    lda dataPtr; sta taskDataPtrLo,x
    lda dataPtr+1; sta taskDataPtrHi,x

    // --- save per-task ZP region via REU DMA (if declared) ---
    ldx curTask
    lda taskZpLen,x
    beq svZpDone
    lda taskZpBase,x; sta $df02    // C64 addr lo = ZP base
    lda #$00; sta $df03            // C64 addr hi (ZP is always $00xx)
    lda taskZpReuLo,x; sta $df04   // REU shadow addr lo
    lda taskZpReuHi,x; sta $df05   // REU shadow addr hi
    lda #$00; sta $df06            // REU bank 0
    lda taskZpLen,x; sta $df07     // length lo
    lda #$00; sta $df08            // length hi (max 240)
    lda #REU_CTRL_INC; sta $df0a   // increment mode
    lda #$90; sta $df01            // C64->REU, immediate
svZpWait:
    lda $df00; and #REU_ST_EOB; beq svZpWait
svZpDone:

    jsr saveStackReu
    // save per-task kReadString state (per-task arrays, not buffer tails)
    ldx curTask
    lda readStrBuf; sta taskReadStrBufLo,x
    lda readStrBuf+1; sta taskReadStrBufHi,x
    lda readStrLen; sta taskReadStrLen,x
    lda readStrMax; sta taskReadStrMax,x
    lda readStrPos; sta taskReadStrPos,x
    lda readStrStartRow; sta taskReadStrStartRow,x

    // save per-task cursor to its screen's matrix tail (all-bitmap)
    ldx curTask
    lda screenAlloc,x
    bne svCurSave
    jmp svNoCurSave             // no surface (label out of branch range)
svCurSave:
    jsr surfDisplayed            // C=1 iff this task's surface is displayed
    bcs svSaveCurDisplayed
    // background surface -> col/row to slot tail (+$23e8)
    ldx curTask                  // surfDisplayed clobbered X
    lda taskGfxReuLo,x; clc; adc #<(GFX_SLOT_MATRIX + GFX_CURSOR_COL); sta $df04
    lda taskGfxReuHi,x; adc #>(GFX_SLOT_MATRIX + GFX_CURSOR_COL); sta $df05
    lda taskGfxReuBank,x; adc #0; sta $df06
    lda #<cursorState; sta $df02
    lda #>cursorState; sta $df03
    lda #2; sta $df07
    lda #0; sta $df08; sta $df0a
    lda #$90; sta $df01           // C64->REU
svWaitCurSave:
    lda $df00; and #REU_ST_EOB; beq svWaitCurSave
    jmp svSaveCurOn
svSaveCurDisplayed:
    lda cursorCol; sta GFX_MATRIX_BASE + GFX_CURSOR_COL
    lda cursorRow; sta GFX_MATRIX_BASE + GFX_CURSOR_ROW
svSaveCurOn:
    ldx curTask                  // surfDisplayed clobbered X
    cpx kbFocus
    beq svSaveOnFocused
    // cursorOn -> slot+$23ea (1 byte)
    lda taskGfxReuLo,x; clc; adc #<(GFX_SLOT_MATRIX + GFX_CURSOR_ON); sta $df04
    lda taskGfxReuHi,x; adc #>(GFX_SLOT_MATRIX + GFX_CURSOR_ON); sta $df05
    lda taskGfxReuBank,x; adc #0; sta $df06
    lda #<cursorOn; sta $df02
    lda #>cursorOn; sta $df03
    lda #1; sta $df07
    lda #0; sta $df08; sta $df0a
    lda #$90; sta $df01           // C64->REU
svWaitCurOnSave:
    lda $df00; and #REU_ST_EOB; beq svWaitCurOnSave
    jmp svNoCurSave
svSaveOnFocused:
    lda cursorOn;  sta GFX_MATRIX_BASE + GFX_CURSOR_ON
svNoCurSave:
    //  --- a display change the keyboard asked for is applied HERE, not in kbPoll.
    //      kbPoll runs before switchTask in the same tick, and the switch writes kArg
    //      (fromTask) and swaps two surfaces through the kReu* calls, whose
    //      parameters ARE kArg -- applied there, those writes land in the middle of
    //      whatever the running task had in the block. Here the outgoing task's kArg
    //      is already saved above and the incoming task's is restored below, so
    //      nothing a task can see is touched. kbScreenGate returns to svNoReq when
    //      no key asked for anything, which is every tick but the F-key's. ---
    jmp kbScreenGate
svNoReq:

    // --- priority: stay on same task if slices remain ---
    ldx curTask
    dec taskSliceLeft,x
    bne svSameTask

    lda taskPriority,x
    sta taskSliceLeft,x
    jsr findNextAlive
    jmp svRestore

svSameTask:
    jmp svRestoreStack

svRestore:
    //  SPRITE HAND-OVER. The install must run on the restore path and NOWHERE
    //  before it: it clobbers kPtr and kArg, and this routine has just saved the
    //  outgoing task's copies and is about to load the incoming task's -- so the
    //  clobber is invisible. Called from the tick, and from every path that goes
    //  through here (yield, sleep, exit/kill), which is what makes a display
    //  change made inside an interrupt land without a task being involved.
    jsr sprInstall
    // restore per-task kReadString state
    ldx curTask
    lda taskReadStrBufLo,x; sta readStrBuf
    lda taskReadStrBufHi,x; sta readStrBuf+1
    lda taskReadStrLen,x; sta readStrLen
    lda taskReadStrMax,x; sta readStrMax
    lda taskReadStrPos,x; sta readStrPos
    lda taskReadStrStartRow,x; sta readStrStartRow

    // restore per-task cursor from its screen's matrix tail (all-bitmap)
    ldx curTask
    lda screenAlloc,x
    bne svCurRest
    jmp svNoCurRest             // no surface (label out of branch range)
svCurRest:
    jsr surfDisplayed            // C=1 iff this task's surface is displayed
    bcs svRestCurDisplayed
    // background surface -> slot tail -> cursorState (col/row at +$23e8)
    ldx curTask                  // surfDisplayed clobbered X
    lda taskGfxReuLo,x; clc; adc #<(GFX_SLOT_MATRIX + GFX_CURSOR_COL); sta $df04
    lda taskGfxReuHi,x; adc #>(GFX_SLOT_MATRIX + GFX_CURSOR_COL); sta $df05
    lda taskGfxReuBank,x; adc #0; sta $df06
    lda #<cursorState; sta $df02
    lda #>cursorState; sta $df03
    lda #2; sta $df07
    lda #0; sta $df08; sta $df0a
    lda #$91; sta $df01           // REU->C64
svWaitCurRest:
    lda $df00; and #REU_ST_EOB; beq svWaitCurRest
    jmp svRestCurOn
svRestCurDisplayed:
    lda GFX_MATRIX_BASE + GFX_CURSOR_COL; sta cursorCol
    lda GFX_MATRIX_BASE + GFX_CURSOR_ROW; sta cursorRow
svRestCurOn:
    ldx curTask                  // surfDisplayed clobbered X
    cpx kbFocus
    beq svRestOnFocused
    // slot+$23ea -> cursorOn (1 byte)
    lda taskGfxReuLo,x; clc; adc #<(GFX_SLOT_MATRIX + GFX_CURSOR_ON); sta $df04
    lda taskGfxReuHi,x; adc #>(GFX_SLOT_MATRIX + GFX_CURSOR_ON); sta $df05
    lda taskGfxReuBank,x; adc #0; sta $df06
    lda #<cursorOn; sta $df02
    lda #>cursorOn; sta $df03
    lda #1; sta $df07
    lda #0; sta $df08; sta $df0a
    lda #$91; sta $df01           // REU->C64
svWaitCurOnRest:
    lda $df00; and #REU_ST_EOB; beq svWaitCurOnRest
    jmp svNoCurRest
svRestOnFocused:
    lda GFX_MATRIX_BASE + GFX_CURSOR_ON;  sta cursorOn
svNoCurRest:

    // --- restore per-task ZP region via REU DMA (if declared) ---
    ldx curTask
    lda taskZpLen,x
    beq svZpRestDone
    lda taskZpBase,x; sta $df02    // C64 addr lo = ZP base
    lda #$00; sta $df03
    lda taskZpReuLo,x; sta $df04
    lda taskZpReuHi,x; sta $df05
    lda #$00; sta $df06
    lda taskZpLen,x; sta $df07
    lda #$00; sta $df08
    lda #REU_CTRL_INC; sta $df0a
    lda #$91; sta $df01            // REU->C64, immediate
svZpRestWait:
    lda $df00; and #REU_ST_EOB; beq svZpRestWait
svZpRestDone:
    // restore kernel ZP for this task
    lda taskKPtrLo,x; sta kPtr
    lda taskKPtrHi,x; sta kPtr+1
    lda taskSrcPtrLo,x; sta srcPtr
    lda taskSrcPtrHi,x; sta srcPtr+1
    // restore kArg/kResult from REU (8 bytes at $4000)
    //  Same aligned-base trick as the save path -- see the note there.
    txa; asl; asl; asl            // task x 8
    sta $df04
    lda #>REU_KARG_BASE
    sta $df05
    lda #0; sta $df06
    lda #<kArg; sta $df02
    lda #>kArg; sta $df03
    lda #8; sta $df07
    lda #0; sta $df08; sta $df0a
    lda #$91; sta $df01           // REU->C64, immediate
svKargRestWait:
    lda $df00; and #$40; beq svKargRestWait
    // restore task-private data pointer (re-entrant task support)
    lda taskDataPtrLo,x; sta dataPtr
    lda taskDataPtrHi,x; sta dataPtr+1
svRestoreStack:
    jmp restoreStack            // restoreStack's rts goes to irqRet, never returns here

// ==========================================
//  saveStackReu -- DMA: $0100+saveSP -> REU_STACK_BASE + taskx256 + saveSP
//  Only saves the used portion of the stack.
// ==========================================
saveStackReu:
    lda #$00
    sta $df0a                   // address control: increment
    lda saveSP
    sta $df02                   // C64 low  = $0100 + saveSP
    lda #$01
    sta $df03                   // C64 high = $01
    ldx curTask                 // reload X (caller may have clobbered)
    lda taskStackReuLo,x
    clc
    adc saveSP
    sta $df04                   // REU low  = base + saveSP
    lda taskStackReuHi,x
    adc #$00
    sta $df05                   // REU high
    lda #0
    sta $df06                   // bank 0
    // length = $0100 - saveSP
    sec
    lda #$00
    sbc saveSP
    sta $df07                   // length lo
    lda #$01
    sbc #$00
    sta $df08                   // length hi (1 if saveSP=0, else 0)
    lda #$90                    // C64->REU, immediate
    sta $df01
    rts

// ==========================================
//  findNextAlive -- round-robin, skip DEAD
// ==========================================
findNextAlive:
    ldy #0
fnaLoop:
    inc curTask
    lda curTask
    cmp totalTasks
    bcc fnaChk
    lda #0
    sta curTask
fnaChk:
    ldx curTask
    lda taskState,x
    cmp #TA_ALIVE               // only schedule ALIVE tasks
    bne fnaNext
    jsr childGate               // C=1: a thread child whose parent is not running
    bcs fnaNext
fnaDone:
    rts
fnaNext:
    iny
    cpy totalTasks
    bcc fnaLoop
    // No alive tasks -- reboot GordonOS.
    //  Through warmBootReset, which drops the state a warm reboot must not carry
    //  over (every library, and the sprite intent) and then `jmp start` -- see
    //  kernel/warm-boot-reset.inc. A reboot that left a library loaded would leave
    //  its patched call entries pointing into pages the fresh bitmap calls free.
    jmp warmBootReset

// ==========================================
//  restoreStack -- read savedSP, then DMA REU -> $0100 + set SP
// ==========================================
restoreStack:
    ldx curTask
    lda savedSP,x
    sta saveSP
    // fall through to restoreStackReu

// ==========================================
//  restoreStackReu -- DMA: REU_STACK_BASE + taskx256 + saveSP -> $0100+saveSP
//  Only restores the portion that was saved (uses saveSP from savedSP).
// ==========================================
restoreStackReu:
    lda #$00
    sta $df0a                   // address control: increment
    lda saveSP
    sta $df02                   // C64 low  = $0100 + saveSP
    lda #$01
    sta $df03                   // C64 high = $01
    ldx curTask                 // reload X
    lda taskStackReuLo,x
    clc
    adc saveSP
    sta $df04                   // REU low  = base + saveSP
    lda taskStackReuHi,x
    adc #$00
    sta $df05                   // REU high
    lda #0
    sta $df06                   // bank 0
    // length = $0100 - saveSP
    sec
    lda #$00
    sbc saveSP
    sta $df07                   // length lo
    lda #$01
    sbc #$00
    sta $df08                   // length hi (1 if saveSP=0, else 0)
    lda #$91                    // REU->C64, immediate
    sta $df01
    ldx saveSP
    txs
    rts

// ==========================================
//  reschedule -- skip save, find next, restore ZP + stack
// ==========================================
reschedule:
    jsr findNextAlive
    jmp svRestore               // restore screen, ZP, dataPtr, stack -> irqRet
