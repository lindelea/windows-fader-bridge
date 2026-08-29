# Windows Fader Bridge

Windows Fader Bridge is an experimental native control-surface bridge for the
Windows per-application volume mixer. Its first hardware target is the Avid S3
through the native EUCON 2026.4 SDK; iCON P1-Nano/Mackie Control support is also
planned.

## Current EUCON prototype

`src/FaderBridge.EuconHost` is a native x64 EUCON application. It currently
provides:

- event-driven Windows Core Audio session discovery and updates;
- one virtual EUCON channel per active Windows audio application;
- S3 banking beyond the 16 physical faders;
- bidirectional fader, encoder, mute, OLED label and position-ring sync;
- per-application peak meters using EUCON Meter API 3.1 with an S3-compatible
  regular-meter fallback;
- stable application persistence IDs for EuControl assignments and layouts;
- direct Core Audio writes on an MMCSS worker for low control latency.

The fader mapping is intentionally linear: S3 table coordinates `-9600..0`
map to Windows volume `0..100%`. The region above 0 dB is treated as physical
overtravel and rebounds to Windows 100%.

## Build

Requirements:

- Windows 11 x64;
- Avid EUCON SDK and Workstation Unified 2026.4;
- Visual Studio 2022 v143 C++ build tools.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-eucon.ps1
```

The executable is written to
`artifacts/eucon/Release/FaderBridge.EuconHost.exe`.

Only one EUCON test application should run at a time. The SDK, EuControl
installer, build artifacts and diagnostic logs are deliberately excluded from
Git.

## Repository status

This is an active hardware-research project, not a finished release. The
`src/FaderBridge.EuConApp` directory is a private working baseline derived from
the Avid SDK example and remains subject to the Avid EUCON SDK License
Agreement. Do not redistribute that directory without confirming the license.

Implementation notes and verified S3 behavior are recorded under `docs/`.
