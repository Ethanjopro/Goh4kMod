// WrappedDevice and WrappedContext core: identity, QueryInterface routing, contexts, and resource/shader
// creation (recorded for stats and traces). M3 adds the 2x rules here.
#include "wrappers.h"
#include "diag.h"
#include "hbao.h"
#include "scale.h"
#include <atomic>
#include <intrin.h>
#pragma intrinsic(_ReturnAddress)

namespace goh4k {

// QueryInterface for the most derived interface the real object supports, keep that pointer without a
// reference, and report its level. `ids` lists the interface versions from 1 upwards.
template <typename T, size_t N>
static T* MostDerived(IUnknown* real, const IID* const (&ids)[N], int* level) {
    *level = 0;
    IUnknown* best = nullptr;
    for (int i = (int)N - 1; i >= 0 && !best; --i) {
        if (SUCCEEDED(real->QueryInterface(*ids[i], (void**)&best))) *level = i + 1;
    }
    if (!best) return reinterpret_cast<T*>(real);
    best->Release();
    return reinterpret_cast<T*>(best);
}

// ---------------------------------------------------------------- WrappedContext

static const IID* const kContextIds[] = {&__uuidof(ID3D11DeviceContext1), &__uuidof(ID3D11DeviceContext2),
                                         &__uuidof(ID3D11DeviceContext3), &__uuidof(ID3D11DeviceContext4)};

static void* g_contextVtbl = nullptr;

bool IsOurContext(ID3D11DeviceContext* ctx) { return ctx && g_contextVtbl && *(void**)ctx == g_contextVtbl; }

WrappedContext::WrappedContext(ID3D11DeviceContext* real, WrappedDevice* device, bool immediate)
    : m_device(device), m_immediate(immediate) {
    static std::atomic<int> nextDeferredId{1};
    m_real = MostDerived<ID3D11DeviceContext4>(real, kContextIds, &m_level);
    if (!immediate) m_id = nextDeferredId++;
    g_contextVtbl = *(void**)this;
}

HRESULT STDMETHODCALLTYPE WrappedContext::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    int need = -1;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11DeviceChild) || riid == __uuidof(ID3D11DeviceContext))
        need = 0;
    else
        for (int i = 0; i < 4; ++i)
            if (riid == *kContextIds[i]) need = i + 1;
    if (need >= 0) {
        if (need > m_level) {
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        *ppv = static_cast<ID3D11DeviceContext4*>(this);
        return S_OK;
    }
    LogIidOnce(m_immediate ? "immediate context" : "deferred context", riid);
    return m_real->QueryInterface(riid, ppv);
}

ULONG STDMETHODCALLTYPE WrappedContext::AddRef() { return m_real->AddRef(); }

ULONG STDMETHODCALLTYPE WrappedContext::Release() {
    ULONG r = m_real->Release();
    if (r == 0 && !m_immediate) delete this;  // the immediate context's wrapper belongs to the device wrapper
    return r;
}

void STDMETHODCALLTYPE WrappedContext::GetDevice(ID3D11Device** ppDevice) {
    if (!ppDevice) return;
    m_device->AddRef();
    *ppDevice = m_device;
}

// ---------------------------------------------------------------- WrappedDevice

static const IID* const kDeviceIds[] = {&__uuidof(ID3D11Device1), &__uuidof(ID3D11Device2), &__uuidof(ID3D11Device3),
                                        &__uuidof(ID3D11Device4), &__uuidof(ID3D11Device5)};

WrappedDevice::WrappedDevice(ID3D11Device* real, ID3D11DeviceContext* realImmediate) {
    m_real = MostDerived<ID3D11Device5>(real, kDeviceIds, &m_level);
    if (realImmediate) m_immediate = new WrappedContext(realImmediate, this, true);
    Log("device wrapped: real=%p ID3D11Device%d, feature level 0x%x, flags 0x%x, immediate context %s", real, m_level,
        (unsigned)real->GetFeatureLevel(), real->GetCreationFlags(),
        m_immediate ? Fmt("ID3D11DeviceContext%d", m_immediate->Level()).c_str() : "not requested");
}

WrappedDevice::~WrappedDevice() {
    Log("device released (%ld deferred contexts were created)", m_deferredCount);
    delete m_immediate;
}

HRESULT STDMETHODCALLTYPE WrappedDevice::QueryInterface(REFIID riid, void** ppv) {
    if (!ppv) return E_POINTER;
    if (riid == IID_Goh4kWrappedDevice) {
        AddRef();
        *ppv = this;
        return S_OK;
    }
    int need = -1;
    if (riid == __uuidof(IUnknown) || riid == __uuidof(ID3D11Device))
        need = 0;
    else
        for (int i = 0; i < 5; ++i)
            if (riid == *kDeviceIds[i]) need = i + 1;
    if (need >= 0) {
        if (need > m_level) {
            *ppv = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        *ppv = static_cast<ID3D11Device5*>(this);
        return S_OK;
    }
    LogIidOnce("device", riid);
    return m_real->QueryInterface(riid, ppv);
}

ULONG STDMETHODCALLTYPE WrappedDevice::AddRef() { return m_real->AddRef(); }

ULONG STDMETHODCALLTYPE WrappedDevice::Release() {
    ULONG r = m_real->Release();
    if (r == 0) delete this;
    return r;
}

WrappedContext* WrappedDevice::AdoptImmediate(ID3D11DeviceContext* real) {
    AcquireSRWLockExclusive(&m_lock);
    if (!m_immediate) m_immediate = new WrappedContext(real, this, true);
    WrappedContext* ctx = m_immediate;
    ReleaseSRWLockExclusive(&m_lock);
    return ctx;
}

WrappedContext* WrappedDevice::WrapDeferred(ID3D11DeviceContext* real) {
    LONG n = InterlockedIncrement(&m_deferredCount);
    if (n <= 4 || n % 100 == 0) Log("deferred context #%ld created", n);
    return new WrappedContext(real, this, false);
}

void STDMETHODCALLTYPE WrappedDevice::GetImmediateContext(ID3D11DeviceContext** pp) {
    if (!pp) return;
    ID3D11DeviceContext* real = nullptr;
    m_real->GetImmediateContext(&real);
    *pp = real ? AdoptImmediate(real) : nullptr;
}

void STDMETHODCALLTYPE WrappedDevice::GetImmediateContext1(ID3D11DeviceContext1** pp) {
    if (!pp) return;
    ID3D11DeviceContext1* real = nullptr;
    m_real->GetImmediateContext1(&real);
    *pp = real ? AdoptImmediate(real) : nullptr;
}

void STDMETHODCALLTYPE WrappedDevice::GetImmediateContext2(ID3D11DeviceContext2** pp) {
    if (!pp) return;
    ID3D11DeviceContext2* real = nullptr;
    m_real->GetImmediateContext2(&real);
    *pp = real ? AdoptImmediate(real) : nullptr;
}

void STDMETHODCALLTYPE WrappedDevice::GetImmediateContext3(ID3D11DeviceContext3** pp) {
    if (!pp) return;
    ID3D11DeviceContext3* real = nullptr;
    m_real->GetImmediateContext3(&real);
    *pp = real ? AdoptImmediate(real) : nullptr;
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateDeferredContext(UINT flags, ID3D11DeviceContext** pp) {
    if (!pp) return m_real->CreateDeferredContext(flags, nullptr);
    ID3D11DeviceContext* real = nullptr;
    HRESULT hr = m_real->CreateDeferredContext(flags, &real);
    *pp = (SUCCEEDED(hr) && real) ? WrapDeferred(real) : real;
    return hr;
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateDeferredContext1(UINT flags, ID3D11DeviceContext1** pp) {
    if (!pp) return m_real->CreateDeferredContext1(flags, nullptr);
    ID3D11DeviceContext1* real = nullptr;
    HRESULT hr = m_real->CreateDeferredContext1(flags, &real);
    *pp = (SUCCEEDED(hr) && real) ? WrapDeferred(real) : real;
    return hr;
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateDeferredContext2(UINT flags, ID3D11DeviceContext2** pp) {
    if (!pp) return m_real->CreateDeferredContext2(flags, nullptr);
    ID3D11DeviceContext2* real = nullptr;
    HRESULT hr = m_real->CreateDeferredContext2(flags, &real);
    *pp = (SUCCEEDED(hr) && real) ? WrapDeferred(real) : real;
    return hr;
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateDeferredContext3(UINT flags, ID3D11DeviceContext3** pp) {
    if (!pp) return m_real->CreateDeferredContext3(flags, nullptr);
    ID3D11DeviceContext3* real = nullptr;
    HRESULT hr = m_real->CreateDeferredContext3(flags, &real);
    *pp = (SUCCEEDED(hr) && real) ? WrapDeferred(real) : real;
    return hr;
}

// ---------------------------------------------------------------- resources and shaders

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateTexture2D(const D3D11_TEXTURE2D_DESC* pDesc,
                                                         const D3D11_SUBRESOURCE_DATA* pInitialData,
                                                         ID3D11Texture2D** ppTexture2D) {
    // HBAO+ works at real size (hbao.cpp), so its own targets are never scaled.
    if (pDesc && ppTexture2D && WantScaled(*pDesc, pInitialData != nullptr) && !FromHbao(_ReturnAddress())) {
        D3D11_TEXTURE2D_DESC d = *pDesc;
        d.Width *= g_cfg.scale;
        d.Height *= g_cfg.scale;
        HRESULT hr = m_real->CreateTexture2D(&d, nullptr, ppTexture2D);
        if (SUCCEEDED(hr)) {
            TagResource(*ppTexture2D, {(UINT)g_cfg.scale, pDesc->Width, pDesc->Height});
            LogScaledCreate(*pDesc);
            RecordTextureCreate(*pDesc, false);
            return hr;
        }
        Log("scaled CreateTexture2D %ux%u failed 0x%08lx; creating it unscaled", d.Width, d.Height, hr);
    }
    HRESULT hr = m_real->CreateTexture2D(pDesc, pInitialData, ppTexture2D);
    if (SUCCEEDED(hr) && pDesc && ppTexture2D) {
        RecordTextureCreate(*pDesc, pInitialData != nullptr);
        // Targets the game sized from a real-sized texture (e.g. our scaled depth buffer) are already 2x of
        // the logical screen: tag them so the logical viewports the game sets on them get doubled too.
        if (*ppTexture2D && IsRealScreenTarget(*pDesc) && !FromHbao(_ReturnAddress())) {
            TagResource(*ppTexture2D, {(UINT)g_cfg.scale, (UINT)g_logicalW, (UINT)g_logicalH});
            NoteOnce("real-size-target-tagged", Fmt("%ux%u fmt=%d bind=0x%x created at real size; tagged as 2x",
                                                    pDesc->Width, pDesc->Height, (int)pDesc->Format, pDesc->BindFlags));
        }
    }
    return hr;
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateTexture2D1(const D3D11_TEXTURE2D_DESC1* pDesc1,
                                                          const D3D11_SUBRESOURCE_DATA* pInitialData,
                                                          ID3D11Texture2D1** ppTexture2D) {
    D3D11_TEXTURE2D_DESC d = {};
    if (pDesc1) memcpy(&d, pDesc1, sizeof(d));  // DESC1 = DESC + TextureLayout
    if (pDesc1 && ppTexture2D && WantScaled(d, pInitialData != nullptr) && !FromHbao(_ReturnAddress())) {
        D3D11_TEXTURE2D_DESC1 d1 = *pDesc1;
        d1.Width *= g_cfg.scale;
        d1.Height *= g_cfg.scale;
        HRESULT hr = m_real->CreateTexture2D1(&d1, nullptr, ppTexture2D);
        if (SUCCEEDED(hr)) {
            TagResource(*ppTexture2D, {(UINT)g_cfg.scale, pDesc1->Width, pDesc1->Height});
            LogScaledCreate(d);
            RecordTextureCreate(d, false);
            return hr;
        }
    }
    HRESULT hr = m_real->CreateTexture2D1(pDesc1, pInitialData, ppTexture2D);
    if (SUCCEEDED(hr) && pDesc1 && ppTexture2D) RecordTextureCreate(d, pInitialData != nullptr);
    return hr;
}

// Views of render/depth targets carry their resource's scale, so binding them costs one lookup.
HRESULT STDMETHODCALLTYPE WrappedDevice::CreateRenderTargetView(ID3D11Resource* pResource,
                                                                const D3D11_RENDER_TARGET_VIEW_DESC* pDesc,
                                                                ID3D11RenderTargetView** ppRTView) {
    pResource = UnwrapResource(pResource);
    HRESULT hr = m_real->CreateRenderTargetView(pResource, pDesc, ppRTView);
    if (SUCCEEDED(hr) && ppRTView && *ppRTView && Scaling()) TagView(*ppRTView);
    return hr;
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateRenderTargetView1(ID3D11Resource* pResource,
                                                                 const D3D11_RENDER_TARGET_VIEW_DESC1* pDesc1,
                                                                 ID3D11RenderTargetView1** ppRTView1) {
    pResource = UnwrapResource(pResource);
    HRESULT hr = m_real->CreateRenderTargetView1(pResource, pDesc1, ppRTView1);
    if (SUCCEEDED(hr) && ppRTView1 && *ppRTView1 && Scaling()) TagView(*ppRTView1);
    return hr;
}

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateDepthStencilView(ID3D11Resource* pResource,
                                                                const D3D11_DEPTH_STENCIL_VIEW_DESC* pDesc,
                                                                ID3D11DepthStencilView** ppDepthStencilView) {
    pResource = UnwrapResource(pResource);
    HRESULT hr = m_real->CreateDepthStencilView(pResource, pDesc, ppDepthStencilView);
    if (SUCCEEDED(hr) && ppDepthStencilView && *ppDepthStencilView && Scaling()) TagView(*ppDepthStencilView);
    return hr;
}

#define SHADER_CREATE(Method, Type, kind)                                                                          \
    HRESULT STDMETHODCALLTYPE WrappedDevice::Method(const void* code, SIZE_T len, ID3D11ClassLinkage* linkage,      \
                                                    Type** pp) {                                                    \
        HRESULT hr = m_real->Method(code, len, linkage, pp);                                                        \
        if (SUCCEEDED(hr) && pp && *pp) RegisterShader(*pp, code, len, kind);                                       \
        return hr;                                                                                                  \
    }
SHADER_CREATE(CreateVertexShader, ID3D11VertexShader, 'v')
SHADER_CREATE(CreatePixelShader, ID3D11PixelShader, 'p')
SHADER_CREATE(CreateGeometryShader, ID3D11GeometryShader, 'g')
SHADER_CREATE(CreateHullShader, ID3D11HullShader, 'h')
SHADER_CREATE(CreateDomainShader, ID3D11DomainShader, 'd')
SHADER_CREATE(CreateComputeShader, ID3D11ComputeShader, 'c')
#undef SHADER_CREATE

HRESULT STDMETHODCALLTYPE WrappedDevice::CreateGeometryShaderWithStreamOutput(
    const void* code, SIZE_T len, const D3D11_SO_DECLARATION_ENTRY* pSODeclaration, UINT NumEntries,
    const UINT* pBufferStrides, UINT NumStrides, UINT RasterizedStream, ID3D11ClassLinkage* linkage,
    ID3D11GeometryShader** pp) {
    HRESULT hr = m_real->CreateGeometryShaderWithStreamOutput(code, len, pSODeclaration, NumEntries, pBufferStrides,
                                                              NumStrides, RasterizedStream, linkage, pp);
    if (SUCCEEDED(hr) && pp && *pp) RegisterShader(*pp, code, len, 'g');
    return hr;
}

}  // namespace goh4k
