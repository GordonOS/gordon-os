// ==========================================
//  TASK UTILITIES
// ==========================================

// --- getTaskName -- X = task index -> kPtr = name string pointer ---
//  For thread tasks (taskThreadNameReuHi != 0), DMAs name from
//  REU into nameBuf and returns pointer to nameBuf.
//  For normal tasks, returns pointer to known registry string.
getTaskName:
    lda taskThreadNameReuHi,x
    beq gtnDirect               // not a thread -> return direct pointer
    // Thread task: DMA REU -> nameBuf (kPtr set early; survives DMA+wait).
    // Hold SEI across setup + poll: the transfer is only 16 bytes, but an
    // IRQ's own REU traffic rewrites $DF02-$DF0A.
    php
    sei
    lda taskThreadNameReuLo,x; sta $df04
    lda taskThreadNameReuHi,x; sta $df05
    lda #<nameBuf; sta $df02
    sta kPtr
    lda #>nameBuf; sta $df03
    sta kPtr+1
    lda #0
    sta $df06                   // bank 0
    sta $df08                   // length hi = 0
    sta $df0a                   // address control = 0
    lda #16; sta $df07          // length lo = 16
    lda #$91; sta $df01         // REU->C64, immediate
    jsr reuWait
    plp
    rts
gtnDirect:
    lda taskNameLo,x
    sta kPtr
    lda taskNameHi,x
    sta kPtr+1
    rts

