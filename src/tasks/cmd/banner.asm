// ==========================================
//  banner.asm -- "banner" command task (relocatable)
// ==========================================
//  "banner <file> <row> <col>" draws the .bnr custom-glyph banner file
//  on the shell's shared screen, then exits. Single-shot command -- the
//  shell loads it, waits for it, and re-enters the prompt.
//  Scratch: 23-byte instance buffer at (dataPtr); the 320-byte row
//  buffer is a 2-page kMalloc block (freed automatically on exit).
//  The shell's cursor position is saved on entry and restored before
//  exit, so the prompt returns where the shell expects it.

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg are defined in tools/assemble-tasks.asm)

bannerBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word bannerEnd - banner     // +5:  code size
    .byte 26, 0, 0               // +7:  data size (26B scratch), ZP base, ZP len
    .word (1 << LIB_FILESYS)     // +10: libMask
    .byte bannerNameEnd - bannerName // +12: name len
bannerName:
    .text "banner"
bannerNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - bannerBinStart), 0

.import source "banner.inc"
.label bannerEnd = *
