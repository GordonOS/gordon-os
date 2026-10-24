// ==========================================
//  GordonOS -- Kernel Jump Table Addresses
// ==========================================
//  Stable API addresses at $E000+. Import this
//  in any task (standalone or built-in) to
//  call kernel routines by name instead of
//  hardcoding hexadecimal addresses.
//
//  Usage:  .import "kernel/jumptable.inc"
//          jsr kPrintChar
//          lda #3; jsr kSyscall
//
//  These constants mirror the jump table in
//  kernel.asm.  Each is a 3-byte JMP at the
//  listed address -- tasks JSR directly to it.

.const kClearScreen   = $E000
.const kPrintChar     = $E003
.const kPrintString   = $E006
.const kSetCursor     = $E009
.const kNewline       = $E00C
.const kScrollScreen  = $E00F
.const kEnableCursor  = $E012
.const kDisableCursor = $E015
.const kReadKey       = $E018
.const kWaitKey       = $E01B
.const kScanKeyboard  = $E01E
.const kStrlen        = $E021
.const kStrcpy        = $E024
.const kByteToHex     = $E027
.const kByteToDec     = $E02A
.const kMul16         = $E02D
.const kDiv16         = $E030
.const kRandom        = $E033
.const kGetTime       = $E036
.const kSetTime       = $E039
.const kPrintTime     = $E03C
.const kGetCursor     = $E03F
.const kCursorAddr    = $E042
.const kLockScreen    = $E045
.const kUnlockScreen  = $E048
.const kGetTaskName   = $E04B
.const kAllocScreen   = $E04E
.const kFreeScreen    = $E051
.const kSwitchView    = $E054
.const kViewScreen    = $E057
.const kShareScreen   = $E05A
.const kTakeScreenOwnership = $E05D
.const kReadString    = $E060
.const kSetTextColor  = $E063
.const kSetCursorColor = $E066
.const kReuToC64      = $E069
.const kReuFromC64    = $E06C
.const kReuFillC64    = $E06F
.const kReuFillReu    = $E072
.const kSkipSpaces    = $E075
.const kHexDigit      = $E078
.const kNibbleToHex   = $E07B
.const kReuFormat     = $E07E
.const kReuDirEntry   = $E081
.const kReuSave       = $E084
.const kReuLoad       = $E087
.const kReuDelete     = $E08A
.const kDynLoad       = $E08D
.const kSyscall       = $E090
.const kDynFree       = $E093
.const kCsrBoth       = $E096
.const kGetCursorOn   = $E099
.const kSetCharShadow = $E09C
.const kCharAt        = $E09F
.const kReadRow       = $E0A2
.const kPrintCharAt   = $E0A5
.const kPrintGlyph    = $E0A8
.const kReuRename     = $E0AB
.const kMalloc        = $E0AE
.const kFree          = $E0B1
.const kReuDirPageDma = $E0B4     // filesys.lib helper (resident)
.const kReuCmpName    = $E0B7     // filesys.lib helper (resident)
.const kReuFindFile   = $E0BA     // filesys.lib helper (resident)
.const kSurfDisplayed = $E0BD     // gfx.lib helper (resident)
.const kBlinkReset    = $E0C0     // erase live blink block + fresh ON
.const kPlot          = $E0C3
.const kLine          = $E0C6
.const kBox           = $E0C9
.const kFillBox       = $E0CC
.const kCircle        = $E0CF
.const kEllipse       = $E0D2
.const kFloodFill     = $E0D5
.const kGetPixel      = $E0D8
.const kClearBitmap   = $E0DB
.const kSetGfxColor   = $E0DE
.const kGfxChar       = $E0E1
.const kGfxString     = $E0E4
//  kAllocBitmap / kFreeBitmap are names for the plain surface calls, not
//  entries: the all-bitmap model means every surface IS a bitmap, so both names
//  reach one routine and a task that wants the bitmap vocabulary still
//  assembles -- at no cost in jump-table slots.
.const kAllocBitmap    = kAllocScreen
.const kFreeBitmap     = kFreeScreen
.const kSetScrollRegion = $E0E7
.const kGetScrollRegion = $E0EA
.const kBlitChar       = $E0ED
.const kBlitCharRev    = $E0F0
.const kPrintGlyphAt   = $E0F3   // printGlyphAt, reached from the $1844/$CA6B pockets
.const kMouseCall      = $E0F6   // mouse.lib call 41 (eager) -- the band's slack entry
.const kReuCreate      = $E0F9   // filesys.lib in-place I/O (lazy)
.const kReuOpen        = $E0FC
.const kReuReadAt      = $E0FF
.const kReuWriteAt     = $E102
.const kStrcmp         = $E107   // string.lib compare (call 28) -- in vector-block headroom
.const kFillMatrix     = $E10A   // gfx.lib call 24 (13th entry) -- fill whole matrix
.const kSetGfxMode     = $E10D   // screen-mgmt.inc -- switch hires/mc in place
.const kFillColorRam   = $E110   // screen-mgmt.inc -- fill colour RAM (MC value-3 colour)
.const kFpCall         = $E113   // fp.lib dispatcher (call 25, eager) -- vector-block headroom
.const kReuDmaRange     = $E116   // FS 24-bit-offset read DMA into (kPtr)

//  Sprite support. These are NOT
//  jump-table entries: the $E000 table's 91 slots are full, so they are plain
//  stubs in the $CDDE block, like kStrcmp ($E107). It is also the boundary
//  between sprite.lib and the kernel: the first four are the library's CALL
//  ENTRIES (libJumpLo/Hi points at them, so libLoad rewrites them in place) and
//  kSpriteCollide is the one resident routine of the five, because it reads the
//  collision latches -- kernel state. src/main.asm pins every address.
//  The setters write the shadow of the task whose surface is displayed; the
//  kernel hands that state over when the display changes hands.
.const kSpriteSheet     = $CDDE   // A = sprite 0-7, kPtr = FS sheet filename
.const kSpriteFrame     = $CDE1   // A = sprite 0-7, X = frame 0-255
.const kSpritePos       = $CDE4   // X = sprite 0-7, A = x lo, Y = y, C = x high bit
.const kSpriteCollide   = $CDE7   // -> A = $D01E mask, X = $D01F mask (then cleared)
.const kSpriteReg       = $CDEA   // A = value, X = shadow offset (0..SPR_SHD_SIZE-1)

//  sid.lib (lib 5, lazy) -- two more entries that are NOT $E000 table entries:
//  the table is full, so their 3-byte stubs sit in the free pocket at $FF7E
//  (src/kernel/lib.inc, pinned by src/main.asm).
//      kSidPlay: A = mode -- 0 = kPtr/kPtr+1 is a .mus image in RAM,
//                1 = kPtr/kPtr+1 is a 9-byte config block naming a file in the
//                    REU FS (the caller gets the offset from kReuFindFile) plus
//                    one 256-byte, page-aligned window per voice
//                kArg = delay (ticks/jiffy), kArg+1 = repeats (0 = forever,
//                1 = once, 2-255)
//                -> C=1 refused (another task is already playing)
//      kSidHush: silence the SID and stop a running player (any task, any time)
.const kSidPlay         = $FF7E
.const kSidHush         = $FF81

//  ipc.lib (lib 6, lazy) -- the inter-process byte channels. Same story as
//  sid.lib's pair: the $E000 table is full, so these two 3-byte stubs live in
//  the $FF7E pocket, with their cold stubs, after the sid pair. See
//  docs/lib-ipc.md.
//  kIpcPost: X = channel (0-3), A = byte to send -> C=1 = bad channel
//            (blocks by yielding while that channel's ring is full).
//  kIpcPick: X = channel -> A = byte, C = 1 if that channel is empty.
//  WARNING: Both use kPtr/kPtr+1 as the ring pointer, so both clobber it.
.const kIpcPost         = $FF84
.const kIpcPick         = $FF87

//  kLibHold (library system, not a library) -- "I am done with everything I
//  declared except these". Narrow the task's libMask to the mask in A, then run
//  the loader's own release path, so the other libraries' pool pages go back
//  immediately instead of at task exit -- without it a task holds every library
//  it declared for its whole life, however briefly it needed them.
//      kLibHold: A = MASK of lib ids to KEEP (bit n = lib n) -> C=0 done.
//                Call it LAST: everything else the task declared is released,
//                so every library the task still calls must be in the mask.
.const kLibHold         = $FFE6
.const SPRITE_TASK_SLOTS = 8      // all eight slots -- the VIC's maximum, and every
                                  // one is an ordinary task sprite. (There is no
                                  // ninth: $1000-$1FFF is char ROM to the VIC
                                  // and $2000-$3FFF is the framebuffer, section 1.2.)

//  Syscall numbers (SYS_*) are not defined here -- they live in
//  src/kernel/syscalls.inc, imported below and by src/main.asm for the
//  kernel build. They cannot live in this file: its k* names are real
//  labels in kernel.asm, so the kernel build cannot import it.
.import source "syscalls.inc"
