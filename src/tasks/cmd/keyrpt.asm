// ==========================================
//  keyrpt.asm -- "keyrpt" command task (relocatable)
// ==========================================
//  Sets the keyboard repeat timing at runtime: "keyrpt <delay> <repeat>"
//  writes the delay-before-repeat and repeat-interval values into the
//  operand bytes of the kernel's lda #KEYB_INITIAL / lda #KEYB_REPEAT
//  immediates (self-modifying -- the kernel runs from RAM). No screen,
//  no keyboard. Args come from cmdArgsBuf (the shell copies them).
//  Reads/writes kernel data via src/tasks/kernel-data.inc.

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg + kernel-data aliases are defined
//  in tools/assemble-tasks.asm)

keyrptBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word keyrptEnd - keyrpt     // +5:  code size
    .byte 0, 0, 0                // +7:  data size, ZP base, ZP len
    .word 0                      // +10: libMask = 0 (no libraries)
    .byte keyrptNameEnd - keyrptName  // +12: name len
keyrptName:
    .text "keyrpt"
keyrptNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - keyrptBinStart), 0

.import source "keyrpt.inc"
.label keyrptEnd = *
