// ==========================================
//  cursor-helpers.inc -- load-cursor helpers
// ==========================================
//  Pinned by main.asm at $5457, sequentially after font-help.inc ($5340)
//  and pool-top.inc ($53F2), so all three loader-band helpers share one
//  .pc chain. 43B, ending at $5481 -- the loader band's leftover is then
//  $5482-$54AA (41B), guarded by the cursorHelpEnd assert against
//  scrollScreenSlot's pin at $54AB.
//
//  Macro-only files emit nothing; these are real code, hence the explicit
//  .pc in main.asm.
//
//  WHY THESE EXIST -- `reuFsPtr` is a SHARED register: filesys.lib (~30
//  references) and gfx.lib use it as their working offset too, and anything
//  nested inside a load can clobber it. The lib-header peek in `dlPreEvict`
//  does exactly that -- it can re-find a lib and leave a stale bank byte
//  behind, after which the task's own fixup table is read one bank high.
//
//  So a load keeps its offset in its own variable (`dlFsSave`) and installs
//  it into `reuFsPtr` immediately before each DMA it issues. A nested
//  operation can still clobber `reuFsPtr`, but the clobber cannot survive to
//  the next transfer, and no caller has to save/restore anything.
//
//  The rules for new code:
//    - treat `reuFsPtr` as an argument register: set it, then call the DMA.
//    - never assume it still holds what you set once you call anything that
//      touches the filesystem (a find, a lib call, a font glyph).
//    - hold a long-lived offset in your own variable and use
//      `dlInstallCursor` (or the macro) before each use.

// --- dlFindMaster -- find a file and make it the loader's cursor ---
//  Input: kArg/kArg+1 = name. Output: C=0 found / C=1 not found.
//  reuFsPtr is left as reuFindFile set it (the file start) -- that is what a
//  header-page DMA needs immediately afterwards. Clobbers A, Y, kArg.
dlFindMaster:
    jsr reuFindFile
    bcs dlFmRts
    :FSCopy(reuFsPtr, dlFsSave)
dlFmRts:
    rts

// --- dlInstallCursor -- reuFsPtr = the loader's cursor ---
//  Call immediately before every DMA the loader issues. Clobbers A.
dlInstallCursor:
    :FSCopy(dlFsSave, reuFsPtr)
    rts

.label cursorHelpEnd = *
