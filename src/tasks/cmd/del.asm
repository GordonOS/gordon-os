// ==========================================
//  del.asm -- "del" command task (relocatable)
// ==========================================
//  Deletes every REU-FS entry matching a wildcard pattern on the
//  shell's shared screen, then exits. Non-reentrant, single-shot
//  command (shell Phase B).
//  Owner discovery: gettid -> taskParent[own] = shell slot -> kShareScreen.
//  Args come from cmdArgsBuf (shell's pcFgPs copies them).
//  Scratch: 27-byte instance buffer at (dataPtr).

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg are defined in tools/assemble-tasks.asm)

delBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word delEnd - del           // +5:  code size
    .byte 27, 0, 0               // +7:  data size (27B scratch), ZP base, ZP len
    .word (1 << LIB_FILESYS) | (1 << LIB_STRING)  // +10: libMask
    .byte delNameEnd - delName   // +12: name len
delName:
    .text "del"
delNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - delBinStart), 0

.import source "del.inc"
.label delEnd = *
