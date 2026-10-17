// ==========================================
//  grpaint.asm -- "grpaint" task (relocatable, single-instance)
// ==========================================
//  Wraps grpaint.inc -- the 1351-driven paint program.
//  It takes a bitmap canvas, puts the 1351's pointer
//  on it as two sprites, and paints with the left button: the segment the pointer
//  covered since the previous displayed frame is drawn into the canvas, one pixel
//  at a time, and each pixel commits its cell's colour.
//  Nominal origin $5000 (page-aligned; delta_lo = 0 for any pool).
////  Every action that changes the canvas -- a stroke, a shape, a fill, a text entry,
//  the CLEAR tile, a palette press and a mode toggle -- leaves a state behind in an
//  REU ring, so 'u' and the band's LEFT arrow step back through it and 'r' and the
//  RIGHT arrow step forward again. Five steps each way; see the history section in
//  grpaint.inc for what a state is and where it lives.
////  The command line is `run grpaint <base> [border bg fg | border bg c1 c2 c3]`. The
//  name picks <base>.pic, and the values are the colours a NEW picture starts with --
//  three of them for hires (border, paper, ink) and five for MC (border, background,
//  c1, c2, c3), so the COUNT is the mode. A picture that already exists is a load,
//  and its own colours win, so the values are then refused rather than ignored.
//
//  WARNING: SINGLE-INSTANCE, deliberately -- KF_RE_RUNNABLE is NOT set, so a second
//  `run grpaint` is refused (`? already running`, error 11) and the instance that
//  holds the canvas keeps it. Its live footprint is 71 of the pool's 109 pages:
//  31 code + gfx.lib 27 + sprite.lib 1 + mouse.lib 2 (all eager) + filesys.lib 10,
//  which its cold start loads to open the band's own file. Two copies plus the shell
//  are therefore the WHOLE pool, and the loader's pre-need sum counts the resident
//  libraries in full for the second load -- so a re-runnable grpaint would not start
//  a second instance at all: it would EVICT the first one, which is the copy that is
//  running.

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg + kernel-data aliases are defined
//  in tools/assemble-tasks.asm)

// Instance-buffer layout. The pointer is 16-bit and it is STATE, so it lives in
// the instance buffer the loader allocates for this task and never in the code
// image, which is the file's own bytes.
// GP_WX/GP_WY/GP_XH are the window position both sprites are placed from, kept
// here rather than in a register because kSpritePos clobbers all three.
//
// GP_TRK_X/GP_TRK_Y are the per-frame tracker that keeps the pot's own noise out
// of the position. Each axis gets three bytes -- uPrev, sRun, sMin -- and they
// must stay in that order and be contiguous, because gpTrack and gpSettle walk
// them with iny/dey from a single base.
//
// One frame's samples are followed as offsets from the frame's first sample
// (sRun) and their minimum is kept (sMin). The position has been moved by every
// delta of the frame, so it carries the noise on the frame's LAST sample; sRun -
// sMin is exactly that much, and it is taken back at the end of the frame --
// along with the previous frame's figure, which is already subtracted. The result
// is that a frame moves the pointer by the change in that minimum, which is the
// reading with no noise on it.
.const GP_POS       = 0          // 4: x lo/hi, y lo/hi -- canvas coordinates
.const GP_FRAME     = 4          // 1: the displayed frame the pointer last moved on
.const GP_WX        = 5          // 1: the window x low byte (scratch)
.const GP_WY        = 6          // 1: the window y (scratch)
.const GP_XH        = 7          // 1: its 9th bit, 0 or 1 (scratch)
.const GP_TRK_X     = 8          // 3: uPrev, sRun, sMin -- the x axis
.const GP_TRK_Y     = 11         // 3: ...and the same three for y

// The stroke. GP_BTN is the button byte MOUSE_READ hands back -- ACTIVE LOW, PA4
// (the fire line) is the left button and PA0 is the right -- and GP_DRAW is 1 while
// an operation is in progress, so a press is an event rather than a level: for the
// pencil that is the button being held, for the line tool a click waiting for its
// second one.
// GP_LAST is where the stroke has been drawn up to, which is also the walk's
// cursor; it holds x as lo/hi because a canvas x of 319 does not fit a byte, and y
// as one byte because gpClamp1 keeps it under 200.
// The rest is one Bresenham walk's state: the two steps, which axis is the longer
// one (GP_MJ), the pixels left to plot (GP_CNT), the error (GP_ERR) and the two
// spans. All of it is state, so all of it lives here rather than in the code image.
.const GP_BTN       = 14         // 1: the buttons as last read (active low)
.const GP_DRAW      = 15         // 1: a stroke, or an anchor waiting for its second click
                                 //    -- and GP_DRAW_DRAG while a rectangle is being dragged
.const GP_LAST      = 16         // 3: the pixel the stroke has reached (x lo, x hi, y)
.const GP_SX        = 19         // 1: the x step, $01 or $ff
.const GP_SXH       = 20         // 1: ...and its sign extension into the high byte
.const GP_SY        = 21         // 1: the y step, $01 or $ff
.const GP_MJ        = 22         // 1: 0 = x is the major axis, 1 = y is
.const GP_CNT       = 23         // 2: major-axis steps still to take
.const GP_ERR       = 25         // 2: the walk's error, always below the major span
.const GP_AJ        = 27         // 2: the major span
.const GP_AI        = 29         // 2: the minor span

// The picker's state: the ink it picks, whether the band is up, and which band cell
// the pointer is on ($ff = none; 0-15 are the swatches, 16-25 the tools). GP_INK is
// the colour the pencil draws with, and the pointer's INTERIOR is coloured with it,
// so the current ink reads with the band down -- and with the band up the interior
// shows the swatch under the pointer, which is the only highlight there can be: a
// swatch's colour IS its matrix nibble, so marking it in the band would mean drawing
// over the colour it marks.
// BOTH buttons read GP_PREV, and for one reason: the band is toggled by a right PRESS
// and selected from by a left one, and the shape tools count clicks -- a held button
// reads the same on every frame, so a level cannot tell a press from a button that is
// simply still down. "A press" is that bit SET in GP_PREV and CLEAR in GP_BTN, because
// the buttons are ACTIVE LOW.
.const GP_INK       = 31         // 1: the ink, 0-15
.const GP_MENU      = 32         // 1: 1 while the UI band is up
.const GP_MSEL      = 33         // 1: the band cell under the pointer, $ff = none
.const GP_TOOL      = 34         // 1: the tool, one of GP_TOOL_*
.const GP_PREV      = 35         // 1: the buttons as the previous frame read them
.const GP_ANCH      = 36         // 3: the shape tools' anchor (x lo, x hi, y)
// The UI band's file record, as kReuOpen left it: size lo/hi then base lo/mid/hi.
// It is the only thing about the band the task remembers -- the image itself is in
// the file and the band it covers is kept in the REU -- so the right button is two
// kReuReadAt calls and a restore.
.const GP_UI        = 39         // 5: the HIRES band file's record (size, then base)
                                 //   GP_UI_MC is the MC one, and gpUiRecOfs is the pair --
                                 //   the record for the mode is chosen by GP_MODE, so the
                                 //   record follows a load or a step as well as a press
// The rectangle walk's own state. It cannot live in kArg: the walk plots a cell at a
// time with kPlot, which builds REU addresses in kArg and so clobbers it between two
// cells. X1/X2 are the left and right edges in pixels, Y1/Y2 the top and bottom rows,
// CX/CY the cell counts (one per 8 pixels, the partial last cell included), and N and R
// the live counters of a span and of a filled box's rows. R is separate from N because
// every span reloads N from the saved count.
.const GP_WK_X1     = 44         // 2: the left edge (x lo, x hi)
.const GP_WK_X2     = 46         // 2: the right edge
.const GP_WK_Y1     = 48         // 1: the top row
.const GP_WK_Y2     = 49         // 1: the bottom row
.const GP_WK_CX     = 50         // 1: the cells across
.const GP_WK_CY     = 51         // 1: the cells down
.const GP_WK_N      = 52         // 1: the live cell counter of one span
.const GP_WK_R      = 53         // 1: the live row counter of a filled box
.const GP_WK_T      = 54         // 2: the cell counts' scratch. NOT kArg+4/+5: that is
                                 //    the tick's per-switch byte (the width's low
                                 //    half here), so task state may not live there
.const GP_WK_RAD    = 56         // 1: the circle band's radius. Saved like the box's
                                 //    edges are: the drag takes the LAST frame's band
                                 //    off before it puts this frame's on, so the erase
                                 //    has to read a value the refresh has not touched
.const GP_WK_RJ     = 57         // 2: the circle's own scratch -- |dx|, 16-bit. Not
                                 //    GP_AJ: that is the line tool's major span
.const GP_CC_CX     = 59         // 2: the cell walk: the centre x (lo/hi)
.const GP_CC_CY     = 61         // 1: ...its centre row
.const GP_CC_R      = 62         // 1: ...and the radius it walks
.const GP_CC_X      = 63         // 1: the recursion's x
.const GP_CC_Y      = 64         // 1: ...and its y
.const GP_CC_D      = 65         // 2: ...and its 16-bit decision term
.const GP_CC_TMP    = 67         // 2: its term scratch
.const GP_CC_V      = 69         // 1: the point's row offset
.const GP_CC_W      = 70         // 1: ...and its x offset
.const GP_CC_PX     = 71         // 2: the point's x
.const GP_CC_LPX    = 73         // 2: ...and its mirror's
.const GP_CC_COL    = 75         // 1: the cell column being tested
.const GP_CC_NROW   = 76         // 1: the cell row being tested
.const GP_CC_ROW    = 77         // 1: the cell row last plotted, $ff = none yet
.const GP_CC_RCOL   = 78         // 1: ...the right column last plotted in it
.const GP_CC_LCOL   = 79         // 1: ...and the left one
.const GP_CC_INV    = 80         // 1: $ff while the top half is walked, $00 after
.const GP_CC_CIN    = 81         // 1: ...the carry that pairs with it
.const GP_CC_ARC    = 82         // 1: 0 = the steep arc, 1 = the shallow one
.const GP_CC_PROW   = 83         // 1: the point's own row -- the pixel the walk plots

// The pixel write op -- kernel DATA, not a call, and the one kernel byte this task
// writes directly. gpDgBand sets it with the interrupts off because the switch path
// CLEARS it: a switch landing between the store and the shape call would strip the XOR
// and leave the shape written SET. See docs/memory-map.md and docs/lib-gfx.md.
.const GP_GFXOP     = $ED5D

// How many times the 1351 is polled in one displayed frame. It is polled several
// times rather than once because the device reports movement against a WRAPPING
// position: the more often it is asked, the smaller each delta is and the less a
// fast flick can be mistaken for a shorter one.
.const GP_READS    = 8

// The canvas, and the window it is shown in: the 40-column window's left edge is
// sprite x = 24 and its first line is sprite y = 50 (docs/lib-sprite.md; sprdemo
// bounds its arena at 50..229 for the same reason), and its only bounds are
// 0..319 by 0..199 -- which is the canvas itself, so both offsets are needed to
// place a sprite over a canvas pixel.
.const GP_W     = 320
.const GP_H     = 200
.const GP_WIN_X = 24            // the window's left edge is sprite x = 24
.const GP_WIN_Y = 50            // ...and its top line is sprite y = 50
.const GP_CX    = GP_W / 2       // 160: the pointer starts centred
.const GP_CY    = GP_H / 2       // 100
// The pointer is drawn as TWO sprites overlaid at one position: GP_SL_FILL is the
// interior inside the arrow and takes the INK -- or, while the palette is up, the
// colour of the swatch the pointer is over -- and GP_SL_OUT is the 1px ring around
// it, which takes a FIXED colour. The ring is the pointer's outline, so it may not
// follow the ink: 0 is a legal ink and it is the canvas's own background, and an
// outline drawn in it erases itself. The two frames never draw the same pixel, so
// sprite priority does not arise.
.const GP_SL_FILL = 0
.const GP_SL_OUT  = 1
.const GP_FR_FILL = 0            // pointer.spr frame 0: the interior
.const GP_FR_OUT  = 1            // pointer.spr frame 1: the ring
.const GP_RING_COL = 1           // the ring: white, fixed, never the ink
.const GP_INK_DEF  = 1           // the ink until the picker picks another
.const GP_BTN_LEFT = %00010000   // PA4 of $DC00: the fire line, active low
.const GP_BTN_RIGHT = %00000001  // PA0: the joystick UP line, active low

// The UI band is the canvas's BOTTOM TWO cell rows, full width, and that placement is
// the whole point: a bitmap's memory is CELL-BLOCK -- the eight bytes of one 8x8 cell
// are contiguous and the cells run row-major over those blocks -- so a cell row of the
// screen is a contiguous 320-byte run and the band's two are one run of 640, which is
// ONE transfer for the pixels and one for the matrix. A floating panel is neither,
// which is why it needs per-row arithmetic and a stride to get wrong.
// WARNING: A RASTER ROW OF THE BAND IS NOT A CONTIGUOUS RUN. Consecutive bytes are
// eight pixels apart and a run of eight bytes is one cell's eight raster rows, so the
// band's pictures are authored in cells, in the file, not as rows of pixels.
// The band's CONTENTS ARE A FILE in the REU filesystem (GP_UI_FILE): the palette and
// the tools as 640 pixel bytes followed by their 80 matrix bytes, exactly what the
// band holds, so showing the UI is a read and hiding it is a restore. Nothing about
// the UI is code -- no picture, no palette, no composition.
// The palette fills the band's left 8 cells across BOTH rows -- sixteen swatches of
// one 8x8 cell each, colours 0-7 along the top row and 8-15 along the bottom, in the
// order of the C64 palette. The tools follow it as 2x2-cell tiles, each carrying a
// 1px border in its own picture: a tile is 16 pixels square, so the tile a pointer is
// on is its distance from the palette's right edge in cells, halved. The PENCIL is
// the first tile -- it is what a fresh canvas starts with -- and then, in order, the
// line, the box, the filled box, the circle, the fill, the three brushes and the clear.
.const GP_PK_ROW   = 23          // the band's first cell row (the canvas's last two)
.const GP_PK_CW    = 40          // cells across the band
.const GP_PK_SW    = 16          // swatches in the palette
.const GP_PK_PW    = 8           // ...which is eight cells across
.const GP_PK_PH    = 2           // ...and both rows of the band
.const GP_PK_SY0   = GP_PK_ROW*8 // 184: the band's first pixel row
.const GP_PK_SH    = GP_PK_PH*8  // 16: ...and it is two cell rows tall
.const GP_PK_CR    = GP_PK_CW*8  // 320: a cell row of the canvas: forty 8-byte cells
.const GP_PK_SW4   = GP_PK_PW*8  // 64: how wide the palette is in pixels
.const GP_PK_SCR0  = $2000 + GP_PK_SY0*40       // $3CC0: the band's first pixel byte
.const GP_PK_MAT0  = $0400 + GP_PK_ROW*40       // $0798: ...and its first matrix byte
.const GP_PK_LEN   = GP_PK_CW*GP_PK_SH          // 640: pixel bytes in the band
.const GP_PK_MLEN  = GP_PK_CW*GP_PK_PH          // 80: matrix bytes in it
.const GP_PK_TX0   = GP_PK_SW4                  // 64: the first tool tile, at its right
.const GP_PK_TN    = 13          // tool tiles the band carries: the pencil, the line,
                                 //   the box, the filled box, the circle, the fill, the
                                 //   three brushes, the text, the clear, the save and the
                                 //   MODE tile -- twelve tools and one command
.const GP_PK_TW    = 16          // a tile is 2x2 cells: sixteen pixels across
.const GP_PK_TW4   = GP_PK_TX0 + GP_PK_TN*GP_PK_TW  // 272: the band's art ends here, where
                                 //   the four slot chips start
.const GP_PK_CHW   = 8           // a chip is ONE cell: eight pixels across
.const GP_PK_CN    = 4           // ...and there are four of them, one a palette slot
.const GP_PK_IN    = 2           // ...and the band ends with two ARROWS: undo across the
                                 //   top cell row and redo across the bottom one, so
                                 //   they are row entries and not column ones
.const GP_PK_TC    = GP_PK_TN*GP_PK_TW/GP_PK_CHW     // 26: the tiles' cells past the
                                 //   palette -- the chips follow them, one cell each
.const GP_PK_HALF  = 256/GP_PK_CHW                   // 32: the cell column x = 256 is at.
                                 //   The cell across is the x DIVIDED BY EIGHT, and the
                                 //   x is nine bits: a test that reads only its low byte
                                 //   calls every press past x = 255 cell 0, i.e. a swatch
.const GP_PK_CH4   = GP_PK_TW4 + GP_PK_CN*GP_PK_CHW  // 304: the four chips' entries end
                                 //   here, where the arrows' own two columns start
.const GP_PK_END4  = GP_PK_CH4 + GP_PK_IN*GP_PK_CHW  // 320: ...and the band's ENTRIES end
                                 //   at its own right edge. Both arrows are TWO cells
                                 //   wide and one cell row tall, so they share these two
                                 //   columns and the ROW says which of them it is
                                 //   WARNING: neither 272 nor 304 is usable as an
                                 //   immediate -- the assembler emits `cmp #272` as
                                 //   `cmp #$10` -- so the hit test reads x's HIGH byte

// The tool entries are numbered on from the palette's swatches, so one byte carries
// what the pointer is on: 0-15 a colour, 16 and up a tool. A tile's entry number
// minus GP_PK_TLINE IS its tool number, which is what makes the picker's arithmetic
// one subtraction and the mark's the same addition back. The PENCIL is the first tile,
// because it is what a fresh canvas starts with: the default is the leftmost entry,
// and the tool in use is the one nearest the palette's edge. The line follows it, and
// then the two rectangles -- the box's outline and its filled form, which is a tool of
// its own rather than a modifier, because the mouse is the only control there is.
.const GP_PK_TLINE = 16          // the entry number of the first tile: the pencil
.const GP_PK_CHIP0 = GP_PK_TLINE + GP_PK_TN     // 29: the first slot chip's entry
.const GP_PK_ICON0 = GP_PK_CHIP0 + GP_PK_CN     // 33: the undo arrow -- the first entry
                                 //   that is neither a colour nor a tool
.const GP_PK_ENT   = GP_PK_ICON0 + GP_PK_IN     // 35: the band's entries end here
.const GP_PK_CHIP_M = GP_PK_MAT0 + GP_PK_PW + GP_PK_TC  // the first chip's matrix byte
                                 //   (the band's top cell row, the chip's one cell)
.const GP_PK_ICON_M = GP_PK_CHIP_M + GP_PK_CN   // ...and the undo arrow's, four cells on
.const GP_PK_REDO_M = GP_PK_ICON_M + GP_PK_CW   // ...and the REDO arrow's, one cell row
                                 //   down: the arrows share the band's last two columns,
                                 //   so the ROW is what tells the two cells apart
// What a MARK writes into a cell. A cell's colour is its matrix nibble, and the band
// image holds the drawing grey (tools/gen-ui.py's tile tuple); a mark turns the drawing
// WHITE, which is the whole feedback for "in use", for "unsaved" and for an arrow that
// has a step to take. A mark is only ever ADDED -- every caller re-reads the band's
// matrix from the image first -- so an unmarked cell keeps the image's own colour.
.const GP_MK_WHITE = $10
// The save tile's WARNING flash: the phase comes from one BIT of the display's frame
// counter, so it is the display's own clock (50.1 Hz) and not the scheduler's tick, and
// the tile is left dark for exactly as long as it is marked.
.const GP_FLASH_BIT = $20        // bit 5: 32 frames each way -- 0.64 s marked and 0.64 s
                                 //   dark at the display's 50 Hz, a calm 0.8 Hz flash
.const GP_TOOL_PEN     = 0
.const GP_TOOL_LINE    = 1
.const GP_TOOL_BOX     = 2
.const GP_TOOL_FILLBOX = 3
.const GP_TOOL_CIRCLE  = 4
.const GP_TOOL_FLOOD   = 5       // one click: kFloodFill from the pointer
.const GP_TOOL_BRUSH1  = 6       // freehand, marking a blob: 6, 7 and 8 are the three
.const GP_TOOL_BRUSH3  = 8       //   sizes, in the order of the band's tiles
.const GP_TOOL_TEXT    = 9       // one press places a pen, and the keys write from it
.const GP_TOOL_CLEAR   = 10      // one press: kClearBitmap over the whole canvas
.const GP_TOOL_SAVE    = 11      // one press: write the picture, when it has changed
.const GP_TOOL_MODET   = 12      // the MODE tile: a COMMAND like the clear, never a tool.
                                 //   Its art says what a press does, so the tile reads
                                 //   "to MC" in the hires band and "to hires" in the MC one

// The surface's bitmap tag: screenKind's own two values, so GP_MODE is comparable with a
// .pic's mode byte. kAllocScreen and kSetGfxMode take the mode as an offset from the
// hires tag -- 0 hires, 1 multicolour -- which is one subtraction away.
.const GP_MODE_STD = 2
.const GP_MODE_MC  = 3
// The chips' frame colour: the low nibble of a chip cell, so the block's own colour is
// the high one and the frame stays readable whatever the slot holds. White, like a mark.
.const GP_CHIP_FRAME = 1

// The text pen's cursor: a screen code, XORed at the pen so it inverts a whole 8x8 block.
// $A0 is the all-ones glyph in every font in charsets/ (checked), so the cell under the
// pen goes to its inverse -- a block cursor, and exactly reversible by writing it again.
.const GP_CARET = $a0
.const GP_BR_X     = 84          // 2: the blit's left column (x lo/hi)
.const GP_BR_XS    = 86          // 2: ...and the column its row starts on
.const GP_BR_Y     = 88          // 1: the blit's top row
.const GP_BR_B     = 89          // 1: the glyph byte being walked
.const GP_BR_N     = 90          // 1: the columns left in it
.const GP_BR_R     = 91          // 1: the glyph row
.const GP_BR_SL    = 92          // 3: the walk's cursor, parked across the blit
.const GP_BR_SH    = 93
.const GP_BR_SY    = 94

// The save/load state, and the ARGUMENTS the task is spawned with.
//  The loader delivers args at offset 0 (KF_TAKES_ARGS), copying up to MAX_TASK_ARGS
//  bytes plus a terminator -- so the first 65 bytes of the instance buffer belong to
//  it, and the state below 65 is written by that copy before a single instruction of
//  ours runs. That is survivable ONLY because every one of those fields is
//  INITIALISED afterwards, before it is read: gpInit sets the position, the frame, the
//  trackers, the buttons, the cursor, the ink, the band and the tool; the walks write
//  their own scratch; kReuOpen fills the UI record. GP_BTN is the one that was NOT and
//  now is -- its first read copies it into GP_PREV, and a name's own bytes there would
//  read as a phantom press on the first frame.
//  The NAME, the record and the header therefore sit ABOVE the arguments' reach, and
//  the name is built into GP_NAME before anything else runs, because the state the
//  args overwrite is the state gpInit is about to write.
.const GP_NAME      = 95         // 12: "<base>.pic" + NUL, built from the argument
.const GP_FREC      = 107        // 5:  the file record kReuOpen / kReuCreate leave
.const GP_HDR       = 112        // 32: the .pic header: the constants below, then the
                                 //     palette and the label a load refreshes
.const GP_DIRTY     = 144        // 1:  the canvas has changed since the last save
.const GP_TEXT      = 145        // 1:  1 while the text pen is down: the keys are text
.const GP_PEN_X     = 146        // 2:  the pen's pixel x, lo/hi -- a PIXEL position, so
                                 //     text starts anywhere on the canvas, not on a cell
.const GP_PEN_Y     = 148        // 1:  ...and its pixel y
.const GP_PEN_X0    = 149        // 2:  the x this entry's line starts at, which a wrap
                                 //     comes back to
// The mode, and the palette it selects from. GP_MODE is the SURFACE's tag, in
// screenKind's own values, so it can be compared with a .pic's mode byte and handed to
// kSetGfxMode by subtracting the hires tag -- and so a mode is never translated twice.
// GP_SLOT is the palette slot the picker has selected: what a swatch press assigns, and
// the source a mark claims (GP_VAL, the same index: 0 is the background, 1-3 are the
// cell's own colours). GP_MB is the palette itself, four colours a cell's values select.
.const GP_MODE      = 151        // 1:  the surface's tag: GP_MODE_STD or GP_MODE_MC
.const GP_SLOT      = 152        // 1:  the selected palette slot, 0-3
.const GP_VAL       = 153        // 1:  the value a mark claims in MC, 0-3 (GP_SLOT's index)
.const GP_MB        = 154        // 4:  the palette: bg0, c1, c2, c3
.const GP_CHIP_SEL  = 158        // 1:  the chip the band's ring is drawn on, so the slot a
                                 //     swatch press assigns is visible (gpChips)

// The history's bytes: the ring's three state numbers, the text pen's own "something has
// been written" flag, which is what keeps an entry that typed nothing from leaving a
// step behind, and the mark flag, which says marks are on the canvas that no state holds
// yet (the line's anchor click, and a stroke still under the button). The numbers are
// INDICES into the ring, 0-GP_U_N-1, and GP_U_HEAD is the state the canvas on screen IS
// -- the invariant every entry point reads and the only thing a step writes.
.const GP_U_HEAD    = 159        // 1:  the state the canvas on screen is
.const GP_U_NEW     = 160        // 1:  the newest state the ring holds
.const GP_U_OLD     = 161        // 1:  ...and the oldest
.const GP_TWROTE    = 162        // 1:  1 while the current text entry has written a glyph
.const GP_U_DRAW    = 163        // 1:  1 while a mark is on the canvas that no state holds
// The band's own face, one FILE per mode, and BOTH records are resolved at spawn. That
// is what keeps a filesystem scan off every press: the MODE tile's press is a frame long
// if it has to scan for the other band, and a frame long enough to outlast the pointer's
// sampling is a press that never happened. GP_UI is the hires band's record and GP_UI_MC
// the multicolour one; gpUiRecOfs is the pair, and the one the mode in hand selects.
.const GP_UI_MC     = 164        // 5: the MC band file's record
// The quit warning: 'q' asks once when there is work only this machine has. GP_WARN is
// that question being on the table (a second 'q' answers it) and GP_FLASH is the save
// tile's half of the flash it makes.
// GP_MENU0 is the band's state as the frame BEGAN, before gpPicker can put it away, so a
// press made with the UI up is told apart from one made with it already down. The flood
// fill reads it, because the fill must not act on the press that dismisses the band.
.const GP_WARN      = 169        // 1: 1 while a 'q' has warned and the next one leaves
.const GP_FLASH     = 170        // 1: the save tile's flash phase: 0 dark, 1 marked
.const GP_MENU0     = 171        // 1: the band's state at the frame's start (GP_MENU)
.const GP_DATA_SIZE = 172        // ...which is where the instance buffer ends

// The ring's own bank and layout. A state is a .pic PAYLOAD -- the three regions of
// gpPicTab, at the offsets and in the order a file carries them (pixels at +32, matrix at
// +8032, colour RAM at +9032), so a snapshot walks the table a save and a load already
// walk -- followed by the two things a payload does not carry and a picture still owns:
// the mode and the four palette colours. Six states, $2800 apart (a bitmap slot's stride,
// so every offset keeps its low byte 0), are five steps of undo and five of redo. The bank
// is the REU's top FULL one, which the filesystem's own capacity reserves to grpaint --
// reuFsCapacity stops one bank short of the REU's top for exactly this
// (docs/memory-map.md) -- so the ring can never overwrite a file. A machine with fewer than
// GP_U_BANK+1 banks has no such bank and gets no history at all: gpUndoCap refuses, and
// nothing is ever written.
.const GP_U_BANK    = 251        // $FB0000-$FBFFFF: the top full REU bank
.const GP_U_N       = 6          // states in the ring: five steps each way
.const GP_U_EXT     = 10032      // 7:    behind the payload, which a .pic makes 10,032
.const GP_U_EXT_LEN = 7          // how much of the instance buffer rides with a state
.const GP_U_PAL_OFF = 3          // the palette's offset in that block (GP_MODE, GP_SLOT,
                                 //   GP_VAL, then GP_MB..GP_MB+3)
.const GP_U_STRIDE  = $2800      // 10240: one state's own space, a bitmap slot's stride

// The name is 11 characters at most (the FS limit), so a base name of 7 -- and the
// loader's 64-byte argument area plus its terminator must end BELOW the name:
// GP_NAME at 95 is comfortably past 65, and this is what keeps it there.
.assert "the save/load state must sit above the loader's argument area", (GP_NAME > 65) ? 1 : 0, 1
.assert "the pen's state fits the instance buffer", (GP_DATA_SIZE >= GP_PEN_X0 + 2) ? 1 : 0, 1
.assert "the history's fields fit the instance buffer", (GP_DATA_SIZE >= GP_U_DRAW + 1) ? 1 : 0, 1
.assert "both band records and the quit warning fit the instance buffer", (GP_DATA_SIZE >= GP_FLASH + 1) ? 1 : 0, 1
.assert "the frame's band state fits the instance buffer", (GP_DATA_SIZE >= GP_MENU0 + 1) ? 1 : 0, 1
.assert "the history's fields are above the loader's argument area", (GP_U_HEAD > 65) ? 1 : 0, 1
.assert "the band's art and its chips fit one cell row of the canvas", (GP_PK_END4 <= GP_PK_CR) ? 1 : 0, 1
//  The entry map is an ABI between three things that cannot see each other: the band's ART
//  (tools/gen-ui.py places the tiles), the hit test (which counts cells) and the dispatcher
//  (which compares entry numbers). A mismatch is a press that does nothing at all, so each
//  edge is pinned here rather than discovered at the canvas.
.assert "the MODE tile is the 13th tile's entry", ((GP_PK_TLINE + GP_TOOL_MODET) == 28) ? 1 : 0, 1
.assert "the high half begins at cell 32", (GP_PK_HALF == 32) ? 1 : 0, 1
.assert "the tiles' cells end where the chips start", (GP_PK_TC == 26) ? 1 : 0, 1
.assert "the four chips follow the thirteen tiles", (GP_PK_CHIP0 == 29) ? 1 : 0, 1
.assert "...and the undo arrow follows the four chips", (GP_PK_ICON0 == 33) ? 1 : 0, 1
.assert "...so the entries end after the redo arrow", (GP_PK_ENT == 35) ? 1 : 0, 1
.assert "the chips end at pixel 304", (GP_PK_CH4 == 304) ? 1 : 0, 1
.assert "the stacked arrows fill the band's last two columns", (GP_PK_END4 == GP_PK_CR) ? 1 : 0, 1
.assert "the first chip's matrix byte is column 34 of the band's top row", (GP_PK_CHIP_M == GP_PK_MAT0 + 34) ? 1 : 0, 1
.assert "the undo arrow's first cell is column 38 of it", (GP_PK_ICON_M == GP_PK_MAT0 + 38) ? 1 : 0, 1
.assert "...and the redo arrow's is column 38 of the band's second row", (GP_PK_REDO_M == GP_PK_MAT0 + GP_PK_CW + 38) ? 1 : 0, 1
//  The states must fit the reserved bank, and one state must hold the payload AND the
//  extras that follow it.
.assert "six states fit the reserved bank", (GP_U_N * GP_U_STRIDE <= $10000) ? 1 : 0, 1
.assert "a state holds the payload and its extras", (GP_U_EXT + GP_U_EXT_LEN <= GP_U_STRIDE) ? 1 : 0, 1
.assert "the palette rides inside the extras block", (GP_U_PAL_OFF + 4 <= GP_U_EXT_LEN) ? 1 : 0, 1

// What GP_DRAW holds while a rectangle tool is being dragged. It is a value of its own
// rather than 1 so that a routine which must take a live band off the canvas (gpDgCancel)
// can tell a drag from the pencil's stroke and the line's waiting anchor -- both of which
// are 1 as well, and neither of which is its to end.
.const GP_DRAW_DRAG   = 2

// The band's own bytes are the only thing the REU holds, and only while the UI is up:
// saving them is what lets the close put the canvas's two bottom rows back, because
// the file's image covers them. 640 pixel bytes at $3CC0 and 80 matrix bytes at $0798
// go to bank 0 -- $D000-$FFFF is 12KB the REU layout leaves free (the filesystem
// starts at bank 5 $9000 and the malloc shadow at bank 252), so this costs the
// filesystem no capacity. Recorded in docs/memory-map.md's REU layout.
.const GP_REU_BANK    = 0
.const GP_REU_SAV_PIX = $D000    // 640: the band, saved while the UI is up
.const GP_REU_SAV_MAT = $D280    // 80:  ...and its matrix

grpaintBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE | KF_TAKES_ARGS  // +2: flags -- NOT KF_RE_RUNNABLE: one
                                 //      canvas, one pointer, and two copies would
                                 //      not fit (see the file header)
                                 //      + the .pic name, which arrives as an argument
    .word $5000                  // +3:  origin (nominal)
    .word grpaintEnd - grpaint   // +5:  code size = end - ENTRY LABEL
    .byte GP_DATA_SIZE, 0, 0     // +7:  data size (the pointer's state), ZP base,
                                 //      ZP len (none)
    .word (1 << LIB_SPRITE) | (1 << LIB_MOUSE) | (1 << LIB_GFX) | (1 << LIB_FILESYS)
                                 // +10: the pointer, its device, kPlot for the
                                 //      stroke itself, and the file the UI band
                                 //      is read out of
    .byte grpaintNameEnd - grpaintName  // +12: name length
grpaintName:
    .text "grpaint"
grpaintNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - grpaintBinStart), 0

.import source "grpaint.inc"
.label grpaintEnd = *
