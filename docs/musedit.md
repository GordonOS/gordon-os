# `musedit` — the tracker-style `.mus` editor

`musedit` writes and edits COMPUTE!'s Enhanced Sidplayer tunes — the `.mus` files that
`sidplay`, `godzi`, `gortris` and `grknoid` play — on GordonOS, and auditions them through the
same player those tasks use.

The file is never converted. `musedit` opens the `.mus` image exactly as it is, works out when
each event plays, and shows that as a three-lane tracker grid. Every edit is written back as
the 2-byte event pairs the format already uses, so a tune opened and saved untouched is
byte-for-byte the file you started with, and any well-formed `.mus` can be opened.

> Status: this guide describes the first release. A raw event view, a sections view, description
> editing, block operations and instrument macros are not in it; see *Limits* at the end.

## Starting

`musedit` needs a **1351 mouse** in the system — a note is entered by clicking a key — so make
sure `mouse 1351` has been run (it is in `boot.bat`) before starting it. Without one the editor
refuses with `? no mouse - mouse 1351 first`.

From the shell:

```
run musedit commodo
```

opens `commodo.mus` from the REU filesystem. The base name is at most **7 characters**
(`.mus` is appended by the editor). A name that does not exist starts a **new tune**: three
voices containing only their `HLT`, an empty description, and the usual load address.

```
run musedit
run musedit toolongnm
```

are refused on a screen of their own with the usage line; press any key to return.

Only one `musedit` can run at a time.

`sid.lib` is single-use, so `run musedit` is refused with `? sid.lib in use` while
another task is playing (`sidplay`, `godzi`, `gortris`, `grknoid`). Stop the other player
first — `S` in `sidplay` stops the tune — and the editor starts normally.

## The screen

```
commodo.mus TEM 144 BAR3 R048      C-4
ROW VOICE 1     VOICE 2     VOICE 3   OVR
>01 C-4 16      A-3 8       ... 2
 02                         ... 4
 03             E-4 4       ... 4
 04 A-3 16                  ... 8
...
 19 ...         ...         ...
[rows 21-23: piano, all 8 octaves over 40 cells, black keys as two-row bars]
DUR 16  TIE-  PLAY  CMD  UNDO  SAVE  Q
```

**Row 0** — the file name, the tempo in force (in decimal), the bar and the row under the cursor,
and the **pitch readout** — the note under the pointer, or the note at the cursor. The bar and
the row are what the playhead moves, so during playback they say where the music is.

**Row 1** — the three lane headers, and at the right the note-entry mode: `OVR` or `INS`.

**Rows 2-20** — the grid, 19 rows. Each row is one **step** of the current tempo — the shortest
ordinary note the tempo allows — so a whole note (one 4/4 bar) is 8, 16, 32 or 64 rows
depending on the tempo. **This is the only part of the screen that scrolls.**

**Rows 21-23** — the piano: **all eight octaves**, C0 to B7, on one screen. The 56 white keys
share the 320-pixel width, so a key is about 5.7 pixels wide; the black keys are short bars on
the boundaries, two rows tall. The key under the pointer is highlighted.

While the music plays a **dot per voice** sits on the key that voice is sounding — **red** for
voice 1, **yellow** for voice 2 and **cyan** for voice 3, so a chord reads at a glance and you can
tell which voice is which. Each dot is placed from the pitch the grid walker recorded for the event
at the playhead, so it always agrees with the note shown in the lane; a voice that is resting has no
dot, and the dots go when the tune stops. A natural's dot sits on the white part of its key, below
the black keys; an accidental's sits up on the black key itself.

A lane is drawn in its own voice's colour — the same red, yellow and cyan — so a note and the dot
that follows it are one thing seen twice. The row number, the lane headers and the head stay white.

**Row 24** — the control bar: `DUR` with the armed duration and the dot cell, `TIE-` the tie
toggle, and the PLAY, CMD, UNDO, SAVE and Q buttons. PLAY, SAVE and Q are clickable; `DUR`,
`TIE-`, `CMD` and `UNDO` answer `? not yet` and are driven from the keys instead (`1`-`9` arm
the duration a **new** note is written with, `.` cycles the dots, `Z`/`X` undo and redo).

The bar answers the mouse: the item under the pointer shows in reverse video and goes back to
plain when the pointer leaves it, and `PLAY` stays reversed for as long as the tune plays, so the
bar always says where the pointer is and what is running.

Inside a lane:

| mark | meaning |
|---|---|
| `C-4 16` | a note starts here, and its duration is a sixteenth |
| `C-4 16  ?` | the note's onset falls between two rows, so the row it is drawn on is not where it starts |
| `... 4` | a rest starts here, lasting a quarter |
| `DEF 3` | a command, on a line of its own (see below) |

The duration is written in the format's own numbers on the note's own row: a whole note, a half,
a quarter, an 8th, a 16th, a 32nd and a 64th read `1`, `2`, `4`, `8`, `16`, `32`, `64`, and a
utility length prints no number because its length is whatever `UTL`/`UTV` set.

Only the row a note **starts** on is written. The rows the note covers stay blank ? the pitch and
the duration already say how long it lasts, and leaving them empty is what lets the window scroll
a line at a time by drawing nothing but the row that appears. So a lane that looks empty under a
note is that note still sounding; the dots on the keyboard are what show which pitch each voice is
holding.

## Moving around

### Every command has its own line

A command changes how a voice sounds from the row it sits on: `ATK`, `WAV`, `PNT`, `TEM`, `HED`
and the rest. Each one gets a line of its own, above the row it belongs to, so that everything a
row does is visible and nothing hides behind a mark:

```
      |ATK  $1    |ATK  $1    |ATK  $0    |
      |SUS  $C    |SUS  $B    |SUS  $A    |
      |F-C  $FE   |P-W  $5DC  |P-W  $708  |
      |TEM  $80   |HED  7     |HED  15    |
      |           |           |DEF  0     |
 01   |C-4 16     |... 16     |F#2 8      |
 02   |           |           |           |
```

A line carries **one command from each lane**, exactly as a row line carries one note from each, so
three voices that fire a command on the same row take one line and not three. A voice with more
commands than the others carries on down the lines below, with the other lanes empty.

The two kinds of command never share a line, because a tempo and an envelope step are not
comparable events:

- the **sound** commands — `ATK`, `WAV`, `P-S`, `P-W` and the rest — take the lines at the top of
  the block;
- the **structural** ones — `TEM`, `UTL`, `UTV`, `HED`, `TAL`, `DEF`, `CALL`, `END`, `HLT` — take
  the lines nearest the row, immediately above its own line.

| command | what its line says |
|---|---|
| `TEM 128` | the tempo |
| `UTL 12` / `UTV 3` | a utility length, for all voices or this one |
| `HED 7` | the head of a repeat section, seven times |
| `DEF 3` / `CALL 3` | a phrase definition, or a call to one |
| `TAL` / `HLT` / `END` | back to the head, this voice stops, the tune's end |
| `ATK 2` / `VOL 15` / `DCY 9` | an envelope step or a volume |
| `F-C 254` / `P-W 1850` / `POR 4095` | a filter cutoff, a pulse width, a portamento |

Every operand is a count, so every one prints in **decimal** — nothing on a command
line is hex.

Every command the player implements has a line, including `MS#` and `FLG`, which are the first
two bytes of a phrase rather than notes, and `AUX`, which changes nothing. The one exception is
`JIF`, whose value is not the option byte alone but the option times four plus the command byte's
top two bits, so the grid would have to show a 10-bit signed number to be truthful; no bundled
tune uses it, and a `JIF` is kept in the file untouched. A note never carries a command that has
no line anywhere.

A command line has **no row number**, because it is not a point in time — it belongs to the row
below it. And because it takes no time, playback does not stop on it: the cursor passes straight
over it to the row's own line, scrolling as many lines as it takes to get there.

### The keys

| action | how |
|---|---|
| place the cursor | click a cell in the grid |
| move one row | cursor up / down; at the bottom the window scrolls a line, at the top it moves back a whole page |
| change voice | cursor left / right, or click a cell in another lane |
| jump to the start of the voice | `CLR/HOME` |

The playhead follows playback and the window follows the playhead. Moving the cursor does not
stop playback, but **editing does not work while the tune is playing** — stop first
(`? stop first`).

## Entering notes

The piano at the bottom of the screen is how notes go in.

1. **Click a cell in the grid.** The cursor goes there; row 0 shows the row and voice.
2. **Point at a key.** Row 0 names the pitch under the pointer (`C-4`, `C#4`) and the key is
   highlighted, so you always know what a click will enter.
3. **Click.** With the cursor on a row where a note starts, the event's pitch becomes the
   key's and its length is kept, so nothing below it moves. Inside a longer note the note is
   split; past the last event (or in an empty voice) rests are filled in so the note lands on
   the cursor's row — see *Editing* below.

A command has a line of its own above the row it belongs to, and that line holds no note: with
the cursor on a command's line a note key is refused (`? no event here`), so an entry can never
rewrite the row below the one the cursor is on. Move down to the row's own line — the one with
the row number — and the note goes in there.

The keys are narrow — about 5.7 pixels per white key — because all eight octaves share the
320-pixel width. The black keys are short bars on the boundaries: C# between C and D, D#
between D and E, F# between F and G, G# between G and A, A# between A and B. The B–C boundary
has none — that is where one octave ends and the next begins. Pointing at a key names its
pitch in row 0, so even a four-pixel black key is never a guess.

Every pitch the format allows is on screen at once — there is nothing to page or scroll.

### Duration, dots and tie

The armed duration applies to a **new** note — one entered where no note starts, or inside a
longer one. An event that already starts on the row keeps its own length, so typing a pitch
over it never re-times the voice. Set the armed value with the number keys `1`–`7`
(whole … 64th) or `8`/`9` (the utility lengths UTL/UTV). `.` cycles the dots (none, dotted,
double-dotted, triplet). The armed value is shown in the control bar's `DUR` field — the dot
cell shows `.` dotted, `:` double-dotted, `3` triplet.

Some notes do not land exactly on a row — a triplet or a double-dotted note can be shorter
than one step. Such an onset is drawn on the row it falls in, with `?` at the right edge.
`SHIFT+U` refines the grid (half, third, or a single jiffy) until every onset has a row of
its own; the row numbering follows the new grid.

### The pointer is required

`musedit` needs a 1351 mouse: a note is entered by clicking a key, so without one there is no
editor to work in. With no mouse in the system the editor refuses with `? no mouse - mouse
1351 first`; run `mouse 1351` from the shell and start it again.

### From the computer keyboard

The piano can also be played from the computer keyboard, which is quicker once you know a
tune: the home row plays the white keys — `A S D F G H J` is one octave, `C` to `B`, and
`K L ;` continue into the next at `C`, `D`, `E` — while `W E T Y U O P` play the black keys
between them (`C# D# F# G# A#` and the next `C# D#`). The map starts at octave 4 and `-`/`+`
move it. A note key enters its pitch at the cursor exactly as a click does, and the pointer
still works alongside it.

### Octave and modifiers

| key | action |
|---|---|
| `-` / `+` | octave for the computer-keyboard note map (0 to 7) |
| `1` … `7` | duration: whole, half, quarter, 8th, 16th, 32nd, 64th |
| `8` / `9` | duration: the UTL / UTV utility length |
| `.` | cycle the dot state: none → dotted → double-dotted → triplet |

The armed values are shown on the control bar and stay armed until you change them. The
editor starts with a plain 16th armed.

**Triplets** are only available at tempos that support them (the entry is refused with
`? illegal duration` elsewhere). **Dotted** and **double-dotted** lengths are refused when the
current tempo cannot express them as whole jiffies — a dotted note needs an even base length,
a double-dotted one a base divisible by four.

### Utility lengths

`8` arms the **UTL** length and `9` the **UTV** length. A note entered with one of these takes
its duration from the `UTL` command (all voices) or the `UTV` command (this voice) instead of
from its own duration bits — useful for a delayed or gated voice. Both forms round-trip as
ordinary notes.

### Sharps and flats

A note key enters the pitch with a **sharp** accidental where one is needed (`C#4`). The
format can also store the same pitch as a flat (`Db4`); the command panel's note fields let
you inspect the accidental, but the first release always enters sharps.

## Editing the timeline

The grid is time, and a note's length is the distance to the next note or rest in its voice.
That gives four behaviours worth knowing:

- **Entering on a note** replaces it. The rows below do not move.
- **Entering inside a longer note** splits it: the long note is shortened to end where your new
  note begins, and the new note lands on the row you clicked. The rest of the tune does not
  shift.
- **Entering where no duration reaches** fills the gap with rests first. Rests are ordinary
  events; you will see `...` where they were inserted.
- **Entering past the end of a voice** fills up to the cursor and inserts before the `HLT`.

To change a note's length, you do not edit the note — you move the next event. Insert a note or
a rest at the row where the note should end and the length follows. This is how the format
itself works, and it is why the grid never shows overlaps.

| key | action |
|---|---|
| `INST/DEL` | replace the note at the cursor with a **rest** of the same length |
| `SHIFT+INST/DEL` | **delete** the event and close the gap |
| `I` | **insert a row** at the cursor, pushing everything below down |
| `SHIFT+I` | **insert a bar** (the rows a bar has at this tempo) |
| `M` | **insert/overwrite mode** for entering notes |
| `Z` / `X` | undo / redo |

The `HLT` that ends a voice cannot be deleted; it can be pushed down by inserting before it.

### Inserting time

Everything you enter goes **on** the grid as it is. When you need more room — to put a note
where another one already sits, or to open a bar in the middle of a tune — insert instead:

- **`I` opens one row.** Everything below the cursor moves down one row, in **all three
  voices**, so the voices stay lined up. A note that is already sounding is cut on the cursor's
  row; it resumes on the same pitch after the gap with a fresh attack.
- **`SHIFT+I` opens a whole bar** — 8, 16, 32 or 64 rows, whichever a bar is at the tempo in
  force. Inserting a bar mid-tune therefore leaves every bar number before it unchanged.
- **`M` switches note entry to insert.** With `INS` showing at the right of row 1, entering a
  note pushes the rows below down by that note's length instead of replacing them, so you can
  build a tune from the top down. `M` again returns to `OVR`, where entering a note replaces
  or splits.

An insert writes to all three voices at once — the three lanes are one timeline, so a per-lane
insert would slide them out of step with no way back. For the same reason an insert is
**refused with `? in a phrase`** when the cursor is on a row that belongs to a `CALL` block:
those rows are the shared definition, and inserting there would edit every place it is called.

The mode is only about the *next* note: switching it changes nothing in the file.

### Illegal durations

A duration the current tempo cannot express is refused with `? illegal duration`, and nothing is
written. The classic case is a 32nd or 64th at a moderate tempo, or a triplet at a tempo that
has no triplet table. Choose a longer value, or change the tempo first.

Tempo is **global**: a `TEM` entered in any voice changes the length of every later note in all
three voices. Rows that become illegal because of a tempo change are flagged on the grid
rather than silently kept.

## Commands — envelopes, waveforms, vibrato, filter and the rest

Commands are the sound of the tune. `.mus` commands are 2-byte events placed **before the note
they affect**; they stay in force until changed. `musedit` groups them by the note they belong
to.

Press `C` (or click `CMD` on the control bar) with the cursor on a note — or on the row where
you want a command — to open the command panel:

```
COMMANDS   V1   ROW 048   C-4 16
 1 ATK   9
 2 DCY   D
 3 SUS   9
 4 REL   6
 5 PNT   4
 6 HLD   0
 7 WAV   NOISE
 8 P-W   0800
 9 VDP   04
10 VRT   20
 [A]dd  [DEL] remove  [RET] edit  [Q] back
```

| key | action |
|---|---|
| click a row | select that command |
| up / down | choose a command |
| `RETURN` | edit its value |
| `A` | add a command (choose a group, then a command, then a value) |
| `INST/DEL` | remove the command |
| `Q` | back to the grid |

Values are typed as up to four hex digits or stepped with `-` and `+`; the panel shows the
decoded decimal or signed value beside each field. Setting a field to the value that means
"no effect" **removes** its pair, so a tune you did not change stays byte-identical.

### The groups

`TEMPO` `VOLUME` `REPEAT` `PHRASE` `ENVELOPE` `WAVE` `FREQ` `FILTER` `MOD` `MISC`

### Command reference

| group | name | value | what it does |
|---|---|---|---|
| TEMPO | `UTL` | 0-255 | length, in jiffies, of a note with the UTL duration |
| | `TEM` | tempo value | sets the tempo (the classic table: `$90` ≈ 100, `$A0` ≈ 90, `$F0` ≈ 60) |
| | `JIF` | -200…757 | jiffy-clock adjust — **no effect in GordonOS** (the scheduler is the clock) and not drawn on the grid |
| VOLUME | `VOL` | 0-15 | master volume |
| | `BMP` | up / down | volume ±1, or down a whole group |
| REPEAT | `HED` | 0-255 | start of a repeat section; the count, 0 = forever |
| | `TAL` | — | end of the section: jump back, or fall through when the count runs out |
| PHRASE | `CALL` | 0-23 | play a phrase definition |
| | `DEF` | 0-23 | define a phrase; the body follows and plays inline |
| | `END` | — | end of a phrase body |
| ENVELOPE | `ATK` `DCY` `SUS` `REL` | 0-15 | the ADSR nibbles |
| | `PNT` | 0-255 | release point: when in the note the release phase starts |
| | `HLD` | 0-255 | hold time before the release point is honoured |
| WAVE | `WAV` | 0-7 | waveform: 0 noise, 1 triangle, 2 sawtooth, 3 tri+saw, 4 pulse, 5 tri+pulse, 6 saw+pulse, 7 all three |
| | `P-W` | 0-4095 | pulse width |
| | `P-S` | -128…127 | pulse-width sweep, added every jiffy |
| | `PVD` | 0-127 | pulse-width vibrato depth |
| | `PVR` | 0-127 | pulse-width vibrato rate |
| | `SNC` | on / off | oscillator sync |
| | `RNG` | on / off | ring modulation |
| FREQ | `VDP` | 0-127 | vibrato depth |
| | `VRT` | 0-127 | vibrato rate |
| | `POR` | 0-16383 | portamento: slide into the next note |
| | `P&V` | on / off | portamento and vibrato together |
| | `DTN` | -2048…2047 | detune, in the player's frequency units |
| | `TPS` | -95…95 | transpose every later note, in semitones |
| | `RTP` | -47…47 | transpose relative to the previous note |
| FILTER | `F-M` | 0-7 | filter mode: 1 low-pass, 2 band-pass, 4 high-pass, combinable |
| | `F-C` | 0-255 | filter cutoff |
| | `F-S` | -128…127 | cutoff sweep, added every jiffy |
| | `AUT` | -128…127 | cutoff follows the note's pitch |
| | `RES` | 0-15 | resonance |
| | `FLT` | on / off | route this voice through the filter |
| | `F-X` | on / off | route the external input through the filter |
| MOD | `LFO` | tri / pulse | software LFO shape |
| | `RUP` `RDN` | 0-31 | LFO rate up / down |
| | `SRC` | 0-2 | modulation source: software LFO, oscillator 3, envelope 3 |
| | `DST` | 0-3 | destination: off, frequency, pulse width, cutoff |
| | `SCA` | -7…7 | modulation scale |
| | `MAX` | 0-255 | modulation maximum |
| MISC | `MS#` | 0-999 | measure number, written by the player and shown on its own line |
| | `UTV` | 0-255 | this voice's utility length (see *Utility lengths*) |
| | `FLG` | 0-255 | sets the player's flag byte |
| | `AUX` | 0-255 | reserved — **no effect** (the player's hook cannot be reached) |
| | `3-O` | on / off | switch voice 3 off (it is then free for sound effects) |
| | `HLT` | — | stop this voice |

Two commands are deliberately present and **do nothing in GordonOS**:

- **`JIF`** would rescale the jiffy; the player's jiffy is the scheduler tick, which a task
  cannot reload, so the value is stored and never used.
- **`AUX`** calls a function pointer that lives inside `sid.lib`'s own data, where no task can
  install a routine.

They are offered so that a tune written for the original player can be opened, inspected and
re-saved without losing them.

## Playing

`SPACE` (or `PLAY` on the control bar) starts the tune from the beginning and again stops it.
The tune plays through `sid.lib`, the same player `sidplay` uses, so what you hear is what will
ship.

The **cursor** is the playhead: while the music plays it moves down the grid with it, the window
scrolls a line at a time to keep it on the middle row, and the head spells out that row's bar and
row. It follows repeats (`HED`/`TAL`) back to the start of the section, and it shows the **call
site** when a phrase (`CALL`) is playing rather than the phrase body. When you stop, the cursor
stays where the music stopped — that is where editing resumes. Starting again always plays from
the tune's first row, so the score goes home with it: the window, the cursor and the head all
return to the top, and what you see is what you hear from the first note.

The player publishes the number of jiffies it has played in this pass, and a grid row is a fixed
number of jiffies at the tempo in force, so the cursor's row is that count divided by the row's
length. Nothing is predicted between events and nothing is re-anchored: a note that lasts 128 rows
simply stays on each row for its share of the count, and a loop restarts the count with the music.
A scroll down redraws only the newly revealed bottom line — the rows above are moved by the
kernel's REU screen scroll. Scrolling up moves the window back a whole page and repaints it,
because the line above the window has not been walked yet.

The first release always plays from the start: the format has no seek command, and a voice's
stream can only be entered at its beginning or at a `HED`. Use sections to structure a long
tune.

A pass is played once. When the tune reaches its last row the music stops by itself and the
score rewinds to the beginning, cursor and all, ready for the next `SPACE`. Sections still loop
within a pass (`HED`/`TAL`), and the cursor follows the rewind when they do.

Only one player can own the SID, and `run musedit` is refused up front while another task has
`sid.lib` loaded — so a busy chip never costs you the editor session. If the chip is taken
*while* you are editing (a task loaded from the shell), `SPACE` reports `? no player` for a
moment and play stays unavailable until `musedit` is restarted; the same message appears if
`sid.lib` cannot be loaded because the pool is full. Editing carries on either way.

## Saving and quitting

`SHIFT+S` (or `SAVE` on the control bar) saves to the same name. If the tune has grown beyond
the room the file was given, the editor recreates the file with room to spare; otherwise it
writes in place. A failed save reports `? no space` and leaves the old file untouched.

The description that ships inside the file — the text `sidplay` can print — is preserved
exactly, including any bytes after its terminator. The first release does not edit it.

`Q` quits. If there are unsaved changes it does not quit: the status line warns and you must
press `Q` again to discard them, or `SHIFT+S` to save.

While you edit, the tune you are working on lives in a scratch copy called `<name>.wrk`; the
`<name>.mus` itself is only touched by a save. The scratch is removed when you quit, so it does
not clutter `dir` — and because a deletion only tombstones a file's space, the REU filesystem
gives that space back when you run `compact`.

## Undo

`Z` undoes the last action and `X` redoes it. One action is one thing you did — entering a note
that split a longer one undoes as a single step, not three. A long run of recent actions is
held; opening a tune resets the history.

The history lives in a 2 KB file called `musedit.und`, made on the first run and reused from
then on, so `dir` shows it and it costs the same space however much you edit.

## Troubleshooting

| message | meaning |
|---|---|
| `? name - run musedit <name>` | no name given, or the base name is too long (`up to 7 characters` on the next line) |
| `? already running` | another `musedit` is resident |
| `? sid.lib in use` | another task has `sid.lib` loaded (a player is running) — printed by the shell before `musedit` starts; stop the player first |
| `? no space` | the pool cannot hold the tune's new size; nothing was changed |
| `? no player` | the SID was taken by another task during playback, or `sid.lib` could not load |
| `? stop first` | an edit was attempted during playback |
| `? illegal duration` | the current tempo cannot express that note length |
| `? no event here` | the rest or delete key needs the cursor on a row where an event starts |
| `? in a phrase` | an insert was attempted on a row that belongs to a `CALL` block — move off it first |
| `? no pointer art` | the mouse is on but the pointer sprites could not be resolved — the REU image is stale |
| `? no mouse - mouse 1351 first` | the system has no 1351: run `mouse 1351` from the shell, then start the editor again |

## Limits

The first release does **not** do these, by design:

- **No per-frame instrument tables.** The SID-Wizard-style waveform/pulse/filter programs,
  hard restart, per-track tempo and multispeed have no counterpart in this format. The nearest
  things are the sweeps and LFO commands listed above.
- **No play-from-cursor.** See *Playing*.
- **No delete row or delete bar.** Inserting opens time in all three voices, but the inverse —
  closing a row in all three at once — is not offered: `SHIFT+INST/DEL` deletes one event in one
  voice, and an insert you regret is a `Z`.
- **No description editing, no raw event view, no sections view, no block copy/paste, no
  instrument macros, no jam recording, no mute/solo.** The file is fully readable and
  editable without them.
- **No `.wds` words**, no printing, no playlists.
