# check-lib-ids.ps1 - tripwire for the library-id / descriptor-table coupling.
#
# A library's ID *is* its index in the positional descriptor tables in
# src/main.asm (libNameLo/Hi, libEntryCount, libEager, libStartPage, libPages,
# libRefCount). The LIB_* constants in src/kernel/task-header.inc are what a
# task writes in its libMask and what libstubs.inc passes to libLoad.
#
# If the two disagree the assembler is silent and the runtime is silent: a task
# whose libMask names bit 4 gets whatever occupies slot 4, and a stub that loads
# id 1 loads whatever is at index 1. Nothing fails; the wrong code runs.
#
# No single-file .assert can see both files, so this script compares them. It is
# called from tools/build-kernel.ps1 and tools/build-tasks.ps1.
#
# Run from anywhere; it locates the project root itself.

$ErrorActionPreference = "Stop"

Set-Location (Split-Path -Parent $PSScriptRoot)

$headerPath = "src/kernel/task-header.inc"
$mainPath   = "src/main.asm"

foreach ($p in @($headerPath, $mainPath)) {
    if (-not (Test-Path $p)) { throw "missing $p - run from the project root" }
}

$header = Get-Content $headerPath -Raw
$main   = Get-Content $mainPath   -Raw

# --- the ids, from the single source of truth ------------------------------
# Only plain decimal assignments; LIB_COUNT is a count, not an id.
$ids = @{}
foreach ($m in [regex]::Matches($header, '(?m)^\s*\.const\s+(LIB_[A-Z0-9_]+)\s*=\s*(\d+)\s*(//.*)?$')) {
    $name = $m.Groups[1].Value
    if ($name -eq 'LIB_COUNT') { continue }
    $ids[$name] = [int]$m.Groups[2].Value
}
if ($ids.Count -eq 0) { throw "no LIB_* ids found in $headerPath" }

$countMatch = [regex]::Match($header, '(?m)^\s*\.const\s+LIB_COUNT\s*=\s*(\d+)')
if (-not $countMatch.Success) { throw "LIB_COUNT is not declared in $headerPath" }
$declaredCount = [int]$countMatch.Groups[1].Value

# --- the table order, from main.asm ---------------------------------------
# The libNameLo block: `libNameLo:` ... `.fill`. Its `<libNameXxx` tokens, in
# order, ARE the ids.
$block = [regex]::Match($main, '(?s)libNameLo:\s*(.*?)\.fill')
if (-not $block.Success) { throw "cannot find the libNameLo table in $mainPath" }

$order = @()
foreach ($m in [regex]::Matches($block.Groups[1].Value, '<libName([A-Za-z0-9]+)')) {
    $order += 'LIB_' + $m.Groups[1].Value.ToUpperInvariant()
}

# --- compare ---------------------------------------------------------------
$problems = @()

if ($order.Count -ne $declaredCount) {
    $problems += "LIB_COUNT is $declaredCount but libNameLo lists $($order.Count) libraries"
}

for ($i = 0; $i -lt $order.Count; $i++) {
    $name = $order[$i]
    if (-not $ids.ContainsKey($name)) {
        $problems += "libNameLo[$i] is '$name' but $headerPath declares no such LIB_* id"
        continue
    }
    if ($ids[$name] -ne $i) {
        $problems += "'$name' is id $($ids[$name]) in $headerPath but sits at index $i in libNameLo"
    }
}

foreach ($name in $ids.Keys) {
    if ($order -notcontains $name) {
        $problems += "'$name' is declared in $headerPath but missing from libNameLo"
    }
}

# --- report ----------------------------------------------------------------
if ($problems.Count -gt 0) {
    Write-Host "lib id lint: FAILED" -ForegroundColor Red
    Write-Host "  A library's id is its index in src/main.asm's descriptor tables."
    Write-Host "  Reorder one without the other and tasks silently load the wrong"
    Write-Host "  library. Reconcile $headerPath with src/main.asm:"
    foreach ($p in $problems) { Write-Host "    - $p" -ForegroundColor Red }
    exit 1
}

Write-Host ("lib id lint: OK ({0} libraries: {1})" -f $order.Count, ($order -join ', '))
exit 0
