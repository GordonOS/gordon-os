# gen-task-asm.ps1 - generate the task pipeline inputs from one manifest.
#
#   tools/tasks.json  ->  tools/assemble-tasks.asm   (stage 1: 3x assembly)
#                         tools/gen-fixups.asm       (stage 2: fixup tables)
#
# Why generated: KickAssembler needs a distinct `.segmentdef`/`outBin` per
# task, and a segment NAME must be a literal identifier at parse time - a
# computed name dies with "Not implemented", and a macro parameter as the
# name expands to "Invalid directive" (`.import source` also demands a
# string constant). So the per-task blocks cannot be produced by the
# assembler and have to be emitted text.
#
# Usage:
#   gen-task-asm.ps1           write the files if they are stale
#   gen-task-asm.ps1 -Check    write nothing; fail if the files are stale
#
# Keep this file pure ASCII: PowerShell 5.1 reads BOM-less .ps1 files as
# ANSI, so non-ASCII bytes corrupt string parsing.

param([switch]$Check)

$ErrorActionPreference = "Stop"

Set-Location (Split-Path -Parent $PSScriptRoot)   # project root (tools/..)

$manifestPath = "tools/tasks.json"
if (-not (Test-Path $manifestPath)) {
    throw "Missing $manifestPath - it is the source of truth for the task pipeline"
}

$manifest = (Get-Content -Raw $manifestPath) | ConvertFrom-Json
$tasks = @($manifest.tasks)
if ($tasks.Count -eq 0) { throw "$manifestPath lists no tasks" }

# Normalise line endings so a CRLF/LF difference never counts as "stale".
function Get-Normalised([string]$text) { return ($text -replace "`r`n", "`n") }

# The generated files are stored with the repository's own ending (LF); the
# builders join with LF and the writer converts to whatever the destination
# file already uses, so a Windows checkout of the repo (CRLF) is not rewritten
# line-for-line on the next regeneration.
function Get-Eol([string]$text) { if ($text -match "`r`n") { return "`r`n" } else { return "`n" } }

# ---------------------------------------------------------------- stage 1
function New-AssembleTasksText {
    $L = New-Object System.Collections.Generic.List[string]

    # --- fixed preamble ---
    $preamble = @(
        '// =========================================='
        '//  assemble-tasks.asm - stage 1 of fixup generation'
        '// =========================================='
        '//  GENERATED FILE - do not edit by hand.'
        '//  Source of truth: tools/tasks.json'
        '//  Regenerate:     tools/gen-task-asm.ps1'
        '//'
        '//  Assembles each task THREE times so tools/gen-fixups.asm can diff'
        '//  the code bodies:'
        '//'
        '//    java -cp KickAss.jar kickass.KickAssembler \'
        '//         -afo -libdir src tools/assemble-tasks.asm :shift=0'
        '//    java -cp KickAss.jar kickass.KickAssembler \'
        '//         -afo -libdir src tools/assemble-tasks.asm :shift=256'
        '//    java -cp KickAss.jar kickass.KickAssembler \'
        '//         -afo -libdir src tools/assemble-tasks.asm :shift=512'
        '//'
        '//  Run from the project root. :shift=0 -> *.base.bin,'
        '//  :shift=256 -> *.shift.bin (origin + $100),'
        '//  :shift=512 -> *.check.bin (origin + $200, relocation verification).'
        '//'
        '//  Each task source sets NO .pc - this script sets it.'
        '//  The header ORIGIN field inside each source is a hard-coded'
        '//  literal, so it stays identical in all three variants.'
        '//  Task sources zero-pad to 256 bytes, so the code body'
        '//  always starts at file offset 256 in every variant.'
        '// =========================================='
        ''
        '.var SHIFT = cmdLineVars.get("shift").asNumber()'
        '.var variant = (SHIFT == 0) ? "base" : (SHIFT == 256 ? "shift" : "check")'
        ''
        '// Shared kernel API constants (import once - task sources rely on these)'
        '.import source "kernel/jumptable.inc"'
        '.const syscall = kSyscall       // old task sources use the internal name'
        ''
        '// Shared task API aliases (kernel addresses - keep in sync with the'
        '// kernel build; src/main.asm .asserts the critical ones). Defined here'
        '// ONCE because KickAssembler symbols are global across all segments.'
        '.const dataPtr     = $F1'
        '.const taskNamePtr = $F3'
        '.const kPtr        = $F5'
        '.const spawnPtr    = $F7'
        '.const srcPtr      = $F9'
        '.const kArg        = $4000'
        '.const screenBase  = $E11C'
        ''
        '// Shared kernel-data addresses (taskState/totalTasks/taskParent/dynTask*...)'
        '// - defined once, imported globally so every task sees them without'
        '// re-defining (KickAssembler symbols are global across all segments).'
        '.import source "src/tasks/kernel-data.inc"'
        ''
        '// fp.lib shared constants (FP_ZP_*/FP_* opcodes/errors + assert macros)'
        '// - imported globally; lib and task sources must NOT import it again.'
        '.import source "src/libs/fp.inc"'
        ''
        '// mouse.lib shared constants (MOUSE_* opcodes + the kArg result map)'
        '// - imported globally; lib and task sources must NOT import it again.'
        '.import source "src/libs/mouse.inc"'
        ''
        '// Pool geometry (single source of truth) - POOL_* + poolAsl/poolLsr.'
        '// The same file the kernel build imports (via kernel.inc), imported once here.'
        '.import source "src/kernel/pool.inc"'
        ''
        '// Task header ABI (magic, flags, libMask bits) - single source'
        '// of truth; the kernel build imports the same file via kernel.inc.'
        '.import source "src/kernel/task-header.inc"'
        ''
        '// Sprite shadow ABI (SPR_SHD_* offsets, SPR_SLOT* addresses and the'
        '// sprOn/sprColour/sprMode/... macros). The offsets are what rasterCommit'
        '// publishes to $D000-$D02E once per frame; the kernel side of the'
        '// contract is src/kernel/sprite.inc and src/main.asm pins both sides.'
        '.import source "src/tasks/sprite-shadow.inc"'
        ''
    )
    foreach ($line in $preamble) { $L.Add($line) }

    $libsSeen = $false
    foreach ($t in $tasks) {
        if ($t.kind -eq 'lib' -and -not $libsSeen) {
            $L.Add('// --- dynamic libraries ---')
            $libsSeen = $true
        }
        $taskNote = ''
        if ($t.PSObject.Properties.Name -contains 'note' -and $t.note) { $taskNote = ' - ' + $t.note }
        $L.Add(('// --- {0}{1} ---' -f $t.name, $taskNote))
        $L.Add(('.segmentdef {0}Out [outBin="../tasks-out/temp/{0}." + variant + ".bin"]' -f $t.name))
        $L.Add(('.segment {0}Out' -f $t.name))
        $L.Add(('.pc = {0} + SHIFT' -f $t.origin))
        $L.Add(('.import source "{0}"' -f $t.source))
        $L.Add('')
    }
    return ($L -join "`n")
}

# ---------------------------------------------------------------- stage 2
function New-GenFixupsText {
    $L = New-Object System.Collections.Generic.List[string]

    $preamble = @(
        '// =========================================='
        '//  gen-fixups.asm - stage 2 of fixup generation'
        '// =========================================='
        '//  GENERATED FILE - do not edit by hand.'
        '//  Source of truth: tools/tasks.json'
        '//  Regenerate:     tools/gen-task-asm.ps1'
        '//'
        '//  Reads the base/shift/check temp assemblies produced by'
        '//  assemble-tasks.asm and emits the final task binaries:'
        '//'
        '//    header + zero-pad to 256 + code body + fixup words + $FFFF'
        '//'
        '//  Fixup rule: every code byte where shift[i] == base[i] + 1 is an'
        '//  absolute high byte, i.e. a relocation site. Offsets are'
        '//  code-body-relative; the loader adds the page delta to each.'
        '//'
        '//    java -cp KickAss.jar kickass.KickAssembler -afo tools/gen-fixups.asm'
        '//'
        '//  Run from the project root, AFTER all three assemble-tasks runs.'
        '// =========================================='
        ''
    )
    foreach ($line in $preamble) { $L.Add($line) }

    foreach ($t in $tasks) {
        $n = $t.name
        $out = "$n.bin"
        if ($t.PSObject.Properties.Name -contains 'out' -and $t.out) { $out = $t.out }
        $taskNote = ''
        if ($t.PSObject.Properties.Name -contains 'note' -and $t.note) { $taskNote = ' - ' + $t.note }

        $L.Add(('// --- {0}{1} ---' -f $n, $taskNote))
        $L.Add(('.var {0}Base     = LoadBinary("../tasks-out/temp/{0}.base.bin")' -f $n))
        $L.Add(('.var {0}Shift    = LoadBinary("../tasks-out/temp/{0}.shift.bin")' -f $n))
        $L.Add(('.var {0}Check    = LoadBinary("../tasks-out/temp/{0}.check.bin")' -f $n))
        $L.Add(('.var {0}NameLen  = {0}Base.get(12)' -f $n))
        $L.Add(('.var {0}CodeSize = {0}Base.getSize() - 256' -f $n))
        $L.Add(('.var {0}Fixups   = List()' -f $n))
        $L.Add('')
        $L.Add('// scan: a code byte that advanced by exactly one page is a high byte.')
        $L.Add('// Mask both operands to 0..255 - LoadBinary returns signed bytes, and')
        $L.Add('// an unmasked difference is only correct while no byte crosses $80.')
        $L.Add('.for (var i = 256; i < {0}Base.getSize(); i++) {{' -f $n)
        $L.Add('    .if ((({0}Shift.get(i) & $FF) - ({0}Base.get(i) & $FF)) == 1) {{' -f $n)
        $L.Add('        .eval {0}Fixups.add(i - 256)' -f $n)
        $L.Add('    }')
        $L.Add('}')
        $L.Add(('.print "{0}: " + {0}CodeSize + " bytes of code, " + {0}Fixups.size() + " fixups"' -f $n))
        $L.Add('')
        $L.Add('// verify fixups: base + 2 pages must equal check.')
        $L.Add('.for (var f = 0; f < {0}Fixups.size(); f++) {{' -f $n)
        $L.Add('    .var idx = 256 + {0}Fixups.get(f)' -f $n)
        $L.Add('    .if (((({0}Base.get(idx) & $FF) + 2) & $FF) != ({0}Check.get(idx) & $FF)) {{' -f $n)
        $L.Add('        .error "{0}: fixup verify failed at code offset " + {0}Fixups.get(f)' -f $n)
        $L.Add('    }')
        $L.Add('}')
        $L.Add('')
        $L.Add('// verify position independence: every NON-fixup byte must be equal.')
        $L.Add('.var {0}Fi = 0' -f $n)
        $L.Add('.for (var i = 256; i < {0}Base.getSize(); i++) {{' -f $n)
        $L.Add('    .if (({0}Fi < {0}Fixups.size()) && (i - 256 == {0}Fixups.get({0}Fi))) {{' -f $n)
        $L.Add('        .eval {0}Fi++' -f $n)
        $L.Add('    } else {')
        $L.Add('        .if (({0}Base.get(i) & $FF) != ({0}Check.get(i) & $FF)) {{' -f $n)
        $L.Add('            .error "{0}: non-fixup byte changed at code offset " + (i - 256)' -f $n)
        $L.Add('        }')
        $L.Add('    }')
        $L.Add('}')
        $L.Add(('.print "{0}: relocation verification OK"' -f $n))
        $L.Add('')
        $L.Add(('.segmentdef {0}Final [outBin="../tasks-out/{1}"]' -f $n, $out))
        $L.Add(('.segment {0}Final' -f $n))
        $L.Add('')
        $L.Add('// magic byte 1 (+0) is shared.')
        $L.Add('.fill 2, {0}Base.get(i)' -f $n)
        $L.Add('')
        $L.Add('// Header: flags..name, then the fixup trailer (16-bit count + the')
        $L.Add('// table offset), then zero padding to the 256-byte page.')
        $L.Add('    .fill 11 + {0}NameLen, {0}Base.get(2 + i)      // flags..name' -f $n)
        $L.Add('    .word {0}Fixups.size()                          // fixup count (16-bit)' -f $n)
        $L.Add('    .word 256 + {0}CodeSize                         // fixup_offset (file offset of the table)' -f $n)
        $L.Add('    .if (17 + {0}NameLen > 256) {{' -f $n)
        $L.Add('        .error "{0}: header overflow"' -f $n)
        $L.Add('    }')
        $L.Add('    .fill 256 - (17 + {0}NameLen), 0                // pad to the 256-byte page' -f $n)
        $L.Add('    .fill {0}CodeSize, {0}Base.get(256 + i)         // code body' -f $n)
        $L.Add('    .for (var f = 0; f < {0}Fixups.size(); f++) {{' -f $n)
        $L.Add('        .word {0}Fixups.get(f)' -f $n)
        $L.Add('    }')
        $L.Add('    .word $FFFF')
        $L.Add('')
    }
    return ($L -join "`n")
}

# ------------------------------------------------------------------ main
$targets = @(
    @{ Path = "tools/assemble-tasks.asm"; Text = (New-AssembleTasksText) },
    @{ Path = "tools/gen-fixups.asm";     Text = (New-GenFixupsText) }
)

$stale = New-Object System.Collections.Generic.List[string]
foreach ($t in $targets) {
    $disk = $null
    if (Test-Path $t.Path) { $disk = [IO.File]::ReadAllText((Join-Path (Get-Location) $t.Path)) }
    if ($null -eq $disk -or (Get-Normalised $disk) -ne (Get-Normalised $t.Text)) { $stale.Add($t.Path) }
}

if ($Check) {
    if ($stale.Count -gt 0) {
        throw ("Generated task asm is stale: " + ($stale -join ", ") + " - run tools/gen-task-asm.ps1")
    }
    Write-Host "task pipeline asm is up to date ($($tasks.Count) tasks/libraries)"
    exit 0
}

if ($stale.Count -eq 0) {
    Write-Host "task pipeline asm is up to date ($($tasks.Count) tasks/libraries)"
    exit 0
}

$utf8NoBom = New-Object System.Text.UTF8Encoding($false)
foreach ($t in $targets) {
    $full = Join-Path (Get-Location) $t.Path
    $eol = "`n"
    if (Test-Path $full) { $eol = Get-Eol ([IO.File]::ReadAllText($full)) }
    $body = $t.Text
    if ($eol -ne "`n") { $body = $body -replace "`n", $eol }
    [IO.File]::WriteAllText($full, $body, $utf8NoBom)
    Write-Host "wrote $($t.Path)"
}
Write-Host "regenerated the task pipeline asm from tools/tasks.json ($($tasks.Count) entries)"
exit 0
