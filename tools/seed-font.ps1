# seed-font.ps1 - build the GordonOS .fnt files from the source chargen
# ROMs (charsets/pet80col-901640-01.bin + charsets/chargen-901225-01.bin):
#   gordon.fnt  -- OS font (hybrid: PET letterforms + C64 graphics)
#   c64uppr.fnt -- stock C64 uppercase/graphics set (chargen offset $0000)
#   c64low.fnt  -- stock C64 lowercase/uppercase set (chargen offset $0800)
#
# gordon.fnt is the lowercase/uppercase set: lowercase at screencode
# $01-$1A, uppercase at $41-$5A. PET letterforms for text, C64 glyphs
# for graphics so PETSCII graphics chars (e.g. maze CHR$(205/206)) render
# exactly as they did with the C64 chargen.
#
#   lowercase $01-$1A <- PET half 2 $61-$7A
#   uppercase $41-$5A <- PET half 2 $41-$5A
#   $00 <- PET half 2 $40 ('@')
#   $1B-$1F <- PET half 2 $5B-$5F ([ ] etc.)
#   $20-$3F <- PET half 2 (digits/punct)
#   everything else from C64 set 2 (graphics)

$ErrorActionPreference = "Stop"

Set-Location (Split-Path -Parent $PSScriptRoot)

# [IO.File] is not provider-aware, so it is given absolute paths built from the
# script's own location rather than from the current directory.
$root = Split-Path -Parent $PSScriptRoot

$pet = [System.IO.File]::ReadAllBytes((Join-Path $root 'charsets/pet80col-901640-01.bin'))
if ($pet.Length -lt 4096) { Write-Error "PET 80-col ROM too small" }
$c64 = [System.IO.File]::ReadAllBytes((Join-Path $root 'charsets/chargen-901225-01.bin'))
if ($c64.Length -lt 4096) { Write-Error "C64 chargen too small" }

function Copy-Glyph([byte[]]$srcRom, [int]$srcOff, [byte[]]$dst, [int]$dstOff) {
    for ($i = 0; $i -lt 8; $i++) {
        $dst[$dstOff + $i] = $srcRom[$srcOff + $i]
    }
}

$out = New-Object byte[] 2048

# lowercase/uppercase set: PET letters, C64 graphics
# PET ROM half 2 starts at offset 2048.
for ($s = 0; $s -lt 256; $s++) {
    $dst = $s * 8
    if ($s -eq 0x00)                         { Copy-Glyph $pet (2048 + 0x40 * 8) $out $dst }
    elseif ($s -ge 0x01 -and $s -le 0x1A)    { Copy-Glyph $pet (2048 + ($s + 0x60) * 8) $out $dst }
    elseif ($s -ge 0x1B -and $s -le 0x1F)    { Copy-Glyph $pet (2048 + ($s + 0x40) * 8) $out $dst }
    elseif ($s -ge 0x20 -and $s -le 0x3F)    { Copy-Glyph $pet (2048 + $s * 8) $out $dst }
    elseif ($s -ge 0x41 -and $s -le 0x5A)    { Copy-Glyph $pet (2048 + $s * 8) $out $dst }
    else                                     { Copy-Glyph $c64 (2048 + $s * 8) $out $dst }
}

[System.IO.File]::WriteAllBytes((Join-Path $root 'charsets/gordon.fnt'), $out)
Write-Host "Wrote charsets/gordon.fnt (lowercase/uppercase, 2KB)"

# --- stock C64 chargen sets (2KB each, screencode order) ---
#   c64uppr.fnt = uppercase/graphics set (chargen offset $0000)
#   c64low.fnt  = lowercase/uppercase set (chargen offset $0800)
$c64uppr = New-Object byte[] 2048
[Array]::Copy($c64, 0, $c64uppr, 0, 2048)
[System.IO.File]::WriteAllBytes((Join-Path $root 'charsets/c64uppr.fnt'), $c64uppr)

$c64low = New-Object byte[] 2048
[Array]::Copy($c64, 2048, $c64low, 0, 2048)
[System.IO.File]::WriteAllBytes((Join-Path $root 'charsets/c64low.fnt'), $c64low)
Write-Host "Wrote charsets/c64uppr.fnt + c64low.fnt (stock C64 chargen, 2KB each)"
