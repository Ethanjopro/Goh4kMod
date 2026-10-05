// Native scaling state; see scale.h.
#include "scale.h"
#include "input.h"
#include <mutex>
#include <set>

namespace goh4k {

// Packed n << 32 | d, so readers on other threads never see half an update.
static std::atomic<uint64_t> g_uiRatio{(1ull << 32) | 1};
std::atomic<UINT> g_uiRealW{0}, g_uiRealH{0};

Ratio UiScaleRatio() {
    uint64_t v = g_uiRatio.load(std::memory_order_relaxed);
    return {(int64_t)(v >> 32), (int64_t)(v & 0xffffffffu)};
}

void SetUiScaleFromClient(UINT realW, UINT realH, const char* why, bool notify) {
    if (!UiScaleConfigured() || !realW || !realH) return;
    g_uiRealW = realW;
    g_uiRealH = realH;
    Ratio r = RatioForHeight(realH, 1080);
    uint64_t v = ((uint64_t)r.n << 32) | (uint64_t)r.d;
    if (g_uiRatio.exchange(v) == v) return;
    Log("ui scale %lld/%lld (%s, real client %ux%u): client size and mouse divided by it", r.n, r.d, why, realW, realH);
    if (notify) NotifyGameWindowSizeChanged();
}

static const GUID kUiCopyGuid = {0x5a1c7e42, 0x9b3d, 0x4f60, {0x8e, 0x21, 0x3c, 0x4d, 0x7a, 0x90, 0x16, 0xb5}};

void MarkUiScreenCopy(ID3D11Resource* r) {
    if (!r || IsUiScreenCopy(r)) return;
    BYTE one = 1;
    r->SetPrivateData(kUiCopyGuid, sizeof(one), &one);
}

bool IsUiScreenCopy(ID3D11Resource* r) {
    BYTE v = 0;
    UINT size = sizeof(v);
    return r && SUCCEEDED(r->GetPrivateData(kUiCopyGuid, &size, &v)) && size == sizeof(v) && v;
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
