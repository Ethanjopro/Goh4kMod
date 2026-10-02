# Goh4kMod log

## Status
M1-M3 done (2026-10-02). Next: M4 (mission passes). Game set to 1920x1080 fullscreen (was 3840x2160), Scale=2.
Working autonomously under /goal. Proxy (d3d11.dll + dxgi.dll) is DEPLOYED during development.

## Gotchas (symptom -> cause -> fix)
- vcvars64.bat prints "'vswhere.exe' is not recognized" -> it calls vswhere by name internally -> harmless;
  build.cmd sends vcvars' stderr to nul and still checks its exit code.
- Git Bash `sed -i 's/$/\r/'` silently leaves LF endings -> Git Bash sed strips CR -> write .cmd files with
  CRLF from PowerShell ([IO.File]::WriteAllText) or rely on .gitattributes eol=crlf.
- No status.txt and "0 frames presented through the wrapper" -> the game drops the swapchain from
  D3D11CreateDeviceAndSwapChain without presenting on it and makes its real one with
  IDXGIFactory::CreateSwapChain -> a dxgi.dll proxy wraps the exe's factory and hands CreateSwapChain to the
  d3d11 proxy (Goh4k_CreateSwapChain).
- The proxy log shows 0 bytes in `ls` while the game runs -> NTFS doesn't update the directory size of a
  file that is still open -> read the file itself.
- PowerShell 5.1: casting the empty output of a function to List[object] gives $null, and @() on a generic
  List throws "Argument types do not match" -> build lists with New-Object and pass .ToArray().
- 2x mode: the game sizes some targets from texture GetDesc (real) and sets viewports from its logical
  size. Symptoms: cut text labels, quarter-screen or black scene, blur everywhere -> backbuffer wrapper
  (logical GetDesc, unwrapped by the generated forwarders), real-size targets tagged as 2x, HBAO+ calls
  exempt by module address. Diagnose with dumprt ps:<shader id> per post stage.
- proxytest while the game runs -> the game consumes capturesequest.txt -> run tests with the game closed.
- drive.exe click path at native 4K UI (coordinates change once the game runs at 1080p logical):
  SINGLEPLAYER 1600,1192 > CAMPAIGN 1420,630 > SELECT CAMPAIGN 1420,728 > USSR 2240,980 > first campaign
  1420,632 > first mission 1420,608 > Easy 1322,1525 (starts loading). Esc/space skip the intro; space
  continues at "press any button"; CONTINUE 3752,38 ends the cutscene. To quit: Esc > Exit 1916,1196 >
  Yes 1988,1136 > top bar EXIT 2400,22 > Yes 1988,1136.

## Game-folder changes
- 2026-10-02 02:55 deploy: d3d11.dll sha256 7EE009219FBA (new file)
- 2026-10-02 03:05 uninstall: removed d3d11.dll
- 2026-10-02 03:05 uninstall check: folder matches the baseline (26 files)
- 2026-10-02 12:11 deploy: d3d11.dll sha256 FC2E9BE9CB9B (new file)
- 2026-10-02 12:11 deploy: dxgi.dll sha256 7B5835FE9F18 (new file)
- 2026-10-02 12:15 uninstall: removed d3d11.dll
- 2026-10-02 12:15 uninstall: removed dxgi.dll
- 2026-10-02 12:15 uninstall check: folder matches the baseline (26 files)
- 2026-10-02 12:26 deploy: d3d11.dll sha256 10B5A6EE919A (new file)
- 2026-10-02 12:26 deploy: dxgi.dll sha256 FBF0679A1C16 (new file)
- 2026-10-02 12:45 deploy: d3d11.dll sha256 36D988FF4FA7 (replaced our previous build)
- 2026-10-02 12:45 deploy: dxgi.dll sha256 F1074AFD8656 (replaced our previous build)
- 2026-10-02 12:48 deploy: d3d11.dll sha256 CDC2FC277A17 (replaced our previous build)
- 2026-10-02 12:48 deploy: dxgi.dll sha256 F1074AFD8656 (replaced our previous build)
- 2026-10-02 13:18 deploy: d3d11.dll sha256 2578D970D812 (replaced our previous build)
- 2026-10-02 13:18 deploy: dxgi.dll sha256 F1074AFD8656 (replaced our previous build)
- 2026-10-02 13:25 deploy: d3d11.dll sha256 E9CB204DDB61 (replaced our previous build)
- 2026-10-02 13:25 deploy: dxgi.dll sha256 F1074AFD8656 (replaced our previous build)
- 2026-10-02 13:31 deploy: d3d11.dll sha256 3D942AE62D77 (replaced our previous build)
- 2026-10-02 13:31 deploy: dxgi.dll sha256 F1074AFD8656 (replaced our previous build)
- 2026-10-02 13:35 deploy: d3d11.dll sha256 A253123E00B5 (replaced our previous build)
- 2026-10-02 13:35 deploy: dxgi.dll sha256 F1074AFD8656 (replaced our previous build)
- 2026-10-02 13:45 deploy: d3d11.dll sha256 7928404FF685 (replaced our previous build)
- 2026-10-02 13:45 deploy: dxgi.dll sha256 F1074AFD8656 (replaced our previous build)
- 2026-10-02 13:52 deploy: d3d11.dll sha256 B75D8D1BB7AE (replaced our previous build)
- 2026-10-02 13:52 deploy: dxgi.dll sha256 F1074AFD8656 (replaced our previous build)

## Cleanup
(none yet; anything a tool writes outside the project folder goes here)

## Journal (newest at the bottom)
### 2026-10-02 Planning + Phase 0
- Plan approved and saved as PLAN.md. References read once and summarized in PLAN.md (Steam thread,
  WineHooks PR #7 mouse code, WineHooks D3D11 HD mode).
- Ethan's choices: repo-local git identity "Ethan <ethan@localhost>"; fullscreen first; one 4K monitor;
  MinHook (vendored source) for the user32 hooks; Claude launches/navigates/plays, asking once per test
  session before taking the mouse.
- Toolchain: cl 19.44.35228 for x64 (Hostx64\x64), ml64, rc and fxc (SDK 10.0.26100.0), windowscodecs.lib
  present. `build.cmd check` builds a test exe and confirms "machine (x64)".

### 2026-10-02 Phase 1 (read-only)
- Steam: one library, C:\Program Files (x86)\Steam; appmanifest_400750.acf says installed, buildid 25492057,
  exe v1.062.0. .claude/game-bin.txt written.
- dumpbin: exe, HBAO+, WaveWorks and bdcap64 are all x64. The exe statically imports
  D3D11CreateDeviceAndSwapChain and CreateDXGIFactory. Dependent Load Flag 0, and d3d11/dxgi are not
  KnownDLLs, so a d3d11.dll proxy in binaries\x64 will load. **Proxy choice confirmed: d3d11.dll.**
  Import lists and the System32 d3d11 (51 exports) and dxgi (20) export lists are in scratch/phase1/.
- Baseline: 26 files with SHA256 in scratch/baseline-binaries-x64.txt. Caveat: announcements.set sits in
  binaries\x64 and the game may update it itself, so the uninstall check should report it, not fail on it.
- Settings: options.set backed up to scratch/settings-backup/options.set.2026-10-02. Current values:
  3840x2160 fullscreen, HDR on, vsync, 8x AA + FXAA, SSAO high, soft intersection, DOF, lens refraction.
  There is no UI-scale setting (gui.scale_index is a computed UI-script variable).
- Shaders: shader.pak is a zip of precompiled DXBC (with reflection data) plus about 34 HLSL post-process
  sources that the game compiles itself. The soft-particle and SSAO sources divide SV_Position by
  GetDimensions() of the depth texture, not by a constant, which is good news for size-matched 2x.
  shader_cache in My Games holds just one file.

### 2026-10-02 M1 build (not yet run in the game)
- d3d11.dll proxy: 51 exports with the System32 ordinals. Two are implemented (D3D11CreateDevice and
  D3D11CreateDeviceAndSwapChain); the rest are ml64 jump stubs that load System32\d3d11.dll on first use.
  Only the exe's own device gets wrapped; DLL callers such as the Steam overlay get the real objects.
- The forwarding wrappers are generated from the SDK headers by src/scripts/gen-wrappers.ps1:
  Device5 (66 methods), Context4 (146) and SwapChain4 (38). The proxy logs to scratch/logs/ and writes
  captures/status.txt about once a second. Settings live in config/goh4k.ini (Wrap, Scale, StatusMs).
- build\proxytest.exe passed 21/21 checks: stub slow and fast paths, QI routing, deferred context,
  Present, ResizeBuffers, and refcounts reaching 0.
- src/scripts/test-gamefolder.ps1 ran deploy/uninstall against scratch\fakegame and passed 11/11:
  backup and restore, game-managed files, stray files, and the LOG.md section.
- build\drive\drive.exe (input plus desktop-duplication snaps) sits in its own folder so it never loads our
  d3d11.dll. The desktop here is HDR (FP16 duplication), so previews clip highlights.
- Finding: a DPI-unaware process here sees a 2560x1440 desktop at 96 DPI (Windows is at 150%). The game
  is DPI-unaware too, so M1 will show what it reports at 4K fullscreen.

### 2026-10-02 M1 session 1 (d3d11.dll only, Wrap=1, 4K fullscreen as Ethan plays)
- Deployed, launched with steam://rungameid/400750, and drove main menu > USSR campaign "Absolute zero"
  (Easy) > about 3 min of gameplay > quit through the menus. The game ran 9.5 min and exited cleanly.
  **The anti-cheat didn't react.** Device and contexts were wrapped and all rendering went through them
  (4 deferred contexts) at 160-240 fps. Evidence: captures/nav/m1-*.png and
  scratch/logs/goh4k-20261002-025554-33752.log.
- The game's first swapchain is 1360x768, windowed, B8G8R8A8 (87), BLT discard, ALLOW_MODE_SWITCH. It is
  released and recreated through the factory (see Gotchas), so this run counted no Presents.
- DPI: the game process is DPI-unaware and sees a 2560x1440 monitor at 96 DPI. drive.exe clicks in
  physical 4K pixels land correctly in the 4K UI.
- uninstall.cmd: folder matches the baseline (26 files). Profile saves and settings are byte-identical to
  scratch/profile-backup-2026-10-02 (backed up before the mission because autosave is on).
- Added the dxgi.dll proxy (20 exports, factory wrapper) and the Goh4k_* hand-off exports in d3d11.dll.
  proxytest now replays the game's sequence and passes 24/24; the deploy self-test still passes.
  deploy.ps1 now ships both DLLs. Next: session 2 to confirm Presents go through the wrapper.

### 2026-10-02 M1 session 2: M1 DONE
- Deployed d3d11.dll + dxgi.dll. The game's factory and its real swapchain are now wrapped: in 4 min of
  menu and mission (same USSR mission), 38,528 frames went through the wrapper at 160-240 fps, and status.txt
  stayed live. The anti-cheat didn't react (2 sessions so far). Uninstall: folder matches the baseline;
  profile unchanged. Log: scratch/logs/goh4k-20261002-121112-9152.log.
- Real swapchain: created at 5.2 s through IDXGIFactory::CreateSwapChain, 3840x2160 B8G8R8A8 with
  **8x MSAA on the backbuffer**, 2 buffers, BLT discard, ALLOW_MODE_SWITCH. The game then calls
  SetFullscreenState(1), ResizeBuffers(0x0) and ResizeTarget(3840x2160), so it runs in exclusive
  fullscreen. The "hdr" option is tone mapping only: the format is BGRA8 and SetColorSpace1 is never called.
- DPI: this time the process was per-monitor aware from the first log line (3840x2160 metrics,
  144 dpi); in session 1 it was unaware at the same point. Cause unknown. M3 will set per-monitor-v2
  awareness itself at DLL load so the mouse math doesn't depend on it.
- Ethan set /goal for autonomous work: no per-session asking from here on (see memory).

### 2026-10-02 M2: built-in capture and diagnostics, DONE
- Proxy (diag.cpp, context.cpp) adds:
  - captures/request.txt, polled 4x a second: capture, trace, dumprt, stats, fix, dumpshader.
  - Ctrl+Shift+F11 hotkey.
  - Backbuffer capture: MSAA resolve, staging copy, WIC PNG plus 1/4 preview and sidecar, written on a worker thread.
  - One-frame traces: every draw/dispatch/clear/copy with its RTs, viewport, scissor, shader ids and SRVs. Deferred-context command lists are spliced in at ExecuteCommandList.
  - dumprt of any event's RT0 or depth target.
  - Texture-creation stats.
  - Shader registry: the DXBC checksum is the id; bytecode is kept for dumpshader.
- Tools: src/shared/image.h (format conversion and PNG); build\drive\imgtool.exe (crop, side, resize, diff).
- proxytest checks the request path end to end against an 8x-MSAA BGRA8 swapchain like the game's.
- In game (4K, Wrap=1):
  - Main menu capture is correct at 3840x2160, resolved from 8x MSAA. The Steam FPS overlay is absent, as expected.
  - The hotkey works. The trace shows 87 events.
  - Mission trace: 5746 events at 149 fps with the trace off.
- References for M3/M4 in captures/:
  - Native 4K: 20261002-122640-menu*, 20261002-122821-mission*.
  - Native 1080p: 20261002-123039-menu1080*, 20261002-123243-mission1080*.
- Switched the game to 1920x1080 fullscreen through its Options menu (it shows a confirm dialog with a
  timeout). options.set differs from the backup only in `mode`; saves are unchanged.
- The menu trace shows a 2D UI only:
  - 3840x2160 x8 backbuffer and depth; viewport is always the full screen; no scissor anywhere.
  - Small offscreen label RTs (36x22, 800x288) are drawn with the stale full-screen viewport.
  - The 1920x1080 menu art is uploaded with UpdateSubresource and then CopyResource'd.
  - So scale only RT/DS-bound screen-size textures, and re-evaluate the viewport on OMSetRenderTargets.
- 1080p UI navigation (differs from 4K):
  SINGLEPLAYER 640,652 > CAMPAIGN 536,194 > SELECT CAMPAIGN 536,290 > USSR 1274,448 > ZA RODINU 536,194 >
  ABSOLUTE ZERO 534,172 > Easy 436,886. CONTINUE 1840,38. In the game menu, EXIT 958,658; confirm YES
  1028,597. Top bar: HOME 480,24, EXIT 1438,24. Options: top bar OPTIONS.

### 2026-10-02 M3: 2x on the main menu (and in a mission), DONE
- Game at 1920x1080 fullscreen with Scale=2. The proxy:
  - creates the swapchain at 3840x2160 and switches the display mode to 3840x2160; GetDesc reports 1920x1080;
  - hooks 14 user32 functions (MinHook) for calls from the exe only;
  - subclasses the game window for WM_SIZE, WM_MOVE and WM_DISPLAYCHANGE;
  - creates RT/DS/UAV textures at the logical screen size or 1/2..1/8 of it at 2x, and scales
    viewports, scissors and copy boxes for them.
- Fixes found along the way (all in the gotchas below):
  1. Cut-off UI labels: the screen viewport comes from the backbuffer's texture desc, which was real
     size. Fixed by normalizing it (later by the backbuffer wrapper).
  2. Black or quarter scene: the HDR scene and post targets were sized from the real backbuffer desc.
     Fixed with a backbuffer wrapper whose GetDesc reports logical size, unwrapped everywhere through the
     generated forwarders.
  3. HBAO+ darkened everything: our rules scaled its 960x540 quarter-res targets and normalized its
     real viewports. Its calls are now recognized by module address and left at real size. Its RenderAO
     hook was unneeded and has been removed; no HBAO+ code is hooked.
  4. Whole frame blurred: the linear-depth target was sized from our scaled depth texture's real desc and
     drawn with a logical viewport, so DOF focused on garbage. Targets created at exactly real screen size
     are now tagged as 2x.
- Evidence in captures/:
  - Menu 20261002-124828-menu2xb vs native 20261002-123039-menu1080: whole frame mean abs 0.36, PSNR
    42.5 dB; button row 47.1 dB. m3-profile-side.png shows the layout is identical.
  - Mission 20261002-133714-mission2xd vs 20261002-123243-mission1080: m3-sharpness-trees2.png shows 4K
    detail. A static region has signed mean +0.12, so exposure is unchanged.
  - Mouse: m3-01 (SINGLEPLAYER at 2x coordinates), m3-04-wheel (list scrolled), m3-move.png and
    m3-rightclick-move.png (squad card select plus right-click move). The cursor reaches all 4 corners, and
    edge-pan scrolls the camera.
- Cosmetic: 1-pixel UI lines come out thinner at 2x. UI text stays 1080p resolution, as intended.
- Still to verify in M4: the glow MSAA downsample (copy.hlsl Load(uv*src_size)), the FXAA texel size,
  soft particles, water, Conquest unit cards and the tech tree. M5: the cursor is half size.

### 2026-10-02 M4 (part 1): mission passes
- Trace diff, native 1080p vs 2x, by capture-analyst: all 3086 matched events are exactly native x2
  (targets, viewports, SRVs, dispatch counts 60x34 -> 120x68, UAVs). HBAO+ is consistent at real
  size and its blend covers the full frame. Only shadow-caster draw counts differ, by 62, which is
  per-frame culling and unrelated to scaling. Scripts are in scratch/cmp.
- SSAO (HBAO+): fixed in M3 by exempting its calls. AO looks right in the mission captures.
- DOF: fixed in M3 (real-size target tagging).
- Glow: the downsample (copy.hlsl MULTISAMPLED, ps 529f0833) does Load(uv * src_size) with src_size =
  logical 1920x1080 from the per-scene CB (dumprt ... cb showed c1 = 1920,1080,1/1920,1/1080). Fix:
  cbpatch.cpp reflects every pixel shader (D3DReflect) and patches constants by name at draw time on a
  copy of the CB, using a lazily shadowed per-scene CB:
  - src_size and textureDim *2 when t0 is scaled;
  - one_pixel /2 when t0 is scaled;
  - fxaaNormPixelWidthHeight /2 and sunMoonPosRes.zw *2 when the target is scaled.
  45 shaders are registered. "fix nocbpatch on" disables it.
  Evidence: patched c1 = 3840,2160,1/3840,1/2160; m4-glowdown-nofix-vs-fix.png shows the fire's glow
  appearing in the downsample only with the patch.
- Soft particles: correct with no change (the shader divides SV_Position by the scaled depth texture's
  GetDimensions). m4-smoke-crop.png, Operation Bustard Hunt intro: no hard edges.
- Tools: dumprt ps:<id prefix> [ds] [cb] (match by pixel shader; cb prints the PS constant buffers).
- Still to check: water/WaveWorks, the sun disc (sky patch), Conquest unit cards and tech tree.
