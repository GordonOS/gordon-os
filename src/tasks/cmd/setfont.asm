// ==========================================
//  setfont.asm -- "setfont" command task (relocatable)
// ==========================================
//  Loads a .fnt file from the REU FS -- switches the system font at
//  runtime by updating the kernel's REU font record (REU-only font).
//  Takes the BASE filename (no extension) -- ".fnt" is appended, like
//  the shell's `runbatch` appends ".bat".
//  Owner discovery: gettid -> taskParent[own] = shell slot -> kShareScreen.
//  No instance buffer -- the name is parsed + extended in cmdArgsBuf.

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg/FONT_REC_* are defined in
//  tools/assemble-tasks.asm / kernel-data.inc)

setfontBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word setfontEnd - setfont   // +5:  code size
    .byte 0, 0, 0                // +7:  data size (0), ZP base, ZP len
    .word 0                      // +10: libMask = 0 (no libraries)
    .byte setfontNameEnd - setfontName // +12: name len
setfontName:
    .text "setfont"
setfontNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - setfontBinStart), 0

.import source "setfont.inc"
.label setfontEnd = *
