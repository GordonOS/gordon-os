# build-tasks.ps1 - assemble dynamic task binaries and generate fixup tables.
#
# Runs the pipeline: gen-task-asm (regenerate the block tables) + 3x
# assemble-tasks + gen-fixups, producing tasks-out/*.bin for every entry in
# tools/tasks.json. See docs/writing-relocatable-tasks.md.
#
# Run from anywhere; it changes to the project root first.
#
# Every artifact it produces is rebuilt, so it clears tasks-out/*.bin first: a
# binary whose entry left the manifest, or one left by a failed run, would
# otherwise survive as a stale file nothing distinguishes from a fresh build. On
# success it records the inputs it consumed in tasks-out/build.stamp, which
# tools/build-reu.ps1 requires before seeding the image (tools/tasks-stamp.ps1).

$ErrorActionPreference = "Stop"

# Project root = one level up from this script's directory (tools/)
Set-Location (Split-Path -Parent $PSScriptRoot)

. "$PSScriptRoot/host.ps1"                        # $PowerShellExe, $PythonExe

# Tripwire for the 24-bit REU FS offset (reuFsPtr): a copy/stash that drops the
# bank byte cannot be caught by any .assert. See the script header.
& "$PSScriptRoot/check-fs-offsets.ps1"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Tripwire for the library-id / descriptor-table coupling: a task's libMask
# names library ids, so the LIB_* constants in task-header.inc must match the
# positional lib tables in src/main.asm.
& "$PSScriptRoot/check-lib-ids.ps1"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Tripwire for an indirect operand outside zero page: `(label),y` addresses zero
# page only, and KickAssembler truncates a label's address to its low byte rather
# than refusing it -- an assembles-and-passes build with a wrong read in it.
& "$PSScriptRoot/check-zp-indirect.ps1"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# KickAssembler jar location - see tools/toolpaths.ps1
. "$PSScriptRoot/toolpaths.ps1"                   # machine paths: $KickAssJar
$kick = $KickAssJar
if (-not (Test-Path $kick)) {
    Write-Error "KickAss.jar not found at: $kick - edit .vscode/settings.json (vs64.kickInstallDir)"
    exit 1
}

# Regenerate tools/assemble-tasks.asm and tools/gen-fixups.asm from
# tools/tasks.json. Writing (not -Check) keeps them in sync automatically;
# the output is deterministic, so an unchanged manifest rewrites nothing.
# Adding a task is therefore a one-line manifest edit plus its source files.
& "$PSScriptRoot/gen-task-asm.ps1"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Clear the outputs this script owns (see the header comment): the assembles
# below write every one of them, so anything left behind is stale by definition.
Remove-Item 'tasks-out/*.bin' -Force -ErrorAction SilentlyContinue
Remove-Item 'tasks-out/temp/*.bin' -Force -ErrorAction SilentlyContinue

# kick.ps1 wraps KickAssembler and fails the build on a failed .assert.
& "$PSScriptRoot/kick.ps1" -cp $kick kickass.KickAssembler -afo -libdir src tools/assemble-tasks.asm :shift=0
& "$PSScriptRoot/kick.ps1" -cp $kick kickass.KickAssembler -afo -libdir src tools/assemble-tasks.asm :shift=256
& "$PSScriptRoot/kick.ps1" -cp $kick kickass.KickAssembler -afo -libdir src tools/assemble-tasks.asm :shift=512

# gen-fixups diffs the three variants with O(n^2) relocation checks;
# pin an explicit heap, but keep it modest enough for machines with a small page file.
& "$PSScriptRoot/kick.ps1" -Xmx2g -cp $kick kickass.KickAssembler -afo tools/gen-fixups.asm
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Build-time header gate: re-derive every field of every binary just written and
# check the ABI invariants the loader would otherwise only discover at load time
# -- trailer position, size identity, fixup bounds and ordering, the sentinel,
# and zero padding. A header-layout change is a whole-tree event and fails
# silently, so it is caught here rather than on the C64. See the script header.
if ($PythonExe -eq '') {
    Write-Error "no Python 3 interpreter found - install one, or create the repo .venv, to run tools/check-task-headers.py"
    exit 1
}

# Generated-table gate: musedit's piano glyphs and the pixel->semitone hit tables
# are both derived from one geometry table, so a stale .inc would draw one
# keyboard and test another. `-Check` regenerates in memory and fails on a diff.
& $PythonExe (Join-Path $PSScriptRoot 'gen-musedit-glyphs.py') -Check
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# The tempo table is the player's own (src/libs/sidplay.inc), copied rather than
# restated: a stale copy would make musedit measure notes the player plays
# differently.
& $PythonExe (Join-Path $PSScriptRoot 'gen-musedit-tables.py') -Check
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# musedit's printed strings: the .inc holds the indices and the REU file `msgs` holds
# the text, both from assets/ui/musedit-msgs.txt. A stale pair would print one
# message where another was meant.
& $PythonExe (Join-Path $PSScriptRoot 'gen-musedit-msgs.py') -Check
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# The duration arithmetic: the tool re-implements the player's routine, checks it
# against the published tempo table and against the generated copy above, and
# sweeps all 8,192 tempo/class/state combinations.
& $PythonExe (Join-Path $PSScriptRoot 'check-mus-timing.py')
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $PythonExe (Join-Path $PSScriptRoot 'check-task-headers.py') --quiet
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Tripwire for the pool's LIVE footprint: a task's code, plus the eager libraries
# its libMask declares, plus any lazy library its own startup code touches, must
# fit POOL_PAGES. The loader counts only the eager ones, so a task over budget
# loads successfully and then dies when it reaches the lazy one -- `basic` is the
# tight case, because its footprint is the whole pool. See the script header.
& "$PSScriptRoot/check-pool-budget.ps1"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Record the input set this build consumed, so a seeder can refuse a stale
# tasks-out/ (see tools/tasks-stamp.ps1).
. "$PSScriptRoot/tasks-stamp.ps1"
Set-Content -Path 'tasks-out/build.stamp' -Value (Get-TaskInputHash) -Encoding ASCII
