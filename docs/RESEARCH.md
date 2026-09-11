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

Archaeology of MIDI Mixer 2.7.3 shows that its .NET core uses NAudio/CoreAudio
session notifications and cached session objects (`MixerWatcher`,
`_CachedSession`, `IAudioSessionNotification`, and `IAudioSessionEventsHandler`).
Its meter timer is separate from volume writes. This is the important latency
lesson; repeatedly enumerating sessions on every fader sample is unnecessary.

The native EUCON path now follows the same hot-path shape:

- S3 -> EuControl -> native EUCON callback -> lock-free latest-value queue
- a same-process MMCSS CoreAudio worker writes cached `ISimpleAudioVolume`
  interfaces immediately
- no Win32 UI dispatch, named pipe, or .NET process participates in a fader write
- session discovery and 30 Hz meter snapshots are outside the write path
- a surface-originated write is acknowledged without driving its own motor;
  externally-originated Windows changes drive the fader and encoder ring

Future resilience work should replace periodic discovery with session/device
notifications and treat endpoint removal, sleep/resume, and Audio Service restart
as normal state transitions.

## Windows application controls

The upper EUCON knob area is modeled as the selected application's parameter
rack, not as device-specific S3 controls. The first implementation uses the
documented Windows `GlobalSystemMediaTransportControlsSessionManager` for media
transport/state and Win32 top-level-window APIs for focus, minimize,
maximize/restore, and topmost state. GSMTC matching requires the media session's
source application ID to match the track package family or executable identity;
there is deliberately no fallback to the global current media session.

Windows endpoint enhancement properties and OEM APO controls are not part of
this implementation. Microsoft documents endpoint properties as client-readable
and driver effects are not a stable per-application control contract. Apollo or
other vendor processing remains owned by its control-room software. Future
application profiles may add explicit, user-configured shortcuts, but commands
without readable state must remain momentary and must not claim persistent LED
feedback.

The first mixed-player test on 2026-08-29 showed why media controls cannot be a
fixed seven-cell page. Apple Music published readable timeline state while its
GSMTC capability flags rejected position, shuffle, and repeat writes; foobar2000
published no identity-matched GSMTC session; and Chromium could publish multiple
sessions with the same source application ID. The adapter now treats the
session's playback-control flags as authoritative, preserves a readable timeline
as a non-interactive position cell when seeking is unavailable, dynamically omits
unsupported actions, prefers Windows' current matching session and then a playing/paused
matching session, and requests title/artist metadata asynchronously. A legacy
`WM_APPCOMMAND` fallback is intentionally not automatic because Windows exposes
no capability query or authoritative state for it; an explicit compatibility
profile may provide that later without pretending unsupported feedback exists.

## External references

- Microsoft Core Audio `IAudioSessionManager2` and `IAudioMeterInformation`
- Avid EUCON Application SDK and EUCON Application Setup Guide
- Avid EuControl 2024.10 release notes (meter fix GWSW-16637)
- iCON P1-Nano product/manual resources
- Mixxx P1-Nano mapping and manual as an open behavioral reference

## EUCON rotary display modes — 2026-09-12

The SDK's `EuPrimitiveKnob::SetPositionRingMode(kRingOff)` is a useful, verified
presentation option for a rotary-position control that intentionally has no
continuous progress meaning. It leaves the encoder/control available while
removing the position/progress ring. Keep this as an explicit display strategy
to evaluate for future enumerated choices, selectors, navigation controls and
other non-progress settings; suitability still has to be checked on both S3
and Avid Control rather than applied globally.

Do not use `kRingOff` for a continuous level. In particular, UAD `Talk dB` is a
talkback microphone volume and therefore uses the solid volume-style
`kRingThermometerLeft` display, just like other attenuation/level controls.
