# EUCON 2026.4 hardware validation baseline

## Purpose

This document records Windows Fader Bridge behavior observed on real EUCON
surfaces. It is project-authored validation evidence, not a copy or substitute
for any Avid SDK guide, example, source file or product manual.

The production application publishes a device-independent EUCON application
model. EuControl or WSControl owns discovery, attachment, banking, layouts and
physical-surface capability differences.

## Verified environment

- Windows 11 x64;
- EuControl 2026.4.0.23;
- separately installed EUCON SDK 2026.4.0.23;
- EuCon2.dll 5.8.1.23;
- physical Avid S3;
- Avid Control attached at the same time;
- processor meter API 3.1.

No Avid SDK file, example or documentation is stored in this repository. Refer
to Avid's separately supplied materials for normative API requirements.

## Verified production behavior

The following behavior was verified by the user on 2026-08-29:

- S3 and Avid Control attach to the same registered application node;
- active Windows applications and enabled/ready endpoints appear as logical
  tracks rather than a fixed physical-strip count;
- fader input, touch, motor feedback, encoder/ring, Mute and LEDs work in both
  directions;
- labels, application colors and meters render on the surface;
- Select can activate, minimize and reliably restore application windows;
- Record Arm selects the corresponding default Windows input/output endpoint;
- Solo implements single-target application monitoring and Clear Solo restores
  the previous mute states;
- endpoint and application pan/balance, route selection and application knob
  pages operate where Windows exposes the required capability;
- live track-list changes do not replace the top-level EUCON node.

## Meter API 3.1 findings

The documented Meter API 3.1 path is the primary implementation:

1. the node advertises Meter API 3.1 before registration;
2. each channel declares its meter format and leg roles after processor
   registration;
3. visibility callbacks provide the handle and format for visible meters;
4. a short-lived `EuBatchedMeterWriter` sends level, peak and clip data;
5. only visible meters with valid handles use the batched path.

On the verified EuControl/S3 setup, dynamically registered tracks did not
always receive a `VisibilityChangedV2` handle even though the SDK setup calls
returned success. A regular meter primitive currently carries the same project
meter value until a valid 3.1 handle becomes available. This is an isolated,
instrumented compatibility fallback rather than a replacement for Meter API
3.1.

## Dynamic topology invariant

The top-level `EuNode` remains alive for the process lifetime. Windows audio
applications and endpoints are represented by stable logical processors added,
removed or reordered inside the smallest valid `Freeze()` / `Thaw()` scope.

Replacing the registered node when the Windows application list changed caused
complete surface detachment during testing. The production implementation must
therefore update processors in place and preserve persistence IDs independently
of Core Audio process IDs or transient slots.

## Feedback invariants

- Windows is authoritative for externally changeable volume, mute, default
  endpoint, mono-audio and route state.
- Touch is explicit interaction state; motor, ring and LED values are feedback
  from the application model.
- Surface callbacks remain short and queue Windows work to the owning thread.
- Compatibility holds and echo suppression must not hide genuine external
  Windows changes.
- A switch LED is explicitly updated when its control contract does not provide
  automatic state feedback.

## Fader policy

Windows volume maps linearly from the project table's `-96 dB..0 dB` range to
`0..100%`. Physical values above unity are application overtravel: the Windows
command is clamped to 100%, and the motor is returned to unity. This is Windows
Fader Bridge policy, not a claim about generic EUCON behavior.

## Required regression check

For any model-affecting change, verify at minimum:

1. application focus and attachment;
2. simultaneous S3 and Avid Control visibility;
3. live application/endpoint add and remove;
4. stable assignment, banking and layout recall;
5. fader, touch, motor and overtravel;
6. encoder, touch, ring and pan reset;
7. Mute, Solo, Select, Record Arm and LEDs;
8. labels, colors and channel order;
9. meter level, peak, clip and performance;
10. absence of oscillation, feedback loops, callback deadlocks and surface
    detach.
