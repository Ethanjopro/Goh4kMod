// Goh4kMod d3d11.dll proxy: DLL entry, real-DLL loading, and the two exports we implement.
// Every other export is an ml64 jump stub (gen/exports.asm) through g_exports.
#include "wrappers.h"
#include "gen/exports_gen.h"
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)

using namespace goh4k;

extern "C" void* g_exports[GOH4K_EXPORT_COUNT] = {};
static INIT_ONCE g_resolveOnce = INIT_ONCE_STATIC_INIT;

// Stand-in for an export the system DLL doesn't have; returning E_NOTIMPL is safe for any x64 signature.
static HRESULT WINAPI MissingExport() { return E_NOTIMPL; }

static BOOL CALLBACK LoadReal(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    wcscpy_s(path + n, MAX_PATH - n, L"\\d3d11.dll");
    HMODULE real = LoadLibraryW(path);
    if (!real) Log("FATAL: LoadLibrary(%ls) failed, error %lu", path, GetLastError());
    int missing = 0;
    for (int i = 0; i < GOH4K_EXPORT_COUNT; ++i) {
        void* p = real ? (void*)GetProcAddress(real, kExportNames[i]) : nullptr;
        if (!p) {
            ++missing;
            p = (void*)&MissingExport;
        }
        g_exports[i] = p;
    }
    Log("real d3d11.dll: %ls, %d exports resolved, %d missing", path, GOH4K_EXPORT_COUNT - missing, missing);
    return TRUE;
}

extern "C" void ResolveExports() { InitOnceExecuteOnce(&g_resolveOnce, LoadReal, nullptr, nullptr); }

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

    auto real = (PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN)g_exports[EXP_D3D11CreateDeviceAndSwapChain];
    HRESULT hr = real(pAdapter, DriverType, Software, Flags, pFeatureLevels, FeatureLevels, SDKVersion, pSwapChainDesc,
                      ppSwapChain, ppDevice, pFeatureLevel, ppImmediateContext);
    Log("  -> 0x%08lx, feature level 0x%x", hr, pFeatureLevel ? (unsigned)*pFeatureLevel : 0u);
    if (FAILED(hr) || !fromGame || !g_cfg.wrap) return hr;

    WrappedDevice* device = nullptr;
    if (ppDevice && *ppDevice) {
        device = new WrappedDevice(*ppDevice, ppImmediateContext ? *ppImmediateContext : nullptr);
        *ppDevice = device;
        if (ppImmediateContext && *ppImmediateContext) *ppImmediateContext = device->Immediate();
    }
    if (ppSwapChain && *ppSwapChain) {
        if (device)
            *ppSwapChain = new WrappedSwapChain(*ppSwapChain, device);
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

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        Init(self);
    } else if (reason == DLL_PROCESS_DETACH) {
        Shutdown(reserved != nullptr);
    }
    return TRUE;
}
