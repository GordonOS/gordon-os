// ==========================================
//  sprite.asm -- dynamic library "sprite.lib" (lib 7, 4 entries, eager)
// ==========================================
//  The task-facing sprite API. Each task drives sprites 0-7 for itself: the
//  eight hardware slots are shared by NUMBER, and the kernel keeps one task's
//  sprite state LIVE at a time -- the display owner's. So there is no
//  allocation, no slot translation and nothing to run out of.
//
//      call 37  kSpriteSheet  A = sprite, kPtr = FS filename
//      call 38  kSpriteFrame  A = sprite, X = frame
//      call 39  kSpritePos    X = sprite, A = x lo, Y = y, C = x high bit
//      call 40  kSpriteReg    A = value, X = shadow offset (0..SPR_SHD_SIZE-1)
//
//  FOCUS POLICY. Sprites belong to the task whose surface is displayed, so each
//  of these opens with the same guard -- `curTask` must equal `kbFocus`, else
//  C=1 and nothing changes. That is the published contract, and it is what
//  keeps a background task's calls out of the live state: the kernel's install
//  hands the state over when the display changes hands, so a task's sprites
//  resume from its own last state the moment it is displayed again.
//
//  WHY THIS IS A LIBRARY. The same reason gfx.lib is: a library's pool cost is
//  charged only while a task that declares it is resident, so the kernel does
//  not carry these bytes for every task in the system. What CANNOT move is the
//  raster side -- the shadow, the install and the commit all belong to the
//  kernel, because no library can be paged in from an interrupt.
//
//  All labels prefixed sprite*/spr* -- imported files share ONE KickAssembler
//  namespace.
//
//  Entry table order = call-index order (base 37): sheet, frame, pos, reg.

.encoding "screencode_mixed"

// (kArg/kPtr/kbFocus/curTask/sprShadow come from kernel-data.inc,
//  SPR_SHD_* / SPRITE_TASK_SLOTS from sprite-shadow.inc, and the call consts
//  from jumptable.inc -- all imported globally by assemble-tasks.asm, so
//  nothing is re-declared here.)
//
//  The shadow is the MIDDLE field of the kernel's per-task state, so writing it
//  is writing the DISPLAY OWNER's sprites -- which is why every entry calls
//  `sprInstall` first: a write must never land in the previous owner's live
//  copy, and the install is what makes the caller's own block the live one.

spriteBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE         // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word spriteEnd - spriteCode // +5:  code size
    .byte 0, 0, 0                // +7:  data size, ZP base, ZP len (none)
    .word 0                      // +10: reserved (libs consume no libMask)
    .byte spriteNameEnd - spriteName // +12: name len
spriteName:
    .text "sprite"
spriteNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - spriteBinStart), 0

spriteCode:
    .word spriteSheet, spriteFrame, spritePos, spriteReg

// ==========================================
//  kSpriteSheet -- bind an FS file to a sprite as its animation sheet
// ==========================================
//  Input:  A = sprite (0-7; 8+ rejected), kPtr/kPtr+1 = filename (screen codes)
//  Output: C=0 resolved / C=1 rejected (sprite 8+, not found, no whole frame,
//          or not the display owner)
//  Writes: spriteSheetLo/Mid/Hi[sprite] = the file's 24-bit FS offset, so frame
//          N is `record + N*64`.
//  PRESERVES kPtr, so one filename pointer can resolve several sprites in a row
//  -- reload A and call again.
//  The sprite is parked on the CALLER'S STACK, never in a library byte: this
//  code is shared by every task, and an interrupt can switch tasks between the
//  park and the use -- a module-level byte would hand the second task's sprite
//  to the first task's store.
//  SEI'd: reuFindFile leaves the FS offset in the SHARED reuFsPtr, so the find
//  and the copy must be atomic or a tick could switch tasks between them.
//  Costs ~1,000-3,000 cycles (a directory scan), so resolve ONCE per sprite and
//  never per frame. A failed re-resolve leaves the previous record intact.
//  Clobbers: A, X, Y, kArg..kArg+7, reuFsPtr, reuFsRemain, srcPtr.
spriteSheet:
    cmp #SPRITE_TASK_SLOTS
    bcs sprSheetBad
    pha                         // [1] the sprite
    lda kPtr                    // the filename pointer: sprInstall clobbers
    pha                         // [2]   kPtr, and this routine needs it below
    lda kPtr+1
    pha                         // [3]
    jsr sprInstall              // make OUR state the live one before writing it
    pla
    sta kPtr+1
    pla
    sta kPtr
    sei                         // the guards, the find and the copy are ONE step: a
                                //   tick between them can hand the display over
                                //   (the record would land in the new owner's
                                //   state) or switch tasks between the find and
                                //   the copy, which share reuFsPtr. No status byte
                                //   is parked -- see spriteReg for why
    ldy curTask
    cpy kbFocus
    bne sprSheetOut
    lda kPtr;    sta kArg       // reuFindFile takes the name in kArg/kArg+1
    lda kPtr+1;  sta kArg+1
    jsr kReuFindFile            // C=0: reuFsPtr(3B) = offset, reuFsRemain = size
    bcs sprSheetBadI
    //  A sheet must contain at least one whole frame (64 bytes).
    lda reuFsRemain+1
    bne sprSheetGot
    lda reuFsRemain
    cmp #64
    bcc sprSheetBadI
sprSheetGot:
    tsx                         // WARNING: with no status byte parked, the sprite
    lda $0101,x                 //   IS the top of the stack here ($0101,x); the
                                //   kPtr bytes were popped above. Read it at
                                //   $0102,x and a stale byte indexes the record
    tax                         //   stores, up through the VIC registers
    lda reuFsPtr;   sta spriteSheetLo,x
    lda reuFsPtr+1; sta spriteSheetMid,x
    lda reuFsPtr+2; sta spriteSheetHi,x
    cli                         // ...and the window ends with the last store
    pla                         // drop [1] -- the kPtr bytes are already gone
    clc
    rts
sprSheetBadI:
    cli
    pla
sprSheetBad:
    sec
    rts
sprSheetOut:
    cli                         // clear the window, then the parked sprite
    pla
    sec
    rts

// ==========================================
//  kSpriteFrame -- load frame X of this sprite's sheet into its slot
// ==========================================
//  Input:  A = sprite (0-7; 8+ rejected), X = frame (0-255)
//  Output: C=0 loaded / C=1 rejected (sprite 8+, no sheet resolved, or not the
//          display owner)
//  The frame index arrives in X, not kArg: the whole sprite API keeps off kArg
//  and every other shared scratch another task may be using.
//  Frame bookkeeping is the task's -- the kernel is stateless about which frame
//  is current -- but the index is recorded, because it is what the kernel
//  re-DMAs when this task's surface next takes the display.
//  Clobbers: A, X, Y, kPtr, kArg..kArg+7, reuFsPtr.
spriteFrame:
    cmp #SPRITE_TASK_SLOTS
    bcs sprFrameBad
    pha                         // [1] the sprite
    txa
    pha                         // [2] the frame
    jsr sprInstall              // A, X and Y are the stack's problem now: the
                                //   live state must be OURS before we write it
    sei                         // the guards and the store are ONE step: a tick
                                //   between them can hand the display over, and
                                //   the index would land in the new owner's state.
                                //   No status byte is parked -- see spriteReg for
                                //   why (a status on top of parked inputs comes
                                //   back as an input and can restore a wrong I)
    ldy curTask
    cpy kbFocus
    bne sprFrameOut
    pla                         // A = frame
    tax                         // X = frame -- sprFrameDma takes it in X
    pla                         // A = sprite
    tay                         // Y = sprite
    txa                         // WARNING: the FRAME lives in X, not in A -- A is
    sta sprFrameIdx,y           //  the sprite here, so storing A would record the
                                //  SPRITE as the frame and the install would then
                                //  re-DMA sheet + sprite*64
    cli                         // the window ends with the store
    tya                         // A = sprite again, for the call below
    jsr sprFrameDma             // A = sprite, X = frame -> C
    rts
sprFrameOut:
    cli                         // clear the window, then the two parked inputs
    pla
    pla
sprFrameBad:
    sec
    rts

// ==========================================
//  kSpritePos -- place a sprite
// ==========================================
//  Input:  X = sprite (0-7; 8+ rejected), A = x LOW byte, Y = y, C = x high bit
//  Output: C=0 applied / C=1 rejected (not the display owner, or sprite 8+)
//  Writes the SHADOW, never the VIC: the kernel's raster commit publishes it in
//  vblank, so there is no wrong moment to call this and the effect appears at
//  the next frame boundary (up to one frame of latency).
//  The x high bit is kept per sprite and the commit assembles those flags into
//  $D010 -- one byte per sprite, and the derivation costs the service a few
//  cycles once per frame instead of bit juggling here, eight times per frame.
//  Clobbers: A, X, Y (all three are inputs, so nothing needs preserving).
spritePos:
    pha                         // [1] x lo -- PHA leaves the flags, and
    tya                         //     therefore the carry, alone
    pha                         // [2] y
    lda #0
    rol                         // A = the input carry (consumed here)
    pha                         // [3] x high bit
    txa
    pha                         // [4] the sprite
    jsr sprInstall              // all four inputs are on the stack
    sei                         // the guards and the stores are ONE step: a tick
                                //   between them can hand the display over, and
                                //   the position would land in the new owner's.
                                //   No status byte is parked -- see spriteReg
    ldy curTask                 // `cpy` compares without touching A or X
    cpy kbFocus
    bne sprPosBad
    pla
    tax                         // X = the sprite                     [pop 4]
    cpx #SPRITE_TASK_SLOTS      // bound-check the POPPED sprite -- the check
    bcs sprPosArgBad            //   cannot move above the `rol`: cmp/cpx clobber
                                //   the carry, which IS the x high bit
    pla                         // A = x high bit                     [pop 3]
    sta sprShadow + SPR_SHD_XHI,x
    txa
    asl
    tax                         // X = 2*sprite
    pla                         // A = y                              [pop 2]
    sta sprShadow+1,x
    pla                         // A = x lo                           [pop 1]
    sta sprShadow,x
    cli                         // the window ends with the last store
    clc
    rts
sprPosArgBad:
    cli                         // clear the window, then the three bytes the
    pla                         //   popped sprite left behind
    pla
    pla
    sec
    rts
sprPosBad:
    cli                         // not the display owner: clear the window, then
    pla                         //   all four parked inputs
    pla
    pla
    pla
    sec
    rts

// ==========================================
//  kSpriteReg -- any single-byte shadow field
// ==========================================
//  Input:  A = value, X = shadow offset (0..SPR_SHD_SIZE-1)
//  Output: C=0 applied / C=1 rejected (bad offset, or not the display owner)
//  This is the one setter for the values with no call of their own: the five
//  per-sprite control bitmasks (enable, y-expand, priority, MC mode, x-expand)
//  at offsets 32-36, and the two MC colours at 37-38.
//  No masking is needed -- a control byte covers all eight slots and the caller
//  owns all eight, because the shadow that is live belongs to ONE task, and
//  that task is the only one whose surface is displayed.
//  Clobbers: Y. A and X are the inputs, so they answer nothing.
spriteReg:
    cpx #SPR_SHD_SIZE           // bound-check FIRST: an unchecked X would walk
    bcs sprRegBad               // out of the block into the next pocket
    pha                         // the value
    txa
    pha                         // the offset
    jsr sprInstall              // make OUR state the live one before writing it
    sei                         // the guards and the store are ONE step: a tick
                                //   between them can hand the display over, and
                                //   the write would land in the new owner's state.
                                //   No status byte is parked: every caller of a
                                //   setter is a task, which runs with interrupts
                                //   ENABLED, so `cli` at the exits is what `sei`
                                //   masked -- and a saved status could restore the
                                //   wrong I (a task calling from inside its own
                                //   SEI window).
    ldy curTask
    cpy kbFocus
    bne sprRegOut
    pla
    tax                         // X = the offset
    pla                         // A = the value
    sta sprShadow,x
    cli                         // the atomic window ends with the store
    clc
    rts
sprRegOut:
    cli                         // clear the window, then the two parked inputs
    pla
    pla
sprRegBad:
    sec
    rts

.label spriteEnd = *
