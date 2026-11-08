# `music/` — the tunes the image ships

Every `*.mus` here is seeded into the REU filesystem by `tools/build-reu.ps1`, so adding a
file is enough to make it playable (`run sidplay <name>`, or `run musedit <name>`).

Nine of them are SID-Wizard tunes from the CGSC library.

Two more are a matched pair kept for the editor's voice-length check (F9 in
`../docs/musedit-review.md`), and are temporary — remove them once the check has been seen:

- **`crossmy.mus`** — `Cross_my_Heart_B`, a well-formed tune. Opens and plays.
- **`badlen.mus`** — `Cross_my_Heart_A`, the *same tune*, damaged: voices 1 and 2 are byte-identical
  to `crossmy`'s, but voice 3 is 851 bytes where its content needs an even count, so the `01 4F`
  terminator straddles a pair boundary and a pair-aligned walk never reaches it. `musedit` now
  refuses it at open with `? not a tune`; before the check it drew six impossible commands and the
  player stopped with an illegal duration.

The pair is here so the difference is visible side by side: `run musedit crossmy` opens,
`run musedit badlen` answers `? not a tune`. Of 18,613 `.mus` files measured on this machine
(16,601 in CGSC v147 and 2,012 elsewhere), exactly one had an odd voice length, and it was this one.
