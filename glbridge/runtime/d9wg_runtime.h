#ifndef D9WG_RUNTIME_H
#define D9WG_RUNTIME_H

#include <stdint.h>

/* Shared guest-side pure mechanics. No Win32, CRT or API object ownership. */
#define D9WG_RUNTIME_RETRY_MS 1000u

int d9wg_retry_ready(uint32_t now, uint32_t failed_at, int armed);

/* Maximum blob in one D9WG record, including the protocol's 8-byte padding. */
uint32_t d9wg_buffer_upload_limit(uint32_t capacity, uint32_t batch_header,
        uint32_t command_header, uint32_t payload);

typedef int (*D9WGEmitBufferChunk)(void *context, uint32_t destination_offset,
        const uint8_t *data, uint32_t byte_count, uint32_t flags);

/* Caller holds its transport lock across the complete upload. A callback
 * failure stops at the accepted prefix; no ambiguous command is retried.
 * DISCARD is a property of the logical lock and is emitted only once. */
int d9wg_upload_buffer(void *context, uint32_t destination_offset,
        const void *data, uint32_t byte_count, uint32_t flags,
        uint32_t discard_mask, uint32_t chunk_limit, D9WGEmitBufferChunk emit);

#endif
