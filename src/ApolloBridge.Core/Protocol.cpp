#include "Protocol.h"
#include <stdexcept>

namespace apollo
{
bool IsPath(std::string_view path)
{
    if (path.empty() || path.size() > 2048 || path.front() != '/' || path.find("..") != path.npos ||
        path.find("//") != path.npos)
        return false;
    for (const unsigned char c : path)
        if (!(c == '/' || c == '_' || c == '-' || c == '.' || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9')))
            return false;
    return path.size() == 1 || path.back() != '/';
}
std::string ReadCommand(std::string_view verb, std::string_view path)
{
    if ((verb != "get" && verb != "subscribe") || !IsPath(path))
        throw std::invalid_argument("Only validated Apollo reads are permitted");
    std::string result(verb);
    result += ' ';
    result += path;
    result += '\0';
    return result;
}
std::vector<std::string> FrameDecoder::Feed(std::string_view bytes)
{
    std::vector<std::string> result;
    for (const char c : bytes)
    {
        if (c == '\0')
        {
            if (partial_.empty())
                throw std::runtime_error("Empty Apollo frame");
            if (result.size() >= 4096)
                throw std::runtime_error("Apollo frame count limit");
            result.push_back(std::move(partial_));
            partial_.clear();
        }
        else
        {
            if (partial_.size() == MaxBytes)
                throw std::runtime_error("Apollo frame exceeds 4 MiB");
            partial_ += c;
        }
    }
    return result;
}
} // namespace apollo
