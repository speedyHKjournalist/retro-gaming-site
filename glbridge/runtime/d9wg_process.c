#include "d9wg_process.h"

#define D9WG_PROCESS_MAGIC 0x31525044u

D9WGProcess *d9wg_process_attach(void)
{
    char name[] = "D9WG.Process.v1.00000000";
    static const char hex[] = "0123456789ABCDEF";
    DWORD pid = GetCurrentProcessId();
    HANDLE mapping;
    D9WGProcess *view, *canonical;
    uint32_t index;
    for (index = 0; index < 8; ++index)
        name[sizeof(name) - 2u - index] = hex[(pid >> (index * 4u)) & 15u];
    mapping = CreateFileMappingA(INVALID_HANDLE_VALUE, NULL, PAGE_READWRITE,
            0, sizeof(D9WGProcess), name);
    if (!mapping) return NULL;
    view = MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(*view));
    if (!view) { CloseHandle(mapping);return NULL; }
    if (!view->magic) {
        FILETIME time;
        LARGE_INTEGER counter;
        uint8_t *bytes = (uint8_t *)view;
        for (index = 0; index < sizeof(*view); ++index) bytes[index] = 0;
        view->bytes = sizeof(*view);
        view->process_id = pid;
        view->canonical = view;
        view->mapping_handle = mapping;
        view->references = 1;
        view->transport = INVALID_HANDLE_VALUE;
        view->frame_id = view->sequence = 1;
        view->next_handle = 1u << 20;
        view->next_shader_handle = D9WG_SHADER_HANDLE_BASE;
        GetSystemTimeAsFileTime(&time);
        if (!QueryPerformanceCounter(&counter)) {
            counter.LowPart = GetTickCount();counter.HighPart = pid;
        }
        view->session_id_low = time.dwLowDateTime ^ counter.LowPart ^ pid;
        view->session_id_high = time.dwHighDateTime ^ (uint32_t)counter.HighPart ^ GetTickCount();
        if (!view->session_id_low && !view->session_id_high) view->session_id_high = D9WG_PROCESS_MAGIC;
        InitializeCriticalSection(&view->transport_lock);
        InitializeCriticalSection(&view->readback_lock);
        view->magic = D9WG_PROCESS_MAGIC;
        return view;
    }
    if (view->magic != D9WG_PROCESS_MAGIC || view->bytes != sizeof(*view) ||
            view->process_id != pid || !view->canonical) {
        UnmapViewOfFile(view);CloseHandle(mapping);return NULL;
    }
    canonical = view->canonical;
    InterlockedIncrement(&canonical->references);
    UnmapViewOfFile(view);
    CloseHandle(mapping);
    return canonical;
}

void d9wg_process_release(D9WGProcess *process)
{
    HANDLE mapping;
    if (!process || InterlockedDecrement(&process->references)) return;
    /* The last frontend has already drained/closed the transport, outside
     * these bookkeeping operations. No pointer into a frontend is retained. */
    mapping = process->mapping_handle;
    DeleteCriticalSection(&process->readback_lock);
    DeleteCriticalSection(&process->transport_lock);
    process->magic = 0;
    process->canonical = NULL;
    UnmapViewOfFile(process);
    CloseHandle(mapping);
}
