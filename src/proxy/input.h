// Window/input coordinate mapping for native scaling; see input.cpp.
#pragma once
#include "common.h"

namespace goh4k {

// DLL load, when UiScale is on: before the game creates its window. All or nothing: if any hook can't be
// made, none stays and UiScale is switched off for the session.
void InstallInputHooks();
int InputHooksInstalled();    // how many are in place (0 if they failed)
int InputHooksPatchedOver();  // how many of them another program has patched over since (CheckGameWindow)

// Every couple of seconds from the game window's Present: puts our window procedure back if the game or
// another program replaced it, and logs hooks patched over by another program (or switches the mod off if
// one of ours was removed).
void CheckGameWindow();

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
// From the game window's Present: the resync a window reconfiguration still waits for (250 ms on), or one for a
// UI that stays projected at the real size (input.cpp).
void ResyncIfLaidOutReal();

}  // namespace goh4k
