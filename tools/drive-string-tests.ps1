# drive-string-tests.ps1 -- run the string-domain regression suite in VICE.
#
# Windows only: it drives the VICE window through System.Windows.Forms SendKeys
# and the Win32 API (user32.dll), neither of which exists on Linux/macOS.
$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $PSScriptRoot)

. "$PSScriptRoot/host.ps1"
if (-not $IsWin) {
    Write-Error "drive-string-tests.ps1 drives the VICE window through Win32 SendKeys, which exists only on Windows."
    exit 1
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing

$sig = @"
using System;
using System.Runtime.InteropServices;
public class Win32D2 {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern void keybd_event(byte vk, byte scan, uint flags, UIntPtr extra);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
}
"@
Add-Type -TypeDefinition $sig

function Get-ViceProc { Get-Process x64sc -ErrorAction SilentlyContinue | Select-Object -First 1 }

function Send-Keys([string]$text) {
    $proc = Get-ViceProc
    if (-not $proc) { throw "x64sc not running" }
    $null = [Win32D2]::SetForegroundWindow($proc.MainWindowHandle)
    Start-Sleep -Milliseconds 400
    foreach ($ch in $text.ToCharArray()) {
        $vk = 0; $shift = $false; $c = [int][char]$ch
        if ($c -ge 97 -and $c -le 122) { $vk = 0x41 + ($c - 97) }
        elseif ($c -ge 48 -and $c -le 57) { $vk = 0x30 + ($c - 48) }
        elseif ($ch -eq ' ') { $vk = 0x20 }
        elseif ($ch -eq '=') { $vk = 0xBB }
        elseif ($ch -eq '+') { $vk = 0xBB; $shift = $true }
        elseif ($ch -eq '"') { $vk = 0xDE; $shift = $true }
        elseif ($ch -eq '$') { $vk = 0x34; $shift = $true }
        elseif ($ch -eq '(') { $vk = 0x39; $shift = $true }
        elseif ($ch -eq ')') { $vk = 0x30; $shift = $true }
        elseif ($ch -eq ',') { $vk = 0xBC }
        else { throw "unsupported char: $ch" }
        if ($shift) { [Win32D2]::keybd_event(0x10, 0, 0, [UIntPtr]::Zero) }
        [Win32D2]::keybd_event([byte]$vk, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 200
        [Win32D2]::keybd_event([byte]$vk, 0, 2, [UIntPtr]::Zero)
        if ($shift) { [Win32D2]::keybd_event(0x10, 0, 2, [UIntPtr]::Zero) }
        Start-Sleep -Milliseconds 250
    }
    Start-Sleep -Milliseconds 400
}

function Send-EnterKey {
    $proc = Get-ViceProc
    $null = [Win32D2]::SetForegroundWindow($proc.MainWindowHandle)
    Start-Sleep -Milliseconds 300
    [Win32D2]::keybd_event(0x0D, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 200
    [Win32D2]::keybd_event(0x0D, 0, 2, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 1200
}

function Snap([string]$name) {
    $proc = Get-ViceProc
    if (-not $proc) { throw "x64sc gone" }
    $r = New-Object Win32D2+RECT
    $null = [Win32D2]::GetWindowRect($proc.MainWindowHandle, [ref]$r)
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

function Do-Line([string]$s) { Send-Keys $s; Send-EnterKey }

# 1. start basic
Send-Keys "run basic"; Send-EnterKey
Start-Sleep -Seconds 5
Snap "01-basic"

# 2. numeric print (ZP-stack descriptor via lab_rtst)
Do-Line "print 3+4"
# 3. string literal print
Do-Line ([System.Text.Encoding]::UTF8.GetString([Convert]::FromBase64String("cHJpbnQgImhlbGxvIg==")))
# 4. assign a$ (LET path: literal -> var)
Do-Line ([System.Text.Encoding]::UTF8.GetString([Convert]::FromBase64String("YSQ9ImRkZGQi")))
# 5. print a$ (REU var descriptor)
Do-Line ([System.Text.Encoding]::UTF8.GetString([Convert]::FromBase64String("cHJpbnQgYSQ=")))
# 6. len(a$)
Do-Line ([System.Text.Encoding]::UTF8.GetString([Convert]::FromBase64String("cHJpbnQgbGVuKGEkKQ==")))
# 7. concat a$+a$
Do-Line ([System.Text.Encoding]::UTF8.GetString([Convert]::FromBase64String("cHJpbnQgYSQrYSQ=")))
# 8. left$(a$,2)
Do-Line ([System.Text.Encoding]::UTF8.GetString([Convert]::FromBase64String("cHJpbnQgbGVmdCQoYSQsMik=")))
# 9. right$(a$,2)
Do-Line ([System.Text.Encoding]::UTF8.GetString([Convert]::FromBase64String("cHJpbnQgcmlnaHQkKGEkLDIp")))
# 10. mid$(a$,2,2)
Do-Line ([System.Text.Encoding]::UTF8.GetString([Convert]::FromBase64String("cHJpbnQgbWlkJChhJCwyLDIp")))
# 11. b$=a$ (var -> var)
Do-Line ([System.Text.Encoding]::UTF8.GetString([Convert]::FromBase64String("YiQ9YSQ=")))
# 12. print b$
Do-Line ([System.Text.Encoding]::UTF8.GetString([Convert]::FromBase64String("cHJpbnQgYiQ=")))
# 13. compare print a$="dddd"
Do-Line ([System.Text.Encoding]::UTF8.GetString([Convert]::FromBase64String("cHJpbnQgYSQ9ImRkZGQi")))
# 14. if a$="dddd" then print "yes"
Do-Line ([System.Text.Encoding]::UTF8.GetString([Convert]::FromBase64String("aWYgYSQ9ImRkZGQiIHRoZW4gcHJpbnQgInllcyI=")))

Snap "02-string-tests"
Write-Output "DONE"
