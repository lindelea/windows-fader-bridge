# MIDI Mixer / controller research notes

Date: 2026-08-29

## What is installed on this workstation

- MIDI Mixer 2.7.3 (Electron UI plus a signed .NET `MIDI Mixer Core.exe`)
- EUCON Workstation Unified 2023.6.0.103
- EuControl and MC_Client are running
- Avid S3 is bridged through `Euphonix EuMidi1` and `Euphonix EuMidi2`
- iCON P1-Nano firmware/driver identifies as `iCON P1-Nano V1.22`
- The P1-Nano exposes four MIDI input and four MIDI output ports
- EuControl exposes eight virtual MIDI input and output ports

## Existing application's architecture

The packaged Electron application depends on the Node `midi` module, but Windows
audio is handled by a separate .NET executable over a local process bridge. The
core contains NAudio and Windows.Devices.Midi. User profiles are JSON and map
logical groups to session paths or endpoint IDs.

The S3 profile proves the current integration is Mackie Control over EuControl's
virtual MIDI driver, not native EUCON:

- two EuMidi ports, eight strips each
- 14-bit pitch-bend faders
- note messages for mute, solo, select, and record
- CC 16-23 relative encoders
- motor feedback over pitch bend
- peak meters over channel pressure

The P1-Nano profile currently uses only its master fader (pitch channel 9 in
one-based Mackie terminology), encoders, transport, bank buttons, and one meter.
It does not exploit all eight virtual strips or the full display surface.

## Concrete failure evidence

Recent MIDI Mixer logs contain:

- foreground-session checks that exceed their deadline and get overwritten
- missing group/button IDs after profile changes
- `Failed to create device: 尚未实现` while creating a MIDI device

The last error comes from the old core's WinRT MIDI path on this machine. The
FaderBridge probe deliberately uses classic WinMM and successfully enumerates
all P1-Nano and EuMidi ports without touching the running application.

## Protocol strategy

1. Ship a direct Mackie adapter first. It covers the S3 through EuMidi and the
   P1-Nano directly, so it is usable without waiting for vendor approval.
2. Support both meter encodings observed in local profiles: standard packed
   Mackie channel pressure and EuMidi's per-pressure-channel variant.
3. Add P1-Nano-specific display/SysEx support after capturing or confirming its
   outbound messages. Keep this isolated in the P1 adapter.
4. Keep a clean adapter boundary for native EUCON. Avid offers an Application SDK
   evaluation toolkit, but redistribution and production use depend on Avid's
   SDK terms; EUCON is not treated as an undocumented open wire protocol.

## Windows audio strategy

- Enumerate every active render endpoint, then every Core Audio session.
- Resolve a fresh session handle before writes; never persist COM session objects.
- Persist matching rules (process path, packaged-app identity, endpoint, or
  foreground fallback), not transient PIDs or session-instance IDs.
- Coalesce high-rate fader input and meter output separately.
- Poll peaks around 30-60 Hz, quantize to the hardware's LED segments, and avoid
  sending unchanged values.
- Treat device removal, sleep/resume, audio-service restart, and MIDI port
  replacement as normal state transitions.

## External references

- Microsoft Core Audio `IAudioSessionManager2` and `IAudioMeterInformation`
- Avid EUCON Application SDK and EUCON Application Setup Guide
- Avid EuControl 2024.10 release notes (meter fix GWSW-16637)
- iCON P1-Nano product/manual resources
- Mixxx P1-Nano mapping and manual as an open behavioral reference
