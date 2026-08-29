# Avid S3 / EUCON 2026.4 hardware baseline

## Purpose

This is the source of truth for FaderBridge's S3 hardware layer. The Windows
Core Audio integration is intentionally deferred until the surface behavior is
understood and repeatable.

The hardware implementation must be derived from the Avid EUCON SDK 2026.4
`EuConApp` example. The earlier minimal `FaderBridge.EuconHost` probe is useful
for comparison, but it is not a safe base for meter or LED behavior: its CH1
meter remained dark even when it sent valid values.

## Verified environment

- EuControl: 2026.4.0.23
- EUCON SDK: 2026.4.0.23
- EuCon2.dll: 5.8.1.23
- Surface: Avid S3
- Processor meter API: 3.1

The workspace copy is under `src/FaderBridge.EuConApp`. The installed SDK in
the user's Documents directory is not modified. Keep this repository private;
the copied source remains governed by the Avid EUCON SDK License Agreement.

## Reproduced result

The original example has eight channels. The workspace hardware baseline makes
only these functional changes:

1. `kNumChannels` is 16 instead of 8.
2. Every channel uses two Sample Peak meter legs (the same Stereo format as the
   original example's CH1).
3. Channels 9–16 repeat the original 1–8 level pattern.

On the physical S3, CH1 lights correctly with approximately two segments. This
was verified by the user on 2026-08-29.

## Exact example meter values

The original example calculates:

```text
base level       = -50.0 dB
channel increment = 6.25 dB
level            = base + increment * channelNumber + jitter
peak             = level + 2.0 dB (with independent jitter in the example)
jitter range     = approximately -0.735 to +0.750 dB
clip             = level > 0.0 dB
```

Ignoring jitter, its eight strip values are:

| Channel | Level | Peak |
|---:|---:|---:|
| 1 | -43.75 dB | -41.75 dB |
| 2 | -37.50 dB | -35.50 dB |
| 3 | -31.25 dB | -29.25 dB |
| 4 | -25.00 dB | -23.00 dB |
| 5 | -18.75 dB | -16.75 dB |
| 6 | -12.50 dB | -10.50 dB |
| 7 | -6.25 dB | -4.25 dB |
| 8 | 0.00 dB | +2.00 dB |

These are floating-point dB values sent to EUCON. They are not physical LED
indexes. EUCON and the S3 render the number and color of illuminated segments.

## Meter API 3.1 invariants

Do not simplify or reorder this pipeline without an S3 A/B test:

1. Set `kATRIBID_ProcessorMeterAPIVersion` to `kMeterAPIVersion_3_1` before the
   node attaches to the surface.
2. Register each channel processor before calling its post-registration meter
   initialization and `EuControlMultiMeter::SetFormat()`.
3. Declare the track format, meter type, parameter flags, leg count, and leg
   roles through `SetFormat()`. Do not use
   `kATRIBID_NumberOfMetersInChannel` for API 3.x.
4. Handle `kEVT_NODE_VisibilityChangedV2` and retain the visibility handle and
   format supplied for each visible meter.
5. Construct a short-lived `EuBatchedMeterWriter` on the dedicated meter update
   thread approximately every 33 ms.
6. Call `SetPerMeterValuesV2()` for master peak/clip, then
   `SetPerLegValuesV2()` for every declared leg's level, peak, RMS context, and
   clip state.
7. Send only visible meters with valid handles.

`FaderBridge.EuconHost` also initializes the ordinary meter primitive as a
compatibility path. On the verified EuControl/S3 stack, dynamically added
application processors do not always receive a `VisibilityChangedV2` handle.
While the handle is invalid, the host writes the same dB peak to that primitive;
the 3.1 batched path takes over whenever a valid handle is available. The legacy
`kATRIBID_NumberOfMetersInChannel` attribute is used only for this fallback and
is ignored by Meter API 3.1.

## Dynamic application topology invariant

The production host registers one top-level EUCON node and keeps it alive for
the lifetime of the process. Active Windows applications are represented by
channel processors added or removed inside `Freeze()` / `Thaw()` updates.

Do not unregister and rebuild the top-level node when the Windows application
list changes. On EuControl 2026.4 that caused the S3 to lose the application
entirely: faders, OLEDs, knobs, switches, and meters all stopped responding.
The persistent-node design plus the meter fallback was verified on the physical
S3 on 2026-08-29; all controls and LED meters operated normally.

The complete `ExNode`, `ExProcessorChannel`, and meter thread from `EuConApp`
are part of the working hardware contract. Valid values alone were not enough
to make the old minimal probe's CH1 reliable.

## Switch and LED behavior

- `EuControlFader` Mute does not automatically mirror its LED. The example's
  `SurfSetMute()` explicitly writes `kID_MuteLed` with `SetCurrentIndex()`.
- Solo, Select, Record Arm, and similar `EuControlSwitch` controls can keep their
  LEDs synchronized automatically when configured like the example.
- Application-to-surface feedback must set both logical state and its LED where
  the control type requires it.

## Fader behavior

The official fader table supports values above unity up to `+12 dB`.
`SurfSetFader()` only reads and prints the surface value, so the original
example does **not** snap a fader back after it crosses `0 dB`.

For a later Windows-volume implementation, `100%` may correspond to `0 dB`.
The desired above-unity snap-back must then be implemented explicitly:

1. accept the surface callback;
2. clamp the application command to 100%;
3. write `0 dB` back to the motorized fader.

This is application policy, not default EUCON behavior.

## Build and hardware check

```powershell
.\scripts\build-euconapp-baseline.ps1
```

Output:

```text
src/FaderBridge.EuConApp/EuConApp/platform/win/x64/
  Release + DLL crtl/EuConApp.exe
```

Run only one EUCON processor test application at a time. The expected S3 state
is `Audio 01` through `Audio 16`, with CH1–8 and CH9–16 showing two matching
ascending meter patterns. CH1 should show approximately two illuminated meter
segments.

## Next hardware experiments

Keep Windows audio out of scope until these are independently verified:

1. surface fader value, touch state, motor feedback, and optional unity
   snap-back;
2. Mute, Solo, Select, and their bidirectional LED states;
3. Stereo level, peak-hold, and clip rendering;
4. 16-channel banking and attention;
5. OLED text, knob values, position rings, and knob switches.
