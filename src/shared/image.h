// Shared image helpers (header-only): pixel conversion from common D3D formats to 8-bit BGR, box
// downscaling, and PNG writing through WIC (part of Windows). Used by the proxy and by drive.exe/imgtool.
#pragma once
#include <windows.h>
#include <dxgiformat.h>
#include <wincodec.h>
#include <cmath>
#include <cstdint>
#include <string>
#include <vector>
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "ole32.lib")

namespace img {

struct Bgr {
    int w = 0, h = 0;
    std::vector<uint8_t> px;  // w*h*3, B G R
};

inline float HalfToFloat(uint16_t h) {
    uint32_t sign = (h >> 15) & 1, exp = (h >> 10) & 0x1F, mant = h & 0x3FF;
    float v;
    if (exp == 0)
        v = std::ldexp((float)mant, -24);
    else if (exp == 31)
        v = mant ? 0.0f : 65504.0f;
    else
        v = std::ldexp((float)(mant | 0x400), (int)exp - 25);
    return sign ? -v : v;
}

inline float Float11(uint32_t bits) {  // 6-bit mantissa, 5-bit exponent, no sign
    uint32_t exp = (bits >> 6) & 0x1F, mant = bits & 0x3F;
    if (exp == 0) return std::ldexp((float)mant, -20);
    if (exp == 31) return 65000.0f;
    return std::ldexp((float)(mant | 0x40), (int)exp - 21);
}

inline float Float10(uint32_t bits) {  // 5-bit mantissa, 5-bit exponent, no sign
    uint32_t exp = (bits >> 5) & 0x1F, mant = bits & 0x1F;
    if (exp == 0) return std::ldexp((float)mant, -19);
    if (exp == 31) return 65000.0f;
    return std::ldexp((float)(mant | 0x20), (int)exp - 20);
}

inline uint8_t LinearToSrgb8(float c) {
    if (!(c > 0)) return 0;
    if (c >= 1) return 255;
    float s = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1 / 2.4f) - 0.055f;
    return (uint8_t)std::lround(s * 255);
}

inline uint8_t Unorm8(float c) { return c <= 0 ? 0 : c >= 1 ? 255 : (uint8_t)std::lround(c * 255); }

// Float color (HDR scene targets etc.): Reinhard x/(1+x), then sRGB, so values above 1 stay visible.
inline uint8_t ToneMap8(float c) { return LinearToSrgb8(c > 0 ? c / (1 + c) : 0); }

// Converts one mapped subresource to BGR. Single-channel formats (depth, AO, masks) are stretched from their
// min..max to grey. Returns false with *why set for formats it doesn't know.
inline bool ToBgr(DXGI_FORMAT fmt, const void* data, UINT pitch, int w, int h, Bgr* out, std::string* why) {
    out->w = w;
    out->h = h;
    out->px.assign((size_t)w * h * 3, 0);
    auto row = [&](int y) { return (const uint8_t*)data + (size_t)y * pitch; };
    auto put = [&](int x, int y, uint8_t r, uint8_t g, uint8_t b) {
        uint8_t* o = &out->px[((size_t)y * w + x) * 3];
        o[0] = b;
        o[1] = g;
        o[2] = r;
    };
    switch (fmt) {
        case DXGI_FORMAT_B8G8R8A8_UNORM: case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        case DXGI_FORMAT_B8G8R8X8_UNORM: case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: case DXGI_FORMAT_B8G8R8X8_TYPELESS:
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const uint8_t* p = row(y) + x * 4;
                    put(x, y, p[2], p[1], p[0]);
                }
            return true;
        case DXGI_FORMAT_R8G8B8A8_UNORM: case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        case DXGI_FORMAT_R8G8B8A8_UINT: case DXGI_FORMAT_R8G8B8A8_SNORM:
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const uint8_t* p = row(y) + x * 4;
                    put(x, y, p[0], p[1], p[2]);
                }
            return true;
        case DXGI_FORMAT_R10G10B10A2_UNORM: case DXGI_FORMAT_R10G10B10A2_TYPELESS: case DXGI_FORMAT_R10G10B10A2_UINT:
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    uint32_t v = ((const uint32_t*)row(y))[x];
                    put(x, y, (uint8_t)((v & 0x3FF) >> 2), (uint8_t)(((v >> 10) & 0x3FF) >> 2),
                        (uint8_t)(((v >> 20) & 0x3FF) >> 2));
                }
            return true;
        case DXGI_FORMAT_R16G16B16A16_FLOAT: case DXGI_FORMAT_R16G16B16A16_TYPELESS:
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const uint16_t* p = (const uint16_t*)row(y) + x * 4;
                    put(x, y, ToneMap8(HalfToFloat(p[0])), ToneMap8(HalfToFloat(p[1])), ToneMap8(HalfToFloat(p[2])));
                }
            return true;
        case DXGI_FORMAT_R32G32B32A32_FLOAT: case DXGI_FORMAT_R32G32B32A32_TYPELESS:
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const float* p = (const float*)row(y) + x * 4;
                    put(x, y, ToneMap8(p[0]), ToneMap8(p[1]), ToneMap8(p[2]));
                }
            return true;
        case DXGI_FORMAT_R11G11B10_FLOAT:
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    uint32_t v = ((const uint32_t*)row(y))[x];
                    put(x, y, ToneMap8(Float11(v & 0x7FF)), ToneMap8(Float11((v >> 11) & 0x7FF)),
                        ToneMap8(Float10(v >> 22)));
                }
            return true;
        case DXGI_FORMAT_R16G16_FLOAT: case DXGI_FORMAT_R16G16_TYPELESS:
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const uint16_t* p = (const uint16_t*)row(y) + x * 2;
                    put(x, y, Unorm8(HalfToFloat(p[0]) * 0.5f + 0.5f), Unorm8(HalfToFloat(p[1]) * 0.5f + 0.5f), 0);
                }
            return true;
        case DXGI_FORMAT_R8G8_UNORM: case DXGI_FORMAT_R8G8_TYPELESS:
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) {
                    const uint8_t* p = row(y) + x * 2;
                    put(x, y, p[0], p[1], 0);
                }
            return true;
        default: break;
    }
    // Single channel: read as float, then stretch min..max to 0..255.
    std::vector<float> v((size_t)w * h);
    bool ok = true;
    for (int y = 0; y < h && ok; ++y)
        for (int x = 0; x < w && ok; ++x) {
            float f;
            switch (fmt) {
                case DXGI_FORMAT_R32_FLOAT: case DXGI_FORMAT_R32_TYPELESS: case DXGI_FORMAT_D32_FLOAT:
                    f = ((const float*)row(y))[x]; break;
                case DXGI_FORMAT_R16_FLOAT: f = HalfToFloat(((const uint16_t*)row(y))[x]); break;
                case DXGI_FORMAT_R16_UNORM: case DXGI_FORMAT_R16_TYPELESS: case DXGI_FORMAT_D16_UNORM:
                    f = ((const uint16_t*)row(y))[x] / 65535.0f; break;
                case DXGI_FORMAT_R8_UNORM: case DXGI_FORMAT_R8_TYPELESS: case DXGI_FORMAT_A8_UNORM:
                    f = row(y)[x] / 255.0f; break;
                case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_D24_UNORM_S8_UINT: case DXGI_FORMAT_R24_UNORM_X8_TYPELESS:
                    f = (((const uint32_t*)row(y))[x] & 0xFFFFFF) / 16777215.0f; break;
                case DXGI_FORMAT_R32G8X24_TYPELESS: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
                case DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS:
                    f = ((const float*)row(y))[x * 2]; break;
                default: ok = false; f = 0; break;
            }
            v[(size_t)y * w + x] = f;
        }
    if (!ok) {
        if (why) *why = "unsupported format " + std::to_string((int)fmt);
        return false;
    }
    float lo = 1e30f, hi = -1e30f;
    for (float f : v)
        if (std::isfinite(f)) lo = f < lo ? f : lo, hi = f > hi ? f : hi;
    float span = hi > lo ? hi - lo : 1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            float f = v[(size_t)y * w + x];
            uint8_t g = std::isfinite(f) ? Unorm8((f - lo) / span) : 255;
            put(x, y, g, g, g);
        }
    if (why) *why = "single channel stretched from " + std::to_string(lo) + " to " + std::to_string(hi);
    return true;
}

// Box-filter downscale by an integer divisor.
inline Bgr Downscale(const Bgr& src, int div) {
    Bgr o;
    if (div <= 1) return src;
    o.w = src.w / div;
    o.h = src.h / div;
    o.px.resize((size_t)o.w * o.h * 3);
    for (int y = 0; y < o.h; ++y)
        for (int x = 0; x < o.w; ++x)
            for (int c = 0; c < 3; ++c) {
                unsigned sum = 0;
                for (int sy = 0; sy < div; ++sy)
                    for (int sx = 0; sx < div; ++sx) sum += src.px[(((size_t)y * div + sy) * src.w + x * div + sx) * 3 + c];
                o.px[((size_t)y * o.w + x) * 3 + c] = (uint8_t)((sum + div * div / 2) / (div * div));
            }
    return o;
}

// Writes a 24-bit PNG. Needs COM initialised on the calling thread.
inline bool SavePng(const std::wstring& path, const Bgr& im) {
    IWICImagingFactory* fac = nullptr;
    IWICStream* stream = nullptr;
    IWICBitmapEncoder* enc = nullptr;
    IWICBitmapFrameEncode* frame = nullptr;
    bool ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fac))) &&
              SUCCEEDED(fac->CreateStream(&stream)) &&
              SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
              SUCCEEDED(fac->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) &&
              SUCCEEDED(enc->Initialize(stream, WICBitmapEncoderNoCache)) &&
              SUCCEEDED(enc->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) &&
              SUCCEEDED(frame->SetSize(im.w, im.h));
    WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
    ok = ok && SUCCEEDED(frame->SetPixelFormat(&fmt)) && IsEqualGUID(fmt, GUID_WICPixelFormat24bppBGR) &&
         SUCCEEDED(frame->WritePixels(im.h, im.w * 3, (UINT)im.px.size(), const_cast<BYTE*>(im.px.data()))) &&
         SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit());
    if (frame) frame->Release();
    if (enc) enc->Release();
    if (stream) stream->Release();
    if (fac) fac->Release();
    return ok;
}

// Reads any WIC-supported image into BGR.
inline bool ReadImage(const std::wstring& path, Bgr* out) {
    IWICImagingFactory* fac = nullptr;
    IWICBitmapDecoder* dec = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICFormatConverter* conv = nullptr;
    UINT w = 0, h = 0;
    bool ok = SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&fac))) &&
              SUCCEEDED(fac->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                       WICDecodeMetadataCacheOnDemand, &dec)) &&
              SUCCEEDED(dec->GetFrame(0, &frame)) && SUCCEEDED(fac->CreateFormatConverter(&conv)) &&
              SUCCEEDED(conv->Initialize(frame, GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, nullptr, 0,
                                         WICBitmapPaletteTypeCustom)) &&
              SUCCEEDED(conv->GetSize(&w, &h));
    if (ok) {
        out->w = (int)w;
        out->h = (int)h;
        out->px.resize((size_t)w * h * 3);
        ok = SUCCEEDED(conv->CopyPixels(nullptr, w * 3, (UINT)out->px.size(), out->px.data()));
    }
    if (conv) conv->Release();
    if (frame) frame->Release();
    if (dec) dec->Release();
    if (fac) fac->Release();
    return ok;
}

}  // namespace img
