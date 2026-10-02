# Goh4kMod plan

## Context
Gates of Hell: Ostfront (GEM2, D3D11, x64) at native 4K has a tiny UI; at 1080p the 3D is soft. Goal: 3D
at 3840x2160 with the UI at its 1080p size. Decided approach (CLAUDE.md): our own small proxy DLL in
binaries\x64. The game is set to 1920x1080 and never learns otherwise. The proxy creates screen-sized
targets at 2x, scales viewports and scissors to match, and maps the mouse back to 1080p coordinates. The
UI keeps drawing in 1080p coordinates into a 2x target, so it keeps its size and only the 3D gains pixels.
WineHooks HD mode has shown the trick works in this game.

## Facts already established (read-only, during planning)
- Install: `C:\Program Files (x86)\Steam\steamapps\common\Call to Arms - Gates of Hell\binaries\x64`.
  The exe is call_to_arms.exe, build 1.062.0. Neither d3d11.dll nor dxgi.dll is in the folder today.
  steam_appid.txt is present, so the exe can be started directly while Steam is running.
- What call_to_arms.exe imports:
  - D3D/DXGI: `D3D11CreateDeviceAndSwapChain` (d3d11), `CreateDXGIFactory` (dxgi), and
    `D3DCompile` / `D3DReflect` (the bundled d3dcompiler_47.dll), so it can compile shaders at runtime.
  - Mouse and window (user32): GetCursorPos, SetCursorPos, ClipCursor, ScreenToClient, ClientToScreen,
    MapWindowPoints, GetClientRect, GetWindowRect, GetSystemMetrics, GetMonitorInfoW,
    EnumDisplaySettingsW, SetWindowPos, GetMessageW, RegisterRawInputDevices/GetRawInputData, SetCursor,
    CreateIconIndirect, ShowCursor.
  - No DirectInput.
- Bundled NVIDIA libraries: GFSDK_SSAO_D3D11.win64.dll is HBAO+, the game's "SSAO". WaveWorks.win64.dll
  does the water. Both draw through the device the game hands them.
- Settings: `Documents\My Games\gates of hell\profiles\209810737\options.set`. Current values:
  `mode "3840x2160"`, `window_mode fullscreen`, `antialiasing "8x"`, fxaa on, `_ssao high`.
- DPI: Windows is at 150% (AppliedDPI 144). The exe has no dpiAware manifest entry and no compat flag,
  so it looks DPI-unaware. M1 will measure what the game actually sees.
- Anti-cheat: Digitalmindsoft's own. The exe has an eCheatDetectionTask and enumerates processes and
  modules by name (K32EnumProcessModulesEx, QueryFullProcessImageNameW). It is active in single-player.
  BugSplat (BsSndRpt64.exe) handles crash reports.
- Phase 1 additions:
  - The exe's load config has Dependent Load Flag 0, and neither d3d11 nor dxgi is a KnownDLL, so a
    d3d11.dll in binaries\x64 gets loaded.
  - HBAO+ and WaveWorks import only KERNEL32. System d3d11.dll has 51 exports to forward (list in
    scratch/phase1/).
  - options.set also has `{hdr}` (an HDR swapchain, so captures must handle 10-bit/FP16), vsync,
    softIntersection, dof and lens_refraction. There is no UI-scale option; `gui.scale_index` is a
    computed UI-script variable stored next to window.size.x/y.
  - resource/shader.pak is a zip:
    - ~5.7k precompiled shaders (shader/cache/dx11.0/<md5>.pso/.vso: a defines header plus DXBC with
      RDEF reflection);
    - ~34 HLSL post-process sources the game compiles itself (postprocess/ssao/*, dof, hdr…);
    - soft_intersection.inc.
  - soft_intersection.inc and SSAO.hlsl turn SV_Position into UV by dividing by
    `depthTexture.GetDimensions()`, not by a constant. Size-matched 2x scaling should therefore stay
    consistent for them, as long as the depth texture they read is scaled too.
- Reference lessons:
  - Steam thread: fullscreen "just works", while borderless renders at desktop size. SSAO shows a dark
    overlay. Soft particles produce artifacts. Conquest unit cards and the tech tree glitch. With the
    cursor unscaled it looks half-size. Water broke only under wined3d.
  - WineHooks PR #7 (mouse): pin the factor at 2 so alt-tab and ResizeBuffers can't reset it. Divide on
    reads (GetCursorPos, MSG.pt and lParam); multiply on writes (SetCursorPos, ClipCursor). Clamp to
    0..1919 / 0..1079. Report the logical size from GetClientRect, GetWindowRect, GetSystemMetrics,
    GetMonitorInfo and EnumDisplaySettings. Scale the cursor in SetCursor with a nearest-neighbour 2x
    copy and a doubled hotspot. GoH makes a new HCURSOR on every change, so keep a 64-entry ring and free
    copies late. Raw-input deltas pass through.
  - WineHooks HD core: it scales every render target and swaps in pixel shaders patched so SV_Position is
    divided by the scale and ld/resinfo are rescaled. It has no size matching and no constant-buffer
    fixes.

## Design decisions
- Proxy: **d3d11.dll** in binaries\x64. One DLL gives us the device, context and swapchain, which all
  come from D3D11CreateDeviceAndSwapChain. Every other export is forwarded to System32\d3d11.dll through
  ml64 jump stubs generated from `dumpbin /exports`. A dxgi.dll proxy is added only if M1's log shows the
  swapchain being created through CreateDXGIFactory.
- Interception: COM wrappers for Device(→5), Context(→4, deferred contexts too) and SwapChain(→4). There
  are no vtable patches. The per-resource scale is stored with SetPrivateData on the real texture.
- Config and output stay in the project. The DLL has the project path compiled in. It reads
  config/goh4k.ini when it starts, writes logs to scratch/logs/ and writes captures to captures/. Only
  d3d11.dll goes into the game folder. `Scale=1` makes the proxy a pure pass-through with no hooks.
- Mouse and window: MinHook (BSD-2), vendored as source in src/third_party/minhook and compiled in, hooks
  the user32 list above. Hooks go in when the DLL loads, before the game creates its window. They only
  convert calls whose return address is inside call_to_arms.exe, so the Steam overlay keeps physical
  coordinates. The game's exe and memory are never touched.
- 2x rules (start here, then adjust from the evidence):
  - Scale a Texture2D 2x when it has no initial data, has an RT, DS or UAV bind flag (or is a DEFAULT
    copy target), and is screen-sized or a screen fraction (1/2, 1/4… with either rounding).
  - Square, array and initial-data textures stay as they are.
  - Viewports and scissors are stored in logical units and scaled at draw time when a scaled target is
    bound. A guard stops double scaling.
  - Copy, update and ClearView boxes are scaled too.
  - The swapchain is really 3840x2160 (the fullscreen mode included), while GetDesc reports 1920x1080.
- Fix toolbox for broken passes, cheapest first. Every fix can be toggled at runtime, so before and after
  come from the same frame:
  (a) keep a given target at 1x;
  (b) patch a constant-buffer variable for one shader hash, found by D3DReflect name;
  (c) replace a shader by hash with a .cso built from src/shaders;
  (d) only if many passes need it, a generic SV_Position patch, ideally at the HLSL level by hooking
      D3DCompile (ask first).
  For HBAO+, a likely fix is scaling the viewport the game passes to RenderAO (ask first).

## Phase 0 (after approval)
1. Save this plan as PLAN.md. Create LOG.md with Status, Journal, Gotchas, Game-folder changes and
   Cleanup sections.
2. Git setup:
   - Set a repo-local identity, `Ethan <ethan@localhost>`.
   - Add `*.dmp *.png *.log` to .gitignore.
   - Make the first commit on master: scaffolding, PLAN.md, LOG.md.
3. Toolchain: confirm `cl` reports x64 (dumpbin already resolves to Hostx64\x64). Check that ml64, rc,
   fxc and windowscodecs.lib are present. Add build.cmd, which runs vcvars64 itself.

## Phase 1 (read-only outside this folder)
1. Record the Steam library lookup (libraryfolders.vdf, appmanifest_400750.acf). Write
   `C:/Program Files (x86)/Steam/steamapps/common/Call to Arms - Gates of Hell/binaries/x64` to
   .claude/game-bin.txt.
2. dumpbin checks:
   - `/headers` on the exe to confirm the machine type is x64.
   - `/imports` on the exe and on the HBAO+, WaveWorks and bdcap64 DLLs.
   - `/exports` on System32 d3d11.dll, saved to scratch/.
3. Confirm the d3d11.dll choice.
4. Baseline: list the file names and SHA256 hashes in binaries\x64 to scratch/baseline-binaries-x64.txt.
   The clean-uninstall check later compares against it.
5. Back up options.set to scratch/settings-backup/. Check options.set and the exe strings for a built-in
   UI-scale option; if one exists, that changes everything, so report it first.
6. Look for shader sources or caches (read-only listing) to see what D3DCompile is compiling.
7. Write a short report, record it in LOG.md, and stop.

## Milestones (each ends with evidence in LOG.md)
**M1. Pass-through proxy plus deploy.cmd and uninstall.cmd.**
- The DLL forwards everything and logs device, swapchain and window facts: requested and real sizes,
  fullscreen state, DPI awareness, GetClientRect, Present count and PID. It also writes captures/status.txt.
- deploy.cmd refuses to run while the game is running. It backs up any stock file with the same name to
  scratch/, copies the files, records names and hashes in scratch/deployed.txt, appends to LOG.md, and
  prints the multiplayer warning.
- uninstall.cmd removes only files whose hashes match, restores any backups, compares the folder against
  the baseline, and appends to LOG.md.
- Done when:
  - the game reaches the main menu and a single-player mission with the proxy loaded;
  - the log shows our swapchain and a rising Present count;
  - the anti-cheat doesn't react;
  - after uninstall, the folder matches the baseline exactly.

**M2. Built-in capture and diagnostics.**
- Two triggers: a hotkey, or captures/request.txt (polled about 4 times a second). It accepts these
  commands: `capture <tag>`, `trace <tag>`, `dumprt <event> <tag>`, `stats`, `fix <name> on|off`.
- Capture: the real backbuffer (resolved if MSAA) goes through a staging copy and WIC to a PNG, plus a
  half-size preview and a .txt sidecar.
- Trace: one frame's draw list (targets and their scale, viewport, shader hashes, SRV sizes).
- dumprt: saves any intermediate target as a PNG.
- Also: build/drive.exe (SendInput click/move/key/wait, plus a `snap` command using desktop
  duplication, used for navigation and for stock "before" shots) and a crop/compare tool.
- Done when:
  - a request-file capture of the main menu has correct colours and size;
  - the hotkey works;
  - the trace lists the menu's passes.

**M3. 2x on the main menu.**
- Switch the game to 1920x1080 fullscreen through its own options menu (I drive).
- Turn on the 2x rules, the swapchain changes and the user32 hooks. Fix DPI awareness if M1 shows the
  game seeing virtualized coordinates.
- Done when:
  - the 2x capture is 3840x2160;
  - downscaled to 1080p, it lines up with the 1x capture (compare-tool score under a threshold, UI edges
    within 1px);
  - crops show sharper 3D and text;
  - drive.exe hovers and clicks buttons in all four quadrants and corners and the right ones react;
  - the cursor reaches every screen edge;
  - right-click and the wheel work.
- Stop rule: if M3 isn't met after two sessions, stop and lay out options (WineHooks-style uniform
  scaling plus a shader patch, compositing the UI separately, a 1.5x/1440p middle ground) instead of
  pushing on.

**M4. Mission passes, one at a time.**
- Order: SSAO (HBAO+), soft particles, Conquest unit cards, tech tree. Then a sweep: water/WaveWorks,
  shadows, bloom/DOF, minimap, selection markers, and AA performance at 8x.
- For each pass: trace it, find the cause (capture-analyst digests traces, dumps and any RenderDoc
  capture), apply the cheapest toolbox fix, then toggle the fix off and on in the same frame for the
  before/after captures.
- Check for regressions with a 1x reference shot and the M3 menu capture.
- RenderDoc (portable, in tools/) only if the trace isn't enough, and only after asking: it puts a module
  into the game, and the anti-cheat scans modules.

**M5. Polish and clean install.**
- Alt-tab out and back keeps the factor at 2 and the mouse right, in fullscreen and in borderless.
- Cursor shown at 2x (SetCursor copy ring).
- Check the Steam overlay (Shift+Tab).
- A clean deploy, a full play check, then uninstall back to a byte-identical baseline. Short usage notes
  in PLAN.md.

## Steps that need Ethan
- Approve this plan, and every deploy.cmd / uninstall.cmd run (I ask in chat; the guard asks too).
- At the start of each test session I ask "OK to take the mouse?". After that I launch the exe, navigate,
  change the in-game resolution, play, and quit by exact PID. Hands off while I drive.
- I never load or save over your saves: I use new skirmishes and a new Conquest campaign. At the end I
  set the resolution back to 3840x2160 or leave it at 1080p for the mod, whichever you choose.
- Ask before: RenderDoc in the game, any new hook target (D3DCompile, HBAO+), replacing a stock DLL, and
  publishing anything (git push, artifacts).
- If the anti-cheat or a BugSplat dialog appears, I stop, decline sending any report, and tell you.
- You: run uninstall.cmd before playing multiplayer.

## Keeping token use down
- Facts and lessons from the references live in PLAN.md, so they are never fetched again. The SessionStart
  briefing plus LOG.md's tail restart each session.
- Builds print errors only. Any output over about 100 lines, and every trace, shader dump and RenderDoc
  capture, goes to capture-analyst (sonnet), which returns 25 lines or fewer.
- The proxy writes a status.txt and a compact stats summary, so I grep those instead of reading logs.
- For navigation I look at small previews (960x540); evidence comes from crops, never whole 4K PNGs.
- A/B captures come from runtime fix toggles, which saves game restarts and re-navigation.
- One fix per step. Short replies. LOG.md gets summarized as it grows.

## Verification (overall)
- Every milestone's done-check is run in the real game, in single-player, with captures saved in
  captures/ and referenced from LOG.md.
- The final check: deploy, play a mission at 2x with every fix on, uninstall, and confirm the folder
  matches the Phase 1 baseline.
