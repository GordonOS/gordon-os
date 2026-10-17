// ==========================================
//  fp.asm -- dynamic library "fp.lib" (lib 3, 1 entry)
// ==========================================
//  Floating-point core extracted from EhBASIC.
//  Single entry `fpExec` -- opcode in A (see fp.inc). All mutable state
//  lives in the task ZP window $90-$C0 (FP_ZP_BASE/FP_ZP_LEN); every
//  consumer declares it.
//  All labels prefixed fp* (global KickAssembler namespace).
//  Entry table order = call-index order (base 25 = 3+9+13): fpExec(25).

.encoding "screencode_mixed"

// (kernel aliases + fp.inc constants imported globally by tools/assemble-tasks.asm)

fpBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word fpEnd - fpCode         // +5:  code size
    .byte 0, 0, 0                // +7:  data size, ZP base, ZP len (libs: none)
    .word 0                      // +10: reserved (libs consume no libMask)
    .byte fpNameEnd - fpName     // +12: name len
fpName:
    .text "fp"
fpNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - fpBinStart), 0

fpCode:
    .word fp.fpExec             // entry table -- 1 entry

// ==========================================
//  FP core (Steps 2+3) -- extracted from EhBASIC, namespaced.
// ==========================================
//  Deviation from the plan: instead of prefixing every label with fp_,
//  the whole core lives in a KickAssembler .namespace -- the same
//  global-collision isolation with zero label churn. The ZP byte map
//  also lives here (fp.inc only carries the window + opcodes, because
//  global ZP .consts would collide with ehbasic.asm's in the shared
//  assemble-tasks run).
//
//  Binary-op convention (EhBASIC evaluator semantics):
//    fac2 = LEFT operand, fac1 = RIGHT operand, result -> fac1.
//    FSUB: fac1 = fac2 - fac1      FDIV: fac1 = fac2 / fac1
//    FADD/FMUL commutative.  FCMP: A=$01 fac1>fac2, $FF fac1<fac2, $00 =.
//    FLOAD: 5-byte float at (kPtr) -> fac2 (the only fac2 filler).
//    fac1 fillers: AYFC (X=hi, Y=lo), BYTEF (byte in Y), FPI, results.
//    Constant -> fac1 = FLOAD + AYFC(0) + FADD (copy trick).
//
//  All handlers return through the fpRet trampoline (C=0); error exits
//  pop the trampoline and return C=1 + A = code directly to the caller.
//  Stack footprint: trampoline 4B + ~3 jsr levels ~= 14-18 bytes.
//  MT safety: only ZP writes, no SEI/CLI, no self-modifying code.

.namespace fp {

// --- ZP map (fac1/fac2 & friends stay at their EhBASIC addresses;
//     scattered scratch remapped into the $90-$C5 window pockets) ---
.const itempl  = $90          // f2fx/f2fu staging (was $11)
.const itemph  = $91
.const temp3   = $92          // int()/sqr scratch (was $5B; +$93-$95 sqr)
.const ut1_pl  = $96          // unpack/pack pointer (was $71)
.const ut1_ph  = $97
.const ut2_pl  = $98          // fcmp pointer (was $73)
.const ut2_ph  = $99
.const fact_1  = $9a          // multiply/divide temp mantissa (was $75)
.const fact_2  = $9b
.const fact_3  = fact_2+1     // contiguous with fact_1/2 -- lab_2569/lab_26f4
                              // index fact_1-1+n
.const adatal  = $9d          // series scratch (was $A4; hi must be $00)
.const adatah  = $9e
.const tempb   = $9f          // (was $78)
.const cflag   = $a0          // sin/cos/tan quadrant flag (was $63)
.const fac2_r  = $a3          // EhBASIC address
.const rbyte4  = $a4          // rnd LFSR state -- ASCENDING run of 4 (was
.const rbyte1  = $a5          //   $D8-$DB): lab_rnd packs fac1 at rbyte4
.const rbyte2  = $a6          //   (exp at rbyte4, m1..m3 at rbyte1..3) and
.const rbyte3  = $a7          //   reads back rbyte1..3 -- must be contiguous
.const numexp  = $a8          // series partial exponent (EhBASIC address)
.const fac1_e  = $ac          // EhBASIC addresses:
.const fac1_1  = fac1_e+1
.const fac1_2  = fac1_e+2
.const fac1_3  = fac1_e+3
.const fac1_s  = fac1_e+4
.const fac1_o  = fac1_e+6
.const numcon  = $b1          // series constant count (EhBASIC address)
.const fpDispA = $bc          // fpExec dispatch stash: A on entry
.const fpDispX = $bd          // fpExec dispatch stash: X on entry (AYFC hi byte)
.const cptrl   = $ba          // series pointer (EhBASIC address)
.const cptrh   = $bb
.const fac2_e  = $b3
.const fac2_1  = fac2_e+1
.const fac2_2  = fac2_e+2
.const fac2_3  = fac2_e+3
.const fac2_s  = fac2_e+4
.const fac_sc  = $b8
.const fac1_r  = $b9
.const fpPwTmp = $c1          // 5-byte power/tan scratch ($C1-$C5; was
                              // func_l $9C/$9D -- fact_3 contiguity fix took
                              // them, so the window grew to $90-$C5)
.const plus_0  = $00          // x or y plus 0
.const plus_1  = $01          // x or y plus 1
.const plus_2  = $02          // x or y plus 2
.const plus_3  = $03          // x or y plus 3

// build-time sanity: every lib ZP byte inside the declared window
.assert "fp lib ZP start in window", (itempl >= FP_ZP_BASE) ? 1 : 0, 1
.assert "fp lib ZP end in window", (fpPwTmp + 4 < FP_ZP_END) ? 1 : 0, 1
.assert "fp lib scratch no overlap", (fact_3 < fac2_r) ? 1 : 0, 1

// --- constants ---
fpOne:
    .byte $81, $00, $00, $00  // 1.0
fpHalf:
    .byte $80, $00, $00, $00  // 0.5
fpTen:
    .byte $84, $20, $00, $00  // 10.0

// ==========================================
//  fpExec -- FP API dispatcher (entry 0)
// ==========================================
//  Input:  A = opcode (FP_*); Y survives the dispatch (BYTEF arg
//          carrier); A/X are stashed (fpDispA/fpDispX) for AYFC.
//  Output: C=0 success / C=1 + A = error code; X clobbered.
fpExec:
    sta fpDispA
    stx fpDispX
    cmp #FP_OPCOUNT
    bcs fpBadOp
    asl
    tax
    lda #>fpRet
    pha
    lda #<fpRet-1
    pha
    lda fpOpTab+1,x
    pha
    lda fpOpTab,x
    pha
    rts                         // -> handler; handler's rts -> fpRet
fpRet:
    clc
    rts
fpBadOp:
    lda #FP_ERR_NIMPL
    sec
    rts

fpOpTab:
    .word fpAdd-1               // $00 FP_ADD
    .word fpSub-1               // $01 FP_SUB
    .word fpMul-1               // $02 FP_MUL
    .word fpDiv-1               // $03 FP_DIV
    .word fpPow-1               // $04 FP_POW
    .word fpNeg-1               // $05 FP_NEG
    .word fpAbs-1               // $06 FP_ABS
    .word fpInt-1               // $07 FP_INT
    .word fpSgn-1               // $08 FP_SGN
    .word fpSqr-1               // $09 FP_SQR
    .word fpExp-1               // $0A FP_EXP
    .word fpLog-1               // $0B FP_LOG
    .word fpSin-1               // $0C FP_SIN
    .word fpCos-1               // $0D FP_COS
    .word fpTan-1               // $0E FP_TAN
    .word fpAtn-1               // $0F FP_ATN
    .word fpRnd-1               // $10 FP_RND
    .word fpCmp-1               // $11 FP_CMP
    .word fpF2fx-1              // $12 FP_F2FX
    .word fpAyfc-1              // $13 FP_AYFC
    .word fpMul10-1             // $14 FP_MUL10
    .word fpDiv10-1             // $15 FP_DIV10
    .word fpLoad-1              // $16 FP_FLOAD
    .word fpStore-1             // $17 FP_FSTORE
    .word fpPi-1                // $18 FP_PI
    .word fpBytef-1             // $19 FP_BYTEF

// --- opcode handlers (all return via fpRet) ---
//  NOTE: lab_add/lab_multiply/lab_divide take A = fac1_e on entry
//  (their zero-check branches) -- the dispatcher leaves A = opcode*2.
fpAdd:      lda fac1_e
            jmp lab_add
fpSub:      jmp lab_subtract
fpMul:      lda fac1_e
            jmp lab_multiply
fpDiv:      lda fac1_e
            jmp lab_divide
fpNeg:      jmp lab_gthan
fpAbs:      jmp lab_abs
fpInt:      jmp lab_int
fpSgn:      jmp lab_sgn
fpCmp:      lda fac2_s
            ora #$7f
            and fac2_1
            sta fac2_1          // merge sign into fac2_1 (external format --
                                // as EhBASIC LAB_LTHAN does before LAB_27F8;
                                // lab_27f8's ora #$80 then restores the
                                // 0.5 bit for the magnitude compare)
            lda #<fac2_e
            ldy #>fac2_e
            jmp lab_27f8
fpF2fx:     jmp lab_f2fx
fpAyfc:     lda fpDispX            // A = high byte (X stashed at dispatch; Y = low)
            jmp lab_ayfc
fpMul10:    jmp lab_269e
fpDiv10:    jmp lab_26b9
fpLoad:     lda kPtr
            ldy kPtr+1
            jmp lab_264d
fpStore:    ldx kPtr
            ldy kPtr+1
            jmp lab_2778
fpBytef:    lda #$00               // A = high = 0; Y carries the byte arg
            jmp lab_ayfc
fpPow:      lda fac1_e             // LAB_POWER computes fac2^fac1 and its
            jmp lab_power          // leading beq tests fac1_e==0 (the
                                   // exponent). The lib's documented binary
                                   // convention is fac2 = LEFT operand,
                                   // fac1 = RIGHT operand, result -> fac1,
                                   // i.e. FP_POW = fac2^fac1 = left^right --
                                   // exactly LAB_POWER's semantics, so no
                                   // conversion is needed. WARNING: Do NOT swap fac1
                                   // and fac2 here: a lib-native convention
                                   // would double-convert the EhBASIC-evaluator
                                   // operands the BASIC wrapper passes in.
fpSqr:      jmp lab_sqr
fpExp:      jmp lab_exp
fpLog:      jmp lab_log
fpSin:      jmp lab_sin
fpCos:      jmp lab_cos
fpTan:      jmp lab_tan
fpAtn:      jmp lab_atn
fpRnd:      jmp lab_rnd
fpPi:       lda #<fpTwoPi
            ldy #>fpTwoPi
            jsr lab_ufac           // fac1 = 2*pi
            dec fac1_e             // fac1 = pi
            rts                    // -> fpRet

// --- error exits (pop the fpRet trampoline, return C=1 + code) ---

// perform subtraction, fac1 from (ay)

lab_244d:
      rts

lab_2455:
      jsr   lab_264d          // unpack memory (ay) into fac2

// perform subtraction, fac1 from fac2

lab_subtract:
      lda   fac1_s            // get fac1 sign (b7)
      eor   #$ff              // complement it
      sta   fac1_s            // save fac1 sign (b7)
      eor   fac2_s            // eor with fac2 sign (b7)
      sta   fac_sc            // save sign compare (fac1 eor fac2)
      lda   fac1_e            // get fac1 exponent
      jmp   lab_add           // go add fac2 to fac1

// perform addition

lab_2467:
      jsr   lab_257b          // shift facx a times right (>8 shifts)
      bcc   lab_24a8          //.go subtract mantissas

// add (ay) to fac1

lab_246c:
      jsr   lab_264d          // unpack memory (ay) into fac2

// add fac2 to fac1

lab_add:
      bne   lab_2474          // branch if fac1 was not zero

// copy fac2 to fac1

lab_279b:
      lda   fac2_s            // get fac2 sign (b7)

// save fac1 sign and copy abs(fac2) to fac1

lab_279d:
      sta   fac1_s            // save fac1 sign (b7)
      ldx   #$04              // 4 bytes to copy
lab_27a1:
      lda   fac1_o,x          // get byte from fac2,x
      sta   fac1_e-1,x        // save byte at fac1,x
      dex                     // decrement count
      bne   lab_27a1          // loop if not all done

      stx   fac1_r            // clear fac1 rounding byte
      rts

                              // fac1 is non zero
lab_2474:
      ldx   fac1_r            // get fac1 rounding byte
      stx   fac2_r            // save as fac2 rounding byte
      ldx   #fac2_e           // set index to fac2 exponent addr
      lda   fac2_e            // get fac2 exponent
lab_247c:
      tay                     // copy exponent
      beq   lab_244d          // exit if zero

      sec                     // set carry for subtract
      sbc   fac1_e            // subtract fac1 exponent
      beq   lab_24a8          // branch if = (go add mantissa)

      bcc   lab_2498          // branch if <

                              // fac2>fac1
      sty   fac1_e            // save fac1 exponent
      ldy   fac2_s            // get fac2 sign (b7)
      sty   fac1_s            // save fac1 sign (b7)
      eor   #$ff              // complement a
      adc   #$00              // +1 (twos complement, carry is set)
      ldy   #$00              // clear y
      sty   fac2_r            // clear fac2 rounding byte
      ldx   #fac1_e           // set index to fac1 exponent addr
      bne   lab_249c          // branch always

lab_2498:
      ldy   #$00              // clear y
      sty   fac1_r            // clear fac1 rounding byte
lab_249c:
      cmp   #$f9              // compare exponent diff with $f9
      bmi   lab_2467          // branch if range $79-$f8

      tay                     // copy exponent difference to y
      lda   fac1_r            // get fac1 rounding byte
      lsr   plus_1,x          // shift fac? mantissa1
      jsr   lab_2592          // shift facx y times right

                              // exponents are equal now do mantissa subtract
lab_24a8:
      bit   fac_sc            // test sign compare (fac1 eor fac2)
      bpl   lab_24f8          // if = add fac2 mantissa to fac1 mantissa and return

      ldy   #fac1_e           // set index to fac1 exponent addr
      cpx   #fac2_e           // compare x to fac2 exponent addr
      beq   lab_24b4          // branch if =

      ldy   #fac2_e           // else set index to fac2 exponent addr

                              // subtract smaller from bigger (take sign of bigger)
lab_24b4:
      sec                     // set carry for subtract
      eor   #$ff              // ones complement a
      adc   fac2_r            // add fac2 rounding byte
      sta   fac1_r            // save fac1 rounding byte
      lda   plus_3,y          // get facy mantissa3
      sbc   plus_3,x          // subtract facx mantissa3
      sta   fac1_3            // save fac1 mantissa3
      lda   plus_2,y          // get facy mantissa2
      sbc   plus_2,x          // subtract facx mantissa2
      sta   fac1_2            // save fac1 mantissa2
      lda   plus_1,y          // get facy mantissa1
      sbc   plus_1,x          // subtract facx mantissa1
      sta   fac1_1            // save fac1 mantissa1

// do abs and normalise fac1

lab_24d0:
      bcs   lab_24d5          // branch if number is +ve

      jsr   lab_2537          // negate fac1

// normalise fac1

lab_24d5:
      ldy   #$00              // clear y
      tya                     // clear a
      clc                     // clear carry for add
lab_24d9:
      ldx   fac1_1            // get fac1 mantissa1
      bne   lab_251b          // if not zero normalise fac1

      ldx   fac1_2            // get fac1 mantissa2
      stx   fac1_1            // save fac1 mantissa1
      ldx   fac1_3            // get fac1 mantissa3
      stx   fac1_2            // save fac1 mantissa2
      ldx   fac1_r            // get fac1 rounding byte
      stx   fac1_3            // save fac1 mantissa3
      sty   fac1_r            // clear fac1 rounding byte
      adc   #$08              // add x to exponent offset
      cmp   #$18              // compare with $18 (max offset, all bits would be =0)
      bne   lab_24d9          // loop if not max

// clear fac1 exponent and sign

lab_24f1:
      lda   #$00              // clear a
lab_24f3:
      sta   fac1_e            // set fac1 exponent

// save fac1 sign

lab_24f5:
      sta   fac1_s            // save fac1 sign (b7)
      rts

// add fac2 mantissa to fac1 mantissa

lab_24f8:
      adc   fac2_r            // add fac2 rounding byte
      sta   fac1_r            // save fac1 rounding byte
      lda   fac1_3            // get fac1 mantissa3
      adc   fac2_3            // add fac2 mantissa3
      sta   fac1_3            // save fac1 mantissa3
      lda   fac1_2            // get fac1 mantissa2
      adc   fac2_2            // add fac2 mantissa2
      sta   fac1_2            // save fac1 mantissa2
      lda   fac1_1            // get fac1 mantissa1
      adc   fac2_1            // add fac2 mantissa1
      sta   fac1_1            // save fac1 mantissa1
      bcs   lab_252a          // if carry then normalise fac1 for c=1

      rts                     // else just exit

lab_2511:
      adc   #$01              // add 1 to exponent offset
      asl   fac1_r            // shift fac1 rounding byte
      rol   fac1_3            // shift fac1 mantissa3
      rol   fac1_2            // shift fac1 mantissa2
      rol   fac1_1            // shift fac1 mantissa1

// normalise fac1

lab_251b:
      bpl   lab_2511          // loop if not normalised

      sec                     // set carry for subtract
      sbc   fac1_e            // subtract fac1 exponent
      bcs   lab_24f1          // branch if underflow (set result = $0)

      eor   #$ff              // complement exponent
      adc   #$01              // +1 (twos complement)
      sta   fac1_e            // save fac1 exponent

// test and normalise fac1 for c=0/1

lab_2528:
      bcc   lab_2536          // exit if no overflow

// normalise fac1 for c=1

lab_252a:
      inc   fac1_e            // increment fac1 exponent
      beq   fpOverflow        // if zero do overflow error

      ror   fac1_1            // shift fac1 mantissa1
      ror   fac1_2            // shift fac1 mantissa2
      ror   fac1_3            // shift fac1 mantissa3
      ror   fac1_r            // shift fac1 rounding byte
lab_2536:
      rts

// negate fac1

lab_2537:
      lda   fac1_s            // get fac1 sign (b7)
      eor   #$ff              // complement it
      sta   fac1_s            // save fac1 sign (b7)

// twos complement fac1 mantissa

lab_253d:
      lda   fac1_1            // get fac1 mantissa1
      eor   #$ff              // complement it
      sta   fac1_1            // save fac1 mantissa1
      lda   fac1_2            // get fac1 mantissa2
      eor   #$ff              // complement it
      sta   fac1_2            // save fac1 mantissa2
      lda   fac1_3            // get fac1 mantissa3
      eor   #$ff              // complement it
      sta   fac1_3            // save fac1 mantissa3
      lda   fac1_r            // get fac1 rounding byte
      eor   #$ff              // complement it
      sta   fac1_r            // save fac1 rounding byte
      inc   fac1_r            // increment fac1 rounding byte
      bne   lab_2563          // exit if no overflow

// increment fac1 mantissa

lab_2559:
      inc   fac1_3            // increment fac1 mantissa3
      bne   lab_2563          // finished if no rollover

      inc   fac1_2            // increment fac1 mantissa2
      bne   lab_2563          // finished if no rollover

      inc   fac1_1            // increment fac1 mantissa1
lab_2563:
      rts

// do overflow error

fpOverflow:
      pla
      pla
      lda #FP_ERR_OVERFLOW
      sec
      rts

// shift fcatemp << a+8 times

lab_2569:
      ldx   #fact_1-1         // set offset to factemp
lab_256b:
      ldy   plus_3,x          // get facx mantissa3
      sty   fac1_r            // save as fac1 rounding byte
      ldy   plus_2,x          // get facx mantissa2
      sty   plus_3,x          // save facx mantissa3
      ldy   plus_1,x          // get facx mantissa1
      sty   plus_2,x          // save facx mantissa2
      ldy   fac1_o            // get fac1 overflow byte
      sty   plus_1,x          // save facx mantissa1

// shift facx -a times right (> 8 shifts)

lab_257b:
      adc   #$08              // add 8 to shift count
      bmi   lab_256b          // go do 8 shift if still -ve

      beq   lab_256b          // go do 8 shift if zero

      sbc   #$08              // else subtract 8 again
      tay                     // save count to y
      lda   fac1_r            // get fac1 rounding byte
      bcs   lab_259a          //.

lab_2588:
      asl   plus_1,x          // shift facx mantissa1
      bcc   lab_258e          // branch if +ve

      inc   plus_1,x          // this sets b7 eventually
lab_258e:
      ror   plus_1,x          // shift facx mantissa1 (correct for asl)
      ror   plus_1,x          // shift facx mantissa1 (put carry in b7)

// shift facx y times right

lab_2592:
      ror   plus_2,x          // shift facx mantissa2
      ror   plus_3,x          // shift facx mantissa3
      ror                     // shift facx rounding byte
      iny                     // increment exponent diff
      bne   lab_2588          // branch if range adjust not complete

lab_259a:
      clc                     // just clear it
      rts

// do convert ay, fca1*(ay)

lab_25fb:
      jsr   lab_264d          // unpack memory (ay) into fac2
lab_multiply:
      beq   lab_264c          // exit if zero

      jsr   lab_2673          // test and adjust accumulators
      lda   #$00              // clear a
      sta   fact_1            // clear temp mantissa1
      sta   fact_2            // clear temp mantissa2
      sta   fact_3            // clear temp mantissa3
      lda   fac1_r            // get fac1 rounding byte
      jsr   lab_2622          // go do shift/add fac2
      lda   fac1_3            // get fac1 mantissa3
      jsr   lab_2622          // go do shift/add fac2
      lda   fac1_2            // get fac1 mantissa2
      jsr   lab_2622          // go do shift/add fac2
      lda   fac1_1            // get fac1 mantissa1
      jsr   lab_2627          // go do shift/add fac2
      jmp   lab_273c          // copy temp to fac1, normalise and return

lab_2622:
      bne   lab_2627          // branch if byte <> zero

// patch 2.22p5.6 (floating point multiply rounding bug) intact:
      sec
      jmp   lab_2569          // shift factemp << a+8 times

                              // else do shift and add
lab_2627:
      sec                     // set top bit (mark for 8 times)
      ror
lab_262a:
      tay                     // copy result
      bcc   lab_2640          // skip next if bit was zero

      clc                     // clear carry for add
      lda   fact_3            // get temp mantissa3
      adc   fac2_3            // add fac2 mantissa3
      sta   fact_3            // save temp mantissa3
      lda   fact_2            // get temp mantissa2
      adc   fac2_2            // add fac2 mantissa2
      sta   fact_2            // save temp mantissa2
      lda   fact_1            // get temp mantissa1
      adc   fac2_1            // add fac2 mantissa1
      sta   fact_1            // save temp mantissa1
lab_2640:
      ror   fact_1            // shift temp mantissa1
      ror   fact_2            // shift temp mantissa2
      ror   fact_3            // shift temp mantissa3
      ror   fac1_r            // shift temp rounding byte
      tya                     // get byte back
      lsr                     // shift byte
      bne   lab_262a          // loop if all bits not done

lab_264c:
      rts

// unpack memory (ay) into fac2

lab_264d:
      sta   ut1_pl            // save pointer low byte
      sty   ut1_ph            // save pointer high byte
      ldy   #$03              // 4 bytes to get (0-3)
      lda   (ut1_pl),y        // get mantissa3
      sta   fac2_3            // save fac2 mantissa3
      dey                     // decrement index
      lda   (ut1_pl),y        // get mantissa2
      sta   fac2_2            // save fac2 mantissa2
      dey                     // decrement index
      lda   (ut1_pl),y        // get mantissa1+sign
      sta   fac2_s            // save fac2 sign (b7)
      eor   fac1_s            // eor with fac1 sign (b7)
      sta   fac_sc            // save sign compare (fac1 eor fac2)
      lda   fac2_s            // recover fac2 sign (b7)
      ora   #$80              // set 1xxx xxx (set normal bit)
      sta   fac2_1            // save fac2 mantissa1
      dey                     // decrement index
      lda   (ut1_pl),y        // get exponent byte
      sta   fac2_e            // save fac2 exponent
      lda   fac1_e            // get fac1 exponent
      rts

// test and adjust accumulators

lab_2673:
      lda   fac2_e            // get fac2 exponent
lab_2675:
      beq   lab_2696          // branch if fac2 = $00 (handle underflow)

      clc                     // clear carry for add
      adc   fac1_e            // add fac1 exponent
      bcc   lab_2680          // branch if sum of exponents <$0100

      bmi   lab_269b          // do overflow error

      clc                     // clear carry for the add
      .byte $2c               // makes next line bit $1410
lab_2680:
      bpl   lab_2696          // if +ve go handle underflow

      adc   #$80              // adjust exponent
      sta   fac1_e            // save fac1 exponent
      bne   lab_268b          // branch if not zero

      jmp   lab_24f5          // save fac1 sign and return

lab_268b:
      lda   fac_sc            // get sign compare (fac1 eor fac2)
      sta   fac1_s            // save fac1 sign (b7)
lab_268f:
      rts

// handle overflow and underflow

lab_2690:
      lda   fac1_s            // get fac1 sign (b7)
      bpl   lab_269b          // do overflow error

                              // handle underflow
lab_2696:
      pla                     // pop return address low byte
      pla                     // pop return address high byte
      jmp   lab_24f1          // clear fac1 exponent and sign and return

// multiply by 10

lab_269e:
      jsr   lab_27ab          // round and copy fac1 to fac2
      tax                     // copy exponent (set the flags)
      beq   lab_268f          // exit if zero

      clc                     // clear carry for add
      adc   #$02              // add two to exponent (*4)
      bcs   lab_269b          // do overflow error if > $ff

      ldx   #$00              // clear byte
      stx   fac_sc            // clear sign compare (fac1 eor fac2)
      jsr   lab_247c          // add fac2 to fac1 (*5)
      inc   fac1_e            // increment fac1 exponent (*10)
      bne   lab_268f          // if non zero just do rts

lab_269b:
      jmp   fpOverflow        // do overflow error

// divide by 10

lab_26b9:
      jsr   lab_27ab          // round and copy fac1 to fac2
      lda   #<fpTen           // set pointer to 10d low addr
      ldy   #>fpTen           // set pointer to 10d high addr
      ldx   #$00              // clear sign

// divide by (ay) (x=sign)

lab_26c2:
      stx   fac_sc            // save sign compare (fac1 eor fac2)
      jsr   lab_ufac          // unpack memory (ay) into fac1
      jmp   lab_divide        // do fac2/fac1

// perform divide-into

lab_divide:
      beq   fpDivZero         // if zero go do /0 error

      jsr   lab_27ba          // round fac1
      lda   #$00              // clear a
      sec                     // set carry for subtract
      sbc   fac1_e            // subtract fac1 exponent (2s complement)
      sta   fac1_e            // save fac1 exponent
      jsr   lab_2673          // test and adjust accumulators
      inc   fac1_e            // increment fac1 exponent
      beq   lab_269b          // if zero do overflow error

      ldx   #$ff              // set index for pre increment
      lda   #$01              // set bit to flag byte save
lab_26e4:
      ldy   fac2_1            // get fac2 mantissa1
      cpy   fac1_1            // compare fac1 mantissa1
      bne   lab_26f4          // branch if <>

      ldy   fac2_2            // get fac2 mantissa2
      cpy   fac1_2            // compare fac1 mantissa2
      bne   lab_26f4          // branch if <>

      ldy   fac2_3            // get fac2 mantissa3
      cpy   fac1_3            // compare fac1 mantissa3
lab_26f4:
      php                     // save fac2-fac1 compare status
      rol                     // shift the result byte
      bcc   lab_2702          // if no carry skip the byte save

      ldy   #$01              // set bit to flag byte save
      inx                     // else increment the index to fact
      cpx   #$02              // compare with the index to fact_3
      bmi   lab_2701          // if not last byte just go save it

      bne   lab_272b          // if all done go save fac1 rounding byte, normalise and
                              // return

      ldy   #$40              // set bit to flag byte save for the rounding byte
lab_2701:
      sta   fact_1,x          // write result byte to fact_1 + index
      tya                     // copy the next save byte flag
lab_2702:
      plp                     // restore fac2-fac1 compare status
      bcc   lab_2704          // if fac2 < fac1 then skip the subtract

      tay                     // save fac2-fac1 compare status
      lda   fac2_3            // get fac2 mantissa3
      sbc   fac1_3            // subtract fac1 mantissa3
      sta   fac2_3            // save fac2 mantissa3
      lda   fac2_2            // get fac2 mantissa2
      sbc   fac1_2            // subtract fac1 mantissa2
      sta   fac2_2            // save fac2 mantissa2
      lda   fac2_1            // get fac2 mantissa1
      sbc   fac1_1            // subtract fac1 mantissa1
      sta   fac2_1            // save fac2 mantissa1
      tya                     // restore fac2-fac1 compare status

                              // fac2 = fac2*2
lab_2704:
      asl   fac2_3            // shift fac2 mantissa3
      rol   fac2_2            // shift fac2 mantissa2
      rol   fac2_1            // shift fac2 mantissa1
      bcs   lab_26f4          // loop with no compare

      bmi   lab_26e4          // loop with compare

      bpl   lab_26f4          // loop always with no compare

// do a<<6, save as fac1 rounding byte, normalise and return

lab_272b:
      lsr                     // shift b1 - b0 ..
      ror                     // ..
      ror                     // .. to b7 - b6
      sta   fac1_r            // save fac1 rounding byte
      plp                     // dump fac2-fac1 compare status
      jmp   lab_273c          // copy temp to fac1, normalise and return

// do "divide by zero" error

fpDivZero:
      pla
      pla
      lda #FP_ERR_DIV0
      sec
      rts

// copy temp to fac1 and normalise

lab_273c:
      lda   fact_1            // get temp mantissa1
      sta   fac1_1            // save fac1 mantissa1
      lda   fact_2            // get temp mantissa2
      sta   fac1_2            // save fac1 mantissa2
      lda   fact_3            // get temp mantissa3
      sta   fac1_3            // save fac1 mantissa3
      jmp   lab_24d5          // normalise fac1 and return

// unpack memory (ay) into fac1

lab_ufac:
      sta   ut1_pl            // save pointer low byte
      sty   ut1_ph            // save pointer high byte
      ldy   #$03              // 4 bytes to do
      lda   (ut1_pl),y        // get last byte
      sta   fac1_3            // save fac1 mantissa3
      dey                     // decrement index
      lda   (ut1_pl),y        // get last-1 byte
      sta   fac1_2            // save fac1 mantissa2
      dey                     // decrement index
      lda   (ut1_pl),y        // get second byte
      sta   fac1_s            // save fac1 sign (b7)
      ora   #$80              // set 1xxx xxxx (add normal bit)
      sta   fac1_1            // save fac1 mantissa1
      dey                     // decrement index
      lda   (ut1_pl),y        // get first byte (exponent)
      sta   fac1_e            // save fac1 exponent
      sty   fac1_r            // clear fac1 rounding byte
      rts

// pack fac1 into (xy)

lab_2778:
      jsr   lab_27ba          // round fac1
      stx   ut1_pl            // save pointer low byte
      sty   ut1_ph            // save pointer high byte
      ldy   #$03              // set index
      lda   fac1_3            // get fac1 mantissa3
      sta   (ut1_pl),y        // store in destination
      dey                     // decrement index
      lda   fac1_2            // get fac1 mantissa2
      sta   (ut1_pl),y        // store in destination
      dey                     // decrement index
      lda   fac1_s            // get fac1 sign (b7)
      ora   #$7f              // set bits x111 1111
      and   fac1_1            // and in fac1 mantissa1
      sta   (ut1_pl),y        // store in destination
      dey                     // decrement index
      lda   fac1_e            // get fac1 exponent
      sta   (ut1_pl),y        // store in destination
      sty   fac1_r            // clear fac1 rounding byte
      rts

// round and copy fac1 to fac2

lab_27ab:
      jsr   lab_27ba          // round fac1

// copy fac1 to fac2

lab_27ae:
      ldx   #$05              // 5 bytes to copy
lab_27b0:
      lda   fac1_e-1,x        // get byte from fac1,x
      sta   fac1_o,x          // save byte at fac2,x
      dex                     // decrement count
      bne   lab_27b0          // loop if not all done

      stx   fac1_r            // clear fac1 rounding byte
lab_27b9:
      rts

// round fac1

lab_27ba:
      lda   fac1_e            // get fac1 exponent
      beq   lab_27b9          // exit if zero

      asl   fac1_r            // shift fac1 rounding byte
      bcc   lab_27b9          // exit if no overflow

// round fac1 (no check)

lab_27c2:
      jsr   lab_2559          // increment fac1 mantissa
      bne   lab_27b9          // branch if no overflow

      jmp   lab_252a          // normalise fac1 for c=1 and return

// get fac1 sign
// return a=ff,c=1/-ve a=01,c=0/+ve

lab_27ca:
      lda   fac1_e            // get fac1 exponent
      beq   lab_27d7          // exit if zero (already correct sgn(0)=0)

// return a=ff,c=1/-ve a=01,c=0/+ve
// no = 0 check

lab_27ce:
      lda   fac1_s            // else get fac1 sign (b7)

// return a=ff,c=1/-ve a=01,c=0/+ve
// no = 0 check, sign in a

lab_27d0:
      rol                     // move sign bit to carry
      lda   #$ff              // set byte for -ve result
      bcs   lab_27d7          // return if sign was set (-ve)

      lda   #$01              // else set byte for +ve result
lab_27d7:
      rts

// perform sgn()

lab_sgn:
      jsr   lab_27ca          // get fac1 sign
                              // return a=$ff/-ve a=$01/+ve
// save a as integer byte

lab_27db:
      sta   fac1_1            // save fac1 mantissa1
      lda   #$00              // clear a
      sta   fac1_2            // clear fac1 mantissa2
      ldx   #$88              // set exponent

// set exp=x, clearfac1 mantissa3 and normalise

lab_27e3:
      lda   fac1_1            // get fac1 mantissa1
      eor   #$ff              // complement it
      rol                     // sign bit into carry

// set exp=x, clearfac1 mantissa3 and normalise

lab_stfa:
      lda   #$00              // clear a
      sta   fac1_3            // clear fac1 mantissa3
      stx   fac1_e            // set fac1 exponent
      sta   fac1_r            // clear fac1 rounding byte
      sta   fac1_s            // clear fac1 sign (b7)
      jmp   lab_24d0          // do abs and normalise fac1

// perform abs()

lab_abs:
      lsr   fac1_s            // clear fac1 sign (put zero in b7)
      rts

// compare fac1 with (ay)
// returns a=$00 if fac1 = (ay)
// returns a=$01 if fac1 > (ay)
// returns a=$ff if fac1 < (ay)

lab_27f8:
      sta   ut2_pl            // save pointer low byte
lab_27fa:
      sty   ut2_ph            // save pointer high byte
      ldy   #$00              // clear index
      lda   (ut2_pl),y        // get exponent
      iny                     // increment index
      tax                     // copy (ay) exponent to x
      beq   lab_27ca          // branch if (ay) exponent=0 and get fac1 sign
                              // a=ff,c=1/-ve a=01,c=0/+ve

      lda   (ut2_pl),y        // get (ay) mantissa1 (with sign)
      eor   fac1_s            // eor fac1 sign (b7)
      bmi   lab_27ce          // if signs <> do return a=ff,c=1/-ve
                              // a=01,c=0/+ve and return

      cpx   fac1_e            // compare (ay) exponent with fac1 exponent
      bne   lab_2828          // branch if different

      lda   (ut2_pl),y        // get (ay) mantissa1 (sign-merged external
                              // format: sign b7, 0.5 bit masked)
      ora   #$80              // normalise top bit (restore the 0.5 bit)
      cmp   fac1_1            // compare with fac1 mantissa1
      bne   lab_2828          // branch if different

      iny                     // increment index
      lda   (ut2_pl),y        // get mantissa2
      cmp   fac1_2            // compare with fac1 mantissa2
      bne   lab_2828          // branch if different

      iny                     // increment index
      lda   #$7f              // set for 1/2 value rounding byte
      cmp   fac1_r            // compare with fac1 rounding byte (set carry)
      lda   (ut2_pl),y        // get mantissa3
      sbc   fac1_3            // subtract fac1 mantissa3
      beq   lab_2850          // exit if mantissa3 equal

// gets here if number <> fac1

lab_2828:
      lda   fac1_s            // get fac1 sign (b7)
      bcc   lab_282e          // branch if fac1 > (ay)

      eor   #$ff              // else toggle fac1 sign
lab_282e:
      jmp   lab_27d0          // return a=ff,c=1/-ve a=01,c=0/+ve

// convert fac1 floating-to-fixed

lab_2831:
      lda   fac1_e            // get fac1 exponent
      beq   lab_287f          // if zero go clear fac1 and return

      sec                     // set carry for subtract
      sbc   #$98              // subtract maximum integer range exponent
      bit   fac1_s            // test fac1 sign (b7)
      bpl   lab_2845          // branch if fac1 +ve

                              // fac1 was -ve
      tax                     // copy subtracted exponent
      lda   #$ff              // overflow for -ve number
      sta   fac1_o            // set fac1 overflow byte
      jsr   lab_253d          // twos complement fac1 mantissa
      txa                     // restore subtracted exponent
lab_2845:
      ldx   #fac1_e           // set index to fac1
      cmp   #$f9              // compare exponent result
      bpl   lab_2851          // if < 8 shifts shift fac1 a times right and return

      jsr   lab_257b          // shift fac1 a times right (> 8 shifts)
      sty   fac1_o            // clear fac1 overflow byte
lab_2850:
      rts

// shift fac1 a times right

lab_2851:
      tay                     // copy shift count
      lda   fac1_s            // get fac1 sign (b7)
      and   #$80              // mask sign bit only (x000 0000)
      lsr   fac1_1            // shift fac1 mantissa1
      ora   fac1_1            // or sign in b7 fac1 mantissa1
      sta   fac1_1            // save fac1 mantissa1
      jsr   lab_2592          // shift fac1 y times right
      sty   fac1_o            // clear fac1 overflow byte
      rts

// perform int()

lab_int:
      lda   fac1_e            // get fac1 exponent
      cmp   #$98              // compare with max int
      bcs   lab_2886          // exit if >= (already int, too big for fractional part!)

      jsr   lab_2831          // convert fac1 floating-to-fixed
      sty   fac1_r            // save fac1 rounding byte
      lda   fac1_s            // get fac1 sign (b7)
      sty   fac1_s            // save fac1 sign (b7)
      eor   #$80              // toggle fac1 sign
      rol                     // shift into carry
      lda   #$98              // set new exponent
      sta   fac1_e            // save fac1 exponent
      lda   fac1_3            // get fac1 mantissa3
      sta   temp3             // save for exp() function
      jmp   lab_24d0          // do abs and normalise fac1

// clear fac1 and return

lab_287f:
      sta   fac1_1            // clear fac1 mantissa1
      sta   fac1_2            // clear fac1 mantissa2
      sta   fac1_3            // clear fac1 mantissa3
      tay                     // clear y
lab_2886:
      rts

// do - fac1

lab_gthan:
      lda   fac1_e            // get fac1 exponent
      beq   lab_2af9          // exit if fac1_e = $00

      lda   fac1_s            // get fac1 sign (b7)
      eor   #$ff              // complement it
      sta   fac1_s            // save fac1 sign (b7)
lab_2af9:
      rts

// convert float to fixed routine. accepts any value that fits in 24 bits, +ve or
// -ve and converts it into a right truncated integer in itempl and itemph

// save unsigned 16 bit integer part of fac1 in temporary integer

lab_f2fx:
      lda   fac1_e            // get fac1 exponent
      cmp   #$98              // compare with exponent = 2^24
      bcs   fpConvErr         // if >= do conversion range error

lab_f2fu:
      jsr   lab_2831          // convert fac1 floating-to-fixed
      lda   fac1_2            // get fac1 mantissa2
      ldy   fac1_3            // get fac1 mantissa3
      sty   itempl            // save temporary integer low byte
      sta   itemph            // save temporary integer high byte
      rts

// conversion range error (>= 2^24 for F2FX)

fpConvErr:
      pla
      pla
      lda #FP_ERR_CONV
      sec
      rts

// save and convert integer ay to fac1

lab_ayfc:
      sta   fac1_1            // save fac1 mantissa1 (A = high byte)
      sty   fac1_2            // save fac1 mantissa2 (Y = low byte)
      ldx   #$90              // set exponent=2^16 (integer)
      jmp   lab_27e3          // set exp=x, clear fac1_3, normalise and return

// convert y to byte in fac1

lab_1fd0:
      lda   #$00              // clear high byte
      beq   lab_ayfc          // always save and convert integer ay to fac1 and return

// ==========================================
//  Transcendentals, power, RND
// ==========================================

// do range error (illegal argument: LOG <= 0, SQR < 0)

fpRangeErr:
      pla
      pla
      lda #FP_ERR_RANGE
      sec
      rts

// perform SQR()

lab_sqr:
      lda   fac1_s            // get fac1 sign
      bmi   fpRangeErr        // if -ve do range error
      lda   fac1_e            // get exponent
      bne   lab_sqrgo         // if non zero do root
      jmp   lab_sqr0          // else just return
lab_sqrgo:
      jsr   lab_27ab          // round and copy fac1 to fac2
      lda   #$00              // clear a
      sta   fact_3            // clear remainder
      sta   fact_2            // ..
      sta   fact_1            // ..
      sta   tempb             // ..
      sta   fac1_3            // clear root
      sta   fac1_2            // ..
      sta   fac1_1            // ..
      ldx   #$18              // 24 pairs of bits to do
      lda   fac2_e            // get exponent
      lsr                     // check odd/even
      bcs   lab_sqe2          // if odd only 1 shift first time
lab_sqe1:
      asl   fac2_3            // shift highest bit of number ..
      rol   fac2_2            // ..
      rol   fac2_1            // ..
      rol   fact_3            // .. into remainder
      rol   fact_2            // ..
      rol   fact_1            // ..
      rol   tempb             // .. never overflows
lab_sqe2:
      asl   fac2_3            // shift highest bit of number ..
      rol   fac2_2            // ..
      rol   fac2_1            // ..
      rol   fact_3            // .. into remainder
      rol   fact_2            // ..
      rol   fact_1            // ..
      rol   tempb             // .. never overflows
      asl   fac1_3            // root = root * 2
      rol   fac1_2            // ..
      rol   fac1_1            // .. never overflows
      lda   fac1_3            // get root low byte
      rol                     // *2
      sta   temp3             // save partial low byte
      lda   fac1_2            // get root low mid byte
      rol                     // *2
      sta   temp3+1           // save partial low mid byte
      lda   fac1_1            // get root high mid byte
      rol                     // *2
      sta   temp3+2           // save partial high mid byte
      lda   #$00              // get root high byte (always $00)
      rol                     // *2
      sta   temp3+3           // save partial high byte
                              // carry clear for subtract +1
      lda   fact_3            // get remainder low byte
      sbc   temp3             // subtract partial low byte
      sta   temp3             // save partial low byte
      lda   fact_2            // get remainder low mid byte
      sbc   temp3+1           // subtract partial low mid byte
      sta   temp3+1           // save partial low mid byte
      lda   fact_1            // get remainder high mid byte
      sbc   temp3+2           // subtract partial high mid byte
      tay                     // copy partial high mid byte
      lda   tempb             // get remainder high byte
      sbc   temp3+3           // subtract partial high byte
      bcc   lab_sqns          // skip sub if remainder smaller
      sta   tempb             // save remainder high byte
      sty   fact_1            // save remainder high mid byte
      lda   temp3+1           // get remainder low mid byte
      sta   fact_2            // save remainder low mid byte
      lda   temp3             // get partial low byte
      sta   fact_3            // save remainder low byte
      inc   fac1_3            // increment root low byte (never any rollover)
lab_sqns:
      dex                     // decrement bit pair count
      bne   lab_sqe1          // loop if not all done
      sec                     // set carry for subtract
      lda   fac2_e            // get exponent
      sbc   #$80              // normalise
      ror                     // /2 and re-bias to $80
      adc   #$00              // add bit zero back in (allow for half shift)
      sta   fac1_e            // save it
      jmp   lab_24d5          // normalise fac1 and return
lab_sqr0:
      rts

// perform LOG()

lab_log:
      jsr   lab_27ca          // test sign and zero
      beq   lab_25c4          // if zero do range error
      bpl   lab_25c7          // skip error if +ve
lab_25c4:
      jmp   fpRangeErr        // do range error (-ve)
lab_25c7:
      lda   fac1_e            // get fac1 exponent
      sbc   #$7f              // normalise it
      pha                     // save it
      lda   #$80              // set exponent to zero
      sta   fac1_e            // save fac1 exponent
      lda   #<fpHalfRt2       // set 1/root2 pointer low byte
      ldy   #>fpHalfRt2       // set 1/root2 pointer high byte
      jsr   lab_246c          // add (ay) to fac1 (1/root2)
      lda   #<fpRt2           // set root2 pointer low byte
      ldy   #>fpRt2           // set root2 pointer high byte
      jsr   lab_26ca          // root2/(x+(1/root2))
      lda   #<fpOne           // set 1 pointer low byte
      ldy   #>fpOne           // set 1 pointer high byte
      jsr   lab_2455          // subtract (ay) from fac1
      lda   #<fpLogC          // set pointer low byte to counter
      ldy   #>fpLogC          // set pointer high byte to counter
      jsr   lab_2b6e          // ^2 then series evaluation
      lda   #<fpNHalf         // set -0.5 pointer low byte
      ldy   #>fpNHalf         // set -0.5 pointer high byte
      jsr   lab_246c          // add (ay) to fac1
      pla                     // restore fac1 exponent
      jsr   lab_2912          // add exponent byte to fac1
      lda   #<fpLog2          // set LOG(2) pointer low byte
      ldy   #>fpLog2          // set LOG(2) pointer high byte
      jmp   lab_25fb          // multiply fac1 by ln(2), normalise and return
                              // (tail call -- the original falls through into
                              //  LAB_25FB; a jsr here would return into
                              //  lab_power and loop forever.)

// perform power function

lab_power:
      beq   lab_exp           // go do EXP() if fac1 == 0 (EhBASIC parity)
      lda   fac2_e            // get fac2 exponent
      bne   lab_2abf          // branch if fac2<>0
      jmp   lab_24f3          // clear fac1 exponent and sign and return
lab_2abf:
      ldx   #<fpPwTmp         // set destination pointer low byte
      ldy   #>fpPwTmp         // set destination pointer high byte
      jsr   lab_2778          // pack fac1 into (xy)
      lda   fac2_s            // get fac2 sign (b7)
      bpl   lab_2ad9          // branch if fac2>0
                              // else fac2 is -ve and can only be raised to an
                              // integer power which gives an x +j0 result
      jsr   lab_int           // perform INT
      lda   #<fpPwTmp         // set source pointer low byte
      ldy   #>fpPwTmp         // set source pointer high byte
      jsr   lab_27f8          // compare fac1 with (ay)
      bne   lab_2ad9          // branch if fac1 <> (ay) to allow range error
                              // this will leave fac1 -ve and cause a range
                              // error when LOG() is called
      tya                     // clear sign b7
      ldy   temp3             // save mantissa 3 from INT() function as sign in y
                              // for possible later negation, b0
lab_2ad9:
      jsr   lab_279d          // save fac1 sign and copy ABS(fac2) to fac1
      tya                     // copy sign back ..
      pha                     // .. and save it
      jsr   lab_log           // do LOG(n)
      lda   #<fpPwTmp         // set pointer low byte
      ldy   #>fpPwTmp         // set pointer high byte
      jsr   lab_25fb          // multiply by the saved fac1
      jsr   lab_exp           // go do EXP(n)
      pla                     // pull sign from stack
      lsr                     // b0 is to be tested, shift to cb
      bcc   lab_powX          // if no bit then exit
      jmp   lab_gthan         // else negate fac1 and return
lab_powX:
      rts

// perform EXP() (x^e)

lab_exp:
      lda   #<fpExp144        // set 1.443 pointer low byte
      ldy   #>fpExp144        // set 1.443 pointer high byte
      jsr   lab_25fb          // do convert ay, fca1*(ay)
      lda   fac1_r            // get fac1 rounding byte
      adc   #$50              // +$50/$100
      bcc   lab_2b2b          // skip rounding if no carry
      jsr   lab_27c2          // round fac1 (no check)
lab_2b2b:
      sta   fac2_r            // save fac2 rounding byte
      jsr   lab_27ae          // copy fac1 to fac2
      lda   fac1_e            // get fac1 exponent
      cmp   #$88              // compare with EXP limit (256d)
      bcc   lab_2b39          // branch if less
lab_2b36:
      jsr   lab_2690          // handle overflow and underflow
lab_2b39:
      jsr   lab_int           // perform INT
      lda   temp3             // get mantissa 3 from INT() function
      clc                     // clear carry for add
      adc   #$81              // normalise +1
      beq   lab_2b36          // if $00 go handle overflow
      sec                     // set carry for subtract
      sbc   #$01              // now correct for exponent
      pha                     // save fac2 exponent
                              // swap fac1 and fac2
      ldx   #$04              // 4 bytes to do
lab_2b49:
      lda   fac2_e,x          // get fac2,x
      ldy   fac1_e,x          // get fac1,x
      sta   fac1_e,x          // save fac1,x
      sty   fac2_e,x          // save fac2,x
      dex                     // decrement count/index
      bpl   lab_2b49          // loop if not all done
      lda   fac2_r            // get fac2 rounding byte
      sta   fac1_r            // save as fac1 rounding byte
      jsr   lab_subtract      // perform subtraction, fac2 from fac1
      jsr   lab_gthan         // do - fac1
      lda   #<fpExpC          // set counter pointer low byte
      ldy   #>fpExpC          // set counter pointer high byte
      jsr   lab_2b84          // go do series evaluation
      lda   #$00              // clear a
      sta   fac_sc            // clear sign compare (fac1 eor fac2)
      pla                     //.get saved fac2 exponent
      jmp   lab_2675          // test and adjust accumulators and return

// ^2 then series evaluation

lab_2b6e:
      sta   cptrl             // save count pointer low byte
      sty   cptrh             // save count pointer high byte
      jsr   lab_276e          // pack fac1 into adatal
      lda   #<adatal          // set pointer low byte (y already $00)
      jsr   lab_25fb          // do convert ay, fca1*(ay)
      jsr   lab_2b88          // go do series evaluation
      lda   #<adatal          // pointer to original # low byte
      ldy   #>adatal          // pointer to original # high byte
      jmp   lab_25fb          // do convert ay, fca1*(ay) and return

// series evaluation

lab_2b84:
      sta   cptrl             // save count pointer low byte
      sty   cptrh             // save count pointer high byte
lab_2b88:
      ldx   #<numexp          // set pointer low byte
      jsr   lab_2770          // set pointer high byte and pack fac1 into numexp
      lda   (cptrl),y         // get constants count
      sta   numcon            // save constants count
      ldy   cptrl             // get count pointer low byte
      iny                     // increment it (now constants pointer)
      tya                     // copy it
      bne   lab_2b97          // skip next if no overflow
      inc   cptrh             // else increment high byte
lab_2b97:
      sta   cptrl             // save low byte
      ldy   cptrh             // get high byte
lab_2b9b:
      jsr   lab_25fb          // do convert ay, fca1*(ay)
      lda   cptrl             // get constants pointer low byte
      ldy   cptrh             // get constants pointer high byte
      clc                     // clear carry for add
      adc   #$04              // +4 to low pointer (4 bytes per constant)
      bcc   lab_2ba8          // skip next if no overflow
      iny                     // increment high byte
lab_2ba8:
      sta   cptrl             // save pointer low byte
      sty   cptrh             // save pointer high byte
      jsr   lab_246c          // add (ay) to fac1
      lda   #<numexp          // set pointer low byte to partial @ numexp
      ldy   #>numexp          // set pointer high byte to partial @ numexp
      dec   numcon            // decrement constants count
      bne   lab_2b9b          // loop until all done
      rts

// RND(n), 32 bit Galoise version. make n=0 for 19th next number in
// sequence or n<>0 to get 19th next number in sequence after seed n.

lab_rnd:
      lda   fac1_e            // get fac1 exponent
      beq   lab_rndnext       // do next random # if zero
      ldx   #rbyte4           // set PRNG pointer low byte
      ldy   #$00              // set PRNG pointer high byte
      jsr   lab_2778          // pack fac1 into (xy)
lab_rndnext:
      ldx   #$af              // set EOR byte
      ldy   #$13              // do this nineteen times
lab_rndloop:
      asl   rbyte1            // shift PRNG most significant byte
      rol   rbyte2            // shift PRNG middle byte
      rol   rbyte3            // shift PRNG least significant byte
      rol   rbyte4            // shift PRNG extra byte
      bcc   lab_rndninc       // branch if bit 32 clear
      txa                     // set EOR byte
      eor   rbyte1            // EOR PRNG extra byte
      sta   rbyte1            // save new PRNG extra byte
lab_rndninc:
      dey                     // decrement loop count
      bne   lab_rndloop       // loop if not all done
      ldx   #$02              // three bytes to copy
lab_rndcopy:
      lda   rbyte1,x          // get PRNG byte
      sta   fac1_1,x          // save fac1 byte
      dex
      bpl   lab_rndcopy       // loop if not complete
      lda   #$80              // set the exponent
      sta   fac1_e            // save fac1 exponent
      asl                     // clear a
      sta   fac1_s            // save fac1 sign
      jmp   lab_24d5          // normalise fac1 and return

// perform COS()

lab_cos:
      lda   #<fpPiHalf        // set (pi/2) pointer low byte
      ldy   #>fpPiHalf        // set (pi/2) pointer high byte
      jsr   lab_246c          // add (ay) to fac1

// perform SIN()

lab_sin:
      jsr   lab_27ab          // round and copy fac1 to fac2
      lda   #<fpTwoPi         // set (2*pi) pointer low byte
      ldy   #>fpTwoPi         // set (2*pi) pointer high byte
      ldx   fac2_s            // get fac2 sign (b7)
      jsr   lab_26c2          // divide by (ay) (x=sign)
      jsr   lab_27ab          // round and copy fac1 to fac2
      jsr   lab_int           // perform INT
      lda   #$00              // clear byte
      sta   fac_sc            // clear sign compare (fac1 eor fac2)
      jsr   lab_subtract      // perform subtraction, fac2 from fac1
      lda   #<fpQuarter       // set 0.25 pointer low byte
      ldy   #>fpQuarter       // set 0.25 pointer high byte
      jsr   lab_2455          // perform subtraction, (ay) from fac1
      lda   fac1_s            // get fac1 sign (b7)
      pha                     // save fac1 sign
      bpl   lab_2c35          // branch if +ve
      jsr   lab_244e          // add 0.5 to fac1
      lda   fac1_s            // get fac1 sign (b7)
      bmi   lab_2c38          // branch if -ve
      lda   cflag             // get comparison evaluation flag
      eor   #$ff              // toggle flag
      sta   cflag             // save comparison evaluation flag
lab_2c35:
      jsr   lab_gthan         // do - fac1
lab_2c38:
      lda   #<fpQuarter       // set 0.25 pointer low byte
      ldy   #>fpQuarter       // set 0.25 pointer high byte
      jsr   lab_246c          // add (ay) to fac1
      pla                     // restore fac1 sign
      bpl   lab_2c45          // branch if was +ve
      jsr   lab_gthan         // else correct fac1
lab_2c45:
      lda   #<fpSinC          // set pointer low byte to counter
      ldy   #>fpSinC          // set pointer high byte to counter
      jmp   lab_2b6e          // ^2 then series evaluation and return

// perform TAN()

lab_tan:
      jsr   lab_276e          // pack fac1 into adatal
      lda   #$00              // clear byte
      sta   cflag             // clear comparison evaluation flag
      jsr   lab_sin           // go do SIN(n)
      ldx   #<fpPwTmp         // set sin(n) pointer low byte
      ldy   #>fpPwTmp         // set sin(n) pointer high byte
      jsr   lab_2778          // pack fac1 into (xy)
      lda   #<adatal          // set n pointer low addr
      ldy   #>adatal          // set n pointer high addr
      jsr   lab_ufac          // unpack memory (ay) into fac1
      lda   #$00              // clear byte
      sta   fac1_s            // clear fac1 sign (b7)
      lda   cflag             // get comparison evaluation flag
      jsr   lab_2c74          // save flag and go do series evaluation
      lda   #<fpPwTmp         // set sin(n) pointer low byte
      ldy   #>fpPwTmp         // set sin(n) pointer high byte
      jmp   lab_26ca          // convert ay and do (ay)/fac1

lab_2c74:
      pha                     // save comparison evaluation flag
      jmp   lab_2c35          // go do series evaluation

// perform ATN()

lab_atn:
      lda   fac1_s            // get fac1 sign (b7)
      pha                     // save sign
      bpl   lab_2ca1          // branch if +ve
      jsr   lab_gthan         // else do - fac1
lab_2ca1:
      lda   fac1_e            // get fac1 exponent
      pha                     // push exponent
      cmp   #$81              // compare with 1
      bcc   lab_2caf          // branch if fac1<1
      lda   #<fpOne           // set 1 pointer low byte
      ldy   #>fpOne           // set 1 pointer high byte
      jsr   lab_26ca          // convert ay and do (ay)/fac1
lab_2caf:
      lda   #<fpAtnC          // set pointer low byte to counter
      ldy   #>fpAtnC          // set pointer high byte to counter
      jsr   lab_2b6e          // ^2 then series evaluation
      pla                     // restore old fac1 exponent
      cmp   #$81              // compare with 1
      bcc   lab_2cc2          // branch if fac1<1
      lda   #<fpPiHalf        // set (pi/2) pointer low byte
      ldy   #>fpPiHalf        // set (pi/2) pointer high byte
      jsr   lab_2455          // perform subtraction, (ay) from fac1
lab_2cc2:
      pla                     // restore fac1 sign
      bpl   lab_atnX          // exit if was +ve
      jmp   lab_gthan         // else do - fac1 and return
lab_atnX:
      rts

// evaluate new ASCII digit (adds the exponent byte in A to fac1)

lab_2912:
      pha                     // save digit
      jsr   lab_27ab          // round and copy fac1 to fac2
      pla                     // restore digit
      jsr   lab_27db          // save a as integer byte
      lda   fac2_s            // get fac2 sign (b7)
      eor   fac1_s            // toggle with fac1 sign (b7)
      sta   fac_sc            // save sign compare (fac1 eor fac2)
      ldx   fac1_e            // get fac1 exponent
      jmp   lab_add           // add fac2 to fac1 and return

// add 0.5 to fac1

lab_244e:
      lda   #<fpHalf          // set 0.5 pointer low byte
      ldy   #>fpHalf          // set 0.5 pointer high byte
      jmp   lab_246c          // add (ay) to fac1

// convert ay and do (ay)/fac1

lab_26ca:
      jsr   lab_264d          // unpack memory (ay) into fac2
      jmp   lab_divide        // do fac2/fac1

// pack fac1 into adatal

lab_276e:
      ldx   #<adatal          // set pointer low byte
lab_2770:
      ldy   #>adatal          // set pointer high byte (always $00)
      jmp   lab_2778          // pack fac1 into (xy) and return

// --- Step-3 constants and series tables (4-byte external floats) ---

fpLogC:
      .byte $02               // counter
      .byte $80,$19,$56,$62   // 0.59898
      .byte $80,$76,$22,$F3   // 0.96147
      .byte $82,$38,$AA,$40   // 2.88539
fpHalfRt2:
      .byte $80,$35,$04,$F3   // 0.70711  1/root 2
fpRt2:
      .byte $81,$35,$04,$F3   // 1.41421  root 2
fpNHalf:
      .byte $80,$80,$00,$00   // -0.5
fpLog2:
      .byte $80,$31,$72,$18   // 0.69315  LOG(2)
fpExp144:
      .byte $81,$38,$AA,$3B   // 1.4427   (1/LOG base 2 e)
fpExpC:
      .byte $06               // counter
      .byte $74,$63,$90,$8C   // 2.17023e-4
      .byte $77,$23,$0C,$AB   // 0.00124
      .byte $7A,$1E,$94,$00   // 0.00968
      .byte $7C,$63,$42,$80   // 0.05548
      .byte $7E,$75,$FE,$D0   // 0.24023
      .byte $80,$31,$72,$15   // 0.69315
      .byte $81,$00,$00,$00   // 1.00000
fpPiHalf:
      .byte $81,$49,$0F,$DB   // 1.570796371 (pi/2)
fpSinC:
      .byte $04               // counter
      .byte $86,$1E,$D7,$FB   // 39.7109
      .byte $87,$99,$26,$65   // -76.575
      .byte $87,$23,$34,$58   // 81.6022
      .byte $86,$A5,$5D,$E1   // -41.3417
fpTwoPi:
      .byte $83,$49,$0F,$DB   // 6.28319 (2*pi)
fpAtnC:
      .byte $08               // counter
      .byte $78,$3A,$C5,$37   // 0.00285
      .byte $7B,$83,$A2,$5C   // -0.0160686
      .byte $7C,$2E,$DD,$4D   // 0.0426915
      .byte $7D,$99,$B0,$1E   // -0.0750429
      .byte $7D,$59,$ED,$24   // 0.106409
      .byte $7E,$91,$72,$00   // -0.142036
      .byte $7E,$4C,$B9,$73   // 0.199926
      .byte $7F,$AA,$AA,$53   // -0.333331
fpAtnOne:
      .byte $81,$00,$00,$00   // 1.0 -- the series loop adds it as the 9th
                              // term (c8): lab_2b9b reads 4 bytes past the
                              // table. EhBASIC parity: LAB_259C follows
                              // LAB_2CC9. Missing it made every ATN result
                              // wrong by 0.75*x (fpQuarter followed here).
fpQuarter:
      .byte $7F,$00,$00,$00   // 0.25

}

.label fpEnd = *
