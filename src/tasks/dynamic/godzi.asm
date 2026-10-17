// ==========================================
//  godzi.asm -- godzi task (relocatable + args, single-instance)
// ==========================================
//  Wraps godzi.inc -- the port of the stand-alone demo in reference/godzi.asm
//  and reference/main.asm: a boat-shaped walker (two overlapping sprites -- black
//  rigging over a light-blue hull) crosses the screen while a black "Godzilla"
//  stands -- or hovers -- elsewhere and flaps between two poses. Four sprite
//  slots, the reference's own layout. Nominal origin $5000 (page-aligned; delta_lo=0 for any pool).
//  WARNING: Single-instance, deliberately: KF_RE_RUNNABLE is NOT set, so a second
//  `run godzi` is refused with `? already running`. The demo holds the SID for
//  its whole session -- the library's claim *and* the chip's registers -- and
//  drives all eight sprite slots, so two copies could not both be right.
//  (The reference's ground band was screen RAM, which an all-bitmap display does
//  not have, so the walker floats on the bitmap instead of standing on it.)
//  See godzi.inc.
//
//  Usage:  run godzi        -> mode 1 (godzi.asm's look) -- plays `commodo`
//          run godzi 2      -> mode 2 (main.asm's look)  -- plays `fsonata`
//  Keys:   '1' / '2' switch mode live (and switch the song with it), q exits --
//  and `q` takes the music with it: the child is reaped and the SID is hushed,
//  so nothing is left playing and no `sidrst` is needed. The shell can kill the
//  task like any other; `kill` silences the chip itself when the victim (or its
//  spawner) declared sid.lib, so that path leaves no drone either.
//
//  libMask = $0022 -- filesys.lib (bit 1) to load the two tunes and the sprite
//  sheets at startup, sid.lib (bit 5) for the player. filesys is handed back
//  with `kLibHold(5)` once both tunes are in RAM (see godzi.inc's MUSIC note).

.encoding "screencode_mixed"

// (syscall alias defined globally in tools/assemble-tasks.asm)
// (frameCount/dataPtr live in src/tasks/kernel-data.inc -- global)

// Per-task ZP state ($10-$15), preserved across context switches by the
// scheduler's ZP DMA. Declared here so the header (zp_base/zp_len) and the
// body (godzi.inc) share one definition. Six bytes: the walker's x needs three
// (the arena crosses 256), the rest are one each.
//
// WARNING: Deliberately NOT $02-$0A: that range is sid.lib's player scratch, declared
// by the thread child that calls kSidPlay (GZ_ZP_TUNE_* below). Each task's
// window is virtualized through its own REU shadow, so an overlap would in fact
// work -- but disjoint windows are one fewer thing to reason about, and keeping
// them apart costs nothing.
.const GZ_ZP_BASE = $10
.const GZ_FRAME = GZ_ZP_BASE + 0   // frameCount as of the last update
.const GZ_ANIM  = GZ_ZP_BASE + 1   // 40-frame counter: the pose cycle (mode 1)
.const GZ_XF    = GZ_ZP_BASE + 2   // walker x, fraction
.const GZ_XL    = GZ_ZP_BASE + 3   // walker x, integer low
.const GZ_XH    = GZ_ZP_BASE + 4   // walker x, 9th bit (0 or 1)
.const GZ_MODE  = GZ_ZP_BASE + 5   // 1 = godzi.asm's look, 2 = main.asm's
.const GZ_ZP_LEN = 6             // (no slot base: every slot is ours while we
                                 //  are the displayed task)
//  The ZP window the PLAYER CHILD gets -- sid.lib's own range, exactly what
//  sidplay.tsk declares. It is the parent that sets it, at spawn time, via
//  kArg+6/kArg+7 (syscall.md section 12): the scheduler's save and restore are gated on
//  the SWITCHING task's own region and its own REU shadow, so a window declared
//  by the parent cannot cover the child -- the parent's restore would DMA its
//  shadow over the child's live ZP. Not this task's window (GZ_ZP_* above),
//  which is deliberately separate; godzi.inc's MUSIC note explains.
.const GZ_ZP_TUNE_BASE = $02
.const GZ_ZP_TUNE_LEN  = 9

godziBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE | KF_TAKES_ARGS | KF_NOT_EVICTABLE
                                 // +2:  flags -- pinned: a pool-full load evicts
                                 //       something else, or fails with "? no
                                 //       space in pool"; swapping godzi out would
                                 //       cold-restart the demo mid-show (and its
                                 //       music thread with it).
                                 //       NOT KF_RE_RUNNABLE: a task that owns a
                                 //       global resource is single-instance -- the
                                 //       SID here, the whole sprite subsystem for
                                 //       any sprite task
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word godziEnd - godzi       // +5:  code size
    .byte 8, GZ_ZP_BASE, GZ_ZP_LEN  // +7: data size (8 = args buffer), ZP base, ZP len
    .word (1 << LIB_FILESYS) | (1 << LIB_SID) | (1 << LIB_SPRITE)  // +10: libMask
    .byte godziNameEnd - godziName  // +12: name len
godziName:
    .text "godzi"
godziNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - godziBinStart), 0

.import source "godzi.inc"
.label godziEnd = *
