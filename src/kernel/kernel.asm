// ==========================================
//  GordonOS -- Kernel Utility Library
// ==========================================
//
//  Jump table at $E000 (fixed address).
//  Tasks call via JSR to these vectors:
//
//    Screen I/O:
//      kClearScreen   -- fill screen with spaces
//      kPrintChar     -- print A at cursor, advance
//      kPrintString   -- print null-term string at (kPtr)
//      kSetCursor     -- X=col, Y=row
//      kGetCursor     -- returns col in X, row in Y
//      kNewline       -- cursor to next line, scroll
//      kScrollScreen  -- move rows up, clear bottom
//      kEnableCursor  -- show blinking cursor
//      kDisableCursor -- hide blinking cursor
//      kPrintAt       -- print string at (X,Y) position
//
//    Screen Lock:
//      kLockScreen    -- prevent scheduler preemption (screen mutex)
//      kUnlockScreen  -- release lock, allow preemption
//
//    Task Info:
//      kGetTaskName   -- X=task index -> kPtr = name string pointer
//
//    Keyboard:
//      kReadKey       -- A=key (0=none)
//      kWaitKey       -- block until key, A=key
//      kReadString    -- read line into buffer at (kPtr), X=maxLen
//
//    String:
//      kStrlen        -- length of string at (kPtr) -> A
//      kStrcpy        -- copy (kPtr) -> (srcPtr)
//      kByteToHex     -- A -> two hex chars at (kPtr)
//      kByteToDec     -- A -> decimal string at (kPtr)
//
//    Math:
//      kMul16         -- kArg x kArg+2 -> kResult (16-bit)
//      kDiv16         -- kArg / kArg+2 -> kResult, rem
//      kRandom        -- random byte -> A
//
//    REU DMA (Hardware-accelerated):
//      kReuToC64      -- copy REU -> C64
//      kReuFromC64    -- copy C64 -> REU
//      kReuFillC64    -- fill C64 RAM with a byte
//      kReuFillReu    -- fill REU RAM with a byte

// ==========================================
//  Kernel argument block ($4000-$4007, in kernel data)
// ==========================================
.label kArg    = $4000       // 4 bytes for mul/div args
.label kResult = $4004       // 4 bytes for mul/div results

// ==========================================
//  Cursor state (in kernel data, not ZP)
// ==========================================
.label cursorCol = cursorState
.label cursorRow = cursorState+1

.pc = $e000

// ==========================================
//  Jump table -- fixed entry points
// ==========================================
kClearScreen:   jmp clearScreen
kPrintChar:     jmp printChar
kPrintString:   jmp printString
kSetCursor:     jmp setCursor
kNewline:       jmp newline
kScrollScreen:  jmp scrollScreen
kEnableCursor:  jmp enableCursor
kDisableCursor: jmp disableCursor
kReadKey:       jmp readKey
kWaitKey:       jmp waitKey
kScanKeyboard:  jmp scanKeyboard
kStrlen:        jmp libStubStrlen
kStrcpy:        jmp libStubStrcpy
kByteToHex:     jmp libStubByteToHex
kByteToDec:     jmp byteToDec
kMul16:         jmp mul16
kDiv16:         jmp div16
kRandom:        jmp random
kGetTime:       jmp libStubTimeGet
kSetTime:       jmp libStubTimeSet
kPrintTime:     jmp libStubTimePrint
kGetCursor:     jmp getCursor
kCursorAddr:    jmp cursorAddr
kLockScreen:    jmp lockScreen
kUnlockScreen:  jmp unlockScreen
kGetTaskName:   jmp getTaskName
kAllocScreen:   jmp allocScreen
kFreeScreen:    jmp freeScreen
kSwitchView:    jmp switchView
kViewScreen:    jmp viewScreen
kShareScreen:   jmp shareScreen
kTakeScreenOwnership: jmp takeScreenOwnership
kReadString:    jmp readString
kSetTextColor:  jmp setTextColor
kSetCursorColor: jmp setCursorColor
kReuToC64:      jmp reuToC64
kReuFromC64:    jmp reuFromC64
kReuFillC64:    jmp reuFillC64
kReuFillReu:    jmp reuFillReu
kSkipSpaces:    jmp libStubSkipSpaces
kHexDigit:      jmp libStubHexDigit
kNibbleToHex:   jmp libStubNibbleToHex
kReuFormat:     jmp libStubFilesysFormat
kReuDirEntry:   jmp libStubFilesysDirEntry
kReuSave:       jmp libStubFilesysSave
kReuLoad:       jmp reuLoadAtomic
kReuDelete:     jmp libStubFilesysDelete
kDynLoad:       jmp dynLoad
kSyscall:       jmp syscall
kDynFree:       jmp dynFree
kCsrBoth:       jmp csrBoth
kGetCursorOn:   jmp getCursorOn
kSetCharShadow: jmp setCharShadow
kCharAt:        jmp charAt
kReadRow:       jmp readRow
kPrintCharAt:   jmp printCharAt
kPrintGlyph:    jmp printGlyph
kReuRename:     jmp libStubFilesysRename
kMalloc:        jmp kMallocImpl
kFree:          jmp kFreeImpl
kReuDirPageDma: jmp reuDirPageDma   // filesys.lib helper (resident)
kReuCmpName:    jmp reuCmpName      // filesys.lib helper (resident)
kReuFindFile:   jmp reuFindFile     // filesys.lib helper (resident)
kSurfDisplayed: jmp surfDisplayed   // gfx.lib helper (resident)
kBlinkReset:    jmp blinkResetK     // erase live block + fresh ON (edit/basic)

// ==========================================
//  Vector block 2 -- graphics group + the scroll-region and blit calls,
//  contiguous with block 1 (the whole table is $E000-$E0F2; $E0F3-$E0F8 is
//  kPrintGlyphAt then kMouseCall, the band's last two entries).
// ==========================================
.pc = $E0C3

// --- Bitmap graphics (geometry + stubs live in gfx.lib, eager) ---
//  Each entry is patched in place to 'jmp <lib routine>' by libLoad
//  (initial target libTrap) via libJumpLo/Hi + libInitLo/Hi -- no
//  libVec indirection. Warm dispatch = one 3-cycle direct jmp.
kPlot:          jmp libTrap         // call 12
kLine:          jmp libTrap         // call 13
kBox:           jmp libTrap         // call 14
kFillBox:       jmp libTrap         // call 15
kCircle:        jmp libTrap         // call 16
kEllipse:       jmp libTrap         // call 17
kFloodFill:     jmp libTrap         // call 18
kGetPixel:      jmp libTrap         // call 19
kClearBitmap:   jmp libTrap         // call 20
kSetGfxColor:   jmp libTrap         // call 21
kGfxChar:       jmp libTrap         // call 22
kGfxString:     jmp libTrap         // call 23
kSetScrollRegion: jmp setScrollRegion
kGetScrollRegion: jmp getScrollRegion
kBlitChar:      jmp blitCharAt
kBlitCharRev:   jmp blitCharAtRev

//  kPrintGlyphAt -- the fixed-cell custom-glyph writer. It takes the first of the
//  two slack entries the band has left ($E0F3-$E0F5).
kPrintGlyphAt:  jmp printGlyphAt

//  kMouseCall -- mouse.lib's one call entry (eager, so libInitLo/Hi gives it
//  libTrap until libLoad patches it in place). It takes the band's last slack
//  entry, $E0F6-$E0F8.
kMouseCall:     jmp libTrap

// ==========================================
//  filesys.lib in-place I/O stubs -- 4 lazy jump entries in the vector-block
//  headroom $E0F9-$E104. The gfx circle scratch is NOT here: the whole gfx.lib
//  scratch (ff* + kEll* + kCircle*) is one contiguous block at $FC3F-$FF7D, at
//  the end of this file.
// ==========================================
.pc = $E0F9
kReuCreate:     jmp libStubFilesysCreate
kReuOpen:       jmp libStubFilesysOpen
kReuReadAt:     jmp libStubFilesysReadAt
kReuWriteAt:    jmp libStubFilesysWriteAt

// ==========================================
//  kStrcmp -- string.lib compare (call 28). Lives in the vector-block
//  headroom $E107 (the groups end at $E0F2; $E0F3-$E0F8 is kPrintGlyphAt
//  and kMouseCall).
// ==========================================
.pc = $E107
kStrcmp:        jmp libStubStrcmp

//  kFillMatrix -- gfx.lib call 24 (13th entry). Lives in the vector-block
//  headroom $E10A (the gfx group is full through $E0E6).
.pc = $E10A
kFillMatrix:    jmp libTrap

//  kSetGfxMode / kFillColorRam -- screen-mgmt.inc, in the remaining
//  vector-block headroom ($E10D / $E110).
.pc = $E10D
kSetGfxMode:    jmp setGfxMode
.pc = $E110
kFillColorRam:  jmp fillColorRam

//  kFpCall -- fp.lib dispatcher (call 25, eager). In the vector-block
//  headroom $E113 (kFillColorRam ends $E112, imports pinned at $E11A).
//  Patched to 'jmp fpExec' when fp.lib loads; tasks reach it with
//  `lda #opcode; jmp kFpCall` (tail-call preserved).
.pc = $E113
kFpCall:        jmp libTrap

.pc = $E116
kReuDmaRange:   jmp reuDmaRange   // 24-bit-offset read DMA (task-side readers)

// ==========================================//  Imported kernel sections (pinned at $E11A -- the gap $E116-$E119
//  is free vector-block headroom)
// ==========================================
.pc = $E11A
.import source "kernel-vars.inc"
.import source "screen-io.inc"
.import source "keyboard.inc"
.import source "scroll-region.inc"   // frag 1 (follows keyboard)
.import source "gfx.inc"             // follows scroll-region
.import source "text-blit.inc"
.import source "screen-mgmt.inc"
.import source "semaphore.inc"
.import source "screen-lock.inc"
.import source "task-util.inc"

// ==========================================
//  REU Filesystem -- internal helpers
//  (moved here from $E0A8 -- Phase A M5; that slot block is now
//  vector-block headroom, pinned graphics group at $E0F6)
// ==========================================

//  Clobbers: A, X, Y
//  Note: reuFsName copied to srcPtr ($F9) because 6502 (indirect),y only
//        works with zero-page pointers.
reuCmpName:
    sty reuFsNameTmp            // save entry offset
    lda reuFsName; sta srcPtr   // copy pointer to ZP for (indirect),y
    lda reuFsName+1; sta srcPtr+1
    ldy #0
rcnLoop:
    lda (srcPtr),y              // search string char
    beq rcnCheckEnd
    cmp #'/' 
    beq rcnCheckSlash
    ldx reuFsNameTmp
    cmp reuDirBuf,x
    bne rcnFail
    inc reuFsNameTmp
    iny
    cpy #11
    bcc rcnLoop                 // field holds 11 chars -- keep comparing
    lda (srcPtr),y              // all 11 name bytes matched: search must end here
    beq rcnMatch
    jmp rcnFail
rcnCheckEnd:
    ldx reuFsNameTmp
    lda reuDirBuf,x
    beq rcnMatch
    cmp #' '
    beq rcnMatch
rcnFail:
    lda #1
    rts
rcnCheckSlash:
    ldx reuFsNameTmp
    lda reuDirBuf,x
    beq rcnMatch
    cmp #' '
    beq rcnMatch
    bne rcnFail
rcnMatch:
    lda #0
    rts

// ==========================================
//  Loader/evict group: malloc -> poolevict -> lib.inc
// ==========================================
.import source "malloc.inc"
.import source "poolevict.inc"
.import source "lib.inc"

// ==========================================
//  dlEvMaybeSelf -- set dlEvictedSelf if the task just swapped is the caller
// ==========================================
//  Input: dlEvictBest = task id just swapped or killed, dlEvictSlot = its slot.
//  Clobbers A, X. Reached by jsr from dlEvMark (single), dlEvMarkShared and
//  dlEvSwapOrKill's kill branch, so its address is not part of any contract.
//  It sits at the top of the free run after sprite.inc, leaving the bottom of
//  that run for the block below to grow into; the core growth headroom it used
//  to occupy is free again for the core band (and pays for the spawnCore ZP
//  carry check).
// ==========================================
.pc = $CA8C

dlEvMaybeSelf:
    ldx dlEvictBest
    cpx curTask
    bne dlEvMsRts
    lda #1
    sta dlEvictedSelf           // the caller is one of the swapped instances
dlEvMsRts:
    rts
.label dlEvMaybeSelfEnd = *

// ==========================================
//  dlProbeFit -- is there a free run of X pages right now?
// ==========================================
//  A load needs ONE contiguous run, so a page count cannot answer this: free
//  pages split into runs smaller than the request are free space the request
//  cannot use, and `pool`, whose rows come from the tables rather than from
//  the bitmap, reports them as free all the same. The allocator's candidate
//  walk is the only place that placement rule lives, so the probe asks it --
//  allocate, then hand the run straight back -- instead of duplicating it.
//  The mark is transient, so it is made with IRQs off: the lib scan in
//  dlPreEvict returns CLI'd, and an interrupt in that window could put another
//  task on the pool and let it see pages this probe is about to give back.
//  Input: X = pages. Output: C=0 a run that long exists / C=1 no such run.
//  dlAllocHi is 0 here: dlProbeFit runs during pre-eviction, before the task's
//  own allocations, and the flag is set only for a library load.
//  Clobbers A, X, Y, kArg+4, kArg+5, kArg+6.
//
//  Home: the loader band, directly below the loader's own growth run. The probe
//  answers a placement question and needs only the allocator, so it belongs
//  beside the loader code that calls it rather than in the $FFxx band.
.pc = $52CD

dlProbeFit:
    sei
    jsr allocFromPool           // X = pages -> A = startPage, C=1 if full
    bcs dlPfRts                 // no run -- C=1 already says so
    tax                         // startPage
    ldy kArg+4                  // pages
    jsr freeFromPool            // give the trial run straight back
    clc
dlPfRts:
    rts
.label dlProbeFitEnd = *

// ==========================================
//  gfx.lib scratch (grouped right after lib.inc):
//  flood-fill span-seed stacks + ellipse scratch
//
//  Pinned: these addresses are ABI -- gfx.lib hardcodes them (see the
//  kCircle* consts in gfx.asm) and main.asm asserts kCircleCxLo/$FF70 and
//  kCircleTemp/$FF7D. The anchor holds them still while the code ahead of it
//  (scroll-region -> gfx -> text-blit -> screen-mgmt -> malloc -> poolevict
//  -> lib) grows into the gap above it: running out of gap is an overlap error
//  here, never a silent move of the scratch.
// ==========================================
.pc = $FC3F

ffStkXLo:  .fill 256, 0   // span-seed X lo stack
ffStkXHi:  .fill 256, 0   // span-seed X hi stack (0/1)
ffStkY:    .fill 256, 0   // span-seed Y stack
ffXLo:     .byte 0        // current X lo
ffXHi:     .byte 0        // current X hi (0/1)
ffY:       .byte 0        // current Y
ffLeftLo:  .byte 0        // segment left boundary X lo
ffLeftHi:  .byte 0
ffRightLo: .byte 0        // segment right boundary X lo
ffRightHi: .byte 0
ffStkPtr:  .byte 0        // stack pointer (entry count)
ffSeedBit: .byte 0        // seed pixel value (0/1 hires, 0-3 mc)
ffFillMc:  .byte 0        // mode flag: 0=hires, 1=mc

kEllCxLo:  .byte 0
kEllCxHi:  .byte 0
kEllCy:    .byte 0
kEllRx:    .byte 0
kEllRy:    .byte 0
kEllA:     .byte 0, 0        // rx^2 (16-bit)
kEllB:     .byte 0, 0        // ry^2 (16-bit)
kEllTwoA:  .byte 0, 0, 0     // 2-rx^2 (24-bit)
kEllTwoB:  .byte 0, 0, 0     // 2-ry^2 (24-bit)
kEllX:     .byte 0
kEllY:     .byte 0
kEllDx:    .byte 0, 0, 0, 0  // 2-b-x (32-bit)
kEllDy:    .byte 0, 0, 0, 0  // 2-a-y (32-bit)
kEllD1:    .byte 0, 0, 0, 0  // region-1 decision (32-bit signed)
kEllD2:    .byte 0, 0, 0, 0  // region-2 decision (32-bit signed)
kEllSqLo:  .byte 0           // 16-bit squaring scratch
kEllSqHi:  .byte 0
kEllPxLo:  .byte 0
kEllPxHi:  .byte 0
kEllPy:    .byte 0
kEllTemp:  .byte 0           // REU RMW buffer

// ==========================================
//  gfx.lib circle scratch -- dedicated to kCircle (NOT shared with
//  kLine/kBox). It has no .pc pin of its own: it flows straight after
//  kEllTemp, so the whole gfx.lib scratch -- ff* + kEll* + kCircle* -- is ONE
//  contiguous block $FC3F-$FF7D (831B). Above it sits the library-entry block:
//  sid.lib's and ipc.lib's call entries, their cold stubs and both name
//  strings at $FF7E-$FFE5 (104B), leaving $FFE6-$FFF9 (20B) free. See
//  main.asm's tail -- it must stay the last thing assembled.
//
//  The addresses are ABI: gfx.lib hardcodes them (see the kCircle* consts
//  in gfx.asm) and main.asm pins kCircleCxLo/$FF70 + kCircleTemp/$FF7D with
//  exact-address .asserts. gxCircle runs SEI'd, so this is atomic and
//  thread-safe even though it is kernel-global (the established gfx-state
//  pattern).
// ==========================================
kCircleCxLo:  .byte 0
kCircleCxHi:  .byte 0
kCircleCy:    .byte 0
kCircleR:     .byte 0
kCircleX:     .byte 0
kCircleY:     .byte 0
kCircleErrLo: .byte 0
kCircleErrHi: .byte 0
kCirclePxLo:  .byte 0
kCirclePxHi:  .byte 0
kCirclePy:    .byte 0
kCircleTmpLo: .byte 0
kCircleTmpHi: .byte 0
kCircleTemp:  .byte 0

// ==========================================
//  printGlyphAt -- write an 8-byte glyph + matrix nibble at (X=col, Y=row)
//  The absolute sibling of printGlyph: kPrintCharAt's fixed-cell write with the
//  glyph taken from the C64 buffer at srcPtr instead of the font. It advances,
//  wraps and scrolls NOTHING, so the last cell of the last row (39,24) is safe
//  here -- printGlyph wraps on the cell it just wrote and wrapClamp then scrolls
//  the whole surface.
//
//  It does not call paRoute: the router blits the FONT glyph for kArg+6 through
//  blitCharSlot, whose font DMA also destroys srcPtr and kArg+5. Its routing
//  decision and its shadow mirror are inlined instead -- the shape printGlyph
//  carries inline for the same reason -- and the cell goes to blitGlyphSlot or
//  blitGlyphFb, which blit from srcPtr.
//  It FINISHES in printCharAt's tail: paNibble writes the matrix nibble and
//  paDone restores the cursor, so the nibble block stays a single copy. Leave
//  through those labels, never by duplicating them.
//
//  Input:  srcPtr = glyph bytes, X = col (0-39), Y = row (0-24)
//  Clobbers: A, X, Y, kPtr, kArg..kArg+6   (srcPtr is read, never written)
//  SEI-guarded (shadow DMA + blit are atomic vs display switches).
//  Split across two pockets by SIZE -- the prologue alone overfills the larger
//  one -- so its tail is jumped to. Both branch targets stay inside one run.
// ==========================================
.pc = $1844

printGlyphAt:
    sei
    lda cursorCol
    pha                         // the live cursor must survive an absolute write
    lda cursorRow
    pha
    stx cursorCol               // X still = col
    sty cursorRow               // Y still = row
    lda #$20
    sta kArg+6                  // the char shadow shows a blank, not a stale cell
    ldx curTask
    lda taskTextColor,x
    sta kArg+5                  // color (blitGlyphSlot reads it; the nibble write
                                //   below re-reads taskTextColor)
    lda taskShadowFlag,x
    beq pgaSurf
    jsr pcShadowChar            // mirrors kArg+6 at (cursorCol,cursorRow); X preserved
pgaSurf:
    jmp pgaTail                 // see the header: the tail needs its own run
.label printGlyphAtEnd = *

.pc = $CA6B

pgaTail:
    jsr surfDisplayed           // X = curTask: pcShadowChar preserves it
    bcs pgaFb                   // displayed -> the framebuffer
    //  Background surface: glyph + colour into the task's own REU slot.
    ldx cursorCol
    ldy cursorRow
    jsr blitGlyphSlot
    jmp paDone                  // shared: cursor restore + cli + rts
pgaFb:
    jsr eraseCursor             // un-draw a live blink block (at its SAVED position)
    ldx cursorCol
    ldy cursorRow
    jsr blitGlyphFb
    jmp paNibble                // shared: matrix nibble + cursor restore + cli + rts
.label printGlyphAtTailEnd = *

// ==========================================
//  gfxSurfPrep -- prepare the running task's cursor for surfSave
// ==========================================
//  doGfxSwitch calls this for the RUNNING task only, immediately before
//  surfSave, and both jobs have to finish before the framebuffer is copied
//  out:
//    1. un-draw a live blink block -- surfSave copies the framebuffer
//       verbatim, so a block drawn at this instant is baked into the
//       outgoing surface's slot, and dgsDone then clears cursorVisible,
//       leaving a phantom cursor where the previous owner's block was;
//    2. persist cursorCol/Row/On into the surface's matrix tail.
//  The caller holds SEI, and eraseCursor restores at the block's SAVED
//  position, so this is a no-op when no block is drawn.
//  X (fromTask -- surfSave's argument) is parked across the call:
//  eraseCursor clobbers A, X, Y, kPtr and kArg+7.
//  Split across two pockets by SIZE -- one run is 17 B -- so the tail is
//  jumped to, the same shape as printGlyphAt above.
// ==========================================
.pc = $CD5C

gfxSurfPrep:
    txa
    pha                         // park fromTask
    jsr eraseCursor
    pla
    tax                         // fromTask back, for surfSave
    lda cursorCol
    sta GFX_MATRIX_BASE + GFX_CURSOR_COL
    jmp gfxSurfPrepTail
.label gfxSurfPrepEnd = *

.pc = $FFE9

gfxSurfPrepTail:
    lda cursorRow
    sta GFX_MATRIX_BASE + GFX_CURSOR_ROW
    lda cursorOn
    sta GFX_MATRIX_BASE + GFX_CURSOR_ON
    rts
.label gfxSurfPrepTailEnd = *

// ==========================================
//  blitGlyphFb -- framebuffer glyph blitter, in the $CDF5 pocket.
//  Input: srcPtr = glyph bytes, X = col (0-39), Y = row (0-24); X preserved.
//  Clobbers: A, Y, kArg..kArg+3, kPtr. One caller: printGlyph.
//  It sits here rather than in text-blit.inc because the $ECDC-$FC3E code
//  chain is packed against the $FC3F gfx.lib anchor: what leaves the chain is
//  the room the chain has to grow. blitFbCommon stays in text-blit.inc and is
//  reached by jmp.
// ==========================================
.pc = $CDF5

blitGlyphFb:
    stx kArg+2                  // col
    sty kArg+3                  // row
    jmp blitFbCommon
.label blitGlyphFbEnd = *
