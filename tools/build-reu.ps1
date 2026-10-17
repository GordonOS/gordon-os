# build-reu.ps1 - build reu/REU.bin and reu/REU-C64U.bin with a freshly
# formatted filesystem, plus every dynamic-task binary and every
# src/batch/*.bat script, so boot needs no seeding step.
#
# This is the host-side replacement for the kernel's boot-time seeding.
# It mirrors the on-disk REU-FS layout in src/kernel/reu-fs.inc
# and src/kernel/kernel.inc:
#
#   bank 5, offset $9000  -> 16 KB directory (512 x 32-byte entries)
#   bank 5, offset $D000  -> file data (nextFreeOffset starts at 0)
#
#   entry:  byte 0 = flags (bit 7 = deleted)
#           bytes 1-16 = name (space-padded, <= 11 chars used)
#           bytes 17-19 = data offset (FS-relative, 24-bit)
#           bytes 20-21 = size
#   slot 0: byte 1 = $A5 signature, bytes 17-19 = nextFreeOffset,
#           bytes 20-22 = capacity (24-bit, matching reuFsInitBoot)
#
# Run from anywhere; it changes to the project root first.
# Requires "Build dynamic tasks" to have run (it reads tasks-out/*.bin), and it
# refuses a set the sources have moved on from -- see tools/tasks-stamp.ps1.

param(
    [int]$ReuSizeKb = 16384,
    # The speed the image declares (see src/batch/boot.bat): the whole machine
    # runs this percent of real time, and `speed.com` scales the kernel's blink
    # and key-repeat timings and sid.lib's pitch/tempo compensation by it. Two
    # images ship: reu/REU.bin at 200 for VICE (the project's own -speed 200),
    # and reu/REU-C64U.bin at 100 for a C64 Ultimate, whose turbo speeds the
    # CPU up without moving the SID's clock or the video timing.
    [int]$Speed = 200,
    [string]$Out = "reu/REU.bin",
    # The second image to write: the same bytes declaring the real-time speed,
    # for a machine whose turbo does not move the SID's clock or the video
    # timing (a C64 Ultimate). Empty to build only the -Out image.
    [string]$C64UOut = "reu/REU-C64U.bin"
)

$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $PSScriptRoot)   # project root (tools/..)

# tasks-out/ survives between builds, so a task edited without a rebuild would be
# seeded as-is and the C64 would run the old binary -- and an image that matches
# stale artifacts passes every other check. The stamp is the input set the last
# task build consumed; see tools/tasks-stamp.ps1.
. "$PSScriptRoot/tasks-stamp.ps1"
$stampFile = 'tasks-out/build.stamp'
if (-not (Test-Path $stampFile) -or ((Get-Content $stampFile -Raw).Trim() -ne (Get-TaskInputHash))) {
    Write-Error "tasks-out is stale - the sources changed since the last task build, so this image would hold old binaries. Run the 'Build dynamic tasks' task, or tools/build-tasks.ps1"
    exit 1
}

# A running VICE maps the image it was launched with (-reuimagerw), so
# reu/REU.bin cannot be replaced while one lives: the write fails with a sharing
# error that names no cause and the build half-finishes. Refuse up front, by
# name -- and only when the file about to be written is that one, because the
# C64 Ultimate artifact is an output no emulator ever opens.
if ((Join-Path (Get-Location) $Out) -eq (Join-Path (Get-Location) "reu/REU.bin") -and
    (Get-Process x64sc -ErrorAction SilentlyContinue)) {
    Write-Error "VICE is running and has reu/REU.bin open (-reuimagerw). Close it and build again: the image a session holds is the one a machine boots, so it cannot be replaced underneath the session."
    exit 1
}

$BankSize   = 65536                 # one REU bank ($df06 selects 64 KB)
$FsBank     = 5
$FsOffset   = 0x9000                # directory start, intra-bank
$DataOffset = 0xD000                # data start, intra-bank
$Signature  = 0xA5
$DirEntrySz = 32
$DirEntries = 512                   # 16 KB of directory; slot 0 is the FS header
$DirBase    = $FsBank * $BankSize + $FsOffset    # 17-bit: bank 5, $9000
$DataBase   = $FsBank * $BankSize + $DataOffset  # 17-bit: bank 5, $D000

# Capacity, mirroring reuFsInitBoot: $3000 + (banks-11)*$10000 (24-bit,
# no clamp). reuDetectSize caps the probe at 256 banks (8-bit counter).
# One bank is RESERVED: $FB0000-$FBFFFF (bank 251) is grpaint's undo ring, so
# the data region ends at $FAFFFF. The kernel clamps to #245 in its >255-bank
# arm, which is this formula at 256 banks -- the two must agree.
$totalBanks = [Math]::Min([int]($ReuSizeKb * 1024 / $BankSize), 256)
if ($totalBanks -le 11) {
    $cap = 0x3000
} else {
    $cap = 0x3000 + ($totalBanks - 11) * 0x10000
}

# Tasks and libraries come from tools/tasks.json - the single source of truth
# shared with tools/gen-task-asm.ps1 (which generates assemble-tasks.asm and
# gen-fixups.asm from it). fsName is the filename written to the REU
# filesystem; the binary is tasks-out\<out>, defaulting to <name>.bin.
$manifestPath = Join-Path (Get-Location) "tools/tasks.json"
if (-not (Test-Path $manifestPath)) {
    Write-Error "missing tools/tasks.json - it lists the tasks to seed"
    exit 1
}
$manifest = (Get-Content -Raw $manifestPath) | ConvertFrom-Json
$tasks = @()
foreach ($t in @($manifest.tasks)) {
    $bin = "$($t.name).bin"
    if ($t.out) { $bin = $t.out }
    #  A filename the shell can never match must not reach the image: the FS
    #  holds a 1-11 character name (docs/lib-filesys.md), the kernel compares at
    #  most that many, and a longer one is stored-but-unfindable -- `run <name>`
    #  reports `? not found` with the file plainly visible in `dir`.
    if (-not $t.fsName -or $t.fsName.Length -gt 11) {
        Write-Error "tasks.json: '$($t.name)' has fsName '$($t.fsName)' ($($t.fsName.Length) chars) - the REU FS caps a filename at 11 characters"
        exit 1
    }
    $tasks += @{ name = $t.fsName; file = "tasks-out/$bin" }
}

# Non-task assets seeded straight from the repo. .fnt files are padded to a
# bank boundary by Seed-File so the REU font blitter can assume a glyph never
# straddles a bank.
$assets = @(
    @{ name = "gordon.fnt";  file = "charsets/gordon.fnt" },
    @{ name = "c64uppr.fnt"; file = "charsets/c64uppr.fnt" },
    @{ name = "c64low.fnt";  file = "charsets/c64low.fnt" },
    @{ name = "ball.spr";    file = "assets/sprites/ball.spr" },
    @{ name = "boat.spr";    file = "assets/sprites/boat.spr" },
    @{ name = "gkball.spr";  file = "assets/sprites/gkball.spr" },
    @{ name = "gkpad.spr";   file = "assets/sprites/gkpad.spr" },
    @{ name = "godzi.spr";   file = "assets/sprites/godzi.spr" },
    @{ name = "pointer.spr"; file = "assets/sprites/pointer.spr" },
    # .pic pictures, written by tools/img2pic.py from the matching -src.png.
    @{ name = "hires.pic";   file = "assets/pics/hires.pic" },
    @{ name = "mc.pic";      file = "assets/pics/mc.pic" },
    @{ name = "alf.pic";     file = "assets/pics/alf.pic" },
    @{ name = "alfgs.pic";   file = "assets/pics/alfgs.pic" },
    # grpaint's UI band, written by tools/gen-ui.py from assets/ui/gpui.txt: the
    # palette and the tools as the band's own 640 pixel bytes + 80 matrix bytes, so
    # the task reads the UI instead of holding a picture of it. ONE FILE PER MODE --
    # the MODE tile's face differs between them, and only the MC band carries the
    # four palette-slot chips.
    @{ name = "gpui.bin";    file = "assets/ui/gpui.bin" },
    @{ name = "gpui_mc.bin"; file = "assets/ui/gpui_mc.bin" }
)

# --- regenerate the generated artefacts from the sources on disk ---
# Charsets, sprite sheets, the UI band, the pictures and the BASIC corpus; see
# tools/build-assets.ps1, which draws the line between "no source in the tree" (the
# committed file ships) and "the generator ran and failed" (which stops the build).
# It runs HERE, in front of the seeding, and that is what makes F5's "fresh REU" flow
# write an image from the art as it stands now.
& "$PSScriptRoot/build-assets.ps1"
if ($LASTEXITCODE -ne 0) {
    Write-Error "tools/build-assets.ps1 failed - the generated artefacts are not current"
    exit 1
}

$imageSize = $ReuSizeKb * 1024
$img = New-Object byte[] $imageSize

# --- format: directory is already zero; write slot 0 ---
$img[$DirBase + 1]  = $Signature
$img[$DirBase + 17] = 0                       # nextFreeOffset lo
$img[$DirBase + 18] = 0                       # nextFreeOffset mid
$img[$DirBase + 19] = 0                       # nextFreeOffset hi
$img[$DirBase + 20] = $cap -band 0xFF         # capacity lo
$img[$DirBase + 21] = ($cap -shr 8) -band 0xFF
$img[$DirBase + 22] = ($cap -shr 16) -band 0xFF

# --- seed each binary ---
# Seed-File writes one FS entry (data + directory slot) and advances the
# allocator. Used for both task binaries and the inline boot.bat.
# $noSplit: pad to the next 64 KB bank boundary if the file would straddle
# one -- used for .fnt files so the REU-only font blitter can assume a
# glyph never crosses a bank.
function Seed-File([string]$entryName, [byte[]]$dataBytes, [bool]$noSplit = $false) {
    $size = $dataBytes.Length
    if ($noSplit) {
        $bankStart = ([math]::Floor($script:offset / 65536) + 1) * 65536
        if ($script:offset + $size -gt $bankStart) { $script:offset = $bankStart }
    }
    if ($script:offset + $size -gt $cap) {
        Write-Error "seed files exceed FS capacity ($cap bytes - grows with REU banks)"
        exit 1
    }
    # The directory is 512 entries and slot 0 holds the filesystem's own header,
    # so 511 files is the entire budget. Unbounded, entry 512 would be written at
    # $DirBase + 512*32 = bank 5 $D000 -- the first 32 bytes of the DATA region,
    # overwriting file 0's contents -- and the only symptom would arrive later,
    # as check-reu-current.ps1 reporting a payload mismatch.
    if ($script:slot -ge $DirEntries) {
        Write-Error "seed files exceed the FS directory ($DirEntries entries; slot 0 is the filesystem header, so $($DirEntries - 1) files is the most). Drop a file or a film chunk."
        exit 1
    }
    # data (bank-major: the 17-bit address IS the file offset)
    [Array]::Copy($dataBytes, 0, $img, $DataBase + $script:offset, $size)
    # directory entry
    $e = $DirBase + $script:slot * $DirEntrySz
    $img[$e] = 0                               # flags
    # screen-code encode the name, space-padded to 16 (lowercase a-z ->
    # $01-$1A; uppercase A-Z -> $41-$5A; digits and '.' stay as-is)
    $nameChars = $entryName.ToCharArray()
    for ($i = 0; $i -lt 16; $i++) {
        $img[$e + 1 + $i] = 0x20               # space pad
        if ($i -lt $nameChars.Length) {
            $c = [int]$nameChars[$i]
            if ($c -ge 97 -and $c -le 122) { $c -= 96 }
            $img[$e + 1 + $i] = $c
        }
    }
    $img[$e + 17] = $script:offset -band 0xFF
    $img[$e + 18] = ($script:offset -shr 8) -band 0xFF
    $img[$e + 19] = ($script:offset -shr 16) -band 0xFF
    $img[$e + 20] = $size -band 0xFF
    $img[$e + 21] = ($size -shr 8) -band 0xFF
    $script:offset += $size
    $script:slot++
}

$offset = 0
$slot = 1
# Where each batch's `speed` declaration lands in the image, so the real-time
# image can be written from the same buffer at the end (see there).
$speedSpots = @()
foreach ($t in @($tasks) + @($assets)) {
    $path = Join-Path (Get-Location) $t.file
    if (-not (Test-Path $path)) {
        Write-Error "missing task binary: $($t.file) (run Build dynamic tasks first)"
        exit 1
    }
    Seed-File $t.name ([IO.File]::ReadAllBytes($path)) ($t.name -like '*.fnt')
}

# --- seed every *.bat file from src/batch/ ---
# Each file is seeded under its own filename, so you can add as many batch
# files as you like and `runbatch <name>` them from the shell (names must
# stay lowercase; base name <= 7 chars, since runbatch appends ".bat").
# One command per line. Each line is screen-code encoded (lowercase a-z ->
# $01-$1A, everything else passes through as ASCII) and terminated with
# CR ($0D) + NUL ($00): CR is the display separator (`type` shows a
# newline), NUL is the real runbatch terminator (so lines may contain 'm').
# boot.bat is the bootstrap hook the boot task runs on startup.
$batchDir = Join-Path (Get-Location) "src/batch"
$bootBatPath = Join-Path $batchDir "boot.bat"
if (-not (Test-Path $bootBatPath)) {
    Write-Error "missing src/batch/boot.bat - it is the bootstrap batch the boot task runs"
    exit 1
}
$batFiles = @(Get-ChildItem -Path $batchDir -Filter *.bat | Sort-Object Name)
foreach ($bat in $batFiles) {
    $batBytes = New-Object System.Collections.Generic.List[byte]
    foreach ($line in [IO.File]::ReadAllLines($bat.FullName)) {
        $line = $line.Trim()
        if ($line.Length -eq 0) { continue }
        # The image is where a machine's speed is declared, so a `speed` line in
        # a batch is written with this build's -Speed. That is the only
        # difference between reu/REU.bin (200, for VICE) and reu/REU-C64U.bin
        # (100, for an Ultimate): one argument.
        if ($line -match '^speed\s+\d+$') {
            $decl = "speed $Speed"
            $speedSpots += @{ At = $DataBase + $offset + $batBytes.Count + $decl.Length - ("$Speed").Length
                              Len = ("$Speed").Length }
            $line = $decl
        }
        foreach ($ch in $line.ToCharArray()) {
            $c = [int]$ch
            if ($c -ge 97 -and $c -le 122) { $c -= 96 }   # a-z -> $01-$1A
            $batBytes.Add([byte]($c -band 0xFF))
        }
        $batBytes.Add(0x0D)      # CR - display separator
        $batBytes.Add(0x00)      # NUL - runbatch line terminator
    }
    Seed-File $bat.Name $batBytes.ToArray()
}

# --- seed every *.bnr file from src/banners/ ---
# Binary custom-glyph banners drawn by `banner <name> <row> <col>` (the
# command appends ".bnr", so base names must stay <= 7 chars). Raw bytes --
# no encoding. Drop any .bnr into src/banners/ and it ships in the FS.
$bannerDir = Join-Path (Get-Location) "src/banners"
$bnrFiles = @()
if (Test-Path $bannerDir) {
    $bnrFiles = @(Get-ChildItem -Path $bannerDir -Filter *.bnr | Sort-Object Name)
}
foreach ($bnr in $bnrFiles) {
    Seed-File $bnr.Name ([IO.File]::ReadAllBytes($bnr.FullName))
}

# --- seed every *.bas file from src/corpus/ ---
# Tokenized Gordon Basic programs for the REU-backed-BASIC corpus.
# Generated by tools/gen-corpus.py (re-run after changing the corpus programs);
# loaded in BASIC with e.g. LOAD "basdemo" + RUN.
$corpusDir = Join-Path (Get-Location) "src/corpus"
$basFiles = @()
if (Test-Path $corpusDir) {
    $basFiles = @(Get-ChildItem -Path $corpusDir -Filter *.bas | Sort-Object Name)
}
foreach ($bas in $basFiles) {
    Seed-File $bas.Name ([IO.File]::ReadAllBytes($bas.FullName))
}

# --- seed every *.mus file from music/ ---
# Raw bytes (no encoding): .mus files are PRG-like with a 2-byte load
# address the sidplay task skips. Base names must stay <= 7 chars so the
# task-appended ".mus" stays within the 11-char FS name limit.
$musDir = Join-Path (Get-Location) "music"
$musFiles = @()
if (Test-Path $musDir) {
    $musFiles = @(Get-ChildItem -Path $musDir -Filter *.mus | Sort-Object Name)
}
foreach ($mus in $musFiles) {
    Seed-File $mus.Name ([IO.File]::ReadAllBytes($mus.FullName))
}

# --- seed the animation films from assets/anim/ ---
# One host directory per film, holding its chunk files and its manifest. A chunk
# is PER_CHUNK consecutive .pic records and is NOT a picture, so it is seeded as
# raw bytes; the film's shape (rate, palette, frame count, the first chunk's
# name) lives in the manifest, which the film task opens by name.
# The FS is FLAT and a film's names come from disk, so a base name has to be
# unique across the whole filesystem: two films sharing one would collide, and
# kReuOpen returns the first match. tools/gen-anim.py refuses a name over 11
# characters, which is the cap this loop re-checks.
$animDir = Join-Path (Get-Location) "assets/anim"
$animFiles = @()
if (Test-Path $animDir) {
    $animFiles = @(Get-ChildItem -Path $animDir -Recurse -File |
                   Where-Object { $_.Extension -eq ".anm" -or $_.Extension -eq ".inf" } |
                   Sort-Object FullName)
}
foreach ($a in $animFiles) {
    if ($a.Name.Length -gt 11) {
        Write-Error "assets/anim: '$($a.Name)' is $($a.Name.Length) characters - the REU FS caps a filename at 11"
        exit 1
    }
    Seed-File $a.Name ([IO.File]::ReadAllBytes($a.FullName))
}

# --- bank-crossing test asset for the `fstest` task ---------------------
# A file whose data region spans a $10000 bank boundary, so the chunked DMA
# paths (reuDmaRange REU->C64, fsxWriteRange C64->REU) and the 24-bit offset
# arithmetic are exercised by a five-second check (`fstest`) instead of
# by accident. The offset arithmetic is exercised by that five-second check
#   Contents: byte j = j & $FF, generated - the task can verify any window
#   from the window's own index, no expected data has to be stored.
#   Placement: the REU data base is bank 5 $D000, so a transfer wraps a bank
#   at FS offsets ending $3000 (not $0000). The file is placed at
#   bank + $1FF8, which puts the test's window ($2FF8 - base16 = $1000, the
#   middle of the 8 KB file) across the wrap. That costs a gap of up to
#   64 KB, which `compact` reclaims (at the cost of the coverage - fstest
#   then reports "skipped", which is the honest answer).
#   Seeded last, so no existing file offset moves.
$crossSize = 8192
$crossBytes = New-Object byte[] $crossSize
for ($i = 0; $i -lt $crossSize; $i++) { $crossBytes[$i] = [byte]($i -band 0xFF) }
$crossBank = ([math]::Floor($offset / 65536) + 1) * 65536
$crossAt   = $crossBank + 0x1FF8
if ($crossAt -gt $offset) { $offset = $crossAt }
Seed-File "fscross.bin" $crossBytes
Write-Host ("Seeded fscross.bin at " + $crossAt + " (its test window starts " + (0x1000).ToString() + " bytes in and spans the bank wrap at " + ($crossBank + 0x3000) + ") for the fstest self-test")

# --- build stamp --------------------------------------------------------
# A file whose contents change on EVERY build, so a rebuilt image is never
# byte-identical to the one already in the tree and both images show up as
# changed. `type buildid` prints it, so a shipped image says which build it is.
# Digits and punctuation only: `type` writes a file's bytes as they are, and an
# ASCII letter would print as a different glyph (FS NAMES are screen-coded,
# file contents are not).
$buildStamp = [DateTime]::Now.ToString("yyyy-MM-dd HH:mm:ss.fff")
Seed-File "buildid" ([Text.Encoding]::ASCII.GetBytes($buildStamp))
Write-Host "Build stamp: buildid = $buildStamp"

# slot 0 nextFreeOffset
$img[$DirBase + 17] = $offset -band 0xFF
$img[$DirBase + 18] = ($offset -shr 8) -band 0xFF
$img[$DirBase + 19] = ($offset -shr 16) -band 0xFF

# --- the real-time image ------------------------------------------------
# A machine whose turbo leaves the SID's clock and the video timing alone (a
# C64 Ultimate) runs at its own speed, so its image declares 100% and every
# timing is derived from that. It is THIS image with the speed declaration
# changed and nothing else: the digits are what moves, and both declarations
# carry the same digit count, so every offset in the file is identical and the
# second image is a substitution in the finished buffer. Writing it here is what
# keeps the pair from drifting -- a build that writes only one of them leaves the
# other stale, and a stale image is the one a machine boots.
$C64USpeed = 100
$c64uPath = ""
$c64uDigits = "$C64USpeed"
if ($C64UOut -ne "" -and $C64UOut -ne $Out) {
    foreach ($s in $speedSpots) {
        if ($s.Len -ne $c64uDigits.Length) {
            Write-Error "$Out declares $Speed% ($($s.Len) digits) and $C64UOut declares $C64USpeed% ($($c64uDigits.Length)): the two would not share a layout. Build both with matching digit counts, or the second image alone with -Speed $C64USpeed -Out $C64UOut."
            exit 1
        }
    }
    $c64uPath = $C64UOut
}

# --- write the images ---------------------------------------------------
# The C64 Ultimate image is written FIRST. A running VICE maps the image it was
# launched with (-reuimagerw), and a mapped file refuses a write, so the primary
# file can fail while this artefact -- which no emulator holds -- succeeds. The
# artefact must not be the one a failed write takes with it.
$failedWrites = @()
if ($c64uPath -ne "") {
    if ($speedSpots.Count -eq 0) {
        # Nothing declared a speed, so there is no declaration to change.
        Write-Warning "no speed declaration was seeded - $c64uPath is identical to $Out"
    }
    $c64uImg = [byte[]]::new($img.Length)
    [Array]::Copy($img, $c64uImg, $img.Length)
    foreach ($s in $speedSpots) {
        for ($i = 0; $i -lt $c64uDigits.Length; $i++) {
            $c64uImg[$s.At + $i] = [byte][char]$c64uDigits[$i]
        }
    }
    try {
        [IO.File]::WriteAllBytes((Join-Path (Get-Location) $c64uPath), $c64uImg)
        Write-Host "Built $c64uPath at speed $C64USpeed%: the same $offset bytes, the speed declaration changed."
    } catch {
        $failedWrites += "$c64uPath -- $($_.Exception.Message.Trim())"
    }
}

$out = Join-Path (Get-Location) $Out
try {
    [IO.File]::WriteAllBytes($out, $img)
    Write-Host "Built $Out at speed $Speed%: $($tasks.Count + $assets.Count + $batFiles.Count + $bnrFiles.Count + $basFiles.Count + $musFiles.Count + $animFiles.Count) files, $offset bytes used ($($cap - $offset) free of $cap)."
} catch {
    $failedWrites += "$Out -- $($_.Exception.Message.Trim())"
}

if ($failedWrites.Count -gt 0) {
    # A write can only fail here because another process holds the file, and the
    # usual one is VICE: it maps reu/REU.bin for the whole session (-reuimagerw)
    # and writes it back on exit, so the file cannot be replaced while it lives.
    # The machine boots the session's copy, which makes this a refusal rather
    # than a warning.
    Write-Error "could not write: $($failedWrites -join '; ')"
} else {
    # One line naming both files and their times, so a task's output says what
    # the build produced without reading the lines above it.
    $mainName = Split-Path -Leaf $out
    $mainStamp = (Get-Item $out).LastWriteTime.ToString("HH:mm:ss")
    if ($c64uPath -ne "") {
        Write-Host "REU images written: $mainName $mainStamp, $(Split-Path -Leaf $c64uPath) $((Get-Item (Join-Path (Get-Location) $c64uPath)).LastWriteTime.ToString('HH:mm:ss'))"
    } else {
        Write-Host "REU image written: $mainName $mainStamp"
    }
}
