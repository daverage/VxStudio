#pragma once

#include <cmath>

#if defined(__APPLE__)
#include <Accelerate/Accelerate.h>
#endif

namespace vxsuite {

// Element-wise transcendentals over float arrays. On Apple platforms these
// dispatch to Accelerate's vForce (SIMD, accurately rounded); elsewhere they
// fall back to scalar libm, so results are platform-consistent to float
// rounding. dst may alias src.

inline void vectorExp(float* dst, const float* src, const int count) {
#if defined(__APPLE__)
    vvexpf(dst, src, &count);
#else
    for (int i = 0; i < count; ++i)
        dst[i] = std::exp(src[i]);
#endif
}

// src elements must be > 0.
inline void vectorLog(float* dst, const float* src, const int count) {
#if defined(__APPLE__)
    vvlogf(dst, src, &count);
#else
    for (int i = 0; i < count; ++i)
        dst[i] = std::log(src[i]);
#endif
}

// src elements must be > 0.
inline void vectorLog10(float* dst, const float* src, const int count) {
#if defined(__APPLE__)
    vvlog10f(dst, src, &count);
#else
    for (int i = 0; i < count; ++i)
        dst[i] = std::log10(src[i]);
#endif
}

} // namespace vxsuite
