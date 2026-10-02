// Window/input coordinate mapping for 2x mode (M3). The game believes its screen and window are 1920x1080
// while the real ones are 3840x2160, so the user32 calls it uses for the mouse and window geometry are
// hooked (MinHook, user32 only): reads are divided by the scale, writes multiplied. Only calls whose return
// address is inside the game exe are converted; other modules (Steam overlay, drivers) see real values.
// The game's exe and memory are never touched. Raw-input deltas pass through unchanged.
#include "input.h"
#include "MinHook.h"
#include <intrin.h>
#include <windowsx.h>
#pragma intrinsic(_ReturnAddress)

namespace goh4k {

static int S() { return g_cfg.scale; }
static uintptr_t g_exeBase = 0, g_exeEnd = 0;

static bool FromGame(void* ret) { return (uintptr_t)ret >= g_exeBase && (uintptr_t)ret < g_exeEnd; }

static LONG Down(LONG v) { return v >= 0 ? v / S() : -((-v + S() - 1) / S()); }  // floor division
static void Down(POINT* p) { p->x = Down(p->x), p->y = Down(p->y); }
static void Down(RECT* r) { r->left = Down(r->left), r->top = Down(r->top), r->right = Down(r->right), r->bottom = Down(r->bottom); }
static void Up(POINT* p) { p->x *= S(), p->y *= S(); }

static LPARAM DownLParam(LPARAM lp) {
    return MAKELPARAM((WORD)(SHORT)Down(GET_X_LPARAM(lp)), (WORD)(SHORT)Down(GET_Y_LPARAM(lp)));
}

static bool IsMouseMessage(UINT m) {
    return (m >= WM_MOUSEFIRST && m <= WM_MOUSELAST) || (m >= WM_NCMOUSEMOVE && m <= WM_NCXBUTTONDBLCLK) ||
           m == WM_NCMOUSEHOVER || m == WM_NCMOUSELEAVE || m == WM_MOUSEHOVER;
}

// ---------------------------------------------------------------- hooks

static BOOL(WINAPI* Real_GetCursorPos)(LPPOINT);
static BOOL WINAPI Hook_GetCursorPos(LPPOINT p) {
    BOOL r = Real_GetCursorPos(p);
    if (r && p && FromGame(_ReturnAddress())) Down(p);
    return r;
}

static BOOL(WINAPI* Real_SetCursorPos)(int, int);
static BOOL WINAPI Hook_SetCursorPos(int x, int y) {
    if (FromGame(_ReturnAddress())) x *= S(), y *= S();
    return Real_SetCursorPos(x, y);
}

static BOOL(WINAPI* Real_ClipCursor)(const RECT*);
static BOOL WINAPI Hook_ClipCursor(const RECT* r) {
    if (r && FromGame(_ReturnAddress())) {
        RECT p = {r->left * S(), r->top * S(), r->right * S(), r->bottom * S()};
        return Real_ClipCursor(&p);
    }
    return Real_ClipCursor(r);
}

static BOOL(WINAPI* Real_ScreenToClient)(HWND, LPPOINT);
static BOOL WINAPI Hook_ScreenToClient(HWND h, LPPOINT p) {
    if (!p || !FromGame(_ReturnAddress())) return Real_ScreenToClient(h, p);
    Up(p);
    BOOL r = Real_ScreenToClient(h, p);
    Down(p);
    return r;
}

static BOOL(WINAPI* Real_ClientToScreen)(HWND, LPPOINT);
static BOOL WINAPI Hook_ClientToScreen(HWND h, LPPOINT p) {
    if (!p || !FromGame(_ReturnAddress())) return Real_ClientToScreen(h, p);
    Up(p);
    BOOL r = Real_ClientToScreen(h, p);
    Down(p);
    return r;
}

static int(WINAPI* Real_MapWindowPoints)(HWND, HWND, LPPOINT, UINT);
static int WINAPI Hook_MapWindowPoints(HWND from, HWND to, LPPOINT pts, UINT n) {
    if (!pts || !FromGame(_ReturnAddress())) return Real_MapWindowPoints(from, to, pts, n);
    for (UINT i = 0; i < n; ++i) Up(&pts[i]);
    int r = Real_MapWindowPoints(from, to, pts, n);
    for (UINT i = 0; i < n; ++i) Down(&pts[i]);
    return MAKELONG((WORD)(SHORT)Down((SHORT)LOWORD(r)), (WORD)(SHORT)Down((SHORT)HIWORD(r)));
}

static BOOL(WINAPI* Real_GetClientRect)(HWND, LPRECT);
static BOOL WINAPI Hook_GetClientRect(HWND h, LPRECT r) {
    BOOL ok = Real_GetClientRect(h, r);
    if (ok && r && FromGame(_ReturnAddress())) Down(r);
    return ok;
}

static BOOL(WINAPI* Real_GetWindowRect)(HWND, LPRECT);
static BOOL WINAPI Hook_GetWindowRect(HWND h, LPRECT r) {
    BOOL ok = Real_GetWindowRect(h, r);
    if (ok && r && FromGame(_ReturnAddress())) Down(r);
    return ok;
}

static int(WINAPI* Real_GetSystemMetrics)(int);
static int WINAPI Hook_GetSystemMetrics(int i) {
    int v = Real_GetSystemMetrics(i);
    if (!FromGame(_ReturnAddress())) return v;
    switch (i) {
        case SM_CXSCREEN: case SM_CYSCREEN: case SM_CXVIRTUALSCREEN: case SM_CYVIRTUALSCREEN:
        case SM_XVIRTUALSCREEN: case SM_YVIRTUALSCREEN: case SM_CXFULLSCREEN: case SM_CYFULLSCREEN:
        case SM_CXMAXIMIZED: case SM_CYMAXIMIZED: case SM_CXMAXTRACK: case SM_CYMAXTRACK:
            return Down(v);
        default: return v;
    }
}

static BOOL(WINAPI* Real_GetMonitorInfoW)(HMONITOR, LPMONITORINFO);
static BOOL WINAPI Hook_GetMonitorInfoW(HMONITOR m, LPMONITORINFO mi) {
    BOOL ok = Real_GetMonitorInfoW(m, mi);
    if (ok && mi && FromGame(_ReturnAddress())) Down(&mi->rcMonitor), Down(&mi->rcWork);
    return ok;
}

static BOOL(WINAPI* Real_EnumDisplaySettingsW)(LPCWSTR, DWORD, DEVMODEW*);
static BOOL WINAPI Hook_EnumDisplaySettingsW(LPCWSTR dev, DWORD mode, DEVMODEW* dm) {
    BOOL ok = Real_EnumDisplaySettingsW(dev, mode, dm);
    if (ok && dm && (mode == ENUM_CURRENT_SETTINGS || mode == ENUM_REGISTRY_SETTINGS) && FromGame(_ReturnAddress())) {
        dm->dmPelsWidth = (DWORD)Down((LONG)dm->dmPelsWidth);
        dm->dmPelsHeight = (DWORD)Down((LONG)dm->dmPelsHeight);
    }
    return ok;
}

static BOOL(WINAPI* Real_SetWindowPos)(HWND, HWND, int, int, int, int, UINT);
static BOOL WINAPI Hook_SetWindowPos(HWND h, HWND after, int x, int y, int cx, int cy, UINT flags) {
    if (FromGame(_ReturnAddress())) {
        if (!(flags & SWP_NOMOVE)) x *= S(), y *= S();
        if (!(flags & SWP_NOSIZE)) cx *= S(), cy *= S();
    }
    return Real_SetWindowPos(h, after, x, y, cx, cy, flags);
}

static void DownMessage(MSG* m) {
    Down(&m->pt);
    if (IsMouseMessage(m->message)) m->lParam = DownLParam(m->lParam);
}

static BOOL(WINAPI* Real_GetMessageW)(LPMSG, HWND, UINT, UINT);
static BOOL WINAPI Hook_GetMessageW(LPMSG m, HWND h, UINT lo, UINT hi) {
    BOOL r = Real_GetMessageW(m, h, lo, hi);
    if (r > 0 && m && FromGame(_ReturnAddress())) DownMessage(m);
    return r;
}

static BOOL(WINAPI* Real_PeekMessageW)(LPMSG, HWND, UINT, UINT, UINT);
static BOOL WINAPI Hook_PeekMessageW(LPMSG m, HWND h, UINT lo, UINT hi, UINT rm) {
    BOOL r = Real_PeekMessageW(m, h, lo, hi, rm);
    if (r && m && FromGame(_ReturnAddress())) DownMessage(m);
    return r;
}

// ---------------------------------------------------------------- window procedure

static HWND g_subclassed = nullptr;
static WNDPROC g_gameProc = nullptr;

static LRESULT CALLBACK SubclassProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_SIZE: case WM_MOVE: l = DownLParam(l); break;
        case WM_DISPLAYCHANGE: l = MAKELPARAM(LOWORD(l) / S(), HIWORD(l) / S()); break;
        default: break;
    }
    return CallWindowProcW(g_gameProc, h, m, w, l);
}

void SubclassGameWindow(HWND hwnd) {
    if (!hwnd || hwnd == g_subclassed) return;
    if (g_subclassed) {
        Log("input: a second game window (%p); only %p is subclassed", hwnd, g_subclassed);
        return;
    }
    g_gameProc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)&SubclassProc);
    g_subclassed = g_gameProc ? hwnd : nullptr;
    Log("input: game window %p subclassed for WM_SIZE/WM_MOVE/WM_DISPLAYCHANGE (%s)", hwnd, g_gameProc ? "ok" : "failed");
}

// ---------------------------------------------------------------- install

template <typename T>
static int Hook(const char* name, T* hook, T** real) {
    MH_STATUS s = MH_CreateHookApi(L"user32", name, (LPVOID)hook, (LPVOID*)real);
    if (s != MH_OK) Log("input hook %s: MH_CreateHookApi failed (%s)", name, MH_StatusToString(s));
    return s == MH_OK;
}

void InstallInputHooks() {
    HMODULE exe = GetModuleHandleW(nullptr);
    auto dos = (const IMAGE_DOS_HEADER*)exe;
    auto nt = (const IMAGE_NT_HEADERS*)((const uint8_t*)exe + dos->e_lfanew);
    g_exeBase = (uintptr_t)exe;
    g_exeEnd = g_exeBase + nt->OptionalHeader.SizeOfImage;

    MH_STATUS s = MH_Initialize();
    if (s != MH_OK) {
        Log("input hooks: MH_Initialize failed (%s)", MH_StatusToString(s));
        return;
    }
    int n = 0;
    n += Hook("GetCursorPos", &Hook_GetCursorPos, &Real_GetCursorPos);
    n += Hook("SetCursorPos", &Hook_SetCursorPos, &Real_SetCursorPos);
    n += Hook("ClipCursor", &Hook_ClipCursor, &Real_ClipCursor);
    n += Hook("ScreenToClient", &Hook_ScreenToClient, &Real_ScreenToClient);
    n += Hook("ClientToScreen", &Hook_ClientToScreen, &Real_ClientToScreen);
    n += Hook("MapWindowPoints", &Hook_MapWindowPoints, &Real_MapWindowPoints);
    n += Hook("GetClientRect", &Hook_GetClientRect, &Real_GetClientRect);
    n += Hook("GetWindowRect", &Hook_GetWindowRect, &Real_GetWindowRect);
    n += Hook("GetSystemMetrics", &Hook_GetSystemMetrics, &Real_GetSystemMetrics);
    n += Hook("GetMonitorInfoW", &Hook_GetMonitorInfoW, &Real_GetMonitorInfoW);
    n += Hook("EnumDisplaySettingsW", &Hook_EnumDisplaySettingsW, &Real_EnumDisplaySettingsW);
    n += Hook("SetWindowPos", &Hook_SetWindowPos, &Real_SetWindowPos);
    n += Hook("GetMessageW", &Hook_GetMessageW, &Real_GetMessageW);
    n += Hook("PeekMessageW", &Hook_PeekMessageW, &Real_PeekMessageW);
    s = MH_EnableHook(MH_ALL_HOOKS);
    Log("input hooks: %d of 14 user32 functions hooked, enable %s; game exe %p-%p", n, MH_StatusToString(s),
        (void*)g_exeBase, (void*)g_exeEnd);
}

}  // namespace goh4k
