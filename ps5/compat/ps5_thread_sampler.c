/* A sampling probe for guest threads (DELETION_WAIT): the waiting main thread
   sends a registered thread SIGUSR1; its handler records where it was (rip,
   then a backtrace) and the sender reads that back. Offsets are from the
   title's load address, for the link map. */
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>
#include <sys/ucontext.h>
#include <time.h>
#include <sched.h>

int backtrace(void **buffer, int size);
extern char __ehdr_start;

#define SAMPLER_THREADS 64
#define SAMPLER_DEPTH 12
static pthread_t threads[SAMPLER_THREADS];
static atomic_int registered[SAMPLER_THREADS];
static volatile uint64_t sample[SAMPLER_DEPTH + 1];
static atomic_int sample_count;
static atomic_int sample_ready;
static atomic_int installed;
static pthread_mutex_t sampling = PTHREAD_MUTEX_INITIALIZER;

static void on_sample(int signal_number, siginfo_t *info, void *context) {
    (void)signal_number; (void)info;
    const ucontext_t *uc = (const ucontext_t *)context;
    const uint64_t base = (uint64_t)(uintptr_t)&__ehdr_start;
    sample[0] = (uint64_t)uc->uc_mcontext.mc_rip - base;
    /* The frame chain from the interrupted code: rbp walk (the title keeps
       frame pointers in places), bounded and checked for sane addresses. */
    int n = 1;
    uint64_t rbp = (uint64_t)uc->uc_mcontext.mc_rbp;
    const uint64_t rsp = (uint64_t)uc->uc_mcontext.mc_rsp;
    while (n <= SAMPLER_DEPTH && rbp > rsp && rbp < rsp + (16u << 20) && (rbp & 7) == 0) {
        const uint64_t ret = ((const uint64_t *)rbp)[1];
        const uint64_t next = ((const uint64_t *)rbp)[0];
        if (ret < base) break;
        sample[n++] = ret - base;
        if (next <= rbp) break;
        rbp = next;
    }
    atomic_store(&sample_count, n);
    atomic_store(&sample_ready, 1);
}

void frr_sampler_register(uint32_t id) {
    if (id >= SAMPLER_THREADS) return;
    if (!atomic_exchange(&installed, 1)) {
        struct sigaction action;
        memset(&action, 0, sizeof action);
        action.sa_sigaction = on_sample;
        action.sa_flags = SA_SIGINFO | SA_RESTART;
        sigemptyset(&action.sa_mask);
        sigaction(SIGUSR1, &action, 0);
    }
    threads[id] = pthread_self();
    atomic_store(&registered[id], 1);
}

/* Up to max offsets (rip first) of thread id, or 0 when it gave none within 5 ms. */
int frr_sampler_sample(uint32_t id, uint64_t *out, int max) {
    if (id >= SAMPLER_THREADS || !atomic_load(&registered[id])) return 0;
    pthread_mutex_lock(&sampling);
    atomic_store(&sample_ready, 0);
    int count = 0;
    if (pthread_kill(threads[id], SIGUSR1) == 0) {
        struct timespec start, now;
        clock_gettime(CLOCK_MONOTONIC, &start);
        for (;;) {
            if (atomic_load(&sample_ready)) {
                count = atomic_load(&sample_count);
                if (count > max) count = max;
                for (int i = 0; i < count; ++i) out[i] = sample[i];
                break;
            }
            clock_gettime(CLOCK_MONOTONIC, &now);
            if ((now.tv_sec - start.tv_sec) * 1000000000L + (now.tv_nsec - start.tv_nsec) > 5000000L) break;
            sched_yield();
        }
    }
    pthread_mutex_unlock(&sampling);
    return count;
}

/* PRESENT_STALL: a watchdog thread that, when the main thread has not
   presented for 150 ms, samples it every 15 ms (up to 24 samples) and logs
   where it was, once per stall, for the first 20 stalls. */
#include <stdio.h>
#include <unistd.h>
void frr_log_line(const char *line);
int frr_stall_watchdog_enabled(void);
static atomic_ullong last_present_ns;
static atomic_int watchdog_started;
static uint64_t sampler_now_ns(void) {
    struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec;
}
static void *present_watchdog(void *unused) {
    (void)unused;
    int stalls = 0;
    char line[4096];
    for (;;) {
        usleep(10000);
        const uint64_t last = atomic_load(&last_present_ns);
        if (!last || sampler_now_ns() - last < 150000000ull || stalls >= 20) continue;
        if (!frr_stall_watchdog_enabled()) continue;  /* the settings page's switch */
        int length = snprintf(line, sizeof line, "PRESENT_STALL samples=");
        int samples = 0;
        while (atomic_load(&last_present_ns) == last && samples < 24 && length < (int)sizeof line - 200) {
            uint64_t frames[13];
            const int n = frr_sampler_sample(1, frames, 13);
            for (int i = 0; i < n; ++i)
                length += snprintf(line + length, sizeof line - length, i ? ">%llx" : "%llx", (unsigned long long)frames[i]);
            length += snprintf(line + length, sizeof line - length, ";");
            ++samples;
            usleep(15000);
        }
        while (atomic_load(&last_present_ns) == last) usleep(5000);
        snprintf(line + length, sizeof line - length, " stall_ms=%.1f\n",
                 (double)(atomic_load(&last_present_ns) - last) / 1e6);
        frr_log_line(line);
        ++stalls;
    }
    return 0;
}
void frr_present_watchdog_tick(void) {
    if (!atomic_exchange(&watchdog_started, 1)) {
        frr_sampler_register(1);
        pthread_t thread;
        pthread_create(&thread, 0, present_watchdog, 0);
    }
    atomic_store(&last_present_ns, sampler_now_ns());
}
