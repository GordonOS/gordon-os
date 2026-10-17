// ==========================================
//  gortris.asm -- "gortris" task (relocatable, pinned) -- port of
//  wiebow/tetris.c64
// ==========================================
//  Wraps gortris.inc. Nominal origin $5000 (page-aligned).
//
//  A port, not a rewrite: the piece set and its rotations are the reference's
//  own 4x4 frame tables (reference/tetris/blocks.asm), rotation is its
//  frame-range walk (SelectBlock/AnimateBlock), the move-then-test-then-put-back
//  order is its BlockLeft/Right/Rotate, and the line sweep follows
//  lines.asm's CheckLines -> FlashLines -> RemoveLines. What changes is the
//  substrate: the reference reads and writes the C64 CHARACTER SCREEN, and this
//  port keeps a 10x20 board in its own image and blits 8-byte glyphs, so
//  collision is a board lookup instead of a screen read (and the AI thread can
//  read the board). See reference/tetris/UPSTREAM.md for what is deliberately
//  NOT ported, and gortris.inc for the design.
//
//  Flags $41 = KF_RELOCATABLE | KF_NOT_EVICTABLE:
//    * relocatable, like every pool task;
//    * PINNED, because it owns the display and the keyboard focus for its whole
//      life and holds the board state -- being swapped out mid-game would need
//      the whole game to restart cold (the restore path re-runs the entry).
//  Not re-runnable (no KF_RE_RUNNABLE): the display, the sprite-less screen and
//  the SID are single resources, so a second `run gortris` is refused with
//  "? already running" rather than allowed to fight over them.
//
//  libMask bit 5 = sid.lib, called DIRECTLY: kSidPlay (from the music thread the
//  task spawns -- the call blocks, so the game itself cannot make it) and kSidHush
//  (from the game, to stop that thread's player when the display is lost). The
//  task does NOT spawn sidplay.tsk; it loads the tune itself. Unlike sidplay it
//  does NOT hand filesys.lib back once the tune is in RAM: its hi-score table has
//  to be WRITTEN, and kLibHold is one-way -- a released library comes back owned by
//  nobody -- so both declared libraries are held for the session (see the HI SCORES
//  note in gortris.inc).
//
//  WARNING: The task's own ZP is untouched: all of its state is in the code image. The
//  music THREAD declares ZP $02-$0A (sid.lib's scratch) at spawn time, and the
//  game never touches that window (see gortris.inc).

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg are defined in tools/assemble-tasks.asm)

gortrisBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE | KF_NOT_EVICTABLE  // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word gortrisEnd - gortris   // +5:  code size
    .byte 0, 0, 0                // +7:  data size 0, ZP base, ZP len -- no ZP
    .word (1 << LIB_FILESYS) | (1 << LIB_SID)  // +10: libMask -- filesys (the
                                 //      tune load) + sid (the player + hush)
    .byte gortrisNameEnd - gortrisName  // +12: name len
gortrisName:
    .text "gortris"
gortrisNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - gortrisBinStart), 0

.import source "gortris.inc"
.label gortrisEnd = *
