// Native scaling: the game keeps its own resolution and renders natively. Only the client size and mouse
// coordinates it sees are divided by k (input.cpp): real height / 1080 on 16:9 and wider screens, real width
// / 1920 on narrower ones (ratio.h RatioForScreen), so its UI lays out at its 1080p size and is drawn
// magnified onto the native screen. Full-screen viewport rules fix what that
// mismatch breaks (context.cpp). Window position, monitor and display-mode queries stay real, so the game's
// resolution logic, its Options and options.set see no change.
#pragma once
#include "common.h"
#include "ratio.h"
#include <atomic>

namespace goh4k {

inline bool UiScaleConfigured() { return g_cfg.uiScale != 0; }

// One consistent picture of the window and the swapchain, read from a single atomic.
//
// Only a client area that covers its whole monitor (fullscreen, or borderless at the desktop size) is scaled.
// In a smaller window the game takes the window's client size as its resolution and saves it to options.set
// (custom_mode / mode), so telling it a logical size there would change the player's setting; such windows
// get k = 1, a pure pass-through, like the stock game.
struct UiState {
    UINT cw = 0, ch = 0;  // the game window's real client size (WM_SIZE, GetClientRect)
    UINT bw = 0, bh = 0;  // its swapchain's buffer size: what the game renders at, the rules' "real screen"
    bool covers = false;  // the client area is its whole monitor
    Ratio k;              // cw/ch -> logical (RatioForScreen); 1/1 at 1080p-class sizes and below, or if !covers
    UINT lw = 0, lh = 0;  // the logical client size the game is told
    bool active = false;  // k > 1: the mod is changing something
};
UiState UiSnapshot();
Ratio UiScaleRatio();                                     // UiSnapshot().k, cheaper
inline bool UiScaleActive() { return UiScaleConfigured() && !(UiScaleRatio() == Ratio{}); }
uint32_t UiStateGeneration();                             // changes whenever the state does

// The real client origin in screen coordinates. Screen-space points are scaled around it (input.cpp), so the
// window's position reads the same whether the game asks a converted or an unconverted API.
POINT UiClientOrigin();
void SetUiClientOrigin(LONG x, LONG y);

// From the window's real client size (WM_SIZE, or GetClientRect at swapchain create/resize), not the buffer
// size: after a mode change the buffers stay stale until the game resizes them, which it only does once it
// has seen a WM_SIZE that matches its new mode. notify: queue a WM_SIZE resync when the factor changes.
void SetUiClient(UINT realW, UINT realH, bool coversMonitor, const char* why, bool notify = true);
void SetUiBuffer(UINT w, UINT h);  // the game window's swapchain, after create/resize
void ClearUiClient(const char* why);  // no game window any more: k = 1 until the next one
// Something the mod relies on broke mid-session: k = 1 and UiScale off from now on (the same switch a failed
// hook install uses), logged as a warning. False if it was off.
// The caller resyncs the window (input.h NotifyGameWindowSizeChanged).
bool UiScaleShutOff(const char* why);

// What the mod did this session, summed up in the log at unload (LogUiSessionSummary).
struct UiCounters {
    std::atomic<long long> gameFrames{0};       // presented by the game window's swapchain (rule 3 counts per frame)
    std::atomic<long long> framesScaled{0};     // ... of them with k > 1
    std::atomic<long long> labelViewports{0};   // rule 1 applications
    std::atomic<long long> screenViewports{0};  // rule 2 applications
    std::atomic<long long> imageDraws{0};       // UI draws of a screen-sized image (rule 3 candidates)
    std::atomic<long long> standInDraws{0};     // ... of them given the stand-in
    std::atomic<long long> standInsMade{0};
    std::atomic<int> uiShaders{0};              // UI vertex shaders recognised
};
UiCounters& Counters();
// Adds one for the session summary; nothing in the release build, which has no log. (gameFrames isn't only for
// the log: it is incremented directly.)
template <typename T>
inline void Count(std::atomic<T>& c) {
    if constexpr (kLogging) c.fetch_add(1, std::memory_order_relaxed);
}
void LogUiSessionSummary();

}  // namespace goh4k
