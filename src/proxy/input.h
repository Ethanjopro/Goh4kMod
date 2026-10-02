// Window/input coordinate mapping for 2x mode (M3); see input.cpp.
#pragma once
#include "common.h"

namespace goh4k {

void InstallInputHooks();  // DLL load, when Scale > 1: before the game creates its window

// Converts WM_SIZE/WM_MOVE/WM_DISPLAYCHANGE for the game's window procedure: those are sent straight to it,
// so the GetMessage hook never sees them. Once per window.
void SubclassGameWindow(HWND hwnd);

// 2x was switched on or off while the real display mode stayed 3840x2160, so Windows sends no WM_SIZE. The
// game only refreshes its window size (used for mouse mapping) on WM_SIZE, so post one with the new size.
void NotifyGameWindowSizeChanged();

}  // namespace goh4k
