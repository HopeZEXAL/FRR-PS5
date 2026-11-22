// frr_prep: the PC side of Free Riders Recompiled for PS5's builder.
//   frr_prep check   DISC                 is this the supported disc? (exit 0 yes)
//   frr_prep xex     DISC OUT.xex         copies default.xex out of the disc
//   frr_prep install DISC GAME_DIR        the console's game/ folder (assets, decoded image)
// DISC is a .iso or a folder with the disc's files. Progress lines on stdout:
//   PROGRESS <stage> <percent>
#include "installer.h"
#include "xex_patcher.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>
#include <cstring>
#include <map>
#include <algorithm>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace fs = std::filesystem;

static int check(const fs::path& source) {
    const sfr::SourceSummary s = sfr::inspect_install_source(source);
    std::cout << "SOURCE problem=" << int(s.problem) << " files=" << s.file_count << " bytes=" << s.total_bytes
              << " detail=" << s.detail << std::endl;
    return s.problem == sfr::SourceProblem::none ? 0 : 3;
}

static int extract_xex(const fs::path& source, const fs::path& out) {
    std::vector<uint8_t> bytes;
    if (fs::is_directory(source)) {
        std::ifstream in(source / "default.xex", std::ios::binary);
        if (!in) { std::cerr << "no default.xex in " << source << '\n'; return 3; }
        bytes.assign(std::istreambuf_iterator<char>(in), {});
    } else {
        sfr::DiscImage disc(source);
        const sfr::DiscEntry* entry = disc.find("default.xex");
        if (!entry) { std::cerr << "no default.xex on the disc\n"; return 3; }
        bytes = disc.read_file(*entry);
    }
    fs::create_directories(out.parent_path().empty() ? fs::path(".") : out.parent_path());
    std::ofstream o(out, std::ios::binary);
    o.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
    if (!o) { std::cerr << "cannot write " << out << '\n'; return 4; }
    std::cout << "XEX " << out.string() << ' ' << bytes.size() << std::endl;
    return 0;
}

// The game's compressed files ("XCTD", 824D3B10 and the LZX decoder 824E2D10):
//   +0  0x0FF512ED, +4 version 1, +8 checksum,
//   +12 flags: bits 0-3 the LZX window (0x8000 << n), bits 4-5 the largest
//       block (0x8000 << n), bits 6-21 the block count,
//       bits 22-23 the size width (0: 20 bits, else 32);
//   +16 the blocks' uncompressed sizes, bit-packed MSB first, padded to 32 bits;
//   then each block: [BE16 0][raw bytes], or LZX frames [BE16 size][frame],
//   one per 32 KB of output, the decoder reset for each block.
// Returns false (with why) when the file does not follow this layout.
static bool unpack_xctd(const std::vector<uint8_t>& in, std::vector<uint8_t>& out, std::string& why) {
    auto be32 = [&](size_t at) { return uint32_t(in[at]) << 24 | uint32_t(in[at + 1]) << 16 | uint32_t(in[at + 2]) << 8 | in[at + 3]; };
    if (in.size() < 20 || be32(0) != 0x0FF512EDu) { why = "not compressed"; return false; }
    const uint32_t flags = be32(12);
    const uint32_t window = 0x8000u << (flags & 0xF);
    if ((flags & 0xF) > 6) { why = "unsupported window"; return false; }
    const uint32_t count = (flags >> 6) & 0xFFFF;
    const uint32_t width = ((flags >> 22) & 3) ? 32 : 20;
    const size_t table_bytes = size_t((uint64_t(width) * count + 31) / 32) * 4;
    size_t at = 16 + table_bytes;
    if (at > in.size()) { why = "block table past the end"; return false; }
    auto entry = [&](uint32_t i) -> uint32_t {
        const uint64_t bit = uint64_t(width) * i;
        uint32_t value = 0;
        for (uint32_t b = 0; b < width; ++b) {
            const uint64_t k = bit + b;
            value = value << 1 | ((in[16 + k / 8] >> (7 - k % 8)) & 1);
        }
        return value;
    };
    out.clear();
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t size = entry(i);
        if (at + 2 > in.size()) { why = "block " + std::to_string(i) + " past the end"; return false; }
        const size_t first = size_t(in[at]) << 8 | in[at + 1];
        if (first == 0) {  // stored
            if (at + 2 + size > in.size()) { why = "stored block past the end"; return false; }
            out.insert(out.end(), in.begin() + std::ptrdiff_t(at + 2), in.begin() + std::ptrdiff_t(at + 2 + size));
            at += 2 + size;
            continue;
        }
        std::vector<uint8_t> frames;
        for (uint32_t made = 0; made < size; made += std::min<uint32_t>(0x8000, size - made)) {
            if (at + 2 > in.size()) { why = "frame past the end"; return false; }
            const size_t length = size_t(in[at]) << 8 | in[at + 1];
            if (!length || at + 2 + length > in.size()) { why = "bad frame size"; return false; }
            frames.insert(frames.end(), in.begin() + std::ptrdiff_t(at + 2), in.begin() + std::ptrdiff_t(at + 2 + length));
            at += 2 + length;
        }
        const size_t base = out.size();
        out.resize(base + size);
        if (lzxDecompress(frames.data(), frames.size(), out.data() + base, size, window, nullptr, 0) != 0) {
            why = "LZX error in block " + std::to_string(i);
            return false;
        }
    }
    if (count == 0) return true;  // an empty file (its header padded)
    if (at != in.size()) { why = "ends at " + std::to_string(at) + " of " + std::to_string(in.size()); return false; }
    return true;
}

// frr_prep unpack GAME_DIR OUT_DIR: every compressed file of the installed
// game, unpacked to the same path under OUT_DIR (for finding the shaders).
static int unpack_all(const fs::path& game, const fs::path& out_dir) {
    size_t compressed = 0, unpacked = 0;
    uint64_t bytes = 0;
    std::map<std::string, int> failures;
    std::vector<fs::path> files;
    for (const auto& e : fs::recursive_directory_iterator(game))
        if (e.is_regular_file()) files.push_back(e.path());
    size_t done = 0;
    for (const auto& path : files) {
        ++done;
        std::ifstream f(path, std::ios::binary);
        char magic[4] = {};
        f.read(magic, 4);
        if (!f || std::memcmp(magic, "\x0F\xF5\x12\xED", 4) != 0) continue;
        ++compressed;
        f.seekg(0);
        std::vector<uint8_t> in((std::istreambuf_iterator<char>(f)), {}), out;
        std::string why;
        if (!unpack_xctd(in, out, why)) {
            if (failures[why]++ < 3) std::cerr << "UNPACK_FAILED " << fs::relative(path, game).generic_string() << ": " << why << '\n';
            continue;
        }
        const fs::path target = out_dir / fs::relative(path, game);
        fs::create_directories(target.parent_path());
        std::ofstream o(target, std::ios::binary);
        o.write(reinterpret_cast<const char*>(out.data()), std::streamsize(out.size()));
        ++unpacked;
        bytes += out.size();
        std::cout << "PROGRESS " << done << ' ' << files.size() << std::endl;
    }
    std::cout << "UNPACKED " << unpacked << " of " << compressed << " compressed files, " << bytes << " bytes" << std::endl;
    for (const auto& [why, n] : failures) std::cout << "  failed (" << n << "): " << why << std::endl;
    return unpacked == compressed ? 0 : 5;
}

static int install(const fs::path& source, const fs::path& game) {
    const char* names[] = {"checking", "copying", "decoding", "finishing"};
    int last = -1;
    sfr::install_game(source, game, [&](sfr::InstallStage stage, uint64_t done, uint64_t total, const std::string&) {
        const int percent = total ? int(done * 100 / total) : 0;
        const int key = int(stage) * 1000 + percent;
        if (key != last) {
            last = key;
            std::cout << "PROGRESS " << names[int(stage)] << ' ' << percent << std::endl;
        }
        return true;
    });
    std::cout << "INSTALLED " << game.string() << std::endl;
    return 0;
}

int main(int argc, char** argv) {
    try {
        // The arguments as paths: on Windows from the wide command line, so any
        // name (a user folder in any language) arrives intact.
        std::vector<fs::path> arg;
#ifdef _WIN32
        int count = 0;
        wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count);
        for (int i = 0; i < count; ++i) arg.emplace_back(wide[i]);
        LocalFree(wide);
#else
        for (int i = 0; i < argc; ++i) arg.emplace_back(argv[i]);
#endif
        argc = int(arg.size());
        const std::string command = argc > 1 ? arg[1].string() : "";
        if (command == "check" && argc == 3) return check(arg[2]);
        if (command == "xex" && argc == 4) return extract_xex(arg[2], arg[3]);
        if (command == "install" && argc == 4) return install(arg[2], arg[3]);
        if (command == "unpack" && argc == 4) return unpack_all(arg[2], arg[3]);
        std::cerr << "usage: frr_prep check DISC | xex DISC OUT.xex | install DISC GAME_DIR | unpack GAME_DIR OUT_DIR\n";
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "ERROR " << e.what() << '\n';
        return 1;
    }
}
