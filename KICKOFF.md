Plan only for now. CLAUDE.md has the rules and the decided approach; follow them while planning too.

Reference reading only (don't install or run these):
- Steam thread "New 4K-Native UI Upscaling Solution": proof that the 2x trick works in this game (via
  WineHooks HD mode), and its known problem spots: SSAO, soft particles, Conquest unit cards and tech tree.
- The diff of WineHooks PR #7 (github.com/Daniel-Lobo/WineHooks/pull/7, mainly MouseHooks.cpp): how they
  keep the 2x factor fixed, clamp mouse coordinates and scale the cursor.

Phase 0: check the git setup, create LOG.md, confirm cl.exe targets x64.

Phase 1, read-only outside this folder: find the game through Steam's library folders; write its
binaries\x64 path, with forward slashes, to .claude/game-bin.txt; use dumpbin to confirm it's x64 and
D3D11 and to see which DLLs it loads from its own folder, then pick the proxy (dxgi.dll or d3d11.dll);
find its settings file and back it up to scratch/.

Then plan the rest as small milestones, each with a done-check:
1. A pass-through proxy that loads and changes nothing, plus deploy.cmd and uninstall.cmd.
2. Frame capture built into the proxy: save the real backbuffer as a PNG in captures/ on a hotkey or when
   a request file appears, so results can be checked without extra tools.
3. 2x rendering on the main menu with a 1080p-sized UI and a working mouse.
4. In a mission, find and fix broken passes one at a time (SSAO, soft particles, unit cards), each proven
   with captures (RenderDoc portable in tools/ when needed).
5. Alt-tab, borderless and cursor size, then a clean install and uninstall.
If milestone 3 stalls after a couple of sessions, stop and lay out options instead of pushing on.

Also list every step that needs me (deploys, getting into a mission, publishing) and how you'll keep
token use down.

When I approve: save the plan as PLAN.md, do Phase 0 and Phase 1 only, then stop with a short report.
