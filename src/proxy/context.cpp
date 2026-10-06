// WrappedContext action overrides: forward to the real context, and while a trace is active record each
// action with the state it ran with (diag.cpp). For native scaling (scale.h) a few full-screen viewports and
// scissors are applied differently from how the game set them; see "full-screen rules" below.
#include "wrappers.h"
#include "diag.h"
#include "scale.h"
#include <algorithm>
#include <atomic>

namespace goh4k {

void WrappedContext::ResetTrackedState() {
    m_numVp = m_numSc = 0;
    m_uiTarget = 0;
    m_uiVpSwapped = false;
    m_vs = nullptr;
    m_vsHashKnown = false;
}

// ---------------------------------------------------------------- full-screen rules
// The game's render targets follow its resolution setting (the swapchain's buffer size, "real"), its UI
// projection uses the client size it was told ("logical"), and its viewports come from either: some from the
// backbuffer's desc (the real screen), some from the client size (the logical screen). Three mismatches
// follow, fixed on full-screen rects only:
// 1. UI label surfaces (render targets smaller than the screen) are drawn with the stale real-screen
//    viewport, so the glyphs come out magnified and clipped: applied as the logical screen, as native 1080p
//    does.
// 2. Tone-map/glow passes on screen-sized targets set the logical-screen viewport, so the image covers only
//    the top-left part: applied as the real screen. Stock native 4K never sets a 1920x1080 viewport on a
//    3840x2160 target (captures/20261002-122821-mission-trace.txt).
// 3. Screen-sized textures the UI draws as images (the blurred backdrop behind prompts) are sampled with
//    coordinates of logical / texture size, so only their top-left part shows, magnified. Such a UI draw (see
//    UiBeforeDraw) gets a stand-in texture k times as large with the image copied 1:1 into its top-left, so
//    those coordinates cover the whole image. The image itself is left alone: the game also draws it full
//    screen in the frame a second popup opens (a 1.x/early-2.0 fix that shrank the image into its top-left
//    showed that frame as a small copy of the screen in the top-left corner, captures/v2/burst-exitq/064).
// "Real screen" is the buffer size, not the window's: in windowed modes the two can differ. Other rects and
// other targets are left alone; RSGet* return the game's own values. 114 traces of menus, missions,
// Conquest and the tech tree never enabled scissoring, so partial scissors aren't converted. "fix nouivp on"
// switches all three off for A/B. HBAO+ (the game's SSAO) sets viewports that match its own targets, so the
// rules never change those; when it restores the game's state, the game's viewport gets the rules like any
// other (A/B with the 1.x exemption: no difference, captures/v2/hb-heats.png).

enum UiTarget { kUiOther = 0, kUiBelow = 1, kUiScreen = 2 };

static const GUID kUiClassGuid = {0x2e7b4c19, 0x6d58, 0x4a3e, {0xa1, 0x07, 0x5b, 0x92, 0xc8, 0x3f, 0x44, 0xd0}};

struct UiClassCache {
    uint32_t generation;
    int cls;            // kUiOther / kUiBelow / kUiScreen: from the size only
    bool renderTarget;  // its texture can be a render target (rule 3's images are)
};

// The size class of a view's texture against the current buffer size, cached on the view (one private-data
// read per bind while the state is unchanged; a view the game frees takes its answer with it).
static UiClassCache UiViewClass(ID3D11View* v, bool render, const UiState& s) {
    uint32_t gen = UiStateGeneration();
    UiClassCache c = {};
    UINT size = sizeof(c);
    if (SUCCEEDED(v->GetPrivateData(kUiClassGuid, &size, &c)) && size == sizeof(c) && c.generation == gen) return c;
    c = {gen, kUiOther, false};
    ID3D11Resource* r = nullptr;
    v->GetResource(&r);
    ID3D11Texture2D* t = nullptr;
    if (r && SUCCEEDED(r->QueryInterface(IID_PPV_ARGS(&t)))) {
        D3D11_TEXTURE2D_DESC d;
        t->GetDesc(&d);
        if (d.Width == s.bw && d.Height == s.bh) c.cls = kUiScreen;
        else if (render && (d.Width < s.bw || d.Height < s.bh)) c.cls = kUiBelow;
        c.renderTarget = (d.BindFlags & D3D11_BIND_RENDER_TARGET) != 0;
        t->Release();
    }
    if (r) r->Release();
    v->SetPrivateData(kUiClassGuid, sizeof(c), &c);
    return c;
}

static int UiTargetClass(ID3D11View* v, bool render, const UiState& s) {
    return v && s.bw && s.bh ? UiViewClass(v, render, s).cls : kUiOther;
}

void WrappedContext::UiTargetsChanged(ID3D11RenderTargetView* const* rtvs, UINT numRtvs, ID3D11DepthStencilView* dsv) {
    UiState s = UiSnapshot();
    int c = kUiOther;
    if (s.active) {
        ID3D11RenderTargetView* rt0 = rtvs && numRtvs ? rtvs[0] : nullptr;
        c = rt0 ? UiTargetClass(rt0, true, s) : UiTargetClass(dsv, false, s);
    }
    if (c != m_uiTarget) {
        m_uiTarget = c;
        if (m_numVp) UiApplyViewports();
        if (m_numSc) UiApplyScissors();
    }
}

// The full-screen size to swap on the bound target, and what it becomes.
static bool UiSwap(int target, UINT* fromW, UINT* fromH, UINT* toW, UINT* toH) {
    if (target == kUiOther) return false;
    UiState s = UiSnapshot();
    if (!s.active || !s.bw || !s.bh) return false;
    if (target == kUiScreen) *fromW = s.lw, *fromH = s.lh, *toW = s.bw, *toH = s.bh;
    else *fromW = s.bw, *fromH = s.bh, *toW = s.lw, *toH = s.lh;
    return *fromW != *toW || *fromH != *toH;
}

// Logged once per rule and kind of rect.
static void NoteRule(int target, bool scissor, UINT fw, UINT fh, UINT tw, UINT th) {
    static std::atomic<bool> noted[6];
    if (noted[target * 2 + (scissor ? 1 : 0)].exchange(true)) return;
    static const char* const kNames[6] = {"", "", "ui-label-viewport", "ui-label-scissor", "ui-screen-viewport",
                                          "ui-screen-scissor"};
    Log("note [%s]: full-screen %s %ux%u applied as %ux%u", kNames[target * 2 + (scissor ? 1 : 0)],
        scissor ? "scissor" : "viewport", fw, fh, tw, th);
}

void WrappedContext::UiApplyViewports() {
    UINT fw = 0, fh = 0, tw = 0, th = 0;
    bool rule = m_numVp && UiSwap(m_uiTarget, &fw, &fh, &tw, &th) && !FixOn("nouivp");
    m_uiVpSwapped = false;
    if (!rule) return m_real->RSSetViewports(m_numVp, m_numVp ? m_vp : nullptr);
    D3D11_VIEWPORT v[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    for (UINT i = 0; i < m_numVp; ++i) {
        v[i] = m_vp[i];
        if (v[i].TopLeftX == 0 && v[i].TopLeftY == 0 && v[i].Width == (float)fw && v[i].Height == (float)fh) {
            v[i].Width = (float)tw;
            v[i].Height = (float)th;
            m_uiVpSwapped = true;
        }
    }
    if (m_uiVpSwapped) {
        NoteRule(m_uiTarget, false, fw, fh, tw, th);
        ++(m_uiTarget == kUiBelow ? Counters().labelViewports : Counters().screenViewports);
    }
    m_real->RSSetViewports(m_numVp, v);
}

void WrappedContext::UiApplyScissors() {
    UINT fw = 0, fh = 0, tw = 0, th = 0;
    bool rule = m_numSc && UiSwap(m_uiTarget, &fw, &fh, &tw, &th) && !FixOn("nouivp");
    if (!rule) return m_real->RSSetScissorRects(m_numSc, m_numSc ? m_sc : nullptr);
    D3D11_RECT r[D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE];
    bool changed = false;
    for (UINT i = 0; i < m_numSc; ++i) {
        r[i] = m_sc[i];
        if (r[i].left == 0 && r[i].top == 0 && r[i].right == (LONG)fw && r[i].bottom == (LONG)fh) {
            r[i].right = (LONG)tw;
            r[i].bottom = (LONG)th;
            changed = true;
        }
    }
    if (changed) NoteRule(m_uiTarget, true, fw, fh, tw, th);
    m_real->RSSetScissorRects(m_numSc, r);
}

// UI draws are recognised by their vertex shader, learnt from the indexed draws into label surfaces whose
// stale viewport rule 1 swapped (the game's UI draws quads with DrawIndexed; its post-processing uses Draw).
// By bytecode id, not pointer: the game creates the same UI shader more than once.
static const int kUiVsSlots = 8;
static std::atomic<uint64_t> g_uiVs[kUiVsSlots];

static bool IsUiVs(uint64_t h) {
    for (auto& p : g_uiVs)
        if (h && p.load(std::memory_order_relaxed) == h) return true;
    return false;
}

static void LearnUiVs(uint64_t h) {
    if (!h || IsUiVs(h)) return;
    for (auto& p : g_uiVs) {
        uint64_t none = 0;
        if (p.compare_exchange_strong(none, h)) {
            ++Counters().uiShaders;
            Log("ui: vertex shader %016llx recognised as the UI's (it draws into label surfaces)", (unsigned long long)h);
            return;
        }
        if (none == h) return;  // another thread just learnt it
    }
    static std::atomic<bool> full{false};
    if (!full.exchange(true)) Log("ui: more than %d UI vertex shaders; %016llx not learnt", kUiVsSlots, (unsigned long long)h);
}

// Rule 3's stand-in for a screen-sized image: large enough that the UI's coordinates (logical / buffer size)
// cover the image, with the image copied 1:1 into its top-left: k times the image in fullscreen, buffer² /
// logical in general (a borderless window can be larger than the buffers). The rest of it is never sampled.
// One per size and format, shared by every image (each draw copies its image in first; the GPU runs copy and
// draw in order), and released a few seconds after its last use (ReleaseStandIns), so it only costs memory
// while a popup is open: 133 MB at 4K, 26 MB at 1440p, 420 MB at 5K. Returned AddRef'd; null if the image
// can't have one (MSAA, arrays, mips, over the 16384 limit at 8K, out of memory). After a failed creation
// nothing is created for 10 s (instead of trying again on every draw of the popup).
static UINT StandInSize(UINT image, UINT buffer, UINT logical) {
    return logical ? (UINT)RatioUp((long)image, Ratio{(int64_t)buffer, (int64_t)logical}) : 0;
}

static const ULONGLONG kStandInRetryMs = 10000;

ID3D11ShaderResourceView* WrappedDevice::UiStandIn(ID3D11DeviceContext* ctx, ID3D11ShaderResourceView* gameSrv,
                                                   ID3D11Texture2D* img, const UiState& s) {
    ULONGLONG now = GetTickCount64(), failedAt = m_standInFailedAt;
    if (failedAt && now - failedAt < kStandInRetryMs) return nullptr;
    D3D11_TEXTURE2D_DESC d;
    img->GetDesc(&d);
    D3D11_SHADER_RESOURCE_VIEW_DESC vd;
    gameSrv->GetDesc(&vd);
    if (d.SampleDesc.Count != 1 || d.ArraySize != 1 || d.MipLevels != 1 || vd.ViewDimension != D3D11_SRV_DIMENSION_TEXTURE2D)
        return nullptr;
    UINT w = StandInSize(d.Width, s.bw, s.lw), h = StandInSize(d.Height, s.bh, s.lh);
    if (!w || !h || w > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION || h > D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION) {
        static std::atomic<bool> logged{false};
        if (!logged.exchange(true))
            Log("ui: a %ux%u stand-in would be over the %u limit: popup backdrops stay magnified", w, h,
                (unsigned)D3D11_REQ_TEXTURE2D_U_OR_V_DIMENSION);
        return nullptr;
    }
    AcquireSRWLockExclusive(&m_standInLock);
    StandIn* e = nullptr;
    for (StandIn& c : m_standIns)
        if (c.srv && c.w == w && c.h == h && c.texFmt == d.Format && c.viewFmt == vd.Format) e = &c;
    if (!e) {
        StandIn* slot = &m_standIns[0];  // a free slot, or the least recently used one
        for (StandIn& c : m_standIns)
            if (!c.srv || c.lastUse < slot->lastUse) slot = &c;
        ReleaseStandIn(slot, "replaced");
        D3D11_TEXTURE2D_DESC nd = d;
        nd.Width = w, nd.Height = h, nd.MipLevels = 1;
        nd.Usage = D3D11_USAGE_DEFAULT, nd.BindFlags = D3D11_BIND_SHADER_RESOURCE, nd.CPUAccessFlags = 0, nd.MiscFlags = 0;
        D3D11_SHADER_RESOURCE_VIEW_DESC nvd = vd;
        nvd.Texture2D.MostDetailedMip = 0, nvd.Texture2D.MipLevels = 1;
        HRESULT hr = m_real->CreateTexture2D(&nd, nullptr, &slot->tex);
        if (SUCCEEDED(hr)) hr = m_real->CreateShaderResourceView(slot->tex, &nvd, &slot->srv);
        if (FAILED(hr)) {
            ReleaseStandIn(slot, nullptr);
            m_standInFailedAt = now;
            ReleaseSRWLockExclusive(&m_standInLock);
            Log("ui: a %ux%u stand-in could not be created (0x%08lx): popup backdrops stay magnified, retried in 10 s", w,
                h, hr);
            return nullptr;
        }
        slot->w = w, slot->h = h, slot->texFmt = d.Format, slot->viewFmt = vd.Format;
        long long made = ++Counters().standInsMade;
        if (made <= 3 || made % 50 == 0)
            Log("ui: popup stand-in %ux%u fmt=%d (%.0f MB) for a %ux%u image the UI draws (#%lld)", w, h, (int)d.Format,
                (double)w * h * 4 / (1024 * 1024), d.Width, d.Height, made);
        e = slot;
    }
    if (failedAt) m_standInFailedAt = 0;
    e->lastUse = now;
    ID3D11Texture2D* tex = e->tex;  // kept alive by the view below
    ID3D11ShaderResourceView* srv = e->srv;
    srv->AddRef();
    ReleaseSRWLockExclusive(&m_standInLock);
    ctx->CopySubresourceRegion(tex, 0, 0, 0, 0, img, 0, nullptr);
    return srv;
}

void WrappedDevice::ReleaseStandIn(StandIn* e, const char* why) {
    if (e->srv && why) {
        long long n = Counters().standInsMade;
        if (n <= 3 || n % 50 == 0) Log("ui: popup stand-in %ux%u released (%s)", e->w, e->h, why);
    }
    if (e->srv) e->srv->Release();
    if (e->tex) e->tex->Release();
    *e = StandIn{};
}

void WrappedDevice::ReleaseStandIns(ULONGLONG unusedForMs) {
    ULONGLONG now = GetTickCount64();
    AcquireSRWLockExclusive(&m_standInLock);
    for (StandIn& c : m_standIns)
        if (c.srv && now - c.lastUse >= unusedForMs) ReleaseStandIn(&c, unusedForMs ? "unused for 5 s" : nullptr);
    ReleaseSRWLockExclusive(&m_standInLock);
}

// The UI draws a backdrop with coordinates 0..1 in the first frame its element exists, and with logical /
// texture size (0..0.5 at 4K) from then on (vertex data read back in the dev build, LOG.md). In stock native
// both mean the whole image, so only the later ones need the stand-in. Per image, the draws of each frame are
// counted: a draw beyond last frame's count is a new element and keeps the game's own image. Draws that resume
// after a display change (the game skips UI frames during a resolution switch) continue the old count.
static const GUID kUiDrawCountGuid = {0x8c2d5f63, 0x1a7e, 0x4b93, {0xb4, 0x0c, 0x6e, 0x15, 0x92, 0xa8, 0x3d, 0x77}};

struct UiDrawCount {
    LONG64 frame;
    UINT count, prevCount;
    uint32_t generation;  // UiStateGeneration at the last draw
};

static bool UiEstablishedDraw(ID3D11Resource* img) {
    UiDrawCount c = {};
    UINT size = sizeof(c);
    if (FAILED(img->GetPrivateData(kUiDrawCountGuid, &size, &c)) || size != sizeof(c)) c = {};
    LONG64 f = Counters().gameFrames;
    uint32_t gen = UiStateGeneration();
    if (c.frame != f) {
        bool continues = c.frame == f - 1 || (c.frame && c.generation != gen);
        c.prevCount = continues ? c.count : 0, c.frame = f, c.count = 0;
    }
    c.generation = gen;
    bool established = c.count < c.prevCount;
    ++c.count;
    img->SetPrivateData(kUiDrawCountGuid, sizeof(c), &c);
    return established;
}

// Before each indexed draw: learn the UI's vertex shader (rule 1 draws), or, for an established UI draw onto a
// screen-sized target that samples a screen-sized image, bind rule 3's stand-in at slot 0. True if UiAfterDraw
// must put the game's texture back.
//
// Slot 0 is read from the context, not tracked from PSSetShaderResources: D3D11 unbinds a view by itself when
// its texture is bound as a render target (the backdrop image is one), and the game may then free it.
bool WrappedContext::UiBeforeDraw() {
    if (!m_vs || (m_uiTarget != kUiBelow && m_uiTarget != kUiScreen)) return false;
    if (!m_vsHashKnown) m_vsHash = ShaderHash(m_vs), m_vsHashKnown = true;
    if (m_uiTarget == kUiBelow) {
        if (m_uiVpSwapped) LearnUiVs(m_vsHash);
        return false;
    }
    if (!IsUiVs(m_vsHash)) return false;
    UiState s = UiSnapshot();
    if (!s.active) return false;
    ID3D11ShaderResourceView* v = nullptr;
    m_real->PSGetShaderResources(0, 1, &v);  // AddRef'd
    if (!v) return false;
    UiClassCache c = UiViewClass(v, false, s);
    ID3D11Resource* img = nullptr;  // a texture: UiViewClass classified it
    if (c.cls == kUiScreen && c.renderTarget && !FixOn("nouivp")) v->GetResource(&img);
    if (!img) {
        v->Release();
        return false;
    }
    m_uiImageDraw = true;
    ++Counters().imageDraws;
    ID3D11ShaderResourceView* standIn =
        UiEstablishedDraw(img) ? m_device->UiStandIn(m_real, v, static_cast<ID3D11Texture2D*>(img), s) : nullptr;
    img->Release();
    if (!standIn) {
        v->Release();
        return false;
    }
    ++Counters().standInDraws;
    m_uiSavedPs0 = v;  // keeps our reference: the context drops its own while the stand-in is bound
    m_real->PSSetShaderResources(0, 1, &standIn);
    standIn->Release();  // the context holds it while bound
    return true;
}

void WrappedContext::UiAfterDraw() {
    m_real->PSSetShaderResources(0, 1, &m_uiSavedPs0);
    m_uiSavedPs0->Release();
    m_uiSavedPs0 = nullptr;
}

void STDMETHODCALLTYPE WrappedContext::OMSetRenderTargets(UINT NumViews, ID3D11RenderTargetView* const* ppRenderTargetViews,
                                                         ID3D11DepthStencilView* pDepthStencilView) {
    m_real->OMSetRenderTargets(NumViews, ppRenderTargetViews, pDepthStencilView);
    if (UiScaleConfigured()) UiTargetsChanged(ppRenderTargetViews, NumViews, pDepthStencilView);
}

void STDMETHODCALLTYPE WrappedContext::OMSetRenderTargetsAndUnorderedAccessViews(
    UINT NumRTVs, ID3D11RenderTargetView* const* ppRenderTargetViews, ID3D11DepthStencilView* pDepthStencilView,
    UINT UAVStartSlot, UINT NumUAVs, ID3D11UnorderedAccessView* const* ppUnorderedAccessViews,
    const UINT* pUAVInitialCounts) {
    m_real->OMSetRenderTargetsAndUnorderedAccessViews(NumRTVs, ppRenderTargetViews, pDepthStencilView, UAVStartSlot,
                                                      NumUAVs, ppUnorderedAccessViews, pUAVInitialCounts);
    if (UiScaleConfigured() && NumRTVs != D3D11_KEEP_RENDER_TARGETS_AND_DEPTH_STENCIL)
        UiTargetsChanged(ppRenderTargetViews, NumRTVs, pDepthStencilView);
}

void STDMETHODCALLTYPE WrappedContext::RSSetViewports(UINT NumViewports, const D3D11_VIEWPORT* pViewports) {
    if (!UiScaleConfigured()) return m_real->RSSetViewports(NumViewports, pViewports);
    m_numVp = std::min<UINT>(NumViewports, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
    if (pViewports) memcpy(m_vp, pViewports, m_numVp * sizeof(D3D11_VIEWPORT));
    else m_numVp = 0;
    UiApplyViewports();
}

void STDMETHODCALLTYPE WrappedContext::RSSetScissorRects(UINT NumRects, const D3D11_RECT* pRects) {
    if (!UiScaleConfigured()) return m_real->RSSetScissorRects(NumRects, pRects);
    m_numSc = std::min<UINT>(NumRects, D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE);
    if (pRects) memcpy(m_sc, pRects, m_numSc * sizeof(D3D11_RECT));
    else m_numSc = 0;
    UiApplyScissors();
}

void STDMETHODCALLTYPE WrappedContext::RSGetViewports(UINT* pNumViewports, D3D11_VIEWPORT* pViewports) {
    if (!UiScaleConfigured() || !pNumViewports) return m_real->RSGetViewports(pNumViewports, pViewports);
    if (pViewports) {
        UINT n = std::min(*pNumViewports, m_numVp);
        memcpy(pViewports, m_vp, n * sizeof(D3D11_VIEWPORT));
        for (UINT i = n; i < *pNumViewports; ++i) pViewports[i] = {};
    }
    *pNumViewports = m_numVp;
}

void STDMETHODCALLTYPE WrappedContext::RSGetScissorRects(UINT* pNumRects, D3D11_RECT* pRects) {
    if (!UiScaleConfigured() || !pNumRects) return m_real->RSGetScissorRects(pNumRects, pRects);
    if (pRects) {
        UINT n = std::min(*pNumRects, m_numSc);
        memcpy(pRects, m_sc, n * sizeof(D3D11_RECT));
        for (UINT i = n; i < *pNumRects; ++i) pRects[i] = {};
    }
    *pNumRects = m_numSc;
}

void STDMETHODCALLTYPE WrappedContext::ClearState() {
    m_real->ClearState();
    ResetTrackedState();
}

void STDMETHODCALLTYPE WrappedContext::VSSetShader(ID3D11VertexShader* pVertexShader,
                                                  ID3D11ClassInstance* const* ppClassInstances, UINT NumClassInstances) {
    m_real->VSSetShader(pVertexShader, ppClassInstances, NumClassInstances);
    if (pVertexShader != m_vs) m_vs = pVertexShader, m_vsHashKnown = false;
}

// ---------------------------------------------------------------- actions

// Arguments are only formatted while a trace is active.
#define TRACE_ACTION(kind, ...) \
    if (TraceActive()) TraceRecord(this, kind, Fmt(__VA_ARGS__))
#define TRACE_AFTER() \
    if (TraceActive()) TraceAfterAction(this)

static std::string BoxStr(const D3D11_BOX* b) {
    return b ? Fmt("box=%u,%u-%u,%u", b->left, b->top, b->right, b->bottom) : std::string("box=all");
}

// Dev-build self-check for rule 3: reads back the quad's texture coordinates (a GPU stall, diagnostics only)
// and logs every draw where the stand-in choice and the coordinates disagree, plus the first few that agree.
void WrappedContext::UiCheckImageDraw(UINT indexCount, UINT startIndex, INT baseVertex, bool standIn) {
    static std::atomic<int> agreed{0}, disagreed{0};
    if (!m_immediate || (agreed >= 12 && disagreed >= 100)) return;
    ID3D11Buffer* vb = nullptr;
    UINT stride = 0, off = 0;
    m_real->IAGetVertexBuffers(0, 1, &vb, &stride, &off);
    ID3D11Buffer* ib = nullptr;
    DXGI_FORMAT ifmt = DXGI_FORMAT_UNKNOWN;
    UINT ioff = 0;
    m_real->IAGetIndexBuffer(&ib, &ifmt, &ioff);
    auto readBack = [&](ID3D11Buffer* b, std::vector<uint8_t>* out) {
        D3D11_BUFFER_DESC bd;
        b->GetDesc(&bd);
        D3D11_BUFFER_DESC sd = {bd.ByteWidth, D3D11_USAGE_STAGING, 0, D3D11_CPU_ACCESS_READ, 0, 0};
        ID3D11Buffer* st = nullptr;
        if (FAILED(m_device->Real()->CreateBuffer(&sd, nullptr, &st))) return false;
        m_real->CopyResource(st, b);
        D3D11_MAPPED_SUBRESOURCE m = {};
        bool ok = SUCCEEDED(m_real->Map(st, 0, D3D11_MAP_READ, 0, &m));
        if (ok) out->assign((const uint8_t*)m.pData, (const uint8_t*)m.pData + bd.ByteWidth), m_real->Unmap(st, 0);
        st->Release();
        return ok;
    };
    std::vector<uint8_t> ibd, vbd;
    float uvMax = -1;
    if (vb && ib && stride >= 36 && readBack(ib, &ibd) && readBack(vb, &vbd)) {
        UINT isz = ifmt == DXGI_FORMAT_R32_UINT ? 4 : 2;
        for (UINT i = 0; i < indexCount && i < 6; ++i) {
            size_t ip = ioff + (size_t)(startIndex + i) * isz;
            if (ip + isz > ibd.size()) break;
            UINT idx = isz == 4 ? *(const uint32_t*)&ibd[ip] : *(const uint16_t*)&ibd[ip];
            size_t vp = off + (size_t)((INT)idx + baseVertex) * stride;
            if (vp + stride > vbd.size()) break;
            const float* fl = (const float*)&vbd[vp];
            uvMax = std::max(uvMax, std::max(fl[7], fl[8]));  // texcoord0 at byte 28 (stride 36)
        }
    }
    if (vb) vb->Release();
    if (ib) ib->Release();
    if (uvMax < 0) return;
    bool wantStandIn = uvMax < 0.99f;
    long long frame = Counters().gameFrames;
    if (wantStandIn == standIn) {
        if (agreed++ < 12) Log("ui check frame %lld: image draw uv max %g, stand-in %d: agrees", frame, uvMax, standIn);
    } else if (disagreed++ < 100) {
        Log("ui check frame %lld: image draw uv max %g, stand-in %d: DISAGREES", frame, uvMax, standIn);
    }
}

void STDMETHODCALLTYPE WrappedContext::DrawIndexed(UINT IndexCount, UINT StartIndexLocation, INT BaseVertexLocation) {
    m_uiImageDraw = false;
    bool standIn = UiScaleConfigured() && UiBeforeDraw();
    if (m_uiImageDraw && g_cfg.diagnostics) UiCheckImageDraw(IndexCount, StartIndexLocation, BaseVertexLocation, standIn);
    TRACE_ACTION(TraceKind::Draw, "DrawIndexed(%u,%u,%d)", IndexCount, StartIndexLocation, BaseVertexLocation);
    m_real->DrawIndexed(IndexCount, StartIndexLocation, BaseVertexLocation);
    if (standIn) UiAfterDraw();
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
    if (!RestoreContextState) ResetTrackedState();  // D3D11 clears the immediate context's state
    TRACE_AFTER();
}

HRESULT STDMETHODCALLTYPE WrappedContext::FinishCommandList(BOOL RestoreDeferredContextState,
                                                           ID3D11CommandList** ppCommandList) {
    HRESULT hr = m_real->FinishCommandList(RestoreDeferredContextState, ppCommandList);
    if (!RestoreDeferredContextState) ResetTrackedState();  // and the deferred context's
    if (SUCCEEDED(hr) && ppCommandList && *ppCommandList) TraceFinishCommandList(this, *ppCommandList);
    return hr;
}

}  // namespace goh4k
