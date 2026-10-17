// ==========================================
//  pause.asm -- "pause" command task (relocatable)
// ==========================================
//  Pauses a task by ID on the shell's shared screen, then exits.
//  Non-reentrant, single-shot command (shell Phase B).
//  Owner discovery: gettid -> taskParent[own] = shell slot -> kShareScreen.
//  Scratch: 9-byte instance buffer at (dataPtr) -- PAUSE_SLOT,
//  PAUSE_DECBUF (5), PAUSE_NAMLO/PAUSE_NAMHI (2), PAUSE_PARENT (1).
//  Arg from cmdArgsBuf.

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg are defined in tools/assemble-tasks.asm)

pauseBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word pauseEnd - pause       // +5:  code size
    .byte 9, 0, 0                // +7:  data size (9B scratch), ZP base, ZP len
    .word 0                      // +10: libMask = 0 (no libraries)
    .byte pauseNameEnd - pauseName // +12: name len
pauseName:
    .text "pause"
pauseNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - pauseBinStart), 0

.import source "pause.inc"
.label pauseEnd = *
