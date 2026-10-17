.const PLAY_DEMO_SONG = false
/*
        Enhanced SID Player by Craig Chamberlain / 1986
        Disassembled by Chris Zinn / October - November 2025

        This is in Kick Assembler format: https://theweb.dk/KickAssembler

        Notes:
        Uses the CIA Timer 1 / Plays at the same speed in NTSC and PAL
        Default Memory Layout:  $C000 - $CC51   49152 - 52305   (3,153 bytes)
        
        Change PLAYER_ADDRESS to assemble it anywhere you like.  ($C000 is the default)
        Change SONG_ADDRESS to load a test song anywhere you like ($8000 is the default)
        Change PLAY_DEMO_SONG = true to try it out with a demo song.

        How to use the Player in your own code:
        ---------------------------------------
        Load the song into memory
        jsr INSTALL_SID_PLAYER to install the IRQ Handler
        jsr INIT_SONG with the address of the song in x,y (lo,hi)
        optionally store a address to a routine at AuxRoutineAddrLoHi to have the AUX command call your code.
        Set SID_STATUS to %00000111 to start playing all voices (Bits 0,1,2 for voices 1,2,3)

        Read FLAG_STATUS to get last value used by a FLG Command.
        
        Stopping a song that is playing:
        lda #$00
        sta SID_STATUS
        jsr HUSH_PLAYER 
        
        jsr REMOVE_SID_PLAYER to restore the IRQ Handler to what is before INSTALL_SID_PLAYER was installed.

        ZERO Page Usage
        Preserved on each interrupt: FB through FF.   $FD,$FE Address of the next command on the current voice
        Used but not preserved:  (In the INIT_SONG routine) : $61 through $65 (Floating Point Accumulator #1 used by BASIC).
*/
                        .cpu _6502NoIllegals
.if (PLAY_DEMO_SONG == true)
{
                        BasicUpstart2(testPlayer)
testPlayer:             jsr INSTALL_SID_PLAYER
                        ldx #<SONG_ADDRESS
                        ldy #>SONG_ADDRESS
                        jsr INIT_SONG
                        lda #%00000111                  // All 3 voices go
                        sta SID_STATUS
                        rts                             // Exit to basic with the song playing

.label                  SONG_ADDRESS = $8000
                        * = SONG_ADDRESS                "Test Song"
                        .import c64 "music/commodore.mus"    // Import C64 PRG file without the 2 byte program header
                        
.label                  SONG_ADDRESS_END = *
.print                  "Song Start Address  : $" + toHexString(SONG_ADDRESS,4) + " " + SONG_ADDRESS
.print                  "Song End   Address  : $" + toHexString(SONG_ADDRESS_END,4) + " " + SONG_ADDRESS_END
.print                  "Song       Size     :  " + (SONG_ADDRESS_END - SONG_ADDRESS) + " bytes"
}


.namespace SID
{
        .label  voice1Control           = $d404
        .label  voice2Control           = $d40b
        .label  voice3Control           = $d412
        .label  voice1FrequencyHi       = $d401
        .label  voice2FrequencyHi       = $d408
        .label  voice3FrequencyHi       = $d40f

        .label  filterCutoffLo          = $d415
        .label  filterCutoffHi          = $d416
        .label  filterResonanceControl  = $d417
        .label  filterVolumeAndFilter   = $d418
}

.label                  PLAYER_ADDRESS = $c000
                        * = PLAYER_ADDRESS             "Working Data"
SID_STATUS:             .byte 0                         // bit 0-2  Enable/Disable voice
                                                        // Bit 3    Illegal state exception (The IRQ started, but bit 7 was on)
                                                        // Bit 34   OtherError  (Seems to be related to IllegalNoteDuration)
                                                        // Bit 4    Illegal Note Duration (Example: 16th. 32nd. in default tempo for example)
                                                        // Bit 45   Illegal Phrase Error  (Calling a phrase that doesn't exist)
                                                        // Bit 35   Stack Overflow Error (Calling a phrase from a phrase more than 5 times)
                                                        // Bit 345  Calling a phrase from another voice / Repeat called but the phrase definition was on a different voice
                                                        // Bit 5    END reached, but no DEF
                                                        // Bit 6    ?
                                                        // Bit 7    IRQ In progress.  This makes sure that the IRQ Cannot run on top of itself while the IRQ is already running
FLAG_STATUS:            .byte 0                         // START OF WORK MEMORY
WorkTypeOctNote:        .byte 0,0,0
WorkNoteDuration:       .byte 0,0,0
WorkMeasureLo:          .byte 0,0,0
WorkMeasureHi:          .byte 0,0,0
WorkTargetNoteFreqLo:   .byte 0,0,0                     //  Frequency Portamento is sliding towards (Command: POR)
WorkTargetNoteFreqHi:   .byte 0,0,0
WorkPulseWidth2Lo:      .byte 0,0,0
WorkPulseWidth2Hi:      .byte 0,0,0
WorkVc1Control:         .byte 0                         // These mirror the bits in $D404 SID.voice1Control
WorkVc2Control:         .byte 0                         // These mirror the bits in $D40B SID.voice2Control
WorkVc3Control:         .byte 0                         // These mirror the bits in $D412 SID.voice3Control
WorkAttackDecays:       .byte 0,0,0
WorkSusRelease:         .byte 0,0,0
WorkFilterSweepState:   .byte 0
WorkFilterCutOff:       .byte 0
WorkFilterControl:      .byte 0
WorkVolAndFilter:       .byte 0
WorkTempo:              .byte 0                         // $90  default values in play routine. See CommandTem,
WorkTempoTableValue:    .byte 0                         // $60  See CommandTem, this is the va;ie looked up value from DataTempoTable for $90
WorkUTLValue:           .byte 0                         // $0c
WorkUTVValues:          .byte 0,0,0
WorkCiaTimerA:          .byte 0,0                       // Lo/Hi of adjusted CIA Timer A countdown clock. See DATA_CIA_TIMER_A for more info.
WorkCurrentVoice:       .byte 0                         // Is set once, but doesn't appear to be referenced after being set
WorkNoteJiffiesLeft:    .byte 0,0,0                     // Jiffys remaining until this note is done playing. See chart on Page 122 of Compute Book
WorkVoiceForCallPhrase: .byte 0,0,0
WorkVoiceNoteTie:       .byte 0,0,0
WorkReleasePoint:       .byte 0,0,0
WorkHoldTime:           .byte 0,0,0
WorkHoldTimeJifsLeft:   .byte 0,0,0
WorkPortamentoTmp:      .byte 0,0,0
WorkPortamentoLo:       .byte 0,0,0
WorkPortamentoHi:       .byte 0,0,0
WorkVibratoDepthA:      .byte 0,0,0
WorkVibratoDepthB:      .byte 0,0,0
WorkVibratoDepthC:      .byte 0,0,0
WorkVibratoDepthD:      .byte 0,0,0
WorkVibratoDepthE:      .byte 0,0,0
WorkVibratoDepthF:      .byte 0,0,0
WorkVibratoRate:        .byte 0,0,0
WorkPortAndVibrato:     .byte 0,0,0
WorkPVDA:               .byte 0,0,0
WorkPVDB:               .byte 0,0,0
WorkPVDC:               .byte 0,0,0
WorkPVDD:               .byte 0,0,0
WorkPCDState:           .byte 0,0,0
WorkPulseVibratoRate:   .byte 0,0,0
WorkPulseVibratoDepth:  .byte 0,0,0
WorkTPSA:               .byte 0,0,0
WorkTPSB:               .byte 0,0,0
WorkTPSC:               .byte 0,0,0
WorkTPSD:               .byte 0,0,0
WorkTPSHalfSteps:       .byte 0,0,0
WorkDetuneLo:           .byte 0,0,0
WorkDetuneHi:           .byte 0,0,0
WorkNoteFreqLo:         .byte 0,0,0
WorkNoteFreqHi:         .byte 0,0,0
WorkPulseSweep:         .byte 0,0,0
WorkPulseSweepOther:    .byte 0,0,0
WorkPulseWidthLo:       .byte 0,0,0
WorkPulseWidthHi:       .byte 0,0,0
WorkAutoFilter:         .byte 0,0,0
WorkFilterSweep:        .byte 0,0,0
WorkFilterCutoffVc:     .byte 0,0,0
WorkNxtCmdLo:           .byte 0,0,0                     //    Lo and Hi of Memory Address of next Command byte
WorkNxtCmdHi:           .byte 0,0,0                     //    For voice 1,2 and 3
WorkVoiceForHEDDef:     .byte 0,0,0
WorkCmdAfterHEDLo:      .byte 0,0,0
WorkCmdAfterHEDHi:      .byte 0,0,0
WorkHEDRepeatCnt:       .byte 0,0,0                     //    How many repeats are left
WorkPhraseCallStack:    .byte 0,0,0
WorkLFO:                .byte 0
WorkOscillatorVoice3:   .byte 0
WorkEnvGenOutputVoice3: .byte 0
WorkVoiceWavSrc:        .byte 0,0,0                     //  0-2. Set by the SRC command 0=Software-Generated waveform  1=OSC3 register 2=ENV3 register
WorkVoiceWavDst:        .byte 0,0,0                     //  0-3. Set by the DST command 0=Modulation Off,1=Frequency,2=Pulse Width,3=Filter Cutoff
WorkVoiceWavScale:      .byte 0,0,0                     //  -7 to 7.  SEt by the SCA command:
WorkLFOC:               .byte 0
WorkLFORampUp:          .byte 0                         //    (Value from 0..31) Set with RUP
WorkLFORampDown:        .byte 0                         //    (Value from 0..31) Set with RDN
WorkMaxModulation:      .byte 0                         //    (Value from 0..255) set with MAX
WorkVoiceDSTA:          .byte 0,0,0
WorkVoiceDSTB:          .byte 0,0,0
WorkVoiceDSTC:          .byte 0,0,0
WorkVoiceDSTD:          .byte 0,0,0
WorkGlobalDSTA:         .byte 0
                        .byte 0,0                       // Unused
WorkGlobalDSTB:         .byte 0
                        .byte 0,0                       // Unused
WorkLFOA:               .byte 0
WorkLFOB:               .byte 0
WorkCallStkNxtCmdAdrLo: .fill 15,0                      // Addresses Lo of next command. voice1=0-4   voice2=5=9   voice3=10=14. Used when calling phrases from phrases
WorkCallStkNxtCmdAdrHi: .fill 15,0                      // Addresses Hi of next command. voice1=0-4   voice2=5=9   voice3=10=14. Used when calling phrases from phrases
PhraseDefLo:            .fill 24,0                      // Lo Byte of the stored address where the phrase notes are located
PhraseDefHi:            .fill 24,0                      // Hi Byte of the stored address where the phrase notes are located
WorkCallStkState:       .fill 15,0                      // END OF WORK MEMORY
WorkPhraseUnknown:      .fill 24,0                      // Initialized with 24 $FFs when a song starts
                        * = *      "Preserved Data"
PRESERVE_ZP_FB:         .byte 0
PRESERVE_ZP_FC:         .byte 0
PRESERVE_ZP_FD:         .byte 0
PRESERVE_ZP_FE:         .byte 0
PRESERVE_ZP_FF:         .byte 0
DATA_CIA_TIMER_A:       .byte 0,0                       // Cycles per Jiffy for the CIA Timer.  (cps = Cycles Per Second) / 60 to get how cycles per jiffy (jiffy = 1/60 second)
                                                        //  On PAL:   16,420  (PAL CPU = 985,248.5 cps / 60 = 16,420 (.80) rounds to 16,420)
                                                        //  On NTSC:  17,045  (NTSC CPU = 1,022,727 cps / 60 = 17,045.45 rounded to 17,045)
DATA_OLD_IRQH:          .byte 0,0

                         * = *     "User Defined Auxiliary Routine"
AuxRoutineAddrLoHi:     .byte <DefaultAuxRoutine
                        .byte >DefaultAuxRoutine
DefaultAuxRoutine:       rts                            // See Command: AUX for more details  A(A=0-255) X=Voice Number (0-2)

                        * = *      "Lookup Tables"
DataVoiceBitMask:       .byte $01,$02,$04               // 001 010 100   (This is loaded and EORed to toggle a voice on or off)
SIDVoiceRegsLo:         .byte $00,$07                   // 0E below this is part of this lookup.  oe is used in the note lookup because it is a rest and is ignored
DataNoteLookup:         .byte $0E,$02,$02               // The first 0E (rest) is also used above when looking up lo byte of SID Register for voice 3 ($D40E)
                        .byte $FE,$02,$02               //    1=C 2=d 3=e 4=f 5=g 6=a 7=b 0=Rest
                        .byte $FE,$FE
DataAccidentalModifier: .byte $00                       // This is a working byte
                        .byte $01,$00                   // 01=Sharp  00 = Normal  FF (-1) = Flat?
DataHalfStepsFromC:     .byte $FF,$00,$02,$04           // In order (starting with FF)  0=Rest 1=C 2=d 3=e 4=f 5=g 6=a 7=b
                        .byte $05,$07,$09,$0B           // Translates to half step modify to the notes from C r=255 c=0 d=2 e=4 f=5 g=7 a=9 b=11

                        // Frequency Look up Table : See C64 Programmers Ref Appendix E for table of values
                        // Index 0  = $861E / 34334   C Octave 7
                        // Index 11 = $FD2E / 64814   B Octave 7
                        //  To calculate an octave, divide the number in half per octave
DataNoteFreqLo:         .byte $1E,$18,$8B,$7E           // Frequency Lo Table (Starting at C Octave 7 - See C64 Programmers Ref Appendix E)
                        .byte $FA,$06,$AC,$F3
                        .byte $E6,$8F,$F8,$2E

DataNoteFreqHi:         .byte $86,$8E,$96,$9F           // Frequency Hi Table (Starting at C Octave 7 - See C64 Programmers Ref Appendix E)
                        .byte $A8,$B3,$BD,$C8
                        .byte $D4,$E1,$EE,$FD

DataOctaveForCutOff:    .byte $8C,$78,$64,$50           // Stored in reverse order.. Octave 7 first then to 0
                        .byte $3C,$28,$14,$00

DataHalfStepForCutOff:  .byte $00,$02,$03,$05
                        .byte $07,$08,$0A,$0C
                        .byte $0D,$0F,$11,$12

DataFilterLookupValue:  .byte $00,$E0

DataMinCallStack:       .byte $00
DataMaxCallStack:       .byte 5,10,15
DataTPSLookupA:         .byte $F9
DataTPSLookupB:         .byte $00,$F5
DataTempoTable:         .byte $00,$00,$00,$10
                        .byte $00,$00,$20,$00
                        .byte $00,$30,$00,$00
                        .byte $40,$00,$00,$50
                        .byte $00,$00,$60,$00
                        .byte $00,$70,$00,$00
                        .byte $80,$00,$00,$90
                        .byte $00,$00,$A0,$00

// -----------------------------------------------------------------
//              Hook - install the SID Player IRQ Routine
// -----------------------------------------------------------------
                        * = *                   "Install SID Player"
INSTALL_SID_PLAYER: {
                        lda #$00
                        sta SID_STATUS                  // All voices off
                        ldx #$95                        // Set NTSC value ro $95$42 ($4295 / 17045=# of cpu cycles for NTSC to refresh)
ntsc:                   ldy #$42                        //     NTSC CPU = 1,022,727 cycles/second / 60 = 17,045.45 rounded to 17,045
                        lda $02A6                       // PAL/NTSC Flag   0=NTSC / 1=PAL
                        beq setCIATimerA
pal:                    ldx #$25                        // Set PAL value to $25$40   ($4025 / 16421=# of cpu cycles for PAL to refresh)
                        ldy #$40                        //     PAL CPU = 985,248.5 / 60 = 16,420.808333... rounds up to 16,421
setCIATimerA:           stx DATA_CIA_TIMER_A            //     Don't think confuse this with 50hz video refresh; this routine is designed to run 60/second
                        sty DATA_CIA_TIMER_A+1
                        lda $0314
                        sta DATA_OLD_IRQH
                        lda $0315
                        sta DATA_OLD_IRQH + 1
                        sei
                        lda #<IRQ_Handler
                        sta $0314
                        lda #>IRQ_Handler
                        sta $0315
                        cli
                        rts
}

// -----------------------------------------------------------------
//              Play  -  Initialize the player to play a new song.
//                       Clears working memory, assigns default values and sets up
//                       pointers to the voice data.
//              Inputs:  x=lo y=hi of memory address of song to play
//              Outputs: x=lo y=hi of string of song info (null terminated text)
//             ZP Usage: $61,$62 lo,hi of address of music in Zero Page  (Not preserved)
//                       $63,$64 lo,hi of D400 - start of SID registers - used to set default values quickly.
//                       $61-$65 is normally the Floating Point Accumulator #1 used by BASIC.
// -----------------------------------------------------------------
                        * = *                   "Initialize Player to Play Song"
INIT_SONG: {
                        lda #$00
                        sta SID_STATUS                  // Turn off all voices
                        stx $61                         // Store lo,hi of address of music in zero page
                        sty $62
                        ldy #$bc
clearWorkMemLoop:       sta SID_STATUS,y                // Set 0s to memory addresses between START end END OF WORK MEMORY
                        dey
                        bne clearWorkMemLoop
                        ldy #$72
clearWorkMemLoop2:      sta WorkPhraseCallStack + 2,y
                        dey
                        bne clearWorkMemLoop2

                        sta SID.filterCutoffLo          // Clear Filter Cutoff Frequency lo
                        sta SID.filterCutoffHi          // Clear Filter Cutoff Frequency hi
                        lda #%00001000
                        sta WorkFilterControl
                        sta SID.filterResonanceControl  // Filters off on all voices.   Enable processing an external audio signal which is brought in through pin 5 of the Audio/Video P6rt.
                        sta WorkVolAndFilter
                        sta SID.filterVolumeAndFilter   // Set volume to 8 out of 15 and turn off all filters
                        lda #$90
                        sta WorkTempo
                        lda #$60
                        sta WorkTempoTableValue
                        lda #$0c
                        sta WorkUTLValue
                        lda DATA_CIA_TIMER_A            // Set the CIA Timer to the hardware detected default values (PAL vs NTSC)
                        sta WorkCiaTimerA               // See DATA_CIA_TIMER_A for more info
                        lda DATA_CIA_TIMER_A + 1
                        sta WorkCiaTimerA + 1
                        lda #$ff
                        sta WorkMaxModulation
                        lda #$d4                        // Use ZP $D3,$D4 to write to SID Register $D4xx
                        sta $64

                        ldx #$02
setInitVoiceValuesLoop: lda #$ff
                        sta WorkMeasureHi,x
                        lda #$01
                        sta WorkNoteJiffiesLeft,x
                        sta WorkUTVValues,x
                        txa
                        sta WorkVoiceForCallPhrase,x
                        sta WorkVoiceForHEDDef,x
                        lda #$04                        // Set default release point in the ADSR envelope to 4
                        sta WorkReleasePoint,x
                        lda DataMinCallStack,x
                        sta WorkPhraseCallStack,x
                        lda #$5b
                        sta WorkTPSC,x
                        lda SIDVoiceRegsLo,x
                        sta $63
                        lda #$00
                        tay
                        sta ($63),y
                        iny
                        sta ($63),y
                        iny
                        sta ($63),y
                        lda #$08
                        sta WorkPulseWidth2Hi,x
                        sta WorkPulseWidthHi,x
                        iny
                        sta ($63),y
                        iny
                        sta ($63),y
                        lda #$40
                        sta WorkVc1Control,x
                        sta ($63),y
                        lda #$20
                        sta WorkAttackDecays,x
                        iny
                        sta ($63),y
                        lda #$f5
                        sta WorkSusRelease,x
                        iny
                        sta ($63),y
                        dex
                        bpl setInitVoiceValuesLoop

                        txa                             // a = $FF
                        ldx #23                         // Set WorkPhraseUnknown C13E - C155 to $FF  (24 memory positions)
initWorkPhraseLoops:    sta WorkPhraseUnknown,x
                        dex
                        bpl initWorkPhraseLoops

                        lda $61                         // 61,62 contain start address of the song  lo,hi
                        clc                             //
                        adc #$06                        // add 6 to the low address
                        sta $63                         // 63,64 will be the start of voice 1 data
                        lda #$00
                        tax
                        tay
                        adc $62
addressOfVoiceLoop:     sta $64                         // a= hi of voice x data
                        sta WorkNxtCmdHi,x              // store hi of voice x data
                        sta WorkCmdAfterHEDHi,x         // store hi of voice x data
                        lda $63                         // a= lo of voice x data
                        sta WorkNxtCmdLo,x              // store lo of voice 1+x data
                        sta WorkCmdAfterHEDLo,x         // store lo of voice 1+x data
                        clc
                        adc ($61),y
                        sta $63
                        lda $64
                        iny
                        adc ($61),y
                        iny
                        inx
                        cpx #$03
                        bne addressOfVoiceLoop

                        ldx $63                         // x=lo a,y=hi address of text area for song info
                        tay
                        rts
}

// -----------------------------------------------------------------
//              Hush
//              Silence the three voices and to reset the hardware timer interrupt rate.
// -----------------------------------------------------------------
                        * = *                   "Hush (Silence Voices, reset timers)"
HUSH_PLAYER:            lda #$00
                        sta SID.voice1Control
                        sta SID.voice2Control
                        sta SID.voice3Control
                        sta SID.voice1FrequencyHi       //  Voice 1 Frequency Control (high byte)
                        sta SID.voice2FrequencyHi       //  Voice 2 Frequency Control (high byte)
                        sta SID.voice3FrequencyHi       //  Voice 3 Frequency Control (high byte)
                        lda #%00001000
                        sta SID.filterResonanceControl  //  Filters off on all voices.   Enable processing an external audio signal which is brought in through pin 5 of the Audio/Video P6rt.
                        lda DATA_CIA_TIMER_A
                        sta $dc04                       //  CIA TIMER A LO
                        lda DATA_CIA_TIMER_A + 1
                        sta $dc05                       //  CIA TIMER A HI
                        rts


// -----------------------------------------------------------------
//              Drop
//              Undoes everything done by HOOK and restores the interrupt processing to normal.
// -----------------------------------------------------------------
                        * = *                   "Drop (Uninstall IRQ Handler)"
REMOVE_SID_PLAYER:      sei
                        lda DATA_OLD_IRQH
                        sta $0314
                        lda DATA_OLD_IRQH + 1
                        sta $0315
                        cli
                        rts

// -----------------------------------------------------------------
//              Exit IRQ (And optionally stop all voices)
//              Called by IRQ_Handler
// -----------------------------------------------------------------

StopMusicAndExitIRQ:    lda #%00001000                  // Set all 3 voices to off.  Bit 3 signals the IRQ Handler was called while it was running
                        sta SID_STATUS
ExitIRQ:                jmp (DATA_OLD_IRQH)

// -----------------------------------------------------------------
//              Interrupt Handler for the Player
//              Called by default 60 times a second (Can be changed with Command: JIF)
// -----------------------------------------------------------------
                        * = *                   "IRQ Handler"
IRQ_Handler:            lda $dc0d                       // Clear CIA 1 Interrupt Control Register
                        lda SID_STATUS
                        bmi StopMusicAndExitIRQ         // Exit the IRQ Handler if bit 7 is set.
                        ora #%10000000                  // Set bit 7 and existing voices (bit 0-2)
                        tay                             // Store above in y
                        and #%00000111                  // Test if any voice is on (bit 0-2)
                        beq ExitIRQ                     // No voices are on - Exit IRQ
                        cld
                        sty SID_STATUS                  // Set SID_STATUS to be bit 7 on and whatever voices are already on
                        cli
                        lda $fb                         // Preserve Zero Page FB - FF
                        sta PRESERVE_ZP_FB
                        lda $fc
                        sta PRESERVE_ZP_FC
                        lda $fd
                        sta PRESERVE_ZP_FD
                        lda $fe
                        sta PRESERVE_ZP_FE
                        lda $ff
                        sta PRESERVE_ZP_FF
                        lda WorkFilterSweepState
                        clc
                        adc WorkGlobalDSTA
                        pha
                        and #$07
                        tay
                        lda WorkGlobalDSTB
                        adc #$00
                        sta $ff
                        pla
                        lsr $ff
                        ror
                        lsr $ff
                        ror
                        lsr $ff
                        ror
                        clc
                        adc WorkFilterCutOff
                        sty SID.filterCutoffLo          // Lo Byte of Filter Cutoff Frequency
                        sta SID.filterCutoffHi          // Hi Byte of Filter Cutoff Frequency
                        lda WorkFilterControl
                        sta SID.filterResonanceControl
                        lda WorkVolAndFilter
                        sta SID.filterVolumeAndFilter   // Volume and Filter Select Register
                        lda #$d4
                        sta $fc
                        ldx #$00                        // fb,fc will be used to quickly set $d400 (Start of SID voice Registers) in the next section
                                                        // X is now also 0 which is used to index data by voice


{                       // Apply current values to the SID Register for each voice
applyToSIDRegsLoop:     lda SID_STATUS                  // Loop for each voice (x=0..2)
                        and DataVoiceBitMask,x          // Check to see if voice x is on
                        beq goToNextVoice
                        lda SIDVoiceRegsLo,x
                        sta $fb                         // fb,fc is now setup for voice x registers ($D400=Voice 0, $D407=Voice 1, $D40E=Voice 2)
                        lda WorkTargetNoteFreqLo,x
                        clc
                        adc WorkVibratoDepthC,x
                        tay
                        lda WorkTargetNoteFreqHi,x
                        adc WorkVibratoDepthD,x
                        pha
                        tya
                        clc
                        adc WorkVoiceDSTA,x
                        ldy #$00
                        sta ($fb),y                     // $D400,$D407, $D40E Voice x Frequency Control (Lo byte)
                        pla
                        adc WorkVoiceDSTB,x
                        iny
                        sta ($fb),y                     // $D401,$D408, $D40F Voice x Frequency Control (Hi byte)
                        lda WorkPulseWidth2Lo,x
                        clc
                        adc WorkPVDC,x
                        sta $ff
                        lda WorkPulseWidth2Hi,x
                        adc WorkPVDD,x
                        pha
                        lda $ff
                        clc
                        adc WorkVoiceDSTC,x
                        iny
                        sta ($fb),y                     // $D402,$D409, $D410 Voice x Pulse Waveform Width (Lo byte)
                        pla
                        adc WorkVoiceDSTD,x
                        iny
                        sta ($fb),y                     // $D403,$D40A, $D411 Voice x Pulse Waveform Width (Hi byte)
                        lda WorkAttackDecays,x
                        iny
                        iny
                        sta ($fb),y                     // $D405,$D40C, $D413 Voice x Attack/Decay Register
                        lda WorkSusRelease,x
                        iny
                        sta ($fb),y                     // $D406,$D40D, $D414 Voice x Sustain/Release Control Register
goToNextVoice:          inx
                        cpx #$03
                        bne applyToSIDRegsLoop

                        ldy WorkVc1Control              //Set all three voices control register at once.  These registers...
                        ldx WorkVc2Control              //    start/stop sound
                        lda WorkVc3Control              //    Select Wave Forms (Triangle, Noise, Sawtooth, Pulse, noise and combinations of each)
                        sty SID.voice1Control
                        stx SID.voice2Control           //    Ring modulate with oscillators 1 and 3
                        sta SID.voice3Control           //    Disable oscillator 1
}

                        // Prepare the CIA Timer to trigger the next IRQ
                        // The only way the timer countdown will change is from a JIF command
                        ldx WorkCiaTimerA               //   Set a new timer value to the CIA Timer A
                        ldy WorkCiaTimerA + 1           //   This value takes effect when the current timer hits 0
                        stx $dc04                       //   At that point, this new value will be used for the count down
                        sty $dc05                       //   By default / this is 1/60th of a second.  See DATA_CIA_TIMER_A for more info
                        lda $d41b                       //   Read Oscillator Voice 3 / Random Number Generator
                        sta WorkOscillatorVoice3
                        lda $d41c                       //   Envelope Generator Voice 3 Output
                        sta WorkEnvGenOutputVoice3


                        // This calls a subroutine to process each voice.
                        // It will pull in the next command, run it and do so until it a note plays or it hits end of song for the voice
                        ldx #$00
{
processVoiceLoop:       lda SID_STATUS
                        and DataVoiceBitMask,x
                        beq goToNextVoice
                        stx WorkCurrentVoice            // Even though it is set here, nothing else in the code appears to ever read this.
                        jsr ProcessVoice
                        lda SID_STATUS
                        and #%01111000                  // Are any of the error status bits set?
                        beq goToNextVoice               // No errors, let's continue to the next voice
                        jmp CleanUpAndExitIRQ           // If there any errors, (all voices will be shutoff) we can continue on.  The player will stop playing the song.
goToNextVoice:          inx
                        cpx #$03
                        bne processVoiceLoop
}

                        // This section of code is to process effects that are global to all voices
                        lda WorkLFOC                    // LFO = Low Frequency Oscillation  (See Command: LFO)
                        bne lFOBLogic
                        lda WorkLFORampUp
                        ora WorkLFORampDown
                        beq voiceWavDstLogic
                        lda WorkLFOA
                        bne lfoSkipAhead
lFORampupLogic:         lda WorkLFORampUp
                        beq lfoRampdownLogic
                        clc
                        adc WorkLFO
                        bcs moreLFOLogic
                        cmp WorkMaxModulation
                        bcc endOfLGOLogic
                        beq endOfLGOLogic
moreLFOLogic:           lda #$00
                        sta WorkLFOA
                        lda WorkLFORampDown
                        beq endOfLGOLogic
                        inc WorkLFOA
                        lda WorkLFO
                        sbc WorkLFORampDown
                        jmp endOfLGOLogic
lfoSkipAhead:           lda WorkLFORampDown
                        beq lFORampupLogic
lfoRampdownLogic:       lda WorkLFO
                        sec
                        sbc WorkLFORampDown
                        bcs endOfLGOLogic
                        lda #$00
                        sta WorkLFOA
                        lda WorkLFORampUp
                        bne endOfLGOLogic
                        inc WorkLFOA
                        bne skipToEnd
lFOBLogic:              dec WorkLFOB
                        bne voiceWavDstLogic
                        lda WorkLFOA
                        bne lowerLFOA
                        inc WorkLFOA
                        lda WorkLFORampDown
                        bne storeLFBOTemp
                        lda #$20
storeLFBOTemp:          sta WorkLFOB
                        lda #$00
                        beq endOfLGOLogic
lowerLFOA:              dec WorkLFOA
                        lda WorkLFORampUp
                        bne endOfLGOBCalc
                        lda #$20
endOfLGOBCalc:          sta WorkLFOB
skipToEnd:              lda WorkMaxModulation
endOfLGOLogic:          sta WorkLFO

                        // Apply Command: DST logic (Loops through the voices)
voiceWavDstLogic: {     ldx #$00
wavDSTVoiceLoop:        lda WorkVoiceWavDst,x
                        beq nextDSTVoiceLoop
                        lda #$00
                        sta $ff
                        ldy WorkVoiceWavSrc,x
                        lda WorkLFO,y
                        ldy WorkVoiceWavScale,x
                        beq skipAheadToDST
                        bmi dstloop
wavLoop:                asl
                        rol $ff
                        dey
                        bne wavLoop
                        beq skipAheadToDST
dstloop:                lsr
                        iny
                        bne dstloop
skipAheadToDST:         ldy WorkVoiceWavDst,x
                        dey
                        bne skipAheadToDSTC
                        sta WorkVoiceDSTA,x
                        lda $ff
                        sta WorkVoiceDSTB,x
                        jmp nextDSTVoiceLoop
skipAheadToDSTC:        dey
                        bne skipAheadToDSTA
                        sta WorkVoiceDSTC,x
                        lda $ff
                        sta WorkVoiceDSTD,x
                        jmp nextDSTVoiceLoop
skipAheadToDSTA:        sta WorkGlobalDSTA
                        lda $ff
                        sta WorkGlobalDSTB
nextDSTVoiceLoop:       inx
                        cpx #$03
                        bne wavDSTVoiceLoop
}

                        // End of the IRQ Handler
                        // Restore the Zero Page FB..FF
                        // Store a new SID_STATUS in case an error occurred
                        lda SID_STATUS
                        and #%01111111                  // Turn bit 7 off to signal the IRQ Handler is done working
CleanUpAndExitIRQ:      sta SID_STATUS
                        lda PRESERVE_ZP_FB              // Restore the preserved Zero Page locations
                        sta $fb
                        lda PRESERVE_ZP_FC
                        sta $fc
                        lda PRESERVE_ZP_FD
                        sta $fd
                        lda PRESERVE_ZP_FE
                        sta $fe
                        lda PRESERVE_ZP_FF
                        sta $ff
                        jmp (DATA_OLD_IRQH)

workNoteTieIsNegative:  lda WorkPortAndVibrato,x
                        bne jumpAhead
                        jmp endOfProcessNote
jumpAhead:              jmp vibratoDepthLogic

                        // This is called from processVoiceLoop (Above) / x= voice #
ProcessVoice:           dec WorkNoteJiffiesLeft,x       // These start at 1 on song init.  Number of Jiffys left to left the note play out.
                        bne NoteIsPlaying
                        jmp SetCurrentCmdAddress
NoteIsPlaying:          lda WorkVoiceNoteTie,x
                        bmi workNoteTieIsNegative
                        bne portamentoLogic
                        lda WorkHoldTimeJifsLeft,x      // From Command:HLD
                        beq releasePointLogic
                        dec WorkHoldTimeJifsLeft,x
                        bne portamentoLogic
releasePointLogic:      lda WorkReleasePoint,x          // Default value is 4 - change with Command: PNT
                        cmp WorkNoteJiffiesLeft,x
                        bcc portamentoLogic
                        lda WorkVc1Control,x
                        and #%11111110                  // Start the release phase of the ADSR for this voice
                        sta WorkVc1Control,x
portamentoLogic:        lda WorkPortamentoTmp,x
                        beq vibratoDepthLogic
                        asl
                        lda WorkTargetNoteFreqLo,x
                        bcs portaMentoLogic
                        adc WorkPortamentoLo,x
                        sta WorkTargetNoteFreqLo,x
                        tay
                        lda WorkTargetNoteFreqHi,x
                        adc WorkPortamentoHi,x
                        sta WorkTargetNoteFreqHi,x
                        pha
                        tya
                        cmp WorkNoteFreqLo,x
                        pla
                        sbc WorkNoteFreqHi,x
                        bcs portaMentoLogic2
                        bcc portaMentoLogic3
portaMentoLogic:        sbc WorkPortamentoLo,x
                        sta WorkTargetNoteFreqLo,x
                        lda WorkTargetNoteFreqHi,x
                        sbc WorkPortamentoHi,x
                        sta WorkTargetNoteFreqHi,x
                        lda WorkNoteFreqLo,x
                        cmp WorkTargetNoteFreqLo,x
                        lda WorkNoteFreqHi,x
                        sbc WorkTargetNoteFreqHi,x
                        bcc portaMentoLogic3
portaMentoLogic2:       lda WorkNoteFreqLo,x
                        sta WorkTargetNoteFreqLo,x
                        lda WorkNoteFreqHi,x
                        sta WorkTargetNoteFreqHi,x
                        lda #$00
                        sta WorkPortamentoTmp,x
portaMentoLogic3:       lda WorkPortAndVibrato,x
                        beq pulseSweepLogic
vibratoDepthLogic:      lda WorkVibratoDepthA,x
                        beq endVibratoLogic
                        ldy #$00
                        dec WorkVibratoDepthB,x
                        bne vibDepthLogic3
                        lda WorkVibratoDepthC,x
                        ora WorkVibratoDepthD,x
                        bne vibDepthLogic2a
                        lda WorkVibratoRate,x
                        sta WorkVibratoDepthE,x
                        sta WorkVibratoDepthB,x
                        lda WorkVibratoDepthA,x
                        asl
                        lda WorkVibratoDepthF,x
                        bcc vibDepthLogic2
                        eor #$ff
                        adc #$00
vibDepthLogic2:         sta WorkVibratoDepthA,x
                        bne vibDepthLogic3a
vibDepthLogic2a:        lda WorkVibratoDepthE,x
                        sta WorkVibratoDepthB,x
                        tya
                        sec
                        sbc WorkVibratoDepthA,x
                        sta WorkVibratoDepthA,x
vibDepthLogic3:         cmp #$00
vibDepthLogic3a:        bpl vibDepthLogic4
                        dey
vibDepthLogic4:         clc
                        adc WorkVibratoDepthC,x
                        sta WorkVibratoDepthC,x
                        tya
                        adc WorkVibratoDepthD,x
                        sta WorkVibratoDepthD,x
endVibratoLogic:        lda WorkVoiceNoteTie,x
                        bmi pvdLogic                    // Is bit 7 set?  Double dotted note
pulseSweepLogic:        lda WorkPulseSweep,x            // See Command: P-S
                        beq pvdLogic
                        clc
                        adc WorkPulseWidth2Lo,x
                        sta WorkPulseWidth2Lo,x
                        lda WorkPulseSweepOther,x
                        adc WorkPulseWidth2Hi,x
                        sta WorkPulseWidth2Hi,x
pvdLogic:               lda WorkPVDA,x                  // See Command: PVD Pulse vibrato depth
                        beq checkForNoteTie
                        ldy #$00
                        dec WorkPVDB,x
                        bne skipAheadPVDB
                        lda WorkPVDC,x
                        ora WorkPVDD,x
                        bne pCDStateLogic
                        lda WorkPulseVibratoRate,x
                        sta WorkPCDState,x
                        sta WorkPVDB,x
                        lda WorkPVDA,x
                        asl
                        lda WorkPulseVibratoDepth,x
                        bcc storePVDAResult
                        eor #$ff
                        adc #$00
storePVDAResult:        sta WorkPVDA,x
                        bne pvdcLogic
pCDStateLogic:          lda WorkPCDState,x
                        sta WorkPVDB,x
                        tya
                        sec
                        sbc WorkPVDA,x
                        sta WorkPVDA,x
skipAheadPVDB:          cmp #$00
pvdcLogic:              bpl skipAheadPVDC
                        dey
skipAheadPVDC:          clc
                        adc WorkPVDC,x
                        sta WorkPVDC,x
                        tya
                        adc WorkPVDD,x
                        sta WorkPVDD,x
checkForNoteTie:        lda WorkVoiceNoteTie,x          // Tie is bit 6 -(bit 7 is double dot, which would make this negative)
                        bpl filterSweepLogic            // If we are a tie not, keep the note playing.
                        jmp endOfProcessNote
filterSweepLogic:       ldy #$00
                        lda WorkFilterSweep,x
                        beq endOfProcessNote
                        bpl sweepLoop
                        iny
sweepLoop:              clc
                        adc WorkFilterSweepState
                        pha
                        and #$07
                        sta WorkFilterSweepState
                        pla
                        ror
                        lsr
                        lsr
                        clc
                        adc DataFilterLookupValue,y
                        clc
                        adc WorkFilterCutOff
                        sta WorkFilterCutOff
endOfProcessNote:       rts

                        // This will move the next command into the current command
                        // The init song routine defaults these values to the same address (Start of Voice x)
                        // Called from ProcessVoice
SetCurrentCmdAddress:   lda WorkNxtCmdLo,x              // Load the next command lo/hi memory address into the fd,fe (Current command memory address)
                        sta $fd
                        lda WorkNxtCmdHi,x
                        sta $fe
                        bne ProcessCurrentCmd
RtsToProcessVoice:      rts

ProcessCmdLoop:         jsr RouteCommand
ProcessCurrentCmd:      lda SID_STATUS
                        and DataVoiceBitMask,x
                        beq RtsToProcessVoice
                        ldy #$00
                        lda ($fd),y                     // Fetch current command byte from (fd,fe)
                        sta $ff                         // Store the current command byte in $FF
                        iny
                        lda ($fd),y                     // Fetch next option by for the command from (fd,fe)
                        tay
                        lda $fd                         // Increase address in fd,fe by 2 - move it to the
                        clc                             // start of the next command byte
                        adc #$02
                        sta $fd                         // Store lo addrss of next comand
                        sta WorkNxtCmdLo,x
                        lda $fe
                        adc #$00
                        sta $fe                         // Store hi address of next command
                        sta WorkNxtCmdHi,x
                        lda $ff                         // $ff=Current command to execute  A=Command Y=Option X=voice # (0..2)
DecodeCommand:          and #%00000011                  // ****
                        bne ProcessCmdLoop              // Notes always end in 00.  01,10 and 11 are commands.

                        // Prepare to Play a note
                        lda WorkNoteFreqLo,x
                        sta WorkTargetNoteFreqLo,x
                        lda WorkNoteFreqHi,x
                        sta WorkTargetNoteFreqHi,x
                        lda $ff                         // Reload the command byte into a

                        // Command: Play a note 
                        sta WorkNoteDuration,x
                        tya
                        sta WorkTypeOctNote,x           // Accidental aa: 10 Normal 01=Sharp 11=Flat   Octive ooo: [EOR of 0-7]   Note nnn: C=001 d=010 e=011 f=100 g=101 a=110 b=111
                        and #%00000111                  // Isolate the Note 0000111
                        tay
                        lda DataNoteLookup,y            // Load something about the note indexed by y where 1=c, 2=d... (We get a $02 or an $FE  (a 2 or a -2)
                        sta DataAccidentalModifier
                        lda WorkTypeOctNote,x
                        and #%00111000                  // Isolate the Octive 00111000
                        lsr
                        lsr
                        lsr                             // Move the bits to positions 0-2
                        adc WorkTPSD,x
                        sta $fd                         // FD = Octive (Numbers in bits 0-2) Octive is 0 based
                        lda WorkTypeOctNote,x
                        and #%11000000                  // Isolate the Type 11000000 (10 Normal 01=Sharp 11=Flat)
                        asl                             // Move the 1 into the carry
                        rol
                        rol                             // Rotate twice to move the bits into positions 0-2
                        tay
                        lda DataAccidentalModifier,y     // Lookup modifyer to make it 1=Sharp, 2=Normal, 3=Flat  *****
                        sta $fe                         // FE = Accidental Modifier  We get back 01=Sharp  00 = Normal  FF (-1) = Flat
                        lda WorkTypeOctNote,x
                        and #%00000111                  // Isolate the Note 0000111
                        beq ProcessNoteSkipAhead        // Branch if it is a rest (Note 000 is a rest)
                        tay                             // Put the Note index in y  (0=Rest, 1=C, 2=D etc...)
                        lda DataHalfStepsFromC,y        // Translates to a halfstep modifier from C where r=255 c=0 d=2 e=4 f=5 g=7 a=9 b=11
                        adc $fe                         // Add in the accidental modifer
                        clc
                        adc WorkTPSHalfSteps,x          // Add in the TPS Value (Transpose note by number of half steps)
                        bpl areWeTooManyHalfSteps       // Are we 0 or more halfsteps above c?
negativeHalfStepsFromC: clc                             // We have negative halfsteps so...
                        adc #12                         // Add 12 half steps to bring us up to a positive number
                        inc $fd                         // We need to move down an octive and add 12 halfsteps from the previous octive (Octive is stored in EORed format so up is down)
areWeTooManyHalfSteps:  cmp #12
                        bcc storeFinalHalfStepCalc      // If somehow we are over 12 in the new octive..
                        sbc #12                         //    Roll it back to where we were
                        dec $fd
storeFinalHalfStepCalc: sta $fe                         // FE = The final halfstep modifer
                        tay                             // Y= Half Steps above C
                        lda DataNoteFreqHi,y            // C = $86 / 134  (Frequency hi)
                        sta $ff
                        lda DataNoteFreqLo,y            // C = $1E / 30   (Frequency Lo)
                        ldy $fd                         // Y = Octive
                        dey                             // Divide the Frequency in half by # of octives
                        bmi applyDetuning
halfPerOctiveLoop:      lsr $ff                         // Divide hi bit half
                        ror                             // Rotate the carry into the frequency lo
                        dey
                        bpl halfPerOctiveLoop
applyDetuning:          clc                             // At this point, $FF = Freq Hi   A= Freq Lo    (From Command: DTN)
                        adc WorkDetuneLo,x              //   Adjust by DetuneLo
                        sta WorkNoteFreqLo,x
                        lda $ff                         //   Adjust by DetuneHi
                        adc WorkDetuneHi,x
                        sta WorkNoteFreqHi,x

                        lda WorkNoteDuration,x
                        bne applyPortamento
                        jmp SetCurrentCmdAddress        // Skip ahead if somehow this is a zero (shouldn't be possible)

applyPortamento:        lda WorkPortamentoLo,x          // Portamento is when a note gradually slides into the next one
                        ora WorkPortamentoHi,x
                        beq finalizeNoteFreq            // No Portamento, skip ahead to finalizing the frequencey we are going to play
                        lda WorkTargetNoteFreqLo,x
                        cmp WorkNoteFreqLo,x
                        lda WorkTargetNoteFreqHi,x
                        sbc WorkNoteFreqHi,x
                        lda #$fe                        // FE = The final halfstep modifer
                        ror
                        sta WorkPortamentoTmp,x
                        bcc processTieNote
ProcessNoteSkipAhead:   beq calculateNoteDuration
finalizeNoteFreq:       sta WorkPortamentoTmp,x
                        lda WorkNoteFreqLo,x
                        sta WorkTargetNoteFreqLo,x
                        lda WorkNoteFreqHi,x
                        sta WorkTargetNoteFreqHi,x
processTieNote:         lda WorkVoiceNoteTie,x
                        asl
                        bne calculateNoteDuration
                        lda WorkPulseSweep,x
                        beq processAutoFilter
                        lda WorkPulseWidthLo,x
                        sta WorkPulseWidth2Lo,x
                        lda WorkPulseWidthHi,x
                        sta WorkPulseWidth2Hi,x
processAutoFilter:      lda WorkAutoFilter,x
                        beq processFilterSweep
                        ldy $fd                         // fd = Octive #
                        clc
                        adc DataOctaveForCutOff,y
                        ldy $fe                         // fe = Half step modifier
                        clc
                        adc DataHalfStepForCutOff,y
                        clc
                        bcc processFilterCutoff
processFilterSweep:     lda WorkFilterSweep,x
                        beq calculateNoteDuration
                        lda WorkFilterCutoffVc,x
processFilterCutoff:    sta WorkFilterCutOff
                        lda #$00
                        sta WorkFilterSweepState
calculateNoteDuration:  lda WorkHoldTime,x
                        sta WorkHoldTimeJifsLeft,x
                        lda WorkNoteDuration,x
                        and #%01000000                  // Is this tied to the next note?
                        sta WorkVoiceNoteTie,x
                        lda WorkNoteDuration,x
                        lsr
                        lsr
                        and #%00000111
                        bne processNoteDurLogic
                        lda WorkNoteDuration,x
                        bmi lookupTempo
                        lda WorkTempo
                        and #$3c
                        bne illegalDurationError
                        lda WorkTempo
                        asl
                        rol
                        rol
                        bne skipToStoreJiffy
                        lda #$04
skipToStoreJiffy:       jmp storeJiffiesLeft
lookupTempo:            lda WorkTempoTableValue
                        beq illegalDurationError
                        and #$3f
                        bne illegalDurationError
                        lda WorkTempoTableValue
                        asl
                        rol
                        rol
                        bne storeJiffiesLeft
illegalDurationError:   lda #%00010000                  // Bit 4: Illegal Note Duration (Example: 16th. 32nd. in default tempo for example)
                        sta SID_STATUS
                        rts

                        // The accumulator starts with a tie flag (1 = a tie is set)
processNoteDurLogic:    cmp #$01
                        bne startNoteLenCalc
                        lda WorkNoteDuration,x
                        and #%00100000
                        bne setToUtilityLen
                        lda WorkUTLValue
                        jmp storeJiffiesLeft
setToUtilityLen:        lda WorkUTVValues,x
                        jmp storeJiffiesLeft
startNoteLenCalc:       tay
                        lda WorkNoteDuration,x
                        and #$a0
                        cmp #$80
                        beq lookupTempoFromTbl
                        sta $ff
                        clc
                        lda WorkTempo
                        bne workTempMinus2
                        sec
workTempMinus2:         dey
                        dey
                        beq doingSomethingHere
rotateLoop:             ror
                        bcs IllegalNoteDuration
                        dey
                        bne rotateLoop
doingSomethingHere:     ldy $ff
                        sta $ff
                        beq storeJiffiesLeft
                        lsr $ff
                        bcs IllegalNoteDuration
                        beq OtherError
                        adc $ff
                        bcs OtherError
                        iny
                        bpl storeJiffiesLeft
                        lsr $ff
                        bcs IllegalNoteDuration
                        adc $ff
                        bcc storeJiffiesLeft
                        bcs OtherError
lookupTempoFromTbl:     lda WorkTempoTableValue
                        beq IllegalNoteDuration
                        dey
                        dey
                        beq storeJiffiesLeft
decreaseJiffy:          lsr
                        bcs IllegalNoteDuration
                        dey
                        bne decreaseJiffy
storeJiffiesLeft:       sta WorkNoteJiffiesLeft,x
                        lda WorkVc1Control,x
                        and #$f6
                        sta WorkVc1Control,x
                        sec
                        lda WorkTypeOctNote,x
                        and #$07
                        bne prepareVoice1Control
                        ror WorkVoiceNoteTie,x
prepareVoice1Control:   lda WorkVc1Control,x
                        adc #$00
                        sta WorkVc1Control,x
                        rts

                        // Stop on 2 different error conditions
IllegalNoteDuration:    lda #%00010000                  // Bit 4: Illegal Note Duration (Example: 16th. 32nd. in default tempo for example)
                        .byte $2c                       // If lda #$10 executes, this and next line of code look like this to the 6502: bit $18a9 (thus the lda #$18 doesn't execute)
OtherError:             lda #%00011000                  // Bits 3+4 Set: (Not sure when yet)
                        sta SID_STATUS
                        rts

                        // Analyze the command byte, and route it to its function
                        // Y=Option byte  $FF contains the command   X=Voice #
                        // rc_... labels are routing by the command byte
                        // Later on, you will see routing by the option byte ro_...
RouteCommand:           tya                             // Start decoding commands.  We are here because the command ends in 01,10 or 11
                        pha                             // Push command option onto the stack
                        lda $ff                         // Load A with command
                        lsr
                        bcc rc________0                 // Bit pattern 10
                        jmp rc________1                 // Bit patterns 01 and 11
rc________0:            lsr                             // This bit is always 1. Only 10 gets to this line of code.
                        lsr
                        bcs rc______110
                        lsr
                        bcs rc_____1010_DTN

                        // Command: P-W Pulse Width  a=4 bits (Value 0-4095 12 bit value)
                        // Pattern: aaaa0010
                        // A=Upper 4 Bits
                        // Stack has lower 8 bits
____00_0_PW:            sta WorkPulseWidthHi,x
                        sta WorkPulseWidth2Hi,x
                        pla
                        sta WorkPulseWidthLo,x
                        sta WorkPulseWidth2Lo,x
                        rts

                        // Command: DTN Detune A=00 -2048 (Hi 4 bits)   A on Stack=Lo 8 bits)  (12 bit value)
                        // Pattern: aaaa1010 or aaaa1010
                        // x=voice
                        // aaaa = hi byte   (This comes in as aaaS1010) where S triggers the valued to be ORed or not
                        // Stack has the lo byte
rc_____1010_DTN:        lsr                             // Skips the ora if a positive
                        bcc rc____01010_DTN
                        ora #%11111000                  // If nnn110n0 (Negative number))
rc____01010_DTN:        sta WorkDetuneHi,x              // If nnn010n0
                        pla
                        sta WorkDetuneLo,x
                        rts


rc______110:            lsr
                        bcs rc_____1110
                        jmp rc_____0110
rc_____1110:            lsr
                        bcs rc____11110
                        lsr
                        bcs rc___101110
                        bne rc___001110_HLD             // a=01 01001110 HLD

                        // Comand: F-C Filter Cutoff
                        // Pattern: 00001110
                        // Stack has value 0-255)
                        // X= Voice
rc_00001110_FC:         pla
                        sta WorkFilterCutoffVc,x
                        sta WorkFilterCutOff
                        rts

                        // Command: HLD Hold Time
                        // Pattern: 01001110
                        // Stack  has value 0-255
                        // x=voice
rc___001110_HLD:        pla                             // HLD Hold Time A=0-255
                        sta WorkHoldTime,x
                        rts

rc___101110:            bne rc___101110_SCA             // a=01 101110_SCA / a != zero

                        // Command: RTP Relative Transpose
                        // Pattern: 00101110                           a=0 00101110 is RTP
                        // Stack has value  -47 to 47
                        // x=voice     Like transpose, but a little different (See Page 211 of the Compute book)
rc_00101110_RTP:  {     pla
                        sta WorkTPSC,x
                        cmp #$5b
                        beq valueZero
                        tay
                        lsr
                        lsr
                        lsr
                        sec
                        sbc #$0b
                        clc
                        adc WorkTPSHalfSteps,x
                        bmi skipAhead
                        cmp #$0c
                        bcc skipSomeMore
                        sbc #$0c
                        dec WorkTPSD,x
                        jmp skipSomeMore
skipAhead:              cmp #$f5
                        bcs skipSomeMore
                        adc #$0c
                        inc WorkTPSD,x
skipSomeMore:           sta WorkTPSHalfSteps,x
                        tya
                        and #$07
                        sec
                        sbc #$03
                        clc
                        adc WorkTPSD,x
                        sta WorkTPSD,x
                        rts
valueZero:              lda WorkTPSA,x
                        sta WorkTPSD,x
                        lda WorkTPSB,x
                        sta WorkTPSHalfSteps,x
                        rts
}

                        // Command: SCA Scale Modulation
                        // Pattern: 01101110  a=01 in 01101110
                        // X Voice
rc___101110_SCA:        pla
                        sta WorkVoiceWavScale,x
                        rts


rc____11110:            lsr
                        bcs rc___111110_JIF

                        // Command: MS# Measure # (0-999)
                        // Pattern: aa011111
                        // x=voice
                        // aa = 00-11 hi byte
                        // Stack has the lo byte
                        // 00 011110    $00-$FF   MS#  Measure # (0-255)
                        // 01 011110    $00-$FF   MS#  Measure # (256-511)
                        // 10 011110    $00-$FF   MS#  Measure # (512-767)
                        // 11 011110    $00-$E7   MS#  Measure # (768-999)
rc___0111_1_MSN:        sta WorkMeasureHi,x
                        pla
                        sta WorkMeasureLo,x
                        rts

                        // Command: JIF Command (-200 to 757) Adjusts WorkCiaTimerA altering the length of a jiffy (no longer 1/60 second)
                        // Pattern: aa111110
                        // aa = New CIA Timer A lo byte
                        // Stack hsa the hi byte for CIA Timer A
                        //                             Example: A = 00000011
rc___111110_JIF:        lsr                             //  A = 00000001 c=1
                        ror                             //  A = 10000000 C=1
                        ror                             //  A = 11000000  (Carry is now clear for addition below)
                        adc DATA_CIA_TIMER_A            //  Add this to the hardware detected lo of Cycles Per Second (PAL vs NTSC)
                        sta WorkCiaTimerA               //  Store it in our WorkCiaTimerA which will get loaded on the CIA A Timer
                        pla                             //  This is the passed in option value
                        adc DATA_CIA_TIMER_A + 1        //  Add with carry to the High value of the hardware detected hi byte cycler per second (PAL vs NTSC)
                        sta WorkCiaTimerA + 1
                        rts                             //  See DATA_CIA_TIMER_A for more info

rc_____0110:            lsr
                        bcc rc____00110
                        jmp rc____10110
rc____00110:            lsr
                        bcs rc___100110
                        lsr
                        bcs rc__1000110
                        lsr
                        bcs rc_10000110_VRT

                        // Command: TEM Tempo
                        // Pattern: 00000110
                        // Option Tempo Value from User Interface (See Tempo Table in attached doc file)
rc_00000110_TEM:        pla                             // TEM $06 Tempo
                        sta WorkTempo                   // Value See table above
                        lsr                             // $90 (Tempo 100) = 10010000
                        lsr                             //
                        lsr                             // Turn it into an index 0-31 by shifting the bits right 3 places
                        tay                             // Now it is 00010010  (18)
                        lda DataTempoTable,y            // Lookup the value in this lookup table (18th place is $60)
                        sta WorkTempoTableValue         // Store it here
                        rts


                        // Command: VRT Vibrato Rate
                        // Pattern: 10000110
                        // Stack has value 0-127
                        // X = Voice
rc_10000110_VRT:        pla
                        sta WorkVibratoRate,x
                        rts


rc__1000110:            lsr
                        bcs rc_11000110_PVD


                        // Command: FLG Flag
                        // Pattern: 01000110
                        // Stack has the Flag Value
rc_01000110_FLG:        pla
                        sta FLAG_STATUS
                        rts

                        // Command: PVD Pulse Vibrato Depth
                        // Pattern: 11000110
                        // Stack has value 0-127)
                        // X Voice
rc_11000110_PVD:{       pla
                        beq isZero
                        sta WorkPulseVibratoDepth,x
                        ldy WorkPVDA,x
                        bne pvdDone
                        sta WorkPVDA,x
                        lda #$01
                        sta WorkPVDB,x
pvdDone:                rts
isZero:                 sta WorkPVDA,x
                        sta WorkPVDC,x
                        sta WorkPVDD,x
                        rts
}


rc___100110:            lsr
                        bcs rc__1100110
                        lsr
                        bcs rc_10100110_TPS

                        // Command: PNT Release Point
                        // Pattern: 00100110
                        // Stack has value 0-255
                        // x=voice #.
rc_00100110_PNT:        pla
                        sta WorkReleasePoint,x
                        rts

                        // Command: TPS Transpose (-95 to 95) Add or subtract half steps to the note playing
                        // Pattern: 10100110
                        // x=Voice
rc_10100110_TPS: {      pla
                        ldy #$00
                        lsr
                        bcc noCarry
                        iny
                        clc
noCarry:                pha
                        and #%00000111
                        adc DataTPSLookupA,y
                        sta WorkTPSA,x
                        sta WorkTPSD,x
                        pla
                        lsr
                        lsr
                        lsr
                        clc
                        adc DataTPSLookupB,y
                        sta WorkTPSB,x
                        sta WorkTPSHalfSteps,x
                        lda #$5b
                        sta WorkTPSC,x
                        rts
}


rc__1100110:            lsr
                        bcs rc_11100110_MAX

                        // Command: F-S Filter Sweep
                        // Pattern: 0110010
                        // Stack has value (-128 to 127)
                        // X Voice
rc_01100110_FS:         pla
                        sta WorkFilterSweep,x
                        rts

                        // Command: MAX
                        // Pattern: 11100110
                        // Stack has the value (0-255)
rc_11100110_MAX:        pla
                        sta WorkMaxModulation
                        rts


rc____10110:            lsr
                        bcs rc___110110
                        lsr
                        bcs rc__1010110
                        lsr
                        bcs rc_10010110_AUT

                        // Command: UTL Utility
                        // Pattern: 00010110
                        // Stack has value A=0-255 (Global) (Default value for a song is 12)
rc_00010110_UTL:        pla
                        sta WorkUTLValue
                        rts

                        // Command: AUT Auto Filter
                        // Pattern: 10010110
                        // From Stack = (-128 - 127)
                        // X=Voice
rc_10010110_AUT:        pla
                        sta WorkAutoFilter,x
                        rts


rc__1010110:            lsr
                        bcs rc_11010110_PVR

                        // Command P-S Pulse Sweep
                        // Pattern: 01010110
                        // Stack has value (-128 to 127) (Per Voice)
                        // X=Voice
rc_01010110_PS:    {    pla
                        sta WorkPulseSweep,x
                        ldy #$00
                        asl                             // If number is negative, carry will be 1
                        bcc valueIsPositive
                        dey
valueIsPositive:        tya
                        sta WorkPulseSweepOther,x
                        rts
}

                        // Command: PVR Pulse-Width Vibratro bit rate
                        // Pattern: 11010110
                        // Stack has value (0-127)
rc_11010110_PVR:        pla
                        sta WorkPulseVibratoRate,x
                        rts


rc___110110:            lsr
                        bcs rc__1110110
                        lsr
                        bcs rc_10110110_AUX

                        // Command: HED Set Head (Start of repeat section)
                        // Pattern: 00110110
                        // x=voice #
                        // Stack has Repeat section number
rc_00110110_HED:        pla                             // Command: HED
                        sta WorkHEDRepeatCnt,x          // Store current head value here (How many times to repeat this section)
                        lda $fd                         // $FD,$FE is memory address of NEXT command after HED
                        sta WorkCmdAfterHEDLo,x         // Lo of next command after HED
                        lda $fe
                        sta WorkCmdAfterHEDHi,x         // Hi of next command after HED
                        lda WorkVoiceForCallPhrase,x
                        sta WorkVoiceForHEDDef,x
                        rts


                        // Command: AUX Auxilary Command
                        // Pattern: 10110110
                        // Stack has value from 0-255
                        // This is for future expansion of the player
                        // Install your own routine at AuxRoutineAddrLoHi either with a custom build of this player
                        // or overwrite the memory address with your routine AFTER the player has been loaded into memory
                        // The default built in routine is just an RTS.
                        // A = Value on the stack before the call
rc_10110110_AUX:        pla                             // Command: AUX Auxilary Command (A from Stack 0-255)
                        jmp (AuxRoutineAddrLoHi)        // Jump to whatever routine is lo/hi at that address. by default the routine is just an RTS

rc__1110110:            lsr
                        bcs rc_11110110_UTV

                        // Command: VDP Vibrato Depth  (Goes hand in hand with VRT Vibrato Rate (0-127)
                        // Pattern: 01110110
                        // Stack has value (0-127)
rc_01110110_VDP:  {     pla
                        bne notZero
                        sta WorkVibratoDepthA,x         // If 0
                        sta WorkVibratoDepthC,x
                        sta WorkVibratoDepthD,x
                        rts
notZero:                sta WorkVibratoDepthF,x         // If > 0
                        ldy WorkVibratoDepthA,x
                        bne vdpDone
                        sta WorkVibratoDepthA,x
                        lda #$01
                        sta WorkVibratoDepthB,x
vdpDone:                rts
}
                        // Command: UTV Utility Set Jiffy Per Voice
                        // Pattern: 11110110
                        // x=voice
                        // Stack has the value
rc_11110110_UTV:        pla
                        sta WorkUTVValues,x
                        rts

rc________1:            lsr
                        bcc rc_______01

                        // Command: POR Portamento
                        // Pattern: aaaaaa 11
                        // aaaaaa=6 bits Hi Value
                        // On Stack is 8 bits for the low value.  20 bits to a number from 0-16383
rc_______11_POR:        sta WorkPortamentoHi,x
                        pla
                        sta WorkPortamentoLo,x
                        rts

                        // Which 01 command is this?
                        // Pops the option off of the stack, and decides by the option value
rc_______01:            pla
                        lsr
                        bcs ro________1
                        lsr
                        bcs ro_______10
                        lsr
                        bcs ro_naaaa100_ATK

                        // Command: DCY Set Decay
                        // Pattern: ro_aaan0000
                        // aaa=New Decay (0-15) n=ignored
                        // x=voice #
ro_nnnn0000_DCY:        lsr
                        ldy #$f0                        // Setup a mask 11110000
                        bne CombineAtkDcy

                        // Command: ATK Set Attack (0-15)
                        // Pattern: 0aaaa100
                        // naaaa    n=ignored aaaa  (0-15)
                        // x=voice #. A=New Attack (0-15)
ro_naaaa100_ATK:        asl                             // Move attack to be in the high 4 bits (like $D405, $D40C and $D413)
                        asl
                        asl
                        asl
                        ldy #$0f                        // Setup a mask 00001111
CombineAtkDcy:          sta $ff
                        tya
                        bcs CombineReleaseSus           // Command: RLS aaa11000 has carry set  Command: SUS 1aaaa 100
                        and WorkAttackDecays,x
                        ora $ff
                        sta WorkAttackDecays,x
                        rts

                        // Command: SUS Sustain Set A=(0-15)  (Upper 4 bits)
                        // Command: RLS Releast Set A=(0-15)  (Lower 4 bits)
                        // x-voice #.  A=New Sustain (0-15)
CombineReleaseSus:      and WorkSusRelease,x
                        ora $ff
                        sta WorkSusRelease,x
                        rts

ro_______10:            lsr
                        bcs ro______110
                        lsr
                        bcs ro_nnnn1010_RES

                        // Command: Call (A=Phrase #) (x=voice number)
                        // Pattern: nnnn0010
ro_nnnn0010_CALL:       sta $ff                         // Store Phrase # here for a moment
                        lda WorkPhraseCallStack,x
                        cmp DataMaxCallStack,x          // Lookup table where 0=5 1=10 2=15
                        beq MaxInnerCallsError
                        inc WorkPhraseCallStack,x
                        tay
                        lda $fd                         // FD,FE is the memory address of the command after this one
                        sta WorkCallStkNxtCmdAdrLo,y
                        lda $fe
                        sta WorkCallStkNxtCmdAdrHi,y
                        lda WorkVoiceForCallPhrase,x
                        sta WorkCallStkState,y          //  y is Voice 1: 0-4  2:5=9   3:10-14
                        ldy $ff
                        lda PhraseDefHi,y
                        beq NoSuchPhraseError
                        sta $fe
                        lda PhraseDefLo,y
                        sta $fd
                        lda WorkPhraseUnknown,y
                        sta WorkVoiceForCallPhrase,x
                        rts



ro________1:            bcs ro________1b
ro______110:            lsr
                        bcs ro_aaaa1110_VOL

                        // Command: DEF Define Phrase
                        // Pattern: aaaa 0110
                        // aaaa is pattern number (0-23)
                        // x voice
ro_aaaa0110_DEF:        tay
                        lda $fd                         // Load hi of next note address
                        sta PhraseDefLo,y               //   y=phrase index
                        lda $fe                         // Load lo of next note address
                        sta PhraseDefHi,y               //   y=phrase index
                        lda WorkVoiceForCallPhrase,x
                        sta WorkPhraseUnknown,y
                        lda WorkPhraseCallStack,x
                        cmp DataMaxCallStack,x          // Lookup table where 0=5 1=10 2=15
                        beq MaxInnerCallsError
                        inc WorkPhraseCallStack,x
                        tay
                        lda #$00
                        sta WorkCallStkNxtCmdAdrHi,y
                        rts


NoSuchPhraseError:      lda #%00110000
                        .byte $2c                       // This appears as bit $2C $28a9 on a branch to NoSuchPhraseError so the lda in the next line is not executed
MaxInnerCallsError:     lda #%00101000                  // This appears as lda #$28 on a branch to MaxInnerCallsError
                        sta SID_STATUS
                        rts

                        // Command: RES  (Filter: Resonence)
                        // Option Pattern: nnnn1010
                        // a=nnnn=0-15
ro_nnnn1010_RES:        asl                             // Move to bits 4-7 (To be stored in $D417 SID.filterResonanceControl)
                        asl
                        asl
                        asl
                        eor WorkFilterControl           //   Mix with the existing lower 4 bits
                        and #$f0
                        eor WorkFilterControl
                        sta WorkFilterControl
                        rts

                        // Command: VOL Set Volumne
                        // Pattern: 01
                        // Option Pattern: aaaa1110 aaaa=Volumne (0-15)
                        //   a=volume
ro_aaaa1110_VOL:        eor WorkVolAndFilter            // Command VOL $01 00000001 (A=New Volumne (0-15)) (Option with shifted right 4 times to get to the 4 bit volume in A)
                        and #$0f                        //   Ensure volume cannot be higher than 15
                        eor WorkVolAndFilter
                        sta WorkVolAndFilter
                        rts


ro________1b:           lsr
                        bcs ro_______11
                        lsr
                        bcs ro______101_RDN

                        // Command: RUP LFO Ramp up  A=0..31 nnnnn
                        // Pattern: 01
                        // Option Pattern: nnnnn001
ro______001_RUP:        sta WorkLFORampUp
                        rts

                        // Command: RDN (LFO Ramp down A=0..31 xxxxx)
                        // Pattern: 01
                        // Option Pattern: nnnnn101
ro______101_RDN:        sta WorkLFORampDown
                        rts

ro_______11:            lsr
                        bcc ro______011
                        jmp ro______111
ro______011:            lsr                             // This is used to set / clear the carry for decisions made in the routines below
                        tay                             // We now have 4 bits in the accumulator and in Y and bit 3 in the carry
                        beq ro_nnnnc011_BMP             // a/y was 0
                        dey
                        beq ro_0001n011_FLT             // a/y was 1
                        dey
                        beq ro_0101n011_RNG             // a/y was 2
                        dey
                        beq ro_0011n011_SNC             // a/y was 3
                        dey
                        beq ro_0100n011_FX              // a/y was 4
                        dey
                        beq ro_0101n011_3O              // a/y was 5
                        dey
                        beq ro_0110n011_LFO             // A/Y was 6
                        dey
                        beq ro_0111n011_PNV             // A/Y was 7
                        and #%00000111                  // A/y was 15 (0111) Set bit 3=0  (This transforms the A to a number from 0-23 for the phrase commands)
                        ora #%00010000                  //                   Set bit 4=1
                        bcs jumpToCommandCall
                        jmp ro_aaaa0110_DEF
jumpToCommandCall:      jmp ro_nnnn0010_CALL

                        // Command: BMP Bump volume up (c=0) or Down by 1 (c=1)
                        // Pattern: 01
                        // Option Pattern: nnnnc011  n=0 Up  n=1 Down
                        // n is in the carry
ro_nnnnc011_BMP:        ldy WorkVolAndFilter
                        bcs DoBumpDown
                        iny                             // Bump Volume Up By 1 (Carry=0)
                        tya
                        and #$0f
                        bne StoreBumpResult             // do nothing is volume is already 15
                        rts
DoBumpDown:             tya                             // Bump Volume Down by 1 (Carry=1)
                        and #$0f
                        beq DoneBumping                 // do nothing is volume is already 0
                        dey
StoreBumpResult:        sty WorkVolAndFilter            // Volumn is bits 0-3
DoneBumping:            rts


                        // Command: Filter Through
                        // Option Pattern: 0001n011   n=carry
                        // 0=no   1=yes
                        // x voice
ro_0001n011_FLT:  {     lda DataVoiceBitMask,x
                        eor #$ff
                        and WorkFilterControl
                        bcc storeAnswer
                        ora DataVoiceBitMask,x
storeAnswer:            sta WorkFilterControl
                        rts
}

                        // Command: RNG Ring Modulation (c=0 off    c=1 on)
                        // Pattern Option:  0101n011
                        // n will become the carry bit before this routine is called
                        // x is voice
ro_0101n011_RNG:        lda WorkVc1Control,x
                        and #%11111011                  // Mask out bit 2
                        bcc SetVcControlAndRts          // If off, skip ahead and write it out
                        ora #$04                        // Turn the bit on
                        bcs SetVcControlAndRts

                        // Command: SNC Snchronize Voices    carry=0 (No)  1=Yes   x= Voice
                        // Enable or disable the Sync Bit in Voice Control Register $D404, $D40B or $D412
ro_0011n011_SNC:        lda WorkVc1Control,x
                        and #%11111101                  // Mask: 1111 1101 (Turn off Synchronize Voice)
                        bcc SetVcControlAndRts          // Were done
                        ora #%00000010                  // Mask: 0000 0010 (Turn this bit on)
                        bcs SetVcControlAndRts          // Were done


                        // Command: F-X Filter External
                        // Option Pattern: 0100n011  n=carry
                        // 0=no  1=yes
ro_0100n011_FX:{        lda WorkFilterControl
                        and #%11110111                  // Mask out bit 3
                        bcc storeNewFilter              // Carry=0 leave it off
                        ora #$08                        // Carry=1 Flip the bit to on
storeNewFilter:         sta WorkFilterControl
                        rts
}
                        // Command: 3-O Voice 3 Off   (Command and option forms a double negative)
                        // Option Pattern: 0101n011  n goes to carry
                        // 3-O Off Means turns voice 3 on (Carry=0)
                        // 3-O On Means turn voice 3 off  (Carry=1)
ro_0101n011_3O:         lda WorkVolAndFilter
{
                        and #%01111111                  // Turn voice 3 Off
                        bcc storeResult
                        ora #%10000000                  // Turn voice 3 On (Carry=0)
storeResult:            sta WorkVolAndFilter
                        rts
}
                        // Command: LFO (Low-Frequency Oscillation)
                        // Option Pattern: 0110n011
                        // n will in the carry
                        //   Simulate Basic type of waveform.  0=Triangle Type  1=Pulse Wave
                        //   Software Generated Waveform Modulation
                        // Related commands: RUP, RDN and MAX
ro_0110n011_LFO:        tya                             // LFO (c=0 for LFO=0) (c=1 for LFO=1)
                        sta WorkLFO
                        sta WorkLFOA
                        iny
                        sty WorkLFOB                    // 0=LFO=0    1 if LFO 1
                        rol                             // 0=LFO==0   3 if LFO 1
                        sta WorkLFOC
                        rts

                        // Command: P&V
                        // Pattern: 01
                        // Option Pattern: 0111n011 where n is 0=off 1 = on
                        // Carry is now bit n
ro_0111n011_PNV:        tya
                        rol
                        sta WorkPortAndVibrato,x
                        rts

ro______111:            lsr
                        bcs ro_____1111
                        lsr
                        bcs ro_nnn10111_FM

                        // Command: WAV WaveForm Set Commands     Op   Option bits     (x=voice #)
                        // Pattern: aaa 00111
                        // aaa combination of waveforms to set (except noise.  noise is all 0s)
                        // x voice
                        //       000 WAVEFORM:SET 0=N  (NOISE)
                        //       001 WAVEFORM:SET 1=T  (TRIANGE)
                        //       010 WAVEFORM:SET 2=S  (SAWTOOTH)
                        //       100 WAVEFORM:SET 4=P  (PULSE)
                        //       011 WAVEFORM:SET 3=TS
                        //       101 WAVEFORM:SET 5=TP
                        //       110 WAVEFORM:SET 6=SP
                        //       111 WAVEFORM:SET 7=TSP
ro_aaa00111_WAV:        bne NotNoiseWaveForm            // Not noise, skip ahead
                        lda #$08                        // 1 000 = Noise          (3 lower bits are in A   4th bit is set just for noise)
NotNoiseWaveForm:       asl                             // 0 001 = Triangle
                        asl                             // 0 010 = Saw tooth
                        asl                             // 0 100 = Pulse     (These can also be combined (see above table))
                        asl                             // Move the bits into position 4-7 to mirror Voice Control Registers $D404/$D40B/$D412
                        eor WorkVc1Control,x            // Mix in the existing lower 4 bits (gate,sync,ring modulation and test)
                        and #$f0
                        eor WorkVc1Control,x
SetVcControlAndRts:     sta WorkVc1Control,x
                        rts

                        // Command: F-M Filter Mode
                        // Option Pattern: nnn10111
                        //  a=nnn where 0=None 1=Low-pass 2=Band=pass 4=High-pass  (0-7, combines them)
ro_nnn10111_FM:         asl                             // Move the bits into postions 4-6 for $D418 Volume and Filter Select Register
                        asl
                        asl
                        asl
                        eor WorkVolAndFilter
                        and #%01110000                  // Mask out the bits we want to set
                        eor WorkVolAndFilter            // Set our bits
                        sta WorkVolAndFilter
                        rts

ro_____1111:            lsr
                        bcc ro____01111

                        // Command: SRC
                        // A=xxx=0-2
                        // x=voice
                        //    0=Software-Generated waveform  1=OSC3 register 2=ENV3 register
ro_nnn11111_SRC:        sta WorkVoiceWavSrc,x
                        rts


ro____01111:            tay                             // A=3 bits , copy into y
                        beq ro_00001111_TAL
                        dey
                        beq ro_00101111_end             // a/y = 1
                        dey
                        beq ro_01001111_HLT             // a/y = 2

                        // Command: DST Set Destination (x=voice number)
                        // Option Pattern: 1nn 01111
                        //   xx=0-3  where xx = 0=Modulation Off, 1=Frequency, 2=Pulse Width, 3=Filter Cutoff  (Per Voice)
                        //           1 00 = 4 (0 = Modulation Off)
                        //           1 01 = 5 (1 = Frequency)
                        //           1 10 = 6 (2 = Pulse Width)
                        //           1 11 = 7 (3 = Filter Cutoff)
ro_1nn01111_DST:        and #%00000011                  // Strip out that 1 at bit 3 - it's ignored and was used just to route the command here.
                        sta WorkVoiceWavDst,x           // Now this matches the 00=Modulation Off,01=Frequency,10=Pulse Width,11=Filter Cutoff
                        lda #$00
                        sta WorkVoiceDSTA,x
                        sta WorkVoiceDSTB,x
                        sta WorkVoiceDSTC,x
                        sta WorkVoiceDSTD,x
                        sta WorkGlobalDSTA
                        sta WorkGlobalDSTB
                        rts

                        // Command: TAL Tail Go back to most recent HED Head to repeat the section
                        // Option Pattern: 00001111
                        // x=voice #.
ro_00001111_TAL:        lda WorkHEDRepeatCnt,x
{                       beq prepareRepeat               // 0 = Inifinite repeat
                        dec WorkHEDRepeatCnt,x          //   = Decrease repeat count by 1
                        beq exitTALRoutine
prepareRepeat:          lda WorkVoiceForCallPhrase,x
                        cmp WorkVoiceForHEDDef,x
                        bne crossVoiceCallError         // if the voice the hed was defined on is different than this tail
                        lda WorkCmdAfterHEDLo,x         // Load the lo,hi memory location of the command that is just after HED, these notes will play next
                        sta $fd
                        lda WorkCmdAfterHEDHi,x
                        sta $fe
exitTALRoutine:         rts
crossVoiceCallError:    lda #%00111000
                        sta SID_STATUS
                        rts
}

                        // Command: END (End definition of phrase from Command: DEF) (Per Voice)
                        // Pattern: 00101111
                        // x=voice number y=Phrase #
                        // This command is called at the end of a definition AND when a call to a phrase ends.
ro_00101111_end:  {     lda WorkPhraseCallStack,x
                        cmp DataMinCallStack,x
                        beq stopSong
                        dec WorkPhraseCallStack,x
                        tay
                        dey
                        lda WorkCallStkNxtCmdAdrHi,y
                        beq justReturn
                        sta $fe
                        lda WorkCallStkNxtCmdAdrLo,y
                        sta $fd
                        lda WorkCallStkState,y
                        sta WorkVoiceForCallPhrase,x
justReturn:             rts
stopSong:               lda #%00100000
                        sta SID_STATUS
                        rts
}
                        // Command: HLT Halt
                        // Pattern: 01
                        // Option Pattern: 01001111
                        // x=voice #.
ro_01001111_HLT:        lda SID_STATUS                  // Stop Playing Music on voice X (0-2)
                        eor DataVoiceBitMask,x          // Toggle the voice to off
                        sta SID_STATUS
                        lda #$01
                        sta WorkNoteJiffiesLeft,x
                        rts

.label                  PLAYER_ADDRESS_END = *

.print                  "Player Address      : $" + toHexString(PLAYER_ADDRESS,4) + " " + PLAYER_ADDRESS
.print                  "Player Address End  : $" + toHexString(PLAYER_ADDRESS_END,4) + " " + PLAYER_ADDRESS_END
.print                  "Player Address Size :  " + (PLAYER_ADDRESS_END - PLAYER_ADDRESS) + " bytes"
