// ==========================================
//  GordonOS -- REU DMA transfer routines
//  Located at $C800.
// ==========================================
.pc = $C800

// ==========================================
//  REU DMA -- Hardware-accelerated transfers
// ==========================================
//  All routines preserve the caller's interrupt
//  state via PHP/PLP. IRQs remain disabled for the
//  entire operation (register setup + DMA trigger +
//  completion polling). The REU competes with the
//  CPU for the bus -- reuWait polls $DF00 until the
//  transfer completes before returning.
//
//  Common parameter block (ToC64/FromC64):
//    kPtr/kPtr+1  = C64 address
//    kArg/kArg+1  = REU address (16-bit within bank)
//    kArg+2       = REU bank
//    kArg+3/kArg+4 = length (16-bit, $0000=64KB)
//
//  FillC64: kPtr/kPtr+1=C64 dest, A=fill byte, kArg/kArg+1=len
//  FillReu: kArg/kArg+1=REU addr, kArg+2=bank, kArg+3/kArg+4=len, A=fill byte

// --- reuWait -- poll $DF00 until end-of-block (bit 6) ---
//  Returns full status byte in A on completion.
//  Clobbers: A
reuWait:
    lda $df00                   // read status (clears EOB/FAULT on this read)
    pha                         // save full status before masking
    and #REU_ST_EOB
    beq reuWaitDiscard          // not done -- retry
    pla                         // done -- full status byte -> A
    rts
reuWaitDiscard:
    pla                         // discard stale saved status
    jmp reuWait

// --- reuSetup -- write C64 addr, REU addr/bank, length from arg block ---
//  Input:  kPtr = C64 addr, kArg..kArg+4 = REU(lo/hi/bank), len(lo/hi)
//  Output: $DF02-$DF08 set; $DF0A and $DF01 NOT set
//  Clobbers: A
reuSetup:
    lda kPtr; sta $df02         // C64 addr lo
    lda kPtr+1; sta $df03       // C64 addr hi
    lda kArg; sta $df04         // REU addr lo
    lda kArg+1; sta $df05       // REU addr hi
    lda kArg+2; sta $df06       // REU bank
    lda kArg+3; sta $df07       // length lo
    lda kArg+4; sta $df08       // length hi
    rts

// --- kReuToC64 -- DMA: REU -> C64 (increment mode) ---
//  Copies from REU to C64 memory.
//  Clobbers: A. X and Y preserved.
//  Modifies: $DF00-$DF0A
reuToC64:
    php
    sei
    jsr reuSetup
    lda #REU_CTRL_INC
    sta $df0a
    lda #REU_EXEC | REU_CMD_REU_TO_C64
    sta $df01
    jsr reuWait
    plp
    rts

// --- kReuFromC64 -- DMA: C64 -> REU (increment mode) ---
//  Copies from C64 memory to REU.
//  Clobbers: A. X and Y preserved.
//  Modifies: $DF00-$DF0A
reuFromC64:
    php
    sei
    jsr reuSetup
    lda #REU_CTRL_INC
    sta $df0a
    lda #REU_EXEC | REU_CMD_C64_TO_REU
    sta $df01
    jsr reuWait
    plp
    rts

// --- kReuFillC64 -- fill C64 memory with a byte (fixed REU addr) ---
//  Input:  kPtr/kPtr+1 = C64 dest, A = fill byte, kArg/kArg+1 = length
//  Stores fill byte in REU bounce buffer, then DMAs REU->C64 with
//  fixed REU address so the same byte is written repeatedly.
//  Clobbers: A, Y
//  Modifies: $DF00-$DF0A
reuFillC64:
    php
    sei
    sta kArg+2                  // save fill byte
    // Step 1: copy fill byte to REU bounce buffer
    lda kArg+2; sta kArg+3      // fill byte -> kArg+3 (C64 temp)
    lda #<kArg+3; sta $df02
    lda #>kArg+3; sta $df03
    lda #<REU_BOUNCE; sta $df04
    lda #>REU_BOUNCE; sta $df05
    lda #0; sta $df06            // bank 0
    lda #1; sta $df07
    lda #0; sta $df08            // length = 1
    lda #REU_CTRL_INC
    sta $df0a
    lda #REU_EXEC | REU_CMD_C64_TO_REU
    sta $df01
    jsr reuWait
    // Step 2: DMA REU -> C64 with fixed REU address
    lda kPtr; sta $df02
    lda kPtr+1; sta $df03
    lda #<REU_BOUNCE; sta $df04
    lda #>REU_BOUNCE; sta $df05
    lda #0; sta $df06            // bank 0
    lda kArg; sta $df07          // length lo
    lda kArg+1; sta $df08        // length hi
    lda #REU_CTRL_FIX_REU
    sta $df0a
    lda #REU_EXEC | REU_CMD_REU_TO_C64
    sta $df01
    jsr reuWait
    plp
    rts

// --- kReuFillReu -- fill REU memory with a byte (fixed C64 addr) ---
//  Input:  kArg/kArg+1 = REU addr, kArg+2 = bank,
//          kArg+3/kArg+4 = length, A = fill byte
//  Stores fill byte in kernel temp, then DMAs C64->REU with
//  fixed C64 address so the same byte is written repeatedly.
//  Clobbers: A
//  Modifies: $DF00-$DF0A
reuFillReu:
    php
    sei
    sta kArg+5                  // save fill byte at kArg+5 (temp)
    lda #<kArg+5; sta $df02
    lda #>kArg+5; sta $df03
    lda kArg; sta $df04         // REU addr lo
    lda kArg+1; sta $df05       // REU addr hi
    lda kArg+2; sta $df06       // REU bank
    lda kArg+3; sta $df07       // length lo
    lda kArg+4; sta $df08       // length hi
    lda #REU_CTRL_FIX_C64
    sta $df0a
    lda #REU_EXEC | REU_CMD_C64_TO_REU
    sta $df01
    jsr reuWait
    plp
    rts

// --- reuDirPageDma -- DMA one 256-byte page between directory and bounce buffer ---
//  Input:  A = page index (0..63), C=0 -> REU->C64 (read), C=1 -> C64->REU (write)
//  Output: reuDirBuf populated (read) or committed (write)
//  Clobbers: A
//  REU address = reuFsDirBase + page x 256. The directory is 512 entries
//  x 32B = 16KB = 64 pages; callers loop the page counter 0..63 and derive
//  the page from the entry index as (entryHi << 5) | (entryLo >> 3).
//  Caller must SEI (all FS kernel calls wrap in SEI/CLI)
reuDirPageDma:
    php                         // save C flag (read/write direction) FIRST
    pha                         // save page index
    // C64 addr always = reuDirBuf
    lda #<REU_DIR_BUF; sta $df02
    lda #>REU_DIR_BUF; sta $df03
    // REU addr = reuFsDirBaseLo/Hi + page (page x 256 always has lo=0)
    pla                         // restore page index
    clc
    adc reuFsDirBaseHi          // page + base hi -> REU addr hi
    sta $df05
    lda reuFsDirBaseLo          // base lo -> REU addr lo (always 0 for $059000)
    sta $df04
    lda reuFsDirBaseBank; sta $df06
    lda #<REU_DIR_PAGE_SIZE; sta $df07
    lda #>REU_DIR_PAGE_SIZE; sta $df08
    lda #0; sta $df0a
    plp                         // restore C flag
    bcs rdpWrite
    lda #$91; sta $df01         // REU->C64
    jmp rdpWait
rdpWrite:
    lda #$90; sta $df01         // C64->REU
rdpWait:
    lda $df00; and #$40; beq rdpWait
    rts
