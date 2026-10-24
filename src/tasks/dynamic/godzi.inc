// ==========================================
//  GordonOS -- godzi: the ported two-figure sprite demo
// ==========================================
//  On screen: a boat-shaped walker crosses the display, left to right, over a
//  green border and a yellow sky -- black rigging over a light-blue hull, its two
//  layers -- while a black "Godzilla" flaps between two poses further up the
//  screen. It runs forever: q exits, '1'/'2' change the look, and the shell can
//  kill the task like any other.
//
//  PROVENANCE. reference/godzi.asm and reference/main.asm are the same
//  stand-alone demo written twice -- both already in KickAssembler syntax -- and
//  this file is a port of both:
//
//    reference                                 this port
//    ----------------------------------------  --------------------------------
//    * = $c000 / BasicUpstart2, hooks           a dynamic task; the OS owns the
//    $0314/$0315                               60 Hz IRQ -- the demo is a task
//    VIC registers written directly             the sprite shadow (kSpritePos /
//    ($D000-$D02E, $07F8-$07FF)                 kSpriteFrame / kSpriteReg)
//    4 sprites at $03C0/$2000/$0340/$0380,      REU sheets into slots 0/1 (the two
//    pointers $0F/$80/$0D/$0E                   boat layers) and slots 2/3 (poses)
//    $D027/$D029/$D02A = 0, and $D028           the same two layers, with the
//    never written at all (see below)           omission fixed: $D028 = 14
//    ground + platform as screen RAM            dropped: an all-bitmap display
//    ($0720/$DB20, $0666)                       has no screen RAM, so the border
//                                               colour stands in for the ground
//    jsr $ffd2 to clear the screen              kAllocScreen + kDisableCursor
//    1 px every 3rd IRQ tick (19.7 px/s)        0.398 px per displayed FRAME --
//                                               the same pace, accumulated
//    godzi1/godzi2 swapped by writing           the same two poses, loaded once
//    $D015 (8 ticks each)                       into two slots and swapped
//                                               through the enable mask
//    sprite 3 toggled with `eor #$08`           mode 2 reproduces that shimmer
//    (main.asm)                                 -- one toggle per frame
//
//  MODES. The two reference files differ in more than style, so both are here.
//  '1'/'2' switch live; `run godzi 2` starts in mode 2.
//
//    mode 1 (godzi.asm) -- Godzilla at (150,150), poses alternating 20 frames
//    each. 0.40 s per pose is the reference's 8 movement ticks (8 x 3 IRQ ticks
//    at 59.2 Hz = 0.405 s), so the flap keeps the original period.
//
//    mode 2 (main.asm) -- Godzilla at (240,64), up in the sky: pose A enabled
//    permanently and pose B toggled once per frame, which is exactly what
//    `lda #$08 / eor $d015` did on every non-movement IRQ there -- a 25 Hz
//    shimmer, not an animation. It is reproduced rather than fixed: it is the
//    one thing the two files disagree about on screen, which is why the mode
//    exists at all.
//
//  WARNING: THE ARENA CROSSES x = 256, so x needs NINE bits -- the lesson this demo
//  carries, and the reason it is worth porting. Sprite x runs 24..344 (the
//  40-column window's edges) and the boat is 48 px wide because the reference
//  X-expands it ($D01D = $03), so its left edge belongs in 24..296 -- past 255.
//  The walk therefore carries into the ninth bit, and kSpritePos must receive
//  that bit in the CARRY. Every site that reads x as one byte breaks across
//  x = 256; sprdemo shipped that mistake three times (its header lists all
//  three). Here x is (XH, XL, XF) and the 9th bit is never folded into XL.
//
//  WARNING: NO FRAME LOAD AFTER STARTUP. Both poses are loaded once, into two slots,
//  and the animation is nothing but the enable mask -- the reference's own
//  trick. That is a deliberate choice, not a happy accident: a frame load
//  rewrites a slot while the VIC may be fetching it, which can split that
//  sprite for one frame (~8% per load), so an animation that reloaded a slot
//  every 0.4 s would put a seam on screen every few seconds. Moving the frame
//  DMA into the vblank would remove that; until then an animation belongs in
//  the enable mask, and this demo never needs a second frame load.
//
//  WARNING: THE WALKER IS TWO SPRITES, AND THE SECOND ONE'S COLOUR IS THE FIX. It is
//  the reference's own two-layer stamp: sprite 0 in front (the rigging and the
//  detail lines) over sprite 1 behind (the filled hull), both at the same
//  position, both X-expanded, both HIRES -- and a hires sprite carries exactly
//  ONE colour, so the pair is the only way to get two colours out of that art.
//  Neither reference file ever writes $D028, sprite 1's colour, so its hull fell
//  back to the VIC's reset default and the walker read as a flat black
//  silhouette; the only other omitted colour register is $D02B, sprite 4, which
//  the demo never uses -- so that single gap is the hull, and it is set here.
//  Where the two layers share a pixel the front sprite wins, so the rigging
//  stays black over a coloured hull, and rows 0-7 (identical in both frames)
//  stay black too: only rows 8-15 differ between the layers.
//
//  A merged single sprite was tried first. It is pixel-identical for a
//  MONOCHROME walker and costs a slot less (and it avoids the permanent
//  sprite-sprite collision a co-located pair reports in $D01E) -- but it also
//  makes a second colour impossible, which is exactly what the stamp is for.
//
//  Every iteration yields (Rule 13), so the shell stays usable while it runs.
//
//  MUSIC. Neither reference file has any sound, so the demo borrows the
//  system's: it calls `kSidPlay` (sid.lib, lib 5) DIRECTLY from a thread child of
//  its own -- the arrangement `gortris` uses, and for the same reason: the call
//  BLOCKS (it sleeps a tick per jiffy), so the demo itself cannot make it.
//
//  Both tunes are STREAMED, so neither is ever copied into RAM:
//      mode 1 -> music/commodo.mus      mode 2 -> music/fsonata.mus
//  At startup the two files are only LOOKED UP -- each tune's filesystem offset is
//  kept -- and one three-page window block is taken for the player to page them
//  into, so the RAM cost is the same three pages whatever the tunes' length. A
//  mode switch is three bytes written into the player's config plus a hush, never
//  a load and never an allocation. Nothing needs the filesystem after startup, so
//  `filesys.lib` is handed straight back with `kLibHold` and only `sid.lib` and
//  `sprite.lib` stay held for the session -- and it is why the art and BOTH tunes
//  are located before the hold: `kLibHold` is one-way.
//
//  WARNING: THE CHILD DECLARES ITS OWN ZP WINDOW ($02-$0A). sid.lib's player runs out
//  of that range, and the ZP save/restore is gated on the SWITCHING task's own
//  region: a window declared by this demo could not cover the child, and the
//  child's own restore is skipped at length 0 -- so its player state would be
//  saved by nobody and clobbered by this demo's next restore. This demo's own ZP
//  therefore lives clear of it, at GZ_ZP_BASE ($10).
//
//  WARNING: `q` MUST TAKE THE MUSIC WITH IT, which is why the child is reaped before
//  the exit: it shares this code page, `sysExit` does not reap thread children
//  (only `kill` cascades), and a child parked inside `kSidPlay` would otherwise
//  outlive the exit holding a pointer into a tune buffer that the exit has just
//  freed. The hush (`kSidHush`, inside gzMusicStop) both ends the player and
//  SILENCES the chip -- so `q` leaves no held note and there is no `sidrst` to
//  spawn: the same call that stops the player is the one that quiets the SID.
//
//  WARNING: A SHELL `kill` HAS NO EXIT CODE TO RUN and the cascade kills the child
//  without a hush -- but the drone is still handled: `kill.com` silences the chip
//  itself when the victim (or the task that spawned it) declared `sid.lib`, which
//  is exactly how this demo's declaration reads. `sidrst` remains as the manual
//  fallback for anything else that leaves the SID sounding (docs/lib-sid.md).
//
//  WARNING: WHY THIS BINARY IS NOT RE-RUNNABLE EITHER. `KF_RE_RUNNABLE` is deliberately
//  left off, so a second `run godzi` is refused with `? already running`. Two
//  copies would drive the same chip and the same sprite register set with no way
//  to tell whose music is whose -- one instance's `q` would hush the other's song
//  -- and the sprite half of that argument is nothing at all now: every slot is
//  this task's while its surface is displayed, so there is no slot to claim and
//  no refusal to print. The two demos coexist because the kernel hands the
//  whole sprite state over with the display.
//  point makes before anything else.
//
//  THE SONG BELONGS TO THE MODE -- commodo for mode 1 (godzi.asm's standing
//  Godzilla), fsonata for mode 2 (main.asm's airborne one) -- so `run godzi 2`
//  starts the second song, and switching modes live cuts the current song and
//  starts the other one. Both are held in RAM for the whole session and both play
//  at GZ_MUSIC_DELAY.
// ==========================================
//
// ---- the sentinel for "no child yet" ----
//  Slots are 0..MAX_TASKS-1, so $FF can never be one.
.const GZ_NO_SLOT = $FF
.const GZ_NO_TUNE = $FF          // gzMusTune's "nothing selected yet" value

// ---- music: the player's pace, and how long the exit waits for the child ----
//  delay 1 -- scheduler ticks slept per jiffy AT 100%, i.e. the file's own tempo
//  (one jiffy of music per jiffy). sid.lib scales it by the machine's speed, so
//  the tune runs at the same real-time tempo on any machine (see docs/lib-sid.md).
.const GZ_MUSIC_DELAY = 1
//  The window pages: one 256-byte, page-aligned window per voice, taken once and
//  shared by both tunes (the player re-arms its page tags when it starts, so a
//  switch never plays a page the previous tune left there).
.const GZ_TUNE_WIN_PAGES = 3
//  Frames to let the child leave after a hush before force-killing it. A yield
//  clobbers Y, so the countdown has to live in memory (see gzMusicReap).
.const GZ_MUSIC_REAP = 20

// ---- slots: one sheet record per slot ----
//  The reference's own four-sprite layout: the walker is two overlapping sprites
//  (which is what lets it be two colours), the Godzilla is two poses of which
//  one shows at a time.
.const GZ_BOAT_FRONT = 0        // the walker's rigging/detail -- in FRONT, black
.const GZ_BOAT_HULL  = 1        // the walker's filled hull -- BEHIND, light blue
.const GZ_POSE_A     = 2        // Godzilla, arms down (the reference's godzi1)
.const GZ_POSE_B     = 3        // Godzilla, arms up   (the reference's godzi2)

//  A slot number is just the pose's index: every one of the eight slots is this
//  task's while its surface is displayed (the kernel keeps exactly one task's
//  sprite state live -- the displayed one's).
.macro gzSlot(c) {
    lda #c
}

// ---- the arena is the DISPLAY WINDOW, not sprite coordinates 0..320 ----
//  The 40-column window's left edge is sprite x = 24 and its right edge 344.
//  The boat is 48 px wide (X-expanded), so its left edge belongs in 24..296.
//  y is the reference's own value, and needs no offset.
.const GZ_BXMIN   = 24
.const GZ_BOAT_W  = 48                  // 24 px x 2 (X-expand)
.const GZ_BXMAX   = 344 - GZ_BOAT_W     // 296
.const GZ_BXOVER  = GZ_BXMAX - $100 + 1 // 41: one past the edge means XL >= this
.const GZ_BY      = 200                 // the reference's $C8, unchanged
//  The walk: 0.398 px per displayed FRAME -- the reference's pace. It stepped
//  1 px every third IRQ tick at 59.2 Hz = 19.7 px/s; a display frame is 50.1 Hz,
//  so the same pace is 19.7/50.1 = 0.393 px/frame, and $66/256 = 0.398. Integer
//  1 px/frame would be 2.5x too fast, and stepping a whole pixel only every few
//  frames stutters: the frame clock fixes SAMPLING and the fractional
//  accumulator fixes QUANTISATION -- a smooth motion needs both (plan section 5.6).
.const GZ_STEP    = $66

// ---- Godzilla: the two reference files place it differently ----
.const GZ_M1_X    = 150         // godzi.asm: $96,$96 -- standing, mid-screen
.const GZ_M1_Y    = 150
.const GZ_M2_X    = 240         // main.asm: $F0,$40 -- up in the sky, right side
.const GZ_M2_Y    = 64

// ---- colours: the reference's silhouettes, plus the hull it forgot to colour ----
//  Both reference files set $D027/$D029/$D02A to 0 and never write $D028, which
//  is sprite 1 -- the walker's hull. Setting it is the one deliberate departure
//  from the reference, and it is what makes the two-layer stamp visible at all:
//  leave it black and both layers are the same colour, so the walker is a solid
//  silhouette and the layering can only be seen when it crosses a coloured
//  background.
.const GZ_COL_FRONT = 0         // sprite 0: the rigging/detail -- black ($D027)
.const GZ_COL_HULL  = 14        // sprite 1: the hull -- light blue ($D028)
.const GZ_COL_GODZI = 0         // sprites 2 and 3: the reference's black
//  BOTH walker layers are X-expanded -- the reference's $D01D = $03 -- which is
//  why the walker's arena ends at 296 and not 320 (see GZ_BXMAX above).
.const GZ_XEXPAND   = %00000011

// ---- enable mask: one bit per slot, the walker's two layers first ----
//  The layers are always enabled together -- the walker is one figure and only
//  its colours differ -- so nothing here ever toggles them individually.
.const GZ_EN_BOAT  = %00000011  // slots 0 + 1: rigging + hull
.const GZ_EN_BIT_A = %00000100  // slot 2: pose A
.const GZ_EN_BIT_B = %00001000  // slot 3: pose B
.const GZ_EN_A     = GZ_EN_BOAT | GZ_EN_BIT_A  // mode 1, first half of the cycle
.const GZ_EN_B     = GZ_EN_BOAT | GZ_EN_BIT_B  // mode 1, second half
.const GZ_EN_BOTH  = GZ_EN_BOAT | GZ_EN_BIT_A | GZ_EN_BIT_B  // mode 2

//  Mode 1 flips pose every 20 frames -- 20/50.1 Hz = 0.40 s, the reference's
//  eight movement ticks (8 x 3 IRQ ticks at 59.2 Hz = 0.405 s).
.const GZ_FLAP     = 20
.const GZ_ANIM_MAX = GZ_FLAP * 2

godzi:
    jsr gzArgs                    // mode from "run godzi [1|2]", default 1
    lda #0                        // hires-text surface (the display is all-bitmap)
    ldx #$05; stx kArg            // border: green -- stands in for the reference's
    ldx #$07; stx kArg+1          // bg0: yellow      4-row ground band
    stx kArg+2                    // bg1/bg2 are unused in hires, but kAllocScreen
    stx kArg+3                    // reads all four
    jsr kAllocScreen
    bcs gzNoScreen
    jmp gzScreen
gzNoScreen:
    lda #SYS_YIELD; jsr syscall   // no surfaces left: idle forever
    jmp *-3

gzScreen:
    jsr kDisableCursor            // a graphics surface: no cursor, and no text
    lda #SYS_GETTID
    jsr syscall
    tax
    //  WARNING: TAKE THE DISPLAY WITHOUT TAKING THE OUTGOING CURSOR BLOCK WITH IT.
    //  Same recipe and the same reason as sprdemo: the blink block is
    //  drawn into framebuffer PIXELS, kSwitchView copies the framebuffer into the
    //  outgoing surface's REU slot, and the kernel then clears cursorVisible -- so
    //  a block alive at this instant becomes permanent content that only a pixel
    //  write can remove. kBlinkReset un-draws it, and the SEI is not decoration:
    //  an IRQ tick between the erase and the switch would draw it straight back.
    //  No syscall may be used inside the window -- the dispatcher re-enables IRQs.
    php
    sei
    txa
    pha
    jsr kBlinkReset               // un-draw a live blink block (no-op if OFF)
    pla
    tax
    jsr kSwitchView               // display it + take kbFocus
    plp
    jsr gzLoadArt                 // three frames, one slot each
    jsr gzSetup                   // colours, expand, positions, starting x
    //  Music: load BOTH tunes, hand filesys back, start the child -- and only
    //  then pick the mode's tune (the child has to exist to be asked). See the
    //  MUSIC note in the header and gzMusicInit below.
    jsr gzMusicInit
    lda GZ_MODE
    sec
    sbc #1                        // mode 1 -> tune 0 (commodo), mode 2 -> tune 1
    jsr gzMusicTune               // point at it and ask the child to play
gzLoop:
    jsr kReadKey                  // non-blocking: A = screen code, 0 = none
    beq gzFrame
    cmp #'q'
    beq gzExit
    sec
    sbc #'1'                      // '1' -> 0, '2' -> 1, anything else -> C set
    cmp #2
    bcc gzSwitch
gzFrame:
    //  WARNING: ONE UPDATE PER FRAME, not per scheduler tick. The tick runs at 59.2 Hz
    //  and the display samples at 50.1 Hz, so a per-tick animation gets 1.18
    //  updates per frame -- some frames show two steps, the next shows none, which
    //  reads as a stutter no matter what the speed arithmetic does. `frameCount`
    //  is incremented by the kernel's raster commit at line 0, so it is the
    //  display's own clock; gating on it makes the displayed motion exactly the
    //  sequence computed here, one step per frame.
    lda frameCount
    cmp GZ_FRAME
    beq gzWait                    // same frame as last time -> nothing to do
    sta GZ_FRAME
    jsr gzTick
gzWait:
    lda #SYS_YIELD
    jsr syscall                   // never starve the scheduler
    jmp gzLoop

gzSwitch:
    adc #1                        // 1 or 2 (C is clear on this path)
    cmp GZ_MODE
    beq gzSwitchDone              // the mode we are already in (key repeat)
    sta GZ_MODE
    jsr gzPlaceGodzi              // the one thing that differs between modes...
    lda GZ_MODE                   // ...and the song, which is per mode
    sec
    sbc #1                        // mode 1 -> tune 0 (commodo), mode 2 -> tune 1
    jsr gzMusicTune               // hush + restart on the new tune (see below)
gzSwitchDone:
    jmp gzFrame

gzExit:
    //  Take the music, and the SID with it. `kSidHush` inside gzMusicStop
    //  silences the chip as well as ending the player, so -- unlike the old
    //  spawn-a-sidplay arrangement -- there is no held note to clear and no
    //  sidrst.com to spawn: the hush is the same call that stops the player, and
    //  the child is reaped before the tune buffers go. See gzMusicBye.
    jsr gzMusicBye
gzExitGo:
    lda #SYS_EXIT
    jsr syscall

// ==========================================
//  gzArgs -- "run godzi [1|2]"
// ==========================================
//  The reference had no modes at all, so this is new plumbing rather than a
//  port. The first '1' or '2' anywhere in the argument string wins; no argument
//  (or no digit) means mode 1, so a bare `run godzi` behaves like godzi.asm.
gzArgs:
    lda #1
    sta GZ_MODE
    lda dataPtr
    ora dataPtr+1
    beq gzArgsDone                // no instance buffer -> default
    ldy #0
gzArgsLoop:
    lda (dataPtr),y
    beq gzArgsDone                // the loader null-terminates the copy
    sec
    sbc #'1'                      // '1' -> 0, '2' -> 1, other chars -> C set
    cmp #2
    bcc gzArgsSet
    iny
    bne gzArgsLoop
gzArgsDone:
    rts
gzArgsSet:
    adc #1                        // C is clear here, so this is 0+1 or 1+1
    sta GZ_MODE
    rts

// ==========================================
//  gzLoadArt -- one sheet record per slot, once
// ==========================================
//  ~1,000-3,000 cycles per kSpriteSheet and SEI'd, paid once at entry. The
//  record is per SLOT, so godzi.spr is resolved twice -- once for
//  each pose slot -- because kSpriteFrame reads the record of the slot it is
//  filling, not of the file it came from.
gzLoadArt:
    lda #<gzBoatName
    sta kPtr
    lda #>gzBoatName
    sta kPtr+1
    gzSlot(GZ_BOAT_FRONT)
    jsr kSpriteSheet
    gzSlot(GZ_BOAT_HULL)
    jsr kSpriteSheet              // a record is per SLOT: one file, two slots
    gzSlot(GZ_BOAT_FRONT)
    ldx #0
    jsr kSpriteFrame              // frame 0 = the reference's sprite 0 (front)
    gzSlot(GZ_BOAT_HULL)
    ldx #1
    jsr kSpriteFrame              // frame 1 = its sprite 1 (the hull)
    lda #<gzGodziName
    sta kPtr
    lda #>gzGodziName
    sta kPtr+1
    gzSlot(GZ_POSE_A)
    jsr kSpriteSheet
    gzSlot(GZ_POSE_B)
    jsr kSpriteSheet
    gzSlot(GZ_POSE_A)
    ldx #0
    jsr kSpriteFrame              // frame 0 = arms down
    gzSlot(GZ_POSE_B)
    ldx #1
    jmp kSpriteFrame              // frame 1 = arms up -- tail call

// ==========================================
//  gzSetup -- colours, geometry, starting state
// ==========================================
gzSetup:
    lda #GZ_BXMIN                 // the walk starts at the window's left edge
    sta GZ_XL
    lda #0
    sta GZ_XF
    sta GZ_XH
    sta GZ_ANIM
    //  Colour, set EXPLICITLY: a sprite whose colour matches the background is
    //  drawn perfectly and is completely invisible, which is the worst kind of
    //  bug (sprdemo hit it first). The reference leaves every sprite it uses at
    //  0 -- black silhouettes -- which is why the sky here is yellow and not
    //  black; the hull is the one exception (see GZ_COL_HULL).
    //  The shadow offset is per-SLOT, so it takes the claimed base too.
    gzSlot(SPR_SHD_COLOUR + GZ_BOAT_FRONT)
    tax
    lda #GZ_COL_FRONT
    jsr kSpriteReg
    gzSlot(SPR_SHD_COLOUR + GZ_BOAT_HULL)
    tax
    lda #GZ_COL_HULL
    jsr kSpriteReg
    gzSlot(SPR_SHD_COLOUR + GZ_POSE_A)
    tax
    lda #GZ_COL_GODZI
    jsr kSpriteReg
    gzSlot(SPR_SHD_COLOUR + GZ_POSE_B)
    tax
    lda #GZ_COL_GODZI
    jsr kSpriteReg
    //  X-expand the walker's layers only -- the reference's $D01D = $03, which is
    //  sprites 0 and 1, i.e. exactly these two -- never the Godzilla.
    sprXExpand(GZ_XEXPAND)
    sprPriority(%00000000)        // in front of the background, as the reference
    jsr gzPaceInit              // the walk and the cadence for this machine
    jsr gzPlaceGodzi
    jsr gzPlaceBoat               // also makes the first frame a valid position
    sprOn(GZ_EN_A)                // pose A + boat -- the next tick takes over
    rts

// ==========================================
//  gzTick -- one displayed frame
// ==========================================
gzTick:
    jsr gzWalk
    jsr gzPlaceBoat
    //  The counter advances in BOTH modes: mode 1 uses it for the pose cycle, and
    //  it is also the music-retry cadence, so it must keep running when the pose
    //  stops needing it. gzCadence is that many of THIS machine's frames.
    inc GZ_ANIM
    lda GZ_ANIM
    cmp gzCadence
    bcc gzPose
    lda #0
    sta GZ_ANIM
    //  ---- mode 1: flip the pose every GZ_FLAP frames ----
gzPose:
    lda GZ_MODE
    cmp #2
    beq gzTickSky
    lda #GZ_EN_A
    ldx GZ_ANIM
    cpx #GZ_FLAP
    bcc gzEnable                  // first half of the cycle -> pose A
    lda #GZ_EN_B
    jmp gzEnable
    //  ---- mode 2: pose B blinks in and out once per frame ----
gzTickSky:
    //  The phase comes from the display clock itself, so it needs no state of
    //  its own: odd frames show both poses, even frames pose A alone.
    lda frameCount
    lsr                           // C = frameCount bit 0
    lda #GZ_EN_A
    bcc gzEnable
    lda #GZ_EN_BOTH
gzEnable:
    ldx #SPR_SHD_ENABLE
    jsr kSpriteReg
    rts                           // and nothing else per frame: the tune is the
                                  //   child's business now -- the player repeats
                                  //   it by itself (kArg+1 = 0), so there is no
                                  //   end-of-song to watch for. See MUSIC above.

//  The walk: x += GZ_STEP, three bytes, so the carry out of the low byte runs
//  through $0100 and the 9th bit with no special case anywhere.
gzWalk:
    lda GZ_XF
    clc
    adc gzStep
    sta GZ_XF
    lda GZ_XL
    adc #0
    sta GZ_XL
    lda GZ_XH
    adc #0
    sta GZ_XH
    lda GZ_XH                     // still below 256? then it cannot be past the
    beq gzWalkDone                // right edge either
    cmp #1
    bne gzWalkWrap                // 2+: impossibly far past the edge -- wrap
    lda GZ_XL
    cmp #GZ_BXOVER                // 41: x > 296 means XL >= 41
    bcc gzWalkDone
gzWalkWrap:
    //  The reference teleports back to the start rather than bouncing, and so
    //  does this: the walk is a lap, not a bounce.
    lda #GZ_BXMIN
    sta GZ_XL
    lda #0
    sta GZ_XF
    sta GZ_XH
gzWalkDone:
    rts

// ==========================================
//  Pacing -- the game's own frames, scaled to the machine
// ==========================================
//  gzStep and gzCadence are derived from GZ_STEP and GZ_ANIM_MAX so the walk and
//  the pose cycle keep the real-time pace of the game's own frames -- one step per
//  displayed frame, which is what a real C64 runs and what the C64U image declares
//  -- on a machine that declares itself faster or slower (`speed`, see
//  docs/quick-start.md). It is the rule sid.lib applies to a tune's ticks-per-jiffy
//  delay, for the same reason: a faster machine's frames are shorter, so it waits
//  more of them and steps a smaller distance in each.
gzPaceInit:
    lda #GZ_ANIM_MAX
    jsr gzPaceN                 // a count of frames
    sta gzCadence
    lda #GZ_STEP
    jsr gzPaceR                 // a rate per frame
    sta gzStep
    rts

//  gzPaceN -- A = the game's own frame count -> A = this machine's.
gzPaceN:
    ldy sysSpeedShift           // +1 = 200%, 0 = 100%, -1 = 50%
    beq gpnRet                  // 100%: the game's own count
    bmi gpnFewer                // slower: longer frames, fewer of them
gpnMore:                        // faster: shorter frames, more of them
    asl
    bcs gpnMax
    dey
    bne gpnMore
    rts
gpnMax:
    lda #255
    rts
gpnFewer:
    lsr
    bne gpnNext
    lda #1                      // never expire instantly
    rts
gpnNext:
    iny
    bne gpnFewer
gpnRet:
    rts

//  gzPaceR -- A = the game's own rate per frame -> A = this machine's: the mirror
//  of gzPaceN, because a shorter frame covers less ground.
gzPaceR:
    ldy sysSpeedShift
    beq gprRet
    bpl gprDown
gprUp:
    asl
    bcs gprMax
    iny                         // Y counts up from the negative shift
    bne gprUp
    rts
gprMax:
    lda #255
    rts
gprDown:
    lsr
    bne gprNext
    lda #1                      // a step with no half: one is the floor
    rts
gprNext:
    dey
    bne gprDown
gprRet:
    rts

//  Show the walker: both layers, at the same position. kSpritePos takes the
//  sprite in X, x in A with the 9th bit in the CARRY, and y in Y -- and it
//  clobbers all three, so nothing may be left live in them across the call: the
//  arguments are re-derived for the second layer rather than saved.
gzPlaceBoat:
    gzSlot(GZ_BOAT_FRONT)
    tax
    jsr gzBoatXY
    jsr kSpritePos
    gzSlot(GZ_BOAT_HULL)
    tax
    jsr gzBoatXY
    jmp kSpritePos                // tail call
gzBoatXY:
    //  A = x, Y = y, C = the ninth bit. X is untouched -- it carries the slot.
    lda GZ_XH
    lsr                           // C = x bit 8 (XH is only ever 0 or 1)
    lda GZ_XL                     // A = the integer's low byte
    ldy #GZ_BY
    rts

//  Both poses share one position, so it is set for each slot in turn.
gzPlaceGodzi:
    gzSlot(GZ_POSE_A)
    tax
    jsr gzGodziXY
    jsr kSpritePos                // clobbers A, X, Y -- hence the re-derivation
    gzSlot(GZ_POSE_B)
    tax
    jsr gzGodziXY
    jmp kSpritePos
gzGodziXY:
    //  A = x, Y = y for the current mode, carry always clear: both reference x
    //  values are below 256 (the asserts at the foot of this file pin that).
    lda GZ_MODE
    cmp #2
    beq gzGodziXY2
    lda #GZ_M1_X
    ldy #GZ_M1_Y
    clc
    rts
gzGodziXY2:
    lda #GZ_M2_X
    ldy #GZ_M2_Y
    clc
    rts

// ==========================================
//  MUSIC -- sid.lib called directly, from a thread child
// ==========================================
//  See the MUSIC note in the header for the design. The demo calls these; the
//  child is reached only through spawnPtr:
//      gzMusicInit   once at startup -- locate both tunes, take the windows, kLibHold,
//                    spawn the child
//      gzMusicTune   startup + mode switch -- point the config at a tune and play it
//      gzMusicStart  ask for music (gzMusicTune's last step)
//      gzMusicStop   hush the chip and ask the child to leave the player loop
//      gzMusicBye    the exit -- quit, stop, reap the child, free the windows

//  gzMusicInit -- locate BOTH tunes, take the window pages, hand filesys.lib back,
//  start the child.
//  Every failure here is simply "no music": the demo is already on screen, and a
//  tune is not worth an error the player has to read and dismiss. A mode whose
//  tune is not in the FS falls back to the other one (see gzMusicTune), and if
//  neither is there, no child is started at all.
gzMusicInit:
    ldy #0                       // not found -> that tune's gzTuneOk stays 0
    lda #<gzTune1Name
    ldx #>gzTune1Name
    jsr gzLoadTune
    ldy #1
    lda #<gzTune2Name
    ldx #>gzTune2Name
    jsr gzLoadTune
gzMiHold:
    //  WARNING: BOTH tunes are LOCATED, so filesys.lib has done its job -- and a declared
    //  library stays resident until the task EXITS, which would keep its pages
    //  (~10 of them) out of the pool for the whole session. kLibHold hands
    //  everything back except sid.lib and sprite.lib. The SPRITE library has to
    //  be in the keep mask because the per-frame kSpritePos and kSpriteReg calls
    //  below are its entries: a released library stops serving the task that gave
    //  it back, and the demo would sit on screen with nothing moving.
    //  It is a ONE-WAY call: nothing here may touch the FS afterwards, which is
    //  exactly why the art (gzLoadArt) and both tunes are located first, and why a
    //  mode switch only rewrites three bytes of the player's config.
    lda #(1 << LIB_SID) | (1 << LIB_SPRITE)
    jsr kLibHold
    //  Start the child only if there is something for it to play.
    lda gzTuneOk+0
    ora gzTuneOk+1
    beq gzMiDone                 // no tune at all: no child, no music
    //  The window pages: one 256-byte window per voice, shared by both tunes --
    //  the player re-arms its own page tags when it starts, so nothing stale is
    //  ever played after a switch.
    lda #GZ_TUNE_WIN_PAGES
    jsr kMalloc                  // -> kPtr = the block
    bcs gzMiDone                 // no pool room: no child either, and no music
    lda kPtr
    sta gzTuneBufLo
    lda kPtr+1
    sta gzTuneBufHi
    ldy #3                       // config +3..+8: the windows, lo/hi interleaved.
    ldx #0                       //   The block is page-aligned, so all three low
gzMiWin:                         //   bytes are its own; the high bytes count up
    lda gzTuneBufLo
    sta gzTuneCfg,y
    iny
    txa
    clc
    adc gzTuneBufHi
    sta gzTuneCfg,y
    iny
    inx
    cpx #GZ_TUNE_WIN_PAGES
    bne gzMiWin
    lda #<gzTuneCfg              // the child hands the player the CONFIG
    sta gzTuneLo
    lda #>gzTuneCfg
    sta gzTuneHi
    lda #<gzMusicThread
    sta spawnPtr
    lda #>gzMusicThread
    sta spawnPtr+1
    lda #GZ_ZP_TUNE_BASE
    sta kArg+6                   // the CHILD's ZP window: sid.lib's scratch range
    lda #GZ_ZP_TUNE_LEN          //   ($02-$0A, what sidplay.tsk declares). It has
    sta kArg+7                   //   to be the child's own registration, because
    lda #SYS_THREADSPAWN         //   the save/restore is gated on the switching task.
    jsr syscall
    bcs gzMiDone                 // no free slot: the demo runs without music
    stx gzMusSlot                // X = the child's slot
gzMiDone:
    rts

//  gzLoadTune -- A/X = the FS name pointer (A = lo, X = hi), Y = the tune index ->
//  that tune's filesystem offset in gzFsLo/Mid/Hi,y and gzTuneOk,y = 1, or C=1 if
//  it is not in the FS (nothing is copied: sid.lib pages the file in later).
//  kReuFindFile clobbers kPtr and kArg, so the name arrives in registers and is
//  set in kArg for the call that wants it; the index rides in gzTuneIx, because
//  the routine needs Y for the stores.
gzLoadTune:
    sty gzTuneIx
    sta gzNameLo
    stx gzNameHi
    sta kArg
    stx kArg+1
    jsr kReuFindFile             // C=1 not in the FS; the offset lands in reuFsPtr
    ldy gzTuneIx                 // (ldy does not disturb C)
    bcs gzLtDone
    lda reuFsPtr
    sta gzFsLo,y
    lda reuFsPtr+1
    sta gzFsMid,y
    lda reuFsPtr+2
    sta gzFsHi,y
    lda #1
    sta gzTuneOk,y
    clc
gzLtDone:
    rts
//  gzMusicStart -- ask the child for music (it plays the tune from the top).
gzMusicStart:
    lda gzTuneLo
    ora gzTuneHi
    beq gzMsDone                 // no tune in RAM
    lda gzMusSlot
    cmp #GZ_NO_SLOT
    beq gzMsDone                 // no child: nothing to ask
    lda #1
    sta gzMusOn
gzMsDone:
    rts

//  gzMusicStop -- silence the chip AND ask the child to stop. Clearing gzMusOn
//  alone does nothing: the child is parked inside kSidPlay's sleep loop, and the
//  hush is what gets it out (its player sees sidStop and returns).
gzMusicStop:
    lda #0
    sta gzMusOn
    jmp kSidHush                 // tail call: silences the SID

//  gzMusicTune -- A = the tune this mode wants: 0 = commodo, 1 = fsonata.
//  Switching is: remember it, hush (which is what unblocks the child -- see
//  gzMusicStop), write that tune's filesystem offset into the player's config, and
//  ask for music again. gzTuneOk says which tunes are actually in the FS, so a
//  missing one falls back to tune 0; with neither there gzTuneLo/Hi are 0 and
//  gzMusicStart quietly does nothing.
//  WARNING: the config is rewritten while the child is still inside the previous
//  kSidPlay -- that is safe because the player COPIES the nine bytes at entry, so
//  the running tune is unaffected; only the next call reads the new offset.
//  Clobbers A, X.
gzMusicTune:
    cmp gzMusTune
    beq gzMtuDone                // already playing it: leave the song alone
    sta gzMusTune
    jsr gzMusicStop
    ldx gzMusTune
    lda gzTuneOk,x
    bne gzMtuSel                 // this tune is in the FS
    ldx #0                       // it is not: fall back to tune 0 (commodo)
    lda gzTuneOk
    bne gzMtuSel
    lda #0                       // neither is: nothing to play
    sta gzTuneLo
    sta gzTuneHi
    lda #GZ_NO_TUNE              // ...and a later request must re-run this test
    sta gzMusTune
    rts
gzMtuSel:
    lda gzFsLo,x                 // config +0..+2: this tune's FS offset
    sta gzTuneCfg
    lda gzFsMid,x
    sta gzTuneCfg+1
    lda gzFsHi,x
    sta gzTuneCfg+2
    lda #<gzTuneCfg
    sta gzTuneLo
    lda #>gzTuneCfg
    sta gzTuneHi
gzMtuGo:
    jsr gzMusicStart
gzMtuDone:
    rts

//  gzMusicBye -- the exit path: leave the player loop for good, make sure the
//  child really is gone, then hand the window pages back. The order matters: the
//  child can still be inside the player, reading those windows, so the free is
//  last -- and the reap is what makes that safe (see gzMusicReap).
//  Clobbers A, X, Y (kFree clobbers X).
gzMusicBye:
    lda #1
    sta gzMusQuit
    jsr gzMusicStop
    jsr gzMusicReap
    lda gzTuneBufLo
    ora gzTuneBufHi
    beq gzMbDone                 // no windows were taken
    lda gzTuneBufLo
    sta kPtr
    lda gzTuneBufHi
    sta kPtr+1
    jsr kFree
    lda #0                       // do not leave a pointer to a freed block
    sta gzTuneBufLo
    sta gzTuneBufHi
gzMbDone:
    rts

//  gzMusicReap -- wait for the child to leave, and kill it if it does not. It
//  shares this code page, so it MUST NOT outlive the demo: sysExit does not reap
//  thread children (only `kill` cascades), and a child parked by
//  `pause godzi.N` would never come back on its own. The countdown lives in
//  memory because a yield clobbers Y.
gzMusicReap:
    ldx gzMusSlot
    cpx #GZ_NO_SLOT
    beq gzMrDone
    lda #GZ_MUSIC_REAP
    sta gzReapCnt
gzMrWait:
    lda taskState,x
    beq gzMrGone                  // it left (DEAD), which is what we asked for
    lda #SYS_YIELD
    jsr syscall                   // X survives a yield; A and Y do not
    dec gzReapCnt
    bne gzMrWait
    ldx gzMusSlot
    lda #SYS_KILL
    jsr syscall                   // it never woke: force it off our code page
gzMrGone:
    lda #GZ_NO_SLOT
    sta gzMusSlot
gzMrDone:
    rts

//  gzMusicThread -- the child. It READS the demo's bytes (gzMusOn, gzMusQuit,
//  gzTuneLo/Hi) and writes only gzMusOn, and it never touches the demo's ZP,
//  because the two share this code page and only one of them runs at a time.
//  Reached ONLY through spawnPtr -- the demo itself never falls in here.
//
//  Loop: leave for good on gzMusQuit; otherwise play when asked and idle (with a
//  yield, never a spin) when not. A refused kSidPlay means another task owns the
//  SID: drop the request rather than retry it -- a mode switch asks again.
gzMusicThread:
    lda gzMusQuit
    bne gzMtExit
    lda gzMusOn
    beq gzMtIdle
    lda gzTuneLo
    sta kPtr
    lda gzTuneHi
    sta kPtr+1
    lda #GZ_MUSIC_DELAY
    sta kArg                     // ticks slept per jiffy AT 100%
    lda #0
    sta kArg+1                   // 0 = THE PLAYER repeats the tune when it ends,
    lda #0                       // no transpose: sid.lib answers the machine's own
    sta kArg+2                   //   pitch shift itself (+1 is one octave DOWN)
    lda #0                       // no semitone offset
    sta kArg+3
    lda #1                       // mode: the tune is paged from the REU
    jsr kSidPlay                 //   so this loop never has to watch for that
    bcc gzMusicThread            // returned: hushed, or the tune finished
    lda #0                       // refused: another task owns the SID
    sta gzMusOn
    jmp gzMusicThread
gzMtIdle:
    lda #SYS_YIELD
    jsr syscall                   // never starve the scheduler
    jmp gzMusicThread
gzMtExit:
    lda #SYS_EXIT
    jsr syscall                   // exit -- never returns

// ==========================================
//  Data
// ==========================================
//  The sheets live in the REU filesystem, seeded by tools/build-reu.ps1 from
//  assets/sprites (the .txt art is the source of truth; tools/gen-sprites.py
//  turns it into the .spr). Names are compared in screen codes, which is what
//  .text produces under screencode_mixed -- the same encoding the FS seeds with.
gzBoatName:  .text "boat.spr"
             .byte 0
gzGodziName: .text "godzi.spr"
             .byte 0

//  ---- the tunes, and the child that plays them ----
//  Both are LOCATED once, at startup (see gzMusicInit): a mode switch must not
//  need the filesystem, because filesys.lib is handed back with kLibHold(5) the
//  moment both are found. The files themselves stay in the REU and sid.lib pages
//  them in.
//
//  WARNING: THIS STATE IS CODE-IMAGE, PER INSTANCE, AND SHARED WITH THE CHILD -- which
//  is why none of it is in ZP: ZP is virtualized per task, so the child could not
//  see the demo's copy. The child runs from this same code page and reads these
//  bytes directly. Names are screen codes (.text under screencode_mixed), the
//  same encoding the FS seeds with.
gzTune1Name: .text "commodo.mus"
             .byte 0
gzTune2Name: .text "fsonata.mus"
             .byte 0
gzMusOn:     .byte 0             // 1 = play, 0 = be silent      (demo -> child)
gzMusQuit:   .byte 0             // 1 = leave for good         (demo -> child)
gzMusSlot:   .byte GZ_NO_SLOT    // the child's slot, $FF = none
gzMusTune:   .byte GZ_NO_TUNE    // the tune the mode wants -- $FF until the first
                                 //   gzMusicTune, so that first call always starts
gzTuneLo:    .byte 0             // the config block the child plays (0 = nothing)
gzTuneHi:    .byte 0
gzTuneCfg:   .fill 9, 0          // sid.lib's streamed config: the tuned file's FS
                                 //   offset (rewritten per tune) then three
                                 //   256-byte, page-aligned window pointers
gzFsLo:      .byte 0, 0          // each tune's FS offset: [0] commodo, [1] fsonata
gzFsMid:     .byte 0, 0
gzFsHi:      .byte 0, 0
gzTuneOk:    .byte 0, 0          // 1 = that tune is in the FS
gzTuneIx:    .byte 0             // gzLoadTune: the tune index, while it works
gzNameLo:    .byte 0             // gzLoadTune in: the FS name pointer
gzNameHi:    .byte 0
gzTuneBufLo: .byte 0             // the window pages, taken once at startup
gzTuneBufHi: .byte 0
gzReapCnt:   .byte 0             // countdown while waiting for the child to exit
gzStep:      .byte 0             // the walk step, for this machine (gzPaceInit)
gzCadence:   .byte 0             // the pose/music cadence, for this machine

//  The assert lives down here because KickAssembler evaluates `.assert` in the
//  FIRST parse -- it cannot see a label the assembler has not reached yet.
//  (Three-argument form: the two-argument form is a parser trap, and a boolean
//  expression does not survive the first parse either.)
.assert "godzi: the 48 px boat must end exactly on the window's right edge", (GZ_BXMAX + GZ_BOAT_W == 344) ? 1 : 0, 1
.assert "godzi: both walker layers must be X-expanded -- that is the 48 px", ((GZ_XEXPAND & GZ_EN_BOAT) == GZ_EN_BOAT) ? 1 : 0, 1
.assert "godzi: the walk must cross x = 256 -- that is this demo's whole point", (GZ_BXMAX > 255) ? 1 : 0, 1
.assert "godzi: Godzilla's x fits one byte, so gzGodziXY may clear carry", ((GZ_M1_X < 256) ? 1 : 0) * ((GZ_M2_X < 256) ? 1 : 0), 1
.assert "godzi: the pose cycle is counted in a single byte", (GZ_ANIM_MAX < 255) ? 1 : 0, 1
