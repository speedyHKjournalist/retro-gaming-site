#include "d9wg_runtime.h"

static uint32_t texture_copy_max_dimension(const D9WGTextureCopyExtent *extent)
{
    uint32_t value = extent->width > extent->height ? extent->width : extent->height;
    return value > extent->depth ? value : extent->depth;
}

int d9wg_texture_copy_plan(const D9WGTextureCopyExtent *source,
        const D9WGTextureCopyExtent *destination, D9WGTextureCopyPlan *plan)
{
    if (!source || !destination || !plan || !source->width || !source->height || !source->depth ||
            !destination->width || !destination->height || !destination->depth ||
            !source->levels || source->levels > 32 || !destination->levels || destination->levels > 32) return 0;
    uint32_t size = texture_copy_max_dimension(source), target = texture_copy_max_dimension(destination), skip = 0;
    while (size > target) { size >>= 1;++skip; }
    uint32_t width = source->width >> skip, height = source->height >> skip, depth = source->depth >> skip;
    if (!width) width = 1;
    if (!height) height = 1;
    if (!depth) depth = 1;
    if (width != destination->width || height != destination->height || depth != destination->depth) return 0;
    uint32_t levels = skip < source->levels ? source->levels - skip : 0;
    if (levels > destination->levels) levels = destination->levels;
    plan->source_first = skip;plan->levels = levels;
    return 1;
}

int d9wg_texture_copy_levels(const D9WGTextureCopyPlan *plan, uint32_t layers,
        D9WGTextureCopyLevel validate, D9WGTextureCopyLevel prepare,
        D9WGTextureCopyLevel copy, void *context)
{
    if (!plan || !layers || layers > 6 || plan->source_first >= 32 ||
            plan->levels > 32u - plan->source_first) return D9WG_TEXTURE_COPY_INVALID;
    if (!plan->levels) return D9WG_TEXTURE_COPY_OK;
    if (!validate || !copy) return D9WG_TEXTURE_COPY_INVALID;
    for (uint32_t pass = 0; pass < 3; ++pass) {
        D9WGTextureCopyLevel callback = pass == 0 ? validate : pass == 1 ? prepare : copy;
        if (!callback) continue;
        for (uint32_t layer = 0; layer < layers; ++layer) {
            for (uint32_t level = 0; level < plan->levels; ++level) {
                int result = callback(context, layer, plan->source_first + level, level);
                if (result) return result;
            }
        }
    }
    return D9WG_TEXTURE_COPY_OK;
}

int d9wg_retry_ready(uint32_t now, uint32_t failed_at, int armed)
{
    /* GetTickCount is a wrapping DWORD on XP. Unsigned subtraction preserves
     * the short backoff across its 49.7-day rollover. */
    return !armed || (uint32_t)(now - failed_at) >= D9WG_RUNTIME_RETRY_MS;
}

uint32_t d9wg_buffer_upload_limit(uint32_t capacity, uint32_t batch_header,
        uint32_t command_header, uint32_t payload)
{
    uint32_t room, record_header;
    if (capacity < batch_header || command_header > UINT32_MAX - payload)
        return 0;
    room = (capacity - batch_header) & ~7u;
    record_header = command_header + payload;
    return room > record_header ? room - record_header : 0;
}

int d9wg_upload_buffer(void *context, uint32_t destination_offset,
        const void *data, uint32_t byte_count, uint32_t flags,
        uint32_t discard_mask, uint32_t chunk_limit, D9WGEmitBufferChunk emit)
{
    const uint8_t *source = data;
    uint32_t done = 0;
    if (!emit || byte_count > UINT32_MAX - destination_offset ||
            (byte_count && (!source || !chunk_limit)))
        return 0;
    if (!byte_count) return emit(context, destination_offset, source, 0, flags);
    while (done < byte_count) {
        uint32_t count = byte_count - done;
        if (count > chunk_limit) count = chunk_limit;
        if (!emit(context, destination_offset + done, source + done, count,
                done ? flags & ~discard_mask : flags))
            return 0;
        done += count;
    }
    return 1;
}

int d9wg_upload_texture_rows(const D9WGTextureRows *texture,
        uint32_t blob_limit, D9WGEmitTextureBand emit, void *context)
{
    uint32_t first = 0, rows_per_band, span;
    if (!texture || !emit) return 0;
    if (!texture->width || !texture->height) return 1;
    if (!texture->source || !texture->row_bytes || !texture->row_count ||
            !texture->block_height || texture->source_pitch < texture->row_bytes ||
            texture->row_count > UINT32_MAX / texture->row_bytes ||
            texture->row_count > UINT32_MAX / texture->block_height ||
            texture->row_count - 1u > (UINT32_MAX - texture->row_bytes) / texture->source_pitch ||
            texture->x > UINT32_MAX - texture->width || texture->y > UINT32_MAX - texture->height)
        return 0;
    span = texture->row_count * texture->block_height;
    if (texture->height > span || texture->height <= span - texture->block_height)
        return 0;
    rows_per_band = blob_limit / texture->row_bytes;
    if (!rows_per_band) return 0;
    while (first < texture->row_count) {
        D9WGTextureBand band;
        uint32_t top = first * texture->block_height;
        band.texture = texture;
        band.first_row = first;
        band.row_count = texture->row_count - first;
        if (band.row_count > rows_per_band) band.row_count = rows_per_band;
        band.y = texture->y + top;
        band.height = band.row_count * texture->block_height;
        if (band.height > texture->height - top) band.height = texture->height - top;
        band.byte_count = band.row_count * texture->row_bytes;
        if (!emit(context, &band)) return 0;
        first += band.row_count;
    }
    return 1;
}
