// Rational coordinate mapping between the game's logical units and real pixels: real = logical * n / d,
// with n >= d. Down rounds towards -infinity and Up towards +infinity, so Down(Up(l)) == l for every l: a
// position the game writes (SetCursorPos, SetWindowPos) reads back unchanged. With d == 1 these are exactly
// the floor division and multiply of the integer 2x mode. Header-only so proxytest can check it directly.
#pragma once
#include <cstdint>

namespace goh4k {

struct Ratio {
    int64_t n = 1, d = 1;
};

inline bool operator==(const Ratio& a, const Ratio& b) { return a.n == b.n && a.d == b.d; }

inline int64_t FloorDiv(int64_t a, int64_t b) {
    int64_t q = a / b;
    return (a % b != 0 && ((a < 0) != (b < 0))) ? q - 1 : q;
}

inline long RatioDown(long v, Ratio r) { return (long)FloorDiv((int64_t)v * r.d, r.n); }
inline long RatioUp(long v, Ratio r) { return (long)-FloorDiv(-(int64_t)v * r.n, r.d); }

// real / logical in lowest terms; 1/1 when real isn't above logical (nothing to shrink).
inline Ratio RatioOf(uint32_t real, uint32_t logical) {
    if (!logical || real <= logical) return {};
    uint32_t a = real, b = logical;
    while (b) {
        uint32_t t = a % b;
        a = b;
        b = t;
    }
    return {(int64_t)(real / a), (int64_t)(logical / a)};
}

// The factor for a real screen against the logical minimum (1920x1080): the smaller of the two axis ratios,
// so the logical screen is never smaller than the minimum on either axis. 16:9 and wider screens are scaled
// by height (3440x1440 -> 2580x1080), narrower ones by width (2560x1600 -> 1920x1200); 1/1 at 1080p-class
// sizes and below (1920x1200, 2560x1080).
inline Ratio RatioForScreen(uint32_t realW, uint32_t realH, uint32_t minW, uint32_t minH) {
    bool byWidth = (uint64_t)realW * minH < (uint64_t)realH * minW;
    return byWidth ? RatioOf(realW, minW) : RatioOf(realH, minH);
}

}  // namespace goh4k
