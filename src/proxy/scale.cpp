// Native scaling state; see scale.h.
#include "scale.h"
#include "input.h"

namespace goh4k {

// cw | ch << 16 | bw << 32 | bh << 48, with the "covers its monitor" flag in bit 31 and the "switched off" flag
// in bit 63 (the heights' top bits; heights stay below 32768): readers on any thread see one consistent set.
static std::atomic<uint64_t> g_state{0};
static const uint64_t kCoversBit = 1ull << 31;
static const uint64_t kOffBit = 1ull << 63;  // set once by UiScaleShutOff; no Update after it
static std::atomic<uint32_t> g_generation{1};
// x | y << 32 (both signed)
static std::atomic<uint64_t> g_origin{0};

static UINT Field(uint64_t v, int i) { return (UINT)((v >> (16 * i)) & 0xffff); }

static Ratio FactorOf(uint64_t v) {
    return (v & kCoversBit) ? RatioForScreen(Field(v, 0), Field(v, 1) & 0x7fff, 1920, 1080) : Ratio{};
}

static UiState Decode(uint64_t v) {
    UiState s;
    s.cw = Field(v, 0), s.ch = Field(v, 1) & 0x7fff, s.bw = Field(v, 2), s.bh = Field(v, 3) & 0x7fff;
    s.covers = (v & kCoversBit) != 0;
    s.k = FactorOf(v);
    s.lw = (UINT)RatioDown((long)s.cw, s.k);
    s.lh = (UINT)RatioDown((long)s.ch, s.k);
    s.active = UiScaleConfigured() && !(s.k == Ratio{});
    return s;
}

UiState UiSnapshot() { return Decode(g_state.load(std::memory_order_acquire)); }
Ratio UiScaleRatio() { return FactorOf(g_state.load(std::memory_order_acquire)); }
uint32_t UiStateGeneration() { return g_generation.load(std::memory_order_acquire); }

POINT UiClientOrigin() {
    uint64_t v = g_origin.load(std::memory_order_acquire);
    return {(LONG)(int32_t)(uint32_t)v, (LONG)(int32_t)(uint32_t)(v >> 32)};
}

void SetUiClientOrigin(LONG x, LONG y) {
    uint64_t v = (uint64_t)(uint32_t)x | ((uint64_t)(uint32_t)y << 32);
    if (g_origin.exchange(v) != v) Log("ui: client origin %ld,%ld", x, y);
}

// Replaces 32 bits of the state (the client or the buffer half); returns the state before and after. Changes nothing
// once switched off, atomically: a WM_SIZE handled while UiScaleShutOff runs can't store a factor again.
static void Update(int half, uint64_t bits32, UiState* before, UiState* after) {
    uint64_t mask = 0xffffffffull << (32 * half), bits = (bits32 & 0xffffffffull) << (32 * half);
    uint64_t old = g_state.load(std::memory_order_acquire), next;
    do {
        next = (old & kOffBit) ? old : (old & ~mask) | bits;
    } while (!g_state.compare_exchange_weak(old, next, std::memory_order_acq_rel));
    if (next != old) g_generation.fetch_add(1, std::memory_order_acq_rel);
    *before = Decode(old);
    *after = Decode(next);
}

void SetUiClient(UINT realW, UINT realH, bool coversMonitor, const char* why, bool notify) {
    if (!UiScaleConfigured() || !realW || !realH) return;
    UiState a, b;
    Update(0, (realW & 0xffff) | ((uint64_t)(realH & 0x7fff) << 16) | (coversMonitor ? kCoversBit : 0), &a, &b);
    if (a.k == b.k) return;
    const char* what = !coversMonitor ? "a window smaller than its monitor: not scaled"
                       : b.active     ? "client size and mouse divided by it"
                                      : "1080p-class or smaller: not scaled";
    Log("ui scale %lld/%lld (%s, real client %ux%u): %s", b.k.n, b.k.d, why, realW, realH, what);
    if (notify) NotifyGameWindowSizeChanged();
}

void SetUiBuffer(UINT w, UINT h) {
    if (!UiScaleConfigured() || !w || !h) return;
    UiState a, b;
    Update(1, (w & 0xffff) | ((uint64_t)(h & 0x7fff) << 16), &a, &b);
    if (a.bw != b.bw || a.bh != b.bh) Log("ui: swapchain buffers %ux%u", w, h);
}

static std::atomic<uint32_t> g_projection{0};        // w | h << 16
static std::atomic<ULONGLONG> g_projectionSince{0};  // when it last changed; 0 once acted on

void SetUiProjection(UINT w, UINT h) {
    uint32_t v = (w & 0xffff) | (h & 0xffff) << 16;
    if (g_projection.exchange(v, std::memory_order_relaxed) != v)
        g_projectionSince.store(GetTickCount64(), std::memory_order_relaxed);
}

bool UiProjectionStuckReal(ULONGLONG now) {
    ULONGLONG since = g_projectionSince.load(std::memory_order_relaxed);
    if (!since || now - since < 250) return false;
    uint32_t v = g_projection.load(std::memory_order_relaxed);
    UINT w = v & 0xffff, h = v >> 16;
    UiState s = UiSnapshot();
    if (!s.active || w != s.bw || h != s.bh || (w == s.lw && h == s.lh)) return false;
    return g_projectionSince.compare_exchange_strong(since, 0);
}

void ClearUiClient(const char* why) {
    if (!UiScaleConfigured()) return;
    UiState a, b;
    Update(0, 0, &a, &b);
    if (!(a.k == b.k)) Log("ui scale 1/1 (%s): nothing converted until the next game window", why);
}

bool UiScaleShutOff(const char* why) {
    if (!UiScaleConfigured() || (g_state.exchange(kOffBit) & kOffBit)) return false;  // k = 1 from here on
    g_generation.fetch_add(1, std::memory_order_acq_rel);
    g_cfg.uiScale = 0;  // as after a failed hook install: every UiScaleConfigured() gate passes through
    Log("WARNING: native scaling switched off for the rest of this session: %s", why);
    return true;
}

UiCounters& Counters() {
    static UiCounters c;
    return c;
}

// One line at unload, plus a warning when a long scaled session never needed rule 1: the game draws its text
// into label surfaces in every mission and prompt, so that points at a game update that changed it.
void LogUiSessionSummary() {
    UiCounters& c = Counters();
    if (!UiScaleConfigured() && !c.framesScaled) return;  // off all session
    long long f = c.framesScaled;
    Log("session: %lld frames scaled; rule 1 %lld, rule 2 %lld; %lld UI image draws, %lld with the stand-in (%lld "
        "made); %d UI vertex shader(s) recognised",
        f, (long long)c.labelViewports, (long long)c.screenViewports, (long long)c.imageDraws,
        (long long)c.standInDraws, (long long)c.standInsMade, (int)c.uiShaders);
    if (f > 20000 && !c.labelViewports)
        Log("WARNING: rule 1 (UI text surfaces) never applied in %lld scaled frames. If text looked too large or "
            "cut off, the game has probably changed how it draws text (NOTES.md, playbook).",
            f);
}

}  // namespace goh4k
