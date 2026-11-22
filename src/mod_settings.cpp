#include "mod_settings.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

namespace sfr::mods {
namespace {
bool set(const char* name) {
    const char* v = std::getenv(name);
    return v && *v;
}

struct State {
    std::atomic<int> index[count]{};
    std::atomic<uint32_t> generation{0};
    std::mutex save_mutex;
    bool online = false;
};

State& state() {
    static State* const s = [] {
        auto* s = new State();
        s->online = set("SFR_LIVE_HOST") || set("SFR_LIVE_SERVER");
        const auto& all = settings();
        for (int i = 0; i < count; ++i) {
            const Setting& setting = all[size_t(i)];
            const char* text = std::getenv(setting.env);
            int wanted = setting.values[0];
            if (text && *text) wanted = int(std::strtol(text, nullptr, 10));
            // The shield was hidden with the types on before it had a switch
            // of its own (SFR_MOD_CHAOS_SUPER_SHIELD=1 showed it).
            if (i == chaos_hide_shield && !(text && *text)) {
                const char* types = std::getenv("SFR_MOD_CHAOS_TYPES");
                const char* shown = std::getenv("SFR_MOD_CHAOS_SUPER_SHIELD");
                wanted = types && *types == '1' && !(shown && *shown == '1') ? 1 : 0;
            }
            // The nearest listed value at or below the one asked for.
            int chosen = 0;
            for (size_t v = 0; v < setting.values.size(); ++v)
                if (setting.values[v] <= wanted) chosen = int(v);
            if (i == chaos_slots && wanted > 0 && chosen == 0) chosen = 1;
            s->index[i].store(chosen);
        }
        return s;
    }();
    return *s;
}

// settings.env: the file the PS5 reads at start (SFR_MOD_SETTINGS_FILE
// elsewhere; nothing is saved without one).
std::string settings_file() {
    if (const char* t = std::getenv("SFR_MOD_SETTINGS_FILE"); t && *t) return t;
#ifdef __PROSPERO__
    return "/data/frr-ps5/settings.env";
#else
    return {};
#endif
}

// Rewrites the mod lines of the file, keeping every other line, from a thread
// of its own (a write to /data can take a while on the PS5).
void save() {
    const std::string path = settings_file();
    if (path.empty()) return;
    std::vector<std::pair<std::string, std::string>> lines;
    const auto& all = settings();
    for (int i = 0; i < count; ++i)
        lines.emplace_back(all[size_t(i)].env, std::to_string(all[size_t(i)].values[size_t(state().index[i].load())]));
    std::thread([path, lines] {
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
        std::cerr << "MOD_SETTINGS saved path=" << path << '\n';
    }).detach();
}
}

const std::vector<Setting>& settings() {
    static const std::vector<Setting> all = {
        {"SFR_MOD_CHAOS_TYPES", "Chaos Emeralds: types",
         "Sonic can grind; Super Sonic has speed, fly and power. Applies from the next race.", {0, 1}, {"Off", "On"}},
        {"SFR_MOD_CHAOS_TRANSFORM_RINGS", "Chaos Emeralds: transform at",
         "The rings Sonic needs to turn into Super Sonic.", {30, 40, 50, 60, 75, 100},
         {"30 rings", "40 rings", "50 rings", "60 rings", "75 rings", "100 rings"}},
        {"SFR_MOD_CHAOS_RING_CAP", "Chaos Emeralds: ring limit",
         "The most rings a Chaos Emeralds rider can hold.", {100, 150, 200, 300, 500, 999},
         {"100", "150", "200", "300", "500", "999"}},
        {"SFR_MOD_CHAOS_SLOTS", "Chaos Emeralds: part slots",
         "Gear-part slots, for any part but grind, air ride and power. Applies from the menus.", {0, 1, 2},
         {"None", "1", "2"}},
        {"SFR_MOD_CHAOS_HIDE_SHIELD", "Chaos Emeralds: hide Ring Eater shield",
         "With the Ring Eater equipped, its shield is not shown while Super Sonic.", {0, 1}, {"Off", "On"}},
        {"SFR_MOD_CHAOS_QUIET_DRAIN", "Chaos Emeralds: quiet ring drain",
         "No ring sound as Super Sonic's rings drain.", {0, 1}, {"Off", "On"}},
    };
    return all;
}

bool online() { return state().online; }

int index(Id id) { return state().index[id].load(std::memory_order_relaxed); }

int value(Id id) {
    const Setting& setting = settings()[size_t(id)];
    if (online()) return setting.values[0];
    return setting.values[size_t(index(id))];
}

void step(Id id, int by) {
    const int n = int(settings()[size_t(id)].values.size());
    State& s = state();
    const int next = ((s.index[id].load() + by) % n + n) % n;
    s.index[id].store(next);
    s.generation.fetch_add(1);
    std::cerr << "MOD_SETTINGS " << settings()[size_t(id)].env << '=' << settings()[size_t(id)].values[size_t(next)] << '\n';
    save();
}

uint32_t generation() { return state().generation.load(std::memory_order_relaxed); }
}
