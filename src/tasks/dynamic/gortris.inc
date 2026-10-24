// ==========================================
//  gortris.inc -- GORTRIS, the GordonOS Tetris (port of wiebow/tetris.c64)
// ==========================================
//  WHAT IS PORTED, AND WHAT IS NOT
//
//  The reference (reference/tetris/, MIT -- see its UPSTREAM.md) is a complete
//  char-mode game. Its game LOGIC is ported here, not reinvented:
//
//    * the piece set is its own data: blocks.asm's 4x4 frame tables, one run of
//      frames per piece, in its order and with its art (J T L S Z I O);
//    * rotation is its frame-range walk (SelectBlock sets first/last,
//      AnimateBlock steps inside them) -- there is no rotation arithmetic;
//    * the movement order is its: erase, move, test, put back if it does not
//      fit (BlockLeft/Right/RotateCW/CCW, DropBlock);
//    * the line sweep is its: CheckLines finds the full rows, FlashLines shows
//      them three times, RemoveLines shifts the field down and adds a blank
//      line at the top (lines.asm);
//    * the keys are its (input.asm): 'a' 's' turn, ENTER drops a
//      row faster, 'p' pauses -- plus 'q', because that is how a task leaves the
//      shell's world in GordonOS, and plus SPACE, the hard drop, which the
//      reference does not have at all (see gtHardDrop: down to the floor, settle
//      at once, and 2 a row for the distance against the step's 1), and plus the
//      move pair 'j' 'k', which the reference drives with ',' '.'.
//
//  The ARTWORK is ours, drawn with the kernel's glyph support. The reference's
//  screens are CharPad tile maps for a custom charset in char mode; they were
//  imported once (they are hires 1bpp, so each tile IS an 8-byte glyph and the
//  tile index is the glyph offset -- see reference/tetris/UPSTREAM.md for the
//  format notes) and then deliberately taken back out: this port wants its own
//  look, and the art is 4.5KB of a pinned task. So the field, the frame, the
//  panel labels and the text are ours below.
//
//  What necessarily changes in the logic is the SUBSTRATE, because the
//  reference reads and writes the C64 CHARACTER SCREEN and GordonOS is
//  all-bitmap:
//
//    * the board is 200 bytes in THIS IMAGE (gtBoard, 10x20; 0 = empty, else
//      piece+1) and is the authority. Collision is a board lookup, not a screen
//      read -- so the renderer never OCRs the display, and a thread child can
//      read the board directly;
//    * drawing is kPrintGlyphAt with an 8-byte glyph, one glyph per
//      cell, its colour from kSetTextColor. The block glyph has a 1px gutter on
//      its left and bottom edge, so neighbouring cells read as separate blocks
//      instead of one solid mass;
//    * the reference's per-piece charset tiles collapse to ONE block glyph,
//      because our glyph already is the whole cell (the reference used several
//      tiles per piece only to shade the faces). Its I-piece bar, which used
//      separate top/middle/bottom tiles, becomes four cells of that one glyph;
//    * the piece spawns CENTRED (box x = 3, so its cells land on board columns
//      4-5) instead of at the field's left edge.
//
//  THE SHELL AROUND THE GAME (the reference's modes)
//
//  The reference has five modes -- attract, level select, play, game over and
//  enter-name -- and this port has all five: the hi-score table and the name entry
//  live in the game-over path (gtOverLeave -> gtHiNameScreen) and the table is
//  shown on the select screen, exactly where the reference shows it. The name is
//  typed straight into the table's own seven-cell field, under a blinking block
//  cursor. What differs
//  is the storage: this port keeps the table in the REU FS via filesys.lib rather
//  than KERNAL LOAD/SAVE (see HI SCORES). Each mode is a byte in gtMode, and all of them
//  are driven from one pass per DISPLAYED frame:
//
//    ATTRACT  four screens on twice the reference's 254-frame delay (see
//             GT_ATTRACT_DELAY): title, credits, controls, and the level-select
//             screen as a preview. Any key except 'q' enters level select. The
//             reference waits for ENTER or fire there; its screens are artwork
//             we do not have yet (see the header note above), so "press any
//             key" is the honest version of that.
//    SELECT   levels 0-9 on the reference's own grid (its levelX/levelY: five
//             columns at 16,18,20,22,24 on rows 7 and 9), 'j' 'k' to choose or a
//             DIGIT to pick one outright, ENTER to start. The chosen digit flashes
//             on the reference's 18-frame FLASH_DELAY, digit/space, and the
//             previous digit is redrawn -- here by repainting all ten, which is ten
//             blits.
//    PLAY     the game itself, with the level shown on the panel.
//    OVER     the reference's game-over animation: the well fills with blocks a
//             row a displayed frame, is held full for ~3 s, empties the same way,
//             and then "game over" / "press key" are printed INSIDE the well
//             (see gtGameOver) -- after which any key returns to the attract loop.
//
//  THE LEVEL RULE IS THE REFERENCE'S (play.asm): 10 lines per level
//  (linesPerLevel), the fall delay drops by 4 per level (delayChange) from
//  DEFAULT_DROP_DELAY, and a line score is multiplied by level+1. Two
//  deliberate differences: the delay is RECOMPUTED from the level rather than
//  subtracted in place -- the reference's `sbc #4 / bcs` sequence lets a delay
//  of exactly 4 become 0 and then oscillate 0/4, and a floor of 4 is plainly
//  what it meant -- and the level the player chose is REMEMBERED for the next
//  game instead of being reset to 0, because a trip back through the title to
//  re-choose is a menu round trip, not a game.
//
//  INPUT. The keys: 'j' 'k' move (the reference drives that pair with ',' '.'),
//  'a' 's' turn, ENTER drops a row, 'p' pauses -- and this port adds SPACE (the
//  hard drop), 'q' (leave the task, which its world has no equivalent of) and
//  'm' (music on/off, which is not a move and so is handled for every screen).
//  It also reads the reference's SECOND input source, the joystick in port 2,
//  with its own mapping: UP is rotate-CCW
//  ("we have only one joystick button, so UP is used for rotate CCW"), DOWN is
//  the soft drop, LEFT/RIGHT move and FIRE is rotate-CW, which is also what
//  starts a game from the title and select screens. Untested -- see gtReadJoy,
//  including why the port has to be read through a DDRA window.
//
//  REPEAT is the reference's tuning, ported: a held key or joystick direction is
//  ignored for INPUTDELAY frames before it repeats, and its DOWN (our drop keys)
//  comes sooner -- its hardcoded 4, which its own comment calls INPUTDELAY / 2.
//  gtRepeat is the one number, adjustable on the SELECT screen with '-' and '+',
//  and gtSetRepeat derives the drop half from it, so the player's knob is the
//  only value to keep in step. The rule is gtInputFilter, and its timer is counted
//  in DISPLAYED frames (gtUpdate) whichever source the input came from.
//
//  MUSIC. sid.lib is called DIRECTLY -- no sidplay task is spawned -- from a
//  THREAD child of this task, because kSidPlay BLOCKS: it sleeps a tick per jiffy
//  inside its own loop, so the game loop cannot make that call. The two talk
//  through three bytes in this image: gtMusOn (1 = play, 0 = be silent),
//  gtMusQuit (leave the loop for good) and gtTuneLo/Hi (the tune's config block).
//
//    * the child declares ZP $02-$0A (kArg+6/+7 at spawn) -- sid.lib's own
//      scratch window, which its player expects to survive the yields in its
//      sleep loop. It has to be the CHILD's own declaration: the scheduler's ZP
//      save/restore is gated on the switching task's own region. The game itself
//      declares no ZP region and keeps every byte of state in this image.
//    * the tune is STREAMED, not buffered: gtLoadTune asks the REU FS where the
//      file is and kMallocs three 256-byte windows for the player to page it
//      into (gtTuneCfg + gtTuneBufLo/Hi), so the RAM cost is three pages
//      whatever the tune's length. Its
//      library hold is NOT given back the way
//      sidplay gives filesys.lib back -- the hi-score table has to be written, so
//      both declared libraries stay held for the session (see HI SCORES).
//    * stopping is kSidHush, not just gtMusOn = 0: the child is parked inside
//      kSidPlay's sleep loop and the hush is what breaks it out. Losing the
//      display does exactly that, and regaining it sets gtMusOn = 1 again (the
//      tune restarts from the top, which is what a restarted player does).
//      A second hush a slice later covers the sliver where the child has read
//      gtMusOn but not yet claimed the SID (see gtUnfocused).
//    * the child SHARES THIS CODE PAGE, so it must not outlive the game: sysExit
//      does NOT reap thread children (only `kill` cascades), so the exit path
//      sets gtMusQuit, hushes, and then REAPS -- it waits for the child's slot to
//      go free and kills it if it lingers (a `pause gortris.N` would otherwise
//      park it there forever). The tune block is freed only after that.
//      A `kill gortris` from the shell does cascade, but a killed SID player
//      leaves the chip holding its note: `kill` clears it for a task that
//      declared sid.lib, and `sidrst` does it by hand otherwise.
//    * THE REPEAT IS THE PLAYER'S, NOT THE FILE'S: kArg+1 = 0 tells kSidPlay to
//      start the tune again when it finishes. `tetris.mus` deliberately carries no
//      HED/TAL loop of its own -- with one, TAL jumps back INSIDE the stream, the
//      three voices never all halt, and the player never sees the song end, so
//      `run sidplay tetris` could never stop.
//    * THE PLAYER CAN MUTE IT: 'm', from every screen (gtKeyMusic). gtMusOff is
//      the preference and gtMusicStart REFUSES while it is set, so a focus regain
//      or a mode change cannot bring the music back behind the player's back; the
//      play screen's panel says "music off" while it is set.
//    * ONE TUNE for the whole session, though the reference switches three (see
//      gtMusicTune, where the plumbing for that is written and switched off).

// ==========================================
//  Geometry, keys, colours
// ==========================================
.const GT_W      = 10           // board columns
.const GT_H      = 20           // board rows
.const GT_COL    = 10           // screen column of board column 0
.const GT_ROW    = 3            // screen row of board row 0
.const GT_WALL_L = 9
.const GT_WALL_R = 20           // = GT_COL + GT_W: the right wall closes the field
.const GT_FLOOR  = 23           // = GT_ROW + GT_H
.const GT_PANEL  = 23           // side-panel text column
.const GT_PREV_C = 24           // "next" box top-left corner (screen coords)
.const GT_PREV_R = 10           //   The "next" LABEL is at row 8: at row 9 the box's
                                //   top cells draw hard against it, so the box sits
                                //   one row lower and leaves that row clear. Row 14
                                //   (GT_LVL_LBL_ROW) is the next thing below it.

//  Keys, as screen codes (kReadKey returns screen codes). The reference drives
//  the same six moves from its own table in input.asm: ',' '.' 'a' 's' ENTER 'p'.
//  ---- THE KERNEL'S KEY TABLE IS NOT PETSCII: RETURN is $81, not $0D -- $0D is
//  the letter M (see keyTable in src/kernel/keyboard.inc). $80+ are its special
//  codes: $80 DEL, $81 RETURN, $82 HOME, $83 STOP, $84 CRSR--, $86 CRSR--.
.const GT_K_LEFT  = $0a         // 'j'
.const GT_K_RIGHT = $0b         // 'k'
.const GT_K_CCW   = $01         // 'a'
.const GT_K_CW    = $13         // 's'
.const GT_K_DOWN  = $81         // RETURN -- one row down now
.const GT_K_DOWN2 = $84         // CRSR-- -- the same move, for muscle memory
.const GT_K_SPACE = $20         // SPACE -- the hard drop (this port's own key;
                                //   the reference has no such move)
.const GT_K_PAUSE = $10         // 'p'
.const GT_K_QUIT  = $11         // 'q'
.const GT_K_MUSIC = $0d         // 'm' -- music on/off. Not a game move, so it is
                                //   handled for EVERY screen (see gtKeyMode)
//  The select screen's repeat adjusters. The C64 has a '-' key and a '+' key of
//  its own, and the kernel's key table maps them to these two screen codes.
.const GT_K_MINUS = $2d         // '-'
.const GT_K_PLUS  = $2b         // '+'

.const GT_C_BG    = $00         // black: the field and the block gutters
.const GT_C_WALL  = $0b         // dark grey: walls, floor
.const GT_C_TEXT  = $0e         // light blue: panel labels, screen text
.const GT_C_FLASH = $01         // white: the completed-line flash
.const GT_C_TITLE = $01         // white: titles and the flashing level

//  The mode machine. One byte, one dispatch per displayed frame (see the header).
.const GT_MODE_ATTRACT = 0
.const GT_MODE_SELECT  = 1
.const GT_MODE_PLAY    = 2
.const GT_MODE_OVER    = 3

//  Timings, all in the game's OWN frames -- one step per displayed frame, which is
//  what a real C64 runs and what the C64U image declares -- never scheduler ticks.
//  gtPaceN scales them to a machine that declares itself faster.
//  ---- The reference's ATTRACT_DELAY is 254; this port shows each screen for four
//  times that (~20 s at the 50 Hz display): the screens are read, not glanced at,
//  and this constant is the one knob. It is counted in SIXTEEN bits (gtAtDelayLo/
//  Hi) because the value does not fit a byte -- and KickAssembler v5.25 SILENTLY
//  TRUNCATES an out-of-range immediate: `lda #1016` assembles as `lda #$F8`
//  (248 frames, ~5 s: *faster* than the reference) with no warning at all.
//  Use `#<`/`#>` for anything that can pass 255.
.const GT_ATTRACT_DELAY = 1016
.const GT_FLASH_DELAY   = 18    // the reference's FLASH_DELAY for the level digit
//  How many tunes the game loads and switches (see the MUSIC note in the header).
//  1 = the one tune it plays today. 3 = the reference's arrangement -- a title
//  tune, the game tune and a game-over tune -- which needs two more names in
//  `music/`, another nine-byte config and another three window pages each. The
//  plumbing is written either way.
.const GT_MUSIC_TUNES = 1
.const GT_TUNE_WIN_PAGES = 3    // the stream windows: one 256-byte page per voice

//  Which tune a mode wants (gtMusicTune). Only meaningful once GT_MUSIC_TUNES is
//  more than 1: with one tune loaded, every mode plays that one.
.const GT_TUNE_GAME  = 0
.const GT_TUNE_TITLE = 1
.const GT_TUNE_OVER  = 2
.const GT_MUSIC_REAP    = 60    // slices to wait for the music child to leave
                                //   before it is killed (~1 s at 60 Hz)

//  Music: sid.lib's kSidPlay arguments (lib-sid.md), plus the tune's FS name.
//  The delay is scheduler ticks slept per jiffy AT 100% -- 1, because the tune is
//  written at the tempo the game wants and one jiffy of music per jiffy plays it
//  as written. sid.lib scales it by the machine's speed, so the tempo is the same
//  in REAL time on every machine (see docs/lib-sid.md); a larger value here is a
//  deliberate slow-down.
.const GT_MUSIC_DELAY = 1

//  How long a HELD 'm' is ignored for, in displayed frames (~1 s at 50 fps).
//  Without it the input filter's normal cadence would toggle the music five times
//  a second, restarting the tune from the top each time (see gtKeyMusic).
.const GT_MUSIC_HELD  = 50

//  The reference's level rule (play.asm) -- its constants, its arithmetic.
.const GT_LINES_PER_LEVEL = 10  // linesPerLevel
.const GT_FALL_START = 70       // DEFAULT_DROP_DELAY, level 0, frames per row
.const GT_FALL_STEP  = 4        // delayChange: the delay drops this much a level

//  The reference's INPUT repeat tuning (input.asm): a held key or joystick
//  direction is ignored for INPUTDELAY frames, and its DOWN (our drop keys)
//  forces a shorter one -- its `ldx #4`, which its own comment calls
//  INPUTDELAY / 2. gtRepeat is adjustable on the select screen ('-' and '+'),
//  and gtRepeatFast is always half of it, floored at one: see gtInputFilter.
.const GT_REPEAT_DEFAULT = 10   // the reference's INPUTDELAY
.const GT_REPEAT_MIN     = 1
.const GT_REPEAT_MAX     = 30
.const GT_FALL_MIN   = 4        // --and never below this (see the header: the
                                //   reference's own floor is a wrap to 0)

//  The surface, in cells. The screens' frame closes all four sides, row 24 included:
//  every cell goes through kPrintGlyphAt (via gtBlitNow), a FIXED-cell write that
//  advances and wraps nothing. A cursor-carrying print could not reach that low --
//  writing the last cell of the last row wraps the cursor to row 25, and the kernel's
//  wrapClamp (src/kernel/scroll-region.inc) scrolls the whole scroll region when
//  `cursorRow > bottom`, AT THE WRAP, so no amount of re-positioning afterwards
//  undoes it: the frame's top edge would vanish while the bottom edge is written.
.const GT_SCREEN_COLS = 40
.const GT_FRAME_LAST  = 24      // the frame's bottom edge -- the screen's last row

//  Panel rows: what the chrome prints where. Every label+value PAIR IS ON ONE ROW
//  -- "score 001000", "lines 000", "level 00" -- with the value at GT_VAL_COL,
//  one space past the 5-character label. The level pair sits one row below the
//  preview box; the mute indicator and "paused" come below that.
//  The select screen's repeat row: the label the screen draws, and the column the
//  value is printed at (just past the label).
.const GT_REP_ROW     = 15
.const GT_REP_COL     = 24
.const GT_SCORE_ROW   = 2       // "score" -- gtPrintScore puts the value beside it
.const GT_LINES_ROW   = 5       // "lines"
.const GT_LVL_LBL_ROW = 15      // "level"
.const GT_VAL_COL     = 29      // the value column, shared by all three pairs
.const GT_LVL_ROW     = GT_LVL_LBL_ROW
.const GT_MUSIC_ROW   = 16      // "music off" while the music is muted
.const GT_PAUSED_ROW  = 17
//  The game-over text, printed INSIDE the well (see gtSayGameOver): two rows
//  around the middle of the field, at its left column. The well is 20 rows tall
//  (GT_ROW..GT_FLOOR-1), so row 12 is one above the middle and row 14 is the
//  prompt under it.
.const GT_OVER_TEXT_ROW = 12
.const GT_OVER_HINT_ROW = 14

//  The hi-score table (hi-scores.asm): three entries of seven name cells and three
//  BCD score bytes, most significant byte first -- the reference's own numbers.
//  The initial entries are house scores, not the reference's $01 fill: see gtHiTab
//  at the foot of the file.
.const GT_HI_ENTRIES  = 3       // its TABLE_ENTRIES
.const GT_HI_NAMELEN  = 7       // its TABLE_NAMELENGTH
.const GT_HI_ENTRYLEN = 10      // TABLE_NAMELENGTH + TABLE_SCORELENGTH (3)
.const GT_HI_LEN      = GT_HI_ENTRIES * GT_HI_ENTRYLEN
//  Where the table sits on the select screen (which is also the attract loop's
//  last screen): a heading, then one row an entry, then the message row under it.
//  Six digits + a space, so the name field starts seven cells in.
.const GT_HI_HDR_ROW  = 17
.const GT_HI_ROW0     = 19
.const GT_HI_MSG_ROW  = 22
.const GT_HI_COL      = 14
.const GT_HI_NAME_COL = GT_HI_COL + 7

//  The level-select grid: the reference's own levelX/levelY tables, five columns
//  on two rows (0-4 then 5-9).
.const GT_LEV_COL0 = 16
.const GT_LEV_COLSTEP = 2
.const GT_LEV_ROW0 = 7
.const GT_LEV_ROW1 = 9
.const GT_LEV_MAX  = 9          // the reference stops at 9; play can pass it
.const GT_DIGIT0    = $30       // screen code '0' (screencode_mixed digits)
.const GT_TEN        = 10       // the decimal radix, for the level display

//  The game-over animation's steps (gameover.asm): 0 fill the well, 1 clear it,
//  2 the text is up and a key leaves.
.const GT_OVER_TEXT = 2
//  How long the FILLED well is held before the clearing pass starts, in displayed
//  frames. The reference starts clearing on the very next update, which makes a
//  full well a one-frame flash -- it could afford that at one update per raster
//  frame. The hold is the port's own addition, asked for twice: the fill has to be
//  SEEN. There is no hold after the clearing pass, so the game-over text follows
//  the clear as it does in the reference.
.const GT_OVER_HOLD = 150       // ~3 s of a full well

// --- print a label: gtPut(col, row, string) ---
//  kSetCursor is the only kernel call that preserves X and Y, so the position
//  survives into gtLabel's kPrintString.
.macro gtPut(col, row, str) {
    ldx #col
    ldy #row
    lda #<str
    sta kPtr
    lda #>str
    sta kPtr+1
    jsr gtLabel
}

// ==========================================
//  gortris -- entry
// ==========================================
//  ---- THIS MUST BE THE FIRST EMITTED BYTE OF THE BODY. The header's code-size
//  field is `gortrisEnd - gortris`, and the loader copies exactly that many
//  bytes from file offset 256 -- so ANY data placed above this label is not
//  copied, is not covered by the size, and silently shifts every fixup offset
//  past the declared end (loader error 1, "? bad task file"). An earlier draft
//  kept the 7-byte colour table up here and the field came out 7 bytes short.
//  Constants and macros above this line are fine: they emit nothing.
gortris:
    lda #0                      // mode 0 = hires-text surface
    ldx #GT_C_BG
    stx kArg                    // border: black
    stx kArg+1                  // bg0: black -- block gutters and panel
    stx kArg+2
    stx kArg+3
    jsr kAllocScreen
    bcc gtHaveScreen
    jmp gtNoScreen              // Rule 4: gtNoScreen is out of branch range
gtHaveScreen:
    jsr kDisableCursor          // a game surface: no blinking text cursor
    lda #SYS_GETTID
    jsr syscall
    sta gtMe                    // our slot -- the per-frame focus compare needs it
    tax
    //  ---- TAKE THE DISPLAY WITHOUT BAKING IN THE OUTGOING BLINK BLOCK.
    //  kSwitchView copies the whole framebuffer into the outgoing task's REU
    //  slot, so a live cursor block drawn at that instant becomes permanent
    //  content there. kBlinkReset un-draws it, SEI-wrapped because an IRQ tick
    //  between the erase and the switch would draw it straight back -- and no
    //  syscall may sit in that window, since the dispatcher re-enables IRQs.
    php
    sei
    txa
    pha
    jsr kBlinkReset             // clobbers A, X, Y, kPtr, kArg+7
    pla
    tax
    jsr kSwitchView             // display it + take kbFocus
    plp

    //  The tune, and the child that plays it, are set up while we still own the
    //  display -- the load is a few frames of REU DMA, and the attract screen is
    //  drawn after it, so nothing is shown half-done.
    lda #GT_REPEAT_DEFAULT      // the reference's INPUTDELAY (adjustable on the
    sta gtRepeat                //   select screen), and its drop-key half
    jsr gtSetRepeat
    jsr gtHiLoad                // the saved hi-score table, if there is one
    jsr gtMusicInit
    //  The game opens on the attract loop, exactly as the reference does, so the
    //  player is told how to start before anything moves (and the level select
    //  gets its turn). gtStartAttract also asks the child for the music.
    jsr gtStartAttract

// ==========================================
//  gtLoop -- one pass per tick, one game step per DISPLAYED FRAME
// ==========================================
//  The scheduler tick is 59.2 Hz and the display samples at 50.1 Hz, so a
//  per-tick simulation is sampled 1.18 times per frame: some frames show two
//  steps, the next none, and it reads as a stutter. frameCount is incremented
//  by the kernel's raster commit at line 0, so it is the display's own clock.
gtLoop:
    lda #SYS_GETTID
    jsr syscall
    cmp kbFocus
    beq gtLoopFocus             // Rule 4: gtUnfocused sits past the joystick
    jmp gtUnfocused             //   and repeat-filter routines, out of branch reach
gtLoopFocus:
    lda frameCount
    cmp gtFrameLast
    beq gtIdle                  // same frame as last time: nothing to do
    sta gtFrameLast             //
    //  One key per DISPLAYED frame, in every mode -- which is the rate the
    //  reference's raster-synced loop polls at (input.asm), and it keeps a key
    //  from being consumed twice in one frame's worth of game time.
    jsr kReadKey                // non-blocking: A = screen code, 0 = none
    bne gtHaveInput
    //  The keyboard came up empty, so look at the joystick -- the reference's own
    //  order (input.asm's GetInput). The codes it produces are the game's key
    //  codes, so the stick drives exactly the same dispatch the keys do.
    jsr gtReadJoy
    beq gtNoKey
gtHaveInput:
    jsr gtInputFilter           // the reference's repeat tuning (gtRepeat)
    beq gtNoKey
    jsr gtKeyMode
gtNoKey:
    jsr gtUpdate                // per-frame: the mode's own tick
gtIdle:
    lda #SYS_YIELD
    jsr syscall                 // never starve the scheduler
    jmp gtLoop

// ==========================================
//  gtKeyMode -- route a key to the mode that owns it
// ==========================================
//  'q' leaves from every screen -- the one key the reference does not have, and
//  the only way out of its port on a multitasking OS.
//  ---- Every target below is a jmp through an inverted branch (Rule 4): the mode
//  routines live past the attract screens, well outside a branch's reach.
//  A = the key (never 0 -- kReadKey's 0 is handled by the caller).
gtKeyMode:
    //  'q' LEAVES FROM EVERY SCREEN -- the title and the select screen included,
    //  not just play. It is checked here, before the mode is even looked at,
    //  because the mode branches below JMP out and none of them would come back
    //  (the title and select handlers only know their own keys, and the play
    //  dispatch below carries no 'q' of its own).
    cmp #GT_K_QUIT
    beq gtKeyQuitKey
    //  'm' TOGGLES THE MUSIC FROM EVERY SCREEN TOO. The tune is the game's, not a
    //  mode's, and the mute is a PLAYER PREFERENCE: gtMusicStart refuses while
    //  gtMusOff is set, which is what stops a focus regain (or a mode change,
    //  which also asks for music) from undoing the player's choice.
    cmp #GT_K_MUSIC
    beq gtKeyMusic
    ldy gtMode
    cpy #GT_MODE_PLAY
    bne gtKmNotPlay
    jmp gtKey                   // the game's own dispatch, below
gtKmNotPlay:
    cpy #GT_MODE_ATTRACT
    bne gtKmNotAttract
    jmp gtKeyAttract
gtKmNotAttract:
    cpy #GT_MODE_SELECT
    bne gtKmNotSelect
    jmp gtSelKey
gtKmNotSelect:
    //  GT_MODE_OVER: the animation has to finish first, because the reference
    //  ignores input in its other steps too -- the text is what asks for a key
    lda gtOverStep
    cmp #GT_OVER_TEXT
    bne gtKmSwallow
    jmp gtOverLeave             // a key: settle the hi-score table, then attract
gtKmSwallow:
    rts
gtKeyQuitKey:
    jmp gtExit

//  'm' -- music on/off. A HELD 'm' must not retrigger on the input filter's normal
//  cadence (that would toggle the tune five times a second, restarting it from the
//  top each time), so its timer is loaded long here. Any other key resets that
//  timer, so this throttles a HELD 'm' only -- see gtInputFilter.
gtKeyMusic:
    lda #GT_MUSIC_HELD
    jsr gtPaceN
    sta gtInCount
    lda gtMusOff
    eor #1
    sta gtMusOff
    jsr gtMusOffPaint
    lda gtMusOff
    beq gtMusBackOn
    jmp gtMusicStop             // muting: silence the chip AND stop the child
gtMusBackOn:
    jmp gtMusicStart            // unmuting: it plays the tune from the top
gtKeyAttract:
    jmp gtStartSelect

// ==========================================
//  JOYSTICK -- port 2, the reference's own source and mapping (UNTESTED)
// ==========================================
//  Ported from input.asm's GetJoyInput: the keyboard is read first and the stick
//  only when the keyboard had nothing, and the codes it produces are the game's
//  key codes, so one dispatch serves both. Its mapping is kept: UP is rotate
//  counter-clockwise ("we have only one joystick button, so UP is used for rotate
//  CCW"), DOWN is the soft drop, LEFT/RIGHT move, and FIRE is rotate-clockwise --
//  which is also what starts the game from the title and the select screen,
//  because those accept the same code.
//
//  WARNING: WHY THIS READS THE PORT THE WAY IT DOES. The stick's five lines are
//  PA0-PA4 of CIA1, and the kernel's keyboard scan WRITES $DC00 to drive the
//  matrix columns (scanKeyboardHw, src/kernel/keyboard.inc) -- so a plain
//  `lda $dc00` reads back the column the scan last left driven, not the stick.
//  The read is therefore: save DDRA ($DC02), set it to 0 so the port is an input,
//  read $DC00, put DDRA back -- all inside SEI, because a scan that ran inside
//  that window would drive nothing and see a phantom "no keys". The C64's pull-ups
//  make a released line read 1, so 0 = pressed.
//  UNTESTED: there is no joystick in the development setup. The keyboard path is
//  unaffected either way, because a stick with nothing held reads as no input.
gtReadJoy:
    php
    sei
    lda $dc02
    pha                         // DDRA: the kernel's scan needs it as output
    lda #0
    sta $dc02
    lda $dc00                   // now this is the stick: PA0-4, active low
    sta gtJoyRaw
    pla
    sta $dc02                   // restore DDRA before anything can scan again
    plp
    lda gtJoyRaw
    and #$1f                    // PA5-7 are not the stick
    cmp #$1f
    beq gtJoyNone               // nothing held
    lsr                         // bit 0: up
    bcs gtJoy1
    lda #GT_K_CCW
    rts
gtJoy1:
    lsr                         // bit 1: down
    bcs gtJoy2
    lda #GT_K_DOWN
    rts
gtJoy2:
    lsr                         // bit 2: left
    bcs gtJoy3
    lda #GT_K_LEFT
    rts
gtJoy3:
    lsr                         // bit 3: right
    bcs gtJoy4
    lda #GT_K_RIGHT
    rts
gtJoy4:
    lsr                         // bit 4: fire
    bcs gtJoyNone
    lda #GT_K_CW
    rts
gtJoyNone:
    lda #0
    rts

// ==========================================
//  gtInputFilter -- the reference's repeat tuning (input.asm)
// ==========================================
//  Its keyDelayCounter/previousKey and joyDelayCounter/previousJoy are one rule:
//  a DIFFERENT code is acted on at once, the SAME one is ignored until its timer
//  runs out, and its DOWN -- our drop keys -- loads the shorter timer. gtInCount
//  is counted down a frame at a time in gtUpdate, so the cadence is measured in
//  displayed frames whichever source the input came from, and '-' and '+' on the
//  select screen set gtRepeat. A stick held in one direction is a level, so it
//  repeats on exactly this rule, which is what the reference's joy delay did.
//
//  A = the code from a source, never 0 on entry. Returns the code to act on, or 0
//  to wait. Clobbers A, X, Y.
gtInputFilter:
    cmp gtInLast
    bne gtIfNew                 // a different action: it goes through now
    lda gtInCount
    beq gtIfAct                 // it has waited long enough: go again
    lda #0                      // still waiting: swallow it
    rts
gtIfNew:
    sta gtInLast
gtIfAct:
    lda gtInLast
    jsr gtIfWait                // Y = the wait this code wants
    tya
    jsr gtPaceN                 // ...for this machine
    sta gtInCount
    lda gtInLast
    rts

//  A = the code -> Y = the wait for it: the drop keys come faster (the
//  reference's INPUTDELAY / 2, ours gtRepeatFast).
gtIfWait:
    ldy gtRepeat
    cmp #GT_K_DOWN
    beq gtIfwFast
    cmp #GT_K_DOWN2
    beq gtIfwFast
    cmp #GT_K_SPACE
    beq gtIfwFast
    rts
gtIfwFast:
    ldy gtRepeatFast
    rts

//  gtSetRepeat -- gtRepeatFast = max(1, gtRepeat / 2): the reference's "the drop
//  key repeats sooner" rule, derived rather than hardcoded, so the one number the
//  player sets is all there is to keep in step.
gtSetRepeat:
    lda gtRepeat
    lsr                         // / 2
    bne gtSrStore
    lda #1                      // a repeat of 1 has no half: keep 1
gtSrStore:
    sta gtRepeatFast
    rts

// ==========================================
//  Pacing -- the game's own frames, scaled to the machine
// ==========================================
//  Every timing constant in this file is in the game's OWN frames: one game step
//  per displayed frame, which is what a real C64 runs and what the C64U image
//  declares (`speed 100`, see docs/quick-start.md). A machine that declares itself
//  FASTER shows more frames per real second, so it waits more of them for the same
//  real time; a slower one waits fewer. It is the rule sid.lib applies to a tune's
//  ticks-per-jiffy delay, for the same reason.
//  The count is a byte, so a fast machine saturates: 400% wants 4x a 70-frame fall
//  delay, which does not fit.
gtPaceN:
    ldy sysSpeedShift           // +1 = 200%, 0 = 100%, -1 = 50%
    beq gpRet                   // 100%: the game's own count
    bmi gpFewer                 // slower than real time: longer frames, fewer of them
gpMore:                         // faster: shorter frames, more of them
    asl
    bcs gpMax
    dey
    bne gpMore
    rts
gpMax:
    lda #255
    rts
gpFewer:
    lsr
    bne gpNext
    lda #1                      // never expire instantly: one frame
    rts
gpNext:
    iny                         // Y counts up from the negative shift
    bne gpFewer
gpRet:
    rts

//  gtPace16 -- the same for gtAtDelayLo/Hi, whose delay is over a byte. Sixteen
//  bits cannot overflow at any speed the command accepts.
gtPace16:
    ldy sysSpeedShift
    beq gp16Ret
    bmi gp16Fewer
gp16More:
    asl gtAtDelayLo
    rol gtAtDelayHi
    dey
    bne gp16More
    rts
gp16Fewer:
    lsr gtAtDelayHi
    ror gtAtDelayLo
    lda gtAtDelayHi
    ora gtAtDelayLo
    bne gp16Next
    inc gtAtDelayLo             // never 0: the screen must still advance
    rts
gp16Next:
    iny
    bne gp16Fewer
gp16Ret:
    rts

// ==========================================
//  gtUpdate -- one displayed frame of the current mode
// ==========================================
gtUpdate:
    lda gtInCount               // the repeat timer runs down a frame at a time,
    beq gtUpNoTick              //   whether or not an input arrives (gtInputFilter)
    dec gtInCount
gtUpNoTick:
    ldy gtMode
    cpy #GT_MODE_ATTRACT
    bne gtUpNotAt
    jmp gtAtTick
gtUpNotAt:
    cpy #GT_MODE_SELECT
    bne gtUpNotSel
    jmp gtSelTick
gtUpNotSel:
    cpy #GT_MODE_PLAY
    bne gtUpDone
    jmp gtStep                  // the game's own frame
gtUpDone:
    jmp gtOverTick              // GT_MODE_OVER: the game-over animation ticks

// ==========================================
//  gtUnfocused -- the display (and the keyboard) belong to another task
// ==========================================
//  This IS the pause the user asked for: the field stops, the game stays stopped
//  until the keyboard comes back, and on the losing TRANSITION the music is
//  stopped -- once, not per frame, so a paused game is not a poll loop. The
//  keystrokes typed while we are away go to whoever owns the keyboard, and the
//  kernel drains the ring on every switch, so nothing typed at the shell can be
//  replayed into the game on resume.
//
//  The "paused" label belongs to the PLAY screen: on the title and select
//  screens there is no panel to print it in, and those screens are frozen by the
//  frame gate anyway, so the missing seconds explain themselves.
gtUnfocused:
    lda gtPaused
    bne gtWaitFocus             // already handled the transition -- do not repeat
    lda #1
    sta gtPaused
    lda #0
    sta gtMusHush
    jsr gtMusicStop             // no music from a task nobody can see
    lda gtMode
    cmp #GT_MODE_PLAY
    bne gtWaitFocus
    jsr gtSayPaused
gtWaitFocus:
    //  ---- A SECOND HUSH. The child asks to play and then claims the SID inside
    //  kSidPlay; if the first hush landed in that sliver the claim happens
    //  AFTERWARDS and the tune would play on with gtMusOn already 0. One more
    //  hush, a slice later, catches it -- the child's next state is the idle loop.
    lda gtMusHush
    bne gtWFYield
    lda #1
    sta gtMusHush
    jsr kSidHush
gtWFYield:
    lda #SYS_YIELD
    jsr syscall
    lda #SYS_GETTID
    jsr syscall
    cmp kbFocus
    bne gtWaitFocus
    //  Back: resync the frame gate (no burst of steps) and give the player the
    //  full fall delay to look at the board before gravity resumes.
    lda frameCount
    sta gtFrameLast
    lda gtFallDelay
    sta gtFallTimer
    lda #0
    sta gtPaused
    lda gtMode
    cmp #GT_MODE_PLAY
    bne gtResumed
    jsr gtErasePaused
gtResumed:
    jsr gtMusicStart            // the music comes back with the display
    jmp gtLoop

// ==========================================
//  ATTRACT -- the reference's four screens on its own 254-frame delay
// ==========================================
//  The screens are OUR text on OUR frame: the reference's are 21x21 CharPad maps
//  in its own charset (see UPSTREAM.md), and this port draws with the kernel's
//  glyphs. When the artwork exists, the drawing routines are the only things that
//  change -- the cycle, the delay and the key handling are the reference's.
gtStartAttract:
    lda #GT_MODE_ATTRACT
    sta gtMode
    lda #GT_TUNE_TITLE
    jsr gtMusicTune             // the mode's tune (a no-op: see gtMusicTune)
    jsr gtMusicStart            // the reference plays its title music here
    lda #0                      // --and opens on screen 0, the title
    // fall through
gtAtShow:                       // A = screen number, 0..3
    sta gtAtStep
    lda #<GT_ATTRACT_DELAY      // ---- split: the delay is > 255 (see the constant)
    sta gtAtDelayLo
    lda #>GT_ATTRACT_DELAY
    sta gtAtDelayHi
    jsr gtPace16                // ...for this machine
    jsr kClearScreen
    jsr gtFrameBox
    //  Each screen is a jmp through an inverted branch (Rule 4): the title is the
    //  fall-through, and the other three are past the end of it.
    ldy gtAtStep
    cpy #1
    bne gtAtNot1
    jmp gtAtCredits
gtAtNot1:
    cpy #2
    bne gtAtNot2
    jmp gtAtKeys
gtAtNot2:
    cpy #3
    bne gtAtTitle
    jmp gtAtPreview
    // --- 0: title ---
gtAtTitle:
    lda #GT_C_TITLE
    jsr kSetTextColor
    gtPut(16, 6, gtSTitle)
    lda #GT_C_TEXT
    jsr kSetTextColor
    gtPut(11, 8, gtSTitleSub)
    gtPut(13, 12, gtSPressAny)
    gtPut(11, 14, gtSTitleHint)
    rts
gtAtCredits:
    lda #GT_C_TEXT
    jsr kSetTextColor
    gtPut(11, 8, gtSCred1)
    gtPut(11, 10, gtSCred2)
    gtPut(9, 12, gtSCred3)
    gtPut(13, 16, gtSCred4)
    rts
gtAtKeys:
    lda #GT_C_TITLE
    jsr kSetTextColor
    gtPut(16, 6, gtSCtrlTitle)
    lda #GT_C_TEXT
    jsr kSetTextColor
    gtPut(12, 9, gtSCtrl1)
    gtPut(12, 11, gtSCtrl2)
    gtPut(12, 13, gtSCtrl3)
    gtPut(12, 15, gtSCtrl4)
    gtPut(12, 17, gtSCtrl5)
    gtPut(12, 19, gtSCtrl6)
    gtPut(12, 21, gtSCtrl7)
    rts
gtAtPreview:
    //  Screen 3 is the select screen itself (the reference shows it in its cycle
    //  too) -- static here, because the flash only ticks in SELECT mode.
    lda #1
    sta gtFlashFlag
    jmp gtDrawSelect

//  The countdown is 16-bit (lo first, borrowing into the high byte), because the
//  delay is longer than a byte -- see GT_ATTRACT_DELAY.
gtAtTick:
    lda gtAtDelayLo
    bne gtAtDec
    dec gtAtDelayHi
gtAtDec:
    dec gtAtDelayLo
    lda gtAtDelayLo
    ora gtAtDelayHi
    bne gtAtTickOut
    lda gtAtStep
    clc
    adc #1
    and #3                      // the reference's cycle: three screens, then the
    jmp gtAtShow                //   level-select screen, then round again
gtAtTickOut:
    rts

//  A one-cell frame around the whole surface. The reference's screens are framed
//  too, so ours is -- and it is the piece of artwork this port has today.
gtFrameBox:
    lda #GT_C_WALL
    jsr kSetTextColor
    lda #<gtGlyphSolid
    sta gtGlLo
    lda #>gtGlyphSolid
    sta gtGlHi
    lda #0
    sta gtTmpCol
gtFrameTopLp:
    lda #0
    sta gtTmpRow
    jsr gtBlitNow               // top edge (row 0)
    lda #GT_FRAME_LAST
    sta gtTmpRow
    jsr gtBlitNow               // bottom edge -- the screen's last row
    inc gtTmpCol
    lda gtTmpCol
    cmp #GT_SCREEN_COLS
    bcc gtFrameTopLp
    lda #1
    sta gtTmpRow
gtFrameSideLp:
    lda #0
    sta gtTmpCol
    jsr gtBlitNow               // left edge
    lda #GT_SCREEN_COLS-1
    sta gtTmpCol
    jsr gtBlitNow               // right edge
    inc gtTmpRow
    lda gtTmpRow
    cmp #GT_FRAME_LAST
    bcc gtFrameSideLp           // rows 1..GT_FRAME_LAST-1: the corners are drawn
    rts                         //   by the two edge loops above

// ==========================================
//  SELECT -- the reference's level grid
// ==========================================
gtStartSelect:
    lda #GT_MODE_SELECT
    sta gtMode
    lda #GT_FLASH_DELAY
    jsr gtPaceN
    sta gtFlashDelay
    lda #1
    sta gtFlashFlag
    jsr kClearScreen
    jsr gtFrameBox
    // fall through -- the screen itself
gtDrawSelect:
    lda #GT_C_TITLE
    jsr kSetTextColor
    gtPut(14, 4, gtSSelTitle)
    lda #GT_C_TEXT
    jsr kSetTextColor
    gtPut(11, 12, gtSSelKey1)
    gtPut(11, 13, gtSSelKey2)
    gtPut(11, 14, gtSSelKey3)
    gtPut(11, GT_REP_ROW, gtSSelKey4)
    jsr gtPrintRepeat           // the value beside it
    jmp gtPrintHi               // and the hi scores under it all
    // fall through -- and the ten digits
//  Paint the reference's grid: every level in the plain colour, the chosen one in
//  the flash colour or blanked, according to gtFlashFlag. Repainting all ten is
//  ten blits and one routine, where the reference tracked the previous digit to
//  repaint exactly one -- the whole grid costs less than the bookkeeping did.
gtSelPaint:
    lda #0
    sta gtTmpIdx
gtSelPaintLp:
    ldy gtTmpIdx
    lda gtLevCol,y
    tax
    lda gtLevRow,y
    tay
    jsr kSetCursor              // X = col, Y = row: the one call that keeps both
    ldy gtTmpIdx
    cpy gtSelLevel
    bne gtSelPlain
    lda gtFlashFlag
    beq gtSelHide
    lda #GT_C_TITLE
    jsr kSetTextColor
    jmp gtSelDigit
gtSelHide:
    lda #$20                    // the flash's off half: a space
    jmp gtSelPrint
gtSelPlain:
    lda #GT_C_TEXT
    jsr kSetTextColor
gtSelDigit:
    lda gtTmpIdx
    clc
    adc #GT_DIGIT0
gtSelPrint:
    jsr kPrintChar              // clobbers A, X, Y, kPtr and kArg
    inc gtTmpIdx
    lda gtTmpIdx
    cmp #10
    bcc gtSelPaintLp
    rts

//  A = the key (never 'q' and never 0 -- the caller has dealt with both).
//  The reference's own bindings: LEFT/RIGHT change the level and RETURN starts
//  (plus fire, which its input.asm maps to the same code as 's'). A DIGIT picks
//  that level outright, which is what the reference's screen asks for with its
//  numbered grid but only offers through the cursor keys.
gtSelKey:
    cmp #GT_DIGIT0
    bcc gtSelNotDigit
    cmp #GT_DIGIT0+10
    bcs gtSelNotDigit
    sec
    sbc #GT_DIGIT0              // '0'..'9' -- 0..9
    sta gtSelLevel
    jmp gtSelChanged
gtSelNotDigit:
    cmp #GT_K_RIGHT
    beq gtSelUp
    cmp #GT_K_LEFT
    beq gtSelDown
    cmp #GT_K_DOWN
    beq gtSelGo
    cmp #GT_K_DOWN2
    beq gtSelGo
    cmp #GT_K_CW
    beq gtSelGo
    cmp #GT_K_MINUS
    beq gtSelRepDown
    cmp #GT_K_PLUS
    beq gtSelRepeat
    rts
gtSelUp:
    lda gtSelLevel
    cmp #GT_LEV_MAX
    bcs gtSelKeyOut
    inc gtSelLevel
    jmp gtSelChanged
gtSelDown:
    lda gtSelLevel
    beq gtSelKeyOut
    dec gtSelLevel
gtSelChanged:
    //  Show the choice at once, on the digit: the reference's levelDisplayFlag
    //  and its delay reset, because the player has just looked at that spot.
    lda #GT_FLASH_DELAY
    jsr gtPaceN
    sta gtFlashDelay
    lda #1
    sta gtFlashFlag
    jsr gtSelPaint
gtSelKeyOut:
    rts
gtSelGo:
    jmp gtStartPlay

//  The select screen's repeat setting: '-' and '+' move it between the limits,
//  and the value is the reference's INPUTDELAY -- the frames between two accepted
//  repeats of the same action. gtSetRepeat halves it for the drop keys.
gtSelRepeat:
    lda gtRepeat
    cmp #GT_REPEAT_MAX
    beq gtSelRepDone
    inc gtRepeat
    jsr gtSetRepeat
    jmp gtPrintRepeat
gtSelRepDown:
    lda gtRepeat
    cmp #GT_REPEAT_MIN
    beq gtSelRepDone
    dec gtRepeat
    jsr gtSetRepeat
    jmp gtPrintRepeat
gtSelRepDone:
    rts

gtSelTick:
    dec gtFlashDelay
    bne gtSelTickOut
    lda #GT_FLASH_DELAY
    jsr gtPaceN
    sta gtFlashDelay
    lda gtFlashFlag
    eor #1
    sta gtFlashFlag
    jsr gtSelPaint
gtSelTickOut:
    rts

// ==========================================
//  gtStartPlay -- starts play once the level select is answered
// ==========================================
gtStartPlay:
    lda #GT_MODE_PLAY
    sta gtMode
    lda #0
    sta gtPaused
    lda gtSelLevel
    sta gtLevel                 // start where the player chose
    lda #0
    sta gtLinesThisLevel
    jsr kClearScreen            // the select screen goes away first
    jsr gtSeed                  // a fresh deal for a fresh game
    lda #GT_TUNE_GAME
    jsr gtMusicTune             // the mode's tune (a no-op: see gtMusicTune)
    //  ---- The chrome is drawn HERE and not at the task entry, because the game no
    //  longer starts at the entry -- the attract and select screens own the surface
    //  until they hand it over. Walls, floor and panel labels are the chrome
    //  routine's job and nothing else draws them (an earlier draft of this mode
    //  machine cleared the screen and went straight to gtNewGame, which is why the
    //  field came up bare).
    jsr gtChrome
    jmp gtNewGame               // --which takes the delay from the level

// ==========================================
//  Level -- the reference's rule, recomputed from the level
// ==========================================
//  gtFallForLevel: delay = max(GT_FALL_MIN, GT_FALL_START - 4*level). The
//  reference subtracts 4 from the live delay instead, which lets a delay of
//  exactly 4 become 0 and then oscillate 0/4 for the rest of the game; deriving it
//  from the level cannot drift and cannot wrap (see the header).
gtFallForLevel:
    lda #GT_FALL_START
    ldx gtLevel
    beq gtFflStore
gtFflLp:
    sec
    sbc #GT_FALL_STEP
    bcs gtFflTry
    lda #GT_FALL_MIN            // borrowed: it went below zero
    jmp gtFflStore
gtFflTry:
    cmp #GT_FALL_MIN
    bcs gtFflNext
    lda #GT_FALL_MIN            // inside the step, but under the floor
    jmp gtFflStore
gtFflNext:
    dex
    bne gtFflLp
gtFflStore:
    jsr gtPaceN                 // ...for this machine
    sta gtFallDelay
    sta gtFallTimer
    rts

//  gtAddLevel -- a level up: ten lines off the counter, and the new delay.
gtAddLevel:
    inc gtLevel
    lda gtLinesThisLevel
    sec
    sbc #GT_LINES_PER_LEVEL
    sta gtLinesThisLevel
    jsr gtFallForLevel
    // fall through -- the new level has to be on the panel
//  gtPrintLevel -- two digits, from a binary level: repeated subtraction by ten,
//  so a level cannot exceed 99 and there is no BCD in this game at all.
gtPrintLevel:
    lda #GT_C_TEXT
    jsr kSetTextColor
    ldx #GT_VAL_COL             // the value shares the label's row, one space past
    ldy #GT_LVL_ROW             //   the label -- see the panel rows above
    jsr kSetCursor
    lda gtLevel
    // fall through -- the shared two-digit printer
//  Print A (0..99) as two digits at the cursor, which the caller has set: binary
//  to decimal by repeated subtraction, so there is no BCD in this game at all.
//  Both two-digit fields (the level and the select screen's repeat) come here.
gtPrint2Dig:
    ldx #0
gtP2Tens:
    cmp #GT_TEN
    bcc gtP2Units
    sec
    sbc #GT_TEN
    inx
    jmp gtP2Tens
gtP2Units:
    sta gtTmpIdx                // units
    stx gtTmpCol                // tens
    lda gtTmpCol
    clc
    adc #GT_DIGIT0
    jsr kPrintChar
    lda gtTmpIdx
    clc
    adc #GT_DIGIT0
    jmp kPrintChar

//  The select screen's repeat value, printed beside the label that screen draws.
gtPrintRepeat:
    lda #GT_C_TITLE
    jsr kSetTextColor
    ldx #GT_REP_COL
    ldy #GT_REP_ROW
    jsr kSetCursor
    lda gtRepeat
    jmp gtPrint2Dig

//  gtCountLevelLines -- the reference's level test (UpdateLineFlash): the lines
//  made this level accumulate, and ten of them is a level.
gtCountLevelLines:
    lda gtLinesThisLevel
    clc
    adc gtLinesMade
    sta gtLinesThisLevel
    cmp #GT_LINES_PER_LEVEL
    bcc gtCllDone
    jmp gtAddLevel
gtCllDone:
    rts

// ==========================================
//  MUSIC -- sid.lib called directly, from a thread child (see the header)
// ==========================================
//  gtMusicInit -- load the tune, then start the child that plays it. Every
//  failure here is simply "no music": the game is on screen, and a tune is not
//  worth an error the player has to read and dismiss.
gtMusicInit:
    lda #<gtTuneName            // the tune every mode plays today: see gtMusicTune
    ldx #>gtTuneName
    jsr gtLoadTune              // C=1 = no music (missing, or no pool room)
    bcs gtMiHold
//  --------------------------------------------------------------------------
//  THE OTHER TUNES -- NOT LOADED. The reference plays three: a title
//  tune in attract (SND_MUSIC_TITLE), the game tune in play (SND_TETRIS) and a
//  game-over tune (SND_MUSIC_GAMEOVER). Each one is another three-page window
//  block plus its own nine-byte config, held for the whole session, and
//  `music/` has only one more tune to offer (`fsonata`), so this port plays one
//  tune throughout. To switch them on: give GT_MUSIC_TUNES its value, add the
//  two names below, and put the body back in gtMusicTune (it is written out
//  there). The loader is already generic -- it takes a name and fills gtTuneCfg
//  -- so each extra tune is exactly this, once per name:
//
//      lda #<gtTune2Name           ; the title tune
//      ldx #>gtTune2Name
//      jsr gtLoadTune              ;   (a second config block and window set)
//      bcs gtMiHold                ; a missing extra tune is not a failure: the
//                                  ;   game falls back to the one it has
//
//      .text "fsonata.mus"         ; gtTune2Name (base names stay <= 7 chars)
//  --------------------------------------------------------------------------
gtMiHold:
    //  NOTE: this is where sidplay hands filesys.lib back with kLibHold(5), and
    //  this task deliberately does NOT. It would be the right thing for a player
    //  that only reads a tune, but a hi-score table has to be WRITTEN, and
    //  kLibHold is one-way -- a released library comes back owned by nobody. So
    //  both declared libraries are held for the session (see the HI SCORES note).
    lda gtTuneLo                // no tune: no child, and no music
    ora gtTuneHi
    beq gtMiDone
    lda #<gtMusicThread
    sta spawnPtr
    lda #>gtMusicThread
    sta spawnPtr+1
    lda #$02
    sta kArg+6                  // zpBase $02 -- sid.lib's scratch window (the one
    lda #9                      //   sidplay.tsk declares). It has to be the
    sta kArg+7                  //   CHILD's own window: the ZP save/restore is
    lda #SYS_THREADSPAWN        //   gated on the switching task's own region.
    jsr syscall
    bcs gtMiDone                // no free slot: the game runs without music
    stx gtMusSlot               // X = the child's slot
gtMiDone:
    rts

//  gtLoadTune: A/X = the FS name pointer (A = lo, X = hi) -> gtTuneLo/Hi = the
//  player's config block for that file, and gtTuneBufLo/Hi = the three window
//  pages it pages the file into; both are cleared and C=1 on any failure (not in
//  the FS, or no pool room). kReuFindFile and kMalloc clobber kPtr and kArg, so
//  the name arrives in registers and is re-set in kArg for the call that wants
//  it; kMalloc's block comes back in kPtr, which is stashed at once.
gtLoadTune:
    sta gtNameLo
    stx gtNameHi
    lda #0                      // not loaded until the windows are ours
    sta gtTuneBufLo
    sta gtTuneBufHi
    sta gtTuneLo
    sta gtTuneHi
    lda gtNameLo
    sta kArg
    lda gtNameHi
    sta kArg+1
    jsr kReuFindFile            // C=1 not in the FS; the offset is in reuFsPtr
    bcs gtLtDone
    ldy #0                      // config +0..+2: the file's FS offset
    lda reuFsPtr
    sta gtTuneCfg,y
    iny
    lda reuFsPtr+1
    sta gtTuneCfg,y
    iny
    lda reuFsPtr+2
    sta gtTuneCfg,y
    lda #GT_TUNE_WIN_PAGES       // one 256-byte window per voice
    jsr kMalloc                 // -> kPtr = the block (C=1: no room)
    bcs gtLtDone
    lda kPtr
    sta gtTuneBufLo
    lda kPtr+1
    sta gtTuneBufHi
    ldy #3                      // config +3..+8: the windows, lo/hi interleaved.
    ldx #0                      //   The block is page-aligned, so all three low
gtLtWin:                        //   bytes are its own; the high bytes count up
    lda gtTuneBufLo
    sta gtTuneCfg,y
    iny
    txa
    clc
    adc gtTuneBufHi
    sta gtTuneCfg,y
    iny
    inx
    cpx #GT_TUNE_WIN_PAGES
    bne gtLtWin
    lda #<gtTuneCfg             // the player is handed the CONFIG, not an image
    sta gtTuneLo
    lda #>gtTuneCfg
    sta gtTuneHi
    clc
gtLtDone:
    rts

//  gtMusicStart -- ask the child for music (it plays the tune from the top).
gtMusicStart:
    lda gtMusOff
    bne gtMsDone                // the player muted it: a focus regain or a mode
                                //   change must not bring it back (gtKeyMusic)
    lda gtTuneLo
    ora gtTuneHi
    beq gtMsDone
    lda gtMusSlot
    cmp #$ff
    beq gtMsDone                // no child: nothing to ask
    lda #1
    sta gtMusOn
gtMsDone:
    rts

//  gtMusicStop -- silence the chip AND ask the child to stop. Clearing gtMusOn
//  alone does nothing: the child is parked inside kSidPlay's sleep loop, and the
//  hush is what gets it out (its player sees sidStop and returns).
gtMusicStop:
    lda #0
    sta gtMusOn
    jmp kSidHush                // tail call: silences the SID

// ==========================================
//  gtMusicTune -- which tune the current mode wants (NOT SWITCHED ON)
// ==========================================
//  The reference changes tune with the mode: SND_MUSIC_TITLE when the attract
//  loop starts, SND_TETRIS when play starts, SND_MUSIC_GAMEOVER on the way out.
//  This port plays ONE tune for the whole session (see the MUSIC note in the
//  header), so this is a no-op stub -- but every mode transition already calls it,
//  so switching the feature on is the body below, not a new call site:
//
//      gtMusicTune:                    ; A = GT_TUNE_*, 0 game / 1 title / 2 over
//          cmp gtMusTune
//          beq gtMtSame                ; already playing it: leave the song alone
//          sta gtMusTune
//          jsr gtMusicStop             ; the hush is what unblocks the child's
//      gtMtSame:                       ;   kSidPlay (see gtMusicStop)
//          ldx gtMusTune
//          lda gtTuneTabLo,x           ; a table built at load time from what
//          sta gtTuneLo                ;   actually loaded, so a tune that is
//          lda gtTuneTabHi,x           ;   missing falls back to the game tune
//          sta gtTuneHi                ;   instead of to a null pointer
//          jsr gtMusicStart            ; and play it (the song restarts from the
//          rts                         ;   top: a restarted player, not resumed)
//
//  What else it needs: the extra names and configs loaded in gtMusicInit (that
//  block is written out there), gtTuneTabLo/Hi built when they load, and
//  GT_MUSIC_TUNES set. A third tune needs a third .mus in `music/`.
gtMusicTune:
    rts

//  gtMusicBye -- the exit path: leave the player loop for good, make sure the
//  child really is gone, then hand its window block back. The order matters: the
//  child can still be inside the player reading those windows, so the free is last.
gtMusicBye:
    lda #1
    sta gtMusQuit
    jsr gtMusicStop
    jsr gtMusicReap
    lda gtTuneBufLo
    ora gtTuneBufHi
    beq gtMbDone
    lda gtTuneBufLo
    sta kPtr
    lda gtTuneBufHi
    sta kPtr+1
    jsr kFree                   // kPtr = the block
gtMbDone:
    rts

//  gtMusicReap -- wait for the child to leave, and kill it if it does not. It
//  shares this code page, so it MUST NOT outlive the game: sysExit does not reap
//  thread children (only `kill` cascades), and a child parked by
//  `pause gortris.N` would never come back on its own. The countdown lives in
//  memory because a yield clobbers Y.
gtMusicReap:
    ldx gtMusSlot
    cpx #$ff
    beq gtMrDone
    lda #GT_MUSIC_REAP
    sta gtReapCnt
gtMrWait:
    lda taskState,x
    beq gtMrGone
    lda #SYS_YIELD
    jsr syscall                 // X survives a yield; A and Y do not
    dec gtReapCnt
    bne gtMrWait
    ldx gtMusSlot
    lda #SYS_KILL
    jsr syscall                 // it never woke: force it off our code page
gtMrGone:
    lda #$ff
    sta gtMusSlot
gtMrDone:
    rts

//  gtMusicThread -- the child. It READS the game's bytes (gtMusOn, gtMusQuit,
//  gtTuneLo/Hi) and writes none of them, and it never touches the parent's
//  scratch, because the two share this code page and only one of them runs at a
//  time. Reached ONLY through spawnPtr -- the game itself never falls in here.
gtMusicThread:
    lda gtMusQuit
    bne gtMtExit
    lda gtMusOn
    beq gtMtIdle
    lda gtTuneLo
    sta kPtr
    lda gtTuneHi
    sta kPtr+1
    lda #GT_MUSIC_DELAY
    sta kArg
    lda #0
    sta kArg+1                  // 0 = THE PLAYER repeats the tune when it ends.
    lda #0                      // no transpose: sid.lib answers the machine's own
    sta kArg+2                  //   pitch shift itself (+1 here is one octave DOWN)
    lda #0
    sta kArg+3                  // no semitone offset
    lda #1                      // mode: the tune is paged from the REU
    jsr kSidPlay                //   The loop lives HERE, not in the file: tetris.mus
                                //   is a single pass that ends in HLT (no HED/TAL), so
                                //   `run sidplay tetris` plays it once and stops while
                                //   this thread gets it again and again. kSidHush ends
                                //   it early; nothing has to watch for the end.
    bcc gtMusicThread           // returned: hushed, or the tune finished
    //  Refused: another task owns the SID. Drop the request rather than spin --
    //  the game asks again when it next takes the display back.
    lda #0
    sta gtMusOn
    jmp gtMusicThread
gtMtIdle:
    lda #SYS_YIELD
    jsr syscall
    jmp gtMusicThread
gtMtExit:
    lda #SYS_EXIT
    jsr syscall
    jmp gtMtExit                // SYS_EXIT never returns; this is the guard

// ==========================================
//  gtKey -- dispatch one key
// ==========================================
gtKey:
    cmp #GT_K_LEFT
    bne gtK1
    jmp gtMoveLeft
gtK1:
    cmp #GT_K_RIGHT
    bne gtK2
    jmp gtMoveRight
gtK2:
    cmp #GT_K_CW
    bne gtK3
    jmp gtRotCW
gtK3:
    cmp #GT_K_CCW
    bne gtK4
    jmp gtRotCCW
gtK4:
    cmp #GT_K_DOWN
    bne gtK4b
    jmp gtSoftDrop
gtK4b:
    cmp #GT_K_DOWN2
    bne gtK4c
    jmp gtSoftDrop
gtK4c:
    cmp #GT_K_SPACE
    bne gtK5
    jmp gtHardDrop
gtK5:
    cmp #GT_K_PAUSE
    bne gtK6
    jmp gtTogglePause
gtK6:
    //  No 'q' here: gtKeyMode checks it first, for every mode, so this dispatch
    //  only ever sees the keys the GAME owns (movement, turns, drops, pause).
gtKeyOut:
    rts

// ==========================================
//  gtStep -- one displayed frame of game time
// ==========================================
gtStep:
    lda gtPaused
    bne gtStepDone
    dec gtFallTimer
    bne gtStepDone
    lda gtFallDelay
    sta gtFallTimer
    jsr gtFall
gtStepDone:
    rts

//  One row of gravity in the reference's order: erase, move, test, put back.
//  A failed test is what LOCKS the piece (DropBlock's status 2 = new block
//  needed), so the fall is the lock trigger.
gtFall:
    jsr gtErasePiece
    inc gtPY
    jsr gtCheckPiece
    bcc gtFallFits
    dec gtPY                   // no room: back up--
    jsr gtDrawPiece            // --put it back on screen--
    jsr gtLockPiece            // --and settle it into the board
    jsr gtClearLines
    jsr gtNextPiece
    rts
gtFallFits:
    jsr gtDrawPiece
    rts

// ==========================================
//  Movement -- erase, move, test, put back (the reference's Block* routines)
// ==========================================
gtMoveLeft:
    jsr gtErasePiece
    dec gtPX
    jsr gtCheckPiece
    bcc gtMoveLeftOk            // ---- test CARRY: gtCheckPiece answers in C (C=0 fits)
    inc gtPX
    jmp gtMovePut
gtMoveLeftOk:
    jsr gtDrawPiece
    rts
gtMoveRight:
    jsr gtErasePiece
    inc gtPX
    jsr gtCheckPiece
    bcc gtMoveRightOk
    dec gtPX
    jmp gtMovePut
gtMoveRightOk:
    jsr gtDrawPiece
    rts
gtMovePut:
    jsr gtDrawPiece
    rts

gtRotCW:
    jsr gtErasePiece
    lda #0                      // 0 = forward through the frame run
    jsr gtAnimate
    jsr gtCheckPiece
    bcc gtRotCwOk
    lda #1                      // no room: turn straight back
    jsr gtAnimate
    jmp gtRotPut
gtRotCwOk:
    jsr gtDrawPiece
    rts
gtRotCCW:
    jsr gtErasePiece
    lda #1                      // 1 = backward
    jsr gtAnimate
    jsr gtCheckPiece
    bcc gtRotCcwOk
    lda #0
    jsr gtAnimate
    jmp gtRotPut
gtRotCcwOk:
    jsr gtDrawPiece
    rts
gtRotPut:
    jsr gtDrawPiece
    rts

//  Soft drop -- the reference's BlockDown: one row, a point when it really
//  moved, and it arms the delay so the next gravity step settles it at once.
gtSoftDrop:
    jsr gtErasePiece
    inc gtPY
    jsr gtCheckPiece
    bcc gtSoftOk                // C=0: it fits, so it really moved
    dec gtPY
    jsr gtDrawPiece
    lda #1
    sta gtFallTimer             // resting: lock on the next step
    rts
gtSoftOk:
    jsr gtDrawPiece
    ldy #1                      // one point per row pushed down
    jsr gtAddScoreY
    jsr gtPrintScore
    rts

// ==========================================
//  gtHardDrop -- SPACE: down to the floor now, settle, and 2 a row for it
// ==========================================
//  ---- NOT the reference's move. Its input.asm has no hard drop -- its DOWN (ENTER)
//  is one row at a time -- so this is the port's own addition, scored the classic
//  way: 2 a row against the soft drop's 1.
//
//  It settles the piece the INSTANT it lands, in exactly the order a failed
//  gravity step would have used (gtFall's own path: lock, clear, next piece), so
//  there is one settle path in the game and not two. Space on a piece that cannot
//  move is therefore a deliberate settle, worth nothing.
gtHardDrop:
    jsr gtErasePiece
    lda #0
    sta gtHardRows              // the distance, which is what it pays for
gtHdLoop:
    inc gtPY
    jsr gtCheckPiece
    bcc gtHdMore                // it fits: keep falling
    dec gtPY                    // no room (or past the floor): back up to where
    jmp gtHdLand                //   the piece actually stopped
gtHdMore:
    inc gtHardRows
    jmp gtHdLoop
gtHdLand:
    jsr gtDrawPiece             // it is on screen before the settle (and the
                                //   flash of a cleared line has something to show)
    ldx gtHardRows
    beq gtHdSettle              // it was already resting: no points
gtHdScoreLp:
    //  ---- WARNING: TWO A ROW, ADDED ONE ROW AT A TIME -- NEVER `asl` THE ROW COUNT.
    //  gtAddScoreY adds in DECIMAL mode, so its addend MUST be a valid BCD byte.
    //  `asl` doubles a BINARY count: 7 rows give $0E, which is not BCD, and the
    //  chip's single +6 correction cannot repair a nibble sum over $0F -- the
    //  units digit comes back as $A and gtPrintDigit prints $30+$A, i.e. ':',
    //  while the addend is worth the wrong number too (20 rows add $28 = 28, not
    //  40). 2 IS valid BCD, so every addend here is.
    ldy #2
    jsr gtAddScoreY
    dex
    bne gtHdScoreLp
    jsr gtPrintScore
    lda #0
    sta gtHardRows
gtHdSettle:
    jsr gtLockPiece             // --now settle it, as a failed fall would have
    jsr gtClearLines
    jmp gtNextPiece

//  'p' -- the manual pause. It freezes the field exactly as losing the display
//  does; only the focus path will silence the music.
gtTogglePause:
    lda gtPaused
    beq gtPauseOn
    lda #0
    sta gtPaused
    jsr gtErasePaused
    rts
gtPauseOn:
    lda #1
    sta gtPaused
    jsr gtSayPaused
    rts

// ==========================================
//  gtCheckPiece -- does the current frame fit at (gtPX, gtPY)?
//  C=0 fits, C=1 occupied or outside the field.
// ==========================================
//  ---- THE ANSWER IS IN CARRY, AND ONLY IN CARRY. The routine loads and compares
//  its way through the frame, so Z on return is just whatever the last compare
//  happened to leave -- and the one case where that LOOKS right is the one that
//  bit this game: for a cell exactly at board column GT_W the bounds test is
//  `cmp #GT_W`, which is EQUAL, so Z=1 -- a caller testing `beq` reads "it fits"
//  and lets a piece slide one column into the right wall. Every caller uses
//  bcc/bcs, and the left/right asymmetry the player saw was exactly that: moving
//  left could only ever fail on occupancy (Z=0, correct by accident), while
//  moving right could fail on the bound (Z=1, misread).
gtCheckPiece:
    ldx #0
gtChkCell:
    lda gtFrameBuf,x
    cmp #$20                    // space = this frame cell is empty
    beq gtChkNext
    txa
    and #3                      // dx within the 4x4 frame
    clc
    adc gtPX
    cmp #GT_W
    bcs gtChkFail               // past the right wall
    sta gtTmpCol
    txa
    lsr
    lsr                         // dy = index / 4
    clc
    adc gtPY
    cmp #GT_H
    bcs gtChkFail               // through the floor
    tay
    lda gtRowLo,y               // row*10 from a table -- no multiply anywhere
    clc
    adc gtTmpCol
    tay
    lda gtBoard,y
    bne gtChkFail               // a settled cell is there
gtChkNext:
    inx
    cpx #16
    bcc gtChkCell
    clc
    rts
gtChkFail:
    sec
    rts

// ==========================================
//  gtLockPiece -- write the frame into the board as piece+1
// ==========================================
//  The screen already shows the piece here, so locking is a board write only:
//  no redraw, no flicker.
gtLockPiece:
    ldx #0
gtLockCell:
    lda gtFrameBuf,x
    cmp #$20
    beq gtLockNext
    txa
    and #3
    clc
    adc gtPX
    sta gtTmpCol
    txa
    lsr
    lsr
    clc
    adc gtPY
    tay
    lda gtRowLo,y
    clc
    adc gtTmpCol
    tay
    lda gtPiece
    clc
    adc #1
    sta gtBoard,y               // 0 keeps meaning "empty"
gtLockNext:
    inx
    cpx #16
    bcc gtLockCell
    rts

// ==========================================
//  Drawing the falling piece
// ==========================================
//  Draw and erase are the same walk; only the glyph differs, which is why the
//  glyph pointer is a pair of bytes rather than a mode flag. Erasing is always
//  safe: a falling piece cannot overlap a settled cell (gtCheckPiece forbids
//  it), so every cell it occupies is genuinely empty underneath.
gtErasePiece:
    lda #<gtGlyphBlank
    sta gtGlLo
    lda #>gtGlyphBlank
    sta gtGlHi
    jmp gtBlitPiece
gtDrawPiece:
    jsr gtSetPieceColour
    lda #<gtGlyphBlock
    sta gtGlLo
    lda #>gtGlyphBlock
    sta gtGlHi
    // fall through
gtBlitPiece:
    lda gtPX
    clc
    adc #GT_COL
    sta gtScrCol
    lda gtPY
    clc
    adc #GT_ROW
    sta gtScrRow
    jmp gtBlitFrame

//  Blit the frame's FILLED cells at (gtScrCol+dx, gtScrRow+dy).
//  The walk index lives in memory, never in X: kPrintGlyph clobbers A, X, Y,
//  kPtr and srcPtr, so nothing may be carried in a register across it.
gtBlitFrame:
    lda #0
    sta gtIdx
gtBlitCell:
    ldx gtIdx
    lda gtFrameBuf,x
    cmp #$20
    beq gtBlitNext
    txa
    and #3
    clc
    adc gtScrCol
    sta gtTmpCol
    ldx gtIdx
    txa
    lsr
    lsr
    clc
    adc gtScrRow
    sta gtTmpRow
    jsr gtBlitNow
gtBlitNext:
    inc gtIdx
    lda gtIdx
    cmp #16
    bcc gtBlitCell
    rts

//  Blit one cell: gtTmpCol/gtTmpRow are SCREEN coordinates, gtGlLo/Hi the glyph.
//  kPrintGlyphAt writes that ONE cell and advances nothing, so the screen's last row
//  is an ordinary row here -- kPrintGlyph would wrap on the cell it had just written
//  and wrapClamp would scroll the whole surface as it did.
//  The walk index lives in memory, never in X: the call clobbers A, X, Y, kPtr and
//  the whole kArg block, so nothing may be carried in a register across it.
gtBlitNow:
    lda gtGlLo
    sta srcPtr
    lda gtGlHi
    sta srcPtr+1
    ldx gtTmpCol
    ldy gtTmpRow
    jmp kPrintGlyphAt

// ==========================================
//  Pieces -- the reference's tables, and its Select/Animate
// ==========================================
//  gtFrameBuf holds the CURRENT frame's 16 cells, copied out of the tables once
//  per frame change, so no walk has to re-derive a table pointer after a call
//  that clobbers srcPtr/kPtr.
gtSelectPiece:
    ldy gtPiece
    lda gtFrameStart,y
    sta gtFrame
    sta gtFirstFrame
    lda gtFrameEnd,y
    sta gtLastFrame
    jmp gtLoadFrame

//  A = 0 -- forward (clockwise), A = 1 -- backward. Wrapping inside
//  [gtFirstFrame, gtLastFrame] is the reference's AnimateBlock: a piece's
//  rotations are a contiguous run of frames, so this game contains no rotation
//  maths at all.
gtAnimate:
    cmp #1
    beq gtAnimBack
    lda gtFrame
    cmp gtLastFrame
    beq gtAnimWrapFwd
    inc gtFrame
    jmp gtLoadFrame
gtAnimWrapFwd:
    lda gtFirstFrame
    sta gtFrame
    jmp gtLoadFrame
gtAnimBack:
    lda gtFrame
    cmp gtFirstFrame
    beq gtAnimWrapBack
    dec gtFrame
    jmp gtLoadFrame
gtAnimWrapBack:
    lda gtLastFrame
    sta gtFrame
    // fall through
gtLoadFrame:
    ldy gtFrame
    lda gtFrameLo,y
    sta kPtr
    lda gtFrameHi,y
    sta kPtr+1
    ldy #0
gtLoadFrameLp:
    lda (kPtr),y
    sta gtFrameBuf,y
    iny
    cpy #16
    bcc gtLoadFrameLp
    rts

// ==========================================
//  gtNextPiece -- take the previewed piece, then preview and place a new one
// ==========================================
gtNextPiece:
    lda gtNext
    sta gtPiece
    jsr gtSelectPiece
    jsr gtPickPiece
    sta gtNext
    jsr gtDrawPreview           // in the new piece's colour
    //  Place the new piece at the top, centred.
    lda #3
    sta gtPX
    lda #0
    sta gtPY
    lda gtFallDelay
    sta gtFallTimer
    jsr gtCheckPiece
    bcs gtOverlap
    jsr gtDrawPiece
    rts
gtOverlap:
    //  It does not fit at the spawn, so the stack has reached the top. Draw it
    //  anyway -- the reference prints it too, so the player can see why -- then
    //  end the game.
    jsr gtDrawPiece
    jmp gtGameOver

// ==========================================
//  gtClearLines -- CheckLines -- FlashLines -- RemoveLines (lines.asm)
// ==========================================
gtClearLines:
    lda #0
    sta gtLinesMade
    sta gtRowIdx
gtScanRows:
    jsr gtRowFull
    bcc gtScanNext
    ldy gtLinesMade
    lda gtRowIdx
    sta gtFullRows,y            // record the row (four is the maximum)
    inc gtLinesMade
gtScanNext:
    inc gtRowIdx
    lda gtRowIdx
    cmp #GT_H
    bcc gtScanRows
    lda gtLinesMade
    bne gtHaveLines
    rts
gtHaveLines:
    jsr gtFlashRows
    jsr gtCollapse
    jsr gtRedrawField
    jsr gtScoreLines
    jsr gtAddLinesY
    jsr gtPrintScore
    jsr gtPrintLines
    jsr gtCountLevelLines        // ten of these is a level (gtAddLevel)
    rts

//  C=1 when every cell of row gtRowIdx is occupied.
gtRowFull:
    ldy gtRowIdx
    lda gtRowLo,y
    tay
    ldx #GT_W
gtRowFullLp:
    lda gtBoard,y
    beq gtRowNotFull
    iny
    dex
    bne gtRowFullLp
    sec
    rts
gtRowNotFull:
    clc
    rts

//  Three flashes, the reference's count, six displayed frames each. Every pass
//  yields, so the rest of the system keeps running through the animation, and
//  the wait is measured in DISPLAYED frames so it takes the same time anywhere.
gtFlashRows:
    lda #3
    sta gtFlashCnt
gtFlashLoop:
    lda #<gtGlyphSolid
    sta gtGlLo
    lda #>gtGlyphSolid
    sta gtGlHi
    lda #GT_C_FLASH
    jsr kSetTextColor
    jsr gtFlashPaint
    lda #6
    jsr gtWaitFrames
    lda #<gtGlyphBlank
    sta gtGlLo
    lda #>gtGlyphBlank
    sta gtGlHi
    jsr gtFlashPaint
    lda #6
    jsr gtWaitFrames
    dec gtFlashCnt
    bne gtFlashLoop
    rts

//  Paint the recorded rows with the current glyph.
gtFlashPaint:
    lda #0
    sta gtTmpIdx
gtFlashPaintLp:
    ldy gtTmpIdx
    cpy gtLinesMade
    bcs gtFlashPaintOut
    lda gtFullRows,y
    clc
    adc #GT_ROW
    sta gtTmpRow
    lda #0
    sta gtTmpCol
gtFlashRowLp:
    lda gtGlLo
    sta srcPtr
    lda gtGlHi
    sta srcPtr+1
    lda gtTmpCol
    clc
    adc #GT_COL
    tax
    ldy gtTmpRow
    jsr kPrintGlyphAt
    inc gtTmpCol
    lda gtTmpCol
    cmp #GT_W
    bcc gtFlashRowLp
    inc gtTmpIdx
    jmp gtFlashPaintLp
gtFlashPaintOut:
    rts

//  Remove the full rows: shift everything above each one down, then blank the
//  top row. Ascending order is correct -- collapsing a row moves nothing below
//  it, so the remaining recorded indices stay valid.
gtCollapse:
    lda #0
    sta gtTmpIdx
gtCollLp:
    ldy gtTmpIdx
    cpy gtLinesMade
    bcs gtCollDone
    lda gtFullRows,y
    sta gtTmpRow
gtCollShift:
    lda gtTmpRow
    beq gtCollTop               // reached the top: blank it
    sec
    sbc #1                      // the row to copy from
    tay
    lda gtRowLo,y
    sta gtSrcOff
    ldy gtTmpRow
    lda gtRowLo,y
    sta gtDstOff
    lda #0
    sta gtTmpCol
gtCollCopyLp:
    lda gtSrcOff
    clc
    adc gtTmpCol
    tay
    lda gtBoard,y
    sta gtByt
    lda gtDstOff
    clc
    adc gtTmpCol
    tay
    lda gtByt
    sta gtBoard,y
    inc gtTmpCol
    lda gtTmpCol
    cmp #GT_W
    bcc gtCollCopyLp
    dec gtTmpRow                // now collapse the row above (which is empty)
    jmp gtCollShift
gtCollTop:
    lda #0
    jsr gtBlankRow
    inc gtTmpIdx
    jmp gtCollLp
gtCollDone:
    rts

//  A = board row to clear.
gtBlankRow:
    tay
    lda gtRowLo,y
    sta gtSrcOff
    lda #0
    sta gtTmpCol
gtBlankRowLp:
    lda gtSrcOff
    clc
    adc gtTmpCol
    tay
    lda #0
    sta gtBoard,y
    inc gtTmpCol
    lda gtTmpCol
    cmp #GT_W
    bcc gtBlankRowLp
    rts

// ==========================================
//  gtRedrawField -- repaint the whole interior from the board
// ==========================================
//  Used after a collapse (the shift moves up to a whole field) and at new game.
//  200 blits inside one frame; the display commits at line 0, so the player sees
//  the result atomically.
gtRedrawField:
    lda #0
    sta gtRowIdx
gtRFRowLp:
    lda #0
    sta gtTmpCol
gtRFCellLp:
    lda gtRowIdx
    tay
    lda gtRowLo,y
    clc
    adc gtTmpCol
    tay
    lda gtBoard,y
    beq gtRFEmpty
    tay
    lda gtColourTab-1,y         // value is piece+1
    jsr kSetTextColor
    lda #<gtGlyphBlock
    sta gtGlLo
    lda #>gtGlyphBlock
    sta gtGlHi
    jmp gtRFBlit
gtRFEmpty:
    lda #<gtGlyphBlank
    sta gtGlLo
    lda #>gtGlyphBlank
    sta gtGlHi
gtRFBlit:
    lda gtTmpCol
    clc
    adc #GT_COL
    sta gtTmpCol                // screen column for this blit
    lda gtRowIdx
    clc
    adc #GT_ROW
    sta gtTmpRow
    jsr gtBlitNow
    sec
    lda gtTmpCol
    sbc #GT_COL
    sta gtTmpCol                // back to the board column
    inc gtTmpCol
    lda gtTmpCol
    cmp #GT_W
    bcc gtRFCellLp
    inc gtRowIdx
    lda gtRowIdx
    cmp #GT_H
    bcc gtRFRowLp
    rts

// ==========================================
//  The "next" preview box
// ==========================================
gtDrawPreview:
    //  Blank the box (all 16 cells, so the previous piece leaves no residue),
    //  then draw the next piece's FIRST frame in its own colour.
    lda #<gtGlyphBlank
    sta gtGlLo
    lda #>gtGlyphBlank
    sta gtGlHi
    lda #GT_PREV_C
    sta gtScrCol
    lda #GT_PREV_R
    sta gtScrRow
    jsr gtBlankBox
    //  ---- THE PREVIEW BORROWS gtFrame, so it must give it back. gtFrame names
    //  the frame within the FALLING piece's run, and this routine needs a
    //  different frame (the previewed piece's first) in the same two bytes --
    //  leaving it changed would make gtCheckPiece and gtDrawPiece use the wrong
    //  shape for the piece in play, from the very first one on (an earlier draft
    //  did exactly that: the piece on screen was the NEXT piece's shape).
    lda gtFrame
    sta gtPrevFrame
    lda gtNext
    tay
    lda gtFrameStart,y          // frame 0 of the previewed piece
    sta gtFrame
    jsr gtLoadFrame
    lda gtNext
    tay
    lda gtColourTab,y
    jsr kSetTextColor
    lda #<gtGlyphBlock
    sta gtGlLo
    lda #>gtGlyphBlock
    sta gtGlHi
    jsr gtBlitFrame
    //  Put gtFrame back and rebuild the buffer from it, so the caller's piece is
    //  described by gtFrameBuf again whatever the caller was doing.
    lda gtPrevFrame
    sta gtFrame
    jmp gtLoadFrame

//  Blank all 16 cells of the 4x4 box at (gtScrCol, gtScrRow) with the current
//  glyph.
gtBlankBox:
    lda #0
    sta gtIdx
gtBlankBoxLp:
    lda gtIdx
    and #3
    clc
    adc gtScrCol
    sta gtTmpCol
    lda gtIdx
    lsr
    lsr
    clc
    adc gtScrRow
    sta gtTmpRow
    jsr gtBlitNow
    inc gtIdx
    lda gtIdx
    cmp #16
    bcc gtBlankBoxLp
    rts

// ==========================================
//  Scoring and the counters
// ==========================================
//  Both counters are 6-digit BCD (three bytes) so that printing them is a
//  nibble walk and a carry propagates with the CPU's own decimal mode -- no
//  16-bit division anywhere in this game.
//    gtScore: player-owned. A thread child must not write it.
//    gtLines: player-owned too.
//  The reference multiplies a line score by (level+1) -- AddLineValue adds the
//  same row once per level. Ours adds it level+1 times, so the multiplier is a
//  loop over the one BCD add and not arithmetic on a 3-byte number.
gtScoreLines:
    ldy gtLinesMade
    dey                         // 0..3
    tya
    asl
    sta gtTmpIdx
    tya
    clc
    adc gtTmpIdx                // y*3
    sta gtPtsOff                // --the table row for this many lines
    lda gtLevel
    clc
    adc #1
    sta gtLvlCnt                // level+1 adds -- the counter is in memory
    sed                         //   because gtScoreAddRow uses X itself
    clc
gtScoreLp:
    ldx gtPtsOff
    lda gtScore+2
    adc gtPts+2,x               // table is most-significant byte first
    sta gtScore+2
    lda gtScore+1
    adc gtPts+1,x
    sta gtScore+1
    lda gtScore
    adc gtPts,x
    sta gtScore
    clc                         // each add starts from a clear carry
    dec gtLvlCnt
    bne gtScoreLp
    cld
    rts

//  Add Y (a small decimal value) to the score.
gtAddScoreY:
    sed
    clc
    tya
    adc gtScore+2
    sta gtScore+2
    lda gtScore+1
    adc #0
    sta gtScore+1
    lda gtScore
    adc #0
    sta gtScore
    cld
    rts

//  Add gtLinesMade lines to the line counter.
gtAddLinesY:
    sed
    clc
    lda gtLines+2
    adc gtLinesMade
    sta gtLines+2
    lda gtLines+1
    adc #0
    sta gtLines+1
    lda gtLines
    adc #0
    sta gtLines
    cld
    rts

//  ---- BOTH VALUES GO ON THE LABEL'S OWN ROW, at GT_VAL_COL: "score 001000" and
//  "lines 000", rather than the label with the number under it. Both fields are
//  fixed width (leading zeros print as spaces, so the numbers right-align and
//  nothing has to be erased between updates), and the UNITS DIGIT ALWAYS PRINTS:
//  a fresh game shows "0", not an empty field.
gtPrintScore:
    lda #<gtScore
    sta kPtr
    lda #>gtScore
    sta kPtr+1
    ldx #GT_VAL_COL
    ldy #GT_SCORE_ROW
    jsr kSetCursor
    jmp gtPrintBcd

gtPrintLines:
    lda #<gtLines
    sta kPtr
    lda #>gtLines
    sta kPtr+1
    ldx #GT_VAL_COL
    ldy #GT_LINES_ROW
    jsr kSetCursor
    jmp gtPrintBcd

//  Print the three BCD bytes at (kPtr) as six digits at the cursor, with
//  leading zeros as spaces -- except the LAST digit, which always prints: a value
//  of 0 reads "0" instead of leaving the field blank. The bytes are copied to a
//  fixed buffer first: the printing calls clobber kPtr, so the source cannot be
//  read through it.
gtPrintBcd:
    ldy #0
gtPbCopy:
    lda (kPtr),y
    sta gtPbBuf,y
    iny
    cpy #3
    bcc gtPbCopy
    lda #0
    sta gtLead                  // 0 = still suppressing leading zeros
    sta gtBcdIdx
gtPbLoop:
    ldx gtBcdIdx
    cpx #3
    bcs gtPbDone
    lda gtPbBuf,x
    pha
    lsr
    lsr
    lsr
    lsr                         // high digit
    jsr gtPrintDigit
    //  ---- THE FIELD'S LAST DIGIT ALWAYS PRINTS, so a value of 0 shows "0" rather
    //  than nothing. It is the units of the six-digit field, so keeping it also
    //  keeps the right-alignment that the padding exists for. gtLead = 1 is what
    //  switches the leading-zero suppression off; A holds the low digit and is
    //  safe on the stack across this test.
    lda gtBcdIdx
    cmp #2
    bne gtPbUnits
    lda #1
    sta gtLead
gtPbUnits:
    pla
    and #$0f                    // low digit
    jsr gtPrintDigit
    inc gtBcdIdx
    jmp gtPbLoop
gtPbDone:
    rts

//  A = 0..9 -- print it at the cursor, or a space while still suppressing.
gtPrintDigit:
    bne gtPdShow
    lda gtLead
    bne gtPdZero
    lda #$20                    // a leading zero prints as a space
    jmp gtPdOut
gtPdZero:
    lda #$30                    // screen code '0'
    jmp gtPdOut
gtPdShow:
    ldx #1
    stx gtLead
    clc
    adc #$30
gtPdOut:
    jmp kPrintChar

// ==========================================
//  Pause indicator, game over, exit
// ==========================================
gtSayPaused:
    lda #GT_C_FLASH
    jsr kSetTextColor
    ldx #GT_PANEL
    ldy #GT_PAUSED_ROW
    jsr kSetCursor
    lda #<gtSPaused
    sta kPtr
    lda #>gtSPaused
    sta kPtr+1
    jmp kPrintString

gtErasePaused:
    ldx #GT_PANEL
    ldy #GT_PAUSED_ROW
    jsr kSetCursor
    lda #<gtSBlank6
    sta kPtr
    lda #>gtSBlank6
    sta kPtr+1
    jmp kPrintString

//  The mute indicator, the same shape as "paused" one row above the pause label.
//  It belongs to the PLAY screen's panel (the attract and select screens have no
//  panel), so BOTH routines check the mode and the caller never has to; gtNewGame
//  repaints it, so a game started while muted says so.
gtSayMusOff:
    lda gtMode
    cmp #GT_MODE_PLAY
    beq gtSmOk
    rts
gtSmOk:
    lda #GT_C_TEXT
    jsr kSetTextColor
    ldx #GT_PANEL
    ldy #GT_MUSIC_ROW
    jsr kSetCursor
    lda #<gtSMusOff
    sta kPtr
    lda #>gtSMusOff
    sta kPtr+1
    jmp kPrintString

gtEraseMusOff:
    lda gtMode
    cmp #GT_MODE_PLAY
    beq gtEmOk
    rts
gtEmOk:
    ldx #GT_PANEL
    ldy #GT_MUSIC_ROW
    jsr kSetCursor
    lda #<gtSBlank9
    sta kPtr
    lda #>gtSBlank9
    sta kPtr+1
    jmp kPrintString

//  Paint the indicator to match gtMusOff.
gtMusOffPaint:
    lda gtMusOff
    bne gtSayMusOff
    jmp gtEraseMusOff

//  Game over is a MODE, not a wait loop: the mode stops the game ticks (gtUpdate)
//  and runs the animation below, and the key that leaves arrives through the
//  normal loop -- so the stack stays balanced and 'q' still means quit (see
//  gtKeyMode). The reference goes on to a hi-score table and a name entry, and so
//  does this port: gtOverLeave is its MODE_ENTERNAME step, which checks the score,
//  takes a row if it earned one, asks for the name and saves the table.
//
//  THE ANIMATION IS THE REFERENCE'S (gameover.asm): it FILLS the well with the
//  block, one row per update -- its StartGameOverMode sets drawCharacter = 105 (the
//  block tile) and linesLeft = 19 -- then CLEARS it the same way with a space, and
//  only then prints the game-over text. Its DrawLine plots (12, linesLeft) and
//  prints ten cells, with linesLeft running 19 down to 0: the well fills from the
//  BOTTOM up, at its column 12, which is this port's board column 0 (GT_COL).
//  Ours is the same walk in the same order; what differs is only the glyph -- the
//  reference prints one charset tile, we blit our 8-byte block -- and the colour,
//  which for it is the single chars colour and for us is GT_C_TEXT.
//
//  THREE STEPS, one row a frame, so both passes take 20 displayed frames and the
//  system keeps running throughout: no busy loop like the one its update-per-
//  raster-frame world could afford.
gtGameOver:
    lda #GT_MODE_OVER
    sta gtMode
    lda #GT_TUNE_OVER
    jsr gtMusicTune             // the mode's tune (a no-op: see gtMusicTune)
    lda #0                      // step 0: filling the well
    sta gtOverStep
    sta gtOverWait              // no hold yet: the fill starts on the next frame
    lda #GT_H-1                 // its linesLeft = 19: 20 rows, from the bottom up
    sta gtOverLeft
    lda #<gtGlyphBlock
    sta gtOverGlLo
    lda #>gtGlyphBlock
    sta gtOverGlHi
    rts                         // gtOverTick does the animating, a row a frame

//  gtOverLeave -- the game-over text was answered with a key. This is the
//  reference's MODE_ENTERNAME step: its game-over mode ends by going to the name
//  entry, and that is where the score is checked against the table
//  (entername.asm's StartEnterNameMode), not at game over itself.
//  A score that does not make the table goes straight back to the attract loop;
//  a score that does takes the row, asks for the name, and is saved.
gtOverLeave:
    jsr gtHiCheck               // A = the row it takes (0..2), or $FF
    cmp #$ff
    beq gtOlNoHi
    jsr gtHiInsert              // move the rows down and put the score in
    jsr gtHiNameScreen          // the table on screen, cursor on the new row
    jsr gtHiSave                // and write it back to the REU FS
gtOlNoHi:
    jmp gtStartAttract

//  One displayed frame of the animation (called from gtUpdate's OVER branch).
gtOverTick:
    lda gtOverStep
    lda gtOverWait              // a finished pass is being held on screen: the
    beq gtOverGo                //   whole point of the hold is that the filled
    dec gtOverWait              //   well is SEEN before it is taken away again
    rts
gtOverGo:
    lda gtOverStep
    cmp #GT_OVER_TEXT
    beq gtOverOut               // the text is up: a key leaves (see gtKeyMode)
    jsr gtOverRow               // this frame's row of the well
    lda gtOverLeft
    beq gtOverPassDone          // row 0 was the last one: the pass is finished
    dec gtOverLeft              // ...otherwise the next frame paints the row above
    rts
gtOverPassDone:
    lda gtOverStep              // the FILL is the pass worth holding: the clearing
    bne gtOverNoHold            //   pass is followed by the text, as it is in the
    lda #GT_OVER_HOLD           //   reference, so it gets no pause of its own
    jsr gtPaceN
    sta gtOverWait
gtOverNoHold:
    lda #GT_H-1                 // the next pass starts at the bottom again
    sta gtOverLeft
    inc gtOverStep
    lda gtOverStep
    cmp #GT_OVER_TEXT
    bne gtOverClear             // step 1 is the clearing pass: same walk, no glyph
    jmp gtSayGameOver           // step 2: the well is empty, so the text goes up
gtOverClear:
    lda #<gtGlyphBlank
    sta gtOverGlLo
    lda #>gtGlyphBlank
    sta gtOverGlHi
gtOverOut:
    rts

//  One row of the well, at board row gtOverLeft, in the current glyph and colour.
gtOverRow:
    lda #GT_C_TEXT
    jsr kSetTextColor
    lda gtOverGlLo
    sta gtGlLo                  // gtBlitNow blits whatever is in gtGlLo/Hi
    lda gtOverGlHi
    sta gtGlHi
    lda #0
    sta gtTmpIdx
gtOverRowLp:
    lda gtTmpIdx
    clc
    adc #GT_COL
    sta gtTmpCol
    lda gtOverLeft
    clc
    adc #GT_ROW
    sta gtTmpRow
    jsr gtBlitNow               // 10 cells across, one at a time
    inc gtTmpIdx
    lda gtTmpIdx
    cmp #GT_W
    bcc gtOverRowLp
    rts

//  The text, once the well has been filled and emptied again. The animation comes
//  first and this is its last step.
//  The game-over text goes INSIDE the well, which is what the reference does: its
//  WELL_GAMEOVER map is 10x20 and is written into the well itself. That map is
//  artwork (big block letters over the whole field) and this port has none, so the
//  words are our own font on the well's own width -- "game over" is nine cells and
//  the well is ten, so it sits flush at the well's left edge, one row above the
//  middle, with the prompt below it.
gtSayGameOver:
    lda #GT_C_FLASH
    jsr kSetTextColor
    ldx #GT_COL
    ldy #GT_OVER_TEXT_ROW
    jsr kSetCursor
    lda #<gtSOver
    sta kPtr
    lda #>gtSOver
    sta kPtr+1
    jsr kPrintString
    lda #GT_C_TEXT
    jsr kSetTextColor
    ldx #GT_COL
    ldy #GT_OVER_HINT_ROW
    jsr kSetCursor
    lda #<gtSPress
    sta kPtr
    lda #>gtSPress
    sta kPtr+1
    jmp kPrintString

//  Hand the display back BEFORE freeing the surface, or the display points at a
//  surface that is gone (the rule edit/godzi follow). The parent is the shell:
//  hand it the display if it is still alive, otherwise let
//  sysExit's own hand-off decide.
//  The music child goes first: it shares our code page and its tune block is
//  freed here, so both have to be finished with before we give up the surface.
gtExit:
    jsr gtMusicBye
    lda #SYS_GETTID
    jsr syscall
    tax
    lda taskParent,x
    cmp #$ff
    beq gtExitFree
    tax
    lda taskState,x
    beq gtExitFree
    jsr kSwitchView             // X = the shell (X is preserved by this call)
gtExitFree:
    jsr kFreeScreen
    lda #SYS_EXIT
    jsr syscall

gtNoScreen:
    lda #SYS_YIELD
    jsr syscall                 // no surfaces left: idle forever
    jmp gtNoScreen

// ==========================================
//  gtChrome -- walls, floor and the panel
// ==========================================
gtChrome:
    lda #GT_C_WALL
    jsr kSetTextColor
    lda #<gtGlyphSolid
    sta gtGlLo
    lda #>gtGlyphSolid
    sta gtGlHi
    lda #GT_ROW
    sta gtTmpRow
gtWallLp:
    lda #GT_WALL_L
    sta gtTmpCol
    jsr gtBlitNow
    lda #GT_WALL_R
    sta gtTmpCol
    jsr gtBlitNow
    inc gtTmpRow
    lda gtTmpRow
    cmp #GT_FLOOR+1
    bcc gtWallLp
    //  The floor, walls included, so the field is closed at the bottom corners.
    lda #GT_FLOOR
    sta gtTmpRow
    lda #GT_WALL_L
    sta gtTmpCol
gtFloorLp:
    jsr gtBlitNow
    inc gtTmpCol
    lda gtTmpCol
    cmp #GT_WALL_R+1
    bcc gtFloorLp
    // --- panel labels ---
    lda #GT_C_TEXT
    jsr kSetTextColor
    gtPut(GT_PANEL, 0,  gtSTitle)
    gtPut(GT_PANEL, GT_SCORE_ROW, gtSScore)
    gtPut(GT_PANEL, GT_LINES_ROW, gtSLines)
    gtPut(GT_PANEL, 8,  gtSNext)
    gtPut(GT_PANEL, GT_LVL_LBL_ROW, gtSLevel)
    gtPut(GT_PANEL, 18, gtSKey1)
    gtPut(GT_PANEL, 19, gtSKey2)
    gtPut(GT_PANEL, 20, gtSKey3)
    gtPut(GT_PANEL, 21, gtSKey4)
    gtPut(GT_PANEL, 22, gtSKey5)
    rts

//  Print the string in kPtr at (X, Y). kSetCursor is the only kernel call that
//  preserves X and Y, which is why the pair survives into kPrintString.
gtLabel:
    jsr kSetCursor
    jmp kPrintString

// ==========================================
//  gtNewGame
// ==========================================
gtNewGame:
    jsr gtBlankBoard
    jsr gtRedrawField
    lda #0
    sta gtScore
    sta gtScore+1
    sta gtScore+2
    sta gtLines
    sta gtLines+1
    sta gtLines+2
    jsr gtPrintScore
    jsr gtPrintLines
    lda #0
    sta gtPaused
    jsr gtErasePaused
    jsr gtFallForLevel          // the fall delay comes from the level
    jsr gtPrintLevel
    jsr gtMusOffPaint           // a game started while muted says so
    jsr gtPickPiece
    sta gtPiece
    jsr gtSelectPiece
    jsr gtPickPiece
    sta gtNext
    jsr gtDrawPreview
    lda #3
    sta gtPX
    lda #0
    sta gtPY
    jmp gtDrawPiece

gtBlankBoard:
    ldx #0
    lda #0
gtBlankBoardLp:
    sta gtBoard,x
    inx
    cpx #GT_W*GT_H
    bcc gtBlankBoardLp
    rts

//  A fresh piece id, 0..6. The kernel RNG is a Galois LFSR with period 255, so
//  kRandom returns 1..255 uniformly; masking to three bits and rejecting 7
//  keeps the seven ids even (about 32 draws each per period).
gtPickPiece:
    jsr kRandom
    and #7
    cmp #7
    beq gtPickPiece
    rts

//  Nudge the kernel RNG by a varying number of steps, so two games in one
//  session do not deal the same pieces. frameCount is where the boot is when the
//  player started the game -- human timing, so it differs run to run -- and this
//  deliberately does NOT sample the SID (voice 3 noise is the usual trick) since
//  the music thread may own the chip.
gtSeed:
    lda frameCount
    beq gtSeedDone
    tax
gtSeedLp:
    jsr kRandom                 // clobbers A only; X survives
    dex
    bne gtSeedLp
gtSeedDone:
    rts

gtSetPieceColour:
    ldy gtPiece
    lda gtColourTab,y
    jmp kSetTextColor

//  Wait A frames of the GAME's own clock, yielding every pass (never a busy wait).
//  WARNING: the count is scaled to the machine (gtPaceN) so the wait is the same REAL
//  length at any declared speed: a caller states the count a real C64 would need.
gtWaitFrames:
    jsr gtPaceN                 // 100% frames -> this machine's frames
    sta gtWaitCnt
gtWaitLp:
    lda frameCount
    cmp gtWaitLast
    beq gtWaitYield
    sta gtWaitLast
    dec gtWaitCnt
    beq gtWaitOut
gtWaitYield:
    lda #SYS_YIELD
    jsr syscall
    jmp gtWaitLp
gtWaitOut:
    rts

// ==========================================
//  Glyphs -- one 8x8 cell each
// ==========================================
//  The block leaves its left column and its bottom row clear: a 1px gutter, so
//  neighbouring cells read as separate blocks instead of one solid mass. (A
//  bevel would need two foreground colours in one cell, and a hires cell has
//  one -- the gutter is what buys the structure back.)
gtGlyphBlock:
    .byte %01111111, %01111111, %01111111, %01111111
    .byte %01111111, %01111111, %01111111, %00000000
gtGlyphSolid:
    .fill 8, $ff
gtGlyphBlank:
    .fill 8, $00

// ==========================================
//  Piece frames -- the reference's own data (blocks.asm)
// ==========================================
//  Four rows of four cells, one `.text` line each: the art is the reference's,
//  and only "space or not" is ever tested. Runs: J 0-3, T 4-7, L 8-11,
//  S 12-13, Z 14-15, I 16-17, O 18. Frames 16/17 consolidate the reference's
//  raw CharPad codes (92/93/94 vertical, 89/90/91 horizontal) into one marker
//  per cell -- those codes were per-segment bar tiles for its charset, which a
//  single block glyph per cell makes unnecessary.
gtFrame00:                      // J
    .text " II "
    .text "  I "
    .text "  I "
    .text "    "
gtFrame01:
    .text "   I"
    .text " III"
    .text "    "
    .text "    "
gtFrame02:
    .text " I  "
    .text " I  "
    .text " II "
    .text "    "
gtFrame03:
    .text "    "
    .text " III"
    .text " I  "
    .text "    "
gtFrame04:                      // T
    .text "  G "
    .text " GG "
    .text "  G "
    .text "    "
gtFrame05:
    .text "  G "
    .text " GGG"
    .text "    "
    .text "    "
gtFrame06:
    .text "  G "
    .text "  GG"
    .text "  G "
    .text "    "
gtFrame07:
    .text "    "
    .text " GGG"
    .text "  G "
    .text "    "
gtFrame08:                      // L
    .text " HH "
    .text " H  "
    .text " H  "
    .text "    "
gtFrame09:
    .text "    "
    .text "HHH "
    .text "  H "
    .text "    "
gtFrame10:
    .text " H  "
    .text " H  "
    .text "HH  "
    .text "    "
gtFrame11:
    .text "H   "
    .text "HHH "
    .text "    "
    .text "    "
gtFrame12:                      // S
    .text " X  "
    .text " XX "
    .text "  X "
    .text "    "
gtFrame13:
    .text " XX "
    .text "XX  "
    .text "    "
    .text "    "
gtFrame14:                      // Z
    .text "  H "
    .text " HH "
    .text " H  "
    .text "    "
gtFrame15:
    .text "HH  "
    .text " HH "
    .text "    "
    .text "    "
gtFrame16:                      // I, vertical
    .text " I  "
    .text " I  "
    .text " I  "
    .text " I  "
gtFrame17:                      // I, horizontal
    .text "    "
    .text "IIII"
    .text "    "
    .text "    "
gtFrame18:                      // O -- one frame: a square does not rotate
    .text "    "
    .text " JJ "
    .text " JJ "
    .text "    "

//  Frame runs per piece (the reference's blockFrameStart/End).
gtFrameStart:
    .byte 0, 4, 8, 12, 14, 16, 18
gtFrameEnd:
    .byte 3, 7, 11, 13, 15, 17, 18
gtFrameLo:
    .byte <gtFrame00, <gtFrame01, <gtFrame02, <gtFrame03
    .byte <gtFrame04, <gtFrame05, <gtFrame06, <gtFrame07
    .byte <gtFrame08, <gtFrame09, <gtFrame10, <gtFrame11
    .byte <gtFrame12, <gtFrame13, <gtFrame14, <gtFrame15
    .byte <gtFrame16, <gtFrame17, <gtFrame18
gtFrameHi:
    .byte >gtFrame00, >gtFrame01, >gtFrame02, >gtFrame03
    .byte >gtFrame04, >gtFrame05, >gtFrame06, >gtFrame07
    .byte >gtFrame08, >gtFrame09, >gtFrame10, >gtFrame11
    .byte >gtFrame12, >gtFrame13, >gtFrame14, >gtFrame15
    .byte >gtFrame16, >gtFrame17, >gtFrame18

//  Piece colours, by piece id (0..6 = J T L S Z I O).
//  ---- Data, so it must live BELOW `gortris` (see the note at the entry label).
gtColourTab:
    .byte $01, $07, $03, $04, $05, $08, $0e

//  Points per simultaneous line count (1..4), most-significant BCD byte first:
//  40, 100, 300, 1200 -- the classic table. lines.asm's own scoring differs in
//  detail; this keeps the familiar numbers.
gtPts:
    .byte $00, $00, $40
    .byte $00, $01, $00
    .byte $00, $03, $00
    .byte $00, $12, $00

// ==========================================
//  HI SCORES -- the reference's table (hi-scores.asm, entername.asm)
// ==========================================
//  Three entries of seven name cells and three BCD score bytes, most significant
//  first: exactly the reference's layout (TABLE_ENTRIES / TABLE_NAMELENGTH /
//  TABLE_SCORELENGTH), and a score has to BEAT a row to take it over.
//
//  Twenty of those bytes are names and nine are scores, so the field widths are
//  the reference's; what the entries CONTAIN at startup is this port's own choice.
//  The reference's RESET_HISCORE_TABLE writes $01 into every byte, which reads as
//  a score of BCD 010101 named "AAAAAAA" (screen code $01 IS 'A') three times
//  over. Two reasons not to copy that: it looks like corrupted memory rather than
//  a table, and 10101 points is roughly 250 cleared lines at level 0, so a first
//  session could never take a row and the flow would be untestable. This port
//  starts with three house scores instead (see gtHiTab), which any decent game
//  can beat.
//
//  It lives in the REU FS (30 bytes, `gortris.hi`) and is loaded at startup, which
//  is why this task now KEEPS its filesys.lib hold for the whole session: sidplay
//  hands that library back because it only ever reads a tune, but a score table
//  has to be WRITTEN. When the shell is up it declares filesys.lib itself, so this
//  costs nothing there; on a system where nothing else declares it, it is ~11 pool
//  pages while the game runs.
//
//  WARNING: saving is find -> delete -> save, because kReuSave refuses a duplicate
//  name. The FS's delete is a TOMBSTONE (docs/lib-filesys.md): it frees the NAME
//  only, so each save costs one directory slot and 30 fresh bytes that nothing but
//  `format` reclaims. 256 slots and 15.9 MB free, so it is not a problem in
//  practice -- but it is not an overwrite, and whoever touches this next should
//  know that.

//  gtHiMul10 -- A = an entry number (0..2) -> A = its byte offset in the table.
//  Ten is not a power of two and there are only three rows, so this is A*8 + A*2.
gtHiMul10:
    asl
    sta gtHiTmp
    asl
    asl
    clc
    adc gtHiTmp
    rts

//  A = an entry number -> kPtr = that entry (the three score bytes, then the
//  seven name cells).
gtHiPtr:
    jsr gtHiMul10
    clc
    adc #<gtHiTab
    sta kPtr
    lda #>gtHiTab
    adc #0
    sta kPtr+1
    rts

//  Does the score in gtScore make the table? A = the entry it takes (0 = best) or
//  $FF if it does not. The comparison is the reference's PROCESS_NEW_SCORE, byte
//  by byte from the most significant down. One deliberate difference: an exact tie
//  does NOT displace the older score. Its compare_flags path counts an all-equal
//  entry as a new high score and rewrites it, which loses the earlier name for no
//  gain; keeping the incumbent is the friendlier reading of the same table.
gtHiCheck:
    lda #0
    sta gtHiEntry
gtHcNext:
    lda gtHiEntry
    jsr gtHiPtr                 // kPtr = the entry's score bytes
    ldy #0
gtHcBytes:
    lda gtScore,y
    cmp (kPtr),y
    bcc gtHcLower               // the score is below this row: try the next
    beq gtHcSame                // equal so far: the next byte decides
    lda gtHiEntry               // higher: this is the row it takes
    rts
gtHcSame:
    iny
    cpy #3
    bcc gtHcBytes
gtHcLower:
    inc gtHiEntry
    lda gtHiEntry
    cmp #GT_HI_ENTRIES
    bcc gtHcNext
    lda #$ff                    // it did not make the table
    rts

//  A = the slot to take: the rows below it move down one, the score goes in, and
//  the name is filled with the reference's placeholder dots -- the player types
//  over them, on that row, in gtHiNameScreen.
gtHiInsert:
    sta gtHiSlot
    lda #GT_HI_ENTRIES-1        // the last row moves first, so nothing is
    sta gtHiEntry               //   overwritten before it is copied
gtHiInsLp:
    lda gtHiEntry
    cmp gtHiSlot
    beq gtHiInsScore
    sec
    sbc #1                      // copy entry[gtHiEntry-1] into entry[gtHiEntry]
    jsr gtHiPtr
    lda kPtr
    sta srcPtr
    lda kPtr+1
    sta srcPtr+1
    lda gtHiEntry
    jsr gtHiPtr
    ldy #GT_HI_ENTRYLEN-1
gtHiInsCopy:
    lda (srcPtr),y
    sta (kPtr),y
    dey
    bpl gtHiInsCopy             // y wraps to $FF after 0: all ten bytes
    dec gtHiEntry
    jmp gtHiInsLp
gtHiInsScore:
    lda gtHiSlot
    jsr gtHiPtr
    ldy #0
gtHiInsScLp:
    lda gtScore,y
    sta (kPtr),y
    iny
    cpy #3
    bcc gtHiInsScLp
    lda #$2e                    // '.' -- the placeholder the player types over
gtHiInsDotLp:
    cpy #GT_HI_ENTRYLEN
    bcs gtHiInsDone
    sta (kPtr),y
    iny
    jmp gtHiInsDotLp
gtHiInsDone:
    rts

//  Load the saved table. Anything other than a clean 30-byte read leaves the
//  reset values in place: a missing file is a first session, not an error.
gtHiLoad:
    lda #<gtHiFileName
    sta kArg
    lda #>gtHiFileName
    sta kArg+1
    jsr kReuFindFile            // C=1: there is no file
    bcs gtHlDone
    lda reuFsRemain              // the size has to be exactly one table
    cmp #GT_HI_LEN
    bne gtHlDone
    lda reuFsRemain+1
    bne gtHlDone
    lda #<gtHiFileName
    sta kArg
    lda #>gtHiFileName
    sta kArg+1
    lda #<gtHiTab
    sta kPtr
    lda #>gtHiTab
    sta kPtr+1
    jsr kReuLoad
gtHlDone:
    rts

//  Write the table back (see the WARNING above about the tombstone). A failure is
//  ignored: the table is still correct in RAM, and the next save tries again.
gtHiSave:
    lda #<gtHiFileName
    sta kArg
    lda #>gtHiFileName
    sta kArg+1
    jsr kReuDelete              // C=1 when there was nothing to delete
    lda #<gtHiFileName
    sta kArg
    lda #>gtHiFileName
    sta kArg+1
    lda #<gtHiTab
    sta kPtr
    lda #>gtHiTab
    sta kPtr+1
    lda #<(gtHiTab+GT_HI_LEN)   // end address, EXCLUSIVE
    sta kArg+2
    lda #>(gtHiTab+GT_HI_LEN)
    sta kArg+3
    jmp kReuSave

//  The table on the select screen, which is also the attract loop's last screen --
//  so the scores are in front of the player before every game. The reference prints
//  its own at (14,14); ours is at the same column, under a heading.
//  Each row is copied out of the table first: kPrintChar clobbers kPtr, so the
//  entry cannot be read through a pointer while it is being printed.
gtPrintHi:
    lda #GT_C_TITLE
    jsr kSetTextColor
    gtPut(GT_HI_COL, GT_HI_HDR_ROW, gtSHiTitle)
    lda #GT_C_TEXT
    jsr kSetTextColor
    lda #0
    sta gtHiEntry
gtPhiRowLp:
    lda gtHiEntry
    jsr gtHiPtr
    ldy #0
gtPhiCopyLp:
    lda (kPtr),y
    sta gtHiRowBuf,y
    iny
    cpy #GT_HI_ENTRYLEN
    bcc gtPhiCopyLp
    lda gtHiEntry
    clc
    adc #GT_HI_ROW0
    tay
    ldx #GT_HI_COL
    jsr kSetCursor
    //  WARNING: the counters in both loops below live in MEMORY, because
    //  kPrintChar clobbers A, X AND Y (Rule 3) -- counting in Y runs off the
    //  end of the row buffer and prints the $01 fill as a long tail of 'a's.
    lda #0
    sta gtHiIdx
gtPhiDigLp:
    ldy gtHiIdx
    lda gtHiRowBuf,y
    pha
    lsr
    lsr
    lsr
    lsr                         // the high nibble is the more significant digit
    clc
    adc #GT_DIGIT0
    jsr kPrintChar
    pla
    and #$0f
    clc
    adc #GT_DIGIT0
    jsr kPrintChar
    inc gtHiIdx
    lda gtHiIdx
    cmp #3
    bcc gtPhiDigLp
    lda #$20                    // a space between the score and the name
    jsr kPrintChar
    lda #3                      // and the seven name cells
    sta gtHiIdx
gtPhiNameLp:
    ldy gtHiIdx
    lda gtHiRowBuf,y
    bne gtPhiNameOut
    lda #$20                    // an empty cell prints as a space, not as $00
gtPhiNameOut:
    jsr kPrintChar
    inc gtHiIdx
    lda gtHiIdx
    cmp #GT_HI_ENTRYLEN
    bcc gtPhiNameLp
    inc gtHiEntry
    lda gtHiEntry
    cmp #GT_HI_ENTRIES
    bcc gtPhiRowLp
    rts

//  gtHnCursor -- put the cursor on the next free cell of the new entry's name
//  field (row of gtHiSlot, column GT_HI_NAME_COL + gtHiLen).
gtHnCursor:
    lda #GT_HI_NAME_COL
    clc
    adc gtHiLen
    tax
    lda gtHiSlot
    clc
    adc #GT_HI_ROW0
    tay
    jmp kSetCursor              // X = col, Y = row: the one call that keeps both

//  gtHiNameScreen -- the player took a row, so take the name. The reference shows
//  the select screen with the table on it, puts the cursor ON the new row and
//  types straight into it (entername.asm); this does the same.
//  WARNING: NOT kReadString. The kernel's line reader spins on kScanKeyboard
//  without ever yielding (src/kernel/keyboard.inc, readString), so a player taking
//  ten seconds over a name would freeze the whole system; this one yields every
//  pass and echoes each character itself.
//  ---- AND THE CURSOR IS OURS TOO, for the same reason: kReadString's
//  reverse-video cursor is drawn by the spin loop we cannot use. The editor
//  turns on the KERNEL's block cursor instead (kEnableCursor), parked on the
//  name cell -- where it blinks with no further code at all, because the IRQ's
//  blink follows the LIVE cursor of the task that owns the display. kPrintChar
//  saves and clears cursorOn while it prints (Rule 2), so the block is never
//  baked into a character, and the session's own surface keeps its cursor off
//  everywhere else (the task disables it at entry).
//  WARNING: THE PRINTABLE TEST BELOW IS NOT "$20 AND UP": A IS A SCREEN CODE, and the
//  letters live at $01-$1A ($41-$5A with shift held). See the note in gtHnLoop.
gtHiNameScreen:
    jsr kClearScreen
    jsr gtFrameBox
    jsr gtDrawSelect            // the select screen, with the table on it
    lda #GT_C_FLASH
    jsr kSetTextColor
    gtPut(7, GT_HI_MSG_ROW, gtSHiNew)
    lda #GT_C_TITLE
    jsr kSetTextColor
    lda #0
    sta gtHiLen
    jsr gtHnCursor              // the cursor starts on the first name cell--
    jsr kEnableCursor           // --and it BLINKS there (see the note above);
                                //   clobbers A, Y, kPtr, so it goes last
gtHnLoop:
    lda #SYS_YIELD
    jsr syscall                 // never spin (see the note above)
    jsr kReadKey
    beq gtHnLoop                // 0 = nothing this frame
    cmp #$81                    // RETURN: the name is done
    beq gtHnDone
    cmp #$80                    // DEL: take the last character back
    beq gtHnDel
    //  ---- "PRINTABLE" IS NOT "$20 AND UP", BECAUSE A IS A SCREEN CODE.
    //  The letters are $01-$1A ($41-$5A with shift held), so a `cmp #$20 / bcc`
    //  test throws away EVERY unshifted letter and the name cannot be typed.
    //  What is genuinely not a character:
    //  $00 (the '@' key, which the kernel's table shares with the F-keys),
    //  $1B-$1F (S^ '[' ']' 'POUND' <-) and $80+ (CRSR, HOME, STOP, DEL, RETURN --
    //  the last two are handled above).
    cmp #$80
    bcs gtHnLoop                // $80+ = the kernel's special keys
    cmp #$20
    bcs gtHnOk                  // $20-$7f: space, digits, punctuation
    cmp #$01
    bcc gtHnLoop                // $00
    cmp #$1b
    bcs gtHnLoop                // $1b-$1f
    // fall through -- $01-$1a, the letters
gtHnOk:
    ldy gtHiLen
    cpy #GT_HI_NAMELEN          // the reference's TABLE_NAMELENGTH: seven cells
    bcs gtHnLoop                // full: further characters are ignored
gtHnAdd:
    //  ---- NO REPOSITIONING HERE, and none is needed: every path through this
    //  loop leaves the cursor ON the cell the next character goes in (the setup
    //  parks it there for an empty name, a print advances it one cell, and DEL
    //  comes back onto the cell it cleared -- see the note there; without that
    //  second move the cursor ran one cell ahead of gtHiLen from the first DEL
    //  onward, and the echo showed it).
    //  WARNING: TWO ORDERING TRAPS HERE:
    //    * positioning AFTER incrementing gtHiLen puts the echo one cell right of
    //      the cell the character belongs in, so the name does not start under
    //      the field's first '.' placeholder;
    //    * POSITIONING BETWEEN THE KEY AND THE ECHO PRINTS THE ROW. gtHnCursor
    //      ends in `tay`, so it leaves A = the row it moved to -- and for the
    //      first new entry that row is 19, whose screen code is `$13`, the glyph
    //      's'. The row would be echoed as a character for EVERY key.
    sta gtHiNameBuf,y           // Y is the index (gtHiLen has not changed yet)
    jsr kPrintChar              // echo it; the cursor advances onto the next cell
    inc gtHiLen
    jmp gtHnLoop
gtHnDel:
    ldy gtHiLen
    beq gtHnLoop
    dey
    sty gtHiLen                 // the length is now the cell being taken back,
    jsr gtHnCursor              //   so this repositions onto it directly
    lda #$2e                    // '.' -- the field's PLACEHOLDER, not a blank:
    jsr kPrintChar              //   the reference restores the dot too (its
                                //   CONTROLLED_INPUT delete path prints $2e)
    //  ---- WARNING: AND THEN THE CURSOR HAS TO COME BACK ONTO THE CELL THIS DEL JUST
    //  CLEARED. kPrintChar ALWAYS advances one cell, so it leaves the cursor on
    //  NAME_COL+gtHiLen+1 while the next character belongs on NAME_COL+gtHiLen.
    //  Without this move, every key typed after a DEL echoes one cell to the
    //  RIGHT of its buffer slot, and the next DEL re-derives its cell from
    //  gtHiLen -- so it wipes the placeholder the previous DEL wrote and leaves
    //  the letter standing, making DEL look like it deleted the character BEFORE
    //  the last one.
    jsr gtHnCursor              // back onto the cell this DEL emptied
    jmp gtHnLoop
gtHnDone:
    //  ---- THE BLINK GOES OFF BEFORE THE BLOCK IS ERASED, NOT AFTER. A live
    //  block must never be left drawn on the surface (the framebuffer copy a
    //  hand-over makes would bake it in), and once cursorOn is 0 no IRQ tick can
    //  draw it again, so the erase cannot be raced by the blink.
    jsr kDisableCursor          // A, Y, kPtr clobbered; X preserved
    jsr kBlinkReset             // un-draw the block: A, X, Y, kPtr, kArg+7
    lda gtHiSlot
    jsr gtHiPtr
    lda kPtr                    // kPtr = the entry's NAME field (score + 3)
    clc
    adc #3
    sta kPtr
    lda kPtr+1
    adc #0
    sta kPtr+1
    ldy #0
gtHnStoreLp:
    lda #$20                    // space: the table prints all seven cells, so a
    cpy gtHiLen                 //   short name must leave blanks behind it
    bcs gtHnStoreIt
    lda gtHiNameBuf,y
gtHnStoreIt:
    sta (kPtr),y
    iny
    cpy #GT_HI_NAMELEN
    bcc gtHnStoreLp
    jsr gtPrintHi               // show the finished row
    rts

// ==========================================
//  Strings (screen codes)
// ==========================================
//  The panel first, then the attract and select screens -- which are placeholders
//  in the sense that matters: they are text on our own frame, and the artwork pass
//  replaces the drawing routines that place them.
gtSTitle:  .text "gortris"; .byte 0
gtSScore:  .text "score";   .byte 0
gtSLines:  .text "lines";   .byte 0
gtSNext:   .text "next";    .byte 0
gtSLevel:  .text "level";   .byte 0
//  The key list, five rows on the panel: ENTER steps a row (the reference's
//  DOWN), SPACE drops to the floor (this port's own move, worth more).
gtSKey1:   .text "j k  move"; .byte 0
gtSKey2:   .text "a s  turn"; .byte 0
gtSKey3:   .text "entr step"; .byte 0
gtSKey4:   .text "spac drop"; .byte 0
gtSKey5:   .text "p q  stop"; .byte 0
gtSPaused: .text "paused";  .byte 0
gtSMusOff: .text "music off"; .byte 0
gtSBlank6: .text "      ";  .byte 0
gtSBlank9: .text "         "; .byte 0
gtSOver:   .text "game over"; .byte 0
gtSPress:  .text "press key"; .byte 0

//  Attract screen 0 -- title (gtSTitle is reused; the pair below it is the
//  reading of the reference's own title screen).
gtSTitleSub:  .text "a gordonOS tetris"; .byte 0
gtSPressAny:  .text "press any key"; .byte 0
gtSTitleHint: .text "to select a level"; .byte 0

//  Attract screen 1 -- credits.
gtSCred1: .text "logic ported from"; .byte 0
gtSCred2: .text "wiebow/tetris.c64"; .byte 0
gtSCred3: .text "(c) wiebo de wit 2015"; .byte 0
gtSCred4: .text "gordonOS 2026"; .byte 0

//  Attract screen 2 -- the controls, one row each so the columns line up.
gtSCtrlTitle: .text "controls"; .byte 0
gtSCtrl1: .text "j k    move";  .byte 0
gtSCtrl2: .text "a s    turn";  .byte 0
gtSCtrl3: .text "entr   step";  .byte 0
gtSCtrl4: .text "spac   drop";  .byte 0
gtSCtrl5: .text "p      pause"; .byte 0
gtSCtrl6: .text "q      quit";  .byte 0
gtSCtrl7: .text "m      music"; .byte 0

//  Select screen.
gtSSelTitle: .text "select level";      .byte 0
gtSSelKey1:  .text "j k   change level"; .byte 0
gtSSelKey2:  .text "0-9   pick a level"; .byte 0
gtSSelKey3:  .text "entr  start game";  .byte 0
gtSSelKey4:  .text "- +   repeat";      .byte 0

//  Hi scores: the heading the table prints, the line that announces a new entry,
//  and the FS name the table is kept under (11 characters is the FS limit).
gtSHiTitle: .text "high scores";            .byte 0
//  The announcement row also names the key that finishes the name: the block
//  cursor shows WHERE to type, this says how to stop. 26 cells at column 7, so
//  it ends at column 32 -- nowhere near a wrap (Rule 2).
gtSHiNew:   .text "new high score - entr done"; .byte 0
gtHiFileName: .text "gortris.hi";  .byte 0

//  The tune. OUR OWN THREE-VOICE SID ARRANGEMENT of the Tetris theme
//  (Korobeiniki -- a traditional Russian folk song, so the melody itself is in
//  the public domain). The MELODY AND BASS LINE were READ from a fan piano
//  sheet: NinSheetMusic sheet 574, "Tetris - A Theme (Korobeiniki)", piano
//  arrangement by Olimar12345, credited as the sheet prints it to Hirokazu
//  Tanaka's game arrangement, Nintendo (C) 1989. The chords, harmony, waveforms,
//  envelopes and form are ours, and the credit and its terms are in NOTICE and
//  reference/tetris/UPSTREAM.md. `tools/gen-mus.py` writes it, `music/tetris.mus`
//  holds it, and `tools/build-reu.ps1` seeds every *.mus in music/ into the REU
//  FS, so `run sidplay tetris` plays the same file from the shell.
//  ---- WHY WRITTEN AND NOT CONVERTED? A `.sid` is a *program*: 6502 code with its
//  own player, its own IRQ and its own private data layout, not a score. Working
//  out which note sounds when, in which voice, with which envelope, IS the
//  arrangement, so no converter can exist -- there is no `sid2mus` to look for
//  either (0 hits on GitHub, nothing on the web). See reference/tetris/
//  UPSTREAM.md. So the tune was written, not converted.
//  Base names stay <= 7 characters so the 11-character FS name limit holds.
gtTuneName:  .text "tetris.mus";  .byte 0

//  The reference's own level positions (levelselect.asm's levelX/levelY): five
//  columns at 16,18,20,22,24, on rows 7 and 9.
gtLevCol:
    .byte GT_LEV_COL0, GT_LEV_COL0+2, GT_LEV_COL0+4, GT_LEV_COL0+6, GT_LEV_COL0+8
    .byte GT_LEV_COL0, GT_LEV_COL0+2, GT_LEV_COL0+4, GT_LEV_COL0+6, GT_LEV_COL0+8
gtLevRow:
    .byte GT_LEV_ROW0, GT_LEV_ROW0, GT_LEV_ROW0, GT_LEV_ROW0, GT_LEV_ROW0
    .byte GT_LEV_ROW1, GT_LEV_ROW1, GT_LEV_ROW1, GT_LEV_ROW1, GT_LEV_ROW1

// ==========================================
//  State -- all of it in this image, none in ZP
// ==========================================
//  Player-owned (a thread child must treat these as read-only):
gtBoard:    .fill GT_W*GT_H, 0  // 0 = empty, else piece+1
gtRowLo:                        // row*10, so no multiply is ever needed
    .byte 0,10,20,30,40,50,60,70,80,90
    .byte 100,110,120,130,140,150,160,170,180,190
gtPiece:    .byte 0             // current piece id 0..6
gtNext:     .byte 0             // previewed piece id
gtFrame:    .byte 0             // current frame within the piece's run
gtFirstFrame: .byte 0
gtLastFrame:  .byte 0
gtPX:       .byte 0             // 4x4 box position, in board columns/rows
gtPY:       .byte 0
gtFrameLast: .byte 0            // last frameCount we stepped on
gtFallDelay: .byte 0            // frames per row
gtFallTimer: .byte 0
gtPaused:   .byte 0
gtMusOff:   .byte 0             // 1 = the player muted the music ('m')
gtScore:    .fill 3, 0          // 6-digit BCD
gtLines:    .fill 3, 0          // 6-digit BCD (3 would do; one printer for both)
gtMe:       .byte 0             // our slot, for the focus compare

//  The mode machine and the screens (see the header):
gtMode:     .byte 0             // GT_MODE_*
gtAtStep:   .byte 0             // which attract screen is up
gtAtDelayLo: .byte 0            // frames left on the current attract screen, low
gtAtDelayHi: .byte 0            //   byte first so the pair decrements as one
gtFlashDelay: .byte 0           // frames until the level digit flips
gtFlashFlag:  .byte 0           // 1 = digit, 0 = blank
gtSelLevel: .byte 0             // the level chosen on the select screen
gtLevel:    .byte 0             // the level being played (rises with lines)
gtLinesThisLevel: .byte 0       // lines made since the last level up

//  Music (see the header): the game owns the request, the child owns the player.
gtMusOn:    .byte 0             // 1 = play, 0 = be silent  (game -- child)
gtMusQuit:  .byte 0             // 1 = leave for good      (game -- child)
gtMusSlot:  .byte $ff           // the child's slot, $FF = no child
gtMusHush:  .byte 0             // the unfocused second-hush guard
gtReapCnt:  .byte 0             // countdown while waiting for the child to exit
gtTuneLo:   .byte 0             // the player's config block (gtTuneCfg), 0 = none
gtTuneHi:   .byte 0
gtNameLo:   .byte 0             // gtLoadTune in: the FS name pointer
gtNameHi:   .byte 0
gtTuneBufLo: .byte 0            // gtLoadTune out: the window pages, 0 = none
gtTuneBufHi: .byte 0
gtTuneCfg:  .fill 9, 0          // sid.lib's streamed config: the file's FS offset
                                //   then three 256-byte, page-aligned windows
gtMusTune:  .byte 0             // the tune the mode asked for (gtMusicTune)
gtRepeat:   .byte 0             // frames between accepted repeats (INPUTDELAY)
gtRepeatFast: .byte 0           // and for the drop keys (gtSetRepeat)
gtInLast:   .byte 0             // the last accepted code (gtInputFilter)
gtInCount:  .byte 0             // frames left before it may repeat
gtJoyRaw:   .byte 0             // the last joystick read ($DC00, untested)

//  The hi-score table, in the reference's byte order: three BCD score bytes, then
//  seven name cells, per entry.
//  The table starts with three house scores, so a first session has something to
//  chase and the screen has something to show. Ten bytes an entry, no padding and
//  no name terminator: three BCD score bytes (most significant first) and then
//  the seven name cells, in that order, which is exactly how the row sits in
//  memory. The names are one space short of the field, so the last cell is a
//  space -- the field is FIXED width, and an entry that is not ten bytes long
//  would shift every row after it.
gtHiTab:
    .byte $00, $10, $00         // 001000
    .text "gordon "             // seven cells, trailing space included
    .byte $00, $05, $00         // 000500
    .text "gordon "
    .byte $00, $02, $50         // 000250
    .text "gordon "
//  The rows are indexed by a stride of GT_HI_ENTRYLEN, so a length change here
//  would quietly corrupt every row after the first. Assert it instead.
.assert "gortris: the hi-score table is exactly GT_HI_LEN bytes", (* - gtHiTab) == GT_HI_LEN ? 1 : 0, 1
gtHiEntry:  .byte 0             // which entry the table routines are on
gtHiSlot:   .byte 0             // the row a new score took (gtHiInsert)
gtHiLen:    .byte 0             // characters typed into the name so far
gtHiIdx:    .byte 0             // a print loop's counter (never Y: kPrintChar)
gtHiTmp:    .byte 0             // gtHiMul10's scratch
gtHiNameBuf: .fill GT_HI_NAMELEN, 0     // the name being typed
gtHiRowBuf:  .fill GT_HI_ENTRYLEN, 0    // one entry, copied out before printing

//  Scratch -- parent-only, clobbered freely:
gtFrameBuf: .fill 16, 0         // the current frame's cells, decoded from the table
gtLinesMade: .byte 0
gtRowIdx:   .byte 0
gtFullRows: .fill 4, 0
gtIdx:      .byte 0
gtTmpCol:   .byte 0
gtTmpRow:   .byte 0
gtTmpIdx:   .byte 0
gtSrcOff:   .byte 0
gtDstOff:   .byte 0
gtByt:      .byte 0
gtPrevFrame: .byte 0            // gtDrawPreview borrows gtFrame through here
gtGlLo:     .byte 0             // glyph to blit
gtGlHi:     .byte 0
gtScrCol:   .byte 0             // blit origin for gtBlitFrame, in screen cells
gtScrRow:   .byte 0
gtFlashCnt: .byte 0
gtLead:     .byte 0
gtBcdIdx:   .byte 0
gtPbBuf:    .fill 3, 0
gtWaitCnt:  .byte 0
gtWaitLast: .byte 0
gtHardRows: .byte 0             // rows the last hard drop travelled (gtHardDrop)
gtOverStep: .byte 0             // the game-over animation's step (GT_OVER_TEXT)
gtOverWait: .byte 0             // frames a finished pass is held on screen
gtOverLeft: .byte 0             // board row it is filling/clearing, bottom up
gtOverGlLo: .byte 0             // the glyph that pass paints (block, then blank)
gtOverGlHi: .byte 0
gtPtsOff:   .byte 0             // gtPts row for gtScoreLines
gtLvlCnt:   .byte 0             // level+1 adds left in that loop
