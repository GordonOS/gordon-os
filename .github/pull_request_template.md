<!-- Thanks for the change. Keep it narrow: one behaviour per pull request. -->

## What this changes

<!-- one paragraph: what the machine does differently now -->

## Why

<!-- the bug or the need; link the issue. If it fixes a bug that was not already
     documented, add it to docs/bugs.md with its root cause and a transferable rule. -->

## Machine-visible effect

<!-- a before/after, a size delta (`grpaint` 7729 -> 7743 bytes), a screenshot,
     a save-state, or the gate output -->

## Checklist

- [ ] The source-only gates pass (paste the output, or list them):
      `check-fs-offsets.ps1` · `check-lib-ids.ps1` · `check-header-offsets.ps1` ·
      `gen-task-asm.ps1 -Check` · `verify-jumptable-docs.ps1` ·
      `verify-inventory-docs.ps1` · `check-ascii.py`
- [ ] What changed was built (`build-kernel.ps1` / `build-tasks.ps1` / `build-reu.ps1`)
      and, if it runs, tried on the machine
- [ ] `docs/` updated where the change alters behaviour, a number or a documented fact
- [ ] No history in comments (Rule 14): no bug numbers, dates, plan steps, "used to"
- [ ] Comments are plain 7-bit ASCII (Rule 19); no non-ASCII inside a `.text` string
- [ ] A fixed bug has its `docs/bugs.md` entry, with a transferable rule
