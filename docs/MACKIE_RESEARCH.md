# Mackie Control research boundary

Status: independent research branch; not a replacement for the verified EUCON host.
Date: 2026-08-30. Baseline: `e57eca4`.

## Architecture decision

`Windows audio -> logical tracks/commands -> Mackie surface model -> MIDI transport`

The new executable is **Windows Fader Bridge for Mackie Control**. It does not
load Avid libraries, discover EUCON surfaces, open EuMidi automatically, modify
EuControl, or replace the existing WindowsFaderBridge.exe. The existing project-
owned NativeAudioController, WindowsCommandExecutor and WindowsMediaController
are compiled into a separate target as a transitional shared source boundary.
The media and command implementations are unchanged. NativeAudioController has
one additive, protocol-independent keyed command entry point: Mackie volume,
pan, mute and default-device requests resolve their stable identity on the audio
worker immediately before applying the write. This prevents a recycled Core
Audio slot from receiving an old track's request. Existing EUCON entry points
and model code are unchanged; its idle keyed queue does not allocate/drain.
A later shared-library extraction requires its own EUCON regression pass.

Mackie Control is not EUCON: it sends MIDI control messages, not an application
object model. This adapter therefore owns track identity, bank selection, LED
feedback and touch arbitration. An eight-strip MIDI bank is a viewport, not an
eight-track application limit. Channel 9 pitch bend is the master fader.

Device profiles express capabilities and reserved ports. The packet codec and
Windows audio engine must not depend on a device name. P1-Nano's single physical
fader represents eight virtual strip faders plus master; it must not be treated
as a one-channel application. Its touchscreen editor and firmware remain owned
by iCON iMAP, not by this bridge.

## Source ledger (no third-party files are redistributed)

- [iCON P1-Nano product/download page](https://iconproaudio.com/product/p1-nano/).
- P1Nano-PD3V102-English manual, pp. 9-18 and 43-54: controls, DAW port mapping,
  iMAP lifecycle, user MIDI messages, hotkeys and saved configurations.
  [Official PDF](https://s3.amazonaws.com/assets.iconproaudio.com/wp-content/uploads/2023/07/01050723/P1Nano-PD3V102-English.pdf).
- [P1-Nano firmware release notes](https://s3.amazonaws.com/assets.iconproaudio.com/wp-content/uploads/2023/07/10071915/P1-Nano-Firmware-Release-Notes-4.html):
  1.24 addresses selected-channel/fader mismatch. USB names alone cannot prove
  the installed version (1.23 removed version text from names).
- [Mackie MCU Pro / XT Pro owner manual](https://mackie.com/img/file_resources/MCU_Pro-XT_Pro_OM.pdf):
  controller modes and architecture; not a complete byte-level protocol reference.
- [Emagic/Mackie Logic Control manual, chapter 13, pp. 105-123](https://images.thomann.de/pics/prod/151261_manual.pdf):
  manufacturer-authored MIDI implementation, hosted by the retailer. Documents
  pitch bend, touch/LED notes, signed-magnitude encoders, LCD, ring and meters.
  This is the older **Logic Control** model, not a claim that every MCU extension
  is specified by this manual.
- [Mixxx P1-Nano integration documentation](https://manual.mixxx.org/2.7/en/hardware/controllers/icon_p1_nano)
  and its [upstream implementation](https://github.com/mixxxdj/mixxx/blob/main/res/controllers/Icon-P1Nano-scripts.js)
  independently corroborate P1-Nano virtual faders and MCU model ID 0x14/LCD
  framing. Observed iCON vendor extensions there are not treated as official
  contracts and are not copied into this project.
- [Microsoft midiInOpen](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-midiinopen),
  [midiOutLongMsg](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-midioutlongmsg),
  [midiOutReset](https://learn.microsoft.com/en-us/windows/win32/api/mmeapi/nf-mmeapi-midioutreset):
  WinMM handle, callback and buffer ownership.

## Implementation contracts

### Independent devices and desktop refinement (2026-08-30)

No MIDI message definition changes. The same codec/surface implementation is
instantiated per independently configured main unit (up to 16), each with its
own transport, touch identity, feedback, bindings and bank/selection. One shared
Windows audio engine remains authoritative; touching any surface defers topology
compaction on the other surfaces as well. This is not Extender aggregation.

`workspace.txt` migrates the existing primary `settings.txt` without rewriting
bindings. New devices have their own project-owned-ID file. Device removal keeps
the file for recovery. Automatic connection defaults on, but enumeration opens
nothing: only an explicitly saved, unambiguous, non-reserved pair can reconnect.
Manual disconnect pauses retry; disconnected devices cannot claim another
device's active ports. The UI selects an editing context without changing other
devices' connections. Two-device state, connection policy, migration and file
isolation are covered by pure tests; simultaneous physical hardware still needs
two-device acceptance. No new physical pair was guessed or opened.

The native UI keeps command descriptions but removes slogans and public debug
access. Dark dropdowns, checkbox-backed switches and consistent card insets
retain native keyboard controls. The developer dashboard is reachable only via
`--diagnostics` at startup. Folder opening uses a Shell PIDL so Explorer reaches
the actual directory even under packaged-host AppData redirection.

### Playback-time display contract (2026-08-30)

Consulted the manufacturer-authored Logic Control manual, pp. 116-118 and 123,
and P1-Nano manual p. 9. The digit feedback contract is MIDI channel 1 CC
0x40..0x49, indexed right-to-left. Digits use the documented seven-segment
character codes; bit 0x40 adds a decimal point. Use individual CC writes, not
a guessed iCON SysEx extension. Compatibility of this older documented path
with P1-Nano's Cubase mode still requires a physical screen check.

The application renders elapsed HH.MM.SS while playing and during a ten-second
pause hold, then switches to local 24-hour system time. Missing/invalid media time
uses the system clock immediately; elapsed values beyond 99:59:59 are not wrapped.
Unused digits are blank, not musical bars or frame-accurate SMPTE. Feedback belongs to the owning surface thread,
is checked at 5 Hz, and sends only changed digits; reconnect invalidates the
cache. It does not change iMAP presets, firmware or transport input mappings.

Windows supplies Position at LastUpdatedTime, StartTime/EndTime and optional
PlaybackRate through GSMTC. Readable time must not depend on permission to seek
or on MinSeekTime/MaxSeekTime. Interpolate only while Playing with a known,
finite playback rate and a valid timestamp; paused/unknown-rate state uses the
reported position. Clamp to the reported media bounds. Never fabricate song
progress for a player without a timeline. These additional reads are opt-in for Mackie;
existing EUCON GetState calls retain their previous percentage/capability path.
Provider failures clear the additional time state, not existing controls.

Microsoft contracts: [timeline properties](https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssessiontimelineproperties)
and [optional playback rate](https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssessionplaybackinfo.playbackrate).

The idle display policy uses [GetLocalTime](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-getlocaltime)
for local wall time and [GetTickCount64](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/nf-sysinfoapi-gettickcount64)
for the ten-second hold. Source/title/artist, timeline bounds or reported position
changes reset the hold; repeated polls and provider timestamps do not. Resume or
an acknowledged seek restores media display at the next feedback cycle (up to
200 ms), without requiring seek capability for reading. Clock output uses the
same existing digit codec and has no effect on Transport LEDs or playback.
The dashboard names the display source to distinguish clock from media time.

- Short input messages are processed on an owning window thread, not inside a
  MIDI driver callback. Audio writes are queued to the existing audio worker.
- Shell commands, media calls and window restoration do not run inside the MIDI
  message dispatch. Window identity includes process, executable and package
  identity; activation remains subject to Windows foreground/security rules.
- Keep prepared SysEx memory alive until the driver returns it. Reset/unprepare
  before closing. Never free an in-flight MIDIHDR.
- Accept fader movement only with explicit touch by default. Suppress motor
  output during touch; prevent banking/flip changes mid-gesture. Keep touch
  bound to a logical key, not a changing enumeration index.
- Windows is authoritative for LEDs and motor feedback. Brief pending-write
  reconciliation handles in-flight audio snapshots; it expires and must not
  hide an external Windows update indefinitely.
- Only active logical tracks receive meaningful controls. The visible Mackie
  order is dense: offline identities are removed and new identities append.
  While any fader is touched, topology is frozen and offline targets become
  inert; compaction occurs only after the final release. State and pending
  writes remain keyed by logical identity, never inherited by a new strip.
- Encoders now serve the selected logical track: 1 volume, 2 Pan/push center,
  3–8 independent left/right/push command bindings. Faders remain volume.
  Track/Pan assignment and Flip cannot silently exchange these roles.
- Encoder steps use signed magnitude; 0x40 is zero, not a decrement. Absolute
  MCU fader values are linear Windows scalar volume in this prototype. No
  device-specific unity position or dB law is guessed.
- Standard meter packets contain peak level 0..12, with 14/15 reserved for
  overload set/clear. A stereo pair uses its maximum peak. Do not claim standard
  MCU carries EUCON's independent multichannel metering or RGB strip colors.
- LCD updates are coalesced to 5 Hz and rings to 20 Hz so traditional MIDI links
  are not treated as unlimited-bandwidth USB. Faders/LEDs are change-driven;
  meters refresh at up to 20 Hz because surfaces may decay them locally.
- Output is limited to documented control feedback. Do not send reset, reboot,
  touch calibration, firmware, iMAP programming or other vendor SysEx.
- MCU clone handshake requirements, iCON proprietary color/meter extensions,
  D5-specific display offsets and HUI are not silently inferred. Record them as
  unverified limitations until authoritative documentation / hardware evidence.

## Workstation inventory

Read-only discovery found four P1-Nano MIDI input/output pairs and an installed
P1-Nano iMAP. C++ v143/Windows SDK and .NET 8 test tooling are already available.
No driver installation is required to begin. Downloaded manuals and the iCON
Smart Install Windows ZIP are stored under the user's Documents folder in
`Windows Fader Bridge Private Research/Mackie Control`, outside Git.

P1-Nano port 4 is reserved for iMAP (manual p.17). Only the matching DAW pair
1, 2 or 3 may be selected. Preserve existing iMAP presets before any user change.
Do not automatically install the package or upgrade firmware. A firmware version
check and selection of an MCU-compatible DAW mode are user hardware checks.

## Verification requirements

Protocol tests must cover signed deltas, fader limits, button release/debounce,
touch feedback exclusion, stable identities, automatic compaction, bank guards,
master switching, pending-write expiry, LCD bounds, meter overload and malformed
input. The host must build without an Avid SDK dependency and leave EUCON source
and runtime untouched. A successful MIDI open/write is not hardware verification.
Only the user can confirm physical motor direction, touch, display formatting,
LEDs and actual P1-Nano mode while away from the workstation.

## Completed software checks (2026-08-30)

- Independent Release and Debug builds without any Avid import library.
- Eight native test groups, 27,670 assertions, including 50,000 seeded malformed
  input events; no MIDI ports opened by tests.
- Opt-in real Windows integration test: its own silent, nonpersistent shared
  audio session, MCU fader -> keyed queue -> Windows volume, Windows -> motor
  packet, mute/unmute, pan and encoder-push center. All passed. Stale identity
  was rejected without touching the live session.
- Native host smoke: three active endpoint tracks, 37 MIDI input ports, 38 output
  ports; **zero MIDI sends**, automatic exit successful. Port counts are a local
  inventory, not hardcoded configuration or a device requirement.
- Existing nine .NET protocol tests still pass.
- Windows UI inspection: actual endpoint names/levels, route controls,
  explicit port selection, refusal to connect without a chosen pair, clean
  exit. Checkbox contrast was corrected after visual inspection.
- Existing EuControl / WindowsFaderBridge process identities and start times
  unchanged. An EUCON Release regression build passed using
  `artifacts/eucon-regression`, without overwriting/relaunching the live executable.
- The new executable's import table contains Windows/VC runtime libraries, no
  Avid/EUCON library. A no-SDK CI workflow was added; remote CI is not claimed
  as executed by this local verification.

The integration test uses the Microsoft contracts for
[shared-stream initialization](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-initialize)
and [session volume services](https://learn.microsoft.com/en-us/windows/win32/api/audioclient/nf-audioclient-iaudioclient-getservice).
No example source or SDK files are copied into the test.

## Explicitly not verified / not implemented

### Local touchscreen preset workflow (2026-08-30)

iMAP 1.25.3 exposes full configuration Save/Load File (`.imap`) and individual
DAW mapping Save/Load (`.p1n-daw`, added in the official iMAP 1.20 release notes).
The supplied exports have XML format marker 1.32. The official manual pp. 48-54
documents assigning touchscreen Note/Channel/Message/Name, hotkeys and saving.
The editor was used to set a sample first-page button to Note 0 / MIDI channel
16; its saved data confirms `valueType=midi`, `type=note`, `chan=16`, `value=0`.
The five visible 4-by-4 pages correspond to control IDs 101-180 in this export;
their labels/order were cross-checked against the editor and supplied Cubase map.
This is observed file serialization, **not** a published manufacturer schema or
a new MIDI protocol definition. The transformer refuses other versions/layouts.

`make-mackie-touchscreen-preset.ps1` requires a separately supplied full export,
changes only the requested Cubase slot's 80 touchscreen buttons, clears their
old keyboard actions, preserves every other slot/mode/core control, and refuses
to overwrite input or outputs. Vendor configuration content is never embedded
in the script or tests, nor committed. Its outputs remain local/ignored. The
current workstation plan uses DAW 2 for Windows and preserves DAW 3 as original
Cubase; the core codec still has no device-name or DAW-name branch.

The companion generic Windows 80 command preset is opt-in and installed only
while disconnected. It rejects conflicts atomically, backs up existing settings,
preserves ports/profiles/track identities, and never sends a command while applying.
MIDI channel 16 is distinct from P1-Nano USB DAW port 2 or maintenance port 4.
Current software tests: 9 native groups / 27,756 assertions plus 319 standalone
preset-generator checks using project-created synthetic fixtures. Editor import
and physical repeated-press/release verification are separate acceptance gates;
file validity and successful builds do not establish hardware operation.

The Release host was restarted independently and its new button installed all
80 bindings, with the previous user settings backed up. The explicitly selected
second P1-Nano input/output pair opened without errors; no ports were guessed.
The running EUCON host/EuControl process identities remained unchanged. The
computer-use helper could not activate iMAP's native Load Mapping file chooser
(`failed to activate captured window` after refreshed-window recovery), so full
editor import/round-trip and physical button acceptance remain pending user
assistance. Do not describe the generated preset as already hardware-verified.

### Playback-time verification and startup blocker (2026-08-30)

Elapsed-time code passed Release and Debug builds: 11 native groups / 239,596
assertions, plus the existing 319 preset-generator checks. Coverage includes
timestamp/rate projection, pause, backward seek, track changes, invalid bounds,
unknown rate, second/minute/hour carry, display overflow, seven-bit encoding,
change-only traffic, reconnect repaint, and motor touch exclusion. The shared
Windows-only additive read path also passed an isolated EUCON Release build in
`artifacts/eucon-regression/Release`; the live EUCON executable was not replaced.

The first Debug smoke passed with four audio tracks and zero MIDI sends. After
the old running Mackie host was exited normally, subsequent Release startup and
Debug smoke blocked inside `midiInGetNumDevs`, before opening any MIDI port or
executing time-display feedback. Startup and enumeration phase traces isolate
the blocking call. Read-only Windows Wait Chain Traversal reported the smoke
main thread waiting over ALPC on process 33416, identified by Win32_Service as
`midisrv` / Windows MIDI Service. This establishes the immediate wait target,
not its underlying root cause or a claim of a Microsoft driver defect.

The stalled test hosts were stopped, and the prior Mackie executable was backed
up locally before replacement. Restarting Windows MIDI Service requires user
authorization because other MIDI clients may be disconnected. EUCON and
EuControl processes were left running. Physical screen formatting and live
pause/seek synchronization remain pending recovery of MIDI enumeration and
user verification; successful codec tests do not establish hardware behavior.

Recovery verification: after the user restarted the service, its PID changed
from 33416 to 29792 and state returned to Running. The Release host then started
normally (PID 14304), enumerating 37 inputs / 38 outputs without blocking. The
previously selected DAW 2 pair was explicitly reconnected through the UI
(`MIDIIN2 (iCON P1-Nano)` / `MIDIOUT2 (iCON P1-Nano)`, indices 33 / 34 on this
enumeration). No other ports were opened, and no preset was reimported.

Selecting Apple Music exposed its paused timeline as 00:00:09 / 00:02:46 with
seeking disabled. A brief diagnostic capture confirmed all ten time-display CC
updates: 0x40..0x49 carried 39,30,70,30,70,30,20,20,20,20 (hex), representing
the six elapsed digits, two decimal points and four blanks. MIDI errors remained
zero. Trace logging was switched off afterward. Playback was not changed by the
test. The EUCON host and EuControl kept their original process IDs/start times.
This verifies the actual Windows read and MIDI send path, not physical rendering
or live pause/seek accuracy; the connected host is ready for that user check.

### Mackie simplification and idle-display check (2026-08-30)

At the user's request, the Mackie dashboard no longer duplicates selection,
foreground, Clear Solo, Mono and application-routing controls. Standard surface
actions and the existing assignable command catalog / Windows 80 bindings stay
intact. At this stage V-Pots were always Pan; the later current-channel encoder
workflow below supersedes that mapping. Track/Pan/Flip still cannot swap roles.
Track lifecycle is automatic and dense, not the EUCON persistent-position policy.
Selection, pending writes and feedback follow stable identities through cleanup;
any touched fader postpones topology changes until the final release. This change
is confined to the Mackie host, protocol helper, tests and documentation.

Both Debug and Release passed 11 native groups / 239,613 assertions and 319
preset-generator checks. Added regressions cover fixed Pan after assignment/Flip,
unsupported Pan without volume fallback, restored-order cleanup, bank clamping,
selected-track compaction, multiple touches, offline-write rejection, surviving
pending Pan/volume/mute/solo state, empty-list feedback and reconnect cleanup.
These tests do not open MIDI ports or alter Windows audio.

The new Release host started normally as PID 30184. UI inspection showed only
three currently online endpoint tracks, no retained application gaps, no routing
or knob-mode/cleanup buttons, and all 80 saved bindings. The explicitly selected
DAW 2 input/output pair reconnected with zero MIDI errors. EuControl (13696), the
EUCON host (26272) and MIDI Service (29792) were not restarted.

The user reported `06.06.34` on the idle numeric display. At inspection time the
old host exposed no available media state; its code would request dashes, so
that observation cannot be presented as a valid song time or proven provider
bug. Idle output now uses the existing documented blank character for all ten
positions, with no decimal flags. A brief trace verified actual CC 0x40..0x49
each carried 0x20 after reconnect. Trace was turned off afterward. Physical
clearing and live playback rendering still require user confirmation; successful
sends alone do not prove device rendering.

### Idle system-clock verification (2026-08-30)

The user-approved clock policy replaces the previous blank idle display. Debug
and Release passed 12 native groups / 239,697 assertions plus 319 preset checks.
The new pure policy tests exercise first-start clock, unavailable/read-only media,
pause at 9,999/10,000 ms, timestamp-only polling, resume, paused seek, track/player
changes, timeline loss/recovery, display overflow, midnight and wall-clock jumps.
Feedback tests verify clock traffic without a media session, change-only digits,
unchanged Transport LEDs and motor touch exclusion. No new protocol messages or
shared Windows/EUCON code were needed for this policy.

The Release host was restarted and the previously selected DAW 2 pair reconnected.
The dashboard displayed local time, matching the Windows clock. A brief MIDI
trace showed 16.21.07 encoded by CC 0x40..0x49, then only changing second digits
through 16.21.17, including the two-digit carry at 10 seconds. MIDI errors were
zero and logging was switched off afterward. EUCON/EuControl and Windows MIDI
Service were left running. No playback was started or stopped for this check;
physical display and live player pause/resume acceptance remain user checks.

### Current-channel encoders (2026-08-30)

At the user's request, the previous all-Pan strip mapping is replaced by a
generic host-side current-channel layout. Existing documented CC 16..23 and
push Note 32..39 are unchanged: encoder 1 adjusts current volume (1% per step),
encoder 2 adjusts current Pan (2% per step) / push center, and 3–8 dispatch
independent left/right/push allowlisted commands. No new vendor MIDI definitions,
iMAP imports, firmware changes or manufacturer branches were added.

Current identity follows explicit Select / UI selection or the first real
fader touch; a second simultaneous touch cannot retarget it. With touch protection
explicitly disabled, accepted fader movement can select. Bank/Channel movement
preserves the selected strip offset, clamped to an online entry on a partial page.
Offline cleanup and default-endpoint changes retain keyed gesture ownership.
P1-Nano manual p.13 describes the adjacent arrows as onboard display navigation;
the bridge does not infer host selection from an unreported local display change.
Physical arrow/Select/touch sequencing still needs the user's verification.

The new editor persists 18 independent assignments separately from existing Note
bindings. Sixteen current-channel actions supplement the unchanged 186 Windows
commands. Current-channel media explicitly disables legacy global-key fallback
so an unsupported selected player cannot accidentally control another player.
Existing Transport fallback semantics are preserved. Ordinary Windows command
scope remains foreground/global, as labeled. Empty mappings are inert; unknown
IDs, out-of-range encoders/gestures and attempts to override encoder 1/2 are
rejected. Discrete rotation executes once per packet, at most 10/sec/encoder,
dropping excess input; it does not expand acceleration into bursts. Push is
edge-triggered. Saving/learning never executes the assigned command.

Volume/Pan rings now reflect the same selected track. Unassigned/command rings
are dark rather than showing unrelated strip Pan; strip LCD names, meters,
motors and buttons retain their original banked meanings. The editor suspends
encoder execution only; touch releases and fader feedback remain live.

Debug and Release pure tests cover selection, bank offsets, touchless input,
multiple touches, offline targets, limits, distinct rings, push debounce,
throttling, strict media scope, invalid/legacy settings and command IDs.
The opt-in audio test also checks encoder 1 against its own actual silent Windows
session volume, alongside encoder 2 balance/push center and stale-key rejection.
The Windows UI was visually inspected: all 18 dropdowns fit, categorized options
are exposed, and saving the empty default mappings succeeds without MIDI writes.
No user audio, default device or playback was changed by that UI check.

Final Debug and Release runs passed 13 pure test groups / 239,804 assertions,
319 synthetic preset checks, and the opt-in own-session audio integration tests.
The final Release process (31444) was started and the previously selected DAW 2
pair reconnected (input 33 / output 34 on this enumeration); Errors remained 0.
All 80 Note bindings survived, and encoders 3–8 remained unassigned by default.
EUCON host 26272, EuControl 13696 and MIDI Service 29792 were not restarted.
Physical encoder direction, the device's local selection behavior, and actual
ring rendering remain user acceptance checks, not inferred from TX counts.

### Jog application layer — 2026-08-30 (pending physical mode capture)

Sources read before adding behavior: independently acquired Logic Control manual
pp. 82–84, 110–114, 118–123, and P1-Nano English manual p. 10. Standard Jog remains
channel-1 CC 0x3C signed magnitude. Scrub switch 0x65 supplies the optional Jog-push
application action. Cursor 0x60–0x63 and User A/B 0x66–0x67 are switch-only, unlike
Zoom 0x64 / Scrub 0x65; no LED packets are emitted to switch-only addresses even
when a custom application command is assigned there. No guessed six-key P1-Nano
message map or vendor SysEx has been added. The P1 manual describes the six local
functions but does not specify their complete Cubase MIDI/HID sequences.

The generic application layer has seven mutually exclusive Jog modes, available
from its settings window or eight allowlisted assignable commands (seven modes
plus Jog push). Mode-key repeat returns to playback; reconnect resets the active
mode without losing persistent speed / command mappings. Standard Jog input is
independent of device names. Note-binding LEDs reflect current mode, not physical
window response. Existing user Note mappings retain precedence over core buttons.

Navi selects adjacent stable online identities, reveals their bank, respects touch
protection, and never activates windows on rotation. Push requests selected-app
activation. Selection/rings follow in the logical model; P1-Nano's internally
selected virtual fader is not assumed to follow a host Select LED. That distinction
requires physical verification and cannot justify remapping generic MCU faders.

Playback Jog uses seconds (default one) rather than whole-song percentages.
The opt-in Windows timeline query additionally returns MinSeek/MaxSeek bounds;
EUCON's default GetState and Execute semantics are unchanged. Conversion requires
seek permission and valid bounds, projects a running timestamp when a rate is
known, and clamps to the seek interval rather than confusing it with song length.
Queued seconds requests for the same player can coalesce. Read-only timelines
remain display-only; no blind keyboard seeking fallback is added.

Move uses Windows vertical/horizontal wheel inputs. Focus ignores accelerated
magnitude and accumulates physical packets into full wheel notches (default four).
General Zoom sends foreground Ctrl +/-; custom Zoom has independent left/right
allowlisted commands, empty by default, not automatic DAW shortcut discovery.
Zoom is throttled to 10 actions/sec; navigation to one channel per 75 ms. Modifier
handling preserves pre-held Ctrl and cleans up keys on partial insertion. Custom
commands expire after 150 ms or foreground change instead of building a backlog.
Windows wheel routing and per-app support still apply. No drag emulation, elevation,
window focus stealing for scroll, or fabricated success detection is used.

Windows sources: [SendInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput)
and [MOUSEINPUT](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-mouseinput).
SendInput acceptance is not evidence of application response; UIPI may block
higher-integrity windows. Pure tests inspect INPUT packets without injecting them.

Initial Debug validation: 16 groups / 240,738 assertions and 319 synthetic preset
checks passed. A no-MIDI smoke run passed with three online tracks / zero sent
messages. An EUCON Release regression build succeeded in a separate output tree;
the running EUCON executable/process was not overwritten or restarted.

Pending: physical capture of ordinary Jog/push, Navi, Move vertical/horizontal,
Focus, and Zoom vertical/horizontal on the explicitly selected DAW-2 port pair;
reconcile device-local vs host actions before assigning native mode controls.
At that stage the old Release capture process remained running. The subsequent
capture and integration below supersede the pending Move/Zoom/Navi input check;
do not report all six native mode keys as implemented.

#### Native Jog capture and integration — 2026-08-30

The user exercised the controller on the existing, explicitly selected DAW-2
pair (MIDIIN2 / MIDIOUT2). The raw log is retained outside Git in private research.
Observed: ordinary Jog CC 0x3C; Cursor 0x60..0x63 with down/zero-velocity release;
Zoom 0x64 down/release; SELECT 0x18..0x1F often down-only, plus Bank 0x2E/0x2F
with releases. No Scrub 0x65 or separate identifiable Focus packet appeared.
Absence on this pair does not prove HID behavior, another port, or a device bug.
The capture was reconciled with the manufacturer manual pp. 82–84, 110–114,
120–122 and P1-Nano manual p. 10. No novel message IDs were invented.

Cursor now uses a separate application Zoom boolean: off means vertical/horizontal
scroll, on means general zoom for Up/Down and custom zoom for Left/Right. Each
axis uses the configured sensitivity; Zoom 0x64 has standard state feedback.
Ordinary CC Jog assignments remain independent; returning a controller to CC Jog
does not require inferring its previous device-local mode. UI labels show both
the ordinary Jog role and Cursor zoom/scroll state. Native physical LED rendering
and application response still require the user's acceptance test.

SELECT is now handled as an absolute identity before toggle debounce, so revisiting
an earlier strip in a down-only navigation stream is not lost. Selection occurs
on down without focus. A matching release of the current pressed identity requests
window focus once; stale releases after banking, offline replacement, another
selection, or touch do not activate the wrong app. No timing heuristic or
manufacturer-name branch is used to guess whether SELECT came from a Jog wheel.
This changes physical Select window activation from down to matching release.
The pure replay-style tests include down-only revisits, bank crossings, stale
release, touch protection, Cursor direction/axis, Zoom toggle/LED and reconnect.

Debug verification after native integration: 17 groups / 240,754 assertions plus
319 synthetic preset checks. Tests emit no global input and open no MIDI ports.
Focus and physical Jog push remain unresolved rather than silently assigned.

The settings window was visually inspected and its default configuration saved
successfully without MIDI connected: all controls fit, the 80 existing Note
bindings survived, and custom horizontal Zoom remained unassigned. During editing,
rotation is suppressed but the native Zoom switch and its held-key state continue
to be tracked, preventing a local mode change from desynchronizing the host.
Final Release tests: 17 groups / 240,755 assertions and 319 preset checks passed.
The final Debug run passed the same checks. Release host 9452 was started and
reconnected to the already selected input 33 / output 34 pair; Errors remained 0.
Detailed MIDI tracing is off after capture. EuControl 13696 and EUCON host 26272
retained their original start times; no iMAP preset, driver or firmware changed.
Actual scrolling, zoom application support, native LEDs and navigation/fader
agreement remain user acceptance checks; a successful MIDI connection is not
proof of those outcomes.

#### Jog desktop navigation and current media target — 2026-08-30

This update supersedes the earlier host `JogNavigate` channel-navigation policy;
the native SELECT/Bank interpretation is unchanged. User reports Focus behaves
like vertical scrolling, requests slower desktop navigation and reports no
visible action for ordinary CC Jog. The subsequent trace contains Scrub 0x65
down/release, resolving only the earlier absence of a physical push capture.

`JogNavigate` now emits previous/next existing virtual desktop shortcuts through
the ordinary CC Jog path. Four signed packets trigger one step, ignoring MIDI
acceleration; a 750 ms cooldown discards intervening input without a backlog.
Reversal and idle reset fractional motion. INPUT batches balance Ctrl/Win and
extended arrow events, reject held modifiers, and release only injected keys on
partial insertion. No actual desktop switching is performed by automated tests.
Reference: [Microsoft Windows keyboard shortcuts](https://support.microsoft.com/en-us/windows/keyboard-shortcuts-in-windows-dcc61a57-8ff0-cffe-9796-cb9706c75eec).

Physical Navi in the observed Cubase configuration shares SELECT/Bank messages
with ordinary controls and has no distinct mode notification. Automatically
repurposing those messages would also change Sel/banking. The safe host mode is
available in Jog settings or through an assignable key; the user has been asked
to choose its hardware entry. No iMAP settings or hardware bindings were changed.

Ordinary Jog was reaching `SeekSeconds` with the selected render-endpoint key,
but the media worker's empty application target matched no GSMTC session.
Endpoint/no-selection transport now opts into Windows' current media session;
explicit application targets stay application-specific. The existing EUCON
GetState/Execute entry points keep their default strict matching semantics.
An opaque shared session lease binds a Mackie request to the same session which
supplied its capabilities and seek range, avoiding a second lookup into another
player. Unavailable/read-only seek failures are visible even when no media state
is readable, and request diagnostics distinguish queue acceptance from dispatch.
Dispatch still is not proof that the player completed an asynchronous request.
Reference: [Microsoft GetCurrentSession](https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssessionmanager.getcurrentsession).

Debug and Release pure tests: 17 groups / 240,840 assertions, plus 319 preset checks; smoke
passed with three endpoints and zero MIDI output. Isolated-output EUCON Release
regression build passed without replacing or restarting the live EUCON process.
Release UI was inspected and restarted on the existing explicitly selected
input 33 / output 34 pair. Existing 80 touch bindings and Jog speed settings were
preserved; ordinary Jog remains in playback mode. No iMAP edits were made.
Physical desktop switching and seek acceptance remain pending user verification.

#### Four independent custom Cursor axes — 2026-08-30

The latest user decision supersedes ALL earlier software Jog-mode policies:
ordinary CC 0x3C is playback only; native Navi and Focus have no host-mode
override or custom assignment. SELECT/Bank retain their existing native channel
semantics. Scrub has no invented app-focus action. Eight retired Jog mode command
IDs are no longer accepted by the catalog, button loader or encoder assignments.

Re-read manufacturer Logic Control pp. 82–84, 110–114 and P1-Nano p. 10. No MIDI
definitions were added: Cursor 0x60..0x63 and Zoom state 0x64 produce four logical
axes (Move vertical/horizontal, Zoom vertical/horizontal), each with independent
negative (up/left) and positive (down/right) commands. User settings choose from
eight project-owned wheel/direction actions or the existing Windows allowlist.
Defaults are unassigned. Sensitivity is independent per axis, 1/2/4/8 events;
dispatch is bounded to 10/sec with no backlog. The ordinary Jog seek path and
media target/capability handling are unchanged.

Raw Cursor/Zoom input cannot be intercepted by generic Note bindings, including
during learning. Legacy single Cursor bindings migrate only to corresponding
Move directions; legacy custom horizontal Zoom mappings retain their own axes.
Explicit new mappings, including empty entries, win regardless of file order.
Other MIDI channels and the 80-key preset retain their original assignments.
Standard switch-only directions receive no fictitious LEDs.

While examining the reported regression, the live older Jog settings window
was open; that version deliberately suppressed rotations while editing. This
is a confirmed reason actions do not run with that window open, not proof of
the entire reported hardware failure. The new settings window shows received
axis/direction without executing commands and explicitly says to close it before
testing. Runtime trace distinguishes raw switch reception, axis/command resolution
and dispatch outcome. Zoom tracking continues during editing and learning.
The settings UI allows an explicit Move/Zoom state calibration after reconnect;
it does not infer the device's private mode or change iMAP. Normal direction
actions show an unassigned message instead of silently doing nothing.

Debug and Release tests: 17 groups / 239,949 assertions and 319 preset checks passed. Coverage
includes all eight directions, release handling, per-axis sensitivity, native
selection/touch behavior, ordinary Jog independence, removed mode IDs, invalid
records, legacy migration/explicit empties, and pure wheel/key INPUT construction.
Smoke passed with three online endpoints and zero MIDI output. Settings were
backed up locally before replacing the running Mackie build. No vendor sources
or generated private presets were added to Git. End-to-end hardware execution
of user-selected direction commands still requires user acceptance.
The four-row/eight-command settings UI was visually verified and saved, retaining
all 80 user touch bindings, playback speed and port names. Release reconnected
on the user's existing input 33 / output 34 pair, with Errors 0. EuControl and
the running EUCON host retained their original processes and start times.

#### Ordinary Jog left/right assignments — 2026-08-30

The user's clarification supersedes the playback-only policy above: ordinary
CC 0x3C now has two independent allowlisted bindings. Defaults remain seek back
and seek forward with the existing seconds-per-tick setting. Either direction
can instead execute a Windows/wheel/direction command or remain unassigned.
Seek bindings are valid only for this fifth logical axis, never as native
Cursor commands. No MIDI message definitions or manufacturer modes were changed;
Navi/Focus still have no custom assignments or host-mode override.

Discrete Jog commands use their own 1/2/4/8-event sensitivity and the same bounded
10/sec dispatch policy; accelerated delta magnitudes do not multiply a command.
Playback keeps its existing bounded delta-to-seconds behavior. Settings preserve
explicit empties and reversed seek directions; old four-axis files default the
new Jog row to playback. Editing detects raw Jog direction without executing it.

Debug and Release tests passed: 17 groups / 239,959 assertions plus 319 preset checks, with
no MIDI ports opened, audio changes or injected global input. Added coverage
includes fifth-axis roundtrip, legacy defaults, invalid records, explicit empty
bindings, mixed seek/custom directions, reversed seek, reconnect and independence
from the current Move/Zoom layer. Physical custom-command acceptance remains
pending user verification.

Release smoke passed with four online tracks and zero MIDI output. The five-row
settings window was visually checked and saved with ordinary Jog's default
seek bindings, retaining all 80 touch bindings and existing Move/Zoom choices.
The new Release process reconnected to the user's selected input 33 / output 34
pair with Errors 0. EUCON and EuControl were not restarted or replaced.

### Remaining hardware/protocol limitations

- Physical P1-Nano Jog/Move/Zoom/Select input was captured as described above;
  end-to-end acceptance of the new behavior and cross-device coverage remain
  pending. Do not describe the program as hardware-certified or zero-latency.
- One MCU MIDI pair, eight logical strip positions plus master; MCU extenders,
  multiple main units, HUI and older Logic Control model IDs are not implemented.
  More than eight Windows tracks are supported by banking, independent of this.
- Manufacturer-specific handshakes, LCD meter-mode initialization, RGB colors,
  iCON D5/secondary display protocols and iMAP touchscreen labels are not sent.
  A device requiring these may have partial/no feedback despite valid MIDI I/O.
  Meter packets alone are not proof that a particular display enables metering.
- MCU LCD is ASCII, six readable characters plus spacing per strip; the Windows
  UI keeps full Unicode names. Lower line currently displays volume, not a DAW
  song/timecode display. Media title/artist/position are shown in the Windows UI.
- Pan is Windows stereo balance, not a DAW equal-power panner. Standard MCU peak
  feedback is quantized from -60..0 dBFS into 0..12, max across Windows channels;
  it is not calibrated multi-channel or intersample true-peak metering.
- Automatic startup/reconnect, installer, persistent custom device profiles,
  arbitrary multi-surface assignments and drag/reorder UI are later product work.
- EUCON and Mackie can read the same Windows state, but their current solo policy
  engines are separate. Use solo from only one running edition at a time until
  a shared cross-process mixer service owns that policy.
- Global Windows media-key fallback cannot guarantee targeting a specific app.
  Capability-aware selected-app transport is used when GSMTC is available;
  unsupported actions do not fabricate a successful LED state.
