// ==========================================
//  fpdemo.asm -- fpdemo task (relocatable + re-runnable)
// ==========================================
//  fp.lib showcase: a sine wave drawn edge to edge with a cosine
//  wave superimposed, pixel by pixel via kPlot. Both waves' peaks
//  and troughs touch the top/bottom of the screen (y = c - c*trig,
//  c = 99.5). theta = 2*pi*x/320, advanced by pi/160 per pixel.
//  Yields every pixel; idles once drawn so the bitmap stays on screen.
//  Re-runnable: each run loads a fresh copy with its own bitmap and
//  per-task ZP state (no "already running").
//
//  ZP: task state $02-$09 plus the fp.lib window $90-$C5 -- the
//  declared region must contain the window (fpZpDeclared assert),
//  and the scheduler DMAs it all on every context switch, so FP
//  state survives preemption.
//
//  libMask = bit 2 (gfx, eager -- kPlot lives in gfx.lib)
//          | bit 3 (fp, eager -- kFpCall dispatches into fp.lib).
//  Nominal origin $5000 (page-aligned; delta_lo=0 for any pool).
//
//  Kernel-symbol aliases below are RESOLVED addresses from the
//  kernel build (kernel.inc / kernel-vars.inc) -- NOT stable
//  jump-table entries. Keep in sync with the kernel.

.encoding "screencode_mixed"

// (syscall alias defined globally in tools/assemble-tasks.asm)
// (taskGfxReu*/screenKind live in src/tasks/kernel-data.inc -- imported globally)
// (fp.inc constants FP_* imported globally; kFpCall from jumptable.inc)

// Per-task ZP state ($02-$09) + fp.lib window ($90-$C5) in one
// declared region, preserved across context switches by the
// scheduler's ZP DMA.
.const FP_DEMO_ZP    = $02
.const FP_DEMO_ZPLEN = $C4          // covers $02..$C5 (incl. fp window)
.const FPD_XLO   = FP_DEMO_ZP + 0   // x low byte
.const FPD_XHI   = FP_DEMO_ZP + 1   // x high byte
.const FPD_YTMP  = FP_DEMO_ZP + 2   // y result of the last wave eval
.const FPD_THETA = FP_DEMO_ZP + 3   // angle theta -- 4-byte float ($05-$08)
.const FPD_TRIG  = FP_DEMO_ZP + 7   // trig opcode (FP_SIN / FP_COS)
:fpZpDeclared(FP_DEMO_ZP, FP_DEMO_ZPLEN)

fpdemoBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE | KF_RE_RUNNABLE  // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word fpdemoEnd - fpdemoStart // +5:  code size
    .byte 0                      // +7:  data size (no args buffer)
    .byte FP_DEMO_ZP             // +8:  ZP base
    .byte FP_DEMO_ZPLEN          // +9:  ZP len
    .word (1 << LIB_GFX) | (1 << LIB_FP)  // +10: libMask
    .byte fpdemoNameEnd - fpdemoName // +12: name len
fpdemoName:
    .text "fpdemo"
fpdemoNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - fpdemoBinStart), 0

.import source "fpdemo.inc"
.label fpdemoEnd = *
