#include "../../src/ApolloBridge.EuconHost/SdkExampleProbe.h"

void SdkExampleProbeTests()
{
    apollo::SdkExampleProbe probe;
    int calls = 0;
    bool conflict = true;
    const auto query = [&] { ++calls; return conflict; };
    Check(!probe.Check(true, 100, false, query) && calls == 0,
          "A connected bridge must not enumerate processes");
    Check(probe.Check(false, 100, false, query) && calls == 1,
          "A disconnected bridge checks conflicts before recovery");
    conflict = false;
    Check(probe.Check(false, 5099, false, query) && calls == 1,
          "A blocked recovery does not poll the process list on every UI frame");
    Check(!probe.Check(false, 5100, false, query) && calls == 2,
          "Conflict expiry permits automatic recovery when the example exits");
    conflict = true;
    Check(probe.Check(false, 5101, true, query) && calls == 3,
          "An explicit connection always performs a fresh conflict check");
    Check(!probe.Check(true, 20000, false, query) && calls == 3,
          "Established connections clear stale conflict state without scans");
    conflict = false;
    Check(!probe.Check(false, 20001, false, query) && calls == 4,
          "A newly disconnected bridge does not inherit an old check deadline");
    Check(apollo::AnyProcessNamed({L"aPoLlOtEsTs.ExE"}),
          "Process lookup finds the running test executable case insensitively");
    Check(!apollo::AnyProcessNamed({L"wfb-nonexistent-regression-process-53cbe87.exe"}) &&
          !apollo::AnyProcessNamed({}), "Absent and empty process names do not create a conflict");
}
