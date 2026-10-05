// Goh4kMod d3d11.dll proxy: shared declarations (config, logging, small helpers).
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <string>

// Answered only by our WrappedDevice's QueryInterface (returns the wrapper itself), so code that is handed
// an ID3D11Device can tell whether it is ours.
static const GUID IID_Goh4kWrappedDevice = {0x7d1b6c9e, 0x3f0a, 0x4c55, {0x9a, 0x1e, 0x6b, 0x2f, 0x8d, 0x4c, 0x0a, 0x17}};

namespace goh4k {

// Settings, [proxy] section of goh4k.ini. Read once at DLL load (restart the game after editing).
//   dev build:     <project>\config\goh4k.ini; logs in <project>\scratch\logs, diagnostics in <project>\captures
//   release build (GOH4K_RELEASE, the shared zip): goh4k.ini next to the DLL; goh4k.log next to the DLL;
//                  diagnostics (if enabled) in goh4k-diagnostics\ next to the DLL
struct Config {
    int wrap = 1;         // 0: forward exports and log device creation only; 1: wrap the game's device, contexts, swapchain
    int uiScale = 1;      // 1: the UI keeps its 1080p size at the game's own resolution (scale.h); 0: pass-through
#ifdef GOH4K_RELEASE
    int diagnostics = 0;  // 1: request.txt / hotkey / status.txt, shader and texture registries
#else
    int diagnostics = 1;
#endif
    int statusMs = 1000;  // how often Present rewrites status.txt
};
extern Config g_cfg;

void Init(HMODULE self);                // DllMain(PROCESS_ATTACH): kernel32 calls only
void Shutdown(bool processExit);        // DllMain(PROCESS_DETACH)
const std::wstring& CapturesDir();      // diagnostics output (captures, traces, status.txt, request.txt)
const std::wstring& ShaderDumpDir();    // dumpshader output
void Log(const char* fmt, ...);
void LogIidOnce(const char* who, REFIID riid);   // first QueryInterface of an IID we pass through
void WriteTextFile(const std::wstring& path, const std::string& text);

std::string Fmt(const char* fmt, ...);
std::string GuidStr(REFGUID g);
std::string ModuleNameOf(const void* addr);
bool IsMainModule(const void* addr);           // is this code address inside the process's .exe?
std::string DescribeSwapChainDesc(const DXGI_SWAP_CHAIN_DESC& d);
std::string DescribeWindow(HWND hwnd);           // client/window rect, DPI awareness, monitor, metrics

extern volatile LONG64 g_totalFrames;

}  // namespace goh4k
