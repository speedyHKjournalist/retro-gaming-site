#ifndef D9WG_PROCESS_ALIASES_H
#define D9WG_PROCESS_ALIASES_H
#include "d9wg_process.h"
static D9WGProcess *g_process;
#define g_transport (g_process->transport)
#define g_dma_buffer (g_process->dma_buffer)
#define g_dma_capacity (g_process->dma_capacity)
#define g_batch_bytes (g_process->batch_bytes)
#define g_command_count (g_process->command_count)
#define g_frame_id (g_process->frame_id)
#define g_sequence (g_process->sequence)
#define g_next_handle (g_process->next_handle)
#define g_next_shader_handle (g_process->next_shader_handle)
#define g_session_id_low (g_process->session_id_low)
#define g_session_id_high (g_process->session_id_high)
#define g_transport_generation (g_process->transport_generation)
#define g_transport_retry_tick (g_process->retry_tick)
#define g_transport_retry_armed (g_process->retry_armed)
#define g_batches_dropped (g_process->batches_dropped)
#define g_transport_lock (g_process->transport_lock)
#define g_readback_lock (g_process->readback_lock)
#define g_query_slots (g_process->query_slots)
#define g_response_request_id (g_process->response_request_id)
#define g_next_request_id (g_process->response_request_id)
#endif
