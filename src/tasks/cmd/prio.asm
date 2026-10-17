// ==========================================
//  prio.asm -- "prio" command task (relocatable)
// ==========================================
//  Sets a task's priority on the shell's shared screen, then exits.
//  Non-reentrant, single-shot command (shell Phase B).
//  Owner discovery: gettid -> taskParent[own] = shell slot -> kShareScreen.
//  Scratch: 10-byte instance buffer at (dataPtr) -- PRIO_SLOT, PRIO_PRIO,
//  PRIO_PARENT, PRIO_NAMLO/PRIO_NAMHI, PRIO_DECBUF (5). Arg from
//  cmdArgsBuf ("<id> <n>").

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg are defined in tools/assemble-tasks.asm)

prioBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word prioEnd - prio         // +5:  code size
    .byte 10, 0, 0               // +7:  data size (10B scratch), ZP base, ZP len
    .word 0                      // +10: libMask = 0 (no libraries)
    .byte prioNameEnd - prioName // +12: name len
prioName:
    .text "prio"
prioNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - prioBinStart), 0

.import source "prio.inc"
.label prioEnd = *
