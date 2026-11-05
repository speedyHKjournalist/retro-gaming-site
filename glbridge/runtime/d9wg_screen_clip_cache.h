#ifndef D9WG_SCREEN_CLIP_CACHE_H
#define D9WG_SCREEN_CLIP_CACHE_H
#include "d9wg_clip.h"

/* Immediate XYZRHW draws often submit the same vertex range with different
 * index lists. ClipStatus includes that entire range. Keep an exact, bounded
 * position snapshot: pointer identity or a hash alone cannot detect edits.
 * The caller validates every draw's indices before consulting this cache. */
typedef struct D9WGScreenClipCache {
    uint8_t *positions;
    uint32_t capacity, count, valid;
    D9WGProcessViewport viewport;
    D9WGClipStatus status;
} D9WGScreenClipCache;

static __inline int d9wg_screen_clip_equal(const void *a, const void *b, uint32_t words)
{
    for (uint32_t i = 0; i < words; ++i) {
        uint32_t x, y;
        __builtin_memcpy(&x, (const uint8_t *)a + i*4u, 4);
        __builtin_memcpy(&y, (const uint8_t *)b + i*4u, 4);
        if (x != y) return 0;
    }
    return 1;
}
static __inline __attribute__((always_inline)) uint32_t d9wg_screen_clip_word(const void *p, unsigned offset)
{
    uint32_t word;__builtin_memcpy(&word, (const uint8_t *)p + offset, 4);return word;
}
static __inline __attribute__((always_inline)) int d9wg_screen_clip_position_equal(const void *a, const void *b)
{
    /* Scalar loads preserve unaligned/aliased inputs and exact NaN bits.
     * Copying two 16-byte arrays makes the size-optimized x86 compiler emit
     * two stack copies (REP MOVSD) for every vertex before comparing them. */
    return d9wg_screen_clip_word(a, 0) == d9wg_screen_clip_word(b, 0) &&
        d9wg_screen_clip_word(a, 4) == d9wg_screen_clip_word(b, 4) &&
        d9wg_screen_clip_word(a, 8) == d9wg_screen_clip_word(b, 8) &&
        d9wg_screen_clip_word(a, 12) == d9wg_screen_clip_word(b, 12);
}
static __inline int d9wg_screen_clip_matches(const D9WGScreenClipCache *cache,
        const void *vertices, uint32_t stride, uint32_t count,
        const D9WGProcessViewport *viewport)
{
    if (!cache->valid || !cache->positions || !vertices || stride < 16 ||
            !count || count != cache->count || count > cache->capacity/16u ||
            !d9wg_screen_clip_equal(&cache->viewport, viewport, 6)) return 0;
    const uint8_t *input = vertices;
    for (uint32_t i = 0; i < count; ++i, input += stride)
        if (!d9wg_screen_clip_position_equal(cache->positions + i*16u, input)) return 0;
    return 1;
}
static __inline void d9wg_screen_clip_store(D9WGScreenClipCache *cache,
        const void *vertices, uint32_t stride, uint32_t count,
        const D9WGProcessViewport *viewport, const D9WGClipStatus *status)
{
    cache->valid = 0;
    if (!cache->positions || !vertices || stride < 16 || !count || count > cache->capacity/16u) return;
    const uint8_t *input = vertices;
    for (uint32_t i = 0; i < count; ++i, input += stride)
        __builtin_memcpy(cache->positions + i*16u, input, 16);
    cache->viewport = *viewport;cache->status = *status;cache->count = count;cache->valid = 1;
}
#endif
