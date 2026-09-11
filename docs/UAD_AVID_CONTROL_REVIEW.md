# Avid Control / S3 model review — 2026-09-12

Status: physical verification passed after publishing a single real Main alias
inside a nonempty SpeakerSelect array. Source, Main/ALT and the Main alias now
render and operate on the tested Avid Control/EuControl setup.

The next physical pass confirmed that stereo Pan controls and writes are now
correct on Avid Control. Explicit `Top_Down` ordering did not change its R/L
presentation. The current candidate therefore publishes the independent Right
member before Left, while retaining side-indexed control pointers and the
official `RightPan`/`LeftPan` semantics. This compensates presentation only;
input and feedback cannot be swapped by container position.
Plug-in Config remains deferred by user request.
Transport is explicitly out of scope: Console has no transport.

## Contracts checked before implementation

Consulted the separately installed EUCON 2026.4 guide sections 3.5.7,
3.6.1–2, 8.2–8.3 and 12.13; installed EuDefinitions, EuCommon, EuNode,
ProcessorAPI/EuProcessor, EuControlKnobCellArray, EuControlKnobCell,
EuControlKnob, EuPrimitiveKnob, EuControlSwitchArray, EuLayoutPan and
EuLayoutMonitor declarations. Compared EuConIO's monitor implementation
first, then EuConApp's Pan, Inserts and knob-set implementations.

- A peer is an alternative parameter at the same physical position, not the
  second independent leg of a stereo panner. The peer experiment collapsed the
  Avid Control display to one pan and was removed. Stereo Pan now publishes two
  normal array members with `LeftPan`/`RightPan` semantic tags. The node
  advertises advanced-panner support and adds the Standard track-format config
  only after EuControl advertises matching advanced-panner handling.
- Config page markers use assigned member IDs. Child navigation belongs to
  EUCON. The existing insert/config hierarchy is preserved while each top-level
  directory cell now reports its live `NumberOfChildren`, paired with the node's
  `SupportsNumberOfChildrenAttribute` feature bit.
- Control-room sources are a named switch array. Main/ALT1/ALT2 are standard
  MomentaryLatch speaker-set switches, not individual-speaker mute controls.
  Source switches use MultiState and application feedback supplies exclusivity;
  the array is not a soft-key container or a synthetic EUCON radio group.
- Talkback microphone level has its own standard knob and talk top switch.
  The upper Control Room Talk alias is a persistent MultiState toggle rather
  than raw press/release. `Talk dB` uses a solid thermometer level ring;
  `kRingOff` is retained only as a documented option for non-progress controls.
  Monitor A–D are additional outputs, not aliases for source buttons.
- Populate dynamic arrays inside the owning frozen model scope; detach
  members/children before releasing owned objects. Keep the existing
  application node and persistence identities.
- Confirmation callbacks only constrain indices. State callbacks capture IDs,
  values, thread, timestamp and epochs; owning threads dispatch writes. Check
  SDK return codes. Do not add delay, retry uncertain writes, or hold application
  locks across SDK calls.

## Read-only engine evidence

The connected engine reports an independent TalkbackMic FaderLevel in dB,
AltMonSelection (0–2), MaxAltMons, and main MixInSource choices mon/cue1–cue4.
Cue output nodes have source/mono/routing but no independent level parameter.
Therefore do not fabricate A–D level controls or repurpose talkback send gain.
No live writes, speaker changes or microphone activation were used for discovery.

UA user-facing semantics were checked against the official
[Monitor Column](https://help.uaudio.com/hc/en-us/articles/25351900668564-Monitor-Column)
and [Talkback](https://help.uaudio.com/hc/en-us/articles/26489966354836-Talkback)
documentation. Speaker switching must preserve the existing main level ceiling
and must not change speaker calibration, routing or ALT count.

## Required physical acceptance

Verify both S3 and Avid Control: independent stereo pan input/readback/reset;
Config entry, browsing and explicit insert load; main/Cue source labels and
exclusive selection; Main/ALT1 selection and LEDs; talk level and switch.
Check locked-state rejection, external Console changes, reconnect, banking,
meters and unchanged fader/encoder behaviour. No physical fix is claimed until
the user completes this matrix.

## Validation result

- Release core suite: 15,368 checks passed.
- Synthetic loopback transport suite passed (no UA engine writes).
- Desktop settings suite: 12 checks passed.
- The installed-SDK text/model run passed. It verifies two independent Pan
  members, legacy and negotiated Standard track-format publication, directory
  child counts, native monitor switch modes, source/speaker identities, upper
  Talk toggle semantics and the solid Talk dB level ring.
- CRInspect was an intermediate rejected candidate. The accepted local build is
  `artifacts/ApolloBridge_Eucon_CRSpeakerMain/Release/ApolloBridge.Eucon.exe`;
  v1.1.0 is built from the same accepted model with release-only cleanup.
- Test build connected to the real engine and registered 13 tracks, five main
  monitor sources, three speaker-set switches and the talk-level knob.
  Startup had a stale-state revocation during discovery, followed by automatic
  reconnect/re-arm; subsequent observation and meter messages continued.
- No live audio parameter changes were sent for validation. Control capability
  registration is not proof that every Avid Control screen renders or dispatches
  correctly. In particular the previously blank Source panel and Config
  navigation/confirmation need user verification on the tablet.
- The first physical Avid Control test rejected the hybrid independent/peer Pan
  model (it collapsed to one visible knob) and produced no Source/ALT events.
  That model was removed rather than retained as a compatibility workaround.
- EuControl attached to the corrected build and advertised surface features
  `430`, including `SupportsAdvancedPannerHandling`; the adapter then published
  the Standard track-format config. Thirteen tracks, five sources, three
  speaker-set choices and Talk dB were registered without a network error.
- Control Room Source still rendered blank and generated no native monitor
  callback in that pass. The next candidate mirrors the current EuConIO source
  container metadata and ordering more literally: exact singular display name,
  `DoNotSort` before attachment, layout name after attachment, then member
  insertion followed by direct MultiState primitive initialization and initial
  state. The generic primitive wrapper is intentionally not used on this array.
- The stock 2026.4 `EuConIO` monitor processor was then built and attached to
  the same EuControl/Avid Control system. Its 24 documented Control Room Source
  members also produced a scrollable but empty Source viewport, while its
  Main/ALT1/ALT2 controls appeared. **This preliminary observation was superseded
  by the user's later side-by-side photographs: the official Source list and
  Main/ALT buttons do render.** It does not establish an SDK rendering defect.
  Advertising
  `kATRIBID_ContainsSoftKeys` on our monitor processor did not populate the
  Source viewport and also did not restore its speaker-set controls, so that
  experiment was removed. The next isolated candidate instead matches the
  stock monitor's control IDs, Main/ALT construction, relative publication
  order, and Source/Folddown persistence hierarchy. It does not change or
  replace the official Source array.
- EUCON does define all three relevant monitor concepts. `ControlRoomSource`
  selects the feed to the main control room; `MainSpkrs`/`Alt1Spkrs`/`Alt2Spkrs`
  select complete monitor sets; `SpeakerSelect` is a separate array for
  individual speakers such as Left, Center, Right and LFE. UAD ALT must not be
  published as `SpeakerSelect`, so the latter panel remains intentionally empty
  unless the engine exposes genuine per-speaker control.
- A/B/C/D remain absent by user agreement. Directory navigation remains the
  SDK-owned AddChild path, not simulated input.
- Physical testing then proved that the Control Room Source children and their
  callbacks do exist: tapping otherwise blank hit regions changes the UAD
  source. This establishes functioning hit regions and dispatch, but does not
  by itself isolate the cause to presentation metadata. The next candidate used the exact
  legacy `SetAttribute` calls and initialization order from the official
  `ExProcessorMonitor`/`ExUtils::InitSoftKeyContainer` implementation for the
  Monitor processor, Source container, and every named Source child. No source
  behavior, value table, or callback routing is changed by this test.
- That name-only candidate still rendered blind hit regions. The next official
  model difference was `SourceSum`, which `EuConIO` publishes immediately after
  `ControlRoomSource` and current Avid Control presents as the Source panel's
  Sum/Intercancel mode selector. The UAD source is exclusive, so the native
  `SourceSum` control is now present but confirmation-locked to index 0
  (Intercancel); it never dispatches an unsupported UAD write.
- The complete Source panel shape still did not make labels visible. The SDK's
  source-specific `ExProcessorMonitor::RenameSourceSwitch` note describes a
  connected-surface rename lifecycle: bracket label publication with the
  owning node's `Freeze()`/`Thaw()` and call `SetAttribute2(..., true)` on each
  Source child. That candidate performed a one-shot refresh after a
  surface attaches/features are negotiated. Logs confirm two successful
  five-child refreshes; no controls are removed or reinserted and no UAD write
  is generated. The latest user photographs reject this candidate as well;
  successful rename calls are not evidence of successful rendering.

## Read-only model comparison and isolated feedback test — 2026-09-12

The latest photographs show the stock example rendering named Source entries,
the Source popup (including NONE), and Main/ALT buttons. Our corresponding
regions are blank but hit-testable. Our Mono and level knobs do render.

A project-owned inspector now reads the documented attribute IDs, child IDs,
primitive initialization status, table types/sizes, indices and switch modes.
It was run against the separately installed official monitor class in an
unregistered local probe, and against our unregistered SDK regression model.
Neither probe registers a surface or sends engine writes. Source switches and
their LEDs are initialized in both models; our five source labels are present.
The library and runtime locations were also checked. These local readbacks do
not prove what the tablet receives or paints.

One remaining behavioral difference is repeated selection feedback: our
full-state updates resend Source and speaker switch indices even when unchanged,
while the static example sets them only when needed. The isolated CRInspect
candidate reads the actual SDK switch and LED indices and sends only when one
differs. It deliberately does not cache only our last-sent value: a surface
change or external Console change must be reconciled immediately. No polling
delay, smoothing or debounce is added. Whether this affects the blank controls
is awaiting physical verification, not established as the root cause.

Core (15,368 checks), desktop (12 checks) and unregistered SDK tests passed.
The self-use installation and both Windows bridges remain untouched.

The user rejected CRInspect: Source and Main/ALT remain blank and hit-testable.
Suppressing duplicate selection feedback is not a rendering fix.

The next diagnostic isolates processor publication/lifetime rather than changing
layout attributes. A private monitor-only preview registers the existing project
ControlRoom class against the existing synthetic regression fixture, with no
channel processors, configuration directory or observation loop. The observer is
never started, the writer remains disarmed and no queued input is dispatched.
The node is frozen while its attributes and monitor are added, then thawed before
registration. On shutdown the monitor is unregistered before node teardown.
Callbacks only queue, as in production; inspection runs on the owning thread.
This follows guide sections 3.5.7, 3.6.1–2 and 7.1.1 and installed EuNode /
EuProcessor contracts. It is a diagnostic, not a production replacement.

Physical isolation results:

- Our unchanged ControlRoom class in the monitor-only probe: still blank.
- Official ExProcessorMonitor in a project-owned minimal host with our node
  persistence/name/features: Source and Main/ALT render (user confirmed).
- The same official processor with controls outside our published set removed
  through RemoveControl before registration: Source and Main/ALT become blank
  (user confirmed). This reproduces the failure without our control-room
  callbacks, UAD data, channel model or ongoing feedback. It narrows the next
  check to omitted monitor controls and their lifecycle, not name spelling.

These private probes link the separately installed official example objects
locally only. Neither their binaries nor licensed objects may be committed or
distributed. The standard Source/SpeakerSelect distinction remains unchanged.

### Nonempty SpeakerSelect compatibility candidate

User-verified continuation: restoring only the official SpeakerSelect array
and its children restores Source and Main/ALT rendering. Keeping that array but
removing every child makes those regions blank again. This is evidence for a
nonempty-array dependency in the tested setup, not yet a general SDK bug claim.

The user requested at least a Main entry. The next project-owned candidate adds
one explicitly scoped compatibility alias, Main, to SpeakerSelect. It selects
the existing UAD main monitor set through the same guarded command as native
MainSpkrs. It does not invent individual Left/Right control or remap ALT1/ALT2
into that array; the standard MainSpkrs/Alt1Spkrs/Alt2Spkrs remain published.
This supersedes the earlier decision to leave the array empty in this candidate.

Concept: monitor switch-array publication, primitive feedback and callbacks.
Guide monitor/lifecycle/threading sections, EuLayoutMonitor, EuControlSwitchArray,
EuControlSwitch and EuPrimitiveSwitch contracts govern the implementation.
The owning thread builds the array and owned child inside the existing frozen
processor-publication scope before registration. The child has a stable name,
persistence ID and initialized two-state switch; PushBack's returned member ID
is retained. Confirmation changes only the proposed index; callbacks queue to
the owner, with existing epoch/permission checks and no direct engine writes.
Feedback reflects actual UAD speaker selection. Remove the child before its
container and destroy both only after processor unregistration. Check SDK
return codes using existing Check/CleanupResult diagnostics. No delay is added.
Production rendering and S3 behavior were physically accepted below.

Candidate output: `artifacts/ApolloBridge_Eucon_CRSpeakerMain/Release` (local,
not distributed). Core 15,368 checks, desktop 12 checks, synthetic loopback
transport suite and the unregistered SDK model test passed. The SDK check
asserts one named Main child, native Main/ALT layouts, shared callback routing,
and matching Main/alias indices before and after ALT1 and Main feedback.
The official diagnostic was closed before launching the full UAD candidate.
No self-use files or Windows bridge processes were changed.

Physical verification result — 2026-09-12:

- Avid Control now renders the Source controls instead of blank hit regions.
- Source selection operates correctly.
- Native Main/ALT monitor-set controls render and operate correctly.
- The single Main entry in SpeakerSelect renders and selects the real UAD main
  monitor set.

The user accepted this candidate as working. This verifies the nonempty
SpeakerSelect compatibility requirement in the current Avid Control/EuControl
setup. It does not justify publishing fictitious individual-speaker entries or
reinterpreting SpeakerSelect as the ALT monitor-set array.
