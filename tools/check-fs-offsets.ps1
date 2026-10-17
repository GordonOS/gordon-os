# check-fs-offsets.ps1 - build tripwire for the 24-bit REU filesystem offset.
#
# reuFsPtr is a 24-bit offset held in three kernel bytes ($42D1-$42D3) and is
# shared by three unrelated writers: reuFindFile (lookup result), the dynamic
# loader (its own load cursor) and fontGlyph (a computed glyph offset).
#
# Anything that copies or stashes reuFsPtr MUST carry all three bytes. The
# bank byte (reuFsPtr+2) is 0 for files below FS $10000 and 1..3 above, and
# the layout is user data - `compact` repacks the FS from the start, so
# deleting a few KB below fp.lib could pull it under $10000 and make "the bank
# byte is always 0" appear to work.
#
# Neither bug could be caught by a KickAssembler .assert: they are data-flow
# mistakes, not size or address mistakes. This script is the tripwire.
#
# Rules (per statement; comments stripped, statements split on ';'):
#   R1  A copy of the lo byte into a stash (lda reuFsPtr + pha, or
#       lda reuFsPtr + sta <other>) must be followed by a reuFsPtr+2
#       reference within the next 8 statements.
#   R2  Forcing the bank byte to a constant (lda #0 / sta reuFsPtr+2)
#       hard-codes "every file lives in the first bank".
#   R3  24-bit arithmetic: a store to reuFsPtr+1 right after an adc/sbc must
#       be followed by the carry step into reuFsPtr+2 -- without it the offset
#       16-bit-wraps mid-load.
#
# Escape hatch: put `fs-lint: allow` in a comment on the statement or on the
# line above it.

param([string]$SrcRoot = "src")

$ErrorActionPreference = "Stop"

Set-Location (Split-Path -Parent $PSScriptRoot)

$problems = New-Object System.Collections.ArrayList
$scanned = 0

$files = Get-ChildItem -Path $SrcRoot -Recurse -File |
    Where-Object { $_.Extension -eq ".inc" -or $_.Extension -eq ".asm" }

foreach ($f in $files) {
    $scanned++
    $lines = @(Get-Content -LiteralPath $f.FullName)

    # Flatten to statements so same-line "lda X; sta Y" and two-line form match alike.
    $stmts = New-Object System.Collections.ArrayList
    for ($i = 0; $i -lt $lines.Count; $i++) {
        $txt = $lines[$i]
        $c = $txt.IndexOf("//")
        if ($c -ge 0) { $txt = $txt.Substring(0, $c) }
        $allow = ($lines[$i] -match 'fs-lint:\s*allow') -or
                 (($i -gt 0) -and ($lines[$i - 1] -match 'fs-lint:\s*allow'))
        foreach ($s in ($txt -split ";")) {
            $t = $s.Trim()
            if ($t.Length -gt 0) {
                [void]$stmts.Add(@{ Text = $t; Line = $i + 1; Allow = $allow })
            }
        }
    }

    for ($k = 0; $k -lt $stmts.Count; $k++) {
        $s = $stmts[$k]

        # R1 - stash/copy of the lo byte must bring +2 along
        if ($s.Text -match '^lda\s+reuFsPtr\s*$' -and ($k + 1) -lt $stmts.Count) {
            $n = $stmts[$k + 1].Text
            $isStash = ($n -match '^pha$') -or (($n -match '^sta\s+') -and ($n -notmatch 'reuFsPtr'))
            if ($isStash) {
                $ok = $false
                $last = [Math]::Min($k + 8, $stmts.Count - 1)
                for ($j = $k + 1; $j -le $last; $j++) {
                    if ($stmts[$j].Text -match 'reuFsPtr\+2') { $ok = $true; break }
                }
                if (-not $ok -and -not $s.Allow) {
                    [void]$problems.Add(("{0}:{1}: copies 'reuFsPtr' without 'reuFsPtr+2' -> {2}" -f `
                        $f.Name, $s.Line, $s.Text))
                }
            }
        }

        # R2 - bank byte forced to a constant
        if ($s.Text -match '^lda\s+#\$?0+\s*$' -and ($k + 1) -lt $stmts.Count) {
            if ($stmts[$k + 1].Text -match '^sta\s+reuFsPtr\+2\s*$' -and -not $s.Allow) {
                [void]$problems.Add(("{0}:{1}: 'sta reuFsPtr+2' forced to bank 0 -> {2}" -f `
                    $f.Name, $s.Line, $s.Text))
            }
        }

        # R3 - carry into +2 after 24-bit arithmetic
        if ($s.Text -match '^sta\s+reuFsPtr\+1\s*$' -and $k -ge 1) {
            if ($stmts[$k - 1].Text -match '^(adc|sbc)\b') {
                $ok = $false
                $last = [Math]::Min($k + 4, $stmts.Count - 1)
                for ($j = $k + 1; $j -le $last; $j++) {
                    if ($stmts[$j].Text -match 'reuFsPtr\+2') { $ok = $true; break }
                }
                if (-not $ok -and -not $s.Allow) {
                    [void]$problems.Add(("{0}:{1}: 24-bit add on 'reuFsPtr+1' without the carry into 'reuFsPtr+2'" -f `
                        $f.Name, $s.Line))
                }
            }
        }
    }
}

if ($problems.Count -gt 0) {
    Write-Host ""
    Write-Host "fs-offset lint FAILED ($($problems.Count) finding(s)):" -ForegroundColor Red
    foreach ($p in $problems) { Write-Host "  $p" }
    Write-Host ""
    Write-Host "reuFsPtr is 24-bit: a copy/stash must carry reuFsPtr, +1 AND +2."
    Write-Host "Suppress a deliberate case with a 'fs-lint: allow' comment on the"
    Write-Host "statement or the line above."
    exit 1
}

Write-Host "fs-offset lint: OK ($scanned source files)"
exit 0
