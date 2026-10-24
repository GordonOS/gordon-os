// ==========================================
//  sidplay.inc -- COMPUTE!'s Enhanced Sidplayer, ported to GordonOS
// ==========================================
//  Player body (wrapped by sidplay.asm). Behaviour is unchanged -- the
//  IRQ driver was extracted into a yield-driven tick:
//      sidTick     = the old IRQ_Handler body (one jiffy of processing)
//      sidInitSong = INIT_SONG (X/Y = .mus data address; pass data+2 to
//                    skip the 2-byte .mus load-address field)
//      sidHush     = silence all three SID voices (no timer reset)
//  ZP scratch ($FB-$FF + $61-$64) is remapped into the task-owned region
//  $02-$0A (declared in the wrapper header so the scheduler DMA-preserves
//  it). These are transient scratch only -- all song state is in Work*.
// ==========================================

                        .cpu _6502NoIllegals

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

.namespace sidplay {

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
//  The accidental, indexed by the option byte's two type bits (6-7), which the note path
//  shifts down to 0-3 unchanged: 1 = Sharp, 2 = Normal, 3 = Flat (C64 Programmers Ref,
//  and `reference/sidplayer-mus-file-format.md`). Index 0 is the note path's scratch:
//  the format has no 00 type, so only 1, 2 and 3 are ever read.
//  WARNING: FOUR ENTRIES, BECAUSE FLAT IS TYPE 3. DataPitchOct is the very next byte, so a
//  three-entry table makes a flat read THAT byte and take it for its modifier: a flat then
//  plays as a natural (0) at 100% and as a semitone SHARP (1, the machine-speed shift) at
//  200%. Keep the fourth entry, and keep it last.
DataAccidentalModifier: .byte $00
                        .byte $01,$00                   // 01=Sharp  00 = Normal
                        .byte $FF                       // FF (-1) = Flat

//  The caller's transpose, written by the wrapper from kArg+2/kArg+3 (the host task
//  owns the kArg block, so these are latched once per kSidPlay call). They live HERE,
//  outside the work window sidInitSong clears, so a transpose survives a song restart,
//  and the note path adds them AFTER the song's own TPS -- the two compose. The reason
//  the option exists: VICE's -speed runs the WHOLE machine, the SID's clock included,
//  faster than real time, so at -speed 200 every pitch doubles and a tune needs one
//  octave down ($FF).
DataPitchOct:           .byte 0                         // octaves, signed (+1 = an octave down)
DataPitchSemi:          .byte 0                         // semitones, signed
DataHalfStepsFromC:     .byte $FF,$00,$02,$04           // In order (starting with FF)  0=Rest 1=C 2=d 3=e 4=f 5=g 6=a 7=b
                        .byte $05,$07,$09,$0B           // Translates to half step modify to the notes from C r=255 c=0 d=2 e=4 f=5 g=7 a=9 b=11

                        // Frequency Look up Table : See C64 Programmers Ref Appendix E for table of values
                        // Index 0  = $861E / 34334   C Octave 7
                        // Index 11 = $FD2E / 64814   B Octave 7
                        //  To calculate an octave, divide the number in half per octave
                        //
                        //  The archive's own table, and it is the NTSC one: $861E is C7 on that
                        //  standard's 1022727 Hz clock to the last bit. A PAL machine reads the
                        //  PAL copy below instead -- see *Tuning* in `docs/lib-sid.md`.
DataNoteFreqLo:         .byte $1E,$18,$8B,$7E           // Frequency Lo Table (Starting at C Octave 7 - See C64 Programmers Ref Appendix E)
                        .byte $FA,$06,$AC,$F3
                        .byte $E6,$8F,$F8,$2E

DataNoteFreqHi:         .byte $86,$8E,$96,$9F           // Frequency Hi Table (Starting at C Octave 7 - See C64 Programmers Ref Appendix E)
                        .byte $A8,$B3,$BD,$C8
                        .byte $D4,$E1,$EE,$FD
.assert "sidlib: the NTSC tables are an entry per note, Lo under Hi", ((DataNoteFreqHi - DataNoteFreqLo) == 12) && ((* - DataNoteFreqHi) == 12) ? 1 : 0, 1

//  ---- THE PAL COPY OF THE TABLE. The table above is the ARCHIVE's own and it is the NTSC
//  one: $861E is C7 on a 1022727 Hz clock to the last bit. A machine's system clock follows
//  its video standard -- 985248 Hz PAL against 1022727 Hz NTSC -- so the same register value
//  sounds 3.8% (0.646 semitones) FLAT on PAL. Every entry here is the one above scaled by
//  NTSC/PAL and halved, so the NOTES are the same on either machine, and sidDetectVideo picks
//  the pair.
//
//  WARNING: THIS TABLE IS ONE OCTAVE DOWN -- $459C is C6, not C7, and the note path relies
//  on that. Scaling the archive's values up by 1.03804 pushes B7 past the 16-bit frequency
//  register ($106D0), and a table that clamped THAT entry would clamp it before the octave
//  halving: the halving divides the clamped value, so every B in every octave would come out
//  45-52 cents flat -- one sour pitch class, which is far more audible than a tune that is
//  uniformly flat by a smaller amount. An octave down, every entry fits, so there is no clamp
//  in the data at all: a PAL note is halved ONE TIME FEWER than its octave field says, and an
//  octave-7 note is DOUBLED (see the note path). That is exact for all twelve notes except B
//  at octave 7, where doubling lands on the clamp at $FFFF -- 45.5 cents flat on the highest
//  note alone, against 64.7 on all twelve with no compensation at all.
//  Both tables are an entry per note, which is what the asserts below hold.
DataNoteFreqLoPal:      .byte $9C,$C0,$23,$C8
                        .byte $B4,$EB,$72,$4C
                        .byte $80,$12,$08,$68
DataNoteFreqHiPal:      .byte $45,$49,$4E,$52
                        .byte $57,$5C,$62,$68
                        .byte $6E,$75,$7C,$83
.assert "sidlib: the PAL tables are an entry per note, Lo under Hi", ((DataNoteFreqHiPal - DataNoteFreqLoPal) == 12) && ((* - DataNoteFreqHiPal) == 12) ? 1 : 0, 1

//  Which table the note path reads, from sidDetectVideo: $FF = not measured yet, 0 = NTSC,
//  1 = PAL. It is NOT in the work window sidInitSong clears, so a song restart does not
//  re-measure -- one measurement per library load.
DataVideoStd:           .byte $FF

//  ---- STREAMED IMAGE ----------------------------------------------------
//  The player addresses the song by FILE OFFSET, and turns an offset into an
//  address where it fetches a command:
//
//      StreamMode 0 -- the image is in RAM at SidBase:  address = SidBase + offset
//      StreamMode 1 -- the image is paged from the REU:  address = winHi:offsetLo
//
//  The window trick depends on two things: each window is PAGE-ALIGNED and 256
//  bytes, so the address of a command is simply that window's high byte over the
//  offset's low byte (no arithmetic at all); and commands are always two bytes at
//  an even offset, so a command and its option byte can never straddle the end of
//  a window.
//
//  These live after DataVideoStd, outside the work window sidInitSong clears, so
//  a song restart does not lose the configuration.
StreamMode:             .byte 0                          // 0 = RAM image, 1 = paged from the REU
SidBaseLo:              .byte 0                          // StreamMode 0: the image's base address
SidBaseHi:              .byte 0
StreamPtrLo:            .byte 0                          // StreamMode 1: the file's FS offset, 24-bit
StreamPtrMid:           .byte 0
StreamPtrHi:            .byte 0
StreamWin:              .fill 6, 0                       // one 256-byte, page-aligned window per
                                                        //   voice, interleaved: lo, hi
StreamPage:             .byte $FF,$FF,$FF                // the file page each window holds ($FF = none)
StreamVoice:            .byte 0                          // scratch: the voice during a fetch
.assert "sidlib: sidInitSong's two clear loops stop short of the video flag",
         (((SID_STATUS + $bc) < DataVideoStd) && ((WorkPhraseCallStack + 2 + $72) < DataVideoStd)) ? 1 : 0, 1

DataOctaveForCutOff:    .byte $8C,$78,$05,$50           // Stored in reverse order.. Octave 7 first then to 0
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

//  sidDetectVideo -- PAL or NTSC? Measured from the VIC, ONCE per library load.
//  The archive's frequency table is the NTSC one, and a machine's system clock follows its
//  video standard, so the same register value sounds 3.8% flat on PAL. This chooses the table
//  that makes the NOTES right on either machine, and that choice is what the note path reads.
//  PAL wraps at 312 raster lines and NTSC at 263, so lines 263-311 exist only on PAL -- and
//  they are visible only through $d011's NINTH bit, which is what this polls for. The poll is
//  bounded by a PAL frame in cycles (7 x 256 iterations ~ 19656), so an NTSC machine pays
//  ~20 ms once and a PAL one catches its window within a frame.
//  WARNING: the KERNAL's $02A6 is NOT a video flag. It is that ROM's RS232 baud-table
//  selector, filled in only when RS232 is opened, so on a machine that never did it holds
//  whatever RAM held at reset -- 0 on VICE -- and a `beq` on it would say NTSC everywhere.
//  Clobbers A, X, Y. (There is no DataPitchOct-style host argument: this takes none.)
sidDetectVideo: {
.const sddPalLo = 263 - 256     // 263 is the first PAL-only line, and bit 7 already told
                                //   us the line is 256 or above, so this is that line's LOW
                                //   byte. NEVER compare the raw 263: an 8-bit immediate
                                //   truncates, which would read as 7 and be right only by
                                //   accident of where the border sits.
    lda DataVideoStd
    bpl sddDone                 // already measured: 0 = NTSC, 1 = PAL
    ldx #0
    ldy #0
sddPoll:
    lda $d011
    bmi sddWindow               // bit 7 set: the raster line is 256 or above
sddNext:
    inx
    bne sddPoll
    iny
    cpy #7                      // one PAL frame's worth of polls
    bcc sddPoll
    lda #0                      // the window never came: NTSC
    sta DataVideoStd
    rts
sddWindow:
    lda $d012
    cmp #sddPalLo               // low bytes 0-6 are 256-262, the lower border, which BOTH
    bcc sddNext                 //   standards reach -- only 7+ is PAL's 263-311
    lda #1
    sta DataVideoStd
sddDone:
    rts
}

// -----------------------------------------------------------------
// -----------------------------------------------------------------
//              sidInitSong - initialize the player for a new song
//              X/Y = .mus data address (pass data+2 to skip the 2-byte
//              load-address field). Clears work memory, sets defaults,
//              computes the three voice data pointers.
// -----------------------------------------------------------------
sidInitSong: {
                        lda #$00
                        sta SID_STATUS                  // Turn off all voices
                        stx $02                         // Store lo,hi of address of music in zero page
                        sty $03
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
                        lda #$ff
                        sta WorkMaxModulation
                        lda #$d4                        // Use ZP $D3,$D4 to write to SID Register $D4xx
                        sta $05

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
                        sta $04
                        lda #$00
                        tay
                        sta ($04),y
                        iny
                        sta ($04),y
                        iny
                        sta ($04),y
                        lda #$08
                        sta WorkPulseWidth2Hi,x
                        sta WorkPulseWidthHi,x
                        iny
                        sta ($04),y
                        iny
                        sta ($04),y
                        lda #$40
                        sta WorkVc1Control,x
                        sta ($04),y
                        lda #$20
                        sta WorkAttackDecays,x
                        iny
                        sta ($04),y
                        lda #$f5
                        sta WorkSusRelease,x
                        iny
                        sta ($04),y
                        dex
                        bpl setInitVoiceValuesLoop

                        txa                             // a = $FF
                        ldx #23                         // Set WorkPhraseUnknown C13E - C155 to $FF  (24 memory positions)
initWorkPhraseLoops:    sta WorkPhraseUnknown,x
                        dex
                        bpl initWorkPhraseLoops

                        lda #$08                        // the song proper starts at file offset 8:
                        sta $04                         //   2 bytes of load address and three size
                        lda #$00                        //   words ahead of the first event, so this
                        tax                             //   is where voice 1 begins
                        tay
addressOfVoiceLoop:     sta $05                         // a= hi of voice x data
                        sta WorkNxtCmdHi,x              // store hi of voice x data
                        sta WorkCmdAfterHEDHi,x         // store hi of voice x data
                        lda $04                         // a= lo of voice x data
                        sta WorkNxtCmdLo,x              // store lo of voice 1+x data
                        sta WorkCmdAfterHEDLo,x         // store lo of voice 1+x data
                        clc
                        adc ($02),y
                        sta $04
                        lda $05
                        iny
                        adc ($02),y
                        iny
                        inx
                        cpx #$03
                        bne addressOfVoiceLoop

                        ldx $04                         // x=lo a,y=hi address of text area for song info
                        tay
                        rts
}

// -----------------------------------------------------------------
//              sidWinFetch -- DMA file page StreamPage,x into voice X's window
//              sidWinSeat  -- seat that window on the page of WorkNxtCmd and
//                             leave its address in $08/$09
// -----------------------------------------------------------------
//  sidWinFetch clobbers kArg..kArg+5 and the FS scratch, so it runs with
//  interrupts off: kReuDmaRange takes the offset from the kernel-global
//  reuFsPtr, and a task that used the filesystem in between would overwrite it.
//  kReuDmaRange itself handles a page that spans two REU banks.
sidWinFetch: {
                        php
                        sei
                        stx StreamVoice
                        txa
                        asl
                        tay                          // y = voice * 2: its window's entry
                        lda StreamPtrLo
                        sta reuFsPtr                 // the file's base, byte 0
                        lda StreamPtrMid
                        clc
                        adc StreamPage,x             // + page * 256
                        sta reuFsPtr+1
                        lda StreamPtrHi
                        adc #$00
                        sta reuFsPtr+2
                        lda StreamWin,y
                        sta kPtr
                        lda StreamWin+1,y
                        sta kPtr+1
                        lda #$00
                        sta kArg+6                   // length = $0100
                        lda #$01
                        sta kArg+7
                        jsr kReuDmaRange
                        plp
                        ldx StreamVoice
                        rts
}

sidWinSeat: {
                        lda WorkNxtCmdHi,x
                        cmp StreamPage,x             // same page as the window holds?
                        beq sWsSeated
                        sta StreamPage,x
                        jsr sidWinFetch
sWsSeated:              txa
                        asl
                        tay
                        lda WorkNxtCmdLo,x           // the window is page-aligned, so the
                        sta $08                      //   offset's low byte IS the address's
                        lda StreamWin+1,y
                        sta $09
                        rts
}

// -----------------------------------------------------------------
//              sidStreamBegin -- seat voice 1's window on page 0 and
//              point X/Y at the three size words inside it
// -----------------------------------------------------------------
sidStreamBegin: {
                        ldx #0
                        lda #$00
                        sta StreamPage
                        jsr sidWinFetch
                        lda StreamWin                // the size words are at file offset 2,
                        clc                          //   which page 0 puts at win0 + 2
                        adc #$02
                        tax
                        lda StreamWin+1
                        adc #$00
                        tay
                        rts
}

// -----------------------------------------------------------------
//              Hush
//              Silence the three voices and to reset the hardware timer interrupt rate.
// -----------------------------------------------------------------
                        * = *                   "sidHush (silence voices)"
sidHush:                lda #$00
                        sta SID.voice1Control
                        sta SID.voice2Control
                        sta SID.voice3Control
                        sta SID.voice1FrequencyHi       //  Voice 1 Frequency Control (high byte)
                        sta SID.voice2FrequencyHi       //  Voice 2 Frequency Control (high byte)
                        sta SID.voice3FrequencyHi       //  Voice 3 Frequency Control (high byte)
                        lda #%00001000
                        sta SID.filterResonanceControl  //  Filters off on all voices
                        rts
// -----------------------------------------------------------------
// -----------------------------------------------------------------
//              sidTick early-exit paths (no IRQ chaining)
// -----------------------------------------------------------------
sidTickErr:             lda #%00001000                  // stop all 3 voices (bit 3 = re-entered)
                        sta SID_STATUS
sidTickRts:             rts

// -----------------------------------------------------------------
// -----------------------------------------------------------------
//              sidTick - process one jiffy of the song
//              Called by the task loop once per scheduler slice; the
//              60 Hz scheduler tick is the player's jiffy clock.
// -----------------------------------------------------------------
sidTick:                lda SID_STATUS
                        bmi sidTickErr                  // bit 7 set -- re-entered (shouldn't happen)
                        ora #%10000000                  // Set bit 7 and existing voices (bit 0-2)
                        tay                             // Store above in y
                        and #%00000111                  // Test if any voice is on (bit 0-2)
                        beq sidTickRts                  // No voices on -- nothing to do
                        cld
                        sty SID_STATUS                  // bit 7 on + whatever voices are already on
                        lda WorkFilterSweepState
                        clc
                        adc WorkGlobalDSTA
                        pha
                        and #$07
                        tay
                        lda WorkGlobalDSTB
                        adc #$00
                        sta $0a
                        pla
                        lsr $0a
                        ror
                        lsr $0a
                        ror
                        lsr $0a
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
                        sta $07
                        ldx #$00                        // fb,fc will be used to quickly set $d400 (Start of SID voice Registers) in the next section
                                                        // X is now also 0 which is used to index data by voice


{                       // Apply current values to the SID Register for each voice
applyToSIDRegsLoop:     lda SID_STATUS                  // Loop for each voice (x=0..2)
                        and DataVoiceBitMask,x          // Check to see if voice x is on
                        beq goToNextVoice
                        lda SIDVoiceRegsLo,x
                        sta $06                         // fb,fc is now setup for voice x registers ($D400=Voice 0, $D407=Voice 1, $D40E=Voice 2)
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
                        sta ($06),y                     // $D400,$D407, $D40E Voice x Frequency Control (Lo byte)
                        pla
                        adc WorkVoiceDSTB,x
                        iny
                        sta ($06),y                     // $D401,$D408, $D40F Voice x Frequency Control (Hi byte)
                        lda WorkPulseWidth2Lo,x
                        clc
                        adc WorkPVDC,x
                        sta $0a
                        lda WorkPulseWidth2Hi,x
                        adc WorkPVDD,x
                        pha
                        lda $0a
                        clc
                        adc WorkVoiceDSTC,x
                        iny
                        sta ($06),y                     // $D402,$D409, $D410 Voice x Pulse Waveform Width (Lo byte)
                        pla
                        adc WorkVoiceDSTD,x
                        iny
                        sta ($06),y                     // $D403,$D40A, $D411 Voice x Pulse Waveform Width (Hi byte)
                        lda WorkAttackDecays,x
                        iny
                        iny
                        sta ($06),y                     // $D405,$D40C, $D413 Voice x Attack/Decay Register
                        lda WorkSusRelease,x
                        iny
                        sta ($06),y                     // $D406,$D40D, $D414 Voice x Sustain/Release Control Register
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

                                                // (CIA timer untouched -- the scheduler is the jiffy clock)
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
                        jmp sidTickDone           // If there any errors, (all voices will be shutoff) we can continue on.  The player will stop playing the song.
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
                        sta $0a
                        ldy WorkVoiceWavSrc,x
                        lda WorkLFO,y
                        ldy WorkVoiceWavScale,x
                        beq skipAheadToDST
                        bmi dstloop
wavLoop:                asl
                        rol $0a
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
                        lda $0a
                        sta WorkVoiceDSTB,x
                        jmp nextDSTVoiceLoop
skipAheadToDSTC:        dey
                        bne skipAheadToDSTA
                        sta WorkVoiceDSTC,x
                        lda $0a
                        sta WorkVoiceDSTD,x
                        jmp nextDSTVoiceLoop
skipAheadToDSTA:        sta WorkGlobalDSTA
                        lda $0a
                        sta WorkGlobalDSTB
nextDSTVoiceLoop:       inx
                        cpx #$03
                        bne wavDSTVoiceLoop
}

                                                // End of the tick: clear bit 7 (re-entrancy flag)
                        lda SID_STATUS
                        and #%01111111                  // Turn bit 7 off to signal the tick is done
sidTickDone:            sta SID_STATUS
                        rts
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

                        // $08/$09 = the address of the command at WorkNxtCmd,x. The
                        // three voice pointers are FILE OFFSETS, so where their bytes
                        // are depends on the mode.
sidCmdAddress:  {       lda StreamMode
                        bne scaStream
                        lda WorkNxtCmdLo,x              // RAM image: address = base + offset
                        clc
                        adc SidBaseLo
                        sta $08
                        lda WorkNxtCmdHi,x
                        adc SidBaseHi
                        sta $09
                        rts
scaStream:              jmp sidWinSeat                  // paged: seat the window, address from it
}

RtsToProcessVoice:      rts
ProcessCmdLoop:         jsr RouteCommand

                        // Process the command at WorkNxtCmd,x. The pointer is a FILE
                        // OFFSET, so $08/$09 (where the command's bytes are) is derived
                        // here, every pass: the advance below moves the offset on, and a
                        // handler that loads a new one -- CALL, END, TAL -- just returns
                        // through this label. The init song routine leaves the three
                        // voices' pointers at their stream starts.
                        // Called from ProcessVoice.
SetCurrentCmdAddress:
ProcessCurrentCmd:      lda SID_STATUS
                        and DataVoiceBitMask,x
                        beq RtsToProcessVoice
                        jsr sidCmdAddress
                        ldy #$00
                        lda ($08),y                     // Fetch current command byte from (fd,fe)
                        sta $0a                         // Store the current command byte in $FF
                        iny
                        lda ($08),y                     // Fetch next option by for the command from (fd,fe)
                        tay
                        clc                             // every command is a byte and an option
                        lda WorkNxtCmdLo,x              //   byte, so the next command is two
                        adc #$02                        //   offsets on
                        sta WorkNxtCmdLo,x
                        bcc nxtCmdNoCarry
                        inc WorkNxtCmdHi,x
nxtCmdNoCarry:
                        lda $0a                         // $ff=Current command to execute  A=Command Y=Option X=voice # (0..2)
DecodeCommand:          and #%00000011                  // ****
                        bne ProcessCmdLoop              // Notes always end in 00.  01,10 and 11 are commands.

                        // Prepare to Play a note
                        lda WorkNoteFreqLo,x
                        sta WorkTargetNoteFreqLo,x
                        lda WorkNoteFreqHi,x
                        sta WorkTargetNoteFreqHi,x
                        lda $0a                         // Reload the command byte into a

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
                        clc
                        adc DataPitchOct                // the caller's octaves, on top of the song's
                        sta $08                         // FD = Octive (Numbers in bits 0-2) Octive is 0 based
                        lda WorkTypeOctNote,x
                        and #%11000000                  // Isolate the Type 11000000 (10 Normal 01=Sharp 11=Flat)
                        asl                             // Move the 1 into the carry
                        rol
                        rol                             // Rotate twice to move the bits into positions 0-2
                        tay
                        lda DataAccidentalModifier,y     // Lookup modifyer to make it 1=Sharp, 2=Normal, 3=Flat  *****
                        sta $09                         // FE = Accidental Modifier  We get back 01=Sharp  00 = Normal  FF (-1) = Flat
                        lda WorkTypeOctNote,x
                        and #%00000111                  // Isolate the Note 0000111
                        bne pnNotRest                   // A rest (Note 000) skips the pitch: out
                        jmp ProcessNoteSkipAhead        //   of branch range, the octave fix is between
pnNotRest:              tay                             // Put the Note index in y  (0=Rest, 1=C, 2=D etc...)
                        lda DataHalfStepsFromC,y        // Translates to a halfstep modifier from C where r=255 c=0 d=2 e=4 f=5 g=7 a=9 b=11
                        adc $09                         // Add in the accidental modifer
                        clc
                        adc WorkTPSHalfSteps,x          // Add in the TPS Value (Transpose note by number of half steps)
                        clc
                        adc DataPitchSemi               // and the caller's semitones
                        bpl areWeTooManyHalfSteps       // Are we 0 or more halfsteps above c?
negativeHalfStepsFromC: clc                             // We have negative halfsteps so...
                        adc #12                         // Add 12 half steps to bring us up to a positive number
                        inc $08                         // We need to move down an octive and add 12 halfsteps from the previous octive (Octive is stored in EORed format so up is down)
areWeTooManyHalfSteps:  cmp #12
                        bcc storeFinalHalfStepCalc      // If somehow we are over 12 in the new octive..
                        sbc #12                         //    Roll it back to where we were
                        dec $08
storeFinalHalfStepCalc: sta $09                         // FE = The final halfstep modifer
                        tay                             // Y= Half Steps above C
                        lda DataVideoStd                // measured once per library load: the
                        bne noteFreqPal                 //   archive's own table is the NTSC one
                        lda DataNoteFreqHi,y            // C = $86 / 134  (Frequency hi)
                        sta $0a
                        lda DataNoteFreqLo,y            // C = $1E / 30   (Frequency Lo)
                        jmp noteFreqHave
noteFreqPal:            lda DataNoteFreqHiPal,y         // PAL: the same notes, scaled for
                        sta $0a                         //   985248 Hz and one octave down
                        lda DataNoteFreqLoPal,y
                        ldy $08                         // Y = Octive
                        beq nfPalTop                    // octave 7: the table is an octave low
                        dey                             // ...so every other octave is halved
                        jmp nfHalve                     //   one time fewer than it says
nfPalTop:               asl                             // octave 7: double the octave-6 entry
                        rol $0a                         //   back up -- which fits for every
                        bcc nfPalOk                     //   note but B
                        lda #$ff                        // B's $106D0 does not fit: clamp it
                        sta $0a                         //   (A = $FF, so the Lo clamps too)
nfPalOk:                jmp applyDetuning
noteFreqHave:           ldy $08                         // Y = Octive
nfHalve:                dey                             // Divide the Frequency in half by # of octives
                        bmi applyDetuning
halfPerOctiveLoop:      lsr $0a                         // Divide hi bit half
                        ror                             // Rotate the carry into the frequency lo
                        dey
                        bpl halfPerOctiveLoop
applyDetuning:          clc                             // At this point, $FF = Freq Hi   A= Freq Lo    (From Command: DTN)
                        adc WorkDetuneLo,x              //   Adjust by DetuneLo
                        sta WorkNoteFreqLo,x
                        lda $0a                         //   Adjust by DetuneHi
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
                        ldy $08                         // fd = Octive #
                        clc
                        adc DataOctaveForCutOff,y
                        ldy $09                         // fe = Half step modifier
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
                        sta $0a
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
doingSomethingHere:     ldy $0a
                        sta $0a
                        beq storeJiffiesLeft
                        lsr $0a
                        bcs IllegalNoteDuration
                        beq OtherError
                        adc $0a
                        bcs OtherError
                        iny
                        bpl storeJiffiesLeft
                        lsr $0a
                        bcs IllegalNoteDuration
                        adc $0a
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
                        lda $0a                         // Load A with command
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
                        adc #$00                        // (no CIA timer base -- jiffy is the scheduler tick)
                        sta WorkCiaTimerA               //  Store it in our WorkCiaTimerA which will get loaded on the CIA A Timer
                        pla                             //  This is the passed in option value
                        adc #$00
                        sta WorkCiaTimerA + 1
                        rts

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
                        lda WorkNxtCmdLo,x              // the command after HED, as a file offset
                        sta WorkCmdAfterHEDLo,x
                        lda WorkNxtCmdHi,x
                        sta WorkCmdAfterHEDHi,x
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
CombineAtkDcy:          sta $0a
                        tya
                        bcs CombineReleaseSus           // Command: RLS aaa11000 has carry set  Command: SUS 1aaaa 100
                        and WorkAttackDecays,x
                        ora $0a
                        sta WorkAttackDecays,x
                        rts

                        // Command: SUS Sustain Set A=(0-15)  (Upper 4 bits)
                        // Command: RLS Releast Set A=(0-15)  (Lower 4 bits)
                        // x-voice #.  A=New Sustain (0-15)
CombineReleaseSus:      and WorkSusRelease,x
                        ora $0a
                        sta WorkSusRelease,x
                        rts

ro_______10:            lsr
                        bcs ro______110
                        lsr
                        bcs ro_nnnn1010_RES

                        // Command: Call (A=Phrase #) (x=voice number)
                        // Pattern: nnnn0010
ro_nnnn0010_CALL:       sta $0a                         // Store Phrase # here for a moment
                        lda WorkPhraseCallStack,x
                        cmp DataMaxCallStack,x          // Lookup table where 0=5 1=10 2=15
                        beq MaxInnerCallsError
                        inc WorkPhraseCallStack,x
                        tay
                        lda WorkNxtCmdLo,x              // the command after this one, as an offset
                        sta WorkCallStkNxtCmdAdrLo,y
                        lda WorkNxtCmdHi,x
                        sta WorkCallStkNxtCmdAdrHi,y
                        lda WorkVoiceForCallPhrase,x
                        sta WorkCallStkState,y          //  y is Voice 1: 0-4  2:5=9   3:10-14
                        ldy $0a
                        lda PhraseDefHi,y               // an undefined phrase has BOTH bytes clear:
                        ora PhraseDefLo,y               //   offsets are never 0, they start at 8
                        beq NoSuchPhraseError
                        lda PhraseDefHi,y
                        sta WorkNxtCmdHi,x
                        lda PhraseDefLo,y
                        sta WorkNxtCmdLo,x
                        lda WorkPhraseUnknown,y
                        sta WorkVoiceForCallPhrase,x
                        rts                             // the loop head derives the new address



ro________1:            bcs ro________1b
ro______110:            lsr
                        bcs ro_aaaa1110_VOL

                        // Command: DEF Define Phrase
                        // Pattern: aaaa 0110
                        // aaaa is pattern number (0-23)
                        // x voice
ro_aaaa0110_DEF:        tay
                        lda WorkNxtCmdLo,x              // the phrase starts at the next command
                        sta PhraseDefLo,y               //   y=phrase index
                        lda WorkNxtCmdHi,x
                        sta PhraseDefHi,y
                        lda WorkVoiceForCallPhrase,x
                        sta WorkPhraseUnknown,y
                        lda WorkPhraseCallStack,x
                        cmp DataMaxCallStack,x          // Lookup table where 0=5 1=10 2=15
                        beq MaxInnerCallsError
                        inc WorkPhraseCallStack,x
                        tay
                        lda #$00                       // tombstones the slot: clear BOTH bytes,
                        sta WorkCallStkNxtCmdAdrHi,y   //   because a real offset is never 0
                        sta WorkCallStkNxtCmdAdrLo,y
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
                        lda WorkCmdAfterHEDLo,x         // the command just after HED plays next
                        sta WorkNxtCmdLo,x
                        lda WorkCmdAfterHEDHi,x
                        sta WorkNxtCmdHi,x
                        rts                             // the loop head derives the new address
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
                        lda WorkCallStkNxtCmdAdrHi,y    // an unused slot is clear in BOTH bytes:
                        ora WorkCallStkNxtCmdAdrLo,y    //   offsets never are, they start at 8
                        beq justReturn
                        lda WorkCallStkNxtCmdAdrHi,y
                        sta WorkNxtCmdHi,x
                        lda WorkCallStkNxtCmdAdrLo,y
                        sta WorkNxtCmdLo,x
                        lda WorkCallStkState,y
                        sta WorkVoiceForCallPhrase,x
                        rts                             // the loop head derives the new address
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

}                       // end namespace sidplay