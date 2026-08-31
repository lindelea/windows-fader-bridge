#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace apollo
{
// Numbers retain their original decimal representation: hardware IDs are int64,
// not floating-point identifiers. No vendor code or third-party parser is used.
struct Json
{
    enum class Kind
    {
        Null,
        Boolean,
        Number,
        String,
        Object,
        Array
    };
    Kind kind = Kind::Null;
    std::string scalar;
    std::map<std::string, Json> object;
    std::vector<Json> array;

    const Json &At(std::string_view key) const;
    bool Has(std::string_view key) const;
    bool Bool(bool fallback = false) const;
    double Number(double fallback = 0.0) const;
    std::string String(std::string fallback = {}) const;
    static Json Parse(std::string_view text);
};
} // namespace apollo
