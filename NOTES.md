# Goh4kMod: how it works and how to fix it after a game update

These notes are for whoever maintains the mod, including a future Claude session. PLAN.md is the plan
and LOG.md is the day-by-day journal with evidence. This file is the condensed knowledge: what the mod
does, why, where things are, and a playbook for when a game update breaks it.

## What it does, in one paragraph

The game is set to 1920x1080, the "logical" size. Our `d3d11.dll` and `dxgi.dll` sit in the game's
`binaries\x64` and wrap the game's D3D11 device, contexts and swapchain. The swapchain and display mode
are really 3840x2160. Every render, depth or UAV target the game creates at the logical screen size, or
at 1/2..1/8 of it, is created at 2x. Viewports, scissors and copy boxes are doubled whenever such targets
are bound. The UI is drawn in 1080p coordinates into the 2x backbuffer, so it keeps its size while the
3D gets 4x the pixels. 15 user32 functions are hooked with MinHook so that mouse and window coordinates
the game sees stay in 1080p units. Only calls made by `call_to_arms.exe` itself are converted, never
other modules. The game's exe and memory are never modified.

## History: what was learned (most important first)

1. **The game makes its real swapchain through the DXGI factory, not D3D11CreateDeviceAndSwapChain.** The
   swapchain from D3D11CreateDeviceAndSwapChain (1360x768) is dropped without ever presenting. The real
   one comes from `IDXGIFactory::CreateSwapChain`, which is why we need a `dxgi.dll` proxy as well
   (src/dxgi). It wraps factories the exe creates and hands CreateSwapChain to the d3d11 proxy through its
   `Goh4k_CreateSwapChain` export.
2. **The game sizes some targets from texture GetDesc but sets viewports in logical units.** The
   backbuffer is therefore handed out as a wrapper whose GetDesc reports 1920x1080 (texture.cpp). Every
   `ID3D11Resource*` argument is unwrapped before it reaches D3D. The generated forwarders do this via
   `UnwrapResource`; hand-written overrides must call it too.
3. **Targets created at exactly the real screen size** (sized from our scaled depth texture's real desc,
   e.g. the linear-depth target used by DOF) are tagged as 2x. Without that the whole frame was blurred.
4. **SSAO is NVIDIA HBAO+** (GFSDK_SSAO_D3D11.win64.dll 3.1). It sizes everything from the real depth
   buffer, so it is left alone: calls whose return address is inside that DLL get no texture scaling and
   no viewport normalisation (hbao.cpp). Nothing in HBAO+ is hooked.
5. **Screen-size constants in pixel shaders.** At creation every pixel shader is reflected with D3DReflect,
   and variables matched by name are patched per draw on a copy of the bound constant buffer
   (cbpatch.cpp):
   - `src_size` and `textureDim`: multiplied by 2 when the source texture is scaled.
   - `one_pixel`: divided by 2 when the source is scaled.
   - `fxaaNormPixelWidthHeight`: divided by 2 when the target is scaled.
   - `sunMoonPosRes.zw`: multiplied by 2 when the target is scaled.

   This fixed the glow downsample (copy.hlsl `Load(uv*src_size)` on the 8x-MSAA scene). The names come
   from the game's own HLSL in `resource/shader.pak`, a zip of HLSL post-process sources plus a
   precompiled cache that keeps its reflection data.
6. **Soft particles, water and HBAO+ divide SV_Position by `GetDimensions()`** of the depth texture, so
   they stay correct as long as that texture is scaled. Nothing was needed for them.
7. **Viewport rules** (context.cpp): viewports and scissors are stored logical and re-applied on every
   target change. A viewport covering exactly the real screen is normalised to logical. A viewport
   larger than the bound target's logical size is left unscaled (the game's stale full-screen viewport
   on small UI label targets).
8. **2x is dynamic** (scale.h `g_scaleActive`).
   - If the game asks for a resolution whose 2x doesn't fit the monitor (it is still set to 4K), the
     mod steps aside completely: rendering, mouse and cursor all run natively.
   - Switching between 1080p and 4K in the game's Options keeps the real display mode at 3840x2160, so
     Windows sends no WM_SIZE. The mod posts its own resync message, and the subclassed window procedure
     turns it into WM_SIZE with the client size **at delivery time**. A size read when posting was stale
     during startup, laid the UI out for 1382x824, and broke the mouse.
9. **Cursor:** the game builds its cursors with CreateIconIndirect at 32x32. The hook builds them at 2x
   (nearest neighbour, doubled hotspot).
10. **The anti-cheat** (Digitalmindsoft's own, scans process and module names) never reacted in about 20
    single-player sessions.

## Where things are

| Area | Files |
|---|---|
| Export forwarding (ml64 jump stubs) | `src/scripts/gen-exports.ps1` -> `src/proxy/gen/*`, `src/dxgi/gen/*` |
| COM forwarders (generated from SDK headers) | `src/scripts/gen-wrappers.ps1` -> `src/*/gen/fwd_*.h` |
| Device/context/swapchain wrappers | `src/proxy/device.cpp`, `context.cpp`, `swapchain.cpp`, `wrappers.h` |
| 2x rules, dynamic state | `src/proxy/scale.*` |
| Backbuffer wrapper / unwrap | `src/proxy/texture.*`, `unwrap.h` |
| Mouse/window/cursor hooks | `src/proxy/input.*` (MinHook in `src/third_party/minhook`) |
| HBAO+ exemption | `src/proxy/hbao.*` |
| Constant-buffer patches | `src/proxy/cbpatch.*` (rule table `kRules`) |
| Diagnostics (capture/trace/dumprt) | `src/proxy/diag.*` |
| Config, logging, dev vs release paths | `src/proxy/util.cpp`, `common.h` |
| dxgi proxy (factory wrapper) | `src/dxgi/main.cpp` |
| Release package text | `src/package/` (README, goh4k.ini, notices); `src/scripts/package.ps1` |
| Tools | `build/drive/drive.exe` (input + desktop snaps), `build/drive/imgtool.exe` (crop/side/diff) |

## Build, test, deploy

- Run `start.cmd` for a session with the MSVC environment. `build.cmd` sets that up itself either way.
- `build.cmd`: dev build (paths to this project compiled in; config in `config/goh4k.ini`; logs in
  `scratch/logs/`; diagnostics in `captures/`).
- `build.cmd release`: release DLLs (`/DGOH4K_RELEASE`: settings and `goh4k.log` next to the DLL, Scale=2
  default, diagnostics off, no local paths embedded), packaged as `build/release/Goh4kMod-1.0.zip`.
  `package.ps1` refuses to package a DLL that contains the user name or project path.
- Tests (run with the game closed):
  - `build\proxytest.exe`: wrapper smoke test plus 2x checks, which run when the dev config says Scale=2.
  - `powershell -File src\scripts\test-gamefolder.ps1`: deploy/uninstall against a fake game folder.
- `deploy.cmd` installs the dev build; `deploy.cmd -Release` installs the zip contents.
  `uninstall.cmd /nopause` removes everything and checks the folder against
  `scratch/baseline-binaries-x64.txt`. After a game update, retake that baseline (Phase 1 step) or the
  check will report the updated game files.
- To bump the version, change it in `src/*/version.rc`, `$Version` in `package.ps1`, and the README title.

## Playbook: the game updated and something broke

1. **Does it still load?** Read `goh4k.log` next to the DLLs (release build) or `scratch/logs/` (dev
   build):
   - "swapchain wrapped ... 2x" and "logical screen 1920x1080, real 3840x2160" mean the core works.
   - If no swapchain is wrapped, the game changed how it creates it. Look for
     `IDXGIFactory::CreateSwapChain` / `CreateSwapChainForHwnd` lines, then check `dumpbin /imports` of
     the exe (d3d11/dxgi functions). A move to D3D12 would need a new proxy.
2. **Crash at start:** `dumpbin /exports C:\Windows\System32\d3d11.dll` (and dxgi). If Windows added
   exports, rerun `gen-exports.ps1 -Dll d3d11` / `-Dll dxgi` and rebuild. If the SDK changed, rerun
   `gen-wrappers.ps1`.
3. **Mouse wrong:** compare `dumpbin /imports call_to_arms.exe | findstr /i user32` with the 15 hooks in
   `input.cpp`. A newly used function such as `GetCursorInfo` or `PeekMessageA` needs the same treatment.
4. **A visual glitch:** use the dev build with `Diagnostics=1`.
   - Get a reference: set `Scale=1` and the game at 1920x1080, `capture ref` + `trace ref`. Then
     `Scale=2`, same scene, `capture x` + `trace x`. Commands go in `captures/request.txt`.
   - Hand both traces to the capture-analyst agent and ask which passes are not exactly native x2:
     target sizes, viewport/target ratio, SRV sizes, dispatch counts.
   - Find the broken pass's pixel shader id, then `dumprt ps:<id> tag cb`. That saves its output and
     prints its constant buffers into the trace. Look for 1920/1080, 960/540 or 1/1920-style values.
   - Read the shader source: `resource/shader.pak` is a zip; extract read-only to `scratch/`, never commit.
   - Pick the cheapest fix:
     - a new name in `cbpatch.cpp kRules`;
     - a rule tweak in `scale.cpp` (`WantScaled`, `IsRealScreenTarget`);
     - an exemption by caller module (as with HBAO+);
     - a viewport rule in `context.cpp`.
   - Prove it with a runtime A/B (`fix nocbpatch on|off`, or a new FixOn toggle) on the same frame.
5. **Another DLL starts drawing through our device** (new NVIDIA library, overlay): check its return
   addresses the way `hbao.cpp` does.
6. **Driving the game for tests:**
   - `build\drive\drive.exe` (click, move, drag, key, wheel, focus, fg, cursor, snap) and `imgtool.exe`.
   - Click coordinates in 2x mode are the 1080p UI position x2.
   - Paths I used (2x mode): SINGLEPLAYER 1280,1304 > CAMPAIGN 1072,388 > SELECT CAMPAIGN 1072,580 >
     USSR 2548,896 > ZA RODINU 1072,388 > ABSOLUTE ZERO 1068,344 (Easy is the default). Skip the intros
     with Esc/space; CONTINUE 3680,76. Leave a mission: Esc > EXIT 1916,1316 > YES 2056,1194.
     Top bar: OPTIONS 2556,48, EXIT 2876,48, YES 2056,1194.
   - Back up the profile's saves first (autosave is on), and use new campaigns or skirmishes only.

## Known limits

- Tested on one 4K monitor (RTX 5090, Windows 11, HDR desktop). 1440p with 1280x720 should work but is
  untested.
- Water (WaveWorks) was never seen on screen in tests. The code path matches soft particles, which work.
- The sun disc patch (`sunMoonPosRes`) is untested visually.
- 1-pixel UI lines draw thinner than at native 1080p. UI text uses the 1080p glyph textures magnified 2x.
- After changing resolution in Options, the cursor keeps its old size until the game creates a new one
  (restart once).
- It conflicts with other mods installed as d3d11.dll or dxgi.dll (ReShade, DXVK).
- Multiplayer is untested by design. Users must uninstall before going online.
