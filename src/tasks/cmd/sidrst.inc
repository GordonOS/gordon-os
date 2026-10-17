// ==========================================
//  sidrst.inc -- zero all SID registers
// ==========================================
//  Clears $D400-$D418 (voice freq/pulse/control, ADSR, filter, volume).
//  $D419-$D41C are read-only (pots/OSC3/ENV3) -- not written.

sidrst:
    ldx #0
    lda #0
sidrstLoop:
    sta $d400,x                 // absolute,X -- fixed hardware address
    inx
    cpx #25                     // $D400..$D418 = 25 writeable registers
    bcc sidrstLoop
    lda #SYS_EXIT               // exit syscall
    jsr syscall
