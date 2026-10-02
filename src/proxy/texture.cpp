// Backbuffer wrapper for 2x mode: forwards everything to the real swapchain buffer except GetDesc, which
// reports the logical (1920x1080) size, and GetDevice, which returns our device wrapper. Without it the game
// sizes some of its screen targets from the real 3840x2160 while setting viewports from 1920x1080.
#include "texture.h"
#include "unwrap.h"

namespace goh4k {

void* g_wrappedTextureVtbl = nullptr;

WrappedTexture2D::WrappedTexture2D(ID3D11Texture2D* real, WrappedDevice* device, UINT logicalW, UINT logicalH)
    : m_real(real), m_device(device), m_logicalW(logicalW), m_logicalH(logicalH) {
    g_wrappedTextureVtbl = *(void**)this;
}

ID3D11Resource* RealOfWrappedTexture(ID3D11Resource* r) { return static_cast<WrappedTexture2D*>(r)->Real(); }

HRESULT STDMETHODCALLTYPE WrappedTexture2D::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) || riid == __uuidof(ID3D11Resource) ||
        riid == __uuidof(ID3D11Texture2D)) {
        AddRef();
        *ppv = static_cast<ID3D11Texture2D*>(this);
        return S_OK;
    }
    LogIidOnce("backbuffer", riid);
    return m_real->QueryInterface(riid, ppv);
}

ULONG STDMETHODCALLTYPE WrappedTexture2D::AddRef() { return m_real->AddRef(); }

// Never deleted: a swapchain buffer's count can reach 0 here while DXGI still owns it, and the swapchain
// wrapper caches this object until the next ResizeBuffers. A few leaked objects per resize at most.
ULONG STDMETHODCALLTYPE WrappedTexture2D::Release() { return m_real->Release(); }

void STDMETHODCALLTYPE WrappedTexture2D::GetDevice(ID3D11Device** ppDevice) {
    if (!ppDevice) return;
    m_device->AddRef();
    *ppDevice = m_device;
}

HRESULT STDMETHODCALLTYPE WrappedTexture2D::GetPrivateData(REFGUID guid, UINT* pDataSize, void* pData) {
    return m_real->GetPrivateData(guid, pDataSize, pData);
}
HRESULT STDMETHODCALLTYPE WrappedTexture2D::SetPrivateData(REFGUID guid, UINT DataSize, const void* pData) {
    return m_real->SetPrivateData(guid, DataSize, pData);
}
HRESULT STDMETHODCALLTYPE WrappedTexture2D::SetPrivateDataInterface(REFGUID guid, const IUnknown* pData) {
    return m_real->SetPrivateDataInterface(guid, pData);
}
void STDMETHODCALLTYPE WrappedTexture2D::GetType(D3D11_RESOURCE_DIMENSION* pResourceDimension) {
    m_real->GetType(pResourceDimension);
}
void STDMETHODCALLTYPE WrappedTexture2D::SetEvictionPriority(UINT EvictionPriority) {
    m_real->SetEvictionPriority(EvictionPriority);
}
UINT STDMETHODCALLTYPE WrappedTexture2D::GetEvictionPriority() { return m_real->GetEvictionPriority(); }

void STDMETHODCALLTYPE WrappedTexture2D::GetDesc(D3D11_TEXTURE2D_DESC* pDesc) {
    m_real->GetDesc(pDesc);
    if (pDesc) {
        pDesc->Width = m_logicalW;
        pDesc->Height = m_logicalH;
    }
}

}  // namespace goh4k
