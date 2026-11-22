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
#include <condition_variable>
#include <mutex>
#include <thread>
#include "installer.h"
#include "launcher_settings.h"
#include "ps5_status_screen.h"

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
#include <cstring>
#include <cctype>

int sfr_game_main(int argc, char** argv);
extern "C" void frr_ps5_apply_schedule_current();
extern "C" const char frr_ps5_version[];
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

bool installed() {
    std::error_code error;
    return fs::is_directory(at("game/image"), error) && fs::is_directory(at("game/assets"), error);
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
    // Lines are queued and written by a thread of their own: a write to
    // /data sometimes took 150-170 ms (build 29's PRESENT_STALL samples: the
    // main thread inside this write), and every thread that logged waited for
    // it. Past 16 MiB queued, lines are dropped and counted (LOG_DROPPED).
    void flush_line() {
        std::string& pending = line();
        if (pending.empty()) return;
        if (!keep(pending)) { pending.clear(); return; }
        {
            std::lock_guard lock(queue_mutex_);
            if (queued_.size() + pending.size() > (16u << 20)) ++dropped_;
            else queued_.append(pending);
            if (!writer_started_) {
                writer_started_ = true;
                std::thread([this] { write_loop(); }).detach();
            }
        }
        queue_ready_.notify_one();
        pending.clear();
    }
    // Off (the default), game.log keeps the start-up lines, the settings,
    // the patches and anything that reads as an error; the per-frame traces
    // (hundreds of thousands of lines a session) only with Detailed logging
    // (SFR_VERBOSE_LOG=1, the settings page) or performance logging.
    static bool keep(const std::string& text) {
        static const bool verbose = [] {
            for (const char* name : {"SFR_VERBOSE_LOG", "SFR_FRAME_METRICS"})
                if (const char* v = std::getenv(name); v && *v && *v != '0') return true;
            return false;
        }();
        if (verbose) return true;
        static const char* const prefixes[] = {"PS5_", "GAME_", "OVERLAY_", "LOG_", "CRASH", "PRESENT_STALL",
                                               "NATIVE_GRAPHICS", "NATIVE_PRESENTATION", "NATIVE_DEVICE",
                                               "ACHIEVEMENT", "Free Riders", "Diagnostic"};
        for (const char* prefix : prefixes)
            if (text.compare(0, std::strlen(prefix), prefix) == 0) return true;
        if (text.find("missing_from_pack") != std::string::npos) return true;
        // The per-frame and per-file traces: their counters ("failed=0",
        // "errors=") would otherwise match the words below.
        static const char* const noisy[] = {"NATIVE_", "RESULT ", "IMPORT ", "REQUEST ", "FILE_", "NUI_",
                                            "RUNTIME_SHADER", "TEXTURE_", "PIPELINE_", "RACE_", "AUDIO_"};
        for (const char* prefix : noisy)
            if (text.compare(0, std::strlen(prefix), prefix) == 0) return false;
        std::string lower(text.size(), 0);
        std::transform(text.begin(), text.end(), lower.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        for (const char* word : {"error", "fail", "missing", "crash", "exception", "abort", "assert", "fatal", "warning"})
            if (lower.find(word) != std::string::npos) return true;
        return false;
    }
    void write_loop() {
        std::string writing;
        for (;;) {
            uint64_t dropped = 0;
            {
                std::unique_lock lock(queue_mutex_);
                queue_ready_.wait(lock, [&] { return !queued_.empty() || dropped_; });
                writing.swap(queued_);
                dropped = std::exchange(dropped_, 0);
            }
            if (dropped) writing += "LOG_DROPPED lines=" + std::to_string(dropped) + "\n";
            size_t done = 0;
            while (done < writing.size()) {
                const ssize_t wrote = ::write(fd_, writing.data() + done, writing.size() - done);
                if (wrote <= 0) break;
                done += size_t(wrote);
            }
            writing.clear();
        }
    }
    int fd_;
    std::mutex queue_mutex_;
    std::condition_variable queue_ready_;
    std::string queued_;
    uint64_t dropped_ = 0;
    bool writer_started_ = false;
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
    std::cerr << "PS5_START version=\"" << frr_ps5_version << "\" root=" << root << " chdir=" << (entered ? "ok" : "refused errno=" + std::to_string(chdir_error)) << '\n';
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
    // SFR_TEST_STATUS_SCREEN=1: the "not found" screen, to check it on an installed console.
    if (const char* t = std::getenv("SFR_TEST_STATUS_SCREEN"); t && *t == '1') {
        sfr::Ps5StatusScreen screen;
        screen.set("Game files not found (test)",
                   "This is the screen shown when /data/frr-ps5 has no game files or no shaders.pack. Remove "
                   "SFR_TEST_STATUS_SCREEN from settings.env to play.",
                   "Close the game from the PS button.");
        screen.run_forever();
    }
    // The game data and shaders.pack come from the builder on a PC (the PS5
    // cannot translate shaders); without them, a screen says what to copy.
    {
        std::error_code error;
        const bool game = installed(), shaders = fs::is_regular_file(at("shaders.pack"), error);
        if (!game || !shaders) {
            frr_boot_mark("game files missing: the status screen");
            std::cerr << "PS5_MISSING game=" << game << " shaders=" << shaders << " root=" << root << '\n';
            sfr::Ps5StatusScreen screen;
            screen.set(game ? "shaders.pack not found" : "Game files not found",
                       std::string("Run the Free Riders Recompiled for PS5 Builder on your PC with your own Sonic Free "
                                   "Riders (Xbox 360) disc image. Copy the whole frr-ps5 folder it makes to "
                                   "/data/frr-ps5 on this console") +
                           (game ? " (it holds shaders.pack, which is missing here)" : "") +
                           ", then start the game again.",
                       "Close the game from the PS button. Details are in /data/frr-ps5/game.log.");
            screen.run_forever();
        }
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
