// ==========================================
//  kill.asm -- "kill" command task (relocatable)
// ==========================================
//  Kills a task by ID on the shell's shared screen, then exits.
//  Non-reentrant, single-shot command.
//  Owner discovery: gettid -> taskParent[own] = shell slot -> kShareScreen.
//  Scratch: 9-byte instance buffer at (dataPtr) -- KILL_SLOT, KILL_DECBUF
//  (5), KILL_NAMLO/KILL_NAMHI (2), KILL_SIDUSE (1). Arg from cmdArgsBuf.
//
//  It also clears the drone a killed music task leaves: a task that declares
//  sid.lib (or whose spawner does) has its chip zeroed after the kill, because
//  a killed player never runs its own hush. See kill.inc and docs/lib-sid.md.

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg are defined in tools/assemble-tasks.asm)

killBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word killEnd - kill         // +5:  code size
    .byte 9, 0, 0                // +7:  data size (9B scratch), ZP base, ZP len
    .word 0                      // +10: libMask = 0 (no libraries)
    .byte killNameEnd - killName // +12: name len
killName:
    .text "kill"
killNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - killBinStart), 0

.import source "kill.inc"
.label killEnd = *
