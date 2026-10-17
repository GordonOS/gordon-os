# build-kernel.ps1 - rebuild the kernel through Ninja with assert-safe KickAssembler.
#
# VS64 generates build/build.ninja and may restore the raw Java/KickAssembler
# command. Patch the generated asm rule before running Ninja so failed .asserts
# become a non-zero build result via tools/kick.ps1.

$ErrorActionPreference = "Stop"

Set-Location (Split-Path -Parent $PSScriptRoot)   # project root (tools/..)

. "$PSScriptRoot/host.ps1"                        # $PowerShellExe, $PathSep
. "$PSScriptRoot/toolpaths.ps1"                   # machine paths: $NinjaExe

# Tripwire for the 24-bit REU FS offset (reuFsPtr): a copy/stash that drops the
# bank byte cannot be caught by any .assert. See the script header.
& "$PSScriptRoot/check-fs-offsets.ps1"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Tripwire for the library-id / descriptor-table coupling: the LIB_* ids in
# task-header.inc and the positional lib tables in src/main.asm must agree.
& "$PSScriptRoot/check-lib-ids.ps1"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

# Tripwire for header field offsets: a task/library header field read as a bare
# `+N` literal on scratchBuf/reuDirBuf cannot be caught by any .assert (it is a
# byte index into a DMA'd page, not a label), and it survives review because the
# edit that breaks it usually also rewrites the comment. See the script header.
& "$PSScriptRoot/check-header-offsets.ps1"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$ninjaFile = Join-Path (Get-Location) "build/build.ninja"
if (-not (Test-Path $ninjaFile)) {
    throw "Missing build/build.ninja - run VS64 project generation once before building"
}

if (-not (Get-Command $NinjaExe -ErrorAction SilentlyContinue) -and -not (Test-Path $NinjaExe)) {
    throw "Ninja not found ('$NinjaExe') - set vs64.ninjaExecutable in .vscode/settings.json, or put ninja on PATH"
}

# The generated asm rule calls Java directly; re-point it through kick.ps1 so a
# failed .assert becomes a non-zero build result. The command must name THIS
# host's PowerShell ("powershell.exe" on Windows, "pwsh" on Linux/macOS) and the
# path separator the running Ninja understands.
$directLine = '    command = $java_exe -jar $asm_exe -odir "$builddir" -asminfofile "$asminfo" $flags $includes -o $out $in'
$kickRel = '$basedir' + $PathSep + 'tools' + $PathSep + 'kick.ps1'
$wrappedLine = '    command = "' + $PowerShellExe + '" -NoProfile -ExecutionPolicy Bypass -File "' + $kickRel + '" -jar $asm_exe -odir "$builddir" -asminfofile "$asminfo" $flags $includes -o $out $in'

$text = [IO.File]::ReadAllText($ninjaFile)
if ($text.Contains($directLine)) {
    $text = $text.Replace($directLine, $wrappedLine)
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [IO.File]::WriteAllText($ninjaFile, $text, $utf8NoBom)
} elseif (-not $text.Contains($wrappedLine)) {
    throw "build/build.ninja asm rule is not recognized; update tools/build-kernel.ps1"
}

& $NinjaExe -C build -t clean
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

& $NinjaExe -C build
exit $LASTEXITCODE