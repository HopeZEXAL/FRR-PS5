/* clock_gettime(CLOCK_MONOTONIC) from the time-stamp counter. A PS5 title's
 * clock_gettime is a system call, about 900 ns (HOST_TIMING clock_read_ns in
 * every game.log), against some 20 ns on a desktop; the game reads the clock
 * for every guest time-base read, wait and frame measurement. Linked with
 * libc++'s chrono.cpp.o, its clock_gettime renamed to this (link_game.sh): the monotonic clocks are the TSC (invariant on the
 * PS5's Zen 2) scaled to nanoseconds from one real reading at start-up; every
 * other clock is the system's. */
#include <stdint.h>
#include <time.h>

int clock_gettime(clockid_t clock, struct timespec *out);
uint64_t sceKernelReadTsc(void);
uint64_t sceKernelGetTscFrequency(void);

static uint64_t base_tsc, base_ns, tsc_frequency;

__attribute__((constructor(103))) static void fast_clock_start(void)
{
   const uint64_t frequency = sceKernelGetTscFrequency();
   struct timespec now;
   if (frequency < 1000000 || clock_gettime(CLOCK_MONOTONIC, &now) != 0)
      return;
   base_tsc = sceKernelReadTsc();
   base_ns = (uint64_t)now.tv_sec * 1000000000ull + (uint64_t)now.tv_nsec;
   tsc_frequency = frequency;
}

static int monotonic(clockid_t clock)
{
   return clock == CLOCK_MONOTONIC
#ifdef CLOCK_MONOTONIC_PRECISE
      || clock == CLOCK_MONOTONIC_PRECISE
#endif
#ifdef CLOCK_MONOTONIC_FAST
      || clock == CLOCK_MONOTONIC_FAST
#endif
#ifdef CLOCK_UPTIME
      || clock == CLOCK_UPTIME
#endif
#ifdef CLOCK_UPTIME_PRECISE
      || clock == CLOCK_UPTIME_PRECISE
#endif
#ifdef CLOCK_UPTIME_FAST
      || clock == CLOCK_UPTIME_FAST
#endif
      ;
}

int ps5_fast_clock_gettime(clockid_t clock, struct timespec *out)
{
   if (!tsc_frequency || !out || !monotonic(clock))
      return clock_gettime(clock, out);
   const uint64_t ticks = sceKernelReadTsc() - base_tsc;
   const uint64_t ns = base_ns + (uint64_t)(((unsigned __int128)ticks * 1000000000ull) / tsc_frequency);
   out->tv_sec = (time_t)(ns / 1000000000ull);
   out->tv_nsec = (long)(ns % 1000000000ull);
   return 0;
}
