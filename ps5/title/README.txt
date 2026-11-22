Free Riders Recompiled for PS5
==============================

A PlayStation 5 homebrew port of Free Riders Recompiled, the PC recompilation
of Sonic Free Riders (Xbox 360). It plays your own copy of the game: no game
data is included, and you need a disc image of your own disc.

This is a fan project. It is not made, endorsed or supported by SEGA, Sony
or Microsoft. Sonic Free Riders is (c) SEGA.

What you need
-------------
- A jailbroken PS5 that runs homebrew titles, with ShadowMountPlus.
- A disc image (.iso) of your own Sonic Free Riders Xbox 360 disc.
- Free space on the console for the installed game (about the size of the .iso).

Installing
----------
Use the Free Riders Recompiled for PS5 Builder on a Windows PC: pick your
disc image and press Build. It makes two folders:
  PPSA48220   install it as a homebrew title
  frr-ps5     copy it to /data/frr-ps5 on the console (the game's data and
              shaders.pack, its shaders translated for the PS5)
Then start the game.

If /data/frr-ps5 has no game files or no shaders.pack, the game says so on
screen instead of starting: copy the builder's whole frr-ps5 folder over.

Playing
-------
- Kinect motions are replaced by the controller; button prompts are shown
  under the game's pose pictures.
- The touchpad moves the menus' hand pointer.
- L3 + R3 opens the Recompiled menu: your achievements, and the Settings tab
  (resolution, frame rate, volume, the Super Sonic mod and more).
  Settings are saved to /data/frr-ps5/settings.env. Those marked "Restart"
  apply after the game is started again (Y closes it for you).
- Settings > Advanced > Reset to defaults puts everything back.

The Super Sonic mod
-------------------
Off by default (every option shows "Default"). The Super Sonic section of
the settings changes the Chaos Emeralds gear: types for Sonic and Super
Sonic, the rings needed to transform, the ring limit, a part slot, the ring
drain and the kick-dash flight. Mods are off when playing online.

Files in /data/frr-ps5
----------------------
  shaders.pack     the game's shaders, translated (made by the builder)
  game/            the installed game
  save/            your saves and achievements
  settings.env     your settings
  game.log         the log of the last start
  boot-trace.txt   start-up steps, and a crash's details
  crash.log        written only if the game crashes
  pipeline-cache/  compiled shaders, kept between starts (the first races
                   can hitch while it fills)

Reporting a problem
-------------------
Attach game.log, boot-trace.txt and crash.log (if there is one), and say
which build you are on (Settings > Advanced > Version). For slowdowns, turn
on Settings > Advanced > Detailed logging and Performance logging first.

Licences
--------
Free Riders Recompiled is free software under the GNU General Public
License, version 3 (licenses/GPL-3.0.txt), and so is this port. The other
components and their licences are listed in CREDITS.txt.
