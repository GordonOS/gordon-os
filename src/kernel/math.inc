// ==========================================
//  MATH -- 16-bit/8-bit arithmetic and conversions.
//  Lives in the $5E0D-$5FFF gap after the REU routines; the hex helpers
//  (byteToHex/hexDigit/nibbleToHex) are in string.lib.
// ==========================================

// --- byteToDec -- convert A (0-255) to decimal string at (kPtr) ---
//  Writes 1-3 digits; does NOT null-terminate.
//  Returns length in Y.
//  Repeated subtraction with digit counters initialised to -1. 
//  Digits go straight into the output buffer at (kPtr) -- no 
//  scratch variables, multitask-safe.
//  The remainder in A is saved on the stack around each digit
//  emission (txa/ora #$30 clobber A).
byteToDec:
    ldy #0
    ldx #$ff                    // hundreds counter = -1
    sec
bdHundreds:
    inx
    sbc #100
    bcs bdHundreds
    adc #100                    // restore remainder ($00-$99)
    cpx #0
    beq bdTens                  // no hundreds -- no leading zero
    pha                         // save remainder
    txa
    ora #$30
    sta (kPtr),y
    iny
    pla                         // restore remainder
bdTens:
    ldx #$ff                    // tens counter = -1
    sec
bdTensLoop:
    inx
    sbc #10
    bcs bdTensLoop
    adc #10                     // restore remainder ($00-$09)
    cpx #0
    beq bdTensZero
    pha                         // save remainder
    txa
    ora #$30
    sta (kPtr),y
    iny
    pla                         // restore remainder
    jmp bdOnes
bdTensZero:
    cpy #0                      // nothing printed yet?
    beq bdOnes                  // value < 10 -- no tens digit
    pha                         // save remainder
    lda #$30                    // hundreds printed -- tens must show "0"
    sta (kPtr),y
    iny
    pla                         // restore remainder
bdOnes:
    ora #$30
    sta (kPtr),y
    iny
    rts

// --- mul16 -- kArg (16-bit) x kArg+2 (16-bit) -> kResult (32-bit) ---
//  kArg, kArg+1 = multiplicand (lo, hi)
//  kArg+2, kArg+3 = multiplier (lo, hi)
//  kResult..kResult+3 = product (lo..hi)
mul16:
    lda #0
    sta kResult
    sta kResult+1
    sta kResult+2
    sta kResult+3
    lda #16
    tax
    nop                         // ABI pad -- mul16 is frozen at 61 bytes (asserts)
m16Loop:
    lsr kArg+3                  // multiplier >>= 1; carry = LSB
    ror kArg+2
    bcc m16Skip
    clc
    lda kResult+2               // result_hi += multiplicand
    adc kArg
    sta kResult+2
    lda kResult+3
    adc kArg+1
    sta kResult+3               // carry-out = bit-16 overflow
m16Skip:
    ror kResult+3               // result >>= 1 (32-bit); carry -> bit 31
    ror kResult+2
    ror kResult+1
    ror kResult
    dex
    bne m16Loop
    rts

// --- div16 -- kArg (16-bit) / kArg+2 (16-bit) -> kResult (16-bit), kResult+2 (16-bit rem) ---
//  kArg, kArg+1 = dividend (lo, hi)
//  kArg+2, kArg+3 = divisor (lo, hi)
//  kResult, kResult+1 = quotient
//  kResult+2, kResult+3 = remainder
div16:
    lda #0
    sta kResult+2               // remainder lo
    sta kResult+3               // remainder hi
    ldx #16
d16Loop:
    asl kArg                    // shift dividend/quotient left
    rol kArg+1
    rol kResult+2               // shift remainder left
    rol kResult+3
    lda kResult+2
    sec
    sbc kArg+2                  // subtract divisor lo
    tay
    lda kResult+3
    sbc kArg+3                  // subtract divisor hi
    bcc d16Skip
    sta kResult+3               // store remainder hi
    sty kResult+2               // store remainder lo
    inc kArg                    // set quotient bit (in dividend location)
d16Skip:
    dex
    bne d16Loop
    // copy quotient from kArg to kResult
    lda kArg
    sta kResult
    lda kArg+1
    sta kResult+1
    rts

// --- random -- LFSR 8-bit random -> A ---
//  Galois LFSR with primitive polynomial $1D (x^8 + x^4 + x^3 + x^2 + 1).
//  Period: 255 (all non-zero values). Uses randomSeed as state.
random:
    lda randomSeed
    asl
    bcc rndNoEor
    eor #$1d                    // polynomial: taps at bits 4,3,2,0
rndNoEor:
    sta randomSeed
    rts
