// ==========================================
//  fs-offset.inc -- operations on a 24-bit REU filesystem offset
// ==========================================
//  An FS offset is three bytes (lo, mid, hi). It is NOT a 16-bit value:
//  `hi` is the bank byte, and it is 0 only for files below FS $10000. The
//  layout is user data (`compact` repacks the FS downward, so which files
//  sit above the boundary depends on what the user has deleted), so "the bank
//  byte is always 0" is never a safe assumption.
//
//  A hand-written operation gets this wrong easily, and a KickAssembler
//  .assert cannot catch it. The two failure shapes to avoid are a carry that
//  never reaches `+2` (as in the fixup chunk advance) and a stash that
//  copies lo+hi only, leaving a stale bank byte. Write these operations with
//  the macros below instead:
//
//    FSCopy(src, dst)                  dst(3) = src(3)
//    FSAdd16(dst, src, lo, hi)         dst(3) = src(3) + (hi:lo)    [dst == src ok]
//    FSAddPage(dst)                    dst(3) += $0100 (one pool page)
//    FSToReuAddr(src, addrLo, addrHi, bank)   REU DMA address of an offset
//
//  Every macro propagates the carry into the third byte. `FSToReuAddr`
//  encodes the wrap rule that is easy to get wrong by hand: the FS data
//  base is bank 5 $D000, so an offset O is transferred at linear address
//  O + $5D000 and wraps a bank when (O + $D000) rolls over $10000 -- i.e. at
//  offsets ending $3000, NOT $0000.
//
//  Guards: tools/check-fs-offsets.ps1 fails the build on a hand-written
//  version of these shapes (R1/R2/R3); `fstest` exercises the chunked
//  DMA paths and the offset arithmetic at runtime.
//
//  The instruction order below matches the hand-written sites that were
//  converted, so conversion is byte-identical (verified by comparing the
//  built PRG / task binaries). Sites that keep a hand-written form do so
//  because their bytes are not contiguous: `type` and `banner` store the
//  offset hi byte in a separate instance field, which no triple-taking
//  macro can express. The lint covers those (R3 checks the carry).

// --- dst(3) = src(3) ---
.macro FSCopy(src, dst) {
    lda src
    sta dst
    lda src+1
    sta dst+1
    lda src+2
    sta dst+2
}

// --- dst(3) = src(3) + (hi:lo), 16-bit addend held in memory ---
//  Pass src == dst for an in-place advance.
.macro FSAdd16(dst, src, lo, hi) {
    lda src
    clc
    adc lo
    sta dst
    lda src+1
    adc hi
    sta dst+1
    lda src+2
    adc #0
    sta dst+2
}

// --- dst(3) += $0100 (advance one 256-byte page) ---
.macro FSAddPage(dst) {
    lda dst
    clc
    adc #0
    sta dst
    lda dst+1
    adc #1
    sta dst+1
    lda dst+2
    adc #0
    sta dst+2
}

// --- REU DMA address of an FS offset ---
//  addr = (O + $5D000) & $FFFF, bank = (O + $5D000) >> 16, i.e.
//  addr hi = O_mid + $D0 (carry) and bank = O_hi + 5 + carry.
.macro FSToReuAddr(src, addrLo, addrHi, bank) {
    lda src
    sta addrLo
    lda src+1
    clc
    adc #$D0
    sta addrHi
    lda src+2
    adc #5
    sta bank
}
