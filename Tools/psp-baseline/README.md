# PPSSPP 3DS PSP Baseline

Minimal PSP homebrew for testing the PPSSPP 3DS port with a legal, reproducible `EBOOT.PBP`.

It exercises:

- PSP boot and HLE startup
- controller reads, including face buttons, d-pad, triggers, Start and Select
- analog input
- a simple libGU sprite path
- 44.1 kHz stereo audio callback through `pspaudiolib`
- vblank/frame pacing

Controls:

- D-pad / analog: move the rectangle
- Triangle: toggle the tone
- Start: toggle automatic rectangle drift
- Select: recenter the rectangle

Build from the repository root:

```powershell
pwsh Tools/psp-baseline/build.ps1
```

The SD-card-ready output is:

```text
_psp-baseline-artifacts/PSP/GAME/PPSSPP3DSBASE/EBOOT.PBP
```
