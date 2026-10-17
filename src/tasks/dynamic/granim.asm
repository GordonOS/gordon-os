// ==========================================
//  granim.asm -- "granim" task (relocatable + re-runnable)
// ==========================================
//  Wraps granim.inc -- the REU animation film player: "run granim llama".
//  The container it reads is defined in docs/pic-format.md.
//  Nominal origin $5000 (page-aligned; delta_lo=0 for any pool).

.encoding "screencode_mixed"

// Instance-buffer layout. The arguments arrive at offset 0 (KF_TAKES_ARGS) and
// the manifest's name is BUILT there -- "<base>.inf" -- so every field is an
// offset from the argument string. Declared here so the header's data_size and
// the body (granim.inc) share one definition.
//
// The manifest buffer and the name field below it OVERLAP the region the loader
// leaves the spawn's arguments in, and that is why every field the play loop
// reads is written before it is used: an unset byte would otherwise read back as
// part of the command line.
.const GRN_NAME   = 0    // 12: "<base>.inf", built in place from the arguments
.const GRN_REC    = 12   // 5:  kReuOpen's record for the CURRENT chunk
.const GRN_MAN    = 17   // 64: the film manifest
.const GRN_FNAME  = 81   // 12: the current chunk's name, screen-coded
.const GRN_POS    = 93   // 2:  the current frame's byte position in its chunk
.const GRN_FS     = 95   // 2:  one frame's byte size, from the manifest
.const GRN_TOTAL  = 97   // 2:  the film's frame count, from the manifest
.const GRN_FRAME  = 99   // 2:  frames shown since the film started
.const GRN_LEFT   = 101  // 1:  frames left in this chunk
.const GRN_PER    = 102  // 1:  frames per chunk, from the manifest
.const GRN_ACC    = 103  // 1:  the fractional film clock
.const GRN_LAST   = 104  // 1:  the frameCount value last seen
.const GRN_PAUSE  = 105  // 1:  nonzero while the spacebar holds the film
.const GRN_ADD    = 106  // 1:  the accumulator's step, the rate x the speed
.const GRN_SPEED  = 107  // 1:  1..GRN_SPEED_MAX, the +/- keys
.const GRN_DIGITS = 108  // 1:  the chunk number's width, from the manifest
.const GRN_MSGL   = 109  // 2:  the refusal message pointer (see grnRefuse)
.const GRN_DATA_SIZE = 111

granimBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE | KF_TAKES_ARGS | KF_RE_RUNNABLE  // +2: flags
    .word $5000                  // +3:  origin (nominal)
    .word granimEnd - granim     // +5:  code size = end - ENTRY LABEL
    .byte GRN_DATA_SIZE, 0, 0    // +7:  data size, ZP base, ZP len (none)
    .word (1 << LIB_GFX) | (1 << LIB_FILESYS)  // +10: libMask
    .byte granimNameEnd - granimName  // +12: name length
granimName:
    .text "granim"
granimNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - granimBinStart), 0

.import source "granim.inc"
.label granimEnd = *
