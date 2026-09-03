# Upper Channel Control implementation contract

Status: initial upper directory failed physical acceptance (duplicate Input).
The navigation-binding correction passed isolated builds and tests; it is not
yet accepted on S3 and Avid Control.

## Official model and ownership

Concepts: top-level knob-set hierarchy, shared child navigation, primitive
feedback, touch and callback authorization. Read SDK guide section 8 in full,
EuLayoutChannel, EuControlKnobCell/Array child contracts, and existing node,
processor and primitive contracts. Checked EuConIO first (no explicit top-level
hierarchy), then EuConApp top-level and custom knob examples. Consulted the S3
2025.12 guide Channel Control section. No vendor contents are copied here.

- Publish one owned top-level knob array per logical channel. Its children are
  the existing Input, Inserts, Aux, Mix and Pan arrays, not duplicate writable
  models. The runtime owns upper/lower attention, navigation, paging and peer
  panners. No device-name branch or surface assignment command.
- Keep standard function layout/persistence identifiers. Do not use layout enum
  or focus-area indices as explicit directory positions. Preserve unused
  standard/custom positions as inert cells so later functions do not move.
  Do not invent EQ/Dyn/Group implementations.
- Keep custom slot 11 (current Console/quick controls) unchanged. Reserve custom
  slot 9 for UNISON and slot 16 for Control Room. Their names and identities are
  application-specific; this is not the S3's built-in AVB I/O.
- Store IDs returned by PushBack. Attach children only after initialization;
  detach parent references before replacing or deleting child arrays. Freeze
  the owning processor for structural changes. Keep top-level identity alive.
  All SDK calls and cleanup stay on the owner thread with checked results.
- Callback work copies values, timestamps and the correct permission epoch only.
  Control Room aliases share the existing MonitorController and volume ceiling;
  channel arming never unlocks monitoring. No duplicate monitor audio state and
  no monitor fader track. Ambiguous/absent monitoring exposes no writable aliases.
- UNISON is discovered from preamp-owned effects, distinct from channel inserts.
  Native normalized/enum metadata is authoritative; never reinterpret raw
  preamp gain as a generic dB knob with a loaded UNISON effect. Retain instance,
  preamp, source and field metadata checks before writes. Never load a plug-in,
  change hardware gain-stage mode, or enable 48V automatically.
- UNISON processing is always in the recording path, regardless of the ordinary
  inserts' REC/MON state. Display actual parameter names and engineering text;
  unknown, disabled or read-only fields are not exposed as writable controls.
- Require explicit sensitive-control confirmation for all UNISON writes: a
  generic parameter name cannot establish whether it changes analog gain,
  impedance or phantom. Line Gain Bypass disables its writes without relabeling
  the loaded plug-in as an empty slot.

## Verification boundary

### Duplicate Input correction — 2026-08-31

The user's S3 photo shows native INPUT at position 2, DYN at 3, and our Input
label at 4. The old directory bound Input to position 4, while leaving position
2 blank. Input itself already used the native `kNAM_Input` and
`kChanFuncID_Input`; it was not registered as a custom function. The mistake was
the explicit parent directory's ordering, not the channel Input implementation.

There is a source discrepancy: guide 8.1's index table and 8.3's display-order
list differ. Section 8.3 places Input/Dynamics/EQ second/third/fourth, consistent
with the installed EuConApp parent-tree example and the observed native labels.
The focus-area values in EuDefinitions also differ from those parent positions.
For this correction, change only the first four directory entries to match the
8.3 display convention. Keep Aux/Pan/Group/Mix and all custom positions unchanged
(8.1, the installed example and the photo agree for the observed Aux/Pan entries).
The disagreement about Group in 8.3 remains documented, not silently generalized
into a new order or a device-name branch.

Implementation contract: retain one owned directory, share existing child arrays, and
perform structure changes on the owner thread inside the existing processor and
parent-control Freeze/Thaw scopes. Detach before child deletion; callbacks remain
queue-only. Resolve bindings by function rather than caller-supplied indices or
labels, validate the child's native layout before linking, and log binding IDs,
layout, function, thread and time with checked SDK return codes. Add pure tests
that pin the navigation order and custom-page boundary, plus SDK readback checks
for native identity, child references, missing functions and wrong-parent refusal.
Do not alter quick controls, surface assignments, focus indices or the audio path.

Build separate binaries and run only pure and loopback-fixture tests now. Do not
launch a production adapter, register a test node, change EuControl or send real
audio writes. S3 + Avid Control must later verify top-level names/order,
independent upper/lower navigation, page/back, stereo Pan peers, monitor safety,
UNISON instance replacement and feedback. No acceptance is claimed before that.

Automated results for the correction: Release and Debug each pass 14,679 pure assertions and the
loopback transport suite, including nested UNISON discovery/write/readback,
stale-instance rejection, per-preamp addresses, monitor permission isolation and
signed DIM conversion. The additional 70 pure checks cover directory order,
single-entry identities/labels, standard/custom separation and fixed custom
positions. Native binaries are isolated in `artifacts/apollo-upper-binding`.
The explicit SDK regression now also checks parent identities/positions/labels,
child layouts, rejection of Input linked under EQ, shared-child references and
monitor/UNISON replacement. It compiled but was not run while the user's original
`apollo-upper-control` adapter remained active. No automatic restart or live UA
write was performed. Full physical acceptance remains outstanding.

### Uppercase navigation labels — 2026-08-31

At the user's request, all application-published top-level function labels now
use uppercase, including INSERTS, INPUT, AUX, PAN, MIX, CONSOLE and CONTROL ROOM.
This is display text only: native identities, directory positions, child
bindings and quick-control contents are unchanged. Channel and plug-in names
are not converted. The uppercase invariant has a pure regression check.
The separate output directory for this presentation change is
`artifacts/apollo-upper-labels`; the running binding-correction build is preserved.
Release native build passed; Release and Debug each pass 14,690 pure assertions.
Transport tests were not repeated for this label-only edit. No SDK/runtime or
physical test was run and no application was restarted.

### Control-room short label contract — 2026-08-31

Concept: primitive string values, not layout or function identity. Re-read guide
3.2.2 and the EuPrimitiveControl/EuPrimitiveTextDisplay string-table contracts;
checked the basic Input labels in EuConIO, then EuConApp's knob-label setup.
Publish explicit four/eight/long text versions: CR / CR / CONTROL ROOM. Other
top-level labels retain their previous short forms. Populate the existing text
primitive before updates, then use the documented three-string ChangeText on
the owner thread. Verify all three stored strings, including any AlreadySet
status; do not broaden non-text error handling. Keep the existing parent freeze
scope, child lifetime, callback behavior and function IDs unchanged. No surface
font control or device-name condition is introduced. Physical width selection
still belongs to EUCON and needs user verification.

The output is `artifacts/apollo-cr-label/Release/ApolloBridge.Eucon.exe`.
Release host build and Release/Debug pure tests passed (14,738 checks each).
SDK regression coverage now includes CR at both short widths, full-name
preservation, repeated updates, clearing/reappearance and detecting an unwanted
automatic truncation. These SDK tests compiled but were not run alongside the
active adapter. No audio commands, surface settings or running process changed.

Sources: [S3 guide](https://resources.avid.com/SupportFiles/ProMixing/Avid_S3_Guide_v2025.12.pdf),
[UA UNISON](https://help.uaudio.com/hc/en-us/articles/26485044036756-Unison).
The separately obtained SDK remains outside Git.
