// Constant-buffer patches for 2x mode (M4); see cbpatch.h.
#include "cbpatch.h"
#include "diag.h"
#include "scale.h"
#include "wrappers.h"
#include <d3d11shader.h>
#include <atomic>
#include <deque>
#include <mutex>
#include <unordered_map>

namespace goh4k {

// ---------------------------------------------------------------- rules (by constant name in the shader)

struct Rule {
    const char* name;
    UINT firstComp, numComps;
    bool multiply;
    bool whenSource;
};

// Names found in the game's HLSL (shader.pak) whose values are in logical pixels.
static const Rule kRules[] = {
    {"src_size", 0, 2, true, true},                    // copy.hlsl: Load(uv * src_size) on a multisampled source
    {"one_pixel", 0, 2, false, true},                  // copy.hlsl: 1 / src_size
    {"textureDim", 0, 2, true, true},                  // heat_mask.hlsl: Load(uv * textureDim)
    {"fxaaNormPixelWidthHeight", 0, 2, false, false},  // lens_effects.hlsl: FXAA texel size
    {"sunMoonPosRes", 2, 2, true, false},              // sky.hlsl: .zw = screen size the sun disc is placed in
};

static const GUID kPatchGuid = {0x6a1f0c55, 0x4e2b, 0x4f13, {0xa7, 0x3d, 0x18, 0x9c, 0x52, 0x0b, 0xe4, 0x71}};
static std::mutex g_patchMutex;
static std::deque<ShaderPatch> g_patches;  // deque: references stay valid as shaders register

typedef HRESULT(WINAPI* PFN_Reflect)(const void*, SIZE_T, REFIID, void**);

static PFN_Reflect Reflector() {
    static PFN_Reflect fn = [] {
        HMODULE m = LoadLibraryW(L"d3dcompiler_47.dll");
        return m ? (PFN_Reflect)GetProcAddress(m, "D3DReflect") : nullptr;
    }();
    return fn;
}

int RegisterShaderPatches(ID3D11DeviceChild* shader, const void* code, SIZE_T length) {
    if (!Scaling() || !shader || !Reflector()) return -1;
    ID3D11ShaderReflection* refl = nullptr;
    if (FAILED(Reflector()(code, length, __uuidof(ID3D11ShaderReflection), (void**)&refl))) return -1;
    ShaderPatch patch;
    D3D11_SHADER_DESC sd = {};
    refl->GetDesc(&sd);
    for (UINT i = 0; i < sd.ConstantBuffers; ++i) {
        ID3D11ShaderReflectionConstantBuffer* cb = refl->GetConstantBufferByIndex(i);
        D3D11_SHADER_BUFFER_DESC cbd = {};
        D3D11_SHADER_INPUT_BIND_DESC bind = {};
        if (FAILED(cb->GetDesc(&cbd)) || cbd.Type != D3D_CT_CBUFFER ||
            FAILED(refl->GetResourceBindingDescByName(cbd.Name, &bind)))
            continue;
        for (UINT v = 0; v < cbd.Variables; ++v) {
            D3D11_SHADER_VARIABLE_DESC vd = {};
            cb->GetVariableByIndex(v)->GetDesc(&vd);
            if (!(vd.uFlags & D3D_SVF_USED)) continue;
            for (const Rule& r : kRules) {
                if (strcmp(vd.Name, r.name) != 0 || (r.firstComp + r.numComps) * 4 > vd.Size) continue;
                patch.ops.push_back({bind.BindPoint, vd.StartOffset, r.firstComp, r.numComps, r.multiply, r.whenSource});
                patch.what += Fmt("%s%s(b%u+%u %s)", patch.what.empty() ? "" : ", ", r.name, bind.BindPoint,
                                  vd.StartOffset, r.multiply ? "*2" : "/2");
            }
        }
    }
    refl->Release();
    if (patch.ops.empty()) return -1;
    int index;
    {
        std::lock_guard<std::mutex> lock(g_patchMutex);
        index = (int)g_patches.size();
        g_patches.push_back(patch);
    }
    shader->SetPrivateData(kPatchGuid, sizeof(index), &index);
    Log("cbpatch: ps %s: %s", ShaderId(shader).c_str(), patch.what.c_str());
    return index;
}

int ShaderPatchIndex(ID3D11DeviceChild* shader) {
    if (!shader) return -1;
    int index = -1;
    UINT size = sizeof(index);
    if (FAILED(shader->GetPrivateData(kPatchGuid, &size, &index)) || size != sizeof(index)) return -1;
    return index;
}

const ShaderPatch& GetShaderPatch(int index) {
    std::lock_guard<std::mutex> lock(g_patchMutex);
    return g_patches[index];
}

// ---------------------------------------------------------------- constant-buffer shadows

static std::mutex g_shadowMutex;
static std::unordered_map<ID3D11Resource*, std::vector<uint8_t>> g_shadows;
static std::atomic<int> g_shadowCount{0};

bool IsTrackedCb(ID3D11Resource* r) {
    if (!g_shadowCount.load(std::memory_order_relaxed)) return false;
    std::lock_guard<std::mutex> lock(g_shadowMutex);
    return g_shadows.count(r) != 0;
}

void TrackCbUpdate(ID3D11Resource* dst, const D3D11_BOX* box, const void* data) {
    if (!data || !g_shadowCount.load(std::memory_order_relaxed)) return;
    std::lock_guard<std::mutex> lock(g_shadowMutex);
    auto it = g_shadows.find(dst);
    if (it == g_shadows.end()) return;
    auto& s = it->second;
    UINT begin = box ? box->left : 0, end = box ? box->right : (UINT)s.size();
    if (end > s.size()) end = (UINT)s.size();
    if (begin < end) memcpy(s.data() + begin, data, end - begin);
}

void TrackCbMapped(ID3D11Resource* r, const void* mapped) {
    if (!mapped) return;
    std::lock_guard<std::mutex> lock(g_shadowMutex);
    auto it = g_shadows.find(r);
    if (it != g_shadows.end()) memcpy(it->second.data(), mapped, it->second.size());
}

// First use: read the buffer back once (a one-off stall), then keep the shadow current from the game's
// UpdateSubresource/Map calls.
static bool ShadowOf(WrappedContext* ctx, ID3D11Buffer* cb, std::vector<uint8_t>* out) {
    {
        std::lock_guard<std::mutex> lock(g_shadowMutex);
        auto it = g_shadows.find(cb);
        if (it != g_shadows.end()) {
            *out = it->second;
            return true;
        }
    }
    D3D11_BUFFER_DESC d;
    cb->GetDesc(&d);
    D3D11_BUFFER_DESC sd = {d.ByteWidth, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0};
    ID3D11Buffer* staging = nullptr;
    if (FAILED(ctx->Device()->Real()->CreateBuffer(&sd, nullptr, &staging))) return false;
    ctx->RealCtx()->CopyResource(staging, cb);
    D3D11_MAPPED_SUBRESOURCE m = {};
    bool ok = SUCCEEDED(ctx->RealCtx()->Map(staging, 0, D3D11_MAP_READ, 0, &m));
    if (ok) {
        out->assign((const uint8_t*)m.pData, (const uint8_t*)m.pData + d.ByteWidth);
        ctx->RealCtx()->Unmap(staging, 0);
        std::lock_guard<std::mutex> lock(g_shadowMutex);
        g_shadows[cb] = *out;
        g_shadowCount = (int)g_shadows.size();
        Log("cbpatch: tracking constant buffer %s", ResTag(cb).c_str());
    }
    staging->Release();
    return ok;
}

// ---------------------------------------------------------------- draw-time patching

// Our own copies, one per slot and size, reused across draws (UpdateSubresource keeps command order).
static ID3D11Buffer* PatchBuffer(WrappedContext* ctx, UINT slot, UINT size) {
    static std::mutex mu;
    static std::unordered_map<uint64_t, ID3D11Buffer*> buffers;
    uint64_t key = ((uint64_t)(uintptr_t)ctx << 16) ^ ((uint64_t)slot << 32) ^ size;
    std::lock_guard<std::mutex> lock(mu);
    auto it = buffers.find(key);
    if (it != buffers.end()) return it->second;
    D3D11_BUFFER_DESC d = {size, D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0};
    ID3D11Buffer* b = nullptr;
    ctx->Device()->Real()->CreateBuffer(&d, nullptr, &b);
    buffers[key] = b;
    return b;
}

CbPatchScope::CbPatchScope(WrappedContext* ctx, int patchIndex, UINT targetScale) {
    if (patchIndex < 0 || !Scaling() || FixOn("nocbpatch")) return;
    const ShaderPatch& sp = GetShaderPatch(patchIndex);
    ID3D11ShaderResourceView* t0 = nullptr;
    ctx->RealCtx()->PSGetShaderResources(0, 1, &t0);
    UINT sourceScale = ScaleOfView(t0);
    if (t0) t0->Release();
    float s = (float)g_cfg.scale;
    for (const CbPatchOp& op : sp.ops) {
        if ((op.whenSource ? sourceScale : targetScale) < 2) continue;
        int k = 0;
        while (k < m_count && m_slots[k] != op.slot) ++k;
        if (k == m_count) {  // first op for this slot: bind a fresh patched copy
            if (m_count == 4) break;
            ID3D11Buffer* cb = nullptr;
            ctx->RealCtx()->PSGetConstantBuffers(op.slot, 1, &cb);
            std::vector<uint8_t> data;
            if (!cb || !ShadowOf(ctx, cb, &data)) {
                if (cb) cb->Release();
                continue;
            }
            // apply every op for this slot to the copy
            for (const CbPatchOp& o : sp.ops) {
                if (o.slot != op.slot || (o.whenSource ? sourceScale : targetScale) < 2) continue;
                if (o.offset + (o.firstComp + o.numComps) * 4 > data.size()) continue;
                float* f = (float*)(data.data() + o.offset) + o.firstComp;
                for (UINT c = 0; c < o.numComps; ++c) f[c] = o.multiply ? f[c] * s : f[c] / s;
            }
            ID3D11Buffer* pb = PatchBuffer(ctx, op.slot, (UINT)data.size());
            if (!pb) {
                cb->Release();
                continue;
            }
            ctx->RealCtx()->UpdateSubresource(pb, 0, nullptr, data.data(), 0, 0);
            ctx->RealCtx()->PSSetConstantBuffers(op.slot, 1, &pb);
            m_slots[m_count] = op.slot;
            m_saved[m_count] = cb;  // keeps the game's buffer referenced until restored
            ++m_count;
        }
    }
    if (m_count) m_ctx = ctx;
}

CbPatchScope::~CbPatchScope() {
    for (int i = 0; i < m_count; ++i) {
        m_ctx->RealCtx()->PSSetConstantBuffers(m_slots[i], 1, &m_saved[i]);
        m_saved[i]->Release();
    }
}

}  // namespace goh4k
