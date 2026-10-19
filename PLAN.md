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

## How to use (2.0)
- **Install:** run `deploy.cmd` (dev build) or `deploy.cmd -Release` (the packaged zip). It copies the DLLs
  into the game's binaries\x64 and records the change in LOG.md.
- **Game settings:** none. Play at the monitor's own resolution, Fullscreen or Windowed borderless; the UI
  keeps its 1080p size and the 3D renders natively. In a smaller window, and at 1920x1080 and below, the
  mod changes nothing.
- **Before multiplayer:** run `uninstall.cmd` (double-clicking is fine). It removes the DLLs and checks the
  folder against the original baseline. No setting needs changing back.
- **Settings:** config\goh4k.ini in this folder (no redeploy needed; restart the game after editing):
  - UiScale=1 (default) turns on native scaling; UiScale=0 is pure pass-through.
  - Wrap=0 forwards the DLL calls only and does nothing else.
- **Diagnostics:** write commands to captures\request.txt while the game runs: `capture <tag>`,
  `trace <tag>`, `dumprt <event>|ps:<id> [tag] [ds] [cb]`, `stats`, `dumpshader <id>`,
  `fix nouivp|nocursorscale on|off` (fixes only work with diagnostics on). Ctrl+Shift+F11 also captures. Results go to captures\, and the log
  goes to scratch\logs\.
- **After a game update:** if Steam's file check runs, it doesn't remove our added DLLs. If the game
  misbehaves after an update, run uninstall.cmd, then rebuild and deploy.
- **Shareable build:** `build.cmd release` produces build\release\Goh4kMod-2.1.3.zip. The zip has no
  goh4k.ini (the built-in defaults are the settings), but the DLL reads one next to it if present. It writes
  no log. Players uninstall by deleting the two DLLs (2.1.1 ships no uninstaller script). See NOTES.md for how
  it works and how to fix it after a game update.
- **History:** milestones M1-M5 above built 1.0 (game at 1920x1080, rendered at 2x). 2.0 replaced that with
  native scaling only (LOG.md, 2026-10-05).

## 2.1: sturdiness (approved 2026-10-05)
Ethan's go, with cuts: in B only the overlays (B1) stay; B2-B5 are out (and with B3 the README's
replace-file warning). None of the "Decisions for Ethan" (no signing, no ReShade chain-loading, no cursor
follow-up, no GitHub push).
Status (2026-10-05): done; results in LOG.md "2.1 sturdiness". A5 needed no fix (borderless renders at the
desktop size). B1 measured: the Steam overlay patches over 5 hooks; deliberately no caller-layer change.
Later (Ethan): the release build writes no log (goh4k.log below means the dev build's log now), and the
README stays as simple as 2.0's, without the multiplayer note (the Nexus page has it).

Ethan asked what could go wrong later (game updates, other mods, other setups) and to make the mod as
sturdy and independent as possible. The rule for all of it: when something around the mod changes, the mod
keeps working, or it switches itself fully off and says why in goh4k.log. It is never half on, never
crashes and never changes a game option.

### A. Bugs found in the current code
1. **Crash risk with popups.** context.cpp keeps the game's slot-0 texture view as a raw pointer (m_ps0,
   m_ps0Copy).
   - D3D11 unbinds a view by itself when its texture becomes a render target, as the popup backdrop
     does. The game may then free the view.
   - The next UI draw before the game binds slot 0 again would read freed memory, and UiAfterDraw would
     re-bind a view the runtime had cleared.
   - Fix: ask the context what is bound (PSGetShaderResources) at the draws that need it.
   - Proof: a proxytest case (bind, bind as target, release, draw), and prompts in game.
2. **A failed hook leaves the mod half on, and can crash it.** MinHook can fail: Exploit protection's
   "Arbitrary code guard", another tool's patch in the way, a Windows update.
   - The window is still subclassed. WM_SIZE is converted but the mouse isn't, so clicks land at k
     times the place.
   - WM_MOVE can call a null Real_GetClientRect.
   - Fix: all or nothing. Any failed hook undoes the others, and the mod stays off for that session
     (logged).
   - Proof: proxytest with a forced failure.
3. **A recreated game window isn't followed.** Only the first window is subclassed. If the game ever
   destroys and recreates it (some engines do on a mode change), the mouse stays converted with the old
   factor.
   - Fix: on WM_NCDESTROY, unsubclass, go back to k = 1, and follow the next swapchain's window.
   - Proof: proxytest.
4. **A lost window procedure goes unnoticed.** If an overlay or the game replaces the window procedure,
   WM_SIZE stops being converted, and the next mode change leaves the UI and the mouse disagreeing.
   - Fix: when another procedure sits above ours, check with a private message that ours still answers.
     If it doesn't, subclass again and resync.
   - Proof: a proxytest that unhooks us.
5. **Borderless below the desktop resolution is untested** (1920x1080 or 2560x1440 on the 4K desktop).
   - If the game keeps a desktop-size window with smaller buffers, rule 3 sizes the popup stand-in from
     the window factor (2) instead of buffer / logical (4/3), so the popup backdrop is wrong.
   - Test first. Fix the ratio if needed.
6. **Small:**
   - The resync message is WM_APP+0x4B7, which could clash with one of the game's. Use
     RegisterWindowMessage.
   - Rule 3's frame count advances on any wrapped swapchain's Present. Count only the game window's.
   - The release log gets a size cap (4 MB).

### B. Other software
1. **Overlays that hook the same user32 functions** (Steam; Discord is installed here).
   - If an overlay hooks on top of ours, the mod sees it as the caller and doesn't convert, so clicks are
     off by k while it runs. Steam was fine in the 1.0 tests.
   - Measure: a log line naming, for each hooked function, the module its first jump lands in. Run with
     the Steam overlay, and Discord's only if Ethan OKs starting it.
   - Fix only if layered: also accept a game call that arrives through one hook layer.
2. **Third-party borderless tools** (Borderless Gaming, Special K and the like) on a game in Windowed mode.
   - The window then covers the monitor and the mod scales. In Windowed mode the game takes the client
     size as its resolution and saves it, so the mod would likely change the player's setting.
   - Test by restyling the window ourselves (drive.exe), with options.set backed up.
   - Likely fix: if the game sizes its buffers to the logical size it was told, treat the window as
     windowed and switch off for it.
3. **Mods with the same file names.** ReShade installs as dxgi.dll; DXVK, Special K and OptiScaler use
   d3d11/dxgi.
   - Copying ours over theirs deletes theirs. Theirs over ours leaves the mod off, which is safe but
     silent.
   - Fix: each DLL checks its partner at start and logs "dxgi.dll here is not Goh4kMod's: mod off". The
     README says: if Windows asks to replace d3d11.dll or dxgi.dll, stop.
   - Chain-loading ReShade is decision 2.
4. **Antivirus and Smart App Control** can block unsigned proxy DLLs, and then the game won't start. The
   README's troubleshooting says what to do. Signing is decision 1.
5. **CET:** link with /CETCOMPAT, so a future game build or Windows setting that enforces shadow stacks
   still loads the DLLs. No effect otherwise.

### C. Other setups
1. **Monitors narrower than 16:9.** k from the height alone gives a logical width under 1920 (2560x1600
   and 1920x1200 both become 1728x1080), a layout the UI was likely never tested in.
   - Proposal, a behaviour change for those screens only: k = min(width / 1920, height / 1080). The
     logical screen is then never smaller than 1920x1080.
   - 2560x1600 becomes 1920x1200. 1920x1200 isn't scaled (it is already 1080p-class). 16:9, ultrawide
     and 32:9 are unchanged.
   - Proof: a proxytest table, and in game at 1920x1200 if the monitor lists it.
2. **Graphics settings.** Every test used Ethan's settings, and the rules depend on which passes run.
   - Sweep: presets Low to Ultra, each AA mode, HDR off, SSAO off, at 4K (one at 1440p).
   - Compare each stock (UiScale=0) against the mod: the same image apart from the UI size.
   - Settings change only through the game's Options. options.set is restored byte-identical from the
     backup.
3. **High resolutions and VRAM.** Rule 3's stand-in is k² times the screen: 133 MB at 4K, 26 MB at 1440p,
   420 MB at 5K. At 8K it is impossible (over 16384 px): the backdrop stays zoomed, with no crash.
   - Fix: one shared stand-in per device, made when a popup opens and released a few seconds after its
     last use. Log its size.
4. **Checked, nothing to do:**
   - Windows display scaling: the game is per-monitor DPI aware by its manifest. Our DPI call failed
     harmlessly (error 5) in all 44 game runs, and Ethan's desktop is at 150%.
   - The editor, dedicated server and workshop tool share the folder, and two of them load d3d11.
     `Exe=` gives them pure pass-through.
   - Our DLLs need no Visual C++ runtime (static CRT, Windows DLLs only).
5. **Not testable here** (added to Known limits; ask players for goh4k.log): AMD and Intel GPUs, a second
   monitor, real 1440p/ultrawide/16:10 panels, Proton and the Steam Deck (the mod is inert there by
   default).

### D. Game updates
1. **An assumptions table in NOTES.md:** each game behaviour the mod relies on, what breaks when it
   changes, what the mod does then, and how the update check sees it. The behaviours:
   - the UI layout follows the client size;
   - the mouse comes through GetCursorPos/GetMessageW/ScreenToClient;
   - rules 1-3;
   - cursors come from CreateIconIndirect;
   - the swapchain comes from IDXGIFactory::CreateSwapChain, called by call_to_arms.exe.
2. **Health lines in the release log:**
   - the game build (exe timestamp and size), next to the build the mod was verified with;
   - "hooks 10/10, window followed, swapchain wrapped", and k;
   - the hook-layer line (B1);
   - a warning if, a minute into k > 1, the UI shader was never recognised (rule 1 didn't fire: the game
     changed how it draws text);
   - a warning if the exe imports a coordinate function the mod doesn't convert (GetCursorInfo,
     GetWindowInfo, PeekMessageA, GetPhysicalCursorPos).
   - The game imports mouse_event today. Dev logging confirms it never moves the cursor with it.
3. **A one-command update check** (dev build, run by Claude):
   - Steps: deploy and launch; main menu; Options; the Exit prompt (burst); a skirmish's first frames;
     alt-tab; a switch to 1440p.
   - Checks: captures diffed against references; the log checked for the health lines and DISAGREES.
   - Then uninstall and give a PASS/FAIL report.
   - The session briefing flags when call_to_arms.exe has changed since the last PASS.

### E. Players
The README gets:
- Troubleshooting:
  - the game won't start or closes itself: run the uninstaller;
  - UI at its normal size: use Fullscreen or borderless at the monitor's resolution;
  - mouse off: send goh4k.log.
- The replace-file warning.
- Single-player only.

### Decisions for Ethan (defaults in brackets)
1. Code-sign the DLLs (about $10 a month with Azure Trusted Signing), so antivirus and Smart App Control
   block them less. [no]
2. Build ReShade chain-loading. [no: detect and explain only]
3. Let the cursor follow a resolution change without a restart. This takes two more hooks (SetCursor,
   DestroyIcon); the game's own cursors stay untouched. [no]
4. The source exists only on this disk: push it to a private GitHub repo. [ask]

### Order
1. In code, with proxytest cases: A1-A4, A6, B5, C1, C3.
2. Build.
3. In game: measure A5, B1 and B2, then fix them.
4. The C2 sweep.
5. D, then E.

Each in-game fix gets before/after captures and a LOG.md entry, and each group a commit. Uninstall at the
end.
