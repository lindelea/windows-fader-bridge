void PreferencesTests()
{
    Preferences p;
    Check(!p.access.Any() && !p.restorePermissions,
          "Desktop defaults are read only; no automatic permission restoration");
    Check(p.background && p.autoConnect && p.configExtension && p.monitorCeiling == -20,
          "Desktop defaults, CONFIG and explicit ceiling");
    Check(p.focusShortcutEnabled && p.focusReturnToBackground &&
              p.focusShortcutModifiers == 7 && p.focusShortcutKey == 'U',
          "UAD global shortcut defaults are explicit");
    p.trustedSystem = "device \"quoted\" \\ new\nline";
    p.language = "en-US";
    p.focusShortcutModifiers = 5;
    p.focusShortcutKey = 0x77; // F8
    for (int bits = 0; bits < 64; ++bits)
    {
        p.access = {bool(bits & 1), bool(bits & 2), bool((bits & 4) && (bits & 1)), bool(bits & 8)};
        p.restorePermissions = (bits & 16) != 0;
        p.configExtension = (bits & 32) != 0;
        const auto restored = DecodePreferences(EncodePreferences(p));
        Check(restored.access == p.access && restored.trustedSystem == p.trustedSystem &&
                  restored.restorePermissions == p.restorePermissions &&
                  restored.configExtension == p.configExtension && restored.language == p.language &&
                  restored.monitorCeiling == -20 &&
                  restored.focusShortcutModifiers == p.focusShortcutModifiers &&
                  restored.focusShortcutKey == p.focusShortcutKey,
              "Versioned settings round trip preserves explicit scope and escaped identity");
    }
    p.access = {true, true, true, true};
    p.restorePermissions = true;
    Check(CanRestorePermissions(p, true, p.trustedSystem),
          "Explicit restoration matches the confirmed system");
    Check(!CanRestorePermissions(p, false, p.trustedSystem) && !CanRestorePermissions(p, true, "other") &&
              !CanRestorePermissions(p, true, ""),
          "Stale, different and unknown systems cannot auto-arm");
    p.restorePermissions = false;
    Check(!CanRestorePermissions(p, true, p.trustedSystem),
          "Saved full-control preferences alone do not grant access");
    p.monitorCeiling = 0;
    Check(DecodePreferences(EncodePreferences(p)).monitorCeiling == 0, "Unity ceiling supported");
    p.monitorCeiling = -96;
    Check(DecodePreferences(EncodePreferences(p)).monitorCeiling == -96, "Minimum ceiling supported");
    for (double invalid : std::vector<double>{0.001, 12.0, -96.001, INFINITY, -INFINITY, NAN})
    {
        p.monitorCeiling = invalid;
        Reject([&] { EncodePreferences(p); });
    }
    for (const auto *invalid : {R"({})", R"({"version":2})", R"({"version":1,"monitorCeiling":12})",
                                R"({"version":1,"monitorCeiling":"0"})", R"({"version":1,"channels":1})",
                                R"({"version":1,"language":"unknown"})", R"({"version":1,"sensitive":true})"})
        Reject([&] { DecodePreferences(invalid); });
    for (const auto *invalid : {R"({"version":1,"focusShortcutModifiers":0})",
                                R"({"version":1,"focusShortcutModifiers":16})",
                                R"({"version":1,"focusShortcutKey":0})",
                                R"({"version":1,"focusShortcutKey":"U"})"})
        Reject([&] { DecodePreferences(invalid); });
    Reject([] { DecodePreferences(std::string(33000, ' ')); });
    Preferences base, next;
    next.access.channels = true;
    Check(RaisesAccess(base, next), "Permission escalation requires confirmation");
    next = base;
    next.monitorCeiling = 0;
    Check(RaisesAccess(base, next), "Raised ceiling requires confirmation");
    next = base;
    next.language = "en-US";
    Check(!RaisesAccess(base, next), "Language change does not grant control");
    next = base;
    next.restorePermissions = true;
    Check(RaisesAccess(base, next), "Startup restore requires confirmation");
}
