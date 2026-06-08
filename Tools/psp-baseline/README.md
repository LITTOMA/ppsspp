# PPSSPP 3DS PSP Baseline

Minimal PSP homebrew for testing the PPSSPP 3DS port with a legal, reproducible `EBOOT.PBP`.

It exercises:

- PSP boot and HLE startup
- controller reads, including face buttons, d-pad, triggers, Start and Select
- analog input
- vblank/frame pacing
- debug framebuffer text output
- a small ASCII heartbeat animation

This is intentionally conservative. It does not use libGU or PSP audio callbacks,
so a hang here points at core PSP execution/input/display timing instead of a
graphics or audio stress path.

Controls:

- Triangle: increment a visible toggle counter
- Start + Select: exit

Build from the repository root:

```powershell
pwsh Tools/psp-baseline/build.ps1
```

The SD-card-ready output is:

```text
_psp-baseline-artifacts/PSP/GAME/PPSSPP3DSBASE/EBOOT.PBP
```
