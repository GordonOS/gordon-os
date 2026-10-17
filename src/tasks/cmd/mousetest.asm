// ==========================================
//  mousetest.asm -- "mousetest" command task (relocatable)
// ==========================================
//  Run it by typing:  mtest
//  A .com is typed by its own name -- `run <name>` appends ".tsk" and is for
//  tasks only. The fsName is `mtest.com`, not `mousetest.com`: the REU FS caps a
//  filename at 11 characters and the typed command IS the FS name
//  (docs/lib-filesys.md).
//
//  The 1351 protocol readout, written
//  TASK-LOCAL on purpose: it reads the SID's POT registers and the CIA port
//  directly, through no library and without consulting `mouseKind`, so it can
//  diagnose a machine the OS believes has no mouse at all. `mousetest.inc` has
//  the readout's shape and the protocol it settles.
//  It also declares LIB_MOUSE and can take the same sample through `mouse.lib`
//  instead -- the 'l' key -- which is step 3.2's proof that the library reads
//  the hardware identically. The two paths are the same numbers or the library
//  is wrong; `mouseKind` is still never consulted either way.
//
//  No arguments. It shares the shell's screen like every other .com command,
//  samples once every MT_REFRESH displayed frames, and exits on a key
//  (docs/lib-mouse.md).

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg + kernel-data aliases are defined
//  in tools/assemble-tasks.asm)

mousetestBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word mousetestEnd - mousetest  // +5:  code size
    .byte 0, 0, 0                // +7:  data size, ZP base, ZP len
    .word (1 << LIB_MOUSE)       // +10: libMask -- the 'l' path reads through it
    .byte mousetestNameEnd - mousetestName  // +12: name len
mousetestName:
    .text "mousetest"
mousetestNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - mousetestBinStart), 0

.import source "mousetest.inc"
.label mousetestEnd = *
