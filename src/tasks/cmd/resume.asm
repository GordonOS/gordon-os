// ==========================================
//  resume.asm -- "resume" command task (relocatable)
// ==========================================
//  Resumes a paused task by ID on the shell's shared screen, then exits.
//  Non-reentrant, single-shot command (shell Phase B).
//  Owner discovery: gettid -> taskParent[own] = shell slot -> kShareScreen.
//  Scratch: 8-byte instance buffer at (dataPtr) -- RESUME_SLOT,
//  RESUME_DECBUF (5), RESUME_NAMLO/RESUME_NAMHI (2). Arg from cmdArgsBuf.

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg are defined in tools/assemble-tasks.asm)

resumeBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word resumeEnd - resume     // +5:  code size
    .byte 8, 0, 0                // +7:  data size (8B scratch), ZP base, ZP len
    .word 0                      // +10: libMask = 0 (no libraries)
    .byte resumeNameEnd - resumeName // +12: name len
resumeName:
    .text "resume"
resumeNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - resumeBinStart), 0

.import source "resume.inc"
.label resumeEnd = *
