// ==========================================
//  sprdemo.asm -- bouncing balls (relocatable + re-runnable)
// ==========================================
//  Wraps sprdemo.inc -- eight coloured balls bouncing off the walls and off each
//  other, forever. Nominal origin $5000 (page-aligned; delta_lo=0 for any pool).
//  libMask = $0080 -- sprite.lib only: the sprite calls are the library's, and
//  the demo's physics is integer (8.8 fixed point) rather than fp.lib.
//
//  RE-RUNNABLE, with nothing to guard: a task owns all eight slots while its
//  surface is displayed, and the kernel hands one task's whole sprite state over
//  when the display changes hands, so any number of instances may run at once --
//  only the displayed one shows. See docs/lib-sprite.md.
//
//  Keys: q exits. The shell can kill it like any task.
//
//  Kernel-symbol aliases below are RESOLVED addresses from the kernel build --
//  NOT stable jump-table entries. The shadow ABI and the slot addresses come
//  from src/tasks/sprite-shadow.inc (imported globally by assemble-tasks.asm),
//  and they are pinned by asserts in src/main.asm.

.encoding "screencode_mixed"

// (syscall alias defined globally in tools/assemble-tasks.asm)
// (sprShadow/kbFocus/taskState live in src/tasks/kernel-data.inc -- global)

//  Per-task ZP state, preserved across context switches by the scheduler's ZP
//  DMA. Seven bytes: the demo is balls-only, and it drives slots 0-7 (there is
//  nothing to allocate -- the kernel keeps one task's sprite state live, the
//  displayed task's, and hands the whole register set over with it).
.const SPRD_ZP_BASE = $02
.const SPRD_SLOT    = SPRD_ZP_BASE + 0   // sheet/frame loop index
.const SPRD_BI      = SPRD_ZP_BASE + 1   // ball index i -- also the move loop
.const SPRD_BJ      = SPRD_ZP_BASE + 2   // ball index j -- the pair inner loop
.const SPRD_T0      = SPRD_ZP_BASE + 3   // dx, in the pair test
.const SPRD_T1      = SPRD_ZP_BASE + 4   // |dx|, kept while |dy| is computed
.const SPRD_T2      = SPRD_ZP_BASE + 5   // noise-seed sample / pair-test |dy| limit
.const SPRD_LAST    = SPRD_ZP_BASE + 6   // frameCount as of the last update
.const SPRD_ZP_LEN  = 7

sprdemoBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE | KF_RE_RUNNABLE  // +2:  flags -- any number of instances
                                 //       may run; only the displayed one shows
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word sprdemoEnd - sprdemo   // +5:  code size
    .byte 0, SPRD_ZP_BASE, SPRD_ZP_LEN  // +7: data size (no args), ZP base, ZP len
    .word (1 << LIB_SPRITE)      // +10: libMask -- the five sprite calls
    .byte sprdemoNameEnd - sprdemoName  // +12: name len
sprdemoName:
    .text "sprdemo"
sprdemoNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - sprdemoBinStart), 0

.import source "sprdemo.inc"
.label sprdemoEnd = *
