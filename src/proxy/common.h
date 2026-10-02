// Goh4kMod d3d11.dll proxy: shared declarations (config, logging, small helpers).
#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <string>

namespace goh4k {

// config/goh4k.ini, [proxy] section. Read once at DLL load; changing it needs a game restart, not a redeploy.
struct Config {
    int wrap = 1;        // 0: forward exports and log device creation only; 1: wrap the game's device, contexts, swapchain
    int scale = 1;       // 1: pass-through. 2x rendering arrives in M3.
    int statusMs = 1000; // how often Present rewrites captures/status.txt
};
extern Config g_cfg;

void Init(HMODULE self);                // DllMain(PROCESS_ATTACH): kernel32 calls only
void Shutdown(bool processExit);        // DllMain(PROCESS_DETACH)
const std::wstring& ProjectDir();
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
