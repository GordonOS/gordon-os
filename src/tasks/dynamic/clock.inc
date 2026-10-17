// ==========================================
//  GordonOS -- Clock task
// ==========================================
//  Prints current CIA #1 TOD time at (0,0) via kPrintTime -> kBlitChar
//  (fixed-position blit: no cursor advance, no char-shadow mirror).
//  Shares the screen of the shell that spawned it; kLockScreen/
//  kUnlockScreen serialise the writes against the shell.
//  Updates ~60 times/sec (every IRQ tick).
//  NOTE (all-bitmap): kBlitChar writes the DISPLAYED framebuffer
//  only, so the blit is skipped (kSurfDisplayed C=0) while the shell is
//  F-keyed away -- the time refreshes on the next tick after switch-back.
//  Fallback path allocates hires-text (mode 0).
//  WARNING: THIS TASK KEEPS NO CLOCK. It displays the CIA #1 TOD registers, and
//  nothing in the kernel initialises them -- the only writer in the tree is
//  `time`'s kSetTime -- so the display is meaningful only after
//  `time hh:mm[:ss] [am|pm]` has set it.
//  WARNING: ACCURACY. The TOD is the emulated machine's time base:
//    * accurate only while the CPU runs at 100% -- any other VICE `-speed` value
//      makes the displayed time drift;
//    * unreliable under REU load: an REU operation halts the processor, which
//      disrupts the time, so it falls behind while the system is doing file I/O,
//      task loads or surface switches.
//  The same limits, from the library's side, are in docs/lib-time.md.

clock:
    lda #SYS_GETTID; jsr syscall         // gettid -> A
    tax
    lda taskParent,x            // parent (shell) slot
    tax
    jsr kShareScreen            // share the caller shell's screen
    bcc clkGo                   // success
    lda #0; jsr kAllocScreen    // fallback: own screen
    bcc clkGo                   // got a screen
    lda #SYS_YIELD; jsr syscall         // no screens -- yield forever
    jmp *-3
clkGo:
    jsr kLockScreen             // acquire screen semaphore
    lda #SYS_GETTID
    jsr syscall                 // gettid -> A = own slot (curTask ZP is IRQ-owned)
    tax
    jsr kSurfDisplayed          // C=1 iff our shared surface is displayed
    bcc clkSkip                 // background -> skip (kBlitChar is framebuffer-only)
    ldx #0
    ldy #0
    jsr kPrintTime              // blit "hh:mm:ss AM" at (0,0) -- no cursor/shadow
clkSkip:
    jsr kUnlockScreen           // release screen semaphore
    lda #SYS_YIELD
    jsr syscall                 // yield
    jmp clock

.encoding "screencode_mixed"
nameClock:
    .text "clock"
    .byte 0
