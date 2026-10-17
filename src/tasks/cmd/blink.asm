// ==========================================
//  blink.asm -- "blink" command task (relocatable)
// ==========================================
//  Sets the cursor blink interval at runtime: "blink <n>" writes n to
//  the kernel's blinkRate byte (ticks per toggle, ~60 Hz; default 30).
//  No screen, no keyboard. Args come from cmdArgsBuf (the shell copies
//  them). Reads/writes kernel data via src/tasks/kernel-data.inc.

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg + kernel-data aliases are defined
//  in tools/assemble-tasks.asm)

blinkBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word blinkEnd - blink       // +5:  code size
    .byte 0, 0, 0                // +7:  data size, ZP base, ZP len
    .word 0                      // +10: libMask = 0 (no libraries)
    .byte blinkNameEnd - blinkName  // +12: name len
blinkName:
    .text "blink"
blinkNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - blinkBinStart), 0

.import source "blink.inc"
.label blinkEnd = *
