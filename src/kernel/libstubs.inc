// ==========================================
//  libstubs.inc -- lazy call cold stubs for dynamic libraries
// ==========================================
//  Each lazy jump-table entry is `jmp libStubXxx` until the lib is
//  loaded, when libLoad patches it in place to `jmp <entry>` (3-cycle
//  warm dispatch -- no bit/bmi/vec test). The cold stub runs once: it
//  saves A/X/Y + kArg[0..7] + kPtr + dataPtr + srcPtr into libSaveBuf,
//  loads the lib, restores them, then jmp's through the now-patched jump
//  entry (C from libLoad survives libColdRestore -- lda/sta/ldx/ldy never
//  touch C). srcPtr is preserved so string.lib's strcpy/strcmp (which
//  take srcPtr as INPUT) survive the cold path; taskNamePtr/spawnPtr
//  are still CLOBBERED (unused by libs).
//  Lives at $1A1B -- the free pocket right after the static data block
//  (taskShadowHi) and before the data heap ($1D40). It cannot move back into
//  the loader band (dynload $4310-$5284, scrollScreenSlot $54AB): the four
//  filesys in-place I/O stubs alone outgrow what is left there.

.pc = $1A1B

// --- shared cold-path register save/restore (C-preserving) ---
libColdSave:
    sta libSaveBuf+0
    stx libSaveBuf+1
    sty libSaveBuf+2
    lda kArg;   sta libSaveBuf+3
    lda kArg+1; sta libSaveBuf+4
    lda kArg+2; sta libSaveBuf+5
    lda kArg+3; sta libSaveBuf+6
    lda kArg+4; sta libSaveBuf+7
    lda kArg+5; sta libSaveBuf+8
    lda kArg+6; sta libSaveBuf+9
    lda kArg+7; sta libSaveBuf+10
    lda kPtr;   sta libSaveBuf+11
    lda kPtr+1; sta libSaveBuf+12
    lda dataPtr; sta libSaveBuf+13
    lda dataPtr+1; sta libSaveBuf+14
    lda srcPtr;   sta libSaveBuf+15
    lda srcPtr+1; sta libSaveBuf+16
    rts

libColdRestore:
    lda libSaveBuf+3;  sta kArg
    lda libSaveBuf+4;  sta kArg+1
    lda libSaveBuf+5;  sta kArg+2
    lda libSaveBuf+6;  sta kArg+3
    lda libSaveBuf+7;  sta kArg+4
    lda libSaveBuf+8;  sta kArg+5
    lda libSaveBuf+9;  sta kArg+6
    lda libSaveBuf+10; sta kArg+7
    lda libSaveBuf+11; sta kPtr
    lda libSaveBuf+12; sta kPtr+1
    lda libSaveBuf+13; sta dataPtr
    lda libSaveBuf+14; sta dataPtr+1
    lda libSaveBuf+15; sta srcPtr
    lda libSaveBuf+16; sta srcPtr+1
    ldx libSaveBuf+1
    ldy libSaveBuf+2
    lda libSaveBuf+0
    rts

libSaveBuf:
    .fill 17, 0

//  Each stub branches to a fail trampoline shared by the stubs around it
//  (libFailTramp*), instead of carrying its own `bcs fail / fail: jmp
//  libLoadFail` pair: the pair costs 5 bytes, the shared form 2 plus a third
//  of a trampoline, and a trampoline sits within 80 bytes of every stub it
//  serves. Inserting a stub in a group means checking the distances -- the
//  assembler rejects a branch that is out of range.

// --- time lib (lib 0): getTime = call 0, setTime = call 1, printTime = call 2 ---
libStubTimeGet:
    jsr libColdSave
    lda #LIB_TIME               // lib id
    jsr libLoad                 // C=0 loaded, C=1 failed
    jsr libColdRestore          // preserves C
    bcs libFailTrampTime
    jmp kGetTime                // jump entry now 'jmp getTime'

libStubTimeSet:
    jsr libColdSave
    lda #LIB_TIME               // lib id
    jsr libLoad
    jsr libColdRestore
    bcs libFailTrampTime
    jmp kSetTime                // jump entry now 'jmp setTime'

libStubTimePrint:
    jsr libColdSave
    lda #LIB_TIME               // lib id
    jsr libLoad
    jsr libColdRestore
    bcs libFailTrampTime
    jmp kPrintTime              // jump entry now 'jmp printTime'

libFailTrampTime:
    jmp libLoadFail

// --- filesys lib (lib 1): format=call 3, save=call 4, delete=call 5,
//     rename=call 6, dirEntry=call 7 (base = 3 = time's entry count) ---
libStubFilesysFormat:
    jsr libColdSave
    lda #LIB_FILESYS            // lib id
    jsr libLoad
    jsr libColdRestore
    bcs libFailTrampFs1
    jmp kReuFormat              // jump entry now 'jmp fsxFormat'

libStubFilesysSave:
    jsr libColdSave
    lda #LIB_FILESYS            // lib id
    jsr libLoad
    jsr libColdRestore
    bcs libFailTrampFs1
    jmp kReuSave                // jump entry now 'jmp fsxSave'

libStubFilesysDelete:
    jsr libColdSave
    lda #LIB_FILESYS            // lib id
    jsr libLoad
    jsr libColdRestore
    bcs libFailTrampFs1
    jmp kReuDelete              // jump entry now 'jmp fsxDelete'

libStubFilesysRename:
    jsr libColdSave
    lda #LIB_FILESYS            // lib id
    jsr libLoad
    jsr libColdRestore
    bcs libFailTrampFs1
    jmp kReuRename              // jump entry now 'jmp fsxRename'

libStubFilesysDirEntry:
    jsr libColdSave
    lda #LIB_FILESYS            // lib id
    jsr libLoad
    jsr libColdRestore
    bcs libFailTrampFs1
    jmp kReuDirEntry            // jump entry now 'jmp fsxDirEntry'

libFailTrampFs1:
    jmp libLoadFail

// filesys lib (lib 1) in-place I/O: create=call 8, open=call 9,
// readAt=call 10, writeAt=call 11
libStubFilesysCreate:
    jsr libColdSave
    lda #LIB_FILESYS            // lib id
    jsr libLoad
    jsr libColdRestore
    bcs libFailTrampFs2
    jmp kReuCreate              // jump entry now 'jmp fsxCreate'

libStubFilesysOpen:
    jsr libColdSave
    lda #LIB_FILESYS            // lib id
    jsr libLoad
    jsr libColdRestore
    bcs libFailTrampFs2
    jmp kReuOpen                // jump entry now 'jmp fsxOpen'

libStubFilesysReadAt:
    jsr libColdSave
    lda #LIB_FILESYS            // lib id
    jsr libLoad
    jsr libColdRestore
    bcs libFailTrampFs2
    jmp kReuReadAt              // jump entry now 'jmp fsxReadAt'

libStubFilesysWriteAt:
    jsr libColdSave
    lda #LIB_FILESYS            // lib id
    jsr libLoad
    jsr libColdRestore
    bcs libFailTrampFs2
    jmp kReuWriteAt             // jump entry now 'jmp fsxWriteAt'

libFailTrampFs2:
    jmp libLoadFail

// --- string lib (lib 4): strlen=22, strcpy=23, strcmp=24,
//     skipSpaces=25, byteToHex=26, hexDigit=27, nibbleToHex=28
//     (base = 22 = time3+filesys5+gfx13+fp1) ---
libStubStrlen:
    jsr libColdSave
    lda #LIB_STRING             // lib id
    jsr libLoad
    jsr libColdRestore
    bcs libFailTrampStr1
    jmp kStrlen                 // jump entry now 'jmp stringLibStrlen'

libStubStrcpy:
    jsr libColdSave
    lda #LIB_STRING             // lib id
    jsr libLoad
    jsr libColdRestore
    bcs libFailTrampStr1
    jmp kStrcpy                 // jump entry now 'jmp stringLibStrcpy'

libStubStrcmp:
    jsr libColdSave
    lda #LIB_STRING             // lib id
    jsr libLoad
    jsr libColdRestore
    bcs libFailTrampStr1
    jmp kStrcmp                 // jump entry now 'jmp stringLibStrcmp'

libStubSkipSpaces:
    jsr libColdSave
    lda #LIB_STRING             // lib id
    jsr libLoad
    jsr libColdRestore
    bcs libFailTrampStr1
    jmp kSkipSpaces             // jump entry now 'jmp stringLibSkipSpaces'

libFailTrampStr1:
    jmp libLoadFail

libStubByteToHex:
    jsr libColdSave
    lda #LIB_STRING             // lib id
    jsr libLoad
    jsr libColdRestore
    bcs libFailTrampStr2
    jmp kByteToHex              // jump entry now 'jmp stringLibByteToHex'

libStubHexDigit:
    jsr libColdSave
    lda #LIB_STRING             // lib id
    jsr libLoad
    jsr libColdRestore
    bcs libFailTrampStr2
    jmp kHexDigit               // jump entry now 'jmp stringLibHexDigit'

libStubNibbleToHex:
    jsr libColdSave
    lda #LIB_STRING             // lib id
    jsr libLoad
    jsr libColdRestore
    bcs libFailTrampStr2
    jmp kNibbleToHex            // jump entry now 'jmp stringLibNibbleToHex'

libFailTrampStr2:
    jmp libLoadFail

// --- libLoadFail -- a lazy lib load failed: report it, then leave through
//     libFailExit. ---
//  dlLibId holds the lib id (libLoad stores it; libColdRestore preserves it).
//  Reporting here and returning would be useless on its own: C cannot carry
//  the news, because the stub's C=1 is also a perfectly ordinary library
//  result, so callers read a load failure as "empty slot" / "end of list"
//  and carry on (`banner`, `del`, `dir`), or ignore it outright (`format`,
//  `ps`). Every trampoline above reaches this, once per failed call.
libLoadFail:
    lda #<msgLibLoadFail
    sta kPtr
    lda #>msgLibLoadFail
    sta kPtr+1
    jsr kPrintString            // "could not load "
    ldx dlLibId
    lda libNameLo,x
    sta kPtr
    lda libNameHi,x
    sta kPtr+1
    jsr kPrintString            // "<name>.lib"

// --- libFailExit -- what the task that could not load its library does ---
//  A command task cannot do its job without the library, and C cannot carry
//  the news (see above), so it ends. That is the whole point: it refuses
//  without any of its own code testing anything.
//
//  The system shell has to survive, but the COMMAND it was running must not.
//  It is re-entered at its own entry with a fresh stack, which is the path a
//  restored instance takes (the entry finds the surface it kept and lands on
//  the prompt), so this needs no state and no shell changes. Without it a
//  command whose library is missing prints this message once per library
//  call and then carries on with stale buffers -- `pool` calls dyHex four
//  times for its header and prints whatever the last decimal left there.
//
//  The shell is identified by its parent link: slot 0 is the boot task and
//  findFreeSlot never hands it to a spawn, so a parent of 0 is the boot
//  target. dlRegister writes that link once and initTCB_ptr does not touch
//  it, so it survives eviction and restore and cannot go stale the way a
//  recycled id can. Re-entering on dataPtr is safe because libColdRestore
//  has already restored the task's own instance-buffer pointer.
//
//  $F7 is the stack a cold-started task gets (initTCB_ptr), so the abandoned
//  command's frames are dropped instead of leaking one stack frame per abort.
//  taskLo/taskHi is a task's code address, i.e. its entry label; pool pages
//  are 256-aligned, so the low byte is 0 and `jmp (kPtr)` cannot hit the NMOS
//  indirect-jump page-boundary bug. dataPtr is taken from the task's own
//  instance-buffer record, not from the restored ZP value: a library may
//  clobber dataPtr as scratch (`gfx.lib` does, for its string drawer), and
//  the entry writes into that buffer before anything else. dlRestore's cold
//  restart sets it the same way.
//  Clobbers A, X, Y, kPtr, dataPtr. Never returns for a command task.
libFailExit:
    ldy curTask
    lda taskParent,y
    beq lfeRestart
    jmp sysExit                 // command task -- the message is out, end it
lfeRestart:
    lda taskDataPtrLo,y
    sta dataPtr
    lda taskDataPtrHi,y
    sta dataPtr+1
    lda taskLo,y
    sta kPtr
    lda taskHi,y
    sta kPtr+1
    ldx #$f7
    txs
    jmp (kPtr)                  // the shell: abandon the command, prompt again

msgLibLoadFail:
    .text "could not load "
    .byte 0

.label libStubsEnd = *

//  WARNING: NOTHING MAY BE ADDED AFTER `libStubsEnd`. The block ends 4 bytes below
//  `$1C6E`, where `syscall.inc`'s sysExit block begins, and those 4 bytes are the
//  whole margin. New lazy stubs belong in the $FF7E pocket instead, next to the
//  entries they belong to (see src/main.asm's tail block).
