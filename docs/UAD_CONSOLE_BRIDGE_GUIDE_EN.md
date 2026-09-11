# UAD Console Bridge for EUCON

[简体中文](UAD_CONSOLE_BRIDGE_GUIDE_ZH.md)

Current application version: **v1.0.0**.

A control bridge between UAD Console and EUCON. Apollo and Console continue to
process audio; the bridge does not replace Console or manage EUCON hardware.
Hardware compatibility follows validation with the actual Apollo, S3 and Avid
Control environment. When first using configuration, routing, plug-in loading,
phantom power or talkback, confirm the device response outside a critical session.

## Overview

The main window is status-only: interface connection, sample rate, clock source,
monitor level/source, mute/dim/mono, per-channel status and category counts. Missing
data is shown as an em dash. The main monitor is never a channel fader.

The scrollable channel list shows name and interface, mono/stereo format, fader
level, pan, signal and peak, mute/solo, UAD REC/MON and output. Stereo channels
retain both independent pan positions. Signal and peak retain native dBFS values.
M and S indicate mute and solo, highlighted red and yellow when active. REC means
recording the processed signal; MON means plug-ins affect monitoring only.
Unsupported fields show an em dash; stale or disconnected readings are cleared.
Category colors remain visible beside each row. Use the mouse wheel, arrow keys
or Page Up / Page Down to browse. Highlighting a row never selects a surface
channel or changes audio. The list has no physical-surface channel-count limit.

## Settings

**General** provides English/Simplified Chinese, background operation, hidden
startup, Windows sign-in launch, and access to the log folder. Closing the
window keeps the application in the tray by default. Use the tray menu to restore
the window or quit. Login startup is off by default and applies to the current
Windows user only. Save the option again if the executable location changes.

**Connection** provides automatic EUCON connection, manual connection/retry and
control suspension. CONFIG is a production feature and is enabled by default;
changing its setting requires restarting this application.
Console, EuControl, drivers and hardware are never restarted by these settings.

**Permissions** offers Read only, Mixing, Full control and Custom profiles:

- Channel control: current eligible online channels, including sends, routing,
  preamps and loaded plug-ins.
- Control room: monitor level, mute, dim, mono, source and talkback.
- Sensitive controls: phantom power, UNISON and talkback to monitors; requires
  channel access.
- Interface and plug-in configuration: supported CONFIG settings, plug-in
  selection and existing preset recall. Turn to preview; press In to apply.

Full control covers implemented, allowlisted operations, not arbitrary device
commands. Apply and confirm permissions explicitly. Granting access sends no audio
command. Session access remains active until the user switches to read-only or
disconnects the bridge. Brief disconnects, model refreshes and individual operation
failures discard stale work and automatically rebind to fresh state without changing
the selected access profile. No old command is replayed.

**Protection** provides a monitor level ceiling from −96 to **0 dB**, initially
−20 dB. Saving it does not change the current level. A live level above the ceiling
does not block control; the next bridge level target is clamped to the ceiling.
Mute, dim, mono, source and talkback remain available. This ceiling limits only
bridge level commands, not Console/hardware control or acoustic SPL. Removing
mute/dim, changing source or analog reference level can still increase loudness.

Restore saved permissions at startup is opt-in. Restoration occurs after startup
when the previously confirmed interface system matches. Apply permissions once to
a connected system to record that identity. Switching to read-only stops subsequent
bridge control without changing the current interface state.

Ordinary mixing controls validate the latest device/channel identity, control type
and range, then dispatch immediately. EUCON events wake the owner thread without
waiting for the desktop refresh timer. Continuous gestures dispatch without a fixed
rate gate and coalesce unsent intermediate positions; Console's live state feed performs final
reconciliation. Plug-in load/unload, preset recall and CONFIG settings retain full
preflight and post-write confirmation. A failed operation does not revoke the
selected permission, and an uncertain write is never automatically retried.
Revoking access cannot undo a command already sent. After connection loss, use
Console or hardware to close talkback if necessary.

## Files and validation

Settings: `%LOCALAPPDATA%\UAD Console Bridge\EUCON\settings.json`; logs are in
the adjacent `logs` folder. Earlier Apollo Bridge logs are retained in their old
location. Settings use strict versioned JSON and atomic replacement. Invalid
settings fall back to read-only defaults without overwriting the original.

Build: `scripts/build-apollo.ps1 -TransportTests`.
Executable: `artifacts/apollo-eucon/Release/ApolloBridge.Eucon.exe`.
The internal executable name and EUCON persistence ID are retained for continuity;
the public product name, application icon and file properties use the new name.

`--ui-preview --preview-connected` uses labelled synthetic data and does not connect
to hardware, initialize EUCON, persist settings or change Windows startup.
`--desktop-self-test` checks isolated settings files and startup-command quoting
without writing to the real registry. `--diagnostics` opens the private legacy
dashboard. No diagnostics button is exposed in the public interface.

Hardware acceptance must include S3 and Avid Control, both read-only feedback and
explicitly authorized low-level control, monitor ceiling, suspension, tray restore
and shutdown. Test phantom power, talkback, routing and plug-in loading only under
operator-supervised safe conditions. The UA interface is private and must be
revalidated after relevant UA software updates.
