# Goh4kMod

Goal: Call to Arms - Gates of Hell: Ostfront (Steam, GEM2 engine, D3D11, x64) renders its 3D at native 4K
while the UI keeps its 1080p size.

Approach (decided): our own small D3D11/DXGI proxy DLL in the game's binaries\x64, DSfix-style. The game
believes it runs at 1920x1080; the proxy renders at 2x behind its back and keeps the mouse consistent.
No WineHooks, DXVK or other third-party runtime in the loop.

## Files
- PLAN.md is the plan. LOG.md is the journal, newest at the bottom: what changed, what worked, what didn't,
  and gotchas as symptom -> cause -> fix. Keep LOG.md lean: summarize old entries instead of letting it grow.
- deploy.cmd and uninstall.cmd are the only way files get into or out of the game folder.

## Containment
- Everything stays in C:\Users\bigep\Documents\Claude-Projects\Goh4kMod: src/, build/, tools/ (portable
  tools), captures/, scratch/.
- Reading the Steam install and the game folder is fine. The game's binaries\x64 folder (path in
  .claude/game-bin.txt) is the only outside place that changes, and only through deploy.cmd and
  uninstall.cmd. They back up any stock file they replace into scratch/ and record what they did in LOG.md.
  Steam's file check restores replaced files but never removes added ones.
- Never install anything system-wide: no installers, winget/choco/scoop, MSI, global pip/npm, drivers or
  services. Portable zips only, into tools/. Prefer what's already here: MSVC, the Windows SDK, dumpbin.
- No registry, PATH, environment-variable, startup or Windows settings changes.
- If a tool writes outside this folder on its own (settings in AppData, temp captures), point it back
  inside if it allows that; otherwise note it in LOG.md under "Cleanup".

## Anti-cheat
The game closes itself when its anti-cheat detects trainers or injection. Stay at the graphics-API and
window/input-API layer; never patch the exe or game memory; never try to evade or disable the anti-cheat.
Test in single-player only, and run uninstall.cmd before Ethan plays multiplayer. If the anti-cheat
reacts, stop and tell Ethan.

## Working style
- Build x64 only (start.cmd sets up MSVC).
- Prove every fix in the real game with before/after captures; nothing is "fixed" without evidence.
- Kill processes by exact PID, never by name. Never commit game files, dumps or extracted assets.
- Keep replies short. Send RenderDoc captures, shader dumps and long build or log output to the
  capture-analyst subagent.
- Ask before deploying to the game folder, driving the mouse or keyboard, or publishing.
