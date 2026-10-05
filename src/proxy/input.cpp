// Window/input coordinate mapping for native scaling (scale.h). The game is told its client area is the
// real one divided by k (1920x1080 at 4K), so the user32 calls it uses for the client size and the mouse
// are hooked (MinHook, user32 only): reads are divided by k, writes multiplied, with rational rounding
// (ratio.h). Window position, monitor and display-mode calls are not hooked at all, so the game's
// resolution logic sees the real values. Only calls whose return address is inside the game exe are
// converted; other modules (Steam overlay, drivers) see real values. The game's exe and memory are never
// touched. Raw-input deltas pass through unchanged.
#include "input.h"
#include "diag.h"
#include "scale.h"
#include "MinHook.h"
#include <algorithm>
#include <cmath>
#include <intrin.h>
#include <vector>
#include <windowsx.h>
#pragma intrinsic(_ReturnAddress)

namespace goh4k {

static uintptr_t g_exeBase = 0, g_exeEnd = 0;

static bool FromGame(void* ret) { return (uintptr_t)ret >= g_exeBase && (uintptr_t)ret < g_exeEnd; }

static Ratio R() { return UiScaleRatio(); }  // 1/1 at 1080p and below: the hooks change nothing

static LONG Down(LONG v) { return RatioDown(v, R()); }  // floor
static LONG Up(LONG v) { return RatioUp(v, R()); }      // ceiling, so Down(Up(v)) == v
static void Down(POINT* p) { p->x = Down(p->x), p->y = Down(p->y); }
static void Down(RECT* r) { r->left = Down(r->left), r->top = Down(r->top), r->right = Down(r->right), r->bottom = Down(r->bottom); }
static void Up(POINT* p) { p->x = Up(p->x), p->y = Up(p->y); }

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
    if (FromGame(_ReturnAddress())) x = Up(x), y = Up(y);
    return Real_SetCursorPos(x, y);
}

// Up on every edge: exactly the real pixels whose Down lies inside the game's rect.
static BOOL(WINAPI* Real_ClipCursor)(const RECT*);
static BOOL WINAPI Hook_ClipCursor(const RECT* r) {
    if (r && FromGame(_ReturnAddress())) {
        RECT p = {Up(r->left), Up(r->top), Up(r->right), Up(r->bottom)};
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

// ---------------------------------------------------------------- cursor size
// The game builds its cursors with CreateIconIndirect and Windows draws them at real pixels, so next to the
// magnified UI they would look small. Cursors (fIcon == FALSE) created by the exe are built at k instead:
// nearest neighbour for whole factors (2 at 4K), an area filter for fractional ones (4/3 at 1440p). The game
// owns and destroys them as usual. "fix nocursorscale on" (set before the cursor is created) keeps the
// original size.

static HBITMAP ScaleColorBitmap(HBITMAP src, int s) {
    BITMAP bm = {};
    if (!GetObjectW(src, sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0) return nullptr;
    int w = bm.bmWidth, h = bm.bmHeight;
    BITMAPINFO bi = {};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
    std::vector<uint32_t> in((size_t)w * h);
    HDC dc = GetDC(nullptr);
    bool ok = GetDIBits(dc, src, 0, h, in.data(), &bi, DIB_RGB_COLORS) == h;
    HBITMAP out = nullptr;
    if (ok) {
        BITMAPINFO bo = {};
        bo.bmiHeader = {sizeof(BITMAPINFOHEADER), w * s, -h * s, 1, 32, BI_RGB};
        void* bits = nullptr;
        out = CreateDIBSection(dc, &bo, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (out && bits) {
            auto* o = (uint32_t*)bits;
            for (int y = 0; y < h * s; ++y)
                for (int x = 0; x < w * s; ++x) o[(size_t)y * w * s + x] = in[(size_t)(y / s) * w + x / s];
        }
    }
    ReleaseDC(nullptr, dc);
    return out;
}

static HBITMAP ScaleMaskBitmap(HBITMAP src, int s) {
    BITMAP bm = {};
    if (!GetObjectW(src, sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0) return nullptr;
    HBITMAP out = CreateBitmap(bm.bmWidth * s, bm.bmHeight * s, 1, 1, nullptr);
    HDC a = CreateCompatibleDC(nullptr), b = CreateCompatibleDC(nullptr);
    HGDIOBJ oa = SelectObject(a, src), ob = SelectObject(b, out);
    SetStretchBltMode(b, COLORONCOLOR);
    StretchBlt(b, 0, 0, bm.bmWidth * s, bm.bmHeight * s, a, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
    SelectObject(a, oa);
    SelectObject(b, ob);
    DeleteDC(a);
    DeleteDC(b);
    return out;
}

// Fractional factors (4/3 at 1440p): a box filter over each output pixel's exact source
// footprint, weighted by alpha when the cursor has an alpha channel (so transparent pixels don't bleed into
// the edges); a plain colour average otherwise (mask cursors, alpha all 0).
static HBITMAP ScaleColorBitmapArea(HBITMAP src, int ow, int oh) {
    BITMAP bm = {};
    if (!GetObjectW(src, sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0) return nullptr;
    int w = bm.bmWidth, h = bm.bmHeight;
    BITMAPINFO bi = {};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), w, -h, 1, 32, BI_RGB};
    std::vector<uint32_t> in((size_t)w * h);
    HDC dc = GetDC(nullptr);
    bool ok = GetDIBits(dc, src, 0, h, in.data(), &bi, DIB_RGB_COLORS) == h;
    HBITMAP out = nullptr;
    if (ok) {
        bool alpha = false;
        for (uint32_t p : in) alpha = alpha || (p >> 24) != 0;
        BITMAPINFO bo = {};
        bo.bmiHeader = {sizeof(BITMAPINFOHEADER), ow, -oh, 1, 32, BI_RGB};
        void* bits = nullptr;
        out = CreateDIBSection(dc, &bo, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (out && bits) {
            auto* o = (uint32_t*)bits;
            double sx = (double)w / ow, sy = (double)h / oh;
            for (int y = 0; y < oh; ++y)
                for (int x = 0; x < ow; ++x) {
                    double y0 = y * sy, y1 = (y + 1) * sy, x0 = x * sx, x1 = (x + 1) * sx;
                    double c[3] = {}, a = 0, area = 0;
                    for (int iy = (int)y0; iy < h && iy < y1; ++iy)
                        for (int ix = (int)x0; ix < w && ix < x1; ++ix) {
                            double wgt = (std::min(x1, ix + 1.0) - std::max(x0, (double)ix)) *
                                         (std::min(y1, iy + 1.0) - std::max(y0, (double)iy));
                            uint32_t p = in[(size_t)iy * w + ix];
                            double pa = alpha ? (p >> 24) / 255.0 : 1.0;
                            for (int k = 0; k < 3; ++k) c[k] += wgt * pa * ((p >> (8 * k)) & 0xff);
                            a += wgt * pa;
                            area += wgt;
                        }
                    uint32_t px = 0;
                    if (a > 0) {
                        for (int k = 0; k < 3; ++k) px |= (uint32_t)std::min(255L, std::lround(c[k] / a)) << (8 * k);
                        if (alpha) px |= (uint32_t)std::min(255L, std::lround(a / area * 255.0)) << 24;
                    }
                    o[(size_t)y * ow + x] = px;
                }
        }
    }
    ReleaseDC(nullptr, dc);
    return out;
}

// 1 bpp masks to any size: nearest via StretchBlt. A monochrome cursor (no colour bitmap) stacks AND over
// XOR in one double-height mask; the halves are scaled separately so they stay aligned.
static HBITMAP ScaleMaskBitmapTo(HBITMAP src, int ow, int oh, bool stacked) {
    BITMAP bm = {};
    if (!GetObjectW(src, sizeof(bm), &bm) || bm.bmWidth <= 0 || bm.bmHeight <= 0) return nullptr;
    HBITMAP out = CreateBitmap(ow, stacked ? oh * 2 : oh, 1, 1, nullptr);
    HDC a = CreateCompatibleDC(nullptr), b = CreateCompatibleDC(nullptr);
    HGDIOBJ oa = SelectObject(a, src), ob = SelectObject(b, out);
    SetStretchBltMode(b, COLORONCOLOR);
    if (stacked) {
        int hh = bm.bmHeight / 2;
        StretchBlt(b, 0, 0, ow, oh, a, 0, 0, bm.bmWidth, hh, SRCCOPY);
        StretchBlt(b, 0, oh, ow, oh, a, 0, hh, bm.bmWidth, hh, SRCCOPY);
    } else {
        StretchBlt(b, 0, 0, ow, oh, a, 0, 0, bm.bmWidth, bm.bmHeight, SRCCOPY);
    }
    SelectObject(a, oa);
    SelectObject(b, ob);
    DeleteDC(a);
    DeleteDC(b);
    return out;
}

// False when cursors stay as they are. The game creates its cursors right after its first Present, when the
// factor is already known; after a resolution change they keep their size until it makes new ones.
static bool CursorRatio(Ratio* r) {
    *r = UiScaleRatio();
    return UiScaleActive();
}

static int RoundScaled(int v, Ratio r) { return (int)((2 * v * r.n + r.d) / (2 * r.d)); }

static HICON(WINAPI* Real_CreateIconIndirect)(PICONINFO);
static HICON WINAPI Hook_CreateIconIndirect(PICONINFO ii) {
    Ratio r;
    if (!ii || ii->fIcon || !ii->hbmMask || !CursorRatio(&r) || !FromGame(_ReturnAddress()) || FixOn("nocursorscale"))
        return Real_CreateIconIndirect(ii);
    ICONINFO big = *ii;
    BITMAP bm = {};
    GetObjectW(ii->hbmMask, sizeof(bm), &bm);
    int w = bm.bmWidth, h = ii->hbmColor ? bm.bmHeight : bm.bmHeight / 2;  // monochrome: AND over XOR
    int ow = std::max(1, RoundScaled(w, r)), oh = std::max(1, RoundScaled(h, r));
    if (r.d == 1) {  // whole factor: nearest neighbour
        int s = (int)r.n;
        big.xHotspot *= s;
        big.yHotspot *= s;
        big.hbmMask = ScaleMaskBitmap(ii->hbmMask, s);  // monochrome cursors: AND over XOR, scaled together
        big.hbmColor = ii->hbmColor ? ScaleColorBitmap(ii->hbmColor, s) : nullptr;
    } else {
        big.xHotspot = (DWORD)std::min(RoundScaled((int)ii->xHotspot, r), ow - 1);
        big.yHotspot = (DWORD)std::min(RoundScaled((int)ii->yHotspot, r), oh - 1);
        big.hbmMask = ScaleMaskBitmapTo(ii->hbmMask, ow, oh, !ii->hbmColor);
        big.hbmColor = ii->hbmColor ? ScaleColorBitmapArea(ii->hbmColor, ow, oh) : nullptr;
    }
    HICON made = (big.hbmMask && (big.hbmColor || !ii->hbmColor)) ? Real_CreateIconIndirect(&big) : nullptr;
    if (big.hbmMask) DeleteObject(big.hbmMask);  // CreateIconIndirect copies the bitmaps
    if (big.hbmColor) DeleteObject(big.hbmColor);
    static std::atomic<int> logged{0};
    if (logged++ < 3)
        Log("input: game cursor created at %s, %dx%d -> %dx%d (%s)",
            (r.d == 1 ? Fmt("%lldx", r.n) : Fmt("%lld/%lld", r.n, r.d)).c_str(), w, h, ow, oh,
            made ? "ok" : "failed, original size used");
    return made ? made : Real_CreateIconIndirect(ii);
}

// ---------------------------------------------------------------- window procedure

static HWND g_subclassed = nullptr;
static WNDPROC g_gameProc = nullptr;

// A private message; the subclass turns it into WM_SIZE with the client size at delivery time (a size read
// when posting can be stale by then, e.g. during startup while the window goes fullscreen).
static const UINT kResyncSizeMsg = WM_APP + 0x4B7;

static LRESULT CALLBACK SubclassProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == kResyncSizeMsg) {
        RECT r = {};
        Real_GetClientRect(h, &r);
        if (IsIconic(h) || r.right <= 0 || r.bottom <= 0) return 0;
        SetUiScaleFromClient((UINT)r.right, (UINT)r.bottom, "resync", false);
        LPARAM size = DownLParam(MAKELPARAM(r.right, r.bottom));
        Log("input: WM_SIZE resync %ldx%ld real -> %dx%d", r.right, r.bottom, LOWORD(size), HIWORD(size));
        return CallWindowProcW(g_gameProc, h, WM_SIZE, SIZE_RESTORED, size);
    }
    if (m == WM_SIZE) {
        if (w != SIZE_MINIMIZED) SetUiScaleFromClient(LOWORD(l), HIWORD(l), "WM_SIZE", false);  // this message carries it
        l = DownLParam(l);
    }
    return CallWindowProcW(g_gameProc, h, m, w, l);
}

void NotifyGameWindowSizeChanged() {
    if (g_subclassed && PostMessageW(g_subclassed, kResyncSizeMsg, 0, 0)) Log("input: window size resync queued after a scale change");
}

void SubclassGameWindow(HWND hwnd) {
    if (!hwnd || hwnd == g_subclassed) return;
    if (g_subclassed) {
        Log("input: a second game window (%p); only %p is subclassed", hwnd, g_subclassed);
        return;
    }
    g_gameProc = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)&SubclassProc);
    g_subclassed = g_gameProc ? hwnd : nullptr;
    Log("input: game window %p subclassed for WM_SIZE (%s)", hwnd, g_gameProc ? "ok" : "failed");
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
    n += Hook("GetMessageW", &Hook_GetMessageW, &Real_GetMessageW);
    n += Hook("PeekMessageW", &Hook_PeekMessageW, &Real_PeekMessageW);
    n += Hook("CreateIconIndirect", &Hook_CreateIconIndirect, &Real_CreateIconIndirect);
    s = MH_EnableHook(MH_ALL_HOOKS);
    Log("input hooks: %d of 10 user32 functions hooked, enable %s; game exe %p-%p", n, MH_StatusToString(s),
        (void*)g_exeBase, (void*)g_exeEnd);
}

}  // namespace goh4k
