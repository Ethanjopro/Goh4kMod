# Goh4kMod: how it works and how to fix it after a game update

These notes are for whoever maintains the mod, including a future Claude session. PLAN.md is the plan
and LOG.md is the day-by-day journal with evidence. This file is the condensed knowledge: what the mod
does, why, where things are, and a playbook for when a game update breaks it.

## What it does, in one paragraph (2.0)

The player keeps the game at the monitor's own resolution. Our `d3d11.dll` and `dxgi.dll` sit in the
game's `binaries\x64` and wrap the game's D3D11 device, contexts and swapchain, but rendering stays
native: no texture, swapchain or display mode is changed. Ten user32 functions are hooked with MinHook so
the game sees its client area, and the mouse, divided by k = real client height / 1080 (2 at 4K, 4/3 at
1440p). Its UI therefore lays out at its 1080p size and is drawn magnified onto the native screen. Three
full-screen viewport rules fix what the mismatch between client size and render size breaks (point 2).
Window position, monitor and display-mode calls are not hooked, so Options, the mode list and
options.set never see a change. Only calls made by `call_to_arms.exe` itself are converted. At 1080p and
below the mod changes nothing. The game's exe and memory are never modified.

## History: what was learned (most important first)

1. **The game takes its sizes from three places.** Render targets follow its resolution setting (the
   saved `mode`); its UI layout and some post-process viewports follow the client size it is told
   (WM_SIZE, GetClientRect); its "screen" viewport comes from the backbuffer desc. Native scaling works
   because only the second is changed.
2. **The three full-screen rules** (context.cpp; `fix nouivp on` disables them, for A/B):
   1. UI label surfaces (render targets smaller than the screen) are drawn with the stale real-screen
      viewport: applied as the logical screen, as at native 1080p. Otherwise text comes out 2x and
      clipped.
   2. Tone-map and glow passes set the logical-screen viewport on screen-sized targets: applied as the real
      screen. Otherwise the 3D fills only the top-left part. Stock native 4K never does this.
   3. The blurred backdrop behind prompts is a screen-sized texture the UI draws as an image, with
      coordinates of logical / texture size. A UI draw (recognised by its vertex shader, which is learnt
      from rule 1's draws) that samples one marks it; from then on the game's full-screen pass into it
      fills only the logical part. Otherwise only its top-left quarter shows, magnified. The UI vertex
      shader is matched by bytecode id: the game creates it more than once.
3. **Coordinate math is rational** (ratio.h, checked exhaustively by proxytest): Down = floor, Up = ceil,
   so Down(Up(v)) == v. ClipCursor uses Up on every edge.
4. **The factor follows the real client size, never the buffer size.** After a mode change the buffers
   stay stale until the game has seen a WM_SIZE that matches its new mode; following the buffer gave a
   black screen.
5. **HBAO+** (GFSDK_SSAO_D3D11.win64.dll 3.1, the game's SSAO) sizes everything from the depth buffer and
   sets its own viewports. Its calls are recognised by return address and the rules never touch them.
6. **Cursor:** the game builds its cursors with CreateIconIndirect at 32x32. The hook builds them at k:
   nearest neighbour for whole factors, an area filter for fractional ones (43x43 at 1440p).
7. **The anti-cheat** (Digitalmindsoft's own, scans process and module names) never reacted in more than 30
   single-player sessions.
8. **1.x (dropped in 2.0):** the game was set to 1920x1080 and the proxy rendered every screen-sized
   target at 2x, with a backbuffer wrapper, viewport/scissor/copy-box scaling and constant-buffer
   patches. It needed a 4K monitor and a manual resolution step. Ethan chose native scaling only for
   2.0, to keep the mod as unintrusive as possible. The 1.0 source is commit e338dd9; the last state with
   both paths is in scratch/src-hybrid-2026-10-05 (not in git).
9. **Rejected: lying about the display mode list** (2026-10-04). It removed native 4K from Options and
   rewrote the saved resolution. Ethan's rule: the mod must not take away or change any game option.
   Facts from it: the Options list comes from DXGI (EnumAdapters/EnumOutputs/GetDisplayModeList), the game
   hides modes under about 1360 wide, and a saved mode missing from the list makes it rewrite
   `{mode desktop}`.
10. **No built-in hi-DPI text** (research 2026-10-05): fonts are fixed-size BMFont pages picked by name and
    size; `gui.scale_index` is an options key the fonts never use. Sharper text would need 2x text
    surfaces plus font pages rebuilt at runtime (LOG.md).

## Where things are

| Area | Files |
|---|---|
| Export forwarding (ml64 jump stubs) | `src/scripts/gen-exports.ps1` -> `src/proxy/gen/*`, `src/dxgi/gen/*` |
| COM forwarders (generated from SDK headers) | `src/scripts/gen-wrappers.ps1` -> `src/*/gen/fwd_*.h` |
| Device/context/swapchain wrappers | `src/proxy/device.cpp`, `context.cpp`, `swapchain.cpp`, `wrappers.h` |
| Scaling factor and state | `src/proxy/scale.*`, `src/proxy/ratio.h` |
| Full-screen viewport rules | `src/proxy/context.cpp` (UiTargetsChanged, UiApplyViewports, UiBeforeDraw) |
| Mouse/window/cursor hooks | `src/proxy/input.*` (MinHook in `src/third_party/minhook`) |
| HBAO+ exemption | `src/proxy/hbao.*` |
| Diagnostics (capture/trace/dumprt/stats) | `src/proxy/diag.*` |
| Config, logging, dev vs release paths | `src/proxy/util.cpp`, `common.h` |
| dxgi proxy (factory wrapper) | `src/dxgi/main.cpp` |
| Release package text | `src/package/` (README, user uninstaller, notices, Nexus page text); `src/scripts/package.ps1` |
| Tools | `build/drive/drive.exe` (input + desktop snaps), `build/drive/imgtool.exe` (crop/side/diff) |

## Build, test, deploy

- Run `start.cmd` for a session with the MSVC environment. `build.cmd` sets that up itself either way.
- `build.cmd`: dev build (paths to this project compiled in; config in `config/goh4k.ini`; logs in
  `scratch/logs/`; diagnostics in `captures/`).
- `build.cmd release`: release DLLs (`/DGOH4K_RELEASE`: settings and `goh4k.log` next to the DLL,
  diagnostics off, no local paths embedded), packaged as `build/release/Goh4kMod-2.0.zip`. `package.ps1`
  refuses to package a DLL that contains the user name or project path.
- Tests (run with the game closed):
  - `build\proxytest.exe`: wrapper smoke test, diagnostics round trip, ratio math, and native-scaling
    checks with a 2560x1440 client.
  - `powershell -File src\scripts\test-gamefolder.ps1`: deploy/uninstall against a fake game folder.
- `deploy.cmd` installs the dev build; `deploy.cmd -Release` installs the newest packaged release.
  `uninstall.cmd /nopause` removes everything and checks the folder against
  `scratch/baseline-binaries-x64.txt`. After a game update, retake that baseline (Phase 1 step) or the
  check will report the updated game files.
- To bump the version, change it in `src/*/version.rc`, `$Version` in `package.ps1`, the log line in
  `util.cpp`, and the README title.

## Playbook: the game updated and something broke

1. **Does it still load?** Read `goh4k.log` next to the DLLs (release build) or `scratch/logs/` (dev
   build):
   - "input hooks: 10 of 10", "ui scale 2/1 (WM_SIZE, real client 3840x2160)" and the three notes
     `ui-label-viewport`, `ui-screen-viewport`, `ui-copy-viewport` mean the core works.
   - If no swapchain is wrapped, the game changed how it creates it. Look for
     `IDXGIFactory::CreateSwapChain` / `CreateSwapChainForHwnd` lines, then check `dumpbin /imports` of
     the exe (d3d11/dxgi functions). A move to D3D12 would need a new proxy.
2. **Crash at start:** `dumpbin /exports C:\Windows\System32\d3d11.dll` (and dxgi). If Windows added
   exports, rerun `gen-exports.ps1 -Dll d3d11` / `-Dll dxgi` and rebuild. If the SDK changed, rerun
   `gen-wrappers.ps1`.
3. **Mouse wrong:** compare `dumpbin /imports call_to_arms.exe | findstr /i user32` with the hooks in
   `input.cpp`. A newly used function such as `GetCursorInfo` or `PeekMessageA` needs the same treatment.
4. **A visual glitch:** use the dev build (`Diagnostics=1`).
   - Get a reference with `UiScale=0` (stock native), then `UiScale=1`, same scene: `capture x` +
     `trace x` in `captures/request.txt`. `fix nouivp on|off` toggles the viewport rules live.
   - Hand both traces to the capture-analyst agent: which draws have a viewport that doesn't match their
     target, and which screen-sized textures the UI samples.
   - `dumprt <event>|ps:<id> [tag] [cb]` saves an event's output and prints its constant buffers.
   - Read the shader source: `resource/shader.pak` is a zip; extract read-only to `scratch/`, never commit.
   - Most fixes are a new full-screen rule or a tweak to the target classes in `context.cpp`.
5. **Another DLL starts drawing through our device** (new NVIDIA library, overlay): check its return
   addresses the way `hbao.cpp` does.
6. **Driving the game for tests:**
   - `build\drive\drive.exe` (click, move, drag, key, wheel, focus, fg, cursor, snap) and `imgtool.exe`.
   - Click coordinates are the 1080p UI position x k (x2 at 4K, x4/3 at 1440p).
   - Paths at 4K: SINGLEPLAYER 1280,1304 > CAMPAIGN 1072,388 > SELECT CAMPAIGN 1072,580 > USSR 2548,896 >
     ZA RODINU 1072,388 > ABSOLUTE ZERO 1068,344 (Easy is the default). Skip the intros with Esc/space;
     CONTINUE 3680,76. Leave a mission: Esc > EXIT 1916,1316 > YES 2056,1194. Top bar: OPTIONS 2556,48,
     EXIT 2876,48, YES 2056,1194. Main menu row: OPTIONS 2556,1304, EXIT 2876,1304.
   - In Options the Resolution row needs one click to select and one on its arrow to open the list.
   - Back up the profile's saves first (autosave is on), and use new campaigns or skirmishes only.

## Known limits

- Tested on one 4K monitor (RTX 5090, Windows 11, HDR desktop), at 3840x2160 and in the game's own
  2560x1440 mode. Not on a real 1440p, 5K, 16:10 or ultrawide monitor.
- Options' read-only "Current mode" line shows the logical size (1920x1080 at 4K).
- UI text and icons are the 1080p art magnified, as in 1.0. 1-pixel UI lines draw one real pixel wide.
- After changing resolution in Options, the cursor keeps its old size until the game creates a new one
  (restart once).
- A single left-click on the minimap doesn't move the camera, same as the stock game.
- It conflicts with other mods installed as d3d11.dll or dxgi.dll (ReShade, DXVK).
- Multiplayer is untested by design. Users must uninstall before going online.
