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

// realH / logicalH in lowest terms; 1/1 when realH isn't above logicalH (nothing to shrink).
inline Ratio RatioForHeight(uint32_t realH, uint32_t logicalH) {
    if (!logicalH || realH <= logicalH) return {};
    uint32_t a = realH, b = logicalH;
    while (b) {
        uint32_t t = a % b;
        a = b;
        b = t;
    }
    return {(int64_t)(realH / a), (int64_t)(logicalH / a)};
}

}  // namespace goh4k
