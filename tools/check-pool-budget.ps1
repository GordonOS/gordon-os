# check-pool-budget.ps1 - the live-footprint tripwire for the task pool.
#
# While a dynamic task is ALIVE it holds its own code plus every library it needs
# at that moment: the EAGER ones named by its libMask (loaded at spawn) and every
# LAZY one its own code has loaded -- a lazy library is pulled in by the first call
# to one of its entries and is then HELD until the task dies or calls kLibHold
# (one-way, and only a few tasks call it), so from that call on its pages are off
# the pool for as long as the task is resident. For `basic` that is filesys.lib,
# which it opens its REU working file with in the first lines of its cold start.
#
# Nothing used to add those up, and the kernel cannot: the loader's pre-need sum
# counts only the eager libraries (dlEagerLibPages), so a task can load
# successfully and then fail when it reaches the lazy one. That is what happened
# when gfx.lib grew by a page -- `basic` needed 65 code + 27 gfx + 8 fp + 10
# filesys = 110 pages of a 109-page pool, loaded, printed its banner, and died
# with "could not load filesys.lib".
#
# Everything it reads is derived, so none of it can drift:
#   POOL_PAGES       src/kernel/pool.inc (POOL_SIZE / POOL_PAGE_SIZE)
#   code size        each tasks-out/*.bin header, offset +5/+6
#   libMask          each tasks-out/*.bin header, offset +10/+11
#   library order    src/main.asm libNameLo -- that order IS the library id
#   eager flags      src/main.asm libEager
#   library sizes    tools/tasks.json (kind = lib) -> tasks-out/<out>
#
# The lazy libraries a task loads have to be listed in $LiveLazy below: which of
# its declared libraries it actually calls is a property of the task's CODE, never
# of its header, so it cannot be derived. Add a line there when a new task does it.
# Every row names a library the task's own libMask declares -- the loop below
# checks that, because a row the header does not declare is a table that has
# drifted, and a missing row is an under-count this gate exists to prevent.
#
# `kMalloc` blocks come from the same pool, so a task that holds one while it is
# alive has those pages off the pool too, and they are modelled here: a block
# sized from a tune FILE is recomputed from the file on disk (the task's own
# ceil(size / 256) arithmetic), and a reservation or scratch block is given in
# pages. A task whose header sets KF_MALLOC_RAM is neither -- its block is sized
# by the spawn argument -- so the script fails until a row is added for it.
#
# A footprint equal to the pool is allowed but reported as a zero margin,
# because the next byte anywhere in that chain breaks the task.
#
# Run from anywhere; it changes to the project root first.

$ErrorActionPreference = "Stop"

# Project root = one level up from this script's directory (tools/)
Set-Location (Split-Path -Parent $PSScriptRoot)

# Task -> the LAZY libraries its own code loads while it is alive (library FILE
# names). A `clock`/`dir`-style one-shot command loads them the moment it runs; a
# resident task loads them in its startup sequence.
$LiveLazy = @{
    # --- one-shot commands: the library loads as the command runs ---
    'clock'   = @('time.lib')                        # prints the TOD
    'time'    = @('time.lib')                        # reads and sets the TOD
    'ps'      = @('string.lib')                      # hex fields
    'dir'     = @('filesys.lib', 'string.lib')       # reads the directory, prints names
    'type'    = @('filesys.lib')                     # streams a file out
    'format'  = @('filesys.lib')                     # formats the FS
    'rename'  = @('filesys.lib')                     # FS work
    'copy'    = @('filesys.lib')                     # FS work
    'del'     = @('filesys.lib', 'string.lib')       # FS work, compares names
    'banner'  = @('filesys.lib')                     # reads the .bnr
    'fstest'  = @('filesys.lib')                     # its whole job
    'sidplay' = @('filesys.lib', 'sid.lib')          # loads the tune, then plays it
    'ipctest' = @('ipc.lib')                         # its whole job
    # --- resident tasks: the load happens in the startup sequence ---
    'shell'   = @('filesys.lib', 'string.lib')       # the boot target runs "runbatch boot"
    'basic'   = @('filesys.lib')                     # opens its REU working file ("basicwrk")
    'edit'    = @('filesys.lib')                     # opens the document it was given
    'grview'  = @('filesys.lib')                     # reads the .pic it was given
    'granim'  = @('filesys.lib')                     # opens the film's manifest, streams chunks
    'grpaint' = @('filesys.lib')                     # resolves the band's image ("gpui.bin")
    'godzi'   = @('filesys.lib', 'sid.lib')          # loads its two tunes, plays through sid.lib
    'gortris' = @('filesys.lib', 'sid.lib')          # loads the tune + the hi-score table, plays
    'grknoid' = @('filesys.lib', 'sid.lib')          # loads the hi-score table + three tunes, plays
}

# Task -> the tune FILEs it loads into a kMalloc block. A block is ceil(size /
# 256) pages, so the number is computed from the file on disk and follows a
# re-authored tune. '*' means "whichever file it is asked for", budgeted as the
# largest tune in the image.
$LiveMallocMusic = @{
    'gortris' = @('music/tetris.mus')                        # its only tune
    'godzi'   = @('music/commodo.mus', 'music/fsonata.mus')  # both modes' tunes, held all session
    'grknoid' = @('music/katmand.mus')                       # GK_MUS_PAGES: one buffer, the largest tune
    'sidplay' = @('*')                                       # sized to whichever tune it is asked to play
}

# Task -> a kMalloc block whose size is a constant, in pages.
$LiveMallocFixed = @{
    'edit'   = 32    # the document: MAX_CHUNKS x CHUNK_PAGES (8 chunks of 4 pages).
                     # The clipboard is NOT here: it is transient, and a select-all
                     # copy of a full document asks for ~32 more pages, which the
                     # pool may refuse (the cap allows it, the free run may not).
    'copy'   = 4     # scratch block
    'banner' = 2     # row buffer, also the entry-name buffer
}

function Get-HeaderField([string]$Path, [int]$Offset) {
    $b = [System.IO.File]::ReadAllBytes($Path)
    return [int]$b[$Offset] -bor ([int]$b[$Offset + 1] -shl 8)
}

function Get-Pages([int]$Bytes) {
    return [int][math]::Ceiling($Bytes / 256)
}

# --- the pool's own geometry, from the kernel -------------------------------
$poolText = Get-Content 'src/kernel/pool.inc' -Raw
$poolSizeMatch = [regex]::Match($poolText, 'POOL_SIZE\s*=\s*\$([0-9A-Fa-f]+)')
$pageSizeMatch = [regex]::Match($poolText, 'POOL_PAGE_SIZE\s*=\s*\$([0-9A-Fa-f]+)')
if (-not $poolSizeMatch.Success -or -not $pageSizeMatch.Success) {
    Write-Error "check-pool-budget: cannot read POOL_SIZE / POOL_PAGE_SIZE from src/kernel/pool.inc"
    exit 1
}
$poolBytes = [Convert]::ToInt32($poolSizeMatch.Groups[1].Value, 16)
$pageBytes = [Convert]::ToInt32($pageSizeMatch.Groups[1].Value, 16)
$poolPages = [int]($poolBytes / $pageBytes)

# MAX_MALLOC_PAGES = (MALLOC_SHADOW_BYTES >> 1) >> (8 + POOL_PAGE_SHIFT): half the
# per-slot shadow, in pages. A block table that asks for more than this can never
# be handed out, so it is an error in the table rather than a footprint.
$shadowMatch = [regex]::Match($poolText, 'MALLOC_SHADOW_BYTES\s*=\s*([0-9]+)\s*\*\s*([0-9]+)')
if (-not $shadowMatch.Success) {
    Write-Error "check-pool-budget: cannot read MALLOC_SHADOW_BYTES from src/kernel/pool.inc"
    exit 1
}
$shadowBytes = [int]$shadowMatch.Groups[1].Value * [int]$shadowMatch.Groups[2].Value
$maxMallocPages = [int](($shadowBytes / 2) -shr (8 + [int][math]::Log($pageBytes / 256, 2)))

# --- library id order and eager flags, from the kernel ----------------------
$mainText = Get-Content 'src/main.asm' -Raw
$orderMatch = [regex]::Match($mainText, 'libNameLo:(.*?)libNameHi:', [System.Text.RegularExpressions.RegexOptions]::Singleline)
if (-not $orderMatch.Success) {
    Write-Error "check-pool-budget: cannot find the libNameLo block in src/main.asm"
    exit 1
}
$libOrder = @()
foreach ($m in [regex]::Matches($orderMatch.Groups[1].Value, '<libName([A-Za-z0-9]+)')) {
    $libOrder += $m.Groups[1].Value.ToLower() + '.lib'
}
$eagerMatch = [regex]::Match($mainText, 'libEager:\s*(?:\r?\n)?\s*\.byte\s*([0-9,\s]+)')
if (-not $eagerMatch.Success) {
    Write-Error "check-pool-budget: cannot find libEager in src/main.asm"
    exit 1
}
$eagerFlags = @()
foreach ($v in $eagerMatch.Groups[1].Value.Split(',')) {
    $v = $v.Trim()
    if ($v -ne '') { $eagerFlags += [int]$v }
}
if ($libOrder.Count -ne $eagerFlags.Count) {
    Write-Error "check-pool-budget: libNameLo names $($libOrder.Count) libraries but libEager lists $($eagerFlags.Count)"
    exit 1
}

# --- library file sizes, from the built binaries ----------------------------
# The manifest is { _readme, tasks } and "out" defaults to <name>.bin.
$manifest = (Get-Content 'tools/tasks.json' -Raw | ConvertFrom-Json).tasks
$libPagesByName = @{}
foreach ($entry in $manifest) {
    if ($entry.kind -ne 'lib') { continue }
    $bin = Join-Path 'tasks-out' $entry.out
    if ($entry.out -eq $null -or $entry.out -eq '') { $bin = Join-Path 'tasks-out' "$($entry.name).bin" }
    if (-not (Test-Path $bin)) {
        Write-Error "check-pool-budget: $($entry.out) is missing -- run tools/build-tasks.ps1 first"
        exit 1
    }
    $libPagesByName[$entry.fsName] = Get-Pages (Get-HeaderField $bin 5)
}

# --- every task's live footprint -------------------------------------------
$fail = 0
$tight = 0
$rows = @()
foreach ($entry in $manifest) {
    if ($entry.kind -eq 'lib') { continue }
    $bin = Join-Path 'tasks-out' $entry.out
    if ($entry.out -eq $null -or $entry.out -eq '') { $bin = Join-Path 'tasks-out' "$($entry.name).bin" }
    if (-not (Test-Path $bin)) {
        Write-Error "check-pool-budget: $($entry.name) has no built binary in tasks-out -- run tools/build-tasks.ps1 first"
        exit 1
    }
    $codePages = Get-Pages (Get-HeaderField $bin 5)
    $mask = Get-HeaderField $bin 10
    $flags = (Get-HeaderField $bin 2) -band 0xFF

    $parts = @("code $codePages")
    $total = $codePages
    for ($id = 0; $id -lt $libOrder.Count; $id++) {
        if ((($mask -shr $id) -band 1) -eq 0) { continue }
        $name = $libOrder[$id]
        if (-not $libPagesByName.ContainsKey($name)) {
            Write-Error "check-pool-budget: $($entry.name) declares $name, which has no built binary"
            exit 1
        }
        if ($eagerFlags[$id] -ne 0) {
            $total += $libPagesByName[$name]
            $parts += "$($name -replace '\.lib$','') $($libPagesByName[$name])"
        }
    }
    foreach ($name in $LiveLazy[$entry.name]) {
        $libId = $libOrder.IndexOf($name)
        if ($libId -lt 0) {
            Write-Error "check-pool-budget: $($entry.name) is listed as loading $name, which is not one of the nine libraries in src/main.asm"
            exit 1
        }
        if ((($mask -shr $libId) -band 1) -eq 0) {
            Write-Error "check-pool-budget: $($entry.name) is listed as loading $name, but its libMask does not declare it -- fix $LiveLazy's row or the task's header"
            exit 1
        }
        $total += $libPagesByName[$name]
        $parts += "$($name -replace '\.lib$','') $($libPagesByName[$name]) (lazy, live)"
    }

    # --- the kMalloc blocks it holds while alive --------------------------------
    $mallocPages = 0
    foreach ($tune in $LiveMallocMusic[$entry.name]) {
        $tunePath = $tune
        if ($tunePath -eq '*') {
            $tunePath = (Get-ChildItem 'music/*.mus' | Sort-Object -Property Length -Descending |
                         Select-Object -First 1).FullName
        }
        if (-not (Test-Path $tunePath)) {
            Write-Error "check-pool-budget: $($entry.name) is listed as loading $tune, which does not exist"
            exit 1
        }
        $mallocPages += Get-Pages (Get-Item $tunePath).Length
    }
    if ($null -ne $LiveMallocFixed[$entry.name]) {
        $mallocPages += [int]$LiveMallocFixed[$entry.name]
    }
    if ((($flags -band 0x10) -ne 0) -and ($mallocPages -eq 0)) {
        Write-Error "check-pool-budget: $($entry.name) sets KF_MALLOC_RAM (the loader kMallocs a block sized by the spawn argument) and has no row in `$LiveMallocMusic or `$LiveMallocFixed -- add one for the pages the caller asks for"
        exit 1
    }
    if ($mallocPages -gt $maxMallocPages) {
        Write-Error "check-pool-budget: $($entry.name) is listed as holding $mallocPages kMalloc pages, over MAX_MALLOC_PAGES ($maxMallocPages) -- the allocator cannot hand that out"
        exit 1
    }
    if ($mallocPages -gt 0) {
        $total += $mallocPages
        $parts += "malloc $mallocPages"
    }

    $margin = $poolPages - $total
    if ($margin -lt 0) { $fail++ }
    if ($margin -eq 0) { $tight++ }
    $rows += [pscustomobject]@{ Task = $entry.name; Pages = $total; Margin = $margin; Parts = ($parts -join ' + ') }
}

$rows = $rows | Sort-Object -Property Pages -Descending
Write-Host "pool budget: $poolPages pages ($poolBytes bytes, $pageBytes byte pages)"
foreach ($r in $rows) {
    $flag = ''
    if ($r.Margin -lt 0) { $flag = '  <-- OVER' } elseif ($r.Margin -eq 0) { $flag = '  <-- exactly full' }
    Write-Host ("  {0,-10} {1,4} pages  margin {2,4}{3}" -f $r.Task, $r.Pages, $r.Margin, $flag)
    Write-Host ("             {0}" -f $r.Parts)
}

if ($fail -gt 0) {
    Write-Host ""
    Write-Error "check-pool-budget: $fail task(s) need more pages than the pool has -- see the OVER rows above"
    exit 1
}
if ($tight -gt 0) {
    $names = ($rows | Where-Object { $_.Margin -eq 0 } | ForEach-Object { $_.Task }) -join ', '
    Write-Host ""
    Write-Warning "check-pool-budget: $names fit(s) the pool exactly, with no page to spare -- the next byte of its code, or of any library it holds, breaks it"
}
exit 0
