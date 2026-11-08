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
DUR 16 TIE PLY RST INS DEL CMD UND SAV Q
```

**Row 0** — the file name, the tempo in force **at the cursor's row** (in decimal), the bar and the
row under the cursor, and the **pitch readout** — the note under the pointer, or the note at the
cursor. The bar and the row are what the playhead moves, so during playback they say where the
music is. The tempo figure is the one a note entered here would be measured against, so a tune
that changes tempo shows the change as the cursor crosses it.

**Row 1** — the three lane headers, and at the right the note-entry mode: `OVR` or `INS`. Those
three cells are a button: the pointer reverses them while it is on them, and a click toggles the
mode — the `M` key by another route. The readout is the button, so what it says and what the next
note entry does cannot disagree.

**Rows 2-20** — the grid, 19 rows. Each row is one **step** of the tempo in force there — the
shortest ordinary note that tempo allows — so a whole note (one 4/4 bar) is 8, 16, 32 or 64 rows
depending on the tempo. A `TEM` part-way through a tune starts a new bar and re-scales the rows
from that row on, so the rows a bar has can differ between one part of a tune and another; the row
numbers in the left column are counted from the bar's own first row. **This is the only part of
the screen that scrolls.**

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

**Row 24** — the control bar: `DUR` with the armed duration and the dot cell, `TIE` the tie
toggle, `PLY` play, `RST`, `INS`, `DEL`, `CMD`, `UND`, `SAV` and `Q`. The labels are short to keep
every item on one row; each does the work of the key it names. Every one of them answers the mouse:

| cell | what it does |
|---|---|
| `DUR` | steps the armed length on through whole, half, quarter, 8th, 16th, 32nd and 64th; the dot or triplet mark rides along, so only the class changes |
| `TIE` | arms or drops the tie, and stays reversed while it is set — the `,` key |
| `PLY` | plays and stops, and stays reversed for as long as the tune plays |
| `RST` | turns the event on the cursor's row into a rest — the `INST/DEL` key |
| `INS` | opens one row of silence in all three voices — the `I` key |
| `DEL` | removes the command on the line, else the event, and closes the gap — `SHIFT+INST/DEL` |
| `CMD` | authors the row's command line, the `C` key |
| `UND` | undoes the last action, the `Z` key |
| `SAV` | saves, `SHIFT+S` |
| `Q` | quits, and answers `? unsaved - q again` first if there are changes |

`RST`, `INS` and `DEL` are what make an editing session possible without the keyboard: with the
piano below for pitches and the grid for the cursor, entering notes, inserting time, changing one,
resting or deleting it, undoing and saving all happen from the pointer alone. `1`-`9` (arming a
specific length), `.` (the dots), `X` (redo) and `SHIFT+I` (insert a whole bar) are still
keyboard-only.

The tie is the duration byte's bit 6: the player holds the note into the one after it, so it
joins two notes into one sound. It applies to a note that is *newly written* — a pitch typed over
a rest or into a gap — and it stays armed, like the length it rides with, until you drop it. A
rest never carries a tie, and entering a rest clears the bit from a note that had one.

The bar answers the mouse: the item under the pointer shows in reverse video and goes back to
plain when the pointer leaves it, and `PLAY` stays reversed for as long as the tune plays, so the
bar always says where the pointer is and what is running.

Inside a lane:

| mark | meaning |
|---|---|
| `C-4 16` | a note starts here, and its duration is a sixteenth |
| `C-4 16.` | ...dotted; `:` is double-dotted and `*` is a triplet |
| `C-4 16. TIE` | the tie flag is set on this event, so the note carries on from the one before it instead of being struck again |
| `C-4 16  ?` | the note's onset falls between two rows, so the row it is drawn on is not where it starts |
| `... 4` | a rest starts here, lasting a quarter |
| `DEF 3` | a command, on a line of its own (see below) |

The duration is written in the format's own numbers on the note's own row: a whole note, a half,
a quarter, an 8th, a 16th, a 32nd and a 64th read `1`, `2`, `4`, `8`, `16`, `32`, `64`, and a
utility length prints no number because its length is whatever `UTL`/`UTV` set.

A dot or a triplet is a **mark right after that number**, the same three the control bar's dot
cell uses: `.` dotted, `:` double-dotted, `*` triplet. Nothing is printed for a plain note, so
`8` is an eighth and `8.` is a dotted eighth — read them as two different lengths, because that is
what they are. All three come from the duration byte itself (bit 5 the dot, bit 7 the triplet, or
the second dot with bit 5), so the mark is the format's own information and not a note the editor
keeps on the side. A note whose onset did not land on a row takes no mark: its lane ends in the
`?` instead, which is the more important thing about it.

`TIE` comes straight after the duration, and it reads the format's own tie bit (bit 6 of the
event's duration byte) rather than anything the editor keeps. A note too long for a single event
is written as several events, the first carrying the attack and every one after it carrying the
tie:

```
C-4 16
C-4 16 TIE
C-4 16 TIE
```

which is one note, struck once and held for three events' worth of time.

Only the row a note **starts** on is written. The rows the note covers stay blank — the pitch and
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
      |ATK 1      |ATK 1      |ATK 0      |
      |SUS 12     |SUS 11     |SUS 10     |
      |F-C 254    |P-W 1500   |P-W 1800   |
      |TEM 128    |HED 7      |HED 15     |
      |           |           |DEF 0      |
 01   |C-4 16     |... 16     |F#2 8      |
 02   |           |           |           |
```

A line carries **one command from each lane**, exactly as a row line carries one note from each, so
three voices that fire a command on the same row take one line and not three. A voice with more
commands than the others carries on down the lines below, with the other lanes empty.

**A command's mnemonic is drawn in reverse video** — a bar in the voice's own colour, exactly as
wide as the letters and with those letters knocked out — and so is the `ROW VOICE 1 VOICE 2 VOICE 3`
header row. So a bar is a command, and plain text is a note or a command's operand, which follows its
mnemonic one space later. (A task that is not the displayed one draws plain text, because reversed
cells are written straight to the framebuffer; the bars come back with the next redraw.)

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
over it to the row's own line, scrolling as many lines as it takes to get there. For the same
reason a line can be written, changed and removed without moving a single row — see
*Writing a command line* below.

### The keys

| action | how |
|---|---|
| place the cursor | click a cell in the grid |
| move one row | cursor up / down; at the bottom the window scrolls a line, at the top it moves back a whole page |
| change voice | cursor left / right, or click a cell in another lane |
| jump to the start of the voice | `CLR/HOME` |

The playhead follows playback and the window follows the playhead. **While the tune is playing
the cursor is the playhead**, so `CRSR-UP`/`CRSR-DOWN` are refused — the follow would drag the
marker straight back. `CRSR-LEFT`/`CRSR-RIGHT` still work, and are the useful pair while playing:
they move the dots to another voice, so you can see which note that voice is sounding. Editing
does not work while the tune is playing either — stop first (`? stop first`).

The cursor marker `>` is drawn in the free cell immediately **left of the lane the cursor is
in** — column 3, 15 or 27 — so it says which voice a key will act on as well as which line you
are on.

## Entering notes

The piano at the bottom of the screen is how notes go in.

1. **Click a cell in the grid.** The cursor goes there; row 0 shows the row and voice.
2. **Point at a key.** Row 0 names the pitch under the pointer (`C-4`, `C#4`) and the key is
   highlighted, so you always know what a click will enter.
3. **Click.** With the cursor on a row where a note starts, the event's pitch becomes the
   key's and its length is kept, so nothing below it moves. A rest is the exception: it has
   no length worth keeping, so the new note takes the armed duration. Inside a longer note the
   note is split; past the last event (or in an empty voice) rests are filled in so the note
   lands on the cursor's row — see *Editing* below.

**A note key also moves the cursor on.** In overwrite mode (`OVR`, the default) each note you type
from the keyboard leaves the cursor on the next row, so a run of notes is played in one go instead
of moving the cursor yourself between them. It is still overwrite: a note typed on a row that
already holds one replaces it, and nothing below is pushed down. A row's **command** lines are
passed over on the way — a command takes no time, so the next note belongs on the row's own line
below them — and the window scrolls a line at a time once the cursor reaches the bottom of the band.
Clicking a key on the piano does **not** move the cursor, because a click is a deliberate "make
*this* cell this pitch". Insert mode (`INS`) has its own rule and is unchanged: the note goes after
the event the cursor is on, and the cursor follows the note it wrote.

**The pitch sounds as you point at it.** Hovering a key on the keyboard — or pressing a note key —
plays it for a tenth of a second, so a tune can be found by ear before anything is written. A click
still just enters the note. It is a bare sawtooth, not the instrument the tune gives that voice — it
answers "is this the note I meant", nothing more. The pitch is the same one the tune would sound:
the editor applies the machine's declared speed (`speed 200` for the shipped VICE image) exactly as
the player does, so the audition and playback agree on what `C-4` is. While the pointer is on the
keyboard a dot in the
cursor voice's own colour sits on the key it is over, the same dot that voice gets on that key during
playback: it says which voice a click would write to, and it comes back in the other colour as soon
as the cursor moves to another voice. Move the pointer off the keyboard and the dot goes. The editor
has no player of its own while stopped; if another task's music is running the keyboard stays silent
rather than cutting into it — and the dot stays away, because the dots on the keys belong to whoever
is playing. During our own playback a click enters nothing (it answers `? stop first`), so there is
nothing to hear either way.

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

The armed duration applies to **every** note a pitch key writes — one entered where no note
starts, where a rest stands, inside a longer one, or over a note that is already there. Whatever
stood on that row keeps its pitch only if you leave it alone; typing a pitch re-times the event to
the armed value, so the rows below move to suit. Set the armed value with the number keys `1`–`7`
(whole … 64th), `8`/`9` (the utility lengths UTL/UTV), or by clicking the bar's `DUR`, which
steps through the seven ordinary lengths in that order. `.` cycles the dots (none, dotted,
double-dotted, triplet). The armed value is shown in the control bar's `DUR` field — the dot
cell shows `.` dotted, `:` double-dotted, `*` triplet — and a note already in the grid carries
the same mark beside its duration, so the lane says which of the three it is.

**The tie** joins a note to the one after it into a single sound, and it is armed with `,` or by
clicking `TIE` in the bar; both toggle the same flag, so the cell lights whether you armed it by
key or by pointer. It rides the armed value the same way the dots do, so it reaches every note a
pitch key writes — including one written into an empty row, where the length may be laid down as
two tied pairs of its own. The grid shows it as `TIE` after the duration, which is also how a
tied event already in a tune reads. A dotted, tied note reads `C-4 8. TIE` and is written as the
duration byte's bits 6 (tie) and 5 (dot) together.

Some notes do not land exactly on a row — a triplet or a double-dotted note can be shorter
than one step. Such an onset is drawn on the row it falls in, with `?` at the right edge.
`SHIFT+U` refines the grid until every onset has a row of its own: a half, a third, or a
single jiffy per row, taking only the steps that divide the tempo's own unit exactly (`arose` at
tempo 144 offers three — 16, 48 and 144 rows to the bar — because 9 jiffies does not halve). Each
press goes one step **finer** and, past the finest, wraps back round to the plain grid, so the
states cycle rather than stop. A tune that contains a triplet is already refined at the third when
it opens, which means its first press takes you to the finest and the second wraps to plain. Where
a later tempo cannot divide the chosen refinement, that part of the tune keeps the plain grid
rather than a broken one. Like
every other edit it needs the music stopped — the grid is drawn in the unit that was in force when
it was painted, so refining it mid-pass would leave the rows and the playhead apart, and it answers
`? stop first`.

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
| `,` | arm or drop the tie (the bar's `TIE` cell) |

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

A spelling that runs past the end of its octave is shown as the note it sounds, because that
is what the player makes of it: a note stored as a B sharp reads as the next octave's C, and
one stored as a flat C reads as the B of the octave below.

## Editing the timeline

The grid is time, and a note's length is the distance to the next note or rest in its voice.
That gives four behaviours worth knowing:

- **Entering on a note** replaces it. The rows below do not move.
- **Entering on a rest** replaces it and takes the armed duration, so the rows below come up by
  the difference.
- **Entering inside a longer event** splits it: the rows above your note keep the event's own
  pitch, the new note takes the armed duration and lands on the row you clicked, and what the
  event had left keeps that pitch again behind the note. A part that is not one legal length is
  written as several tied events. The three runs together last exactly as long as the event did,
  so the rest of the tune does not shift.
- **Entering where no duration reaches** fills the gap with rests first. Rests are ordinary
  events; you will see `...` where they were inserted.
- **Entering past the end of a voice** fills up to the cursor and inserts before the `HLT`.

To change a note's length, you do not edit the note — you move the next event. Insert a note or
a rest at the row where the note should end and the length follows. This is how the format
itself works, and it is why the grid never shows overlaps.

| key | action |
|---|---|
| `INST/DEL` | replace the note at the cursor with a **rest** of the same length |
| `SHIFT+INST/DEL` | **delete** the event and close the gap; on a command line it removes that command |
| `I` | **insert a row** at the cursor, pushing everything below down |
| `SHIFT+I` | **insert a bar** (the rows a bar has at the tempo in force where the cursor is) |
| `M` | **insert/overwrite mode** for entering notes |
| `C` | **write or edit the command** at the cursor (see *Commands*) |
| `SHIFT+D` | **follow a phrase**: from a `CALL` to its `DEF`, from a `DEF` to the next `CALL` |
| `Z` / `X` | undo / redo |

The `HLT` that ends a voice cannot be deleted; it can be pushed down by inserting before it.

### Following the structure

The command lines above a row are the tune's own control flow: a `CALL` plays a phrase whose
`DEF` sits somewhere else in the same voice, and `immig` has sixty-five of them, so reading one
half of a pair means finding the other.

Put the cursor on a command line and press **`SHIFT+D`**:

- on a **`CALL`** it moves the cursor to that phrase's **`DEF`** — the line that carries the
  definition, not the row's own numbered line;
- on a **`DEF`** it moves to the **next `CALL` of the same phrase below the cursor**, and once
  there is none below it wraps round to the first one in the tune — so pressing it again and
  again tours the call sites.

The window follows: the found row becomes the top of the grid, the line carrying the command is
brought into the band, and the head's `BAR`/`R` show where you are. Anything else on the line — an
`ATK`, a `TEM`, or a row's own numbered line — answers `? not a phrase` and moves nothing.

**`SHIFT+D` acts on the lane the cursor is in.** A line carries one command from each lane, so
the lane decides which one is followed — and the `>` marker in the gap left of the lane tells you
which lane that is. If the lane has no command on that line, the key answers `? not a phrase`:
move the cursor left or right into the lane that shows the command.

### Rows that belong to a phrase body

A `DEF` body is not a separate region of the grid: it plays **inline**, where the definition
stands, so each of its rows is drawn once, in order, with the `DEF` on the command line above the
first of them. What those rows share is that they are the *same bytes* every `CALL` of that
phrase jumps into — so an edit there is an edit in every call site.

The editor tells you when the cursor is on one: the status line says **`phrase 3 x5`** — the
phrase number and how many `CALL` sites play it, counted across all three voices (a body is
global, so any voice's `CALL` can play it). It says so again each time a note is entered there,
because that is the moment the change reaches every call site.

Moving **time** in or out of a body is refused instead (see *Inserting time*): the rows below
would shift inside the body, and the phrase would come back longer or shorter everywhere it is
played. Changing a pitch, a command or a duration affects every call site too, and is allowed.

### Inserting time

Everything you enter goes **on** the grid as it is. When you need more room — to put a note
where another one already sits, or to open a bar in the middle of a tune — insert instead:

- **`I` opens one row.** Everything below the cursor moves down one row, in **all three
  voices**, so the voices stay lined up. A note that is already sounding is cut on the cursor's
  row; it resumes on the same pitch after the gap with a fresh attack.
- **`SHIFT+I` opens a whole bar** — 8, 16, 32 or 64 rows, whichever a bar is at the tempo in
  force where the cursor is (a tune that changes tempo has bars of different lengths, and the
  bar inserted is the one that fits the part you are in). Inserting a bar mid-tune therefore
  leaves every bar number before it unchanged.
- **`M` switches note entry to insert** — as does a click on the `OVR`/`INS` readout at the right
  of row 1. With `INS` showing, entering a note writes it **after** the event the cursor's line
  holds, opening that note's length in **that voice only** so its later events move down, and the
  cursor follows the new note. The other two lanes keep exactly the events they had, so a voice
  you have not written to yet stays empty. Entering notes one after another therefore builds the
  tune forward, in the order you play them. `M` again returns to `OVR`, where entering a note
  replaces or splits.

Inserting **time** — `I`, `SHIFT+I`, `INS` — is the operation that touches all three voices: the
three lanes are one timeline, so opening rows in one lane alone would slide it against the others
with nothing to line it up again. Inserting a **note** in `INS` mode is that voice's own edit, and
leaves the other two untouched.

Either kind of insert is **refused** when the row belongs to a **phrase body**: `? phrase 3 x5`
names the phrase and how many `CALL` sites play it, and nothing is written. Those rows are the
shared definition, so "one extra row" here would be one extra row in every place the phrase is
played — and in the inline pass as well, because a definition plays on the way past.

`SHIFT+INST/DEL` (delete the event and close the gap) is refused for the same reason and with the
same message: it takes time *out* of the body. A **note** written into a body is allowed — that is
how you change the phrase everywhere at once — and the status line says `phrase 3 x5` as it goes,
so you know how many call sites you just changed.

The mode is only about the *next* note: switching it changes nothing in the file.

### Illegal durations

A duration the current tempo cannot express is refused with `? illegal duration`, and nothing is
written. The classic case is a 32nd or 64th at a moderate tempo, or a triplet at a tempo that
has no triplet table. Choose a longer value, or change the tempo first.

Tempo is **global**: a `TEM` entered in any voice changes the length of every later note in all
three voices. It is also a **point in the tune**, not a property of the file: a `TEM` at any row
takes effect there and holds until the next one, so a tune can change tempo as often as you like.

What the grid does about it: from the row a `TEM` sits on, the rows are the new tempo's own step
and a bar is one whole note counted in those rows, so **a bar's row count can change at a change** —
a tune at tempo 144 (16 rows to the bar) that moves to 192 has 64 rows to the bar from there on.
A change that lands between bar lines cuts that bar short, and a change on a bar line starts the
next one. The head's tempo figure, `SHIFT+I`'s "one bar" and the duration checks all follow the
row the cursor is on rather than the tune's opening tempo.

`TEM` is one register in the player, so two voices that set different tempos on the **same row**
do not run at two tempos: each lane's own note is measured with the value that lane set, and from
the next row the tempo is the **last** of them (voice 3's, since the player processes the voices in
order). Rows that become illegal because of a tempo change are flagged on the grid rather than
silently kept.

One corner a foreign file can carry: a note pair whose duration byte is exactly `$00`. The format
does not say what such a pair means — the reference notes that only the player decides — and the
player plays it as a 64th note, so the grid draws it as one and it is written back unchanged.

## Commands — envelopes, waveforms, vibrato, filter and the rest

Commands are the sound of the tune. `.mus` commands are 2-byte events placed **before the note
they affect**; they stay in force until changed. `musedit` groups them by the note they belong
to: every command that comes into force at a note's onset gets **a line of its own above that
note's row**, and the cursor passes over those lines on its way down.

A command pair takes no time of its own. It comes into force at the onset of the next note or
rest, which is why a command line only ever sits above a note or a rest: there is no place
"inside" a duration to put one, and the editor refuses a row in the middle of a note rather
than splitting it.

### Writing a command line

Put the cursor on a **note's own row** and press `C` (or click `CMD` on the control bar). The
head row becomes the prompt:

```
CMD TEM 120
```

| key | in the prompt |
|---|---|
| CRSR up / down | choose any of the 49 commands, in the format's own order |
| `RETURN` | take it, then finish its value — or write it straight away for `TAL`, `HLT`, `END` |
| digits | build the value in decimal, never past what that command takes |
| `INST/DEL` | back one digit out |
| `STOP` | cancel, and the grid comes back as it was |

The list runs from the tune's own settings — `TEM` `UTL` `UTV` `HED` `DEF` `CALL` `TAL` `HLT`
`END` — through every sound command the format defines, ending with `MS#`, `FLG` and
`AUX`. The pair is written at the boundary the cursor's row starts, so every row stays where it
is and nothing needs renumbering.

**Values.** The prompt takes the value in decimal, up to whatever that command allows: most take
0-255, the one-bit commands (`SNC`, `FLT`, `RNG`, `3-O`, `BMP`, `F-X`, `LFO`, `P&V`) take 0 or 1,
`WAV` and `F-M` 0-7, the envelope nibbles 0-15, `RUP`/`RDN` 0-31, and three take more than a byte
— `MS#` up to 999, `P-W` and `DTN` up to 4095, `POR` up to 16383. A value the command cannot
hold is clamped to its maximum rather than refused, so what the prompt shows is always what gets
written.

**Overwrite or insert** — the mode `M` shows decides what happens when that boundary already
carries the same command: `OVR` replaces it where it stands, `INS` adds a second pair.

**Editing and removing** — put the cursor on an existing command line: `C` opens the prompt on
that line, already filled in and ready to change, and `SHIFT+INST/DEL` removes that pair and
closes the gap. `Z` takes either back, like any other edit.

**Every** command the picker offers is writable, the sound ones included: `C` on a `P-W` line opens
the prompt on `P-W` with the operand that line already carries — `CMD P-W 1035` — and taking a new
value replaces that pair where it stands, so the line never carries two of them. The operands below
are timing and waveform numbers that belong to the pattern rather than to a line somebody types, so
they read and write as plain counts; the reference below says what the player does with each of them.

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
  cannot reload, so the pair is decoded, the option byte is consumed, and nothing follows from it.
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
because the line above the window has not been walked yet. Either way the window remembers which
of a row's lines it is standing on, not just the row: a repaint for an edit, an undo or a change
of row unit puts the score back exactly as it was, and the cursor's row is read from the same
map the screen was drawn from.

The first release always plays from the start: the format has no seek command, and a voice's
stream can only be entered at its beginning or at a `HED`. Use sections to structure a long
tune.

The tune **loops only if it says so**: a section is a `HED` and the `TAL` that closes back to it, and
the player honours that inside a pass, so a tune written to repeat repeats by itself and one with no
section ends at its last row, where the music stops and the score rewinds to the beginning, cursor
and all, ready for the next `SPACE`. While a section loops, the score keeps **counting up**: the walk
executes the same `HED`/`TAL`, so each time round is walked as later rows with later clocks rather
than jumping back to the section's first row — the cursor keeps following the music, and the row
numbers keep climbing, with the bar number climbing with them.

Only one player can own the SID, and `run musedit` is refused up front while another task has
`sid.lib` loaded — so a busy chip never costs you the editor session. If the chip is taken
*while* you are editing (a task loaded from the shell), `SPACE` reports `? no player` for a
moment and play stays unavailable until `musedit` is restarted; the same message appears if
`sid.lib` cannot be loaded because the pool is full. Editing carries on either way.

## Saving and quitting

`SHIFT+S` (or `SAVE` on the control bar) saves to the same name. If the tune has grown beyond
the room the file was given, the editor recreates the file with room to spare; otherwise it
writes in place. A failed save reports `? no space` or `? dir full` and leaves the old file
untouched.

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
that split a longer one undoes as a single step, not one per event it wrote. A long run of recent
actions is held; opening a tune resets the history.

The history lives in a 2 KB file called `musedit.und`, made on the first run and reused from
then on, so `dir` shows it and it costs the same space however much you edit.

## Troubleshooting

| message | meaning |
|---|---|
| `? name - run musedit <name>` | no name given, or the base name is too long (`up to 7 characters` on the next line) |
| `? already running` | another `musedit` is resident |
| `? sid.lib in use` | another task has `sid.lib` loaded (a player is running) — printed by the shell before `musedit` starts; stop the player first |
| `? no space` | the pool cannot hold the tune's new size; nothing was changed |
| `? no windows` | the pool could not seat the three read windows, and the grid cannot be read without them — nothing was opened |
| `? no player` | the SID was taken by another task during playback, or `sid.lib` could not load |
| `? stop first` | an edit was attempted during playback |
| `? illegal duration` | the current tempo cannot express that note length |
| `? no event here` | the rest or delete key needs the cursor on a row where an event starts |
| `? hlt ends the voice` | `SHIFT+INST/DEL` on a `hlt` line: that pair is what ends the voice, so removing it would leave the voice unreadable — delete the events above it instead, or retype the line |
| `? phrase 3 x5` | an insert or a delete was attempted on a row inside phrase 3's `DEF` body, which 5 `CALL` sites play — move off it, or edit the phrase deliberately with a note |
| `? nothing to undo` | `Z` with no action behind it — a freshly opened tune, or everything already undone |
| `? nothing to redo` | `X` with nothing undone since the last edit |
| `? no definition` | `SHIFT+D` on a `call 3` line when the tune has no `def 3` at all |
| `? no call` | `SHIFT+D` on a `def 3` line that no `CALL` in the tune plays |
| `? not a phrase` | `SHIFT+D` on a line with no `CALL` or `DEF` in the cursor's lane — move into the lane that shows one (see *Following a phrase*) |
| `? no pointer art` | the mouse is on but the pointer sprites could not be resolved — the REU image is stale |
| `? no mouse - mouse 1351 first` | the system has no 1351: run `mouse 1351` from the shell, then start the editor again |
| `? no fs` | the REU image has no filesystem signature — the image that ships with the release always has one, so this one is from another build or has been wiped: put the shipped image back (`format` makes an empty filesystem, but the bundled tunes come back only with a fresh image) |
| `? no scratch` | the scratch copy `<name>.wrk` could not be opened or made — it is 255 pages, 65280 bytes, so the REU filesystem needs that much free — or the tune is larger than it can hold |
| `? dir full` | the REU filesystem's directory has no free record for a new file — delete a file you no longer need, then save again |
| `? write failed` | the REU filesystem refused a transfer: a save failed part way (the working file still holds the image, so save again), or a page of the working copy could not be moved, which means the edit on screen may not be in the file — save soon and check the result |
| `? not a valid .mus file` | the file is either too short to hold a tune header, or one of its voices does not end on its `01 4F` terminator — a stream of two-byte commands that ends anywhere else has lost or gained a byte, so every pair after that point would read as a command the tune never wrote |
| `? saved` | the tune was written — a report, not a refusal |
| `? unsaved - q again` | `Q` with edits that are not saved: `S` saves them, or `Q` again quits and discards them |

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

Two numbers bound what you can build, and neither is in the file:

- **A tune can grow to about 65 KB.** The editor works on a 65,280-byte scratch copy of the tune in
  the REU filesystem, and a fresh one takes that room when the tune is first opened, whatever the
  tune's size. That is fourteen times the largest bundled tune, so it is not a limit you will meet
  by writing, and the space it takes comes back on `compact` once you stop editing. `? no space`
  means that cap, not the pool.
- **A voice must end on its `01 4F` terminator.** Every voice is a stream of two-byte commands, so
  a `.mus` whose voice ends anywhere else has lost or gained a byte and cannot be read as the
  format says — the editor refuses it at open with `? not a valid .mus file` rather than draw
  commands the tune never wrote. All the bundled tunes are well formed; a file that is not is
  damaged or was written by something else.
