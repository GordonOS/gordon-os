// ==========================================
//  pool.inc -- pool geometry (single source of truth)
// ==========================================
//  Imported by BOTH the kernel build (via kernel.inc) and the task
//  build (via tools/assemble-tasks.asm), so POOL_PAGE_SIZE is defined
//  in exactly one place. Everything below derives from it.
//
//  Pool: $5700-$C3FF (109 pages, ~27.25 KB), boxed by the loader/scroll
//  scratch page at $C400 above and the scrollScreenSlot band ($5400-$56FF)
//  below. No RAM-resident font: glyphs are DMA'd from the REU FS on demand
//  (fontRecLo/Mid/Hi in kernel RAM), which is what lets the pool run all the
//  way to $C3FF.

// KickAssembler has no integer log2, so ilog2 counts trailing zero bits
// recursively: POOL_PAGE_SHIFT = log2(POOL_PAGE_SIZE/256).
.function ilog2(v) {
    .if (v <= 1) .return 0
    .return ilog2(v >> 1) + 1
}

.const POOL_BASE      = $5700
.const POOL_SIZE      = $6D00          // pool range $5700-$C3FF (109 x 256B pages)
.const POOL_PAGE_SIZE = $0100          // allocation granularity (the knob)
.const POOL_PAGE_SHIFT = ilog2(POOL_PAGE_SIZE >> 8)
.const POOL_PAGE_MASK = POOL_PAGE_SIZE - 1
.const POOL_PAGES     = POOL_SIZE >> (8 + POOL_PAGE_SHIFT)
.const POOL_END       = POOL_BASE + POOL_PAGES * POOL_PAGE_SIZE
.const POOL_LAST      = POOL_END - 1
.const POOL_BITMAP_BYTES = (POOL_PAGES + 7) >> 3

// Per-slot REU malloc shadow (banks 252-255, 32 KB/slot). Half of it is
// the program-RAM cap, the other half the runtime-malloc cap.
.const MALLOC_SHADOW_BYTES = 32 * 1024
.const MAX_MALLOC_PAGES  = (MALLOC_SHADOW_BYTES >> 1) >> (8 + POOL_PAGE_SHIFT)

// Pool-page shift macros -- emit asl/lsr POOL_PAGE_SHIFT times so the
// page-size math in the loader/malloc stays in sync with POOL_PAGE_SIZE.
.macro poolAsl() {
    .for (var i = 0; i < POOL_PAGE_SHIFT; i++) {
        asl
    }
}
.macro poolLsr() {
    .for (var i = 0; i < POOL_PAGE_SHIFT; i++) {
        lsr
    }
}
