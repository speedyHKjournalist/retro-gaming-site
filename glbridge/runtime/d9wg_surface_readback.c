#include "d9wg_surface_readback.h"

int d9wg_surface_readback(D9WGProcess* process,
    const D9WGReadbackSurface* request, uint32_t row_bytes,
    uint32_t row_count, uint8_t* destination, uint32_t destination_bytes,
    BOOL (*flush)(BOOL), D9WGReadbackDiagnostic* diagnostic)
{
    D9WGReadbackDiagnostic detail = { 0 };
    int result = D9WG_READBACK_INVALID;
    if (!process || !request || !destination || !flush || !request->width || !request->height || !row_bytes || !row_count || row_count > request->height || request->destination_pitch < row_bytes || row_count > destination_bytes / request->destination_pitch)
        goto done;
    uint32_t capacity = D9WG_RESPONSE_REGION_BYTES - D9WG_READBACK_REGION_OFFSET - (uint32_t)sizeof(D9WGReadbackResponse) - D9WG_HEARTBEAT_BYTES;
    detail.rows_per_chunk = capacity / request->destination_pitch;
    if (!detail.rows_per_chunk)
        goto done;

    EnterCriticalSection(&process->readback_lock);
    EnterCriticalSection(&process->transport_lock);
    result = D9WG_READBACK_TRANSPORT;
    if (!d9wg_transport_open(process))
        goto unlock;
    uint8_t* base = d9wg_transport_response(process);
    if (!base)
        goto unlock;
    volatile D9WGReadbackResponse* response = (volatile D9WGReadbackResponse*)(base + D9WG_READBACK_REGION_OFFSET);
    volatile uint32_t* heartbeat = (volatile uint32_t*)(base + D9WG_HEARTBEAT_OFFSET);
    while (detail.first_row < row_count) {
        D9WGReadbackSurface command = *request;
        uint32_t rows = row_count - detail.first_row;
        if (rows > detail.rows_per_chunk)
            rows = detail.rows_per_chunk;
        LONG id = InterlockedIncrement(&process->response_request_id);
        if (!id)
            id = InterlockedIncrement(&process->response_request_id);
        detail.request_id = (uint32_t)id;
        detail.expected_bytes = rows * request->destination_pitch;
        response->request_id = detail.request_id;
        response->byte_count = 0;
        response->reserved = 0;
        response->status = D9WG_RESPONSE_PENDING;
        MemoryBarrier();
        command.first_row = detail.first_row;
        command.row_count = rows;
        command.destination_bytes = detail.expected_bytes;
        command.response_offset = D9WG_READBACK_REGION_OFFSET;
        command.request_id = detail.request_id;
        uint8_t* payload;
        if (!d9wg_transport_reserve(process, D9WG_OP_READBACK_SURFACE,
                sizeof(command), 0, NULL, &payload, NULL, flush))
            goto unlock;
        __builtin_memcpy(payload, &command, sizeof(command));
        if (!flush(FALSE))
            goto unlock;

        DWORD start = GetTickCount();
        detail.heartbeat = *heartbeat;
        for (;;) {
            MemoryBarrier();
            if (response->status != D9WG_RESPONSE_PENDING)
                break;
            uint32_t beat = *heartbeat;
            if (beat != detail.heartbeat) {
                detail.heartbeat = beat;
                start = GetTickCount();
            }
            if ((DWORD)(GetTickCount() - start) >= D9WG_READBACK_SILENCE_MS)
                break;
            Sleep(1);
        }
        MemoryBarrier();
        detail.response_id = response->request_id;
        detail.status = response->status;
        detail.actual_bytes = response->byte_count;
        detail.silent_ms = (DWORD)(GetTickCount() - start);
        result = D9WG_READBACK_RESPONSE;
        if (detail.response_id != detail.request_id || detail.status != D9WG_RESPONSE_OK || detail.actual_bytes != detail.expected_bytes)
            goto unlock;
        __builtin_memcpy(destination + detail.first_row * request->destination_pitch,
            base + D9WG_READBACK_REGION_OFFSET + sizeof(D9WGReadbackResponse), detail.expected_bytes);
        detail.first_row += rows;
        result = D9WG_READBACK_TRANSPORT;
    }
    result = D9WG_READBACK_OK;
unlock:
    LeaveCriticalSection(&process->transport_lock);
    LeaveCriticalSection(&process->readback_lock);
done:
    if (diagnostic)
        *diagnostic = detail;
    return result;
}
