# Desktop stability follow-up — 2026-10-24

## Evidence and limits

The local UAD 1.1.0 user-mode dump from October 22 points at
`App::Tick -> SdkExampleAdapterRunning -> CreateToolhelp32Snapshot ->
kernel32!ThpAllocateSnapshotSection`, an invalid-pointer read. Production called
this developer-example guard once per second, including when already connected.
The arguments and handle cleanup were valid; this does not establish why the
OS API encountered invalid data. Remove the unnecessary steady-state workload
and the application's Toolhelp dependency, not claim an OS defect or blue-screen fix.

A separate Windows EUCON 1.0.3 user-mode dump from October 24 points at
`WindowsMediaController::GetState -> MinSeekTime` on a null timeline projection.
The shared backend also dereferenced other nullable playback objects unchecked.

System events show a `0x101` CPU watchdog on October 21, `0x4E` memory-list
corruption on October 23/24, and `0x1A/0x41790` on October 24. These are not
identical failures. Original kernel dump reads were denied by their existing
permissions; their user-mode callers have not been independently verified here.
No kernel dump, proprietary SDK material, or personal crash log is added to Git.

## Small-change design

- Keep the C++/Win32 implementation, all audio dispatch and EUCON models intact.
- Do not enumerate processes at all when UAD's adapter is connected. Cache
  disconnected checks for five seconds, but force a fresh check on explicit
  connection. Retain automatic retry when a conflicting example exits.
- Replace Toolhelp snapshots with bounded `K32EnumProcesses` and read-only
  limited-information executable-name queries. Enumeration failure blocks the
  connection conservatively; exited/protected processes cannot be inspected.
  This preflight does not guarantee identification of an inaccessible executable.
- Read absent media projections as unavailable properties, not pointers to
  dereference. Preserve the player's other controls and metadata when only
  its timeline is absent/fails. Reject seeks without a usable timeline.
- Keep the default EUCON vs opt-in Mackie timeline semantics unchanged.

Alternative: only slow the existing Toolhelp polling. Rejected because it
retains the exact snapshot API from the user-mode failure and still scans
unnecessarily after connection. Disabling all conflict checking was rejected
because official example adapters must still be kept separate.

## Verification checklist

- [x] Regressions fail on legacy logic: connected adapter still queries, and
  absent timeline is dereferenced (fixture raises instead of crashing Windows).
- [x] Probe tests: no connected scans; bounded disconnected retry; manual
  refresh; conflict cleared; real process-name lookup without SDK examples.
- [x] Media tests: null timeline/playback/controls, valid values, default vs
  opt-in behavior, zero range and clamping. No player commands are executed.
- [x] Release and Debug pure suites plus isolated transport tests.
- [x] Build all three candidate executables outside live self-use paths.
- [x] Review diff and preserve unrelated ConfigLayout/ConfigKnob/design work.

## Final local verification

- Apollo Release/Debug: 15,377 checks each, including actual self-process-name
  lookup without launching SDK examples. Isolated loopback transport suites pass.
- Mackie Release/Debug: 20 groups / 240,867 assertions and 319 preset checks each.
  New tests include actual null WinRT projections without activating a player,
  in addition to nullable value fixtures, optional-field/rate failures and
  preservation of valid session ranking. The test target now links the existing
  Windows SDK's WindowsApp.lib; no Avid SDK dependency was added to that target.
- Regression tests first failed for connected scanning and null timeline access.
  Review found two capability-preservation gaps: null playback discarded matched
  sessions, and optional display errors removed seeking. Corrected both; the
  optional-error regression was observed failing before its correction.
- Independent read-only review accepted the revised production logic; physical
  hardware behavior and blue-screen causation were explicitly not assessed.
- Isolated Release builds: UAD `1.1.1-test.1`, Windows EUCON `1.0.4-test.1`,
  Mackie `1.0.3-test.1`. No SDK-linked UAD/Windows candidate was launched to avoid
  duplicate process identity/log side effects while the production apps run.
- Existing live bridge PIDs (UAD 19972, Windows 18408, Mackie 25356), EuControl
  25264 and MC_Client 25108 were not restarted or replaced. These fixes are
  built candidates, not an installed update or a published release.

This is a candidate stability improvement, not proof that a kernel corruption
or the original blue screens are repaired. Do not modify drivers, runtime,
startup settings or automatically replace the running bridge during diagnosis.

## User-authorized local activation

The user subsequently requested replacement. At 10:17 JST on October 24,
replaced only the three existing self-use executables in place, without old
binary backups. Tray applications were stopped by their exact process paths
after UI inspection; only these bridge processes were restarted. Before
restart, the installed UAD desktop self-test, Windows EUCON unregistered feedback
self-test and Mackie no-MIDI smoke all exited successfully. Executable hashes
match the reviewed candidates, nine settings files retain their hashes, and
all three registry startup entries retain their original paths/arguments.

Running versions/PIDs: UAD `1.1.1-test.1` / 6780, Windows EUCON `1.0.4-test.1` /
15408, Mackie `1.0.3-test.1` / 23916. EuControl 25264, MC_Client 25108 and
UAMixerEngine 29188 retained their existing processes and start times. Windows
EUCON visibility callbacks and Mackie feedback on the saved port pair resumed.
This confirms the application paths, not physical control/meter certification.

Before replacement the old UAD UI already displayed no online Apollo. After
replacement its observer still reports `No online Apollo`; no claim of a working
UAD device/surface connection is made for this session. Hardware availability
and longer-term blue-screen behavior need further observation. Nothing was
published to GitHub in this activation step.

## Formal release decision

After using the installed candidates, the user reported no apparent issues and
requested formal publication plus replacement of the local copies. Promote the
same functional changes to UAD `1.1.1`, Windows EUCON `1.0.4` and Mackie `1.0.3`.
Rebuild with final resource/UI versions, rerun regression suites, package all
three and retain the existing self-use/startup locations. Release notes must
describe the proven application stability improvements, not claim that the
kernel-memory corruption or blue screens have been conclusively fixed.

Final Release and Debug suites repeated successfully with the final version
resources. All three Inno Setup installers built with matching product
versions, the pinned translation checksum and a valid Microsoft runtime
signature. The executable inputs were checked before packaging; installer logic
is unchanged from the earlier installed packages except product versions.
The local executables were replaced in place with the final builds, and UAD's
desktop self-test, Windows EUCON's unregistered feedback self-test and Mackie's
no-MIDI smoke each passed. Nine persisted settings files and all registry
startup entries remained unchanged. External EUCON/UA services kept their PIDs.
