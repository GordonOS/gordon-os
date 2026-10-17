// ==========================================
//  GordonOS -- Syscall numbers
// ==========================================
//      lda #SYS_GETTID
//      jsr syscall
//
//  The number goes in A; `syscall` is the dispatcher (the kernel's own
//  `syscall:` label internally, and the alias for kSyscall, $E090, in
//  task code).
//
//  Imported by BOTH builds:
//    - kernel:  src/main.asm imports this directly
//    - tasks:   the generated tools/assemble-tasks.asm imports it
//  It lives in its own file because neither jump table could host it:
//  src/kernel/jumptable.inc defines `k*` names that are real labels in
//  kernel.asm (a duplicate-symbol error if the kernel imported it), and
//  src/kernel/syscall.inc emits the dispatcher itself.
//
//  These MUST match `syscallTbl` in kernel/syscall.inc -- the dispatcher
//  indexes that table with the number, so a mismatch silently calls the
//  wrong routine.
//
//  Numbers 8 and 9 are deliberate gaps -- inert no-ops (allocscreen,
//  freescreen) kept so the numbering does not shift. Call kAllocScreen /
//  kFreeScreen directly instead.
//  Numbers 10 and 11 (semWait / semSignal) are the counting-semaphore pair
//  for task synchronisation: index in X (semaphore 0 is the screen lock --
//  do not use it from a task).
//
//  Do NOT confuse these with the *loader* error codes (0-13) that
//  kDynLoad returns in kArg when kArg=0 -- different numbering at the
//  same values (e.g. syscall 4 is pause, loader error 4 is "no space in
//  pool").

.const SYS_EXIT        = 0     // terminate the current task (never returns)
.const SYS_SPAWN       = 1     // spawnPtr, taskNamePtr, kArg+1 = dataSize
.const SYS_KILL        = 2     // X = target index (cascades to thread children)
.const SYS_YIELD       = 3     // give up the timeslice; resumes at JSR+3
.const SYS_PAUSE       = 4     // X = target index
.const SYS_RESUME      = 5     // X = target index
.const SYS_SETPRIO     = 6     // X = target, kArg+1 = priority 0-5
.const SYS_GETTID      = 7     // returns the caller's slot in A
.const SYS_SEMWAIT     = 10    // X = semaphore index; blocks (yields) until acquired
.const SYS_SEMSIGNAL   = 11    // X = semaphore index; releases; count caps at 255
.const SYS_THREADSPAWN = 12    // spawnPtr = entry; child is parent.N; kArg+6/+7 = zpBase/zpLen (caller's to set)
.const SYS_SLEEP       = 13    // kArg+1 = tick count; resumes after N ticks
