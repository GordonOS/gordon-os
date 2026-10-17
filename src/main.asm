// ==========================================
//  GordonOS -- Preemptive multitasking kernel
//  Target: Commodore 64 (6510, ~1 MHz)
//  Assembler: KickAssembler v5.25
// ==========================================
//
//  === FOR AI AGENTS / NEW DEVELOPERS ===
//  Full documentation in project root:
//    README.md              -- overview + quick start
//    architecture.md        -- kernel internals, data structures
//    programmers-guide.md   -- syscall API, task patterns
//    future-improvements.md   -- roadmap
//
//  === BUILD ===
//    ninja -C build -t clean && ninja -C build
//    Output: build/gordon-os.prg
//    Requires: KickAssembler v5.25, Java JDK, Ninja, VS64
//
//  === MEMORY MAP ===
//    $0801    BASIC loader (SYS 2064)
//    $0810    Kernel code (this file + imports)
//    $4000    Kernel data -- TCB table (MAX_TASKS x 257 bytes)
//             + tcbLo/Hi, taskState, totalTasks, screen arrays,
//               REU tables, task priorities, data heap
//    $E000    Kernel utility library (jump table + routines)
//    $FFFA    NMI vector -> nmiNull (RTI)
//    $FFFE    IRQ vector -> irqHandler
//
//  === SOURCE FILES (import order) ===
//    kernel/kernel.inc    -- MAX_TASKS=24, TCB_SP=0, TCB_STACK=1
//                            CIA timer $4100 (~60 Hz), ZP $F1-$FF, GFX_SLOT_COUNT=28
//    kernel/init.inc      -- boot: bank KERNAL, set vectors,
//                            initTCB (compile-time tasks),
//                            initTCB_ptr (runtime spawn),
//                            start CIA timer, enter task1 via RTI
//    kernel/scheduler.inc -- switchTask (save stack -> TCB,
//                            findNextAlive, restore TCB -> stack),
//                            restoreStack, reschedule
//    kernel/irq.inc       -- irqHandler (PHA,TXA:PHA,TYA:PHA,
//                            ack CIA, JSR switchTask),
//                            irqRet (PLA:TAY,PLA:TAX,PLA,RTI)
//    kernel/syscall.inc   -- dispatcher (PHP/PLA save SR, SEI),
//                            sysExit(0), sysSpawn(1), sysKill(2),
//                            sysYield(3), sysPause(4), sysResume(5),
//                            sysSetPrio(6), sysGetTid(7),
//                            (8/9 unused -> syscallBad no-op),
//                            sysSemWait(10), sysSemSignal(11),
//                            sysThreadSpawn(12),
//                            findFreeSlot, shrinkTotalTasks,
//                            transferFocusAway
//    kernel/kernel.asm    -- jump table at $E000 + imports:
//                            kernel-vars.inc, screen-io.inc,
//                            keyboard.inc, string.inc, semaphore.inc,
//                            screen-lock.inc, task-util.inc,
//                            screen-mgmt.inc, gfx.inc ($F063)
//    tasks/tasks.inc      -- taskLo/Hi tables, known task registry, flags, data sizes
//    kernel/reu.inc       -- REU DMA routines ($5CA6)
//    kernel/math.inc      -- mul16, div16, random
//
//  === ARCHITECTURE SUMMARY ===
//    Scheduler:  Round-robin, CIA #1 Timer A tick (~60 Hz)
//    Switch:     Full $0100-$01FF saved/restored per task via REU DMA
//    TCB:        257 bytes (savedSP + 256-byte stack image)
//    Stack frame: 8 bytes at $01F8-$01FF, savedSP=$F7
//    RTS +1 bug:  Store irqRet-1 (6502 RTS adds 1)
//    States:      taskState[] = 0=DEAD, 1=ALIVE, 2=PAUSED, 3=SLEEPING
//                 totalTasks grows on spawn, shrinks on exit
//    Reentrant:   task-private dataPtr ($F1-$F2) per instance
//                 data heap at $1C00-$1FFF, bump-allocated
//    Reentrancy:  Syscall dispatcher does SEI; CLI on return
//
//  === SYSCALLS ===
//    Number goes in A, then jsr syscall. Prefer the SYS_* names defined in
//    kernel/syscalls.inc (see syscall.md); the numbers must match
//    syscallTbl in kernel/syscall.inc.
//    lda #SYS_EXIT;        jsr syscall  -> 0  exit (never returns)
//    lda #SYS_SPAWN;       jsr syscall  -> 1  spawn (spawnPtr=$F7-$F8 = addr)
//    lda #SYS_KILL;        jsr syscall  -> 2  kill (X = target index)
//    lda #SYS_YIELD;       jsr syscall  -> 3  yield (resumes at JSR+3)
//    lda #SYS_PAUSE;       jsr syscall  -> 4  pause (X = target index)
//    lda #SYS_RESUME;      jsr syscall  -> 5  resume (X = target index)
//    lda #SYS_SETPRIO;     jsr syscall  -> 6  setprio (X=target, kArg+1=prio 0-5)
//    lda #SYS_GETTID;      jsr syscall  -> 7  gettid (returns curTask in A)
//        8-9 are no-ops (unassigned)
//    lda #SYS_SEMWAIT;     jsr syscall  -> 10 semWait (X = semaphore index; yields
//                                            while it is 0)
//    lda #SYS_SEMSIGNAL;   jsr syscall  -> 11 semSignal (X = semaphore index)
//    lda #SYS_THREADSPAWN; jsr syscall  -> 12 threadSpawn (child named parent.N;
//                                            kArg+6/+7 = zpBase/zpLen, caller's)
//    lda #SYS_SLEEP;       jsr syscall  -> 13 sleep (kArg+1 = ticks, resumes after N)
//
//  === IMPORT FILES ===
    .pc = $0801

    // BASIC loader: 10 SYS 2064
    .byte $0c, $08, $0a, $00, $9e, $32, $30, $36, $34, $00, $00, $00

    .pc = $0810

    // --- Kernel source (code at $0810+) ---
    .import source "kernel/kernel.inc"   // constants + ZP (no code)
.import source "kernel/fs-offset.inc" // 24-bit FS offset macros (no code, no .pc)
    .import source "kernel/syscalls.inc" // syscall numbers (SYS_*) -- shared with tasks
    .import source "kernel/init.inc"     // boot + TCB builders
    .import source "kernel/scheduler.inc"// context switch
    .import source "kernel/irq.inc"      // IRQ handler
    .import source "kernel/syscall.inc"  // syscall API
    .import source "kernel/kernel.asm"   // kernel utility library ($C800 bitmap, $E000 kernel)
    .encoding "screencode_mixed"     // screen codes for task code

    .import source "kernel/reu.inc"      // REU DMA routines ($C800+)
    .import source "kernel/math.inc"     // math utilities

// ==========================================
//  Dynamic task loader tables (in $C800-$CFFF)
// ==========================================
.pc = $CA99                   // pinned -- kernel-data.inc aliases + task binaries bake this base in

dynTaskOriginLo:   .fill MAX_DYN_TASKS, 0
dynTaskOriginHi:   .fill MAX_DYN_TASKS, 0
dynTaskSizeLo:     .fill MAX_DYN_TASKS, 0
dynTaskSizeHi:     .fill MAX_DYN_TASKS, 0
dynTaskId:         .fill MAX_DYN_TASKS, $FF
dynTaskStartPage:  .fill MAX_DYN_TASKS, 0
dynTaskPageCount:  .fill MAX_DYN_TASKS, 0
dynTaskResidency:  .fill MAX_DYN_TASKS, $FF
dynTaskFlags:      .fill MAX_DYN_TASKS, 0
dynTaskCount:      .byte 0

// --- Shared-code residency records (name-keyed, 10B each) ---
//  Reentrant binaries share ONE loaded copy; records key on the
//  binary name (pointer into the per-slot dynTaskNameBuf).
resNameLo:         .fill MAX_RESIDENTS, 0
resNameHi:         .fill MAX_RESIDENTS, 0
resAddrLo:         .fill MAX_RESIDENTS, 0
resAddrHi:         .fill MAX_RESIDENTS, 0
resSizeLo:         .fill MAX_RESIDENTS, 0
resSizeHi:         .fill MAX_RESIDENTS, 0
resStartPage:      .fill MAX_RESIDENTS, 0
resPageCount:      .fill MAX_RESIDENTS, 0
resRefCount:       .fill MAX_RESIDENTS, 0
resCount:          .byte 0

// --- Pool bitmap ---
poolBitmap:        .fill POOL_BITMAP_BYTES, 0   // POOL_PAGES bits

// --- Generic page-granular malloc blocks (kMalloc/kFree) ---
//  One entry per live block. owner = GordonOS task slot ($FF = free);
//  swapped = pages freed, data preserved in the per-dyn-slot REU shadow.
mallocOwner:       .fill MAX_MALLOC_BLOCKS, $FF
mallocStartPage:   .fill MAX_MALLOC_BLOCKS, 0
mallocPages:       .fill MAX_MALLOC_BLOCKS, 0
mallocSwapped:     .fill MAX_MALLOC_BLOCKS, 0
mallocCount:       .byte 0

// --- Per-dyn-slot program-RAM block (KF_MALLOC_RAM) ---
//  startPage=$FF while swapped.
dynTaskRamStartPage: .fill MAX_DYN_TASKS, 0
dynTaskRamPages:     .fill MAX_DYN_TASKS, 0


// --- Per-slot dynamic task name buffers (12 B each) ---
//  Each dynamic task copies its name here so `ps`/`kill` show the
//  task's OWN name, not whichever dynamic task was loaded last.
dynTaskNameBuf:
    .fill MAX_DYN_TASKS * 12, 0
dynTaskNameLo:
    .for (var i = 0; i < MAX_DYN_TASKS; i++)
        .byte <(dynTaskNameBuf + (i * 12))
dynTaskNameHi:
    .for (var i = 0; i < MAX_DYN_TASKS; i++)
        .byte >(dynTaskNameBuf + (i * 12))

// --- Per-surface text/cursor colors ---
//  Foreground (color-RAM low nibble) per surface; kSetTextColor and
//  kSetCursorColor write these.
taskTextColor:   .fill MAX_TASKS, 0
taskCursorColor: .fill MAX_TASKS, 0

// --- Per-task kReadString state ---
taskReadStrBufLo:   .fill MAX_TASKS, 0
taskReadStrBufHi:   .fill MAX_TASKS, 0
taskReadStrLen:     .fill MAX_TASKS, 0
taskReadStrMax:     .fill MAX_TASKS, 0
taskReadStrPos:     .fill MAX_TASKS, 0
taskReadStrStartRow:.fill MAX_TASKS, 0

// ==========================================
//  Boot task code -- loads the shell from the host-built REU FS.
//  Lives right after the dyn tables; boot/bootEnd are referenced by init.inc.
// ==========================================
.pc = *
    .import source "tasks/boot.inc"
.label bootEnd = *

// ==========================================
//  Dynamic kernel library descriptors.
//  Read-only, indexed by lib id. Lives in the free pocket after the
//  boot task (bootEnd) and before char-shadow.inc ($CE00).
//  libNameLo/Hi point at "<name>.lib\0" strings in kernel text;
//  libEntryCount = number of exposed call stubs per lib;
//  libEager = non-zero -> load at spawn (else lazy, first call).
// ==========================================
.pc = *
libNameLo:
    //  WARNING: THE ORDER OF THESE IS THE LIBRARY ID. It must match the LIB_*
    //  constants in src/kernel/task-header.inc: tasks put those ids in their
    //  libMask and libstubs.inc passes them to libLoad, so reordering here
    //  renumbers every task binary in the tree. tools/check-lib-ids.ps1
    //  compares this list against task-header.inc and fails the build on drift.
    //  ids: LIB_TIME LIB_FILESYS LIB_GFX LIB_FP LIB_STRING LIB_SID LIB_IPC
    //       LIB_SPRITE LIB_MOUSE
    .byte <libNameTime, <libNameFilesys, <libNameGfx, <libNameFp, <libNameString
    .byte <libNameSid, <libNameIpc, <libNameSprite, <libNameMouse
    .fill MAX_LIBS - LIB_COUNT, 0
libNameHi:
    .byte >libNameTime, >libNameFilesys, >libNameGfx, >libNameFp, >libNameString
    .byte >libNameSid, >libNameIpc, >libNameSprite, >libNameMouse
    .fill MAX_LIBS - LIB_COUNT, 0
libEntryCount:
    .byte 3, 9, 13, 1, 7, 2, 2, 4, 1  // LIB_TIME LIB_FILESYS LIB_GFX LIB_FP
                                      //   LIB_STRING LIB_SID LIB_IPC
                                      //   LIB_SPRITE LIB_MOUSE (call-index bases)
    .fill MAX_LIBS - LIB_COUNT, 0
libEager:
    .byte 0, 0, 1, 1, 0, 0, 0, 1, 1  // LIB_GFX + LIB_FP + LIB_SPRITE +
                                     //   LIB_MOUSE eager, the rest lazy
    .fill MAX_LIBS - LIB_COUNT, 0

// --- Per-dyn-slot library mask -- the 16-bit libMask each
//  dynamic task declared; dynFree reads it to release libraries. ---
.pc = *
dynTaskLibMaskLo: .fill MAX_DYN_TASKS, 0
dynTaskLibMaskHi: .fill MAX_DYN_TASKS, 0

// ==========================================
//  Per-task scroll region. Lives in the free pocket
//  $CD6D-$CDFF before char-shadow.inc ($CE00). Default 0..24 = whole
//  screen (current behavior).
// ==========================================
.pc = $CD6D
taskScrollTop:    .fill MAX_TASKS, 0
taskScrollBottom: .fill MAX_TASKS, 24
scrollSeedBuf:    .byte 0, 0

// ==========================================
//  Orphan free-list -- spawnCore pushes dead slots' wrong-size buffers here
//  before recycling so mixed-size churn stays recyclable. Lives in the
//  $4304-$4329 gap.
// ==========================================
.pc = $4304
freeListSize: .fill FREE_LIST_ENTRIES, 0
freeListLo:   .fill FREE_LIST_ENTRIES, 0
freeListHi:   .fill FREE_LIST_ENTRIES, 0

// ==========================================
//  Dynamic task loader code -- sets its own .pc = $4310 (owns $4310-$529F)
// ==========================================
    .import source "kernel/dynload.inc"

// ==========================================
//  Static data block -- lib runtime tables + gfx LUTs + task font/shadow,
//  in the low-band hole at $189F+.
//  libStartPage/Pages/RefCount are indexed by lib id (0..15).
//  libJumpLo/Hi + libInitLo/Hi let libLoad patch each call's 3-byte
//  kernel jump entry in place.
// ==========================================
.pc = $189F
libStartPage:  .fill MAX_LIBS, $FF    // $FF = not loaded
libPages:      .fill MAX_LIBS, 0      // pool pages held
libRefCount:   .fill MAX_LIBS, 0      // resident tasks depending on it

//  LIB_CALLS is the width of all four scan tables, and it is the ceiling the
//  library system actually has (docs/dynamic-libraries.md section 4). The last
//  index is mouse.lib's (41).
.const LIB_CALLS = 42           // time3 + filesys9 + gfx13 + fp1 + string7
                                //   + sid2 (kSidPlay/kSidHush)
                                //   + ipc2 (kIpcPost/kIpcPick)
                                //   + sprite4 (kSpriteSheet/Frame/Pos/Reg)
                                //   + mouse1 (kMouseCall)
                                //  WARNING: this is the ceiling -- the 16-byte
                                //  growth reservoir below is spent, so the tables
                                //  end flush against `gfxRowLutLo` again and a
                                //  further call must be a dispatching entry
                                //  (fpExec / kFpCall is the pattern).

libJumpLo:
    .byte <kGetTime, <kSetTime, <kPrintTime                 // time 0-2
    .byte <kReuFormat, <kReuSave, <kReuDelete, <kReuRename  // filesys 3-6
    .byte <kReuDirEntry                                     // filesys 7
    .byte <kReuCreate, <kReuOpen, <kReuReadAt, <kReuWriteAt // filesys 8-11
    .byte <kPlot, <kLine, <kBox, <kFillBox                  // gfx 12-23
    .byte <kCircle, <kEllipse, <kFloodFill, <kGetPixel
    .byte <kClearBitmap, <kSetGfxColor, <kGfxChar, <kGfxString
    .byte <kFillMatrix                                      // gfx 24
    .byte <kFpCall                                          // fp 25
    .byte <kStrlen, <kStrcpy, <kStrcmp, <kSkipSpaces        // string 26-32
    .byte <kByteToHex, <kHexDigit, <kNibbleToHex
    .byte <kSidPlay, <kSidHush                              // 33-34: sid.lib
    .byte <kIpcPost, <kIpcPick                             // 35-36: ipc.lib
    .byte <kSpriteSheet, <kSpriteFrame, <kSpritePos        // 37-39: sprite.lib
    .byte <kSpriteReg                                      // 40: sprite.lib
    .byte <kMouseCall                                      // 41: mouse.lib (eager)
libJumpHi:
    .byte >kGetTime, >kSetTime, >kPrintTime
    .byte >kReuFormat, >kReuSave, >kReuDelete, >kReuRename
    .byte >kReuDirEntry
    .byte >kReuCreate, >kReuOpen, >kReuReadAt, >kReuWriteAt
    .byte >kPlot, >kLine, >kBox, >kFillBox
    .byte >kCircle, >kEllipse, >kFloodFill, >kGetPixel
    .byte >kClearBitmap, >kSetGfxColor, >kGfxChar, >kGfxString
    .byte >kFillMatrix
    .byte >kFpCall
    .byte >kStrlen, >kStrcpy, >kStrcmp, >kSkipSpaces
    .byte >kByteToHex, >kHexDigit, >kNibbleToHex
    .byte >kSidPlay, >kSidHush                              // 33-34: sid.lib
    .byte >kIpcPost, >kIpcPick                             // 35-36: ipc.lib
    .byte >kSpriteSheet, >kSpriteFrame, >kSpritePos        // 37-39: sprite.lib
    .byte >kSpriteReg                                      // 40: sprite.lib
    .byte >kMouseCall                                      // 41: mouse.lib (eager)

libInitLo:
    .byte <libStubTimeGet, <libStubTimeSet, <libStubTimePrint
    .byte <libStubFilesysFormat, <libStubFilesysSave, <libStubFilesysDelete, <libStubFilesysRename
    .byte <libStubFilesysDirEntry
    .byte <libStubFilesysCreate, <libStubFilesysOpen, <libStubFilesysReadAt, <libStubFilesysWriteAt
    .byte <libTrap, <libTrap, <libTrap, <libTrap            // gfx -- eager -> libTrap
    .byte <libTrap, <libTrap, <libTrap, <libTrap
    .byte <libTrap, <libTrap, <libTrap, <libTrap
    .byte <libTrap
    .byte <libTrap                                          // fp -- eager -> libTrap
    .byte <libStubStrlen, <libStubStrcpy, <libStubStrcmp, <libStubSkipSpaces
    .byte <libStubByteToHex, <libStubHexDigit, <libStubNibbleToHex
    .byte <libStubSidPlay, <libStubSidHush                  // 33-34: sid.lib (lazy)
    .byte <libStubIpcPost, <libStubIpcPick                  // 35-36: ipc.lib (lazy)
    .byte <libTrap, <libTrap, <libTrap, <libTrap           // 37-40: sprite.lib
                                                           //   (eager -> libTrap)
    .byte <libTrap                                         // 41: mouse.lib (eager)
libInitHi:
    .byte >libStubTimeGet, >libStubTimeSet, >libStubTimePrint
    .byte >libStubFilesysFormat, >libStubFilesysSave, >libStubFilesysDelete, >libStubFilesysRename
    .byte >libStubFilesysDirEntry
    .byte >libStubFilesysCreate, >libStubFilesysOpen, >libStubFilesysReadAt, >libStubFilesysWriteAt
    .byte >libTrap, >libTrap, >libTrap, >libTrap
    .byte >libTrap, >libTrap, >libTrap, >libTrap
    .byte >libTrap, >libTrap, >libTrap, >libTrap
    .byte >libTrap
    .byte >libTrap
    .byte >libStubStrlen, >libStubStrcpy, >libStubStrcmp, >libStubSkipSpaces
    .byte >libStubByteToHex, >libStubHexDigit, >libStubNibbleToHex
    .byte >libStubSidPlay, >libStubSidHush                  // 33-34: sid.lib (lazy)
    .byte >libStubIpcPost, >libStubIpcPick                  // 35-36: ipc.lib (lazy)
    .byte >libTrap, >libTrap, >libTrap, >libTrap           // 37-40: sprite.lib
    .byte >libTrap                                         // 41: mouse.lib (eager)
.label libThunkTablesEnd = *

// --- LIB_CALLS growth point (the reservoir is spent) ---
//  Each extra call index needs 4 bytes -- one in each of the four tables. The
//  pinned addresses below (`gfxRowLut*`, `FONT_*`, the task shadows, `libstubs`)
//  sit directly after the tables with no gap, so growing the tables SHIFTS every
//  one of them 4 bytes up, and the aliased copies in gfx.asm / kernel.inc /
//  kernel-data.inc have to move with it (the asserts in this file name each one;
//  docs/kernel-fragmentation.md records the current boundaries).
//  WARNING: the next call index is not free -- either fund another +4 shift of
//  this whole tail, or expose it as a dispatching entry the way fp.lib exposes a
//  single `fpExec`.
//  WARNING: Keep this point here, above `gfxRowLutLo`. Below the LUTs, growth shifts
//  `gfxRowLut*` and the whole `FONT_*` block (the asserts in this file are the
//  only thing that would catch the stale copies in gfx.asm / kernel.inc /
//  kernel-data.inc).

gfxRowLutLo:
    .for (var r = 0; r < 25; r++)
        .byte <($2000 + (r * 320))
gfxRowLutHi:
    .for (var r = 0; r < 25; r++)
        .byte >($2000 + (r * 320))

// Cell row offset: r*40 for r=0..24 (matrix nibble calculation)
gfxCellRowLutLo:
    .for (var r = 0; r < 25; r++)
        .byte <(r * 40)
gfxCellRowLutHi:
    .for (var r = 0; r < 25; r++)
        .byte >(r * 40)

// REU-only font -- the glyph data itself lives in the REU.
// 48B total, and downstream addresses depend on that size.
fontRecLo:      .byte 0          // current font FS data-area offset (24-bit)
fontRecMid:     .byte 0
fontRecHi:      .byte 0
fontC2:         .byte 0          // blitChar col/row stash across the glyph DMA
fontC3:         .byte 0
fontC6:         .byte 0          // blitCharSlot kArg+6/+7 stash
fontC7:         .byte 0
fontGlyphs:     .fill 8, 0       // 8-byte glyph DMA scratch
fontChar:       .byte 0          // fontGlyph's char stash (A clobber guard)
taskShadowFlag:    .fill MAX_TASKS, 0
taskShadowHi:      .fill MAX_TASKS, 0

    .import source "kernel/char-shadow.inc"   // .pc = $CE00 (free $CDA6-$DFFF)
    .import source "kernel/libstubs.inc"      // .pc = $1A17 -- lazy call stubs
    .import source "kernel/scroll-slot.inc"   // .pc = $54AB -- scrollScreenSlot (loader band)

// ==========================================
//  Sprite support -- sprite.inc sets its own
//  four .pc blocks. The 8 fixed sprite slots fragment the free space so badly
//  that the routines cannot share one pocket, hence:
//    $CD9F-$CDFF  sprBlkAddr + sprRestoreFrames, the 5 public stubs and
//                 sprPtrBytes
//    $1CC0-$1D3F  sprFrameDma          (128B pocket, below the data heap $1D40)
//    $567D-$56FF  rasterCommit         (131B pocket, below the pool $5700)
//    $CFA4-$CFFF  tables + state       (92B pocket, above char-shadow $CE00)
//  The stubs are NOT in the $FF7E-$FFF9 run: that run is the gfx scratch's
//  growth headroom. sid.lib's entries, cold stubs and name string hold its
//  bottom 52B and ipc.lib's the next 52B, so the entry block is $FF7E-$FFE5
//  (104B), leaving $FFE6-$FFF9 (20B) free.
// ==========================================
    .import source "kernel/sprite.inc"

// --- sprite: region ends (each block's neighbour) ---
.assert "sprite: install helpers clear the scroll-region data", (scrollSeedBuf + 2 <= $CD9F) ? 1 : 0, 1
.assert "sprite: install helpers fit below the entry stubs ($CDDE)", (sprInstallHelpEnd <= $CDDE) ? 1 : 0, 1
.assert "sprite: sheet code + stubs fit below char-shadow ($CE00)", (sprStubEnd <= $CE00) ? 1 : 0, 1
.assert "sprite: char-shadow stays below the sprite tables ($CFA4)", (charShadowEnd <= $CFA4) ? 1 : 0, 1
.assert "libstubs must not grow past sysExit's block at $1C6E", (libStubsEnd <= $1C6E) ? 1 : 0, 1
.assert "sprite: frame code fits below the data heap ($1D40)", (sprFrameEnd <= $1D40) ? 1 : 0, 1
.assert "sprite: tables fit below I/O ($D000)", (sprDataEnd <= $D000) ? 1 : 0, 1
.assert "sprite: raster commit clears scrollScreenSlot", (scrollSlotEnd <= $567D) ? 1 : 0, 1
.assert "sprite: raster commit fits below the pool ($5700)", (rasterCommitEnd <= $5700) ? 1 : 0, 1
.assert "sprite: API fits below the pinned dyn tables ($CA99)", (sprApiEnd <= $CA99) ? 1 : 0, 1
.assert "sprite: install fits the loader band before font-help", (sprInstallEnd <= $5340) ? 1 : 0, 1
.assert "sprite: warm reset fits the freed $529E pocket", (warmSpriteResetEnd <= $52CD) ? 1 : 0, 1
.assert "sprite: pointer bytes fit below char-shadow ($CE00)", (sprPtrBytesEnd <= $CE00) ? 1 : 0, 1
//  The commit's two copy loops use abs,x, and a page crossing costs an extra
//  cycle per iteration (19 accesses/frame). Both must sit inside one page.
.assert "sprite: shadow does not straddle a page", ((sprShReg & $FF00) == ((sprShReg + 16) & $FF00)) ? 1 : 0, 1
.assert "sprite: colours do not straddle a page", ((sprShColour & $FF00) == ((sprShColour + 7) & $FF00)) ? 1 : 0, 1

// --- sprite: slot addresses are forced, so guard the regions they sit in ---
//  THE CHAR ROM BOUNDARY IS THE LOAD-BEARING ONE. $1000-$1FFF in VIC bank 0 is
//  character ROM to every VIC fetch, so a slot placed there renders font bytes
//  instead of art. Any slot must stay below $1000 or
//  this build fails.
.assert "sprite: no slot reaches the char ROM mirror ($1000)", (SPRITE_SLOT7 + 64 <= $1000) ? 1 : 0, 1
.assert "sprite: all 8 slots are 64-byte aligned", (((SPRITE_SLOT0&63) + (SPRITE_SLOT1&63) + (SPRITE_SLOT2&63) + (SPRITE_SLOT3&63) + (SPRITE_SLOT4&63) + (SPRITE_SLOT5&63) + (SPRITE_SLOT6&63) + (SPRITE_SLOT7&63)) == 0) ? 1 : 0, 1
.assert "sprite: slots clear EhBASIC vars ($0200-$02A0)", (SPRITE_SLOT0 >= $02A1) ? 1 : 0, 1
.assert "sprite: slots clear the video matrix ($0400)", (SPRITE_SLOT4 + 64 <= $0400) ? 1 : 0, 1
.assert "sprite: low band is contiguous ($02C0-$03FF)", ((SPRITE_SLOT4 - SPRITE_SLOT0) == 4*64) ? 1 : 0, 1
.assert "sprite: high band is contiguous ($0F00-$0FBF)", ((SPRITE_SLOT7 - SPRITE_SLOT5) == 2*64) ? 1 : 0, 1
.assert "sprite: the two bands do not overlap", (SPRITE_SLOT5 >= SPRITE_SLOT4 + 64) ? 1 : 0, 1
//  $0F00-$0FBF must stay clear of BOTH
//  slots-of-other-things and ordinary code. $0FC0 stays free on purpose (8 is the
//  hardware maximum), and the two asserts below are the tripwires that catch an
//  edit that moves code into the band.
.assert "sprite: high band ends before $0FC0", (SPRITE_SLOT7 + 64 <= $0FC0) ? 1 : 0, 1
.assert "sprite: sysExit stayed out of the sprite band", (sysExit >= $0FC0) ? 1 : 0, 1
.assert "sprite: spawnCore stayed out of the sprite band", (spawnCore >= $0FC0) ? 1 : 0, 1
//  The syscall handler table must clear the band as well, and that is not a code
//  space question: the last entry's two bytes are the handler ADDRESS, so a slot
//  drawn over them sends the dispatcher anywhere the art happens to point.
.assert "syscall: table ends below the sprite band", ((syscallTbl + 2*SYSCALL_COUNT) <= SPRITE_SLOT5) ? 1 : 0, 1
//  No slot is reserved for anything: eight slots, eight task sprites, and the
//  public calls reject only 8+ (out of range). A task that wants a mouse pointer
//  drives a sprite like any other.
.assert "sprite: every slot is a task slot (no reservation)", SPRITE_TASK_SLOTS, SPRITE_SLOT_COUNT
.assert "sprite: slot count is eight (the hardware maximum)", SPRITE_SLOT_COUNT, 8
.assert "sprite: enable mask is $FF at eight slots", SPR_ENABLE_MASK, $FF
//  The relocated routines must fit their reserved runs -- $1CBF is the last byte
//  before spriteFrame ($1CC0), and the spawnCore piece's ceiling is taskLo
//  ($186F). The sprite gate is in the loader band; see sprite.inc.
.assert "sprite: moved sysExit fits below spriteFrame ($1CC0)", (sysExitMovedEnd <= $1CC0) ? 1 : 0, 1
.assert "sprite: moved spawnCore piece fits below taskLo ($186F)", (spawnPieceEnd <= $186F) ? 1 : 0, 1
//  The deadline guard only holds if the deadline is BEFORE the first line a
//  sprite can be visible on: a sprite at Y=31 is displayed from line 32 on PAL,
//  so a deadline above 31 would skip commits that were still in time.
.assert "sprite: commit deadline precedes the earliest sprite line", (SPR_COMMIT_DEADLINE <= 31) ? 1 : 0, 1
// --- sprite: task-facing ABI, mirrored in jumptable.inc ---
.assert "jumptable.inc alias: kSpriteSheet moved", kSpriteSheet, $CDDE
.assert "jumptable.inc alias: kSpriteFrame moved", kSpriteFrame, $CDE1
.assert "jumptable.inc alias: kSpritePos moved", kSpritePos, $CDE4
.assert "jumptable.inc alias: kSpriteCollide moved", kSpriteCollide, $CDE7
.assert "jumptable.inc alias: kSpriteReg moved", kSpriteReg, $CDEA
//  The task-side shadow header (src/tasks/sprite-shadow.inc) publishes these
//  offsets as an ABI; a layout change must fail here rather than corrupt tasks.
.assert "sprite-shadow.inc alias: SPR_SHD_SIZE changed", SPR_SHD_SIZE, 39
.assert "sprite-shadow.inc alias: SPR_SHD_COLOUR moved", (sprShColour - sprShReg), 24
.assert "sprite-shadow.inc alias: SPR_SHD_XHI moved", (sprShXHi - sprShReg), 16
.assert "sprite-shadow.inc alias: SPR_SHD_ENABLE moved", (sprShEnable - sprShReg), 32
.assert "sprite: slot owner table follows the shadow", (sprFrameIdx - sprShReg), SPR_SHD_SIZE
//  The live region is ONE CONTIGUOUS RUN in the saved block's own order, and
//  SPR_BLK_LEN is the length of both halves of a hand-over: if these disagree,
//  sprInstall would save a different set of bytes than it loads.
.assert "sprite: the per-task block is one run", SPR_BLK_LEN, (sprFrameIdx + SPRITE_SLOT_COUNT - sprLiveState)
.assert "sprite: the block is sheet/hi/shadow/frames in that order", (spriteSheetHi - sprLiveState), 16
.assert "kernel-data.inc alias: curTask moved", curTask, $FD
.assert "kernel-data.inc alias: sprLiveOwner moved", sprLiveOwner, $CFFB
//  Task-side literals that must track the kernel: if the kernel side moves,
//  the build fails HERE so both sides are updated together (the alias files
//  are src/tasks/kernel-data.inc and src/tasks/sprite-shadow.inc).
.assert "kernel-data.inc alias: sprShadow moved", sprShReg, $CFBC
.assert "kernel-data.inc alias: sprFrameIdx moved", sprFrameIdx, $CFE3
.assert "kernel-data.inc alias: frameCount moved", sprFrameCount, $CFFF
.assert "sprite-shadow.inc alias: SPR_SLOT0 changed", SPRITE_SLOT0, $02C0
.assert "sprite-shadow.inc alias: SPR_SLOT4 changed", SPRITE_SLOT4, $03C0
.assert "sprite-shadow.inc alias: SPR_SLOT5 changed", SPRITE_SLOT5, $0F00
.assert "sprite-shadow.inc alias: SPR_SLOT7 changed", SPRITE_SLOT7, $0F80
.assert "jumptable.inc alias: SPRITE_TASK_SLOTS changed", SPRITE_TASK_SLOTS, 8

// ==========================================
//  Data heap relocated to $1C00 (was $0400).
//  $0400-$07FF now free for bitmap video matrix.
// ==========================================

// ==========================================
//  Kernel data at $4000
// ==========================================
.pc = $4000                    // kernel data. savedSP=24B, taskStackReu*=48B, per-task arrays ~= 1.0KB. Total ~= 1.1KB ($4000-$445D). Was ~7.6KB with TCB table.

// --- Kernel argument block ---
    .byte 0, 0, 0, 0, 0, 0, 0, 0   // $4000-$4007: kArg(4) + kResult(4)

// --- savedSP: the ONLY per-task TCB data kept in C64 RAM ---
savedSP:
    .fill MAX_TASKS, 0

// --- REU stack base addresses (precomputed at boot) ---
taskStackReuLo:
    .fill MAX_TASKS, 0
taskStackReuHi:
    .fill MAX_TASKS, 0

// --- Task state: 1=ALIVE, 0=DEAD ---
taskState:
    .fill MAX_TASKS, 0          // all DEAD; init.inc sets compile-time slots ALIVE

// --- Runtime task count (grows/shrinks) ---
totalTasks:
    .byte 0                     // set by init

// --- Task name pointers (indexed by slot) ---
taskNameLo:
    .fill MAX_TASKS, 0
taskNameHi:
    .fill MAX_TASKS, 0

// --- Task priority (1-255, higher = more slices) ---
taskPriority:
    .fill MAX_TASKS, 0
taskSliceLeft:
    .fill MAX_TASKS, 0          // slices remaining this round

// --- Virtual screen arrays ---
screenAlloc:
    .fill MAX_TASKS, 0          // 1 = task has a surface
screenOwner:
    .fill MAX_TASKS, 0          // $FF = owns, 0-7 = shares this task's surface

// --- Per-task data pointer arrays (for re-entrant tasks) ---
taskDataPtrLo:
    .fill MAX_TASKS, 0          // per-instance data buffer low byte
taskDataPtrHi:
    .fill MAX_TASKS, 0          // per-instance data buffer high byte
taskDataSize:
    .fill MAX_TASKS, 0          // bytes allocated for this instance (0 = none)

// --- Parent task tracking (for cascading kill) ---
taskParent:
    .fill MAX_TASKS, $ff        // $FF = no parent, else parent task index

// --- Thread index counter (per task, for naming) ---
taskThreadIdx:
    .fill MAX_TASKS, 0          // increments on each threadSpawn

// --- Thread name REU addresses (per-slot, for "parent.N" naming) ---
taskThreadNameReuLo:
    .fill MAX_TASKS, 0
taskThreadNameReuHi:
    .fill MAX_TASKS, 0

// --- Per-instance data heap (bump allocator) ---
// Starts after REU FS code (which can grow to ~$1D40 with bank-crossing DMA)
dataHeap:
    .word $1D40                 // next free byte in heap
dataHeapEnd:
    .word $2000                 // end of heap ($1D40-$1FFF = 704B)

// --- Per-task ZP DMA save/restore ---
taskZpBase:
    .fill MAX_TASKS, 0          // start of ZP region to save (0 = no ZP save)
taskZpLen:
    .fill MAX_TASKS, 0          // length of ZP region (0 = skip DMA)
taskZpReuLo:
    .fill MAX_TASKS, 0          // REU shadow address lo (precomputed at boot)
taskZpReuHi:
    .fill MAX_TASKS, 0          // REU shadow address hi

// --- Bitmap mode tracking (screen kind per task) ---
screenKind:
    .fill MAX_TASKS, 0          // 0=none, 1=text, 2=std bitmap, 3=mc bitmap

// --- Per-task bitmap REU slot addresses ---
taskGfxReuLo:
    .fill MAX_TASKS, 0
taskGfxReuHi:
    .fill MAX_TASKS, 0
taskGfxReuBank:
    .fill MAX_TASKS, 0

// --- Per-task bitmap VIC colors ---
taskGfxBorder:
    .fill MAX_TASKS, 0
taskGfxBg0:
    .fill MAX_TASKS, 0
taskGfxBg1:
    .fill MAX_TASKS, 0
taskGfxBg2:
    .fill MAX_TASKS, 0

// --- REU Filesystem state variables ---
reuFsDirBaseLo:     .byte 0
reuFsDirBaseHi:     .byte 0
reuFsDirBaseBank:   .byte 0
reuFsDataBaseLo:    .byte 0
reuFsDataBaseHi:    .byte 0
reuFsDataBaseBank:  .byte 0
reuFsCapacityLo:    .byte 0
reuFsCapacityHi:    .byte 0
reuFsCapacity2:     .byte 0        // 24-bit capacity MSB
reuTotalBanks:      .word 256
reuFsEnabled:       .byte 0
reuFsNameTmp:       .byte 0
reuFsPtr:           .byte 0, 0, 0  // 24-bit FS data offset
reuFsRemain:        .word 0
reuFsFile:          .word 0
reuFsLen:           .word 0
reuFsFlags:         .byte 0
reuFsName:          .word 0
cmdArgsBuf:         .fill 38, 0  // foreground-command args ( writes, command reads)

// ==========================================
//  Scratch text buffer -- 1 KB at $C400-$C7FF
//  When a bitmap is displayed, background text tasks render
//  here instead of their C64 screen buffer at $2000+slot*$0400.
//  The scheduler DMAs this <-> REU char shadow on each context switch.
// ==========================================
.pc = $C400
scratchBuf:
    .fill $0400, 0              // 1 KB scratch text buffer (right before I/O)

// ==========================================
//  Task registry at $186F -- the free pocket after the REU FS block, so the
//  kernel core block can grow past $132C. The FS end assert below guards
//  against growth into it.
// ==========================================
.pc = $186F
.import source "tasks/tasks.inc"    // taskLo/Hi lookup tables

// ==========================================
//  REU Filesystem data at $13D4
// ==========================================
.pc = $13D4
reuDirBuf:
    .fill REU_DIR_PAGE_SIZE, 0     // $13D4-$14D3: 256B directory page buffer

.import source "kernel/reu-fs.inc"  // REU filesystem code after reuDirBuf

// ==========================================
//  REU-only font helpers -- loader-headroom pocket $5340-$54AA (free
//  between dynload $4310-$533F and scrollScreenSlot $54AB).
// ==========================================
.pc = $5340
.import source "kernel/font-help.inc"

// ==========================================
//  Library pool placement -- placed sequentially right after
//  font-help, i.e. in the rest of the same loader-headroom pocket. Kernel
//  code only: the 109-page pool is untouched, so nothing is taken from
//  tasks. See pool-top.inc.
// ==========================================
.import source "kernel/pool-top.inc"

// ==========================================
//  Load-cursor helpers -- loader band, $5457. Placed sequentially right after
//  pool-top.inc so the three loader-band helpers ($5340 font-help, $53F2
//  pool-top, $5457 cursor-helpers) stay together and share one .pc chain.
//
//  WARNING: They live DOWN here, not in the high region: $FF70-$FFBE is the growth
//  headroom for the gfx/misc band (lib.inc + gfx.lib scratch), which ends
//  flush against it at $FF6F. Keeping the helpers in the loader band leaves
//  that pocket free to merge with $FFCD-$FFF9 into the 124B $FF7E-$FFF9 run --
//  of which $FF7E-$FFE5 (104B) is the library-entry block (sid.lib, then
//  ipc.lib), leaving $FFE6-$FFF9 (20B).
//
//  Cost: the loader band's high half belongs to these helpers, leaving
//  $5482-$54AA (41B) for slot-shadow below.
// ==========================================
.pc = $5457
.import source "kernel/cursor-helpers.inc"
.assert "cursor helpers fit below scrollScreenSlot", (cursorHelpEnd <= $54AB) ? 1 : 0, 1
.assert "font helpers fit below scrollScreenSlot", (fontHelpEnd <= $54AB) ? 1 : 0, 1
.assert "pool-top helpers fit below scrollScreenSlot", (poolTopEnd <= $54AB) ? 1 : 0, 1

// ==========================================
//  Slot-shadow clearing -- the loader band's last free run, $5482-$54AA (41B).
//  Placed sequentially after cursor-helpers, so the four loader-band helpers
//  ($5340 font-help, $53F2 pool-top, $5457 cursor-helpers, $5482 slot-shadow)
//  share one .pc chain and none of them takes anything from the pool or from
//  the core's growth headroom. See slot-shadow.inc.
// ==========================================
.pc = $5482
.import source "kernel/slot-shadow.inc"
.assert "slot-shadow helpers fit below scrollScreenSlot", (slotShadowEnd <= $54AB) ? 1 : 0, 1

//  The kernel core ($0810+) is bounded by reuDirBuf, pinned at $13D4. Nothing
//  guarded this before: syscall.inc is the last core file, so if it grew past
//  $13D4 the .pc = $13D4 above would silently collide with it and the build
//  would still be green.
.assert "kernel core fits below reuDirBuf ($13D4)", (kernelCoreEnd <= $13D4) ? 1 : 0, 1

// ==========================================
//  M5 guard: src/tasks/dynamic/basic.asm hardcodes these kernel-
//  internal addresses as aliases. A kernel move must fail the build
//  here instead of breaking BASIC silently at runtime.
// ==========================================
.assert "basic.asm alias: screenBase moved", screenBase, $E11C
.assert "kernel-vars.inc alias: cursorCol moved", cursorCol, $E11A
.assert "kernel-vars.inc alias: cursorRow moved", cursorRow, $E11B
.assert "kernel-vars.inc alias: cursorOn moved", cursorOn, $E22C
.assert "kernel-vars.inc alias: cursorVisible moved", cursorVisible, $E22B
.assert "kernel-vars.inc alias: blinkTick moved", blinkTick, $E22A
.assert "kernel-data.inc alias: kbFocus moved", kbFocus, $E1FF
.assert "kernel-data.inc alias: keybHeld moved", keybHeld, $E214
.assert "kernel-data.inc alias: keybCount moved", keybCount, $E216
.assert "kernel-data.inc alias: keybTail moved", keybTail, $E218
.assert "kernel-data.inc alias: keybBuffer moved", keybBuffer, $E219
.assert "kernel-data.inc alias: semCount moved", semCount, $E120
.assert "kernel-data.inc alias: semOwner moved", semOwner, $E124
.assert "keyboard.inc alias: keybRepeatLda moved", keybRepeatLda, $E549
.assert "keyboard.inc alias: keybInitialLda moved", keybInitialLda, $E55E
.assert "keyboard.inc alias: mouseKind moved", mouseKind, $E530
.assert "keyboard.inc alias: sysSpeedShift moved", sysSpeedShift, $E531

// shell.asm aliases (relocated shell hardcodes these kernel
// addresses -- a kernel move must fail this build, not the shell's)
.assert "shell.asm alias: taskState moved", taskState, $4050
.assert "shell.asm alias: totalTasks moved", totalTasks, $4068
.assert "shell.asm alias: taskPriority moved", taskPriority, $4099
.assert "shell.asm alias: screenAlloc moved", screenAlloc, $40C9
.assert "kernel-data.inc alias: taskParent moved", taskParent, $4141
.assert "kernel-data.inc alias: cmdArgsBuf moved", cmdArgsBuf, $42DE
.assert "kernel-data.inc alias: FONT_REC_LO moved", FONT_REC_LO, $19DB
.assert "kernel-data.inc alias: reuFsDataBaseLo moved", reuFsDataBaseLo, $42C8
.assert "kernel-data.inc alias: reuFsDataBaseHi moved", reuFsDataBaseHi, $42C9
.assert "kernel-data.inc alias: reuFsDataBaseBank moved", reuFsDataBaseBank, $42CA
.assert "kernel-data.inc alias: taskShadowFlag moved", taskShadowFlag, $19EB
.assert "kernel-data.inc alias: taskShadowHi moved", taskShadowHi, $1A03
.assert "char-shadow region fits under ZP shadows", (SHADOW_BASE + MAX_TASKS*$400 <= REU_ZP_BASE) ? 1 : 0, 1
//  The scheduler computes the kArg shadow address as `task x 8` with NO add --
//  that is only the full address because the base is page-aligned and the whole
//  table fits in one page. Break either and every task silently reads and
//  writes another task's kArg image.
.assert "kArg shadow base is page-aligned", ((REU_KARG_BASE & $FF) == 0) ? 1 : 0, 1
.assert "kArg shadow table fits one page", (MAX_TASKS*8 <= 256) ? 1 : 0, 1
//  `sysThreadSpawn` computes the thread-name address as `slot x 16` with NO
//  add -- the shift's overflow is the high byte's carry -- so the base's low
//  byte must be zero, and the table must span at most two pages (one carry).
//  Break either and a thread child in slot 16+ writes its name over the child
//  16 slots earlier, which `ps` then shows.
.assert "thread-name base is page-aligned", ((REU_THREAD_NAME & $FF) == 0) ? 1 : 0, 1
.assert "thread-name table needs at most one carry", (MAX_TASKS*16 <= 512) ? 1 : 0, 1
//  `sprBlkAddr` computes a slot's sprite block as `slot x 128` with no add and
//  no table -- the base's low byte must be zero for the low byte of the result
//  to be `(slot&1) << 7`, and the shift's overflow (one bit) is the high byte's
//  carry. Break either and a task's sprite state is read from the wrong block.
//  The region must also clear the thread-name table below it, and stay out of
//  bank 0's $D000+ range (REU address space, but no reason to look like C64 I/O).
.assert "sprite block base is page-aligned", ((REU_SPR_BASE & $FF) == 0) ? 1 : 0, 1
.assert "sprite blocks clear the thread names", (REU_SPR_BASE >= REU_THREAD_NAME + MAX_TASKS*16) ? 1 : 0, 1
.assert "sprite blocks stay below the $D000 mark", (REU_SPR_BASE + MAX_TASKS*128 <= $D000) ? 1 : 0, 1
.assert "shell.asm alias: reuFsCapacityLo moved", reuFsCapacityLo, $42CB
.assert "shell.asm alias: reuFsCapacityHi moved", reuFsCapacityHi, $42CC
.assert "kernel-data.inc alias: reuFsCapacity2 moved", reuFsCapacity2, $42CD
.assert "shell.asm alias: reuFsEnabled moved", reuFsEnabled, $42D0
.assert "shell.asm alias: reuTotalBanks moved", reuTotalBanks, $42CE
.assert "kernel-data.inc alias: reuFsDirBaseLo moved", reuFsDirBaseLo, $42C5
.assert "kernel-data.inc alias: reuFsDirBaseHi moved", reuFsDirBaseHi, $42C6
.assert "kernel-data.inc alias: reuFsDirBaseBank moved", reuFsDirBaseBank, $42C7
.assert "kernel-data.inc alias: reuFsPtr moved", reuFsPtr, $42D2
.assert "kernel-data.inc alias: reuFsRemain moved", reuFsRemain, $42D5
.assert "kernel-data.inc alias: reuFsFile moved", reuFsFile, $42D7
.assert "kernel-data.inc alias: reuFsLen moved", reuFsLen, $42D9
.assert "kernel-data.inc alias: reuFsFlags moved", reuFsFlags, $42DB
.assert "kernel-data.inc alias: reuFsName moved", reuFsName, $42DC
.assert "kernel-data.inc alias: reuDirBuf moved", reuDirBuf, $13D4
.assert "shell.asm alias: osName moved", osName, $AD3
.assert "shell.asm alias: osVersion moved", osVersion, $ADC
.assert "shell.asm alias: taskLo moved", taskLo, $186F
.assert "shell.asm alias: taskHi moved", taskHi, $1887
.assert "gfxdemo.asm alias: taskGfxReuLo moved", taskGfxReuLo, $421D
.assert "gfxdemo.asm alias: taskGfxReuHi moved", taskGfxReuHi, $4235
.assert "gfxdemo.asm alias: taskGfxReuBank moved", taskGfxReuBank, $424D
.assert "gfx.asm alias: taskGfxBorder moved", taskGfxBorder, $4265
.assert "gfx.asm alias: FONT_REC_LO moved", FONT_REC_LO, $19DB
.assert "gfx.asm alias: FONT_GLYPHS moved", FONT_GLYPHS, $19E2
.assert "gfx.asm alias: screenKind moved", screenKind, $4205
.assert "gfx.asm alias: gfxPixelMasks moved", gfxPixelMasks, $ECE9
.assert "gfx.asm alias: gfxCol moved", gfxCol, $ED40
.assert "gfx.asm alias: dispIsBitmap moved", dispIsBitmap, $ED45
.assert "gfx.asm alias: kLineX1Lo moved", kLineX1Lo, $ED46
.assert "gfx.asm alias: kBoxColor moved", kBoxColor, $ED56
.assert "gfx.asm alias: gfxOp moved", gfxOp, $ED5D
.assert "gfx.asm alias: gfxRowLutLo moved", gfxRowLutLo, $1977
.assert "gfx.asm alias: gfxCellRowLutLo moved", gfxCellRowLutLo, $19A9
.assert "gfx.asm alias: kCircleCxLo moved", kCircleCxLo, $FF70
.assert "gfx.asm alias: kCircleTemp moved", kCircleTemp, $FF7D
.assert "gfx.asm alias: kEllCxLo moved", kEllCxLo, $FF49
.assert "gfx.asm alias: kEllTemp moved", kEllTemp, $FF6F
.assert "gfx.asm alias: ffStkXLo moved", ffStkXLo, $FC3F
.assert "gfx.asm alias: ffSeedBit moved", ffSeedBit, $FF47

// jumptable.inc hardcodes these bitmap-graphics jump entries (they live
// AFTER reuCmpName in the $E000+ section, so a kernel-size change shifts
// them and breaks every task that JSRs them by address). Guard them.
.assert "jumptable.inc alias: kGetCursorOn moved", kGetCursorOn, $E099
.assert "jumptable.inc alias: kSetCharShadow moved", kSetCharShadow, $E09C
.assert "jumptable.inc alias: kCharAt moved", kCharAt, $E09F
.assert "jumptable.inc alias: kReadRow moved", kReadRow, $E0A2
.assert "jumptable.inc alias: kPrintCharAt moved", kPrintCharAt, $E0A5
.assert "jumptable.inc alias: kPrintGlyph moved", kPrintGlyph, $E0A8
.assert "jumptable.inc alias: kPrintGlyphAt moved", kPrintGlyphAt, $E0F3
.assert "jumptable.inc alias: kReuRename moved", kReuRename, $E0AB
.assert "jumptable.inc alias: kMalloc moved", kMalloc, $E0AE
.assert "jumptable.inc alias: kFree moved", kFree, $E0B1
.assert "jumptable.inc alias: kReuDirPageDma moved", kReuDirPageDma, $E0B4
.assert "jumptable.inc alias: kReuCmpName moved", kReuCmpName, $E0B7
.assert "jumptable.inc alias: kReuFindFile moved", kReuFindFile, $E0BA
.assert "jumptable.inc alias: kSurfDisplayed moved", kSurfDisplayed, $E0BD
.assert "jumptable.inc alias: kBlinkReset moved", kBlinkReset, $E0C0
.assert "jumptable.inc alias: kReuCreate moved", kReuCreate, $E0F9
.assert "jumptable.inc alias: kReuOpen moved", kReuOpen, $E0FC
.assert "jumptable.inc alias: kReuReadAt moved", kReuReadAt, $E0FF
.assert "jumptable.inc alias: kReuWriteAt moved", kReuWriteAt, $E102
.assert "reu-fs.inc: FS block grows into task registry", (reuFsCodeEnd <= taskLo) ? 1 : 0, 1
.assert "reu-fs.inc: reuLoadAtomic must stay above $0801 (VICE autostart)", (reuLoadAtomic >= $0801) ? 1 : 0, 1
.assert "jumptable.inc alias: kPlot moved", kPlot, $E0C3
.assert "jumptable.inc alias: kLine moved", kLine, $E0C6
.assert "jumptable.inc alias: kBox moved", kBox, $E0C9
.assert "jumptable.inc alias: kFillBox moved", kFillBox, $E0CC
//  kAllocBitmap / kFreeBitmap have no entry and no address of their own: their
//  names are .const aliases of kAllocScreen / kFreeScreen in jumptable.inc, so
//  there is nothing to pin here.
.assert "jumptable.inc alias: kCircle moved", kCircle, $E0CF
.assert "jumptable.inc alias: kEllipse moved", kEllipse, $E0D2
.assert "jumptable.inc alias: kFloodFill moved", kFloodFill, $E0D5
.assert "jumptable.inc alias: kGetPixel moved", kGetPixel, $E0D8
.assert "jumptable.inc alias: kClearBitmap moved", kClearBitmap, $E0DB
.assert "jumptable.inc alias: kSetGfxColor moved", kSetGfxColor, $E0DE
.assert "jumptable.inc alias: kGfxChar moved", kGfxChar, $E0E1
.assert "jumptable.inc alias: kGfxString moved", kGfxString, $E0E4
.assert "shell.asm alias: dynTaskOriginLo moved", dynTaskOriginLo, $CA99
.assert "shell.asm alias: dynTaskSizeLo moved", dynTaskSizeLo, $CAA9
.assert "shell.asm alias: dynTaskId moved", dynTaskId, $CAB9
.assert "shell.asm alias: dynTaskResidency moved", dynTaskResidency, $CAD1
.assert "shell.asm alias: dynTaskCount moved", dynTaskCount, $CAE1
.assert "kernel-data.inc alias: resStartPage moved", resStartPage, $CAFA
.assert "kernel-data.inc alias: resPageCount moved", resPageCount, $CAFE
.assert "kernel-data.inc alias: resRefCount moved", resRefCount, $CB02
.assert "kernel-data.inc alias: poolBitmap moved", poolBitmap, $CB07
.assert "kernel-data.inc alias: mallocOwner moved", mallocOwner, $CB15
.assert "kernel-data.inc alias: mallocStartPage moved", mallocStartPage, $CB1D
.assert "kernel-data.inc alias: mallocPages moved", mallocPages, $CB25
.assert "kernel-data.inc alias: mallocSwapped moved", mallocSwapped, $CB2D
.assert "kernel-data.inc alias: mallocCount moved", mallocCount, $CB35
.assert "kernel-data.inc alias: dynTaskRamStartPage moved", dynTaskRamStartPage, $CB36
.assert "kernel-data.inc alias: dynTaskRamPages moved", dynTaskRamPages, $CB3E
.assert "kernel-data.inc alias: libStartPage moved", libStartPage, $189F
.assert "kernel-data.inc alias: libNameLo moved", libNameLo, $CD0C
.assert "kernel-data.inc alias: libEager moved", libEager, $CD3C
.assert "sid: kSidPlay entry stub moved", kSidPlay, $FF7E
.assert "sid: kSidHush entry stub moved", kSidHush, $FF81
.assert "ipc: kIpcPost entry stub moved", kIpcPost, $FF84
.assert "ipc: kIpcPick entry stub moved", kIpcPick, $FF87
//  sprite.lib's four call entries are the $CDDE stubs (sprite.inc); the assert
//  for them is with the other sprite addresses. Nothing follows kLibHold in the
//  $FF7E pocket, so $FFE9 stays free.
.assert "lib jump table fits before lib init table", (libJumpHi + LIB_CALLS <= libInitLo) ? 1 : 0, 1
.assert "lib thunk tables are exactly LIB_CALLS wide", (((libJumpHi - libJumpLo) == LIB_CALLS) ? 1 : 0) * (((libInitLo - libJumpHi) == LIB_CALLS) ? 1 : 0) * (((libInitHi - libInitLo) == LIB_CALLS) ? 1 : 0), 1
.assert "lib descriptors fit below char-shadow", (libEager + MAX_LIBS <= $CE00) ? 1 : 0, 1
.assert "fp lib call base", (3 + 9 + 13), 25
.assert "string lib call base", (3 + 9 + 13 + 1), 26
.assert "dynTaskLibMask fits below char-shadow", (dynTaskLibMaskHi + MAX_DYN_TASKS <= $CE00) ? 1 : 0, 1
.assert "lib loader fits below hardware vectors", (libNameString < $FFFA) ? 1 : 0, 1
//  The loader band really ends at $533F: font-help.inc is pinned at $5340 and
//  lives inside the loader's headroom pocket by design. Asserting against
//  scrollScreenSlot ($54AB) allowed 165..527 bytes of growth to overwrite
//  font-help without failing the build.
.assert "dynload fits before font-help ($5340)", (dlLibsEnd <= $5340) ? 1 : 0, 1
.assert "libstubs fit below data heap", (libStubsEnd <= $1D40) ? 1 : 0, 1
.assert "shell.asm alias: POOL_BASE moved", POOL_BASE, $5700
.assert "shell.asm alias: POOL_PAGES changed", POOL_PAGES, 109
.assert "shell.asm alias: POOL_END moved", POOL_END, $C400
.assert "pool: POOL_PAGE_SIZE = $100 << SHIFT", POOL_PAGE_SIZE, ($0100 << POOL_PAGE_SHIFT)
.assert "pool: POOL_PAGES derived from SIZE", POOL_PAGES, 109
.assert "pool: POOL_END derived", POOL_END, $C400
.assert "pool: bitmap bytes", POOL_BITMAP_BYTES, 14
.assert "pool: MAX_MALLOC_PAGES derived", MAX_MALLOC_PAGES, 64
.assert "jumptable.inc alias: kSetScrollRegion moved", kSetScrollRegion, $E0E7
.assert "jumptable.inc alias: kGetScrollRegion moved", kGetScrollRegion, $E0EA
.assert "jumptable.inc alias: kBlitChar moved", kBlitChar, $E0ED
.assert "jumptable.inc alias: kBlitCharRev moved", kBlitCharRev, $E0F0
.assert "scroll-region data fits below char-shadow", (scrollSeedBuf + 2 <= $CE00) ? 1 : 0, 1
.assert "scroll-region code fits below lib.inc", (scrollRegionEnd <= $FE66) ? 1 : 0, 1
.assert "blitCharAt fits below lib.inc", (blitCharAtEnd <= $FE66) ? 1 : 0, 1
.assert "kPrintCharAt router fits the pocket under gfx.inc ($ECDC)", (paRouteEnd <= $ECDC) ? 1 : 0, 1
.assert "blitGlyphFb relocated to the $CDF5 pocket", blitGlyphFb, $CDF5
.assert "blitGlyphFb fits its pocket (char-shadow is $CE00)", (blitGlyphFbEnd <= $CE00) ? 1 : 0, 1
//  printGlyphAt is one routine in two runs: the prologue is too big for the
//  larger pocket, so its tail is a `jmp` away. Neither half may cross the block
//  that follows its run (taskLo, then the pinned dyn tables).
.assert "printGlyphAt fits the $1844 pocket (taskLo is $186F)", (printGlyphAtEnd <= $186F) ? 1 : 0, 1
.assert "printGlyphAt's tail fits the $CA6B pocket (pinned dyn tables at $CA8C)", (printGlyphAtTailEnd <= $CA8C) ? 1 : 0, 1
.assert "gfxSurfPrep fits the $CD5C pocket (17B, before the scroll-region data)", (gfxSurfPrepEnd <= $CD6D) ? 1 : 0, 1
.assert "gfxSurfPrep's tail fits the $FFE9 pocket (17B, above kLibHold)", (gfxSurfPrepTailEnd <= $FFFA) ? 1 : 0, 1
.assert "scrollScreenSlot fits in reclaimed pool bottom", (scrollSlotEnd <= POOL_BASE) ? 1 : 0, 1

//  kLibHold's routine lives in the $1D16-$1D3F pocket. Imported HERE, right
//  before the `.pc = $FF7E` below, and nowhere else: a `.pc` takes over the
//  location counter, so importing it earlier would emit every following byte
//  from $1D3F upward and break the six gfx.asm aliases pinned to those
//  addresses. The $FF7E block resets the counter, so nothing is disturbed.
.import source "kernel/lib-hold.inc"

//  Asserted here, not in the block above, because `.assert` conditions must be
//  resolvable in the FIRST parse -- libHoldEnd does not exist until the import.
.assert "lib: libHold fits the $1D16 pocket", (libHoldEnd <= $1D40) ? 1 : 0, 1
.assert "lib: kLibHold entry stub moved", kLibHold, $FFE6
//  The warm-reboot reset lives at $181D, in the run between spawnCore's
//  relocated first half and taskLo ($186F) -- the `sprite.lib` name string
//  follows it at $1839. Same `.pc` caveat as
//  kLibHold -- lib-hold.inc's `.pc = $1D16` resets the counter again. WARNING: It must
//  NOT move into the core's growth headroom: that pocket has to stay free for
//  the core band to grow into.
.import source "kernel/warm-boot-reset.inc"

.assert "lib: warmBootReset fits below taskLo ($186F)", (warmResetEnd <= $186F) ? 1 : 0, 1
.assert "lib: warmBootReset leaves the core its headroom", (warmResetEnd <= $181D+$23) ? 1 : 0, 1

// ==========================================
//  sprite.lib -- name string
// ==========================================
//  The name cannot live in the $FFxx band (kLibHold is pinned at $FFE6 and
//  nothing may be inserted before it), so "sprite.lib" sits in the free pocket
//  between warmBootReset and the task tables. `libNameLo/Hi[7]` point at it.
//
//  WARNING: sprite.lib HAS NO COLD STUB AND NO ENTRY BYTE OUTSIDE THE $CDDE STUB
//  BLOCK. It is EAGER, so `libInitLo/Hi` gives all four of its call entries the
//  same `libTrap` target that every eager library uses, `dynLoad` loads it at
//  spawn, and `libLoad` patches the entries themselves (src/kernel/sprite.inc).
.pc = $1839
libNameSprite:  .text "sprite.lib"; .byte 0
.label libNameSpriteEnd = *

.assert "sprite lib name fits below taskLo ($186F)", (libNameSpriteEnd <= $186F) ? 1 : 0, 1

// ==========================================
//  mouse.lib -- name string
// ==========================================
//  "mouse.lib" is 10 bytes with its NUL, which is what the 12-byte
//  relocation-slack pocket above the sprite slots ($0FC0-$0FCB) holds. It cannot
//  join the block in lib.inc, which ends flush at $FF7D, and `libNameLo/Hi[8]`
//  reach it from anywhere. mouse.lib is EAGER, so `libInitLo/Hi` gives its one
//  call entry the same `libTrap` target every eager library uses.
.pc = $0FC0
libNameMouse:   .text "mouse.lib"; .byte 0
.label libNameMouseEnd = *

.assert "mouse lib name fits in the $0FC0 pocket", (libNameMouseEnd <= $0FCC) ? 1 : 0, 1

// ==========================================
//  sid.lib's call entries + lazy stubs -- the $FF7E pocket
// ==========================================
//  These two 3-byte entries could not go in the $E000 jump table (full: its
//  only free byte is $E119), and their lazy cold stubs could not go in
//  libstubs.inc (that block ends at $1C6E, hard against syscall.inc's sysExit
//  block -- see the note there). So both live in the largest free pocket in the
//  map, exactly like the five kSprite* stubs in the $CDDE pocket. Using the
//  biggest hole rather than a small one keeps the smaller holes available.
//
//  `kSidPlay`/`kSidHush` are task-visible through jumptable.inc's consts and are
//  pinned by the asserts above; `libJumpLo/Hi` points at these entries and
//  `libInitLo/Hi` at the stubs, so `libLoad` patches the entry and `libFree`
//  restores it.
//
//  WARNING: THIS BLOCK MUST STAY THE LAST THING ASSEMBLED. A `.pc` sets the location
//  counter, so anything emitted afterwards continues from here -- at the very top
//  of RAM -- and silently walks off the end of addressable memory (that is how
//  the first attempt at this broke six gfx.asm aliases). Nothing follows these
//  bytes except the asserts below.
//
//  ALL FOUR ENTRIES COME FIRST, then every cold stub, then the name strings --
//  so each entry address is fixed arithmetic ($FF7E + 3n) and can be pinned by
//  an assert, rather than depending on the size of a stub above it.
.pc = $FF7E
kSidPlay:       jmp libStubSidPlay
kSidHush:       jmp libStubSidHush
kIpcPost:       jmp libStubIpcPost
kIpcPick:       jmp libStubIpcPick

libStubSidPlay:
    jsr libColdSave
    lda #LIB_SID                // lib id
    jsr libLoad
    jsr libColdRestore          // preserves C, and the kArg/kPtr arguments
    bcs libStubSidPlayFail
    jmp kSidPlay                // the entry is now 'jmp <sidPlay>'
libStubSidPlayFail:
    jmp libLoadFail

libStubSidHush:
    jsr libColdSave
    lda #LIB_SID                // lib id
    jsr libLoad
    jsr libColdRestore
    bcs libStubSidHushFail
    jmp kSidHush                // the entry is now 'jmp <sidHush>'
libStubSidHushFail:
    jmp libLoadFail

libStubIpcPost:
    jsr libColdSave
    lda #LIB_IPC                // lib id
    jsr libLoad
    jsr libColdRestore
    bcs libStubIpcPostFail
    jmp kIpcPost                // the entry is now 'jmp <ipcPost>'
libStubIpcPostFail:
    jmp libLoadFail

libStubIpcPick:
    jsr libColdSave
    lda #LIB_IPC                // lib id
    jsr libLoad
    jsr libColdRestore
    bcs libStubIpcPickFail
    jmp kIpcPick                // the entry is now 'jmp <ipcPick>'
libStubIpcPickFail:
    jmp libLoadFail

//  The lib name strings live in this pocket rather than beside the other lib
//  names in lib.inc: that block ends at $FF7D, so more bytes there would consume
//  the pocket this block sits in. `libNameLo/Hi[5]`/`[6]` point at them either
//  way, so the only cost is that they are not adjacent to their siblings.
libNameSid:     .text "sid.lib"; .byte 0
libNameIpc:     .text "ipc.lib"; .byte 0

//  kLibHold is NOT a library entry -- it is library-system machinery -- so it sits
//  at the far end of the pocket instead of joining the entry run above (that run
//  is $FF7E + 3n by contract, because libJumpLo/Hi index it). Its stub jumps
//  straight at the routine: no cold-load stub, no libLoad.
kLibHold:       jmp libHold

//  sprite.lib's four call entries are NOT here: they are the first four stubs in
//  src/kernel/sprite.inc's $CDDE block (see there for why). The run above stays
//  `$FF7E + 3n` for the lazy libraries only.

.label libEntryStubBlockEnd = *

.assert "lib entries + stubs fit the $FF7E pocket", (libEntryStubBlockEnd <= $FFF9) ? 1 : 0, 1
