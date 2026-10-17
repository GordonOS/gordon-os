// ==========================================
//  sidrst.asm -- "sidrst" command task (relocatable)
// ==========================================
//  Resets the SID: zeroes all 25 writeable registers ($D400-$D418).
//  Run after killing a music task to silence a stuck note. No screen, no
//  keyboard, no args -- just clears the SID and exits. Tier-0 style:
//  zero fixups (only hardware + fixed jump-table addresses).

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg are defined in tools/assemble-tasks.asm)

sidrstBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word sidrstEnd - sidrst     // +5:  code size
    .byte 0, 0, 0                // +7:  data size, ZP base, ZP len
    .word 0                      // +10: libMask = 0 (no libraries)
    .byte sidrstNameEnd - sidrstName  // +12: name len
sidrstName:
    .text "sidrst"
sidrstNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - sidrstBinStart), 0

.import source "sidrst.inc"
.label sidrstEnd = *
