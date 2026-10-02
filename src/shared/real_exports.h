// Shared by the d3d11 and dxgi proxies: loads the System32 original behind the generated export stubs
// (gen/exports.asm jumps through g_exports; its slow path calls ResolveExports).
// Include exactly once per DLL, after its gen/exports_gen.h, and define OnRealLoaded().
#pragma once
#include <windows.h>

extern "C" void* g_exports[GOH4K_EXPORT_COUNT] = {};
static INIT_ONCE g_resolveOnce = INIT_ONCE_STATIC_INIT;

static void OnRealLoaded(const wchar_t* path, HMODULE real, int missing);  // logging, per DLL

// Stand-in for an export the system DLL lacks; returning E_NOTIMPL is safe for any x64 signature.
static HRESULT WINAPI MissingExport() { return E_NOTIMPL; }

static BOOL CALLBACK LoadReal(PINIT_ONCE, PVOID, PVOID*) {
    wchar_t path[MAX_PATH];
    UINT n = GetSystemDirectoryW(path, MAX_PATH);
    swprintf_s(path + n, MAX_PATH - n, L"\\%s", GOH4K_REAL_DLL);
    HMODULE real = LoadLibraryW(path);
    int missing = 0;
    for (int i = 0; i < GOH4K_EXPORT_COUNT; ++i) {
        void* p = real ? (void*)GetProcAddress(real, kExportNames[i]) : nullptr;
        if (!p) {
            ++missing;
            p = (void*)&MissingExport;
        }
        g_exports[i] = p;
    }
    OnRealLoaded(path, real, missing);
    return TRUE;
}

extern "C" void ResolveExports() { InitOnceExecuteOnce(&g_resolveOnce, LoadReal, nullptr, nullptr); }
