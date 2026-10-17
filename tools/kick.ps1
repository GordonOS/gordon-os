# kick.ps1 - run KickAssembler and fail the build if any .assert fails.
#
# KickAssembler prints "ERROR IN ASSERTION!!!" / "Made N asserts, M FAILED!"
# on a failed .assert but STILL exits 0, so a broken guardrail silently
# ships a bad binary. This wrapper runs the assembler, streams its output,
# and throws (=> non-zero exit) if an assertion failed.
#
# Usage: kick.ps1 <java-args...>   -- the full java command line, e.g.
#        kick.ps1 -jar <KickAss.jar> -odir ... main.asm
#        kick.ps1 -cp <KickAss.jar> kickass.KickAssembler -afo ...
#
# Tool paths come from tools/toolpaths.ps1 (single source of truth:
# .vscode/settings.json), matching the VS64-generated build.ninja.

$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $PSScriptRoot)   # project root (tools/..)
. "$PSScriptRoot/toolpaths.ps1"

if (-not (Test-Path $KickAssJar)) {
    throw "KickAss.jar not found at: $KickAssJar"
}

# Tee-Object keeps output streaming to the console while also capturing it.
& $JavaExe @args 2>&1 | Tee-Object -Variable output
$exit = $LASTEXITCODE

if ($exit -ne 0) {
    throw "KickAssembler exited with code $exit"
}

# .assert failures are not fatal to KickAssembler - catch them ourselves.
if (($output -join "`n") -match "FAILED!|ERROR IN ASSERTION") {
    throw "KickAssembler assertion(s) failed - build aborted"
}
