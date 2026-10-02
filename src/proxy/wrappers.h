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

namespace goh4k {

class WrappedDevice;

class WrappedContext final : public Fwd_ID3D11DeviceContext4 {
public:
    WrappedContext(ID3D11DeviceContext* real, WrappedDevice* device, bool immediate);
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    void STDMETHODCALLTYPE GetDevice(ID3D11Device** ppDevice) override;

    int Level() const { return m_level; }

private:
    WrappedDevice* m_device;
    bool m_immediate;
    int m_level = 0;  // highest ID3D11DeviceContextN the real object supports
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

    WrappedContext* Immediate() { return m_immediate; }
    ID3D11Device* Real() { return m_real; }

private:
    WrappedContext* AdoptImmediate(ID3D11DeviceContext* real);
    WrappedContext* WrapDeferred(ID3D11DeviceContext* real);

    WrappedContext* m_immediate = nullptr;
    SRWLOCK m_lock = SRWLOCK_INIT;
    int m_level = 0;  // highest ID3D11DeviceN the real object supports
    LONG m_deferredCount = 0;
};

class WrappedSwapChain final : public Fwd_IDXGISwapChain4 {
public:
    WrappedSwapChain(IDXGISwapChain* real, WrappedDevice* device);
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;

    HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** ppDevice) override;
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT Buffer, REFIID riid, void** ppSurface) override;
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

    WrappedDevice* m_device;
    int m_level = 0;  // highest IDXGISwapChainN the real object supports
    LONG64 m_frames = 0;
    LONG64 m_framesAtStatus = 0;
    ULONGLONG m_statusTick = 0;
    int m_colorSpace = -1;
    bool m_loggedBuffer = false;
};

}  // namespace goh4k
