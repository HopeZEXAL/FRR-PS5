#pragma once
// Guest address space on the PlayStation 5, for GuestMemory.
//
// A PS5 title's anonymous mmap draws on its flexible-memory budget (448 MiB
// configured on the console this was measured on) and is refused 4 GiB at
// any address, so GuestMemory's POSIX backend cannot reserve the guest
// space there. The console's own calls can:
//
//   reserve   sceKernelReserveVirtualRange: address space, no memory behind it
//   back      sceKernelAllocateDirectMemory + sceKernelMapDirectMemory(FIXED):
//             memory from the 12 GiB direct pool, mapped read-write
//   unback    sceKernelMunmap, sceKernelReserveVirtualRange(FIXED) to put the
//             reservation back, sceKernelReleaseDirectMemory
//   protect   sceKernelMprotect
//
// Direct memory is handed out in 64 KiB units, so GuestMemory works in 64 KiB
// host pages here (16 guest pages each), the way it already handles 16 KiB
// hosts. A reserved page has no memory: unlike Linux, making it read-write
// does not make it usable, so every host page a commit touches is backed
// first. Direct memory is not zeroed when allocated; back() zeroes it.
//
// The space goes at 0x10_0000_0000. A title's mapping with no address lands
// in the Vulkan driver's GPU window (0x2_0000_0000..0x3_0000_0000), and the
// PS5 SDK fork's layout keeps 0x10_0000_0000 for guest-memory arenas (its
// heap is at 0x20_0000_0000, GPU memory from 0x40_0000_0000).
//
// Verified on a console by the Free Riders PS5 Vulkan probe (2e): 4 GiB
// reserved exactly at the hint, commits written and read back, protection
// changes, decommit and recommit, a 512 MiB commit, all while RADV drew 599
// frames at 59.9 fps.
//
// SFR_PS5_MEMORY_SIM builds the same backend on a Linux or macOS host over a
// strict simulation of those calls: an unbacked page cannot be protected
// into use, direct memory arrives filled with 0xCD instead of zeros, and a
// map, unmap or release that does not match the simulated state fails. The
// runtime tests run against it before anything reaches a console.
#include <cstddef>
#include <cstdint>

#if defined(__PROSPERO__) || defined(SFR_PS5_MEMORY_SIM)
#define SFR_PS5_MEMORY 1
#endif

#ifdef SFR_PS5_MEMORY
namespace sfr::ps5vm {
constexpr uint64_t page_size = 0x10000;          // the direct-memory unit
constexpr uintptr_t guest_hint = 0x1000000000ull;  // the guest-memory area
constexpr uintptr_t gpu_window_low = 0x200000000ull, gpu_window_high = 0x300000000ull;

// Reserves size bytes of address space at the guest hint. Returns 0, or the
// kernel's error (nonzero) with *at untouched.
int32_t reserve(size_t size, void** at);
// Frees a whole reservation made by reserve(). Pages still backed must have
// been unbacked first.
int32_t free_range(void* at, size_t size);
// Backs one host page of a reservation with fresh direct memory, read-write
// and zeroed. Returns 0 and the memory's start, or the kernel's error.
int32_t back(void* at, int64_t* direct);
// Returns one backed host page to reserved-only and releases its memory.
int32_t unback(void* at, int64_t direct);
// Changes the protection of backed pages: read and write, read only, or none.
int32_t protect(void* at, size_t size, bool read, bool write);
}  // namespace sfr::ps5vm
#endif
