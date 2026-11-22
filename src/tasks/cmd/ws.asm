// ==========================================
//  ws.asm -- "ws" command task (relocatable)
// ==========================================
//  Exercises this task's per-task REU workspace (REU_WS_BASE + slot x
//  REU_WS_SIZE) and reports three checks, then exits.
//  Non-reentrant, single-shot command.
//  Owner discovery: gettid -> taskParent[own] = shell slot -> kShareScreen.
//  Scratch: 15-byte instance buffer at (dataPtr) -- WS_BUF(8), WS_DEC(5),
//  WS_SLOT(1), WS_TMP(1).

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg are defined in tools/assemble-tasks.asm)

wsBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word wsEnd - ws             // +5:  code size
    .byte 15, 0, 0               // +7:  data size (15B scratch), ZP base, ZP len
    .word (1 << LIB_STRING)      // +10: libMask (kByteToDec)
    .byte wsNameEnd - wsName     // +12: name len
wsName:
    .text "ws"
wsNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - wsBinStart), 0

.import source "ws.inc"
.label wsEnd = *
