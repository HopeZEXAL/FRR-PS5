#pragma once
// The two declarations of plume_d3d11.h that runtime_shader_cache.cpp uses,
// for frr_shaders (which never runs the D3D11 backend).
#include <cstdint>
#include <span>
#include <string>
#include <vector>
namespace plume {
std::string D3D11ShaderKey(std::span<const uint8_t> source, const std::string& entry, const char* profile,
                           uint32_t specConstants);
extern const std::vector<uint8_t>* (*D3D11PrecompiledShader)(const std::string& key);
}
