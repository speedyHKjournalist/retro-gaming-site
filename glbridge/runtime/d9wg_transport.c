#include "d9wg_transport.h"
#include "d9wg_runtime.h"
#include "../openglproxy/v86gl_ioctl.h"

#define D9WG_OUTER_HEADER 8u

uint8_t *d9wg_transport_batch(D9WGProcess *process)
{
    return process->dma_buffer + sizeof(V86GLDMADesc) + D9WG_OUTER_HEADER;
}

uint32_t d9wg_transport_capacity(const D9WGProcess *process)
{
    uint32_t overhead = sizeof(V86GLDMADesc) + D9WG_OUTER_HEADER + D9WG_RESPONSE_REGION_BYTES;
    return process->dma_capacity > overhead ? process->dma_capacity - overhead : 0;
}

uint8_t *d9wg_transport_response(D9WGProcess *process)
{
    return process->dma_buffer && process->dma_capacity >= D9WG_RESPONSE_REGION_BYTES
        ? process->dma_buffer + process->dma_capacity - D9WG_RESPONSE_REGION_BYTES : NULL;
}

void d9wg_transport_reset_batch(D9WGProcess *process)
{
    D9WGBatchHeader *header;
    process->batch_bytes = sizeof(D9WGBatchHeader);
    process->command_count = 0;
    if (!process->dma_buffer || d9wg_transport_capacity(process) < sizeof(*header)) return;
    header = (D9WGBatchHeader *)d9wg_transport_batch(process);
    ZeroMemory(header, sizeof(*header));
    header->magic = D9WG_MAGIC;
    header->version_major = D9WG_VERSION_MAJOR;
    header->version_minor = D9WG_VERSION_MINOR;
    header->frame_id = process->frame_id;
    header->session_id_low = process->session_id_low;
    header->session_id_high = process->session_id_high;
}

void d9wg_transport_close(D9WGProcess *process)
{
    DWORD returned = 0;
    if (process->transport != INVALID_HANDLE_VALUE) {
        if (process->dma_buffer)
            DeviceIoControl(process->transport, V86GL_IOCTL_UNMAP_BUFFER, NULL, 0, NULL, 0, &returned, NULL);
        CloseHandle(process->transport);
    }
    process->transport = INVALID_HANDLE_VALUE;
    process->dma_buffer = NULL;process->dma_capacity = 0;
    process->batch_bytes = process->command_count = 0;
}

BOOL d9wg_transport_open(D9WGProcess *process)
{
    return d9wg_transport_open_report(process, NULL);
}

BOOL d9wg_transport_open_report(D9WGProcess *process, uint32_t *stage)
{
    V86GLMapBuffer mapping;
    DWORD returned = 0, error;
    if (stage) *stage = D9WG_OPEN_READY;
    if (process->dma_buffer) return TRUE;
    if (!d9wg_retry_ready(GetTickCount(), process->retry_tick, process->retry_armed)) {
        if (stage) *stage = D9WG_OPEN_DEFERRED;
        return FALSE;
    }
    process->retry_tick = GetTickCount();process->retry_armed = TRUE;
    process->transport = CreateFileA(V86GL_DEVICE_DOS_NAME, GENERIC_READ | GENERIC_WRITE,
            0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (process->transport == INVALID_HANDLE_VALUE) {
        if (stage) *stage = D9WG_OPEN_FAILED;
        return FALSE;
    }
    ZeroMemory(&mapping, sizeof(mapping));
    if (!DeviceIoControl(process->transport, V86GL_IOCTL_MAP_D3D_BUFFER, NULL, 0,
            &mapping, sizeof(mapping), &returned, NULL) &&
            !((GetLastError() == ERROR_INVALID_FUNCTION || GetLastError() == ERROR_NOT_SUPPORTED) &&
              DeviceIoControl(process->transport, V86GL_IOCTL_MAP_BUFFER, NULL, 0,
                  &mapping, sizeof(mapping), &returned, NULL))) {
        error = GetLastError();d9wg_transport_close(process);SetLastError(error);
        if (stage) *stage = D9WG_MAP_FAILED;
        return FALSE;
    }
    if (returned != sizeof(mapping) || !mapping.user_address || mapping.buffer_bytes <
            sizeof(V86GLDMADesc) + D9WG_OUTER_HEADER + sizeof(D9WGBatchHeader) +
            sizeof(D9WGCommandHeader) + D9WG_RESPONSE_REGION_BYTES) {
        d9wg_transport_close(process);SetLastError(ERROR_INVALID_DATA);
        if (stage) *stage = D9WG_MAP_FAILED;
        return FALSE;
    }
    process->dma_buffer = (uint8_t *)(uintptr_t)mapping.user_address;
    process->dma_capacity = mapping.buffer_bytes;
    process->retry_armed = FALSE;
    ++process->transport_generation;
    ZeroMemory(d9wg_transport_response(process), D9WG_RESPONSE_REGION_BYTES);
    d9wg_transport_reset_batch(process);
    return TRUE;
}

BOOL d9wg_transport_submit(D9WGProcess *process, BOOL present)
{
    V86GLDMADesc *descriptor;
    D9WGBatchHeader *batch;
    V86GLSubmit submit;
    uint8_t *outer;
    uint32_t bytes;
    DWORD returned = 0, error;
    if (!process->dma_buffer || process->batch_bytes < sizeof(D9WGBatchHeader) ||
            process->batch_bytes > d9wg_transport_capacity(process)) return FALSE;
    if (!process->command_count) return TRUE;
    batch = (D9WGBatchHeader *)d9wg_transport_batch(process);
    batch->frame_id = process->frame_id;
    batch->flags = present ? D9WG_BATCH_FLAG_PRESENT : 0;
    batch->command_count = process->command_count;
    batch->command_bytes = process->batch_bytes - sizeof(*batch);
    batch->session_id_low = process->session_id_low;
    batch->session_id_high = process->session_id_high;
    outer = process->dma_buffer + sizeof(*descriptor);
    outer[0] = (uint8_t)(V86GL_CTRL_D3D9_BATCH & 0xffu);
    outer[1] = (uint8_t)(V86GL_CTRL_D3D9_BATCH >> 8);
    outer[2] = outer[3] = 0xff;
    bytes = process->batch_bytes;
    outer[4] = (uint8_t)bytes;outer[5] = (uint8_t)(bytes >> 8);
    outer[6] = (uint8_t)(bytes >> 16);outer[7] = (uint8_t)(bytes >> 24);
    descriptor = (V86GLDMADesc *)process->dma_buffer;
    descriptor->magic = V86GL_MAGIC;descriptor->version = V86GL_VERSION;
    descriptor->flags = 0;descriptor->frame_id = process->frame_id;
    descriptor->command_count = 1;descriptor->command_bytes = D9WG_OUTER_HEADER + bytes;
    descriptor->reserved0 = D9WG_MAGIC;descriptor->reserved1 = 0;
    submit.descriptor_bytes = sizeof(*descriptor) + descriptor->command_bytes;
    submit.flags = 0;
    if (!DeviceIoControl(process->transport, V86GL_IOCTL_SUBMIT, &submit, sizeof(submit),
            NULL, 0, &returned, NULL)) {
        error = GetLastError();++process->batches_dropped;
        d9wg_transport_reset_batch(process);SetLastError(error);return FALSE;
    }
    if (present) ++process->frame_id;
    d9wg_transport_reset_batch(process);
    return TRUE;
}

BOOL d9wg_transport_reserve(D9WGProcess *process, uint16_t opcode,
        uint32_t payload_bytes, uint32_t extra_bytes, D9WGCommandHeader **command_out,
        uint8_t **payload_out, uint8_t **extra_out, BOOL (*flush)(BOOL))
{
    D9WGCommandHeader *command;
    uint32_t size, capacity = d9wg_transport_capacity(process);
    if (!process->dma_buffer || capacity < sizeof(D9WGBatchHeader) ||
            process->batch_bytes < sizeof(D9WGBatchHeader) ||
            extra_bytes > UINT32_MAX - sizeof(*command) ||
            payload_bytes > UINT32_MAX - sizeof(*command) - extra_bytes)
        return FALSE;
    size = sizeof(*command) + payload_bytes + extra_bytes;
    if (size > UINT32_MAX - 7u) return FALSE;
    size = D9WG_ALIGN8(size);
    if (size > capacity - sizeof(D9WGBatchHeader)) return FALSE;
    if (process->batch_bytes > capacity - size) {
        uint32_t after;
        if (!flush || !flush(FALSE) || !process->dma_buffer) return FALSE;
        after = d9wg_transport_capacity(process);
        if (after < sizeof(D9WGBatchHeader) || size > after - sizeof(D9WGBatchHeader) ||
                process->batch_bytes < sizeof(D9WGBatchHeader) || process->batch_bytes > after - size)
            return FALSE;
    }
    command = (D9WGCommandHeader *)(d9wg_transport_batch(process) + process->batch_bytes);
    ZeroMemory(command, size);
    command->opcode = opcode;command->size = size;command->sequence = process->sequence++;
    if (command_out) *command_out = command;
    if (payload_out) *payload_out = (uint8_t *)(command + 1);
    if (extra_out) *extra_out = (uint8_t *)(command + 1) + payload_bytes;
    process->batch_bytes += size;++process->command_count;
    return TRUE;
}
