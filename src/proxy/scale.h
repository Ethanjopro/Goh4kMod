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
Ratio UiScaleRatio();  // from the client size; 1/1 at 1080p and below
inline bool UiScaleActive() { return UiScaleConfigured() && !(UiScaleRatio() == Ratio{}); }
extern std::atomic<UINT> g_uiRealW, g_uiRealH;  // the window's real client size, as last set below

// From the window's real client size (WM_SIZE, or GetClientRect at swapchain create/resize), not the buffer
// size: after a mode change the buffers stay stale until the game resizes them, which it only does once it
// has seen a WM_SIZE that matches its new mode. notify: queue a WM_SIZE resync when the factor changes.
void SetUiScaleFromClient(UINT realW, UINT realH, const char* why, bool notify = true);

// Screen-sized textures the UI draws as images (the blurred backdrop behind prompts). The UI samples them
// with coordinates of logical / texture size, so whatever renders into them has to fill the logical part.
void MarkUiScreenCopy(ID3D11Resource* r);
bool IsUiScreenCopy(ID3D11Resource* r);

// Something worth knowing, logged once per kind with the details.
void NoteOnce(const char* kind, const std::string& detail);

}  // namespace goh4k
