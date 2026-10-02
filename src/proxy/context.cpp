// WrappedContext action overrides: forward to the real context, and while a trace is active record each
// action with the state it ran with (diag.cpp). M3 adds viewport/scissor/copy scaling here.
#include "wrappers.h"
#include "diag.h"

namespace goh4k {

// Arguments are only formatted while a trace is active.
#define TRACE_ACTION(kind, ...) \
    if (TraceActive()) TraceRecord(this, kind, Fmt(__VA_ARGS__))
#define TRACE_AFTER() \
    if (TraceActive()) TraceAfterAction(this)

static std::string BoxStr(const D3D11_BOX* b) {
    return b ? Fmt("box=%u,%u-%u,%u", b->left, b->top, b->right, b->bottom) : std::string("box=all");
}

void STDMETHODCALLTYPE WrappedContext::DrawIndexed(UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation) {
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
    TRACE_AFTER();
}

HRESULT STDMETHODCALLTYPE WrappedContext::FinishCommandList(BOOL RestoreDeferredContextState,
                                                           ID3D11CommandList** ppCommandList) {
    HRESULT hr = m_real->FinishCommandList(RestoreDeferredContextState, ppCommandList);
    if (SUCCEEDED(hr) && ppCommandList && *ppCommandList) TraceFinishCommandList(this, *ppCommandList);
    return hr;
}

}  // namespace goh4k
