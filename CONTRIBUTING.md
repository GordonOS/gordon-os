# Contributing to GordonOS

Thanks for looking. GordonOS is a from-scratch preemptive multitasking OS for the
Commodore 64 with a REU, written entirely in 6502 assembly, and it is an ambitious
machine to work on — but the tree is small enough to hold in your head, and every
subsystem has a document.

**New here?** Read the [README](README.md) front matter, then
[docs/features.md](docs/features.md) for the tour and
[docs/tutorial.md](docs/tutorial.md) for a guided path from "I have read the README"
to "I have written and shipped a task".

---

## Ways to help

| | |
|---|---|
| **Report a bug** | Open an issue with the "Run" command, the emulator/hardware and the REU image used, and what you expected. A wrong screen or a hang is usually a documented rule about to bite — see *Conventions* below. |
| **Fix a bug** | `docs/bugs.md` is the bug log: each entry has a root cause, a fix and a "transferable" rule. `docs/future-improvements.md` lists the roadmap. |
| **Add a demo task** | `docs/tutorial.md` walks one end to end; `tools/tasks.json` registers it and the build picks it up on the next run. |
| **Port or write a library** | `docs/dynamic-libraries.md` and the per-library reference (`docs/lib-*.md`) are the contract. |
| **Improve the docs** | Every `docs/*.md` describes the system as it is **now** (see *Conventions* — no history, no changelogs). |
| **Settle a hardware question** | `docs/c64_1750_REU_programming_guide.md`, `docs/lib-mouse.md` and the VIC-II article show the standard this repo holds itself to: measure or read the reference, never recall. |
| **Ask, or discuss work in progress** | The **GordonOS Development** Facebook group is private — request to join: <https://www.facebook.com/groups/1486946993399861>. |

---

## Using AI

**AI assistance is welcome here, not frowned upon.** The convention set this repository
enforces — [`.github/copilot-instructions.md`](.github/copilot-instructions.md) — is
written for an AI assistant, and it is the same set the tree enforces on everyone.

The condition is the one this project asks of any change: **the machine has to be
understood.** Use AI to get there faster, not to skip it.

- **Know the C64 and its custom chips.** Excellent knowledge of the machine is expected of a
  contributor here, not a passing acquaintance: the 6510 and its I/O port, the VIC-II and
  the raster, the SID, the two CIAs, the memory map and the banking that swaps the ROMs
  out, the REU and its DMA. So which register a kernel call clobbers, what the VIC does at
  that raster line, what the CIA is counting — the answer comes from the reference
  (`docs/c64_1750_REU_programming_guide.md`, `docs/kernel.md`, `docs/lib-*.md`, the VIC-II
  article) or from a measurement on the machine, never from recall.
- **Know 6502 assembly, not just the syntax.** The addressing modes that exist — and the
  ones the assembler accepts anyway, as KickAssembler does when a `(zp),x` assembles
  silently as `zp,x`; which registers and flags each instruction really touches; how far
  a branch reaches; what a `.text` string actually encodes. Read the CPU and assembler
  references, and disassemble the emitted bytes (`tools/dis6502.py`) when it matters.
- **Own the change.** If you cannot explain why it is correct, or reproduce the check
  that says it is, it is not ready — prompted code and typed code alike. Declaring a
  tool is not required; understanding the result is.

---

## Setting up

The full environment — every prerequisite with per-OS install commands, the three
build stages and the tool paths — is **[docs/development.md](docs/development.md)**
and **[docs/quick-start.md](docs/quick-start.md)** → *Option 2: Build from source*.

The short version — `git clone https://github.com/GordonOS/gordon-os.git`, open the
folder in VS Code once (VS64 generates `build/build.ninja`), then:

1. VS Code + the **VS64** extension (it generates `build/build.ninja`, which every
   kernel build needs), a JDK (8+; tested with 26), **KickAssembler 5.25**,
   **VICE 3.9+**, Ninja, and PowerShell 7 — or Windows PowerShell 5.1 on
   Windows.
2. `cp .vscode/settings.json.sample .vscode/settings.json` and edit the four
   `vs64.*` paths. Without it the first build stops with `KickAss.jar not found at:`
   (an empty path — the settings file is missing, not the assembler).
3. `pwsh tools/build-kernel.ps1` → `build/gordon-os.prg`.
   `pwsh tools/build-tasks.ps1` → `tasks-out/*.bin`, then `pwsh tools/build-reu.ps1`
   seeds them into `reu/REU.bin`.

In VS Code, **Build & Launch with fresh REU** does the whole chain and starts VICE.

> **The REU image is a tracked build output and it changes on every build** (it
> carries a build stamp). Expect a 16 MB binary diff after a rebuild; it is not a
> mistake. `pwsh tools/check-reu-current.ps1` answers "why is my change not in the
> emulator?" — it compares `tasks-out/*.bin` against what the image holds.

---

## Before you open a pull request

Run the gates. They are the cheap half of the build (no assembler, no emulator) and
they are what CI runs:

```bash
pwsh -NoProfile -File tools/check-fs-offsets.ps1        # REU-FS 24-bit offset arithmetic
pwsh -NoProfile -File tools/check-lib-ids.ps1           # LIB_* ids vs the descriptor tables
pwsh -NoProfile -File tools/check-header-offsets.ps1    # header fields read as HDR_* constants
pwsh -NoProfile -File tools/gen-task-asm.ps1 -Check     # the generated task asm is current
pwsh -NoProfile -File tools/verify-jumptable-docs.ps1   # every entry documented, addresses agree
pwsh -NoProfile -File tools/verify-inventory-docs.ps1   # the two call inventories agree with the tree
python3 tools/check-ascii.py                            # Rule 19
python3 tools/check-links.py                            # relative links in the docs
```

Then build what you changed: `tools/build-kernel.ps1` for the kernel,
`tools/build-tasks.ps1` for a task, `tools/build-reu.ps1` to seed the image. A
failed `.assert` **must** fail the build — KickAssembler exits 0 on one, so every
invocation goes through `tools/kick.ps1`; use `tools/build-kernel.ps1` rather than
raw Ninja.

If you touched kernel memory, `pwsh tools/verify-map.ps1` checks the free-run totals
against `docs/memory-map.md`. If you found and fixed a bug, add it to `docs/bugs.md`
with its root cause and a transferable rule.

---

## Conventions

**The authoritative list is [`.github/copilot-instructions.md`](.github/copilot-instructions.md).**
It is written as instructions for the AI assistant, but it is the convention set this
repository actually enforces — the ZP/IRQ rules, the register clobber table, the
branch-distance rule, the loader and pool rules, and the documentation rules below.
Read it before touching the kernel. The headline rules:

- **Comments and docs carry no history.** A comment states what is true now; the
  reason a constraint exists stays, the story of the change does not. No bug
  numbers, dates, plan phases or "used to be" — those belong in `docs/bugs.md`.
  `tools/scan-history.py` inventories violations of this rule.
- **Code comments are plain 7-bit ASCII** (Rule 19). Typographic characters live in
  `.md` files only, and never in a `.text` string — there it is emitted data and a
  wrong screen code. `tools/check-ascii.py` enforces it.
- **The register clobber table is the contract.** A kernel call that clobbers X
  clobbers X for *every* caller; read the routine's own source before holding a
  value in a register across it.
- **`.assert` the invariant you rely on.** The tree pins addresses, sizes, offsets
  and counts with asserts because a comment cannot fail a build.
- **Reuse before you write.** A new kernel routine should compose the existing
  granular ones; if a kernel call does the job, use it.
- **This is a multitasking OS.** Anything you add must be safe against a ~60 Hz
  preemption and must not leave state another task can inherit.

Keep a change narrow, explain what the machine does differently, and say which
gates you ran in the pull request.

---

## Reporting a bug

The most useful report says **what you ran** (`run grpaint dragon`, a shell command,
an F-key switch), **where** (VICE version, `-speed`, REU image, or real hardware —
C64 Ultimate, 1541 Ultimate, plain C64 + 1764) and **what you saw** versus what you
expected. If a task hangs or the screen garbles, note whether the debug trace was
needed. Screenshots help enormously; a save-state is even better.

---

## Licensing of contributions

GordonOS's original code is **GPL-2.0** (see [LICENSE](LICENSE)). By contributing
you agree your contribution is licensed under the same terms. Note that the shipped
binaries also contain third-party components with narrower terms — EhBASIC
(non-commercial) and the COMPUTE!'s Enhanced SID Player (no explicit licence) — so
**do not add code that is incompatible with those, and do not relicense vendored
code**. The component list is in [NOTICE](NOTICE) and [docs/LICENSE.md](docs/LICENSE.md).
