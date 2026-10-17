# verify-jumptable-docs.ps1 -- does docs/kernel.md agree with jumptable.inc?
#
# Two questions, one pass over the tree:
#   1. ADDRESSES. jumptable.inc is the TASK-FACING ABI and is pinned by .asserts
#      in main.asm, so it is authoritative. docs/kernel.md heads each section
#      with the same address ("### `kPrintGlyph` -- `$E0A8`"), and those
#      headings are how a human looks an entry up. Nothing keeps the two in
#      step, so they can drift -- and a drifted address is worse than a missing
#      one: a task that assembles against the doc calls whatever now lives there.
#   2. COVERAGE. Every entry must be headable somewhere a reader would look. The
#      API docs are split (resident entries in docs/kernel.md, library entries
#      and their resident companions in docs/lib-*.md, every entry in the two
#      inventories), so an entry kernel.md does not head is fine -- an entry no
#      document heads is a gap, and that fails the gate.
#
# Two rules are baked into the address check:
#
#   1. A heading may document SEVERAL entries on one line --
#      "### `kMalloc` -- `$E0AE` / `kFree` -- `$E0B1`" -- so each address must be
#      paired with the name that PRECEDES it. Pairing the first name with the
#      last address reports a false positive.
#   2. A stale address can COLLIDE with another entry's correct one, so a repair
#      must be scoped to that section's own heading -- never mapped globally.
#
# Exit 0 = all headings agree and every entry is documented. Exit 1 otherwise.

$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $PSScriptRoot)

$jt = @{}
foreach ($line in Get-Content src/kernel/jumptable.inc -Encoding UTF8) {
    #  \b, not \s*$: most .const lines carry a trailing comment ("= $E0B4     //
    #  filesys.lib helper"), and an end-of-line anchor silently skips every one
    #  of them -- which leaves their headings unchecked.
    if ($line -match '^\s*\.const\s+(k[A-Za-z0-9_]+)\s*=\s*\$([0-9A-Fa-f]{4})\b') {
        $jt[$Matches[1]] = $Matches[2].ToUpper()
    }
}

# Walk each heading line as a token stream: `kName` tokens name the entry that
# a following $XXXX belongs to.
$doc     = @{}
$stale   = @()
$checked = 0
$lines   = [IO.File]::ReadAllLines((Resolve-Path docs/kernel.md).Path, [Text.Encoding]::UTF8)

for ($i = 0; $i -lt $lines.Count; $i++) {
    if ($lines[$i] -notmatch '^###\s') { continue }
    $pending = @()
    foreach ($tok in [regex]::Matches($lines[$i], '`(k[A-Za-z0-9_]+)`|\$([0-9A-Fa-f]{4})')) {
        if ($tok.Groups[1].Success) {
            $pending += $tok.Groups[1].Value
        } elseif ($pending.Count -gt 0) {
            $addr = $tok.Groups[2].Value.ToUpper()
            foreach ($name in $pending) {
                if (-not $doc.ContainsKey($name)) { $doc[$name] = $addr }
                $checked++
                if ($jt.ContainsKey($name) -and $jt[$name] -ne $addr) {
                    $stale += [pscustomobject]@{
                        Line = $i + 1; Entry = $name
                        Doc = "`$" + $addr; Actual = "`$" + $jt[$name]
                    }
                }
            }
            $pending = @()
        }
    }
}

$missing = ($jt.Keys | Where-Object { -not $doc.ContainsKey($_) -and $_ -ne 'kSyscall' } | Sort-Object)

# An entry kernel.md does not head may still be documented where it BELONGS:
# a per-library doc heads its own entries ("## `kSpriteCollide` -- the frame-
# latched masks"), and the two call inventories head every resident helper and
# every library call. That is documentation a reader can find, so the list
# below is information, not a fault -- only an entry with NO heading anywhere is
# a real gap, and that fails the gate.
$anywhere = @{}
foreach ($f in (Get-ChildItem docs -Filter *.md)) {
    foreach ($line in [IO.File]::ReadAllLines($f.FullName, [Text.Encoding]::UTF8)) {
        if ($line -notmatch '^#{2,6}\s') { continue }
        foreach ($m in [regex]::Matches($line, '\b(k[A-Za-z0-9_]+)\b')) { $anywhere[$m.Groups[1].Value] = $true }
    }
}
$undocumented = @($jt.Keys | Where-Object { -not $anywhere.ContainsKey($_) } | Sort-Object)

Write-Output "jumptable.inc entries : $($jt.Count)"
Write-Output "documented addresses  : $checked"
Write-Output ""

if ($stale.Count -gt 0) {
    Write-Output "STALE ADDRESSES in docs/kernel.md ($($stale.Count)):"
    $stale | Format-Table -AutoSize | Out-String | Write-Output
    Write-Output "Repair: edit each section's OWN heading line only."
    Write-Output "Do NOT map addresses globally - a stale address may equal"
    Write-Output "another entry's correct one (doc kBlitChar vs real kSetScrollRegion)."
} else {
    Write-Output "OK   docs/kernel.md addresses : all $checked headings agree with jumptable.inc"
}

if ($missing.Count -gt 0) {
    $missingList = $missing -join ', '
    Write-Output ""
    Write-Output "Documented outside kernel.md ($($missing.Count)): $missingList"
}

if ($undocumented.Count -gt 0) {
    $undocList = $undocumented -join ', '
    Write-Output ""
    Write-Output "ERROR: jumptable.inc entries with no section in ANY docs/*.md ($($undocumented.Count)): $undocList"
    Write-Output "Repair: add a heading for each in docs/kernel.md (resident) or the"
    Write-Output "owning docs/lib-*.md -- an entry a reader cannot look up is a gap."
}

exit ([int](($stale.Count -gt 0) -or ($undocumented.Count -gt 0)))
