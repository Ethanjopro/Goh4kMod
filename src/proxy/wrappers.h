// COM wrappers for the game's device, contexts and swapchain. The generated Fwd_* bases forward every
// method to the real object; these classes override only what the proxy needs.
//
// Reference counting is forwarded: AddRef/Release go to the real object, and a wrapper deletes itself
// when the real count reaches 0. The wrappers keep no references of their own (interface pointers from
// QueryInterface are released right away and kept raw), so the game's references are the only ones.
// The immediate context's wrapper lives as long as the device wrapper.
#pragma once
#include "common.h"
#include "gen/fwd_device.h"
#include "gen/fwd_context.h"
#include "gen/fwd_swapchain.h"
#include <atomic>
#include <string>
#include <vector>

namespace goh4k {

class WrappedDevice;
struct UiState;

// Rule 4 (context.cpp, screenshots): true if d is the game's screenshot readback texture made at the logical
// screen size; *real is then d at the real size.
bool ScreenshotReadbackSize(const D3D11_TEXTURE2D_DESC& d, D3D11_TEXTURE2D_DESC* real);
// Buffer-size guard (swapchain.cpp): a swapchain size request for the game window at exactly the logical size
// while its real client still covers the monitor at k > 1 becomes the real client size. False: leave it.
bool RealBufferSize(HWND hwnd, UINT* width, UINT* height, const char* call);

class WrappedContext final : public Fwd_ID3D11DeviceContext4 {
public:
    WrappedContext(ID3D11DeviceContext* real, WrappedDevice* device, bool immediate);
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    void STDMETHODCALLTYPE GetDevice(ID3D11Device** ppDevice) override;

    // Actions: recorded while a trace is active (context.cpp).
    void STDMETHODCALLTYPE DrawIndexed(UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation) override;
    void STDMETHODCALLTYPE Draw(UINT VertexCount, UINT StartVertexLocation) override;
    void STDMETHODCALLTYPE DrawIndexedInstanced(UINT IndexCountPerInstance, UINT InstanceCount, UINT StartIndexLocation,
                                                INT BaseVertexLocation, UINT StartInstanceLocation) override;
    void STDMETHODCALLTYPE DrawInstanced(UINT VertexCountPerInstance, UINT InstanceCount, UINT StartVertexLocation,
                                         UINT StartInstanceLocation) override;
    void STDMETHODCALLTYPE DrawAuto() override;
    void STDMETHODCALLTYPE DrawIndexedInstancedIndirect(ID3D11Buffer* pBufferForArgs, UINT AlignedByteOffsetForArgs) override;
    void STDMETHODCALLTYPE DrawInstancedIndirect(ID3D11Buffer* pBufferForArgs, UINT AlignedByteOffsetForArgs) override;
    void STDMETHODCALLTYPE Dispatch(UINT ThreadGroupCountX, UINT ThreadGroupCountY, UINT ThreadGroupCountZ) override;
    void STDMETHODCALLTYPE DispatchIndirect(ID3D11Buffer* pBufferForArgs, UINT AlignedByteOffsetForArgs) override;
    void STDMETHODCALLTYPE CopySubresourceRegion(ID3D11Resource* pDstResource, UINT DstSubresource, UINT DstX, UINT DstY,
                                                 UINT DstZ, ID3D11Resource* pSrcResource, UINT SrcSubresource,
                                                 const D3D11_BOX* pSrcBox) override;
    void STDMETHODCALLTYPE CopySubresourceRegion1(ID3D11Resource* pDstResource, UINT DstSubresource, UINT DstX, UINT DstY,
                                                  UINT DstZ, ID3D11Resource* pSrcResource, UINT SrcSubresource,
                                                  const D3D11_BOX* pSrcBox, UINT CopyFlags) override;
    void STDMETHODCALLTYPE CopyResource(ID3D11Resource* pDstResource, ID3D11Resource* pSrcResource) override;
    void STDMETHODCALLTYPE UpdateSubresource(ID3D11Resource* pDstResource, UINT DstSubresource, const D3D11_BOX* pDstBox,
                                             const void* pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch) override;
    void STDMETHODCALLTYPE UpdateSubresource1(ID3D11Resource* pDstResource, UINT DstSubresource, const D3D11_BOX* pDstBox,
                                              const void* pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch,
                                              UINT CopyFlags) override;
    void STDMETHODCALLTYPE ClearRenderTargetView(ID3D11RenderTargetView* pRenderTargetView,
                                                 const FLOAT ColorRGBA[4]) override;
    void STDMETHODCALLTYPE ClearUnorderedAccessViewUint(ID3D11UnorderedAccessView* pUnorderedAccessView,
                                                        const UINT Values[4]) override;
    void STDMETHODCALLTYPE ClearUnorderedAccessViewFloat(ID3D11UnorderedAccessView* pUnorderedAccessView,
                                                         const FLOAT Values[4]) override;
    void STDMETHODCALLTYPE ClearDepthStencilView(ID3D11DepthStencilView* pDepthStencilView, UINT ClearFlags, FLOAT Depth,
                                                 UINT8 Stencil) override;
    void STDMETHODCALLTYPE ClearView(ID3D11View* pView, const FLOAT Color[4], const D3D11_RECT* pRect,
                                     UINT NumRects) override;
    void STDMETHODCALLTYPE GenerateMips(ID3D11ShaderResourceView* pShaderResourceView) override;
    void STDMETHODCALLTYPE ResolveSubresource(ID3D11Resource* pDstResource, UINT DstSubresource,
                                              ID3D11Resource* pSrcResource, UINT SrcSubresource,
                                              DXGI_FORMAT Format) override;
    void STDMETHODCALLTYPE ExecuteCommandList(ID3D11CommandList* pCommandList, BOOL RestoreContextState) override;
    HRESULT STDMETHODCALLTYPE FinishCommandList(BOOL RestoreDeferredContextState,
                                                ID3D11CommandList** ppCommandList) override;

    // Native-scaling state (context.cpp): the game's viewports/scissors are kept as set and re-applied with
    // the full-screen rules whenever the bound target changes class. Get* return the game's values.
    void STDMETHODCALLTYPE OMSetRenderTargets(UINT NumViews, ID3D11RenderTargetView* const* ppRenderTargetViews,
                                              ID3D11DepthStencilView* pDepthStencilView) override;
    void STDMETHODCALLTYPE OMSetRenderTargetsAndUnorderedAccessViews(
        UINT NumRTVs, ID3D11RenderTargetView* const* ppRenderTargetViews, ID3D11DepthStencilView* pDepthStencilView,
        UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView* const* ppUnorderedAccessViews,
        const UINT* pUAVInitialCounts) override;
    void STDMETHODCALLTYPE RSSetViewports(UINT NumViewports, const D3D11_VIEWPORT* pViewports) override;
    void STDMETHODCALLTYPE RSSetScissorRects(UINT NumRects, const D3D11_RECT* pRects) override;
    void STDMETHODCALLTYPE RSGetViewports(UINT* pNumViewports, D3D11_VIEWPORT* pViewports) override;
    void STDMETHODCALLTYPE RSGetScissorRects(UINT* pNumRects, D3D11_RECT* pRects) override;
    void STDMETHODCALLTYPE ClearState() override;
    // Which vertex shader is bound: UI draws are recognised by it (context.cpp).
    void STDMETHODCALLTYPE VSSetShader(ID3D11VertexShader* pVertexShader, ID3D11ClassInstance* const* ppClassInstances,
                                       UINT NumClassInstances) override;

    int Level() const { return m_level; }
    bool IsImmediate() const { return m_immediate; }
    int Id() const { return m_id; }
    ID3D11DeviceContext4* RealCtx() const { return m_real; }
    WrappedDevice* Device() const { return m_device; }

    // Trace buffer of a deferred context (diag.cpp); only touched by the thread recording on it.
    std::vector<std::string> traceLines;
    int traceGen = -1;

private:
    void ResetTrackedState();
    void UiTargetsChanged(ID3D11RenderTargetView* const* rtvs, UINT numRtvs, ID3D11DepthStencilView* dsv);
    void UiApplyViewports();
    void UiApplyScissors();
    bool UiBeforeDraw();
    void UiAfterDraw();
    void UiCheckImageDraw(UINT indexCount, UINT startIndex, INT baseVertex, bool standIn);
    void UiFollowProjection();

    WrappedDevice* m_device;
    bool m_immediate;
    int m_level = 0;  // highest ID3D11DeviceContextN the real object supports
    int m_id = 0;     // 0 = immediate, 1.. = deferred contexts in creation order

    D3D11_VIEWPORT m_vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};  // as the game set them
    UINT m_numVp = 0;
    D3D11_RECT m_sc[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] = {};
    UINT m_numSc = 0;
    int m_uiTarget = 0;            // bound target class (context.cpp UiTarget)
    bool m_uiVpSwapped = false;    // a full-screen viewport is applied swapped right now
    bool m_uiRealProjection = false;  // the UI projection at the last label draw was the real screen (rule 1 off)
    ID3D11VertexShader* m_vs = nullptr;  // bound vertex shader (no reference)
    uint64_t m_vsHash = 0;               // its id (diag.h ShaderHash), looked up on first need after a bind
    bool m_vsHashKnown = false;
    ID3D11ShaderResourceView* m_uiSavedPs0 = nullptr;  // the game's slot-0 view while rule 3's stand-in is bound
    bool m_uiImageDraw = false;  // the current draw is a UI draw of a screen-sized image (stand-in or not)
};

class WrappedDevice final : public Fwd_ID3D11Device5 {
public:
    WrappedDevice(ID3D11Device* real, ID3D11DeviceContext* realImmediate);
    ~WrappedDevice();
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    void STDMETHODCALLTYPE GetImmediateContext(ID3D11DeviceContext** ppImmediateContext) override;
    void STDMETHODCALLTYPE GetImmediateContext1(ID3D11DeviceContext1** ppImmediateContext) override;
    void STDMETHODCALLTYPE GetImmediateContext2(ID3D11DeviceContext2** ppImmediateContext) override;
    void STDMETHODCALLTYPE GetImmediateContext3(ID3D11DeviceContext3** ppImmediateContext) override;
    HRESULT STDMETHODCALLTYPE CreateDeferredContext(UINT ContextFlags, ID3D11DeviceContext** ppDeferredContext) override;
    HRESULT STDMETHODCALLTYPE CreateDeferredContext1(UINT ContextFlags, ID3D11DeviceContext1** ppDeferredContext) override;
    HRESULT STDMETHODCALLTYPE CreateDeferredContext2(UINT ContextFlags, ID3D11DeviceContext2** ppDeferredContext) override;
    HRESULT STDMETHODCALLTYPE CreateDeferredContext3(UINT ContextFlags, ID3D11DeviceContext3** ppDeferredContext) override;

    // Resource and shader creation: recorded for stats/trace when diagnostics are on (device.cpp).
    HRESULT STDMETHODCALLTYPE CreateTexture2D(const D3D11_TEXTURE2D_DESC* pDesc, const D3D11_SUBRESOURCE_DATA* pInitialData,
                                              ID3D11Texture2D** ppTexture2D) override;
    HRESULT STDMETHODCALLTYPE CreateTexture2D1(const D3D11_TEXTURE2D_DESC1* pDesc1,
                                               const D3D11_SUBRESOURCE_DATA* pInitialData,
                                               ID3D11Texture2D1** ppTexture2D) override;
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const void* pShaderBytecode, SIZE_T BytecodeLength,
                                                 ID3D11ClassLinkage* pClassLinkage,
                                                 ID3D11VertexShader** ppVertexShader) override;
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const void* pShaderBytecode, SIZE_T BytecodeLength,
                                                ID3D11ClassLinkage* pClassLinkage,
                                                ID3D11PixelShader** ppPixelShader) override;
    HRESULT STDMETHODCALLTYPE CreateGeometryShader(const void* pShaderBytecode, SIZE_T BytecodeLength,
                                                   ID3D11ClassLinkage* pClassLinkage,
                                                   ID3D11GeometryShader** ppGeometryShader) override;
    HRESULT STDMETHODCALLTYPE CreateGeometryShaderWithStreamOutput(
        const void* pShaderBytecode, SIZE_T BytecodeLength, const D3D11_SO_DECLARATION_ENTRY* pSODeclaration,
        UINT NumEntries, const UINT* pBufferStrides, UINT NumStrides, UINT RasterizedStream,
        ID3D11ClassLinkage* pClassLinkage, ID3D11GeometryShader** ppGeometryShader) override;
    HRESULT STDMETHODCALLTYPE CreateHullShader(const void* pShaderBytecode, SIZE_T BytecodeLength,
                                               ID3D11ClassLinkage* pClassLinkage, ID3D11HullShader** ppHullShader) override;
    HRESULT STDMETHODCALLTYPE CreateDomainShader(const void* pShaderBytecode, SIZE_T BytecodeLength,
                                                 ID3D11ClassLinkage* pClassLinkage,
                                                 ID3D11DomainShader** ppDomainShader) override;
    HRESULT STDMETHODCALLTYPE CreateComputeShader(const void* pShaderBytecode, SIZE_T BytecodeLength,
                                                  ID3D11ClassLinkage* pClassLinkage,
                                                  ID3D11ComputeShader** ppComputeShader) override;

    WrappedContext* Immediate() { return m_immediate; }
    ID3D11Device* Real() { return m_real; }

    // Rule 3's stand-in (context.cpp): the image copied into it on ctx, returned AddRef'd, or null.
    ID3D11ShaderResourceView* UiStandIn(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* gameSrv, ID3D11Texture2D* img,
                                        const UiState& s);
    // Releases the stand-ins unused for at least that long (0: all). They hold the device: the game window's
    // Present releases idle ones, its swapchain's final release all of them.
    static const ULONGLONG kStandInIdleMs = 5000;
    void ReleaseStandIns(ULONGLONG unusedForMs);

private:
    struct StandIn {
        ID3D11Texture2D* tex = nullptr;
        ID3D11ShaderResourceView* srv = nullptr;
        UINT w = 0, h = 0;
        DXGI_FORMAT texFmt = DXGI_FORMAT_UNKNOWN, viewFmt = DXGI_FORMAT_UNKNOWN;
        ULONGLONG lastUse = 0;
    };
    WrappedContext* AdoptImmediate(ID3D11DeviceContext* real);
    WrappedContext* WrapDeferred(ID3D11DeviceContext* real);
    void ReleaseStandIn(StandIn* e, const char* why);  // m_standInLock held

    WrappedContext* m_immediate = nullptr;
    SRWLOCK m_lock = SRWLOCK_INIT;
    int m_level = 0;  // highest ID3D11DeviceN the real object supports
    LONG m_deferredCount = 0;
    StandIn m_standIns[2];  // one per size/format in use; two in case two formats alternate
    SRWLOCK m_standInLock = SRWLOCK_INIT;
    std::atomic<ULONGLONG> m_standInFailedAt{0};  // when a stand-in last couldn't be created (0: never)
};

class WrappedSwapChain final : public Fwd_IDXGISwapChain4 {
public:
    WrappedSwapChain(IDXGISwapChain* real, WrappedDevice* device);
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** ppDevice) override;
    HRESULT STDMETHODCALLTYPE Present(UINT SyncInterval, UINT Flags) override;
    HRESULT STDMETHODCALLTYPE Present1(UINT SyncInterval, UINT PresentFlags,
                                       const DXGI_PRESENT_PARAMETERS* pPresentParameters) override;
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL Fullscreen, IDXGIOutput* pTarget) override;
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT NewFormat,
                                            UINT SwapChainFlags) override;
    HRESULT STDMETHODCALLTYPE ResizeBuffers1(UINT BufferCount, UINT Width, UINT Height, DXGI_FORMAT Format,
                                             UINT SwapChainFlags, const UINT* pCreationNodeMask,
                                             IUnknown* const* ppPresentQueue) override;
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* pNewTargetParameters) override;
    HRESULT STDMETHODCALLTYPE SetColorSpace1(DXGI_COLOR_SPACE_TYPE ColorSpace) override;
    HRESULT STDMETHODCALLTYPE SetHDRMetaData(DXGI_HDR_METADATA_TYPE Type, UINT Size, void* pMetaData) override;

private:
    void AfterPresent(HRESULT hr, UINT flags);
    void WriteStatus(ULONGLONG now);
    void LogState(const char* why);
    void UpdateUiScale(const char* why);
    void ResyncAfterModeChange();

    WrappedDevice* m_device;
    HWND m_hwnd = nullptr;  // its output window
    bool m_followed = false;  // the mod followed m_hwnd as the game window at the last Present
    int m_level = 0;  // highest IDXGISwapChainN the real object supports
    LONG64 m_frames = 0;
    ULONGLONG m_checkTick = 0;  // last window/hook check (input.h CheckGameWindow)
    LONG64 m_framesAtStatus = 0;
    ULONGLONG m_statusTick = 0;
    double m_fps = 0;
    int m_colorSpace = -1;
};

}  // namespace goh4k
