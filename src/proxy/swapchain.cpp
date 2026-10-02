// WrappedSwapChain: forwards everything, counts frames, logs size/mode changes, keeps captures/status.txt
// current so a session can see the game's state without reading logs, and runs the diagnostics around
// Present (diag.cpp).
#include "wrappers.h"
#include "diag.h"
#include "input.h"
#include "scale.h"
#include "texture.h"

namespace goh4k {

// ---------------------------------------------------------------- 2x sizing

// The game asks for logical sizes. Returns whether the request runs at 2x, doubling *w/*h in place if so.
// A request whose 2x doesn't fit the monitor (the game is set to native 4K, say) runs natively instead;
// callers switch 2x off for it (SetScaleActive). 0x0 ("use the window size") keeps the current state.
bool ScaleRequest(HWND hwnd, UINT* w, UINT* h) {
    if (!ScaleConfigured() || !w || !h) return false;
    UINT s = (UINT)g_cfg.scale;
    if (*w == 0 || *h == 0) return g_scaleActive;
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &mi);  // not from the exe: real values
    if (*w * s > (UINT)(mi.rcMonitor.right - mi.rcMonitor.left) || *h * s > (UINT)(mi.rcMonitor.bottom - mi.rcMonitor.top))
        return false;
    *w *= s;
    *h *= s;
    return true;
}

void WrappedSwapChain::UpdateLogicalSize() {
    DXGI_SWAP_CHAIN_DESC d = {};
    m_real->GetDesc(&d);
    UINT s = m_scaled ? (UINT)g_cfg.scale : 1;
    g_logicalW = d.BufferDesc.Width / s;
    g_logicalH = d.BufferDesc.Height / s;
    if (ScaleConfigured())
        Log("logical screen %ux%u, real %ux%u%s", (UINT)g_logicalW, (UINT)g_logicalH, d.BufferDesc.Width,
            d.BufferDesc.Height, m_scaled ? "" : " (2x off)");
}

// After a resize decision: remember it, switch 2x on/off, and drop cached backbuffer wrappers.
void WrappedSwapChain::SetScaled(bool scaled, const char* why) {
    m_scaled = scaled;
    SetScaleActive(scaled, why);
    UpdateLogicalSize();
    for (auto& b : m_buffers) b = nullptr;  // old wrappers are left to leak (see texture.cpp)
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::GetDesc(DXGI_SWAP_CHAIN_DESC* pDesc) {
    HRESULT hr = m_real->GetDesc(pDesc);
    if (SUCCEEDED(hr) && pDesc && m_scaled) {
        pDesc->BufferDesc.Width /= (UINT)g_cfg.scale;
        pDesc->BufferDesc.Height /= (UINT)g_cfg.scale;
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::GetDesc1(DXGI_SWAP_CHAIN_DESC1* pDesc) {
    HRESULT hr = m_real->GetDesc1(pDesc);
    if (SUCCEEDED(hr) && pDesc && m_scaled) {
        pDesc->Width /= (UINT)g_cfg.scale;
        pDesc->Height /= (UINT)g_cfg.scale;
    }
    return hr;
}

static const IID* const kSwapChainIds[] = {&__uuidof(IDXGISwapChain1), &__uuidof(IDXGISwapChain2),
                                           &__uuidof(IDXGISwapChain3), &__uuidof(IDXGISwapChain4)};

WrappedSwapChain::WrappedSwapChain(IDXGISwapChain* real, WrappedDevice* device, bool scaled)
    : m_device(device), m_scaled(scaled) {
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
    Log("swapchain wrapped: real=%p IDXGISwapChain%d%s", real, m_level, m_scaled ? ", 2x" : "");
    LogState("created");
    if (ScaleConfigured()) {
        UpdateLogicalSize();
        DXGI_SWAP_CHAIN_DESC d = {};
        m_real->GetDesc(&d);
        SubclassGameWindow(d.OutputWindow);
    }
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
        delete this;
    }
    return r;
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::GetDevice(REFIID riid, void** ppDevice) {
    if (m_device) return m_device->QueryInterface(riid, ppDevice);
    return m_real->GetDevice(riid, ppDevice);
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::GetBuffer(UINT Buffer, REFIID riid, void** ppSurface) {
    // In 2x mode the game gets a wrapper whose GetDesc reports the logical size (texture.cpp).
    bool wrap = m_scaled && ppSurface && Buffer < kMaxBuffers &&
                (riid == __uuidof(ID3D11Texture2D) || riid == __uuidof(ID3D11Resource));
    HRESULT hr = m_real->GetBuffer(Buffer, wrap ? __uuidof(ID3D11Texture2D) : riid, ppSurface);
    if (SUCCEEDED(hr) && m_scaled && ppSurface && *ppSurface) {
        ID3D11Resource* r = nullptr;
        if (SUCCEEDED(static_cast<IUnknown*>(*ppSurface)->QueryInterface(IID_PPV_ARGS(&r)))) {
            TagResource(r, {(UINT)g_cfg.scale, (UINT)g_logicalW, (UINT)g_logicalH});
            r->Release();
        }
    }
    if (SUCCEEDED(hr) && wrap) {
        auto* real = static_cast<ID3D11Texture2D*>(*ppSurface);
        if (!m_buffers[Buffer] || m_buffers[Buffer]->Real() != real)
            m_buffers[Buffer] = new WrappedTexture2D(real, m_device, g_logicalW, g_logicalH);
        *ppSurface = static_cast<ID3D11Texture2D*>(m_buffers[Buffer]);  // carries GetBuffer's reference
    }
    if (!m_loggedBuffer) {
        m_loggedBuffer = true;
        Log("GetBuffer(%u, %s) -> 0x%08lx", Buffer, GuidStr(riid).c_str(), hr);
    }
    return hr;
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
    text += Fmt("config=Wrap=%d Scale=%d\r\n", g_cfg.wrap, g_cfg.scale);
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
    LogState("after SetFullscreenState");
    return hr;
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::ResizeBuffers(UINT BufferCount, UINT Width, UINT Height,
                                                          DXGI_FORMAT NewFormat, UINT SwapChainFlags) {
    Log("ResizeBuffers(count=%u, %ux%u, fmt=%d, flags=0x%x)", BufferCount, Width, Height, (int)NewFormat,
        SwapChainFlags);
    DXGI_SWAP_CHAIN_DESC d = {};
    m_real->GetDesc(&d);
    bool scaled = ScaleRequest(d.OutputWindow, &Width, &Height);
    HRESULT hr = m_real->ResizeBuffers(BufferCount, Width, Height, NewFormat, SwapChainFlags);
    Log("  -> 0x%08lx%s", hr, scaled && Width ? Fmt(" (real %ux%u)", Width, Height).c_str() : "");
    if (SUCCEEDED(hr) && ScaleConfigured()) SetScaled(scaled, "ResizeBuffers");
    LogState("after ResizeBuffers");
    return hr;
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::ResizeBuffers1(UINT BufferCount, UINT Width, UINT Height,
                                                           DXGI_FORMAT Format, UINT SwapChainFlags,
                                                           const UINT* pCreationNodeMask,
                                                           IUnknown* const* ppPresentQueue) {
    Log("ResizeBuffers1(count=%u, %ux%u, fmt=%d, flags=0x%x)", BufferCount, Width, Height, (int)Format,
        SwapChainFlags);
    DXGI_SWAP_CHAIN_DESC d = {};
    m_real->GetDesc(&d);
    bool scaled = ScaleRequest(d.OutputWindow, &Width, &Height);
    HRESULT hr =
        m_real->ResizeBuffers1(BufferCount, Width, Height, Format, SwapChainFlags, pCreationNodeMask, ppPresentQueue);
    Log("  -> 0x%08lx", hr);
    if (SUCCEEDED(hr) && ScaleConfigured()) SetScaled(scaled, "ResizeBuffers1");
    LogState("after ResizeBuffers1");
    return hr;
}

HRESULT STDMETHODCALLTYPE WrappedSwapChain::ResizeTarget(const DXGI_MODE_DESC* p) {
    if (p)
        Log("ResizeTarget(%ux%u fmt=%d refresh=%u/%u scanline=%d scaling=%d)", p->Width, p->Height, (int)p->Format,
            p->RefreshRate.Numerator, p->RefreshRate.Denominator, (int)p->ScanlineOrdering, (int)p->Scaling);
    else
        Log("ResizeTarget(null)");
    DXGI_MODE_DESC mode;
    bool decided = false, scaled = false;
    if (p && ScaleConfigured() && p->Width && p->Height) {
        mode = *p;
        DXGI_SWAP_CHAIN_DESC d = {};
        m_real->GetDesc(&d);
        decided = true;
        scaled = ScaleRequest(d.OutputWindow, &mode.Width, &mode.Height);
        if (scaled) {
            Log("  -> real mode %ux%u", mode.Width, mode.Height);
            p = &mode;
        }
    }
    HRESULT hr = m_real->ResizeTarget(p);
    Log("  -> 0x%08lx", hr);
    // The mode the game chose decides 2x even when the real mode stays the same (e.g. 1920x1080 at 2x and
    // native 3840x2160 are both a 3840x2160 mode), in which case no ResizeBuffers follows.
    if (SUCCEEDED(hr) && decided) SetScaled(scaled, "ResizeTarget");
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
