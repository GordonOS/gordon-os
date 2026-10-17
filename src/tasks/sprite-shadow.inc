// ==========================================
//  sprite-shadow.inc -- the sprite shadow ABI (task side)
// ==========================================
//  Zero runtime cost: `.const`s and macros, all expanded at assembly time.
//  They publish the OFFSETS inside the kernel's sprite shadow block -- the
//  layout `rasterCommit` copies to `$D000-$D02E` once per frame in the vertical
//  blank.
//
//  The kernel side of this contract is src/kernel/sprite.inc, and the layout is
//  pinned by `.assert`s in src/main.asm -- moving a field fails the KERNEL build
//  rather than silently corrupting tasks.
//
//  How to drive a sprite:
//
//      sprColour(2, WHITE)      ; sprite 2's own colour   -> $D027+2
//      sprOn(%00000101)         ; enable sprites 0 and 2   -> $D015
//      ldx #SPR_ID              ; kSpritePos: X = sprite, A = x lo, Y = y,
//      lda xPix                 ;            C = x high bit (x >= 256)
//      ldy yPix
//      clc
//      jsr kSpritePos
//      lda #SPR_ID              ; kSpriteFrame: A = slot, X = frame
//      ldx frameIdx
//      jsr kSpriteFrame
//      jsr kSpriteCollide       ; -> A = sprite-sprite, X = sprite-background
//
//  TWO RULES WORTH KNOWING BEFORE WRITING ANYTHING:
//
//  1. Every accessor is FOCUS-GUARDED. Only the task whose surface is
//     *displayed* may write the shadow, and a rejected call returns C=1 having
//     written nothing. Losing the display HIDES your sprites -- what is not
//     installed is not published -- but it does not forget them: get the
//     display back and the kernel re-installs your state, art and all, at the
//     next frame. Nothing to re-assert, and nothing to poll for.
//  2. Every write lands at the next vertical blank. There is no wrong moment
//     to write, and nothing to wait for: no `$D012` polling, no frame counting
//     for safety. What is left to you is arithmetic -- positions must be a
//     smooth function of time (plan section 5.6 explains the fractional ramp).
//  3. TWO TASKS CAN DRIVE SPRITES AT ONCE, AND EACH TASK OWNS ALL EIGHT SLOTS.
//     There is nothing to allocate: the kernel keeps exactly ONE task's sprite
//     state live -- the task whose surface is DISPLAYED -- and hands it over
//     when the display changes hands. Sheet records, shadow and frame indices
//     are saved per task slot in the REU, so a task that is not displayed is
//     simply not installed, and it comes back to the art it left.
//     The five control bytes (`$D015`/`$D017`/`$D01B`/`$D01C`/`$D01D`) are
//     bitmasks over all eight slots in ONE byte each, so writing one is
//     writing your whole sprite set -- which is correct here, because the byte
//     that is live is yours alone. The MC colours (`$D025`/`$D026`) are global
//     hardware and per-task for the same reason: the commit publishes them
//     from the shadow of whichever task is displayed.
//     A new task starts with an all-zero block, so it inherits nothing: no
//     enable, no expansions, no priority, no MC mode, no sheet.
//
//  See docs/kernel.md ("Sprites") for the calls themselves.

// --- shadow layout (offsets from the base of the block) ---
.const SPR_SHD_X0       = 0      // sprite n: x low at X0 + 2n, y at +1
.const SPR_SHD_XHI      = 16     // per-sprite X high bit -- kSpritePos maintains it
.const SPR_SHD_COLOUR   = 24     // sprite n's own colour at COLOUR + n
.const SPR_SHD_ENABLE   = 32     // $D015
.const SPR_SHD_YEXP     = 33     // $D017
.const SPR_SHD_PRIORITY = 34     // $D01B
.const SPR_SHD_MC       = 35     // $D01C
.const SPR_SHD_XEXP     = 36     // $D01D
.const SPR_SHD_MC0      = 37     // $D025
.const SPR_SHD_MC1      = 38     // $D026
.const SPR_SHD_SIZE     = 39     // block length; kSpriteReg rejects X >= this
//  WARNING: the byte just PAST the 39 is NOT part of the shadow: it is
//  `sprFrameIdx[0]`, the frame each sprite is showing. The library writes it
//  and the kernel re-DMAs from it when this task takes the display back, so a
//  task never touches it.

// --- slot addresses: sprite N is fetched from slot N (bank 0, 64-byte aligned) ---
//  A task almost never needs these -- `kSpriteSheet`/`kSpriteFrame` move frames
//  into the slot for you -- but a task MAY write slot pixels directly (that is how
//  a task would draw art it does not want to keep in the REU FS).
//  There are EIGHT slots (the hardware maximum), and no slot is reserved for
//  anything -- a task that wants a mouse pointer drives a sprite like any other. Two bands hold them --
//  $02C0/$0300/$0340/$0380/$03C0
//  below the video matrix, and $0F00/$0F40/$0F80 released from the kernel core.
//  Nothing else in bank 0 can hold frames ($1000-$1FFF is char ROM to the VIC,
//  $2000-$3FFF is the framebuffer).
.const SPR_SLOT0 = $02C0
.const SPR_SLOT1 = $0300
.const SPR_SLOT2 = $0340
.const SPR_SLOT3 = $0380
.const SPR_SLOT4 = $03C0
.const SPR_SLOT5 = $0F00
.const SPR_SLOT6 = $0F40
.const SPR_SLOT7 = $0F80   // all eight are ordinary task sprites

// --- the fields worth a name; anything else is a kSpriteReg offset ---
//  Masks are the CALLER's property: the shadow is a sink, never a query -- pass
//  the mask you want and keep it. Nothing reads the shadow back.
//  Every one of these is a plain `kSpriteReg` (A = value, X = offset): the byte
//  that is live belongs to the task whose surface is displayed, so a control
//  byte is this task's own set of eight and nothing has to be merged.
.macro sprOn(value) {             // $D015 -- every channel has a slot behind it
    lda #value
    ldx #SPR_SHD_ENABLE
    jsr kSpriteReg
}
.macro sprColour(n, v) {         // the per-sprite colour, $D027+n
    lda #v
    ldx #SPR_SHD_COLOUR + n
    jsr kSpriteReg
}
.macro sprPriority(v) {          // $D01B, 1 bit per sprite: 1 = behind the
    lda #v                       // foreground graphics (but in front of
    ldx #SPR_SHD_PRIORITY        // background pixels), 0 = in front of both.
    jsr kSpriteReg               // Drawing order only -- collisions are
}                                // unaffected. See docs/programmers-guide.md
                                 // -> Sprites -> "In front of, or behind, the
                                 // graphics".
.macro sprMode(v) {              // 1 bit per sprite: 1 = multicolour
    lda #v
    ldx #SPR_SHD_MC
    jsr kSpriteReg
}
.macro sprXExpand(v) {           // 1 bit per sprite
    lda #v
    ldx #SPR_SHD_XEXP
    jsr kSpriteReg
}
.macro sprYExpand(v) {           // 1 bit per sprite
    lda #v
    ldx #SPR_SHD_YEXP
    jsr kSpriteReg
}
.macro sprMcColour(i, v) {       // i = 0 or 1 ($D025/$D026) -- global hardware,
    lda #v                       //   published from the displayed task's
    ldx #SPR_SHD_MC0 + i         //   shadow, so per-task in effect
    jsr kSpriteReg
}

//  There is no shadow-init macro: the kernel zeroes a new task's block when the
//  slot is reused (`clearSlotSprite`, src/kernel/sprite.inc), so a task that
//  takes the display never inherits the previous occupant's enable, expansions,
//  priority or MC mode.
