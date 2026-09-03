#pragma once
#include "Json.h"
#include <cmath>
#include <cstdint>
#include <sstream>
#include <stdexcept>

namespace apollo
{
// Preferences are intent, never an active controller epoch. Loading this file
// cannot authorize a write. Runtime identity/readback guards remain mandatory.
struct AccessPolicy
{
    bool channels = false, monitor = false, sensitive = false, configuration = false;
    bool Any() const
    {
        return channels || monitor || sensitive || configuration;
    }
    bool operator==(const AccessPolicy &p) const
    {
        return channels == p.channels && monitor == p.monitor && sensitive == p.sensitive &&
               configuration == p.configuration;
    }
    bool operator!=(const AccessPolicy &p) const
    {
        return !(*this == p);
    }
};
struct Preferences
{
    std::string language = "zh-CN";
    bool background = true, startMinimized = false, autoConnect = true;
    bool restorePermissions = false, configExtension = true;
    bool focusShortcutEnabled = true, focusReturnToBackground = true;
    std::uint32_t focusShortcutModifiers = 7, focusShortcutKey = 'U';
    AccessPolicy access;
    double monitorCeiling = -20;
    std::string trustedSystem;
};
inline void ValidatePreferences(const Preferences &p)
{
    if (p.language != "zh-CN" && p.language != "en-US")
        throw std::invalid_argument("Unsupported interface language");
    if (!std::isfinite(p.monitorCeiling) || p.monitorCeiling < -96 || p.monitorCeiling > 0)
        throw std::invalid_argument("Monitor ceiling must be between -96 and 0 dB");
    if (p.access.sensitive && !p.access.channels)
        throw std::invalid_argument("Sensitive controls require channel permission");
    if (p.trustedSystem.size() > 4096)
        throw std::invalid_argument("Invalid saved system identity");
    if (!p.focusShortcutModifiers || (p.focusShortcutModifiers & ~15U) ||
        !p.focusShortcutKey || p.focusShortcutKey > 0xFE)
        throw std::invalid_argument("Invalid global shortcut");
}
inline std::string PreferenceString(const std::string &s)
{
    std::string out = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (unsigned char ch : s)
    {
        if (ch == '"' || ch == '\\')
        {
            out += '\\';
            out += char(ch);
        }
        else if (ch < 32)
        {
            out += "\\u00";
            out += hex[ch >> 4];
            out += hex[ch & 15];
        }
        else
            out += char(ch);
    }
    return out + '"';
}
inline std::string EncodePreferences(const Preferences &p)
{
    ValidatePreferences(p);
    std::ostringstream o;
    o.imbue(std::locale::classic());
    o << "{\n  \"version\": 1,\n  \"language\": " << PreferenceString(p.language);
    const auto flag = [&](const char *key, bool v) {
        o << ",\n  \"" << key << "\": " << (v ? "true" : "false");
    };
    flag("background", p.background);
    flag("startMinimized", p.startMinimized);
    flag("autoConnect", p.autoConnect);
    flag("restorePermissions", p.restorePermissions);
    flag("configExtension", p.configExtension);
    flag("focusShortcutEnabled", p.focusShortcutEnabled);
    flag("focusReturnToBackground", p.focusReturnToBackground);
    flag("channels", p.access.channels);
    flag("monitor", p.access.monitor);
    flag("sensitive", p.access.sensitive);
    flag("configuration", p.access.configuration);
    o << ",\n  \"focusShortcutModifiers\": " << p.focusShortcutModifiers
      << ",\n  \"focusShortcutKey\": " << p.focusShortcutKey
      << ",\n  \"monitorCeiling\": " << p.monitorCeiling
      << ",\n  \"trustedSystem\": " << PreferenceString(p.trustedSystem) << "\n}\n";
    return o.str();
}
inline Preferences DecodePreferences(const std::string &text)
{
    if (text.size() > 32768)
        throw std::invalid_argument("Settings file too large");
    const auto j = Json::Parse(text);
    if (j.kind != Json::Kind::Object || j.At("version").kind != Json::Kind::Number ||
        j.At("version").Number() != 1)
        throw std::invalid_argument("Unsupported settings version");
    Preferences p;
    const auto flag = [&](const char *key, bool &out) {
        if (!j.Has(key))
            return;
        if (j.At(key).kind != Json::Kind::Boolean)
            throw std::invalid_argument("Invalid Boolean preference");
        out = j.At(key).Bool();
    };
    const auto str = [&](const char *key, std::string &out) {
        if (!j.Has(key))
            return;
        if (j.At(key).kind != Json::Kind::String)
            throw std::invalid_argument("Invalid text preference");
        out = j.At(key).String();
    };
    str("language", p.language);
    str("trustedSystem", p.trustedSystem);
    flag("background", p.background);
    flag("startMinimized", p.startMinimized);
    flag("autoConnect", p.autoConnect);
    flag("restorePermissions", p.restorePermissions);
    flag("configExtension", p.configExtension);
    flag("focusShortcutEnabled", p.focusShortcutEnabled);
    flag("focusReturnToBackground", p.focusReturnToBackground);
    flag("channels", p.access.channels);
    flag("monitor", p.access.monitor);
    flag("sensitive", p.access.sensitive);
    flag("configuration", p.access.configuration);
    if (j.Has("monitorCeiling"))
    {
        if (j.At("monitorCeiling").kind != Json::Kind::Number)
            throw std::invalid_argument("Invalid monitor ceiling");
        p.monitorCeiling = j.At("monitorCeiling").Number(NAN);
    }
    const auto shortcutNumber = [&](const char *key, std::uint32_t &out) {
        if (!j.Has(key)) return;
        if (j.At(key).kind != Json::Kind::Number)
            throw std::invalid_argument("Invalid global shortcut");
        const auto value = j.At(key).Number(NAN);
        if (!std::isfinite(value) || value < 0 || value > 0xffffffffU ||
            std::floor(value) != value)
            throw std::invalid_argument("Invalid global shortcut");
        out = static_cast<std::uint32_t>(value);
    };
    shortcutNumber("focusShortcutModifiers", p.focusShortcutModifiers);
    shortcutNumber("focusShortcutKey", p.focusShortcutKey);
    ValidatePreferences(p);
    return p;
}
inline bool CanRestorePermissions(const Preferences &p, bool fresh, const std::string &identity)
{
    return p.restorePermissions && p.access.Any() && fresh && !p.trustedSystem.empty() && !identity.empty() &&
           p.trustedSystem == identity;
}
inline bool RaisesAccess(const Preferences &from, const Preferences &to)
{
    return (!from.access.channels && to.access.channels) || (!from.access.monitor && to.access.monitor) ||
           (!from.access.sensitive && to.access.sensitive) ||
           (!from.access.configuration && to.access.configuration) ||
           (!from.restorePermissions && to.restorePermissions) || to.monitorCeiling > from.monitorCeiling;
}
} // namespace apollo
