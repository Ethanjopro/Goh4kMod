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
#include "../proxy/ratio.h"

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

// 0) Rational coordinate mapping (ratio.h), exhaustively over a range wider than any screen.
static void CheckRatioMath() {
    using namespace goh4k;
    const Ratio ratios[] = {{2, 1}, {4, 3}, {3, 2}, {8, 3}};
    for (const Ratio& r : ratios) {
        int roundTrip = 0, oldFormula = 0, clip = 0;
        for (long v = -9000; v <= 9000; ++v) {
            if (RatioDown(RatioUp(v, r), r) != v) ++roundTrip;
            if (r.d == 1) {  // the 1.0 integer formulas
                long s = (long)r.n;
                long oldDown = v >= 0 ? v / s : -((-v + s - 1) / s);
                if (RatioDown(v, r) != oldDown || RatioUp(v, r) != v * s) ++oldFormula;
            }
        }
        // ClipCursor: real pixels whose Down lies in [l, h) are exactly [Up(l), Up(h)).
        for (long l = -40; l <= 40; l += 7)
            for (long h = l + 1; h <= 60; h += 5)
                for (long p = -200; p <= 200; ++p) {
                    long d = RatioDown(p, r);
                    bool inLogical = d >= l && d < h;
                    bool inReal = p >= RatioUp(l, r) && p < RatioUp(h, r);
                    if (inLogical != inReal) ++clip;
                }
        CHECK(roundTrip == 0, "ratio %lld/%lld: Down(Up(v)) == v for -9000..9000 (%d misses)", r.n, r.d, roundTrip);
        if (r.d == 1)
            CHECK(oldFormula == 0, "ratio %lld/1: identical to the 1.0 floor/multiply (%d differences)", r.n, oldFormula);
        CHECK(clip == 0, "ratio %lld/%lld: ClipCursor edges cover exactly the logical rect (%d misses)", r.n, r.d, clip);
    }
    // The factor per screen: the logical screen is never smaller than 1920x1080 on either axis.
    struct Case {
        uint32_t w, h;
        int64_t n, d;
        long lw, lh;
    };
    const Case cases[] = {
        {3840, 2160, 2, 1, 1920, 1080}, {2560, 1440, 4, 3, 1920, 1080}, {5120, 2880, 8, 3, 1920, 1080},
        {3200, 1800, 5, 3, 1920, 1080}, {7680, 4320, 4, 1, 1920, 1080}, {3440, 1440, 4, 3, 2580, 1080},
        {5120, 1440, 4, 3, 3840, 1080}, {3840, 1600, 40, 27, 2592, 1080}, {2560, 1600, 4, 3, 1920, 1200},
        {3840, 2400, 2, 1, 1920, 1200}, {2048, 1536, 16, 15, 1920, 1440}, {2160, 3840, 9, 8, 1920, 3413},
        {1920, 1200, 1, 1, 1920, 1200}, {2560, 1080, 1, 1, 2560, 1080}, {1920, 1080, 1, 1, 1920, 1080},
        {1366, 768, 1, 1, 1366, 768},
    };
    int wrong = 0;
    for (const Case& c : cases) {
        Ratio k = RatioForScreen(c.w, c.h, 1920, 1080);
        long lw = RatioDown((long)c.w, k), lh = RatioDown((long)c.h, k);
        if (k.n != c.n || k.d != c.d || lw != c.lw || lh != c.lh) {
            ++wrong;
            printf("      %ux%u: k %lld/%lld -> %ldx%ld, expected %lld/%lld -> %ldx%ld\n", c.w, c.h, k.n, k.d, lw, lh, c.n,
                   c.d, c.lw, c.lh);
        }
    }
    CHECK(wrong == 0, "factor table: %d screens (16:9, 16:10, ultrawide, 32:9, 4:3, portrait, 1080p-class): %d wrong",
          (int)(sizeof(cases) / sizeof(cases[0])), wrong);
}

static LPARAM g_lastSize = 0;  // the last WM_SIZE the test window's own procedure saw
static LRESULT CALLBACK TestWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_SIZE) g_lastSize = l;
    return DefWindowProcW(h, m, w, l);
}

// The primary monitor's size in real pixels (the test is per-monitor DPI aware through the proxy).
static void ScreenSize(int* w, int* h) {
    HDC dc = GetDC(nullptr);
    *w = GetDeviceCaps(dc, HORZRES), *h = GetDeviceCaps(dc, VERTRES);
    ReleaseDC(nullptr, dc);
}

static void Pump() {
    MSG msg;
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

typedef HRESULT(WINAPI* PFN_Compile)(LPCVOID, SIZE_T, LPCSTR, const void*, void*, LPCSTR, LPCSTR, UINT, UINT, ID3DBlob**,
                                     ID3DBlob**);

static ID3DBlob* Compile(const char* src, const char* target) {
    static PFN_Compile compile = (PFN_Compile)GetProcAddress(LoadLibraryW(L"d3dcompiler_47.dll"), "D3DCompile");
    ID3DBlob *code = nullptr, *err = nullptr;
    if (compile) compile(src, strlen(src), nullptr, nullptr, nullptr, "main", target, 0, 0, &code, &err);
    if (err) err->Release();
    return code;
}

// Rule 3 must read pixel-shader slot 0 back from the context. D3D11 unbinds a view by itself when its texture
// is bound as a render target; trusting the last PSSetShaderResources would put that stale view back after a
// stand-in draw, or touch it after the game freed it. Needs k > 1 (the window covers a monitor above 1080p).
static void CheckStaleSlot0(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc, UINT w, UINT h) {
    ID3DBlob* vsb = Compile("float4 main(uint id : SV_VertexID) : SV_Position {"
                            " return float4((id & 1) * 2.0 - 1, (id >> 1) * 2.0 - 1, 0, 1); }",
                            "vs_4_0");
    ID3DBlob* psb = Compile("Texture2D t : register(t0);"
                            " float4 main(float4 p : SV_Position) : SV_Target { return t.Load(int3(0, 0, 0)); }",
                            "ps_4_0");
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    if (vsb) dev->CreateVertexShader(vsb->GetBufferPointer(), vsb->GetBufferSize(), nullptr, &vs);
    if (psb) dev->CreatePixelShader(psb->GetBufferPointer(), psb->GetBufferSize(), nullptr, &ps);
    if (vsb) vsb->Release();
    if (psb) psb->Release();
    auto target = [&](UINT tw, UINT th, ID3D11Texture2D** t, ID3D11RenderTargetView** rtv, ID3D11ShaderResourceView** srv) {
        D3D11_TEXTURE2D_DESC td = {tw, th, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT,
                                   D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0};
        *t = nullptr, *rtv = nullptr;
        if (srv) *srv = nullptr;
        if (FAILED(dev->CreateTexture2D(&td, nullptr, t))) return;
        dev->CreateRenderTargetView(*t, nullptr, rtv);
        if (srv) dev->CreateShaderResourceView(*t, nullptr, srv);
    };
    ID3D11Texture2D *label, *screen, *img;
    ID3D11RenderTargetView *labelRtv, *screenRtv, *imgRtv;
    ID3D11ShaderResourceView* imgSrv;
    target(256, 256, &label, &labelRtv, nullptr);
    target(w, h, &screen, &screenRtv, nullptr);
    target(w, h, &img, &imgRtv, &imgSrv);
    const UINT16 idx[6] = {0, 1, 2, 2, 1, 3};
    D3D11_BUFFER_DESC bd = {sizeof(idx), D3D11_USAGE_DEFAULT, D3D11_BIND_INDEX_BUFFER, 0, 0, 0};
    D3D11_SUBRESOURCE_DATA init = {idx, 0, 0};
    ID3D11Buffer* ib = nullptr;
    dev->CreateBuffer(&bd, &init, &ib);
    if (!vs || !ps || !labelRtv || !screenRtv || !imgRtv || !imgSrv || !ib) {
        printf("note  shaders or targets for the slot-0 check unavailable (d3dcompiler_47?): skipped\n");
    } else {
        ctx->IASetIndexBuffer(ib, DXGI_FORMAT_R16_UINT, 0);
        ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ctx->VSSetShader(vs, nullptr, 0);
        ctx->PSSetShader(ps, nullptr, 0);
        D3D11_VIEWPORT full = {0, 0, (float)w, (float)h, 0, 1};
        // The UI's shader is learnt from a label-surface draw with the real-screen viewport (rule 1).
        ctx->OMSetRenderTargets(1, &labelRtv, nullptr);
        ctx->RSSetViewports(1, &full);
        ctx->DrawIndexed(6, 0, 0);
        // Two frames of the UI drawing the image on a screen-sized target: the second draw gets the stand-in.
        for (int f = 0; f < 2; ++f) {
            ctx->OMSetRenderTargets(1, &screenRtv, nullptr);
            ctx->RSSetViewports(1, &full);
            ctx->PSSetShaderResources(0, 1, &imgSrv);
            ctx->DrawIndexed(6, 0, 0);
            sc->Present(0, 0);
        }
        ID3D11ShaderResourceView* back = nullptr;
        ctx->PSGetShaderResources(0, 1, &back);
        CHECK(back == imgSrv, "slot 0: the game's own view is back after a stand-in draw");
        if (back) back->Release(), back = nullptr;
        // The image becomes a render target: the runtime takes it out of slot 0.
        ctx->OMSetRenderTargets(1, &imgRtv, nullptr);
        ctx->PSGetShaderResources(0, 1, &back);
        bool cleared = !back;
        if (back) back->Release(), back = nullptr;
        ctx->OMSetRenderTargets(1, &screenRtv, nullptr);
        ctx->DrawIndexed(6, 0, 0);
        ctx->PSGetShaderResources(0, 1, &back);
        if (cleared)
            CHECK(!back, "slot 0: a view the runtime unbound stays unbound after a UI draw (%p)", (void*)back);
        else
            printf("note  the runtime didn't unbind slot 0, so the stale-view check is inconclusive\n");
        if (back) back->Release(), back = nullptr;
        // The game frees the image; the next UI draws must not touch it.
        imgSrv->Release(), imgRtv->Release(), img->Release();
        imgSrv = nullptr, imgRtv = nullptr, img = nullptr;
        ctx->DrawIndexed(6, 0, 0);
        sc->Present(0, 0);
        ctx->DrawIndexed(6, 0, 0);
        CHECK(true, "slot 0: UI draws after the game freed the image");
    }
    ctx->ClearState();
    IUnknown* const all[] = {vs, ps, ib, labelRtv, screenRtv, imgRtv, imgSrv, label, screen, img};
    for (IUnknown* p : all)
        if (p) p->Release();
}

// Child process (--hookfail, with GOH4K_TEST_FAILHOOK set by the parent): one hook can't be made, so the mod
// must stay fully off: a monitor-covering window keeps its real client size, and moving it (WM_MOVE) is safe.
static int HookFailChild(const wchar_t* dir) {
    wchar_t path[MAX_PATH];
    swprintf_s(path, L"%s\\dxgi.dll", dir);
    LoadLibraryW(path);
    swprintf_s(path, L"%s\\d3d11.dll", dir);
    HMODULE proxy = LoadLibraryW(path);
    if (!proxy) return 3;
    int realW = 0, realH = 0;
    ScreenSize(&realW, &realH);
    WNDCLASSW wc = {};
    wc.lpfnWndProc = TestWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"goh4k_proxytest_child";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"proxytest child", WS_POPUP, 0, 0, realW, realH, nullptr, nullptr,
                                wc.hInstance, nullptr);
    DXGI_SWAP_CHAIN_DESC scd = {};
    scd.BufferDesc.Width = 640, scd.BufferDesc.Height = 360, scd.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    scd.SampleDesc.Count = 1, scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT, scd.BufferCount = 1;
    scd.OutputWindow = hwnd, scd.Windowed = TRUE, scd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
    auto create = (PFN_D3D11_CREATE_DEVICE_AND_SWAP_CHAIN)GetProcAddress(proxy, "D3D11CreateDeviceAndSwapChain");
    IDXGISwapChain* sc = nullptr;
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    HRESULT hr = create(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, nullptr, 0, D3D11_SDK_VERSION, &scd, &sc, &dev,
                        nullptr, &ctx);
    if (FAILED(hr)) return 4;
    for (int i = 0; i < 5; ++i) Pump(), sc->Present(0, 0);
    RECT cr = {};
    GetClientRect(hwnd, &cr);
    SetWindowPos(hwnd, nullptr, 10, 10, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);  // WM_MOVE
    SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
    for (int i = 0; i < 5; ++i) Pump(), sc->Present(0, 0);
    bool real = cr.right == realW && cr.bottom == realH;
    printf("      child: client %ldx%ld for a %dx%d window, WM_MOVE survived\n", cr.right, cr.bottom, realW, realH);
    sc->Release(), ctx->Release(), dev->Release();
    DestroyWindow(hwnd);
    return real ? 0 : 2;
}

int main(int argc, char** argv) {
    wchar_t self[MAX_PATH], dir[MAX_PATH];
    GetModuleFileNameW(nullptr, self, MAX_PATH);
    wcscpy_s(dir, self);
    *wcsrchr(dir, L'\\') = 0;
    if (argc > 1 && strcmp(argv[1], "--hookfail") == 0) return HookFailChild(dir);
    CheckRatioMath();
    // All or nothing: a hook that can't be made leaves the mod fully off.
    {
        wchar_t cmd[MAX_PATH + 32];
        swprintf_s(cmd, L"\"%s\" --hookfail", self);
        SetEnvironmentVariableW(L"GOH4K_TEST_FAILHOOK", L"GetClientRect");
        STARTUPINFOW si = {sizeof(si)};
        PROCESS_INFORMATION pi = {};
        BOOL ok = CreateProcessW(nullptr, cmd, nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi);
        SetEnvironmentVariableW(L"GOH4K_TEST_FAILHOOK", nullptr);
        DWORD code = 99;
        if (ok) {
            WaitForSingleObject(pi.hProcess, 60000);
            GetExitCodeProcess(pi.hProcess, &code);
            CloseHandle(pi.hThread), CloseHandle(pi.hProcess);
        }
        CHECK(ok && code == 0, "hook failure: with GetClientRect's hook missing the mod stays off (child exit %lu)", code);
    }
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
    wc.lpfnWndProc = TestWndProc;
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
    // The factory stays for the second game window at the end (6).

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
        // Deferred frames stop before the dumprt request (frame 170), so its event #1 is always a clear on
        // the immediate context, whichever frame the 4-per-second request poll arms it for.
        if (def && rtv && frame % 30 == 0 && frame < 170) {
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

    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\config\\goh4k.ini", root);

    // 5b) Native scaling checks (config\goh4k.ini UiScale=1, the default). A bordered 2560x1440 window doesn't
    // cover its monitor, so nothing is converted (the game would save the logical size as its resolution). A
    // borderless window covering the monitor gets k = monitor height / 1080: the client size and the cursor are
    // divided by it around the client origin; buffers, textures, screen metrics and the window rect stay real.
    int uiScale = GetPrivateProfileIntW(L"proxy", L"UiScale", 1, ini);
    bool scaled = false;  // k > 1 on this monitor
    int realW = 0, realH = 0;
    long lw = 0, lh = 0;
    if (uiScale == 1) {
        ScreenSize(&realW, &realH);
        // Windowed: pass-through.
        RECT outer = {0, 0, 2560, 1440};  // SetWindowPos isn't hooked
        AdjustWindowRectExForDpi(&outer, WS_OVERLAPPEDWINDOW, FALSE, 0, GetDpiForWindow(hwnd));
        SetWindowPos(hwnd, nullptr, 0, 0, outer.right - outer.left, outer.bottom - outer.top, SWP_NOZORDER | SWP_NOACTIVATE);
        hr = sc->ResizeBuffers(0, 2560, 1440, DXGI_FORMAT_UNKNOWN, 0);
        CHECK(SUCCEEDED(hr), "ui-scale: ResizeBuffers to 2560x1440 (0x%08lx)", hr);
        RECT cr = {};
        GetClientRect(hwnd, &cr);
        CHECK(cr.right == 2560 && cr.bottom == 1440, "ui-scale: a bordered 2560x1440 window isn't scaled (%ldx%ld)", cr.right,
              cr.bottom);
        // Covering the monitor: scaled.
        SetWindowLongPtrW(hwnd, GWL_STYLE, WS_POPUP);
        SetWindowPos(hwnd, nullptr, 0, 0, realW, realH, SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        hr = sc->ResizeBuffers(0, (UINT)realW, (UINT)realH, DXGI_FORMAT_UNKNOWN, 0);
        CHECK(SUCCEEDED(hr), "ui-scale: ResizeBuffers to the monitor's %dx%d (0x%08lx)", realW, realH, hr);
        goh4k::Ratio k = goh4k::RatioForScreen((uint32_t)realW, (uint32_t)realH, 1920, 1080);
        lw = goh4k::RatioDown(realW, k), lh = goh4k::RatioDown(realH, k);
        D3D11_TEXTURE2D_DESC td = {(UINT)realW, (UINT)realH, 1, 1, DXGI_FORMAT_R8G8B8A8_UNORM, {1, 0}, D3D11_USAGE_DEFAULT,
                                   D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE, 0, 0};
        ID3D11Texture2D* t = nullptr;
        dev->CreateTexture2D(&td, nullptr, &t);
        D3D11_TEXTURE2D_DESC o = {};
        if (t) t->GetDesc(&o), t->Release();
        CHECK(o.Width == (UINT)realW && o.Height == (UINT)realH, "ui-scale: a screen-size render target is created as asked (%ux%u)",
              o.Width, o.Height);
        // Rule 4: the game's screenshot readback (CPU-readable, at the client size it was told) is made at the real
        // size; other CPU-readable textures as asked.
        D3D11_TEXTURE2D_DESC rd = {(UINT)lw, (UINT)lh, 1, 1, DXGI_FORMAT_B8G8R8A8_UNORM, {1, 0}, D3D11_USAGE_STAGING, 0,
                                   D3D11_CPU_ACCESS_READ, 0};
        const UINT sizes[] = {0, 256};  // 0: the logical screen size
        for (UINT size : sizes) {
            if (size) rd.Width = rd.Height = size;
            o = {};
            t = nullptr;
            dev->CreateTexture2D(&rd, nullptr, &t);
            if (t) t->GetDesc(&o), t->Release();
            UINT ww = size ? size : (UINT)realW, wh = size ? size : (UINT)realH;
            CHECK(o.Width == ww && o.Height == wh, "screenshot: a CPU-readable %ux%u texture is made %ux%u", rd.Width,
                  rd.Height, o.Width, o.Height);
        }
        D3D11_VIEWPORT vp = {0, 0, (float)lw, (float)lh, 0, 1}, back = {};
        ctx->RSSetViewports(1, &vp);
        UINT nvp = 1;
        ctx->RSGetViewports(&nvp, &back);
        CHECK(nvp == 1 && back.Width == (float)lw && back.Height == (float)lh, "ui-scale: RSGetViewports returns the game's own %gx%g",
              back.Width, back.Height);
        DXGI_SWAP_CHAIN_DESC d = {};
        sc->GetDesc(&d);
        CHECK(d.BufferDesc.Width == (UINT)realW && d.BufferDesc.Height == (UINT)realH, "ui-scale: GetDesc stays real (%ux%u)",
              d.BufferDesc.Width, d.BufferDesc.Height);
        GetClientRect(hwnd, &cr);
        CHECK(cr.right == lw && cr.bottom == lh, "ui-scale: GetClientRect %ldx%ld for the real %dx%d (k %lld/%lld)", cr.right,
              cr.bottom, realW, realH, k.n, k.d);
        CHECK(GetSystemMetrics(SM_CXSCREEN) == realW, "ui-scale: GetSystemMetrics(SM_CXSCREEN) stays real (%d, %d)",
              GetSystemMetrics(SM_CXSCREEN), realW);
        WINDOWINFO wi = {sizeof(wi)};
        GetWindowInfo(hwnd, &wi);  // not hooked
        RECT wr = {};
        GetWindowRect(hwnd, &wr);
        CHECK(wr.right - wr.left == realW, "ui-scale: GetWindowRect stays real (%ld wide)", wr.right - wr.left);
        long ox = wi.rcClient.left, oy = wi.rcClient.top;
        CURSORINFO ci = {sizeof(ci)};
        GetCursorInfo(&ci);  // not hooked
        POINT p = {};
        GetCursorPos(&p);
        long want = ox + goh4k::RatioDown(ci.ptScreenPos.x - ox, k);
        CHECK(p.x >= want - 1 && p.x <= want + 1, "ui-scale: GetCursorPos x %ld is the real %ld divided by k around the client origin %ld",
              p.x, ci.ptScreenPos.x, ox);
        POINT c0 = {0, 0}, c1 = {lw - 1, lh - 1};
        ClientToScreen(hwnd, &c0);
        ClientToScreen(hwnd, &c1);
        CHECK(c0.x == ox && c0.y == oy && c1.x == ox + lw - 1 && c1.y == oy + lh - 1,
              "ui-scale: ClientToScreen puts the client at its real origin %ld,%ld with logical extent (%ld,%ld)-(%ld,%ld)", ox,
              oy, c0.x, c0.y, c1.x, c1.y);
        POINT s = c1;
        ScreenToClient(hwnd, &s);
        CHECK(s.x == lw - 1 && s.y == lh - 1, "ui-scale: ScreenToClient(ClientToScreen(p)) == p (%ld,%ld)", s.x, s.y);
        POINT m[2] = {{0, 0}, {lw - 1, lh - 1}};
        int off = MapWindowPoints(hwnd, nullptr, m, 2);
        CHECK(m[0].x == c0.x && m[1].y == c1.y && (SHORT)LOWORD(off) == ox && (SHORT)HIWORD(off) == oy,
              "ui-scale: MapWindowPoints to the screen matches ClientToScreen (offset %d,%d)", (SHORT)LOWORD(off),
              (SHORT)HIWORD(off));
        RECT dr = {};
        GetClientRect(GetDesktopWindow(), &dr);
        CHECK(dr.right == realW, "ui-scale: GetClientRect of the desktop stays real (%ld)", dr.right);
        scaled = !(k.n == k.d);
        if (!scaled) printf("note  this monitor is 1080p-class or less, so the scaled checks above saw k = 1\n");
    }

    if (scaled) {
        CheckStaleSlot0(dev, ctx, sc, (UINT)realW, (UINT)realH);
        // Our window procedure replaced by the window's own (what a program that subclassed before us puts back):
        // within a few seconds the mod subclasses again and resyncs the game with a logical WM_SIZE.
        SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)&TestWndProc);
        g_lastSize = 0;
        for (int i = 0; i < 50 && !g_lastSize; ++i) Pump(), sc->Present(0, 0), Sleep(100);
        CHECK((WNDPROC)GetWindowLongPtrW(hwnd, GWLP_WNDPROC) != &TestWndProc,
              "window procedure: replaced, then the mod's put back in front");
        CHECK(LOWORD(g_lastSize) == lw && HIWORD(g_lastSize) == lh, "window procedure: the resync WM_SIZE is logical (%ux%u)",
              LOWORD(g_lastSize), HIWORD(g_lastSize));

        // A borderless Options APPLY: another thread re-applies the window (a frame change at the same size, so
        // Windows sends no WM_SIZE), then sends the window a private message. Right after it the mod resyncs.
        g_lastSize = 0;
        HANDLE t = CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
            SetWindowPos((HWND)p, nullptr, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            SendMessageW((HWND)p, WM_USER + 0x19, 0, 0);
            return 0;
        }, hwnd, 0, nullptr);
        for (int i = 0; i < 200 && WaitForSingleObject(t, 0) == WAIT_TIMEOUT; ++i) Pump(), Sleep(5);
        CloseHandle(t);
        Pump();
        CHECK(LOWORD(g_lastSize) == lw && HIWORD(g_lastSize) == lh,
              "reconfigured window: a logical WM_SIZE right after the game's message (%ux%u)", LOWORD(g_lastSize),
              HIWORD(g_lastSize));
        // The same without a message: the resync comes from Present within a second.
        g_lastSize = 0;
        SetWindowPos(hwnd, nullptr, 0, 0, 0, 0, SWP_NOSIZE | SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        Pump();
        CHECK(g_lastSize == 0, "reconfigured window: no WM_SIZE at once without the game's message");
        for (int i = 0; i < 20 && !g_lastSize; ++i) Pump(), sc->Present(0, 0), Sleep(50);
        CHECK(LOWORD(g_lastSize) == lw && HIWORD(g_lastSize) == lh,
              "reconfigured window: without a message, a logical WM_SIZE from Present (%ux%u)", LOWORD(g_lastSize),
              HIWORD(g_lastSize));
    }

    // 6) Release everything; the device must reach 0. On the way: once the game window is destroyed nothing is
    // converted, and the next game window (a new swapchain's) is followed.
    if (def) def->Release();
    if (sc4) sc4->Release();
    if (ctx4) ctx4->Release();
    if (dev5) dev5->Release();
    ULONG scRef = sc->Release();
    CHECK(scRef == 0, "swapchain final Release returns 0 (%lu)", scRef);
    DestroyWindow(hwnd);
    if (scaled) {
        CURSORINFO ci = {sizeof(ci)};
        GetCursorInfo(&ci);  // not hooked
        POINT p = {};
        GetCursorPos(&p);
        CHECK(p.x == ci.ptScreenPos.x && p.y == ci.ptScreenPos.y, "game window destroyed: the cursor reads real (%ld,%ld)",
              p.x, p.y);
        HWND hwnd2 = CreateWindowExW(0, wc.lpszClassName, L"proxytest 2", WS_POPUP, 0, 0, realW, realH, nullptr, nullptr,
                                     wc.hInstance, nullptr);
        scd.OutputWindow = hwnd2;
        scd.SampleDesc.Count = 1;
        IDXGISwapChain* sc2 = nullptr;
        hr = factory->CreateSwapChain(dev, &scd, &sc2);
        RECT cr = {};
        GetClientRect(hwnd2, &cr);
        CHECK(SUCCEEDED(hr) && cr.right == lw && cr.bottom == lh, "a new game window is followed: client %ldx%ld", cr.right,
              cr.bottom);
        // A game that recreates its window makes the new one (and its swapchain) before destroying the old one:
        // the new one is followed once the old one is gone.
        HWND hwnd3 = CreateWindowExW(0, wc.lpszClassName, L"proxytest 3", WS_POPUP, 0, 0, realW, realH, nullptr, nullptr,
                                     wc.hInstance, nullptr);
        scd.OutputWindow = hwnd3;
        IDXGISwapChain* sc3 = nullptr;
        hr = factory->CreateSwapChain(dev, &scd, &sc3);
        GetClientRect(hwnd3, &cr);
        CHECK(SUCCEEDED(hr) && cr.right == realW, "while the old window exists the newer one isn't followed yet (%ldx%ld)",
              cr.right, cr.bottom);
        if (sc2) sc2->Release();
        DestroyWindow(hwnd2);
        GetClientRect(hwnd3, &cr);
        CHECK(cr.right == lw && cr.bottom == lh, "the old window destroyed: the newer one is followed (%ldx%ld)", cr.right,
              cr.bottom);
        if (sc3) sc3->Release();
        DestroyWindow(hwnd3);
    }
    factory->Release();
    ULONG ctxRef = ctx->Release();
    ULONG devRef = dev->Release();
    CHECK(devRef == 0, "device final Release returns 0 (%lu, context %lu)", devRef, ctxRef);

    printf("%s: %d failure(s)\n", g_failures ? "FAILED" : "PASSED", g_failures);
    return g_failures ? 1 : 0;
}
