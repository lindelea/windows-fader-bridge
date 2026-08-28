# Native EUCON 2026.4 implementation

## Baseline

- Avid EUCON SDK: 2026.4.0.23
- EUCON API library: 5.8.1.23
- Windows EUCON Workstation Unified: 2026.4.0.23
- Architecture: native x64 C++ EUCON host plus the existing .NET Windows Core
  Audio engine
- Target surface: Avid S3 first; S1/Dock/Avid Control should follow the same
  published object model

The implementation deliberately does not use `EuMidi1/2`, Mackie Control, or
MIDI messages for the Avid path. EuControl attaches the physical surface to the
application's registered EUCON node and channel processors.

## Probe status

`FaderBridge.EuconHost.exe` currently registers 16 stable channel-strip
processors. Each processor publishes:

- a fader with a -96 dB to 0 dB value table;
- a mute switch and LED;
- a stable persistence ID and left-to-right channel order;
- channel name and channel number text displays;
- a mono level meter;
- a channel knob set containing a volume rotary.

The executable owns a real Win32 foreground window. This matters because the
official console examples can make EUCON application-focus behavior ambiguous.
Surface callbacks post short messages to the window thread; later Windows audio
work is performed off the EUCON callback thread to avoid SDK lock inversions.

## Next implementation slice

1. Replace the 16 placeholder names with live Windows Core Audio session names.
2. Convert between Core Audio scalar volume and the EUCON dB fader law.
3. Route S3 fader/mute/rotary callbacks to the selected session.
4. Push external Windows volume/mute changes back to the motor faders and LEDs.
5. Feed per-session peak values into EUCON meters. Start with legacy meter
   updates for validation, then move to `EuBatchedMeterWriter` and only send
   visible strips.
6. Add stable session matching and banking rules so restarting an application
   does not unnecessarily reshuffle the S3.

## S3 validation after reboot

1. Confirm EuControl reports 2026.4 and the S3 is online.
2. Start `artifacts\eucon\Release\FaderBridge.EuconHost.exe`.
3. Bring its window to the foreground and select/lock the app in EuControl if
   required.
4. Verify 16 strips show `APP 1` through `APP 16`.
5. Move faders and knobs and press Mute; the probe window title should report
   the channel, control and value.
6. Verify all 16 meters animate without stuck or corrupted LED segments.

The 2023.6 EuControl settings and cached installer were copied to
`sdk\backup\eucon-2023.6-before-upgrade-20260829` before upgrading. The 2026.4
installer log is `sdk\eucon-workstation-2026.4-install.log`.
