// PlayStation 5 entry point of the game (a homebrew title). The game runs
// from /data/frr-ps5:
//   *.iso                    your Sonic Free Riders disc image: installed on
//                            the first start (src/installer.h), then no
//                            longer needed
//   game/image, game/assets  the installed game, as the launcher installs it
//   shaders.pack             the compiled shaders (scripts/pack_shaders.py)
//   settings.env             optional NAME=VALUE lines for the runtime
//   game.log                 the runtime's trace output (rewritten each start)
//   out/shaders/runtime      every shader the game made that shaders.pack
//                            lacks: its original.bin, for an offline pack
//   save/, pipeline-cache/   saves and the Vulkan pipeline cache
// A title has no command line and no environment, so this sets both up and
// calls the game's own main (diagnostic_main.cpp, compiled as sfr_game_main),
// as android_main.cpp does on Android.
#include "installer.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <system_error>

int sfr_game_main(int argc, char** argv);
extern "C" int sceKernelDebugOutText(int channel, const char* text);

namespace {
namespace fs = std::filesystem;
const char* const root = "/data/frr-ps5";

void klog(const std::string& line) {
    sceKernelDebugOutText(0, ("[frr] " + line + "\n").c_str());
}

// NAME=VALUE lines; # starts a comment. Values already set are kept.
void load_settings(const fs::path& path) {
    std::ifstream in(path);
    std::string line;
    while (std::getline(in, line)) {
        if (const auto hash = line.find('#'); hash != std::string::npos) line.erase(hash);
        const auto equals = line.find('=');
        if (equals == std::string::npos || equals == 0) continue;
        std::string name = line.substr(0, equals), value = line.substr(equals + 1);
        const auto trim = [](std::string& text) {
            text.erase(0, text.find_first_not_of(" \t\r"));
            text.erase(text.find_last_not_of(" \t\r") + 1);
        };
        trim(name);
        trim(value);
        if (!name.empty()) setenv(name.c_str(), value.c_str(), 0);
    }
}

// The first .iso in the data folder (SFR_INSTALL_SOURCE names another).
fs::path install_source() {
    if (const char* chosen = std::getenv("SFR_INSTALL_SOURCE"); chosen && *chosen) return chosen;
    std::error_code error;
    for (const auto& entry : fs::directory_iterator(".", error)) {
        std::string extension = entry.path().extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        if (entry.is_regular_file(error) && extension == ".iso") return entry.path();
    }
    return {};
}

// Installs game/ from the disc image when it is not there yet. Returns false
// when there is nothing to play.
bool ensure_installed() {
    std::error_code error;
    if (fs::is_directory("game/image", error) && fs::is_directory("game/assets", error)) return true;
    const fs::path source = install_source();
    if (source.empty()) {
        std::cerr << "PS5_INSTALL missing: copy your Sonic Free Riders disc image (.iso) to " << root << '\n';
        klog("no game installed and no .iso in /data/frr-ps5");
        return false;
    }
    const sfr::SourceSummary summary = sfr::inspect_install_source(source);
    std::cerr << "PS5_INSTALL source=" << source.string() << " problem=" << int(summary.problem)
              << " files=" << summary.file_count << " bytes=" << summary.total_bytes << " detail=" << summary.detail << '\n';
    if (summary.problem != sfr::SourceProblem::none) {
        klog("the disc image cannot be installed: " + summary.detail);
        return false;
    }
    klog("installing the game from " + source.string() + " (several minutes)");
    const auto start = std::chrono::steady_clock::now();
    int reported = -1;
    try {
        sfr::install_game(source, "game", [&](sfr::InstallStage stage, uint64_t done, uint64_t total, const std::string& item) {
            const int percent = total ? int(done * 100 / total) : 0;
            if (percent / 5 != reported / 5) {
                reported = percent;
                const char* names[] = {"checking", "copying", "decoding", "finishing"};
                std::cerr << "PS5_INSTALL " << names[int(stage)] << ' ' << percent << "% " << item << '\n';
                klog("install " + std::string(names[int(stage)]) + " " + std::to_string(percent) + "%");
            }
            return true;
        });
    } catch (const std::exception& e) {
        std::cerr << "PS5_INSTALL failed: " << e.what() << '\n';
        klog(std::string("install failed: ") + e.what());
        return false;
    }
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    std::cerr << "PS5_INSTALL done seconds=" << seconds << " (the .iso can be deleted now)\n";
    klog("installed; the .iso can be deleted now");
    return true;
}
}  // namespace

int main() {
    mkdir(root, 0777);
    if (chdir(root) != 0) {
        klog("cannot enter /data/frr-ps5");
        return 1;
    }
    if (!std::freopen("game.log", "w", stderr)) klog("cannot write /data/frr-ps5/game.log");
    std::setvbuf(stderr, nullptr, _IOLBF, 1 << 16);  // whole lines, so a crash keeps the trace
    std::cerr << "PS5_START root=" << root << '\n';

    load_settings("settings.env");
    // Defaults, under the data folder (a title's own folder is read-only).
    setenv("SFR_SHADER_PACK", "shaders.pack", 0);
    setenv("SFR_SAVE_DIRECTORY", "save", 0);

    if (!ensure_installed()) {
        std::cerr.flush();
        return 1;
    }
    const char* region = std::getenv("SFR_GAME_REGION");
    std::string region_argument = std::string("--game-region=") + (region && *region ? region : "ntsc-us");
    char program[] = "FreeRiders", image[] = "game/image", assets[] = "game/assets";
    char* arguments[] = {program, image, assets, region_argument.data(), nullptr};
    std::cerr << "PS5_GAME starting\n";
    klog("starting the game (log: /data/frr-ps5/game.log)");
    const int code = sfr_game_main(4, arguments);
    std::cerr << "PS5_GAME exited code=" << code << '\n';
    std::cerr.flush();
    klog("the game exited with " + std::to_string(code));
    return code;
}
