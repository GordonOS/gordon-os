// ==========================================
//  GordonOS -- grknoid: gorkanoid
// ==========================================
//  An Arkanoid/Breakout game: a paddle sprite bounces a ball sprite into a wall
//  of bricks drawn as cells on this task's own bitmap surface.
//
//  Controls: 'j' and 'k' move the paddle, SPACE fires, 'p' pauses, 'q' quits,
//  and the joystick in port 2 drives the same bits.
//
//  WARNING: THE PADDLE READS THE HELD KEY, NOT KEY EVENTS. `keybHeld` (kernel data)
//  is a LEVEL -- the code of the key that is down right now, or $FF. kReadKey
//  delivers the EVENT side of the same scan, on the kernel's repeat cadence
//  (KEYB_INITIAL = 30 ticks, then KEYB_REPEAT = 6), so a paddle driven from it
//  steps about ten times a second and its speed follows the emulator clock.
//  A held KEYB_REPEAT is a global knob (keyrpt.com pokes it) and must not be
//  retuned by a game, and the matrix must not be scanned from a task either --
//  the IRQ's scan drives $DC00 and a task driving it would make every task see
//  phantom keys.
//
//  WARNING: A HELD DIRECTION IS A LEVEL, THE BUTTONS ARE EDGES. Movement is applied
//  every displayed frame for as long as a direction is down, which is what makes
//  it glide; a button's ACTION comes from the newly-down bits (`~last & now`), so
//  one press is one action and a held key cannot retrigger.
//
//  WARNING: ONE STEP PER DISPLAYED FRAME. The scheduler tick is 59.2 Hz and the
//  display samples at 50.1 Hz, so a per-tick step is sampled 1.18 times per
//  frame: some frames show two steps, the next none, which reads as a stutter no
//  speed constant can remove. `frameCount` is the display's own clock (the
//  kernel's raster commit increments it at line 0), so compare it for CHANGE and
//  run the step only when it differs. It is 8-bit and it wraps.
//
//  WARNING: THE SURFACE IS ALREADY BLANK. allocScreen's fresh path fills the slot's
//  pixels with 0, its matrix with 0 and colour RAM with the task's text colour,
//  so nothing here clears anything. The ink for a cell comes from the matrix
//  nibble the print path writes -- a pixel is black until a call puts a nibble in
//  its cell.
//
//  WARNING: a CURSOR-CARRYING print may not reach the last cell of the last row
//  (39,24). It wraps immediately, and a wrap past the region's bottom row scrolls
//  the whole surface WHILE the character is being written, so the top row is lost
//  with no way to reposition afterwards. Every cell here is therefore drawn with
//  kPrintGlyphAt (through gkGlyphAt): a fixed-cell write advances nothing, so the
//  field's own bottom row is safe.
//
//  Every iteration yields (Rule 13), so the shell stays usable while it runs.
// ==========================================

//  Keys, as screen codes -- both `keybHeld` and `kReadKey` answer in screen
//  codes, and comparisons against `.text` strings match because .text is
//  screencode_mixed too. The letters are $01-$1A ('a' = 1), so 'j' is $0A; the
//  characters in the $20-$3F group agree with PETSCII.
.const GK_K_LEFT  = $0a                     // 'j'
.const GK_K_RIGHT = $0b                     // 'k'
.const GK_K_FIRE  = $20                     // SPACE
.const GK_K_PAUSE = $10                     // 'p'
.const GK_K_QUIT  = $11                     // 'q'

//  How many bindings the control map below holds.
.const GK_KEYMAP_N = 5

//  The input byte. A direction is read as a LEVEL (it is down now); a button's
//  action is taken from its newly-down bit.
.const GK_B_LEFT  = %00000001
.const GK_B_RIGHT = %00000010
.const GK_B_FIRE  = %00000100
.const GK_B_PAUSE = %00001000
.const GK_B_QUIT  = %00010000

//  Surface colours: a blue frame on black.
.const GK_BORDER  = 6
.const GK_C_BG    = 0
.const GK_C_TITLE = 7                       // yellow
.const GK_C_TEXT  = 1                       // white
.const GK_C_PAD   = 14                      // light blue, the paddle
.const GK_C_BALL  = 1                       // white, the ball
//  The three steps the palette has between white and dark grey. The ball's colour
//  pulse walks them, and the last one is the hue the wall wears.
.const GK_C_GREY_LIGHT  = 15
.const GK_C_GREY_MEDIUM = 12
.const GK_C_GREY_DARK   = 11

//  The paddle: sprite 0, one sheet, x-expanded so the 24 px art is 48 px on
//  screen. The ball is sprite 1, one frame, an 8x8 disc in the sheet's top-left
//  corner, so the sprite's position IS the ball's box -- which is the box the
//  physics tests, so no offset arithmetic exists anywhere.
//  The five control bytes are each a bitmask over all eight slots; the byte that
//  is live belongs to the displayed task alone, so this task's set is its own
//  bits and needs no merging.
.const GK_SPR_PADDLE = 0
.const GK_SPR_BALL   = 1
.const GK_PAD_XEXP   = %00000001
.const GK_SPRITES_ON = %00000011            // paddle + ball

//  Paddle travel. GK_PADK is the paddle's x MINUS GK_PAD_MIN, so the whole run
//  fits in ONE byte: GK_PAD_MIN + 255 = 287 is the right stop, which makes the
//  byte's own range the field -- no second byte, and no clamp value needed on the
//  right. kSpritePos wants x's ninth bit in the carry, and that is exactly the
//  carry out of `clc / adc #GK_PAD_MIN`, so placing it costs no branch either.
.const GK_PAD_MIN   = 32                    // cell column 1's left edge
.const GK_PAD_MAXK  = 255                   // x = 287: the bar ends one px short of the wall
.const GK_PAD_START = 128                   // x = 160: the middle of the run, where a serve puts it
.const GK_PAD_Y     = 242                   // the bar's first raster line is 243 -- inside row 24,
                                            //   the screen's LAST row, so no dead space is left
                                            //   under the paddle
//  ---- THE PADDLE'S ACCELERATION. The step starts at GK_PAD_SPEED_MIN on a new press and
//  gains a gear every GK_PAD_ACCEL displayed frames the SAME direction is held, up to
//  GK_PAD_SPEED_MAX. Releasing it, or reversing, drops it straight back, so a fresh press
//  is always the slow precise step and the speed is something the player builds -- which is
//  what stops a fast paddle being twitchy, and it is still there when the ball is.
//  WARNING: THE MAXIMUM IS THE PLAYER'S TIME BUDGET, not a taste setting. One ball descent
//  is 168 px, which is 224 displayed frames at level 1 but only 112 at level 5 -- so the
//  paddle covers 112 of its 255 units on the last level. The ball's own lateral travel is
//  ~84 px a descent at EVERY level (the launch angle is constant), but a wall or a brick
//  REVERSES it, doubling the demand to ~168, and a 1 px step cannot follow that. Held, a
//  3 px step crosses the field in 85 frames, comfortably inside one descent.
.const GK_PAD_SPEED_MIN = 1
.const GK_PAD_SPEED_MAX = 3
//  WARNING: the accel tick is a POWER OF TWO, because gkPadAccel tests the hold counter's
//  low bits rather than comparing it -- 16 frames (0.32 s) a gear, so the second arrives
//  0.32 s into a hold and the third 0.64 s in. WARNING: that rate is the point, not a
//  leftover: a 0.16 s gear reaches the top inside a single wall-to-wall reposition, where
//  the paddle reads as twitchy again -- which is the exact thing the acceleration exists to
//  prevent. The next step up is 32 (0.64 s a gear); the assert below keeps the mask honest.
.const GK_PAD_ACCEL      = 16
.const GK_PAD_ACCEL_MASK = GK_PAD_ACCEL - 1
.assert "grknoid: the accel tick is a power of two", ((GK_PAD_ACCEL & GK_PAD_ACCEL_MASK) == 0) ? 1 : 0, 1
.const GK_PAD_W     = 48                    // 24 px of art, doubled by x-expansion
//  WARNING: a whole-pixel step is bounded by the COLLISION TEST, not by taste: the paddle's
//  x is tested for overlap in one step, so a step wider than the paddle itself could put the
//  bar on the far side of the ball between two frames and miss it. Anything slower than a
//  whole pixel needs a fractional accumulator, not a smaller constant.
.assert "grknoid: the fastest paddle step cannot skip the ball", (GK_PAD_SPEED_MAX < GK_PAD_W) ? 1 : 0, 1
.const GK_PAD_BAR_H = 6                     // the rows of the sheet the bar is drawn in

//  The ball's box is GK_BALL_W pixels on a side and the walls are cell columns 0
//  and 39, so the interior runs x 32..336. Vertically the top wall is cell row 1,
//  and the ball's TOP EDGE is what is bounded.
//  WARNING: sprite y is one LESS than the raster line it displays from, and cell row R
//  covers rasters 51 + 8*R .. 58 + 8*R -- so row 1's wall ends at raster 66 (the ball's
//  top edge sits ON it at sprite y 65, and 66 would clear it) and the ball is lost on
//  row 24's last line, raster 250. Both bounds are in sprite y, which is why they are
//  65 and 249 and not 66 and 250.
.const GK_FIELD_L    = 32                   // the left wall's inner edge
.const GK_FIELD_R    = 336                  // the right wall's inner edge
.const GK_BALL_W     = 8                    // the box, in pixels, on both axes:
                                            //   the smallest size that reads round
.const GK_BALL_MINX  = GK_FIELD_L
.const GK_BALL_MAXX  = GK_FIELD_R - GK_BALL_W
.const GK_BALL_MINY  = 65                   // ON row 1's last line (raster 66)
.const GK_BALL_DEADY = 249                  // row 24's last line (raster 250), one under
                                            //   the paddle's bar
.const GK_BALL_CENTRE = GK_BALL_W / 2       // the box's centre pixel
.const GK_MAXX_ML    = GK_BALL_MAXX & $ff   // the right stop as a pair of bytes,
.const GK_MAXX_MH    = GK_BALL_MAXX >> 8    //   plus one, for the compare
.const GK_MAXX_OV    = (GK_BALL_MAXX & $ff) + 1

//  ---- The death line, drawn by gkWalls. The ball is lost when its box's TOP edge
//  reaches GK_BALL_DEADY, so that line is the field's floor and the player should be able
//  to see it: gkGlyphDead draws ONE pixel line, on a cell's last, and a row of them on
//  GK_DEAD_ROW puts it exactly on sprite y 50 + 8*24 + 7 = 249 -- the eighth line of the
//  cell row the paddle floats in, which is the screen's LAST row.
//  The line walks columns 1..38, so it runs between the walls without overwriting them;
//  gkWalls draws the two bottom corners at (0,24) and (39,24) with the same wall glyph,
//  through kPrintGlyphAt -- a fixed-cell write with no cursor to advance and no wrap to
//  fire.
.const GK_DEAD_ROW = 24

//  ---- The ball's colour pulse: the palette's greys from white down to dark grey and back
//  up, one shade every GK_BALL_RATE DISPLAYED frames (see gkBallColour). Presentation
//  only -- the box, the physics and every test are colour-blind.
.const GK_BALL_RAMP_N = 6                   // shades in the ramp, both ends included
.const GK_BALL_RATE   = 8                   // displayed frames a shade is held

//  Velocities are 8.8 and SIGNED, with the sign carried through every byte above
//  the fraction -- so -0.5 is $FF:FF:$80, not $00:00:$80.
//  THE LAUNCH IS THE GAME'S PACE, AND IT IS PER LEVEL: every other bounce mirrors a
//  velocity and only the paddle ADDS to one, so the launch and the paddle's bend are the
//  whole of it. gkLaunch takes the level's vx from gkVxTab and DERIVES vy from it, so the
//  speed ramps and the ANGLE does not (see the ramp block below the level constants).
//  Level 1's vx is exactly the GK_VXMIN floor, which is what lets a dead-centre paddle hit
//  -- it cancels the bend -- be an exact mirror rather than a floored one. Level 1's vx is a
//  full 8.8 WORD, because the ramp passes 1.0 px/frame; the FLOOR stays a fraction, because
//  its test only runs at all when the integer byte is zero.
.const GK_VX0      = $00C0                  // 0.75 px/frame
.const GK_VXMIN    = $C0                    // 0.75 px/frame: the floor on |vx| after
.const GK_VXMINNEG = 256 - GK_VXMIN         //   a paddle hit, so it never climbs
                                            //   straight up and stalls

//  The cap. 3.0 px/frame: the ceiling on |vx|, because the paddle ADDS its angle
//  to the speed the ball arrived with, so a player chasing the ball would ratchet
//  that speed up on every bounce. It is a whole doubling above the ramp's top (1.5),
//  which is the headroom the cap had when that top was 0.9375. The value is the
//  MIDDLE byte, the integer part: 3 IS 3.0, and -3 is $FD.
//  WARNING: the mirror decides which way the sum went from THIS byte's sign, so the cap has
//  to stay well under 128 -- that assert is the guard on it, and the brick-cell assert on the
//  ramp is the tighter, physical one.
.const GK_VXCAP    = 3
.const GK_VXCAPN   = 256 - GK_VXCAP
.assert "grknoid: the vx cap leaves the integer byte's sign usable", (GK_VXCAP < 128) ? 1 : 0, 1

//  The english. A paddle moving INTO the ball adds to the angle and one moving
//  away subtracts, which is the only thing the player's own motion can say about
//  a bounce: chase a ball leftwards and the ball leaves harder to the left,
//  catch the same ball while moving right and the bounce is cut back. It is a FIXED
//  0.5 px/frame, never a share of the paddle's step -- adding the whole step (up to
//  3 px a frame) would swamp the +-0.84 the angle itself spans and the ball would
//  leave almost sideways.
//  NOTE: it does NOT scale with the paddle's step. What a hit is told is the paddle's
//  DIRECTION (GK_PADV, -1/0/+1), never how far it moved, so a faster paddle does not
//  hand the ball a harder kick -- it only gets there sooner.
.const GK_VXSPIN   = $80                    // 0.5 px/frame, against a 1..3 px paddle
.const GK_VXSPIN_L = GK_VXSPIN & $ff
.const GK_VXSPIN_H = GK_VXSPIN >> 8

//  The paddle hit's two bounds. The boxes overlap when d = bx - padx is in
//  -(W-1) .. PAD_W-1, and the angle comes from d less the distance between the
//  paddle's centre and the ball's centre when it is centred on it.
.const GK_HIT_NEG  = 256 - (GK_BALL_W - 1)  // the most negative d that still touches
.const GK_HIT_BIAS = GK_PAD_W / 2 - GK_BALL_CENTRE

//  The cell grid. A cell is one glyph, and the window's top-left cell covers
//  sprite (24, 50) -- so a cell's origin is (24 + col*8, 50 + row*8), and a
//  sprite's y is the raster line it displays from minus one.
.const GK_COLS     = 40
.const GK_ROWS     = 25
.const GK_WIN_X    = 24                     // sprite x of cell column 0
.const GK_WIN_Y    = 50                     // sprite y of cell row 0
.const GK_WALL_TOP   = 1                    // the wall rows/columns
.const GK_WALL_LEFT  = 0
.const GK_WALL_RIGHT = 39
.const GK_WALL_LAST  = 24                   // the LAST row the side walls draw, and it is the
                                            //   surface's own bottom row: the corners at (0,24)
                                            //   and (39,24) are wall cells like any other, drawn
                                            //   by kPrintGlyphAt, which advances and wraps
                                            //   nothing. The loop tests GK_WALL_LAST+1 because it
                                            //   writes a row BEFORE testing it -- testing against
                                            //   LAST itself stops a row early and leaves row 24's
                                            //   sides blank. The death line (columns 1..38)
                                            //   then closes the row between them.
//  The attract screens' box, which is a DIFFERENT shape from the play field's frame: it
//  closes all four sides, and it covers the WHOLE surface -- row 0 across the top and row
//  24 across the bottom, the surface's own last row. The play field is open at the top
//  (row 0 is the HUD there) and at the bottom (the ball dies there).
//  The bottom edge is the surface's last row and is drawn like every other edge: kPrintGlyphAt
//  writes a fixed cell, so no print here can advance into a wrap.
.const GK_FRAME_TOP    = 0
.const GK_FRAME_BOTTOM = 24

//  The wall of bricks: 8 bricks a row, 4 cells each (32 px), so a row is exactly
//  half the window and the margins are symmetric. Rows 3..8. EIGHT bricks a row
//  and FOUR cells wide are what make a row exactly four nibble-packed bytes --
//  no other pair of numbers keeps the level table aligned to the source lines.
.const GK_BRICK_X0    = 4                   // the cell column the wall starts at
.const GK_BRICK_Y0    = 3                   // the cell row it starts at
.const GK_BRICK_COLS  = 8
.const GK_BRICK_ROWS  = 6
.const GK_BRICK_CELLS = 4
.const GK_BRICKS      = GK_BRICK_COLS * GK_BRICK_ROWS
.const GK_BRICK_ENDX  = GK_BRICK_X0 + GK_BRICK_COLS * GK_BRICK_CELLS
.const GK_BRICK_ENDY  = GK_BRICK_Y0 + GK_BRICK_ROWS
.const GK_ROW_BYTES   = GK_BRICK_COLS / 2   // two bricks a byte
.const GK_LEVEL_BYTES = GK_BRICK_ROWS * GK_ROW_BYTES
.const GK_LEVEL_COUNT = 5

//  ---- THE LAUNCH RAMP. The launch is the game's pace (see the velocity block), and it is
//  a per-level one: level 1 is GK_VX0_F and each level adds GK_VXSTEP, so level 5 launches
//  at exactly twice level 1's speed -- about 19% a level.
//  vy is DERIVED from vx as -2 * vx (gkLaunch), which holds the launch ANGLE constant. That
//  is the whole design: ramping vy on its own would steepen the ball, and a steeper ball
//  needs LESS lateral travel per descent, so the ramp would partly cancel itself out.
//  WARNING: THE RAMP'S CEILING IS GEOMETRY, AND THE RAMP IS A WORD BECAUSE OF IT. gkVxTab
//  holds vx as a full 8.8 value: a fraction-only byte cannot express 1.0 px/frame at all,
//  so the ceiling is a representation limit, not physics. What bounds the top is the
//  ball against the smallest thing it may not skip: a brick cell is 8 px, and the fastest
//  launch falls GK_VYMAX = 2 x 1.5 = 3.0 px/frame, so it cannot pass one. The paddle band is
//  13 rows (104 px) and the paddle's face is 48 px wide, both far above that. The asserts
//  below fail the BUILD rather than waiting for a play test.
.const GK_VXSTEP = $0030                    // +0.1875 px/frame a level
.const GK_VXLAST = GK_VX0 + GK_VXSTEP * (GK_LEVEL_COUNT - 1)
.const GK_VYMAX  = $0300                    // 3.0 px/frame: the fastest climb allowed
.assert "grknoid: level 5 launches at twice level 1's speed", (GK_VXLAST == 2 * GK_VX0) ? 1 : 0, 1
.assert "grknoid: the fastest launch is exactly GK_VYMAX", (2 * GK_VXLAST == GK_VYMAX) ? 1 : 0, 1
.assert "grknoid: level 1's launch is the GK_VXMIN floor", (GK_VX0 == GK_VXMIN) ? 1 : 0, 1
.assert "grknoid: the fastest fall cannot skip a brick cell (8 px)", (GK_VYMAX < $0800) ? 1 : 0, 1

//  The HUD, on row 0 above the wall.
.const GK_HUD_ROW     = 0
.const GK_SCORE_LBL   = 0
.const GK_SCORE_COL   = 6                   // five digits
.const GK_SCORE_DIGITS = 5
.const GK_BRICKS_LBL  = 14
.const GK_BRICKS_COL  = 21                  // two digits
.const GK_LIVES_LBL   = 26                  // "lives", then the count beside it
.const GK_LIVES_COL   = 32
//  The level, in the last cells of the row: it is the field the launch speed ramps on, so a
//  player who cannot see it cannot tell a faster level from a lucky bounce.
.const GK_LEVEL_LBL   = 34                  // "lev" (34-36), one BLANK cell, then the digit --
.const GK_LEVEL_COL   = GK_LEVEL_LBL + 4    //   the two must not touch: "lev3" reads as one word.
                                            //   Nothing writes cell 37; the row is cleared with
                                            //   the surface (kClearScreen before every gkHud)
.assert "grknoid: the level digit stays on the HUD row", (GK_LEVEL_COL < GK_COLS) ? 1 : 0, 1

//  Colours. A brick's colour comes from its HIT POINTS *and* its column's parity, so
//  damage stays legible -- the hue family says how many hits are left -- while the pair
//  keeps the wall from being one flat slab of colour. The wall is a shade darker than
//  anything that plays.
.const GK_C_WALL  = 11                      // dark grey
.const GK_C_HP1A  = 7                       // yellow / cyan: one hit left, and cracked
.const GK_C_HP1B  = 3
.const GK_C_HP2A  = 13                      // light green / green: two hits
.const GK_C_HP2B  = 5
.const GK_C_HP3A  = 2                       // red / orange: three hits
.const GK_C_HP3B  = 8
.const GK_PTS_CRACK = 5                     // and a brick broken outright pays 10 x the level

//  ---- The mode machine. One byte, one dispatch per displayed frame. The attract
//  loop is the game's front door and the ONLY screen that carries the controls:
//  the play surface holds the wall, the HUD and the sprites and nothing else.
.const GK_MODE_ATTRACT = 0
.const GK_MODE_PLAY    = 1
.const GK_MODE_OVER    = 2
.const GK_ATTRACT_SCREENS = 4               // title, controls, credits, high scores

//  How long each attract screen is held, in DISPLAYED frames.
//  WARNING: it does not fit a byte, and KickAssembler v5.25 SILENTLY TRUNCATES an
//  out-of-range immediate -- `lda #1016` assembles as `lda #$f8`, a quarter of the
//  intended time, with no warning at all -- so the countdown is SIXTEEN bits
//  (gkTickLo first) and the constant is loaded with #</#>. The value is a comfort
//  choice: the screens are read, not glanced at, and this one constant is the knob.
.const GK_ATTRACT_DELAY = 1016

//  How long the GAME-OVER screen waits for an answer before dropping back to the attract
//  loop, in DISPLAYED frames -- so a machine nobody is at cycles instead of sitting on a
//  prompt forever. It is the same rough 20 s an attract screen gets, a separate constant
//  because the two are tuned for different reasons.
//  WARNING: this is the FALLBACK clock, not the game-over screen's clock. While the
//  game-over tune is playing the screen waits for it instead and leaves when it ends
//  (gkOverFrame); this countdown applies only to a screen with no tune to wait for, which
//  is also the case it is tuned for -- a screen nobody is at has nobody to hear music
//  either, so it is the reading time the score gets.
//  The countdown only runs once the keyboard has gone IDLE (gkKeysArmed) and only while
//  this task's surface is displayed, so the screen cannot be taken from a player who is
//  still playing -- a hand still on the keys stops the clock. WARNING: sixteen bits, for
//  the reason above.
//  WARNING: the NAME ENTRY does not use it and must not be given it -- that screen is a
//  data-entry step, so it waits for the player. See gkHiNameScreen.
.const GK_RETURN_DELAY = 1016

//  The run: five lives, one lost a lost ball, and losing the last one is game over.
.const GK_LIVES_START = 5

//  ---- THE MUSIC. Three tunes, one per mode, played by ONE thread child: the tunes are
//  the Compute's Gazette Sid Collection's, which is the collection written for the very
//  Sidplayer this repository's sid.lib ports, so a `.mus` file is played as it was
//  published rather than converted.
//  The attract loop and the game each have a tune that REPEATS; the game-over screen has
//  one that plays ONCE and then hands the attract tune back, so `repeats` is a per-tune
//  property (gkMusRepeat) and not a constant.
//  WARNING: switching tunes is `kSidHush` PLUS a changed request. The child sits inside
//  kSidPlay's sleep loop, so clearing a flag would leave it playing: nothing but the hush
//  makes the player return, and only then does the child re-read the request.
.const GK_MUS_ATTRACT = 0                   // katmand: the attract screens, and the start
.const GK_MUS_PLAY    = 1                   // immig:   a game in progress
.const GK_MUS_OVER    = 2                   // firstdt: the game-over screen, played ONCE
.const GK_MUS_N       = 3
.const GK_MUS_NONE    = $ff                 // no child spawned
//  ---- THE ONE TUNE BUFFER. Pages of the task's single kMalloc, sized for the LARGEST of
//  the three tunes and held for the session: the tunes are katmand 4584 B (18 pages),
//  immig 932 B (4) and firstdt 382 B (2), and cycling those three sizes through the pool
//  fragments it until the largest request fails (see gkMusInit). WARNING: a tune LONGER than
//  this is refused by gkMusLoad rather than loaded past the end of the block -- so a new
//  .mus bigger than the largest one here needs this raised, and otherwise goes quietly
//  silent instead of corrupting the pool.
.const GK_MUS_PAGES   = 18
.const GK_MUS_DELAY   = 1                   // scheduler ticks slept per jiffy AT 100%:
                                            //   one jiffy of music per jiffy plays the
                                            //   file at its own tempo. sid.lib scales
                                            //   it by the machine's speed, so the tune
                                            //   is the same REAL time on any machine
                                            //   (see docs/lib-sid.md)
.const GK_MUS_REAP    = 20                  // yields to give the child before force-killing
                                            //   it: the player has to leave its loop and
                                            //   run SYS_EXIT, which takes a few switches
//  How many passes round the scheduler the child waits before asking for the SID again after
//  another task refused it. The wait costs nothing -- the tune is already in RAM, so a retry
//  is one kSidPlay call with no FS read and no kMalloc -- and this pause is what keeps a
//  refused child from hammering the chip. ~1 s of scheduler passes.
.const GK_MUS_BUSY    = 64
//  The child declares its OWN ZP window: sid.lib's scratch is remapped into base 2, length
//  9, and only a task header -- or a thread spawn's own
//  registration -- can declare one. It cannot be this task's: the parent's window is
//  $10..$28, the scheduler saves and restores on the SWITCHING task's own window, and a
//  parent's restore would DMA its shadow straight over a child's live ZP.
.const GK_ZP_MUS_BASE = $02
.const GK_ZP_MUS_LEN  = 9

//  Where the game-over screen's three lines sit: "game over", the run's score, and the
//  prompt that settles the hi-score table. That screen is a SHELL screen -- cleared, with
//  the closed box around it -- so these are its own rows, not the play field's.
.const GK_OVER_TEXT_ROW  = 6
.const GK_OVER_SCORE_ROW = 10
.const GK_OVER_SCORE_LBL = 12               // "score", five characters
.const GK_OVER_SCORE_COL = 18               // its value, one space past a 5-cell label
.const GK_OVER_HINT_ROW  = 14

//  Where the pause banner sits. It is printed over the frozen field: the paddle
//  and the ball take no step while it is up, so nothing can be lost behind it.
.const GK_PAUSED_ROW = 18

//  ---- The hi-score table: three BCD score bytes (most significant byte first) and then
//  seven name cells -- TEN bytes an entry, which is what GK_HI_ENTRYLEN says -- so an
//  entry prints as six digits and a seven-cell name. A score has to BEAT a row to take it
//  over, and an exact tie leaves the incumbent row alone.
//  The game's OWN score is binary and stays binary (see the warning on gkDrawScore);
//  gkScoreToBCD turns it into these three bytes once, when it is about to take a row.
.const GK_HI_ENTRIES  = 3
.const GK_HI_NAMELEN  = 7
.const GK_HI_SCORELEN = 3
.const GK_HI_ENTRYLEN = GK_HI_SCORELEN + GK_HI_NAMELEN
.const GK_HI_LEN      = GK_HI_ENTRIES * GK_HI_ENTRYLEN
.const GK_HI_DOT      = $2e                 // '.' -- the placeholder a name is typed over
//  Where the table sits on the high-score screen, which is also the attract loop's
//  last screen and the screen a new name is typed on. Six digits and a space, so
//  the name field starts seven cells in.
.const GK_HI_HDR_ROW  = 5
.const GK_HI_ROW0     = 9
.const GK_HI_ROWSTEP  = 2
.const GK_HI_MSG_ROW  = 17
.const GK_HI_COL      = 13
.const GK_HI_NAME_COL = GK_HI_COL + 7

//  gkPut(col, row, str) -- print a string at a cell. A macro so each line of the
//  control list is one line of source.
.macro gkPut(col, row, str) {
    lda #<str
    sta kPtr
    lda #>str
    sta kPtr+1
    ldx #col
    ldy #row
    jsr gkPrintStr
}

//  ---- THIS MUST BE THE FIRST EMITTED BYTE OF THE BODY. The header's code-size
//  field is `grknoidEnd - grknoid`, and the loader copies exactly that many bytes
//  from file offset 256 -- so data placed above this label is not copied, is not
//  covered by the size, and shifts every fixup offset past the declared end. The
//  constants and the macro above are fine: they emit nothing.
grknoid:
    lda #0                      // mode 0 = hires-text surface
    ldx #GK_BORDER
    stx kArg                    // border
    ldx #GK_C_BG
    stx kArg+1                  // bg0 -- the field plays on this
    stx kArg+2
    stx kArg+3
    jsr kAllocScreen
    bcc gkHaveScreen
    jmp gkNoScreen              // Rule 4: gkNoScreen is well out of branch range
gkHaveScreen:
    jsr kDisableCursor          // a game surface: no blinking text cursor
    //  Our own slot, for the per-frame focus compare: asking once is what keeps
    //  a SYS_GETTID out of the loop.
    lda #SYS_GETTID
    jsr syscall
    sta GK_ME
    tax
    //  ---- TAKE THE DISPLAY WITHOUT BAKING IN THE OUTGOING BLINK BLOCK.
    //  kSwitchView copies the whole framebuffer into the outgoing task's REU
    //  slot, so a live cursor block drawn at that instant becomes permanent
    //  content there: it returns as a phantom cursor at the cell the previous
    //  owner's cursor was on, and only a pixel write removes it. kBlinkReset
    //  un-draws the block at the DISPLAYED surface's saved position, which at
    //  this moment is the outgoing owner's. The SEI is not decoration -- an IRQ
    //  tick between the erase and the switch draws it straight back -- and no
    //  syscall may sit in the window, because the dispatcher re-enables IRQs.
    php
    sei
    txa
    pha
    jsr kBlinkReset             // clobbers A, X, Y, kPtr, kArg+7
    pla
    tax
    jsr kSwitchView             // display it + take kbFocus
    plp
    jsr gkSprites
    jsr gkHiLoad                // the saved hi-score table, if there is one: it has
                                //   to be in hand before the attract loop, which
                                //   SHOWS it
    jsr gkMusInit               // and the three tunes, plus the child that plays them
    jsr gkStartAttract          // the game opens on the attract loop: the CONTROLS
                                //   and the high scores are read before anything
                                //   moves, and the play surface never carries them
                                //   surface never carries them

// ==========================================
//  gkLoop -- one input read and one step per DISPLAYED frame
// ==========================================
gkLoop:
    lda GK_ME
    cmp kbFocus
    beq gkFocused
    //  Not displayed: act on nothing, but DO keep the input byte in step. The
    //  kernel scans the keyboard and the stick whatever the focus is, so a key
    //  that went down while the game was in the background would otherwise read
    //  as a fresh edge the moment the display came back -- typing in the shell
    //  could fire a shot.
    lda frameCount
    cmp GK_FRAME
    beq gkIdle
    sta GK_FRAME
    jsr gkInput
    jmp gkIdle
gkFocused:
    lda frameCount
    cmp GK_FRAME
    beq gkIdle                  // same frame as last time: nothing to do
    sta GK_FRAME
    jsr gkInput
    lda GK_EDGE
    and #GK_B_QUIT
    bne gkExit
    jsr gkModeFrame             // the mode owns the frame's step
gkIdle:
    lda #SYS_YIELD
    jsr syscall                 // never starve the scheduler
    jmp gkLoop

// ==========================================
//  gkModeFrame -- one step of whatever mode is up
// ==========================================
//  WARNING: THE DISPATCH IS A `jsr` AND EVERY ARM IS A `jmp`, SO EACH ARM ENDS IN
//  EXACTLY ONE `rts` THAT RETURNS TO THE FRAME LOOP. Jumping straight into an arm
//  from the loop instead would leave its `rts` popping the cold-start stack (the
//  task entry never pushed anything), and the game would run into a random address.
//  The arms are jmps through inverted branches (Rule 4): the attract screens and the
//  hi-score routines are well past a branch's reach.
gkModeFrame:
    ldy gkMode
    cpy #GK_MODE_PLAY
    bne gkMfNotPlay
    jmp gkPlayFrame
gkMfNotPlay:
    cpy #GK_MODE_ATTRACT
    bne gkMfOver
    jmp gkAtFrame
gkMfOver:
    jmp gkOverFrame

// ==========================================
//  gkPlayFrame -- one frame of the game itself
// ==========================================
//  A paused game takes no step at all, so the field is frozen behind the banner and
//  nothing can be lost while it is up. 'q' is handled above the dispatch, so a
//  paused game can always be left.
gkPlayFrame:
    lda GK_EDGE
    and #GK_B_PAUSE
    beq gkPfNotPause
    jsr gkPauseToggle
gkPfNotPause:
    lda gkPaused
    bne gkPfOut
    jsr gkPaddle
    jmp gkBall                  // after the paddle: the ball uses its new position
gkPfOut:
    rts

//  gkPauseToggle -- 'p'. Taking the banner down writes the same number of blank
//  cells it wrote, because nothing else owns that row.
gkPauseToggle:
    lda gkPaused
    eor #1
    sta gkPaused
    bne gkPtShow
    lda #GK_C_TEXT
    jsr kSetTextColor
    gkPut(16, GK_PAUSED_ROW, gkSBlank6)
    rts
gkPtShow:
    lda #GK_C_TITLE
    jsr kSetTextColor
    gkPut(16, GK_PAUSED_ROW, gkSPaused)
    rts

// ==========================================
//  gkExit -- hand the display back, then leave
// ==========================================
//  Hand the display back BEFORE freeing the surface, or the VIC is left pointing
//  at a freed surface.
//  The parent is the shell: give it the display if it is still alive, otherwise
//  leave the hand-off to sysExit.
gkExit:
    //  Music first: stop the player, reap the child and hand the tune buffer back while our
    //  own pages are still ours to run on (the reap yields, so it cannot happen after
    //  the surface goes or the pool has been told we are leaving).
    jsr gkMusicBye
    lda #SYS_GETTID
    jsr syscall
    tax
    lda taskParent,x
    cmp #$ff
    beq gkExitFree
    tax
    lda taskState,x
    beq gkExitFree
    jsr kSwitchView             // X = the shell (kSwitchView preserves X)
gkExitFree:
    jsr kFreeScreen
    lda #SYS_EXIT
    jsr syscall

gkNoScreen:
    //  No free surface: yield forever rather than exit, so the task is visible
    //  in `ps` instead of vanishing without a word.
    lda #SYS_YIELD
    jsr syscall
    jmp gkNoScreen

// ==========================================
//  gkSprites -- resolve the paddle's art, once
// ==========================================
//  kSpriteSheet costs ~1,000-3,000 cycles and runs SEI'd, so it happens here and
//  never in the frame loop. The art never changes, so the frame loads once too: a
//  slot is written wherever the beam is, and re-loading a frame whose sprite is on
//  screen can split it for that one line.
gkSprites:
    lda #<gkSPadName
    sta kPtr
    lda #>gkSPadName
    sta kPtr+1
    lda #GK_SPR_PADDLE
    jsr kSpriteSheet
    lda #<gkSBallName
    sta kPtr
    lda #>gkSBallName
    sta kPtr+1
    lda #GK_SPR_BALL
    jsr kSpriteSheet
    lda #GK_SPR_PADDLE
    ldx #0
    jsr kSpriteFrame            // one frame each; all later motion is position
    lda #GK_SPR_BALL
    ldx #0
    jsr kSpriteFrame
    sprColour(GK_SPR_PADDLE, GK_C_PAD)
    sprColour(GK_SPR_BALL, GK_C_BALL)
    sprXExpand(GK_PAD_XEXP)
    sprOn(GK_SPRITES_ON)
    rts

// ==========================================
//  gkInput -- this displayed frame's input byte
// ==========================================
//  Both sources feed the SAME bits, so one consumer serves the keyboard and the
//  stick. Clobbers A, X, Y and the ZP scratch.
gkInput:
    lda #0
    sta GK_INPUT
    jsr gkKeyBits
    ora GK_INPUT
    sta GK_INPUT
    jsr gkJoyBits
    ora GK_INPUT
    sta GK_INPUT
    //  The newly-down bits: what is down now and was not last frame. This one
    //  rule is what makes a held button a single action.
    lda GK_LAST
    eor #$ff
    and GK_INPUT
    sta GK_EDGE
    lda GK_INPUT
    sta GK_LAST
    rts

//  gkKeyBits -- the held key as an input bit, or 0 if it is not one of ours.
//  The two tables ARE the control map: a new binding is one row in each.
gkKeyBits:
    lda keybHeld
    ldx #GK_KEYMAP_N - 1
gkKeyScan:
    cmp gkKeyMap,x
    beq gkKeyHit
    dex
    bpl gkKeyScan
    lda #0
    rts
gkKeyHit:
    lda gkKeyBit,x
    rts

//  gkJoyBits -- the stick in port 2 as the same bits.
//  The stick's five lines are PA0-PA4 of CIA #1, and the kernel's keyboard scan
//  WRITES $DC00 to drive the matrix columns -- so a plain `lda $dc00` reads back
//  the column the scan last left driven, not the stick. The read is therefore:
//  save DDRA, make the port an input, read it, put DDRA back -- all inside SEI,
//  because a scan that ran in that window would drive nothing and see a phantom
//  "no keys". The C64's pull-ups make a released line read 1, so the port is
//  ACTIVE LOW and its five bits are inverted once before any test.
//  The bits are tested INDIVIDUALLY: a paddle needs left and right distinguishable
//  in the SAME frame, so this is not a priority chain.
gkJoyBits:
    php
    sei
    lda $dc02
    pha                         // DDRA: the kernel's scan needs it as output
    lda #0
    sta $dc02
    lda $dc00                   // now this is the stick
    sta GK_JOYRAW
    pla
    sta $dc02                   // DDRA back before anything can scan again
    plp
    //  WARNING: THE FIVE BITS ARE INVERTED HERE, ONCE. A released line reads 1
    //  through the C64's pull-ups, so the port is active low; after this every
    //  test below reads "bit set = pressed". Taking the raw byte as if 1 meant
    //  pressed latches ALL FIVE bits on an empty port, which sets BOTH
    //  directions -- and both directions down cancels, so the paddle never moves
    //  while the keyboard-only bits (the quit edge) keep working, which reads as
    //  "the directions alone are broken".
    lda GK_JOYRAW
    and #%00011111              // PA0-PA4 are the stick; PA5-PA7 are not
    eor #%00011111
    sta GK_JOYRAW               // now 1 = pressed
    lda #0
    sta GK_JTMP
    lda GK_JOYRAW
    and #%00000100              // left (PA2)
    beq gkJNotLeft
    lda #GK_B_LEFT
    sta GK_JTMP
gkJNotLeft:
    lda GK_JOYRAW
    and #%00001000              // right (PA3)
    beq gkJNotRight
    lda GK_JTMP
    ora #GK_B_RIGHT
    sta GK_JTMP
gkJNotRight:
    lda GK_JOYRAW
    and #%00010000              // fire (PA4)
    beq gkJNotFire
    lda GK_JTMP
    ora #GK_B_FIRE
    sta GK_JTMP
gkJNotFire:
    lda GK_JTMP
    rts

// ==========================================
//  gkPadAccel -- the paddle's step for THIS motion frame
// ==========================================
//  The step starts at GK_PAD_SPEED_MIN on a new press and gains a gear every GK_PAD_ACCEL
//  MOTION frames the SAME direction is held, up to GK_PAD_SPEED_MAX; releasing it, or
//  reversing, drops it straight back. So the paddle is precise when the player is placing
//  it and fast once they are chasing, and speed is something they build rather than
//  something the game hands them.
//  WARNING: MOTION frames, not displayed ones: a machine that declares itself faster shows
//  more frames per real second, so counting displayed frames would hand out the gears and
//  the release/reset in a fraction of the real time they take at 100%.
//  WARNING: the direction the counter belongs to is kept, not just the direction held: a
//  release must reset the step (the first idle frame does it, because the requested bits
//  differ from the remembered ones), or a fresh press would inherit the old gear.
//  WARNING: a gear is ticked by the hold counter's LOW BITS and not by comparing it, which
//  is why GK_PAD_ACCEL is a power of two -- the counter itself is free to run on and wrap.
//  Holding into a wall still counts, but a reversal resets, so it buys nothing.
//  Clobbers A.
gkPadAccel:
    lda GK_INPUT
    and #GK_B_LEFT | GK_B_RIGHT
    cmp gkPadDir
    beq gkPaSame                // the same direction: keep counting
    sta gkPadDir                // a new press or a reversal: the step starts over
    lda #GK_PAD_SPEED_MIN
    sta gkPadStep
    lda #0
    sta gkPadHold
    rts
gkPaSame:
    lda gkPadDir
    beq gkPaOut                 // nothing held: nothing to build
    inc gkPadHold
    lda gkPadHold
    and #GK_PAD_ACCEL_MASK
    bne gkPaOut                 // only every GK_PAD_ACCELth frame of the hold
    lda gkPadStep
    cmp #GK_PAD_SPEED_MAX
    bcs gkPaOut
    inc gkPadStep
gkPaOut:
    rts

// ==========================================
//  Pacing -- the game's own frames, scaled to the machine
//
//  The ball and the paddle move in whole steps of their own size, so a machine that
//  declares itself faster SKIPS some of its displayed frames instead of moving a
//  smaller distance in each. That keeps the step, the 8.8 ball velocity and the
//  collision granularity exactly as the game has them, and it keeps all three of
//  the paddle's gears. The game's own frames are the basis: one step per displayed
//  frame, which is what a real C64 runs and what the C64U image declares (`speed`,
//  see docs/quick-start.md).
//  WARNING: WHAT CHANGES WITH THE DECLARED SPEED IS ONLY HOW MANY FRAMES IT TAKES,
//  NEVER WHAT THE GAME DOES. The ball and the paddle move on exactly the SAME frames
//  -- those where `frameCount & (2^shift - 1)` is zero -- so their order within a
//  frame is the 100% order and a bounce resolves as it does at 100%. Everything else
//  the player can see is real time too: the accelerator counts the MOTION frames a
//  direction is held (gkPadAccel), and a screen's hold and the game-over wait are
//  shifted to real time as they are set (gkTickSet).
//  C=1 means this displayed frame is not a motion frame.
gkStill:
    ldx sysSpeedShift           // +1 = 200%, 0 = 100%, -1 = 50%
    beq gksNo                   // 100% or slower: every frame moves
    bmi gksNo
    lda #1
gksMask:
    asl                         // 2^shift: the frames between two motion frames
    dex
    bne gksMask
    sec
    sbc #1                      // the phase bits that must be zero
    and frameCount
    beq gksNo
    sec                         // a skipped frame
    rts
gksNo:
    clc
    rts

//  gkPaddle -- one step per frame while a direction is held
// ==========================================
//  A LEVEL, not an event. Both directions down cancels: neither compare matches,
//  and the flow falls through to placing the paddle where it already is.
gkPaddle:
    jsr gkStill                 // the paddle and the ball move on the SAME frames: one
    bcc gkPgGo                  //   rule, so their geometry is the 100% geometry
    rts                         // not a motion frame: the paddle holds position
gkPgGo:
    jsr gkPadAccel              // this MOTION frame's step, from how long the key is held
    //  Remember where it started. The english a hit gets is the direction the
    //  paddle ACTUALLY travelled, and a paddle held against a wall does not move
    //  however hard the key is held -- so the comparison is against this, not
    //  against what the key asked for.
    lda GK_PADK
    sta GK_PADKOLD
    lda GK_INPUT
    and #GK_B_LEFT | GK_B_RIGHT
    cmp #GK_B_LEFT
    beq gkPadLeft
    cmp #GK_B_RIGHT
    beq gkPadRight
gkPadPlace:
    //  ---- the english: -1, 0 or +1 for the way it moved this frame.
    lda GK_PADK
    cmp GK_PADKOLD
    bne gkPadMoved
    lda #0
    sta GK_PADV
    jmp gkPadShow
gkPadMoved:
    bcc gkPadMovedLeft          // cmp sets C from `new >= old`
    lda #1
    sta GK_PADV
    jmp gkPadShow
gkPadMovedLeft:
    lda #$ff
    sta GK_PADV
gkPadShow:
    //  A = the x low byte and C = x's ninth bit, both from the same add -- which
    //  is exactly the pair kSpritePos wants. ldx/ldy and jsr leave the flags
    //  alone, so the carry survives into the call.
    lda GK_PADK
    ldx #GK_SPR_PADDLE
    ldy #GK_PAD_Y
    clc
    adc #GK_PAD_MIN
    jsr kSpritePos
    rts
gkPadLeft:
    lda GK_PADK
    sec
    sbc gkPadStep
    bcs gkPadStore              // no borrow: the step landed inside the field
    lda #0                      // nearer the wall than one step: clamp
gkPadStore:
    sta GK_PADK
    jmp gkPadPlace
gkPadRight:
    lda GK_PADK
    clc
    adc gkPadStep
    bcc gkPadStore              // no overflow: still inside the field
    lda #GK_PAD_MAXK            // past the right stop: clamp
    jmp gkPadStore

// ==========================================
//  gkBall -- the ball's step, once per displayed frame
// ==========================================
//  FIXED POINT: x is
//  (XH:XL:XF) because the field's x runs 32..330 and does not fit one byte, y is
//  (YI:YF) because it never leaves 65..233, and a velocity is signed 8.8 with the
//  sign carried through the bytes above the fraction. Integrating is an `adc`
//  chain and a bounce is a two's-complement negation, so nothing multiplies,
//  divides or takes a square root and fp.lib is not needed.
//
//  WARNING: THE BALL'S BOX IS THE SHEET'S TOP-LEFT 8x8, so its sprite position IS
//  the box. Every bound below is that box, not the ball's centre.
gkBall:
    lda GK_BSTATE
    bne gkBallFly
    //  ---- ON THE PADDLE. It is positioned from the paddle every frame, so it
    //  rides along with it, and SPACE launches it.
    jsr gkBallStick
    lda GK_EDGE
    and #GK_B_FIRE
    beq gkBallShow
    jsr gkLaunch                // this level's launch: the ball's pace for the whole life
    lda #1
    sta GK_BSTATE
    jmp gkBallShow              // move it on the frame it was launched
gkBallFly:
    jsr gkStill
    bcc gkbGo
    rts                         // not a ball-motion frame: the ball holds
gkbGo:
    jsr gkBallMove
    jsr gkBallWalls
    //  ---- past the paddle's row: the ball is LOST, which costs a life. The wall,
    //  the score and the level survive it; the last life is game over.
    lda GK_BYI
    cmp #GK_BALL_DEADY
    bcc gkBallFlying
    dec gkLives
    beq gkBallDead
    lda #0                      // back on the paddle: SPACE serves again
    sta GK_BSTATE
    jsr gkBallStick
    jsr gkHud                   // the lives field has to show the loss
    jmp gkBallShow
gkBallDead:
    jsr gkHud                   // and the last one
    jmp gkGameOver
gkBallFlying:
    //  The bricks first, then the paddle -- and the state is re-read afterwards,
    //  because taking the last brick rebuilds the level and parks the ball back on
    //  the paddle, where it is not a falling ball for the paddle test to catch.
    jsr gkBallBrick
    lda GK_BSTATE
    beq gkBallShow
    //  The paddle can only catch a ball that is coming DOWN; a rising ball is
    //  already past it, and testing anyway would let it re-trigger.
    lda GK_VYI
    bmi gkBallShow
    jsr gkBallPaddle
gkBallShow:
    jsr gkBallPlace
    jmp gkBallColour            // the pulse steps with the ball, on the same clock

// ==========================================
//  gkLaunch -- this LEVEL's launch velocity into the five velocity bytes
// ==========================================
//  vx comes from gkVxTab as a full 8.8 WORD; the sign byte is $00 because the serve always
//  goes to the RIGHT. vy is 2 * vx upwards: the fraction's doubling CARRY is |vy|'s integer
//  byte and vx's integer byte doubles into it, so the ramp's top (1.5) gives a vy of 3.0.
//  It is then negated by gkBallMirrorY -- the bounce's own two's-complement negation, called
//  rather than re-written.
//  WARNING: the ceiling is the game's own invariants, not taste, and the asserts on
//  GK_VYMAX are what hold it (see the ramp block).
//  Clobbers A, X, Y.
gkLaunch:
    ldy gkLevel
    dey
    tya
    asl                         // a WORD an entry, so the index is (level - 1) * 2
    tay
    lda gkVxTab,y               // vx's fraction
    asl                         // C = the doubling's carry (2 x 1.5 = 3.0, so |vy|'s
    sta GK_VYF                  //   integer byte is 0..3)
    lda gkVxTab+1,y             // vx's integer byte, doubled with that carry
    rol
    sta GK_VYI                  // |vy|'s integer byte
    //  WARNING: VX IS LOADED AGAIN, ON PURPOSE. A held the fraction for the doubling above,
    //  and the `lda #0` that clears the vx bytes below would destroy it -- an `asl` after that
    //  `lda` doubles ZERO, which leaves the ball with no vy at all and it flies off
    //  horizontally. Each use of A here is the instruction after its own
    //  load, so there is no value in flight across another load.
    lda gkVxTab,y
    sta GK_VXF
    lda gkVxTab+1,y
    sta GK_VXL
    lda #0
    sta GK_VXH                  // the serve always goes to the RIGHT, so the sign byte is 0
    jmp gkBallMirrorY           // and up: vy = -vy, every byte

//  gkBallMove -- integrate the position by one frame.
gkBallMove:
    lda GK_BXF
    clc
    adc GK_VXF
    sta GK_BXF
    lda GK_BXL
    adc GK_VXL
    sta GK_BXL
    lda GK_BXH
    adc GK_VXH
    sta GK_BXH
    lda GK_BYF
    clc
    adc GK_VYF
    sta GK_BYF
    lda GK_BYI
    adc GK_VYI
    sta GK_BYI
    rts

//  gkBallStick -- put the ball on top of the paddle, centred.
//  The box starts at the paddle's x plus GK_HIT_BIAS, which lands the ball's
//  centre exactly on the paddle's.
gkBallStick:
    lda GK_PADK
    clc
    adc #GK_PAD_MIN + GK_HIT_BIAS
    sta GK_BXL
    lda #0
    adc #0
    sta GK_BXH
    lda #0
    sta GK_BXF
    lda #GK_PAD_Y - GK_BALL_W
    sta GK_BYI
    lda #0
    sta GK_BYF
    rts

//  gkBallPlace -- publish the ball's position.
gkBallPlace:
    ldy GK_BYI
    ldx #GK_SPR_BALL
    lda GK_BXH
    lsr                         // C = x's ninth bit
    lda GK_BXL                  // A = x low byte (lda leaves C alone)
    jsr kSpritePos
    rts

// ==========================================
//  gkBallColour -- one step of the ball's grey pulse
// ==========================================
//  The ball walks the palette's greys from white down to dark grey and back up, one
//  shade every GK_BALL_RATE MOTION frames. MOTION frames, so the pulse keeps its REAL
//  cadence at any declared speed (gkStill) -- and never scheduler ticks, which the
//  display samples 1.18 times a frame (see the frameCount warning at the top).
//  WARNING: a shade is written only when it CHANGES, so this is one kSpriteReg per
//  GK_BALL_RATE frames rather than one per frame. The value goes into the SHADOW, which
//  the kernel's raster commit publishes at raster line 0, so no write can split the
//  sprite -- there is no moment to avoid and nothing to wait for.
//  Clobbers A, X, Y (kSpriteReg does).
gkBallColour:
    inc gkBAnim
    lda gkBAnim
    cmp #GK_BALL_RATE
    bcc gkBallColOut
    lda #0
    sta gkBAnim
    inc gkBRamp
    lda gkBRamp
    cmp #GK_BALL_RAMP_N
    bcc gkBallColSet
    lda #0                      // past the last shade: back to the top of the ramp
    sta gkBRamp
gkBallColSet:
    tay
    lda gkBallRamp,y
    ldx #SPR_SHD_COLOUR + GK_SPR_BALL
    jsr kSpriteReg
gkBallColOut:
    rts

//  gkBallWalls -- reverse a component where the box meets an edge.
//  The x test runs on the ninth bit first: below 256 the left edge is the only
//  one in reach, and at or above it the right one is. Both ends set the fraction
//  to zero, so the ball lands exactly on the bound instead of drifting into it.
gkBallWalls:
    lda GK_BXH
    bne gkBallWRight
    lda GK_BXL
    cmp #GK_BALL_MINX
    bcs gkBallWTop
    lda #GK_BALL_MINX
    sta GK_BXL
    lda #0
    sta GK_BXF
    sta GK_BXH
    jsr gkBallMirrorX
    jmp gkBallWTop
gkBallWRight:
    lda GK_BXL
    cmp #GK_MAXX_OV
    bcc gkBallWTop
    lda #GK_MAXX_ML
    sta GK_BXL
    lda #GK_MAXX_MH
    sta GK_BXH
    lda #0
    sta GK_BXF
    jsr gkBallMirrorX
gkBallWTop:
    lda GK_BYI
    cmp #GK_BALL_MINY
    bcs gkBallWDone
    lda #GK_BALL_MINY
    sta GK_BYI
    lda #0
    sta GK_BYF
    jsr gkBallMirrorY
gkBallWDone:
    rts

//  gkBallMirrorX / gkBallMirrorY -- vx = -vx, vy = -vy, every byte.
//  A bounce reverses the direction and keeps the speed: the negation is two's
//  complement, so it is exact for the sign-extended forms too.
gkBallMirrorX:
    lda #0
    sec
    sbc GK_VXF
    sta GK_VXF
    lda #0
    sbc GK_VXL
    sta GK_VXL
    lda #0
    sbc GK_VXH
    sta GK_VXH
    rts

gkBallMirrorY:
    lda #0
    sec
    sbc GK_VYF
    sta GK_VYF
    lda #0
    sbc GK_VYI
    sta GK_VYI
    rts

// ==========================================
//  gkBallPaddle -- mirror the ball, then bend it by the hit
// ==========================================
//  d = bx - padx is small whenever the two boxes overlap, so the whole test runs
//  on d and never needs a compare wider than one byte plus its sign.
gkBallPaddle:
    //  The y band first. The bar is GK_PAD_Y .. +GK_PAD_BAR_H-1 and the ball is
    //  GK_BALL_W deep, so they meet while the ball's top is anywhere in
    //  GK_PAD_Y-(GK_BALL_W-1) .. GK_PAD_Y+GK_PAD_BAR_H-1 -- 13 rows (104 px), which
    //  a fall of at most GK_VYMAX a frame cannot skip.
    //  Its exits are plain `rts` rather than branches to the tail: the d
    //  computation below puts that tail out of branch range.
    lda GK_BYI
    cmp #GK_PAD_Y - (GK_BALL_W - 1)
    bcs gkBallPYin
    rts                         // still above the paddle
gkBallPYin:
    cmp #GK_PAD_Y + GK_PAD_BAR_H
    bcc gkBallPDepth
    rts                         // already past it
gkBallPDepth:
    //  padx as a 9-bit pair, then d = bx - padx as a 16-bit signed value
    lda GK_PADK
    clc
    adc #GK_PAD_MIN
    sta GK_PLO
    lda #0
    adc #0
    sta GK_PHI
    lda GK_BXL
    sec
    sbc GK_PLO
    sta GK_OFFL
    lda GK_BXH
    sbc GK_PHI
    sta GK_OFFH
    //  The boxes overlap exactly when d is in -(GK_BALL_W-1) .. GK_PAD_W-1. The
    //  no-hit exits share an `rts` placed right here rather than branching to the
    //  routine's tail: the hit path below is long enough to put that tail beyond
    //  branch range.
    lda GK_OFFH
    bmi gkBallPNeg
    bne gkBallPNoHit            // d > 255: no overlap
    lda GK_OFFL
    cmp #GK_PAD_W
    bcs gkBallPNoHit
    jmp gkBallPHit
gkBallPNeg:
    lda GK_OFFL
    cmp #GK_HIT_NEG
    bcs gkBallPHit              // d >= -(GK_BALL_W-1): the boxes touch
gkBallPNoHit:
    rts
gkBallPHit:
    jsr gkBallMirrorY           // up, with the speed it arrived with
    //  Lift the ball clear, so the next frame cannot re-trigger on this overlap.
    lda #GK_PAD_Y - GK_BALL_W
    sta GK_BYI
    lda #0
    sta GK_BYF
    //  The angle: offset = d - GK_HIT_BIAS, scaled by 8 (three asl/rol on a
    //  signed 16-bit value), so the paddle's edge sends the ball away at up to
    //  0.81 px a frame while its centre sends it nearly straight up.
    lda GK_OFFL
    sec
    sbc #GK_HIT_BIAS
    sta GK_OFFL
    lda GK_OFFH
    sbc #0
    sta GK_OFFH
    asl GK_OFFL
    rol GK_OFFH
    asl GK_OFFL
    rol GK_OFFH
    asl GK_OFFL
    rol GK_OFFH
    //  ---- THE ENGLISH. A paddle moving into the ball adds to the angle, one
    //  moving away subtracts, so chasing a ball leftwards sends it away harder to
    //  the left and catching it while moving right cuts the bounce back. It is HALF
    //  the paddle's speed, never the whole of it: the whole 1 px a frame would swamp
    //  the +-0.84 the angle itself spans, and the ball would leave almost sideways.
    lda GK_PADV
    beq gkBallPAfterSpin        // it did not move, so it has nothing to say
    bmi gkBallPSpinLeft
    clc
    lda GK_OFFL
    adc #GK_VXSPIN_L
    sta GK_OFFL
    lda GK_OFFH
    adc #GK_VXSPIN_H
    sta GK_OFFH
    jmp gkBallPAfterSpin
gkBallPSpinLeft:
    sec
    lda GK_OFFL
    sbc #GK_VXSPIN_L
    sta GK_OFFL
    lda GK_OFFH
    sbc #GK_VXSPIN_H
    sta GK_OFFH
gkBallPAfterSpin:
    //  ---- THE MIRROR. A flat face reflects the horizontal component unchanged,
    //  so the ball keeps the vx it arrived with and the paddle's angle is ADDED
    //  to it: a centre hit is an exact mirror, an off-centre one bends the shot
    //  the ball already had by up to 0.84 px/frame towards the side it landed on.
    //  vy was mirrored above.
    lda GK_VXF
    clc
    adc GK_OFFL
    sta GK_VXF
    lda GK_VXL
    adc GK_OFFH
    sta GK_VXL
    //  ---- The third byte is the SIGN of the sum, never a copy of its integer
    //  byte: gkBallMove adds it into the x's high byte, so a 1 there is a 256 px
    //  jump. The cap keeps |vx| small, so a positive sum's integer byte never sets
    //  its top bit and `bmi` below is the exact sign test (asserted at GK_VXCAP).
    lda GK_VXL
    bmi gkBallPNegSum
    //  ---- the cap. The paddle adds to the arriving speed now, so a player
    //  chasing the ball would ratchet it up bounce after bounce.
    cmp #GK_VXCAP
    bcc gkBallPPosFloor
    lda #GK_VXCAP
    sta GK_VXL
    lda #0
    sta GK_VXF
gkBallPPosFloor:
    //  WARNING: NEVER leave |vx| under GK_VXMIN. A centre hit cannot reach this:
    //  the floor only bites under one, so the ball keeps at least GK_VXMIN out of
    //  every bounce and a dead-centre hit is the exact mirror above. The floor is
    //  on the SUM's own sign, so a hit that reversed the ball is not floored back
    //  the way it came.
    lda GK_VXL
    bne gkBallPPosStore         // 1.0 px/frame or more: above the floor
    lda GK_VXF
    cmp #GK_VXMIN
    bcs gkBallPPosStore
    lda #GK_VXMIN
    sta GK_VXF
gkBallPPosStore:
    lda #0
    sta GK_VXH
    rts
gkBallPNegSum:
    cmp #GK_VXCAPN
    bcs gkBallPNegFloor         // -2.0 or -1.x: inside the cap
    lda #GK_VXCAPN
    sta GK_VXL
    lda #0
    sta GK_VXF
gkBallPNegFloor:
    lda GK_VXL
    cmp #$ff
    bne gkBallPNegStore         // -1.0 px/frame or less: above the floor
    lda GK_VXF
    cmp #GK_VXMINNEG
    bcc gkBallPNegStore
    lda #GK_VXMINNEG
    sta GK_VXF
gkBallPNegStore:
    lda #$ff
    sta GK_VXH
    rts

// ==========================================
//  gkGlyphAt -- draw the glyph in gkGLO/gkGHI at (gkBCELL, gkBROW)
// ==========================================
//  kPrintGlyphAt writes ONE cell at an explicit (X,Y): no cursor advance, no wrap,
//  no scroll, so a cell in the surface's last row is as safe as any other. It
//  clobbers A, X, Y, kPtr and the whole kArg block, which is why the position and
//  the glyph pointer are both reloaded from MEMORY for every cell -- the cell sits
//  in the image (gkBCELL/gkBROW) rather than in X and Y.
gkGlyphAt:
    lda gkGLO
    sta srcPtr
    lda gkGHI
    sta srcPtr+1
    ldx gkBCELL
    ldy gkBROW
    jmp kPrintGlyphAt

// ==========================================
//  gkPaintBrick -- paint one board entry's four cells
// ==========================================
//  Input: X = the board index, A = its hit points. 0 blanks the cells, 1 draws the
//  cracked glyph and 2-3 the solid one, and the colour follows the hit points.
//  This is the ONLY place a brick is drawn, erased or repainted, so a repaint after
//  a hit touches that brick's four cells and nothing else on the surface.
gkPaintBrick:
    sta gkBHP
    //  The parity that picks this brick's hue: a CHECKERBOARD, so the colour
    //  alternates down the rows as well as along them. Bit 0 of the board index is
    //  the column's parity and bit 3 the row's -- a row is GK_BRICK_COLS = 8 bricks
    //  wide, an even stride, so the row's bit never reaches bit 0 -- and the two are
    //  XORed into the one bit gkHpColour reads.
    txa
    and #1
    sta gkPAR                   // column parity
    txa
    and #%00001000              // the row's parity sits at bit 3
    lsr
    lsr
    lsr                         // -> bit 0
    eor gkPAR
    sta gkPAR                   // column ^ row
    //  The cell origin from the board index -- derived BEFORE the colour call,
    //  because kSetTextColor clobbers X (`ldx curTask`) and X is the index: with the
    //  calls the other way round every brick derives its origin from a TASK SLOT and
    //  all 48 paint into the same four cells.
    txa
    and #GK_BRICK_COLS - 1
    asl
    asl
    clc
    adc #GK_BRICK_X0            // col = X0 + (i & 7)*4
    sta gkBCELL
    txa
    lsr
    lsr
    lsr
    clc
    adc #GK_BRICK_Y0            // row = Y0 + i/8
    sta gkBROW
    lda gkBHP
    asl                         // hit points * 2
    ora gkPAR                   // + the parity = the colour table index
    tay
    lda gkHpColour,y
    jsr kSetTextColor           // clobbers A, X, Y, kPtr
    ldy gkBHP
    lda gkGlyphLo,y
    sta gkGLO
    lda gkGlyphHi,y
    sta gkGHI
    ldx #GK_BRICK_CELLS
gkPbCell:
    stx gkBCNT                  // kGlyphAt clobbers X, so the count lives in memory
    jsr gkGlyphAt
    inc gkBCELL
    ldx gkBCNT
    dex
    bne gkPbCell
    rts

// ==========================================
//  gkDrawBricks -- paint the whole brick area
// ==========================================
//  Every board entry, four cells each: this level's bricks in their colours and the
//  gaps blanked. One pass handles a fresh level AND a level change, which is the
//  only way the gaps of the new level are guaranteed clear.
gkDrawBricks:
    ldx #0
gkBkLoop:
    txa
    pha                         // gkPaintBrick clobbers X
    lda gkBoard,x
    jsr gkPaintBrick
    pla
    tax
    inx
    cpx #GK_BRICKS
    bcc gkBkLoop
    rts

// ==========================================
//  gkBuildLevel -- decode a level table into gkBoard
// ==========================================
//  One NIBBLE per brick, two bricks a byte, low brick first, so a row is four bytes
//  and each level is GK_LEVEL_BYTES. The value IS the brick's hit points: 0 is a
//  gap, 1..3 the hits to break it.
gkBuildLevel:
    //  the level table is (level-1) * GK_LEVEL_BYTES into gkLevels. kPtr is used as
    //  the walk pointer because (zp),y needs a zero-page address and the whole walk
    //  makes no kernel call, so nothing can clobber it.
    lda gkLevel
    sec
    sbc #1
    asl
    asl
    asl                         // *8
    sta gkSCR0
    asl                         // *16
    clc
    adc gkSCR0                  // *24, which is GK_LEVEL_BYTES
    clc
    adc #<gkLevels
    sta kPtr
    lda #>gkLevels
    adc #0
    sta kPtr+1
    ldy #0
    ldx #0
    lda #0
    sta gkBricksLeft
gkBldByte:
    lda (kPtr),y
    pha
    and #$0f
    sta gkBoard,x               // the low brick
    inx
    pla
    lsr
    lsr
    lsr
    lsr                         // the high brick
    sta gkBoard,x
    inx
    iny
    cpy #GK_LEVEL_BYTES
    bcc gkBldByte
    //  count what is standing
    ldx #GK_BRICKS - 1
gkBldCount:
    lda gkBoard,x
    beq gkBldNotBrick
    inc gkBricksLeft
gkBldNotBrick:
    dex
    bpl gkBldCount
    rts

// ==========================================
//  gkHud -- the labels, the score and the bricks left
// ==========================================
gkHud:
    lda #GK_C_TEXT
    jsr kSetTextColor
    lda #<gkSScore
    sta kPtr
    lda #>gkSScore
    sta kPtr+1
    ldx #GK_SCORE_LBL
    ldy #GK_HUD_ROW
    jsr gkPrintStr
    lda #<gkSBricks
    sta kPtr
    lda #>gkSBricks
    sta kPtr+1
    ldx #GK_BRICKS_LBL
    ldy #GK_HUD_ROW
    jsr gkPrintStr
    lda #<gkSLives
    sta kPtr
    lda #>gkSLives
    sta kPtr+1
    ldx #GK_LIVES_LBL
    ldy #GK_HUD_ROW
    jsr gkPrintStr
    lda #<gkSLevel
    sta kPtr
    lda #>gkSLevel
    sta kPtr+1
    ldx #GK_LEVEL_LBL
    ldy #GK_HUD_ROW
    jsr gkPrintStr
    jsr gkDrawScore
    jsr gkDrawBricksLeft
    jsr gkDrawLives
    jmp gkDrawLevel

//  gkDrawLives -- the lives count. One digit covers GK_LIVES_START, and the label
//  is printed with the rest of the HUD (gkHud).
gkDrawLives:
    ldx #GK_LIVES_COL
    ldy #GK_HUD_ROW
    jsr kSetCursor
    lda gkLives
    clc
    adc #'0'
    jmp kPrintChar

//  gkDrawLevel -- the level number. One digit covers GK_LEVEL_COUNT, and the label
//  is printed with the rest of the HUD (gkHud).
gkDrawLevel:
    ldx #GK_LEVEL_COL
    ldy #GK_HUD_ROW
    jsr kSetCursor
    lda gkLevel
    clc
    adc #'0'
    jmp kPrintChar

//  gkDrawScore -- the 16-bit score as five decimal digits.
//  WARNING: REPEATED SUBTRACTION, NOT DECIMAL MODE. `sed` would leave the CPU in BCD for
//  the length of the add, and the timer IRQ does arithmetic in that window -- the
//  keyboard scan's own `adc` would then compute a wrong index and deliver a wrong
//  key code, and one of the wrong keys is `q`, which quits the game. Binary
//  arithmetic here and decimal only at the point of display avoids the whole class.
gkDrawScore:
    lda gkScoreLo
    sta gkSCR0                  // a copy: the loop drains what it tests
    lda gkScoreHi
    sta gkSCR1
    ldx #GK_SCORE_COL
    ldy #GK_HUD_ROW
    jsr kSetCursor
    // fall through
//  gkPrintDigits -- the 16-bit value in gkSCR0/gkSCR1 as five decimal digits, at the
//  live cursor. The HUD's score and every hi-score row come through here, so there is
//  one decimal printer and one place its warning lives -- see above.
gkPrintDigits:
    ldy #0
gkScPow:
    lda #'0'
    sta gkDIG
gkScSub:
    lda gkSCR0
    sec
    sbc gkPowLo,y
    sta gkSCR2
    lda gkSCR1
    sbc gkPowHi,y
    bcc gkScDigit               // it borrowed: this power is done
    sta gkSCR1
    lda gkSCR2
    sta gkSCR0
    inc gkDIG
    jmp gkScSub
gkScDigit:
    tya
    pha                         // kPrintChar clobbers Y, and Y is the power index
    lda gkDIG
    jsr kPrintChar
    pla
    tay
    iny
    cpy #GK_SCORE_DIGITS
    bcc gkScPow
    rts

//  gkDrawBricksLeft -- the bricks-left count as two digits.
//  The tens are found by subtracting ten until it borrows, then the borrow is
//  undone by adding it back, which leaves the units in A.
gkDrawBricksLeft:
    ldx #GK_BRICKS_COL
    ldy #GK_HUD_ROW
    jsr kSetCursor
    lda gkBricksLeft
    ldx #'0' - 1
gkBrTens:
    inx
    sec
    sbc #10
    bcs gkBrTens
    adc #10                     // undo the last subtraction
    pha                         // the units, while the tens are printed
    txa                         // kPrintChar clobbers X
    jsr kPrintChar
    pla
    clc
    adc #'0'
    jmp kPrintChar

// ==========================================
//  gkWalls -- the frame, drawn once
// ==========================================
//  Row 1 across, and the two side columns down to row 24 -- the surface's own last row.
//  Every cell goes through gkGlyphAt (kPrintGlyphAt), which writes a FIXED cell: no cursor
//  advance, no wrap, no scroll. Row 24 is then complete -- the two corners are wall cells
//  and the death line covers the columns between them.
gkWalls:
    lda #GK_C_WALL
    jsr kSetTextColor
    lda #<gkGlyphWall
    sta gkGLO
    lda #>gkGlyphWall
    sta gkGHI
    ldy #GK_WALL_TOP
    sty gkBROW
    ldx #0
gkWTop:
    stx gkBCELL
    jsr gkGlyphAt
    ldx gkBCELL
    inx
    cpx #GK_COLS
    bcc gkWTop
    ldy #GK_WALL_TOP
    sty gkBROW
gkWRow:
    ldx #GK_WALL_LEFT
    stx gkBCELL
    jsr gkGlyphAt
    ldx #GK_WALL_RIGHT
    stx gkBCELL
    jsr gkGlyphAt
    ldy gkBROW
    iny
    sty gkBROW
    cpy #GK_WALL_LAST+1         // +1: the row above was DRAWN before this test, so the bound
    bcc gkWRow                  //   is one past the last row to draw (rows GK_WALL_TOP..LAST)
    //  ---- the death line (see GK_DEAD_ROW). Columns 1..38, so it runs between the walls
    //  and overwrites neither, and only the GLYPH changes: the colour is the wall's
    //  already, set at the top of this routine.
    lda #<gkGlyphDead
    sta gkGLO
    lda #>gkGlyphDead
    sta gkGHI
    lda #GK_DEAD_ROW
    sta gkBROW
    ldx #GK_WALL_LEFT + 1
    stx gkBCELL
gkWDead:
    jsr gkGlyphAt
    ldx gkBCELL
    inx
    stx gkBCELL
    cpx #GK_WALL_RIGHT
    bcc gkWDead
    rts

// ==========================================
//  gkBrickAt -- damage the brick at (gkBCELL, gkBROW), if there is one
// ==========================================
//  C=1 on a hit. The brick's hit points drop by one; at zero it is erased, the
//  count falls, and either way the score moves. gkBoard -- a byte array in THIS
//  IMAGE -- is the authority: the drawn cells are presentation, and a second reader
//  (a thread child, later) can consult the array with no char shadow and no OCR.
gkBrickAt:
    lda gkBROW
    cmp #GK_BRICK_Y0
    bcc gkBrickNo
    cmp #GK_BRICK_ENDY
    bcs gkBrickNo
    lda gkBCELL
    cmp #GK_BRICK_X0
    bcc gkBrickNo
    cmp #GK_BRICK_ENDX
    bcs gkBrickNo
    //  the board index = (row - Y0)*COLS + (col - X0)/CELLS, and both of those are
    //  shifts because the row is 8 wide and a brick is 4 cells
    lda gkBROW
    sec
    sbc #GK_BRICK_Y0
    asl
    asl
    asl                         // *8
    sta gkSCR0
    lda gkBCELL
    sec
    sbc #GK_BRICK_X0
    lsr
    lsr                         // /4
    clc
    adc gkSCR0
    tax                         // X = the board index
    lda gkBoard,x
    beq gkBrickNo               // a gap
    sec
    sbc #1
    sta gkBoard,x
    bne gkBrickCracked
    //  ---- gone: count it, score it, and blank its four cells
    dec gkBricksLeft
    ldy gkLevel
    lda gkPtsTab-1,y            // 10 x the level, so a later level pays more
    jsr gkAddScore
    lda #0
    jsr gkPaintBrick            // hit points 0 blanks the cells
    lda gkBricksLeft
    bne gkBrickHud
    jsr gkNextLevel             // the wall is empty: build and paint the next
    jmp gkBrickHitDone
gkBrickCracked:
    lda #GK_PTS_CRACK
    jsr gkAddScore
    lda gkBoard,x
    jsr gkPaintBrick            // repaint THAT brick and nothing else
gkBrickHud:
    jsr gkHud
gkBrickHitDone:
    sec                         // C=1: the ball met a brick
    rts
gkBrickNo:
    clc
    rts

//  gkAddScore -- A = the points, into the 16-bit score. Plain binary: see the
//  warning on gkDrawScore about why there is no decimal mode here.
gkAddScore:
    clc
    adc gkScoreLo
    sta gkScoreLo
    bcc gkAddDone
    inc gkScoreHi
gkAddDone:
    rts

// ==========================================
//  gkBallBrick -- the ball against the wall of bricks
// ==========================================
//  The ball's box is 8 px and a cell is 8 px, so the box spans at most two cells on
//  each axis: the 2x2 block at the ball's own cell and the next one. Each of the
//  four is tested in turn, and the FIRST brick found ends the frame's test.
//
//  The response comes from which axis the hit cell differs from the BALL'S CENTRE
//  cell on -- a different column reverses vx, a different row reverses vy, and a
//  corner does both. Reversing vy alone would be cheaper and is wrong: a ball that meets
//  a brick's SIDE has to come off it sideways, and a top-and-bottom-only response leaves a
//  whole class of hits unreachable.
gkBallBrick:
    //  the ball's own cell. (x - 24) does not fit a byte -- x runs to 328 -- so the
    //  ninth bit is folded in afterwards as 32, and the low three bits of the low
    //  byte are kept first: they say where the ball sits INSIDE its cell, which is
    //  what decides whether its centre is in this cell or the next.
    lda GK_BXL
    sec
    sbc #GK_WIN_X
    sta gkSCR0
    and #7
    sta gkOffX
    lda GK_BXH
    sbc #0
    asl
    asl
    asl
    asl
    asl                         // the ninth bit becomes 32
    sta gkSCR1
    lda gkSCR0
    lsr
    lsr
    lsr
    clc
    adc gkSCR1
    sta gkBallCellC
    //  the row is plain 8-bit: y never leaves 65..233
    lda GK_BYI
    sec
    sbc #GK_WIN_Y
    sta gkSCR0
    and #7
    sta gkOffY
    lda gkSCR0
    lsr
    lsr
    lsr
    sta gkBallCellR
    //  the ball's centre cell: the centre is four pixels into the box, so it is the
    //  next cell when the ball's own offset in its cell is four or more
    lda gkOffX
    cmp #GK_BALL_CENTRE
    lda gkBallCellC
    adc #0
    sta gkSCR2                  // the centre column
    lda gkOffY
    cmp #GK_BALL_CENTRE
    lda gkBallCellR
    adc #0
    sta gkSCR3                  // the centre row
    //  the four cells of the block
    ldx #0
gkBBCand:
    stx gkCandIx
    txa
    and #1
    clc
    adc gkBallCellC
    sta gkBCELL
    txa
    lsr
    clc
    adc gkBallCellR
    sta gkBROW
    //  Which axes THIS candidate differs on, decided before the damage call and not
    //  after it: gkBrickAt repaints the brick, and repainting needs gkBCELL/gkBROW
    //  for the brick's own cells. A candidate that misses is overwritten by the next.
    lda #0
    sta gkFlipX
    sta gkFlipY
    lda gkBCELL
    cmp gkSCR2
    beq gkBBCandRow
    inc gkFlipX
gkBBCandRow:
    lda gkBROW
    cmp gkSCR3
    beq gkBBCandTest
    inc gkFlipY
gkBBCandTest:
    jsr gkBrickAt
    bcs gkBBHit
    ldx gkCandIx
    inx
    cpx #4
    bcc gkBBCand
    rts                         // no brick: the ball is in open air
gkBBHit:
    lda gkFlipX
    ora gkFlipY
    bne gkBBHaveAxis
    //  neither differs, so the box is already INSIDE a brick -- reachable when a new
    //  level is built around a ball that is sitting still. The only way out is both.
    lda #1
    sta gkFlipX
    sta gkFlipY
gkBBHaveAxis:
    lda gkFlipX
    beq gkBBApplyY
    jsr gkBallMirrorX
gkBBApplyY:
    lda gkFlipY
    beq gkBBDone
    jsr gkBallMirrorY
gkBBDone:
    rts

// ==========================================
//  gkNextLevel -- the wall is empty
// ==========================================
gkNextLevel:
    inc gkLevel
    lda gkLevel
    cmp #GK_LEVEL_COUNT
    bcc gkLvlOk
    lda #1                      // past the last level: round to the first again
    sta gkLevel
gkLvlOk:
    jsr gkBuildLevel
    jsr gkDrawBricks
    jsr gkHud                    // the count and the level both moved
    lda #0
    sta GK_BSTATE               // and the ball waits on the paddle again
    jmp gkBallStick

// ==========================================
//  gkFrameBox -- a complete box around the whole surface
// ==========================================
//  The ATTRACT screens' frame, and the difference from gkWalls is the point: gkWalls is
//  the PLAY field's frame, open along the top (row 0 is the HUD there) and along the
//  bottom (the ball dies there), while this one closes all four sides -- row 0 across,
//  row 24 across, and the two side columns between them.
//  The bottom edge IS the surface's last row and needs no special handling: every edge cell
//  goes through kPrintGlyphAt, which advances and wraps nothing (see gkGlyphAt).
gkFrameBox:
    lda #GK_C_WALL
    jsr kSetTextColor
    lda #<gkGlyphWall
    sta gkGLO
    lda #>gkGlyphWall
    sta gkGHI
    lda #GK_FRAME_TOP
    sta gkBROW
    jsr gkFrameRow               // the top edge, every column
    lda #GK_FRAME_TOP + 1        // then the two side columns, down to the row above
    sta gkBROW                   //   the bottom edge -- which is drawn differently
gkFbSide:
    lda #GK_WALL_LEFT
    sta gkBCELL
    jsr gkGlyphAt
    lda #GK_WALL_RIGHT
    sta gkBCELL
    jsr gkGlyphAt
    inc gkBROW
    lda gkBROW
    cmp #GK_FRAME_BOTTOM
    bcc gkFbSide
    //  fall through -- the bottom edge, the surface's LAST row, drawn by the same full-row
    //  helper as the top edge: kPrintGlyphAt writes fixed cells, so the last row is ordinary
    lda #GK_FRAME_BOTTOM
    sta gkBROW
    jmp gkFrameRow

//  One whole row of a frame, at gkBROW. X walks the columns, which is why the count is
//  reloaded from gkBCELL: gkGlyphAt clobbers X.
gkFrameRow:
    ldx #0
gkFrNext:
    stx gkBCELL
    jsr gkGlyphAt
    ldx gkBCELL
    inx
    cpx #GK_COLS
    bcc gkFrNext
    rts

// ==========================================
//  gkStartAttract -- the attract loop, from screen 0
// ==========================================
//  Four screens -- title, the CONTROLS, credits, and the high score table (which is
//  also where a qualifying score's name is typed, so the table is in front of the
//  player before every game). Any key starts a game; the
//  16-bit countdown advances the screens in between. 'q' is checked above the mode
//  dispatch, so it leaves from every screen.
gkStartAttract:
    lda #GK_MODE_ATTRACT
    sta gkMode
    sprOn(0)                    // no paddle or ball hovering over the screens
    lda #GK_MUS_ATTRACT         // and the attract tune, which is also the tune a game
    jsr gkMusSet                //   STARTS under -- one of the three CGSC tunes
    lda #0                      // and no key left over from the game that just ended: the
    sta gkKeyArmed              //   latch shuts until the player lets go of the keys
    jsr gkDrainKeys
    lda #0                      // screen 0 is the title
    // fall through -- A = the screen number, 0..GK_ATTRACT_SCREENS-1
gkAtShow:
    sta gkAtStep
    lda #<GK_ATTRACT_DELAY      // ---- split: the delay is > 255 (see the constant)
    ldx #>GK_ATTRACT_DELAY
    jsr gkTickSet
    jsr kClearScreen
    jsr gkFrameBox              // the CLOSED box: every attract screen wears it
    //  Each screen is a jmp through an inverted branch (Rule 4): the title is the
    //  fall-through and the others are past the end of it.
    ldy gkAtStep
    cpy #1
    bne gkAtNot1
    jmp gkAtControls
gkAtNot1:
    cpy #2
    bne gkAtNot2
    jmp gkAtCredits
gkAtNot2:
    cpy #3
    bne gkAtTitle
    jmp gkAtHiscore
    //  --- 0: title ---
gkAtTitle:
    lda #GK_C_TITLE
    jsr kSetTextColor
    gkPut(15, 6, gkSTitle)
    lda #GK_C_TEXT
    jsr kSetTextColor
    gkPut(8, 8, gkSTitleSub)
    gkPut(13, 12, gkSPressAny)
    gkPut(12, 14, gkSTitleHint)
    rts
    //  --- 1: the controls, which is the only place they are shown ---
gkAtControls:
    lda #GK_C_TITLE
    jsr kSetTextColor
    gkPut(16, 5, gkSCtrlTitle)
    lda #GK_C_TEXT
    jsr kSetTextColor
    gkPut(12, 8, gkSCtrl1)
    gkPut(12, 10, gkSCtrl2)
    gkPut(12, 12, gkSCtrl3)
    gkPut(12, 14, gkSCtrl4)
    rts
    //  --- 2: credits ---
gkAtCredits:
    lda #GK_C_TITLE
    jsr kSetTextColor
    gkPut(15, 6, gkSTitle)
    lda #GK_C_TEXT
    jsr kSetTextColor
    gkPut(10, 10, gkSCredMus)   // the only borrowed thing in this game: the tunes are the
    gkPut(13, 12, gkSCred3)     //   CGSC's (see NOTICE); gordonOS wrote the rest
    rts
    //  --- 3: the high score table, which gkPrintHi draws for both this screen and
    //  the name entry -- one routine, so the table cannot look different in the two
    //  places it appears.
gkAtHiscore:
    jmp gkPrintHi

// ==========================================
//  The mode's countdown -- one pair of bytes for every screen that counts
// ==========================================
//  A 16-bit frame countdown, LOW byte first so the pair decrements as one. Sixteen bits
//  because the delays do not fit a byte and KickAssembler v5.25 SILENTLY truncates an
//  out-of-range immediate (see GK_ATTRACT_DELAY), and ONE pair because the mode machine
//  runs exactly one screen at a time: an attract screen's own hold and the game-over
//  screen's wait for an answer can never overlap. Every screen that counts SETS it on entry
//  (gkTickSet) and the screen decides what running out means -- the attract loop advances a
//  screen, the game-over screen leaves. The name entry is deliberately NOT one of them.

//  gkTickSet -- A = the low byte, X = the high byte, of a 100% machine's count.
//  WARNING: a machine that declares itself faster shows more displayed frames per real
//  second, so the count is shifted LEFT by sysSpeedShift -- the same rule gortris's
//  gtPace16 applies to the same kind of delay -- which keeps a screen's hold and the
//  game-over wait the same REAL length at any declared speed. Sixteen bits cannot
//  overflow at any speed `speed.com` accepts (1016 x 8 = 8128), and the count is never
//  shifted down to zero.
gkTickSet:
    sta gkTickLo
    stx gkTickHi
    ldy sysSpeedShift
    beq gkTsRet
    bmi gkTsFewer
gkTsMore:
    asl gkTickLo
    rol gkTickHi
    dey
    bne gkTsMore
    rts
gkTsFewer:
    lsr gkTickHi
    ror gkTickLo
    lda gkTickHi
    ora gkTickLo
    bne gkTsNext
    inc gkTickLo                // never 0: the screen must still advance
    rts
gkTsNext:
    iny
    bne gkTsFewer
gkTsRet:
    rts

//  gkTick16 -- one displayed frame off the countdown. C=1 when it reaches zero.
gkTick16:
    lda gkTickLo
    bne gkTkDec
    dec gkTickHi
gkTkDec:
    dec gkTickLo
    lda gkTickLo
    ora gkTickHi
    beq gkTkZero
    clc
    rts
gkTkZero:
    sec
    rts

//  One ATTRACT frame: any key starts a game, otherwise the countdown runs. The key comes
//  from the KERNEL'S OWN QUEUE -- "press any key" means any key, which this game's five
//  mapped input bits cannot say -- and the gate is what keeps the game that just ended from
//  starting the next one (see gkKeysArmed).
gkAtFrame:
    jsr gkKeysArmed
    bcs gkAfTick                // the keyboard has not been idle yet: run the countdown
    jsr kReadKey
    beq gkAfTick
    jmp gkStartPlay
gkAfTick:
    jsr gkTick16
    bcc gkAfOut
    //  the screen's hold is up: on to the next one. The screen count is the CONSTANT, so
    //  adding a screen needs no arithmetic change here.
    lda gkAtStep
    clc
    adc #1
    cmp #GK_ATTRACT_SCREENS
    bcc gkAfNext
    lda #0                      // past the last screen: round to the first
gkAfNext:
    jmp gkAtShow
gkAfOut:
    rts

// ==========================================
//  gkStartPlay -- a fresh run: level 1, five lives, score 0
// ==========================================
//  The surface is cleared of the attract screen, the wall is rebuilt and drawn,
//  and the ball goes back on the paddle waiting for SPACE. The sprites come back
//  on for a game and off again for the attract screens, so they never hover over
//  the text.
gkStartPlay:
    lda #GK_MODE_PLAY
    sta gkMode
    lda #GK_MUS_PLAY            // the game's own tune
    jsr gkMusSet
    lda #GK_LIVES_START
    sta gkLives
    lda #1
    sta gkLevel
    lda #0
    sta gkScoreLo
    sta gkScoreHi
    sta gkPaused
    sta GK_LAST                 // no phantom edge: the key that started the game is
    sta GK_INPUT                //   this frame's input, not the next frame's fresh
    sta GK_EDGE                 //   edge
    sta GK_BSTATE               // and the ball waits on the paddle
    sta GK_VXF
    sta GK_VXL
    sta GK_VXH
    sta GK_VYI
    sta GK_VYF
    lda #GK_PAD_START           // the middle of the run, where a serve puts it
    sta GK_PADK
    sta GK_PADKOLD              // no english on the first frame: it did not move
    sprOn(GK_SPRITES_ON)
    jsr kClearScreen
    jsr gkWalls
    jsr gkBuildLevel
    jsr gkDrawBricks
    jsr gkHud
    jsr gkPadPlace
    jsr gkBallStick
    jmp gkBallPlace

// ==========================================
//  gkGameOver -- the last life is gone
// ==========================================
//  A SHELL SCREEN, not the play field: the field is cleared and the same closed box the
//  attract screens wear goes up, with the result and the prompt inside it. A play field
//  left standing after the last life reads as a game that has not ended, and the field
//  belongs to a game that is still running. The key that answers the prompt settles the
//  hi-score table and returns to the attract loop (gkOverFrame).
gkGameOver:
    lda #GK_MODE_OVER
    sta gkMode
    lda #0                      // the next game starts with the ball on the paddle
    sta GK_BSTATE
    sprOn(0)                    // and no sprite hovers over a shell screen
    lda #0
    sta gkMusDone               // the last game-over screen's tune is not THIS one's answer
    lda #GK_MUS_OVER            // the game-over tune, played ONCE: the child hands the
    jsr gkMusSet                //   attract tune back when it has played it through
                                //   (gkMusicThread), and gkOverFrame waits on that cue
    lda #0
    sta gkKeyArmed              // the keys from the game that just ended are NOT an answer:
    jsr gkDrainKeys             //   the latch shuts until the player lets go of them
    lda #<GK_RETURN_DELAY       // ---- split: like the attract delay (see the constant)
    ldx #>GK_RETURN_DELAY
    jsr gkTickSet
    jsr kClearScreen
    jsr gkFrameBox
    lda #GK_C_TITLE
    jsr kSetTextColor
    gkPut(15, GK_OVER_TEXT_ROW, gkSGameOver)
    lda #GK_C_TEXT
    jsr kSetTextColor
    gkPut(GK_OVER_SCORE_LBL, GK_OVER_SCORE_ROW, gkSScore)
    lda gkScoreLo               // the run's own score, through the HUD's own printer
    sta gkSCR0
    lda gkScoreHi
    sta gkSCR1
    ldx #GK_OVER_SCORE_COL
    ldy #GK_OVER_SCORE_ROW
    jsr kSetCursor
    jsr gkPrintDigits
    lda #GK_C_TEXT
    jsr kSetTextColor
    gkPut(15, GK_OVER_HINT_ROW, gkSPressKey)
    rts

// ==========================================
//  The keyboard's own queue: draining it, and when an answer counts
// ==========================================
//  The game reads keybHeld for its input, so NOTHING consumes the kernel's key queue while
//  a game is running -- and the IRQ keeps filling it. That is harmless until a screen asks
//  for a key: a press from the game is still sitting in there, and it answers.
//  WARNING: A DIRECTION HELD WHEN THE LAST LIFE GOES IS NOT AN ANSWER. The player is
//  usually still driving the paddle with 'j' or 'k', and a held key's kernel repeat pushes
//  an event every KEYB_REPEAT ticks -- so the game-over screen was skipped within a frame
//  or two and the high scores came up mid-steer. Every "press a key" screen therefore waits
//  for the hands to come OFF the keys and for the queue to be empty, and only then takes a
//  fresh press.

//  gkDrainKeys -- empty the queue: a FIXED 16 reads, whatever comes back.
//  WARNING: DO NOT STOP EARLY, AND DO NOT TRY TO ASK WHETHER THE QUEUE IS EMPTY.
//  kScanKeyboard returns $ff BOTH for an empty queue AND for a RELEASE event -- the IRQ
//  pushes $ff when a key comes up (keyboard.inc, kbReleased) -- so "kReadKey returned 0"
//  only means "empty OR release", and a loop that stops there stops at the first release
//  and LEAVES EVERY PRESS BEHIND IT. Nothing drains the queue while a game is running (the
//  game reads keybHeld, a level, and gkKeysArmed runs only on the attract and game-over
//  screens), so by game over it holds a full 16 mixed events; the presses that survived then
//  answered "press a key" on the very first armed frame, which left the game-over screen
//  before its tune had a chance to play. The queue's capacity IS 16 (the IRQ refuses a 17th),
//  so 16 reads always empty whatever was in it -- and every read CONSUMES what it pops,
//  releases included. The counter lives in memory because kReadKey clobbers X and Y.
gkDrainKeys:
    lda #16
    sta gkDrCnt
gkDkLoop:
    jsr kReadKey
    dec gkDrCnt
    bne gkDkLoop
    rts

//  gkKeysArmed -- C=0 when a key press may be taken as an answer.
//  WARNING: THE IDLE TEST IS A LATCH, NOT A PER-FRAME TEST, AND THAT IS THE WHOLE POINT.
//  A key that is HELD when the screen comes up is the player still playing: its kernel
//  repeat keeps pushing events, and one of them would answer the prompt for them. But a
//  press IS a moment when a key is held, so a per-frame idle test never lets an answer
//  through -- it drains the press itself and has nothing left by the time the key comes up.
//  So: while something is held the gate stays shut and the queue is drained; the first frame
//  that finds the keyboard IDLE arms it for good, and from then on any key read is a fresh
//  press and is the answer -- which holds only because gkDrainKeys empties the queue
//  completely, releases and all (see its warning).
gkKeysArmed:
    lda gkKeyArmed
    bne gkKaYes                 // already armed: a key read now is an answer
    lda keybHeld
    cmp #$ff
    bne gkKaWait                // something is still held: not armed yet
    inc gkKeyArmed              // idle: arm it, once, for the rest of this screen
    jsr gkDrainKeys             //   and throw away what the held key left behind
gkKaYes:
    clc
    rts
gkKaWait:
    jsr gkDrainKeys
    sec
    rts

//  One OVER frame: the message stays up until the keys are let go of and one is pressed,
//  and that press settles the hi-score table and returns to the attract loop. A screen
//  nobody answers runs down on its own and leaves by the SAME door, so an abandoned good
//  score still takes its row -- and the name entry it then reaches waits for the player
//  however long that takes (gkHiNameScreen).
//  WARNING: THE COUNTDOWN IS NOT THE CLOCK WHILE THE GAME-OVER TUNE IS PLAYING. That tune
//  is firstdt, a ONE-SHOT, so the screen waits for it to finish and leaves the moment it
//  does: the music is the clock. gkMusDone is the cue the child sets when it has played it
//  through, and the countdown is what a screen with NO tune to wait for falls back on -- no
//  child at all, or a child that gave the request back without playing it. A refusal is
//  neither of those: the child still holds the tune and is retrying for the chip, so that
//  screen waits, which is the whole point of the retry (gkMtBusy).
gkOverFrame:
    jsr gkKeysArmed
    bcs gkOfOut                 // the keyboard has not been idle yet: not an answer
    jsr kReadKey
    bne gkOfLeave
    lda gkMusDone
    bne gkOfNow                 // it played to the end: leave, whatever the countdown says
    lda gkMusSlot
    cmp #GK_MUS_NONE
    beq gkOfCount               // no child: nothing can ever play it
    lda gkMusWant
    cmp #GK_MUS_ATTRACT
    beq gkOfCount               // the child gave the request back without playing it
    rts                         // it is playing, or waiting for the SID: wait for it
gkOfNow:
    //  ---- leave on this frame's tick. The countdown is set to ONE, not zero: gkTick16
    //  borrows from the high byte when the low byte is already 0, so a zeroed countdown
    //  would be stranded at $ffff instead of expiring.
    lda #1
    ldx #0
    jsr gkTickSet
gkOfCount:
    jsr gkTick16                // nobody is answering: run the screen down
    bcc gkOfOut
gkOfLeave:
    jmp gkOverLeave
gkOfOut:
    rts

//  gkOverLeave -- the score is checked against the table HERE, on the game-over
//  screen's way out, rather than at the moment the last life went: that screen is
//  where the player reads the result, and its exit is where the table settles. A
//  score that does not make the table goes straight back to the attract loop; a
//  score that does takes the row, asks for the name, and is saved.
gkOverLeave:
    jsr gkScoreToBCD            // the table is BCD, the game's score is binary
    jsr gkHiCheck               // A = the row it takes (0..GK_HI_ENTRIES-1), or $FF
    cmp #$ff
    beq gkOlNoHi
    jsr gkHiInsert              // move the rows down and put the score in
    jsr gkHiNameScreen          // the table on screen, cursor on the new row
    jsr gkHiSave                // and write it back to the REU FS
gkOlNoHi:
    jmp gkStartAttract

// ==========================================
//  The hi-score table
// ==========================================
//  Ten bytes an entry: three BCD score bytes, most significant byte first, then the seven
//  name cells, in that order, which is how the row sits in memory. The table lives in this
//  image and is mirrored to the REU FS as `grknoid.hi`, so it survives a reboot.

//  gkScoreToBCD -- the run's 16-bit score into the table's three BCD bytes (gkScoreBCD).
//  The game's score is BINARY and the table is BCD, so this runs once, at
//  the moment a score is about to take a row.
//  A 16-bit score stops at 65535, so the table's leading byte -- the top TWO digits -- is
//  always $00 and the leading nibble of the second byte is always zero: the five digits
//  come from the same powers the HUD's printer uses (gkPowLo/gkPowHi), so the two cannot
//  drift apart.
//  WARNING: repeated subtraction, never `sed` -- see the warning on gkDrawScore.
gkScoreToBCD:
    lda gkScoreLo
    sta gkSCR0                  // a copy: the loop drains what it tests
    lda gkScoreHi
    sta gkSCR1
    ldy #0                      // power 0 = 10000, the ten-thousands digit
    jsr gkSbDigit
    sta gkScoreBCD              // byte 0's LOW nibble -- its high nibble is the leading
                                //   zero, and d(10000) can only reach 6, so it is already 0
    ldy #1
    jsr gkSbDigit
    asl
    asl
    asl
    asl
    sta gkScoreBCD+1            // byte 1's high nibble: the thousands
    ldy #2
    jsr gkSbDigit
    ora gkScoreBCD+1
    sta gkScoreBCD+1            // and its low nibble: the hundreds
    ldy #3
    jsr gkSbDigit
    asl
    asl
    asl
    asl
    sta gkScoreBCD+2            // byte 2's high nibble: the tens
    ldy #4
    jsr gkSbDigit
    ora gkScoreBCD+2
    sta gkScoreBCD+2            // and its low nibble: the units
    rts

//  A = the decimal digit for power Y (0 = 10000 .. 4 = 1), draining gkSCR0/gkSCR1.
//  One digit, by the same repeated subtraction gkPrintDigits does. Y is the caller's and
//  survives, because no printing call is made here.
gkSbDigit:
    lda #0
    sta gkDIG
gkSbSub:
    lda gkSCR0
    sec
    sbc gkPowLo,y
    sta gkSCR3
    lda gkSCR1
    sbc gkPowHi,y
    bcc gkSbGot
    sta gkSCR1
    lda gkSCR3
    sta gkSCR0
    inc gkDIG
    jmp gkSbSub
gkSbGot:
    lda gkDIG
    rts

gkHiMul10:
//  A = an entry number -> A = its byte offset in the table. Ten is not a power of two
//  and there are only three rows, so this is A*8 + A*2 rather than a real multiply.
    asl
    sta gkHiTmp
    asl
    asl
    clc
    adc gkHiTmp
    rts

//  A = an entry number -> kPtr = that entry (three score bytes, then the name).
gkHiPtr:
    jsr gkHiMul10
    clc
    adc #<gkHiTab
    sta kPtr
    lda #>gkHiTab
    adc #0
    sta kPtr+1
    rts

//  Does the run's score make the table? A = the row it takes (0 = best) or $FF.
//  The three score bytes are compared from the most significant down, and an exact tie
//  does NOT displace the incumbent: it takes the row BELOW the equal one, because
//  rewriting an equal entry would lose the earlier name for no gain.
gkHiCheck:
    lda #0
    sta gkHiEntry
gkHcNext:
    lda gkHiEntry
    jsr gkHiPtr                 // kPtr = the entry's score bytes
    ldy #0
gkHcBytes:
    lda gkScoreBCD,y
    cmp (kPtr),y
    bcc gkHcLower               // below this row: try the next
    beq gkHcSame                // equal so far: the next byte decides
    lda gkHiEntry               // higher: this is the row it takes
    rts
gkHcSame:
    iny
    cpy #GK_HI_SCORELEN
    bcc gkHcBytes
gkHcLower:
    inc gkHiEntry
    lda gkHiEntry
    cmp #GK_HI_ENTRIES
    bcc gkHcNext
    lda #$ff                    // it did not make the table
    rts

//  A = the row to take: the rows below it move down one, the score goes in, and the
//  name is filled with the placeholder dots the player types over.
gkHiInsert:
    sta gkHiSlot
    lda #GK_HI_ENTRIES-1        // the last row moves first, so nothing is
    sta gkHiEntry               //   overwritten before it is copied
gkHiInsLp:
    lda gkHiEntry
    cmp gkHiSlot
    beq gkHiInsScore
    sec
    sbc #1                      // copy entry[gkHiEntry-1] into entry[gkHiEntry]
    jsr gkHiPtr
    lda kPtr
    sta srcPtr
    lda kPtr+1
    sta srcPtr+1
    lda gkHiEntry
    jsr gkHiPtr
    ldy #GK_HI_ENTRYLEN-1
gkHiInsCopy:
    lda (srcPtr),y
    sta (kPtr),y
    dey
    bpl gkHiInsCopy             // y wraps to $FF after 0: all nine bytes
    dec gkHiEntry
    jmp gkHiInsLp
gkHiInsScore:
    lda gkHiSlot
    jsr gkHiPtr
    ldy #0
gkHiInsScLp:
    lda gkScoreBCD,y            // the three BCD score bytes, most significant first
    sta (kPtr),y
    iny
    cpy #GK_HI_SCORELEN
    bcc gkHiInsScLp
    lda #GK_HI_DOT
gkHiInsDotLp:
    cpy #GK_HI_ENTRYLEN
    bcs gkHiInsDone
    sta (kPtr),y
    iny
    jmp gkHiInsDotLp
gkHiInsDone:
    rts

//  The table on the high-score screen. Each row is copied out of the table first:
//  kPrintChar clobbers kPtr, so an entry cannot be read through a pointer while it
//  is being printed -- and the loop counters live in MEMORY, because kPrintChar
//  clobbers A, X and Y as well.
gkPrintHi:
    lda #GK_C_TITLE
    jsr kSetTextColor
    gkPut(GK_HI_COL, GK_HI_HDR_ROW, gkSHiTitle)
    lda #GK_C_TEXT
    jsr kSetTextColor
    lda #0
    sta gkHiEntry
gkPhiRowLp:
    lda gkHiEntry
    jsr gkHiPtr
    ldy #0
gkPhiCopyLp:
    lda (kPtr),y
    sta gkHiRowBuf,y
    iny
    cpy #GK_HI_ENTRYLEN
    bcc gkPhiCopyLp
    lda gkHiEntry               // the row: ROW0 + entry * ROWSTEP
    asl                         // ROWSTEP is 2, so this IS the row offset
    clc
    adc #GK_HI_ROW0
    tay
    ldx #GK_HI_COL
    jsr kSetCursor
    jsr gkPhiScore              // the six digits
    lda #$20                    // a space between the score and the name
    jsr kPrintChar
    lda #GK_HI_SCORELEN         // and the seven name cells
    sta gkHiIdx
gkPhiNameLp:
    ldy gkHiIdx
    lda gkHiRowBuf,y
    bne gkPhiNameOut
    lda #$20                    // an empty cell prints as a space, not as $00
gkPhiNameOut:
    jsr kPrintChar
    inc gkHiIdx
    lda gkHiIdx
    cmp #GK_HI_ENTRYLEN
    bcc gkPhiNameLp
    inc gkHiEntry
    lda gkHiEntry
    cmp #GK_HI_ENTRIES
    bcc gkPhiRowLp
    rts

//  The row's three BCD score bytes as six digits at the live cursor: each byte is two
//  nibbles, most significant first. The counter
//  lives in MEMORY, because kPrintChar clobbers A, X and Y.
gkPhiScore:
    lda #0
    sta gkHiIdx
gkPhiDigLp:
    ldy gkHiIdx
    lda gkHiRowBuf,y
    pha
    lsr
    lsr
    lsr
    lsr                         // the high nibble is the more significant digit
    clc
    adc #'0'
    jsr kPrintChar
    pla
    and #$0f
    clc
    adc #'0'
    jsr kPrintChar
    inc gkHiIdx
    lda gkHiIdx
    cmp #GK_HI_SCORELEN
    bcc gkPhiDigLp
    rts

//  Load the saved table. Anything other than a clean GK_HI_LEN-byte read leaves the
//  house scores in place: a missing file is a first session, not an error.
gkHiLoad:
    lda #<gkHiFileName
    sta kArg
    lda #>gkHiFileName
    sta kArg+1
    jsr kReuFindFile            // C=1: there is no file
    bcs gkHlDone
    lda reuFsRemain             // the size has to be exactly one table
    cmp #GK_HI_LEN
    bne gkHlDone
    lda reuFsRemain+1
    bne gkHlDone
    lda #<gkHiFileName
    sta kArg
    lda #>gkHiFileName
    sta kArg+1
    lda #<gkHiTab
    sta kPtr
    lda #>gkHiTab
    sta kPtr+1
    jsr kReuLoad
gkHlDone:
    rts

//  Write the table back. A failure is ignored: the table is still correct in RAM and
//  the next save tries again.
//  WARNING: saving is find -> delete -> save, because kReuSave refuses a duplicate
//  name -- and the FS's delete is a TOMBSTONE (docs/lib-filesys.md): it frees the
//  NAME only, so every save costs one directory slot and a fresh GK_HI_LEN bytes that
//  nothing but `format` reclaims. It is not an overwrite, and whoever touches this
//  next should know that.
gkHiSave:
    lda #<gkHiFileName
    sta kArg
    lda #>gkHiFileName
    sta kArg+1
    jsr kReuDelete              // C=1 when there was nothing to delete
    lda #<gkHiFileName
    sta kArg
    lda #>gkHiFileName
    sta kArg+1
    lda #<gkHiTab
    sta kPtr
    lda #>gkHiTab
    sta kPtr+1
    lda #<(gkHiTab+GK_HI_LEN)   // end address, EXCLUSIVE
    sta kArg+2
    lda #>(gkHiTab+GK_HI_LEN)
    sta kArg+3
    jmp kReuSave

//  gkHnCursor -- put the cursor on the next free cell of the new entry's name field
//  (the row of gkHiSlot, column GK_HI_NAME_COL + gkHiLen).
gkHnCursor:
    lda #GK_HI_NAME_COL
    clc
    adc gkHiLen
    tax
    lda gkHiSlot
    asl                         // ROWSTEP is 2
    clc
    adc #GK_HI_ROW0
    tay
    jmp kSetCursor              // X = col, Y = row: the one call that keeps both

//  gkHiNameScreen -- the player took a row, so take the name. The table is drawn
//  with gkPrintHi and the cursor sits ON the new row.
//  WARNING: NOT kReadString. The kernel's line reader spins on kScanKeyboard and
//  never yields, so a player taking ten seconds over a name would freeze the whole
//  system; this loop yields every pass and echoes each character itself.
//  ---- and the cursor is ours too, for the same reason: kReadString's reverse-video
//  cursor belongs to the spin loop we cannot use, so the KERNEL's block cursor is
//  turned on and parked on the name cell, where the IRQ's blink follows it with no
//  further code. kPrintChar saves and clears cursorOn while it prints, so the block
//  is never baked into a character.
//  ---- and this screen NEVER times out, unlike the game-over screen. It is a data-entry
//  step: RETURN is what commits the typed name (gkHnDone) and gkHiSave runs only after this
//  routine returns, so a countdown here would either throw a good score away or store a name
//  the player never finished. RETURN is the ONLY way out: 'q' is a
//  letter here (screen code $11 is inside the $01-$1A a name accepts), so it is typed into
//  the name rather than quitting, and gkLoop's quit check cannot fire either because this
//  screen runs inside a jsr. The blink keeps the cursor visibly parked until RETURN arrives.
gkHiNameScreen:
    jsr kClearScreen
    jsr gkFrameBox
    jsr gkPrintHi
    lda #GK_C_TITLE
    jsr kSetTextColor
    gkPut(7, GK_HI_MSG_ROW, gkSHiNew)
    lda #0
    sta gkHiLen
    sta gkKeyArmed              // the key that answered the game-over prompt is still down:
    jsr gkDrainKeys             //   wait for it to come up before a character is typed
    jsr gkHnCursor              // the cursor starts on the first name cell --
    jsr kEnableCursor           // --and it BLINKS there; clobbers A, X, Y, kPtr,
                                //   so it goes last
gkHnLoop:
    lda #SYS_YIELD
    jsr syscall                 // never spin (see the note above)
    jsr gkKeysArmed
    bcs gkHnLoop                // the key that answered the prompt is still down: wait
    jsr kReadKey
    beq gkHnLoop                // 0 = nothing this frame
    cmp #$81                    // RETURN: the name is done
    beq gkHnDone
    cmp #$80                    // DEL: take the last character back
    beq gkHnDel
    //  ---- "PRINTABLE" IS NOT "$20 AND UP", BECAUSE A IS A SCREEN CODE. The letters
    //  are $01-$1A ($41-$5A with shift held), so a `cmp #$20 / bcc` test throws away
    //  EVERY unshifted letter and the name cannot be typed. What is genuinely not a
    //  character: $00 (the '@' key, shared with the F-keys), $1B-$1F (S^ '[' ']'
    //  POUND <-) and $80+ (the kernel's special keys, two handled above).
    cmp #$80
    bcs gkHnLoop
    cmp #$20
    bcs gkHnOk
    cmp #$01
    bcc gkHnLoop
    cmp #$1b
    bcs gkHnLoop
    // fall through -- $01-$1a, the letters
gkHnOk:
    ldy gkHiLen
    cpy #GK_HI_NAMELEN          // seven cells: the field's width
    bcs gkHnLoop                // full: further characters are ignored
    //  WARNING: no repositioning here, and none is needed: the setup parks the
    //  cursor on the cell an empty name starts at, a print advances it one cell, and
    //  DEL comes back onto the cell it cleared. Positioning BETWEEN the key and the
    //  echo would print the row instead of the character -- gkHnCursor ends in `tay`,
    //  so it leaves A = the row it moved to.
    sta gkHiNameBuf,y           // Y is the index (gkHiLen has not changed yet)
    jsr kPrintChar              // echo it; the cursor advances onto the next cell
    inc gkHiLen
    jmp gkHnLoop
gkHnDel:
    ldy gkHiLen
    beq gkHnLoop
    dey
    sty gkHiLen                 // the length is now the cell being taken back
    jsr gkHnCursor
    lda #GK_HI_DOT              // '.' -- the field's placeholder, not a blank
    jsr kPrintChar
    //  WARNING: and then the cursor has to come back onto the cell this DEL just
    //  cleared. kPrintChar ALWAYS advances one cell, so without this second move
    //  every key typed after a DEL echoes one cell right of its buffer slot, and the
    //  next DEL re-derives its cell from gkHiLen -- so it wipes the placeholder and
    //  leaves the letter standing, making DEL look like it deleted the character
    //  BEFORE the last one.
    jsr gkHnCursor
    jmp gkHnLoop
gkHnDone:
    //  ---- the blink goes off BEFORE the block is erased, not after. A live block
    //  drawn into a surface copy a screen hand-over makes would be baked in, and
    //  once cursorOn is 0 no IRQ tick can draw it again, so the erase cannot be
    //  raced by the blink.
    jsr kDisableCursor
    jsr kBlinkReset             // un-draw the block: A, X, Y, kPtr, kArg+7
    lda gkHiSlot
    jsr gkHiPtr
    lda kPtr                    // kPtr = the entry's NAME field (two score bytes in)
    clc
    adc #GK_HI_SCORELEN
    sta kPtr
    lda kPtr+1
    adc #0
    sta kPtr+1
    ldy #0
gkHnStoreLp:
    lda #$20                    // space: the table prints all seven cells, so a short
    cpy gkHiLen                 //   name must leave blanks behind it
    bcs gkHnStoreIt
    lda gkHiNameBuf,y
gkHnStoreIt:
    sta (kPtr),y
    iny
    cpy #GK_HI_NAMELEN
    bcc gkHnStoreLp
    jmp gkPrintHi               // show the finished row

// ==========================================
//  The music -- three CGSC tunes on one thread child
// ==========================================
//  One child, three tunes, and ONE BUFFER that is allocated once and handed back only at
//  quit: the child kReuLoads whichever tune the mode asks for into it and plays it. Nothing
//  is allocated or freed per mode change, and that is the point -- see gkMusInit for the
//  pool arithmetic that made a per-tune malloc lose the biggest tune first, and
//  gkMusBufFree for the one place the pages go back.
//  WARNING: THE BLOCK IS THE TASK'S, THE LOADS ARE THE CHILD'S. The child is the only code
//  that can know it is outside kSidPlay, and the player reads those bytes on every jiffy it
//  is running, so nothing may write them while it plays -- which is why the parent owns the
//  block and the child only ever loads INTO it, never frees it.
//  WARNING: gkMusCur names the tune IN THE BUFFER, and it is set only after a load
//  SUCCEEDS. A failed load leaves the previous tune's bytes in place, so a stale gkMusCur
//  would have the child play the wrong tune; the failure path leaves it alone and the
//  fallback re-asks.
//  The child runs THIS task's code pages, so the request and the block pointer are ordinary
//  bytes in this image -- there is no IPC and no pointer to pass.
//  WARNING: every "no music" outcome is SILENT, never an error the player has to read. A
//  tune missing from the FS, no free TCB slot for the child, no pool room for the buffer,
//  and another task already owning the SID all leave the game running without music: the
//  chip is one resource, and sid.lib refuses the second caller rather than sharing it.
//  ---- AND EACH OF THOSE DEGRADES ON ITS OWN TERMS:
//  * SID HELD BY ANOTHER TASK. The tune is already in the buffer and is NOT thrown away, and
//    the child asks again every GK_MUS_BUSY passes. The retry is one kSidPlay call -- no FS
//    read and no pool request -- so the music comes back by itself the moment the other
//    task lets the chip go, even inside a mode that never changes.
//  * A TUNE THAT WILL NOT LOAD. Missing from the FS, unreadable, or LONGER than the buffer:
//    gkMusLoad refuses it (see its warning) and the child falls back to the attract tune,
//    and to silence if that will not load either. It does NOT retry on a timer -- the next
//    mode change asks again, which is often enough for a condition that clears.
//  * NO TCB SLOT for the child at all: the game runs silently for the session, and `ps`
//    simply shows no child.

//  gkMusInit -- allocate the ONE tune buffer, then start the child.
//  WARNING: THIS IS THE TASK'S ONLY kMalloc, AND THAT IS THE WHOLE POINT OF THE DESIGN. A
//  fresh block per tune -- 18, 4 and 2 pages, freed when the player returns -- would cycle
//  through the pool on every mode change: the sizes differ, the pages come back in pieces,
//  and the largest tune is the first request the pool can refuse while the smaller ones
//  still fit. One buffer, allocated once, means the allocator sees exactly one block for
//  the session.
//  WARNING: TWO RULES MADE THE BIG ONE FAIL, AND ONE ALLOCATION DEFEATS BOTH. kMalloc
//  refuses when owned + requested passes MAX_MALLOC_PAGES (64 pages: half the 32 KB
//  per-slot malloc shadow), and allocFromPool needs CONSECUTIVE free pages -- so a per-tune
//  cycle can lose the largest tune through either.
//  A failure here is silence with no child at all: the game plays without music and `ps`
//  shows no child. It is NOT retried, because a kMalloc retry can EVICT another task.
//  WARNING: NO kLibHold CALL, AND THAT IS THE POINT. filesys.lib is held for the whole
//  session -- the hi-score table is WRITTEN at game over, and the child READS a tune file
//  on every switch -- so there is no library left to hand back, and the call would be a
//  no-op. The child can use those entries because a loaded library's jump entries are
//  patched for the whole system, not per task.
//  Clobbers A, X, Y and kArg.
gkMusInit:
    lda #GK_MUS_PAGES
    jsr kMalloc                 // A = pages -> kPtr = the block
    bcs gkMuDone                // no room for a tune: no child either, and no music
    lda kPtr
    sta gkMusBufLo
    lda kPtr+1
    sta gkMusBufHi
    lda #<gkMusicThread
    sta spawnPtr
    lda #>gkMusicThread
    sta spawnPtr+1
    lda #GK_ZP_MUS_BASE
    sta kArg+6                  // the CHILD's ZP window, not ours (see the constant)
    lda #GK_ZP_MUS_LEN
    sta kArg+7
    lda #SYS_THREADSPAWN
    jsr syscall
    bcs gkMuNoChild             // no free slot: hand the buffer straight back
    stx gkMusSlot               // X = the child's slot
    rts
gkMuNoChild:
    jmp gkMusBufFree
gkMuDone:
    rts

//  gkMusLoad -- X = the tune index -> that tune's bytes into the ONE buffer, C=1 on any
//  failure (not in the FS, unreadable, or longer than the buffer). Nothing is allocated or
//  freed here: the block is the task's, and gkMusBufFree is the only thing that hands it
//  back.
//  WARNING: THE BLOCK IS VALIDATED, NOT RESIZED. A tune longer than GK_MUS_PAGES is REFUSED,
//  because loading it would run past the end of the block -- so raising the tune sizes means
//  raising the constant, and this is the guard that turns that mistake into silence.
//  kReuFindFile and kReuLoad both clobber kPtr and kArg, so the name is re-set in kArg
//  before each call that wants it. The image is passed to the player AS STORED: kSidPlay
//  skips the 2-byte load address itself.
//  WARNING: a SUCCESSFUL load does not make the tune playable -- the caller sets gkMusCur,
//  and a failed load deliberately leaves it alone (see the block header).
gkMusLoad:
    lda gkMusNameLo,x
    sta gkMusNLo
    lda gkMusNameHi,x
    sta gkMusNHi
    sta kArg+1
    lda gkMusNLo
    sta kArg
    jsr kReuFindFile            // C=1: no such file; its size lands in reuFsRemain
    bcs gkMlDone
    lda reuFsRemain
    clc
    adc #$ff                    // carry set when the low byte is not zero
    lda reuFsRemain+1
    adc #0                      // A = ceil(size / 256)
    bcs gkMlDone                // an absurd size
    cmp #GK_MUS_PAGES+1
    bcs gkMlDone                // longer than the buffer: refuse it, never overflow it
    lda gkMusNLo                // the FS wants the name in kArg again
    sta kArg
    lda gkMusNHi
    sta kArg+1
    lda gkMusBufLo              // and the destination in kPtr: the ONE buffer
    sta kPtr
    lda gkMusBufHi
    sta kPtr+1
    jmp kReuLoad                // C=0: the tune is in the buffer
gkMlDone:
    rts

//  gkMusSet -- A = the tune this mode wants.
//  A request for the tune already asked for changes NOTHING, which is what keeps the
//  attract tune running when the game-over screen hands the loop back (gkMusSet's own
//  handler already fell back to it). The hush is what unblocks the child; it re-reads the
//  request and starts whatever is named there.
//  Clobbers A, X, Y (kSidHush does).
gkMusSet:
    cmp gkMusWant
    beq gkMsOut
    sta gkMusWant
    lda #1
    sta gkMusOn
    jmp kSidHush
gkMsOut:
    rts

//  gkMusStop -- silence the chip and ask the child to leave the player loop.
gkMusStop:
    lda #0
    sta gkMusOn
    jmp kSidHush                 // tail call: the player returns, the child goes idle

//  gkMusicBye -- the exit: tell the child to leave for good, hush the chip, make sure it
//  really is gone, and then hand the tune buffer back. The free is LAST and after the reap,
//  because the child plays those bytes -- giving the block away while it is inside the player
//  would be a use-after-free. It must not outlive us either way: it shares this code page.
//  WARNING: the free is explicit rather than left to the kernel, which does free a dyn task's
//  blocks on exit. A task that takes 18 pool pages should say where they go back, and the
//  reap's kill path (a child that would not leave) must not be the thing that decides it.
gkMusicBye:
    lda #1
    sta gkMusQuit
    jsr gkMusStop
    jsr gkMusReap               // wait for the child, and kill it if it will not go
    jmp gkMusBufFree            // and give the 18 pages back

//  gkMusReap -- wait for the child to leave, and kill it if it does not.
//  It shares this code page, so it MUST NOT outlive us: sysExit does not reap thread
//  children (only `kill` cascades), and a child still parked inside the player would be
//  scheduled onto pages the pool has already handed out. The countdown lives in memory
//  because a yield clobbers A and Y (X survives one).
gkMusReap:
    ldx gkMusSlot
    cpx #GK_MUS_NONE
    beq gkMrDone
    lda #GK_MUS_REAP
    sta gkMusCnt
gkMrWait:
    lda taskState,x
    beq gkMrGone                // it left (DEAD), which is what we asked for
    lda #SYS_YIELD
    jsr syscall
    dec gkMusCnt
    bne gkMrWait
    ldx gkMusSlot
    lda #SYS_KILL
    jsr syscall                 // it never woke: force it off our code page
gkMrGone:
    lda #GK_MUS_NONE
    sta gkMusSlot
gkMrDone:
    rts

//  gkMusBufFree -- hand the ONE buffer back. Called by the PARENT, on the quit path only,
//  and only after the child has been reaped: nothing may run while the bytes it plays are
//  being given away.
//  WARNING: A QUIT MUST REACH THIS. The block is 18 pool pages, and the pool would carry
//  them for the rest of the session if the free were skipped -- which is the one thing the
//  buffer-per-tune design got right by accident, and the reason this is explicit rather
//  than left to the kernel's own free on task exit.
//  Clobbers A, X, Y (kFree does).
gkMusBufFree:
    lda gkMusBufLo
    ora gkMusBufHi
    beq gkMfDone                // nothing held
    lda gkMusBufLo
    sta kPtr
    lda gkMusBufHi
    sta kPtr+1
    jsr kFree
    lda #0
    sta gkMusBufLo
    sta gkMusBufHi
    lda #GK_MUS_NONE
    sta gkMusCur                // and no tune is playable
    lda #0
    sta gkMusWait               // a new tune starts with no pause armed
    rts
gkMfDone:
    rts

//  gkMusicThread -- the CHILD. It READS the game's bytes (gkMusOn, gkMusQuit, gkMusWant) and
//  the block gkMusBufLo/Hi point at, and writes gkMusCur; it never touches the game's ZP,
//  because the two share this code page and only one of them runs at a time.
//  Reached ONLY through spawnPtr -- the game itself never falls in here.
//
//  The loop is LOAD -> PLAY, one tune at a time, re-reading the request every time round: a
//  hush means the request has already changed, so going round is what plays the new tune.
//  Nothing is freed here at all -- the buffer is the task's and lives until the quit.
//  The one case where it does not even LOAD is a refusal: the tune is still in the buffer, so
//  the retry is one kSidPlay call (see gkMtBusy). A tune that is not in the FS, or is too big
//  for the buffer, falls back to the attract tune once, so one bad file cannot leave the
//  whole game silent; with the attract tune unusable too the child goes quiet rather than
//  playing bytes that are not a tune, and the next mode change asks again.
gkMusicThread:
    lda gkMusQuit
    bne gkMtQuit                // Rule 4: the two exits sit beside the dispatch, so these stay
    lda gkMusOn                 //   short branches however long the loop below grows
    bne gkMtWork
gkMtIdleGo:
    jmp gkMtIdle                // far: one shared `jmp` rather than a long branch
    //  ---- the game is shutting the child down: there is NOTHING to hand back here. The
    //  buffer is the task's, and gkMusicBye frees it once this child is gone.
gkMtQuit:
    lda #SYS_EXIT
    jsr syscall                 // exit -- never returns
    jmp gkMtQuit                // if it ever did, exit again rather than run the loop
gkMtWork:
    //  ---- the wanted tune may already be in the buffer: a refusal left it there, and a
    //  one-shot that has finished asks for the attract tune explicitly
    lda gkMusCur
    cmp gkMusWant
    beq gkMtPlay
    //  ---- load the wanted tune, falling back to the attract tune once
    ldx gkMusWant
    jsr gkMusLoad
    bcc gkMtLoaded
    lda gkMusWant
    cmp #GK_MUS_ATTRACT
    beq gkMtNoTune
    lda #GK_MUS_ATTRACT         // fall back to it -- the retry cannot loop, because the
    sta gkMusWant               //   request is the attract tune by then
    jmp gkMusicThread
gkMtLoaded:
    //  WARNING: gkMusCur is set HERE and nowhere else, once the bytes really are in the
    //  buffer. Setting it before the load (as the first cut did) makes a FAILED load claim a
    //  tune the buffer does not hold, and the child then plays the previous tune's bytes.
    lda gkMusWant
    sta gkMusCur
    // fall through -- and play it
gkMtPlay:
    //  NO FREE AFTER PLAYING, and that is the change: the next tune is loaded into this same
    //  buffer. Falling out of kSidPlay is still the only moment the bytes are not being read,
    //  which is why every LOAD is here and not in the game.
    lda gkMusBufLo
    sta kPtr
    lda gkMusBufHi
    sta kPtr+1
    lda #GK_MUS_DELAY
    sta kArg                    // ticks slept per jiffy AT 100%
    ldy gkMusCur
    lda gkMusRepeat,y           // 0 = the PLAYER repeats it forever, 1 = play it once
    sta kArg+1
    lda #0                      // no transpose: sid.lib answers the machine's own pitch
    sta kArg+2                  //   shift itself (+1 in this field is one octave DOWN)
    lda #0
    sta kArg+3                  // no semitone offset
    lda #0                      // mode: the image is in RAM at kPtr
    jsr kSidPlay                // blocks: the tune, a hush, or a refusal
    bcs gkMtBusy                // refused: the buffer still holds this tune, ask again later
    //  ---- it played: a hush, or a one-shot reaching its end
    ldy gkMusCur                // reload: the call clobbers A, X and Y
    lda gkMusRepeat,y
    beq gkMtAgain               // it repeats, so only a hush can have returned it
    lda gkMusWant               // a ONE-SHOT: if the request has not changed the tune is
    cmp gkMusCur                //   over, and the attract tune is what follows -- the
    bne gkMtAgain               //   game-over screen still being up is the point
    lda #1
    sta gkMusDone               // the cue gkOverFrame waits on: the tune has been heard
    lda #GK_MUS_ATTRACT
    sta gkMusWant
gkMtAgain:                      // Rule 4: one shared `jmp` back to the dispatch
    jmp gkMusicThread
gkMtNoTune:
    //  ---- no tune in the FS (or none that fits the buffer), not even the attract one
    //  WARNING: quiet, and NOT on a timer -- see the warning above. Nothing is playable, and
    //  nothing is asked for until a mode change asks again.
    lda #GK_MUS_NONE
    sta gkMusCur
    lda #0
    sta gkMusOn                 // the next mode change asks again
    jmp gkMtIdle
gkMtBusy:
    lda #GK_MUS_BUSY            // another task owns the SID: keep the tune, pause, ask again
    sta gkMusWait
    lda #0
    sta gkMusOn
    jmp gkMtIdle
gkMtIdle:
    lda #SYS_YIELD
    jsr syscall                 // never starve the scheduler
    lda gkMusWait
    beq gkMtLook                // no pause armed: look at the request again
    dec gkMusWait
    bne gkMtLook                // still pausing
    lda #1
    sta gkMusOn                 // the pause is over: ask for the chip again
gkMtLook:                       // Rule 4: both ways out of the idle loop share one `jmp`
    jmp gkMusicThread           //   back to the dispatch

//  gkPrintStr -- print a null-terminated screen-code string at a cell.
//  Input: X = column, Y = row, kPtr = the string.
//  kSetCursor is the only kernel call that preserves X and Y, so the position
//  survives into kPrintString, which then advances the cursor through the text.
gkPrintStr:
    jsr kSetCursor
    jmp kPrintString

// ==========================================
//  Tables, glyphs and strings (screen codes)
// ==========================================
//  The control map, walked downward: index 0 is tried first.
gkKeyMap:   .byte GK_K_LEFT, GK_K_RIGHT, GK_K_FIRE, GK_K_PAUSE, GK_K_QUIT
gkKeyBit:   .byte GK_B_LEFT, GK_B_RIGHT, GK_B_FIRE, GK_B_PAUSE, GK_B_QUIT

//  The cell glyphs, eight bytes each, drawn with kPrintGlyphAt at an explicit cell. The
//  brick carries a one-pixel gutter down its right and along its bottom, so
//  neighbouring bricks read as separate and a cleared cell reads as a hole.
//  A brick glyph carries a mortar line through its middle as well: without it a
//  brick is a featureless block, and the frame -- the one glyph with no gutter --
//  is what a featureless block reads as.
gkGlyphWall:    .byte $ff,$ff,$ff,$ff,$ff,$ff,$ff,$ff
//  The damage ladder, one glyph per hit-point value: intact, then a bite out of
//  the top course, then split by a diagonal. A brick therefore reads its OWN
//  state, not just the wall's hue -- two hits left and three hits left read
//  differently.
gkGlyphBrick:   .byte $7f,$7f,$7f,$00,$7f,$7f,$7f,$00
gkGlyphChipped: .byte $3f,$3f,$3f,$00,$7f,$7f,$7f,$00
gkGlyphCracked: .byte $7f,$5f,$6f,$77,$7b,$7d,$7e,$00
gkGlyphNone:    .byte $00,$00,$00,$00,$00,$00,$00,$00
//  The death line: a cell's LAST pixel line and nothing else, so a row of these is a
//  one-pixel line exactly on the raster the ball is lost at (see GK_DEAD_ROW).
gkGlyphDead:    .byte $00,$00,$00,$00,$00,$00,$00,$ff

//  Indexed by a brick's HIT POINTS, 0..3: entry 0 is the erase, and 1-3 are the
//  ladder above, so every standing brick shows its damage.
gkGlyphLo:  .byte <gkGlyphNone, <gkGlyphCracked, <gkGlyphChipped, <gkGlyphBrick
gkGlyphHi:  .byte >gkGlyphNone, >gkGlyphCracked, >gkGlyphChipped, >gkGlyphBrick

//  Indexed by a brick's HIT POINTS * 2 + its CHECKERBOARD parity, so each hit-point
//  group owns a PAIR of hues that alternate along the row and swap over row by row:
//  the wall reads as a chequer of two shades per hit-point level. The pair for hit
//  points 0 is the erase: the blank glyph writes background pixels, so the colour is
//  never seen.
gkHpColour: .byte GK_C_WALL,  GK_C_WALL
            .byte GK_C_HP1A, GK_C_HP1B
            .byte GK_C_HP2A, GK_C_HP2B
            .byte GK_C_HP3A, GK_C_HP3B

//  The ball's colour ramp, walked and wrapped by gkBallColour: the palette's greys from
//  white down to dark grey and back up again, so the pulse turns around at both ends
//  instead of jumping. Its length is GK_BALL_RAMP_N, which the assert below pins -- the
//  index is compared against that constant, not against the table.
gkBallRamp: .byte GK_C_BALL, GK_C_GREY_LIGHT, GK_C_GREY_MEDIUM, GK_C_GREY_DARK
            .byte GK_C_GREY_MEDIUM, GK_C_GREY_LIGHT
.assert "grknoid: the ball ramp is exactly GK_BALL_RAMP_N shades", (* - gkBallRamp) == GK_BALL_RAMP_N ? 1 : 0, 1

//  The score's decimal powers, most significant first, and the points a brick is
//  worth. Both are plain BINARY: see the warning on gkDrawScore.
gkPowLo: .byte $10,$e8,$64,$0a,$01          // 10000, 1000, 100, 10, 1
gkPowHi: .byte $27,$03,$00,$00,$00
gkPtsTab: .byte 10,20,30,40,50              // 10 x the level, levels 1..5

//  The launch ramp's vx, one WORD an entry and one entry a level: level 1 is the GK_VXMIN
//  floor, each level adds GK_VXSTEP, and level 5 is exactly twice level 1. Everything else the
//  launch needs is derived in gkLaunch, because the ANGLE is not a per-level property.
gkVxTab: .word GK_VX0, GK_VX0 + GK_VXSTEP, GK_VX0 + 2*GK_VXSTEP
         .word GK_VX0 + 3*GK_VXSTEP, GK_VXLAST
.assert "grknoid: the launch table covers every level", (* - gkVxTab) == GK_LEVEL_COUNT * 2 ? 1 : 0, 1

//  ---- the game's state. Ordinary bytes in this image, not zero page: they are
//  read and written inside one step of the frame loop, which never yields, so they
//  do not have to survive a context switch -- and every ZP byte the header declares
//  is DMA'd to and from the REU on every switch.
gkLevel:      .byte 1
gkBricksLeft: .byte 0
gkScoreLo:    .byte 0
gkScoreHi:    .byte 0

//  The mode machine and the attract loop (see the constants):
gkMode:      .byte 0             // GK_MODE_*
gkAtStep:    .byte 0             // which attract screen is up
gkTickLo:    .byte 0             // the mode's countdown, LOW byte first so the pair
gkTickHi:    .byte 0             //   decrements as one (gkTick16)
//  The run:
gkLives:     .byte GK_LIVES_START
gkPaused:    .byte 0
gkDrCnt:     .byte 0             // gkDrainKeys' read counter
gkKeyArmed:  .byte 0             // 0 = waiting for the keyboard to go idle (gkKeysArmed)
gkBAnim:     .byte 0             // displayed frames since the ball's last shade change
gkBRamp:     .byte 0             // the ball's shade, an index into gkBallRamp
//  The paddle's acceleration (gkPadAccel): the step it is taking now, how long the held
//  direction has been held, and WHICH direction that counter belongs to (0 = none held).
gkPadStep:   .byte GK_PAD_SPEED_MIN
gkPadHold:   .byte 0
gkPadDir:    .byte 0

//  ---- The hi-score table. Three BCD score bytes then the seven name cells, per entry:
//  ten bytes, which is what GK_HI_ENTRYLEN says. The rows are indexed by that stride, so
//  a length change HERE would quietly corrupt every row after the first -- the assert below
//  is what catches it. The entries start as HOUSE SCORES rather than zeros (001000 /
//  000500 / 000250, every one named `gordon`): a zero table would mean a first game takes
//  a row by default, and the attract screen would have nothing to show.
gkHiTab:
    .byte $00, $10, $00         // 001000
    .text "gordon "             // seven cells, the trailing space included
    .byte $00, $05, $00         // 000500
    .text "gordon "
    .byte $00, $02, $50         // 000250
    .text "gordon "
.assert "grknoid: the hi-score table is exactly GK_HI_LEN bytes", (* - gkHiTab) == GK_HI_LEN ? 1 : 0, 1
gkHiEntry:   .byte 0             // which entry the table routines are on
gkHiSlot:    .byte 0             // the row a new score took (gkHiInsert)
gkHiLen:     .byte 0             // characters typed into the name so far
gkHiIdx:     .byte 0             // a print loop's counter (never Y: kPrintChar)
gkHiTmp:     .byte 0             // gkHiMul10's scratch
gkScoreBCD:  .fill GK_HI_SCORELEN, 0   // the run's score as the table stores it
gkHiNameBuf: .fill GK_HI_NAMELEN, 0    // the name being typed
gkHiRowBuf:  .fill GK_HI_ENTRYLEN, 0   // one entry, copied out before printing

//  ---- the music, shared with the thread child: it runs THIS code page, so these bytes
//  are the whole of the interface. The child READS gkMusOn/gkMusQuit/gkMusWant and the
//  block gkMusBufLo/Hi point at, and writes gkMusCur; the rest is the game's.
gkMusOn:     .byte 0             // 1 = the game wants music
gkMusQuit:   .byte 0             // 1 = leave the player loop for good
gkMusSlot:   .byte GK_MUS_NONE   // the child's slot, $ff = none
gkMusWant:   .byte GK_MUS_NONE   // the tune the mode wants ($ff = none asked for yet)
gkMusCur:    .byte GK_MUS_NONE   // the tune IN gkMusBufLo/Hi, $ff = the buffer holds nothing
gkMusWait:   .byte 0             // the child's pause before it asks for the SID again
gkMusDone:   .byte 0             // the child has PLAYED the game-over tune through
gkMusCnt:    .byte 0             // gkMusReap's countdown (the game's, not the child's)
gkMusNLo:    .byte 0             // gkMusLoad: the FS name pointer
gkMusNHi:    .byte 0
gkMusBufLo:  .byte 0             // gkMusInit out: the task's ONE tune buffer, allocated once
gkMusBufHi:  .byte 0             //   and freed by gkMusBufFree on the way out

//  ---- the drawing and collision scratch, same reasoning. gkGLO/gkGHI hold a glyph
//  pointer between cells; gkBallCellC/R is the ball's own cell, gkSCR2/gkSCR3 its
//  centre cell, and gkFlipX/gkFlipY say which components a brick hit reverses.
gkGLO:       .byte 0
gkGHI:       .byte 0
gkBCELL:     .byte 0
gkBROW:      .byte 0
gkBCNT:      .byte 0
gkSCR0:      .byte 0
gkSCR1:      .byte 0
gkSCR2:      .byte 0
gkSCR3:      .byte 0
gkDIG:       .byte 0
gkBHP:       .byte 0
gkPAR:       .byte 0                     // the brick's tile parity (column ^ row), for its hue
gkOffX:      .byte 0
gkOffY:      .byte 0
gkBallCellC: .byte 0
gkBallCellR: .byte 0
gkCandIx:    .byte 0
gkFlipX:     .byte 0
gkFlipY:     .byte 0

//  ---- THE AUTHORITY. 0 = a gap, 1..3 = the hits left to break the brick. The
//  drawn cells are presentation; nothing ever reads the surface back.
gkBoard: .fill GK_BRICKS, 0

// ==========================================
//  The levels
// ==========================================
//  One NIBBLE per brick, two bricks a byte, the left brick low. Eight bricks a row
//  and four cells each make a row exactly four bytes, so each level below is six
//  source lines that ARE the wall: $11 is two one-hit bricks, $23 would be a
//  two-hit brick followed by a three-hit one.
gkLevels:
//  ---- 1: every brick one hit
        .byte $11,$11,$11,$11
        .byte $11,$11,$11,$11
        .byte $11,$11,$11,$11
        .byte $11,$11,$11,$11
        .byte $11,$11,$11,$11
        .byte $11,$11,$11,$11
//  ---- 2: a two-hit top row
        .byte $22,$22,$22,$22
        .byte $11,$11,$11,$11
        .byte $11,$11,$11,$11
        .byte $11,$11,$11,$11
        .byte $11,$11,$11,$11
        .byte $11,$11,$11,$11
//  ---- 3: a chequerboard, which needs the rows to alternate because a row has an
//  even number of bricks
        .byte $21,$21,$21,$21
        .byte $12,$12,$12,$12
        .byte $21,$21,$21,$21
        .byte $12,$12,$12,$12
        .byte $21,$21,$21,$21
        .byte $12,$12,$12,$12
//  ---- 4: a gradient, softest at the bottom
        .byte $33,$33,$33,$33
        .byte $33,$33,$33,$33
        .byte $22,$22,$22,$22
        .byte $22,$22,$22,$22
        .byte $11,$11,$11,$11
        .byte $11,$11,$11,$11
//  ---- 5: hard corners round a soft middle
        .byte $11,$11,$11,$11
        .byte $33,$11,$11,$33
        .byte $11,$33,$33,$11
        .byte $11,$33,$33,$11
        .byte $33,$11,$11,$33
        .byte $11,$11,$11,$11

gkSPadName: .text "gkpad.spr";       .byte 0
gkSBallName:.text "gkball.spr";      .byte 0

//  The three tunes, in GK_MUS_* order, and the two tables the child walks: their FS
//  names, and kSidPlay's repeats byte per tune (0 = the PLAYER repeats it forever, 1 =
//  play it once -- which is the game-over tune, and the only one-shot of the three).
//  A base name stays <= 7 characters so the ".mus" in the FS entry keeps the whole name
//  inside the REU FS's 11-character limit.
gkMusName0:  .text "katmand.mus";    .byte 0
gkMusName1:  .text "immig.mus";      .byte 0
gkMusName2:  .text "firstdt.mus";    .byte 0
gkMusNameLo: .byte <gkMusName0, <gkMusName1, <gkMusName2
gkMusNameHi: .byte >gkMusName0, >gkMusName1, >gkMusName2
gkMusRepeat: .byte 0, 0, 1
gkSScore:   .text "score";          .byte 0
gkSBricks:  .text "bricks";         .byte 0
gkSLives:   .text "lives";          .byte 0
gkSLevel:   .text "lev";            .byte 0

//  Attract screen 0: the title, and the only cue a player needs to start.
gkSTitle:    .text "gorkanoid";             .byte 0
gkSTitleSub: .text "an arkanoid for gordonOS"; .byte 0
gkSPressAny: .text "press any key";         .byte 0
gkSTitleHint:.text "to start a game";       .byte 0

//  Attract screen 1: the CONTROLS, one row each so the columns line up. They
//  belong here and not on the play surface, which carries the wall, the HUD and
//  the sprites. 'q' is listed because a task must always be able to end -- see
//  docs/writing-relocatable-tasks.md.
gkSCtrlTitle:.text "controls";       .byte 0
gkSCtrl1:   .text "j k    move";     .byte 0
gkSCtrl2:   .text "space  fire";     .byte 0
gkSCtrl3:   .text "p      pause";    .byte 0
gkSCtrl4:   .text "q      quit";     .byte 0

//  Attract screen 2: credits. The only borrowed thing in this game is the music -- the
//  three tunes are the Compute's Gazette Sid Collection's (CGSC), the archive of songs
//  written for the very Sidplayer this repository ports. See NOTICE.
gkSCredMus: .text "music: CGSC archives";    .byte 0
gkSCred3:   .text "gordonOS 2026";           .byte 0

//  The run's own messages. gkSBlank6 is exactly as long as gkSPaused, because it
//  is what takes the banner back down.
gkSPaused:  .text "paused";          .byte 0
gkSBlank6:  .text "      ";          .byte 0
gkSGameOver:.text "game over";       .byte 0
gkSPressKey:.text "press key";       .byte 0

//  The high-score screen: the heading, the line that answers a new entry, and the FS
//  name the table is kept under (11 characters is the FS limit).
gkSHiTitle: .text "high scores";               .byte 0
gkSHiNew:   .text "new high score - entr done"; .byte 0
gkHiFileName: .text "grknoid.hi";              .byte 0

grknoidEnd:
