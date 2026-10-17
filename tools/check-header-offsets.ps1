# check-header-offsets.ps1 - header fields must be read by NAME, never by bare offset.
#
# A task/library header is staged in one of two 256-byte buffers: `scratchBuf`
# (the kernel's load path, via dynParseHeader) and `reuDirBuf` (dlPeekLibPages,
# which DMAs a library's header page in to size it). Every field in that page has
# a name in src/kernel/task-header.inc (HDR_*), and those constants are the only
# place the layout is written down.
#
# A bare `+N` on one of those buffers is a hidden coupling: when the layout
# changes, the constant moves and the literal does not. It is the worst kind of
# miss to review, because the edit that adds the bug usually rewrites the
# surrounding comment too -- so the diff reads as a comment-only change and the
# reviewer's eye slides off it.
#
# No .assert can catch this (the value is a byte offset into a DMA'd page, not a
# label), so it is a tripwire script instead. Run by tools/build-kernel.ps1
# before Ninja.
#
# The two buffer uses that are NOT header reads are allow-listed below.
#
# Negative test (a check that cannot be shown to fail is not a check):
#   powershell -File tools/check-header-offsets.ps1 -Path tools/probes
# must exit 1 and name both probe lines.

param(
    # Source tree to scan. Overridable so the tripwire can be tested against the
    # fixture in tools/probes -- a check that cannot be shown to fail is not a check.
    [Parameter(Mandatory = $false)][string]$Path = 'src/kernel'
)

$ErrorActionPreference = 'Stop'
Set-Location (Split-Path -Parent $PSScriptRoot)   # project root (tools/..)

$hdrFile = 'src/kernel/task-header.inc'

# --- the field names, so a violation can name the constant to use -------------
$fields = @{}
foreach ($m in [regex]::Matches([IO.File]::ReadAllText($hdrFile), '\.const\s+(HDR_[A-Z0-9_]+)\s*=\s*(\d+)')) {
    $fields[[int]$m.Groups[2].Value] = $m.Groups[1].Value
}
if ($fields.Count -eq 0) {
    Write-Host 'FAIL: could not parse any HDR_* constant from task-header.inc'
    exit 1
}

# --- allowed bare literals on each buffer ------------------------------------
#   scratchBuf: none. The kernel stages exactly one thing there -- a header page.
#   reuDirBuf : it is ALSO the REU FS directory buffer, and reu-fs.inc indexes its
#               32-byte entries by literal: +1 is a scratch byte, +17..+21 are the
#               entry's 24-bit data offset and 16-bit remaining length. Those are
#               directory fields, not header fields. Anything else in the header
#               page's range must go through HDR_*.
$allow = @{
    'reuDirBuf' = @(1, 17, 18, 19, 20, 21)
}

$buffers = @('scratchBuf', 'reuDirBuf')
$bad = $false
$scanned = 0

foreach ($file in (Get-ChildItem $Path -Recurse -File | Where-Object { $_.Extension -in '.inc', '.asm' })) {
    $scanned++
    $lineNo = 0
    foreach ($line in [IO.File]::ReadAllLines($file.FullName)) {
        $lineNo++
        # strip a trailing line comment so `lda scratchBuf+7  // code_size hi`
        # is judged on its code, not on prose that may legitimately mention offsets
        $code = ($line -split '//')[0]
        foreach ($buf in $buffers) {
            foreach ($m in [regex]::Matches($code, [regex]::Escape($buf) + '\+\s*(\d+)')) {
                $off = [int]$m.Groups[1].Value
                $permitted = $allow.ContainsKey($buf) -and ($allow[$buf] -contains $off)
                if ($permitted) { continue }
                # Name the constant to use. An offset that is not a field START is
                # still owned by one -- +6 is HDR_CODE_SIZE's hi half -- so say which
                # field the offset falls inside rather than just refusing.
                if ($fields.ContainsKey($off)) {
                    $hint = "use $($fields[$off])"
                } else {
                    $below = $fields.Keys | Where-Object { $_ -le $off } | Sort-Object -Descending | Select-Object -First 1
                    if ($null -ne $below) {
                        $hint = "offset +$off lies inside $($fields[$below]) (at +$below) -- use the constant"
                    } else {
                        $hint = 'no HDR_* constant is at or below this offset -- is it a header field at all?'
                    }
                }
                $rel = $file.FullName.Substring((Get-Location).Path.Length) -replace '^[\\/]+', ''
                Write-Host ("FAIL {0}:{1}: {2}+{3} is a bare header offset -- {4}" -f $rel, $lineNo, $buf, $off, $hint)
                $bad = $true
            }
        }
    }
}

if ($bad) {
    Write-Host ''
    Write-Host 'Header fields must be named. See src/kernel/task-header.inc.'
    exit 1
}

# --- the constants themselves: strictly increasing, inside the header page ----
$prev = -1
foreach ($off in ($fields.Keys | Sort-Object)) {
    if ($off -le $prev) { Write-Host "FAIL: HDR_* offsets are not strictly increasing at $off"; exit 1 }
    $prev = $off
}
$maxOff = ($fields.Keys | Measure-Object -Maximum).Maximum
if ($maxOff -gt 20) { Write-Host "FAIL: HDR_* offset $maxOff is past the 256-byte header page's field run"; exit 1 }

Write-Host ("header offsets ok: {0} files scanned, {1} HDR_* constants" -f $scanned, $fields.Count)
exit 0
