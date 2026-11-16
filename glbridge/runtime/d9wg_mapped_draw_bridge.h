#ifndef D9WG_MAPPED_DRAW_BRIDGE_H
#define D9WG_MAPPED_DRAW_BRIDGE_H
#include "d9wg_buffer_lock.h"

static int d9wg_emit_mapped_buffer(void *context, uint32_t handle,
        uint32_t offset, const uint8_t *data, uint32_t size, uint32_t flags)
{
    (void)context;
    return emit_buffer_update(handle, offset, data, size, flags);
}

/* Initial managed residency includes the entire CPU allocation, even writes
 * outside the declared Lock range. Later locks keep their exact partial upload.
 * An unsuccessful upload leaves residency pending for PreLoad / draw retry. */
static inline BOOL d9wg_preload_buffer(uint32_t handle, const uint8_t *shadow,
        uint32_t length, BOOL *pending)
{
    if (!*pending) return TRUE;
    if (!emit_buffer_update(handle, 0, shadow, length, 0)) return FALSE;
    *pending = FALSE;
    return TRUE;
}

static BOOL d9wg_flush_draw_buffers(D9WG_MAPPED_DEVICE *device, BOOL indexed)
{
    D9WGMappedBuffer maps[D9WG_MAX_DRAW_MAPS];
    void *objects[D9WG_MAX_DRAW_MAPS];
    BOOL index_kind[D9WG_MAX_DRAW_MAPS];
    uint32_t count = 0;
    _Static_assert(sizeof(device->streams)/sizeof(device->streams[0]) < D9WG_MAX_DRAW_MAPS, "mapped draw stream capacity");
#define D9WG_COLLECT_DRAW_MAP(buffer, is_index) do { \
    if ((buffer) && (buffer)->full_upload_pending) { \
        if ((buffer)->device != device) return FALSE; \
        if (!d9wg_preload_buffer((buffer)->handle, (buffer)->shadow, \
                (buffer)->length, &(buffer)->full_upload_pending)) return FALSE; \
    } \
    if ((buffer) && (buffer)->locked) { \
        if ((buffer)->device != device) return FALSE; \
        maps[count] = (D9WGMappedBuffer){ (uintptr_t)(buffer), (buffer)->shadow, \
            (buffer)->handle, (buffer)->length, (buffer)->lock_offset, \
            (buffer)->lock_size, (buffer)->lock_flags, 0 }; \
        objects[count] = (buffer);index_kind[count++] = (is_index); \
    } \
} while (0)
    for (uint32_t i = 0; i < sizeof(device->streams)/sizeof(device->streams[0]); ++i) {
        D9WG_MAPPED_VERTEX *buffer = device->streams[i].buffer;
        D9WG_COLLECT_DRAW_MAP(buffer, FALSE);
    }
    if (indexed) {
        D9WG_MAPPED_INDEX *buffer = device->index_buffer;
        D9WG_COLLECT_DRAW_MAP(buffer, TRUE);
    }
#undef D9WG_COLLECT_DRAW_MAP
    BOOL result = d9wg_flush_mapped_buffers(maps, count, d9wg_emit_mapped_buffer, NULL);
    /* The transport may have accepted a prefix. Preserve its receipts even
     * when the final buffer failed; later draws/Unlock must not re-discard it. */
    for (uint32_t i = 0; i < count; ++i) {
        if (!maps[i].uploaded) continue;
        if (index_kind[i]) {
            D9WG_MAPPED_INDEX *buffer = objects[i];
            buffer->lock_flags &= ~D9WG_BUFFER_LOCK_DISCARD;
        } else {
            D9WG_MAPPED_VERTEX *buffer = objects[i];
            buffer->lock_flags &= ~D9WG_BUFFER_LOCK_DISCARD;
        }
    }
    return result;
}
#undef D9WG_MAPPED_DEVICE
#undef D9WG_MAPPED_VERTEX
#undef D9WG_MAPPED_INDEX
#endif
