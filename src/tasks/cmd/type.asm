// ==========================================
//  type.asm -- "type" command task (relocatable)
// ==========================================
//  Prints a REU-FS file's contents (raw screen codes) on the shell's
//  shared screen, then exits. Non-reentrant, single-shot command.
//  Takes the FULL filename (extension required -- nothing is appended).
//  Streams the file in 80-byte REU DMA windows (same logic as the
//  shell's runbatch).
//  Owner discovery: gettid -> taskParent[own] = shell slot -> kShareScreen.
//  Scratch: 93-byte instance buffer at (dataPtr) -- TYPE_WIN(80),
//  TYPE_POS(2), TYPE_SIZE(2), TYPE_OFF(2), TYPE_SLOT(1), TYPE_LEN(1),
//  TYPE_END(2), TYPE_CUR(2), TYPE_PEND(1). See type.inc.

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg are defined in tools/assemble-tasks.asm)

typeBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word typeEnd - type         // +5:  code size
    .byte 95, 0, 0               // +7:  data size (95B scratch), ZP base, ZP len
    .word (1 << LIB_FILESYS)     // +10: libMask
    .byte typeNameEnd - typeName // +12: name len
typeName:
    .text "type"
typeNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - typeBinStart), 0

.import source "type.inc"
.label typeEnd = *
