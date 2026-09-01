# UAD Console Bridge for EUCON — desktop implementation

## Scope and naming

The public product name is **UAD Console Bridge for EUCON**. The window uses
**UAD Console Bridge** and **FOR EUCON**. This is an independent application, not
an official UA or Avid application. No vendor logos, SDK assets or example code
are included. The generated icon depicts three independent faders, not a letter.

The desktop is a presentation layer over the existing observer and controllers.
The previous dashboard remains available only through `--diagnostics`.
The main view is status-only. It does not contain audio controls or permission
unlock buttons. English and Simplified Chinese are available in Settings.

## EUCON change contract (before editing the displayed application name)

Concept: node naming, not node identity or callback behavior. Consulted the
separately installed GettingStartedWithEuCon guide section 6 (initialization and
singleton), section 7 (callback ownership/threading), EuNode naming declarations,
then EuConIO ExTop and EuConApp ExTop initialization ordering.

- Set human-facing names during the existing initial node Freeze/Thaw, before
  RegisterNode. Keep the established persistent ID to preserve layouts.
- The node and its controls remain owned by the adapter; the desktop owns none.
- All adapter calls remain on the existing main owner thread. Callbacks still
  queue work and never access window controls.
- Preserve error checks and exception cleanup. No runtime, driver or surface
  restart is introduced. Suspending control revokes write epochs; it does not
  recreate the node or change surface assignment.
- A name change still needs S3 and Avid Control verification. Not yet verified.

## Permission and protection policy

Saved preferences are not active permissions. Four independent scopes are
channel control, control room, sensitive channel operations, and CONFIG.
Profiles: read only, mixing, full control, custom. Full means all **implemented,
allowlisted** operations, not arbitrary hardware commands. CONFIG remains
experimental. No preset files are overwritten.

Default: read only, no login startup, no permission restoration. Background close
and automatic EUCON publication are enabled. Automatic publication does not arm
controllers. Permission restoration is opt-in and requires the saved interface-system
identity at startup. After explicit activation, the selected access remains active
for the session: transient disconnects and individual failures discard pending work
and automatically rebind to fresh state. Granting or rebinding permission sends no
hardware write. Identity, type and explicit readback checks validate each command;
they no longer silently downgrade the user's access profile.

Runtime access audit:

- Only an explicit switch to read-only, Suspend Control, process exit, or a new
  unconfirmed interface-system identity revokes the user's authority.
- A stale/disconnected UAD snapshot suspends writers and drops queued gestures.
  Fresh state rebinds the selected scopes without replaying a command.
- A rejected write, failed readback, CONFIG timeout, channel capability change,
  or monitor-model refresh clears only that operation/controller instance; the
  desktop restores it from fresh state while the session authority remains set.
- EUCON adapter failure destroys its live processors and writers. Manual Retry
  creates a new adapter; the previously confirmed session authority is retained.
- Input/output routing changes deliberately enter a tracked transition with no
  surface callback epoch. The old gesture cannot cross the route boundary, and
  automatic restoration does not bypass the confirmation phase.
- CONSOLE exposes real interface settings only. The former `ACCESS` status cell
  was removed; permission state belongs exclusively to desktop Settings.
- Channel and monitor queues coalesce repeated gestures per control and use one
  serial writer each. Audible continuous controls dispatch from validated live
  state at no more than 100 Hz, without per-step discovery/readback transactions;
  Console subscriptions reconcile final state. CONFIG permits one fully confirmed
  pending operation. Uncertain writes are never retried, so reconnect/recovery
  cannot produce an API request storm.

The monitor ceiling is configurable from -96 to 0 dB. Default: -20 dB. It clamps
future bridge level requests. Changing it never sends a level request. A current
level above the ceiling does not block non-level controls or permission activation;
the next bridge level target is clamped. Console and physical hardware remain outside
this ceiling. It is not an SPL limiter. Legacy diagnostic arm calls retain their
original arm-time ceiling.

Settings: `%LOCALAPPDATA%\UAD Console Bridge\EUCON\settings.json`, strict versioned
JSON, bounded reads, atomic replacement. Invalid settings fail closed without
overwriting the original. Logs use the same product folder under `logs`.
Old Apollo Bridge logs are retained in their previous folder.

Login startup uses only the current user's named Run entry and a quoted exact
executable path. It does not launch Console, EuControl or any driver. Tray Quit
revokes permissions and shuts down the adapter. Closing the window follows the
saved background preference. A missing tray never leaves the app inaccessible.

## Offline UI verification

`--ui-preview` skips the observer and never initializes EUCON or a control
transport. `--preview-connected` is allowed only with `--ui-preview`; it supplies
clearly labelled synthetic status data. Settings in preview are memory-only;
startup, permission, file and hardware state are not changed.

Initial overview/settings validation on 2026-08-31 (before the channel-list revision):

- Release and Debug native builds pass in the isolated `uad-console-bridge-ui`
  output. The live Windows Fader Bridge executable was not overwritten.
- Core regression: 15,258 assertions pass in each configuration.
- Synthetic transport regression passes in each configuration, including
  explicit 0 dB permission ceiling, lower-ceiling rejection without a write,
  bounded subsequent level commands, readback, disconnect and no-retry guards.
  Only ephemeral loopback test servers are used.
- Desktop settings regression: 12 checks pass per configuration. It covers
  round-trip persistence, atomic replacement, preserving the original under a
  sharing violation, temporary-file cleanup and quoted startup command paths.
  It never modifies the real startup registry. The native build script now
  runs this test automatically.
- Offline smoke passes with all controllers locked, no SDK initialization and
  no network/audio/MIDI activity.
- Windows visual and interaction QA: Chinese and English, dark combo menus,
  keyboard navigation, switch rendering, client size down to 1020 × 710,
  ceiling input rejection above 0 dB and successful preview save at 0 dB.
  Closing with background enabled keeps the preview process alive; launching
  again restores the same window/process. Disabling background and closing
  exits. The connected-looking preview has no TCP connections.
- The SDK text/model regression remains blocked by the already-running Windows
  EUCON bridge. Its safety guard was respected; no user application or runtime
  was stopped to run it.

### Read-only per-channel restoration

The initial overview incorrectly reduced channel status to category totals.
The main window now retains both totals and a scrollable, read-only channel list:
identity/name, interface/type, mono/stereo, fader dB, independent stereo pans,
native signal/peak dBFS, mute/solo, UAD REC/MON and output. The list uses the
existing logical surface order and excludes the monitor strip. Missing fields
stay unknown, and stale/offline snapshots clear the rows. Selection is local
reading focus only; no UI notification sends an audio, EUCON or transport command.

The implementation uses a native owner-data, owner-drawn report list. A fresh
window receives its item count even when the cached channel topology is unchanged
(for example, returning from Settings). Live refresh preserves scrolling and
matches reading selection by stable channel key when topology changes. The full
row summary is provided through LVN_GETDISPINFO for accessibility. Default client
size is 1080 × 820; minimum size remains 1020 × 710. Native keyboard paging remains
available. No SDK model, callback or publication code changed in this revision.

Revision validation on 2026-08-31:

- Release native build and core tests pass using the installed MSVC 14.44.35207
  compiler and Windows SDK 10.0.26100.0, in `uad-console-bridge-channels` output.
  The normal MSBuild invocation is currently blocked by sandbox SDK discovery
  and an inherited duplicate PATH entry; no permissions or installed tools were
  modified. Direct compilation used the project source list, SDK include/library
  locations and native resource file.
- Core: 15,280 checks pass, including 22 new presentation checks for mono/stereo,
  meter units, REC/MON, missing/invalid data, disconnection, a 64-channel list,
  monitor exclusion and column widths. No sockets/audio/MIDI/EUCON initialization.
- Desktop settings: 12 checks pass; offline smoke passes with permissions locked.
- The current desktop automation session did not approve access to the preview
  application. Actual list rendering, scrolling, bilingual layout, returning
  from Settings and DPI behavior remain unverified visually. The prior screenshot
  in `images/uad-console-bridge-ui-preview-zh.jpg` depicts the initial overview,
  not the revised channel list. It has not been presented as a new screenshot.
- No live application was stopped or replaced; no hardware controls were used.

Remaining acceptance: actual S3 + Avid Control publication/naming and feedback;
fresh/reconnected interface permission workflows; real Config/device writes;
Windows login-startup execution, tray menu/Explorer recovery, mixed-DPI displays,
and screen-reader/high-contrast coverage. No commercial-release readiness is
claimed before this acceptance. Software permission tests are not a substitute
for studio monitoring safety checks.
