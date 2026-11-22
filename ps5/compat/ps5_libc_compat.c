/* libc functions the PS5's libraries do not export, which the port's code
 * (or code it builds) calls. Linked into the game title only. */

/* ffs: the 1-based index of the lowest set bit, 0 for 0 (POSIX strings.h).
 * XenonUtils' XEX decoder sizes its LZX window with it. */
int ffs(int value) { return __builtin_ffs(value); }

