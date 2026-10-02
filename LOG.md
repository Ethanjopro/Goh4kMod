# Goh4kMod log

## Status
Phase 0 and Phase 1 done (2026-10-02). Next: M1 pass-through d3d11.dll proxy + deploy/uninstall, after
Ethan's go. Game folder untouched so far.

## Gotchas (symptom -> cause -> fix)
- vcvars64.bat prints "'vswhere.exe' is not recognized" -> it calls vswhere by name internally -> harmless;
  build.cmd sends vcvars' stderr to nul and still checks its exit code.
- Git Bash `sed -i 's/$/\r/'` silently leaves LF endings -> Git Bash sed strips CR -> write .cmd files with
  CRLF from PowerShell ([IO.File]::WriteAllText) or rely on .gitattributes eol=crlf.

## Game-folder changes
(none yet; deploy.cmd / uninstall.cmd append here)

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
