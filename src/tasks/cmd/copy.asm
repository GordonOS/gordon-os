// ==========================================
//  copy.asm -- "copy" command task (relocatable)
// ==========================================
//  Copies an REU-FS entry to a new name on the shell's shared screen,
//  then exits. Non-reentrant, single-shot command (shell Phase B).
//  Owner discovery: gettid -> taskParent[own] = shell slot -> kShareScreen.
//  Args come from cmdArgsBuf (shell's pcFgPs copies them).
//  Scratch: 22-byte instance buffer at (dataPtr); the 4-page bounce
//  buffer is a runtime kMalloc block (freed automatically on exit).

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg are defined in tools/assemble-tasks.asm)

copyBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word copyEnd - copy         // +5:  code size
    .byte 22, 0, 0               // +7:  data size (22B scratch), ZP base, ZP len
    .word (1 << LIB_FILESYS)     // +10: libMask
    .byte copyNameEnd - copyName // +12: name len
copyName:
    .text "copy"
copyNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - copyBinStart), 0

.import source "copy.inc"
.label copyEnd = *
