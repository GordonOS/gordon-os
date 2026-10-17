
// ==========================================
//  Derived from EhBASIC v2.22p5
//  Original: (C) Lee Davison 2001-2016
//  Website: http://members.multimania.co.uk/leeedavison/
//  Contact: leeedavison@googlemail.com
//
//  GordonOS port modifications (C) 2026 Besim Atalay
//  Relocated to $5000 with .encoding "ascii"
//  I/O vectors patched via src/tasks/dynamic/basic.asm
//  Memory layout: ram_base=$8000 (pinned), ram_top=$A7FF
// ==========================================

// enhanced basic to assemble under 6502 simulator, $ver 2.22p5

// $e7e1 $e7cf $e7c6 $e7d3 $e7d1 $e7d5 $e7cf $e81e $e825

// 2.00      new revision numbers start here
// 2.01      fixed lcase$() and ucase$()
// 2.02      new get value routine done
// 2.03      changed rnd() to galoise method
// 2.04      fixed spc()
// 2.05      new get value routine fixed
// 2.06      changed usr() code
// 2.07      fixed str$()
// 2.08      changed input and read to remove need for $00 start to input buffer
// 2.09      fixed rnd()
// 2.10      integrated missed changes from an earlier version
// 2.20      added else to if .. then and fixed if .. goto <statement> to cause error
// 2.21      fixed if .. then return to not cause error
// 2.22      fixed rnd() breaking the get byte routine
// 2.22p     patched to disable use of decimal mode and fix ibuff issues
//              (bugsnquirks.txt notes 2, 4 and 5)
//              tabs converted to spaces, tabwidth=6
// 2.22p2    fixed can't continue error on 1st statement after direct mode
//              changed input to throw "break in line ##" on empty line input
// 2.22p3    fixed ram above code / ibuff above ehbasic patch breaks str$()
//              fix provided by github user mgcaret
// 2.22p4    fixed string compare of equal strings in direct mode returns false
//              fixed false stored to a variable after a string compare 
//                 is > 0 and < 1e-16
//              added additional stack floor protection for background interrupts
//              fixed conditional loop & next cannot find their data strucure on stack
// 2.22p5    fixes issues reported by users ruud and dclxvi on the 6502.org forum
//      5.0     http://forum.6502.org/viewtopic.php?f=5&t=5500
//              sanity check for ram top allows values below ram base
//      5.1-7   http://forum.6502.org/viewtopic.php?f=5&t=5606
//              1-7 coresponds to the bug# in the thread
//      5.1     to expression with a subtract may evaluate with the sign bit flipped
//      5.3     call to lab_1b5b may return to an address -$100 (page not incremented)
//      5.4     string concatenate followed by minus or not() crashes ehbasic
//      5.5     garbage collection may cause an overlap with temporary strings
//      5.6     floating point multiply rounding bug
//      5.7     val() may cause string variables to be trashed

// zero page use ..

// the following locations are bulk initialized from strtab at lab_gmem
.const lab_warm = $04       // basic warm start entry point
.const wrmjpl = lab_warm+1// basic warm start vector jump low byte
.const wrmjph = lab_warm+2// basic warm start vector jump high byte

.const nullct = $0d       // nulls output after each line
.const tpos = $0e       // basic shell position byte
.const twidth = $0f       // basic shell width byte
.const iclim = $10       // input column limit
.const itempl = $11       // temporary integer low byte
.const itemph = itempl+1  // temporary integer high byte
// end bulk initialize from strtab at lab_gmem

.const nums_1 = itempl    // number to bin/hex string convert msb
.const nums_2 = nums_1+1  // number to bin/hex string convert
.const nums_3 = nums_1+2  // number to bin/hex string convert lsb

.const srchc = $5b       // search character
.const temp3 = srchc     // temp byte used in number routines
.const scnquo = $5c       // scan-between-quotes flag
.const asrch = scnquo    // alt search character

.const xoaw_l = srchc     // exclusive or, or and and word low byte
.const xoaw_h = scnquo    // exclusive or, or and and word high byte

.const ibptr = $5d       // input buffer pointer
.const dimcnt = ibptr     // # of dimensions
.const tindx = ibptr     // token index

.const defdim = $5e       // default dim flag
.const dtypef = $5f       // data type flag, $ff=string, $00=numeric
.const oquote = $60       // open quote flag (b7) (flag: data scan// list quote// memory)
.const gclctd = $60       // garbage collected flag
.const sufnxf = $61       // subscript/fnx flag, 1xxx xxx = fn(0xxx xxx)
.const imode = $62       // input mode flag, $00=input, $80=read

.const cflag = $63       // comparison evaluation flag

.const tabsiz = $64       // tab step size (was input flag)

.const next_s = $65       // next descriptor stack address

                              // these two bytes form a word pointer to the item
                              // currently on top of the descriptor stack
.const last_sl = $66       // last descriptor stack address low byte
.const last_sh = $67       // last descriptor stack address high byte (always $00)

.const des_sk = $68       // descriptor stack start address (temp strings)

//                 = $70       // end of descriptor stack

.const ut1_pl = $71       // utility pointer 1 low byte
.const ut1_ph = ut1_pl+1  // utility pointer 1 high byte
.const ut2_pl = $73       // utility pointer 2 low byte
.const ut2_ph = ut2_pl+1  // utility pointer 2 high byte

.const temp_2 = ut1_pl    // temp byte for block move    

.const fact_1 = $75       // fac temp mantissa1
.const fact_2 = fact_1+1  // fac temp mantissa2
.const fact_3 = fact_2+1  // fac temp mantissa3

.const dims_l = fact_2    // array dimension size low byte
.const dims_h = fact_3    // array dimension size high byte

.const tempb = $78       // temp page 0 byte

.const smeml = $79       // start of mem low byte       (start-of-basic)
.const smemh = smeml+1   // start of mem high byte      (start-of-basic)
.const svarl = $7b       // start of vars low byte      (start-of-variables)
.const svarh = svarl+1   // start of vars high byte     (start-of-variables)
.const sarryl = $7d       // var mem end low byte        (start-of-arrays)
.const sarryh = sarryl+1  // var mem end high byte       (start-of-arrays)
.const earryl = $7f       // array mem end low byte      (end-of-arrays)
.const earryh = earryl+1  // array mem end high byte     (end-of-arrays)
.const sstorl = $81       // string storage low byte     (string storage (moving down))
.const sstorh = sstorl+1  // string storage high byte    (string storage (moving down))
.const sutill = $83       // string utility ptr low byte
.const sutilh = sutill+1  // string utility ptr high byte
.const ememl = $85       // end of mem low byte         (limit-of-memory)
.const ememh = ememl+1   // end of mem high byte        (limit-of-memory)
.const clinel = $87       // current line low byte       (basic line number)
.const clineh = clinel+1  // current line high byte      (basic line number)
.const blinel = $89       // break line low byte         (previous basic line number)
.const blineh = blinel+1  // break line high byte        (previous basic line number)

.const cpntrl = $8b       // continue pointer low byte
.const cpntrh = cpntrl+1  // continue pointer high byte

.const dlinel = $8d       // current data line low byte
.const dlineh = dlinel+1  // current data line high byte

.const dptrl = $8f       // data pointer low byte
.const dptrh = dptrl+1   // data pointer high byte

.const rdptrl = $91       // read pointer low byte
.const rdptrh = rdptrl+1  // read pointer high byte

.const varnm1 = $93       // current var name 1st byte
.const varnm2 = varnm1+1  // current var name 2nd byte

.const cvaral = $95       // current var address low byte
.const cvarah = cvaral+1  // current var address high byte

.const frnxtl = $97       // var pointer for for/next low byte
.const frnxth = frnxtl+1  // var pointer for for/next high byte

.const tidx1 = frnxtl    // temp line index

.const lvarpl = frnxtl    // let var pointer low byte
.const lvarph = frnxth    // let var pointer high byte

.const prstk = $99       // precedence stacked flag

.const comp_f = $9b       // compare function flag, bits 0,1 and 2 used
                              // bit 2 set if >
                              // bit 1 set if =
                              // bit 0 set if <

.const func_l = $9c       // function pointer low byte
.const func_h = func_l+1  // function pointer high byte

.const garb_l = func_l    // garbage collection working pointer low byte
.const garb_h = func_h    // garbage collection working pointer high byte

.const des_2l = $9e       // string descriptor_2 pointer low byte
.const des_2h = des_2l+1  // string descriptor_2 pointer high byte

.const g_step = $a0       // garbage collect step size

.const fnxjmp = $a1       // jump vector for functions
.const fnxjpl = fnxjmp+1  // functions jump vector low byte
.const fnxjph = fnxjmp+2  // functions jump vector high byte

.const g_indx = fnxjpl    // garbage collect temp index

.const fac2_r = $a3       // fac2 rounding byte

.const adatal = $a4       // array data pointer low byte
.const adatah = adatal+1  // array data pointer high  byte

.const nbendl = adatal    // new block end pointer low byte
.const nbendh = adatah    // new block end pointer high  byte

.const obendl = $a6       // old block end pointer low byte
.const obendh = obendl+1  // old block end pointer high  byte

.const numexp = $a8       // string to float number exponent count
.const expcnt = $a9       // string to float exponent count

.const numbit = numexp    // bit count for array element calculations

.const numdpf = $aa       // string to float decimal point flag
.const expneg = $ab       // string to float eval exponent -ve flag

.const astrtl = numdpf    // array start pointer low byte
.const astrth = expneg    // array start pointer high  byte

.const histrl = numdpf    // highest string low byte
.const histrh = expneg    // highest string high  byte

.const baslnl = numdpf    // basic search line pointer low byte
.const baslnh = expneg    // basic search line pointer high  byte

.const fvar_l = numdpf    // find/found variable pointer low byte
.const fvar_h = expneg    // find/found variable pointer high  byte

.const ostrtl = numdpf    // old block start pointer low byte
.const ostrth = expneg    // old block start pointer high  byte

.const vrschl = numdpf    // variable search pointer low byte
.const vrschh = expneg    // variable search pointer high  byte

.const fac1_e = $ac       // fac1 exponent
.const fac1_1 = fac1_e+1  // fac1 mantissa1
.const fac1_2 = fac1_e+2  // fac1 mantissa2
.const fac1_3 = fac1_e+3  // fac1 mantissa3
.const fac1_s = fac1_e+4  // fac1 sign (b7)

.const str_ln = fac1_e    // string length
.const str_pl = fac1_1    // string pointer low byte
.const str_ph = fac1_2    // string pointer high byte

.const des_pl = fac1_2    // string descriptor pointer low byte
.const des_ph = fac1_3    // string descriptor pointer high byte

.const mids_l = fac1_3    // mid$ string temp length byte

.const negnum = $b1       // string to float eval -ve flag
.const numcon = $b1       // series evaluation constant count

.const fac1_o = $b2       // fac1 overflow byte

.const fac2_e = $b3       // fac2 exponent
.const fac2_1 = fac2_e+1  // fac2 mantissa1
.const fac2_2 = fac2_e+2  // fac2 mantissa2
.const fac2_3 = fac2_e+3  // fac2 mantissa3
.const fac2_s = fac2_e+4  // fac2 sign (b7)

.const fac_sc = $b8       // fac sign comparison, acc#1 vs #2
.const fac1_r = $b9       // fac1 rounding byte

.const ssptr_l = fac_sc    // string start pointer low byte
.const ssptr_h = fac1_r    // string start pointer high byte

.const sdescr = fac_sc    // string descriptor pointer

.const csidx = $ba       // line crunch save index
.const asptl = csidx     // array size/pointer low byte
.const aspth = $bb       // array size/pointer high byte

.const btmpl = asptl     // basic pointer temp low byte
.const btmph = aspth     // basic pointer temp low byte

.const cptrl = asptl     // basic pointer temp low byte
.const cptrh = aspth     // basic pointer temp low byte

.const sendl = asptl     // basic pointer temp low byte
.const sendh = aspth     // basic pointer temp low byte

// the following locations are bulk initialized from lab_2cee at lab_2d4e
.const lab_igby = $bc       // get next basic byte subroutine (ZP jmp trampoline)

.const lab_gbyt = $c2       // get current basic byte subroutine (ZP jmp trampoline)

//                 = $c3-$c4   // jmp peekTextByte OPERAND -- never alias!
// end bulk initialize from lab_2cee at lab_2d4e
// the ZP trampoline shrink left $c5 and $ca-$d7 free;
//   $c6/$c7 = varPtr, $c8/$c9 = bpntrl/h
.const bpntrl = $c8       // basic execute (get byte) pointer low byte
.const bpntrh = bpntrl+1  // basic execute (get byte) pointer high byte

.const varPtr = $c6       // wrapper base: var-space access (2 bytes)

.const rbyte4 = $d8       // extra prng byte
.const rbyte1 = rbyte4+1  // most significant prng byte
.const rbyte2 = rbyte4+2  // middle prng byte
.const rbyte3 = rbyte4+3  // least significant prng byte

// $dc-$e1 are free -- this port has no IRQ/NMI handler flags or vectors
//                 = $e2       // unused
//                 = $e3       // unused
//                 = $e4       // unused
//                 = $e5       // unused
//                 = $e6       // unused
//                 = $e7       // unused
//                 = $e8       // unused
//                 = $e9       // unused
//                 = $ea       // unused
//                 = $eb       // unused
//                 = $ec       // unused
//                 = $ed       // unused
//                 = $ee       // unused

.const decss = $14       // number to decimal string start ($14-$24 -- relocated
                         // from $EF so the 17-byte buffer + ZP,Y can never
                         // wrap past $F0 into kernel/IRQ ZP, section 3.5)
.const decssp1 = decss+1   // number to decimal string start

// token values needed for basic

// primary command tokens (can start a statement)

.const tk_end = $80             // end token
.const tk_for = tk_end+1        // for token
.const tk_next = tk_for+1        // next token
.const tk_data = tk_next+1       // data token
.const tk_input = tk_data+1       // input token
.const tk_dim = tk_input+1      // dim token
.const tk_read = tk_dim+1        // read token
.const tk_let = tk_read+1       // let token
.const tk_dec = tk_let+1        // dec token
.const tk_goto = tk_dec+1        // goto token
.const tk_run = tk_goto+1       // run token
.const tk_if = tk_run+1        // if token
.const tk_restore = tk_if+1         // restore token
.const tk_gosub = tk_restore+1    // gosub token
.const tk_return = tk_gosub+1      // return token
.const tk_rem = tk_return+1     // rem token
.const tk_stop = tk_rem+1        // stop token
.const tk_on = tk_stop+1       // on token
.const tk_inc = tk_on+1         // inc token
.const tk_wait = tk_inc+1        // wait token
.const tk_load = tk_wait+1       // load token
.const tk_save = tk_load+1       // save token
.const tk_def = tk_save+1       // def token
.const tk_poke = tk_def+1        // poke token
.const tk_call = tk_poke+1       // call token
.const tk_do = tk_call+1       // do token
.const tk_loop = tk_do+1         // loop token
.const tk_print = tk_loop+1       // print token
.const tk_cont = tk_print+1      // cont token
.const tk_list = tk_cont+1       // list token
.const tk_clear = tk_list+1       // clear token
.const tk_new = tk_clear+1      // new token
.const tk_width = tk_new+1        // width token
.const tk_get = tk_width+1      // get token
.const tk_swap = tk_get+1        // swap token
.const tk_exit = tk_swap+1     // exit token (GordonOS: terminate task)
.const tk_dir = tk_exit+1      // dir token (GordonOS: list REU filesystem)

// secondary command tokens, can't start a statement

.const tk_tab = tk_dir+1        // tab token
.const tk_else = tk_tab+1        // else token
.const tk_to = tk_else+1       // to token
.const tk_fn = tk_to+1         // fn token
.const tk_spc = tk_fn+1         // spc token
.const tk_then = tk_spc+1        // then token
.const tk_not = tk_then+1       // not token
.const tk_step = tk_not+1        // step token
.const tk_until = tk_step+1       // until token
.const tk_while = tk_until+1      // while token

// opperator tokens

.const tk_plus = tk_while+1       // + token
.const tk_minus = tk_plus+1       // - token
.const tk_mul = tk_minus+1      // * token
.const tk_div = tk_mul+1        // / token
.const tk_power = tk_div+1        // ^ token
.const tk_and = tk_power+1      // and token
.const tk_eor = tk_and+1        // eor token
.const tk_or = tk_eor+1        // or token
.const tk_rshift = tk_or+1         // rshift token
.const tk_lshift = tk_rshift+1     // lshift token
.const tk_gt = tk_lshift+1     // > token
.const tk_equal = tk_gt+1         // = token
.const tk_lt = tk_equal+1      // < token

// functions tokens

.const tk_sgn = tk_lt+1         // sgn token
.const tk_int = tk_sgn+1        // int token
.const tk_abs = tk_int+1        // abs token
.const tk_fre = tk_abs+1        // fre token
.const tk_pos = tk_fre+1        // pos token
.const tk_sqr = tk_pos+1        // sqr token
.const tk_rnd = tk_sqr+1        // rnd token
.const tk_log = tk_rnd+1        // log token
.const tk_exp = tk_log+1        // exp token
.const tk_cos = tk_exp+1        // cos token
.const tk_sin = tk_cos+1        // sin token
.const tk_tan = tk_sin+1        // tan token
.const tk_atn = tk_tan+1        // atn token
.const tk_peek = tk_atn+1        // peek token
.const tk_len = tk_peek+1       // len token
.const tk_strs = tk_len+1        // str$ token
.const tk_val = tk_strs+1       // val token
.const tk_asc = tk_val+1        // asc token
.const tk_ucases = tk_asc+1        // ucase$ token
.const tk_lcases = tk_ucases+1     // lcase$ token
.const tk_chrs = tk_lcases+1     // chr$ token
.const tk_hexs = tk_chrs+1       // hex$ token
.const tk_bins = tk_hexs+1       // bin$ token
.const tk_max = tk_bins+1       // max token
.const tk_min = tk_max+1        // min token
.const tk_pi = tk_min+1        // pi token
.const tk_lefts = tk_pi+1       // left$ token
.const tk_rights = tk_lefts+1      // right$ token
.const tk_mids = tk_rights+1     // mid$ token

// GordonOS graphics tokens -- getpixel($DD) is a function;
// the statement tokens are $DE-$F5.

.const tk_getpixel = $DD        // getpixel(x,y) function token
.const tk_mode = $DE            // mode token
.const tk_cursor = $DF          // cursor token
.const tk_cls = $E0             // cls token
.const tk_plot = $E1            // plot token
.const tk_line = $E2            // line token
.const tk_box = $E3             // box token
.const tk_fillbox = $E4         // fillbox token
.const tk_circle = $E5          // circle token
.const tk_ellipse = $E6         // ellipse token
.const tk_flood = $E7           // flood token
.const tk_gchar = $E8           // gchar token
.const tk_gtext = $E9           // gtext token
.const tk_printat = $EA         // printat token
.const tk_locate = $EB          // locate token
.const tk_colour = $EC          // colour token
.const tk_scroll = $ED          // scroll token
.const tk_gdef = $EE            // gdef token
.const tk_glyph = $EF           // glyph token
.const tk_border = $F0          // border token
.const tk_pen1 = $F1            // pen1 token
.const tk_pen2 = $F2            // pen2 token
.const tk_shadow = $F3          // shadow token
.const tk_pen0 = $F4            // pen0 token
.const tk_scrollby = $F5        // scrollby token (scroll the screen n rows)
.const tk_pen3 = $F6            // pen3 token

// offsets from a base of x or y

.const plus_0 = $00       // x or y plus 0
.const plus_1 = $01       // x or y plus 1
.const plus_2 = $02       // x or y plus 2
.const plus_3 = $03       // x or y plus 3

.const lab_stak = $0100     // stack bottom, no offset

.const lab_skfe = lab_stak+$fe
                              // flushed stack address
.const lab_skff = lab_stak+$ff
                              // flushed stack address

// the following locations are bulk initialized from pg2_tabs at lab_cold
.const ccflag = $0200     // basic ctrl-c flag, 00 = enabled, 01 = dis
.const ccbyte = ccflag+1  // basic ctrl-c byte
.const ccnull = ccbyte+1  // basic ctrl-c byte timeout

.const vec_cc = ccnull+1  // ctrl c check vector
// end bulk initialize from pg2_tabs at lab_cold

// the following locations are bulk initialized by min_mon.asm from lab_vec at lab_stlp
.const vec_in = vec_cc+2  // input vector
.const vec_out = vec_in+2  // output vector
.const vec_ld = vec_out+2 // load vector
.const vec_sv = vec_ld+2  // save vector
// end bulk initialize by min_mon.asm from lab_vec at lab_stlp

// ibuffs can now be anywhere in ram, ensure that the max length is < $80,
// the input buffer must not cross a page boundary and must not overlap with
// program ram pages!  GordonOS: extended to 127 bytes ($7F) since the
// old line-editing loop is replaced by kEditLine.

//ibuffs            = irq_vec+$14
.const ibuffs = vec_sv+$16
                              // start of input buffer after irq/nmi code
.const ibuffe = ibuffs+$7F// end of input buffer (127 bytes)

// ram_base PINNED to $8000 (section 3.7 Option 5 keeps program RAM at $8000-$A7FF).
// Relocatable code must reference program RAM at a FIXED address -- an
// origin-relative auto-compute would shift with the code and break fixups.
.const ram_base = $8000        // fallback start (strtab default; runtime B overrides)

.const stack_floor = 16        // bytes left free on stack for background interrupts

// this start can be changed to suit your system
// (GordonOS: no .pc here -- the address is inherited from the
//  importing basic.asm, so the I/O layer, entry, interpreter,
//  and line editor form one contiguous dynamic-task image.)

// basic cold start entry point

// new page 2 initialisation, copy block to ccflag on

lab_cold:
      ldy   #pg2_tabe-pg2_tabs-1
                              // byte count-1
lab_2d13:
      lda   pg2_tabs,y        // get byte
      sta   ccflag,y          // store in page 2
      dey                     // decrement count
      bpl   lab_2d13          // loop if not done

      ldx   #$ff              // set byte
      stx   clineh            // set current line high byte (set immediate mode)
      txs                     // reset stack pointer

      lda   #$4c              // code for jmp
      sta   fnxjmp            // save for jump vector for functions

// copy block from lab_2cee to $00bc - $00c4 (two 3-byte jmp trampolines)

      ldx   #strtab-lab_2cee  // set byte count
lab_2d4e:
      lda   lab_2cee-1,x      // get byte from table
      sta   lab_igby-1,x      // save byte in page zero
      dex                     // decrement count
      bne   lab_2d4e          // loop if not all done

// copy block from strtab to $0000 - $0012

lab_gmem:
      ldx   #endtab-strtab-1  // set byte count-1
tabloop:
      lda   strtab,x          // get byte from table
      sta   plus_0,x          // save byte in page zero
      dex                     // decrement count
      cpx   #$03              // stop before $0000-$0003 (6510 ports!)
      bne   tabloop           // loop if not all done

// set-up start values

      lda   #$00              // clear a
      sta   fac1_o            // clear fac1 overflow byte
      sta   last_sh           // clear descriptor stack top item pointer high byte

      lda   #$0e              // set default tab size
      sta   tabsiz            // save it
      lda   #$03              // set garbage collect step size for descriptor stack
      sta   g_step            // save it
      ldx   #des_sk           // descriptor stack start
      stx   next_s            // set descriptor stack pointer
      jsr   lab_crlf          // print cr/lf
                              // GordonOS REU program RAM: the
                              // working file IS the program RAM -- no C64
                              // RAM scan. emem = $FFFF, sstor = $FFFF,
                              // B = file offset 0 (pre-text byte), smem = 1.
      lda   #$ff              // set end of mem
      sta   ememl
      sta   ememh
      sta   sstorl            // bottom of string space = end of mem
      sta   sstorh
      ldy   #$00              // B_lo = 0
      sty   smeml
      sty   smemh             // smem = 0 (B = file offset 0)

      lda   #$00              // wrapper base = B (file offset 0)
      sta   ut1_pl
      sta   ut1_ph
      tya                     // A = 0
      jsr   pokeVarByte       // clear the pre-text byte at file offset 0
      inc   smeml             // smem = 1 (program text starts here)

      jsr   lab_crlf          // print cr/lf
      jsr   lab_1463          // do "new" and "clear"
      lda   ememl             // get end of mem low byte
      sec                     // set carry for subtract
      sbc   smeml             // subtract start of mem low byte
      tax                     // copy to x
      lda   ememh             // get end of mem high byte
      sbc   smemh             // subtract start of mem high byte
      jsr   lab_295e          // print xa as unsigned integer (bytes free)
      lda   #<lab_smsg        // point to sign-on message (low addr)
      ldy   #>lab_smsg        // point to sign-on message (high addr)
      jsr   lab_18c3          // print null terminated string from memory
      lda   #<lab_1274        // warm start vector low byte
      ldy   #>lab_1274        // warm start vector high byte
      sta   wrmjpl            // save warm start vector low byte
      sty   wrmjph            // save warm start vector high byte
      jmp   (wrmjpl)          // go do warm start

// open up space in memory
// move (ostrtl)-(obendl) to new block ending at (nbendl)

// nbendl,nbendh - new block end address (a/y)
// obendl,obendh - old block end address
// ostrtl,ostrth - old block start address

// returns with ..

// nbendl,nbendh - new block start address (high byte - $100)
// obendl,obendh - old block start address (high byte - $100)
// ostrtl,ostrth - old block start address (unchanged)

copyBlock:
      jsr   lab_121f          // check available memory, "out of memory" error if no room
                              // addr to check is in ay (low/high)
      sta   earryl            // save new array mem end low byte
      sty   earryh            // save new array mem end high byte

// open up space in memory
// move (ostrtl)-(obendl) to new block ending at (nbendl)
// don't set array end

copyBlockNC:
      lda   #0
      sta   $cc               // lnLenZ: uses fioBuf; invalidate line cache
      lda   ut1_pl; pha       // preserve ut1/ut2 (GC callers use them after)
      lda   ut1_ph; pha
      lda   ut2_pl; pha
      lda   ut2_ph; pha
      // fioLen = obend - ostrt (block length, 16-bit)
      sec
      lda   obendl
      sbc   ostrtl
      sta   fioLen
      lda   obendh
      sbc   ostrth
      sta   fioLen+1
      // save ostrt (final obendl) and compute newStart = nbend - len
      lda   ostrtl; sta fioScr+2
      lda   ostrth; sta fioScr+3
      sec
      lda   nbendl
      sbc   fioLen
      sta   fioScr
      lda   nbendh
      sbc   fioLen+1
      sta   fioScr+1
      lda   fioLen
      ora   fioLen+1
      bne   fCbGo
      jmp   fCbDone
fCbGo:
      lda   fioLen+1
      bne   fCb256
      lda   fioLen
      bne   fCbSmall
      jmp   fCbDone
fCbSmall:
      sta   fioChunk
      lda   #0
      sta   fioChunk+1
      sta   fioLen
      jmp   fCbCopy
fCb256:
      lda   #0; sta fioChunk
      lda   #1; sta fioChunk+1
      lda   fioLen+1
      sec; sbc #1
      sta   fioLen+1
fCbCopy:
      // ut1 = obendl - chunk; ut2 = nbendl - chunk (backward)
      sec
      lda   obendl
      sbc   fioChunk
      sta   ut1_pl
      lda   obendh
      sbc   fioChunk+1
      sta   ut1_ph
      sec
      lda   nbendl
      sbc   fioChunk
      sta   ut2_pl
      lda   nbendh
      sbc   fioChunk+1
      sta   ut2_ph
      jsr   fioChunkRd
      jsr   fioChunkWr
      lda   ut1_pl; sta obendl
      lda   ut1_ph; sta obendh
      lda   ut2_pl; sta nbendl
      lda   ut2_ph; sta nbendh
      jmp   fCbGo
fCbDone:
      // obendl = ostrt, nbendl = newStart (hi bytes - 1, per contract)
      lda   fioScr+2; sta obendl
      lda   fioScr+3; sta obendh
      sec
      lda   obendh; sbc #1; sta obendh
      lda   fioScr; sta nbendl
      lda   fioScr+1; sta nbendh
      sec
      lda   nbendh; sbc #1; sta nbendh
      pla; sta ut2_ph
      pla; sta ut2_pl
      pla; sta ut1_ph
      pla; sta ut1_pl
      rts

// check room on stack for a bytes
// stack too deep? do om error

lab_1212:
// *** patch - additional stack floor protection for background interrupts
// *** add
      // .if   stack_floor
      clc                     // prep adc
      adc   #stack_floor      // stack pointer lower limit before interrupts
      // .endif
// *** end patch
      sta   tempb             // save result in temp byte
      tsx                     // copy stack
      cpx   tempb             // compare new "limit" with stack
      bcc   lab_omer          // if stack < limit do "out of memory" error then warm start

      rts

// check available memory, "out of memory" error if no room
// addr to check is in ay (low/high)

lab_121f:
      cpy   sstorh            // compare bottom of string mem high byte
      bcc   lab_124b          // if less then exit (is ok)

      bne   lab_1229          // skip next test if greater (tested <)

                              // high byte was =, now do low byte
      cmp   sstorl            // compare with bottom of string mem low byte
      bcc   lab_124b          // if less then exit (is ok)

                              // addr is > string storage ptr (oops!)
lab_1229:
      pha                     // push addr low byte
      ldx   #$08              // set index to save adatal to expneg inclusive
      tya                     // copy addr high byte (to push on stack)

                              // save misc numeric work area
lab_122d:
      pha                     // push byte
      lda   adatal-1,x        // get byte from adatal to expneg ( ,$00 not pushed)
      dex                     // decrement index
      bpl   lab_122d          // loop until all done

      jsr   lab_garb          // garbage collection routine

                              // restore misc numeric work area
      ldx   #$00              // clear the index to restore bytes
lab_1238:
      pla                     // pop byte
      sta   adatal,x          // save byte to adatal to expneg
      inx                     // increment index
      cpx   #$08              // compare with end + 1
      bmi   lab_1238          // loop if more to do

      pla                     // pop addr high byte
      tay                     // copy back to y
      pla                     // pop addr low byte
      cpy   sstorh            // compare bottom of string mem high byte
      bcc   lab_124b          // if less then exit (is ok)

      bne   lab_omer          // if greater do "out of memory" error then warm start

                              // high byte was =, now do low byte
      cmp   sstorl            // compare with bottom of string mem low byte
      bcs   lab_omer          // if >= do "out of memory" error then warm start

                              // ok exit, carry clear
lab_124b:
      rts

// do "out of memory" error then warm start

lab_omer:
      ldx   #$0c              // error code $0c ("out of memory" error)

// do error #x, then warm start

lab_xerr:
      jsr   lab_crlf          // print cr/lf

      lda   lab_baer,x        // get error message pointer low byte
      ldy   lab_baer+1,x      // get error message pointer high byte
      jsr   lab_18c3          // print null terminated string from memory

      jsr   lab_1491          // flush stack and clear continue flag
      lda   #<lab_emsg        // point to " error" low addr
      ldy   #>lab_emsg        // point to " error" high addr
lab_1269:
      jsr   lab_18c3          // print null terminated string from memory
      ldy   clineh            // get current line high byte
      iny                     // increment it
      beq   lab_1274          // go do warm start (was immediate mode)

                              // else print line number
      jsr   lab_2953          // print " in line [line #]"

// basic warm start entry point
// wait for basic command

lab_1274:
      lda   #<lab_rmsg        // point to "ready" message low byte
      ldy   #>lab_rmsg        // point to "ready" message high byte

      jsr   lab_18c3          // go do print string

// wait for basic command (no "ready")

lab_127d:
      jsr   lab_1357          // call for basic input
lab_1280:
      stx   bpntrl            // set basic execute pointer low byte
      sty   bpntrh            // set basic execute pointer high byte
      lda   #$01              // text pointer is now C64 memory (ibuffs)
      sta   textDom
      jsr   lab_gbyt          // scan memory
      beq   lab_127d          // loop while null

// got to interpret input line now ..

      ldx   #$ff              // current line to null value
      stx   clineh            // set current line high byte
      bcc   lab_1295          // branch if numeric character (handle new basic line)

                              // no line number .. immediate mode
      jsr   lab_13a6          // crunch keywords into basic tokens
      jmp   lab_15f6          // go scan and interpret code

// handle new basic line

lab_1295:
      jsr   lab_gfpn          // get fixed-point number into temp integer
      jsr   lab_13a6          // crunch keywords into basic tokens
      sty   ibptr             // save index pointer to end of crunched line
      jsr   lab_ssln          // search basic for temp integer line number
      bcc   lab_12e6          // branch if not found

                              // aroooogah! line # already exists! delete it
      lda   baslnl            // wrapper base = found line
      sta   ut1_pl
      lda   baslnh
      sta   ut1_ph
      ldy   #$01              // set index to next line pointer high byte
      jsr   peekVarByte       // get next line pointer high byte
      sta   ut1_ph            // save it
      lda   svarl             // get start of vars low byte
      sta   ut1_pl            // save it
      lda   baslnh            // get found line pointer high byte
      sta   ut2_ph            // save it
      lda   baslnl            // get found line pointer low byte
      dey                     // decrement index
      sta   varPtr            // stash pointer low (also wrapper base lo)
      lda   baslnh            // wrapper base high = line pointer
      sta   varPtr+1
      jsr   peekVarByte2      // get next line pointer low byte
      sta   tempb             // stash it (HEAD: baslnl - nextLineLo!)
      lda   baslnl            // A = found line pointer low
      sbc   tempb             // subtract next line pointer low byte (C=1
                              //   from lab_ssln's found path, untouched)
      clc                     // clear carry for add
      adc   svarl             // add start of vars low byte
      sta   svarl             // save new start of vars low byte
      sta   ut2_pl            // save destination pointer low byte
      lda   svarh             // get start of vars high byte
      adc   #$ff              // -1 + carry
      sta   svarh             // save start of vars high byte
      sbc   baslnh            // subtract found line pointer high byte
      tax                     // copy to block count
      sec                     // set carry for subtract
      lda   baslnl            // get found line pointer low byte
      sbc   svarl             // subtract start of vars low byte
      tay                     // copy to bytes in first block count
      bcs   lab_12d0          // branch if overflow

      inx                     // increment block count (correct for =0 loop exit)
      dec   ut2_ph            // decrement destination high byte
lab_12d0:
      clc                     // clear carry for add
      adc   ut1_pl            // add source pointer low byte
      bcc   lab_12d8          // branch if no overflow

      dec   ut1_ph            // else decrement source pointer high byte
      clc                     // clear carry

                              // close up memory to delete old line
lab_12d8:
      jsr   copyBlockUp       // forward copy (ut1)->(ut2), X pages + Y bytes

                              // got new line in buffer and no existing same #
lab_12e6:
      lda   ibuffs            // get byte from start of input buffer
      beq   lab_1319          // if null line just go flush stack/vars and exit

                              // got new line and it isn't empty line
      lda   ememl             // get end of mem low byte
      ldy   ememh             // get end of mem high byte
      sta   sstorl            // set bottom of string space low byte
      sty   sstorh            // set bottom of string space high byte
      lda   svarl             // get start of vars low byte  (end of basic)
      sta   obendl            // save old block end low byte
      ldy   svarh             // get start of vars high byte (end of basic)
      sty   obendh            // save old block end high byte
      adc   ibptr             // add input buffer pointer    (also buffer length)
      bcc   lab_1301          // branch if no overflow from add

      iny                     // else increment high byte
lab_1301:
      sta   nbendl            // save new block end low byte (move to, low byte)
      sty   nbendh            // save new block end high byte
      jsr   copyBlock        // open up space in memory
                              // old start pointer ostrtl,ostrth set by the find line call
      lda   earryl            // get array mem end low byte
      ldy   earryh            // get array mem end high byte
      sta   svarl             // save start of vars low byte
      sty   svarh             // save start of vars high byte
      ldy   ibptr             // get input buffer pointer    (also buffer length)
      dey                     // adjust for loop type
      lda   baslnl            // wrapper base = line destination
      sta   ut1_pl
      lda   baslnh
      sta   ut1_ph
lab_1311:
      lda   ibuffs-4,y        // get byte from crunched line
      jsr   pokeVarByte       // save it to program memory
      dey                     // decrement count
      cpy   #$03              // compare with first byte-1
      bne   lab_1311          // continue while count <> 3

      lda   itemph            // get line # high byte
      jsr   pokeVarByte       // save it to program memory
      dey                     // decrement count
      lda   itempl            // get line # low byte
      jsr   pokeVarByte       // save it to program memory
      dey                     // decrement count
      lda   #$ff              // set byte to allow chain rebuild. if you didn't set this
                              // byte then a zero already here would stop the chain rebuild
                              // as it would think it was the [eot] marker.
      jsr   pokeVarByte       // save it to program memory

lab_1319:
      jsr   lab_1477          // reset execution to start, clear vars and flush stack
      ldx   smeml             // get start of mem low byte
      lda   smemh             // get start of mem high byte
      ldy   #$01              // index to high byte of next line pointer
lab_1325:
      stx   ut1_pl            // set line start pointer low byte
      sta   ut1_ph            // set line start pointer high byte
      jsr   peekVarByte       // get next line pointer high byte (Y=1)
      sta   tempb             // stash it
      dey                     // decrement index to low byte
      jsr   peekVarByte       // get next line pointer low byte (Y=0)
      ora   tempb             // OR low and high (Z set if link == 0)
      beq   lab_133e          // exit if end of program

// rebuild chaining of basic lines

      ldy   #$04              // point to first code byte of line
                              // there is always 1 byte + [eol] as null entries are deleted
lab_1330:
      iny                     // next code byte
      jsr   peekVarByte       // get byte
      bne   lab_1330          // loop if not [eol]

      sec                     // set carry for add + 1
      tya                     // copy end index
      adc   ut1_pl            // add to line start pointer low byte
      tax                     // copy to x
      ldy   #$00              // clear index, point to this line's next line pointer
      jsr   pokeVarByte       // set next line pointer low byte
      tya                     // clear a
      adc   ut1_ph            // add line start pointer high byte + carry
      iny                     // increment index to high byte
      jsr   pokeVarByte       // save next line pointer low byte
      bcc   lab_1325          // go do next line, branch always, carry clear


lab_133e:
      jmp   lab_127d          // else we just wait for basic command, no "ready"

// print "? " and get basic input

lab_inln:
      jsr   lab_18e3          // print "?" character
      jsr   lab_18e0          // print " "
                              // fall through -- no "? " on retry

// receive line from keyboard
// call for basic input (main entry point) -- GordonOS: patched to use kEditLine

lab_1357:
      jsr   vecInln           // GordonOS: kEditLine replaces char loop
      cpy   #0                // empty? (not focused or no input)
      beq   lab_1357_yield    // yes -> yield and retry
      jsr   lab_crlf          // print CR/LF (clobbers X,Y via kPrintChar)
      ldx   #<ibuffs          // set x to buffer start low byte
      ldy   #>ibuffs          // set y to buffer start high byte
      rts                     // return -- main loop falls into lab_1280; INPUT resumes parsing
lab_1357_yield:
      lda   #SYS_YIELD        // syscall yield
      jsr   syscall
      jmp   lab_1357          // retry

// crunch keywords into basic tokens
// position independent buffer version ..
// faster, dictionary search version ....

lab_13a6:
      ldy   #$ff              // set save index (makes for easy math later)

      sec                     // set carry for subtract
      lda   bpntrl            // get basic execute pointer low byte
      sbc   #<ibuffs          // subtract input buffer start pointer
      tax                     // copy result to x (index past line # if any)

      stx   oquote            // clear open quote/data flag
lab_13ac:
      lda   ibuffs,x          // get byte from input buffer
      beq   lab_13ec          // if null save byte then exit

      cmp   #'{'              // compare with "{" (lowercase 'z'+1)
      bcs   lab_13ec          // if >= go save byte then continue crunching

      cmp   #'<'              // compare with "<"
      bcs   lab_13cc          // if >= go crunch now

      cmp   #'0'              // compare with "0"
      bcs   lab_13ec          // if >= go save byte then continue crunching

      sta   scnquo            // save buffer byte as search character
      cmp   #$22              // is it quote character?
      beq   lab_1410          // branch if so (copy quoted string)

      cmp   #'*'              // compare with "*"
      bcc   lab_13ec          // if < go save byte then continue crunching

                              // else crunch now
lab_13cc:
      bit   oquote            // get open quote/data token flag
      bvs   lab_13ec          // branch if b6 of oquote set (was data)
                              // go save byte then continue crunching

      stx   tempb             // save buffer read index
      sty   csidx             // copy buffer save index
      ldy   #<tab_1stc        // get keyword first character table low address
      sty   ut2_pl            // save pointer low byte
      ldy   #>tab_1stc        // get keyword first character table high address
      sty   ut2_ph            // save pointer high byte
      ldy   #$00              // clear table pointer

lab_13d0:
      cmp   (ut2_pl),y        // compare with keyword first character table byte
      beq   lab_13d1          // go do word_table_chr if match

      bcc   lab_13ea          // if < keyword first character table byte go restore
                              // y and save to crunched

      iny                     // else increment pointer
      bne   lab_13d0          // and loop (branch always)

// have matched first character of some keyword

lab_13d1:
      tya                     // copy matching index
      asl                     // *2 (bytes per pointer)
      tax                     // copy to new index
      lda   tab_chrt,x        // get keyword table pointer low byte
      sta   ut2_pl            // save pointer low byte
      lda   tab_chrt+1,x      // get keyword table pointer high byte
      sta   ut2_ph            // save pointer high byte

      ldy   #$ff              // clear table pointer (make -1 for start)

      ldx   tempb             // restore buffer read index

lab_13d6:
      iny                     // next table byte
      lda   (ut2_pl),y        // get byte from table
lab_13d8:
      bmi   lab_13ea          // all bytes matched so go save token

      inx                     // next buffer byte
      cmp   ibuffs,x          // compare with byte from input buffer
      beq   lab_13d6          // go compare next if match

      bne   lab_1417          // branch if >< (not found keyword)

lab_13ea:
      ldy   csidx             // restore save index

                              // save crunched to output
lab_13ec:
      inx                     // increment buffer index (to next input byte)
      iny                     // increment save index (to next output byte)
      sta   ibuffs,y          // save byte to output
      cmp   #$00              // set the flags, set carry
      beq   lab_142a          // do exit if was null [eol]

                              // a holds token or byte here
      sbc   #':'              // subtract ":" (carry set by cmp #00)
      beq   lab_13ff          // branch if it was ":" (is now $00)

                              // a now holds token-$3a
      cmp   #tk_data-$3a      // compare with data token - $3a
      bne   lab_1401          // branch if not data

                              // token was : or data
lab_13ff:
      sta   oquote            // save token-$3a (clear for ":", tk_data-$3a for data)
lab_1401:
      eor   #tk_rem-$3a       // effectively subtract rem token offset
      bne   lab_13ac          // if wasn't rem then go crunch rest of line

      sta   asrch             // else was rem so set search for [eol]

                              // loop for rem, "..." etc.
lab_1408:
      lda   ibuffs,x          // get byte from input buffer
      beq   lab_13ec          // branch if null [eol]

      cmp   asrch             // compare with stored character
      beq   lab_13ec          // branch if match (end quote)

                              // entry for copy string in quotes, don't crunch
lab_1410:
      iny                     // increment buffer save index
      sta   ibuffs,y          // save byte to output
      inx                     // increment buffer read index
      bne   lab_1408          // loop while <> 0 (should never be 0!)

                              // not found keyword this go
lab_1417:
      ldx   tempb             // compare has failed, restore buffer index (start byte!)

                              // now find the end of this word in the table
lab_141b:
      lda   (ut2_pl),y        // get table byte
      php                     // save status
      iny                     // increment table index
      plp                     // restore byte status
      bpl   lab_141b          // if not end of keyword go do next

      lda   (ut2_pl),y        // get byte from keyword table
      bne   lab_13d8          // go test next word if not zero byte (end of table)

                              // reached end of table with no match
      lda   ibuffs,x          // restore byte from input buffer
      bpl   lab_13ea          // branch always (all bytes in buffer are $00-$7f)
                              // go save byte in output and continue crunching

                              // reached [eol]
lab_142a:
      iny                     // increment pointer
      iny                     // increment pointer (makes it next line pointer high byte)
      sta   ibuffs,y          // save [eol] (marks [eot] in immediate mode)
      iny                     // adjust for line copy
      iny                     // adjust for line copy
      iny                     // adjust for line copy
// *** begin patch for when ibuffs is $xx00 - daryl rictor ***
// *** insert
      // .if   ibuffs&$ff==0
      lda   bpntrl            // test for $00
      bne   lab_142p          // not $00
      dec   bpntrh            // allow for increment when $xx00
lab_142p:
      // .endif
// *** end   patch for when ibuffs is $xx00 - daryl rictor ***
// end of patch
      dec   bpntrl            // allow for increment
      rts

// search basic for temp integer line number from start of mem

lab_ssln:
      lda   smeml             // get start of mem low byte
      ldx   smemh             // get start of mem high byte

// search basic for temp integer line number from ax
// returns carry set if found
// returns baslnl/baslnh pointer to found or next higher (not found) line

// old 541 new 507

lab_shln:
      ldy   #$01              // set index
      sta   baslnl            // save low byte as current
      stx   baslnh            // save high byte as current
      sta   ut1_pl            // wrapper base mirror
      stx   ut1_ph
      jsr   peekVarByte       // get pointer high byte from addr
      sta   tempb
      dey
      jsr   peekVarByte       // get pointer low byte from addr
      ora   tempb
      beq   lab_145f          // pointer was zero so we're done, do 'not found' exit

      ldy   #$03              // set index to line # high byte
      jsr   peekVarByte       // get line # high byte
      dey                     // decrement index (point to low byte)
      cmp   itemph            // compare with temporary integer high byte
      bne   lab_1455          // if <> skip low byte check

      jsr   peekVarByte       // get line # low byte
      cmp   itempl            // compare with temporary integer low byte
lab_1455:
      bcs   lab_145e          // else if temp < this line, exit (passed line#)

lab_1456:
      dey                     // decrement index to next line ptr high byte
      jsr   peekVarByte       // get next line pointer high byte
      tax                     // copy to x
      dey                     // decrement index to next line ptr low byte
      jsr   peekVarByte       // get next line pointer low byte
      bcc   lab_shln          // go search for line # in temp (itempl/itemph) from ax
                              // (carry always clear)

lab_145e:
      beq   lab_1460          // exit if temp = found line #, carry is set

lab_145f:
      clc                     // clear found flag
lab_1460:
      rts

// perform new

lab_new:
      bne   lab_1460          // exit if not end of statement (to do syntax error)

lab_1463:
      lda   smeml             // fillBlock dst = start of mem
      sta   ut1_pl
      lda   smemh
      sta   ut1_ph
      lda   #$00              // clear byte
      ldx   #$02              // 2 bytes (next line pointer)
      ldy   #$00
      jsr   fillBlock         // clear first line, next line pointer
      clc                     // clear carry
      lda   smeml             // get start of mem low byte
      adc   #$02              // calculate end of basic low byte
      sta   svarl             // save start of vars low byte
      lda   smemh             // get start of mem high byte
      adc   #$00              // add any carry
      sta   svarh             // save start of vars high byte

// reset execution to start, clear vars and flush stack

lab_1477:
      clc                     // clear carry
      lda   smeml             // get start of mem low byte
      adc   #$ff              // -1
      sta   bpntrl            // save basic execute pointer low byte
      lda   smemh             // get start of mem high byte
      adc   #$ff              // -1+carry
      sta   bpntrh            // save basic execute pointer high byte
      lda   #$00              // text pointer is now a REU file offset
      sta   textDom

// "clear" command gets here

lab_147a:
      lda   ememl             // get end of mem low byte
      ldy   ememh             // get end of mem high byte
      sta   sstorl            // set bottom of string space low byte
      sty   sstorh            // set bottom of string space high byte
      lda   svarl             // get start of vars low byte
      ldy   svarh             // get start of vars high byte
      sta   sarryl            // save var mem end low byte
      sty   sarryh            // save var mem end high byte
      sta   earryl            // save array mem end low byte
      sty   earryh            // save array mem end high byte
      jsr   lab_161a          // perform restore command

// flush stack and clear continue flag

lab_1491:
      ldx   #des_sk           // set descriptor stack pointer
      stx   next_s            // save descriptor stack pointer
      pla                     // pull return address low byte
      tax                     // copy return address low byte
      pla                     // pull return address high byte
      stx   lab_skfe          // save to cleared stack
      sta   lab_skff          // save to cleared stack
      ldx   #$fd              // new stack pointer
      txs                     // reset stack
      lda   #$00              // clear byte
//*** fix p2: not needed -- the continue pointer is saved anyway
//      sta   cpntrh            // clear continue pointer high byte
      sta   sufnxf            // clear subscript/fnx flag
lab_14a6:
      rts

// perform clear

lab_clear:
      beq   lab_147a          // if no following token go do "clear"

                              // else there was a following token (go do syntax error)
      rts

// perform list [n][-m]
// bigger, faster version (a _lot_ faster)

lab_list:
      bcc   lab_14bd          // branch if next character numeric (list n..)

      beq   lab_14bd          // branch if next character [null] (list)

      cmp   #tk_minus         // compare with token for -
      bne   lab_14a6          // exit if not - (list -m)

                              // list [[n][-m]]
                              // this bit sets the n , if present, as the start and end
lab_14bd:
      jsr   lab_gfpn          // get fixed-point number into temp integer
      jsr   lab_ssln          // search basic for temp integer line number
                              // (pointer in baslnl/baslnh)
      jsr   lab_gbyt          // scan memory
      beq   lab_14d4          // branch if no more characters

                              // this bit checks the - is present
      cmp   #tk_minus         // compare with token for -
      bne   lab_1460          // return if not "-" (will be syntax error)

                              // list [n]-m
                              // the - was there so set m as the end value
      jsr   lab_igby          // increment and scan memory
      jsr   lab_gfpn          // get fixed-point number into temp integer
      beq   lab_14d4          // ok -> continue (short forward)
      jmp   lab_1460          // not ok -> exit (far)

lab_14d4:
      lda   itempl            // get temporary integer low byte
      ora   itemph            // or temporary integer high byte
      bne   lab_14e2          // branch if start set

      lda   #$ff              // set for -1
      sta   itempl            // set temporary integer low byte
      sta   itemph            // set temporary integer high byte
lab_14e2:
      lda   baslnl            // wrapper base mirror
      sta   ut1_pl
      lda   baslnh
      sta   ut1_ph
      ldy   #$01              // set index for line
      sty   oquote            // clear open quote flag
      jsr   lab_crlf          // print cr/lf
      jsr   peekVarByte       // get next line pointer high byte
                              // pointer initially set by search at lab_14bd
      sta   tempb
      dey
      jsr   peekVarByte       // get next line pointer low byte
      ora   tempb
      beq   lab_152b          // if null all done so exit
      ldy   #$01
      jsr   lab_1629          // do crtl-c check vector

      iny                     // increment index for line
      jsr   peekVarByte       // get line # low byte
      tax                     // copy to x
      iny                     // increment index
      jsr   peekVarByte       // get line # high byte
      cmp   itemph            // compare with temporary integer high byte
      bne   lab_14ff          // branch if no high byte match

      cpx   itempl            // compare with temporary integer low byte
      beq   lab_1501          // branch if = last line to do (< will pass next branch)

lab_14ff:                      // else ..
      bcs   lab_152b          // if greater all done so exit

lab_1501:
      sty   tidx1             // save index for line
      jsr   lab_295e          // print xa as unsigned integer
      lda   baslnl            // re-point wrapper base (lab_295e -> lab_18c3 ->
      sta   ut1_pl            // lab_20b4/lab_22b6 clobber ut1 -- Bug: LIST
      lda   baslnh            // read string space instead of the line)
      sta   ut1_ph
      lda   #$20              // space is the next character
lab_1508:
      ldy   tidx1             // get index for line
      and   #$7f              // mask top out bit of character
lab_150c:
      jsr   lab_prna          // go print the character
      cmp   #$22              // was it " character
      bne   lab_1519          // branch if not

                              // we are either entering or leaving a pair of quotes
      lda   oquote            // get open quote flag
      eor   #$ff              // toggle it
      sta   oquote            // save it back
lab_1519:
      iny                     // increment index
      jsr   peekVarByte       // get next byte
      bne   lab_152e          // branch if not [eol] (go print character)
      tay                     // else clear index
      jsr   peekVarByte       // get next line pointer low byte
      tax                     // copy to x
      iny                     // increment index
      jsr   peekVarByte       // get next line pointer high byte
      sta   tempb
      txa
      ora   tempb
      beq   lab_152b          // both link bytes zero -> end of program
      lda   tempb
      stx   baslnl            // set pointer to line low byte
      sta   baslnh            // set pointer to line high byte
      jmp   lab_14e2          // go do next line
                              // else ..
lab_152b:
      rts

lab_152e:
      bpl   lab_150c          // just go print it if not token byte

                              // else was token byte so uncrunch it (maybe)
      bit   oquote            // test the open quote flag
      bmi   lab_150c          // just go print character if open quote set

      ldx   #>lab_keyt        // get table address high byte
      asl                     // *2
      asl                     // *4
      bcc   lab_152f          // branch if no carry

      inx                     // else increment high byte
      clc                     // clear carry for add
lab_152f:
      adc   #<lab_keyt        // add low byte
      bcc   lab_1530          // branch if no carry

      inx                     // else increment high byte
lab_1530:
      sta   ut2_pl            // save table pointer low byte
      stx   ut2_ph            // save table pointer high byte
      sty   tidx1             // save index for line
      ldy   #$00              // clear index
      lda   (ut2_pl),y        // get length
      tax                     // copy length
      iny                     // increment index
      lda   (ut2_pl),y        // get 1st character
      dex                     // decrement length
      beq   lab_1508          // if no more characters exit and print

      jsr   lab_prna          // go print the character
      iny                     // increment index
      lda   (ut2_pl),y        // get keyword address low byte
      pha                     // save it for now
      iny                     // increment index
      lda   (ut2_pl),y        // get keyword address high byte
      ldy   #$00
      sta   ut2_ph            // save keyword pointer high byte
      pla                     // pull low byte
      sta   ut2_pl            // save keyword pointer low byte
lab_1540:
      lda   (ut2_pl),y        // get character
      dex                     // decrement character count
      beq   lab_1508          // if last character exit and print

      jsr   lab_prna          // go print the character
      iny                     // increment index
      bne   lab_1540          // loop for next character

// perform for

lab_for:
      lda   #$80              // set fnx
      sta   sufnxf            // set subscript/fnx flag
      jsr   lab_let           // go do let
      pla                     // pull return address
      pla                     // pull return address
      lda   #$10              // we need 16d bytes !
      jsr   lab_1212          // check room on stack for a bytes
      jsr   lab_snbs          // scan for next basic statement ([:] or [eol])
      clc                     // clear carry for add
      tya                     // copy index to a
      adc   bpntrl            // add basic execute pointer low byte
      pha                     // push onto stack
      lda   bpntrh            // get basic execute pointer high byte
      adc   #$00              // add carry
      pha                     // push onto stack
      lda   clineh            // get current line high byte
      pha                     // push onto stack
      lda   clinel            // get current line low byte
      pha                     // push onto stack
      lda   #tk_to            // get "to" token
      jsr   lab_scca          // scan for chr$(a) , else do syntax error then warm start
      jsr   lab_ctnm          // check if source is numeric, else do type mismatch
      jsr   lab_evnm          // evaluate expression and check is numeric,
                              // else do type mismatch
// *** begin patch  2.22p5.1   to expression may get sign bit flipped
// *** add
      jsr   lab_27ba          // round fac1
// *** end   patch  2.22p5.1   to expression may get sign bit flipped
      lda   fac1_s            // get fac1 sign (b7)
      ora   #$7f              // set all non sign bits
      and   fac1_1            // and fac1 mantissa1
      sta   fac1_1            // save fac1 mantissa1
      lda   #<lab_159f        // set return address low byte
      ldy   #>lab_159f        // set return address high byte
      sta   ut1_pl            // save return address low byte
      sty   ut1_ph            // save return address high byte
      jmp   lab_1b66          // round fac1 and put on stack (returns to next instruction)

lab_159f:
      lda   #<lab_259c        // set 1 pointer low addr (default step size)
      ldy   #>lab_259c        // set 1 pointer high addr
      jsr   lab_ufac          // unpack memory (ay) into fac1
      jsr   lab_gbyt          // scan memory
      cmp   #tk_step          // compare with step token
      bne   lab_15b3          // jump if not "step"

                              //.was step so ..
      jsr   lab_igby          // increment and scan memory
      jsr   lab_evnm          // evaluate expression and check is numeric,
                              // else do type mismatch
lab_15b3:
      jsr   lab_27ca          // return a=ff,c=1/-ve a=01,c=0/+ve
      sta   fac1_s            // set fac1 sign (b7)
                              // this is +1 for +ve step and -1 for -ve step, in next we
                              // compare the for value and the to value and return +1 if
                              // for > to, 0 if for = to and -1 if for < to. the value
                              // here (+/-1) is then compared to that result and if they
                              // are the same (+ve and for > to or -ve and for < to) then
                              // the loop is done
                             
// *** begin patch  2.22p5.3   potential return address -$100 (page not incremented) ***
// *** add
   // .if [* & $ff] == $fd
      nop                     // return address of jsr +1 (on  next page)
   // .endif  
// *** end   patch  2.22p5.3   potential return address -$100 (page not incremented) ***
      jsr   lab_1b5b          // push sign, round fac1 and put on stack
      lda   frnxth            // get var pointer for for/next high byte
      pha                     // push on stack
      lda   frnxtl            // get var pointer for for/next low byte
      pha                     // push on stack
      lda   #tk_for           // get for token
      pha                     // push on stack

// interpreter inner loop

lab_15c2:
      jsr   lab_1629          // do crtl-c check vector
      lda   bpntrl            // get basic execute pointer low byte
      ldy   bpntrh            // get basic execute pointer high byte

      ldx   clineh            // continue line is $ffxx for immediate mode
                              // ($00xx for run from immediate mode)
      inx                     // increment it (now $00 if immediate mode)
//*** fix p2: not needed -- the continue pointer is saved anyway
//      beq   lab_15d1          // branch if null (immediate mode)

      sta   cpntrl            // save continue pointer low byte
      sty   cpntrh            // save continue pointer high byte
      lda   textDom           // save the pointer domain with it
      sta   contDom
lab_15d1:
      ldy   #$00              // clear index
      jsr   textByteAtY       // get next byte
      beq   lab_15dc          // branch if null [eol]

      cmp   #':'              // compare with ":"
      beq   lab_15f6          // branch if = (statement separator)

lab_15d9:
      jmp   lab_sner          // else syntax error then warm start

                              // have reached [eol]
lab_15dc:
      lda   textDom           // 1 = C64 ibuffs (immediate mode), 0 = REU program
      beq   lab_15dcadv       // program mode -> advance to next line
      clc                     // immediate mode: no "break" message
      jmp   lab_1651          // go do warm start (immediate line has no next line)
lab_15dcadv:
      ldy   #$01              // set index to next line pointer low byte
      jsr   textByteAtY       // get next line pointer low byte
      sta   tempb             // stash it
      iny                     // increment index to high byte
      jsr   textByteAtY       // get next line pointer high byte
      ora   tempb             // OR low and high (Z set if link == 0)
      clc                     // clear carry for no "break" message
      beq   lab_1651          // if null go to immediate mode (was immediate or [eot]
                              // marker)

      iny                     // increment index
      jsr   textByteAtY       // get line # low byte
      sta   clinel            // save current line low byte
      iny                     // increment index
      jsr   textByteAtY       // get line # high byte
      sta   clineh            // save current line high byte
      tya                     // a now = 4
      clc                     // carry MUST be clear -- the REU reads above leak C
      adc   bpntrl            // add basic execute pointer low byte
      sta   bpntrl            // save basic execute pointer low byte
      bcc   lab_15f6          // branch if no overflow

      inc   bpntrh            // else increment basic execute pointer high byte
lab_15f6:
      jsr   lab_igby          // increment and scan memory

lab_15f9:
      jsr   lab_15ff          // go interpret basic code from (bpntrl)

lab_15fc:
      jmp   lab_15c2          // loop

// interpret basic code from (bpntrl)

lab_15ff:
      beq   lab_1628          // exit if zero [eol]

lab_1602:
      asl                     // *2 bytes per vector and normalise token
      bcs   lab_1609          // branch if was token

      jmp   lab_let           // else go do implied let

lab_1609:
      cmp   #[tk_tab-$80]*2   // compare normalised token * 2 with tab
      bcc   lab_160c          // primary token -> lab_ctbl dispatch
      cmp   #[tk_mode-$80]*2  // compare with first graphics token
      bcc   lab_15d9          // secondary token (tab..mode-1) -> syntax error
      sec                     // set carry for subtract
      sbc   #[tk_mode-$80]*2  // index = (token - tk_mode) * 2
      tay                     // copy to index
      lda   lab_gxctbl+1,y    // get graphics vector high byte
      pha                     // onto stack
      lda   lab_gxctbl,y      // get graphics vector low byte
      pha                     // onto stack
      jmp   lab_igby          // jump to increment and scan memory
                              // then "return" to vector
lab_160c:
      tay                     // copy to index
      lda   lab_ctbl+1,y      // get vector high byte
      pha                     // onto stack
      lda   lab_ctbl,y        // get vector low byte
      pha                     // onto stack
      jmp   lab_igby          // jump to increment and scan memory
                              // then "return" to vector

// ctrl-c check jump. this is called as a subroutine but exits back via a jump if a
// key press is detected.

lab_1629:
      jmp   (vec_cc)          // ctrl c check vector

// if there was a key press it gets back here ..

lab_1636:
      cmp   #$03              // compare with ctrl-c

// perform stop

lab_stop:
      bcs   lab_163b          // branch if token follows stop
                              // else just end
// end

lab_end:
      clc                     // clear the carry, indicate a normal program end
lab_163b:
      bne   lab_167a          // if wasn't ctrl-c or there is a following byte return

      lda   bpntrh            // get the basic execute pointer high byte
//*** fix p2: not needed -- the continue pointer is saved anyway
//      eor   #>ibuffs          // compare with buffer address high byte (cb unchanged)
//      beq   lab_164f          // branch if the basic pointer is in the input buffer
//                              // (can't continue in immediate mode)
//                              // else ..
//      eor   #>ibuffs          // correct the bits
      ldy   bpntrl            // get basic execute pointer low byte
      sty   cpntrl            // save continue pointer low byte
      sta   cpntrh            // save continue pointer high byte
      lda   textDom           // save the pointer domain with it
      sta   contDom
lab_1647:
      lda   clinel            // get current line low byte
      ldy   clineh            // get current line high byte
      sta   blinel            // save break line low byte
      sty   blineh            // save break line high byte
lab_164f:
      pla                     // pull return address low
      pla                     // pull return address high
lab_1651:
      bcc   lab_165e          // if was program end just do warm start

                              // else ..
      lda   #<lab_bmsg        // point to "break" low byte
      ldy   #>lab_bmsg        // point to "break" high byte
      jmp   lab_1269          // print "break" and do warm start

lab_165e:
      jmp   lab_1274          // go do warm start

// perform restore

lab_restore:
      bne   lab_restoren      // branch if next character not null (restore n)

lab_161a:
      sec                     // set carry for subtract
      lda   smeml             // get start of mem low byte
      sbc   #$01              // -1
      ldy   smemh             // get start of mem high byte
      bcs   lab_1624          // branch if no underflow

lab_uflow:
      dey                     // else decrement high byte
lab_1624:
      sta   dptrl             // save data pointer low byte
      sty   dptrh             // save data pointer high byte
lab_1628:
      rts

                              // is restore n
lab_restoren:
      jsr   lab_gfpn          // get fixed-point number into temp integer
      jsr   lab_snbl          // scan for next basic line
      lda   clineh            // get current line high byte
      cmp   itemph            // compare with temporary integer high byte
      bcs   lab_reset_search  // branch if >= (start search from beginning)

      tya                     // else copy line index to a
      sec                     // set carry (+1)
      adc   bpntrl            // add basic execute pointer low byte
      ldx   bpntrh            // get basic execute pointer high byte
      bcc   lab_go_search     // branch if no overflow to high byte

      inx                     // increment high byte
      bcs   lab_go_search     // branch always (can never be carry clear)

// search for line # in temp (itempl/itemph) from start of mem pointer (smeml)

lab_reset_search:
      lda   smeml             // get start of mem low byte
      ldx   smemh             // get start of mem high byte

// search for line # in temp (itempl/itemph) from (ax)

lab_go_search:
      jsr   lab_shln          // search basic for temp integer line number from ax
      bcs   lab_line_found    // if carry set go set pointer

      jmp   lab_16f7          // else go do "undefined statement" error

lab_line_found:
                              // carry already set for subtract
      lda   baslnl            // get pointer low byte
      sbc   #$01              // -1
      ldy   baslnh            // get pointer high byte
      bcs   lab_1624          // branch if no underflow (save data pointer and return)

      bcc   lab_uflow         // else decrement high byte then save data pointer and
                              // return (branch always)

lab_167a:
      rts

// perform cont

lab_cont:
      bne   lab_167a          // if following byte exit to do syntax error

      lda   contDom            // continue pointer domain (saved at fetch/stop)
      bne   lab_167c           // direct mode -> can't continue
      ldy   cpntrh            // get continue pointer high byte

                              // we can continue so ..
lab_166c:
      sty   bpntrh            // save basic execute pointer high byte
      lda   cpntrl            // get continue pointer low byte
      sta   bpntrl            // save basic execute pointer low byte
      lda   #$00              // continue pointer is a REU file offset
      sta   textDom
      lda   blinel            // get break line low byte
      ldy   blineh            // get break line high byte
      sta   clinel            // set current line low byte
      sty   clineh            // set current line high byte
      rts

lab_167c:
      ldx   #$1e              // error code $1e ("can't continue" error)
      jmp   lab_xerr          // do error #x, then warm start

// perform run

lab_run:
      bne   lab_1696          // branch if run n
      jmp   lab_1477          // reset execution to start, clear variables, flush stack and
                              // return

// does run n

lab_1696:
      jsr   lab_147a          // go do "clear"
      beq   lab_16b0          // get n and do goto n (branch always as clear sets z=1)

// perform do

lab_do:
      lda   #$05              // need 5 bytes for do
      jsr   lab_1212          // check room on stack for a bytes
      lda   bpntrh            // get basic execute pointer high byte
      pha                     // push on stack
      lda   bpntrl            // get basic execute pointer low byte
      pha                     // push on stack
      lda   clineh            // get current line high byte
      pha                     // push on stack
      lda   clinel            // get current line low byte
      pha                     // push on stack
      lda   #tk_do            // token for do
      pha                     // push on stack
      jsr   lab_gbyt          // scan memory
      jmp   lab_15c2          // go do interpreter inner loop

// perform gosub

lab_gosub:
      lda   #$05              // need 5 bytes for gosub
      jsr   lab_1212          // check room on stack for a bytes
      lda   bpntrh            // get basic execute pointer high byte
      pha                     // push on stack
      lda   bpntrl            // get basic execute pointer low byte
      pha                     // push on stack
      lda   clineh            // get current line high byte
      pha                     // push on stack
      lda   clinel            // get current line low byte
      pha                     // push on stack
      lda   #tk_gosub         // token for gosub
      pha                     // push on stack
lab_16b0:
      jsr   lab_gbyt          // scan memory
      jsr   lab_goto          // perform goto n
      jmp   lab_15c2          // go do interpreter inner loop
                              // (can't rts, we used the stack!)

// perform goto

lab_goto:
      jsr   lab_gfpn          // get fixed-point number into temp integer
      jsr   lab_snbl          // scan for next basic line
      lda   clineh            // get current line high byte
      cmp   itemph            // compare with temporary integer high byte
      bcs   lab_16d0          // branch if >= (start search from beginning)

      tya                     // else copy line index to a
      sec                     // set carry (+1)
      adc   bpntrl            // add basic execute pointer low byte
      ldx   bpntrh            // get basic execute pointer high byte
      bcc   lab_16d4          // branch if no overflow to high byte

      inx                     // increment high byte
      bcs   lab_16d4          // branch always (can never be carry)

// search for line # in temp (itempl/itemph) from start of mem pointer (smeml)

lab_16d0:
      lda   smeml             // get start of mem low byte
      ldx   smemh             // get start of mem high byte

// search for line # in temp (itempl/itemph) from (ax)

lab_16d4:
      jsr   lab_shln          // search basic for temp integer line number from ax
      bcc   lab_16f7          // if carry clear go do "undefined statement" error
                              // (unspecified statement)

                              // carry already set for subtract
      lda   baslnl            // get pointer low byte
      sbc   #$01              // -1
      sta   bpntrl            // save basic execute pointer low byte
      lda   baslnh            // get pointer high byte
      sbc   #$00              // subtract carry
      sta   bpntrh            // save basic execute pointer high byte
      lda   #$00              // goto target is a REU file offset
      sta   textDom
lab_16e5:
      rts

lab_donok:
      ldx   #$22              // error code $22 ("loop without do" error)
      jmp   lab_xerr          // do error #x, then warm start

// perform loop

lab_loop:
      tay                     // save following token
      tsx                     // copy stack pointer
      lda   lab_stak+3,x      // get token byte from stack
      cmp   #tk_do            // compare with do token
      bne   lab_donok         // branch if no matching do

      inx                     // dump calling routine return address
      inx                     // dump calling routine return address
      txs                     // correct stack
      tya                     // get saved following token back
      beq   loopalways        // if no following token loop forever
                              // (stack pointer in x)

      cmp   #':'              // could be ':'
      beq   loopalways        // if :... loop forever

      sbc   #tk_until         // subtract token for until, we know carry is set here
      tax                     // copy to x (if it was until then y will be correct)
      beq   dorest            // branch if was until

      dex                     // decrement result
      bne   lab_16fc          // if not while go do syntax error and warm start
                              // only if the token was while will this fail

      dex                     // set invert result byte
dorest:
      stx   frnxth            // save invert result byte
      jsr   lab_igby          // increment and scan memory
      jsr   lab_evex          // evaluate expression
      lda   fac1_e            // get fac1 exponent
      beq   docmp             // if =0 go do straight compare

      lda   #$ff              // else set all bits
docmp:
      tsx                     // copy stack pointer
      eor   frnxth            // eor with invert byte
      bne   loopdone          // if <> 0 clear stack and back to interpreter loop

                              // loop condition wasn't met so do it again
loopalways:
      lda   lab_stak+2,x      // get current line low byte
      sta   clinel            // save current line low byte
      lda   lab_stak+3,x      // get current line high byte
      sta   clineh            // save current line high byte
      lda   lab_stak+4,x      // get basic execute pointer low byte
      sta   bpntrl            // save basic execute pointer low byte
      lda   lab_stak+5,x      // get basic execute pointer high byte
      sta   bpntrh            // save basic execute pointer high byte
      jsr   lab_gbyt          // scan memory
      jmp   lab_15c2          // go do interpreter inner loop

                              // clear stack and back to interpreter loop
loopdone:
      inx                     // dump do token
      inx                     // dump current line low byte
      inx                     // dump current line high byte
      inx                     // dump basic execute pointer low byte
      inx                     // dump basic execute pointer high byte
      txs                     // correct stack
      jmp   lab_data          // go perform data (find : or [eol])

// do the return without gosub error

lab_16f4:
      ldx   #$04              // error code $04 ("return without gosub" error)
      .byte $2c               // makes next line bit lab_0ea2

lab_16f7:                      // do undefined statement error
      ldx   #$0e              // error code $0e ("undefined statement" error)
      jmp   lab_xerr          // do error #x, then warm start

// perform return

lab_return:
      bne   lab_16e5          // exit if following token (to allow syntax error)

lab_16e8:
      pla                     // dump calling routine return address
      pla                     // dump calling routine return address
      pla                     // pull token
      cmp   #tk_gosub         // compare with gosub token
      bne   lab_16f4          // branch if no matching gosub

lab_16ff:
      pla                     // pull current line low byte
      sta   clinel            // save current line low byte
      pla                     // pull current line high byte
      sta   clineh            // save current line high byte
      pla                     // pull basic execute pointer low byte
      sta   bpntrl            // save basic execute pointer low byte
      pla                     // pull basic execute pointer high byte
      sta   bpntrh            // save basic execute pointer high byte

                              // now do the data statement as we could be returning into
                              // the middle of an on <var> gosub n,m,p,q line
                              // (the return address used by the data statement is the one
                              // pushed before the gosub was executed!)

// perform data

lab_data:
      jsr   lab_snbs          // scan for next basic statement ([:] or [eol])

                              // set basic execute pointer
lab_170f:
      tya                     // copy index to a
      clc                     // clear carry for add
      adc   bpntrl            // add basic execute pointer low byte
      sta   bpntrl            // save basic execute pointer low byte
      bcc   lab_1719          // skip next if no carry

      inc   bpntrh            // else increment basic execute pointer high byte
lab_1719:
      rts

lab_16fc:
      jmp   lab_sner          // do syntax error then warm start

// scan for next basic statement ([:] or [eol])
// returns y as index to [:] or [eol]

lab_snbs:
      ldx   #':'              // set look for character = ":"
      .byte $2c               // makes next line bit $00a2

// scan for next basic line
// returns y as index to [eol]

lab_snbl:
      ldx   #$00              // set alt search character = [eol]
      ldy   #$00              // set search character = [eol]
      sty   asrch             // store search character
lab_1725:
      txa                     // get alt search character
      eor   asrch             // toggle search character, effectively swap with $00
      sta   asrch             // save swapped search character
lab_172d:
      jsr   textByteAtY       // get next byte
      beq   lab_1719          // exit if null [eol]

      cmp   asrch             // compare with search character
      beq   lab_1719          // exit if found

      iny                     // increment index
      cmp   #$22              // compare current character with open quote
      bne   lab_172d          // if not open quote go get next character

      beq   lab_1725          // if found go swap search character for alt search character

// perform if

lab_if:
      jsr   lab_evex          // evaluate the expression
      jsr   lab_gbyt          // scan memory
      cmp   #tk_then          // compare with then token
      beq   lab_174b          // if it was then go do if

                              // wasn't if .. then so must be if .. goto
      cmp   #tk_goto          // compare with goto token
      bne   lab_16fc          // if it wasn't goto go do syntax error

      ldx   bpntrl            // save the basic pointer low byte
      ldy   bpntrh            // save the basic pointer high byte
      tya                     // save pointer hi (lab_igby clobbers Y)
      pha
      jsr   lab_igby          // increment and scan memory
      pla
      tay                     // restore pointer hi (C survives pla/tay)
      bcs   lab_16fc          // if not numeric go do syntax error

      stx   bpntrl            // restore the basic pointer low byte
      sty   bpntrh            // restore the basic pointer high byte
lab_174b:
      lda   fac1_e            // get fac1 exponent
      beq   lab_174e          // if the result was zero go look for an else

      jsr   lab_igby          // else increment and scan memory
      bcs   lab_174d          // if not numeric go do var or keyword

lab_174c:
      jmp   lab_goto          // else was numeric so do goto n

                              // is var or keyword
lab_174d:
// *** patch       allow next, loop & return to find for, do or gosub structure on stack
// *** replace
//      cmp   #tk_return        // compare the byte with the token for return
//      bne   lab_174g          // if it wasn't return go interpret basic code from (bpntrl)
//                              // and return to this code to process any following code
//
//      jmp   lab_1602          // else it was return so interpret basic code from (bpntrl)
//                              // but don't return here
//
//lab_174g
//      jsr   lab_15ff          // interpret basic code from (bpntrl)
//
//// the if was executed and there may be a following else so the code needs to return
//// here to check and ignore the else if present
//
//      ldy   #$00              // clear the index
//      lda   (bpntrl),y        // get the next basic byte
//      cmp   #tk_else          // compare it with the token for else
//      beq   lab_data          // if else ignore the following statement
//
//// there was no else so continue execution of if <expr> then <stat> [: <stat>]. any
//// following else will, correctly, cause a syntax error
//
//      rts                     // else return to the interpreter inner loop
//
// *** with
      pla                     // discard interpreter loop return address
      pla                     // so data structures are at the correct stack offset
      jsr   lab_gbyt          // restore token or variable
      jsr   lab_15ff          // interpret basic code from (bpntrl)

// the if was executed and there may be a following else so the code needs to return
// here to check and ignore the else if present

      ldy   #$00              // clear the index
      jsr   textByteAtY       // get the next basic byte
      cmp   #tk_else          // compare it with the token for else
      bne   lab_no_else       // no - continue on this line
      jsr   lab_data          // yes - skip the rest of the line

// there was no else so continue execution of if <expr> then <stat> [: <stat>]. any
// following else will, correctly, cause a syntax error

lab_no_else:
      jmp lab_15c2            // return to the interpreter inner loop
// *** end patch  allow next, loop & return to find for, do or gosub structure on stack

// perform else after if

lab_174e:
      ldy   #$00              // clear the basic byte index
      ldx   #$01              // clear the nesting depth
lab_1750:
      iny                     // increment the basic byte index
      jsr   textByteAtY       // get the next basic byte
      beq   lab_1753          // if eol go add the pointer and return

      cmp   #tk_if            // compare the byte with the token for if
      bne   lab_1752          // if not if token skip the depth increment

      inx                     // else increment the nesting depth ..
      bne   lab_1750          // .. and continue looking

lab_1752:
      cmp   #tk_else          // compare the byte with the token for else
      bne   lab_1750          // if not else token continue looking

      dex                     // was else so decrement the nesting depth
      bne   lab_1750          // loop if still nested

      iny                     // increment the basic byte index past the else

// found the matching else, now do <{n|statement}>

lab_1753:
      tya                     // else copy line index to a
      clc                     // clear carry for add
      adc   bpntrl            // add the basic execute pointer low byte
      sta   bpntrl            // save the basic execute pointer low byte
      bcc   lab_1754          // branch if no overflow to high byte

      inc   bpntrh            // else increment the basic execute pointer high byte
lab_1754:
      jsr   lab_gbyt          // scan memory
      bcc   lab_174c          // if numeric do goto n
                              // the code will return to the interpreter loop at the
                              // tail end of the goto <n>

      jmp   lab_15ff          // interpret basic code from (bpntrl)
                              // the code will return to the interpreter loop at the
                              // tail end of the <statement>

// perform rem, skip (rest of) line

lab_rem:
      jsr   lab_snbl          // scan for next basic line
      jmp   lab_170f          // go set basic execute pointer and return, branch always

lab_16fd:
      jmp   lab_sner          // do syntax error then warm start

// perform on

lab_on:
      jsr   lab_gtby          // get byte parameter
      pha                     // push goto/gosub token
      cmp   #tk_gosub         // compare with gosub token
      beq   lab_176b          // branch if gosub

      cmp   #tk_goto          // compare with goto token
lab_1767:
      bne   lab_16fd          // if not goto do syntax error then warm start


// next character was goto or gosub

lab_176b:
      dec   fac1_3            // decrement index (byte value)
      bne   lab_1773          // branch if not zero

      pla                     // pull goto/gosub token
      jmp   lab_1602          // go execute it

lab_1773:
      jsr   lab_igby          // increment and scan memory
      jsr   lab_gfpn          // get fixed-point number into temp integer (skip this n)
                              // (we could ldx #',' and jsr lab_snbl+2, then we
                              // just bne lab_176b for the loop. should be quicker ..
                              // no we can't, what if we meet a colon or [eol]?)
      cmp   #$2c              // compare next character with ","
      beq   lab_176b          // loop if ","

lab_177e:
      pla                     // else pull keyword token (run out of options)
                              // also dump +/-1 pointer low byte and exit
lab_177f:
      rts

// takes n * 106 + 11 cycles where n is the number of digits

// get fixed-point number into temp integer

lab_gfpn:
      ldx   #$00              // clear reg
      stx   itempl            // clear temporary integer low byte
lab_1785:
      stx   itemph            // save temporary integer high byte
      bcs   lab_177f          // return if carry set, end of scan, character was
                              // not 0-9

      cpx   #$19              // compare high byte with $19
      tay                     // ensure zb = 0 if the branch is taken
      bcs   lab_1767          // branch if >=, makes max line # 63999 because next
                              // bit does *$0a, = 64000, compare at target will fail
                              // and do syntax error

      sbc   #'0'-1            // subtract "0", $2f + carry, from byte
      tay                     // copy binary digit
      lda   itempl            // get temporary integer low byte
      asl                     // *2 low byte
      rol   itemph            // *2 high byte
      asl                     // *2 low byte
      rol   itemph            // *2 high byte, *4
      adc   itempl            // + low byte, *5
      sta   itempl            // save it
      txa                     // get high byte copy to a
      adc   itemph            // + high byte, *5
      asl   itempl            // *2 low byte, *10d
      rol                     // *2 high byte, *10d
      tax                     // copy high byte back to x
      tya                     // get binary digit back
      adc   itempl            // add number low byte
      sta   itempl            // save number low byte
      bcc   lab_17b3          // if no overflow to high byte get next character

      inx                     // else increment high byte
lab_17b3:
      jsr   lab_igby          // increment and scan memory
      jmp   lab_1785          // loop for next character

// perform dec

lab_dec:
      lda   #<lab_2afd        // set -1 pointer low byte
      .byte $2c               // bit abs to skip the lda below

// perform inc

lab_inc:
      lda   #<lab_259c        // set 1 pointer low byte
lab_17b5:
      pha                     // save +/-1 pointer low byte
lab_17b7:
      jsr   lab_gvar          // get var address
      ldx   dtypef            // get data type flag, $ff=string, $00=numeric
      bmi   increrr           // exit if string

      sta   lvarpl            // save var address low byte
      sty   lvarph            // save var address high byte
      jsr   readVar           // unpack var (ay) into fac1
      pla                     // get +/-1 pointer low byte
      pha                     // save +/-1 pointer low byte
      ldy   #>lab_259c        // set +/-1 pointer high byte (both the same)
      jsr   lab_246c          // add (ay) to fac1
      jsr   writeVarL         // pack fac1 into variable (lvarpl)

      jsr   lab_gbyt          // scan memory
      cmp   #','              // compare with ","
      bne   lab_177e          // exit if not "," (either end or error)

                              // was "," so another incr variable to do
      jsr   lab_igby          // increment and scan memory
      jmp   lab_17b7          // go do next var

increrr:
      jmp   lab_1abc          // do "type mismatch" error then warm start

// perform let

lab_let:
      jsr   lab_gvar          // get var address
      sta   lvarpl            // save var address low byte
      sty   lvarph            // save var address high byte
      lda   #tk_equal         // get = token
      jsr   lab_scca          // scan for chr$(a), else do syntax error then warm start
      lda   dtypef            // get data type flag, $ff=string, $00=numeric
      pha                     // push data type flag
      jsr   lab_evex          // evaluate expression
      pla                     // pop data type flag
      rol                     // set carry if type = string
// *** begin patch  result of a string compare stores string pointer to variable
//                  but should store fac1 (true/false value)
// *** replace
//      jsr   lab_cktm          // type match check, set c for string
//      bne   lab_17d5          // branch if string
// *** with
      jsr   lab_cktm          // type match check, keep c (expected type)
      bcs   lab_17d5          // branch if string
// *** end patch

      jmp   writeVarL         // pack fac1 into variable (lvarpl) and return

// string let

lab_17d5:
      lda   des_pl            // wrapper base = descriptor
      sta   ut1_pl
      lda   des_ph
      sta   ut1_ph
      ldy   #$02              // set index to pointer high byte
      jsr   descRd            // get string pointer high byte (ZP or REU descriptor)
      cmp   sstorh            // compare bottom of string space high byte
      bcc   lab_17f4          // if less assign value and exit (was in program memory)

      bne   lab_17e6          // branch if >
                              // else was equal so compare low bytes
      dey                     // decrement index
      jsr   descRd            // get pointer low byte (ZP or REU descriptor)
      cmp   sstorl            // compare bottom of string space low byte
      bcc   lab_17f4          // if less assign value and exit (was in program memory)

                              // pointer was >= to bottom of string space pointer
lab_17e6:
      lda   desDom            // descriptor in a REU variable?
      beq   lab_17fb          // yes -> make space and copy the string

lab_17f4:
      lda   des_pl            // get descriptor pointer low byte
      ldy   des_ph            // get descriptor pointer high byte
      ldx   desDom            // des_2 = descriptor copy -- inherit its domain
      stx   des2Dom
      jmp   lab_1811          // clean stack, copy descriptor to variable and return

                              // make space and copy string
lab_17fb:
      ldy   #$00              // index to length
      jsr   descRd            // get string length (ZP or REU descriptor)
      jsr   lab_209c          // copy string
      lda   des_2l            // get descriptor pointer low byte
      ldy   des_2h            // get descriptor pointer high byte
      sta   ssptr_l           // save descriptor pointer low byte
      sty   ssptr_h           // save descriptor pointer high byte
      lda   des2Dom           // ssptr = des_2 copy -- inherit its domain
      sta   ssDom
      jsr   lab_228a          // copy string from descriptor (sdescr) to (sutill)
      lda   #<fac1_e          // set descriptor pointer low byte
      ldy   #>fac1_e          // get descriptor pointer high byte
      ldx   #1                // descriptor 2 is now the ZP fac1_e pseudo-descriptor
      stx   des2Dom

                              // clean stack and assign value to string variable
lab_1811:
      sta   des_2l            // save descriptor_2 pointer low byte
      sty   des_2h            // save descriptor_2 pointer high byte
      lda   des2Dom           // only ZP descriptors can sit on the stack
      beq   lab_1811np
      lda   des_2l            // lab_22eb compares A,Y against last_sl,last_sh
      jsr   lab_22eb          // clean descriptor stack, ya = pointer
lab_1811np:
      lda   des_2l            // wrapper base = descriptor 2
      sta   ut1_pl
      lda   des_2h
      sta   ut1_ph
      lda   lvarpl            // second wrapper base = let variable
      sta   varPtr
      lda   lvarph
      sta   varPtr+1
      ldy   #$00              // index to length
      jsr   descRdD2          // get string length (descriptor 2 domain)
      jsr   pokeVarByte2      // copy to let string variable
      iny                     // index to string pointer low byte
      jsr   descRdD2          // get string pointer low byte (descriptor 2 domain)
      jsr   pokeVarByte2      // copy to let string variable
      iny                     // index to string pointer high byte
      jsr   descRdD2          // get string pointer high byte (descriptor 2 domain)
      jsr   pokeVarByte2      // copy to let string variable
      rts

// perform get

lab_get:
      jsr   lab_gvar          // get var address
      sta   lvarpl            // save var address low byte
      sty   lvarph            // save var address high byte
      jsr   inget             // get input byte
      ldx   dtypef            // get data type flag, $ff=string, $00=numeric
      bmi   lab_gets          // go get string character

                              // was numeric get
      tay                     // copy character to y
      jsr   lab_1fd0          // convert y to byte in fac1
      jmp   writeVarL         // pack fac1 into variable (lvarpl) and return

lab_gets:
      pha                     // save character
      lda   #$01              // string is single byte
      bcs   lab_isbyte        // branch if byte received

      pla                     // string is null
lab_isbyte:
      jsr   lab_mssp          // make string space a bytes long a=$ac=length,
                              // x=$ad=sutill=ptr low byte, y=$ae=sutilh=ptr high byte
      beq   lab_nost          // skip store if null string

      lda   str_pl            // wrapper base = string (before pla -- A holds the char)
      sta   ut1_pl
      lda   str_ph
      sta   ut1_ph
      pla                     // get character back
      ldy   #$00              // clear index
      jsr   writeStrByte      // save byte in string (byte is string!)
lab_nost:
      jsr   lab_rtst          // check for space on descriptor stack then put address
                              // and length on descriptor stack and update stack pointers

      jmp   lab_17d5          // do string let and return

// perform print

lab_1829:
      jsr   lab_18c6          // print string from sutill/sutilh
lab_182c:
      jsr   lab_gbyt          // scan memory

// print

lab_print:
      beq   lab_crlf          // if nothing following just print cr/lf

lab_1831:
      cmp   #tk_tab           // compare with tab( token
      beq   lab_18a2          // go do tab/spc

      cmp   #tk_spc           // compare with spc( token
      beq   lab_18a2          // go do tab/spc

      cmp   #','              // compare with ","
      beq   lab_188b          // go do move to next tab mark

      cmp   #';'              // compare with "//"
      beq   lab_18bd          // if "//" continue with print processing

      jsr   lab_evex          // evaluate expression
      bit   dtypef            // test data type flag, $ff=string, $00=numeric
      bmi   lab_1829          // branch if string

      jsr   lab_296e          // convert fac1 to string
      jsr   lab_20aeC64       // decimal scratch is C64 memory
      ldy   #$00              // clear index

// don't check fit if shell width byte is zero

      lda   twidth            // get shell width byte
      beq   lab_185e          // skip check if zero

      sec                     // set carry for subtract
      sbc   tpos              // subtract shell position
      sbc   (des_pl),y        // subtract string length
      bcs   lab_185e          // branch if less than shell width

      jsr   lab_crlf          // else print cr/lf
lab_185e:
      jsr   lab_18c6          // print string from sutill/sutilh
      beq   lab_182c          // always go continue processing line

// print cr/lf

lab_crlf:
      lda   #$0d              // load [cr]
      jsr   lab_prna          // go print the character
      lda   #$0a              // load [lf]
      bne   lab_prna          // go print the character and return, branch always

lab_188b:
      lda   tpos              // get shell position
      cmp   iclim             // compare with input column limit
      bcc   lab_1897          // branch if less

      jsr   lab_crlf          // else print cr/lf (next line)
      bne   lab_18bd          // continue with print processing (branch always)

lab_1897:
      sec                     // set carry for subtract
lab_1898:
      sbc   tabsiz            // subtract tab size
      bcs   lab_1898          // loop if result was +ve

      eor   #$ff              // complement it
      adc   #$01              // +1 (twos complement)
      bne   lab_18b6          // always print a spaces (result is never $00)

                              // do tab/spc
lab_18a2:
      pha                     // save token
      jsr   lab_sgby          // scan and get byte parameter
      cmp   #$29              // is next character )
      beq   lab_18a2ok
      jmp   lab_1910          // if not do syntax error then warm start
lab_18a2ok:

      pla                     // get token back
      cmp   #tk_tab           // was it tab ?
      bne   lab_18b7          // if not go do spc

                              // calculate tab offset
      txa                     // copy integer value to a
      sbc   tpos              // subtract shell position
      bcc   lab_18bd          // branch if result was < 0 (can't tab backwards)

                              // print a spaces
lab_18b6:
      tax                     // copy result to x
lab_18b7:
      txa                     // set flags on size for spc
      beq   lab_18bd          // branch if result was = $0, already here

                              // print x spaces
lab_18ba:
      jsr   lab_18e0          // print " "
      dex                     // decrement count
      bne   lab_18ba          // loop if not all done

                              // continue with print processing
lab_18bd:
      jsr   lab_igby          // increment and scan memory
      bne   lab_1831          // if more to print go do it

      rts

// print null terminated string from memory

lab_18c3:
      sta   ut1_pl            // C64 code/utility string pointer
      sty   ut1_ph
      ldy   #$00
lab_18c3lp:
      lda   (ut1_pl),y
      beq   lab_188c
      jsr   lab_prna
      iny
      bne   lab_18c3lp
      inc   ut1_ph             // support strings crossing a page boundary
      bne   lab_18c3lp

// print string from sutill/sutilh

lab_18c6:
      jsr   lab_22b6          // pop string off descriptor stack, or from top of string
                              // space returns with a = length, x=$71=pointer low byte,
                              // y=$72=pointer high byte
      ldy   #$00              // reset index
      tax                     // copy length to x
      beq   lab_188c          // exit (rts) if null string

lab_18cd:
      txa                     // readStrByte clobbers X; it holds the count
      pha
      jsr   readStrByte       // get next byte
      sta   fioScr            // preserve byte while restoring X
      pla
      tax
      lda   fioScr
      jsr   lab_prna          // go print the character
      iny                     // increment index
      dex                     // decrement count
      bne   lab_18cd          // loop if not done yet

      rts

                              // print single format character
// print " "

lab_18e0:
      lda   #$20              // load " "
      .byte $2c               // change next line to bit lab_3fa9

// print "?" character

lab_18e3:
      lda   #$3f              // load "?" character

// print character in a
// now includes the null handler
// also includes infinite line length code
// note! some routines expect this one to exit with zb=0

lab_prna:
      cmp   #' '              // compare with " "
      bcc   lab_18f9          // branch if less (non printing)

                              // else printable character
      pha                     // save the character

// don't check fit if shell width byte is zero

      lda   twidth            // get shell width
      bne   lab_18f0          // branch if not zero (not infinite length)

// is "infinite line" so check tab position

      lda   tpos              // get position
      sbc   tabsiz            // subtract tab size, carry set by cmp #$20 above
      bne   lab_18f7          // skip reset if different

      sta   tpos              // else reset position
      beq   lab_18f7          // go print character

lab_18f0:
      cmp   tpos              // compare with shell character position
      bne   lab_18f7          // branch if not at end of line

      jsr   lab_crlf          // else print cr/lf
lab_18f7:
      inc   tpos              // increment shell position
      pla                     // get character back
lab_18f9:
      jsr   v_outp            // output byte via output vector
      cmp   #$0d              // compare with [cr]
      bne   lab_188a          // branch if not [cr]

                              // else print nullct nulls after the [cr]
      stx   tempb             // save buffer index
      ldx   nullct            // get null count
      beq   lab_1886          // branch if no nulls

      lda   #$00              // load [null]
lab_1880:
      jsr   lab_prna          // go print the character
      dex                     // decrement count
      bne   lab_1880          // loop if not all done

      lda   #$0d              // restore the character (and set the flags)
lab_1886:
      stx   tpos              // clear shell position (x always = zero when we get here)
      ldx   tempb             // restore buffer index
lab_188a:
      and   #$ff              // set the flags
lab_188c:
      rts

// handle bad input data

lab_1904:
      lda   imode             // get input mode flag, $00=input, $00=read
      bpl   lab_1913          // branch if input (go do redo)

      lda   dlinel            // get current data line low byte
      ldy   dlineh            // get current data line high byte
      sta   clinel            // save current line low byte
      sty   clineh            // save current line high byte
lab_1910:
      jmp   lab_sner          // do syntax error then warm start

                              // mode was input
lab_1913:
      lda   #<lab_redo        // point to redo message (low addr)
      ldy   #>lab_redo        // point to redo message (high addr)
      jsr   lab_18c3          // print null terminated string from memory
      lda   cpntrl            // get continue pointer low byte
      ldy   cpntrh            // get continue pointer high byte
      sta   bpntrl            // save basic execute pointer low byte
      sty   bpntrh            // save basic execute pointer high byte
      lda   #$00              // bpntr = continue pointer (program text, REU)
      sta   textDom
      rts

// perform input

lab_input:
      cmp   #$22              // compare next byte with open quote
      bne   lab_1934          // branch if no prompt string

      jsr   lab_1bc1          // print "..." string
      lda   #$3b              // load a with "//"
      jsr   lab_scca          // scan for chr$(a), else do syntax error then warm start
      jsr   lab_18c6          // print string from sutill/sutilh

                              // done with prompt, now get data
lab_1934:
      jsr   lab_ckrn          // check not direct, back here if ok
      jsr   lab_inln          // print "? " and get basic input
      lda   #$00              // set mode = input
      cmp   ibuffs            // test first byte in buffer
      bne   lab_1953          // branch if not null input

// *** change p2: keep carry set to throw break message
//      clc                     // was null input so clear carry to exit program
      jmp   lab_1647          // go do break exit

// perform read

lab_read:
      ldx   dptrl             // get data pointer low byte
      ldy   dptrh             // get data pointer high byte
      lda   #$80              // set mode = read

lab_1953:
      sta   imode             // set input mode flag, $00=input, $80=read
      stx   rdptrl            // save read pointer low byte
      sty   rdptrh            // save read pointer high byte

                              // read or input next variable from list
lab_195b:
      jsr   lab_gvar          // get (var) address
      sta   lvarpl            // save address low byte
      sty   lvarph            // save address high byte
      lda   bpntrl            // get basic execute pointer low byte
      ldy   bpntrh            // get basic execute pointer high byte
      sta   itempl            // save as temporary integer low byte
      sty   itemph            // save as temporary integer high byte
      ldx   rdptrl            // get read pointer low byte
      ldy   rdptrh            // get read pointer high byte
      stx   bpntrl            // set basic execute pointer low byte
      sty   bpntrh            // set basic execute pointer high byte
      lda   #$01              // assume input: data is C64 ibuffs
      bit   imode             // test input mode flag, $00=input, $80=read
      bpl   lab_195bd         // branch if input
      lda   #$00              // read: data is REU program text
lab_195bd:
      sta   strC64Src         // lab_20b4 scan domain (1=C64, 0=REU)
      sta   textDom           // lab_gbyt fetch domain (1=C64, 0=REU)
      jsr   lab_gbyt          // scan memory
      bne   lab_1988          // branch if not null

                              // pointer was to null entry
      bit   imode             // test input mode flag, $00=input, $80=read
      bmi   lab_19dd          // branch if read

                              // mode was input
      jsr   lab_18e3          // print "?" character (double ? for extended input)
      jsr   lab_inln          // print "? " and get basic input
      stx   bpntrl            // set basic execute pointer low byte
      sty   bpntrh            // set basic execute pointer high byte
lab_1985:
      jsr   lab_gbyt          // scan memory
lab_1988:
      bit   dtypef            // test data type flag, $ff=string, $00=numeric
      bpl   lab_19b0          // branch if numeric

                              // else get string
      sta   srchc             // save search character
      cmp   #$22              // was it " ?
      beq   lab_1999          // branch if so

      lda   #':'              // else search character is ":"
      sta   srchc             // set new search character
      lda   #','              // other search character is ","
      clc                     // clear carry for add
lab_1999:
      sta   asrch             // set second search character
      lda   bpntrl            // get basic execute pointer low byte
      ldy   bpntrh            // get basic execute pointer high byte

      adc   #$00              // c is =1 if we came via the beq lab_1999, else =0
      bcc   lab_19a4          // branch if no execute pointer low byte rollover

      iny                     // else increment high byte
lab_19a4:
      jsr   lab_20b4          // print srchc or asrch terminated string to sutill/sutilh
      jsr   lab_23f3          // restore basic execute pointer from temp (btmpl/btmph)
      jsr   lab_17d5          // go do string let
      jmp   lab_19b6          // go check string terminator

                              // get numeric input
lab_19b0:
      jsr   lab_2887          // get fac1 from string
      jsr   writeVarL         // pack fac1 into (lvarpl)
lab_19b6:
      jsr   lab_gbyt          // scan memory
      beq   lab_19c5          // branch if null (last entry)

      cmp   #','              // else compare with ","
      beq   lab_19c2          // branch if ","

      jmp   lab_1904          // else go handle bad input data

                              // got good input data
lab_19c2:
      jsr   lab_igby          // increment and scan memory
lab_19c5:
      lda   bpntrl            // get basic execute pointer low byte (temp read/input ptr)
      ldy   bpntrh            // get basic execute pointer high byte (temp read/input ptr)
      sta   rdptrl            // save for now
      sty   rdptrh            // save for now
      lda   itempl            // get temporary integer low byte (temp basic execute ptr)
      ldy   itemph            // get temporary integer high byte (temp basic execute ptr)
      sta   bpntrl            // set basic execute pointer low byte
      sty   bpntrh            // set basic execute pointer high byte
      lda   #$00              // bpntr is back on program text (REU)
      sta   textDom
      jsr   lab_gbyt          // scan memory
      beq   lab_1a03          // if null go do extra ignored message

      jsr   lab_1c01          // else scan for "," , else do syntax error then warm start
      jmp   lab_195b          // go input next variable from list

                              // find next data statement or do "out of data" error
lab_19dd:
      jsr   lab_snbs          // scan for next basic statement ([:] or [eol])
      iny                     // increment index
      tax                     // copy character ([:] or [eol])
      bne   lab_19f6          // branch if [:]

      ldx   #$06              // set for "out of data" error
      jsr   textByteAtY       // get next line pointer low byte
      sta   tempb             // stash it
      iny                     // increment index to high byte
      jsr   textByteAtY       // get next line pointer high byte
      ora   tempb             // OR low and high (Z set if link == 0)
      beq   lab_1a54          // branch if end (eventually does error x)

      iny                     // increment index
      jsr   textByteAtY       // get next line # low byte
      sta   dlinel            // save current data line low byte
      iny                     // increment index
      jsr   textByteAtY       // get next line # high byte
      iny                     // increment index
      sta   dlineh            // save current data line high byte
lab_19f6:
      jsr   textByteAtY       // get byte
      iny                     // increment index
      tax                     // copy to x
      jsr   lab_170f          // set basic execute pointer
      cpx   #tk_data          // compare with "data" token
      bne   lab_19f7          // not data -> keep scanning
      jmp   lab_1985          // was "data" so go do next read (too far to branch)
lab_19f7:
      bne   lab_19dd          // go find next statement if not "data"

// end of input/read routine

lab_1a03:
      lda   rdptrl            // get temp read pointer low byte
      ldy   rdptrh            // get temp read pointer high byte
      ldx   imode             // get input mode flag, $00=input, $80=read
      bpl   lab_1a0e          // branch if input

      jmp   lab_1624          // save ay as data pointer and return

                              // we were getting input
lab_1a0e:
      ldy   #$00              // clear index
      lda   (rdptrl),y        // get next byte
      bne   lab_1a1b          // error if not end of input

      rts

                              // user typed too much
lab_1a1b:
      lda   #<lab_imsg        // point to extra ignored message (low addr)
      ldy   #>lab_imsg        // point to extra ignored message (high addr)
      jmp   lab_18c3          // print null terminated string from memory and return

// search the stack for for activity
// exit with z=1 if for else exit with z=0

lab_11a1:
      tsx                     // copy stack pointer
      inx                     // +1 pass return address
      inx                     // +2 pass return address
      inx                     // +3 pass calling routine return address
      inx                     // +4 pass calling routine return address
lab_11a6:
      lda   lab_stak+1,x      // get token byte from stack
      cmp   #tk_for           // is it for token
      bne   lab_11ce          // exit if not for token

                              // was for token
      lda   frnxth            // get var pointer for for/next high byte
      bne   lab_11bb          // branch if not null

      lda   lab_stak+2,x      // get for variable pointer low byte
      sta   frnxtl            // save var pointer for for/next low byte
      lda   lab_stak+3,x      // get for variable pointer high byte
      sta   frnxth            // save var pointer for for/next high byte
lab_11bb:
      cmp   lab_stak+3,x      // compare var pointer with stacked var pointer (high byte)
      bne   lab_11c7          // branch if no match

      lda   frnxtl            // get var pointer for for/next low byte
      cmp   lab_stak+2,x      // compare var pointer with stacked var pointer (low byte)
      beq   lab_11ce          // exit if match found

lab_11c7:
      txa                     // copy index
      clc                     // clear carry for add
      adc   #$10              // add for stack use size
      tax                     // copy back to index
      bne   lab_11a6          // loop if not at start of stack

lab_11ce:
      rts

// perform next

lab_next:
      bne   lab_1a46          // branch if next var

      ldy   #$00              // else clear y
      beq   lab_1a49          // branch always (no variable to search for)

// next var

lab_1a46:
      jsr   lab_gvar          // get variable address
lab_1a49:
      sta   frnxtl            // store variable pointer low byte
      sty   frnxth            // store variable pointer high byte
                              // (both cleared if no variable defined)
      jsr   lab_11a1          // search the stack for for activity
      beq   lab_1a56          // branch if found

      ldx   #$00              // else set error $00 ("next without for" error)
lab_1a54:
      beq   lab_1abe          // do error #x, then warm start

lab_1a56:
      txs                     // set stack pointer, x set by search, dumps return addresses

      txa                     // copy stack pointer
      sec                     // set carry for subtract
      sbc   #$f7              // point to to var
      sta   ut2_pl            // save pointer to to var for compare
      adc   #$fb              // point to step var

      ldy   #>lab_stak        // point to stack page high byte
      jsr   lab_ufac          // unpack memory (step value) into fac1
      tsx                     // get stack pointer back
      lda   lab_stak+8,x      // get step sign
      sta   fac1_s            // save fac1 sign (b7)
      lda   frnxtl            // get for variable pointer low byte
      ldy   frnxth            // get for variable pointer high byte
      jsr   lab_246cVar       // add (for variable, REU) to fac1
      jsr   writeVarL         // pack fac1 into (for variable)
      ldy   #>lab_stak        // point to stack page high byte
      jsr   lab_27fa          // compare fac1 with (y,ut2_pl) (to value)
      tsx                     // get stack pointer back
      cmp   lab_stak+8,x      // compare step sign
      beq   lab_1a9b          // branch if = (loop complete)

                              // loop back and do it all again
      lda   lab_stak+$0d,x    // get for line low byte
      sta   clinel            // save current line low byte
      lda   lab_stak+$0e,x    // get for line high byte
      sta   clineh            // save current line high byte
      lda   lab_stak+$10,x    // get basic execute pointer low byte
      sta   bpntrl            // save basic execute pointer low byte
      lda   lab_stak+$0f,x    // get basic execute pointer high byte
      sta   bpntrh            // save basic execute pointer high byte
lab_1a98:
      jmp   lab_15c2          // go do interpreter inner loop

                              // loop complete so carry on
lab_1a9b:
      txa                     // stack copy to a
      adc   #$0f              // add $10 ($0f+carry) to dump for structure
      tax                     // copy back to index
      txs                     // copy to stack pointer
      jsr   lab_gbyt          // scan memory
      cmp   #','              // compare with ","
      bne   lab_1a98          // branch if not "," (go do interpreter inner loop)

                              // was "," so another next variable to do
      jsr   lab_igby          // else increment and scan memory
      jsr   lab_1a46          // do next (var)

// evaluate expression and check is numeric, else do type mismatch

lab_evnm:
      jsr   lab_evex          // evaluate expression

// check if source is numeric, else do type mismatch

lab_ctnm:
      clc                     // destination is numeric
      .byte $24               // makes next line bit $38

// check if source is string, else do type mismatch

lab_ctst:
      sec                     // required type is string

// type match check, set c for string, clear c for numeric

lab_cktm:
      bit   dtypef            // test data type flag, $ff=string, $00=numeric
      bmi   lab_1aba          // branch if data type is string

                              // else data type was numeric
      bcs   lab_1abc          // if required type is string do type mismatch error
lab_1ab9:
      rts

                              // data type was string, now check required type
lab_1aba:
      bcs   lab_1ab9          // exit if required type is string

                              // else do type mismatch error
lab_1abc:
      ldx   #$18              // error code $18 ("type mismatch" error)
lab_1abe:
      jmp   lab_xerr          // do error #x, then warm start

// evaluate expression

lab_evex:
      ldx   bpntrl            // get basic execute pointer low byte
      bne   lab_1ac7          // skip next if not zero

      dec   bpntrh            // else decrement basic execute pointer high byte
lab_1ac7:
      dec   bpntrl            // decrement basic execute pointer low byte

lab_evez:
      lda   #$00              // set null precedence (flag done)
lab_1acc:
      pha                     // push precedence byte
      lda   #$02              // 2 bytes
      jsr   lab_1212          // check room on stack for a bytes
      jsr   lab_gval          // get value from line
      lda   #$00              // clear a
      sta   comp_f            // clear compare function flag
lab_1adb:
      jsr   lab_gbyt          // scan memory
lab_1ade:
      sec                     // set carry for subtract
      sbc   #tk_gt            // subtract token for > (lowest comparison function)
      bcc   lab_1afa          // branch if < tk_gt

      cmp   #$03              // compare with ">" to "<" tokens
      bcs   lab_1afa          // branch if >= tk_sgn (highest evaluation function +1)

                              // was token for > = or < (a = 0, 1 or 2)
      cmp   #$01              // compare with token for =
      rol                     // *2, b0 = carry (=1 if token was = or <)
                              // (a = 0, 3 or 5)
      eor   #$01              // toggle b0
                              // (a = 1, 2 or 4. 1 if >, 2 if =, 4 if <)
      eor   comp_f            // eor with compare function flag bits
      cmp   comp_f            // compare with compare function flag
      bcc   lab_1b53          // if <(comp_f) do syntax error then warm start
                              // was more than one <, = or >)

      sta   comp_f            // save new compare function flag
      jsr   lab_igby          // increment and scan memory
      jmp   lab_1ade          // go do next character

                              // token is < ">" or > "<" tokens
lab_1afa:
      ldx   comp_f            // get compare function flag
      bne   lab_1b2a          // branch if compare function

      bcc   lab_1afb          // not a function token -> operator or lower
      jmp   lab_1b78          // go do functions
lab_1afb:
                              // else was <  tk_gt so is operator or lower
      adc   #tk_gt-tk_plus    // add # of operators (+, -, *, /, ^, and, or or eor)
      bcc   lab_1b78          // branch if < + operator

                              // carry was set so token was +, -, *, /, ^, and, or or eor
      bne   lab_1b0b          // branch if not + token

      bit   dtypef            // test data type flag, $ff=string, $00=numeric
      bpl   lab_1b0b          // branch if not string

                              // will only be $00 if type is string and token was +
      jmp   lab_224d          // add strings, string 1 is in descriptor des_pl, string 2
                              // is in line, and return

lab_1b0b:
      sta   ut1_pl            // save it
      asl                     // *2
      adc   ut1_pl            // *3
      tay                     // copy to index
lab_1b13:
      pla                     // pull previous precedence
      cmp   lab_oppt,y        // compare with precedence byte
      bcs   lab_1b7d          // branch if a >=

      jsr   lab_ctnm          // check if source is numeric, else do type mismatch
lab_1b1c:
      pha                     // save precedence
lab_1b1d:
      jsr   lab_1b43          // get vector, execute function then continue evaluation
      pla                     // restore precedence
      ldy   prstk             // get precedence stacked flag
      bpl   lab_1b3c          // branch if stacked values

      tax                     // copy precedence (set flags)
      beq   lab_1b9d          // exit if done

      bne   lab_1b86          // else pop fac2 and return, branch always

lab_1b2a:
      rol   dtypef            // shift data type flag into cb
      txa                     // copy compare function flag
      sta   dtypef            // clear data type flag, x is 0xxx xxxx
      rol                     // shift data type into compare function byte b0
      ldx   bpntrl            // get basic execute pointer low byte
      bne   lab_1b34          // branch if no underflow

      dec   bpntrh            // else decrement basic execute pointer high byte
lab_1b34:
      dec   bpntrl            // decrement basic execute pointer low byte
.const tk_lt_plus = tk_lt-tk_plus
      ldy   #tk_lt_plus*3     // set offset to last operator entry
      sta   comp_f            // save new compare function flag
      bne   lab_1b13          // branch always

lab_1b3c:
      cmp   lab_oppt,y        //.compare with stacked function precedence
      bcs   lab_1b86          // branch if a >=, pop fac2 and return

      bcc   lab_1b1c          // branch always

//.get vector, execute function then continue evaluation

lab_1b43:
      lda   lab_oppt+2,y      // get function vector high byte
      pha                     // onto stack
      lda   lab_oppt+1,y      // get function vector low byte
      pha                     // onto stack
                              // now push sign, round fac1 and put on stack
// *** begin patch  2.22p5.3   potential return address -$100 (page not incremented) ***
// *** add
   // .if [* & $ff] == $fd
      nop                     // return address of jsr +1 (on  next page)
   // .endif  
// *** end   patch  2.22p5.3   potential return address -$100 (page not incremented) ***
      jsr   lab_1b5b          // function will return here, then the next rts will call
                              // the function
      lda   comp_f            // get compare function flag
      pha                     // push compare evaluation byte
      lda   lab_oppt,y        // get precedence byte
      jmp   lab_1acc          // continue evaluating expression

lab_1b53:
      jmp   lab_sner          // do syntax error then warm start

// push sign, round fac1 and put on stack

lab_1b5b:
      lda   desDom            // stash string-descriptor domain of the fac push
      sta   fac2Dom
      pla                     // get return addr low byte
      sta   ut1_pl            // save it
      inc   ut1_pl            // increment it (was ret-1 pushed? yes!)
                              // note! no check is made on the high byte! if the calling
                              // routine assembles to a page edge then this all goes
                              // horribly wrong !!!
      pla                     // get return addr high byte
      sta   ut1_ph            // save it
      lda   fac1_s            // get fac1 sign (b7)
      pha                     // push sign

// round fac1 and put on stack

// *** begin patch  2.22p5.1   to expression may get sign bit flipped
// *** replace
//lab_1b66
//      jsr   lab_27ba          // round fac1
// *** with
      jsr   lab_27ba          // round fac1
lab_1b66:
// *** end   patch  2.22p5.1   to expression may get sign bit flipped
      lda   fac1_3            // get fac1 mantissa3
      pha                     // push on stack
      lda   fac1_2            // get fac1 mantissa2
      pha                     // push on stack
      lda   fac1_1            // get fac1 mantissa1
      pha                     // push on stack
      lda   fac1_e            // get fac1 exponent
      pha                     // push on stack
      jmp   (ut1_pl)          // return, sort of

// do functions

lab_1b78:
      ldy   #$ff              // flag function
      pla                     // pull precedence byte
lab_1b7b:
      beq   lab_1b9d          // exit if done

lab_1b7d:
      cmp   #$64              // compare previous precedence with $64
      beq   lab_1b84          // branch if was $64 (< function)

      jsr   lab_ctnm          // check if source is numeric, else do type mismatch
lab_1b84:
      sty   prstk             // save precedence stacked flag

                              // pop fac2 and return
lab_1b86:
      pla                     // pop byte
      lsr                     // shift out comparison evaluation lowest bit
      sta   cflag             // save comparison evaluation flag
      pla                     // pop exponent
      sta   fac2_e            // save fac2 exponent
      pla                     // pop mantissa1
      sta   fac2_1            // save fac2 mantissa1
      pla                     // pop mantissa2
      sta   fac2_2            // save fac2 mantissa2
      pla                     // pop mantissa3
      sta   fac2_3            // save fac2 mantissa3
      pla                     // pop sign
      sta   fac2_s            // save fac2 sign (b7)
      eor   fac1_s            // eor fac1 sign (b7)
      sta   fac_sc            // save sign compare (fac1 eor fac2)
lab_1b9d:
      lda   fac1_e            // get fac1 exponent
      rts

// print "..." string to string util area

lab_1bc1:
      lda   bpntrl            // get basic execute pointer low byte
      ldy   bpntrh            // get basic execute pointer high byte
      adc   #$00              // add carry to low byte
      bcc   lab_1bca          // branch if no overflow

      iny                     // increment high byte
lab_1bca:
      jsr   lab_20ae          // print " terminated string to sutill/sutilh
      jmp   lab_23f3          // restore basic execute pointer from temp and return

// get value from line

lab_gval:
      jsr   lab_igby          // increment and scan memory
      bcs   lab_1bac          // branch if not numeric character

                              // else numeric string found (e.g. 123)
lab_1ba9:
      jmp   lab_2887          // get fac1 from string and return

// get value from line .. continued

                              // wasn't a number so ..
lab_1bac:
      tax                     // set the flags
      bmi   lab_1bd0          // if -ve go test token values

                              // else it is either a string, number, variable or (<expr>)
      cmp   #'$'              // compare with "$"
      beq   lab_1ba9          // branch if "$", hex number

      cmp   #'%'              // else compare with "%"
      beq   lab_1ba9          // branch if "%", binary number

      cmp   #'.'              // compare with "."
      beq   lab_1ba9          // if so get fac1 from string and return (e.g. was .123)

                              // it wasn't any sort of number so ..
      cmp   #$22              // compare with "
      beq   lab_1bc1          // branch if open quote

                              // wasn't any sort of number so ..

// evaluate expression within parentheses

      cmp   #'('              // compare with "("
      bne   lab_1c18          // if not "(" get (var), return value in fac1 and $ flag

lab_1bf7:
      jsr   lab_evez          // evaluate expression, no decrement

// all the 'scan for' routines return the character after the sought character

// scan for ")" , else do syntax error then warm start

lab_1bfb:
      lda   #$29              // load a with ")"

// scan for chr$(a) , else do syntax error then warm start

lab_scca:
      ldy   #$00              // clear index
      sta   varPtr            // stash search character ($c6 free scratch)
      jsr   textByteAtY       // get byte
      cmp   varPtr            // check next byte is = a
      bne   lab_sner          // if not do syntax error then warm start

      jmp   lab_igby          // increment and scan memory then return

// scan for "(" , else do syntax error then warm start

lab_1bfe:
      lda   #$28              // load a with "("
      bne   lab_scca          // scan for chr$(a), else do syntax error then warm start
                              // (branch always)

// scan for "," , else do syntax error then warm start

lab_1c01:
      lda   #$2c              // load a with ","
      bne   lab_scca          // scan for chr$(a), else do syntax error then warm start
                              // (branch always)

// syntax error then warm start

lab_sner:
      ldx   #$02              // error code $02 ("syntax" error)
      jmp   lab_xerr          // do error #x, then warm start

// get value from line .. continued
// do tokens

lab_1bd0:
      cmp   #tk_minus         // compare with token for -
      beq   lab_1c11          // branch if - token (do set-up for functions)

                              // wasn't -n so ..
      cmp   #tk_plus          // compare with token for +
      beq   lab_gval          // branch if + token (+n = n so ignore leading +)

      cmp   #tk_not           // compare with token for not
      bne   lab_1be7          // branch if not token for not

                              // was not token
.const tk_equal_plus = tk_equal-tk_plus
      ldy   #tk_equal_plus*3  // offset to not function
      bne   lab_1c13          // do set-up for function then execute (branch always)

// do = compare

lab_equal:
      jsr   lab_evir          // evaluate integer expression (no sign check)
      lda   fac1_3            // get fac1 mantissa3
      eor   #$ff              // invert it
      tay                     // copy it
      lda   fac1_2            // get fac1 mantissa2
      eor   #$ff              // invert it
      jmp   lab_ayfc          // save and convert integer ay to fac1 and return

// get value from line .. continued

                              // wasn't +, -, or not so ..
lab_1be7:
      cmp   #tk_fn            // compare with token for fn
      bne   lab_1bee          // branch if not token for fn

      jmp   lab_201e          // go evaluate fnx

// get value from line .. continued

                              // wasn't +, -, not or fn so ..
lab_1bee:
      sbc   #tk_sgn           // subtract with token for sgn
      bcs   lab_1c27          // if a function token go do it

      jmp   lab_sner          // else do syntax error

// set-up for functions

lab_1c11:
.const tk_gt_plus = tk_gt-tk_plus
      ldy   #tk_gt_plus*3     // set offset from base to > operator
lab_1c13:
      pla                     // dump return address low byte
// *** begin patch  2.22p5.4  concatenate minus or not() crashes ehbasic  ***
// *** replace
//      pla                     // dump return address high byte
//      jmp   lab_1b1d          // execute function then continue evaluation
// *** with
      tax                     // save to trap concatenate
      pla                     // dump return address high byte
      cpx   #<[lab_224da+2]   // from concatenate low return address?
      bne   lab_1c13b         // no - continue!
      cmp   #>[lab_224da+2]   // from concatenate high return address?
      beq   lab_1c13a         // yes - error!
lab_1c13b:
      jmp   lab_1b1d          // execute function then continue evaluation
lab_1c13a:
      jmp   lab_1abc          // throw "type mismatch error" then warm start      
// *** end   patch  2.22p5.4  concatenate minus or not() crashes ehbasic  ***

// variable name set-up
// get (var), return value in fac_1 and $ flag

lab_1c18:
      jsr   lab_gvar          // get (var) address
      sta   fac1_2            // save address low byte in fac1 mantissa2
      sty   fac1_3            // save address high byte in fac1 mantissa3
      lda   #0                // des_pl = REU variable descriptor
      sta   desDom
      ldx   dtypef            // get data type flag, $ff=string, $00=numeric
      bmi   lab_1c25          // if string then return (does rts)

lab_1c24:
      lda   fac1_2            // readVar needs A = var address low byte
      jmp   readVar           // unpack var (ay) into fac1

lab_1c25:
// *** begin patch  string pointer high byte trashed when moved to stack
// *** add
      lsr   fac1_r            // clear bit 7 (<$80) = do not round up
// *** end patch 
      rts

// get value from line .. continued
// only functions left so ..

// set up function references

// new for v2.0+ this replaces a lot of if .. then .. elseif .. then .. that was needed
// to process function calls. now the function vector is computed and pushed on the stack
// and the preprocess offset is read. if the preprocess offset is non zero then the vector
// is calculated and the routine called, if not this routine just does rts. whichever
// happens the rts at the end of this routine, or the end of the preprocess routine, calls
// the function code

// this also removes the code that bypassed type checking for functions that
// returned strings

lab_1c27:
      asl                     // *2 (2 bytes per function address)
      tay                     // copy to index

      lda   lab_ftbl+1,y        // get function jump vector high byte
      pha                     // push functions jump vector high byte
      lda   lab_ftbl,y        // get function jump vector low byte
      pha                     // push functions jump vector low byte

      lda   lab_ftpl+1,y        // get function pre process vector high byte
      beq   lab_1c56          // skip pre process if null vector

      pha                     // push functions pre process vector high byte
      lda   lab_ftpl,y        // get function pre process vector low byte
      pha                     // push functions pre process vector low byte

lab_1c56:
      rts                     // do function, or pre process, call

// process string expression in parenthesis

lab_ppfs:
      jsr   lab_1bf7          // process expression in parenthesis
      jmp   lab_ctst          // check if source is string then do function,
                              // else do type mismatch

// process numeric expression in parenthesis

lab_ppfn:
      jsr   lab_1bf7          // process expression in parenthesis
      jmp   lab_ctnm          // check if source is numeric then do function,
                              // else do type mismatch

// process getpixel(x, y) -- GordonOS graphics function (token $DD). The
// cruncher bakes the "(" into the token, so the first argument follows
// the token directly. Stages x and y into gxGpX/gxGpXh/gxGpY (basic.asm)
// for the action gxFnGetPixel.

lab_ppgn:
      jsr   lab_evez          // evaluate x
      jsr   lab_ctnm          // x must be numeric
      jsr   lab_f2fx          // itempl/itemph = x (16-bit)
      lda   itempl; sta gxGpX
      lda   itemph; sta gxGpXh
      jsr   lab_1c01          // scan for ","
      jsr   lab_evnm          // evaluate y (decrements -- past the ",")
      jsr   lab_f2fx          // itempl/itemph = y
      lda   itempl; sta gxGpY
      lda   itemph; sta gxGpYh
      jsr   lab_1bfb          // scan for ")"
      rts                     // call the action (gxFnGetPixel)

// set numeric data type and increment basic execute pointer

lab_ppbi:
      lsr   dtypef            // clear data type flag, $ff=string, $00=numeric
      jmp   lab_igby          // increment and scan memory then do function

// process string for left$, right$ or mid$

lab_lrms:
      jsr   lab_evez          // evaluate (should be string) expression
      jsr   lab_1c01          // scan for ",", else do syntax error then warm start
      jsr   lab_ctst          // check if source is string, else do type mismatch

      pla                     // get function jump vector low byte
      tax                     // save functions jump vector low byte
      pla                     // get function jump vector high byte
      tay                     // save functions jump vector high byte
      lda   des_ph            // get descriptor pointer high byte
      pha                     // push string pointer high byte
      lda   des_pl            // get descriptor pointer low byte
      pha                     // push string pointer low byte
      tya                     // get function jump vector high byte back
      pha                     // save functions jump vector high byte
      txa                     // get function jump vector low byte back
      pha                     // save functions jump vector low byte
      jsr   lab_gtby          // get byte parameter
      txa                     // copy byte parameter to a
      rts                     // go do function

// process numeric expression(s) for bin$ or hex$

lab_bhss:
      jsr   lab_evez          // process expression
      jsr   lab_ctnm          // check if source is numeric, else do type mismatch
      lda   fac1_e            // get fac1 exponent
      cmp   #$98              // compare with exponent = 2^24
      bcs   lab_bher          // branch if n>=2^24 (is too big)

      jsr   lab_2831          // convert fac1 floating-to-fixed
      ldx   #$02              // 3 bytes to do
lab_cfac:
      lda   fac1_1,x          // get byte from fac1
      sta   nums_1,x          // save byte to temp
      dex                     // decrement index
      bpl   lab_cfac          // copy fac1 mantissa to temp

      jsr   lab_gbyt          // get next basic byte
      ldx   #$00              // set default to no leading "0"s
      cmp   #')'              // compare with close bracket
      beq   lab_1c54          // if ")" go do rest of function

      jsr   lab_scgb          // scan for "," and get byte
      jsr   lab_gbyt          // get last byte back
      cmp   #')'              // is next character )
      bne   lab_bher          // if not ")" go do error

lab_1c54:
      rts                     // else do function

lab_bher:
      jmp   lab_fcer          // do function call error then warm start

// perform eor

// added operator format is the same as and or or, precedence is the same as or

// this bit worked first time but it took a while to sort out the operator table
// pointers and offsets afterwards!

lab_eor:
      jsr   getfirst          // get first integer expression (no sign check)
      eor   xoaw_l            // eor with expression 1 low byte
      tay                     // save in y
      lda   fac1_2            // get fac1 mantissa2
      eor   xoaw_h            // eor with expression 1 high byte
      jmp   lab_ayfc          // save and convert integer ay to fac1 and return

// perform or

lab_or:
      jsr   getfirst          // get first integer expression (no sign check)
      ora   xoaw_l            // or with expression 1 low byte
      tay                     // save in y
      lda   fac1_2            // get fac1 mantissa2
      ora   xoaw_h            // or with expression 1 high byte
      jmp   lab_ayfc          // save and convert integer ay to fac1 and return

// perform and

lab_and:
      jsr   getfirst          // get first integer expression (no sign check)
      and   xoaw_l            // and with expression 1 low byte
      tay                     // save in y
      lda   fac1_2            // get fac1 mantissa2
      and   xoaw_h            // and with expression 1 high byte
      jmp   lab_ayfc          // save and convert integer ay to fac1 and return

// get first value for or, and or eor

getfirst:
      jsr   lab_evir          // evaluate integer expression (no sign check)
      lda   fac1_2            // get fac1 mantissa2
      sta   xoaw_h            // save it
      lda   fac1_3            // get fac1 mantissa3
      sta   xoaw_l            // save it
      jsr   lab_279b          // copy fac2 to fac1 (get 2nd value in expression)
      jsr   lab_evir          // evaluate integer expression (no sign check)
      lda   fac1_3            // get fac1 mantissa3
lab_1c95:
      rts

// perform comparisons

// do < compare

lab_lthan:
      jsr   lab_cktm          // type match check, set c for string
      bcs   lab_1cae          // branch if string

                              // do numeric < compare
      lda   fac2_s            // get fac2 sign (b7)
      ora   #$7f              // set all non sign bits
      and   fac2_1            // and fac2 mantissa1 (and in sign bit)
      sta   fac2_1            // save fac2 mantissa1
      lda   #<fac2_e          // set pointer low byte to fac2
      ldy   #>fac2_e          // set pointer high byte to fac2
      jsr   lab_27f8          // compare fac1 with fac2 (ay)
      tax                     // copy result
      jmp   lab_1ce1          // go evaluate result

                              // do string < compare
lab_1cae:
      lsr   dtypef            // clear data type flag, $ff=string, $00=numeric
      dec   comp_f            // clear < bit in compare function flag
      jsr   lab_22b6          // pop string off descriptor stack, or from top of string
                              // space returns with a = length, x=pointer low byte,
                              // y=pointer high byte
      sta   str_ln            // save length
      stx   str_pl            // save string pointer low byte
      sty   str_ph            // save string pointer high byte
      lda   fac2Dom           // descriptor 2 domain (stashed when fac2 was pushed)
      sta   desDom
      lda   fac2_2            // get descriptor pointer low byte
      ldy   fac2_3            // get descriptor pointer high byte
      jsr   lab_22ba          // pop (ya) descriptor off stack or from top of string space
                              // returns with a = length, x=pointer low byte,
                              // y=pointer high byte
      stx   fac2_2            // save string pointer low byte
      sty   fac2_3            // save string pointer high byte
      tax                     // copy length
      sec                     // set carry for subtract
      sbc   str_ln            // subtract string 1 length
      beq   lab_1cd6          // branch if str 1 length = string 2 length

      lda   #$01              // set str 1 length > string 2 length
      bcc   lab_1cd6          // branch if so

      ldx   str_ln            // get string 1 length
      lda   #$ff              // set str 1 length < string 2 length
lab_1cd6:
      sta   fac1_s            // save length compare
      lda   fac1_1            // wrapper base = string 1 data
      sta   ut1_pl
      lda   fac1_2
      sta   ut1_ph
      lda   fac2_2            // second wrapper base = string 2 data
      sta   varPtr
      lda   fac2_3
      sta   varPtr+1
      ldy   #$ff              // set index
      inx                     // adjust for loop
lab_1cdb:
      iny                     // increment index
      dex                     // decrement count
      bne   lab_1ce6          // branch if still bytes to do

      ldx   fac1_s            // get length compare back
lab_1ce1:
      bmi   lab_1cf2          // branch if str 1 < str 2

      clc                     // flag str 1 <= str 2
      bcc   lab_1cf2          // go evaluate result

lab_1ce6:
      jsr   readStrByte2      // get string 2 byte
      sta   tempb             // stash it
      jsr   readStrByte       // get string 1 byte
      cmp   tempb             // compare with string 2 byte
      beq   lab_1cdb          // loop if bytes =

      ldx   #$ff              // set str 1 < string 2
      bcc   lab_1cf2          // branch if so

      ldx   #$01              //  set str 1 > string 2
lab_1cf2:
      inx                     // x = 0 (left>right), 1 (equal), 2 (left<right)
      lda   lab_1cf2_bits,x   // 1 (left>right), 2 (equal), 4 (left<right)
      and   cflag             // and with comparison evaluation flag
      beq   lab_1cfb          // branch if 0 (compare is false)

      lda   #$ff              // else set result true
lab_1cfb:
      jmp   lab_27db          // save a as integer byte and return

lab_1cf2_bits:
      .byte $01, $02, $04     // left>right, equal, left<right

lab_1cfe:
      jsr   lab_1c01          // scan for ",", else do syntax error then warm start

// perform dim

lab_dim:
      tax                     // copy "dim" flag to x
      jsr   lab_1d10          // search for variable
      jsr   lab_gbyt          // scan memory
      bne   lab_1cfe          // scan for "," and loop if not null

      rts

// perform << (left shift)

lab_lshift:
      jsr   getpair           // get integer expression and byte (no sign check)
      lda   fac1_2            // get expression high byte
      ldx   tempb             // get shift count
      beq   noshift           // branch if zero

      cpx   #$10              // compare bit count with 16d
      bcs   toobig            // branch if >=

ls_loop:
      asl   fac1_3            // shift low byte
      rol                     // shift high byte
      dex                     // decrement bit count
      bne   ls_loop           // loop if shift not complete

      ldy   fac1_3            // get expression low byte
      jmp   lab_ayfc          // save and convert integer ay to fac1 and return

// perform >> (right shift)

lab_rshift:
      jsr   getpair           // get integer expression and byte (no sign check)
      lda   fac1_2            // get expression high byte
      ldx   tempb             // get shift count
      beq   noshift           // branch if zero

      cpx   #$10              // compare bit count with 16d
      bcs   toobig            // branch if >=

rs_loop:
      lsr                     // shift high byte
      ror   fac1_3            // shift low byte
      dex                     // decrement bit count
      bne   rs_loop           // loop if shift not complete

noshift:
      ldy   fac1_3            // get expression low byte
      jmp   lab_ayfc          // save and convert integer ay to fac1 and return

toobig:
      lda   #$00              // clear high byte
      tay                     // copy to low byte
      jmp   lab_ayfc          // save and convert integer ay to fac1 and return

getpair:
      jsr   lab_evby          // evaluate byte expression, result in x
      stx   tempb             // save it
      jsr   lab_279b          // copy fac2 to fac1 (get 2nd value in expression)
      jmp   lab_evir          // evaluate integer expression (no sign check)

// search for variable

// return pointer to variable in cvaral/cvarah

lab_gvar:
      ldx   #$00              // set dim flag = $00
      jsr   lab_gbyt          // scan memory (1st character)
lab_1d10:
      stx   defdim            // save dim flag
lab_1d12:
      sta   varnm1            // save 1st character
      and   #$7f              // clear fn flag bit
      jsr   lab_casc          // check byte, return c=0 if<"a" or >"z"
      bcs   lab_1d1f          // branch if ok

      jmp   lab_sner          // else syntax error then warm start

                              // was variable name so ..
lab_1d1f:
      ldx   #$00              // clear 2nd character temp
      stx   dtypef            // clear data type flag, $ff=string, $00=numeric
      jsr   lab_igby          // increment and scan memory (2nd character)
      bcc   lab_1d2d          // branch if character = "0"-"9" (ok)

                              // 2nd character wasn't "0" to "9" so ..
      jsr   lab_casc          // check byte, return c=0 if<"a" or >"z"
      bcc   lab_1d38          // branch if <"a" or >"z" (go check if string)

lab_1d2d:
      tax                     // copy 2nd character

                              // ignore further (valid) characters in the variable name
lab_1d2e:
      jsr   lab_igby          // increment and scan memory (3rd character)
      bcc   lab_1d2e          // loop if character = "0"-"9" (ignore)

      jsr   lab_casc          // check byte, return c=0 if<"a" or >"z"
      bcs   lab_1d2e          // loop if character = "a"-"z" (ignore)

                              // check if string variable
lab_1d38:
      cmp   #'$'              // compare with "$"
      bne   lab_1d47          // branch if not string

// to introduce a new variable type (% suffix for integers say) then this branch
// will need to go to that check and then that branch, if it fails, go to lab_1d47

                              // type is string
      lda   #$ff              // set data type = string
      sta   dtypef            // set data type flag, $ff=string, $00=numeric
      txa                     // get 2nd character back
      ora   #$80              // set top bit (indicate string var)
      tax                     // copy back to 2nd character temp
      jsr   lab_igby          // increment and scan memory

// after we have determined the variable type we need to come back here to determine
// if it's an array of type. this would plug in a%(b[,c[,d]])) integer arrays nicely


lab_1d47:                      // gets here with character after var name in a
      stx   varnm2            // save 2nd character
      ora   sufnxf            // or with subscript/fnx flag (or fn name)
      cmp   #'('              // compare with "("
      bne   lab_1d53          // branch if not "("

      jmp   lab_1e17          // go find, or make, array

// either find or create var
// var name (1st two characters only!) is in varnm1,varnm2

                              // variable name wasn't var(... so look for plain var
lab_1d53:
      lda   #$00              // clear a
      sta   sufnxf            // clear subscript/fnx flag
      lda   svarl             // get start of vars low byte
      ldx   svarh             // get start of vars high byte
      ldy   #$00              // clear index
lab_1d5d:
      stx   vrschh            // save search address high byte
      stx   ut1_ph            // mirror for wrapper access
lab_1d5f:
      sta   vrschl            // save search address low byte
      sta   ut1_pl            // mirror for wrapper access
      cpx   sarryh            // compare high address with var space end
      bne   lab_1d69          // skip next compare if <>

                              // high addresses were = so compare low addresses
      cmp   sarryl            // compare low address with var space end
      beq   lab_1d8b          // if not found go make new var

lab_1d69:
      jsr   peekVarByte       // get variable name 1st character
      cmp   varnm1            // compare with variable name 1st character
      bne   lab_1d77          // branch if no match

                              // 1st characters match so compare 2nd characters
      iny                     // index to point to variable name 2nd character
      jsr   peekVarByte       // get variable name 2nd character
      cmp   varnm2            // compare with variable name 2nd character
      beq   lab_1dd7          // branch if match (found var)

      dey                     // else decrement index (now = $00)
lab_1d77:
      clc                     // clear carry for add
      lda   vrschl            // get search address low byte
      adc   #$06              // +6 (offset to next var name)
      bcc   lab_1d5f          // loop if no overflow to high byte

      inx                     // else increment high byte
      bne   lab_1d5d          // loop always (ram doesn't extend to $ffff !)

// check byte, return c=0 if<"a" or >"z" or "a" to "z"

lab_casc:
      cmp   #'a'              // compare with "a"
      bcs   lab_1d83          // go check <"z"+1

// check byte, return c=0 if<"A" or >"Z"

lab_1d82:
      cmp   #'A'              // compare with "A"
      bcc   lab_1d8a          // exit if less

                              // carry is set
      sbc   #$5b              // subtract "z"+1
      sec                     // set carry
      sbc   #$a5              // subtract $a5 (restore byte)
                              // carry clear if byte>$5a
lab_1d8a:
      rts

lab_1d83:
      sbc   #$7b              // subtract "z"+1
      sec                     // set carry
      sbc   #$85              // subtract $85 (restore byte)
                              // carry clear if byte>$7a
      rts

                              // reached end of variable mem without match
                              // .. so create new variable
lab_1d8b:
      pla                     // pop return address low byte
      pha                     // push return address low byte
.const lab_1c18p2 = lab_1c18+2
      cmp   #<lab_1c18p2      // compare with expected calling routine return low byte
      bne   lab_1d98          // if not get (var) go create new var

// this will only drop through if the call was from lab_1c18 and is only called
// from there if it is searching for a variable from the rhs of a let a=b statement
// it prevents the creation of variables not assigned a value.

// value returned by this is either numeric zero (exponent byte is $00) or null string
// (descriptor length byte is $00). in fact a pointer to any $00 byte would have done.

// doing this saves 6 bytes of variable memory and 168 machine cycles of time

// this is where you would put the undefined variable error call e.g.

//                             // variable doesn't exist so flag error
//     ldx   #$24              // error code $24 ("undefined variable" error)
//     jmp   lab_xerr          // do error #x then warm start

// the above replaces the three code lines below

                              // else return dummy null value
      lda   #<lab_1d96        // low byte point to $00,$00
                              // (uses part of misc constants table)
      ldy   #>lab_1d96        // high byte point to $00,$00
      rts

                              // create new numeric variable
lab_1d98:
      lda   sarryl            // get var mem end low byte
      ldy   sarryh            // get var mem end high byte
      sta   ostrtl            // save old block start low byte
      sty   ostrth            // save old block start high byte
      lda   earryl            // get array mem end low byte
      ldy   earryh            // get array mem end high byte
      sta   obendl            // save old block end low byte
      sty   obendh            // save old block end high byte
      clc                     // clear carry for add
      adc   #$06              // +6 (space for one var)
      bcc   lab_1dae          // branch if no overflow to high byte

      iny                     // else increment high byte
lab_1dae:
      sta   nbendl            // set new block end low byte
      sty   nbendh            // set new block end high byte
      jsr   copyBlock         // open up space in memory
      lda   nbendl            // get new start low byte
      ldy   nbendh            // get new start high byte (-$100)
      iny                     // correct high byte
      sta   sarryl            // save new var mem end low byte
      sty   sarryh            // save new var mem end high byte
      ldy   #$00              // clear index
      lda   vrschl            // re-point wrapper base (lab_11cf may GC)
      sta   ut1_pl
      lda   vrschh
      sta   ut1_ph
      lda   varnm1            // get var name 1st character
      jsr   pokeVarByte       // save var name 1st character
      iny                     // increment index
      lda   varnm2            // get var name 2nd character
      jsr   pokeVarByte       // save var name 2nd character
      lda   #$00              // clear a
      iny                     // increment index
      jsr   pokeVarByte       // initialise var byte
      iny                     // increment index
      jsr   pokeVarByte       // initialise var byte
      iny                     // increment index
      jsr   pokeVarByte       // initialise var byte
      iny                     // increment index
      jsr   pokeVarByte       // initialise var byte

                              // found a match for var ((vrschl) = ptr)
lab_1dd7:
      lda   vrschl            // get var address low byte
      clc                     // clear carry for add
      adc   #$02              // +2 (offset past var name bytes)
      ldy   vrschh            // get var address high byte
      bcc   lab_1de1          // branch if no overflow from add

      iny                     // else increment high byte
lab_1de1:
      sta   cvaral            // save current var address low byte
      sty   cvarah            // save current var address high byte
      rts

// set-up array pointer (adatal/h) to first element in array
// set adatal,adatah to astrtl,astrth+2*dimcnt+#$05

lab_1de6:
      lda   dimcnt            // get # of dimensions (1, 2 or 3)
      asl                     // *2 (also clears the carry !)
      adc   #$05              // +5 (result is 7, 9 or 11 here)
      adc   astrtl            // add array start pointer low byte
      ldy   astrth            // get array pointer high byte
      bcc   lab_1df2          // branch if no overflow

      iny                     // else increment high byte
lab_1df2:
      sta   adatal            // save array data pointer low byte
      sty   adatah            // save array data pointer high byte
      rts

// evaluate integer expression

lab_evin:
      jsr   lab_igby          // increment and scan memory
      jsr   lab_evnm          // evaluate expression and check is numeric,
                              // else do type mismatch

// evaluate integer expression (no check)

lab_evpi:
      lda   fac1_s            // get fac1 sign (b7)
      bmi   lab_1e12          // do function call error if -ve

// evaluate integer expression (no sign check)

lab_evir:
      lda   fac1_e            // get fac1 exponent
      cmp   #$90              // compare with exponent = 2^16 (n>2^15)
      bcc   lab_1e14          // branch if n<2^16 (is ok)

      lda   #<lab_1df7        // set pointer low byte to -32768
      ldy   #>lab_1df7        // set pointer high byte to -32768
      jsr   lab_27f8          // compare fac1 with (ay)
lab_1e12:
      beq   lab_1e14          // if = skip the error jump (falls into lab_1e14)
      jmp   lab_fcer          // if <> do function call error then warm start

lab_1e14:
      jmp   lab_2831          // convert fac1 floating-to-fixed and return

// find or make array

lab_1e17:
      lda   defdim            // get dim flag
      pha                     // push it
      lda   dtypef            // get data type flag, $ff=string, $00=numeric
      pha                     // push it
      ldy   #$00              // clear dimensions count

// now get the array dimension(s) and stack it (them) before the data type and dim flag

lab_1e1f:
      tya                     // copy dimensions count
      pha                     // save it
      lda   varnm2            // get array name 2nd byte
      pha                     // save it
      lda   varnm1            // get array name 1st byte
      pha                     // save it
      jsr   lab_evin          // evaluate integer expression
      pla                     // pull array name 1st byte
      sta   varnm1            // restore array name 1st byte
      pla                     // pull array name 2nd byte
      sta   varnm2            // restore array name 2nd byte
      pla                     // pull dimensions count
      tay                     // restore it
      tsx                     // copy stack pointer
      lda   lab_stak+2,x      // get dim flag
      pha                     // push it
      lda   lab_stak+1,x      // get data type flag
      pha                     // push it
      lda   fac1_2            // get this dimension size high byte
      sta   lab_stak+2,x      // stack before flag bytes
      lda   fac1_3            // get this dimension size low byte
      sta   lab_stak+1,x      // stack before flag bytes
      iny                     // increment dimensions count
      tya                     // save dimensions count (lab_gbyt clobbers Y)
      pha
      jsr   lab_gbyt          // scan memory
      pla
      tay                     // restore dimensions count (C survives pla/tay)
      cmp   #','              // compare with ","
      beq   lab_1e1f          // if found go do next dimension

      sty   dimcnt            // store dimensions count
      jsr   lab_1bfb          // scan for ")" , else do syntax error then warm start
      pla                     // pull data type flag
      sta   dtypef            // restore data type flag, $ff=string, $00=numeric
      pla                     // pull dim flag
      sta   defdim            // restore dim flag
      ldx   sarryl            // get array mem start low byte
      lda   sarryh            // get array mem start high byte

// now check to see if we are at the end of array memory (we would be if there were
// no arrays).

lab_1e5c:
      stx   astrtl            // save as array start pointer low byte
      stx   varPtr            // mirror for wrapper access
      sta   astrth            // save as array start pointer high byte
      sta   varPtr+1          // mirror for wrapper access
      cmp   earryh            // compare with array mem end high byte
      bne   lab_1e68          // branch if not reached array mem end

      cpx   earryl            // else compare with array mem end low byte
      beq   lab_1ea1          // go build array if not found

                              // search for array
lab_1e68:
      ldy   #$00              // clear index
      jsr   peekVarByte2      // get array name first byte
      iny                     // increment index to second name byte
      cmp   varnm1            // compare with this array name first byte
      bne   lab_1e77          // branch if no match

      jsr   peekVarByte2      // else get array name second byte
      cmp   varnm2            // compare with this array name second byte
      beq   lab_1e8d          // array found so branch

                              // no match
lab_1e77:
      iny                     // increment index
      jsr   peekVarByte2      // get array size low byte
      clc                     // clear carry for add
      adc   astrtl            // add array start pointer low byte
      tax                     // copy low byte to x
      iny                     // increment index
      jsr   peekVarByte2      // get array size high byte
      adc   astrth            // add array mem pointer high byte
      bcc   lab_1e5c          // if no overflow go check next array

// do array bounds error

lab_1e85:
      ldx   #$10              // error code $10 ("array bounds" error)
      .byte $2c               // makes next bit bit lab_08a2

// do function call error

lab_fcer:
      ldx   #$08              // error code $08 ("function call" error)
lab_1e8a:
      jmp   lab_xerr          // do error #x, then warm start

                              // found array, are we trying to dimension it?
lab_1e8d:
      ldx   #$12              // set error $12 ("double dimension" error)
      lda   defdim            // get dim flag
      bne   lab_1e8a          // if we are trying to dimension it do error #x, then warm
                              // start

// found the array and we're not dimensioning it so we must find an element in it

      jsr   lab_1de6          // set-up array pointer (adatal/h) to first element in array
                              // (astrtl,astrth points to start of array)
      ldy   #$04              // set index to array's # of dimensions
      jsr   peekVarByte2      // get array's dimensions count
      cmp   dimcnt            // compare with no of dimensions
      bne   lab_1e85          // if wrong do array bounds error, could do "wrong
                              // dimensions" error here .. if we want a different
                              // error message

      jmp   lab_1f28          // found array so go get element
                              // (could jump to lab_1f28 as all lab_1f24 does is take
                              // dimcnt and save it at (astrtl),y which is already the
                              // same or we would have taken the bne)

                              // array not found, so build it
lab_1ea1:
      jsr   lab_1de6          // set-up array pointer (adatal/h) to first element in array
                              // (astrtl,astrth points to start of array)
      jsr   lab_121f          // check available memory, "out of memory" error if no room
                              // addr to check is in ay (low/high)
      ldy   #$00              // clear y (don't need to clear a)
      sty   aspth             // clear array data size high byte
      lda   varnm1            // get variable name 1st byte
      jsr   pokeVarByte2      // save array name 1st byte
      iny                     // increment index
      lda   varnm2            // get variable name 2nd byte
      jsr   pokeVarByte2      // save array name 2nd byte
      lda   dimcnt            // get dimensions count
      ldy   #$04              // index to dimension count
      sty   asptl             // set array data size low byte (four bytes per element)
      jsr   pokeVarByte2      // set array's dimensions count

                              // now calculate the size of the data space for the array
      clc                     // clear carry for add (clear on subsequent loops)
lab_1ec0:
      ldx   #$0b              // set default dimension value low byte
      lda   #$00              // set default dimension value high byte
      bit   defdim            // test default dim flag
      bvc   lab_1ed0          // branch if b6 of defdim is clear

      pla                     // else pull dimension value low byte
      adc   #$01              // +1 (allow for zeroeth element)
      tax                     // copy low byte to x
      pla                     // pull dimension value high byte
      adc   #$00              // add carry from low byte

lab_1ed0:
      iny                     // index to dimension value high byte
      jsr   pokeVarByte2      // save dimension value high byte
      iny                     // index to dimension value low byte
      txa                     // get dimension value low byte
      jsr   pokeVarByte2      // save dimension value low byte
      jsr   lab_1f7c          // does xy = (astrtl),y * (asptl)
      stx   asptl             // save array data size low byte
      sta   aspth             // save array data size high byte
      ldy   ut1_pl            // restore index (saved by subroutine)
      dec   dimcnt            // decrement dimensions count
      bne   lab_1ec0          // loop while not = 0

      adc   adatah            // add size high byte to first element high byte
                              // (carry is always clear here)
      bcs   lab_1f45          // if overflow go do "out of memory" error

      sta   adatah            // save end of array high byte
      tay                     // copy end high byte to y
      txa                     // get array size low byte
      adc   adatal            // add array start low byte
      bcc   lab_1ef3          // branch if no carry

      iny                     // else increment end of array high byte
      beq   lab_1f45          // if overflow go do "out of memory" error

                              // set-up mostly complete, now zero the array
lab_1ef3:
      jsr   lab_121f          // check available memory, "out of memory" error if no room
                              // addr to check is in ay (low/high)
      sta   earryl            // save array mem end low byte
      sty   earryh            // save array mem end high byte
      lda   adatal            // fillBlock dst = first element
      sta   ut1_pl
      lda   adatah
      sta   ut1_ph
      lda   #$00              // clear byte for array clear
      ldx   asptl             // length low byte (array size low)
      ldy   aspth             // length high byte (array size high)
      jsr   fillBlock         // zero the whole array data space (EXACT size --
                              // HEAD zeroes asptl + aspth*256 bytes, no +1 page)
      sec                     // set carry for subtract
      ldy   #$02              // index to array size low byte
      lda   earryl            // get array mem end low byte
      sbc   astrtl            // subtract array start low byte
      jsr   pokeVarByte2      // save array size low byte
      iny                     // index to array size high byte
      lda   earryh            // get array mem end high byte
      sbc   astrth            // subtract array start high byte
      jsr   pokeVarByte2      // save array size high byte
      lda   defdim            // get default dim flag
      bne   lab_1f7b          // exit (ret) if this was a dim command

                              // else, find element
      iny                     // index to # of dimensions

lab_1f24:
      jsr   peekVarByte2      // get array's dimension count
      sta   dimcnt            // save it

// we have found, or built, the array. now we need to find the element

lab_1f28:
      lda   #$00              // clear byte
      sta   asptl             // clear array data pointer low byte
lab_1f2c:
      sta   aspth             // save array data pointer high byte
      iny                     // increment index (point to array bound high byte)
      pla                     // pull array index low byte
      tax                     // copy to x
      sta   fac1_2            // save index low byte to fac1 mantissa2
      pla                     // pull array index high byte
      sta   fac1_3            // save index high byte to fac1 mantissa3
      sta   tempb             // stash index high for compare
      jsr   peekVarByte2      // get array bound high byte
      cmp   tempb             // bound - index
      bcc   lab_1f42          // bound < index -> out of bounds
      bne   lab_1f48          // bound > index -> within bounds

                              // else high byte was = so test low bytes
      iny                     // index to array bound low byte
      txa                     // get array index low byte
      sta   tempb             // stash index low for compare
      jsr   peekVarByte2      // get array bound low byte
      cmp   tempb             // bound - index
      bcs   lab_1f49          // bound >= index -> within bounds

lab_1f42:
      jmp   lab_1e85          // else do array bounds error

lab_1f45:
      jmp   lab_omer          // do "out of memory" error then warm start

lab_1f48:
      iny                     // index to array bound low byte
lab_1f49:
      lda   aspth             // get array data pointer high byte
      ora   asptl             // or with array data pointer low byte
      beq   lab_1f5a          // branch if array data pointer = null (skip multiply)

      jsr   lab_1f7c          // does xy = (astrtl),y * (asptl)
      txa                     // get result low byte
      adc   fac1_2            // add index low byte from fac1 mantissa2
      tax                     // save result low byte
      tya                     // get result high byte
      ldy   ut1_pl            // restore index
lab_1f5a:
      adc   fac1_3            // add index high byte from fac1 mantissa3
      stx   asptl             // save array data pointer low byte
      dec   dimcnt            // decrement dimensions count
      bne   lab_1f2c          // loop if dimensions still to do

      asl   asptl             // array data pointer low byte * 2
      rol                     // array data pointer high byte * 2
      asl   asptl             // array data pointer low byte * 4
      rol                     // array data pointer high byte * 4
      tay                     // copy high byte
      lda   asptl             // get low byte
      adc   adatal            // add array data start pointer low byte
      sta   cvaral            // save as current var address low byte
      tya                     // get high byte back
      adc   adatah            // add array data start pointer high byte
      sta   cvarah            // save as current var address high byte
      tay                     // copy high byte to y
      lda   cvaral            // get current var address low byte
lab_1f7b:
      rts

// does xy = (astrtl),y * (asptl)

lab_1f7c:
      sty   ut1_pl            // save index
      jsr   peekVarByte2      // get dimension size low byte
      sta   dims_l            // save dimension size low byte
      dey                     // decrement index
      jsr   peekVarByte2      // get dimension size high byte
      sta   dims_h            // save dimension size high byte

      lda   #$10              // count = $10 (16 bit multiply)
      sta   numbit            // save bit count
      ldx   #$00              // clear result low byte
      ldy   #$00              // clear result high byte
lab_1f8f:
      txa                     // get result low byte
      asl                     // *2
      tax                     // save result low byte
      tya                     // get result high byte
      rol                     // *2
      tay                     // save result high byte
      bcs   lab_1f45          // if overflow go do "out of memory" error

      asl   asptl             // shift multiplier low byte
      rol   aspth             // shift multiplier high byte
      bcc   lab_1fa8          // skip add if no carry

      clc                     // else clear carry for add
      txa                     // get result low byte
      adc   dims_l            // add dimension size low byte
      tax                     // save result low byte
      tya                     // get result high byte
      adc   dims_h            // add dimension size high byte
      tay                     // save result high byte
      bcs   lab_1f45          // if overflow go do "out of memory" error

lab_1fa8:
      dec   numbit            // decrement bit count
      bne   lab_1f8f          // loop until all done

      rts

// perform fre()

lab_fre:
      lda   dtypef            // get data type flag, $ff=string, $00=numeric
      bpl   lab_1fb4          // branch if numeric

      jsr   lab_22b6          // pop string off descriptor stack, or from top of string
                              // space returns with a = length, x=$71=pointer low byte,
                              // y=$72=pointer high byte

                              // fre(n) was numeric so do this
lab_1fb4:
      jsr   lab_garb          // go do garbage collection
      sec                     // set carry for subtract
      lda   sstorl            // get bottom of string space low byte
      sbc   earryl            // subtract array mem end low byte
      tay                     // copy result to y
      lda   sstorh            // get bottom of string space high byte
      sbc   earryh            // subtract array mem end high byte
      jmp   lab_ayfcu         // convert UNSIGNED free bytes (fix >32767 wrap)

// save and convert integer ay to fac1 (signed)

lab_ayfc:
      lsr   dtypef            // clear data type flag, $ff=string, $00=numeric
      sta   fac1_1            // save fac1 mantissa1
      sty   fac1_2            // save fac1 mantissa2
      ldx   #$90              // set exponent=2^16 (integer)
      jmp   lab_27e3          // set exp=x, clear fac1_3, normalise and return

// convert UNSIGNED 16-bit AY to fac1 -- fre(0) free bytes are 0..65535
// (the signed lab_ayfc path wraps values >32767 to negative)

lab_ayfcu:
      lsr   dtypef            // clear data type flag
      sta   fac1_1            // fac1_1 = A (high byte)
      sty   fac1_2            // fac1_2 = Y (low byte)
      ldx   #$90              // exponent = 2^16
      sec                     // force +ve: lab_24d0 must not negate
      jmp   lab_stfa          // clear fac1_3/r/s, set exponent, normalise

// perform pos()

lab_pos:
      ldy   tpos              // get shell position

// convert y to byte in fac1

lab_1fd0:
      lda   #$00              // clear high byte
      beq   lab_ayfc          // always save and convert integer ay to fac1 and return

// check not direct (used by def and input)

lab_ckrn:
      ldx   clineh            // get current line high byte
      inx                     // increment it
      bne   lab_1f7b          // return if can continue not direct mode

                              // else do illegal direct error
lab_1fd9:
      ldx   #$16              // error code $16 ("illegal direct" error)
lab_1fdb:
      jmp   lab_xerr          // go do error #x, then warm start

// perform def

lab_def:
      jsr   lab_200b          // check fnx syntax
      sta   func_l            // save function pointer low byte
      sty   func_h            // save function pointer high byte
      jsr   lab_ckrn          // check not direct (back here if ok)
      jsr   lab_1bfe          // scan for "(" , else do syntax error then warm start
      lda   #$80              // set flag for fnx
      sta   sufnxf            // save subscript/fnx flag
      jsr   lab_gvar          // get (var) address
      jsr   lab_ctnm          // check if source is numeric, else do type mismatch
      jsr   lab_1bfb          // scan for ")" , else do syntax error then warm start
      lda   #tk_equal         // get = token
      jsr   lab_scca          // scan for chr$(a), else do syntax error then warm start
      lda   cvarah            // get current var address high byte
      pha                     // push it
      lda   cvaral            // get current var address low byte
      pha                     // push it
      lda   bpntrh            // get basic execute pointer high byte
      pha                     // push it
      lda   bpntrl            // get basic execute pointer low byte
      pha                     // push it
      jsr   lab_data          // go perform data
      jmp   lab_207a          // put execute pointer and variable pointer into function
                              // and return

// check fnx syntax

lab_200b:
      lda   #tk_fn            // get fn" token
      jsr   lab_scca          // scan for chr$(a) , else do syntax error then warm start
                              // return character after a
      ora   #$80              // set fn flag bit
      sta   sufnxf            // save fn flag so array variable test fails
      jsr   lab_1d12          // search for fn variable
      jmp   lab_ctnm          // check if source is numeric and return, else do type
                              // mismatch

                              // evaluate fnx
lab_201e:
      jsr   lab_200b          // check fnx syntax
      pha                     // push function pointer low byte
      tya                     // copy function pointer high byte
      pha                     // push function pointer high byte
      jsr   lab_1bfe          // scan for "(", else do syntax error then warm start
      jsr   lab_evex          // evaluate expression
      jsr   lab_1bfb          // scan for ")", else do syntax error then warm start
      jsr   lab_ctnm          // check if source is numeric, else do type mismatch
      pla                     // pop function pointer high byte
      sta   func_h            // restore it
      pla                     // pop function pointer low byte
      sta   func_l            // restore it
      ldx   #$20              // error code $20 ("undefined function" error)
      lda   func_l            // wrapper base = function record
      sta   ut1_pl
      lda   func_h
      sta   ut1_ph
      ldy   #$03              // index to variable pointer high byte
      jsr   peekVarByte       // get variable pointer high byte
      beq   lab_1fdb          // if zero go do undefined function error

      sta   cvarah            // save variable address high byte
      dey                     // index to variable address low byte
      jsr   peekVarByte       // get variable address low byte
      sta   cvaral            // save variable address low byte
      tax                     // copy address low byte

                              // now stack the function variable value before use
      lda   cvaral            // wrapper base = variable
      sta   ut1_pl
      lda   cvarah
      sta   ut1_ph
      iny                     // index to mantissa_3
lab_2043:
      jsr   peekVarByte       // get byte from variable
      pha                     // stack it
      dey                     // decrement index
      bpl   lab_2043          // loop until variable stacked

      ldy   cvarah            // get variable address high byte
      jsr   writeVar          // pack fac1 (function expression value) into (xy)
                              // (function variable), return y=0, always
      lda   func_l            // writeVar clobbered ut1 -- re-point it
      sta   ut1_pl
      lda   func_h
      sta   ut1_ph
      lda   bpntrh            // get basic execute pointer high byte
      pha                     // push it
      lda   bpntrl            // get basic execute pointer low byte
      pha                     // push it
      jsr   peekVarByte       // get function execute pointer low byte (y=0)
      sta   bpntrl            // save as basic execute pointer low byte
      iny                     // index to high byte
      jsr   peekVarByte       // get function execute pointer high byte
      sta   bpntrh            // save as basic execute pointer high byte
      lda   cvarah            // get variable address high byte
      pha                     // push it
      lda   cvaral            // get variable address low byte
      pha                     // push it
      jsr   lab_evnm          // evaluate expression and check is numeric,
                              // else do type mismatch
      pla                     // pull variable address low byte
      sta   func_l            // save variable address low byte
      pla                     // pull variable address high byte
      sta   func_h            // save variable address high byte
      jsr   lab_gbyt          // scan memory
      beq   lab_2074          // branch if null (should be [eol] marker)

      jmp   lab_sner          // else syntax error then warm start

// restore bpntrl,bpntrh and function variable from stack

lab_2074:
      pla                     // pull basic execute pointer low byte
      sta   bpntrl            // restore basic execute pointer low byte
      pla                     // pull basic execute pointer high byte
      sta   bpntrh            // restore basic execute pointer high byte

// put execute pointer and variable pointer into function

lab_207a:
      lda   func_l            // wrapper base = function record
      sta   ut1_pl
      lda   func_h
      sta   ut1_ph
      ldy   #$00              // clear index
      pla                     // pull basic execute pointer low byte
      jsr   pokeVarByte       // save to function
      iny                     // increment index
      pla                     // pull basic execute pointer high byte
      jsr   pokeVarByte       // save to function
      iny                     // increment index
      pla                     // pull current var address low byte
      jsr   pokeVarByte       // save to function
      iny                     // increment index
      pla                     // pull current var address high byte
      jsr   pokeVarByte       // save to function
      rts

// perform str$()

lab_strs:
      jsr   lab_ctnm          // check if source is numeric, else do type mismatch
      jsr   lab_296e          // convert fac1 to string
      lda   #<decssp1         // set result string low pointer
      ldy   #>decssp1         // set result string high pointer
      beq   lab_20aeC64       // decimal scratch is C64 memory

// do string vector
// copy des_pl/h to des_2l/h and make string space a bytes long

lab_209c:
      ldx   des_pl            // get descriptor pointer low byte
      ldy   des_ph            // get descriptor pointer high byte
      stx   des_2l            // save descriptor pointer low byte
      sty   des_2h            // save descriptor pointer high byte
      pha                     // A = length -- lab_2115 consumes it below
      lda   desDom            // descriptor 2 = descriptor copy -- inherit domain
      sta   des2Dom
      pla

// make string space a bytes long
// a=length, x=sutill=ptr low byte, y=sutilh=ptr high byte

lab_mssp:
      jsr   lab_2115          // make space in string memory for string a long
                              // return x=sutill=ptr low byte, y=sutilh=ptr high byte
      stx   str_pl            // save string pointer low byte
      sty   str_ph            // save string pointer high byte
      sta   str_ln            // save length
      rts

// scan, set up string
// print " terminated string to sutill/sutilh

lab_20ae:
      pha                     // preserve source pointer low byte (A)
      lda   #$00
      sta   strC64Src
      pla
      jmp   lab_20aeGo
lab_20aeC64:
      pha                     // preserve source pointer low byte (A)
      lda   #$01
      sta   strC64Src
      pla
lab_20aeGo:
      ldx   #$22              // set terminator to "
      stx   srchc             // set search character (terminator 1)
      stx   asrch             // set terminator 2

// print [srchc] or [asrch] terminated string to sutill/sutilh
// source is ay

lab_20b4:
      sta   ssptr_l           // store string start low byte
      sty   ssptr_h           // store string start high byte
      sta   str_pl            // save string pointer low byte
      sty   str_ph            // save string pointer high byte
      sta   ut1_pl            // wrapper base mirror
      sty   ut1_ph
      ldy   #$ff              // set length to -1
lab_20be:
      iny                     // increment length
      lda   strC64Src
      bne   lab_20beC64
      lda   clineh
      cmp   #$ff
      bne   lab_20beReu
lab_20beC64:
      lda   (ut1_pl),y        // utility/ibuffs source is C64 memory
      jmp   lab_20beByte
lab_20beReu:
      jsr   readStrByte       // stored-program source is REU memory
lab_20beByte:
      beq   lab_20cf          // exit loop if null byte [eos]

      cmp   srchc             // compare with search character (terminator 1)
      beq   lab_20cb          // branch if terminator

      cmp   asrch             // compare with terminator 2
      bne   lab_20be          // loop if not terminator 2

lab_20cb:
      cmp   #$22              // compare with "
      beq   lab_20d0          // branch if " (carry set if = !)

lab_20cf:
      clc                     // clear carry for add (only if [eol] terminated string)
lab_20d0:
      sty   str_ln            // save length in fac1 exponent
      tya                     // copy length to a
      adc   ssptr_l           // add string start low byte
      sta   sendl             // save string end low byte
      ldx   ssptr_h           // get string start high byte
      bcc   lab_20dc          // branch if no low byte overflow

      inx                     // else increment high byte
lab_20dc:
      stx   sendh             // save string end high byte
      lda   strC64Src
      bne   lab_mvst          // decimal scratch is C64 memory
      lda   clineh
      cmp   #$ff
      beq   lab_mvst          // immediate-mode source is C64 ibuffs
      jmp   lab_rtst          // program strings are REU offsets (including $00xx)
lab_mvst:
                              // string in utility area, move to string memory
      tya                     // copy length to a
      jsr   lab_209c          // copy des_pl/h to des_2l/h and make string space a bytes
                              // long
      ldx   ssptr_l           // get string start low byte
      ldy   ssptr_h           // get string start high byte
      jsr   copyStringC64     // utility/input string -> REU string space

// check for space on descriptor stack then ..
// put string address and length on descriptor stack and update stack pointers

lab_rtst:
      ldx   next_s            // get string stack pointer
      cpx   #des_sk+$09       // compare with max+1
      bne   lab_20f8          // branch if space on string stack

                              // else do string too complex error
      ldx   #$1c              // error code $1c ("string too complex" error)
lab_20f5:
      jmp   lab_xerr          // do error #x, then warm start

// put string address and length on descriptor stack and update stack pointers

lab_20f8:
      lda   str_ln            // get string length
      sta   plus_0,x          // put on string stack
      lda   str_pl            // get string pointer low byte
      sta   plus_1,x          // put on string stack
      lda   str_ph            // get string pointer high byte
      sta   plus_2,x          // put on string stack
      ldy   #$00              // clear y
      stx   des_pl            // save string descriptor pointer low byte
      sty   des_ph            // save string descriptor pointer high byte (always $00)
      lda   #1                // des_pl now points at the ZP descriptor stack
      sta   desDom
      dey                     // y = $ff
      sty   dtypef            // save data type flag, $ff=string
      stx   last_sl           // save old stack pointer (current top item)
      inx                     // update stack pointer
      inx                     // update stack pointer
      inx                     // update stack pointer
      stx   next_s            // save new top item value
      rts

// build descriptor
// make space in string memory for string a long
// return x=sutill=ptr low byte, y=sutill=ptr high byte

lab_2115:
      lsr   gclctd            // clear garbage collected flag (b7)

                              // make space for string a long
lab_2117:
      pha                     // save string length
      eor   #$ff              // complement it
      sec                     // set carry for subtract (twos comp add)
      adc   sstorl            // add bottom of string space low byte (subtract length)
      ldy   sstorh            // get bottom of string space high byte
      bcs   lab_2122          // skip decrement if no underflow

      dey                     // decrement bottom of string space high byte
lab_2122:
      cpy   earryh            // compare with array mem end high byte
      bcc   lab_2137          // do out of memory error if less

      bne   lab_212c          // if not = skip next test

      cmp   earryl            // compare with array mem end low byte
      bcc   lab_2137          // do out of memory error if less

lab_212c:
      sta   sstorl            // save bottom of string space low byte
      sty   sstorh            // save bottom of string space high byte
      sta   sutill            // save string utility ptr low byte
      sty   sutilh            // save string utility ptr high byte
      tax                     // copy low byte to x
      pla                     // get string length back
      rts

lab_2137:
      ldx   #$0c              // error code $0c ("out of memory" error)
      lda   gclctd            // get garbage collected flag
      bmi   lab_20f5          // if set then do error code x

      jsr   lab_garb          // else go do garbage collection
      lda   #$80              // flag for garbage collected
      sta   gclctd            // set garbage collected flag
      pla                     // pull length
      bne   lab_2117          // go try again (loop always, length should never be = $00)

// garbage collection routine

lab_garb:
      lda   #$03              // re-init g_step -- fp.lib aliases $a0 (cflag) and
      sta   g_step            //   clobbers it between GC runs (var/array stride)
      ldx   ememl             // get end of mem low byte
      lda   ememh             // get end of mem high byte

// re-run routine from last ending

lab_214b:
      stx   sstorl            // set string storage low byte
      sta   sstorh            // set string storage high byte
      ldy   #$00              // clear index
      sty   garb_h            // clear working pointer high byte (flag no strings to move)
// *** begin patch  2.22p5.5  garbage collection may overlap temporary strings
// *** add
      sty   garb_l            // clear working pointer low byte (flag no strings to move)
// *** begin patch  2.22p5.5  garbage collection may overlap temporary strings
      lda   earryl            // get array mem end low byte
      ldx   earryh            // get array mem end high byte
      sta   histrl            // save as highest string low byte
      stx   histrh            // save as highest string high byte
      lda   #des_sk           // set descriptor stack pointer
      sta   ut1_pl            // save descriptor stack pointer low byte
      sty   ut1_ph            // save descriptor stack pointer high byte ($00)
      ldx   #1                // descriptor-stack walk is C64 ZP
      stx   gcDom
lab_2161:
      cmp   next_s            // compare with descriptor stack pointer
      beq   lab_216a          // branch if =

      jsr   lab_21d7          // go garbage collect descriptor stack
      beq   lab_2161          // loop always

                              // done stacked strings, now do string vars
lab_216a:
      lda   #0                // var and array walks are REU file offsets
      sta   gcDom
      asl   g_step            // set step size = $06
      lda   svarl             // get start of vars low byte
      ldx   svarh             // get start of vars high byte
      sta   ut1_pl            // save as pointer low byte
      stx   ut1_ph            // save as pointer high byte
lab_2176:
      cpx   sarryh            // compare start of arrays high byte
      bne   lab_217e          // branch if no high byte match

      cmp   sarryl            // else compare start of arrays low byte
      beq   lab_2183          // branch if = var mem end

lab_217e:
      jsr   lab_21d1          // go garbage collect strings
      beq   lab_2176          // loop always

                              // done string vars, now do string arrays
lab_2183:
      sta   nbendl            // save start of arrays low byte as working pointer
      stx   nbendh            // save start of arrays high byte as working pointer
      lda   #$04              // set step size
      sta   g_step            // save step size
lab_218b:
      lda   nbendl            // get pointer low byte
      ldx   nbendh            // get pointer high byte
lab_218f:
      cpx   earryh            // compare with array mem end high byte
      bne   lab_219a          // branch if not at end

      cmp   earryl            // else compare with array mem end low byte
      bne   lab_219a          // not at end
      jmp   lab_2216          // tidy up and exit if at end

lab_219a:
      sta   ut1_pl            // save pointer low byte
      stx   ut1_ph            // save pointer high byte
      ldy   #$02              // set index
      jsr   peekVarByte       // get array size low byte
      clc                     // size add must not inherit the loop's carry
      adc   nbendl            // add start of this array low byte
      sta   nbendl            // save start of next array low byte
      iny                     // increment index
      jsr   peekVarByte       // get array size high byte
      adc   nbendh            // add start of this array high byte
      sta   nbendh            // save start of next array high byte
      ldy   #$01              // set index
      jsr   peekVarByte       // get name second byte
      bpl   lab_218b          // skip if not string array

// was string array so ..

      ldy   #$04              // set index
      jsr   peekVarByte       // get # of dimensions
      asl                     // *2
      adc   #$05              // +5 (array header size)
      jsr   lab_2208          // go set up for first element
lab_21c4:
      cpx   nbendh            // compare with start of next array high byte
      bne   lab_21cc          // branch if <> (go do this array)

      cmp   nbendl            // else compare element pointer low byte with next array
                              // low byte
      beq   lab_218f          // if equal then go do next array

lab_21cc:
      jsr   lab_21d7          // go defrag array strings
      beq   lab_21c4          // go do next array string (loop always)

// defrag string variables
// enter with xa = variable pointer
// return with xa = next variable pointer

lab_21d1:
      iny                     // increment index (y was $00)
      jsr   peekVarByte       // get var name byte 2
      bpl   lab_2206          // if not string, step pointer to next var and return

      iny                     // else increment index
lab_21d7:
      jsr   gcRead            // get string length (ZP stack or REU var/array)
      beq   lab_2206          // if null, step pointer to next string and return

      iny                     // else increment index
      jsr   gcRead            // get string pointer low byte
      tax                     // copy to x
      iny                     // increment index
      jsr   gcRead            // get string pointer high byte
      cmp   sstorh            // compare bottom of string space high byte
      bcc   lab_21ec          // branch if less

      bne   lab_2206          // if greater, step pointer to next string and return

                              // high bytes were = so compare low bytes
      cpx   sstorl            // compare bottom of string space low byte
      bcs   lab_2206          // if >=, step pointer to next string and return

                              // string pointer is < string storage pointer (pos in mem)
lab_21ec:
      cmp   histrh            // compare to highest string high byte
      bcc   lab_2207          // if <, step pointer to next string and return

      bne   lab_21f6          // if > update pointers, step to next and return

                              // high bytes were = so compare low bytes
      cpx   histrl            // compare to highest string low byte
      bcc   lab_2207          // if <, step pointer to next string and return

                              // string is in string memory space
lab_21f6:
      stx   histrl            // save as new highest string low byte
      sta   histrh            // save as new highest string high byte
      lda   gcDom             // remember where the descriptor itself lives
      sta   garbDom
      lda   ut1_pl            // get start of vars(descriptors) low byte
      ldx   ut1_ph            // get start of vars(descriptors) high byte
      sta   garb_l            // save as working pointer low byte
      stx   garb_h            // save as working pointer high byte
      dey                     // decrement index differs
      dey                     // decrement index (should point to descriptor start)
      sty   g_indx            // save index pointer

                              // step pointer to next string
lab_2206:
      clc                     // clear carry for add
lab_2207:
      lda   g_step            // get step size
lab_2208:
      adc   ut1_pl            // add pointer low byte
      sta   ut1_pl            // save pointer low byte
      bcc   lab_2211          // branch if no overflow

      inc   ut1_ph            // else increment high byte
lab_2211:
      ldx   ut1_ph            // get pointer high byte
      ldy   #$00              // clear y
      rts

// search complete, now either exit or set-up and move string

lab_2216:
      dec   g_step            // decrement step size (now $03 for descriptor stack)
// *** begin patch  2.22p5.5  garbage collection may overlap temporary strings
// *** replace
//      ldx   garb_h            // get string to move high byte
// *** with
      lda   garb_h            // any string to move?
      ora   garb_l
// *** end   patch  2.22p5.5  garbage collection may overlap temporary strings
      beq   lab_2211          // exit if nothing to move

      ldy   g_indx            // get index byte back (points to descriptor)
      clc                     // clear carry for add
      lda   garbDom           // read/update the descriptor in its own domain
      sta   gcDom
      lda   garb_l            // wrapper base = string to move
      sta   ut1_pl
      lda   garb_h
      sta   ut1_ph
      jsr   gcRead            // get string length
      adc   histrl            // add highest string low byte
      sta   obendl            // save old block end low pointer
      lda   histrh            // get highest string high byte
      adc   #$00              // add any carry
      sta   obendh            // save old block end high byte
      lda   sstorl            // get bottom of string space low byte
      ldx   sstorh            // get bottom of string space high byte
      sta   nbendl            // save new block end low byte
      stx   nbendh            // save new block end high byte
      jsr   copyBlockNC       // open up space in memory, don't set array end
      ldy   g_indx            // get index byte
      iny                     // point to descriptor low byte
      lda   nbendl            // get string pointer low byte
      jsr   gcWrite           // save new string pointer low byte
      inc   nbendh            // correct high byte (move sets high byte -1)
      lda   nbendh            // get new string pointer high byte
      iny                     // point to descriptor high byte
      jsr   gcWrite           // save new string pointer high byte (clobbers X)
      ldx   nbendl            // X = new string pointer low byte -> lab_214b sstorl
      jmp   lab_214b          // re-run routine from last ending
                              // (but don't collect this string)

// concatenate
// add strings, string 1 is in descriptor des_pl, string 2 is in line

lab_224d:
      lda   des_ph            // get descriptor pointer high byte
      pha                     // put on stack
      lda   des_pl            // get descriptor pointer low byte
      pha                     // put on stack
      lda   desDom            // stash descriptor 1 domain for ssptr
      sta   ssDom
// *** begin patch  2.22p5.4  concatenate minus or not() crashes ehbasic  ***
// *** add extra label to verify originating function
lab_224da:
// *** end patch    2.22p5.4  concatenate minus or not() crashes ehbasic  ***
      jsr   lab_gval          // get value from line
      jsr   lab_ctst          // check if source is string, else do type mismatch
      pla                     // get descriptor pointer low byte back
      sta   ssptr_l           // set pointer low byte
      pla                     // get descriptor pointer high byte back
      sta   ssptr_h           // set pointer high byte
      lda   ssptr_l           // wrapper base = descriptor 1
      sta   ut1_pl
      lda   ssptr_h
      sta   ut1_ph
      lda   des_pl            // second wrapper base = descriptor 2
      sta   varPtr
      lda   des_ph
      sta   varPtr+1
      ldy   #$00              // clear index
      jsr   descRdS           // get length_1 from descriptor 1 (ssDom)
      sta   tempb             // stash it
      jsr   descRd2           // get length_2 from descriptor 2 (desDom)
      clc                     // clear carry for add
      adc   tempb             // add length_1
      bcc   lab_226d          // branch if no overflow

      ldx   #$1a              // else set error code $1a ("string too long" error)
      jmp   lab_xerr          // do error #x, then warm start

lab_226d:
      jsr   lab_209c          // copy des_pl/h to des_2l/h and make string space a bytes
                              // long
      jsr   lab_228a          // copy string from descriptor (sdescr) to (sutill)
      lda   des_2l            // get descriptor pointer low byte
      ldy   des_2h            // get descriptor pointer high byte
      jsr   lab_22ba          // pop descriptor 2 (domain = desDom via lab_209c)
                              // returns with a = length, ut1_pl = pointer low byte,
                              // ut1_ph = pointer high byte
      jsr   copyString        // store string a bytes long from (ut1_pl) to (sutill)
      lda   ssptr_l           //.set descriptor pointer low byte
      ldy   ssptr_h           //.set descriptor pointer high byte
      lda   ssDom             // descriptor 1 domain
      sta   desDom
      lda   ssptr_l           // lab_22ba pops (A,Y) -- restore pointer low byte
      jsr   lab_22ba          // pop descriptor 1
                              // returns with a = length, x=ut1_pl=pointer low byte,
                              // y=ut1_ph=pointer high byte
      jsr   lab_rtst          // check for space on descriptor stack then put string
                              // address and length on descriptor stack and update stack
                              // pointers
      jmp   lab_1adb          //.continue evaluation

// copy string from descriptor (sdescr) to (sutill)

lab_228a:
      lda   sdescr            // wrapper base = descriptor
      sta   ut1_pl
      lda   sdescr+1
      sta   ut1_ph
      ldy   #$00              // clear index
      jsr   descRdS           // get string length (descriptor 1 domain)
      pha                     // save on stack
      iny                     // increment index
      jsr   descRdS           // get source string pointer low byte (descriptor 1)
      tax                     // copy to x
      iny                     // increment index
      jsr   descRdS           // get source string pointer high byte (descriptor 1)
      tay                     // copy to y
      pla                     // get length back

// store string a bytes long from yx to (sutill)

lab_2298:
      stx   ut1_pl            // save source string pointer low byte
      sty   ut1_ph            // save source string pointer high byte

// store string a bytes long from (ut1_pl) to (sutill)

lab_229c:
      jmp   copyString        // forward copy, advance sutill (wrapper)

// evaluate string

lab_evst:
      jsr   lab_ctst          // check if source is string, else do type mismatch

// pop string off descriptor stack, or from top of string space
// returns with a = length, x=pointer low byte, y=pointer high byte

lab_22b6:
      lda   des_pl            // get descriptor pointer low byte
      ldy   des_ph            // get descriptor pointer high byte

// pop (ya) descriptor off stack or from top of string space
// returns with a = length, x=ut1_pl=pointer low byte, y=ut1_ph=pointer high byte

lab_22ba:
      sta   ut1_pl            // save descriptor pointer low byte
      sty   ut1_ph            // save descriptor pointer high byte
      lda   desDom            // REU descriptors are never stack entries
      beq   lab_22bax
      lda   ut1_pl            // lab_22eb compares A,Y against last_sl,last_sh
      jsr   lab_22eb          // clean descriptor stack (Z=1 if it was the top)
      jmp   lab_22bay
lab_22bax:
      lda   #1                // never the stack top (Z=0)
lab_22bay:
      php                     // save stack-top flag
      ldy   #$00              // clear index
      jsr   descRd            // get string length (desDom domain)
      pha                     // put on stack
      iny                     // increment index
      jsr   descRd            // get string pointer low byte from descriptor
      tax                     // copy to x
      iny                     // increment index
      jsr   descRd            // get string pointer high byte from descriptor
      tay                     // copy to y
      pla                     // get string length back
      plp                     // restore status
      bne   lab_22e6          // branch if pointer <> last_sl,last_sh

      cpy   sstorh            // compare bottom of string space high byte
      bne   lab_22e6          // branch if <>

      cpx   sstorl            // else compare bottom of string space low byte
      bne   lab_22e6          // branch if <>

      pha                     // save string length
      clc                     // clear carry for add
      adc   sstorl            // add bottom of string space low byte
      sta   sstorl            // save bottom of string space low byte
      bcc   lab_22e5          // skip increment if no overflow

      inc   sstorh            // increment bottom of string space high byte
lab_22e5:
      pla                     // restore string length
lab_22e6:
      stx   ut1_pl            // save string pointer low byte
      sty   ut1_ph            // save string pointer high byte
      rts

// clean descriptor stack, ya = pointer
// checks if ay is on the descriptor stack, if so does a stack discard

lab_22eb:
      cpy   last_sh           // compare pointer high byte
      bne   lab_22fb          // exit if <>

      cmp   last_sl           // compare pointer low byte
      bne   lab_22fb          // exit if <>

      sta   next_s            // save descriptor stack pointer
      sbc   #$03              // -3
      sta   last_sl           // save low byte -3
      ldy   #$00              // clear high byte
lab_22fb:
      rts

// perform chr$()

lab_chrs:
      jsr   lab_evby          // evaluate byte expression, result in x
      txa                     // copy to a
      pha                     // save character
      lda   #$01              // string is single byte
      jsr   lab_mssp          // make string space a bytes long a=$ac=length,
                              // x=$ad=sutill=ptr low byte, y=$ae=sutilh=ptr high byte
      lda   str_pl            // wrapper base = string (before pla -- A holds the char)
      sta   ut1_pl
      lda   str_ph
      sta   ut1_ph
      pla                     // get character back
      ldy   #$00              // clear index
      jsr   writeStrByte      // save byte in string (byte is string!)
      jmp   lab_rtst          // check for space on descriptor stack then put string
                              // address and length on descriptor stack and update stack
                              // pointers

// perform left$()

lab_left:
      pha                     // push byte parameter
      jsr   lab_236f          // pull string data and byte parameter from stack
                              // return pointer in des_2l/h, byte in a (and x), y=0
      sta   tempb             // stash byte parameter
      lda   des_2l            // wrapper base = descriptor
      sta   ut1_pl
      lda   des_2h
      sta   ut1_ph
      jsr   descRdD2          // get string length (descriptor 2 domain)
      sta   varPtr            // stash length
      lda   tempb             // get byte parameter back
      cmp   varPtr            // compare byte parameter with string length
      tya                     // clear a
      beq   lab_2316          // go do string copy (branch always)

// perform right$()

lab_right:
      pha                     // push byte parameter
      jsr   lab_236f          // pull string data and byte parameter from stack
                              // return pointer in des_2l/h, byte in a (and x), y=0
      sta   tempb             // stash byte parameter
      lda   des_2l            // wrapper base = descriptor
      sta   ut1_pl
      lda   des_2h
      sta   ut1_ph
      jsr   descRdD2          // get string length (descriptor 2 domain)
      sta   varPtr            // stash length
      lda   tempb             // get byte parameter back
      clc                     // clear carry for add-1
      sbc   varPtr            // subtract string length
      eor   #$ff              // invert it (a=len(expression$)-l)

lab_2316:
      bcc   lab_231c          // branch if string length > byte parameter

      jsr   descRdD2          // else make parameter = length (descriptor 2 domain)
      tax                     // copy to byte parameter copy
      tya                     // clear string start offset
lab_231c:
      pha                     // save string start offset
lab_231d:
      txa                     // copy byte parameter (or string length if <)
lab_231e:
      pha                     // save string length
      jsr   lab_mssp          // make string space a bytes long a=$ac=length,
                              // x=$ad=sutill=ptr low byte, y=$ae=sutilh=ptr high byte
      lda   des_2l            // get descriptor pointer low byte
      ldy   des_2h            // get descriptor pointer high byte
      jsr   lab_22ba          // pop (ya) descriptor off stack or from top of string space
                              // returns with a = length, x=ut1_pl=pointer low byte,
                              // y=ut1_ph=pointer high byte
      pla                     // get string length back
      tay                     // copy length to y
      pla                     // get string start offset back
      clc                     // clear carry for add
      adc   ut1_pl            // add start offset to string start pointer low byte
      sta   ut1_pl            // save string start pointer low byte
      bcc   lab_2335          // branch if no overflow

      inc   ut1_ph            // else increment string start pointer high byte
lab_2335:
      tya                     // copy length to a
      jsr   lab_229c          // store string a bytes long from (ut1_pl) to (sutill)
      jmp   lab_rtst          // check for space on descriptor stack then put string
                              // address and length on descriptor stack and update stack
                              // pointers

// perform mid$()

lab_mids:
      pha                     // push byte parameter
      lda   #$ff              // set default length = 255
      sta   mids_l            // save default length
      jsr   lab_gbyt          // scan memory
      cmp   #')'              // compare with ")"
      beq   lab_2358          // branch if = ")" (skip second byte get)

      jsr   lab_1c01          // scan for "," , else do syntax error then warm start
      jsr   lab_gtby          // get byte parameter (use copy in mids_l)
lab_2358:
      jsr   lab_236f          // pull string data and byte parameter from stack
                              // return pointer in des_2l/h, byte in a (and x), y=0
      dex                     // decrement start index
      txa                     // copy to a
      pha                     // save string start offset
      sta   tempb             // stash start index
      lda   des_2l            // wrapper base = descriptor
      sta   ut1_pl
      lda   des_2h
      sta   ut1_ph
      jsr   descRdD2          // get string length (descriptor 2 domain)
      sta   varPtr            // stash length
      lda   tempb             // get start index back
      clc                     // clear carry for sub-1
      ldx   #$00              // clear output string length
      sbc   varPtr            // subtract string length
      bcs   lab_231d          // if start>string length go do null string

      eor   #$ff              // complement -length
      cmp   mids_l            // compare byte parameter
      bcc   lab_231e          // if length>remaining string go do right$

      lda   mids_l            // get length byte
      bcs   lab_231e          // go do string copy (branch always)

// pull string data and byte parameter from stack
// return pointer in des_2l/h, byte in a (and x), y=0

lab_236f:
      jsr   lab_1bfb          // scan for ")" , else do syntax error then warm start
      pla                     // pull return address low byte (return address)
      sta   fnxjpl            // save functions jump vector low byte
      pla                     // pull return address high byte (return address)
      sta   fnxjph            // save functions jump vector high byte
      pla                     // pull byte parameter
      tax                     // copy byte parameter to x
      pla                     // pull string pointer low byte
      sta   des_2l            // save it
      pla                     // pull string pointer high byte
      sta   des_2h            // save it
      lda   desDom            // pulled descriptor = des_pl copy -- inherit domain
      sta   des2Dom
      ldy   #$00              // clear index
      txa                     // copy byte parameter
      bne   lab_236f_go       // non-zero -> proceed (lab_23a8 too far to branch)
      jmp   lab_23a8          // if null do function call error then warm start
lab_236f_go:
      inc   fnxjpl            // increment function jump vector low byte
                              // (jsr pushes return addr-1. this is all very nice
                              // but will go tits up if either call is on a page
                              // boundary!)
      jmp   (fnxjpl)          // in effect, rts

// perform lcase$()

lab_lcase:
      jsr   lab_evst          // evaluate string
      sta   str_ln            // set string length
      tay                     // copy length to y
      beq   nostring          // branch if null string

      jsr   lab_mssp          // make string space a bytes long a=length,
                              // x=sutill=ptr low byte, y=sutilh=ptr high byte
      stx   str_pl            // save string pointer low byte
      sty   str_ph            // save string pointer high byte
      stx   varPtr            // second wrapper base = destination
      sty   varPtr+1
      tay                     // get string length back

lc_loop:
      dey                     // decrement index
      jsr   readStrByte       // get byte from string
      jsr   lab_1d82          // is character "a" to "z"
      bcc   noucase           // branch if not upper case alpha

      ora   #$20              // convert upper to lower case
noucase:
      jsr   writeStrByte2     // save byte back to string
      tya                     // test index
      bne   lc_loop           // loop if not all done

      beq   nostring          // tidy up and exit, branch always

// perform ucase$()

lab_ucase:
      jsr   lab_evst          // evaluate string
      sta   str_ln            // set string length
      tay                     // copy length to y
      beq   nostring          // branch if null string

      jsr   lab_mssp          // make string space a bytes long a=length,
                              // x=sutill=ptr low byte, y=sutilh=ptr high byte
      stx   str_pl            // save string pointer low byte
      sty   str_ph            // save string pointer high byte
      stx   varPtr            // second wrapper base = destination
      sty   varPtr+1
      tay                     // get string length back

uc_loop:
      dey                     // decrement index
      jsr   readStrByte       // get byte from string
      jsr   lab_casc          // is character "a" to "z" (or "a" to "z")
      bcc   nolcase           // branch if not alpha

      and   #$df              // convert lower to upper case
nolcase:
      jsr   writeStrByte2     // save byte back to string
      tya                     // test index
      bne   uc_loop           // loop if not all done

nostring:
      jmp   lab_rtst          // check for space on descriptor stack then put string
                              // address and length on descriptor stack and update stack
                              // pointers

// perform len()

lab_lens:
      jsr   lab_esgl          // evaluate string, get length in a (and y)
      jmp   lab_1fd0          // convert y to byte in fac1 and return

// evaluate string, get length in y

lab_esgl:
      jsr   lab_evst          // evaluate string
      tay                     // copy length to y
      rts

// perform asc()

lab_asc:
      jsr   lab_esgl          // evaluate string, get length in a (and y)
      beq   lab_23a8          // if null do function call error then warm start

      ldy   #$00              // set index to first character
      jsr   readStrByte       // get byte
      tay                     // copy to y
      jmp   lab_1fd0          // convert y to byte in fac1 and return

// do function call error then warm start

lab_23a8:
      jmp   lab_fcer          // do function call error then warm start

// scan and get byte parameter

lab_sgby:
      jsr   lab_igby          // increment and scan memory

// get byte parameter

lab_gtby:
      jsr   lab_evnm          // evaluate expression and check is numeric,
                              // else do type mismatch

// evaluate byte expression, result in x

lab_evby:
      jsr   lab_evpi          // evaluate integer expression (no check)

      ldy   fac1_2            // get fac1 mantissa2
      bne   lab_23a8          // if top byte <> 0 do function call error then warm start

      ldx   fac1_3            // get fac1 mantissa3
      jmp   lab_gbyt          // scan memory and return

// perform val()

lab_val:
      jsr   lab_esgl          // evaluate string, get length in a (and y)
      bne   lab_23c5          // branch if not null string

                              // string was null so set result = $00
      jmp   lab_24f1          // clear fac1 exponent and sign and return

lab_23c5:
// *** begin patch  2.22p5.7  val() may cause string variables to be trashed
// *** replace     
//      ldx   bpntrl            // get basic execute pointer low byte
//      ldy   bpntrh            // get basic execute pointer high byte
//      stx   btmpl             // save basic execute pointer low byte
//      sty   btmph             // save basic execute pointer high byte
//      ldx   ut1_pl            // get string pointer low byte
//      stx   bpntrl            // save as basic execute pointer low byte
//      clc                     // clear carry
//      adc   ut1_pl            // add string length
//      sta   ut2_pl            // save string end low byte
//      lda   ut1_ph            // get string pointer high byte
//      sta   bpntrh            // save as basic execute pointer high byte
//      adc   #$00              // add carry to high byte
//      sta   ut2_ph            // save string end high byte
//      ldy   #$00              // set index to $00
//      lda   (ut2_pl),y        // get string end +1 byte
//      pha                     // push it
//      tya                     // clear a
//      sta   (ut2_pl),y        // terminate string with $00
//      jsr   lab_gbyt          // scan memory
//      jsr   lab_2887          // get fac1 from string
//      pla                     // restore string end +1 byte
//      ldy   #$00              // set index to zero
//      sta   (ut2_pl),y        // put string end byte back
// *** with
      pha                     // save length
      iny                     // string length +1
      tya
      jsr   lab_mssp          // allocate temp string +1 bytes long
      pla                     // get length back
      jsr   copyString        // copy string (ut1_pl) -> (sutill) for a bytes
      lda   sutill            // wrapper base = temp string
      sta   ut1_pl
      lda   sutilh
      sta   ut1_ph
      lda   #0                // add delimiter to end of string
      tay
      jsr   writeStrByte
      ldx   bpntrl            // save basic execute pointer low byte
      ldy   bpntrh
      stx   btmpl
      sty   btmph
      ldx   str_pl            // point to temporary string
      ldy   str_ph
      stx   bpntrl
      sty   bpntrh
      jsr   lab_gbyt          // scan memory
      jsr   lab_2887          // get fac1 from string
// *** end patch    2.22p5.7  val() may cause string variables to be trashed

// restore basic execute pointer from temp (btmpl/btmph)

lab_23f3:
      ldx   btmpl             // get basic execute pointer low byte back
      ldy   btmph             // get basic execute pointer high byte back
      stx   bpntrl            // save basic execute pointer low byte
      sty   bpntrh            // save basic execute pointer high byte
      rts

// get two parameters for poke or wait

lab_gadb:
      jsr   lab_evnm          // evaluate expression and check is numeric,
                              // else do type mismatch
      jsr   lab_f2fx          // save integer part of fac1 in temporary integer

// scan for "," and get byte, else do syntax error then warm start

lab_scgb:
      jsr   lab_1c01          // scan for "," , else do syntax error then warm start
      lda   itemph            // save temporary integer high byte
      pha                     // on stack
      lda   itempl            // save temporary integer low byte
      pha                     // on stack
      jsr   lab_gtby          // get byte parameter
      pla                     // pull low byte
      sta   itempl            // restore temporary integer low byte
      pla                     // pull high byte
      sta   itemph            // restore temporary integer high byte
      rts

// convert float to fixed routine. accepts any value that fits in 24 bits, +ve or
// -ve and converts it into a right truncated integer in itempl and itemph

// save unsigned 16 bit integer part of fac1 in temporary integer

lab_f2fx:
      lda   fac1_e            // get fac1 exponent
      cmp   #$98              // compare with exponent = 2^24
      bcs   lab_23a8          // if >= do function call error then warm start

lab_f2fu:
      jsr   lab_2831          // convert fac1 floating-to-fixed
      lda   fac1_2            // get fac1 mantissa2
      ldy   fac1_3            // get fac1 mantissa3
      sty   itempl            // save temporary integer low byte
      sta   itemph            // save temporary integer high byte
      rts

// perform peek()

lab_peek:
      jsr   lab_f2fx          // save integer part of fac1 in temporary integer
      ldx   #$00              // clear index
      lda   (itempl,x)        // get byte via temporary integer (addr)
      tay                     // copy byte to y
      jmp   lab_1fd0          // convert y to byte in fac1 and return

// perform poke

lab_poke:
      jsr   lab_gadb          // get two parameters for poke or wait
      txa                     // copy byte argument to a
      ldx   #$00              // clear index
      sta   (itempl,x)        // save byte via temporary integer (addr)
      rts

// perform swap

lab_swap:
      jsr   lab_gvar          // get var1 address
      sta   lvarpl            // save var1 address low byte
      sty   lvarph            // save var1 address high byte
      lda   dtypef            // get data type flag, $ff=string, $00=numeric
      pha                     // save data type flag

      jsr   lab_1c01          // scan for "," , else do syntax error then warm start
      jsr   lab_gvar          // get var2 address (pointer in cvaral/h)
      pla                     // pull var1 data type flag
      eor   dtypef            // compare with var2 data type
      bmi   swaperr           // exit if not both the same type ($ff=diff)

      ldy   #$03              // four bytes to swap (either value or descriptor+1)
      lda   lvarpl            // wrapper base = var1
      sta   ut1_pl
      lda   lvarph
      sta   ut1_ph
      lda   cvaral            // second wrapper base = var2
      sta   varPtr
      lda   cvarah
      sta   varPtr+1
swaplp:
      jsr   peekVarByte       // get byte from var1
      sta   tempb             // save var1 byte
      jsr   peekVarByte2      // get byte from var2
      jsr   pokeVarByte       // save byte to var1
      lda   tempb             // restore var1 byte
      jsr   pokeVarByte2      // save byte to var2
      dey                     // decrement index
      bpl   swaplp            // loop until done

      rts

swaperr:
      jmp   lab_1abc          // do "type mismatch" error then warm start

// perform call

lab_call:
      jsr   lab_evnm          // evaluate expression and check is numeric,
                              // else do type mismatch
      jsr   lab_f2fx          // convert floating-to-fixed
      lda   #>callexit        // set return address high byte
      pha                     // put on stack
      lda   #<callexit-1      // set return address low byte
      pha                     // put on stack
      jmp   (itempl)          // do indirect jump to user routine

// if the called routine exits correctly then it will return to here. this will then get
// the next byte for the interpreter and return

callexit:
      jmp   lab_gbyt          // scan memory and return

// perform wait

lab_wait:
      jsr   lab_gadb          // get two parameters for poke or wait
      stx   frnxtl            // save byte
      ldx   #$00              // clear mask
      jsr   lab_gbyt          // scan memory
      beq   lab_2441          // skip if no third argument

      jsr   lab_scgb          // scan for "," and get byte, else sn error then warm start
lab_2441:
      stx   frnxth            // save eor argument
lab_2445:
      lda   (itempl),y        // get byte via temporary integer (addr)
      eor   frnxth            // eor with second argument (mask)
      and   frnxtl            // and with first argument (byte)
      beq   lab_2445          // loop if result is zero

lab_244d:
      rts

// perform subtraction, fac1 from (ay)

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

// add 0.5 to fac1

lab_244e:
      lda   #<lab_2a96        // set 0.5 pointer low byte
      ldy   #>lab_2a96        // set 0.5 pointer high byte

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
      beq   lab_2564          // if zero do overflow error and warm start

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

// do overflow error (overflow exit)

lab_2564:
      ldx   #$0a              // error code $0a ("overflow" error)
      jmp   lab_xerr          // do error #x, then warm start

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

// perform log()

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
// *** begin patch  2.22p5.6  floating point multiply rounding bug
// *** replace
//      jmp   lab_2569          // shift fcatemp << a+8 times
//
//                              // else do shift and add
//lab_2627
//      lsr                     // shift byte
//      ora   #$80              // set top bit (mark for 8 times)
// *** with
      sec
      jmp   lab_2569          // shift factemp << a+8 times

                              // else do shift and add
lab_2627:
      sec                     // set top bit (mark for 8 times)
      ror
// *** end patch    2.22p5.6  floating point multiply rounding bug
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
      jmp   lab_2564          // do overflow error and warm start

// divide by 10

lab_26b9:
      jsr   lab_27ab          // round and copy fac1 to fac2
      lda   #<lab_26b5        // set pointer to 10d low addr
      ldy   #>lab_26b5        // set pointer to 10d high addr
      ldx   #$00              // clear sign

// divide by (ay) (x=sign)

lab_26c2:
      stx   fac_sc            // save sign compare (fac1 eor fac2)
      jsr   lab_ufac          // unpack memory (ay) into fac1
      jmp   lab_divide        // do fac2/fac1

                              // perform divide-by
// convert ay and do (ay)/fac1

lab_divide:
      beq   lab_2737          // if zero go do /0 error

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

lab_2737:
      ldx   #$14              // error code $14 ("divide by zero" error)
      jmp   lab_xerr          // do error #x, then warm start

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

// pack fac1 into adatal

lab_2770:
      ldy   #>adatal          // set pointer high byte
      beq   lab_2778          // pack fac1 into (xy) and return

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

      lda   (ut2_pl),y        // get (ay) mantissa1 (with sign)
      ora   #$80              // normalise top bit
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

lab_287f:
      sta   fac1_1            // clear fac1 mantissa1
      sta   fac1_2            // clear fac1 mantissa2
      sta   fac1_3            // clear fac1 mantissa3
      tay                     // clear y
lab_2886:
      rts

// get fac1 from string
// this routine now handles hex and binary values from strings
// starting with "$" and "%" respectively

lab_2887:
      ldy   #$00              // clear y
      sty   dtypef            // clear data type flag, $ff=string, $00=numeric
      ldx   #$09              // set index
lab_288b:
      sty   numexp,x          // clear byte
      dex                     // decrement index
      bpl   lab_288b          // loop until numexp to negnum (and fac1) = $00

      bcc   lab_28fe          // branch if 1st character numeric

// get fac1 from string .. first character wasn't numeric

      cmp   #'-'              // else compare with "-"
      bne   lab_289a          // branch if not "-"

      stx   negnum            // set flag for -ve number (x = $ff)
      beq   lab_289c          // branch always (go scan and check for hex/bin)

// get fac1 from string .. first character wasn't numeric or -

lab_289a:
      cmp   #'+'              // else compare with "+"
      bne   lab_289d          // branch if not "+" (go check for hex/bin)

// was "+" or "-" to start, so get next character

lab_289c:
      jsr   lab_igby          // increment and scan memory
      bcc   lab_28fe          // branch if numeric character

// code here for hex and binary numbers

lab_289d:
      cmp   #'$'              // else compare with "$"
      bne   lab_nhex          // branch if not "$"

      jmp   lab_chex          // branch if "$"

lab_nhex:
      cmp   #'%'              // else compare with "%"
      bne   lab_28a3          // branch if not "%" (continue original code)

      jmp   lab_cbin          // branch if "%"

lab_289e:
      jsr   lab_igby          // increment and scan memory (ignore + or get next number)
lab_28a1:
      bcc   lab_28fe          // branch if numeric character

// get fac1 from string .. character wasn't numeric, -, +, hex or binary

lab_28a3:
      cmp   #'.'              // else compare with "."
      beq   lab_28d5          // branch if "."

// get fac1 from string .. character wasn't numeric, -, + or .

      cmp   #'e'              // else compare with "e"
      bne   lab_28db          // branch if not "e"

                              // was "e" so evaluate exponential part
      jsr   lab_igby          // increment and scan memory
      bcc   lab_28c7          // branch if numeric character

      cmp   #tk_minus         // else compare with token for -
      beq   lab_28c2          // branch if token for -

      cmp   #'-'              // else compare with "-"
      beq   lab_28c2          // branch if "-"

      cmp   #tk_plus          // else compare with token for +
      beq   lab_28c4          // branch if token for +

      cmp   #'+'              // else compare with "+"
      beq   lab_28c4          // branch if "+"

      bne   lab_28c9          // branch always

lab_28c2:
      ror   expneg            // set exponent -ve flag (c, which=1, into b7)
lab_28c4:
      jsr   lab_igby          // increment and scan memory
lab_28c7:
      bcc   lab_2925          // branch if numeric character

lab_28c9:
      bit   expneg            // test exponent -ve flag
      bpl   lab_28db          // if +ve go evaluate exponent

                              // else do exponent = -exponent 
      lda   #$00              // clear result
      sec                     // set carry for subtract
      sbc   expcnt            // subtract exponent byte
      jmp   lab_28dd          // go evaluate exponent

lab_28d5:
      ror   numdpf            // set decimal point flag
      bit   numdpf            // test decimal point flag
      bvc   lab_289e          // branch if only one decimal point so far

                              // evaluate exponent
lab_28db:
      lda   expcnt            // get exponent count byte
lab_28dd:
      sec                     // set carry for subtract
      sbc   numexp            // subtract numerator exponent
      sta   expcnt            // save exponent count byte
      beq   lab_28f6          // branch if no adjustment

      bpl   lab_28ef          // else if +ve go do fac1*10^expcnt

                              // else go do fac1/10^(0-expcnt)
lab_28e6:
      jsr   lab_26b9          // divide by 10
      inc   expcnt            // increment exponent count byte
      bne   lab_28e6          // loop until all done

      beq   lab_28f6          // branch always

lab_28ef:
      jsr   lab_269e          // multiply by 10
      dec   expcnt            // decrement exponent count byte
      bne   lab_28ef          // loop until all done

lab_28f6:
      lda   negnum            // get -ve flag
      bmi   lab_28fb          // if -ve do - fac1 and return

      rts

// do - fac1 and return

lab_28fb:
      jmp   lab_gthan         // do - fac1 and return

// do unsigned fac1*10+number

lab_28fe:
      pha                     // save character
      bit   numdpf            // test decimal point flag
      bpl   lab_2905          // skip exponent increment if not set

      inc   numexp            // else increment number exponent
lab_2905:
      jsr   lab_269e          // multiply fac1 by 10
      pla                     // restore character
      and   #$0f              // convert to binary
      jsr   lab_2912          // evaluate new ascii digit
      jmp   lab_289e          // go do next character

// evaluate new ascii digit

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

// evaluate next character of exponential part of number

lab_2925:
      lda   expcnt            // get exponent count byte
      cmp   #$0a              // compare with 10 decimal
      bcc   lab_2934          // branch if less

      lda   #$64              // make all -ve exponents = -100 decimal (causes underflow)
      bit   expneg            // test exponent -ve flag
      bmi   lab_2942          // branch if -ve

      jmp   lab_2564          // else do overflow error

lab_2934:
      asl                     // * 2
      asl                     // * 4
      adc   expcnt            // * 5
      asl                     // * 10
      ldy   #$00              // set index
      pha                     // push accumulated value (carry preserved)
      jsr   textByteAtY       // get character (carry preserved)
      sta   varPtr            // stash character
      pla                     // restore accumulated value
      adc   varPtr            // add character (carry still from asl)
      sbc   #'0'-1            // convert character to binary
lab_2942:
      sta   expcnt            // save exponent count byte
      jmp   lab_28c4          // go get next character

// print " in line [line #]"

lab_2953:
      lda   #<lab_lmsg        // point to " in line " message low byte
      ldy   #>lab_lmsg        // point to " in line " message high byte
      jsr   lab_18c3          // print null terminated string from memory

                              // print basic line #
      lda   clineh            // get current line high byte
      ldx   clinel            // get current line low byte

// print xa as unsigned integer

lab_295e:
      sta   fac1_1            // save low byte as fac1 mantissa1
      stx   fac1_2            // save high byte as fac1 mantissa2
      ldx   #$90              // set exponent to 16d bits
      sec                     // set integer is +ve flag
      jsr   lab_stfa          // set exp=x, clearfac1 mantissa3 and normalise
      ldy   #$00              // clear index
      tya                     // clear a
      jsr   lab_297b          // convert fac1 to string, skip sign character save
      jmp   lab_18c3          // print null terminated string from memory and return

// convert fac1 to ascii string result in (ay)
// not any more, moved scratchpad to page 0

lab_296e:
      ldy   #$01              // set index = 1
      lda   #$20              // character = " " (assume +ve)
      bit   fac1_s            // test fac1 sign (b7)
      bpl   lab_2978          // branch if +ve

      lda   #$2d              // else character = "-"
lab_2978:
      sta   decss,y           // save leading character (" " or "-")
lab_297b:
      sta   fac1_s            // clear fac1 sign (b7)
      sty   sendl             // save index
      iny                     // increment index
      ldx   fac1_e            // get fac1 exponent
      bne   lab_2989          // branch if fac1<>0

                              // exponent was $00 so fac1 is 0
      lda   #'0'              // set character = "0"
      jmp   lab_2a89          // save last character, [eot] and exit

                              // fac1 is some non zero value
lab_2989:
      lda   #$00              // clear (number exponent count)
      cpx   #$81              // compare fac1 exponent with $81 (>1.00000)

      bcs   lab_299a          // branch if fac1=>1

                              // fac1<1
      lda   #<lab_294f        // set pointer low byte to 1,000,000
      ldy   #>lab_294f        // set pointer high byte to 1,000,000
      jsr   lab_25fb          // do convert ay, fca1*(ay)
      lda   #$fa              // set number exponent count (-6)
lab_299a:
      sta   numexp            // save number exponent count
lab_299c:
      lda   #<lab_294b        // set pointer low byte to 999999.4375 (max before sci note)
      ldy   #>lab_294b        // set pointer high byte to 999999.4375
      jsr   lab_27f8          // compare fac1 with (ay)
      beq   lab_29c3          // exit if fac1 = (ay)

      bpl   lab_29b9          // go do /10 if fac1 > (ay)

                              // fac1 < (ay)
lab_29a7:
      lda   #<lab_2947        // set pointer low byte to 99999.9375
      ldy   #>lab_2947        // set pointer high byte to 99999.9375
      jsr   lab_27f8          // compare fac1 with (ay)
      beq   lab_29b2          // branch if fac1 = (ay) (allow decimal places)

      bpl   lab_29c0          // branch if fac1 > (ay) (no decimal places)

                              // fac1 <= (ay)
lab_29b2:
      jsr   lab_269e          // multiply by 10
      dec   numexp            // decrement number exponent count
      bne   lab_29a7          // go test again (branch always)

lab_29b9:
      jsr   lab_26b9          // divide by 10
      inc   numexp            // increment number exponent count
      bne   lab_299c          // go test again (branch always)

// now we have just the digits to do

lab_29c0:
      jsr   lab_244e          // add 0.5 to fac1 (round fac1)
lab_29c3:
      jsr   lab_2831          // convert fac1 floating-to-fixed
      ldx   #$01              // set default digits before dp = 1
      lda   numexp            // get number exponent count
      clc                     // clear carry for add
      adc   #$07              // up to 6 digits before point
      bmi   lab_29d8          // if -ve then 1 digit before dp

      cmp   #$08              // a>=8 if n>=1e6
      bcs   lab_29d9          // branch if >= $08

                              // carry is clear
      adc   #$ff              // take 1 from digit count
      tax                     // copy to a
      lda   #$02              //.set exponent adjust
lab_29d8:
      sec                     // set carry for subtract
lab_29d9:
      sbc   #$02              // -2
      sta   expcnt            //.save exponent adjust
      stx   numexp            // save digits before dp count
      txa                     // copy to a
      beq   lab_29e4          // branch if no digits before dp

      bpl   lab_29f7          // branch if digits before dp

lab_29e4:
      ldy   sendl             // get output string index
      lda   #$2e              // character "."
      iny                     // increment index
      sta   decss,y           // save to output string
      txa                     //.
      beq   lab_29f5          //.

      lda   #'0'              // character "0"
      iny                     // increment index
      sta   decss,y           // save to output string
lab_29f5:
      sty   sendl             // save output string index
lab_29f7:
      ldy   #$00              // clear index (point to 100,000)
      ldx   #$80              // 
lab_29fb:
      lda   fac1_3            // get fac1 mantissa3
      clc                     // clear carry for add
      adc   lab_2a9a+2,y        // add -ve lsb
      sta   fac1_3            // save fac1 mantissa3
      lda   fac1_2            // get fac1 mantissa2
      adc   lab_2a9a+1,y        // add -ve nmsb
      sta   fac1_2            // save fac1 mantissa2
      lda   fac1_1            // get fac1 mantissa1
      adc   lab_2a9a,y        // add -ve msb
      sta   fac1_1            // save fac1 mantissa1
      inx                     // 
      bcs   lab_2a18          // 

      bpl   lab_29fb          // not -ve so try again

      bmi   lab_2a1a          // 

lab_2a18:
      bmi   lab_29fb          // 

lab_2a1a:
      txa                     // 
      bcc   lab_2a21          // 

      eor   #$ff              // 
      adc   #$0a              // 
lab_2a21:
      adc   #'0'-1            // add "0"-1 to result
      iny                     // increment index ..
      iny                     // .. to next less ..
      iny                     // .. power of ten
      sty   cvaral            // save as current var address low byte
      ldy   sendl             // get output string index
      iny                     // increment output string index
      tax                     // copy character to x
      and   #$7f              // mask out top bit
      sta   decss,y           // save to output string
      dec   numexp            // decrement # of characters before the dp
      bne   lab_2a3b          // branch if still characters to do

                              // else output the point
      lda   #$2e              // character "."
      iny                     // increment output string index
      sta   decss,y           // save to output string
lab_2a3b:
      sty   sendl             // save output string index
      ldy   cvaral            // get current var address low byte
      txa                     // get character back
      eor   #$ff              // 
      and   #$80              // 
      tax                     // 
      cpy   #$12              // compare index with max
      bne   lab_29fb          // loop if not max

                              // now remove trailing zeroes
      ldy   sendl             // get output string index
lab_2a4b:
      lda   decss,y           // get character from output string
      dey                     // decrement output string index
      cmp   #'0'              // compare with "0"
      beq   lab_2a4b          // loop until non "0" character found

      cmp   #'.'              // compare with "."
      beq   lab_2a58          // branch if was dp

                              // restore last character
      iny                     // increment output string index
lab_2a58:
      lda   #$2b              // character "+"
      ldx   expcnt            // get exponent count
      beq   lab_2a8c          // if zero go set null terminator and exit

                              // exponent isn't zero so write exponent
      bpl   lab_2a68          // branch if exponent count +ve

      lda   #$00              // clear a
      sec                     // set carry for subtract
      sbc   expcnt            // subtract exponent count adjust (convert -ve to +ve)
      tax                     // copy exponent count to x
      lda   #'-'              // character "-"
lab_2a68:
      sta   decss+2,y         // save to output string
      lda   #$45              // character "e"
      sta   decss+1,y         // save exponent sign to output string
      txa                     // get exponent count back
      ldx   #'0'-1            // one less than "0" character
      sec                     // set carry for subtract
lab_2a74:
      inx                     // increment 10's character
      sbc   #$0a              //.subtract 10 from exponent count
      bcs   lab_2a74          // loop while still >= 0

      adc   #':'              // add character ":" ($30+$0a, result is 10 less that value)
      sta   decss+4,y         // save to output string
      txa                     // copy 10's character
      sta   decss+3,y         // save to output string
      lda   #$00              // set null terminator
      sta   decss+5,y         // save to output string
      beq   lab_2a91          // go set string pointer (ay) and exit (branch always)

                              // save last character, [eot] and exit
lab_2a89:
      sta   decss,y           // save last character to output string

                              // set null terminator and exit
lab_2a8c:
      lda   #$00              // set null terminator
      sta   decss+1,y         // save after last character

                              // set string pointer (ay) and exit
lab_2a91:
      lda   #<decssp1         // set result string low pointer
      ldy   #>decssp1         // set result string high pointer
      rts

// perform power function

lab_gthan:
      lda   fac1_e            // get fac1 exponent
      beq   lab_2af9          // exit if fac1_e = $00

      lda   fac1_s            // get fac1 sign (b7)
      eor   #$ff              // complement it
      sta   fac1_s            // save fac1 sign (b7)
lab_2af9:
      rts

// perform exp()   (x^e)

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
      lda   #<lab_2afe        // set counter pointer low byte
      ldy   #>lab_2afe        // set counter pointer high byte
      jsr   lab_2b84          // go do series evaluation
      lda   #$00              // clear a
      sta   fac_sc            // clear sign compare (fac1 eor fac2)
      pla                     //.get saved fac2 exponent
      jmp   lab_2675          // test and adjust accumulators and return

// ^2 then series evaluation

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
      adc   #$04              // +4 to  low pointer (4 bytes per constant)
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

// rnd(n), 32 bit galoise version. make n=0 for 19th next number in sequence or n<>0
// to get 19th next number in sequence after seed n. this version of the prng uses
// the galois method and a sample of 65536 bytes produced gives the following values.

// entropy = 7.997442 bits per byte
// optimum compression would reduce these 65536 bytes by 0 percent

// chi square distribution for 65536 samples is 232.01, and
// randomly would exceed this value 75.00 percent of the time

// arithmetic mean value of data bytes is 127.6724, 127.5 would be random
// monte carlo value for pi is 3.122871269, error 0.60 percent
// serial correlation coefficient is -0.000370, totally uncorrelated would be 0.0

loopprn:
      asl   rbyte1            // shift prng most significant byte
      rol   rbyte2            // shift prng middle byte
      rol   rbyte3            // shift prng least significant byte
      rol   rbyte4            // shift prng extra byte
      bcc   ninc1             // branch if bit 32 clear

      txa                     // set eor byte
      eor   rbyte1            // eor prng extra byte
      sta   rbyte1            // save new prng extra byte
ninc1:
      dey                     // decrement loop count
      bne   loopprn           // loop if not all done

      ldx   #$02              // three bytes to copy
copyprng:
      lda   rbyte1,x          // get prng byte
      sta   fac1_1,x          // save fac1 byte
      dex
      bpl   copyprng          // loop if not complete

      lda   #$80              // set the exponent
      sta   fac1_e            // save fac1 exponent

      asl                     // clear a
      sta   fac1_s            // save fac1 sign

      jmp   lab_24d5          // normalise fac1 and return

// perform cos()

lab_bins:
      cpx   #$19              // max + 1
      bcs   binferr           // exit if too big ( > or = )

      stx   tempb             // save # of characters ($00 = leading zero remove)
      lda   #$18              // need a byte long space
      jsr   lab_mssp          // make string space a bytes long
      lda   str_pl            // wrapper base = temp string
      sta   ut1_pl
      lda   str_ph
      sta   ut1_ph
      ldy   #$17              // set index
      ldx   #$18              // character count
nextb1:
      lsr   nums_1            // shift highest byte
      ror   nums_2            // shift middle byte
      ror   nums_3            // shift lowest byte bit 0 to carry
      txa                     // load with "0"/2
      rol                     // shift in carry
      jsr   writeStrByte      // save to temp string + index
      dey                     // decrement index
      bpl   nextb1            // loop if not done

      lda   tempb             // get # of characters
      beq   endbhs            // branch if truncate

      tax                     // copy length to x
      sec                     // set carry for add !
      eor   #$ff              // 1's complement
      adc   #$18              // add 24d
      beq   gopr2             // if zero print whole string

      bne   gopr1             // else go make output string
      
// this is the exit code and is also used by hex$()
// truncate string to remove leading "0"s

endbhs:
      tay                     // clear index (a=0, x=length here)
nextb2:
      jsr   readStrByte       // get character from string
      cmp   #'0'              // compare with "0"
      bne   gopr              // if not "0" then go print string from here

      dex                     // decrement character count
      beq   gopr3             // if zero then end of string so go print it

      iny                     // else increment index
      bpl   nextb2            // loop always

// make fixed length output string - ignore overflows!

gopr3:
      inx                     // need at least 1 character
gopr:
      tya                     // copy result
gopr1:
      clc                     // clear carry for add
      adc   str_pl            // add low address
      sta   str_pl            // save low address
      lda   #$00              // do high byte
      adc   str_ph            // add high address
      sta   str_ph            // save high address
gopr2:
      stx   str_ln            // x holds string length
      jsr   lab_igby          // update basic execute pointer (to character past ")")
      jmp   lab_rtst          // check for space on descriptor stack then put address
                              // and length on descriptor stack and update stack pointers

binferr:
      jmp   lab_fcer          // do function call error then warm start

// perform hex$()

lab_hexs:
      cpx   #$07              // max + 1
      bcs   binferr           // exit if too big ( > or = )

      stx   tempb             // save # of characters

      lda   #$06              // need 6 bytes for string
      jsr   lab_mssp          // make string space a bytes long
      lda   str_pl            // wrapper base = temp string
      sta   ut1_pl
      lda   str_ph
      sta   ut1_ph
      ldy   #$05              // set string index

// *** disable decimal mode patch - comment next line ***
//      sed                     // need decimal mode for nibble convert
      lda   nums_3            // get lowest byte
      jsr   lab_a2hx          // convert a to ascii hex byte and output
      lda   nums_2            // get middle byte
      jsr   lab_a2hx          // convert a to ascii hex byte and output
      lda   nums_1            // get highest byte
      jsr   lab_a2hx          // convert a to ascii hex byte and output
// *** disable decimal mode patch - comment next line ***
//      cld                     // back to binary

      ldx   #$06              // character count
      lda   tempb             // get # of characters
      beq   endbhs            // branch if truncate

      tax                     // copy length to x
      sec                     // set carry for add !
      eor   #$ff              // 1's complement
      adc   #$06              // add 6d
      beq   gopr2             // if zero print whole string

      bne   gopr1             // else go make output string (branch always)

// convert a to ascii hex byte and output .. note set decimal mode before calling

lab_a2hx:
      tax                     // save byte
      and   #$0f              // mask off top bits
      jsr   lab_al2x          // convert low nibble to ascii and output
      txa                     // get byte back
      lsr                     // /2  shift high nibble to low nibble
      lsr                     // /4
      lsr                     // /8
      lsr                     // /16
lab_al2x:
      cmp   #$0a              // set carry for +1 if >9
// *** begin disable decimal mode patch ***
// *** insert
      bcc   lab_al20          // skip adjust if <= 9
      adc   #$06              // adjust for a to f
lab_al20:
// *** end   disable decimal mode patch ***
      adc   #'0'              // add ascii "0"
      jsr   writeStrByte      // save to temp string
      dey                     // decrement counter
      rts

lab_nlto:
      sta   fac1_e            // save fac1 exponent
      lda   #$00              // clear sign compare
lab_mlte:
      sta   fac_sc            // save sign compare (fac1 eor fac2)
      txa                     // restore character
      jsr   lab_2912          // evaluate new ascii digit

// gets here if the first character was "$" for hex
// get hex number

lab_chex:
      jsr   lab_igby          // increment and scan memory
      bcc   lab_ishn          // branch if numeric character

      ora   #$20              // case convert, allow "a" to "f" and "a" to "f"
      sbc   #'a'              // subtract "a" (carry set here)
      cmp   #$06              // compare normalised with $06 (max+1)
      bcs   lab_exch          // exit if >"f" or <"0"

      adc   #$0a              // convert to nibble
lab_ishn:
      and   #$0f              // convert to binary
      tax                     // save nibble
      lda   fac1_e            // get fac1 exponent
      beq   lab_mlte          // skip multiply if zero

      adc   #$04              // add four to exponent (*16 - carry clear here)
      bcc   lab_nlto          // if no overflow do evaluate digit

lab_mlto:
      jmp   lab_2564          // do overflow error and warm start

lab_nxch:
      tax                     // save bit
      lda   fac1_e            // get fac1 exponent
      beq   lab_mlbt          // skip multiply if zero

      inc   fac1_e            // increment fac1 exponent (*2)
      beq   lab_mlto          // do overflow error if = $00

      lda   #$00              // clear sign compare
lab_mlbt:
      sta   fac_sc            // save sign compare (fac1 eor fac2)
      txa                     // restore bit
      jsr   lab_2912          // evaluate new ascii digit

// gets here if the first character was  "%" for binary
// get binary number

lab_cbin:
      jsr   lab_igby          // increment and scan memory
      eor   #'0'              // convert "0" to 0 etc.
      cmp   #$02              // compare with max+1
      bcc   lab_nxch          // branch exit if < 2

lab_exch:
      jmp   lab_28f6          // evaluate -ve flag and return

// ctrl-c check routine. includes limited "life" byte save for inget routine
// now also the code that checks to see if an interrupt has occurred

ctrlc:
      lda   ccflag            // get [ctrl-c] check flag
      bne   lab_fba2          // exit if inhibited

      jsr   v_inpt            // scan input device
      bcc   lab_fba0          // exit if buffer empty

      sta   ccbyte            // save received byte
      ldx   #$20              // "life" timer for bytes
      stx   ccnull            // set countdown
      jmp   lab_1636          // return to basic

lab_fba0:
      ldx   ccnull            // get countdown byte
      beq   lab_fba2          // exit if finished

      dec   ccnull            // else decrement countdown
lab_fba2:
      rts

// get byte from input device, no waiting
// returns with carry set if byte in a

inget:
      jsr   v_inpt            // call scan input device
      bcs   lab_fb95          // if byte go reset timer

      lda   ccnull            // get countdown
      beq   lab_fb96          // exit if empty

      lda   ccbyte            // get last received byte
      sec                     // flag we got a byte
lab_fb95:
      ldx   #$00              // clear x
      stx   ccnull            // clear timer because we got a byte
lab_fb96:
      rts

// max() min() pre process

lab_mmpp:
      jsr   lab_evez          // process expression
      jmp   lab_ctnm          // check if source is numeric, else do type mismatch

// perform max()

lab_max:
      jsr   lab_phfa          // push fac1, evaluate expression,
                              // pull fac2 and compare with fac1
      bpl   lab_max           // branch if no swap to do

      lda   fac2_1            // get fac2 mantissa1
      ora   #$80              // set top bit (clear sign from compare)
      sta   fac2_1            // save fac2 mantissa1
      jsr   lab_279b          // copy fac2 to fac1
      beq   lab_max           // go do next (branch always)

// perform min()

lab_min:
      jsr   lab_phfa          // push fac1, evaluate expression,
                              // pull fac2 and compare with fac1
      bmi   lab_min           // branch if no swap to do

      beq   lab_min           // branch if no swap to do

      lda   fac2_1            // get fac2 mantissa1
      ora   #$80              // set top bit (clear sign from compare)
      sta   fac2_1            // save fac2 mantissa1
      jsr   lab_279b          // copy fac2 to fac1
      beq   lab_min           // go do next (branch always)

// exit routine. don't bother returning to the loop code
// check for correct exit, else so syntax error

lab_mmec:
      cmp   #')'              // is it end of function?
      bne   lab_mmse          // if not do max min syntax error

      pla                     // dump return address low byte
      pla                     // dump return address high byte
      jmp   lab_igby          // update basic execute pointer (to chr past ")")

lab_mmse:
      jmp   lab_sner          // do syntax error then warm start

// check for next, evaluate and return or exit
// this is the routine that does most of the work

lab_phfa:
      jsr   lab_gbyt          // get next basic byte
      cmp   #','              // is there more ?
      bne   lab_mmec          // if not go do end check

                              // push fac1
      jsr   lab_27ba          // round fac1
      lda   fac1_s            // get fac1 sign
      ora   #$7f              // set all non sign bits
      and   fac1_1            // and fac1 mantissa1 (and in sign bit)
      pha                     // push on stack
      lda   fac1_2            // get fac1 mantissa2
      pha                     // push on stack
      lda   fac1_3            // get fac1 mantissa3
      pha                     // push on stack
      lda   fac1_e            // get fac1 exponent
      pha                     // push on stack

      jsr   lab_igby          // scan and get next basic byte (after ",")
      jsr   lab_evnm          // evaluate expression and check is numeric,
                              // else do type mismatch

                              // pop fac2 (max/min expression so far)
      pla                     // pop exponent
      sta   fac2_e            // save fac2 exponent
      pla                     // pop mantissa3
      sta   fac2_3            // save fac2 mantissa3
      pla                     // pop mantissa1
      sta   fac2_2            // save fac2 mantissa2
      pla                     // pop sign/mantissa1
      sta   fac2_1            // save fac2 sign/mantissa1
      sta   fac2_s            // save fac2 sign

                              // compare fac1 with (packed) fac2
      lda   #<fac2_e          // set pointer low byte to fac2
      ldy   #>fac2_e          // set pointer high byte to fac2
      jmp   lab_27f8          // compare fac1 with fac2 (ay) and return
                              // returns a=$00 if fac1 = (ay)
                              // returns a=$01 if fac1 > (ay)
                              // returns a=$ff if fac1 < (ay)

// perform width

lab_wdth:
      cmp   #','              // is next byte ","
      beq   lab_tbsz          // if so do tab size

      jsr   lab_gtby          // get byte parameter
      txa                     // copy width to a
      beq   lab_nstt          // branch if set for infinite line

      cpx   #$10              // else make min width = 16d
      bcc   taberr            // if less do function call error and exit

// this next compare ensures that we can't exit width via an error leaving the
// tab size greater than the line length.

      cpx   tabsiz            // compare with tab size
      bcs   lab_nstt          // branch if >= tab size

      stx   tabsiz            // else make tab size = shell width
lab_nstt:
      stx   twidth            // set the shell width
      jsr   lab_gbyt          // get basic byte back
      beq   wexit             // exit if no following

      cmp   #','              // else is it ","
      bne   lab_mmse          // if not do syntax error

lab_tbsz:
      jsr   lab_sgby          // scan and get byte parameter
      txa                     // copy tab size
      bmi   taberr            // if >127 do function call error and exit

      cpx   #$01              // compare with min-1
      bcc   taberr            // if <=1 do function call error and exit

      lda   twidth            // set flags for width
      beq   lab_svtb          // skip check if infinite line

      cpx   twidth            // compare tab with width
      beq   lab_svtb          // ok if =

      bcs   taberr            // branch if too big

lab_svtb:
      stx   tabsiz            // save tab size

// calculate tab column limit from tab size. the iclim is set to the last tab
// position on a line that still has at least one whole tab width between it
// and the end of the line.

wexit:
      lda   twidth            // get width
      beq   lab_sulp          // branch if infinite line

      cmp   tabsiz            // compare with tab size
      bcs   lab_wdlp          // branch if >= tab size

      sta   tabsiz            // else make tab size = shell width
lab_sulp:
      sec                     // set carry for subtract
lab_wdlp:
      sbc   tabsiz            // subtract tab size
      bcs   lab_wdlp          // loop while no borrow

      adc   tabsiz            // add tab size back
      clc                     // clear carry for add
      adc   tabsiz            // add tab size back again
      sta   iclim             // save for now
      lda   twidth            // get width back
      sec                     // set carry for subtract
      sbc   iclim             // subtract remainder
      sta   iclim             // save tab column limit
lab_nosq:
      rts

taberr:
      jmp   lab_fcer          // do function call error then warm start

// perform pi

lab_pi:
      lda   #<lab_2c7c        // set (2*pi) pointer low byte
      ldy   #>lab_2c7c        // set (2*pi) pointer high byte
      jsr   lab_ufac          // unpack memory (ay) into fac1
      dec   fac1_e            // make result = pi
      rts

// system dependant i/o vectors
// these are in ram and are set by the monitor at start-up

v_inpt:
      jmp   (vec_in)          // non halting scan input device
v_outp:
      jmp   (vec_out)         // send byte to output device
v_load:
      jmp   (vec_ld)          // load basic program
v_save:
      jmp   (vec_sv)          // save basic program

// the rest are tables messages and code for ram

// the rest of the code is tables and basic start-up code

pg2_tabs:
      .byte $00               // ctrl-c flag           -     $00 = enabled
      .byte $00               // ctrl-c byte           -     get needs this
      .byte $00               // ctrl-c byte timeout   -     get needs this
      .word ctrlc             // ctrl c check vector
//     .word xxxx              // non halting key input -     monitor to set this
//     .word xxxx              // output vector         -     monitor to set this
//     .word xxxx              // load vector           -     monitor to set this
//     .word xxxx              // save vector           -     monitor to set this
pg2_tabe:
// character get subroutine for zero page
// (the ZP block is two 3-byte jmp trampolines -- the scan
//  logic lives in the fetchTextByte/peekTextByte wrappers. The jmp
//  operands are code addresses: loader-fixed-up at load time, copied
//  into ZP at cold start.)

// page 0 initialisation table from $bc -- 3-byte jmp trampolines
// to the fetch/peek wrappers (the loader fixes up the code addresses at
// load; this table is copied into ZP at cold start AFTER the fixups run).

lab_2cee:
      jmp   fetchTextByte     // ZP $bc-$be -- advance + scan (lab_igby)
      .byte 0,0,0             // ZP $bf-$c1 -- free
      jmp   peekTextByte      // ZP $c2-$c4 -- scan (lab_gbyt)
                              //  $c3/$c4 hold this jmp's operand -- bpntrl/h
                              //  MUST NOT alias them (they live at $c8/$c9)

// page zero initialisation table $00-$12 inclusive

strtab:
      .byte $00               // $00 -- pad (avoid 6510 DDR)
      .byte $00               // $01 -- pad (avoid 6510 PORT)
      .byte $00               // $02 -- unused by basic
      .byte $00               // $03 -- unused by basic
      .byte $4c               // $04 -- jmp opcode for warm start
      .word lab_cold          // $05-$06 -- warm start vector (cold start)

      .word $0000             // $07-$08 -- unused
      .byte $00               // $09 -- unused
      .byte $00               // $0A -- unused (no usr vector)
      .byte $00               // $0B -- unused (no usr vector)
      .byte $00               // $0C -- unused (no usr vector)
      .byte $00               // default null count
      .byte $00               // clear shell position
      .byte $00               // default shell width byte
      .byte $f2               // default limit for tab = 14
      .word ram_base          // start of user ram
endtab:

// ==========================================
//  REU-backed accessor wrappers
// ==========================================
//  Program RAM lives in the REU working file. ZP pointers hold 16-bit
//  FILE OFFSETS (0..$FFFF). The 5-byte file record sits at dataPtr+0..4.
//  textDom tracks the text-pointer domain: 1 = C64 (ibuffs), 0 = REU offset.
//  It is set at the four pointer-transition sites (lab_1280, lab_1477,
//  lab_166c, lab_16d4) -- never derived from clineh, which stays $FF through
//  the RUN/NEW transition and would misclassify the first program fetch.
//  Flag/register contracts = the EhBASIC originals.

.const lnBaseL = $CA       // buffered line base offset lo
.const lnBaseH = $CB
.const lnLenZ  = $CC       // buffered line length (0 = empty)
.const lnBufL  = $CD       // line buffer page address lo
.const lnBufH  = $CE

// --- 1-byte REU access at (base)+Y ---
//  fioRead: A = byte. fioWrite: A = byte written. Both preserve Y.

fioRead:
    php; pla; sta fioP      // save entry flags (C survives the kernel call)
      txa; pha
      tya; sta fioY
      lda fioY
      clc; adc ut1_pl; sta kArg+2
    lda ut1_ph; adc #0; sta kArg+3
    jmp fioReadGo
fioRead2:
    php; pla; sta fioP
      txa; pha
      tya; sta fioY
      lda fioY
      clc; adc varPtr; sta kArg+2
    lda varPtr+1; adc #0; sta kArg+3
fioReadGo:
    lda dataPtr; sta kArg
    lda dataPtr+1; sta kArg+1
    lda #1; sta kArg+6
    lda #0; sta kArg+7
    lda #0; sta fioScr      // 0 for EOF-clamped reads (offset $FFFF)
    lda #<fioScr; sta kPtr
    lda #>fioScr; sta kPtr+1
    jsr kReuReadAt
      pla; tax
    lda fioP
    lsr                     // C = entry carry
    ldy fioY
    lda fioScr              // N/Z from byte; C untouched
    rts

fioWrite:
    sta fioScr
    tya; sta fioY
    php; pla; sta fioP
      lda fioY
      clc; adc ut1_pl; sta kArg+2
    lda ut1_ph; adc #0; sta kArg+3
    jmp fioWriteGo
fioWrite2:
    sta fioScr
    tya; sta fioY
    php; pla; sta fioP
      lda fioY
      clc; adc varPtr; sta kArg+2
    lda varPtr+1; adc #0; sta kArg+3
fioWriteGo:
    lda #0; sta lnLenZ      // program write -> invalidate line-buffer cache
    lda dataPtr; sta kArg
    lda dataPtr+1; sta kArg+1
    lda #1; sta kArg+6
    lda #0; sta kArg+7
    lda #<fioScr; sta kPtr
    lda #>fioScr; sta kPtr+1
    jsr kReuWriteAt
    lda fioP
    lsr
    ldy fioY
    lda fioScr              // A = written byte; C = entry carry
    rts

// --- descriptor byte readers (domain-aware) ---
//  Descriptors live either on the page-zero descriptor stack or in a REU
//  string variable. Numeric tests (pointer vs svarl/svarh) are UNSOUND
//  here: REU var offsets and ZP addresses both have high byte $00 for
//  small programs. The domain is carried explicitly instead:
//    desDom   domain of des_pl    (0 = REU var, 1 = C64 ZP)
//    des2Dom  domain of des_2
//    ssDom    domain of ssptr (sdescr)
//  Set at the sites that write each pointer, copied when a pointer is
//  copied (lab_209c, lab_224d, lab_236f, lab_17f4/lab_17fb/lab_1811).
//  descRd:   A = byte at (ut1_pl)+Y per desDom.
//  descRd2:  A = byte at (varPtr)+Y per desDom.
//  descRdD2: A = byte at (ut1_pl)+Y per des2Dom.
//  descRdS:  A = byte at (ut1_pl)+Y per ssDom.
//  gcRead:   A = byte at (ut1_pl)+Y per gcDom (GC descriptor walks).
//  All preserve Y and X; N/Z from the byte (matches lda (zp),y).

descRd:
    lda   desDom
    bne   descZP
    jmp   fioRead
descRdD2:
    lda   des2Dom
    bne   descZP
    jmp   fioRead
descRdS:
    lda   ssDom
    bne   descZP
    jmp   fioRead
descRd2:
    lda   desDom
    bne   descZP2
    jmp   fioRead2
gcRead:
    lda   gcDom
    bne   gcZP
    jmp   fioRead
gcWrite:
    pha
    lda   gcDom
    bne   gcWZP
    pla
    jmp   fioWrite
gcWZP:
    pla
    sta   (ut1_pl),y
    rts
descZP:
    lda   (ut1_pl),y
    rts
descZP2:
    lda   (varPtr),y
    rts
gcZP:
    lda   (ut1_pl),y
    rts

// --- fetchTextByte / peekTextByte -- buffered text scan ---
//  Same lab_igby/lab_gbyt contract: A = byte; Z=1 if byte==tk_else,
//  C=1 if not '0'-'9'; spaces skipped (with advance). Clobbers A, Y.

fetchTextByte:
    inc   bpntrl
    bne   fetchNoCarry
    inc   bpntrh
fetchNoCarry:
    jsr   bufMaybeFill        // refill the line buffer if bpntrl left it

peekTextByte:
    ldy   #$00
    jsr   textRd
    cmp   #tk_else
    beq   peekRts
    cmp   #':'
    bcs   peekRts
    cmp   #' '
    beq   fetchTextByte       // skip the space and rescan
    sec
    sbc   #'0'
    sec
    sbc   #$d0
peekRts:
    rts

// textByteAtY -- raw text byte at (bpntrl),y; flags = load result.
//  Y preserved. Immediate mode reads C64; file mode uses the buffer/DMA.
textByteAtY:
    lda   textDom
    beq   tbaFile
    lda   (bpntrl),y
    rts
tbaFile:
    php; pla; sta fioP      // save caller flags -- C must survive the read
    tya
    sta   fioY
    clc
    adc   bpntrl
    sta   kArg+2
    lda   bpntrh
    adc   #0
    sta   kArg+3
    lda   kArg+3
    cmp   lnBaseH
    bcc   tbaDma
    bne   tbaDma
    lda   kArg+2
    cmp   lnBaseL
    bcc   tbaDma
    lda   kArg+2
    sec
    sbc   lnBaseL
    cmp   lnLenZ
    bcs   tbaDma
    tay
    lda   (lnBufL),y
      pha                     // save byte; PLA after LDY restores N/Z from byte
    lda   fioP
    lsr                     // C = caller carry
    ldy   fioY
      pla                     // restore byte -- sets N/Z, C untouched
    rts
tbaDma:
    lda dataPtr; sta kArg
    lda dataPtr+1; sta kArg+1
    lda #1; sta kArg+6
    lda #0; sta kArg+7
    lda #0; sta fioScr      // 0 for EOF-clamped reads
    lda #<fioScr; sta kPtr
    lda #>fioScr; sta kPtr+1
    jsr   kReuReadAt
    lda fioP
    lsr                     // C = caller carry saved at textByteAtY entry
    ldy fioY
    lda fioScr              // N/Z from byte; C untouched
    rts

// textRd -- byte at (bpntrl)+Y, buffer-aware. Y clobbered (fetch/peek only).
textRd:
    lda   textDom
    beq   textRdFile
    lda   (bpntrl),y
    rts
textRdFile:
    tya
    sta   fioY
    clc
    adc   bpntrl
    sta   kArg+2
    lda   bpntrh
    adc   #0
    sta   kArg+3
    lda   kArg+3
    cmp   lnBaseH
    bcc   textRdDma
    bne   textRdDma
    lda   kArg+2
    cmp   lnBaseL
    bcc   textRdDma
    lda   kArg+2
    sec
    sbc   lnBaseL
    cmp   lnLenZ
    bcs   textRdDma
    tay
    lda   (lnBufL),y
    pha                     // restore Y before A so flags describe the byte
    ldy   fioY
    pla
    rts
textRdDma:
    lda   dataPtr; sta kArg
    lda   dataPtr+1; sta kArg+1
    lda   #1; sta kArg+6
    lda   #0; sta kArg+7
    lda   #0; sta fioScr    // 0 for EOF-clamped reads
    lda   #<fioScr; sta kPtr
    lda   #>fioScr; sta kPtr+1
    jsr   kReuReadAt
    lda   fioScr
    ldy   fioY
    rts

// bufMaybeFill -- refill the line buffer if bpntrl left the buffered line.
bufMaybeFill:
    lda   textDom
    bne   bufMaybeRts
    sec
    lda   bpntrl
    sbc   lnBaseL
    sta   fioWalk
    lda   bpntrh
    sbc   lnBaseH
    bne   bufFillJmp
    lda   fioWalk
    cmp   lnLenZ
    bcc   bufMaybeRts
bufFillJmp:
    jmp   bufFill
bufMaybeRts:
    rts

// bufFill -- DMA the line containing bpntrl into the line buffer.
//  Walks the program linked list (from the buffered line if bpntrl is
//  at/after it, else from smem). Sets lnBase/lnLen.
bufFill:
    lda   lnLenZ
    beq   bufFillStart
    lda   bpntrh
    cmp   lnBaseH
    bcc   bufFillStart
    bne   bufFillCur
    lda   bpntrl
    cmp   lnBaseL
    bcc   bufFillStart
bufFillCur:
    lda   lnBaseL; sta fioWalk
    lda   lnBaseH; sta fioWalk+1
    jmp   bufFillLp
bufFillStart:
    lda   smeml; sta fioWalk
    lda   smemh; sta fioWalk+1
bufFillLp:
    lda   dataPtr; sta kArg
    lda   dataPtr+1; sta kArg+1
    lda   fioWalk; sta kArg+2
    lda   fioWalk+1; sta kArg+3
    lda   #2; sta kArg+6
    lda   #0; sta kArg+7
    lda   #<fioScr; sta kPtr
    lda   #>fioScr; sta kPtr+1
    jsr   kReuReadAt
    lda   fioScr
    ora   fioScr+1
    beq   bufFillDma          // link == 0 -> fioWalk is the last line
    lda   bpntrh
    cmp   fioScr+1
    bcc   bufFillDma
    bne   bufFillAdv
    lda   bpntrl
    cmp   fioScr
    bcc   bufFillDma
bufFillAdv:
    lda   fioScr; sta fioWalk
    lda   fioScr+1; sta fioWalk+1
    jmp   bufFillLp
bufFillDma:
    lda   fioWalk; sta lnBaseL
    lda   fioWalk+1; sta lnBaseH
    ldy   #5
    lda   (dataPtr),y; sta lnBufL
    iny
    lda   (dataPtr),y; sta lnBufH
    lda   dataPtr; sta kArg
    lda   dataPtr+1; sta kArg+1
    lda   fioWalk; sta kArg+2
    lda   fioWalk+1; sta kArg+3
    lda   #0; sta kArg+6      // 256 bytes
    lda   #1; sta kArg+7
    lda   lnBufL; sta kPtr
    lda   lnBufH; sta kPtr+1
    jsr   kReuReadAt
    ldy   #4
bufFillScan:
    lda   (lnBufL),y
    beq   bufFillEnd
    iny
    bne   bufFillScan
bufFillEnd:
    iny
    sty   lnLenZ
    rts

// readVar -- 4-byte FP at (AY) -> fac1. A=offset lo, Y=offset hi.
//  Clobbers A, Y, ut1_pl/ut1_ph.
readVar:
    sta   ut1_pl
    sty   ut1_ph
    lda   dataPtr; sta kArg
    lda   dataPtr+1; sta kArg+1
    lda   ut1_pl; sta kArg+2
    lda   ut1_ph; sta kArg+3
    lda   #4; sta kArg+6
    lda   #0; sta kArg+7
    lda   #<fac1_e; sta kPtr
    lda   #>fac1_e; sta kPtr+1
    jsr   kReuReadAt
    lda   fac1_1
    sta   fac1_s
    ora   #$80
    sta   fac1_1
    lda   #0
    sta   fac1_r
    rts

// writeVarL -- fac1 -> 4 bytes at lvarpl. Clobbers A, X, Y, ut1_pl/ut1_ph.
writeVarL:
    ldx   lvarpl
    ldy   lvarph

// writeVar -- fac1 -> 4 bytes at (XY). Clobbers A, Y, ut1_pl/ut1_ph.
writeVar:
    jsr   lab_27ba          // round fac1
    stx   ut1_pl
    sty   ut1_ph
    lda   fac1_e; sta fioScr
    lda   fac1_s
    ora   #$7f
    and   fac1_1
    sta   fioScr+1
    lda   fac1_2; sta fioScr+2
    lda   fac1_3; sta fioScr+3
    lda   dataPtr; sta kArg
    lda   dataPtr+1; sta kArg+1
    lda   ut1_pl; sta kArg+2
    lda   ut1_ph; sta kArg+3
    lda   #4; sta kArg+6
    lda   #0; sta kArg+7
    lda   #<fioScr; sta kPtr
    lda   #>fioScr; sta kPtr+1
    jsr   kReuWriteAt
    lda   #0; sta fac1_r
    rts

// byte accessors -- Y preserved (poke preserves A too).
peekVarByte:   jmp fioRead
pokeVarByte:   jmp fioWrite
peekVarByte2:  jmp fioRead2
pokeVarByte2:  jmp fioWrite2
readStrByte:   jmp fioRead
writeStrByte:  jmp fioWrite
readStrByte2:  jmp fioRead2
writeStrByte2: jmp fioWrite2

// copyString -- A = length; copy (ut1_pl) -> (sutill) forward, advance
//  sutill by length. Clobbers A, X, Y. ut1 preserved.
copyString:
    tax
    beq   copyStrRts
    sta   fioLen
    lda   #0; sta fioLen+1
    lda   ut1_pl; pha
    lda   ut1_ph; pha
    lda   ut2_pl; pha
    lda   ut2_ph; pha
    lda   sutill; sta ut2_pl
    lda   sutilh; sta ut2_ph
    jsr   fCopyFwd
    lda   ut2_pl; sta sutill
    lda   ut2_ph; sta sutilh
    pla; sta ut2_ph
    pla; sta ut2_pl
    pla; sta ut1_ph
    pla; sta ut1_pl
copyStrRts:
    rts

// copyStringC64 -- A bytes from C64 source X/Y to REU string space at sutill.
// Used only for utility strings and immediate-mode ibuffs; program strings
// already live in the REU working file and use copyString above.
copyStringC64:
      sta   fioLen
      stx   kPtr
      sty   kPtr+1
      lda   dataPtr; sta kArg
      lda   dataPtr+1; sta kArg+1
      lda   sutill; sta kArg+2
      lda   sutilh; sta kArg+3
      lda   fioLen; sta kArg+6
      lda   #0; sta kArg+7
      jsr   kReuWriteAt
      lda   sutill
      clc
      adc   fioLen
      sta   sutill
      bcc   copyC64Rts
      inc   sutilh
copyC64Rts:
      rts

// copyBlockUp -- forward block copy (lab_12d8 body). ut1_pl = src,
//  ut2_pl = dst, X = page count, Y = first-block byte count.
//  The original inline loop starts at (ut1)+Y -- the entry Y is a
//  source OFFSET, not a length -- so add it to both pointers first
//  (16-bit, with wraparound exactly like (zp),y).
//  Clobbers A, X, Y (ut1/ut2 advance).
copyBlockUp:
    sty   fioScr
    tya
    clc
    adc   ut1_pl
    sta   ut1_pl
    lda   ut1_ph
    adc   #$00
    sta   ut1_ph
    tya
    clc
    adc   ut2_pl
    sta   ut2_pl
    lda   ut2_ph
    adc   #$00
    sta   ut2_ph
    stx   fioLen+1
    tya
    beq   copyUpFull
    dec   fioLen+1           // total = (X-1)*256 + (256-Y)
    lda   #0
    sec
    sbc   fioScr
    sta   fioLen
    jsr   fCopyFwd
    rts
copyUpFull:
    lda   #0
    sta   fioLen             // total = X*256
    jsr   fCopyFwd
    rts

// fCopyFwd -- copy fioLen (16-bit) bytes from file offset (ut1) to (ut2),
//  forward, in <=256-byte chunks via fioBuf. Advances ut1/ut2 by fioLen.
fCopyFwd:
    lda   #0; sta lnLenZ    // program move -> invalidate line-buffer cache
    lda   fioLen
    ora   fioLen+1
    bne   fCfGo
    rts
fCfGo:
    lda   fioLen+1
    bne   fCf256
    lda   fioLen
    bne   fCfSmall
    jmp   fCfRts
fCfSmall:
    sta   fioChunk
    lda   #0
    sta   fioChunk+1
    sta   fioLen
    jmp   fCfCopy
fCf256:
    lda   #0; sta fioChunk
    lda   #1; sta fioChunk+1
    lda   fioLen+1
    sec; sbc #1
    sta   fioLen+1
fCfCopy:
    jsr   fioChunkRd
    jsr   fioChunkWr
    lda   ut1_pl; clc; adc fioChunk; sta ut1_pl
    lda   ut1_ph; adc fioChunk+1; sta ut1_ph
    lda   ut2_pl; clc; adc fioChunk; sta ut2_pl
    lda   ut2_ph; adc fioChunk+1; sta ut2_ph
    jmp   fCfGo
fCfRts:
    rts

// fioChunkRd / fioChunkWr -- DMA fioChunk (16-bit) bytes between (ut1/ut2)
//  and fioBuf.
fioChunkRd:
    lda   dataPtr; sta kArg
    lda   dataPtr+1; sta kArg+1
    lda   ut1_pl; sta kArg+2
    lda   ut1_ph; sta kArg+3
    lda   fioChunk; sta kArg+6
    lda   fioChunk+1; sta kArg+7
    lda   #<fioBuf; sta kPtr
    lda   #>fioBuf; sta kPtr+1
    jsr   kReuReadAt
    rts
fioChunkWr:
    lda   dataPtr; sta kArg
    lda   dataPtr+1; sta kArg+1
    lda   ut2_pl; sta kArg+2
    lda   ut2_ph; sta kArg+3
    lda   fioChunk; sta kArg+6
    lda   fioChunk+1; sta kArg+7
    lda   #<fioBuf; sta kPtr
    lda   #>fioBuf; sta kPtr+1
    jsr   kReuWriteAt
    rts

// fillBlock -- A = fill byte, ut1_pl = dst offset, X = len lo, Y = len hi.
//  Chunked kReuFillReu (bank-clamped). Clobbers A, X, Y; ut1 advances.
fillBlock:
    sta   fioFill
    stx   fioLen
    sty   fioLen+1
    lda   #0; sta lnLenZ    // program fill -> invalidate line-buffer cache
fillLp:
    lda   fioLen
    ora   fioLen+1
    bne   fillGo
    rts
fillGo:
    lda   fioLen+1
    beq   fillSmall
    lda   #0; sta fioChunk      // chunk = 256
    lda   #1; sta fioChunk+1
    jmp   fillClamp
fillSmall:
    lda   fioLen
    bne   fillSmGo
    jmp   fillRts
fillSmGo:
    sta   fioChunk              // chunk = fioLen (1..255)
    lda   #0
    sta   fioChunk+1
    jmp   fillClamp
fillClamp:
    ldy   #2
    lda   (dataPtr),y; sta kArg       // base lo
    iny
    lda   (dataPtr),y; sta fioScr     // base mid
    iny
    lda   (dataPtr),y; sta fioScr+1   // base hi
    lda   kArg
    clc
    adc   ut1_pl
    sta   kArg                         // addr lo
    lda   fioScr
    adc   ut1_ph
      adc   #$D0                         // FS data base: bank 5, $D000
    sta   kArg+1                       // addr hi
    lda   fioScr+1
      adc   #5
    sta   kArg+2                       // bank
    lda   #0
    sec
    sbc   kArg
    sta   fioScr                       // bank remain lo
    lda   #0
    sbc   kArg+1
    sta   fioScr+1                     // bank remain hi
    lda   fioScr+1
    cmp   fioChunk+1
    bcc   fillSetClamp
    bne   fillDo
    lda   fioScr
    cmp   fioChunk
    bcc   fillSetClamp
    jmp   fillDo
fillSetClamp:
    lda   fioScr; sta fioChunk
    lda   fioScr+1; sta fioChunk+1
fillDo:
    lda   fioChunk; sta kArg+3
    lda   fioChunk+1; sta kArg+4
    lda   fioFill
    jsr   kReuFillReu
    lda   ut1_pl; clc; adc fioChunk; sta ut1_pl
    lda   ut1_ph; adc fioChunk+1; sta ut1_ph
    sec
    lda   fioLen; sbc fioChunk; sta fioLen
    lda   fioLen+1; sbc fioChunk+1; sta fioLen+1
    jmp   fillLp
fillRts:
    rts

// lab_246cVar -- add (file offset AY) to fac1. REU-aware lab_246c, for the
//  FOR/NEXT variable path (the other lab_246c callers pass code constants).
lab_246cVar:
    jsr   lab_264dVar
    jmp   lab_add           // add fac2 to fac1

// lab_264dVar -- unpack memory (file offset AY) into fac2 (REU-aware
//  lab_264d). Returns A = fac1_e.
lab_264dVar:
    sta   ut1_pl
    sty   ut1_ph
    lda   dataPtr; sta kArg
    lda   dataPtr+1; sta kArg+1
    lda   ut1_pl; sta kArg+2
    lda   ut1_ph; sta kArg+3
    lda   #4; sta kArg+6
    lda   #0; sta kArg+7
    lda   #<fac2_e; sta kPtr
    lda   #>fac2_e; sta kPtr+1
    jsr   kReuReadAt
    lda   fac2_1
    sta   fac2_s
    eor   fac1_s
    sta   fac_sc
    lda   fac2_s
    ora   #$80
    sta   fac2_1
    lda   fac1_e
    rts

// --- REU scratch (code-space; BASIC is single-instance) ---
fioScr:   .byte 0,0,0,0   // 1-4 byte DMA scratch
fioY:     .byte 0         // saved Y across REU calls
fioP:     .byte 0         // saved entry flags (carry survives REU calls)
fioLen:   .byte 0,0       // block-move remaining length
fioChunk: .byte 0,0       // current chunk length
fioWalk:  .byte 0,0       // line-walk cursor
fioFill:  .byte 0         // fill byte
strC64Src:.byte 0         // lab_20ae source domain: 1=C64, 0=REU
textDom:  .byte 1         // text pointer domain: 1=C64 (ibuffs), 0=REU offset
contDom:  .byte 0         // continue pointer domain (saved at lab_15c2/lab_163b)
desDom:   .byte 1         // des_pl descriptor domain: 1=C64 ZP, 0=REU var
des2Dom:  .byte 1         // des_2 descriptor domain
ssDom:    .byte 1         // ssptr (sdescr) descriptor domain
gcDom:    .byte 1         // GC descriptor-walk domain
garbDom:  .byte 0         // domain of descriptor selected for GC move
fac2Dom:  .byte 1         // domain stashed with the fac push (lab_1b5b)
fioBuf:   .fill 256, 0    // block-move bounce buffer

lab_smsg:
      .text " bytes free"

.byte $0d

.byte $0a

.byte $0a
      .text "enhanced basic 3.0"
      .byte $0a
      .byte $00

// numeric constants and series

                              // constants and series for log(n)
lab_25a0:
      .byte $02               // counter
      .byte $80
      .byte $19
      .byte $56
      .byte $62   // 0.59898
      .byte $80
      .byte $76
      .byte $22
      .byte $f3   // 0.96147
//##   .byte $80
      .byte $76
      .byte $22
      .byte $f1   // 0.96147
      .byte $82
      .byte $38
      .byte $aa
      .byte $40   // 2.88539
//##   .byte $82
      .byte $38
      .byte $aa
      .byte $45   // 2.88539

lab_25ad:
      .byte $80
      .byte $35
      .byte $04
      .byte $f3   // 0.70711   1/root 2
lab_25b1:
      .byte $81
      .byte $35
      .byte $04
      .byte $f3   // 1.41421   root 2
lab_25b5:
      .byte $80
      .byte $80
      .byte $00
      .byte $00   // -0.5
lab_25b9:
      .byte $80
      .byte $31
      .byte $72
      .byte $18   // 0.69315   log(2)

                              // numeric print constants
lab_2947:
      .byte $91
      .byte $43
      .byte $4f
      .byte $f8   // 99999.9375 (max value with at least one decimal)
lab_294b:
      .byte $94
      .byte $74
      .byte $23
      .byte $f7   // 999999.4375 (max value before scientific notation)
lab_294f:
      .byte $94
      .byte $74
      .byte $24
      .byte $00   // 1000000

                              // exp(n) constants and series
lab_2afa:
      .byte $81
      .byte $38
      .byte $aa
      .byte $3b   // 1.4427    (1/log base 2 e)
lab_2afe:
      .byte $06               // counter
      .byte $74
      .byte $63
      .byte $90
      .byte $8c   // 2.17023e-4
      .byte $77
      .byte $23
      .byte $0c
      .byte $ab   // 0.00124
      .byte $7a
      .byte $1e
      .byte $94
      .byte $00   // 0.00968
      .byte $7c
      .byte $63
      .byte $42
      .byte $80   // 0.05548
      .byte $7e
      .byte $75
      .byte $fe
      .byte $d0   // 0.24023
      .byte $80
      .byte $31
      .byte $72
      .byte $15   // 0.69315
      .byte $81
      .byte $00
      .byte $00
      .byte $00   // 1.00000

//##   .byte $07               // counter
//##   .byte $74
      .byte $94
      .byte $2e
      .byte $40   // -1/7! (-1/5040)
//##   .byte $77
      .byte $2e
      .byte $4f
      .byte $70   //  1/6! ( 1/720)
//##   .byte $7a
      .byte $88
      .byte $02
      .byte $6e   // -1/5! (-1/120)
//##   .byte $7c
      .byte $2a
      .byte $a0
      .byte $e6   //  1/4! ( 1/24)
//##   .byte $7e
      .byte $aa
      .byte $aa
      .byte $50   // -1/3! (-1/6)
//##   .byte $7f
      .byte $7f
      .byte $ff
      .byte $ff   //  1/2! ( 1/2)
//##   .byte $81
      .byte $80
      .byte $00
      .byte $00   // -1/1! (-1/1)
//##   .byte $81
      .byte $00
      .byte $00
      .byte $00   //  1/0! ( 1/1)

                              // trigonometric constants and series
lab_2c78:
      .byte $81
      .byte $49
      .byte $0f
      .byte $db   // 1.570796371 (pi/2) as floating #
lab_2c84:
      .byte $04               // counter
      .byte $86
      .byte $1e
      .byte $d7
      .byte $fb   // 39.7109
//##   .byte $86
      .byte $1e
      .byte $d7
      .byte $ba   // 39.7109
      .byte $87
      .byte $99
      .byte $26
      .byte $65   //-76.575
//##   .byte $87
      .byte $99
      .byte $26
      .byte $64   //-76.575
      .byte $87
      .byte $23
      .byte $34
      .byte $58   // 81.6022
      .byte $86
      .byte $a5
      .byte $5d
      .byte $e1   //-41.3417
//##   .byte $86
      .byte $a5
      .byte $5d
      .byte $e0   //-41.3417
lab_2c7c:
      .byte $83
      .byte $49
      .byte $0f
      .byte $db   // 6.28319 (2*pi) as floating #
//##   .byte $83
      .byte $49
      .byte $0f
      .byte $da   // 6.28319 (2*pi) as floating #

lab_2cc9:
      .byte $08               // counter
      .byte $78
      .byte $3a
      .byte $c5
      .byte $37   // 0.00285
      .byte $7b
      .byte $83
      .byte $a2
      .byte $5c   //-0.0160686
      .byte $7c
      .byte $2e
      .byte $dd
      .byte $4d   // 0.0426915
      .byte $7d
      .byte $99
      .byte $b0
      .byte $1e   //-0.0750429
      .byte $7d
      .byte $59
      .byte $ed
      .byte $24   // 0.106409
      .byte $7e
      .byte $91
      .byte $72
      .byte $00   //-0.142036
      .byte $7e
      .byte $4c
      .byte $b9
      .byte $73   // 0.199926
      .byte $7f
      .byte $aa
      .byte $aa
      .byte $53   //-0.333331

//##   .byte $08               // counter
//##   .byte $78
      .byte $3b
      .byte $d7
      .byte $4a   // 1/17
//##   .byte $7b
      .byte $84
      .byte $6e
      .byte $02   //-1/15
//##   .byte $7c
      .byte $2f
      .byte $c1
      .byte $fe   // 1/13
//##   .byte $7d
      .byte $9a
      .byte $31
      .byte $74   //-1/11
//##   .byte $7d
      .byte $5a
      .byte $3d
      .byte $84   // 1/9
//##   .byte $7e
      .byte $91
      .byte $7f
      .byte $c8   //-1/7
//##   .byte $7e
      .byte $4c
      .byte $bb
      .byte $e4   // 1/5
//##   .byte $7f
      .byte $aa
      .byte $aa
      .byte $6c   //-1/3

lab_259c:
.label lab_1d96 = *+1             // $00,$00 used for undefined variables
      .byte $81
      .byte $00
      .byte $00
      .byte $00   // 1.000000 used for inc
lab_2afd:
      .byte $81
      .byte $80
      .byte $00
      .byte $00   // -1.00000 used for dec

                              // misc constants
lab_1df7:
      .byte $90               //-32768 (uses first three bytes from 0.5)
lab_2a96:
      .byte $80
      .byte $00
      .byte $00
      .byte $00   // 0.5
lab_2c80:
      .byte $7f
      .byte $00
      .byte $00
      .byte $00   // 0.25
lab_26b5:
      .byte $84
      .byte $20
      .byte $00
      .byte $00   // 10.0000 divide by 10 constant

// this table is used in converting numbers to ascii.

lab_2a9a:
.const lab_2a9b = lab_2a9a+1
.const lab_2a9c = lab_2a9b+1
      .byte $fe
      .byte $79
      .byte $60       // -100000
      .byte $00
      .byte $27
      .byte $10       // 10000
      .byte $ff
      .byte $fc
      .byte $18       // -1000
      .byte $00
      .byte $00
      .byte $64       // 100
      .byte $ff
      .byte $ff
      .byte $f6       // -10
      .byte $00
      .byte $00
      .byte $01       // 1

lab_ctbl:
      .word lab_end-1         // end
      .word lab_for-1         // for
      .word lab_next-1        // next
      .word lab_data-1        // data
      .word lab_input-1       // input
      .word lab_dim-1         // dim
      .word lab_read-1        // read
      .word lab_let-1         // let
      .word lab_dec-1         // dec             new command
      .word lab_goto-1        // goto
      .word lab_run-1         // run
      .word lab_if-1          // if
      .word lab_restore-1     // restore         modified command
      .word lab_gosub-1       // gosub
      .word lab_return-1      // return
      .word lab_rem-1         // rem
      .word lab_stop-1        // stop
      .word lab_on-1          // on              modified command
      .word lab_inc-1         // inc             new command
      .word lab_wait-1        // wait
      .word v_load-1          // load
      .word v_save-1          // save
      .word lab_def-1         // def
      .word lab_poke-1        // poke
      .word lab_call-1        // call            new command
      .word lab_do-1          // do              new command
      .word lab_loop-1        // loop            new command
      .word lab_print-1       // print
      .word lab_cont-1        // cont
      .word lab_list-1        // list
      .word lab_clear-1       // clear
      .word lab_new-1         // new
      .word lab_wdth-1        // width           new command
      .word lab_get-1         // get             new command
      .word lab_swap-1        // swap            new command
      .word lab_exit-1        // exit            new command
      .word lab_dir-1         // dir             new command

// graphics statement dispatch table (token range tk_mode..tk_pen3)

lab_gxctbl:
      .word gxDoMode-1        // mode
      .word gxDoCursor-1      // cursor
      .word gxDoCls-1         // cls
      .word gxDoPlot-1        // plot
      .word gxDoLine-1        // line
      .word gxDoBox-1         // box
      .word gxDoFillBox-1     // fillbox
      .word gxDoCircle-1      // circle
      .word gxDoEllipse-1     // ellipse
      .word gxDoFlood-1       // flood
      .word gxDoGChar-1       // gchar
      .word gxDoGText-1       // gtext
      .word gxDoPrintAt-1     // printat
      .word gxDoLocate-1      // locate
      .word gxDoColour-1      // colour
      .word gxDoScroll-1      // scroll
      .word gxDoGdef-1        // gdef
      .word gxDoGlyph-1       // glyph
      .word gxDoBorder-1      // border
      .word gxDoPen1-1        // pen1
      .word gxDoPen2-1        // pen2
      .word gxDoShadow-1      // shadow
      .word gxDoPen0-1        // pen0
      .word gxDoScrollby-1    // scrollby
      .word gxDoPen3-1        // pen3

// function pre process routine table

lab_ftpl:
.const lab_ftpm = lab_ftpl+$01
      .word lab_ppfn-1        // sgn(n)    process numeric expression in ()
      .word lab_ppfn-1        // int(n)          "
      .word lab_ppfn-1        // abs(n)          "
      .word lab_1bf7-1        // fre(x)          "
      .word lab_1bf7-1        // pos(x)          "
      .word lab_ppfn-1        // sqr(n)    process numeric expression in ()
      .word lab_ppfn-1        // rnd(n)          "
      .word lab_ppfn-1        // log(n)          "
      .word lab_ppfn-1        // exp(n)          "
      .word lab_ppfn-1        // cos(n)          "
      .word lab_ppfn-1        // sin(n)          "
      .word lab_ppfn-1        // tan(n)          "
      .word lab_ppfn-1        // atn(n)          "
      .word lab_ppfn-1        // peek(n)         "
      .word lab_ppfs-1        // len($)    process string expression in ()
      .word lab_ppfn-1        // str$(n)   process numeric expression in ()
      .word lab_ppfs-1        // val($)    process string expression in ()
      .word lab_ppfs-1        // asc($)          "
      .word lab_ppfs-1        // ucase$($)       "
      .word lab_ppfs-1        // lcase$($)       "
      .word lab_ppfn-1        // chr$(n)   process numeric expression in ()
      .word lab_bhss-1        // hex$(n)         "
      .word lab_bhss-1        // bin$(n)         "
      .word lab_mmpp-1        // max()     process numeric expression
      .word lab_mmpp-1        // min()           "
      .word lab_ppbi-1        // pi        advance pointer
      .word lab_lrms-1        // left$()   process string expression
      .word lab_lrms-1        // right$()        "
      .word lab_lrms-1        // mid$()          "
      .word lab_ppgn-1        // getpixel(x,y)   GordonOS graphics function

// action addresses for functions

lab_ftbl:
.const lab_ftbm = lab_ftbl+$01
      .word fpSgnW-1         // sgn()
      .word fpIntW-1         // int()
      .word fpAbsW-1         // abs()
      .word lab_fre-1         // fre()
      .word lab_pos-1         // pos()
      .word fpSqrW-1         // sqr()
      .word fpRndW-1         // rnd()           modified function
      .word fpLogW-1         // log()
      .word fpExpW-1         // exp()
      .word fpCosW-1         // cos()
      .word fpSinW-1         // sin()
      .word fpTanW-1         // tan()
      .word fpAtnW-1         // atn()
      .word lab_peek-1        // peek()
      .word lab_lens-1        // len()
      .word lab_strs-1        // str$()
      .word lab_val-1         // val()
      .word lab_asc-1         // asc()
      .word lab_ucase-1       // ucase$()        new function
      .word lab_lcase-1       // lcase$()        new function
      .word lab_chrs-1        // chr$()
      .word lab_hexs-1        // hex$()          new function
      .word lab_bins-1        // bin$()          new function
      .word lab_max-1         // max()           new function
      .word lab_min-1         // min()           new function
      .word lab_pi-1          // pi              new function
      .word lab_left-1        // left$()
      .word lab_right-1       // right$()
      .word lab_mids-1        // mid$()
      .word gxFnGetPixel-1    // getpixel(x,y)   GordonOS graphics function

// hierarchy and action addresses for operator

lab_oppt:
      .byte $79               // +
      .word fpAddW-1
      .byte $79               // -
      .word fpSubW-1
      .byte $7b               // *
      .word fpMulW-1
      .byte $7b               // /
      .word fpDivW-1
      .byte $7f               // ^
      .word fpPowW-1
      .byte $50               // and
      .word lab_and-1
      .byte $46               // eor             new operator
      .word lab_eor-1
      .byte $46               // or
      .word lab_or-1
      .byte $56               // >>              new operator
      .word lab_rshift-1
      .byte $56               // <<              new operator
      .word lab_lshift-1
      .byte $7d               // >
      .word fpGtW-1
      .byte $5a               // =
      .word lab_equal-1
      .byte $64               // <
      .word lab_lthan-1

// keywords start with ..
// this is the first character table and must be in alphabetic order
tab_1stc:
      .text "*"
      .text "+"
      .text "-"
      .text "/"
      .text "<"
      .text "="
      .text ">"
      .text "?"
      .text "^"
      .text "a"
      .text "b"
      .text "c"
      .text "d"
      .text "e"
      .text "f"
      .text "g"
      .text "h"
      .text "i"
      .text "l"
      .text "m"
      .text "n"
      .text "o"
      .text "p"
      .text "r"
      .text "s"
      .text "t"
      .text "u"
      .text "v"
      .text "w"
      .byte $00               // table terminator

// pointers to keyword tables

tab_chrt:
      .word tab_star          // table for "*"
      .word tab_plus          // table for "+"
      .word tab_mnus          // table for "-"
      .word tab_slas          // table for "/"
      .word tab_less          // table for "<"
      .word tab_equl          // table for "="
      .word tab_more          // table for ">"
      .word tab_qest          // table for "?"
      .word tab_powr          // table for "^"
      .word tab_asca          // table for "a"
      .word tab_ascb          // table for "b"
      .word tab_ascc          // table for "c"
      .word tab_ascd          // table for "d"
      .word tab_asce          // table for "e"
      .word tab_ascf          // table for "f"
      .word tab_ascg          // table for "g"
      .word tab_asch          // table for "h"
      .word tab_asci          // table for "i"
      .word tab_ascl          // table for "l"
      .word tab_ascm          // table for "m"
      .word tab_ascn          // table for "n"
      .word tab_asco          // table for "o"
      .word tab_ascp          // table for "p"
      .word tab_ascr          // table for "r"
      .word tab_ascs          // table for "s"
      .word tab_asct          // table for "t"
      .word tab_ascu          // table for "u"
      .word tab_ascv          // table for "v"
      .word tab_ascw          // table for "w"

// tables for each start character, note if a longer keyword with the same start
// letters as a shorter one exists then it must come first, else the list is in
// alphabetical order as follows ..

// [keyword,token
// [keyword,token]]
// end marker (#$00)

tab_star:
      .byte tk_mul,$00        // *
tab_plus:
      .byte tk_plus,$00       // +
tab_mnus:
      .byte tk_minus,$00      // -
tab_slas:
      .byte tk_div,$00        // /
tab_less:
lbb_lshift:
      .text "<"
.byte tk_lshift     // <<  note - "<<" must come before "<"
      .byte tk_lt             // <
      .byte $00
tab_equl:
      .byte tk_equal
.byte $00      // =
tab_more:
lbb_rshift:
      .text ">"
.byte tk_rshift     // >>  note - ">>" must come before ">"
      .byte tk_gt             // >
      .byte $00
tab_qest:
      .byte tk_print,$00      // ?
tab_asca:
lbb_abs:
      .text "bs("
.byte tk_abs      // abs(
lbb_and:
      .text "nd"
.byte tk_and       // and
lbb_asc:
      .text "sc("
.byte tk_asc      // asc(
lbb_atn:
      .text "tn("
.byte tk_atn      // atn(
      .byte $00
tab_ascb:
lbb_bins:
      .text "in$("
.byte tk_bins    // bin$(
lbb_border:
      .text "order"
.byte tk_border  // border
lbb_box:
      .text "ox"
.byte tk_box      // box
      .byte $00
tab_ascc:
lbb_call:
      .text "all"
.byte tk_call     // call
lbb_chrs:
      .text "hr$("
.byte tk_chrs    // chr$(
lbb_circle:
      .text "ircle"
.byte tk_circle  // circle
lbb_clear:
      .text "lear"
.byte tk_clear   // clear
lbb_cls:
      .text "ls"
.byte tk_cls      // cls
lbb_colour:
      .text "olour"
.byte tk_colour  // colour
lbb_cont:
      .text "ont"
.byte tk_cont     // cont
lbb_cos:
      .text "os("
.byte tk_cos      // cos(
lbb_cursor:
      .text "ursor"
.byte tk_cursor  // cursor
      .byte $00
tab_ascd:
lbb_data:
      .text "ata"
.byte tk_data     // data
lbb_dec:
      .text "ec"
.byte tk_dec       // dec
lbb_def:
      .text "ef"
.byte tk_def       // def
lbb_dim:
      .text "im"
.byte tk_dim       // dim
lbb_dir:
      .text "ir"
.byte tk_dir       // dir
lbb_do:
      .text "o"
.byte tk_do         // do
      .byte $00
tab_asce:
lbb_ellipse:
      .text "llipse"
.byte tk_ellipse // ellipse
lbb_else:
      .text "lse"
.byte tk_else     // else
lbb_end:
      .text "nd"
.byte tk_end       // end
lbb_eor:
      .text "or"
.byte tk_eor       // eor
lbb_exit:
      .text "xit"
.byte tk_exit      // exit
lbb_exp:
      .text "xp("
.byte tk_exp      // exp(
      .byte $00
tab_ascf:
lbb_fillbox:
      .text "illbox"
.byte tk_fillbox // fillbox
lbb_flood:
      .text "lood"
.byte tk_flood   // flood
lbb_fn:
      .text "n"
.byte tk_fn         // fn
lbb_for:
      .text "or"
.byte tk_for       // for
lbb_fre:
      .text "re("
.byte tk_fre      // fre(
      .byte $00
tab_ascg:
lbb_gchar:
      .text "char"
.byte tk_gchar    // gchar
lbb_gdef:
      .text "def"
.byte tk_gdef     // gdef
lbb_getpixel:
      .text "etpixel("
.byte tk_getpixel // getpixel(
lbb_get:
      .text "et"
.byte tk_get       // get
lbb_glyph:
      .text "lyph"
.byte tk_glyph    // glyph
lbb_gosub:
      .text "osub"
.byte tk_gosub   // gosub
lbb_goto:
      .text "oto"
.byte tk_goto     // goto
lbb_gtext:
      .text "text"
.byte tk_gtext    // gtext
      .byte $00
tab_asch:
lbb_hexs:
      .text "ex$("
.byte tk_hexs    // hex$(
      .byte $00
tab_asci:
lbb_if:
      .text "f"
.byte tk_if         // if
lbb_inc:
      .text "nc"
.byte tk_inc       // inc
lbb_input:
      .text "nput"
.byte tk_input   // input
lbb_int:
      .text "nt("
.byte tk_int      // int(
      .byte $00
tab_ascl:
lbb_lcases:
      .text "case$("
.byte tk_lcases
                              // lcase$(
lbb_lefts:
      .text "eft$("
.byte tk_lefts  // left$(
lbb_len:
      .text "en("
.byte tk_len      // len(
lbb_let:
      .text "et"
.byte tk_let       // let
lbb_line:
      .text "ine"
.byte tk_line     // line
lbb_list:
      .text "ist"
.byte tk_list     // list
lbb_load:
      .text "oad"
.byte tk_load     // load
lbb_locate:
      .text "ocate"
.byte tk_locate  // locate
lbb_log:
      .text "og("
.byte tk_log      // log(
lbb_loop:
      .text "oop"
.byte tk_loop     // loop
      .byte $00
tab_ascm:
lbb_max:
      .text "ax("
.byte tk_max      // max(
lbb_mids:
      .text "id$("
.byte tk_mids    // mid$(
lbb_min:
      .text "in("
.byte tk_min      // min(
lbb_mode:
      .text "ode"
.byte tk_mode     // mode
      .byte $00
tab_ascn:
lbb_new:
      .text "ew"
.byte tk_new       // new
lbb_next:
      .text "ext"
.byte tk_next     // next
lbb_not:
      .text "ot"
.byte tk_not       // not
      .byte $00
tab_asco:
lbb_on:
      .text "n"
.byte tk_on         // on
lbb_or:
      .text "r"
.byte tk_or         // or
      .byte $00
tab_ascp:
lbb_peek:
      .text "eek("
.byte tk_peek    // peek(
lbb_pen0:
      .text "en0"
.byte tk_pen0     // pen0
lbb_pen1:
      .text "en1"
.byte tk_pen1     // pen1
lbb_pen2:
      .text "en2"
.byte tk_pen2     // pen2
lbb_pen3:
      .text "en3"
.byte tk_pen3     // pen3
lbb_pi:
      .text "i"
.byte tk_pi         // pi
lbb_plot:
      .text "lot"
.byte tk_plot     // plot
lbb_poke:
      .text "oke"
.byte tk_poke     // poke
lbb_pos:
      .text "os("
.byte tk_pos      // pos(
lbb_printat:
      .text "rintat"
.byte tk_printat // printat
lbb_print:
      .text "rint"
.byte tk_print   // print
      .byte $00
tab_ascr:
lbb_read:
      .text "ead"
.byte tk_read     // read
lbb_rem:
      .text "em"
.byte tk_rem       // rem
lbb_restore:
      .text "estore"
.byte tk_restore
                              // restore
lbb_return:
      .text "eturn"
.byte tk_return // return
lbb_rights:
      .text "ight$("
.byte tk_rights
                              // right$(
lbb_rnd:
      .text "nd("
.byte tk_rnd      // rnd(
lbb_run:
      .text "un"
.byte tk_run       // run
      .byte $00
tab_ascs:
lbb_save:
      .text "ave"
.byte tk_save     // save
lbb_scrollby:
      .text "crollby"
.byte tk_scrollby // scrollby
lbb_scroll:
      .text "croll"
.byte tk_scroll  // scroll
lbb_sgn:
      .text "gn("
.byte tk_sgn      // sgn(
lbb_shadow:
      .text "hadow"
.byte tk_shadow  // shadow
lbb_sin:
      .text "in("
.byte tk_sin      // sin(
lbb_spc:
      .text "pc("
.byte tk_spc      // spc(
lbb_sqr:
      .text "qr("
.byte tk_sqr      // sqr(
lbb_step:
      .text "tep"
.byte tk_step     // step
lbb_stop:
      .text "top"
.byte tk_stop     // stop
lbb_strs:
      .text "tr$("
.byte tk_strs    // str$(
lbb_swap:
      .text "wap"
.byte tk_swap     // swap
      .byte $00
tab_asct:
lbb_tab:
      .text "ab("
.byte tk_tab      // tab(
lbb_tan:
      .text "an("
.byte tk_tan      // tan(
lbb_then:
      .text "hen"
.byte tk_then     // then
lbb_to:
      .text "o"
.byte tk_to         // to
      .byte $00
tab_ascu:
lbb_ucases:
      .text "case$("
.byte tk_ucases
                              // ucase$(
lbb_until:
      .text "ntil"
.byte tk_until   // until
      .byte $00
tab_ascv:
lbb_val:
      .text "al("
.byte tk_val      // val(
      .byte $00
tab_ascw:
lbb_wait:
      .text "ait"
.byte tk_wait     // wait
lbb_while:
      .text "hile"
.byte tk_while   // while
lbb_width:
      .text "idth"
.byte tk_width   // width
      .byte $00
tab_powr:
      .byte tk_power,$00      // ^

// new decode table for list
// table is ..
// byte - keyword length, keyword first character
// word - pointer to rest of keyword from dictionary

// note if length is 1 then the pointer is ignored

// one dense 4-byte record per token, indexed by the raw token value, so it
// stays in step with the token count: a statement added or removed shifts
// every later record, and nothing checks the count. tk_*, the dispatch tables
// and the tab_asc* keyword chains move with it.

lab_keyt:
      .byte 3,'e'
      .word lbb_end           // end
      .byte 3,'f'
      .word lbb_for           // for
      .byte 4,'n'
      .word lbb_next          // next
      .byte 4,'d'
      .word lbb_data          // data
      .byte 5,'i'
      .word lbb_input         // input
      .byte 3,'d'
      .word lbb_dim           // dim
      .byte 4,'r'
      .word lbb_read          // read
      .byte 3,'l'
      .word lbb_let           // let
      .byte 3,'d'
      .word lbb_dec           // dec
      .byte 4,'g'
      .word lbb_goto          // goto
      .byte 3,'r'
      .word lbb_run           // run
      .byte 2,'i'
      .word lbb_if            // if
      .byte 7,'r'
      .word lbb_restore       // restore
      .byte 5,'g'
      .word lbb_gosub         // gosub
      .byte 6,'r'
      .word lbb_return        // return
      .byte 3,'r'
      .word lbb_rem           // rem
      .byte 4,'s'
      .word lbb_stop          // stop
      .byte 2,'o'
      .word lbb_on            // on
      .byte 3,'i'
      .word lbb_inc           // inc
      .byte 4,'w'
      .word lbb_wait          // wait
      .byte 4,'l'
      .word lbb_load          // load
      .byte 4,'s'
      .word lbb_save          // save
      .byte 3,'d'
      .word lbb_def           // def
      .byte 4,'p'
      .word lbb_poke          // poke
      .byte 4,'c'
      .word lbb_call          // call
      .byte 2,'d'
      .word lbb_do            // do
      .byte 4,'l'
      .word lbb_loop          // loop
      .byte 5,'p'
      .word lbb_print         // print
      .byte 4,'c'
      .word lbb_cont          // cont
      .byte 4,'l'
      .word lbb_list          // list
      .byte 5,'c'
      .word lbb_clear         // clear
      .byte 3,'n'
      .word lbb_new           // new
      .byte 5,'w'
      .word lbb_width         // width
      .byte 3,'g'
      .word lbb_get           // get
      .byte 4,'s'
      .word lbb_swap          // swap
      .byte 4,'e'
      .word lbb_exit          // exit
      .byte 3,'d'
      .word lbb_dir           // dir

// secondary commands (can't start a statement)

      .byte 4,'t'
      .word lbb_tab           // tab
      .byte 4,'e'
      .word lbb_else          // else
      .byte 2,'t'
      .word lbb_to            // to
      .byte 2,'f'
      .word lbb_fn            // fn
      .byte 4,'s'
      .word lbb_spc           // spc
      .byte 4,'t'
      .word lbb_then          // then
      .byte 3,'n'
      .word lbb_not           // not
      .byte 4,'s'
      .word lbb_step          // step
      .byte 5,'u'
      .word lbb_until         // until
      .byte 5,'w'
      .word lbb_while         // while

// opperators

      .byte 1,'+'
      .word $0000             // +
      .byte 1,'-'
      .word $0000             // -
      .byte 1,'*'
      .word $0000             // *
      .byte 1,'/'
      .word $0000             // /
      .byte 1,'^'
      .word $0000             // ^
      .byte 3,'a'
      .word lbb_and           // and
      .byte 3,'e'
      .word lbb_eor           // eor
      .byte 2,'o'
      .word lbb_or            // or
      .byte 2,'>'
      .word lbb_rshift        // >>
      .byte 2,'<'
      .word lbb_lshift        // <<
      .byte 1,'>'
      .word $0000             // >
      .byte 1,'='
      .word $0000             // =
      .byte 1,'<'
      .word $0000             // <

// functions

      .byte 4,'s'             //
      .word lbb_sgn           // sgn
      .byte 4,'i'             //
      .word lbb_int           // int
      .byte 4,'a'             //
      .word lbb_abs           // abs
      .byte 4,'f'             //
      .word lbb_fre           // fre
      .byte 4,'p'             //
      .word lbb_pos           // pos
      .byte 4,'s'             //
      .word lbb_sqr           // sqr
      .byte 4,'r'             //
      .word lbb_rnd           // rnd
      .byte 4,'l'             //
      .word lbb_log           // log
      .byte 4,'e'             //
      .word lbb_exp           // exp
      .byte 4,'c'             //
      .word lbb_cos           // cos
      .byte 4,'s'             //
      .word lbb_sin           // sin
      .byte 4,'t'             //
      .word lbb_tan           // tan
      .byte 4,'a'             //
      .word lbb_atn           // atn
      .byte 5,'p'             //
      .word lbb_peek          // peek
      .byte 4,'l'             //
      .word lbb_len           // len
      .byte 5,'s'             //
      .word lbb_strs          // str$
      .byte 4,'v'             //
      .word lbb_val           // val
      .byte 4,'a'             //
      .word lbb_asc           // asc
      .byte 7,'u'             //
      .word lbb_ucases        // ucase$
      .byte 7,'l'             //
      .word lbb_lcases        // lcase$
      .byte 5,'c'             //
      .word lbb_chrs          // chr$
      .byte 5,'h'             //
      .word lbb_hexs          // hex$
      .byte 5,'b'             //
      .word lbb_bins          // bin$
      .byte 4,'m'             //
      .word lbb_max           // max
      .byte 4,'m'             //
      .word lbb_min           // min
      .byte 2,'p'             //
      .word lbb_pi            // pi
      .byte 6,'l'             //
      .word lbb_lefts         // left$
      .byte 7,'r'             //
      .word lbb_rights        // right$
      .byte 5,'m'             //
      .word lbb_mids          // mid$

// GordonOS graphics tokens -- getpixel($DD) is a function,
// the statement tokens are $DE-$F7.

      .byte 9,'g'
      .word lbb_getpixel       // getpixel(
      .byte 4,'m'
      .word lbb_mode           // mode
      .byte 6,'c'
      .word lbb_cursor         // cursor
      .byte 3,'c'
      .word lbb_cls            // cls
      .byte 4,'p'
      .word lbb_plot           // plot
      .byte 4,'l'
      .word lbb_line           // line
      .byte 3,'b'
      .word lbb_box            // box
      .byte 7,'f'
      .word lbb_fillbox        // fillbox
      .byte 6,'c'
      .word lbb_circle         // circle
      .byte 7,'e'
      .word lbb_ellipse        // ellipse
      .byte 5,'f'
      .word lbb_flood          // flood
      .byte 5,'g'
      .word lbb_gchar          // gchar
      .byte 5,'g'
      .word lbb_gtext          // gtext
      .byte 7,'p'
      .word lbb_printat        // printat
      .byte 6,'l'
      .word lbb_locate         // locate
      .byte 6,'c'
      .word lbb_colour         // colour
      .byte 6,'s'
      .word lbb_scroll         // scroll
      .byte 4,'g'
      .word lbb_gdef           // gdef
      .byte 5,'g'
      .word lbb_glyph          // glyph
      .byte 6,'b'
      .word lbb_border         // border
      .byte 4,'p'
      .word lbb_pen1           // pen1
      .byte 4,'p'
      .word lbb_pen2           // pen2
      .byte 6,'s'
      .word lbb_shadow         // shadow
      .byte 4,'p'
      .word lbb_pen0           // pen0
      .byte 8,'s'
      .word lbb_scrollby       // scrollby
      .byte 4,'p'
      .word lbb_pen3           // pen3

// basic messages, mostly error messages

lab_baer:
      .word err_nf            //$00 next without for
      .word err_sn            //$02 syntax
      .word err_rg            //$04 return without gosub
      .word err_od            //$06 out of data
      .word err_fc            //$08 function call
      .word err_ov            //$0a overflow
      .word err_om            //$0c out of memory
      .word err_us            //$0e undefined statement
      .word err_bs            //$10 array bounds
      .word err_dd            //$12 double dimension array
      .word err_d0            //$14 divide by 0
      .word err_id            //$16 illegal direct
      .word err_tm            //$18 type mismatch
      .word err_ls            //$1a long string
      .word err_st            //$1c string too complex
      .word err_cn            //$1e continue error
      .word err_uf            //$20 undefined function
      .word err_ld            //$22 loop without do
      .word err_iq            //$24 illegal quantity

// i may implement these two errors to force definition of variables and
// dimensioning of arrays before use.

//     .word err_uv            //$24 undefined variable

// the above error works (see the code and comments below lab_1d8b)

//     .word err_ua            //$26 undimensioned array
err_nf:      .text "next without for"

.byte $00
err_sn:      .text "syntax"

.byte $00
err_rg:      .text "return without gosub"
.byte $00
err_od:      .text "out of data"
.byte $00
err_fc:      .text "function call"
.byte $00
err_ov:      .text "overflow"
.byte $00
err_om:      .text "out of memory"
.byte $00
err_us:      .text "undefined statement"
.byte $00
err_bs:      .text "array bounds"
.byte $00
err_dd:      .text "double dimension"
.byte $00
err_d0:      .text "divide by zero"
.byte $00
err_id:      .text "illegal direct"
.byte $00
err_tm:      .text "type mismatch"
.byte $00
err_ls:      .text "string too long"
.byte $00
err_st:      .text "string too complex"
.byte $00
err_cn:      .text "can't continue"
.byte $00
err_uf:      .text "undefined function"
.byte $00
err_ld:      .text "loop without do"
.byte $00
err_iq:      .text "illegal quantity"
.byte $00

//err_uv     .byte "undefined variable",$00

// the above error works (see the code and comments below lab_1d8b)

//err_ua     .byte "undimensioned array",$00
lab_bmsg:    .byte $0d

.byte $0a

.text "break"

.byte $00
lab_emsg:    .text " error"

.byte $00
lab_lmsg:    .text " in line "
.byte $00
lab_rmsg:    .byte $0d
.byte $0a
.text "ready"
.byte $0d
.byte $0a
.byte $00
lab_imsg:    .text " extra ignored"

.byte $0d

.byte $0a

.byte $00
lab_redo:    .text " redo from start"

.byte $0d

.byte $0a

.byte $00

aa_end_basic:

// ==========================================
//  lab_exit -- EXIT command: terminate the BASIC task
// ==========================================
//  Deletes the persistent workspace file (basicwrk), frees BASIC's own
//  screen, then exits via syscall 0 (sysExit -> dynFree releases the
//  pool code pages + data-region record). sysExit never returns. A
//  crash bypasses this path, so the workspace file survives to be
//  reused on the next run.
lab_exit:
    lda #<msgWorkName           // remove the workspace file
    sta kArg                    // (no-op if it is already gone)
    lda #>msgWorkName
    sta kArg+1
    jsr kReuDelete
    jsr kFreeScreen             // free the screen allocated at startup
    lda #SYS_EXIT
    jsr syscall                 // exit -- never returns
    jmp lab_exit                // safety (unreachable)

// ==========================================
//  lab_dir -- DIR command: list REU filesystem entries
// ==========================================
//  Iterates kReuDirEntry from index 1 until the first empty slot,
//  printing each name padded to 11 columns, an extra space, then its
//  size in right-aligned decimal bytes (5-char field). Slot 0 is the
//  "(free)" meta entry and is skipped.
lab_dir:
    beq   lab_dir_entry      // if end of statement, proceed
    jmp   lab_dir_rt         // not end -- defer to interpreter
lab_dir_entry:
    jsr   lab_crlf           // newline before the directory listing
    lda   #1                 // start at entry 1 (slot 0 = meta)
    sta   dirIdx
    lda   #0
    sta   dirIdxHi
lab_dir_lp:
    lda   dirIdxHi
    cmp   #2
    bne   lab_dir_go         // hi != 2 -> scan it
    jmp   lab_dir_rt         // exhausted (idx == 512)
lab_dir_go:
    lda   #<dirNameBuf
    sta   kPtr
    lda   #>dirNameBuf
    sta   kPtr+1
    lda   dirIdx
    ldx   dirIdxHi
    jsr   kReuDirEntry       // C=0 entry (name->buf, size->kArg/kArg+1); C=1 empty
    bcs   lab_dir_rt         // end of directory
    lda   kArg               // stash size (kPrintString/kPrintChar clobber kArg)
    sta   dirSizeLo
    lda   kArg+1
    sta   dirSizeHi
    ldy   #0                 // measure name length (null-terminated, <=11)
lab_dir_nlen:
    lda   (kPtr),y
    beq   lab_dir_ngot
    iny
    cpy   #11
    bcc   lab_dir_nlen
lab_dir_ngot:
    tya                     // A = len
    sta   dirPad            // pad = 11 - len
    lda   #11
    sec
    sbc   dirPad
    sta   dirPad
    jsr   kPrintString      // print name (clobbers kPtr/kArg)
lab_dir_pad:
    lda   dirPad            // pad name out to 11 columns
    beq   lab_dir_pdone
    dec   dirPad
    lda   #$20
    jsr   kPrintChar
    jmp   lab_dir_pad
lab_dir_pdone:
    lda   #$20              // extra space: longest name never touches size
    jsr   kPrintChar
    jsr   lab_dir_sizepad   // dirPad = 5 - decimal digits
lab_dir_rpad:
    lda   dirPad            // right-align the size column
    beq   lab_dir_rp_done
    dec   dirPad
    lda   #$20
    jsr   kPrintChar
    jmp   lab_dir_rpad
lab_dir_rp_done:
    lda   dirSizeHi
    ldx   dirSizeLo
    jsr   lab_295e          // print decimal size (A=hi, X=lo)
    jsr   lab_crlf          // newline
    inc   dirIdx
    bne   lab_dir_lp        // loop (entries are 1..511)
    inc   dirIdxHi
    jmp   lab_dir_lp
lab_dir_rt:
    rts

// lab_dir_sizepad -- dirPad = 5 - decimal digits of dirSizeLo/Hi
lab_dir_sizepad:
    lda   dirSizeHi
    bne   dsp_hg0
    lda   dirSizeLo         // value < 256
    cmp   #10
    bcc   dsp_4             // 1 digit
    cmp   #100
    bcc   dsp_3             // 2 digits
    lda   #2
    bne   dsp_set           // 3 digits (always)
dsp_hg0:
    cmp   #$27              // A = hi (>= 1)
    bcc   dsp_hi3
    bne   dsp_0             // hi > $27 -> 5 digits
    lda   dirSizeLo
    cmp   #$10
    bcs   dsp_0             // >= 10000 -> 5 digits
    lda   #1
    bne   dsp_set           // 4 digits (9984..9999)
dsp_hi3:
    cmp   #$03              // A = hi (1..$26)
    bcc   dsp_2             // hi 1..2 -> 3 digits
    bne   dsp_1             // hi 4..$26 -> 4 digits
    lda   dirSizeLo
    cmp   #$E8
    bcs   dsp_1             // >= 1000 -> 4 digits
dsp_2:
    lda   #2
    bne   dsp_set           // 3 digits
dsp_1:
    lda   #1
    bne   dsp_set           // 4 digits
dsp_0:
    lda   #0
    beq   dsp_set           // 5 digits
dsp_4:
    lda   #4
    bne   dsp_set           // 1 digit
dsp_3:
    lda   #3
dsp_set:
    sta   dirPad
    rts

dirIdx:
    .byte 0
dirIdxHi:
    .byte 0
dirSizeLo:
    .byte 0
dirSizeHi:
    .byte 0
dirPad:
    .byte 0
dirNameBuf:
    .fill 12, 0

// ==========================================
//  (kEditLine full-screen editor removed -- preserved verbatim in
//   reference/ehbasic-editline.asm for the future bitmap port.)
// ==========================================