// ==========================================
//  fp.inc -- fp.lib shared constants (lib + consumers)
// ==========================================
//  Imported by src/libs/fp.asm AND every task that uses FP.
//  Single source of truth for the ZP window and the opcode set.

// FP state window -- one contiguous 54B block ($90-$C5), sized for the 5-byte
// power/tan scratch: fact_3's contiguity requirement uses the pocket at $9C.
//  fac1/fac2 and friends stay at their EhBASIC addresses ($9C-$BB);
//  the scattered scratch bytes remap into the window's free pockets.
.const FP_ZP_BASE = $90
.const FP_ZP_LEN  = $36
.const FP_ZP_END  = FP_ZP_BASE + FP_ZP_LEN   // exclusive

// opcodes (A register on entry to fpExec)
.const FP_ADD    = $00
.const FP_SUB    = $01
.const FP_MUL    = $02
.const FP_DIV    = $03
.const FP_POW    = $04
.const FP_NEG    = $05
.const FP_ABS    = $06
.const FP_INT    = $07
.const FP_SGN    = $08
.const FP_SQR    = $09
.const FP_EXP    = $0A
.const FP_LOG    = $0B
.const FP_SIN    = $0C
.const FP_COS    = $0D
.const FP_TAN    = $0E
.const FP_ATN    = $0F
.const FP_RND    = $10
.const FP_CMP    = $11
.const FP_F2FX   = $12
.const FP_AYFC   = $13
.const FP_MUL10  = $14
.const FP_DIV10  = $15
.const FP_FLOAD  = $16
.const FP_FSTORE = $17
.const FP_PI     = $18
.const FP_BYTEF  = $19

// error codes (returned with C=1 from fpExec)
.const FP_ERR_DIV0     = 0    // divide by zero
.const FP_ERR_OVERFLOW = 1    // overflow
.const FP_ERR_RANGE    = 2    // illegal argument (log <= 0, sqrt < 0, ...)
.const FP_ERR_CONV     = 3    // conversion range (>= 2^24 for F2FX)
.const FP_ERR_NIMPL    = 4    // out-of-range opcode (fpBadOp) -- all 26 opcodes are implemented

.const FP_OPCOUNT = FP_BYTEF + 1   // 26 opcodes

// Build-time guard: fail if a task's own ZP const lands in the window.
.macro fpZPFree(addr) {
    .assert "fpZPFree: task ZP collides with the fp window", ((addr < FP_ZP_BASE) || (addr >= FP_ZP_END)) ? 1 : 0, 1
}

// Build-time guard: the declared region must contain the window.
.macro fpZpDeclared(base, len) {
    .assert "fpZpDeclared: region must contain the fp window", ((base <= FP_ZP_BASE) && (base + len >= FP_ZP_END)) ? 1 : 0, 1
}

// Hard limits
.assert "fp window exceeds task ZP", (FP_ZP_END <= $F0) ? 1 : 0, 1
