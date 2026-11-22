# Free Riders Recompiled for PS5

A port of [Free Riders Recompiled](https://github.com/YuutaTsubasa/Free-Riders-Recompiled)
(by YuutaTsubasa) to the PlayStation 5, as a homebrew title for a jailbroken
console. Everything in this repository outside `ps5/` is Free Riders
Recompiled, with the port's changes as ordinary commits on top; this folder
holds what only the PS5 needs.

**Made with AI.** This port was "vibe-coded": nearly all of its code was
written by Claude (Anthropic's AI model), and HopeZEXAL directed it, deciding
what it should be and do, and tested every build on a real PS5.

**No game code or data is in this repository or its releases.** Players run
the builder on a Windows PC with their own disc image: it recompiles the game
code, prepares the game data and translates the shaders, on their PC.

Players: download the builder from Releases and follow its README.

## What is here

| Folder | What it is |
| --- | --- |
| `compat/` | PS5 start-up and C library gaps: early settings, the boot trace and crash log, the stall watchdog, the build version |
| `title/` | Building the PS5 runtime and title: `compile_one.sh` (one source with the game's flags), `compile_game_main.sh`, `link_game.sh`, `paths.sh`, the title's `param.json`, icon, background, README and CREDITS |
| `builder/app/` | The builder itself (Python, runs on the player's PC): `build.py` (the steps), `gui.py` (the window) and its scripts |
| `builder/host/` | The builder's own programs, cross-built for Windows with llvm-mingw: `frr_prep` (disc checks, install, unpacking the game's compressed files), `frr_shaders` (finds and translates the shaders with the game's own translation code), the launcher |
| `builder/windows/` | Files that go at the top of the Windows builder package |
| `patches/` | Changes to Plume (presenting through `VK_KHR_display`) and to the PS5 port of SDL 2 (the touchpad) |
| `art/` | Sources of the icon and background (original art) |

PS5-specific code inside the game's sources: `src/ps5_main.cpp`,
`src/ps5_status_screen.*`, `src/game_settings.*`, the settings tab in
`src/achievement_overlay.cpp`, and `__PROSPERO__` sections elsewhere.

## Dependencies

| Project | Revision | Use |
| --- | --- | --- |
| [PS5_Vulkan](https://github.com/mihawk-99/PS5_Vulkan) (Mihawk-99) | `7f492b4c` | RADV for the PS5, `prospero-clang18`, the title converter and signer, `libc.prx` |
| [PS5_Mesa](https://github.com/mihawk-99/PS5_Mesa) (Mihawk-99) | `1ba9874f` | the driver PS5_Vulkan builds |
| [PS5_PayloadSDK](https://github.com/mihawk-99/PS5_PayloadSDK) (Mihawk-99, on ps5-payload-dev's SDK) | `b83202be` | toolchain, headers, platform layer |
| [Plume](https://github.com/renderbag/plume) | `11926860` + Free Riders Recompiled's patches + `patches/plume-*` | rendering |
| [SDL 2 for the PS5](https://github.com/ps5-payload-dev/SDL) | `ee4c47dc` + `patches/sdl-ps5-*` | controller and audio |
| [dxc-bin](https://github.com/renderbag/dxc-bin) | `737ac9f5` | the game's embedded shaders; shipped in the builder for translating shaders |
| LLVM 18 (Windows release), Python 3.12 ([python-build-standalone](https://github.com/astral-sh/python-build-standalone)), [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) | | the builder package |

## Building

1. Set up PS5_Vulkan (its README) with the revisions above, then build SDL
   for the PS5 with `patches/sdl-ps5-*` into `$OPT/sdl2-ps5`, and apply
   `patches/plume-*` to Plume.
2. Make the game's embedded shaders into `$SHADERS` (dxc on
   `src/shaders/{blit,model,overlay}.hlsl`, DXIL and SPIR-V headers, as the
   main CMakeLists.txt's `sfr_embed_shader` does).
3. Compile every runtime source with `title/compile_one.sh` into
   `title/obj/` (`src/diagnostic_main.cpp` with `compile_game_main.sh`), and
   `src/ps5_*.cpp`, the installer sources and `compat/*.c` into
   `title/inst-obj/`. `title/paths.sh` says where everything is looked for.
4. The builder's runtime is those objects in one archive
   (`libfrr_runtime.a`), with SDL, RADV, the payload SDK's libraries and the
   link recipe beside it; `builder/app/build.py` shows the layout it expects
   and does the link the way `title/link_game.sh` does.
5. Cross-build `builder/host/*` for Windows with llvm-mingw
   (`host/mingw.cmake` for XenonRecomp; the others are a few files each,
   against Free Riders Recompiled's installer and shader sources).

The game code itself is never built here for release: the builder makes it
on each player's PC from their own disc.

## Licence

GPL-3.0-or-later, as Free Riders Recompiled. Every project this builds on,
and its licence: `title/CREDITS.txt`.
