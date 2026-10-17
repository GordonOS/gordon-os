// ==========================================
//  time.asm -- dynamic library "time.lib" (lib 0, 3 entries)
// ==========================================
//  getTime/setTime/printTime -- CIA #1 Time-of-Day clock (12-hour AM/PM).
//  Entry-table order = call-index order: getTime(0), setTime(1),
//  printTime(2). printTime uses a LIB-LOCAL buffer (not the kernel
//  timeBuf) so it has no fixed-address dependency, and calls the resident
//  kByteToHex/kBlitChar jump-table entries by their fixed addresses.
//  printTime blits "HH:MM:SS AM/PM" at (X=col, Y=row) with NO cursor
//  advance and NO char-shadow mirror -- fixed-position output.

.encoding "screencode_mixed"

timeLibBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word timeLibEnd - timeLibCode // +5: code size
    .byte 0, 0, 0                // +7:  data size, ZP base, ZP len
    .word 0                      // +10: reserved (libs consume no libMask)
    .byte timeLibNameEnd - timeLibName  // +12: name len
timeLibName:
    .text "time"
timeLibNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - timeLibBinStart), 0

timeLibCode:
    .word timeLibGet, timeLibSet, timeLibPrint  // entry table (3 entries)

timeLibGet:
    lda $dc0b                   // hours -> latches all TOD registers
    sta kArg+3
    lda $dc0a
    sta kArg+2
    lda $dc09
    sta kArg+1
    lda $dc08
    sta kArg
    rts

timeLibSet:
    lda kArg+3                  // hours (bit 7 = AM/PM)
    sta $dc0b                   // stop clock, load hours
    lda kArg+2
    sta $dc0a                   // minutes
    lda kArg+1
    sta $dc09                   // seconds
    lda kArg
    sta $dc08                   // 1/10 sec
    lda $dc08                   // read to restart TOD clock
    rts

timeLibPrint:
    php; sei                    // atomic: prevent preemption during build
    stx timeLibCol              // blit column (input X)
    sty timeLibRow              // blit row (input Y)
    jsr timeLibGet              // lib-internal call (relocated via fixup)
    lda kArg+3                  // full hours (bit 7 = PM flag)
    pha                         // save for AM/PM check later
    and #$7f                    // mask PM flag for byteToHex
    sta kArg+3                  // store masked hours
    lda #<timeLibBuf; sta kPtr; lda #>timeLibBuf; sta kPtr+1
    lda kArg+3; jsr kByteToHex  // "12" -> timeLibBuf+0
    lda #':'; sta timeLibBuf+2
    lda #<(timeLibBuf+3); sta kPtr; lda #>(timeLibBuf+3); sta kPtr+1
    lda kArg+2; jsr kByteToHex  // "34" -> timeLibBuf+3
    lda #':'; sta timeLibBuf+5
    lda #<(timeLibBuf+6); sta kPtr; lda #>(timeLibBuf+6); sta kPtr+1
    lda kArg+1; jsr kByteToHex  // "56" -> timeLibBuf+6
    // append " AM" or " PM"
    pla                         // original hours (bit 7 = PM flag)
    bpl timeLibPm               // bit 7 clear -> PM (VICE CIA convention)
    lda #'A'
    bne timeLibStore            // always taken
timeLibPm:
    lda #'P'
timeLibStore:
    sta timeLibBuf+9
    lda #' '
    sta timeLibBuf+8
    lda #'M'
    sta timeLibBuf+10
    lda #0
    sta timeLibBuf+11
    // blit "HH:MM:SS AM/PM" at (col+i, row) -- kBlitChar has no cursor
    // advance and no char-shadow mirror, so the clock never moves the
    // shell's cursor nor pollutes its shadow.
    lda #0
    sta timeLibIdx
timeLibBlit:
    ldy timeLibIdx
    lda timeLibBuf,y            // char (screen code; NUL at index 11)
    beq timeLibDone
    pha                         // save char (kBlitChar clobbers A,X,Y)
    tya                         // i
    clc
    adc timeLibCol              // col + i
    tax                         // X = col + i
    lda timeLibRow              // row
    tay                         // Y = row
    pla                         // A = char
    jsr kBlitChar               // blit at (X,Y)
    inc timeLibIdx
    lda timeLibIdx
    cmp #11
    bcc timeLibBlit
timeLibDone:
    plp                         // restore original interrupt state
    rts

timeLibIdx:
    .byte 0
timeLibCol:
    .byte 0
timeLibRow:
    .byte 0
timeLibBuf:
    .fill 12, 0
.label timeLibEnd = *
