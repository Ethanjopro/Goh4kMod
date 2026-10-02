// Window/input coordinate mapping for 2x mode (M3); see input.cpp.
#pragma once
#include "common.h"

namespace goh4k {

void InstallInputHooks();  // DLL load, when Scale > 1: before the game creates its window

// Converts WM_SIZE/WM_MOVE/WM_DISPLAYCHANGE for the game's window procedure: those are sent straight to it,
// so the GetMessage hook never sees them. Once per window.
void SubclassGameWindow(HWND hwnd);

}  // namespace goh4k
