// Diagnostics (M2): see diag.h for the request commands. Everything here is idle unless a request or the
// hotkey asks for something; the per-call cost outside a trace is one relaxed atomic load.
#include "diag.h"
#include "wrappers.h"
#include "../shared/image.h"
#include "unwrap.h"
#include <algorithm>
#include <map>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace goh4k {

std::atomic<bool> g_traceActive{false};

static const GUID kResIdGuid = {0x5c2a8f31, 0x9d4e, 0x4b7a, {0x8e, 0x15, 0x2f, 0x6c, 0x7d, 0x90, 0xa1, 0x3b}};
static const GUID kShaderHashGuid = {0x8e4b1d22, 0x6a3f, 0x47c9, {0xb2, 0x58, 0x0d, 0x1e, 0x9f, 0x44, 0x6a, 0x5c}};
static std::atomic<UINT> g_nextResId{1};

// Trace state. The immediate context's lines live here; deferred contexts keep their own buffers until
// FinishCommandList, then they wait in g_clLines until ExecuteCommandList splices them in.
static std::mutex g_traceMutex;
static std::string g_traceTag;
static LONG64 g_traceFrame = 0;
static std::vector<std::string> g_traceLines;
static int g_traceEvent = 0;
static std::atomic<int> g_traceGen{0};
static int g_dumpEvent = -1;  // dumprt armed (also for ps: mode)
static std::string g_dumpPs;  // dumprt ps:<id prefix>: match by pixel shader instead of event number
static bool g_dumpDepth = false;
static std::string g_dumpTag;
static std::mutex g_clMutex;
static std::unordered_map<ID3D11CommandList*, std::vector<std::string>> g_clLines;
static std::mutex g_resMutex;
static std::map<UINT, std::string> g_traceRes;

static std::vector<std::string> g_pendingCaptures;  // render thread only
static bool g_hotkeyDown = false;
static ULONGLONG g_lastPoll = 0;

static std::mutex g_statsMutex;
static std::map<std::string, int> g_texStats;
struct ShaderEntry {
    char kind;
    std::vector<uint8_t> code;
};
static std::unordered_map<uint64_t, ShaderEntry> g_shaders;

static std::mutex g_fixMutex;
static std::map<std::string, bool> g_fixes;

// ---------------------------------------------------------------- small helpers

static std::string Stamp() {
    SYSTEMTIME st;
    GetLocalTime(&st);
    return Fmt("%04u%02u%02u-%02u%02u%02u", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
}

static std::wstring Widen(const std::string& s) { return std::wstring(s.begin(), s.end()); }

static std::string CleanTag(const std::string& t) {
    std::string o;
    for (char c : t) o += (isalnum((unsigned char)c) || c == '-' || c == '_') ? c : '_';
    return o.empty() ? std::string("x") : o.substr(0, 48);
}

static std::wstring CapturePath(const std::string& name) { return ProjectDir() + L"\\captures\\" + Widen(name); }

static void AppendFile(const std::wstring& path, const std::string& text) {
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    DWORD written;
    WriteFile(h, text.data(), (DWORD)text.size(), &written, nullptr);
    CloseHandle(h);
}

static void Report(LONG64 frame, const std::string& msg) {
    AppendFile(CapturePath("requests.log"), Fmt("%s frame %lld: %s\r\n", Stamp().c_str(), (long long)frame, msg.c_str()));
    Log("request: %s", msg.c_str());
}

std::string ResTag(ID3D11Resource* r) {
    if (!r) return "-";
    r = UnwrapResource(r);
    UINT id = 0, size = sizeof(id);
    if (FAILED(r->GetPrivateData(kResIdGuid, &size, &id)) || size != sizeof(id)) {
        id = g_nextResId++;
        r->SetPrivateData(kResIdGuid, sizeof(id), &id);
    }
    D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    r->GetType(&dim);
    std::string tag, full;
    if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
        D3D11_TEXTURE2D_DESC d;
        static_cast<ID3D11Texture2D*>(r)->GetDesc(&d);
        tag = Fmt("r%u:%ux%u:f%d", id, d.Width, d.Height, (int)d.Format);
        if (d.SampleDesc.Count > 1) tag += Fmt(":x%u", d.SampleDesc.Count);
        if (d.ArraySize > 1) tag += Fmt(":a%u", d.ArraySize);
        full = Fmt("r%u Texture2D %ux%u fmt=%d mips=%u array=%u msaa=%u bind=0x%x usage=%d cpu=0x%x misc=0x%x", id,
                   d.Width, d.Height, (int)d.Format, d.MipLevels, d.ArraySize, d.SampleDesc.Count, d.BindFlags,
                   (int)d.Usage, d.CPUAccessFlags, d.MiscFlags);
    } else if (dim == D3D11_RESOURCE_DIMENSION_BUFFER) {
        D3D11_BUFFER_DESC d;
        static_cast<ID3D11Buffer*>(r)->GetDesc(&d);
        tag = Fmt("b%u:%uB", id, d.ByteWidth);
        full = Fmt("b%u Buffer %u bytes bind=0x%x usage=%d misc=0x%x stride=%u", id, d.ByteWidth, d.BindFlags,
                   (int)d.Usage, d.MiscFlags, d.StructureByteStride);
    } else if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE3D) {
        D3D11_TEXTURE3D_DESC d;
        static_cast<ID3D11Texture3D*>(r)->GetDesc(&d);
        tag = Fmt("r%u:%ux%ux%u:f%d", id, d.Width, d.Height, d.Depth, (int)d.Format);
        full = "r" + std::to_string(id) + " Texture3D " + tag;
    } else {
        tag = Fmt("r%u:dim%d", id, (int)dim);
        full = tag;
    }
    if (TraceActive()) {
        std::lock_guard<std::mutex> lock(g_resMutex);
        g_traceRes.emplace(id, full);
    }
    return tag;
}

std::string ViewTag(ID3D11View* v) {
    if (!v) return "-";
    ID3D11Resource* r = nullptr;
    v->GetResource(&r);
    std::string s = ResTag(r);
    if (r) r->Release();
    return s;
}

std::string ShaderId(ID3D11DeviceChild* shader) {
    if (!shader) return "-";
    uint64_t h = 0;
    UINT size = sizeof(h);
    if (FAILED(shader->GetPrivateData(kShaderHashGuid, &size, &h)) || size != sizeof(h)) return "?";
    return Fmt("%016llx", (unsigned long long)h);
}

template <typename T>
static void ReleaseAll(T** p, int n) {
    for (int i = 0; i < n; ++i)
        if (p[i]) p[i]->Release();
}

static std::string SrvList(ID3D11ShaderResourceView** srv, int n) {
    std::string s;
    for (int i = 0; i < n; ++i)
        if (srv[i]) s += Fmt("%s%d:", s.empty() ? "" : ",", i) + ViewTag(srv[i]);
    return "[" + s + "]";
}

static std::string GraphicsState(ID3D11DeviceContext* c) {
    ID3D11VertexShader* vs = nullptr;
    ID3D11PixelShader* ps = nullptr;
    c->VSGetShader(&vs, nullptr, nullptr);
    c->PSGetShader(&ps, nullptr, nullptr);
    std::string s = " vs=" + ShaderId(vs) + " ps=" + ShaderId(ps);
    if (vs) vs->Release();
    if (ps) ps->Release();

    ID3D11RenderTargetView* rtv[8] = {};
    ID3D11DepthStencilView* dsv = nullptr;
    c->OMGetRenderTargets(8, rtv, &dsv);
    std::string rts;
    for (int i = 0; i < 8; ++i)
        if (rtv[i]) rts += Fmt("%s%d:", rts.empty() ? "" : ",", i) + ViewTag(rtv[i]);
    s += " rt=[" + rts + "] ds=" + ViewTag(dsv);
    ReleaseAll(rtv, 8);
    if (dsv) dsv->Release();

    D3D11_VIEWPORT vp[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    UINT nvp = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    c->RSGetViewports(&nvp, vp);
    if (nvp)
        s += Fmt(" vp=%g,%g,%gx%g%s", vp[0].TopLeftX, vp[0].TopLeftY, vp[0].Width, vp[0].Height, nvp > 1 ? "(+more)" : "");
    else
        s += " vp=-";
    ID3D11RasterizerState* rs = nullptr;
    c->RSGetState(&rs);
    bool scissor = false;
    if (rs) {
        D3D11_RASTERIZER_DESC rd;
        rs->GetDesc(&rd);
        scissor = rd.ScissorEnable != FALSE;
        rs->Release();
    }
    if (scissor) {
        D3D11_RECT sc[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
        UINT nsc = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
        c->RSGetScissorRects(&nsc, sc);
        s += nsc ? Fmt(" sc=%ld,%ld-%ld,%ld", sc[0].left, sc[0].top, sc[0].right, sc[0].bottom) : " sc=none";
    }
    ID3D11ShaderResourceView* srv[16] = {};
    c->PSGetShaderResources(0, 16, srv);
    s += " srv=" + SrvList(srv, 16);
    ReleaseAll(srv, 16);
    return s;
}

static std::string ComputeState(ID3D11DeviceContext* c) {
    ID3D11ComputeShader* cs = nullptr;
    c->CSGetShader(&cs, nullptr, nullptr);
    std::string s = " cs=" + ShaderId(cs);
    if (cs) cs->Release();
    ID3D11ShaderResourceView* srv[16] = {};
    c->CSGetShaderResources(0, 16, srv);
    s += " srv=" + SrvList(srv, 16);
    ReleaseAll(srv, 16);
    ID3D11UnorderedAccessView* uav[8] = {};
    c->CSGetUnorderedAccessViews(0, 8, uav);
    std::string us;
    for (int i = 0; i < 8; ++i)
        if (uav[i]) us += Fmt("%s%d:", us.empty() ? "" : ",", i) + ViewTag(uav[i]);
    s += " uav=[" + us + "]";
    ReleaseAll(uav, 8);
    return s;
}

// ---------------------------------------------------------------- capture

static DXGI_FORMAT ResolveFormat(DXGI_FORMAT f) {
    switch (f) {
        case DXGI_FORMAT_B8G8R8A8_TYPELESS: return DXGI_FORMAT_B8G8R8A8_UNORM;
        case DXGI_FORMAT_R8G8B8A8_TYPELESS: return DXGI_FORMAT_R8G8B8A8_UNORM;
        case DXGI_FORMAT_R10G10B10A2_TYPELESS: return DXGI_FORMAT_R10G10B10A2_UNORM;
        case DXGI_FORMAT_R16G16B16A16_TYPELESS: return DXGI_FORMAT_R16G16B16A16_FLOAT;
        case DXGI_FORMAT_R16G16_TYPELESS: return DXGI_FORMAT_R16G16_FLOAT;
        case DXGI_FORMAT_R32_TYPELESS: return DXGI_FORMAT_R32_FLOAT;
        case DXGI_FORMAT_D24_UNORM_S8_UINT: case DXGI_FORMAT_R24G8_TYPELESS: case DXGI_FORMAT_D32_FLOAT:
        case DXGI_FORMAT_D16_UNORM: case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: case DXGI_FORMAT_R32G8X24_TYPELESS:
            return DXGI_FORMAT_UNKNOWN;  // depth can't be resolved
        default: return f;
    }
}

static DXGI_FORMAT StagingFormat(DXGI_FORMAT f) {  // depth formats can't be staging; use their typeless family
    switch (f) {
        case DXGI_FORMAT_D24_UNORM_S8_UINT: return DXGI_FORMAT_R24G8_TYPELESS;
        case DXGI_FORMAT_D32_FLOAT: return DXGI_FORMAT_R32_TYPELESS;
        case DXGI_FORMAT_D16_UNORM: return DXGI_FORMAT_R16_TYPELESS;
        case DXGI_FORMAT_D32_FLOAT_S8X24_UINT: return DXGI_FORMAT_R32G8X24_TYPELESS;
        default: return f;
    }
}

// Copies subresource 0 of a 2D texture to the CPU (resolving MSAA), converts it to BGR, and writes
// <name>.png, <name>-preview.png and <name>.txt into captures/ on a worker thread.
static bool CaptureTexture(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Resource* res, const std::string& name,
                           const std::string& note, std::string* result) {
    res = UnwrapResource(res);
    D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    res->GetType(&dim);
    if (dim != D3D11_RESOURCE_DIMENSION_TEXTURE2D) return *result = "not a 2D texture", false;
    D3D11_TEXTURE2D_DESC d;
    static_cast<ID3D11Texture2D*>(res)->GetDesc(&d);
    ID3D11Resource* src = res;
    src->AddRef();
    if (d.SampleDesc.Count > 1) {
        DXGI_FORMAT rf = ResolveFormat(d.Format);
        if (rf == DXGI_FORMAT_UNKNOWN) {
            src->Release();
            return *result = Fmt("MSAA format %d can't be resolved", (int)d.Format), false;
        }
        D3D11_TEXTURE2D_DESC rd = d;
        rd.SampleDesc = {1, 0};
        rd.MipLevels = rd.ArraySize = 1;
        rd.Usage = D3D11_USAGE_DEFAULT;
        rd.BindFlags = rd.CPUAccessFlags = rd.MiscFlags = 0;
        ID3D11Texture2D* resolved = nullptr;
        if (FAILED(dev->CreateTexture2D(&rd, nullptr, &resolved))) {
            src->Release();
            return *result = "creating the resolve target failed", false;
        }
        ctx->ResolveSubresource(resolved, 0, src, 0, rf);
        src->Release();
        src = resolved;
    }
    D3D11_TEXTURE2D_DESC sd = d;
    sd.Format = StagingFormat(d.Format);
    sd.SampleDesc = {1, 0};
    sd.MipLevels = sd.ArraySize = 1;
    sd.Usage = D3D11_USAGE_STAGING;
    sd.BindFlags = sd.MiscFlags = 0;
    sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ID3D11Texture2D* staging = nullptr;
    if (FAILED(dev->CreateTexture2D(&sd, nullptr, &staging))) {
        src->Release();
        return *result = Fmt("creating a staging texture (format %d) failed", (int)sd.Format), false;
    }
    ctx->CopySubresourceRegion(staging, 0, 0, 0, 0, src, 0, nullptr);
    src->Release();
    D3D11_MAPPED_SUBRESOURCE m = {};
    HRESULT hr = ctx->Map(staging, 0, D3D11_MAP_READ, 0, &m);
    if (FAILED(hr)) {
        staging->Release();
        return *result = Fmt("Map failed 0x%08lx", hr), false;
    }
    img::Bgr bgr;
    std::string why;
    bool ok = img::ToBgr(sd.Format, m.pData, m.RowPitch, (int)d.Width, (int)d.Height, &bgr, &why);
    ctx->Unmap(staging, 0);
    staging->Release();
    if (!ok) return *result = why, false;

    std::string info = Fmt("%s\r\nsource: %ux%u format %d msaa %u mips %u array %u bind 0x%x\r\n%s\r\n", note.c_str(), d.Width,
                           d.Height, (int)d.Format, d.SampleDesc.Count, d.MipLevels, d.ArraySize, d.BindFlags,
                           why.c_str());
    std::wstring base = CapturePath(name);
    std::thread([bgr = std::move(bgr), base, info]() {
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        int div = bgr.w >= 1920 ? 4 : bgr.w >= 960 ? 2 : 1;
        bool a = img::SavePng(base + L".png", bgr);
        bool b = img::SavePng(base + L"-preview.png", img::Downscale(bgr, div));
        WriteTextFile(base + L".txt", info + (a && b ? "" : "PNG WRITE FAILED\r\n"));
        CoUninitialize();
    }).detach();
    *result = Fmt("%s.png (%ux%u, format %d%s)", name.c_str(), d.Width, d.Height, (int)d.Format,
                  d.SampleDesc.Count > 1 ? Fmt(", resolved from %ux MSAA", d.SampleDesc.Count).c_str() : "");
    return true;
}

// ---------------------------------------------------------------- trace

void TraceRecord(WrappedContext* ctx, TraceKind kind, const std::string& call) {
    std::string state = kind == TraceKind::Draw       ? GraphicsState(ctx->RealCtx())
                        : kind == TraceKind::Dispatch ? ComputeState(ctx->RealCtx())
                                                      : std::string();
    if (ctx->IsImmediate()) {
        std::lock_guard<std::mutex> lock(g_traceMutex);
        g_traceLines.push_back(Fmt("#%d ", ++g_traceEvent) + call + state);
    } else {
        int gen = g_traceGen.load();
        if (ctx->traceGen != gen) {
            ctx->traceLines.clear();
            ctx->traceGen = gen;
        }
        ctx->traceLines.push_back(Fmt("  d%d.%zu ", ctx->Id(), ctx->traceLines.size() + 1) + call + state);
    }
}

void TraceAfterAction(WrappedContext* ctx) {
    if (!ctx->IsImmediate() || g_dumpEvent < 0) return;
    if (g_dumpPs.empty()) {
        if (g_dumpEvent != g_traceEvent) return;
    } else {  // dumprt ps:<id>: after the first immediate draw with that pixel shader
        ID3D11PixelShader* ps = nullptr;
        ctx->RealCtx()->PSGetShader(&ps, nullptr, nullptr);
        bool match = ps && ShaderId(ps).compare(0, g_dumpPs.size(), g_dumpPs) == 0;
        if (ps) ps->Release();
        if (!match) return;
        g_dumpPs.clear();
    }
    g_dumpEvent = -1;
    ID3D11RenderTargetView* rtv = nullptr;
    ID3D11DepthStencilView* dsv = nullptr;
    ctx->RealCtx()->OMGetRenderTargets(1, &rtv, &dsv);
    ID3D11View* view = g_dumpDepth ? (ID3D11View*)dsv : (ID3D11View*)rtv;
    std::string result = g_dumpDepth ? "no depth target bound" : "no render target 0 bound";
    if (view) {
        ID3D11Resource* r = nullptr;
        view->GetResource(&r);
        std::string name = Stamp() + "-" + g_dumpTag + Fmt("-e%d-%s", g_traceEvent, g_dumpDepth ? "ds" : "rt0");
        std::string tag = ResTag(r);
        CaptureTexture(ctx->Device()->Real(), ctx->RealCtx(), r, name,
                       Fmt("dumprt %s after event #%d, resource %s", g_dumpDepth ? "ds" : "rt0", g_traceEvent, tag.c_str()),
                       &result);
        r->Release();
    }
    if (rtv) rtv->Release();
    if (dsv) dsv->Release();
    std::lock_guard<std::mutex> lock(g_traceMutex);
    g_traceLines.push_back("    -> dumprt: " + result);
}

void TraceFinishCommandList(WrappedContext* ctx, ID3D11CommandList* cl) {
    if (!cl || ctx->traceGen != g_traceGen.load() || ctx->traceLines.empty()) return;
    std::lock_guard<std::mutex> lock(g_clMutex);
    g_clLines[cl] = std::move(ctx->traceLines);
    ctx->traceLines.clear();
}

void TraceExecuteCommandList(ID3D11CommandList* cl) {
    std::vector<std::string> lines;
    {
        std::lock_guard<std::mutex> lock(g_clMutex);
        auto it = g_clLines.find(cl);
        if (it != g_clLines.end()) {
            lines = std::move(it->second);
            g_clLines.erase(it);
        }
    }
    std::lock_guard<std::mutex> lock(g_traceMutex);
    if (lines.empty())
        g_traceLines.push_back("    (command list recorded before the trace started, or empty)");
    else
        g_traceLines.insert(g_traceLines.end(), lines.begin(), lines.end());
}

static void StartTrace(const std::string& tag, LONG64 frame) {
    std::lock_guard<std::mutex> lock(g_traceMutex);
    g_traceTag = tag;
    g_traceFrame = frame + 1;
    g_traceLines.clear();
    g_traceEvent = 0;
    g_traceGen++;
    {
        std::lock_guard<std::mutex> l2(g_resMutex);
        g_traceRes.clear();
    }
    {
        std::lock_guard<std::mutex> l3(g_clMutex);
        g_clLines.clear();
    }
    g_traceActive = true;
}

static void FinishTrace(IDXGISwapChain* sc) {
    g_traceActive = false;
    std::lock_guard<std::mutex> lock(g_traceMutex);
    DXGI_SWAP_CHAIN_DESC d = {};
    sc->GetDesc(&d);
    std::string out = Fmt("trace '%s' of frame %lld, %d immediate-context events\r\nswapchain %s\r\n"
                          "Lines: #N = immediate context event (dumprt numbering); dK.N = deferred context K, "
                          "spliced in at its ExecuteCommandList.\r\n\r\n",
                          g_traceTag.c_str(), (long long)g_traceFrame, g_traceEvent, DescribeSwapChainDesc(d).c_str());
    for (auto& l : g_traceLines) out += l + "\r\n";
    out += "\r\n--- resources\r\n";
    {
        std::lock_guard<std::mutex> l2(g_resMutex);
        for (auto& r : g_traceRes) out += r.second + "\r\n";
    }
    {
        std::lock_guard<std::mutex> l3(g_clMutex);
        if (!g_clLines.empty()) out += Fmt("\r\n%zu recorded command lists were not executed in this frame\r\n", g_clLines.size());
        g_clLines.clear();
    }
    if (g_dumpEvent >= 0)
        out += g_dumpPs.empty() ? Fmt("\r\ndumprt: event #%d was not reached\r\n", g_dumpEvent)
                                : "\r\ndumprt: no draw with ps " + g_dumpPs + "\r\n";
    g_dumpEvent = -1;
    g_dumpPs.clear();
    std::string name = Stamp() + "-" + g_traceTag + "-trace.txt";
    WriteTextFile(CapturePath(name), out);
    Report(g_traceFrame, Fmt("trace -> captures\\%s (%d events)", name.c_str(), g_traceEvent));
    g_traceLines.clear();
}

// ---------------------------------------------------------------- requests

static void WriteStats(const std::string& tag, LONG64 frame, double fps) {
    std::string out = Fmt("stats '%s' at frame %lld, %.1f fps\r\n\r\n--- textures created (count  desc)\r\n", tag.c_str(),
                          (long long)frame, fps);
    std::map<char, int> kinds;
    {
        std::lock_guard<std::mutex> lock(g_statsMutex);
        std::vector<std::pair<int, std::string>> rows;
        for (auto& t : g_texStats) rows.push_back({t.second, t.first});
        std::sort(rows.begin(), rows.end(), [](auto& a, auto& b) { return a.first > b.first; });
        for (auto& r : rows) out += Fmt("%6d  %s\r\n", r.first, r.second.c_str());
        for (auto& s : g_shaders) kinds[s.second.kind]++;
    }
    out += "\r\n--- shaders (distinct bytecode)\r\n";
    for (auto& k : kinds) out += Fmt("%c: %d\r\n", k.first, k.second);
    out += "\r\n--- fixes\r\n";
    {
        std::lock_guard<std::mutex> lock(g_fixMutex);
        for (auto& f : g_fixes) out += f.first + (f.second ? " on\r\n" : " off\r\n");
    }
    std::string name = Stamp() + "-" + tag + "-stats.txt";
    WriteTextFile(CapturePath(name), out);
    Report(frame, "stats -> captures\\" + name);
}

static void DumpShader(const std::string& id, LONG64 frame) {
    uint64_t h = _strtoui64(id.c_str(), nullptr, 16);
    std::vector<uint8_t> code;
    {
        std::lock_guard<std::mutex> lock(g_statsMutex);
        auto it = g_shaders.find(h);
        if (it != g_shaders.end()) code = it->second.code;
    }
    if (code.empty()) return Report(frame, "dumpshader " + id + ": unknown shader");
    std::wstring dir = ProjectDir() + L"\\scratch\\shaders";
    CreateDirectoryW(dir.c_str(), nullptr);
    WriteTextFile(dir + L"\\" + Widen(id) + L".cso", std::string(code.begin(), code.end()));
    Report(frame, Fmt("dumpshader %s -> scratch\\shaders\\%s.cso (%zu bytes)", id.c_str(), id.c_str(), code.size()));
}

static void PollRequests(LONG64 frame, double fps) {
    std::wstring path = CapturePath("request.txt");
    if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) return;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ | DELETE, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;  // still being written; next poll
    char buf[4096];
    DWORD n = 0;
    ReadFile(h, buf, sizeof(buf) - 1, &n, nullptr);
    CloseHandle(h);
    DeleteFileW(path.c_str());
    buf[n] = 0;

    char* save = nullptr;
    for (char* line = strtok_s(buf, "\r\n", &save); line; line = strtok_s(nullptr, "\r\n", &save)) {
        std::vector<std::string> a;
        char* s2 = nullptr;
        for (char* t = strtok_s(line, " \t", &s2); t; t = strtok_s(nullptr, " \t", &s2)) a.push_back(t);
        if (a.empty()) continue;
        auto arg = [&](size_t i, const char* def) { return i < a.size() ? a[i] : std::string(def); };
        if (a[0] == "capture") {
            g_pendingCaptures.push_back(CleanTag(arg(1, "cap")));
        } else if (a[0] == "trace") {
            StartTrace(CleanTag(arg(1, "trace")), frame);
            Report(frame, "trace started");
        } else if (a[0] == "dumprt" && a.size() >= 2) {
            StartTrace(CleanTag(arg(2, "dump")), frame);
            g_dumpPs = a[1].rfind("ps:", 0) == 0 ? a[1].substr(3) : std::string();
            g_dumpEvent = g_dumpPs.empty() ? atoi(a[1].c_str()) : 0;
            g_dumpDepth = arg(3, "") == "ds";
            g_dumpTag = CleanTag(arg(2, "dump"));
            Report(frame, g_dumpPs.empty() ? Fmt("dumprt armed for event #%d", g_dumpEvent)
                                           : "dumprt armed for the first draw with ps " + g_dumpPs);
        } else if (a[0] == "stats") {
            WriteStats(CleanTag(arg(1, "stats")), frame, fps);
        } else if (a[0] == "fix" && a.size() >= 3) {
            {
                std::lock_guard<std::mutex> lock(g_fixMutex);
                g_fixes[a[1]] = a[2] == "on";
            }
            Report(frame, "fix " + a[1] + " " + (a[2] == "on" ? "on" : "off"));
        } else if (a[0] == "dumpshader" && a.size() >= 2) {
            DumpShader(a[1], frame);
        } else {
            Report(frame, "unknown request: " + a[0]);
        }
    }
}

void DiagBeforePresent(IDXGISwapChain* sc, WrappedDevice* device, LONG64 frame) {
    if (TraceActive()) FinishTrace(sc);
    if (g_pendingCaptures.empty() || !device) return;
    for (auto& tag : g_pendingCaptures) {
        ID3D11Texture2D* bb = nullptr;
        std::string result = "GetBuffer failed";
        if (SUCCEEDED(sc->GetBuffer(0, IID_PPV_ARGS(&bb)))) {
            std::string name = Stamp() + "-" + tag;
            CaptureTexture(device->Real(), device->Immediate()->RealCtx(), bb, name,
                           Fmt("capture '%s' of frame %lld (backbuffer)", tag.c_str(), (long long)frame), &result);
            bb->Release();
        }
        Report(frame, "capture -> captures\\" + result);
    }
    g_pendingCaptures.clear();
}

void DiagAfterPresent(IDXGISwapChain*, WrappedDevice*, LONG64 frame, double fps) {
    bool down = (GetAsyncKeyState(VK_CONTROL) & 0x8000) && (GetAsyncKeyState(VK_SHIFT) & 0x8000) &&
                (GetAsyncKeyState(VK_F11) & 0x8000);
    if (down && !g_hotkeyDown) g_pendingCaptures.push_back("hotkey");
    g_hotkeyDown = down;
    ULONGLONG now = GetTickCount64();
    if (now - g_lastPoll >= 250) {
        g_lastPoll = now;
        PollRequests(frame, fps);
    }
}

// ---------------------------------------------------------------- registries

void RegisterShader(ID3D11DeviceChild* shader, const void* bytecode, SIZE_T length, char kind) {
    if (!shader || !bytecode || !length) return;
    const uint8_t* p = (const uint8_t*)bytecode;
    uint64_t h = 0;
    if (length >= 20 && memcmp(p, "DXBC", 4) == 0) {
        memcpy(&h, p + 4, sizeof(h));  // first half of the container's own checksum
    } else {
        h = 1469598103934665603ull;
        for (SIZE_T i = 0; i < length; ++i) h = (h ^ p[i]) * 1099511628211ull;
    }
    shader->SetPrivateData(kShaderHashGuid, sizeof(h), &h);
    std::lock_guard<std::mutex> lock(g_statsMutex);
    if (g_shaders.find(h) == g_shaders.end()) g_shaders.emplace(h, ShaderEntry{kind, std::vector<uint8_t>(p, p + length)});
}

void RecordTextureCreate(const D3D11_TEXTURE2D_DESC& d, bool initialData) {
    std::string key = Fmt("%5ux%-5u f%-3d bind=0x%-3x usage=%d msaa=%u mips=%-2u array=%-2u misc=0x%x%s", d.Width,
                          d.Height, (int)d.Format, d.BindFlags, (int)d.Usage, d.SampleDesc.Count, d.MipLevels,
                          d.ArraySize, d.MiscFlags, initialData ? " +data" : "");
    std::lock_guard<std::mutex> lock(g_statsMutex);
    ++g_texStats[key];
}

bool FixOn(const char* name) {
    std::lock_guard<std::mutex> lock(g_fixMutex);
    auto it = g_fixes.find(name);
    return it != g_fixes.end() && it->second;
}

}  // namespace goh4k
