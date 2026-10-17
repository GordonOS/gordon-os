# check-reu-current.ps1 - does reu/REU.bin actually contain the binaries you built?
#
# The companion of tools/check-reu-fs.ps1, and NOT a duplicate of it:
#
#   check-reu-fs.ps1       LAYOUT gate   - is the image internally consistent?
#                                         (entries tightly packed, data ends at
#                                         nextFreeOffset). Exits non-zero.
#   check-reu-current.ps1  CURRENCY check - is the image up to date with what
#                                         the task pipeline built, and was that
#                                         pipeline current itself?
#
# A stale image fails at run time as `? no fs or not found` (or worse: an old
# binary runs), which looks like a loader bug. This script answers the other
# half of that question - it byte-verifies every tasks-out/*.bin payload inside
# the image, so a forgotten `tools/build-reu.ps1` is visible immediately.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/check-reu-current.ps1
#
# Exit 0 = every built binary is present and byte-identical in the image, and
#          tasks-out/ was built from the sources now on disk.
# Exit 1 = a payload mismatch, a built binary with no FS entry, or a stale
#          tasks-out/ (run tools/build-tasks.ps1, then tools/build-reu.ps1).
#          (VICE must be closed before rewriting the image.)
#
# Which binaries are checked is decided by tools/tasks.json: a build's name is
# <name>.bin, or its `out` field (fplib emits fp.bin). A file in tasks-out/ that
# the manifest does not produce is a leftover from an entry that has been
# removed - it is not seeded into the image, so there is nothing to verify and
# it is reported as stale rather than as a mismatch.
#
# Layout mirrored from src/kernel/reu-fs.inc:
#   bank 5, offset $9000 -> 512 x 32-byte directory entries
#   entry: byte 0 flags (bit 7 = deleted), 1-16 name (screen codes, space
#          padded), 17-19 data offset (24-bit), 20-21 size

param([string]$Image = 'reu/REU.bin')

$ErrorActionPreference = "Stop"

Set-Location (Split-Path -Parent $PSScriptRoot)   # project root (tools/..)

$DirBase  = 364544                # bank 5 + $9000  : the 512 x 32B directory
$DataBase = 380928                # bank 5 + $D000  : file data (offsets are FS-relative)
$EntryLen = 32
$Slots    = 512
# Latin-1 (28591) is the byte-preserving map the REU FS needs. .NET Core needs
# the code-page provider registered before GetEncoding can return it; .NET
# Framework (Windows PowerShell 5.1) has it built in.
try { [Text.Encoding]::RegisterProvider([Text.Encoding]::CodePagesEncodingProvider::Instance) } catch { }
$enc = [Text.Encoding]::GetEncoding(28591)

if (-not (Test-Path $Image)) { throw "missing $Image - run tools/build-reu.ps1" }

$b = [IO.File]::ReadAllBytes((Join-Path (Get-Location) $Image))
$s = $enc.GetString($b)

# Screen code -> text. $01-$1A are the lowercase letterforms, $41-$5A the
# uppercase ones, $20-$3F space/digits/punctuation.
function Dec-ScreenCode($a, $start, $len) {
    $o = ''
    for ($k = 0; $k -lt $len; $k++) {
        $c = [int]$a[$start + $k]
        if     ($c -ge 1 -and $c -le 26)     { $o += [char](96 + $c) }   # a-z
        elseif ($c -ge 0x41 -and $c -le 0x5A){ $o += [char]$c }          # A-Z
        elseif ($c -ge 32)                   { $o += [char]$c }          # printable
    }
    return $o
}

$order = @()          # live directory entries, in slot order
for ($slot = 0; $slot -lt $Slots; $slot++) {
    $e = $DirBase + $slot * $EntryLen
    if ($b[$e] -band 0x80) { continue }                              # deleted
    $name = (Dec-ScreenCode $b ($e + 1) 16).Trim()
    if ($name.Length -eq 0) { continue }
    $order += [pscustomobject]@{
        Slot = $slot; Entry = $e; Name = $name; Flags = $b[$e]
        Size = $b[$e + 20] + $b[$e + 21] * 256
        Off  = $b[$e + 17] + $b[$e + 18] * 256 + $b[$e + 19] * 65536
    }
}

"image = $Image"
"size  = $($b.Length)"
"FS live entries = $($order.Count)  (slot 0 is the signature)"
''
'== FS directory =='
$order | ForEach-Object { '{0,3}  {1,-13} flags=${2:X2} size={3,-6} off24=${4:X6}' -f $_.Slot, $_.Name, $_.Flags, $_.Size, $_.Off }

''
'== tasks-out/*.bin vs the image =='
# Match a built binary to its FS entry by the ENTRY'S RECORDED OFFSET, not by
# name: a library's build name is not its FS name (gfxlib.bin seeds gfx.lib,
# fp.bin seeds fp.lib, ...), so name matching reports six phantom "missing"
# binaries. The offset is exact and needs no name mapping at all.
$entries = @($order | Where-Object { $_.Slot -ne 0 })

# The manifest decides what a build produces, so a leftover from a removed
# entry is skipped (and named) instead of being reported as a mismatch.
$manifest = (Get-Content -Raw 'tools/tasks.json') | ConvertFrom-Json
$produced = @{}
foreach ($t in @($manifest.tasks)) {
    $produced[$(if ($t.out) { $t.out } else { "$($t.name).bin" })] = $true
}

$ok = 0; $bad = 0; $noEntry = 0; $stale = 0
Get-ChildItem 'tasks-out/*.bin' -File | Sort-Object Name | ForEach-Object {
    if (-not $produced.ContainsKey($_.Name)) {
        $stale++
        '{0,-15} {1}' -f $_.Name, 'stale - not in tools/tasks.json'
        return
    }
    $g      = [IO.File]::ReadAllBytes($_.FullName)
    $needle = $enc.GetString($g[0..([Math]::Min(23, $g.Length - 1))])
    $at     = -1
    $from   = 0
    $hit    = $null
    # find the payload, then confirm an FS entry actually claims that address
    while ($true) {
        $at = $s.IndexOf($needle, $from, [StringComparison]::Ordinal)
        if ($at -lt 0) { break }
        $hit = $entries | Where-Object { ($DataBase + $_.Off) -eq $at } | Select-Object -First 1
        if ($hit) { break }
        $from = $at + 1
    }
    $same = $false
    if ($hit) {
        $same = ($hit.Size -eq $g.Length)
        if ($same) {
            for ($k = 0; $k -lt $g.Length; $k++) { if ($b[$DataBase + $hit.Off + $k] -ne $g[$k]) { $same = $false; break } }
        }
    }
    $dirTxt = if ($hit) { "$($hit.Name):$($hit.Size)" } else { '-' }
    if (-not $hit) { $noEntry++ }
    elseif ($same) { $ok++ } else { $bad++ }
    '{0,-15} file={1,-6} fs={2,-18} {3,-8} {4}' -f $_.Name, $g.Length, $dirTxt, "abs=$at", $(if ($same) { 'payload ok' } else { 'PAYLOAD MISMATCH' })
}
''
"payload ok = $ok   bad = $bad   no FS entry = $noEntry   stale = $stale"

''
'== FS entries with no tasks-out/<name>.bin (assets, fonts, music, batch) =='
$order | Where-Object { $_.Slot -ne 0 } | ForEach-Object {
    $base = ($_.Name -replace '\.[^.]*$', '')
    if (-not (Test-Path "tasks-out/$base.bin")) { '  {0,-13} size={1}' -f $_.Name, $_.Size }
}

''
'== tasks-out vs the sources =='
# The payload check above answers "does the image hold these bytes?"; this
# answers "were these bytes built from the sources on disk?" -- an image can be
# byte-identical to a tasks-out/ that a source edit has left behind.
. "$PSScriptRoot/tasks-stamp.ps1"
$stampFile = 'tasks-out/build.stamp'
$stampOk = (Test-Path $stampFile) -and ((Get-Content $stampFile -Raw).Trim() -eq (Get-TaskInputHash))
if ($stampOk) {
    'current: no .asm/.inc under src/ (or tools/tasks.json) has changed since the last task build'
} else {
    'STALE: the sources changed since the last task build'
}

if (-not $stampOk) {
    Write-Error "tasks-out/ is STALE: the sources changed since the last task build, so the image cannot be shown to be current - run tools/build-tasks.ps1 then tools/build-reu.ps1"
    exit 1
}
if ($bad -gt 0 -or $noEntry -gt 0) {
    Write-Error "image is NOT current: $bad payload mismatch(es), $noEntry built binary(ies) missing from the FS - run tools/build-reu.ps1 (VICE closed)"
    exit 1
}
"current: all $ok built binaries are byte-identical in the image"
exit 0
