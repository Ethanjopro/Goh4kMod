// drive.exe: small input + screenshot helper for test sessions (only used after Ethan's OK to take the mouse).
// Commands run left to right in one invocation, e.g.
//   drive focus 1234 wait 300 click 1920 1080 wait 1500 snap captures\nav\menu.png
// Coordinates are physical screen pixels (the process is per-monitor DPI aware).
//   snap <file.png> [divisor]   desktop duplication of the primary monitor, box-downscaled (default /4)
//   move x y | click x y [right] | dclick x y | wheel n | key name[+name...] | wait ms
//   focus pid                   bring that process's main window to the foreground
//   fg                          print the foreground window's pid, title and size, and the cursor position
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include <dxgi1_6.h>
#include <wincodec.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

template <typename T>
static void SafeRelease(T*& p) {
    if (p) p->Release();
    p = nullptr;
}

// ------------------------------------------------------------------ input

static void SendMouse(DWORD flags, LONG dx = 0, LONG dy = 0, DWORD data = 0) {
    INPUT in = {};
    in.type = INPUT_MOUSE;
    in.mi.dx = dx;
    in.mi.dy = dy;
    in.mi.mouseData = data;
    in.mi.dwFlags = flags;
    SendInput(1, &in, sizeof(in));
}

static void MoveTo(int x, int y) {
    int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
    LONG ax = (LONG)std::lround(x * 65535.0 / (sw - 1));
    LONG ay = (LONG)std::lround(y * 65535.0 / (sh - 1));
    SendMouse(MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE, ax, ay);
}

static void Click(int x, int y, bool right, int count) {
    MoveTo(x, y);
    Sleep(60);
    for (int i = 0; i < count; ++i) {
        SendMouse(right ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_LEFTDOWN);
        Sleep(60);
        SendMouse(right ? MOUSEEVENTF_RIGHTUP : MOUSEEVENTF_LEFTUP);
        if (i + 1 < count) Sleep(80);
    }
}

static WORD VkFromName(const std::string& n) {
    if (n.size() == 1 && isalnum((unsigned char)n[0])) return (WORD)toupper((unsigned char)n[0]);
    if ((n[0] == 'f' || n[0] == 'F') && n.size() <= 3 && isdigit((unsigned char)n[1])) return (WORD)(VK_F1 + atoi(n.c_str() + 1) - 1);
    struct { const char* name; WORD vk; } names[] = {
        {"esc", VK_ESCAPE}, {"enter", VK_RETURN}, {"space", VK_SPACE}, {"tab", VK_TAB}, {"backspace", VK_BACK},
        {"up", VK_UP}, {"down", VK_DOWN}, {"left", VK_LEFT}, {"right", VK_RIGHT}, {"home", VK_HOME}, {"end", VK_END},
        {"pgup", VK_PRIOR}, {"pgdn", VK_NEXT}, {"insert", VK_INSERT}, {"delete", VK_DELETE}, {"shift", VK_SHIFT},
        {"ctrl", VK_CONTROL}, {"alt", VK_MENU}, {"pause", VK_PAUSE}};
    for (auto& e : names)
        if (_stricmp(n.c_str(), e.name) == 0) return e.vk;
    return 0;
}

static void SendKey(WORD vk, bool up) {
    UINT sc = MapVirtualKeyW(vk, MAPVK_VK_TO_VSC_EX);
    INPUT in = {};
    in.type = INPUT_KEYBOARD;
    in.ki.wScan = (WORD)(sc & 0xFF);
    in.ki.dwFlags = KEYEVENTF_SCANCODE | (up ? KEYEVENTF_KEYUP : 0) | ((sc & 0xFF00) ? KEYEVENTF_EXTENDEDKEY : 0);
    SendInput(1, &in, sizeof(in));
}

static bool Key(const std::string& combo) {
    std::vector<WORD> vks;
    size_t start = 0;
    while (start <= combo.size()) {
        size_t plus = combo.find('+', start);
        std::string part = combo.substr(start, plus == std::string::npos ? std::string::npos : plus - start);
        WORD vk = VkFromName(part);
        if (!vk) {
            printf("unknown key '%s'\n", part.c_str());
            return false;
        }
        vks.push_back(vk);
        if (plus == std::string::npos) break;
        start = plus + 1;
    }
    for (WORD vk : vks) {
        SendKey(vk, false);
        Sleep(40);
    }
    for (size_t i = vks.size(); i-- > 0;) {
        SendKey(vks[i], true);
        Sleep(40);
    }
    return true;
}

struct FindWin {
    DWORD pid;
    HWND best;
    LONG bestArea;
};

static BOOL CALLBACK EnumProc(HWND h, LPARAM lp) {
    auto* f = (FindWin*)lp;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != f->pid || !IsWindowVisible(h) || GetWindow(h, GW_OWNER)) return TRUE;
    RECT r;
    GetWindowRect(h, &r);
    LONG area = (r.right - r.left) * (r.bottom - r.top);
    if (area > f->bestArea) {
        f->best = h;
        f->bestArea = area;
    }
    return TRUE;
}

static bool Focus(DWORD pid) {
    FindWin f = {pid, nullptr, -1};
    EnumWindows(EnumProc, (LPARAM)&f);
    if (!f.best) {
        printf("focus: no visible window for pid %lu\n", pid);
        return false;
    }
    if (IsIconic(f.best)) ShowWindow(f.best, SW_RESTORE);
    if (!SetForegroundWindow(f.best)) {
        HWND fg = GetForegroundWindow();
        DWORD fgThread = GetWindowThreadProcessId(fg, nullptr), me = GetCurrentThreadId();
        AttachThreadInput(me, fgThread, TRUE);
        SetForegroundWindow(f.best);
        BringWindowToTop(f.best);
        AttachThreadInput(me, fgThread, FALSE);
    }
    Sleep(150);
    bool ok = GetForegroundWindow() == f.best;
    printf("focus pid %lu hwnd %p: %s\n", pid, f.best, ok ? "foreground" : "NOT foreground");
    return ok;
}

static void PrintForeground() {
    HWND h = GetForegroundWindow();
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    wchar_t title[256] = L"";
    GetWindowTextW(h, title, 256);
    RECT r = {};
    GetWindowRect(h, &r);
    POINT p = {};
    GetCursorPos(&p);
    char utf8[768] = "";
    WideCharToMultiByte(CP_UTF8, 0, title, -1, utf8, sizeof(utf8), nullptr, nullptr);
    printf("fg pid=%lu hwnd=%p rect=(%ld,%ld)-(%ld,%ld) cursor=(%ld,%ld) title=\"%s\"\n", pid, h, r.left, r.top, r.right,
           r.bottom, p.x, p.y, utf8);
}

// ------------------------------------------------------------------ snap

static float HalfToFloat(uint16_t h) {
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

static uint8_t LinearToSrgb8(float c) {
    if (!(c > 0)) return 0;
    if (c >= 1) return 255;
    float s = c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1 / 2.4f) - 0.055f;
    return (uint8_t)std::lround(s * 255);
}

static bool SavePng(const std::wstring& path, int w, int h, const std::vector<uint8_t>& bgr) {
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
              SUCCEEDED(frame->SetSize(w, h));
    WICPixelFormatGUID fmt = GUID_WICPixelFormat24bppBGR;
    ok = ok && SUCCEEDED(frame->SetPixelFormat(&fmt)) && IsEqualGUID(fmt, GUID_WICPixelFormat24bppBGR) &&
         SUCCEEDED(frame->WritePixels(h, w * 3, (UINT)bgr.size(), const_cast<BYTE*>(bgr.data()))) &&
         SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit());
    SafeRelease(frame);
    SafeRelease(enc);
    SafeRelease(stream);
    SafeRelease(fac);
    return ok;
}

static bool Snap(const std::wstring& path, int div) {
    if (div < 1) div = 1;
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) return printf("snap: no DXGI factory\n"), false;
    IDXGIAdapter1* adapter = nullptr;
    IDXGIOutput* output = nullptr;
    for (UINT a = 0; !output && factory->EnumAdapters1(a, &adapter) != DXGI_ERROR_NOT_FOUND; ++a) {
        IDXGIOutput* o = nullptr;
        for (UINT i = 0; !output && adapter->EnumOutputs(i, &o) != DXGI_ERROR_NOT_FOUND; ++i) {
            DXGI_OUTPUT_DESC od;
            o->GetDesc(&od);
            if (od.AttachedToDesktop && od.DesktopCoordinates.left == 0 && od.DesktopCoordinates.top == 0)
                output = o;
            else
                o->Release();
        }
        if (!output) SafeRelease(adapter);
    }
    SafeRelease(factory);
    if (!output) return printf("snap: primary output not found\n"), false;

    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    HRESULT hr = D3D11CreateDevice(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &dev,
                                   nullptr, &ctx);
    SafeRelease(adapter);
    IDXGIOutputDuplication* dup = nullptr;
    if (SUCCEEDED(hr)) {
        IDXGIOutput5* o5 = nullptr;
        if (SUCCEEDED(output->QueryInterface(IID_PPV_ARGS(&o5)))) {
            DXGI_FORMAT fmts[] = {DXGI_FORMAT_B8G8R8A8_UNORM, DXGI_FORMAT_R16G16B16A16_FLOAT};
            hr = o5->DuplicateOutput1(dev, 0, 2, fmts, &dup);
            o5->Release();
        }
        if (!dup) {
            IDXGIOutput1* o1 = nullptr;
            if (SUCCEEDED(output->QueryInterface(IID_PPV_ARGS(&o1)))) {
                hr = o1->DuplicateOutput(dev, &dup);
                o1->Release();
            }
        }
    }
    SafeRelease(output);
    if (!dup) {
        printf("snap: desktop duplication failed (0x%08lx)\n", hr);
        SafeRelease(ctx);
        SafeRelease(dev);
        return false;
    }

    ID3D11Texture2D* tex = nullptr;
    for (int tries = 0; tries < 20 && !tex; ++tries) {
        DXGI_OUTDUPL_FRAME_INFO fi = {};
        IDXGIResource* res = nullptr;
        hr = dup->AcquireNextFrame(250, &fi, &res);
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) continue;
        if (FAILED(hr)) break;
        if (fi.LastPresentTime.QuadPart == 0 && tries < 3) {  // pointer-only update; wait for an image
            res->Release();
            dup->ReleaseFrame();
            continue;
        }
        res->QueryInterface(IID_PPV_ARGS(&tex));
        res->Release();
        if (!tex) dup->ReleaseFrame();
    }
    if (!tex) {
        printf("snap: no frame (0x%08lx)\n", hr);
        SafeRelease(dup);
        SafeRelease(ctx);
        SafeRelease(dev);
        return false;
    }
    D3D11_TEXTURE2D_DESC td;
    tex->GetDesc(&td);
    D3D11_TEXTURE2D_DESC sd = td;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    sd.MiscFlags = 0;
    ID3D11Texture2D* staging = nullptr;
    dev->CreateTexture2D(&sd, nullptr, &staging);
    if (staging) ctx->CopyResource(staging, tex);
    tex->Release();
    dup->ReleaseFrame();

    bool ok = false;
    D3D11_MAPPED_SUBRESOURCE m = {};
    if (staging && SUCCEEDED(ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m))) {
        int W = (int)td.Width, H = (int)td.Height, w = W / div, h = H / div;
        bool fp16 = td.Format == DXGI_FORMAT_R16G16B16A16_FLOAT;
        std::vector<uint8_t> out((size_t)w * h * 3);
        double lumSum = 0;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                double acc[3] = {0, 0, 0};
                for (int sy = 0; sy < div; ++sy) {
                    const uint8_t* row = (const uint8_t*)m.pData + (size_t)(y * div + sy) * m.RowPitch;
                    for (int sx = 0; sx < div; ++sx) {
                        int px = x * div + sx;
                        if (fp16) {
                            const uint16_t* p = (const uint16_t*)row + (size_t)px * 4;  // RGBA linear scRGB
                            acc[0] += LinearToSrgb8(HalfToFloat(p[2]));
                            acc[1] += LinearToSrgb8(HalfToFloat(p[1]));
                            acc[2] += LinearToSrgb8(HalfToFloat(p[0]));
                        } else {
                            const uint8_t* p = row + (size_t)px * 4;  // BGRA
                            acc[0] += p[0];
                            acc[1] += p[1];
                            acc[2] += p[2];
                        }
                    }
                }
                uint8_t* o = &out[((size_t)y * w + x) * 3];
                for (int c = 0; c < 3; ++c) o[c] = (uint8_t)std::lround(acc[c] / (div * div));
                lumSum += 0.114 * o[0] + 0.587 * o[1] + 0.299 * o[2];
            }
        }
        ctx->Unmap(staging, 0);
        ok = SavePng(path, w, h, out);
        printf("snap %ls: %dx%d from %ux%u %s, mean luma %.1f, %s\n", path.c_str(), w, h, td.Width, td.Height,
               fp16 ? "fp16 (scRGB, clipped)" : "bgra8", lumSum / ((double)w * h), ok ? "saved" : "SAVE FAILED");
    }
    SafeRelease(staging);
    SafeRelease(dup);
    SafeRelease(ctx);
    SafeRelease(dev);
    return ok;
}

// ------------------------------------------------------------------ main

int wmain(int argc, wchar_t** argv) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    auto arg = [&](int i) { return i < argc ? std::wstring(argv[i]) : std::wstring(); };
    auto num = [&](int i) { return i < argc ? _wtoi(argv[i]) : 0; };
    auto narrow = [](const std::wstring& s) {  // key names are ASCII
        std::string r;
        for (wchar_t c : s) r += (char)c;
        return r;
    };
    int rc = 0;
    for (int i = 1; i < argc;) {
        std::wstring cmd = arg(i);
        if (cmd == L"snap") {
            std::wstring file = arg(i + 1);
            int div = 4;
            if (i + 2 < argc && iswdigit(argv[i + 2][0])) div = num(i + 2), i += 1;
            if (!Snap(file, div)) rc = 1;
            i += 2;
        } else if (cmd == L"move") {
            MoveTo(num(i + 1), num(i + 2));
            i += 3;
        } else if (cmd == L"click" || cmd == L"dclick") {
            bool right = arg(i + 3) == L"right";
            Click(num(i + 1), num(i + 2), right, cmd == L"dclick" ? 2 : 1);
            i += right ? 4 : 3;
        } else if (cmd == L"wheel") {
            SendMouse(MOUSEEVENTF_WHEEL, 0, 0, (DWORD)(num(i + 1) * WHEEL_DELTA));
            i += 2;
        } else if (cmd == L"key") {
            if (!Key(narrow(arg(i + 1)))) rc = 1;
            i += 2;
        } else if (cmd == L"wait") {
            Sleep(num(i + 1));
            i += 2;
        } else if (cmd == L"focus") {
            if (!Focus((DWORD)num(i + 1))) rc = 1;
            i += 2;
        } else if (cmd == L"fg") {
            PrintForeground();
            i += 1;
        } else {
            printf("unknown command '%ls'\n", cmd.c_str());
            return 2;
        }
    }
    CoUninitialize();
    return rc;
}
