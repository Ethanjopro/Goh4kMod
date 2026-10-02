// NVIDIA HBAO+ (GFSDK_SSAO_D3D11.win64.dll 3.1, the game's "SSAO") in 2x mode.
//
// HBAO+ sizes everything from the depth buffer it is given, which at 2x is the real 3840x2160 one, so it is
// already consistent at real size. What broke it (AO for a quarter of the screen, the rest blended to
// black) were our own rules: its quarter-res 960x540 targets matched the "1/2 of logical" pattern and were
// doubled, and its real 3840x2160 viewports were normalised to the logical 1920x1080. So HBAO+'s own calls
// are recognised by return address and left alone: its textures are never scaled and its viewports are
// taken as real. No HBAO+ code or memory is hooked (an earlier RenderAO hook turned out unnecessary: the
// game passes no input viewport).
#include "hbao.h"
#include "scale.h"

namespace goh4k {

static uintptr_t g_hbaoBase = 0, g_hbaoEnd = 0;

bool FromHbao(void* ret) { return (uintptr_t)ret >= g_hbaoBase && (uintptr_t)ret < g_hbaoEnd; }

void FindHbaoModule() {
    static bool done = false;
    if (done || !Scaling()) return;
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
