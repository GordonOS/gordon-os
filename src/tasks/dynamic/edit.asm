// ==========================================
//  edit.asm -- "edit" task (relocatable, .tsk)
// ==========================================
//  Wraps edit.inc -- zero source changes.
//  Nominal origin $5000 (page-aligned; delta_lo=0 for any pool).

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg defined in tools/assemble-tasks.asm)

editBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE | KF_TAKES_ARGS | KF_RE_RUNNABLE | KF_NOT_EVICTABLE
                                 // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word editEnd - edit         // +5:  code size
    .byte 208, 0, 0            // +7:  data size, ZP base, ZP len
    .word (1 << LIB_FILESYS)     // +10: libMask
    .byte editNameEnd - editName // +12: name len
editName:
    .text "edit"
editNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - editBinStart), 0

.import source "edit.inc"
.label editEnd = *
