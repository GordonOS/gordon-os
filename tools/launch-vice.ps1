# launch-vice.ps1 - launch VICE with the GordonOS REU image.
#
# Driven by the F5 dropdown (tasks.json input "reuMode"):
#   "Use existing REU"      -> keep reu/REU.bin (saved files survive);
#                              built fresh if missing. reu/REU-C64U.bin is
#                              refreshed either way: VICE opens only the first,
#                              so rebuilding the second cannot cost a session.
#   "Scratch + rebuild REU" -> rebuild both images via tools/build-reu.ps1
#                              (fresh filesystem with all task binaries).
#                              Refuses to launch if the image could not be
#                              replaced -- a running VICE maps it.
#
# Run from anywhere; it changes to the project root first.
#
#  -Mouse1351 : launch with a Commodore 1351 in control port 2 AND mouse
#               emulation (pointer grab) on -- what the mouse needs to work at
#               all. VICE's control-port device alone is not enough: the `Mouse`
#               resource is what feeds the host pointer into the emulated
#               machine, so without it the POT bytes stay frozen and the buttons
#               read released (see docs/lib-mouse.md). Off by default, because
#               grabbing the pointer and taking port 2 away from the joystick is
#               not what every other task wants.

param([string]$Mode = "Use existing REU", [switch]$Mouse1351)

$ErrorActionPreference = "Stop"
Set-Location (Split-Path -Parent $PSScriptRoot)   # project root (tools/..)

. "$PSScriptRoot/toolpaths.ps1"                   # machine paths: $ViceExe
if (-not (Test-Path $ViceExe)) {
    Write-Error "VICE not found at: $ViceExe - edit .vscode/settings.json (vs64.viceExecutable)"
    exit 1
}

$vice = $ViceExe
$reuImage = Join-Path (Get-Location) "reu/REU.bin"

# Machine-specific VICE flags from .vscode/settings.json (vs64.viceArgs), e.g. a
# -directory search path when the C64 ROMs are not where VICE looks by default.
$viceArgList = @($ViceArgs -split '\s+' | Where-Object { $_ -ne '' })

#  A 1351 in control port 2 with mouse emulation on: "1351" is one of the names
#  VICE's own control-port option accepts, and -mouse is the `Mouse` resource
#  ("Enable mouse grab"). Mouse devices must also be attached before the machine
#  starts -- VICE's own note in src/joyport/mouse_1351.c is that attaching one
#  after it has started needs a reset.
$mouseArgs = @()
if ($Mouse1351) { $mouseArgs = @("-controlport2device", "1351", "-mouse") }

#  -speed 200 = run the emulator at 200% of real time. Keep this in step with the
#  two `args` lines in .vscode\launch.json (the F5 launch flows), and see
#  docs\quick-start.md: the kernel's cursor blink and keyboard repeat defaults are
#  tuned for THIS speed, so a different one needs `blink`/`keyrpt` adjusted to match.
$ViceSpeed = 200

if ($Mode -like "*Scratch*") {
    & "$PSScriptRoot/build-reu.ps1"
    if ($LASTEXITCODE -ne 0) {
        # A running VICE maps the image, so the fresh filesystem could not be
        # written. Launching anyway would boot the OLD one under a "fresh REU"
        # label -- a stale image that looks current is the one outcome worse
        # than not launching.
        Write-Error "the REU image was not rebuilt, so this launch would boot the old one. Close VICE and run this flow again."
        exit 1
    }
    Write-Host "Scratch: rebuilt a fresh filesystem."
} else {
    if (Test-Path $reuImage) {
        Write-Host "Using existing REU image."
        #  The C64 Ultimate image is an artefact of the build, not of this
        #  session: VICE opens only $reuImage, so nothing running here can have
        #  changed it, and writing it costs neither saved files nor the live
        #  filesystem. Every flow keeps it current, so no launch can leave it
        #  behind.
        & "$PSScriptRoot/build-reu.ps1" -Speed 100 -Out "reu/REU-C64U.bin"
        if ($LASTEXITCODE -ne 0) { Write-Warning "reu/REU-C64U.bin was not refreshed - run the Build REU task" }
    } else {
        & "$PSScriptRoot/build-reu.ps1"
        Write-Host "No REU image found - built a fresh filesystem."
    }
}

& $vice @viceArgList -speed $ViceSpeed -reu -reusize 16384 -reuimage $reuImage -reuimagerw @mouseArgs build/gordon-os.prg
