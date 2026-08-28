# FaderBridge

FaderBridge is an in-progress Windows audio-session mixer bridge for MIDI and
control-surface hardware. The first target devices are:

- Avid S3 through the native EUCON 2026.4 SDK
- iCON P1-Nano through its class-compliant Mackie Control MIDI port

The repository currently contains a read-only probe plus the first protocol
codec. It does not change volume or send MIDI during probing.

## Run the hardware probe

```powershell
dotnet run --project .\src\FaderBridge.Probe
```

## Test

```powershell
dotnet test
```

## Design direction

The core is deliberately Windows-native and separate from the eventual UI:

1. Core Audio snapshots enumerate every active render endpoint and its sessions.
2. Stable rules map a strip to a process path, AppUserModel identity, endpoint,
   or the current foreground session.
3. Device adapters translate MIDI/Mackie messages into normalized actions and
   translate current volume, mute, labels, and peak values back to the hardware.
4. Adapters are replaceable so native EUCON and MIDI/Mackie hardware share the
   audio engine without leaking protocol-specific behavior into it.

The existing MIDI Mixer 2.7.3 installation is used only as behavioral and
interoperability research material. Its source is not copied into this project.

## Native EUCON 2026.4 probe

`src/FaderBridge.EuconHost` is a native x64 EUCON client, not a MIDI or Mackie
translator. It publishes 16 channel-strip processors to EuControl with motor
faders, mute, displays, rotary controls, and meters.

Build it with:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\build-eucon.ps1
```

The probe requires Avid EUCON SDK/Workstation Unified 2026.4 and the Visual
Studio 2022 v143 C++ toolchain. See `docs/EUCON_2026.md` for the implementation
and test plan.
