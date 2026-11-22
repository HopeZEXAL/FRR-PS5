#!/usr/bin/env bash
source "$(dirname -- "${BASH_SOURCE[0]:-$0}")/paths.sh"
# compile_one.sh SOURCE OBJECT LOG -- one game source for the PS5, with the game's own flags.
src=$1 obj=$2 log=$3
if [[ $src == *.c ]]; then
  export PS5_PAYLOAD_SDK=$SRC/PS5_Vulkan/.deps/native/ps5-payload-sdk
  exec sh $SRC/PS5_Vulkan/tooling/prospero-clang18 -std=c11 -O3 -DNDEBUG -w -ffunction-sections -fdata-sections -I$FRR/tools/XenonRecomp/XenonUtils -I$FRR/tools/XenonRecomp/thirdparty -I$FRR/tools/XenonRecomp/thirdparty/libmspack/libmspack/mspack -I$FRR/tools/XenonRecomp/thirdparty/tiny-AES-c -I$FRR/tools/XenonRecomp/thirdparty/fmt/include -I$FRR/tools/XenonRecomp/thirdparty/xxHash -I$FRR/tools/XenonRecomp/thirdparty/disasm -I$FRR/tools/XenonRecomp/thirdparty/TinySHA1 \
    -Dmain=sfr_game_main -c "$src" -o "$obj" > "$log" 2>&1
fi
F=$FRR P=$SRC/plume
export PS5_PAYLOAD_SDK=$SRC/PS5_Vulkan/.deps/native/ps5-payload-sdk
sh $SRC/PS5_Vulkan/tooling/prospero-clang18 -std=gnu++20 -O3 -DNDEBUG -DSFR_DIRECT_MEMORY=1 -D_LIBCPP_ENABLE_EXPERIMENTAL \
  -fno-strict-aliasing -ffp-model=strict -fexceptions -fcxx-exceptions -Wno-unused-variable -Wno-unused-value -w \
  -ffunction-sections -fdata-sections -include $FRR/ps5/compat/atomic_ref_compat.h \
  -I$F/src -I$F/out/recomp/diagnostic -I$F/tools/XenonRecomp/thirdparty/simde -I$F/tools/XenonRecomp/thirdparty/TinySHA1 \
  -I$F/tools/imgui -I$F/tools/imgui/backends -I$SHADERS \
  -I$P -I$P/contrib/Vulkan-Headers/include -I$P/contrib/VulkanMemoryAllocator/include -I$P/contrib/volk -DVK_NO_PROTOTYPES \
  -I$OPT/sdl2-ps5/include/SDL2 -I$FRR/tools/XenonRecomp/XenonUtils -I$FRR/tools/XenonRecomp/thirdparty -I$FRR/tools/XenonRecomp/thirdparty/libmspack/libmspack/mspack -I$FRR/tools/XenonRecomp/thirdparty/tiny-AES-c -I$FRR/tools/XenonRecomp/thirdparty/fmt/include -I$FRR/tools/XenonRecomp/thirdparty/xxHash -I$FRR/tools/XenonRecomp/thirdparty/disasm -I$FRR/tools/XenonRecomp/thirdparty/TinySHA1 \
  -Dmain=sfr_game_main -c "$src" -o "$obj" > "$log" 2>&1
