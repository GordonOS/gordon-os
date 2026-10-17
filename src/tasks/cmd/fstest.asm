// ==========================================
//  fstest.asm -- "fstest" command task (relocatable)
// ==========================================
//  Runtime self-test for the REU filesystem bank boundary ($10000): it
//  round-trips a 16-byte window that SPANS the boundary through the two
//  chunked DMA paths (kReuReadAt -> reuDmaRange, kReuWriteAt ->
//  fsxWriteRange) and verifies the 24-bit offset arithmetic around them.
//  See docs/kernel.md "24-bit invariant".
//  Owner discovery + screen protocol: same as ps/copy
//  (gettid -> taskParent[own] = shell slot -> kShareScreen -> kLockScreen).

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg are defined in tools/assemble-tasks.asm)

fstestBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word fstestEnd - fstest     // +5:  code size
    .byte 1, 0, 0                // +7:  data size 1 (unused), ZP base, ZP len
    .word (1 << LIB_FILESYS)     // +10: libMask
    .byte fstestNameEnd - fstestName  // +12: name len
fstestName:
    .text "fstest"
fstestNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - fstestBinStart), 0

.import source "fstest.inc"
.label fstestEnd = *
