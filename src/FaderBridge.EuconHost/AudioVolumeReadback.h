#pragma once

#include <cmath>

// An unsuccessful/invalid read is unknown, not silence. Owned by one logical
// audio slot; resetting that slot also resets this state.
struct AudioVolumeReadback
{
    float value = 0.0F;
    bool known = false;

    bool Observe(bool succeeded, float observed) noexcept
    {
        if (!succeeded || !std::isfinite(observed) || observed < 0.0F || observed > 1.0F)
            return false;
        value = observed;
        known = true;
        return true;
    }
};
