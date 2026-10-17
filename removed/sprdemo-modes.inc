// ==========================================
//  GordonOS -- sprdemo: the Sprite Lab
//  Skeleton + mode 0 (raw) + mode 6 (shadow inspector)
// ==========================================
//  Why this task exists: "does the sprite subsystem work?" must be answered by
//  RUNNING ONE PROGRAM AND LOOKING AT IT, not by reasoning about it. Each mode
//  is one test.
//
//  Mode 0 matters more than it looks: it writes a 64-byte pattern into slot 0
//  with no REU, no assets and no sheet, so if it works then the slots, the
//  $07F8 pointer page, the raster commit and the setters are all fine -- and a
//  later failure is in the sheet/DMA path.
//
//  Every loop iteration yields (Rule 13): the demo must never starve the
//  scheduler, or the shell would be unusable while it runs -- and the tear
//  procedure needs the shell.

//  Arena bounds for mode 5. The x axis is NINE bits on purpose: the right bound
//  is 296, which needs this sprite's $D010 high bit, so the mode exercises
//  kSpritePos's carry-in as well as the bounce. (A sprite at x=24 is the left
//  edge of the 40-column text area, so 16..296 is fully on screen.)
.const SPRD_XMIN    = 16
.const SPRD_XMAX_LO = 40     // 296 = 256 + 40 -- high bit set
.const SPRD_YMIN    = 60
.const SPRD_YMAX    = 200

//  Mode 9's arena is the WHOLE screen. x is nine bits (0..296 = the right edge
//  minus the ball's 24 px), so the balls cross the $D010 boundary constantly and
//  kSpritePos's carry-in is exercised in continuous motion as well as in mode 5.
//  y's integer byte is 50..229: the visible window opens at raster line 51 and a
//  ball is 21 lines tall, so 229 is the last y that keeps it fully on screen.
.const SPRD_BALLS  = 8
.const SPRD_BXMAX  = 296
.const SPRD_BYMIN  = 50
.const SPRD_BYMAX  = 229
//  Ball-ball overlap half-extents. Deliberately SMALLER than the sprite (24x21)
//  so a bounce needs a visible overlap rather than a corner graze.
.const SPRD_HITX   = 16
.const SPRD_HITY   = 14
.const SPRD_HITXLO = 256 - SPRD_HITX    // dx low byte of -SPRD_HITX

sprdemo:
    //  Take a surface AND the display. The sprite API is focus-owner-only, so
    //  nothing would be accepted until kSwitchView has made us the owner.
    lda #0
    ldx #$0b; stx kArg           // border: dark grey
    ldx #0;   stx kArg+1         // bg0: BLACK
    stx kArg+2; stx kArg+3
    //  WARNING: Both colours are set EXPLICITLY, because a colour collision is
    //  the failure mode: a white sprite (colour 1) on a white background is
    //  drawn perfectly and is completely invisible -- the worst kind of bug,
    //  since every register looks right. Text has the same trap, so the text
    //  colour is set here too rather than left to the task default.
    jsr kAllocScreen
    bcc sprdScreen
    lda #SYS_YIELD; jsr syscall  // no screens: idle forever
    jmp *-3
sprdScreen:
    lda #1                       // text: white, so it reads on black bg0
    jsr kSetTextColor
    jsr kDisableCursor           // graphics surface, no cursor
    lda #SYS_GETTID
    jsr syscall
    tax
    jsr kSwitchView              // display it + take kbFocus
    lda #9
    sta SPRD_MODE
    jsr sprdShow                 // start in mode 9 -- the balls demo
sprdLoop:
    jsr kReadKey                 // non-blocking: A = screen code, 0 = none
    beq sprdStep
    cmp #'q'
    beq sprdExit
    sec
    sbc #'0'                     // mode number?
    bcc sprdStep                 // below '0' -- ignore
    cmp #10
    bcs sprdStep                 // above '9' -- ignore
    sta SPRD_MODE
    jsr sprdShow
sprdStep:
    jsr sprdTick                 // per-mode animation step
    lda #SYS_YIELD
    jsr syscall                  // never starve the scheduler
    jmp sprdLoop
sprdExit:
    lda #SYS_EXIT
    jsr syscall

// ==========================================
//  sprdShow -- draw the selected mode
//  Modes 1-5, 7 and 8 arrive in 4b-4d; until then they say so rather than
//  showing a blank screen, so "nothing happened" is never ambiguous.
// ==========================================
sprdShow:
    jsr kClearScreen
    lda SPRD_MODE
    cmp #0
    beq sprdMode0
    cmp #2
    bne sprdNotM2
    jmp sprdMode2                // out of branch range: the dispatch grew when
sprdNotM2:                       // mode 9 joined it (Rule 4)
    cmp #3
    bne sprdNotM3
    jmp sprdMode3                // out of branch range: the modes below grow
sprdNotM3:
    cmp #4
    bne sprdNotM4
    jmp sprdMode4
sprdNotM4:
    cmp #5
    bne sprdNotM5
    jmp sprdMode5
sprdNotM5:
    cmp #6
    bne sprdNotM6
    jmp sprdMode6
sprdNotM6:
    cmp #1
    bne sprdNotM1
    jmp sprdMode1                // modes 1 and 8 share the orbit code
sprdNotM1:
    cmp #7
    bne sprdNotM7
    jmp sprdMode7
sprdNotM7:
    cmp #8
    bne sprdNotM8
    jmp sprdMode1
sprdNotM8:
    cmp #9
    bne sprdModeTodo
    jmp sprdMode9                // the balls demo -- and the entry mode
sprdModeTodo:                    // unreachable from the keys -- 0-9 are all
    lda #<sprTodo                // built. A net for a corrupted SPRD_MODE only.
    ldx #>sprTodo
    jsr sprdP
    jsr kNewline
    rts

// ==========================================
//  Mode 0 -- raw: no REU, no assets, no sheet
// ==========================================
//  The whole sprite path with the least machinery: hand-copy 64 bytes into the
//  slot, then place and enable. The $07F8 pointer and every VIC sprite register
//  are the raster commit's job, so this exercises slots, the pointer page, the
//  commit and the setters -- and nothing else.
sprdMode0:
    lda #<sprMsgM0
    ldx #>sprMsgM0
    jsr sprdP
    jsr kNewline
    //  ONE slot, so this is the smallest thing that can possibly work: 64 bytes
    //  of pattern into slot 0 at $02C0, then place and enable. Slot 5 is NOT
    //  filled with the same bytes: that A/B test of the VIC bank 0 char ROM
    //  mirror renders ROM bytes, which is why the demo uses five slots and not
    //  eight.
    ldx #0
sprdM0Copy:
    lda sprdPattern,x
    sta SPR_SLOT0,x              // slot 0: $02C0, the lowest slot
    inx
    cpx #64
    bcc sprdM0Copy
    lda #20
    sta SPRD_X                   // sweep start (sprdTick moves it)
    lda #0
    sta SPRD_TICK
    ldx #0
    lda SPRD_X
    ldy #60                      // y >= 50, or the top rows are off screen (section 4.4)
    clc
    jsr kSpritePos
    sprColour(0, 7)              // yellow on the black bg0 set above
    sprOn(%00000001)             // sprite 0 only
    lda #<sprMsgM0b
    ldx #>sprMsgM0b
    jsr sprdP
    jsr kNewline
    rts

// ==========================================
//  Mode 2 -- anim: two sheets at once, one per slot group
// ==========================================
//  Sprite 0 cycles all 8 frames of ball.spr. Sprites 1-7 each hold ONE frame of
//  ship.spr -- frames 0 to 6 -- and ship.spr is a measuring stick (frame N is
//  3*(N+1) columns wide), so the seven of them must read as a staircase. That is
//  the whole test: a wrong linear frame offset (frame N at N*64) or a
//  mixed-up per-slot sheet record shows up as a wrong width, and a slot bleeding
//  into its neighbour shows up as a step that is not a step.
//
//  SEVEN steps: the sheet has eight frames, so the staircase runs 0-6 and frame
//  7 (the full-width 24-column sprite) stays unused -- it would leave no room
//  for the others.
//
//  They are stacked in a COLUMN, all at x=20, because the widths grow to 168 px:
//  a row would run off the right of the screen by the fifth step. A column makes
//  the growth the point of the picture.
//
//  Two different animations from two different sheets need one sheet record per
//  slot, resolved once, never per frame.
sprdMode2:
    lda #0
    sta SPRD_CNT                 // sheet rejections
    sta SPRD_FAIL                // frame-load rejections
    lda #<sprMsgM2
    ldx #>sprMsgM2
    jsr sprdP
    jsr kNewline
    //  WARNING: ONE SHEET RECORD PER SLOT. Sprite N fetches its pixels
    //  from slot N, and the record saying where that slot's art lives is
    //  indexed by slot -- so resolving "ship.spr" into slot 1 gives sprites 2-3
    //  NO art at all: their kSpriteFrame calls return C=1, their slots keep
    //  whatever RAM happened to hold, and the screen shows garbage pixels that
    //  look like a broken sprite. Three sprites showing three different frames
    //  need three records, so this resolves the same sheet three times --
    //  ~1,000-3,000 cycles each, SEI'd, which is exactly why "one global record
    //  + re-resolve per frame" was rejected in section 3.2.
    //  A task that wants many sprites on ONE animation should instead point them
    //  all at one slot (the VIC happily reads one block for several sprites) and
    //  pay for one record.
    //  kPtr survives kSpriteSheet, so the name is set once per sheet, not per
    //  call.
    lda #<sprBallName
    sta kPtr
    lda #>sprBallName
    sta kPtr+1
    lda #0                       // A = slot
    jsr kSpriteSheet
    bcc sprdM2BallOk
    inc SPRD_CNT
sprdM2BallOk:
    lda #<sprShipName
    sta kPtr
    lda #>sprShipName
    sta kPtr+1
    lda #1
    sta SPRD_IDX
sprdM2Sheet:
    lda SPRD_IDX                 // A = slot 1..SPRITE_TASK_SLOTS-1
    jsr kSpriteSheet
    bcc sprdM2SheetOk
    inc SPRD_CNT
sprdM2SheetOk:
    inc SPRD_IDX
    lda SPRD_IDX
    cmp #SPRITE_TASK_SLOTS
    bcc sprdM2Sheet
    //  Sprites 1-3: frames 0-2 of the ship sheet, one load each. Rethink the
    //  index at every step: kSpriteFrame clobbers A, X and Y.
    lda #0
    sta SPRD_IDX
sprdM2Hold:
    ldx SPRD_IDX                 // X = frame f
    txa
    clc
    adc #1                       // A = slot f+1
    jsr kSpriteFrame
    bcc sprdM2HoldOk
    inc SPRD_FAIL
sprdM2HoldOk:
    inc SPRD_IDX
    lda SPRD_IDX
    cmp #SPRITE_TASK_SLOTS-1
    bcc sprdM2Hold
    //  Place the ball top-right, then the staircase as a column down the left:
    //  frame N is 3*(N+1) columns wide, so the seven steps grow 24 -> 168 px.
    //  WARNING: y must be >= 50. A sprite's first line appears at raster line Y+1 and
    //  the visible window starts at line 51 on PAL, so anything smaller loses
    //  its top rows off the top of the screen (plan section 4.4). The ball sat at
    //  y=40 and had roughly half of itself cut away.
    ldx #0
    lda #240
    ldy #60
    clc
    jsr kSpritePos
    sprColour(0, 7)              // ball: yellow
    //  WARNING: kSpritePos takes the sprite in X and clobbers A, X and Y -- all three
    //  are its inputs -- so the loop index lives in ZP and is reloaded on every
    //  iteration. Holding it in X silently dropped five of these six calls.
    lda #1
    sta SPRD_IDX
    lda #60
    sta SPRD_X                   // y accumulator: 60 + i*26 keeps the last
sprdM2Row:                       // sprite (216) clear of the bottom (237 max)
    ldx SPRD_IDX
    lda #20                      // all seven share one left edge
    ldy SPRD_X
    clc                          // x stays under 256
    jsr kSpritePos
    lda SPRD_X
    clc
    adc #26
    sta SPRD_X
    inc SPRD_IDX
    lda SPRD_IDX
    cmp #SPRITE_TASK_SLOTS
    bcc sprdM2Row
    //  Colours 1-7 in one loop: kSpriteReg preserves X (its guard uses cpy and
    //  it clobbers only A and Y), so the shadow offset can live in X.
    ldx #SPR_SHD_COLOUR + 1
    lda #3                       // cyan
sprdM2Col:
    jsr kSpriteReg
    inx
    cpx #SPR_SHD_COLOUR + SPRITE_TASK_SLOTS
    bcc sprdM2Col
    //  Count the rejections rather than hoping: a refused kSpriteSheet or
    //  kSpriteFrame is invisible on screen (the slot keeps its old RAM, which
    //  reads as "garbage sprite"), so the mode reports its own failures.
    lda #<sprMsgFail
    ldx #>sprMsgFail
    jsr sprdP
    lda SPRD_CNT
    jsr sprdHex
    lda #$20
    jsr kPrintChar
    lda SPRD_FAIL
    jsr sprdHex
    jsr kNewline
    sprOn(%11111111)             // all eight slots -- see the note in mode 2
    rts

// ==========================================
//  Mode 3 -- flags: one sheet, four interpretations
// ==========================================
//  blob.spr is MULTICOLOUR art, and $D01C decides how the VIC reads it: the same
//  64 bytes are 24 one-bit pixels or 12 double-wide two-bit ones.
//  SEVEN sprites show every combination side by side:
//    0 hires - 1 hires+X-exp - 2 hires+Y-exp - 3 both expans
//    4 MC    - 5 MC+X-exp    - 6 MC+Y-exp    - 7 MC + both expans
//  A wrong bit in $D01C/$D01D/$D017 is visibly wrong next to seven correct
//  sprites, and the hires ones are the reference the MC ones differ from.
//
//  The four frames differ only in the size of the colour-3 core, and the tick
//  cycles them on ALL slots together: with one sheet per slot, a per-slot sheet
//  record is what makes that legal.
//
//  The band is 34 px apart: X-expanded sprites are 48 px wide, the rest 24, so
//  the widest run ends at x=238 and nothing needs the $D010 high bit.
sprdMode3:
    lda #<sprMsgM3
    ldx #>sprMsgM3
    jsr sprdP
    jsr kNewline
    lda #0
    sta SPRD_CNT                 // sheet rejections
    sta SPRD_FAIL                // frame-load rejections
    sta SPRD_IDX                 // current frame (the tick advances it)
    //  kPtr survives kSpriteSheet, so the name is set once for all four slots.
    lda #<sprBlobName
    sta kPtr
    lda #>sprBlobName
    sta kPtr+1
    lda #0
    sta SPRD_SLOT
sprdM3Sheet:
    lda SPRD_SLOT                // A = slot 0..SPRITE_TASK_SLOTS-1
    jsr kSpriteSheet
    bcc sprdM3SheetOk
    inc SPRD_CNT
sprdM3SheetOk:
    inc SPRD_SLOT
    lda SPRD_SLOT
    cmp #SPRITE_TASK_SLOTS
    bcc sprdM3Sheet
    //  frame 0 into every slot, so the still frame is complete on entry
    lda #0
    sta SPRD_SLOT
sprdM3Frame:
    ldx #0                       // X = frame 0
    lda SPRD_SLOT                // A = slot
    jsr kSpriteFrame
    bcc sprdM3FrameOk
    inc SPRD_FAIL
sprdM3FrameOk:
    inc SPRD_SLOT
    lda SPRD_SLOT
    cmp #SPRITE_TASK_SLOTS
    bcc sprdM3Frame
    //  WARNING: kSpritePos clobbers A, X and Y -- they are its inputs -- so nothing here
    //  can hold a value in a register across a call.
    //  WARNING: kSpritePos clobbers A, X and Y -- they are its inputs -- so the loop
    //  index lives in ZP and is reloaded each iteration. Eight sprites in TWO
    //  ROWS of four: the X-expanded ones are 48 px wide, so the columns are 70
    //  apart and the widest run ends at x=278; the Y-expanded ones are 42 lines
    //  from y=150, ending at line 192.
    lda #0
    sta SPRD_IDX
sprdM3Place:
    ldx SPRD_IDX
    ldy sprdM3Y,x                // Y = y
    lda sprdM3X,x                // A = x lo (nothing here needs the $D010 bit)
    clc
    jsr kSpritePos
    inc SPRD_IDX
    lda SPRD_IDX
    cmp #SPRITE_TASK_SLOTS
    bcc sprdM3Place
    //  Per-sprite colour: 1 = the sprite's own colour, 2 = $D025, 3 = $D026, so
    //  the blob shows the whole palette. kSpriteReg preserves X (its guard uses
    //  cpy), which is what lets the shadow offset live in it.
    ldx #SPR_SHD_COLOUR
    lda #7                       // yellow -- the '1' pixels
sprdM3Col:
    jsr kSpriteReg
    inx
    cpx #SPR_SHD_COLOUR + SPRITE_TASK_SLOTS
    bcc sprdM3Col
    sprMcColour(0, 3)            // $D025 = cyan  (the '2' pixels, all sprites)
    sprMcColour(1, 10)           // $D026 = light red (the '3' pixels)
    sprMode(%11110000)           // sprites 4-7 read the block as 2-bit pixels
    sprXExpand(%10101010)        // sprites 1, 3, 5 and 7: double width
    sprYExpand(%11001100)        // sprites 2, 3, 6 and 7: double height
    sprPriority(%00000000)       // all in front; mode 4 is the priority test
    sprOn(%11111111)             // every slot
    lda #<sprMsgFail
    ldx #>sprMsgFail
    jsr sprdP
    lda SPRD_CNT
    jsr sprdHex
    lda #$20
    jsr kPrintChar
    lda SPRD_FAIL
    jsr sprdHex
    jsr kNewline
    rts

// ==========================================
//  Mode 4 -- prio: the priority bit against a printed wall
// ==========================================
//  $D01B decides whether a sprite is drawn IN FRONT of the foreground pixels or
//  BEHIND them. "Foreground" is any pixel that is not bg0 -- printed text
//  included, because a glyph blit sets the glyph's pixels and their matrix
//  nibble. So the wall is 8 rows of solid cells (screen code $A0, the filled
//  block in the OS font), drawn at col 8: x 64-255, raster lines 115-179.
//
//  Both sprites sit at x = 56, so each straddles the wall's left edge with its
//  first 8 pixels: sprite 0 (priority 0) draws whole, sprite 1 (priority 1) is
//  cut off at x = 64 -- the same art, two outcomes, one glance.
//
//  Both still report $D01F (read out at row 4): priority decides what is DRAWN,
//  not what COLLIDES (section 5.4), and that is the part worth seeing.
sprdMode4:
    lda #<sprMsgM4
    ldx #>sprMsgM4
    jsr sprdP
    jsr kNewline
    lda #<sprMsgM4b
    ldx #>sprMsgM4b
    jsr sprdP
    jsr kNewline
    lda #0
    sta SPRD_CNT                 // sheet rejections
    sta SPRD_FAIL                // frame-load rejections
    sta SPRD_TICK                // so the readout starts on a known tick
    //  wall: kSetCursor preserves X and Y, so the row counter lives in ZP and
    //  the string pointer can be reloaded per row (kPrintString clobbers both).
    lda #8
    sta SPRD_IDX
sprdM4Wall:
    ldx #8                       // col 8
    ldy SPRD_IDX                 // row
    jsr kSetCursor
    lda #<sprdWall24
    ldx #>sprdWall24
    jsr sprdP
    inc SPRD_IDX
    lda SPRD_IDX
    cmp #16
    bcc sprdM4Wall
    //  sheets + frame 0 for slots 0 and 1. kSpriteFrame clobbers kPtr, and
    //  kSpriteSheet reads it, so the name is set once per resolve.
    lda #<sprBallName
    sta kPtr
    lda #>sprBallName
    sta kPtr+1
    lda #0
    jsr kSpriteSheet
    bcc sprdM4S1
    inc SPRD_CNT
sprdM4S1:
    lda #<sprBallName
    sta kPtr
    lda #>sprBallName
    sta kPtr+1
    lda #1
    jsr kSpriteSheet
    bcc sprdM4F0
    inc SPRD_CNT
sprdM4F0:
    ldx #0
    lda #0
    jsr kSpriteFrame
    bcc sprdM4F1
    inc SPRD_FAIL
sprdM4F1:
    lda #1
    ldx #0
    jsr kSpriteFrame
    bcc sprdM4Pos
    inc SPRD_FAIL
sprdM4Pos:
    ldx #0
    lda #56
    ldy #120
    clc
    jsr kSpritePos
    ldx #1
    lda #56
    ldy #150
    clc
    jsr kSpritePos
    sprColour(0, 7)              // yellow -- in front
    sprColour(1, 3)              // cyan -- behind the wall
    sprPriority(%00000010)       // $D01B bit 1: sprite 1 is drawn behind
    sprOn(%00000011)
    rts

// ==========================================
//  Mode 5 -- arena: bouncing sprites and the collision latch
// ==========================================
//  Two sprites bounce inside a box that has one **drawn** obstacle across it, so
//  both collision sources fire: $D01E when they meet each other, $D01F when
//  either crosses the obstacle's pixels.
//
//  The poll is deliberately SLOW -- every 8th tick, i.e. ~7 Hz against a 60 Hz
//  service -- because that is the property worth demonstrating: the service
//  latches $D01E/$D01F every frame and OR-accumulates into `sprCollSs`/
//  `sprCollBg`, so a task polling at any rate sees every collision (section 5.4).
//
//  The x range is 16..296 -- deliberately PAST 255 -- so every pass exercises
//  kSpritePos's $D010 carry-in as well as the bounce: each sprite crosses the
//  255 boundary twice per traversal and must not wrap when it does.
//
//  The evidence is two event rows: row 22 fills with '*' for sprite-sprite and
//  row 23 with '+' for sprite-background. A missed collision shows up as a
//  short row, not as an invisible failure.
sprdMode5:
    lda #<sprMsgM5
    ldx #>sprMsgM5
    jsr sprdP
    jsr kNewline
    lda #<sprMsgM5b
    ldx #>sprMsgM5b
    jsr sprdP
    jsr kNewline
    lda #2
    sta sprdLogSs                // event-log columns, back to the left edge
    sta sprdLogBg
    lda #0
    sta SPRD_TICK
    //  The arena tables are NOT reset here: they hold live state, so leaving the
    //  mode and coming back resumes the flight. A task re-run reloads a fresh
    //  copy from the REU and initialises them (KF_RE_RUNNABLE, not reentrant).
    //  obstacle: 3 rows of 16 solid cells at col 12 (raster y ~= 131-155)
    lda #10
    sta SPRD_IDX
sprdM5Wall:
    ldx #12                      // col 12
    ldy SPRD_IDX                 // rows 10-12
    jsr kSetCursor
    lda #<sprdWall16
    ldx #>sprdWall16
    jsr sprdP
    inc SPRD_IDX
    lda SPRD_IDX
    cmp #13
    bcc sprdM5Wall
    //  ball.spr on slots 0-1, frame 0
    lda #<sprBallName
    sta kPtr
    lda #>sprBallName
    sta kPtr+1
    lda #0
    jsr kSpriteSheet
    bcc sprdM5S1
    inc SPRD_CNT
sprdM5S1:
    lda #<sprBallName
    sta kPtr
    lda #>sprBallName
    sta kPtr+1
    lda #1
    jsr kSpriteSheet
    ldx #0
    lda #0
    jsr kSpriteFrame
    bcc sprdM5F1
    inc SPRD_FAIL
sprdM5F1:
    lda #1
    ldx #0
    jsr kSpriteFrame
    //  place both from the tables
    lda #0
    sta SPRD_SLOT
sprdM5Place:
    jsr sprdMvPos                // place sprite SPRD_SLOT (no move)
    inc SPRD_SLOT
    lda SPRD_SLOT
    cmp #2
    bcc sprdM5Place
    sprColour(0, 7)
    sprColour(1, 3)
    sprPriority(%00000000)       // in front of the obstacle -- this mode is
    sprOn(%00000011)             // about collisions, not layering
    rts

// ==========================================
//  Mode 9 -- balls: eight coloured balls bouncing off the walls and off each
//  other, forever (q exits; kill from the shell works like any task)
// ==========================================
//  A DEMO rather than a test vector, and the mode the task starts in. It needs
//  per-ball state a sprite mode normally does not have:
//
//    x  (fraction, integer)  -- 9 bits of integer, 0..296, so the balls cross the
//                              $D010 boundary constantly
//    y  (fraction, integer)  -- integer byte 50..229 (the visible window)
//    vx, vy (16-bit SIGNED, 1/256 px per tick)
//
//  8.8 FIXED POINT, NOT fp.lib. Position is integrated with a 16-bit `adc`
//  chain, which is what makes "some fast, some slow" free: one speed table spans
//  0.31 to 1.88 px/tick and the same add integrates all of them. A wall bounce
//  is a two's-complement negation of one component. Nothing here multiplies,
//  divides or takes a square root, so a float library would add 8 pool pages and
//  ~200 cycles per operation to do worse than an integer add -- and this task's
//  libMask is deliberately 0 (it proves the sprite API from a bare task).
//
//  WARNING: COLLISION IS SOFTWARE, NOT $D01E. The VIC's sprite collision is a
//  rectangle test on the whole 24x21 box, so two balls can "collide" with no
//  visible pixel touching -- which reads as a bug in a demo. The 28 pairs are
//  tested here against a slightly SMALLER box (16x14 half-extents), so a bounce
//  needs a real overlap. Response: EXCHANGE the two velocity vectors -- the
//  equal-mass elastic answer. It is cheap, conserves momentum exactly, and can
//  never lock two balls together, because exchanging the velocities negates
//  their relative velocity by construction.
//
//  WARNING: ONE frame load per ball, at entry, and none afterwards. A frame load
//  rewrites a slot while the VIC is fetching it, which can split that sprite for
//  a single frame (~8% per load). At one load per 16 ticks
//  that is invisible (mode 2), but eight balls reloading on a schedule would put
//  seams on screen several times a second. The balls hold one frame each.
sprdMode9:
    //  ball.spr into all eight slots -- one sheet record PER SLOT,
    //  ~1,000-3,000 cycles each and SEI'd: eight of them at entry. That is the
    //  API's rule, and it is paid once.
    lda #<sprBallName
    sta kPtr
    lda #>sprBallName
    sta kPtr+1
    lda #0
    sta SPRD_SLOT
sprdM9Sheet:
    lda SPRD_SLOT
    jsr kSpriteSheet
    inc SPRD_SLOT
    lda SPRD_SLOT
    cmp #SPRD_BALLS
    bcc sprdM9Sheet
    lda #0
    sta SPRD_SLOT
sprdM9Frame:
    ldx #0                       // X = frame 0
    lda SPRD_SLOT
    jsr kSpriteFrame
    inc SPRD_SLOT
    lda SPRD_SLOT
    cmp #SPRD_BALLS
    bcc sprdM9Frame
    //  Per ball: random position, an independent random speed class and sign
    //  for each axis, and its own colour. kRandom clobbers A only, so the loop
    //  index can stay in ZP and X can carry the ball index across the calls.
    lda #0
    sta SPRD_BI
sprdM9Init:
    jsr kRandom
    ldx SPRD_BI
    sta sprdBXHi,x               // x = 0..255 -- the INTEGER byte, so balls are
    lda #0                       //     scattered across the screen on entry
    sta sprdBXLo,x               // fraction 0 (they pick one up as they move)
    jsr kRandom                  //     and never start past the right wall
    and #$7f
    clc
    adc #SPRD_BYMIN              // y = 50..177; the rest is reached by bouncing
    sta sprdBYHi,x
    lda #0
    sta sprdBYLo,x
    //  vx: magnitude from the speed table, sign from one more random bit
    jsr kRandom
    pha                          // keep the sign bit across the table lookup
    and #3
    tay
    lda sprdSpdLo,y
    sta sprdBVXLo,x
    lda sprdSpdHi,y
    sta sprdBVXHi,x
    pla
    and #4
    beq sprdM9VxPos
    jsr sprdBNegVX               // X = ball index
sprdM9VxPos:
    //  vy: an INDEPENDENT class, so the slopes vary instead of every ball
    //  running at 45 degrees
    jsr kRandom
    pha
    and #3
    tay
    lda sprdSpdLo,y
    sta sprdBVYLo,x
    lda sprdSpdHi,y
    sta sprdBVYHi,x
    pla
    and #4
    beq sprdM9VyPos
    jsr sprdBNegVY
sprdM9VyPos:
    //  colour: one palette entry per ball, so all eight look different.
    //  kSpriteReg takes the value in A and the shadow offset in X.
    ldy SPRD_BI
    lda sprdPal,y
    pha
    tya
    clc
    adc #SPR_SHD_COLOUR
    tax
    pla
    jsr kSpriteReg
    inc SPRD_BI
    lda SPRD_BI
    cmp #SPRD_BALLS
    bcc sprdM9Init
    //  place them where the random numbers put them, and light all eight
    lda #0
    sta SPRD_BI
sprdM9Place:
    ldx SPRD_BI
    jsr sprdBPut
    inc SPRD_BI
    lda SPRD_BI
    cmp #SPRD_BALLS
    bcc sprdM9Place
    sprPriority(%00000000)       // in front of the text they cross
    sprOn(%11111111)
    lda #<sprMsgM9
    ldx #>sprMsgM9
    jsr sprdP
    jsr kNewline
    lda #<sprMsgM9b
    ldx #>sprMsgM9b
    jsr sprdP
    jsr kNewline
    rts

//  --- mode 7: the label + counter, painted on entry and after every clear ---
//  (kClearScreen wipes the screen, so this cannot be a one-shot.)
sprdT7Paint:
    ldx #0
    ldy #0
    jsr kSetCursor
    lda #<sprMsgM7
    ldx #>sprMsgM7
    jsr sprdP
    ldx #0
    ldy #1
    jsr kSetCursor
    lda #<sprMsgM7b
    ldx #>sprMsgM7b
    jsr sprdP
    lda SPRD_TICK
    jsr sprdHex
    lda #$20
    jsr kPrintChar
    lda sprdT7X
    jsr sprdHex
    rts

// --- place one arena sprite without moving it (SPRD_SLOT = sprite) ---
//  WARNING: x is nine bits: xH is this sprite's $D010 bit, and kSpritePos takes it in
//  the CARRY (its third input is A = x lo, Y = y, C = x high). `lsr` moves the
//  flag into C without needing A to be free first.
sprdMvPos:
    ldx SPRD_SLOT
    ldy sprdArY,x
    lda sprdArXH,x
    lsr                          // C = x high bit (0 = x < 256)
    lda sprdArX,x                // A = x lo
    jsr kSpritePos
    rts

// --- move one arena sprite: velocity, bounce off the bounds, re-place it ---
//  Input: SPRD_SLOT = sprite. Velocities are SIGNED bytes, so the x step tests
//  the direction and uses `adc`/`sbc` accordingly -- the carry then means
//  "crossed 256" going right and "crossed below 256" going left, which is
//  exactly when the high bit flips. |dx| is 2, so clamping at a bound is exact.
sprdMove:
    ldx SPRD_SLOT
    lda sprdArDX,x
    bmi sprdMvXleft
    //  --- right: lo += 2, carry out = passed 255 ---
    lda sprdArX,x
    clc
    adc #2
    sta sprdArX,x
    bcc sprdMvXrOk
    lda #1
    sta sprdArXH,x               // now x >= 256
sprdMvXrOk:
    lda sprdArXH,x
    beq sprdMvY                  // below 256: nowhere near the right bound
    lda sprdArX,x
    cmp #SPRD_XMAX_LO            // 40 -> x == 296
    bcc sprdMvY
    lda #SPRD_XMAX_LO
    sta sprdArX,x                // high bit stays set
    jsr sprdFlipX
    jmp sprdMvY
    //  --- left: lo -= 2, borrow = dropped below 256 ---
sprdMvXleft:
    lda sprdArX,x
    sec
    sbc #2
    sta sprdArX,x
    bcs sprdMvXlOk
    lda #0
    sta sprdArXH,x
sprdMvXlOk:
    lda sprdArXH,x
    bne sprdMvY                  // still >= 256: not at the left bound
    lda sprdArX,x
    cmp #SPRD_XMIN
    bcs sprdMvY
    lda #SPRD_XMIN
    sta sprdArX,x
    jsr sprdFlipX
sprdMvY:
    lda sprdArY,x
    clc
    adc sprdArDY,x
    sta sprdArY,x
    cmp #SPRD_YMIN
    bcc sprdMvYlo
    cmp #SPRD_YMAX
    bcs sprdMvYhi
sprdMvDone:
    jmp sprdMvPos
sprdMvYlo:
    lda #SPRD_YMIN
    sta sprdArY,x
    jsr sprdFlipY
    jmp sprdMvDone
sprdMvYhi:
    lda #SPRD_YMAX
    sta sprdArY,x
    jsr sprdFlipY
    jmp sprdMvDone
//  Negate the velocity (two's complement). X still holds the sprite index.
sprdFlipX:
    lda sprdArDX,x
    eor #$ff
    clc
    adc #1
    sta sprdArDX,x
    rts
sprdFlipY:
    lda sprdArDY,x
    eor #$ff
    clc
    adc #1
    sta sprdArDY,x
    rts

// ==========================================
//  Modes 1 and 8 -- the smoothness A/B
// ==========================================
//  THE TWO MODES DIFFER BY TWO DATA BYTES. `sprdStepF`/`sprdStepL` are the x
//  step, and they are the whole difference:
//    mode 1: 217 + 0 -> a FRACTIONAL ramp. The 1/256-px fraction carries into
//            the whole-pixel byte, so the position is a smooth function of time
//            and the 50.1 Hz display samples it as 0,1,2,3,...
//    mode 8: 0 + 1  -> `pos += 1` per tick. 59.2 updates/s sampled at 50.1 Hz is
//            1.181 ticks per frame, so one frame in six shows a 2 px step -- a
//            ~9 Hz hitch that NO kernel change can remove.
//  Sharing one code path is deliberate: it makes it impossible for the two
//  modes to differ in anything else, which is what an A/B has to guarantee.
//
//  The path is a traverse with a triangle-wave y, and the x range is 24..280 --
//  past 255 -- so the $D010 bit is exercised in continuous motion (mode 5 proves
//  the bounce; this proves the ramp).
sprdMode1:
    lda SPRD_MODE
    cmp #8
    beq sprdM1Naive
    lda #<sprMsgM1
    ldx #>sprMsgM1
    lda #217                     // 0.847 px per tick
    sta sprdStepF
    lda #0
    sta sprdStepL
    jmp sprdM1Go
sprdM1Naive:
    lda #<sprMsgM8
    ldx #>sprMsgM8
    lda #0
    sta sprdStepF                // no fraction: one whole pixel per tick
    lda #1
    sta sprdStepL
sprdM1Go:
    jsr sprdP
    jsr kNewline
    lda SPRD_MODE
    cmp #8
    beq sprdM1Msg2
    lda #<sprMsgM1b
    ldx #>sprMsgM1b
    jmp sprdM1Msg2b
sprdM1Msg2:
    lda #<sprMsgM8b
    ldx #>sprMsgM8b
sprdM1Msg2b:
    jsr sprdP
    jsr kNewline
    lda #0
    sta SPRD_TICK
    sta SPRD_CNT                 // sheet rejections
    sta SPRD_FAIL                // frame-load rejections
    //  ball.spr into slots 0-3 (one record per slot) + frame 0 into each
    lda #<sprBallName
    sta kPtr
    lda #>sprBallName
    sta kPtr+1
    lda #0
    sta SPRD_SLOT
sprdM1Sheet:
    lda SPRD_SLOT
    jsr kSpriteSheet
    bcc sprdM1SheetOk
    inc SPRD_CNT
sprdM1SheetOk:
    inc SPRD_SLOT
    lda SPRD_SLOT
    cmp #SPRITE_TASK_SLOTS
    bcc sprdM1Sheet
    lda #0
    sta SPRD_SLOT
sprdM1Frame:
    ldx #0
    lda SPRD_SLOT
    jsr kSpriteFrame
    bcc sprdM1FrameOk
    inc SPRD_FAIL
sprdM1FrameOk:
    inc SPRD_SLOT
    lda SPRD_SLOT
    cmp #SPRITE_TASK_SLOTS
    bcc sprdM1Frame
    //  place all eight, then colours 0-7, then enable
    lda #0
    sta SPRD_SLOT
sprdM1Place:
    jsr sprdOrbPut
    inc SPRD_SLOT
    lda SPRD_SLOT
    cmp #SPRITE_TASK_SLOTS
    bcc sprdM1Place
    ldx #SPR_SHD_COLOUR
    lda #7
sprdM1Col:
    jsr kSpriteReg
    inx
    cpx #SPR_SHD_COLOUR + SPRITE_TASK_SLOTS
    bcc sprdM1Col
    sprOn(%11111111)
    rts

// ==========================================
//  Mode 7 -- tear: the section 6.6 procedure, reworked
// ==========================================
//  A late commit shows the old X above and the new one below, so a tear is only
//  observable when ALL THREE hold: the sprite is on screen, the position moved a
//  visible amount since the last commit, and the commit lands *inside* the
//  sprite's lines. All three have to be arranged deliberately: y=31 puts 19 of the
//  21 lines in the top border, a 0.3 px/frame drift leaves the step under a pixel,
//  and the provoker below is this task's own call.
//
//  WARNING: THE PROVOKER MUST BE THIS TASK'S OWN CALL. A provoker typed at the
//  shell cannot work: the shell owns the display then, so the kernel has installed
//  the SHELL's sprite state and our own is parked in the REU -- nothing of ours is
//  in $D015, so there is nothing on screen to tear. That is the display-ownership
//  policy working: **another task's long DMA cannot tear your sprites, because
//  that task can only run while your sprites are off.**
//
//  So the provoker is `kClearScreen` every ~2 s: it holds SEI across an 8 KB
//  pixel fill plus a 1 KB matrix fill, and `kReuFillC64` issues ONE burst of
//  `length` cycles with the CPU halted -- ~9,200 cycles of deferral while THIS
//  task owns the display.
//
//  The commit then lands at an essentially random line of the frame, so a tear
//  needs the burst to STRADDLE line 0 *and* to end inside the sprite's lines.
//  The burst is ~9,200 cycles = 0.47 frames ~= 146 lines, so: 146/312 of start
//  phases contain line 0, and of those only 25 end inside lines 121-162 ->
//  **25/312 ~= 8% per provocation**, i.e. about one tear in twelve clears.
//  (Not 42/312: that ignored the straddle.)
//
//  WARNING: THE COMMIT HAS A DEADLINE. A commit that finds the raster past line
//  30 publishes NOTHING, so this mode shows a **held position for one frame**
//  (~4px jump on the following frame) instead of a split sprite. Mode 7 is the worst case on purpose -- 42 display lines
//  instead of 21, on a screen the provoker has just blanked, so there is nothing
//  to hide against. The counter being repainted after every clear is the "still
//  trying" signal; a HOLD is the pass, a horizontal step is the failure.
//
//  The bounds: only this sprite is affected, the screen and any
//  other sprite are untouched, and it is gone by the next frame.
sprdMode7:
    lda #0
    sta SPRD_TICK
    sta SPRD_CNT
    lda #<sprBallName
    sta kPtr
    lda #>sprBallName
    sta kPtr+1
    lda #0
    jsr kSpriteSheet
    bcc sprdM7SheetOk
    inc SPRD_CNT
sprdM7SheetOk:
    ldx #0
    lda #0
    jsr kSpriteFrame
    bcc sprdM7Place
    inc SPRD_CNT
sprdM7Place:
    sprColour(0, 7)
    sprYExpand(%00000001)        // 42 lines: doubles the window a late commit
    sprOn(%00000001)             // can land inside (see the tick)
    jsr sprdT7Paint
    rts

// ==========================================
//  Mode 6 -- shadow: the debugging tool
// ==========================================
//  Prints the shadow, the live VIC sprite registers, the byte after the shadow
//  block (the bound-check evidence) and the two rejection paths. This is what
//  turns "the sprite is wrong" into a readable answer. Note it reads the live
//  $D000-$D01D directly: reading is harmless, and comparing the two is the
//  point. It does NOT read $D01E/$D01F -- reading those clears them, so
//  collisions only ever come through kSpriteCollide.
sprdMode6:
    lda #<sprMsgSh
    ldx #>sprMsgSh
    jsr sprdP
    jsr kNewline
    lda #<sprShadow
    sta kPtr
    lda #>sprShadow
    sta kPtr+1
    lda #40                      // 39B shadow + the byte after it
    jsr sprdDump
    jsr sprdNl
    lda #<sprMsgLive
    ldx #>sprMsgLive
    jsr sprdP
    lda #<$d000
    sta kPtr
    lda #>$d000
    sta kPtr+1
    lda #16                      // $D000-$D00F (X/Y)
    jsr sprdDump
    lda #<sprMsgCtrl
    ldx #>sprMsgCtrl
    jsr sprdP
    lda $d010
    jsr sprdHex
    lda #$20
    jsr kPrintChar
    lda $d015
    jsr sprdHex
    lda $d01b
    jsr sprdHex
    lda $d01c
    jsr sprdHex
    lda $d01d
    jsr sprdHex
    jsr sprdNl
    //  The two colours that decide whether anything is VISIBLE. A sprite whose
    //  colour equals the background is drawn and invisible -- exactly how mode 0
    //  first failed -- so print both and make that one glance, not a hunt.
    lda #<sprMsgBg
    ldx #>sprMsgBg
    jsr sprdP
    lda $d021                    // bg0
    jsr sprdHex
    lda $d027                    // sprite 0's own colour
    jsr sprdHex
    jsr sprdNl
    //  Rejection paths. X = SPR_SHD_SIZE must be refused without writing, and so
    //  must slot 8 (one past the last slot) -- both are C=1, so print the carry.
    lda #<sprMsgRej
    ldx #>sprMsgRej
    jsr sprdP
    lda #0
    ldx #SPR_SHD_SIZE            // one past the end of the shadow
    jsr kSpriteReg
    jsr sprdCy
    lda #0
    ldx #SPRITE_TASK_SLOTS       // = 8, one past the last slot
    ldy #0
    clc
    jsr kSpritePos
    jsr sprdCy
    lda #0
    ldx #0
    ldy #0
    clc
    jsr kSpritePos               // sprite 0 in the same frame: expect C=0
    jsr sprdCy
    jsr sprdNl
    //  Focus state: the guard compares curTask with kbFocus, and a task's own
    //  id comes from SYS_GETTID (curTask in ZP is the IRQ's, never a task's).
    lda #<sprMsgFoc
    ldx #>sprMsgFoc
    jsr sprdP
    lda #SYS_GETTID
    jsr syscall
    jsr sprdHex
    lda kbFocus
    jsr sprdHex
    jsr sprdNl
    lda #<sprMsgQ
    ldx #>sprMsgQ
    jsr sprdP
    rts

// ==========================================
//  sprdTick -- per-mode animation step
// ==========================================
//  In 4a only mode 0 moves: it sweeps slowly so the position path is visibly
//  live and the commit is continuously exercised. Modes are otherwise static.
sprdTick:
    lda SPRD_MODE
    beq sprdTickSweep            // mode 0: the raw-slot sweep
    cmp #2
    beq sprdTickAnim             // mode 2: cycle the ball's 8 frames
    cmp #3
    beq sprdTickFlags            // mode 3: pulse the blob's core
    cmp #4
    beq sprdTickPrio             // mode 4: live collision readout
    cmp #1
    beq sprdTickOrbJ             // modes 1 and 8 share the orbit code
    cmp #8
    bne sprdNotTi8
sprdTickOrbJ:
    jmp sprdTickOrbit
sprdNotTi8:
    cmp #7
    bne sprdNotTi7
    jmp sprdTickTear
sprdNotTi7:
    cmp #5
    bne sprdNotTi5
    jmp sprdTickArena            // out of branch range (Rule 4): three more
sprdNotTi5:                      // mode handlers sit in between
    cmp #9
    bne sprdTickDone
    jmp sprdTickBalls
sprdTickDone:
    rts
//  --- mode 0: sweep sprite 0 slowly, so a frozen sprite is obvious
sprdTickSweep:
    inc SPRD_TICK
    lda SPRD_TICK
    and #7
    bne sprdTickDone             // only every 8th tick
    lda SPRD_X
    clc
    adc #1
    cmp #71                      // 20..70, and x never needs $D010
    bcc sprdTickSame
    lda #20
sprdTickSame:
    sta SPRD_X
    ldx #0
    lda SPRD_X
    ldy #60
    clc
    jmp kSpritePos               // tail call
//  --- mode 2: advance ball.spr one frame every 16 ticks
sprdTickAnim:
    inc SPRD_TICK
    lda SPRD_TICK
    and #15
    bne sprdTickDone
    lda SPRD_IDX
    clc
    adc #1
    and #7                       // 8 frames, task-owned wrap
    sta SPRD_IDX
    tax                          // X = frame
    lda #0                       // A = slot 0
    jsr kSpriteFrame
    rts

//  --- mode 3: pulse the blob's core, all four slots together (slower: four
//  frame loads per step, and the pulse reads better at ~2 Hz)
sprdTickFlags:
    inc SPRD_TICK
    lda SPRD_TICK
    and #31
    bne sprdTickDone
    lda SPRD_IDX
    clc
    adc #1
    and #3                       // 4 frames, task-owned wrap
    sta SPRD_IDX
    lda #0
    sta SPRD_SLOT
sprdT3Lp:
    ldx SPRD_IDX                 // X = frame
    lda SPRD_SLOT                // A = slot
    jsr kSpriteFrame             // clobbers A, X, Y and kPtr
    inc SPRD_SLOT
    lda SPRD_SLOT
    cmp #SPRITE_TASK_SLOTS
    bcc sprdT3Lp
    rts

//  --- mode 4: keep a live collision readout on row 4. Both sprites overlap the
//  wall, so $D01F should stay set; $D01E stays clear (they never meet).
//  kSpriteCollide CLEARS the latches, so this is also the poll -- a fixed row
//  means no scroll and no moving the wall.
sprdTickPrio:
    inc SPRD_TICK
    lda SPRD_TICK
    and #31
    bne sprdTickDone
    ldx #0
    ldy #4
    jsr kSetCursor
    lda #<sprMsgM4c
    ldx #>sprMsgM4c
    jsr sprdP
    jsr kSpriteCollide          // C=1 if we are not the display owner
    bcs sprdTickDone
    sta SPRD_CNT                // $D01E mask
    stx SPRD_FAIL               // $D01F mask (kPrintChar clobbers X)
    lda SPRD_CNT
    jsr sprdHex
    lda SPRD_FAIL
    jsr sprdHex
    rts

//  --- mode 5: bounce every other tick, poll every 8th ---
sprdTickArena:
    inc SPRD_TICK
    lda SPRD_TICK
    and #1
    bne sprdT5Poll
    lda #0
    sta SPRD_SLOT
sprdT5Move:
    jsr sprdMove
    inc SPRD_SLOT
    lda SPRD_SLOT
    cmp #2
    bcc sprdT5Move
sprdT5Poll:
    lda SPRD_TICK
    and #7
    bne sprdT5Out
    jsr kSpriteCollide
    bcs sprdT5Out               // lost the display: nothing to report
    sta SPRD_CNT                // $D01E
    stx SPRD_FAIL               // $D01F
    lda SPRD_CNT
    beq sprdT5NoSs
    ldx sprdLogSs
    ldy #22
    jsr kSetCursor
    lda #<sprdStar
    ldx #>sprdStar
    jsr sprdP
    inc sprdLogSs
    lda sprdLogSs
    cmp #40
    bcc sprdT5NoSs
    lda #2
    sta sprdLogSs
sprdT5NoSs:
    lda SPRD_FAIL
    beq sprdT5Out
    ldx sprdLogBg
    ldy #23
    jsr kSetCursor
    lda #<sprdPlus
    ldx #>sprdPlus
    jsr sprdP
    inc sprdLogBg
    lda sprdLogBg
    cmp #40
    bcc sprdT5Out
    lda #2
    sta sprdLogBg
sprdT5Out:
    rts

// ==========================================
//  Helpers (after the entry point, so the code-size field covers everything)
// ==========================================

//  --- mode 9: integrate, bounce, then resolve ball-ball overlaps ---
sprdTickBalls:
    inc SPRD_TICK
    //  1. MOVE -- every ball, in ZP-indexed loops: kSpritePos and the sprite
    //  calls clobber A, X and Y, so no loop index may live in a register.
    lda #0
    sta SPRD_BI
sprdT9Move:
    ldx SPRD_BI
    lda sprdBXLo,x               // x += vx -- 16-bit, and the velocity is signed,
    clc                          // so this one add covers every direction
    adc sprdBVXLo,x
    sta sprdBXLo,x
    lda sprdBXHi,x
    adc sprdBVXHi,x
    sta sprdBXHi,x
    lda sprdBYLo,x               // y += vy -- same shape, fraction then integer
    clc
    adc sprdBVYLo,x
    sta sprdBYLo,x
    lda sprdBYHi,x
    adc sprdBVYHi,x
    sta sprdBYHi,x
    jsr sprdBWalls               // clamp at a wall, flipping that component
    jsr sprdBPut                 // then show it there
    inc SPRD_BI
    lda SPRD_BI
    cmp #SPRD_BALLS
    bcc sprdT9Move
    //  2. COLLIDE -- each of the 28 pairs (i<j) exactly once
    lda #0
    sta SPRD_BI
sprdT9Outer:
    lda SPRD_BI
    clc
    adc #1
    sta SPRD_BJ
sprdT9Inner:
    jsr sprdBHit
    inc SPRD_BJ
    lda SPRD_BJ
    cmp #SPRD_BALLS
    bcc sprdT9Inner
    inc SPRD_BI
    lda SPRD_BI
    cmp #SPRD_BALLS-1
    bcc sprdT9Outer
    rts

//  Place one ball. X = ball index; kSpritePos takes the sprite in X, x in A
//  with the 9th bit in the CARRY, and y in Y -- and clobbers all three.
//  sprdBWalls has already run, so the high byte is 0 or 1 here.
sprdBPut:
    lda sprdBXHi,x
    lsr                          // C = x bit 8
    lda sprdBXLo,x
    ldy sprdBYHi,x
    jmp kSpritePos               // tail call

//  Clamp one ball inside the arena, flipping whichever component hit a wall.
//  X = ball index. Clobbers A, Y (and kSpritePos's clobbers, via sprdBPut).
sprdBWalls:
    //  ---- x: 0..296. $0128, so "hi = 1 and lo > 40" IS past the right wall ----
    lda sprdBXHi,x
    beq sprdBWxOk                // 0..255 -- inside, whatever the fraction says
    cmp #$ff
    bne sprdBWxPosHi
    lda #0                       // went past the left wall -> pin to 0
    sta sprdBXLo,x
    sta sprdBXHi,x
    jsr sprdBNegVX
    jmp sprdBWxOk
sprdBWxPosHi:
    cmp #1
    bne sprdBWxFar               // 2+ -- impossibly far, treat as the wall
    lda sprdBXLo,x
    cmp #SPRD_BXMAX-$100+1       // 41: lo > 40 means x > 296
    bcc sprdBWxOk
sprdBWxFar:
    lda #SPRD_BXMAX-$100         // pin to 296 = $0128 and turn around
    sta sprdBXLo,x
    lda #1
    sta sprdBXHi,x
    jsr sprdBNegVX
sprdBWxOk:
    //  ---- y: 50..229 ----
    lda sprdBYHi,x
    cmp #SPRD_BYMIN
    bcc sprdBWyLow
    cmp #SPRD_BYMAX+1
    bcs sprdBWyHigh
    rts
sprdBWyLow:
    lda #SPRD_BYMIN
    sta sprdBYHi,x
    jmp sprdBNegVY               // tail call -- its rts returns to our caller
sprdBWyHigh:
    lda #SPRD_BYMAX
    sta sprdBYHi,x
    jmp sprdBNegVY

//  Reverse one velocity component: a 16-bit two's complement negation, with the
//  low byte's overflow carried into the high one. Exact, so a ball's speed is
//  identical either side of a bounce and nothing drifts over a long run.
//  X = ball index. Clobbers A.
sprdBNegVX:
    lda sprdBVXLo,x
    eor #$ff
    clc
    adc #1
    sta sprdBVXLo,x
    lda sprdBVXHi,x
    eor #$ff
    adc #0
    sta sprdBVXHi,x
    rts
sprdBNegVY:
    lda sprdBVYLo,x
    eor #$ff
    clc
    adc #1
    sta sprdBVYLo,x
    lda sprdBVYHi,x
    eor #$ff
    adc #0
    sta sprdBVYHi,x
    rts

//  Overlap test for the pair (SPRD_BI, SPRD_BJ), and the bounce if they touch.
//  A rectangle test, not a circle one: the sprites ARE rectangles, and the
//  half-extents are deliberately under the real 24x21 so a bounce needs a
//  visible overlap. Clobbers A, X, Y.
sprdBHit:
    ldx SPRD_BI
    ldy SPRD_BJ
    lda sprdBXLo,x               // dx = xi - xj, 16-bit
    sec
    sbc sprdBXLo,y
    sta SPRD_T0
    lda sprdBXHi,x
    sbc sprdBXHi,y               // A = dx high. Two 0..296 values can only give
    beq sprdBHdxPos              // 0, 1 or $ff here, so the test is short:
    cmp #$ff                     // $ff means dx is -256..-1
    bne sprdBHout                // 1 (or 2) means |dx| >= 256 -- nowhere near
    lda SPRD_T0
    cmp #SPRD_HITXLO             // < -16 -> out
    bcc sprdBHout
    jmp sprdBHdy
sprdBHdxPos:
    lda SPRD_T0
    cmp #SPRD_HITX+1             // > 16 -> out
    bcs sprdBHout
sprdBHdy:
    lda sprdBYHi,x               // dy = yi - yj, integer bytes only -- a
    sec                          // sub-pixel gap is not worth a second compare
    sbc sprdBYHi,y
    bcs sprdBHdyPos
    eor #$ff
    clc
    adc #1                       // A = |dy|
sprdBHdyPos:
    cmp #SPRD_HITY+1             // > 14 -> out
    bcs sprdBHout
    //  They touch: exchange the velocity vectors. Equal masses, so this is the
    //  elastic answer, and it ALWAYS separates them -- the relative velocity is
    //  reversed by construction, so a pair can never stick together.
    jsr sprdBSwapVX
    jsr sprdBSwapVY
sprdBHout:
    rts

//  Exchange one velocity component between balls SPRD_BI and SPRD_BJ.
//  SPRD_T2 is the temp -- pha/pla would work but the stack is not free here.
sprdBSwapVX:
    ldx SPRD_BI
    ldy SPRD_BJ
    lda sprdBVXLo,x
    sta SPRD_T2
    lda sprdBVXLo,y
    sta sprdBVXLo,x
    lda SPRD_T2
    sta sprdBVXLo,y
    lda sprdBVXHi,x
    sta SPRD_T2
    lda sprdBVXHi,y
    sta sprdBVXHi,x
    lda SPRD_T2
    sta sprdBVXHi,y
    rts
sprdBSwapVY:
    ldx SPRD_BI
    ldy SPRD_BJ
    lda sprdBVYLo,x
    sta SPRD_T2
    lda sprdBVYLo,y
    sta sprdBVYLo,x
    lda SPRD_T2
    sta sprdBVYLo,y
    lda sprdBVYHi,x
    sta SPRD_T2
    lda sprdBVYHi,y
    sta sprdBVYHi,x
    lda SPRD_T2
    sta sprdBVYHi,y
    rts

//  --- modes 1/8: advance the orbit every tick ---
//  x moves every tick (that is the A/B) and y's wobble angle advances every
//  fourth, then all eight sprites are re-placed from the tables.
sprdTickOrbit:
    inc SPRD_TICK
    lda #0
    sta SPRD_SLOT
sprdOrbLp:
    jsr sprdOrbStep
    inc SPRD_SLOT
    lda SPRD_SLOT
    cmp #SPRITE_TASK_SLOTS
    bcc sprdOrbLp
    lda SPRD_TICK
    and #3
    bne sprdOrbPlace
    lda #0
    sta SPRD_SLOT
sprdOrbAngLp:
    ldx SPRD_SLOT
    inc sprdOYA,x
    inc SPRD_SLOT
    lda SPRD_SLOT
    cmp #SPRITE_TASK_SLOTS
    bcc sprdOrbAngLp
sprdOrbPlace:
    lda #0
    sta SPRD_SLOT
sprdOrbPlLp:
    jsr sprdOrbPut
    inc SPRD_SLOT
    lda SPRD_SLOT
    cmp #SPRITE_TASK_SLOTS
    bcc sprdOrbPlLp
    //  colours cycle every 16th tick: one palette, four phases. kSpriteReg
    //  preserves X (its guard uses cpy), so the shadow offset can live in it.
    lda SPRD_TICK
    and #15
    bne sprdOrbOut
    lda #0
    sta SPRD_SLOT
sprdOrbColLp:
    ldx SPRD_SLOT
    txa
    clc
    adc #SPR_SHD_COLOUR
    tax                          // X = this sprite's colour offset
    lda SPRD_TICK
    lsr
    lsr
    lsr
    lsr                          // tick / 16
    clc
    adc SPRD_SLOT
    and #7
    tay
    lda sprdPal,y
    jsr kSpriteReg
    inc SPRD_SLOT
    lda SPRD_SLOT
    cmp #SPRITE_TASK_SLOTS
    bcc sprdOrbColLp
sprdOrbOut:
    rts

//  x += the step, bounce off 24/280, flip the direction. The step is the ONLY
//  difference between modes 1 and 8 (sprdStepF/sprdStepL), and the carry chain
//  across fraction -> low -> high is what makes x nine bits wide.
sprdOrbStep:
    ldx SPRD_SLOT
    lda sprdOXD,x
    bne sprdOrbSL
    lda sprdOXF,x
    clc
    adc sprdStepF
    sta sprdOXF,x
    lda sprdOXL,x
    adc sprdStepL
    sta sprdOXL,x
    bcc sprdOrbBound
    lda sprdOXH,x
    eor #1
    sta sprdOXH,x
    jmp sprdOrbBound
sprdOrbSL:
    lda sprdOXF,x
    sec
    sbc sprdStepF
    sta sprdOXF,x
    lda sprdOXL,x
    sbc sprdStepL
    sta sprdOXL,x
    bcs sprdOrbBound
    lda sprdOXH,x
    eor #1
    sta sprdOXH,x
sprdOrbBound:
    lda sprdOXH,x
    beq sprdOrbBoundLo
    lda sprdOXL,x                // x >= 256: the right bound is lo >= 24
    cmp #24
    bcc sprdOrbStepDone
    lda #24
    sta sprdOXL,x
    jmp sprdOrbFlip
sprdOrbBoundLo:
    lda sprdOXL,x
    cmp #24
    bcs sprdOrbStepDone
    lda #24
    sta sprdOXL,x
sprdOrbFlip:
    lda sprdOXD,x
    eor #1
    sta sprdOXD,x
sprdOrbStepDone:
    rts

//  Place one orbit sprite: y = base + the triangle table, x nine bits with the
//  high bit in the carry (kSpritePos's third argument).
sprdOrbPut:
    ldx SPRD_SLOT
    lda sprdOYA,x
    and #63
    tay
    lda sprdWob,y
    clc
    adc sprdOYB,x
    tay                          // Y = y
    lda sprdOXH,x
    lsr                          // C = x high bit
    lda sprdOXL,x
    jsr kSpritePos
    rts

//  --- mode 7: drift 2 px/tick, and provoke a late commit every ~2 s ---
sprdTickTear:
    inc SPRD_TICK
    //  2 px/tick = ~2.4 px per frame, so the step a tear reveals is one you can
    //  actually see (a 0.3 px drift leaves it under a pixel)
    lda sprdT7D
    bne sprdT7L
    lda sprdT7X
    clc
    adc #2
    sta sprdT7X
    cmp #232
    bcc sprdT7Place
    dec sprdT7X
    inc sprdT7D
    jmp sprdT7Place
sprdT7L:
    lda sprdT7X
    sec
    sbc #2
    sta sprdT7X
    cmp #24
    bcs sprdT7Place
    inc sprdT7X
    dec sprdT7D
sprdT7Place:
    ldx #0
    lda sprdT7X
    ldy #120                     // in view, and y-expanded to 42 lines
    clc
    jsr kSpritePos
    lda SPRD_TICK
    and #127
    bne sprdT7Out
    jsr kClearScreen             // THE PROVOKER: ~9,200 cycles, SEI held,
    jsr sprdT7Paint              // so the commit is held off past our lines.
sprdT7Out:                       // The clear wiped the label, so repaint it.
    rts

//  Print the string at (A = lo, X = hi). kPtr is the task's own copy, so this
//  is safe to use anywhere in the demo.
sprdP:
    sta kPtr
    stx kPtr+1
    jsr kPrintString
    rts

//  Newline via the kernel's own routine (`kPrintString` does not interpret
//  control codes, so a CR byte would print a CR glyph).
sprdNl:
    jmp kNewline

//  Print A as two hex digits.
sprdHex:
    pha
    lsr
    lsr
    lsr
    lsr
    jsr sprdNib
    pla
    and #$0f
sprdNib:
    tax
    lda sprdHexDig,x
    jmp kPrintChar               // tail call: prints, advances, returns

//  Print 'C' + the carry as 0/1 -- the shape every rejection path reports in.
//  WARNING: The carry must be CAPTURED BEFORE the first kPrintChar. printChar clears
//  it on the way out (it does `and #$0F` combining the matrix byte), so reading
//  the carry after the call reports kPrintChar's, not the API's -- which is
//  always 0, making every rejection look like a success. That is exactly how
//  this helper first shipped, and it silently hid the reject paths.
//  Preserves A (the API's return value). P is clobbered: callers set their own
//  carry with clc before the call they are testing.
sprdCy:
    pha                         // keep the API's return value for the caller
    php                         // ...and P, whose bit 0 is the carry we report
    lda #'C'
    jsr kPrintChar
    pla                         // A = the caller's P
    and #$01                    // carry -> '0'/'1'
    ora #'0'
    jsr kPrintChar
    pla                         // A = the API's return value
    rts

//  Print `A` bytes of hex from (kPtr). kPtr is advanced for the next block.
//  Clobbers A, X, Y, kPtr.
sprdDump:
    sta SPRD_CNT
    lda #0
    sta SPRD_IDX
sprdDumpL:
    ldy SPRD_IDX
    lda (kPtr),y
    jsr sprdHex
    inc SPRD_IDX
    lda SPRD_IDX
    cmp SPRD_CNT
    bcc sprdDumpL
    rts

// ==========================================
//  Data
// ==========================================
.encoding "screencode_mixed"

sprMsgM0:  .text "mode 0  raw    : 64B pattern into slot0 $02C0, no reu"
           .byte 0
sprMsgM0b: .text "sprite 0 (yellow) sweeps 20-70;  slots=8, all eight are tasks"
           .byte 0
sprMsgSh:  .text "mode 6  shadow : 39B shadow + 1, then the live vic regs"
           .byte 0
sprMsgLive: .text "live D000-0F     "
           .byte 0
sprMsgCtrl: .text "  D010 "
           .byte 0
sprMsgBg:   .text "BG0/S27 "
           .byte 0
sprMsgRej:  .text "reject: reg(X=39) pos(8=oob) pos(0) = C"
           .byte 0
sprMsgFoc:  .text "focus: own id / kbfocus = "
           .byte 0
sprMsgQ:   .text "q=exit  0-8 select: 1=orbit 2=anim 3=flags 4=prio 5=arena 6=shadow 7=tear 8=naive"
           .byte 0
sprTodo:   .text "no such mode  q=exit"
           .byte 0
sprMsgM2:  .text "mode 2  anim   : ball.spr x8 on s0, ship.spr f0-6 on s1-7"
           .byte 0
sprMsgM3:  .text "mode 3  flags  : 7 sprites: hires/xexp/yexp/x+y/mc/mc+x/mc+y"
           .byte 0
sprMsgM4:  .text "mode 4  prio   : ball.spr twice -- sprite 0 in front, sprite 1 behind"
           .byte 0
sprMsgM4b: .text "wall = printed solid cells; behind is sliced at x=64"
           .byte 0
sprMsgM4c: .text "collide SS/BG = "
           .byte 0
sprMsgM5:  .text "mode 5  arena  : 2 sprites bounce x=16..296 (past 255, $D010)"
           .byte 0
sprMsgM5b: .text "row22=SS row23=BG, polled every 8th tick (~7Hz) -- latch sees all"
           .byte 0
sprMsgM1:  .text "mode 1  orbit  : FRACTIONAL ramp, 0.847px/tick -- smooth"
           .byte 0
sprMsgM1b: .text "press 8: same orbit, pos+=1/tick -- look for the jitter, not a hitch"
           .byte 0
sprMsgM8:  .text "mode 8  naive  : pos+=1/tick -- 9Hz step jitter; subtle at 1px (d15)"
           .byte 0
sprMsgM8b: .text "59.2 ticks/s sampled at 50.1 Hz = a 2px step 1 frame in 6 (~9Hz)"
           .byte 0
sprMsgM9:  .text "mode 9  balls  : 8 coloured balls, random speed + direction,"
           .byte 0
sprMsgM9b: .text "bounce off walls and each other - q quits - 0-8 = test modes"
           .byte 0
sprMsgM7:  .text "mode 7  tear   : guard ON -- expect a 1-frame HOLD, never a split"
           .byte 0
sprMsgM7b: .text "provoker = kClearScreen (8KB SEI'd) every ~2s. tick/x: "
           .byte 0
sprBlobName: .text "blob.spr"
           .byte 0
sprMsgFail: .text "reject: sheets NN frames NN"
           .byte 0
sprBallName: .text "ball.spr"
           .byte 0
sprShipName: .text "ship.spr"
           .byte 0

sprdHexDig: .text "0123456789ABCDEF"

//  --- mode 4/5 data ---
//  A solid row: screen code $A0 is the filled 8x8 block in the OS font
//  (charsets/gordon.fnt, byte-verified), so these are real foreground pixels --
//  both the thing a sprite can be hidden behind and the thing it collides with.
sprdWall24:  .byte $A0,$A0,$A0,$A0,$A0,$A0,$A0,$A0,$A0,$A0,$A0,$A0
              .byte $A0,$A0,$A0,$A0,$A0,$A0,$A0,$A0,$A0,$A0,$A0,$A0
              .byte 0
sprdWall16:  .byte $A0,$A0,$A0,$A0,$A0,$A0,$A0,$A0
              .byte $A0,$A0,$A0,$A0,$A0,$A0,$A0,$A0
              .byte 0
sprdStar:    .byte $2A, 0     // screen code '*' -- a sprite-sprite event
sprdPlus:    .byte $2B, 0     // screen code '+' -- a wall (background) event
//  Arena state. Mutable, and safe to keep here: sprdemo is RE-RUNNABLE but not
//  reentrant, so a re-run loads a fresh copy and these re-initialise.
sprdArX:     .byte 100, 240
sprdArXH:    .byte 0, 0       // x bit 8 -- this sprite's $D010 bit ($00/$01)
sprdArY:     .byte 70, 150
sprdArDX:    .byte 2, $fe     // signed: +2 and -2 (two's complement)
sprdArDY:    .byte 2, $fe
sprdLogSs:   .byte 2          // next event-log column, row 22 / row 23
sprdLogBg:   .byte 2

// --- modes 1/8 data ---
//  x is NINE bits plus a 1/256-px fraction: (sprdOXH, sprdOXL, sprdOXF).
//  sprdStepF/sprdStepL are the whole A/B: 217+0 (fractional) vs 0+1 (naive),
//  written by the mode entry, read by sprdOrbStep. Nothing else differs.
sprdOXH:  .byte 0, 0, 0, 0, 0, 0, 0, 0
sprdOXL:  .byte 24, 56, 88, 120, 152, 184, 216, 248
sprdOXF:  .byte 0, 32, 64, 96, 128, 160, 192, 224
sprdOXD:  .byte 0, 1, 0, 1, 0, 1, 0, 1  // 0 = moving right, 1 = moving left
sprdOYA:  .byte 0, 8, 16, 24, 32, 40, 48, 56  // wobble angle, & 63
//  Wobble base: y = base + table, where the triangle table spans 0..31. The
//  largest base is 192, so the tallest y is 223 -- the 21-line sprite still ends
//  at line 244, clear of the bottom of the window (section 4.4).
sprdOYB:  .byte 52, 72, 92, 112, 132, 152, 172, 192
sprdStepF: .byte 217            // px/tick as a 1/256 fraction -- mode 1
sprdStepL: .byte 0              // whole px/tick -- mode 1 (mode 8 writes these)
sprdPal:  .byte 7, 3, 10, 13, 1, 5, 8, 15
// --- mode 3 data: eight sprites in two rows of four (x then y, per sprite) ---
//  Columns 70 px apart because the X-expanded sprites are 48 px wide; rows 70 and
//  150 so a 42-line Y-expanded sprite at y=150 still ends inside the window.
sprdM3X:  .byte 20, 90, 160, 230, 20, 90, 160, 230
sprdM3Y:  .byte 70, 70, 70, 70, 150, 150, 150, 150
//  The y wobble: a TRIANGLE, not a sine, and generated rather than typed. Its
//  slope is constant, so the y motion is exactly "a smooth function of time" and
//  the only thing left that can hitch is the X step -- which is what modes 1/8
//  are comparing. A sine would add its own curvature to that picture for no
//  benefit, and hand-typing 64 values is how tables get a wrong entry.
sprdWob:
    .for (var i = 0; i < 32; i++) { .byte i }
    .for (var i = 31; i >= 0; i--) { .byte i }

// --- mode 7 data ---
sprdT7X:  .byte 120            // drifting x
sprdT7D:  .byte 0              // 0 = right, 1 = left

// --- mode 9 data: eight balls ---
//  x and y are 8.8 fixed point (fraction then integer) and the velocities are
//  8.8 SIGNED, so the SAME 16-bit add integrates every speed and a wall bounce
//  is a two's-complement negation. y's integer byte stays 50..229 (the visible
//  window) and x's is nine bits, 0..296.
sprdBXLo:  .fill SPRD_BALLS, 0
sprdBXHi:  .fill SPRD_BALLS, 0
sprdBYLo:  .fill SPRD_BALLS, 0
sprdBYHi:  .fill SPRD_BALLS, 0
sprdBVXLo: .fill SPRD_BALLS, 0
sprdBVXHi: .fill SPRD_BALLS, 0
sprdBVYLo: .fill SPRD_BALLS, 0
sprdBVYHi: .fill SPRD_BALLS, 0
//  Four speed classes in 1/256 px per tick: 0.31, 0.63, 1.13, 1.88. The slowest
//  crosses the screen in ~17 s and the fastest in ~2.8 s -- "some fast, some
//  slow" without any ball being an unreadable blur.
sprdSpdLo: .byte $50, $a0, $20, $e0
sprdSpdHi: .byte 0,   0,   1,   1

//  Mode 0's 64-byte pattern: a frame with a bar across the middle, so a wrong
//  row stride, a wrong bit order or a wrong slot shows up immediately as a
//  broken shape rather than as "some pixels". 3 bytes/row, bit 7 = leftmost,
//  21 rows (63B) + 1 pad byte.
sprdPattern:
    .byte %11111111, %11111111, %11111111   // row 0  -- top edge
    .byte %10000000, %00000000, %00000001
    .byte %10000000, %00000000, %00000001
    .byte %10000000, %00000000, %00000001
    .byte %10000000, %00000000, %00000001
    .byte %10000000, %00000000, %00000001
    .byte %10000000, %00000000, %00000001
    .byte %10000000, %00000000, %00000001
    .byte %10000000, %00000000, %00000001
    .byte %10000000, %00000000, %00000001
    .byte %11111111, %11111111, %11111111   // row 10 -- centre bar
    .byte %10000000, %00000000, %00000001
    .byte %10000000, %00000000, %00000001
    .byte %10000000, %00000000, %00000001
    .byte %10000000, %00000000, %00000001
    .byte %10000000, %00000000, %00000001
    .byte %10000000, %00000000, %00000001
    .byte %10000000, %00000000, %00000001
    .byte %10000000, %00000000, %00000001
    .byte %10000000, %00000000, %00000001
    .byte %11111111, %11111111, %11111111   // row 20 -- bottom edge
    .byte 0                                 // pad: the 64th byte
