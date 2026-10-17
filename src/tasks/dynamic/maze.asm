// ==========================================
//  maze.asm -- 10 PRINT maze (relocatable + re-runnable)
// ==========================================
//  Wraps maze.inc -- zero source changes.
//  Nominal origin $5000 (page-aligned; delta_lo=0 for any pool).

.encoding "screencode_mixed"

// (syscall alias defined globally in tools/assemble-tasks.asm)

mazeBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE | KF_RE_RUNNABLE  // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word mazeEnd - maze         // +5:  code size
    .byte 0, 0, 0                // +7:  data size, ZP base, ZP len
    .word 0                      // +10: libMask = 0 (no libraries)
    .byte mazeNameEnd - mazeName // +12: name len
mazeName:
    .text "maze"
mazeNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - mazeBinStart), 0

.import source "maze.inc"
.label mazeEnd = *
