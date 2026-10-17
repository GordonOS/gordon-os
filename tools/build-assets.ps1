# build-assets.ps1 - regenerate every generated artefact whose source is in the tree
#
# tools/build-reu.ps1 runs this before it seeds, so the F5 "fresh REU" flow always
# writes the image from the sources now on disk. Each step names what it regenerates
# from; a step whose source is absent says so and leaves the committed artefact alone,
# because that copy is then the only one there is.
#
#   charsets/*.fnt        <- the chargen ROMs in charsets/    tools/seed-font.ps1
#   assets/sprites/*.spr  <- the ASCII art *.txt beside them  tools/gen-sprites.py
#   assets/ui/gpui.bin    <- assets/ui/gpui.txt               tools/gen-ui.py
#   assets/pics/*.pic     <- assets/pics/*-src.png            tools/img2pic.py
#   src/corpus/*.bas      <- the program text inside gen-corpus.py
#
# NOT regenerated, because the tree holds no source for them: music/*.mus (written
# from a score or a MIDI that is not committed) and src/banners/*.bnr (written from an
# image that is not committed). Their committed files are what ships, and they are
# listed here so the gap is visible rather than silent.
#
# A generator that RUNS and fails means the source is wrong, and that stops the build:
# a malformed picture must not reach an image. A missing interpreter -- or Pillow,
# which only img2pic.py and img2bnr.py want -- does not stop it; the committed
# artefact is used instead and the step reports the skip.
#
# The source image for a picture names its own conversion, and the markers are not
# part of the picture's name: 'mc' is multicolour, 'dither' error-diffuses the samples
# (with an optional two-digit strength, as in '-dither40-'), and anything else is
# hires with flat matching. So 'alf-mc-dither40-src.png' writes assets/pics/alf.pic,
# multicolour and diffused at 40%, while 'mc-src.png' writes mc.pic, flat.
#
#   pwsh -NoProfile -File tools/build-assets.ps1                              (Linux/macOS)
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/build-assets.ps1 (Windows)
#
# Exit 0 = every generator with a source ran clean. Exit 1 = one of them failed.

$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $PSScriptRoot)   # project root (tools/..)

. "$PSScriptRoot/host.ps1"                        # $PythonExe

# The repo .venv is where Pillow lives (docs/development.md); the interpreter
# itself (venv first, then the platform's usual names) comes from host.ps1.
$py = $PythonExe
$havePy = ($py -ne '')
$havePil = $false
if ($havePy) {
    & $py -c "import PIL" 2>$null | Out-Null
    $havePil = ($LASTEXITCODE -eq 0)
}

$failed = @()
$done = @()

# --- charsets: the two chargen ROMs -> gordon.fnt, c64uppr.fnt, c64low.fnt ---
if (Test-Path "charsets/chargen-901225-01.bin") {
    try {
        & "$PSScriptRoot/seed-font.ps1" | Out-Null
        $done += "charsets/*.fnt"
    } catch {
        $failed += "seed-font.ps1 (charsets/*.fnt): $($_.Exception.Message)"
    }
} else {
    Write-Host "  charsets: no chargen ROMs in charsets/ - the committed .fnt files ship"
}

# --- sprite sheets: assets/sprites/*.txt -> the .spr beside each ---
if (-not $havePy) {
    Write-Host "  sprites, UI band, corpus, pictures: no Python interpreter - committed files ship"
} else {
    if (@(Get-ChildItem "assets/sprites/*.txt" -ErrorAction SilentlyContinue).Count -gt 0) {
        & $py "tools/gen-sprites.py"
        if ($LASTEXITCODE -eq 0) { $done += "assets/sprites/*.spr" }
        else { $failed += "gen-sprites.py (assets/sprites/*.spr)" }
    }

    # --- the UI band, one file per mode: assets/ui/gpui.txt -> gpui.bin, gpui_mc.bin ---
    #  The same fourteen pictures make both: the MODE tile's two faces are the last two,
    #  and each band takes the one that names the mode a press would go to.
    if (Test-Path "assets/ui/gpui.txt") {
        & $py "tools/gen-ui.py" --mode std
        if ($LASTEXITCODE -eq 0) { $done += "assets/ui/gpui.bin" }
        else { $failed += "gen-ui.py --mode std (assets/ui/gpui.bin)" }
        & $py "tools/gen-ui.py" --mode mc
        if ($LASTEXITCODE -eq 0) { $done += "assets/ui/gpui_mc.bin" }
        else { $failed += "gen-ui.py --mode mc (assets/ui/gpui_mc.bin)" }
    }

    # --- the BASIC corpus: the program text inside gen-corpus.py ---
    & $py "tools/gen-corpus.py"
    if ($LASTEXITCODE -eq 0) { $done += "src/corpus/*.bas" }
    else { $failed += "gen-corpus.py (src/corpus/*.bas)" }

    # --- pictures: assets/pics/*-src.png -> the .pic beside each ---
    $picSrcs = @(Get-ChildItem "assets/pics/*-src.png" -ErrorAction SilentlyContinue | Sort-Object Name)
    if ($picSrcs.Count -eq 0) {
        Write-Host "  pictures: no assets/pics/*-src.png - the committed .pic files ship"
    } elseif (-not $havePil) {
        Write-Host "  pictures: Pillow is not installed ($py) - the committed .pic files ship"
    } else {
        foreach ($src in $picSrcs) {
            $name = $src.BaseName -replace '-src$', ''
            $marks = $name -split '-'
            $mode = if ($marks -contains "mc") { "mc" } else { "hires" }
            $mark = @($marks | Where-Object { $_ -like "dither*" })[0]
            $base = @($marks | Where-Object { $_ -ne "mc" -and $_ -notlike "dither*" }) -join "-"
            if (-not $base) { $base = $name }     # 'mc-src.png' is named for its mode alone
            $picArgs = @("--mode", $mode)
            if ($mark) {
                $amount = if ($mark -match '^dither(\d+)$') { [double]$Matches[1] / 100 } else { 1.0 }
                $picArgs += @("--dither", $amount)
            }
            $out = Join-Path $src.DirectoryName "$base.pic"
            & $py "tools/img2pic.py" $src.FullName $out @picArgs
            if ($LASTEXITCODE -eq 0) { $done += "assets/pics/$base.pic" }
            else { $failed += "img2pic.py ($($src.Name))" }
        }
    }
}

Write-Host "  music/*.mus and src/banners/*.bnr: no source in the tree - committed files ship"

if ($failed.Count -gt 0) {
    foreach ($f in $failed) { Write-Host "  FAILED: $f" }
    exit 1
}
if ($done.Count -gt 0) { Write-Host "  regenerated: $($done -join ', ')" }
exit 0
