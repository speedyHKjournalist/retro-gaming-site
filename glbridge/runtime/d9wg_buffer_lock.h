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

/* Retail8 permits nested maps. Preserve all writable ranges until the last
 * Unlock (draw-time mapped uploads also observe this union). Streaming hints
 * are conservative once maps overlap; READONLY survives only if all maps are. */
static inline int d9wg_buffer_lock_merge(uint32_t offset, uint32_t size, uint32_t flags,
        uint32_t *count, uint32_t *mapped_offset, uint32_t *mapped_size, uint32_t *mapped_flags)
{
    if (*count == UINT32_MAX) return 0;
    if (!*count) {
        *mapped_offset = offset;
        *mapped_size = size;
        *mapped_flags = flags;
    } else {
        uint32_t end = *mapped_offset + *mapped_size;
        uint32_t next_end = offset + size;
        if (offset < *mapped_offset) *mapped_offset = offset;
        if (next_end > end) end = next_end;
        *mapped_size = end - *mapped_offset;
        *mapped_flags &= flags & D9WG_BUFFER_LOCK_READONLY;
    }
    ++*count;
    return 1;
}

#define D9WG_MAX_DRAW_MAPS 17u
typedef struct D9WGMappedBuffer {
    uintptr_t identity;
    const uint8_t *shadow;
    uint32_t handle, length, offset, size, flags, uploaded;
} D9WGMappedBuffer;
typedef int (*D9WGEmitMappedBuffer)(void *context, uint32_t handle,
        uint32_t offset, const uint8_t *data, uint32_t size, uint32_t flags);
/* Validate every map before emitting. Duplicate bindings upload once; genuine
 * read-only maps upload nothing. Accepted unique maps retain an uploaded
 * receipt even if a later emission fails, so callers consume DISCARD once. */
int d9wg_flush_mapped_buffers(D9WGMappedBuffer *maps, uint32_t count,
        D9WGEmitMappedBuffer emit, void *context);
#endif
