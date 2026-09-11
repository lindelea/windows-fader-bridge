# Startup recovery

The three desktop bridges may be launched together at Windows sign-in. Their
dependency order is intentionally not encoded in the Run entries: each process
owns recovery for the service or transport it consumes.

- Windows Core Audio initialization stays on its existing owner thread. If
  AudioSrv or the default endpoint is not ready yet, initialization is retried
  once per second. This wait ends before audio frames, meter updates, or control
  commands begin and therefore adds no steady-state control latency.
- Windows Fader Bridge for EUCON creates and registers its application node only
  after `EuConManager::Initialize()` succeeds. A failed, unregistered host is
  completely destroyed before the next two-second initialization attempt. Once
  registered, the top-level node remains alive for the process lifetime and is
  never rebuilt as recovery policy.
- Windows Fader Bridge for Mackie Control continues to enumerate ports every two
  seconds and reconnects only an explicitly saved, uniquely resolvable safe MIDI
  input/output pair. Core Audio startup recovery is shared with the EUCON
  edition.
- UAD Console Bridge for EUCON keeps its existing observer reconnection loop for
  UA Mixer Engine and its bounded EUCON adapter retry. No UAD write is queued or
  replayed across a disconnect.
- All three applications expose a tray **Restart** command. The replacement
  process waits for the source process to exit before taking the single-instance
  identity, so restart cannot create two active adapters.

The EUCON ordering follows the 2026.4 SDK guide and installed declarations:
initialize the library and check the result; create the model only on success;
register one application node; on shutdown unregister processors and the node
before destroying EUCON. The official EuConIO and EuConApp examples use the same
construction and teardown order.

Verification on 2026-09-11:

- both native editions reported Core Audio ready;
- the EUCON edition reported successful EUCON initialization and received audio
  frames;
- the Mackie edition reopened the saved `primary` MIDI pair;
- the UAD edition established its loopback connection to UA Mixer Engine on port
  4710;
- Mackie and UAD tray restart paths replaced their processes and recovered their
  respective connections;
- isolated Mackie tests passed 18 groups / 240,732 assertions and Apollo core
  tests passed 15,348 checks without opening hardware transports.
