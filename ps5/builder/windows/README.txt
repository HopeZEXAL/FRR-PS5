Free Riders Recompiled for PS5 - Builder (Windows)
==================================================

Makes the PS5 homebrew title and its game data from your own Sonic Free
Riders (Xbox 360) disc image. Nothing from the game is in this builder: the
game code is recompiled on your PC from your disc, and the game data is
copied from your disc.

1. Unzip this whole folder somewhere with about 10 GB free (not inside
   Program Files). Keep the folders beside FRR-PS5-Builder.exe.
2. Run FRR-PS5-Builder.exe. If Windows SmartScreen warns about an unknown
   publisher, choose "More info" then "Run anyway".
3. Pick your disc image (.iso) and an output folder, and press Build.
   The first build takes about 15 to 30 minutes, depending on your PC.
4. When it finishes, the output folder has:
     PPSA48220   install it as a homebrew title (e.g. copy it to
                 /data/homebrew/ on the console for ShadowMountPlus)
     frr-ps5     copy it to /data/frr-ps5 on the console
   Then start the game. In the game, L3 + R3 opens the Recompiled menu.

If a build stops, frr-build.log in the output folder says why; attach it
when reporting a problem.

Only the Xbox 360 release of Sonic Free Riders is supported (the builder
checks your disc's default.xex).

This PS5 port was made with AI ("vibe-coded"): nearly all of its code was
written by Claude (Anthropic's AI model), directed and tested on a real PS5
by HopeZEXAL.

Free Riders Recompiled for PS5 is a port of Free Riders Recompiled by
YuutaTsubasa (https://github.com/YuutaTsubasa/Free-Riders-Recompiled) to the
PS5, using PS5_Vulkan by Mihawk-99 and the PS5 payload SDK by John Törnblom.
Everyone whose work is in it, and their licences: CREDITS.txt and licenses/.
Free Riders Recompiled and this port are GPL v3.
