# UAD real-time update review — 2026-09-05

Scope: UAD observation, owner-thread delivery, desktop rendering, channel and
monitor write scheduling, plug-in units and recovery. This is a code review and
isolated-test result, not a claim of measured physical zero latency.

## Implemented

- Removed the observer's fixed 33 ms publication gate. Already available frames
  are consumed without waiting for a batch to fill, bounded by 256 frames or
  2 ms of processing before publication. The bound is a work budget, not a sleep.
- Meter-only updates preserve the channel/configuration snapshot and update
  existing channel or monitor legs in native dBFS. Other properties and discovery
  rebuild the model. No animation, interpolation or calibration offset is added.
- Publication calls a notification after releasing the snapshot mutex. Windows
  posts at most one pending observation message; its existing owner thread reads
  the latest snapshot and performs the existing EUCON Apply path. SDK callbacks,
  meter format, visibility handles and API ownership remain unchanged.
- The existing 33 ms timer remains for lifecycle/idle maintenance; fresh meter
  data no longer waits for it. Process-conflict checks use elapsed time rather
  than update counts, so faster meter traffic does not accelerate process scans.
- Removed the desktop's 250 ms redraw gate. Visible readings repaint on update;
  hidden windows retain current state without painting.
  Follow-up comparison requested by the user: desktop repainting is restored
  to a 250 ms interval in `artifacts/uad-ui-250ms/Release`. Incoming state,
  audio dispatch and EUCON feedback retain their immediate paths.
- Added five-second aggregate diagnostics for latest-receive-to-Apply and owner
  update duration. These timestamps start inside our receiver and do not measure
  source-generation, socket backlog, surface rendering or acoustic latency.

## Checks

Release and Debug core and transport tests cover subscription recovery, meter
edge notifications, notification callbacks reading snapshots without deadlock,
stable configuration identity across meter changes, native dB/stereo leg handling,
and external fader changes after optimistic feedback expires. Isolated Release
host build and desktop settings checks pass. The Release synthetic run measured
59 microseconds maximum from latest receive processing to notification; the
fixture edge-to-notification check includes a 40 ms fixture poll and test sampling
and is not a hardware benchmark. Repeat hardware timing under actual session load.

## Remaining findings

1. Follow-up: removed the channel/monitor per-address 10 ms rate gate and the
   post-switch 5 ms reply wait. Queued work dispatches immediately; only unsent
   intermediate positions coalesce. Reply checks are nonblocking between writes
   and during idle wakeups, including delayed replies after switches. Idle waits
   are interruptible by Submit. No pre-write discovery round trips were found
   on the live channel path. A Release synthetic run dispatched 32 consecutive
   channel/monitor pairs in 3922 microseconds; this is not device response latency.
   Regression tests cover delayed denial without retries for both controllers.
2. The observer still performs full discovery every ten seconds on the same
   connection. It processes unsolicited messages during reads, but metadata work
   can compete with meter processing. Large plug-in sessions need timing capture;
   a separate discovery connection would require snapshot reconciliation tests.
3. Desktop painting and EUCON Apply share the existing owner thread. Desktop
   painting is again limited to 250 ms at the user's request; this does not establish a bounded render cost.
   Compare visible/hidden-window timing and CPU use before claiming load stability.
4. Plug-in display uses UAD StringValue (dB, Hz, ms, ratio, modes); normalized
   values are not presented as invented percentages. Sample peak/clip feedback
   retains the engine's actual values and stereo leg identity.
5. Control selection remains active through transient connection failures and
   rebinds on fresh state. Driver-object reconstruction now renews observation
   subscriptions. Uncertain writes are not automatically replayed.

The follow-up isolated executable is under `artifacts/uad-immediate-control/Release`. User
verification on S3 and Avid Control, meter on/off edges, Console adjustments,
visible/hidden desktop, and driver recovery remains outstanding. No running
bridge, EuControl service, driver or audio setting was changed during those tests.

## CPU follow-up: unchanged-control fast path (2026-09-05)

The observer now stamps non-meter state independently of meter updates. After
controls settle, meter-only observations update the existing strip meter values
and batch writer without rebuilding channel/control/configuration feedback.
Control revisions, connection/metadata changes, authority epochs, dispatch counts,
pending writes and queued surface callbacks immediately retain the full path.
The 300 ms settling window requests additional full updates, never postpones an
operation; it preserves optimistic-feedback expiry. Unknown revisions use the
full path. Meter cadence and audio dispatch have no new wait or rate limit.

Official contracts reviewed: GettingStartedWithEuCon callback/threading sections
7.1–7.5 and Meter API 3.x lifecycle/visibility/batching sections; installed
EuBatchedMeterWriter declarations; EuConIO channel feedback, EuConApp batched
meter handling, and the older FAQ meter example as non-normative context.
SDK writes remain on the existing owner thread, callbacks remain queued, and
saved visibility handles/formats, scoped writer ownership and error handling
are unchanged. No node replacement or surface-specific branch was added.

Release host build and 12 desktop checks passed. Debug core passed 15,348 checks
and synthetic transport passed, including unchanged control revisions across
meter edges. Policy tests cover all stamp fields, callbacks, pending writes and
settling expiry. These are not physical-surface acceptance tests.

Local process CPU sampling (normalized across logical processors): the running
UI-250-ms build measured 4.91% over 15 seconds; the new build initially measured
1.45% over 15 seconds. A second 20-second sample measured 3.73%, with concurrent
surface fader events and dispatched writes recorded in the log. Thus 1.45% is
not a sustained operating-load claim. Same machine and existing device connections; not a
controlled DAW workload benchmark. UI painting remains at 250 ms in both builds.
The UAD bridge alone was normally closed and restarted from the isolated
`artifacts/uad-cpu-review/Release` output. Windows bridges and EuControl were
not restarted. Memory usage is not an optimization target at the user's request.

Physical S3 and Avid Control verification remains required: fader/touch/release,
external Console changes, plug-in controls, control room, meter onset/clip/stereo,
banking/focus and connection recovery. The measured CPU reduction does not prove
zero end-to-end latency or acceptance under a fully loaded session.

## Desktop repaint follow-up (2026-09-05)

At the user's request the desktop 250 ms gate is removed again, together with
synchronous UpdateWindow calls from observation updates. Meter-only state now
invalidates the channel list rather than the complete parent window. Non-meter
revisions, connection/freshness transitions and displayed application status
invalidate the parent normally. Windows coalesces pending paint requests without
an application frame timer. Minimized/hidden windows do not request live paints.
Owner-drawn channel rows compose their background, labels and meters in a memory
bitmap before one screen copy; the existing list double-buffer style remains.
This change affects presentation only, not transport, EUCON feedback or control.
Build output: `artifacts/uad-smooth-ui/Release`. Live flicker and CPU behavior need
user evaluation; automated core/settings tests do not validate visual smoothness.

Settings follow-up: custom toggles, combo faces and owner-drawn buttons now use
the same offscreen composition strategy as channel rows. Page rebuilding disables
parent redraw only while replacing and laying out children, then invalidates all
children once; hidden windows are not made visible by WM_SETREDRAW. No paint timer
or control delay is introduced. Output: `artifacts/uad-settings-paint/Release`.
Settings interaction flicker still requires visual acceptance on the user's display.
