// Window/input coordinate mapping for native scaling; see input.cpp.
#pragma once
#include "common.h"

namespace goh4k {

void InstallInputHooks();  // DLL load, when UiScale is on: before the game creates its window

// Converts WM_SIZE for the game's window procedure (it is sent straight to it, so the GetMessage hook never
// sees it) and follows WM_MOVE for the client origin. Once per window.
void SubclassGameWindow(HWND hwnd);
bool IsGameWindow(HWND hwnd);  // the subclassed one
bool ClientCoversMonitor(HWND hwnd, UINT clientW, UINT clientH);  // only then is the client scaled (scale.h)

// Post a WM_SIZE with the current (logical) client size. Needed when the factor changes, and after the game's
// own mode changes (SetFullscreenState, ResizeTarget): when the window size stays the same (borderless ->
// fullscreen at the desktop size) Windows sends no WM_SIZE, and the game lays its UI out from its mode size
// instead. At most one is queued at a time.
void NotifyGameWindowSizeChanged();

}  // namespace goh4k
