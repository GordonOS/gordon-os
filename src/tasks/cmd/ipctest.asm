// ==========================================
//  ipctest.asm -- "ipctest" command task (relocatable)
// ==========================================
//  Multi-channel ipc.lib self-test. Run it by typing:  ipctest
//  (the external .com path, so the shell waits for it and the verdict is on
//  screen before the next prompt). Declares ipc.lib (libMask bit 6).
//
//  A permanent runtime self-test -- the ipc.lib counterpart of `fstest`
//  (filesys.lib). Re-run it after any change to src/libs/ipc.asm and expect
//  `ipc PASS`; docs/lib-ipc.md -> *How it was proved* records what it covers.
//
//  Owner discovery + screen protocol: same as ps/fstest
//  (gettid -> taskParent[own] = shell slot -> kShareScreen -> kLockScreen).
//  Non-reentrant on purpose: the channels are shared library data, so a
//  second copy of this test would be a second producer on the same channel --
//  exactly what the library's contract forbids. Flags $01 = a second `ipctest`
//  is refused with "? already running" rather than allowed to interleave.

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg are defined in tools/assemble-tasks.asm)

ipctestBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags (single instance)
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word ipctestEnd - ipctest   // +5:  code size
    .byte 0, 0, 0                // +7:  data size 0, ZP base, ZP len
    .word (1 << LIB_IPC)         // +10: libMask
    .byte ipctestNameEnd - ipctestName  // +12: name len
ipctestName:
    .text "ipctest"
ipctestNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - ipctestBinStart), 0

.import source "ipctest.inc"
.label ipctestEnd = *
