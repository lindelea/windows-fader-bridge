# Apollo Bridge research

Status: opt-in ordinary-channel control and independently locked control-room
validation checkpoint, not a production controller. The chronological contracts
below retain earlier read-only stages; the latest control-room contract supersedes
the previous MONITOR-as-channel projection.

## Product boundary

Apollo Bridge for EUCON is a separate executable, application identity and
output directory. The existing Windows Fader Bridge editions are untouched.
A future Mackie Control edition may reuse `ApolloBridge.Core`, never the EUCON
host or its lifecycle. No DAW, MIDI translator, audio driver or audio processing
is inserted into the audio signal path.

The UA interface is private and version-sensitive. It is not a supported UA SDK.
Apollo Bridge is an independent project, not an Avid or Universal Audio product.

## Native Inserts Config hierarchy and plug-in power

The complete official Knob Sets and plug-in configuration sections were
rechecked before changing the model. Inserts Config is a hierarchy, not one
large value table: a slot enters a folder/category page, a category enters a
plug-in page, and the surface performs automatic pagination according to its
physical knob count. The first project-owned choice is an explicit `NONE` cell;
all remaining choices are one plug-in per cell. No surface width is assumed and
Page/Back remain runtime-owned navigation.

The installed SDK headers and official advanced example also confirm that the
lower switch of a loaded insert uses the momentary/latch switch contract for
plug-in enable/bypass. Application feedback sets both switch state and LED from
the engine's `Power` value. Callback work remains queued to the owning thread.
Loading or unloading an insert legitimately changes the child knob model; an
already granted channel permission is now re-armed only when connection
generation and stable channel identity are unchanged. Reconnects, routing/type
changes, or a different channel still revoke permission.

Physical S3 verification showed that the documented third-level folder child
was published and its knob-top gesture reached EUCON, but the surface did not
navigate from the folder display to the plug-in display. The production model
therefore keeps the category information without depending on that unverified
depth: each category is a filtered selector knob on level 2. Turning previews
only that category, the lower `In` switch confirms the selected plug-in, and
`NONE` remains the first fixed unload cell. Automatic Page and Back navigation
remain runtime-owned.

Read-only inspection of the locally running UA engine exposed 223 catalogue
entries in this test installation: 127 reported `Authorized` and 96 reported an
available but not-started demo. The selector continues to admit only entries
whose live `Authorized` property is true, so browsing cannot silently begin a
trial. Category placement now uses the engine's own `Categories` metadata and
falls back to the previous name heuristic only when that metadata is absent.
No catalogue data or UA-owned names are stored in the repository.

Surface labels are derived presentation data only. The hardware label removes
the non-distinguishing UAD/UADx/Universal Audio prefix, prioritizes model and
edition tokens, and fits the result into the eight-character OLED field.
Colliding abbreviations receive a stable numeric suffix. The configuration
value sent back to the UA engine remains the untouched full plug-in name, so
label abbreviation cannot redirect a load operation.

## Native UNISON Config catalogue and lifecycle

The custom UNISON knob set owns a documented configuration page rather than a
second invented top-level category. The complete SDK guide Section 8, installed
knob-cell array/layout/category declarations, and official EuConApp plug-in and
instrument examples were checked before the change; EuConIO has no corresponding
UNISON loader. The first Config member is marked with `NewConfigPage`, child
selectors use `AddChild`, and pagination/Back remain runtime-owned. Construction
and removal use the existing owner-thread model lifecycle. Primitive callbacks
only enqueue to the existing serial Config controller.

Read-only inspection of the current UA engine on 2026-09-01 found 47 catalogue
entries whose native boolean `Unison` property was true; 25 also reported
`Authorized=true`. No catalogue names or UA-owned content were retained. UNISON
eligibility therefore requires both exact live properties. It is not inferred
from a plug-in name, product category, or the ordinary-insert catalogue. NONE is
the first explicit unload choice. Preset entries are accepted only from the
currently loaded authorized plug-in's advertised file list.

The write path is not a new arbitrary UA command surface: it reuses the existing
allowlisted effect `EffectName`/`Preset` Config writer. Each operation re-reads
the hardware identity, effect instance, current value and selected catalogue
entry, issues one bounded request, then confirms the complete effect node.
Uncertain completion is never retried. A refreshed plug-in topology invalidates
the old callback epoch without downgrading the user's session-level permission.

## Persistent sensitive permission across confirmed 48V refresh

Physical testing exposed a permission-lifecycle defect rather than a UA write
failure. Both 48V activation and deactivation were accepted and authoritatively
read back by the engine, but the subsequent Input model refresh created a new
callback epoch and cleared the already granted sensitive-control permission.
The next activation was therefore rejected locally and never sent to UA.

An explicitly granted Full/Custom policy now follows the same stable logical
channel for the lifetime of the verified connection. Confirmed 48V, input/output
routing, link-mode and plug-in topology refreshes rebuild their controls without
asking for permission again. The callback epoch still changes, so stale gestures
remain invalid. A disconnect, generation change, channel removal, logical identity
change, talkback-master rebind or failed readback still locks the affected target;
authorization is never inferred from feedback and no failed write is retried.

## EUCON implementation contract (recorded before adapter implementation)

Concepts: node/processor lifecycle, primitive feedback, touch, Pan knob set,
identity, callback threading, Meter API 3.1 visibility.

Consulted the separately installed EUCON SDK 2026.4 guide, complete sections
3-8 and 13.3, plus 12.2 and 12.14; the installed declarations for EuConManager,
EuCon, EuNode, EuCommon, EuProcessor, EuControl, EuControlFader, EuControlSwitch,
EuControlTextDisplay, EuControlKnobCell/Array, EuControlMultiMeter,
EuPrimitiveControl/Slider/Switch/Knob/Meter/Led, EuDefinitions, EuLayoutChannel,
EuBatchedMeters and EuBatchedMeterWriter. Supporting examples were EuConIO
ExTop/ExProcessorChannel, EuConApp ExKnobSetPan and its meter setup/visibility
and writer routines, and the FAQ MeterExample. The older FAQ's 2.0 meter-count
attribute does not supersede the current 3.1 SetFormat contract.

Implementation requirements:

- Initialize the EUCON manager with the current default-priority overload before
  constructing SDK objects. Check failures; do not continue partial startup.
- Give each control its owning processor in its constructor. Initialize each
  primitive before access; numeric tables must be sorted.
- Build the initial processor model, required attributes and authoritative
  values before registering one process-lifetime node. No process-name override.
- Stable channel identity derives from the exact hardware ID plus physical
  input/aux identity, not the device enumeration index or a user-editable name.
  Raw hardware identifiers are not logged or included in source fixtures.
- Use standard channel types, layout rules/names and independent channel order.
  The surface owns discovery, assignment, attention and banking.
- Batch live topology changes in one node Freeze/Thaw; use processor/knob-array
  scopes for smaller changes. Do not replace the registered application node.
- Copy callback data to an owner-thread inbox. Never hold its lock while calling
  the SDK. No socket I/O or backend mutation from a callback.
- Track touch against the lifetime of a logical processor. Defer topology while
  touched. In this read-only checkpoint, do not drive a touched fader; restore
  the real state on release. Surface input is observable, never executed.
- Declare Meter API 3.1 before registration. Initialize primitives and set meter
  format only after the processor belongs to its node. Save visibility handle
  and format from callbacks; do not query them per frame. Use the V2 batched
  writer methods only for valid visible meters, with a writer local to each
  update cycle. Record errors; no automatic legacy fallback in this new host.
- Unregister processors before destruction, unregister/destroy the node, then
  destroy the manager. Runtime failures must be visible, not reported as active.
- Required acceptance: S3 AND Avid Control, including simultaneous visibility,
  rename/topology/banking, touch, motor/rings/LEDs and meter visibility. Compilation
  and UA observation alone do not establish hardware verification.

## Locally observed on 2026-08-30

### Feedback refinement contract (before editing)

Concepts: peer knob cells, channel versus output format, monitor track semantics,
and per-leg meter level/peak feedback. Rechecked the complete guide sections 8,
12.13 and 13.3, the current knob-array, knob, multi-meter and channel-layout
declarations, and channel/track format and type attributes. Supporting references:
EuConIO channel implementation first, EuConApp Pan and common meter initialization,
then the FAQ peer-knob example. No vendor implementation is copied.

- Preserve both native stereo pan parameters. Use a primary/peer knob pair at
  one position, with the documented upper switch selecting the peer. Navigation
  belongs to EUCON; do not turn that switch into an Apollo write or collapse L/R
  to an average. Detach array members before destroying their owned objects.
- Publish ChannelFormat for channel/meter leg count, independently of TrackFormat
  (the main output format used by panners). Unknown output formats stay unknown.
- Expose a separately identified monitor channel only for the observed explicit
  stereo output configuration. Use the standard Monitor track type, native
  attenuation and mute. Do not manufacture input Solo/Pan/Record controls or
  surround speaker assignments. Summing to mono does not change the physical
  two-leg output format.
- Register it through the existing node lifecycle; format changes invalidate
  visibility handles and require fresh visibility. SDK failures are surfaced.
  No locks or backend I/O in callbacks, and no new mixer-write capability.
- Keep native dB values in the Meter API 3.1 writer. Level, peak-hold and clip are
  distinct. Do not apply a device-specific offset for printed S3 LED markings,
  and never apply monitor attenuation to its pre-attenuation bus meter.
- Require fresh S3 and Avid Control feedback acceptance after these changes.

Windows UA Mixer Engine 11.9.0 accepted loopback TCP port 4710, UTF-8 text
commands terminated by NUL, and returned NUL-terminated JSON. Only `get` and
`subscribe` have been used. No `set`, `/Sleep` keepalive writes, Console UI automation,
firmware changes or audio configuration changes were performed.

- `/devices` contains both online and retained offline devices.
- This online x8's monitor was discovered at output 22, not the output 4 shown
  in some other-model examples. Production code uses IOType and capabilities.
- Monitor `Active=false` was observed while level and meter data were valid.
  Input eligibility and monitor availability must not share that filter.
- Input `Active=false` can identify a linked stereo pair's subordinate channel.
  Active, EnabledByUser and ChannelHidden are distinct fields.
- Input/AUX `FaderLevel` is native dB, with reported range -144 to +12.
  `FaderLevelTapered` is a separate normalized taper, not a linear dB scale.
- Stereo exposes independent `Pan` and `Pan2`; the linked pair name is
  `StereoName`. Do not collapse these into Windows-style balance.
- Monitor CRMonitorLevel, Mute, DimOn and MixToMono are readable. ALT selection
  exists as metadata but has not been control-tested.
- MeterLevel, MeterPeakLevel and MeterClip are exposed. A short monitor-level
  subscription returned 140 frames in 3.5 seconds, including initial read replies.
  That is evidence of a live feed, not a promised application latency.
- An already loaded Precision Limiter instance exposed nine parameter nodes with
  names, formatted strings, normalized values and, for some controls, enums.
  This is not evidence that every plugin or plugin-loading operation is supported.
- Preamp and six send nodes were discoverable on an inspected input. Reading
  metadata does not verify writes, timing, gain taper or Unison interactions.

Full captures remain outside Git. Tests contain invented data only.

## Current safety and limits

The compiled transport accepts only get/subscribe. There is no set operation,
no queued/replayed mixer command, no saved mixer state, and no remote host option.
Reconnect reads a fresh snapshot. A failed connection clears published state.
Frame size, nesting, queue size, discovery and deadlines are bounded. SDK-free
tests cannot open sockets, MIDI ports, audio sessions or an EUCON node.

Channel L/R meter role mapping and feedback are provisional until the surface
test. Monitor meter indices have NOT been assigned surround speaker roles;
explicit Stereo monitor configurations now expose the first two indexed feeds
as provisional L/R through a standard Monitor strip. Other configurations remain
raw diagnostic state. Missing feeds preserve their index rather than shifting
another leg into their place. Stereo and monitor acceptance is still required.
No plugin, preamp, Cue, monitor write,
project-payload persistence, automatic startup, or production installer is shipped
at this checkpoint. No assumption is made about S3's own Audio Control section.

The read-only fader uses a project-owned piecewise dB taper, not an inferred
conversion of UA's normalized taper. Its 1,025 single-precision entries are
strictly increasing and include unity exactly for the observed input range.
Capability/range changes replace only the affected processor under node freeze,
with the same persistent identity and a new callback tag, deferred during touch.
Pan membership/stereo changes use the existing processor's knob-array scope.
Stereo Pan L/R now uses a primary/peer pair, selected by the standard upper
switch. The desktop distinguishes native gain, live meter level and engine
peak-hold; it no longer labels the live value as the held peak. No meter offset
has been added for S3's physical scale. ChannelFormat is explicitly published;
TrackFormat is independently derived only for an identified stereo destination.

## Verification log — 2026-08-30

- Release and Debug pure tests: 12,210 checks each. Includes strict UTF-8,
  exact numeric identity, framed input, read-only command allowlist, online and
  stereo rules, invalid telemetry, range bounds and fader-table monotonicity.
- Synthetic loopback transport tests: initial get/subscribe, malformed envelope,
  interrupted partial read, disconnect clearing, fresh reconnect generation,
  persistent identity and bounded shutdown. Uses an ephemeral port belonging to
  the fixture, not UA's 4710, and no EUCON/audio/MIDI initialization.
- Native live observer, 24 seconds: one online device, one retained offline
  device, 13 visible channels, 17 filtered channels, one monitor section; 5,228
  frames, 216 connected samples and 203 changing monitor samples. Generation
  stayed at 1 across periodic rediscovery. Counts describe this session only.
- Isolated regression build of the existing Windows EUCON edition succeeded;
  its live executable was not overwritten or restarted.
- Desktop QA: Chinese/English switch, scrolling through the thirteenth channel,
  and disabling the EUCON connection while the existing adapter runs were checked.
  The Computer Use skill was used only for the new observer and its log folder;
  no Console or EuControl settings were automated.
- S3/Avid Control feedback, engine-version compatibility beyond this machine,
  and all writes remain unverified. Do not mark this as a hardware baseline.

## Feedback refinement validation — 2026-08-30

- Debug and Release core suites now pass 12,230 checks, including independent
  channel/output formats, asymmetric dual pan, monitor identity/projection,
  mono-summing versus output format, sparse meter legs, unavailable values and
  separate live/held peaks. All fixtures remain synthetic.
- Debug and Release synthetic loopback transport suites pass. Debug native host
  builds and smoke passes. An 8-second live read-only observer run received
  3,277 frames, 64 connected samples and 63 changing monitor samples, generation
  unchanged at 1. No EUCON node or audio writes were opened by that observer.
- A separate Release build is in `artifacts/apollo-eucon-validation/Release`;
  the running Apollo executable was not overwritten. This is a local validation
  output, not a redistributed SDK package. Release smoke and a 6-second live
  read-only run also passed (1,755 frames, 49 connected samples, 48 changing
  monitor samples, generation 1).
- Window restoration/restart and the new exposed-model acceptance remain
  pending: Computer Use reported a minimized window and user-input conflict.
  Do not treat a successful build as verification of peer selection, meter
  visibility, topology cleanup or monitor feedback on S3/Avid Control.

## Ordinary-channel write contract — 2026-08-30 (before implementation)

Concepts: primitive values/callback threading, explicit touch, PAN semantic
placement and peer-knob lifecycle. Reviewed the SDK guide sections 3.1–3.2,
4–5, 7–8 and 12.12–12.14, installed primitive/processor/control contracts,
EuConIO channel dispatch, then EuConApp dispatch and the peer-knob FAQ.

- Decode the callback's table index using the matching numeric GetValueAt
  overload while the supplied primitive is valid; never retain that pointer.
  Queue copied events and do all application/SDK work on the owning threads.
  No application lock held by a callback may also surround an SDK call.
- Distinguish force refreshes, touch and peer-selector events from parameter
  requests. Touch must not become a value write. Do not drive a touched fader.
  Peer switching remains runtime-owned, using saved member IDs, not positions.
- Use native LeftPan/RightPan primitive semantics through LayoutName1. A mono
  source uses LeftPan. Values remain independent percentages; there is no
  synthetic balance, stereo averaging, automation mode or invented pan link.
- Keep the single registered node, persistence keys and Meter API 3.1 path.
  Do not compensate for a particular surface's printed meter scale.

UA's local set protocol is private and version-dependent, corroborated by the
two authors linked below, not an official SDK contract. Readable metadata alone
does not prove writability. The initial write allowlist is ordinary-channel
FaderLevel, Pan, Pan2, Mute and Solo only; outputs, talkback, preamps, phantom
power, routing and plugins are excluded. Honor disabled/read-only metadata.

Start unarmed; explicitly arm one stable channel, never a row number. Revoke
on disconnect, stale state, identity/format/capability change or write failure.
Do not persist permission, replay queued gestures on reconnection or push values
when arming. Use a separate worker/connection from the meter observer, bounded
latest-value coalescing, request expiry and fresh identity/metadata checks before
each write. Confirm with a channel-node read, not a same-path set echo. A private
protocol without transactions cannot guarantee atomicity against a simultaneous
Console edit: report ambiguity, do not retry a potentially executed write.

The user authorized live tests on BUS 1/2 or BUS 3/4. Discover exact identity,
record current values, require muted/lowest-level conditions, restore only the
values still owned by the test. Do not automatically test Solo (global listening
effect). S3 peer selection was user-verified; new writes and Avid Control remain
subject to separate hardware acceptance.

## Ordinary-channel control validation — 2026-08-31

Implemented as a protocol-independent typed channel controller plus EUCON
callback adapter. It is a deliberately single-channel, opt-in validation stage,
not the final all-channel product. Known Mic/Line/Virtual/ADAT/S/PDIF input types
and AUX returns are eligible; unknown types and TalkbackMic remain read only.
The separate command connection is created only when there is an actual request.
Observer freshness is bounded to two seconds (allowing periodic rediscovery);
individual queued gestures and pre-write requests expire after 500 ms. Callback
age is checked separately so a blocked UI cannot replay old gestures as new.

- Release and Debug: 13,268 pure checks pass, plus synthetic transport suites
  covering all five field types, cancellation immediately before sending,
  expired gestures, changed stereo identity, incorrect readback despite a valid
  set echo, explicit engine rejection, and disconnect after a potentially
  executed set. Uncertain writes are not retried. All fixtures are project-owned.
- Live x8 test was limited to the user-authorized BUS 1/2, already muted, solo
  off and at minimum fader level. Gain -144 -> -140 -> -144 dB; Pan -1 -> -0.99
  -> -1; Pan2 +1 -> +0.99 -> +1. Each value was independently read back and
  restored. Observed PAN float rounding was within 1e-4. Mute, Solo, other
  channels, outputs, routing and preamps were not changed.
- A separate 14-second armed-readiness check crossed periodic rediscovery
  without losing permission; it submitted no writes and explicitly disarmed.
  An 8-second native observer run received 2,168 frames, 68 connected samples,
  67 changing monitor samples and an unchanged generation. Release/Debug smoke
  remains offline, without EUCON initialization.
- New Release host started after the old Apollo process exited normally;
  EuControl/Console/drivers were not restarted or configured. The new node
  registered 14 projected strips; visibility callbacks supplied valid Meter
  API 3.1 handles and batched writes returned success. This is runtime evidence,
  not physical validation of every display, gesture or attached surface.
- Native PAN tags and explicit knob touch were added without replacing peer
  selection. The user's earlier S3 Sel verification remains useful, but new
  fader/knob/mute/solo writes, bidirectional feedback, touch/release, bank/format
  changes and simultaneous Avid Control behavior still require user acceptance.

Manual bench tools are built with the transport tests but never run by ordinary
builds/CI. `ApolloTransportTests.exe --live-muted-channel "exact channel name"`
requires one already-muted stereo channel at minimum gain and solo off; it only
tests/restores gain and PAN, with fresh state checks before each write and
conditional restoration. `--live-readiness "exact channel name"` performs only
the no-write permission-stability check. Neither tool initializes EUCON or
changes driver/device settings. Do not run live probes during recording.

## Control-room contract — 2026-08-31 (before implementation)

The user accepted the ordinary-channel stage, then explicitly requested that
MONITOR be removed from the channel bank. A monitor output is not an input/AUX
strip. Retain its independent read-only telemetry; never project it as a channel
or let ordinary-channel permission authorize monitor writes.

Concepts reviewed: Monitor processor lifecycle, standard monitor layout,
primitive tables, confirmation, callback ownership and touch. Read the complete
guide sections 3.5–3.6, 4–5 and 7, the installed EuLayoutMonitor, EuControlKnob,
EuControlSwitchArray, EuControlSwitch and primitive/processor declarations;
consulted EuConIO monitor initialization/dispatch/fold-down helpers before
EuConApp. These are private references, not repository contents.

- Publish at most one kProcType_Monitor / kRUL_EuLayoutMonitor processor.
  ControlRoom is an EuControlKnob, Mute/Dim are EuControlSwitch controls, and
  Mono is a latching switch in the standard FolddownFormat switch array.
  Mono sums the mix; it does not change the physical stereo output format.
  Mute and Dim use explicit latching presses (no implicit unmute on release).
- No channel fader, Solo/PAN, invented calibrated SPL, fixed reference level,
  speaker/source switching, dim-depth adjustment or talkback in this stage.
  Let the runtime choose surface placement; do not hard-code S3 hardware.
- Construct controls with their processor parent; initialize tables and stable
  persistence IDs before registration. Register the initial processor before
  the single node. Unregister before destruction; use node freeze/thaw only
  for processor add/remove and preserve the existing channel processors.
- Save returned array member IDs. Callback pointers never escape. Decode only
  the matching primitive type, copy events and dispatch off the callback thread.
  Confirmation may only constrain an index, using atomic policy state; no I/O,
  logging or application lock. Owner-side SDK calls hold no callback lock.
- Default locked, independent non-persistent authorization, no write on unlock.
  Capture current native attenuation as a ceiling, revalidate it immediately
  before each typed write, and clamp requested increases to that ceiling.
  External level above the ceiling revokes control; never force it back down.
  This is a software request ceiling, not a calibrated hearing/SPL safeguard.
  Removing Mute/Dim can increase loudness even below the ceiling: warn explicitly.
- Require one unambiguous, supported stereo/main-monitor context. Recheck stable
  identity, source, speaker selection, stereo mode, gain-mode and dim-depth
  metadata before writes; context changes revoke permission. Unsupported or
  incomplete modes stay locked. Private protocol metadata is not proof of
  writability, so confirm each request by explicit node readback and never retry
  an uncertain set. No routing, gain-mode or safety metadata is writable here.
- Preserve the accepted ordinary-channel controller and Meter API 3.1 path.
  Exercise monitor writes only against synthetic loopback fixtures this round;
  live monitor changes require the user's own confirmation/testing on both S3
  and Avid Control. The currently running build is not replaced automatically.

Read-only x8 metadata currently reports stereo main speakers, native level
range -96..0 dB and separate Boolean Mute/DimOn/MixToMono. DimAttenuation is
reported numerically but Console offers discrete choices: do not infer a new
continuous control. Raw captures remain outside Git. This is not a universal
Apollo-model guarantee.

## Independent control-room validation — 2026-08-31

- MONITOR is removed from desktop/EUCON channel projection, channel order and
  fader registration. Defensive checks reject a monitor passed to either the
  ordinary-channel controller or a strip constructor. Its telemetry remains in
  the separate Monitor model. No existing Windows-backend code was changed.
- The new Monitor processor uses the native control-room knob, latching
  Mute/Dim and a single Mono fold-down option. Missing, disabled, read-only or
  non-Boolean switch capabilities do not become actionable monitor buttons.
  Confirmation clamps surface requests, while a separate typed writer enforces
  the ceiling again against freshly read metadata. Neither permission unlock
  creates a write, and channel/monitor epochs cannot authorize each other.
- Release and Debug each pass 14,314 pure checks and the synthetic transport
  suite. Coverage includes all four monitor fields, clamped readback, locked
  startup, final cancellation, expired requests, source/speaker/mode/dim-depth
  changes, external gain above the ceiling, false set echoes, rejection and
  disconnect after a potentially executed write. No uncertain request is retried.
- Isolated Release/Debug native host builds and offline smoke checks pass.
  Validation outputs are under `artifacts/apollo-monitor-validation`, not the
  running application's output directory. The original process was preserved.
- An 8-second real read-only observation found 13 input/AUX channels and one
  separate eligible monitor context: 13 projected channels, 2,271 frames,
  68 connected samples, 67 changing meter samples, generation 1. This opened
  no EUCON node and sent no audio/control writes.
- No live main-monitor level/Mute/Dim/Mono changes were made. The new GUI and
  Monitor processor have not been launched against the attached surfaces yet.
  S3 and Avid Control acceptance remains pending: absence of MONITOR in channel
  banks, correct control-room assignment, read-only behavior, explicit unlock,
  level ceiling, touch/release, switch feedback and ordinary-channel regression.
  Compilation is not evidence that a particular surface exposes every native
  monitor control; inspect runtime assignment before adding any compatibility path.

## Monitor extension contract — 2026-08-31 (before implementation)

User verification: Avid Control's current Monitor interface works; S3 Mute,
Dim and Mono work through soft-key assignments. This does not verify new
controls below or provide a dedicated S3 monitor knob.

Reviewed SDK guide 3.5.7, 3.6, 4–5, 7 and 11.6.14, installed Monitor layout,
knob, switch/array and primitive table contracts, then EuConIO Monitor and
EuConApp Monitor source dispatch. Keep one Monitor processor and node; initialize
controls before registration, retain assigned array member IDs and detach array
members before destruction. Numeric tables are ascending. Momentary/latch
timing belongs to EUCON surfaces, not an application timer. Callbacks copy
values only; worker performs typed writes and independent node readback.

- DIM Amount maps to standard DimLevel with negative dB feedback. UA stores
  positive attenuation; current UA documentation specifies seven discrete
  depths (9, 17, 26, 34, 43, 51, 60 dB), not an invented continuous range.
- Monitor SOURCE is an exclusive selection of Main Mix or available Cue buses.
  Publish only explicitly enumerated, enabled engine sources via ControlRoomSource.
  This changes what the engineer hears, not Cue mix levels or output routing.
- TALK is Talkback, a distinct standard switch. Require an identified online
  talkback master and native talkback mic; do not change mic selection, sends,
  preamp gain or Talkback-to-monitor routing. Block activation if physical-CR
  talkback is enabled to avoid acoustic feedback. UA owns automatic dimming.
- Monitor A–D represent additional monitor outputs with independent levels,
  sources and optional mute/dim. Four source buttons are not four such outputs.
  COMS is Listenback, not Talkback. Neither is fabricated to fill an empty UI.
- ALT trim is speaker calibration, not Monitor A volume. No dedicated ALT trim
  knob exists in the installed standard Monitor layout. Native per-output trim
  metadata does not establish stereo-pair write atomicity: defer this control.

All new writes remain explicit-opt-in. Source and dim-depth changes can increase
perceived loudness without increasing the main level, and TALK opens a microphone;
the unlock warning must say so. The level ceiling is not an acoustic SPL limit.
Supported source/depth value changes are now normal monitor state updates, not
processor topology changes. Unknown modes, metadata, source sets, talkback master
or physical-CR routing changes still revoke permission. Never replay/retry failed
writes; if communication fails after TALK activates, do not promise fail-closed
microphone behavior: the operator must use Console/hardware to close it.

Read-only captures: current engine enumerates mon/cue1–cue4, device-level
DimAttenuation, global TalkbackOn and TalkbackMaster, and per-output AltMonTrim.
No live monitor writes or Console configuration changes were used for research.

The user subsequently confirmed that controls without a matching standard EUCON
semantic must not be added. ALT Trim, unproven Monitor A–D mappings and COMS
remain absent; no application-specific substitute or fake channel is introduced.

## Monitor extension validation — 2026-08-31

- Core Release/Debug suites pass 14,346 checks. New coverage includes signed
  discrete dim tables, exact device/root/output paths, source enums/disabled
  choices/injection rejection, root subscription updates, talkback-master and
  mic-selector guards, physical-CR feedback prevention and context stability.
- Synthetic transport tests verify all five source choices, DIM depth and TALK
  on/off, separate node readback for device/global fields, and rejection of false
  set echoes without retry. No fixture talks to UA's engine or initializes EUCON.
- Isolated native outputs: `artifacts/apollo-monitor-extended/Release` and
  `Debug`. The running PID 3356 from `apollo-monitor-validation` was preserved.
  Smoke does not initialize EUCON or open sockets.
- A six-second live read-only observer found 13 ordinary channels, one monitor,
  and one available control of each new kind: dim depth, source selection and
  TALK. It received 759 frames/49 connected samples with stable generation 1.
  Meter changes were zero during this sample; this is not meter-motion evidence.
- New UI/model has not been attached to S3 or Avid Control. Physical acceptance
  is pending: source labels and exclusive selection, correct signed dim values,
  TALK latch/hold/release and LED feedback, hardware/Console changes in both
  directions, locked-state rejection, main-level ceiling and normal channel
  regression. Do not claim new live write support as verified until this passes.

## Channel expansion — 2026-08-31

Implementation contract and source mapping:
[AUX / MIX / Input / Inserts](APOLLO_CHANNEL_EXPANSION.md).

- The new typed address includes feature kind, native slot and (for plug-ins)
  parameter slot. Coalescing one send can no longer overwrite another send.
  Requests keep the channel identity, permission epoch and metadata captured at
  arm time; the writer re-reads the target before each command and confirms a
  complete node afterward. Failed/uncertain requests are never retried.
- AUX includes discovered AUX/Cue gain, send-in (inverse Bypass), and native
  mono send pan children. Stereo send pan, per-send Pre/Post and invented
  routing/configuration pages are absent.
- MIX enumerates enabled OutputDestination choices. A lower-switch press
  selects a route; rotation cannot re-route audio. Confirmed input/output
  changes cancel the channel permission rather than carrying it to a new path.
- Input exposes existing native gain/HPF/polarity/Pad. Gain intersects UA's
  documented plain-preamp 10–65 dB range with reported bounds; Unison, unknown
  Unison state and line gain bypass do not acquire a guessed gain curve.
  Mic/Line selection is unavailable while Hi-Z or phantom power is active.
  Phantom power itself has no write address.
- Inserts keeps real empty slot positions. Loaded plug-ins have child pages,
  native step enums, continuous normalized controls and engine-authored display
  strings. No EQ/DYN category is guessed. Instance changes immediately discard
  the old parameter tree and request discovery. A new normalized value clears
  stale engineering-unit text until the corresponding display update arrives.
- Fixed channel functions use the SDK's standard function persistence IDs.
  Children use stored member IDs, explicit detach/ownership and unique callback
  IDs. Touch/nav are Raw press/release, while ordinary on/off controls retain
  latching semantics. Touch feedback and callback lifetime need hardware
  regression; this is not a claim of an SDK defect.
- Release and Debug each pass **14,434 pure checks**, plus synthetic transport
  tests for all newly supported field families, output/input confirmation,
  send identity changes, plug-in replacement/relabel, fresh disabled/read-only
  metadata, Hi-Z context changes, final cancellation and false set echoes.
- Isolated Release/Debug hosts build under
  `artifacts/apollo-channel-validation`. The old Apollo PID 3356 and Mackie PID
  32596 were kept running. No new surface model was attached and no real set
  commands were issued.
- A 30-second read-only observation found 13 channels, 74 sends, 3 preamps,
  6 loaded inserts, 47 parameter nodes and 11 output selectors. Generation
  stayed at 1; 2,653 frames / 266 connected samples, zero stale samples, maximum
  published-snapshot age 1,258 ms (below the existing 2-second safety cutoff).
  No meter motion occurred in this sample; do not treat it as meter-motion
  validation. These counts describe this observation, not device assumptions.

S3 and Avid Control acceptance is still required, including slot order,
navigation/Back, send-in LED polarity, real parameter writes/display units,
touch release, live topology, routing lockout and existing channel/monitor
regression. See the consolidated checklist in the Chinese guide. These features
remain experimental; no live recording-readiness claim is made.

## Text initialization regression and repair — 2026-08-31

- User reported that the channel-expansion build could not connect. The runtime
  log stopped after channel 11 with `Label table failed: 65`. This was an adapter
  error-handling defect, not evidence of a network or SDK defect.
- A new explicit `--sdk-text-test` reproduced the same failure using only an
  unregistered text control: loading an empty string into a freshly initialized
  indexed-string entry returned `kERR_AlreadySet` (65). Repeated non-empty loads
  also returned that status. The installed enum describes the value as already
  matching, while the old universal checker accepted only `kERR_OK`.
- The correction is limited to text initialization/update results. For
  `AlreadySet`, check table bounds and read back the full, eight-character and
  four-character entries, accepting the no-op only when all agree. The general
  SDK error checker remains strict. Other errors, invalid entries and mismatched
  text fail; no dummy destination, fabricated input or space-filled label is
  substituted. Diagnostics retain the result, owner thread, primitive ID, entry,
  text length and readback status, without logging the text content.
- The diagnostic shares production label/update helpers. Release and Debug
  each pass 94 real-SDK text checks, including empty/non-empty/repeated labels,
  Chinese and long text, all three display widths, numeric knob display text,
  mismatch rejection, invalid-entry rejection and non-text strictness. The test
  registers no node and never accesses Apollo, MIDI or audio. Offline smoke
  remains SDK-free; mixed diagnostic modes and a concurrent Apollo GUI are
  rejected. Run this optional test only without other EUCON test adapters.
- Release and Debug also each pass the existing 14,434 pure checks and synthetic
  transport suite. Both native hosts and offline smoke pass. The pre-fix binary
  was left intact; the correction is in `artifacts/apollo-text-fix`.
- Read-only GUI connection of the corrected Release host (PID 46792) registered
  all 13 channel strips and the separate locked monitor processor. The runtime
  subsequently delivered meter visibility handles and accepted meter writes
  with result 0. No audio-control permission was opened by the agent. This
  establishes that initialization proceeds, not that S3/Avid Control labels,
  navigation and all new controls have passed user acceptance.
- Windows displayed a firewall prompt for the new executable path. The agent
  left Windows security settings and that prompt to the user; no EuControl or
  driver restart/change was performed. Physical S3 and Avid Control regression
  remains pending user verification.

## Console workflow refinement — 2026-08-31

User feedback: sends and loaded plug-in parameter control work well; normal
routing was unnecessarily ending the channel's authorization. Additional input,
AUX-return and talkback controls were requested, together with type colors and
empty-slot labeling. This is user feedback, not an assertion that every surface
has passed the complete test matrix.

- Input now varies by actual channel capability: Mic-only 48V, native preamp
  controls, non-preamp line reference (+4 dBu = attenuation enabled), S/PDIF SRC,
  AUX-return PRE/POST/MONO, and verified master TALK/TB-to-monitor. The placement
  of bus and talkback settings in Input was explicitly selected by the user.
- Rec has an intentional application-specific mapping requested by the user:
  LED on means UAD effects printed to the DAW; off means dry recording with
  processed monitoring. It does not publish a fictitious Recording transport
  state. Named UAD REC/MON alternatives appear on the custom Console knob set.
  AUX and Unison always-print semantics are not overridden.
- Colors distinguish preamp, plain line, ADAT, S/PDIF, virtual, AUX and talkback
  families, independently of user naming. Empty slots explicitly show None and
  cannot engage nonexistent plug-ins; loaded but bypassed inserts remain named.
- Independent channel queues share a fair writer. Each callback carries a
  channel-specific authorization epoch. A confirmed self-authored routing/input
  or phantom-context change drains old gestures and waits for a complete fresh
  model before continuing that same identity. External changes lock only the
  affected channel; disconnection requires fresh explicit authorization.
- Phantom activation, TB-to-monitor activation, and TALK into the speakers
  require per-channel safety confirmation. This does not write any parameter.
  The master and built-in mic selection are rechecked before talkback writes.
- Discovery now continues publishing the existing complete model's incoming
  feedback between metadata reads. Only completed trees replace the model.
  Rebuilding an Input page does not recreate unrelated Inserts/AUX pages.
- Destination width is separate from input width: verified Line/ADAT legs and
  S/PDIF L/R establish mono/stereo formats. Unknown or missing legs stay Unknown.
- Release and Debug core tests pass 14,575 checks. Synthetic loopback tests cover
  all added property families, protected activation, independent simultaneous
  channel operations, stale/cross-channel callback rejection, own-route
  continuation, external-route isolation, and a deliberately slow (>2-second)
  metadata refresh without stale published feedback. No real audio writes.
- A 30-second real read-only observation of the development build retained
  generation 1: 13 visible channels, 74 sends, 3 preamps, 6 inserts and 47 plug-in
  parameters; 2,729 frames, 268 connected samples, zero stale samples and a maximum
  snapshot age of 1,048 ms. No meter movement occurred, so this is connection/
  freshness evidence only, not moving-meter validation.
- A follow-up 12-second read-only capability check found three available Mic
  phantom controls, two visible line-reference controls, one visible SRC,
  eleven input REC/MON modes, two AUX Input pages and one TALK Input page.
  Counts reflect the user's current mono/stereo linking and visible channels;
  they are not baked into the adapter.
- After the user closed the old GUI, Release and Debug each passed 117 real-SDK
  checks: strip creation, Rec state polarity, value-only feedback and selective
  knob-page rebuilding. The first run exposed a fixture lifecycle omission
  (Freeze returned NullPointer before processor membership was established).
  The fixture now attaches strips to an unpublished node and detaches them
  before destruction; it never registers the node or accesses UA/MIDI/audio.
  Production error handling is unchanged. Both builds also retain 14,575 passing
  pure checks, and Release offline smoke confirms all control permissions locked.
  Physical S3 and Avid Control acceptance of controls, colors and LEDs is pending.

Builds are isolated under `artifacts/apollo-console-workflow`, leaving the prior
working executable intact. No EuControl/driver setting was changed, no real
phantom/talkback/recording-mode operation was sent, and licensed SDK materials
and raw read captures remain outside Git.

## Sources

### Upper Channel Control hierarchy — 2026-08-31

Added an explicit top-level directory sharing the existing standard channel
arrays, plus custom UNISON (slot 9) and Control Room (slot 16). Console/quick
controls (slot 11) remain unchanged. The monitor alias shares the independent
monitor writer, authorization and ceiling; no monitor fader or duplicated state.
UNISON uses discovered preamp/effect/parameter identities, normalized and enum
values and engineering-text feedback, with pre-write identity checks and
explicit sensitive-control confirmation. Line Gain Bypass keeps the name visible
but exposes no writable controls. No raw UNISON gain curve is inferred.

Release and Debug each pass 14,609 pure checks and the synthetic transport suite.
Native binaries are isolated under `artifacts/apollo-upper-control`. This
iteration did not run SDK regression, launch an adapter, inspect live UA state,
or send hardware writes. Physical S3 and Avid Control acceptance remains pending.
See `docs/APOLLO_UPPER_CONTROL.md` for the source/ownership and acceptance contract.

Follow-up: the user's first S3 check found duplicate INPUT/Input labels. Native
Input layout/function IDs were correct, but the explicit parent directory placed
Input at the fourth entry instead of its observed native second entry. The
guide's 8.1 index table and 8.3 display convention conflict; the installed example
and physical first-four labels support 8.3 for these entries. Corrected that
parent order only, leaving Aux/Pan/Group/Mix and custom 9/11/16 unchanged. This is
not a device-name branch and does not change focus-area indices. The discrepancy
and the still-unverified surface matrix are recorded in the implementation
contract rather than assuming the guide is uniformly ordered.

The correction uses named semantic bindings with native child-layout/identity
validation, and logs each structural link. Release and Debug pass 14,679 pure
checks and the synthetic transport suite; separate binaries are under
`artifacts/apollo-upper-binding`. Expanded SDK readback/rejection checks compiled
but were not run alongside the user's live adapter. No restart or hardware write
was performed, and physical acceptance of this correction is still pending.

### Identification and UI polish — 2026-08-31

- The user requested saturated channel-family colors and slot-prefixed empty
  insert labels. Applied the explicit palette recorded in the Chinese guide;
  empty and loaded slots share the existing one-based physical-slot numbering.
- The UI timer was calling MoveWindow and SetWindowText every 33 ms even when
  nothing changed. Geometry, caption, enabled-state and scrollbar changes are
  now compared before calling the corresponding Win32 mutator. Meter painting
  and actual permission changes still refresh normally.
- The reported expiry is our pre-send 500 ms guard, not a UA rejection. The
  inspected session records a queued gesture followed roughly 562 ms later by
  expiry and a zero authorization epoch for that channel. Existing logs cannot
  partition the delay into queue versus individual preflight reads. No retry,
  timeout relaxation or permission bypass was introduced; the UI now explains
  the timeout and recovery separately for channels and control room.
- Release and Debug pure suites each pass 14,579 checks including exact palette,
  preamp-family stability and sparse empty-slot numbering. Output is isolated in
  `artifacts/apollo-channel-polish`; the running prior build is left intact.
  New-build SDK regression and S3/Avid Control visual acceptance are pending the
  user's switch to this build. No hardware setting or audio value was changed.

- [UA Console overview](https://help.uaudio.com/hc/en-us/articles/25347160337556-UAD-Console-Overview)
- [Monitor mix controls](https://help.uaudio.com/hc/en-us/articles/25351484855828-Monitor-Mix-Controls)
- [Monitor column](https://help.uaudio.com/hc/en-us/articles/25351900668564-Monitor-Column)
- [UA Talkback](https://help.uaudio.com/hc/en-us/articles/26489966354836-Talkback)
- [UA Sends](https://help.uaudio.com/hc/en-us/articles/25350937974676-Sends)
- [UA Preamp Controls](https://help.uaudio.com/hc/en-us/articles/25349542599956-Preamp-Controls)
- [Avid S4/S6 guide: Monitor Select / Coms](https://resources.avid.com/SupportFiles/ProMixing/S4_S6_Guide_v2025.12.pdf)
- [Lyra author's private-interface observations](https://headroomstudio.dev/lyra/guide.html)
- [Apollo Control author's implementation](https://github.com/jasonmcaffee/apollo-control)

The latter two are corroborating research, not manufacturer contracts. No source
code from them is copied into this project. Avid SDK materials remain in their
separately obtained private installation; obtain access through Avid.
## Real-time mixing write path — 2026-09-01

EUCON concept reviewed: primitive state callbacks, confirmation callbacks, touch
state, callback threading and application-to-surface feedback. The complete
relevant sections of the installed 2026.4 Getting Started guide, current
`EuProcessor` and `EuPrimitiveControl` declarations, EuConIO channel example,
EuConApp channel example and FAQ `KnobByDeltaExample` were consulted. EUCON calls
`OnPrimitiveCallback` for each physical primitive state change; the application
must update its corresponding parameter. The callback runs on an EUCON-created
thread, so it copies the event to the existing owning queue. Touch suppresses
competing application-to-surface feedback; it does not defer surface-to-application
writes until release.

The former ordinary control path performed complete remote discovery, one set and
complete remote readback for every encoder/fader step. That serialized transaction
made a live gesture sound and display late. Ordinary channel and Control Room
controls now validate identity, shape, type, range, permission epoch and freshness
from the observer's latest completed model immediately before one typed set. Level,
pan, send level/pan, preamp gain, loaded insert parameters and UNISON parameters are
coalesced at a maximum 100 Hz; only the newest unsent value for each typed address is
retained. Mute, solo, bypass/power, source, record/monitor and other discrete audio
controls dispatch immediately. The existing observer subscription is authoritative
and corrects optimistic feedback after the engine publishes state.

Plug-in load/unload, preset recall and CONFIG interface/routing settings remain
transactional with fresh discovery and explicit post-write node confirmation.
No uncertain write is replayed. Synthetic loopback regression verifies the live
path, rejection/disconnect behavior, independent permissions and the unchanged
CONFIG confirmation path; core tests and the isolated native build pass. Physical
S3 and Avid Control acceptance of timing, final feedback and all audible control
families remains required.

The first physical follow-up found Control Room level still stepped. The remaining
latency was local: callback events waited for the desktop's 33 ms UI timer before
reaching the monitor queue, then met the writer interval. The callback inbox now
posts a coalesced owner-thread message as soon as an event arrives. SDK processing
remains on the same owner thread, but audio dispatch is independent of UI refresh.
Continuous writers now use a 10 ms interval; acknowledgements are drained before
the next live write instead of blocking the current audible step.

Plug-in and UNISON `NormalizedValue` remains a real-time transport value, not a
display unit. It is never shown as a percentage. OLED value text now comes only
from UAD's corresponding `StringValue`, preserving native dB, Hz, ms, ratio and
mode semantics. A normalized write is dispatched immediately and does not wait
for that text response; while the authoritative text is pending the value line is
blank rather than showing a fabricated unit.

Touch ownership gates only adapter-to-surface position feedback. It does not gate
UAD `StringValue` updates: engineering-unit text continues to refresh while the
operator holds and turns a plug-in or UNISON encoder.

Loaded insert and UNISON preset CONFIG pages no longer use one rotary selector.
Every advertised preset file is a fixed knob cell in the same CONFIG array;
EUCON exposes the available OLEDs and Page navigation, and `In` recalls the preset
shown on that cell. Folder catalogue entries and unadvertised save operations
remain excluded, and preset recall retains the transactional CONFIG writer.

## Permission and multi-control audit — 2026-09-01

The saved access profile is session authority, not a one-shot queue token. A stale
gesture, rejected set, route refresh, input-mode refresh, phantom context change or
plug-in topology change discards only work derived from the old model. Once the
observer publishes a fresh matching logical channel, the desktop reconciler creates
a new callback epoch and restores the user's selected scope. A failed channel arm is
now transactional: validation completes before the permission entry is published,
so a transient incomplete model cannot leave a tracked-but-unarmed shell that blocks
future reconciliation.

The former 10 ms sleep was global to the channel writer. It limited one control to
100 Hz but also serialized unrelated faders, pans and sends. The live writer now
tracks the dispatch interval per typed channel/address; a delayed repeat of one
parameter cannot block a different continuous control or a switch queued behind it.
Control Room uses the same per-field policy. Replies are checked on an independent
deadline without delaying the audible set, and an ambiguous or rejected write is
never replayed.

An EUCON adapter exception still destroys the failed node through the documented
owner lifecycle and discards all queued gestures. With automatic connection enabled,
the application now rebuilds the adapter after bounded 1/2/4/8-second backoff instead
of silently changing the saved access profile or requiring all controls to be granted
again. Successful reconstruction binds fresh epochs only; it never repeats a UAD
write that may already have executed.

Pure queue/model tests, synthetic loopback transport tests, desktop settings tests
and an isolated Release EUCON build pass. Physical acceptance must cover simultaneous
multi-fader/encoder moves, S3 plus Avid Control attachment, transient surface loss,
and confirmation that no touched control receives competing motor/ring feedback.
