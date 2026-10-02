// Goh4kMod d3d11.dll proxy: DLL entry, the d3d11 exports we implement, and the entry points our dxgi.dll
// proxy calls. Every other export is an ml64 jump stub (gen/exports.asm) through g_exports.
#include "wrappers.h"
#include "hbao.h"
#include "input.h"
#include "scale.h"
#include "gen/exports_gen.h"
#include "../shared/real_exports.h"
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)

using namespace goh4k;

static void OnRealLoaded(const wchar_t* path, HMODULE real, int missing) {
    if (!real) Log("FATAL: LoadLibrary(%ls) failed, error %lu", path, GetLastError());
    Log("real d3d11.dll: %ls, %d exports resolved, %d missing", path, GOH4K_EXPORT_COUNT - missing, missing);
}

static const char* DriverName(D3D_DRIVER_TYPE t) {
    switch (t) {
        case D3D_DRIVER_TYPE_UNKNOWN: return "unknown(adapter)";
        case D3D_DRIVER_TYPE_HARDWARE: return "hardware";
        case D3D_DRIVER_TYPE_WARP: return "warp";
        case D3D_DRIVER_TYPE_REFERENCE: return "reference";
        default: return "other";
    }
}

static std::string Levels(const D3D_FEATURE_LEVEL* p, UINT n) {
    if (!p) return "default";
    std::string s;
    for (UINT i = 0; i < n; ++i) s += Fmt("%s0x%x", i ? "," : "", (unsigned)p[i]);
    return s;
}

extern "C" HRESULT WINAPI Proxy_D3D11CreateDeviceAndSwapChain(
    IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software, UINT Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels, UINT SDKVersion,
    const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc, IDXGISwapChain** ppSwapChain, ID3D11Device** ppDevice,
    D3D_FEATURE_LEVEL* pFeatureLevel, ID3D11DeviceContext** ppImmediateContext) {
    void* caller = _ReturnAddress();
    ResolveExports();
    bool fromGame = IsMainModule(caller);
    Log("D3D11CreateDeviceAndSwapChain from %s: adapter=%p driver=%s flags=0x%x levels=%s", ModuleNameOf(caller).c_str(),
        pAdapter, DriverName(DriverType), Flags, Levels(pFeatureLevels, FeatureLevels).c_str());
    if (pSwapChainDesc) Log("  requested swapchain: %s", DescribeSwapChainDesc(*pSwapChainDesc).c_str());

    DXGI_SWAP_CHAIN_DESC scaledDesc;
    bool scaled = false;
    if (pSwapChainDesc && fromGame && g_cfg.wrap) {
        scaledDesc = *pSwapChainDesc;
        scaled = ScaleRequest(scaledDesc.OutputWindow, &scaledDesc.BufferDesc.Width, &scaledDesc.BufferDesc.Height);
        if (scaled) pSwapChainDesc = &scaledDesc;
    }
    auto real = (PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN)g_exports[EXP_D3D11CreateDeviceAndSwapChain];
    HRESULT hr = real(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels, SDKVersion, pSwapChainDesc,
                      ppSwapChain, ppDevice, pFeatureLevel, ppImmediateContext);
    Log("  -> 0x%08lx, feature level 0x%x", hr, pFeatureLevel ? (unsigned)*pFeatureLevel : 0u);
    if (FAILED(hr) || !fromGame || !g_cfg.wrap) return hr;

    FindHbaoModule();  // 2x mode only; HBAO+ is a static import of the game, loaded by now
    WrappedDevice* device = nullptr;
    if (ppDevice && *ppDevice) {
        device = new WrappedDevice(*ppDevice, ppImmediateContext ? *ppImmediateContext : nullptr);
        *ppDevice = device;
        if (ppImmediateContext && *ppImmediateContext) *ppImmediateContext = device->Immediate();
    }
    if (ppSwapChain && *ppSwapChain) {
        if (device)
            *ppSwapChain = new WrappedSwapChain(*ppSwapChain, device, scaled);
        else
            Log("  swapchain left unwrapped: the caller didn't ask for the device");
    }
    return hr;
}

extern "C" HRESULT WINAPI Proxy_D3D11CreateDevice(IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType, HMODULE Software,
                                                  UINT Flags, const D3D_FEATURE_LEVEL* pFeatureLevels,
                                                  UINT FeatureLevels, UINT SDKVersion, ID3D11Device** ppDevice,
                                                  D3D_FEATURE_LEVEL* pFeatureLevel,
                                                  ID3D11DeviceContext** ppImmediateContext) {
    void* caller = _ReturnAddress();
    ResolveExports();
    bool fromGame = IsMainModule(caller);
    Log("D3D11CreateDevice from %s: adapter=%p driver=%s flags=0x%x levels=%s", ModuleNameOf(caller).c_str(), pAdapter,
        DriverName(DriverType), Flags, Levels(pFeatureLevels, FeatureLevels).c_str());

    auto real = (PFN_D3D11_CREATE_DEVICE)g_exports[EXP_D3D11CreateDevice];
    HRESULT hr = real(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels, SDKVersion, ppDevice,
                      pFeatureLevel, ppImmediateContext);
    Log("  -> 0x%08lx, feature level 0x%x", hr, pFeatureLevel ? (unsigned)*pFeatureLevel : 0u);
    if (FAILED(hr) || !fromGame || !g_cfg.wrap) return hr;

    if (ppDevice && *ppDevice) {
        WrappedDevice* device = new WrappedDevice(*ppDevice, ppImmediateContext ? *ppImmediateContext : nullptr);
        *ppDevice = device;
        if (ppImmediateContext && *ppImmediateContext) *ppImmediateContext = device->Immediate();
    }
    return hr;
}

// ---------------------------------------------------------------- called by our dxgi.dll proxy
// The game creates its real swapchain through IDXGIFactory::CreateSwapChain, not through
// D3D11CreateDeviceAndSwapChain. Our dxgi proxy wraps the factory and hands those calls here, so the
// swapchain gets wrapped with the device wrapper it belongs to. DXGI itself is given the real device.

static WrappedDevice* AsWrappedDevice(IUnknown* p) {  // AddRef'd, or null if it isn't ours
    WrappedDevice* wd = nullptr;
    if (p && SUCCEEDED(p->QueryInterface(IID_Goh4kWrappedDevice, (void**)&wd))) return wd;
    return nullptr;
}

extern "C" HRESULT WINAPI Goh4k_CreateSwapChain(IDXGIFactory* factory, IUnknown* pDevice, DXGI_SWAP_CHAIN_DESC* pDesc,
                                                IDXGISwapChain** ppSwapChain) {
    WrappedDevice* wd = AsWrappedDevice(pDevice);
    Log("IDXGIFactory::CreateSwapChain, %s device", wd ? "our wrapped" : "a foreign");
    if (pDesc) Log("  requested swapchain: %s", DescribeSwapChainDesc(*pDesc).c_str());
    DXGI_SWAP_CHAIN_DESC scaledDesc;
    bool scaled = false;
    if (pDesc && wd && g_cfg.wrap) {
        scaledDesc = *pDesc;
        scaled = ScaleRequest(scaledDesc.OutputWindow, &scaledDesc.BufferDesc.Width, &scaledDesc.BufferDesc.Height);
        if (scaled) pDesc = &scaledDesc;
    }
    HRESULT hr = factory->CreateSwapChain(wd ? (IUnknown*)wd->Real() : pDevice, pDesc, ppSwapChain);
    Log("  -> 0x%08lx%s", hr, scaled ? Fmt(" (real %ux%u)", pDesc->BufferDesc.Width, pDesc->BufferDesc.Height).c_str() : "");
    if (SUCCEEDED(hr) && wd && g_cfg.wrap && ppSwapChain && *ppSwapChain)
        *ppSwapChain = new WrappedSwapChain(*ppSwapChain, wd, scaled);
    if (wd) wd->Release();
    return hr;
}

extern "C" HRESULT WINAPI Goh4k_CreateSwapChainForHwnd(IDXGIFactory2* factory, IUnknown* pDevice, HWND hWnd,
                                                       const DXGI_SWAP_CHAIN_DESC1* pDesc,
                                                       const DXGI_SWAP_CHAIN_FULLSCREEN_DESC* pFullscreenDesc,
                                                       IDXGIOutput* pRestrictToOutput, IDXGISwapChain1** ppSwapChain) {
    WrappedDevice* wd = AsWrappedDevice(pDevice);
    Log("IDXGIFactory2::CreateSwapChainForHwnd, %s device", wd ? "our wrapped" : "a foreign");
    if (pDesc)
        Log("  requested: %ux%u fmt=%d msaa=%u buffers=%u scaling=%d effect=%d flags=0x%x windowed=%d", pDesc->Width,
            pDesc->Height, (int)pDesc->Format, pDesc->SampleDesc.Count, pDesc->BufferCount, (int)pDesc->Scaling,
            (int)pDesc->SwapEffect, pDesc->Flags, pFullscreenDesc ? pFullscreenDesc->Windowed : 1);
    DXGI_SWAP_CHAIN_DESC1 scaledDesc;
    bool scaled = false;
    if (pDesc && wd && g_cfg.wrap) {
        scaledDesc = *pDesc;
        scaled = ScaleRequest(hWnd, &scaledDesc.Width, &scaledDesc.Height);
        if (scaled) pDesc = &scaledDesc;
    }
    HRESULT hr = factory->CreateSwapChainForHwnd(wd ? (IUnknown*)wd->Real() : pDevice, hWnd, pDesc, pFullscreenDesc,
                                                 pRestrictToOutput, ppSwapChain);
    Log("  -> 0x%08lx", hr);
    if (SUCCEEDED(hr) && wd && g_cfg.wrap && ppSwapChain && *ppSwapChain)
        *ppSwapChain = new WrappedSwapChain(*ppSwapChain, wd, scaled);
    if (wd) wd->Release();
    return hr;
}

extern "C" void WINAPI Goh4k_LogLine(const char* line) { Log("%s", line); }

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        Init(self);
        if (Scaling()) {
            // Real pixels everywhere, so the 2x mapping is exact whatever Windows' display scaling is.
            BOOL dpi = SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
            Log("2x mode: per-monitor-v2 DPI awareness %s (error %lu)", dpi ? "set" : "not set", dpi ? 0 : GetLastError());
            InstallInputHooks();
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        Shutdown(reserved != nullptr);
    }
    return TRUE;
}
