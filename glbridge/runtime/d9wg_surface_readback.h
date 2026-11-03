#ifndef D9WG_SURFACE_READBACK_H
#define D9WG_SURFACE_READBACK_H
#include "d9wg_transport.h"
#define D9WG_READBACK_OK 0
#define D9WG_READBACK_INVALID 1
#define D9WG_READBACK_TRANSPORT 2
#define D9WG_READBACK_RESPONSE 3
#define D9WG_READBACK_SILENCE_MS 6000u

typedef struct D9WGReadbackDiagnostic {
    uint32_t first_row, rows_per_chunk, request_id, response_id;
    uint32_t status, expected_bytes, actual_bytes, silent_ms, heartbeat;
} D9WGReadbackDiagnostic;

/* Read complete linear texel rows into caller-owned storage. The request supplies
 * surface identity, dimensions and destination pitch; range/id/response fields
 * are generated here. Accepted chunks remain copied on a later failure.
 * Locks are acquired here in readback->transport order. The transport lock
 * pins the response mapping throughout every wait; the host replies without
 * acquiring either guest lock. The flush callback must return actual transport
 * acceptance, including capacity flushes. Only host silence, not backlog,
 * times out. */
int d9wg_surface_readback(D9WGProcess* process,
    const D9WGReadbackSurface* request, uint32_t row_bytes,
    uint32_t row_count, uint8_t* destination, uint32_t destination_bytes,
    BOOL (*flush)(BOOL), D9WGReadbackDiagnostic* diagnostic);
#endif
