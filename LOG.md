# Goh4kMod log

## Status
Phase 0 and Phase 1 done (2026-10-02). Next: M1 pass-through d3d11.dll proxy + deploy/uninstall, after
Ethan's go. Game folder untouched so far.

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
- drive.exe click path at native 4K UI (coordinates change once the game runs at 1080p logical):
  SINGLEPLAYER 1600,1192 > CAMPAIGN 1420,630 > SELECT CAMPAIGN 1420,728 > USSR 2240,980 > first campaign
  1420,632 > first mission 1420,608 > Easy 1322,1525 (starts loading). Esc/space skip the intro; space
  continues at "press any button"; CONTINUE 3752,38 ends the cutscene. To quit: Esc > Exit 1916,1196 >
  Yes 1988,1136 > top bar EXIT 2400,22 > Yes 1988,1136.

## Game-folder changes
- 2026-10-02 02:55 deploy: d3d11.dll sha256 7EE009219FBA (new file)
- 2026-10-02 03:05 uninstall: removed d3d11.dll
- 2026-10-02 03:05 uninstall check: folder matches the baseline (26 files)

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
