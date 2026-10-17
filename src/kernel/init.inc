// ==========================================
//  GordonOS -- Boot & TCB initialisation
// ==========================================
//
//  Boot: banks KERNAL, sets vectors, starts CIA
//  timer, enters shell task via RTI.
//
//  initTCB     -- build TCB from taskLo/Hi[X]
//  initTCB_ptr -- build TCB from spawnPtr
//  nmiNull     -- NMI handler

// ==========================================
//  Boot entry ($0810)
// ==========================================
start:
    sei
    cld                         // clear decimal flag (BCD off)

    // --- Memory configuration ---
    // $01 bits: 0=LORAM  1=HIRAM  2=CHAREN
    // $35 = %00110101:
    //   LORAM=1  -> BASIC ROM at $A000 (unused)
    //   HIRAM=0  -> RAM at $E000-$FFFF (our vectors!)
    //   CHAREN=1 -> I/O at $D000-$DFFF (VIC-II, CIA, SID)
    lda #$35
    sta $01

    // --- Hardware vectors (now writable RAM) ---
    // NMI: just RTI -- prevents crash from RESTORE key
    lda #<nmiNull
    sta $fffa
    lda #>nmiNull
    sta $fffb

    // IRQ: our handler
    lda #<irqHandler
    sta $fffe
    lda #>irqHandler
    sta $ffff

    // --- Initialise all TCBs ---
    // Each TCB gets a fake interrupt frame so that when
    // the scheduler first switches to a task, it appears
    // to have been "interrupted" and can be resumed via RTI.
    // X = task index, passed to initTCB below.
    lda #1
    sta totalTasks              // one compile-time task (boot)
    // Reset data heap for warm reboot (starts after REU FS code)
    lda #<$1D40; sta dataHeap
    lda #>$1D40; sta dataHeap+1
    lda #<$2000; sta dataHeapEnd
    lda #>$2000; sta dataHeapEnd+1
    ldx #0
    jsr initTCB
    lda #1
    sta taskState               // mark slot 0 ALIVE

    // --- Warm-reboot reset: all-bitmap surface/gfx state ---
    //  (kernel DATA survives `jmp start` -- without this, leaked slot
    //  bits, stale taskGfxReu*, and stale focus/cursor state pile up
    //  across reboots until kAllocScreen fails and the shell halts
    //  silently on a blank screen.)
    lda #$ff; sta kbFocus
    lda #0
    sta cursorVisible
    sta gfxSlotFirstFree
    sta gfxSlotFreeMap
    sta gfxSlotFreeMap+1
    sta gfxSlotFreeMap+2
    sta gfxSlotFreeMap+3
    sta dynTaskCount
    ldx #0
pcbClr:
    sta poolBitmap,x
    inx
    cpx #POOL_BITMAP_BYTES
    bne pcbClr
    sta resCount
    lda #BLINK_RATE
    sta blinkRate               // global blink rate (default)
    sta blinkTick               // initial countdown (A still = BLINK_RATE)
    ldx #0
surfRstLp:
    lda #0
    sta screenAlloc,x
    sta screenOwner,x
    sta screenKind,x
    sta taskGfxReuLo,x
    sta taskGfxReuHi,x
    sta taskGfxReuBank,x
    sta taskTextColor,x
    sta taskCursorColor,x
    // clear the orphan free-list too -- dataHeap resets to $1D40 here,
    // so stale entries from before a warm reboot could point at ranges
    // the fresh bump allocator hands out again (double-own)
    cpx #FREE_LIST_ENTRIES
    bcs srNoFreeClr
    sta freeListSize,x          // size 0 = empty; stale ptrs never read
srNoFreeClr:
    inx
    cpx #MAX_TASKS
    bcc surfRstLp

    // Warm-reboot reset of the dynamic-task + malloc tables and task
    // slots 1.. (slot 0 = boot stays ALIVE). Lives at $FE34 -- the kernel
    // core block ($0810-$131B) is full.
    jsr dynTableReset

    // --- Set task address and name for boot ---
    lda #<boot
    sta taskLo+0
    lda #>boot
    sta taskHi+0
    lda #<nameBoot
    sta taskNameLo+0
    lda #>nameBoot
    sta taskNameHi+0            // slot 0 = "boot"

    // --- Font record ---
    //  REU-only font: the boot task resolves gordon.fnt into fontRec*
    //  before any task can print (main.asm zero-initializes it). This
    //  block's size is what keeps downstream kernel-data addresses pinned.
    ldx #$02
fontInitLp:
    lda #$00
    sta fontRecLo,x
    dex
    bpl fontInitLp
    .fill 4, $EA                // size-match pad -- MUST be NOPs: the loop
                                // falls through here (a 0 pad = 4 BRKs)

    // REU detection: write test values to addr regs, read back.
    // No REU -> open bus reads $FF. REU present -> values persist.
    lda #$12
    sta $df02
    lda #$34
    sta $df03
    lda #$56
    sta $df04
    lda #$78
    sta $df05
    lda $df02; cmp #$12; beq reuCheck2
    jmp noReu
reuCheck2:
    lda $df03; cmp #$34; beq reuCheck3
    jmp noReu
reuCheck3:
    lda $df04; cmp #$56; beq reuCheck4
    jmp noReu
reuCheck4:
    lda $df05; cmp #$78; beq reuFound
    jmp noReu
reuFound:

    // REU found -- initialise per-task stack REU addresses
    lda #<REU_STACK_BASE
    sta kArg
    lda #>REU_STACK_BASE
    sta kArg+1
    ldx #0
reuStackLp:
    lda kArg; sta taskStackReuLo,x
    lda kArg+1; sta taskStackReuHi,x
    lda kArg+1; clc; adc #1    // hi byte += 1 (lo always 0, never carries)
    sta kArg+1
    inx
    cpx #MAX_TASKS
    bcc reuStackLp

    // Stack frames are now DMA'd directly to REU by initTCB_ptr -- no separate copy step needed.

    // Thread name REU addresses are set per-task by sysThreadSpawn -- no boot init needed.

    // Initialise per-task ZP shadow REU addresses (REU_ZP_BASE + task x MAX_ZP_LEN)
    lda #<REU_ZP_BASE
    sta kArg
    lda #>REU_ZP_BASE
    sta kArg+1
    ldx #0
reuZpInitLp:
    lda kArg; sta taskZpReuLo,x
    lda kArg+1; sta taskZpReuHi,x
    lda kArg; clc; adc #MAX_ZP_LEN; sta kArg
    lda kArg+1; adc #$00; sta kArg+1
    inx
    cpx #MAX_TASKS
    bcc reuZpInitLp

    //  ZERO EVERY PER-TASK SPRITE BLOCK. The blocks live in the REU and VICE
    //  writes the REU image back, so a cold boot can start with the last
    //  session's state still in them -- and the BOOT task (slot 0) is the one
    //  slot that never goes through the spawn path (`clearSlotSprite`), so it
    //  would install a previous session's sprites, art and all. The other slots
    //  are cleared as they are spawned; this closes slot 0 (and any residual
    //  block from an image written by an older build).
    jsr warmSpriteReset

    // --- REU size detection & FS init (code in reu-fs.inc) ---
    // reuDetectSize skipped for now: 16MB REU default is correct,
    // and the probe writes to $FFF0 in all banks which can
    // corrupt REU data on warm reboot.
    jsr reuFsInitBoot           // compute FS base/data/capacity/enabled

    jmp reuReady

noReu:
    // REU not found -- clear screen, print message, halt
    jsr kClearScreen
    lda #<msgNoReu; sta kPtr
    lda #>msgNoReu; sta kPtr+1
    jsr kPrintString
    // fall into the halt loop below
haltNoReu:
    jmp haltNoReu

.encoding "screencode_mixed"
msgNoReu:
    .text "reu required"
    .byte 0

reuReady:

    // --- Initialise unified surface state ---
    lda #$00; sta screenBase
    lda #$20; sta screenBase+1   // screenBase = framebuffer $2000
    lda #0; sta cursorCol        // cursorState (col)
    sta cursorRow                // cursorState+1 (row)
    lda #1; sta cursorOn
    sta dispIsBitmap             // VIC is always in bitmap mode now
    lda #DEFAULT_BORDER_COLOR; sta $d020
    lda #DEFAULT_BG_COLOR; sta $d021
    // VIC: hires bitmap mode (bitmap on, multicolor off, bitmap $2000)
    lda $d011; ora #$20; sta $d011
    lda $d016; and #$ef; sta $d016
    lda #GFX_D018; sta $d018

    // --- Initialise semaphore pool ---
    lda #1; sta semCount        // semaphore 0 (screen) = available

    // --- Boot screen: blank the visible display directly (boot has no
    //     surface -- kClearScreen would take the background path and fill
    //     slot 0's nonexistent REU surface) ---
    lda #<$2000; sta kPtr
    lda #>$2000; sta kPtr+1
    lda #<$2000; sta kArg
    lda #>$2000; sta kArg+1
    lda #0
    jsr kReuFillC64             // pixels = 0
    lda #GFX_MATRIX_BASE_LO; sta kPtr
    lda #GFX_MATRIX_BASE_HI; sta kPtr+1
    lda #<$0400; sta kArg
    lda #>$0400; sta kArg+1
    lda #DEFAULT_BG_COLOR
    jsr kReuFillC64             // matrix = bg0
    lda #<$d800; sta kPtr
    lda #>$d800; sta kPtr+1
    lda #<$0400; sta kArg
    lda #>$0400; sta kArg+1
    lda #DEFAULT_BG_COLOR
    jsr kReuFillC64             // color RAM = bg color

    lda #0
    sta curTask                 // start with task 0

    // --- CIA #1 Timer A: scheduler heartbeat ---
    // The CIA 6526 has two 16-bit timers. Timer A counts
    // down from the latch value at system clock speed.
    // When it underflows (reaches 0), it reloads and can
    // trigger an IRQ. We use continuous mode.
    //
    // $DC0E = %00010001 = $11:
    //   bit 0 = 1 -> start timer
    //   bit 3 = 0 -> continuous (auto-reload)
    //   bit 4 = 1 -> force load latch->counter now
    //   bit 5 = 0 -> count PHI22 system clock (~1 MHz)
    lda #TIMER_LO
    sta $dc04                   // write latch low byte
    lda #TIMER_HI
    sta $dc05                   // write latch high, transfer to counter
    lda #$11
    sta $dc0e                   // start in continuous mode
    lda #$00
    sta $dc0f                   // CRB: clear bit 7 -> TOD read mode

    // --- Interrupt control ---
    // Disable CIA #2 IRQs (not used)
    lda #$7f
    sta $dd0d
    lda $dd0d                   // read to ack any pending

    // Enable CIA #1 Timer A IRQ
    // $DC0D: bit 7=1 (set mask), bit 0=1 (Timer A underflow)
    lda #$81
    sta $dc0d

    // --- Configure CIA #1 ports for keyboard ---
    // DDRA ($DC02): $FF = PA0-7 outputs (keyboard column select)
    // DDRB ($DC03): $00 = PB0-7 inputs  (keyboard row read)
    lda #$ff
    sta $dc02
    lda #$00
    sta $dc03

    // --- Sprite raster interrupt ---
    //  rasterCommit publishes the sprite shadow to $D000-$D02E plus the
    //  $07F8-$07FF data pointers, once per frame, in the vertical blank.
    //  Line 0 is in vblank on PAL AND NTSC and precedes the earliest line a
    //  sprite can be visible (PAL Y=31, NTSC ~Y=20), so a task's update is
    //  never half-applied.
    //  Enabled HERE, at the end of init, rather than with the VIC mode setup
    //  above: a raster interrupt must not be able to fire while init is still
    //  running. WARNING: From now on $D012 is KERNEL-OWNED -- writing it moves this
    //  interrupt, and $D011 bit 7 must stay 0 for line 0 to mean line 0.
    lda #0
    sta $d012                   // raster compare = line 0
    lda #$01
    sta $d01a                   // enable the RST (raster) interrupt only

    // --- Enter boot task ---
    ldx #$ff
    txs
    lda #>boot
    pha
    lda #<boot
    pha
    lda #$00
    pha
    cli
    rti

// ==========================================
//  initTCB -- Build initial stack frame for task X
//  X = task index (preserved).
//  Reads taskLo/Hi[x] into spawnPtr, then falls
//  through to initTCB_ptr.
//  NOTE: RTS adds 1 to the popped address before
//  jumping, so we store irqRet-1.
// ==========================================
initTCB:
    lda taskLo,x; sta spawnPtr
    lda taskHi,x; sta spawnPtr+1
    // fall through to initTCB_ptr

// ==========================================
//  initTCB_ptr -- build initial stack frame in REU
//  X = task index (preserved).
//  Builds the 8-byte fake interrupt frame in frameBuf,
//  DMAs it to REU at REU_STACK_BASE + x*256 + $F8.
//  Only savedSP[x] is kept in C64 RAM.
//  Does NOT touch the 6502 stack -- safe to call with
//  any SP depth.
// ==========================================
initTCB_ptr:
    stx initTaskX               // save task index (X)

    lda #$f7
    sta savedSP,x               // savedSP[x] = $F7

    // Build 8-byte frame in frameBuf (kernel data, SEI-safe)
    ldy #0
    lda #<[irqRet - 1]; sta frameBuf,y
    iny; lda #>[irqRet - 1]; sta frameBuf,y
    iny; lda #$00; sta frameBuf,y   // Y=0
    iny; sta frameBuf,y             // X=0
    iny; sta frameBuf,y             // A=0
    iny; sta frameBuf,y             // SR=0
    iny
    lda spawnPtr; sta frameBuf,y    // PCL
    iny; lda spawnPtr+1; sta frameBuf,y  // PCH

    // DMA the 8-byte frame: frameBuf -> REU_STACK_BASE + x*256 + $F8
    ldx initTaskX               // restore task index
    lda #<frameBuf; sta $df02   // C64 addr lo
    lda #>frameBuf; sta $df03   // C64 addr hi
    lda taskStackReuLo,x
    clc; adc #$F8; sta $df04    // REU addr lo = base + $F8
    lda taskStackReuHi,x
    adc #0; sta $df05           // REU addr hi
    lda #0; sta $df06           // bank 0
    lda #8; sta $df07           // length lo = 8
    lda #0; sta $df08           // length hi = 0
    lda #0; sta $df0a           // increment mode
    lda #$90; sta $df01         // C64->REU, immediate
ipWaitDma:
    lda $df00; and #$40; beq ipWaitDma

    ldx initTaskX               // restore task index for final writes
    lda #1; sta taskPriority,x
    sta taskSliceLeft,x
    rts

// ==========================================
//  NMI handler -- immediately return
//  Prevents crash if RESTORE key is pressed or
//  CIA #2 generates an NMI.
// ==========================================
nmiNull:
    rti

// ==========================================
//  Boot screen strings
// ==========================================
.encoding "screencode_mixed"
osName:
    .text "gordonOS"
    .byte 0
osVersion:
    .text VERSION
    .byte 0

.encoding "screencode_mixed"
nameBoot:
    .text "boot"
    .byte 0
