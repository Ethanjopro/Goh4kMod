// Diagnostics (M2): captures, one-frame traces, stats and runtime fix toggles, driven by
// captures/request.txt (polled ~4x a second) or the Ctrl+Shift+F11 hotkey. Results are written to captures/
// and each handled request is appended to captures/requests.log.
//
// request.txt, one command per line:
//   capture [tag]              backbuffer as PNG (+ quarter-size preview + .txt sidecar)
//   trace [tag]                every draw/dispatch/clear/copy of the next frame with its state
//   dumprt <event>|ps:<id> [tag] [ds] [cb]  trace the next frame and save render target 0 (or the depth
//                              target; cb also prints the pixel shader constant buffers into the trace)
//                              right after immediate-context event <event>, or after the first draw
//                              whose pixel shader id starts with <id>
//   stats [tag]                textures created so far (by size/format/flags), shaders, fixes
//   fix <name> on|off          runtime toggle read by the fixes (M3/M4)
//   dumpshader <id>            write that shader's bytecode to scratch/shaders/<id>.cso
#pragma once
#include "common.h"
#include <atomic>
#include <string>
#include <vector>

namespace goh4k {

class WrappedContext;
class WrappedDevice;

extern std::atomic<bool> g_traceActive;
inline bool TraceActive() { return g_traceActive.load(std::memory_order_relaxed); }

enum class TraceKind { Plain, Draw, Dispatch };

// Context hooks (only called while a trace is active, except TraceFinish/TraceExecute bookkeeping).
void TraceRecord(WrappedContext* ctx, TraceKind kind, const std::string& call);
void TraceAfterAction(WrappedContext* ctx);  // dumprt check, immediate context only
void TraceFinishCommandList(WrappedContext* ctx, ID3D11CommandList* cl);
void TraceExecuteCommandList(ID3D11CommandList* cl);

// Swapchain hooks: before the real Present (captures, end of a trace) and after it (requests, hotkey).
void DiagBeforePresent(IDXGISwapChain* realSwapChain, WrappedDevice* device, LONG64 frame);
void DiagAfterPresent(IDXGISwapChain* realSwapChain, WrappedDevice* device, LONG64 frame, double fps);

// Registries filled by the device wrapper.
void RegisterShader(ID3D11DeviceChild* shader, const void* bytecode, SIZE_T length, char kind);
std::string ShaderId(ID3D11DeviceChild* shader);  // 16 hex digits, "-" for none
uint64_t ShaderHash(ID3D11DeviceChild* shader);   // the same id as a number; 0 if the shader isn't tagged
void RecordTextureCreate(const D3D11_TEXTURE2D_DESC& d, bool initialData);
std::string ResTag(ID3D11Resource* r);           // e.g. r12:3840x2160:f87:x8 (stable id per resource)
std::string ViewTag(ID3D11View* v);              // ResTag of the view's resource

bool FixOn(const char* name);  // default off

}  // namespace goh4k
