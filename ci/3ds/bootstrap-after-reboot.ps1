param(
    [string]$SourceDir = "",
    [string]$Repo = "",
    [string]$Upstream = "hrydgard/ppsspp",
    [string]$Branch = "master",
    [switch]$StartRunner,
    [switch]$DispatchSelfHostedBuild,
    [switch]$SkipClone
)

$ErrorActionPreference = "Stop"

function Require-Command($Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Missing required command: $Name"
    }
}

function Copy-OverlayFile($FromRoot, $ToRoot, $RelativePath) {
    $src = Join-Path $FromRoot $RelativePath
    $dst = Join-Path $ToRoot $RelativePath
    if (-not (Test-Path $src)) {
        throw "Overlay source missing: $src"
    }
    $dstDir = Split-Path $dst -Parent
    New-Item -ItemType Directory -Force -Path $dstDir | Out-Null
    Copy-Item -Force -Path $src -Destination $dst
}

Require-Command gh
Require-Command git
Require-Command docker

$overlayRoot = (Resolve-Path (Join-Path $PSScriptRoot "../..")).Path
if ([string]::IsNullOrWhiteSpace($SourceDir)) {
    $SourceDir = Join-Path $overlayRoot "_ppsspp-source"
}

Write-Host "Overlay root: $overlayRoot"
Write-Host "Source dir:   $SourceDir"

gh auth status | Out-Host

if (-not $SkipClone) {
    if (-not (Test-Path (Join-Path $SourceDir ".git"))) {
        gh repo clone $Upstream $SourceDir -- --recursive
    } else {
        git -C $SourceDir fetch --all --prune
        git -C $SourceDir submodule update --init --recursive
    }

    git -C $SourceDir checkout $Branch
}

if (-not (Test-Path (Join-Path $SourceDir "CMakeLists.txt"))) {
    throw "$SourceDir does not look like a PPSSPP source checkout."
}

$overlayFiles = @(
    ".dockerignore",
    ".github/workflows/build-3ds.yml",
    "ci/3ds/README.md",
    "ci/3ds/build-3ds.sh",
    "ci/3ds/Dockerfile.runner",
    "ci/3ds/docker-compose.runner.yml",
    "ci/3ds/runner-entrypoint.sh",
    "ci/3ds/setup-github-ci.ps1",
    "cmake/Toolchain-devkitARM-3DS.cmake"
)

foreach ($file in $overlayFiles) {
    Copy-OverlayFile -FromRoot $overlayRoot -ToRoot $SourceDir -RelativePath $file
}

Write-Host "Copied 3DS CI overlay into PPSSPP source checkout."

docker compose -f (Join-Path $SourceDir "ci/3ds/docker-compose.runner.yml") config | Out-Host

if (-not [string]::IsNullOrWhiteSpace($Repo)) {
    Push-Location $SourceDir
    try {
        pwsh ci/3ds/setup-github-ci.ps1 -Repo $Repo -RunnerName "ppsspp-3ds-runner" -RunnerLabels "3ds,devkitpro,devkitarm" -StartRunner:$StartRunner -DispatchSelfHostedBuild:$DispatchSelfHostedBuild
    } finally {
        Pop-Location
    }
} else {
    Write-Host "No -Repo supplied, so runner registration and workflow dispatch were skipped."
    Write-Host "Next: cd $SourceDir"
    Write-Host "Then commit/push the copied files and run: pwsh ci/3ds/setup-github-ci.ps1 -Repo OWNER/REPO -StartRunner"
}
