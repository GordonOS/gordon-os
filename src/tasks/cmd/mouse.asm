// ==========================================
//  mouse.asm -- "mouse" command task (relocatable)
// ==========================================
//  Declares what pointer this machine has: "mouse 1351" / "mouse off", and with
//  no argument it prints the current setting. It writes the kernel's mouseKind
//  byte -- the machine-wide value a consuming task reads at start-up, in the
//  mould of sysSpeedShift (speed.com) and blinkRate (blink.com).
//  It shares the shell's screen for that one line (the dir/time/ps prologue),
//  reads its args from cmdArgsBuf, and exits.
//  No libraries, on purpose: this is POLICY, not the device. mouse.lib is the
//  reader and never consults mouseKind -- which is what lets `mtest` diagnose a
//  machine the OS believes has no mouse at all (docs/lib-mouse.md).

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg + kernel-data aliases are defined
//  in tools/assemble-tasks.asm)

mouseCmdBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word mouseCmdEnd - mouse    // +5:  code size
    .byte 0, 0, 0                // +7:  data size, ZP base, ZP len
    .word 0                      // +10: libMask = 0 (no libraries)
    .byte mouseCmdNameEnd - mouseCmdName  // +12: name len
mouseCmdName:
    .text "mouse"
mouseCmdNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - mouseCmdBinStart), 0

.import source "mouse.inc"
.label mouseCmdEnd = *
