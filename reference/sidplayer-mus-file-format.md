# Enhanced SID Player .MUS File Format
## Binary Format
.MUS Files are PRG Files.  
All 2 byte values are in Lo/Hi order.
| Bytes | Description | Notes |
| :--: | -- | -- | 
| 0-1 | Load Address | Ignored by SID Editors and Players<br/>SID Editors and Players will load or save the song into their own specific address. |
| 2-3 | Voice 1 Data Size | Count of bytes |
| 4-5 | Voice 2 Data Size | Count of bytes |
| 6-7 | Voice 3 Data Size | Count of bytes |
|  | Voice 1 Data | Always ends in HLT ($01 $4F) |
|  | Voice 2 Data | Always ends in HLT ($01 $4F) |
|  | Voice 3 Data | Always ends in HLT ($01 $4F) |
|  | Song Description | NULL Terminated text describing the song and other misc. data |

## Voice Data Format
Voice data is a stream of command/option pairs represented by 2 bytes of data.  
The voice data will always end with a HLT Halt Command.
The first byte is called the command byte and the second byte is called the option byte leading the reader to believe that the voice data format is simply an 8 bit value referencing a command followed by an 8 bit value.
In reality the bits for the command and data are intermixed across both bytes.
For example, the POR (Portamento) command is just 2 bits to identify the command and 14 bits of data (Value from 0-16383) taken from both the command and option bytes.
## Note Command  
The command is a note when the command byte contains a 0 in bits 0 and 1.  
The command byte contains the duration of the note.  
The option byte contains the accidental, octave and note to be played.  
Not all note durations are available in all tempos.  See tempo chart below.  
### Note Duration Byte (Command Byte)
| Bit(s) | Description | Values |
| :--: | -- | -- |
| 0-1 |  | Always 00 |
| 2-4 | Duration | 010 Whole <br/> 011 Half <br/>100 Quarter <br/>101 Eighth <br/>110 16th <br/> 111 32nd <br/> 000 64th <br/> 001 Utility duration|
| 5 |  Dotted | 0=No 1=Yes |
| 6 |  Tie | 0=No 1=Yes |
| 7 |  Double Dotted | 0=No 1=Yes |

A pair whose command byte is exactly `$00` is an **absolute-pitch** pair rather than a
64th note, and `$04` / `$24` are the utility-duration forms — both sources list them
ahead of the duration table in their decode order. What an absolute-pitch pair's option
byte holds is not documented by either source.

### Note Byte (Option Byte)
| Bit(s) | Description | Values |
| :--: | -- | -- |
| 0-2 | Note to Play | Rest=000 C=001 d=010 e=011 f=100 g=101 a=110 b=111  |
| 3-5 | Octave | Values 0-7.<br/>Stored EORed with 11111111 |
| 6-7 |  Accidental | 10 Normal <br/>01 Sharp <br/>11 FLAT   |

## Command Reference
The commands are grouped together as they appear in the SID Editor.  
0 and 1 are used for pattern matching the command to a routine that processes it.  
bbbbbb shows that the bits that are used as a value.  B is used here to show that the routine is using the entire option byte.  
aaaa shows the bits that are used as a value.  A is used when the routine has the value in the accumulator after LSRing the other bits off in the process of routing the command to the routine.  
c signifies that the bit will end up in the carry flag when the routine is run.  
n signifies bits that are not used at all by the routine.  


| Group          | CMD  | Name | Command | Option  | Voices | Info |
| :-             | :-   | :-   | -:      | -:      | :-:    | :-   |
| **Tempo**      | UTL  | Utility Jiffy   | 00010110  | bbbbbbbb  | All | Utility Jiffy All voices (0-255) |
|                | TEM  | Tempo           | 00000110  | bbbbbbbb  | All | Set Tempo (See tempo chart below for values)
| **Volume**     | VOL  | Volume          |       01  | aaaa1110  | All | Volume aaaa = 0-15 |
|                | BMP  | Bump Up or Dn   |       01  | nnnnc011  | All | Quick Volume Adjust.  c=0 Volume +1   c=1 Volume - Group |
| **Repeat**     | HED  | Head            | 00110110  | bbbbbbbb  |  V  | Start of repeat section.  bbbbbbbb = count of times to repeat the section (0=Infinite) |
|                | TAL  | Tail            |       01  | 00001111  |  V  | Tail - Move the play head back to the first command after the most recent HED / decrement the count and play the section again |
| **Phrase**     | CALL | Call Phrase     |       01  | aaaa0010  |  V  | Call / Play a phrase definition.  Values 0-15 use `aaaa0010`; 16-23 use `nB` (`01 8B` = 16, `01 FB` = 23), i.e. bit 7 set with the value less 8 in bits 6-4 |
|                | DEF  | Define Phrase   |       01  | aaaa0110  |  V  | Start definition of a phrase.  Values 0-15 use `aaaa0110`; 16-23 use `n3` (`01 83` = 16, `01 F3` = 23), i.e. bit 7 set with the value less 8 in bits 6-4 |
|                | END  | End Definition  |       01  | 00101111  |  V  | |
| **Envelope**   | ATK  | Attack          |       01  | 0aaaa100  |  V  |  aaaa = 0-15 |
|                | DCY  | Decay           |       01  | aaaa0000  |  V  |  aaaa = 0-15 |
|                | SUS  | Sustain         |       01  | 1aaaa100  |  V  |  aaaa = 0-15 |
|                | REL  | Release         |       01  | aaaa1000  |  V  |  aaaa = 0-15 |
|                | PNT  | Point Release   | 00100110  | bbbbbbbb  |  V  |  bbbbbbbb = 0-255 |
|                | HLD  | Hold Time       | 01001110  | bbbbbbbb  |  V  |  bbbbbbbb = 0-255 |
| **Waveform**   | P-W  | Pulse Wave      | aaaa 0010 |  bbbbbbbb |  V  | Wave: Pulse-Width value (0-4095) aaaa=Hi 4 bits  bbbbbbbb= Lo 8 bits |
|                | P-S  | Pulse Sweep     |  01010110 |  bbbbbbbb |  V  | Wave: Pulse-Sweep value (-128 to 127) |
|                | PVD  | Pulse Vib. Dp.  |  11000110 |  bbbbbbbb |  V  | Wave: Pulse Vibrato Depth (0-127) |
|                | PVR  | Pulse Vibrato   |  11010110 |  nbbbbbbb |  V  | Wave: Pulse Vibrato BitRate (0-127) |
|                | SNC  | Waveform Sync   |        01 |  0011c011 |  V  | c 0=off 1=on |
|                | RNG  | Ring Modulation |        01 |  0101c011 |  V  | c 0=Off 1=On |
|                | WAV  | Waveform        |        01 | aaa 00111 |  V  | Any combination of waveforms except Noise / Noise = 0 <br/>000 0=Noise         011 3=Triangle and Sawtooth <br/>001 1=Triangle      101 5=Triangle and Pulse <br/>010 2=Sawtooth      110 6=Sawtooth and Pulse <br/>100 4=Pulse <br/> 111 7=Triangle, Sawtooth and Pulse |    
| **Freq**       | VDP  | Vibrato Depth   |  01110110 |  bbbbbbbb |  V  | bbbbbbb=Vibrato Depth (0-127)   |
|                | VRT  | Vibrato Rate    |  10000110 |  bbbbbbbb |  V  | Vibrato Rate (0-127) |
|                | POR  | Portamento      | aaaaaa 11 |  bbbbbbbb |  V  | Portamento (0-16384) aaaaaa bbbbbbbb (Combined to make a 14 bit number) |
|                | P&V  | Prt and Vibr.   |        01 |  0111c011 | All | Portamento and Vibrato c=0 On  c=1 Off |
|                | DTN  | Detune          | aaaa 1010 |  bbbbbbbb |  V  | aaaa = Hi Byte / bbbbbbbb = Lo Byte  (-2048 to 2047) 12 Bit value |
|                | TPS  | Transpose       |  10100110 |  bbbbbbbb |  V  | Adds or subtracts bbbbbbbb (-95 to 95) adjust all notes by half steps |
|                | RTP  | Relative Transp.|  00101110 |  bbbbbbbb |  V  | Relative Transpose (-47 to 47) adjust note by half steps from prev note played |
| **Filter**     | F-M  | Filter Mode     |        01 | aaa 10111 | All | Filter Mode: 0=None 1=Low-pass 2=Band=pass 4=High-pass  (0-7, combines them) |
|                | AUT  | Auto Filter     |  10010110 |  bbbbbbbb |  V  | Auto Filter: (-128 - 127) |
|                | RES  | Resonance Filter|        01 | aaaa 1010 | All | Resonance Filter (0-15) |
|                | FLT  | Filter Through  |        01 |  0001c011 | All | Filter Through 0=No 1=Yes |
|                | F-C  | Filter Cutoff   |  00001110 |  bbbbbbbb |  V  | Filter Cutoff (0-255) |
|                | F-S  | Filter Sweep    |  01100110 |  bbbbbbbb |  V  | Filter Sweep  (-128 to 127)  |
|                | F-X  | Filter:External |        01 |  0100c011 | All | Filter External 0=No 1=Yes      |
| **Modulation** | LFO  | Low Freq. Osc.  |        01 |  0110c011 | All | Low Frequency Oscillation.  Used in Software Generated Waveform Modulation. 0=triangle 1=pulse wave |
|                | RUP  |                 |        01 |  aaaaa001 | All | LFO Rate Up (0-31)|
|                | RDN  |                 |        01 |  aaaaa101 | All | LFO Rate Down (0-31)|
|                | SRC  | Source          |        01 |  aaa11111 |  V  | aaa=0-2  0=Software-Generated waveform  1=OSC3 register 2=ENV3 register|
|                | DST  | Destination     |        01 |  1aa01111 |  V  |  aa=0-3  0=Modulation Off , 1=Frequency, 2=Pulse Width, 3=Filter Cutoff|
|                | SCA  | Mod Scale       |  01101110 |  bbbbbbbb |  V  |Modulation: Scale (-7 to 7)  -7|
|                | MAX  | Modulation Max  |  11100110 |  bbbbbbbb | All | Modulation: Max Value (0-255)|
| **Misc**       | MS#  | Measure #       | aa 011110 |  bbbbbbbb |  V  |  aa = Hi Byte / bb = Lo byte of Measure # (Highest value allowed is 999) |
|                | UTV  | Util. Jif Voice |  11110110 |  bbbbbbbb |  V  |   Utility Jiffy Current Voice  (0-255) |
|                | JIF  | Jiffy Clock     | aa 111110 |  bbbbbbbb | All |  10-bit two's complement value = `byte2 * 4 + bits 7-6 of byte 1`, range -200..757 (`3E 40` = 256, `7E BD` = 757, `3E CE` = -200) |
|                | FLG  | Flag            |  01000110 |  bbbbbbbb | All |  Insert a flag.  A flag will set FLAG_STATUS to the desired value.  **FLG 1 in voice 1 tells the player to display the next line of the `.wds` file** when one exists |
|                | AUX  | Aux             |  10110110 |  bbbbbbbb | All |   AUX Auxiliary Command (0-255) - For future expansion of SID Player |
|                | 3-O  | Voice 3 Off     |        01 |     c011  |  V3 |  c = 0=no  1=yes |
|                | HLT  | HALT            |        01 |  01001111 |  V  |  Stop Playing Voice |


## Tempo Table
Values in jiffys (1/60) of a seconds.  
These are the values used by the TEM command.  
   
|       Value  |  M.M. | Whole  |  Half  |  Quarter  |  8th  |   16th  |  32nd  |   64th |
|       :-:    |   -:  |   -:   |   -:   |   -:      |   -:  |    -:   |   -:   |    -:  |
|       $08    |  1800 |    8   |    4   |    2      |   1   |         |        |        |
|       $10    |   900 |   16   |    8   |    4      |   2   |     1   |        |        |
|       $18    |   600 |   24   |   12   |    6      |   3   |         |        |        |
|       $20    |   450 |   32   |   16   |    8      |   4   |     2   |     1  |        |
|       $28    |   360 |   40   |   20   |   10      |   5   |         |        |        |
|       $30    |   300 |   48   |   24   |   12      |   6   |     3   |        |        |
|       $38    |   257 |   56   |   28   |   14      |   7   |         |        |        |
|       $40    |   225 |   64   |   32   |   16      |   8   |     4   |     2  |   1    |
|       $48    |   200 |   72   |   36   |   18      |   9   |         |        |        |
|       $50    |   180 |   80   |   40   |   20      |  10   |     5   |        |        |
|       $58    |   163 |   88   |   44   |   22      |  11   |         |        |        |
|       $60    |   150 |   96   |   48   |   24      |  12   |     6   |     3  |        |
|       $68    |   138 |  104   |   52   |   26      |  13   |         |        |        | 
|       $70    |   128 |  112   |   56   |   28      |  14   |     7   |        |        |
|       $78    |   120 |  120   |   60   |   30      |  15   |         |        |        |
|       $80    |   112 |  128   |   64   |   32      |  16   |     8   |     4  |    2   |
|       $88    |   105 |  136   |   68   |   34      |  17   |         |        |        |
|       $90    |   100 |  144   |   72   |   36      |  18   |     9   |        |        |
|       $98    |    94 |  152   |   76   |   38      |  19   |         |        |        |
|       $A0    |    90 |  160   |   80   |   40      |  20   |    10   |     5  |        |
|       $A8    |    85 |  168   |   84   |   42      |  21   |         |        |        |
|       $B0    |    81 |  176   |   88   |   44      |  22   |    11   |        |        |
|       $B8    |    78 |  184   |   92   |   46      |  23   |         |        |        |
|       $C0    |    75 |  192   |   96   |   48      |  24   |    12   |     6  |   3    |
|       $C8    |    72 |  200   |  100   |   50      |  25   |         |        |        |
|       $D0    |    69 |  208   |  104   |   52      |  26   |    13   |        |        |
|       $D8    |    66 |  216   |  108   |   54      |  27   |         |        |        |
|       $E0    |    64 |  224   |  112   |   56      |  28   |    14   |     7  |        |
|       $E8    |    62 |  232   |  116   |   58      |  29   |         |        |        |
|       $F0    |    60 |  240   |  120   |   60      |  30   |    15   |        |        |
|       $F8    |    58 |  258   |  124   |   62      |  31   |         |        |        |
|       $00    |    56 |  256   |  128   |   64      |  32   |    16   |     8  |   4    |

## Cross-checks

Two further descriptions of this format circulate with the Compute's Gazette Sid
Collection - *The format of Enhanced Sidplayer MUSic files* (Peter Weighill) and
*SIDPLAYER MUSIC FILE CONTENTS* (Dick Thornton). Both agree with this file on the
header, the note pair, the command set and the tempo basis, and both are fuller than
this file in places (Weighill tabulates every byte pair; Thornton explains the packed
fields). Where they disagree with this file:

- **DST** puts the destination in bits 6-5 of the option byte: `8F` off, `AF`
  frequency, `CF` pulse width, `EF` filter cutoff. Thornton's "101 = 2" does not
  match the pattern or Weighill's byte table.
- **The double-dotted bit.** Thornton reports a *triplet* when bit 7 is set and bit 5
  is clear. The player body in `src/libs/sidplay.inc` is the authority for it.
- **MS#** reaches 1023 in the encoding (bits 7-6 of byte 1 plus byte 2); the editor
  caps the value at 999.
- **Utility durations.** `$04` and `$24` are note-form pairs whose duration is a
  utility count rather than a note length; neither source pins down their subject.
