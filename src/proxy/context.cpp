// WrappedContext action overrides: forward to the real context, and while a trace is active record each
// action with the state it ran with (diag.cpp). In 2x mode (scale.h) viewports, scissors, copy boxes and
// ClearView rects follow the scale of the resources involved.
#include "wrappers.h"
#include "diag.h"
#include "scale.h"
#include "hbao.h"
#include "cbpatch.h"
#include <algorithm>
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)

namespace goh4k {

// ---------------------------------------------------------------- 2x state

void WrappedContext::ResetTrackedState() {
    m_psPatch = -1;
    m_numVp = m_numSc = 0;
    m_targetScale = m_appliedScale = 1;
    m_targetLogicalW = m_targetLogicalH = 0;
}

void WrappedContext::TargetsChanged(ID3D11RenderTargetView* const* rtvs, UINT numRtvs, ID3D11DepthStencilView* dsv,
                                    ID3D11UnorderedAccessView* const* uavs, UINT numUavs) {
    ID3D11View* first = nullptr;
    for (UINT i = 0; i < numRtvs && rtvs && !first; ++i) first = rtvs[i];
    if (!first) first = dsv;
    for (UINT i = 0; i < numUavs && uavs && !first; ++i) first = uavs[i];
    ScaleTag t = {1, 0, 0};
    UINT s = Scaling() ? ScaleOfView(first, &t) : 1;
    m_targetScale = s;
    m_targetLogicalW = t.logicalW;
    m_targetLogicalH = t.logicalH;
    if (s != m_appliedScale) {
        ApplyViewports();
        ApplyScissors();
    }
}

void WrappedContext::ApplyViewports() {
    UINT ts = Scaling() ? m_targetScale : 1;  // the targets may predate 2x being switched off
    m_appliedScale = ts;
    if (ts <= 1 || !m_numVp) return m_real->RSSetViewports(m_numVp, m_vp);
    D3D11_VIEWPORT v[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    float s = (float)ts;
    for (UINT i = 0; i < m_numVp; ++i) {
        v[i] = m_vp[i];
        // A viewport bigger than the target's logical size means the game already used real sizes.
        if (m_targetLogicalW && (m_vp[i].TopLeftX + m_vp[i].Width > m_targetLogicalW + 0.5f ||
                                 m_vp[i].TopLeftY + m_vp[i].Height > m_targetLogicalH + 0.5f)) {
            NoteOnce("viewport-already-real",
                     Fmt("viewport %gx%g on a target with logical size %ux%u left unscaled", m_vp[i].Width,
                         m_vp[i].Height, m_targetLogicalW, m_targetLogicalH));
            continue;
        }
        v[i].TopLeftX *= s;
        v[i].TopLeftY *= s;
        v[i].Width *= s;
        v[i].Height *= s;
    }
    m_real->RSSetViewports(m_numVp, v);
}

void WrappedContext::ApplyScissors() {
    UINT ts = Scaling() ? m_targetScale : 1;
    if (ts <= 1 || !m_numSc) return m_real->RSSetScissorRects(m_numSc, m_sc);
    D3D11_RECT r[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    LONG s = (LONG)ts;
    for (UINT i = 0; i < m_numSc; ++i)
        r[i] = {m_sc[i].left * s, m_sc[i].top * s, m_sc[i].right * s, m_sc[i].bottom * s};
    m_real->RSSetScissorRects(m_numSc, r);
}

void STDMETHODCALLTYPE WrappedContext::OMSetRenderTargets(UINT NumViews, ID3D11RenderTargetView* const* ppRenderTargetViews,
                                                         ID3D11DepthStencilView* pDepthStencilView) {
    m_real->OMSetRenderTargets(NumViews, ppRenderTargetViews, pDepthStencilView);
    if (ScaleConfigured()) TargetsChanged(ppRenderTargetViews, NumViews, pDepthStencilView, nullptr, 0);
}

void STDMETHODCALLTYPE WrappedContext::OMSetRenderTargetsAndUnorderedAccessViews(
    UINT NumRTVs, ID3D11RenderTargetView* const* ppRenderTargetViews, ID3D11DepthStencilView* pDepthStencilView,
    UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView* const* ppUnorderedAccessViews,
    const UINT* pUAVInitialCounts) {
    m_real->OMSetRenderTargetsAndUnorderedAccessViews(NumRTVs, ppRenderTargetViews, pDepthStencilView, UAVStartSlot,
                                                      NumUAVs, ppUnorderedAccessViews, pUAVInitialCounts);
    if (ScaleConfigured() && NumRTVs != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL)
        TargetsChanged(ppRenderTargetViews, NumRTVs, pDepthStencilView,
                       NumUAVs != D3D11_KEEP_UNORDERED_ACCESS_VIEWS ? ppUnorderedAccessViews : nullptr,
                       NumUAVs != D3D11_KEEP_UNORDERED_ACCESS_VIEWS ? NumUAVs : 0);
}

// The game sizes its screen viewport from the backbuffer texture's own GetDesc, which reports the real
// size. A viewport/scissor covering exactly the real screen is stored as the logical screen: on scaled
// targets that re-applies to the same real size, and on unscaled targets (UI label textures drawn with
// the stale screen viewport) it gives what native 1080p would have used.
static bool IsRealScreen(float x, float y, float w, float h) {
    UINT s = (UINT)g_cfg.scale, lw = g_logicalW, lh = g_logicalH;
    return lw && x == 0 && y == 0 && w == (float)(lw * s) && h == (float)(lh * s);
}

void STDMETHODCALLTYPE WrappedContext::RSSetViewports(UINT NumViewports, const D3D11_VIEWPORT* pViewports) {
    if (!ScaleConfigured()) return m_real->RSSetViewports(NumViewports, pViewports);
    m_numVp = std::min<UINT>(NumViewports, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
    if (pViewports) memcpy(m_vp, pViewports, m_numVp * sizeof(D3D11_VIEWPORT));
    else m_numVp = 0;
    for (UINT i = 0; i < m_numVp && Scaling() && !FromHbao(_ReturnAddress()); ++i)  // HBAO+ works at real size
        if (IsRealScreen(m_vp[i].TopLeftX, m_vp[i].TopLeftY, m_vp[i].Width, m_vp[i].Height)) {
            m_vp[i].Width = (float)g_logicalW;
            m_vp[i].Height = (float)g_logicalH;
        }
    ApplyViewports();
}

void STDMETHODCALLTYPE WrappedContext::RSSetScissorRects(UINT NumRects, const D3D11_RECT* pRects) {
    if (!ScaleConfigured()) return m_real->RSSetScissorRects(NumRects, pRects);
    m_numSc = std::min<UINT>(NumRects, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
    if (pRects) memcpy(m_sc, pRects, m_numSc * sizeof(D3D11_RECT));
    else m_numSc = 0;
    for (UINT i = 0; i < m_numSc && Scaling() && !FromHbao(_ReturnAddress()); ++i)
        if (IsRealScreen((float)m_sc[i].left, (float)m_sc[i].top, (float)(m_sc[i].right - m_sc[i].left),
                         (float)(m_sc[i].bottom - m_sc[i].top))) {
            m_sc[i].right = (LONG)g_logicalW;
            m_sc[i].bottom = (LONG)g_logicalH;
        }
    ApplyScissors();
}

void STDMETHODCALLTYPE WrappedContext::RSGetViewports(UINT* pNumViewports, D3D11_VIEWPORT* pViewports) {
    if (!Scaling() || !pNumViewports) return m_real->RSGetViewports(pNumViewports, pViewports);
    if (pViewports) {
        UINT n = std::min(*pNumViewports, m_numVp);
        memcpy(pViewports, m_vp, n * sizeof(D3D11_VIEWPORT));
        for (UINT i = n; i < *pNumViewports; ++i) pViewports[i] = {};
    }
    *pNumViewports = m_numVp;
}

void STDMETHODCALLTYPE WrappedContext::RSGetScissorRects(UINT* pNumRects, D3D11_RECT* pRects) {
    if (!Scaling() || !pNumRects) return m_real->RSGetScissorRects(pNumRects, pRects);
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

// ---------------------------------------------------------------- constant-buffer patches

void STDMETHODCALLTYPE WrappedContext::PSSetShader(ID3D11PixelShader* pPixelShader,
                                                  ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances) {
    m_real->PSSetShader(pPixelShader, ppClassInstances, NumClassInstances);
    if (Scaling()) m_psPatch = ShaderPatchIndex(pPixelShader);
}

HRESULT STDMETHODCALLTYPE WrappedContext::Map(ID3D11Resource* pResource, UINT Subresource, D3D11_MAP MapType,
                                             UINT MapFlags, D3D11_MAPPED_SUBRESOURCE* pMappedResource) {
    pResource = UnwrapResource(pResource);
    HRESULT hr = m_real->Map(pResource, Subresource, MapType, MapFlags, pMappedResource);
    if (SUCCEEDED(hr) && pMappedResource && MapType != D3D11_MAP_READ && IsTrackedCb(pResource)) {
        m_mappedCb = pResource;
        m_mappedCbData = pMappedResource->pData;
    }
    return hr;
}

void STDMETHODCALLTYPE WrappedContext::Unmap(ID3D11Resource* pResource, UINT Subresource) {
    pResource = UnwrapResource(pResource);
    if (m_mappedCb && pResource == m_mappedCb) {
        TrackCbMapped(pResource, m_mappedCbData);
        m_mappedCb = nullptr;
    }
    m_real->Unmap(pResource, Subresource);
}

static std::string Pair(ID3D11Resource* dst, ID3D11Resource* src) { return "dst " + ResTag(dst) + " src " + ResTag(src); }

// Arguments are only formatted while a trace is active.
#define TRACE_ACTION(kind, ...) \
    if (TraceActive()) TraceRecord(this, kind, Fmt(__VA_ARGS__))
#define TRACE_AFTER() \
    if (TraceActive()) TraceAfterAction(this)

static std::string BoxStr(const D3D11_BOX* b) {
    return b ? Fmt("box=%u,%u-%u,%u", b->left, b->top, b->right, b->bottom) : std::string("box=all");
}

void STDMETHODCALLTYPE WrappedContext::DrawIndexed(UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation) {
    CbPatchScope cbp(this, m_psPatch, m_targetScale);
    TRACE_ACTION(TraceKind::Draw, "DrawIndexed(%u,%u,%d)", IndexCount, StartIndexLocation, BaseVertexLocation);
    m_real->DrawIndexed(IndexCount, StartIndexLocation, BaseVertexLocation);
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::Draw(UINT VertexCount, UINT StartVertexLocation) {
    CbPatchScope cbp(this, m_psPatch, m_targetScale);
    TRACE_ACTION(TraceKind::Draw, "Draw(%u,%u)", VertexCount, StartVertexLocation);
    m_real->Draw(VertexCount, StartVertexLocation);
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount,
                                                           UINT StartIndexLocation, INT BaseVertexLocation,
                                                           UINT StartInstanceLocation) {
    CbPatchScope cbp(this, m_psPatch, m_targetScale);
    TRACE_ACTION(TraceKind::Draw, "DrawIndexedInstanced(%u,%u)", IndexCountPerInstance, InstanceCount);
    m_real->DrawIndexedInstanced(IndexCountPerInstance, InstanceCount, StartIndexLocation, BaseVertexLocation,
                                 StartInstanceLocation);
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::DrawInstanced(UINT VertexCountPerInstance, UINT InstanceCount,
                                                    UINT StartVertexLocation, UINT StartInstanceLocation) {
    CbPatchScope cbp(this, m_psPatch, m_targetScale);
    TRACE_ACTION(TraceKind::Draw, "DrawInstanced(%u,%u)", VertexCountPerInstance, InstanceCount);
    m_real->DrawInstanced(VertexCountPerInstance, InstanceCount, StartVertexLocation, StartInstanceLocation);
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::DrawAuto() {
    CbPatchScope cbp(this, m_psPatch, m_targetScale);
    TRACE_ACTION(TraceKind::Draw, "DrawAuto()");
    m_real->DrawAuto();
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::DrawIndexedInstancedIndirect(ID3D11Buffer* pBufferForArgs,
                                                                   UINT AlignedByteOffsetForArgs) {
    CbPatchScope cbp(this, m_psPatch, m_targetScale);
    TRACE_ACTION(TraceKind::Draw, "DrawIndexedInstancedIndirect");
    m_real->DrawIndexedInstancedIndirect(pBufferForArgs, AlignedByteOffsetForArgs);
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::DrawInstancedIndirect(ID3D11Buffer* pBufferForArgs, UINT AlignedByteOffsetForArgs) {
    CbPatchScope cbp(this, m_psPatch, m_targetScale);
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
    pDstResource = UnwrapResource(pDstResource);
    pSrcResource = UnwrapResource(pSrcResource);
    TRACE_ACTION(TraceKind::Plain, "CopySubresourceRegion dst=%s/%u at %u,%u src=%s/%u %s", ResTag(pDstResource).c_str(),
                 DstSubresource, DstX, DstY, ResTag(pSrcResource).c_str(), SrcSubresource, BoxStr(pSrcBox).c_str());
    if (Scaling()) {
        UINT ds = ScaleOf(pDstResource), ss = ScaleOf(pSrcResource);
        if (ds != ss) NoteOnce("copy-region-mixed-scale", Pair(pDstResource, pSrcResource));
        if (ds > 1 && ds == ss) {
            D3D11_BOX b;
            if (pSrcBox) b = {pSrcBox->left * ds, pSrcBox->top * ds, pSrcBox->front, pSrcBox->right * ds,
                              pSrcBox->bottom * ds, pSrcBox->back};
            m_real->CopySubresourceRegion(pDstResource, DstSubresource, DstX * ds, DstY * ds, DstZ, pSrcResource,
                                          SrcSubresource, pSrcBox ? &b : nullptr);
            TRACE_AFTER();
            return;
        }
    }
    m_real->CopySubresourceRegion(pDstResource, DstSubresource, DstX, DstY, DstZ, pSrcResource, SrcSubresource, pSrcBox);
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::CopySubresourceRegion1(ID3D11Resource* pDstResource, UINT DstSubresource,
                                                             UINT DstX, UINT DstY, UINT DstZ,
                                                             ID3D11Resource* pSrcResource, UINT SrcSubresource,
                                                             const D3D11_BOX* pSrcBox, UINT CopyFlags) {
    pDstResource = UnwrapResource(pDstResource);
    pSrcResource = UnwrapResource(pSrcResource);
    TRACE_ACTION(TraceKind::Plain, "CopySubresourceRegion1 dst=%s/%u at %u,%u src=%s/%u %s", ResTag(pDstResource).c_str(),
                 DstSubresource, DstX, DstY, ResTag(pSrcResource).c_str(), SrcSubresource, BoxStr(pSrcBox).c_str());
    if (Scaling()) {
        UINT ds = ScaleOf(pDstResource), ss = ScaleOf(pSrcResource);
        if (ds != ss) NoteOnce("copy-region-mixed-scale", Pair(pDstResource, pSrcResource));
        if (ds > 1 && ds == ss) {
            D3D11_BOX b;
            if (pSrcBox) b = {pSrcBox->left * ds, pSrcBox->top * ds, pSrcBox->front, pSrcBox->right * ds,
                              pSrcBox->bottom * ds, pSrcBox->back};
            m_real->CopySubresourceRegion1(pDstResource, DstSubresource, DstX * ds, DstY * ds, DstZ, pSrcResource,
                                           SrcSubresource, pSrcBox ? &b : nullptr, CopyFlags);
            TRACE_AFTER();
            return;
        }
    }
    m_real->CopySubresourceRegion1(pDstResource, DstSubresource, DstX, DstY, DstZ, pSrcResource, SrcSubresource,
                                   pSrcBox, CopyFlags);
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::CopyResource(ID3D11Resource* pDstResource, ID3D11Resource* pSrcResource) {
    pDstResource = UnwrapResource(pDstResource);
    pSrcResource = UnwrapResource(pSrcResource);
    TRACE_ACTION(TraceKind::Plain, "CopyResource dst=%s src=%s", ResTag(pDstResource).c_str(),
                 ResTag(pSrcResource).c_str());
    if (Scaling() && ScaleOf(pDstResource) != ScaleOf(pSrcResource))
        NoteOnce("copy-resource-mixed-scale", Pair(pDstResource, pSrcResource));
    m_real->CopyResource(pDstResource, pSrcResource);
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::UpdateSubresource(ID3D11Resource* pDstResource, UINT DstSubresource,
                                                        const D3D11_BOX* pDstBox, const void* pSrcData,
                                                        UINT SrcRowPitch, UINT SrcDepthPitch) {
    pDstResource = UnwrapResource(pDstResource);
    TrackCbUpdate(pDstResource, pDstBox, pSrcData);
    TRACE_ACTION(TraceKind::Plain, "UpdateSubresource dst=%s/%u %s", ResTag(pDstResource).c_str(), DstSubresource,
                 BoxStr(pDstBox).c_str());
    if (Scaling() && ScaleOf(pDstResource) > 1) NoteOnce("update-scaled-resource", "dst " + ResTag(pDstResource));
    m_real->UpdateSubresource(pDstResource, DstSubresource, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
    TRACE_AFTER();
}

void STDMETHODCALLTYPE WrappedContext::UpdateSubresource1(ID3D11Resource* pDstResource, UINT DstSubresource,
                                                         const D3D11_BOX* pDstBox, const void* pSrcData,
                                                         UINT SrcRowPitch, UINT SrcDepthPitch, UINT CopyFlags) {
    pDstResource = UnwrapResource(pDstResource);
    TrackCbUpdate(pDstResource, pDstBox, pSrcData);
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
    if (Scaling() && pRect && NumRects && NumRects <= 64) {
        UINT s = ScaleOfView(pView);
        if (s > 1) {
            D3D11_RECT r[64];
            for (UINT i = 0; i < NumRects; ++i)
                r[i] = {pRect[i].left * (LONG)s, pRect[i].top * (LONG)s, pRect[i].right * (LONG)s, pRect[i].bottom * (LONG)s};
            m_real->ClearView(pView, Color, r, NumRects);
            TRACE_AFTER();
            return;
        }
    }
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
    pDstResource = UnwrapResource(pDstResource);
    pSrcResource = UnwrapResource(pSrcResource);
    TRACE_ACTION(TraceKind::Plain, "ResolveSubresource dst=%s/%u src=%s/%u fmt=%d", ResTag(pDstResource).c_str(),
                 DstSubresource, ResTag(pSrcResource).c_str(), SrcSubresource, (int)Format);
    if (Scaling() && ScaleOf(pDstResource) != ScaleOf(pSrcResource))
        NoteOnce("resolve-mixed-scale", Pair(pDstResource, pSrcResource));
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
