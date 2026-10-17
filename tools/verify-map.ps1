# verify-map.ps1 -- parse the kernel build map and check the pocket claims.
# The standing static gate for the memory map: it runs the kernel build, parses
# the `Memory Map` block, recomputes the free runs (after carving out what the
# map cannot show -- the fixed sprite slots above all) and checks the totals that
# docs/memory-map.md publishes. Run it with no arguments after any layout change:
#   pwsh -File tools/verify-map.ps1                          (Linux/macOS)
#   powershell -File tools/verify-map.ps1                    (Windows)
# Override the expected figures with -ExpectTotal / -ExpectPockets if a change is
# meant to move them (then update docs/memory-map.md in the same commit).

param(
    [Parameter(Mandatory = $false)][string]$ExpectTotal = '125',
    [Parameter(Mandatory = $false)][string]$ExpectPockets = '22'
)

$ErrorActionPreference = 'Stop'
$root = Join-Path $PSScriptRoot '..'
Push-Location $root

. "$PSScriptRoot/host.ps1"                        # $PowerShellExe

# --- run the build, capture stdout ---
$out = & $PowerShellExe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'build-kernel.ps1') 2>&1 | Out-String

# --- asserts ---
$madeLine = ($out -split "`n" | Where-Object { $_ -match 'Made\s+(\d+)\s+asserts\s*,\s*(\d+)\s+failed' } | Select-Object -First 1)
if ($madeLine) { Write-Host ("build: {0}" -f $madeLine.Trim()) }
else { Write-Host 'build: ASSERTS LINE NOT FOUND'; }
$assertsMade = 0; $assertsFailed = -1
if ($madeLine -match 'Made\s+(\d+)\s+asserts\s*,\s*(\d+)\s+failed') {
    $assertsMade = [int]$Matches[1]; $assertsFailed = [int]$Matches[2]
}

# --- parse the Memory Map block ---
$occ = @()
$inMap = $false
foreach ($line in ($out -split "`n")) {
    if ($line -match '^Memory Map') { $inMap = $true; continue }
    if ($inMap -and $line -match '^\s*\$([0-9a-fA-F]{4})-\$([0-9a-fA-F]{4})') {
        $occ += , @([Convert]::ToInt32($Matches[1], 16), [Convert]::ToInt32($Matches[2], 16))
    }
}
if ($occ.Count -eq 0) { Write-Host 'FAIL: could not parse the Memory Map'; Pop-Location; exit 1 }

# --- regions the map cannot show (runtime / functional, not kernel pockets) ---
$occ += , @(0x021f, 0x02a0)   # EhBASIC page-2 block (runtime-loaded task)
$occ += , @(0x0400, 0x07ff)   # bitmap video matrix
$occ += , @(0x0800, 0x0800)   # byte below the BASIC stub
$occ += , @(0x02c0, 0x03ff)   # sprite slots 0-4 (fixed VIC blocks)
#  Slots 5-7 are three fixed VIC blocks at $0F00/$0F40/$0F80.
#  $0FC0-$0FCB is the band's relocation slack, 10 bytes of which hold the
#  mouse.lib name string ($0FC0-$0FC9) -- 2 bytes remain free. The core's last
#  block ends at $0EFD, so the gap below the band is the run $0EFE-$0EFF.
$occ += , @(0x0f00, 0x0fbf)   # sprite slots 5-7
$occ += , @(0x1d40, 0x1fff)   # data heap
$occ += , @(0x2000, 0x3fff)   # framebuffer
$occ += , @(0x5700, 0xc3ff)   # task pool
$occ += , @(0xd000, 0xdfff)   # I/O
$occ += , @(0xfffa, 0xffff)   # hardware vectors
$occ = $occ | Sort-Object { $_[0] }

# --- free runs ---
$runs = @(); $total = 0; $cur = 0x020d
foreach ($b in $occ) {
    if ($b[0] -gt $cur) {
        $size = $b[0] - $cur
        $runs += [pscustomobject]@{ Range = ('{0:X4}-{1:X4}' -f $cur, ($b[0] - 1)); Size = $size }
        $total += $size
    }
    if (($b[1] + 1) -gt $cur) { $cur = $b[1] + 1 }
}

Write-Host ''
Write-Host 'Free runs (largest first):'
$runs | Sort-Object Size -Descending | Format-Table -AutoSize
Write-Host ('free total: {0} bytes across {1} pockets' -f $total, $runs.Count)

# --- checks ---
$bad = $false
function Check($name, $cond, $detail) {
    if ($cond) { Write-Host ('  OK   {0} {1}' -f $name, $detail) }
    else { Write-Host ('  FAIL {0} {1}' -f $name, $detail); $script:bad = $true }
}
function FreeRun($a, $b) { @($runs | Where-Object { $_.Range -eq $wantRange }) }
function Occupied($a, $b) {
    $hit = $false
    foreach ($x in $occ) { if ($x[0] -le $a -and $x[1] -ge $b) { $hit = $true } }
    $hit
}

Write-Host ''
Write-Host 'Checks:'
Check 'build green        :' ($assertsFailed -eq 0 -and $assertsMade -gt 0) ("{0} asserts, {1} failed" -f $assertsMade, $assertsFailed)

# PRG first two bytes
$prg = [System.IO.File]::ReadAllBytes((Join-Path $root 'build/gordon-os.prg'))[0..1]
Check 'PRG starts 01 08   :' ($prg[0] -eq 1 -and $prg[1] -eq 8) ('{0:X2} {1:X2}' -f $prg[0], $prg[1])

# Range lookups are done inline: a helper resolving $runs from the parent scope
# silently returns nothing, which is worse than verbose. Every check below is a
# permanent layout invariant.
Check 'scratch FC3F-FF7D:' (Occupied 0xfc3f 0xff7d) 'one contiguous gfx.lib scratch block (831B)'
#  The library-entry block: sid.lib's and ipc.lib's call entries, their lazy
#  cold stubs and both name strings, $FF7E-$FFE5 (104B). It must stay the LAST
#  thing assembled in main.asm.
Check 'lib entries FF7E  :' (Occupied 0xff7e 0xffe8) 'sid.lib + ipc.lib entries + cold stubs + both names (104B) + kLibHold (3B)'
#  sprite.lib has no entry here and no cold stub: it is eager, and its four
#  call entries are the first four stubs in the $CDDE block (sprite.inc).
$hit = @($runs | Where-Object { $_.Range -eq 'FFF6-FFF9' })
Check 'pocket FFF6-FFF9 :' ($hit.Count -eq 1 -and $hit[0].Size -eq 4) ('{0} bytes free above kLibHold (gfxSurfPrep tail 13B at $FFE9)' -f ($hit | ForEach-Object { $_.Size }))
$hit = @($runs | Where-Object { $_.Range -eq 'FFEC-FFF9' })
Check 'probe 52CD      :' (Occupied 0x52cd 0x52db) ('dlProbeFit is 15B in the loader band')
Check 'sprite name 1839 :' (Occupied 0x1839 0x1843) 'sprite.lib name string (11B); the library is eager, so it needs no cold stub'
Check 'sprite install 52DC :' (Occupied 0x52dc 0x5336) 'sprInstall + the state mover are 91B in the loader band (the per-task hand-over, up from the gate''s 73B)'
Check 'sprite warm reset   :' (Occupied 0x529e 0x52c0) 'warmSpriteReset is 35B in the pocket below dlProbeFit ($52CD) -- a reboot must forget every task''s sprite state'
#  warmBootReset (kernel/warm-boot-reset.inc) lives in its own run at
#  $181D-$183F, NOT in the core's growth headroom: code parked at the bottom of
#  that pocket leaves the whole core band without room, and the next core change
#  fails the build with a block overlap. The two checks below pin both ends of
#  that trade -- the routine's own pocket, and the headroom it leaves free.
#  printGlyphAt is ONE routine in TWO runs: its prologue is bigger than either
#  pocket, so the tail is a `jmp` away -- the same "a routine reached by a jmp is
#  an ideal tenant for a free run" trade warmBootReset uses. main.asm asserts
#  both ends. The tail sits above the sprite API and below dlEvMaybeSelf.
$hit = @($runs | Where-Object { $_.Range -eq '186B-186E' })
Check 'pocket 186B-186E :' ($hit.Count -eq 1 -and $hit[0].Size -eq 4) ('{0} bytes (printGlyphAt fills $1844 up to here, below taskLo at $186F)' -f ($hit | ForEach-Object { $_.Size }))
$hit = @($runs | Where-Object { $_.Range -eq 'CA8B-CA8B' })
Check 'printGlyphAt tail :' ($hit.Count -eq 1 -and $hit[0].Size -eq 1) ('{0} byte left after printGlyphAt''s tail ($CA6B-$CA8A), before dlEvMaybeSelf' -f ($hit | ForEach-Object { $_.Size }))
#  The core headroom run (15B, pinned below) is the kernel core's only spare
#  space: it is the run the next core change takes, and a change larger than it
#  needs a relocation. main.asm's `kernelCoreEnd <= $13D4` assert fires when it
#  runs out.
#  The loader band's own headroom is the region above `dynload.inc` (ends
#  $529D) and below font-help ($5340): `dlProbeFit` ($52CD-$52DB) and
#  sprInstall ($52DC-$5336) live there, leaving the free run pinned below.
#  A loader change bigger than that free run needs a relocation.
#  The pocket under gfx.inc's `$ECDC` anchor holds kPrintCharAt's surface router
#  The code chain's growth room is the run at the top of the chain,
#  `$FC3D-$FC3E` (2B): blitGlyphFb (9B) sits in the `$CDF5` pocket rather than in
#  the chain, and what leaves the chain is what the chain can grow by. Growing
#  past `$FC3E` trips the `.pc = $FC3F` anchor.
Check 'router ECBD      :' (Occupied 0xECBD 0xECDB) 'paRoute is 31B - the pocket under gfx.inc ($ECDC) exactly full'
Check 'glyph blit CDF5 :' (Occupied 0xCDF5 0xCDFD) 'blitGlyphFb is 9B in the $CDF5 pocket'
$hit = @($runs | Where-Object { $_.Range -eq 'FC3D-FC3E' })
#  The code-chain growth run below the $FC3F gfx scratch pin: 2B. gfxOp ($ED5D) is one
#  byte of it -- the pixel write op sits after gfx.inc's pinned data block so that no
#  address gfx.lib hardcodes moves, which is the one byte of chain room it costs.
Check 'chain tail        :' ($hit.Count -eq 1 -and $hit[0].Size -eq 2) ('{0} bytes of code-chain growth room below the $FC3F gfx scratch pin' -f ($hit | ForEach-Object { $_.Size }))
#  The `$E000` band's own slack: jump table group 2 ends at `$E0F2` and the band
#  holds two more entries there -- kPrintGlyphAt took `$E0F3` and kMouseCall the
#  last one, `$E0F6`, before the filesys stubs at `$E0F9`. The band now has no
#  spare entry.
Check 'kMouseCall E0F6  :' (Occupied 0xE0F6 0xE0F8) 'kMouseCall fills the band''s last slack entry ($E0F6-$E0F8)'
#  The mouse.lib name string spends 10 of the 12 bytes that were the sprite
#  band's relocation slack.
Check 'mouse name 0FC0  :' (Occupied 0x0FC0 0x0FC9) 'libNameMouse is 10B in the $0FC0 slack pocket'
#  libStubsEnd's own margin: the block ends at $1C6A and sysExit starts at
#  $1C6E, which main.asm asserts as `libStubsEnd <= $1C6E`.
$hit = @($runs | Where-Object { $_.Range -eq '1C6A-1C6D' })
Check 'stub tail 1C6A   :' ($hit.Count -eq 1 -and $hit[0].Size -eq 4) ('{0} bytes free between libStubsEnd ($1C6A) and sysExit ($1C6E)' -f ($hit | ForEach-Object { $_.Size }))
$hit = @($runs | Where-Object { $_.Range -eq '13C5-13D3' })
Check 'core headroom     :' ($hit.Count -eq 1 -and $hit[0].Size -eq 15) ('{0} bytes free after kernelCoreEnd ($13C5), before reuDirBuf' -f ($hit | ForEach-Object { $_.Size }))
$hit = @($runs | Where-Object { $_.Range -eq '5337-533F' })
Check 'loader headroom   :' ($hit.Count -eq 1 -and $hit[0].Size -eq 9) ('{0} bytes free after sprInstall ($52DC-$5336), before the font helpers' -f ($hit | ForEach-Object { $_.Size }))
Check 'sprite helpers CD9F:' (Occupied 0xcd9f 0xcdce) 'sprBlkAddr + sprRestoreFrames are 48B in the $CD9F pocket'
Check 'sprite state CFA4 :' (Occupied 0xcfa4 0xcfff) 'the per-task sprite state (24B sheet records + 39B shadow + 8B frame indices) then the kernel-only bytes -- 92B'
Check 'sprite API CA2F    :' (Occupied 0xca2f 0xca6a) 'kSpriteCollide + clearSlotSprite are 60B above the pinned dyn tables'
Check 'dlEvMaybeSelf     :' (Occupied 0xCA8C 0xCA98) 'dlEvMaybeSelf is 13B at the top of the free run after sprite.inc, not in the core growth headroom'
#  The four loader-band helpers share one band: $5340 font-help, $53F2 pool-top,
#  $5457 cursor-helpers, $5482 slot-shadow (32B), ending flush at $54AA.
$hit = @($runs | Where-Object { $_.Range -eq '54A2-54AA' })
Check 'pocket 54A2-54AA :' ($hit.Count -eq 1 -and $hit[0].Size -eq 9) ('{0} bytes (slot-shadow 32B at $5482)' -f ($hit | ForEach-Object { $_.Size }))
#  The REU DMA band's own tail: the routines end at $CA1A and the sprite API
#  starts at $CA2F, so this run belongs to neither and stays free.
$hit = @($runs | Where-Object { $_.Range -eq 'CA2C-CA2E' })
Check 'pocket CA2C-CA2E :' ($hit.Count -eq 1 -and $hit[0].Size -eq 3) ('{0} bytes (warmClearBufRecs 17B at $CA1B)' -f ($hit | ForEach-Object { $_.Size }))
#  `gfxSurfPrep` fills the `$CD5C` run (free RAM between `dynTaskLibMask` and
#  the scroll-region data) up to `$CD6B`, leaving one byte. Its tail sits in
#  the `$FFE9` run above `kLibHold`. Both are reached by jump -- see the
#  `printGlyphAt` blocks in `kernel.asm` for the same shape.
#  The library failure path (`libLoadFail` + `libFailExit`) lives inside
#  the `$1A1B` stub block instead, whose growth limit `main.asm` asserts at
#  `$1C6E` -- the start of syscall.inc's sysExit block.
$hit = @($runs | Where-Object { $_.Range -eq 'CD6C-CD6C' })
Check 'pocket CD6C      :' ($hit.Count -eq 1 -and $hit[0].Size -eq 1) ('{0} byte (gfxSurfPrep 16B at $CD5C, before the scroll-region data)' -f ($hit | ForEach-Object { $_.Size }))
Check 'FFBF-FFCC         :' (Occupied 0xffbf 0xffcc) ' (the gfx scratch pin, inside the lib-entry block)'

if ($ExpectTotal -ne '') { Check 'free total         :' ($total -eq [int]$ExpectTotal) ("{0} (expected {1})" -f $total, $ExpectTotal) }
if ($ExpectPockets -ne '') { Check 'pocket count       :' ($runs.Count -eq [int]$ExpectPockets) ("{0} (expected {1})" -f $runs.Count, $ExpectPockets) }

Pop-Location
if ($bad) { exit 1 }
Write-Host ''
Write-Host 'All checks passed.'
