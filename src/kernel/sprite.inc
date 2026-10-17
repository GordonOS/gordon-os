// ==========================================
//  sprite.inc -- sprite slots, sheet records, frame loading
// ==========================================
//  THIS FILE OWNS ITS OWN .pc BLOCKS (like char-shadow.inc / libstubs.inc /
//  dynload.inc), because the routines do not fit in one pocket. main.asm asserts
//  every end against its neighbour:
//    $CD9F-$CDFF  the install helpers + the five public stubs + sprPtrBytes
//                 sprBlkAddr + sprRestoreFrames at $CD9F, then all FIVE public
//                 entry stubs (15B, $CDDE-$CDEC) and sprPtrBytes (8B,
//                 $CDED-$CDF4). The stubs live here rather than in the
//                 $FF7E-$FFF9 run because that run is the gfx scratch's growth
//                 headroom; four of the five are the library's call entries.
//    $1CC0-$1D3F  sprFrameDma          86B of a 128B pocket -- below the data
//                                     heap at $1D40
//    $52DC-$533F  sprInstall + the state mover, in the loader band
//    $567D-$56FF  rasterCommit        129B of a 131B pocket -- the free run below
//                                     the task pool, between scrollScreenSlot
//                                     and POOL_BASE ($5700)
//    $CFA4-$CFFF  tables + state      92B of a 92B pocket -- after char-shadow
//
//  THE MODEL. A sprite's pixels can only be fetched by the VIC from a
//  64-byte-aligned block inside its 16KB bank, so the art lives in the REU
//  FS and one frame at a time is DMA'd into a *slot*. The slots do not move
//  and are not allocated -- there are 8, fixed, and every one is an ordinary
//  task sprite (SPRITE_TASK_SLOTS). No slot is reserved: a task that wants a
//  mouse pointer drives a sprite like anything else, so holding one back would
//  cost every task 1/8 of the hardware.
//
//  A "sheet" is a plain file of consecutive 64-byte frames (frame N at
//  offset N*64) -- the same shape as the REU font (glyph = fontRec + char*8),
//  which is why this is 3 routines and not a cache. Resolve a sheet once per
//  sprite, load a frame per animation step.
//
//  NOT a cache: a slot holds exactly one frame and is overwritten on demand,
//  so there is no eviction policy and nothing is retained between frames.
//
//  PER-TASK STATE. The eight slots are shared by NUMBER, so only one task's
//  sprite state can be live in the register set -- and that task is the one
//  whose surface is DISPLAYED. Everything a task has to say about its sprites
//  (the sheet records, the 39-byte shadow and the frame indices) therefore
//  lives in one REU block per task slot, and `sprInstall` moves the display
//  owner's block into kernel RAM when the display changes hands; the block's
//  fields ARE the live fields, in the same order, so a hand-over is two DMAs.
//  Nothing is allocated and nothing is masked: a task owns all eight slots
//  while it is displayed, and the kernel publishes exactly its shadow.
//
//  The task-facing calls (sheet, frame, pos, reg) are NOT here -- they are
//  sprite.lib, because their cost is then charged only to the tasks that
//  declare it. What cannot move is the raster side: the shadow, the install
//  and the commit, none of which can run from a library (no library can be
//  paged in from an interrupt).

// --- slot addresses: VIC bank 0 ($0000-$3FFF), 64-byte aligned ---
//  THERE ARE EIGHT -- the hardware maximum. Two bands hold them:
//    * $02A1-$03FF is the low band (EhBASIC owns $0200-$02A0 below it, the
//      video matrix starts at $0400 above).
//    * $0F00-$0FBF is the high band, in the kernel core's old space.
//  $1000-$1FFF CANNOT hold frames: it is a CHARACTER ROM MIRROR in VIC bank 0 --
//  the VIC reads ROM, not RAM, for every fetch type there, sprite data included.
//  The same 64B pattern renders correctly in $02C0 but shows font bytes in
//  $17C0. $2000-$3FFF is the framebuffer, DMA'd wholesale per surface by
//  surfSave/surfLoad, so its tail is not free either.
//
//  Every slot is an ordinary task sprite: slot N is simply address N of the
//  table below, and there is no reserved one (see SPRITE_TASK_SLOTS). Slot 4 is
//  $03C0 (the tail of the low band) and slots 5-7 are the high band, so the two
//  bands are both full.
.const SPRITE_SLOT0 = $02C0
.const SPRITE_SLOT1 = $0300
.const SPRITE_SLOT2 = $0340
.const SPRITE_SLOT3 = $0380
.const SPRITE_SLOT4 = $03C0
//  Slots 5-7 use the high band $0F00-$0FBF. With them the count reaches the
//  HARDWARE MAXIMUM of 8, so $0FC0 is deliberately NOT a slot: it stays free
//  as relocation slack for whatever grows next.
.const SPRITE_SLOT5 = $0F00
.const SPRITE_SLOT6 = $0F40
.const SPRITE_SLOT7 = $0F80

//  ALL EIGHT SLOTS ARE TASK SPRITES -- no slot is reserved. A task that wants a
//  mouse pointer drives a sprite like any other, which is exactly what the API
//  is for; reserving a slot for a feature nobody has built would cost every
//  task one sprite. The public calls accept 0-7 and reject only 8+.
//  Also defined in jumptable.inc, which the task build imports instead of this
//  file -- the two never share an assembly.
.const SPRITE_TASK_SLOTS = 8
.const SPRITE_SLOT_COUNT = SPRITE_TASK_SLOTS

//  $D015 mask for the commit: only slots that exist may be enabled. A task can
//  write sprOn($FF); the channels with no slot behind them must stay dark, or
//  the VIC fetches their data from a pointer byte nothing maintains.
//  WARNING: At 8 slots this is $FF -- ALL EIGHT channels have real data -- so the
//  commit does not apply it. It is kept (and asserted) as the statement of the
//  invariant: slot count == hardware maximum. If a future layout drops a slot,
//  the assert fails and the `and` must come back to $d015.
.const SPR_ENABLE_MASK = (1 << SPRITE_SLOT_COUNT) - 1

//  The commit deadline -- the raster line past which publishing a new X is no
//  longer safe. A sprite at Y is first displayed on line
//  Y+1, and the earliest Y a task can usefully use is 31 on PAL (32 on screen,
//  with the top border above it), so ~line 30 is the last line at which the
//  whole sprite row is still ahead of the beam. Past this the guard skips the
//  commit entirely rather than split the sprite (see rasterCommit).
//  PAL-first: NTSC's earliest sprite line is ~Y=20, so an NTSC build would want
//  ~20 here; the OS targets PAL (VICE), and one const beats a runtime probe.
.const SPR_COMMIT_DEADLINE = 30

//  Shadow block length -- the published ABI of the layout in the data block at
//  the bottom of this file, and what the library's kSpriteReg bound-checks
//  against. Defined here because KickAssembler needs a const before its use.
//    X0..Y7 16B | XHi flags 8B | colours 8B | 7 control bytes = 39
.const SPR_SHD_SIZE = 39

//  Per-task sprite block -- the length of ONE TASK'S sprite state, and
//  therefore the length of both DMAs a hand-over makes (sprInstall). It covers
//  the sheet records (24B), the shadow (SPR_SHD_SIZE = 39B) and the frame
//  indices (8B), and it is spelled out because the live region in the data
//  block must be exactly this long and in this order: main.asm asserts the two
//  against each other.
.const SPR_BLK_LEN = 24 + SPR_SHD_SIZE + 8
.const SPR_BLK_SHIFT = 7     // 128B per task slot; the block address is a shift

// ==========================================
//  sprBlkAddr / sprRestoreFrames -- $CD9F
// ==========================================
//  Each task slot has one 128-byte REU block (REU_SPR_BASE + slot*128) holding
//  its sheet records, its shadow and its frame indices -- the LIVE state's own
//  fields, in the live order, so sprInstall can move the whole thing with one
//  DMA in each direction.
//
//  The block address has no table: slot*128 is a 7-bit shift, so the low byte
//  is $80 for an odd slot and 0 for an even one, and the high byte is the base
//  plus half the slot.
//
//  Clobbers A and Y. X SURVIVES: the spawn path that clears a recycled slot
//  still needs the new slot in X.
.pc = $CD9F
sprBlkAddr:
    tay
    lsr                         // A = slot >> 1
    clc
    adc #>REU_SPR_BASE
    sta kArg+1
    lda #0
    sta kArg+2                  // bank 0
    tya
    and #1
    beq sprBlkEven
    lda #$80
sprBlkEven:
    sta kArg                    // block low byte
    rts

// ==========================================
//  sprRestoreFrames -- put the incoming owner's art back into the slots
// ==========================================
//  A hand-over leaves the outgoing owner's pixels in the eight slots, because
//  the slots are shared by NUMBER. Every sprite the incoming owner has
//  resolved is therefore re-DMA'd from its recorded frame -- which is what
//  sprFrameIdx is kept for.
//  A sprite with no sheet resolved (sheetHi = 0; bank 0 is never an FS data
//  bank) is skipped: there is nothing to fetch.
//  Clobbers A, X, Y, kPtr, kArg..kArg+7, reuFsPtr, reuFsFile/reuFsLen/
//  reuFsFlags (the FS scratch reuDmaRange uses), and the slot pixels.
sprRestoreFrames:
    ldx #SPRITE_SLOT_COUNT-1
sprRfLoop:
    lda spriteSheetHi,x
    beq sprRfNext
    txa
    pha
    pha                         // the sprite, kept across the DMA: sprFrameDma
    lda sprFrameIdx,x           //   clobbers X and wants the frame in it
    tax
    pla
    jsr sprFrameDma             // A = sprite, X = frame
    pla
    tax                         // back to the loop's own index
sprRfNext:
    dex
    bpl sprRfLoop
    rts
.label sprInstallHelpEnd = *

// ==========================================
//  sprFrameDma -- DMA frame X of slot A's sheet into the slot
// ==========================================
//  Input:  A = slot (0..SPRITE_TASK_SLOTS-1), X = frame (0-255)
//  Output: C=0 loaded, C=1 rejected (slot 8+ / no sheet resolved for it)
//  Clobbers: A, X, Y, kArg..kArg+7, kPtr, reuFsPtr, reuFsFile/reuFsLen/
//  reuFsFlags. (Both FS-scratch users are atomic: the FS entries hold SEI across
//  their own use of it, and this routine's SEI covers the build + the DMA.)
//  SEI'd: the source offset is built in the SHARED reuFsPtr and the DMA
//  follows immediately -- a tick between them would corrupt the transfer.
//
//  Two callers, and both are the display owner's: sprite.lib's kSpriteFrame
//  (which has already checked the focus and recorded the index) and
//  sprRestoreFrames, which runs the same DMA for every sprite when the task
//  takes the display back. Kernel address is published to the library and to
//  sprRestoreFrames as the `sprFrameDma` const.
//
//  This performs the DMA directly, so the slot is written wherever the beam
//  happens to be: a slot rewritten while the sprite is on screen splits it into
//  two animation frames.
.pc = $1CC0
sprFrameDma:
    cmp #SPRITE_TASK_SLOTS
    bcs sprFrameBad
    tay                         // Y = slot: abs,Y indexes every table below,
                                // and nothing clobbers Y before reuDmaRange
    lda spriteSheetHi,y
    beq sprFrameBad             // 0 = no sheet resolved for this slot (bank 0
                                //   is never an FS data bank, so a zero record
                                //   cannot name a file)
    // frame*64 -> kArg (lo) / kArg+1 (hi). Six unrolled shifts would be 36B and
    // a 4-instruction loop is worse (54B), so rotate the two low bits of the
    // frame into the high byte instead: (frame<<6) is (frame&3)<<6 in lo and
    // frame>>2 in hi -- and `ror` is the only way to shift bits into a byte.
    lda #0
    sta kArg
    txa                         // A = frame
    lsr                         // C = frame bit 0
    ror kArg                    // kArg = bit 0 << 7
    lsr                         // C = frame bit 1, A = frame >> 2
    ror kArg                    // kArg = (frame & 3) << 6
    sta kArg+1
    php
    sei                         // atomic: reuFsPtr is shared, and the add below
                                // must not be separated from its DMA
    // reuFsPtr = spriteSheet[slot] + frame*64
    //  Hand-written 24-bit add, not :FSAdd16: the record is three separate
    //  per-slot arrays, so there is no contiguous 3-byte source for the macro
    //  to take. The carry MUST reach the bank byte -- that is lint rule R3
    //  (tools/check-fs-offsets.ps1), which fails the build if it does not.
    lda spriteSheetLo,y
    clc
    adc kArg
    sta reuFsPtr
    lda spriteSheetMid,y
    adc kArg+1
    sta reuFsPtr+1
    lda spriteSheetHi,y
    adc #0
    sta reuFsPtr+2
    // 64 bytes -> the slot (multi-bank safe; reuDmaRange restores kPtr)
    lda #64; sta kArg+6
    lda #0;  sta kArg+7
    lda spriteSlotLo,y; sta kPtr
    lda spriteSlotHi,y; sta kPtr+1
    jsr reuDmaRange             // clobbers A and Y -- everything above is done
    plp                         // restores the caller's I flag
    clc
    rts
sprFrameBad:
    sec
    rts
.label sprFrameEnd = *

// ==========================================
//  kSpriteCollide -- $CA2F
// ==========================================
//  The setters (sheet, frame, pos, reg) are NOT here: they write the shadow,
//  which is ordinary RAM, so they are sprite.lib -- the pool pays for them only
//  while a task that declares it is resident. What stays is what the raster
//  side owns: the shadow itself, the install, the commit, and this reader,
//  which is the ONLY consumer of $D01E/$D01F (the hardware clears them on
//  read, so one reader is what makes the latch work).
//
//  FOCUS POLICY is the library's job for the setters and is repeated here,
//  because a non-owner must not steal the display owner's collision evidence.
.pc = $CA2F

// --- kSpriteCollide: the frame-latched collision masks ------------------
//  Input:  --
//  Output: C=0 A = sprite-sprite mask, X = sprite-background mask (both then
//          CLEARED, so a collision is reported to exactly one caller).
//          C=1 rejected (not the display owner) -- masks left untouched.
//  Clobbers: X, Y.  X is the background mask (an OUTPUT, not a scratch --
//  a caller that keeps a value in X loses it), Y and A are scratch on the
//  reject path. The masks are the previous frame's (section 5.4): $D01E/$D01F
//  are read-and-cleared by hardware, so the SERVICE is their only reader and
//  these latches are OR-accumulated until someone asks for them.
spriteCollide:
    ldy curTask
    cpy kbFocus
    bne sprColBad
    lda sprCollSs
    ldx sprCollBg
    ldy #0
    sty sprCollSs
    sty sprCollBg
    clc
    rts
sprColBad:
    sec
    rts
.label sprApiEnd = *

// ==========================================
//  clearSlotSprite -- a NEW task starts with no sprites
// ==========================================
//  Each task slot has its own sprite block (REU_SPR_BASE + slot*128), and it
//  has the same per-slot-stale shape as the ZP shadow: the block is per SLOT,
//  but only a task that RAN in that slot ever wrote it, and ids are recycled
//  lowest-first. A successor spawned into a recycled slot would otherwise wear
//  the previous occupant's sprites.
//
//  Zeros are the ONE correct starting state and they cover all three runs at
//  once: no control bit set (every sprite disabled) and no sheet resolved
//  (bank 0 is never an FS data bank, so a zero record cannot name a file) --
//  which is what lets sprFrameDma treat a zero bank byte as "nothing to fetch".
//
//  The INSTALLED slot is forgotten too. The live copy in kernel RAM belongs to
//  the task that just died, and the next install would otherwise write it back
//  over this fill: `sprLiveOwner = $FF` makes that install skip its save, and
//  `sprPubMask = 0` keeps the stale state off the screen until it does.
//
//  Called from scSetAlive (syscall.inc) with X = the new task's slot, SEI'd (the
//  whole spawn path is), so no IRQ can start a competing REU DMA.
//  Clobbers: A, Y, kArg..kArg+5. X SURVIVES -- the caller still needs the slot.
clearSlotSprite:
    txa                         // sprBlkAddr takes the slot in A
    jsr sprBlkAddr              // kArg/kArg+1 = the block, kArg+2 = bank 0
    lda #SPR_BLK_LEN
    sta kArg+3                  // length lo
    lda #0
    sta kArg+4                  // length hi
    cpx sprLiveOwner
    bne sprCsFill
    lda #$ff
    sta sprLiveOwner            // nothing is installed: do not save it back
    lda #0
    sta sprPubMask
sprCsFill:
    lda #0
    jsr kReuFillReu             // A = 0 -> the block reads as "no sprites"
    rts
.label clearSlotSpriteEnd = *

// ==========================================
//  warmSpriteReset -- forget every task's sprite state
// ==========================================
//  Called from warmBootReset AND from init.inc's cold boot. VICE is launched
//  with a read-write REU image (`-reuimagerw`), so the per-task blocks in it
//  survive into the next session, and slot 0 is the one slot that never goes
//  through the spawn path -- without this the boot task would install a
//  previous session's sprites. Separate from sprInstall because a reboot has
//  to leave NO trace of what the last session's sprites were: three pieces of
//  state survive `jmp start`.
//    * `sprPubMask` -- the live state in kernel RAM is still the last owner's,
//      and until the first install replaces it the commit would keep
//      publishing it; $00 stops that.
//    * `sprLiveOwner` -- $FF is "nothing installed", so the first install has
//      nothing to save back over the fill below.
//    * every per-task block -- a dynamic task always gets a zeroed block from
//      `clearSlotSprite` when its slot is reused, but the BOOT task does not go
//      through the spawn path at all, so without this fill slot 0 would come
//      back wearing whatever the last session's slot 0 had.
//  One DMA: the blocks are contiguous and 128B apart, so the whole table is a
//  single fill from REU_SPR_BASE. (Both facts make the address a shift, which
//  is why the length's low byte is 0 and needs no store.)
//  Reached by a jsr from warmBootReset -- interrupts off, no task running -- so
//  clobbering A, X, Y and kArg is fine.
.pc = $529E
warmSpriteReset:
    lda #0
    sta sprPubMask
    sta kArg                    // REU address low byte
    sta kArg+2                  // bank 0
    sta kArg+3                  // length low byte
    lda #$ff
    sta sprLiveOwner
    lda #>REU_SPR_BASE
    sta kArg+1
    lda #>MAX_TASKS*128         // $C00: 24 blocks of 128B
    sta kArg+4
    lda #0
    jsr kReuFillReu             // A = 0 -> every block reads as "no sprites"
    rts
.label warmSpriteResetEnd = *

// ==========================================
//  sprInstall -- $52DC
// ==========================================
//  THE HAND-OVER. All eight slots are shared by NUMBER, so the register set can
//  describe exactly one task at a time -- the task whose surface is DISPLAYED.
//  Each task's sprite state waits in its own REU block (sprBlkAddr), and this
//  is what moves it in and out:
//
//      save the live state into the OLD owner's block   (one DMA)
//      load the NEW owner's block into the live state   (one DMA)
//      re-DMA the frames its sheet records name         (up to eight DMAs)
//
//  The live state holds the sheet records, the shadow and the frame indices in
//  the block's own order, so the two DMAs are one routine.
//
//  WHO CALLS IT, AND WHY BOTH:
//    * the scheduler's restore path (switchTask/reschedule, i.e. the timer tick
//      and every yield/sleep/exit/kill) -- so a display change made inside an
//      interrupt (an F-key screen switch) is honoured with no task involved, and
//      so an owner that dies without touching a sprite setter stops being
//      published. That is also the ONE place where clobbering kPtr/kArg is free:
//      the scheduler has just saved the outgoing task's copies and reloads the
//      incoming task's a few instructions later, so the swap must NOT be called
//      anywhere earlier in the tick.
//    * sprite.lib, at the top of every setter -- so a write can never land in
//      the PREVIOUS owner's live state: the new owner's first call installs its
//      own block first, well before the next switch would.
//
//  Idempotent, and cheap when nothing changed (a compare and an rts), which is
//  the common case for both callers.
//
//  `sprPubMask` is maintained here rather than by the commit: the commit ANDs
//  it into the $D015 publish and stays byte-identical, and $00 is what keeps a
//  stale live state off the screen when there is no display owner at all
//  (kbFocus = $FF, a blanked display -- sprites draw over the border, so
//  "nothing is displayed" must mean nothing is published).
//
//  Clobbers A, X, Y, kArg..kArg+7, kPtr, reuFsPtr and the FS scratch
//  sprFrameDma uses (reuFsFile/reuFsLen/reuFsFlags). A CALLER THAT NEEDS kPtr
//  KEEPS ITS OWN COPY -- kSpriteSheet's filename pointer is the one case.
.pc = $52DC
sprInstall:
    lda kbFocus
    cmp sprLiveOwner
    beq sprInsDone              // nothing changed: the common case
    php
    sei                         // the swap is up to ten DMAs, and a second
                                //   install must not interleave with it
    lda #0                      // the latches are evidence about the sprites
    sta sprCollSs               //   that were published, so they die with the
    sta sprCollBg               //   hand-over: the new owner's first
                                //   kSpriteCollide cannot report the old one's
    lda sprLiveOwner
    bmi sprInsLoad              // $FF: nothing installed, nothing to save
    jsr sprStateMove            // save: live state -> the old owner's block
sprInsLoad:
    lda kbFocus
    sta sprLiveOwner
    bmi sprInsNone              // no display owner: publish nothing
    jsr sprStateLoad
    jsr sprRestoreFrames
    lda #$ff
    sta sprPubMask
    plp
    rts
sprInsNone:
    lda #0
    sta sprPubMask
    plp
sprInsDone:
    rts

// --- the two halves of the swap -----------------------------------------
//  Input: A = task slot.
//  The live state and a task's block are the SAME layout, which is what makes
//  each half a single DMA: kArg..kArg+2 is the block (sprBlkAddr), kArg+3/+4 the
//  length, and kPtr the live end.
//  Clobbers A, X, Y, kArg..kArg+4, kPtr, reuFsPtr.
sprStateMove:
    jsr sprBlkAddr
    jsr sprDmaSetup
    jmp kReuFromC64             // C64 -> REU
sprStateLoad:
    jsr sprBlkAddr
    jsr sprDmaSetup
    jmp kReuToC64               // REU -> C64
sprDmaSetup:
    lda #SPR_BLK_LEN
    sta kArg+3
    lda #0
    sta kArg+4
    lda #<sprLiveState
    sta kPtr
    lda #>sprLiveState
    sta kPtr+1
    rts
.label sprInstallEnd = *

// ==========================================
//  rasterCommit -- $567D
// ==========================================
//  Called once per frame from irqHandler when the VIC raster flag is set. The
//  raster compare is at line 0, which is in the vertical blank on BOTH PAL and
//  NTSC, and before the earliest line a sprite can be visible (PAL Y=31, NTSC
//  ~Y=20) -- so the whole sprite register set changes between frames and a
//  task's update is never half-applied. This is what makes every sprite
//  setter a plain store with no timing rule.
//
//  HARD RULES. This code runs with I=1 and can preempt ANY
//  task or kernel code, including the dynamic loader's CLI windows:
//    * no CLI -- re-enabling interrupts would let a pending timer tick switch
//      tasks INSIDE the commit, leaving the shadow half-copied
//    * no kernel calls, no kernel ZP, no kArg/kPtr/srcPtr
//    * no REU use at all -- the commit moves no data
//  It touches only A, X, Y, its own state block, and the VIC. Y is free: the
//  only caller, irqHandler, has already pushed A/X/Y and irqRet restores them --
//  and the $D010 loop needs it to read a flag without destroying it.
.pc = $567D
rasterCommit:
    //  FRAME COUNTER -- the one thing here that is not about sprites. It ticks
    //  once per frame, BEFORE the margin test, because a frame is a frame even
    //  when the commit skips. Tasks use it to update once per FRAME instead of
    //  once per scheduler tick: the tick runs at 59.2 Hz and the display samples
    //  at 50.1 Hz, so a per-tick animation is sampled unevenly (two updates land
    //  in one frame, none in the next), while a per-frame one is sampled exactly.
    //  Wrapping is 8-bit and deliberate -- a task only compares it for CHANGE.
    inc sprFrameCount
    //  DEADLINE GUARD. The
    //  commit has a hard deadline: it must finish before the first sprite line is
    //  reached, or a sprite's X changes part-way DOWN it and the sprite is split
    //  into two positions. That tear is real and visible -- `sprdemo` mode 7 shows
    //  it (~8% per provoked ~9,200-cycle burst) -- so a late commit now does
    //  NOTHING instead: the sprite holds its previous position for that one frame
    //  and the next frame's on-time commit publishes the accumulated movement. A
    //  stutter, not a split.
    //
    //  WARNING: This is not a "skip if late" that can starve: the test is against a
    //  FIXED deadline, not against the sprites in use, so only a burst that
    //  actually straddles line 0 and runs past line 30 is affected. Normally the
    //  compare fires at line 0 and this code runs a few dozen cycles later -- the
    //  guard's 7 cycles are the whole cost on a normal frame.
    //
    //  Skipping the collision latch below loses nothing: $D01E/$D01F are cleared
    //  by READING, so they accumulate until the next commit reads them.
    lda $d012
    cmp #SPR_COMMIT_DEADLINE
    bcc rcGo                    // in time -- commit as normal
    rts                         // too late -- leave it all to the next frame
rcGo:
    ldx #15                     // $D000-$D00F as ONE 16-byte copy: the shadow
rcPos:                          // mirrors $D000 interleaved (X0,Y0,...,X7,Y7) so
    lda sprShReg,x              // this is a loop, not 8 unrolled register pairs
    sta $d000,x
    dex
    bpl rcPos
    //  $D010 (the X high bits) is ASSEMBLED from the per-sprite flags rather
    //  than mirrored, so kSpritePos can stay a plain store (section 5.2). bitMasks is
    //  the existing 1<<n table from dynload.inc -- no second copy of it.
    //
    //  WARNING: THE FLAGS ARE STATE, NOT A LATCH -- read them, never shift them.
    //  `lsr sprShXHi,x` is a read-modify-write on MEMORY: it zeroes the flag and
    //  puts bit 0 in the carry, so the commit would consume what it had just
    //  published. A sprite at x >= 256 then loses its $D010 bit on the very next
    //  frame and is drawn at x-256 until the task happens to call kSpritePos
    //  again. It is invisible while every x stays under 256, where all eight
    //  flags are 0 and destroying them is a no-op. The flag byte is 0 or 1
    //  (kSpritePos stores the plain carry), so `beq` is the exact test -- and it
    //  costs nothing: ldy+beq is the same 5 bytes as lsr+bcc. Do not "optimise"
    //  this back to a shift.
    lda #0
    ldx #7
rcXhi:
    ldy sprShXHi,x
    beq rcXhiNext
    ora bitMasks,x
rcXhiNext:
    dex
    bpl rcXhi
    sta $d010
    ldx #7
rcCol:
    lda sprShColour,x
    sta $d027,x
    dex
    bpl rcCol
    lda sprShEnable
    and sprPubMask              // only the DISPLAY OWNER's sprites are published
    sta $d015                   // WARNING: no `and #SPR_ENABLE_MASK` term: at 8 slots
                                // it is $FF, because every channel has a slot
                                // behind it.
                                // Re-add it if a slot is ever dropped -- the assert
                                // "enable mask is $FF at eight slots" is the tripwire.
    lda sprShYExp;     sta $d017
    lda sprShPriority; sta $d01b
    lda sprShMc;       sta $d01c
    lda sprShXExp;     sta $d01d
    lda sprShMc0;      sta $d025
    lda sprShMc1;      sta $d026
    //  $07F8-$07FF -- the VIC sprite data pointers (slot address / 64). That
    //  page is INSIDE the per-surface matrix ($0400-$07FF), which surfLoad
    //  DMAs on every view switch, so the pointers are re-applied here every
    //  frame rather than hooked into gfx.inc (section 3.3). This also self-heals
    //  after any surface DMA: whichever surface becomes visible picks up the
    //  current pointers at once.
    ldx #7
rcPtr:
    lda sprPtrBytes,x
    sta $07f8,x
    dex
    bpl rcPtr
    //  Collision latch. $D01E/$D01F are read-only and READING CLEARS THEM, so
    //  this is their only reader (section 5.4): a task cannot steal another task's
    //  evidence by reading early, and a task that polls every Nth frame still
    //  sees every collision because the latch survives between polls.
    lda $d01e
    ora sprCollSs
    sta sprCollSs
    lda $d01f
    ora sprCollBg
    sta sprCollBg
    rts
.label rasterCommitEnd = *

// ==========================================
//  Data -- $CFA4-$CFFF
// ==========================================
//  THE PER-TASK REGION COMES FIRST, CONTIGUOUS AND IN THE BLOCK'S OWN ORDER.
//  Everything from `sprLiveState` to the end of `sprFrameIdx` is what
//  sprInstall DMAs in and out of a task's REU block, so the live copy and the
//  saved copy are the same bytes in the same order -- no field mapping, one
//  length, and a single assert pins the two halves together (SPR_BLK_LEN).
//  A sprite's sheet is "unresolved" when its bank byte is 0: the FS data area
//  spans banks 5..251, so 0 can never name a file, and the spawn-time fill of
//  a recycled slot (all zeros) is therefore already "no sprites at all".
//
//  NO spritePtrs routine. Writing $07F8-$07FF is the raster commit's
//  job, inlined there: the pointer page is INSIDE the per-surface matrix
//  ($0400-$07FF is DMA'd on every view switch), so it has to be re-applied in
//  vblank anyway -- and the service may not call subroutines.
.pc = $CFA4
sprLiveState:                       // the display owner's state, live
// --- sheet records: the 24-bit FS offset of frame 0, per sprite ---
//  Published as `spriteSheetLo/Mid/Hi` in src/tasks/kernel-data.inc: the library
//  writes them (it resolves the sheet), sprFrameDma reads them.
spriteSheetLo:   .fill SPRITE_SLOT_COUNT, 0
spriteSheetMid:  .fill SPRITE_SLOT_COUNT, 0
spriteSheetHi:   .fill SPRITE_SLOT_COUNT, 0     // 0 = unresolved (see above)

// --- the shadow: a plain RAM copy of the whole sprite register set ---
//  Tasks write THIS; rasterCommit is the only writer of the VIC. The order is
//  the published ABI for kSpriteReg and lives task-side in sprite-shadow.inc,
//  so it is part of the interface:
//    sprShReg     16B   X0,Y0,X1,Y1,...,X7,Y7  -> $D000-$D00F, one copy loop
//    sprShXHi      8B   per-sprite X high bit -> assembled into $D010 each frame
//    sprShColour   8B   -> $D027-$D02E, one copy loop
//    then the 7 control bytes
//  The block stays EIGHT-wide because the VIC's register set is: the arrays
//  mirror hardware, not the slot count, so shrinking them would change the
//  published offsets for no gain -- all eight channels now have slots.
//  All-zero = every sprite disabled, which is what the first commit publishes
//  (nothing on screen changes until a task writes the shadow).
sprShReg:        .fill 16, 0
sprShXHi:        .fill 8, 0
sprShColour:     .fill 8, 0
sprShEnable:     .byte 0
sprShYExp:       .byte 0
sprShPriority:   .byte 0
sprShMc:         .byte 0
sprShXExp:       .byte 0
sprShMc0:        .byte 0
sprShMc1:        .byte 0
//  (SPR_SHD_SIZE = 39 is declared at the top of this file -- the library's
//  kSpriteReg bound-checks the offset it is handed against it.)

// --- frame indices: the frame each sprite is showing ---
//  The library records the frame it was last asked for, so sprRestoreFrames can
//  put the art back when this task takes the display again. Published as
//  `sprFrameIdx` in src/tasks/kernel-data.inc.
sprFrameIdx:     .fill SPRITE_SLOT_COUNT, 0
//  SPR_BLK_LEN = 71 ends here.

// ==========================================
//  Not per-task: everything below belongs to the kernel alone
// ==========================================
spriteSlotLo:    .byte <SPRITE_SLOT0, <SPRITE_SLOT1, <SPRITE_SLOT2, <SPRITE_SLOT3
                 .byte <SPRITE_SLOT4, <SPRITE_SLOT5, <SPRITE_SLOT6, <SPRITE_SLOT7
spriteSlotHi:    .byte >SPRITE_SLOT0, >SPRITE_SLOT1, >SPRITE_SLOT2, >SPRITE_SLOT3
                 .byte >SPRITE_SLOT4, >SPRITE_SLOT5, >SPRITE_SLOT6, >SPRITE_SLOT7
//  The slot whose state is live in the bank above ($FF = nothing installed).
//  Written by sprInstall, read by it and by the spawn path's invalidation.
sprLiveOwner:    .byte $FF
sprPubMask:      .byte 0          // $FF while a display owner is installed,
                                  // $00 while there is none. Read by the
                                  // commit's $D015 publish -- never by a task.
// --- collision latch: NOT shadow, never committed to the VIC (read-only) ---
// --- collision latch: NOT shadow, never committed to the VIC (read-only) ---
sprCollSs:       .byte 0          // $D01E sprite-sprite, latched
sprCollBg:       .byte 0          // $D01F sprite-background, latched
//  Per-FRAME counter, incremented by the raster commit (see it for why). Not
//  part of the shadow and not committed to the VIC: it is a clock a task can
//  read to align an animation with the display. Published to tasks as
//  `frameCount` in src/tasks/kernel-data.inc.
sprFrameCount:   .byte 0
.label sprDataEnd = *

// ==========================================
//  VIC sprite data pointers -- $CDED
// ==========================================
//  slot address / 64, computed at assembly time. Data only (the raster commit
//  reads it); it lives here because the $CFA4 pocket ran out of room.
//  EIGHT entries -- one per slot, and there are eight slots, so
//  there are no padding entries left. $07F8-$07FF must be FULLY defined either
//  way: the pointer page travels with the surface (surfLoad DMAs it), so leaving
//  bytes to the previous surface would leave a live pointer to a stale address.
.pc = $CDED
sprPtrBytes:     .byte SPRITE_SLOT0/64, SPRITE_SLOT1/64, SPRITE_SLOT2/64, SPRITE_SLOT3/64
                 .byte SPRITE_SLOT4/64, SPRITE_SLOT5/64, SPRITE_SLOT6/64, SPRITE_SLOT7/64
.label sprPtrBytesEnd = *

// ==========================================
//  Public entry stubs -- $CDDE+
// ==========================================
//  The $E000 jump table's 91 slots are full, so these are plain jmp stubs like
//  kStrcmp/kFillMatrix. They sit at a FIXED address range so the library can
//  grow without moving the task-facing ABI; main.asm asserts the stub end
//  against char-shadow ($CE00) and each address against jumptable.inc.
//
//  FOUR OF THE FIVE ARE sprite.lib CALL ENTRIES, and they are ordinary library
//  entry bytes: `libJumpLo/Hi` points at them, so libLoad rewrites each in
//  place to `jmp <the library's entry>` and libFree restores the initial
//  `jmp libTrap`. They live here rather than in the `$FF7E + 3n` run because
//  that run is the gfx scratch's growth headroom, and the run is contiguous
//  only as a contract -- the addresses are just an index into libJumpLo/Hi.
//  `kSpriteCollide` is NOT an entry: it reads the latches, which are kernel
//  state, so it is a plain resident routine.//
//  ==========================================
//  kbScreenGate / kbApplyScreenReq -- the F-key's display change, applied late
//  ==========================================
//  kbPoll RECORDS the screen an F-key asked for in kbScreenReq and returns. The
//  switch itself is applied from the scheduler's switch path, at the one point in
//  the tick where the outgoing task's kArg has already been saved to its REU slot
//  and the incoming task's has not yet been put back: `switchView` stores fromTask
//  into kArg and then swaps two surfaces through the kReu* calls, whose parameters
//  are the kArg block -- so a switch run under the key scan would land those writes
//  in the middle of whatever the running task had in the block. A task marshalling
//  a kArg call (kBox, kDma and friends) would then hand the kernel a value nobody
//  asked for, intermittently and only while an F-key is pressed.
//  The switch also has to reach the RESTORE path, not a bare resume: sprInstall is
//  what publishes the new display owner's sprites, and the kArg restore is what
//  puts the block back.
//  kbScreenReq is $ff for "nothing asked for" (screens are 0-7). A value left over
//  from a warm reboot is harmless: kbSwitchToScreen re-resolves the number against
//  the live task table and switches nothing if no task owns that screen.
//
//  These live in free pockets because every band they are called from is pinned:
//  keyboard.inc's band is ABI (asserts in main.asm), and the switch path is the
//  kernel core, whose headroom is for the next change rather than for storage.
//  They sit HERE, between the $CDED data band and the $CDDE stub band, because a
//  `.pc` band's extent runs to its last emitted byte and the file's later code is
//  anchored elsewhere -- a band inserted mid-flow would swallow it.
.pc = $52C1
kbScreenGate:
    lda kbScreenReq
    bmi kbSgNone                 // $ff: nothing was asked for
    jmp kbApplyScreenReq         // applies it, then joins the restore path
kbSgNone:
    jmp svNoReq                  // back to the tick's ordinary flow
.label kbScreenGateEnd = *

.pc = $CDCF
kbApplyScreenReq:
    ldx kbScreenReq              // the screen the key scan asked for
    lda #$ff
    sta kbScreenReq              // taken: a second press is a new request
    jsr kbSwitchToScreen         // display + kbFocus move; X is the screen number
    jmp svRestore                // the full restore path -- see the header
kbScreenReq:  .byte $ff          // the requested screen, $ff = none
.label kbApplyScreenReqEnd = *
.pc = $CDDE
kSpriteSheet:   jmp libTrap
kSpriteFrame:   jmp libTrap
kSpritePos:     jmp libTrap
kSpriteCollide: jmp spriteCollide
kSpriteReg:     jmp libTrap
.label sprStubEnd = *
