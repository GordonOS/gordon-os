// ==========================================
//  kernel-shared.inc -- kernel constants shared with the task build
// ==========================================
//  Imported by BOTH the kernel build (via kernel.inc) and the task build
//  (via src/tasks/kernel-data.inc), so each value is defined in exactly
//  one place -- same pattern as pool.inc for pool geometry.

// --- Task slot limit ---
.const MAX_TASKS = 24         // change to resize all arrays

// --- Semaphore pool ---
.const MAX_SEMAPHORES = 4     // counting semaphores for task synchronisation (0=unavailable, 1-255=available slots)

// --- Task states ---
.const TA_DEAD   = 0          // slot free / task terminated
.const TA_ALIVE  = 1          // task running (scheduled)
.const TA_PAUSED = 2          // task suspended (skipped by scheduler)
.const TA_SLEEPING = 3        // task sleeping (timed block, skipped by scheduler)
.const TA_SWAPPED = 4         // task evicted from the pool (code not resident; skipped by scheduler)

// --- REU bounce buffer -- the scroll's 7680B pixel round-trip, 960B matrix
// round-trip, and kReuFillC64's 1-byte fill. Lives in the dead
// REU_CHAR_SHADOW region ($A000-$BFFF, never read). ---
.const REU_BOUNCE = $A000     // scroll/fill bounce (max 7680B: $A000-$BDFF)

// --- REU filesystem format signature (slot 0, byte 1) ---
.const REU_FS_SIGNATURE = $A5

// --- Dynamic task loader ---
.const MAX_DYN_TASKS     = 8
.const MAX_MALLOC_BLOCKS = 8   // kMalloc block-table entries
//  Shared-code residency records -- one per distinct reentrant BINARY
//  resident at once (not per running instance -- that's MAX_DYN_TASKS).
//  pcPool's shared-record dedup packs one bit per record into a
//  single-byte bitmask (POOLRS), so this must stay <= 8 -- asserted in
//  src/tasks/dynamic/shell.inc.
.const MAX_RESIDENTS = 4

// --- Dynamic kernel libraries ---
.const MAX_LIBS = 16

// --- Keyboard repeat and cursor blink (in ticks, ~60 Hz) ---
//  REAL-TIME values: 30 ticks is ~500 ms on a machine running at 100%.
//  `speed.com` scales all three by the machine's speed (a left/right shift
//  by `sysSpeedShift`) before poking the kernel, so a 200% machine gets
//  60/60/12. They are shared with the task build because the command is a
//  task: it derives the pokes from these and never from the live values,
//  which is what makes running `speed` twice a no-op.
.const KEYB_INITIAL = 30       // delay before the first repeat (~500 ms)
.const KEYB_REPEAT  = 6        // ticks between repeats (~100 ms)
.const BLINK_RATE   = 30       // cursor blink interval per toggle (~500 ms)
