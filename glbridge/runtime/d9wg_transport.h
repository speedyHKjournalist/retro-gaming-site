#ifndef D9WG_TRANSPORT_H
#define D9WG_TRANSPORT_H
#include "d9wg_process.h"

/* All operations require the process transport lock. A flush callback is
 * synchronous and never retained; frontend HRESULT/drop policy stays local. */
uint8_t *d9wg_transport_batch(D9WGProcess *process);
uint32_t d9wg_transport_capacity(const D9WGProcess *process);
uint8_t *d9wg_transport_response(D9WGProcess *process);
void d9wg_transport_reset_batch(D9WGProcess *process);
void d9wg_transport_close(D9WGProcess *process);
BOOL d9wg_transport_open(D9WGProcess *process);
#define D9WG_OPEN_READY 0u
#define D9WG_OPEN_DEFERRED 1u
#define D9WG_OPEN_FAILED 2u
#define D9WG_MAP_FAILED 3u
BOOL d9wg_transport_open_report(D9WGProcess *process, uint32_t *stage);
BOOL d9wg_transport_submit(D9WGProcess *process, BOOL present);
BOOL d9wg_transport_reserve(D9WGProcess *process, uint16_t opcode,
        uint32_t payload_bytes, uint32_t extra_bytes, D9WGCommandHeader **command,
        uint8_t **payload, uint8_t **extra, BOOL (*flush)(BOOL));
#endif
