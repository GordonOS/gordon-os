// ==========================================
//  rename.asm -- "rename" command task (relocatable)
// ==========================================
//  Renames an REU-FS entry on the shell's shared screen, then exits.
//  Non-reentrant, single-shot command (shell Phase B).
//  Owner discovery: gettid -> taskParent[own] = shell slot -> kShareScreen.
//  Args come from cmdArgsBuf (shell's pcFgPs copies them).

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg are defined in tools/assemble-tasks.asm)

renameBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word renameEnd - rename     // +5:  code size
    .byte 4, 0, 0                // +7:  data size (4B scratch), ZP base, ZP len
    .word (1 << LIB_FILESYS)     // +10: libMask
    .byte renameNameEnd - renameName // +12: name len
renameName:
    .text "rename"
renameNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - renameBinStart), 0

.import source "rename.inc"
.label renameEnd = *
