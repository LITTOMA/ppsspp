param(
    [string]$Image = "pspdev/pspdev:v20260301",
    [string]$OutDir = "_psp-baseline-artifacts"
)

$ErrorActionPreference = "Stop"

$repoRoot = Resolve-Path (Join-Path $PSScriptRoot "..\..")
$projectDir = Join-Path $repoRoot "tools\psp-baseline"
$outRoot = Join-Path $repoRoot $OutDir
$gameDir = Join-Path $outRoot "PSP\GAME\PPSSPP3DSBASE"

New-Item -ItemType Directory -Force -Path $gameDir | Out-Null

docker run --rm --entrypoint /bin/bash `
    -v "${projectDir}:/src" `
    -w /src `
    $Image `
    -lc 'export PSPDEV=/usr/local/pspdev; export PSPSDK=$PSPDEV/psp/sdk; export PATH=$PATH:$PSPDEV/bin:$PSPSDK/bin; make clean; make'

Copy-Item -Force (Join-Path $projectDir "EBOOT.PBP") (Join-Path $gameDir "EBOOT.PBP")
Copy-Item -Force (Join-Path $projectDir "README.md") (Join-Path $outRoot "README.md")

Get-FileHash -Algorithm SHA256 (Join-Path $gameDir "EBOOT.PBP")
Write-Host "Baseline EBOOT ready: $gameDir\EBOOT.PBP"
