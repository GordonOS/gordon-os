# gen-call-inventory.ps1 -- regenerate (or check) the call-count NUMBERS in
#   docs/kernel-call-inventory.md
#   docs/library-call-inventory.md
#
# Both documents are authored prose plus derived NUMBERS: each entry's total
# call count, its per-file breakdown, per-library totals and the summary
# totals. Only the numbers are generated -- never hand-edit them, which is how
# they went stale in the first place. The prose (the "What it does" column,
# the methodology, the entry lists themselves) passes through untouched.
#
# The counting rule, ONE rule for both documents:
#   * every *.asm and *.inc under src/;
#   * a call is `jsr <name>` or `jmp <name>`, where <name> is the entry's own
#     name or one of its aliases (`.const <alias> = k<Name>`, the four the
#     methodology lists: syscall, csrBoth, kAllocBitmap, kFreeBitmap);
#   * a line whose first non-space characters are // ; or * is a comment and
#     is not counted;
#   * inside a cold stub -- nearest preceding label starts with `libStub` --
#     the FIRST `jmp k<Name>` is the stub completing its own first (cold)
#     call, not a caller, and is not counted. 19 in libstubs.inc + 4 in
#     main.asm = 23; the script asserts that number, so a new stub cannot
#     silently change the rule.
#   * a `kName: jmp <target>` jump-table definition line names the TARGET, so
#     it never matches a call to kName and needs no special case.
#
# What it will NOT do: move an entry between the summary table and the
# "no current call sites" table. That move needs its description moved with
# it, and a description is prose. The script fails with the entry named, so a
# human makes that one edit.
#
# Usage
#   tools/gen-call-inventory.ps1            rewrite both documents in place
#   tools/gen-call-inventory.ps1 -Check     report drift, write nothing, exit 1
#   tools/gen-call-inventory.ps1 -Doc Kernel
#
# verify-inventory-docs.ps1 runs -Check, so a stale count fails the doc gate.

[CmdletBinding()]
param(
    [ValidateSet('All', 'Kernel', 'Library')]
    [string]$Doc = 'All',
    [switch]$Check
)

$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $PSScriptRoot)

$KERNEL_DOC  = 'docs/kernel-call-inventory.md'
$LIBRARY_DOC = 'docs/library-call-inventory.md'

# .const alias -> the k* name it stands for (the methodology's four).
$script:Alias = @{
    'syscall'       = 'kSyscall'
    'csrBoth'       = 'kCsrBoth'
    'kAllocBitmap'  = 'kAllocScreen'
    'kFreeBitmap'   = 'kFreeScreen'
}
$script:ColdStubJumps = 0

# The documents are stored with the repository's own line ending (LF), and a
# Windows checkout of the repo can be CRLF, so nothing here may assume one: the
# split and the join both follow whatever the file already uses. Assuming CRLF
# parses an LF file as a single line -- every entry then reads as absent, the
# call counts come out zero, and the cold-stub assertion fails on a tree that
# is in fact fine.
function Get-Eol([string]$text) {
    if ($text -match "`r`n") { return "`r`n" } else { return "`n" }
}

# --- source files, once -----------------------------------------------------
$script:SourceFiles = @(Get-ChildItem -Recurse -File -Path src -Include *.asm, *.inc |
    Sort-Object FullName | ForEach-Object { $_.FullName })

function Get-CallCounts {
    # name -> @{ Total = n; Files = @( @{ File = 'src/...'; Count = n } ) }
    param([string[]]$Names)

    $counts = @{}
    foreach ($n in $Names) {
        $counts[$n] = @{ Total = 0; Files = @{} }
    }

    foreach ($file in $script:SourceFiles) {
        $rel = ($file.Substring((Get-Location).Path.Length) -replace '^[\\/]+', '') -replace '\\', '/'
        $inStub = $false
        $stubJumpSeen = $false
        foreach ($line in [System.IO.File]::ReadAllLines($file, [System.Text.Encoding]::UTF8)) {
            $trim = $line.TrimStart()
            if ($trim -eq '' -or $trim.StartsWith('//') -or $trim.StartsWith(';') -or $trim.StartsWith('*')) { continue }
            $code = $line
            # A label may share its line with the instruction (`kSidPlay: jmp libStubSidPlay`),
            # so strip it and keep looking rather than skipping the line.
            if ($code -match '^\s*([A-Za-z_][A-Za-z0-9_]*):(.*)$') {
                $label = $Matches[1]
                $code = $Matches[2]
                $inStub = $label.StartsWith('libStub')
                $stubJumpSeen = $false
            }
            # A line may hold more than one call (`jsr kGetTaskName; jsr kPrintString`).
            foreach ($m in [regex]::Matches($code, '\b(jsr|jmp)\s+([A-Za-z_][A-Za-z0-9_]*)')) {
                $op = $m.Groups[1].Value
                $target = $m.Groups[2].Value
                if ($script:Alias.ContainsKey($target)) { $target = $script:Alias[$target] }
                if (-not $counts.ContainsKey($target)) { continue }
                if ($inStub -and $op -eq 'jmp' -and -not $stubJumpSeen) {
                    $stubJumpSeen = $true
                    $script:ColdStubJumps++
                    continue
                }
                $counts[$target].Total++
                if (-not $counts[$target].Files.ContainsKey($rel)) { $counts[$target].Files[$rel] = 0 }
                $counts[$target].Files[$rel]++
            }
        }
    }

    foreach ($n in $Names) {
        $counts[$n].Files = @($counts[$n].Files.GetEnumerator() |
            ForEach-Object { @{ File = $_.Key; Calls = $_.Value } } |
            Sort-Object @{ Expression = { -$_.Calls } }, @{ Expression = { $_.File } })
    }
    return $counts
}

function Format-FileTable {
    # The header and separator, then one row per file: the whole table, so the
    # caller can replace an existing one rather than add to it.
    param($Entry)
    $out = New-Object System.Collections.Generic.List[string]
    $out.Add('| File | Calls |')
    $out.Add('|---|---:|')
    foreach ($f in $Entry.Files) { $out.Add('| `' + $f.File + '` | ' + $f.Calls + ' |') }
    return $out
}

function Assert-SameShape {
    # A rewrite only moves numbers, so the line count must stay in the same
    # ballpark. Anything else means the emit/skip logic dropped or duplicated
    # a block, and writing that would mangle the document.
    param([string]$Path, [string]$Old, [string]$New)
    $o = ($Old -split '\r?\n').Count
    $n = ($New -split '\r?\n').Count
    if ($n -gt ($o * 2) -or $n -lt ($o / 2)) {
        Write-Host ("ERROR: {0}: the rewrite is {1} lines, the original {2} -- refusing to write" -f $Path, $n, $o)
        exit 4
    }
}

# ============================================================================
#  docs/kernel-call-inventory.md
# ============================================================================
function Update-KernelDoc {
    param([switch]$Report)

    $text = [System.IO.File]::ReadAllText((Join-Path (Get-Location) $KERNEL_DOC), [System.Text.Encoding]::UTF8)
    $eol = Get-Eol $text
    $lines = $text -split '\r?\n'
    $em = [string][char]0x2014

    # --- parse: summary rows, detail sections, the no-call-sites table ---
    $summary = New-Object System.Collections.Generic.List[object]        # name, addr, desc
    $sections = @{}                                                     # name -> the block's lines
    $noSites = New-Object System.Collections.Generic.List[object]        # name, addr, desc
    $summaryOrder = New-Object System.Collections.Generic.List[string]
    $sectionOrder = New-Object System.Collections.Generic.List[string]
    $noSitesOrder = New-Object System.Collections.Generic.List[string]

    $mode = ''
    foreach ($line in $lines) {
        if ($line -match '^\| Kernel call \| Address \| Total calls') { $mode = 'summary'; continue }
        if ($line -match '^### (k[A-Za-z0-9_]+)\s*$') {
            $mode = 'section'
            $name = $Matches[1]
            $sectionOrder.Add($name)
            $sections[$name] = New-Object System.Collections.Generic.List[string]
            $sections[$name].Add($line)
            continue
        }
        if ($line -match '^## ') { $mode = '' }
        if ($line -match '^\| Kernel call \| Address \| What it does \|') { $mode = 'nosites'; continue }
        if ($mode -eq 'summary') {
            if ($line -match '^\|\s*\[`(k[A-Za-z0-9_]+)`\]') {
                $cells = $line.Trim('|') -split '\|'
                if ($cells.Count -ge 5) {
                    $summary.Add(@{ Name = $Matches[1]; Addr = $cells[1].Trim(); Desc = $cells[4].Trim() })
                    $summaryOrder.Add($Matches[1])
                }
                continue
            }
            if ($line -notmatch '^\|') { $mode = '' }
        } elseif ($mode -eq 'nosites') {
            if ($line -match '^\|\s*`(k[A-Za-z0-9_]+)`\s*\|\s*([^|]+)\|') {
                $cells = $line.Trim('|') -split '\|'
                $noSites.Add(@{ Name = $Matches[1]; Addr = $Matches[2].Trim(); Desc = $cells[2].Trim() })
                $noSitesOrder.Add($Matches[1])
                continue
            }
            if ($line -notmatch '^\|') { $mode = '' }
        } elseif ($mode -eq 'section') {
            $sections[$sectionOrder[$sectionOrder.Count - 1]].Add($line)
        }
    }

    # --- count ---
    $names = @($summaryOrder) + @($noSitesOrder |
        Where-Object { -not $script:Alias.ContainsKey($_) })
    $counts = Get-CallCounts -Names $names

    $bad = 0
    foreach ($row in $summary) {
        if ($counts[$row.Name].Total -eq 0) {
            Write-Host ("  ! {0} has no call sites now -- move its row (with its description) into the no-call-sites table" -f $row.Name)
            $bad++
        }
    }
    foreach ($row in $noSites) {
        if ($script:Alias.ContainsKey($row.Name)) { continue }
        if ($counts[$row.Name].Total -gt 0) {
            Write-Host ("  ! {0} has call sites now -- move its row (with its description) into the summary table" -f $row.Name)
            $bad++
        }
    }
    if ($bad -gt 0) {
        Write-Host ("{0}: {1} entry(ies) need a human move; nothing written" -f $KERNEL_DOC, $bad)
        exit 2
    }

    # --- rebuild the numbers ---
    $total = 0
    foreach ($row in $summary) { $total += $counts[$row.Name].Total }
    $withSites = @($summary | Where-Object { $counts[$_.Name].Total -gt 0 }).Count
    $without = @($noSites | Where-Object { -not $script:Alias.ContainsKey($_.Name) }).Count

    $summarySorted = @($summary | Sort-Object @{ Expression = { -$counts[$_.Name].Total } }, @{ Expression = { $_.Name } })

    $changed = New-Object System.Collections.Generic.List[string]
    $out = New-Object System.Collections.Generic.List[string]
    $i = 0
    while ($i -lt $lines.Count) {
        $line = $lines[$i]

        # the two summary bullets
        if ($line -match '^- \*\*\d+ total call sites\*\* found across `src/`\.$') {
            $out.Add('- **' + $total + ' total call sites** found across `src/`.')
            $i++
            continue
        }
        if ($line -match '^- \d+ of them have at least one call site in the current source tree;$') {
            $out.Add('- ' + $withSites + ' of them have at least one call site in the current source tree;')
            $i++
            continue
        }
        if ($line -match '^\s+\d+ have none \(listed at the end\)\.$') {
            $out.Add('  ' + $without + ' have none (listed at the end).')
            $i++
            continue
        }
        if ($line -match '^> \*\*first place \(\d+ calls\)\*\*\.') {
            $out.Add('> **first place (' + $counts['kSyscall'].Total + ' calls)**. See the aliasing note in Methodology.')
            $i++
            continue
        }

        if ($line -match '^- \d+ resident kernel entry points in scope') {
            $out.Add([regex]::Replace($line, '^- \d+ resident kernel entry points in scope', '- ' + ($summary.Count + $without) + ' resident kernel entry points in scope'))
            $i++
            continue
        }
        if ($line -match '^That leaves \*\*\d+ resident kernel entry points\*\*') {
            $out.Add([regex]::Replace($line, '\*\*\d+ resident kernel entry points\*\*', '**' + ($summary.Count + $without) + ' resident kernel entry points**'))
            $i++
            continue
        }

        # the summary table
        if ($line -match '^\| Kernel call \| Address \| Total calls') {
            $out.Add($line)
            $out.Add('|---|---|---:|---:|---|')
            $i += 2
            while ($i -lt $lines.Count -and $lines[$i] -match '^\|') { $i++ }
            foreach ($row in $summarySorted) {
                $t = $counts[$row.Name].Total
                $f = $counts[$row.Name].Files.Count
                $out.Add('| [`' + $row.Name + '`](#' + $row.Name.ToLower() + ') | ' + $row.Addr + ' | ' + $t + ' | ' + $f + ' | ' + $row.Desc + ' |')
            }
            continue
        }

        # the no-call-sites table
        if ($line -match '^\| Kernel call \| Address \| What it does \|') {
            $out.Add($line)
            $out.Add('|---|---|---|')
            $i += 2
            while ($i -lt $lines.Count -and $lines[$i] -match '^\|') { $i++ }
            foreach ($row in $noSites) {
                $out.Add('| `' + $row.Name + '` | ' + $row.Addr + ' | ' + $row.Desc + ' |')
            }
            continue
        }

        # a detail section heading: emit every section in the summary's order
        if ($line -match '^### (k[A-Za-z0-9_]+)\s*$') {
            foreach ($row in $summarySorted) {
                $block = $sections[$row.Name]
                $wroteTable = $false
                foreach ($bl in $block) {
                    if ($bl -match ('^`\$[0-9A-F]{4}` ' + $em + ' \*\*\d+ calls?\*\* across \d+ file\(s\)$')) {
                        $n = $counts[$row.Name].Total
                        $out.Add('`' + $row.Addr + '` ' + $em + ' **' + $n + ' calls** across ' + $counts[$row.Name].Files.Count + ' file(s)')
                        continue
                    }
                    if ($bl -match '^\| File \| Calls \|$') { continue }
                    if ($bl -match '^\|---\|') {
                        if (-not $wroteTable) {
                            foreach ($fl in (Format-FileTable $counts[$row.Name])) { $out.Add($fl) }
                            $wroteTable = $true
                        }
                        continue
                    }
                    if ($bl -match '^\| `src[\\/]' -or $bl -match '^\| `tasks-out[\\/]') { continue }
                    $out.Add($bl)
                }
                $null = $wroteTable
            }
            # skip every original section -- all of them were just emitted,
            # and a library's (or the document's) section block runs to the
            # next `## ` heading, not to the next `### `.
            $i++
            while ($i -lt $lines.Count -and $lines[$i] -notmatch '^## ') { $i++ }
            continue
        }

        $out.Add($line)
        $i++
    }

    # the count line of a section is the only number not written above; do it now
    $new = $out -join $eol
    foreach ($row in $summary) {
        $n = $counts[$row.Name].Total
        $pattern = '(?m)^`' + [regex]::Escape($row.Addr) + '` ' + $em + ' \*\*\d+ calls?\*\* across \d+ file\(s\)$'
        $replacement = '`' + $row.Addr + '` ' + $em + ' **' + $n + ' calls** across ' + $counts[$row.Name].Files.Count + ' file(s)'
        $new = [regex]::Replace($new, $pattern, $replacement.Replace('$', '$$'))
    }

    if ($new -eq $text) {
        Write-Host ("{0}: numbers current ({1} entries, {2} call sites, {3} with none)" -f $KERNEL_DOC, $summary.Count, $total, $without)
        return 0
    }
    if ($Report) {
        foreach ($row in $summary) {
            $old = [regex]::Match($text, '^\| \[`' + [regex]::Escape($row.Name) + '`\].*$', 'Multiline')
            if ($old.Success) {
                $cells = $old.Value.Trim('|') -split '\|'
                if ([int]$cells[2].Trim() -ne $counts[$row.Name].Total) {
                    Write-Host ('  {0}: {1} -> {2}' -f $row.Name, $cells[2].Trim(), $counts[$row.Name].Total)
                }
            }
        }
        Write-Host ("{0}: {1} entries, {2} call sites -- STALE (run without -Check to rewrite)" -f $KERNEL_DOC, $summary.Count, $total)
        return 1
    }
    Assert-SameShape $KERNEL_DOC $text $new
    [System.IO.File]::WriteAllText((Join-Path (Get-Location) $KERNEL_DOC), $new, (New-Object System.Text.UTF8Encoding($false)))
    Write-Host ("{0}: rewritten -- {1} entries, {2} call sites, {3} with none" -f $KERNEL_DOC, $summary.Count, $total, $without)
    return 0
}

# ============================================================================
#  docs/library-call-inventory.md
# ============================================================================
function Update-LibraryDoc {
    param([switch]$Report)

    $path = Join-Path (Get-Location) $LIBRARY_DOC
    $text = [System.IO.File]::ReadAllText($path, [System.Text.Encoding]::UTF8)
    $eol = Get-Eol $text
    $lines = $text -split '\r?\n'
    $em = [string][char]0x2014

    # --- parse: per-library tables and detail sections ---
    $libs = New-Object System.Collections.Generic.List[object]     # name, note, entries
    $lib = $null
    $covered = New-Object System.Collections.Generic.List[string]   # detail blocks, in order
    $blocks = @{}                                                   # name -> lines

    for ($i = 0; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        if ($line -match '^## ([a-z]+)\.lib\s*$') {
            $lib = @{ Name = $Matches[1]; Note = ''; Entries = New-Object System.Collections.Generic.List[object]; TableAt = -1; DetailAt = -1 }
            $libs.Add($lib)
            continue
        }
        if ($null -ne $lib -and $line -match '^\*(lib \d+, \w+) ' + $em + ' source: `([^`]+)`\*$') {
            $lib.Note = $Matches[1]
            continue
        }
        if ($null -ne $lib -and $line -match '^\| Call \| Address \| Total calls') {
            $lib.TableAt = $i
            $j = $i + 2
            while ($j -lt $lines.Count -and $lines[$j] -match '^\|') {
                $cells = $lines[$j].Trim('|') -split '\|'
                if ($cells.Count -ge 5 -and $cells[0] -match '`(k[A-Za-z0-9_]+)`') {
                    $lib.Entries.Add(@{ Name = $Matches[1]; Addr = $cells[1].Trim(); Desc = $cells[4].Trim() })
                }
                $j++
            }
            continue
        }
        if ($null -ne $lib -and $line -match '^### (k[A-Za-z0-9_]+)\s*$') {
            $name = $Matches[1]
            $block = New-Object System.Collections.Generic.List[string]
            $block.Add($lines[$i])
            $k = $i + 1
            while ($k -lt $lines.Count -and $lines[$k] -notmatch '^### k[A-Za-z0-9_]+\s*$' -and $lines[$k] -notmatch '^## ') {
                $block.Add($lines[$k]); $k++
            }
            $blocks[$name] = $block
            $covered.Add($name)
        }
    }

    $names = @($covered)
    $counts = Get-CallCounts -Names $names

    $total = 0
    foreach ($l in $libs) { foreach ($e in $l.Entries) { $total += $counts[$e.Name].Total } }
    $withSites = @($names | Where-Object { $counts[$_].Total -gt 0 }).Count
    $without = $names.Count - $withSites

    # entry counts in a library's header row must match the table it heads
    foreach ($l in $libs) {
        if ($l.Entries.Count -ne @($l.Entries | Where-Object { $blocks.ContainsKey($_.Name) }).Count) {
            Write-Host ("  ! {0}.lib: an entry has no detail section" -f $l.Name)
        }
    }

    if ($Report) {
        $stale = 0
        foreach ($l in $libs) {
            foreach ($e in $l.Entries) {
                $m = [regex]::Match($text, '^\| \[`' + [regex]::Escape($e.Name) + '`\].*$', 'Multiline')
                if ($m.Success) {
                    $cells = $m.Value.Trim('|') -split '\|'
                    if ([int]$cells[2].Trim() -ne $counts[$e.Name].Total) {
                        Write-Host ('  {0}: {1} -> {2}' -f $e.Name, $cells[2].Trim(), $counts[$e.Name].Total)
                        $stale++
                    }
                }
            }
        }
        if ($text -notmatch ('- \*\*' + $total + ' total call sites\*\*')) { $stale++ }
        Write-Host ("{0}: {1} entries, {2} call sites, {3} stale" -f $LIBRARY_DOC, $names.Count, $total, $stale)
        return ([int]($stale -gt 0))
    }

    # --- rewrite ---
    $out = New-Object System.Collections.Generic.List[string]
    $curLib = $null
    $i = 0
    while ($i -lt $lines.Count) {
        $line = $lines[$i]

        if ($line -match '^## ([a-z]+)\.lib\s*$') {
            $curLib = $libs | Where-Object { $_.Name -eq $Matches[1] } | Select-Object -First 1
            $out.Add($line)
            $i++
            continue
        }

        if ($line -match '^- 9 lazily/eagerly-loaded libraries') {
            $out.Add('- 9 lazily/eagerly-loaded libraries (`src/libs/*.asm`), ' + $names.Count + ' call entry points')
            $i++
            continue
        }
        if ($line -match '^- \*\*\d+ total call sites\*\* found across `src/`\.$') {
            $out.Add('- **' + $total + ' total call sites** found across `src/`.')
            $i++
            continue
        }
        if ($line -match '^- \d+ of them have at least one call site in the current source tree; \d+ have$') {
            $out.Add('- ' + $withSites + ' of them have at least one call site in the current source tree; ' + $without + ' have')
            $i++
            continue
        }
        if ($line -match '^  none\.$') {
            $out.Add('  none.')
            $i++
            continue
        }

        if ($line -match '^This gives \*\*\d+ library call entry points\*\*') {
            $out.Add('This gives **' + $names.Count + ' library call entry points** across the ' + $libs.Count + ' libraries.')
            $i++
            continue
        }

        # the "By library" table
        if ($line -match '^\| Library \| Note \| Calls \| Total call sites \|$') {
            $out.Add($line)
            $out.Add('|---|---|---|---:|')
            $i += 2
            while ($i -lt $lines.Count -and $lines[$i] -match '^\|') { $i++ }
            foreach ($l in $libs) {
                $t = 0
                foreach ($e in $l.Entries) { $t += $counts[$e.Name].Total }
                $note = if ($l.Note -eq '') { '' } else { $l.Note }
                $out.Add('| [`' + $l.Name + '.lib`](#' + $l.Name + 'lib) | ' + $note + ' | ' + $l.Entries.Count + ' | ' + $t + ' |')
            }
            continue
        }

        # a per-library table
        if ($null -ne $curLib -and $line -match '^\| Call \| Address \| Total calls') {
            $here = $libs | Where-Object { $_.TableAt -eq $i } | Select-Object -First 1
            $out.Add($line)
            $out.Add('|---|---|---:|---:|---|')
            $i += 2
            while ($i -lt $lines.Count -and $lines[$i] -match '^\|') { $i++ }
            foreach ($e in @($here.Entries | Sort-Object @{ Expression = { -$counts[$_.Name].Total } }, @{ Expression = { $_.Name } })) {
                $out.Add('| [`' + $e.Name + '`](#' + $e.Name.ToLower() + ') | ' + $e.Addr + ' | ' + $counts[$e.Name].Total + ' | ' + $counts[$e.Name].Files.Count + ' | ' + $e.Desc + ' |')
            }
            continue
        }

        # a detail section: emit this library's sections, sorted by count
        if ($null -ne $curLib -and $line -match '^### (k[A-Za-z0-9_]+)\s*$') {
            foreach ($e in @($curLib.Entries | Sort-Object @{ Expression = { -$counts[$_.Name].Total } }, @{ Expression = { $_.Name } })) {
                $block = $blocks[$e.Name]
                $emitted = $false
                $wroteTable = $false
                foreach ($bl in $block) {
                    if ($bl -match '^\*\*\d+ calls?\*\* across \d+ file\(s\):$') {
                        $n = $counts[$e.Name].Total
                        $out.Add('**' + $n + ' calls** across ' + $counts[$e.Name].Files.Count + ' file(s):')
                        $emitted = $true
                        continue
                    }
                    if ($bl -match '^No call sites in the current source tree\.$') {
                        if ($counts[$e.Name].Total -eq 0) { $out.Add($bl) }
                        $emitted = $true
                        continue
                    }
                    if ($bl -match '^\| File \| Calls \|$') { continue }
                    if ($bl -match '^\|---\|') {
                        if (-not $wroteTable -and $counts[$e.Name].Total -gt 0) {
                            foreach ($fl in (Format-FileTable $counts[$e.Name])) { $out.Add($fl) }
                            $wroteTable = $true
                        }
                        continue
                    }
                    if ($bl -match '^\| `src[\\/]') { continue }
                    $out.Add($bl)
                }
                $null = $wroteTable
                if (-not $emitted) { $out.Add('No call sites in the current source tree.') }
            }
            $i++
            while ($i -lt $lines.Count -and $lines[$i] -notmatch '^## ') { $i++ }
            continue
        }

        $out.Add($line)
        $i++
    }

    $new = $out -join $eol
    Assert-SameShape $LIBRARY_DOC $text $new
    [System.IO.File]::WriteAllText($path, $new, (New-Object System.Text.UTF8Encoding($false)))
    Write-Host ("{0}: rewritten -- {1} entries, {2} call sites" -f $LIBRARY_DOC, $names.Count, $total)
    return 0
}

# --- run --------------------------------------------------------------------
$rc = 0
if ($Doc -eq 'All' -or $Doc -eq 'Kernel')  { $rc = [Math]::Max($rc, (Update-KernelDoc -Report:$Check)) }
if ($Doc -eq 'All' -or $Doc -eq 'Library') { $rc = [Math]::Max($rc, (Update-LibraryDoc -Report:$Check)) }

if ($script:ColdStubJumps -ne 23) {
    Write-Host ("ERROR: counted {0} cold-stub self-completion jumps, expected 23 -- the rule in this script no longer matches the tree" -f $script:ColdStubJumps)
    exit 3
}
if ($Check -and $rc -ne 0) {
    Write-Host "call inventories are STALE -- run tools/gen-call-inventory.ps1"
} elseif ($Check) {
    Write-Host "call inventories: numbers current"
}
exit $rc
