# Where the PS5 build finds its pieces. Override any of these in the
# environment. FRR is this repository; the others are the PS5 dependencies
# (see ps5/README.md for the revisions and how to set them up).
here_paths=$(cd -- "$(dirname -- "${BASH_SOURCE[0]:-$0}")" && pwd)
: "${FRR:=$(cd "$here_paths/../.." && pwd)}"
: "${SRC:=$HOME/src}"                                   # PS5_Vulkan, PS5_Mesa, plume, SDL-ps5
: "${OPT:=$HOME/opt}"                                   # sdl2-ps5 (SDL built for the PS5)
: "${SHADERS:=$HOME/build/frr-ps5/embedded_shaders}"    # the game's blit/model/overlay shaders
export FRR SRC OPT SHADERS
