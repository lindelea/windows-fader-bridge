# Windows Fader Bridge development instructions

## Parallel product scope: Mackie Control research

The separately requested Mackie Control edition is a protocol adapter, not an
iCON P1-Nano driver. Its boundary is Windows audio -> stable logical tracks ->
MCU surface state -> MIDI transport. Keep its executable, settings, lifecycle,
build output and tests independent of the verified EUCON edition. Do not launch
or replace EUCON when testing Mackie. Device profiles may describe reserved
ports/capabilities; do not branch the MCU codec on manufacturer/device names.

Read `docs/MACKIE_RESEARCH.md` for the manufacturer source ledger and explicit
unverified protocol areas, and `docs/MACKIE_GUIDE_ZH.md` for the current workflow.
Consult original, separately obtained manufacturer documents before extending
message definitions. Keep vendor manuals/installers outside Git; links and our
own implementation only. Do not treat open-source controller scripts as a
normative manufacturer contract or copy them into this project.

Build/test with `scripts/build-mackie.ps1`. `-AudioIntegration` is opt-in and
changes only the test process's own silent audio session. Pure tests and smoke
must not open MIDI ports. Physical MIDI tests require an explicit selected
pair; never auto-open EuMidi, an iMAP maintenance port or a guessed device.
Keep touch state keyed across banking and endpoint changes. Shared Windows
backend extensions must retain existing EUCON entry-point semantics and get
an isolated-output EUCON regression build; never overwrite the live executable.

The remaining sections define the established EUCON edition and still apply to
any EUCON model/SDK changes.

## Mission

Build Windows Fader Bridge as a device-independent EUCON application adapter
for the Windows per-application audio mixer.

The production boundary is:

```text
Windows Core Audio
  <-> Windows mixer model
  <-> Fader Bridge EUCON adapter
  <-> Avid EUCON runtime
  <-> EuControl or WSControl
  <-> compatible EUCON surfaces
```

Fader Bridge must not act as an S3, S4, S6, S1, Dock, or Avid Control driver.
It publishes a standards-based application model. Avid's EUCON runtime owns
surface discovery, attachment, focus, assignment, layouts, banking, capability
differences, and hardware communication.

## Source-of-truth order

Never implement EUCON behavior from memory, analogy, trial-and-error, or a
single observed device result. Consult sources in this order:

1. Avid EUCON SDK 2026.4 official guide:
   `%USERPROFILE%\Documents\Avid\EUCON SDK\GettingStartedWithEuCon.pdf`
2. Installed SDK declarations and inline API contracts:
   `C:\Program Files\Avid\EUCON SDK\include`
   - start with `EuDefinitions.h`, `EuNode.h`, `EuCommon.h`, and the exact
     `EuControl*`, `EuPrimitive*`, or `EuBatchedMeter*` header in use;
3. Official basic adapter example:
   `%USERPROFILE%\Documents\Avid\EUCON SDK\EuConIO`
4. Official advanced adapter example:
   `%USERPROFILE%\Documents\Avid\EUCON SDK\EuConApp`
5. Focused official examples:
   `%USERPROFILE%\Documents\Avid\EUCON SDK\FAQ Code Examples`
   - especially `ChannelVisibility.cpp`, `MeterExample.cpp`,
     `PersistentPayloadExample.cpp`, `KnobByDeltaExample.cpp`, and
     `AutomationExample.cpp` when relevant;
6. Official color-grading example only for applicable controls:
   `%USERPROFILE%\Documents\Avid\EUCON SDK\EuConColor`
7. Waveform Link documentation only for waveform work:
   `%USERPROFILE%\Documents\Avid\EUCON SDK\WaveformLink Documentation.pdf`
8. Repository design and verified findings:
   - `docs/EUCON_ARCHITECTURE.md`
   - `docs/eucon-s3-hardware-baseline.md`
   - `docs/EUCON_2026.md`
   - `docs/RESEARCH.md`

The guide and installed header contracts are normative. Example projects are
non-normative supporting evidence: use them to confirm a documented lifecycle
or API call pattern, never to invent behavior that the guide and headers do not
define. If an example is older than the installed header contract, follow the
current non-deprecated API unless the guide explicitly requires otherwise.

All Avid documentation, examples, headers, libraries and installers must remain
outside this repository. Consult only a separately obtained local SDK copy and
never copy its contents into a change or commit.

## Required workflow for EUCON changes

Before changing EUCON model or callback code:

1. Name the EUCON concept being changed: node lifecycle, processor lifecycle,
   assignment, layout, primitive value, touch, knob set, persistence, meter,
   callback, threading, or another documented concept.
2. Read the complete relevant guide section, not only a search result or code
   fragment.
3. Read the declarations and comments for every SDK class and enum involved.
4. Study the matching EuConIO implementation first, then EuConApp and the
   focused FAQ example if the feature is advanced.
5. Write down the official lifecycle, ordering, ownership, callback-thread, and
   error-handling requirements before editing code.
6. If documentation and observed behavior disagree, instrument the documented
   path first. Preserve return codes, event types, thread identity, object IDs,
   visibility state, and timing in diagnostics.
7. Implement the smallest standards-based change. Compatibility workarounds
   must be isolated, documented, and must never silently replace the official
   path.
8. Build and run proportionate regression tests.
9. Require user verification on both the physical S3 and Avid Control for any
   change affecting the exposed EUCON model or feedback behavior.
10. Record verified behavior and any SDK discrepancy in `docs/` before commit.

Do not claim an SDK or driver bug until the official model, required attributes,
call ordering, visibility, focus/assignment state, and callback thread have all
been verified with evidence.

## Generic EUCON invariants

- Keep one top-level application `EuNode` alive for the process lifetime.
- Register the node only after its required global model is initialized.
- Add, change, or remove live processors using the smallest valid
  `Freeze()` / `Thaw()` scope. Do not rebuild the node when Windows audio
  applications appear or disappear.
- Represent Windows applications as logical channel-strip processors with
  stable persistence IDs. Process IDs and transient Core Audio slots are not
  track identity.
- Use documented processor types, track types, layout rules, layout names,
  channel order, and standard control semantics.
- Never use a connected device's physical strip count as application capacity.
  EUCON owns assignment and banking.
- Never branch production behavior on device names such as S3 or Avid Control.
  Device names are allowed only in tests, diagnostics, and compatibility notes.
- Keep callbacks short. Queue work to an owning thread and avoid any lock that
  could also be held while another thread calls the EUCON API.
- Treat touch as explicit interaction state and motor/LED/ring feedback as
  application-to-surface state. Prevent feedback loops without suppressing
  genuine external Windows changes.
- Use Meter API 3.1 visibility callbacks, saved handles, saved formats, and
  `EuBatchedMeterWriter` as the primary meter path. Ordinary primitive writes
  are a temporary compatibility fallback and must not become the assumed path.
- Add optional Project, System, Monitor, Transport, Assignable Knob, automation,
  or persistence processors only when the Windows mixer supplies the matching
  documented semantics.

## Current verified baseline

- Git commit `e120721` is the rollback point verified on 2026-08-29.
- Avid S3 faders, touch, motor feedback, encoder/ring, mute/LED, labels, and
  meters were verified working.
- Avid Control can attach to the same application model at the same time.
- Replacing the registered top-level node during a Windows application-list
  change caused complete surface detachment and is prohibited.
- A regular meter primitive fallback currently keeps meter LEDs active when the
  documented Meter API 3.1 visibility handle is unavailable. On 2026-08-29,
  all Meter API setup calls returned `kERR_OK`, but dynamically registered
  tracks received no `VisibilityChangedV2` callback in the attached EuControl
  setup. This remains an investigated compatibility path, not a final
  architectural assumption.

## Architecture work still required

- Verify the new stable logical track registry and mutable Core Audio routing
  on both attached test surfaces before accepting it as the baseline.
- Audit the current Volume knob's use of the predefined Input knob-set layout.
- Audit forced primitive refreshes and overtravel rebound as generic application
  policy rather than device fixes.
- Instrument and resolve Meter API 3.1 visibility negotiation before removing
  the compatibility fallback.
- Replace outdated fixed-16-channel documentation with the live virtual-track
  model wherever it is still presented as current behavior.

## Build and verification

Build the native adapter with:

```powershell
.\scripts\build-eucon.ps1
```

Run only one processor-side EUCON test application at a time. Do not modify or
restart EuControl, MC_Client, WSControl, drivers, firmware, or surface settings
unless the user explicitly authorizes that operation.

For model-affecting changes, verify at minimum:

- application focus and attachment;
- simultaneous S3 and Avid Control visibility;
- live application add/remove and stable assignment;
- banking and layout recall;
- fader input, touch, motor feedback, and overtravel policy;
- encoder input, touch, ring feedback, and semantic placement;
- mute in both directions and LED state;
- labels and channel order;
- meter visibility, level, peak, clip, and performance;
- absence of motor oscillation, feedback loops, callback deadlocks, and surface
  detach.

## Repository and licensing discipline

- Preserve unrelated user changes and keep experimental work out of verified
  commits.
- Do not commit SDK installers, binaries, build artifacts, logs, dumps,
  decompiled material, or third-party licensed material.
- Never commit or redistribute Avid SDK headers, libraries, examples,
  documentation, installers, or copied fragments. The repository contains only
  project-owned implementation and links to official acquisition sources.
- Keep private research and licensed reference material outside the repository.
- A hardware observation becomes a rule only after it is reconciled with the
  official model and verified across the available surface matrix.
