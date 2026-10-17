# vice-step.ps1 -- stepwise VICE driver: launch or send verified keys.
# Usage: vice-step.ps1 launch | keys "text" | enter
#
# Windows only: it drives the VICE window through System.Windows.Forms SendKeys
# and the Win32 API (user32.dll), neither of which exists on Linux/macOS. On
# those hosts use the F5 launch flows and drive VICE by hand, or its remote
# monitor.
param(
    [Parameter(Position=0)][string]$Action = "keys",
    [Parameter(Position=1)][string]$Text = "",
    [Parameter(Position=2)][string]$Arg2 = ""
)

$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $PSScriptRoot)   # project root (tools/..)

. "$PSScriptRoot/host.ps1"
if (-not $IsWin) {
    Write-Error "vice-step.ps1 drives the VICE window through Win32 SendKeys, which exists only on Windows."
    exit 1
}

. "$PSScriptRoot/toolpaths.ps1"
$vice = $ViceExe

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$sig = @"
using System;
using System.Runtime.InteropServices;
public class Win32 {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern void mouse_event(uint dwFlags, int dx, int dy, uint dwData, UIntPtr extra);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
Add-Type -TypeDefinition $sig

function Get-ViceProc {
    Get-Process x64sc -ErrorAction SilentlyContinue | Select-Object -First 1
}

function Send-Enter($proc) {
    $null = [Win32]::SetForegroundWindow($proc.MainWindowHandle)
    Start-Sleep -Milliseconds 500
    [Win32]::keybd_event(0x0D, 0, 0, [UIntPtr]::Zero)      # VK_RETURN down
    Start-Sleep -Milliseconds 200                           # hold it
    [Win32]::keybd_event(0x0D, 0, 2, [UIntPtr]::Zero)      # KEYUP
    Start-Sleep -Milliseconds 1200
}

function Send-Text($proc, [string]$text) {
    $null = [Win32]::SetForegroundWindow($proc.MainWindowHandle)
    Start-Sleep -Milliseconds 500
    foreach ($ch in $text.ToCharArray()) {
        $vk = 0
        $shift = $false
        $c = [int][char]$ch
        if ($c -ge 97 -and $c -le 122) { $vk = 0x41 + ($c - 97) }   # a-z
        elseif ($c -ge 48 -and $c -le 57) { $vk = 0x30 + ($c - 48) } # 0-9
        elseif ($ch -eq ' ') { $vk = 0x20 }
        elseif ($ch -eq '=') { $vk = 0xBB }                          # OEM_PLUS
        elseif ($ch -eq ';') { $vk = 0xBA; $shift = $true }          # OEM_1 shifted
        elseif ($ch -eq ':') { $vk = 0xBA; $shift = $true }          # OEM_1 shifted
        elseif ($ch -eq '+') { $vk = 0xBB; $shift = $true }          # OEM_PLUS shifted
        elseif ($ch -eq ',') { $vk = 0xBC }                          # OEM_COMMA
        elseif ($ch -eq '.') { $vk = 0xBE }                          # OEM_PERIOD
        elseif ($ch -eq '"') { $vk = 0xDE; $shift = $true }          # OEM_7 shifted
        elseif ($ch -eq '$') { $vk = 0x34; $shift = $true }          # 4 shifted
        elseif ($ch -eq '(') { $vk = 0x39; $shift = $true }          # 9 shifted
        elseif ($ch -eq ')') { $vk = 0x30; $shift = $true }          # 0 shifted
        else { throw "unsupported char: $ch" }
        Write-Output ("KEY: {0} -> vk=0x{1:X2} shift={2}" -f $ch, $vk, $shift)
        if ($shift) { [Win32]::keybd_event(0x10, 0, 0, [UIntPtr]::Zero) }
        [Win32]::keybd_event([byte]$vk, 0, 0, [UIntPtr]::Zero)      # key down
        Start-Sleep -Milliseconds 200                                # hold >=3 IRQ frames
        [Win32]::keybd_event([byte]$vk, 0, 2, [UIntPtr]::Zero)      # key up
        if ($shift) { [Win32]::keybd_event(0x10, 0, 2, [UIntPtr]::Zero) }
        Start-Sleep -Milliseconds 250
    }
    Start-Sleep -Milliseconds 800
}

function Shot([string]$name) {
    $proc = Get-ViceProc
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

switch ($Action) {
    "launch" {
        $existing = Get-ViceProc
        if ($existing) { Write-Output "ALREADY RUNNING pid=$($existing.Id)"; break }
        $reuImage = (Resolve-Path reu\REU.bin).Path
        Start-Process -FilePath $vice -ArgumentList @(
            "-reu", "-reusize", "16384",
            "-reuimage", "`"$reuImage`"",
            "-reuimagerw", "build/gordon-os.prg"
        ) | Out-Null
        Start-Sleep -Seconds 12
        Shot "00-boot"
    }
    "keys" {
        $proc = Get-ViceProc
        if (-not $proc) { throw "x64sc not running" }
        Send-Text $proc $Text
        Shot "typed"
    }
    "keys64" {
        $proc = Get-ViceProc
        if (-not $proc) { throw "x64sc not running" }
        $decoded = [System.Text.Encoding]::UTF8.GetString([Convert]::FromBase64String($Text))
        Send-Text $proc $decoded
        Shot "typed"
    }
    "enter" {
        $proc = Get-ViceProc
        if (-not $proc) { throw "x64sc not running" }
        Send-Enter $proc
        Shot "after-enter"
    }
    "shot" {
        Shot "manual"
    }
    "menu" {
        $proc = Get-ViceProc
        if (-not $proc) { throw "x64sc not running" }
        $null = [Win32]::SetForegroundWindow($proc.MainWindowHandle)
        Start-Sleep -Milliseconds 300
        [Win32]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)   # Alt down
        [Win32]::keybd_event(0x46, 0, 0, [UIntPtr]::Zero)   # F down
        Start-Sleep -Milliseconds 120
        [Win32]::keybd_event(0x46, 0, 2, [UIntPtr]::Zero)
        [Win32]::keybd_event(0x12, 0, 2, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 900
        Shot "menu"
    }
    "click" {
        # args: click <dx> <dy> -- mouse click at (dx,dy) relative to window top-left
        $proc = Get-ViceProc
        if (-not $proc) { throw "x64sc not running" }
        $r = New-Object Win32+RECT
        $null = [Win32]::GetWindowRect($proc.MainWindowHandle, [ref]$r)
        $null = [Win32]::SetForegroundWindow($proc.MainWindowHandle)
        Start-Sleep -Milliseconds 200
        $x = $r.Left + [int]$Text
        $y = $r.Top + [int]$Arg2
        [System.Windows.Forms.Cursor]::Position = New-Object System.Drawing.Point($x, $y)
        Start-Sleep -Milliseconds 150
        [Win32]::mouse_event(0x02, 0, 0, 0, [UIntPtr]::Zero)  # left down
        Start-Sleep -Milliseconds 80
        [Win32]::mouse_event(0x04, 0, 0, 0, [UIntPtr]::Zero)  # left up
        Start-Sleep -Milliseconds 800
        Shot "after-click"
    }
    default { throw "unknown action" }
}
