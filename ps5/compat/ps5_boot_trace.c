/* Boot tracer for the PS5 game title: runs before every other static
 * constructor, writes /data/frr-ps5/boot-trace.txt straight away, and records
 * any crash signal with its code offset in the title, so a crash before the
 * game's own log exists can still be traced to a function (llvm-symbolizer on
 * the title's llvm-pie.elf). Only plain system calls in the handler. */
#include <fcntl.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ucontext.h>
#include <unistd.h>

int sceKernelDebugOutText(int channel, const char *text);
int backtrace(void **buffer, int size);
extern char __ehdr_start;  /* the title's load address (lld) */

static int trace_fd = -1;
static int crash_fd = -1;  /* crash.log, opened by the crash handler */
extern const char frr_ps5_version[];

static void put(const char *text)
{
   if (trace_fd >= 0) (void)!write(trace_fd, text, strlen(text));
   if (crash_fd >= 0) (void)!write(crash_fd, text, strlen(text));
   sceKernelDebugOutText(0, text);
}

static void put_hex(const char *label, uint64_t value)
{
   char buffer[48];
   size_t n = strlen(label);
   memcpy(buffer, label, n);
   buffer[n++] = '0';
   buffer[n++] = 'x';
   for (int shift = 60; shift >= 0; shift -= 4) buffer[n++] = "0123456789abcdef"[(value >> shift) & 15];
   buffer[n++] = ' ';
   buffer[n] = 0;
   put(buffer);
}

/* Called from ps5_main.cpp at each start-up stage. */
void frr_boot_mark(const char *stage)
{
   put("boot: ");
   put(stage);
   put("\n");
}

static void crashed(int signal_number, siginfo_t *info, void *context)
{
   const ucontext_t *uc = (const ucontext_t *)context;
   const uint64_t base = (uint64_t)(uintptr_t)&__ehdr_start;
   const uint64_t pc = (uint64_t)uc->uc_mcontext.mc_rip;
   /* crash.log: the same lines, in a file of their own, for bug reports. */
   crash_fd = open("/data/frr-ps5/crash.log", O_WRONLY | O_CREAT | O_TRUNC, 0666);
   if (crash_fd >= 0) {
      const char *head = "Free Riders Recompiled for PS5 crashed. Please attach this file, game.log and\n"
                         "boot-trace.txt to your bug report.\nversion: ";
      (void)!write(crash_fd, head, strlen(head));
      (void)!write(crash_fd, frr_ps5_version, strlen(frr_ps5_version));
      (void)!write(crash_fd, "\n", 1);
   }
   put("CRASH ");
   put_hex("signal=", (uint64_t)signal_number);
   put_hex("fault_address=", (uint64_t)(uintptr_t)info->si_addr);
   put_hex("pc=", pc);
   put_hex("title_base=", base);
   put_hex("pc_offset=", pc - base);
   put("\n");
   /* A call through a null pointer leaves the caller's return address at
      [rsp]; scanning the stack for title code addresses gives the callers
      even when backtrace() cannot unwind from the handler. */
   const uint64_t sp = (uint64_t)uc->uc_mcontext.mc_rsp;
   put_hex("rsp=", sp);
   put_hex("rbp=", (uint64_t)uc->uc_mcontext.mc_rbp);
   put("\n");
   put_hex("rax=", (uint64_t)uc->uc_mcontext.mc_rax); put_hex("rbx=", (uint64_t)uc->uc_mcontext.mc_rbx);
   put_hex("rcx=", (uint64_t)uc->uc_mcontext.mc_rcx); put_hex("rdx=", (uint64_t)uc->uc_mcontext.mc_rdx);
   put("\n");
   put_hex("rsi=", (uint64_t)uc->uc_mcontext.mc_rsi); put_hex("rdi=", (uint64_t)uc->uc_mcontext.mc_rdi);
   put_hex("r8=", (uint64_t)uc->uc_mcontext.mc_r8); put_hex("r9=", (uint64_t)uc->uc_mcontext.mc_r9);
   put("\n");
   put_hex("r10=", (uint64_t)uc->uc_mcontext.mc_r10); put_hex("r11=", (uint64_t)uc->uc_mcontext.mc_r11);
   put_hex("r12=", (uint64_t)uc->uc_mcontext.mc_r12); put_hex("r13=", (uint64_t)uc->uc_mcontext.mc_r13);
   put("\n");
   put_hex("r14=", (uint64_t)uc->uc_mcontext.mc_r14); put_hex("r15=", (uint64_t)uc->uc_mcontext.mc_r15);
   put("\n");
   void *frames[24];
   const int count = backtrace(frames, 24);
   for (int i = 0; i < count; ++i) {
      put_hex("  frame_offset=", (uint64_t)(uintptr_t)frames[i] - base);
      put("\n");
   }
   if (sp != 0 && (sp & 7) == 0) {
      const uint64_t *stack = (const uint64_t *)(uintptr_t)sp;
      for (int i = 0; i < 96; ++i) {
         const uint64_t value = stack[i];
         if (value > base && value - base < 0x3110000) {
            put_hex("  stack_code_offset=", value - base);
            put_hex("at_slot=", (uint64_t)i);
            put("\n");
         }
      }
   }
   if (trace_fd >= 0) fsync(trace_fd);
   if (crash_fd >= 0) { fsync(crash_fd); close(crash_fd); crash_fd = -1; }
   signal(signal_number, SIG_DFL);
   raise(signal_number);
}

__attribute__((constructor(101))) static void boot_trace_start(void)
{
   mkdir("/data/frr-ps5", 0777);
   trace_fd = open("/data/frr-ps5/boot-trace.txt", O_WRONLY | O_CREAT | O_TRUNC, 0666);
   put("boot: first constructor (before the game's static initialisers)\n");
   put_hex("boot: title_base=", (uint64_t)(uintptr_t)&__ehdr_start);
   put("\n");
   struct sigaction action;
   memset(&action, 0, sizeof action);
   action.sa_sigaction = crashed;
   action.sa_flags = SA_SIGINFO;
   sigemptyset(&action.sa_mask);
   const int signals[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTRAP};
   for (unsigned i = 0; i < sizeof signals / sizeof signals[0]; ++i) sigaction(signals[i], &action, 0);
}
