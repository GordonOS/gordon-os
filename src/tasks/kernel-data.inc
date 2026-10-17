// ==========================================
//  kernel-data.inc -- shared kernel-data address aliases
// ==========================================
//  Resolved addresses from the kernel build (kernel.inc / kernel-vars.inc
//  / main.asm) -- NOT stable jump-table entries. Keep in sync with the
//  kernel; src/main.asm .asserts the critical ones, so a kernel move
//  fails the kernel build instead of breaking tasks silently.
//
//  Imported ONCE from tools/assemble-tasks.asm. KickAssembler symbols are
//  global across all segments, so task sources must NOT re-define these.

// 24-bit FS offset macros (FSCopy/FSAdd16/FSAddPage/FSToReuAddr). Imported
// here because this file is imported exactly once for the whole task/lib
// assembly -- a second import would re-define the macros (KickAssembler
// symbols, macros included, are global across all segments).
.import source "kernel/fs-offset.inc"

// Task header constants (HDR_*, KF_*, LIB_*) come from
// src/kernel/task-header.inc, imported once by the assemble-tasks preamble --
// not from this file.

// Kernel constants shared with the kernel build: MAX_TASKS, MAX_SEMAPHORES,
// TA_*, REU_BOUNCE, REU_FS_SIGNATURE, MAX_DYN_TASKS, MAX_MALLOC_BLOCKS,
// MAX_RESIDENTS, MAX_LIBS. Imported here (like fs-offset.inc above) since
// this file is imported exactly once for the whole task/lib assembly.
.import source "kernel/kernel-shared.inc"

.const taskState         = $4050
.const totalTasks        = $4068
.const taskPriority      = $4099
.const screenAlloc       = $40C9
.const blinkRate        = $F0      // cursor blink interval (runtime-settable via blink.com)
//  The pointer device this machine has: 0 = off, 1 = 1351. The `mouse` command
//  is the only writer; the task that talks to the device is the reader, and
//  mouse.lib reads the hardware rather than this byte.
.const mouseKind        = $E530
//  The machine's speed relative to real time, as a signed exponent: 1 = 200%,
//  0 = 100%, -1 = 50%. `speed.com` is the only writer (it accepts powers of
//  two only) and it also derives the kernel's blink/key-repeat ticks from it.
//  sid.lib shifts the tune's pitch and its ticks-per-jiffy delay by it, and a
//  task that paces itself in displayed frames shifts its own durations by it.
.const sysSpeedShift    = $E531
.const kbFocus          = $E1FF
.const keybHeld         = $E214    // the key currently held, $FF = none. A LEVEL: the IRQ
                                   // scan sets it on a press and leaves it until the key
                                   // is released, so a task can test "is this key down" --
                                   // a paddle or a smooth cursor needs that. kReadKey is
                                   // the EVENT side of the same scan and delivers on its
                                   // repeat cadence (KEYB_INITIAL, then KEYB_REPEAT), not
                                   // once per tick. IRQ-written, so read it in one `lda`.
                                   // It tracks the keyboard whatever the focus is, so a
                                   // task must gate on its own `kbFocus` before acting.
.const keybCount        = $E216    // keyboard ring buffer (IRQ-owned -- SEI before use)
.const keybTail         = $E218
.const keybBuffer       = $E219
.const keybRepeatPatch  = $E54A    // operand byte of lda #KEYB_REPEAT (keyboard.inc)
.const keybInitialPatch = $E55F    // operand byte of lda #KEYB_INITIAL (keyboard.inc)
.const taskParent        = $4141
//  Counting semaphores (kernel.inc MAX_SEMAPHORES = 4). Index 0 is the SCREEN
//  lock -- never touch it from a task; 1-3 are yours, via syscall 10 (acquire,
//  X = index, blocks by yielding while the count is 0) and syscall 11 (release,
//  X = index). A task sets the initial count with a plain store to semCount+X
//  (one byte, atomic). Ownership is tracked in the kernel only, and semOwner is
//  best-effort: a single byte cannot record several holders of a counting
//  semaphore -- only the task that took the last slot (the 1->0 transition) is
//  named, and only that case is auto-released if it dies.
// (MAX_SEMAPHORES comes from kernel-shared.inc, imported above.)
.const semCount          = $E120   // 4 bytes: 0 = unavailable, 1-255 = free slots
.const semOwner          = $E124   // 4 bytes: owner task, or $FF for none
.const cmdArgsBuf       = $42DE
.const reuFsDirBaseLo   = $42C5
.const reuFsDirBaseHi   = $42C6
.const reuFsDirBaseBank = $42C7
.const reuFsCapacityLo   = $42CB
.const reuFsCapacityHi   = $42CC
.const reuFsCapacity2    = $42CD    // 24-bit capacity MSB
.const reuTotalBanks     = $42CE
.const reuFsEnabled      = $42D0
.const reuFsDataBaseLo   = $42C8
.const reuFsDataBaseHi   = $42C9
.const reuFsDataBaseBank = $42CA
.const reuFsPtr          = $42D2    // FS offset (find/save/load scratch) -- 24-bit
.const reuFsRemain       = $42D5    // FS size (find/save/load scratch)
.const reuFsFile         = $42D7    // entry page scratch (save/rename)
.const reuFsLen          = $42D9    // entry index scratch (save/rename)
.const reuFsFlags        = $42DB    // page counter scratch
.const reuFsName         = $42DC    // name pointer scratch
.const reuDirBuf        = $13D4    // 256B directory page buffer
.const osName            = $AD3    // kernel string (init.inc)
.const osVersion         = $ADC    // kernel string (init.inc)
//  The sprite shadow base (kernel label sprShReg) -- the block the raster
//  commit publishes to $D000-$D02E every frame. It is the middle field of the
//  per-task state: 24B of sheet records come before it, 8B of frame indices
//  after. Tasks read it for diagnostics only; the API is write-only.
//  Offsets + macros: src/tasks/sprite-shadow.inc. Pinned by src/main.asm.
.const sprShadow         = $CFBC
//  Sheet records, per sprite: the 24-bit REU FS offset of frame 0. `Hi = 0`
//  means unresolved -- bank 0 is never an FS data bank (the FS data area is
//  banks 5-251), which is what lets a zeroed block read as "no sheet".
//  A library (src/libs/sprite.asm) resolves them; the kernel re-DMAs from them.
.const spriteSheetLo      = $CFA4
.const spriteSheetMid     = $CFAC
.const spriteSheetHi      = $CFB4
//  The frame last asked for, per sprite. It is what the kernel re-DMAs into the
//  slot when this task's surface takes the display, so a task comes back to the
//  art it left.
.const sprFrameIdx        = $CFE3
//  Kernel routine: DMA frame X of sprite A's sheet into the slot. The library's
//  kSpriteFrame calls it; the kernel's install calls it once per sprite to
//  restore a returning display owner's art.
.const sprFrameDma        = $1CC0
//  Kernel routine: hand the sprite state to the task whose surface is displayed.
//  The scheduler calls it on every context switch, and the library calls it at
//  the top of every setter -- that is what stops a write from landing in the
//  previous owner's live state. Idempotent and cheap when the display has not
//  changed. Clobbers A, X, Y, kPtr (callers that need kPtr keep a copy).
.const sprInstall         = $52DC
//  Per-frame counter, incremented by the kernel's raster commit at raster line
//  0. A task updates ONCE PER FRAME by waiting for this byte to change instead
//  of once per scheduler tick: the tick is 59.2 Hz and the display 50.1 Hz, so
//  per-tick animation is sampled unevenly. Compare for CHANGE only -- 8-bit, and
//  it wraps.
.const frameCount         = $CFFF
.const taskLo            = $186F
.const taskHi            = $1887
.const taskShadowFlag    = $19EB
.const taskShadowHi      = $1A03
.const taskGfxReuLo      = $421D   // per-task bitmap REU address (lo)
.const taskGfxReuHi      = $4235   // per-task bitmap REU address (hi)
.const taskGfxReuBank    = $424D   // per-task bitmap REU bank
.const taskGfxBorder     = $4265   // per-task border colour ($D020); bg0/bg1/bg2 follow at +24/+48/+72
.const screenKind        = $4205   // per-task bitmap mode (0=none, 2=std, 3=mc)
.const curTask           = $FD     // currently-running task slot (scheduler ZP)
.const dynTaskOriginLo   = $CA99
.const dynTaskOriginHi   = $CAA1
.const dynTaskSizeLo     = $CAA9
.const dynTaskSizeHi     = $CAB1
.const dynTaskId         = $CAB9
//  Per-dyn-slot 16-bit library mask: what each loaded task DECLARED, minus
//  whatever `kLibHold` handed back (so a music task's mask is exactly what it
//  kept -- {sid.lib}, plus sprite.lib when it drives sprites). It is the only
//  task-visible record of "this task uses sid.lib"
//  -- the SID owner itself lives in the library's private data, which no task
//  can address -- and `kill` reads it to clean up a killed player's drone.
//  sid.lib = bit 5 of the LOW byte (lib id 5); index by dyn slot, mapping to a
//  task through dynTaskId above. Addresses read from build/main.sym and pinned
//  in docs/memory-map.md -- a kernel move must update them here too.
.const dynTaskLibMaskLo  = $CD4C
.const dynTaskLibMaskHi  = $CD54
.const dynTaskResidency  = $CAD1
//  How many instances the residency record has. `ps` reads it through the
//  instance's `dynTaskResidency` to tell a genuinely shared instance from a lone
//  one that is merely attached. Address from build/main.sym.
.const resRefCount       = $CB02
.const dynTaskCount      = $CAE1
.const dynTaskStartPage  = $CAC1
.const dynTaskPageCount  = $CAC9
.const dynTaskFlags      = $CAD9
.const resStartPage      = $CAFA
.const resPageCount      = $CAFE
.const poolBitmap        = $CB07
.const mallocOwner       = $CB15
.const mallocStartPage   = $CB1D
.const mallocPages       = $CB25
.const mallocSwapped     = $CB2D
.const mallocCount       = $CB35
.const dynTaskRamStartPage = $CB36
.const dynTaskRamPages   = $CB3E
.const FONT_REC_LO     = $19DB   // REU-only font: FS data-area offset (24-bit)
.const FONT_REC_MID    = $19DC
.const FONT_REC_HI     = $19DD

// --- Dynamic kernel libraries ---
//  Runtime state (mutated by libLoad/libFree) + read-only descriptors
//  (name ptrs, entry counts, eager flags). The shell's `pool` command
//  walks these to list loaded libraries.
.const libStartPage    = $189F    // $FF = not loaded
.const libPages        = $18AF
.const libRefCount     = $18BF
.const libNameLo       = $CD0C
.const libNameHi       = $CD1C
.const libEntryCount   = $CD2C
.const libEager        = $CD3C