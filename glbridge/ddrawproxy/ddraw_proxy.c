/*
 * App-local DirectDraw / Direct3D 1-7 frontend for Windows XP guests in v86.
 *
 * Independent from d3d8.dll and d3d9.dll as a deployment -- a game directory
 * holds exactly one proxy -- but not as an implementation: this DLL emits the
 * same D9WG command stream on the same V86GL_CTRL_D3D9_BATCH transport, and
 * ../d3d9-webgpu/d3d9_executor.js (plus its ddraw_ops.js mixin) executes it.
 * ddraw_protocol.h is the translation layer and enumerates every place the
 * legacy APIs genuinely differ from Direct3D 9.
 *
 * Current scope: the DirectDraw 2D path through IDirectDraw7 --
 * cooperative level and display mode, surface creation including flip chains
 * and attached buffers, Lock/Unlock over system-memory shadows with dirty-rect
 * uploads, Blt/BltFast with source colour keys, mirroring and fills, Flip,
 * palettes (kept as GPU-side indices), clippers resolved into per-rectangle
 * blits, and GDI GetDC/ReleaseDC over a DIB section.
 *
 * Direct3D 1-7 factories, legacy object-model views, execute buffers, fixed-
 * function state, UP/VB draw paths and vertex buffers share those surfaces and
 * the same host device. DirectDraw overlays are composited at present time;
 * video ports remain a separate, explicitly refused API family.
 * Each refusal names itself through D9WG_OP_GUEST_LOG so a missing picture can
 * be told apart from a picture drawn wrongly.
 *
 * The discipline throughout matches the sibling proxies: an entry point either
 * does what DirectDraw says or fails, GetCaps only claims what is really
 * implemented, and every approximation is written down in README.md.
 */

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define CINTERFACE
#include <windows.h>
#include <initguid.h>
#include <ddraw.h>
#include <d3d.h>
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>
#include "../openglproxy/v86gl_ioctl.h"
#include "ddraw_protocol.h"

/* The legacy DirectX SDK declared these in auxiliary headers; current MinGW
 * intentionally omits them although the COM methods still take the values. */
#ifndef DDOVERZ_SENDTOFRONT
#define DDOVERZ_SENDTOFRONT 0u
#define DDOVERZ_SENDTOBACK 1u
#define DDOVERZ_MOVEFORWARD 2u
#define DDOVERZ_MOVEBACKWARD 3u
#define DDOVERZ_INSERTINFRONTOF 4u
#define DDOVERZ_INSERTINBACKOF 5u
#endif
#ifndef DDENUMOVERLAYZ_BACKTOFRONT
#define DDENUMOVERLAYZ_BACKTOFRONT 0u
#define DDENUMOVERLAYZ_FRONTTOBACK 1u
#endif

#define DDWG_LOG_PREFIX "[ddraw-webgpu] "
#ifdef DDWG_DIAGNOSTIC_TRACE
#define V86WG_DIAGNOSTIC_COMPONENT "ddraw-webgpu"
#define V86WG_DIAGNOSTIC_FILE_STEM "ddraw_trace"
#include "../diagnostic_trace.h"
#define DDWG_TRACE_ERROR(result) \
    v86wg_diagnostic_hresult((HRESULT)(result), __func__, __LINE__)
#define DDWG_TRACE_RESULT(result) \
    v86wg_diagnostic_result((HRESULT)(result), __func__, __LINE__)
#define DDWG_TRACE(...) v86wg_diagnostic_write(__VA_ARGS__)
#define DDWG_TRACE_CHECKPOINT(...) do { \
    v86wg_diagnostic_write(__VA_ARGS__); \
    v86wg_diagnostic_flush(); \
} while (0)
#else
#define DDWG_TRACE_ERROR(result) (result)
#define DDWG_TRACE_RESULT(result) (result)
#define DDWG_TRACE(...) ((void)0)
#define DDWG_TRACE_CHECKPOINT(...) ((void)0)
#endif

/* The VGL2 record header the batch rides inside: function code, 0xFFFF marker,
 * and the extended payload size. Identical to the D3D8/D3D9 proxies. */
#define DDWG_VGL2_RECORD_HEADER_BYTES 8u

/* Handles start high enough that a stale handle from a previous process shows
 * up as an unknown handle on the host rather than colliding with a live one. */
#define DDWG_HANDLE_GENERATION_ONE (1u << 22)

/* One device per DirectDraw object. DirectDraw has no device concept of its
 * own, but D9WG needs one to hang the swap-chain image and the display mode
 * off, so the DirectDraw object is it. */
#define DDWG_MAX_SURFACES 4096u
#define DDWG_MAX_OVERLAYS 64u

static HANDLE g_transport = INVALID_HANDLE_VALUE;
static uint8_t *g_dma_buffer;
static uint32_t g_dma_capacity;
static uint32_t g_batch_bytes;
static uint32_t g_command_count;
static uint32_t g_frame_id = 1;
static uint32_t g_sequence = 1;
static uint32_t g_next_handle = DDWG_HANDLE_GENERATION_ONE;
static uint32_t g_session_id_low;
static uint32_t g_session_id_high;
/*
 * The transport is retried, not latched off. v86gl.sys is a demand-start
 * service: an application that reaches DirectDraw before "sc start v86gl" has
 * run -- or while another process still owns the single 16 MiB mapping the
 * driver hands out -- must not be left permanently blind once the device
 * becomes available. Every successful open bumps a generation, and the device,
 * the palettes and the surface storage are recreated lazily against it.
 */
#define DDWG_TRANSPORT_RETRY_MS 750u
#define DDWG_LOG_FILE_MAX_LINES 4096u

static DWORD g_transport_retry_tick;    /* GetTickCount at the last failure */
static BOOL g_transport_retry_armed;
static uint32_t g_transport_generation; /* incremented on every open */
static uint32_t g_hello_generation;
static HINSTANCE g_instance;
static char g_log_path[MAX_PATH];
static BOOL g_log_path_resolved;
static UINT g_log_lines;
static CRITICAL_SECTION g_transport_lock;
static CRITICAL_SECTION g_object_lock;

#ifdef DDWG_DIAGNOSTIC_TRACE
static volatile LONG g_trace_blt_status_calls;
static volatile LONG g_trace_flip_status_calls;
static volatile LONG g_trace_scanline_calls;
static volatile LONG g_trace_vblank_status_calls;

/* Polling methods can run millions of times in a broken wait loop. Log the
 * first few calls and then powers of two: enough to prove where the guest is
 * stuck without turning a five-minute dxdiag hang into a multi-megabyte log. */
static void ddwg_trace_poll(volatile LONG *counter, const char *name,
        DWORD object, DWORD value0, DWORD value1)
{
    LONG count = InterlockedIncrement(counter);
    if (count <= 8 || (count & (count - 1)) == 0)
        DDWG_TRACE_CHECKPOINT("POLL name=%s count=%ld object=%08lX "
                "value0=%lu value1=%lu", name, count, object,
                value0, value1);
}
#else
#define ddwg_trace_poll(counter, name, object, value0, value1) ((void)0)
#endif

/*
 * A transport failure is the one failure this DLL cannot report through
 * D9WG_OP_GUEST_LOG, because the guest log rides the transport that just
 * failed -- which is how "the bridge is not open" reached the caller as
 * DDERR_OUTOFVIDEOMEMORY with nothing written down anywhere. OutputDebugStringA
 * needs a debugger attached, and a v86 guest running a game does not have one,
 * so every line also goes to ddraw-webgpu.log beside this DLL.
 */
static void ddwg_log_resolve_path(void)
{
    char module[MAX_PATH];
    DWORD length;
    int index;
    int cut = -1;

    g_log_path_resolved = TRUE;
    g_log_path[0] = 0;
    length = GetModuleFileNameA(g_instance, module, MAX_PATH);
    if (length && length < MAX_PATH) {
        for (index = 0; module[index]; ++index) {
            if (module[index] == '\\' || module[index] == '/')
                cut = index;
        }
        module[cut + 1] = 0;
    } else {
        length = GetTempPathA(MAX_PATH, module);
        if (!length || length >= MAX_PATH)
            return;
    }
    if (lstrlenA(module) + (int)sizeof("ddraw-webgpu.log") > MAX_PATH)
        return;
    lstrcpynA(g_log_path, module, MAX_PATH);
    lstrcatA(g_log_path, "ddraw-webgpu.log");
}

static void ddwg_log(const char *text)
{
    char body[2 * MAX_PATH + 64];
    char line[2 * MAX_PATH + 128];
    HANDLE file;
    DWORD written = 0;

    OutputDebugStringA(DDWG_LOG_PREFIX);
    OutputDebugStringA(text);
    OutputDebugStringA("\r\n");
    DDWG_TRACE("LOG %s", text);

    if (!g_log_path_resolved)
        ddwg_log_resolve_path();
    if (!g_log_path[0] || g_log_lines >= DDWG_LOG_FILE_MAX_LINES)
        return;
    ++g_log_lines;
    file = CreateFileA(g_log_path, FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        /* Read-only directory: keep the debugger sink and stop retrying. */
        g_log_path[0] = 0;
        return;
    }
    lstrcpynA(body, text, (int)sizeof(body));
    wsprintfA(line, "%lu " DDWG_LOG_PREFIX "%s\r\n", GetTickCount(), body);
    SetFilePointer(file, 0, NULL, FILE_END);
    WriteFile(file, line, (DWORD)lstrlenA(line), &written, NULL);
    CloseHandle(file);
}

/*
 * One line the first time a program reaches a DirectDraw entry point through
 * this DLL. "Is the proxy the DLL this program actually loaded?" is the first
 * question behind every report of a DirectDraw error, and app-local loading
 * makes it a real question: dxdiag.exe and a game in its own directory resolve
 * ddraw.dll from different places. It is written from the entry point rather
 * than from DllMain so no file I/O happens under the loader lock.
 */
static LONG g_banner_written;

static void ddwg_log_banner_once(void)
{
    char banner[2 * MAX_PATH + 64];
    char host[MAX_PATH];
    char self[MAX_PATH];
    DWORD length;

    if (InterlockedExchange(&g_banner_written, 1))
        return;
    length = GetModuleFileNameA(NULL, host, MAX_PATH);
    if (!length || length >= MAX_PATH)
        lstrcpynA(host, "?", MAX_PATH);
    length = GetModuleFileNameA(g_instance, self, MAX_PATH);
    if (!length || length >= MAX_PATH)
        lstrcpynA(self, "?", MAX_PATH);
    wsprintfA(banner, "%s loaded by %s", self, host);
    ddwg_log(banner);
}

static BOOL guid_equal(REFIID left, REFIID right)
{
    UINT index;
    if (!left || !right || left->Data1 != right->Data1
            || left->Data2 != right->Data2 || left->Data3 != right->Data3)
        return FALSE;
    for (index = 0; index < 8; ++index) {
        if (left->Data4[index] != right->Data4[index]) return FALSE;
    }
    return TRUE;
}

static BOOL iid_is_unknown(REFIID iid)
{
    static const IID unknown = { 0x00000000, 0x0000, 0x0000,
        { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 } };
    return guid_equal(iid, &unknown);
}

static uint32_t allocate_handle(void)
{
    uint32_t handle = (uint32_t)InterlockedIncrement((LONG *)&g_next_handle);
    if (!handle)
        handle = (uint32_t)InterlockedIncrement((LONG *)&g_next_handle);
    return handle;
}

static void initialize_session_id(HINSTANCE instance)
{
    FILETIME time;
    LARGE_INTEGER counter;
    DWORD process_id = GetCurrentProcessId();
    DWORD thread_id = GetCurrentThreadId();

    GetSystemTimeAsFileTime(&time);
    if (!QueryPerformanceCounter(&counter)) {
        counter.LowPart = GetTickCount();
        counter.HighPart = process_id ^ thread_id;
    }
    g_session_id_low = time.dwLowDateTime ^ counter.LowPart ^ process_id
            ^ (uint32_t)(uintptr_t)instance;
    g_session_id_high = time.dwHighDateTime ^ counter.HighPart
            ^ GetTickCount() ^ (thread_id * 0x9E3779B9u);
    if (!g_session_id_low && !g_session_id_high)
        g_session_id_high = 0xDDA70001u;
}

/* ------------------------------------------------------------------ *
 * Transport
 * ------------------------------------------------------------------ *
 *
 * Structurally identical to ../d3d8proxy and ../d3d9proxy: the three DLLs
 * compile separately and are deployed separately, so they each carry their own
 * copy rather than sharing a header that would tie their lifecycles together.
 */

static uint8_t *batch_base(void)
{
    return g_dma_buffer + sizeof(V86GLDMADesc) + DDWG_VGL2_RECORD_HEADER_BYTES;
}

static uint8_t *response_region(void)
{
    return g_dma_buffer + g_dma_capacity - D9WG_RESPONSE_REGION_BYTES;
}

static uint32_t batch_capacity(void)
{
    /* The last D9WG_RESPONSE_REGION_BYTES of the mapping belong to the host:
     * readback pixels and its liveness heartbeat live there, so a batch that
     * ran into them would be overwritten mid-flight. */
    if (g_dma_capacity <= D9WG_RESPONSE_REGION_BYTES + sizeof(V86GLDMADesc)
            + DDWG_VGL2_RECORD_HEADER_BYTES)
        return 0;
    return g_dma_capacity - (uint32_t)sizeof(V86GLDMADesc)
            - DDWG_VGL2_RECORD_HEADER_BYTES - D9WG_RESPONSE_REGION_BYTES;
}

static void reset_batch_locked(void)
{
    D9WGBatchHeader *header;

    g_batch_bytes = sizeof(D9WGBatchHeader);
    g_command_count = 0;
    if (!g_dma_buffer || batch_capacity() < sizeof(D9WGBatchHeader))
        return;

    header = (D9WGBatchHeader *)batch_base();
    ZeroMemory(header, sizeof(*header));
    header->magic = D9WG_MAGIC;
    header->version_major = D9WG_VERSION_MAJOR;
    header->version_minor = D9WG_VERSION_MINOR;
    header->frame_id = g_frame_id;
    header->session_id_low = g_session_id_low;
    header->session_id_high = g_session_id_high;
}

static void close_transport_locked(void)
{
    DWORD returned = 0;

    if (g_transport != INVALID_HANDLE_VALUE) {
        if (g_dma_buffer) {
            DeviceIoControl(g_transport, V86GL_IOCTL_UNMAP_BUFFER,
                    NULL, 0, NULL, 0, &returned, NULL);
        }
        CloseHandle(g_transport);
    }
    g_transport = INVALID_HANDLE_VALUE;
    g_dma_buffer = NULL;
    g_dma_capacity = 0;
    g_batch_bytes = 0;
    g_command_count = 0;
}

/* Backs the transport off for DDWG_TRANSPORT_RETRY_MS and writes the reason
 * down once per distinct reason, so a command per frame does not become an
 * open attempt per frame or a log line per frame. */
static void transport_failed_locked(const char *reason)
{
    static char last_reason[D9WG_LOG_MAX_TEXT];

    g_transport_retry_tick = GetTickCount();
    g_transport_retry_armed = TRUE;
    if (lstrcmpA(last_reason, reason) != 0) {
        lstrcpynA(last_reason, reason, (int)D9WG_LOG_MAX_TEXT);
        ddwg_log(reason);
        DDWG_TRACE_CHECKPOINT("TRANSPORT FAILURE reason=%s", reason);
    }
}

static BOOL open_transport_locked(void)
{
    V86GLMapBuffer mapping;
    char reason[D9WG_LOG_MAX_TEXT];
    DWORD returned = 0;
    DWORD error;

    if (g_dma_buffer)
        return TRUE;
    if (g_transport_retry_armed &&
            GetTickCount() - g_transport_retry_tick < DDWG_TRANSPORT_RETRY_MS)
        return FALSE;

    DDWG_TRACE_CHECKPOINT("TRANSPORT OPEN_BEGIN device=\\\\.\\v86gl");
    g_transport = CreateFileA(V86GL_DEVICE_DOS_NAME,
            GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_transport == INVALID_HANDLE_VALUE) {
        error = GetLastError();
        wsprintfA(reason, "could not open \\\\.\\v86gl (Win32 error %lu); the "
                "v86gl kernel service is not started, so no DirectDraw surface "
                "can reach the browser", error);
        transport_failed_locked(reason);
        return FALSE;
    }

    ZeroMemory(&mapping, sizeof(mapping));
    if (!DeviceIoControl(g_transport, V86GL_IOCTL_MAP_BUFFER,
            NULL, 0, &mapping, sizeof(mapping), &returned, NULL)) {
        error = GetLastError();
        wsprintfA(reason, "v86gl MAP_BUFFER failed (Win32 error %lu); error 170 "
                "means another process still owns the single 16 MiB DMA "
                "mapping", error);
        close_transport_locked();
        transport_failed_locked(reason);
        return FALSE;
    }
    if (returned != sizeof(mapping) || !mapping.user_address
            || mapping.buffer_bytes < sizeof(V86GLDMADesc)
                    + DDWG_VGL2_RECORD_HEADER_BYTES
                    + sizeof(D9WGBatchHeader) + sizeof(D9WGCommandHeader)
                    + D9WG_RESPONSE_REGION_BYTES) {
        wsprintfA(reason, "v86gl MAP_BUFFER returned an unusable mapping "
                "(bytes=%lu returned=%lu)", mapping.buffer_bytes, returned);
        close_transport_locked();
        transport_failed_locked(reason);
        return FALSE;
    }

    g_dma_buffer = (uint8_t *)(uintptr_t)mapping.user_address;
    g_dma_capacity = mapping.buffer_bytes;
    g_transport_retry_armed = FALSE;
    /*
     * Everything the host knows about belonged to the previous generation.
     * Bumping it here is what makes ensure_device, the palettes and
     * ensure_surface_storage recreate themselves on their next use instead of
     * addressing handles the host has never seen.
     */
    ++g_transport_generation;
    reset_batch_locked();
    DDWG_TRACE_CHECKPOINT("TRANSPORT OPEN_OK handle=%08lX dma=%08lX "
            "capacity=%lu generation=%lu", (DWORD)(uintptr_t)g_transport,
            (DWORD)(uintptr_t)g_dma_buffer, g_dma_capacity,
            g_transport_generation);
    return TRUE;
}

static BOOL submit_batch_locked(BOOL present)
{
    V86GLDMADesc *descriptor;
    D9WGBatchHeader *batch;
    V86GLSubmit submit;
    uint8_t *outer;
    uint32_t outer_bytes;
    DWORD returned = 0;

    if (!open_transport_locked())
        return FALSE;
    if (g_command_count == 0)
        return TRUE;

    DDWG_TRACE_CHECKPOINT("BATCH SUBMIT_BEGIN frame=%lu present=%lu "
            "commands=%lu bytes=%lu generation=%lu", g_frame_id,
            present ? 1u : 0u, g_command_count, g_batch_bytes,
            g_transport_generation);

    batch = (D9WGBatchHeader *)batch_base();
    batch->frame_id = g_frame_id;
    batch->flags = present ? D9WG_BATCH_FLAG_PRESENT : 0;
    batch->command_count = g_command_count;
    batch->command_bytes = g_batch_bytes - (uint32_t)sizeof(*batch);
    batch->session_id_low = g_session_id_low;
    batch->session_id_high = g_session_id_high;

    outer = g_dma_buffer + sizeof(V86GLDMADesc);
    outer[0] = (uint8_t)(V86GL_CTRL_D3D9_BATCH & 0xFFu);
    outer[1] = (uint8_t)(V86GL_CTRL_D3D9_BATCH >> 8);
    outer[2] = 0xFF;
    outer[3] = 0xFF;
    outer[4] = (uint8_t)(g_batch_bytes & 0xFFu);
    outer[5] = (uint8_t)((g_batch_bytes >> 8) & 0xFFu);
    outer[6] = (uint8_t)((g_batch_bytes >> 16) & 0xFFu);
    outer[7] = (uint8_t)((g_batch_bytes >> 24) & 0xFFu);

    outer_bytes = DDWG_VGL2_RECORD_HEADER_BYTES + g_batch_bytes;
    descriptor = (V86GLDMADesc *)g_dma_buffer;
    descriptor->magic = V86GL_MAGIC;
    descriptor->version = V86GL_VERSION;
    /* Present is an inner command; the GL bridge must not swap on its own. */
    descriptor->flags = 0;
    descriptor->frame_id = g_frame_id;
    descriptor->command_count = 1;
    descriptor->command_bytes = outer_bytes;
    descriptor->reserved0 = D9WG_MAGIC;
    descriptor->reserved1 = 0;

    submit.descriptor_bytes = (uint32_t)sizeof(*descriptor) + outer_bytes;
    submit.flags = 0;
    if (!DeviceIoControl(g_transport, V86GL_IOCTL_SUBMIT,
            &submit, sizeof(submit), NULL, 0, &returned, NULL)) {
        char reason[D9WG_LOG_MAX_TEXT];
        wsprintfA(reason, "D9WG batch submit failed (Win32 error %lu)",
                GetLastError());
        close_transport_locked();
        transport_failed_locked(reason);
        return FALSE;
    }

    DDWG_TRACE_CHECKPOINT("BATCH SUBMIT_OK frame=%lu present=%lu "
            "commands=%lu descriptor_bytes=%lu returned=%lu", g_frame_id,
            present ? 1u : 0u, g_command_count,
            submit.descriptor_bytes, returned);
    if (present)
        ++g_frame_id;
    reset_batch_locked();
    return TRUE;
}

static BOOL reserve_command_locked(uint16_t opcode, uint32_t payload_bytes,
        uint32_t extra_bytes, uint8_t **payload_out, uint8_t **extra_out)
{
    uint32_t raw_size;
    uint32_t record_size;
    D9WGCommandHeader *command;

    if (!open_transport_locked())
        return FALSE;
    if (payload_bytes > 0xFFFFFFFFu - sizeof(*command) - extra_bytes)
        return FALSE;
    raw_size = (uint32_t)sizeof(*command) + payload_bytes + extra_bytes;
    record_size = D9WG_ALIGN8(raw_size);
    if (batch_capacity() < sizeof(D9WGBatchHeader))
        return FALSE;
    if (record_size > batch_capacity() - sizeof(D9WGBatchHeader))
        return FALSE;
    if (g_batch_bytes + record_size > batch_capacity()
            && !submit_batch_locked(FALSE))
        return FALSE;

    command = (D9WGCommandHeader *)(batch_base() + g_batch_bytes);
    ZeroMemory(command, record_size);
    command->opcode = opcode;
    command->size = record_size;
    command->sequence = g_sequence++;
    if (payload_out)
        *payload_out = (uint8_t *)(command + 1);
    if (extra_out)
        *extra_out = (uint8_t *)(command + 1) + payload_bytes;
    g_batch_bytes += record_size;
    ++g_command_count;
    return TRUE;
}

static BOOL emit_command(uint16_t opcode, const void *payload,
        uint32_t payload_bytes)
{
    uint8_t *destination;
    BOOL result;

    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(opcode, payload_bytes, 0, &destination,
            NULL);
    if (result && payload_bytes)
        CopyMemory(destination, payload, payload_bytes);
    LeaveCriticalSection(&g_transport_lock);
    return result;
}

/* ------------------------------------------------------------------ *
 * Refusals, mirrored into the browser console
 * ------------------------------------------------------------------ *
 *
 * A DirectDraw call this DLL turns down is invisible everywhere the developer
 * can look: the host sees a clean stream of valid commands, and the guest's
 * own trace lives inside a VM whose filesystem the page cannot reach. That gap
 * is what turns "the picture is wrong" into guesswork, so every refusal says
 * so out loud -- once per distinct message, so a per-frame refusal costs one
 * line rather than one line per frame.
 */

#define DDWG_LOG_SEEN_MAX 96u

static char g_log_seen[DDWG_LOG_SEEN_MAX][D9WG_LOG_MAX_TEXT];
static UINT g_log_seen_count;
static BOOL g_log_overflowed;

static BOOL host_log_already_sent(const char *text)
{
    UINT index;
    for (index = 0; index < g_log_seen_count; ++index) {
        if (lstrcmpA(g_log_seen[index], text) == 0)
            return TRUE;
    }
    if (g_log_seen_count < DDWG_LOG_SEEN_MAX) {
        lstrcpynA(g_log_seen[g_log_seen_count], text, (int)D9WG_LOG_MAX_TEXT);
        ++g_log_seen_count;
        return FALSE;
    }
    if (g_log_overflowed)
        return TRUE;
    g_log_overflowed = TRUE;
    return FALSE;
}

static void host_log(uint32_t severity, const char *format, ...)
{
    char text[D9WG_LOG_MAX_TEXT];
    uint8_t payload[sizeof(D9WGGuestLog) + D9WG_LOG_MAX_TEXT];
    D9WGGuestLog *header = (D9WGGuestLog *)payload;
    va_list arguments;
    int length;

    va_start(arguments, format);
    wvsprintfA(text, format, arguments);
    va_end(arguments);
    if (host_log_already_sent(text))
        return;
    ddwg_log(text);
    length = lstrlenA(text);
    if (length > (int)D9WG_LOG_MAX_TEXT)
        length = (int)D9WG_LOG_MAX_TEXT;
    header->severity = severity;
    header->text_bytes = (uint32_t)length;
    CopyMemory(payload + sizeof(*header), text, (SIZE_T)length);
    emit_command(D9WG_OP_GUEST_LOG, payload,
            (uint32_t)(sizeof(*header) + (uint32_t)length));
}

#define HOSTLOG_INFO(...) host_log(D9WG_LOG_SEVERITY_INFO, __VA_ARGS__)
#define HOSTLOG_REFUSED(...) host_log(D9WG_LOG_SEVERITY_REFUSED, __VA_ARGS__)
#define HOSTLOG_FAILED(...) host_log(D9WG_LOG_SEVERITY_FAILED, __VA_ARGS__)

#define UNSUPPORTED(name, result) \
    (HOSTLOG_REFUSED("%s is not implemented and returned an error", name), \
     DDWG_TRACE_ERROR(result))

/* ------------------------------------------------------------------ *
 * Objects
 * ------------------------------------------------------------------ *
 *
 * The versioned DirectDraw interfaces (IDirectDraw..IDirectDraw7,
 * IDirectDrawSurface..IDirectDrawSurface7) are different views of one object
 * with one reference count, which is why each object carries every vtable it
 * answers to and QueryInterface hands back the same allocation. Milestone 1
 * implements the v7 view only; the earlier vtable slots exist so that adding
 * them later cannot change the object layout underneath live code.
 */

typedef struct DDrawObject DDrawObject;
typedef struct DDSurface DDSurface;
typedef struct DDPalette DDPalette;
typedef struct DDClipper DDClipper;
typedef struct D3D7Device D3D7Device;
typedef struct DDTextureStorage DDTextureStorage;
typedef struct DDSurfaceMemory DDSurfaceMemory;
typedef struct D3DLegacyViewport D3DLegacyViewport;
typedef struct D3DLegacyMaterial D3DLegacyMaterial;
typedef struct D3DLegacyLight D3DLegacyLight;
typedef struct D3DLegacyMatrix D3DLegacyMatrix;

static IDirect3D7Vtbl g_d3d7_vtbl;
static IDirect3D3Vtbl g_d3d3_vtbl;
static IDirect3D2Vtbl g_d3d2_vtbl;
static IDirect3DVtbl g_d3d1_vtbl;
static IDirect3DTexture2Vtbl g_d3d_texture2_vtbl;
static IDirect3DTextureVtbl g_d3d_texture1_vtbl;
static IDirectDrawGammaControlVtbl g_gamma_vtbl;

struct DDPalette {
    const IDirectDrawPaletteVtbl *vtbl;
    LONG ref;
    DDrawObject *owner;
    DWORD caps;
    uint32_t slot;              /* which D9WG palette slot holds the table */
    PALETTEENTRY entries[256];
    DDPalette *next;
};

struct DDClipper {
    const IDirectDrawClipperVtbl *vtbl;
    LONG ref;
    DDrawObject *owner;
    HWND window;
    RGNDATA *clip_list;         /* heap copy, or NULL when tracking a window */
    DWORD clip_list_bytes;
    BOOL changed;
    DDClipper *next;
};

/* One WebGPU texture may expose many DirectDraw surface objects: every mip
 * level and every cube face is independently Lock-able and reference-counted,
 * but all of them address one D9WG handle.  Keeping that lifetime in a shared
 * allocation prevents releasing a child surface from destroying the texture
 * underneath its siblings. */
struct DDTextureStorage {
    LONG ref;
    uint32_t handle;
    uint32_t resource_kind;
    BOOL created;
    uint32_t create_generation;  /* the transport generation it was made in */
    /*
     * The creation command, kept verbatim. DirectDraw surface memory is the
     * guest's own; the host texture is a cache of it that can be missing --
     * because the bridge was not open yet when the surface was created -- and
     * be built later without the application knowing. Replaying the recorded
     * command is how a surface stops being permanently dead just because
     * CreateSurface happened to run first.
     */
    uint16_t create_opcode;
    uint32_t create_bytes;
    union {
        D9WGCreateTexture2D texture_2d;
        D9WGCreateTextureCube texture_cube;
    } create;
};

/* DuplicateSurface creates another COM object over the same pixels, not
 * another copy of the pixels.  Mip/cube subresources share DDTextureStorage
 * but do not share their CPU bits, so the memory lifetime is deliberately a
 * second object instead of another field in DDTextureStorage. */
struct DDSurfaceMemory {
    LONG ref;
    BYTE *bits;
    SIZE_T bytes;
    BOOL owned;                 /* FALSE for SetSurfaceDesc client memory */
    BOOL gpu_dirty;             /* shared by DuplicateSurface aliases */
};

struct DDSurface {
    /*
     * Every version this object answers to, each at its own offset, because a
     * versioned DirectDraw interface is a different *view* of one object with
     * one reference count -- not a different object. The version a call came
     * through is recovered from which of these the caller's pointer names
     * (surface_from_any), which is also how a method that returns an interface
     * knows which version to hand back.
     */
    const IDirectDrawSurface7Vtbl *vtbl7;   /* first: the canonical identity */
    const IDirectDrawSurface4Vtbl *vtbl4;
    const IDirectDrawSurface3Vtbl *vtbl3;
    const IDirectDrawSurface2Vtbl *vtbl2;
    const IDirectDrawSurfaceVtbl *vtbl1;
    const IDirect3DTexture2Vtbl *texture2_vtbl;
    const IDirect3DTextureVtbl *texture1_vtbl;
    const IDirectDrawGammaControlVtbl *gamma_vtbl;
    LONG ref;
    DDrawObject *owner;
    uint32_t handle;            /* the D9WG texture behind it */
    uint32_t overlay_id;        /* object identity, unlike aliased handle */
    DDTextureStorage *storage;
    UINT mip_level;
    UINT cube_face;
    DDSURFACEDESC2 desc;        /* what the app asked for, kept verbatim */
    DDGAMMARAMP gamma_ramp;     /* primary only; stored, not applied */
    unsigned format;            /* DDWG_FMT_* */
    BOOL indexed;               /* stored as r8uint indices on the host */
    UINT width;
    UINT height;
    UINT bytes_per_pixel;
    UINT pitch;
    DDSurfaceMemory *memory;
    BYTE *shadow;               /* system-memory bits Lock hands out */
    BOOL external_memory;       /* upload before every use as a source */
    BOOL upload_pending;        /* host texture is blank; owes a full upload */
    BOOL implicit;              /* complex child; cannot be duplicated */
    DWORD lock_depth;
    RECT locked_rect;
    DWORD lock_flags;
    BOOL primary;
    BOOL front_of_chain;
    DDSurface *flip_next;       /* ring of the flip chain, NULL when alone */
    DDSurface *mip_next;        /* next smaller level, owned by this surface */
    DDSurface *cube_faces[6];   /* level-zero faces; only the cube root owns */
    DDSurface *attached_depth;
    DDPalette *palette;
    DDClipper *clipper;
    DDCOLORKEY color_key[4];
    DWORD color_key_present;    /* bit per DDCKEY kind */
    BOOL overlay_visible;
    RECT overlay_source_rect;
    RECT overlay_destination_rect;
    DDSurface *overlay_destination;
    uint32_t overlay_wire_flags;
    DDCOLORKEY overlay_source_key_override;
    DDCOLORKEY overlay_destination_key_override;
    LONG overlay_z_order;
    BOOL overlay_dirty;
    D3DTEXTUREHANDLE texture_handle;
    HDC dc;                     /* GetDC/ReleaseDC over a DIB section */
    HBITMAP dib;
    HBITMAP old_bitmap;
    BYTE *dib_bits;
    DDSurface *next;            /* the owner's flat list */
};

struct DDrawObject {
    const IDirectDraw7Vtbl *vtbl7;          /* first: the canonical identity */
    const IDirectDraw4Vtbl *vtbl4;
    const IDirectDraw3Vtbl *vtbl3;
    const IDirectDraw2Vtbl *vtbl2;
    const IDirectDrawVtbl *vtbl1;
    const IDirect3D7Vtbl *d3d7_vtbl;
    const IDirect3D3Vtbl *d3d3_vtbl;
    const IDirect3D2Vtbl *d3d2_vtbl;
    const IDirect3DVtbl *d3d1_vtbl;
    LONG ref;
    HWND window;
    DWORD cooperative_flags;
    uint32_t device_handle;
    BOOL device_created;
    uint32_t device_generation;     /* the transport generation it belongs to */
    UINT mode_width;
    UINT mode_height;
    UINT mode_bpp;
    UINT mode_refresh;
    BOOL guest_mode_changed;    /* ChangeDisplaySettings really moved the desktop */
    BOOL mode_set;
    DDSurface *surfaces;
    DDSurface *primary;
    DDPalette *palettes;
    DDClipper *clippers;
    uint32_t next_palette_slot;
    D3D7Device *d3d7_device;
    D3DLegacyMaterial *legacy_materials;
    DDrawObject *next;
};

static DDrawObject *g_objects;
static LONG g_object_count;

static void *heap_alloc_zero(SIZE_T bytes)
{
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
}

static void heap_free(void *pointer)
{
    if (pointer) HeapFree(GetProcessHeap(), 0, pointer);
}

static DDSurfaceMemory *surface_memory_create(BYTE *bits, SIZE_T bytes,
        BOOL owned)
{
    DDSurfaceMemory *memory = (DDSurfaceMemory *)heap_alloc_zero(
            sizeof(*memory));
    if (!memory) return NULL;
    if (!bits && owned) {
        bits = (BYTE *)heap_alloc_zero(bytes);
        if (!bits) { heap_free(memory); return NULL; }
    }
    memory->ref = 1;
    memory->bits = bits;
    memory->bytes = bytes;
    memory->owned = owned;
    return memory;
}

static void surface_memory_addref(DDSurfaceMemory *memory)
{
    if (memory) InterlockedIncrement(&memory->ref);
}

static void surface_memory_release(DDSurfaceMemory *memory)
{
    if (!memory || InterlockedDecrement(&memory->ref) != 0) return;
    if (memory->owned) heap_free(memory->bits);
    heap_free(memory);
}

static DDSurface *surface_from_iface(IDirectDrawSurface7 *iface)
{
    return (DDSurface *)iface;
}

static DDrawObject *object_from_iface(IDirectDraw7 *iface)
{
    return (DDrawObject *)iface;
}

static DDPalette *palette_from_iface(IDirectDrawPalette *iface)
{
    return (DDPalette *)iface;
}

static DDClipper *clipper_from_iface(IDirectDrawClipper *iface)
{
    return (DDClipper *)iface;
}

/* ------------------------------------------------------------------ *
 * Formats
 * ------------------------------------------------------------------ */

static UINT format_bytes_per_pixel(unsigned format)
{
    switch (format) {
    case DDWG_FMT_P8: case DDWG_FMT_A8: case DDWG_FMT_L8:
    case DDWG_FMT_R3G3B2:
        return 1u;
    case DDWG_FMT_R5G6B5: case DDWG_FMT_X1R5G5B5: case DDWG_FMT_A1R5G5B5:
    case DDWG_FMT_A4R4G4B4: case DDWG_FMT_X4R4G4B4: case DDWG_FMT_A8R3G3B2:
    case DDWG_FMT_A8P8: case DDWG_FMT_A8L8: case DDWG_FMT_D16:
    case DDWG_FMT_D15S1: case DDWG_FMT_V8U8: case DDWG_FMT_L6V5U5:
        return 2u;
    case DDWG_FMT_R8G8B8:
        return 3u;
    case DDWG_FMT_A8R8G8B8: case DDWG_FMT_X8R8G8B8: case DDWG_FMT_D32:
    case DDWG_FMT_D24S8: case DDWG_FMT_D24X8: case DDWG_FMT_D24X4S4:
    case DDWG_FMT_X8L8V8U8:
        return 4u;
    default:
        return 0u;
    }
}

static UINT format_compressed_block_bytes(unsigned format)
{
    switch (format) {
    case DDWG_FMT_DXT1:
        return 8u;
    case DDWG_FMT_DXT2: case DDWG_FMT_DXT3:
    case DDWG_FMT_DXT4: case DDWG_FMT_DXT5:
        return 16u;
    default:
        return 0u;
    }
}

static BOOL format_is_compressed(unsigned format)
{
    return format_compressed_block_bytes(format) != 0u;
}

static BOOL format_is_depth(unsigned format)
{
    switch (format) {
    case DDWG_FMT_D16: case DDWG_FMT_D15S1: case DDWG_FMT_D24S8:
    case DDWG_FMT_D24X8: case DDWG_FMT_D24X4S4: case DDWG_FMT_D32:
    case DDWG_FMT_D16_LOCKABLE:
        return TRUE;
    default:
        return FALSE;
    }
}

static void describe_pixel_format(const DDPIXELFORMAT *source,
        DDrawPixelFormatDesc *out)
{
    ZeroMemory(out, sizeof(*out));
    out->flags = source->dwFlags;
    out->fourcc = source->dwFourCC;
    out->bit_count = source->dwRGBBitCount;
    out->red_mask = source->dwRBitMask;
    out->green_mask = source->dwGBitMask;
    out->blue_mask = source->dwBBitMask;
    out->alpha_mask = source->dwRGBAlphaBitMask;
    out->z_bit_depth = source->dwZBufferBitDepth;
    out->stencil_bit_depth = source->dwStencilBitDepth;
}

/* The pixel format of the current display mode, which is what a surface
 * created without one inherits. */
static void mode_pixel_format(UINT bpp, DDPIXELFORMAT *out)
{
    ZeroMemory(out, sizeof(*out));
    out->dwSize = sizeof(*out);
    switch (bpp) {
    case 8:
        out->dwFlags = DDPF_RGB | DDPF_PALETTEINDEXED8;
        out->dwRGBBitCount = 8;
        return;
    case 16:
        out->dwFlags = DDPF_RGB;
        out->dwRGBBitCount = 16;
        out->dwRBitMask = 0xF800;
        out->dwGBitMask = 0x07E0;
        out->dwBBitMask = 0x001F;
        return;
    case 24:
        out->dwFlags = DDPF_RGB;
        out->dwRGBBitCount = 24;
        out->dwRBitMask = 0xFF0000;
        out->dwGBitMask = 0x00FF00;
        out->dwBBitMask = 0x0000FF;
        return;
    default:
        out->dwFlags = DDPF_RGB;
        out->dwRGBBitCount = 32;
        out->dwRBitMask = 0xFF0000;
        out->dwGBitMask = 0x00FF00;
        out->dwBBitMask = 0x0000FF;
        return;
    }
}

/*
 * The size of the display, which is what a primary surface is.
 *
 * Deliberately not the window's client rectangle. A DirectDraw display mode
 * describes the display, not the app's window: a DDSCL_NORMAL app's primary
 * surface *is* the desktop, and it reaches its own window through a clipper.
 * Consulting GetClientRect here was also wrong in a way that only showed up
 * in a real guest -- an MFC frame exists before it is sized, and 3DMark 2000
 * calls SetCooperativeLevel while its client rectangle still measures 1x1.
 * That 1x1 was latched as the display mode, so every primary surface created
 * afterwards was 1x1, no call ever failed, and the first symptom was the host
 * refusing a blit for reading outside a one-pixel source.
 */
static void object_display_size(DDrawObject *object, UINT *width, UINT *height)
{
    UINT resolved_width = object->mode_width;
    UINT resolved_height = object->mode_height;
    int metric;

    if (!resolved_width && (metric = GetSystemMetrics(SM_CXSCREEN)) > 0)
        resolved_width = (UINT)metric;
    if (!resolved_height && (metric = GetSystemMetrics(SM_CYSCREEN)) > 0)
        resolved_height = (UINT)metric;

    *width = resolved_width ? resolved_width : 640u;
    *height = resolved_height ? resolved_height : 480u;
}

/* Rows are 8-byte aligned. DirectDraw lets the driver pick the pitch and every
 * app reads it back out of the surface description, so the only requirement is
 * that we report what we actually use. */
static UINT surface_pitch_for(UINT width, UINT bytes_per_pixel)
{
    return ((width * bytes_per_pixel) + 7u) & ~7u;
}

/* ------------------------------------------------------------------ *
 * D9WG emitters
 * ------------------------------------------------------------------ */

/* The handshake is owned by the transport generation, not by a one-shot flag:
 * a HELLO that was dropped because the device was not open yet must be sent
 * again once it is, or the host executes a stream it never greeted. */
static void emit_hello_once(void)
{
    D9WGHello hello;

    if (g_hello_generation && g_hello_generation == g_transport_generation)
        return;
    ZeroMemory(&hello, sizeof(hello));
    hello.guest_pointer_bits = (uint32_t)(sizeof(void *) * 8u);
    hello.feature_bits = 0;
    hello.session_id_low = g_session_id_low;
    hello.session_id_high = g_session_id_high;
    DDWG_TRACE_CHECKPOINT("HELLO QUEUE session=%08lX:%08lX "
            "generation=%lu pointer_bits=%lu", hello.session_id_high,
            hello.session_id_low, g_transport_generation,
            hello.guest_pointer_bits);
    if (emit_command(D9WG_OP_HELLO, &hello, sizeof(hello)))
        g_hello_generation = g_transport_generation;
}

static void emit_session_end(void)
{
    D9WGSessionEnd end;

    /* Do not open the transport just to announce a process that never sent a
     * D9WG command. Once HELLO has been queued at least once, SESSION_END is
     * the process-wide safety net for objects left live at DLL unload. */
    if (!g_hello_generation)
        return;
    ZeroMemory(&end, sizeof(end));
    end.session_id_low = g_session_id_low;
    end.session_id_high = g_session_id_high;
    DDWG_TRACE_CHECKPOINT("SESSION END QUEUE session=%08lX:%08lX",
            end.session_id_high, end.session_id_low);
    emit_command(D9WG_OP_SESSION_END, &end, sizeof(end));
}

static void emit_device_destroy(DDrawObject *object)
{
    D9WGDestroyResource destroy;

    if (!object || !object->device_created || !object->device_handle)
        return;
    ZeroMemory(&destroy, sizeof(destroy));
    destroy.resource_handle = object->device_handle;
    /* resource_kind 0 is the established D8WG/D9WG device-destroy
     * convention. Reusing it makes DirectDraw follow the same host path as
     * D3D8/D3D9 instead of leaving its last swap image on screen forever. */
    destroy.resource_kind = 0;
    DDWG_TRACE_CHECKPOINT("DEVICE DESTROY QUEUE handle=%lu generation=%lu",
            object->device_handle, object->device_generation);
    emit_command(D9WG_OP_DESTROY_RESOURCE, &destroy, sizeof(destroy));
    object->device_created = FALSE;
    object->device_generation = 0;
}

static void emit_palette_table(DDPalette *palette);

static BOOL ensure_device(DDrawObject *object)
{
    D9WGCreateDevice create;
    DDPalette *palette;
    BOOL recovering;
    UINT width;
    UINT height;

    if (object->device_created &&
            object->device_generation == g_transport_generation)
        return TRUE;
    recovering = object->device_created;
    object->device_created = FALSE;
    if (!object->device_handle)
        object->device_handle = allocate_handle();

    emit_hello_once();
    object_display_size(object, &width, &height);
    ZeroMemory(&create, sizeof(create));
    create.device_handle = object->device_handle;
    create.hwnd = (uint32_t)(uintptr_t)object->window;
    create.width = width;
    create.height = height;
    /* The swap-chain image is always true colour: it is the surface the page
     * composites, and a palettised primary resolves into it rather than being
     * one. */
    create.backbuffer_format = DDWG_FMT_X8R8G8B8;
    create.windowed = (object->cooperative_flags & DDSCL_FULLSCREEN) ? 0u : 1u;
    create.behavior_flags = 0;
    create.enable_auto_depth_stencil = 0;
    DDWG_TRACE_CHECKPOINT("DEVICE CREATE handle=%lu hwnd=%08lX size=%lux%lu "
            "windowed=%lu recovering=%lu generation=%lu",
            create.device_handle, create.hwnd, create.width, create.height,
            create.windowed, recovering ? 1u : 0u, g_transport_generation);
    if (!emit_command(D9WG_OP_CREATE_DEVICE, &create, sizeof(create)))
        return FALSE;
    object->device_created = TRUE;
    object->device_generation = g_transport_generation;
    /* A palette table lives on the host against the device that is gone. The
     * surfaces restate their own bindings when their storage is recreated;
     * the tables themselves have no other owner to restate them. */
    if (recovering) {
        for (palette = object->palettes; palette; palette = palette->next)
            emit_palette_table(palette);
    }
    return TRUE;
}

static void emit_display_mode(DDrawObject *object)
{
    D9WGDDSetDisplayMode mode;
    uint32_t flags = 0;

    if (!ensure_device(object))
        return;
    if (object->cooperative_flags & DDSCL_NORMAL) flags |= D9WG_DDSCL_NORMAL;
    if (object->cooperative_flags & DDSCL_EXCLUSIVE)
        flags |= D9WG_DDSCL_EXCLUSIVE;
    if (object->cooperative_flags & DDSCL_FULLSCREEN)
        flags |= D9WG_DDSCL_FULLSCREEN;

    ZeroMemory(&mode, sizeof(mode));
    mode.device_handle = object->device_handle;
    mode.width = object->mode_width;
    mode.height = object->mode_height;
    mode.bits_per_pixel = object->mode_bpp;
    mode.refresh_rate = object->mode_refresh ? object->mode_refresh : 60u;
    mode.cooperative_flags = flags;
    mode.guest_mode_changed = object->guest_mode_changed ? 1u : 0u;
    DDWG_TRACE_CHECKPOINT("DISPLAY_MODE device=%lu size=%lux%lux%lu "
            "refresh=%lu cooperative=%08lX guest_changed=%lu",
            mode.device_handle, mode.width, mode.height,
            mode.bits_per_pixel, mode.refresh_rate, mode.cooperative_flags,
            mode.guest_mode_changed);
    emit_command(D9WG_OP_DD_SET_DISPLAY_MODE, &mode, sizeof(mode));
}

static void rebind_host_surface_state(DDSurface *surface);

/*
 * The creation command is recorded on the storage before any attempt to send
 * it, because the attempt is the part that can fail for a reason that has
 * nothing to do with the surface: v86gl.sys is a demand-start service, and a
 * program that reaches CreateSurface before it is running must still get a
 * surface. What it gets is a surface that owes the host a texture, and the
 * record is what lets ensure_surface_storage pay that debt later.
 */
static void record_surface_storage_create(DDSurface *surface, UINT level_count,
        BOOL cube)
{
    DDTextureStorage *storage = surface->storage;

    if (cube) {
        D9WGCreateTextureCube cube_create;
        ZeroMemory(&cube_create, sizeof(cube_create));
        cube_create.device_handle = surface->owner->device_handle;
        cube_create.resource_handle = surface->handle;
        cube_create.edge_length = surface->width;
        cube_create.level_count = level_count;
        cube_create.format = surface->format;
        if (surface->desc.ddsCaps.dwCaps & DDSCAPS_3DDEVICE)
            cube_create.usage |= DDWG_USAGE_RENDERTARGET;
        if (surface->indexed)
            cube_create.usage |= D9WG_USAGE_DDRAW_INDEXED;
        cube_create.pool = DDWG_POOL_DEFAULT;
        storage->resource_kind = D9WG_RESOURCE_TEXTURE_CUBE;
        storage->create_opcode = D9WG_OP_CREATE_TEXTURE_CUBE;
        storage->create_bytes = (uint32_t)sizeof(cube_create);
        storage->create.texture_cube = cube_create;
        return;
    }
    {
        D9WGCreateTexture2D create;
        ZeroMemory(&create, sizeof(create));
        create.device_handle = surface->owner->device_handle;
        create.resource_handle = surface->handle;
        create.width = surface->width;
        create.height = surface->height;
        create.level_count = level_count;
        create.format = surface->format;
        create.usage = format_is_depth(surface->format)
            ? DDWG_USAGE_DEPTHSTENCIL : 0u;
        if (surface->desc.ddsCaps.dwCaps & DDSCAPS_3DDEVICE)
            create.usage |= DDWG_USAGE_RENDERTARGET;
        if (surface->indexed)
            create.usage |= D9WG_USAGE_DDRAW_INDEXED;
        create.pool = DDWG_POOL_DEFAULT;
        storage->resource_kind = D9WG_RESOURCE_TEXTURE_2D;
        storage->create_opcode = D9WG_OP_CREATE_TEXTURE_2D;
        storage->create_bytes = (uint32_t)sizeof(create);
        storage->create.texture_2d = create;
    }
}

/*
 * Sends the recorded creation for a surface whose host texture is missing:
 * either it was never created, because the transport was not open when
 * CreateSurface ran, or it belonged to an earlier transport generation. Every
 * surface sharing the storage is marked for a full re-upload, because the new
 * texture starts blank and the guest shadow is the only copy of the pixels.
 */
static BOOL ensure_surface_storage(DDSurface *surface)
{
    DDTextureStorage *storage;
    DDSurface *sibling;
    BOOL rebuilding;

    if (!surface || !surface->storage)
        return FALSE;
    storage = surface->storage;
    if (storage->created &&
            storage->create_generation == g_transport_generation)
        return TRUE;
    if (!storage->create_opcode)
        return FALSE;
    if (!ensure_device(surface->owner))
        return FALSE;
    rebuilding = storage->create_generation != 0;
    storage->created = FALSE;
    /* The device handle is reissued with the device, so the recorded command
     * carries the old one until it is refreshed here. */
    if (storage->create_opcode == D9WG_OP_CREATE_TEXTURE_CUBE)
        storage->create.texture_cube.device_handle =
                surface->owner->device_handle;
    else
        storage->create.texture_2d.device_handle =
                surface->owner->device_handle;
    DDWG_TRACE_CHECKPOINT("SURFACE STORAGE_CREATE handle=%lu opcode=%u "
            "size=%lux%lu format=%u level=%lu face=%lu rebuilding=%lu",
            surface->handle, storage->create_opcode, surface->width,
            surface->height, surface->format, surface->mip_level,
            surface->cube_face, rebuilding ? 1u : 0u);
    if (!emit_command(storage->create_opcode, &storage->create,
            storage->create_bytes))
        return FALSE;
    storage->created = TRUE;
    storage->create_generation = g_transport_generation;
    if (rebuilding) {
        EnterCriticalSection(&g_object_lock);
        for (sibling = surface->owner->surfaces; sibling;
                sibling = sibling->next) {
            if (sibling->storage == storage)
                sibling->upload_pending = TRUE;
        }
        LeaveCriticalSection(&g_object_lock);
    }
    rebind_host_surface_state(surface);
    return TRUE;
}

static BOOL emit_surface_storage_create(DDSurface *surface, UINT level_count,
        BOOL cube)
{
    record_surface_storage_create(surface, level_count, cube);
    return ensure_surface_storage(surface);
}

static BOOL emit_surface_create(DDSurface *surface)
{
    return emit_surface_storage_create(surface, 1u, FALSE);
}

/* Uploads one rectangle of a surface's shadow. The rectangle is what Lock was
 * given, so a game that locks a scanline pays for a scanline. */
static BOOL emit_surface_upload(DDSurface *surface, const RECT *rect)
{
    D9WGUpdateTexture update;
    uint8_t *payload;
    uint8_t *blob;
    BOOL result;
    UINT left = 0, top = 0, right = surface->width, bottom = surface->height;
    UINT row_bytes;
    UINT rows;
    UINT row;
    UINT source_x;
    UINT source_y;
    UINT block_bytes = format_compressed_block_bytes(surface->format);

    if (!surface->shadow || !surface->handle)
        return FALSE;
    if (!ensure_surface_storage(surface)) {
        /* The guest just wrote pixels the host will never see. Record the
         * debt against this surface so the upload happens once the texture
         * exists, rather than losing the write. */
        surface->upload_pending = TRUE;
        return FALSE;
    }
    /* A texture that has just been created holds nothing, so a dirty-rect
     * upload would leave the rest of it blank. */
    if (surface->upload_pending)
        rect = NULL;
    if (rect) {
        left = (UINT)(rect->left < 0 ? 0 : rect->left);
        top = (UINT)(rect->top < 0 ? 0 : rect->top);
        right = (UINT)rect->right;
        bottom = (UINT)rect->bottom;
        if (right > surface->width) right = surface->width;
        if (bottom > surface->height) bottom = surface->height;
    }
    if (right <= left || bottom <= top)
        return TRUE;
    if (block_bytes) {
        UINT left_block = left / 4u;
        UINT top_block = top / 4u;
        UINT right_block = (right + 3u) / 4u;
        UINT bottom_block = (bottom + 3u) / 4u;
        row_bytes = (right_block - left_block) * block_bytes;
        rows = bottom_block - top_block;
        source_x = left_block * block_bytes;
        source_y = top_block;
    } else {
        row_bytes = (right - left) * surface->bytes_per_pixel;
        rows = bottom - top;
        source_x = left * surface->bytes_per_pixel;
        source_y = top;
    }

    DDWG_TRACE_CHECKPOINT("SURFACE UPLOAD_BEGIN handle=%lu level=%lu "
            "face=%lu rect=%lu,%lu-%lu,%lu pitch=%lu bytes=%lu",
            surface->handle, surface->mip_level, surface->cube_face,
            left, top, right, bottom, row_bytes, row_bytes * rows);

    ZeroMemory(&update, sizeof(update));
    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_UPDATE_TEXTURE, sizeof(update),
            row_bytes * rows, &payload, &blob);
    if (result) {
        update.resource_handle = surface->handle;
        update.level = surface->mip_level;
        update.x = left;
        update.y = top;
        update.z = surface->cube_face;
        update.width = right - left;
        /* Width/height stay in logical texels on the wire. The host converts
         * them to whole 4x4 WebGPU copy extents for BC formats; sending the
         * number of block rows here would upload only the first half of an
         * 8x8 DXT image. row_pitch/slice_pitch remain byte/block based. */
        update.height = bottom - top;
        update.depth = 1;
        update.row_pitch = row_bytes;
        update.slice_pitch = row_bytes * rows;
        update.data_bytes = row_bytes * rows;
        update.data_offset = (uint32_t)((uint8_t *)blob - batch_base());
        CopyMemory(payload, &update, sizeof(update));
        for (row = 0; row < rows; ++row) {
            CopyMemory(blob + row * row_bytes,
                    surface->shadow + (source_y + row) * surface->pitch
                        + source_x,
                    row_bytes);
        }
        if (left == 0 && top == 0 && right >= surface->width &&
                bottom >= surface->height)
            surface->upload_pending = FALSE;
    }
    LeaveCriticalSection(&g_transport_lock);
    DDWG_TRACE_CHECKPOINT("SURFACE UPLOAD_END handle=%lu result=%lu "
            "pending=%lu", surface->handle, result ? 1u : 0u,
            surface->upload_pending ? 1u : 0u);
    return result;
}

/* Everything that names a surface handle on the wire goes through here first:
 * the host texture must exist and must hold the guest's pixels before it can
 * be a blit source, an overlay, or a texture. */
static BOOL prepare_surface_for_host(DDSurface *surface)
{
    if (!ensure_surface_storage(surface))
        return FALSE;
    if (!surface->upload_pending)
        return TRUE;
    /* A depth buffer's guest shadow is never the truth -- nothing writes it,
     * and UPDATE_TEXTURE against a depth texture is not a copy the host can
     * make. A rebuilt one simply starts cleared. */
    if (!surface->shadow || format_is_depth(surface->format)) {
        surface->upload_pending = FALSE;
        return TRUE;
    }
    return emit_surface_upload(surface, NULL);
}

/* SetSurfaceDesc deliberately gives DirectDraw no notification when the
 * client writes its buffer.  Uploading an externally-owned source immediately
 * before it is consumed preserves that zero-copy API contract.  A pending GPU
 * write wins until Lock/readback synchronises it back to client memory. */
static BOOL sync_external_surface_for_read(DDSurface *surface)
{
    if (!surface || !surface->external_memory ||
            surface->memory->gpu_dirty)
        return TRUE;
    return emit_surface_upload(surface, NULL);
}

static void emit_color_key(DDSurface *surface, DWORD kind,
        const DDCOLORKEY *key, BOOL present)
{
    D9WGDDSetColorKey command;
    DDrawPixelFormatDesc format;

    if (!surface->handle || !surface->storage->created)
        return;
    describe_pixel_format(&surface->desc.ddpfPixelFormat, &format);
    ZeroMemory(&command, sizeof(command));
    command.surface_handle = surface->handle;
    command.key_kind = kind;
    command.present = present ? 1u : 0u;
    if (present && key) {
        command.color_low =
            ddraw_color_key_to_comparison(&format, key->dwColorSpaceLowValue);
        command.color_high =
            ddraw_color_key_to_comparison(&format, key->dwColorSpaceHighValue);
    }
    emit_command(D9WG_OP_DD_SET_COLOR_KEY, &command, sizeof(command));
}

static void emit_palette_table(DDPalette *palette)
{
    uint8_t *payload;
    uint8_t *blob;
    D9WGSetPalette command;
    BOOL reserved;
    UINT index;

    if (!palette->owner || !ensure_device(palette->owner))
        return;
    ZeroMemory(&command, sizeof(command));
    EnterCriticalSection(&g_transport_lock);
    reserved = reserve_command_locked(D9WG_OP_SET_PALETTE, sizeof(command),
            256u * 4u, &payload, &blob);
    if (reserved) {
        command.device_handle = palette->owner->device_handle;
        command.palette_index = palette->slot;
        command.entry_count = 256;
        command.data_offset = (uint32_t)((uint8_t *)blob - batch_base());
        CopyMemory(payload, &command, sizeof(command));
        /* The wire carries D3DCOLOR (A8R8G8B8). DirectDraw palette entries
         * have no alpha of their own -- peFlags is a usage hint, not a
         * channel -- so every entry is opaque, which is what a palettised
         * surface means on screen. */
        for (index = 0; index < 256u; ++index) {
            blob[index * 4u + 0u] = palette->entries[index].peBlue;
            blob[index * 4u + 1u] = palette->entries[index].peGreen;
            blob[index * 4u + 2u] = palette->entries[index].peRed;
            blob[index * 4u + 3u] = 0xFFu;
        }
    }
    LeaveCriticalSection(&g_transport_lock);
}

static void emit_surface_palette_binding(DDSurface *surface)
{
    D9WGDDSetSurfacePalette command;

    if (!surface->handle || !surface->palette || !surface->storage->created)
        return;
    ZeroMemory(&command, sizeof(command));
    command.surface_handle = surface->handle;
    command.palette_index = surface->palette->slot;
    command.flags = surface->indexed ? 1u : 0u;
    emit_command(D9WG_OP_DD_SET_SURFACE_PALETTE, &command, sizeof(command));
}

/*
 * Colour keys and palette bindings live on the host against the *handle*, and
 * a flip moves handles between surface objects -- so after a rotation the
 * front buffer's handle is carrying the back buffer's key. Restating them is
 * two or three commands for a flip chain, and getting it wrong is a sprite
 * that stops being transparent every other frame.
 */
static void emit_surface_palette_binding(DDSurface *surface);
static void emit_color_key(DDSurface *surface, DWORD kind,
        const DDCOLORKEY *key, BOOL present);

static void rebind_host_surface_state(DDSurface *surface)
{
    DWORD kind;

    if (!surface->handle)
        return;
    for (kind = 0; kind < 4u; ++kind) {
        BOOL present = (surface->color_key_present & (1u << kind)) != 0;
        emit_color_key(surface, kind,
                present ? &surface->color_key[kind] : NULL, present);
    }
    if (surface->palette)
        emit_surface_palette_binding(surface);
}

static BOOL emit_blt(DDSurface *destination, const RECT *destination_rect,
        DDSurface *source, const RECT *source_rect, uint32_t flags,
        uint32_t fill_color, float fill_depth)
{
    D9WGDDBlt command;

    if (!destination || !destination->owner)
        return FALSE;
    if (source && !sync_external_surface_for_read(source))
        return FALSE;
    if (!ensure_device(destination->owner))
        return FALSE;
    if (!prepare_surface_for_host(destination))
        return FALSE;
    if (source && !prepare_surface_for_host(source))
        return FALSE;
    ZeroMemory(&command, sizeof(command));
    command.device_handle = destination->owner->device_handle;
    command.source_handle = source ? source->handle : 0u;
    command.source_level = source ? source->mip_level : 0u;
    command.source_face = source ? source->cube_face : 0u;
    command.destination_handle = destination->handle;
    command.destination_level = destination->mip_level;
    command.destination_face = destination->cube_face;
    command.flags = flags;
    command.fill_color = fill_color;
    command.fill_depth = fill_depth;
    if (source_rect) {
        command.source_rect[0] = source_rect->left;
        command.source_rect[1] = source_rect->top;
        command.source_rect[2] = source_rect->right;
        command.source_rect[3] = source_rect->bottom;
    } else if (source) {
        command.source_rect[2] = (int32_t)source->width;
        command.source_rect[3] = (int32_t)source->height;
    }
    if (destination_rect) {
        command.destination_rect[0] = destination_rect->left;
        command.destination_rect[1] = destination_rect->top;
        command.destination_rect[2] = destination_rect->right;
        command.destination_rect[3] = destination_rect->bottom;
    } else {
        command.destination_rect[2] = (int32_t)destination->width;
        command.destination_rect[3] = (int32_t)destination->height;
    }
    if (!emit_command(D9WG_OP_DD_BLT, &command, sizeof(command)))
        return FALSE;
    destination->memory->gpu_dirty = TRUE;
    return TRUE;
}

/* A blit into the swap-chain image (handle 0), which is what makes a surface
 * visible. Flip and "blit to the primary" both end here. */
static BOOL emit_blt_to_screen(DDSurface *source, const RECT *dirty_rect)
{
    D9WGDDBlt command;
    DDrawObject *object = source->owner;
    RECT area;

    if (!sync_external_surface_for_read(source) || !ensure_device(object))
        return FALSE;
    if (!prepare_surface_for_host(source))
        return FALSE;
    area.left = 0;
    area.top = 0;
    area.right = (LONG)source->width;
    area.bottom = (LONG)source->height;
    if (dirty_rect) {
        if (dirty_rect->left > area.left) area.left = dirty_rect->left;
        if (dirty_rect->top > area.top) area.top = dirty_rect->top;
        if (dirty_rect->right < area.right) area.right = dirty_rect->right;
        if (dirty_rect->bottom < area.bottom)
            area.bottom = dirty_rect->bottom;
    }
    if (area.right <= area.left || area.bottom <= area.top)
        return TRUE;
    ZeroMemory(&command, sizeof(command));
    command.device_handle = object->device_handle;
    command.source_handle = source->handle;
    command.source_level = source->mip_level;
    command.source_face = source->cube_face;
    command.source_rect[0] = area.left;
    command.source_rect[1] = area.top;
    command.source_rect[2] = area.right;
    command.source_rect[3] = area.bottom;
    command.destination_handle = 0;
    command.destination_rect[0] = area.left;
    command.destination_rect[1] = area.top;
    command.destination_rect[2] = area.right;
    command.destination_rect[3] = area.bottom;
    DDWG_TRACE_CHECKPOINT("SCREEN_BLT source=%lu level=%lu face=%lu "
            "rect=%ld,%ld-%ld,%ld surface=%lux%lu", source->handle,
            source->mip_level, source->cube_face, area.left, area.top,
            area.right, area.bottom, source->width, source->height);
    return emit_command(D9WG_OP_DD_BLT, &command, sizeof(command));
}

static BOOL emit_overlay_state(DDSurface *surface)
{
    D9WGDDUpdateOverlay command;
    DDSurface *target;
    BOOL restore_source = FALSE;
    BOOL restore_destination = FALSE;
    BOOL result;

    if (!surface || !(surface->desc.ddsCaps.dwCaps & DDSCAPS_OVERLAY) ||
            !surface->handle)
        return FALSE;
    if (surface->overlay_visible &&
            !sync_external_surface_for_read(surface))
        return FALSE;
    if (!ensure_device(surface->owner)) return FALSE;
    if (!prepare_surface_for_host(surface)) return FALSE;
    target = surface->overlay_destination;
    if (surface->overlay_wire_flags & D9WG_DDOVER_KEY_SOURCE) {
        if (surface->overlay_wire_flags &
                D9WG_DDOVER_KEY_SOURCE_OVERRIDE) {
            emit_color_key(surface, D9WG_DDCKEY_SOURCE_OVERLAY,
                    &surface->overlay_source_key_override, TRUE);
            restore_source = TRUE;
        } else if (surface->color_key_present &
                (1u << D9WG_DDCKEY_SOURCE_OVERLAY)) {
            emit_color_key(surface, D9WG_DDCKEY_SOURCE_OVERLAY,
                    &surface->color_key[D9WG_DDCKEY_SOURCE_OVERLAY], TRUE);
        }
    }
    if (target && (surface->overlay_wire_flags &
            D9WG_DDOVER_KEY_DESTINATION)) {
        if (surface->overlay_wire_flags &
                D9WG_DDOVER_KEY_DESTINATION_OVERRIDE) {
            emit_color_key(target, D9WG_DDCKEY_DESTINATION_OVERLAY,
                    &surface->overlay_destination_key_override, TRUE);
            restore_destination = TRUE;
        } else if (target->color_key_present &
                (1u << D9WG_DDCKEY_DESTINATION_OVERLAY)) {
            emit_color_key(target, D9WG_DDCKEY_DESTINATION_OVERLAY,
                    &target->color_key[D9WG_DDCKEY_DESTINATION_OVERLAY], TRUE);
        }
    }
    ZeroMemory(&command, sizeof(command));
    command.surface_handle = surface->handle;
    command.overlay_id = surface->overlay_id;
    command.source_rect[0] = surface->overlay_source_rect.left;
    command.source_rect[1] = surface->overlay_source_rect.top;
    command.source_rect[2] = surface->overlay_source_rect.right;
    command.source_rect[3] = surface->overlay_source_rect.bottom;
    command.destination_rect[0] = surface->overlay_destination_rect.left;
    command.destination_rect[1] = surface->overlay_destination_rect.top;
    command.destination_rect[2] = surface->overlay_destination_rect.right;
    command.destination_rect[3] = surface->overlay_destination_rect.bottom;
    command.flags = surface->overlay_visible
        ? (surface->overlay_wire_flags | D9WG_DDOVER_SHOW)
        : D9WG_DDOVER_HIDE;
    command.z_order = (uint32_t)(surface->overlay_z_order < 0
        ? 0 : surface->overlay_z_order);
    command.destination_handle = surface->overlay_destination
        ? surface->overlay_destination->handle : 0u;
    result = emit_command(D9WG_OP_DD_UPDATE_OVERLAY, &command,
            sizeof(command));
    if (restore_source)
        emit_color_key(surface, D9WG_DDCKEY_SOURCE_OVERLAY,
                &surface->color_key[D9WG_DDCKEY_SOURCE_OVERLAY],
                (surface->color_key_present &
                    (1u << D9WG_DDCKEY_SOURCE_OVERLAY)) != 0);
    if (restore_destination)
        emit_color_key(target, D9WG_DDCKEY_DESTINATION_OVERLAY,
                &target->color_key[D9WG_DDCKEY_DESTINATION_OVERLAY],
                (target->color_key_present &
                    (1u << D9WG_DDCKEY_DESTINATION_OVERLAY)) != 0);
    return result;
}

static BOOL emit_present_and_flush(DDrawObject *object)
{
    D9WGPresent present;
    DDSurface *overlay;
    RECT client;
    POINT origin;
    BOOL result;

    if (!ensure_device(object))
        return FALSE;
    /* An overlay backed by client memory can change without Lock/Unlock. Its
     * persistent host overlay entry samples the latest upload at scanout. */
    for (overlay = object->surfaces; overlay; overlay = overlay->next) {
        if (overlay->overlay_visible && overlay->external_memory &&
                !overlay->memory->gpu_dirty)
            emit_surface_upload(overlay, NULL);
    }
    ZeroMemory(&present, sizeof(present));
    present.device_handle = object->device_handle;
    present.hwnd = (uint32_t)(uintptr_t)object->window;
    present.width = object->mode_width;
    present.height = object->mode_height;
    if (object->window && GetClientRect(object->window, &client)) {
        origin.x = client.left;
        origin.y = client.top;
        if (ClientToScreen(object->window, &origin)) {
            present.x = origin.x;
            present.y = origin.y;
        }
        if (client.right > client.left)
            present.width = (uint32_t)(client.right - client.left);
        if (client.bottom > client.top)
            present.height = (uint32_t)(client.bottom - client.top);
    }
    /* An exclusive-fullscreen title whose mode change really happened owns the
     * whole screen; its window rectangle is the screen. */
    if ((object->cooperative_flags & DDSCL_FULLSCREEN) &&
            object->guest_mode_changed) {
        present.x = 0;
        present.y = 0;
        present.width = object->mode_width;
        present.height = object->mode_height;
    }
    DDWG_TRACE_CHECKPOINT("PRESENT BEGIN device=%lu hwnd=%08lX "
            "rect=%ld,%ld %lux%lu frame=%lu", present.device_handle,
            present.hwnd, present.x, present.y, present.width,
            present.height, g_frame_id);
    if (!emit_command(D9WG_OP_PRESENT, &present, sizeof(present)))
        return FALSE;
    EnterCriticalSection(&g_transport_lock);
    result = submit_batch_locked(TRUE);
    LeaveCriticalSection(&g_transport_lock);
    DDWG_TRACE_CHECKPOINT("PRESENT END device=%lu result=%lu next_frame=%lu",
            present.device_handle, result ? 1u : 0u, g_frame_id);
    return result;
}

/* ------------------------------------------------------------------ *
 * Readback: a Lock of a surface the GPU has written
 * ------------------------------------------------------------------ *
 *
 * Most 2D titles never need this -- they build a frame in system memory and
 * blit it out, so the shadow is always the truth. It matters for the ones that
 * blit sprites into a buffer and then read that buffer back, and returning the
 * stale shadow there is a wrong picture that looks like a missing one.
 *
 * The deadline measures host *silence* rather than elapsed time: every batch
 * the host finishes bumps the heartbeat, so a host merely thousands of batches
 * behind keeps this waiting, and only a host that has stopped answering trips
 * the cap.
 */

static CRITICAL_SECTION g_readback_lock;
static LONG g_next_request_id = 1;

static BOOL surface_readback(DDSurface *surface)
{
    D9WGReadbackSurface command;
    D9WGReadbackResponse *response;
    volatile uint32_t *heartbeat;
    uint8_t *base;
    UINT capacity;
    UINT rows_per_chunk;
    UINT first_row = 0;
    UINT total_rows;
    UINT row_texels;
    BOOL ok = TRUE;

    if (!surface->memory->gpu_dirty || !surface->shadow || !surface->handle)
        return TRUE;
    row_texels = format_is_compressed(surface->format) ? 4u : 1u;
    total_rows = (surface->height + row_texels - 1u) / row_texels;

    EnterCriticalSection(&g_readback_lock);
    EnterCriticalSection(&g_transport_lock);
    if (!open_transport_locked()) {
        LeaveCriticalSection(&g_transport_lock);
        LeaveCriticalSection(&g_readback_lock);
        return FALSE;
    }
    base = response_region();
    response = (D9WGReadbackResponse *)(base + D9WG_READBACK_REGION_OFFSET);
    heartbeat = (volatile uint32_t *)(base + D9WG_HEARTBEAT_OFFSET);
    capacity = D9WG_RESPONSE_REGION_BYTES - D9WG_READBACK_REGION_OFFSET
            - (UINT)sizeof(*response) - D9WG_HEARTBEAT_BYTES;
    rows_per_chunk = surface->pitch ? capacity / surface->pitch : 0;
    LeaveCriticalSection(&g_transport_lock);
    if (!rows_per_chunk) {
        LeaveCriticalSection(&g_readback_lock);
        return FALSE;
    }

    while (first_row < total_rows) {
        uint32_t request_id =
            (uint32_t)InterlockedIncrement(&g_next_request_id);
        UINT chunk_rows = total_rows - first_row;
        UINT logical_first_row;
        UINT logical_row_count;
        DWORD start;
        uint32_t last_beat;
        BOOL emitted;
        uint8_t *payload;

        if (chunk_rows > rows_per_chunk) chunk_rows = rows_per_chunk;
        ZeroMemory(response, sizeof(*response));
        response->request_id = request_id;
        response->status = D9WG_RESPONSE_PENDING;
        MemoryBarrier();

        ZeroMemory(&command, sizeof(command));
        command.device_handle = surface->owner->device_handle;
        command.texture_handle = surface->handle;
        command.level = surface->mip_level;
        command.face = surface->cube_face;
        command.format = surface->format;
        command.width = surface->width;
        command.height = surface->height;
        logical_first_row = first_row * row_texels;
        logical_row_count = chunk_rows * row_texels;
        if (logical_row_count > surface->height - logical_first_row)
            logical_row_count = surface->height - logical_first_row;
        command.first_row = logical_first_row;
        command.row_count = logical_row_count;
        command.destination_pitch = surface->pitch;
        command.destination_bytes = chunk_rows * surface->pitch;
        command.response_offset = D9WG_READBACK_REGION_OFFSET;
        command.request_id = request_id;

        EnterCriticalSection(&g_transport_lock);
        emitted = reserve_command_locked(D9WG_OP_READBACK_SURFACE,
                sizeof(command), 0, &payload, NULL);
        if (emitted) {
            CopyMemory(payload, &command, sizeof(command));
            emitted = submit_batch_locked(FALSE);
        }
        LeaveCriticalSection(&g_transport_lock);
        if (!emitted) { ok = FALSE; break; }

        start = GetTickCount();
        last_beat = *heartbeat;
        for (;;) {
            uint32_t beat;
            MemoryBarrier();
            if (response->status != D9WG_RESPONSE_PENDING)
                break;
            beat = *heartbeat;
            if (beat != last_beat) {
                last_beat = beat;
                start = GetTickCount();
            }
            if (GetTickCount() - start >= 6000u)
                break;
            Sleep(1);
        }
        MemoryBarrier();
        if (response->request_id != request_id
                || response->status != D9WG_RESPONSE_OK
                || response->byte_count != command.destination_bytes) {
            HOSTLOG_FAILED("surface readback failed: status=%lu rows=%lu "
                    "pitch=%lu (0 = the host stopped answering, 2 = it "
                    "reported a failure)", response->status, chunk_rows,
                    surface->pitch);
            ok = FALSE;
            break;
        }
        CopyMemory(surface->shadow + first_row * surface->pitch,
                (const uint8_t *)(response + 1), command.destination_bytes);
        first_row += chunk_rows;
    }
    LeaveCriticalSection(&g_readback_lock);
    if (ok)
        surface->memory->gpu_dirty = FALSE;
    return ok;
}

/* ------------------------------------------------------------------ *
 * IDirectDrawSurface7
 * ------------------------------------------------------------------ */

static IDirectDrawSurface7Vtbl g_surface_vtbl;
static IDirectDrawSurface4Vtbl g_surface_vtbl4;
static IDirectDrawSurface3Vtbl g_surface_vtbl3;
static IDirectDrawSurface2Vtbl g_surface_vtbl2;
static IDirectDrawSurfaceVtbl g_surface_vtbl1;
static IDirectDraw7Vtbl g_ddraw_vtbl;
static IDirectDraw4Vtbl g_ddraw_vtbl4;
static IDirectDraw3Vtbl g_ddraw_vtbl3;
static IDirectDraw2Vtbl g_ddraw_vtbl2;
static IDirectDrawVtbl g_ddraw_vtbl1;
static IDirectDrawPaletteVtbl g_palette_vtbl;
static IDirectDrawClipperVtbl g_clipper_vtbl;

#define DD_VERSION_1 1u
#define DD_VERSION_2 2u
#define DD_VERSION_3 3u
#define DD_VERSION_4 4u
#define DD_VERSION_7 7u

/*
 * Which version an interface pointer belongs to, read from the vtable it
 * carries. This is what lets one implementation serve five interface versions
 * without a per-version copy of every method: the thunks convert their
 * arguments and forward to the version 7 entry point, which is where the work
 * actually lives.
 */
static DDSurface *surface_from_any(void *iface, unsigned *version)
{
    const void *vtbl = *(const void **)iface;
    unsigned found = DD_VERSION_7;
    SIZE_T offset = offsetof(DDSurface, vtbl7);

    if (vtbl == (const void *)&g_surface_vtbl4) {
        found = DD_VERSION_4; offset = offsetof(DDSurface, vtbl4);
    } else if (vtbl == (const void *)&g_surface_vtbl3) {
        found = DD_VERSION_3; offset = offsetof(DDSurface, vtbl3);
    } else if (vtbl == (const void *)&g_surface_vtbl2) {
        found = DD_VERSION_2; offset = offsetof(DDSurface, vtbl2);
    } else if (vtbl == (const void *)&g_surface_vtbl1) {
        found = DD_VERSION_1; offset = offsetof(DDSurface, vtbl1);
    }
    if (version) *version = found;
    return (DDSurface *)((BYTE *)iface - offset);
}

static void *surface_interface(DDSurface *surface, unsigned version)
{
    if (!surface) return NULL;
    switch (version) {
    case DD_VERSION_1: return &surface->vtbl1;
    case DD_VERSION_2: return &surface->vtbl2;
    case DD_VERSION_3: return &surface->vtbl3;
    case DD_VERSION_4: return &surface->vtbl4;
    default: return &surface->vtbl7;
    }
}

static DDrawObject *object_from_any(void *iface, unsigned *version)
{
    const void *vtbl = *(const void **)iface;
    unsigned found = DD_VERSION_7;
    SIZE_T offset = offsetof(DDrawObject, vtbl7);

    if (vtbl == (const void *)&g_ddraw_vtbl4) {
        found = DD_VERSION_4; offset = offsetof(DDrawObject, vtbl4);
    } else if (vtbl == (const void *)&g_ddraw_vtbl3) {
        found = DD_VERSION_3; offset = offsetof(DDrawObject, vtbl3);
    } else if (vtbl == (const void *)&g_ddraw_vtbl2) {
        found = DD_VERSION_2; offset = offsetof(DDrawObject, vtbl2);
    } else if (vtbl == (const void *)&g_ddraw_vtbl1) {
        found = DD_VERSION_1; offset = offsetof(DDrawObject, vtbl1);
    }
    if (version) *version = found;
    return (DDrawObject *)((BYTE *)iface - offset);
}

static void *object_interface(DDrawObject *object, unsigned version)
{
    if (!object) return NULL;
    switch (version) {
    case DD_VERSION_1: return &object->vtbl1;
    case DD_VERSION_2: return &object->vtbl2;
    case DD_VERSION_3: return &object->vtbl3;
    case DD_VERSION_4: return &object->vtbl4;
    default: return &object->vtbl7;
    }
}

/*
 * DDSURFACEDESC (versions 1-3) against DDSURFACEDESC2 (versions 4 and 7). The
 * fields line up one for one except that the older one carries a DDSCAPS where
 * the newer carries a DDSCAPS2, and that its dwMipMapCount union slot doubles
 * as dwZBufferBitDepth -- a depth that DDSURFACEDESC2 dropped, because by then
 * the pixel format carried it.
 */
static void desc1_to_desc2(const DDSURFACEDESC *from, DDSURFACEDESC2 *to)
{
    ZeroMemory(to, sizeof(*to));
    to->dwSize = sizeof(*to);
    to->dwFlags = from->dwFlags;
    to->dwHeight = from->dwHeight;
    to->dwWidth = from->dwWidth;
    to->lPitch = from->lPitch;
    to->dwBackBufferCount = from->dwBackBufferCount;
    to->dwMipMapCount = from->dwMipMapCount;
    to->dwAlphaBitDepth = from->dwAlphaBitDepth;
    to->dwReserved = from->dwReserved;
    to->lpSurface = from->lpSurface;
    to->ddckCKDestOverlay = from->ddckCKDestOverlay;
    to->ddckCKDestBlt = from->ddckCKDestBlt;
    to->ddckCKSrcOverlay = from->ddckCKSrcOverlay;
    to->ddckCKSrcBlt = from->ddckCKSrcBlt;
    to->ddpfPixelFormat = from->ddpfPixelFormat;
    to->ddsCaps.dwCaps = from->ddsCaps.dwCaps;
    /* A version 1 caller asking for a Z buffer says so in dwZBufferBitDepth,
     * and nothing downstream reads that field -- the pixel format is what
     * CreateSurface looks at -- so it is turned into one here. */
    if ((from->dwFlags & DDSD_ZBUFFERBITDEPTH) &&
            !(from->dwFlags & DDSD_PIXELFORMAT)) {
        to->dwFlags = (to->dwFlags & ~DDSD_ZBUFFERBITDEPTH) | DDSD_PIXELFORMAT;
        to->ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
        to->ddpfPixelFormat.dwFlags = DDPF_ZBUFFER;
        to->ddpfPixelFormat.dwZBufferBitDepth = from->dwZBufferBitDepth;
        to->ddpfPixelFormat.dwRGBBitCount = from->dwZBufferBitDepth;
    }
}

static void desc2_to_desc1(const DDSURFACEDESC2 *from, DDSURFACEDESC *to)
{
    ZeroMemory(to, sizeof(*to));
    to->dwSize = sizeof(*to);
    to->dwFlags = from->dwFlags;
    to->dwHeight = from->dwHeight;
    to->dwWidth = from->dwWidth;
    to->lPitch = from->lPitch;
    to->dwBackBufferCount = from->dwBackBufferCount;
    to->dwMipMapCount = from->dwMipMapCount;
    to->dwAlphaBitDepth = from->dwAlphaBitDepth;
    to->dwReserved = from->dwReserved;
    to->lpSurface = from->lpSurface;
    to->ddckCKDestOverlay = from->ddckCKDestOverlay;
    to->ddckCKDestBlt = from->ddckCKDestBlt;
    to->ddckCKSrcOverlay = from->ddckCKSrcOverlay;
    to->ddckCKSrcBlt = from->ddckCKSrcBlt;
    to->ddpfPixelFormat = from->ddpfPixelFormat;
    to->ddsCaps.dwCaps = from->ddsCaps.dwCaps;
}

static void surface_destroy(DDSurface *surface);
static void emit_window_state_not_showing(DDrawObject *object);
static void surface_storage_release(DDTextureStorage *storage);
static DDTextureStorage *surface_storage_allocate(void);
static ULONG WINAPI surface_AddRef(IDirectDrawSurface7 *iface);
static HRESULT WINAPI ddraw_QueryInterface(IDirectDraw7 *iface, REFIID iid,
        void **out);
static ULONG WINAPI ddraw_AddRef(IDirectDraw7 *iface);
static ULONG WINAPI ddraw_Release(IDirectDraw7 *iface);
static void d3d7_rebind_after_flip(DDrawObject *object);
static void d3d7_refresh_render_target(DDrawObject *object,
        DDSurface *surface);
static HRESULT d3d_legacy_surface_create_device(DDSurface *surface,
        REFIID iid, void **out);

static HRESULT WINAPI surface_QueryInterface(IDirectDrawSurface7 *iface,
        REFIID iid, void **out)
{
    DDSurface *surface = surface_from_any(iface, NULL);
    unsigned version = 0;

    DDWG_TRACE_CHECKPOINT("CALL surface_QueryInterface handle=%lu iid=%08lX",
            surface->handle, iid ? iid->Data1 : 0u);
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = NULL;
    if (iid_is_unknown(iid) || guid_equal(iid, &IID_IDirectDrawSurface7))
        version = DD_VERSION_7;
    else if (guid_equal(iid, &IID_IDirectDrawSurface4))
        version = DD_VERSION_4;
    else if (guid_equal(iid, &IID_IDirectDrawSurface3))
        version = DD_VERSION_3;
    else if (guid_equal(iid, &IID_IDirectDrawSurface2))
        version = DD_VERSION_2;
    else if (guid_equal(iid, &IID_IDirectDrawSurface))
        version = DD_VERSION_1;
    if (!version) {
        if (guid_equal(iid, &IID_IDirect3DTexture2)) {
            surface_AddRef((IDirectDrawSurface7 *)&surface->vtbl7);
            *out = &surface->texture2_vtbl;
            return DDWG_TRACE_RESULT(D3D_OK);
        }
        if (guid_equal(iid, &IID_IDirect3DTexture)) {
            surface_AddRef((IDirectDrawSurface7 *)&surface->vtbl7);
            *out = &surface->texture1_vtbl;
            return DDWG_TRACE_RESULT(D3D_OK);
        }
        if (guid_equal(iid, &IID_IDirectDrawGammaControl)) {
            /* DDCAPS2_PRIMARYGAMMA is the only gamma capability claimed, so
             * the primary is the only surface that answers to it. */
            if (!(surface->desc.ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE))
                return DDWG_TRACE_ERROR(E_NOINTERFACE);
            surface_AddRef((IDirectDrawSurface7 *)&surface->vtbl7);
            *out = &surface->gamma_vtbl;
            return DDWG_TRACE_RESULT(DD_OK);
        }
        if (guid_equal(iid, &IID_IDirect3DHALDevice) ||
                guid_equal(iid, &IID_IDirect3DRGBDevice) ||
                guid_equal(iid, &IID_IDirect3DRampDevice) ||
                guid_equal(iid, &IID_IDirect3DMMXDevice) ||
                guid_equal(iid, &IID_IDirect3DRefDevice))
            return d3d_legacy_surface_create_device(surface, iid, out);
        return DDWG_TRACE_ERROR(E_NOINTERFACE);
    }
    /* One object, one reference count: every version is the same allocation
     * seen through a different vtable, so the identity an app compares
     * pointers against is preserved per version and the count is shared. */
    surface_AddRef((IDirectDrawSurface7 *)&surface->vtbl7);
    *out = surface_interface(surface, version);
    DDWG_TRACE("SURFACE QUERY_INTERFACE handle=%lu version=%u out=%08lX",
            surface->handle, version, (DWORD)(uintptr_t)*out);
    return DDWG_TRACE_RESULT(DD_OK);
}

static ULONG WINAPI surface_AddRef(IDirectDrawSurface7 *iface)
{
    return (ULONG)InterlockedIncrement(&surface_from_any(iface, NULL)->ref);
}

static ULONG WINAPI surface_Release(IDirectDrawSurface7 *iface)
{
    DDSurface *surface = surface_from_any(iface, NULL);
    LONG remaining = InterlockedDecrement(&surface->ref);

    if (remaining == 0) {
        DDWG_TRACE_CHECKPOINT("SURFACE DESTROY handle=%lu object=%08lX "
                "primary=%lu", surface->handle,
                (DWORD)(uintptr_t)surface, surface->primary ? 1u : 0u);
        surface_destroy(surface);
    }
    return (ULONG)(remaining < 0 ? 0 : remaining);
}

/* ------------------------------------------------------------------ *
 * IDirectDrawGammaControl
 *
 * Another interface on the primary surface, reached by QueryInterface, and
 * the one DDCAPS2_PRIMARYGAMMA promises. Advertising that bit without this
 * interface was the mismatch worth removing: a title that checks the cap and
 * then queries -- 3DMark 2000's graphics layer is one -- got E_NOINTERFACE
 * from a driver that had just said gamma was available.
 *
 * The ramp is stored and returned, not applied: presentation cadence and
 * output transfer belong to the browser, exactly as with the D3D8/D3D9
 * frontends' SetGammaRamp. What that costs is listed in the README's
 * deviations -- a fade-to-black done through gamma stays lit.
 * ------------------------------------------------------------------ */

static DDSurface *gamma_from_iface(IDirectDrawGammaControl *iface)
{
    return (DDSurface *)((BYTE *)iface - offsetof(DDSurface, gamma_vtbl));
}

static HRESULT WINAPI gamma_QueryInterface(IDirectDrawGammaControl *iface,
        REFIID iid, void **out)
{
    DDSurface *surface = gamma_from_iface(iface);
    return surface_QueryInterface((IDirectDrawSurface7 *)&surface->vtbl7,
            iid, out);
}

static ULONG WINAPI gamma_AddRef(IDirectDrawGammaControl *iface)
{
    DDSurface *surface = gamma_from_iface(iface);
    return surface_AddRef((IDirectDrawSurface7 *)&surface->vtbl7);
}

static ULONG WINAPI gamma_Release(IDirectDrawGammaControl *iface)
{
    DDSurface *surface = gamma_from_iface(iface);
    return surface_Release((IDirectDrawSurface7 *)&surface->vtbl7);
}

static HRESULT WINAPI gamma_GetGammaRamp(IDirectDrawGammaControl *iface,
        DWORD flags, LPDDGAMMARAMP ramp)
{
    DDSurface *surface = gamma_from_iface(iface);
    (void)flags;
    if (!ramp) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *ramp = surface->gamma_ramp;
    return DD_OK;
}

static HRESULT WINAPI gamma_SetGammaRamp(IDirectDrawGammaControl *iface,
        DWORD flags, LPDDGAMMARAMP ramp)
{
    DDSurface *surface = gamma_from_iface(iface);
    /* DDSGR_CALIBRATE asks the driver to correct the ramp for this monitor.
     * There is no monitor here to calibrate against, and the API allows a
     * driver to ignore it. */
    (void)flags;
    if (!ramp) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    surface->gamma_ramp = *ramp;
    return DD_OK;
}

static IDirectDrawGammaControlVtbl g_gamma_vtbl = {
    gamma_QueryInterface, gamma_AddRef, gamma_Release,
    gamma_GetGammaRamp, gamma_SetGammaRamp,
};

static HRESULT WINAPI surface_AddAttachedSurface(IDirectDrawSurface7 *iface,
        LPDIRECTDRAWSURFACE7 attachment)
{
    DDSurface *surface = surface_from_iface(iface);
    DDSurface *other = surface_from_iface(attachment);

    if (!other) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (other->desc.ddsCaps.dwCaps & DDSCAPS_ZBUFFER) {
        surface->attached_depth = other;
        IDirectDrawSurface7_AddRef(attachment);
        d3d7_refresh_render_target(surface->owner, surface);
        return DD_OK;
    }
    if (other->desc.ddsCaps.dwCaps & DDSCAPS_BACKBUFFER) {
        /* An explicitly attached back buffer joins the flip ring. */
        other->flip_next = surface->flip_next ? surface->flip_next : surface;
        surface->flip_next = other;
        IDirectDrawSurface7_AddRef(attachment);
        return DD_OK;
    }
    return UNSUPPORTED("AddAttachedSurface for this surface kind",
            DDERR_CANNOTATTACHSURFACE);
}

static HRESULT WINAPI surface_AddOverlayDirtyRect(IDirectDrawSurface7 *iface,
        LPRECT rect)
{
    DDSurface *surface = surface_from_iface(iface);
    if (!(surface->desc.ddsCaps.dwCaps & DDSCAPS_OVERLAY))
        return DDWG_TRACE_ERROR(DDERR_NOTAOVERLAYSURFACE);
    if (!rect || rect->left < 0 || rect->top < 0 ||
            rect->right > (LONG)surface->width ||
            rect->bottom > (LONG)surface->height ||
            rect->right <= rect->left || rect->bottom <= rect->top)
        return DDWG_TRACE_ERROR(DDERR_INVALIDRECT);
    /* The host recomposites the overlay from its texture on every Present, so
     * tracking a smaller emulated dirty list cannot reduce the copy. Keeping
     * the dirty state still makes REFRESHDIRTYRECTS observably correct. */
    surface->overlay_dirty = TRUE;
    return DD_OK;
}

/* Resolves the clipper attached to a destination into the rectangle list the
 * blit has to be split across. Returns the count written, or 0 when the blit
 * is unclipped. */
static UINT clipper_rects(DDClipper *clipper, RECT *out, UINT capacity)
{
    UINT count = 0;

    if (!clipper) return 0;
    if (clipper->window) {
        RECT client;
        if (!GetClientRect(clipper->window, &client)) return 0;
        if (capacity) { out[0] = client; count = 1; }
        return count;
    }
    if (clipper->clip_list && clipper->clip_list->rdh.nCount) {
        const RECT *rects = (const RECT *)clipper->clip_list->Buffer;
        UINT total = clipper->clip_list->rdh.nCount;
        UINT index;
        if (total > capacity) total = capacity;
        for (index = 0; index < total; ++index)
            out[index] = rects[index];
        return total;
    }
    return 0;
}

static BOOL rect_intersect(const RECT *a, const RECT *b, RECT *out)
{
    out->left = a->left > b->left ? a->left : b->left;
    out->top = a->top > b->top ? a->top : b->top;
    out->right = a->right < b->right ? a->right : b->right;
    out->bottom = a->bottom < b->bottom ? a->bottom : b->bottom;
    return out->right > out->left && out->bottom > out->top;
}

#define DDWG_MAX_CLIP_RECTS 64u

static HRESULT WINAPI surface_Blt(IDirectDrawSurface7 *iface, LPRECT dest_rect,
        LPDIRECTDRAWSURFACE7 source_iface, LPRECT source_rect, DWORD flags,
        LPDDBLTFX fx)
{
    DDSurface *destination = surface_from_iface(iface);
    DDSurface *source = source_iface ? surface_from_iface(source_iface) : NULL;
    uint32_t blt_flags = 0;
    uint32_t fill_color = 0;
    float fill_depth = 0.0f;
    RECT effective_destination;
    RECT effective_source;
    RECT clip_rects[DDWG_MAX_CLIP_RECTS];
    UINT clip_count;
    UINT index;
    BOOL ok = TRUE;

    DDWG_TRACE_CHECKPOINT("CALL surface_Blt dst=%lu src=%lu flags=%08lX "
            "dst_rect=%ld,%ld-%ld,%ld src_rect=%ld,%ld-%ld,%ld",
            destination->handle, source ? source->handle : 0u, flags,
            dest_rect ? dest_rect->left : 0,
            dest_rect ? dest_rect->top : 0,
            dest_rect ? dest_rect->right : (LONG)destination->width,
            dest_rect ? dest_rect->bottom : (LONG)destination->height,
            source_rect ? source_rect->left : 0,
            source_rect ? source_rect->top : 0,
            source_rect ? source_rect->right :
                    (source ? (LONG)source->width : 0),
            source_rect ? source_rect->bottom :
                    (source ? (LONG)source->height : 0));
    if (destination->lock_depth)
        return DDWG_TRACE_ERROR(DDERR_SURFACEBUSY);

    effective_destination.left = 0;
    effective_destination.top = 0;
    effective_destination.right = (LONG)destination->width;
    effective_destination.bottom = (LONG)destination->height;
    if (dest_rect) effective_destination = *dest_rect;

    if (flags & DDBLT_COLORFILL) {
        if (!fx) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
        blt_flags |= D9WG_DDBLT_COLOR_FILL;
        fill_color = fx->dwFillColor;
        source = NULL;
    } else if (flags & DDBLT_DEPTHFILL) {
        DWORD depth;
        DWORD maximum;
        if (!fx) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
        if (!format_is_depth(destination->format))
            return DDWG_TRACE_ERROR(DDERR_INVALIDPIXELFORMAT);
        depth = fx->dwFillDepth;
        switch (destination->format) {
        case DDWG_FMT_D15S1: maximum = 0x7fffu; break;
        case DDWG_FMT_D16:
        case DDWG_FMT_D16_LOCKABLE: maximum = 0xffffu; break;
        case DDWG_FMT_D24S8:
        case DDWG_FMT_D24X8:
        case DDWG_FMT_D24X4S4: maximum = 0xffffffu; break;
        default: maximum = 0xffffffffu; break;
        }
        if (depth > maximum) depth = maximum;
        fill_depth = (float)depth / (float)maximum;
        blt_flags |= D9WG_DDBLT_DEPTH_FILL;
        source = NULL;
    } else {
        if (!source) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
        effective_source.left = 0;
        effective_source.top = 0;
        effective_source.right = (LONG)source->width;
        effective_source.bottom = (LONG)source->height;
        if (source_rect) effective_source = *source_rect;
        if (source->lock_depth) return DDWG_TRACE_ERROR(DDERR_SURFACEBUSY);
        if (source->palette) emit_surface_palette_binding(source);
    }

    if (flags & DDBLT_KEYSRC) {
        if (!(source && (source->color_key_present &
                (1u << D9WG_DDCKEY_SOURCE_BLT))))
            return DDWG_TRACE_ERROR(DDERR_NOCOLORKEY);
        emit_color_key(source, D9WG_DDCKEY_SOURCE_BLT,
                &source->color_key[D9WG_DDCKEY_SOURCE_BLT], TRUE);
        blt_flags |= D9WG_DDBLT_KEY_SOURCE;
    }
    if (flags & DDBLT_KEYSRCOVERRIDE) {
        if (!fx || !source) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
        /* An override key is a key for the duration of one blit. Sending it as
         * surface state around the blit keeps DD_BLT to one shape instead of
         * carrying a second key nobody else needs. */
        emit_color_key(source, D9WG_DDCKEY_SOURCE_BLT, &fx->ddckSrcColorkey,
                TRUE);
        blt_flags |= D9WG_DDBLT_KEY_SOURCE;
    }
    if (flags & DDBLT_KEYDEST) {
        if (!(destination->color_key_present &
                (1u << D9WG_DDCKEY_DESTINATION_BLT)))
            return DDWG_TRACE_ERROR(DDERR_NOCOLORKEY);
        emit_color_key(destination, D9WG_DDCKEY_DESTINATION_BLT,
                &destination->color_key[D9WG_DDCKEY_DESTINATION_BLT], TRUE);
        blt_flags |= D9WG_DDBLT_KEY_DESTINATION;
    }
    if (flags & DDBLT_KEYDESTOVERRIDE) {
        if (!fx) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
        emit_color_key(destination, D9WG_DDCKEY_DESTINATION_BLT,
                &fx->ddckDestColorkey, TRUE);
        blt_flags |= D9WG_DDBLT_KEY_DESTINATION;
    }
    if ((flags & DDBLT_DDFX) && fx) {
        if (fx->dwDDFX & DDBLTFX_MIRRORLEFTRIGHT)
            blt_flags |= D9WG_DDBLT_MIRROR_X;
        if (fx->dwDDFX & DDBLTFX_MIRRORUPDOWN)
            blt_flags |= D9WG_DDBLT_MIRROR_Y;
        if (fx->dwDDFX & (DDBLTFX_ROTATE90 | DDBLTFX_ROTATE180 |
                DDBLTFX_ROTATE270 | DDBLTFX_ARITHSTRETCHY))
            HOSTLOG_REFUSED("Blt DDBLTFX rotation/arithmetic stretch is "
                    "ignored; only mirroring is implemented");
    }
    if ((flags & DDBLT_ROP) && fx) {
        /* DirectDraw only ever required SRCCOPY and BLACKNESS of a driver. */
        if (fx->dwROP == BLACKNESS) {
            blt_flags |= D9WG_DDBLT_COLOR_FILL;
            fill_color = 0;
            source = NULL;
        } else if (fx->dwROP != SRCCOPY) {
            return UNSUPPORTED("Blt with a raster operation other than "
                    "SRCCOPY/BLACKNESS", DDERR_NORASTEROPHW);
        }
    }

    clip_count = clipper_rects(destination->clipper, clip_rects,
            DDWG_MAX_CLIP_RECTS);
    if (!clip_count) {
        ok = emit_blt(destination, &effective_destination, source,
                source ? &effective_source : NULL, blt_flags, fill_color,
                fill_depth);
    } else {
        for (index = 0; index < clip_count; ++index) {
            RECT piece;
            RECT piece_source;
            if (!rect_intersect(&effective_destination, &clip_rects[index],
                    &piece))
                continue;
            if (source) {
                LONG destination_width = effective_destination.right -
                    effective_destination.left;
                LONG destination_height = effective_destination.bottom -
                    effective_destination.top;
                LONG source_width = effective_source.right -
                    effective_source.left;
                LONG source_height = effective_source.bottom -
                    effective_source.top;
                if (destination_width <= 0 || destination_height <= 0)
                    continue;
                piece_source.left = effective_source.left +
                    MulDiv(piece.left - effective_destination.left,
                        source_width, destination_width);
                piece_source.top = effective_source.top +
                    MulDiv(piece.top - effective_destination.top,
                        source_height, destination_height);
                piece_source.right = effective_source.left +
                    MulDiv(piece.right - effective_destination.left,
                        source_width, destination_width);
                piece_source.bottom = effective_source.top +
                    MulDiv(piece.bottom - effective_destination.top,
                        source_height, destination_height);
            }
            if (!emit_blt(destination, &piece, source,
                    source ? &piece_source : NULL, blt_flags, fill_color,
                    fill_depth))
                ok = FALSE;
        }
    }

    if ((flags & DDBLT_KEYSRCOVERRIDE) && source) {
        /* Restore whatever key the surface really carries. */
        emit_color_key(source, D9WG_DDCKEY_SOURCE_BLT,
                &source->color_key[D9WG_DDCKEY_SOURCE_BLT],
                (source->color_key_present &
                    (1u << D9WG_DDCKEY_SOURCE_BLT)) != 0);
    }
    if (flags & DDBLT_KEYDESTOVERRIDE) {
        emit_color_key(destination, D9WG_DDCKEY_DESTINATION_BLT,
                &destination->color_key[D9WG_DDCKEY_DESTINATION_BLT],
                (destination->color_key_present &
                    (1u << D9WG_DDCKEY_DESTINATION_BLT)) != 0);
    }

    /* SetSurfaceDesc exposes application-owned system memory. A GPU blit into
     * it is not complete, from DirectDraw's point of view, until those bytes
     * are visible through the pointer the application supplied. */
    if (ok && destination->external_memory &&
            !surface_readback(destination))
        ok = FALSE;

    /* A blit that lands on the visible primary is visible immediately -- there
     * is no flip to wait for. This is how every windowed 2D title puts a frame
     * on screen. */
    if (ok && destination->primary && !destination->flip_next) {
        emit_blt_to_screen(destination, &effective_destination);
        emit_present_and_flush(destination->owner);
    }
    return DDWG_TRACE_RESULT(ok ? DD_OK : DDERR_GENERIC);
}

static HRESULT WINAPI surface_BltBatch(IDirectDrawSurface7 *iface,
        LPDDBLTBATCH batch, DWORD count, DWORD flags)
{
    (void)iface; (void)batch; (void)count; (void)flags;
    /* Never implemented by the retail runtime either. */
    return UNSUPPORTED("BltBatch", DDERR_UNSUPPORTED);
}

static HRESULT WINAPI surface_BltFast(IDirectDrawSurface7 *iface, DWORD x,
        DWORD y, LPDIRECTDRAWSURFACE7 source_iface, LPRECT source_rect,
        DWORD trans)
{
    DDSurface *destination = surface_from_iface(iface);
    DDSurface *source = source_iface ? surface_from_iface(source_iface) : NULL;
    RECT effective_source;
    RECT destination_rect;
    uint32_t blt_flags = 0;
    BOOL ok;

    DDWG_TRACE_CHECKPOINT("CALL surface_BltFast dst=%lu src=%lu x=%lu y=%lu "
            "flags=%08lX", destination->handle,
            source ? source->handle : 0u, x, y, trans);
    if (!source) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (destination->lock_depth || source->lock_depth)
        return DDWG_TRACE_ERROR(DDERR_SURFACEBUSY);
    if (source->palette) emit_surface_palette_binding(source);

    effective_source.left = 0;
    effective_source.top = 0;
    effective_source.right = (LONG)source->width;
    effective_source.bottom = (LONG)source->height;
    if (source_rect) effective_source = *source_rect;

    destination_rect.left = (LONG)x;
    destination_rect.top = (LONG)y;
    destination_rect.right = (LONG)x + (effective_source.right -
        effective_source.left);
    destination_rect.bottom = (LONG)y + (effective_source.bottom -
        effective_source.top);

    if (trans & DDBLTFAST_SRCCOLORKEY) {
        if (!(source->color_key_present & (1u << D9WG_DDCKEY_SOURCE_BLT)))
            return DDWG_TRACE_ERROR(DDERR_NOCOLORKEY);
        emit_color_key(source, D9WG_DDCKEY_SOURCE_BLT,
                &source->color_key[D9WG_DDCKEY_SOURCE_BLT], TRUE);
        blt_flags |= D9WG_DDBLT_KEY_SOURCE;
    }
    if (trans & DDBLTFAST_DESTCOLORKEY) {
        if (!(destination->color_key_present &
                (1u << D9WG_DDCKEY_DESTINATION_BLT)))
            return DDWG_TRACE_ERROR(DDERR_NOCOLORKEY);
        emit_color_key(destination, D9WG_DDCKEY_DESTINATION_BLT,
                &destination->color_key[D9WG_DDCKEY_DESTINATION_BLT], TRUE);
        blt_flags |= D9WG_DDBLT_KEY_DESTINATION;
    }

    ok = emit_blt(destination, &destination_rect, source, &effective_source,
            blt_flags, 0, 0.0f);
    if (ok && destination->external_memory &&
            !surface_readback(destination))
        ok = FALSE;
    if (ok && destination->primary && !destination->flip_next) {
        emit_blt_to_screen(destination, &destination_rect);
        emit_present_and_flush(destination->owner);
    }
    return DDWG_TRACE_RESULT(ok ? DD_OK : DDERR_GENERIC);
}

static HRESULT WINAPI surface_DeleteAttachedSurface(IDirectDrawSurface7 *iface,
        DWORD flags, LPDIRECTDRAWSURFACE7 attachment)
{
    DDSurface *surface = surface_from_iface(iface);
    DDSurface *other = attachment ? surface_from_iface(attachment) : NULL;

    (void)flags;
    if (other && surface->attached_depth == other) {
        surface->attached_depth = NULL;
        d3d7_refresh_render_target(surface->owner, surface);
        IDirectDrawSurface7_Release(attachment);
        return DD_OK;
    }
    if (other && surface->mip_next == other) {
        surface->mip_next = NULL;
        IDirectDrawSurface7_Release(attachment);
        return DD_OK;
    }
    if (other) {
        UINT face;
        for (face = 1u; face < 6u; ++face) {
            if (surface->cube_faces[face] != other) continue;
            surface->cube_faces[face] = NULL;
            IDirectDrawSurface7_Release(attachment);
            return DD_OK;
        }
    }
    return DDWG_TRACE_ERROR(DDERR_SURFACENOTATTACHED);
}

static HRESULT WINAPI surface_EnumAttachedSurfaces(IDirectDrawSurface7 *iface,
        LPVOID context, LPDDENUMSURFACESCALLBACK7 callback)
{
    DDSurface *surface = surface_from_iface(iface);
    DDSurface *attachment;

    if (!callback) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (surface->mip_next) {
        DDSURFACEDESC2 description = surface->mip_next->desc;
        IDirectDrawSurface7_AddRef(
                (IDirectDrawSurface7 *)surface->mip_next);
        if (callback((IDirectDrawSurface7 *)surface->mip_next, &description,
                context) == DDENUMRET_CANCEL)
            return DD_OK;
    }
    {
        UINT face;
        for (face = 1u; face < 6u; ++face) {
            DDSurface *cube = surface->cube_faces[face];
            DDSURFACEDESC2 description;
            if (!cube) continue;
            description = cube->desc;
            IDirectDrawSurface7_AddRef((IDirectDrawSurface7 *)cube);
            if (callback((IDirectDrawSurface7 *)cube, &description,
                    context) == DDENUMRET_CANCEL)
                return DD_OK;
        }
    }
    attachment = surface->flip_next;
    while (attachment && attachment != surface) {
        DDSURFACEDESC2 description = attachment->desc;
        IDirectDrawSurface7_AddRef((IDirectDrawSurface7 *)attachment);
        if (callback((IDirectDrawSurface7 *)attachment, &description,
                context) == DDENUMRET_CANCEL)
            return DD_OK;
        attachment = attachment->flip_next;
    }
    if (surface->attached_depth) {
        DDSURFACEDESC2 description = surface->attached_depth->desc;
        IDirectDrawSurface7_AddRef(
                (IDirectDrawSurface7 *)surface->attached_depth);
        callback((IDirectDrawSurface7 *)surface->attached_depth, &description,
                context);
    }
    return DD_OK;
}

static HRESULT WINAPI surface_EnumOverlayZOrders(IDirectDrawSurface7 *iface,
        DWORD flags, LPVOID context, LPDDENUMSURFACESCALLBACK7 callback)
{
    DDSurface *destination = surface_from_iface(iface);
    DDSurface *overlays[DDWG_MAX_OVERLAYS];
    DDSurface *walk;
    UINT count = 0, i, j;

    if (!callback || (flags != DDENUMOVERLAYZ_BACKTOFRONT &&
            flags != DDENUMOVERLAYZ_FRONTTOBACK))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    for (walk = destination->owner->surfaces; walk; walk = walk->next) {
        if (walk->overlay_destination == destination &&
                count < DDWG_MAX_OVERLAYS)
            overlays[count++] = walk;
    }
    for (i = 1; i < count; ++i) {
        DDSurface *value = overlays[i];
        j = i;
        while (j && overlays[j - 1]->overlay_z_order >
                value->overlay_z_order) {
            overlays[j] = overlays[j - 1];
            --j;
        }
        overlays[j] = value;
    }
    for (i = 0; i < count; ++i) {
        UINT at = flags == DDENUMOVERLAYZ_FRONTTOBACK
            ? count - 1u - i : i;
        DDSURFACEDESC2 description = overlays[at]->desc;
        surface_AddRef((IDirectDrawSurface7 *)&overlays[at]->vtbl7);
        if (callback((IDirectDrawSurface7 *)&overlays[at]->vtbl7,
                &description, context) == DDENUMRET_CANCEL)
            break;
    }
    return DD_OK;
}

static HRESULT WINAPI surface_Flip(IDirectDrawSurface7 *iface,
        LPDIRECTDRAWSURFACE7 override_iface, DWORD flags)
{
    DDSurface *surface = surface_from_iface(iface);
    DDSurface *override_surface = override_iface
        ? surface_from_iface(override_iface) : NULL;

    DDWG_TRACE_CHECKPOINT("CALL surface_Flip front=%lu override=%lu "
            "flags=%08lX has_chain=%lu", surface->handle,
            override_surface ? override_surface->handle : 0u, flags,
            surface->flip_next ? 1u : 0u);
    if (!surface->primary && !(surface->desc.ddsCaps.dwCaps & DDSCAPS_FLIP))
        return DDWG_TRACE_ERROR(DDERR_NOTFLIPPABLE);
    if (surface->lock_depth) return DDWG_TRACE_ERROR(DDERR_SURFACEBUSY);

    if (override_surface) {
        /* Flip to a named surface: that surface's contents become the front
         * buffer, and the chain is not rotated. */
        emit_blt_to_screen(override_surface, NULL);
    } else if (surface->flip_next && surface->flip_next != surface) {
        /*
         * Real DirectDraw rotates which memory the display scans out while the
         * interface pointers stay put, so an app that cached its back-buffer
         * pointer keeps drawing into the buffer that is now off screen. The
         * storage rotates here for exactly that reason: the handles and
         * shadows move between the surface objects, the objects do not move.
         */
        DDSurface *walk = surface;
        uint32_t handle = surface->handle;
        DDTextureStorage *storage = surface->storage;
        DDSurfaceMemory *memory = surface->memory;
        BYTE *shadow = surface->shadow;
        BOOL external_memory = surface->external_memory;
        DDSurface *previous = surface;

        walk = surface->flip_next;
        while (walk != surface) {
            previous->handle = walk->handle;
            previous->storage = walk->storage;
            previous->memory = walk->memory;
            previous->shadow = walk->shadow;
            previous->external_memory = walk->external_memory;
            previous = walk;
            walk = walk->flip_next;
        }
        previous->handle = handle;
        previous->storage = storage;
        previous->memory = memory;
        previous->shadow = shadow;
        previous->external_memory = external_memory;
        /* The storage moved; the per-handle host state has to follow it. */
        rebind_host_surface_state(surface);
        for (walk = surface->flip_next; walk && walk != surface;
                walk = walk->flip_next)
            rebind_host_surface_state(walk);
        d3d7_rebind_after_flip(surface->owner);
        emit_blt_to_screen(surface, NULL);
    } else {
        emit_blt_to_screen(surface, NULL);
    }
    (void)flags; /* DDFLIP_NOVSYNC/WAIT/INTERVALn: the browser's presentation
                  * cadence is not ours to choose. Recorded in README.md. */
    return DDWG_TRACE_RESULT(emit_present_and_flush(surface->owner)
            ? DD_OK : DDERR_GENERIC);
}

static HRESULT WINAPI surface_GetAttachedSurface(IDirectDrawSurface7 *iface,
        LPDDSCAPS2 caps, LPDIRECTDRAWSURFACE7 *out)
{
    DDSurface *surface = surface_from_iface(iface);
    DDSurface *walk;

    if (!caps || !out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = NULL;
    /*
     * Some DirectX 6 callers initialise only DDSCAPS2::dwCaps before asking
     * for the next mip level.  Native DirectDraw does not interpret the
     * unused dwCaps2 bits as a cube-face selector when the source surface is
     * not a cube map.  Do the same here; otherwise stack garbage that happens
     * to overlap DDSCAPS2_CUBEMAP_ALLFACES hides a valid mip attachment and
     * turns the query into DDERR_NOTFOUND.
     */
    if ((surface->desc.ddsCaps.dwCaps2 & DDSCAPS2_CUBEMAP) &&
            (caps->dwCaps2 & DDSCAPS2_CUBEMAP_ALLFACES)) {
        static const DWORD face_caps[6] = {
            DDSCAPS2_CUBEMAP_POSITIVEX, DDSCAPS2_CUBEMAP_NEGATIVEX,
            DDSCAPS2_CUBEMAP_POSITIVEY, DDSCAPS2_CUBEMAP_NEGATIVEY,
            DDSCAPS2_CUBEMAP_POSITIVEZ, DDSCAPS2_CUBEMAP_NEGATIVEZ,
        };
        UINT face;
        for (face = 0u; face < 6u; ++face) {
            DDSurface *candidate = surface->cube_faces[face];
            if (!candidate && face == surface->cube_face &&
                    surface->mip_level == 0u)
                candidate = surface;
            if (candidate && (caps->dwCaps2 & face_caps[face])) {
                *out = (IDirectDrawSurface7 *)candidate;
                IDirectDrawSurface7_AddRef(*out);
                return DD_OK;
            }
        }
        return DDWG_TRACE_ERROR(DDERR_NOTFOUND);
    }
    if (caps->dwCaps & DDSCAPS_MIPMAP) {
        if (!surface->mip_next) return DDWG_TRACE_ERROR(DDERR_NOTFOUND);
        *out = (IDirectDrawSurface7 *)surface->mip_next;
        IDirectDrawSurface7_AddRef(*out);
        return DD_OK;
    }
    if (caps->dwCaps & DDSCAPS_ZBUFFER) {
        if (!surface->attached_depth) return DDWG_TRACE_ERROR(DDERR_NOTFOUND);
        *out = (IDirectDrawSurface7 *)surface->attached_depth;
        IDirectDrawSurface7_AddRef(*out);
        return DD_OK;
    }
    walk = surface->flip_next;
    while (walk && walk != surface) {
        if ((walk->desc.ddsCaps.dwCaps & caps->dwCaps) == caps->dwCaps) {
            *out = (IDirectDrawSurface7 *)walk;
            IDirectDrawSurface7_AddRef(*out);
            return DD_OK;
        }
        walk = walk->flip_next;
    }
    return DDWG_TRACE_ERROR(DDERR_NOTFOUND);
}

static HRESULT WINAPI surface_GetBltStatus(IDirectDrawSurface7 *iface,
        DWORD flags)
{
    DDSurface *surface = surface_from_iface(iface);
    (void)surface;
    (void)flags;
    ddwg_trace_poll(&g_trace_blt_status_calls, "GetBltStatus",
            surface->handle, flags, 0u);
    /* Blits are queued to the host and never block the guest. */
    return DD_OK;
}

static HRESULT WINAPI surface_GetCaps(IDirectDrawSurface7 *iface,
        LPDDSCAPS2 caps)
{
    DDSurface *surface = surface_from_iface(iface);

    if (!caps) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *caps = surface->desc.ddsCaps;
    return DD_OK;
}

static HRESULT WINAPI surface_GetClipper(IDirectDrawSurface7 *iface,
        LPDIRECTDRAWCLIPPER *out)
{
    DDSurface *surface = surface_from_iface(iface);

    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (!surface->clipper) return DDWG_TRACE_ERROR(DDERR_NOCLIPPERATTACHED);
    *out = (IDirectDrawClipper *)surface->clipper;
    IDirectDrawClipper_AddRef(*out);
    return DD_OK;
}

static BOOL surface_color_key_kind(DWORD flags, DWORD *kind,
        DWORD *description_flag)
{
    DWORD selector = flags & ~DDCKEY_COLORSPACE;

    if (flags & ~(DDCKEY_COLORSPACE | DDCKEY_DESTBLT |
            DDCKEY_DESTOVERLAY | DDCKEY_SRCBLT | DDCKEY_SRCOVERLAY))
        return FALSE;
    switch (selector) {
    case DDCKEY_DESTBLT:
        *kind = D9WG_DDCKEY_DESTINATION_BLT;
        *description_flag = DDSD_CKDESTBLT;
        return TRUE;
    case DDCKEY_DESTOVERLAY:
        *kind = D9WG_DDCKEY_DESTINATION_OVERLAY;
        *description_flag = DDSD_CKDESTOVERLAY;
        return TRUE;
    case DDCKEY_SRCBLT:
        *kind = D9WG_DDCKEY_SOURCE_BLT;
        *description_flag = DDSD_CKSRCBLT;
        return TRUE;
    case DDCKEY_SRCOVERLAY:
        *kind = D9WG_DDCKEY_SOURCE_OVERLAY;
        *description_flag = DDSD_CKSRCOVERLAY;
        return TRUE;
    default:
        return FALSE;
    }
}

static DDCOLORKEY *surface_description_color_key(DDSurface *surface,
        DWORD kind)
{
    switch (kind) {
    case D9WG_DDCKEY_DESTINATION_BLT:
        return &surface->desc.ddckCKDestBlt;
    case D9WG_DDCKEY_DESTINATION_OVERLAY:
        return &surface->desc.ddckCKDestOverlay;
    case D9WG_DDCKEY_SOURCE_OVERLAY:
        return &surface->desc.ddckCKSrcOverlay;
    default:
        return &surface->desc.ddckCKSrcBlt;
    }
}

static HRESULT WINAPI surface_GetColorKey(IDirectDrawSurface7 *iface,
        DWORD flags, LPDDCOLORKEY key)
{
    DDSurface *surface = surface_from_iface(iface);
    DWORD kind, description_flag;

    if (!key) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (!surface_color_key_kind(flags, &kind, &description_flag))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (!(surface->color_key_present & (1u << kind)))
        return DDWG_TRACE_ERROR(DDERR_NOCOLORKEY);
    *key = surface->color_key[kind];
    return DD_OK;
}

/*
 * GDI interoperation. A DirectDraw surface has no HDC of its own here, so
 * GetDC hands back a DIB section of the same geometry seeded from the shadow,
 * and ReleaseDC copies it back and uploads. That is enough for what these
 * titles use it for -- TextOut over a menu or a score -- and it is honest
 * about what it is not: GDI drawn straight onto the primary while DirectDraw
 * owns it does not appear, which is the same boundary d7vk draws.
 */
static HRESULT WINAPI surface_GetDC(IDirectDrawSurface7 *iface, HDC *out)
{
    DDSurface *surface = surface_from_iface(iface);
    struct {
        BITMAPINFOHEADER header;
        union {
            RGBQUAD colors[256];
            DWORD masks[3];
        } table;
    } info;
    HDC screen;
    UINT index;
    UINT row;
    UINT row_bytes;

    DDWG_TRACE_CHECKPOINT("CALL surface_GetDC handle=%lu size=%lux%lu "
            "bpp=%lu", surface->handle, surface->width, surface->height,
            surface->bytes_per_pixel * 8u);
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (!surface->bytes_per_pixel) return DDWG_TRACE_ERROR(DDERR_UNSUPPORTED);
    if (surface->dc) return DDWG_TRACE_ERROR(DDERR_DCALREADYCREATED);
    if (surface->lock_depth) return DDWG_TRACE_ERROR(DDERR_SURFACEBUSY);
    /* A failed readback has already reported itself; the DC is still built
     * over the shadow, because a DC that does not exist is worse than one
     * whose GPU-written pixels are stale. */
    (void)(surface->memory->gpu_dirty && surface_readback(surface));

    ZeroMemory(&info, sizeof(info));
    info.header.biSize = sizeof(info.header);
    info.header.biWidth = (LONG)surface->width;
    /* Negative height: a DIB is bottom-up by default and a DirectDraw surface
     * is top-down. Getting this wrong renders every DC-drawn string upside
     * down in a way that looks like a blit bug. */
    info.header.biHeight = -(LONG)surface->height;
    info.header.biPlanes = 1;
    info.header.biBitCount = (WORD)(surface->bytes_per_pixel * 8u);
    info.header.biCompression = BI_RGB;
    if ((surface->bytes_per_pixel == 2u ||
            surface->bytes_per_pixel == 4u) &&
            (surface->desc.ddpfPixelFormat.dwFlags & DDPF_RGB) &&
            surface->desc.ddpfPixelFormat.dwRBitMask &&
            surface->desc.ddpfPixelFormat.dwGBitMask &&
            surface->desc.ddpfPixelFormat.dwBBitMask) {
        /* Windows defines a 16-bit BI_RGB DIB as RGB555.  DirectDraw's
         * ordinary 16-bit display mode is RGB565, so using BI_RGB silently
         * moves the low red bit into the high green bit.  GDI-created
         * textures then arrive at the host as dark red with bright green
         * stripes (dxdiag's DirectX 7 cube).  BI_BITFIELDS makes the DIB use
         * the exact masks that the surface reports and that the WebGPU upload
         * decoder consumes.  The three DWORD masks occupy bmiColors for a
         * BITMAPINFOHEADER-format DIB. */
        info.table.masks[0] = surface->desc.ddpfPixelFormat.dwRBitMask;
        info.table.masks[1] = surface->desc.ddpfPixelFormat.dwGBitMask;
        info.table.masks[2] = surface->desc.ddpfPixelFormat.dwBBitMask;
        info.header.biCompression = BI_BITFIELDS;
    } else if (surface->bytes_per_pixel == 1u) {
        info.header.biClrUsed = 256;
        for (index = 0; index < 256u; ++index) {
            if (surface->palette) {
                info.table.colors[index].rgbRed =
                        surface->palette->entries[index].peRed;
                info.table.colors[index].rgbGreen =
                        surface->palette->entries[index].peGreen;
                info.table.colors[index].rgbBlue =
                        surface->palette->entries[index].peBlue;
            }
        }
    }

    screen = GetDC(NULL);
    if (!screen) return DDWG_TRACE_ERROR(DDERR_GENERIC);
    surface->dc = CreateCompatibleDC(screen);
    ReleaseDC(NULL, screen);
    if (!surface->dc) return DDWG_TRACE_ERROR(DDERR_GENERIC);
    surface->dib = CreateDIBSection(surface->dc, (const BITMAPINFO *)&info,
            DIB_RGB_COLORS, (void **)&surface->dib_bits, NULL, 0);
    if (!surface->dib) {
        DeleteDC(surface->dc);
        surface->dc = NULL;
        return DDWG_TRACE_ERROR(DDERR_GENERIC);
    }
    surface->old_bitmap = (HBITMAP)SelectObject(surface->dc, surface->dib);
    row_bytes = surface->width * surface->bytes_per_pixel;
    for (row = 0; row < surface->height; ++row) {
        CopyMemory(surface->dib_bits + row * (((row_bytes + 3u) & ~3u)),
                surface->shadow + row * surface->pitch, row_bytes);
    }
    *out = surface->dc;
    DDWG_TRACE("SURFACE DC_CREATED handle=%lu dc=%08lX",
            surface->handle, (DWORD)(uintptr_t)surface->dc);
    return DDWG_TRACE_RESULT(DD_OK);
}

static HRESULT surface_finish_dc(DDSurface *surface, BOOL present);

static HRESULT WINAPI surface_ReleaseDC(IDirectDrawSurface7 *iface, HDC dc)
{
    DDSurface *surface = surface_from_iface(iface);
    HRESULT result;

    DDWG_TRACE_CHECKPOINT("CALL surface_ReleaseDC handle=%lu dc=%08lX",
            surface->handle, (DWORD)(uintptr_t)dc);
    if (!surface->dc || dc != surface->dc)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    result = surface_finish_dc(surface, TRUE);
    return DDWG_TRACE_RESULT(result);
}

/* Commit a DIB-backed DC to the surface shadow.  The public ReleaseDC path
 * presents a primary immediately; the windowed-primary seed below needs the
 * same upload without making the still-unmodified desktop into a new frame. */
static HRESULT surface_finish_dc(DDSurface *surface, BOOL present)
{
    UINT row;
    UINT row_bytes;

    GdiFlush();
    row_bytes = surface->width * surface->bytes_per_pixel;
    for (row = 0; row < surface->height; ++row) {
        CopyMemory(surface->shadow + row * surface->pitch,
                surface->dib_bits + row * (((row_bytes + 3u) & ~3u)),
                row_bytes);
    }
    SelectObject(surface->dc, surface->old_bitmap);
    DeleteObject(surface->dib);
    DeleteDC(surface->dc);
    surface->dc = NULL;
    surface->dib = NULL;
    surface->dib_bits = NULL;
    surface->old_bitmap = NULL;
    emit_surface_upload(surface, NULL);
    surface->memory->gpu_dirty = FALSE;
    if (present && surface->primary && !surface->flip_next) {
        emit_blt_to_screen(surface, NULL);
        emit_present_and_flush(surface->owner);
    }
    return DD_OK;
}

/*
 * DDSCL_NORMAL does not give an application a private black primary: its
 * primary is the desktop that GDI has already drawn.  Seed our host-backed
 * primary from that desktop before the first DirectDraw blit.  3DMark 2000's
 * splash copies the 420x170 rectangle behind itself, animates into the
 * primary, then restores that saved rectangle.  A zero-filled proxy primary
 * therefore turned the whole 800x600 overlay black even though the logo blit
 * and all its coordinates were correct.
 *
 * This is intentionally only the initial snapshot.  GDI writes made while
 * DirectDraw owns the primary still do not enter its texture (documented in
 * README.md), but a normal primary begins with the pixels the API promises.
 */
static BOOL seed_windowed_primary_from_screen(DDSurface *surface)
{
    HDC target;
    HDC screen;
    BOOL copied;
    HRESULT result;

    if (!surface || !surface->primary || !surface->owner ||
            !(surface->owner->cooperative_flags & DDSCL_NORMAL) ||
            (surface->owner->cooperative_flags & DDSCL_FULLSCREEN) ||
            !surface->owner->window || !IsWindow(surface->owner->window) ||
            surface->bytes_per_pixel < 2u)
        return FALSE;
    result = surface_GetDC((IDirectDrawSurface7 *)&surface->vtbl7, &target);
    if (FAILED(result)) return FALSE;
    screen = GetDC(NULL);
    copied = screen && BitBlt(target, 0, 0, (int)surface->width,
            (int)surface->height, screen, 0, 0, SRCCOPY);
    if (screen) ReleaseDC(NULL, screen);
    result = surface_finish_dc(surface, FALSE);
    DDWG_TRACE("PRIMARY DESKTOP_SEED handle=%lu copied=%lu result=%08lX",
            surface->handle, copied ? 1u : 0u, (DWORD)result);
    return copied && SUCCEEDED(result);
}

static HRESULT WINAPI surface_GetFlipStatus(IDirectDrawSurface7 *iface,
        DWORD flags)
{
    DDSurface *surface = surface_from_iface(iface);
    (void)surface;
    (void)flags;
    ddwg_trace_poll(&g_trace_flip_status_calls, "GetFlipStatus",
            surface->handle, flags, 0u);
    return DD_OK;
}

static HRESULT WINAPI surface_GetOverlayPosition(IDirectDrawSurface7 *iface,
        LPLONG x, LPLONG y)
{
    DDSurface *surface = surface_from_iface(iface);
    if (!x || !y) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (!(surface->desc.ddsCaps.dwCaps & DDSCAPS_OVERLAY))
        return DDWG_TRACE_ERROR(DDERR_NOTAOVERLAYSURFACE);
    if (!surface->overlay_destination) return DDWG_TRACE_ERROR(DDERR_NOOVERLAYDEST);
    *x = surface->overlay_destination_rect.left;
    *y = surface->overlay_destination_rect.top;
    return DD_OK;
}

static HRESULT WINAPI surface_GetPalette(IDirectDrawSurface7 *iface,
        LPDIRECTDRAWPALETTE *out)
{
    DDSurface *surface = surface_from_iface(iface);

    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (!surface->palette) return DDWG_TRACE_ERROR(DDERR_NOPALETTEATTACHED);
    *out = (IDirectDrawPalette *)surface->palette;
    IDirectDrawPalette_AddRef(*out);
    return DD_OK;
}

static HRESULT WINAPI surface_GetPixelFormat(IDirectDrawSurface7 *iface,
        LPDDPIXELFORMAT format)
{
    DDSurface *surface = surface_from_iface(iface);

    if (!format) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *format = surface->desc.ddpfPixelFormat;
    format->dwSize = sizeof(*format);
    return DD_OK;
}

static HRESULT WINAPI surface_GetSurfaceDesc(IDirectDrawSurface7 *iface,
        LPDDSURFACEDESC2 description)
{
    DDSurface *surface = surface_from_iface(iface);

    if (!description) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (description->dwSize != sizeof(DDSURFACEDESC2))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *description = surface->desc;
    description->dwSize = sizeof(DDSURFACEDESC2);
    return DD_OK;
}

static HRESULT WINAPI surface_Initialize(IDirectDrawSurface7 *iface,
        LPDIRECTDRAW dd, LPDDSURFACEDESC2 description)
{
    (void)iface; (void)dd; (void)description;
    /* DirectDraw creates its surfaces initialised; the API says so. */
    return DDWG_TRACE_ERROR(DDERR_ALREADYINITIALIZED);
}

static HRESULT WINAPI surface_IsLost(IDirectDrawSurface7 *iface)
{
    (void)iface;
    /* Surfaces live in guest memory plus a host texture, and neither is lost
     * on a mode change or a task switch. A title that calls Restore anyway
     * gets DD_OK from it. */
    return DD_OK;
}

static HRESULT WINAPI surface_Lock(IDirectDrawSurface7 *iface, LPRECT rect,
        LPDDSURFACEDESC2 description, DWORD flags, HANDLE event)
{
    DDSurface *surface = surface_from_iface(iface);
    LONG left = 0, top = 0;

    (void)event;
    DDWG_TRACE_CHECKPOINT("CALL surface_Lock handle=%lu flags=%08lX "
            "rect=%ld,%ld-%ld,%ld depth=%lu", surface->handle, flags,
            rect ? rect->left : 0, rect ? rect->top : 0,
            rect ? rect->right : (LONG)surface->width,
            rect ? rect->bottom : (LONG)surface->height,
            surface->lock_depth);
    if (!description) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (description->dwSize != sizeof(DDSURFACEDESC2))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (!surface->shadow) return DDWG_TRACE_ERROR(DDERR_GENERIC);
    if (surface->dc) return DDWG_TRACE_ERROR(DDERR_SURFACEBUSY);

    if (surface->memory->gpu_dirty && !(flags & DDLOCK_WRITEONLY))
        surface_readback(surface);

    if (rect) {
        if (rect->left < 0 || rect->top < 0 ||
                rect->right > (LONG)surface->width ||
                rect->bottom > (LONG)surface->height ||
                rect->right <= rect->left || rect->bottom <= rect->top)
            return DDWG_TRACE_ERROR(DDERR_INVALIDRECT);
        if (format_is_compressed(surface->format) &&
                ((rect->left & 3) || (rect->top & 3) ||
                 ((rect->right & 3) && rect->right != (LONG)surface->width) ||
                 ((rect->bottom & 3) &&
                    rect->bottom != (LONG)surface->height)))
            return DDWG_TRACE_ERROR(DDERR_INVALIDRECT);
        surface->locked_rect = *rect;
        left = rect->left;
        top = rect->top;
    } else {
        surface->locked_rect.left = 0;
        surface->locked_rect.top = 0;
        surface->locked_rect.right = (LONG)surface->width;
        surface->locked_rect.bottom = (LONG)surface->height;
    }
    surface->lock_flags = flags;
    ++surface->lock_depth;

    *description = surface->desc;
    description->dwSize = sizeof(DDSURFACEDESC2);
    description->dwFlags |= DDSD_LPSURFACE | DDSD_PITCH;
    description->lPitch = (LONG)surface->pitch;
    if (format_is_compressed(surface->format))
        description->lpSurface = surface->shadow + (top / 4) * surface->pitch
            + (left / 4) * format_compressed_block_bytes(surface->format);
    else
        description->lpSurface = surface->shadow + top * surface->pitch
            + left * surface->bytes_per_pixel;
    DDWG_TRACE("SURFACE LOCKED handle=%lu pitch=%lu bits=%08lX depth=%lu",
            surface->handle, surface->pitch,
            (DWORD)(uintptr_t)description->lpSurface, surface->lock_depth);
    return DDWG_TRACE_RESULT(DD_OK);
}

static HRESULT WINAPI surface_Restore(IDirectDrawSurface7 *iface)
{
    (void)iface;
    return DD_OK;
}

static HRESULT WINAPI surface_SetClipper(IDirectDrawSurface7 *iface,
        LPDIRECTDRAWCLIPPER clipper_iface)
{
    DDSurface *surface = surface_from_iface(iface);
    DDClipper *clipper = clipper_iface ? clipper_from_iface(clipper_iface)
        : NULL;

    if (surface->clipper)
        IDirectDrawClipper_Release((IDirectDrawClipper *)surface->clipper);
    surface->clipper = clipper;
    if (clipper)
        IDirectDrawClipper_AddRef(clipper_iface);
    return DD_OK;
}

static HRESULT WINAPI surface_SetColorKey(IDirectDrawSurface7 *iface,
        DWORD flags, LPDDCOLORKEY key)
{
    DDSurface *surface = surface_from_iface(iface);
    DDSurface *walk;
    DWORD kind, description_flag;

    if (!surface_color_key_kind(flags, &kind, &description_flag))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);

    if (!key) {
        surface->color_key_present &= ~(1u << kind);
        surface->desc.dwFlags &= ~description_flag;
        emit_color_key(surface, kind, NULL, FALSE);
    } else {
        surface->color_key[kind] = *key;
        surface->color_key_present |= (1u << kind);
        surface->desc.dwFlags |= description_flag;
        *surface_description_color_key(surface, kind) = *key;
        emit_color_key(surface, kind, key, TRUE);
    }

    /* A visible overlay persists on the host between calls. Refresh its
     * snapshotted key immediately, including every overlay using this surface
     * as its destination. This is also what keeps DuplicateSurface aliases'
     * independent keys from collapsing onto their shared texture handle. */
    if (kind == D9WG_DDCKEY_SOURCE_OVERLAY && surface->overlay_visible)
        emit_overlay_state(surface);
    if (kind == D9WG_DDCKEY_DESTINATION_OVERLAY) {
        for (walk = surface->owner->surfaces; walk; walk = walk->next) {
            if (walk->overlay_visible && walk->overlay_destination == surface)
                emit_overlay_state(walk);
        }
    }
    return DD_OK;
}

static HRESULT WINAPI surface_SetOverlayPosition(IDirectDrawSurface7 *iface,
        LONG x, LONG y)
{
    DDSurface *surface = surface_from_iface(iface);
    LONG width, height;
    if (!(surface->desc.ddsCaps.dwCaps & DDSCAPS_OVERLAY))
        return DDWG_TRACE_ERROR(DDERR_NOTAOVERLAYSURFACE);
    if (!surface->overlay_destination) return DDWG_TRACE_ERROR(DDERR_NOOVERLAYDEST);
    width = surface->overlay_destination_rect.right -
        surface->overlay_destination_rect.left;
    height = surface->overlay_destination_rect.bottom -
        surface->overlay_destination_rect.top;
    surface->overlay_destination_rect.left = x;
    surface->overlay_destination_rect.top = y;
    surface->overlay_destination_rect.right = x + width;
    surface->overlay_destination_rect.bottom = y + height;
    return DDWG_TRACE_ERROR(emit_overlay_state(surface)
            ? DD_OK : DDERR_GENERIC);
}

static HRESULT WINAPI surface_SetPalette(IDirectDrawSurface7 *iface,
        LPDIRECTDRAWPALETTE palette_iface)
{
    DDSurface *surface = surface_from_iface(iface);
    DDPalette *palette = palette_iface ? palette_from_iface(palette_iface)
        : NULL;

    if (surface->palette)
        IDirectDrawPalette_Release((IDirectDrawPalette *)surface->palette);
    surface->palette = palette;
    if (palette) {
        IDirectDrawPalette_AddRef(palette_iface);
        emit_palette_table(palette);
        emit_surface_palette_binding(surface);
    }
    return DD_OK;
}

static HRESULT WINAPI surface_Unlock(IDirectDrawSurface7 *iface, LPRECT rect)
{
    DDSurface *surface = surface_from_iface(iface);
    RECT uploaded;

    DDWG_TRACE_CHECKPOINT("CALL surface_Unlock handle=%lu rect=%ld,%ld-%ld,%ld "
            "depth=%lu flags=%08lX", surface->handle,
            rect ? rect->left : surface->locked_rect.left,
            rect ? rect->top : surface->locked_rect.top,
            rect ? rect->right : surface->locked_rect.right,
            rect ? rect->bottom : surface->locked_rect.bottom,
            surface->lock_depth, surface->lock_flags);
    if (!surface->lock_depth) return DDWG_TRACE_ERROR(DDERR_NOTLOCKED);
    --surface->lock_depth;
    uploaded = surface->locked_rect;
    if (rect) uploaded = *rect;
    /* A read-only lock changed nothing, so there is nothing to send. */
    if (!(surface->lock_flags & DDLOCK_READONLY)) {
        emit_surface_upload(surface, &uploaded);
        surface->memory->gpu_dirty = FALSE;
        if (surface->primary && !surface->flip_next) {
            emit_blt_to_screen(surface, &uploaded);
            emit_present_and_flush(surface->owner);
        }
    }
    return DDWG_TRACE_RESULT(DD_OK);
}

static HRESULT WINAPI surface_UpdateOverlay(IDirectDrawSurface7 *iface,
        LPRECT source_rect, LPDIRECTDRAWSURFACE7 destination,
        LPRECT destination_rect, DWORD flags, LPDDOVERLAYFX fx)
{
    const DWORD unsupported = DDOVER_ALPHADEST |
        DDOVER_ALPHADESTCONSTOVERRIDE | DDOVER_ALPHADESTNEG |
        DDOVER_ALPHADESTSURFACEOVERRIDE | DDOVER_ALPHAEDGEBLEND |
        DDOVER_ALPHASRC | DDOVER_ALPHASRCCONSTOVERRIDE |
        DDOVER_ALPHASRCNEG | DDOVER_ALPHASRCSURFACEOVERRIDE |
        DDOVER_AUTOFLIP | DDOVER_BOB | DDOVER_OVERRIDEBOBWEAVE |
        DDOVER_INTERLEAVED | DDOVER_BOBHARDWARE |
        DDOVER_ARGBSCALEFACTORS | DDOVER_DEGRADEARGBSCALING;
    DDSurface *surface = surface_from_iface(iface);
    DDSurface *target = destination ? surface_from_iface(destination)
        : surface->overlay_destination;
    DDSurface *walk;
    RECT source_area;
    RECT destination_area;
    uint32_t wire_flags = 0;
    LONG maximum_z = -1;
    BOOL ok;

    if (!(surface->desc.ddsCaps.dwCaps & DDSCAPS_OVERLAY))
        return DDWG_TRACE_ERROR(DDERR_NOTAOVERLAYSURFACE);
    if ((flags & DDOVER_SHOW) && (flags & DDOVER_HIDE))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (flags & unsupported)
        return UNSUPPORTED("UpdateOverlay video-port/alpha mode",
                DDERR_UNSUPPORTED);
    if ((flags & (DDOVER_DDFX | DDOVER_KEYSRCOVERRIDE |
            DDOVER_KEYDESTOVERRIDE)) &&
            (!fx || fx->dwSize != sizeof(*fx)))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);

    if ((flags & DDOVER_HIDE) || (!destination && !destination_rect &&
            !source_rect && !(flags & (DDOVER_SHOW | DDOVER_REFRESHALL |
                DDOVER_REFRESHDIRTYRECTS)))) {
        surface->overlay_visible = FALSE;
        return DDWG_TRACE_ERROR(emit_overlay_state(surface)
                ? DD_OK : DDERR_GENERIC);
    }
    if (!target) return DDWG_TRACE_ERROR(DDERR_NOOVERLAYDEST);
    if (target->owner != surface->owner) return DDWG_TRACE_ERROR(DDERR_INVALIDOBJECT);

    source_area.left = 0;
    source_area.top = 0;
    source_area.right = (LONG)surface->width;
    source_area.bottom = (LONG)surface->height;
    if (source_rect) source_area = *source_rect;
    if (source_area.left < 0 || source_area.top < 0 ||
            source_area.right > (LONG)surface->width ||
            source_area.bottom > (LONG)surface->height ||
            source_area.right <= source_area.left ||
            source_area.bottom <= source_area.top)
        return DDWG_TRACE_ERROR(DDERR_INVALIDRECT);

    if (destination_rect) destination_area = *destination_rect;
    else if (surface->overlay_destination)
        destination_area = surface->overlay_destination_rect;
    else
        return DDWG_TRACE_ERROR(DDERR_INVALIDRECT);
    if (destination_area.right <= destination_area.left ||
            destination_area.bottom <= destination_area.top)
        return DDWG_TRACE_ERROR(DDERR_INVALIDRECT);

    if (flags & DDOVER_KEYSRC) {
        if (!(surface->color_key_present &
                (1u << D9WG_DDCKEY_SOURCE_OVERLAY)))
            return DDWG_TRACE_ERROR(DDERR_NOCOLORKEY);
        wire_flags |= D9WG_DDOVER_KEY_SOURCE;
    }
    if (flags & DDOVER_KEYSRCOVERRIDE) {
        surface->overlay_source_key_override = fx->dckSrcColorkey;
        wire_flags |= D9WG_DDOVER_KEY_SOURCE |
            D9WG_DDOVER_KEY_SOURCE_OVERRIDE;
    }
    if (flags & DDOVER_KEYDEST) {
        if (!(target->color_key_present &
                (1u << D9WG_DDCKEY_DESTINATION_OVERLAY)))
            return DDWG_TRACE_ERROR(DDERR_NOCOLORKEY);
        wire_flags |= D9WG_DDOVER_KEY_DESTINATION;
    }
    if (flags & DDOVER_KEYDESTOVERRIDE) {
        surface->overlay_destination_key_override = fx->dckDestColorkey;
        wire_flags |= D9WG_DDOVER_KEY_DESTINATION |
            D9WG_DDOVER_KEY_DESTINATION_OVERRIDE;
    }
    if ((flags & DDOVER_DDFX) &&
            (fx->dwDDFX & DDOVERFX_MIRRORLEFTRIGHT))
        wire_flags |= D9WG_DDOVER_MIRROR_X;
    if ((flags & DDOVER_DDFX) &&
            (fx->dwDDFX & DDOVERFX_MIRRORUPDOWN))
        wire_flags |= D9WG_DDOVER_MIRROR_Y;

    if (surface->overlay_destination != target) {
        for (walk = surface->owner->surfaces; walk; walk = walk->next) {
            if (walk != surface && walk->overlay_destination == target &&
                    walk->overlay_z_order > maximum_z)
                maximum_z = walk->overlay_z_order;
        }
        if (surface->overlay_destination)
            surface_Release((IDirectDrawSurface7 *)&
                    surface->overlay_destination->vtbl7);
        surface->overlay_destination = target;
        surface_AddRef((IDirectDrawSurface7 *)&target->vtbl7);
        surface->overlay_z_order = maximum_z + 1;
    }
    surface->overlay_source_rect = source_area;
    surface->overlay_destination_rect = destination_area;
    surface->overlay_wire_flags = wire_flags;
    if (flags & DDOVER_SHOW) surface->overlay_visible = TRUE;
    if (flags & (DDOVER_REFRESHALL | DDOVER_REFRESHDIRTYRECTS))
        surface->overlay_dirty = FALSE;
    ok = emit_overlay_state(surface);
    return DDWG_TRACE_ERROR(ok ? DD_OK : DDERR_GENERIC);
}

static HRESULT WINAPI surface_UpdateOverlayDisplay(IDirectDrawSurface7 *iface,
        DWORD flags)
{
    (void)iface; (void)flags;
    /* Microsoft documents this method as unimplemented even in DirectX 7. */
    return DDWG_TRACE_ERROR(DDERR_UNSUPPORTED);
}

static HRESULT WINAPI surface_UpdateOverlayZOrder(IDirectDrawSurface7 *iface,
        DWORD flags, LPDIRECTDRAWSURFACE7 reference)
{
    DDSurface *surface = surface_from_iface(iface);
    DDSurface *relative = reference ? surface_from_iface(reference) : NULL;
    DDSurface *ordered[DDWG_MAX_OVERLAYS];
    DDSurface *walk;
    UINT count = 0, i, j, old_index = 0, insert_at;

    if (!(surface->desc.ddsCaps.dwCaps & DDSCAPS_OVERLAY))
        return DDWG_TRACE_ERROR(DDERR_NOTAOVERLAYSURFACE);
    if (!surface->overlay_destination) return DDWG_TRACE_ERROR(DDERR_NOOVERLAYDEST);
    if (flags > DDOVERZ_INSERTINBACKOF) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if ((flags == DDOVERZ_INSERTINFRONTOF ||
            flags == DDOVERZ_INSERTINBACKOF) &&
            (!relative || relative == surface ||
             relative->overlay_destination != surface->overlay_destination))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);

    for (walk = surface->owner->surfaces; walk; walk = walk->next) {
        if (walk->overlay_destination == surface->overlay_destination &&
                count < DDWG_MAX_OVERLAYS)
            ordered[count++] = walk;
    }
    for (i = 1; i < count; ++i) {
        DDSurface *value = ordered[i];
        j = i;
        while (j && ordered[j - 1]->overlay_z_order >
                value->overlay_z_order) {
            ordered[j] = ordered[j - 1];
            --j;
        }
        ordered[j] = value;
    }
    for (i = 0; i < count; ++i) {
        if (ordered[i] == surface) { old_index = i; break; }
    }
    for (i = old_index; i + 1u < count; ++i) ordered[i] = ordered[i + 1u];
    --count;
    switch (flags) {
    case DDOVERZ_SENDTOFRONT: insert_at = count; break;
    case DDOVERZ_SENDTOBACK: insert_at = 0; break;
    case DDOVERZ_MOVEFORWARD:
        insert_at = old_index < count ? old_index + 1u : count;
        if (insert_at > count) insert_at = count;
        break;
    case DDOVERZ_MOVEBACKWARD:
        insert_at = old_index ? old_index - 1u : 0u;
        break;
    case DDOVERZ_INSERTINFRONTOF:
    case DDOVERZ_INSERTINBACKOF:
        insert_at = 0;
        while (insert_at < count && ordered[insert_at] != relative)
            ++insert_at;
        if (flags == DDOVERZ_INSERTINFRONTOF) ++insert_at;
        break;
    default: return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    }
    for (i = count; i > insert_at; --i) ordered[i] = ordered[i - 1u];
    ordered[insert_at] = surface;
    ++count;
    for (i = 0; i < count; ++i) {
        ordered[i]->overlay_z_order = (LONG)i;
        emit_overlay_state(ordered[i]);
    }
    return DD_OK;
}

static HRESULT WINAPI surface_GetDDInterface(IDirectDrawSurface7 *iface,
        LPVOID *out)
{
    DDSurface *surface = surface_from_iface(iface);

    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = (IDirectDraw7 *)surface->owner;
    IDirectDraw7_AddRef((IDirectDraw7 *)surface->owner);
    return DD_OK;
}

static HRESULT WINAPI surface_PageLock(IDirectDrawSurface7 *iface, DWORD flags)
{
    (void)iface; (void)flags;
    /* The shadow is ordinary process memory and never moves. */
    return DD_OK;
}

static HRESULT WINAPI surface_PageUnlock(IDirectDrawSurface7 *iface,
        DWORD flags)
{
    (void)iface; (void)flags;
    return DD_OK;
}

static HRESULT WINAPI surface_SetSurfaceDesc(IDirectDrawSurface7 *iface,
        LPDDSURFACEDESC2 description, DWORD flags)
{
    const DWORD allowed = DDSD_LPSURFACE | DDSD_PIXELFORMAT | DDSD_WIDTH |
        DDSD_HEIGHT | DDSD_PITCH | DDSD_CAPS;
    DDSurface *surface = surface_from_iface(iface);
    DDSURFACEDESC2 new_desc;
    DDrawPixelFormatDesc format_desc;
    DDSurfaceMemory *new_memory;
    DDTextureStorage *new_storage = NULL;
    DDTextureStorage *old_storage;
    DDSurfaceMemory *old_memory;
    unsigned new_format;
    UINT width, height, bytes_per_pixel, block_bytes, pitch, rows;
    SIZE_T bytes;
    BOOL recreate;

    if (!description || description->dwSize != sizeof(*description) || flags)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (!(surface->desc.ddsCaps.dwCaps & DDSCAPS_SYSTEMMEMORY) ||
            (surface->desc.ddsCaps.dwCaps & DDSCAPS_PRIMARYSURFACE) ||
            surface->implicit)
        return DDWG_TRACE_ERROR(DDERR_INVALIDSURFACETYPE);
    if (surface->lock_depth || surface->dc) return DDWG_TRACE_ERROR(DDERR_SURFACEBUSY);
    if (description->dwFlags & ~allowed) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (!(description->dwFlags & DDSD_LPSURFACE) || !description->lpSurface)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if ((description->dwFlags & DDSD_CAPS) &&
            (description->ddsCaps.dwCaps || description->ddsCaps.dwCaps2 ||
             description->ddsCaps.dwCaps3 || description->ddsCaps.dwCaps4))
        return DDWG_TRACE_ERROR(DDERR_INVALIDCAPS);
    if ((description->dwFlags & DDSD_WIDTH) &&
            !(description->dwFlags & DDSD_PITCH))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if ((description->dwFlags & DDSD_PITCH) &&
            !(description->dwFlags & DDSD_WIDTH))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (surface->mip_next || surface->cube_faces[1] ||
            surface->cube_faces[2] || surface->cube_faces[3] ||
            surface->cube_faces[4] || surface->cube_faces[5])
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);

    width = (description->dwFlags & DDSD_WIDTH)
        ? description->dwWidth : surface->width;
    height = (description->dwFlags & DDSD_HEIGHT)
        ? description->dwHeight : surface->height;
    if (!width || !height) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);

    new_desc = surface->desc;
    if (description->dwFlags & DDSD_PIXELFORMAT)
        new_desc.ddpfPixelFormat = description->ddpfPixelFormat;
    describe_pixel_format(&new_desc.ddpfPixelFormat, &format_desc);
    new_format = ddraw_pixel_format_to_d3dformat(&format_desc);
    if (new_format == DDWG_FMT_UNKNOWN) return DDWG_TRACE_ERROR(DDERR_INVALIDPIXELFORMAT);
    bytes_per_pixel = format_bytes_per_pixel(new_format);
    block_bytes = format_compressed_block_bytes(new_format);
    if (!bytes_per_pixel && !block_bytes) return DDWG_TRACE_ERROR(DDERR_INVALIDPIXELFORMAT);
    if (description->dwFlags & DDSD_PITCH) {
        if (description->lPitch <= 0 || (description->lPitch & 3))
            return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
        pitch = (UINT)description->lPitch;
    } else if (block_bytes) {
        pitch = ((width + 3u) / 4u) * block_bytes;
    } else {
        pitch = surface_pitch_for(width, bytes_per_pixel);
    }
    if (block_bytes) {
        if (pitch < ((width + 3u) / 4u) * block_bytes)
            return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
        rows = (height + 3u) / 4u;
    } else {
        if (pitch < width * bytes_per_pixel) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
        rows = height;
    }
    bytes = (SIZE_T)pitch * rows;
    if (pitch && bytes / pitch != rows) return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY);
    new_memory = surface_memory_create((BYTE *)description->lpSurface,
            bytes, FALSE);
    if (!new_memory) return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY);

    recreate = width != surface->width || height != surface->height ||
        new_format != surface->format || surface->storage->ref > 1;
    if (recreate) {
        new_storage = surface_storage_allocate();
        if (!new_storage) {
            surface_memory_release(new_memory);
            return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY);
        }
    }

    old_storage = surface->storage;
    old_memory = surface->memory;
    new_desc.dwFlags |= DDSD_LPSURFACE | DDSD_WIDTH | DDSD_HEIGHT |
        DDSD_PITCH | DDSD_PIXELFORMAT;
    new_desc.dwFlags &= ~DDSD_LINEARSIZE;
    new_desc.lpSurface = description->lpSurface;
    new_desc.dwWidth = width;
    new_desc.dwHeight = height;
    new_desc.lPitch = (LONG)pitch;
    new_desc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);

    surface->desc = new_desc;
    surface->format = new_format;
    surface->indexed = ddraw_format_is_palettised(new_format);
    surface->width = width;
    surface->height = height;
    surface->bytes_per_pixel = bytes_per_pixel;
    surface->pitch = pitch;
    surface->memory = new_memory;
    surface->shadow = new_memory->bits;
    surface->external_memory = TRUE;
    if (new_storage) {
        surface->storage = new_storage;
        surface->handle = new_storage->handle;
        /* The rollback that used to live here existed for a host texture that
         * could not be created. That is now an owed texture rather than a
         * failure -- rolling the description back behind a caller whose own
         * memory is already installed would be the worse of the two. */
        if (!emit_surface_create(surface))
            surface->upload_pending = TRUE;
    }
    if (!emit_surface_upload(surface, NULL)) {
        /* The app-facing description is still valid and Lock must expose the
         * supplied pointer. Keep it installed; a later source use retries the
         * upload instead of rolling the COM object back behind the caller. */
        surface->memory->gpu_dirty = FALSE;
    }
    rebind_host_surface_state(surface);
    surface_memory_release(old_memory);
    if (new_storage) surface_storage_release(old_storage);
    return DD_OK;
}

static HRESULT WINAPI surface_SetPrivateData(IDirectDrawSurface7 *iface,
        REFGUID tag, LPVOID data, DWORD bytes, DWORD flags)
{
    (void)iface; (void)tag; (void)data; (void)bytes; (void)flags;
    return UNSUPPORTED("Surface SetPrivateData", DDERR_UNSUPPORTED);
}

static HRESULT WINAPI surface_GetPrivateData(IDirectDrawSurface7 *iface,
        REFGUID tag, LPVOID buffer, LPDWORD bytes)
{
    (void)iface; (void)tag; (void)buffer; (void)bytes;
    return DDWG_TRACE_ERROR(DDERR_NOTFOUND);
}

static HRESULT WINAPI surface_FreePrivateData(IDirectDrawSurface7 *iface,
        REFGUID tag)
{
    (void)iface; (void)tag;
    return DDWG_TRACE_ERROR(DDERR_NOTFOUND);
}

static HRESULT WINAPI surface_GetUniquenessValue(IDirectDrawSurface7 *iface,
        LPDWORD value)
{
    DDSurface *surface = surface_from_iface(iface);

    if (!value) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *value = (DWORD)surface->handle;
    return DD_OK;
}

static HRESULT WINAPI surface_ChangeUniquenessValue(IDirectDrawSurface7 *iface)
{
    (void)iface;
    return DD_OK;
}

static HRESULT WINAPI surface_SetPriority(IDirectDrawSurface7 *iface,
        DWORD priority)
{
    (void)iface; (void)priority;
    /* A managed-texture eviction hint. Nothing is evicted here. */
    return DD_OK;
}

static HRESULT WINAPI surface_GetPriority(IDirectDrawSurface7 *iface,
        LPDWORD priority)
{
    (void)iface;
    if (!priority) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *priority = 0;
    return DD_OK;
}

static HRESULT WINAPI surface_SetLOD(IDirectDrawSurface7 *iface, DWORD lod)
{
    (void)iface; (void)lod;
    return DD_OK;
}

static HRESULT WINAPI surface_GetLOD(IDirectDrawSurface7 *iface, LPDWORD lod)
{
    (void)iface;
    if (!lod) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *lod = 0;
    return DD_OK;
}

/* ------------------------------------------------------------------ *
 * IDirectDrawPalette
 * ------------------------------------------------------------------ */

static HRESULT WINAPI palette_QueryInterface(IDirectDrawPalette *iface,
        REFIID iid, void **out)
{
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = NULL;
    if (iid_is_unknown(iid) || guid_equal(iid, &IID_IDirectDrawPalette)) {
        IDirectDrawPalette_AddRef(iface);
        *out = iface;
        return DD_OK;
    }
    return DDWG_TRACE_ERROR(E_NOINTERFACE);
}

static ULONG WINAPI palette_AddRef(IDirectDrawPalette *iface)
{
    return (ULONG)InterlockedIncrement(&palette_from_iface(iface)->ref);
}

static ULONG WINAPI palette_Release(IDirectDrawPalette *iface)
{
    DDPalette *palette = palette_from_iface(iface);
    LONG remaining = InterlockedDecrement(&palette->ref);

    if (remaining == 0) {
        EnterCriticalSection(&g_object_lock);
        if (palette->owner) {
            DDPalette **link = &palette->owner->palettes;
            while (*link && *link != palette) link = &(*link)->next;
            if (*link) *link = palette->next;
        }
        LeaveCriticalSection(&g_object_lock);
        heap_free(palette);
    }
    return (ULONG)(remaining < 0 ? 0 : remaining);
}

static HRESULT WINAPI palette_GetCaps(IDirectDrawPalette *iface, LPDWORD caps)
{
    DDPalette *palette = palette_from_iface(iface);

    if (!caps) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *caps = palette->caps;
    return DD_OK;
}

static HRESULT WINAPI palette_GetEntries(IDirectDrawPalette *iface,
        DWORD flags, DWORD base, DWORD count, LPPALETTEENTRY entries)
{
    DDPalette *palette = palette_from_iface(iface);
    DWORD index;

    (void)flags;
    if (!entries || base > 256u || count > 256u - base)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    for (index = 0; index < count; ++index)
        entries[index] = palette->entries[base + index];
    return DD_OK;
}

static HRESULT WINAPI palette_Initialize(IDirectDrawPalette *iface,
        LPDIRECTDRAW dd, DWORD flags, LPPALETTEENTRY entries)
{
    (void)iface; (void)dd; (void)flags; (void)entries;
    return DDWG_TRACE_ERROR(DDERR_ALREADYINITIALIZED);
}

static HRESULT WINAPI palette_SetEntries(IDirectDrawPalette *iface,
        DWORD flags, DWORD start, DWORD count, LPPALETTEENTRY entries)
{
    DDPalette *palette = palette_from_iface(iface);
    DWORD index;

    (void)flags;
    if (!entries || start > 256u || count > 256u - start)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    for (index = 0; index < count; ++index)
        palette->entries[start + index] = entries[index];
    /*
     * The whole table goes out even when a few entries changed. A palette is
     * one kilobyte; splitting it into ranges would save nothing measurable and
     * would need the host to track which entries it has. Palette animation --
     * water, fades, flickering torchlight -- is per-frame in these titles, and
     * this is the path that carries it.
     */
    emit_palette_table(palette);
    return DD_OK;
}

/* ------------------------------------------------------------------ *
 * IDirectDrawClipper
 * ------------------------------------------------------------------ */

static HRESULT WINAPI clipper_QueryInterface(IDirectDrawClipper *iface,
        REFIID iid, void **out)
{
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = NULL;
    if (iid_is_unknown(iid) || guid_equal(iid, &IID_IDirectDrawClipper)) {
        IDirectDrawClipper_AddRef(iface);
        *out = iface;
        return DD_OK;
    }
    return DDWG_TRACE_ERROR(E_NOINTERFACE);
}

static ULONG WINAPI clipper_AddRef(IDirectDrawClipper *iface)
{
    return (ULONG)InterlockedIncrement(&clipper_from_iface(iface)->ref);
}

static ULONG WINAPI clipper_Release(IDirectDrawClipper *iface)
{
    DDClipper *clipper = clipper_from_iface(iface);
    LONG remaining = InterlockedDecrement(&clipper->ref);

    if (remaining == 0) {
        EnterCriticalSection(&g_object_lock);
        if (clipper->owner) {
            DDClipper **link = &clipper->owner->clippers;
            while (*link && *link != clipper) link = &(*link)->next;
            if (*link) *link = clipper->next;
        }
        LeaveCriticalSection(&g_object_lock);
        heap_free(clipper->clip_list);
        heap_free(clipper);
    }
    return (ULONG)(remaining < 0 ? 0 : remaining);
}

static HRESULT WINAPI clipper_GetClipList(IDirectDrawClipper *iface,
        LPRECT rect, LPRGNDATA list, LPDWORD size)
{
    DDClipper *clipper = clipper_from_iface(iface);
    DWORD needed;

    (void)rect;
    if (!size) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (clipper->window) {
        RECT client;
        needed = (DWORD)(sizeof(RGNDATAHEADER) + sizeof(RECT));
        if (!list) { *size = needed; return DD_OK; }
        if (*size < needed) return DDWG_TRACE_ERROR(DDERR_REGIONTOOSMALL);
        if (!GetClientRect(clipper->window, &client))
            return DDWG_TRACE_ERROR(DDERR_GENERIC);
        ZeroMemory(list, needed);
        list->rdh.dwSize = sizeof(RGNDATAHEADER);
        list->rdh.iType = RDH_RECTANGLES;
        list->rdh.nCount = 1;
        list->rdh.nRgnSize = sizeof(RECT);
        list->rdh.rcBound = client;
        CopyMemory(list->Buffer, &client, sizeof(client));
        *size = needed;
        return DD_OK;
    }
    if (!clipper->clip_list) return DDWG_TRACE_ERROR(DDERR_NOCLIPLIST);
    needed = clipper->clip_list_bytes;
    if (!list) { *size = needed; return DD_OK; }
    if (*size < needed) return DDWG_TRACE_ERROR(DDERR_REGIONTOOSMALL);
    CopyMemory(list, clipper->clip_list, needed);
    *size = needed;
    return DD_OK;
}

static HRESULT WINAPI clipper_GetHWnd(IDirectDrawClipper *iface, HWND *out)
{
    DDClipper *clipper = clipper_from_iface(iface);

    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = clipper->window;
    return DD_OK;
}

static HRESULT WINAPI clipper_Initialize(IDirectDrawClipper *iface,
        LPDIRECTDRAW dd, DWORD flags)
{
    (void)iface; (void)dd; (void)flags;
    return DDWG_TRACE_ERROR(DDERR_ALREADYINITIALIZED);
}

static HRESULT WINAPI clipper_IsClipListChanged(IDirectDrawClipper *iface,
        WINBOOL *changed)
{
    DDClipper *clipper = clipper_from_iface(iface);

    if (!changed) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *changed = clipper->changed;
    clipper->changed = FALSE;
    return DD_OK;
}

static HRESULT WINAPI clipper_SetClipList(IDirectDrawClipper *iface,
        LPRGNDATA list, DWORD flags)
{
    DDClipper *clipper = clipper_from_iface(iface);
    DWORD bytes;

    (void)flags;
    heap_free(clipper->clip_list);
    clipper->clip_list = NULL;
    clipper->clip_list_bytes = 0;
    clipper->changed = TRUE;
    if (!list) return DD_OK;
    if (clipper->window) return DDWG_TRACE_ERROR(DDERR_CLIPPERISUSINGHWND);
    bytes = (DWORD)(sizeof(RGNDATAHEADER) + list->rdh.nRgnSize);
    clipper->clip_list = (RGNDATA *)heap_alloc_zero(bytes);
    if (!clipper->clip_list) return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY);
    CopyMemory(clipper->clip_list, list, bytes);
    clipper->clip_list_bytes = bytes;
    return DD_OK;
}

static HRESULT WINAPI clipper_SetHWnd(IDirectDrawClipper *iface, DWORD flags,
        HWND window)
{
    DDClipper *clipper = clipper_from_iface(iface);

    (void)flags;
    heap_free(clipper->clip_list);
    clipper->clip_list = NULL;
    clipper->clip_list_bytes = 0;
    clipper->window = window;
    clipper->changed = TRUE;
    return DD_OK;
}

/* ------------------------------------------------------------------ *
 * Surface creation and teardown
 * ------------------------------------------------------------------ */

static void surface_storage_release(DDTextureStorage *storage)
{
    D9WGDestroyResource destroy;

    if (!storage || InterlockedDecrement(&storage->ref) != 0) return;
    ZeroMemory(&destroy, sizeof(destroy));
    if (storage->created) {
        destroy.resource_handle = storage->handle;
        destroy.resource_kind = storage->resource_kind;
        emit_command(D9WG_OP_DESTROY_RESOURCE, &destroy, sizeof(destroy));
    }
    heap_free(storage);
}

static DDTextureStorage *surface_storage_allocate(void)
{
    DDTextureStorage *storage = (DDTextureStorage *)heap_alloc_zero(
            sizeof(*storage));
    if (!storage) return NULL;
    storage->ref = 1;
    storage->handle = allocate_handle();
    storage->resource_kind = D9WG_RESOURCE_TEXTURE_2D;
    return storage;
}

static void surface_destroy(DDSurface *surface)
{
    DDrawObject *owner = surface->owner;
    UINT face;
    BOOL was_primary = FALSE;

    if ((surface->desc.ddsCaps.dwCaps & DDSCAPS_OVERLAY) &&
            surface->overlay_destination) {
        surface->overlay_visible = FALSE;
        emit_overlay_state(surface);
    }

    if (surface->dc) {
        SelectObject(surface->dc, surface->old_bitmap);
        DeleteObject(surface->dib);
        DeleteDC(surface->dc);
    }
    if (surface->mip_next) {
        DDSurface *mip = surface->mip_next;
        surface->mip_next = NULL;
        surface_Release((IDirectDrawSurface7 *)&mip->vtbl7);
    }
    for (face = 1u; face < 6u; ++face) {
        if (surface->cube_faces[face]) {
            DDSurface *attached = surface->cube_faces[face];
            surface->cube_faces[face] = NULL;
            surface_Release((IDirectDrawSurface7 *)&attached->vtbl7);
        }
    }
    if (surface->attached_depth) {
        DDSurface *depth = surface->attached_depth;
        surface->attached_depth = NULL;
        surface_Release((IDirectDrawSurface7 *)&depth->vtbl7);
    }
    if (surface->overlay_destination) {
        DDSurface *destination = surface->overlay_destination;
        surface->overlay_destination = NULL;
        surface_Release((IDirectDrawSurface7 *)&destination->vtbl7);
    }
    surface_storage_release(surface->storage);
    if (surface->palette)
        IDirectDrawPalette_Release((IDirectDrawPalette *)surface->palette);
    if (surface->clipper)
        IDirectDrawClipper_Release((IDirectDrawClipper *)surface->clipper);
    EnterCriticalSection(&g_object_lock);
    if (owner) {
        DDSurface **link = &owner->surfaces;
        while (*link && *link != surface) link = &(*link)->next;
        if (*link) *link = surface->next;
        if (owner->primary == surface) {
            owner->primary = NULL;
            was_primary = TRUE;
        }
    }
    LeaveCriticalSection(&g_object_lock);
    /* Outside the object lock: emitting takes the transport lock, and the two
     * are never held in the other order anywhere else. */
    if (was_primary) emit_window_state_not_showing(owner);
    surface_memory_release(surface->memory);
    heap_free(surface);
}

/*
 * Tell the host this device has stopped putting anything on screen.
 *
 * The overlay canvas is *shown* by a present, and only two things ever take
 * it away again: destroying the device, and a window-state report saying the
 * window is not showing. An app that releases its primary surface while
 * keeping the DirectDraw object alive hits neither -- 3DMark 2000 does
 * exactly that when its splash screen closes -- so the last frame it
 * presented stayed composited over the guest's own screen for the rest of the
 * process, hiding the GDI window underneath it.
 *
 * The report carries the window's real geometry with D9WG_WINDOW_VISIBLE
 * cleared. That bit means "what this device presents is on screen", which is
 * precisely what stops being true when the primary goes away. The window
 * itself may well still be up, and is described as it is.
 */
static void emit_window_state_not_showing(DDrawObject *object)
{
    D9WGWindowState command;
    HWND window;
    HWND foreground;
    RECT window_rect;
    RECT client;
    uint32_t flags = 0;

    if (!object || !object->device_created || !object->device_handle)
        return;
    window = object->window;
    foreground = GetForegroundWindow();
    SetRect(&window_rect, 0, 0, 0, 0);
    SetRect(&client, 0, 0, 0, 0);
    if (window && IsWindow(window)) {
        flags |= D9WG_WINDOW_IS_WINDOW;
        if (IsIconic(window)) flags |= D9WG_WINDOW_ICONIC;
        if (window == foreground) flags |= D9WG_WINDOW_FOREGROUND;
        GetWindowRect(window, &window_rect);
        GetClientRect(window, &client);
    }
    if (object->cooperative_flags & DDSCL_FULLSCREEN)
        flags |= D9WG_WINDOW_FULLSCREEN;
    /* Says which claim this is: nothing to present, rather than a window that
     * has gone away. Without it the host reads a cleared VISIBLE bit as a
     * minimised window and warns that input is going elsewhere -- for a title
     * that has simply finished drawing and whose window is fine. */
    flags |= D9WG_WINDOW_NO_SURFACE;
    ZeroMemory(&command, sizeof(command));
    command.device_handle = object->device_handle;
    command.hwnd = (uint32_t)(uintptr_t)window;
    command.foreground_hwnd = (uint32_t)(uintptr_t)foreground;
    command.flags = flags;
    command.window_x = window_rect.left;
    command.window_y = window_rect.top;
    command.window_width = (uint32_t)(window_rect.right - window_rect.left);
    command.window_height = (uint32_t)(window_rect.bottom - window_rect.top);
    command.client_width = (uint32_t)(client.right - client.left);
    command.client_height = (uint32_t)(client.bottom - client.top);
    DDWG_TRACE_CHECKPOINT("WINDOW_STATE not_showing device=%lu hwnd=%08lX "
            "flags=%08lX", command.device_handle, command.hwnd, flags);
    if (!emit_command(D9WG_OP_WINDOW_STATE, &command, sizeof(command)))
        return;
    /* Flushed rather than left in the batch: the app has stopped presenting,
     * so there may be no next submission until the process exits, and a
     * hide that arrives then is no hide at all. */
    EnterCriticalSection(&g_transport_lock);
    submit_batch_locked(FALSE);
    LeaveCriticalSection(&g_transport_lock);
}

static DDSurface *surface_allocate(DDrawObject *object,
        const DDSURFACEDESC2 *request, unsigned format, UINT width,
        UINT height, DWORD caps, DDTextureStorage *shared_storage,
        UINT mip_level, UINT cube_face)
{
    DDSurface *surface;
    UINT bytes_per_pixel = format_bytes_per_pixel(format);
    UINT block_bytes = format_compressed_block_bytes(format);
    UINT pitch;
    UINT shadow_rows;
    SIZE_T shadow_bytes;

    if ((!bytes_per_pixel && !block_bytes) || !width || !height)
        return NULL;
    if (block_bytes) {
        pitch = ((width + 3u) / 4u) * block_bytes;
        shadow_rows = (height + 3u) / 4u;
    } else {
        pitch = surface_pitch_for(width, bytes_per_pixel);
        shadow_rows = height;
    }
    shadow_bytes = (SIZE_T)pitch * shadow_rows;
    if (pitch && shadow_bytes / pitch != shadow_rows) return NULL;

    surface = (DDSurface *)heap_alloc_zero(sizeof(*surface));
    if (!surface) return NULL;
    surface->memory = surface_memory_create(NULL, shadow_bytes, TRUE);
    if (!surface->memory) { heap_free(surface); return NULL; }
    surface->shadow = surface->memory->bits;

    /* Every version's vtable, because QueryInterface may hand out any of them
     * and the app calls straight through whichever it holds. */
    surface->vtbl7 = &g_surface_vtbl;
    surface->vtbl4 = &g_surface_vtbl4;
    surface->vtbl3 = &g_surface_vtbl3;
    surface->vtbl2 = &g_surface_vtbl2;
    surface->vtbl1 = &g_surface_vtbl1;
    surface->texture2_vtbl = &g_d3d_texture2_vtbl;
    surface->texture1_vtbl = &g_d3d_texture1_vtbl;
    surface->gamma_vtbl = &g_gamma_vtbl;
    surface->ref = 1;
    surface->owner = object;
    {
        /* The identity ramp, which is what a surface reports before anyone
         * sets one: 8-bit level i widened to 16 bits is i * 257. */
        UINT entry;
        for (entry = 0; entry < 256u; ++entry) {
            WORD value = (WORD)(entry * 257u);
            surface->gamma_ramp.red[entry] = value;
            surface->gamma_ramp.green[entry] = value;
            surface->gamma_ramp.blue[entry] = value;
        }
    }
    if (shared_storage) {
        InterlockedIncrement(&shared_storage->ref);
        surface->storage = shared_storage;
    } else {
        surface->storage = surface_storage_allocate();
        if (!surface->storage) {
            surface_memory_release(surface->memory);
            heap_free(surface);
            return NULL;
        }
    }
    surface->handle = surface->storage->handle;
    surface->overlay_id = allocate_handle();
    surface->mip_level = mip_level;
    surface->cube_face = cube_face;
    surface->format = format;
    surface->indexed = ddraw_format_is_palettised(format);
    surface->width = width;
    surface->height = height;
    surface->bytes_per_pixel = bytes_per_pixel;
    surface->pitch = pitch;
    surface->primary = (caps & DDSCAPS_PRIMARYSURFACE) != 0;
    surface->implicit = shared_storage != NULL;

    surface->desc = *request;
    surface->desc.dwSize = sizeof(DDSURFACEDESC2);
    surface->desc.dwFlags |= DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT |
        DDSD_CAPS;
    surface->desc.dwWidth = width;
    surface->desc.dwHeight = height;
    if (block_bytes) {
        surface->desc.dwFlags &= ~DDSD_PITCH;
        surface->desc.dwFlags |= DDSD_LINEARSIZE;
        surface->desc.dwLinearSize = (DWORD)shadow_bytes;
    } else {
        surface->desc.dwFlags |= DDSD_PITCH;
        surface->desc.lPitch = (LONG)pitch;
    }
    surface->desc.ddsCaps.dwCaps = caps;
    if (!(request->dwFlags & DDSD_PIXELFORMAT))
        mode_pixel_format(bytes_per_pixel * 8u,
                &surface->desc.ddpfPixelFormat);
    surface->desc.ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);

    if (request->dwFlags & DDSD_CKSRCBLT) {
        surface->color_key[D9WG_DDCKEY_SOURCE_BLT] = request->ddckCKSrcBlt;
        surface->color_key_present |= 1u << D9WG_DDCKEY_SOURCE_BLT;
    }
    if (request->dwFlags & DDSD_CKDESTBLT) {
        surface->color_key[D9WG_DDCKEY_DESTINATION_BLT] =
            request->ddckCKDestBlt;
        surface->color_key_present |= 1u << D9WG_DDCKEY_DESTINATION_BLT;
    }
    if (request->dwFlags & DDSD_CKSRCOVERLAY) {
        surface->color_key[D9WG_DDCKEY_SOURCE_OVERLAY] =
            request->ddckCKSrcOverlay;
        surface->color_key_present |= 1u << D9WG_DDCKEY_SOURCE_OVERLAY;
    }
    if (request->dwFlags & DDSD_CKDESTOVERLAY) {
        surface->color_key[D9WG_DDCKEY_DESTINATION_OVERLAY] =
            request->ddckCKDestOverlay;
        surface->color_key_present |=
            1u << D9WG_DDCKEY_DESTINATION_OVERLAY;
    }

    EnterCriticalSection(&g_object_lock);
    surface->next = object->surfaces;
    object->surfaces = surface;
    LeaveCriticalSection(&g_object_lock);
    return surface;
}

/*
 * A DirectDraw surface is guest memory with a host texture cached behind it,
 * and only the first half can fail for a reason the application caused. When
 * the bridge is not open the surface is still a valid surface -- Lock, Blt and
 * GetDC all work against the shadow -- so it is created anyway and owes the
 * host a texture, which ensure_surface_storage builds on first use. Failing
 * here instead is what turned "v86gl.sys is not started" into
 * DDERR_OUTOFVIDEOMEMORY out of CreateSurface, which is neither true nor
 * something the caller can act on.
 */
static DDSurface *surface_create(DDrawObject *object,
        const DDSURFACEDESC2 *request, unsigned format, UINT width,
        UINT height, DWORD caps)
{
    DDSurface *surface = surface_allocate(object, request, format, width,
            height, caps, NULL, 0u, 0u);
    if (!surface) return NULL;
    /* ensure_surface_storage restates the colour keys and the palette binding
     * as part of building the texture, whenever that turns out to be. */
    if (!emit_surface_create(surface))
        surface->upload_pending = TRUE;
    return surface;
}

static UINT surface_full_mip_count(UINT width, UINT height)
{
    UINT levels = 1u;
    while (width > 1u || height > 1u) {
        if (width > 1u) width >>= 1;
        if (height > 1u) height >>= 1;
        ++levels;
    }
    return levels;
}

static BOOL surface_build_mip_chain(DDSurface *top,
        const DDSURFACEDESC2 *request, unsigned format, UINT level_count,
        DWORD caps, DWORD cube_caps)
{
    DDSurface *parent = top;
    UINT level;
    for (level = 1u; level < level_count; ++level) {
        DDSURFACEDESC2 seed = *request;
        UINT width = top->width >> level;
        UINT height = top->height >> level;
        DWORD child_caps = (caps | DDSCAPS_TEXTURE | DDSCAPS_MIPMAP) &
                ~(DDSCAPS_PRIMARYSURFACE | DDSCAPS_FRONTBUFFER |
                  DDSCAPS_BACKBUFFER | DDSCAPS_FLIP | DDSCAPS_VISIBLE);
        DDSurface *child;
        if (!width) width = 1u;
        if (!height) height = 1u;
        if (level + 1u == level_count) child_caps &= ~DDSCAPS_COMPLEX;
        else child_caps |= DDSCAPS_COMPLEX;
        seed.dwFlags |= DDSD_WIDTH | DDSD_HEIGHT | DDSD_MIPMAPCOUNT |
                DDSD_PIXELFORMAT | DDSD_CAPS;
        seed.dwFlags &= ~DDSD_BACKBUFFERCOUNT;
        seed.dwWidth = width;
        seed.dwHeight = height;
        seed.dwMipMapCount = level_count - level;
        seed.ddsCaps.dwCaps = child_caps;
        seed.ddsCaps.dwCaps2 = cube_caps;
        child = surface_allocate(top->owner, &seed, format, width, height,
                child_caps, top->storage, level, top->cube_face);
        if (!child) return FALSE;
        parent->mip_next = child; /* the child's initial ref is this edge */
        parent = child;
    }
    return TRUE;
}

static DDSurface *surface_create_complex_texture(DDrawObject *object,
        const DDSURFACEDESC2 *request, unsigned format, UINT width,
        UINT height, DWORD caps, UINT level_count, BOOL cube)
{
    static const DWORD face_caps[6] = {
        DDSCAPS2_CUBEMAP_POSITIVEX, DDSCAPS2_CUBEMAP_NEGATIVEX,
        DDSCAPS2_CUBEMAP_POSITIVEY, DDSCAPS2_CUBEMAP_NEGATIVEY,
        DDSCAPS2_CUBEMAP_POSITIVEZ, DDSCAPS2_CUBEMAP_NEGATIVEZ,
    };
    DDSURFACEDESC2 root_seed = *request;
    DDSurface *root;
    UINT face;

    caps |= DDSCAPS_TEXTURE;
    if (level_count > 1u) caps |= DDSCAPS_MIPMAP | DDSCAPS_COMPLEX;
    if (cube) caps |= DDSCAPS_COMPLEX;
    root_seed.dwFlags |= DDSD_PIXELFORMAT | DDSD_CAPS;
    if (level_count > 1u) root_seed.dwFlags |= DDSD_MIPMAPCOUNT;
    root_seed.dwMipMapCount = level_count;
    root_seed.ddsCaps.dwCaps = caps;
    root = surface_allocate(object, &root_seed, format, width, height, caps,
            NULL, 0u, 0u);
    if (!root) return NULL;
    if (cube) root->cube_faces[0] = root;
    /* As in surface_create: a missing bridge leaves the texture owed, not the
     * surface refused. surface_build_mip_chain and the faces below share this
     * storage, so ensure_surface_storage builds it for all of them at once. */
    if (!emit_surface_storage_create(root, level_count, cube))
        root->upload_pending = TRUE;
    if (!surface_build_mip_chain(root, &root_seed, format, level_count, caps,
            cube ? DDSCAPS2_CUBEMAP | face_caps[0] : 0u)) {
        surface_Release((IDirectDrawSurface7 *)&root->vtbl7);
        return NULL;
    }
    if (!cube) return root;

    for (face = 1u; face < 6u; ++face) {
        DDSURFACEDESC2 seed = root_seed;
        DDSurface *face_surface;
        seed.ddsCaps.dwCaps2 = DDSCAPS2_CUBEMAP | face_caps[face];
        face_surface = surface_allocate(object, &seed, format, width, height,
                caps, root->storage, 0u, face);
        if (!face_surface || !surface_build_mip_chain(face_surface, &seed,
                format, level_count, caps, seed.ddsCaps.dwCaps2)) {
            if (face_surface)
                surface_Release((IDirectDrawSurface7 *)&face_surface->vtbl7);
            surface_Release((IDirectDrawSurface7 *)&root->vtbl7);
            return NULL;
        }
        root->cube_faces[face] = face_surface;
    }
    return root;
}

/* Direct3D 7 is kept in a separate include so the already-large DirectDraw
 * implementation remains navigable. It deliberately shares the private
 * object and transport helpers above rather than defining a second backend. */
#include "d3d7_proxy.inc"
#include "d3d_legacy_proxy.inc"

/* ------------------------------------------------------------------ *
 * IDirectDraw7
 * ------------------------------------------------------------------ */

static HRESULT WINAPI ddraw_QueryInterface(IDirectDraw7 *iface, REFIID iid,
        void **out)
{
    DDrawObject *object = object_from_any(iface, NULL);
    unsigned version = 0;

    DDWG_TRACE_CHECKPOINT("CALL ddraw_QueryInterface object=%08lX iid=%08lX",
            (DWORD)(uintptr_t)object, iid ? iid->Data1 : 0u);
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = NULL;
    if (guid_equal(iid, &IID_IDirect3D7)) {
        ddraw_AddRef((IDirectDraw7 *)&object->vtbl7);
        *out = &object->d3d7_vtbl;
        return DDWG_TRACE_RESULT(D3D_OK);
    }
    if (guid_equal(iid, &IID_IDirect3D3)) {
        ddraw_AddRef((IDirectDraw7 *)&object->vtbl7);
        *out = &object->d3d3_vtbl;
        return DDWG_TRACE_RESULT(D3D_OK);
    }
    if (guid_equal(iid, &IID_IDirect3D2)) {
        ddraw_AddRef((IDirectDraw7 *)&object->vtbl7);
        *out = &object->d3d2_vtbl;
        return DDWG_TRACE_RESULT(D3D_OK);
    }
    if (guid_equal(iid, &IID_IDirect3D)) {
        ddraw_AddRef((IDirectDraw7 *)&object->vtbl7);
        *out = &object->d3d1_vtbl;
        return DDWG_TRACE_RESULT(D3D_OK);
    }
    if (iid_is_unknown(iid) || guid_equal(iid, &IID_IDirectDraw7))
        version = DD_VERSION_7;
    else if (guid_equal(iid, &IID_IDirectDraw4))
        version = DD_VERSION_4;
    else if (guid_equal(iid, &IID_IDirectDraw3))
        version = DD_VERSION_3;
    else if (guid_equal(iid, &IID_IDirectDraw2))
        version = DD_VERSION_2;
    else if (guid_equal(iid, &IID_IDirectDraw))
        version = DD_VERSION_1;
    if (!version) return DDWG_TRACE_ERROR(E_NOINTERFACE);
    ddraw_AddRef((IDirectDraw7 *)&object->vtbl7);
    *out = object_interface(object, version);
    DDWG_TRACE("DDRAW QUERY_INTERFACE object=%08lX version=%u out=%08lX",
            (DWORD)(uintptr_t)object, version, (DWORD)(uintptr_t)*out);
    return DDWG_TRACE_RESULT(DD_OK);
}

static ULONG WINAPI ddraw_AddRef(IDirectDraw7 *iface)
{
    return (ULONG)InterlockedIncrement(&object_from_any(iface, NULL)->ref);
}

static ULONG WINAPI ddraw_Release(IDirectDraw7 *iface)
{
    DDrawObject *object = object_from_any(iface, NULL);
    LONG remaining = InterlockedDecrement(&object->ref);

    if (remaining == 0) {
        DDWG_TRACE_CHECKPOINT("DIRECTDRAW DESTROY object=%08lX device=%lu "
                "mode_changed=%lu", (DWORD)(uintptr_t)object,
                object->device_handle,
                object->guest_mode_changed ? 1u : 0u);
        EnterCriticalSection(&g_object_lock);
        {
            DDrawObject **link = &g_objects;
            while (*link && *link != object) link = &(*link)->next;
            if (*link) *link = object->next;
        }
        LeaveCriticalSection(&g_object_lock);
        if (object->guest_mode_changed)
            ChangeDisplaySettingsA(NULL, 0);
        emit_device_destroy(object);
        EnterCriticalSection(&g_transport_lock);
        submit_batch_locked(FALSE);
        LeaveCriticalSection(&g_transport_lock);
        InterlockedDecrement(&g_object_count);
        heap_free(object);
    }
    return (ULONG)(remaining < 0 ? 0 : remaining);
}

static HRESULT WINAPI ddraw_Compact(IDirectDraw7 *iface)
{
    (void)iface;
    /* Nothing to defragment: surfaces are host textures and heap blocks. */
    return DD_OK;
}

static HRESULT WINAPI ddraw_CreateClipper(IDirectDraw7 *iface, DWORD flags,
        LPDIRECTDRAWCLIPPER *out, IUnknown *outer)
{
    DDrawObject *object = object_from_iface(iface);
    DDClipper *clipper;

    (void)flags;
    if (outer) return DDWG_TRACE_ERROR(CLASS_E_NOAGGREGATION);
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    clipper = (DDClipper *)heap_alloc_zero(sizeof(*clipper));
    if (!clipper) return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY);
    clipper->vtbl = &g_clipper_vtbl;
    clipper->ref = 1;
    clipper->owner = object;
    EnterCriticalSection(&g_object_lock);
    clipper->next = object->clippers;
    object->clippers = clipper;
    LeaveCriticalSection(&g_object_lock);
    *out = (IDirectDrawClipper *)clipper;
    return DD_OK;
}

static HRESULT WINAPI ddraw_CreatePalette(IDirectDraw7 *iface, DWORD flags,
        LPPALETTEENTRY table, LPDIRECTDRAWPALETTE *out, IUnknown *outer)
{
    DDrawObject *object = object_from_iface(iface);
    DDPalette *palette;
    UINT index;

    if (outer) return DDWG_TRACE_ERROR(CLASS_E_NOAGGREGATION);
    if (!out || !table) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (!(flags & DDPCAPS_8BIT)) {
        /* 1/2/4-bit palettes have no indexed storage on the host and no user
         * in the target library. */
        return UNSUPPORTED("CreatePalette for a palette narrower than 8 bits",
                DDERR_UNSUPPORTED);
    }
    palette = (DDPalette *)heap_alloc_zero(sizeof(*palette));
    if (!palette) return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY);
    palette->vtbl = &g_palette_vtbl;
    palette->ref = 1;
    palette->owner = object;
    palette->caps = flags;
    EnterCriticalSection(&g_object_lock);
    palette->slot = object->next_palette_slot++;
    palette->next = object->palettes;
    object->palettes = palette;
    LeaveCriticalSection(&g_object_lock);
    for (index = 0; index < 256u; ++index)
        palette->entries[index] = table[index];
    emit_palette_table(palette);
    *out = (IDirectDrawPalette *)palette;
    return DD_OK;
}

static HRESULT WINAPI ddraw_CreateSurface(IDirectDraw7 *iface,
        LPDDSURFACEDESC2 request, LPDIRECTDRAWSURFACE7 *out, IUnknown *outer)
{
    DDrawObject *object = object_from_iface(iface);
    DDSurface *surface;
    DDrawPixelFormatDesc format_desc;
    DDPIXELFORMAT pixel_format;
    unsigned format;
    UINT width;
    UINT height;
    DWORD caps;
    DWORD back_buffers;
    UINT mip_levels = 1u;
    BOOL cube;

    DDWG_TRACE_CHECKPOINT("CALL ddraw_CreateSurface object=%08lX request=%08lX "
            "outer=%08lX", (DWORD)(uintptr_t)object,
            (DWORD)(uintptr_t)request, (DWORD)(uintptr_t)outer);
    if (outer) return DDWG_TRACE_ERROR(CLASS_E_NOAGGREGATION);
    if (!out || !request) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (request->dwSize != sizeof(DDSURFACEDESC2)) {
        /* The v1-v3 description is a different size, and guessing which one
         * arrived is how a surface ends up with someone else's caps. */
        HOSTLOG_REFUSED("CreateSurface was given a %lu-byte description; only "
                "the DDSURFACEDESC2 form is implemented", request->dwSize);
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    }
    *out = NULL;
    caps = request->ddsCaps.dwCaps;
    back_buffers = (request->dwFlags & DDSD_BACKBUFFERCOUNT)
        ? request->dwBackBufferCount : 0;
    DDWG_TRACE_CHECKPOINT("SURFACE REQUEST flags=%08lX caps=%08lX caps2=%08lX "
            "size=%lux%lu backbuffers=%lu pf_flags=%08lX pf_bits=%lu",
            request->dwFlags, caps, request->ddsCaps.dwCaps2,
            request->dwWidth, request->dwHeight, back_buffers,
            request->ddpfPixelFormat.dwFlags,
            request->ddpfPixelFormat.dwRGBBitCount);

    if (caps & DDSCAPS_PRIMARYSURFACE) {
        object_display_size(object, &width, &height);
        mode_pixel_format(object->mode_bpp, &pixel_format);
    } else {
        if (!(request->dwFlags & DDSD_WIDTH) ||
                !(request->dwFlags & DDSD_HEIGHT))
            return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
        width = request->dwWidth;
        height = request->dwHeight;
        /* DirectDraw refuses a zero-sized surface. Accepting one produced a
         * host texture clamped to 1x1 that no call ever complained about,
         * until something blitted from it. */
        if (!width || !height) {
            HOSTLOG_REFUSED("CreateSurface asked for a %lux%lu surface; "
                    "DirectDraw answers a zero dimension with "
                    "DDERR_INVALIDPARAMS", width, height);
            return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
        }
        if (request->dwFlags & DDSD_PIXELFORMAT)
            pixel_format = request->ddpfPixelFormat;
        else
            mode_pixel_format(object->mode_bpp ? object->mode_bpp : 32,
                    &pixel_format);
    }

    if (caps & DDSCAPS_VIDEOPORT) {
        return UNSUPPORTED("CreateSurface for a DirectDraw video port",
                DDERR_UNSUPPORTED);
    }
    cube = (request->ddsCaps.dwCaps2 & DDSCAPS2_CUBEMAP) != 0;
    if (cube && width != height) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if ((caps & DDSCAPS_MIPMAP) || (request->dwFlags & DDSD_MIPMAPCOUNT)) {
        UINT full_levels = surface_full_mip_count(width, height);
        mip_levels = (request->dwFlags & DDSD_MIPMAPCOUNT)
            ? request->dwMipMapCount : full_levels;
        if (!mip_levels || mip_levels > full_levels)
            return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    }
    if ((cube || mip_levels > 1u) && back_buffers)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);

    describe_pixel_format(&pixel_format, &format_desc);
    format = ddraw_pixel_format_to_d3dformat(&format_desc);
    if (format == DDWG_FMT_UNKNOWN) {
        HOSTLOG_REFUSED("CreateSurface asked for a pixel format outside the "
                "supported matrix (flags=%08lX bits=%lu r=%08lX g=%08lX "
                "b=%08lX)", pixel_format.dwFlags, pixel_format.dwRGBBitCount,
                pixel_format.dwRBitMask, pixel_format.dwGBitMask,
                pixel_format.dwBBitMask);
        return DDWG_TRACE_ERROR(DDERR_INVALIDPIXELFORMAT);
    }

    {
        DDSURFACEDESC2 seed = *request;
        seed.ddpfPixelFormat = pixel_format;
        seed.dwFlags |= DDSD_PIXELFORMAT;
        if (cube || mip_levels > 1u)
            surface = surface_create_complex_texture(object, &seed, format,
                    width, height, caps, mip_levels, cube);
        else
            surface = surface_create(object, &seed, format, width, height,
                    caps);
    }
    if (!surface) return DDWG_TRACE_ERROR(DDERR_OUTOFVIDEOMEMORY);

    if (caps & DDSCAPS_PRIMARYSURFACE) {
        object->primary = surface;
        surface->front_of_chain = back_buffers > 0;
        if (!back_buffers)
            (void)seed_windowed_primary_from_screen(surface);
    }

    /*
     * A flip chain is created as one complex surface: the primary names how
     * many back buffers it wants, and they are attached rather than created
     * separately. They are linked into a ring here because Flip rotates the
     * storage around it.
     */
    if (back_buffers) {
        DDSurface *previous = surface;
        DWORD index;
        for (index = 0; index < back_buffers; ++index) {
            DDSURFACEDESC2 seed = *request;
            DWORD buffer_caps = (caps & ~(DDSCAPS_PRIMARYSURFACE |
                    DDSCAPS_VISIBLE)) | DDSCAPS_BACKBUFFER | DDSCAPS_FLIP;
            DDSurface *back;
            seed.ddpfPixelFormat = pixel_format;
            seed.dwFlags |= DDSD_PIXELFORMAT;
            seed.dwFlags &= ~DDSD_BACKBUFFERCOUNT;
            back = surface_create(object, &seed, format, width, height,
                    buffer_caps);
            if (!back) break;
            back->implicit = TRUE;
            previous->flip_next = back;
            previous = back;
        }
        previous->flip_next = surface;
        surface->desc.ddsCaps.dwCaps |= DDSCAPS_FLIP;
    }

    *out = (IDirectDrawSurface7 *)surface;
    DDWG_TRACE("SURFACE CREATED handle=%lu object=%08lX size=%lux%lu "
            "pitch=%lu format=%u caps=%08lX primary=%lu backbuffers=%lu",
            surface->handle, (DWORD)(uintptr_t)surface, surface->width,
            surface->height, surface->pitch, surface->format, caps,
            surface->primary ? 1u : 0u, back_buffers);
    return DDWG_TRACE_RESULT(DD_OK);
}

static HRESULT WINAPI ddraw_DuplicateSurface(IDirectDraw7 *iface,
        LPDIRECTDRAWSURFACE7 source, LPDIRECTDRAWSURFACE7 *out)
{
    DDrawObject *object = object_from_iface(iface);
    DDSurface *original;
    DDSurface *duplicate;
    DWORD caps;

    if (!out || !source) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = NULL;
    original = surface_from_iface(source);
    if (!original || original->owner != object) return DDWG_TRACE_ERROR(DDERR_INVALIDOBJECT);
    caps = original->desc.ddsCaps.dwCaps;
    /* DirectDraw explicitly excludes primary, 3-D, and implicitly-created
     * members of a complex surface.  An explicit texture/offscreen/overlay
     * object is valid: only this subresource's pixels are aliased. */
    if ((caps & (DDSCAPS_PRIMARYSURFACE | DDSCAPS_3DDEVICE)) ||
            original->implicit)
        return DDWG_TRACE_ERROR(DDERR_CANTDUPLICATE);
    if (original->lock_depth || original->dc) return DDWG_TRACE_ERROR(DDERR_SURFACEBUSY);

    duplicate = surface_allocate(object, &original->desc, original->format,
            original->width, original->height, caps, original->storage,
            original->mip_level, original->cube_face);
    if (!duplicate) return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY);

    /* surface_allocate made a private zeroed shadow because that is the safe
     * default for mip/cube children. Replace it with the one DuplicateSurface
     * promises to alias. */
    surface_memory_release(duplicate->memory);
    duplicate->memory = original->memory;
    surface_memory_addref(duplicate->memory);
    duplicate->shadow = duplicate->memory->bits;
    duplicate->external_memory = original->external_memory;
    duplicate->implicit = FALSE;
    duplicate->desc.lpSurface = original->desc.lpSurface;
    duplicate->color_key_present = original->color_key_present;
    CopyMemory(duplicate->color_key, original->color_key,
            sizeof(duplicate->color_key));
    duplicate->texture_handle = original->texture_handle;
    if (original->palette) {
        duplicate->palette = original->palette;
        IDirectDrawPalette_AddRef((IDirectDrawPalette *)duplicate->palette);
    }
    if (original->clipper) {
        duplicate->clipper = original->clipper;
        IDirectDrawClipper_AddRef((IDirectDrawClipper *)duplicate->clipper);
    }
    *out = (IDirectDrawSurface7 *)&duplicate->vtbl7;
    return DD_OK;
}

/* The modes this path can really present. Anything not listed is a mode the
 * page would have to letterbox or scale, and reporting one we cannot honour is
 * how a game ends up rendering into a rectangle nobody sees. */
static const struct { DWORD width; DWORD height; } g_modes[] = {
    { 320, 200 }, { 320, 240 }, { 512, 384 }, { 640, 400 }, { 640, 480 },
    { 800, 600 }, { 1024, 768 }, { 1152, 864 }, { 1280, 720 }, { 1280, 1024 },
};
static const DWORD g_mode_depths[] = { 8, 16, 32 };

static HRESULT WINAPI ddraw_EnumDisplayModes(IDirectDraw7 *iface, DWORD flags,
        LPDDSURFACEDESC2 filter, LPVOID context,
        LPDDENUMMODESCALLBACK2 callback)
{
    UINT mode;
    UINT depth;

    (void)iface;
    (void)flags;
    DDWG_TRACE_CHECKPOINT("CALL ddraw_EnumDisplayModes flags=%08lX "
            "filter=%08lX filter_flags=%08lX", flags,
            (DWORD)(uintptr_t)filter, filter ? filter->dwFlags : 0u);
    if (!callback) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    for (mode = 0; mode < sizeof(g_modes) / sizeof(g_modes[0]); ++mode) {
        for (depth = 0; depth < sizeof(g_mode_depths) / sizeof(DWORD);
                ++depth) {
            DDSURFACEDESC2 description;
            if (filter) {
                if ((filter->dwFlags & DDSD_WIDTH) &&
                        filter->dwWidth != g_modes[mode].width)
                    continue;
                if ((filter->dwFlags & DDSD_HEIGHT) &&
                        filter->dwHeight != g_modes[mode].height)
                    continue;
            }
            ZeroMemory(&description, sizeof(description));
            description.dwSize = sizeof(description);
            description.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT |
                DDSD_PITCH | DDSD_REFRESHRATE;
            description.dwWidth = g_modes[mode].width;
            description.dwHeight = g_modes[mode].height;
            description.dwRefreshRate = 60;
            mode_pixel_format(g_mode_depths[depth],
                    &description.ddpfPixelFormat);
            description.lPitch = (LONG)surface_pitch_for(g_modes[mode].width,
                    g_mode_depths[depth] / 8u);
            if (callback(&description, context) == DDENUMRET_CANCEL)
                return DDWG_TRACE_RESULT(DD_OK);
        }
    }
    return DDWG_TRACE_RESULT(DD_OK);
}

static HRESULT WINAPI ddraw_EnumSurfaces(IDirectDraw7 *iface, DWORD flags,
        LPDDSURFACEDESC2 filter, LPVOID context,
        LPDDENUMSURFACESCALLBACK7 callback)
{
    DDrawObject *object = object_from_iface(iface);
    DDSurface *surface;

    (void)flags; (void)filter;
    if (!callback) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    EnterCriticalSection(&g_object_lock);
    surface = object->surfaces;
    LeaveCriticalSection(&g_object_lock);
    while (surface) {
        DDSURFACEDESC2 description = surface->desc;
        DDSurface *next = surface->next;
        IDirectDrawSurface7_AddRef((IDirectDrawSurface7 *)surface);
        if (callback((IDirectDrawSurface7 *)surface, &description, context)
                == DDENUMRET_CANCEL)
            break;
        surface = next;
    }
    return DD_OK;
}

static HRESULT WINAPI ddraw_FlipToGDISurface(IDirectDraw7 *iface)
{
    (void)iface;
    /* There is no GDI surface behind the overlay to flip back to. */
    return DD_OK;
}

static void fill_caps(DDCAPS *caps)
{
    DWORD size = caps->dwSize;

    ZeroMemory(caps, sizeof(*caps));
    caps->dwSize = size;
    /*
     * Only bits with code behind them, named here with what implements them:
     *   BLT / BLTSTRETCH / BLTCOLORFILL  -> emit_blt + ddraw_ops.js onDDBlt
     *   COLORKEY + CKEYCAPS_SRCBLT       -> emit_color_key + the keyed variant
     *   PALETTE / PALETTEVSYNC           -> emit_palette_table, indexed storage
     *   CANBLTSYSMEM                     -> every surface has a shadow
     *   3D                               -> d3d7_proxy.inc, the whole of it
     * DDCAPS_3D is how a DirectX 7 title asks "is there a Direct3D device
     * behind this DirectDraw object at all"; the SDK's own enumeration sample
     * checks it before it will call EnumDevices, and every title descended
     * from that sample does the same.
     * Video ports and alpha overlays remain separate capabilities and are not
     * claimed; ordinary keyed/scaled/mirrored overlays are present-time GPU
     * composites and are advertised below.
     */
    caps->dwCaps = DDCAPS_3D | DDCAPS_BLT | DDCAPS_BLTSTRETCH |
        DDCAPS_BLTCOLORFILL | DDCAPS_BLTDEPTHFILL | DDCAPS_COLORKEY |
        DDCAPS_PALETTE | DDCAPS_CANBLTSYSMEM | DDCAPS_BLTQUEUE |
        DDCAPS_OVERLAY | DDCAPS_OVERLAYFOURCC | DDCAPS_OVERLAYSTRETCH;
    caps->dwCaps2 = DDCAPS2_WIDESURFACES | DDCAPS2_PRIMARYGAMMA;
    caps->dwCKeyCaps = DDCKEYCAPS_SRCBLT | DDCKEYCAPS_SRCBLTCLRSPACE |
        DDCKEYCAPS_DESTBLT | DDCKEYCAPS_DESTBLTCLRSPACE |
        DDCKEYCAPS_SRCOVERLAY | DDCKEYCAPS_SRCOVERLAYCLRSPACE |
        DDCKEYCAPS_DESTOVERLAY | DDCKEYCAPS_DESTOVERLAYCLRSPACE;
    caps->dwFXCaps = DDFXCAPS_BLTMIRRORLEFTRIGHT | DDFXCAPS_BLTMIRRORUPDOWN |
        DDFXCAPS_BLTSTRETCHX | DDFXCAPS_BLTSTRETCHY |
        DDFXCAPS_BLTSHRINKX | DDFXCAPS_BLTSHRINKY |
        DDFXCAPS_OVERLAYSHRINKX | DDFXCAPS_OVERLAYSHRINKY |
        DDFXCAPS_OVERLAYSTRETCHX | DDFXCAPS_OVERLAYSTRETCHY |
        DDFXCAPS_OVERLAYMIRRORLEFTRIGHT |
        DDFXCAPS_OVERLAYMIRRORUPDOWN;
    caps->dwPalCaps = DDPCAPS_8BIT | DDPCAPS_ALLOW256 | DDPCAPS_PRIMARYSURFACE;
    caps->dwSVCaps = 0;
    caps->dwZBufferBitDepths = DDBD_16 | DDBD_24 | DDBD_32;
    /* The host allocates GPU textures on demand; there is no fixed pool. This
     * number exists because titles refuse to start when it is zero. */
    caps->dwVidMemTotal = 64u * 1024u * 1024u;
    caps->dwVidMemFree = 48u * 1024u * 1024u;
    caps->dwMaxVisibleOverlays = 32;
    caps->dwCurrVisibleOverlays = 0;
    caps->dwMinOverlayStretch = 1;
    caps->dwMaxOverlayStretch = 16000;
    caps->dwNumFourCCCodes = 7;
    caps->ddsOldCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_PRIMARYSURFACE |
        DDSCAPS_FRONTBUFFER | DDSCAPS_BACKBUFFER | DDSCAPS_FLIP |
        DDSCAPS_COMPLEX | DDSCAPS_TEXTURE | DDSCAPS_MIPMAP |
        DDSCAPS_3DDEVICE | DDSCAPS_ZBUFFER | DDSCAPS_SYSTEMMEMORY |
        DDSCAPS_VIDEOMEMORY | DDSCAPS_OVERLAY;
    caps->ddsCaps.dwCaps = caps->ddsOldCaps.dwCaps;
    caps->ddsCaps.dwCaps2 = DDSCAPS2_CUBEMAP |
        DDSCAPS2_CUBEMAP_ALLFACES;
}

static HRESULT copy_caps_to_caller(LPDDCAPS destination, DWORD visible,
        WINBOOL emulation)
{
    DDCAPS caps;
    DWORD caller_size = destination->dwSize;
    DWORD copy_size;

    /* DDCAPS grew in DirectX 5 and 6.  The older layouts are prefixes of the
     * DX7 layout, and callers select one by putting its size in dwSize.  In
     * particular, XP dxdiag creates the version 1 DirectDraw interface and
     * may ask for an older layout.  Fill a full temporary structure and copy
     * only the part the caller provided so those probes do not fail or write
     * past their buffer. */
    if (caller_size < sizeof(destination->dwSize))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    caps.dwSize = sizeof(caps);
    fill_caps(&caps);
    caps.dwCurrVisibleOverlays = visible;
    if (emulation) caps.dwCaps = 0;

    copy_size = caller_size < sizeof(caps) ? caller_size : sizeof(caps);
    CopyMemory(destination, &caps, copy_size);
    destination->dwSize = caller_size;
    return DD_OK;
}

static HRESULT WINAPI ddraw_GetCaps(IDirectDraw7 *iface, LPDDCAPS driver,
        LPDDCAPS emulation)
{
    DDrawObject *object = object_from_iface(iface);
    DDSurface *surface;
    HRESULT result;
    DWORD visible = 0;
    DDWG_TRACE_CHECKPOINT("CALL ddraw_GetCaps driver=%08lX driver_size=%lu "
            "emulation=%08lX emulation_size=%lu", (DWORD)(uintptr_t)driver,
            driver ? driver->dwSize : 0u, (DWORD)(uintptr_t)emulation,
            emulation ? emulation->dwSize : 0u);
    for (surface = object->surfaces; surface; surface = surface->next)
        if (surface->overlay_visible) ++visible;
    if (!driver && !emulation) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (driver) {
        result = copy_caps_to_caller(driver, visible, FALSE);
        if (FAILED(result)) return DDWG_TRACE_ERROR(result);
    }
    if (emulation) {
        /* Everything here is "hardware": there is no software fallback layer
         * underneath, and claiming one would invite a title to ask for it. */
        result = copy_caps_to_caller(emulation, visible, TRUE);
        if (FAILED(result)) return DDWG_TRACE_ERROR(result);
    }
    return DDWG_TRACE_RESULT(DD_OK);
}

static HRESULT WINAPI ddraw_GetDisplayMode(IDirectDraw7 *iface,
        LPDDSURFACEDESC2 description)
{
    DDrawObject *object = object_from_iface(iface);
    UINT bpp = object->mode_bpp ? object->mode_bpp : 32u;

    DDWG_TRACE_CHECKPOINT("CALL ddraw_GetDisplayMode object=%08lX "
            "description=%08lX size=%lu", (DWORD)(uintptr_t)object,
            (DWORD)(uintptr_t)description,
            description ? description->dwSize : 0u);
    if (!description || description->dwSize != sizeof(DDSURFACEDESC2))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    ZeroMemory(description, sizeof(*description));
    description->dwSize = sizeof(*description);
    description->dwFlags = DDSD_WIDTH | DDSD_HEIGHT | DDSD_PITCH |
        DDSD_PIXELFORMAT | DDSD_REFRESHRATE;
    description->dwWidth = object->mode_width ? object->mode_width
        : (DWORD)GetSystemMetrics(SM_CXSCREEN);
    description->dwHeight = object->mode_height ? object->mode_height
        : (DWORD)GetSystemMetrics(SM_CYSCREEN);
    description->dwRefreshRate = object->mode_refresh ? object->mode_refresh
        : 60u;
    mode_pixel_format(bpp, &description->ddpfPixelFormat);
    description->lPitch = (LONG)surface_pitch_for(description->dwWidth,
            bpp / 8u);
    DDWG_TRACE("DISPLAY_MODE_CURRENT size=%lux%lux%lu refresh=%lu pitch=%ld",
            description->dwWidth, description->dwHeight, bpp,
            description->dwRefreshRate, description->lPitch);
    return DDWG_TRACE_RESULT(DD_OK);
}

static HRESULT WINAPI ddraw_GetFourCCCodes(IDirectDraw7 *iface, LPDWORD count,
        LPDWORD codes)
{
    static const DWORD supported[] = {
        DDWG_FMT_DXT1, DDWG_FMT_DXT2, DDWG_FMT_DXT3,
        DDWG_FMT_DXT4, DDWG_FMT_DXT5, DDWG_FMT_UYVY, DDWG_FMT_YUY2,
    };
    DWORD capacity;
    DWORD index;
    (void)iface;
    if (!count) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    capacity = *count;
    *count = sizeof(supported) / sizeof(supported[0]);
    if (!codes) return DD_OK;
    if (capacity > *count) capacity = *count;
    for (index = 0; index < capacity; ++index) codes[index] = supported[index];
    return DD_OK;
}

static HRESULT WINAPI ddraw_GetGDISurface(IDirectDraw7 *iface,
        LPDIRECTDRAWSURFACE7 *out)
{
    DDrawObject *object = object_from_iface(iface);

    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (!object->primary) return DDWG_TRACE_ERROR(DDERR_NOTFOUND);
    *out = (IDirectDrawSurface7 *)object->primary;
    IDirectDrawSurface7_AddRef(*out);
    return DD_OK;
}

static HRESULT WINAPI ddraw_GetMonitorFrequency(IDirectDraw7 *iface,
        LPDWORD frequency)
{
    DDrawObject *object = object_from_iface(iface);

    if (!frequency) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *frequency = object->mode_refresh ? object->mode_refresh : 60u;
    return DD_OK;
}

/*
 * The raster position and the vertical blank, derived from the clock rather
 * than from anything real: the browser presents when it presents, and the
 * guest cannot see that cadence. Titles use these two for pacing, so they have
 * to advance smoothly and cross the blank sixty times a second; what they must
 * not do is report a raster that never moves, which turns a wait loop into a
 * hang.
 */
static DWORD raster_position(const DDrawObject *object)
{
    DWORD height = object->mode_height ? object->mode_height : 480u;
    DWORD ticks = GetTickCount();
    DWORD frame_position = ticks % 17u;   /* ~60 Hz */
    return (frame_position * height) / 17u;
}

static HRESULT WINAPI ddraw_GetScanLine(IDirectDraw7 *iface, LPDWORD scanline)
{
    DDrawObject *object = object_from_iface(iface);

    if (!scanline) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *scanline = raster_position(object);
    ddwg_trace_poll(&g_trace_scanline_calls, "GetScanLine",
            (DWORD)(uintptr_t)object, *scanline, 0u);
    return DD_OK;
}

static HRESULT WINAPI ddraw_GetVerticalBlankStatus(IDirectDraw7 *iface,
        WINBOOL *in_blank)
{
    DDrawObject *object = object_from_iface(iface);

    if (!in_blank) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *in_blank = raster_position(object) >=
        (object->mode_height ? object->mode_height : 480u) - 8u;
    ddwg_trace_poll(&g_trace_vblank_status_calls, "GetVerticalBlankStatus",
            (DWORD)(uintptr_t)object, *in_blank ? 1u : 0u, 0u);
    return DD_OK;
}

static HRESULT WINAPI ddraw_Initialize(IDirectDraw7 *iface, GUID *guid)
{
    (void)iface; (void)guid;
    return DDWG_TRACE_ERROR(DDERR_ALREADYINITIALIZED);
}

static HRESULT WINAPI ddraw_RestoreDisplayMode(IDirectDraw7 *iface)
{
    DDrawObject *object = object_from_iface(iface);

    DDWG_TRACE_CHECKPOINT("CALL ddraw_RestoreDisplayMode object=%08lX "
            "guest_changed=%lu", (DWORD)(uintptr_t)object,
            object->guest_mode_changed ? 1u : 0u);
    if (object->guest_mode_changed) {
        ChangeDisplaySettingsA(NULL, 0);
        object->guest_mode_changed = FALSE;
    }
    object->mode_set = FALSE;
    emit_display_mode(object);
    return DDWG_TRACE_RESULT(DD_OK);
}

static HRESULT WINAPI ddraw_SetCooperativeLevel(IDirectDraw7 *iface,
        HWND window, DWORD flags)
{
    DDrawObject *object = object_from_iface(iface);
    UINT width;
    UINT height;

    DDWG_TRACE_CHECKPOINT("CALL ddraw_SetCooperativeLevel object=%08lX "
            "hwnd=%08lX flags=%08lX", (DWORD)(uintptr_t)object,
            (DWORD)(uintptr_t)window, flags);
    if ((flags & DDSCL_EXCLUSIVE) && !window)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    object->window = window;
    object->cooperative_flags = flags;
    if (!object->mode_width || !object->mode_height) {
        object_display_size(object, &width, &height);
        if (!object->mode_width) object->mode_width = width;
        if (!object->mode_height) object->mode_height = height;
    }
    emit_display_mode(object);
    DDWG_TRACE("COOPERATIVE_LEVEL object=%08lX size=%lux%lu mode_bpp=%lu",
            (DWORD)(uintptr_t)object, object->mode_width,
            object->mode_height, object->mode_bpp);
    return DDWG_TRACE_RESULT(DD_OK);
}

static HRESULT WINAPI ddraw_SetDisplayMode(IDirectDraw7 *iface, DWORD width,
        DWORD height, DWORD bpp, DWORD refresh, DWORD flags)
{
    DDrawObject *object = object_from_iface(iface);
    DEVMODEA mode;

    (void)flags;
    DDWG_TRACE_CHECKPOINT("CALL ddraw_SetDisplayMode object=%08lX "
            "size=%lux%lux%lu refresh=%lu flags=%08lX cooperative=%08lX",
            (DWORD)(uintptr_t)object, width, height, bpp, refresh, flags,
            object->cooperative_flags);
    if (!width || !height) return DDWG_TRACE_ERROR(DDERR_INVALIDMODE);
    if (bpp != 8 && bpp != 16 && bpp != 24 && bpp != 32)
        return DDWG_TRACE_ERROR(DDERR_INVALIDMODE);

    object->mode_width = width;
    object->mode_height = height;
    object->mode_bpp = bpp;
    object->mode_refresh = refresh;
    object->mode_set = TRUE;

    /*
     * Try to move the guest desktop for real first. When it works, the game's
     * window is the screen and a click lands where the picture says it should;
     * when it does not, the overlay stays at the window's own rectangle at 1:1
     * rather than being stretched, because a stretched overlay with untouched
     * input coordinates looks right and points wrong.
     */
    object->guest_mode_changed = FALSE;
    if (object->cooperative_flags & DDSCL_FULLSCREEN) {
        ZeroMemory(&mode, sizeof(mode));
        mode.dmSize = sizeof(mode);
        mode.dmPelsWidth = width;
        mode.dmPelsHeight = height;
        mode.dmBitsPerPel = bpp;
        mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
        if (refresh) {
            mode.dmDisplayFrequency = refresh;
            mode.dmFields |= DM_DISPLAYFREQUENCY;
        }
        if (ChangeDisplaySettingsA(&mode, CDS_FULLSCREEN) ==
                DISP_CHANGE_SUCCESSFUL)
            object->guest_mode_changed = TRUE;
        else
            HOSTLOG_INFO("the guest desktop could not switch to %lux%lux%lu; "
                    "the overlay is placed over the game window at 1:1 "
                    "instead", width, height, bpp);
    }
    emit_display_mode(object);
    DDWG_TRACE("DISPLAY_MODE_RESULT object=%08lX guest_changed=%lu",
            (DWORD)(uintptr_t)object,
            object->guest_mode_changed ? 1u : 0u);
    return DDWG_TRACE_RESULT(DD_OK);
}

static HRESULT WINAPI ddraw_WaitForVerticalBlank(IDirectDraw7 *iface,
        DWORD flags, HANDLE event)
{
    DDrawObject *object = object_from_iface(iface);
    DWORD height = object->mode_height ? object->mode_height : 480u;

    (void)event;
    if (flags & DDWAITVB_BLOCKBEGIN) {
        /* Sleep to the next ~60 Hz boundary. A title pacing itself on this
         * gets a steady cadence; one polling it in a tight loop stops burning
         * the emulated CPU. */
        DWORD position = raster_position(object);
        DWORD remaining = position < height
            ? ((height - position) * 17u) / height : 0u;
        if (remaining) Sleep(remaining);
    }
    return DD_OK;
}

static HRESULT WINAPI ddraw_GetAvailableVidMem(IDirectDraw7 *iface,
        LPDDSCAPS2 caps, LPDWORD total, LPDWORD free_bytes)
{
    (void)iface; (void)caps;
    if (total) *total = 64u * 1024u * 1024u;
    if (free_bytes) *free_bytes = 48u * 1024u * 1024u;
    return DD_OK;
}

static HRESULT WINAPI ddraw_GetSurfaceFromDC(IDirectDraw7 *iface, HDC dc,
        LPDIRECTDRAWSURFACE7 *out)
{
    DDrawObject *object = object_from_iface(iface);
    DDSurface *surface;

    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = NULL;
    EnterCriticalSection(&g_object_lock);
    for (surface = object->surfaces; surface; surface = surface->next) {
        if (surface->dc == dc) {
            *out = (IDirectDrawSurface7 *)surface;
            break;
        }
    }
    LeaveCriticalSection(&g_object_lock);
    if (!*out) return DDWG_TRACE_ERROR(DDERR_NOTFOUND);
    IDirectDrawSurface7_AddRef(*out);
    return DD_OK;
}

static HRESULT WINAPI ddraw_RestoreAllSurfaces(IDirectDraw7 *iface)
{
    (void)iface;
    return DD_OK;
}

static HRESULT WINAPI ddraw_TestCooperativeLevel(IDirectDraw7 *iface)
{
    (void)iface;
    /* Nothing here is ever lost to another process: the overlay belongs to
     * this page and this session. */
    return DD_OK;
}

static HRESULT WINAPI ddraw_GetDeviceIdentifier(IDirectDraw7 *iface,
        LPDDDEVICEIDENTIFIER2 identifier, DWORD flags)
{
    (void)iface; (void)flags;
    if (!identifier) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    ZeroMemory(identifier, sizeof(*identifier));
    lstrcpynA(identifier->szDriver, "v86gl.sys",
            (int)sizeof(identifier->szDriver));
    lstrcpynA(identifier->szDescription, "v86gl DirectDraw to WebGPU",
            (int)sizeof(identifier->szDescription));
    identifier->dwVendorId = 0x1234;
    identifier->dwDeviceId = 0xD9D7;
    identifier->dwSubSysId = 0;
    identifier->dwRevision = 1;
    identifier->liDriverVersion.LowPart = 1;
    return DD_OK;
}

static HRESULT WINAPI ddraw_StartModeTest(IDirectDraw7 *iface, LPSIZE modes,
        DWORD count, DWORD flags)
{
    (void)iface; (void)modes; (void)count; (void)flags;
    /* Refresh-rate testing has nothing to test against here. */
    return DDWG_TRACE_ERROR(DDERR_TESTFINISHED);
}

static HRESULT WINAPI ddraw_EvaluateMode(IDirectDraw7 *iface, DWORD flags,
        DWORD *timeout)
{
    (void)iface; (void)flags;
    if (timeout) *timeout = 0;
    return DDWG_TRACE_ERROR(DDERR_TESTFINISHED);
}

/* ------------------------------------------------------------------ *
 * Vtables
 * ------------------------------------------------------------------ */

static IDirectDrawSurface7Vtbl g_surface_vtbl = {
    surface_QueryInterface, surface_AddRef, surface_Release,
    surface_AddAttachedSurface, surface_AddOverlayDirtyRect, surface_Blt,
    surface_BltBatch, surface_BltFast, surface_DeleteAttachedSurface,
    surface_EnumAttachedSurfaces, surface_EnumOverlayZOrders, surface_Flip,
    surface_GetAttachedSurface, surface_GetBltStatus, surface_GetCaps,
    surface_GetClipper, surface_GetColorKey, surface_GetDC,
    surface_GetFlipStatus, surface_GetOverlayPosition, surface_GetPalette,
    surface_GetPixelFormat, surface_GetSurfaceDesc, surface_Initialize,
    surface_IsLost, surface_Lock, surface_ReleaseDC, surface_Restore,
    surface_SetClipper, surface_SetColorKey, surface_SetOverlayPosition,
    surface_SetPalette, surface_Unlock, surface_UpdateOverlay,
    surface_UpdateOverlayDisplay, surface_UpdateOverlayZOrder,
    surface_GetDDInterface, surface_PageLock, surface_PageUnlock,
    surface_SetSurfaceDesc, surface_SetPrivateData, surface_GetPrivateData,
    surface_FreePrivateData, surface_GetUniquenessValue,
    surface_ChangeUniquenessValue, surface_SetPriority, surface_GetPriority,
    surface_SetLOD, surface_GetLOD,
};

static IDirectDraw7Vtbl g_ddraw_vtbl = {
    ddraw_QueryInterface, ddraw_AddRef, ddraw_Release, ddraw_Compact,
    ddraw_CreateClipper, ddraw_CreatePalette, ddraw_CreateSurface,
    ddraw_DuplicateSurface, ddraw_EnumDisplayModes, ddraw_EnumSurfaces,
    ddraw_FlipToGDISurface, ddraw_GetCaps, ddraw_GetDisplayMode,
    ddraw_GetFourCCCodes, ddraw_GetGDISurface, ddraw_GetMonitorFrequency,
    ddraw_GetScanLine, ddraw_GetVerticalBlankStatus, ddraw_Initialize,
    ddraw_RestoreDisplayMode, ddraw_SetCooperativeLevel, ddraw_SetDisplayMode,
    ddraw_WaitForVerticalBlank, ddraw_GetAvailableVidMem,
    ddraw_GetSurfaceFromDC, ddraw_RestoreAllSurfaces,
    ddraw_TestCooperativeLevel, ddraw_GetDeviceIdentifier,
    ddraw_StartModeTest, ddraw_EvaluateMode,
};

static IDirectDrawPaletteVtbl g_palette_vtbl = {
    palette_QueryInterface, palette_AddRef, palette_Release, palette_GetCaps,
    palette_GetEntries, palette_Initialize, palette_SetEntries,
};

static IDirectDrawClipperVtbl g_clipper_vtbl = {
    clipper_QueryInterface, clipper_AddRef, clipper_Release,
    clipper_GetClipList, clipper_GetHWnd, clipper_Initialize,
    clipper_IsClipListChanged, clipper_SetClipList, clipper_SetHWnd,
};

/* ------------------------------------------------------------------ *
 * Entry points
 * ------------------------------------------------------------------ */

static DDrawObject *object_create(void)
{
    DDrawObject *object;

    ddwg_log_banner_once();
    object = (DDrawObject *)heap_alloc_zero(sizeof(*object));
    if (!object) return NULL;
    object->vtbl7 = &g_ddraw_vtbl;
    object->vtbl4 = &g_ddraw_vtbl4;
    object->vtbl3 = &g_ddraw_vtbl3;
    object->vtbl2 = &g_ddraw_vtbl2;
    object->vtbl1 = &g_ddraw_vtbl1;
    object->d3d7_vtbl = &g_d3d7_vtbl;
    object->d3d3_vtbl = &g_d3d3_vtbl;
    object->d3d2_vtbl = &g_d3d2_vtbl;
    object->d3d1_vtbl = &g_d3d1_vtbl;
    object->ref = 1;
    object->device_handle = allocate_handle();
    object->mode_bpp = 32;
    EnterCriticalSection(&g_object_lock);
    object->next = g_objects;
    g_objects = object;
    LeaveCriticalSection(&g_object_lock);
    InterlockedIncrement(&g_object_count);
    return object;
}

HRESULT WINAPI DirectDrawCreateEx(GUID *guid, LPVOID *out, REFIID iid,
        IUnknown *outer)
{
    DDrawObject *object;

    (void)guid;
    DDWG_TRACE_CHECKPOINT("CALL DirectDrawCreateEx guid=%08lX iid=%08lX "
            "out=%08lX outer=%08lX", guid ? guid->Data1 : 0u,
            iid ? iid->Data1 : 0u, (DWORD)(uintptr_t)out,
            (DWORD)(uintptr_t)outer);
    if (outer) return DDWG_TRACE_ERROR(CLASS_E_NOAGGREGATION);
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = NULL;
    /* The API says so: DirectDrawCreateEx creates only IDirectDraw7. */
    if (!guid_equal(iid, &IID_IDirectDraw7))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    object = object_create();
    if (!object) return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY);
    emit_hello_once();
    HOSTLOG_INFO("DirectDraw/Direct3D7 frontend attached");
    *out = (IDirectDraw7 *)object;
    DDWG_TRACE("DIRECTDRAW CREATED object=%08lX device=%lu interface=7",
            (DWORD)(uintptr_t)object, object->device_handle);
    return DDWG_TRACE_RESULT(DD_OK);
}

HRESULT WINAPI DirectDrawCreate(GUID *guid, LPDIRECTDRAW *out, IUnknown *outer)
{
    DDrawObject *object;

    (void)guid;
    DDWG_TRACE_CHECKPOINT("CALL DirectDrawCreate guid=%08lX out=%08lX "
            "outer=%08lX", guid ? guid->Data1 : 0u,
            (DWORD)(uintptr_t)out, (DWORD)(uintptr_t)outer);
    if (outer) return DDWG_TRACE_ERROR(CLASS_E_NOAGGREGATION);
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = NULL;
    object = object_create();
    if (!object) return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY);
    emit_hello_once();
    HOSTLOG_INFO("DirectDraw frontend attached through DirectDrawCreate "
            "(version 1 interface)");
    /* The version 1 interface, which is what this entry point promises. The
     * app will usually QueryInterface its way up to version 2 or 4 straight
     * away; all of them are views of this one object. */
    *out = (IDirectDraw *)&object->vtbl1;
    DDWG_TRACE("DIRECTDRAW CREATED object=%08lX device=%lu interface=1",
            (DWORD)(uintptr_t)object, object->device_handle);
    return DDWG_TRACE_RESULT(DD_OK);
}

HRESULT WINAPI DirectDrawCreateClipper(DWORD flags,
        LPDIRECTDRAWCLIPPER *out, IUnknown *outer)
{
    DDClipper *clipper;

    (void)flags;
    if (outer) return DDWG_TRACE_ERROR(CLASS_E_NOAGGREGATION);
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    clipper = (DDClipper *)heap_alloc_zero(sizeof(*clipper));
    if (!clipper) return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY);
    clipper->vtbl = &g_clipper_vtbl;
    clipper->ref = 1;
    clipper->owner = NULL;   /* a free-standing clipper has no DirectDraw */
    *out = (IDirectDrawClipper *)clipper;
    return DD_OK;
}

/* The enumeration callbacks describe one device: the display this page draws
 * over. A NULL GUID is the primary display driver, which is what every title
 * picks. */
HRESULT WINAPI DirectDrawEnumerateA(LPDDENUMCALLBACKA callback, LPVOID context)
{
    if (!callback) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    callback(NULL, (LPSTR)"v86gl DirectDraw to WebGPU", (LPSTR)"display",
            context);
    return DD_OK;
}

HRESULT WINAPI DirectDrawEnumerateW(LPDDENUMCALLBACKW callback, LPVOID context)
{
    static const WCHAR description[] = L"v86gl DirectDraw to WebGPU";
    static const WCHAR name[] = L"display";

    if (!callback) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    callback(NULL, (LPWSTR)description, (LPWSTR)name, context);
    return DD_OK;
}

HRESULT WINAPI DirectDrawEnumerateExA(LPDDENUMCALLBACKEXA callback,
        LPVOID context, DWORD flags)
{
    (void)flags;
    if (!callback) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    callback(NULL, (LPSTR)"v86gl DirectDraw to WebGPU", (LPSTR)"display",
            context, NULL);
    return DD_OK;
}

HRESULT WINAPI DirectDrawEnumerateExW(LPDDENUMCALLBACKEXW callback,
        LPVOID context, DWORD flags)
{
    static const WCHAR description[] = L"v86gl DirectDraw to WebGPU";
    static const WCHAR name[] = L"display";

    (void)flags;
    if (!callback) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    callback(NULL, (LPWSTR)description, (LPWSTR)name, context, NULL);
    return DD_OK;
}

/*
 * COM class-factory entry points. A title that reaches DirectDraw through
 * CoCreateInstance never gets here -- the registry sends it to
 * system32\ddraw.dll, which is a documented limitation of app-local
 * replacement -- but a title that calls DllGetClassObject on this module
 * directly does.
 */
HRESULT WINAPI DllGetClassObject(REFCLSID clsid, REFIID iid, void **out)
{
    (void)clsid; (void)iid;
    if (out) *out = NULL;
    HOSTLOG_REFUSED("DllGetClassObject: creating DirectDraw through COM is "
            "not implemented; use DirectDrawCreateEx");
    return DDWG_TRACE_ERROR(CLASS_E_CLASSNOTAVAILABLE);
}

HRESULT WINAPI DllCanUnloadNow(void)
{
    return g_object_count ? S_FALSE : S_OK;
}

/*
 * The retail ddraw.dll exports these alongside the documented entry points,
 * and a title that statically imports any of them fails to *load* against a
 * DLL that omits them -- which surfaces as "unable to initialize DirectDraw"
 * with no chance to run. The D3D8 path hit the same class of problem with
 * Warcraft III's ValidateVertexShader import.
 *
 * They exist and answer sensibly; the ones with real semantics behind them
 * (D3DParseUnknownCommand, the surface-local accessors) arrive with the
 * Direct3D milestones and say so when called.
 */
void WINAPI AcquireDDThreadLock(void)
{
    EnterCriticalSection(&g_object_lock);
}

void WINAPI ReleaseDDThreadLock(void)
{
    LeaveCriticalSection(&g_object_lock);
}

HRESULT WINAPI D3DParseUnknownCommand(LPVOID command, LPVOID *next)
{
    (void)command;
    if (next) *next = NULL;
    return UNSUPPORTED("D3DParseUnknownCommand", DDERR_INVALIDPARAMS);
}

HRESULT WINAPI CompleteCreateSysmemSurface(DWORD unknown)
{
    (void)unknown;
    return UNSUPPORTED("CompleteCreateSysmemSurface", DDERR_UNSUPPORTED);
}

HRESULT WINAPI DDInternalLock(DWORD unknown1, DWORD unknown2)
{
    (void)unknown1; (void)unknown2;
    return UNSUPPORTED("DDInternalLock", DDERR_UNSUPPORTED);
}

HRESULT WINAPI DDInternalUnlock(DWORD unknown)
{
    (void)unknown;
    return UNSUPPORTED("DDInternalUnlock", DDERR_UNSUPPORTED);
}

DWORD WINAPI GetDDSurfaceLocal(DWORD unknown1, DWORD unknown2, DWORD unknown3)
{
    (void)unknown1; (void)unknown2; (void)unknown3;
    return 0;
}

HRESULT WINAPI GetSurfaceFromDC(DWORD unknown1, DWORD unknown2, DWORD unknown3)
{
    (void)unknown1; (void)unknown2; (void)unknown3;
    return DDWG_TRACE_ERROR(DDERR_NOTFOUND);
}

DWORD WINAPI GetOLEThunkData(DWORD index)
{
    (void)index;
    return 0;
}

HRESULT WINAPI RegisterSpecialCase(DWORD unknown1, DWORD unknown2,
        DWORD unknown3, DWORD unknown4)
{
    (void)unknown1; (void)unknown2; (void)unknown3; (void)unknown4;
    return DD_OK;
}

HRESULT WINAPI DSoundHelp(DWORD unknown1, DWORD unknown2, DWORD unknown3)
{
    (void)unknown1; (void)unknown2; (void)unknown3;
    return DDWG_TRACE_ERROR(DDERR_UNSUPPORTED);
}

void WINAPI SetAppCompatData(DWORD type, DWORD value)
{
    (void)type; (void)value;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
#ifdef DDWG_DIAGNOSTIC_TRACE
    DWORD pending_commands;
#else
    (void)reserved;
#endif
    if (reason == DLL_PROCESS_ATTACH) {
        g_instance = instance;
        DisableThreadLibraryCalls(instance);
        initialize_session_id(instance);
        InitializeCriticalSection(&g_transport_lock);
        InitializeCriticalSection(&g_object_lock);
        InitializeCriticalSection(&g_readback_lock);
#ifdef DDWG_DIAGNOSTIC_TRACE
        v86wg_diagnostic_process_attach(instance);
#endif
    } else if (reason == DLL_PROCESS_DETACH) {
        DDrawObject *object;
#ifdef DDWG_DIAGNOSTIC_TRACE
        pending_commands = g_command_count;
#endif
        /* Hide every live primary before closing the process session. */
        for (object = g_objects; object; object = object->next)
            emit_window_state_not_showing(object);
        emit_session_end();
        EnterCriticalSection(&g_transport_lock);
        if (g_command_count)
            submit_batch_locked(FALSE);
        close_transport_locked();
        LeaveCriticalSection(&g_transport_lock);
        DeleteCriticalSection(&g_readback_lock);
        DeleteCriticalSection(&g_object_lock);
        DeleteCriticalSection(&g_transport_lock);
#ifdef DDWG_DIAGNOSTIC_TRACE
        v86wg_diagnostic_process_detach(reserved, pending_commands);
#endif
    }
    return TRUE;
}

/* ------------------------------------------------------------------ *
 * The pre-version-7 interfaces
 * ------------------------------------------------------------------ *
 *
 * DirectDrawCreate hands back IDirectDraw (version 1), and a title then either
 * uses it directly or asks for version 2 or 4 -- which is how essentially every
 * retail DirectDraw game enters. Version 7 is what this DLL implements; these
 * are the thunks that convert a version's arguments and forward.
 *
 * They are generated rather than written out five times because the versions
 * differ in exactly three ways, all mechanical: the interface pointer type,
 * DDSURFACEDESC against DDSURFACEDESC2 (with DDSCAPS against DDSCAPS2 inside
 * it), and Unlock taking the locked pointer in the old versions and a
 * rectangle in the new ones. Everything else is the same call.
 *
 * A method that hands an interface back returns the *caller's* version, not
 * version 7 -- a version 2 app calling GetAttachedSurface gets an
 * IDirectDrawSurface2, because that is what it will call through.
 */

static UINT legacy_collect_overlays(DDSurface *destination, DWORD flags,
        DDSurface **overlays, UINT capacity)
{
    DDSurface *walk;
    UINT count = 0, i, j;
    for (walk = destination->owner->surfaces; walk; walk = walk->next) {
        if (walk->overlay_destination == destination && count < capacity)
            overlays[count++] = walk;
    }
    for (i = 1; i < count; ++i) {
        DDSurface *value = overlays[i];
        j = i;
        while (j && overlays[j - 1]->overlay_z_order >
                value->overlay_z_order) {
            overlays[j] = overlays[j - 1];
            --j;
        }
        overlays[j] = value;
    }
    if (flags == DDENUMOVERLAYZ_FRONTTOBACK) {
        for (i = 0; i < count / 2u; ++i) {
            DDSurface *swap = overlays[i];
            overlays[i] = overlays[count - 1u - i];
            overlays[count - 1u - i] = swap;
        }
    }
    return count;
}

#define LEGACY_SURFACE_THUNKS(tag, IFACE, VERSION, DESC, CAPS, ENUMCB, \
        CBIFACE, UNLOCKARG, UNLOCK_TO_RECT) \
static HRESULT WINAPI tag##_QueryInterface(IFACE *iface, REFIID iid, \
        void **out) \
{ \
    return surface_QueryInterface(SURFACE7(iface), iid, out); \
} \
static ULONG WINAPI tag##_AddRef(IFACE *iface) \
{ \
    return surface_AddRef(SURFACE7(iface)); \
} \
static ULONG WINAPI tag##_Release(IFACE *iface) \
{ \
    return surface_Release(SURFACE7(iface)); \
} \
static HRESULT WINAPI tag##_AddAttachedSurface(IFACE *iface, IFACE *other) \
{ \
    return surface_AddAttachedSurface(SURFACE7(iface), \
            other ? SURFACE7(other) : NULL); \
} \
static HRESULT WINAPI tag##_AddOverlayDirtyRect(IFACE *iface, LPRECT rect) \
{ \
    return surface_AddOverlayDirtyRect(SURFACE7(iface), rect); \
} \
static HRESULT WINAPI tag##_Blt(IFACE *iface, LPRECT destination_rect, \
        IFACE *source, LPRECT source_rect, DWORD flags, LPDDBLTFX fx) \
{ \
    return surface_Blt(SURFACE7(iface), destination_rect, \
            source ? SURFACE7(source) : NULL, source_rect, flags, fx); \
} \
static HRESULT WINAPI tag##_BltBatch(IFACE *iface, LPDDBLTBATCH batch, \
        DWORD count, DWORD flags) \
{ \
    return surface_BltBatch(SURFACE7(iface), batch, count, flags); \
} \
static HRESULT WINAPI tag##_BltFast(IFACE *iface, DWORD x, DWORD y, \
        IFACE *source, LPRECT source_rect, DWORD trans) \
{ \
    return surface_BltFast(SURFACE7(iface), x, y, \
            source ? SURFACE7(source) : NULL, source_rect, trans); \
} \
static HRESULT WINAPI tag##_DeleteAttachedSurface(IFACE *iface, DWORD flags, \
        IFACE *other) \
{ \
    return surface_DeleteAttachedSurface(SURFACE7(iface), flags, \
            other ? SURFACE7(other) : NULL); \
} \
static HRESULT WINAPI tag##_EnumAttachedSurfaces(IFACE *iface, void *context, \
        ENUMCB callback) \
{ \
    DDSurface *surface = surface_from_any(iface, NULL); \
    DDSurface *attachment; \
    UINT face; \
    if (!callback) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS); \
    if (surface->mip_next) { \
        DESC description; \
        description.dwSize = sizeof(DESC); \
        legacy_describe(surface->mip_next, &description); \
        surface_AddRef(SURFACE7(&surface->mip_next->vtbl7)); \
        if (callback((CBIFACE)surface_interface(surface->mip_next, VERSION), \
                &description, context) == DDENUMRET_CANCEL) \
            return DD_OK; \
    } \
    for (face = 1u; face < 6u; ++face) { \
        DESC description; \
        attachment = surface->cube_faces[face]; \
        if (!attachment) continue; \
        description.dwSize = sizeof(DESC); \
        legacy_describe(attachment, &description); \
        surface_AddRef(SURFACE7(&attachment->vtbl7)); \
        if (callback((CBIFACE)surface_interface(attachment, VERSION), \
                &description, context) == DDENUMRET_CANCEL) \
            return DD_OK; \
    } \
    for (attachment = surface->flip_next; \
            attachment && attachment != surface; \
            attachment = attachment->flip_next) { \
        DESC description; \
        description.dwSize = sizeof(DESC); \
        legacy_describe(attachment, &description); \
        surface_AddRef(SURFACE7(&attachment->vtbl7)); \
        /* The SDK never declared a per-version enumeration callback, so the \
         * pointer type in the typedef is the version 1 one no matter which \
         * interface is enumerating. The object handed over is still the \
         * caller's version, which is what the app casts it back to. */ \
        if (callback((CBIFACE)surface_interface(attachment, VERSION), \
                &description, context) == DDENUMRET_CANCEL) \
            return DD_OK; \
    } \
    if (surface->attached_depth) { \
        DESC description; \
        description.dwSize = sizeof(DESC); \
        legacy_describe(surface->attached_depth, &description); \
        surface_AddRef(SURFACE7(&surface->attached_depth->vtbl7)); \
        callback((CBIFACE)surface_interface(surface->attached_depth, VERSION), \
                &description, context); \
    } \
    return DD_OK; \
} \
static HRESULT WINAPI tag##_EnumOverlayZOrders(IFACE *iface, DWORD flags, \
        void *context, ENUMCB callback) \
{ \
    DDSurface *destination = surface_from_any(iface, NULL); \
    DDSurface *overlays[DDWG_MAX_OVERLAYS]; \
    UINT count, index; \
    if (!callback || (flags != DDENUMOVERLAYZ_BACKTOFRONT && \
            flags != DDENUMOVERLAYZ_FRONTTOBACK)) \
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS); \
    count = legacy_collect_overlays(destination, flags, overlays, \
            DDWG_MAX_OVERLAYS); \
    for (index = 0; index < count; ++index) { \
        DESC description; \
        description.dwSize = sizeof(DESC); \
        legacy_describe(overlays[index], &description); \
        surface_AddRef(SURFACE7(&overlays[index]->vtbl7)); \
        if (callback((CBIFACE)surface_interface(overlays[index], VERSION), \
                &description, context) == DDENUMRET_CANCEL) \
            break; \
    } \
    return DD_OK; \
} \
static HRESULT WINAPI tag##_Flip(IFACE *iface, IFACE *override_surface, \
        DWORD flags) \
{ \
    return surface_Flip(SURFACE7(iface), \
            override_surface ? SURFACE7(override_surface) : NULL, flags); \
} \
static HRESULT WINAPI tag##_GetAttachedSurface(IFACE *iface, CAPS *caps, \
        IFACE **out) \
{ \
    DDSCAPS2 caps2; \
    IDirectDrawSurface7 *found = NULL; \
    HRESULT result; \
    if (!caps || !out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS); \
    *out = NULL; \
    ZeroMemory(&caps2, sizeof(caps2)); \
    if ((VERSION) >= DD_VERSION_4) \
        caps2 = *(const DDSCAPS2 *)(const void *)caps; \
    else \
        caps2.dwCaps = caps->dwCaps; \
    result = surface_GetAttachedSurface(SURFACE7(iface), &caps2, &found); \
    if (SUCCEEDED(result) && found) \
        *out = (IFACE *)surface_interface(surface_from_any(found, NULL), \
                VERSION); \
    return DDWG_TRACE_ERROR(result); \
} \
static HRESULT WINAPI tag##_GetBltStatus(IFACE *iface, DWORD flags) \
{ \
    return surface_GetBltStatus(SURFACE7(iface), flags); \
} \
static HRESULT WINAPI tag##_GetCaps(IFACE *iface, CAPS *caps) \
{ \
    DDSurface *surface = surface_from_any(iface, NULL); \
    if (!caps) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS); \
    ZeroMemory(caps, sizeof(*caps)); \
    caps->dwCaps = surface->desc.ddsCaps.dwCaps; \
    return DD_OK; \
} \
static HRESULT WINAPI tag##_GetClipper(IFACE *iface, \
        LPDIRECTDRAWCLIPPER *out) \
{ \
    return surface_GetClipper(SURFACE7(iface), out); \
} \
static HRESULT WINAPI tag##_GetColorKey(IFACE *iface, DWORD flags, \
        LPDDCOLORKEY key) \
{ \
    return surface_GetColorKey(SURFACE7(iface), flags, key); \
} \
static HRESULT WINAPI tag##_GetDC(IFACE *iface, HDC *out) \
{ \
    return surface_GetDC(SURFACE7(iface), out); \
} \
static HRESULT WINAPI tag##_GetFlipStatus(IFACE *iface, DWORD flags) \
{ \
    return surface_GetFlipStatus(SURFACE7(iface), flags); \
} \
static HRESULT WINAPI tag##_GetOverlayPosition(IFACE *iface, LPLONG x, \
        LPLONG y) \
{ \
    return surface_GetOverlayPosition(SURFACE7(iface), x, y); \
} \
static HRESULT WINAPI tag##_GetPalette(IFACE *iface, \
        LPDIRECTDRAWPALETTE *out) \
{ \
    return surface_GetPalette(SURFACE7(iface), out); \
} \
static HRESULT WINAPI tag##_GetPixelFormat(IFACE *iface, \
        LPDDPIXELFORMAT format) \
{ \
    return surface_GetPixelFormat(SURFACE7(iface), format); \
} \
static HRESULT WINAPI tag##_GetSurfaceDesc(IFACE *iface, DESC *description) \
{ \
    DDSurface *surface = surface_from_any(iface, NULL); \
    if (!description || description->dwSize != sizeof(DESC)) \
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS); \
    legacy_describe(surface, description); \
    return DD_OK; \
} \
static HRESULT WINAPI tag##_Initialize(IFACE *iface, LPDIRECTDRAW dd, \
        DESC *description) \
{ \
    (void)iface; (void)dd; (void)description; \
    return DDWG_TRACE_ERROR(DDERR_ALREADYINITIALIZED); \
} \
static HRESULT WINAPI tag##_IsLost(IFACE *iface) \
{ \
    return surface_IsLost(SURFACE7(iface)); \
} \
static HRESULT WINAPI tag##_Lock(IFACE *iface, LPRECT rect, DESC *description, \
        DWORD flags, HANDLE event) \
{ \
    DDSURFACEDESC2 description2; \
    HRESULT result; \
    if (!description || description->dwSize != sizeof(DESC)) \
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS); \
    ZeroMemory(&description2, sizeof(description2)); \
    description2.dwSize = sizeof(description2); \
    result = surface_Lock(SURFACE7(iface), rect, &description2, flags, event); \
    if (SUCCEEDED(result)) legacy_narrow(&description2, description); \
    return DDWG_TRACE_ERROR(result); \
} \
static HRESULT WINAPI tag##_ReleaseDC(IFACE *iface, HDC dc) \
{ \
    return surface_ReleaseDC(SURFACE7(iface), dc); \
} \
static HRESULT WINAPI tag##_Restore(IFACE *iface) \
{ \
    return surface_Restore(SURFACE7(iface)); \
} \
static HRESULT WINAPI tag##_SetClipper(IFACE *iface, \
        LPDIRECTDRAWCLIPPER clipper) \
{ \
    return surface_SetClipper(SURFACE7(iface), clipper); \
} \
static HRESULT WINAPI tag##_SetColorKey(IFACE *iface, DWORD flags, \
        LPDDCOLORKEY key) \
{ \
    return surface_SetColorKey(SURFACE7(iface), flags, key); \
} \
static HRESULT WINAPI tag##_SetOverlayPosition(IFACE *iface, LONG x, LONG y) \
{ \
    return surface_SetOverlayPosition(SURFACE7(iface), x, y); \
} \
static HRESULT WINAPI tag##_SetPalette(IFACE *iface, \
        LPDIRECTDRAWPALETTE palette) \
{ \
    return surface_SetPalette(SURFACE7(iface), palette); \
} \
static HRESULT WINAPI tag##_Unlock(IFACE *iface, UNLOCKARG locked) \
{ \
    /* Versions 1-3 pass the pointer Lock returned rather than a rectangle. \
     * Passing NULL means "the rectangle this surface was locked with", which \
     * the surface recorded at Lock time and is exactly what the old \
     * signature cannot express. */ \
    return surface_Unlock(SURFACE7(iface), UNLOCK_TO_RECT(locked)); \
} \
static HRESULT WINAPI tag##_UpdateOverlay(IFACE *iface, LPRECT source_rect, \
        IFACE *destination, LPRECT destination_rect, DWORD flags, \
        LPDDOVERLAYFX fx) \
{ \
    return surface_UpdateOverlay(SURFACE7(iface), source_rect, \
            destination ? SURFACE7(destination) : NULL, destination_rect, \
            flags, fx); \
} \
static HRESULT WINAPI tag##_UpdateOverlayDisplay(IFACE *iface, DWORD flags) \
{ \
    return surface_UpdateOverlayDisplay(SURFACE7(iface), flags); \
} \
static HRESULT WINAPI tag##_UpdateOverlayZOrder(IFACE *iface, DWORD flags, \
        IFACE *reference) \
{ \
    return surface_UpdateOverlayZOrder(SURFACE7(iface), flags, \
            reference ? SURFACE7(reference) : NULL); \
}

/* Version 2 added these three; version 1 has no slots for them. */
#define LEGACY_SURFACE_V2_THUNKS(tag, IFACE, VERSION) \
static HRESULT WINAPI tag##_GetDDInterface(IFACE *iface, LPVOID *out) \
{ \
    DDSurface *surface = surface_from_any(iface, NULL); \
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS); \
    ddraw_AddRef((IDirectDraw7 *)&surface->owner->vtbl7); \
    *out = object_interface(surface->owner, VERSION); \
    return DD_OK; \
} \
static HRESULT WINAPI tag##_PageLock(IFACE *iface, DWORD flags) \
{ \
    return surface_PageLock(SURFACE7(iface), flags); \
} \
static HRESULT WINAPI tag##_PageUnlock(IFACE *iface, DWORD flags) \
{ \
    return surface_PageUnlock(SURFACE7(iface), flags); \
}

/* Version 3 added SetSurfaceDesc. */
#define LEGACY_SURFACE_V3_THUNKS(tag, IFACE, DESC) \
static HRESULT WINAPI tag##_SetSurfaceDesc(IFACE *iface, DESC *description, \
        DWORD flags) \
{ \
    DDSURFACEDESC2 description2; \
    if (!description || description->dwSize != sizeof(DESC)) \
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS); \
    if (sizeof(DESC) == sizeof(DDSURFACEDESC2)) \
        CopyMemory(&description2, description, sizeof(description2)); \
    else \
        desc1_to_desc2((const DDSURFACEDESC *)(const void *)description, \
                &description2); \
    return surface_SetSurfaceDesc(SURFACE7(iface), &description2, flags); \
}

/* The version 7 pointer for an object reached through any other version. */
#define SURFACE7(iface) \
    ((IDirectDrawSurface7 *)&surface_from_any((void *)(iface), NULL)->vtbl7)

/* Fills whichever description shape the caller's version uses. */
static void legacy_describe(DDSurface *surface, void *description)
{
    DDSURFACEDESC *narrow = (DDSURFACEDESC *)description;

    if (narrow->dwSize == sizeof(DDSURFACEDESC2)) {
        *(DDSURFACEDESC2 *)description = surface->desc;
        ((DDSURFACEDESC2 *)description)->dwSize = sizeof(DDSURFACEDESC2);
        return;
    }
    desc2_to_desc1(&surface->desc, narrow);
}

static void legacy_narrow(const DDSURFACEDESC2 *from, void *to)
{
    DDSURFACEDESC *narrow = (DDSURFACEDESC *)to;

    if (narrow->dwSize == sizeof(DDSURFACEDESC2)) {
        *(DDSURFACEDESC2 *)to = *from;
        ((DDSURFACEDESC2 *)to)->dwSize = sizeof(DDSURFACEDESC2);
        return;
    }
    desc2_to_desc1(from, narrow);
}

/*
 * Versions 1-3 hand Unlock the pointer Lock returned; version 4 hands it a
 * rectangle. Only the second is a rectangle, and only the second is passed on
 * -- treating a surface pointer as a RECT* would upload whatever four integers
 * happen to sit at the start of the locked bits.
 */
static LPRECT legacy_unlock_rect_pointer(LPVOID locked)
{
    (void)locked;
    return NULL;
}

static LPRECT legacy_unlock_rect_rect(LPRECT locked)
{
    return locked;
}

LEGACY_SURFACE_THUNKS(surface1, IDirectDrawSurface, DD_VERSION_1,
        DDSURFACEDESC, DDSCAPS, LPDDENUMSURFACESCALLBACK,
        LPDIRECTDRAWSURFACE, LPVOID, legacy_unlock_rect_pointer)
LEGACY_SURFACE_THUNKS(surface2, IDirectDrawSurface2, DD_VERSION_2,
        DDSURFACEDESC, DDSCAPS, LPDDENUMSURFACESCALLBACK,
        LPDIRECTDRAWSURFACE, LPVOID, legacy_unlock_rect_pointer)
LEGACY_SURFACE_THUNKS(surface3, IDirectDrawSurface3, DD_VERSION_3,
        DDSURFACEDESC, DDSCAPS, LPDDENUMSURFACESCALLBACK,
        LPDIRECTDRAWSURFACE, LPVOID, legacy_unlock_rect_pointer)
LEGACY_SURFACE_THUNKS(surface4, IDirectDrawSurface4, DD_VERSION_4,
        DDSURFACEDESC2, DDSCAPS2, LPDDENUMSURFACESCALLBACK2,
        LPDIRECTDRAWSURFACE4, LPRECT, legacy_unlock_rect_rect)

LEGACY_SURFACE_V2_THUNKS(surface2, IDirectDrawSurface2, DD_VERSION_2)
LEGACY_SURFACE_V2_THUNKS(surface3, IDirectDrawSurface3, DD_VERSION_3)
LEGACY_SURFACE_V2_THUNKS(surface4, IDirectDrawSurface4, DD_VERSION_4)
LEGACY_SURFACE_V3_THUNKS(surface3, IDirectDrawSurface3, DDSURFACEDESC)
LEGACY_SURFACE_V3_THUNKS(surface4, IDirectDrawSurface4, DDSURFACEDESC2)

/* Version 4 additionally has the private-data and uniqueness methods. */
static HRESULT WINAPI surface4_SetPrivateData(IDirectDrawSurface4 *iface,
        REFGUID tag, LPVOID data, DWORD bytes, DWORD flags)
{
    return surface_SetPrivateData(SURFACE7(iface), tag, data, bytes, flags);
}

static HRESULT WINAPI surface4_GetPrivateData(IDirectDrawSurface4 *iface,
        REFGUID tag, LPVOID buffer, LPDWORD bytes)
{
    return surface_GetPrivateData(SURFACE7(iface), tag, buffer, bytes);
}

static HRESULT WINAPI surface4_FreePrivateData(IDirectDrawSurface4 *iface,
        REFGUID tag)
{
    return surface_FreePrivateData(SURFACE7(iface), tag);
}

static HRESULT WINAPI surface4_GetUniquenessValue(IDirectDrawSurface4 *iface,
        LPDWORD value)
{
    return surface_GetUniquenessValue(SURFACE7(iface), value);
}

static HRESULT WINAPI surface4_ChangeUniquenessValue(
        IDirectDrawSurface4 *iface)
{
    return surface_ChangeUniquenessValue(SURFACE7(iface));
}

static IDirectDrawSurfaceVtbl g_surface_vtbl1 = {
    surface1_QueryInterface, surface1_AddRef, surface1_Release,
    surface1_AddAttachedSurface, surface1_AddOverlayDirtyRect, surface1_Blt,
    surface1_BltBatch, surface1_BltFast, surface1_DeleteAttachedSurface,
    surface1_EnumAttachedSurfaces, surface1_EnumOverlayZOrders, surface1_Flip,
    surface1_GetAttachedSurface, surface1_GetBltStatus, surface1_GetCaps,
    surface1_GetClipper, surface1_GetColorKey, surface1_GetDC,
    surface1_GetFlipStatus, surface1_GetOverlayPosition, surface1_GetPalette,
    surface1_GetPixelFormat, surface1_GetSurfaceDesc, surface1_Initialize,
    surface1_IsLost, surface1_Lock, surface1_ReleaseDC, surface1_Restore,
    surface1_SetClipper, surface1_SetColorKey, surface1_SetOverlayPosition,
    surface1_SetPalette, surface1_Unlock, surface1_UpdateOverlay,
    surface1_UpdateOverlayDisplay, surface1_UpdateOverlayZOrder,
};

static IDirectDrawSurface2Vtbl g_surface_vtbl2 = {
    surface2_QueryInterface, surface2_AddRef, surface2_Release,
    surface2_AddAttachedSurface, surface2_AddOverlayDirtyRect, surface2_Blt,
    surface2_BltBatch, surface2_BltFast, surface2_DeleteAttachedSurface,
    surface2_EnumAttachedSurfaces, surface2_EnumOverlayZOrders, surface2_Flip,
    surface2_GetAttachedSurface, surface2_GetBltStatus, surface2_GetCaps,
    surface2_GetClipper, surface2_GetColorKey, surface2_GetDC,
    surface2_GetFlipStatus, surface2_GetOverlayPosition, surface2_GetPalette,
    surface2_GetPixelFormat, surface2_GetSurfaceDesc, surface2_Initialize,
    surface2_IsLost, surface2_Lock, surface2_ReleaseDC, surface2_Restore,
    surface2_SetClipper, surface2_SetColorKey, surface2_SetOverlayPosition,
    surface2_SetPalette, surface2_Unlock, surface2_UpdateOverlay,
    surface2_UpdateOverlayDisplay, surface2_UpdateOverlayZOrder,
    surface2_GetDDInterface, surface2_PageLock, surface2_PageUnlock,
};

static IDirectDrawSurface3Vtbl g_surface_vtbl3 = {
    surface3_QueryInterface, surface3_AddRef, surface3_Release,
    surface3_AddAttachedSurface, surface3_AddOverlayDirtyRect, surface3_Blt,
    surface3_BltBatch, surface3_BltFast, surface3_DeleteAttachedSurface,
    surface3_EnumAttachedSurfaces, surface3_EnumOverlayZOrders, surface3_Flip,
    surface3_GetAttachedSurface, surface3_GetBltStatus, surface3_GetCaps,
    surface3_GetClipper, surface3_GetColorKey, surface3_GetDC,
    surface3_GetFlipStatus, surface3_GetOverlayPosition, surface3_GetPalette,
    surface3_GetPixelFormat, surface3_GetSurfaceDesc, surface3_Initialize,
    surface3_IsLost, surface3_Lock, surface3_ReleaseDC, surface3_Restore,
    surface3_SetClipper, surface3_SetColorKey, surface3_SetOverlayPosition,
    surface3_SetPalette, surface3_Unlock, surface3_UpdateOverlay,
    surface3_UpdateOverlayDisplay, surface3_UpdateOverlayZOrder,
    surface3_GetDDInterface, surface3_PageLock, surface3_PageUnlock,
    surface3_SetSurfaceDesc,
};

static IDirectDrawSurface4Vtbl g_surface_vtbl4 = {
    surface4_QueryInterface, surface4_AddRef, surface4_Release,
    surface4_AddAttachedSurface, surface4_AddOverlayDirtyRect, surface4_Blt,
    surface4_BltBatch, surface4_BltFast, surface4_DeleteAttachedSurface,
    surface4_EnumAttachedSurfaces, surface4_EnumOverlayZOrders, surface4_Flip,
    surface4_GetAttachedSurface, surface4_GetBltStatus, surface4_GetCaps,
    surface4_GetClipper, surface4_GetColorKey, surface4_GetDC,
    surface4_GetFlipStatus, surface4_GetOverlayPosition, surface4_GetPalette,
    surface4_GetPixelFormat, surface4_GetSurfaceDesc, surface4_Initialize,
    surface4_IsLost, surface4_Lock, surface4_ReleaseDC, surface4_Restore,
    surface4_SetClipper, surface4_SetColorKey, surface4_SetOverlayPosition,
    surface4_SetPalette, surface4_Unlock, surface4_UpdateOverlay,
    surface4_UpdateOverlayDisplay, surface4_UpdateOverlayZOrder,
    surface4_GetDDInterface, surface4_PageLock, surface4_PageUnlock,
    surface4_SetSurfaceDesc, surface4_SetPrivateData, surface4_GetPrivateData,
    surface4_FreePrivateData, surface4_GetUniquenessValue,
    surface4_ChangeUniquenessValue,
};

/* The version 7 pointer for a DirectDraw object reached through any version. */
#define DDRAW7(iface) \
    ((IDirectDraw7 *)&object_from_any((void *)(iface), NULL)->vtbl7)

#define LEGACY_DDRAW_THUNKS(tag, IFACE, SURFIFACE, VERSION, DESC, \
        ENUMMODESCB, ENUMSURFCB) \
static HRESULT WINAPI tag##_QueryInterface(IFACE *iface, REFIID iid, \
        void **out) \
{ \
    return ddraw_QueryInterface(DDRAW7(iface), iid, out); \
} \
static ULONG WINAPI tag##_AddRef(IFACE *iface) \
{ \
    return ddraw_AddRef(DDRAW7(iface)); \
} \
static ULONG WINAPI tag##_Release(IFACE *iface) \
{ \
    return ddraw_Release(DDRAW7(iface)); \
} \
static HRESULT WINAPI tag##_Compact(IFACE *iface) \
{ \
    return ddraw_Compact(DDRAW7(iface)); \
} \
static HRESULT WINAPI tag##_CreateClipper(IFACE *iface, DWORD flags, \
        LPDIRECTDRAWCLIPPER *out, IUnknown *outer) \
{ \
    return ddraw_CreateClipper(DDRAW7(iface), flags, out, outer); \
} \
static HRESULT WINAPI tag##_CreatePalette(IFACE *iface, DWORD flags, \
        LPPALETTEENTRY table, LPDIRECTDRAWPALETTE *out, IUnknown *outer) \
{ \
    return ddraw_CreatePalette(DDRAW7(iface), flags, table, out, outer); \
} \
static HRESULT WINAPI tag##_CreateSurface(IFACE *iface, DESC *request, \
        SURFIFACE **out, IUnknown *outer) \
{ \
    DDSURFACEDESC2 request2; \
    IDirectDrawSurface7 *created = NULL; \
    HRESULT result; \
    if (!out || !request) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS); \
    *out = NULL; \
    if (request->dwSize != sizeof(DESC)) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS); \
    legacy_widen(request, &request2); \
    result = ddraw_CreateSurface(DDRAW7(iface), &request2, &created, outer); \
    if (SUCCEEDED(result) && created) \
        *out = (SURFIFACE *)surface_interface( \
                surface_from_any(created, NULL), VERSION); \
    return DDWG_TRACE_ERROR(result); \
} \
static HRESULT WINAPI tag##_DuplicateSurface(IFACE *iface, SURFIFACE *source, \
        SURFIFACE **out) \
{ \
    IDirectDrawSurface7 *duplicate = NULL; \
    HRESULT result; \
    if (!out || !source) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS); \
    *out = NULL; \
    result = ddraw_DuplicateSurface(DDRAW7(iface), SURFACE7(source), \
            &duplicate); \
    if (SUCCEEDED(result)) \
        *out = (SURFIFACE *)surface_interface( \
                surface_from_any(duplicate, NULL), VERSION); \
    return DDWG_TRACE_ERROR(result); \
} \
static HRESULT WINAPI tag##_EnumDisplayModes(IFACE *iface, DWORD flags, \
        DESC *filter, LPVOID context, ENUMMODESCB callback) \
{ \
    UINT mode; \
    UINT depth; \
    (void)iface; (void)flags; \
    if (!callback) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS); \
    for (mode = 0; mode < sizeof(g_modes) / sizeof(g_modes[0]); ++mode) { \
        for (depth = 0; depth < sizeof(g_mode_depths) / sizeof(DWORD); \
                ++depth) { \
            DESC description; \
            if (filter) { \
                if ((filter->dwFlags & DDSD_WIDTH) && \
                        filter->dwWidth != g_modes[mode].width) \
                    continue; \
                if ((filter->dwFlags & DDSD_HEIGHT) && \
                        filter->dwHeight != g_modes[mode].height) \
                    continue; \
            } \
            ZeroMemory(&description, sizeof(description)); \
            description.dwSize = sizeof(description); \
            description.dwFlags = DDSD_WIDTH | DDSD_HEIGHT | \
                DDSD_PIXELFORMAT | DDSD_PITCH | DDSD_REFRESHRATE; \
            description.dwWidth = g_modes[mode].width; \
            description.dwHeight = g_modes[mode].height; \
            description.dwRefreshRate = 60; \
            mode_pixel_format(g_mode_depths[depth], \
                    &description.ddpfPixelFormat); \
            description.lPitch = (LONG)surface_pitch_for( \
                    g_modes[mode].width, g_mode_depths[depth] / 8u); \
            if (callback(&description, context) == DDENUMRET_CANCEL) \
                return DD_OK; \
        } \
    } \
    return DD_OK; \
} \
static HRESULT WINAPI tag##_EnumSurfaces(IFACE *iface, DWORD flags, \
        DESC *filter, LPVOID context, ENUMSURFCB callback) \
{ \
    DDrawObject *object = object_from_any(iface, NULL); \
    DDSurface *surface; \
    (void)flags; (void)filter; \
    if (!callback) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS); \
    EnterCriticalSection(&g_object_lock); \
    surface = object->surfaces; \
    LeaveCriticalSection(&g_object_lock); \
    while (surface) { \
        DESC description; \
        DDSurface *next = surface->next; \
        description.dwSize = sizeof(DESC); \
        legacy_describe(surface, &description); \
        surface_AddRef((IDirectDrawSurface7 *)&surface->vtbl7); \
        if (callback(surface_interface(surface, VERSION), &description, \
                context) == DDENUMRET_CANCEL) \
            break; \
        surface = next; \
    } \
    return DD_OK; \
} \
static HRESULT WINAPI tag##_FlipToGDISurface(IFACE *iface) \
{ \
    return ddraw_FlipToGDISurface(DDRAW7(iface)); \
} \
static HRESULT WINAPI tag##_GetCaps(IFACE *iface, LPDDCAPS driver, \
        LPDDCAPS emulation) \
{ \
    return ddraw_GetCaps(DDRAW7(iface), driver, emulation); \
} \
static HRESULT WINAPI tag##_GetDisplayMode(IFACE *iface, DESC *description) \
{ \
    DDSURFACEDESC2 description2; \
    HRESULT result; \
    if (!description || description->dwSize != sizeof(DESC)) \
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS); \
    ZeroMemory(&description2, sizeof(description2)); \
    description2.dwSize = sizeof(description2); \
    result = ddraw_GetDisplayMode(DDRAW7(iface), &description2); \
    if (SUCCEEDED(result)) legacy_narrow(&description2, description); \
    return DDWG_TRACE_ERROR(result); \
} \
static HRESULT WINAPI tag##_GetFourCCCodes(IFACE *iface, LPDWORD count, \
        LPDWORD codes) \
{ \
    return ddraw_GetFourCCCodes(DDRAW7(iface), count, codes); \
} \
static HRESULT WINAPI tag##_GetGDISurface(IFACE *iface, SURFIFACE **out) \
{ \
    IDirectDrawSurface7 *found = NULL; \
    HRESULT result; \
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS); \
    *out = NULL; \
    result = ddraw_GetGDISurface(DDRAW7(iface), &found); \
    if (SUCCEEDED(result) && found) \
        *out = (SURFIFACE *)surface_interface(surface_from_any(found, NULL), \
                VERSION); \
    return DDWG_TRACE_ERROR(result); \
} \
static HRESULT WINAPI tag##_GetMonitorFrequency(IFACE *iface, \
        LPDWORD frequency) \
{ \
    return ddraw_GetMonitorFrequency(DDRAW7(iface), frequency); \
} \
static HRESULT WINAPI tag##_GetScanLine(IFACE *iface, LPDWORD scanline) \
{ \
    return ddraw_GetScanLine(DDRAW7(iface), scanline); \
} \
static HRESULT WINAPI tag##_GetVerticalBlankStatus(IFACE *iface, \
        WINBOOL *in_blank) \
{ \
    return ddraw_GetVerticalBlankStatus(DDRAW7(iface), in_blank); \
} \
static HRESULT WINAPI tag##_Initialize(IFACE *iface, GUID *guid) \
{ \
    return ddraw_Initialize(DDRAW7(iface), guid); \
} \
static HRESULT WINAPI tag##_RestoreDisplayMode(IFACE *iface) \
{ \
    return ddraw_RestoreDisplayMode(DDRAW7(iface)); \
} \
static HRESULT WINAPI tag##_SetCooperativeLevel(IFACE *iface, HWND window, \
        DWORD flags) \
{ \
    return ddraw_SetCooperativeLevel(DDRAW7(iface), window, flags); \
} \
static HRESULT WINAPI tag##_WaitForVerticalBlank(IFACE *iface, DWORD flags, \
        HANDLE event) \
{ \
    return ddraw_WaitForVerticalBlank(DDRAW7(iface), flags, event); \
}

/* Widens whichever description shape the caller's version uses. */
static void legacy_widen(const void *from, DDSURFACEDESC2 *to)
{
    const DDSURFACEDESC *narrow = (const DDSURFACEDESC *)from;

    if (narrow->dwSize == sizeof(DDSURFACEDESC2)) {
        *to = *(const DDSURFACEDESC2 *)from;
        to->dwSize = sizeof(DDSURFACEDESC2);
        return;
    }
    desc1_to_desc2(narrow, to);
}

LEGACY_DDRAW_THUNKS(ddraw1, IDirectDraw, IDirectDrawSurface, DD_VERSION_1,
        DDSURFACEDESC, LPDDENUMMODESCALLBACK, LPDDENUMSURFACESCALLBACK)
LEGACY_DDRAW_THUNKS(ddraw2, IDirectDraw2, IDirectDrawSurface, DD_VERSION_2,
        DDSURFACEDESC, LPDDENUMMODESCALLBACK, LPDDENUMSURFACESCALLBACK)
LEGACY_DDRAW_THUNKS(ddraw3, IDirectDraw3, IDirectDrawSurface, DD_VERSION_3,
        DDSURFACEDESC, LPDDENUMMODESCALLBACK, LPDDENUMSURFACESCALLBACK)
LEGACY_DDRAW_THUNKS(ddraw4, IDirectDraw4, IDirectDrawSurface4, DD_VERSION_4,
        DDSURFACEDESC2, LPDDENUMMODESCALLBACK2, LPDDENUMSURFACESCALLBACK2)

/*
 * SetDisplayMode is the one method whose *arity* changed: version 1 takes
 * three arguments, everything after it takes five. A refresh rate of zero and
 * no flags is what version 1 meant.
 */
static HRESULT WINAPI ddraw1_SetDisplayMode(IDirectDraw *iface, DWORD width,
        DWORD height, DWORD bpp)
{
    return ddraw_SetDisplayMode(DDRAW7(iface), width, height, bpp, 0, 0);
}

#define LEGACY_DDRAW_MODE_THUNK(tag, IFACE) \
static HRESULT WINAPI tag##_SetDisplayMode(IFACE *iface, DWORD width, \
        DWORD height, DWORD bpp, DWORD refresh, DWORD flags) \
{ \
    return ddraw_SetDisplayMode(DDRAW7(iface), width, height, bpp, refresh, \
            flags); \
}

LEGACY_DDRAW_MODE_THUNK(ddraw2, IDirectDraw2)
LEGACY_DDRAW_MODE_THUNK(ddraw3, IDirectDraw3)
LEGACY_DDRAW_MODE_THUNK(ddraw4, IDirectDraw4)

/* GetAvailableVidMem arrived in version 2 with a DDSCAPS, and version 4
 * widened it to DDSCAPS2. Neither is read: there is no fixed pool to report
 * against. */
#define LEGACY_DDRAW_VIDMEM_THUNK(tag, IFACE, CAPS) \
static HRESULT WINAPI tag##_GetAvailableVidMem(IFACE *iface, CAPS *caps, \
        LPDWORD total, LPDWORD free_bytes) \
{ \
    (void)caps; \
    return ddraw_GetAvailableVidMem(DDRAW7(iface), NULL, total, free_bytes); \
}

LEGACY_DDRAW_VIDMEM_THUNK(ddraw2, IDirectDraw2, DDSCAPS)
LEGACY_DDRAW_VIDMEM_THUNK(ddraw3, IDirectDraw3, DDSCAPS)
LEGACY_DDRAW_VIDMEM_THUNK(ddraw4, IDirectDraw4, DDSCAPS2)

#define LEGACY_DDRAW_SURFACE_FROM_DC_THUNK(tag, IFACE, SURFIFACE, VERSION) \
static HRESULT WINAPI tag##_GetSurfaceFromDC(IFACE *iface, HDC dc, \
        SURFIFACE **out) \
{ \
    IDirectDrawSurface7 *found = NULL; \
    HRESULT result; \
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS); \
    *out = NULL; \
    result = ddraw_GetSurfaceFromDC(DDRAW7(iface), dc, &found); \
    if (SUCCEEDED(result) && found) \
        *out = (SURFIFACE *)surface_interface(surface_from_any(found, NULL), \
                VERSION); \
    return DDWG_TRACE_ERROR(result); \
}

LEGACY_DDRAW_SURFACE_FROM_DC_THUNK(ddraw3, IDirectDraw3, IDirectDrawSurface,
        DD_VERSION_3)
LEGACY_DDRAW_SURFACE_FROM_DC_THUNK(ddraw4, IDirectDraw4, IDirectDrawSurface4,
        DD_VERSION_4)

static HRESULT WINAPI ddraw4_RestoreAllSurfaces(IDirectDraw4 *iface)
{
    return ddraw_RestoreAllSurfaces(DDRAW7(iface));
}

static HRESULT WINAPI ddraw4_TestCooperativeLevel(IDirectDraw4 *iface)
{
    return ddraw_TestCooperativeLevel(DDRAW7(iface));
}

/* DDDEVICEIDENTIFIER2 added dwWHQLLevel; everything before it is identical,
 * so the version 4 form is the version 7 one minus its last field. */
static HRESULT WINAPI ddraw4_GetDeviceIdentifier(IDirectDraw4 *iface,
        LPDDDEVICEIDENTIFIER identifier, DWORD flags)
{
    DDDEVICEIDENTIFIER2 wide;
    HRESULT result;

    if (!identifier) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    result = ddraw_GetDeviceIdentifier(DDRAW7(iface), &wide, flags);
    if (FAILED(result)) return DDWG_TRACE_ERROR(result);
    ZeroMemory(identifier, sizeof(*identifier));
    CopyMemory(identifier->szDriver, wide.szDriver,
            sizeof(identifier->szDriver));
    CopyMemory(identifier->szDescription, wide.szDescription,
            sizeof(identifier->szDescription));
    identifier->liDriverVersion = wide.liDriverVersion;
    identifier->dwVendorId = wide.dwVendorId;
    identifier->dwDeviceId = wide.dwDeviceId;
    identifier->dwSubSysId = wide.dwSubSysId;
    identifier->dwRevision = wide.dwRevision;
    identifier->guidDeviceIdentifier = wide.guidDeviceIdentifier;
    return DD_OK;
}

static IDirectDrawVtbl g_ddraw_vtbl1 = {
    ddraw1_QueryInterface, ddraw1_AddRef, ddraw1_Release, ddraw1_Compact,
    ddraw1_CreateClipper, ddraw1_CreatePalette, ddraw1_CreateSurface,
    ddraw1_DuplicateSurface, ddraw1_EnumDisplayModes, ddraw1_EnumSurfaces,
    ddraw1_FlipToGDISurface, ddraw1_GetCaps, ddraw1_GetDisplayMode,
    ddraw1_GetFourCCCodes, ddraw1_GetGDISurface, ddraw1_GetMonitorFrequency,
    ddraw1_GetScanLine, ddraw1_GetVerticalBlankStatus, ddraw1_Initialize,
    ddraw1_RestoreDisplayMode, ddraw1_SetCooperativeLevel,
    ddraw1_SetDisplayMode, ddraw1_WaitForVerticalBlank,
};

static IDirectDraw2Vtbl g_ddraw_vtbl2 = {
    ddraw2_QueryInterface, ddraw2_AddRef, ddraw2_Release, ddraw2_Compact,
    ddraw2_CreateClipper, ddraw2_CreatePalette, ddraw2_CreateSurface,
    ddraw2_DuplicateSurface, ddraw2_EnumDisplayModes, ddraw2_EnumSurfaces,
    ddraw2_FlipToGDISurface, ddraw2_GetCaps, ddraw2_GetDisplayMode,
    ddraw2_GetFourCCCodes, ddraw2_GetGDISurface, ddraw2_GetMonitorFrequency,
    ddraw2_GetScanLine, ddraw2_GetVerticalBlankStatus, ddraw2_Initialize,
    ddraw2_RestoreDisplayMode, ddraw2_SetCooperativeLevel,
    ddraw2_SetDisplayMode, ddraw2_WaitForVerticalBlank,
    ddraw2_GetAvailableVidMem,
};

static IDirectDraw3Vtbl g_ddraw_vtbl3 = {
    ddraw3_QueryInterface, ddraw3_AddRef, ddraw3_Release, ddraw3_Compact,
    ddraw3_CreateClipper, ddraw3_CreatePalette, ddraw3_CreateSurface,
    ddraw3_DuplicateSurface, ddraw3_EnumDisplayModes, ddraw3_EnumSurfaces,
    ddraw3_FlipToGDISurface, ddraw3_GetCaps, ddraw3_GetDisplayMode,
    ddraw3_GetFourCCCodes, ddraw3_GetGDISurface, ddraw3_GetMonitorFrequency,
    ddraw3_GetScanLine, ddraw3_GetVerticalBlankStatus, ddraw3_Initialize,
    ddraw3_RestoreDisplayMode, ddraw3_SetCooperativeLevel,
    ddraw3_SetDisplayMode, ddraw3_WaitForVerticalBlank,
    ddraw3_GetAvailableVidMem, ddraw3_GetSurfaceFromDC,
};

static IDirectDraw4Vtbl g_ddraw_vtbl4 = {
    ddraw4_QueryInterface, ddraw4_AddRef, ddraw4_Release, ddraw4_Compact,
    ddraw4_CreateClipper, ddraw4_CreatePalette, ddraw4_CreateSurface,
    ddraw4_DuplicateSurface, ddraw4_EnumDisplayModes, ddraw4_EnumSurfaces,
    ddraw4_FlipToGDISurface, ddraw4_GetCaps, ddraw4_GetDisplayMode,
    ddraw4_GetFourCCCodes, ddraw4_GetGDISurface, ddraw4_GetMonitorFrequency,
    ddraw4_GetScanLine, ddraw4_GetVerticalBlankStatus, ddraw4_Initialize,
    ddraw4_RestoreDisplayMode, ddraw4_SetCooperativeLevel,
    ddraw4_SetDisplayMode, ddraw4_WaitForVerticalBlank,
    ddraw4_GetAvailableVidMem, ddraw4_GetSurfaceFromDC,
    ddraw4_RestoreAllSurfaces, ddraw4_TestCooperativeLevel,
    ddraw4_GetDeviceIdentifier,
};
