# Config page experiment

Checkpoint before this experiment: `7de5f97` (local only, no push).

## Contract recorded before implementation

Concept: knob-set configuration pages and primitive text feedback. Sources read:
the separately obtained SDK guide 8.2.1–8.2.10 and S3 guide p46; installed
EuControlKnobCellArray, EuControlKnobCell, EuControl and EuPrimitiveKnob contracts;
basic EuConIO Input construction, then EuConApp Inserts configuration pages.
No vendor source or documentation is included here.

- Populate cells first; keep the returned member IDs. Mark the first configuration
  cell with NewConfigPage(member ID), never a positional index. GetConfigPages
  must return that same ID. Normal/config separation and hardware navigation
  belong to EUCON; do not intercept Config or replace the registered node.
- All structural mutations happen on the existing owner thread under the
  smallest existing Freeze/Thaw scope. Controls receive their owning processor
  in their constructors. Detach parent references before removing child arrays
  and releasing our objects. Keep configuration cell persistence IDs distinct.
- Existing primitive callbacks only decode and enqueue. Configuration aliases
  use existing channel/monitor dispatch, epochs, touch deferral and safety gates.
  Global preview cells have no writable address and cannot enqueue commands.
- Check all SDK results, read back page markers, and log control/member IDs and
  owner thread. Preserve existing callback diagnostics and error handling.
- Opt in with --experimental-config; ordinary startup retains existing pages.
  Build into a separate output directory. Do not change Quick Controls, native
  Monitor semantics, Mixer/Close behavior, or runtime/surface settings.

## Expansion contract — screenshot allowlist (2026-08-31)

Before editing the expanded model, guide sections 8.1–8.3 (including the Inserts
diagram), installed knob-array/cell/primitive/control contracts, EuConIO Input
construction and EuConApp Inserts configuration construction were reviewed.
Native INSERTS Config chooses a slot's plug-in; the loaded plug-in's own Config
chooses its preset. Child entry/Back and page sizes remain runtime-owned.
The same existing owner-thread Freeze/Thaw and callback queue rules above apply.

New configuration writes use an independent, explicitly enabled permission, not
channel or monitor permission. Rotation only stages an enum choice; the lower
switch confirms it. Plugin/preset lists use the native lower-switch selection.
Every request re-reads identity, current value and choice availability before a
single bounded write, then reads the complete node back; uncertain writes are
never retried. Disconnection, stale state and identity changes invalidate work.
No SDK, firmware, application preference files or opaque AppCommand are written.

Only the user's screenshot properties are in scope. The old clock-lock,
streaming and default-I/O readouts are removed. ALT count and MIDI event/channel
preferences are deferred where no verified single live engine property exists;
do not simulate them with multi-output writes or edits to Console preferences.
Preset recall is distinct from preset save/overwrite and from whole-console
plug-in scenes. Only existing authorized plug-ins and advertised preset files
may be offered. No demo activation or preset overwrite operation is added.

UA's Apollo x8 Hardware Manual explicitly excludes the front-panel METER switch
from Console remote control. No corresponding engine property was found.
PostFaderMetering is Console metering and must not be labelled hardware IN/OUT.

## Initial prototype (historical, read-only global preview)

INPUT Config exposes applicable source/reference/SRC, phantom, AUX bus setup and
TB-to-monitor aliases. MIX Config exposes the existing output choices. Their
normal pages stay intact for this experiment. CR Config exposes existing DIM
depth and monitor source choices behind the independent monitor authorization.
CONSOLE Config is a live, explicitly read-only global configuration preview.
Missing values display N/A and disconnection clears old values.

No invented plugin loader/preset selector or configurable fixed AUX destination
is provided. INSERTS, UNISON and AUX get no Config page until UA operations and
their safety contracts are verified. No new global write path is implemented in
this navigation-first experiment (including metering preferences).

## Acceptance pending

SDK page-marker/readback tests do not prove hardware presentation. S3 and Avid
Control still need verification: Config entry/exit from INPUT/MIX/CR/CONSOLE,
normal-page preservation, paging, source labels, parameter/LED feedback, touch
and cross-channel isolation. Do not claim all 16 physical displays are available
under every EuControl assignment or built-in I/O display mode.

## Pre-existing short-label issue encountered during regression

The first actual SDK run of the prior CR short-label regression stopped before
the new Config checks: ChangeText(CR, CR, CONTROL ROOM) returned OK, but the
4-character readback was CONT. A separate unpublished primitive using the
documented three-string LoadValueAt returned OK and preserved all three strings.
The label helper now logs the discrepancy and, only on mismatch, loads the
explicit single-entry table under primitive Freeze/Thaw, checking each width
before and after Thaw. An unchanged long name can also cause LoadValueAt to keep
the old short variants, so this isolated label-only path resets and initializes
that table before loading. It checks type and size first; no other primitive or
control is reset. Errors remain fatal. This is an observed-runtime compatibility
path, not a claim about every SDK/runtime or surface; hardware acceptance is
still required. No vendor implementation was inspected or copied.

## Initial prototype build (historical)

```powershell
.\scripts\build-apollo.ps1 -NativeOutputName apollo-config-experiment -TransportTests
& .\artifacts\apollo-config-experiment\Release\ApolloBridge.Eucon.exe --experimental-config
```

Close the other Apollo/EUCON adapter first. The title includes CONFIG EXPERIMENT.
Connect EUCON in the application, attention a channel, enter INPUT, MIX, CR or
CONSOLE, then press the S3 Config button. Use Page/Back as provided by EUCON.
Do not use the S3 right-side Page-pair gesture: that toggles its built-in I/O.
CONSOLE contains only actual interface settings. Unavailable values say N/A and
after disconnection OFFLINE. Permission state remains in the desktop Settings UI
and does not consume an upper-knob cell.

Normal launch without the flag does not add Config pages. Prior build output is
untouched. Experimental source changes remain separate from the checkpoint.

## Initial prototype verification — 2026-08-31

- Release and Debug: 15,146 pure assertions each, 233 real-SDK text/model
  assertions each. SDK fixtures attach processors only to an unpublished node;
  they do not register an application or connect to Apollo/MIDI/audio.
- Both synthetic loopback transport suites passed. Both experimental-mode
  offline smokes passed with all permissions locked and no external handles.
- Marker tests cover ordinary/experimental construction, distinct normal vs
  configuration boundaries, monitor detach/reattach, unchanged quick controls,
  and global preview value updates/clearing without writable bindings.
- An 8-second real read-only observation found 13 channels, one monitor and all
  11 global configuration values. 62 connected samples, zero stale samples,
  maximum snapshot age 1,042 ms, stable generation 1. No moving meter signal was
  present; this is not meter accuracy verification.
- No GUI was launched, no surface/runtime settings changed, no hardware/audio
  writes sent, and no push performed. Hardware Config navigation and display
  acceptance remain pending. The Config work is deliberately uncommitted above
  checkpoint 7de5f97 until the user evaluates it.

## S3 encoder-position display contract — 2026-09-01

The S3 OLED reserves an encoder-position region for Channel Encoders and Channel
Control knob cells. Avid's own S3 illustrations retain the empty outline even
for top-level semantic entries such as Inserts, Input, Dyn and EQ; the processor
SDK does not expose a separate OLED scene or pixel-layout selector. The public
model controls the encoder position indication through `EuPrimitiveKnob` ring
mode, not the surrounding S3 display chrome.

Continuous and rotatable selector parameters therefore retain their applicable
ring modes. Directory, switch-only and fixed/read-only cells explicitly use
ring-off; their knob primitive remains uninitialized when no rotation is
supported. This follows the official EuConIO Input example for its switch-only
Phase cell. Callbacks, ownership, construction order and Freeze/Thaw scopes are
unchanged. The empty S3 outline may remain by design and must not be hidden by
misusing an unrelated display or automation primitive. Physical S3 and Avid
Control presentation still require acceptance.

### Semantic ring-mode refinement

Before implementation, the SDK guide's knob-cell and value-table sections, the
installed `EuPrimitiveKnob`/`tRING` declarations, the EuConIO Input construction,
the EuConApp Input/Pan/plug-in examples, and the current S3 encoder-display guide
were checked again. Ring mode changes are static processor-model metadata: they
do not change value tables, callbacks, touch handling, ownership, page structure
or write dispatch, and therefore require no additional Freeze/Thaw at runtime.
Every SDK result remains checked during the existing frozen construction scope.

The bridge uses ring-off for directory, fixed/read-only and switch-only cells;
point for indexed choices, preamp gain and plug-in parameters whose polarity is
not reported by Console; left thermometer for send level, monitor level and DIM
depth; and center-anchored for channel/send pan. Right, centered and inverse
thermometer modes remain unused until Console supplies an unambiguous matching
parameter semantic. This avoids inferring Q, width or bipolar gain from a plug-in
name or formatted value. The S3's fixed empty outline is surface chrome and may
remain around every mode.

## Current expanded build

Output: `artifacts/apollo-config-expanded/{Release,Debug}/ApolloBridge.Eucon.exe`.
Pass `--experimental-config`; the old experimental executable is not replaced.
See the Chinese guide for operating instructions. New configuration authority is
separate from both audio-channel and Monitor authority, and defaults locked.
An indexed selector uses a non-actionable current-value entry plus the available
choices. Turn to preview, then press the lower switch (In) to confirm within ten
seconds. The plug-in library uses native short names where available, retaining
the canonical name in the long display and in the typed command. Navigation and
rotation do not load a plug-in. No demo license is activated.

Supported experimental write paths:

- INSERTS Config: real slot -> child PLUGIN selector -> explicit load/NONE.
  Inside an existing ordinary insert: Config -> PRESET selector. Only advertised
  preset files for that authorized plug-in are listed; folders are not commands.
- UNISON Config: the dedicated preamp insert exposes explicit NONE/unload and
  only catalogue entries whose live metadata reports both `Authorized=true` and
  the native boolean `Unison=true`. Loaded UNISON plug-ins expose their advertised
  preset files through Config. The display label is abbreviated only for the
  surface; the write retains the complete native plug-in name.
- CR Config: active Cue MONO, MIX/CUE source, available mirror outputs; headphone
  source assignment. These settings do not create new channel strips or alter
  native Monitor semantics. C1MONO/C1MIRR/C1SRC labels remain distinct at four
  characters. Runtime-owned page sizes and existing CR controls are retained.
- CONSOLE Config: screenshot-listed sample rate, clock, buffer, delay compensation,
  Cue count, headroom, digital mirror, hardware Function-switch assignment,
  Console meter position,
  peak/clip hold, editing mode and MIDI input device, when reported by the engine.
  No clock-lock/streaming/default-I/O status clutter remains in this page.

Not implemented: atomic ALT count, MIDI channel/event/note preferences, saving or
overwriting presets, hardware METER IN/OUT. No preference
file edits, guessed AppCommand payloads, multi-output ALT emulation, GUI scripting
or firmware access are used. Existing UNISON parameter and power controls are
unchanged. The public manuals describe semantics, not a supported TCP API;
metadata observations and synthetic tests are not hardware write acceptance.

Safety checks additionally cover changed choices/current values, stable hardware
identity, generation, expiring selections and confirmation, independent arming,
one in-flight operation, complete-node readback distinct from a scalar echo,
post-write hardware identity, and no retry after rejection/uncertain completion.
Monitor Config callbacks carry a structural revision so an old member ID cannot
inherit a rebuilt setting. Unrelated knob pages are retained where their own
schema is unchanged. All SDK mutations stay on the owning thread.

Sources consulted (separately obtained Avid SDK remains outside Git):

- [UA Hardware Settings](https://help.uaudio.com/hc/en-us/articles/25403573794836-Hardware-Settings-Panel)
- [UA Cues](https://help.uaudio.com/hc/en-us/articles/25351037512340-Cues)
- [UA MIDI Settings](https://help.uaudio.com/hc/en-us/articles/25403799874068-MIDI-Settings-Panel)
- [UA Plug-In Scenes](https://help.uaudio.com/hc/en-us/articles/28354758319892-Plug-In-Scenes)
- [Apollo x8 Hardware Manual](https://media.uaudio.com/support/manuals/hardware/Apollo%20x8%20Hardware%20Manual.pdf)

### Expanded verification — 2026-08-31

- Release/Debug: 15,155 pure checks and synthetic ephemeral-loopback transport
  suites passed. New transport cases exercise cue/HP routing, owned plug-in
  loading, preset recall, changed identity/current value/choices, cancellation,
  rejection and misleading scalar echoes. No real audio writes are in these tests.
- Release/Debug real SDK: 238 checks each. Tests include nested Config markers and selector readback, ordinary
  startup preservation, changing global values without rebuilding the plugin
  page, and the existing directory/CR/UNISON regressions. Hardware presentation
  and all newly added real writes still require S3 + Avid Control acceptance.
- An 8-second read-only live observation found 13 channels, one Monitor, 129
  configuration entries and zero stale samples; maximum snapshot age 1,093 ms.
  No moving meter signal was present. No actual plug-in, preset, clock, headroom,
  cue, MIDI or preference change was sent to the real interface.
- Final 12-second read-only check: 83 connected samples, 13 channels, one
  Monitor, 11 global readouts, 129 configuration entries, zero stale samples,
  maximum snapshot age 1,058 ms and stable generation. Both offline smokes passed.
- No commit or push of this experiment; checkpoint 7de5f97 remains intact.

## UNISON Config extension — 2026-09-01

The EUCON concept is the custom UNISON knob set's native configuration page.
The complete SDK guide Section 8 was reread together with the installed knob-cell
array/layout/category contracts. EuConIO has no matching UNISON loader; the
official EuConApp plug-in and instrument examples confirm the documented
`NewConfigPage` boundary and `AddChild` hierarchy used here. Construction remains
on the existing model owner thread, callbacks continue to enqueue to the single
CONFIG writer, and Page/Back plus physical page width remain EUCON-owned.

A read-only live catalogue inspection found 47 entries reporting native
`Unison=true`; 25 of those also reported `Authorized=true`. No names or vendor
catalogue content were stored. The production selector uses that exact pair of
properties, never category/name inference, so non-UNISON and unauthorized entries
are excluded. NONE remains the first choice and unloads explicitly. Every load,
unload, or preset recall receives the same fresh identity/current-value/choice
validation and complete effect-node readback as ordinary inserts, with no retry
after an uncertain result.

Isolated Release output `uad-unison-config-review` passed 15,294 core assertions,
the synthetic transport suite including a UNISON load and instance confirmation,
12 desktop settings checks, and offline experimental smoke. It did not publish an
EUCON application or write to real hardware. Physical S3 and Avid Control Config
entry, pagination, confirmation, load/NONE, preset and refreshed parameter-page
feedback remain user acceptance items.
