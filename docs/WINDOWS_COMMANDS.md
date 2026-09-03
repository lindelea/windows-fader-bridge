# Assignable Windows commands

Chinese users can refer to [`WINDOWS_COMMANDS_ZH.md`](WINDOWS_COMMANDS_ZH.md)
for translated names, short explanations, and suggested surface assignments.

Windows Fader Bridge publishes Windows actions through the standard EUCON
command-processor hierarchy. EuControl owns placement, pages, labels, colors,
and the physical keys used to invoke them.

```text
Key Commands
  -> category
     -> command
```

The catalog contains 189 commands in 15 categories. The original two Windows
Audio commands retain their existing persistence IDs; the remaining commands use
globally unique IDs under `FaderBridge.Windows.Commands.*.v1`.

## Categories

### Windows Audio

Mono Audio; Clear Solo.

These are stateful Windows audio commands. Their LEDs are driven by the current
Windows state rather than by the physical press.

### System Tools

Task Manager; Windows Terminal; PowerShell; Command Prompt; Run; File Explorer;
Control Panel; System Information; Computer Management; Device Manager; Disk
Management; Services; Event Viewer; Registry Editor; Resource Monitor;
Calculator; Notepad; Paint; Character Map; Snipping Tool; Power User Menu;
Quick Assist; Sound Output Panel; Calendar and Clock; System About.

### Settings

Settings; Windows Update; Sound Settings; Volume Mixer; Display Settings;
Network Settings; Bluetooth; Installed Apps; Startup Apps; Default Apps;
Storage Settings; Power Settings; Date and Time; Clipboard Settings; Microphone
Privacy; Camera Privacy; Accessibility; Printers; Windows Security.

Settings pages use Microsoft's documented `ms-settings:` URI scheme. Page
availability may vary by Windows version, SKU, policy, and installed hardware.

### Folders

Home Folder; Desktop; Documents; Downloads; Music; Pictures; Videos; This PC;
Quick Access; Network; Recycle Bin; Roaming AppData; Local AppData; Temp Folder;
Startup Folder.

Physical folders use `SHGetKnownFolderPath`; no username, drive letter, or
localized directory name is embedded in production code.

### File Explorer

Extra Large Icons; Large Icons; Medium Icons; Small Icons; List; Details;
Tiles; Content; New Folder; Properties; Toggle Preview Pane; Toggle Details
Pane; Previous Folder; Next Folder; Parent Folder; Search Folder; Fit Columns
to Content.

These use Windows' native `Ctrl+Shift+1..8` File Explorer view commands and
are sent only while an `explorer.exe` window owns the foreground. A useful
surface assignment is `View1 = Large Icons` and `View2 = Details`; EuControl
still owns the actual key assignment.

### Window Management

Start Menu; Search; Quick Settings; Notifications; Show Desktop; Minimize All;
Restore Minimized; Task View; Next Window; Previous Window; Minimize Window;
Maximize or Restore; Close Window; Toggle Always on Top; Snap Left; Snap Right;
Snap Up; Snap Down; Move to Next Monitor; Move to Previous Monitor; Project
Display; Cast; Lock Computer; Snap Layouts; Snap Top Half; Snap Bottom Half;
Isolate Current Window; Peek at Desktop; Window Menu.

Foreground-window commands operate on the window that is active when the
command reaches Windows. Close Window follows normal `WM_CLOSE` behavior, so an
application can still show its own unsaved-work confirmation.

### Virtual Desktops

New Desktop; Close Desktop; Next Desktop; Previous Desktop.

### Taskbar

Pinned App 1 through Pinned App 10; Next Taskbar App; Previous Taskbar App;
Notification Area.

Pinned App commands follow the user's current Windows taskbar order. They open
the corresponding pinned application or switch to it if it is already running,
so they require no application-specific configuration in Fader Bridge.

### Editing

Undo; Redo; Cut; Copy; Paste; Select All; Save; Save As; Open; Find; Print;
Rename; Escape; Enter; Delete; Backspace; Next Field; Previous Field; Context
Menu.

### Browser and Tabs

New Tab; Close Tab; Reopen Closed Tab; Next Tab; Previous Tab; Address Bar;
Back; Forward; Refresh; New Window; Full Screen; Zoom In; Zoom Out; Reset Zoom;
Page Top; Page Bottom; Page Up; Page Down.

These are conventional keyboard commands. The foreground application decides
whether it supports them; Fader Bridge does not claim application-specific
capability or steal focus before sending them.

### Media

Play or Pause; Next Track; Previous Track; Stop; System Mute; System Volume Up;
System Volume Down.

These use Windows media and volume virtual keys and therefore follow the same
system media-routing policy as a multimedia keyboard.

### Capture and Input

Screen Snip; Save Full Screenshot; Copy Active Window; Game Bar; Screen
Recording; Voice Typing; Emoji Panel; Clipboard History.

### Input and Language

Next Input Language; Previous Input Language; Previous Input Method.

### Accessibility

On-Screen Keyboard; Open Magnifier; Close Magnifier; Magnifier Zoom In;
Magnifier Zoom Out; Toggle Narrator; Toggle Color Filters.

### EUCON Applications

Windows EUCON; UAD EUCON; Mackie Control.

These commands summon an already-running bridge through a private Windows
message. They do not synthesize the configured keyboard shortcut and do not
launch a stopped application. The target bridge owns its foreground/background
policy. This keeps the command useful when the user records a different global
shortcut later.

## Threading and execution

`WindowsCommandProcessor::OnPrimitiveCallback` runs on a EUCON-owned thread.
It only identifies the category/member pair and posts a typed command message
to the native application window. `WindowsCommandExecutor` runs the action on
the Win32 owner thread. This keeps shell activation, foreground-window calls,
and keyboard injection out of the SDK callback and avoids locks shared with
EUCON API calls.

Stateless commands are one-shot switches with standard momentary LED behavior.
Construction and destruction follow the official order: initialize the
container, add it to the processor, initialize and push its switches, then
remove switches in reverse order before removing the container.

## Safety boundary

The built-in catalog does not contain shutdown, reboot, sign-out, forced
process termination, file deletion, disk formatting, service changes, driver
changes, or commands that bypass Windows consent. Administrative tools may be
opened, but Windows remains responsible for UAC and any subsequent mutation.

## Verification status

Local verification on 2026-08-29 covered:

- Release build and process startup;
- zero EUCON command-initialization error returns in diagnostics;
- complete one-to-one coverage of all 184 command IDs in both the published
  catalog and Windows executor;
- successful Settings URI, system executable, Known Folder, and `SendInput`
  dispatch without blocking the bridge.

Physical verification is still required in the EuControl Soft Keys editor on
both S3 and Avid Control: category visibility, saved assignments, labels,
momentary LEDs, and representative invocation from each category.
