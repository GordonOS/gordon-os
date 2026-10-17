# Commodore 1750 REU on the C64 — Programming Guide

## Scope

This guide covers the Commodore 1750 REU (512 KiB) connected to a C64, with 6502 assembly examples.

The central idea is that the REU is **not directly addressable RAM** from the 6510. Instead, the C64 programs the REU/REC registers at `$DF00-$DF0A`, then the REU performs DMA between C64 memory and its own RAM.

The examples use conventional 6502 syntax compatible with assemblers such as ACME/64tass after minor syntax adjustments.

---

# 1. REU memory model

A 1750 has 512 KiB of REU RAM:

```text
bank 0 = $00000-$0FFFF
bank 1 = $10000-$1FFFF
bank 2 = $20000-$2FFFF
bank 3 = $30000-$3FFFF
bank 4 = $40000-$4FFFF
bank 5 = $50000-$5FFFF
bank 6 = $60000-$6FFFF
bank 7 = $70000-$7FFFF
```

The C64 CPU has only a 16-bit address bus, so it cannot execute:

```asm
lda $12000
```

Instead, the desired REU address is placed into the REU address registers, and a DMA operation transfers data to/from a C64 address.

Think of every REU operation as specifying:

1. C64 address
2. REU address + bank
3. transfer length
4. address-control mode
5. transfer type

---

# 2. REU register map

```text
$DF00  STATUS
$DF01  COMMAND
$DF02  C64 address low
$DF03  C64 address high
$DF04  REU address low
$DF05  REU address high
$DF06  REU bank
$DF07  transfer length low
$DF08  transfer length high
$DF09  interrupt mask
$DF0A  address control
```

Convenient definitions:

```asm
REU_STATUS  = $DF00
REU_COMMAND = $DF01
REU_C64LO   = $DF02
REU_C64HI   = $DF03
REU_REULO   = $DF04
REU_REUHI   = $DF05
REU_BANK    = $DF06
REU_LENLO   = $DF07
REU_LENHI   = $DF08
REU_IRQ     = $DF09
REU_CTRL    = $DF0A
```

Command constants:

```asm
REU_EXECUTE     = %10000000    ; bit 7 — start DMA
REU_AUTOLOAD    = %00100000    ; bit 5 — save params for re-trigger
REU_IMMEDIATE   = %00010000    ; bit 4 — $DF01 trigger (clear = $FF00 trigger)

REU_C64_TO_REU  = %00000000    ; %00
REU_REU_TO_C64  = %00000001    ; %01
REU_SWAP        = %00000010    ; %10
REU_VERIFY      = %00000011    ; %11
```

> **GordonOS always uses immediate mode.** The kernel's `REU_EXEC = $90` packs execute (`$80`) + immediate (`$10`), so commands are `$90`–`$93` (and `$B0`–`$B3` with AUTOLOAD). This matches the existing scheduler/init code which uses `$90`/`$91`. The examples below use the guide's raw constants for clarity; in GordonOS source, use `REU_EXEC | REU_CMD_*` from `kernel.inc`.

Address-control constants:

```asm
REU_FIX_C64     = %10000000
REU_FIX_REU     = %01000000
```

Status constants commonly used:

```asm
REU_ST_IRQ      = %10000000
REU_ST_EOB      = %01000000
REU_ST_ERROR    = %00100000
```

Interrupt-mask constants:

```asm
REU_IRQ_ENABLE  = %10000000
REU_IRQ_EOB     = %01000000
REU_IRQ_ERROR   = %00100000
```

---

# 3. STATUS — `$DF00`

The status register reports the result/state of the most recent REU operation.

Important bits:

```text
bit 7  IRQ pending
bit 6  End of block
bit 5  Verify fault
```

A simple polling loop:

```asm
WaitREU:
        lda REU_STATUS
        and #REU_ST_EOB
        beq WaitREU
        rts
```

For VERIFY:

```asm
WaitVerify:
        lda REU_STATUS
        and #REU_ST_EOB
        beq WaitVerify

        lda REU_STATUS
        and #REU_ST_ERROR
        bne NotEqual

Equal:
        rts

NotEqual:
        ; comparison failed
        rts
```

Important: status event bits are cleared by reading the status register, so code that needs to preserve status information should save the value immediately.

---

# 4. COMMAND — `$DF01`

The transfer type is selected by the low two bits:

```text
%00 = C64 -> REU
%01 = REU -> C64
%10 = SWAP
%11 = VERIFY
```

Bit 7 starts the operation.

Bit 5 enables AUTOLOAD.

The command register therefore provides the basic four operations:

```asm
lda #REU_EXECUTE | REU_C64_TO_REU
sta REU_COMMAND

lda #REU_EXECUTE | REU_REU_TO_C64
sta REU_COMMAND

lda #REU_EXECUTE | REU_SWAP
sta REU_COMMAND

lda #REU_EXECUTE | REU_VERIFY
sta REU_COMMAND
```

---

# 5. C64 -> REU

This is the basic store/save operation.

Example: copy 256 bytes from C64 `$4000` to REU bank 3, offset `$2000`.

```asm
        ; C64 source = $4000
        lda #<$4000
        sta REU_C64LO
        lda #>$4000
        sta REU_C64HI

        ; REU destination = bank 3:$2000
        lda #<$2000
        sta REU_REULO
        lda #>$2000
        sta REU_REUHI
        lda #3
        sta REU_BANK

        ; length = $0100 = 256 bytes
        lda #<$0100
        sta REU_LENLO
        lda #>$0100
        sta REU_LENHI

        ; increment both addresses
        lda #0
        sta REU_CTRL

        ; execute C64 -> REU
        lda #REU_EXECUTE | REU_C64_TO_REU
        sta REU_COMMAND
```

Conceptually:

```text
REU bank 3:$2000-$20FF = C64 $4000-$40FF
```

---

# 6. REU -> C64

Reverse the direction:

```asm
        ; C64 destination = $4000
        lda #<$4000
        sta REU_C64LO
        lda #>$4000
        sta REU_C64HI

        ; REU source = bank 3:$2000
        lda #<$2000
        sta REU_REULO
        lda #>$2000
        sta REU_REUHI
        lda #3
        sta REU_BANK

        ; length = 256
        lda #<$0100
        sta REU_LENLO
        lda #>$0100
        sta REU_LENHI

        lda #0
        sta REU_CTRL

        lda #REU_EXECUTE | REU_REU_TO_C64
        sta REU_COMMAND
```

To wait for completion:

```asm
WaitREU:
        lda REU_STATUS
        and #REU_ST_EOB
        beq WaitREU
```

---

# 7. SWAP

SWAP exchanges the contents of C64 and REU memory.

Before:

```text
C64  = AAAAAAAA
REU  = BBBBBBBB
```

After:

```text
C64  = BBBBBBBB
REU  = AAAAAAAA
```

Example:

```asm
        lda #<$4000
        sta REU_C64LO
        lda #>$4000
        sta REU_C64HI

        lda #<$2000
        sta REU_REULO
        lda #>$2000
        sta REU_REUHI
        lda #3
        sta REU_BANK

        lda #<$0100
        sta REU_LENLO
        lda #>$0100
        sta REU_LENHI

        lda #0
        sta REU_CTRL

        lda #REU_EXECUTE | REU_SWAP
        sta REU_COMMAND
```

Useful applications include:

- double buffering
- screen swapping
- save/restore workspaces
- temporary buffers
- context switching

---

# 8. VERIFY

VERIFY compares C64 and REU memory without copying.

Example:

```asm
        ; C64 = $4000
        lda #<$4000
        sta REU_C64LO
        lda #>$4000
        sta REU_C64HI

        ; REU = bank 3:$2000
        lda #<$2000
        sta REU_REULO
        lda #>$2000
        sta REU_REUHI
        lda #3
        sta REU_BANK

        ; length = 256
        lda #<$0100
        sta REU_LENLO
        lda #>$0100
        sta REU_LENHI

        lda #0
        sta REU_CTRL

        lda #REU_EXECUTE | REU_VERIFY
        sta REU_COMMAND

WaitVerify:
        lda REU_STATUS
        and #REU_ST_EOB
        beq WaitVerify

        lda REU_STATUS
        and #REU_ST_ERROR
        bne NotEqual

Equal:
        ; identical
        rts

NotEqual:
        ; mismatch
        rts
```

This is useful for:

- cache validation
- RAM-disk checks
- comparing buffers
- detecting changed data

---

# 9. REU address registers

The REU address consists of:

```text
$DF04  low byte
$DF05  high byte
$DF06  bank
```

For example:

```asm
        ; REU address = bank 5:$1234
        lda #<$1234
        sta REU_REULO
        lda #>$1234
        sta REU_REUHI
        lda #5
        sta REU_BANK
```

This corresponds to physical REU address `$51234`.

---

# 10. Transfer length

The length is a 16-bit value:

```asm
        ; 4096 bytes
        lda #<$1000
        sta REU_LENLO
        lda #>$1000
        sta REU_LENHI
```

A length of `$0000` represents a 64 KiB transfer rather than a zero-length transfer.

Thus:

```asm
        lda #0
        sta REU_LENLO
        sta REU_LENHI
```

requests 65536 bytes.

---

# 11. Automatic address increment

With normal address control:

```asm
        lda #0
        sta REU_CTRL
```

both addresses increment after each transferred byte.

Example:

```text
C64 $4000 -> REU bank 2:$1000
C64 $4001 -> REU bank 2:$1001
C64 $4002 -> REU bank 2:$1002
...
```

This is the normal mode for copying blocks.

---

# 12. Fixed C64 address

Set bit 7 of `$DF0A`:

```asm
        lda #REU_FIX_C64
        sta REU_CTRL
```

The C64 address remains constant while the REU address advances.

For example:

```text
C64[$D020] -> REU[$00000]
C64[$D020] -> REU[$00001]
C64[$D020] -> REU[$00002]
...
```

This is useful for:

- repeated sampling
- pattern generation
- hardware-assisted fills
- unusual I/O-style transfers

---

# 13. Fixed REU address

Set bit 6:

```asm
        lda #REU_FIX_REU
        sta REU_CTRL
```

Then:

```text
REU[$20000] -> C64[$4000]
REU[$20000] -> C64[$4001]
REU[$20000] -> C64[$4002]
...
```

This is useful for filling C64 memory from one REU byte.

---

# 14. Fixed both addresses

```asm
        lda #REU_FIX_C64 | REU_FIX_REU
        sta REU_CTRL
```

Now both addresses remain fixed.

This is a specialised mode and is less useful for normal block copies.

---

# 15. AUTOLOAD

Normally, the REU's current address/length registers are updated as a transfer progresses.

AUTOLOAD allows the original transfer parameters to be restored for repeated transfers.

Example:

```asm
        ; configure C64 address, REU address, bank and length once

        lda #REU_EXECUTE | REU_AUTOLOAD | REU_REU_TO_C64
        sta REU_COMMAND
```

This is useful for repeatedly copying the same REU block to the same C64 destination.

Typical applications:

- animation frames
- repeated screen transfers
- repeatedly restoring a template
- periodic data transfers

---

# 16. FF00 trigger

The command register has a control bit associated with triggering DMA via `$FF00`.

For ordinary programming, immediate execution is simpler:

```asm
        lda #REU_EXECUTE | REU_REU_TO_C64
        sta REU_COMMAND
```

The FF00-trigger mechanism can be useful when a program needs to synchronise the start of DMA with a write to `$FF00`.

---

# 17. REU interrupts

The interrupt mask register is `$DF09`.

The relevant controls are:

```text
bit 7 = enable REU interrupt
bit 6 = end-of-block interrupt
bit 5 = verify-error interrupt
```

For an end-of-block interrupt:

```asm
        lda #REU_IRQ_ENABLE | REU_IRQ_EOB
        sta REU_IRQ
```

Then start the DMA:

```asm
        lda #REU_EXECUTE | REU_REU_TO_C64
        sta REU_COMMAND
```

When the transfer completes, the REU can signal the C64.

---

# 18. IRQ handler considerations

A C64 program should normally integrate REU IRQ handling into its existing IRQ handler instead of blindly replacing the system handler.

A simplified handler:

```asm
REUIRQ:
        pha
        txa
        pha
        tya
        pha

        lda REU_STATUS
        sta REU_LAST_STATUS

        and #REU_ST_IRQ
        beq NotREU

        lda #1
        sta DMA_DONE

NotREU:
        pla
        tay
        pla
        tax
        pla
        rti

REU_LAST_STATUS:
        .byte 0

DMA_DONE:
        .byte 0
```

The exact integration depends on whether you are using the KERNAL IRQ chain, a custom raster IRQ, or a completely custom interrupt setup.

---

# 19. IMPORTANT: SEI/CLI and REU DMA

You do **not** need to execute `SEI` before every REU operation.

The REU performs DMA independently of normal 6510 instruction execution.

The critical distinction is:

```text
SEI around REU setup
```

versus:

```text
SEI for the entire duration of DMA
```

These are not the same.

A sensible design is often:

```asm
        sei

        ; configure all REU registers
        ; configure REU interrupt if required
        ; start DMA

        cli

        ; DMA may still be running here
```

This protects the configuration/start sequence without unnecessarily disabling C64 interrupts during a long DMA transfer.

Do NOT normally do this:

```asm
        sei

        ; start DMA

wait:
        lda REU_STATUS
        and #REU_ST_EOB
        beq wait

        cli
```

unless you specifically require the entire DMA to be non-interruptible.

Keeping IRQs disabled for a large transfer can interfere with:

- keyboard scanning
- CIA timer IRQs
- system time
- music players
- raster IRQs
- other interrupt-driven code

---

# 20. Correctly preserving the caller's interrupt state

A library routine should not blindly do:

```asm
sei
...
cli
```

because the caller may already have had IRQs disabled.

For example, this is wrong as a reusable library routine:

```asm
REU_Copy:
        sei
        ; setup
        ; start
        cli
        rts
```

If the caller entered with IRQs disabled, this routine unexpectedly enables them.

A better approach is to save the processor status and restore it.

One simple pattern is:

```asm
REU_Copy:
        php
        sei

        ; configure REU
        ; start DMA

        plp
        rts
```

`PHP` saves the processor status, including the I flag. `PLP` restores it.

However, this only protects the **setup/start sequence**. It does not wait for DMA completion.

For a polling routine that must wait for completion, a more appropriate pattern is:

```asm
REU_CopyWait:
        php
        sei

        ; configure REU
        ; start DMA

        plp

WaitDMA:
        lda REU_STATUS
        and #REU_ST_EOB
        beq WaitDMA

        rts
```

This means:

1. save caller's flags
2. disable IRQs during REU setup/start
3. restore caller's IRQ state
4. allow normal interrupts while DMA is running
5. poll until DMA completes

This is usually a much better default for a general-purpose blocking REU routine.

---

# 21. Why the exact library policy matters

There are two different concerns:

### Protecting REU setup

If an interrupt handler can touch the REU registers, you don't want an IRQ to occur halfway through:

```text
write C64 low
write C64 high
write REU low
write REU high
write bank
write length
...
```

So:

```asm
php
sei
...
plp
```

is useful.

### Waiting for DMA

Once the DMA has started, there is generally no reason to keep IRQs disabled.

Therefore:

```asm
php
sei
; setup
; start
plp

; DMA runs
; IRQs can run

wait for EOB
```

is generally preferable to:

```asm
sei
; setup
; start
; wait for EOB
cli
```

---

# 22. A reusable C64 -> REU routine

This version assumes the caller has placed parameters in zero page.

```asm
C64LO   = $02
C64HI   = $03
REULO   = $04
REUHI   = $05
REUBANK = $06
LENLO   = $07
LENHI   = $08

REU_CopyTo:
        php
        sei

        lda C64LO
        sta REU_C64LO
        lda C64HI
        sta REU_C64HI

        lda REULO
        sta REU_REULO
        lda REUHI
        sta REU_REUHI

        lda REUBANK
        sta REU_BANK

        lda LENLO
        sta REU_LENLO
        lda LENHI
        sta REU_LENHI

        lda #0
        sta REU_CTRL

        lda #REU_EXECUTE | REU_C64_TO_REU
        sta REU_COMMAND

        plp
        rts
```

This routine starts the transfer and returns immediately. The caller can poll `$DF00` or use an interrupt.

---

# 23. Blocking C64 -> REU routine

If the routine should not return until DMA has completed:

```asm
REU_CopyToWait:
        php
        sei

        lda C64LO
        sta REU_C64LO
        lda C64HI
        sta REU_C64HI

        lda REULO
        sta REU_REULO
        lda REUHI
        sta REU_REUHI

        lda REUBANK
        sta REU_BANK

        lda LENLO
        sta REU_LENLO
        lda LENHI
        sta REU_LENHI

        lda #0
        sta REU_CTRL

        lda #REU_EXECUTE | REU_C64_TO_REU
        sta REU_COMMAND

        plp

WaitCopyTo:
        lda REU_STATUS
        and #REU_ST_EOB
        beq WaitCopyTo

        rts
```

Notice that `PLP` happens **before** the wait loop.

Thus the caller's original interrupt state is restored while the REU DMA is still in progress.

---

# 24. Blocking REU -> C64 routine

```asm
REU_CopyFromWait:
        php
        sei

        lda C64LO
        sta REU_C64LO
        lda C64HI
        sta REU_C64HI

        lda REULO
        sta REU_REULO
        lda REUHI
        sta REU_REUHI

        lda REUBANK
        sta REU_BANK

        lda LENLO
        sta REU_LENLO
        lda LENHI
        sta REU_LENHI

        lda #0
        sta REU_CTRL

        lda #REU_EXECUTE | REU_REU_TO_C64
        sta REU_COMMAND

        plp

WaitCopyFrom:
        lda REU_STATUS
        and #REU_ST_EOB
        beq WaitCopyFrom

        rts
```

---

# 25. SWAP routine

```asm
REU_SwapWait:
        php
        sei

        lda C64LO
        sta REU_C64LO
        lda C64HI
        sta REU_C64HI

        lda REULO
        sta REU_REULO
        lda REUHI
        sta REU_REUHI

        lda REUBANK
        sta REU_BANK

        lda LENLO
        sta REU_LENLO
        lda LENHI
        sta REU_LENHI

        lda #0
        sta REU_CTRL

        lda #REU_EXECUTE | REU_SWAP
        sta REU_COMMAND

        plp

WaitSwap:
        lda REU_STATUS
        and #REU_ST_EOB
        beq WaitSwap

        rts
```

---

# 26. VERIFY routine

A useful VERIFY routine should return a clear result.

For example:

```text
A = 0  equal
A = 1  different
```

Implementation:

```asm
REU_VerifyWait:
        php
        sei

        lda C64LO
        sta REU_C64LO
        lda C64HI
        sta REU_C64HI

        lda REULO
        sta REU_REULO
        lda REUHI
        sta REU_REUHI

        lda REUBANK
        sta REU_BANK

        lda LENLO
        sta REU_LENLO
        lda LENHI
        sta REU_LENHI

        lda #0
        sta REU_CTRL

        lda #REU_EXECUTE | REU_VERIFY
        sta REU_COMMAND

        plp

WaitVerify2:
        lda REU_STATUS
        and #REU_ST_EOB
        beq WaitVerify2

        lda REU_STATUS
        and #REU_ST_ERROR
        beq VerifyEqual

        lda #1
        rts

VerifyEqual:
        lda #0
        rts
```

---

# 27. Fast memory fill using fixed addresses

A useful REU trick is to keep one side fixed.

Suppose the byte at C64 `$4000` should be copied throughout a REU block.

Set:

```asm
        lda #<$4000
        sta REU_C64LO
        lda #>$4000
        sta REU_C64HI

        lda #0
        sta REU_REULO
        sta REU_REUHI
        sta REU_BANK

        ; 256 bytes
        lda #<$0100
        sta REU_LENLO
        lda #>$0100
        sta REU_LENHI

        ; fixed C64 address
        lda #REU_FIX_C64
        sta REU_CTRL

        lda #REU_EXECUTE | REU_C64_TO_REU
        sta REU_COMMAND
```

Conceptually:

```text
C64[$4000] -> REU[$00000]
C64[$4000] -> REU[$00001]
C64[$4000] -> REU[$00002]
...
```

---

# 28. Double buffering with SWAP

Suppose:

```text
C64 screen buffer = $4000-$5FFF
REU screen buffer = bank 0:$4000-$5FFF
```

Swap them:

```asm
        lda #<$4000
        sta REU_C64LO
        lda #>$4000
        sta REU_C64HI

        lda #<$4000
        sta REU_REULO
        lda #>$4000
        sta REU_REUHI

        lda #0
        sta REU_BANK

        lda #<$2000
        sta REU_LENLO
        lda #>$2000
        sta REU_LENHI

        lda #0
        sta REU_CTRL

        lda #REU_EXECUTE | REU_SWAP
        sta REU_COMMAND
```

This exchanges the two 8 KiB buffers.

---

# 29. Save/restore state

A REU can provide the basis for game save states or application snapshots.

For example, save a large C64 RAM region:

```asm
        lda #<$0800
        sta REU_C64LO
        lda #>$0800
        sta REU_C64HI

        lda #0
        sta REU_REULO
        sta REU_REUHI
        sta REU_BANK

        lda #<$F800
        sta REU_LENLO
        lda #>$F800
        sta REU_LENHI

        lda #0
        sta REU_CTRL

        lda #REU_EXECUTE | REU_C64_TO_REU
        sta REU_COMMAND
```

Restore using:

```asm
        lda #<$0800
        sta REU_C64LO
        lda #>$0800
        sta REU_C64HI

        lda #0
        sta REU_REULO
        sta REU_REUHI
        sta REU_BANK

        lda #<$F800
        sta REU_LENLO
        lda #>$F800
        sta REU_LENHI

        lda #0
        sta REU_CTRL

        lda #REU_EXECUTE | REU_REU_TO_C64
        sta REU_COMMAND
```

A real snapshot system must additionally decide how to handle:

- zero page
- stack
- VIC registers
- CIA state
- I/O registers
- interrupt state
- KERNAL state
- cartridge/expansion state

So a raw RAM copy is not automatically a complete machine snapshot.

---

# 30. 64 KiB boundary considerations

The C64-side address is only 16 bits.

If a transfer starts at `$F000` and runs for `$2000` bytes, the C64 address wraps:

```text
$F000
...
$FFFF
$0000
...
$0FFF
```

Likewise, the REU address can wrap at the end of the available REU address space.

For code that logically treats memory as a contiguous larger address space, split transfers at the relevant boundaries rather than assuming a 16-bit address will magically become `$10000`.

For the 1750, the physical REU space is `$00000-$7FFFF`.

---

# 31. A simple RAM-disk layout

A useful 1750 application is to divide its RAM into logical areas:

```text
bank 0   program data
bank 1   graphics
bank 2   music
bank 3   level data
bank 4   decompression workspace
bank 5   cache
bank 6   cache
bank 7   cache
```

A 16 KiB block can be loaded from bank 3:

```asm
        ; C64 destination = $4000
        lda #<$4000
        sta REU_C64LO
        lda #>$4000
        sta REU_C64HI

        ; REU source = bank 3:$0000
        lda #0
        sta REU_REULO
        sta REU_REUHI
        lda #3
        sta REU_BANK

        ; 16 KiB
        lda #<$4000
        sta REU_LENLO
        lda #>$4000
        sta REU_LENHI

        lda #0
        sta REU_CTRL

        lda #REU_EXECUTE | REU_REU_TO_C64
        sta REU_COMMAND
```

---

# 32. Complete operation matrix

```text
Transfer type     Command low bits     Meaning
------------------------------------------------
C64 -> REU             %00             store
REU -> C64             %01             load
SWAP                   %10             exchange
VERIFY                 %11             compare
```

Typical commands:

```asm
; C64 -> REU
lda #$80
sta REU_COMMAND

; REU -> C64
lda #$81
sta REU_COMMAND

; SWAP
lda #$82
sta REU_COMMAND

; VERIFY
lda #$83
sta REU_COMMAND

; REU -> C64 with AUTOLOAD
lda #$A1
sta REU_COMMAND
```

---

# 33. Recommended programming model

For a robust C64 application, use this model:

```text
1. Save processor status
2. SEI
3. Configure all REU registers
4. Start DMA
5. Restore processor status
6. Either:
     a. poll for EOB, or
     b. continue working and receive REU IRQ
7. Check status/error when appropriate
```

The key point is:

```asm
php
sei

; REU setup
; start DMA

plp
```

rather than blindly:

```asm
sei
...
cli
```

because the former preserves the caller's original interrupt state.

---

# 34. Recommended blocking transfer pattern

```asm
REU_BlockingTransfer:
        php
        sei

        ; configure C64 address
        ; configure REU address
        ; configure bank
        ; configure length
        ; configure address control

        ; start DMA

        plp

Wait:
        lda REU_STATUS
        and #REU_ST_EOB
        beq Wait

        rts
```

This lets the C64 service interrupts during the potentially long DMA.

---

# 35. Important caveats for a production library

A production REU library should also decide:

1. Whether it owns `$DF00-$DF0A` exclusively.
2. Whether IRQ handlers can use the REU.
3. Whether the routine is blocking or asynchronous.
4. How REU IRQs are integrated with the C64 IRQ vector.
5. How transfers crossing C64 `$FFFF/$0000` are split.
6. How transfers crossing REU bank boundaries are handled.
7. Whether the caller expects A/X/Y to be preserved.
8. Whether processor flags should be preserved.
9. Whether the routine should detect a missing/wrong REU.
10. Whether the program runs under KERNAL, a custom IRQ system, or a cartridge environment.

---

# 36. Summary

The 1750's most important capabilities are:

- 512 KiB of external RAM
- C64 -> REU DMA
- REU -> C64 DMA
- SWAP
- VERIFY
- 19-bit REU addressing
- 16-bit C64 addressing
- 16-bit transfer length
- `$0000` length meaning 64 KiB
- fixed C64 address
- fixed REU address
- fixed both addresses
- AUTOLOAD
- FF00-triggered operation
- end-of-block IRQ
- verify-error IRQ
- REU IRQ status
- DMA address wrapping

The most important conceptual rule is:

> **The REU is DMA-controlled external memory, not directly addressable 6510 memory.**

And the most important interrupt rule is:

> **Do not blindly `SEI` before every REU operation and `CLI` afterward. Protect the REU configuration/start sequence when necessary, preserve the caller's interrupt state with `PHP/PLP`, and normally allow interrupts while a long DMA transfer is in progress.**

---

# 37. Suggested next step

A full production-quality 1750 library can build on these primitives:

```text
REU_Init
REU_Detect
REU_CopyTo
REU_CopyFrom
REU_CopyToWait
REU_CopyFromWait
REU_Swap
REU_Verify
REU_Fill
REU_SaveState
REU_LoadState
REU_SetBank
REU_Wait
REU_EnableIRQ
REU_DisableIRQ
```

Such a library should use a documented zero-page calling convention and explicitly preserve the registers/flags promised to the caller.

