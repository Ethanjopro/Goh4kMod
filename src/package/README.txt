Goh4kMod 1.0 - sharp 4K 3D, readable UI
For Call to Arms - Gates of Hell: Ostfront (Steam, Windows)
=========================================================

At native 4K the game's interface is tiny. At 1080p it is readable but the battlefield looks soft.
This mod gives you both: the game keeps its 1080p interface size while the 3D is rendered at full 4K.

Requirements
------------
- A 3840x2160 (4K) monitor.
- A graphics card that runs the game at native 4K (performance is about the same as native 4K).

Install
-------
1. Close the game.
2. In Steam: right-click Call to Arms - Gates of Hell > Manage > Browse local files.
3. Open the folder  binaries\x64
4. Copy these 3 files from the zip into that folder:  d3d11.dll   dxgi.dll   goh4k.ini
5. Start the game. In OPTIONS > VIDEO set:
     Resolution    1920x1080
     Display mode  Fullscreen   (Windowed borderless also works)
   Click APPLY, then YES to keep the settings.
6. Restart the game once (this also gives you the full-size mouse cursor).

That's it. While the game is set to 3840x2160, the mod stays out of the way.

Uninstall - and ALWAYS before playing multiplayer
-------------------------------------------------
Delete these files from binaries\x64:  d3d11.dll   dxgi.dll   goh4k.ini   goh4k.log
Then set your resolution back in OPTIONS if you like.

The game has its own anti-cheat. The mod was tested in single-player only. Remove it before going online.

Turn it off without uninstalling
--------------------------------
Open goh4k.ini in Notepad, change  Scale=2  to  Scale=1, save, restart the game.

Troubleshooting
---------------
- Interface is tiny: the game is set to your full monitor resolution. Set it to 1920x1080.
- Nothing changed: check that the files are in binaries\x64, not the game's main folder.
- Another mod uses d3d11.dll or dxgi.dll (for example ReShade): the two can't be installed together.
- Something looks wrong after a game update: uninstall, then check for a newer version of this mod.
- goh4k.log (created next to the DLLs) records what the mod did. Include it when reporting a problem.

Other monitors (untested): set the game to exactly half your monitor resolution,
for example 1280x720 on a 2560x1440 monitor.

Credits
-------
Uses MinHook by Tsuda Kageyu (BSD 2-Clause license, see THIRD-PARTY-NOTICES.txt).
