// 2x rendering rules (M3); see scale.h.
#include "scale.h"
#include "diag.h"
#include <mutex>
#include <set>

namespace goh4k {

std::atomic<UINT> g_logicalW{0}, g_logicalH{0};

static const GUID kScaleGuid = {0x3b9f6e10, 0x2c47, 0x4d8a, {0x91, 0x0e, 0x5a, 0x7b, 0x3c, 0x2d, 0x1e, 0x6f}};

UINT ScaleOf(ID3D11Resource* r, ScaleTag* tag) {
    if (!r) return 1;
    ScaleTag t = {};
    UINT size = sizeof(t);
    if (FAILED(r->GetPrivateData(kScaleGuid, &size, &t)) || size != sizeof(t) || t.scale < 2) return 1;
    if (tag) *tag = t;
    return t.scale;
}

UINT ScaleOfView(ID3D11View* v, ScaleTag* tag) {
    if (!v) return 1;
    ScaleTag t = {};
    UINT size = sizeof(t);
    if (SUCCEEDED(v->GetPrivateData(kScaleGuid, &size, &t)) && size == sizeof(t)) {
        if (tag) *tag = t;
        return t.scale < 2 ? 1 : t.scale;
    }
    ID3D11Resource* r = nullptr;
    v->GetResource(&r);
    UINT s = ScaleOf(r, tag);
    if (r) r->Release();
    return s;
}

void TagResource(ID3D11Resource* r, const ScaleTag& t) {
    if (r) r->SetPrivateData(kScaleGuid, sizeof(t), &t);
}

void TagView(ID3D11View* v) {
    if (!v) return;
    ScaleTag t = {1, 0, 0};
    ID3D11Resource* r = nullptr;
    v->GetResource(&r);
    ScaleOf(r, &t);
    if (r) r->Release();
    v->SetPrivateData(kScaleGuid, sizeof(t), &t);  // scale 1 is stored too, so the lookup stays on the view
}

static bool Matches(UINT v, UINT full, UINT k) { return v == full / k || v == (full + k - 1) / k; }

bool WantScaled(const D3D11_TEXTURE2D_DESC& d, bool initialData) {
    if (!Scaling() || initialData || d.Usage != D3D11_USAGE_DEFAULT) return false;
    if (!(d.BindFlags & (D3D11_BIND_RENDER_TARGET | D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_UNORDERED_ACCESS))) return false;
    UINT lw = g_logicalW, lh = g_logicalH;
    if (!lw || !lh || (d.MiscFlags & D3D11_RESOURCE_MISC_TEXTURECUBE)) return false;
    for (UINT k : {1u, 2u, 4u, 8u})
        if (Matches(d.Width, lw, k) && Matches(d.Height, lh, k)) return true;
    return false;
}

void LogScaledCreate(const D3D11_TEXTURE2D_DESC& d) {
    static std::atomic<int> n{0};
    int i = ++n;
    if (i <= 80)
        Log("scaled texture #%d: %ux%u -> %ux%u fmt=%d bind=0x%x msaa=%u mips=%u array=%u", i, d.Width, d.Height,
            d.Width * g_cfg.scale, d.Height * g_cfg.scale, (int)d.Format, d.BindFlags, d.SampleDesc.Count, d.MipLevels,
            d.ArraySize);
    else if (i % 100 == 0)
        Log("scaled texture #%d", i);
}

void NoteOnce(const char* kind, const std::string& detail) {
    static std::mutex mu;
    static std::set<std::string> seen;
    {
        std::lock_guard<std::mutex> lock(mu);
        if (!seen.insert(kind).second) return;
    }
    Log("2x note [%s]: %s", kind, detail.c_str());
}

}  // namespace goh4k
