#!/usr/bin/env bash
source "$(dirname -- "${BASH_SOURCE[0]:-$0}")/paths.sh"
# Links the whole game into a homebrew title (PPSA48220): the runtime, the
# installer, the generated game code, Plume, SDL2 for the PS5 and RADV, the
# way the probes are linked (PS5_Vulkan tools/radv-link.sh).
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
vk=$SRC/PS5_Vulkan
sdk_root="$vk/.deps/native/ps5-payload-sdk"
archive="$vk/.deps/native/radv-release/lib/libvulkan_radeon.ps5.a"
native="$vk/tooling/native"
tool="$vk/build/runtime-shim/ps5-native-tool"
sdl=$OPT/sdl2-ps5
work="$here/title"
param="$here/param.json"
title_id=$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1]))["titleId"])' "$param")
mkdir -p "$work/obj" "$work/stubs"
cc() { PS5_PAYLOAD_SDK="$sdk_root" sh "$vk/tooling/prospero-clang18" "$@"; }

objects=("$here"/obj/*.o "$here"/inst-obj/*.o "$here"/gen-obj/*.o)
[[ -f $here/obj/camera_capture_v4l2.o ]] && { echo "camera_capture_v4l2.o must not be linked" >&2; exit 2; }
cc -std=c++20 -O2 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -c "$native/app_crt.cpp" -o "$work/obj/app_crt.o"
cc -std=c++20 -O2 -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections -c "$native/app_cpp_runtime.cpp" -o "$work/obj/app_cpp_runtime.o"
stubs=() stub_args=()
for library in libSceAgc libSceAgcDriver; do
    source=vendor/ps5/sdk/stubs/agc_canary_link_stub.c
    [[ $library == libSceAgcDriver ]] && source=vendor/ps5/sdk/stubs/agc_driver_canary_link_stub.c
    cc -std=c11 -O2 -fPIC -c "$vk/$source" -o "$work/obj/${library}_stub.o"
    "$sdk_root/bin/prospero-lld" --shared -soname "$library.prx" -o "$work/stubs/$library.so" "$work/obj/${library}_stub.o"
    stubs+=("$work/stubs/$library.so"); stub_args+=(--stub "$work/stubs/$library.so")
done
source "$vk/tools/radv-link.sh"
radv_link_recipe "$vk" "$sdk_root" "$archive" || exit 2
link() {
    "$sdk_root/bin/prospero-lld" "${radv_linker_script[@]}" --eh-frame-hdr "${radv_link_flags[@]}" \
        --version-script "$native/app-symbols.map" --exclude-libs=ALL \
        -e _start -o "$work/llvm-pie.elf" "$@" \
        "$work/obj/app_crt.o" "$work/obj/app_cpp_runtime.o" "${objects[@]}" "$sdl/lib/libSDL2.a" "${stubs[@]}" \
        "$sdk_root/target/lib/libc++experimental.a" \
        "${radv_link_inputs[@]}" --as-needed "$sdk_root"/target/lib/*.so
}
link
mapfile -t weak_zero < <(python3 $FRR/ps5/builder/app/scripts/weak_zero.py "$(command -v llvm-readelf-18)" \
    "$work/llvm-pie.elf" "${stubs[@]}" "$sdk_root"/target/lib/*.so)
link "${weak_zero[@]}"
"$tool" link --in "$work/llvm-pie.elf" --out "$work/eboot.elf" --stub-dir "$sdk_root/target/lib" "${stub_args[@]}" \
    --module-sdk 0x02000009 --companion-sdk 0x08050001 --file-name eboot.elf
app="$work/dist/$title_id"
rm -rf -- "$app"; mkdir -p "$app/sce_sys" "$app/sce_module"
"$tool" self --sign --in "$work/eboot.elf" --out "$app/eboot.bin" --magic 0x1D3D154F
cp "$param" "$app/sce_sys/param.json"; cp "$here/icon0.png" "$app/sce_sys/icon0.png"; cp "$here/pic0.png" "$app/sce_sys/pic0.png"
cp "$vk/runtime/libc.prx" "$app/sce_module/libc.prx"
# The overlays' font (the achievement list, its toasts): DejaVu Sans, under its
# own free licence (fonts/LICENSE), since the console has no fonts to borrow.
mkdir -p "$app/fonts"
cp /usr/share/fonts/truetype/dejavu/DejaVuSans.ttf /usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf "$app/fonts/"
cp /usr/share/doc/fonts-dejavu-core/copyright "$app/fonts/LICENSE"
# Licences, credits and the README (CREDITS.txt lists them).
S=$SRC X=$FRR/tools/XenonRecomp/thirdparty L="$app/licenses"
mkdir -p "$L/Mesa"
cp "$FRR/COPYING" "$L/GPL-3.0.txt"
cp "$FRR/tools/XenonRecomp/LICENSE.md" "$L/XenonRecomp.txt"
cp "$S/plume/LICENSE" "$L/Plume.txt"
cp $SRC/PS5_Mesa/docs/license.rst "$L/Mesa/license.rst"
cp -r $SRC/PS5_Mesa/licenses/. "$L/Mesa/"
cp "$S/SDL-ps5/LICENSE.txt" "$L/SDL2.txt"
cp "$S/plume/contrib/VulkanMemoryAllocator/LICENSE.txt" "$L/VulkanMemoryAllocator.txt"
cp "$S/plume/contrib/volk/LICENSE.md" "$L/volk.txt"
cp "$FRR/tools/imgui/LICENSE.txt" "$L/imgui.txt"
cp "$X/simde/COPYING" "$L/simde.txt"
cp "$X/fmt/LICENSE" "$L/fmt.txt"
cp "$X/xxHash/LICENSE" "$L/xxHash.txt"
cp "$X/libmspack/libmspack/COPYING.LIB" "$L/libmspack-LGPL-2.1.txt"
cp "$X/tiny-AES-c/unlicense.txt" "$L/tiny-AES-c.txt"
sed -n '1,25p' "$X/TinySHA1/TinySHA1.hpp" > "$L/TinySHA1.txt"
cp "$here/README.txt" "$here/CREDITS.txt" "$app/"
"$tool" self --inspect --file "$app/eboot.bin" > /dev/null
printf 'title: %s (eboot.bin %s bytes)\n' "$app" "$(stat -c %s "$app/eboot.bin")"
