#include "Json.h"

#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace apollo
{
namespace
{
class Parser
{
  public:
    explicit Parser(std::string_view text) : text_(text)
    {
    }
    Json Parse()
    {
        if (text_.size() > 4 * 1024 * 1024)
            Fail();
        auto result = Value(0);
        Space();
        if (position_ != text_.size())
            Fail();
        return result;
    }

  private:
    [[noreturn]] static void Fail()
    {
        throw std::runtime_error("Invalid Apollo JSON");
    }
    void Space()
    {
        while (position_ < text_.size() && (text_[position_] == ' ' || text_[position_] == '\r' ||
                                            text_[position_] == '\n' || text_[position_] == '\t'))
            ++position_;
    }
    bool Take(char c)
    {
        if (position_ < text_.size() && text_[position_] == c)
        {
            ++position_;
            return true;
        }
        return false;
    }
    bool Digit() const
    {
        return position_ < text_.size() && text_[position_] >= '0' && text_[position_] <= '9';
    }
    void Literal(std::string_view value)
    {
        if (text_.substr(position_, value.size()) != value)
            Fail();
        position_ += value.size();
    }
    unsigned Hex()
    {
        unsigned code = 0;
        for (int i = 0; i < 4; ++i)
        {
            if (position_ >= text_.size())
                Fail();
            const char c = text_[position_++];
            const int v = c >= '0' && c <= '9'   ? c - '0'
                          : c >= 'a' && c <= 'f' ? c - 'a' + 10
                          : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                 : -1;
            if (v < 0)
                Fail();
            code = code * 16 + static_cast<unsigned>(v);
        }
        return code;
    }
    static void Utf8(std::string &out, unsigned c)
    {
        if (c < 0x80)
            out += static_cast<char>(c);
        else if (c < 0x800)
        {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 63));
        }
        else if (c < 0x10000)
        {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 63));
            out += static_cast<char>(0x80 | (c & 63));
        }
        else
        {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 63));
            out += static_cast<char>(0x80 | ((c >> 6) & 63));
            out += static_cast<char>(0x80 | (c & 63));
        }
    }
    std::string String()
    {
        if (!Take('"'))
            Fail();
        std::string out;
        while (position_ < text_.size())
        {
            const unsigned char c = static_cast<unsigned char>(text_[position_++]);
            if (c == '"')
                return out;
            if (c < 32)
                Fail();
            if (c == '\\')
            {
                if (position_ == text_.size())
                    Fail();
                switch (text_[position_++])
                {
                case '"':
                    out += '"';
                    break;
                case '\\':
                    out += '\\';
                    break;
                case '/':
                    out += '/';
                    break;
                case 'b':
                    out += '\b';
                    break;
                case 'f':
                    out += '\f';
                    break;
                case 'n':
                    out += '\n';
                    break;
                case 'r':
                    out += '\r';
                    break;
                case 't':
                    out += '\t';
                    break;
                case 'u': {
                    unsigned code = Hex();
                    if (code >= 0xD800 && code <= 0xDBFF)
                    {
                        if (!Take('\\') || !Take('u'))
                            Fail();
                        const unsigned low = Hex();
                        if (low < 0xDC00 || low > 0xDFFF)
                            Fail();
                        code = 0x10000 + ((code - 0xD800) << 10) + low - 0xDC00;
                    }
                    else if (code >= 0xDC00 && code <= 0xDFFF)
                        Fail();
                    Utf8(out, code);
                    break;
                }
                default:
                    Fail();
                }
            }
            else if (c < 0x80)
                out += static_cast<char>(c);
            else
            {
                const unsigned count = c >= 0xC2 && c <= 0xDF   ? 1
                                       : c >= 0xE0 && c <= 0xEF ? 2
                                       : c >= 0xF0 && c <= 0xF4 ? 3
                                                                : 0;
                if (!count || position_ + count > text_.size())
                    Fail();
                unsigned code = c & (0x7F >> count);
                for (unsigned i = 0; i < count; ++i)
                {
                    const unsigned char next = static_cast<unsigned char>(text_[position_++]);
                    if ((next & 0xC0) != 0x80)
                        Fail();
                    code = (code << 6) | (next & 63);
                }
                if ((count == 1 && code < 0x80) || (count == 2 && code < 0x800) || (count == 3 && code < 0x10000) ||
                    code > 0x10FFFF || (code >= 0xD800 && code <= 0xDFFF))
                    Fail();
                Utf8(out, code);
            }
        }
        Fail();
    }
    Json Value(unsigned depth)
    {
        if (depth > 64 || ++nodes_ > 100000)
            Fail();
        Space();
        if (position_ == text_.size())
            Fail();
        Json out;
        if (Take('{'))
        {
            out.kind = Json::Kind::Object;
            Space();
            if (Take('}'))
                return out;
            do
            {
                Space();
                auto key = String();
                Space();
                if (!Take(':'))
                    Fail();
                if (!out.object.emplace(std::move(key), Value(depth + 1)).second)
                    Fail();
                Space();
                if (Take('}'))
                    return out;
            } while (Take(','));
            Fail();
        }
        if (Take('['))
        {
            out.kind = Json::Kind::Array;
            Space();
            if (Take(']'))
                return out;
            do
            {
                out.array.push_back(Value(depth + 1));
                Space();
                if (Take(']'))
                    return out;
            } while (Take(','));
            Fail();
        }
        if (text_[position_] == '"')
        {
            out.kind = Json::Kind::String;
            out.scalar = String();
            return out;
        }
        if (text_[position_] == 't')
        {
            Literal("true");
            out.kind = Json::Kind::Boolean;
            out.scalar = "true";
            return out;
        }
        if (text_[position_] == 'f')
        {
            Literal("false");
            out.kind = Json::Kind::Boolean;
            out.scalar = "false";
            return out;
        }
        if (text_[position_] == 'n')
        {
            Literal("null");
            return out;
        }
        const auto start = position_;
        Take('-');
        if (!Take('0'))
        {
            if (!Digit())
                Fail();
            while (Digit())
                ++position_;
        }
        if (Take('.'))
        {
            if (!Digit())
                Fail();
            while (Digit())
                ++position_;
        }
        if (Take('e') || Take('E'))
        {
            if (!Take('+'))
                Take('-');
            if (!Digit())
                Fail();
            while (Digit())
                ++position_;
        }
        out.kind = Json::Kind::Number;
        out.scalar = text_.substr(start, position_ - start);
        // Reject overflow rather than permitting NaN/Infinity into a control model.
        if (!std::isfinite(out.Number(std::numeric_limits<double>::quiet_NaN())))
            Fail();
        return out;
    }
    std::string_view text_;
    size_t position_ = 0;
    size_t nodes_ = 0;
};
} // namespace
const Json &Json::At(std::string_view key) const
{
    static const Json empty;
    const auto found = object.find(std::string(key));
    return found == object.end() ? empty : found->second;
}
bool Json::Has(std::string_view key) const
{
    return kind == Kind::Object && object.count(std::string(key)) != 0;
}
bool Json::Bool(bool fallback) const
{
    return kind == Kind::Boolean ? scalar == "true" : fallback;
}
double Json::Number(double fallback) const
{
    if (kind != Kind::Number)
        return fallback;
    double result = 0;
    const auto parsed = std::from_chars(scalar.data(), scalar.data() + scalar.size(), result);
    return parsed.ec == std::errc{} && parsed.ptr == scalar.data() + scalar.size() && std::isfinite(result) ? result
                                                                                                            : fallback;
}
std::string Json::String(std::string fallback) const
{
    return kind == Kind::String ? scalar : fallback;
}
Json Json::Parse(std::string_view text)
{
    return Parser(text).Parse();
}
} // namespace apollo
