// ==========================================
//  REMOVED kernel routines -- kept intact for reference, NOT built.
//  Origin: src/kernel/reu.inc.
//  The jump-table vectors kReuSwap ($E072), kReuVerify ($E075) and
//  kReuRetrigger ($E07E) are not in kernel.asm: no call sites.
// ==========================================

// --- kReuSwap -- DMA: exchange C64 <-> REU (increment mode) ---
//  Atomically swaps contents of C64 and REU memory regions.
//  Both regions are modified. Length must match.
//  Set kArg+5 non-zero to enable AUTOLOAD (saves params for kReuRetrigger).
//  Clobbers: A
//  Modifies: $DF00-$DF0A, C64 memory at kPtr, REU memory at kArg..kArg+2
reuSwap:
    php
    sei
    jsr reuSetup
    lda #REU_CTRL_INC
    sta $df0a
    lda #REU_EXEC | REU_CMD_SWAP
    ldy kArg+5; beq rsNoAl
    ora #REU_AUTOLOAD
    sta reuLastCmd
rsNoAl:
    sta $df01
    jsr reuWait
    plp
    rts

// --- kReuVerify -- DMA: compare C64 vs REU (increment mode) ---
//  Compares C64 memory against REU without copying.
//  Returns: C=0 if identical, C=1 if mismatch found.
//  Set kArg+5 non-zero to enable AUTOLOAD (saves params for kReuRetrigger).
//  Clobbers: A
//  Modifies: $DF00-$DF0A
reuVerify:
    php
    sei
    jsr reuSetup
    lda #REU_CTRL_INC
    sta $df0a
    lda #REU_EXEC | REU_CMD_VERIFY
    ldy kArg+5; beq rvNoAl
    ora #REU_AUTOLOAD
    sta reuLastCmd
rvNoAl:
    sta $df01
    jsr reuWait
    and #REU_ST_FAULT           // check FAULT from captured status (A=reuWait result)
    bne rvFail
    plp
    clc                         // C=0 -> identical (set after PLP so caller sees it)
    rts
rvFail:
    plp
    sec                         // C=1 -> mismatch (set after PLP so caller sees it)
    rts

// --- kReuRetrigger -- re-execute last AUTOLOAD-enabled transfer ---
//  Re-triggers the last DMA that was set up with kArg+5 non-zero.
//  The REU restores the original address/length from the AUTOLOAD
//  save buffer and re-executes the same transfer. No need to
//  reconfigure registers -- just writes $DF01 with the saved command.
//  Clobbers: A
//  Modifies: $DF00-$DF0A
reuRetrigger:
    php
    sei
    lda reuLastCmd              // saved command (has EXEC+AUTOLOAD+type)
    beq rtrRts                  // never configured -- skip
    sta $df01
    jsr reuWait
rtrRts:
    plp
    rts
