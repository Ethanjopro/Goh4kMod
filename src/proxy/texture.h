// Backbuffer wrapper for 2x mode; see texture.cpp and unwrap.h.
#pragma once
#include "wrappers.h"

namespace goh4k {

class WrappedTexture2D final : public ID3D11Texture2D {
public:
    WrappedTexture2D(ID3D11Texture2D* real, WrappedDevice* device, UINT logicalW, UINT logicalH);
    ID3D11Texture2D* Real() const { return m_real; }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override;
    ULONG STDMETHODCALLTYPE AddRef() override;
    ULONG STDMETHODCALLTYPE Release() override;
    void STDMETHODCALLTYPE GetDevice(ID3D11Device** ppDevice) override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guid, UINT* pDataSize, void* pData) override;
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guid, UINT DataSize, const void* pData) override;
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID guid, const IUnknown* pData) override;
    void STDMETHODCALLTYPE GetType(D3D11_RESOURCE_DIMENSION* pResourceDimension) override;
    void STDMETHODCALLTYPE SetEvictionPriority(UINT EvictionPriority) override;
    UINT STDMETHODCALLTYPE GetEvictionPriority() override;
    void STDMETHODCALLTYPE GetDesc(D3D11_TEXTURE2D_DESC* pDesc) override;

private:
    ID3D11Texture2D* m_real;
    WrappedDevice* m_device;
    UINT m_logicalW, m_logicalH;
};

}  // namespace goh4k
