# Free Riders Recompiled for PS5

An unofficial port of *Sonic Free Riders* (Xbox 360) to the **PlayStation 5**,
as a homebrew title for jailbroken consoles. It is built on
[Free Riders Recompiled](https://github.com/YuutaTsubasa/Free-Riders-Recompiled)
by YuutaTsubasa, the static recompilation of the game for PC: nearly all of the
game runs on its code. Its own README is in [README.FRR.md](README.FRR.md).

> [!IMPORTANT]
> **This PS5 port was made with AI ("vibe-coded").** Nearly all of its code was
> written by Claude (Anthropic's AI model). HopeZEXAL directed it, deciding
> what it should be and do, and tested every build on a real PS5.

> [!NOTE]
> **No game code or data is included.** You need your own copy of the game
> (the USA/Europe Xbox 360 disc) as a disc image. The builder makes the PS5
> title and its data from it on your own PC.

## What you need

- A jailbroken PS5 that runs homebrew titles, with
  [ShadowMountPlus](https://github.com/search?q=ShadowMountPlus&type=repositories) and FTP access.
- A Windows PC, with about 10 GB free.
- A disc image (`.iso`) of your own *Sonic Free Riders* Xbox 360 disc.

## Installing

1. Download **FRR-PS5-Builder** from [Releases](../../releases) and unzip the
   whole folder (not inside Program Files).
2. Run `FRR-PS5-Builder.exe`. If SmartScreen warns about an unknown
   publisher, choose *More info* → *Run anyway*.
3. Pick your `.iso` and an output folder and press **Build**. It takes about
   15 to 30 minutes: it checks the disc, prepares the game data, translates
   the game's shaders, recompiles the game code and makes the title.
4. Copy the two folders it makes to the console:
   - `PPSA48220` → install it as a homebrew title (e.g. `/data/homebrew/`).
   - `frr-ps5` → `/data/frr-ps5`.
5. Start the game.

If the game files or `shaders.pack` are missing, the game says so on screen
instead of starting. Copy the builder's whole `frr-ps5` folder again.

## Playing

- Kinect motions are on the controller; prompts are shown under the game's
  pose pictures. The touchpad moves the menus' hand pointer.
- **L3 + R3** opens the Recompiled menu: achievements, and **Settings**
  (resolution up to 1440p, 30/60 fps races, volume, CPU difficulty, the Super
  Sonic mod, and more). Settings are kept in `/data/frr-ps5/settings.env`.
- The first races can hitch for a moment while the graphics pipelines are
  prepared; they are kept between starts.

### The Super Sonic mod

Off by default. Its section in Settings changes the Chaos Emeralds gear:
Sonic grinds and Super Sonic gets all three types, he transforms at 50 rings
and holds up to 200, one part slot opens, the ring drain can be tuned for
riding and for flight, the kick-dash flight can be turned off, and the Ring
Eater's shield can be hidden while Super. Mods are off online.

## Reporting problems

Open an issue **on this repository** (not on Free Riders Recompiled) with
`game.log`, `boot-trace.txt` and, if there is one, `crash.log` from
`/data/frr-ps5`, and the version from *Settings → Advanced → Version*. For
slowdowns, turn on *Detailed logging* and *Performance logging* first. For a
failed build, attach `frr-build.log` from the output folder.

## For developers

The PS5-specific code, the build scripts and the builder's source are in
[ps5/](ps5/README.md), with the dependencies and how to build. Changes to the
game's own sources are ordinary commits on top of Free Riders Recompiled.

## Credits

Free Riders Recompiled by **YuutaTsubasa** (with knuckleslee, Saiyokage and
its testers) — the game this runs. **Mihawk-99**'s PS5_Vulkan, PS5_Mesa and
PS5_PayloadSDK, built on **BlackBearReloaded**'s ps5-native-app-boilerplate —
the graphics driver and title tools. The PS5 payload SDK and SDL's PS5 port by
**John Törnblom** (ps5-payload-dev). XenonRecomp and XenosRecomp
(**hedge-dev**), Plume (**renderbag**), Mesa, SDL and every library listed in
[ps5/title/CREDITS.txt](ps5/title/CREDITS.txt), with their licences.

## Licence

GPL-3.0-or-later, as Free Riders Recompiled ([COPYING](COPYING)).

*Sonic Free Riders* is © SEGA. This project is not affiliated with or
endorsed by SEGA, Sony or Microsoft.
