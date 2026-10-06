# Windows fader topology/readback audit — 2026-10-06

## Evidence, not yet hardware acceptance

The running builds were Windows EUCON 1.0.2 and Mackie 1.0.1. The EUCON
trace contained only 100% targets for channel 1 during the inspected period;
it does not establish that the physical motor received/retained that position.
Mackie's normal trace did not contain successful motor packets. Consequently
neither a runtime defect nor a device defect is established by this audit.

Confirmed unsafe paths:

- EUCON registers/thaws new channels before setting their Windows volume.
- Every topology change and a new visible channel cause whole-node feedback
  replay, even when an existing Master has not changed.
- Mackie treats any selection/topology dirty flag as forced motor replay.
- A failed Core Audio volume read leaves the snapshot's default zero in place.
- A failed default-endpoint lookup loses the Master role; a failed activation
  during discovery can remove an endpoint that was actually enumerated.
- EUCON's last-sent cache cannot detect an SDK primitive index that subsequently
  differs from Windows. Rejecting a non-human callback does not restore that
  primitive's value by itself.

## Official contract reviewed before implementation

Concepts: processor lifecycle, assignment/visibility, primitive value,
callback threading, touch, and application-to-surface feedback.

Read the separately obtained Avid 2026.4 guide sections 3.6.1–3.6.2,
7.1–7.6, 12.4, 12.10 and 12.14; installed EuNode, EuCommon, EuProcessor,
EuControlFader, EuPrimitiveControl and relevant EuDefinitions contracts;
then EuConIO channel/registration/value code, EuConApp registration and the
ChannelVisibility FAQ example. No SDK material is copied here.

- Keep the registered top-level node and retained channel processors alive.
  Register initialized processors; remove them from the node before destruction.
- Bracket processor add/remove/reorder in one minimal node Freeze/Thaw scope.
  Reordering changes order/number, not identity. Check and log lifecycle results.
- Initialize primitive tables before reading/writing values. Set the current
  index using the same table coordinate as input. Set OnlySendIfDifferent before
  the first index write. SyncNode forces every assigned control; it is not a
  prerequisite of every ordinary track addition.
- Visibility belongs to the runtime, which may notify all tracks or only changed
  tracks. Recover the affected channel, not every channel solely because one
  new track became visible.
- Callbacks run on SDK threads: record/queue state only, never hold an application
  lock across an SDK call. Forced callbacks are not human audio commands.
- Motor feedback must not fight a touched fader or its release-tail input.
  Preserve deferred recovery until that individual channel is settled.
- Meter format setup stays after processor registration, as documented by the
  current meter API/example. This audit does not change metering or UAD models.

Microsoft GetMasterVolumeLevelScalar supplies a value on success, not a
zero-volume instruction on failure. Reference:
https://learn.microsoft.com/en-us/windows/win32/api/endpointvolume/nf-endpointvolume-iaudioendpointvolume-getmastervolumelevelscalar

## Implementation boundaries and verification plan

Hold the last successful, finite Windows scalar on temporary read failure;
never substitute a failed read with silence. Preserve endpoint identity/default
status when discovery/query fails rather than when absence is established.
Initialize new EUCON fader/knob/pan/mute before registration and thaw. Scope
topology/visibility replay to changed channels. Verify idle SDK index against
Windows without periodic motor writes; repair only mismatches outside touch
and pending writes. Mackie compares both packet and logical binding: repaint
changed bindings but do not replay unchanged Master on append/selection.

Add quiet, always-on motor target/transmit/touch diagnostics. No device-name
branch, guessed MIDI message, audio write, controller reset or runtime restart.
Pure tests exercise read failures, valid zero, topology append/remove/bank,
equal-volume identity replacement, touch exclusion and idle SDK index recovery.
Build both products with isolated EUCON output; hardware acceptance must cover
S3, Avid Control and the saved P1-Nano pair, particularly repeated app start/exit
and long idle. Software tests alone cannot certify the intermittent physical drop.

## Verification and local handoff

- Final local candidates: Windows EUCON `1.0.3-test.2`, Mackie `1.0.2-test.2`.
  These are not GitHub releases. Replaced the executables at their existing
  self-use paths; registry startup paths still resolve to those exact versions.
  No old executable backup was created. Existing settings/ports are preserved.
- Release pure tests: 18 groups / 240,851 assertions; preset checks: 319.
  EUCON isolated Release build and unregistered feedback self-test passed.
  Mackie smoke passed, zero MIDI sends and no open MIDI handles.
- Opt-in integration created only its own silent, nonpersistent session. Volume
  input, encoder volume, Windows-to-motor feedback, mute/unmute, stereo balance,
  push-to-center and rejection of a nonexistent track identity all passed.
  The first integration run found a fresh-read flag accidentally set on the
  cached-fallback branch; corrected it, added a fresh-read regression, rebuilt
  both products and repeated the integration before the final handoff.
- In the final live traces, the silent application's add/remove at roughly
  40.9–41.2 seconds caused no Master motor replay in either product. EUCON
  Master targets were all 1.0/index 728 during startup; dedicated MCU Master
  had one initial target 16,383 and none for that topology operation. The live
  MIDI/lifecycle logs contained no errors during this check. This is evidence
  of host output, not proof of the motor's physical position.
- UAD PID 5852, EuControl PID 4640 and MC_Client PID 24528 remained running
  with their original start times. Only the two in-scope bridge processes were
  restarted. No firmware, driver, runtime setting or iMAP edit was performed.
- The user tentatively reported that Master appeared fixed on the first
  candidate. The same Master/topology changes remain in the final candidate;
  full S3/Avid Control/P1-Nano and long-idle acceptance is still pending.

Retained readback is marked non-fresh: it may keep the display/identity stable
but must not drive a motor or expire an in-flight gesture as though Windows had
confirmed an old value. Live Windows volume requests remain immediate. Recovery
uses the existing touch/release-tail safety state, not a periodic motor timer.

## Patch release decision — 2026-10-06

After the hardware check request covering S3, Avid Control and P1-Nano, the user
reported that the fix appeared to work and requested publication. This is
preliminary acceptance, not a separate certification of each surface or of
long-idle behavior. Release Windows EUCON `1.0.3` and Mackie `1.0.2` from the
same functional changes as test.2, with final product/installer versions.
Preserve settings and existing local launch paths. UAD is not updated in this
release. Release notes describe the host-side safeguards without promising
that all possible causes of physical motor drift have been eliminated.

Final-version validation passed: isolated EUCON Release build and unregistered
feedback self-test; Mackie Release pure/preset tests and no-MIDI smoke. Installed
both final installers into disposable, explicit test directories, verified
embedded versions and SHA-256 identity with the built executables, then
uninstalled the test copies. The bundled Microsoft runtime's signature was
valid and the exact same runtime was already installed; cancelled its redundant
elevation requests during these package tests without changing that runtime.
Only the two bridge executables were replaced/restarted at their existing
self-use paths, now showing `1.0.3` and `1.0.2`. UAD, EuControl and MC_Client
retained their original processes. Settings and startup paths were preserved;
no obsolete executable backup was created.
