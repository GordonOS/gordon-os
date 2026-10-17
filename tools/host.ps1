# host.ps1 - the host facts that let one script set run on Windows and on Unix.
#
# The build and launch scripts run under Windows PowerShell 5.1 on Windows and
# under PowerShell 7 ("pwsh") on Linux/macOS. A script cannot name a PowerShell
# executable, a Python interpreter or a path separator directly and still start
# on both, so this file resolves all three once. Dot-source it from the top of
# any script that needs them:
#
#     . "$PSScriptRoot/host.ps1"
#
# Afterwards it has set:
#   $IsWin          $true on Windows, $false on Linux/macOS
#   $IsMac          $true on macOS, $false otherwise
#   $PowerShellExe  absolute path to the host's own PowerShell executable
#   $PythonExe      path to a Python 3 interpreter, or "" when none is found
#   $PathSep        the platform's directory separator

$IsWin = $true
if (Test-Path Variable:IsWindows) { $IsWin = $IsWindows }   # $IsWindows exists on PS 6+ only
$IsMac = $false
if (Test-Path Variable:IsMacOS) { $IsMac = $IsMacOS }       # $IsMacOS exists on PS 6+ only

$PathSep = [IO.Path]::DirectorySeparatorChar

# The host's own executable: "powershell.exe" under 5.1, "pwsh"/"pwsh.exe"
# under PowerShell 7. Resolving it from $PSHOME keeps a child PowerShell the
# same flavour as the parent, rather than guessing a name off PATH.
if ($PSVersionTable.PSEdition -eq 'Core') {
    $PowerShellExe = Join-Path $PSHOME $(if ($IsWin) { 'pwsh.exe' } else { 'pwsh' })
} else {
    $PowerShellExe = Join-Path $PSHOME 'powershell.exe'
}

# A Python 3 interpreter, most specific first: the repo venv (either layout),
# then the platform's usual installation names.
$HostProjectRoot = Split-Path -Parent $PSScriptRoot
$HostPythonNames = if ($IsWin) { @('python', 'python3') } else { @('python3', 'python') }
$PythonExe = ''
foreach ($HostCandidate in @(
        (Join-Path $HostProjectRoot '.venv/bin/python'),
        (Join-Path $HostProjectRoot '.venv/Scripts/python.exe')) + $HostPythonNames) {
    if (Test-Path $HostCandidate) { $PythonExe = (Resolve-Path $HostCandidate).Path; break }
    $HostFound = Get-Command $HostCandidate -ErrorAction SilentlyContinue
    if ($HostFound) { $PythonExe = $HostFound.Path; break }
}
