// ==========================================
//  spirog.asm -- spirog task (relocatable + re-runnable, takes args)
// ==========================================
//  Draws a hypotrochoid (spirograph) curve as one polyline through the
//  bitmap, tracing it on screen, then idles with the picture up.
//  'q' exits from either state.
//
//      run spirog                    default shape (80 35 45, colour 1)
//      run spirog 1..9               a preset shape
//      run spirog 10                 three colours picked at random for
//                                    the whole screen, then 25 random
//                                    presets -- each scaled down, placed in
//                                    a random part of the screen, and drawn
//                                    in one of those three
//      run spirog <R> <r> <d> [col]  ring R, rolling circle r, pen offset
//                                    d (1..127 each), colour 1..15
//
//  x = (R-r)*cos(t) + d*cos(phi)      y = (R-r)*sin(t) - d*sin(phi)
//  phi = t*(R-r)/r.  t and phi are 16-bit angles (65536 = one turn) and
//  both steps are exact integers, so the curve CLOSES: after
//  N = 65536>>m segments t has done Q turns and phi P turns, where
//  P/Q = (R-r)/r in lowest terms and N is sized so the faster of the two
//  waves gets at least 40 samples per cycle (512..4096 segments).
//  Trig is a 256-entry sine table indexed by the angle's high byte, so
//  the drawing loop is pure integer -- four 8x8 multiplies per point.
//
//  R and r set the shape (R must exceed r), d the pen throw.  When
//  (R-r) + d passes 95 both are halved (a pure scale -- the shape is
//  unchanged) so the curve always fits the 320x200 bitmap.
//
//  ZP: task state $02-$27, declared via header zp_base/zp_len so the
//  scheduler DMAs it on every context switch.
//
//  libMask = bit 2 (gfx, eager -- kLine / kClearBitmap / kFillMatrix /
//  kGfxString live in gfx.lib).
//  Nominal origin $5000 (page-aligned; delta_lo=0 for any pool).

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg defined in tools/assemble-tasks.asm)
// (taskGfxReu*/screenKind live in src/tasks/kernel-data.inc -- imported globally)

// Per-task ZP state, preserved across context switches by the scheduler's
// ZP DMA. Declared here so the header (zp_base/zp_len) and the body
// (spirog.inc) share one definition.
.const SG_ZP     = $02
.const SG_TH     = SG_ZP + 0    // $02-$03 theta (16-bit angle, 65536 = 1 turn)
.const SG_PH     = SG_ZP + 2    // $04-$05 phi
.const SG_DTH    = SG_ZP + 4    // $06-$07 theta step (exact)
.const SG_DPH    = SG_ZP + 6    // $08-$09 phi step (exact)
.const SG_N      = SG_ZP + 8    // $0A-$0B segments left
.const SG_A      = SG_ZP + 10   // $0C (R-r), scaled so A+B <= 95
.const SG_B      = SG_ZP + 11   // $0D d, scaled with A
.const SG_PX     = SG_ZP + 12   // $0E previous segment x (screen)
.const SG_PY     = SG_ZP + 13   // $0F previous segment y
.const SG_SX     = SG_ZP + 14   // $10 current x (screen)
.const SG_SY     = SG_ZP + 15   // $11 current y
.const SG_CNT    = SG_ZP + 16   // $12 yield/keys countdown
.const SG_M      = SG_ZP + 17   // $13 m (step shift)
.const SG_R      = SG_ZP + 18   // $14 ring radius R (arg)
.const SG_RR     = SG_ZP + 19   // $15 rolling radius r (arg)
.const SG_D      = SG_ZP + 20   // $16 pen offset d (arg)
.const SG_COL    = SG_ZP + 21   // $17 colour (arg, 1..15)
.const SG_BIG    = SG_ZP + 22   // $18 max(P,Q) -- the faster wave's period
.const SG_ST     = SG_ZP + 23   // $19 sin(theta)
.const SG_CT     = SG_ZP + 24   // $1A cos(theta)
.const SG_SP     = SG_ZP + 25   // $1B sin(phi)
.const SG_CP     = SG_ZP + 26   // $1C cos(phi)
.const SG_MV     = SG_ZP + 27   // $1D sgMul: signed table value
.const SG_MR     = SG_ZP + 28   // $1E-$1F sgMul: radius, shifted left
.const SG_PR     = SG_ZP + 30   // $20-$21 sgMul: product
.const SG_S1     = SG_ZP + 32   // $22-$23 signed term sum
.const SG_T0     = SG_ZP + 34   // $24 scratch
.const SG_T1     = SG_ZP + 35   // $25 scratch
.const SG_T2     = SG_ZP + 36   // $26 scratch
.const SG_T3     = SG_ZP + 37   // $27 scratch
.const SG_CX     = SG_ZP + 38   // $28-$29 curve centre x (160 for a single shape;
                                //        a montage shape can pass 255)
.const SG_CY     = SG_ZP + 40   // $2A curve centre y (100 for a single shape)
.const SG_SXH    = SG_ZP + 41   // $2B high byte of the current x
.const SG_PXH    = SG_ZP + 42   // $2C high byte of the previous x
.const SG_LEFT   = SG_ZP + 43   // $2D montage shapes still to draw
.const SG_EXTRA  = SG_ZP + 44   // $2E montage: extra halvings of this shape
.const SG_INK    = SG_ZP + 45   // $2F the value kLine draws with
.const SG_C1     = SG_ZP + 46   // $30 montage colour 1 (matrix high nibble)
.const SG_C2     = SG_ZP + 47   // $31 montage colour 2 (matrix low nibble)
.const SG_C3     = SG_ZP + 48   // $32 montage colour 3 (colour RAM)
.const SG_MODE   = SG_ZP + 49   // $33 bitmap mode in use (0 hires, 1 MC)
.const SG_SPIN   = SG_ZP + 50   // $34 montage bag cursor's random phase
.const SG_ZP_LEN = 51           // $02..$34

spirogBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE | KF_TAKES_ARGS | KF_RE_RUNNABLE  // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word spirogEnd - spirogStart // +5: code size
    .byte 72, SG_ZP, SG_ZP_LEN   // +7: data size (args buf), ZP base, ZP len
    .word (1 << LIB_GFX)         // +10: libMask
    .byte spirogNameEnd - spirogName // +12: name len
spirogName:
    .text "spirog"
spirogNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - spirogBinStart), 0

.import source "spirog.inc"
.label spirogEnd = *
