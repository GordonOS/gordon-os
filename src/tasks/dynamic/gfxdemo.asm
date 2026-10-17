// ==========================================
//  gfxdemo.asm -- gfxdemo task (relocatable + re-runnable)
// ==========================================
//  Wraps gfxdemo.inc -- the "spider web" line weave. Four symmetric
//  fans of kLine strokes (top/bottom + left/right). Bounces once
//  (y: 0->top->0) then idles (yield) so the bitmap stays on screen.
//  Takes one arg -- the y interval (step): "run gfxdemo 3" = tight,
//  "run gfxdemo 20" = loose. x interval = step*8/5 (320/200 coupling).
//  Re-runnable: each run loads a fresh copy with its own bitmap and
//  per-task ZP state (no "already running").
//  Nominal origin $5000 (page-aligned; delta_lo=0 for any pool).
//
//  Kernel-symbol aliases below are RESOLVED addresses from the
//  kernel build (kernel.inc / kernel-vars.inc) -- NOT stable
//  jump-table entries. Keep in sync with the kernel.
//  TODO: expose these via the jump table instead of hardcoding.

.encoding "screencode_mixed"

// (syscall alias defined globally in tools/assemble-tasks.asm)
// (taskGfxReu*/screenKind live in src/tasks/kernel-data.inc -- imported globally)

// Per-task ZP state ($02-$06), preserved across context switches by the
// scheduler's ZP DMA. Declared here so the header (zp_base/zp_len) and
// the body (gfxdemo.inc) share one definition.
.const GFX_ZP_BASE = $02
.const GFX_XLO = GFX_ZP_BASE + 0   // x (top endpoint) low byte
.const GFX_XHI = GFX_ZP_BASE + 1   // x high byte
.const GFX_Y   = GFX_ZP_BASE + 2   // y (side endpoint) 0..199
.const GFX_DIR = GFX_ZP_BASE + 3   // 1 = increasing, 0 = decreasing
.const GFX_YM  = GFX_ZP_BASE + 4   // 199 - y (bottom mirror of y)
.const GFX_STEP  = GFX_ZP_BASE + 5 // y interval (arg, 1..25, default 5)
.const GFX_XSTEP = GFX_ZP_BASE + 6 // x interval = step*8/5
.const GFX_ZP_LEN = 7

gfxdemoBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE | KF_TAKES_ARGS | KF_RE_RUNNABLE  // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word gfxdemoEnd - gfxdemoStart // +5:  code size
    .byte 8, GFX_ZP_BASE, GFX_ZP_LEN  // +7: data size (8 = args buf), ZP base, ZP len
    .word (1 << LIB_GFX)         // +10: libMask
    .byte gfxdemoNameEnd - gfxdemoName // +12: name len
gfxdemoName:
    .text "gfxdemo"
gfxdemoNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - gfxdemoBinStart), 0

.import source "gfxdemo.inc"
.label gfxdemoEnd = *
