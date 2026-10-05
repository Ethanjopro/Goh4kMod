// WrappedContext action overrides: forward to the real context, and while a trace is active record each
// action with the state it ran with (diag.cpp). For native scaling (scale.h) a few full-screen viewports and
// scissors are applied differently from how the game set them; see "full-screen rules" below.
#include "wrappers.h"
#include "diag.h"
#include "scale.h"
#include "hbao.h"
#include <algorithm>
#include <atomic>
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)

namespace goh4k {

void WrappedContext::ResetTrackedState() {
    m_numVp = m_numSc = 0;
    m_uiTarget = 0;
    m_uiVpSwapped = m_uiVpFromHbao = m_uiScFromHbao = false;
    m_vs = nullptr;
    m_vsHashKnown = false;
}

// ---------------------------------------------------------------- full-screen rules
// The game's render targets follow its resolution setting (real), its UI projection uses the client size it
// was told (logical), and its viewports come from either: some from the backbuffer's desc (the real screen),
// some from the client size (the logical screen). Three mismatches follow, fixed on full-screen rects only:
// 1. UI label surfaces (render targets smaller than the screen) are drawn with the stale real-screen
//    viewport, so the glyphs come out magnified and clipped: applied as the logical screen, as native 1080p
//    does.
// 2. Tone-map/glow passes on screen-sized targets set the logical-screen viewport, so the image covers only
//    the top-left part: applied as the real screen. Stock native 4K never sets a 1920x1080 viewport on a
//    3840x2160 target (captures/20261002-122821-mission-trace.txt).
// 3. Screen-sized textures the UI draws as images (the blurred backdrop behind prompts) are sampled with
//    coordinates of logical / texture size, so only their top-left part shows, magnified. Such textures are
//    marked when a UI draw samples one (UiBeforeDraw); from then on they are treated like rule 1, so the
//    game's full-screen pass into them fills exactly that part.
// Other rects, other targets and HBAO+ are left alone; RSGet* return the game's own values. "fix nouivp on"
// switches all three off for A/B.

enum UiTarget { kUiOther = 0, kUiBelow = 1, kUiScreen = 2, kUiCopy = 3 };

static int UiTargetClass(ID3D11View* v, bool render) {
    if (!v) return kUiOther;
    ID3D11Resource* r = nullptr;
    v->GetResource(&r);
    int c = kUiOther;
    ID3D11Texture2D* t = nullptr;
    if (r && SUCCEEDED(r->QueryInterface(IID_PPV_ARGS(&t)))) {
        D3D11_TEXTURE2D_DESC d;
        t->GetDesc(&d);
        UINT rw = g_uiRealW, rh = g_uiRealH;
        if (render && IsUiScreenCopy(r)) c = kUiCopy;
        else if (d.Width == rw && d.Height == rh) c = kUiScreen;
        else if (render && (d.Width < rw || d.Height < rh)) c = kUiBelow;
        t->Release();
    }
    if (r) r->Release();
    return c;
}

void WrappedContext::UiTargetsChanged(ID3D11RenderTargetView* const* rtvs, UINT numRtvs, ID3D11DepthStencilView* dsv) {
    ID3D11RenderTargetView* rt0 = rtvs && numRtvs ? rtvs[0] : nullptr;
    int c = rt0 ? UiTargetClass(rt0, true) : UiTargetClass(dsv, false);
    if (c != m_uiTarget) {
        m_uiTarget = c;
        if (m_numVp) UiApplyViewports();
        if (m_numSc) UiApplyScissors();
    }
}

// The full-screen size to swap on the bound target, and what it becomes.
static bool UiSwap(int target, UINT* fromW, UINT* fromH, UINT* toW, UINT* toH) {
    if (target == kUiOther || !UiScaleActive()) return false;
    Ratio q = UiScaleRatio();
    UINT rw = g_uiRealW, rh = g_uiRealH, lw = (UINT)RatioDown((long)rw, q), lh = (UINT)RatioDown((long)rh, q);
    if (target == kUiScreen) *fromW = lw, *fromH = lh, *toW = rw, *toH = rh;
    else *fromW = rw, *fromH = rh, *toW = lw, *toH = lh;
    return true;
}

static const char* UiRuleName(int target, bool scissor) {
    switch (target) {
        case kUiBelow: return scissor ? "ui-label-scissor" : "ui-label-viewport";
        case kUiScreen: return scissor ? "ui-screen-scissor" : "ui-screen-viewport";
        default: return scissor ? "ui-copy-scissor" : "ui-copy-viewport";
    }
}

void WrappedContext::UiApplyViewports() {
    UINT fw = 0, fh = 0, tw = 0, th = 0;
    bool rule = m_numVp && !m_uiVpFromHbao && UiSwap(m_uiTarget, &fw, &fh, &tw, &th) && !FixOn("nouivp");
    D3D11_VIEWPORT v[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    bool changed = false;
    for (UINT i = 0; i < m_numVp; ++i) {
        v[i] = m_vp[i];
        if (rule && v[i].TopLeftX == 0 && v[i].TopLeftY == 0 && v[i].Width == (float)fw && v[i].Height == (float)fh) {
            v[i].Width = (float)tw;
            v[i].Height = (float)th;
            changed = true;
        }
    }
    m_uiVpSwapped = changed;
    if (changed) NoteOnce(UiRuleName(m_uiTarget, false), Fmt("full-screen viewport %ux%u applied as %ux%u", fw, fh, tw, th));
    m_real->RSSetViewports(m_numVp, m_numVp ? v : nullptr);
}

void WrappedContext::UiApplyScissors() {
    UINT fw = 0, fh = 0, tw = 0, th = 0;
    bool rule = m_numSc && !m_uiScFromHbao && UiSwap(m_uiTarget, &fw, &fh, &tw, &th) && !FixOn("nouivp");
    D3D11_RECT r[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    bool changed = false;
    for (UINT i = 0; i < m_numSc; ++i) {
        r[i] = m_sc[i];
        if (rule && r[i].left == 0 && r[i].top == 0 && r[i].right == (LONG)fw && r[i].bottom == (LONG)fh) {
            r[i].right = (LONG)tw;
            r[i].bottom = (LONG)th;
            changed = true;
        }
    }
    if (changed) NoteOnce(UiRuleName(m_uiTarget, true), Fmt("full-screen scissor %ux%u applied as %ux%u", fw, fh, tw, th));
    m_real->RSSetScissorRects(m_numSc, m_numSc ? r : nullptr);
}

// UI draws are recognised by their vertex shader, learnt from the indexed draws into label surfaces whose
// stale viewport rule 1 swapped (the game's UI draws quads with DrawIndexed; its post-processing uses Draw).
// By bytecode id, not pointer: the game creates the same UI shader more than once.
static std::atomic<uint64_t> g_uiVs[4];

static bool IsUiVs(uint64_t h) {
    for (auto& p : g_uiVs)
        if (h && p.load(std::memory_order_relaxed) == h) return true;
    return false;
}

static void LearnUiVs(uint64_t h) {
    if (!h || IsUiVs(h)) return;
    for (auto& p : g_uiVs) {
        uint64_t none = 0;
        if (p.compare_exchange_strong(none, h)) {
            Log("ui: vertex shader %016llx recognised as the UI's (it draws into label surfaces)", (unsigned long long)h);
            return;
        }
    }
}

// Before each indexed draw: learn the UI's vertex shader (rule 1 draws), or mark a screen-sized texture that a
// UI draw onto a screen-sized target samples as its image (rule 3).
void WrappedContext::UiBeforeDraw() {
    if (!m_vs || !UiScaleActive() || (m_uiTarget != kUiBelow && m_uiTarget != kUiScreen)) return;
    if (!m_vsHashKnown) m_vsHash = ShaderHash(m_vs), m_vsHashKnown = true;
    if (m_uiTarget == kUiBelow) {
        if (m_uiVpSwapped) LearnUiVs(m_vsHash);
        return;
    }
    if (!IsUiVs(m_vsHash)) return;
    ID3D11ShaderResourceView* srv = nullptr;
    m_real->PSGetShaderResources(0, 1, &srv);
    if (!srv) return;
    ID3D11Resource* r = nullptr;
    srv->GetResource(&r);
    srv->Release();
    ID3D11Texture2D* t = nullptr;
    if (r && SUCCEEDED(r->QueryInterface(IID_PPV_ARGS(&t)))) {
        D3D11_TEXTURE2D_DESC d;
        t->GetDesc(&d);
        if (d.Width == g_uiRealW && d.Height == g_uiRealH && (d.BindFlags & D3D11_BIND_RENDER_TARGET) && !IsUiScreenCopy(r)) {
            MarkUiScreenCopy(r);
            Log("ui: %ux%u fmt=%d texture drawn by the UI as an image; renders into it fill the logical part from now on",
                d.Width, d.Height, (int)d.Format);
        }
        t->Release();
    }
    if (r) r->Release();
}

void STDMETHODCALLTYPE WrappedContext::OMSetRenderTargets(UINT NumViews, ID3D11RenderTargetView* const* ppRenderTargetViews,
                                                         ID3D11DepthStencilView* pDepthStencilView) {
    m_real->OMSetRenderTargets(NumViews, ppRenderTargetViews, pDepthStencilView);
    if (UiScaleConfigured()) UiTargetsChanged(ppRenderTargetViews, NumViews, pDepthStencilView);
}

void STDMETHODCALLTYPE WrappedContext::OMSetRenderTargetsAndUnorderedAccessViews(
    UINT NumRTVs, ID3D11RenderTargetView* const* ppRenderTargetViews, ID3D11DepthStencilView* pDepthStencilView,
    UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView* const* ppUnorderedAccessViews,
    const UINT* pUAVInitialCounts) {
    m_real->OMSetRenderTargetsAndUnorderedAccessViews(NumRTVs, ppRenderTargetViews, pDepthStencilView, UAVStartSlot,
                                                      NumUAVs, ppUnorderedAccessViews, pUAVInitialCounts);
    if (UiScaleConfigured() && NumRTVs != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL)
        UiTargetsChanged(ppRenderTargetViews, NumRTVs, pDepthStencilView);
}

void STDMETHODCALLTYPE WrappedContext::RSSetViewports(UINT NumViewports, const D3D11_VIEWPORT* pViewports) {
    if (!UiScaleConfigured()) return m_real->RSSetViewports(NumViewports, pViewports);
    m_numVp = std::min<UINT>(NumViewports, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
    if (pViewports) memcpy(m_vp, pViewports, m_numVp * sizeof(D3D11_VIEWPORT));
    else m_numVp = 0;
    m_uiVpFromHbao = FromHbao(_ReturnAddress());
    UiApplyViewports();
}

void STDMETHODCALLTYPE WrappedContext::RSSetScissorRects(UINT NumRects, const D3D11_RECT* pRects) {
    if (!UiScaleConfigured()) return m_real->RSSetScissorRects(NumRects, pRects);
    m_numSc = std::min<UINT>(NumRects, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
    if (pRects) memcpy(m_sc, pRects, m_numSc * sizeof(D3D11_RECT));
    else m_numSc = 0;
    m_uiScFromHbao = FromHbao(_ReturnAddress());
    UiApplyScissors();
}

void STDMETHODCALLTYPE WrappedContext::RSGetViewports(UINT* pNumViewports, D3D11_VIEWPORT* pViewports) {
    if (!UiScaleConfigured() || !pNumViewports) return m_real->RSGetViewports(pNumViewports, pViewports);
    if (pViewports) {
        UINT n = std::min(*pNumViewports, m_numVp);
        memcpy(pViewports, m_vp, n * sizeof(D3D11_VIEWPORT));
        for (UINT i = n; i < *pNumViewports; ++i) pViewports[i] = {};
    }
    *pNumViewports = m_numVp;
}

void STDMETHODCALLTYPE WrappedContext::RSGetScissorRects(UINT* pNumRects, D3D11_RECT* pRects) {
    if (!UiScaleConfigured() || !pNumRects) return m_real->RSGetScissorRects(pNumRects, pRects);
    if (pRects) {
        UINT n = std::min(*pNumRects, m_numSc);
        memcpy(pRects, m_sc, n * sizeof(D3D11_RECT));
        for (UINT i = n; i < *pNumRects; ++i) pRects[i] = {};
    }
    *pNumRects = m_numSc;
}

void STDMETHODCALLTYPE WrappedContext::ClearState() {
    m_real->ClearState();
    ResetTrackedState();
}

void STDMETHODCALLTYPE WrappedContext::VSSetShader(ID3D11VertexShader* pVertexShader,
                                                  ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances) {
    m_real->VSSetShader(pVertexShader, ppClassInstances, NumClassInstances);
    if (pVertexShader != m_vs) m_vs = pVertexShader, m_vsHashKnown = false;
}

// ---------------------------------------------------------------- actions

// Arguments are only formatted while a trace is active.
#define TRACE_ACTION(kind, ...) \
    if (TraceActive()) TraceRecord(this, kind, Fmt(__VA_ARGS__))
#define TRACE_AFTER() \
    if (TraceActive()) TraceAfterAction(this)

static std::string BoxStr(const D3D11_BOX* b) {
    return b ? Fmt("box=%u,%u-%u,%u", b->left, b->top, b->right, b->bottom) : std::string("box=all");
}

void STDMETHODCALLTYPE WrappedContext::DrawIndexed(UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation) {
    if (UiScaleConfigured()) UiBeforeDraw();
    TRACE_ACTION(TraceKind::Draw, "DrawIndexed(%u,%u,%d)", IndexCount, StartIndexLocation, BaseVertexLocation);
    m_real->DrawIndexed(IndexCount, StartIndexLocation, BaseVertexLocation);
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::Draw(UINT VertexCount, UINT StartVertexLocation) {
    TRACE_ACTION(TraceKind::Draw, "Draw(%u,%u)", VertexCount, StartVertexLocation);
    m_real->Draw(VertexCount, StartVertexLocation);
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount,
                                                           UINT StartIndexLocation, INT BaseVertexLocation,
                                                           UINT StartInstanceLocation) {
    TRACE_ACTION(TraceKind::Draw, "DrawIndexedInstanced(%u,%u)", IndexCountPerInstance, InstanceCount);
    m_real->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation,
                                 StartInstanceLocation);
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::DrawInstanced(UINT VertexCountPerInstance, UINT InstanceCount,
                                                    UINT StartVertexLocation, UINT StartInstanceLocation) {
    TRACE_ACTION(TraceKind::Draw, "DrawInstanced(%u,%u)", VertexCountPerInstance, InstanceCount);
    m_real->DrawInstanced(VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::DrawAuto() {
    TRACE_ACTION(TraceKind::Draw, "DrawAuto()");
    m_real->DrawAuto();
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::DrawIndexedInstancedIndirect(ID3D11Buffer* pBufferForArgs,
                                                                   UINT AlignedByteOffsetForArgs) {
    TRACE_ACTION(TraceKind::Draw, "DrawIndexedInstancedIndirect");
    m_real->DrawIndexedInstancedIndirect(pBufferForArgs, AlignedByteOffsetForArgs);
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::DrawInstancedIndirect(ID3D11Buffer* pBufferForArgs, UINT AlignedByteOffsetForArgs) {
    TRACE_ACTION(TraceKind::Draw, "DrawInstancedIndirect");
    m_real->DrawInstancedIndirect(pBufferForArgs, AlignedByteOffsetForArgs);
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::Dispatch(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ) {
    TRACE_ACTION(TraceKind::Dispatch, "Dispatch(%u,%u,%u)", ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
    m_real->Dispatch(ThreadGroupCountX, ThreadGroupCountY, ThreadGroupCountZ);
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::DispatchIndirect(ID3D11Buffer* pBufferForArgs, UINT AlignedByteOffsetForArgs) {
    TRACE_ACTION(TraceKind::Dispatch, "DispatchIndirect");
    m_real->DispatchIndirect(pBufferForArgs, AlignedByteOffsetForArgs);
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::CopySubresourceRegion(ID3D11Resource* pDstResource, UINT DstSubresource, UINT DstX,
                                                            UINT DstY, UINT DstZ, ID3D11Resource* pSrcResource,
                                                            UINT SrcSubresource, const D3D11_BOX* pSrcBox) {
    TRACE_ACTION(TraceKind::Plain, "CopySubresourceRegion dst=%s/%u at %u,%u src=%s/%u %s", ResTag(pDstResource).c_str(),
                 DstSubresource, DstX, DstY, ResTag(pSrcResource).c_str(), SrcSubresource, BoxStr(pSrcBox).c_str());
    m_real->CopySubresourceRegion(pDstResource, DstSubresource, DstX, DstY, DstZ, pSrcResource, SrcSubresource, pSrcBox);
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::CopySubresourceRegion1(ID3D11Resource* pDstResource, UINT DstSubresource,
                                                             UINT DstX, UINT DstY, UINT DstZ,
                                                             ID3D11Resource* pSrcResource, UINT SrcSubresource,
                                                             const D3D11_BOX* pSrcBox, UINT CopyFlags) {
    TRACE_ACTION(TraceKind::Plain, "CopySubresourceRegion1 dst=%s/%u at %u,%u src=%s/%u %s", ResTag(pDstResource).c_str(),
                 DstSubresource, DstX, DstY, ResTag(pSrcResource).c_str(), SrcSubresource, BoxStr(pSrcBox).c_str());
    m_real->CopySubresourceRegion1(pDstResource, DstSubresource, DstX, DstY, DstZ, pSrcResource, SrcSubresource,
                                   pSrcBox, CopyFlags);
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::CopyResource(ID3D11Resource* pDstResource, ID3D11Resource* pSrcResource) {
    TRACE_ACTION(TraceKind::Plain, "CopyResource dst=%s src=%s", ResTag(pDstResource).c_str(),
                 ResTag(pSrcResource).c_str());
    m_real->CopyResource(pDstResource, pSrcResource);
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::UpdateSubresource(ID3D11Resource* pDstResource, UINT DstSubresource,
                                                        const D3D11_BOX* pDstBox, const void* pSrcData,
                                                        UINT SrcRowPitch, UINT SrcDepthPitch) {
    TRACE_ACTION(TraceKind::Plain, "UpdateSubresource dst=%s/%u %s", ResTag(pDstResource).c_str(), DstSubresource,
                 BoxStr(pDstBox).c_str());
    m_real->UpdateSubresource(pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::UpdateSubresource1(ID3D11Resource* pDstResource, UINT DstSubresource,
                                                         const D3D11_BOX* pDstBox, const void* pSrcData,
                                                         UINT SrcRowPitch, UINT SrcDepthPitch, UINT CopyFlags) {
    TRACE_ACTION(TraceKind::Plain, "UpdateSubresource1 dst=%s/%u %s", ResTag(pDstResource).c_str(), DstSubresource,
                 BoxStr(pDstBox).c_str());
    m_real->UpdateSubresource1(pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch, CopyFlags);
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::ClearRenderTargetView(ID3D11RenderTargetView* pRenderTargetView,
                                                            const FLOAT ColorRGBA[4]) {
    TRACE_ACTION(TraceKind::Plain, "ClearRTV %s (%g,%g,%g,%g)", ViewTag(pRenderTargetView).c_str(), ColorRGBA[0],
                 ColorRGBA[1], ColorRGBA[2], ColorRGBA[3]);
    m_real->ClearRenderTargetView(pRenderTargetView, ColorRGBA);
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::ClearUnorderedAccessViewUint(ID3D11UnorderedAccessView* pUnorderedAccessView,
                                                                   const UINT Values[4]) {
    TRACE_ACTION(TraceKind::Plain, "ClearUAVUint %s", ViewTag(pUnorderedAccessView).c_str());
    m_real->ClearUnorderedAccessViewUint(pUnorderedAccessView, Values);
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::ClearUnorderedAccessViewFloat(ID3D11UnorderedAccessView* pUnorderedAccessView,
                                                                    const FLOAT Values[4]) {
    TRACE_ACTION(TraceKind::Plain, "ClearUAVFloat %s", ViewTag(pUnorderedAccessView).c_str());
    m_real->ClearUnorderedAccessViewFloat(pUnorderedAccessView, Values);
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::ClearDepthStencilView(ID3D11DepthStencilView* pDepthStencilView, UINT ClearFlags,
                                                            FLOAT Depth, UINT8 Stencil) {
    TRACE_ACTION(TraceKind::Plain, "ClearDSV %s flags=%u depth=%g stencil=%u", ViewTag(pDepthStencilView).c_str(),
                 ClearFlags, Depth, (unsigned)Stencil);
    m_real->ClearDepthStencilView(pDepthStencilView, ClearFlags, Depth, Stencil);
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::ClearView(ID3D11View* pView, const FLOAT Color[4], const D3D11_RECT* pRect,
                                                UINT NumRects) {
    TRACE_ACTION(TraceKind::Plain, "ClearView %s rects=%u", ViewTag(pView).c_str(), NumRects);
    m_real->ClearView(pView, Color, pRect, NumRects);
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::GenerateMips(ID3D11ShaderResourceView* pShaderResourceView) {
    TRACE_ACTION(TraceKind::Plain, "GenerateMips %s", ViewTag(pShaderResourceView).c_str());
    m_real->GenerateMips(pShaderResourceView);
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::ResolveSubresource(ID3D11Resource* pDstResource, UINT DstSubresource,
                                                         ID3D11Resource* pSrcResource, UINT SrcSubresource,
                                                         DXGI_FORMAT Format) {
    TRACE_ACTION(TraceKind::Plain, "ResolveSubresource dst=%s/%u src=%s/%u fmt=%d", ResTag(pDstResource).c_str(),
                 DstSubresource, ResTag(pSrcResource).c_str(), SrcSubresource, (int)Format);
    m_real->ResolveSubresource(pDstResource, DstSubresource, pSrcResource, SrcSubresource, Format);
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::ExecuteCommandList(ID3D11CommandList* pCommandList, BOOL RestoreContextState) {
    if (TraceActive()) {
        TraceRecord(this, TraceKind::Plain, Fmt("ExecuteCommandList(restore=%d)", RestoreContextState));
        if (m_immediate) TraceExecuteCommandList(pCommandList);
    }
    m_real->ExecuteCommandList(pCommandList, RestoreContextState);
    if (!RestoreContextState) ResetTrackedState();  // D3D11 clears the immediate context's state
    TRACE_AFTER();
}

HRESULT STDMETHODCALLTYPE WrappedContext::FinishCommandList(BOOL RestoreDeferredContextState,
                                                           ID3D11CommandList** ppCommandList) {
    HRESULT hr = m_real->FinishCommandList(RestoreDeferredContextState, ppCommandList);
    if (!RestoreDeferredContextState) ResetTrackedState();  // and the deferred context's
    if (SUCCEEDED(hr) && ppCommandList && *ppCommandList) TraceFinishCommandList(this, *ppCommandList);
    return hr;
}

}  // namespace goh4k
