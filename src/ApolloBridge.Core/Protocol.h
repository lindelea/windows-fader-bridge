#pragma once
#include <string>
#include <string_view>
#include <vector>

namespace apollo
{
bool IsPath(std::string_view path);
// This client deliberately has no state-changing command builder.
std::string ReadCommand(std::string_view verb, std::string_view path);

class FrameDecoder
{
  public:
    static constexpr size_t MaxBytes = 4 * 1024 * 1024;
    std::vector<std::string> Feed(std::string_view bytes);
    bool Partial() const
    {
        return !partial_.empty();
    }
    void Reset()
    {
        partial_.clear();
    }

  private:
    std::string partial_;
};
} // namespace apollo
