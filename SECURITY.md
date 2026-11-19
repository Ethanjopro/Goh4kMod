# Security Policy

Goh4kMod is two DLLs (`d3d11.dll`, `dxgi.dll`) that the game loads from its own `binaries\x64` folder, so
they run with the game's permissions inside its process. Please treat security reports accordingly.

## Supported versions

Only the latest release gets fixes. Older versions should be replaced with the latest one.

| Version | Supported |
| --- | --- |
| 2.1.x (latest) | Yes |
| < 2.1 | No |

## Reporting a vulnerability

Please **do not open a public issue** for a security problem.

Report it privately through GitHub: open the **Security** tab of this repository and choose
**Report a vulnerability**. Include the mod version, what you found, and steps to reproduce if you have
them. You should get a reply within a week; once a fix is released the report can be made public, with
credit if you want it.

In scope, for example:
- The DLLs loading code from an unexpected location (they load the real Windows `d3d11.dll` / `dxgi.dll` by
  full path from the System32 folder).
- Memory-safety bugs in the proxy that a crafted file or input could trigger, such as its `goh4k.ini`
  settings file.
- Release builds leaking local information (paths, user names) or writing files outside the game folder.

Not security issues (please use normal issues): the game crashing or misrendering with the mod, and
compatibility with other mods or overlays.

## Getting safe builds

Any `d3d11.dll` or `dxgi.dll` you put in the game folder runs code on your PC. Only download Goh4kMod from
this repository's Releases page or its official Nexus Mods page, or build it yourself from this source.
Do not use copies re-uploaded elsewhere.

## Anti-cheat

Goh4kMod stays at the graphics-API and window/input layer: it does not patch the game executable or game
memory, and it does not try to evade or disable the game's anti-cheat. It is tested in single-player only;
remove it before playing multiplayer.
