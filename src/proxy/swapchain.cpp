// WrappedSwapChain: forwards everything, counts frames, logs size/mode changes, keeps the native-scaling
// factor in step with the window (scale.h), keeps captures/status.txt current so a session can see the
// game's state without reading logs, and runs the diagnostics around Present (diag.cpp).
#include "wrappers.h"
#include "diag.h"
#include "input.h"
#include "scale.h"

namespace goh4k {

static const IID* const kSwapChainIds[] = {&__uuidof(IDXGISwapChain1), &__uuidof(IDXGISwapChain2),
                                           &__uuidof(IDXGISwapChain3), &__uuidof(IDXGISwapChain4)};

WrappedSwapChain::WrappedSwapChain(IDXGISwapChain* real, WrappedDevice* device) : m_device(device) {
    m_real = reinterpret_cast<IDXGISwapChain4*>(real);
    for (int i = 3; i >= 0; --i) {
        IUnknown* p = nullptr;
        if (SUCCEEDED(real->QueryInterface(*kSwapChainIds[i], (void**)&p))) {
            m_level = i + 1;
            m_real = reinterpret_cast<IDXGISwapChain4*>(p);
            p->Release();
            break;
        }
    }
    Log("swapchain wrapped: real=%p IDXGISwapChain%d", real, m_level);
    LogState("created");
    DXGI_SWAP_CHAIN_DESC d = {};
    m_real->GetDesc(&d);
    m_hwnd = d.OutputWindow;
    if (UiScaleConfigured()) {
        SubclassGameWindow(m_hwnd);
        UpdateUiScale("new swapchain");
    }
}

// The factor follows the window's real client size, the full-screen rules its buffer size (scale.h). Only
// the game window's swapchain counts. Called from our module, so GetClientRect isn't converted.
void WrappedSwapChain::UpdateUiScale(const char* why) {
    if (!UiScaleConfigured() || !IsGameWindow(m_hwnd)) return;
    DXGI_SWAP_CHAIN_DESC d = {};
    m_real->GetDesc(&d);
    SetUiBuffer(d.BufferDesc.Width, d.BufferDesc.Height);
    RECT r = {};
    if (GetClientRect(m_hwnd, &r) && !IsIconic(m_hwnd))
        SetUiClient((UINT)r.right, (UINT)r.bottom, ClientCoversMonitor(m_hwnd, r.right, r.bottom), why);
}

// Buffer-size guard (2.1.3). The game resizes with ResizeBuffers(0x0), which DXGI resolves from the real client,
// and creates its swapchains at its mode size, so its buffers always match the screen. But the client size it is
// told is the logical one: a request at exactly that size would give a 1080p picture stretched over a 4K window,
// with k still 2 and the full-screen rules silent (they see buffers == logical). Such a request is given the real
// client size. The client is read now, not from the state: after a real switch to a 1080p mode the window is
// already 1080p (or no longer covers a monitor), so a deliberate 1920x1080 is never changed.
bool RealBufferSize(HWND hwnd, UINT* width, UINT* height, const char* call) {
    if (!width || !height || !*width || !*height || !UiScaleConfigured() || !hwnd || !IsGameWindow(hwnd)) return false;
    UiState s = UiSnapshot();
    if (!s.active || *width != s.lw || *height != s.lh) return false;
    RECT r = {};
    if (!GetClientRect(hwnd, &r) || IsIconic(hwnd)) return false;  // our module's call: real
    UINT cw = (UINT)r.right, ch = (UINT)r.bottom;
    if ((cw == *width && ch == *height) || !ClientCoversMonitor(hwnd, cw, ch)) return false;
    static std::atomic<int> logged{0};
    if (logged++ < 3) Log("guard: %s asked for the logical size %ux%u while the client is %ux%u: made %ux%u", call,
                          *width, *height, cw, ch, cw, ch);
    *width = cw, *height = ch;
    return true;
}

// The game's own mode change (input.h NotifyGameWindowSizeChanged). Not after ResizeBuffers: the game calls
// that in answer to WM_SIZE, so a resync there would loop.
void WrappedSwapChain::ResyncAfterModeChange() {
    if (UiScaleActive() && IsGameWindow(m_hwnd)) NotifyGameWindowSizeChanged();
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    int need = -1;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIDeviceSubObject) ||
        riid == __uuidof(IDXGISwapChain))
        need = 0;
    else
        for (int i = 0; i < 4; ++i)
            if (riid == *kSwapChainIds[i]) need = i + 1;
    if (need >= 0) {
        if (need > m_level) {
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        *ppv = static_cast<IDXGISwapChain4*>(this);
        return S_OK;
    }
    LogIidOnce("swapchain", riid);
    return m_real->QueryInterface(riid, ppv);
}

ULONG STDMETHODCALLTYPE WrappedSwapChain::AddRef() { return m_real->AddRef(); }

ULONG STDMETHODCALLTYPE WrappedSwapChain::Release() {
    ULONG r = m_real->Release();
    if (r == 0) {
        Log("swapchain released after %lld frames", (long long)m_frames);
        // The real swapchain held the device until now, so our device wrapper is still alive. Its stand-ins hold
        // the device too: let them go, or the game's own release of the device would never reach zero.
        if (m_device) m_device->ReleaseStandIns(0);
        delete this;
    }
    return r;
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::GetDevice(REFIID riid, void** ppDevice) {
    if (m_device) return m_device->QueryInterface(riid, ppDevice);
    return m_real->GetDevice(riid, ppDevice);
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::Present(UINT SyncInterval, UINT Flags) {
    if (!(Flags & DXGI_PRESENT_TEST)) DiagBeforePresent(m_real, m_device, m_frames + 1);
    HRESULT hr = m_real->Present(SyncInterval, Flags);
    AfterPresent(hr, Flags);
    return hr;
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::Present1(UINT SyncInterval, UINT PresentFlags,
                                                     const DXGI_PRESENT_PARAMETERS* pPresentParameters) {
    if (!(PresentFlags & DXGI_PRESENT_TEST)) DiagBeforePresent(m_real, m_device, m_frames + 1);
    HRESULT hr = m_real->Present1(SyncInterval, PresentFlags, pPresentParameters);
    AfterPresent(hr, PresentFlags);
    return hr;
}

// One line after the game window's first Present (and its first hook check): is everything the mod needs in
// place?
static void LogHealth() {
    UiState s = UiSnapshot();
    int over = InputHooksPatchedOver();
    std::string patched = over ? Fmt(" (%d patched over by another program)", over) : std::string();
    Log("health: input hooks %d%s, game window followed, swapchain wrapped; client %ux%u%s, buffers %ux%u, k %lld/%lld%s",
        InputHooksInstalled(), patched.c_str(), s.cw, s.ch, s.covers ? " (covers its monitor)" : "", s.bw, s.bh, s.k.n,
        s.k.d, UiScaleConfigured() ? "" : "; native scaling is off");
}

// Dev self-check for the guard's case arriving by another path: buffers at the logical size while the real
// client is larger and k > 1, at two checks in a row (a mode switch leaves the buffers stale for a moment).
static void CheckBufferSize() {
    static int seen = 0;
    static bool warned = false;
    UiState s = UiSnapshot();
    bool bad = s.active && s.bw == s.lw && s.bh == s.lh && (s.cw != s.bw || s.ch != s.bh);
    seen = bad ? seen + 1 : 0;
    if (seen == 2 && !warned) {
        warned = true;
        Log("WARNING: the swapchain buffers are %ux%u, the logical size, while the client is %ux%u at k %lld/%lld: "
            "the picture is upscaled", s.bw, s.bh, s.cw, s.ch, s.k.n, s.k.d);
    }
}

void WrappedSwapChain::AfterPresent(HRESULT hr, UINT flags) {
    if (flags & DXGI_PRESENT_TEST) return;
    ++m_frames;
    DiagAfterPresent(m_real, m_device, m_frames, m_fps);
    InterlockedIncrement64(&g_totalFrames);
    static HRESULT lastLogged = S_OK;
    if (hr != lastLogged) {
        Log("Present returned 0x%08lx (frame %lld)", hr, (long long)m_frames);
        lastLogged = hr;
    }
    if (m_frames == 1) LogState("first Present");
    ULONGLONG now = GetTickCount64();
    bool game = IsGameWindow(m_hwnd);
    if (game && !m_followed) UpdateUiScale("game window followed");  // its buffers too, after a window change
    m_followed = game;
    if (game) {
        ++Counters().gameFrames;
        if (UiScaleActive()) Count(Counters().framesScaled);
        ResyncIfLaidOutReal();
        if (now - m_checkTick >= 2000) {
            bool first = !m_checkTick;
            m_checkTick = now;
            CheckGameWindow();
            if (m_device) m_device->ReleaseStandIns(WrappedDevice::kStandInIdleMs);
            if (first && kLogging) LogHealth();
            if (kLogging) CheckBufferSize();
        }
    }
    if (g_cfg.diagnostics && now - m_statusTick >= (ULONGLONG)g_cfg.statusMs) WriteStatus(now);
}

void WrappedSwapChain::WriteStatus(ULONGLONG now) {
    double fps = 0;
    if (m_statusTick) fps = double(m_frames - m_framesAtStatus) * 1000.0 / double(now - m_statusTick);
    m_fps = fps;
    m_statusTick = now;
    m_framesAtStatus = m_frames;

    DXGI_SWAP_CHAIN_DESC d = {};
    m_real->GetDesc(&d);
    BOOL fs = FALSE;
    m_real->GetFullscreenState(&fs, nullptr);
    SYSTEMTIME st;
    GetLocalTime(&st);
    std::string text = Fmt("pid=%lu\r\nupdated=%04u-%02u-%02u %02u:%02u:%02u\r\nframes=%lld\r\nfps=%.1f\r\n", GetCurrentProcessId(),
                           st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, (long long)m_frames, fps);
    text += "swapchain=" + DescribeSwapChainDesc(d) + "\r\n";
    text += Fmt("fullscreen=%d\r\ncolorspace=%d\r\n", fs, m_colorSpace);
    text += "window=" + DescribeWindow(d.OutputWindow) + "\r\n";
    Ratio ui = UiScaleRatio();
    text += Fmt("config=Wrap=%d UiScale=%d (now %lld/%lld)\r\n", g_cfg.wrap, g_cfg.uiScale, ui.n, ui.d);
    WriteTextFile(CapturesDir() + L"\\status.txt", text);
}

void WrappedSwapChain::LogState(const char* why) {
    DXGI_SWAP_CHAIN_DESC d = {};
    m_real->GetDesc(&d);
    BOOL fs = FALSE;
    m_real->GetFullscreenState(&fs, nullptr);
    Log("swapchain %s: %s fullscreen=%d", why, DescribeSwapChainDesc(d).c_str(), fs);
    Log("  window: %s", DescribeWindow(d.OutputWindow).c_str());
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::SetFullscreenState(BOOL Fullscreen, IDXGIOutput* pTarget) {
    Log("SetFullscreenState(%d, output=%p)", Fullscreen, pTarget);
    HRESULT hr = m_real->SetFullscreenState(Fullscreen, pTarget);
    Log("  -> 0x%08lx", hr);
    if (SUCCEEDED(hr)) ResyncAfterModeChange();
    LogState("after SetFullscreenState");
    return hr;
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::ResizeBuffers(UINT BufferCount, UINT Width, UINT Height,
                                                          DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    Log("ResizeBuffers(count=%u, %ux%u, fmt=%d, flags=0x%x)", BufferCount, Width, Height, (int)NewFormat,
        SwapChainFlags);
    RealBufferSize(m_hwnd, &Width, &Height, "ResizeBuffers");
    HRESULT hr = m_real->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);
    Log("  -> 0x%08lx", hr);
    if (SUCCEEDED(hr)) UpdateUiScale("ResizeBuffers");
    LogState("after ResizeBuffers");
    return hr;
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::ResizeBuffers1(UINT BufferCount, UINT Width, UINT Height,
                                                           DXGI_FORMAT Format, UINT SwapChainFlags,
                                                           const UINT* pCreationNodeMask,
                                                           IUnknown* const* ppPresentQueue) {
    Log("ResizeBuffers1(count=%u, %ux%u, fmt=%d, flags=0x%x)", BufferCount, Width, Height, (int)Format,
        SwapChainFlags);
    RealBufferSize(m_hwnd, &Width, &Height, "ResizeBuffers1");
    HRESULT hr =
        m_real->ResizeBuffers1(BufferCount, Width, Height, Format, SwapChainFlags, pCreationNodeMask, ppPresentQueue);
    Log("  -> 0x%08lx", hr);
    if (SUCCEEDED(hr)) UpdateUiScale("ResizeBuffers1");
    LogState("after ResizeBuffers1");
    return hr;
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::ResizeTarget(const DXGI_MODE_DESC* p) {
    if (p)
        Log("ResizeTarget(%ux%u fmt=%d refresh=%u/%u scanline=%d scaling=%d)", p->Width, p->Height, (int)p->Format,
            p->RefreshRate.Numerator, p->RefreshRate.Denominator, (int)p->ScanlineOrdering, (int)p->Scaling);
    else
        Log("ResizeTarget(null)");
    HRESULT hr = m_real->ResizeTarget(p);
    Log("  -> 0x%08lx", hr);
    if (SUCCEEDED(hr)) ResyncAfterModeChange();
    LogState("after ResizeTarget");
    return hr;
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::SetColorSpace1(DXGI_COLOR_SPACE_TYPE ColorSpace) {
    HRESULT hr = m_real->SetColorSpace1(ColorSpace);
    if ((int)ColorSpace != m_colorSpace) {
        Log("SetColorSpace1(%d) -> 0x%08lx", (int)ColorSpace, hr);
        m_colorSpace = (int)ColorSpace;
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::SetHDRMetaData(DXGI_HDR_METADATA_TYPE Type, UINT Size, void* pMetaData) {
    static int lastType = -1;
    if ((int)Type != lastType) {
        lastType = (int)Type;
        if (Type == DXGI_HDR_METADATA_TYPE_HDR10 && pMetaData && Size >= sizeof(DXGI_HDR_METADATA_HDR10)) {
            auto* m = static_cast<const DXGI_HDR_METADATA_HDR10*>(pMetaData);
            Log("SetHDRMetaData(HDR10: max mastering %u, min mastering %u, MaxCLL %u, MaxFALL %u)",
                m->MaxMasteringLuminance, m->MinMasteringLuminance, m->MaxContentLightLevel,
                m->MaxFrameAverageLightLevel);
        } else {
            Log("SetHDRMetaData(type=%d, size=%u)", (int)Type, Size);
        }
    }
    return m_real->SetHDRMetaData(Type, Size, pMetaData);
}

}  // namespace goh4k
