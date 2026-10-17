// ==========================================
//  mouse.inc -- mouse.lib shared constants (lib + consumers)
// ==========================================
//  Imported by src/libs/mouse.asm AND every task that declares LIB_MOUSE.
//  Single source of truth for the opcode set and the result block, so a consumer
//  names MOUSE_READ and MOUSE_DELTA_X rather than a bare number.
//
//  The control port is FIXED at port 2:
//  there is no "select port" opcode, deliberately, and leaving that space free is
//  what keeps a later addition at 4 bytes instead of a relocation.

// --- opcodes (A on entry to kMouseCall) ---
//  MOUSE_READ with the reader disabled returns C=1 and writes nothing, which is
//  how a consumer tells "no tracking" from "no movement".
.const MOUSE_READ    = $00
.const MOUSE_ENABLE  = $01
.const MOUSE_DISABLE = $02
.const MOUSE_OPCOUNT = 3      // opcodes 0..2; anything else C=1

// --- the result block, written by MOUSE_READ into the CALLER's kArg ---
//  WARNING: kArg is a per-call scratch, not state -- another task's kernel call
//  can overwrite it, so read the block immediately after the call and copy what
//  must survive into your own instance data.
.const MOUSE_RAW_X   = 0      // the POTX byte the SID returned ($40..$BF on a 1351)
.const MOUSE_RAW_Y   = 1      // the POTY byte
.const MOUSE_DELTA_X = 2      // SIGNED, -32..+31, since the previous read
.const MOUSE_DELTA_Y = 3

// --- the decoded position is bits 6-1 of a raw byte; bit 7 is unused and bit 0
//     is the qualifier the reader shifts out ---
.const MOUSE_DEC_AND = $3f
