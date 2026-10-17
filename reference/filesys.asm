; simple ROM filesystem for 6502 V 1.00
; uses all bytes, no blocks, no pages, just bytes
; initially for Vic 20 web server

; *********************************************************************
; constants for the file system

none		=	$00			; no flags, no parent

; flags:

deleted	=	$80			; file is deleted but not removed
directory	=	$40			; file is really a directory


; *********************************************************************
; this sets up the code for the 6502 simulator

	*=	$0000				; all in page zero for now

	JMP	LAB_test			; go test the code (for 6502 sim)

; *********************************************************************
; variables for the file system

; zero page

filesys_l	.ds	1			; file system pointer low byte
filesys_h	.ds	1			; file system pointer high byte

lensys_l	.ds	1			; file system length low byte
lensys_h	.ds	1			; file system length high byte

file_l	.ds	1			; current file pointer low byte
file_h	.ds	1			; current file pointer high byte

length_l	.ds	1			; current file length low byte
length_h	.ds	1			; current file length high byte

fileflags	.ds	1			; current file's flag byte

findname_l	.ds	1			; filename to find pointer low byte
findname_h	.ds	1			; filename to find pointer high byte


; *********************************************************************

; Notes:
;	filenames are case sensetive
;	no dates, modified or other extra attributes
;	"/" and $00 are not allowed in filenames
;	filename length is limited to 255 bytes
;	files don't have to be in alpha order
;	files and directories can be mixed

; file format is ..

f_start	.ds	1			; flag byte
						; bit	function
						; ---	--------
						;  7	deleted
						;  6	directory
						; 5-0	unassigned
f_length	.ds	2			; file payload length word
f_name	.ds	1			; filename and null terminator byte
;		.ds	length		; here to here+length-1 is the file payload


; *********************************************************************
; first the test code. this code is for the 6502 simulator and is just
; to demonstrate the routines working. If breakpoints are set to after
; the LAB_find calls you can examine the Carry flag and pointers after
; the routine returns.

	*=	$1000


LAB_test
	LDA	#<file_00			; get test pointer low byte
	STA	findname_l			; save as find name low byte
	LDA	#>file_00			; get test pointer high byte
	STA	findname_h			; save as find name high byte
	JSR	LAB_find			; go find file

	LDA	#<file_01			; get test pointer low byte
	STA	findname_l			; save as find name low byte
	LDA	#>file_01			; get test pointer high byte
	STA	findname_h			; save as find name high byte
	JSR	LAB_find			; go find file

	LDA	#<file_02			; get test pointer low byte
	STA	findname_l			; save as find name low byte
	LDA	#>file_02			; get test pointer high byte
	STA	findname_h			; save as find name high byte
	JSR	LAB_find			; go find file


	BRK					; end of test code

; different strings to test the file find

file_00
	.byte	"/"				; return index.html
	.byte	$00				; 
file_01
	.byte	"/images"			; return not found, eventually
	.byte	$00				; 
file_02
	.byte	"/images/e.gif"		; return images/e.gif
	.byte	$00				; 


; *********************************************************************
; find file, searches the file system for the filename pointed to by
; the find name pointer. returns with Cb = 1 and the pointer to the
; file payload in filesys, if found.
;
; file name to search for should be "/[path[path]][name]" where path is
; the directory name followed by a "/". the path may be many deep and
; both the path and the name may be null. if the name is null then the
; default_file name will be searched for the path given.

LAB_find
	LDA	root+$01			; get root file payload length low byte
	STA	lensys_l			; save payload length low byte
	LDA	root+$02			; get root file payload length high byte
	STA	lensys_h			; save payload length high byte

	LDA	#<root_b			; get file system root body pointer low byte
	STA	filesys_l			; set file system pointer low byte
	LDA	#>root_b			; get file system root body pointer high byte
	STA	filesys_h			; set file system pointer high byte
	LDY	#$00				; clear index
	LDA	(findname_l),Y		; get the first byte of the name to find
	CMP	#'/'				; compare with separator
	BNE	LAB_exit_n_found		; exit if not "/" at start

	JSR	LAB_directory		; search the root directory for the file
	BVC	LAB_exit_find		; exit if it's a file

	JSR	LAB_find_default		; if it's a directory go find the default file
LAB_exit_find
	RTS


; flag file found and exit

LAB_exit_found
	BIT	fileflags			; test the flags byte, set Vb for a directory
	SEC					; flag found
	RTS


; flag file not found and exit

LAB_exit_n_found
	CLV					; flag not a directory
	CLC					; flag not found
	RTS


; increment filesys to the next file pointer in the directory

LAB_nextfile
	CLC					; clear carry for add
	LDA	filesys_l			; get filesys low byte
	ADC	#$02				; increment to next pointer
	STA	filesys_l			; save filesys low byte
	BCC	ni_inc_h			; branch if no rollover

	INC	filesys_h			; else increment filesys high byte
ni_inc_h
	SEC					; set carry for subtract
	LDA	lensys_l			; get remaining length low byte
	SBC	#$02				; increment to next pointer
	STA	lensys_l			; save remaining length low byte
	BCS	LAB_comparefile		; branch if no rollunder

	DEC	lensys_h			; decerment remaining length high byte

LAB_comparefile
	LDA	lensys_l			; get remaining directory length low byte
	ORA	lensys_h			; OR remaining directory length high byte
	BEQ	LAB_exit_n_found		; exit if no more directory entries

	LDY	#$00				; clear index
	LDA	(filesys_l),Y		; get file pointer low byte
	STA	file_l			; save this file pointer low byte
	INY					; increment index
	LDA	(filesys_l),Y		; get file pointer high byte
	STA	file_h			; save this file pointer high byte

	DEY					; clear index
	LDA	(file_l),Y			; get this file's flags
	STA	fileflags			; save the file's flag byte
	INY					; point to payload length low byte
	LDA	(file_l),Y			; get this file's payload length low byte
	STA	length_l			; save this file's payload length low byte
	INY					; point to payload length high byte
	LDA	(file_l),Y			; get this file's payload length high byte
	STA	length_h			; save this file's payload length high byte

	CLC					; clear carry for add
	LDA	file_l			; get this file pointer low byte
	ADC	#f_name-f_start		; add offset to the file name
	STA	file_l			; save this file pointer low byte
	BCC	nf_inc_h			; branch if no rollover

	INC	file_h			; else increment this file pointer high byte
nf_inc_h
	LDY	#$FF				; set so first increment clears index

; compare this file's name, pointed to by file, with the find file
; name pointed to by findname. exits with Y indexed to the next byte
; in the name if the whole name matched.

LAB_comparename
	INY					; increment index
	LDA	(file_l),Y			; get next byte of name to test
	BEQ	LAB_cnameexit		; exit if end of name (match)

	EOR	(findname_l),Y		; compare with next byte of name to find
	BEQ	LAB_comparename		; loop if character match

	BNE	LAB_nextfile		; branch if not this file

LAB_cnameexit
	LDA	(findname_l),Y		; get next byte of name to find
	BEQ	LAB_end_find		; branch if end of name to find

	CMP	#'/'				; compare with separator
	BNE	LAB_nextfile		; branch if not end of name to find

LAB_end_find
	PHA					; save next byte of name to find

						; name matched so update lensys
	LDA	length_l			; get length low byte
	STA	lensys_l			; save length low byte
	LDA	length_h			; get length high byte
	STA	lensys_h			; save length high byte

						; name matched so update filesys
	SEC					; set carry for add +1, past $00
	TYA					; copy offset
	ADC	file_l			; add this file pointer low byte
	STA	filesys_l			; save as file system pointer low byte
	LDA	file_h			; get this file pointer high byte
	ADC	#$00				; add in the carry
	STA	filesys_h			; save as file system pointer high byte

	PLA					; restore next byte of name to find
	BEQ	LAB_exit_found		; branch if end of name to find

	BIT	fileflags			; else test the flags byte
	BVC	LAB_exit_n_found		; branch if not a directory


; searches the directory pointed to by the file system pointer for the
; filename pointed to by the find name pointer. the routine is entered
; with filesys pointing to the first directory byte after the file header
; and lensys holding the remaining directory size

LAB_directory				; ok so far so update findname
	SEC					; set carry for add +1, past "/" or $00
	TYA					; copy offset
	LDY	#$00				; clear index
	ADC	findname_l			; add offset to findname low byte
	STA	findname_l			; save findname low byte
	TYA					; set $00 for add carry
	ADC	findname_h			; add findname high byte
	STA	findname_h			; save findname high byte

	LDA	(findname_l),Y		; get next byte of name to find
	BNE	LAB_comparefile		; go compare file if not null filename

LAB_find_default				; else get default file for this directory
	LDA	#<default_file		; get default filename pointer low byte
	STA	findname_l			; set pointer of name to find low byte
	LDA	#>default_file		; get default filename pointer high byte
	STA	findname_h			; set pointer of name to find high byte
	BNE	LAB_comparefile		; go compare file (branch always)


; *********************************************************************
; this is the default file in all directories. this will be the target
; if the path ends in "/" and can be a unique file in each directory

default_file
	.byte	"index.html",$00		; 


; *********************************************************************
; this is the test data for the filesystem. it is made up of the HTML
; pages and other files for the Vic 20 mini web server

; structure for this trial is ..

; root+--errors+--404			; error 404, really an html file
;	|
;	+--images+--e.gif			; small .gif image
;	|
;	+--b.html				; button page
;	+--index.html			; default page
;	+--o.html				; other page


; *********************************************************************
; the root file is always a directory

root
	.byte	directory			; flags
	.word	root_end-root_b		; file payload length
	.byte	$00				; filename, null in this case
root_b
	.word	errors			; errors directory
	.word	images			; images directory
						; done directories, now do files
	.word	b_html			; the button page
	.word	index				; dummy file for test
	.word	index_html			; default web page
	.word	o_html			; other web page
root_end


; *********************************************************************
; dummy file for test

index
	.byte	none				; flags
	.word	$0000				; file payload length
	.byte	"index",$00			; filename, null terminator

; *********************************************************************
; default web page, served for "GET / "

index_html
	.byte	none				; flags
	.word	index_end-index_html_b	; file payload length
	.byte	"index.html",$00		; filename, null terminator
						; rest is file payload
index_html_b
	.byte	"HTTP/1.0 200 OK",$0D,$0A,"Content-Type: text/html",$0D,$0A
	.byte	$0D,$0A
	.byte	"<HTML><HEAD><TITLE>Vic 20 WEB Server</TITLE></HEAD>"
	.byte	"<BODY BGCOLOR=#CC6502 TEXT=WHITE>"
	.byte	"<TABLE ALIGN=CENTER WIDTH=80% HEIGHT=90%>"
	.byte	"<TR><TD VALIGN=CENTER>"
	.byte	"<FONT SIZE=+1>Vic 20 WEB Server </FONT>"
	.byte	"- <A HREF=b.html>The button page</A>"
	.byte	" - <A HREF=o.html>The other page</A>"
	.byte	"<P>"
	.byte	"<FONT SIZE=+4><B>Welcome</B></FONT>"
	.byte	"<FONT FACE=Courier> - to the default page</FONT>"
	.byte	"<P>"
	.byte	"<A HREF=mailto:leeedavison@lycos.co.uk>&nbsp;e-mail "
	.byte	"me <IMG SRC=images/e.gif BORDER=0 ALT=e-mail></A>"
	.byte	"</TD></TR></TABLE></BODY></HTML>"
index_end

; *********************************************************************
; other web page, served for "GET /o.html"

o_html
	.byte	none				; flags
	.word	o_html_end-o_html_b	; file payload length
	.byte	"o.html",$00		; filename, null terminator
						; rest is file payload
o_html_b
	.byte	"HTTP/1.0 200 OK",$0D,$0A,"Content-Type: text/html",$0D,$0A
	.byte	$0D,$0A
	.byte	"<HTML><HEAD><TITLE>Vic 20 WEB Server</TITLE></HEAD>"
	.byte	"<BODY BGCOLOR=#65CC02 TEXT=WHITE>"
	.byte	"<TABLE ALIGN=CENTER WIDTH=80% HEIGHT=90%>"
	.byte	"<TR><TD VALIGN=CENTER>"
	.byte	"<FONT SIZE=+1>Vic 20 WEB Server </FONT>"
	.byte	"- <A HREF=>The home page</A>"
	.byte	" - <A HREF=b.html>The button page</A>"
	.byte	"<P>"
	.byte	"<FONT SIZE=+4><B>Welcome</B></FONT>"
	.byte	"<FONT FACE=Courier> - to the other page</FONT>"
	.byte	"<P>"
	.byte	"<A HREF=mailto:leeedavison@lycos.co.uk>&nbsp;e-mail "
	.byte	"me <IMG SRC=images/e.gif BORDER=0 ALT=e-mail></A>"
	.byte	"</TD></TR></TABLE></BODY></HTML>"
o_html_end


; *********************************************************************
; the button page, served for "GET /b.html"

b_html
	.byte	none				; flags
	.word	b_html_end-b_html_b	; file payload length
	.byte	"b.html",$00		; filename, null terminator
						; rest is file payload
b_html_b
	.byte	"HTTP/1.0 200 OK",$0D,$0A,"Content-Type: text/html",$0D,$0A
	.byte	$0D,$0A
	.byte	"<HTML><HEAD><TITLE>Vic 20 WEB Server</TITLE></HEAD>"
	.byte	"<BODY BGCOLOR=#065C02 TEXT=WHITE>"
	.byte	"<TABLE ALIGN=CENTER WIDTH=90% HEIGHT=90%>"
	.byte	"<TR><TD VALIGN=CENTER>"
	.byte	"<FONT SIZE=+1>Vic 20 WEB Server </FONT>"
	.byte	"- <A HREF=>The home page</A>"
	.byte	" - <A HREF=o.html>The other page</A>"
	.byte	"<P>"
	.byte	"<FONT SIZE=+4><B>Welcome</B></FONT>"
	.byte	"<FONT FACE=Courier> - to the button page</FONT>"
	.byte	"<P>"
	.byte	"<A HREF=mailto:leeedavison@lycos.co.uk>&nbsp;e-mail "
	.byte	"me <IMG SRC=images/e.gif BORDER=0 ALT=e-mail></A></TD>"
	.byte	"<TD WIDTH=20% ALIGN=CENTER>Are you easily confused?"
	.byte	"<FORM METHOD=GET ACTION=b.html>"
	.byte	"<PRE>"
	.byte	"yes <INPUT TYPE=RADIO NAME=4 VALUE=0>",$0D,$0A
	.byte	"no  <INPUT TYPE=RADIO NAME=4 VALUE=1>",$0D,$0A
	.byte	"fish<INPUT TYPE=RADIO NAME=4 VALUE=2 CHECKED>"
	.byte	"</PRE>"
	.byte	"<INPUT TYPE=SUBMIT VALUE=Submit>"
	.byte	"</FORM></TD></TR></TABLE></BODY></HTML>"
b_html_end


; *********************************************************************
; errors directory

errors
	.byte	directory			; flags
	.word	errors_end-errors_b	; file payload length
	.byte	"errors",$00		; filename, null terminator
errors_b
	.word	e_404				; error 404, served for not found files
errors_end


; error 404, served for not found files

e_404
	.byte	none				; flags
	.word	e_404_end-e_404_b		; file payload length
	.byte	"404",$00			; filename, null terminator
						; rest is file payload
e_404_b
	.byte	"HTTP/1.0 200 OK",$0D,$0A,"Content-Type: text/html",$0D,$0A
	.byte	$0D,$0A
	.byte	"<HTML><HEAD><TITLE>Error 404</TITLE></HEAD>"
	.byte	"<BODY BGCOLOR=#CC0265 TEXT=WHITE>"
	.byte	"<TABLE ALIGN=CENTER WIDTH=80% HEIGHT=90%>"
	.byte	"<TR><TD VALIGN=CENTER>"
	.byte	"<FONT SIZE=+1>Vic 20 WEB Server </FONT>"
	.byte	"- <A HREF=>The home page</A>"
	.byte	"<P>"
	.byte	"<FONT SIZE=+4><B>Error 404</B></FONT>"
	.byte	"<FONT FACE=Courier> - File not found</FONT>"
	.byte	"<P>"
	.byte	"<A HREF=mailto:leeedavison@lycos.co.uk>&nbsp;e-mail "
	.byte	"me <IMG SRC=images/e.gif BORDER=0 ALT=e-mail></A>"
	.byte	"</TD></TR></TABLE></BODY></HTML>"
e_404_end


; *********************************************************************
; images directory

images
	.byte	directory			; flags
	.word	images_end-images_b	; file payload length
	.byte	"images",$00		; filename, null terminator
images_b
	.word	e_gif				; small image
images_end


; small image, served for "GET /email.gif"

e_gif
	.byte	none				; flags
	.word	e_gif_end-e_gif_b		; file payload length
	.byte	"e.gif",$00			; filename, null terminator
						; rest is file payload
e_gif_b
	.byte	"HTTP/1.0 200 OK",$0D,$0A,"Content-Type: image/gif",$0D,$0A
	.byte	$0D,$0A,$47,$49,$46,$38,$39,$61,$10,$00,$10,$00,$C4,$00,$00
	.byte	$F7,$00,$5F,$33,$03,$9A,$33,$03,$CA,$03,$03,$83,$5F,$5F,$F7
	.byte	$33,$63,$FA,$63,$9B,$FB,$63,$CB,$FB,$03,$9B,$CB,$63,$FA,$FA
	.byte	$FA,$FA,$CB,$97,$00,$00,$CB,$03,$03,$FA,$FA,$FA,$63,$63,$63
	.byte	$03,$03,$03,$FF,$FF,$FF,$00,$00,$00,$00,$00,$00,$00,$00,$00
	.byte	$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00
	.byte	$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00,$00
	.byte	$00,$00,$00,$00,$00,$00,$21,$F9,$04,$01,$00,$00,$10,$00,$2C
	.byte	$00,$00,$00,$00,$10,$00,$10,$00,$00,$05,$69,$20,$24,$8E,$C6
	.byte	$91,$24,$63,$9A,$96,$49,$11,$18,$AA,$28,$40,$85,$59,$40,$46
	.byte	$A0,$3A,$8E,$20,$20,$07,$01,$6F,$38,$7C,$38,$1A,$3F,$84,$40
	.byte	$C1,$6C,$36,$8D,$C8,$02,$62,$E0,$64,$02,$18,$0A,$E8,$60,$BB
	.byte	$AD,$32,$16,$D9,$63,$A3,$51,$2D,$43,$C9,$E5,$EA,$B9,$2C,$28
	.byte	$B8,$A9,$D0,$B4,$42,$70,$82,$3B,$E4,$F3,$7A,$F8,$C1,$EF,$F3
	.byte	$05,$26,$5B,$7C,$2A,$2F,$10,$02,$25,$37,$03,$31,$10,$04,$01
	.byte	$05,$06,$06,$37,$8B,$23,$01,$03,$6D,$31,$21,$00,$3B
e_gif_end


; *********************************************************************
