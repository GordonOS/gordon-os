# Security Policy

## Supported versions

Only the current `main` branch is supported. GordonOS is a hobby operating
system for the Commodore 64; fixes land on `main` and are published to the
binaries repository.

## Reporting a vulnerability

Please report suspected security issues privately — do **not** open a public
issue.

- Email **Besim Atalay** at [besim.atalay@gmail.com](mailto:besim.atalay@gmail.com)
  with a description, the version from `ver`, the machine or emulator used, and
  the steps to reproduce.
- If GitHub's private vulnerability reporting is available on this repository,
  use **Security → Report a vulnerability** instead.

You will get an acknowledgement as soon as possible. There is no bug bounty;
credit is given in the fix commit when requested.

## In scope

GordonOS banks out both ROMs and runs with interrupts disabled around kernel
calls, so the interesting failure modes are memory safety and task isolation:

- a task reading or writing another task's memory, stack or zero page
- a syscall or loader path that frees pages still in use, or hands one page to
  two owners
- a kernel or library routine that corrupts a caller's registers or the `kArg`
  block
- a malformed task or library binary, REU filesystem image, or `.pic`/`.mus`
  file that crashes or corrupts the kernel

## Not in scope

- bugs in the bundled demos and games that affect only their own screen
- timing or audio quirks caused by the emulator's `speed` setting
- the third-party non-commercial terms listed in `NOTICE`
