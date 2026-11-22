// ==========================================
//  ipc.asm -- dynamic library "ipc.lib" (lib 6, 2 entries, lazy)
// ==========================================
//  Inter-process communication that costs the kernel nothing: four independent
//  single-producer / single-consumer byte channels that live in THIS LIBRARY'S
//  OWN DATA. A library's `.byte` variables are one copy shared by every task
//  that declares the lib (that is what makes `sidOwner` in sid.lib work), and
//  the library is refcounted, so a ring cannot be freed while a participant
//  lives. The bytes therefore live in the pool, not in the kernel image.
//
//      kIpcPost   call 35 -- X = channel 0..3, A = byte -> push it (yields while full)
//                          X = $80+c, A = length, kPtr = source -> a whole frame
//      kIpcPick   call 36 -- X = channel 0..3 -> A = next byte, C=1 if empty
//                          X = $80+c, kPtr = dest -> A = length, C=1 if empty
//
//  WARNING: NO LOCK, AND NONE IS NEEDED. One writer owns `ipcHead`, one reader owns
//  `ipcTail`, each of them a single byte, so every update is one `sta` and every
//  read is one `lda` -- atomic on a single-CPU 6502, preemptive or not. The ring
//  holds 127 bytes (one slot is left empty so full and empty are distinct).
//
//  WARNING: BOTH INDICES NAME THE SLOT THEY WILL USE NEXT. `ipcHead` is the slot the
//  producer writes next and `ipcTail` the slot the consumer reads next, so
//  `head == tail` is empty and `(head+1) & MASK == tail` is full. The two MUST
//  agree: a producer that wrote `head+1` would leave the reader one slot behind
//  and its first `kIpcPick` would hand back slot 0, which was never written -- a
//  phantom byte that shifts the whole stream by one (the proof read
//  `00 00 01 02...`, one match in 256, with only one producer alive).
//
//  WARNING: ONE PRODUCER AND ONE CONSUMER, BY DESIGN. Two tasks posting would both
//  write that channel's `ipcHead` and corrupt its ring; use another channel
//  (there are four) rather than sharing one. `kIpcPost` is the *producer's*
//  call and `kIpcPick` the consumer's: they are not interchangeable, and neither
//  is re-entrant.
//
//  WARNING: THE PIPE IS EMPTY AT LOAD. `ipcHead`/`ipcTail` are initialised by the
//  binary, so a libLoad after a libFree starts four fresh pipes -- a purge on
//  last close, which is the right default. Buffered bytes do not survive that.
//
//  WARNING: NO CALLER ZP, BUT `kPtr` IS CLOBBERED. The library declares no ZP window;
//  it borrows the kernel's `kPtr`/`kPtr+1` as its ring pointer, and those are
//  saved and restored per task by `switchTask`. That choice is deliberate, not
//  merely convenient: a caller-supplied ZP pointer would need declaring, and a
//  patched `sta abs,y` operand would be *shared* -- two posters preempting each
//  other inside `kIpcPost` would clobber one another's channel. Per-task `kPtr`
//  has no such race.
//
//  THE BLOCK HALF. The same two entries also carry whole FRAMES, for payloads a
//  byte ring is the wrong shape for. X = $80 + channel selects a block channel;
//  the byte channels are 0..3, so the two ranges cannot collide, and $84 and up
//  is refused exactly as X = 4 is:
//
//      kIpcPost  X = $80+c, A = length (1..255), kPtr = the source
//                -> C=1 if X is out of range; blocks while the ring is full
//      kIpcPick  X = $80+c, kPtr = the destination
//                -> C=0, A = the length; C=1 when that channel is empty
//
//  A frame is one 256-byte page of bank 250 (`REU_IPC_BASE`), so the frame index
//  IS the address: lo 0, hi = frame, bank = the IPC bank. One DMA per message
//  instead of one `sta` per byte -- 255 bytes in a fraction of a millisecond,
//  against ~86 cycles a byte through the ring -- which is the whole reason for a
//  second path. There is no third kernel entry because `LIB_CALLS` is at its
//  ceiling (main.asm): the channel code is the sub-op, the way `kMouseCall` and
//  `kFpCall` select theirs.
//
//  WARNING: THE LENGTH LIVES IN `ipcBlkLen`, INDEXED BY FRAME -- not by channel, and
//  not in the frame itself. One writer (that frame's producer) and one reader (its
//  consumer), which is the same single-owner argument that makes `ipcHead` and
//  `ipcTail` lock-free. It is written BEFORE the payload and published by the head
//  byte after it, so a consumer can never read a frame's length and payload from
//  different messages.
//
//  WARNING: 63 OF THE 64 FRAMES PER CHANNEL ARE USABLE. The ring leaves one frame
//  empty so that "full" and "empty" stay distinct, exactly as the byte rings do.
// ==========================================

.encoding "screencode_mixed"

// (kArg/kPtr/SYS_* come from kernel-data.inc + jumptable.inc, imported globally
//  by assemble-tasks.asm -- do not re-declare them here.)

ipcBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE        // +2:  flags
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word ipcEnd - ipcCode       // +5:  code size
    .byte 0, 0, 0                // +7:  data size, ZP base, ZP len (none)
    .word 0                      // +10: reserved (libs consume no libMask)
    .byte ipcNameEnd - ipcName   // +12: name len
ipcName:
    .text "ipc"
ipcNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - ipcBinStart), 0

ipcCode:
    // Entry table, in call-index order: base 35, so index 35 = ipcPost and
    // 36 = ipcPick.
    .word ipcPost, ipcPick

//  The ring geometry. 128 bytes per channel: one slot is left empty so that
//  "full" and "empty" are different states, and a power of two lets the index
//  wrap with a mask instead of a modulo. Tunable -- the code only assumes a
//  power of two in IPC_MASK.
.const IPC_SIZE = 128
.const IPC_MASK = IPC_SIZE - 1

//  Four independent channels. With a single ring, any two tasks wanting to talk
//  had to share it; the source itself noted that "a second channel is a second
//  ring". The ring is indexed by CHANNEL, and a channel is four rings of the
//  same geometry.
.const IPC_CHANNELS = 4

//  The block channels: X = IPC_BLK_CODE + c selects channel c of the REU frames.
//  Four of them, 64 frames each, filling bank 250 exactly (256 pages), and a
//  power of two so both the channel base (c x 64) and the ring wrap (& 63) are
//  arithmetic rather than a table.
.const IPC_BLK_CODE     = $80
.const IPC_BLK_CHANNELS = 4
.const IPC_BLK_MASK_CH  = IPC_BLK_CHANNELS - 1
.const IPC_BLK_FRAMES   = 64
.const IPC_BLK_MASK     = IPC_BLK_FRAMES - 1
.const IPC_BLK_USED     = IPC_BLK_CHANNELS * IPC_BLK_FRAMES   // = the bank's 256 frames
.const IPC_BLK_BANK     = REU_IPC_BASE / $10000               // the bank they live in

// ==========================================
//  ipcPost -- kIpcPost: append byte A to the pipe
// ==========================================
//  Input:  X = channel (0..IPC_CHANNELS-1), A = the byte
//  Output: C = 0 stored. C = 1 = channel out of range, nothing stored.
//          (While the ring is full it blocks instead of failing.)
//  Clobbers: A, X, Y, kPtr/kPtr+1, kArg+1 (the sleep's count)
//  Yields with SYS_SLEEP while full, so a slow consumer applies backpressure
//  without burning the CPU -- and the scheduler switches tasks with the ring
//  half-written, which is exactly what the single-writer rule makes safe.
//
//  WARNING: SYS_SLEEP RETURNS WITH X = curTask, SO THE CHANNEL IS PARKED ON THE STACK.
//  `sysSleep` opens with `ldx curTask`, and the sleep is not optional here, so X
//  cannot be assumed to survive the wait. Both the byte and the channel go on the
//  task's OWN stack (`pha` then `txa; pha`), which is the one place a preemptive
//  switch cannot reach -- `switchTask` saves and restores SP per task, so the pair
//  is part of the suspended image. The channel is therefore re-read from `$0102,x`
//  after the wait (one below the top: the byte is pushed on top of it so the
//  store's `pla` recovers it without touching X at all).
//  Re-deriving the channel matters for more than the wait itself: with a stale
//  `X = curTask`, `ipcHead,x`/`ipcTail,x` would address a NEIGHBOURING channel's
//  indices -- a task pair on channel 0 would freeze and silently scribble channel
//  3's head.
ipcPost:
    cpx #IPC_CHANNELS
    bcc ipcPostByte              // 0-3: the byte pipe
    cpx #IPC_BLK_CODE            // below $80 there is nothing...
    bcc ipcPostBad
    cpx #IPC_BLK_CODE+IPC_BLK_CHANNELS
    bcc ipcBlkSend               // ...$80-$83 are the block frames
ipcPostBad:
    sec                          // C=1, nothing stored -- and NO `pla`, because the
    rts                          //   range check ran before anything was parked
ipcPostByte:
    tay                          // park the byte in Y for a moment...
    txa
    pha                          // ...park the CHANNEL (lower on the stack)...
    tya
    pha                          // ...then park the byte on top of it
    lda ipcRingLo,x              // kPtr = this channel's ring. kPtr and not a
    sta kPtr                     //   patched store operand: kPtr is per-task
    lda ipcRingHi,x              //   (switchTask saves/restores it), so two
    sta kPtr+1                   //   tasks preempting each other here cannot
                                 //   collide on a shared operand -- which a
                                 //   self-modified `sta abs,y` would.
ipcPostFull:
    ldy ipcHead,x                // head is the slot this push will fill, and it
    tya                          //   stays in Y for the store below
    clc
    adc #1
    and #IPC_MASK                // the slot AFTER head: the one that stays free
    cmp ipcTail,x
    beq ipcPostWait              // no room: head+1 == tail
    pla                          // the parked byte -- X untouched, so...
    sta (kPtr),y                 // ...write at HEAD (a +1 here would leave the
                                 //   reader a slot behind and hand it the
                                 //   never-written slot 0)
    iny                          // head+1 -- head is always 0..127, so no overflow
    tya
    and #IPC_MASK
    sta ipcHead,x                // ...then the reader may see it
    pla                          // drop the parked channel
    clc
    rts
ipcPostWait:
    lda #1                       // one tick: a single-tick sleep still switches
    sta kArg+1
    lda #SYS_SLEEP
    jsr syscall
    tsx                          // X is curTask now -- take the channel back off
    lda $0102,x                  //   our own stack ($0102,x = one below the top,
    tax                          //   because the byte sits above it)
    jmp ipcPostFull

// ==========================================
//  ipcBlkSend -- the block half of kIpcPost: X = $80+channel, A = length (1..255),
//  kPtr = the source. One DMA into the ring's next frame.
// ==========================================
//  The channel and the length are parked on the task's own stack while the full
//  test waits, for the reason ipcPost's wait does the same -- SYS_SLEEP comes
//  back with X = curTask. The frame's length goes into `ipcBlkLen` BEFORE the
//  payload and the head byte is published after it, so a consumer never pairs a
//  length with another message's bytes.
//  Clobbers A, X, Y, kPtr (read only), kArg..kArg+4.
ipcBlkSend:
    cmp #0                       // a length of 0 is not "nothing to send": the DMA
    beq ipcPostBad               //   counts down to zero, so it would move 65536
                                 //   bytes and run over the whole bank
    pha                          // park the length...
    txa
    pha                          // ...and the channel code on top of it
iblkSRetry:
    tsx
    lda $0101,x                  // the parked channel code
    and #IPC_BLK_MASK_CH
    tay                          // Y = the block channel
    lda ipcBlkHead,y             // A = the slot this frame will fill
    sta kArg+1                   // the frame index starts at the slot
    clc
    adc #1
    and #IPC_BLK_MASK            // the slot AFTER it: the one that stays free
    cmp ipcBlkTail,y
    beq iblkSWait                // no room: head+1 == tail
    tya
    asl
    asl
    asl
    asl
    asl
    asl                          // channel x 64 = its first frame
    clc
    adc kArg+1
    sta kArg+1                   // = the frame index, which IS the address's hi byte
    tsx
    lda $0102,x                  // the parked length
    sta kArg+3                   // the transfer length...
    ldx kArg+1                   // ...and the frame this length belongs to
    sta ipcBlkLen,x
    lda #0
    sta kArg+4                   // length hi
    sta kArg                     // REU lo = 0: the frame's first byte
    lda #IPC_BLK_BANK
    sta kArg+2
    jsr kReuFromC64              // (kPtr) -> the frame; X and Y survive
    lda ipcBlkHead,y             // the slot again -- nothing moved it
    clc
    adc #1
    and #IPC_BLK_MASK
    sta ipcBlkHead,y             // ...then the consumer may see it
    pla                          // the parked channel code
    pla                          // the parked length
    clc
    rts
iblkSWait:
    lda #1
    sta kArg+1
    lda #SYS_SLEEP
    jsr syscall
    jmp iblkSRetry

// ==========================================
//  ipcPick -- kIpcPick: take the oldest byte from the pipe
// ==========================================
//  Input:  X = channel (0..IPC_CHANNELS-1)
//  Output: C = 0, A = the byte. C = 1 = that channel is empty, or X is out of
//          range -- in both cases nothing was consumed.
//  Clobbers: A, X, Y, kPtr/kPtr+1
//  Non-blocking on purpose: a producer that dies must not leave the consumer
//  waiting forever. A consumer that wants to wait calls this in a loop with
//  SYS_SLEEP, which yields.
ipcPick:
    cpx #IPC_CHANNELS
    bcc ipcPickByte              // 0-3: the byte pipe
    cpx #IPC_BLK_CODE            // below $80 there is nothing...
    bcc ipcPickEmpty
    cpx #IPC_BLK_CODE+IPC_BLK_CHANNELS
    bcc ipcBlkPick               // ...$80-$83 are the block frames
ipcPickEmpty:
    sec                          // out of range or empty: nothing was consumed
    rts
ipcPickByte:
    lda ipcRingLo,x
    sta kPtr                     // kPtr = this channel's ring (see ipcPost)
    lda ipcRingHi,x
    sta kPtr+1
    lda ipcHead,x
    cmp ipcTail,x
    beq ipcPickEmpty             // head == tail: nothing buffered
    ldy ipcTail,x                // tail is the slot to read, and it stays in Y
    lda (kPtr),y
    pha
    iny                          // tail+1 -- tail is always 0..127, so no overflow
    tya
    and #IPC_MASK
    sta ipcTail,x                // ...then release the slot
    pla
    clc
    rts

// ==========================================
//  ipcBlkPick -- the block half of kIpcPick: X = $80+channel, kPtr = the
//  destination. One DMA out of the ring's oldest frame.
// ==========================================
//  C=1 (and nothing transferred) when the channel is empty, so a consumer polls
//  or sleeps exactly as it does for a byte channel. The tail byte is advanced
//  only AFTER the payload has landed in the caller's buffer, so a producer can
//  never overwrite the frame mid-transfer.
//  Clobbers A, X, Y, kPtr (read only), kArg..kArg+4.
ipcBlkPick:
    txa
    and #IPC_BLK_MASK_CH
    tay                          // Y = the block channel
    lda ipcBlkTail,y             // A = the frame this read will take
    cmp ipcBlkHead,y
    beq ipcPickEmpty             // head == tail: nothing buffered
    sta kArg+1                   // the frame index starts at the slot
    tya
    asl
    asl
    asl
    asl
    asl
    asl                          // channel x 64 = its first frame
    clc
    adc kArg+1
    sta kArg+1                   // = the frame index, which IS the address's hi byte
    ldx kArg+1
    lda ipcBlkLen,x              // the length its producer recorded
    sta kArg+3
    pha                          // ...and the answer this call returns
    lda #0
    sta kArg+4                   // length hi
    sta kArg                     // REU lo = 0: the frame's first byte
    lda #IPC_BLK_BANK
    sta kArg+2
    jsr kReuToC64                // the frame -> (kPtr); X and Y survive
    lda ipcBlkTail,y
    clc
    adc #1
    and #IPC_BLK_MASK
    sta ipcBlkTail,y             // ...then the frame may be reused
    pla
    clc
    rts

// ==========================================
//  Library data -- ONE copy, shared by every consumer, which is what makes this
//  a mailbox rather than a per-task buffer.
// ==========================================
ipcHead:     .fill IPC_CHANNELS, 0   // producer's write index, one per channel
ipcTail:     .fill IPC_CHANNELS, 0   // consumer's read index, one per channel
ipcBlkHead:  .fill IPC_BLK_CHANNELS, 0   // the same pair for the block frames
ipcBlkTail:  .fill IPC_BLK_CHANNELS, 0
//  The length of every frame in the bank, indexed by FRAME. One writer (that
//  frame's producer) and one reader (its consumer), which is what keeps this
//  lock-free on the same argument as head and tail. Written before the payload,
//  published by the head byte after it.
ipcBlkLen:   .fill IPC_BLK_USED, 0
//  Channel -> ring table. Both routines load a channel's base into kPtr, so the
//  rings need no page alignment and pack tightly. Each `.byte >ipcRingN` is a
//  relocated high byte and is picked up by the three-pass fixup generator.
ipcRingLo:   .byte <ipcRing0, <ipcRing1, <ipcRing2, <ipcRing3
ipcRingHi:   .byte >ipcRing0, >ipcRing1, >ipcRing2, >ipcRing3
ipcRing0:    .fill IPC_SIZE, 0
ipcRing1:    .fill IPC_SIZE, 0
ipcRing2:    .fill IPC_SIZE, 0
ipcRing3:    .fill IPC_SIZE, 0

//  The block frames are addressed as lo 0, hi = frame index, bank = the IPC
//  bank, so the base must be page-aligned and one page must hold the longest
//  message a length byte can describe.
.assert "the IPC bank starts on a bank boundary", (REU_IPC_BASE & $FFFF) == 0 ? 1 : 0, 1
.assert "the block channels fill that one bank", IPC_BLK_USED == $100 ? 1 : 0, 1
.assert "the frame index is the high byte of its address", IPC_BLK_BANK * $10000 == REU_IPC_BASE ? 1 : 0, 1
.assert "a frame holds the longest message", REU_IPC_FRAME >= 255 ? 1 : 0, 1

.label ipcEnd = *
