#pragma once

#include <cmath>

namespace monarch
{

/** Linear (B-taper) pot mapping: used for DRIVE, TONE, and PRESENCE. */
inline float linearTaper (float x, float R_max)
{
    return R_max * x;
}

/** Audio (A-taper) pot mapping: used for VOL. */
inline float audioTaper (float x, float R_max)
{
    return R_max * std::pow (10.0f, 2.0f * x - 2.0f);
}

} // namespace monarch
