#pragma once
#include <cstdint>

namespace apollo
{
// Owner-thread cache for the pre-connection example-adapter guard.
class SdkExampleProbe
{
    std::uint64_t next_ = 0;
    bool blocked_ = false;
public:
    template<class Query>
    bool Check(bool connected, std::uint64_t now, bool force, Query query)
    {
        if (connected)
        {
            next_ = 0;
            blocked_ = false;
            return false;
        }
        if (force || now >= next_)
        {
            blocked_ = query();
            next_ = now + 5000;
        }
        return blocked_;
    }
};
}
