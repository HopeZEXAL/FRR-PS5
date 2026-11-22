#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sfr::game_settings {
// The port's settings page (the Settings tab of the L3 + R3 page,
// achievement_overlay.cpp). Each setting is one or more environment
// switches (settings.env on the PS5): its value at start comes from there,
// and a change made on the page is written back. Some apply at once, some
// from the next race, some only after a restart (the page says which).
enum Id {
    // Display
    resolution,        // SFR_RENDER_SCALE (percent of 720 lines)
    frame_limit,       // SFR_RACE_FPS: 60, or 30 (a race drawn every other frame)
    race_frameskip,    // SFR_RACE_FRAMESKIP
    frame_counter,     // SFR_FPS_OVERLAY
    // Audio
    volume,            // SFR_VOLUME
    skip_movies,       // SFR_SKIP_MOVIES
    skip_logos,        // SFR_SKIP_LOGOS
    // Gameplay
    cpu_difficulty,    // SFR_CPU_DIFFICULTY
    // Super Sonic (the Chaos Emeralds gear; off online)
    chaos_types,       // SFR_MOD_CHAOS_TYPES: Sonic speed only, Super Sonic all three
    chaos_transform,   // SFR_MOD_CHAOS_TRANSFORM_RINGS: rings to turn Super (30 = the game's)
    chaos_ring_cap,    // SFR_MOD_CHAOS_RING_CAP: the rings a Chaos Emeralds racer holds (100)
    chaos_slots,       // SFR_MOD_CHAOS_SLOTS: gear-part slots (0 = the game's)
    chaos_hide_shield, // SFR_MOD_CHAOS_HIDE_SHIELD: Ring Eater's shield hidden while Super
    chaos_quiet_drain, // SFR_MOD_CHAOS_QUIET_DRAIN: no ring sound for Super Sonic's drain
    // Controls
    touchpad_pointer,  // SFR_TOUCHPAD_POINTER
    button_prompts,    // SFR_BUTTON_PROMPTS
    // Advanced
    notifications,     // SFR_ACHIEVEMENT_NOTIFICATIONS
    performance_log,   // SFR_FRAME_METRICS and SFR_GPU_TIMING
    stall_watchdog,    // SFR_STALL_WATCHDOG
    count
};

enum class Applies { now, next_race, menus, restart };

struct Setting {
    Id id;
    const char* section;
    std::vector<const char*> env;     // every switch it sets (the first is read)
    const char* name;
    const char* description;
    std::vector<const char*> values;  // as written to the switches
    std::vector<const char*> labels;  // one per value
    int fallback;                     // the index when the switch is unset or unknown
    Applies applies;
    bool offline_only;                // the game's own behaviour online (values[0])
};
const std::vector<Setting>& settings();
// The sections in order, and the settings of one.
const std::vector<const char*>& sections();
std::vector<Id> in_section(const char* section);

// The value in effect, as the switch's text and as a number.
const char* text(Id id);
int value(Id id);
// The index of the value shown on the page.
int index(Id id);
// Steps the setting by +1 / -1 (wrapping) and saves the file.
void step(Id id, int by);
// Whether a setting that applies after a restart was changed since start.
bool restart_pending();
// Writes the file now (before closing the game).
void save_now();
// Changes so far (game_patches.cpp re-applies its table patch when it moves).
uint32_t generation();
bool online();
}

extern "C" int frr_setting_value(int id);
