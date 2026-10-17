// ==========================================
//  ps.asm -- "ps" command task (relocatable)
// ==========================================
//  Lists tasks on the shell's shared screen, then exits.
//  Non-reentrant, single-shot command.
//  Owner discovery: gettid -> taskParent[own] = shell slot -> kShareScreen.
//  Scratch: 7-byte instance buffer at (dataPtr) -- PS_IDX, PS_SLOT,
//  PS_DECBUF(5).

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg are defined in tools/assemble-tasks.asm)

psBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word psEnd - ps             // +5:  code size
    .byte 7, 0, 0                // +7:  data size (7B scratch), ZP base, ZP len
    .word (1 << LIB_STRING)      // +10: libMask
    .byte psNameEnd - psName     // +12: name len
psName:
    .text "ps"
psNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - psBinStart), 0

.import source "ps.inc"
.label psEnd = *
