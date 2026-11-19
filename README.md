# Goh4kMod

Play **Call to Arms - Gates of Hell: Ostfront** at your monitor's full resolution (1440p, 4K) with the
menus and HUD at their 1080p size. Nothing to set in the game.

Above 1080p the game's interface shrinks: at 4K it is half the size it was meant to be. Goh4kMod is a
small D3D11/DXGI proxy (`d3d11.dll` + `dxgi.dll`) that lets the game render natively at your resolution
while telling it the window is 1080p-sized (native / k, with k = height / 1080, or width / 1920 on screens
narrower than 16:9) and converting the mouse to match. The UI lays out at its 1080p size; the 3D scene is
rendered at full resolution with the game's own settings and performance. No game files or settings are
changed.

> **Single-player only.** The game has its own anti-cheat and the mod was tested in single-player only.
> Uninstall (delete the two DLLs) before playing multiplayer.

## Install

Requires the Steam version on Windows and a monitor above 1080p (tested at 3840x2160 and 2560x1440).

1. Close the game.
2. In Steam, right-click the game > Manage > Browse local files, and open `binaries\x64`.
3. Copy `d3d11.dll` and `dxgi.dll` from the release zip into that folder.
4. Start the game and play at your monitor's resolution (OPTIONS > VIDEO > Resolution). Fullscreen and
   Windowed borderless are supported; in a smaller "Windowed" window the mod stays off.

To uninstall, delete `d3d11.dll` and `dxgi.dll` from `binaries\x64`.

More detail (what OPTIONS > VIDEO shows, compatibility) is in
[src/package/README.txt](src/package/README.txt), the readme shipped in the release zip.
Not compatible with ReShade, DXVK or other mods that use `d3d11.dll` or `dxgi.dll`.

## Building from source

Needs Visual Studio (or Build Tools) with the C++ x64 tools and the Windows SDK. `build.cmd` finds and sets
up MSVC itself, so it works from any shell.

```
build.cmd release    :: shareable DLLs + build\release\Goh4kMod-<version>.zip
build.cmd            :: development build (proxy, dxgi, drive tool, proxytest)
build.cmd tests      :: build\proxytest.exe, the local smoke test
```

Development builds read their settings from [config/goh4k.ini](config/goh4k.ini).
`deploy.cmd` / `uninstall.cmd` copy a build into and out of the game's `binaries\x64` (backing up any stock
file they replace); they read the folder path from `.claude/game-bin.txt`, a one-line local file you create.

## Layout

| Path | What |
| --- | --- |
| `src/proxy/` | `d3d11.dll`: wraps the device, contexts and swapchain; UI scaling and mouse conversion |
| `src/dxgi/` | `dxgi.dll`: wraps the game's DXGI factory and hands swapchain creation to `d3d11.dll` |
| `src/drive/` | Dev tools: input driving and screenshots (`drive.exe`), image crop/diff (`imgtool.exe`) |
| `src/tests/` | `proxytest.cpp`, the smoke test |
| `src/scripts/` | Deploy, uninstall, packaging and code-generation scripts |
| `src/third_party/minhook/` | MinHook (vendored) |
| `PLAN.md`, `LOG.md`, `NOTES.md` | Development plan, journal and maintainer notes |
| `CLAUDE.md`, `.claude/` | Instructions and hooks for Claude Code, which was used to develop the mod |

## Security

See [SECURITY.md](SECURITY.md) for how to report a vulnerability and where to get safe builds.

## License

Goh4kMod is licensed under the [GNU General Public License v3.0](LICENSE).
It includes [MinHook](https://github.com/TsudaKageyu/minhook) by Tsuda Kageyu, under the BSD 2-Clause
license ([src/third_party/minhook/LICENSE.txt](src/third_party/minhook/LICENSE.txt)).

Goh4kMod is a fan-made mod, not affiliated with or endorsed by the game's developers or publisher.
