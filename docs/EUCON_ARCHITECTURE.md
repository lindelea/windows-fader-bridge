# Generic EUCON architecture

## Goal and boundary

Windows Fader Bridge is a EUCON-aware application adapter for the Windows
per-application audio mixer. It models Windows mixer tracks through the EUCON
SDK; it does not discover or drive any physical surface.

```text
Windows Core Audio
        <->
Windows mixer track model
        <->
Fader Bridge EUCON adapter
        <->
EUCON runtime and assignment layer
        <->
EuControl (S1, S3, Dock, Avid Control)
or WSControl (S4, S6)
        <->
Compatible EUCON surfaces
```

EuControl, MC_Client, WSControl, and the rest of the Avid EUCON runtime own
application focus, workstation and surface attachment, physical assignment,
banking, layouts, capability differences, and device communication. Fader
Bridge owns only the Windows mixer model and its bidirectional EUCON state.

The Avid EUCON SDK 2026.4 documentation, installed headers, and unmodified
example projects are authoritative. Device observations are validation
evidence, not production architecture rules.

## Official model invariants

1. Initialize `EuConManager` before creating EUCON objects and destroy it only
   after the registered model has been removed.
2. Register one long-lived top-level `EuNode` for the application. Multiple
   nodes per application are not supported by the documented application model.
3. Represent mixer tracks as `EuProcessor` channel strips with stable,
   application-defined persistence IDs.
4. Use standard processor types, layout rules, layout names, track types, and
   channel order attributes. EUCON owns physical assignment and banking.
5. Modify the live model by freezing the smallest appropriate parent, changing
   processors or controls, and thawing it. Never rebuild the application node
   because the Windows session list changed.
6. Keep EUCON callbacks short. Queue application work to its owning thread and
   avoid locks that may also be held while calling the EUCON API.
7. Treat meter visibility handles as dynamic assignment state. Save callback
   data and send batched values only for visible meters with valid handles.
8. Never infer a physical strip count, device model, or control placement in
   the Windows mixer or EUCON application model.

## Logical track model

A Windows application is one logical mixer track even when it owns multiple
Core Audio sessions or restarts its processes. Its stable application key is
the EUCON persistence identity. Core Audio slot numbers are transient routing
details, not EUCON track identity.

Windows audio endpoints are also logical mixer tracks. Their persistent
identity is the Windows endpoint ID, never a transient slot or a physical
EUCON strip. Only `DEVICE_STATE_ACTIVE` endpoints are published; this includes
devices Windows describes as ready and excludes disabled devices. Render
endpoints use the standard Monitor track type, the current default render
endpoint uses Master, and capture endpoints use Input.

Endpoint channels expose the same standard volume, mute, and meter controls as
app channels. Their standard channel Record Arm placement is intentionally
reinterpreted as a one-of-N default-endpoint selector within each Windows data
flow. It is initialized as a one-shot request control with an application-owned
LED, not as a toggling state switch. Pressing Rec queues a Windows
default-endpoint request; the authoritative Core Audio default state then
drives every Rec LED. Default state never participates in channel ordering.
The endpoint ID keeps the same Processor, layouts, direct assignments, and
banked identity stable when the default changes.

Every active capture endpoint maintains its own lightweight shared-mode WASAPI
capture stream when the endpoint has no hardware peak meter. The worker drains
and discards capture packets only to keep Windows' software endpoint meter
active; audio data is never retained. Consequently, every enabled input track
meters independently whether or not it is the default capture endpoint. This
was verified with two simultaneous capture endpoints on 2026-08-29. Default
selection remains exclusively Rec/LED state and never gates metering.

Endpoint enumeration, volume, mute, metering, and default-change notification
use documented Windows Core Audio interfaces. Microsoft does not publish a
desktop API for changing the default endpoint. That write is isolated behind
the `IPolicyConfig::SetDefaultEndpoint` compatibility boundary and applies the
Console, Multimedia, and Communications roles together. Failures must not
modify EUCON state optimistically; the next endpoint frame remains authoritative.
On 2026-08-29, capture-endpoint selection from Rec and external Windows default
selection were verified in both directions: all three roles returned `S_OK`,
Windows Settings followed the surface selection, and the authoritative default
state drove the mutually exclusive Rec LEDs. Diagnostics present logical EUCON
channel order and never label a transient Core Audio slot as a channel number.

Each track exposes standard EUCON semantics where applicable:

- channel-strip processor and audio track type;
- stable persistence ID and independent channel order;
- fader, touch, mute and mute LED;
- name and number displays;
- level meter with negotiated format;
- standard knob sets or knob maps only when their semantics match.

## Application channel knob sets

Application tracks publish one top-level `EuControlKnobCellArray`. The node
declares `kSupportsNumberOfChildrenAttribute`, and every top-level cell reports
its actual child count. Standard Input, Pan, and Mix pages retain their EUCON
semantics for Windows per-application input routing, channel balance, and output
routing. Inserts, Dynamics, EQ, Aux Send, and Group are blank because Windows
does not supply those channel semantics.

Application-specific controls use the documented optional second page:

- knob set 9 (`Media`) uses
  `GlobalSystemMediaTransportControlsSessionManager` and exposes play/pause,
  previous, next, stop, normalized timeline position, shuffle, and repeat;
- knob set 11 (`Quick`) retains session volume and adds volume/pan reset,
  default input/output routing, unmute, and clear-solo operations;
- knob set 13 (`Window`) exposes focus, minimize, maximize/restore, and
  topmost state through Win32 window APIs.

The knob sets belong to the logical application processor, not an S3. EUCON
owns their placement on S3, S4/S6, S1, Dock, and Avid Control. Knob-cell lower
switch LEDs are application-owned feedback: window and GSMTC state are sampled
only for the attentioned/selected application at 200 ms intervals. Media
sessions are matched to the selected application by package family or executable
identity; Fader Bridge never falls back to an unrelated system-current session.
Unsupported GSMTC capabilities reject the command instead of simulating a
keyboard action or displaying false state.

The initial custom-page implementation builds and starts successfully with the
Windows 11 26100 C++/WinRT projection and EUCON SDK 2026.4. Physical S3 and Avid
Control navigation, switch placement, LEDs, and media-session matching remain
required verification before this becomes a committed baseline.

## Application commands and Windows mono audio

Windows' global accessibility mono mix is application-wide state, not a
channel-strip or monitor-room control. The EUCON SDK defines no standard Mono
Audio or Mix-to-Monitors layout name. Fader Bridge therefore publishes it by
the documented application-command model:

```text
Key Commands -> Windows Audio -> Mono Audio
```

`Mono Audio` is a one-shot `EuControlSwitch` in a command processor. Its
callback only queues work to the Windows audio worker. The worker toggles the
Windows setting named
`SystemSettings_Accessibility_IsAudioMonoMixStateEnabled` through the same
`SettingsHandlers_Accessibility.dll` data-model object used by Windows
Settings; no audio-service restart is allowed. Microsoft publishes no desktop
API for this switch, so the dynamically loaded `GetSetting` /
`SystemSettings.DataModel.ISettingItem` path is an isolated Windows-internal
compatibility boundary. The authoritative handler value is read on audio
frames and drives an application-owned EUCON LED, so a change made in Windows
Settings also updates the assigned surface key.

EuControl owns physical placement. A user may assign this command to any key
that the EuControl Soft Keys editor exposes. Production code must not name an
S3 or claim a fixed key directly. On the tested S3, Mix to Mons is a fixed
onboard monitor/talkback control and is not assignable; Clear Solo was used as
the temporary test key.

On 2026-08-29, S3 button presses changed the real Windows audio engine between
stereo and mono, a professional stereo balance meter confirmed the processing,
and the assigned LED followed in both directions. Windows Settings changes also
drove the LED. An already-open Windows Settings page can retain a stale visual
toggle after an external command even though the handler value and audio engine
have changed; this is treated as a Windows UI-cache limitation, not audio state.
Avid Control command placement and feedback remain to be verified.

## Assignable Windows command catalog

The command processor now follows the complete documented three-level model:

```text
Key Commands -> category switch array -> one-shot command switch
```

The existing `Windows Audio` persistence IDs remain unchanged. Fourteen additional
categories publish 187 commands, for 189 assignable commands in total.
Every category and command has an explicit globally unique, versioned
persistence ID. These IDs are independent of display names and must never be
renamed after release because EuControl stores them in user application sets.

Stateless commands use the SDK's documented `kSWITCH_OneShot` behavior and let
the switch own its momentary LED. Mono Audio and Clear Solo remain
application-owned LEDs because they reflect authoritative Windows state. The
EUCON callback performs no Windows operation: it posts a typed command to the
Win32 application thread, which invokes the corresponding Shell API, official
`ms-settings:` URI, Known Folder API, foreground-window API, or `SendInput`
shortcut.

The catalog deliberately omits shutdown, reboot, sign-out, file deletion,
formatting, service mutation, and other high-impact actions. See
`WINDOWS_COMMANDS.md` for the category inventory and execution contract.

## Foreground switching between bridge applications

Concepts: application focus, command processor lifecycle, soft-key persistence,
and callback threading. The implementation follows GettingStartedWithEuCon
sections 3.5.4, 10.1, 10.5 and 11.5.5, the installed `EuProcessor`,
`EuControlSwitchArray`, `EuControlSwitch` and `EuPrimitiveSwitch` declarations,
then the EuConIO command example before EuConApp.

EUCON still determines the controlled application from the actual topmost
Windows application; there is no processor-side focus override. Each bridge
therefore registers a distinct global shortcut and accepts a private summon
message. Windows EUCON defaults to `Ctrl+Alt+Shift+W`, UAD EUCON to
`Ctrl+Alt+Shift+U`, and Mackie Control to `Ctrl+Alt+Shift+M`. Users can record a
replacement combination. `RegisterHotKey` is authoritative for conflicts; a
failed replacement restores the previous registration and settings.

The two EUCON adapters publish `Key Commands -> EUCON Applications` with three
one-shot commands. Callbacks queue to the existing owner thread before sending
the target message. They do not synthesize keyboard input, share an application
model, launch a stopped process, or branch on the attached surface. The UAD
command processor is created under the initial node freeze, registered before
the node, kept for the adapter lifetime, then unregistered before node teardown.
All processor, container and command persistence IDs are globally unique and
versioned.

The EUCON editions optionally hide 400 ms after being summoned, allowing
EuControl's documented OS focus tracking to observe the real foreground change
without leaving the bridge over the working application. Mackie Control remains
foreground because it has no EUCON focus policy. Exact switching behavior and
saved soft-key assignments require S3 and Avid Control verification.

## Application Solo and Clear Solo

Application tracks expose the standard `EuLayoutChannel::kNAM_Solo` switch as
a two-state momentary latch. Windows Core Audio has no per-session Solo API, so
the audio worker implements single-target intercancel Solo: the selected
application is made audible and every other active application session is
muted. Endpoint tracks are deliberately excluded; Solo never mutes a render or
capture device.

At the start of a Solo session, the worker saves each application's existing
mute state by stable application identity. Pressing the same Solo again, or
invoking Clear Solo, restores those saved states instead of blindly unmuting
everything. Changing directly to another Solo target retains the original
snapshot, unmutes the new target, and mutes the former target. Applications
that appear while Solo is active are incorporated into the same policy without
changing EUCON processor identity.

Clear Solo is published in both documented forms:

- the standard `EuLayoutSystem::kNAM_ClearSolo` one-shot switch on the single
  System Processor;
- an assignable `Key Commands -> Windows Audio -> Clear Solo` command.

Both controls queue the same worker operation. Their application-owned LEDs
remain on whenever any application is soloed, as required by guide section
12.5. Channel Solo state and all Clear Solo LEDs are driven from the worker's
authoritative audio frame rather than from an optimistic surface press.

On 2026-08-29, the user verified channel Solo, direct intercancel switching,
same-channel restore, Clear Solo, mute restoration, and LED feedback on the
attached EUCON setup. A wider surface regression matrix remains desirable.

## Application Pan / Windows stereo balance

Stereo application sessions expose a dedicated predefined Pan knob set using
`EuLayoutChannel::kNAM_Pan` and the standard `Avid.Chan.Pan` function
persistence ID. The knob-cell array follows the documented
`Freeze()` / `PushBack()` / `Thaw()` lifecycle, and its centered position ring
and `-100..+100` table implement the documented EUCON pan semantics. Surface
callbacks only publish the stable track identity and requested value; the Core
Audio worker owns all Windows writes.

Windows does not expose a DAW-style stereo routing panner for an existing audio
session. `IChannelAudioVolume` supplies independent session-channel gains and
Microsoft explicitly describes it as suitable for stereo balance controls.
The adapter therefore presents one accurately labelled Pan/Balance parameter:
center writes `L=1, R=1`, a left move retains `L=1` while attenuating R, and a
right move retains `R=1` while attenuating L. This avoids the unwanted center
attenuation of an equal-power mono panner and keeps the existing
`ISimpleAudioVolume` fader as an independent multiplicative master level.
Following guide section 14.7.1, the knob-top switch is a raw momentary input:
its press state requests the parameter default, immediately returns the EUCON
ring to `Center`, and queues `L=1, R=1` to the Core Audio worker. The release
state performs no action.

Only application sessions whose `IChannelAudioVolume::GetChannelCount()` is
exactly two receive the Pan knob set. Endpoint, mono, and multichannel tracks
do not receive a fabricated panner. The control intentionally has no
`EuLayoutPan` mono/stereo position tag: one Windows balance value is not the
pair of left/right input panners required by that semantic contract. Channel
volume notifications trigger immediate reverse synchronization; the balance
is reconstructed from the L/R ratio so a common per-channel attenuation is not
misread as pan.

The implementation builds successfully against EUCON SDK 2026.4. On
2026-08-29, the user verified the standard Pan function, Windows stereo balance
result, ring feedback, bidirectional synchronization, and knob-press Center
reset on S3. Avid Control remains to be explicitly regression-tested.

Stereo render endpoints use the same EUCON Pan/Balance model through
`IAudioEndpointVolume`. Unlike application-session channel factors, endpoint
channel scalars are the absolute audio-tapered values displayed as the Windows
left/right device sliders. The worker therefore keeps the louder side at the
current endpoint Master value and attenuates only the opposite side; Pan cannot
raise either leg above Master. Capture endpoints and non-stereo render
endpoints remain excluded. On 2026-08-29, the user verified Master Pan,
knob-press Center reset, Windows left/right UI linkage, and bidirectional
feedback on the active Universal Audio Thunderbolt WDM output.

## True per-leg metering and mono format

Meter API 3.1 is the primary meter path. Each live track declares its current
format with `EuControlMultiMeter::SetFormat()` after Processor registration.
The adapter saves the format and visibility handle supplied by
`VisibilityChangedV2`, then writes only visible meters through one
`EuBatchedMeterWriter` per update cycle.

Windows session and endpoint meters are read per channel. Endpoint mix-format
channel masks are translated to the corresponding EUCON meter roles, and
multiple Windows sessions belonging to one logical application are combined by
taking the maximum value for each leg. When Windows mono audio is enabled, each
track is re-declared as one `kMTR_Mono` leg and sends the maximum source level;
when mono is disabled, the real channel roles and leg count are restored.

On 2026-08-29, dynamically registered tracks received valid Meter API 3.1
visibility handles in the attached EuControl setup and every observed batched
write returned success. The user verified true multi-leg meters and the dynamic
stereo/mono display. The ordinary primitive write remains an isolated startup
fallback for the interval in which a track has no valid visibility handle; it
is not the application model.

## Application Select and attention

Application channels expose the standard `EuLayoutChannel::kNAM_Select` as a
two-state `kSWITCH_MultiState`. Windows has one foreground application, so the
adapter gives the switch application-specific intercancel semantics: Select on
restores and activates the matching application window, while Select off
minimizes it. Selecting another channel clears the previous Select state without
minimizing the previous application.

The audio-session process is not assumed to own the visible window. Window
resolution first matches a session PID, then a package family (for packaged
applications such as Apple Music), then the executable path (for browser audio
worker processes). Only visible, non-cloaked, non-tool top-level windows are
eligible. A bounded restore handshake confirms that `IsIconic` is false before
foreground activation. If the ordinary foreground request is rejected, the UI
thread temporarily attaches to the existing foreground/target input queues,
uses the documented top-level activation calls, and immediately detaches; it
does not synthesize keyboard input or leave the target topmost.

The node also receives the surface-owned `kATRIBID_AttentionedTrackPID`
attribute documented by the installed SDK. Attention callbacks copy only the
persistent ID and post work to the Win32 UI thread. Initial attention published
during surface attachment is suppressed because it is synchronization state,
not a user request to activate a Windows window.

On 2026-08-29, the user verified application matching, Select LED intercancel,
activation of obscured windows, repeated minimize/restore, and packaged/Win32
window handling on the attached S3 setup. Avid Control remains to be explicitly
regression-tested for this workflow.

## Current conformance audit

| Area | Status | Notes |
|---|---|---|
| Single persistent application node | Conformant | Registered once; live application changes do not replace it. |
| Dynamic channel processors | Conformant | Active applications are added and removed inside node `Freeze()` / `Thaw()`. |
| Standard channel model | Conformant | Channel-strip processor, `EuLayoutChannel`, audio track type, standard fader/name/number/meter controls. |
| Stable persistence IDs | Conformant | Derived from Windows application identity rather than process ID. |
| Channel color | Verified | `kATRIBID_ChannelColor` is populated with `0x00RRGGBB` metadata. Packaged apps use their manifest-declared logo rather than an audio helper executable; Win32 apps use the executable icon. Dominant chromatic colors are normalized for LED visibility, meaningful white/grey foregrounds are preserved, and extraction failures use a stable identity palette. S3 rendering was verified with Apple Music, Chrome, PotPlayer, and foobar2000. |
| Surface-independent capacity | Conformant | Only active processors are registered; the Core Audio ceiling is not a surface strip count. |
| Callback thread discipline | Mostly conformant | Core Audio writes are queued; remaining SDK calls from callbacks require review. |
| Track identity and ordering | Verified | Channel Processors are keyed by stable application identity. Mutable atomic routes target the current Core Audio slot; reordering retains the Processor and changes only `ChannelOrder` and channel number as specified by guide section 12.4. Verified smooth and near-zero-latency on the attached S3/Avid Control setup. |
| Default endpoint selection | Verified | Active render/capture endpoints retain stable Processor identity. Rec is a one-shot request, Windows default state owns its LED, and Console/Multimedia/Communications roles switch together. Capture selection and reverse synchronization were verified on Windows 11 with S3. |
| Windows mono command | Verified on S3 | A standards-based assignable command queues the Windows setting handler and Windows state owns its LED. Clear Solo press/LED and real mono processing were verified; Mix to Mons is fixed and unavailable for assignment. Avid Control remains to be tested. |
| Assignable Windows commands | Awaiting surface verification | The single documented Key Commands processor exposes 15 stable categories and 189 commands. EUCON callbacks post typed work to the Win32 owner thread. SDK initialization returned no errors; Settings URI, system executable, Known Folder, application-summon and general SendInput paths passed local tests. File Explorer, taskbar, input-language assignment and invocation, plus labels and momentary LEDs, remain to be verified on S3 and Avid Control. |
| Application Solo / Clear Solo | Verified | Application channels use standard Solo semantics; the Windows worker performs single-target intercancel muting and restores the pre-Solo mute snapshot. The standard System Clear Solo and an assignable command share authoritative state and LED feedback. Endpoint channels are excluded. |
| Select / Attention | Verified on S3 | Standard channel Select drives one-of-N Windows application activation; off minimizes the selected application. Surface-owned AttentionedTrackPID is consumed on the UI thread. PID, package-family, and executable matching cover helper/worker processes without device-specific code. |
| Application and output Pan / balance | Verified on S3 | Stereo application sessions use independent `IChannelAudioVolume` factors; stereo render endpoints use absolute `IAudioEndpointVolume` channel scalars capped by Master. Both expose the predefined Pan knob set and knob-top Center reset. Capture and non-stereo tracks are excluded. Avid Control remains to be tested. |
| Application custom knob sets | Awaiting hardware verification | Custom pages 9/11/13 expose Media, Quick Controls, and Window operations. Media cells are generated from each GSMTC session's advertised read/write capabilities using the documented knob-array `Freeze` / `Remove` / `PushBack` / `Thaw` lifecycle. A readable but non-seekable timeline is retained as a read-only position cell; unsupported actions are absent. Metadata is fetched asynchronously and published as title/artist label cells. Session matching prefers the Windows current session and then active playback among identity-matched sessions; no unrelated global-session fallback is allowed. |
| Desktop application shell | Locally verified | `WindowsFaderBridge.exe` is a per-monitor-DPI-aware, single-instance native tray application. Its Overview, General and About shell shares the Mackie edition's desktop information architecture and neutral component tokens while retaining the EUCON purple accent and omitting MIDI-only settings. Simplified Chinese and English cover the complete shell, tray, dialogs, and shortcut recorder; the selected language is persisted per user. The status window is optional UI around the process-lifetime EUCON node: close/hide does not destroy the adapter, while explicit tray Exit does. Per-user startup uses the standard HKCU Run value with `--background`; no service, elevation, driver, or scheduled task is introduced. |
| Volume knob semantics | Needs work | Volume currently borrows the predefined Input knob-set layout. Confirm the correct standard model or use an official knob-map strategy. |
| Meter API 3.1 | Verified | Windows supplies true per-leg peaks and endpoint channel roles. Tracks declare dynamic stereo/mono formats, save `VisibilityChangedV2` handles, and use `EuBatchedMeterWriter`; observed batched calls returned success. The ordinary write is isolated to startup/no-handle compatibility. |
| Device-derived behavior | Needs review | Forced refresh and overtravel rebound came from hardware testing. Classify them as generic application policy or remove them after cross-surface tests. |
| Optional global processors | Partially conformant | The single System Processor now exposes documented Clear Solo semantics. Add Project, Monitor, Transport, or Assignable Knob only when Windows supplies a matching standard concept. |

## Migration rules

- Keep commit `e120721` as the verified rollback point.
- Change one EUCON contract at a time; do not combine topology, metering, and
  motor-feedback changes in one hardware test.
- Validate every accepted change on both S3 and Avid Control. More test devices
  extend the matrix without adding device branches to production code.
- Device names belong in tests and compatibility notes only. Production logic
  is expressed using EUCON model semantics and negotiated capabilities.

## Planned sequence

1. Hardware-verify the stable Windows mixer track registry and mutable Core
   Audio routing on both S3 and Avid Control.
2. Audit attributes and knob modeling against the complete guide sections and
   current installed header contracts; use official examples only as supporting
   evidence.
3. Move remaining SDK output out of callback threads.
4. Verify Meter API 3.1 negotiation on additional surface/runtime combinations,
   then retire the ordinary startup fallback when no supported setup requires
   it.
5. Run the same regression matrix on S3 and Avid Control: discovery, banking,
   assignment/layout recall, fader/touch/motor, encoder/touch/ring, mute/LED,
   labels, and peak meters.
