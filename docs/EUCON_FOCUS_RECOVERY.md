# Windows EUCON feedback recovery

2026-09-17 investigation: application switching reached WindowsFaderBridge in
EuControl's log, while unchanged motor targets could be discarded by the host's
lastMotorIndex cache even during fullRefresh. Attachment was the only explicit
full-refresh trigger. This explains a recovery gap, not a proven runtime defect.

Contracts reviewed: guide sections 7.1–7.5, 10.1–10.4 and 12.10; installed
EuNode::SyncNode, EuProcessor::OnProcessorCallback, EuDefinitions attribute and
force-update contracts; EuConIO ExTop, EuConApp ExNode and ChannelVisibility FAQ.
The runtime owns assignment. SyncNode republishes processor state to assigned
surfaces; it does not choose an application or write audio parameters.

Implementation plan: request recovery on desktop activation, the existing
summon completion, attachment, and a channel's transition to visible. Callbacks
only set atomics. On the owner thread, retain the request while a fader is
touched or an audio command is pending; reconcile latest Core Audio values,
bypass motor deduplication once, and SyncNode once after the model is current.
Keep ordinary change-driven feedback. Ignore FORCE_UPDATE notifications in all
input dispatchers so synchronization cannot execute audio or system commands.
Log requests, visibility, sync results and motor write results. Do not add a
periodic refresh, audio delay, node rebuild or device-name special case.

Physical verification must cover a long background stay, Windows/UAD switching,
unchanged levels, motor position, Pan, mute/solo/default/select LEDs, labels,
meters and touch protection on S3 and Avid Control. Pending verification.

## Local validation

- `scripts/build-eucon.ps1 -Validation` passed in isolated output.
- `WindowsFaderBridge.exe --feedback-self-test` exited 0. This mode constructs
  unregistered models only, before the application mutex, audio engine and UI.
  It checks request coalescing, visibility rising edges, normal mute/system
  dispatch, and suppression of forced feedback in channel/system/command paths.
- Installed the candidate at the existing self-use Windows EUCON path; retained
  a local executable/trace backup under artifacts/local-backups/2026-09-17-focus-recovery.
- Live trace: channel visibility changed 0 to 1, recovery republished all three
  current channel motor values; SetCurrentIndex, Refresh and SyncNode returned
  0. Subsequent visible=1 notifications did not trigger another recovery loop.
  This demonstrates API-path execution, not physical motor verification.
- UAD was still running the v1.0.0 executable although the current startup entry
  points to v1.1.0. Restarted only that bridge into the existing v1.1.0 executable;
  EuControl, Mackie, drivers and UAMixerEngine were not restarted or modified.
- Prepared for the Windows EUCON v1.0.1 patch release after local runtime
  validation. Physical S3 and Avid Control verification remains part of the
  release acceptance record.

## Channel-list recovery follow-up (2026-09-17)

Concepts: processor lifecycle and application-to-surface feedback after channel
addition, removal or reordering. Read guide 3.6.1-3.6.2, 7.1-7.5, 12.4, 12.10
and 12.14; EuNode Freeze/Thaw/SyncNode declarations, visibility attributes,
EuConIO and EuConApp ExTop, and the ChannelVisibility FAQ.

Keep the application node and retained channel processors alive. The owner
thread brackets a batch of topology changes with one Freeze/Thaw. Visibility
describes whether a track is present on a surface, not whether it has been
reassigned to another physical strip: an existing visible track need not have
a new false-to-true transition. After Thaw, explicitly request the existing
one-shot full feedback recovery. Log the Thaw return code. The recovery remains
pending through active touches and pending writes, then republishes current
audio values and calls SyncNode. Do not set visibility attributes ourselves,
reset touch state, write audio values, rebuild the node or add a periodic timer.

This addresses a missing host recovery trigger, not a proven SDK defect.
Physical verification: keep an input at 100%, add/remove an audio application,
and check retained faders and labels on S3 and Avid Control. Repeat while a
fader is touched and verify recovery after release. Verification pending.

Local candidate validation: isolated build and existing unregistered SDK
feedback regression passed. A two-second PCM-zero test session added a fifth
track to the four-track live model, then exited. Both 4-to-5 and 5-to-4 topology
changes produced the new recovery request, republished the unchanged input
fader target (1.0000 / index 728), and completed SyncNode with result 0.
All logged motor SetCurrentIndex/Refresh results were 0. No audio volume was
changed by the test. The trace was retained in the local topology-recovery
backup directory. S3 motor position and Avid Control verification, including
touch/release during application add/remove, still require user confirmation.
This is a local candidate; the published v1.0.1 installer is unchanged.
