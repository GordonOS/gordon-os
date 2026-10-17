// ==========================================
//  string.asm -- dynamic library "string.lib" (lib 4, 7 entries)
// ==========================================
//  strlen / strcpy / strcmp / skipSpaces / byteToHex / hexDigit /
//  nibbleToHex -- string manipulation and byte formatting, out of the resident
//  kernel. Entry-table order = call-index
//  order: strlen(22), strcpy(23), strcmp(24), skipSpaces(25),
//  byteToHex(26), hexDigit(27), nibbleToHex(28) -- base 22 =
//  time(3)+filesys(5)+gfx(13)+fp(1).
//  strcmp is dual-mode: kArg=0 exact compare, kArg=1 wildcard match
//  ('*' any run incl. empty, '?' any single char).
//  LAZY (lib id 4): time.lib calls kByteToHex and filesys.lib calls
//  kStrlen, and libs cannot declare lib dependencies (libMask=0), so
//  this lib must self-load on first call.
//  byteToHex uses a LIB-LOCAL hexChars table (no fixed-address dep),
//  like time.lib's timeLibBuf.

.encoding "screencode_mixed"

stringLibBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word stringLibEnd - stringLibCode // +5: code size
    .byte 0, 0, 0                // +7:  data size, ZP base, ZP len
    .word 0                      // +10: reserved (libs consume no libMask)
    .byte stringLibNameEnd - stringLibName  // +12: name len
stringLibName:
    .text "string"
stringLibNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - stringLibBinStart), 0

stringLibCode:
    .word stringLibStrlen, stringLibStrcpy, stringLibStrcmp
    .word stringLibSkipSpaces, stringLibByteToHex, stringLibHexDigit
    .word stringLibNibbleToHex   // entry table (7 entries)

// --- strlen -- length of null-terminated string at (kPtr) -> A ---
stringLibStrlen:
    ldy #0
slibSlLoop:
    lda (kPtr),y
    beq slibSlDone
    iny
    bne slibSlLoop
slibSlDone:
    tya
    rts

// --- strcpy -- copy null-terminated string (kPtr) -> (srcPtr) ---
stringLibStrcpy:
    ldy #0
slibScLoop:
    lda (kPtr),y
    sta (srcPtr),y
    beq slibScDone
    iny
    bne slibScLoop
slibScDone:
    rts

// --- strcmp -- compare (kPtr) vs (srcPtr), or wildcard match ---
//  kArg = mode: 0 = exact, 1 = wildcard.
//  Exact:    Z=1 equal, Z=0 differ (both null-terminated).
//  Wildcard: kPtr = name, srcPtr = pattern. '*' = any run (incl. empty),
//            '?' = any single char. Z=1 match, Z=0 no match. Pointer-
//            advancement walk -- both strings read via (zp),y, so no
//            self-modifying code.
//  Clobbers: exact -- A, Y. wildcard -- A, Y, kPtr, srcPtr, kArg+2..kArg+7.
//  X preserved (both modes).
stringLibStrcmp:
    lda kArg
    beq slibSmExact
    jmp slibSmWild
slibSmExact:
    ldy #0
slibSmLoop:
    lda (kPtr),y
    cmp (srcPtr),y
    bne slibSmDone              // differ -> Z=0
    lda (kPtr),y
    beq slibSmDone              // both NUL (equal) -> Z=1
    iny
    bne slibSmLoop
slibSmDone:
    rts

slibSmWild:
    lda #$ff
    sta kArg+4                  // hasStar = $ff (none)
    ldy #0
slibWmLoop:
    lda (srcPtr),y              // pattern char
    beq slibWmPatEnd
    cmp #'*'
    beq slibWmStar
    cmp #'?'
    beq slibWmQ
    cmp (kPtr),y                // literal vs name char
    bne slibWmBack
slibWmAdv:
    jsr slibIncK
    jsr slibIncSrc
    jmp slibWmLoop
slibWmQ:
    lda (kPtr),y                // '?' must consume one name char
    beq slibWmBack
    jmp slibWmAdv
slibWmStar:
    lda kPtr;   sta kArg+2      // starName = kPtr
    lda kPtr+1; sta kArg+6
    lda srcPtr
    clc
    adc #1
    sta kArg+3                  // starPat = srcPtr + 1
    lda srcPtr+1
    adc #0
    sta kArg+7
    lda #0
    sta kArg+4                  // hasStar = 0 (yes)
    jsr slibIncSrc              // advance pattern past '*'
    jmp slibWmLoop
slibWmPatEnd:
    lda (kPtr),y                // name must end too
    beq slibWmYes
slibWmBack:
    lda kArg+4
    bne slibWmFail              // no star -> fail
    lda kArg+2; sta kPtr        // kPtr = starName
    lda kArg+6; sta kPtr+1
    lda (kPtr),y                // name ended at starName -> can't extend
    beq slibWmFail
    jsr slibIncK                // kPtr = starName + 1
    lda kPtr;   sta kArg+2      // starName = kPtr (extended)
    lda kPtr+1; sta kArg+6
    lda kArg+3; sta srcPtr      // srcPtr = starPat
    lda kArg+7; sta srcPtr+1
    jmp slibWmLoop
slibWmFail:
    lda #1                      // Z=0
    rts
slibWmYes:
    lda #0                      // Z=1
    rts

// --- 16-bit pointer increments (X and Y preserved) ---
slibIncK:
    inc kPtr
    bne slibIncKDone
    inc kPtr+1
slibIncKDone:
    rts
slibIncSrc:
    inc srcPtr
    bne slibIncSDone
    inc srcPtr+1
slibIncSDone:
    rts

// --- skipSpaces -- advance kPtr past space characters ($20) ---
//  Input:  kPtr = string pointer
//  Output: kPtr advanced past leading spaces; A = first non-space char
//          C=1 if at null terminator, C=0 if non-space found
//  Clobbers: A, Y. Preserves: X
stringLibSkipSpaces:
    ldy #0
slibSkLoop:
    lda (kPtr),y
    beq slibSkEnd               // null
    cmp #$20
    bne slibSkFound             // non-space
    iny
    bne slibSkLoop
slibSkFound:
    tya
    clc
    adc kPtr                    // kPtr += Y
    sta kPtr
    bcc slibSkReload
    inc kPtr+1
slibSkReload:
    ldy #0
    lda (kPtr),y
    clc                         // C=0: non-space found
    rts
slibSkEnd:
    tya
    clc
    adc kPtr                    // kPtr += Y (to terminator)
    sta kPtr
    bcc slibSkEndR
    inc kPtr+1
slibSkEndR:
    lda #0
    sec                         // C=1: hit null
    rts

// --- byteToHex -- A -> 2 hex chars at (kPtr) (lib-local hexChars) ---
stringLibByteToHex:
    pha
    lsr; lsr; lsr; lsr          // high nibble
    tax
    lda slibHexChars,x
    ldy #0
    sta (kPtr),y
    pla
    and #$0f                    // low nibble
    tax
    lda slibHexChars,x
    iny
    sta (kPtr),y
    rts

// --- hexDigit -- screen-code hex digit -> nibble ---
//  Input:  A = screen code ('0'-'9'=$30-$39, 'a'-'f'=$01-$06)
//  Output: C=0 success, A = nibble (0-15); C=1 failure, A = 0
//  Preserves: X, Y
stringLibHexDigit:
    cmp #$30
    bcc slibHdChkLet            // < $30 -> might be a-f
    cmp #$3a
    bcc slibHdDig               // $30-$39 -> 0-9
    jmp slibHdFail              // > $39 and not a letter
slibHdChkLet:
    cmp #$01
    bcc slibHdFail              // < $01
    cmp #$07
    bcs slibHdFail              // $07+
    clc; adc #9                 // $01-$06 -> 10-15
    clc                         // C=0
    rts
slibHdDig:
    sec; sbc #$30               // $30 -> 0, $39 -> 9
    clc
    rts
slibHdFail:
    lda #0
    sec
    rts

// --- nibbleToHex -- nibble (0-15) -> screen-code hex char ---
//  Input:  A = nibble (upper nibble ignored)
//  Output: A = screen code ('0'-'9'=$30-$39, 'a'-'f'=$01-$06)
//  Preserves: X, Y
stringLibNibbleToHex:
    and #$0f
    cmp #10
    bcc slibNtDigit
    sec; sbc #9                 // 10 -> 1 ('a'), 15 -> 6 ('f')
    rts
slibNtDigit:
    clc; adc #$30               // 0 -> '0', 9 -> '9'
    rts

slibHexChars:
    .text "0123456789abcdef"
.label stringLibEnd = *
