# check-zp-indirect.ps1 - an INDIRECT operand must be zero page.
#
# `(label),y` addresses zero page only, and KickAssembler does NOT reject a label
# outside it: it truncates the operand to the label's LOW BYTE. So
#
#     lda (muWcPt),y          ; muWcPt is at $893A
#
# assembles as `lda ($3A),y` -- a read of whatever 16-bit pointer happens to sit at
# zero page $3A. Nothing downstream can tell: it assembles, every .assert passes,
# and the fault presents as a logic bug in the routine ("? no windows", a wrong
# byte, a hang). The same trap turns `(zp),x`, which does not exist, into `zp,x` --
# a different instruction with a different meaning.
#
# The rule enforced here is the checkable one: every name in an indirect operand
# must resolve BELOW $100 -- from a zero-page .const or .label in the sources, from
# an assembled symbol file, or through a .const chain that ends at one of those. The
# sources alone are enough, because a fresh clone has no symbol files: the ones this
# reads are build output. A name that cannot be resolved is a FAILURE rather than a
# warning, because the alternative is a check that goes quiet exactly when someone
# adds new code, which is when it matters.
#
# Run from anywhere; it changes to the project root first. Exit 1 on violation.
# A step of tools/build-tasks.ps1 (before the assembles) and of the CI gates.

$ErrorActionPreference = 'Stop'

Set-Location (Split-Path -Parent $PSScriptRoot)

# --- indirect operands -------------------------------------------------------
# group 1 = the name, group 2 = an optional +N displacement
$indirect = [regex]'\(\s*([A-Za-z_]\w*)\s*(?:\+\s*(\d+)\s*)?\)\s*,\s*[yYxX]\b'

# --- .const NAME = VALUE, where VALUE is $hex, decimal, or NAME(+N|-N) -------
$constDef = [regex]'^\s*\.const\s+(\w+)\s*=\s*(\S+)'

# --- .label NAME=$ADDR, as the symbol files write it ------------------------
$labelDef = [regex]'^\.label\s+(\w+)=\$([0-9a-fA-F]+)'

# --- .label NAME = $ADDR, as the sources write it (the '=' need not be tight) --
$labelSrc = [regex]'^\s*\.label\s+(\w+)\s*=\s*\$([0-9a-fA-F]+)'

function Get-Num([string]$Text) {
    if ($Text -match '^\$([0-9a-fA-F]{1,4})$') {
        return [Convert]::ToInt32($Matches[1], 16)
    }
    if ($Text -match '^([0-9]{1,5})$') {
        return [int]$Matches[1]
    }
    return -1
}

# --- collect the definitions -------------------------------------------------
$consts = @{}                       # name -> raw value text
$labels = @{}                       # name -> address

foreach ($file in Get-ChildItem -Path 'src' -Recurse -Include '*.asm', '*.inc' -File) {
    foreach ($line in Get-Content -LiteralPath $file.FullName) {
        $cut = $line.IndexOf('//')
        if ($cut -ge 0) { $line = $line.Substring(0, $cut) }
        $m = $constDef.Match($line)
        if ($m.Success -and -not $consts.ContainsKey($m.Groups[1].Value)) {
            $consts[$m.Groups[1].Value] = $m.Groups[2].Value
        }
        # A zero-page pointer is usually a .label in the sources (kPtr, srcPtr,
        # dataPtr, tmpPtr are), and the symbol files that also carry them are
        # build output that a fresh clone does not have.
        $lm = $labelSrc.Match($line)
        if ($lm.Success -and -not $labels.ContainsKey($lm.Groups[1].Value)) {
            $labels[$lm.Groups[1].Value] = [Convert]::ToInt32($lm.Groups[2].Value, 16)
        }
    }
}

foreach ($sym in @('tools/assemble-tasks.sym', 'build/main.sym', 'tools/gen-fixups.sym')) {
    if (-not (Test-Path $sym)) { continue }
    foreach ($line in Get-Content -LiteralPath $sym) {
        $m = $labelDef.Match($line.Trim())
        if (-not $m.Success) { continue }
        $addr = [Convert]::ToInt32($m.Groups[2].Value, 16)
        if (-not $labels.ContainsKey($m.Groups[1].Value)) {
            $labels[$m.Groups[1].Value] = $addr
        }
    }
}

# --- resolve a name to an address, following .const chains -------------------
# Returns -1 when it cannot be resolved. $Depth guards a cyclic definition.
function Resolve-Name([string]$Name, [int]$Depth) {
    if ($Depth -gt 8) { return -1 }

    $num = Get-Num $Name
    if ($num -ge 0) { return $num }

    if ($labels.ContainsKey($Name)) { return $labels[$Name] }

    if (-not $consts.ContainsKey($Name)) { return -1 }
    $value = $consts[$Name]

    $num = Get-Num $value
    if ($num -ge 0) { return $num }

    # NAME+N or NAME-N, in either order of the operator being reached
    if ($value -match '^([A-Za-z_]\w*)\s*\+\s*(\d+)$') {
        $base = Resolve-Name $Matches[1] ($Depth + 1)
        if ($base -lt 0) { return -1 }
        return $base + [int]$Matches[2]
    }
    if ($value -match '^([A-Za-z_]\w*)\s*-\s*(\d+)$') {
        $base = Resolve-Name $Matches[1] ($Depth + 1)
        if ($base -lt 0) { return -1 }
        return $base - [int]$Matches[2]
    }
    if ($value -match '^([A-Za-z_]\w*)$') {
        return (Resolve-Name $Matches[1] ($Depth + 1))
    }
    return -1
}

# --- the scan ---------------------------------------------------------------
$root = (Get-Location).Path + [IO.Path]::DirectorySeparatorChar
$bad = New-Object System.Collections.ArrayList
$scanned = 0

foreach ($file in Get-ChildItem -Path 'src' -Recurse -Include '*.asm', '*.inc' -File) {
    $scanned++
    $lineNo = 0
    foreach ($line in Get-Content -LiteralPath $file.FullName) {
        $lineNo++
        $cut = $line.IndexOf('//')
        if ($cut -ge 0) { $line = $line.Substring(0, $cut) }

        foreach ($m in $indirect.Matches($line)) {
            $name = $m.Groups[1].Value
            $addr = Resolve-Name $name 0

            if ($addr -lt 0) {
                [void]$bad.Add(('{0}:{1}: ({2}) - cannot resolve {2} to an address, so nothing proves it is zero page' -f $file.FullName.Replace($root, ''), $lineNo, $name))
                continue
            }
            if ($addr -ge 0x100) {
                [void]$bad.Add(('{0}:{1}: ({2}) - {2} is at ${3:X4}, not zero page; the operand is truncated to ${4:X2}' -f $file.FullName.Replace($root, ''), $lineNo, $name, $addr, ($addr -band 0xFF)))
            }
        }
    }
}

if ($bad.Count -gt 0) {
    foreach ($line in $bad) { Write-Host "  $line" }
    Write-Host ""
    Write-Host "zp-indirect lint: $($bad.Count) violation(s) in $scanned source files"
    Write-Host "  An indirect operand is zero page only. Give the pointer a zero-page home,"
    Write-Host "  or copy it into kPtr/srcPtr and read through that."
    exit 1
}

Write-Host "zp-indirect lint: OK ($scanned source files, every indirect operand is zero page)"
exit 0
