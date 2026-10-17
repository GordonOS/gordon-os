// ==========================================
//  format.asm -- "format" command task (relocatable)
// ==========================================
//  Interactive: asks "format? y/n", reads one key, formats the REU-FS.
//  Non-reentrant, single-shot command (shell Phase B).
//  Grabs kbFocus via kSwitchView (its surface IS the shell's, so the
//  display doesn't change) so kReadString sees input.
//  Scratch: 3-byte instance buffer at (dataPtr) -- FORMAT_SLOT,
//  FORMAT_BUF (2).

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg are defined in tools/assemble-tasks.asm)

formatBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word formatEnd - format     // +5:  code size
    .byte 3, 0, 0                // +7:  data size (3B scratch), ZP base, ZP len
    .word (1 << LIB_FILESYS)     // +10: libMask
    .byte formatNameEnd - formatName // +12: name len
formatName:
    .text "format"
formatNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - formatBinStart), 0

.import source "format.inc"
.label formatEnd = *
