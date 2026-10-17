# vice-test.ps1 -- boot GordonOS in VICE, drive BASIC via SendKeys, screenshot.
#
# Windows only: it drives the VICE window through System.Windows.Forms SendKeys
# and the Win32 API (user32.dll), neither of which exists on Linux/macOS. On
# those hosts use the F5 launch flows and drive VICE by hand, or its remote
# monitor.
param(
    [string]$KeysFile = ""   # optional: path to a script file of key sequences
)

$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $PSScriptRoot)   # project root (tools/..)

. "$PSScriptRoot/host.ps1"
if (-not $IsWin) {
    Write-Error "vice-test.ps1 drives the VICE window through Win32 SendKeys, which exists only on Windows."
    exit 1
}

. "$PSScriptRoot/toolpaths.ps1"
$vice = $ViceExe
if (-not (Test-Path $vice)) { throw "VICE not found: $vice" }

$reuImage = (Resolve-Path reu/REU.bin).Path

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

# --- P/Invoke for window bounds ---
$sig = @"
using System;
using System.Runtime.InteropServices;
public class Win32 {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
Add-Type -TypeDefinition $sig

function Press-Enter {
    # keydown + hold + keyup -- GordonOS's 60 Hz scan misses instant presses
    [Win32]::keybd_event(0x0D, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 200
    [Win32]::keybd_event(0x0D, 0, 2, [UIntPtr]::Zero)
}

function Send-Keys($proc, [string]$text, [int]$pauseMs = 1500) {
    $null = [Win32]::SetForegroundWindow($proc.MainWindowHandle)
    Start-Sleep -Milliseconds 400
    foreach ($ch in $text.ToCharArray()) {
        [System.Windows.Forms.SendKeys]::SendWait([string]$ch)
        Start-Sleep -Milliseconds 150   # 60 Hz keyboard scan needs slow typing
    }
    Press-Enter
    Start-Sleep -Milliseconds $pauseMs
}

function Shot([string]$name) {
    $proc = Get-Process x64sc -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $proc) { throw "x64sc gone" }
    $r = New-Object Win32+RECT
    $null = [Win32]::GetWindowRect($proc.MainWindowHandle, [ref]$r)
    $w = $r.Right - $r.Left; $h = $r.Bottom - $r.Top
    if ($w -le 0 -or $h -le 0) { throw "bad window rect" }
    $bmp = New-Object System.Drawing.Bitmap($w, $h)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.CopyFromScreen($r.Left, $r.Top, 0, 0, $bmp.Size)
    $path = Join-Path (Resolve-Path temp) "$name.png"
    $bmp.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $g.Dispose(); $bmp.Dispose()
    Write-Output "SCREENSHOT: $path"
}

# --- Launch VICE ---
$proc = Start-Process -FilePath $vice -ArgumentList @(
    "-reu", "-reusize", "16384",
    "-reuimage", "`"$reuImage`"",
    "-reuimagerw", "build/gordon-os.prg"
) -PassThru
Start-Sleep -Seconds 9          # kernel boot + shell dynload
$proc.Refresh()

# --- Drive the UI ---
Send-Keys $proc "run basic" 4500   # load EhBASIC
Shot "01-basic"

Send-Keys $proc "10 print 3" 2000
Shot "02-typed"

Send-Keys $proc "run" 4000
Shot "03-after-run"

Write-Output "DONE"
