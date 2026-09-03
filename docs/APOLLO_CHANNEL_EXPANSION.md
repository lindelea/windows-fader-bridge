# Apollo channel expansion — implementation contract

Status: development build; S3 and Avid Control acceptance is pending.

## Channel identification polish — 2026-08-31

Concepts: channel color attributes and insert-cell display text only. Reviewed
the guide's text primitive and knob-set sections, the installed ChannelColor
RGB contract and SetAttribute2 notification contract, and the basic label and
advanced channel-color examples. Preserve the existing owner-thread SDK calls,
checked errors, stable slot identities and minimal array lifecycle. Change only
project-owned palette constants and empty-slot labels; no new callbacks or
vendor source. Empty labels use the same one-based physical slot number as
loaded plug-ins and stay non-actionable.

GUI buttons must not receive unchanged geometry, caption or enabled-state
updates on the 33 ms refresh timer. This is presentation behavior, not a
connection/permission indicator. Keep the 500 ms pre-write expiry protection
unchanged and explain its channel/control-room lock in both UI languages.

## Offline SDK fixture lifecycle correction — 2026-08-31

Concept: processor ownership and Freeze/Thaw. Re-read guide 3.6 in full,
EuNode registration declarations, EuProcessor update declarations and the
tERR enum; checked EuConIO then EuConApp setup/teardown ordering. Error 6 is
NullPointer: the metadata-update fixture was freezing a strip before attaching
it to its owning node. Register each fixture processor with the unpublished
node before incremental updates, bracket membership changes with node
Freeze/Thaw, and unregister before destruction, including exception cleanup.
Never register the fixture node with EuCon. All calls stay on the test thread;
preserve checked return values and the production error policy unchanged.

## Console workflow revision contract — 2026-08-31

Concepts: Input/application-specific knob sets, channel color and format,
primitive feedback, and callback authorization. Read guide section 8 (including
the Input and Group diagrams), channel/node and callback sections, and color
and record-arm definitions. Checked installed EuLayoutChannel,
EuControlKnobCell(Array), EuControlSwitch, EuPrimitiveSwitch and track/color
enums; compared EuConIO Input ownership with EuConApp Input/custom knob sets.
The following supersedes the earlier single-channel/routing-lock restrictions.

- Keep the top-level node and stable channel identity. Initialize owned arrays
  before publication; use assigned member IDs, minimal Freeze/Thaw, checked
  return values, and detach children before destruction. No vendor code copied.
- The Input page may expose actual Mic-only 48V, line reference attenuation and
  digital SRC. Values and availability come from verified engine metadata.
- AUX bus pre/post and output mono belong to the AUX return, not to independent
  input sends. Mute stays the ordinary channel mute. Mono summing does not change
  the physical stereo bus format.
- The user subsequently chose Input as the channel-type-specific home for AUX
  PRE/POST/MONO and talkback TALK/TB-to-monitor. Implement that explicit grouping;
  do not hide these controls on the custom Console page. Console retains the
  explicitly labeled UAD REC/MON alternative to the physical Rec button.
- UAD REC/MON selects processing before/after the DAW feed. It is not recording,
  record-arm or playback/input monitoring. The user explicitly requested reusing
  the channel Rec button and LED for this operation after discussing that semantic
  difference. Isolate this intentional application mapping: LED on = effects
  printed; off = dry DAW feed. Never publish a fictitious transport Recording
  state. Also keep named UAD REC/MON controls on the Console page for clarity.
  Group remains unimplemented: its native meaning is
  routing/control groups, not channel categories or stereo linking.
- Only the uniquely identified talkback master channel gets global TALK and
  TB-to-monitor actions. Default protection blocks phantom ON and TB-to-monitor
  ON (and TALK ON into speakers) until explicit per-channel safety confirmation.
  Automated testing never enables these on real hardware.
- Authorization is per logical channel. Confirmed self-authored route changes
  cancel old gestures, wait for matching fresh observer state, then continue the
  same channel. External identity/capability changes revoke only that channel;
  disconnect revokes all. Never retry an ambiguous write.
- Callbacks copy a channel-specific epoch and primitive data only. No callback
  performs UA I/O or takes a lock enclosing SDK calls. One writer serializes
  independent per-channel queues fairly.
- Refresh must keep consuming/publishing subscription feedback while reading
  metadata; never expose a partially discovered tree as a complete new model.
- Output format uses verified output legs and exact known routing vocabulary;
  unknown/ambiguous destinations remain Unknown, never guessed from source width.
- Empty insert slots are explicitly None with no actionable child. Loaded but
  bypassed plug-ins remain loaded. Categories/colors use capabilities, never
  the user's custom names or a surface model.

UA semantics checked against its own [preamp/reference/SRC guide](https://help.uaudio.com/hc/en-us/articles/25349542599956-Preamp-Controls),
[AUX returns](https://help.uaudio.com/hc/en-us/articles/25351771431060-Aux-Returns),
[insert guide](https://help.uaudio.com/hc/en-us/articles/25350369296660-UAD-Plug-In-Inserts)
and [Talkback guide](https://help.uaudio.com/hc/en-us/articles/26489966354836-Talkback).
Private read-only captures confirm line Pad, SRConvert, RecordPreEffects,
SendPostFader, MixToMono and the existing global talkback properties. Protocol
write/readback semantics still require physical S3 and Avid Control acceptance.

## Text primitive correction contract — 2026-08-31

Concept: primitive string-table initialization and application-to-surface text
feedback, not channel assignment or audio control. Re-read guide sections 3.2,
6 and 7, the installed EuPrimitiveControl/EuPrimitiveTextDisplay and text-control
declarations, and the tERR enum. Checked EuConIO text initialization first, then
EuConApp knob labels and the shared text-display helper for ownership/order.

- Initialize the indexed-string table before loading labels. Runtime text
  changes use ChangeText; the original processor/control owns the primitive.
- Keep all SDK calls on the owner thread. No new callback work, locks, top-level
  node replacement, or UA writes are needed for a text correction.
- The installed tERR contract defines AlreadySet as an already-matching value.
  Do not relax the global error checker. A text-specific no-op may be accepted
  only after successful readback verifies the requested entry; other errors and
  mismatched readback must still fail with diagnostics.
- Add an explicit SDK-only text regression mode, separate from offline smoke
  and pure tests. It may initialize/destroy the SDK but must not register a node,
  connect to UA, open MIDI/audio, or attach a surface. Destroy owned objects before
  destroying the SDK. Do not run alongside another EUCON test adapter.
- Live acceptance remains read-only and must distinguish model registration
  from user verification of S3 and Avid Control feedback.

## Source and model contract

Consulted the separately installed EUCON 2026.4 guide, section 8 in full,
including the Input, AUX, Inserts and MIX diagrams, and the installed
EuControlKnobCellArray, EuControlKnobCell, EuPrimitiveKnob and EuLayoutChannel
declarations. EuConIO Input and EuConApp Input/knob-set examples were used only
to check API usage and ownership. The guide's Input convention takes precedence
over the older example's different ordering. No vendor source is included here.

- The registered application node remains alive. Channel identity is unchanged.
- AUX contains actual sends: level on the encoder, send-in on the lower switch,
  and a child panner only when the source really supports send pan.
- MIX contains available output destinations, selected by the lower switch.
  Disabled destinations are not offered. Turning an output knob never changes
  routing. Confirmed self-authored changes drain old gestures and refresh this
  channel's authorization; external changes still revoke it.
- Inserts contains actual loaded plug-ins and native child parameter pages.
  EUCON owns child navigation and page size. No guessed EQ/Dynamics categories,
  plug-in loading, preset switching, or simulated plug-in windows.
- Input contains supported preamp operations and the user-selected per-channel
  Console settings listed above, not channel volume. Phantom activation requires
  separate safety confirmation; Mic/Line selection is disabled while phantom is
  active to avoid indirect switching. Unsupported controls are omitted.
- Apollo's AUX pre/post setting affects a whole bus. It must not be exposed as
  though each input owns an independent pre/post switch. PostFaderOverride's
  undocumented three-state behavior is not guessed.
- New arrays are populated under their Freeze/Thaw scope before publication.
  Store SDK-assigned member IDs; never infer them from surface positions.
  Children are detached before destruction; owned cells outlive registration.
- Callback work is limited to copying primitive values, IDs, timestamp, thread
  and permission epoch into a bounded queue. All UA I/O runs on the writer.
  No callback pointer survives the callback. No backend lock encloses SDK calls.
- Touch sensors and child-navigation presses use the documented two-state Raw
  switch mode, not a press-only MultiState latch. This follows the switch enum
  contract and the official common knob-cell initializer; physical release must
  clear touch. Re-read guide section 7 before this callback-state change.
- Preserve SDK return codes in diagnostics. Exceptions unwind registration.
  Rejected, changed-identity, expired or unconfirmed operations discard their
  own pending work. The desktop preserves the user's explicitly selected access
  and rebinds from a fresh model; possibly executed writes are never retried.

## Apollo evidence and verification boundary

Read-only local discovery showed named AUX/Cue sends, an enabled-choice output
destination enum, preamp children, eight insert slots, and loaded plug-in
parameters with normalized values, display strings and optional step enums.
Raw captures remain in private research outside Git. Node and property names
are observations of the local engine, not a supported public UA API promise.

UA's own [Sends documentation](https://help.uaudio.com/hc/en-us/articles/25350937974676-Sends)
documents send level/mute/pan and bus-wide AUX pre/post. Its
[Monitor Mix Controls](https://help.uaudio.com/hc/en-us/articles/25351484855828-Monitor-Mix-Controls)
documents dual channel pan and unavailable send pan on stereo-linked inputs.
The [preamp reference](https://help.uaudio.com/hc/en-us/articles/25349542599956-Preamp-Controls)
defines the plain native gain range, Hi-Z override, phantom-power hazard, Pad
scope and line gain bypass. Actual presence/enabled/read-only metadata and a
fresh per-write check remain mandatory; no manufacturer-name branch is used.

While the owner is away, tests use an isolated synthetic engine only. Real Apollo
routing, monitor level, sends, preamps and plug-ins must not be changed. Builds
go to a new output directory and do not replace or launch the running bridge.

## Knob value and switch presentation contract - 2026-09-01

Concepts: primitive value text, temporary encoder text, knob-cell upper/lower
switches, LEDs and peer knobs. Re-read the guide's primitive-control and complete
knob-set sections, the installed EuPrimitiveControl, EuPrimitiveKnob,
EuPrimitiveSwitch, EuControlKnobCell and EuControlKnobCellArray declarations,
then checked EuConIO before EuConApp and the focused peer-knob example. The Avid
S3 guide was used only to confirm the physical mapping of In and Sel.

- A knob's numeric/index table is the control value and ring source. Its 4-, 8-
  and long strings provide the temporary value text selected by EUCON while the
  encoder is touched or moved. The label primitive remains the stable parameter
  name; callback code must not rewrite it on the EUCON callback thread.
- Publish explicit dB strings for send level, preamp gain, Control Room level and
  DIM amount. Use `-INF` only for a real fader floor; retain signed dB values for
  finite attenuation. Pan and plug-in choice/percentage text keep their existing
  semantic formats.
- A binary parameter is an upper/lower switch plus its LED, not a fake knob or
  progress bar. Its value table also carries OFF/ON strings for surfaces that
  present switch state text. Switch-only cells keep the encoder ring off.
- S3 In maps to the knob-cell lower switch. S3 Sel maps to the upper switch and
  is intended for a genuine secondary parameter/state, such as an EQ F/Q peer,
  stereo Pan peer, or per-send pre/post. Encoder press is the knob-top switch:
  it remains EUCON hierarchy navigation for cells with children, while a Pan
  cell without children uses a documented one-shot top switch to reset its own
  mono/left/right parameter to center. These are distinct primitives.
- Apollo currently supplies a verified stereo Pan peer, so Sel is meaningful
  there. It does not expose a verified per-send pre/post parameter or plug-in
  peer relationship; do not populate Sel merely to make every button active.
  Apollo's AUX return pre/post is bus-wide and remains in Input as previously
  agreed, not misrepresented as per-send pre/post.
- Pan keeps the predefined `kNAM_Pan` knob set and the native left/right Pan
  layout tags. Mono publishes one left/mono cell; stereo publishes independent
  left and right peer cells. Pressing the active Pan encoder resets only that
  cell to `C` (native value zero), so centering one stereo leg cannot move the
  other. The channel-level `kNAM_PanClear` semantic clears a whole panner and is
  not substituted for this leg-specific encoder action.
- All Config OLED labels and choice text are uppercase for consistent S3
  legibility. This is display-only: native UAD enum strings, including the
  hardware Function-switch assignment value, remain byte-for-byte unchanged
  when written.

The isolated Release build is
`artifacts/uad-value-display-review/Release/ApolloBridge.Eucon.exe`. Core tests
pass 15,290 checks, transport tests pass, and desktop settings pass 12 checks.
The expanded real-SDK text/readback regression compiled but was not run while
the production-identity bridge remained active. No running process, EuControl
state or Apollo parameter was changed. Physical S3 and Avid Control acceptance
is still required for the temporary-value timing and switch presentation.

Live S3 observation later that day showed that the surface does not render a
knob-cell switch value table as a separate OLED toggle or persistent OFF/ON
readout. The In LED reflects the lower switch correctly, while the fixed empty
encoder-region outline remains even with the ring set to Off. Treat that outline
as surface chrome, not parameter progress. A short trial that appended OFF/ON to
the persistent OLED label reduced clarity and was removed by user decision.
Switch-only cells retain their plain parameter name and use the In LED as the
authoritative state indication.

## Pre-write gesture lifetime correction - 2026-09-01

Live diagnostics proved that the former 500 ms pre-write deadline could expire
during ordinary scheduling and incorrectly disarm the whole channel or Control
Room permission. This was a local policy fault, not a UA rejection or a changed
device identity.

- Per-field coalescing remains in place, so rapid encoder/fader input still
  produces one latest absolute target rather than flooding the engine.
- A request now has a five-second pre-write lifetime. Both the EUCON dispatch
  path and the writer enforce it.
- An expired request is never sent and only that request is removed. It does not
  change the permission epoch, clear other fields, or force the user to unlock
  Control Room again.
- Identity/capability changes, a definite UA rejection, and uncertain
  post-write readback still revoke the affected permission. Possibly executed
  writes are still never retried.
- Queue extraction no longer owns a separate fatal timeout; the writer performs
  the single final freshness decision and can clean up the matching pending
  state without conflating cancellation with an unsafe write.

The isolated Release build is
`artifacts/uad-control-stability-review/Release/ApolloBridge.Eucon.exe`.
Core tests pass 15,290 checks, transport tests cover nonfatal expired channel
and Control Room requests, and desktop settings pass 12 checks. The active old
process was not replaced while the user was operating it; physical S3 and Avid
Control acceptance remains required.
