#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sfr::mods {
// The game-changing options of the port's own mods (game_patches.cpp), which
// the overlay's Mods tab (achievement_overlay.cpp) switches while the game
// runs. Each is an environment switch (settings.env on the PS5): its value
// at start comes from there, and a change made on the tab is written back.
// None applies online (SFR_LIVE_HOST / SFR_LIVE_SERVER), where every player
// races by the same rules.
enum Id {
    chaos_types,       // SFR_MOD_CHAOS_TYPES: Sonic speed only, Super Sonic all three
    chaos_transform,   // SFR_MOD_CHAOS_TRANSFORM_RINGS: rings to turn Super (30 = the game's)
    chaos_ring_cap,    // SFR_MOD_CHAOS_RING_CAP: the rings a Chaos Emeralds racer holds (100)
    chaos_slots,       // SFR_MOD_CHAOS_SLOTS: gear-part slots (0 = the game's)
    chaos_hide_shield, // SFR_MOD_CHAOS_HIDE_SHIELD: Ring Eater's shield hidden while Super
    chaos_quiet_drain, // SFR_MOD_CHAOS_QUIET_DRAIN: no ring sound for Super Sonic's drain
    count
};

struct Setting {
    const char* env;
    const char* name;
    const char* description;
    std::vector<int> values;          // values[0] is the game's own behaviour
    std::vector<const char*> labels;  // one per value
};
const std::vector<Setting>& settings();

// The value in effect (0 / the game's own when online).
int value(Id id);
// The index of the value shown on the tab.
int index(Id id);
// Steps the setting by +1 / -1 (wrapping) and saves the file.
void step(Id id, int by);
// Changes so far (game_patches.cpp re-applies its table patch when it moves).
uint32_t generation();
bool online();
}
