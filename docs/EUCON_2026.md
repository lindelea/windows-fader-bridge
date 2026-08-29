# Native EUCON 2026.4 implementation

## Baseline

- Avid EUCON SDK: 2026.4.0.23
- EUCON API library: 5.8.1.23
- Windows EUCON Workstation Unified: 2026.4.0.23
- Architecture: native x64 C++ EUCON adapter with in-process Windows Core Audio
- Target surface: Avid S3 first; S1/Dock/Avid Control should follow the same
  published object model

The implementation deliberately does not use `EuMidi1/2`, Mackie Control, or
MIDI messages for the Avid path. EuControl attaches the physical surface to the
application's registered EUCON node and channel processors.

## Current status

`WindowsFaderBridge.exe` publishes a live, device-independent set of logical
Windows audio processors. There is no fixed 16-channel model: EUCON owns
assignment and banking across however many active application and endpoint
tracks Windows currently supplies. Each applicable processor publishes:

- a fader with a -96 dB to 0 dB value table;
- a mute switch and LED;
- a stable persistence ID and left-to-right channel order;
- channel name and channel number text displays;
- true per-leg peak meters through Meter API 3.1;
- volume, pan/balance, default-device routing, Select, Solo, and capability-
  negotiated application controls where Windows supplies matching semantics.

The executable owns a native status window and system-tray shell. Closing the
window leaves the adapter running; a full exit is available from the tray menu.
Surface callbacks remain short and queue Windows audio work away from the EUCON
callback thread to avoid SDK lock inversions.

## Surface validation after reboot

1. Confirm EuControl reports 2026.4 and the S3 is online.
2. Start `artifacts\eucon\Release\WindowsFaderBridge.exe`.
3. Verify S3 and Avid Control attach to the same published application model.
4. Verify live Windows applications/endpoints appear with stable assignments
   and bank normally rather than being limited by physical strip count.
5. Exercise fader/touch/motor, encoder/ring, Mute/Solo/Select/Rec, labels,
   routes, custom knob pages, and peak meters.
6. Close the status window, confirm control continues from the tray, then use
   the tray menu to exit cleanly.

SDK installers, EuControl packages, logs and private compatibility archives are
kept outside the repository. Builders must obtain required Avid software from
the official source and comply with its separate license.
