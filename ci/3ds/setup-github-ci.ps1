param(
    [string]$Repo = "",
    [string]$RunnerName = "ppsspp-3ds-runner",
    [string]$RunnerLabels = "3ds,devkitpro,devkitarm",
    [switch]$StartRunner,
    [switch]$DispatchSelfHostedBuild
)

$ErrorActionPreference = "Stop"

function Require-Command($Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Missing required command: $Name"
    }
}

Require-Command gh
Require-Command docker

gh auth status | Out-Host

if ([string]::IsNullOrWhiteSpace($Repo)) {
    $repoJson = gh repo view --json owner,name,url | ConvertFrom-Json
} else {
    $repoJson = gh repo view $Repo --json owner,name,url | ConvertFrom-Json
}

$owner = $repoJson.owner.login
$name = $repoJson.name
$url = $repoJson.url
$slug = "$owner/$name"

Write-Host "Repository: $slug"
Write-Host "URL: $url"

$token = gh api -X POST "repos/$slug/actions/runners/registration-token" --jq ".token"
if ([string]::IsNullOrWhiteSpace($token)) {
    throw "Could not create a self-hosted runner registration token for $slug"
}

$envPath = Join-Path $PSScriptRoot "runner.env"
$envText = @"
REPO_URL=$url
GITHUB_REPOSITORY=$slug
RUNNER_TOKEN=$token
RUNNER_NAME=$RunnerName
RUNNER_LABELS=$RunnerLabels
RUNNER_EPHEMERAL=false
RUNNER_VERSION=2.334.0
"@

Set-Content -Path $envPath -Value $envText -Encoding ascii
Write-Host "Wrote runner environment: $envPath"

if ($StartRunner) {
    docker compose --env-file $envPath -f (Join-Path $PSScriptRoot "docker-compose.runner.yml") up -d --build
}

if ($DispatchSelfHostedBuild) {
    gh workflow run build-3ds.yml --repo $slug -f use_self_hosted=true
    gh run watch --repo $slug
}
