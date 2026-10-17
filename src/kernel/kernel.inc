// ==========================================
//  GordonOS -- Kernel constants & zero page
// ==========================================
//
//  TCB layout (257 bytes per task):
//    Byte 0       savedSP (stack pointer when preempted)
//    Bytes 1-256  256-byte stack image ($0100-$01FF)
//
//  Stack frame saved/restored on context switch:
//    $01F8  <[irqRet-1]   RTS return addr (RTS adds 1)
//    $01F9  >[irqRet-1]
//    $01FA  Y=0           popped by PLA:TAY
//    $01FB  X=0           popped by PLA:TAX
//    $01FC  A=0           popped by PLA
//    $01FD  SR=0          popped by RTI (bit 2=0 -> IRQs on)
//    $01FE  <task_addr    popped by RTI -> PCL
//    $01FF  >task_addr    popped by RTI -> PCH
//    savedSP = $F7

// --- REU stack image base (bank 0) ---
.const REU_STACK_BASE = $0000 // 24 x 256B stack images in REU bank 0

// --- Constants shared with the task build (single source of truth) ---
//  MAX_TASKS, MAX_SEMAPHORES, TA_*, REU_BOUNCE, REU_FS_SIGNATURE,
//  MAX_DYN_TASKS, MAX_MALLOC_BLOCKS, MAX_RESIDENTS, MAX_LIBS
//  and the keyboard/blink tick rates -- imported
//  by BOTH this file (kernel build) and the task sources that need one
//  of these (task build), so each value is defined in exactly one place.
.import source "kernel-shared.inc"

// --- Orphan free-list (spawnCore) ---
.const FREE_LIST_ENTRIES = 4  // mismatched-size dead buffers pushed here; data at $4304

// --- OS version (shown in boot banner) ---
.const VERSION = "v1.0bt"

// --- CIA #1 Timer A latch (~16640 cycles ~= 60 Hz) ---
.const TIMER_LO  = $00
.const TIMER_HI  = $41

// --- Keyboard repeat (in ticks, ~60 Hz) ---
//  KEYB_INITIAL/KEYB_REPEAT and BLINK_RATE live in kernel-shared.inc: the
//  `speed` command scales them, so the task build needs them too.

// --- Per-instance data heap ---
.const MAX_INSTANCE_DATA = 255 // max bytes per task instance buffer (0=no buffer)

// --- Task header flags (header byte +3) ---
//  Single source of truth: task-header.inc -- imported by this file (kernel
//  build) and by tools/gen-task-asm.ps1 (task/lib build). Change it there.
.import source "task-header.inc"

// --- Screen geometry -- the 40x25 cell grid over the 320x200 bitmap ---------
//  A text cell is one 8x8 glyph, so a matrix / char-shadow row is exactly
//  SCREEN_COLS bytes and the grid is SCREEN_COLS x SCREEN_ROWS. The two
//  "last index" forms are what the wrap and clamp tests compare against.
.const SCREEN_COLS     = 40                // cells per row (a row is 40 bytes)
.const SCREEN_ROWS     = 25                // rows per screen
.const SCREEN_LAST_COL = SCREEN_COLS - 1   // 39 -- last valid column
.const SCREEN_LAST_ROW = SCREEN_ROWS - 1   // 24 -- last row / default scroll bottom

// --- Default screen colors ---
.const DEFAULT_BORDER_COLOR = $0b  // dark grey border
.const DEFAULT_BG_COLOR      = $00  // black background
.const DEFAULT_TEXT_COLOR    = $0f  // light grey text
.const DEFAULT_CURSOR_COLOR  = $01  // white cursor

// --- REU (RAM Expansion Unit) required ---
// All context switches use REU DMA (~256 cycles).
// Boot halts if REU is not detected.
// Commands: $90 = C64->REU, $91 = REU->C64 (both immediate)
//
// REU commands -- GordonOS always uses immediate mode ($DF01 trigger):
//   Base: REU_EXEC = $90 = %10010000 (bit 7=exec, bit 4=immediate)
//   C64->REU: $90 | $00 = $90    REU->C64: $90 | $01 = $91
.const REU_EXEC           = $90   // execute + immediate (matches scheduler's $9x convention)
.const REU_CMD_C64_TO_REU = $00
.const REU_CMD_REU_TO_C64 = $01
//
// REU address control ($DF0A):
.const REU_CTRL_INC       = $00   // both addresses increment
.const REU_CTRL_FIX_C64   = $80   // fixed C64 addr, REU incs (-> fill REU)
.const REU_CTRL_FIX_REU   = $40   // fixed REU addr, C64 incs (-> fill C64)
//
// REU status bits ($DF00):
.const REU_ST_EOB         = $40   // bit 6 -- end of block
//
// REU bounce buffer -- the scroll's 7680B pixel round-trip, 960B matrix
// round-trip, and kReuFillC64's 1-byte fill. Lives in the dead
// REU_CHAR_SHADOW region ($A000-$BFFF, never read): a 7680B bounce cannot
// live at $1800-$35FF, which is the live char shadow region.
// (REU_BOUNCE itself comes from kernel-shared.inc, imported above.)

// --- Bitmap graphics ---
.const BMPMODE_NONE       = 0
.const BMPMODE_TEXT       = 1
.const BMPMODE_STD        = 2
.const BMPMODE_MC         = 3
.const GFX_SLOT_PIXELS    = $2000  // pixel data bytes per bitmap slot
.const GFX_SLOT_MATRIX    = $2000  // slot offset of the 1 KB video matrix
.const GFX_SLOT_COLRAM    = $2400  // slot offset of the 1 KB MC color RAM
.const GFX_SLOT_TOTAL     = $2800  // total slot size (10 KB)
.const GFX_SLOT_BASE_BANK = $01    // base bank for bitmap slots (banks 1+)
.const GFX_SLOT_COUNT     = 28     // maximum bitmap slots
.const GFX_D018           = $18    // $D018 for bitmap $2000 + matrix $0400 (no overlap)
.const GFX_MATRIX_BASE_LO = $00
.const GFX_MATRIX_BASE_HI = $04
// Matrix tail -- the 24 spare bytes of the 1KB video matrix past the
// visible 40x25 cells (framebuffer $07E8-$07FF; slot +$23E8-$23FF).
// Per-surface state lives here: block-cursor save/restore and the
// scroll region (GFX_SCROLL_TOP/BOTTOM at $03EE/$03EF).
.const GFX_MATRIX_BASE   = $0400  // framebuffer video matrix base
.const GFX_CURSOR_COL    = $03e8  // cursor column
.const GFX_CURSOR_ROW    = $03e9  // cursor row
.const GFX_CURSOR_ON     = $03ea  // cursor enabled (1/0)
.const GFX_CURSOR_NIB    = $03eb  // saved matrix byte under the block
.const GFX_CURSOR_SCOL   = $03ec  // saved column (block-OFF position)
.const GFX_CURSOR_SROW   = $03ed  // saved row (block-OFF position)
.const GFX_SCROLL_TOP    = $03ee  // scroll-region top (matrix-tail offset)
.const GFX_SCROLL_BOTTOM = $03ef  // scroll-region bottom (matrix-tail offset)
.const GFX_CURSOR_GLYPH  = $03f0  // saved glyph bytes (8, block-OFF)
.const BASIC_INBUF_LO     = $80    // relocated to $1A80
.const BASIC_INBUF_HI     = $1A
//
// Per-task ZP save (DMA-based context switch):
.const REU_THREAD_NAME  = $C000  // per-task thread name storage (24 x 16B)

//  Per-task sprite state, one 128-byte block per task slot, so a block is
//  `slot*128` -- the low byte is `slot&1 ? $80 : 0` and the high byte is
//  `>REU_SPR_BASE + slot/2`, which is two instructions and no table.
//  WARNING: KEEP IT PAGE-ALIGNED. `sprBlkAddr` relies on the base's low byte
//  being 0 for the low byte to be `(slot&1) << 7`, exactly like REU_KARG_BASE.
//  24 x 128 = $C00 exactly, so the region is $C400-$CFFF: it sits in bank 0's
//  free run above the thread names ($C000-$C17F) and stays BELOW $D000, so no
//  byte of it is ever confused with the C64's I/O window -- this is REU
//  address space, but keeping it out of that range costs nothing here.
//  (Do NOT confuse this with `SCRATCH_ADDR`, which is a C64 address.)
//  Only the DISPLAY OWNER's state is live in kernel RAM (sprite.inc); every
//  other task's waits here, so the eight slots are shared by number only.
.const REU_SPR_BASE     = $C400  // per-task sprite shadow + sheet records
//  Per-task kArg/kResult save area (24 x 8B). WARNING: KEEP IT PAGE-ALIGNED: the
//  scheduler computes the address as `task x 8`, which is only the LOW byte if
//  the base's low byte is zero (and `MAX_TASKS x 8 <= 256`, asserted). A
//  non-aligned base needs `clc / adc #<base` before the store and a
//  carry-propagating `adc #>base` after it -- 5 extra bytes at each of the two
//  sites, for arithmetic that alignment does for free.
.const REU_KARG_BASE    = $9700  // 192B: $9700-$97BF, inside the free $9740-$9FFF run
.const REU_ZP_BASE       = $8000  // per-task ZP shadow in REU ($8000 + task x 240)
.const MAX_ZP_LEN        = 240    // max bytes of ZP a task can declare ($00-$EF)
.const REU_MALLOC_BANK   = 252    // malloc shadows: banks 252-255 (top 4, 32 KB/slot)

// --- Bitmap view-switch temp buffers (REU bank 0) ---
// 8 x 1 KB char shadows at $A000-$BFFF, indexed by screen slot:
//   slot = (screenBaseHi - $20) >> 2  ->  REU addr = $A000 + slot * $0400
// NOTE: dead data in the all-bitmap model -- nothing reads it -- but the
// boot seeding loop still runs and references this constant. The constant must
// stay while that loop does; the two can be removed together.
.const REU_CHAR_SHADOW    = $A000  // 8 KB: per-screen-slot text char shadows (bank 0)
.const SHADOW_BASE       = $2000  // live char shadows (opt-in, kSetCharShadow):
                                  // 24 x $400 in bank 0 ($2000-$7FFF) --
                                  // MAX_TASKS=24, so surfaces always fit slots 0-23

// --- REU Filesystem ---
.const REU_FS_MIN_START  = $059000    // bank 5, offset $9000
.const REU_FS_BANK       = 5          // REU bank for filesystem start
.const REU_FS_OFFSET     = $9000      // intra-bank offset
.const REU_DIR_ENTRIES   = 512
.const REU_DIR_ENTRY_SIZE = 32
.const REU_DIR_PAGE_SIZE  = 256
.const REU_DIR_TOTAL_SIZE = REU_DIR_ENTRIES * REU_DIR_ENTRY_SIZE  // 16384 ($4000)
// (REU_FS_SIGNATURE comes from kernel-shared.inc, imported above.)
.const REU_FS_BANK5_CAP  = $3000     // free bytes in bank 5: $10000 - REU_FS_OFFSET - REU_DIR_TOTAL_SIZE
.const REU_DIR_BUF        = $13D4     // C64 address of reuDirBuf bounce buffer

// --- Pool geometry (single source of truth) ---
//  POOL_PAGE_SIZE, every derived constant, and the poolAsl/poolLsr
//  macros live in pool.inc -- imported by BOTH this file (kernel build)
//  and tools/assemble-tasks.asm (task build). Change page size there.
.import source "pool.inc"

// --- Dynamic task loader ---
// (MAX_DYN_TASKS, MAX_MALLOC_BLOCKS, MAX_RESIDENTS come from
// kernel-shared.inc, imported above.)
.const MAX_TASK_ARGS   = 64    // max args bytes copied into a KF_TAKES_ARGS instance buffer

// --- Dynamic kernel libraries ---
//  A library is a relocatable .lib binary loaded into the pool on demand,
//  shared + refcounted, freed at refcount 0. The task header carries a
//  16-bit libMask (bits 0..15 = libs 0..15). Each exposed call is a 3-byte
//  kernel jump entry patched in place by libLoad -- there is no call-count
//  ceiling; the per-call libJump/libInit tables (main.asm) grow as needed.
// (MAX_LIBS comes from kernel-shared.inc, imported above.)

// --- Bitmap text ---
// REU-only font: the 2KB font lives in the REU FS; the boot
// task resolves it once and stores the 24-bit FS data-area offset here.
// The blitter DMA's each 8-byte glyph into FONT_GLYPHS on demand.
// These constants live in the 48-byte block main.asm labels `fontRecLo` --
// keep its size stable so downstream addresses don't move.
.const FONT_REC_LO    = $19DB   // current font: FS data-area offset (24-bit)
.const FONT_REC_MID   = $19DC
.const FONT_REC_HI    = $19DD
.const FONT_C2        = $19DE   // blitChar col/row stash (kArg+2/+3 survive
.const FONT_C3        = $19DF   //   the glyph DMA)
.const FONT_C6        = $19E0   // blitCharSlot stash (kArg+6/+7 survive)
.const FONT_C7        = $19E1
.const FONT_GLYPHS    = $19E2   // 8-byte glyph DMA scratch

// Pool geometry, derived constants and the poolAsl/poolLsr page-size
// macros all live in pool.inc (imported at the top of this file).
//     SCRATCH_ADDR_LO/HI are the REU address (bank 0) of the $C400 scratch
//     page, used by the boot char-shadow seed loop.
.const SCRATCH_ADDR_LO    = $00
.const SCRATCH_ADDR_HI    = $C4

// --- Zero page ($F1-$FF) -- IRQ SAFETY RULES ---
//
//  WARNING:  CRITICAL: IRQ handler fires asynchronously at ~60 Hz.
//  Any ZP location used by the IRQ path MUST NOT be used by
//  task-level (non-SEI) kernel code, or the kernel code
//  MUST save/restore it. Violating this causes random crashes
//  during scrolling, printing, or keyboard input.
//
//  IRQ path ZP usage:
//    kbPoll -> cursorAddr -> kPtr  ($F5-F6)  <- SAVED/RESTORED
//    kbPoll -> scanKeyboardHw -> kbTemp ($FF) <- IRQ-ONLY
//    switchTask -> tmpPtr ($FB-FC), curTask ($FD), saveSP ($FE)
//
//  $F1-$F2 (dataPtr) is scheduler-managed -- saved/restored per-task.
//  DO NOT use $F5, $FB-$FE in task-level kernel code.
//  $F9 (srcPtr) is free from IRQ use but clobbered by kernel routines
//    (kScrollScreen, kStrcpy, kNewline). OK for task scratch between calls.
//  $F7 (spawnPtr) is free for task use; spawn runs with SEI.
//
//  Tasks must not rely on ANY ZP values across preemptions.
// ==========================================
.label blinkRate = $f0        // cursor blink interval (ticks) -- kernel-global, runtime-settable (blink.com); never DMA'd (task ZP bound is <= $F0)
.label dataPtr   = $f1         // task-private data pointer (2 bytes) -- scheduler saves/restores per-task
.label taskNamePtr = $f3     // task name pointer for spawn (2 bytes)
.label kPtr    = $f5         // kernel string pointer (2 bytes) WARNING: IRQ saves/restores
.label srcPtr  = $f9         // scroll/general pointer -- scheduler-managed, kernel-clobbered (scroll/strcpy)
.label spawnPtr = $f7         // task address for spawn syscall (2 bytes)
.label tmpPtr   = $fb         // scheduler pointer (2 bytes) NEVER- IRQ -- DO NOT USE in tasks
.label curTask  = $fd         // current task index NEVER- IRQ -- DO NOT USE in tasks
.label saveSP   = $fe         // temporary SP storage NEVER- IRQ -- DO NOT USE in tasks
.label kbTemp   = $ff         // keyboard scan temp NEVER- IRQ-ONLY -- DO NOT USE in tasks
