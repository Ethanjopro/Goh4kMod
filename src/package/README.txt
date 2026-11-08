Goh4kMod 2.1.4
Call to Arms - Gates of Hell: Ostfront: play at your monitor's full resolution, interface at its 1080p size.
For monitors above 1080p (1440p, 4K).

The game renders at your Resolution. OPTIONS > VIDEO "Current mode 1920x1080" is the interface size the
mod gives the game, not the render size.

NEW IN 2.1.4: when one popup replaced another (for example "Please wait" after creating a Conquest), the
screen behind it could show as a small copy in the top-left corner with black around it, and stay so
while the game loaded. Fixed.
NEW IN 2.1.3: a safety check keeps the picture at your monitor's full resolution if the game ever asks
for its buffers at the interface size.
NEW IN 2.1.2: the game's own screenshots (PrintScreen) are saved at full resolution again (they were
1920x1080 with 2.1.1, which made the game look blurrier than it was).


INSTALL
-------
1. Close the game.
2. In Steam, right-click Call to Arms - Gates of Hell: Ostfront > Manage > Browse local files.
3. Open the folder  binaries\x64
4. Copy these 2 files from the zip into that folder:
     d3d11.dll
     dxgi.dll
5. Start the game. There is nothing to set: play at your monitor's own resolution
   (OPTIONS > VIDEO > Resolution, for example 2560x1440 or 3840x2160).
   Display mode Fullscreen or Windowed borderless. In a smaller "Windowed" window the mod stays off and
   the interface is the game's normal size.
   Coming from version 1.0? It needed Resolution 1920x1080. Set your monitor's resolution again.


UNINSTALL
---------
1. Close the game.
2. In binaries\x64, delete  d3d11.dll  and  dxgi.dll
The game's settings stay as they are; the interface just goes back to its normal size.
