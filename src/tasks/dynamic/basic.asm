// ==========================================
//  basic.asm -- EhBASIC dynamic task (Tier 2, fixed-origin $5000)
// ==========================================
//  One contiguous image: task entry + I/O layer + EhBASIC
//  interpreter + full-screen line editor. kDynLoad places the code
//  body at file offset 256, after the 256-byte header page.
//
//  Kernel API aliases below are RESOLVED addresses from the kernel
//  build (kernel.inc / kernel-vars.inc) -- NOT stable jump-table
//  entries. Keep them in sync with the kernel -- the jump table does not
//  expose them yet.
//  (Guard: src/main.asm .asserts these aliases against the kernel
//  labels -- a kernel move now fails the kernel build instead of
//  breaking BASIC silently.)
//
//  Assembled by tools/assemble-tasks.asm; no .pc here.
//  EhBASIC program RAM: the REU working file. The instance
//  buffer holds the 5-byte file record + line-buffer control bytes.

.encoding "screencode_mixed"

// --- Kernel symbol aliases (resolved from the kernel build) ---
//  (syscall alias defined globally in tools/assemble-tasks.asm;
//   shared kPtr/srcPtr/screenBase aliases also live there)
.const csrBoth          = kCsrBoth         // $E0A5 -- editor cursor save/restore

// --- Header (256-byte paged): prefix + name + fixup count + fixup_offset,
//     zero-padded so the code starts at file offset 256 ---
//  Relocatable: the loader fixes up the code at load; program RAM stays at
//  $8000-$A7FF (5 pool pages). The loader marks those pages before allocating
//  code pages, so the code can never land on program RAM.
.const ZP_BASE          = $04
.const ZP_LEN           = $EC
.const BASIC_FLAGS      = KF_RELOCATABLE
.const BASIC_DATA_SIZE  = 12                 // record(5) + line state(5); args ignored

basicBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte BASIC_FLAGS            // +2:  flags = RELOCATABLE (BASIC reads the keyboard, not arguments)
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word code_end - code_start  // +5:  code size
    .byte BASIC_DATA_SIZE        // +7:  instance data size (B/N + ceiling)
    .byte ZP_BASE                // +8:  ZP base
    .byte ZP_LEN                 // +9:  ZP len
    .word (1 << LIB_FILESYS) | (1 << LIB_GFX) | (1 << LIB_FP)  // +10: libMask
    .byte name_end - taskName    // +12: name len
taskName:
    .text "basic"                // +13: name (5 chars)
name_end:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - basicBinStart), 0  // pad -- code lands at file offset 256

// ==========================================
//  Code body (entry FIRST -- the loader spawns at code_start)
// ==========================================
code_start:

basic:
    // --- Screen setup ---
    //  kAllocScreen takes border/bg0/bg1/bg2 from kArg..kArg+3 -- set them
    //  explicitly (like the shell) or BASIC inherits stale border/bg from
    //  whatever the previous boot left in kArg.
    lda #$0b; sta kArg           // border = dark grey
    lda #$00
    sta kArg+1                   // bg0 = black
    sta kArg+2                   // bg1 (unused for hires)
    sta kArg+3                   // bg2 (unused for hires)
    lda #0                      // mode 0 = hires-text surface
    jsr kAllocScreen
    bcc basicHasScreen
basicHalt:
    sei                         // scheduler repaints $D020 -- halt atomically
    lda #$02                    // distinctive red border
    sta $d020
    jmp basicHalt
basicHasScreen:
    lda #SYS_GETTID; jsr syscall         // gettid -> A
    tax; jsr kSwitchView        // switch display + grab kbFocus
    jsr kClearScreen
    lda #1; jsr kSetCharShadow  // mirror text for kReadRow (editor)
    jsr kEnableCursor

    // --- Print banner ---
    lda #<msgBasicBanner; sta kPtr
    lda #>msgBasicBanner; sta kPtr+1
    jsr kPrintString

    // --- Set up EhBASIC I/O vectors ---
    lda #<vecIn; sta $0205     // VEC_IN = vecIn
    lda #>vecIn; sta $0206
    lda #<vecOut; sta $0207    // VEC_OUT = vecOut
    lda #>vecOut; sta $0208
    lda #<vecLoad; sta $0209   // VEC_LD = vecLoad
    lda #>vecLoad; sta $020A
    lda #<vecSave; sta $020B   // VEC_SV = vecSave
    lda #>vecSave; sta $020C

    // --- REU working file + line buffer ---
    //  Single fixed workspace file "basicwrk": reused when it exists,
    //  created (65535 bytes) otherwise. kReuOpen writes the 5-byte
    //  record over dataPtr+0..4.
    lda #<msgWorkName; sta kArg
    lda #>msgWorkName; sta kArg+1
    lda dataPtr; sta kPtr       // record buffer = instance buffer
    lda dataPtr+1; sta kPtr+1
    jsr kReuOpen
    bcc basicWorkOk
    lda #<msgWorkName; sta kArg // not found -- create it (65535 bytes)
    lda #>msgWorkName; sta kArg+1
    lda #$ff; sta kArg+2
    sta kArg+3
    jsr kReuCreate
    lda #<msgWorkName; sta kArg
    lda #>msgWorkName; sta kArg+1
    lda dataPtr; sta kPtr
    lda dataPtr+1; sta kPtr+1
    jsr kReuOpen
    bcc basicWorkOk
    jmp basicHalt               // still failed -> halt
basicWorkOk:
    //  BASIC is single-instance, so reuse fioBuf as the executing-line
    //  cache instead of spending a pool page that filesys.lib needs.
    ldy #5
    lda #<fioBuf; sta (dataPtr),y // line buffer addr lo
    iny
    lda #>fioBuf; sta (dataPtr),y // line buffer addr hi
    ldy #7
    lda #0; sta (dataPtr),y     // lnBase lo
    iny
    sta (dataPtr),y             // lnBase hi
    iny
    sta (dataPtr),y             // lnLen = 0

    // --- Jump to EhBASIC cold start ---
    jmp lab_cold                // never returns

    // Fallback if EhBASIC somehow returns
    lda #SYS_EXIT; jsr syscall         // exit

// --- Strings ---
msgBasicBanner:
    .text "gordon basic (ehbasic) v1.5"
    .byte 0
msgWorkName:
    .text "basicwrk"            // REU working-file name (screen codes)
    .byte 0

// ==========================================
//  I/O layer
// ==========================================

// --- PETSCII <-> screen code conversion ---
// EhBASIC is assembled with .encoding "ascii" ('A'=$41..'Z'=$5A).
// GordonOS kernel produces screen codes (kPrintChar expects screen codes).

ascii_to_screen:
    cmp #$5e                    // '^' -> screen code ^ ($1E)
    bne atsNotCaret
    lda #$1e
    rts
atsNotCaret:
    cmp #$0d                    // CR -- pass through for vecOut check
    beq atsDone
    cmp #$0a                    // LF -- pass through
    beq atsDone
    cmp #$41                    // 'A'
    bcc atsDone                 // below 'A' -- unchanged ($20-$3F pass through)
    cmp #$5b                    // 'Z'+1
    bcs atsNotUpper             // not A-Z
    // $41-$5A = ASCII 'A'-'Z' = C64 shifted screen codes -- no conversion
    rts
atsNotUpper:
    cmp #$61                    // 'a'
    bcc atsDone                 // $5B-$60 -- unchanged
    cmp #$7b                    // 'z'+1
    bcs atsDone                 // above 'z' -- unchanged
    sec
    sbc #$60                    // $61-$7A -> $01-$1A (screen codes, lowercase)
atsDone:
    rts

screen_to_ascii:
    // special C64 keys first
    cmp #$83                    // RUN/STOP key -> CTRL-C
    bne staNotStop
    lda #$03                    // -> ASCII CTRL-C
    sec                         // C=1: key available
    rts
staNotStop:
    cmp #$80                    // DEL key?
    bne staNotDel
    lda #$08                    // -> ASCII backspace
    sec                         // C=1: key available
    rts
staNotDel:
    cmp #$81                    // RETURN key?
    bne staNotRet
    lda #$0d                    // -> ASCII CR
    sec                         // C=1: key available
    rts
staNotRet:
    cmp #$1e                    // ^ key (^) -> ASCII '^' (the power operator)
    bne staNotUp
    lda #$5e
    sec                         // C=1: key available
    rts
staNotUp:
    // unshifted letters: screen codes $01-$1A -> ASCII lowercase 'a'-'z'
    cmp #$01
    bcc staPassthru             // < $01 -> pass through unchanged
    cmp #$1b
    bcs staCheckShift           // >= $1B -> check shifted letters
    clc
    adc #$60                    // $01-$1A -> $61-$7A (ASCII lowercase)
    sec                         // C=1: key available
    rts
staCheckShift:
    // shifted letters: screen codes $41-$5A -> ASCII uppercase 'A'-'Z'
    cmp #$41
    bcc staPassthru             // $1B-$40 -> pass through unchanged
    cmp #$5b
    bcs staPassthru             // >= $5B -> pass through unchanged
    // A already holds $41-$5A = ASCII 'A'-'Z' -- fall through
staPassthru:
    sec                         // C=1: key available
staDone:
    rts

// --- Output patch (replaces EhBASIC VEC_OUT) ---
// Input: A = ASCII character (EhBASIC assembled with .encoding "ascii")
// MUST preserve A, X, Y, and flags.
vecOut:
    stx voSaveX                 // save X
    sty voSaveY                 // save Y
    sta voSaveA                 // save original A
    php                         // save flags (especially Z)
    cmp #$08                    // ASCII backspace -> visual delete
    beq voBackspace
    cmp #$0d                    // CR -> newline
    beq voCR
    cmp #$0a                    // LF -> ignore
    beq voRestore
    jsr ascii_to_screen         // ASCII -> screen code
    jsr kPrintChar
    jmp voRestore
voBackspace:
    jsr kGetCursor              // X=col, Y=row
    cpx #0
    beq voRestore               // at column 0, nothing to delete
    dex
    jsr kSetCursor              // cursor left
    lda #$20                    // space (screen code)
    jsr kPrintChar              // overwrite char (un-XORs blink), advances right
    jsr kGetCursor              // X=col
    dex
    jsr kSetCursor              // back to erased position
    jmp voRestore
voCR:
    jsr kNewline                // blink XOR restored at saved cell by the IRQ
voRestore:
    plp                         // restore flags
    ldx voSaveX                 // restore X
    ldy voSaveY                 // restore Y
    lda voSaveA                 // restore original ASCII char
    stx $78                     // sync tempb -- lab_188a does ldx tempb after echo
voDone:
    rts

voSaveX: .byte 0
voSaveY: .byte 0
voSaveA: .byte 0

// --- Inline string print (replaces LAB_SCRNA) ---
vecScrn:
    pla; sta kPtr               // pop return addr lo
    pla; sta kPtr+1             // pop return addr hi
    jsr kPrintString            // prints null-term string, advances kPtr
    lda kPtr+1; pha             // push advanced addr hi
    lda kPtr; pha               // push advanced addr lo
    rts

// --- Single char input (replaces VEC_IN) ---
// NON-BLOCKING: returns C=0 if no key, C=1 with ASCII char in A.
vecIn:
    stx viSaveX                 // save X
    sty viSaveY                 // save Y
    jsr kScanKeyboard           // non-blocking; A=$FF if no key
    cmp #$ff
    beq viEmpty                 // return C=0
    jsr screen_to_ascii         // screen code -> ASCII (sets C=1)
    ldx viSaveX
    ldy viSaveY
    rts                         // C=1
viEmpty:
    ldx viSaveX
    ldy viSaveY
    clc                         // C=0: no key
    rts

viSaveX: .byte 0
viSaveY: .byte 0

// --- STOP key check (stub -- returns not-stopped) ---
vecStop:
    clc                         // C=0: not stopped
    rts

// ==========================================
//  EhBASIC Interpreter (ASCII encoding)
// ==========================================
.encoding "ascii"
.import source "ehbasic.asm"

// ==========================================
//  fp.lib wrappers (operator/function migration)
// ==========================================
//  lab_oppt/lab_ftbl rows repointed here call the lib by opcode.
//  Success (C=0): result sits in fac1, wrapper rts's back into the
//  evaluator. Error (C=1, A=FP_ERR_*): mapped to BASIC's error and
//  long-jumped to lab_xerr (never returns).
//    FP_ERR_DIV0($00) -> $14 "division by zero"
//    FP_ERR_OVERFLOW($01) -> $0a "overflow"
//    FP_ERR_RANGE($02)/FP_ERR_CONV($03) -> $08 "function call"

fpGo:
      jmp   kFpCall

fpAddW:  lda #FP_ADD; jmp fpCall
fpSubW:  lda #FP_SUB; jmp fpCall
fpMulW:  lda #FP_MUL; jmp fpCall
fpDivW:  lda #FP_DIV; jmp fpCall
fpPowW:  lda #FP_POW; jmp fpCall
fpGtW:   lda #FP_NEG; jmp fpCall  // unary minus + > tail (lab_gthan lives in the lib as FNEG)
fpSgnW:  lda #FP_SGN; jmp fpCall
fpIntW:  lda #FP_INT; jmp fpCall
fpAbsW:  lda #FP_ABS; jmp fpCall
fpSqrW:  lda #FP_SQR; jmp fpCall
fpRndW:  lda #FP_RND; jmp fpCall
fpLogW:  lda #FP_LOG; jmp fpCall
fpExpW:  lda #FP_EXP; jmp fpCall
fpCosW:  lda #FP_COS; jmp fpCall
fpSinW:  lda #FP_SIN; jmp fpCall
fpTanW:  lda #FP_TAN; jmp fpCall
fpAtnW:  lda #FP_ATN; jmp fpCall

fpCall:
      // A holds the lib opcode on entry -- the save loops below clobber
      // A, so push it first and pop it right before jsr fpGo (Rule 3).
      pha
      // The fp.lib shares this task's ZP window ($90-$C5). Three regions
      // must survive a call:
      //   $98-$9E -- live EhBASIC state: prstk ($99), comp_f ($9B),
      //             func_l/h ($9C/$9D), des_2l ($9E). The lib's
      //             ut2_pl/h ($98/$99 -- written by lab_27f8, used by
      //             FCMP and POW's negative-base branch), fact_1-3
      //             ($9A-$9C -- every multiply/divide) and adatal/h
      //             ($9D/$9E -- series eval) alias them. A clobbered
      //             prstk turns lab_1b1d+3's bpl test positive, so the
      //             evaluator re-dispatches the '>' row and runs into
      //             lab_sner.
      //   $BC/$BD -- fpDispA/fpDispX, inside lab_igby's ZP trampoline.
      //   $C1-$C5 -- fpPwTmp (pow/tan); $C2-$C4 = lab_gbyt trampoline.
      //             bpntrl/h live at $C8/$C9 (outside the fp window).
      ldx   #$06
fpSav0: lda   $98,x
        sta   fpSave,x
        dex
        bpl   fpSav0
      ldx   #$01
fpSav1: lda   $bc,x
        sta   fpSave+7,x
        dex
        bpl   fpSav1
      ldx   #$04
fpSav2: lda   $c1,x
        sta   fpSave+9,x
        dex
        bpl   fpSav2
      pla                       // A = opcode (pla preserves C)
      jsr   fpGo
      pha                       // save result A (lib error code on C=1 --
                                // the restore loops' lda clobber A)
      ldx   #$06
fpRst0: lda   fpSave,x
        sta   $98,x
        dex
        bpl   fpRst0
      ldx   #$01
fpRst1: lda   fpSave+7,x
        sta   $bc,x
        dex
        bpl   fpRst1
      ldx   #$04
fpRst2: lda   fpSave+9,x
        sta   $c1,x
        dex
        bpl   fpRst2
      pla                       // A = result/error code (pla preserves C)
      bcs   fpErrMap
      rts

fpSave: .fill 14, 0             // $98-$9E, $BC/$BD, $C1-$C5 (C preserved by loops)

fpErrMap:
      cmp   #FP_ERR_DIV0
      beq   fpEDiv0
      cmp   #FP_ERR_OVERFLOW
      beq   fpEOver
      ldx   #$08              // range/conversion -> function call error
      jmp   fpEDo
fpEDiv0:
      ldx   #$14              // "divide by zero"
      jmp   fpEDo
fpEOver:
      ldx   #$0a              // "overflow"
fpEDo:
      jmp   lab_xerr

// --- Full-screen line editor (bitmap port) ---
.import source "basicedit.inc"

// --- Line input patch (after ehbasic.asm) ---
vecInln:
    lda #<ibuffs; sta kPtr       // fill EhBASIC's input buffer directly
    lda #>ibuffs; sta kPtr+1
    ldx #ibuffe-ibuffs           // max length from buffer size
    jsr basicedit.kEditLine    // Y = length, buffer = screen codes
    sty viSaveY                 // save length for conversion loop
    cpy #0                      // sty doesn't set flags on 6502
    beq vecInlnEmpty            // empty? skip conversion
    ldy #0
vecInlnCvt:
    lda ibuffs,y                // get screen code
    jsr screen_to_ascii         // convert to ASCII
    sta ibuffs,y                // store ASCII back
    iny
    cpy viSaveY
    bcc vecInlnCvt
vecInlnEmpty:
    rts

// ==========================================
//  SAVE "name" -- export working file to REU FS as "name.bas"
// ==========================================
//  Evaluates the filename string expression with EhBASIC's own
//  evaluator, validates it (1-7 chars, no '.'), appends ".bas", and
//  copies working-file bytes [0, svarl) through the line-buffer page.
.encoding "screencode_mixed"

vecSave:
    jsr   bsEval              // C=0 ok, C=1 error already printed
    bcc   bsSvName
    jmp   bsRts
bsSvName:
    lda   #<bsNameBuf         // create target .bas at exact program-image size
    sta   kArg
    lda   #>bsNameBuf
    sta   kArg+1
    lda   svarl               // size = [0, svarl), B_lo = 0
    sta   kArg+2
    lda   svarh
    sta   kArg+3
    jsr   kReuCreate
    bcc   bsSvCreated
    lda   kArg+6              // error code
    beq   bsExists            // 0 = duplicate
    cmp   #1
    beq   bsNoSpace           // 1 = no space
    cmp   #2
    beq   bsDirFull           // 2 = dir full
    jmp   bsBadName           // 3/4/5/other
bsSvCreated:
    lda   #<bsNameBuf
    sta   kArg
    lda   #>bsNameBuf
    sta   kArg+1
    lda   #<bsFileRec
    sta   kPtr
    lda   #>bsFileRec
    sta   kPtr+1
    jsr   kReuOpen            // resolve target record for writeAt
    bcs   bsNoSpace           // should not fail after create
    lda   dataPtr             // source = working file record
    sta   bsSrcRec
    lda   dataPtr+1
    sta   bsSrcRec+1
    lda   #<bsFileRec         // dest = .bas file record
    sta   bsDstRec
    lda   #>bsFileRec
    sta   bsDstRec+1
    lda   svarl
    sta   bsCopyLen
    lda   svarh
    sta   bsCopyLen+1
    jsr   bsCopyReuRange
    bcc   bsOk
    jmp   bsNoSpace
bsExists:
    lda   #<bsMsgExists
    ldx   #>bsMsgExists
    jmp   bsPrt
bsNoSpace:
    lda   #<bsMsgNoSpace
    ldx   #>bsMsgNoSpace
    jmp   bsPrt
bsDirFull:
    lda   #<bsMsgDirFull
    ldx   #>bsMsgDirFull
    jmp   bsPrt
bsBadName:
    lda   #<bsMsgBadName
    ldx   #>bsMsgBadName
    jmp   bsPrt
bsOk:
    lda   #<bsMsgSaved
    ldx   #>bsMsgSaved
bsPrt:
    sta   kPtr
    stx   kPtr+1
    jsr   kPrintString
    jsr   kNewline
bsRts:
    rts

// bsEval -- evaluate "name" expression; build screen-code "name.bas".
//  Out: C=0, bsNameBuf holds null-term screen-code name (1-7 chars + .bas)
//       C=1, "? bad name" already printed
bsEval:
    jsr   lab_evex           // evaluate expression (back up one byte first)
    jsr   lab_evst           // check string + pop descriptor (A=len, X/Y=ptr)
    stx   ut1_pl             // string pointer (ASCII bytes, REU-aware)
    sty   ut1_ph
    cmp   #0
    beq   bsBad              // empty name
    cmp   #8                 // >7 chars?
    bcs   bsBad
    sta   bsLen
    ldy   #0
bsCopy:
    jsr   readStrByte        // ASCII name char
    cmp   #'.'               // reject '.'
    beq   bsBad
    jsr   ascii_to_screen    // ASCII -> screen code
    sta   bsNameBuf,y
    iny
    cpy   bsLen
    bcc   bsCopy
    ldx   #0
bsSfx:
    lda   bsBas,x            // ".bas" (screen codes)
    sta   bsNameBuf,y
    inx
    iny
    cpx   #4
    bcc   bsSfx
    lda   #0
    sta   bsNameBuf,y        // null-terminate
    clc
    rts
bsBad:
    lda   #<bsMsgBadName
    sta   kPtr
    lda   #>bsMsgBadName
    sta   kPtr+1
    jsr   kPrintString
    jsr   kNewline
    sec
    rts

bsNameBuf:
    .fill 12, 0
bsFileRec:
    .fill 5, 0
bsSrcRec:
    .word 0
bsDstRec:
    .word 0
bsCopyLen:
    .word 0
bsCopyOff:
    .word 0
bsChunk:
    .word 0
bsNextLo:
    .byte 0
bsLen:
    .byte 0
bsBas:
    .text ".bas"
bsMsgSaved:
    .text "saved"
    .byte 0
bsMsgExists:
    .text "? exists"
    .byte 0
bsMsgNoSpace:
    .text "? no space"
    .byte 0
bsMsgDirFull:
    .text "? dir full"
    .byte 0
bsMsgBadName:
    .text "? bad name"
    .byte 0

// ==========================================
//  LOAD "name" -- import program from REU FS ("name.bas")
// ==========================================
//  Copies the .bas file into the per-instance working file at offset 0,
//  resets vars/string space, relinks lines, and warm-starts.
vecLoad:
    jsr   bsEval              // C=0 ok, C=1 error already printed
    bcc   bsLdName            // branch short (forward)
    jmp   bsRts               // error already printed -- return
bsLdName:
    lda   #<bsNameBuf
    sta   kArg
    lda   #>bsNameBuf
    sta   kArg+1
    lda   #<bsFileRec
    sta   kPtr
    lda   #>bsFileRec
    sta   kPtr+1
    jsr   kReuOpen
    bcc   bsLdOk
    lda   #<bsMsgNotFound     // C=1: not found
    ldx   #>bsMsgNotFound
    jmp   bsPrt
bsLdOk:
    lda   #<bsFileRec         // source = .bas file record
    sta   bsSrcRec
    lda   #>bsFileRec
    sta   bsSrcRec+1
    lda   dataPtr             // dest = working file record
    sta   bsDstRec
    lda   dataPtr+1
    sta   bsDstRec+1
    lda   bsFileRec           // size lo
    sta   bsCopyLen
    lda   bsFileRec+1         // size hi
    sta   bsCopyLen+1
    jsr   bsCopyReuRange
    bcc   bsLdCopied
    jmp   bsNoSpace
bsLdCopied:
    lda   bsFileRec           // size lo (B_lo = 0 -> no carry)
    sta   svarl
    clc
    lda   bsFileRec+1         // size hi
    adc   smemh               // + B_hi
    sta   svarh
    jsr   bsRelink            // relative links -> absolute (position-independent load)
    jsr   lab_1477            // reset execution, vars, data pointer, stack
    jmp   lab_1274            // warm start -- never returns

// bsRelink -- rebuild absolute line links after LOAD. The .bas file stores
//  RELATIVE (0-based) links; this walks the program from smem (B+1) and
//  recomputes each link as line_start + length, so the program loads
//  correctly regardless of where BASIC's program RAM was allocated.
//  Stops at a zero link (both bytes). Clobbers A, X, Y, ut1.
bsRelink:
    ldx   smeml               // X/A = smem = program start (B+1)
    lda   smemh
bsRlLoop:
    stx   ut1_pl
    sta   ut1_ph              // ut1 = current line offset
    ldy   #0
    jsr   peekVarByte         // link lo
    bne   bsRlLine            // non-zero -> a line follows
    iny
    jsr   peekVarByte         // link hi (only when lo == 0)
    beq   bsRlDone            // link == 0 -> end of program
bsRlLine:
    ldy   #4                  // first text byte
bsRlEol:
    iny
    jsr   peekVarByte
    bne   bsRlEol             // until 0 (end of line)
    sec
    tya                       // A = Y (eol index)
    adc   ut1_pl              // A = next_lo (ut1_lo + Y + 1)
    sta   bsNextLo
    ldy   #0
    jsr   pokeVarByte         // store link lo
    lda   #0
    adc   ut1_ph              // A = next_hi (ut1_hi + carry)
    iny
    jsr   pokeVarByte         // store link hi
    ldx   bsNextLo            // X/A = next line offset
    jmp   bsRlLoop            // X = next_lo, A = next_hi
bsRlDone:
    rts

bsMsgNotFound:
    .text "? not found"
    .byte 0

bsLineBufToKPtr:
    ldy   #5
    lda   (dataPtr),y
    sta   kPtr
    iny
    lda   (dataPtr),y
    sta   kPtr+1
    rts

bsCopyReuRange:
    lda   #0
    sta   lnLenZ              // fioBuf is the line cache and copy bounce
    sta   bsCopyOff
    sta   bsCopyOff+1
bsCopyLoop:
    lda   bsCopyLen
    ora   bsCopyLen+1
    bne   bsCopyMore
    clc
    rts
bsCopyMore:
    lda   bsCopyLen+1
    beq   bsCopySmall
    lda   #0
    sta   bsChunk
    lda   #1
    sta   bsChunk+1
    jmp   bsCopyChunk
bsCopySmall:
    lda   bsCopyLen
    sta   bsChunk
    lda   #0
    sta   bsChunk+1
bsCopyChunk:
    lda   bsSrcRec
    sta   kArg
    lda   bsSrcRec+1
    sta   kArg+1
    lda   bsCopyOff
    sta   kArg+2
    lda   bsCopyOff+1
    sta   kArg+3
    lda   bsChunk
    sta   kArg+6
    lda   bsChunk+1
    sta   kArg+7
    jsr   bsLineBufToKPtr
    jsr   kReuReadAt
    bcs   bsCopyFail
    lda   bsDstRec
    sta   kArg
    lda   bsDstRec+1
    sta   kArg+1
    lda   bsCopyOff
    sta   kArg+2
    lda   bsCopyOff+1
    sta   kArg+3
    lda   bsChunk
    sta   kArg+6
    lda   bsChunk+1
    sta   kArg+7
    jsr   bsLineBufToKPtr
    jsr   kReuWriteAt
    bcs   bsCopyFail
    lda   bsCopyOff
    clc
    adc   bsChunk
    sta   bsCopyOff
    lda   bsCopyOff+1
    adc   bsChunk+1
    sta   bsCopyOff+1
    sec
    lda   bsCopyLen
    sbc   bsChunk
    sta   bsCopyLen
    lda   bsCopyLen+1
    sbc   bsChunk+1
    sta   bsCopyLen+1
    jmp   bsCopyLoop
bsCopyFail:
    sec
    rts

// ==========================================
//  Graphics statement handlers
// ==========================================
//  Dispatched from lab_gxctbl (ehbasic.asm). On entry bpntrl points at
//  the first argument byte (or the statement terminator for no-arg
//  commands); return with bpntrl at the terminator. Arguments are
//  evaluated with EhBASIC's evaluator; drawing is via direct kernel
//  calls.
//
//  gxCharCol = value-1 colour (matrix high nibble; set by PEN1/COLOUR).
//  gxPen2Col = value-2 colour (matrix low nibble; set by PEN2).
//  gxRefillMatrix packs both: matrix byte = (gxCharCol<<4) | gxPen2Col.

gxCharCol:
    .byte $0f                  // DEFAULT_TEXT_COLOR (light grey)
gxPen2Col:
    .byte 0                    // MC value-2 colour (matrix low nibble)
gxMode:
    .byte 0                    // 0=hires, 1=multicolor (screenKind-2 mirror)

// gxNum -- evaluate numeric expression -> 16-bit in itempl/itemph
//  (lab_f2fx also returns A=hi, Y=lo). Clobbers A,X,Y + EhBASIC ZP.
gxNum:
    jsr lab_evnm               // FAC1 = number (else type-mismatch error)
    jmp lab_f2fx               // itempl/itemph = 16-bit; rts

// gxChkX -- refuse x (itempl/itemph) out of range: "illegal quantity".
//  MC: x must be 0..159. Hires: x must be 0..319. Clobbers A; error path
//  (ldx #$24; jmp lab_xerr) never returns. itempl/itemph preserved.
gxChkX:
    lda gxMode
    bne gxChkXMc
    lda itemph
    beq gxChkXOk              // itemph == 0 -> x 0..255, valid
    cmp #2
    bcs gxChkXErr             // itemph >= 2 -> x >= 512 or negative
    lda itempl                // itemph == 1 -> x 256..319
    cmp #64
    bcs gxChkXErr             // x >= 320
gxChkXOk:
    rts
gxChkXMc:
    lda itemph
    bne gxChkXErr
    lda itempl
    cmp #160
    bcs gxChkXErr             // x >= 160
    rts
gxChkXErr:
    ldx #$24
    jmp lab_xerr

// gxChkY -- refuse y (itempl/itemph) > 199 (all modes): "illegal quantity".
//  Clobbers A; error path never returns. itempl/itemph preserved.
gxChkY:
    lda itemph
    bne gxChkYErr
    lda itempl
    cmp #200
    bcs gxChkYErr
    rts
gxChkYErr:
    ldx #$24
    jmp lab_xerr

// gxTodo -- placeholder for not-yet-implemented commands
gxTodo:
    jmp lab_sner               // ?syntax error (command not wired yet)

// gxRefillMatrix -- refill the whole matrix from the pen colours.
//  Matrix byte = (gxCharCol << 4) | gxPen2Col (value-1 | value-2 colour).
//  Clobbers A (kFillMatrix clobbers A,X,Y,kPtr,kArg).
gxRefillMatrix:
    lda gxCharCol
    asl; asl; asl; asl        // high nibble = value-1 colour
    ora gxPen2Col             // low nibble = value-2 colour
    jsr kFillMatrix           // fill matrix with the packed byte
    rts

// Program-mode expression reads use kArg as REU DMA scratch, so multi-arg
// statements must stage their partially parsed values elsewhere.
gxArgBuf: .fill 7, 0
gxArgCopy7:
    ldy #6
gxArgCopy7Lp:
    lda gxArgBuf,y
    sta kArg,y
    dey
    bpl gxArgCopy7Lp
    rts

// --- CLS ---
gxDoCls:
    jsr kClearScreen           // pixels + matrix + char shadow + cursor home
    jsr gxRefillMatrix         // refill matrix with both pen nibbles
    rts

// --- PLOT x,y,c ---
gxDoPlot:
    jsr gxNum                 // x
    jsr gxChkX
    lda itempl; sta gxArgBuf      // x lo
    lda itemph; sta gxArgBuf+1    // x hi
    jsr lab_1c01              // ","
    jsr gxNum                 // y
    jsr gxChkY
    lda itempl; sta gxArgBuf+2    // y
    jsr lab_1c01              // ","
    jsr gxNum                 // colour
    ldx gxArgBuf                  // x lo
    ldy gxArgBuf+2                // y
    lda gxArgBuf+1                // x hi
    lsr                       // C = x hi bit
    lda itempl                // colour (C preserved)
    jsr kPlot
    rts

// --- CURSOR 0|1 ---
gxDoCursor:
    jsr gxNum                 // 0 = off, non-zero = on
    lda itempl
    beq gxCurOff
    jsr kEnableCursor
    rts
gxCurOff:
    jsr kDisableCursor
    rts

// --- LINE x1,y1,x2,y2,c ---
gxDoLine:
    jsr gxNum; jsr gxChkX; lda itempl; sta gxArgBuf; lda itemph; sta gxArgBuf+1    // x1
    jsr lab_1c01
    jsr gxNum; jsr gxChkY; lda itempl; sta gxArgBuf+2                            // y1
    jsr lab_1c01
    jsr gxNum; jsr gxChkX; lda itempl; sta gxArgBuf+4; lda itemph; sta gxArgBuf+5    // x2 (kArg+3 unused)
    jsr lab_1c01
    jsr gxNum; jsr gxChkY; lda itempl; sta gxArgBuf+6                            // y2
    jsr lab_1c01
    jsr gxNum; lda itempl                                        // colour
    pha
    jsr gxArgCopy7
    pla
    jsr kLine
    rts

// --- BOX x,y,w,h,c ---
gxDoBox:
    jsr gxNum; jsr gxChkX; lda itempl; sta gxArgBuf; lda itemph; sta gxArgBuf+1    // x
    jsr lab_1c01
    jsr gxNum; jsr gxChkY; lda itempl; sta gxArgBuf+2                            // y
    jsr lab_1c01
    jsr gxNum; lda itempl; sta gxArgBuf+4; lda itemph; sta gxArgBuf+5    // w (kArg+3 unused)
    jsr lab_1c01
    jsr gxNum; lda itempl; sta gxArgBuf+6                            // h
    jsr lab_1c01
    jsr gxNum; lda itempl                                        // colour
    pha
    jsr gxArgCopy7
    pla
    jsr kBox
    rts

// --- FILLBOX x,y,w,h,c ---
gxDoFillBox:
    jsr gxNum; jsr gxChkX; lda itempl; sta gxArgBuf; lda itemph; sta gxArgBuf+1    // x
    jsr lab_1c01
    jsr gxNum; jsr gxChkY; lda itempl; sta gxArgBuf+2                            // y
    jsr lab_1c01
    jsr gxNum; lda itempl; sta gxArgBuf+4; lda itemph; sta gxArgBuf+5    // w (kArg+3 unused)
    jsr lab_1c01
    jsr gxNum; lda itempl; sta gxArgBuf+6                            // h
    jsr lab_1c01
    jsr gxNum; lda itempl                                        // colour
    pha
    jsr gxArgCopy7
    pla
    jsr kFillBox
    rts

// --- CIRCLE x,y,r,c ---
gxDoCircle:
    jsr gxNum; jsr gxChkX; lda itempl; sta gxArgBuf; lda itemph; sta gxArgBuf+1    // cx
    jsr lab_1c01
    jsr gxNum; jsr gxChkY; lda itempl; sta gxArgBuf+2                            // cy
    jsr lab_1c01
    jsr gxNum; lda itempl; sta gxArgBuf+3                            // r
    jsr lab_1c01
    jsr gxNum; lda itempl                                        // colour
    pha
    jsr gxArgCopy7
    pla
    jsr kCircle
    rts

// --- ELLIPSE cx,cy,rx,ry,c ---
gxDoEllipse:
    jsr gxNum; jsr gxChkX; lda itempl; sta gxArgBuf; lda itemph; sta gxArgBuf+1    // cx
    jsr lab_1c01
    jsr gxNum; jsr gxChkY; lda itempl; sta gxArgBuf+2                            // cy
    jsr lab_1c01
    jsr gxNum; lda itempl; sta gxArgBuf+3                            // rx
    jsr lab_1c01
    jsr gxNum; lda itempl; sta gxArgBuf+4                            // ry
    jsr lab_1c01
    jsr gxNum; lda itempl                                        // colour
    pha
    jsr gxArgCopy7
    pla
    jsr kEllipse
    rts

// --- FLOOD x,y,c ---
gxDoFlood:
    jsr gxNum; jsr gxChkX; lda itempl; sta gxArgBuf; lda itemph; sta gxArgBuf+1    // x
    jsr lab_1c01
    jsr gxNum; jsr gxChkY; lda itempl; sta gxArgBuf+2                            // y
    jsr lab_1c01
    jsr gxNum; lda itempl                                        // colour
    pha
    jsr gxArgCopy7
    pla
    jsr kFloodFill
    rts

// --- GETPIXEL(x,y) -- function (lab_ftbl); x/y staged by lab_ppgn ---
gxGpX:  .byte 0                 // pixel column lo
gxGpXh: .byte 0                 // pixel column hi
gxGpY:  .byte 0                 // pixel row
gxGpYh: .byte 0                 // pixel row hi
gxFnGetPixel:
    lda gxGpX; sta itempl
    lda gxGpXh; sta itemph
    jsr gxChkX
    lda gxGpY; sta itempl
    lda gxGpYh; sta itemph
    jsr gxChkY
    ldx gxGpX                   // X = col lo
    lda gxGpXh
    lsr                         // C = col hi
    ldy gxGpY                   // Y = row
    jsr kGetPixel               // A = bit mask (nonzero=set) or 0; C=1 -> A=0
    beq gxGpZero
    lda #1                      // hires: normalize nonzero -> 1
gxGpZero:
    tay
    jmp lab_1fd0                // FAC1 = Y, rts

// --- LOCATE col,row ---
gxDoLocate:
    jsr gxNum                 // col
    lda itempl
    sta gxArgBuf              // gxNum/lab_1c01 do touch kArg in program mode
    jsr lab_1c01              // ","
    jsr gxNum                 // row
    ldx gxArgBuf              // X = col
    ldy itempl                // Y = row
    jsr kSetCursor
    rts

// --- COLOUR c -- text colour; doubles as the hires drawing colour ---
gxDoColour:
    jsr gxNum                 // colour
    lda itempl
    sta gxCharCol             // keep CLS/PEN1 matrix refill in sync
    jsr kSetTextColor         // A = colour
    rts

// --- BORDER c ---
gxDoBorder:
    jsr gxNum                 // colour
    lda itempl
    ldx #0
    jsr kSetGfxColor
    rts

// --- SCROLL top,bottom -- set region; cursor -> top-left of the region ---
gxScrTop: .byte 0
gxDoScroll:
    jsr gxNum; lda itempl; sta gxScrTop  // stash top (code space survives kernel)
    jsr lab_1c01
    jsr gxNum; ldx itempl                // X = bottom
    lda gxScrTop                         // A = top
    jsr kSetScrollRegion
    bcs gxScrDone                        // C=1 invalid -- region unchanged
    ldx #0                               // col 0
    ldy gxScrTop                         // row = top
    jsr kSetCursor
gxScrDone:
    rts

// --- SHADOW 0|1 ---
gxDoShadow:
    jsr gxNum                 // 0 = off, non-zero = on
    lda itempl
    jsr kSetCharShadow
    rts

// --- SCROLLBY n -- scroll the current region n rows (0-24) ---
gxSbCount: .byte 0
gxDoScrollby:
    jsr gxNum                 // n
    lda itempl
    beq gxSbDone              // 0 -> nothing
    sta gxSbCount
gxSbLoop:
    jsr kScrollScreen
    dec gxSbCount
    bne gxSbLoop
gxSbDone:
    rts

// --- PRINTAT col,row,c,"str" ---
gxPatCol: .byte 0
gxPatRow: .byte 0
gxPatLen: .byte 0
gxDoPrintAt:
    jsr gxNum; lda itempl; sta gxPatCol       // col
    jsr lab_1c01
    jsr gxNum; lda itempl; sta gxPatRow       // row
    jsr lab_1c01
    jsr gxNum; lda itempl; jsr kSetTextColor  // colour
    jsr lab_1c01
    jsr lab_evex              // evaluate string expression
    jsr lab_evst              // A=len, X/Y=ptr (ASCII)
    stx itempl                // string pointer -> itempl/itemph ($11/$12)
    sty itemph
    sta gxPatLen
gxPatLoop:
    lda gxPatLen
    beq gxPatDone
    ldy #0
    lda (itempl),y            // ASCII char
    jsr ascii_to_screen       // -> screen code
    pha                       // save screen code (kPrintCharAt clobbers A)
    inc itempl                // advance string pointer
    bne gxPatAdv
    inc itemph
gxPatAdv:
    ldx gxPatCol              // X = col
    ldy gxPatRow              // Y = row
    pla                       // A = screen code
    jsr kPrintCharAt
    inc gxPatCol
    dec gxPatLen
    jmp gxPatLoop
gxPatDone:
    rts

// --- GCHAR x,y,"ch",c -- draw one glyph at pixel (x,y) ---
gxGcChar: .byte 0
gxDoGChar:
    jsr gxNum; jsr gxChkX; lda itempl; sta gxArgBuf; lda itemph; sta gxArgBuf+1    // x
    jsr lab_1c01
    jsr gxNum; jsr gxChkY; lda itempl; sta gxArgBuf+2                            // y
    jsr lab_1c01
    jsr lab_evex              // string expression (ch)
    jsr lab_evst              // A=len, X/Y=ptr
    stx itempl                // itempl/itemph = string pointer
    sty itemph
    ldy #0
    lda (itempl),y            // first char (ASCII)
    jsr ascii_to_screen       // -> screen code
    sta gxGcChar              // stash (gxNum clobbers X)
    jsr lab_1c01              // ","
    jsr gxNum                 // colour
    pha
    jsr gxArgCopy7
    ldx gxGcChar              // X = char
    pla                       // A = colour
    jsr kGfxChar
    rts

// --- GTEXT x,y,c,"str" -- draw a string at pixel (x,y) ---
gxTexX:   .byte 0
gxTexXh:  .byte 0
gxTexY:   .byte 0
gxTexCol: .byte 0
gxTexLen: .byte 0
gxDoGText:
    jsr gxNum; jsr gxChkX; lda itempl; sta gxTexX; lda itemph; sta gxTexXh  // x
    jsr lab_1c01
    jsr gxNum; jsr gxChkY; lda itempl; sta gxTexY                            // y
    jsr lab_1c01
    jsr gxNum; lda itempl; sta gxTexCol                          // colour
    jsr lab_1c01
    jsr lab_evex              // string expression
    jsr lab_evst              // A=len, X/Y=ptr
    stx itempl                // itempl/itemph = string pointer
    sty itemph
    sta gxTexLen
gxTexLoop:
    lda gxTexLen
    beq gxTexDone
    ldy #0
    lda (itempl),y            // ASCII char
    jsr ascii_to_screen       // -> screen code
    tax                       // X = char
    lda gxTexX;   sta kArg    // x lo
    lda gxTexXh;  sta kArg+1  // x hi
    lda gxTexY;   sta kArg+2  // y
    lda gxTexCol              // A = colour
    jsr kGfxChar
    inc itempl                // advance string pointer
    bne gxTexAdv
    inc itemph
gxTexAdv:
    lda gxTexX                // x += 8 (glyph pitch)
    clc
    adc #8
    sta gxTexX
    lda gxTexXh
    adc #0
    sta gxTexXh
    dec gxTexLen
    jmp gxTexLoop
gxTexDone:
    rts

// --- GDEF n,b0..b7 -- define custom glyph #n (16 glyphs x 8 bytes) ---
gxGlyphs: .fill 128, 0
gxGdIdx:  .byte 0
gxGdPtrLo: .byte 0
gxGdPtrHi: .byte 0
gxDoGdef:
    jsr gxNum                 // n (glyph #)
    lda itempl
    and #$0f                  // clamp 0-15
    asl; asl; asl             // n*8
    clc
    adc #<gxGlyphs
    sta kPtr
    lda #0
    adc #>gxGlyphs
    sta kPtr+1                // kPtr = gxGlyphs + n*8 (survives gxNum)
    lda kPtr
    sta gxGdPtrLo
    lda kPtr+1
    sta gxGdPtrHi
    lda #0
    sta gxGdIdx               // byte index 0..7 (Y is clobbered by gxNum)
gxGdefLoop:
    jsr lab_1c01              // ","
    jsr gxNum                 // byte value -> itempl
    lda itempl
    lda gxGdPtrLo
    sta kPtr
    lda gxGdPtrHi
    sta kPtr+1
    lda itempl
    ldy gxGdIdx               // reload index after the jsr calls
    sta (kPtr),y              // store glyph byte
    inc gxGdIdx
    lda gxGdIdx
    cmp #8
    bcc gxGdefLoop
    rts

// --- GLYPH n -- print custom glyph #n at the text cursor ---
gxDoGlyph:
    jsr gxNum                 // n
    lda itempl
    and #$0f
    asl; asl; asl
    clc
    adc #<gxGlyphs
    sta kPtr
    lda #0
    adc #>gxGlyphs
    sta kPtr+1
    jsr kPrintGlyph
    rts

// --- MODE n -- 0=hires, 1=multicolor (colours preserved across the switch) ---
gxDoMode:
    jsr gxNum                 // n (0=hires, 1=multicolor)
    lda itempl
    and #$01                  // clamp 0/1
    sta gxMode                // mirror for gxChkX range checks
    jsr kSetGfxMode           // preserves border/bg0/bg1/bg2
    rts

// --- PEN0 c -- pixel value 0 = background ($D021) ---
gxDoPen0:
    jsr gxNum                 // colour
    lda itempl
    ldx #1
    jsr kSetGfxColor          // $D021
    rts

// --- PEN1 c -- pixel value 1 = video matrix high nibble ---
gxDoPen1:
    jsr gxNum                 // colour
    lda itempl
    sta gxCharCol             // remember for CLS (foreground colour)
    jsr gxRefillMatrix        // refill both nibbles
    rts

// --- PEN2 c -- pixel value 2 = video matrix low nibble ---
gxDoPen2:
    jsr gxNum                 // colour
    lda itempl
    sta gxPen2Col             // remember for refills
    jsr gxRefillMatrix        // refill both nibbles
    rts

// --- PEN3 c -- pixel value 3 = colour RAM (4-bit, low nibble) ---
gxDoPen3:
    jsr gxNum                 // colour
    lda itempl
    jsr kFillColorRam         // fill all 1000 colour-RAM slots with c
    rts

// End of code body -- ram_base is pinned in ehbasic.asm.
ehbasic_end:
code_end:

// ==========================================
//  Build-time guards (section 3.5 no-spill, section 3.3 pins)
// ==========================================
.assert "decss buffer reaches kernel ZP", (decss + 16 <= $F0) ? 1 : 0, 1
.assert "declared ZP range exceeds $F0", (ZP_BASE + ZP_LEN <= $F0) ? 1 : 0, 1
.assert "basic flags must be $01 (relocatable, no args)", (BASIC_FLAGS == $01) ? 1 : 0, 1
