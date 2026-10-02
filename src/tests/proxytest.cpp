// Local smoke test for build\d3d11.dll, run before anything goes near the game:
// loads the proxy by full path, exercises the export stubs (slow and fast path), device/context/swapchain
// wrapping, QueryInterface routing, deferred contexts, Present and ResizeBuffers, and final refcounts.
// Uses a hidden window, so nothing shows on screen. Exit code 0 = all checks passed.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11_4.h>
#include <dxgi1_6.h>
#include <inspectable.h>
#include <cstdio>

static int g_failures = 0;
#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (cond) {                                         \
            printf("ok    " __VA_ARGS__);                   \
        } else {                                            \
            printf("FAIL  " __VA_ARGS__);                   \
            ++g_failures;                                   \
        }                                                   \
        printf("\n");                                       \
    } while (0)

typedef HRESULT(WINAPI* PFN_FromDxgi)(IDXGIDevice*, IInspectable**);

int main() {
    wchar_t dir[MAX_PATH];
    GetModuleFileNameW(nullptr, dir, MAX_PATH);
    *wcsrchr(dir, L'\\') = 0;
    wchar_t proxyPath[MAX_PATH], dxgiPath[MAX_PATH], sysPath[MAX_PATH];
    swprintf_s(proxyPath, L"%s\\d3d11.dll", dir);
    swprintf_s(dxgiPath, L"%s\\dxgi.dll", dir);
    UINT n = GetSystemDirectoryW(sysPath, MAX_PATH);
    wcscpy_s(sysPath + n, MAX_PATH - n, L"\\d3d11.dll");

    // Like the game: our dxgi.dll is loaded before anything else touches DXGI, so System32's d3d11.dll
    // binds its dxgi imports to our stubs.
    HMODULE dxgiProxy = LoadLibraryW(dxgiPath);
    CHECK(dxgiProxy != nullptr, "load dxgi proxy %ls", dxgiPath);
    if (!dxgiProxy) return 1;
    HMODULE proxy = LoadLibraryW(proxyPath);
    CHECK(proxy != nullptr, "load proxy %ls", proxyPath);
    if (!proxy) return 1;
    HMODULE sys = LoadLibraryW(sysPath);
    CHECK(sys != nullptr && sys != proxy, "system d3d11.dll is a separate module");

    // 1) Export stub slow path: first call into a stub we don't implement, with real arguments.
    {
        auto sysCreate = (PFN_D3D11_CREATE_DEVICE)GetProcAddress(sys, "D3D11CreateDevice");
        ID3D11Device* sdev = nullptr;
        HRESULT hr = sysCreate(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &sdev,
                               nullptr, nullptr);
        CHECK(SUCCEEDED(hr), "system D3D11CreateDevice for the stub test (0x%08lx)", hr);
        IDXGIDevice* dxgi = nullptr;
        if (sdev) sdev->QueryInterface(IID_PPV_ARGS(&dxgi));
        auto fromDxgi = (PFN_FromDxgi)GetProcAddress(proxy, "CreateDirect3D11DeviceFromDXGIDevice");
        CHECK(fromDxgi != nullptr, "proxy exports CreateDirect3D11DeviceFromDXGIDevice");
        for (int pass = 0; pass < 2 && fromDxgi && dxgi; ++pass) {
            IInspectable* insp = nullptr;
            hr = fromDxgi(dxgi, &insp);
            CHECK(SUCCEEDED(hr) && insp, "stub call %s path (0x%08lx)", pass ? "fast" : "slow", hr);
            if (insp) insp->Release();
        }
        if (dxgi) dxgi->Release();
        if (sdev) sdev->Release();
    }

    // 2) Device + swapchain through the proxy.
    WNDCLASSW wc = {};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"goh4k_proxytest";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"proxytest", WS_OVERLAPPEDWINDOW, 0, 0, 656, 399, nullptr, nullptr,
                                wc.hInstance, nullptr);
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferDesc.Width = 640;
    scd.BufferDesc.Height = 360;
    scd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;  // like the game: BGRA8 with MSAA on the backbuffer
    scd.SampleDesc.Count = 8;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.BufferCount = 1;
    scd.OutputWindow = hwnd;
    scd.Windowed = TRUE;
    scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    // The game's sequence: a factory from CreateDXGIFactory, D3D11CreateDeviceAndSwapChain on its adapter,
    // that swapchain dropped, then the real swapchain made with IDXGIFactory::CreateSwapChain.
    auto createFactory = (HRESULT(WINAPI*)(REFIID, void**))GetProcAddress(dxgiProxy, "CreateDXGIFactory");
    IDXGIFactory* factory = nullptr;
    HRESULT hr = createFactory ? createFactory(IID_PPV_ARGS(&factory)) : E_FAIL;
    CHECK(SUCCEEDED(hr) && factory, "CreateDXGIFactory through the dxgi proxy (0x%08lx)", hr);
    if (!factory) return 1;
    IDXGIAdapter* adapter = nullptr;
    factory->EnumAdapters(0, &adapter);
    auto create = (PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN)GetProcAddress(proxy, "D3D11CreateDeviceAndSwapChain");
    IDXGISwapChain* sc = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    D3D_FEATURE_LEVEL fl = {};
    hr = create(adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &scd, &sc, &dev, &fl, &ctx);
    CHECK(SUCCEEDED(hr) && sc && dev && ctx, "D3D11CreateDeviceAndSwapChain through the proxy (0x%08lx, fl 0x%x)", hr,
          (unsigned)fl);
    if (FAILED(hr)) return 1;
    if (adapter) adapter->Release();
    sc->SetFullscreenState(FALSE, nullptr);
    sc->Release();
    sc = nullptr;
    hr = factory->CreateSwapChain(dev, &scd, &sc);
    CHECK(SUCCEEDED(hr) && sc, "IDXGIFactory::CreateSwapChain with the wrapped device (0x%08lx)", hr);
    if (FAILED(hr)) return 1;
    factory->Release();

    // 3) Identity and QueryInterface routing.
    ID3D11Device* fromCtx = nullptr;
    ctx->GetDevice(&fromCtx);
    CHECK(fromCtx == dev, "context->GetDevice returns the wrapped device");
    if (fromCtx) fromCtx->Release();
    ID3D11Device* fromSc = nullptr;
    sc->GetDevice(IID_PPV_ARGS(&fromSc));
    CHECK(fromSc == dev, "swapchain->GetDevice returns the wrapped device");
    if (fromSc) fromSc->Release();
    ID3D11DeviceContext* imm = nullptr;
    dev->GetImmediateContext(&imm);
    CHECK(imm == ctx, "GetImmediateContext returns the same wrapper");
    if (imm) imm->Release();
    ID3D11Device5* dev5 = nullptr;
    dev->QueryInterface(IID_PPV_ARGS(&dev5));
    CHECK(dev5 && (void*)dev5 == (void*)dev, "QI ID3D11Device5 returns the wrapper");
    ID3D11DeviceContext4* ctx4 = nullptr;
    ctx->QueryInterface(IID_PPV_ARGS(&ctx4));
    CHECK(ctx4 && (void*)ctx4 == (void*)ctx, "QI ID3D11DeviceContext4 returns the wrapper");
    ID3D11DeviceContext3* imm3 = nullptr;
    if (dev5) dev5->GetImmediateContext3(&imm3);
    CHECK(imm3 && (void*)imm3 == (void*)ctx, "GetImmediateContext3 returns the same wrapper");
    if (imm3) imm3->Release();
    IDXGISwapChain4* sc4 = nullptr;
    sc->QueryInterface(IID_PPV_ARGS(&sc4));
    CHECK(sc4 && (void*)sc4 == (void*)sc, "QI IDXGISwapChain4 returns the wrapper");
    IDXGIDevice* dxgiDev = nullptr;
    dev->QueryInterface(IID_PPV_ARGS(&dxgiDev));
    CHECK(dxgiDev != nullptr, "QI IDXGIDevice passes through to the real device");
    if (dxgiDev) dxgiDev->Release();

    // 4) Deferred context: record a clear, execute it on the immediate context.
    ID3D11DeviceContext* def = nullptr;
    hr = dev->CreateDeferredContext(0, &def);
    CHECK(SUCCEEDED(hr) && def && def != ctx, "CreateDeferredContext (0x%08lx)", hr);
    ID3D11Device* fromDef = nullptr;
    if (def) def->GetDevice(&fromDef);
    CHECK(fromDef == dev, "deferred context->GetDevice returns the wrapped device");
    if (fromDef) fromDef->Release();

    // 5) Frames, a resize in the middle, and diagnostics requests through captures\request.txt.
    wchar_t root[MAX_PATH];
    wcscpy_s(root, dir);
    *wcsrchr(root, L'\\') = 0;  // build\ -> project root
    wchar_t capDir[MAX_PATH], reqPath[MAX_PATH];
    swprintf_s(capDir, L"%s\\captures", root);
    swprintf_s(reqPath, L"%s\\request.txt", capDir);
    int presented = 0;
    for (int frame = 0; frame < 240; ++frame) {
        if (frame == 90) {
            const char req[] = "capture ptest\r\ntrace ptest\r\nstats ptest\r\nfix ptestfix on\r\n";
            HANDLE h = CreateFileW(reqPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
            DWORD w = 0;
            WriteFile(h, req, sizeof(req) - 1, &w, nullptr);
            CloseHandle(h);
        }
        if (frame == 170) {
            const char req[] = "dumprt 1 ptestdump\r\n";
            HANDLE h = CreateFileW(reqPath, GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
            DWORD w = 0;
            WriteFile(h, req, sizeof(req) - 1, &w, nullptr);
            CloseHandle(h);
        }
        Sleep(6);
        if (frame == 60) {
            hr = sc->ResizeBuffers(0, 800, 450, DXGI_FORMAT_UNKNOWN, 0);
            CHECK(SUCCEEDED(hr), "ResizeBuffers to 800x450 (0x%08lx)", hr);
        }
        ID3D11Texture2D* bb = nullptr;
        sc->GetBuffer(0, IID_PPV_ARGS(&bb));
        ID3D11RenderTargetView* rtv = nullptr;
        if (bb) dev->CreateRenderTargetView(bb, nullptr, &rtv);
        float color[4] = {frame / 120.0f, 0.2f, 0.4f, 1.0f};
        if (def && rtv && frame % 30 == 0) {
            def->ClearRenderTargetView(rtv, color);
            ID3D11CommandList* cl = nullptr;
            def->FinishCommandList(FALSE, &cl);
            if (cl) {
                ctx->ExecuteCommandList(cl, FALSE);
                cl->Release();
            }
        } else if (rtv) {
            ctx->OMSetRenderTargets(1, &rtv, nullptr);
            ctx->ClearRenderTargetView(rtv, color);
        }
        ctx->OMSetRenderTargets(0, nullptr, nullptr);
        if (rtv) rtv->Release();
        if (bb) bb->Release();
        hr = sc->Present(0, 0);
        if (SUCCEEDED(hr)) ++presented;
    }
    CHECK(presented == 240, "240 Presents succeeded (%d)", presented);

    // Diagnostics outputs (PNGs are written on a worker thread).
    Sleep(2000);
    auto found = [&](const wchar_t* pattern) {
        wchar_t p[MAX_PATH];
        swprintf_s(p, L"%s\\%s", capDir, pattern);
        WIN32_FIND_DATAW fd;
        HANDLE f = FindFirstFileW(p, &fd);
        if (f == INVALID_HANDLE_VALUE) return 0ull;
        FindClose(f);
        return ((unsigned long long)fd.nFileSizeHigh << 32) | fd.nFileSizeLow;
    };
    CHECK(GetFileAttributesW(reqPath) == INVALID_FILE_ATTRIBUTES, "request.txt consumed");
    CHECK(found(L"*-ptest.png") > 1000, "capture PNG written (MSAA backbuffer resolved)");
    CHECK(found(L"*-ptest-preview.png") > 0 && found(L"*-ptest.txt") > 0, "preview and sidecar written");
    CHECK(found(L"*-ptest-trace.txt") > 200, "trace written");
    CHECK(found(L"*-ptest-stats.txt") > 100, "stats written");
    CHECK(found(L"*-ptestdump-e1-rt0.png") > 1000, "dumprt PNG written for event #1");
    // Clean up the test's images (traces and stats stay for inspection).
    {
        wchar_t p[MAX_PATH];
        swprintf_s(p, L"%s\\*-ptest*", capDir);
        WIN32_FIND_DATAW fd;
        HANDLE f = FindFirstFileW(p, &fd);
        if (f != INVALID_HANDLE_VALUE) {
            do {
                wchar_t full[MAX_PATH];
                swprintf_s(full, L"%s\\%s", capDir, fd.cFileName);
                if (!wcsstr(fd.cFileName, L"trace") && !wcsstr(fd.cFileName, L"stats")) DeleteFileW(full);
            } while (FindNextFileW(f, &fd));
            FindClose(f);
        }
    }

    // 6) Release everything; the device must reach 0.
    if (def) def->Release();
    if (sc4) sc4->Release();
    if (ctx4) ctx4->Release();
    if (dev5) dev5->Release();
    ULONG scRef = sc->Release();
    ULONG ctxRef = ctx->Release();
    ULONG devRef = dev->Release();
    CHECK(scRef == 0, "swapchain final Release returns 0 (%lu)", scRef);
    CHECK(devRef == 0, "device final Release returns 0 (%lu, context %lu)", devRef, ctxRef);
    DestroyWindow(hwnd);

    printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "PASSED", g_failures);
    return g_failures ? 1 : 0;
}
