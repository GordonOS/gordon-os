// ==========================================
//  clock.asm -- CIA time at (0,0) (relocatable)
// ==========================================
//  Wraps clock.inc -- zero source changes.
//  Shares the shell's screen (slot 1) via kShareScreen.
//  Nominal origin $5000 (page-aligned; delta_lo=0 for any pool).

.encoding "screencode_mixed"

// (syscall alias defined globally in tools/assemble-tasks.asm)

clockBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word clockEnd - clock       // +5:  code size
    .byte 0, 0, 0                // +7:  data size, ZP base, ZP len
    .word (1 << LIB_TIME)        // +10: libMask
    .byte clockNameEnd - clockName // +12: name len
clockName:
    .text "clock"
clockNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - clockBinStart), 0

.import source "clock.inc"
.label clockEnd = *
