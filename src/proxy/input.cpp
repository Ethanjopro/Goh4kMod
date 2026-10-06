// Window/input coordinate mapping for native scaling (scale.h). The game is told its client area is the
// real one divided by k (1920x1080 at 4K), so the user32 calls it uses for its client size and the mouse are
// hooked (MinHook, user32 only): reads are divided by k, writes multiplied, with rational rounding
// (ratio.h). Client coordinates scale around the client's corner; screen coordinates scale around the
// client's screen origin, so the window's position reads the same through converted and unconverted calls
// (window rect, monitor info and WM_MOVE are not hooked and stay real). Only calls whose return address is
// inside the game exe are converted; other modules (Steam overlay, drivers) see real values. The game's exe
// and memory are never touched. Raw-input deltas pass through unchanged.
#include "input.h"
#include "diag.h"
#include "scale.h"
#include "MinHook.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <intrin.h>
#include <vector>
#include <windowsx.h>
#pragma intrinsic(_ReturnAddress)

namespace goh4k {

static uintptr_t g_exeBase = 0, g_exeEnd = 0;
static std::atomic<HWND> g_subclassed{nullptr};

// Hook ids: the rows of g_hooks, in order. While CheckHooks measures a hook another program patched over,
// each caller check is counted: do the game's calls still reach us with its return address?
enum HookId {
    kGetCursorPos, kSetCursorPos, kClipCursor, kScreenToClient, kClientToScreen, kMapWindowPoints, kGetClientRect,
    kGetMessageW, kPeekMessageW, kCreateIconIndirect, kHookCount
};
static std::atomic<bool> g_counting{false};
struct CallCounts {
    std::atomic<uint32_t> checks[kHookCount], game[kHookCount];
};
alignas(64) static CallCounts g_calls;  // off the cache line of the globals every hook call reads

static bool FromGame(int id, void* ret) {
    bool game = (uintptr_t)ret >= g_exeBase && (uintptr_t)ret < g_exeEnd;
    if (g_counting.load(std::memory_order_relaxed)) {
        g_calls.checks[id].fetch_add(1, std::memory_order_relaxed);
        if (game) g_calls.game[id].fetch_add(1, std::memory_order_relaxed);
    }
    return game;
}

static Ratio R() { return UiScaleRatio(); }  // 1/1 at 1080p-class sizes and below: the hooks change nothing

// Client space: around 0,0.
static LONG Down(LONG v) { return RatioDown(v, R()); }  // floor
static LONG Up(LONG v) { return RatioUp(v, R()); }      // ceiling, so Down(Up(v)) == v
static void Down(POINT* p) { p->x = Down(p->x), p->y = Down(p->y); }
static void Up(POINT* p) { p->x = Up(p->x), p->y = Up(p->y); }

// Screen space: around the client's real screen origin o.
static void DownS(POINT* p) {
    POINT o = UiClientOrigin();
    p->x = o.x + Down(p->x - o.x), p->y = o.y + Down(p->y - o.y);
}
static void UpS(POINT* p) {
    POINT o = UiClientOrigin();
    p->x = o.x + Up(p->x - o.x), p->y = o.y + Up(p->y - o.y);
}

static LPARAM DownLParam(LPARAM lp) {
    return MAKELPARAM((WORD)(SHORT)Down(GET_X_LPARAM(lp)), (WORD)(SHORT)Down(GET_Y_LPARAM(lp)));
}
static LPARAM DownSLParam(LPARAM lp) {
    POINT p = {GET_X_LPARAM(lp), GET_Y_LPARAM(lp)};
    DownS(&p);
    return MAKELPARAM((WORD)(SHORT)p.x, (WORD)(SHORT)p.y);
}

// Client-area mouse messages carry client coordinates; the wheel ones carry screen coordinates. Non-client
// ones (title bar, borders) stay real: DefWindowProc needs them as they are.
static bool IsClientMouseMessage(UINT m) {
    return m >= WM_MOUSEFIRST && m <= WM_MOUSELAST && m != WM_MOUSEWHEEL && m != WM_MOUSEHWHEEL;
}
static bool IsWheelMessage(UINT m) { return m == WM_MOUSEWHEEL || m == WM_MOUSEHWHEEL; }

// Diagnostics: the first calls of the two "write" hooks, with what the game passed and the real window
// geometry next to it, so it shows which coordinate space the game uses for them.
static void LogWrite(const char* what, const std::string& args, const std::string& real) {
    static std::atomic<int> n{0};
    if (!g_cfg.diagnostics || n++ >= 24) return;
    RECT wr = {}, cr = {};
    POINT o = UiClientOrigin();
    if (g_subclassed) GetWindowRect(g_subclassed, &wr), GetClientRect(g_subclassed, &cr);  // our module: real
    Log("input: %s(%s) -> real %s; window (%ld,%ld)-(%ld,%ld), client %ldx%ld at %ld,%ld", what, args.c_str(), real.c_str(),
        wr.left, wr.top, wr.right, wr.bottom, cr.right, cr.bottom, o.x, o.y);
}

// ---------------------------------------------------------------- hooks

static BOOL(WINAPI* Real_GetCursorPos)(LPPOINT);
static BOOL WINAPI Hook_GetCursorPos(LPPOINT p) {
    BOOL r = Real_GetCursorPos(p);
    if (r && p && FromGame(kGetCursorPos, _ReturnAddress())) DownS(p);
    return r;
}

static BOOL(WINAPI* Real_SetCursorPos)(int, int);
static BOOL WINAPI Hook_SetCursorPos(int x, int y) {
    if (FromGame(kSetCursorPos, _ReturnAddress())) {
        POINT p = {x, y};
        UpS(&p);
        LogWrite("SetCursorPos", Fmt("%d,%d", x, y), Fmt("%ld,%ld", p.x, p.y));
        x = p.x, y = p.y;
    }
    return Real_SetCursorPos(x, y);
}

// Up on every edge: exactly the real pixels whose Down lies inside the game's rect.
static BOOL(WINAPI* Real_ClipCursor)(const RECT*);
static BOOL WINAPI Hook_ClipCursor(const RECT* r) {
    if (r && FromGame(kClipCursor, _ReturnAddress())) {
        POINT a = {r->left, r->top}, b = {r->right, r->bottom};
        UpS(&a), UpS(&b);
        RECT p = {a.x, a.y, b.x, b.y};
        LogWrite("ClipCursor", Fmt("%ld,%ld-%ld,%ld", r->left, r->top, r->right, r->bottom),
                 Fmt("%ld,%ld-%ld,%ld", p.left, p.top, p.right, p.bottom));
        return Real_ClipCursor(&p);
    }
    if (!r && FromGame(kClipCursor, _ReturnAddress())) LogWrite("ClipCursor", "null", "null");
    return Real_ClipCursor(r);
}

static BOOL(WINAPI* Real_ScreenToClient)(HWND, LPPOINT);
static BOOL WINAPI Hook_ScreenToClient(HWND h, LPPOINT p) {
    if (!p || !FromGame(kScreenToClient, _ReturnAddress())) return Real_ScreenToClient(h, p);
    UpS(p);
    BOOL r = Real_ScreenToClient(h, p);
    Down(p);
    return r;
}

static BOOL(WINAPI* Real_ClientToScreen)(HWND, LPPOINT);
static BOOL WINAPI Hook_ClientToScreen(HWND h, LPPOINT p) {
    if (!p || !FromGame(kClientToScreen, _ReturnAddress())) return Real_ClientToScreen(h, p);
    Up(p);
    BOOL r = Real_ClientToScreen(h, p);
    DownS(p);
    return r;
}

// A null or desktop window means screen coordinates.
static bool IsScreen(HWND h) { return !h || h == GetDesktopWindow(); }

static int(WINAPI* Real_MapWindowPoints)(HWND, HWND, LPPOINT, UINT);
static int WINAPI Hook_MapWindowPoints(HWND from, HWND to, LPPOINT pts, UINT n) {
    if (!pts || !n || !FromGame(kMapWindowPoints, _ReturnAddress())) return Real_MapWindowPoints(from, to, pts, n);
    POINT first = pts[0];
    for (UINT i = 0; i < n; ++i) IsScreen(from) ? UpS(&pts[i]) : Up(&pts[i]);
    int r = Real_MapWindowPoints(from, to, pts, n);
    for (UINT i = 0; i < n; ++i) IsScreen(to) ? DownS(&pts[i]) : Down(&pts[i]);
    if (!r && GetLastError()) return r;
    // The return value is the offset added to every point, in the game's units.
    return MAKELONG((WORD)(SHORT)(pts[0].x - first.x), (WORD)(SHORT)(pts[0].y - first.y));
}

// Only the game window's own client area is logical; other windows (the desktop, say) stay real.
static BOOL(WINAPI* Real_GetClientRect)(HWND, LPRECT);
static BOOL WINAPI Hook_GetClientRect(HWND h, LPRECT r) {
    BOOL ok = Real_GetClientRect(h, r);
    if (ok && r && h && h == g_subclassed && FromGame(kGetClientRect, _ReturnAddress())) {
        r->right = Down(r->right);
        r->bottom = Down(r->bottom);
    }
    return ok;
}

static void DownMessage(MSG* m) {
    DownS(&m->pt);
    if (m->hwnd && m->hwnd != g_subclassed) return;  // only the game window's client area is logical
    if (IsClientMouseMessage(m->message)) m->lParam = DownLParam(m->lParam);
    else if (IsWheelMessage(m->message)) m->lParam = DownSLParam(m->lParam);
}

static BOOL(WINAPI* Real_GetMessageW)(LPMSG, HWND, UINT, UINT);
static BOOL WINAPI Hook_GetMessageW(LPMSG m, HWND h, UINT lo, UINT hi) {
    BOOL r = Real_GetMessageW(m, h, lo, hi);
    if (r > 0 && m && FromGame(kGetMessageW, _ReturnAddress())) DownMessage(m);
    return r;
}

static BOOL(WINAPI* Real_PeekMessageW)(LPMSG, HWND, UINT, UINT, UINT);
static BOOL WINAPI Hook_PeekMessageW(LPMSG m, HWND h, UINT lo, UINT hi, UINT rm) {
    BOOL r = Real_PeekMessageW(m, h, lo, hi, rm);
    if (r && m && FromGame(kPeekMessageW, _ReturnAddress())) DownMessage(m);
    return r;
}

// ---------------------------------------------------------------- cursor size
// The game builds its cursors with CreateIconIndirect and Windows draws them at real pixels, so next to the
// magnified UI they would look small. Cursors (fIcon == FALSE) created by the exe are built at k instead, with
// a box filter over each output pixel's exact source footprint: nearest neighbour for whole factors (2 at
// 4K), smooth for fractional ones (4/3 at 1440p). The game owns and destroys them as usual. "fix
// nocursorscale on" (set before the cursor is created) keeps the original size.

// Colour, weighted by alpha when the cursor has an alpha channel (so transparent pixels don't bleed into the
// edges); a plain average otherwise (alpha all 0).
static HBITMAP ScaleColorBitmap(HBITMAP src, int ow, int oh) {
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

// 1 bpp masks: nearest via StretchBlt. A monochrome cursor (no colour bitmap) stacks AND over XOR in one
// double-height mask; the halves are scaled separately so they stay aligned.
static HBITMAP ScaleMaskBitmap(HBITMAP src, int ow, int oh, bool stacked) {
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

static int RoundScaled(int v, Ratio r) { return (int)((2 * v * r.n + r.d) / (2 * r.d)); }

// The game creates its cursors right after its first Present, when the factor is already known; after a
// resolution change they keep their size until it makes new ones.
static HICON(WINAPI* Real_CreateIconIndirect)(PICONINFO);
static HICON WINAPI Hook_CreateIconIndirect(PICONINFO ii) {
    Ratio r = UiScaleRatio();
    if (!ii || ii->fIcon || !ii->hbmMask || !UiScaleActive() || !FromGame(kCreateIconIndirect, _ReturnAddress()) ||
        FixOn("nocursorscale"))
        return Real_CreateIconIndirect(ii);
    BITMAP bm = {};
    GetObjectW(ii->hbmMask, sizeof(bm), &bm);
    int w = bm.bmWidth, h = ii->hbmColor ? bm.bmHeight : bm.bmHeight / 2;  // monochrome: AND over XOR
    int ow = std::max(1, RoundScaled(w, r)), oh = std::max(1, RoundScaled(h, r));
    ICONINFO big = *ii;
    big.xHotspot = (DWORD)std::min(RoundScaled((int)ii->xHotspot, r), ow - 1);
    big.yHotspot = (DWORD)std::min(RoundScaled((int)ii->yHotspot, r), oh - 1);
    big.hbmMask = ScaleMaskBitmap(ii->hbmMask, ow, oh, !ii->hbmColor);
    big.hbmColor = ii->hbmColor ? ScaleColorBitmap(ii->hbmColor, ow, oh) : nullptr;
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

static std::atomic<WNDPROC> g_gameProc{nullptr};  // what our subclass calls on to

// Does a client area of this size at the window's current position fill its monitor exactly (fullscreen, or
// borderless at the desktop size)? GetMonitorInfoW isn't hooked, so this is the real (current-mode) rect.
bool ClientCoversMonitor(HWND h, UINT cw, UINT ch) {
    POINT o = {0, 0};
    if (!(Real_ClientToScreen ? Real_ClientToScreen(h, &o) : ClientToScreen(h, &o))) return false;
    MONITORINFO mi = {sizeof(mi)};
    if (!GetMonitorInfoW(MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST), &mi)) return false;
    return o.x == mi.rcMonitor.left && o.y == mi.rcMonitor.top && (LONG)cw == mi.rcMonitor.right - mi.rcMonitor.left &&
           (LONG)ch == mi.rcMonitor.bottom - mi.rcMonitor.top;
}

// A private message; the subclass turns it into WM_SIZE with the client size at delivery time (a size read
// when posting can be stale by then, e.g. during startup while the window goes fullscreen). Registered, so it
// can't be one of the game's own WM_APP messages.
static UINT g_resyncMsg = 0;

static void UpdateClientOrigin(HWND h) {
    POINT o = {0, 0};
    if (Real_ClientToScreen ? Real_ClientToScreen(h, &o) : ClientToScreen(h, &o)) SetUiClientOrigin(o.x, o.y);
}

// When the queued resync was posted (0: none queued). A queued one counts for 2 s only: delivered while our procedure
// was out of the chain, it would never come back to clear this.
static std::atomic<ULONGLONG> g_resyncPostedAt{0};

static LRESULT CALLBACK SubclassProc(HWND h, UINT m, WPARAM w, LPARAM l);

// The window is going away: nothing is converted until the next game window (the next swapchain's).
// A newer game window whose swapchain appeared while the current window still existed (a game that recreates
// its window makes the new one first): followed as soon as the current one is destroyed.
static std::atomic<HWND> g_pendingWindow{nullptr};

// Puts SubclassProc in front of hwnd's procedure (expected: the one there now). What it calls on to is stored
// first, because a message can arrive the moment ours is in place: the window's thread keeps dispatching
// while this runs on the render thread.
static bool InstallSubclass(HWND hwnd, WNDPROC expected) {
    g_gameProc = expected;
    WNDPROC was = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)&SubclassProc);
    if (!was) return false;
    if (was != expected) g_gameProc = was;  // replaced in between
    return true;
}

static void ForgetGameWindow(HWND h) {
    if (g_subclassed != h) return;
    if ((WNDPROC)GetWindowLongPtrW(h, GWLP_WNDPROC) == &SubclassProc)
        SetWindowLongPtrW(h, GWLP_WNDPROC, (LONG_PTR)g_gameProc.load());
    g_subclassed = nullptr;
    g_resyncPostedAt = 0;
    ClearUiClient("game window destroyed");
    Log("input: game window %p destroyed, no longer subclassed", h);
    HWND next = g_pendingWindow.exchange(nullptr);
    if (!next || !IsWindow(next)) return;
    SubclassGameWindow(next);
    RECT r = {};
    if (g_subclassed == next && Real_GetClientRect(next, &r) && !IsIconic(next))
        SetUiClient((UINT)r.right, (UINT)r.bottom, ClientCoversMonitor(next, r.right, r.bottom), "next game window",
                    true);
}

static LRESULT CALLBACK SubclassProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (g_resyncMsg && m == g_resyncMsg) {
        g_resyncPostedAt = 0;
        RECT r = {};
        Real_GetClientRect(h, &r);
        if (IsIconic(h) || r.right <= 0 || r.bottom <= 0) return 0;
        UpdateClientOrigin(h);
        SetUiClient((UINT)r.right, (UINT)r.bottom, ClientCoversMonitor(h, r.right, r.bottom), "resync", false);
        LPARAM size = DownLParam(MAKELPARAM(r.right, r.bottom));
        Log("input: WM_SIZE resync %ldx%ld real -> %dx%d", r.right, r.bottom, LOWORD(size), HIWORD(size));
        return CallWindowProcW(g_gameProc, h, WM_SIZE, SIZE_RESTORED, size);
    }
    if (m == WM_SIZE) {
        if (w != SIZE_MINIMIZED) {
            UpdateClientOrigin(h);
            // This message carries the size, converted below with the factor just set, so no resync.
            SetUiClient(LOWORD(l), HIWORD(l), ClientCoversMonitor(h, LOWORD(l), HIWORD(l)), "WM_SIZE", false);
        }
        l = DownLParam(l);
    } else if (m == WM_MOVE) {
        SetUiClientOrigin(GET_X_LPARAM(l), GET_Y_LPARAM(l));  // the client's screen origin, passed on real
        RECT r = {};
        if (Real_GetClientRect(h, &r) && !IsIconic(h))  // moved onto or off a whole monitor
            SetUiClient((UINT)r.right, (UINT)r.bottom, ClientCoversMonitor(h, r.right, r.bottom), "WM_MOVE", true);
    } else if (m == WM_NCDESTROY) {
        WNDPROC next = g_gameProc;
        ForgetGameWindow(h);
        return CallWindowProcW(next, h, m, w, l);
    }
    return CallWindowProcW(g_gameProc, h, m, w, l);
}

void NotifyGameWindowSizeChanged() {
    HWND h = g_subclassed;
    if (!h || !g_resyncMsg) return;
    ULONGLONG now = GetTickCount64(), at = g_resyncPostedAt;
    if ((at && now - at < 2000) || !g_resyncPostedAt.compare_exchange_strong(at, now)) return;  // one is queued
    if (PostMessageW(h, g_resyncMsg, 0, 0)) Log("input: window size resync queued");
    else g_resyncPostedAt = 0;
}

void SubclassGameWindow(HWND hwnd) {
    if (!hwnd || hwnd == g_subclassed) return;
    if (HWND cur = g_subclassed; cur && IsWindow(cur)) {
        g_pendingWindow = hwnd;
        Log("input: a second game window (%p): followed once %p is gone", hwnd, cur);
        return;
    }
    if (!g_resyncMsg) g_resyncMsg = RegisterWindowMessageW(L"Goh4kMod.ResyncSize");
    bool ok = InstallSubclass(hwnd, (WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC));
    g_subclassed = ok ? hwnd : nullptr;
    if (ok) UpdateClientOrigin(hwnd);
    Log("input: game window %p subclassed for WM_SIZE/WM_MOVE (%s)", hwnd, ok ? "ok" : "failed");
}

bool IsGameWindow(HWND hwnd) { return hwnd && hwnd == g_subclassed; }

// ---------------------------------------------------------------- install and checks

struct HookEntry {
    const char* name;
    void* detour;
    void** real;
    bool converts;          // false: only the cursor's size
    void* target;           // the user32 function
    uint8_t stock[5];       // its first bytes before our patch
    uint8_t ours[5];        // and with it (MinHook writes a 5-byte jmp)
    int state;              // last seen: 0 ours, 1 another patch over ours, 2 our patch gone
    uint32_t checks, game;  // caller-check counts when the patch over ours was seen
    int checksLeft;         // CheckHooks runs left to measure how its calls arrive (0: done)
};

template <typename T>
static HookEntry Entry(const char* name, T* detour, T** real, bool converts = true) {
    return HookEntry{name, (void*)detour, (void**)real, converts, nullptr, {}, {}, 0, 0, 0, 0};
}

static HookEntry g_hooks[] = {  // in HookId order
    Entry("GetCursorPos", &Hook_GetCursorPos, &Real_GetCursorPos),
    Entry("SetCursorPos", &Hook_SetCursorPos, &Real_SetCursorPos),
    Entry("ClipCursor", &Hook_ClipCursor, &Real_ClipCursor),
    Entry("ScreenToClient", &Hook_ScreenToClient, &Real_ScreenToClient),
    Entry("ClientToScreen", &Hook_ClientToScreen, &Real_ClientToScreen),
    Entry("MapWindowPoints", &Hook_MapWindowPoints, &Real_MapWindowPoints),
    Entry("GetClientRect", &Hook_GetClientRect, &Real_GetClientRect),
    Entry("GetMessageW", &Hook_GetMessageW, &Real_GetMessageW),
    Entry("PeekMessageW", &Hook_PeekMessageW, &Real_PeekMessageW),
    Entry("CreateIconIndirect", &Hook_CreateIconIndirect, &Real_CreateIconIndirect, false),
};
static_assert(sizeof(g_hooks) / sizeof(g_hooks[0]) == kHookCount, "one g_hooks row per HookId");
static bool g_hooksOn = false;

static bool Readable(const void* p, size_t n) {
    MEMORY_BASIC_INFORMATION mbi = {};
    if (!VirtualQuery(p, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)))
        return false;
    return (const uint8_t*)p + n <= (const uint8_t*)mbi.BaseAddress + mbi.RegionSize;
}

// The module a call into fn reaches first: follows the jumps hook libraries put at a function's entry (jmp
// rel8/rel32, jmp [rip+x], mov rax/r11,imm64 + jmp) through relay pages until it lands in another module.
// Only used to name, in the log, whoever patched over us.
static HMODULE Landing(const void* fn) {
    const uint8_t* p = (const uint8_t*)fn;
    HMODULE start = ModuleOf(fn);
    for (int i = 0; i < 6 && p && Readable(p, 13); ++i) {
        const uint8_t* next = nullptr;
        if (p[0] == 0xE9) next = p + 5 + *(const int32_t*)(p + 1);
        else if (p[0] == 0xEB) next = p + 2 + (int8_t)p[1];
        else if (p[0] == 0xFF && p[1] == 0x25) {
            const uint8_t* slot = p + 6 + *(const int32_t*)(p + 2);
            if (Readable(slot, 8)) next = *(const uint8_t* const*)slot;
        } else if (p[0] == 0x48 && p[1] == 0xB8 && p[10] == 0xFF && p[11] == 0xE0) next = *(const uint8_t* const*)(p + 2);
        else if (p[0] == 0x49 && p[1] == 0xBB && p[10] == 0x41 && p[11] == 0xFF && p[12] == 0xE3)
            next = *(const uint8_t* const*)(p + 2);
        if (!next) break;
        p = next;
        HMODULE m = ModuleOf(p);
        if (m && m != start) return m;
    }
    return ModuleOf(p);
}

// proxytest (dev builds): GOH4K_TEST_FAILHOOK=<name> pretends that hook can't be made, to check that the mod
// then stays fully off.
static bool TestFailsHook(const char* name) {
#ifdef GOH4K_RELEASE
    (void)name;
    return false;
#else
    char fail[64] = {};
    GetEnvironmentVariableA("GOH4K_TEST_FAILHOOK", fail, sizeof(fail));
    return strcmp(fail, name) == 0;
#endif
}

void InstallInputHooks() {
    g_exeBase = (uintptr_t)GetModuleHandleW(nullptr);
    g_exeEnd = g_exeBase + ExeHeaders()->OptionalHeader.SizeOfImage;

    MH_STATUS s = MH_Initialize();
    bool inited = s == MH_OK;
    int made = 0;
    const char* failed = inited ? nullptr : "MH_Initialize";
    MH_STATUS failedWith = s;
    if (inited) {
        for (HookEntry& e : g_hooks) {
            MH_STATUS c = MH_CreateHookApiEx(L"user32", e.name, e.detour, e.real, &e.target);
            if (c == MH_OK && TestFailsHook(e.name)) MH_RemoveHook(e.target), c = MH_ERROR_NOT_EXECUTABLE;
            if (c != MH_OK) {
                Log("input hook %s: %s", e.name, MH_StatusToString(c));
                if (!failed) failed = e.name, failedWith = c;
                continue;
            }
            memcpy(e.stock, e.target, sizeof(e.stock));
            ++made;
        }
        if (made == kHookCount) {
            s = MH_EnableHook(MH_ALL_HOOKS);
            if (s != MH_OK) failed = "MH_EnableHook", failedWith = s;
        }
    }
    if (failed) {
        // All or nothing: with some hooks missing the game would get a logical client size and a real mouse (or
        // the reverse), and clicks would land in the wrong place. Without any, it runs as it does without the mod.
        if (inited) MH_Uninitialize();  // also undoes any hook already enabled
        for (HookEntry& e : g_hooks) *e.real = nullptr;
        g_cfg.uiScale = 0;
        Log("input hooks: %d of %d made, %s failed (%s): native scaling is off for this session", made, kHookCount, failed,
            MH_StatusToString(failedWith));
        return;
    }
    for (HookEntry& e : g_hooks) memcpy(e.ours, e.target, sizeof(e.ours));
    g_hooksOn = true;
    Log("input hooks: %d of %d user32 functions hooked; game exe %p-%p", made, kHookCount, (void*)g_exeBase, (void*)g_exeEnd);
}

int InputHooksInstalled() { return g_hooksOn ? kHookCount : 0; }

int InputHooksPatchedOver() {
    int n = 0;
    for (const HookEntry& e : g_hooks) n += e.state == 1;
    return n;
}

// Is each hook still where we put it? Another program (an overlay) can patch over ours after us, or restore
// the original bytes. Changes are logged. Through another patch, the game's calls still reach us with its
// return address if that program passes them on with a jump (they are then converted as usual), but not if
// it calls us: then they look like that program's own calls and stay real. For up to 500 calls or a minute
// after a patch over ours, the caller checks are counted and logged to tell which. With a converting hook
// restored, the mod would be half on, so it switches itself off.
static void CheckHooks() {
    bool measuring = false;
    for (int i = 0; i < kHookCount; ++i) {
        HookEntry& e = g_hooks[i];
        uint8_t now[5];
        memcpy(now, e.target, sizeof(now));
        int state = !memcmp(now, e.ours, sizeof(now)) ? 0 : !memcmp(now, e.stock, sizeof(now)) ? 2 : 1;
        if (state != e.state) {
            e.state = state;
            if (state == 0) {
                Log("input hook %s: ours again", e.name);
            } else if (state == 1) {
                e.checks = g_calls.checks[i], e.game = g_calls.game[i], e.checksLeft = 30;
                Log("input hook %s: patched over by %s", e.name, ModuleNameOf(Landing(e.target)).c_str());
            } else {
                Log("input hook %s: our patch was removed", e.name);
                if (e.converts && UiScaleShutOff(Fmt("the %s hook was removed by another program", e.name).c_str()))
                    NotifyGameWindowSizeChanged();
            }
        }
        if (state != 1 || !e.checksLeft) continue;
        uint32_t checks = g_calls.checks[i] - e.checks, game = g_calls.game[i] - e.game;
        if (checks < 500 && --e.checksLeft) {
            measuring = true;
            continue;
        }
        e.checksLeft = 0;
        if (checks)
            Log("input hook %s, patched over: %u of the %u calls since came with the game's return address%s", e.name,
                game, checks, game ? " (converted as usual)" : "; calls that come through it are not converted");
    }
    g_counting.store(kLogging && measuring, std::memory_order_relaxed);  // the counts only feed a log line
}

// Our window procedure can drop out of the chain: the game or another program sets a new one, or one that
// subclassed before us puts back what it replaced. WM_SIZE would then reach the game real while everything
// else stays logical. When the top procedure is exactly the one we wrapped, or the class's own, ours is
// certainly out of the chain: subclass again and resync. Any other procedure above ours normally calls on to
// ours, so it is left alone.
static void CheckWindowProc() {
    HWND h = g_subclassed;
    if (!h) return;
    WNDPROC top = (WNDPROC)GetWindowLongPtrW(h, GWLP_WNDPROC);
    if (!top || top == &SubclassProc) return;
    if (top != g_gameProc.load() && top != (WNDPROC)GetClassLongPtrW(h, GCLP_WNDPROC)) return;
    if (!InstallSubclass(h, top)) return;
    Log("input: the game window's procedure was replaced and ours dropped out; subclassed again");
    g_resyncPostedAt = 0;  // one queued meanwhile went to the other procedure
    NotifyGameWindowSizeChanged();
}

void CheckGameWindow() {
    if (!UiScaleConfigured()) return;
    CheckWindowProc();
    CheckHooks();
}

}  // namespace goh4k
