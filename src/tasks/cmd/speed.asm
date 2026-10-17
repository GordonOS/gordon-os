// ==========================================
//  speed.asm -- "speed" command task (relocatable)
// ==========================================
//  Declares the speed the whole machine runs at relative to real time:
//  "speed <percent>" writes the kernel's sysSpeedShift and re-derives the
//  kernel's blink and key-repeat tick counts from it, and "speed" with no
//  argument reports the video standard it measures (see speed.inc). Args come
//  from cmdArgsBuf (the shell copies them). Reads/writes kernel data via
//  src/tasks/kernel-data.inc.

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg + kernel-data aliases are defined
//  in tools/assemble-tasks.asm)

speedBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word speedEnd - speed       // +5:  code size
    .byte 0, 0, 0                // +7:  data size, ZP base, ZP len
    .word 0                      // +10: libMask = 0 (no libraries)
    .byte speedNameEnd - speedName  // +12: name len
speedName:
    .text "speed"
speedNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - speedBinStart), 0

.import source "speed.inc"
.label speedEnd = *
