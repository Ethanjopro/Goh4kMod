// Goh4kMod dxgi.dll proxy. Forwards every dxgi export to System32\dxgi.dll. When the game exe itself calls
// CreateDXGIFactory/1/2, it gets a wrapped factory whose CreateSwapChain/CreateSwapChainForHwnd are handed to
// our d3d11.dll proxy (Goh4k_* exports), which wraps the swapchain. Logging, config and all wrapping logic
// live in the d3d11 proxy; without it this DLL is a pure pass-through.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <intrin.h>
#include <cstdarg>
#include <cstdio>
#include "gen/exports_gen.h"
#include "../shared/real_exports.h"
#include "gen/fwd_factory.h"
#pragma intrinsic(_ReturnAddress)

typedef HRESULT(WINAPI* PFN_Goh4kCreateSwapChain)(IDXGIFactory*, IUnknown*, DXGI_SWAP_CHAIN_DESC*, IDXGISwapChain**);
typedef HRESULT(WINAPI* PFN_Goh4kCreateSwapChainForHwnd)(IDXGIFactory2*, IUnknown*, HWND, const DXGI_SWAP_CHAIN_DESC1*,
                                                         const DXGI_SWAP_CHAIN_FULLSCREEN_DESC*, IDXGIOutput*,
                                                         IDXGISwapChain1**);
typedef void(WINAPI* PFN_Goh4kLogLine)(const char*);

static HMODULE g_self;
static PFN_Goh4kCreateSwapChain g_createSwapChain;
static PFN_Goh4kCreateSwapChainForHwnd g_createSwapChainForHwnd;
static PFN_Goh4kLogLine g_logLine;
static INIT_ONCE g_findOnce = INIT_ONCE_STATIC_INIT;

static void Log(const char* fmt, ...) {
    if (!g_logLine) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    g_logLine(buf);
}

static void OnRealLoaded(const wchar_t* path, HMODULE real, int missing) {
    Log("real dxgi.dll: %ls, %s, %d exports missing", path, real ? "loaded" : "FAILED TO LOAD", missing);
}

// Our d3d11 proxy sits next to us (same folder). Matching by full path avoids picking up System32's d3d11.dll,
// which has the same base name.
static BOOL CALLBACK FindD3D11Proxy(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(g_self, path, MAX_PATH);
    wchar_t* slash = wcsrchr(path, L'\\');
    if (!slash) return TRUE;
    wcscpy_s(slash + 1, MAX_PATH - (slash + 1 - path), L"d3d11.dll");
    HMODULE d3d11 = GetModuleHandleW(path);
    if (!d3d11) return TRUE;
    g_logLine = (PFN_Goh4kLogLine)GetProcAddress(d3d11, "Goh4k_LogLine");
    g_createSwapChain = (PFN_Goh4kCreateSwapChain)GetProcAddress(d3d11, "Goh4k_CreateSwapChain");
    g_createSwapChainForHwnd = (PFN_Goh4kCreateSwapChainForHwnd)GetProcAddress(d3d11, "Goh4k_CreateSwapChainForHwnd");
    Log("dxgi proxy: found the d3d11 proxy, swapchain hand-off %s", g_createSwapChain ? "available" : "MISSING");
    return TRUE;
}

static bool IsMainModule(const void* addr) {
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)addr, &mod))
        return false;
    return mod == GetModuleHandleW(nullptr);
}

// ---------------------------------------------------------------- WrappedFactory

static const IID* const kFactoryIds[] = {&__uuidof(IDXGIFactory1), &__uuidof(IDXGIFactory2), &__uuidof(IDXGIFactory3),
                                         &__uuidof(IDXGIFactory4), &__uuidof(IDXGIFactory5), &__uuidof(IDXGIFactory6),
                                         &__uuidof(IDXGIFactory7)};

class WrappedFactory final : public Fwd_IDXGIFactory7 {
public:
    explicit WrappedFactory(IUnknown* real) {
        m_real = reinterpret_cast<IDXGIFactory7*>(real);
        for (int i = 6; i >= 0; --i) {
            IUnknown* p = nullptr;
            if (SUCCEEDED(real->QueryInterface(*kFactoryIds[i], (void**)&p))) {
                m_level = i + 1;
                m_real = reinterpret_cast<IDXGIFactory7*>(p);
                p->Release();
                break;
            }
        }
    }
    int Level() const { return m_level; }

    // Returns the interface id's level (0 = IDXGIFactory and below), or -1 for anything else.
    static int LevelOf(REFIID riid) {
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IDXGIObject) || riid == __uuidof(IDXGIFactory)) return 0;
        for (int i = 0; i < 7; ++i)
            if (riid == *kFactoryIds[i]) return i + 1;
        return -1;
    }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (!ppv) return E_POINTER;
        int need = LevelOf(riid);
        if (need >= 0) {
            if (need > m_level) {
                *ppv = nullptr;
                return E_NOINTERFACE;
            }
            AddRef();
            *ppv = static_cast<IDXGIFactory7*>(this);
            return S_OK;
        }
        return m_real->QueryInterface(riid, ppv);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return m_real->AddRef(); }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = m_real->Release();
        if (r == 0) delete this;
        return r;
    }

    HRESULT STDMETHODCALLTYPE CreateSwapChain(IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc,
                                              IDXGISwapChain** ppSwapChain) override {
        return g_createSwapChain(m_real, pDevice, pDesc, ppSwapChain);
    }
    HRESULT STDMETHODCALLTYPE CreateSwapChainForHwnd(IUnknown* pDevice, HWND hWnd, const DXGI_SWAP_CHAIN_DESC1* pDesc,
                                                     const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                                                     IDXGIOutput* pRestrictToOutput,
                                                     IDXGISwapChain1** ppSwapChain) override {
        return g_createSwapChainForHwnd(m_real, pDevice, hWnd, pDesc, pFullscreenDesc, pRestrictToOutput, ppSwapChain);
    }
    HRESULT STDMETHODCALLTYPE MakeWindowAssociation(HWND WindowHandle, UINT Flags) override {
        HRESULT hr = m_real->MakeWindowAssociation(WindowHandle, Flags);
        Log("IDXGIFactory::MakeWindowAssociation(%p, 0x%x) -> 0x%08lx", WindowHandle, Flags, hr);
        return hr;
    }

private:
    int m_level = 0;
};

// Wraps a factory the game exe just created, if our d3d11 proxy is there to take the swapchains.
static HRESULT MaybeWrap(HRESULT hr, REFIID riid, void** ppFactory, void* caller, const char* fn) {
    if (FAILED(hr) || !ppFactory || !*ppFactory || !IsMainModule(caller)) return hr;
    InitOnceExecuteOnce(&g_findOnce, FindD3D11Proxy, nullptr, nullptr);
    if (!g_createSwapChain || !g_createSwapChainForHwnd || WrappedFactory::LevelOf(riid) < 0) {
        Log("%s from the game: factory left unwrapped", fn);
        return hr;
    }
    auto* w = new WrappedFactory(static_cast<IUnknown*>(*ppFactory));  // takes over the caller's reference
    *ppFactory = static_cast<IDXGIFactory7*>(w);
    Log("%s from the game: factory wrapped (IDXGIFactory%d)", fn, w->Level());
    return hr;
}

extern "C" HRESULT WINAPI Proxy_CreateDXGIFactory(REFIID riid, void** ppFactory) {
    void* caller = _ReturnAddress();
    ResolveExports();
    auto real = (HRESULT(WINAPI*)(REFIID, void**))g_exports[EXP_CreateDXGIFactory];
    return MaybeWrap(real(riid, ppFactory), riid, ppFactory, caller, "CreateDXGIFactory");
}

extern "C" HRESULT WINAPI Proxy_CreateDXGIFactory1(REFIID riid, void** ppFactory) {
    void* caller = _ReturnAddress();
    ResolveExports();
    auto real = (HRESULT(WINAPI*)(REFIID, void**))g_exports[EXP_CreateDXGIFactory1];
    return MaybeWrap(real(riid, ppFactory), riid, ppFactory, caller, "CreateDXGIFactory1");
}

extern "C" HRESULT WINAPI Proxy_CreateDXGIFactory2(UINT Flags, REFIID riid, void** ppFactory) {
    void* caller = _ReturnAddress();
    ResolveExports();
    auto real = (HRESULT(WINAPI*)(UINT, REFIID, void**))g_exports[EXP_CreateDXGIFactory2];
    return MaybeWrap(real(Flags, riid, ppFactory), riid, ppFactory, caller, "CreateDXGIFactory2");
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = self;
        DisableThreadLibraryCalls(self);
    }
    return TRUE;
}
