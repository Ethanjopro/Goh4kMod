# Goh4kMod: how it works and how to fix it after a game update

These notes are for whoever maintains the mod, including a future Claude session. PLAN.md is the plan
and LOG.md is the day-by-day journal with evidence. This file is the condensed knowledge: what the mod
does, why, where things are, and a playbook for when a game update breaks it.

## What it does, in one paragraph (2.0)

The player keeps the game at the monitor's own resolution. Our `d3d11.dll` and `dxgi.dll` sit in the
game's `binaries\x64` and wrap the game's D3D11 device, contexts and swapchain, but rendering stays
native: no texture, swapchain or display mode is changed. Ten user32 functions are hooked with MinHook so
the game sees its client area, and the mouse, divided by k (2 at 4K, 4/3 at 1440p): real height / 1080 on
16:9 and wider screens, real width / 1920 on narrower ones, so the logical screen is never smaller than
1920x1080 (2560x1600 -> 1920x1200; 1920x1200 isn't scaled). Its UI therefore lays out at its 1080p size and
is drawn magnified onto the native screen. Three
full-screen viewport rules fix what the mismatch between client size and render size breaks (point 2).
Window position, monitor and display-mode calls are not hooked, so Options, the mode list and
options.set never see a change. Only calls made by `call_to_arms.exe` itself are converted. Only a client
area covering its whole monitor (fullscreen, borderless) is scaled; a smaller window, and anything at 1080p
and below, is left exactly as the stock game has it. The game's exe and memory are never modified.

## History: what was learned (most important first)

1. **The game takes its sizes from three places.** Render targets follow its resolution setting (the
   saved `mode`); its UI layout and some post-process viewports follow the client size it is told
   (WM_SIZE, GetClientRect); its "screen" viewport comes from the backbuffer desc. Native scaling works
   because only the second is changed.
2. **The three full-screen rules** (context.cpp; `fix nouivp on` disables them, for A/B). "Real screen"
   is the swapchain's buffer size, which windowed modes can make differ from the window:
   1. UI label surfaces (render targets smaller than the screen) are drawn with the stale real-screen
      viewport: applied as the logical screen, as at native 1080p. Otherwise text comes out 2x and
      clipped.
   2. Tone-map and glow passes set the logical-screen viewport on screen-sized targets: applied as the real
      screen. Otherwise the 3D fills only the top-left part. Stock native 4K never does this.
   3. The blurred backdrop behind prompts is a screen-sized texture the UI draws as an image. Each popup's
      backdrop element uses texture coordinates 0..1 in its first frame and logical / texture size (0..0.5
      at 4K) afterwards; at native 1080p both mean the whole image, with the mod the later ones show only
      its top-left quarter, magnified. Fix: those later draws (recognised by the UI's vertex shader, learnt
      from rule 1's draws and matched by bytecode id, and by a per-image count of draws per frame) get a
      stand-in texture k times the image's size with the image copied 1:1 into its top-left. The image
      itself is never changed: an earlier fix that shrank it into its top-left showed it small in the
      top-left corner on a second popup's first frame. The dev build reads back each such draw's
      coordinates and logs `ui check ... DISAGREES` when the choice was wrong.
   Partial scissors aren't converted: 114 traces (menus, missions, Conquest, tech tree, scrolled Options)
   never enabled scissoring.
3. **Coordinate math is rational** (ratio.h, checked exhaustively by proxytest): Down = floor, Up = ceil,
   so Down(Up(v)) == v. Client points scale around 0,0; screen points (cursor, MSG.pt, wheel) scale around
   the client's real screen origin, so the window's position reads the same through converted and
   unconverted calls (window rect, monitor info and WM_MOVE stay real). ClipCursor uses Up on every edge.
   The game never called ClipCursor or SetCursorPos in any mode tested, camera rotation included.
4. **The factor follows the real client size, never the buffer size.** After a mode change the buffers
   stay stale until the game has seen a WM_SIZE that matches its new mode; following the buffer gave a
   black screen. After the game's own SetFullscreenState/ResizeTarget the mod posts a WM_SIZE resync: when
   the window size doesn't change (borderless -> fullscreen at the desktop size) Windows sends none, and the
   game then lays its UI out from its mode size (UI at native size plus a stale copy in the top-left).
5. **Windowed mode is not scaled.** In a window smaller than its monitor the game takes the client size as
   its resolution and saves it (`mode`, `custom_mode` in options.set): with the logical size reported, a
   2560x1440 window got saved as 1920x1080. So only a client covering its whole monitor (fullscreen
   exclusive, borderless at the desktop size) is scaled. Borderless and fullscreen save only the chosen
   mode.
6. **HBAO+** (GFSDK_SSAO_D3D11.win64.dll 3.1, the game's SSAO) sizes everything from the depth buffer and
   sets viewports that match its own targets, so the rules never change them. 2.0 at first exempted its
   calls by return address, which also exempted the game's viewport when HBAO+ restored it; an A/B with
   and without the exemption showed no difference (captures/v2/hb-heats.png), so it was removed.
7. **Cursor:** the game builds its cursors with CreateIconIndirect at 32x32. The hook builds them at k with
   one box filter (exact nearest neighbour for whole factors, smooth for fractional ones: 43x43 at 1440p).
8. **The anti-cheat** (Digitalmindsoft's own, scans process and module names) never reacted in more than 30
   single-player sessions.
9. **1.x (dropped in 2.0):** the game was set to 1920x1080 and the proxy rendered every screen-sized
   target at 2x, with a backbuffer wrapper, viewport/scissor/copy-box scaling and constant-buffer
   patches. It needed a 4K monitor and a manual resolution step. Ethan chose native scaling only for
   2.0, to keep the mod as unintrusive as possible. The 1.0 source is commit e338dd9; the last state with
   both paths is in scratch/src-hybrid-2026-10-05 (not in git).
10. **Rejected: lying about the display mode list** (2026-10-04). It removed native 4K from Options and
   rewrote the saved resolution. Ethan's rule: the mod must not take away or change any game option.
   Facts from it: the Options list comes from DXGI (EnumAdapters/EnumOutputs/GetDisplayModeList), the game
   hides modes under about 1360 wide, and a saved mode missing from the list makes it rewrite
   `{mode desktop}`.
11. **No built-in hi-DPI text** (research 2026-10-05): fonts are fixed-size BMFont pages picked by name and
    size; `gui.scale_index` is an options key the fonts never use. Sharper text would need 2x text
    surfaces plus font pages rebuilt at runtime (LOG.md).
12. **2.1 sturdiness** (2026-10-05, PLAN.md 2.1): the mod is all or nothing and checks itself.
    - Hooks: if any of them can't be made, none stays and the mod is off for the session. Every 2 s the
      game window's Present checks each hook's first bytes: a hook another program patched over is logged
      (with how its next calls arrive, counted for up to 500 calls or a minute); one whose patch was removed
      switches the mod off (never half on).
    - Window procedure: if ours drops out of the chain (top = the one we wrapped, or the class's own), it is
      put back and the game resynced. WM_NCDESTROY unsubclasses and sets k = 1; the next swapchain's window
      is followed. A queued resync counts for 2 s only, so a lost one can't block later ones.
    - Rule 3 reads pixel-shader slot 0 back from the context (D3D11 unbinds a view by itself when its texture
      becomes a render target), and its stand-in is shared per size and format and released 5 s after the
      last popup draw (it holds the device, so it also goes with the swapchain).
    - Logs: game build and the build this version was verified with, user32 coordinate functions the exe
      imports that the mod doesn't convert, a health line, a session summary at unload (with a warning if
      rule 1 never applied in a long scaled session), and a 4 MB cap.
13. **The Steam overlay patches over 5 of our hooks** (GetCursorPos, SetCursorPos, ClipCursor, GetMessageW,
    PeekMessageW; it hooks after us). Its GetCursorPos passes the game's calls on with a jump (538 of 551
    reach us with the game's return address: converted). Its GetMessageW calls us from its own code (game ->
    overlay -> overlay -> us), so with the overlay on, the mouse coordinates in the game's messages stay
    real. Harmless: the game reads the mouse with GetCursorPos (clicks, drag box and minimap were right in
    every session, all with the overlay loaded). Deliberately not "fixed" by accepting calls through another
    module: the overlay's own window would then get logical coordinates. Clicking inside the overlay works.
14. **mouse_event:** the game imports it and calls it every 10 s with a relative +1/-1 px move (a keep-awake
    nudge, dev-build logging). Relative moves need no conversion.
15. **Borderless renders at the desktop size** whatever Resolution says (buffers 3840x2160 with Resolution
    2560x1440 on the 4K desktop), so buffers never differ from the window there. The stand-in size is
    buffer² / logical anyway, which is k times the image whenever they match.

## What the mod relies on in the game (check these after an update)

| The game... | If an update changes it | What the mod does | How it shows |
|---|---|---|---|
| lays its UI out from the client size it is told (WM_SIZE, GetClientRect) | UI at native size while the mouse is divided by k: clicks land wrong | nothing automatic | menu captures differ; session summary: rule 1 never applied |
| reads the mouse with GetCursorPos (and ScreenToClient/ClientToScreen/MapWindowPoints, MSG) | a new function (GetCursorInfo, PeekMessageA, GetMessagePos...) sees real pixels | logs a WARNING at start | `game now imports ...` in goh4k.log |
| keeps one window and its own window procedure | handled | follows a new window; puts its procedure back | `destroyed` / `subclassed again` log lines |
| draws UI text into label surfaces with the real-screen viewport (rule 1) | text 2x and cut off, or the rule unused | nothing automatic | text crops; session summary warning |
| sets the logical viewport on screen-sized targets only by mistake (rule 2; stock 4K never sets 1920x1080 on 3840x2160) | a new half-res pass in the top-left of a full target would be stretched | nothing automatic | preset-sweep traces: a new rule-2 pixel shader |
| draws popup backdrops with 0..1 in an element's first frame and logical / texture size after (rule 3) | wrong backdrop for some frames | dev build logs `ui check ... DISAGREES` | update check burst + dev log |
| draws all UI with one vertex shader, learnt from rule-1 draws | rule 3 never fires: backdrops magnified | — | no `recognised as the UI's` line; summary shows 0 UI shaders |
| makes its cursors with CreateIconIndirect | cursor stays 32 px | — | `drive cursor` |
| creates its device with D3D11CreateDeviceAndSwapChain and its swapchain with IDXGIFactory::CreateSwapChain, from call_to_arms.exe | the mod does nothing (safe) | — | no `health:` line |
| imports only D3D11CreateDeviceAndSwapChain and CreateDXGIFactory from d3d11/dxgi | an export our stubs lack: the game won't start | — | playbook step 2 |
| saves the client size as its resolution in a window smaller than its monitor | (why such windows aren't scaled) | — | — |

`game: built ...` at the top of goh4k.log says whether this is the build the mod version was verified with
(`kVerifiedGameStamp` in util.cpp; bump it after verifying a new game build).

## Where things are

| Area | Files |
|---|---|
| Export forwarding (ml64 jump stubs) | `src/scripts/gen-exports.ps1` -> `src/proxy/gen/*`, `src/dxgi/gen/*` |
| COM forwarders (generated from SDK headers) | `src/scripts/gen-wrappers.ps1` -> `src/*/gen/fwd_*.h` |
| Device/context/swapchain wrappers | `src/proxy/device.cpp`, `context.cpp`, `swapchain.cpp`, `wrappers.h` |
| Scaling factor and state | `src/proxy/scale.*`, `src/proxy/ratio.h` |
| Full-screen viewport rules | `src/proxy/context.cpp` (UiTargetsChanged, UiApplyViewports, UiBeforeDraw) |
| Mouse/window/cursor hooks | `src/proxy/input.*` (MinHook in `src/third_party/minhook`) |
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
    checks: a bordered 2560x1440 window passes through, a borderless monitor-sized one is scaled.
  - `powershell -File src\scripts\test-gamefolder.ps1`: deploy/uninstall against a fake game folder.
  - `powershell -ExecutionPolicy Bypass -File src\scripts\update-check.ps1` (the game closed, hands off the
    mouse, about 7 minutes, 4K Fullscreen): backs up the profile, deploys the dev build, drives menu, Exit
    prompt (frame burst), Options and a new mission, diffs the snapshots against
    captures\update-check\ref and checks the dev log's lines, then restores the profile and the previous
    deployment and prints PASS/FAIL (history in scratch\update-check-history.txt). `-MakeReference` stores a
    run's snapshots as references. The session briefing says when call_to_arms.exe has changed since the
    last PASS.
- `deploy.cmd` installs the dev build; `deploy.cmd -Release` installs the newest packaged release.
  `uninstall.cmd /nopause` removes everything and checks the folder against
  `scratch/baseline-binaries-x64.txt`. After a game update, retake that baseline (Phase 1 step) or the
  check will report the updated game files.
- To bump the version, change it in `src/*/version.rc`, `$Version` in `package.ps1`, the log line in
  `util.cpp`, and the README title.

## Playbook: the game updated and something broke

0. **Run the update check** (src\scripts\update-check.ps1, above). Its FAIL lines say where to start; its
   snapshots are in captures\update-check\<time>. Then check the table "What the mod relies on" against
   what changed. When everything is right again, bump `kVerifiedGameStamp` (util.cpp) to the new exe's link
   time stamp (`game: built ... stamp` in the log) and retake the references with `-MakeReference`.
1. **Does it still load?** Read `goh4k.log` next to the DLLs (release build) or `scratch/logs/` (dev
   build):
   - `game: built ...` (is this the verified build?), "input hooks: 10 of 10", "health: input hooks 10 (5
     patched over by another program), game window followed, swapchain wrapped; client 3840x2160 (covers its
     monitor), buffers 3840x2160, k 2/1", "ui: vertex shader ... recognised as the UI's", the notes
     `ui-label-viewport` and `ui-screen-viewport` (first mission or prompt), "ui: popup stand-in 7680x4320"
     (first prompt) and the `session:` summary at unload mean the core works. `WARNING` lines say what
     broke. Dev build: no `ui check ... DISAGREES` lines except one per resolution switch.
   - `input hook X: patched over by Y` is another program (the Steam overlay: 5 hooks, expected) and the
     line after up to 500 calls or a minute says whether the game's calls still arrive directly.
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
5. **Another DLL starts drawing through our device** (new NVIDIA library, overlay) and a rule misfires on
   it: exempt its calls by return address (module range, the way 2.0 first did for HBAO+; see git
   history of context.cpp).
6. **Driving the game for tests:**
   - `build\drive\drive.exe` (click, move, drag, mdrag, mrotate, key, wheel, focus, fg, cursor, snap, burst)
     and
     `imgtool.exe`.
   - Click coordinates are the 1080p UI position x k (x2 at 4K, x4/3 at 1440p), plus the client origin in a
     window. A window smaller than its monitor isn't scaled, so the UI there is at its native size.
   - The Options dialog stays open after APPLY/YES. A click aimed at the main menu then lands on RESET TO
     DEFAULT (answer NO). The keep-settings confirm times out after 12 s: click YES in the same command.
   - Paths at 4K: SINGLEPLAYER 1280,1304 > CAMPAIGN 1072,388 > SELECT CAMPAIGN 1072,580 > USSR 2548,896 >
     ZA RODINU 1072,388 > ABSOLUTE ZERO 1068,344 (Easy is the default). Skip the intros with Esc/space;
     CONTINUE 3680,76. Leave a mission: Esc > EXIT 1916,1316 > YES 2056,1194. Top bar: OPTIONS 2556,48,
     EXIT 2876,48, YES 2056,1194. Main menu row: OPTIONS 2556,1304, EXIT 2876,1304.
   - In Options the Resolution row needs one click to select and one on its arrow to open the list.
   - Back up the profile's saves first (autosave is on), and use new campaigns or skirmishes only.

## Known limits

- Tested on one 4K monitor (RTX 5090, Windows 11, HDR desktop, 150% display scaling): fullscreen
  3840x2160, 2560x1440, 2560x1600 (16:10, k 4/3, logical 1920x1200) and 1920x1200 (not scaled), borderless,
  windowed 2560x1440; presets Minimum, Medium, Maximum and Ethan's custom settings. Not on a real 1440p, 5K,
  ultrawide or multi-monitor setup, nor on AMD or Intel GPUs (the mod uses only core D3D11 calls).
- 8K (k = 4): the popup stand-in would be over D3D11's 16384 px limit, so popup backdrops stay magnified
  (logged once).
- With the Steam overlay on, the mouse coordinates inside the game's window messages aren't converted
  (History 13). Harmless while the game reads the mouse with GetCursorPos.
- Proton / Steam Deck: the mod is inert by default (Proton's own d3d11/dxgi are used).
- Windowed mode (a window smaller than its monitor) isn't scaled: the UI is its stock native size there.
- Options' read-only "Current mode" line shows the logical size (1920x1080 at 4K).
- UI text and icons are the 1080p art magnified, as in 1.0. 1-pixel UI lines draw one real pixel wide.
- After changing resolution in Options, the cursor keeps its old size until the game creates a new one
  (restart once).
- A single left-click on the minimap doesn't move the camera, same as the stock game.
- A small 1382x824 window flashes for a split second when quitting or when the game loses focus: the
  game's own SetFullscreenState(0) (DXGI restores its startup window size). Same without the mod.
- During a resolution switch one frame can show a popup backdrop slightly magnified (the game draws it
  once with the old coordinates while the monitor re-syncs). The same single frame can follow an applied
  graphics change that makes the game recreate its swapchain (an MSAA change): the new backdrop texture has
  no draw history yet. Both sit inside the game's own mode-switch flash.
- After an in-game graphics change the game sizes many full-screen passes from the client size (about 1100
  rule-2 draws per frame instead of 15 after a fresh start); rule 2 restores them all, and smaller targets
  keep their own viewports (preset sweep, LOG.md 2026-10-05).
- It conflicts with other mods installed as d3d11.dll or dxgi.dll (ReShade, DXVK).
- Multiplayer is untested by design. Users must uninstall before going online.
