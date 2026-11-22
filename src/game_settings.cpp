#include "game_settings.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

namespace sfr::game_settings {
namespace {
bool set(const char* name) {
    const char* v = std::getenv(name);
    return v && *v;
}

struct State {
    std::atomic<int> index[count]{};
    int at_start[count]{};
    std::atomic<uint32_t> generation{0};
    std::mutex save_mutex;
    bool online = false;
};

int initial_index(const Setting& setting) {
    const char* text = std::getenv(setting.env[0]);
    // The shield was hidden with the types on before it had a switch of its
    // own (SFR_MOD_CHAOS_SUPER_SHIELD=1 showed it).
    if (setting.id == chaos_hide_shield && !(text && *text)) {
        const char* types = std::getenv("SFR_MOD_CHAOS_TYPES");
        const char* shown = std::getenv("SFR_MOD_CHAOS_SUPER_SHIELD");
        return types && *types == '1' && !(shown && *shown == '1') ? 1 : 0;
    }
    if (!text || !*text) return setting.fallback;
    for (size_t v = 0; v < setting.values.size(); ++v)
        if (std::strcmp(setting.values[v], text) == 0) return int(v);
    // A number between the listed ones: the nearest listed one below it.
    char* end = nullptr;
    const long wanted = std::strtol(text, &end, 10);
    if (end == text) return setting.fallback;
    int chosen = -1;
    for (size_t v = 0; v < setting.values.size(); ++v) {
        char* value_end = nullptr;
        const long value = std::strtol(setting.values[v], &value_end, 10);
        if (value_end != setting.values[v] && value <= wanted) chosen = int(v);
    }
    return chosen >= 0 ? chosen : setting.fallback;
}

State& state() {
    static State* const s = [] {
        auto* s = new State();
        s->online = set("SFR_LIVE_HOST") || set("SFR_LIVE_SERVER");
        for (const Setting& setting : settings()) {
            const int i = initial_index(setting);
            s->index[setting.id].store(i);
            s->at_start[setting.id] = i;
        }
        return s;
    }();
    return *s;
}

// settings.env: the file the PS5 reads at start (SFR_SETTINGS_FILE
// elsewhere; nothing is saved without one).
std::string settings_file() {
    if (const char* t = std::getenv("SFR_SETTINGS_FILE"); t && *t) return t;
#ifdef __PROSPERO__
    return "/data/frr-ps5/settings.env";
#else
    return {};
#endif
}

using Lines = std::vector<std::pair<std::string, std::string>>;
Lines current_lines() {
    Lines lines;
    for (const Setting& setting : settings())
        for (const char* env : setting.env) lines.emplace_back(env, setting.values[size_t(state().index[setting.id].load())]);
    return lines;
}

// Rewrites the settings' lines of the file, keeping every other line.
void write_file(const std::string& path, const Lines& lines) {
    std::lock_guard lock(state().save_mutex);
    std::vector<std::string> kept;
    {
        std::ifstream in(path, std::ios::binary);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            const auto equals = line.find('=');
            const std::string key = equals == std::string::npos ? line : line.substr(0, equals);
            const bool ours = std::any_of(lines.begin(), lines.end(), [&](const auto& l) { return l.first == key; }) ||
                              key == "SFR_MOD_CHAOS_SUPER_SHIELD";
            if (!ours) kept.push_back(line);
        }
    }
    while (!kept.empty() && kept.back().empty()) kept.pop_back();
    std::ostringstream text;
    for (const auto& line : kept) text << line << '\n';
    for (const auto& [key, value] : lines) text << key << '=' << value << '\n';
    std::ofstream(path, std::ios::binary | std::ios::trunc) << text.str();
    std::cerr << "GAME_SETTINGS saved path=" << path << '\n';
}

// From a thread of its own: a write to /data can take a while on the PS5.
void save() {
    const std::string path = settings_file();
    if (path.empty()) return;
    std::thread([path, lines = current_lines()] { write_file(path, lines); }).detach();
}
}

const std::vector<Setting>& settings() {
    static const std::vector<Setting> all = {
        {resolution, "Display", {"SFR_RENDER_SCALE"}, "Resolution",
         "How sharp the game is drawn. 1440p is drawn larger and shrunk to fit the screen.", {"100", "150", "200"},
         {"720p", "1080p (Default)", "1440p"}, 1, Applies::restart, false},
        {frame_limit, "Display", {"SFR_RACE_FPS"}, "Race frame rate",
         "30 draws every other frame of a race, which still runs at full speed. Menus stay at 60.", {"60", "30"},
         {"60 (Default)", "30"}, 0, Applies::now, false},
        {race_frameskip, "Display", {"SFR_RACE_FRAMESKIP"}, "Race frame skip",
         "When a race falls behind, skip drawing a frame to keep its speed.", {"1", "0"}, {"Auto (Default)", "Off"}, 0,
         Applies::restart, false},
        {frame_counter, "Display", {"SFR_FPS_OVERLAY"}, "Frame counter", "Frames a second, in a corner of the screen.",
         {"0", "1"}, {"Off (Default)", "On"}, 0, Applies::now, false},
        {volume, "Audio", {"SFR_VOLUME"}, "Volume", "The game's overall volume.",
         {"0", "10", "20", "30", "40", "50", "60", "70", "80", "90", "100"},
         {"0%", "10%", "20%", "30%", "40%", "50%", "60%", "70%", "80%", "90%", "100% (Default)"}, 10, Applies::restart, false},
        {skip_movies, "Audio", {"SFR_SKIP_MOVIES"}, "Skip movies", "Leave out the game's movies.", {"0", "1"},
         {"Off (Default)", "On"}, 0, Applies::restart, false},
        {skip_logos, "Audio", {"SFR_SKIP_LOGOS"}, "Skip intro logos", "Start at the title screen.", {"0", "1"},
         {"Off (Default)", "On"}, 0, Applies::restart, false},
        {cpu_difficulty, "Gameplay", {"SFR_CPU_DIFFICULTY"}, "CPU difficulty", "How hard the computer racers try.",
         {"easy", "normal", "hard"}, {"Easy", "Normal (Default)", "Hard"}, 1, Applies::next_race, false},
        // Hidden (no section) until it reaches the ride: build 48 ended the
        // "On your Gear!" preparation early and the ride still played.
        {skip_warmup, "", {"SFR_SKIP_RACE_WARMUP"}, "Skip race warm-up",
         "Leave out the ride after Omochao through the cones before each race.", {"0", "1"}, {"Off", "On"}, 0,
         Applies::next_race, false},
        {chaos_types, "Super Sonic", {"SFR_MOD_CHAOS_TYPES"}, "Types",
         "Sonic can grind; Super Sonic has speed, fly and power.", {"0", "1"}, {"Default", "On"}, 0, Applies::next_race,
         true},
        {chaos_transform, "Super Sonic", {"SFR_MOD_CHAOS_TRANSFORM_RINGS"}, "Transform at",
         "The rings Sonic needs to turn into Super Sonic.", {"30", "40", "50", "60", "75", "100"},
         {"Default (30)", "40 rings", "50 rings", "60 rings", "75 rings", "100 rings"}, 0, Applies::now, true},
        {chaos_ring_cap, "Super Sonic", {"SFR_MOD_CHAOS_RING_CAP"}, "Ring limit",
         "The most rings the Chaos Emeralds' rider can hold.", {"100", "150", "200", "300", "500", "999"},
         {"Default (100)", "150", "200", "300", "500", "999"}, 0, Applies::now, true},
        {chaos_slots, "Super Sonic", {"SFR_MOD_CHAOS_SLOTS"}, "Part slots",
         "Gear-part slots on the Chaos Emeralds, for any part but grind, air ride and power.", {"0", "1", "2"},
         {"Default (None)", "1", "2"}, 0, Applies::menus, true},
        {chaos_hide_shield, "Super Sonic", {"SFR_MOD_CHAOS_HIDE_SHIELD"}, "Hide Ring Eater shield",
         "With the Ring Eater equipped, its shield is not shown while Super Sonic.", {"0", "1"}, {"Default", "On"}, 0,
         Applies::now, true},
        {chaos_quiet_drain, "Super Sonic", {"SFR_MOD_CHAOS_QUIET_DRAIN"}, "Quiet ring drain",
         "No ring sound as Super Sonic's rings drain.", {"0", "1"}, {"Default", "On"}, 0, Applies::now, true},
        {no_flight, "Super Sonic", {"SFR_SUPER_SONIC_STAY_ON_BOARD"}, "Kick dash flight",
         "Off keeps Super Sonic on his board: the kick dash is an ordinary one.", {"0", "1"}, {"Default (On)", "Off"}, 0,
         Applies::now, true},
        {drain_riding, "Super Sonic", {"SFR_MOD_CHAOS_DRAIN_RIDING"}, "Ring drain: riding",
         "How fast Super Sonic loses rings riding his board.", {"0", "3000", "2000", "1000", "500", "250"},
         {"Default (None)", "1 per 3 s", "1 per 2 s", "1 per 1 s", "2 per 1 s", "4 per 1 s"}, 0, Applies::now, true},
        {drain_flight, "Super Sonic", {"SFR_MOD_CHAOS_DRAIN_FLIGHT"}, "Ring drain: flight",
         "How fast Super Sonic loses rings while flying after a kick dash.", {"game", "off", "1000", "500", "250", "100"},
         {"Default (3 per 1 s)", "None", "1 per 1 s", "2 per 1 s", "4 per 1 s", "10 per 1 s"}, 0, Applies::now, true},
        {hit_loss, "Super Sonic", {"SFR_MOD_CHAOS_HIT_LOSS"}, "Rings lost when hit",
         "How many rings Super Sonic loses to a hit or an attack.", {"game", "0", "25", "50", "75", "100"},
         {"Default", "None", "25", "50", "75", "100"}, 0, Applies::now, true},
        {touchpad_pointer, "Controls", {"SFR_TOUCHPAD_POINTER"}, "Touchpad pointer",
         "The touchpad moves the menus' hand cursor.", {"0", "1"}, {"Off", "On (Default)"}, 1, Applies::now, false},
        {button_prompts, "Controls", {"SFR_BUTTON_PROMPTS"}, "Button prompts",
         "Show the button to press under the game's pose pictures.", {"0", "1"}, {"Off", "On (Default)"}, 1, Applies::restart,
         false},
        {notifications, "Advanced", {"SFR_ACHIEVEMENT_NOTIFICATIONS"}, "Achievement notifications",
         "A notification when an achievement unlocks.", {"0", "1"}, {"Off", "On (Default)"}, 1, Applies::restart, false},
        {performance_log, "Advanced", {"SFR_FRAME_METRICS", "SFR_GPU_TIMING"}, "Performance logging",
         "Frame timings in game.log, for finding slowdowns (it grows large).", {"0", "1"}, {"Off (Default)", "On"}, 0,
         Applies::restart, false},
        {stall_watchdog, "Advanced", {"SFR_STALL_WATCHDOG"}, "Stall watchdog",
         "Notes in game.log where the game was when a frame is very late.", {"0", "1"}, {"Off", "On (Default)"}, 1,
         Applies::now, false},
        {menu_hint, "Advanced", {"SFR_MENU_HINT"}, "Menu hint",
         "On the title screen, a note that L3 + R3 opens this menu.", {"0", "1"}, {"Off", "On (Default)"}, 1,
         Applies::now, false},
    };
    return all;
}

const std::vector<const char*>& sections() {
    static const std::vector<const char*> all = {"Display", "Audio", "Gameplay", "Super Sonic", "Controls", "Advanced"};
    return all;
}

std::vector<Id> in_section(const char* section) {
    std::vector<Id> out;
    for (const Setting& setting : settings())
        if (std::strcmp(setting.section, section) == 0) out.push_back(setting.id);
    return out;
}

bool online() { return state().online; }

int index(Id id) { return state().index[id].load(std::memory_order_relaxed); }

const char* text(Id id) {
    const Setting& setting = settings()[size_t(id)];
    if (setting.offline_only && online()) return setting.values[0];
    return setting.values[size_t(index(id))];
}

int value(Id id) { return int(std::strtol(text(id), nullptr, 10)); }

void step(Id id, int by) {
    const Setting& setting = settings()[size_t(id)];
    const int n = int(setting.values.size());
    State& s = state();
    const int next = ((s.index[id].load() + by) % n + n) % n;
    s.index[id].store(next);
    s.generation.fetch_add(1);
    std::cerr << "GAME_SETTINGS " << setting.env[0] << '=' << setting.values[size_t(next)] << '\n';
    save();
}

bool restart_pending() {
    for (const Setting& setting : settings())
        if (setting.applies == Applies::restart && state().index[setting.id].load() != state().at_start[setting.id])
            return true;
    return false;
}

void save_now() {
    const std::string path = settings_file();
    if (!path.empty()) write_file(path, current_lines());
}

uint32_t generation() { return state().generation.load(std::memory_order_relaxed); }
}

extern "C" int frr_setting_value(int id) {
    return sfr::game_settings::value(sfr::game_settings::Id(id));
}

extern "C" int frr_stall_watchdog_enabled() {
    return sfr::game_settings::value(sfr::game_settings::stall_watchdog) != 0;
}
