/* The game reads some settings while its static initialisers run (the CPU
 * diagnostic's watchdog and call budget among them), before main() can set
 * them: build 10 still stopped at 10 seconds. This runs first (after the boot
 * tracer): /data/frr-ps5/settings.env (NAME=VALUE lines), then the launcher's
 * play defaults for whatever it leaves unset. */
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void load_settings_env(void)
{
   const int fd = open("/data/frr-ps5/settings.env", O_RDONLY);
   if (fd < 0)
      return;
   static char text[16384];
   ssize_t length = read(fd, text, sizeof(text) - 1);
   close(fd);
   if (length <= 0)
      return;
   text[length] = 0;
   char *line = text;
   while (line && *line) {
      char *next = strchr(line, '\n');
      if (next)
         *next++ = 0;
      size_t n = strlen(line);
      if (n && line[n - 1] == '\r')
         line[--n] = 0;
      char *equals = strchr(line, '=');
      if (line[0] != '#' && equals && equals != line) {
         *equals = 0;
         setenv(line, equals + 1, 0);
      }
      line = next;
   }
}

__attribute__((constructor(102))) static void early_environment(void)
{
   load_settings_env();
   static const char *const defaults[][2] = {
      {"SFR_CALL_BUDGET", "18446744073709551615"},
      {"SFR_WATCHDOG_SECONDS", "31536000"},
      {"SFR_ALLOW_RENDER_TARGETS", "1"},
      {"SFR_TRACE_GRAPHICS", "0"},
      {"SFR_DIAGNOSTIC_ENTRIES", "0"},
      {"SFR_TRACE_IMPORTS", "0"},
      {"SFR_FRAME_LIMIT", "60"},
      {"SFR_RENDER_SCALE", "150"},
      {"SFR_PARALLEL_WORKER", "all"},
      {"SFR_PROFILE", "1"},
      {"SFR_GRAPHICS", "vulkan"},
      /* Mesa's own disk cache: its database lock took 0.6-2 s on the PS5 when
         a pipeline was looked up; FRR keeps the pipelines (pipeline-cache/). */
      {"MESA_SHADER_CACHE_DISABLE", "true"},
   };
   for (size_t i = 0; i < sizeof(defaults) / sizeof(defaults[0]); ++i)
      setenv(defaults[i][0], defaults[i][1], 0);
}
