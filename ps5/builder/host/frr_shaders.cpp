// frr_shaders: makes shaders.pack (the game's shaders, translated for Vulkan)
// from the installed game, ahead of time, with the game's own translation code
// (runtime_shader_cache.cpp): the PS5 cannot translate shaders itself.
//   frr_shaders WORK_DIR DIR [DIR...]
// DIRs: the installed game/ (image/ and assets/) and its unpacked compressed
// files (frr_prep unpack). Every Xbox 360 shader container found is translated into
// WORK_DIR/runtime (the runtime cache layout scripts/pack_shaders.py packs).
// The tools come from the environment, as the game reads them:
//   SFR_SHADER_TRANSLATOR, SFR_DXC_SPIRV, SFR_SHADER_COMMON.
// Progress on stdout: "SHADERS found N", "PROGRESS done total".
#include "runtime_shader_cache.h"
#include "native_graphics.h"
#include "guest_memory.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

namespace fs = std::filesystem;

namespace {
uint32_t be32(const std::vector<uint8_t>& d, size_t at) {
    return uint32_t(d[at]) << 24 | uint32_t(d[at + 1]) << 16 | uint32_t(d[at + 2]) << 8 | d[at + 3];
}

struct Found {
    sfr::ShaderStage stage;
    std::vector<uint8_t> bytes;
};

// Shader containers as the game hands them to the GPU (native_shaders.cpp):
// big-endian flags 0x102A11xx (bit 0: vertex), virtual and physical sizes.
void scan(const std::vector<uint8_t>& data, std::set<std::pair<int, std::vector<uint8_t>>>& out, size_t& hits) {
    if (data.size() < 36) return;
    for (size_t at = 0; at + 36 <= data.size(); at += 4) {
        if (data[at] != 0x10 || data[at + 1] != 0x2A || data[at + 2] != 0x11) continue;
        const uint32_t flags = be32(data, at), virtual_size = be32(data, at + 4), physical_size = be32(data, at + 8);
        const uint64_t size = uint64_t(virtual_size) + physical_size;
        if (virtual_size < 36 || !physical_size || size > 0x100000 || at + size > data.size()) continue;
        const uint32_t shader_offset = be32(data, at + 0x18);
        if (shader_offset >= virtual_size) continue;
        ++hits;
        out.emplace(int(flags & 1), std::vector<uint8_t>(data.begin() + std::ptrdiff_t(at),
                                                         data.begin() + std::ptrdiff_t(at + size)));
    }
}

std::vector<uint8_t> read_all(const fs::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), {});
}
}

#ifdef _WIN32
#include "plume_d3d11.h"
namespace plume {
std::string D3D11ShaderKey(std::span<const uint8_t>, const std::string&, const char*, uint32_t) { return {}; }
const std::vector<uint8_t>* (*D3D11PrecompiledShader)(const std::string&) = nullptr;
}
#endif
namespace sfr {
// (guest_memory.cpp's, without the rest of guest memory)
RuntimeStop::RuntimeStop(std::string category, uint64_t address, std::string detail)
    : std::runtime_error(category + ": " + detail), category(std::move(category)), address(address), detail(std::move(detail)) {}
// The translation runs for the Vulkan backend (SPIR-V), as on the PS5.
GraphicsBackend selected_graphics_backend() { return GraphicsBackend::vulkan; }
}

int main(int argc, char** argv) {
    std::vector<fs::path> arg;
#ifdef _WIN32
    int count = 0;
    wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int i = 0; i < count; ++i) arg.emplace_back(wide[i]);
    LocalFree(wide);
#else
    for (int i = 0; i < argc; ++i) arg.emplace_back(argv[i]);
#endif
    if (arg.size() < 3) {
        std::cerr << "usage: frr_shaders WORK_DIR DIR [DIR...]\n";
        return 2;
    }
    const fs::path work = arg[1];
    fs::create_directories(work / "runtime");
    const std::string cache = (work / "runtime").string();
#ifdef _WIN32
    _putenv_s("SFR_RUNTIME_SHADER_CACHE", cache.c_str());
    _putenv_s("SFR_SHADER_PACK", (work / "no-pack").string().c_str());
    _putenv_s("SFR_GRAPHICS", "vulkan");
#else
    setenv("SFR_RUNTIME_SHADER_CACHE", cache.c_str(), 1);
    setenv("SFR_SHADER_PACK", (work / "no-pack").c_str(), 1);
    setenv("SFR_GRAPHICS", "vulkan", 1);
#endif
    std::set<std::pair<int, std::vector<uint8_t>>> found;
    size_t hits = 0, files = 0;
    std::vector<fs::path> inputs;
    for (size_t d = 2; d < arg.size(); ++d)
        for (const auto& entry : fs::recursive_directory_iterator(arg[d]))
            if (entry.is_regular_file()) inputs.push_back(entry.path());
    std::sort(inputs.begin(), inputs.end());
    for (const auto& path : inputs) {
        // Compressed files are scanned in their unpacked copies.
        std::ifstream probe(path, std::ios::binary);
        unsigned char magic[4] = {};
        probe.read(reinterpret_cast<char*>(magic), 4);
        if (magic[0] == 0x0F && magic[1] == 0xF5 && magic[2] == 0x12 && magic[3] == 0xED) continue;
        const size_t before = hits;
        scan(read_all(path), found, hits);
        ++files;
        if (hits != before) std::cout << "FILE " << path.filename().generic_string() << ' ' << (hits - before) << '\n';
    }
    std::cout << "SHADERS found " << found.size() << " containers=" << hits << " files=" << files << std::endl;
    size_t done = 0, failed = 0;
    for (const auto& [stage, bytes] : found) {
        try {
            const auto& entry = sfr::runtime_shader(stage ? sfr::ShaderStage::vertex : sfr::ShaderStage::pixel, bytes);
            if (entry.spirv.empty()) ++failed;
        } catch (const std::exception& e) {
            ++failed;
            std::cerr << "SHADER_FAILED " << e.what() << '\n';
        }
        std::cout << "PROGRESS " << ++done << ' ' << found.size() << std::endl;
    }
    std::cout << "TRANSLATED " << done - failed << " failed=" << failed << std::endl;
    return 0;
}
