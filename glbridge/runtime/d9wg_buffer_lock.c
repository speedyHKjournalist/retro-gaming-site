#include "d9wg_buffer_lock.h"

uint32_t d9wg_buffer_lock_flags(uint32_t usage, uint32_t pool, uint32_t requested)
{
    uint32_t streaming = requested & (D9WG_BUFFER_LOCK_DISCARD | D9WG_BUFFER_LOCK_NOOVERWRITE);
    /* A usable read-only access has neither a write-only resource nor a
     * streaming hint. Contradictory access hints fall back to a writable map. */
    if ((requested & D9WG_BUFFER_LOCK_READONLY)
        && !(usage & D9WG_BUFFER_USAGE_WRITEONLY) && !streaming)
        return D9WG_BUFFER_LOCK_READONLY;
    if (!(usage & D9WG_BUFFER_USAGE_DYNAMIC) || pool > 1u)
        return 0;
    /* Simultaneous streaming hints preserve existing storage (NOOVERWRITE). */
    if (streaming & D9WG_BUFFER_LOCK_NOOVERWRITE)
        return D9WG_BUFFER_LOCK_NOOVERWRITE;
    return streaming & D9WG_BUFFER_LOCK_DISCARD;
}

int d9wg_flush_mapped_buffers(D9WGMappedBuffer *maps, uint32_t count,
        D9WGEmitMappedBuffer emit, void *context)
{
    if (count > D9WG_MAX_DRAW_MAPS || (count && (!maps || !emit))) return 0;
    for (uint32_t i = 0; i < count; ++i) {
        const D9WGMappedBuffer *map = maps + i;
        if (!map->identity || !map->handle || map->offset > map->length ||
                map->size > map->length - map->offset || (map->size && !map->shadow)) return 0;
        for (uint32_t j = 0; j < i; ++j) {
            const D9WGMappedBuffer *prior = maps + j;
            if (prior->identity == map->identity && (prior->handle != map->handle ||
                    prior->shadow != map->shadow || prior->length != map->length ||
                    prior->offset != map->offset || prior->size != map->size || prior->flags != map->flags)) return 0;
        }
    }
    for (uint32_t i = 0; i < count; ++i) maps[i].uploaded = 0;
    for (uint32_t i = 0; i < count; ++i) {
        D9WGMappedBuffer *map = maps + i;
        if (map->flags & D9WG_BUFFER_LOCK_READONLY) continue;
        uint32_t j = 0;
        for (; j < i; ++j) if (maps[j].identity == map->identity) break;
        if (j < i) continue;
        if (!emit(context, map->handle, map->offset,
                map->shadow ? map->shadow + map->offset : 0, map->size, map->flags)) return 0;
        map->uploaded = 1;
    }
    return 1;
}
