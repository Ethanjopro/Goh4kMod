// Native scaling state; see scale.h.
#include "scale.h"
#include "input.h"
#include <mutex>
#include <set>

namespace goh4k {

// cw | ch << 16 | bw << 32 | bh << 48, with the "covers its monitor" flag in bit 31 (ch's top bit; heights
// stay below 32768): readers on any thread see one consistent set.
static std::atomic<uint64_t> g_state{0};
static const uint64_t kCoversBit = 1ull << 31;
static std::atomic<uint32_t> g_generation{1};
// x | y << 32 (both signed)
static std::atomic<uint64_t> g_origin{0};

static UINT Field(uint64_t v, int i) { return (UINT)((v >> (16 * i)) & 0xffff); }

static Ratio RatioOf(uint64_t v) { return (v & kCoversBit) ? RatioForHeight(Field(v, 1) & 0x7fff, 1080) : Ratio{}; }

static UiState Decode(uint64_t v) {
    UiState s;
    s.cw = Field(v, 0), s.ch = Field(v, 1) & 0x7fff, s.bw = Field(v, 2), s.bh = Field(v, 3);
    s.covers = (v & kCoversBit) != 0;
    s.k = RatioOf(v);
    s.lw = (UINT)RatioDown((long)s.cw, s.k);
    s.lh = (UINT)RatioDown((long)s.ch, s.k);
    s.active = UiScaleConfigured() && !(s.k == Ratio{});
    return s;
}

UiState UiSnapshot() { return Decode(g_state.load(std::memory_order_acquire)); }
Ratio UiScaleRatio() { return RatioOf(g_state.load(std::memory_order_acquire)); }
uint32_t UiStateGeneration() { return g_generation.load(std::memory_order_acquire); }

POINT UiClientOrigin() {
    uint64_t v = g_origin.load(std::memory_order_acquire);
    return {(LONG)(int32_t)(uint32_t)v, (LONG)(int32_t)(uint32_t)(v >> 32)};
}

void SetUiClientOrigin(LONG x, LONG y) {
    uint64_t v = (uint64_t)(uint32_t)x | ((uint64_t)(uint32_t)y << 32);
    if (g_origin.exchange(v) != v) Log("ui: client origin %ld,%ld", x, y);
}

// Replaces 32 bits of the state (the client or the buffer half); returns the state before and after.
static void Update(int half, uint64_t bits32, UiState* before, UiState* after) {
    uint64_t mask = 0xffffffffull << (32 * half), bits = (bits32 & 0xffffffffull) << (32 * half);
    uint64_t old = g_state.load(std::memory_order_acquire), next;
    do {
        next = (old & ~mask) | bits;
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
    Log("ui scale %lld/%lld (%s, real client %ux%u%s): client size and mouse divided by it", b.k.n, b.k.d, why, realW,
        realH, coversMonitor ? "" : ", a window smaller than its monitor: not scaled");
    if (notify) NotifyGameWindowSizeChanged();
}

void SetUiBuffer(UINT w, UINT h) {
    if (!UiScaleConfigured() || !w || !h) return;
    UiState a, b;
    Update(1, (w & 0xffff) | ((uint64_t)(h & 0xffff) << 16), &a, &b);
    if (a.bw != b.bw || a.bh != b.bh) Log("ui: swapchain buffers %ux%u", w, h);
}

static const GUID kUiCopyGuid = {0x5a1c7e42, 0x9b3d, 0x4f60, {0x8e, 0x21, 0x3c, 0x4d, 0x7a, 0x90, 0x16, 0xb5}};

void MarkUiScreenCopy(ID3D11Resource* r) {
    if (!r) return;
    uint64_t frame = (uint64_t)g_totalFrames, seen = 0;
    UINT size = sizeof(seen);
    if (SUCCEEDED(r->GetPrivateData(kUiCopyGuid, &size, &seen)) && size == sizeof(seen) && seen == frame) return;
    r->SetPrivateData(kUiCopyGuid, sizeof(frame), &frame);
}

bool IsUiScreenCopy(ID3D11Resource* r) {
    uint64_t seen = 0;
    UINT size = sizeof(seen);
    if (!r || FAILED(r->GetPrivateData(kUiCopyGuid, &size, &seen)) || size != sizeof(seen)) return false;
    return (uint64_t)g_totalFrames - seen <= 2;
}

void NoteOnce(const char* kind, const std::string& detail) {
    static std::mutex mu;
    static std::set<std::string> seen;
    {
        std::lock_guard<std::mutex> lock(mu);
        if (!seen.insert(kind).second) return;
    }
    Log("note [%s]: %s", kind, detail.c_str());
}

}  // namespace goh4k
