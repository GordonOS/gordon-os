// ==========================================
//  time.asm -- "time" command task (relocatable)
// ==========================================
//  Shows or sets the CIA TOD clock on the shell's shared screen, then
//  exits. Non-reentrant, single-shot command.
//  Owner discovery: gettid -> taskParent[own] = shell slot -> kShareScreen.
//  No instance buffer needed (parsing uses kArg + X/Y). Args come from
//  cmdArgsBuf (shell copies them).

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg are defined in tools/assemble-tasks.asm)

timeBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word timeEnd - time         // +5:  code size
    .byte 0, 0, 0                // +7:  data size (0), ZP base, ZP len
    .word (1 << LIB_TIME)        // +10: libMask
    .byte timeNameEnd - timeName // +12: name len
timeName:
    .text "time"
timeNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - timeBinStart), 0

.import source "time.inc"
.label timeEnd = *
