// ==========================================
//  GordonOS -- Boot task
// ==========================================
//  Seeds REU char shadows (dead data, kept for its boot-time timing), then
//  resolves the bitmap font (gordon.fnt -> REU font record) and loads the
//  boot target (shell.tsk) from the REU FS. The FS is built host-side by
//  tools/build-reu.ps1 -- no boot-time seeding. If either load fails -> halt.
//
//  Boot target: shell.tsk spawned with startup args "runbatch boot" --
//  the first shell executes boot.bat once (currently: spawn a shell,
//  then exit), which is the bootstrap hook for initial tasks.
//
//  NOTE: the char-shadow seed loop is dead data in the all-bitmap model
//  (nothing reads $A000-$BFFF), but it must STAY: removing it shifts boot's
//  timing, which moves a scheduler freeze onto the cold boot.

boot:
    // Seed all char shadows with default tail data so
    // no screen ever reads garbage D020/D021 from REU.
    // SEI: the scheduler's context-switch REU DMAs must not interleave
    // with these direct register pokes (M4).
    sei
    lda #DEFAULT_BORDER_COLOR
    sta $C7FE                   // scratch tail: D020 (end of scratchBuf)
    lda #DEFAULT_BG_COLOR
    sta $C7FF                   // scratch tail: D021
    ldx #0
bootSeedLoop:
    lda #SCRATCH_ADDR_LO; sta $df02
    lda #SCRATCH_ADDR_HI; sta $df03
    txa; asl; asl               // slot x 4 = REU hi offset
    clc; adc #>REU_CHAR_SHADOW
    sta $df05                   // REU hi = $A0 + slotx4
    lda #0; sta $df04; sta $df06
    lda #<$0400; sta $df07
    lda #>$0400; sta $df08
    lda #0; sta $df0a
    lda #$90; sta $df01         // C64 -> REU
bootSeedWait:
    lda $df00; and #$40; beq bootSeedWait
    inx
    cpx #8; bcc bootSeedLoop    // slots 0-7
    cli

    // --- resolve the bitmap font (REU-only) -- helper in the $5284
    //     kernel pocket (the boot box is exactly full). ---
    jsr bootFontResolve         // C=0: fontRec* = gordon.fnt FS offset
    bcs bootFailed              // font missing/too short -> halt

    // kDynLoad's header parser writes the binary name to (dataPtr) --
    // point it at a boot-owned scratch buffer. 12 bytes is enough: the
    // parser copies at most 11 chars + a null (dynload.inc:426).
    //
    // WARNING: This buffer ($021F-$022A) OVERLAPS EhBASIC's `ibuffs`
    // ($0221-$02A0 -- `ibuffs = vec_sv+$16`, ehbasic.asm:490) by 10 bytes.
    // It is safe, but NOT because tasks get evicted and restored: the boot
    // task is ONE-SHOT. It runs before any other task exists, is never
    // respawned, and BASIC cannot start until long after it has exited.
    // Nothing else points dataPtr here -- every other kDynLoad caller uses
    // its own instance buffer or a per-slot name buffer (dynload.inc:1561,
    // :1633). BASIC's own line input DOES use ibuffs from $0221, and it
    // needs the full 127 bytes.
    //
    // If anything ever makes that temporal separation untrue, move this to
    // $020D (free, 18B, ends before ibuffs) -- a two-immediate change.
    lda #<$021F
    sta dataPtr
    lda #>$021F
    sta dataPtr+1

    // --- load the boot target from the REU FS ---
    lda #<bootName
    sta kPtr
    lda #>bootName
    sta kPtr+1
    jsr kDynLoad                // SEI-atomic; C=0 -> target spawned + registered
    bcs bootFailed              // C=1 -> failure -> halt
    jmp bootDone                // C=0 -> success

bootFailed:
    sei                         // kDynLoad returned CLI'd on failure -- halt atomically
bootHalt:
    jmp bootHalt

bootDone:
    sei                         // kDynLoad returned CLI'd -- exit atomically
    lda #0
    sta taskState               // mark slot 0 DEAD (curTask = 0)
    jmp reschedule              // never returns

.encoding "screencode_mixed"
// boot target + startup args (loader convention: name\0args\0)
// args run through the shell's startup hook once, then it prompts.
// If boot.bat is missing, "runbatch boot" prints "? not found" and the
// shell still reaches its prompt.
bootFontName:
    .text "gordon.fnt"
    .byte 0
bootName:
    .text "shell.tsk"
    .byte 0
    .text "runbatch boot"
    .byte 0
