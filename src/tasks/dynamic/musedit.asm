// ==========================================
//  musedit.asm -- tracker-style editor for COMPUTE!'s Sidplayer (.mus) tunes
// ==========================================
//  Wraps musedit.inc -- the task body, its strings and the generated piano glyphs.
//  Nominal origin $5000 (page-aligned; delta_lo=0 for any pool page).

.encoding "screencode_mixed"

// (syscall alias + dataPtr/kPtr/kArg defined in tools/assemble-tasks.asm)

// The instance buffer's size. Declared here because the header needs it before
// the body is imported; musedit.inc's zero loop uses the same constant.
.const MU_DATA_SIZE = 53

museditBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE | KF_TAKES_ARGS | KF_NOT_EVICTABLE
                                 // +2:  flags: it holds unsaved work, so it is pinned
    .word $5000                  // +3:  origin (page-aligned, nominal)
    .word museditEnd - museditMain // +5: code size (the entry is the first label)
    .byte MU_DATA_SIZE, 0, 0     // +7:  data size; no ZP here (the player child declares $02-$0A)
    .word (1 << LIB_FILESYS) | (1 << LIB_SID) | (1 << LIB_SPRITE) | (1 << LIB_MOUSE)
                                 // +10: filesys 10 + sid 13 lazy, sprite 1 + mouse 2 eager
    .byte museditNameEnd - museditName // +12: name length
museditName:
    .text "musedit"
museditNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - museditBinStart), 0

.import source "musedit.inc"
.import source "musedit-glyphs.inc"
.import source "musedit-tables.inc"
.label museditEnd = *
