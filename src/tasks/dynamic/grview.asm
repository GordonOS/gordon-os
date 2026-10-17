// ==========================================
//  grview.asm -- "grview" task (relocatable + re-runnable)
// ==========================================
//  Wraps grview.inc -- the .pic picture viewer: "run grview hires".
//  The format it reads is defined in
//  docs/pic-format.md.
//  Nominal origin $5000 (page-aligned; delta_lo=0 for any pool).

.encoding "screencode_mixed"

// Instance-buffer layout. The arguments arrive at offset 0 (KF_TAKES_ARGS) and
// the file name is BUILT there -- "<name>.pic" -- so every field is an offset
// from the argument string. Declared here so the header's data_size and the body
// (grview.inc) share one definition.
.const GRV_REC       = 12        // 5: kReuOpen record (size 16-bit + base 24-bit)
.const GRV_HDR       = 17        // 32: .pic header, with its label at +16
.const GRV_MSGL      = 49        // 2: refusal message pointer (see grvRefuse)
.const GRV_DATA_SIZE = 51

grviewBinStart:
    .word HDR_MAGIC              // +0:  magic 'G','O' (LE)
    .byte KF_RELOCATABLE | KF_TAKES_ARGS | KF_RE_RUNNABLE  // +2: flags
    .word $5000                  // +3:  origin (nominal)
    .word grviewEnd - grview     // +5:  code size = end - ENTRY LABEL
    .byte GRV_DATA_SIZE, 0, 0    // +7:  data size, ZP base, ZP len (none)
    .word (1 << LIB_GFX) | (1 << LIB_FILESYS)  // +10: libMask
    .byte grviewNameEnd - grviewName  // +12: name length
grviewName:
    .text "grview"
grviewNameEnd:
    .word 0                      // fixup_count (16-bit -- written by the build)
    .word 0                      // fixup_offset (written by the build)
    .fill 256 - (* - grviewBinStart), 0

.import source "grview.inc"
.label grviewEnd = *
