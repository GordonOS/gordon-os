// ==========================================
//  shell.asm --  shell task (relocatable + reentrant)
// ==========================================
//  Wraps shell.inc -- source changes limited to the
//  run-command fallback (registry is empty in the relocated world).
//  Nominal origin $5000 (page-aligned; delta_lo=0 for any pool).
//
//  Kernel API aliases below are RESOLVED addresses from the kernel
//  build (kernel.inc / kernel-vars.inc) -- NOT stable jump-table
//  entries. Keep them in sync with the kernel. (Guard: src/main.asm
//  .asserts these addresses against the kernel labels -- a kernel move
//  now fails the kernel build instead of breaking the shell
//  silently.)

.encoding "screencode_mixed"

// (syscall alias defined globally in tools/assemble-tasks.asm)

// --- ZP (task-owned, scheduler-saved) -- shared ones in assemble-tasks.asm ---

// --- Kernel data addresses (shared: src/tasks/kernel-data.inc) ---
//  taskState/totalTasks/taskPriority/screenAlloc/taskParent/reuFs*/osName/
//  osVersion/taskLo/taskHi/dynTask* are defined once in kernel-data.inc,
//  imported globally by tools/assemble-tasks.asm.

// --- Kernel constants (shell-specific) ---
// MAX_DYN_TASKS, MAX_RESIDENTS, TA_ALIVE/PAUSED/SLEEPING: src/kernel/
// kernel-shared.inc (imported globally via src/tasks/kernel-data.inc).

//  Per-instance buffer size. The header emits it, so it has to be known here --
//  before shell.inc's offset block is read. This is the ONE place the value is
//  written: shell.inc asserts it against its highest offset (POOLRS + 1), so
//  adding an offset without enlarging the buffer fails the build.
.const SHELL_DATA_SIZE = 105
// Pool geometry (POOL_*) comes from src/kernel/pool.inc, imported by
// tools/assemble-tasks.asm before this file -- do not redefine here.

// ==========================================
//  Header (256-byte paged)
// ==========================================
shellBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE | KF_REENTRANT | KF_TAKES_ARGS
                                 // +2:  flags -- KF_TAKES_ARGS delivers
                                 //      "run shell <cmd>" into the instance buffer
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word shellEnd - shell              // +5:  code size (code body only)
    .byte SHELL_DATA_SIZE         // +7:  data size (per-instance heap)
    .byte 0, 0                   // +8:  ZP base, +9: ZP len (dataPtr is
                                 //      scheduler-saved -- nothing here)
    .word (1 << LIB_FILESYS) | (1 << LIB_STRING)  // +10: libMask
    .byte shellNameEnd - shellName // +12: name len
shellName:
    .text "shell"
shellNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - shellBinStart), 0

.import source "shell.inc"
.label shellEnd = *
