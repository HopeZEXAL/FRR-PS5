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
#include "launcher_settings.h"

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
#include <cerrno>
#include <fcntl.h>
#include <streambuf>

int sfr_game_main(int argc, char** argv);
extern "C" void frr_ps5_apply_schedule_current();
// For compat/ps5_thread_sampler.c (PRESENT_STALL lines).
extern "C" void frr_log_line(const char* line) { std::cerr << line; }
// The boot tracer (ps5_boot_trace.c): /data/frr-ps5/boot-trace.txt.
extern "C" void frr_boot_mark(const char* stage);
extern "C" int sceKernelDebugOutText(int channel, const char* text);

namespace {
namespace fs = std::filesystem;
const char* const root = "/data/frr-ps5";
// Every path is absolute: a title cannot count on its working directory
// (chdir into the data folder is tried, but build 1 stopped right after it).
std::string at(const char* name) { return std::string(root) + "/" + name; }

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
    for (const auto& entry : fs::directory_iterator(root, error)) {
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
    if (fs::is_directory(at("game/image"), error) && fs::is_directory(at("game/assets"), error)) return true;
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
        sfr::install_game(source, at("game"), [&](sfr::InstallStage stage, uint64_t done, uint64_t total, const std::string& item) {
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
// game.log by write(2) on its own descriptor, a line at a time. Through the
// C library's stderr the log stopped for good at the Vulkan driver's first
// message (build 7: every later line lost, the start-up marks showing the game
// ran on), so std::cerr no longer goes through a FILE.
class FdLogBuf final : public std::streambuf {
public:
    explicit FdLogBuf(int fd) : fd_(fd) {}
protected:
    int_type overflow(int_type ch) override {
        if (ch == traits_type::eof()) return traits_type::not_eof(ch);
        line().push_back(char(ch));
        if (ch == '\n') flush_line();
        return ch;
    }
    std::streamsize xsputn(const char* text, std::streamsize count) override {
        std::string& pending = line();
        for (std::streamsize i = 0; i < count; ++i) {
            pending.push_back(text[i]);
            if (text[i] == '\n') flush_line();
        }
        return count;
    }
    int sync() override { flush_line(); return 0; }
private:
    static std::string& line() { thread_local std::string pending; return pending; }
    void flush_line() {
        std::string& pending = line();
        size_t done = 0;
        while (done < pending.size()) {
            const ssize_t wrote = ::write(fd_, pending.data() + done, pending.size() - done);
            if (wrote <= 0) break;
            done += size_t(wrote);
        }
        pending.clear();
    }
    int fd_;
};
}  // namespace

int main() {
    frr_boot_mark("main (every static initialiser finished)");
    mkdir(root, 0777);
    const bool entered = chdir(root) == 0;
    const int chdir_error = entered ? 0 : errno;
    frr_boot_mark(entered ? "chdir /data/frr-ps5 ok" : "chdir /data/frr-ps5 refused (absolute paths only)");
    const int log_fd = ::open(at("game.log").c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_APPEND, 0666);
    if (log_fd < 0) {
        klog("cannot write /data/frr-ps5/game.log");
    } else {
        static FdLogBuf log_buffer(log_fd);
        std::cerr.rdbuf(&log_buffer);
        std::clog.rdbuf(&log_buffer);
        // C stderr (the Vulkan driver's messages) to the same file.
        // A title is refused dup2: C stderr then gets a file of its own.
        if (::dup2(log_fd, 2) < 0 && !std::freopen(at("driver.log").c_str(), "w", stderr))
            frr_boot_mark("no file for C stderr");
        std::setvbuf(stderr, nullptr, _IONBF, 0);
    }
    // SDL's PS5 video driver opens VideoOut, which the renderer owns: if
    // anything starts SDL video it gets the dummy driver instead.
    setenv("SDL_VIDEODRIVER", "dummy", 1);
    std::cerr << "PS5_START root=" << root << " chdir=" << (entered ? "ok" : "refused errno=" + std::to_string(chdir_error)) << '\n';
    frr_boot_mark("game.log open");

    load_settings(at("settings.env"));
    // What the launcher hands the game to play (launcher_settings.cpp): no
    // diagnostic watchdog (the CPU diagnostic stopped the game at 10 s, build
    // 9), no call budget, no traces, parallel guests, a 60 fps frame limit.
    // settings.ini holds the launcher's options, as on the desktop; any
    // NAME=VALUE in settings.env still wins.
    setenv("SFR_GRAPHICS", "vulkan", 1);
    {
        const auto settings = sfr::load_launcher_settings(at("settings.ini"));
        for (const auto& [name, value] : sfr::game_environment(settings, root))
            if (!value.empty() && name != "SFR_GRAPHICS") setenv(name.c_str(), value.c_str(), 0);
    }
    // Defaults, under the data folder (a title's own folder is read-only),
    // as absolute paths for every relative default the game has.
    setenv("SFR_SHADER_PACK", at("shaders.pack").c_str(), 0);
    setenv("SFR_SAVE_DIRECTORY", at("save").c_str(), 0);
    setenv("SFR_RUNTIME_SHADER_CACHE", at("out/shaders/runtime").c_str(), 0);
    setenv("SFR_PIPELINE_CACHE_PATH", at("pipeline-cache/vulkan.bin").c_str(), 0);
    setenv("SFR_PIPELINE_MANIFEST_LOCAL", at("pipeline-cache/vulkan.manifest").c_str(), 0);
    std::error_code made;
    fs::create_directories(at("pipeline-cache"), made);
    fs::create_directories(at("save"), made);

    frr_boot_mark("checking the installation");
    if (!ensure_installed()) {
        frr_boot_mark("nothing to play (see game.log)");
        std::cerr.flush();
        return 1;
    }
    const char* region = std::getenv("SFR_GAME_REGION");
    std::string region_argument = std::string("--game-region=") + (region && *region ? region : "ntsc-us");
    std::string image_path = at("game/image"), assets_path = at("game/assets");
    char program[] = "FreeRiders";
    char* image = image_path.data();
    char* assets = assets_path.data();
    char* arguments[] = {program, image, assets, region_argument.data(), nullptr};
    std::cerr << "PS5_GAME starting\n";
    frr_boot_mark("starting the game");
    klog("starting the game (log: /data/frr-ps5/game.log)");
    frr_ps5_apply_schedule_current();  // the main thread too (native_thread_posix.cpp)
    const int code = sfr_game_main(4, arguments);
    std::cerr << "PS5_GAME exited code=" << code << '\n';
    std::cerr.flush();
    klog("the game exited with " + std::to_string(code));
    return code;
}
