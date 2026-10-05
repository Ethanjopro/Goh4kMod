// Native scaling: the game keeps its own resolution and renders natively. Only the client size and mouse
// coordinates it sees are divided by k = real client height / 1080 (input.cpp), so its UI lays out at its
// 1080p size and is drawn magnified onto the native screen. Full-screen viewport rules fix what that
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
    Ratio k;              // cw/ch -> logical: ch / 1080 in lowest terms; 1/1 at 1080p and below or if !covers
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

// Something worth knowing, logged once per kind with the details.
void NoteOnce(const char* kind, const std::string& detail);

}  // namespace goh4k
