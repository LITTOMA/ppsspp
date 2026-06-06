# PPSSPP 3DS CI

This directory contains the reproducible 3DS build path for the PPSSPP port.

## Hosted devkitPro Docker job

The default GitHub Actions job runs inside `devkitpro/devkitarm:latest` and calls:

```sh
ci/3ds/build-3ds.sh
```

Expected outputs are collected under `artifacts/3ds/` and uploaded as workflow artifacts.

## Self-hosted devkitPro runner

Create a runner registration token with GitHub CLI and optionally start the runner:

```powershell
pwsh ci/3ds/setup-github-ci.ps1 -Repo OWNER/REPO -StartRunner
```

Run the optional self-hosted CI job:

```powershell
gh workflow run build-3ds.yml --repo OWNER/REPO -f use_self_hosted=true
gh run watch --repo OWNER/REPO
```

The compose runner is itself based on `devkitpro/devkitarm`, so local runner builds and hosted Docker builds use the same devkitARM/libctru environment.

## Resume after reboot or network recovery

If this directory only contains the CI overlay and not the PPSSPP source tree yet, run:

```powershell
pwsh ci/3ds/bootstrap-after-reboot.ps1 -Repo OWNER/REPO -StartRunner
```

That clones `hrydgard/ppsspp` into `_ppsspp-source`, copies this CI overlay into the checkout, validates compose, and starts the self-hosted runner when `-StartRunner` is provided.

## Current porting contract

`build-3ds.sh` intentionally fails if the source tree does not produce a `.3dsx` or `.cia`. That keeps CI honest while the PPSSPP 3DS platform layer is being wired into CMake.
