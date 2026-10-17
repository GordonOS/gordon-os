# verify-inventory-docs.ps1 -- do the call inventories agree with jumptable.inc?
#
# The companion to verify-jumptable-docs.ps1, which does the same job for
# docs/kernel.md's section headings. The two inventories --
#   docs/kernel-call-inventory.md   resident entries + every call site
#   docs/library-call-inventory.md  library entries + every call site
# -- quote each entry's address, and jumptable.inc is the authority for it
# (main.asm pins the table to it with .asserts). Nothing keeps the three in
# step, so a jump-table renumbering leaves the docs describing the OLD table:
# a reader who assembles against a stale address calls whatever now lives
# there.
#
# Where an address sits, per line shape:
#   a table row    | [`kName`](#kname) | `$E090` | 154 | 37 | ... |   -> cell 2
#   a detail pair  "### kName" then "`$E090` -- **154 calls** ..."   -> next line
# An alias row quotes a NAME instead of an address
# ("| `kFreeBitmap` | (alias of `kFreeScreen`) | ... |"), so that pair is
# checked as: the alias target must resolve to the same address as the name.
#
# Exit 0 = every quoted address agrees. Exit 1 = at least one is stale.

$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $PSScriptRoot)

# --- jumptable.inc: the direct entries, and the names that alias them ---
$addr  = @{}   # kName -> $XXXX
$alias = @{}   # kName -> the kName it stands for
foreach ($line in Get-Content src/kernel/jumptable.inc -Encoding UTF8) {
    if ($line -match '^\s*\.const\s+(k[A-Za-z0-9_]+)\s*=\s*\$([0-9A-Fa-f]{4})\b') {
        $addr[$Matches[1]] = $Matches[2].ToUpper()
    } elseif ($line -match '^\s*\.const\s+(k[A-Za-z0-9_]+)\s*=\s*(k[A-Za-z0-9_]+)\b') {
        $alias[$Matches[1]] = $Matches[2]
    }
}

function Resolve-Entry([string]$name) {
    $hops = 0
    while ($alias.ContainsKey($name) -and $hops -lt 4) { $name = $alias[$name]; $hops++ }
    if ($addr.ContainsKey($name)) { return $addr[$name] }
    if ($alias.ContainsKey($name)) { return '(alias)' }
    return $null
}

$files = @('docs/kernel-call-inventory.md', 'docs/library-call-inventory.md')

$checked = 0
$stale   = @()
$current = ''          # the entry a detail heading introduced

foreach ($file in $files) {
    $lines = [IO.File]::ReadAllLines((Resolve-Path $file).Path, [Text.Encoding]::UTF8)
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]

        # a detail section heading names the entry its address line belongs to
        if ($line -match '^###\s+`?(k[A-Za-z0-9_]+)`?\s*$') { $current = $Matches[1]; continue }

        # table rows: the name is in cell 1, its address (or alias) in cell 2
        if ($line.StartsWith('|')) {
            $cells = $line.Split('|')
            if ($cells.Count -ge 4) {
                $nm = [regex]::Match($cells[1], '`(k[A-Za-z0-9_]+)`')
                if ($nm.Success) {
                    $name = $nm.Groups[1].Value
                    $am = [regex]::Match($cells[2], '\$([0-9A-Fa-f]{4})')
                    if ($am.Success) {
                        $checked++
                        $want = Resolve-Entry $name
                        $got  = $am.Groups[1].Value.ToUpper()
                        if ($null -eq $want) {
                            $stale += [pscustomobject]@{ File = (Split-Path $file -Leaf); Line = $i + 1; Entry = $name; Doc = "`$" + $got; Actual = 'not in jumptable.inc' }
                        } elseif ($want -ne $got) {
                            $stale += [pscustomobject]@{ File = (Split-Path $file -Leaf); Line = $i + 1; Entry = $name; Doc = "`$" + $got; Actual = "`$" + $want }
                        }
                    } else {
                        # an alias row: "-- (alias of `kFreeScreen`) --"
                        $al = [regex]::Match($cells[2], '`(k[A-Za-z0-9_]+)`')
                        if ($al.Success) {
                            $checked++
                            $target = $al.Groups[1].Value
                            $want = Resolve-Entry $name
                            $they = Resolve-Entry $target
                            if ($null -eq $want -or $null -eq $they -or $want -ne $they) {
                                $stale += [pscustomobject]@{ File = (Split-Path $file -Leaf); Line = $i + 1; Entry = $name; Doc = "alias of " + $target; Actual = "`$" + $want }
                            }
                        }
                    }
                }
            }
            continue
        }

        # a detail section: the first line after the heading carries the address
        if ($current -ne '' -and $line -match '\$([0-9A-Fa-f]{4})') {
            $checked++
            $want = Resolve-Entry $current
            $got  = $Matches[1].ToUpper()
            if ($null -eq $want) {
                $stale += [pscustomobject]@{ File = (Split-Path $file -Leaf); Line = $i + 1; Entry = $current; Doc = "`$" + $got; Actual = 'not in jumptable.inc' }
            } elseif ($want -ne $got) {
                $stale += [pscustomobject]@{ File = (Split-Path $file -Leaf); Line = $i + 1; Entry = $current; Doc = "`$" + $got; Actual = "`$" + $want }
            }
            $current = ''
        }
    }
}

Write-Output ("jumptable.inc entries : {0} direct, {1} alias" -f $addr.Count, $alias.Count)
Write-Output ("inventory addresses   : {0} checked in 2 files" -f $checked)
Write-Output ""

if ($stale.Count -gt 0) {
    Write-Output ("STALE ADDRESSES ({0}):" -f $stale.Count)
    $stale | Format-Table -AutoSize | Out-String | Write-Output
    Write-Output "Repair: update the quoted address in the inventory row (or the alias"
    Write-Output "name), and regenerate that document's counts if a call moved with it."
    exit 1
}

Write-Output ""

# The addresses above are one half of the gate; the call counts are the other.
# They drift the moment a task gains or loses a call, and a stale count is a
# wrong number in a document a reader is meant to trust.
& "$PSScriptRoot/gen-call-inventory.ps1" -Check
if ($LASTEXITCODE -ne 0) {
    Write-Output "Repair: run tools/gen-call-inventory.ps1 -- it rewrites the numbers only," ". never an address and never any prose."
    exit 1
}

Write-Output "OK   inventory addresses : all $checked agree with jumptable.inc"
exit 0
