# Goh4kMod

Goal: Call to Arms - Gates of Hell: Ostfront (Steam, GEM2 engine, D3D11, x64) runs at the monitor's own
resolution (1440p, 4K) while the UI keeps its 1080p size, with no game setting to change.

Approach (decided, 2.0): our own small D3D11/DXGI proxy DLL in the game's binaries\x64. The game keeps its
resolution and renders natively; the proxy only tells it the client area is native/k (k = height/1080, or width/1920 on
screens narrower than 16:9) and converts the mouse, so the UI lays out at 1080p size (NOTES.md). Keep it as unintrusive as possible:
Ethan dropped 1.0's 2x render path for that reason. No WineHooks, DXVK or other third-party runtime in
the loop.

## Files
- PLAN.md is the plan. LOG.md is the journal, newest at the bottom: what changed, what worked, what didn't,
  and gotchas as symptom -> cause -> fix. Keep LOG.md lean: summarize old entries instead of letting it grow.
- deploy.cmd and uninstall.cmd are the only way files get into or out of the game folder.

## Containment
- Everything stays in C:\Users\bigep\Documents\Claude\Goh4kMod: src/, build/, tools/ (portable
  tools), captures/, scratch/.
- Reading the Steam install and the game folder is fine. The game's binaries\x64 folder (path in
  .claude/game-bin.txt) is the only outside place that changes, and only through deploy.cmd and
  uninstall.cmd. They back up any stock file they replace into scratch/ and record what they did in LOG.md.
  Steam's file check restores replaced files but never removes added ones.
- Nothing system-wide without Ethan's OK at the prompt: installers, winget/choco/scoop, MSI, global
  pip/npm, drivers, services, and registry, PATH, environment-variable, startup or Windows settings
  changes. The guard hook asks him. Otherwise portable zips only, into tools/. Prefer what's already here:
  MSVC, the Windows SDK, dumpbin.
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
- Work autonomously: deploy and uninstall, launch the game and drive its mouse and keyboard, and make the
  PLAN.md "ask first" calls without asking; record each one in LOG.md. Ethan approves only system-wide
  changes and publishing. The guard hook prompts him for system-wide changes, git push and PRs; ask him
  before any release upload (Nexus or similar).
