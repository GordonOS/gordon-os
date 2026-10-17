// ==========================================
//  grknoid.asm -- gorkanoid (relocatable + not evictable)
// ==========================================
//  Wraps grknoid.inc -- an Arkanoid/Breakout game: a paddle sprite bounces a
//  ball sprite into a wall of bricks drawn as glyph cells on the task's own
//  bitmap surface. The wall, the physics and the music are described at the top
//  of grknoid.inc.
//
//  Nominal origin $5000 (page-aligned; delta_lo=0 for any pool).
//
//  libMask declares sprite.lib (bit 7, eager), filesys.lib (bit 1) and sid.lib
//  (bit 5, lazy): the paddle and the ball are sprites, the hi-score table is
//  WRITTEN back to the REU FS, and the three tunes are played through sid.lib.
//
//  Controls: 'j' and 'k' move the paddle, SPACE fires, 'p' pauses, 'q' quits;
//  the joystick in port 2 drives the same bits. The paddle follows the key being
//  HELD (kernel data `keybHeld`, a level) once per displayed frame rather than
//  kReadKey's repeat events -- see grknoid.inc for why.
//
//  Kernel-symbol aliases referenced below are RESOLVED addresses from the kernel
//  build -- NOT stable jump-table entries. `kbFocus`/`keybHeld`/`frameCount` live
//  in src/tasks/kernel-data.inc (imported globally by assemble-tasks.asm) and are
//  pinned by asserts in src/main.asm.

.encoding "screencode_mixed"

// (syscall alias defined globally in tools/assemble-tasks.asm)
// (the sprite shadow ABI + macros come from src/tasks/sprite-shadow.inc,
//  imported globally by assemble-tasks.asm)

//  Per-task ZP state, preserved across context switches by the scheduler's ZP
//  DMA. $10 keeps clear of the music child's own window ($02-$0A, which sid.lib
//  requires): a parent's window cannot protect a child's, and each task's region
//  is DMA'd from its own REU shadow on its own switch-in.
//
//  The ball is FIXED POINT: x is three bytes
//  (XH:XL:XF -- integer high, integer low, 1/256 fraction) because the field's x
//  runs 32..328 and does not fit in one byte, y is two (YI:YF) because it never
//  leaves 65..249 (the top wall down to the death line), and a velocity is signed
//  8.8 with its sign carried through
//  every byte above the fraction.
.const GK_ZP_BASE = $10
.const GK_ME      = GK_ZP_BASE + 0    // our own task slot, for the focus compare
.const GK_FRAME   = GK_ZP_BASE + 1    // frameCount as of the last pass
.const GK_INPUT   = GK_ZP_BASE + 2    // the bits held down this frame
.const GK_EDGE    = GK_ZP_BASE + 3    // the bits newly down this frame
.const GK_LAST    = GK_ZP_BASE + 4    // GK_INPUT as of the previous frame
.const GK_JOYRAW  = GK_ZP_BASE + 5    // the port-2 read, in pressed sense
.const GK_JTMP    = GK_ZP_BASE + 6    // stick-bit accumulator
.const GK_PADK    = GK_ZP_BASE + 7    // paddle x, as an offset from GK_PAD_MIN
.const GK_BXF     = GK_ZP_BASE + 8    // ball x fraction
.const GK_BXL     = GK_ZP_BASE + 9    // ball x integer low
.const GK_BXH     = GK_ZP_BASE + 10   // ball x integer high (0/1)
.const GK_BYI     = GK_ZP_BASE + 11   // ball y integer
.const GK_BYF     = GK_ZP_BASE + 12   // ball y fraction
.const GK_VXF     = GK_ZP_BASE + 13   // ball vx, 8.8
.const GK_VXL     = GK_ZP_BASE + 14
.const GK_VXH     = GK_ZP_BASE + 15   // the SUM's sign: $00 or $ff, never $01
.const GK_VYI     = GK_ZP_BASE + 16   // ball vy, 8.8
.const GK_VYF     = GK_ZP_BASE + 17
.const GK_BSTATE  = GK_ZP_BASE + 18   // 0 = on the paddle, 1 = in flight
.const GK_PLO     = GK_ZP_BASE + 19   // scratch: the paddle's x, as a 9-bit pair
.const GK_PHI     = GK_ZP_BASE + 20
.const GK_OFFL    = GK_ZP_BASE + 21   // scratch: the hit's deflection, 16-bit signed
.const GK_OFFH    = GK_ZP_BASE + 22   //   -- the offset, x8, plus the english
.const GK_PADV    = GK_ZP_BASE + 23   // -1/0/+1: the way the paddle actually moved
.const GK_PADKOLD = GK_ZP_BASE + 24   // its offset at the start of this frame
.const GK_ZP_LEN  = 25

grknoidBinStart:
    .word HDR_MAGIC                          // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE | KF_NOT_EVICTABLE  // +2:  flags -- pinned: a live game
                                             //      must not be swapped out, the
                                             //      restore restarts it cold
    .word $5000                              // +3:  origin (page-aligned, nominal)
    .word grknoidEnd - grknoid               // +5:  code size
    .byte 0, GK_ZP_BASE, GK_ZP_LEN           // +7:  data size (no args), ZP base/len
    .word (1 << LIB_SPRITE) | (1 << LIB_FILESYS) | (1 << LIB_SID)  // +10: libMask --
                                             //      sprite.lib for the art, filesys.lib
                                             //      because the score table is WRITTEN
                                             //      (the hold is kept all session), and
                                             //      sid.lib for the three CGSC tunes
    .byte grknoidNameEnd - grknoidName       // +12: name len
grknoidName:
    .text "grknoid"
grknoidNameEnd:
    .word 0                                  // fixup_count (16-bit -- written by the build)
    .word 0                                  // fixup_offset (written by the build)
    .fill 256 - (* - grknoidBinStart), 0

.import source "grknoid.inc"

//  The brick hue's parity is derived from two bits of the board index: bit 0 is the
//  column's parity and bit 3 is the row's. Bit 3 is the row's ONLY while a row is
//  eight bricks wide, so widening the wall means changing that derivation too.
.assert "grknoid: the tile parity needs 8 brick columns", (GK_BRICK_COLS == 8) ? 1 : 0, 1
