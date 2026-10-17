# toolpaths.ps1 - tool paths (single source of truth: .vscode/settings.json).
#
# Dot-sourced by tools/launch-vice.ps1 and tools/build-tasks.ps1.
# Paths come from the VS64 settings in .vscode/settings.json; env vars
# VICE_EXE / KICKASS_JAR / JAVA_EXE / NINJA_EXE / VICE_ARGS still override for ad-hoc runs.
#
# .vscode/settings.json is GITIGNORED (the paths are machine-specific), so a
# fresh clone does not have one -- it must be created from the bundled sample:
#     cp .vscode/settings.json.sample .vscode/settings.json           (Linux/macOS)
#     Copy-Item .vscode\settings.json.sample .vscode\settings.json    (Windows)
# then edit the four vs64.* paths. Without it every path below comes out empty
# and the build stops with "KickAss.jar not found at: " (empty), which is the
# confusing part -- see docs/quick-start.md -> "Option 2: Build from source".

$settingsJson = Join-Path $PSScriptRoot "../.vscode/settings.json"
$vs64 = $null
if (Test-Path $settingsJson) {
    $raw = Get-Content $settingsJson -Raw
    $raw = $raw -replace '(?m)//[^\r\n]*', ''      # strip // comments (JSON5)
    $vs64 = $raw | ConvertFrom-Json
} else {
    Write-Warning (".vscode/settings.json not found - tool paths are unset. " +
                   "Create it from the bundled sample and edit the four vs64.* paths:`n" +
                   "    cp .vscode/settings.json.sample .vscode/settings.json          (Linux/macOS)`n" +
                   "    Copy-Item .vscode\settings.json.sample .vscode\settings.json   (Windows)`n" +
                   "See docs/quick-start.md -> 'Option 2: Build from source'.")
}

$ViceExe = if ($env:VICE_EXE) { $env:VICE_EXE }
           elseif ($vs64.'vs64.viceExecutable') { $vs64.'vs64.viceExecutable' }
           else { "" }

$KickAssJar = if ($env:KICKASS_JAR) { $env:KICKASS_JAR }
              elseif ($vs64.'vs64.kickInstallDir') { Join-Path $vs64.'vs64.kickInstallDir' "KickAss.jar" }
              else { "" }

$JavaExe = if ($env:JAVA_EXE) { $env:JAVA_EXE }
           elseif ($vs64.'vs64.javaExecutable') { $vs64.'vs64.javaExecutable' }
           else { "java" }

# Extra VICE args (e.g. "-directory <path>" when the C64 ROMs are not where VICE
# looks by default).
$ViceArgs = if ($env:VICE_ARGS) { $env:VICE_ARGS }
            elseif ($vs64.'vs64.viceArgs') { $vs64.'vs64.viceArgs' }
            else { "" }

# Ninja: the setting, else the Ninja bundled inside the VS64 extension
# (resources/ninja/<platform>/), else the ninja on PATH.
if (-not (Test-Path Variable:IsWin)) { . "$PSScriptRoot/host.ps1" }   # $IsWin, $IsMac
$NinjaExe = if ($env:NINJA_EXE) { $env:NINJA_EXE }
            elseif ($vs64.'vs64.ninjaExecutable') { $vs64.'vs64.ninjaExecutable' }
            else { "" }
if ($NinjaExe -eq "") {
    $ninjaPlat = if ($IsWin) { 'win' } elseif ($IsMac) { 'mac' } else { 'linux' }
    $ninjaRel  = if ($IsWin) { "resources/ninja/$ninjaPlat/ninja.exe" } else { "resources/ninja/$ninjaPlat/ninja" }
    $extRoot   = Join-Path $HOME '.vscode/extensions'
    if (Test-Path $extRoot) {
        $NinjaExe = @(Get-ChildItem -Path $extRoot -Filter 'rosc.vs64-*' -Directory -ErrorAction SilentlyContinue |
            Sort-Object Name |
            ForEach-Object { Join-Path $_.FullName $ninjaRel } |
            Where-Object { Test-Path $_ } | Select-Object -Last 1)[0]
    }
    if (-not $NinjaExe) { $NinjaExe = "ninja" }
}

