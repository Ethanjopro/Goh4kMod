// imgtool.exe: small image helper for evidence and comparisons (WIC, no dependencies).
//   crop <in> <out> <x> <y> <w> <h> [zoom]   cut a region; optional integer nearest-neighbour zoom
//   side <a> <b> <out>                       a and b side by side (b resized to a's size if needed)
//   resize <in> <out> <w> <h>                box filter for integer factors, else nearest
//   diff <a> <b> [heat.png]                  compare; the larger image is box-downscaled to the smaller
//                                            if the sizes differ by an integer factor. Prints mean abs
//                                            difference, PSNR and % of pixels off by more than 16 or 48.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "../shared/image.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>

using img::Bgr;

static Bgr Nearest(const Bgr& s, int w, int h) {
    Bgr o;
    o.w = w;
    o.h = h;
    o.px.resize((size_t)w * h * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            int sx = std::min(s.w - 1, (int)((long long)x * s.w / w)), sy = std::min(s.h - 1, (int)((long long)y * s.h / h));
            memcpy(&o.px[((size_t)y * w + x) * 3], &s.px[((size_t)sy * s.w + sx) * 3], 3);
        }
    return o;
}

static Bgr Resize(const Bgr& s, int w, int h) {
    if (w > 0 && h > 0 && s.w % w == 0 && s.h % h == 0 && s.w / w == s.h / h) return img::Downscale(s, s.w / w);
    return Nearest(s, w, h);
}

static bool Load(const wchar_t* p, Bgr* b) {
    if (img::ReadImage(p, b)) return true;
    wprintf(L"cannot read %s\n", p);
    return false;
}

static bool Save(const wchar_t* p, const Bgr& b) {
    if (img::SavePng(p, b)) {
        wprintf(L"wrote %s (%dx%d)\n", p, b.w, b.h);
        return true;
    }
    wprintf(L"cannot write %s\n", p);
    return false;
}

int wmain(int argc, wchar_t** argv) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (argc < 2) {
        printf("usage: imgtool crop|side|resize|diff ... (see source header)\n");
        return 2;
    }
    std::wstring cmd = argv[1];
    if (cmd == L"crop" && argc >= 8) {
        Bgr in;
        if (!Load(argv[2], &in)) return 1;
        int x = _wtoi(argv[4]), y = _wtoi(argv[5]), w = _wtoi(argv[6]), h = _wtoi(argv[7]);
        int zoom = argc >= 9 ? std::max(1, _wtoi(argv[8])) : 1;
        x = std::clamp(x, 0, in.w - 1);
        y = std::clamp(y, 0, in.h - 1);
        w = std::clamp(w, 1, in.w - x);
        h = std::clamp(h, 1, in.h - y);
        Bgr o;
        o.w = w;
        o.h = h;
        o.px.resize((size_t)w * h * 3);
        for (int r = 0; r < h; ++r) memcpy(&o.px[(size_t)r * w * 3], &in.px[((size_t)(y + r) * in.w + x) * 3], (size_t)w * 3);
        if (zoom > 1) o = Nearest(o, w * zoom, h * zoom);
        return Save(argv[3], o) ? 0 : 1;
    }
    if (cmd == L"side" && argc >= 5) {
        Bgr a, b;
        if (!Load(argv[2], &a) || !Load(argv[3], &b)) return 1;
        if (b.w != a.w || b.h != a.h) b = Resize(b, a.w, a.h);
        Bgr o;
        o.w = a.w * 2 + 8;
        o.h = a.h;
        o.px.assign((size_t)o.w * o.h * 3, 255);
        for (int r = 0; r < a.h; ++r) {
            memcpy(&o.px[(size_t)r * o.w * 3], &a.px[(size_t)r * a.w * 3], (size_t)a.w * 3);
            memcpy(&o.px[((size_t)r * o.w + a.w + 8) * 3], &b.px[(size_t)r * b.w * 3], (size_t)b.w * 3);
        }
        return Save(argv[4], o) ? 0 : 1;
    }
    if (cmd == L"resize" && argc >= 6) {
        Bgr in;
        if (!Load(argv[2], &in)) return 1;
        return Save(argv[3], Resize(in, _wtoi(argv[4]), _wtoi(argv[5]))) ? 0 : 1;
    }
    if (cmd == L"diff" && argc >= 4) {
        Bgr a, b;
        if (!Load(argv[2], &a) || !Load(argv[3], &b)) return 1;
        if (a.w != b.w || a.h != b.h) {
            Bgr& larger = a.w > b.w ? a : b;
            Bgr& smaller = a.w > b.w ? b : a;  // (not "small": rpcndr.h #defines it)
            if (larger.w % smaller.w || larger.h % smaller.h || larger.w / smaller.w != larger.h / smaller.h) {
                printf("sizes %dx%d and %dx%d are not an integer factor apart\n", a.w, a.h, b.w, b.h);
                return 1;
            }
            printf("downscaled %dx%d by %d to compare\n", larger.w, larger.h, larger.w / smaller.w);
            larger = img::Downscale(larger, larger.w / smaller.w);
        }
        double sum = 0, sq = 0;
        size_t over16 = 0, over48 = 0, n = (size_t)a.w * a.h;
        Bgr heat;
        heat.w = a.w;
        heat.h = a.h;
        heat.px.resize(n * 3);
        for (size_t i = 0; i < n; ++i) {
            int m = 0;
            for (int c = 0; c < 3; ++c) {
                int d = abs((int)a.px[i * 3 + c] - (int)b.px[i * 3 + c]);
                sum += d;
                sq += (double)d * d;
                m = std::max(m, d);
            }
            over16 += m > 16;
            over48 += m > 48;
            uint8_t v = (uint8_t)std::min(255, m * 4);
            heat.px[i * 3 + 0] = 0;
            heat.px[i * 3 + 1] = v / 2;
            heat.px[i * 3 + 2] = v;
        }
        double mse = sq / (n * 3.0);
        printf("diff %dx%d: mean abs %.2f, PSNR %.1f dB, >16: %.2f%%, >48: %.2f%%\n", a.w, a.h, sum / (n * 3.0),
               mse > 0 ? 10 * log10(255.0 * 255.0 / mse) : 99.0, 100.0 * over16 / n, 100.0 * over48 / n);
        if (argc >= 5) Save(argv[4], heat);
        return 0;
    }
    printf("bad arguments\n");
    return 2;
}
