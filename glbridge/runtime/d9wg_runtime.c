#include "d9wg_runtime.h"

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
