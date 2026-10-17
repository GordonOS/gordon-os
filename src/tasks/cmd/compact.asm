// ==========================================
//  compact.asm -- "compact" command task (relocatable)
// ==========================================
//  Reclaims REU-FS space: packs every live file down to the front of the
//  data region and releases the directory slots held by tombstones, then
//  sets nextFreeOffset so `dir`'s free figure reflects reality.
//
//  Non-reentrant, single-shot command (shell Phase B). Takes no args and
//  reads nothing from cmdArgsBuf. Owner discovery: gettid ->
//  taskParent[own] = shell slot -> kShareScreen.
//
//  HARD CONSTRAINT: all logic lives in compact.inc. There is
//  no kernel space left -- filesys.lib must not grow (a resident task declares
//  it and then holds its pages for the session) -- so this adds no kernel call,
//  jump-table entry, kernel-data byte or lib entry. libMask = 0: no library is loaded.

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg + kernel-data aliases come from
//  tools/assemble-tasks.asm + src/tasks/kernel-data.inc)

// Instance buffer size -- single source of truth: compact.inc asserts that
// its whole layout (CMP_END) fits inside this value.
.const CMP_DATA_SIZE = 240

// MAX_TASKS, TA_DEAD, TA_SWAPPED: src/kernel/kernel-shared.inc (imported
// globally via src/tasks/kernel-data.inc).

compactBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags (no KF_TAKES_ARGS)
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word compactEnd - compact   // +5:  code size
    .byte CMP_DATA_SIZE          // +7:  instance buffer size
    .byte 0, 0                   // +8:  ZP base, ZP len
    .word 0                      // +10: libMask = 0 (no libraries)
    .byte compactNameEnd - compactName  // +12: name len
compactName:
    .text "compact"
compactNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - compactBinStart), 0

.import source "compact.inc"
.label compactEnd = *
