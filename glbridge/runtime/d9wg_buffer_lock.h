#ifndef D9WG_BUFFER_LOCK_H
#define D9WG_BUFFER_LOCK_H
#include <stdint.h>

#define D9WG_BUFFER_LOCK_READONLY 0x10u
#define D9WG_BUFFER_LOCK_NOOVERWRITE 0x1000u
#define D9WG_BUFFER_LOCK_DISCARD 0x2000u
#define D9WG_BUFFER_USAGE_WRITEONLY 0x8u
#define D9WG_BUFFER_USAGE_DYNAMIC 0x200u

/* XP retail accepts conflicting and unknown Lock hints. This buffer-only
 * policy returns effective CPU read-only / upload hints, following Wine's
 * access conversion and map sanitization. Other advisory/unknown bits have
 * no buffer-shadow effect. DEFAULT and MANAGED have GPU backing.
 * DISCARD makes old contents undefined; do not clear the whole CPU shadow. */
uint32_t d9wg_buffer_lock_flags(uint32_t usage, uint32_t pool, uint32_t requested);
#endif
