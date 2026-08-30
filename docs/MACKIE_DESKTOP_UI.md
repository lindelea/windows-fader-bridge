# Mackie desktop workspace

## Product structure

The native desktop shell is a presentation layer over the existing Mackie
application. One Windows audio engine serves independently owned MIDI transports
and surface models. The diagnostic dashboard remains a developer-only startup
option (`--diagnostics`), with no button in the everyday UI. Advanced retains
log-folder access. No EUCON entry points or runtime behavior are changed here.

- **Overview:** online logical tracks, current selection, volume, balance,
  peak level, default/mute/solo states, bank navigation and media information.
  Detailed RX/TX counters and development commentary stay in Diagnostics.
- **Settings:** General, Device, Button mapping, Encoder mapping,
  Jog & directions, Advanced and About.
- **Language:** Simplified Chinese and English, applied immediately and persisted.
  Windows device/application names are preserved as supplied by Windows.
  The original diagnostic dashboard retains its established diagnostic wording.
- **Lifecycle:** close-to-tray is configurable. Explicit opt-in startup uses
  the current-user Run value `WindowsFaderBridge.Mackie`, independent of any
  other edition. Startup uses `--background`; per-device automatic connection
  is on by default, but only reconnects an explicitly saved, uniquely named pair.
  A second launch restores the everyday window. Quit remains in the tray menu.

## Assignment workflow

Choose an existing control or detect one physical button. Detection does not
execute the bound command or overwrite its assignment. Browse a category or
search with Chinese/English words, inspect the description, then assign.
Replacing an existing command requires confirmation. Empty/unassigned state
is explicit. Windows commands remain allowlisted; this is not a shell editor.

The same command browser supports buttons, all 18 encoder gestures, and all
10 Jog/Move/Zoom directions. Unsupported command types are excluded per target.
Names/descriptions use the project's own Chinese manual; identifiers never
change with the UI language. Search supports multiple words and either language.

While an assignment page is visible, custom buttons and encoder/Jog gestures
are previewed, not executed. Core touch releases, native Select/banking and Zoom
state still reach the original model. Preview applies only to the selected
device; other connected devices keep working. Return to Overview (or another settings
page) to test actual commands. Navi/Focus do not acquire custom assignments.
Settings writes are atomic; a failed assignment save leaves the in-memory
mapping unchanged. No iMAP settings are rewritten.

## Visual language

Graphite surfaces, restrained amber selection/action states, green connection
and metering, clear type hierarchy, generous spacing and fixed navigation.
The window is resizable, with scrollable lists instead of a fixed channel limit.
Native buttons, edits, combos and list views keep keyboard/accessibility support;
owner drawing provides the visual treatment. No browser runtime is required.
Dropdowns and popup items use the dark palette. Boolean settings are native
checkboxes rendered as switches. Assignment actions have 20 px card insets;
disabled primary actions use a darker fill with legible text. Windows command
descriptions remain visible; slogans and redundant developer prose are removed.

## Multiple devices and connection policy

Devices → **＋ Add device** creates an unconfigured device with automatic
connection enabled. Select its profile and exact input/output pair, then save.
Up to 16 independent main units are supported; this is not MCU Extender chaining.
The device picker on Overview and assignment pages chooses the editing target,
not the only active device. Each device owns its MIDI handles, bank/selection,
touch state, feedback caches, clock, bindings and reconnect policy. Audio state
and the command worker remain shared. Any surface touch defers channel topology
compaction on the others until all touches have ended.

Automatic mode retries only a previously saved, uniquely resolvable pair.
Missing/duplicate names, reserved maintenance/EuMidi ports, mismatched P1 DAW
pairs, and ports already assigned to another device are refused. Enumeration
never opens a handle. Unplug/error triggers a retry; manual Disconnect pauses
automatic retry for that device until Connect, re-enabling automatic mode, saving
its configuration, or restarting. Switching automatic mode off does not interrupt
an existing connection. Manual mode requires Connect after startup or unplug.

The legacy `settings.txt` remains the primary device file. `workspace.txt` stores
language, close-to-tray and device IDs; additional configurations live under
`devices/<id>.txt`. Removing a device unregisters it and closes only its handles;
its file is retained for recovery. Failed saves do not replace active settings.
Open settings folder resolves a Shell directory object after ensuring the
directory exists, including the packaged-host redirected AppData case.

## Icon provenance

The Mackie-only artwork is project-generated, not a manufacturer logo.
Source: `src/FaderBridge.MackieHost/assets/mackie-icon-source.png`.
Windows resource: `src/FaderBridge.MackieHost/assets/WindowsFaderBridge.Mackie.ico`.
Package with `scripts/build-mackie-icon.ps1` (16/24/32/48/64/128/256 px).

Created with the built-in image generation tool. Final prompt direction:
“Professional Windows audio utility icon; three precision silver faders on a
deep graphite tile, restrained warm amber tracks, bold legible geometry,
no text or third-party logos. Fill all corners with charcoal, no checkerboard,
fewer scale ticks for small taskbar sizes.”

## Verification

Automated coverage includes all 186 localized command entries/categories,
bilingual multi-word search, unchanged command identity, English/Chinese settings
roundtrip and rejection of invalid preferences. The existing MCU and preset
tests still run without MIDI/audio mutations. Visual and functional acceptance
on the user's hardware remains separate from these pure tests.

### Desktop acceptance — 2026-08-30

- Release and Debug builds passed: 17 groups / 240,707 assertions, plus 319
  preset-generator checks. The Debug smoke check passed with five online tracks
  and zero MIDI messages sent; it did not open a MIDI port.
- Checked Chinese and English navigation, immediate language switching and
  language persistence after restarting the Mackie application. Left the running
  workspace in Chinese.
- Checked live Overview, General, Device, button/encoder/Jog mapping, Advanced
  and About pages. Command search accepts Chinese; the selected existing command
  is scrolled into view with its description. Reapplying the same assignment
  exercised persistence without executing its Windows command.
- Compared settings with the pre-UI backup: all 80 button mappings, encoder/Jog
  assignments, directional sensitivities and selected ports/capabilities were
  preserved. Startup remained off.
- Reconnected only the previously selected P1-Nano DAW MIDI pair. Hid the
  workspace to the tray and launched the same executable again: the original
  process and connection were retained, and the everyday window reopened.
- Confirmed the original diagnostic dashboard is still reachable. Checked
  default window layout and a reduced-height window. The separately running
  EUCON application and EuControl processes were not restarted or replaced.

Physical button detection and newly changed assignments still need the user's
hands-on acceptance. Other display scaling factors and minimum-size layouts
have not yet received visual acceptance testing.

### Refinement and multi-device acceptance — 2026-08-30

- Release and Debug builds passed: 18 groups / 240,731 assertions plus 319
  preset-generator checks. Release smoke loaded a two-device workspace and
  passed with five tracks and zero MIDI sends across all contexts.
- Inspected Chinese and English General, Devices, button and Jog pages;
  expanded dark dropdowns, on/off switches, disabled primary-button contrast,
  command descriptions and card insets. The final Release device-name field
  is visibly outlined. Advanced contains log/settings-folder access, no debug
  dashboard action. The settings-folder button successfully opened the actual
  redirected AppData directory in Explorer without the old missing-path dialog.
- Added an unconfigured device through the plus button, changed only its auto
  setting, confirmed its mappings were empty, then switched back to the primary
  device with its existing mappings and active connection intact. No additional
  MIDI pair was opened. Removed only the generated test entry from the manifest
  after exiting; its separate settings file remains recoverable.
- Restored Chinese and close-to-tray, then launched the Release build. It
  automatically reconnected the previously selected P1-Nano DAW2 pair.
  Manual disconnect stayed disconnected while automatic mode remained enabled.
  The original 80 button mappings, encoder/Jog settings and port/profile options
  matched the pre-change backup; only autoConnect/deviceName fields were added.
- EUCON and EuControl retained their original process IDs and start times.
  Simultaneous operation of two physical controllers, unplug/replug recovery,
  and other DPI/minimum-window configurations still need hardware/visual checks.
