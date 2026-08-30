# Mackie Control research boundary

Status: independent research branch; not a replacement for the verified EUCON host.
Date: 2026-08-30. Baseline: `e57eca4`.

## Architecture decision

`Windows audio -> logical tracks/commands -> Mackie surface model -> MIDI transport`

The new executable is **Windows Fader Bridge for Mackie Control**. It does not
load Avid libraries, discover EUCON surfaces, open EuMidi automatically, modify
EuControl, or replace the existing WindowsFaderBridge.exe. The existing project-
owned NativeAudioController, WindowsCommandExecutor and WindowsMediaController
are compiled into a separate target as a transitional shared source boundary.
The media and command implementations are unchanged. NativeAudioController has
one additive, protocol-independent keyed command entry point: Mackie volume,
pan, mute and default-device requests resolve their stable identity on the audio
worker immediately before applying the write. This prevents a recycled Core
Audio slot from receiving an old track's request. Existing EUCON entry points
and model code are unchanged; its idle keyed queue does not allocate/drain.
A later shared-library extraction requires its own EUCON regression pass.

Mackie Control is not EUCON: it sends MIDI control messages, not an application
object model. This adapter therefore owns track identity, bank selection, LED
feedback and touch arbitration. An eight-strip MIDI bank is a viewport, not an
eight-track application limit. Channel 9 pitch bend is the master fader.

Device profiles express capabilities and reserved ports. The packet codec and
Windows audio engine must not depend on a device name. P1-Nano's single physical
fader represents eight virtual strip faders plus master; it must not be treated
as a one-channel application. Its touchscreen editor and firmware remain owned
by iCON iMAP, not by this bridge.

## Source ledger (no third-party files are redistributed)

- [iCON P1-Nano product/download page](https://iconproaudio.com/product/p1-nano/).
- P1Nano-PD3V102-English manual, pp. 9-18 and 43-54: controls, DAW port mapping,
  iMAP lifecycle, user MIDI messages, hotkeys and saved configurations.
  [Official PDF](https://s3.amazonaws.com/assets.iconproaudio.com/wp-content/uploads/2023/07/01050723/P1Nano-PD3V102-English.pdf).
- [P1-Nano firmware release notes](https://s3.amazonaws.com/assets.iconproaudio.com/wp-content/uploads/2023/07/10071915/P1-Nano-Firmware-Release-Notes-4.html):
  1.24 addresses selected-channel/fader mismatch. USB names alone cannot prove
  the installed version (1.23 removed version text from names).
- [Mackie MCU Pro / XT Pro owner manual](https://mackie.com/img/file_resources/MCU_Pro-XT_Pro_OM.pdf):
  controller modes and architecture; not a complete byte-level protocol reference.
- [Emagic/Mackie Logic Control manual, chapter 13, pp. 105-123](https://images.thomann.de/pics/prod/151261_manual.pdf):
  manufacturer-authored MIDI implementation, hosted by the retailer. Documents
  pitch bend, touch/LED notes, signed-magnitude encoders, LCD, ring and meters.
  This is the older **Logic Control** model, not a claim that every MCU extension
  is specified by this manual.
- [Mixxx P1-Nano integration documentation](https://manual.mixxx.org/2.7/en/hardware/controllers/icon_p1_nano)
  and its [upstream implementation](https://github.com/mixxxdj/mixxx/blob/main/res/controllers/Icon-P1Nano-scripts.js)
  independently corroborate P1-Nano virtual faders and MCU model ID 0x14/LCD
  framing. Observed iCON vendor extensions there are not treated as official
  contracts and are not copied into this project.
- [Microsoft midiInOpen](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-midiinopen),
  [midiOutLongMsg](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-midioutlongmsg),
  [midiOutReset](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-midioutreset):
  WinMM handle, callback and buffer ownership.

## Implementation contracts

- Short input messages are processed on an owning window thread, not inside a
  MIDI driver callback. Audio writes are queued to the existing audio worker.
- Shell commands, media calls and window restoration do not run inside the MIDI
  message dispatch. Window identity includes process, executable and package
  identity; activation remains subject to Windows foreground/security rules.
- Keep prepared SysEx memory alive until the driver returns it. Reset/unprepare
  before closing. Never free an in-flight MIDIHDR.
- Accept fader movement only with explicit touch by default. Suppress motor
  output during touch; prevent banking/flip changes mid-gesture. Keep touch
  bound to a logical key, not a changing enumeration index.
- Windows is authoritative for LEDs and motor feedback. Brief pending-write
  reconciliation handles in-flight audio snapshots; it expires and must not
  hide an external Windows update indefinitely.
- Only active logical tracks receive meaningful controls; blank bank positions
  never route to a different app. Track order is persistent; disconnecting an
  app does not silently reshuffle assignments.
- Encoder steps use signed magnitude; 0x40 is zero, not a decrement. Absolute
  MCU fader values are linear Windows scalar volume in this prototype. No
  device-specific unity position or dB law is guessed.
- Standard meter packets contain peak level 0..12, with 14/15 reserved for
  overload set/clear. A stereo pair uses its maximum peak. Do not claim standard
  MCU carries EUCON's independent multichannel metering or RGB strip colors.
- LCD updates are coalesced to 5 Hz and rings to 20 Hz so traditional MIDI links
  are not treated as unlimited-bandwidth USB. Faders/LEDs are change-driven;
  meters refresh at up to 20 Hz because surfaces may decay them locally.
- Output is limited to documented control feedback. Do not send reset, reboot,
  touch calibration, firmware, iMAP programming or other vendor SysEx.
- MCU clone handshake requirements, iCON proprietary color/meter extensions,
  D5-specific display offsets and HUI are not silently inferred. Record them as
  unverified limitations until authoritative documentation / hardware evidence.

## Workstation inventory

Read-only discovery found four P1-Nano MIDI input/output pairs and an installed
P1-Nano iMAP. C++ v143/Windows SDK and .NET 8 test tooling are already available.
No driver installation is required to begin. Downloaded manuals and the iCON
Smart Install Windows ZIP are stored under the user's Documents folder in
`Windows Fader Bridge Private Research/Mackie Control`, outside Git.

P1-Nano port 4 is reserved for iMAP (manual p.17). Only the matching DAW pair
1, 2 or 3 may be selected. Preserve existing iMAP presets before any user change.
Do not automatically install the package or upgrade firmware. A firmware version
check and selection of an MCU-compatible DAW mode are user hardware checks.

## Verification requirements

Protocol tests must cover signed deltas, fader limits, button release/debounce,
touch feedback exclusion, stable tracks, disappearance, bank/flip guards,
master switching, pending-write expiry, LCD bounds, meter overload and malformed
input. The host must build without an Avid SDK dependency and leave EUCON source
and runtime untouched. A successful MIDI open/write is not hardware verification.
Only the user can confirm physical motor direction, touch, display formatting,
LEDs and actual P1-Nano mode while away from the workstation.

## Completed software checks (2026-08-30)

- Independent Release and Debug builds without any Avid import library.
- Eight native test groups, 27,670 assertions, including 50,000 seeded malformed
  input events; no MIDI ports opened by tests.
- Opt-in real Windows integration test: its own silent, nonpersistent shared
  audio session, MCU fader -> keyed queue -> Windows volume, Windows -> motor
  packet, mute/unmute, pan and encoder-push center. All passed. Stale identity
  was rejected without touching the live session.
- Native host smoke: three active endpoint tracks, 37 MIDI input ports, 38 output
  ports; **zero MIDI sends**, automatic exit successful. Port counts are a local
  inventory, not hardcoded configuration or a device requirement.
- Existing nine .NET protocol tests still pass.
- Windows UI inspection: actual endpoint names/levels, route controls,
  explicit port selection, refusal to connect without a chosen pair, clean
  exit. Checkbox contrast was corrected after visual inspection.
- Existing EuControl / WindowsFaderBridge process identities and start times
  unchanged. An EUCON Release regression build passed using
  `artifacts/eucon-regression`, without overwriting/relaunching the live executable.
- The new executable's import table contains Windows/VC runtime libraries, no
  Avid/EUCON library. A no-SDK CI workflow was added; remote CI is not claimed
  as executed by this local verification.

The integration test uses the Microsoft contracts for
[shared-stream initialization](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-initialize)
and [session volume services](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-getservice).
No example source or SDK files are copied into the test.

## Explicitly not verified / not implemented

- No physical P1-Nano (or other MCU surface) gestures have been exercised in this
  change. Do not describe the program as hardware-certified or zero-latency.
- One MCU MIDI pair, eight logical strip positions plus master; MCU extenders,
  multiple main units, HUI and older Logic Control model IDs are not implemented.
  More than eight Windows tracks are supported by banking, independent of this.
- Manufacturer-specific handshakes, LCD meter-mode initialization, RGB colors,
  iCON D5/secondary display protocols and iMAP touchscreen labels are not sent.
  A device requiring these may have partial/no feedback despite valid MIDI I/O.
  Meter packets alone are not proof that a particular display enables metering.
- MCU LCD is ASCII, six readable characters plus spacing per strip; the Windows
  UI keeps full Unicode names. Lower line currently displays volume, not a DAW
  song/timecode display. Media title/artist/position are shown in the Windows UI.
- Pan is Windows stereo balance, not a DAW equal-power panner. Standard MCU peak
  feedback is quantized from -60..0 dBFS into 0..12, max across Windows channels;
  it is not calibrated multi-channel or intersample true-peak metering.
- Automatic startup/reconnect, installer, persistent custom device profiles,
  arbitrary multi-surface assignments and drag/reorder UI are later product work.
- EUCON and Mackie can read the same Windows state, but their current solo policy
  engines are separate. Use solo from only one running edition at a time until
  a shared cross-process mixer service owns that policy.
- Global Windows media-key fallback cannot guarantee targeting a specific app.
  Capability-aware selected-app transport is used when GSMTC is available;
  unsupported actions do not fabricate a successful LED state.
