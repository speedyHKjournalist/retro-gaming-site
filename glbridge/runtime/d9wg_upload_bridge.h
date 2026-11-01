#ifndef D9WG_UPLOAD_BRIDGE_H
#define D9WG_UPLOAD_BRIDGE_H

/* Included after each frontend's transport primitives. Only the reserve
 * call shape differs; payload packing and copies are shared here. */
static int emit_buffer_chunk_locked(void *context, uint32_t destination_offset,
        const uint8_t *data, uint32_t byte_count, uint32_t lock_flags)
{
    D9WGUpdateBuffer update;
    uint8_t *payload, *blob;
    if (!D9WG_RESERVE_UPLOAD(D9WG_OP_UPDATE_BUFFER, sizeof(update), byte_count, &payload, &blob))
        return FALSE;
    ZeroMemory(&update, sizeof(update));
    update.resource_handle = *(const uint32_t *)context;
    update.destination_offset = destination_offset;
    update.byte_count = byte_count;
    update.data_offset = (uint32_t)(blob - batch_base());
    update.lock_flags = lock_flags;
    CopyMemory(payload, &update, sizeof(update));
    if (byte_count) CopyMemory(blob, data, byte_count);
    return TRUE;
}

static BOOL emit_buffer_update(uint32_t handle, uint32_t destination_offset,
        const void *data, uint32_t byte_count, uint32_t lock_flags)
{
    BOOL result = FALSE;
    EnterCriticalSection(&g_transport_lock);
    if (open_transport_locked()) {
        uint32_t limit = d9wg_buffer_upload_limit(batch_capacity(),
                sizeof(D9WGBatchHeader), sizeof(D9WGCommandHeader), sizeof(D9WGUpdateBuffer));
        result = d9wg_upload_buffer(&handle, destination_offset, data, byte_count,
                lock_flags, D9WG_DISCARD_FLAG, limit, emit_buffer_chunk_locked);
    }
    LeaveCriticalSection(&g_transport_lock);
    return result;
}

static int emit_texture_band_locked(void *context, const D9WGTextureBand *band)
{
    const D9WGTextureRows *texture = band->texture;
    D9WGUpdateTexture update;
    uint8_t *payload, *blob;
    uint32_t row;
    (void)context;
    if (!D9WG_RESERVE_UPLOAD(D9WG_OP_UPDATE_TEXTURE, sizeof(update), band->byte_count, &payload, &blob))
        return FALSE;
    ZeroMemory(&update, sizeof(update));
    update.resource_handle = texture->resource_handle;
    update.level = texture->level;
    update.x = texture->x;update.y = band->y;update.z = texture->z;
    update.width = texture->width;update.height = band->height;update.depth = 1;
    update.row_pitch = texture->row_bytes;
    update.slice_pitch = band->byte_count;
    update.data_bytes = band->byte_count;
    update.data_offset = (uint32_t)(blob - batch_base());
    CopyMemory(payload, &update, sizeof(update));
    for (row = 0; row < band->row_count; ++row)
        CopyMemory(blob + row * texture->row_bytes,
                texture->source + (band->first_row + row) * texture->source_pitch,
                texture->row_bytes);
    return TRUE;
}

static BOOL emit_texture_rows(const D9WGTextureRows *texture)
{
    BOOL result = FALSE;
    EnterCriticalSection(&g_transport_lock);
    if (open_transport_locked()) {
        uint32_t limit = d9wg_buffer_upload_limit(batch_capacity(),
                sizeof(D9WGBatchHeader), sizeof(D9WGCommandHeader), sizeof(D9WGUpdateTexture));
        result = d9wg_upload_texture_rows(texture, limit, emit_texture_band_locked, NULL);
    }
    LeaveCriticalSection(&g_transport_lock);
    return result;
}
#undef D9WG_RESERVE_UPLOAD
#undef D9WG_DISCARD_FLAG
#endif
