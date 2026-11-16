#ifndef D9WG_PROCESS_H
#define D9WG_PROCESS_H
#include <windows.h>
#include <stdint.h>
#include "../d3d9proxy/d3d9_protocol.h"

/* Process-local state shared by separately linked frontend DLLs. The canonical
 * mapping stays alive until the last DLL detaches, even if its creator unloads.
 * Attach/release are called only from serialized DLL process notifications. */
typedef struct D9WGProcess {
    uint32_t magic, bytes, process_id;
    struct D9WGProcess *canonical;
    HANDLE mapping_handle;
    LONG references;
    CRITICAL_SECTION transport_lock, readback_lock;
    HANDLE transport;
    uint8_t *dma_buffer;
    uint32_t dma_capacity, batch_bytes, command_count, frame_id, sequence;
    uint32_t next_handle, next_shader_handle, session_id_low, session_id_high;
    uint32_t transport_generation;
    DWORD retry_tick, batches_dropped;
    BOOL retry_armed;
    DWORD caps_retry_tick;
    BOOL caps_retry_armed;
    volatile LONG query_slots[D9WG_QUERY_SLOT_COUNT];
    volatile LONG response_request_id;
} D9WGProcess;

D9WGProcess *d9wg_process_attach(void);
void d9wg_process_release(D9WGProcess *process);
#endif
