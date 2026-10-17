# publish-binaries.ps1
#
# Publish the runnable GordonOS binaries to the PUBLIC binaries repo
# (https://github.com/besimatalay/gordon-os-binaries) from the source repo.
#
# The public repo is kept as a plain local clone under the parent of this repo
# (../gordon-os-binaries by default). The script copies
# build/gordon-os.prg + reu/REU.bin into it, commits, and pushes.
#
# Usage:
#   pwsh -File tools/publish-binaries.ps1                                    (Linux/macOS)
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools/publish-binaries.ps1   (Windows)
#   ... add -Message "Release 1.0" or -SkipPush as needed.

param(
    [string]$BinDir  = "",          # local clone of the public binaries repo
    [string]$Message = "Update GordonOS binaries",
    [string]$Remote  = "https://github.com/besimatalay/gordon-os-binaries.git",
    [switch]$SkipPush               # commit but don't push (for testing)
)

$ErrorActionPreference = "Stop"

. "$PSScriptRoot/host.ps1"                        # $PowerShellExe

# F5 runs this in a terminal whose buffer is reused between runs, so an old failure stays
# scrolled above the new output and reads as the current one. Start from a clear screen:
# everything below is this run's.
try { Clear-Host } catch { }

#  Identify the copy that is running: the debug console reuses its buffer, so the first
#  line of every run names this file, its size and its timestamp.
$me = Get-Item $PSCommandPath
Write-Host "publish-binaries.ps1: $($me.Length) B, modified $($me.LastWriteTime)"

# git writes progress and warnings to stderr, and PowerShell 5.1 turns that into a
# TERMINATING error under $ErrorActionPreference = "Stop" -- so a routine warning from
# git could kill a publish with a raw error. Run git with that relaxed and let its exit
# code decide; the output still flows to the caller, so a return value can be captured.
function Invoke-Git {
    param([Parameter(ValueFromRemainingArguments = $true)][string[]]$GitArgs)
    $previous = $ErrorActionPreference
    $ErrorActionPreference = "Continue"
    try { & git @GitArgs } finally { $ErrorActionPreference = $previous }
}

$root = Split-Path -Parent $PSScriptRoot                 # repo root (tools/..)
if ($BinDir -eq "") {
    $BinDir = Join-Path (Split-Path -Parent $root) "gordon-os-binaries"
}

# --- who may publish ---------------------------------------------------------
#  The binaries repo is public and this script ships in every clone and fork.
#  Only a maintainer's clone carries a maintainer origin: a fork's origin names
#  the fork's own owner, so a publish started from one is refused here instead
#  of failing at push time. GitHub write access is the enforcement -- a fork has
#  none -- and this check states the intent before any artifact is built.
$AllowedOwners = @('besimatalay', 'GordonOS')

function Get-GitHubOwner {
    param([string]$Url)
    if ($Url -match '^https?://(?:www\.)?github\.com/(?<owner>[^/]+)/') { return $Matches['owner'] }
    if ($Url -match '^git@github\.com:(?<owner>[^/]+)/') { return $Matches['owner'] }
    return $null
}

if (-not $SkipPush) {
    Push-Location $root
    try { $originUrl = "$(Invoke-Git remote get-url origin)" } finally { Pop-Location }
    $owner = Get-GitHubOwner $originUrl
    if ($AllowedOwners -notcontains $owner) {
        throw "Refusing to publish: origin is '$originUrl', which is not a GordonOS maintainer repository (allowed owners: $($AllowedOwners -join ', ')). Use -SkipPush to commit locally without publishing."
    }
}

$prg = Join-Path $root "build/gordon-os.prg"
$reu = Join-Path $root "reu/REU.bin"
#  The same image built for a machine that runs at real time: a C64 Ultimate's
#  turbo speeds the CPU up without moving the SID's clock or the video timing,
#  so every timing in the OS has to stay at 100%. `build-reu.ps1` writes it
#  beside REU.bin in the same pass; the only difference between the two images
#  is the `speed` argument in boot.bat.
$reuC64u = Join-Path $root "reu/REU-C64U.bin"

# Publishing must never fail for want of a build: an artifact that is absent -- or that a
# build running beside this script has just unlinked -- is built here and read again.
function Get-ArtifactBytes {
    param([string]$Path, [string]$Builder)
    for ($attempt = 1; $attempt -le 3; $attempt++) {
        if (Test-Path $Path) {
            try { return ,[IO.File]::ReadAllBytes($Path) } catch { }
        }
        Write-Host "$(Split-Path -Leaf $Path) unavailable - running $Builder ..."
        & $PowerShellExe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot $Builder) | Out-Host
        if ($LASTEXITCODE -ne 0) { throw "$Builder failed (exit code $LASTEXITCODE)." }
    }
    throw "Cannot read $Path even after running $Builder."
}

#  Bytes in hand beat a path that can be unlinked: build-kernel.ps1 starts with
#  `ninja -C build -t clean`, so a kernel build running beside this script deletes the PRG
#  partway through the publish. Reading before the clone and the pull means the clone still
#  receives a complete artifact.
$prgBytes = Get-ArtifactBytes $prg "build-kernel.ps1"
$reuBytes = Get-ArtifactBytes $reu "build-reu.ps1"
$reuC64uBytes = Get-ArtifactBytes $reuC64u "build-reu.ps1"

Write-Host "publish: $((Split-Path -Leaf $prg)) $($prgBytes.Length) B, $((Split-Path -Leaf $reu)) $($reuBytes.Length) B, $((Split-Path -Leaf $reuC64u)) $($reuC64uBytes.Length) B -> $BinDir"

# --- clone if needed -------------------------------------------------------
if (-not (Test-Path (Join-Path $BinDir ".git"))) {
    Write-Host "Cloning $Remote into $BinDir ..."
    Invoke-Git clone $Remote $BinDir
    if ($LASTEXITCODE -ne 0) { throw "git clone failed." }
}

Push-Location $BinDir
try {
    # --- refresh ------------------------------------------------------------
    $hasCommits = $true
    $null = Invoke-Git rev-parse --verify HEAD 2>$null
    if ($LASTEXITCODE -ne 0) { $hasCommits = $false }

    if ($hasCommits) {
        Invoke-Git pull --ff-only --quiet
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "git pull failed - continuing with local copy; push may be rejected."
        }
    }

    # --- copy artifacts -----------------------------------------------------
    #  Bytes written from the snapshot taken above, not Copy-Item: the sources may be
    #  gone by now, and a half-copied kernel is worse than a clear failure.
    [IO.File]::WriteAllBytes((Join-Path $BinDir (Split-Path -Leaf $prg)), $prgBytes)
    [IO.File]::WriteAllBytes((Join-Path $BinDir (Split-Path -Leaf $reu)), $reuBytes)
[IO.File]::WriteAllBytes((Join-Path $BinDir (Split-Path -Leaf $reuC64u)), $reuC64uBytes)
    Copy-Item (Join-Path $root "tools/README.binaries.md") (Join-Path $BinDir "README.md") -Force
    Copy-Item (Join-Path $root "NOTICE") (Join-Path $BinDir "NOTICE") -Force
    Copy-Item (Join-Path $root "docs/LICENSE.md") (Join-Path $BinDir "LICENSE.md") -Force

    # --- the documentation ---------------------------------------------------
    #  The five application guides are the documentation that ships; the rest of docs/
    #  (the kernel reference, the memory map, the build notes, the tutorial) is developer
    #  material and stays in the source repo. A guide's link to a doc that does not ship is
    #  NEUTRALISED rather than copied -- the target stays in the text as a bare name in
    #  code font, and the copied guides are then resolved link by link, so a guide that
    #  gains a link to a developer doc cannot publish it dead.
    $guides = @('edit.md', 'gordonpaint.md', 'gortris.md', 'grknoid.md', 'gordonbasic.md')
    $docDir = Join-Path $root "docs"
    $docsOut = Join-Path $BinDir "docs"
    $utf8NoBom = New-Object System.Text.UTF8Encoding $false
    $linkRx = [regex]'\[([^\]]+)\]\(([^)]+)\)'
    $plainNames = New-Object System.Collections.Generic.List[string]

    New-Item -ItemType Directory -Force -Path $docsOut | Out-Null
    #  Replaced, not merged: a doc that leaves the shipped set must not linger here.
    Get-ChildItem -Path $docsOut -Filter "*.md" | Remove-Item -Force

    foreach ($guide in $guides) {
        $source = Join-Path $docDir $guide
        if (-not (Test-Path $source)) { throw "Missing guide: docs/$guide" }
        $text = [IO.File]::ReadAllText($source, $utf8NoBom)
        $built = New-Object System.Text.StringBuilder
        $at = 0
        foreach ($m in $linkRx.Matches($text)) {
            $target = $m.Groups[2].Value.Trim()
            #  Only a .md target can be a document: an image, NOTICE or a fragment is not
            #  touched, and a link to something else entirely is left for the check below.
            $neutralise = ($target -match '(?i)\.md$') -and ($guides -notcontains $target)
            [void]$built.Append($text.Substring($at, $m.Index - $at))
            if ($neutralise) {
                [void]$built.Append('`' + $m.Groups[1].Value + '`')
                $plainNames.Add("$guide -> $target")
            } else {
                [void]$built.Append($m.Value)
            }
            $at = $m.Index + $m.Length
        }
        [void]$built.Append($text.Substring($at))
        [IO.File]::WriteAllText((Join-Path $docsOut $guide), $built.ToString(), $utf8NoBom)
    }

    #  Nothing else travels with them: the attribution a guide needs is in `NOTICE` at the
    #  root, which is what its `../NOTICE` links resolve to. The provenance notes stay in
    #  the source repo -- and a copy left by an earlier publish must not linger here.
    Remove-Item -Recurse -Force (Join-Path $BinDir "reference") -ErrorAction SilentlyContinue
    Write-Host ("docs: {0} guides + {1} plain name(s)" -f $guides.Count, $plainNames.Count)
    $plainNames | Sort-Object -Unique | ForEach-Object { Write-Host "        $_" }

    #  The screenshots ship with the docs that embed them. They live under docs/img/
    #  because that is where BOTH spellings resolve: README.md and quickstart.binaries.md
    #  land at the repo root and address them as docs/img/<name>, while a guide under
    #  docs/ addresses them as img/<name> -- the path its source already uses. The folder
    #  is replaced, not merged, so an image removed from the source does not linger here.
    $imgOut = Join-Path $BinDir "docs/img"
    Remove-Item -Recurse -Force $imgOut -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Force -Path $imgOut | Out-Null
    Copy-Item -Path (Join-Path $root "docs/img/*") -Destination $imgOut -Force
    Copy-Item (Join-Path $root "tools/quickstart.binaries.md") (Join-Path $BinDir "quickstart.binaries.md") -Force

    # --- every published link must resolve -----------------------------------
    #  The whole published tree, not just the guides: a README or quickstart line pointing
    #  at a doc that does not ship is the same defect, and this is the only place it can be
    #  caught -- the sources live in a tree that has every file. Stops the publish before
    #  the commit, so a dead link never reaches the binaries repo.
    $broken = New-Object System.Collections.Generic.List[string]
    $published = Get-ChildItem -Path $BinDir -Recurse -File -Filter "*.md" |
                 Where-Object { $_.FullName -notmatch '[\\/]\.git[\\/]' }
    foreach ($file in $published) {
        foreach ($m in $linkRx.Matches([IO.File]::ReadAllText($file.FullName, $utf8NoBom))) {
            $target = $m.Groups[2].Value.Trim()
            if ($target -match '^(?i:https?://|mailto:|#)' -or $target -like '*<*') { continue }
            #  A relative target has to look like a path: a sentence's brackets are not one.
            if (-not (($target -match '(?i)\.[a-z0-9]{1,5}$') -or ($target -match '/'))) { continue }
            $where = $file.FullName.Substring($BinDir.Length + 1)
            if (-not (Test-Path ([IO.Path]::GetFullPath((Join-Path $file.DirectoryName ($target -replace '#.*$', '')))))) {
                $broken.Add("$where -> $target")
            }
        }
    }
    if ($broken.Count -gt 0) {
        throw ("Published text carries {0} unresolvable link(s):`n        {1}" -f $broken.Count, ($broken -join "`n        "))
    }
    Write-Host ("links: verified in {0} published .md file(s)" -f $published.Count)

    # --- commit --------------------------------------------------------------
    Invoke-Git add -A
    Invoke-Git diff --cached --quiet
    if ($LASTEXITCODE -eq 0) {
        Write-Host "No changes - nothing to publish."
    } else {
        Invoke-Git commit -m $Message --quiet
        if ($LASTEXITCODE -ne 0) { throw "git commit failed." }

        if (-not $SkipPush) {
            if ($hasCommits) {
                Invoke-Git push --quiet
            } else {
                $branch = (Invoke-Git symbolic-ref --short HEAD).Trim()
                Invoke-Git push -u origin $branch --quiet
            }
            if ($LASTEXITCODE -ne 0) { throw "git push failed." }
            Write-Host "Published to $Remote"
        } else {
            Write-Host "Committed locally (push skipped)."
        }
    }
} finally {
    Pop-Location
}
