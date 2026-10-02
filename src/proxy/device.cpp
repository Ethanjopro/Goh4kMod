// WrappedDevice and WrappedContext: pure forwarding for now (M1). M3 adds the 2x rules here.
#include "wrappers.h"

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

WrappedContext::WrappedContext(ID3D11DeviceContext* real, WrappedDevice* device, bool immediate)
    : m_device(device), m_immediate(immediate) {
    m_real = MostDerived<ID3D11DeviceContext4>(real, kContextIds, &m_level);
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

}  // namespace goh4k
