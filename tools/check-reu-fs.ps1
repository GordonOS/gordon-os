# check-reu-fs.ps1 - decode the REU filesystem directory from reu/REU.bin.
#
# Verifies what tools/build-reu.ps1 actually seeded: every directory entry's
# name, size and offset, and that the file data ends exactly at the allocator's
# nextFreeOffset. Useful after touching the manifest (tools/tasks.json) or the
# FS layout, since a wrong fsName or a stale image is otherwise only visible at
# runtime as "? no fs or not found".
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/check-reu-fs.ps1
#
# This is the LAYOUT half of the question (is the image internally consistent?).
# The other half - "does the image contain the binaries tasks-out just built?" -
# is tools/check-reu-current.ps1. Neither is a duplicate: a fresh, correctly
# packed image can still be stale, and a current image can still be malformed.
#
# Layout mirrored from src/kernel/reu-fs.inc:
#   bank 5, offset $9000 -> 512 x 32-byte directory entries
#   bank 5, offset $D000 -> file data
#   entry: byte 0 flags (bit 7 = deleted), 1-16 name (screen codes, space
#          padded), 17-19 data offset (24-bit), 20-21 size
#
# -Strict additionally requires the data area to be TIGHTLY packed, failing on
# any gap. The default tolerates gaps, because tools/build-reu.ps1 deliberately
# reserves one: fscross.bin is placed at the next $10000 bank boundary + $1FF8
# so the fstest window spans the wrap, leaving ~39 KB unused before it.
# Overlap and a wrong nextFreeOffset are hard errors in both modes.

param([switch]$Strict = $false)

$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $PSScriptRoot)   # project root (tools/..)

$image = "reu/REU.bin"
if (-not (Test-Path $image)) { throw "missing $image - run tools/build-reu.ps1" }

$img = [IO.File]::ReadAllBytes((Join-Path (Get-Location) $image))
$DirBase = 5 * 65536 + 0x9000
$entrySize = 32

# PowerShell notes: cast to [int] before shifting (a [byte] -shl 8 wraps to
# 8 bits), and a byte of 0 or $20 terminates the space-padded name.
function Decode-Name([int]$e) {
    $s = ""
    for ($i = 0; $i -lt 16; $i++) {
        $c = [int]$img[$e + 1 + $i]
        if ($c -eq 0 -or $c -eq 0x20) { continue }
        if ($c -ge 1 -and $c -le 26) { $s += [char](96 + $c) }   # a-z -> $01-$1A
        elseif ($c -ge 0x41 -and $c -le 0x5A) { $s += [char]$c }
        else { $s += [char]$c }
    }
    return $s
}

$sig = $img[$DirBase + 1]
$next = [int]$img[$DirBase + 17] + ([int]$img[$DirBase + 18] -shl 8) + ([int]$img[$DirBase + 19] -shl 16)
$cap = [int]$img[$DirBase + 20] + ([int]$img[$DirBase + 21] -shl 8) + ([int]$img[$DirBase + 22] -shl 16)
"slot0: signature=$('{0:x2}' -f $sig) nextFreeOffset=$next capacity=$cap"

$count = 0
$lastEnd = 0
$bad = 0
$gaps = 0
$gapBytes = 0
for ($slot = 1; $slot -lt 512; $slot++) {
    $e = $DirBase + $slot * $entrySize
    if ($img[$e] -band 0x80) { continue }              # deleted
    $name = Decode-Name $e
    if ($name.Length -eq 0) { continue }
    $off = [int]$img[$e + 17] + ([int]$img[$e + 18] -shl 8) + ([int]$img[$e + 19] -shl 16)
    $size = [int]$img[$e + 20] + ([int]$img[$e + 21] -shl 8)
    $count++
    if ($off -lt $lastEnd) {
        $bad++
        "  ! OVERLAP: $name starts at $off, previous entry ended at $lastEnd"
    } elseif ($off -gt $lastEnd) {
        $gaps++
        $gapBytes += ($off - $lastEnd)
        "  . gap of $($off - $lastEnd) bytes before $name ($lastEnd -> $off)"
    }
    $lastEnd = $off + $size
    "{0,-14} size={1,-7} end={2}" -f $name, $size, $lastEnd
}

"total entries: $count"
if ($gaps -gt 0) {
    # A gap is not proof of a bug: fscross.bin is placed at the next $10000 bank
    # boundary + $1FF8 on purpose (tools/build-reu.ps1, "bank-crossing test
    # asset"), because its whole job is to make an 8 KB transfer span the wrap.
    # Overlap and a wrong nextFreeOffset stay hard errors; tightness is opt-in.
    $note = "$gaps gap(s) totalling $gapBytes bytes - legitimate only if something is deliberately reserved"
    if ($Strict) { Write-Error $note; exit 1 }
    Write-Host "note: $note"
}
if ($lastEnd -ne $next) {
    Write-Error "data ends at $lastEnd but nextFreeOffset says $next"
    exit 1
}
if ($bad -gt 0) {
    Write-Error "$bad overlapping entr(y/ies)"
    exit 1
}
"layout OK: $count entries, data ends at $lastEnd (== nextFreeOffset)"
