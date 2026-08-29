# Windows Fader Bridge

Windows Fader Bridge is an experimental native control-surface bridge for the
Windows per-application volume mixer. It exposes a device-independent mixer
model through the native EUCON 2026.4 SDK; the Avid S3 and Avid Control are the
first validation surfaces. iCON P1-Nano/Mackie Control support is also planned.

## Screenshots

### Windows application

![Windows Fader Bridge status window](docs/images/windows-fader-bridge-app.png)

### Assignable commands in EuControl

![Windows Fader Bridge command categories in the EuControl Soft Key Command Editor](docs/images/eucontrol-command-assignment.png)

### Avid Control on iPad

![Windows audio applications and devices controlled from Avid Control on an iPad](docs/images/avid-control-ipad.png)

A physical control surface is optional. EuControl can expose the same Windows
mixer model to Avid Control on an iPad, providing software faders, meters,
channel colors, mute, solo, pan, banking and soft-key access.

### EUCON surface in use

![Windows audio applications and devices controlled from a EUCON surface](docs/images/eucon-surface-in-use.jpeg)

The pictured surface is validation hardware, not a device-specific dependency.
Windows Fader Bridge publishes a device-independent EUCON application model and
leaves surface discovery, assignment and banking to the Avid EUCON runtime.

## Current application

`src/FaderBridge.EuconHost` is a native x64 EUCON application. It currently
provides:

- event-driven Windows Core Audio session discovery and updates;
- one virtual EUCON channel per active Windows audio application;
- EUCON-managed assignment, layouts and banking across the virtual track list;
- bidirectional fader, encoder, mute, OLED label and position-ring sync;
- per-application peak meters using EUCON Meter API 3.1 with a temporary
  regular-meter compatibility fallback;
- stable application persistence IDs for EuControl assignments and layouts;
- per-application EUCON channel colors derived from Windows app/package icons,
  with a stable fallback palette;
- direct Core Audio writes on an MMCSS worker for low control latency.
- 186 freely assignable EUCON soft-key commands across Windows Audio, system
  tools, Settings, folders, File Explorer views, window management, taskbar,
  virtual desktops, editing, browser tabs, media, capture/input, input methods,
  and accessibility;
- a native dark status window, professional mixer icon, system-tray background
  operation, single-instance activation, diagnostics access, and optional
  per-user startup with Windows.

Closing the main window or choosing **Hide to tray** keeps the bridge running.
Double-click the tray icon to reopen it, or use the tray menu to open, configure
startup, or fully exit. Startup launches with `--background`, so no window is
shown until requested.

The application fader mapping is intentionally linear: table coordinates
`-9600..0` map to Windows volume `0..100%`. The region above 0 dB is treated as
physical overtravel and rebounds to Windows 100%.

## Build

Requirements:

- Windows 11 x64;
- Avid EUCON Application SDK and Workstation Unified 2026.4, obtained
  separately from Avid and used under Avid's own license;
- Visual Studio 2022 v143 C++ build tools.

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-eucon.ps1
```

The executable is written to
`artifacts/eucon/Release/WindowsFaderBridge.exe`.

The Avid SDK, headers, libraries, documentation, examples, installers and
EuControl software are **not** part of this repository. Obtain the SDK from
the [official Avid EUCON Application SDK page](https://developer.avid.com/eucon/),
accept Avid's applicable terms, and install it locally before building. See
[`docs/BUILDING.md`](docs/BUILDING.md) for complete setup instructions and a
custom SDK-path example.

Only one EUCON test application should run at a time. The SDK, EuControl
installer, build artifacts and diagnostic logs are deliberately excluded from
Git.

## Repository status

This is an active hardware-research project, not a finished release. The public
source tree contains only Windows Fader Bridge project material. Proprietary
Avid SDK content and privately collected research material are deliberately
excluded.

The generic integration contract is recorded in `docs/EUCON_ARCHITECTURE.md`;
the assignable command catalog is documented in `docs/WINDOWS_COMMANDS.md`, and
the Chinese command manual is in `docs/WINDOWS_COMMANDS_ZH.md`. Device-specific
verification evidence is also under `docs/`.

## License

Windows Fader Bridge source code is available under the
[Mozilla Public License 2.0](LICENSE). MPL-2.0 requires modifications to covered
source files to remain available under the same license while allowing the
project to interoperate with separately licensed system and SDK components.

MPL-2.0 does not grant any rights to Avid software, documentation, trademarks,
SDK files or EUCON technology supplied by Avid. See
[`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md).
