# Goh4kMod log

## Status
1.0 (M1-M5, 2026-10-02/04, commit e338dd9) replaced by 2.0 native scaling (2026-10-05, commit f08b58d).
- Release 2.1.4 is INSTALLED (2026-11-08), the same files as build\release\Goh4kMod-2.1.4.zip: d3d11.dll and
  dxgi.dll in binaries\x64. Nexus still serves 2.1.3 until Ethan says to upload.
- The game is set to 3840x2160 Fullscreen (Ethan's choice for 2.0); the UI keeps its 1080p size.
- Run uninstall.cmd before playing multiplayer.
- Maintainer notes are in NOTES.md; usage is in PLAN.md "How to use" and the zip's README.txt.

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
- A campaign page under the cursor turns main-menu click spots into mission starts (2880,1306 hit
  CLOSING THE POCKET) -> leave via the top bar (EXIT 2878,46, YES 2056,1196), and snap before clicking.
- A .bat that deletes itself after `pause` stays behind if the user closes the window with X -> cmd is
  killed before the del -> delete first on one already-parsed line: `(goto) 2>nul & del "%~f0" & pause`.
- PowerShell 5.1: casting the empty output of a function to List[object] gives $null, and @() on a generic
  List throws "Argument types do not match" -> build lists with New-Object and pass .ToArray().
- Native scaling: the game sizes targets from its mode (real) but takes some viewports and its UI layout
  from the client size (logical). Symptoms: text 2x and clipped, 3D in the top-left corner, magnified
  backdrop behind prompts -> the three full-screen rules in context.cpp (NOTES.md point 2), HBAO+ calls
  exempt by module address. Diagnose with trace + `fix nouivp on|off` on the same frame. (1.x had the
  mirror-image problem at 2x, fixed with a backbuffer wrapper and target tagging.)
- A wildcard link (`*.obj`) picks up objects of deleted sources: unresolved externals from a file that no
  longer exists -> build.cmd deletes the proxy objects before every build.
- proxytest while the game runs -> the game consumes captures\request.txt -> run tests with the game closed.
- drive.exe click path at native 4K UI (coordinates change once the game runs at 1080p logical):
  SINGLEPLAYER 1600,1192 > CAMPAIGN 1420,630 > SELECT CAMPAIGN 1420,728 > USSR 2240,980 > first campaign
  1420,632 > first mission 1420,608 > Easy 1322,1525 (starts loading). Esc/space skip the intro; space
  continues at "press any button"; CONTINUE 3752,38 ends the cutscene. To quit: Esc > Exit 1916,1196 >
  Yes 1988,1136 > top bar EXIT 2400,22 > Yes 1988,1136.
- Profile "restored byte-identical" but the game shows the test's changes on its next start -> the game syncs
  game_progress/keys/settings.set and campaign saves with its own remote copy (goh.profile_v2; game.log
  `SyncFile ... Local=<mtime UTC> Remote=...`, the newer wins: `overwrite` or `skip`; a clean exit uploads) ->
  a test that exits cleanly persists; put a file back with its original content and a newer mtime, and the
  game keeps it (`skip`). A deleted save comes back from the remote. Prefer tests that create nothing.
- Popup over a small copy of the screen in the top-left corner, held while loading -> rule 3's draw count when
  one popup replaces another in the same frame -> the draw's own coordinates decide (NOTES History 21).

## Game-folder changes
Summarized 2026-10-05. Per-build lines up to 1.0 are in git (e338dd9); later test builds were not kept.
- Deploys of dev and release builds: 2026-10-02 40, 2026-10-04 15, 2026-10-05 83 file copies (d3d11.dll, dxgi.dll, Uninstall Goh4kMod.bat).
- Every uninstall ended with "folder matches the baseline (26 files)": 2026-10-02 03:05, 2026-10-02 12:15, 2026-10-02 14:12, 2026-10-02 15:54, 2026-10-04 02:08, 2026-10-04 03:07, 2026-10-04 03:18, 2026-10-05 14:14, 2026-10-05 14:17.
- Installed at the time of this summary: the 2.1 release (build\release\Goh4kMod-2.1.zip, 2026-10-05 19:42). Later deploys follow:
- 2026-10-05 20:24 uninstall: removed d3d11.dll
- 2026-10-05 20:24 uninstall: removed dxgi.dll
- 2026-10-05 20:24 uninstall: removed goh4k.log
- 2026-10-05 20:24 uninstall: removed Uninstall Goh4kMod.bat
- 2026-10-05 20:24 uninstall check: folder matches the baseline (26 files)
- 2026-10-05 20:24 deploy: d3d11.dll sha256 C977E391B79A (new file)
- 2026-10-05 20:24 deploy: dxgi.dll sha256 61E37C56C576 (new file)
- 2026-10-05 20:28 uninstall: removed d3d11.dll
- 2026-10-05 20:28 uninstall: removed dxgi.dll
- 2026-10-05 20:28 uninstall check: folder matches the baseline (26 files)
- 2026-10-05 20:28 deploy: d3d11.dll sha256 514FB46DCC99 (new file)
- 2026-10-05 20:28 deploy: dxgi.dll sha256 D23886FCBDAC (new file)
- 2026-10-05 20:28 deploy: Uninstall Goh4kMod.bat sha256 87F36EB430A2 (new file)
- 2026-10-05 21:05 uninstall: removed d3d11.dll
- 2026-10-05 21:05 uninstall: removed dxgi.dll
- 2026-10-05 21:05 uninstall: removed goh4k.log
- 2026-10-05 21:05 uninstall: removed Uninstall Goh4kMod.bat
- 2026-10-05 21:05 uninstall check: folder matches the baseline (26 files)
- 2026-10-05 21:05 deploy: d3d11.dll sha256 6D0359A1495A (new file)
- 2026-10-05 21:05 deploy: dxgi.dll sha256 46795A406393 (new file)
- 2026-10-05 21:05 deploy: Uninstall Goh4kMod.bat sha256 87F36EB430A2 (new file)
- 2026-10-05 23:50 to 2026-10-06 01:01: 18 dev-build deploys (d3d11.dll, dxgi.dll) while debugging the borderless
  APPLY (2026-10-06 entry below), the update check's included.
- 2026-10-06 01:04 deploy: a first 2.1.1 package (still with Uninstall Goh4kMod.bat).
- 2026-10-06 01:30 uninstall: removed d3d11.dll, dxgi.dll, Uninstall Goh4kMod.bat; folder matches the baseline
  (26 files).
- 2026-10-06 01:30 deploy: the final 2.1.1 release, no uninstaller (d3d11.dll A1FF60BB798C, dxgi.dll 4543AB43E49F).
- 2026-10-12 00:42 to 01:37: 12 deploys of dev builds and the 2.1.1 release (d3d11.dll, dxgi.dll) for the blur and
  screenshot work (2026-10-12 entry below), the update check included. 01:37 deploy -Release: the 2.1.2 release
  (d3d11.dll C85CBB73AB08, dxgi.dll 4FFDEF415781), installed now.
- 2026-10-19 01:58 deploy: d3d11.dll sha256 CFE625640CCE (replaced our previous build)
- 2026-10-19 01:58 deploy: dxgi.dll sha256 E4BF07468C75 (replaced our previous build)
- 2026-10-19 02:07 deploy: d3d11.dll sha256 C85CBB73AB08 (replaced our previous build)
- 2026-10-19 02:07 deploy: dxgi.dll sha256 4FFDEF415781 (replaced our previous build)
- 2026-10-19 02:17 deploy: d3d11.dll sha256 44EDB485ACD5 (replaced our previous build)
- 2026-10-19 02:17 deploy: dxgi.dll sha256 1D8664B20112 (replaced our previous build)
- 2026-10-19 02:21 deploy: d3d11.dll sha256 C85CBB73AB08 (replaced our previous build)
- 2026-10-19 02:21 deploy: dxgi.dll sha256 4FFDEF415781 (replaced our previous build)
- 2026-10-19 02:21 deploy: d3d11.dll sha256 D7B5BE3ADE1A (replaced our previous build)
- 2026-10-19 02:21 deploy: dxgi.dll sha256 CD1DE4F10EA4 (replaced our previous build)
- 2026-11-08 01:35 to 02:00: 5 dev-build deploys (d3d11.dll, dxgi.dll) and one of the 2.1.3 release (01:39, the update
  check's put-back) for the popup work (2026-11-08 entry below). 02:04 deploy -Release: the 2.1.4 release
  (d3d11.dll 0255C871E7B5, dxgi.dll 29CDA7CE57F5), installed now.
- 2026-11-08 02:12 deploy: d3d11.dll sha256 0255C871E7B5 (replaced our previous build)
- 2026-11-08 02:12 deploy: dxgi.dll sha256 29CDA7CE57F5 (replaced our previous build)

## Cleanup
- Game profile (Documents\My Games\gates of hell\profiles\209810737): the M4 test created a new Conquest
  campaign, campaign\conquest 4.sav (Germany vs USSR, one retreat). Delete it freely. Ethan's own saves and
  campaigns are byte-identical to scratch\profile-backup-2026-10-02.
- options.set: 1.0 needed 1920x1080; 2.0 needs no setting. Since 2026-10-05 it is back at 3840x2160 (set
  through the game's Options); Ethan has since set Windowed borderless, Resolution 1920x1080 himself (his
  profile as of 2026-10-05 23:50: scratch\settings-backup\borderless-20261005-235013). Older copies:
  scratch\settings-backup\ (options.set.2026-10-02 and profile-2026-10-05).
- scratch\shaderpak holds HLSL extracted read-only from shader.pak for analysis (gitignored). Delete freely.
- Game screenshots (Documents\My Games\gates of hell\screenshots): the 2026-10-12 tests' PrintScreens were moved
  out to captures\shots (scripts move each new one). Ethan's own 261012-0031/0032 (1920x1080, 2.1.1) and
  261005-2339 stay.
- scratch\interface-pak holds interface.pak extracted read-only for the font research (gitignored, 2078
  files). Delete freely.
- 2026-11-08 tests left campaign\conquest 7.sav in Ethan's profile and its remote copy (a test Conquest:
  Germany vs USSR, Easy, day 2, two retreats). Delete freely (SINGLEPLAYER > CONQUEST > LOAD CONQUEST >
  DELETE). The same tests set settings.set `{mp {army ger}}`; put back to Ethan's `usa` (content byte-identical
  to scratch\settings-backup\skirmish-dev-20261108-014148, newer mtime so the game's sync keeps it).

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

### 2026-10-02 M4 (part 2) and M5: DONE
- Conquest (problem spot reported in the Steam thread): created a new Conquest campaign with the mod at 2x.
  - Unit cards: m4-cards-crop.png shows art, text and lock icons all correct.
  - 3D squad preview: m4-unitpreview-crop.png shows nine soldiers rendered cleanly.
  - Tech tree: m4-tech-crop.png shows nodes, lines, icons and text all correct.
  - Units deploy by drag-and-drop (drive.exe drag), and the battle started and played.
  - The WineHooks glitches here came from scaling every render target and patching shaders. Our
    size-matched rules leave these UI targets at 1x.
- Water/WaveWorks: none of the tested maps had water in view; a battle trace shows only reflection
  pre-pass draws (512x512, unscaled) and no screen-size issue. Water shaders read SV_Position / GetDimensions
  of scaled textures, so they behave like soft particles. A visual check on a water map is still to do
  (not blocking).
- Cursor (M5): CreateIconIndirect is hooked for exe calls only, and cursors are built at 2x (nearest
  neighbour, doubled hotspot). `drive cursor` shows 32x32 before and 64x64 after.
  "fix nocursorscale on" disables it.
- Alt-tab (M5):
  - Fullscreen: SetFullscreenState(0), ResizeTarget(1920x1080) becomes a real 3840x2160 mode, and
    ResizeBuffers(0x0) stays 2x. Before/after captures: PSNR 52.5 dB. Clicks still map correctly.
  - Borderless ("Windowed borderless foreground"): 3840x2160 windowed, menu clicks correct, alt-tab
    before/after PSNR 53.1 dB.
  - Switched back to Fullscreen afterwards. The keep-settings dialog only sticks if the Options dialog is
    then left with Esc, not X.
- Steam overlay (Shift+Tab): draws at native 4K with its own cursor and closes cleanly. Its calls are not
  converted (exe-only return-address filter).
- Clean install (M5): uninstall, then the folder matches the baseline (26 files). Then a clean rebuild
  (build.cmd), proxytest PASSED, the deploy self-test PASSED, and a fresh deploy.
  Play check on the fresh install:
  - menu vs native 1080p: PSNR 42.1 dB;
  - mission: 29.7 dB, which is 4K detail vs 1080p, signed mean +0.02;
  - cursor 64x64, 159 fps, clean exit.
  The mod is left INSTALLED for Ethan.
- Anti-cheat never reacted, across about 15 single-player sessions with the proxies and user32 hooks.

### 2026-10-02 Shareable release build (Goh4kMod 1.0)
- `build.cmd release` builds the DLLs with GOH4K_RELEASE and packages them as
  build/release/Goh4kMod-1.0.zip (267 KB): d3d11.dll, dxgi.dll, goh4k.ini, README.txt and
  THIRD-PARTY-NOTICES.txt (MinHook BSD-2).
  - Settings and goh4k.log live next to the DLL. Scale=2 is the default; diagnostics are off.
  - /PDBALTPATH keeps local paths out. package.ps1 checks that the DLLs don't contain the user name or
    the project path.
- Exe filter (`Exe=` key, default call_to_arms.exe): the editor, server and workshop exes get a pure
  pass-through and write no log.
- First-run fix (found by testing a user whose game is still at 4K):
  - 2x is now dynamic. A resolution whose 2x doesn't fit the monitor runs natively, rendering and mouse
    included.
  - Switching between 1080p and 4K in Options keeps the real mode at 3840x2160, so Windows sends no
    WM_SIZE and the game's mouse mapping went stale (clicks missed the dialogs). The mod now posts a resync
    message that the subclass turns into WM_SIZE with the size at delivery time. An earlier version that
    posted a size read at post time laid the UI out for 1382x824.
  - Verified in game:
    - stock 4K setting -> install -> launch: native, Options reachable;
    - switch to 1920x1080 + YES: saved, 2x active, menus clickable;
    - 2x -> 3840x2160 + YES: saved;
    - back to 1080p: saved.
- The final zip binaries were deployed with `deploy.cmd -Release` and played into a mission: 2x, cursor
  64x64, 160 fps. Uninstall now also removes goh4k.ini, goh4k.log and goh4k-diagnostics; the self-test
  covers this.
- NOTES.md: architecture, what was learned, and a playbook for fixing the mod after a game update.
- Mishap: a mis-aimed scripted click opened "reset video settings to default?". I answered NO and the
  settings are intact.

### 2026-10-04 Packaging for Nexus Mods
- The zip ships `Uninstall Goh4kMod.bat` (src/package) instead of goh4k.ini. The release defaults
  (Scale=2, Diagnostics=0) are the shipped settings; the DLL still reads a goh4k.ini if someone adds one.
- The uninstaller runs from binaries\x64. It refuses outside the game folder or while the game runs. It
  removes our DLLs (checked by ProductName), goh4k.ini/.log/-diagnostics, then itself; someone else's
  d3d11.dll stays. Self-test section D covers this; deploy -Release and uninstall.cmd know the file.
- README cut to 4K install/uninstall steps only. package.ps1 writes the text files with CRLF.
- Verified in game (deploy.cmd -Release, no ini): log shows `config ... Scale=2` from the defaults,
  `logical screen 1920x1080, real 3840x2160` and 2x cursors; main menu in captures/release-noini/menu.png;
  a click on EXIT landed (exit.png) and YES closed the game.

### 2026-10-04 Half-size mode list: built, verified, reverted
- Tried skipping the manual 1920x1080 step by having the dxgi proxy report every display mode at half size.
  It worked in game (captures/keep4k/), but it removed native 4K from Options and the game rewrote the
  player's saved resolution. Ethan rejected it: the mod must not take away or change game options, and
  no trade-off is worth it. All feature code is reverted to e338dd9; NOTES point 11 keeps what was learned.
- Kept (net goods): the packaging work from earlier today, plus a fix for a flaky self-test. uninstall.ps1
  archived deployed.txt as scratch\deployed-<seconds>.txt, so two uninstalls in one second collided
  (exit 1, about 1 run in 5), and test runs wrote these records into the real scratch\. Now they go to
  $RecordDir (the fake folder in test mode) with millisecond names. Repro loop 0/12 failures, self-test
  5/5. The 23 leaked test records were deleted.
- Restored build verified in game: 1360x768 startup, then logical 1920x1080 / real 3840x2160, full
  stock resolution list in Options (revert-list.png). "Use desktop settings" also runs 2x with 1.0.
- Ethan's options.set is back to `{mode "1920x1080"}` Fullscreen, set through the game's Options. It
  matches the session-start backup except the `{mods "mod_3562631215"}` block (01 WAVE). The game dropped
  that across restarts, stock included. The original file is in scratch/settings-backup/.
- Saves unchanged (compared with scratch/settings-backup/saves-2026-10-04).

### 2026-10-05 Permissions: no approvals except system-wide changes and publishing
- Ethan asked for no approvals except system-wide installs. guard.sh no longer asks for deploy/uninstall.
  Direct writes into the Steam install are now blocked instead of asked; deploy.cmd/uninstall.cmd stay the
  way in. System-wide installs, registry and environment changes moved from deny to ask (settings.json
  too, since a deny rule beats a hook's ask). git push / gh pr create still ask. CLAUDE.md updated.
- Gotcha: the old guard matched any "Gates of Hell" path, which includes the profile under
  My Games\gates of hell. Now it matches only "Call to Arms - Gates of Hell" (the install), and only the
  command text, not the description. Copies count as writes only when the install path comes last.
  28/28 sample tool calls give the expected decision.
- The autoMode rules for ~/.claude/settings.json were blocked by the classifier as self-modification;
  left for Ethan to add through /permissions.

### 2026-10-05 Native-mode experiment (plan step 1): GO
- Idea (Ethan: keep the selected resolution, UI sized automatically): the player keeps native resolution;
  the proxy only tells the game its client area is native/k (k = real height / 1080) and converts the
  mouse. Rendering, the mode list and options stay untouched. Dev-only `UiScale=1` with `Scale=1`.
- Built: src/proxy/ratio.h (Down = floor, Up = ceil, so Down(Up(l)) == l; d==1 equals the 1.0 formulas;
  proxytest checks 2/1, 4/3, 3/2, 8/3 exhaustively plus ClipCursor edges). Minimal lie: client size,
  WM_SIZE, cursor and message coordinates, Screen/Client mapping, ClipCursor. Metrics, monitor info,
  EnumDisplaySettings, window rect, WM_MOVE/WM_DISPLAYCHANGE and SetWindowPos stay real.
- Evidence (captures/uiscale/, game set through its own Options):
  - 4K, k=2: main menu, campaign, Options and mission list laid out exactly like 1080p x2 (30-menu4k,
    51-campaign, 52-options). Mission 3D is native 4K full screen (72-cut, 73-gameplay); the HUD matches
    the 1.0 capture of the same moment (20261002-133714-mission2xd). Box select works (74-select); a move
    order's X lands at the right-click point (79-dest).
  - 1440p, k=4/3 (the 4K monitor in a real 2560x1440 mode): Options (84-kept1440), mission list, HUD
    (88-play1440), hover tooltip, box select and move X at the click point (91-move1440).
  - Text sharpness equals 1.0 (crops side-sp / side-prof against 20261002-141401-final_menu).
  - options.set: only `mode` changed, by my own Options choices. Saves byte-identical.
- Gotchas (symptom -> cause -> fix):
  1. Black screen after the Options confirm timed out back to 1080p -> the factor followed the buffer
     height, which stays stale after a mode change; the game got WM_SIZE 960x540 and never resized ->
     the factor follows the real client size (WM_SIZE, GetClientRect).
  2. Labels 2x and clipped (Options, campaign) -> label surfaces drawn with the stale real-screen
     viewport while the UI projection is logical (trace uiopt #37 #149 #152 #188) -> on targets smaller
     than the screen, a full real-screen viewport is applied as the logical screen.
  3. Mission 3D only in the top-left quarter, rest cream -> tone-map/glow passes set the client-size
     1920x1080 viewport on 3840x2160 targets (trace cut1 #8480 #8484 #8491 #8500); stock 4K never does
     that (20261002-122821-mission-trace) -> on screen-sized targets the logical full-screen viewport is
     applied as the real screen. Scissors get the same two rules; `fix nouivp on` disables them.
- Open: cursor still 32 px at native, Options' read-only "Current mode" line shows 1920x1080 at 4K
  (cosmetic), alt-tab, fps comparison, and running alongside the 1.0 path (plan step 2a).

### 2026-10-05 Native mode alongside the 1.0 path (plan step 2a)
- `UiScale` is now read in release builds too (release default 1, dev default 0). Native mode runs
  whenever the 2x path isn't active; with UiScale=0 every code path is the 1.0 one.
- Cursor: built at the native factor. Whole factors use the 1.0 nearest neighbour; fractional ones use an
  area filter in alpha (AND/XOR halves of mono masks scaled separately). Log: 32x32 -> 64x64 at 4K,
  32x32 -> 43x43 at 1440p.
- Gotcha: a switch to 1920x1080 from the game's own 2560x1440 exclusive mode ran natively instead of 2x
  -> ScaleRequest compared 2x with the current mode (2560 wide) -> it now uses the monitor's desktop
  mode (EnumDisplaySettings registry settings).
- Verified with Scale=2 UiScale=1 (dev config now):
  - proxytest passes in three configs (Scale=1/UiScale=1, Scale=2/UiScale=1 incl. the 2x -> native
    hand-over at 2560x1440, Scale=2/UiScale=0). test-gamefolder.ps1 passes.
  - Start at 1440p: native 4/3. Live switches in Options: 1440p -> 1080p gives 2x (ResizeTarget), 1080p
    -> 4K gives native 2/1 (captures/uiscale/112, 115). Start at native 4K: native 2/1, 64 px cursors.
  - Alt-tab at native 4K: same SetFullscreenState(0)/ResizeTarget sequence as 1.0, clicks correct.
  - 1.0 regression: start at 1080p gives logical 1920x1080 / real 3840x2160 and 2x cursors; the menu
    against 20261002-141401-final_menu differs only in animated sparks, the profile level and the
    online counter (reg10-heat).
- Known limit: after the live 1080p (2x) -> native switch, labels redrawn during the switch showed
  half-size stale text until Options was reopened (116 vs 117).
- End state: options.set and the whole profile byte-identical to the session-start backup
  (scratch/settings-backup/profile-2026-10-05). The game folder has the 1.0 release again
  (deploy.cmd -Release). README/Nexus text, the version bump and a 1.1 package wait for Ethan.

### 2026-10-05 Research: genuinely higher-resolution UI text (plan step 3, report only)
- Fonts are BMFont pages picked by name and size in inline format strings (gamelogic.pak registry
  skins, layout XML). There is no scaled-resource naming anywhere.
- `gui.scale_index` comes from an options key `video/scale_index` (absent from options.set). It only
  scales two layout elements in PageGameMenu.xml. Its eGUI_Helper resource-pattern helpers have no
  callers. Fonts never use it, and setting it would be a game-option change: not viable.
- Exact 2x font pairs in the pak cover about 4% of font references, and none of the HUD (arial_hq).
- Feasible, moderate risk, not built: 2x text surfaces plus font pages rebuilt at runtime at 2x
  (same glyph boxes, so UVs and layout don't change), glyphs drawn with DirectWrite from installed
  system fonts. Arial covers the HUD. Menu faces (pobeda, anka, open_sans) aren't installed and would
  stay magnified. Unverified: whether the glyph and surface passes sample by normalized UV.

### 2026-10-05 Version 2.0: native scaling only (Ethan: drop the 1.0 path, as unintrusive as possible)
- Ethan's report on the first native build: "when in a menu prompt (like applying settings) the background
  expands in a weird way". Cause: the blurred backdrop behind prompts is a 3840x2160 texture (trace
  prompt1 #95-#96) that the UI draws as an image with coordinates of logical / texture size, so only its
  top-left quarter showed, magnified. `fix nouivp on` didn't change it. Fix (rule 3): a UI draw (vertex
  shader learnt from the label-surface draws, matched by bytecode id because the game creates it twice)
  that samples a screen-sized render texture marks it; the game's full-screen pass into it then gets the
  logical viewport. Verified: exit prompt at 4K (captures/v2/02), Options (04), keep-settings confirm at
  1440p (05), in-mission game menu (09), release build (40).
- Removed for 2.0: the whole 2x path (texture scaling, backbuffer wrapper and unwrap, cbpatch, copy-box,
  scissor and ClearView scaling, ScaleRequest/ResizeTarget changes), the GetWindowRect,
  GetSystemMetrics, GetMonitorInfoW, EnumDisplaySettingsW and SetWindowPos hooks (10 of 15 user32 hooks
  remain), WM_MOVE/WM_DISPLAYCHANGE handling, and the `Scale` setting. Shader bytecode copies and texture
  stats are now diagnostics-only; vertex shaders always get their 8-byte id. Forwarders regenerated
  without unwrapping. Hand-written src/proxy (*.cpp/*.h): 3,349 lines in 1.0, 3,686 with both paths, 2,785 in 2.0.
- Checks (dev build unless noted):
  - proxytest passes: 6/6 runs after fixing a flaky dumprt check (deferred clears could become event #1), 3/3 on the final build;
    test-gamefolder.ps1 PASSED with the 2.0 package.
  - 4K: menus, prompt, Options, campaign, mission, HUD, box select, move orders as before.
  - 1440p (game's own mode): mission list, HUD, game menu, edge pan (11-side), tooltip.
  - Live Options switches 4K -> 1440p -> 1080p -> 4K: factor 2/1 -> 4/3 -> 1/1 -> 2/1, no black screen.
  - Stock comparison with Wrap=0 (no mod code): a single minimap click doesn't move the camera there in
    the stock game either (32-33-side), so not a mod issue.
  - Release build at 4K: 151 FPS vs stock 152 FPS at the same mission moment (41-rel-play vs
    32-stock-before); alt-tab then a squad-card click works (42); no diagnostics folder created.
  - options.set: only `mode` differs from the session-start backup (now 3840x2160, Ethan's choice); saves
    byte-identical.
- 1.0 is kept: commit e338dd9, build\release\Goh4kMod-1.0\ and scratch\Goh4kMod-1.0-release.zip; the last
  state with both paths is in scratch\src-hybrid-2026-10-05.

### 2026-10-05 Code-review fixes for 2.0 (all 15 findings)
- Review of f08b58d: 15 findings. All addressed; the in-game ones were checked in game first.
- **Found while testing (not in the review): windowed mode changed the player's setting.** In a window
  smaller than its monitor the game takes the client size as its resolution and saves it: a 2560x1440
  window was saved as `mode`/`custom_mode` "1920x1080". Fix: only a client covering its whole monitor
  (fullscreen, borderless) is scaled; smaller windows get k = 1. Verified: windowed 2560x1440 now saves
  2560x1440, also after dragging the window; borderless and fullscreen save only the chosen mode; exclusive
  1440p is still 4/3. Ethan's custom_mode (1280x720, not selectable in Options) and settings.set window
  location were restored from the session-start backup by hand after the tests.
- **Found while testing: borderless -> fullscreen at the desktop size left the UI at native size** with a
  stale copy in the top-left (captures/v2/59): no WM_SIZE, so the game laid out from its mode. Fix: a
  WM_SIZE resync after the game's own SetFullscreenState/ResizeTarget (at most one queued).
- Review findings:
  - Rules keyed off the client size -> they now use the swapchain buffer size.
  - Rule 3 marked textures forever -> the mark lasts 2 frames and is refreshed while the UI draws it.
  - Screen points scaled around 0,0 while window rect/WM_MOVE are real -> screen points now scale around
    the client's real screen origin (tracked from WM_MOVE/WM_SIZE). Verified in game in windowed mode at
    origin 811,446, menus through to a mission (before windowed scaling was switched off); it now matters
    for a fullscreen/borderless game on a secondary monitor (untested: one monitor here).
    In no mode did the game call ClipCursor or SetCursorPos (dev-build logging), camera rotation included.
  - HBAO+ restore exemption -> exemption removed after an A/B showed no difference (hb-heats).
  - GetClientRect converted any window -> only the game window; desktop stays real (proxytest).
  - Partial scissors -> 114 traces incl. scrolled Options never enable scissoring; the list clips
    correctly (53-crop). Left as full-screen-only, documented.
  - Three separate atomics -> one 64-bit state (client size, buffer size, covers flag) plus a generation.
  - UI-VS table of 4 never evicted, duplicates on races -> 8 slots, duplicate check, logged when full.
  - Per-bind/per-draw COM calls -> target class cached on the view per state generation; slot-0 texture
    checked once per PSSetShaderResources; FixOn returns at once without diagnostics; notes are atomics.
  - Two cursor scalers -> one box filter (exact nearest neighbour at whole factors).
  - PLAN.md mojibake restored from 1.0 (only the How-to-use section differs now); stale LOG lines fixed.
- Checks: proxytest passes (bordered 2560x1440 window passes through, monitor-sized one scaled 2/1,
  origin-relative mapping, desktop rect real); test-gamefolder.ps1 PASSED. In game (dev, then release):
  4K prompt/menus/mission/rotation, borderless, windowed at an offset origin through to a mission,
  exclusive 1440p. Release build: 150 FPS (stock 152 at the same moment), prompt and HUD correct.
- drive.exe gained `mdrag` and `mrotate` (middle-button camera rotation).
- Profile at the end: identical to the session-start backup except Ethan's own `mode "3840x2160"`.

### 2026-10-05 Popup glitch: the screen copy shown small in the top-left for one frame (Ethan)
- Ethan: "the actual prompt window appearing in the top left corner for a split second" when opening a
  menu popup (going back to the main menu from a mission). First mistaken for the stock window flash on
  quit/focus loss (DXGI restores the 1382x824 startup window when fullscreen is left; stock does it too,
  measured with a 5 ms window poller; noted in NOTES known limits).
- New tool: `drive burst <dir> <ms>` saves every presented frame (desktop duplication, reopened after mode
  changes). Caught: captures/v2/burst-exitq/064 (Exit confirm over the Game Menu): the whole screen shrunk
  into the top-left quarter for one frame.
- Cause: rule 3 shrank the backdrop image into its top-left (the UI samples it with logical / texture
  size). Vertex data read back in the dev build: **each popup's backdrop is drawn with texture coordinates
  0..1 in its element's first frame and 0..0.5 afterwards** (0..0.75 at 1440p). The first frame of a second
  popup therefore showed the shrunk image. (The 2-frame mark from the review fixes made it worse: every
  popup's first frame.)
- Fix: the image is no longer changed. An established UI draw of a screen-sized image gets a stand-in
  texture k times its size with the image copied 1:1 into its top-left (7680x4320 at 4K, 3414x1920 at
  1440p, one GPU copy per draw, kept on the image as private data). A draw beyond last frame's count of
  draws of that image is a new element and keeps the game's image; draws resuming after a display change
  continue the count.
- Dev-build self-check reads back each image draw's coordinates and logs disagreements: 0 in menus,
  missions, Options, Esc menu, Exit confirm (incl. over the Game Menu), 1440p. One per resolution switch
  remains: the game draws one frame with the old coordinates on the recreated image while the monitor
  re-syncs (no texture choice is right for that frame).
- Bursts after the fix (dev and release): main-menu Exit and Options, Esc menu, Exit confirm x3, Options
  over the Game Menu, YES back to the menu, 1440p Exit prompt: no intermediate frame, no top-left copy.

### 2026-10-05 2.1 sturdiness (PLAN.md 2.1; Ethan: A, B1 overlays, C, D, E; none of the decisions)
- **Bugs fixed, each with a proxytest case that fails on the 2.0 build (bad9465, worktree run) and passes now:**
  - Rule 3 trusted the last PSSetShaderResources: D3D11 unbinds a view by itself when its texture becomes a
    render target, and 2.0 then put that stale view back after its stand-in draw (and would touch it once
    the game freed it). Slot 0 is now read back from the context.
  - A hook that couldn't be made left the mod half on (WM_SIZE converted, mouse real) and WM_MOVE called a
    null pointer. Now all or nothing (child-process test with a forced GetClientRect failure).
  - A destroyed game window kept the factor (2.0 test: cursor 1028,597 for the real 2056,1194) and a new
    window wasn't followed. WM_NCDESTROY now resets; the next swapchain's window is followed.
  - A replaced window procedure went unnoticed; now put back and resynced. Found on the way: a resync that
    reached the other procedure kept "one queued" forever; it now counts for 2 s only.
  - Small: registered resync message, rule 3 counts only the game window's Presents, 4 MB log cap.
- **Overlays (B1):** a hook check every 2 s. The Steam overlay patches over 5 of our hooks after us
  (`input hooks: 5 of 11 patched over`). Its GetCursorPos jumps on (538 of 551 calls with the game's return
  address, converted); its GetMessageW calls us (stack: overlay, overlay, call_to_arms.exe), so message
  coordinates stay real with the overlay on: harmless, the game reads the mouse with GetCursorPos (all
  sessions ran with the overlay). Not "fixed" (the overlay's own window would get logical coordinates);
  clicking inside the overlay works. A removed patch on a converting hook switches the mod off.
- **Screens narrower than 16:9 (C1):** k = min(w/1920, h/1080). In game: 2560x1600 fullscreen -> 4/3,
  logical 1920x1200, menus and the Exit prompt right (stand-in 3414x2134, captures/v21/12-13); 1920x1200 ->
  not scaled (was 10/9). 3840x2160 back afterwards.
- **Stand-in (C3):** shared per size/format, released 5 s after the last popup draw (log: created at the
  prompt, `released (unused for 5 s)`), and with the swapchain (it holds the device).
- **Borderless (A5):** with Resolution 2560x1440 the game still renders at the desktop size (buffers
  3840x2160), so buffers never differ from the window; stand-in size is buffer² / logical anyway.
- **Preset sweep (C2):** Ethan's custom, Minimum, Medium, Maximum in a mission (Absolute zero), captures and
  traces (now with `game-vp=` where a rule changed the viewport). Visuals right in all four. After an
  in-game apply the game sizes ~1100 full-screen draws per frame from the client size (15 after a fresh
  start); rule 2 restores all; no draw anywhere has a viewport smaller than its target; no scissoring.
  An MSAA change recreates the swapchain: one frame of magnified popup backdrop (dev self-check), like a
  resolution switch (NOTES known limits).
- **Logs (D2):** game build vs the verified one (stamp 6ab3dcc7), unconverted coordinate imports (none),
  health line, hook-layer lines, session summary at unload. The game calls mouse_event every 10 s with a
  relative +-1 px nudge (dev logging): nothing to convert.
- **Update check (D3):** src/scripts/update-check.ps1; the session briefing flags a changed exe.
- Docs: NOTES (assumptions table, History 12-15, playbook step 0, limits), README troubleshooting, CLAUDE.md.
- Update check runs: the first (references) showed the script's own bug: Esc plus a centre click in its
  intro-skip loop opened the Game Menu in the mission and hit RESTART (profile restored, no harm). The loop
  now uses only Space and CONTINUE's corner. Rerun: 15 of 15 PASS (menu 46.9 dB, prompt 51.3 dB, 0 of 253
  odd burst frames, options, mission menu, all log checks, profile byte-identical).
- Version 2.1 (version.rc, package, log line, README). proxytest PASSED, test-gamefolder PASSED.
  Release 2.1 deployed and checked: 10 of 10 hooks, health line, the Exit prompt correct, stand-in released
  after 5 s, session summary. Ethan's profile byte-identical to the session-start backup
  (scratch/settings-backup/profile-2026-10-05-v21).
- Cleanup pass (/simplify): one view-class cache for rules 1-3; slot 0 read back with no
  PSSetShaderResources override; one stand-in release path; one module and exe-header helper; hook counts
  only while measuring (patched-over count in the health line); the answered dev-only probes removed (10
  hooks in every build); one off switch; update-check.ps1 on gamefolder-common.ps1. Update check 15/15.
- Code-review fixes (Ethan: items 1-9):
  - update-check.ps1 puts back exactly what was installed (nothing -> uninstall.cmd, newest release ->
    deploy -Release, dev -> as is, other -> FAIL line) and the whole profiles folder, in a finally block.
  - Subclassing stores the next procedure before ours goes in (a message could arrive in between).
  - A newer game window that appears while the current one exists is followed once that one is destroyed.
  - Shut-off is atomic: an off bit in the state word, so a WM_SIZE on another thread cannot store k > 1 again.
  - The import scan in DllMain is bounds-checked against the image and runs under SEH.
  - Past the 4 MB log cap, WARNING lines and the session summary still go in (64 KB reserve).
  - Stand-in creation failure: an atomic time, retried after 10 s; the 8K size limit no longer blocks others.
  - This file: today's deploy lines condensed into the game-folder summary.
- Ethan: no logging in the release, and the README as simple as 2.0's without the multiplayer note (the
  Nexus page has it). Release: no goh4k.log; the log-only work (game-build scan, hook-call counting,
  session counters, health line) is compiled out (`kLogging`); the shut-off itself stays. README back to
  install/uninstall only; nexus-page.txt loses its "attach goh4k.log" line. Release rebuilt, uninstalled
  and redeployed clean: menu and Exit prompt right, no goh4k.log written; profile unchanged.

### 2026-10-06 Borderless: changing the resolution broke the UI (Ethan)
- **Symptom:** in Windowed borderless, Options > Resolution > APPLY (any APPLY) left the whole UI at native
  size, the menu backdrop shrunk into the top-left 1920x1080, until a restart (captures/borderless/05-06,
  trace captures/20261005-235302-bl-broken-trace.txt: UI vertex shader on the backbuffer at 3840x2160).
- **Cause** (dev-only probe hooks, since removed): on APPLY the game thread re-applies the borderless
  window (GetMonitorInfoW 3840x2160, SetWindowLongPtr style/exstyle, SetWindowPos with SWP_FRAMECHANGED and
  no size change, ShowWindow), stores the monitor's real size as its screen size and SendMessages its window
  WM_USER+0x19, whose handler lays the UI out at that size. Same window size, so no WM_SIZE: the mod never
  told it the logical size again. The resync after SetFullscreenState/ResizeTarget (fullscreen) doesn't run
  in this path (no DXGI call at all).
- **Fix 1 (input.cpp):** a WM_WINDOWPOSCHANGED with a frame change or an explicit size and no WM_SIZE (k > 1)
  arms a resync; it is queued right after the game has handled the first private message (WM_USER..0xBFFF)
  sent from another thread, or 250 ms on from Present. The game then answers our WM_SIZE with its own
  WM_USER+0x19 and ResizeBuffers(0x0), and lays out at 1920x1080 again.
- Tried and dropped, each in game: resync posted at the reconfigure (handled before the game's store:
  broken); a WM_SIZE just before WM_USER+0x19 and a logical GetMonitorInfoW (the handler then shrinks the
  window to 1920x1080: k = 1, 1080p rendering); a 50 ms settle delay and a k = 1 window during the handler
  (labels drawn in between stay small).
- **Fix 2 (context.cpp, rule 1):** the render thread still draws one frame projected at the real size (the UI
  shader's vs_scene viewProj, 2/3840, logged per label draw), after the resync; rule 1 squeezed its label
  surfaces to 1920x1080, and those it keeps (the APPLY label, the option description, CONFIRM) stayed half
  size. Rule 1 now reads the projection from the game's UpdateSubresource of that constant buffer and leaves
  the game's viewport while it is the real one (`ui: the game projects its UI at 3840x2160 ... / 1920x1080`).
- Stock reference (UiScale=0, captures/borderless/20-24): the confirm prompt's backdrop is a clean blur;
  with the mod now too (full-res crop, no double image). Rule 3's dev self-check still flags one image
  draw per frame (coordinates 0..1 with the stand-in) while the resolution list or the prompt is up, with no
  visible effect at full resolution; left as is.
- Tests: proxytest PASSED with two new cases (reconfigure + private message from another thread -> logical
  WM_SIZE right after it; without a message -> from Present). In game, borderless 3840x2160 desktop,
  Resolution 1920x1080 -> 1600x900/1920x1200: APPLY + YES (captures/borderless/final-yes) and APPLY + timeout
  (final-revert): prompt, Options, menu (43.6-43.8 dB vs the update-check reference) and Exit prompt
  (50.1-51.2 dB) right; options.set keeps the chosen mode only.
- **Self-check (scale.cpp, input.cpp):** the last UI projection is kept; if it stays at the real size for
  250 ms while k > 1, a resync (once per change). Covers a resync that came too early (at fullscreen start
  the first private message after a reconfigure was 0x418, one of the game's per-frame messages, sent from
  another thread there; harmless duplicate) and any other path that lays the UI out at the real size. Proof:
  `fix noreconfig on` (dev A/B, switches the message path off) + APPLY: `projected at the real size for 250
  ms: resync` 250 ms after the change, prompt and Options right (captures/borderless/selfheal). The
  projection never left 1920x1080 in the fullscreen sessions (menus, prompt, Options, mission, 2560x1440).
- Fullscreen: update check 16 of 16 PASS (menu, Exit prompt 0 of 249 odd frames, Options, mission, mission
  menu, logs; profile restored), with Ethan's earlier fullscreen options.set for the run. Options APPLY
  3840x2160 -> 2560x1440 (k 4/3) and the timeout back (k 2/1), Exit prompt 51.9 dB after.
- Version 2.1.1 (version.rc, package.ps1, log line, README, build.cmd, NOTES). Release built and tested in
  borderless (APPLY + YES: prompt, Options, menu 46.5 dB, Exit prompt 50.8 dB; no goh4k.log); installed.
  Ethan's profile byte-identical to the session-start backup (scratch/settings-backup/borderless-20261005-235013).
- Ethan: no uninstaller in the release (antivirus tools flag the .bat); the README just says to delete the two
  DLLs. src/package/Uninstall Goh4kMod.bat removed; package.ps1 ships d3d11.dll, dxgi.dll, README.txt,
  THIRD-PARTY-NOTICES.txt; deploy -Release copies the two DLLs; uninstall.cmd still removes a leftover
  uninstaller from older installs (test-gamefolder: section C covers it, D checks the package contents). README
  and nexus-page.txt: install 2 files, uninstall = delete d3d11.dll and dxgi.dll. Release rebuilt: menu 45.5 dB,
  Exit prompt 51.4 dB, clean quit.
- Gotcha: one clean quit (Exit > YES) after the fullscreen test runs wrote options.set as Fullscreen 3840x2160
  although the game had been started on Ethan's borderless 1920x1080 file. Not the mod (it never touches the
  profile; options.set is not in the game's cloud profile goh.profile_v2, which holds game_progress, keys and
  settings). Repeated with the same release: borderless loaded and kept on quit. Check options.set after test
  sessions that switch display modes; restore from the session-start backup.

### 2026-10-12 Nexus report "3D blurrier with the mod": game screenshots were 1920x1080 (2.1.2)
- Report (zill01, Nexus, 4K, RTX 5090, 8x MSAA): with the mod the 3D looks blurrier than without; in
  fullscreen "PrintScreen" saves 1920x1080 and Options shows "Current mode: 1920x1080"; borderless looks
  blurry too. Their trace's 1920x1080 texture into the 4K target is the main-menu art (stock does the same).
- **The live picture is native.** New dev request `uiscale 0|1` switches the scaling off and on live, so mod
  and stock share one mission frame (scratch/ab-live.ps1): same render-target sizes (1305 vs 1304 draws to
  3840x2160, 6 to 1920x1080 in both), detail score (new `imgtool sharp`) mod 7.30 / stock 7.17 / mod 7.05 in
  fullscreen, 7.29 / 7.16 / 7.06 in borderless (the drift is the mission's time of day; a 1080p upscale would
  halve it), 2x crops alike (captures/blur/live-fs). Post-process constants: all screen sizes and texel sizes
  are 3840x2160 except lens effects' c3.xy (960,540 vs 1920,1080) and c17.zw textureDims_LR (1920x1080 vs
  3840x2160); per the shader source neither is used in normal view (thermal/drone vision only).
- **The game's own screenshot was the 1080p picture.** PrintScreen saves a JPEG (My Games\gates of hell\
  screenshots): 1920x1080 with the mod, 3840x2160 with `uiscale 0`. Staging-triggered trace (dev `fix
  tracestaging on`): the game makes a CPU-readable texture at its client size, a GPU texture from its desc,
  ResolveSubresource from the 3840x2160 x8 backbuffer into 1920x1080 (size mismatch; the driver shrank the
  frame), copies and saves at the readable texture's size (no D3DX: its own encoder). Fix (rule 4,
  context.cpp/device.cpp): a CPU-readable, unbound, single-sample texture at exactly the logical size is made
  at the real size; the GPU texture follows from its desc, so the resolve is same-size again. First tried also
  resolving into a real-size twin and redirecting the copy: unnecessary once seen that the game copies the
  desc, removed. `fix noshot on` for A/B. Texture stats: the game makes no other staging textures.
- Evidence (captures/shots): fullscreen and borderless, menu and mission, fixed 3840x2160, stock 3840x2160,
  unfixed 1920x1080; mission detail fixed 7.31 / stock 7.27 / unfixed 3.36 at screen scale (unfixed-vs-fixed.png
  is the reported blur). proxytest: new case (logical-size readback made real, a 256x256 one as asked), PASSED.
  Update check 15 of 16 PASS (menu 43.8 dB, Exit prompt 50.6 dB and 0 of 253 odd frames, Options, mission menu,
  all log checks, profile restored); the FAIL was its own "put back": an older dev build was installed.
- **Ethan: borderless 4K -> Fullscreen 4K rendered 1080p.** Reproduced with Resolution 3840x2160: ResizeTarget
  3840x2160, stays 4K. His borderless profile had Resolution 1920x1080 (scratch/settings-backup/
  borderless-20261005-235013); borderless ignores it, Fullscreen applies it: the game's own behaviour (NOTES
  History 18). The mod's "Current mode 1920x1080" in Options makes this easy to misread; Nexus page text now
  says what it means and to check Resolution when leaving borderless.
- Version 2.1.2 (version.rc, package.ps1, log line, README, build.cmd, NOTES, PLAN). Release built
  (build/release/Goh4kMod-2.1.2.zip) and installed; its game screenshot is 3840x2160, no goh4k.log. Ethan's
  profile byte-identical to the session-start backup (scratch/settings-backup/blur-20261012-004207).

### 2026-10-19 Project folder organized (Ethan: "organize the project folder")
- Nothing deleted, no tracked file moved; paths the scripts and the proxy use stay where they are
  (captures/ root for request.txt, status.txt, requests.log and new diagnostics; scratch/ root for deployed.txt,
  baseline-binaries-x64.txt, update-check-*.txt; logs/, settings-backup/, phase1/, shaders/).
- Moved, so older journal entries that name these files bare now find them here:
  - captures/2026MMDD-*: the proxy's capture/trace/stats output (764 files) -> captures/diag/2026-MM-DD/.
  - captures/m3-*.png, m4-*.png (milestone evidence) -> captures/m3/, captures/m4/.
  - scratch/deployed-*.txt (37) -> scratch/deploy-history/; uninstall.ps1 writes new ones there
    (gamefolder-common.ps1 $RecordDir). Self-test (test-gamefolder.ps1) PASSED.
  - scratch/*.png (2026-10-02 crops, rel-menu.png) -> scratch/images/.
  - scratch/ab-blur.ps1, ab-live.ps1, shot-check.ps1 (include path now ..\..\src\scripts), an.awk, segments.*,
    vp.*, gamepid.txt, switch-logline.txt, shots-before.txt -> scratch/scripts/.
  - scratch/minhook-1.3.3.zip and minhook-src/ (source of the vendored copy) -> scratch/downloads/.
- CLAUDE.md named the old project path (Documents\Claude-Projects\Goh4kMod); now Documents\Claude\Goh4kMod.

### 2026-10-19 Nexus reports reviewed; "selection outlines" bug not reproduced
- Nexus (read 2026-10-19): Bugs tab, Adiday 16 Oct, 2.1.1, 4K TV: "Selection outlines (yellow) render at 1080p
  size and offset". Posts: zill01 (blur, 2.1.1, answered by 2.1.2), CaiusAfterHours 17 Oct ("runs at lower
  resolution"). Nexus still serves 2.1.1; 2.1.2 (1080p screenshots fixed) was never uploaded.
- Hypothesis from the shader source (lenseffects_v2 multi-channel mask prepass reads an MSAA mask with
  Load(SV_Position)): the game makes the mask at the client size. **Refuted** in game (dev build, 4K Fullscreen,
  8x MSAA, Absolute Zero, squad drag-selected; mod vs live `uiscale 0`): no outline pass runs (no draw binds t6),
  no 1920x1080 MSAA texture in any trace or stats, mod and stock draw sequences identical apart from rule-2
  viewports. Selection circles, hover tooltip and units at the barn's corner: same place mod vs stock
  (captures/outline: side-mod-stock.png, side-hover.png, side-barn.png; capture-analyst report).
- Tooling: scratch/scripts/ol-grab.ps1 (snap + capture + trace + stats of the running game into captures/outline).
- Restored: 2.1.2 release installed again (deploy -Release), profiles byte-identical to
  scratch/settings-backup/live-20261019-015829. No code changed.

### 2026-10-19 2.1.3: buffer-size guard (Ethan: convert, then upload with notes)
- swapchain.cpp RealBufferSize: ResizeBuffers/ResizeBuffers1 and a later CreateSwapChain/ForHwnd on the game window
  asking for exactly the logical size, while the real client (read at call time) covers its monitor at k > 1, get the
  real client size; logged `guard:` (dev). Present self-check: WARNING if the buffers are logical at two 2 s checks.
  NOTES History 19.
- proxytest PASSED, new: ResizeBuffers(1920x1080) on the 4K window gives 3840x2160 buffers, GetClientRect stays
  logical, ResizeBuffers(0x0) stays real; dev log `guard: ResizeBuffers asked for the logical size 1920x1080 while
  the client is 3840x2160: made 3840x2160`. In the game the guard never fires (CreateSwapChain 3840x2160,
  ResizeBuffers 0x0). Update check 16 of 16 PASS (captures/update-check/20261019-021704), no warnings.
- Release 2.1.3 (version.rc x2, package.ps1, util.cpp, README with what's new for 2.1.2/2.1.3 and the
  "Current mode" note, build.cmd, NOTES, PLAN): build/release/Goh4kMod-2.1.3.zip, test-gamefolder PASSED,
  installed (deploy -Release); main menu at the 1080p UI size, no goh4k.log (captures/outline/release-213-menu.png).
- Uploaded to Nexus (Ethan's go, 2026-10-19 02:37): Goh4kMod-2.1.3.zip as an update of the main file
  "Goh4kUIMod (Beta)", 2.1.1 archived, mod version set to 2.1.3, changelog = README's what's new. Description,
  posts and the outline bug report left untouched. Nexus virus scan was still running at upload.

### 2026-11-08 2.1.4: a popup replacing a popup (Ethan: "on full screen it doesn't seem to work correctly anymore")
- Checked first: call_to_arms.exe unchanged (2026-10-02, 1.067.0); System32 d3d11/dxgi/user32 from 2026-10-13; NVIDIA
  driver 32.0.16.2101 installed 2026-10-28 (after the last PASS). Ethan's profile unchanged since 2026-10-15. Update
  check with the 2.1.3 dev build, 4K Fullscreen: 16 of 16 PASS (captures/update-check/20261108-013505). His game.log
  (release 2.1.3, 01:28): Options open 46 s on the main menu, then MULTIPLAYER > skirmish > LAN create > 1v1
  Balzerweg vs AI, through status-dialog popups into loading. Nexus posts not read: WebFetch gets 403, the Chrome
  extension wasn't connected.
- Reproduced in single-player (Conquest; the MULTIPLAYER menu stays undriven, anti-cheat rule): Create Conquest >
  CONTINUE showed "Please wait" over a half-size copy of the menu in the top-left quarter, black elsewhere, held ~10 s
  while the game loaded (captures/nov08/04-after-continue.png); dev log `ui check frame 12949: image draw uv max 1,
  stand-in 1: DISAGREES`. Cause: rule 3's per-image draw count (NOTES History 21).
- Fix (context.cpp): rule 3 decides from the draw's own texture coordinates (UiImageDrawCoords), read from CPU copies
  of the bound vertex and index buffers (UiShadow: one readback on first use, then the immediate context's
  UpdateSubresource; a copy or discard re-reads; deferred writes or CPU/GPU-writable buffers: untrusted). Logical /
  texture size -> stand-in, 0..1 -> image, anything else -> the count as before. Cost: one private-data lookup per
  UpdateSubresource, 11.6 ns measured (~15 us a frame at 1300 calls in a battle).
- proxytest: new case (first frame, next frame, a popup's first frame right after another's, two established draws in
  a frame) PASSED; against the 2.1.3 release DLLs it fails exactly there (7680-wide stand-in on a first frame).
- In game, fixed dev build: the same transition shows "Please wait" over the full blurred menu
  (captures/nov08/fix/please-wait-before-after.png); operation map, CONFIRM, battle start, battle Esc menu > Options,
  RETREAT > YES ("Saving progress" replaces the confirm), defeat screen, results, Exit prompt: bursts without odd
  frames, 0 DISAGREES, session `9961 UI image draws (9961 decided by their coordinates)`. Battle mod vs live
  `uiscale 0`: same passes and target sizes (captures/20261108-014628/014636-nov08-battle-*). Update check 16 of 16
  PASS (captures/update-check/20261108-020033; 3642 of 3642 image draws by coordinates).
- Version 2.1.4 (version.rc x2, package.ps1, util.cpp, README, build.cmd, NOTES, PLAN). build/release/Goh4kMod-2.1.4.zip,
  test-gamefolder PASSED, installed (deploy -Release). Release in game (no goh4k.log): main menu, LOAD CONQUEST >
  "Please wait", battle HUD (147 FPS), RETREAT > YES > "Saving progress" > Defeat by burst: no odd frames
  (captures/nov08/rel). None of these is the popup-replaces-popup case itself (page > popup; "Saving progress" is a
  dim overlay); the decision code is the same in both builds (not behind kLogging/diagnostics). Release, 4K
  Fullscreen: alt-tab out (desktop shown) and back: UI at its 1080p size, OPTIONS opens from its button, backdrop
  right (captures/nov08/alttab).
- Profile at the end: options/settings/keys/game_progress.set byte-identical to the pre-test backup
  (scratch/settings-backup/skirmish-dev-20261108-014148, the game's sync agreeing: `include settings.set` at exit);
  only campaign\conquest 7.sav is new.
- See Gotchas (the game's own sync) and Cleanup (conquest 7.sav left; settings.set army put back).
- Not done: the MULTIPLAYER > skirmish path itself; Options' "Current mode 1920x1080" is unchanged (README explains it).
