#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace apollo
{
// Bridge-owned dB taper, not UA's FaderLevelTapered conversion. More travel is
// available near unity. Exact native dB remains the authoritative value.
inline std::vector<float> FaderDbTable(double minimum, double maximum)
{
    if (!std::isfinite(minimum) || !std::isfinite(maximum) || minimum >= maximum || minimum < -200 || maximum > 30)
        throw std::runtime_error("Unsupported fader dB range");
    constexpr std::array<double, 8> db{-200, -144, -60, -30, -10, 0, 12, 30};
    constexpr std::array<double, 8> travel{-0.1, 0, 0.125, 0.375, 0.625, 0.8125, 1, 1.25};
    const auto interpolate = [](double x, const auto &from, const auto &to) {
        size_t end = 1;
        while (end + 1 < from.size() && x > from[end])
            ++end;
        return to[end - 1] + (to[end] - to[end - 1]) * (x - from[end - 1]) / (from[end] - from[end - 1]);
    };
    const double first = interpolate(minimum, db, travel), last = interpolate(maximum, db, travel);
    std::vector<float> result;
    result.reserve(1025);
    for (int i = 0; i <= 1024; ++i)
    {
        const float value =
            static_cast<float>(i == 0      ? minimum
                               : i == 1024 ? maximum
                                           : interpolate(first + (last - first) * i / 1024, travel, db));
        if (!result.empty() && value <= result.back())
            throw std::runtime_error("Fader range cannot be represented safely");
        result.push_back(value);
    }
    return result;
}
} // namespace apollo
