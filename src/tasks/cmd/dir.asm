// ==========================================
//  dir.asm -- "dir" command task (relocatable)
// ==========================================
//  Lists REU-FS directory on the shell's shared screen, then exits.
//  Non-reentrant, single-shot command.
//  Owner discovery: gettid -> taskParent[own] = shell slot -> kShareScreen.
//  Scratch: 36-byte instance buffer at (dataPtr) -- DIR_IDX, DIR_LINEBUF
//  (12), DIR_DECBUF (6), DIR_PAD (1), DIR_SZLO/DIR_SZHI (2), DIR_MODE,
//  DIR_LINES, DIR_PATTERN (12).

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg are defined in tools/assemble-tasks.asm)

dirBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word dirEnd - dir           // +5:  code size
    .byte 36, 0, 0               // +7:  data size (36B scratch), ZP base, ZP len
    .word (1 << LIB_FILESYS) | (1 << LIB_STRING)  // +10: libMask
    .byte dirNameEnd - dirName   // +12: name len
dirName:
    .text "dir"
dirNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - dirBinStart), 0

.import source "dir.inc"
.label dirEnd = *
