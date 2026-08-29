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
| Volume knob semantics | Needs work | Volume currently borrows the predefined Input knob-set layout. Confirm the correct standard model or use an official knob-map strategy. |
| Meter API 3.1 | Needs investigation | Setup calls return `kERR_OK`, but dynamically registered tracks did not receive the documented `VisibilityChangedV2` callback in the verified EuControl setup. The adapter now trusts callback state instead of querying/inventing visibility and uses an isolated, verified legacy write only while no valid 3.1 handle exists. |
| Device-derived behavior | Needs review | Forced refresh and overtravel rebound came from hardware testing. Classify them as generic application policy or remove them after cross-surface tests. |
| Optional global processors | Needs review | Add Project, System, Monitor, Transport, or Assignable Knob only when Windows Mixer exposes the matching standard semantics. |

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
4. Move remaining SDK output out of callback threads.
5. Instrument meter negotiation and retire the ordinary fallback only when the
   documented Meter API 3.1 path is reliable on all attached test surfaces.
6. Run the same regression matrix on S3 and Avid Control: discovery, banking,
   assignment/layout recall, fader/touch/motor, encoder/touch/ring, mute/LED,
   labels, and peak meters.
