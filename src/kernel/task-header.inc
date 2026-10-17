// ==========================================
//  task-header.inc -- dynamic task / library header ABI
// ==========================================
//  SINGLE SOURCE OF TRUTH for the 256-byte paged header: the magic, the flag
//  bits at +2, the field offsets below and the library-id bits of the 16-bit
//  libMask at +10/+11. Imported once by BOTH builds:
//
//    kernel build : src/kernel/kernel.inc
//                     .import source "task-header.inc"
//    task build   : tools/gen-task-asm.ps1 (generates tools/assemble-tasks.asm)
//                     .import source "src/kernel/task-header.inc"
//
//  Change a value HERE and both builds follow -- there is no second copy.
//
//  Constants only: no .pc, no emitted bytes, no code, no imports.

// --- +0/+1: magic --------------------------------------------------------
//  The bytes are $47 'G' then $4F 'O', written as ONE little-endian word --
//  so the literal is $4F47, NOT $474F, which emits $4F $47 and is rejected
//  by dynParseHeader as a bad magic. Never spell the value out in a wrapper.
.const HDR_MAGIC = $4F47

// --- header field offsets (the 256-byte paged header) --------------------
//  Named for the READERS. The loader DMAs the header page into scratchBuf
//  ($C400) and dynParseHeader walks it with `iny`, so these numbers otherwise
//  appear only as a bare `scratchBuf+7` in the middle of a load path.
//  The code body follows the page at file offset 256, and the fixup table
//  follows the code body -- see the trailer below.
.const HDR_FLAGS     = 2    // +2     flag bits (KF_*)
.const HDR_ORIGIN    = 3    // +3/+4  origin, little-endian
.const HDR_CODE_SIZE = 5    // +5/+6  code size, little-endian
.const HDR_DATA_SIZE = 7    // +7     per-instance data size
.const HDR_ZP_BASE   = 8    // +8     ZP save region base
.const HDR_ZP_LEN    = 9    // +9     ZP save region length
.const HDR_LIBMASK   = 10   // +10/+11 libMask (LIB_* bits), little-endian
.const HDR_NAME_LEN  = 12   // +12    name length (the name follows at +13)

// --- the fixup trailer, immediately after the name -----------------------
//  +13+name_len  fixup count, 16-bit little-endian
//  +15+name_len  the fixup table's own file offset, 16-bit little-endian.
//                Always 256 + code_size; dynParseHeader asserts it, so a
//                file whose table is not where the header says is rejected.
//  The table itself is stored AFTER the code body, which is why the count is
//  not bounded by the header page: any number of fixups fits.
//  The wrapper writes these as two zero words -- the build fills them in.

// --- +3: flags -----------------------------------------------------------
//  Write flags as a named expression, never as a bare number:
//
//      .byte KF_RELOCATABLE | KF_TAKES_ARGS   // +3:  flags
//
//  and do NOT re-declare these names in a task, lib or .cmd wrapper:
//  KickAssembler symbols are global across the whole assembly, so a local
//  copy is a duplicate-symbol error.

.const KF_RELOCATABLE   = $01   // task has a fixup table and can be relocated (required)
.const KF_NEEDS_SCREEN  = $02   // task requires an allocated screen (defined, unused)
.const KF_REENTRANT     = $04   // name-keyed shared code; each instance gets its own data buffer
.const KF_TAKES_ARGS    = $08   // loader copies `run` args into the instance buffer at spawn
.const KF_MALLOC_RAM    = $10   // loader kMallocs a program-RAM block (kArg+2/+3 = pages)
.const KF_RE_RUNNABLE   = $20   // a second `run` loads a fresh pool copy (no sharing);
                                //   works with KF_RELOCATABLE, and is NOT reentrant
.const KF_NOT_EVICTABLE = $40   // pinned: dlEvict never swaps this task out. If every
                                //   resident task is pinned, a pool-full load fails with
                                //   error 4 ("? no space in pool")
.const DYN_FLAG_SWAPPED = $80   // dyn-table bit, NOT a header flag: code was evicted

// --- +11/+12: libMask bits -----------------------------------------------
//  One bit per library, so a task declares its dependencies as
//
//      .word (1 << LIB_FILESYS) | (1 << LIB_STRING)   // +11: libMask
//
//  A libMask is a LIFETIME declaration: what a task declares stays in the
//  pool until the task exits (or calls kLibHold) -- see
//  docs/dynamic-libraries.md.
//
//  WARNING: These ids ARE the index order of the library descriptor tables in
//  src/main.asm (libNameLo/Hi, libEntryCount, libEager, libStartPage,
//  libPages, libRefCount). Those tables are positional, so inserting a
//  library in the middle SHIFTS every id after it. tools/check-lib-ids.ps1
//  reads that order and fails the build when it disagrees with this list,
//  so the two cannot drift silently -- keep both in step.
.const LIB_TIME     = 0   // 3 entries
.const LIB_FILESYS  = 1   // 9 entries
.const LIB_GFX      = 2   // 13 entries, eager
.const LIB_FP       = 3   // 1 entry, eager
.const LIB_STRING   = 4   // 7 entries
.const LIB_SID      = 5   // 2 entries, lazy
.const LIB_IPC      = 6   // 2 entries, lazy
.const LIB_SPRITE   = 7   // 4 entries (sheet/frame/pos/reg), eager
.const LIB_MOUSE    = 8   // 1 entry, eager
.const LIB_COUNT    = 9   // libraries the kernel descriptor tables define
