// ==========================================
//  libHold -- release every library this task declared EXCEPT those in a mask
//                                                                   (kLibHold)
// ==========================================
//  Input:  A = MASK of lib ids to KEEP -- bit n keeps library n, so 0 gives
//              every declared library back. Any 8-bit value is valid.
//  Output: C = 0 always (there is no failure path -- see the pocket note below)
//  Clobbers: A, X, Y, dlLibTmp
//
//  `libMask` is a LIFETIME declaration: a library a task declares stays
//  resident until that task exits, even if it stopped needing it after the
//  first few instructions. A task that reads a file once and then runs for
//  minutes on something else would hold the file library's pages for the whole
//  run, which on a loaded system shows up as eviction churn. `kLibHold` hands
//  those pages back as soon as the task is done with them.
//
//  HOW IT WORKS -- it does not reimplement refcounting. `dlLibRelease` walks the
//  mask STORED IN THE TASK'S DYN SLOT, so the trick is to make that stored mask
//  say "the libraries I am giving back" for the duration of the call, and then
//  put the kept bits back:
//
//    1. clear the kept bits in the stored mask ("everything else")
//    2. call dlLibRelease -- it decrements exactly those and frees any at zero
//    3. store the kept mask alone as the task's mask ("I hold only these now")
//
//  WARNING: THE FIRST STEP IS `and ~keep`, NEVER "store keep". The stored mask IS the
//  ownership, so the order of these three steps is the whole routine: store the
//  keep mask first and dlLibRelease walks the mask "keep these", releasing the
//  libraries it was told to keep while never touching the rest.
//
//  The stack holds both things `dlLibRelease` clobbers -- the dyn slot (Y) and
//  the keep bit -- because it also clobbers dlLibTmp/dlLibTmp2, which is where
//  this would otherwise stash them.
//
//  WARNING: ONE-WAY. A LAZY library this task releases and then uses again reloads
//  through its cold stub (`libFree` puts that back), but the reload is credited to
//  nobody -- refcount 0 -- and a refcount-0 library can never be freed again (the
//  release path refuses to decrement below zero), so its pages leak until a
//  reboot. An EAGER library -- gfx.lib, fp.lib, sprite.lib -- has no cold stub:
//  its entries are the fail-safe `jmp libTrap` (`sec`/`rts`), so the calls go
//  silent and nothing reloads it until a task that still declares the library
//  loads it and `libLoad` re-patches the shared entries. Release only what the
//  task is done with for good: a library the task keeps CALLING -- every frame,
//  in a loop -- has to be in the keep mask.
//
//  WARNING: Call it LAST -- anything the caller still needs must be in the keep
//  mask, or (better) declared libs it still uses must be acquired again. Releasing a
//  library another task also declares is safe: the refcount simply does not
//  reach zero, so the code and pages stay.
//
//  A dynamic task always has a dyn-table entry while it runs (every task loads
//  through kDynLoad), which is why the "not found" return of dynFindByTaskId is
//  not tested -- it cannot happen here, and the 2 bytes it would cost are the
//  bytes that let this fit in the pocket.
//
//  Lives in the $1D16-$1D3F pocket (42B, just below the data heap at $1D40):
//  the kernel's main code band ends flush against the $FF7E library-entry block
//  with ZERO slack, so a routine this size has nowhere else to go. Bounded by
//  an assert in src/main.asm. The code is 33B; the 9B left over are held as an
//  inert NOP sled so the pocket's end address -- and the free-space totals that
//  tools/verify-map.ps1 pins -- do not move. The sled is where a future change
//  buys its bytes here.
// ==========================================

.pc = $1D16

libHold:
    php                         // preserve the caller's I flag (libLoad does)
    sei                         // the mask writes and the walk must be atomic
    pha                         // the keep mask, parked across dynFindByTaskId
    lda curTask
    jsr dynFindByTaskId         // A = this task's dyn slot
    tay                         // Y = dyn slot (the mask is indexed by slot)
    pla                         // A = the keep mask
    pha                         // ...stash it for the final store
    eor #$ff                    // ~keep -- complement the MASK, while A still
                                //   holds it: a `tya` before this complements
                                //   the DYN SLOT instead, and the release then
                                //   walks a garbage mask.
    and dynTaskLibMaskLo,y      // = exactly the libraries to give back
    sta dynTaskLibMaskLo,y      // dlLibRelease walks the mask STORED HERE, so
                                //   this is the state that decides what goes
    tya                         // ...stash the slot (the call clobbers Y)...
    pha
    jsr dlLibRelease            // decrement the rest; free any that hits zero
    pla                         // dyn slot
    tay
    pla                         // the keep mask
    sta dynTaskLibMaskLo,y      // mask = exactly the libraries we keep now
    plp
    clc
    rts

    .fill 9, $ea                // the pocket's leftover bytes: inert NOPs, so the
                                //   pocket end stays put (never $00 -- that is a BRK)

.label libHoldEnd = *
