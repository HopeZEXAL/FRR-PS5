#include "video_mode.h"
#include <cmath>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <SDL.h>
#endif

namespace sfr {
DisplayMode query_native_display_mode() {
#ifdef _WIN32
    DEVMODEW native{};
    native.dmSize = sizeof(native);
    if (!EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &native))
        throw RuntimeStop("native-display", 0, "cannot query the current native display mode");
    constexpr DWORD required = DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY | DM_DISPLAYFLAGS;
    if ((native.dmFields & required) != required)
        throw RuntimeStop("native-display", 0, "native display mode is missing required fields");
    const DisplayMode mode{native.dmPelsWidth, native.dmPelsHeight,
                           static_cast<float>(native.dmDisplayFrequency),
                           (native.dmDisplayFlags & DM_INTERLACED) != 0};
    if (!mode.width || mode.width > 16384 || !mode.height || mode.height > 16384 ||
        !std::isfinite(mode.refresh_hz) || mode.refresh_hz <= 1.0f || mode.refresh_hz > 1000.0f ||
        mode.interlaced)
        throw RuntimeStop("native-display", 0, "native display mode is invalid or unsupported");
    return mode;
#elif defined(__PROSPERO__)
    // SDL's PS5 video driver must not start (it opens VideoOut, which the
    // renderer owns): the console's output is 1080p at 60 Hz, the mode the
    // renderer's display surface picks.
    return {1920, 1080, 60.0f, false};
#else
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
        throw RuntimeStop("native-display", 0, "cannot open the SDL video subsystem");
    SDL_DisplayMode native{};
    const bool queried = SDL_GetCurrentDisplayMode(0, &native) == 0;
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    if (!queried || native.w <= 0 || native.w > 16384 || native.h <= 0 || native.h > 16384)
        throw RuntimeStop("native-display", 0, "native display mode is invalid or unsupported");
    // Some compositors (WSLg among them) report no refresh rate.
    return {uint32_t(native.w), uint32_t(native.h), native.refresh_rate > 1 ? float(native.refresh_rate) : 60.0f, false};
#endif
}
}
