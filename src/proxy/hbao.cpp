// NVIDIA HBAO+ (GFSDK_SSAO_D3D11.win64.dll 3.1, the game's "SSAO").
//
// HBAO+ sizes everything from the depth buffer it is given and sets its own viewports, so it is consistent
// at the real resolution by itself. Its calls are recognised by return address and the full-screen viewport
// rules (context.cpp) never touch them; 1.x learnt the hard way that rewriting its viewports breaks AO. No
// HBAO+ code or memory is hooked.
#include "hbao.h"
#include "scale.h"

namespace goh4k {

static uintptr_t g_hbaoBase = 0, g_hbaoEnd = 0;

bool FromHbao(void* ret) { return (uintptr_t)ret >= g_hbaoBase && (uintptr_t)ret < g_hbaoEnd; }

void FindHbaoModule() {
    static bool done = false;
    if (done || !UiScaleConfigured()) return;
    done = true;
    HMODULE m = GetModuleHandleW(L"GFSDK_SSAO_D3D11.win64.dll");
    if (!m) return Log("hbao: GFSDK_SSAO_D3D11.win64.dll not loaded");
    auto dos = (const IMAGE_DOS_HEADER*)m;
    auto nt = (const IMAGE_NT_HEADERS*)((const uint8_t*)m + dos->e_lfanew);
    g_hbaoBase = (uintptr_t)m;
    g_hbaoEnd = g_hbaoBase + nt->OptionalHeader.SizeOfImage;
    Log("hbao: module %p-%p; its textures and viewports stay real-sized", (void*)g_hbaoBase, (void*)g_hbaoEnd);
}

}  // namespace goh4k
