/*
 * App-local Direct3D 9 frontend for Windows XP guests running in v86.
 *
 * COM objects remain frontend-owned. D3D8 and DirectDraw emit this same
 * D9WG protocol and host executor. Pure retry/upload mechanics are shared
 * by d9wg_runtime, together with process-wide DMA, query and handle state.
 *
 * Current scope: device/resource lifecycle, vertex declarations and FVF,
 * vertex/index buffers, 2D/cube/volume textures, integer and floating-point
 * render targets, four-sample MSAA resolves, the fixed-function draw path,
 * shader model 1.1-3.0 (including vertex texture fetch), instancing, user clip
 * planes, state blocks, asynchronous GPU queries, and GPU render-target
 * readback through the response tail of the shared DMA arena.
 *
 * Software ProcessVertices uses the shared fixed-function vertex kernel.
 * Programmable vertex processing is still refused explicitly.
 *
 * The discipline throughout: an entry point either does what D3D9 says or
 * fails, and GetDeviceCaps only claims what is actually implemented. Where an
 * approximation is unavoidable it is exposed through the host's bounded
 * getStats() counters.
 */

/* Private consumers link the same implementation without publishing the
 * d3d9.dll loader/compatibility exports in their own module. */
#ifdef D9WG_STATIC_CORE
#define Direct3DCreate9 d9wg_d3d9_core_create
#define DllMain d9wg_d3d9_core_module
#endif

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <d3d9.h>
#include "d3d9_core.h"
#include "../runtime/d9wg_declaration8.h"
#include "../runtime/d9wg_shader8_local_defs.h"
#include <stdint.h>
#include "../runtime/d9wg_runtime.h"
#include "../runtime/d9wg_reset.h"
#include "../runtime/d9wg_present_parameters.h"
#include "../runtime/d9wg_display_mode.h"
#include "../runtime/d9wg_window_reset.h"
#include "../runtime/d9wg_window_mode.h"
#include "../runtime/d9wg_private_data.h"
#include "../runtime/d9wg_present_pacing.h"
#include "../runtime/d9wg_cursor.h"
#include "../runtime/d9wg_surface_readback.h"
#include "../runtime/d9wg_surface_lock.h"
#include "../runtime/d9wg_backbuffer_upload.h"
#include "../runtime/d9wg_format_layouts.h"
#include "../runtime/d9wg_depth_surface_layout8.h"
#include "../runtime/d9wg_format_queries.h"
#include "../runtime/d9wg_state_defaults.h"
#include "../runtime/d9wg_state_bridge8.h"
#include "../runtime/d9wg_fvf.h"
#include "../runtime/d9wg_math.h"
#define D9WG_LIGHTING_SDK_MATERIAL D3DMATERIAL9
#define D9WG_LIGHTING_SDK_LIGHT D3DLIGHT9
#include "../runtime/d9wg_lighting_bridge.h"
#include "../runtime/d9wg_palette_state.h"
#include "../runtime/d9wg_light_state.h"
#define D9WG_STATE_BLOCK_API 9
#include "../runtime/d9wg_state_block_scope.h"
#include "../runtime/d9wg_vertex.h"
#include "../runtime/d9wg_process_layout8.h"
#include "../runtime/d9wg_vertex_windows.h"
#include "../runtime/d9wg_fpu.h"
#include "../runtime/d9wg_transport.h"
#include "../runtime/d9wg_process_aliases.h"
#ifdef D9WG_DIAGNOSTIC_TRACE
#include <stdarg.h>
#endif
#include "../openglproxy/v86gl_ioctl.h"
#include "d3d9_protocol.h"
#include "../runtime/d9wg_window_probe.h"
#include "../runtime/d9wg_window_watch.h"
#include "../runtime/d9wg_buffer_lock.h"

_Static_assert(D3DLOCK_READONLY == D9WG_BUFFER_LOCK_READONLY, "readonly lock flag");
_Static_assert(D3DLOCK_NOOVERWRITE == D9WG_BUFFER_LOCK_NOOVERWRITE, "nooverwrite lock flag");
_Static_assert(D3DLOCK_DISCARD == D9WG_BUFFER_LOCK_DISCARD, "discard lock flag");
_Static_assert(D3DUSAGE_WRITEONLY == D9WG_BUFFER_USAGE_WRITEONLY, "writeonly buffer usage");
_Static_assert(D3DUSAGE_DYNAMIC == D9WG_BUFFER_USAGE_DYNAMIC, "dynamic buffer usage");
#include "../upload_bands.h"

#ifndef D3DSTREAMSOURCE_INDEXEDDATA
#define D3DSTREAMSOURCE_INDEXEDDATA 0x40000000u
#endif
#ifndef D3DSTREAMSOURCE_INSTANCEDATA
#define D3DSTREAMSOURCE_INSTANCEDATA 0x80000000u
#endif
#define D9_STREAMSOURCE_FREQUENCY_MASK 0x3fffffffu

#ifdef D9WG_DIAGNOSTIC_TRACE
/*
 * Small, CRT-free first-chance diagnostic trace. This is compiled only by
 * build.sh --diagnostic; the ordinary d3d9.dll has no file I/O or VEH overhead.
 * Every line is one WriteFile call.  Ordinary calls are flushed in batches so
 * tracing does not perturb the game as much; an exception and process detach
 * explicitly flush their complete diagnostic tail.
 */
static HANDLE g_trace_file = INVALID_HANDLE_VALUE;
static PVOID g_trace_veh;
static volatile LONG g_trace_in_veh;
static volatile LONG g_trace_call_sequence;
static volatile LONG g_trace_last_enter_sequence;
static volatile LONG g_trace_last_exit_sequence;
static volatile LONG g_trace_last_hresult;
static volatile LONG g_trace_last_output;
static const char *g_trace_last_enter_method = "none";
static const char *g_trace_last_exit_method = "none";
static HMODULE g_trace_exe_module;
static HMODULE g_trace_self_module;
static char g_trace_exe_path[MAX_PATH];
static char g_trace_self_path[MAX_PATH];
static volatile LONG g_trace_vb_count;
static volatile LONG g_trace_ib_count;
static volatile LONG g_trace_vb_bytes;
static volatile LONG g_trace_ib_bytes;
static volatile LONG g_trace_last_freed_surface;
/* Reset by trace_frame_summary() after every Present. */
static volatile LONG g_frame_draws;
static volatile LONG g_frame_primitives;
static volatile LONG g_frame_rejected;
static volatile LONG g_frame_clears;
/* The device window, so PROCESS_DETACH can say whether it outlived the
 * process. A title that quit because its window was destroyed and the message
 * pump drained a WM_QUIT looks identical, from D3D9's side, to one that called
 * ExitProcess outright -- this is the bit that tells the two apart. */
static HWND g_trace_device_window;
#define D9_TRACE_MAX_RANGES 4096
typedef struct D9TraceRange {
    const char *kind;
    DWORD ordinal;
    DWORD object;
    DWORD start;
    DWORD end;
} D9TraceRange;
static D9TraceRange g_trace_ranges[D9_TRACE_MAX_RANGES];
static volatile LONG g_trace_range_count;

static void trace_open(HINSTANCE instance)
{
    char path[MAX_PATH];
    DWORD length = GetModuleFileNameA(instance, path, MAX_PATH);

    g_trace_self_module = (HMODULE)instance;
    g_trace_exe_module = GetModuleHandleA(NULL);
    ZeroMemory(g_trace_self_path, sizeof(g_trace_self_path));
    ZeroMemory(g_trace_exe_path, sizeof(g_trace_exe_path));
    GetModuleFileNameA(g_trace_self_module, g_trace_self_path,
            MAX_PATH - 1);
    GetModuleFileNameA(g_trace_exe_module, g_trace_exe_path, MAX_PATH - 1);
    if (!length || length >= MAX_PATH)
        return;
    while (length && path[length - 1] != '\\' && path[length - 1] != '/')
        --length;
#ifdef D9WG_STATIC_CORE
    if (length + sizeof("d3d9_core_trace_4294967295.log") > MAX_PATH)
        return;
    wsprintfA(path + length, "d3d9_core_trace_%lu.log", GetCurrentProcessId());
#else
    if (length + sizeof("d3d9_trace_4294967295.log") > MAX_PATH)
        return;
    wsprintfA(path + length, "d3d9_trace_%lu.log", GetCurrentProcessId());
#endif
    g_trace_file = CreateFileA(path, GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, NULL);
}

static void trace_write(const char *format, ...)
{
    char message[768];
    char line[896];
    DWORD written;
    va_list arguments;

    if (g_trace_file == INVALID_HANDLE_VALUE)
        return;
    va_start(arguments, format);
    wvsprintfA(message, format, arguments);
    va_end(arguments);
    wsprintfA(line, "[%08lX %lu:%lu] %s\r\n", GetTickCount(),
            GetCurrentProcessId(), GetCurrentThreadId(), message);
    WriteFile(g_trace_file, line, (DWORD)lstrlenA(line), &written, NULL);
}

static void trace_flush(void)
{
    if (g_trace_file != INVALID_HANDLE_VALUE)
        FlushFileBuffers(g_trace_file);
}

static void trace_mark_enter(const char *method)
{
    LONG sequence = InterlockedIncrement(&g_trace_call_sequence);
    g_trace_last_enter_method = method;
    InterlockedExchange(&g_trace_last_enter_sequence, sequence);
}

static void trace_mark_exit(const char *method, HRESULT result,
        const void *output)
{
    LONG sequence = InterlockedIncrement(&g_trace_call_sequence);
    g_trace_last_exit_method = method;
    InterlockedExchange(&g_trace_last_hresult, (LONG)result);
    InterlockedExchange(&g_trace_last_output, (LONG)(uintptr_t)output);
    InterlockedExchange(&g_trace_last_exit_sequence, sequence);
}

static void trace_buffer_created(BOOL index_buffer, UINT length)
{
    if (index_buffer) {
        InterlockedIncrement(&g_trace_ib_count);
        InterlockedExchangeAdd(&g_trace_ib_bytes, (LONG)length);
    } else {
        InterlockedIncrement(&g_trace_vb_count);
        InterlockedExchangeAdd(&g_trace_vb_bytes, (LONG)length);
    }
}

static void trace_register_range(const char *kind, DWORD ordinal,
        const void *object, const void *start, DWORD byte_count)
{
    LONG slot;
    D9TraceRange *range;
    DWORD address = (DWORD)(uintptr_t)start;

    if (!start || !byte_count)
        return;
    slot = InterlockedIncrement(&g_trace_range_count) - 1;
    if (slot < 0 || slot >= D9_TRACE_MAX_RANGES)
        return;
    range = &g_trace_ranges[slot];
    range->ordinal = ordinal;
    range->object = (DWORD)(uintptr_t)object;
    range->start = address;
    range->end = address + byte_count;
    range->kind = kind;
}

static void trace_match_address(const char *label, DWORD address)
{
    LONG count = InterlockedCompareExchange(&g_trace_range_count, 0, 0);
    LONG index;
    BOOL matched = FALSE;

    if (count > D9_TRACE_MAX_RANGES)
        count = D9_TRACE_MAX_RANGES;
    for (index = 0; index < count; ++index) {
        D9TraceRange *range = &g_trace_ranges[index];
        if (!range->kind || address < range->start || address >= range->end)
            continue;
        trace_write("POINTER_MATCH label=%s address=%08lX kind=%s "
                "ordinal=%lu object=%08lX range=%08lX-%08lX offset=%lu",
                label, address, range->kind, range->ordinal, range->object,
                range->start, range->end, address - range->start);
        matched = TRUE;
    }
    if (!matched)
        trace_write("POINTER_MATCH label=%s address=%08lX kind=none",
                label, address);
}

static void trace_hex_line(const char *label, DWORD address, const BYTE *data)
{
    trace_write("%s %08lX: %02X %02X %02X %02X %02X %02X %02X %02X "
            "%02X %02X %02X %02X %02X %02X %02X %02X", label, address,
            data[0], data[1], data[2], data[3], data[4], data[5], data[6],
            data[7], data[8], data[9], data[10], data[11], data[12], data[13],
            data[14], data[15]);
}

static void trace_memory_block(const char *label, DWORD address)
{
    BYTE data[64];
    SIZE_T received = 0;

    ZeroMemory(data, sizeof(data));
    if (!address || !ReadProcessMemory(GetCurrentProcess(),
            (LPCVOID)(uintptr_t)address, data, sizeof(data), &received)
            || received != sizeof(data)) {
        trace_write("%s read failed address=%08lX got=%lu error=%lu", label,
                address, (DWORD)received, GetLastError());
        return;
    }
    trace_hex_line(label, address, data);
    trace_hex_line(label, address + 16u, data + 16);
    trace_hex_line(label, address + 32u, data + 32);
    trace_hex_line(label, address + 48u, data + 48);
}

/*
 * The last marked method to be entered and the last to be left. Written on the
 * way out of the process as well as on an exception: a title that decides to
 * quit rather than crash leaves no other record of where it got to, which is
 * how GTA San Andreas's exit looked from the trace -- a gap and then
 * PROCESS_DETACH.
 */
/*
 * Whether the device window outlived the process, and who holds the foreground.
 * Kept out of trace_last_call() because that one also runs from the vectored
 * exception handler, where calling into USER32 risks faulting a second time --
 * on a stack overflow especially. This is only meaningful at exit anyway.
 */
/*
 * The guest's memory situation.
 *
 * This proxy keeps a full shadow copy of every managed texture and buffer in
 * guest RAM, on top of the copy the application itself holds, so it roughly
 * doubles a title's texture footprint. GTA San Andreas reaches its loading
 * screen, streams a few hundred textures, and then shuts down cleanly -- which
 * is what a title does when one of *its own* allocations fails, and is
 * invisible from the D3D9 side because none of the proxy's own allocations
 * failed. Sampling this makes the difference between "the guest ran out" and
 * "something else entirely" a fact rather than a theory.
 */
static void trace_memory_state(const char *reason)
{
    MEMORYSTATUS status;

    ZeroMemory(&status, sizeof(status));
    status.dwLength = sizeof(status);
    GlobalMemoryStatus(&status);
    trace_write("MEM %s load=%lu%% phys=%luK/%luK page=%luK/%luK "
            "virtual=%luK/%luK", reason, status.dwMemoryLoad,
            (DWORD)(status.dwAvailPhys / 1024u),
            (DWORD)(status.dwTotalPhys / 1024u),
            (DWORD)(status.dwAvailPageFile / 1024u),
            (DWORD)(status.dwTotalPageFile / 1024u),
            (DWORD)(status.dwAvailVirtual / 1024u),
            (DWORD)(status.dwTotalVirtual / 1024u));
}

/*
 * One line per frame: what the title drew, and what was refused.
 *
 * `rejected` is the field to read first. It counts draws the proxy turned down
 * before emitting anything, which is the one failure mode that looks identical
 * to a title simply not drawing.
 */
static void trace_frame_summary(void)
{
    LONG draws = InterlockedExchange(&g_frame_draws, 0);
    LONG primitives = InterlockedExchange(&g_frame_primitives, 0);
    LONG rejected = InterlockedExchange(&g_frame_rejected, 0);
    LONG clears = InterlockedExchange(&g_frame_clears, 0);

    /* A frame that drew nothing and refused nothing is not worth a line --
     * that is the loading screen presenting an already-composed image, and it
     * would otherwise bury the frames that do have something to say. */
    if (!draws && !rejected && !clears)
        return;
    trace_write("FRAME draws=%ld primitives=%ld rejected=%ld clears=%ld",
            draws, primitives, rejected, clears);
}

static void trace_window_state(void)
{
    HWND window = g_trace_device_window;
    BOOL alive = window && IsWindow(window);

    trace_write("WINDOW hwnd=%08lX alive=%lu visible=%lu foreground=%08lX "
            "active=%08lX", (DWORD)(uintptr_t)window, (DWORD)alive,
            (DWORD)(alive && IsWindowVisible(window)),
            (DWORD)(uintptr_t)GetForegroundWindow(),
            (DWORD)(uintptr_t)GetActiveWindow());
}

static void trace_last_call(const char *reason)
{
    trace_write("LAST reason=%s enter_seq=%ld enter=%s exit_seq=%ld exit=%s "
            "hr=%08lX out=%08lX vb=%ld/%luB ib=%ld/%luB", reason,
            InterlockedCompareExchange(&g_trace_last_enter_sequence, 0, 0),
            g_trace_last_enter_method,
            InterlockedCompareExchange(&g_trace_last_exit_sequence, 0, 0),
            g_trace_last_exit_method,
            (DWORD)InterlockedCompareExchange(&g_trace_last_hresult, 0, 0),
            (DWORD)InterlockedCompareExchange(&g_trace_last_output, 0, 0),
            InterlockedCompareExchange(&g_trace_vb_count, 0, 0),
            (DWORD)InterlockedCompareExchange(&g_trace_vb_bytes, 0, 0),
            InterlockedCompareExchange(&g_trace_ib_count, 0, 0),
            (DWORD)InterlockedCompareExchange(&g_trace_ib_bytes, 0, 0));
}

static LONG WINAPI trace_vectored_exception(PEXCEPTION_POINTERS pointers)
{
    EXCEPTION_RECORD *record;
    CONTEXT *context;
    ULONG_PTR parameter0 = 0;
    ULONG_PTR parameter1 = 0;
    BOOL deep_dump = FALSE;

    if (!pointers || !pointers->ExceptionRecord)
        return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedCompareExchange(&g_trace_in_veh, 1, 0))
        return EXCEPTION_CONTINUE_SEARCH;
    record = pointers->ExceptionRecord;
    context = pointers->ContextRecord;
    if (record->NumberParameters > 0)
        parameter0 = record->ExceptionInformation[0];
    if (record->NumberParameters > 1)
        parameter1 = record->ExceptionInformation[1];
#if defined(__i386__) || defined(_M_IX86)
    if (context) {
        trace_write("EXCEPTION code=%08lX flags=%08lX address=%08lX "
                "params=%lu p0=%08lX p1=%08lX eip=%08lX esp=%08lX "
                "ebp=%08lX eax=%08lX ebx=%08lX ecx=%08lX edx=%08lX "
                "esi=%08lX edi=%08lX",
                record->ExceptionCode, record->ExceptionFlags,
                (DWORD)(uintptr_t)record->ExceptionAddress,
                record->NumberParameters, (DWORD)parameter0,
                (DWORD)parameter1, context->Eip, context->Esp, context->Ebp,
                context->Eax, context->Ebx, context->Ecx, context->Edx,
                context->Esi, context->Edi);
        deep_dump = record->ExceptionCode == EXCEPTION_ACCESS_VIOLATION
                || record->ExceptionCode == EXCEPTION_ILLEGAL_INSTRUCTION;
    } else
#endif
    {
        trace_write("EXCEPTION code=%08lX flags=%08lX address=%08lX "
                "params=%lu p0=%08lX p1=%08lX",
                record->ExceptionCode, record->ExceptionFlags,
                (DWORD)(uintptr_t)record->ExceptionAddress,
                record->NumberParameters, (DWORD)parameter0,
                (DWORD)parameter1);
    }
    trace_last_call("exception");
#if defined(__i386__) || defined(_M_IX86)
    if (context && deep_dump
            && record->ExceptionCode != EXCEPTION_STACK_OVERFLOW) {
        MEMORY_BASIC_INFORMATION memory;
        SIZE_T queried;
        BYTE code[48];
        SIZE_T received = 0;
        HANDLE process = GetCurrentProcess();
        DWORD code_start = context->Eip >= 16 ? context->Eip - 16
                : context->Eip;
        DWORD stack_values[16];
        DWORD stack_valid = 0;
        DWORD index;
        DWORD outer_field_18 = 0;
        DWORD candidate_vtbl = 0;

        ZeroMemory(&memory, sizeof(memory));
        queried = VirtualQuery((LPCVOID)(uintptr_t)context->Eip, &memory,
                sizeof(memory));
        if (queried) {
            DWORD allocation = (DWORD)(uintptr_t)memory.AllocationBase;
            const char *owner = "unknown";
            if (memory.AllocationBase == g_trace_exe_module)
                owner = "exe";
            else if (memory.AllocationBase == g_trace_self_module)
                owner = "proxy";
            trace_write("EIP_REGION owner=%s base=%08lX allocation=%08lX rva=%08lX "
                    "size=%08lX state=%08lX protect=%08lX type=%08lX",
                    owner, (DWORD)(uintptr_t)memory.BaseAddress, allocation,
                    context->Eip - allocation, (DWORD)memory.RegionSize,
                    memory.State, memory.Protect, memory.Type);
        } else {
            trace_write("EIP_REGION VirtualQuery failed error=%lu",
                    GetLastError());
        }

        ZeroMemory(code, sizeof(code));
        if (ReadProcessMemory(process, (LPCVOID)(uintptr_t)code_start, code,
                sizeof(code), &received) && received == sizeof(code)) {
            trace_hex_line("CODE", code_start, code);
            trace_hex_line("CODE", code_start + 16, code + 16);
            trace_hex_line("CODE", code_start + 32, code + 32);
        } else {
            trace_write("CODE read failed start=%08lX got=%lu error=%lu",
                    code_start, (DWORD)received, GetLastError());
        }

        ZeroMemory(stack_values, sizeof(stack_values));
        for (index = 0; index < 16; ++index) {
            DWORD value = 0;
            received = 0;
            if (ReadProcessMemory(process,
                    (LPCVOID)(uintptr_t)(context->Esp + index * 4u), &value,
                    sizeof(value), &received) && received == sizeof(value)) {
                stack_values[index] = value;
                stack_valid |= 1u << index;
            }
        }
        trace_write("STACK esp=%08lX valid=%04lX +00=%08lX +04=%08lX +08=%08lX +0C=%08lX",
                context->Esp, stack_valid, stack_values[0], stack_values[1],
                stack_values[2], stack_values[3]);
        trace_write("STACK esp=%08lX +10=%08lX +14=%08lX +18=%08lX +1C=%08lX",
                context->Esp, stack_values[4], stack_values[5],
                stack_values[6], stack_values[7]);
        trace_write("STACK esp=%08lX +20=%08lX +24=%08lX +28=%08lX +2C=%08lX",
                context->Esp, stack_values[8], stack_values[9],
                stack_values[10], stack_values[11]);
        trace_write("STACK esp=%08lX +30=%08lX +34=%08lX +38=%08lX +3C=%08lX",
                context->Esp, stack_values[12], stack_values[13],
                stack_values[14], stack_values[15]);

        received = 0;
        if (ReadProcessMemory(process,
                (LPCVOID)(uintptr_t)(context->Eax + 0x18u), &outer_field_18,
                sizeof(outer_field_18), &received)
                && received == sizeof(outer_field_18)) {
            received = 0;
            if (!ReadProcessMemory(process,
                    (LPCVOID)(uintptr_t)outer_field_18, &candidate_vtbl,
                    sizeof(candidate_vtbl), &received)
                    || received != sizeof(candidate_vtbl))
                candidate_vtbl = 0;
            trace_write("OBJECT_CHAIN outer=%08lX outer_plus_18=%08lX "
                    "candidate_vtbl=%08lX ecx=%08lX last_freed_surface=%08lX "
                    "candidate_matches_last_free=%lu",
                    context->Eax, outer_field_18, candidate_vtbl,
                    context->Ecx,
                    (DWORD)InterlockedCompareExchange(
                            &g_trace_last_freed_surface, 0, 0),
                    outer_field_18 == (DWORD)InterlockedCompareExchange(
                            &g_trace_last_freed_surface, 0, 0));
        } else {
            trace_write("OBJECT_CHAIN outer=%08lX +18 read failed got=%lu error=%lu",
                    context->Eax, (DWORD)received, GetLastError());
        }
        trace_memory_block("EAX_OBJECT", context->Eax);
        if (outer_field_18 && outer_field_18 != context->Eax)
            trace_memory_block("FIELD18_OBJECT", outer_field_18);
        if (context->Ecx && context->Ecx != outer_field_18
                && context->Ecx != context->Eax)
            trace_memory_block("ECX_OBJECT", context->Ecx);
        trace_match_address("EAX", context->Eax);
        trace_match_address("FIELD18", outer_field_18);
        if (context->Ecx != outer_field_18)
            trace_match_address("ECX", context->Ecx);

        {
            DWORD frame = context->Ebp;
            for (index = 0; index < 8 && frame; ++index) {
                DWORD pair[2];
                DWORD next;
                received = 0;
                if (frame & 3u || !ReadProcessMemory(process,
                        (LPCVOID)(uintptr_t)frame, pair, sizeof(pair), &received)
                        || received != sizeof(pair))
                    break;
                trace_write("FRAME %lu ebp=%08lX return=%08lX next=%08lX",
                        index, frame, pair[1], pair[0]);
                if (pair[1] >= 16u)
                    trace_memory_block("RETURN_CODE", pair[1] - 16u);
                next = pair[0];
                if (next <= frame || next - frame > 1024u * 1024u)
                    break;
                frame = next;
            }
        }
    }
#endif
    trace_flush();
    InterlockedExchange(&g_trace_in_veh, 0);
    return EXCEPTION_CONTINUE_SEARCH;
}

/*
 * Window-message tracing.
 *
 * D3D9 tracing has taken GTA San Andreas as far as it can: every call the game
 * makes succeeds, and then the process exits with its window already destroyed
 * and the device never released. Whatever ends the game is a window message, so
 * that is what has to be watched next -- WM_CLOSE/WM_DESTROY name what tore the
 * window down, WM_QUIT names what drained the message pump, and
 * WM_ACTIVATEAPP/WM_DISPLAYCHANGE/WM_SIZE would implicate this proxy's own
 * fullscreen setup, which calls ChangeDisplaySettings and SetWindowPos on the
 * app's window from inside CreateDevice.
 *
 * Both hooks are thread-local (this thread, this process) and diagnostic-only:
 * the ordinary d3d9.dll installs nothing.
 */
static HHOOK g_trace_callwnd_hook;
static HHOOK g_trace_getmsg_hook;

static BOOL trace_message_is_interesting(UINT message)
{
    switch (message) {
    case WM_CLOSE:
    case WM_DESTROY:
    case WM_NCDESTROY:
    case WM_QUIT:
    case WM_ACTIVATE:
    case WM_ACTIVATEAPP:
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
    case WM_ENABLE:
    case WM_SHOWWINDOW:
    case WM_SIZE:
    case WM_MOVE:
    case WM_WINDOWPOSCHANGED:
    case WM_DISPLAYCHANGE:
    case WM_SYSCOMMAND:
    case WM_QUERYENDSESSION:
    case WM_ENDSESSION:
        return TRUE;
    default:
        return FALSE;
    }
}

static LRESULT CALLBACK trace_call_wnd_proc(int code, WPARAM wparam,
        LPARAM lparam)
{
    if (code == HC_ACTION && lparam) {
        const CWPSTRUCT *sent = (const CWPSTRUCT *)lparam;
        if (trace_message_is_interesting(sent->message))
            trace_write("MSG sent hwnd=%08lX msg=%04lX wparam=%08lX "
                    "lparam=%08lX", (DWORD)(uintptr_t)sent->hwnd,
                    (DWORD)sent->message, (DWORD)sent->wParam,
                    (DWORD)sent->lParam);
    }
    return CallNextHookEx(g_trace_callwnd_hook, code, wparam, lparam);
}

static LRESULT CALLBACK trace_get_message(int code, WPARAM wparam,
        LPARAM lparam)
{
    /* WM_QUIT only ever arrives through the pump, never through SendMessage,
     * so it is visible here and nowhere else. */
    if (code == HC_ACTION && lparam) {
        const MSG *posted = (const MSG *)lparam;
        if (trace_message_is_interesting(posted->message))
            trace_write("MSG posted hwnd=%08lX msg=%04lX wparam=%08lX "
                    "lparam=%08lX", (DWORD)(uintptr_t)posted->hwnd,
                    (DWORD)posted->message, (DWORD)posted->wParam,
                    (DWORD)posted->lParam);
    }
    return CallNextHookEx(g_trace_getmsg_hook, code, wparam, lparam);
}

static void trace_install_message_hooks(void)
{
    DWORD thread;

    if (g_trace_callwnd_hook || g_trace_getmsg_hook)
        return;
    thread = GetCurrentThreadId();
    g_trace_callwnd_hook = SetWindowsHookExA(WH_CALLWNDPROC,
            trace_call_wnd_proc, NULL, thread);
    g_trace_getmsg_hook = SetWindowsHookExA(WH_GETMESSAGE, trace_get_message,
            NULL, thread);
    trace_write("HOOKS thread=%lu callwnd=%08lX getmsg=%08lX", thread,
            (DWORD)(uintptr_t)g_trace_callwnd_hook,
            (DWORD)(uintptr_t)g_trace_getmsg_hook);
}

static void trace_remove_message_hooks(void)
{
    if (g_trace_callwnd_hook) {
        UnhookWindowsHookEx(g_trace_callwnd_hook);
        g_trace_callwnd_hook = NULL;
    }
    if (g_trace_getmsg_hook) {
        UnhookWindowsHookEx(g_trace_getmsg_hook);
        g_trace_getmsg_hook = NULL;
    }
}

static void trace_close(void)
{
    if (g_trace_file != INVALID_HANDLE_VALUE) {
        trace_flush();
        CloseHandle(g_trace_file);
        g_trace_file = INVALID_HANDLE_VALUE;
    }
}

#define TRACE(...) trace_write(__VA_ARGS__)
#define TRACE_FLUSH() trace_flush()
#define TRACE_MARK_ENTER(method) trace_mark_enter(method)
#define TRACE_MARK_EXIT(method, result, output) \
    trace_mark_exit(method, result, output)
#define TRACE_BUFFER_CREATED(index_buffer, length) \
    trace_buffer_created(index_buffer, length)
#define TRACE_SURFACE_FREED(surface) \
    InterlockedExchange(&g_trace_last_freed_surface, \
            (LONG)(uintptr_t)(surface))
#define TRACE_REGISTER_RANGE(kind, ordinal, object, start, size) \
    trace_register_range(kind, ordinal, object, start, (DWORD)(size))
/*
 * Per-frame draw accounting.
 *
 * Tracing every draw is a firehose -- that is why the command-level trace is
 * left commented out -- but the draw stream was the last part of a running
 * title that no line of this log described at all. One summary per Present
 * costs a line a frame and answers the questions that matter: is the title
 * drawing, how much, and (the reason this exists) is any of it being thrown
 * away. The four Draw* entry points refuse between them at nine places, each
 * a bare D3DERR_INVALIDCALL that leaves no trace, so a title whose geometry is
 * being dropped wholesale looks exactly like one that never drew.
 */
#define TRACE_FRAME_DRAW(primitives) \
    do { \
        InterlockedIncrement(&g_frame_draws); \
        InterlockedExchangeAdd(&g_frame_primitives, (LONG)(primitives)); \
    } while (0)
#define TRACE_FRAME_REJECT() InterlockedIncrement(&g_frame_rejected)
#define TRACE_FRAME_CLEAR() InterlockedIncrement(&g_frame_clears)
/*
 * Wraps a bare `return D3DERR_*` so a refusal leaves a mark. Two rounds of
 * chasing San Andreas rested on "no call in the trace fails" -- but 249 of the
 * 283 error returns in this file could not have appeared if they had. They are
 * not covered by the per-frame reject counter (draw calls only), they emit no
 * FAIL line, and UNSUPPORTED() only covers the deliberate stubs. A validation
 * refusal in Clear, Lock, SetStreamSource or CreateTexture was simply silence.
 */
#define TRACE_REFUSE(hr) \
    (trace_write("REFUSE %s:%d -> %08lX", __func__, (int)__LINE__, \
            (DWORD)(hr)), (hr))
#else
#define TRACE(...) ((void)0)
#define TRACE_FLUSH() ((void)0)
#define TRACE_MARK_ENTER(method) ((void)0)
#define TRACE_MARK_EXIT(method, result, output) ((void)0)
#define TRACE_BUFFER_CREATED(index_buffer, length) ((void)0)
#define TRACE_SURFACE_FREED(surface) ((void)0)
#define TRACE_REGISTER_RANGE(kind, ordinal, object, start, size) ((void)0)
#define TRACE_FRAME_DRAW(primitives) ((void)0)
#define TRACE_FRAME_REJECT() ((void)0)
#define TRACE_FRAME_CLEAR() ((void)0)
#define TRACE_REFUSE(hr) (hr)
#endif

#define D9_MAX_RENDER_STATES 256u
#define D9_MAX_TEXTURE_STAGES 8u
#define D9_MAX_TEXTURE_BINDINGS 20u /* 16 pixel + 4 vs_3_0 vertex samplers */
#define D9_MAX_PALETTES D9WG_PALETTE_COUNT

/*
 * ATI's depth-as-texture FOURCCs, absent from d3d9types.h because they were
 * never part of D3D9 proper -- a driver advertises them through
 * CheckDeviceFormat and an app that sees one renders depth into a texture and
 * samples the stored value back. That is a different operation from binding a
 * D24X8 texture to a sampler, which a D3D9 driver turns into a hardware
 * shadow-map comparison; both are supported here and the host keeps them
 * apart, because sampling one as though it were the other silently produces a
 * plausible-looking wrong image.
 */
#define D9_FOURCC(a, b, c, d) \
    ((D3DFORMAT)((DWORD)(a) | ((DWORD)(b) << 8) | ((DWORD)(c) << 16) \
        | ((DWORD)(d) << 24)))
#define D9FMT_DF16 D9_FOURCC('D', 'F', '1', '6')
#define D9FMT_DF24 D9_FOURCC('D', 'F', '2', '4')
#define D9FMT_INTZ D9_FOURCC('I', 'N', 'T', 'Z')
/* 3DMark05 uses this as a negative PCF capability probe, not a texture. */
#define D9FOURCC_NO_PCF D9_FOURCC('-', 'P', 'C', 'F')
/*
 * ATI's 3Dc compressed formats, which are BC4 and BC5 under their pre-DX10
 * names. A normal map stored as ATI2N keeps only X and Y and the shader
 * reconstructs Z, which is why it beats DXT5 for normals badly enough that
 * 2005-2007 titles ship art in it.
 *
 * WebGPU exposes exactly these as bc4-r-unorm and bc5-rg-unorm under the
 * texture-compression-bc feature, so this is a rename rather than a
 * translation. ATI1/ATI2 are the FOURCCs D3D9 apps actually probe for;
 * '3Dc\0'-style spellings never shipped.
 */
#define D9FMT_ATI1N D9_FOURCC('A', 'T', 'I', '1')
#define D9FMT_ATI2N D9_FOURCC('A', 'T', 'I', '2')
/*
 * A render target that discards everything written to it, so a depth-only pass
 * needs no colour buffer. Apps bind it and render shadow or depth pre-passes
 * against a real depth-stencil surface.
 */
#define D9FMT_NULL D9_FOURCC('N', 'U', 'L', 'L')
/*
 * The two vendor hacks with no faithful mapping, named here so the refusals
 * below can say which one was asked for.
 *
 * RAWZ is NVIDIA's depth-as-texture format for parts that predate INTZ. What
 * it hands the shader is not a depth value but the depth buffer's *bytes*
 * spread across A, R and G, which the app then reassembles with a hardcoded
 * dot product. Serving it from a real depth texture would give the app clean
 * depth in .r and its decode would turn that into nonsense -- worse than a
 * refusal, because the picture would be wrong rather than absent. Every title
 * that probes for RAWZ probes for INTZ first, which is implemented.
 *
 * NVDB is the depth bounds test. WebGPU has no such state at any level, and
 * there is nothing to approximate it with: skipping it draws pixels the app
 * meant to reject.
 */
#define D9FMT_RAWZ D9_FOURCC('R', 'A', 'W', 'Z')
#define D9FOURCC_NVDB D9_FOURCC('N', 'V', 'D', 'B')
#define D9FOURCC_ATOC D9_FOURCC('A', 'T', 'O', 'C')
#define D9_MAX_TEXTURE_STAGE_STATES 33u
/* D3D9's architectural maximum. The host does not bind 16 WebGPU vertex
 * buffers per draw to honour this -- vertexBufferLayoutsFor() builds one layout
 * per stream the *declaration* actually references, which is what WebGPU's
 * maxVertexBuffers bounds, and a slot nothing references costs nothing. 4 was
 * the DX7-era figure and it rejects ordinary skinned formats that split
 * position, weights, normals and two UV sets across separate streams. */
#define D9_MAX_STREAMS 16u
/* The tessellation ceiling shared by the RT patches and N-patches. It bounds
 * the scratch allocation each of them makes: a level of 64 already means 2145
 * vertices per source triangle. */
#define D9_PATCH_MAX_SEGMENTS 64u
#define D9_MAX_TRANSFORMS 512u
#define D9_MAX_LIGHTS 8u
#define D9_MAX_SAMPLERS 20u /* 16 pixel + 4 D3DVERTEXTEXTURESAMPLER slots */
/* fill_caps() reports NumSimultaneousRTs; MRT slots beyond 0 are only bindable
 * because a translated pixel shader can write oC1..oC3. */
#define D9_MAX_RENDER_TARGETS 4u
#define D9_MAX_CLIP_PLANES 6u
#define D9_MAX_SAMPLER_STATES 14u
/* Constant-register file sizes for the shader model fill_caps() reports.
 * vs_2_0 guarantees 256 float constants and ps_2_0 guarantees 32; the 224
 * here is the ps_3_0 ceiling, so the shadow is large enough for any shader
 * the host translator will accept without needing a second size later. */
#define D9_MAX_VS_CONST_F 256u
#define D9_MAX_PS_CONST_F 224u
#define D9_MAX_CONST_I 16u
#define D9_MAX_CONST_B 16u
/* A shader's token stream carries no length: the app hands over a bare
 * pointer and the terminator has to be found by walking it. This bounds that
 * walk so a malformed/non-shader pointer cannot read arbitrarily far past the
 * app's allocation. Real D3D9 shaders are limited to 512 (vs_2_0) / 32768
 * (vs_3_0 with flow control) instruction slots, so this is far above any
 * legitimate input. */
#define D9_MAX_SHADER_TOKENS 65536u

/*
 * Reported adapter identity (see d3d_get_adapter_identifier).
 *
 * This is deliberately a single switchable block because it is a *test
 * variable*, not a settled design decision. Warcraft III was observed
 * calling GetAdapterIdentifier and then releasing IDirect3D9 without ever
 * asking for caps, formats or a device -- which leaves two live
 * explanations: either it gates on recognising the hardware, or it only
 * wanted the video card name and never intended to render through D3D9 at
 * all. Reporting a GPU the game certainly knows discriminates between them:
 * if it still walks away, hardware recognition is definitively not the
 * gate.
 *
 * The identity must stay consistent with what fill_caps() reports, because a
 * game that recognises the card knows what that card can do. M1 paired
 * D9_ADAPTER_GEFORCE4_MX (NV17: hardware T&L, no programmable shaders) with
 * VertexShaderVersion/PixelShaderVersion = 0.0. The default profile now
 * implements and advertises SM3, so it reports the proxy's native adapter
 * identity rather than borrowing an older fixed hardware table whose known
 * limits would contradict the caps returned below.
 *
 * Set D9_ADAPTER_IDENTITY to D9_ADAPTER_NATIVE to go back to advertising
 * ourselves honestly once the question is settled.
 */
#define D9_ADAPTER_NATIVE       0
#define D9_ADAPTER_GEFORCE4_MX  1
#define D9_ADAPTER_VMWARE_SVGA  2
#define D9_ADAPTER_GEFORCEFX_5200 3

#define D9_ADAPTER_IDENTITY D9_ADAPTER_NATIVE
#define D9_HANDLE_GENERATION_ONE (1u << 20)

typedef struct D9Direct3D D9Direct3D;
typedef struct D9Device D9Device;
typedef struct D9SwapChain D9SwapChain;
typedef struct D9VertexBuffer D9VertexBuffer;
typedef struct D9IndexBuffer D9IndexBuffer;
typedef struct D9Texture D9Texture;
typedef struct D9VertexDeclaration D9VertexDeclaration;
typedef struct D9Surface D9Surface;
typedef struct D9Shader D9Shader;
typedef struct D9CubeTexture D9CubeTexture;
typedef struct D9VolumeTexture D9VolumeTexture;
typedef struct D9Volume D9Volume;
typedef struct D9StateBlock D9StateBlock;
typedef struct D9Query D9Query;

typedef struct D9TextureLevel {
    D9WGPrivateStore private_data;
    BYTE *shadow;
    /* The one surface GetSurfaceLevel hands out for this level, created on
     * first request and destroyed with the texture.  D3D9 makes a level
     * surface a sub-object of its texture rather than a free-standing COM
     * object: the same pointer comes back every call, and its refcount is the
     * texture's, so an app may legally drop its surface reference and keep
     * using the pointer for as long as it still holds the texture.  Kart Rider
     * does exactly that -- GetSurfaceLevel, LockRect, Release, UnlockRect --
     * and a per-call surface that frees itself at refcount 0 turns that into a
     * call through a freed vtable. */
    D9Surface *level_surface;
    D9Volume *level_volume;
    /* TRUE only when every pixel in shadow is known. Render targets allocate
     * this lazily for M4 Clear/ColorFill/copy readback and invalidate it on any
     * GPU draw, so GetRenderTargetData can return known content without ever
     * fabricating GPU-produced pixels. */
    BOOL shadow_valid;
    UINT width;
    UINT height;
    UINT depth;
    UINT row_pitch;
    UINT row_count;
    UINT slice_pitch;
    UINT byte_count;
    RECT lock_rect;
    D3DBOX lock_box;
    DWORD lock_flags;
    BOOL locked;
} D9TextureLevel;

typedef struct D9StreamBinding {
    D9VertexBuffer *buffer;
    UINT stride;
    UINT offset; /* SetStreamSource OffsetInBytes */
    DWORD frequency; /* 1, INDEXEDDATA|count, or INSTANCEDATA|step rate */
} D9StreamBinding;

#define D9_STATE_BLOCK_TRANSFORMS D9WG_STATE_BLOCK_TRANSFORMS

struct D9StateBlock {
    IDirect3DStateBlock9 iface;
    LONG refcount;
    BOOL device_parent_ref; /* false for recording and device-owned8 tokens */
    DWORD handle8; /* zero for ordinary9 COM blocks; never a protocol resource */
    D9Device *device;

    BOOL has_render_state[D9_MAX_RENDER_STATES];
    DWORD render_states[D9_MAX_RENDER_STATES];
    BOOL has_texture_stage_state[D9_MAX_TEXTURE_STAGES][D9_MAX_TEXTURE_STAGE_STATES];
    DWORD texture_stage_states[D9_MAX_TEXTURE_STAGES][D9_MAX_TEXTURE_STAGE_STATES];
    BOOL has_sampler_state[D9_MAX_SAMPLERS][D9_MAX_SAMPLER_STATES];
    DWORD sampler_states[D9_MAX_SAMPLERS][D9_MAX_SAMPLER_STATES];
    BOOL has_transform[D9_STATE_BLOCK_TRANSFORMS];
    float transforms[D9_STATE_BLOCK_TRANSFORMS][16];
    BOOL has_texture[D9_MAX_TEXTURE_BINDINGS];
    D9Texture *textures[D9_MAX_TEXTURE_BINDINGS];
    D9CubeTexture *cube_textures[D9_MAX_TEXTURE_BINDINGS];
    D9VolumeTexture *volume_textures[D9_MAX_TEXTURE_BINDINGS];
    BOOL has_palette;
    UINT current_palette;
    BOOL has_material;
    D3DMATERIAL9 material;
    BOOL has_clip_plane[D9_MAX_CLIP_PLANES];
    float clip_planes[D9_MAX_CLIP_PLANES][4];
    BOOL has_npatch_mode;
    float npatch_segments;
    BOOL has_light[D9_MAX_LIGHTS];
    D3DLIGHT9 lights[D9_MAX_LIGHTS];
    BOOL has_light_enable[D9_MAX_LIGHTS];
    BOOL light_enabled[D9_MAX_LIGHTS];
    D9WGLightState *extra_lights;
    BOOL light_allocation_failed;
    BOOL has_viewport;
    D3DVIEWPORT9 viewport;
    BOOL has_scissor;
    RECT scissor_rect;
    BOOL has_vertex_shader;
    D9Shader *vertex_shader;
    BOOL has_pixel_shader;
    D9Shader *pixel_shader;
    BOOL has_vs_const_f[D9_MAX_VS_CONST_F];
    float vs_const_f[D9_MAX_VS_CONST_F][4];
    BOOL has_ps_const_f[D9_MAX_PS_CONST_F];
    float ps_const_f[D9_MAX_PS_CONST_F][4];
    BOOL has_vs_const_i[D9_MAX_CONST_I];
    int vs_const_i[D9_MAX_CONST_I][4];
    BOOL has_ps_const_i[D9_MAX_CONST_I];
    int ps_const_i[D9_MAX_CONST_I][4];
    BOOL has_vs_const_b[D9_MAX_CONST_B];
    BOOL vs_const_b[D9_MAX_CONST_B];
    BOOL has_ps_const_b[D9_MAX_CONST_B];
    BOOL ps_const_b[D9_MAX_CONST_B];
    BOOL has_vertex_format;
    DWORD fvf;
    D9VertexDeclaration *vertex_declaration;
    BOOL has_stream[D9_MAX_STREAMS];
    BOOL has_stream_frequency[D9_MAX_STREAMS];
    D9StreamBinding streams[D9_MAX_STREAMS];
    BOOL has_indices;
    D9IndexBuffer *index_buffer;
    struct D9StateBlock *next_device_resource;
};

struct D9Direct3D {
    IDirect3D9 iface;
    LONG refcount;
    UINT compatibility_api;
};

/*
 * The primary swap chain is created implicitly with the device.  Keep its
 * COM identity embedded in D9Device so repeated GetSwapChain(0) calls return
 * the same object, just as the native runtime does.  The device itself does
 * not own a COM reference to this object (that would form a cycle); every
 * active implicit chain holds one child reference while its public count is
 * nonzero; additional chains own one child reference for their lifetime.
 */
struct D9SwapChain {
    IDirect3DSwapChain9 iface;
    LONG refcount;
    D9Device *device;
    /*
     * Zero for the device's implicit chain, whose back buffer the host
     * addresses as the canvas itself. An additional chain has a real handle and
     * a real render-target texture behind `back_buffer`, which is what lets
     * SetRenderTarget, StretchRect and GetRenderTargetData treat it as an
     * ordinary target with no special cases anywhere but present.
     */
    uint32_t handle;
    HWND window;
    D3DPRESENT_PARAMETERS present;
    IDirect3DSurface9 *back_buffer;
    D9SwapChain *next_device_chain;
    D9WGPresentClock present_clock;
    D9WGPresentPacing present_pacing;
};

struct D9Device {
    IDirect3DDevice9 iface;
    D9SwapChain implicit_swap_chain;
    /* CreateAdditionalSwapChain results, so device teardown and Reset can find
     * them. The implicit chain is not on this list -- it is a member. */
    D9SwapChain *additional_swap_chains;
    LONG refcount;
    LONG child_parent_refs;
    BOOL releasing_owned_refs;
    D9Direct3D *parent;
    uint32_t handle;
    D3DDEVICE_CREATION_PARAMETERS creation;
    D3DPRESENT_PARAMETERS present;
    D3DDISPLAYMODE display_mode;
    D9WGCursorState app_cursor_state;
    D9WGResetState reset_state;
    D3DVIEWPORT9 viewport;
    float transforms[D9_MAX_TRANSFORMS][16];
    DWORD render_states[D9_MAX_RENDER_STATES];
    DWORD texture_stage_states[D9_MAX_TEXTURE_STAGES][D9_MAX_TEXTURE_STAGE_STATES];
    DWORD sampler_states[D9_MAX_SAMPLERS][D9_MAX_SAMPLER_STATES];
    D3DMATERIAL9 material;
    /* The last ramp SetGammaRamp was given, kept so GetGammaRamp answers with
     * it rather than with zeroes. D3D9 has no "no ramp set" state -- a device
     * starts at identity -- so `gamma_ramp_set` records whether the app supplied one;
     * Reset replays that stored ramp and its flags to the new device. */
    D3DGAMMARAMP gamma_ramp;
    BOOL gamma_ramp_set;
    DWORD gamma_ramp_flags;
    /* Only meaningful on a D3DCREATE_MIXED_VERTEXPROCESSING device; the other
     * two modes are fixed at creation and answer from BehaviorFlags. */
    BOOL software_vertex_processing;
    /* SetNPatchMode's level, or 0 when tessellation is off. Zero is the gate
     * the draw path checks, so the default costs one comparison. */
    float npatch_segments;
    D3DLIGHT9 lights[D9_MAX_LIGHTS];
    BOOL light_set[D9_MAX_LIGHTS];
    BOOL light_enabled[D9_MAX_LIGHTS];
    D9WGLightState *extra_lights;
    D9StreamBinding streams[D9_MAX_STREAMS];
    D9IndexBuffer *index_buffer;
    D9Texture *textures[D9_MAX_TEXTURE_BINDINGS];
    /* Parallel to `textures`: a stage holds either a 2D or a cube texture, and
     * exactly one of the two slots is non-NULL. Keeping them apart rather than
     * behind a tagged union keeps the release paths type-correct. */
    D9CubeTexture *cube_bindings[D9_MAX_TEXTURE_BINDINGS];
    D9VolumeTexture *volume_bindings[D9_MAX_TEXTURE_BINDINGS];
    DWORD fvf;
    D9VertexDeclaration *vertex_declaration;
    BOOL in_scene;
    D9VertexBuffer *vertex_buffers;
    D9IndexBuffer *index_buffers;
    D9Texture *texture_resources;
    D9CubeTexture *cube_textures;
    D9VolumeTexture *volume_textures;
    D9VertexDeclaration *vertex_declarations;
    D9Shader *shaders;
    D9StateBlock *state_blocks;
    D9Query *queries;
    /* The unfinished block owns recorded values/references; live state stays
     * unchanged until Apply. Even same-value calls retain their masks. */
    D9StateBlock *recording_result;
    /* Explicitly bound targets. A NULL slot 0 means the implicit back buffer,
     * which is what a device renders to until the app says otherwise. */
    D9Surface *render_target_surfaces[D9_MAX_RENDER_TARGETS];
    D9Surface *depth_stencil_surface;
    BOOL depth_stencil_unbound;
    /*
     * The implicit back buffer and the auto depth-stencil. D3D9 creates both
     * along with the device and hands the *same* object back from every
     * GetBackBuffer / GetDepthStencilSurface call, so they are cached here and
     * freed only in device teardown. Allocating a fresh surface per call
     * instead broke pointer identity -- and worse, releasing that surface freed
     * the block, which the heap then handed straight back out as an unrelated
     * resource while the application was still holding the pointer.
     */
    D9Surface *implicit_back_buffer;
    D9Surface *implicit_depth_stencil;
    RECT scissor_rect;
    BOOL scissor_set;
    float clip_planes[D9_MAX_CLIP_PLANES][4];
    D9WGClipStatus clip_status;
    BOOL clip_status_valid;
    D9Shader *vertex_shader;
    D9Shader *pixel_shader;
    BOOL cursor_ready;
    BOOL cursor_visible;
    /* Set once the application drives the D3D9 hardware cursor itself, which
     * takes precedence over the GDI capture fallback. */
    BOOL app_cursor;
    HCURSOR system_cursor;
    BOOL display_mode_changed;
    D9WGWindowCache window_cache;
    D9WGWindowWatch window_watch;
    /* Saved across the borderless override an exclusive-fullscreen device
     * applies to its window, so the app's own frame comes back on teardown. */
    HWND styled_window;
    LONG saved_window_style;
    LONG saved_window_exstyle;
    D9WGPresentClock present_clock;
    D9WGPresentPacing present_pacing;
    /* Rate limiting for maintain_fullscreen_foreground(). */
    DWORD last_foreground_claim;
    DWORD foreground_claims;
    /* The window emit_window_state() last reported, which the window-event
     * hook reports again when anything on the screen changes. */
    HWND state_window;
    /*
     * D3D9's constant registers are device state, not shader state: they
     * survive SetVertexShader and are what the app expects to still be there
     * after a Reset. Holding the authoritative copy here (rather than only on
     * the host) is what lets set_shader_constant_f() suppress the very common
     * "set the same matrix palette again every frame" traffic and lets
     * emit_device_state_after_reset() replay the live values after a Reset.
     */
    float vs_const_f[D9_MAX_VS_CONST_F][4];
    int vs_const_i[D9_MAX_CONST_I][4];
    BOOL vs_const_b[D9_MAX_CONST_B];
    float ps_const_f[D9_MAX_PS_CONST_F][4];
    int ps_const_i[D9_MAX_CONST_I][4];
    BOOL ps_const_b[D9_MAX_CONST_B];
    D9WGPaletteStore palettes;
    UINT current_palette;
    uint32_t reset_epoch;
};

/*
 * IDirect3DVertexShader9 and IDirect3DPixelShader9 have byte-for-byte
 * identical vtable layouts (IUnknown + GetDevice + GetFunction), so one
 * struct backs both; only `is_pixel` and which vtable pointer is installed
 * differ. The guest keeps the raw token stream and never interprets it
 * beyond counting tokens and hashing (plan 4.2) -- all translation happens
 * host-side in d3d9_shader_pipeline.js.
 */
struct D9Shader {
    union {
        IDirect3DVertexShader9 vertex;
        IDirect3DPixelShader9 pixel;
    } iface;
    LONG refcount;
    LONG binding_refs;          /* private declaration/shader slot holds */
    D9Device *device;
    uint32_t handle;
    BOOL is_pixel;
    DWORD *code;
    UINT token_count;
    uint32_t hash_low;
    uint32_t hash_high;
    D9Shader *next_device_resource;
};

struct D9VertexBuffer {
    IDirect3DVertexBuffer9 iface;
    LONG refcount;
    LONG binding_refs;          /* private stream/index storage holds */
    D9Device *device;
    uint32_t handle;
    BYTE *shadow;
    D9WGClipBuffer clips;
    UINT length;
    DWORD usage;
    DWORD fvf;
    D3DPOOL pool;
    DWORD priority;
    D9WGPrivateStore private_data;
    UINT lock_offset;
    UINT lock_size;
    DWORD lock_flags;
    BOOL locked;
    D9VertexBuffer *next_device_resource;
};

struct D9IndexBuffer {
    IDirect3DIndexBuffer9 iface;
    LONG refcount;
    LONG binding_refs;          /* private stream/index storage holds */
    D9Device *device;
    uint32_t handle;
    BYTE *shadow;
    UINT length;
    DWORD usage;
    D3DFORMAT format;
    D3DPOOL pool;
    DWORD priority;
    D9WGPrivateStore private_data;
    UINT lock_offset;
    UINT lock_size;
    DWORD lock_flags;
    BOOL locked;
    D9IndexBuffer *next_device_resource;
};

struct D9Texture {
    IDirect3DTexture9 iface;
    LONG refcount;
    LONG binding_refs;          /* sampler/target holds do not expose COM refs */
    D9Device *device;
    uint32_t handle;
    UINT width;
    UINT height;
    UINT level_count;
    DWORD usage;
    D3DFORMAT format;
    D3DPOOL pool;
    D3DMULTISAMPLE_TYPE multisample;
    DWORD multisample_quality;
    DWORD priority;
    DWORD lod;
    D9WGPrivateStore private_data;
    D9TextureLevel *levels;
    D9Texture *next_device_resource;
    /* Private surface/backbuffer storage: its public owner holds the device pin. */
    BOOL private_back_buffer;
    /* D3DUSAGE_AUTOGENMIPMAP only: the filter the app asked the driver to
     * downsample with. Zero means it never said, which D3D9 reports as
     * D3DTEXF_LINEAR. */
    D3DTEXTUREFILTERTYPE autogen_filter;
};

struct D9CubeTexture {
    IDirect3DCubeTexture9 iface;
    LONG refcount;
    LONG binding_refs;          /* sampler and cube-face target storage holds */
    D9Device *device;
    uint32_t handle;
    UINT edge_length;
    UINT level_count;
    DWORD usage;
    D3DFORMAT format;
    D3DPOOL pool;
    DWORD priority;
    DWORD lod;
    D9WGPrivateStore private_data;
    D9TextureLevel *levels; /* face * level_count + level */
    D9CubeTexture *next_device_resource;
    /* D3DUSAGE_AUTOGENMIPMAP only: the filter the app asked the driver to
     * downsample with. Zero means it never said, which D3D9 reports as
     * D3DTEXF_LINEAR. */
    D3DTEXTUREFILTERTYPE autogen_filter;
};

struct D9VolumeTexture {
    IDirect3DVolumeTexture9 iface;
    LONG refcount;
    LONG binding_refs;          /* private sampler storage holds */
    D9Device *device;
    uint32_t handle;
    UINT width;
    UINT height;
    UINT depth;
    UINT level_count;
    DWORD usage;
    D3DFORMAT format;
    D3DPOOL pool;
    DWORD priority;
    DWORD lod;
    D9WGPrivateStore private_data;
    D9TextureLevel *levels;
    D9VolumeTexture *next_device_resource;
};

/* The COM conversion helpers cast interface pointers to these objects. */
typedef char D9WGTextureInterfaceFirst[
    offsetof(D9Texture, iface) == 0 && offsetof(D9CubeTexture, iface) == 0
            && offsetof(D9VolumeTexture, iface) == 0 ? 1 : -1];

struct D9Volume {
    IDirect3DVolume9 iface;
    D9VolumeTexture *texture;
    UINT level;
};

static IDirect3DCubeTexture9Vtbl g_cube_vtbl;
static IDirect3DVolumeTexture9Vtbl g_volume_texture_vtbl;
static IDirect3DVolume9Vtbl g_volume_vtbl;

/* D3DMAXDECLLENGTH (18) bounds the app-supplied element array; +0 is enough
 * since we never append our own sentinel back into this array. */
struct D9VertexDeclaration {
    IDirect3DVertexDeclaration9 iface;
    LONG refcount;
    LONG binding_refs;          /* private declaration/shader slot holds */
    D9Device *device;
    uint32_t handle;
    UINT element_count;
    DWORD fvf;                 /* native association, also for POSITION-only user views */
    DWORD legacy_fvf8;         /* private8 cache key; never interpreted as an API9 FVF */
    BOOL fvf_converted;         /* cache origin is distinct from an associated user FVF */
    BOOL fvf_cache_ref;         /* device cache owns one private storage hold */
    D3DVERTEXELEMENT9 elements[D3DMAXDECLLENGTH];
    D9VertexDeclaration *next_device_resource;
};

/* Surface views returned for texture levels, render targets, and the implicit
 * swap-chain buffers. The implicit back buffer has no ordinary texture handle
 * (wire handle zero names the canvas), but GetRenderTargetData reads it from
 * the executor's persistent post-Present GPU snapshot. LockRect remains
 * invalid for default-pool render targets, as it is on native D3D9. */
struct D9Surface {
    IDirect3DSurface9 iface;
    LONG refcount;
    /* Standalone storage can remain bound after its public count reaches zero. */
    LONG binding_refs;
    D9WGPrivateStore private_data;
    D9Device *device;
    /* Non-NULL for a texture-level or standalone render-target surface. The
     * surface is a view onto that texture level; LockRect/UnlockRect share its
     * shadow and upload path when the resource's pool/usage permits locking.
     * NULL for implicit swap-chain surfaces, represented by wire handle zero. */
    D9Texture *texture;
    /* Cube face/level surfaces use the same stable child-object lifetime as
     * 2D texture surfaces, but forward LockRect and COM references to the cube
     * parent.  `cube_face` is valid whenever cube_texture is non-NULL. */
    D9CubeTexture *cube_texture;
    UINT cube_face;
    /* TRUE when this surface is one of the texture's level sub-objects, held
     * in D9TextureLevel::level_surface and living exactly as long as the
     * texture.  Its AddRef/Release forward to the texture and it is freed only
     * during texture teardown.  FALSE for the surface create_target_texture
     * builds, which points at a texture too but owns it the other way round:
     * that surface holds the texture's only reference and takes its own
     * refcount, so forwarding there would be a cycle. */
    BOOL texture_child;
    /* IDirect3DSurface9::GetDC state. The DC is backed by a DIB section --
     * the only bitmap GDI both draws into and exposes a raw pointer for -- so
     * the surface's bits live in two places while a DC is out, and `dc_bits` /
     * `dc_pitch` remember where the surface's own storage was so ReleaseDC can
     * copy back. The surface stays LockRect-locked for that whole window,
     * which is what D3D9 specifies. */
    HDC dc;
    HBITMAP dib;
    HBITMAP dc_previous;
    void *dib_bits;
    void *dc_bits;
    UINT dib_pitch;
    UINT dc_pitch;
    UINT dc_pixel_bytes;
    /* Non-NULL for the implicit back buffer.  This is a non-owning pointer:
     * the surface's tracked device reference already keeps the embedded swap
     * chain alive, while GetContainer takes a fresh public chain reference. */
    D9SwapChain *swap_chain;
    UINT level;
    UINT width;
    UINT height;
    D3DFORMAT format;
    D3DPOOL pool;
    D3DMULTISAMPLE_TYPE multisample;
    DWORD multisample_quality;
    /* Non-NULL for a standalone CPU surface from
     * CreateOffscreenPlainSurface: it owns its pixels and has no GPU resource
     * behind it at all. That is exactly what a cursor bitmap is -- the app
     * builds one, fills it, hands it to SetCursorProperties, and the surface
     * itself is never rendered with. */
    BYTE *shadow;
    UINT row_pitch;
    UINT byte_count;
    BOOL locked;
    RECT lock_rect;
    DWORD lock_flags;
    BOOL lockable_render_target;
    /* TRUE for the surface GetDepthStencilSurface hands back for the device's
     * implicit auto depth-stencil. It has no texture and no pixels; its only
     * job is to be passed back to SetDepthStencilSurface, which is how an app
     * restores depth after a render-to-texture pass. */
    BOOL auto_depth_stencil;
};

/*
 * TRUE for the two surfaces the device owns outright rather than creating on
 * demand: the implicit back buffer and the auto depth-stencil. Each of the two
 * fields tested here is written by exactly one place, so the pair already
 * identifies them and no extra flag is needed.
 *
 * The surface has its own public count. Only its zero/nonzero transitions
 * retain/release the device; the cached wrapper survives a zero public count
 * until device teardown.
 */
static BOOL surface_is_implicit(const D9Surface *surface)
{
    return (surface->swap_chain && !surface->swap_chain->handle)
        || surface->auto_depth_stencil;
}

static void core_view_retire8(void *object);
static void texture_destroy_storage(D9Texture *texture);

static void texture_binding_add_ref(D9Texture *texture)
{
    InterlockedIncrement(&texture->binding_refs);
}

static void texture_binding_release(D9Texture *texture)
{
    if (!InterlockedDecrement(&texture->binding_refs)
            && !InterlockedCompareExchange(&texture->refcount, 0, 0))
        texture_destroy_storage(texture);
}

static void cube_destroy_storage(D9CubeTexture *texture);

static void cube_binding_add_ref(D9CubeTexture *texture)
{
    InterlockedIncrement(&texture->binding_refs);
}

static void cube_binding_release(D9CubeTexture *texture)
{
    if (!InterlockedDecrement(&texture->binding_refs)
            && !InterlockedCompareExchange(&texture->refcount, 0, 0))
        cube_destroy_storage(texture);
}

static void volume_texture_destroy_storage(D9VolumeTexture *texture);

static void volume_texture_binding_add_ref(D9VolumeTexture *texture)
{
    InterlockedIncrement(&texture->binding_refs);
}

static void volume_texture_binding_release(D9VolumeTexture *texture)
{
    if (!InterlockedDecrement(&texture->binding_refs)
            && !InterlockedCompareExchange(&texture->refcount, 0, 0))
        volume_texture_destroy_storage(texture);
}

static void vb_destroy_storage(D9VertexBuffer *buffer);

static void vb_binding_add_ref(D9VertexBuffer *buffer)
{
    InterlockedIncrement(&buffer->binding_refs);
}

static void vb_binding_release(D9VertexBuffer *buffer)
{
    if (!InterlockedDecrement(&buffer->binding_refs)
            && !InterlockedCompareExchange(&buffer->refcount, 0, 0))
        vb_destroy_storage(buffer);
}

static void ib_destroy_storage(D9IndexBuffer *buffer);

static void ib_binding_add_ref(D9IndexBuffer *buffer)
{
    InterlockedIncrement(&buffer->binding_refs);
}

static void ib_binding_release(D9IndexBuffer *buffer)
{
    if (!InterlockedDecrement(&buffer->binding_refs)
            && !InterlockedCompareExchange(&buffer->refcount, 0, 0))
        ib_destroy_storage(buffer);
}

static void decl_destroy_storage(D9VertexDeclaration *object);

static void decl_binding_add_ref(D9VertexDeclaration *object)
{
    InterlockedIncrement(&object->binding_refs);
}

static void decl_binding_release(D9VertexDeclaration *object)
{
    if (!InterlockedDecrement(&object->binding_refs)
            && !InterlockedCompareExchange(&object->refcount, 0, 0))
        decl_destroy_storage(object);
}

/* Clear only cache holds: public views and captured bindings retain storage.
 * Detach each cache ownership flag before its private release. */
static void device_clear_fvf_cache(D9Device *device)
{
    D9VertexDeclaration *decl = device->vertex_declarations;
    while (decl) {
        D9VertexDeclaration *next = decl->next_device_resource;
        if (decl->fvf_cache_ref) {
            decl->fvf_cache_ref = FALSE;
            decl_binding_release(decl);
        }
        decl = next;
    }
}

static void shader_destroy_storage(D9Shader *object);

static void shader_binding_add_ref(D9Shader *object)
{
    InterlockedIncrement(&object->binding_refs);
}

static void shader_binding_release(D9Shader *object)
{
    if (!InterlockedDecrement(&object->binding_refs)
            && !InterlockedCompareExchange(&object->refcount, 0, 0))
        shader_destroy_storage(object);
}

static void surface_destroy_storage(D9Surface *surface);
static void swap_chain_destroy_storage(D9SwapChain *chain);

static BOOL surface_uses_private_bindings(const D9Surface *surface)
{
    return !surface->texture_child && !surface->cube_texture;
}

static void surface_binding_add_ref(D9Surface *surface)
{
    if (surface->cube_texture)
        cube_binding_add_ref(surface->cube_texture);
    else if (surface->texture_child)
        texture_binding_add_ref(surface->texture);
    else if (surface_uses_private_bindings(surface))
        InterlockedIncrement(&surface->binding_refs);
    else
        IDirect3DSurface9_AddRef(&surface->iface);
}

static void surface_binding_release(D9Surface *surface)
{
    if (surface->cube_texture) {
        cube_binding_release(surface->cube_texture);
        return;
    }
    if (surface->texture_child) {
        texture_binding_release(surface->texture);
        return;
    }
    if (!surface_uses_private_bindings(surface)) {
        IDirect3DSurface9_Release(&surface->iface);
        return;
    }
    if (!InterlockedDecrement(&surface->binding_refs)
            && !InterlockedCompareExchange(&surface->refcount, 0, 0)
            && !surface_is_implicit(surface)) {
        if (surface->swap_chain) {
            D9SwapChain *chain = surface->swap_chain;
            if (!InterlockedCompareExchange(&chain->refcount, 0, 0))
                swap_chain_destroy_storage(chain);
        } else {
            surface_destroy_storage(surface);
        }
    }
}

/* BeginStateBlock records calls, including calls which write the value the
 * device already has.  The block structure itself lives near its COM methods;
 * these helpers keep its layout out of the ordinary Set* implementations. */
static BOOL state_block_record_render_state(D9Device *device, UINT state, DWORD value);
static BOOL state_block_record_texture_stage_state(D9Device *device, UINT stage, UINT state, DWORD value);
static BOOL state_block_record_sampler_state(D9Device *device, UINT sampler, UINT state, DWORD value);
static const float *state_block_transform_source(D9Device *device, UINT state);
static BOOL state_block_record_transform(D9Device *device, UINT state, const D3DMATRIX *matrix);
static BOOL state_block_record_palette(D9Device *device, UINT index);
static BOOL state_block_record_material(D9Device *device, const D3DMATERIAL9 *material);
static BOOL state_block_record_clip_plane(D9Device *device, UINT index, const float *plane);
static BOOL state_block_record_npatch_mode(D9Device *device, float segments);
static BOOL state_block_record_stream_frequency(D9Device *device, UINT stream, DWORD divider);
static BOOL state_block_record_viewport(D9Device *device, const D3DVIEWPORT9 *viewport);
static BOOL state_block_record_scissor(D9Device *device, const RECT *rect);
static BOOL state_block_record_constant(D9Device *device, BOOL is_pixel,
        BOOL is_float, BOOL is_bool, UINT start, UINT count, const void *data);

static IDirect3D9Vtbl g_d3d_vtbl;
static IDirect3DDevice9Vtbl g_device_vtbl;
static IDirect3DSwapChain9Vtbl g_swap_chain_vtbl;
static IDirect3DVertexBuffer9Vtbl g_vb_vtbl;
static IDirect3DIndexBuffer9Vtbl g_ib_vtbl;
static IDirect3DTexture9Vtbl g_texture_vtbl;
static IDirect3DCubeTexture9Vtbl g_cube_vtbl;
static IDirect3DVolumeTexture9Vtbl g_volume_texture_vtbl;
static IDirect3DVolume9Vtbl g_volume_vtbl;
static IDirect3DVertexDeclaration9Vtbl g_decl_vtbl;
static IDirect3DSurface9Vtbl g_surface_vtbl;
static IDirect3DVertexShader9Vtbl g_vertex_shader_vtbl;
static IDirect3DPixelShader9Vtbl g_pixel_shader_vtbl;

static void device_clear_bindings(D9Device *device);
static BOOL device_has_reset_blockers(D9Device *device);
static void device_release_owned_references(D9Device *device);
static void device_clear_state_block_handles8(D9Device *device);
static BOOL emit_device_state_after_reset(D9Device *device);
static BOOL emit_cube_texture_create(D9Device *device, D9CubeTexture *texture);
static BOOL emit_cube_texture_update(D9CubeTexture *texture, UINT face,
        UINT level, const RECT *rect);
static BOOL emit_volume_texture_create(D9Device *device,
        D9VolumeTexture *texture);
static BOOL emit_volume_texture_update(D9VolumeTexture *texture, UINT level,
        const D3DBOX *box);
static void device_child_add_ref(D9Device *device);
static void device_child_release(D9Device *device);
static UINT shader_model_version(void);
static D9Surface *surface_from_iface(IDirect3DSurface9 *iface)
{
    return (D9Surface *)iface;
}
static void emit_cursor_position(D9Device *device, int x, int y, DWORD flags);
static void update_system_cursor(D9Device *device, HWND window);
static BOOL emit_window_state(D9Device *device, HWND window);
static void watch_window_state(D9Device *device);
static void unwatch_window_state(D9Device *device);
static BOOL reset_window_mode(D9Device *device, HWND window);
static BOOL maintain_fullscreen_foreground(D9Device *device, HWND window,
        const D9WGWindowProbe *probe);
static BOOL restore_display_mode(D9Device *device);

static D9Direct3D *d3d_from_iface(IDirect3D9 *iface)
{
    return (D9Direct3D *)iface;
}

static D9Device *device_from_iface(IDirect3DDevice9 *iface)
{
    return (D9Device *)iface;
}

static UINT device_compatibility_api(const D9Device *device)
{
    return device->parent->compatibility_api == 8 ? 8u : 9u;
}

static D9SwapChain *swap_chain_from_iface(IDirect3DSwapChain9 *iface)
{
    return (D9SwapChain *)iface;
}

static D9VertexBuffer *vb_from_iface(IDirect3DVertexBuffer9 *iface)
{
    return (D9VertexBuffer *)iface;
}

static D9IndexBuffer *ib_from_iface(IDirect3DIndexBuffer9 *iface)
{
    return (D9IndexBuffer *)iface;
}

static D9Texture *texture_from_iface(IDirect3DTexture9 *iface)
{
    return (D9Texture *)iface;
}

static D9VertexDeclaration *decl_from_iface(IDirect3DVertexDeclaration9 *iface)
{
    return (D9VertexDeclaration *)iface;
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

static HINSTANCE g_module_instance;

/*
 * D9WG_DUMP_SHADERS=1: write every shader's raw token stream to disk, next to
 * this DLL, named by its bytecode hash.
 *
 * The point is corpus, not diagnostics. The translator in
 * d3d9_shader_pipeline.js is hand-written, so its only correctness evidence is
 * this repository's own tests -- and those run on shaders *we* assembled by
 * hand, which is exactly the wrong sample: hand-written test bytecode contains
 * the instruction encodings we already thought of. A real game's shaders
 * contain the ones we did not.
 *
 * Every D3D9 game is a corpus contributor whether or not it is playable here:
 * a client that only reaches its main menu before wedging still creates its
 * full shader set during startup, so a few minutes in v86 yields a permanent
 * regression corpus that can be replayed offline through
 * d3d9_shader_pipeline_test.js and naga without launching anything.
 *
 * Files are named d3d9_dump/{vs,ps}_<major><minor>_<hash16>.d9sh and are pure
 * DWORD token streams (no header), the exact bytes CreateVertexShader received,
 * so the offline harness can feed them straight to compileShader(). Naming by
 * content hash makes the write idempotent: a game that re-creates the same
 * shader every level load writes one file.
 */
static BOOL g_dump_shaders_checked;
static BOOL g_dump_shaders_enabled;
static char g_dump_shaders_directory[MAX_PATH];

/* Fills g_dump_shaders_directory with "<dll dir>\d3d9_dump\" and creates it.
 * It is anchored to the module because a game may SetCurrentDirectory during
 * startup, and a dump nobody can find is worse than no dump. */
static BOOL dump_shaders_enabled(void)
{
    char value[8];
    DWORD length;
    DWORD index;

    if (g_dump_shaders_checked)
        return g_dump_shaders_enabled;
    g_dump_shaders_checked = TRUE;
    length = GetEnvironmentVariableA("D9WG_DUMP_SHADERS", value, sizeof(value));
    if (length != 1 || value[0] != '1')
        return FALSE;

    length = GetModuleFileNameA(g_module_instance, g_dump_shaders_directory,
            sizeof(g_dump_shaders_directory) - 16);
    if (!length || length >= sizeof(g_dump_shaders_directory) - 16)
        return FALSE;
    for (index = length; index > 0; --index) {
        if (g_dump_shaders_directory[index - 1] == '\\'
                || g_dump_shaders_directory[index - 1] == '/')
            break;
    }
    lstrcpynA(g_dump_shaders_directory + index, "d3d9_dump\\",
            (int)(sizeof(g_dump_shaders_directory) - index));
    /* Strip the trailing separator for CreateDirectoryA, then keep it for the
     * per-file path concatenation below. */
    g_dump_shaders_directory[index + 9] = '\0';
    if (!CreateDirectoryA(g_dump_shaders_directory, NULL)
            && GetLastError() != ERROR_ALREADY_EXISTS)
        return FALSE;
    g_dump_shaders_directory[index + 9] = '\\';
    g_dump_shaders_directory[index + 10] = '\0';
    g_dump_shaders_enabled = TRUE;
    return TRUE;
}

static void dump_shader_bytecode(const DWORD *code, UINT token_count,
        BOOL is_pixel, uint32_t hash_low, uint32_t hash_high)
{
    char path[MAX_PATH];
    char name[64];
    HANDLE file;
    DWORD written;

    if (!dump_shaders_enabled())
        return;
    wsprintfA(name, "%s_%lu%lu_%08lX%08lX.d9sh", is_pixel ? "ps" : "vs",
            (unsigned long)((code[0] >> 8) & 0xFFu),
            (unsigned long)(code[0] & 0xFFu),
            (unsigned long)hash_high, (unsigned long)hash_low);
    if (lstrlenA(g_dump_shaders_directory) + lstrlenA(name) >= (int)sizeof(path))
        return;
    lstrcpynA(path, g_dump_shaders_directory, sizeof(path));
    lstrcatA(path, name);
    /* CREATE_NEW rather than CREATE_ALWAYS: the name is a content hash, so an
     * existing file already holds these exact bytes and rewriting it every
     * time the game re-creates the shader is pure I/O in a startup path. */
    file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE)
        return;
    WriteFile(file, code, token_count * 4u, &written, NULL);
    CloseHandle(file);
}

/* A frontend8 view is weak with respect to its real9 object. The caller
 * owns an ordinary resource reference while looking up/installing the view.
 * Its metadata lasts until actual storage retirement, including public zero.
 * Tombstones remain linked during callbacks so retirement cannot reattach a
 * view to the same dying object. Callbacks run outside the transport lock. */
typedef struct D9CoreView8 {
    struct D9CoreView8 *next;
    void *object;
    void *view;
    D9WGCoreViewRetire8 retire;
    BOOL retiring;
} D9CoreView8;
static D9CoreView8 *g_core_views8;

static D9Device *core_view_device8(IUnknown *object)
{
    if (!object) return NULL;
    const void *vtbl = object->lpVtbl;
    D9Device *device = NULL;
    if (vtbl == &g_device_vtbl) device = (D9Device *)object;
    else if (vtbl == &g_vb_vtbl) device = ((D9VertexBuffer *)object)->device;
    else if (vtbl == &g_ib_vtbl) device = ((D9IndexBuffer *)object)->device;
    else if (vtbl == &g_texture_vtbl) device = ((D9Texture *)object)->device;
    else if (vtbl == &g_cube_vtbl) device = ((D9CubeTexture *)object)->device;
    else if (vtbl == &g_volume_texture_vtbl) device = ((D9VolumeTexture *)object)->device;
    else if (vtbl == &g_volume_vtbl) device = ((D9Volume *)object)->texture->device;
    else if (vtbl == &g_surface_vtbl) device = ((D9Surface *)object)->device;
    else if (vtbl == &g_swap_chain_vtbl) {
        D9SwapChain *chain = (D9SwapChain *)object;
        /* D3D8 only exposes additional chains;its implicit surfaces use the
         * frontend device as their container. */
        if (chain->handle) device = chain->device;
    }
    return device && device_compatibility_api(device) == 8u ? device : NULL;
}

/* Called only while holding g_transport_lock. */
static D9CoreView8 **core_view_link8(void *object)
{
    D9CoreView8 **link = &g_core_views8;
    while (*link && (*link)->object != object) link = &(*link)->next;
    return link;
}

HRESULT WINAPI d9wg_d3d9_core_get_view8(IUnknown *object, void **out)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = NULL;
    if (!core_view_device8(object)) return D3DERR_INVALIDCALL;
    EnterCriticalSection(&g_transport_lock);
    D9CoreView8 *entry = *core_view_link8(object);
    HRESULT hr = entry && entry->retiring ? D3DERR_INVALIDCALL : D3D_OK;
    if (entry && !entry->retiring) *out = entry->view;
    LeaveCriticalSection(&g_transport_lock);
    return hr;
}

HRESULT WINAPI d9wg_d3d9_core_attach_view8(IUnknown *object, void *view,
        D9WGCoreViewRetire8 retire)
{
    if (!view || !retire || !core_view_device8(object)) return D3DERR_INVALIDCALL;
    EnterCriticalSection(&g_transport_lock);
    D9CoreView8 **link = core_view_link8(object);
    if (*link) {
        LeaveCriticalSection(&g_transport_lock);
        return D3DERR_INVALIDCALL;
    }
    D9CoreView8 *entry = (D9CoreView8 *)HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY, sizeof(*entry));
    if (!entry) {
        LeaveCriticalSection(&g_transport_lock);
        return E_OUTOFMEMORY;
    }
    entry->object = object;
    entry->view = view;
    entry->retire = retire;
    *link = entry;
    LeaveCriticalSection(&g_transport_lock);
    return D3D_OK;
}

static void core_view_retire8(void *object)
{
    if (!object) return;
    EnterCriticalSection(&g_transport_lock);
    D9CoreView8 *entry = *core_view_link8(object);
    if (!entry || entry->retiring) {
        LeaveCriticalSection(&g_transport_lock);
        return;
    }
    entry->retiring = TRUE;
    LeaveCriticalSection(&g_transport_lock);
    entry->retire(entry->view);
    EnterCriticalSection(&g_transport_lock);
    /* Another object's callback may have changed adjacent links. */
    D9CoreView8 **link = core_view_link8(object);
    if (*link == entry) *link = entry->next;
    LeaveCriticalSection(&g_transport_lock);
    HeapFree(GetProcessHeap(), 0, entry);
}

static uint32_t allocate_handle(void)
{
    uint32_t handle = (uint32_t)InterlockedIncrement((LONG *)&g_next_handle);
    if (!handle)
        handle = (uint32_t)InterlockedIncrement((LONG *)&g_next_handle);
    return handle;
}

/*
 * Shaders draw from a disjoint numeric range with bit 0 always set (see
 * D9WG_SHADER_HANDLE_BASE in d3d9_protocol.h), stepping by two so that
 * property holds for every handle. The host keeps one flat resource table,
 * so this is what guarantees a shader handle can never alias a buffer,
 * texture or declaration handle allocated by allocate_handle().
 */

static uint32_t allocate_shader_handle(void)
{
    return (uint32_t)InterlockedExchangeAdd((LONG *)&g_next_shader_handle, 2);
}

/* ---- VGL2 transport / D9WG batch buffer ---- */

/*
 * A transport failure is never permanent (review S1-3): the renderer may not
 * be up yet, may be behind, or the page may have been reloaded. Opening is
 * retried after D9WG_RUNTIME_RETRY_MS; a batch the host refuses is dropped
 * and counted, and the mapping -- with it the host's session, which ends when
 * the arena is released -- is kept.
 */
static BOOL g_hello_emitted;

static uint8_t *batch_base(void)
{
    return d9wg_transport_batch(g_process);
}

static uint32_t batch_capacity(void)
{
    return d9wg_transport_capacity(g_process);
}

static uint8_t *response_region(void)
{
    return d9wg_transport_response(g_process);
}

static LONG next_response_request_id(void)
{
    LONG id = InterlockedIncrement(&g_response_request_id);
    if (!id) id = InterlockedIncrement(&g_response_request_id);
    return id;
}

static int allocate_query_slot(void)
{
    UINT index;
    for (index = 0; index < D9WG_QUERY_SLOT_COUNT; ++index) {
        if (InterlockedCompareExchange((LONG *)&g_query_slots[index], 1, 0) == 0)
            return (int)index;
    }
    return -1;
}

static void free_query_slot(UINT index)
{
    if (index < D9WG_QUERY_SLOT_COUNT)
        InterlockedExchange((LONG *)&g_query_slots[index], 0);
}

static void close_transport_locked(void)
{
    d9wg_transport_close(g_process);
}

static BOOL open_transport_locked(void)
{
    BOOL attempt = !g_dma_buffer && d9wg_retry_ready(GetTickCount(), g_transport_retry_tick, g_transport_retry_armed);
    BOOL result = d9wg_transport_open(g_process);
    if (!result && attempt) {
        TRACE("TRANSPORT open/map failed error=%lu; retrying in %lums", GetLastError(), (DWORD)D9WG_RUNTIME_RETRY_MS);
    }
    return result;
}

static BOOL submit_batch_locked(BOOL present)
{
    BOOL result;
    if (!open_transport_locked()) return FALSE;
    result = d9wg_transport_submit(g_process, present);
    if (!result) {
        TRACE("SUBMIT failed error=%lu; batch dropped (total=%lu)", GetLastError(), g_batches_dropped);
    }
    /* A dropped batch must not stop later commands in the D3D stream. */
    return TRUE;
}

static BOOL reserve_command_locked(uint16_t opcode, uint32_t payload_bytes,
        uint32_t extra_bytes, D9WGCommandHeader **command_out,
        uint8_t **payload_out, uint8_t **extra_out)
{
    if (!open_transport_locked()) return FALSE;
    return d9wg_transport_reserve(g_process, opcode, payload_bytes, extra_bytes,
            command_out, payload_out, extra_out, submit_batch_locked);
}

static BOOL emit_command(uint16_t opcode, const void *payload,
        uint32_t payload_bytes)
{
    uint8_t *destination;
    BOOL result;

    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(opcode, payload_bytes, 0, NULL,
            &destination, NULL);
    if (result && payload_bytes)
        CopyMemory(destination, payload, payload_bytes);
    LeaveCriticalSection(&g_transport_lock);
    return result;
}

#include "../runtime/d9wg_state_defaults_bridge.h"

/*
 * Reports a refusal or failure to the host so it lands in the browser console.
 *
 * This exists because the guest's own trace file is written inside a VM whose
 * filesystem the developer cannot reach from the page, so until now the one
 * fact that ends a "the picture is wrong" investigation -- the app asked for
 * something and was told no -- was recorded only where nobody could read it.
 * Compiled into the ordinary DLL, not just the diagnostic one: needing a
 * special build to find out that a call was refused is most of the problem.
 *
 * Deduplicated by exact text. A failure inside a per-frame path would otherwise
 * bill one message per frame forever, which costs batch space and drowns the
 * console; the first occurrence is the informative one. The table is small and
 * never grows past its cap, so a pathological caller cannot leak through it.
 */
/* Bumped whenever guest-visible behaviour changes, so the console can say
 * which DLL is actually loaded rather than leaving it to be inferred. */
#define D9_PROXY_BUILD "negative-pcf-probe-20260906"

#define D9_HOSTLOG_MAX_DISTINCT 128u

static char g_hostlog_seen[D9_HOSTLOG_MAX_DISTINCT][D9WG_LOG_MAX_TEXT];
static UINT g_hostlog_seen_count;
static BOOL g_hostlog_overflowed;

static BOOL hostlog_already_sent(const char *text)
{
    UINT index;
    for (index = 0; index < g_hostlog_seen_count; ++index) {
        if (lstrcmpA(g_hostlog_seen[index], text) == 0)
            return TRUE;
    }
    if (g_hostlog_seen_count < D9_HOSTLOG_MAX_DISTINCT) {
        lstrcpynA(g_hostlog_seen[g_hostlog_seen_count], text,
                (int)D9WG_LOG_MAX_TEXT);
        ++g_hostlog_seen_count;
        return FALSE;
    }
    /* Past the cap every further distinct message would be re-sent forever;
     * say so once and go quiet rather than flooding. */
    if (g_hostlog_overflowed)
        return TRUE;
    g_hostlog_overflowed = TRUE;
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
    if (hostlog_already_sent(text))
        return;
    length = lstrlenA(text);
    if (length > (int)D9WG_LOG_MAX_TEXT)
        length = (int)D9WG_LOG_MAX_TEXT;
    header->severity = severity;
    header->text_bytes = (uint32_t)length;
    CopyMemory(payload + sizeof(*header), text, (SIZE_T)length);
    emit_command(D9WG_OP_GUEST_LOG, payload,
            (uint32_t)(sizeof(*header) + (uint32_t)length));
}

#define HOSTLOG_INFO(...) \
    host_log(D9WG_LOG_SEVERITY_INFO, __VA_ARGS__)
#define HOSTLOG_REFUSED(...) \
    host_log(D9WG_LOG_SEVERITY_REFUSED, __VA_ARGS__)

/*
 * Every refusal reported through this is a real API the game asked for and did
 * not get, and a bare D3DERR_INVALIDCALL is indistinguishable from "the game
 * never called it" -- from the host console, from the trace, from everywhere.
 * That blind spot is what made Kart Rider's missing shop art un-diagnosable:
 * the picture was wrong, the executor reported a clean frame with no dropped
 * draws, and nothing anywhere recorded that the guest had been turned down.
 * Name the refusal so the next round starts from a fact instead of a
 * hypothesis.
 *
 * D9WG_OP_GUEST_LOG mirrors these bounded, deduplicated refusals into the
 * browser console, while the diagnostic DLL retains the full trace.
 */
#define UNSUPPORTED(name) \
    do { \
        TRACE("STUB %s -> D3DERR_INVALIDCALL (unimplemented)", name); \
        HOSTLOG_REFUSED("%s is not implemented and returned " \
                "D3DERR_INVALIDCALL", name); \
    } while (0)

#define HOSTLOG_FAILED(...) \
    host_log(D9WG_LOG_SEVERITY_FAILED, __VA_ARGS__)

#define D9WG_RESERVE_UPLOAD(op, size, extra, payload, blob) \
    reserve_command_locked(op, size, extra, NULL, payload, blob)
#define D9WG_DISCARD_FLAG D3DLOCK_DISCARD
#include "../runtime/d9wg_upload_bridge.h"
#define D9WG_MAPPED_DEVICE D9Device
#define D9WG_MAPPED_VERTEX D9VertexBuffer
#define D9WG_MAPPED_INDEX D9IndexBuffer
#include "../runtime/d9wg_mapped_draw_bridge.h"
#include "../runtime/d9wg_texture_lod_bridge.h"
#include "../runtime/d9wg_gamma_bridge.h"
#include "../runtime/d9wg_private_data_bridge.h"
#include "../runtime/d9wg_present_pacing_bridge.h"
static BOOL submit_readback_batch_locked(BOOL present);
#include "../runtime/d9wg_cursor_bridge.h"

/* ---- format / geometry helpers (format-agnostic; mirrors d3d8proxy) ---- */

static BOOL multiply_u32(UINT left, UINT right, UINT *result)
{
    if (left && right > 0xFFFFFFFFu / left)
        return FALSE;
    *result = left * right;
    return TRUE;
}

static BOOL texture_format_layout(D3DFORMAT format, UINT *block_width,
        UINT *block_height, UINT *block_bytes)
{
    return d9wg_format_layout(9, (uint32_t)format, block_width, block_height, block_bytes);
}

static BOOL texture_level_layout(D3DFORMAT format, UINT width, UINT height,
        UINT *row_pitch, UINT *row_count, UINT *byte_count)
{
    return d9wg_texture_level_layout(9, (uint32_t)format, width, height,
            row_pitch, row_count, byte_count);
}

static BOOL supported_texture_format(D3DFORMAT format)
{
    switch (format) {
    case D3DFMT_R8G8B8:
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
    case D3DFMT_R5G6B5:
    case D3DFMT_X1R5G5B5:
    case D3DFMT_A1R5G5B5:
    case D3DFMT_A4R4G4B4:
    case D3DFMT_R3G3B2:
    case D3DFMT_A8R3G3B2:
    case D3DFMT_X4R4G4B4:
    case D3DFMT_A2B10G10R10:
    case D3DFMT_A8B8G8R8:
    case D3DFMT_X8B8G8R8:
    case D3DFMT_G16R16:
    case D3DFMT_A2R10G10B10:
    case D3DFMT_A16B16G16R16:
    case D3DFMT_L8:
    case D3DFMT_A8:
    case D3DFMT_A8L8:
    case D3DFMT_A4L4:
    case D3DFMT_V8U8:
    case D3DFMT_L6V5U5:
    case D3DFMT_X8L8V8U8:
    case D3DFMT_Q8W8V8U8:
    case D3DFMT_V16U16:
    case D3DFMT_A2W10V10U10:
    case D3DFMT_CxV8U8:
    case D3DFMT_L16:
    case D3DFMT_R16F:
    case D3DFMT_G16R16F:
    case D3DFMT_A16B16G16R16F:
    case D3DFMT_R32F:
    case D3DFMT_G32R32F:
    case D3DFMT_A32B32G32R32F:
    case D3DFMT_Q16W16V16U16:
    /* Palettized. The palette is device state applied at sample time, which is
     * why these need SetPaletteEntries to be real rather than a stub. */
    case D3DFMT_P8:
    case D3DFMT_A8P8:
    /* Packed 4:2:2 -- two texels per 32-bit block, sharing chroma. */
    case D3DFMT_UYVY:
    case D3DFMT_YUY2:
    case D3DFMT_R8G8_B8G8:
    case D3DFMT_G8R8_G8B8:
    case D3DFMT_DXT1:
    case D3DFMT_DXT2:
    case D3DFMT_DXT3:
    case D3DFMT_DXT4:
    case D3DFMT_DXT5:
        return TRUE;
    default:
        return format == D9FMT_ATI1N || format == D9FMT_ATI2N;
    }
}

/* Sampling support and render-target support are deliberately separate.
 * Luminance, signed bump-map and block-compressed textures are valid shader
 * inputs, but not valid outputs for the proxy's RGBA render path. */
static BOOL supported_render_target_format(D3DFORMAT format)
{
    switch (format) {
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
    case D3DFMT_R5G6B5:
    case D3DFMT_X1R5G5B5:
    case D3DFMT_A1R5G5B5:
    case D3DFMT_A4R4G4B4:
    case D3DFMT_A2B10G10R10:
    case D3DFMT_A2R10G10B10:
    case D3DFMT_R16F:
    case D3DFMT_G16R16F:
    case D3DFMT_A16B16G16R16F:
    case D3DFMT_R32F:
    case D3DFMT_G32R32F:
    case D3DFMT_A32B32G32R32F:
        return TRUE;
    default:
        /* The NULL render target: a colour attachment whose contents nothing
         * ever reads, so a depth-only pass does not have to allocate one it
         * will not use. It is a render target and nothing else -- never a
         * sampled texture -- which is why it appears here and in no other
         * format predicate. */
        return format == D9FMT_NULL;
    }
}

/* Depth surfaces are real WebGPU render attachments.  The guest cannot lock
 * or read them back, so the observable distinction between these D3D9
 * formats is their depth/stencil role rather than their physical storage;
 * the executor maps them to depth24plus-stencil8. */
static BOOL supported_depth_stencil_format(D3DFORMAT format)
{
    switch (format) {
    case D3DFMT_D16:
    case D3DFMT_D16_LOCKABLE:
    case D3DFMT_D15S1:
    case D3DFMT_D24S8:
    case D3DFMT_D24X8:
    case D3DFMT_D24X4S4:
    case D3DFMT_D32:
        return TRUE;
    default:
        /* Outside the switch because a FOURCC is not a D3DFORMAT enumerator,
         * and -Wswitch is right to say so. */
        if (format == D9FMT_RAWZ) {
            HOSTLOG_REFUSED("the RAWZ depth-as-texture format is not "
                    "implemented: it hands the shader the depth buffer's bytes "
                    "for the app to reassemble, and serving clean depth "
                    "instead would make that decode produce nonsense. Use "
                    "INTZ, which is implemented");
            return FALSE;
        }
        return format == D9FMT_DF16 || format == D9FMT_DF24
                || format == D9FMT_INTZ;
    }
}

static BOOL supported_volume_texture_format(D3DFORMAT format);
static BOOL emit_generate_mips(D9Device *device, uint32_t resource_handle,
        D3DTEXTUREFILTERTYPE filter, BOOL defer_generation);
/* N-patch tessellation, defined with the other higher-order primitive code far
 * below but reached from the draw path above it. Returns D3DERR_INVALIDCALL for
 * a draw it cannot tessellate, which the callers treat as "draw it flat"
 * rather than as a failure: a mesh without normals should still appear. */
static HRESULT d9_draw_npatch(IDirect3DDevice9 *iface, D9Device *device,
        D3DPRIMITIVETYPE primitive_type, UINT primitive_count,
        const void *indices, UINT index_size, UINT base_vertex);
static HRESULT create_target_texture(D9Device *device, UINT width, UINT height,
        D3DFORMAT format, DWORD usage, D3DMULTISAMPLE_TYPE multisample,
        DWORD multisample_quality, IDirect3DSurface9 **surface_out);

/*
 * The single predicate behind both CheckDeviceFormat and every Create*
 * entry point.
 *
 * These rules used to be spelled out twice, once per caller, and had drifted
 * apart: the query blessed any cube map whose format was sampleable, while
 * device_create_cube_texture refused every cube map carrying a usage flag.
 * 3DMark06 asked whether it could filter a G16R16 cube map, was told yes, and
 * got D3DERR_INVALIDCALL back from the create -- a combination D3D9 never
 * produces and which apps therefore do not defend against. It threw a C++
 * exception out of the failure and died dereferencing an uninitialised COM
 * pointer while unwinding.
 *
 * So the invariant is: a create that CheckDeviceFormat blessed must succeed,
 * and one it refused must be the only thing that fails. Answering "no" is
 * always safe -- an app told no picks another format or does without -- but
 * answering "yes" and then failing is not recoverable from the app's side.
 * Both callers routing through here is what keeps that true as formats are
 * added.
 *
 * `usage` here carries only real D3DUSAGE_* creation bits; CheckDeviceFormat
 * strips its query-only bits before calling.
 */
static BOOL texture_create_supported(D3DRESOURCETYPE type, DWORD usage,
        D3DFORMAT format, D3DPOOL pool)
{
    /* Hint bits are deliberately absent from every test below.
     * D3DUSAGE_AUTOGENMIPMAP, _DMAP, _NPATCHES and _SOFTWAREPROCESSING say what
     * a resource is *for*, not whether it can exist, and D3D9 creates the
     * resource either way. Refusing a create is the dangerous direction -- it
     * is the direction that crashed 3DMark06 -- so this predicate answers only
     * the question a create actually asks. CheckDeviceFormat applies the
     * stricter test on top, which keeps the query no weaker than the create
     * while never making the create refuse something D3D9 would build. */
    if (pool > D3DPOOL_SCRATCH)
        return FALSE;
    /* No resource is both a colour and a depth attachment. */
    if ((usage & D3DUSAGE_RENDERTARGET) && (usage & D3DUSAGE_DEPTHSTENCIL))
        return FALSE;
    if (usage & D3DUSAGE_DEPTHSTENCIL) {
        /* Only 2D surfaces back a depth attachment: the executor's depth path
         * resolves one mip of one array layer, which a cube or volume cannot
         * express without a face/slice selector the protocol does not carry. */
        if (type != D3DRTYPE_TEXTURE && type != D3DRTYPE_SURFACE)
            return FALSE;
        return pool == D3DPOOL_DEFAULT
                && supported_depth_stencil_format(format);
    }
    if (usage & D3DUSAGE_RENDERTARGET) {
        /* Cube maps included since protocol 1.4: D9WG_OP_SET_RENDER_TARGET
         * carries the face, so the host can attach one layer of the six-layer
         * texture a cube map really is. Volumes stay out -- a 3D texture slice
         * is not an array layer and has no D3D9 surface to bind anyway. */
        if (type == D3DRTYPE_VOLUMETEXTURE)
            return FALSE;
        return pool == D3DPOOL_DEFAULT
                && supported_render_target_format(format);
    }
    switch (type) {
    case D3DRTYPE_VOLUMETEXTURE:
        return supported_volume_texture_format(format);
    case D3DRTYPE_SURFACE:
        /*
         * A plain offscreen surface. This answered no for a while, because the
         * ones built here were CPU-only and StretchRect -- which D3D9 defines
         * on any two D3DPOOL_DEFAULT surfaces -- had no handle to work with;
         * 3DMark06 asked, was told yes, and got an
         * "IDirect3DDevice9::StretchRect failed" box.
         *
         * The lesson was not "answer no" but that the invariant has two halves:
         * a create the query blessed must succeed, *and* the operations D3D9
         * defines on the result must work. A DEFAULT-pool offscreen surface now
         * carries the same GPU texture a render target does, so both halves
         * hold and the honest answer is yes again.
         */
        return supported_texture_format(format);
    case D3DRTYPE_TEXTURE:
    case D3DRTYPE_CUBETEXTURE:
        return supported_texture_format(format);
    default:
        return FALSE;
    }
}

static BOOL supported_backbuffer_format(D3DFORMAT format)
{
    return format == D3DFMT_A8R8G8B8 || format == D3DFMT_X8R8G8B8
            || format == D3DFMT_R5G6B5;
}

/* WebGPU guarantees a single quality level for the sample counts it exposes;
 * this backend deliberately advertises only the 4-sample mode that it can
 * create and resolve consistently for both colour and depth attachments.
 * D3D9 quality indices are zero-based, hence the one advertised level means
 * MultiSampleQuality must be zero. */
static BOOL supported_multisample(D3DFORMAT format,
        D3DMULTISAMPLE_TYPE multisample)
{
    if (multisample == D3DMULTISAMPLE_NONE)
        return TRUE;
    if (multisample != D3DMULTISAMPLE_4_SAMPLES)
        return FALSE;
    return supported_backbuffer_format(format)
            || supported_render_target_format(format)
            || supported_depth_stencil_format(format);
}

static D9TextureLevel *surface_texture_level(D9Surface *surface)
{
    if (!surface)
        return NULL;
    if (surface->texture && surface->level < surface->texture->level_count)
        return &surface->texture->levels[surface->level];
    if (surface->cube_texture && surface->cube_face < 6u
            && surface->level < surface->cube_texture->level_count)
        return &surface->cube_texture->levels[surface->cube_face
                * surface->cube_texture->level_count + surface->level];
    return NULL;
}

static BOOL ensure_target_shadow(D9Surface *surface)
{
    D9TextureLevel *level = surface_texture_level(surface);
    if (!level || (surface->format != D3DFMT_A8R8G8B8
            && surface->format != D3DFMT_X8R8G8B8))
        return FALSE;
    if (level->shadow)
        return TRUE;
    if (!level->byte_count && !texture_level_layout(surface->format,
            level->width, level->height, &level->row_pitch,
            &level->row_count, &level->byte_count))
        return FALSE;
    level->shadow = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            level->byte_count);
    return level->shadow != NULL;
}

static void mirror_target_color_fill(D9Surface *surface, const RECT *area,
        D3DCOLOR color)
{
    D9TextureLevel *level = surface_texture_level(surface);
    RECT clipped;
    UINT x, y;
    BOOL full;

    if (!level || !area)
        return;
    clipped.left = area->left < 0 ? 0 : area->left;
    clipped.top = area->top < 0 ? 0 : area->top;
    clipped.right = area->right > (LONG)level->width
            ? (LONG)level->width : area->right;
    clipped.bottom = area->bottom > (LONG)level->height
            ? (LONG)level->height : area->bottom;
    if (clipped.right <= clipped.left || clipped.bottom <= clipped.top)
        return;
    full = clipped.left == 0 && clipped.top == 0
            && clipped.right == (LONG)level->width
            && clipped.bottom == (LONG)level->height;
    /* Filling part of an unknown image does not make the other pixels known. */
    if (!full && !level->shadow_valid)
        return;
    if (!ensure_target_shadow(surface))
        return;
    if (surface->format == D3DFMT_X8R8G8B8)
        color |= 0xFF000000u;
    for (y = (UINT)clipped.top; y < (UINT)clipped.bottom; ++y) {
        DWORD *row = (DWORD *)(level->shadow + y * level->row_pitch);
        for (x = (UINT)clipped.left; x < (UINT)clipped.right; ++x)
            row[x] = color;
    }
    if (full)
        level->shadow_valid = TRUE;
}

static void mirror_target_copy(D9Surface *source, const RECT *source_area,
        D9Surface *destination, const RECT *destination_area)
{
    D9TextureLevel *from = surface_texture_level(source);
    D9TextureLevel *to = surface_texture_level(destination);
    UINT width, height, row_bytes, row;
    BOOL destination_full;
    BYTE *temporary = NULL;

    if (!from || !to || !source_area || !destination_area
            || source->format != destination->format
            || (source->format != D3DFMT_A8R8G8B8
                && source->format != D3DFMT_X8R8G8B8)
            || source_area->left < 0 || source_area->top < 0
            || destination_area->left < 0 || destination_area->top < 0
            || source_area->right > (LONG)from->width
            || source_area->bottom > (LONG)from->height
            || destination_area->right > (LONG)to->width
            || destination_area->bottom > (LONG)to->height
            || source_area->right - source_area->left
                    != destination_area->right - destination_area->left
            || source_area->bottom - source_area->top
                    != destination_area->bottom - destination_area->top
            || !from->shadow || !from->shadow_valid) {
        if (to)
            to->shadow_valid = FALSE;
        return;
    }
    destination_full = destination_area->left == 0 && destination_area->top == 0
            && destination_area->right == (LONG)to->width
            && destination_area->bottom == (LONG)to->height;
    if (!destination_full && !to->shadow_valid)
        return;
    if (!ensure_target_shadow(destination)) {
        to->shadow_valid = FALSE;
        return;
    }
    width = (UINT)(source_area->right - source_area->left);
    height = (UINT)(source_area->bottom - source_area->top);
    row_bytes = width * 4u;
    if (from == to) {
        temporary = (BYTE *)HeapAlloc(GetProcessHeap(), 0, row_bytes * height);
        if (!temporary) {
            to->shadow_valid = FALSE;
            return;
        }
        for (row = 0; row < height; ++row)
            CopyMemory(temporary + row * row_bytes,
                    from->shadow + ((UINT)source_area->top + row) * from->row_pitch
                    + (UINT)source_area->left * 4u, row_bytes);
    }
    for (row = 0; row < height; ++row)
        CopyMemory(to->shadow + ((UINT)destination_area->top + row) * to->row_pitch
                + (UINT)destination_area->left * 4u,
                temporary ? temporary + row * row_bytes
                    : from->shadow + ((UINT)source_area->top + row) * from->row_pitch
                        + (UINT)source_area->left * 4u,
                row_bytes);
    if (temporary)
        HeapFree(GetProcessHeap(), 0, temporary);
    if (destination_full)
        to->shadow_valid = TRUE;
}

static void invalidate_render_target_mirrors(D9Device *device)
{
    UINT index;
    for (index = 0; index < D9_MAX_RENDER_TARGETS; ++index) {
        D9TextureLevel *level = surface_texture_level(
                device->render_target_surfaces[index]);
        if (level)
            level->shadow_valid = FALSE;
    }
}

static UINT full_mip_level_count(UINT width, UINT height)
{
    UINT levels = 1;
    while (width > 1 || height > 1) {
        if (width > 1) width >>= 1;
        if (height > 1) height >>= 1;
        ++levels;
    }
    return levels;
}

static BOOL emit_level_update_api(UINT api, uint32_t handle, D3DFORMAT format,
        const D9TextureLevel *level_data, UINT level, UINT z, const RECT *rect)
{
    D9WGTextureRows rows;
    UINT block_width, block_height, block_bytes;
    if (!d9wg_format_layout(api, format, &block_width, &block_height, &block_bytes))
        return FALSE;
    ZeroMemory(&rows, sizeof(rows));
    rows.resource_handle = handle;rows.level = level;rows.z = z;
    rows.x = (UINT)rect->left;rows.y = (UINT)rect->top;
    rows.width = (UINT)(rect->right - rect->left);
    rows.height = (UINT)(rect->bottom - rect->top);
    rows.block_height = block_height;
    rows.row_count = (rows.height + block_height - 1u) / block_height;
    if (!multiply_u32((rows.width + block_width - 1u) / block_width,
            block_bytes, &rows.row_bytes))
        return FALSE;
    rows.source_pitch = level_data->row_pitch;
    rows.source = level_data->shadow + ((UINT)rect->top / block_height) * level_data->row_pitch
            + ((UINT)rect->left / block_width) * block_bytes;
    return emit_texture_rows(&rows);
}

static BOOL emit_level_update(uint32_t handle, D3DFORMAT format,
        const D9TextureLevel *level_data, UINT level, UINT z, const RECT *rect)
{
    return emit_level_update_api(9u, handle, format, level_data, level, z, rect);
}

static BOOL emit_texture_update(D9Texture *texture, UINT level, const RECT *rect)
{
    return emit_level_update_api(device_compatibility_api(texture->device), texture->handle, texture->format, &texture->levels[level], level, 0, rect);
}

static BOOL primitive_element_count(D3DPRIMITIVETYPE type,
        UINT primitive_count, UINT *element_count)
{
    switch (type) {
    case D3DPT_POINTLIST:
        *element_count = primitive_count;
        return TRUE;
    case D3DPT_LINELIST:
        return multiply_u32(primitive_count, 2u, element_count);
    case D3DPT_LINESTRIP:
        if (primitive_count == 0xFFFFFFFFu)
            return FALSE;
        *element_count = primitive_count + 1u;
        return TRUE;
    case D3DPT_TRIANGLELIST:
        return multiply_u32(primitive_count, 3u, element_count);
    case D3DPT_TRIANGLESTRIP:
    case D3DPT_TRIANGLEFAN:
        if (primitive_count > 0xFFFFFFFDu)
            return FALSE;
        *element_count = primitive_count + 2u;
        return TRUE;
    default:
        return FALSE;
    }
}

/* ---- FVF -> vertex declaration (plan section 4.3) ---- */

/*
 * Only the position/normal/diffuse/specular/2D-texcoord subset that M1's
 * fixed pipeline understands. Blended positions (XYZB*), pretransformed
 * XYZW, and non-default (1D/3D/4D) texture coordinate sizes are honestly
 * rejected rather than silently truncated -- FALSE means "SetFVF/CreateVertex
 * Buffer(..., this fvf, ...) should fail with D3DERR_INVALIDCALL", not "best
 * effort".
 */
static BOOL fvf_to_declaration(DWORD fvf, D9WGVertexElement *elements,
        UINT *element_count)
{
    return d9wg_fvf_decode(D9WG_FVF_D3D9, fvf, elements, D3DMAXDECLLENGTH,
            element_count, NULL);
}

/* ---- vertex declaration validation ---- */

static BOOL declaration_element_supported(const D3DVERTEXELEMENT9 *e)
{
    if (e->Stream >= D9_MAX_STREAMS || e->Method != D3DDECLMETHOD_DEFAULT)
        return FALSE;
    switch (e->Usage) {
    case D3DDECLUSAGE_POSITION:
    case D3DDECLUSAGE_POSITIONT:
    case D3DDECLUSAGE_NORMAL:
    case D3DDECLUSAGE_COLOR:
    case D3DDECLUSAGE_TEXCOORD:
    case D3DDECLUSAGE_PSIZE:
    /* M2: a programmable vertex shader binds its inputs by semantic, so any
     * usage the host can name is now bindable even though the fixed-function
     * path still ignores most of them. */
    case D3DDECLUSAGE_BLENDWEIGHT:
    case D3DDECLUSAGE_BLENDINDICES:
    case D3DDECLUSAGE_TANGENT:
    case D3DDECLUSAGE_BINORMAL:
    case D3DDECLUSAGE_FOG:
        break;
    default:
        /* TESSFACTOR/DEPTH/SAMPLE have no consumer on this path. */
        return FALSE;
    }
    switch (e->Type) {
    case D3DDECLTYPE_FLOAT1:
    case D3DDECLTYPE_FLOAT2:
    case D3DDECLTYPE_FLOAT3:
    case D3DDECLTYPE_FLOAT4:
    case D3DDECLTYPE_D3DCOLOR:
    /* M5: compact skinning declarations. WebGPU exposes the unnormalised
     * integer formats as integer shader inputs, and UDEC3/DEC3N as a packed
     * uint32; the host builds a declaration-specific WGSL variant which
     * converts each of them to the float4 D3D9 promises in v#. */
    case D3DDECLTYPE_UBYTE4:
    case D3DDECLTYPE_SHORT2:
    case D3DDECLTYPE_SHORT4:
    /* The remaining compact formats D3D9 delivers to a shader as floats and
     * WebGPU has an exact vertex-format equivalent for. */
    case D3DDECLTYPE_UBYTE4N:
    case D3DDECLTYPE_SHORT2N:
    case D3DDECLTYPE_SHORT4N:
    case D3DDECLTYPE_USHORT2N:
    case D3DDECLTYPE_USHORT4N:
    case D3DDECLTYPE_UDEC3:
    case D3DDECLTYPE_DEC3N:
    case D3DDECLTYPE_FLOAT16_2:
    case D3DDECLTYPE_FLOAT16_4:
        return TRUE;
    default:
        return FALSE;
    }
}

/* Scans the app's D3DDECL_END()-terminated array (sentinel Stream==0xFF),
 * bounded by D3DMAXDECLLENGTH so a missing sentinel can never walk off the
 * app's allocation. On success fills `wire` with the exact wire shape
 * CREATE_VERTEX_DECLARATION/SET_FVF send. */
static BOOL parse_vertex_declaration(const D3DVERTEXELEMENT9 *elements,
        D9WGVertexElement *wire, UINT *count_out)
{
    UINT count = 0;
    while (count < D3DMAXDECLLENGTH && elements[count].Stream != 0xFF) {
        const D3DVERTEXELEMENT9 *e = &elements[count];
        if (!declaration_element_supported(e)) {
            /* Which element, not just "the declaration": one rejected usage or
             * D3DDECLTYPE discards the whole array, and the attribute the app
             * loses is almost never the one it would guess. Deduped by text,
             * so a declaration rebuilt every frame costs one line. */
            HOSTLOG_REFUSED("vertex declaration rejected at element %lu: "
                    "stream=%lu offset=%lu type=%lu method=%lu usage=%lu "
                    "usage_index=%lu (the whole declaration is discarded)",
                    (DWORD)count, (DWORD)e->Stream, (DWORD)e->Offset,
                    (DWORD)e->Type, (DWORD)e->Method, (DWORD)e->Usage,
                    (DWORD)e->UsageIndex);
            return FALSE;
        }
        wire[count].stream = e->Stream;
        wire[count].offset = e->Offset;
        wire[count].type = e->Type;
        wire[count].method = e->Method;
        wire[count].usage = e->Usage;
        wire[count].usage_index = e->UsageIndex;
        ++count;
    }
    if (count == D3DMAXDECLLENGTH && elements[count].Stream != 0xFF)
        return FALSE;
    *count_out = count;
    return TRUE;
}

/* ---- resource create/update emitters ---- */

static BOOL emit_vertex_buffer_create(D9Device *device, D9VertexBuffer *buffer)
{
    D9WGCreateBuffer command;
    command.device_handle = device->handle;
    command.resource_handle = buffer->handle;
    command.resource_kind = D9WG_RESOURCE_BUFFER_VERTEX;
    command.byte_count = buffer->length;
    command.usage = buffer->usage;
    command.fvf = buffer->fvf;
    command.pool = buffer->pool;
    command.reserved = 0;
    return emit_command(D9WG_OP_CREATE_BUFFER, &command, sizeof(command));
}

static BOOL emit_index_buffer_create(D9Device *device, D9IndexBuffer *buffer)
{
    D9WGCreateBuffer command;
    command.device_handle = device->handle;
    command.resource_handle = buffer->handle;
    command.resource_kind = D9WG_RESOURCE_BUFFER_INDEX;
    command.byte_count = buffer->length;
    command.usage = buffer->usage;
    command.fvf = (uint32_t)buffer->format;
    command.pool = buffer->pool;
    command.reserved = 0;
    return emit_command(D9WG_OP_CREATE_BUFFER, &command, sizeof(command));
}

static BOOL emit_texture_create(D9Device *device, D9Texture *texture)
{
    D9WGCreateTexture2D command;
    ZeroMemory(&command, sizeof(command));
    command.device_handle = device->handle;
    command.resource_handle = texture->handle;
    command.width = texture->width;
    command.height = texture->height;
    command.level_count = texture->level_count;
    command.format = texture->format;
    command.usage = texture->usage;
    command.pool = texture->pool;
    command.multisample_type = texture->multisample;
    command.multisample_quality = texture->multisample_quality;
    if (!emit_command(D9WG_OP_CREATE_TEXTURE_2D, &command, sizeof(command))) return FALSE;
    return device_compatibility_api(device) == 8u
        || !(texture->usage & D3DUSAGE_AUTOGENMIPMAP)
        || emit_generate_mips(device, texture->handle, texture->autogen_filter, TRUE);
}

static BOOL emit_vertex_declaration_create(D9Device *device, uint32_t handle,
        const D9WGVertexElement *elements, UINT count)
{
    D9WGCreateVertexDeclaration command;
    uint8_t *payload;
    uint8_t *blob;
    uint32_t element_bytes = (uint32_t)count * sizeof(D9WGVertexElement);
    BOOL result;

    command.device_handle = device->handle;
    command.resource_handle = handle;
    command.element_count = count;
    command.reserved = 0;
    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_CREATE_VERTEX_DECLARATION,
            sizeof(command), element_bytes, NULL, &payload, &blob);
    if (result) {
        CopyMemory(payload, &command, sizeof(command));
        if (element_bytes)
            CopyMemory(blob, elements, element_bytes);
    }
    LeaveCriticalSection(&g_transport_lock);
    return result;
}

static BOOL emit_set_fvf(D9Device *device, DWORD fvf,
        const D9WGVertexElement *elements, UINT count)
{
    D9WGSetFVF command;
    uint8_t *payload;
    uint8_t *blob;
    uint32_t element_bytes = (uint32_t)count * sizeof(D9WGVertexElement);
    BOOL result;

    command.device_handle = device->handle;
    command.fvf = fvf;
    command.element_count = count;
    command.reserved = 0;
    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_SET_FVF,
            sizeof(command), element_bytes, NULL, &payload, &blob);
    if (result) {
        CopyMemory(payload, &command, sizeof(command));
        if (element_bytes)
            CopyMemory(blob, elements, element_bytes);
    }
    LeaveCriticalSection(&g_transport_lock);
    return result;
}

/* ---- shader bytecode: length, hash, upload ----
 *
 * CreateVertexShader/CreatePixelShader receive a bare `const DWORD *` with no
 * length. The stream has to be walked to its D3DVS_END()/D3DPS_END()
 * terminator (0x0000FFFF) to know how many bytes to copy and send.
 *
 * The walk cannot be a naive "scan for 0x0000FFFF": `def`/`defi` embed four
 * raw literal DWORDs, and an integer constant of 65535 or a denormal float
 * would be indistinguishable from the terminator. Those three opcodes are
 * therefore skipped by their known fixed size. Everything else is safe to
 * step over one token at a time, because every operand token has bit 31 set
 * and so can never be mistaken for the terminator -- which is also why this
 * needs no per-opcode operand table on the guest side. Instruction lengths
 * (SM2.0+) are used when present purely to skip faster.
 */
#define D9_SIO_OPCODE_MASK 0x0000FFFFu
#define D9_SIO_COMMENT 0xFFFEu
#define D9_SIO_END 0xFFFFu
#define D9_SIO_DEF 81u
#define D9_SIO_DEFI 48u
#define D9_SIO_DEFB 47u

/* ---- Stage 6: D3D8 shader model 1.x bytecode validation ---- */

static BOOL shader8_regtype_valid(DWORD register_type, BOOL is_pixel_shader)
{
    switch (register_type >> D3DSP_REGTYPE_SHIFT) {
    case D3DSPR_TEMP:
    case D3DSPR_INPUT:
    case D3DSPR_CONST:
    case D3DSPR_ADDR: /* SDK9 ordinal;legacy tokens keep SDK8 shifted encoding. */
        return TRUE;
    case D3DSPR_RASTOUT:
    case D3DSPR_ATTROUT:
    case D3DSPR_TEXCRDOUT:
        return !is_pixel_shader;
    default:
        return FALSE;
    }
}

/*
 * Returns FALSE for any opcode outside the supported instruction set for this
 * shader type/version. The set is the host translator's, not a smaller one of
 * this DLL's own: a legal instruction refused here is a shader the backend
 * could have run, and the app has no way to tell that apart from a driver
 * with no shader support at all. What stays refused is what the host also
 * refuses (`texdepth`, `texm3x2depth`, `texm3x3`) plus anything malformed, on
 * the parser safety rule "never guess at semantics for an instruction you do
 * not implement". On success,
 * *operand_words is the number of DWORD parameter tokens following the
 * opcode token (D3DSIO_DEF's trailing four are raw float32 immediates, not
 * register-encoded operands, but are still counted here).
 */
static BOOL shader8_opcode_supported(WORD opcode, BOOL is_pixel_shader,
        UINT minor, UINT *operand_words)
{
    switch (opcode) {
    case D3DSIO_NOP:
        *operand_words = 0; return TRUE;
    case D3DSIO_MOV:
        *operand_words = 2; return TRUE;
    case D3DSIO_ADD:
    case D3DSIO_SUB:
    case D3DSIO_MUL:
    case D3DSIO_MIN:
    case D3DSIO_MAX:
    case D3DSIO_DP3:
    case D3DSIO_DP4:
        *operand_words = 3; return TRUE;
    case D3DSIO_MAD:
        *operand_words = 4; return TRUE;
    case D3DSIO_RCP:
    case D3DSIO_RSQ:
    case D3DSIO_EXP:
    case D3DSIO_LOG:
    case D3DSIO_LIT:
    case D3DSIO_FRC:
    case D3DSIO_EXPP:
    case D3DSIO_LOGP:
        if (is_pixel_shader) return FALSE;
        *operand_words = 2; return TRUE;
    case D3DSIO_SLT:
    case D3DSIO_SGE:
    case D3DSIO_DST:
        if (is_pixel_shader) return FALSE;
        *operand_words = 3; return TRUE;
    case D3DSIO_DEF:
        *operand_words = 5; return TRUE;
    case D3DSIO_M4x4:
    case D3DSIO_M4x3:
    case D3DSIO_M3x4:
    case D3DSIO_M3x3:
    case D3DSIO_M3x2:
        /* dst, the source vector, and the first row of the matrix. These are
         * how every vs_1_x transform is spelled -- 3DMark 2001 opens with
         * `m4x4 oPos, v0, c0` -- and the host translator expands each one into
         * consecutive dot products. Refusing them refused vertex shaders as a
         * feature. */
        if (is_pixel_shader) return FALSE;
        *operand_words = 3; return TRUE;
    case D3DSIO_LRP:
        if (!is_pixel_shader) return FALSE;
        *operand_words = 4; return TRUE;
    case D3DSIO_CND:
        /* ps_1_4 keeps CND and compares each source component to 0.5. */
        if (!is_pixel_shader || minor > 4u) return FALSE;
        *operand_words = 4; return TRUE;
    case D3DSIO_CMP:
        if (!is_pixel_shader || minor < 2u) return FALSE;
        *operand_words = 4; return TRUE;
    case D3DSIO_TEXCOORD:
        /* ps_1_4 renamed this to `texcrd dst, src` and gave it a source
         * operand; 1.1-1.3 `texcoord dst` has none. Reading the wrong number
         * of words here does not just mis-validate one instruction, it
         * desyncs the walk over everything after it. */
        if (!is_pixel_shader) return FALSE;
        *operand_words = (minor >= 4u) ? 2u : 1u; return TRUE;
    case D3DSIO_TEX:
        if (!is_pixel_shader) return FALSE;
        *operand_words = (minor >= 4u) ? 2u : 1u; return TRUE;
    case D3DSIO_TEXKILL:
        if (!is_pixel_shader) return FALSE;
        *operand_words = 1; return TRUE;
    case D3DSIO_PHASE:
        if (!is_pixel_shader || minor != 4u) return FALSE;
        *operand_words = 0; return TRUE;
    case D3DSIO_BEM:
        /* ps_1_4's arithmetic form of the texbem displacement: `bem dst.rg,
         * src0, src1` applies the destination stage's D3DTSS_BUMPENVMAT*
         * matrix without sampling. 1.4-only -- earlier versions have only the
         * texture-addressing spelling. 3DMark 2001's Advanced Pixel Shader
         * test is a ps_1_4 shader built around it, so refusing it failed that
         * test at CreatePixelShader. */
        if (!is_pixel_shader || minor != 4u) return FALSE;
        *operand_words = 3; return TRUE;
    /*
     * The ps_1_x texture-addressing family. The host translates all of these
     * (see the TEXBEM/TEXM3x* cases in d3d9_shader_pipeline.js's emit()), so
     * refusing them here hid environment bump mapping behind a validator that
     * had never been updated to match. TEXDEPTH, TEXM3x2DEPTH and TEXM3x3 stay
     * refused -- those are the three the host itself refuses -- and TEXM3x3DIFF
     * was never implemented by any hardware.
     */
    case D3DSIO_TEXBEM:
    case D3DSIO_TEXBEML:
    case D3DSIO_TEXREG2AR:
    case D3DSIO_TEXREG2GB:
    case D3DSIO_TEXREG2RGB:
    case D3DSIO_TEXDP3:
    case D3DSIO_TEXDP3TEX:
    case D3DSIO_TEXM3x2PAD:
    case D3DSIO_TEXM3x2TEX:
    case D3DSIO_TEXM3x3PAD:
    case D3DSIO_TEXM3x3TEX:
    case D3DSIO_TEXM3x3VSPEC:
        if (!is_pixel_shader) return FALSE;
        *operand_words = 2; return TRUE;
    case D3DSIO_TEXM3x3SPEC:
        /* The one that also takes the eye-ray constant. */
        if (!is_pixel_shader) return FALSE;
        *operand_words = 3; return TRUE;
    default:
        return FALSE;
    }
}

/*
 * Walk one D3D8 SM1.x token stream (the app-supplied pFunction, starting
 * right after the version token) until D3DSIO_END. Rejects anything that
 * would read past `max_tokens`, any opcode outside shader8_opcode_supported,
 * and any register-type field outside the recognized D3DSPR_* set, so a
 * corrupt or hostile token stream can never desync the parser or run
 * unbounded. On success *body_token_count is the token count from index 0
 * through (but excluding) the END token.
 */
static BOOL shader8_validate_body(const DWORD *tokens, UINT max_tokens,
        BOOL is_pixel_shader, UINT minor, UINT *body_token_count)
{
    UINT offset = 0;
    while (offset < max_tokens) {
        DWORD token = tokens[offset];
        WORD opcode;
        UINT operand_words;

        if (token == (DWORD)D3DVS_END()) {
            *body_token_count = offset;
            return TRUE;
        }
        opcode = (WORD)(token & D3DSI_OPCODE_MASK);
        if (opcode == D3DSIO_COMMENT) {
            UINT comment_words = (UINT)((token & D3DSI_COMMENTSIZE_MASK)
                    >> D3DSI_COMMENTSIZE_SHIFT);
            if (offset + 1u + comment_words > max_tokens) {
                TRACE("SHADER VALIDATE REFUSE kind=%s minor=%u "
                        "offset=%u opcode=%04X reason=truncated-comment",
                        is_pixel_shader ? "pixel" : "vertex", minor, offset,
                        (UINT)opcode);
                return FALSE;
            }
            offset += 1u + comment_words;
            continue;
        }
        if (!shader8_opcode_supported(opcode, is_pixel_shader, minor,
                &operand_words)) {
            TRACE("SHADER VALIDATE REFUSE kind=%s minor=%u offset=%u "
                    "opcode=%04X reason=unsupported-opcode",
                    is_pixel_shader ? "pixel" : "vertex", minor, offset,
                    (UINT)opcode);
            return FALSE;
        }
        if (offset + 1u + operand_words > max_tokens) {
            TRACE("SHADER VALIDATE REFUSE kind=%s minor=%u offset=%u "
                    "opcode=%04X reason=truncated-operands",
                    is_pixel_shader ? "pixel" : "vertex", minor, offset,
                    (UINT)opcode);
            return FALSE;
        }
        if (opcode != D3DSIO_NOP && opcode != D3DSIO_PHASE) {
            DWORD dst_regtype = tokens[offset + 1u] & D3DSP_REGTYPE_MASK;
            if (!shader8_regtype_valid(dst_regtype, is_pixel_shader)) {
                TRACE("SHADER VALIDATE REFUSE kind=%s minor=%u "
                        "offset=%u opcode=%04X reason=invalid-dst-regtype "
                        "regtype=%08lX",
                        is_pixel_shader ? "pixel" : "vertex", minor, offset,
                        (UINT)opcode, dst_regtype);
                return FALSE;
            }
        }
        if (opcode != D3DSIO_DEF && opcode != D3DSIO_NOP
                && opcode != D3DSIO_PHASE) {
            UINT src_count = operand_words - 1u;
            UINT src;
            for (src = 0; src < src_count; ++src) {
                DWORD src_regtype =
                        tokens[offset + 2u + src] & D3DSP_REGTYPE_MASK;
                if (!shader8_regtype_valid(src_regtype, is_pixel_shader)) {
                    TRACE("SHADER VALIDATE REFUSE kind=%s minor=%u "
                            "offset=%u opcode=%04X reason=invalid-src-regtype "
                            "source=%u regtype=%08lX",
                            is_pixel_shader ? "pixel" : "vertex", minor,
                            offset, (UINT)opcode, src, src_regtype);
                    return FALSE;
                }
            }
        }
        offset += 1u + operand_words;
    }
    TRACE("SHADER VALIDATE REFUSE kind=%s minor=%u offset=%u "
            "reason=missing-end", is_pixel_shader ? "pixel" : "vertex",
            minor, offset);
    return FALSE; /* ran past max_tokens without ever finding D3DSIO_END */
}

static BOOL shader_token_count(const DWORD *code, UINT *count_out,
        BOOL *is_pixel_out)
{
    UINT index;
    DWORD version;
    UINT major;

    if (!code)
        return FALSE;
    version = code[0];
    if ((version >> 16) == 0xFFFEu)
        *is_pixel_out = FALSE;
    else if ((version >> 16) == 0xFFFFu)
        *is_pixel_out = TRUE;
    else
        return FALSE;
    major = (version >> 8) & 0xFFu;
    if (major < 1 || major > 3)
        return FALSE;

    for (index = 1; index < D9_MAX_SHADER_TOKENS; ) {
        DWORD token = code[index];
        DWORD opcode = token & D9_SIO_OPCODE_MASK;

        if (opcode == D9_SIO_END) {
            *count_out = index + 1;
            return TRUE;
        }
        if (opcode == D9_SIO_COMMENT) {
            index += 1 + ((token >> 16) & 0x7FFFu);
            continue;
        }
        if (!(token & 0x80000000u)
                && (opcode == D9_SIO_DEF || opcode == D9_SIO_DEFI)) {
            index += 6; /* instruction + destination + four literals */
            continue;
        }
        if (!(token & 0x80000000u) && opcode == D9_SIO_DEFB) {
            index += 3; /* instruction + destination + one literal */
            continue;
        }
        if (!(token & 0x80000000u) && major >= 2 && ((token >> 24) & 0xFu)) {
            index += 1 + ((token >> 24) & 0xFu);
            continue;
        }
        ++index;
    }
    return FALSE;
}

/* 64-bit FNV-1a over the raw token bytes. Kept byte-oriented (rather than
 * hashing DWORDs directly) so hashTokens() in d3d9_shader_pipeline.js can
 * reproduce it exactly in JavaScript, letting the host reuse the hash the
 * guest already computed as its translation-cache key. */
static void shader_bytecode_hash(const DWORD *code, UINT token_count,
        uint32_t *low_out, uint32_t *high_out)
{
    uint32_t low = 0x84222325u;
    uint32_t high = 0xCBF29CE4u;
    UINT index;
    UINT byte_index;

    for (index = 0; index < token_count; ++index) {
        DWORD token = code[index];
        for (byte_index = 0; byte_index < 4; ++byte_index) {
            uint32_t l0, l1, h0, h1, r0, r1, r2, r3;
            low ^= (token >> (byte_index * 8)) & 0xFFu;
            /* Multiply the 64-bit accumulator by the FNV prime
             * 0x100000001B3 in 16-bit limbs. The 0x100000000 part of the
             * prime contributes `low` into the high half. */
            l0 = low & 0xFFFFu;
            l1 = low >> 16;
            h0 = high & 0xFFFFu;
            h1 = high >> 16;
            r0 = l0 * 0x1B3u;
            r1 = l1 * 0x1B3u + (r0 >> 16);
            r2 = h0 * 0x1B3u + (r1 >> 16) + l0;
            r3 = h1 * 0x1B3u + (r2 >> 16) + l1;
            low = ((r1 & 0xFFFFu) << 16) | (r0 & 0xFFFFu);
            high = ((r3 & 0xFFFFu) << 16) | (r2 & 0xFFFFu);
        }
    }
    *low_out = low;
    *high_out = high;
}

static BOOL emit_shader_create(D9Device *device, D9Shader *shader)
{
    D9WGCreateVertexShader command;
    uint8_t *payload;
    uint8_t *blob;
    uint32_t code_bytes = (uint32_t)shader->token_count * 4u;
    BOOL result;

    ZeroMemory(&command, sizeof(command));
    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(shader->is_pixel
                    ? D9WG_OP_CREATE_PIXEL_SHADER
                    : D9WG_OP_CREATE_VERTEX_SHADER,
            sizeof(command), code_bytes, NULL, &payload, &blob);
    if (result) {
        command.device_handle = device->handle;
        command.resource_handle = shader->handle;
        command.instruction_token_count = shader->token_count;
        command.code_offset = (uint32_t)(blob - batch_base());
        command.bytecode_hash_low = shader->hash_low;
        command.bytecode_hash_high = shader->hash_high;
        CopyMemory(payload, &command, sizeof(command));
        CopyMemory(blob, shader->code, code_bytes);
    }
    LeaveCriticalSection(&g_transport_lock);
    return result;
}

/* SET_*_SHADER_CONSTANT_F/I/B all use the same header shape, with the payload
 * blob holding vector_count * 16 bytes (float4 / int4) or, for the bool form,
 * one 32-bit slot per register. */
static BOOL emit_shader_constants(D9Device *device, uint16_t opcode,
        UINT start_register, UINT vector_count, const void *data,
        uint32_t data_bytes)
{
    D9WGSetShaderConstantF command;
    uint8_t *payload;
    uint8_t *blob;
    BOOL result;

    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(opcode, sizeof(command), data_bytes,
            NULL, &payload, &blob);
    if (result) {
        command.device_handle = device->handle;
        command.start_register = start_register;
        command.vector_count = vector_count;
        command.data_offset = (uint32_t)(blob - batch_base());
        CopyMemory(payload, &command, sizeof(command));
        CopyMemory(blob, data, data_bytes);
    }
    LeaveCriticalSection(&g_transport_lock);
    return result;
}

static BOOL emit_draw_primitive_up(const D9WGDrawPrimitiveUP *draw,
        const void *vertices)
{
    D9WGDrawPrimitiveUP payload_value = *draw;
    uint8_t *payload;
    uint8_t *blob;
    BOOL result;

    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_DRAW_PRIMITIVE_UP,
            sizeof(payload_value), payload_value.vertex_bytes, NULL,
            &payload, &blob);
    if (result) {
        payload_value.vertex_data_offset = (uint32_t)(blob - batch_base());
        CopyMemory(payload, &payload_value, sizeof(payload_value));
        CopyMemory(blob, vertices, payload_value.vertex_bytes);
    }
    LeaveCriticalSection(&g_transport_lock);
    return result;
}

static BOOL emit_draw_indexed_primitive_up(
        const D9WGDrawIndexedPrimitiveUP *draw, const void *indices,
        const void *vertices)
{
    D9WGDrawIndexedPrimitiveUP payload_value = *draw;
    uint32_t extra_bytes;
    uint8_t *payload;
    uint8_t *blob;
    BOOL result;

    if (payload_value.index_bytes > 0xFFFFFFFFu - payload_value.vertex_bytes)
        return FALSE;
    extra_bytes = payload_value.index_bytes + payload_value.vertex_bytes;
    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_DRAW_INDEXED_PRIMITIVE_UP,
            sizeof(payload_value), extra_bytes, NULL, &payload, &blob);
    if (result) {
        payload_value.index_data_offset = (uint32_t)(blob - batch_base());
        payload_value.vertex_data_offset = payload_value.index_data_offset
                + payload_value.index_bytes;
        CopyMemory(payload, &payload_value, sizeof(payload_value));
        CopyMemory(blob, indices, payload_value.index_bytes);
        CopyMemory(blob + payload_value.index_bytes, vertices,
                payload_value.vertex_bytes);
    }
    LeaveCriticalSection(&g_transport_lock);
    return result;
}

/* ---- device lifecycle plumbing ---- */

static void emit_hello_once(void)
{
    D9WGHello hello;

    if (InterlockedCompareExchange((LONG *)&g_hello_emitted, TRUE, FALSE))
        return;
    hello.guest_pointer_bits = 32;
    /* Reports the caps profile selected for this process (see
     * D9WG_FEATURE_* in the protocol header). */
    hello.feature_bits = shader_model_version() >= 2 ?
            D9WG_FEATURE_SHADER_MODEL_2 : 0;
    if (shader_model_version() >= 3)
        hello.feature_bits |= D9WG_FEATURE_SHADER_MODEL_3;
    hello.session_id_low = g_session_id_low;
    hello.session_id_high = g_session_id_high;
    emit_command(D9WG_OP_HELLO, &hello, sizeof(hello));
    /* Identify this DLL build in the browser console once per process, then
     * use the same channel for later refusals and failures. */
    HOSTLOG_INFO("proxy build %s loaded; refusals and failures will be "
            "reported here", D9_PROXY_BUILD);
}

static void fill_display_mode(D3DDISPLAYMODE *mode, UINT width, UINT height,
        D3DFORMAT format)
{
    mode->Width = width;
    mode->Height = height;
    mode->RefreshRate = 60;
    mode->Format = format;
}

/*
 * Caps stay honest about what is actually implemented. The default profile
 * exposes shader model 3.0, while the VS20Caps/PS20Caps sub-structures still
 * describe the common model-2 subset precisely for applications that inspect
 * them even after selecting an SM3 path.
 * Notably PS20Caps.DynamicFlowControlDepth stays 0: data-dependent branching
 * in a pixel shader translates, but WGSL forbids implicit-derivative texture
 * sampling inside it, so any sample in such a branch silently loses mip
 * selection -- claiming the capability would invite exactly the shaders that
 * hit that degradation.
 *
 * M4.5 makes the lower-capability path a supported negotiated profile:
 * D9WG_CAPS_PROFILE=ffp reports fixed-function T&L with no programmable
 * shaders, D9WG_CAPS_PROFILE=sm2 reports the SM2 path, and the default `sm3`
 * profile reports the complete shader path used by SM3/HDR applications.
 * D9WG_SHADER_MODEL=0 remains accepted as a backwards-compatible spelling.
 */
static UINT shader_model_version(void)
{
    static LONG cached = -1;
    char value[16];
    DWORD length;

    if (cached >= 0)
        return (UINT)cached;
    length = GetEnvironmentVariableA("D9WG_CAPS_PROFILE", value, sizeof(value));
    if (length && length < sizeof(value)) {
        if (!lstrcmpiA(value, "ffp") || !lstrcmpiA(value, "m4.5") ||
                !lstrcmpiA(value, "low")) {
            cached = 0;
            return 0;
        }
        if (!lstrcmpiA(value, "sm2") || !lstrcmpiA(value, "m5")) {
            cached = 2;
            return 2;
        }
        if (!lstrcmpiA(value, "sm3") || !lstrcmpiA(value, "m6")) {
            cached = 3;
            return 3;
        }
    }
    length = GetEnvironmentVariableA("D9WG_SHADER_MODEL", value, sizeof(value));
    cached = (length == 1 && value[0] == '0') ? 0 : 3;
    return (UINT)cached;
}

static uint32_t runtime_host_features(void)
{
    uint32_t guest_features = shader_model_version() >= 2 ? D9WG_FEATURE_SHADER_MODEL_2 : 0;
    if (shader_model_version() >= 3) guest_features |= D9WG_FEATURE_SHADER_MODEL_3;
    EnterCriticalSection(&g_transport_lock);
    uint32_t features = d9wg_transport_host_features(g_process, guest_features, submit_batch_locked);
    LeaveCriticalSection(&g_transport_lock);
    return features;
}

static BOOL env_flag_enabled(const char *name)
{
    char value[16];
    DWORD length = GetEnvironmentVariableA(name, value, sizeof(value));

    return length && length < sizeof(value) && value[0] != '0';
}

/*
 * User clip planes are implemented by carrying vertex-computed plane distances
 * through two vec4 varyings and discarding negative fragments.  Six is D3D9's
 * architectural maximum and is now an ordinary backed capability.
 */
static DWORD reported_user_clip_planes(void)
{
    return D9_MAX_CLIP_PLANES;
}

/*
 * D9WG_CAPS_PERMISSIVE=1: report the capability set of a complete D3D9
 * implementation instead of only what this proxy backs.
 *
 * This exists for a recurring failure shape: a title reads D3DCAPS9, decides
 * the device cannot run it, and exits before drawing -- with no call having
 * failed, so nothing in a trace points at the cause. The GTA SA case was
 * narrowed to one supported ShadeCaps bit and fixed in fill_caps();
 * A broader caps comparison remains useful for the next title.
 *
 * The values below are SwiftShader 2.1's, read straight out of that probe, so
 * this is "answer what a working implementation answered" rather than another
 * guess about what a real card reports. Enabling it claims plenty this proxy
 * does not implement: an app that takes one of those paths renders wrong. That
 * is the trade -- it is a bisection tool for finding which field a title gates
 * on, not a mode to ship in. Fields where the reference and this proxy already
 * agree are omitted, so the list below IS the outstanding difference.
 */
static void apply_permissive_caps(D3DCAPS9 *caps)
{
    if (!env_flag_enabled("D9WG_CAPS_PERMISSIVE"))
        return;

    caps->Caps = D3DCAPS_READ_SCANLINE;
    caps->Caps2 = D3DCAPS2_FULLSCREENGAMMA | D3DCAPS2_DYNAMICTEXTURES
            | D3DCAPS2_CANAUTOGENMIPMAP;
    caps->Caps3 = D3DCAPS3_ALPHA_FULLSCREEN_FLIP_OR_DISCARD
            | D3DCAPS3_LINEAR_TO_SRGB_PRESENTATION | D3DCAPS3_COPY_TO_VIDMEM
            | D3DCAPS3_COPY_TO_SYSTEMMEM;
    caps->DevCaps = 0x009BBFF0u;
    caps->PrimitiveMiscCaps = 0x001FCFF2u;
    caps->RasterCaps = 0x07712190u;
    caps->ShadeCaps = 0x00084208u;
    caps->TextureCaps = 0x0001ECC5u;
    caps->CubeTextureFilterCaps = caps->TextureFilterCaps;
    caps->VolumeTextureFilterCaps = caps->TextureFilterCaps;
    caps->TextureAddressCaps = 0x0000003Fu;
    caps->VolumeTextureAddressCaps = 0x0000003Fu;
    caps->LineCaps = 0x0000001Fu;
    caps->MaxTextureWidth = 65536;
    caps->MaxTextureHeight = 65536;
    caps->MaxVolumeExtent = 65536;
    caps->MaxTextureAspectRatio = 65536;
    caps->MaxAnisotropy = 16;
    caps->GuardBandLeft = -1.0e8f;
    caps->GuardBandTop = -1.0e8f;
    caps->GuardBandRight = 1.0e8f;
    caps->GuardBandBottom = 1.0e8f;
    caps->StencilCaps = 0x000001FFu;
    caps->FVFCaps = 0x00180008u;
    caps->TextureOpCaps = 0x03FFFFFFu;
    caps->VertexProcessingCaps = 0x0000013Bu;
    caps->MaxUserClipPlanes = 6;
    caps->MaxVertexBlendMatrices = 4;
    caps->MaxVertexBlendMatrixIndex = 255;
    caps->MaxPointSize = 8192.0f;
    caps->MaxPrimitiveCount = 2097152;
    caps->MaxVertexIndex = 16777216;
    caps->MaxStreams = 16;
    caps->MaxStreamStride = 65536;
    caps->DevCaps2 = D3DDEVCAPS2_STREAMOFFSET
            | D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES
            | D3DDEVCAPS2_VERTEXELEMENTSCANSHARESTREAMOFFSET;
    caps->StretchRectFilterCaps = 0x03000300u;
    caps->MaxVShaderInstructionsExecuted = 0xFFFFFFFFu;
    caps->MaxPShaderInstructionsExecuted = 0xFFFFFFFFu;
}

static void fill_caps_for_profile(D3DCAPS9 *caps, UINT shader_model, BOOL permissive)
{
    ZeroMemory(caps, sizeof(*caps));
    caps->DeviceType = D3DDEVTYPE_HAL;
    caps->AdapterOrdinal = D3DADAPTER_DEFAULT;
    /*
     * Everything below claims only what this proxy implements -- but the
     * fields left at their ZeroMemory value were understating it, which is the
     * same class of bug as answering a call successfully with the wrong value.
     * A cap reported as 0 is not "conservative" when the feature works: it
     * tells a title the device cannot do something it can, and titles branch on
     * that. Each addition here names the code that backs it.
     */
    caps->Caps2 = D3DCAPS2_CANMANAGERESOURCE
            /* device_set_gamma_ramp(). */
            | D3DCAPS2_FULLSCREENGAMMA
            /* D3DUSAGE_DYNAMIC passes CheckDeviceFormat and CreateTexture, and
             * the lock paths honour DISCARD/NOOVERWRITE. */
            | D3DCAPS2_DYNAMICTEXTURES
            /* D3DUSAGE_AUTOGENMIPMAP: CreateTexture and CreateCubeTexture both
             * accept it, the host allocates the full chain and regenerates
             * everything below level 0 whenever level 0 is written -- by upload
             * or by a render pass -- and GenerateMipSubLevels forces it. The
             * comment here used to say the usage was refused, which stopped
             * being true and left a working capability unadvertised. */
            | D3DCAPS2_CANAUTOGENMIPMAP;
    /* SetCursorProperties/SetCursorPosition/ShowCursor are all implemented, so
     * reporting no hardware cursor at all was simply wrong. */
    caps->CursorCaps = D3DCURSORCAPS_COLOR;
    /* SetStreamSource carries OffsetInBytes to the host, and StretchRect
     * accepts texture-level surfaces as its source. */
    caps->DevCaps2 = D3DDEVCAPS2_STREAMOFFSET
            | D3DDEVCAPS2_CAN_STRETCHRECT_FROM_TEXTURES;
    /* A single-adapter device is a group of one. Zero is not a value any
     * runtime reports, and an app that iterates the group gets nothing. */
    caps->NumberOfAdaptersInGroup = 1;
    caps->PresentationIntervals = D9WG_PRESENT_INTERVAL_MASK;
    caps->DevCaps = D3DDEVCAPS_HWRASTERIZATION
            | D3DDEVCAPS_HWTRANSFORMANDLIGHT
            | D3DDEVCAPS_DRAWPRIMTLVERTEX
            | D3DDEVCAPS_EXECUTESYSTEMMEMORY
            | D3DDEVCAPS_EXECUTEVIDEOMEMORY
            | D3DDEVCAPS_TEXTURESYSTEMMEMORY
            | D3DDEVCAPS_TEXTUREVIDEOMEMORY
            /* Pre-transformed vertices draw from both a vertex buffer and a
             * DrawPrimitiveUP pointer. */
            | D3DDEVCAPS_TLVERTEXSYSTEMMEMORY
            | D3DDEVCAPS_TLVERTEXVIDEOMEMORY
            /* Present queues the frame and returns; nothing has to drain
             * before the app may draw again. */
            | D3DDEVCAPS_CANRENDERAFTERFLIP
            /* Driver-model bits rather than features: every D3D9 device serves
             * draws through the DrawPrimitives2 DDI.  Some engines read their
             * absence as "this is a DX7-era driver" and take a legacy path. */
            | D3DDEVCAPS_DRAWPRIMITIVES2
            | D3DDEVCAPS_DRAWPRIMITIVES2EX
            /* Higher-order primitives. WebGPU has no tessellation stage, so
             * DrawRectPatch/DrawTriPatch evaluate the surface in the guest and
             * draw the result as an ordinary indexed triangle list -- which is
             * why nothing about them reaches the protocol.
             *
             * HANDLEZERO and not the caching form: nothing here keeps a
             * tessellated patch between calls, so every call must carry its own
             * D3DRECTPATCH_INFO/D3DTRIPATCH_INFO. QUINTICRTPATCHES stays absent
             * because only LINEAR/QUADRATIC/CUBIC are evaluated. */
            | D3DDEVCAPS_RTPATCHES
            | D3DDEVCAPS_RTPATCHHANDLEZERO
            /* N-patches: SetNPatchMode above 1 makes every following triangle
             * draw a PN triangle, evaluated in the guest and drawn as an
             * ordinary indexed list. D3DRS_POSITIONDEGREE and
             * D3DRS_NORMALDEGREE are both honoured. */
            | D3DDEVCAPS_NPATCHES;
    caps->PrimitiveMiscCaps = D3DPMISCCAPS_CULLNONE
            | D3DPMISCCAPS_CULLCW | D3DPMISCCAPS_CULLCCW
            | D3DPMISCCAPS_COLORWRITEENABLE | D3DPMISCCAPS_BLENDOP
            | D3DPMISCCAPS_SEPARATEALPHABLEND
            | D3DPMISCCAPS_INDEPENDENTWRITEMASKS
            /* D3DRS_ZWRITEENABLE reaches the host and drives the depth-stencil
             * state's depthWriteEnabled.  Reporting no MASKZ told apps to keep
             * depth writes on for passes built around turning them off --
             * particle and decal passes above all. */
            | D3DPMISCCAPS_MASKZ
            /* Pre-transformed (XYZRHW) geometry goes through the same render
             * pass, viewport and scissor rectangle as everything else, so it is
             * clipped rather than requiring the app to pre-clip it. */
            | D3DPMISCCAPS_CLIPTLVERTS
            /* M3: D3DTA_TEMP as a stage argument and D3DTSS_RESULTARG
             * choosing it, both implemented by the host's cascade. */
            | D3DPMISCCAPS_TSSARGTEMP;
    caps->RasterCaps = D3DPRASTERCAPS_ZTEST | D3DPRASTERCAPS_DEPTHBIAS
            | D3DPRASTERCAPS_SLOPESCALEDEPTHBIAS
            | D3DPRASTERCAPS_FOGVERTEX | D3DPRASTERCAPS_FOGTABLE
            | D3DPRASTERCAPS_WFOG | D3DPRASTERCAPS_FOGRANGE
            /* M3: SetScissorRect reaches the host and its render pass. */
            | D3DPRASTERCAPS_SCISSORTEST
            /* WGSL interpolates varyings perspective-correct unless a stage is
             * declared @interpolate(linear), and none are. */
            | D3DPRASTERCAPS_COLORPERSPECTIVE
            /* D3DSAMP_MIPMAPLODBIAS. WebGPU samplers have no bias field, so the
             * host applies it at the sample call instead -- textureSampleBias
             * in both the fixed-function cascade and a translated pixel shader,
             * baked as a literal and quantised to 1/16 of a level so a title
             * that animates the bias cannot mint a pipeline per frame. */
            | D3DPRASTERCAPS_MIPMAPLODBIAS;
    caps->ZCmpCaps = 0xFFu;
    /* ZERO through BOTHINVSRCALPHA plus the BLENDFACTOR pair. The host maps
     * BOTH* to its resolved source/destination pair and installs WebGPU's
     * dynamic blend constant for BLENDFACTOR/INVBLENDFACTOR. */
    caps->SrcBlendCaps = 0x3FFFu;
    caps->DestBlendCaps = 0x3FFFu;
    caps->AlphaCmpCaps = 0xFFu;
    caps->StencilCaps = D3DSTENCILCAPS_KEEP | D3DSTENCILCAPS_ZERO
            | D3DSTENCILCAPS_REPLACE | D3DSTENCILCAPS_INCRSAT
            | D3DSTENCILCAPS_DECRSAT | D3DSTENCILCAPS_INVERT
            | D3DSTENCILCAPS_INCR | D3DSTENCILCAPS_DECR
            /* D3DRS_TWOSIDEDSTENCILMODE and the four D3DRS_CCW_STENCIL* states
             * are consumed by the host and become WebGPU's stencilBack face.
             * Shadow-volume renderers pick a two-pass fallback without this. */
            | D3DSTENCILCAPS_TWOSIDED;
    caps->ShadeCaps = D3DPSHADECAPS_COLORGOURAUDRGB
            /* The fixed-function host computes per-vertex material/light
             * specular when D3DRS_SPECULARENABLE is set and adds COLOR1 after
             * the texture cascade. GTA SA's car environment-map pipeline
             * explicitly requires this bit; omitting it makes
             * CCarFXRenderer::Initialise fail during the legal screen and the
             * game's frontend loader then exits cleanly. */
            | D3DPSHADECAPS_SPECULARGOURAUDRGB
            | D3DPSHADECAPS_FOGGOURAUD | D3DPSHADECAPS_ALPHAGOURAUDBLEND;
    caps->TextureCaps = D3DPTEXTURECAPS_ALPHA | D3DPTEXTURECAPS_MIPMAP
            | D3DPTEXTURECAPS_PERSPECTIVE
            /* M3: real IDirect3DCubeTexture9, six faces per level, sampled
             * through a texture_cube<f32> by both the fixed-function cascade
             * and a translated pixel shader's `dcl_cube`. NONPOW2CONDITIONAL
             * and CUBEMAP_POW2 stay absent: WebGPU has no such restriction, so
             * claiming one would only make apps pad textures for nothing. */
            | D3DPTEXTURECAPS_CUBEMAP
            /* CreateCubeTexture takes a level count and the host allocates the
             * cube with that mipLevelCount, same as a 2D texture. */
            | D3DPTEXTURECAPS_MIPCUBEMAP
            | D3DPTEXTURECAPS_VOLUMEMAP | D3DPTEXTURECAPS_MIPVOLUMEMAP
            /* D3DTTFF_PROJECTED is honoured: the host divides the texture
             * coordinate by its last component before sampling. */
            | D3DPTEXTURECAPS_PROJECTED;
    caps->TextureFilterCaps = D3DPTFILTERCAPS_MINFPOINT
            | D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MAGFPOINT
            | D3DPTFILTERCAPS_MAGFLINEAR | D3DPTFILTERCAPS_MIPFPOINT
            | D3DPTFILTERCAPS_MIPFLINEAR
            /* Paired with MaxAnisotropy below: D3DTEXF_ANISOTROPIC resolves to
             * linear filtering plus the sampler's anisotropy value.  Claiming a
             * MaxAnisotropy of 16 while reporting no anisotropic filter type
             * would contradict itself. */
            | D3DPTFILTERCAPS_MINFANISOTROPIC
            | D3DPTFILTERCAPS_MAGFANISOTROPIC;
    /* Cube textures (M3) sample through the same filter path as 2D ones, so
     * advertising D3DPTEXTURECAPS_CUBEMAP while reporting no cube filtering
     * whatsoever contradicted the TextureCaps bit right above. */
    caps->CubeTextureFilterCaps = caps->TextureFilterCaps;
    caps->VolumeTextureFilterCaps = caps->TextureFilterCaps;
    caps->TextureAddressCaps = D3DPTADDRESSCAPS_WRAP
            | D3DPTADDRESSCAPS_MIRROR | D3DPTADDRESSCAPS_CLAMP
            /* WebGPU has no border-colour sampler, so the host clamps for the
             * physical sample and then substitutes D3DSAMP_BORDERCOLOR for every
             * coordinate that fell outside the unit domain on a BORDER axis --
             * a real implementation of the mode, not an approximation of it. */
            | D3DPTADDRESSCAPS_BORDER
            /* MIRRORONCE is exact rather than emulated: "mirror about zero,
             * then clamp" is a clamp-to-edge sampler -- which is what the host
             * already selects for the mode -- plus abs() on that axis of the
             * coordinate, applied in both the fixed-function cascade and a
             * translated pixel shader. */
            | D3DPTADDRESSCAPS_MIRRORONCE
            /* ADDRESSU, ADDRESSV and ADDRESSW are read and applied separately
             * per stage; nothing forces them to agree. */
            | D3DPTADDRESSCAPS_INDEPENDENTUV;
    /* device_stretch_rect() resolves its filter argument to point or linear. */
    caps->StretchRectFilterCaps = D3DPTFILTERCAPS_MINFPOINT
            | D3DPTFILTERCAPS_MAGFPOINT | D3DPTFILTERCAPS_MINFLINEAR
            | D3DPTFILTERCAPS_MAGFLINEAR;
    /* D3DPT_LINELIST/D3DPT_LINESTRIP reach the host's draw path, and lines
     * there are textured, depth-tested, blended and fogged like triangles. */
    caps->LineCaps = D3DLINECAPS_TEXTURE | D3DLINECAPS_ZTEST
            | D3DLINECAPS_BLEND | D3DLINECAPS_ALPHACMP | D3DLINECAPS_FOG;
    caps->TextureOpCaps = D3DTEXOPCAPS_DISABLE | D3DTEXOPCAPS_SELECTARG1
            | D3DTEXOPCAPS_SELECTARG2 | D3DTEXOPCAPS_MODULATE
            | D3DTEXOPCAPS_MODULATE2X | D3DTEXOPCAPS_MODULATE4X
            | D3DTEXOPCAPS_ADD | D3DTEXOPCAPS_ADDSIGNED
            | D3DTEXOPCAPS_ADDSIGNED2X | D3DTEXOPCAPS_SUBTRACT
            | D3DTEXOPCAPS_ADDSMOOTH | D3DTEXOPCAPS_BLENDDIFFUSEALPHA
            | D3DTEXOPCAPS_BLENDTEXTUREALPHA | D3DTEXOPCAPS_BLENDFACTORALPHA
            | D3DTEXOPCAPS_BLENDTEXTUREALPHAPM
            | D3DTEXOPCAPS_BLENDCURRENTALPHA | D3DTEXOPCAPS_DOTPRODUCT3
            | D3DTEXOPCAPS_MULTIPLYADD | D3DTEXOPCAPS_LERP
            /* Environment bump mapping: the host displaces the next stage's
             * coordinate by this stage's sampled (du, dv) through
             * D3DTSS_BUMPENVMAT00..11, and the luminance form additionally
             * modulates by BUMPENVLSCALE/BUMPENVLOFFSET. */
            | D3DTEXOPCAPS_BUMPENVMAP
            | D3DTEXOPCAPS_BUMPENVMAPLUMINANCE
            /* The four "modulate one channel set, add the other" operations,
             * emitted by textureOpExpression() in the host's cascade. D3D9
             * defines all four for D3DTSS_COLOROP only; the host refuses them
             * as an alpha operation, which is what D3D9 does too.
             * D3DTEXOPCAPS_PREMODULATE stays absent: it modulates against the
             * *next* stage's texture and nothing in the cascade carries a value
             * backwards. */
            | D3DTEXOPCAPS_MODULATEALPHA_ADDCOLOR
            | D3DTEXOPCAPS_MODULATECOLOR_ADDALPHA
            | D3DTEXOPCAPS_MODULATEINVALPHA_ADDCOLOR
            | D3DTEXOPCAPS_MODULATEINVCOLOR_ADDALPHA;
    /* WebGPU guarantees maxTextureDimension2D >= 8192 on every implementation,
     * and the host creates its device with the default limits, so 4096 was
     * half of what actually works. */
    caps->MaxTextureWidth = 8192;
    caps->MaxTextureHeight = 8192;
    caps->MaxVolumeExtent = 2048;
    caps->MaxTextureRepeat = 8192;
    caps->MaxTextureAspectRatio = 8192;
    /* D3DSAMP_MAXANISOTROPY reaches the host and becomes the WebGPU sampler's
     * maxAnisotropy (dropped only when the app's own filters are not all
     * linear, which WebGPU forbids combining with anisotropy).  16 is WebGPU's
     * ceiling; reporting 1 meant "no anisotropic filtering" and made apps grey
     * out the setting. */
    caps->MaxAnisotropy = 16;
    caps->MaxVertexW = 1.0e10f;
    caps->MaxPointSize = 256.0f;
    caps->MaxPrimitiveCount = 0xFFFFFu;
    caps->MaxVertexIndex = 0xFFFFFFu;
    caps->MaxStreams = D9_MAX_STREAMS;
    /* Advisory, and nothing in the proxy or the protocol clamps a stride --
     * SetStreamSource carries it as a uint32.  255 was the figure DX7-era parts
     * reported and it rejects perfectly ordinary skinned vertex formats. */
    caps->MaxStreamStride = 65535;
    /* M5 declaration formats beyond the baseline FLOATn/D3DCOLOR set. */
    caps->DeclTypes = D3DDTCAPS_UBYTE4 | D3DDTCAPS_UBYTE4N
            | D3DDTCAPS_SHORT2N | D3DDTCAPS_SHORT4N
            | D3DDTCAPS_USHORT2N | D3DDTCAPS_USHORT4N
            | D3DDTCAPS_UDEC3 | D3DDTCAPS_DEC3N
            | D3DDTCAPS_FLOAT16_2 | D3DDTCAPS_FLOAT16_4;
    if (shader_model) {
        caps->VertexShaderVersion = shader_model >= 3 ?
                (DWORD)D3DVS_VERSION(3, 0) : (DWORD)D3DVS_VERSION(2, 0);
        caps->MaxVertexShaderConst = D9_MAX_VS_CONST_F;
        caps->PixelShaderVersion = shader_model >= 3 ?
                (DWORD)D3DPS_VERSION(3, 0) : (DWORD)D3DPS_VERSION(2, 0);
        /* The largest absolute value a ps_1_x register can hold. 8.0 is what
         * SM1.4-class hardware reported; anything smaller makes an engine
         * pre-scale its constants. */
        caps->PixelShader1xMaxValue = 8.0f;
        caps->DevCaps |= D3DDEVCAPS_PUREDEVICE;
        /* r0..r31 and four levels of static nesting are what the translator
         * emits WGSL for; predication is handled by guarding the instruction. */
        caps->VS20Caps.Caps = D3DVS20CAPS_PREDICATION;
        caps->VS20Caps.DynamicFlowControlDepth = D3DVS20_MAX_DYNAMICFLOWCONTROLDEPTH;
        caps->VS20Caps.NumTemps = 32;
        caps->VS20Caps.StaticFlowControlDepth = 4;
        caps->PS20Caps.Caps = D3DPS20CAPS_ARBITRARYSWIZZLE
                | D3DPS20CAPS_GRADIENTINSTRUCTIONS
                | D3DPS20CAPS_PREDICATION
                | D3DPS20CAPS_NODEPENDENTREADLIMIT
                | D3DPS20CAPS_NOTEXINSTRUCTIONLIMIT;
        caps->PS20Caps.DynamicFlowControlDepth = 0; /* see the note above */
        caps->PS20Caps.NumTemps = 32;
        caps->PS20Caps.StaticFlowControlDepth = 4;
        caps->PS20Caps.NumInstructionSlots = 512;
        caps->MaxVShaderInstructionsExecuted = 65535;
        caps->MaxPShaderInstructionsExecuted = 65535;
        caps->MaxVertexShader30InstructionSlots = shader_model >= 3 ?
                32768 : 0;
        caps->MaxPixelShader30InstructionSlots = shader_model >= 3 ?
                32768 : 0;
        caps->VertexTextureFilterCaps = shader_model >= 3
                ? D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MINFLINEAR
                    | D3DPTFILTERCAPS_MAGFPOINT | D3DPTFILTERCAPS_MAGFLINEAR
                    | D3DPTFILTERCAPS_MIPFPOINT | D3DPTFILTERCAPS_MIPFLINEAR
                : 0;
    } else {
        caps->VertexShaderVersion = (DWORD)D3DVS_VERSION(0, 0);
        caps->MaxVertexShaderConst = 0;
        caps->PixelShaderVersion = (DWORD)D3DPS_VERSION(0, 0);
    }
    /* Eight texture coordinate sets, plus per-vertex point size: a declaration
     * carrying D3DDECLUSAGE_PSIZE (or the D3DFVF_PSIZE bit) is bound to the
     * host's point-size input and used in place of D3DRS_POINTSIZE. */
    caps->FVFCaps = (8u & D3DFVFCAPS_TEXCOORDCOUNTMASK) | D3DFVFCAPS_PSIZE;
    caps->MaxTextureBlendStages = D9_MAX_TEXTURE_STAGES;
    caps->MaxSimultaneousTextures = D9_MAX_TEXTURE_STAGES;
    caps->VertexProcessingCaps = D3DVTXPCAPS_TEXGEN
            | D3DVTXPCAPS_DIRECTIONALLIGHTS | D3DVTXPCAPS_POSITIONALLIGHTS
            | D3DVTXPCAPS_LOCALVIEWER
            /* All four of D3DRS_{DIFFUSE,SPECULAR,AMBIENT,EMISSIVE}MATERIALSOURCE
             * are consumed, so a vertex colour can stand in for any material
             * channel.  Without this bit an engine duplicates its meshes per
             * material instead of colouring them per vertex. */
            | D3DVTXPCAPS_MATERIALSOURCE7
            /* D3DTSS_TCI_SPHEREMAP. The host's fixed-function vertex stage
             * generates the classic sphere-map projection of the eye-space
             * reflection vector -- the same formula GL_SPHERE_MAP and wined3d
             * use, which is what titles authored their sphere-map art
             * against. */
            | D3DVTXPCAPS_TEXGEN_SPHEREMAP | D3DVTXPCAPS_TWEENING;
    caps->MaxActiveLights = 8;
    /* Implemented through interpolated distances plus fragment discard because
     * core WGSL has no user clip-distance builtin. */
    caps->MaxUserClipPlanes = reported_user_clip_planes();
    /* Fixed-function vertex blending: the host builds the weighted sum of
     * D3DTS_WORLDMATRIX(0..3) in its generated vertex stage, so 4 -- D3D9's own
     * maximum -- is now backed rather than claimed. Indexed blending is backed
     * too, and BLENDINDICES can name any of D3D9's 256 world matrices; the host
     * uploads a palette sized from the matrices the guest has actually set.
     *
     * Tweening independently interpolates POSITION1/NORMAL1 before the same
     * transforms, lighting, texgen, fog and clipping. The common CPU path and
     * real WebGPU pixels cover that separate mode before advertising it. */
    caps->MaxVertexBlendMatrices = 4;
    caps->MaxVertexBlendMatrixIndex = 255;
    /* The tessellation ceiling d9_draw_npatch() clamps to. It bounds the
     * scratch each draw allocates; 16-bit output indices bound it further for
     * a large mesh, and a draw that would overflow them is left flat. */
    caps->MaxNpatchTessellationLevel = (float)D9_PATCH_MAX_SEGMENTS;
    if (permissive) apply_permissive_caps(caps);
    /* Render targets and MRT: the host binds up to four colour attachments and
     * a translated pixel shader's oC0..oC3 reach them (M4 work brought forward
     * because a 2005-era D3D9 game renders most of its frame into textures). */
    caps->NumSimultaneousRTs = D9_MAX_RENDER_TARGETS;
}

static void fill_caps(D3DCAPS9 *caps)
{
    fill_caps_for_profile(caps, shader_model_version(), TRUE);
}

static UINT reset_buffer_owned_refs(D9Device *device, const void *iface, BOOL index_buffer)
{
    /* Stream/index slots retain storage without a public COM reference. */
    (void)device;(void)iface;(void)index_buffer;
    return 0;
}

static D9Surface *reset_bound_surface(D9Device *device, UINT slot)
{
    return slot < D9_MAX_RENDER_TARGETS ? device->render_target_surfaces[slot]
                                      : device->depth_stencil_surface;
}

static UINT reset_surface_owned_refs(D9Device *device, D9Surface *surface)
{
    UINT refs = 0;
    /* Internal target slots never discount a private surface public ref. */
    if (surface && surface_uses_private_bindings(surface))
        return 0;
    if (surface)
        for (UINT i = 0; i <= D9_MAX_RENDER_TARGETS; ++i)
            if (reset_bound_surface(device, i) == surface)
                ++refs;
    return refs;
}

static UINT reset_texture_owned_refs(D9Device *device, const void *iface, UINT kind)
{
    UINT refs = 0;
    for (UINT i = 0; i <= D9_MAX_RENDER_TARGETS; ++i) {
        D9Surface *surface = reset_bound_surface(device, i);
        if (!surface)
            continue;
        if (kind == D9WG_RESOURCE_TEXTURE_2D && surface->texture
                && &surface->texture->iface == iface) {
            if (!surface->texture_child) {
                /* A standalone target owns one backing-texture reference,
                 * independent of how many slots retain its surface. */
                UINT j;
                for (j = 0; j < i; ++j)
                    if (reset_bound_surface(device, j) == surface)
                        break;
                if (j == i && (UINT)InterlockedCompareExchange(&surface->refcount, 0, 0)
                        == (surface->swap_chain ? reset_surface_owned_refs(device, surface) : 0u))
                    ++refs;
            }
        }
    }
    return refs;
}

static BOOL device_has_reset_blockers(D9Device *device)
{
    for (D9SwapChain *chain = device->additional_swap_chains; chain;
            chain = chain->next_device_chain)
        if (InterlockedCompareExchange(&chain->refcount, 0, 0))
            return TRUE;
    if (device->implicit_back_buffer && d9wg_reset_resource_blocks(D3DPOOL_DEFAULT,
            (UINT)device->implicit_back_buffer->refcount,
            reset_surface_owned_refs(device, device->implicit_back_buffer),
            device->implicit_back_buffer->locked))
        return TRUE;
    if (device->implicit_depth_stencil && d9wg_reset_resource_blocks(D3DPOOL_DEFAULT,
            (UINT)device->implicit_depth_stencil->refcount,
            reset_surface_owned_refs(device, device->implicit_depth_stencil),
            device->implicit_depth_stencil->locked))
        return TRUE;
    for (D9VertexBuffer *buffer = device->vertex_buffers; buffer; buffer = buffer->next_device_resource)
        if (d9wg_reset_resource_blocks(buffer->pool,
                (UINT)InterlockedCompareExchange(&buffer->refcount, 0, 0),
                reset_buffer_owned_refs(device, &buffer->iface, FALSE), buffer->locked))
            return TRUE;
    for (D9IndexBuffer *buffer = device->index_buffers; buffer; buffer = buffer->next_device_resource)
        if (d9wg_reset_resource_blocks(buffer->pool,
                (UINT)InterlockedCompareExchange(&buffer->refcount, 0, 0),
                reset_buffer_owned_refs(device, &buffer->iface, TRUE), buffer->locked))
            return TRUE;
    for (D9Texture *texture = device->texture_resources; texture; texture = texture->next_device_resource) {
        BOOL locked = FALSE;
        for (UINT level = 0; level < texture->level_count; ++level)
            if (texture->levels[level].locked) {
                locked = TRUE;
                break;
            }
        if (d9wg_reset_resource_blocks(texture->pool,
                (UINT)InterlockedCompareExchange(&texture->refcount, 0, 0),
                reset_texture_owned_refs(device, &texture->iface, D9WG_RESOURCE_TEXTURE_2D), locked))
            return TRUE;
    }
    for (D9CubeTexture *texture = device->cube_textures; texture; texture = texture->next_device_resource) {
        BOOL locked = FALSE;
        for (UINT level = 0; level < texture->level_count * 6u; ++level)
            if (texture->levels[level].locked) {
                locked = TRUE;
                break;
            }
        if (d9wg_reset_resource_blocks(texture->pool,
                (UINT)InterlockedCompareExchange(&texture->refcount, 0, 0),
                reset_texture_owned_refs(device, &texture->iface, D9WG_RESOURCE_TEXTURE_CUBE), locked))
            return TRUE;
    }
    for (D9VolumeTexture *texture = device->volume_textures; texture; texture = texture->next_device_resource) {
        BOOL locked = FALSE;
        for (UINT level = 0; level < texture->level_count; ++level)
            if (texture->levels[level].locked) {
                locked = TRUE;
                break;
            }
        if (d9wg_reset_resource_blocks(texture->pool,
                (UINT)InterlockedCompareExchange(&texture->refcount, 0, 0),
                reset_texture_owned_refs(device, &texture->iface, D9WG_RESOURCE_TEXTURE_VOLUME), locked))
            return TRUE;
    }
    return FALSE;
}

/*
 * D3DCREATE_PUREDEVICE. A pure device keeps no shadow of the state a state
 * block can hold, so D3D9 fails every Get* that would read one back -- the
 * whole point of the flag is that the runtime stopped tracking. The list below
 * is exactly the one D3D9 documents.
 *
 * This proxy *does* keep that shadow (it needs it for Reset and for state
 * blocks), so answering these would be easy and would still be wrong: an app
 * that asked for a pure device and gets answers has been told its performance
 * hint was honoured when the tracking it paid to avoid is still happening, and
 * an app written against real hardware never sees success here.
 *
 * GetSoftwareVertexProcessing and GetNPatchMode are in D3D9's list too but
 * return BOOL and FLOAT, so they have no way to report the failure and keep
 * answering.
 */
static BOOL device_is_pure(const D9Device *device)
{
    return (device->creation.BehaviorFlags & D3DCREATE_PUREDEVICE) != 0;
}

#define PURE_DEVICE_REFUSE(device, name) \
    do { \
        if (device_is_pure(device)) { \
            HOSTLOG_REFUSED("%s is not available on a D3DCREATE_PUREDEVICE " \
                    "device, which keeps no readable copy of state-block " \
                    "state", name); \
            return TRACE_REFUSE(D3DERR_INVALIDCALL); \
        } \
    } while (0)

static void device_clear_bindings(D9Device *device)
{
    UINT index;
    for (index = 0; index < D9_MAX_STREAMS; ++index) {
        D9VertexBuffer *buffer = device->streams[index].buffer;
        device->streams[index].buffer = NULL;
        device->streams[index].stride = 0;
        device->streams[index].offset = 0;
        device->streams[index].frequency = 1u;
        if (buffer) vb_binding_release(buffer);
    }
    if (device->index_buffer) {
        D9IndexBuffer *buffer = device->index_buffer;
        device->index_buffer = NULL;
        ib_binding_release(buffer);
    }
    for (index = 0; index < D9_MAX_TEXTURE_BINDINGS; ++index) {
        D9Texture *texture = device->textures[index];
        D9CubeTexture *cube = device->cube_bindings[index];
        D9VolumeTexture *volume = device->volume_bindings[index];
        device->textures[index] = NULL;
        device->cube_bindings[index] = NULL;
        device->volume_bindings[index] = NULL;
        if (texture) texture_binding_release(texture);
        if (cube) cube_binding_release(cube);
        if (volume) volume_texture_binding_release(volume);
    }
    for (index = 0; index < D9_MAX_RENDER_TARGETS; ++index) {
        D9Surface *surface = device->render_target_surfaces[index];
        device->render_target_surfaces[index] = NULL;
        if (surface) surface_binding_release(surface);
    }
    if (device->depth_stencil_surface) {
        D9Surface *surface = device->depth_stencil_surface;
        device->depth_stencil_surface = NULL;
        surface_binding_release(surface);
    }
    device->depth_stencil_unbound = FALSE;
    if (device->vertex_declaration) {
        D9VertexDeclaration *decl = device->vertex_declaration;
        device->vertex_declaration = NULL;
        decl_binding_release(decl);
    }
    if (device->vertex_shader) {
        D9Shader *shader = device->vertex_shader;
        device->vertex_shader = NULL;
        shader_binding_release(shader);
    }
    if (device->pixel_shader) {
        D9Shader *shader = device->pixel_shader;
        device->pixel_shader = NULL;
        shader_binding_release(shader);
    }
    device->fvf = 0;
}

static void device_cancel_recording(D9Device *device)
{
    D9StateBlock *recording = device->recording_result;
    /* Detach before releasing resources: private-data callbacks may re-enter. */
    device->recording_result = NULL;
    if (recording) {
        IDirect3DStateBlock9_Release(&recording->iface);
    }
}

static void device_release_owned_references(D9Device *device)
{
    TRACE("ENTER Device.ReleaseOwnedReferences device=%08lX refs=%ld child_refs=%ld",
            device->handle,
            InterlockedCompareExchange(&device->refcount, 0, 0),
            InterlockedCompareExchange(&device->child_parent_refs, 0, 0));
    device_cancel_recording(device);
    device_clear_state_block_handles8(device);
    device_clear_bindings(device);
    device_clear_fvf_cache(device);
    TRACE("LEAVE Device.ReleaseOwnedReferences device=%08lX refs=%ld child_refs=%ld",
            device->handle,
            InterlockedCompareExchange(&device->refcount, 0, 0),
            InterlockedCompareExchange(&device->child_parent_refs, 0, 0));
}

/*
 * After a Reset the host has a new device under the new handle, with
 * default state; the resources -- buffers, textures, declarations, shaders --
 * are the session's, not the device's, and keep their handles and contents
 * there. They used to be created again under new handles and uploaded again,
 * without the old ones ever being destroyed: every Reset doubled the GPU
 * memory (review S1-9a). What the new device lacks is the shader constant
 * registers, sent unconditionally because the host's copy is empty, not
 * stale.
 */
static BOOL emit_device_state_after_reset(D9Device *device)
{
    if (!emit_render_default_patches(device->handle, device_compatibility_api(device), device->present.EnableAutoDepthStencil, device->render_states)
            || !emit_object_default_patches(device->handle, &device->material))
        return FALSE;

    if (device->gamma_ramp_set && !d9wg_emit_gamma_ramp(device->handle,
            device->gamma_ramp_flags, &device->gamma_ramp))
        return FALSE;

    if (!emit_shader_constants(device, D9WG_OP_SET_VERTEX_SHADER_CONSTANT_F,
                    0, D9_MAX_VS_CONST_F, device->vs_const_f,
                    D9_MAX_VS_CONST_F * 16u)
            || !emit_shader_constants(device, D9WG_OP_SET_PIXEL_SHADER_CONSTANT_F,
                    0, D9_MAX_PS_CONST_F, device->ps_const_f,
                    D9_MAX_PS_CONST_F * 16u)
            || !emit_shader_constants(device, D9WG_OP_SET_VERTEX_SHADER_CONSTANT_I,
                    0, D9_MAX_CONST_I, device->vs_const_i, D9_MAX_CONST_I * 16u)
            || !emit_shader_constants(device, D9WG_OP_SET_PIXEL_SHADER_CONSTANT_I,
                    0, D9_MAX_CONST_I, device->ps_const_i, D9_MAX_CONST_I * 16u))
        return FALSE;
    {
        uint32_t packed[D9_MAX_CONST_B];
        UINT index;
        for (index = 0; index < D9_MAX_CONST_B; ++index)
            packed[index] = device->vs_const_b[index] ? 1u : 0u;
        if (!emit_shader_constants(device, D9WG_OP_SET_VERTEX_SHADER_CONSTANT_B,
                0, D9_MAX_CONST_B, packed, D9_MAX_CONST_B * 4u))
            return FALSE;
        for (index = 0; index < D9_MAX_CONST_B; ++index)
            packed[index] = device->ps_const_b[index] ? 1u : 0u;
        if (!emit_shader_constants(device, D9WG_OP_SET_PIXEL_SHADER_CONSTANT_B,
                0, D9_MAX_CONST_B, packed, D9_MAX_CONST_B * 4u))
            return FALSE;
    }
    return TRUE;
}

static void device_child_add_ref(D9Device *device)
{
    InterlockedIncrement(&device->child_parent_refs);
    IDirect3DDevice9_AddRef(&device->iface);
}

static void device_child_release(D9Device *device)
{
    InterlockedDecrement(&device->child_parent_refs);
    IDirect3DDevice9_Release(&device->iface);
}

static BOOL device_light_is_set(D9Device *device, UINT index)
{
    return index < D9_MAX_LIGHTS && device->light_set[index];
}

static void device_init_states_for_api(D9Device *device, UINT api)
{
    UINT slot;
    UINT stage;
    device->current_palette = 0;

    /* Setters suppress equal values, so the shadow and host must start from
     * the same generated defaults, including creation-time depth state. */
    d9wg_clip_status_init(&device->clip_status);device->clip_status_valid = TRUE;
    ZeroMemory(device->render_states, sizeof(device->render_states));
    ZeroMemory(device->texture_stage_states,
            sizeof(device->texture_stage_states));
    ZeroMemory(device->sampler_states, sizeof(device->sampler_states));

    D3DCAPS9 caps;fill_caps(&caps);
    DWORD point_max_bits;CopyMemory(&point_max_bits, &caps.MaxPointSize, sizeof(point_max_bits));
    for (UINT state = 0; state < sizeof(device->render_states)/sizeof(device->render_states[0]); ++state)
        device->render_states[state] = d9wg_render_default(api, state,
                device->present.EnableAutoDepthStencil, point_max_bits);
    d9wg_object_defaults(api, &device->material, device->clip_planes, D9_MAX_CLIP_PLANES);
    d9wg_light_free(&device->extra_lights);
    ZeroMemory(device->lights, sizeof(device->lights));
    ZeroMemory(device->light_set, sizeof(device->light_set));
    ZeroMemory(device->light_enabled, sizeof(device->light_enabled));
    for (stage = 0; stage < D9_MAX_TEXTURE_STAGES; ++stage)
        for (UINT state = 0; state < sizeof(device->texture_stage_states[stage])/sizeof(device->texture_stage_states[stage][0]); ++state)
            device->texture_stage_states[stage][state] = d9wg_texture_default(api, stage, state);
    for (slot = 0; slot < D9_MAX_SAMPLERS; ++slot)
        for (UINT state = 0; state < sizeof(device->sampler_states[slot])/sizeof(device->sampler_states[slot][0]); ++state)
            device->sampler_states[slot][state] = d9wg_sampler_default(state);
    for (slot = 0; slot < D9_MAX_STREAMS; ++slot)
        device->streams[slot].frequency = 1u;
    for (slot = 0; slot < D9_MAX_TRANSFORMS; ++slot) {
        ZeroMemory(device->transforms[slot], sizeof(device->transforms[slot]));
        device->transforms[slot][0] = 1.0f;
        device->transforms[slot][5] = 1.0f;
        device->transforms[slot][10] = 1.0f;
        device->transforms[slot][15] = 1.0f;
    }
}

static void device_init_states(D9Device *device)
{
    device_init_states_for_api(device, device_compatibility_api(device));
}

/* ---- IDirect3D9 ---- */

static HRESULT WINAPI d3d_query_interface(IDirect3D9 *iface, REFIID iid,
        void **object)
{
    if (!object)
        return E_POINTER;
    *object = NULL;
    if (!iid || (!iid_is_unknown(iid) && !guid_equal(iid, &IID_IDirect3D9))) {
        /* A refused QueryInterface is how a title discovers there is no
         * IDirect3D9Ex here, and some give up on the spot. Name the GUID it
         * asked for rather than leaving a bare E_NOINTERFACE. */
        TRACE("FAIL Direct3D9.QueryInterface iid=%08lX-%04lX-%04lX -> %08lX",
                iid ? iid->Data1 : 0, iid ? (DWORD)iid->Data2 : 0,
                iid ? (DWORD)iid->Data3 : 0, (DWORD)E_NOINTERFACE);
        return E_NOINTERFACE;
    }
    *object = iface;
    IDirect3D9_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI d3d_add_ref(IDirect3D9 *iface)
{
    return (ULONG)InterlockedIncrement(&d3d_from_iface(iface)->refcount);
}

static ULONG WINAPI d3d_release(IDirect3D9 *iface)
{
    D9Direct3D *d3d = d3d_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&d3d->refcount);
    if (!refs) {
        HeapFree(GetProcessHeap(), 0, d3d);
    }
    return refs;
}

static HRESULT WINAPI d3d_register_software_device(IDirect3D9 *iface,
        void *initialize)
{
    (void)iface; (void)initialize;

    TRACE("FAIL RegisterSoftwareDevice -> %08lX", (DWORD)D3DERR_INVALIDCALL);
    return D3DERR_INVALIDCALL;
}

static UINT WINAPI d3d_get_adapter_count(IDirect3D9 *iface)
{
    (void)iface;

    TRACE("OK GetAdapterCount -> 1");
    return 1;
}

static void fill_adapter_identifier8(D3DADAPTER_IDENTIFIER9 *identifier, DWORD flags)
{
    lstrcpynA(identifier->Driver, "d3d8-webgpu", sizeof(identifier->Driver));
    lstrcpynA(identifier->Description,
            "v86 Direct3D 8 WebGPU Adapter", sizeof(identifier->Description));
    identifier->VendorId = 0x1234;
    identifier->DeviceId = 0x5686;
    identifier->SubSysId = 0x56861234;
    identifier->Revision = 1;
    identifier->DriverVersion.HighPart = (6 << 16) | 14;
    identifier->DriverVersion.LowPart = (10 << 16) | 6764;
    identifier->DeviceIdentifier.Data1 = 0xD8E60000u | identifier->DeviceId;
    identifier->DeviceIdentifier.Data2 = (WORD)identifier->VendorId;
    identifier->DeviceIdentifier.Data3 = (WORD)identifier->DeviceId;
    identifier->DeviceIdentifier.Data4[0] = 0x9A;
    identifier->DeviceIdentifier.Data4[1] = 0xB1;
    identifier->DeviceIdentifier.Data4[2] = 0xC2;
    identifier->DeviceIdentifier.Data4[3] = 0xD3;
    identifier->DeviceIdentifier.Data4[4] = 0xE4;
    identifier->DeviceIdentifier.Data4[5] = 0xF5;
    identifier->DeviceIdentifier.Data4[6] = 0x06;
    identifier->DeviceIdentifier.Data4[7] = 0x17;
    /* The same bit means NO_WHQL in D3D8 and WHQL_LEVEL in D3D9. */
    identifier->WHQLLevel = (flags & D3DENUM_WHQL_LEVEL) ? 0 : 1;
}

static HRESULT WINAPI d3d_get_adapter_identifier(IDirect3D9 *iface,
        UINT adapter, DWORD flags, D3DADAPTER_IDENTIFIER9 *identifier)
{
    (void)iface; (void)flags;

    TRACE("CALL GetAdapterIdentifier adapter=%lu flags=%08lX", adapter, flags);
    if (adapter || !identifier) {
        TRACE("FAIL GetAdapterIdentifier -> %08lX", (DWORD)D3DERR_INVALIDCALL);
        return D3DERR_INVALIDCALL;
    }
    ZeroMemory(identifier, sizeof(*identifier));
    /*
     * Fields other than the identity triple are the same in every variant:
     * no real driver reports an all-zero DriverVersion, an all-zero
     * DeviceIdentifier GUID (apps cache it to notice a driver change) or an
     * empty DeviceName, and a caller that sanity-checks the struct would
     * reject those regardless of which card we claim to be.
     */
    lstrcpynA(identifier->DeviceName, "\\\\.\\DISPLAY1",
            sizeof(identifier->DeviceName));
    if (d3d_from_iface(iface)->compatibility_api == 8) {
        fill_adapter_identifier8(identifier, flags);
        return D3D_OK;
    }
#if D9_ADAPTER_IDENTITY == D9_ADAPTER_GEFORCEFX_5200
    /* NV34. The entry-level card of the first NVIDIA generation with
     * vs_2_0/ps_2_0, matching the shader model fill_caps() reports from M2
     * on. */
    lstrcpynA(identifier->Driver, "nv4_disp.dll", sizeof(identifier->Driver));
    lstrcpynA(identifier->Description, "NVIDIA GeForce FX 5200",
            sizeof(identifier->Description));
    identifier->VendorId = 0x10DE;
    identifier->DeviceId = 0x0322;
    identifier->SubSysId = 0x032210DE;
    identifier->Revision = 0xA1;
    /* ForceWare 66.93, a real XP driver revision for this card. */
    identifier->DriverVersion.HighPart = (6 << 16) | 14;
    identifier->DriverVersion.LowPart = (10 << 16) | 6693;
#elif D9_ADAPTER_IDENTITY == D9_ADAPTER_GEFORCE4_MX
    /* NV17. Hardware T&L, no programmable shaders -- the one widely-known
     * card whose real capabilities match what fill_caps() reports. */
    lstrcpynA(identifier->Driver, "nv4_disp.dll", sizeof(identifier->Driver));
    lstrcpynA(identifier->Description, "NVIDIA GeForce4 MX 440",
            sizeof(identifier->Description));
    identifier->VendorId = 0x10DE;
    identifier->DeviceId = 0x0171;
    identifier->SubSysId = 0x017110DE;
    identifier->Revision = 0xA3;
    /* ForceWare 45.23, a real driver revision for this card on XP. */
    identifier->DriverVersion.HighPart = (6 << 16) | 14;
    identifier->DriverVersion.LowPart = (10 << 16) | 4523;
#elif D9_ADAPTER_IDENTITY == D9_ADAPTER_VMWARE_SVGA
    lstrcpynA(identifier->Driver, "vm3dum.dll", sizeof(identifier->Driver));
    lstrcpynA(identifier->Description, "VMware SVGA 3D",
            sizeof(identifier->Description));
    identifier->VendorId = 0x15AD;
    identifier->DeviceId = 0x0405;
    identifier->SubSysId = 0x040515AD;
    identifier->Revision = 0;
    identifier->DriverVersion.HighPart = (6 << 16) | 14;
    identifier->DriverVersion.LowPart = (1 << 16) | 1264;
#else
    lstrcpynA(identifier->Driver, "d3d9-webgpu.dll",
            sizeof(identifier->Driver));
    lstrcpynA(identifier->Description, "v86 Direct3D 9 WebGPU Adapter",
            sizeof(identifier->Description));
    identifier->VendorId = 0x1234;
    identifier->DeviceId = 0x5687;
    identifier->SubSysId = 0x56871234;
    identifier->Revision = 1;
    /* product.version.subversion.build packed as two DWORDs, the way real
     * drivers report it (here 6.14.10.6764, a plausible XP-era driver). */
    identifier->DriverVersion.HighPart = (6 << 16) | 14;
    identifier->DriverVersion.LowPart = (10 << 16) | 6764;
#endif
    /* Stable and non-zero; derived from the identity so switching variants
     * also looks like a different driver, which is what an app caching this
     * field would expect. */
    identifier->DeviceIdentifier.Data1 = 0xD9E60000u | identifier->DeviceId;
    identifier->DeviceIdentifier.Data2 = (WORD)identifier->VendorId;
    identifier->DeviceIdentifier.Data3 = (WORD)identifier->DeviceId;
    identifier->DeviceIdentifier.Data4[0] = 0x9A;
    identifier->DeviceIdentifier.Data4[1] = 0xB1;
    identifier->DeviceIdentifier.Data4[2] = 0xC2;
    identifier->DeviceIdentifier.Data4[3] = 0xD3;
    identifier->DeviceIdentifier.Data4[4] = 0xE4;
    identifier->DeviceIdentifier.Data4[5] = 0xF5;
    identifier->DeviceIdentifier.Data4[6] = 0x06;
    identifier->DeviceIdentifier.Data4[7] = 0x17;
    /* 1 = WHQL-signed but no date info, rather than 0 = "not certified",
     * which some titles treat as an unusable/blacklisted driver. */
    identifier->WHQLLevel = 1;

    TRACE("OK GetAdapterIdentifier vendor=%04lX device=%04lX desc=%s",
            identifier->VendorId, identifier->DeviceId,
            identifier->Description);
    return D3D_OK;
}

/*
 * The adapter's mode list, enumerated from the guest display driver rather than
 * invented. It used to be a fixed three entries (640x480, 800x600, 1024x768),
 * and that is not a cosmetic understatement: San Andreas stores the chosen
 * video mode in gta_sa.set as an *index into this list*, not as a resolution.
 * Run the title once against a d3d9.dll that offers a longer list (SwiftShader
 * reports 18 modes per format), let it save an index valid there, and the next
 * run against a three-entry list finds that index out of range and destroys its
 * windows during enumeration -- before CreateDevice, before drawing anything.
 *
 * EnumDisplaySettings is the right source because it is also the constraint on
 * the other end: exclusive fullscreen calls ChangeDisplaySettings, so a mode
 * this list advertises has to be one the guest can actually be switched into.
 * Offering a mode the driver does not have would trade this failure for a
 * failed mode change.
 *
 * One walk serves both entry points, so the count and the index-to-mode mapping
 * cannot drift apart: pass out=NULL to count, or a mode to fetch index `wanted`.
 */
static UINT enumerate_adapter_modes(D3DFORMAT format, UINT wanted,
        D3DDISPLAYMODE *out)
{
    DEVMODEA current;
    DWORD bits_per_pixel = (format == D3DFMT_R5G6B5) ? 16u : 32u;
    DWORD index = 0;
    UINT matched = 0;

    for (;;) {
        ZeroMemory(&current, sizeof(current));
        current.dmSize = sizeof(current);
        if (!EnumDisplaySettingsA(NULL, index++, &current))
            break;
        if (current.dmBitsPerPel != bits_per_pixel)
            continue;
        if (!current.dmPelsWidth || !current.dmPelsHeight)
            continue;
        if (out && matched == wanted) {
            fill_display_mode(out, current.dmPelsWidth, current.dmPelsHeight,
                    format);
            /* 0 and 1 are the driver's "default/unspecified" encodings. */
            if (current.dmDisplayFrequency > 1)
                out->RefreshRate = current.dmDisplayFrequency;
            return matched + 1;
        }
        ++matched;
    }
    /* Counting returns the complete list length. Fetching must instead report
     * whether the requested index was found. Returning `matched` here made
     * every index past the end look successful whenever the adapter had at
     * least one mode; Warcraft III consequently enumerated forever and never
     * reached CreateDevice. */
    return out ? 0 : matched;
}

/* Private D3D8 factories expose the legacy flattened list through format
 * UNKNOWN. Ordinary D3D9 factories retain driver-enumerated, filtered modes. */
static UINT enumerate_adapter_modes8(UINT index, D3DDISPLAYMODE *mode)
{
    static const struct {
        UINT width;
        UINT height;
        D3DFORMAT format;
    } modes[] = {
        { 640, 480, D3DFMT_X1R5G5B5 },
        { 640, 480, D3DFMT_R5G6B5 },
        { 640, 480, D3DFMT_X8R8G8B8 },
        { 800, 600, D3DFMT_X1R5G5B5 },
        { 800, 600, D3DFMT_R5G6B5 },
        { 800, 600, D3DFMT_X8R8G8B8 },
        { 1024, 768, D3DFMT_X1R5G5B5 },
        { 1024, 768, D3DFMT_R5G6B5 },
        { 1024, 768, D3DFMT_X8R8G8B8 }
    };
    const UINT count = sizeof(modes) / sizeof(modes[0]);
    if (!mode) return count;
    if (index >= count) return 0;
    fill_display_mode(mode, modes[index].width, modes[index].height, modes[index].format);
    return 1;
}

static UINT WINAPI d3d_get_adapter_mode_count(IDirect3D9 *iface, UINT adapter,
        D3DFORMAT format)
{
    UINT count;
    if (d3d_from_iface(iface)->compatibility_api == 8)
        return adapter || format != D3DFMT_UNKNOWN ? 0 : enumerate_adapter_modes8(0, NULL);

    if (adapter || (format != D3DFMT_X8R8G8B8 && format != D3DFMT_R5G6B5)) {
        /* A zero here empties the app's video-mode list for that format, which
         * is a common reason to abort startup before drawing anything. */
        TRACE("OK GetAdapterModeCount adapter=%lu format=%08lX -> 0", adapter,
                (DWORD)format);
        return 0;
    }
    count = enumerate_adapter_modes(format, 0, NULL);
    TRACE("OK GetAdapterModeCount adapter=%lu format=%08lX -> %lu", adapter,
            (DWORD)format, count);
    return count;
}

static HRESULT WINAPI d3d_enum_adapter_modes(IDirect3D9 *iface, UINT adapter,
        D3DFORMAT format, UINT index, D3DDISPLAYMODE *mode)
{
    if (d3d_from_iface(iface)->compatibility_api == 8) {
        if (adapter || !mode || format != D3DFMT_UNKNOWN
                || !enumerate_adapter_modes8(index, mode))
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        return D3D_OK;
    }

    if (adapter || !mode || (format != D3DFMT_X8R8G8B8
            && format != D3DFMT_R5G6B5)
            || !enumerate_adapter_modes(format, index, mode)) {
        TRACE("FAIL EnumAdapterModes adapter=%lu format=%08lX index=%lu -> %08lX",
                adapter, (DWORD)format, index, (DWORD)D3DERR_INVALIDCALL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    TRACE("OK EnumAdapterModes format=%08lX index=%lu -> %lux%lu refresh=%lu",
            (DWORD)format, index, mode->Width, mode->Height,
            mode->RefreshRate);
    return D3D_OK;
}

static HRESULT WINAPI d3d_get_adapter_display_mode(IDirect3D9* iface,
    UINT adapter, D3DDISPLAYMODE* mode)
{
    (void)iface;

    if (adapter || !mode)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    D9WGDisplayMode current;
    d9wg_display_mode_current(&current);
    fill_display_mode(mode, current.width, current.height, (D3DFORMAT)current.format);
    mode->RefreshRate = current.refresh_rate;
    TRACE("OK GetAdapterDisplayMode -> %lux%lu format=%08lX refresh=%lu",
        mode->Width, mode->Height, (DWORD)mode->Format, mode->RefreshRate);
    return D3D_OK;
}

/* Version8 query policy is a restriction of the common implemented formats.
 * Legacy device/resource code still calls its local predicates until wrapped. */
static BOOL supported_render_target_format8(D3DFORMAT format)
{
    return (format == D3DFMT_A8R8G8B8 || format == D3DFMT_X8R8G8B8
            || format == D3DFMT_R5G6B5 || format == D3DFMT_X1R5G5B5
            || format == D3DFMT_A1R5G5B5 || format == D3DFMT_A4R4G4B4)
            && supported_render_target_format(format);
}

static BOOL supported_depth_stencil_format8(D3DFORMAT format)
{
    return (format == D3DFMT_D16 || format == D3DFMT_D24S8
            || format == D3DFMT_D32 || format == D3DFMT_D15S1
            || format == D3DFMT_D24X8 || format == D3DFMT_D24X4S4)
            && supported_depth_stencil_format(format);
}

static HRESULT WINAPI d3d_check_device_type(IDirect3D9 *iface, UINT adapter,
        D3DDEVTYPE type, D3DFORMAT display_format,
        D3DFORMAT backbuffer_format, WINBOOL windowed)
{
    BOOL ok;
    BOOL legacy8 = d3d_from_iface(iface)->compatibility_api == 8;
    (void)windowed;
    ok = !adapter && type == D3DDEVTYPE_HAL
            && (display_format == D3DFMT_X8R8G8B8
                || display_format == D3DFMT_R5G6B5
                || (legacy8 && display_format == D3DFMT_X1R5G5B5))
            && (legacy8 ? supported_render_target_format8(backbuffer_format)
                : supported_backbuffer_format(backbuffer_format));

    TRACE("OK CheckDeviceType adapter=%lu type=%lu display=%08lX backbuffer=%08lX "
            "windowed=%lu -> %08lX", adapter, (DWORD)type,
            (DWORD)display_format, (DWORD)backbuffer_format, (DWORD)windowed,
            (DWORD)(ok ? D3D_OK : D3DERR_NOTAVAILABLE));
    return ok ? D3D_OK : D3DERR_NOTAVAILABLE;
}

static BOOL supported_format8(D3DFORMAT format, BOOL image_surface)
{
    UINT width, height, bytes;
    return d9wg_format_layout(8, (uint32_t)format, &width, &height, &bytes)
            && (!image_surface || (width == 1 && height == 1));
}

static HRESULT WINAPI d3d_check_device_format8(IDirect3D9 *iface,
        UINT adapter, D3DDEVTYPE type, D3DFORMAT adapter_format,
        DWORD usage, D3DRESOURCETYPE resource_type, D3DFORMAT format)
{
    (void)iface;
    (void)adapter_format;
    if (adapter || type != D3DDEVTYPE_HAL)
        return TRACE_REFUSE(D3DERR_NOTAVAILABLE);
    if (resource_type == D3DRTYPE_TEXTURE
            && !(usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL))
            && supported_format8(format, FALSE))
        return D3D_OK;
    if (resource_type == D3DRTYPE_CUBETEXTURE
            && !(usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL))
            && supported_format8(format, FALSE))
        return D3D_OK;
    if (resource_type == D3DRTYPE_CUBETEXTURE
            && usage == D3DUSAGE_RENDERTARGET
            && supported_render_target_format8(format))
        return D3D_OK;
    /* Volumes have no block-compressed layout on this path, and are never
     * render targets -- device_create_volume_texture refuses both. */
    if (resource_type == D3DRTYPE_VOLUMETEXTURE
            && !(usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL))
            && format != D3DFMT_DXT1 && format != D3DFMT_DXT2
            && format != D3DFMT_DXT3 && format != D3DFMT_DXT4
            && format != D3DFMT_DXT5
            && supported_format8(format, FALSE))
        return D3D_OK;
    if (resource_type == D3DRTYPE_TEXTURE
            && usage == D3DUSAGE_RENDERTARGET
            && supported_render_target_format8(format))
        return D3D_OK;
    /*
     * 3DMark2001 validates every enumerated display mode with this exact
     * query before it exposes the adapter:
     *
     *   CheckDeviceFormat(display_format, D3DUSAGE_RENDERTARGET,
     *           D3DRTYPE_SURFACE, display_format)
     *
     * A render-target surface is the object returned by CreateRenderTarget,
     * which this frontend backs with the same one-level target texture as the
     * texture case above. Omitting SURFACE here therefore hid a capability the
     * device really has and made 3DMark reject every 32-bit display mode as
     * non-renderable before CreateDevice was ever reached.
     */
    if (resource_type == D3DRTYPE_SURFACE
            && usage == D3DUSAGE_RENDERTARGET
            && supported_render_target_format8(format))
        return D3D_OK;
    if (resource_type == D3DRTYPE_SURFACE
            && (usage & D3DUSAGE_DEPTHSTENCIL)
            && supported_depth_stencil_format8(format))
        return D3D_OK;
    if (resource_type == D3DRTYPE_SURFACE
            && !(usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL))
            && supported_format8(format, TRUE))
        return D3D_OK;
    TRACE("CHECK_FORMAT REFUSE adapter=%lu type=%lu adapter_fmt=%08lX "
            "usage=%08lX resource_type=%lu format=%08lX", adapter,
            (DWORD)type, (DWORD)adapter_format, usage, (DWORD)resource_type,
            (DWORD)format);
    return TRACE_REFUSE(D3DERR_NOTAVAILABLE);
}

static HRESULT WINAPI d3d_check_device_format(IDirect3D9 *iface,
        UINT adapter, D3DDEVTYPE type, D3DFORMAT adapter_format,
        DWORD usage, D3DRESOURCETYPE resource_type, D3DFORMAT format)
{
    BOOL ok = FALSE;
    HRESULT result;
    if (d3d_from_iface(iface)->compatibility_api == 8)
        return d3d_check_device_format8(iface, adapter, type, adapter_format,
                usage, resource_type, format);
    (void)adapter_format;
    /* 3DMark05's 0x0046F240 probe stops looking for filtered depth textures
     * when '-PCF' succeeds. Our depth comparison sampler supports PCF, so
     * NOTAVAILABLE is the expected answer here. Keep this query out of the
     * generic refusal warning; never advertise it as a creatable format. */
    if (!adapter && type == D3DDEVTYPE_HAL && !usage
            && resource_type == D3DRTYPE_TEXTURE && format == D9FOURCC_NO_PCF) {
        TRACE("PROBE CheckDeviceFormat -PCF -> %08lX (expected negative probe)",
                (DWORD)D3DERR_NOTAVAILABLE);
        HOSTLOG_INFO("CheckDeviceFormat -PCF: negative capability probe; "
                "depth comparison filtering is available");
        return D3DERR_NOTAVAILABLE;
    }
    if (!adapter && type == D3DDEVTYPE_HAL) {
        /* D3DUSAGE_QUERY_* bits exist only in this call and never reach a
         * Create*, so they are answered here and masked off before the shared
         * creation predicate sees the usage. D3DUSAGE_AUTOGENMIPMAP is masked
         * for the same reason from the other direction: the create paths
         * accept a valid 2D/cube request even if generation is unavailable.
         * Only the query returns D3DOK_NOAUTOGEN; creation returns D3D_OK
         * with a single accessible level in either case. */
        const DWORD query_bits = D3DUSAGE_QUERY_LEGACYBUMPMAP
                | D3DUSAGE_QUERY_SRGBREAD | D3DUSAGE_QUERY_FILTER
                | D3DUSAGE_QUERY_SRGBWRITE
                | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING
                | D3DUSAGE_QUERY_VERTEXTEXTURE | D3DUSAGE_QUERY_WRAPANDMIP;
        /* Behaviours this backend does not implement. Unlike the create paths,
         * the query may safely refuse them: an app told no here picks something
         * else, whereas an app refused a create has nowhere to go. Legacy
         * bump-map formats are exposed as ordinary sampled textures rather than
         * through the fixed-function bump pipeline; displacement mapping and
         * N-patch tessellation do not exist here at all.
         *
         * D3DUSAGE_AUTOGENMIPMAP is no longer among them: the host allocates
         * the chain behind the app's single visible level and refills it by
         * blitting each level from the one above whenever level 0 changes.
         * Volume textures are the exception, and D3D9 does not support
         * automatic generation for those either. */
        /* D3DUSAGE_DMAP stays unimplemented: displacement mapping samples a
         * texture *during* tessellation, and nothing here can. D3DUSAGE_
         * NPATCHES is gone from this list -- N-patch tessellation is
         * implemented, and a buffer created with the usage is otherwise an
         * ordinary vertex buffer. */
        const DWORD unimplemented = D3DUSAGE_QUERY_LEGACYBUMPMAP
                | D3DUSAGE_DMAP
                | (((resource_type != D3DRTYPE_TEXTURE && resource_type != D3DRTYPE_CUBETEXTURE)
                        || (usage & D3DUSAGE_DYNAMIC)) ? D3DUSAGE_AUTOGENMIPMAP : 0u);
        const DWORD creation_usage = usage
                & ~(query_bits | D3DUSAGE_AUTOGENMIPMAP
                    | D3DUSAGE_SOFTWAREPROCESSING);
        BOOL renderable = supported_render_target_format(format);
        if (!(usage & unimplemented)
                && texture_create_supported(resource_type, creation_usage,
                        format, D3DPOOL_DEFAULT)
                && d9wg_format_query_supported(resource_type, usage, format,
                        D9WG_HOST_FEATURE_MASK, shader_model_version(), renderable)) {
            uint32_t host_features = 0;
            uint32_t info = d9wg_format_query_info(format);
            if ((info & 32u) || ((info & 4u)
                    && (usage & (D3DUSAGE_QUERY_FILTER | D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING)))) {
                host_features = runtime_host_features();
            }
            ok = d9wg_format_query_supported(resource_type, usage, format,
                    host_features, shader_model_version(), renderable);
        }
    }
    result = ok ? D3D_OK : D3DERR_NOTAVAILABLE;
    if (ok && (usage & D3DUSAGE_AUTOGENMIPMAP)
            && !d9wg_autogen_mips_supported(resource_type, format))
        result = D3DOK_NOAUTOGEN;
    TRACE("%s CheckDeviceFormat adapter=%lu type=%lu adapter_fmt=%08lX "
            "usage=%08lX resource_type=%lu format=%08lX -> %08lX",
            SUCCEEDED(result) ? "OK" : "FAIL", adapter, (DWORD)type,
            (DWORD)adapter_format, usage, (DWORD)resource_type,
            (DWORD)format, (DWORD)result);
    /*
     * D3DERR_NOTAVAILABLE is a legitimate answer, not a refusal, which is
     * exactly why it needs reporting: an app that asks whether it may use a
     * format and is told no does not fail, it quietly does without -- loads no
     * texture, draws no model, logs nothing. That is indistinguishable from
     * "the app never wanted to draw it", and the difference is the whole
     * question when content is missing with a clean command stream. Deduped by
     * text, so each distinct usage/type/format triple costs one line however
     * many times it is probed.
     */
    if (!ok)
        HOSTLOG_REFUSED("CheckDeviceFormat said no: format=%08lX usage=%08lX "
                "resource_type=%lu (an app told no here silently does "
                "without)", (DWORD)format, usage, (DWORD)resource_type);
    return result;
}

static HRESULT WINAPI d3d_check_multisample(IDirect3D9 *iface, UINT adapter,
        D3DDEVTYPE type, D3DFORMAT format, WINBOOL windowed,
        D3DMULTISAMPLE_TYPE multisample, DWORD *quality_levels)
{
    BOOL ok;
    (void)windowed;
    ok = !adapter && type == D3DDEVTYPE_HAL
            /* Version8 ignores the queried format; use a common colour format to
             * keep its sample-count policy without imposing D3D9 format checks. */
            && supported_multisample(d3d_from_iface(iface)->compatibility_api == 8
                    ? D3DFMT_A8R8G8B8 : format, multisample);
    if (quality_levels) *quality_levels = ok ? 1u : 0u;

    TRACE("OK CheckDeviceMultiSampleType adapter=%lu type=%lu format=%08lX "
            "multisample=%lu -> %08lX", adapter, (DWORD)type, (DWORD)format,
            (DWORD)multisample, (DWORD)(ok ? D3D_OK : D3DERR_NOTAVAILABLE));
    return ok ? D3D_OK : D3DERR_NOTAVAILABLE;
}

static HRESULT WINAPI d3d_check_depth_stencil(IDirect3D9 *iface,
        UINT adapter, D3DDEVTYPE type, D3DFORMAT adapter_format,
        D3DFORMAT render_format, D3DFORMAT depth_format)
{
    BOOL ok;
    (void)adapter_format; (void)render_format;
    ok = !adapter && type == D3DDEVTYPE_HAL
            && (d3d_from_iface(iface)->compatibility_api == 8
                    ? supported_depth_stencil_format8(depth_format)
                    : supported_depth_stencil_format(depth_format));

    TRACE("OK CheckDepthStencilMatch adapter=%lu type=%lu adapter_fmt=%08lX "
            "render_fmt=%08lX depth_fmt=%08lX -> %08lX", adapter, (DWORD)type,
            (DWORD)adapter_format, (DWORD)render_format, (DWORD)depth_format,
            (DWORD)(ok ? D3D_OK : D3DERR_NOTAVAILABLE));
    return ok ? D3D_OK : D3DERR_NOTAVAILABLE;
}

static HRESULT WINAPI d3d_check_device_format_conversion(IDirect3D9 *iface,
        UINT adapter, D3DDEVTYPE type, D3DFORMAT source, D3DFORMAT target)
{
    HRESULT result;
    (void)iface;
    if (adapter || type != D3DDEVTYPE_HAL)
        result = D3DERR_NOTAVAILABLE;
    else
        result = source == target ? D3D_OK : D3DERR_NOTAVAILABLE;
    TRACE("OK CheckDeviceFormatConversion source=%08lX target=%08lX -> %08lX",
            (DWORD)source, (DWORD)target, (DWORD)result);
    return result;
}

/*
 * Caps are the classic silent-abort surface: a title reads them once, decides
 * the adapter cannot run it, and exits without drawing or complaining. Logging
 * the fields games actually gate on turns that into something readable.
 */
static void trace_caps(const char *source, const D3DCAPS9 *caps)
{
    /* TRACE compiles away entirely in the ordinary d3d9.dll. */
    (void)source; (void)caps;
    TRACE("CAPS %s dev=%08lX vs=%08lX ps=%08lX blend_stages=%lu "
            "simultaneous_textures=%lu streams=%lu rts=%lu lights=%lu",
            source, caps->DevCaps, caps->VertexShaderVersion,
            caps->PixelShaderVersion, caps->MaxTextureBlendStages,
            caps->MaxSimultaneousTextures, caps->MaxStreams,
            caps->NumSimultaneousRTs, caps->MaxActiveLights);
    TRACE("CAPS %s texture=%08lX raster=%08lX src_blend=%08lX dst_blend=%08lX "
            "texture_op=%08lX max_texture=%lux%lu anisotropy=%lu "
            "primitives=%lu", source, caps->TextureCaps, caps->RasterCaps,
            caps->SrcBlendCaps, caps->DestBlendCaps, caps->TextureOpCaps,
            caps->MaxTextureWidth, caps->MaxTextureHeight, caps->MaxAnisotropy,
            caps->MaxPrimitiveCount);
    TRACE("CAPS %s filter=%08lX address=%08lX stencil=%08lX zcmp=%08lX "
            "shade=%08lX alpha_cmp=%08lX vtxp=%08lX fvf=%08lX decl=%08lX "
            "max_index=%08lX vs_const=%lu texture_repeat=%lu stride=%lu "
            "blend_matrices=%lu clip_planes=%lu caps2=%08lX",
            source, caps->TextureFilterCaps, caps->TextureAddressCaps,
            caps->StencilCaps, caps->ZCmpCaps, caps->ShadeCaps,
            caps->AlphaCmpCaps, caps->VertexProcessingCaps, caps->FVFCaps,
            caps->DeclTypes, caps->MaxVertexIndex, caps->MaxVertexShaderConst,
            caps->MaxTextureRepeat, caps->MaxStreamStride,
            caps->MaxVertexBlendMatrices, caps->MaxUserClipPlanes,
            caps->Caps2);
}

static HRESULT WINAPI d3d_get_device_caps(IDirect3D9 *iface, UINT adapter,
        D3DDEVTYPE type, D3DCAPS9 *caps)
{
    (void)iface;
    if (adapter || type != D3DDEVTYPE_HAL || !caps) {
        TRACE("FAIL Direct3D9.GetDeviceCaps adapter=%lu type=%lu -> %08lX",
                adapter, (DWORD)type, (DWORD)D3DERR_INVALIDCALL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    fill_caps(caps);
    trace_caps("Direct3D9.GetDeviceCaps", caps);
    return D3D_OK;
}

static HMONITOR WINAPI d3d_get_adapter_monitor(IDirect3D9 *iface, UINT adapter)
{
    HMONITOR monitor;
    (void)iface;
    if (adapter)
        return NULL;
    monitor = MonitorFromWindow(NULL, MONITOR_DEFAULTTOPRIMARY);
    TRACE("OK GetAdapterMonitor -> %08lX", (DWORD)(uintptr_t)monitor);
    return monitor;
}

static HRESULT WINAPI d3d_create_device(IDirect3D9* iface, UINT adapter,
    D3DDEVTYPE type, HWND focus_window, DWORD behavior,
    D3DPRESENT_PARAMETERS* parameters,
    IDirect3DDevice9** device_out)
{
    D9Direct3D* d3d = d3d_from_iface(iface);
    D9Device* device;
    UINT api;
    D9WGCreateDevice command;
    HWND window;
    RECT client;
    POINT origin;
    UINT width, height;
    D3DFORMAT backbuffer_format;

    TRACE("CALL CreateDevice adapter=%lu type=%lu focus=%08lX behavior=%08lX",
        adapter, (DWORD)type, (DWORD)(uintptr_t)focus_window, behavior);

    if (!device_out) {
        TRACE("FAIL CreateDevice missing output -> %08lX",
            (DWORD)D3DERR_INVALIDCALL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    *device_out = NULL;
    if (adapter || type != D3DDEVTYPE_HAL || !parameters) {
        TRACE("FAIL CreateDevice invalid arguments -> %08lX",
            (DWORD)D3DERR_INVALIDCALL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    api = d3d->compatibility_api == 8 ? 8u : 9u;
    TRACE("CreateDevice params %lux%lu fmt=%08lX count=%lu multisample=%lu "
          "quality=%lu swap=%lu windowed=%lu auto_depth=%lu "
          "depth_fmt=%08lX flags=%08lX interval=%08lX hwnd=%08lX",
        parameters->BackBufferWidth, parameters->BackBufferHeight,
        (DWORD)parameters->BackBufferFormat, parameters->BackBufferCount,
        (DWORD)parameters->MultiSampleType, parameters->MultiSampleQuality,
        (DWORD)parameters->SwapEffect, (DWORD)parameters->Windowed,
        (DWORD)parameters->EnableAutoDepthStencil,
        (DWORD)parameters->AutoDepthStencilFormat, parameters->Flags,
        parameters->PresentationInterval,
        (DWORD)(uintptr_t)parameters->hDeviceWindow);
    if (!d9wg_present_interval_valid(api, parameters->Windowed,
            parameters->PresentationInterval, D9WG_PRESENT_INTERVAL_MASK))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!d9wg_swap_effect_valid(api, parameters->SwapEffect, parameters->BackBufferCount))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!d9wg_present_multisample_valid(api, parameters->SwapEffect,
            parameters->MultiSampleType, parameters->MultiSampleQuality, parameters->Flags))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (api == 9 && !parameters->Windowed && parameters->BackBufferFormat == D3DFMT_UNKNOWN)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    backbuffer_format = api == 8
        ? (parameters->BackBufferFormat ? parameters->BackBufferFormat : D3DFMT_X8R8G8B8)
        : (D3DFORMAT)d9wg_display_mode_backbuffer_format(
        parameters->BackBufferFormat);
    if (!supported_multisample(backbuffer_format,
            parameters->MultiSampleType)) {
        TRACE("FAIL CreateDevice unsupported multisample=%lu quality=%lu "
              "format=%08lX -> %08lX",
            (DWORD)parameters->MultiSampleType,
            parameters->MultiSampleQuality, (DWORD)backbuffer_format,
            (DWORD)D3DERR_NOTAVAILABLE);
        return TRACE_REFUSE(D3DERR_NOTAVAILABLE);
    }
    if (parameters->EnableAutoDepthStencil
        && !(api == 8 ? supported_depth_stencil_format8(parameters->AutoDepthStencilFormat)
            : supported_depth_stencil_format(parameters->AutoDepthStencilFormat))) {
        TRACE("FAIL CreateDevice unsupported depth_fmt=%08lX -> %08lX",
            (DWORD)parameters->AutoDepthStencilFormat,
            (DWORD)D3DERR_NOTAVAILABLE);
        return TRACE_REFUSE(D3DERR_NOTAVAILABLE);
    }
    if (parameters->EnableAutoDepthStencil
        && !supported_multisample(parameters->AutoDepthStencilFormat,
            parameters->MultiSampleType)) {
        TRACE("FAIL CreateDevice depth multisample mismatch depth_fmt=%08lX "
              "multisample=%lu -> %08lX",
            (DWORD)parameters->AutoDepthStencilFormat,
            (DWORD)parameters->MultiSampleType,
            (DWORD)D3DERR_NOTAVAILABLE);
        return TRACE_REFUSE(D3DERR_NOTAVAILABLE);
    }
    if (parameters->BackBufferFormat != D3DFMT_UNKNOWN
        && !(api == 8 ? supported_render_target_format8(parameters->BackBufferFormat)
            : supported_backbuffer_format(parameters->BackBufferFormat))) {
        TRACE("FAIL CreateDevice unsupported backbuffer_fmt=%08lX -> %08lX",
            (DWORD)parameters->BackBufferFormat,
            (DWORD)D3DERR_NOTAVAILABLE);
        return TRACE_REFUSE(D3DERR_NOTAVAILABLE);
    }
    if (parameters->BackBufferWidth > 8192
        || parameters->BackBufferHeight > 8192) {
        TRACE("FAIL CreateDevice oversized backbuffer=%lux%lu -> %08lX",
            parameters->BackBufferWidth, parameters->BackBufferHeight,
            (DWORD)D3DERR_INVALIDCALL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    if (parameters->BackBufferCount > 3) {
        TRACE("FAIL CreateDevice backbuffer_count=%lu -> %08lX",
            parameters->BackBufferCount, (DWORD)D3DERR_INVALIDCALL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    /*
     * BehaviorFlags was recorded and otherwise ignored, which meant an app
     * could ask for a combination D3D9 rejects and be handed a device anyway --
     * and then behave as if the combination it asked for were in force. D3D9
     * requires exactly one vertex-processing mode, and D3DCREATE_PUREDEVICE
     * only alongside the hardware one.
     */
    if (api == 9) {
        const DWORD vertex_processing = behavior
            & (D3DCREATE_SOFTWARE_VERTEXPROCESSING
                | D3DCREATE_HARDWARE_VERTEXPROCESSING
                | D3DCREATE_MIXED_VERTEXPROCESSING);
        if (vertex_processing != D3DCREATE_SOFTWARE_VERTEXPROCESSING
            && vertex_processing != D3DCREATE_HARDWARE_VERTEXPROCESSING
            && vertex_processing != D3DCREATE_MIXED_VERTEXPROCESSING) {
            TRACE("FAIL CreateDevice behavior=%08lX names %s vertex processing "
                  "mode -> %08lX",
                behavior,
                vertex_processing ? "more than one" : "no",
                (DWORD)D3DERR_INVALIDCALL);
            HOSTLOG_FAILED("CreateDevice refused: BehaviorFlags %08lX must "
                           "name exactly one vertex-processing mode",
                behavior);
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        }
        if ((behavior & D3DCREATE_PUREDEVICE)
            && vertex_processing != D3DCREATE_HARDWARE_VERTEXPROCESSING) {
            TRACE("FAIL CreateDevice behavior=%08lX pairs PUREDEVICE with "
                  "non-hardware vertex processing -> %08lX",
                behavior,
                (DWORD)D3DERR_INVALIDCALL);
            HOSTLOG_FAILED("CreateDevice refused: D3DCREATE_PUREDEVICE "
                           "requires D3DCREATE_HARDWARE_VERTEXPROCESSING");
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        }
    }

    window = parameters->hDeviceWindow ? parameters->hDeviceWindow
                                       : focus_window;
    SetRect(&client, 0, 0, 640, 480);
    origin.x = 0;
    origin.y = 0;
    if (window) {
        GetClientRect(window, &client);
        ClientToScreen(window, &origin);
    }
    width = d9wg_present_extent(parameters->BackBufferWidth,
        (uint32_t)(client.right - client.left), 640);
    height = d9wg_present_extent(parameters->BackBufferHeight,
        (uint32_t)(client.bottom - client.top), 480);
    if (width > 8192 || height > 8192)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);

    device = (D9Device*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
        sizeof(*device));
    if (!device) {
        TRACE("FAIL CreateDevice allocation -> %08lX", (DWORD)E_OUTOFMEMORY);
        return E_OUTOFMEMORY;
    }
    d9wg_fpu_setup((behavior & D3DCREATE_FPU_PRESERVE) != 0);
    device->iface.lpVtbl = &g_device_vtbl;
    device->implicit_swap_chain.iface.lpVtbl = &g_swap_chain_vtbl;
    device->implicit_swap_chain.device = device;
    device->refcount = 1;
    device->parent = d3d;
    IDirect3D9_AddRef(iface);
    device->handle = allocate_handle();
    device->creation.AdapterOrdinal = adapter;
    device->creation.DeviceType = type;
    device->creation.hFocusWindow = focus_window;
    device->creation.BehaviorFlags = behavior;
    fill_display_mode(&device->display_mode,
        width, height,
        backbuffer_format);
    d9wg_present_parameters_effective(&device->present, parameters,
        width, height, device->display_mode.Format, window);
    device->viewport.X = 0;
    device->viewport.Y = 0;
    device->viewport.Width = device->display_mode.Width;
    device->viewport.Height = device->display_mode.Height;
    device->viewport.MinZ = 0.0f;
    device->viewport.MaxZ = 1.0f;
    /* D3D9's default scissor rect is the whole back buffer (a state block of
     * D3DSBT_ALL captured it as {0,0,0,0}) */
    SetRect(&device->scissor_rect, 0, 0, (int)device->display_mode.Width,
        (int)device->display_mode.Height);
    device_init_states_for_api(device, api);

    ZeroMemory(&command, sizeof(command));
    command.device_handle = device->handle;
    command.hwnd = (uint32_t)(uintptr_t)window;
    command.x = origin.x;
    command.y = origin.y;
    command.width = width;
    command.height = height;
    command.backbuffer_format = device->display_mode.Format;
    command.windowed = parameters->Windowed;
    command.behavior_flags = behavior;
    command.enable_auto_depth_stencil = parameters->EnableAutoDepthStencil;
    command.auto_depth_stencil_format = parameters->AutoDepthStencilFormat;
    command.multisample_type = parameters->MultiSampleType;
    command.multisample_quality = parameters->MultiSampleQuality;
    if (!emit_command(D9WG_OP_CREATE_DEVICE, &command, sizeof(command))) {
        IDirect3D9_Release(iface);
        HeapFree(GetProcessHeap(), 0, device);
        TRACE("FAIL CreateDevice transport -> %08lX",
            (DWORD)D3DERR_DRIVERINTERNALERROR);
        return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
    }
    if (!emit_render_default_patches(device->handle, api, device->present.EnableAutoDepthStencil, device->render_states)
        || !emit_object_default_patches(device->handle, &device->material)) {
        IDirect3DDevice9_Release(&device->iface);
        return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
    }

    if (!reset_window_mode(device, window)) {
        IDirect3DDevice9_Release(&device->iface);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    emit_window_state(device, window);
    watch_window_state(device);
    d9wg_present_parameters_return(api, parameters, &device->present);
    *device_out = &device->iface;
#ifdef D9WG_DIAGNOSTIC_TRACE
    g_trace_device_window = window;
#endif
    TRACE("OK CreateDevice handle=%08lX surface=%lux%lu hwnd=%08lX",
        device->handle, command.width, command.height,
        (DWORD)(uintptr_t)window);
    return D3D_OK;
}

/* ---- IDirect3DDevice9: COM + lifecycle ---- */

static HRESULT WINAPI device_query_interface(IDirect3DDevice9 *iface,
        REFIID iid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = NULL;
    if (!iid || (!iid_is_unknown(iid)
            && !guid_equal(iid, &IID_IDirect3DDevice9)))
        return E_NOINTERFACE;
    *object = iface;
    IDirect3DDevice9_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI device_add_ref(IDirect3DDevice9 *iface)
{
    return (ULONG)InterlockedIncrement(&device_from_iface(iface)->refcount);
}

static ULONG WINAPI device_release(IDirect3DDevice9 *iface)
{
    D9Device *device = device_from_iface(iface);
    LONG refs_before = InterlockedCompareExchange(&device->refcount, 0, 0);
    LONG children_before = InterlockedCompareExchange(
            &device->child_parent_refs, 0, 0);
    ULONG refs;

    TRACE_MARK_ENTER("Device.Release");
    TRACE("ENTER Device.Release object=%08lX vtbl=%08lX handle=%08lX "
            "refs=%ld child_refs=%ld releasing=%lu",
            (DWORD)(uintptr_t)iface, (DWORD)(uintptr_t)iface->lpVtbl,
            device->handle, refs_before, children_before,
            (DWORD)device->releasing_owned_refs);
    (void)refs_before;
    (void)children_before;
    refs = (ULONG)InterlockedDecrement(&device->refcount);

    if (device->releasing_owned_refs) {
        TRACE("LEAVE Device.Release nested object=%08lX refs=%lu child_refs=%ld",
                (DWORD)(uintptr_t)iface, refs,
                InterlockedCompareExchange(&device->child_parent_refs, 0, 0));
        TRACE_MARK_EXIT("Device.Release", (HRESULT)refs, NULL);
        return refs;
    }
    /* Resource public refs keep this complete state alive, even after the
     * application's last direct device reference is gone. All internal live
     * and recorded bindings retain storage without a device parent pin. */
    if (!refs) {
        TRACE("Device.Release final-state object=%08lX refs=%lu child_refs=%ld",
                (DWORD)(uintptr_t)iface, refs,
                InterlockedCompareExchange(&device->child_parent_refs, 0, 0));
        device->releasing_owned_refs = TRUE;
        /* Final private-data callbacks may query the device while bound-only
         * storage is retired, even when every public parent pin is gone. */
        InterlockedIncrement(&device->refcount);
        device_release_owned_references(device);
        /* Cache finalizers run before host/device teardown under the same
         * temporary pin. Reentrant insertion is drained; retaining the device
         * leaves subsequent stores intact until its next final Release. */
        while (InterlockedCompareExchange(&device->refcount, 0, 0) == 1
                && ((device->implicit_back_buffer
                        && device->implicit_back_buffer->private_data.head)
                    || (device->implicit_depth_stencil
                        && device->implicit_depth_stencil->private_data.head))) {
            if (device->implicit_back_buffer)
                d9wg_private_clear(&device->implicit_back_buffer->private_data, &d9wg_private_ops);
            if (InterlockedCompareExchange(&device->refcount, 0, 0) == 1
                    && device->implicit_depth_stencil)
                d9wg_private_clear(&device->implicit_depth_stencil->private_data, &d9wg_private_ops);
        }
        refs = (ULONG)InterlockedDecrement(&device->refcount);
        device->releasing_owned_refs = FALSE;
    }
    if (!refs) {
        D9WGDestroyResource destroy;
        d9wg_light_free(&device->extra_lights);
        d9wg_palette_free(&device->palettes);
        TRACE("Device.Release destroy object=%08lX handle=%08lX pending=%lu",
                (DWORD)(uintptr_t)iface, device->handle, g_command_count);
        unwatch_window_state(device);
        restore_display_mode(device);
        destroy.resource_handle = device->handle;
        destroy.resource_kind = 0;
        emit_command(D9WG_OP_DESTROY_RESOURCE, &destroy, sizeof(destroy));
        EnterCriticalSection(&g_transport_lock);
        if (g_command_count)
            submit_batch_locked(FALSE);
        LeaveCriticalSection(&g_transport_lock);
        TRACE("LEAVE Device.Release free object=%08lX refs=0 child_refs=%ld",
                (DWORD)(uintptr_t)iface,
                InterlockedCompareExchange(&device->child_parent_refs, 0, 0));
        TRACE_MARK_EXIT("Device.Release", 0, NULL);
        TRACE_FLUSH();
        /* The implicit surfaces outlive every public reference to them by
         * design, so the device is what finally frees them. Reaching here means
         * their public counts are zero: each active surface pinned one device
         * reference that had to reach zero to get this far. */
        if (device->implicit_back_buffer) {
            core_view_retire8(device->implicit_back_buffer);
            HeapFree(GetProcessHeap(), 0, device->implicit_back_buffer);
        }
        if (device->implicit_depth_stencil) {
            core_view_retire8(device->implicit_depth_stencil);
            HeapFree(GetProcessHeap(), 0, device->implicit_depth_stencil);
        }
        /* Frontend Device8 metadata outlives every implicit surface view.
         * Private-data finalizers and any resurrection already ran above. */
        core_view_retire8(device);
        /* Registry queries in retirement callbacks still inspect the parent
         * compatibility profile. Keep that factory alive through every view. */
        IDirect3D9_Release(&device->parent->iface);
        HeapFree(GetProcessHeap(), 0, device);
        return 0;
    }
    TRACE("LEAVE Device.Release object=%08lX refs=%lu child_refs=%ld",
            (DWORD)(uintptr_t)iface, refs,
            InterlockedCompareExchange(&device->child_parent_refs, 0, 0));
    TRACE_MARK_EXIT("Device.Release", (HRESULT)refs, NULL);
    return refs;
}

static HRESULT WINAPI device_test_cooperative_level(IDirect3DDevice9* iface)
{
    /* Deliberately untraced: most titles call this every frame. */
    return d9wg_reset_cooperative_result(&device_from_iface(iface)->reset_state);
}

static UINT WINAPI device_get_available_texture_mem(IDirect3DDevice9 *iface)
{
    (void)iface;
    /* Titles size their texture budget from this, and some refuse to start
     * below a threshold, so it is worth knowing when it was read. */
    TRACE("OK Device.GetAvailableTextureMem -> %lu", 256u * 1024u * 1024u);
    return 256u * 1024u * 1024u;
}

static HRESULT WINAPI device_evict_managed_resources(IDirect3DDevice9 *iface)
{
    (void)iface;
    return D3D_OK;
}

static HRESULT WINAPI device_get_direct3d(IDirect3DDevice9 *iface,
        IDirect3D9 **d3d_out)
{
    D9Device *device = device_from_iface(iface);
    if (!d3d_out)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *d3d_out = &device->parent->iface;
    IDirect3D9_AddRef(*d3d_out);
    return D3D_OK;
}

static HRESULT WINAPI device_get_caps(IDirect3DDevice9 *iface, D3DCAPS9 *caps)
{
    (void)iface;
    if (!caps)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    fill_caps(caps);
    trace_caps("Device.GetDeviceCaps", caps);
    return D3D_OK;
}

static HRESULT WINAPI device_get_display_mode(IDirect3DDevice9* iface,
    UINT swapchain, D3DDISPLAYMODE* mode)
{
    D9Device* device = device_from_iface(iface);
    if (swapchain || !mode)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    HRESULT result = d3d_get_adapter_display_mode(&device->parent->iface,
        device->creation.AdapterOrdinal, mode);
    if (SUCCEEDED(result))
        TRACE("OK Device.GetDisplayMode -> %lux%lu format=%08lX refresh=%lu",
            mode->Width, mode->Height, (DWORD)mode->Format, mode->RefreshRate);
    return result;
}

static HRESULT WINAPI device_get_creation_parameters(IDirect3DDevice9 *iface,
        D3DDEVICE_CREATION_PARAMETERS *parameters)
{
    D9Device *device = device_from_iface(iface);
    if (!parameters)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *parameters = device->creation;
    TRACE("OK Device.GetCreationParameters adapter=%lu type=%lu behavior=%08lX",
            parameters->AdapterOrdinal, (DWORD)parameters->DeviceType,
            parameters->BehaviorFlags);
    return D3D_OK;
}

/* ---- D3D9 hardware cursor ----
 *
 * A fullscreen D3D9 game draws its pointer through these three calls, not
 * through GDI, so nothing about it ever reaches the VGA framebuffer the page
 * composites underneath the WebGPU overlay. With them stubbed the pointer is
 * simply invisible -- and the site hides the browser's own cursor, on the
 * assumption the guest draws one.
 *
 * D3D9's hardware cursor follows the OS pointer by itself once ShowCursor is
 * on; SetCursorPosition only warps it. emit_present_and_flush() therefore
 * re-sends the live pointer position every frame rather than relying on the
 * app to call SetCursorPosition.
 */
static HRESULT WINAPI device_set_cursor_properties(IDirect3DDevice9 *iface,
        UINT hotspot_x, UINT hotspot_y, IDirect3DSurface9 *bitmap)
{
    D9Device *device = device_from_iface(iface);
    if (!bitmap || bitmap->lpVtbl != &g_surface_vtbl)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    D9Surface *surface = surface_from_iface(bitmap);
    D9TextureLevel *level = surface_texture_level(surface);
    D3DDISPLAYMODE adapter;
    if (FAILED(d3d_get_adapter_display_mode(&device->parent->iface,
            device->creation.AdapterOrdinal, &adapter)))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    D9WGCursorImage image;
    ZeroMemory(&image, sizeof(image));
    image.adapter_width = adapter.Width;
    image.adapter_height = adapter.Height;
    image.level = surface->level;
    image.locked = surface->locked || (level && level->locked);
    image.device = surface->device->handle;
    image.format = surface->format;
    image.width = surface->width;
    image.height = surface->height;
    image.pixels = surface->shadow ? surface->shadow : level ? level->shadow : NULL;
    image.pitch = surface->shadow ? surface->row_pitch : level ? level->row_pitch : 0;
    image.byte_count = surface->shadow ? surface->byte_count : level ? level->byte_count : 0;
    DWORD usage = surface->texture ? surface->texture->usage
            : surface->cube_texture ? surface->cube_texture->usage : 0;
    image.readback = surface->pool == D3DPOOL_DEFAULT
            && (surface->swap_chain || (usage & D3DUSAGE_RENDERTARGET)
                || !level || !level->shadow_valid);
    image.handle = surface->texture ? surface->texture->handle
            : surface->cube_texture ? surface->cube_texture->handle : 0;
    image.face = surface->cube_texture ? surface->cube_face : 0;
    HRESULT result = d9wg_cursor_upload(device->handle, hotspot_x, hotspot_y, &image);
    if (FAILED(result))
        return TRACE_REFUSE(result);
    BOOL committed = d9wg_cursor_commit(device->handle, &device->app_cursor_state,
            image.width, image.height);
    device->cursor_ready = TRUE;
    device->app_cursor = TRUE;
    device->cursor_visible = device->app_cursor_state.visible;
    return TRACE_REFUSE(committed ? D3D_OK : D3DERR_DRIVERINTERNALERROR);
}

/*
 * Reports the guest window manager's view of the device window, once and then
 * only when it changes.
 *
 * The host draws its overlay on top unconditionally, so a game whose window is
 * minimised, hidden, or merely not in the foreground still looks perfectly
 * rendered -- while the guest routes every click to whatever window really
 * owns those pixels. That failure is invisible in the picture by construction,
 * so it has to be reported rather than deduced.
 */
static BOOL emit_window_state_from_probe(D9Device *device, HWND window,
        const D9WGWindowProbe *probe)
{
    D9WGWindowStateWithRegion report;
    BOOL result;
    d9wg_window_probe_report(probe, device->handle, !device->present.Windowed, 0, &report);
    EnterCriticalSection(&g_transport_lock);
    device->state_window = window;
    if (d9wg_window_cache_matches(&device->window_cache, &report)) {
        LeaveCriticalSection(&g_transport_lock);
        return FALSE;
    }
    result = emit_command(D9WG_OP_WINDOW_STATE, &report,
            (uint32_t)D9WG_WINDOW_STATE_BYTES(&report));
    if (result) d9wg_window_cache_commit(&device->window_cache, &report);
    LeaveCriticalSection(&g_transport_lock);
    return result;
}

static BOOL emit_window_state(D9Device *device, HWND window)
{
    D9WGWindowProbe probe;
    EnterCriticalSection(&g_transport_lock);
    d9wg_window_probe_capture((uintptr_t)window, D9WG_WINDOW_PROBE_STATE |
        D9WG_WINDOW_PROBE_REGION | D9WG_WINDOW_PROBE_OUTER, &probe);
    BOOL emitted = emit_window_state_from_probe(device, window, &probe);
    LeaveCriticalSection(&g_transport_lock);
    return emitted;
}

/* A shared message thread observes foreground/window events even while the
 * application stops presenting or pumping messages. Removal synchronizes with
 * callbacks before the API object is destroyed; report/flush policy stays here. */
static int notify_device_window(void *context)
{
    D9Device *device = context;
    return device->state_window && emit_window_state(device, device->state_window);
}

static void flush_window_reports(void)
{
    if (g_command_count) submit_batch_locked(FALSE);
}

static void watch_window_state(D9Device *device)
{
    d9wg_window_watch_add(&device->window_watch, device,
        notify_device_window, flush_window_reports, &g_transport_lock);
}

static void unwatch_window_state(D9Device *device)
{
    d9wg_window_watch_remove(&device->window_watch);
}

/*
 * A real fullscreen D3D9 device takes the foreground and the top of the
 * z-order when it is created -- that is part of what "exclusive fullscreen"
 * means, and it is done by the driver/runtime, not by the game. Nothing here
 * is a real driver, so without this the game's window can sit behind whatever
 * the user last had focused while its frames are still composited on top by
 * the host: the picture looks right and every click lands in the window
 * underneath.
 */
/*
 * SetForegroundWindow alone is unreliable: Windows denies it to a process that
 * is not already in the foreground, and returns FALSE without doing anything.
 * Briefly attaching to the current foreground thread's input queue is the
 * documented way around that restriction, and is what a game or a launcher does
 * for exactly this reason. Detaching again immediately matters -- staying
 * attached couples the two message queues and can deadlock both.
 */
static void raise_window_to_foreground(HWND window)
{
    d9wg_window_mode_raise(window);
}

static D9WGWindowModeBindings window_mode_bindings(D9Device* device)
{
    return (D9WGWindowModeBindings) { &device->styled_window,
        &device->saved_window_style, &device->saved_window_exstyle,
        &device->display_mode_changed, &device->last_foreground_claim };
}



/*
 * Re-takes the foreground for a fullscreen device that has lost it.
 *
 * The one-shot claim at CreateDevice is not enough. Need for Speed: Most
 * Wanted reached its main menu with foreground: false in every window-state
 * report -- the picture was perfect and every click went to whatever window was
 * on top instead. Two things cause that and neither is visible in the frame:
 * a game that creates or activates another window after the device (a splash,
 * a launcher, a message-only window), and Windows refusing SetForegroundWindow
 * outright, which it does for a process that is not already in the foreground.
 *
 * A real exclusive-fullscreen D3D9 device holds the foreground for its whole
 * lifetime, re-acquiring it on activation, so maintaining it here is matching
 * the runtime rather than fighting the user: nothing else in a v86 guest whose
 * entire purpose is this one game wants the focus. It stays limited to
 * fullscreen devices -- a windowed game stealing focus every frame would be
 * indefensible -- and to one attempt per interval, because SetForegroundWindow
 * is a syscall and losing focus is not something a frame boundary can fix
 * faster than a person can notice.
 */
static BOOL maintain_fullscreen_foreground(D9Device *device, HWND window,
        const D9WGWindowProbe *probe)
{
    DWORD now;

    if (device->present.Windowed || !window || !(probe->state.flags & D9WG_WINDOW_IS_WINDOW))
        return FALSE;
    if ((probe->state.flags & D9WG_WINDOW_FOREGROUND) != 0)
        return FALSE;
    /* Something the game itself put over its whole window -- Warcraft III's
     * cinematic player -- owns the screen and the keyboard (Esc skips the
     * video) until it goes away. A splash or a message-only window does not
     * cover the device window, so NFS's case below still re-claims. */
    if ((probe->state.flags & D9WG_WINDOW_OCCLUDED) != 0)
        return FALSE;
    now = GetTickCount();
    /* GetTickCount wraps every ~49 days; the subtraction is unsigned so the
     * wrap is harmless, but a zero initial value must not look like "claimed
     * just now" either -- hence the explicit first-time case. */
    if (device->foreground_claims &&
            (DWORD)(now - device->last_foreground_claim) < 500u)
        return FALSE;
    device->last_foreground_claim = now;
    ++device->foreground_claims;
    /* Window activation can call application window procedures; release the
     * transport lock while mutating, then recapture under it in Present. */
    LeaveCriticalSection(&g_transport_lock);
    if ((probe->state.flags & D9WG_WINDOW_ICONIC) != 0)
        ShowWindow(window, SW_RESTORE);
    raise_window_to_foreground(window);
    EnterCriticalSection(&g_transport_lock);
    return TRUE;

}

/* Undo the mode change when the device goes away, so a crashed or closed game
 * does not leave the guest desktop stuck at the game's resolution. */
static BOOL restore_display_mode(D9Device* device)
{
    D9WGWindowModeBindings bindings = window_mode_bindings(device);
    return d9wg_window_mode_restore(&bindings);
}

static BOOL reset_window_mode(D9Device* device, HWND window)
{
    D9WGWindowModeBindings bindings = window_mode_bindings(device);
    return d9wg_window_mode_apply(&bindings, device->present.Windowed, window,
        device->creation.hFocusWindow, device->display_mode.Width, device->display_mode.Height);
}

static void emit_cursor_position(D9Device *device, int x, int y, DWORD flags)
{
    d9wg_cursor_position_wire(device->handle, x, y, flags);
}

/*
 * Capture whatever cursor Windows is currently showing and ship it as if the
 * application had called SetCursorProperties.
 *
 * This exists because most games never call SetCursorProperties at all: on
 * real hardware Windows composites the GDI cursor onto the primary surface
 * even for a fullscreen D3D9 device, so the game simply lets it. Here the
 * "primary surface" is a WebGPU canvas Windows knows nothing about, so that
 * compositing never happens and the pointer is invisible no matter what the
 * game does -- Warcraft III's first run showed cursorUploads: 0 for exactly
 * this reason. Reading the cursor out of GDI covers both mechanisms with one
 * path.
 */
/*
 * Enabled by default.  The browser deliberately hides its CSS cursor above
 * the guest canvas, and the VGA framebuffer cannot contain a Windows cursor,
 * so leaving this opt-in makes any title which delegates its pointer to GDI
 * unplayable.  An application-provided D3D9 hardware cursor still wins through
 * device->app_cursor.  D9WG_GDI_CURSOR=0 remains available for a title which
 * draws its own pointer geometry and does not want the Windows pointer layered
 * over it.
 */
static BOOL gdi_cursor_fallback_enabled(void)
{
    static LONG cached = -1;
    char value[8];
    DWORD length;

    if (cached >= 0)
        return cached != 0;
    length = GetEnvironmentVariableA("D9WG_GDI_CURSOR", value, sizeof(value));
    cached = (length == 1 && value[0] == '0') ? 0 : 1;
    return cached != 0;
}

static BOOL emit_system_cursor_bitmap(D9Device *device, HCURSOR cursor)
{
    ICONINFO icon;
    BITMAP bitmap;
    HDC dc;
    struct { BITMAPINFOHEADER header; DWORD masks[3]; } info;
    BYTE *colors = NULL;
    BYTE *mask = NULL;
    D9WGSetCursorProperties command;
    uint8_t *payload;
    uint8_t *blob;
    UINT width;
    UINT height;
    UINT pixel_bytes;
    UINT index;
    BOOL monochrome;
    BOOL has_alpha = FALSE;
    BOOL result = FALSE;

    ZeroMemory(&icon, sizeof(icon));
    if (!GetIconInfo(cursor, &icon))
        return FALSE;
    monochrome = icon.hbmColor == NULL;
    if (!GetObjectA(monochrome ? icon.hbmMask : icon.hbmColor,
            sizeof(bitmap), &bitmap))
        goto done;
    width = (UINT)bitmap.bmWidth;
    /* A monochrome cursor packs an AND mask above an XOR mask in a single
     * bitmap of twice the real height. */
    height = (UINT)(monochrome ? bitmap.bmHeight / 2 : bitmap.bmHeight);
    if (!width || !height || width > 256u || height > 256u)
        goto done;
    if (!multiply_u32(width * 4u, height, &pixel_bytes))
        goto done;

    dc = CreateCompatibleDC(NULL);
    if (!dc)
        goto done;
    ZeroMemory(&info, sizeof(info));
    info.header.biSize = sizeof(info.header);
    info.header.biWidth = (LONG)width;
    /* Negative height asks for a top-down image, which is the row order the
     * wire format and every other upload on this path already use. */
    info.header.biHeight = -(LONG)(monochrome ? height * 2u : height);
    info.header.biPlanes = 1;
    info.header.biBitCount = 32;
    info.header.biCompression = BI_RGB;

    mask = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            monochrome ? pixel_bytes * 2u : pixel_bytes);
    if (!mask) {
        DeleteDC(dc);
        goto done;
    }
    if (!GetDIBits(dc, icon.hbmMask, 0, monochrome ? height * 2u : height,
            mask, (BITMAPINFO *)&info, DIB_RGB_COLORS)) {
        DeleteDC(dc);
        goto done;
    }
    if (!monochrome) {
        colors = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                pixel_bytes);
        if (!colors) {
            DeleteDC(dc);
            goto done;
        }
        info.header.biHeight = -(LONG)height;
        if (!GetDIBits(dc, icon.hbmColor, 0, height, colors,
                (BITMAPINFO *)&info, DIB_RGB_COLORS)) {
            DeleteDC(dc);
            goto done;
        }
    }
    DeleteDC(dc);

    if (monochrome) {
        /* AND=1,XOR=0 -> transparent; AND=1,XOR=1 -> inverted, approximated
         * as white; AND=0 -> the opaque XOR colour (black or white). */
        colors = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                pixel_bytes);
        if (!colors)
            goto done;
        for (index = 0; index < width * height; ++index) {
            BYTE and_bit = mask[index * 4] ? 1u : 0u;
            BYTE xor_bit = mask[(width * height + index) * 4] ? 1u : 0u;
            BYTE value = xor_bit ? 255u : 0u;
            colors[index * 4 + 0] = value;
            colors[index * 4 + 1] = value;
            colors[index * 4 + 2] = value;
            colors[index * 4 + 3] = and_bit ? 0u : 255u;
        }
    } else {
        for (index = 0; index < width * height; ++index) {
            if (colors[index * 4 + 3]) {
                has_alpha = TRUE;
                break;
            }
        }
        if (!has_alpha) {
            /* A pre-XP style colour cursor carries no alpha channel; the 1bpp
             * mask is what says which texels see through (white == cut out). */
            for (index = 0; index < width * height; ++index)
                colors[index * 4 + 3] = mask[index * 4] ? 0u : 255u;
        }
    }

    ZeroMemory(&command, sizeof(command));
    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_SET_CURSOR_PROPERTIES,
            sizeof(command), pixel_bytes, NULL, &payload, &blob);
    if (result) {
        command.device_handle = device->handle;
        command.hotspot_x = icon.xHotspot;
        command.hotspot_y = icon.yHotspot;
        command.width = width;
        command.height = height;
        command.data_bytes = pixel_bytes;
        command.data_offset = (uint32_t)(blob - batch_base());
        CopyMemory(payload, &command, sizeof(command));
        CopyMemory(blob, colors, pixel_bytes);
    }
    LeaveCriticalSection(&g_transport_lock);

done:
    if (colors) HeapFree(GetProcessHeap(), 0, colors);
    if (mask) HeapFree(GetProcessHeap(), 0, mask);
    if (icon.hbmColor) DeleteObject(icon.hbmColor);
    if (icon.hbmMask) DeleteObject(icon.hbmMask);
    return result;
}

/* Called once per Present. Only re-captures the bitmap when the HCURSOR
 * actually changes -- a game swaps cursors on hover, not every frame. */
static void update_system_cursor(D9Device *device, HWND window)
{
    CURSORINFO cursor_info;
    POINT pointer;

    if (!gdi_cursor_fallback_enabled())
        return;
    ZeroMemory(&cursor_info, sizeof(cursor_info));
    cursor_info.cbSize = sizeof(cursor_info);
    if (!GetCursorInfo(&cursor_info))
        return;

    if (!(cursor_info.flags & CURSOR_SHOWING) || !cursor_info.hCursor) {
        if (device->cursor_visible) {
            D9WGShowCursor hide;
            device->cursor_visible = FALSE;
            hide.device_handle = device->handle;
            hide.show = 0;
            emit_command(D9WG_OP_SHOW_CURSOR, &hide, sizeof(hide));
        }
        return;
    }
    if (cursor_info.hCursor != device->system_cursor) {
        if (!emit_system_cursor_bitmap(device, cursor_info.hCursor))
            return;
        device->system_cursor = cursor_info.hCursor;
        device->cursor_ready = TRUE;
    }
    if (!device->cursor_visible) {
        D9WGShowCursor show;
        device->cursor_visible = TRUE;
        show.device_handle = device->handle;
        show.show = 1;
        emit_command(D9WG_OP_SHOW_CURSOR, &show, sizeof(show));
    }
    pointer = cursor_info.ptScreenPos;
    if (window)
        ScreenToClient(window, &pointer);
    emit_cursor_position(device, pointer.x, pointer.y, 0);
}

static void WINAPI device_set_cursor_position(IDirect3DDevice9 *iface,
        int x, int y, DWORD flags)
{
    D9Device *device = device_from_iface(iface);
    HWND window = device->present.hDeviceWindow ? device->present.hDeviceWindow : device->creation.hFocusWindow;
    d9wg_cursor_set_position(device->handle, window, &device->app_cursor_state,
            (int32_t)x, (int32_t)y, flags);
}

static WINBOOL WINAPI device_show_cursor(IDirect3DDevice9 *iface, WINBOOL show)
{
    D9Device *device = device_from_iface(iface);
    HWND window = device->present.hDeviceWindow ? device->present.hDeviceWindow : device->creation.hFocusWindow;
    WINBOOL previous = d9wg_cursor_show(device->handle, window, &device->app_cursor_state, show);
    device->cursor_visible = device->app_cursor_state.visible;
    return previous;
}

static UINT WINAPI device_get_number_of_swap_chains(IDirect3DDevice9 *iface)
{
    TRACE_MARK_ENTER("Device.GetNumberOfSwapChains");
    TRACE("CALL GetNumberOfSwapChains device=%08lX -> 1",
            device_from_iface(iface)->handle);
    TRACE_MARK_EXIT("Device.GetNumberOfSwapChains", 1,
            &device_from_iface(iface)->implicit_swap_chain.iface);
    (void)iface;
    return 1;
}

static HRESULT WINAPI device_reset(IDirect3DDevice9* iface,
    D3DPRESENT_PARAMETERS* parameters)
{
    D9Device* device = device_from_iface(iface);
    D9WGResetDevice reset;
    const UINT api = device_compatibility_api(device);
    RECT client;
    POINT origin;
    HWND window;
    D3DFORMAT backbuffer_format;

    d9wg_reset_begin(&device->reset_state);
    /* Reset was completely untraced, which left a hole exactly where one is
     * least affordable: a title that resets to apply its saved video settings
     * does so once, early, and everything after depends on it having worked.
     * Its three refusal paths returned bare HRESULTs, indistinguishable in a
     * trace from the call never happening. */
    TRACE("CALL Reset %lux%lu fmt=%08lX count=%lu multisample=%lu windowed=%lu "
          "auto_depth=%lu depth_fmt=%08lX interval=%08lX hwnd=%08lX",
        parameters ? parameters->BackBufferWidth : 0,
        parameters ? parameters->BackBufferHeight : 0,
        parameters ? (DWORD)parameters->BackBufferFormat : 0,
        parameters ? parameters->BackBufferCount : 0,
        parameters ? (DWORD)parameters->MultiSampleType : 0,
        parameters ? (DWORD)parameters->Windowed : 0,
        parameters ? (DWORD)parameters->EnableAutoDepthStencil : 0,
        parameters ? (DWORD)parameters->AutoDepthStencilFormat : 0,
        parameters ? parameters->PresentationInterval : 0,
        parameters ? (DWORD)(uintptr_t)parameters->hDeviceWindow : 0);
    /* D3D9 takes up to D3DPRESENT_BACK_BUFFERS_MAX (3) back buffers here,
     * as CreateDevice does; refusing more than one failed every Reset of a
     * double- or triple-buffered game (review S1-9b) */
    if (!parameters || parameters->BackBufferCount > 3) {
        TRACE("FAIL Reset parameters=%lu multisample=%lu count=%lu "
              "blockers=%lu -> %08lX",
            (DWORD)(parameters != NULL),
            parameters ? (DWORD)parameters->MultiSampleType : 0,
            parameters ? parameters->BackBufferCount : 0,
            (DWORD)device_has_reset_blockers(device),
            (DWORD)D3DERR_INVALIDCALL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    if (api == 8 && parameters->MultiSampleType != D3DMULTISAMPLE_NONE
            && parameters->MultiSampleType != D3DMULTISAMPLE_4_SAMPLES)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!d9wg_present_interval_valid(api, parameters->Windowed,
            parameters->PresentationInterval, D9WG_PRESENT_INTERVAL_MASK))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!d9wg_swap_effect_valid(api, parameters->SwapEffect, parameters->BackBufferCount))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!d9wg_present_multisample_valid(api, parameters->SwapEffect,
            parameters->MultiSampleType, parameters->MultiSampleQuality, parameters->Flags))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (api == 9 && !parameters->Windowed && parameters->BackBufferFormat == D3DFMT_UNKNOWN)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    backbuffer_format = api == 8
        ? (parameters->BackBufferFormat ? parameters->BackBufferFormat : device->display_mode.Format)
        : (D3DFORMAT)d9wg_display_mode_backbuffer_format(
        parameters->BackBufferFormat);
    if (!supported_multisample(backbuffer_format, parameters->MultiSampleType)) {
        TRACE("FAIL Reset unsupported multisample=%lu quality=%lu -> %08lX",
            (DWORD)parameters->MultiSampleType,
            parameters->MultiSampleQuality,
            (DWORD)D3DERR_NOTAVAILABLE);
        return TRACE_REFUSE(D3DERR_NOTAVAILABLE);
    }
    if (parameters->EnableAutoDepthStencil
        && !(api == 8 ? supported_depth_stencil_format8(parameters->AutoDepthStencilFormat)
            : supported_depth_stencil_format(parameters->AutoDepthStencilFormat))) {
        TRACE("FAIL Reset depth_fmt=%08lX -> %08lX",
            (DWORD)parameters->AutoDepthStencilFormat,
            (DWORD)D3DERR_NOTAVAILABLE);
        return TRACE_REFUSE(D3DERR_NOTAVAILABLE);
    }
    if (parameters->EnableAutoDepthStencil
        && !supported_multisample(parameters->AutoDepthStencilFormat,
            parameters->MultiSampleType))
        return TRACE_REFUSE(D3DERR_NOTAVAILABLE);
    if (parameters->BackBufferFormat != D3DFMT_UNKNOWN
        && !(api == 8 ? supported_render_target_format8(parameters->BackBufferFormat)
            : supported_backbuffer_format(parameters->BackBufferFormat))) {
        TRACE("FAIL Reset backbuffer_fmt=%08lX -> %08lX",
            (DWORD)parameters->BackBufferFormat,
            (DWORD)D3DERR_NOTAVAILABLE);
        return TRACE_REFUSE(D3DERR_NOTAVAILABLE);
    }
    window = parameters->hDeviceWindow ? parameters->hDeviceWindow
                                       : device->creation.hFocusWindow;
    SetRect(&client, 0, 0, 640, 480);
    origin.x = origin.y = 0;
    if (window) {
        GetClientRect(window, &client);
        ClientToScreen(window, &origin);
    }
    ZeroMemory(&reset, sizeof(reset));
    reset.old_device_handle = device->handle;
    reset.new_device_handle = allocate_handle();
    reset.hwnd = (uint32_t)(uintptr_t)window;
    reset.x = origin.x;
    reset.y = origin.y;
    reset.width = d9wg_present_extent(parameters->BackBufferWidth,
        (uint32_t)(client.right - client.left), device->display_mode.Width);
    reset.height = d9wg_present_extent(parameters->BackBufferHeight,
        (uint32_t)(client.bottom - client.top), device->display_mode.Height);
    reset.backbuffer_format = backbuffer_format;
    reset.windowed = parameters->Windowed;
    reset.behavior_flags = device->creation.BehaviorFlags;
    reset.enable_auto_depth_stencil = parameters->EnableAutoDepthStencil;
    reset.auto_depth_stencil_format = parameters->AutoDepthStencilFormat;
    reset.multisample_type = parameters->MultiSampleType;
    reset.multisample_quality = parameters->MultiSampleQuality;
    if (reset.width > 8192 || reset.height > 8192)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    device_cancel_recording(device);
    if (device_has_reset_blockers(device))
        return TRACE_REFUSE(api == 8 ? D3DERR_DEVICELOST : D3DERR_INVALIDCALL);
    if (!emit_command(D9WG_OP_RESET, &reset, sizeof(reset)))
        return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
    /* Reset ends an active Scene once the host accepts the new device. */
    device->in_scene = FALSE;
    device->handle = reset.new_device_handle;
    device_clear_bindings(device);
    ++device->reset_epoch;
    d9wg_present_parameters_effective(&device->present, parameters,
        reset.width, reset.height, reset.backbuffer_format, window);
    fill_display_mode(&device->display_mode, reset.width, reset.height,
        reset.backbuffer_format);
    /* A Reset resizes the implicit surfaces rather than replacing them -- the
     * app keeps the same pointers across it -- so the cached objects have to
     * pick up the new geometry here or GetDesc keeps reporting the old size. */
    if (device->implicit_back_buffer) {
        /* A cached object survives Reset, but its CPU lock storage does not. */
        if (device->implicit_back_buffer->shadow)
            HeapFree(GetProcessHeap(), 0, device->implicit_back_buffer->shadow);
        device->implicit_back_buffer->shadow = NULL;
        device->implicit_back_buffer->row_pitch = 0;
        device->implicit_back_buffer->byte_count = 0;
        device->implicit_back_buffer->width = device->display_mode.Width;
        device->implicit_back_buffer->height = device->display_mode.Height;
        device->implicit_back_buffer->format = device->display_mode.Format;
        device->implicit_back_buffer->multisample = parameters->MultiSampleType;
        device->implicit_back_buffer->multisample_quality = parameters->MultiSampleQuality;
    }
    if (device->implicit_depth_stencil) {
        device->implicit_depth_stencil->width = reset.width;
        device->implicit_depth_stencil->height = reset.height;
        device->implicit_depth_stencil->format = parameters->AutoDepthStencilFormat;
        device->implicit_depth_stencil->multisample = parameters->MultiSampleType;
        device->implicit_depth_stencil->multisample_quality = parameters->MultiSampleQuality;
    }
    device_init_states(device);
    d9wg_window_cache_invalidate(&device->window_cache);
    device->viewport.X = device->viewport.Y = 0;
    device->viewport.Width = reset.width;
    device->viewport.Height = reset.height;
    device->viewport.MinZ = 0.0f;
    device->viewport.MaxZ = 1.0f;
    SetRect(&device->scissor_rect, 0, 0, (int)reset.width, (int)reset.height);
    if (!emit_device_state_after_reset(device)) {
        TRACE("FAIL Reset emit_device_state_after_reset -> %08lX",
            (DWORD)D3DERR_DRIVERINTERNALERROR);
        return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
    }
    if (!reset_window_mode(device, window))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    emit_window_state(device, window);
    TRACE("OK Reset old_handle=%08lX new_handle=%08lX %lux%lu epoch=%lu",
        reset.old_device_handle, device->handle, reset.width, reset.height,
        device->reset_epoch);

    d9wg_present_parameters_return(api, parameters, &device->present);
    d9wg_reset_complete(&device->reset_state);
    return D3D_OK;
}

/*
 * Where a window's client area actually is, in screen coordinates.
 *
 * An empty client rect is not a curiosity to work around -- it means the host
 * has no idea where the game's output is, and it positions the WebGPU overlay
 * from exactly this. Get it wrong and the picture is drawn somewhere the guest
 * does not think the window is, so a click at the pixel the user aimed at is
 * delivered to whatever the guest really has there: another window, the
 * desktop, anything.
 *
 * GetWindowRect is the fallback because it works on windows GetClientRect
 * reports nothing useful for (fullscreen, above all), and it answers both
 * questions at once.
 */
static void probe_window_rect(HWND window, int fallback_width,
        int fallback_height, RECT *client, POINT *origin)
{
    D9WGWindowProbe probe;
    D9WGWindowGeometry geometry;
    d9wg_window_probe_capture((uintptr_t)window, D9WG_WINDOW_PROBE_OUTER_IF_EMPTY, &probe);
    d9wg_window_probe_geometry(&probe, D9WG_WINDOW_GEOMETRY_D3D9,
        (uint32_t)fallback_width, (uint32_t)fallback_height, &geometry);
    SetRect(client, 0, 0, (int)geometry.width, (int)geometry.height);
    origin->x = geometry.x;origin->y = geometry.y;
}

/*
 * An additional swap chain's present. It carries the chain's own window rect
 * for the same reason the implicit one does -- there is no window-move
 * subclassing, so Present is the live source of truth for where the host should
 * put that chain's canvas.
 */
static BOOL emit_present_swap_chain_and_flush(D9SwapChain *chain,
        HWND override_window)
{
    D9Device *device = chain->device;
    D9WGPresentSwapChain present;
    HWND window = override_window ? override_window : chain->window;
    RECT client;
    POINT origin;
    uint8_t *payload;
    BOOL result;

    probe_window_rect(window, (int)chain->present.BackBufferWidth,
            (int)chain->present.BackBufferHeight, &client, &origin);
    ZeroMemory(&present, sizeof(present));
    present.device_handle = device->handle;
    present.swap_chain_handle = chain->handle;
    present.hwnd = (uint32_t)(uintptr_t)window;
    present.x = origin.x;
    present.y = origin.y;
    present.width = (uint32_t)(client.right - client.left);
    present.height = (uint32_t)(client.bottom - client.top);

    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_PRESENT_SWAP_CHAIN,
            sizeof(present), 0, NULL, &payload, NULL);
    if (result) {
        CopyMemory(payload, &present, sizeof(present));
        result = submit_batch_locked(TRUE);
    }
    LeaveCriticalSection(&g_transport_lock);
    return result;
}

static BOOL emit_present_and_flush(D9Device *device, HWND override_window)
{
    D9WGPresent present;
    HWND window = override_window ? override_window
            : device->present.hDeviceWindow;
    D9WGWindowProbe probe;
    D9WGWindowGeometry geometry;
    uint8_t *payload;
    BOOL result;

    d9wg_window_watch_retry();

    if (!window)
        window = device->creation.hFocusWindow;
    EnterCriticalSection(&g_transport_lock);
    d9wg_window_probe_capture((uintptr_t)window, D9WG_WINDOW_PROBE_STATE |
        D9WG_WINDOW_PROBE_REGION | D9WG_WINDOW_PROBE_OUTER, &probe);
    if (maintain_fullscreen_foreground(device, window, &probe))
        d9wg_window_probe_capture((uintptr_t)window, D9WG_WINDOW_PROBE_STATE |
            D9WG_WINDOW_PROBE_REGION | D9WG_WINDOW_PROBE_OUTER, &probe);
    d9wg_window_probe_geometry(&probe, D9WG_WINDOW_GEOMETRY_D3D9,
        device->display_mode.Width, device->display_mode.Height, &geometry);
    present.device_handle = device->handle;
    present.hwnd = (uint32_t)(uintptr_t)window;
    present.x = geometry.x;
    present.y = geometry.y;
    /*
     * An empty client rect is not a curiosity to work around -- it means the
     * host has no idea where the game's output actually is, and it positions
     * the WebGPU overlay from exactly this. Get it wrong and the picture is
     * drawn somewhere the guest does not think the window is, so a click at
     * the pixel the user aimed at is delivered to whatever the guest really
     * has there: another window, the desktop, anything.
     *
     * GetWindowRect is the fallback because it works on windows GetClientRect
     * reports nothing useful for, and it answers both questions at once
     * (position and size, in screen coordinates).
     */
    present.width = geometry.width;
    present.height = geometry.height;

    /* D3D9's hardware cursor follows the OS pointer on its own, so the app is
     * under no obligation to call SetCursorPosition -- most never do. Sample
     * it here instead, once per frame, and only while a cursor is actually
     * armed and visible. */
    emit_window_state_from_probe(device, window, &probe);
    if (device->app_cursor) {
        d9wg_cursor_present(device->handle, window, &device->app_cursor_state);
    } else {
        /* The application is leaving the pointer to Windows; capture it out
         * of GDI instead (see emit_system_cursor_bitmap). */
        update_system_cursor(device, window);
    }

    result = reserve_command_locked(D9WG_OP_PRESENT, sizeof(present), 0,
            NULL, &payload, NULL);
    if (result) {
        CopyMemory(payload, &present, sizeof(present));
        result = submit_batch_locked(TRUE);
    }
    LeaveCriticalSection(&g_transport_lock);
    return result;
}

/* Raster status shares the presentation clock and its fallback units. */
static DWORD present_clock(D9Device *device)
{
    return d9wg_present_clock_now(&device->present_clock);
}

static HRESULT WINAPI device_present(IDirect3DDevice9* iface,
    const RECT* src_rect, const RECT* dst_rect, HWND override_window,
    const RGNDATA* dirty_region)
{
    BOOL ok;
    HRESULT status = d9wg_reset_present_result(&device_from_iface(iface)->reset_state);
    if (status != D3D_OK)
        return TRACE_REFUSE(status);
    (void)src_rect;
    (void)dst_rect;
    (void)dirty_region;

    TRACE("CALL Present device=%08lX override=%08lX",
        device_from_iface(iface)->handle,
        (DWORD)(uintptr_t)override_window);
#ifdef D9WG_DIAGNOSTIC_TRACE
    trace_frame_summary();
    {
        /* Every 256th frame: often enough to watch a loading screen eat memory,
         * rare enough to stay out of the way of everything else. */
        static DWORD frame;
        if ((frame++ & 0xFFu) == 0)
            trace_memory_state("present");
    }
#endif
    ok = emit_present_and_flush(device_from_iface(iface), override_window);
    /* After the flush, so the frame is on its way to the host while this
     * thread is parked rather than before it. */
    if (ok) {
        D9Device* device = device_from_iface(iface);
        d9wg_throttle_present(&device->present_clock, &device->present_pacing,
            device->present.PresentationInterval, device->display_mode.RefreshRate);
    }
    TRACE("%s Present device=%08lX -> %08lX", ok ? "OK" : "FAIL",
        device_from_iface(iface)->handle,
        (DWORD)(ok ? D3D_OK : D3DERR_DRIVERINTERNALERROR));
    return ok ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_set_dialog_box_mode(IDirect3DDevice9 *iface,
        WINBOOL enable)
{ (void)iface; (void)enable; return D3D_OK; }

/*
 * D3D9's gamma ramp is applied at scanout rather than by the renderer, so it
 * reaches the host as its own command and is applied in the present blit.
 *
 * SetGammaRamp returns void: there is no way to tell the caller it failed, and
 * a title's brightness slider is expected to work with no acknowledgement. The
 * ramp is therefore recorded locally whether or not the emit succeeds, so
 * GetGammaRamp still answers correctly on a device whose command ring is full.
 */
static void WINAPI device_set_gamma_ramp(IDirect3DDevice9 *iface,
        UINT swapchain, DWORD flags, const D3DGAMMARAMP *ramp)
{
    D9Device *device = device_from_iface(iface);
    TRACE("CALL SetGammaRamp device=%08lX chain=%lu flags=%08lX ramp=%08lX", device->handle, swapchain, flags, (DWORD)(uintptr_t)ramp);
    if (swapchain) return;
    d9wg_set_gamma_ramp(device->handle, flags, ramp, &device->gamma_ramp,
            &device->gamma_ramp_set, &device->gamma_ramp_flags);
}

static void WINAPI device_get_gamma_ramp(IDirect3DDevice9 *iface,
        UINT swapchain, D3DGAMMARAMP *ramp)
{
    D9Device *device = device_from_iface(iface);
    if (swapchain) { if (ramp) ZeroMemory(ramp, sizeof(*ramp)); return; }
    d9wg_get_gamma_ramp(&device->gamma_ramp, device->gamma_ramp_set, ramp);
}

static HRESULT WINAPI device_begin_scene(IDirect3DDevice9 *iface)
{
    D9Device *device = device_from_iface(iface);
    D9WGDeviceOnly command;

    if (device->in_scene)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    device->in_scene = TRUE;
    command.device_handle = device->handle;
    command.reserved = 0;
    return emit_command(D9WG_OP_BEGIN_SCENE, &command, sizeof(command))
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_end_scene(IDirect3DDevice9 *iface)
{
    D9Device *device = device_from_iface(iface);
    D9WGDeviceOnly command;

    if (!device->in_scene)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    device->in_scene = FALSE;
    command.device_handle = device->handle;
    command.reserved = 0;
    return emit_command(D9WG_OP_END_SCENE, &command, sizeof(command))
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_clear(IDirect3DDevice9 *iface, DWORD rect_count,
        const D3DRECT *rects, DWORD flags, D3DCOLOR color, float z,
        DWORD stencil)
{
    D9Device *device = device_from_iface(iface);
    D9WGClear command;
    uint8_t *payload;
    uint8_t *rect_data;
    uint32_t rect_bytes;
    BOOL result;

    if (rect_count && !rects)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!(flags & (D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL)))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (rect_count > 0xFFFFFFFFu / sizeof(*rects))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    rect_bytes = rect_count * sizeof(*rects);
    command.device_handle = device->handle;
    command.clear_flags = flags;
    command.color = color;
    command.depth = z;
    command.stencil = stencil;
    command.rect_count = rect_count;

    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_CLEAR, sizeof(command), rect_bytes,
            NULL, &payload, &rect_data);
    if (result) {
        CopyMemory(payload, &command, sizeof(command));
        if (rect_bytes)
            CopyMemory(rect_data, rects, rect_bytes);
    }
    LeaveCriticalSection(&g_transport_lock);
    if (result) {
        TRACE_FRAME_CLEAR();
    }
    if (result && (flags & D3DCLEAR_TARGET)) {
        UINT target;
        for (target = 0; target < D9_MAX_RENDER_TARGETS; ++target) {
            D9Surface *surface = device->render_target_surfaces[target];
            if (!surface)
                continue; /* slot 0 NULL is the implicit back buffer */
            if (!rect_count) {
                RECT full;
                SetRect(&full, 0, 0, (int)surface->width,
                        (int)surface->height);
                mirror_target_color_fill(surface, &full, color);
            } else {
                DWORD index;
                for (index = 0; index < rect_count; ++index) {
                    RECT area;
                    area.left = rects[index].x1;
                    area.top = rects[index].y1;
                    area.right = rects[index].x2;
                    area.bottom = rects[index].y2;
                    mirror_target_color_fill(surface, &area, color);
                }
            }
        }
    }
    return result ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_set_transform(IDirect3DDevice9 *iface,
        D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix)
{
    D9Device *device = device_from_iface(iface);
    uint32_t slot;
    D9WGSetTransform command;
    if (!matrix || !d9wg_state_block_transform_slot((UINT)state, &slot))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (state_block_record_transform(device, (UINT)state, matrix)) return D3D_OK;
    CopyMemory(device->transforms[state], matrix, sizeof(float) * 16);
    command.device_handle = device->handle;
    command.state = (uint32_t)state;
    CopyMemory(command.matrix, matrix, sizeof(command.matrix));
    return emit_command(D9WG_OP_SET_TRANSFORM, &command, sizeof(command))
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_get_transform(IDirect3DDevice9 *iface,
        D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetTransform");
    D9Device *device = device_from_iface(iface);
    uint32_t slot;
    if (!matrix || !d9wg_state_block_transform_slot((UINT)state, &slot))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    CopyMemory(matrix, device->transforms[state], sizeof(float) * 16);
    return D3D_OK;
}

static HRESULT WINAPI device_set_viewport(IDirect3DDevice9 *iface,
        const D3DVIEWPORT9 *viewport)
{
    D9Device *device = device_from_iface(iface);
    D9Surface *target = device->render_target_surfaces[0];
    UINT target_width = target ? target->width : device->display_mode.Width;
    UINT target_height = target ? target->height : device->display_mode.Height;
    D9WGSetViewport command;
    if (!viewport || !viewport->Width || !viewport->Height) {
        TRACE("REJECT SetViewport empty viewport=%08lX",
                (DWORD)(uintptr_t)viewport);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    /*
     * A rejected SetViewport is one of the worst silent failures in this
     * surface: the call returns an error the app almost never checks, the old
     * viewport stays, and every later draw is rendered at whatever scale that
     * viewport implies. A title drawing a model into a small panel then gets it
     * at full-target scale -- oversized and off-centre, with nothing anywhere
     * saying why. Trace both outcomes; the accepted case is a few lines a frame
     * and is what makes the rejected case legible by contrast.
     */
    if (viewport->X > target_width
            || viewport->Y > target_height
            || viewport->Width > target_width - viewport->X
            || viewport->Height > target_height - viewport->Y
            || viewport->MinZ < 0.0f || viewport->MaxZ > 1.0f
            || viewport->MinZ > viewport->MaxZ) {
        TRACE("REJECT SetViewport x=%lu y=%lu %lux%lu minz=%ld/1000 "
                "maxz=%ld/1000 limit=%lux%lu",
                viewport->X, viewport->Y, viewport->Width, viewport->Height,
                (LONG)(viewport->MinZ * 1000.0f),
                (LONG)(viewport->MaxZ * 1000.0f),
                target_width, target_height);
        HOSTLOG_REFUSED("SetViewport %lux%lu at %lu,%lu refused; the target is "
                "%lux%lu, so every later draw keeps the previous viewport's "
                "scale", viewport->Width, viewport->Height, viewport->X,
                viewport->Y, target_width, target_height);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    TRACE("OK SetViewport x=%lu y=%lu %lux%lu minz=%ld/1000 maxz=%ld/1000",
            viewport->X, viewport->Y, viewport->Width, viewport->Height,
            (LONG)(viewport->MinZ * 1000.0f),
            (LONG)(viewport->MaxZ * 1000.0f));
    if (state_block_record_viewport(device, viewport)) return D3D_OK;
    device->viewport = *viewport;
    command.device_handle = device->handle;
    command.x = viewport->X;
    command.y = viewport->Y;
    command.width = viewport->Width;
    command.height = viewport->Height;
    command.min_z = viewport->MinZ;
    command.max_z = viewport->MaxZ;
    command.reserved = 0;
    return emit_command(D9WG_OP_SET_VIEWPORT, &command, sizeof(command))
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_get_viewport(IDirect3DDevice9 *iface,
        D3DVIEWPORT9 *viewport)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetViewport");
    if (!viewport)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *viewport = device_from_iface(iface)->viewport;
    return D3D_OK;
}

static HRESULT WINAPI device_set_render_state(IDirect3DDevice9 *iface,
        D3DRENDERSTATETYPE state, DWORD value)
{
    D9Device *device = device_from_iface(iface);
    D9WGSetRenderState command;
    if ((UINT)state >= D9_MAX_RENDER_STATES)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (state_block_record_render_state(device, (UINT)state, value)) return D3D_OK;
    if (device->render_states[state] == value)
        return D3D_OK;
    /*
     * Adaptive tessellation is the one part of the higher-order primitive
     * family with nothing behind it. It is not a refinement of the N-patch
     * level that could be approximated: D3D9 ties it to displacement mapping,
     * where the per-edge level comes from sampling D3DDMAPSAMPLER's texture
     * *during* tessellation, and nothing in a stage-less pipeline can sample a
     * texture there. Setting it therefore changes nothing, and a title that
     * enables it and sees no extra detail deserves to be told why rather than
     * left to wonder whether its meshes are wrong.
     */
    if (state == D3DRS_ENABLEADAPTIVETESSELLATION && value)
        HOSTLOG_REFUSED("D3DRS_ENABLEADAPTIVETESSELLATION is not implemented: "
                "its per-edge level comes from sampling D3DDMAPSAMPLER during "
                "tessellation, which has no equivalent here. N-patches still "
                "tessellate at the SetNPatchMode level");
    /*
     * The vendor render-state hacks share the D3DRS_ADAPTIVETESS_* slots,
     * because D3D9 never grew states of their own for them. ADAPTIVETESS_Y
     * carrying 'ATOC' is alpha-to-coverage, which the host implements natively;
     * ADAPTIVETESS_X carrying 'NVDB' is the depth bounds test, which nothing
     * can. Naming the second one matters because it fails *permissively*: an
     * app that enables depth bounds and is ignored draws pixels it meant to
     * reject, which reads as a depth bug rather than a missing feature.
     */
    if (state == D3DRS_ADAPTIVETESS_X && value == D9FOURCC_NVDB)
        HOSTLOG_REFUSED("the NVDB depth bounds test is not implemented: "
                "WebGPU has no depth bounds state, and ignoring it draws "
                "pixels the app meant to reject rather than fewer");
    /*
     * RESZ: the app writes a magic value into D3DRS_POINTSIZE to ask the driver
     * to resolve the bound multisampled depth buffer into an INTZ texture. It
     * cannot be served here, and the reason is a hard one rather than a missing
     * feature: the host's depth attachment is depth24plus-stencil8, and WebGPU
     * gives that format no copy-source capability at all, so there is no
     * operation -- copy, blit or resolve -- that can read it out. WebGPU has no
     * multisampled depth resolve either.
     *
     * What the trick exists to work around is binding the depth buffer as a
     * texture while it is also the depth attachment. That specific need is
     * already met: an INTZ texture can be bound to a sampler and read directly
     * (see depthSampleModeFor in the executor), which is the path a title
     * should take here.
     *
     * The magic value is a float NaN pattern, so no legitimate point size can
     * collide with it.
     */
    if (state == D3DRS_POINTSIZE && value == 0x7FA05000u)
        HOSTLOG_REFUSED("the RESZ depth-resolve trick is not implemented: the "
                "host's depth attachment is depth24plus-stencil8, which WebGPU "
                "gives no copy-source capability, so nothing can read it out. "
                "Bind an INTZ texture and sample it directly instead");
    device->render_states[state] = value;
    command.device_handle = device->handle;
    command.state = state;
    command.value = value;
    command.reserved = 0;
    return emit_command(D9WG_OP_SET_RENDER_STATE, &command, sizeof(command))
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_get_render_state(IDirect3DDevice9 *iface,
        D3DRENDERSTATETYPE state, DWORD *value)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetRenderState");
    if (!value || (UINT)state >= D9_MAX_RENDER_STATES)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *value = device_from_iface(iface)->render_states[state];
    return D3D_OK;
}

static BOOL texture_binding_slot(DWORD stage, UINT *slot)
{
    if (stage < 16u) {
        *slot = stage;
        return TRUE;
    }
    if (stage >= D3DVERTEXTEXTURESAMPLER0
            && stage < D3DVERTEXTEXTURESAMPLER0 + 4u) {
        *slot = 16u
                + stage - D3DVERTEXTEXTURESAMPLER0;
        return TRUE;
    }
    return FALSE;
}

static DWORD texture_binding_stage(UINT slot)
{
    return slot < 16u ? slot
            : D3DVERTEXTEXTURESAMPLER0
                + slot - 16u;
}

static BOOL sampler_state_slot(DWORD sampler, UINT *slot)
{
    if (sampler < 16u) {
        *slot = sampler;
        return TRUE;
    }
    if (sampler >= D3DVERTEXTEXTURESAMPLER0
            && sampler < D3DVERTEXTEXTURESAMPLER0 + 4u) {
        *slot = 16u + sampler - D3DVERTEXTEXTURESAMPLER0;
        return TRUE;
    }
    return FALSE;
}

static DWORD sampler_state_index(UINT slot)
{
    return slot < 16u ? slot
            : D3DVERTEXTEXTURESAMPLER0 + slot - 16u;
}

static HRESULT WINAPI device_get_texture(IDirect3DDevice9 *iface,
        DWORD stage, IDirect3DBaseTexture9 **texture_out)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetTexture");
    D9Device *device = device_from_iface(iface);
    UINT slot;
    if (!texture_out || !texture_binding_slot(stage, &slot))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (device->textures[slot])
        *texture_out = (IDirect3DBaseTexture9 *)&device->textures[slot]->iface;
    else if (device->cube_bindings[slot])
        *texture_out = (IDirect3DBaseTexture9 *)&device->cube_bindings[slot]->iface;
    else if (device->volume_bindings[slot])
        *texture_out = (IDirect3DBaseTexture9 *)&device->volume_bindings[slot]->iface;
    else
        *texture_out = NULL;
    if (*texture_out)
        IDirect3DBaseTexture9_AddRef(*texture_out);
    return D3D_OK;
}

/*
 * SetTexture takes an IDirect3DBaseTexture9*, so the concrete type has to be
 * recovered from the vtable pointer -- there is no tag in the interface. Doing
 * it by vtable is also the validation: a pointer that is neither of ours is
 * rejected instead of being dereferenced as if it were.
 *
 * Cube textures (M3) bind through the same D9WG_OP_SET_TEXTURE, because the
 * host resource table is one flat handle space and the *host* already knows
 * each handle's kind from its CREATE command. Sending the kind again here would
 * be a second source of truth for the same fact.
 */
static HRESULT WINAPI device_set_texture(IDirect3DDevice9 *iface,
        DWORD stage, IDirect3DBaseTexture9 *texture_iface)
{
    D9Device *device = device_from_iface(iface);
    D9Texture *texture = NULL;
    D9CubeTexture *cube = NULL;
    D9VolumeTexture *volume = NULL;
    uint32_t handle = 0;
    UINT slot;
    D9WGSetTexture command;

    if (!texture_binding_slot(stage, &slot))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (texture_iface) {
        const void *vtbl = ((const IDirect3DTexture9 *)texture_iface)->lpVtbl;
        if (vtbl == (const void *)&g_texture_vtbl) {
            texture = (D9Texture *)texture_iface;
            if (texture->device != device)
                return TRACE_REFUSE(D3DERR_INVALIDCALL);
            handle = texture->handle;
        } else if (vtbl == (const void *)&g_cube_vtbl) {
            cube = (D9CubeTexture *)texture_iface;
            if (cube->device != device)
                return TRACE_REFUSE(D3DERR_INVALIDCALL);
            handle = cube->handle;
        } else if (vtbl == (const void *)&g_volume_texture_vtbl) {
            volume = (D9VolumeTexture *)texture_iface;
            if (volume->device != device)
                return TRACE_REFUSE(D3DERR_INVALIDCALL);
            handle = volume->handle;
        } else {
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        }
    }
    if (device->recording_result) {
        D9StateBlock *block = device->recording_result;
        D9Texture *old_texture = block->textures[slot];
        D9CubeTexture *old_cube = block->cube_textures[slot];
        D9VolumeTexture *old_volume = block->volume_textures[slot];
        if (texture) texture_binding_add_ref(texture);
        if (cube) cube_binding_add_ref(cube);
        if (volume) volume_texture_binding_add_ref(volume);
        block->has_texture[slot] = TRUE;block->textures[slot] = texture;
        block->cube_textures[slot] = cube;block->volume_textures[slot] = volume;
        if (old_texture) texture_binding_release(old_texture);
        if (old_cube) cube_binding_release(old_cube);
        if (old_volume) volume_texture_binding_release(old_volume);
        return D3D_OK;
    }
    if (device->textures[slot] == texture && device->cube_bindings[slot] == cube
            && device->volume_bindings[slot] == volume)
        return D3D_OK;
    D9Texture *old_texture = device->textures[slot];
    D9CubeTexture *old_cube = device->cube_bindings[slot];
    D9VolumeTexture *old_volume = device->volume_bindings[slot];
    if (texture) texture_binding_add_ref(texture);
    if (cube) cube_binding_add_ref(cube);
    if (volume) volume_texture_binding_add_ref(volume);
    device->textures[slot] = texture;
    device->cube_bindings[slot] = cube;
    device->volume_bindings[slot] = volume;
    if (old_texture) texture_binding_release(old_texture);
    if (old_cube) cube_binding_release(old_cube);
    if (old_volume) volume_texture_binding_release(old_volume);
    command.device_handle = device->handle;
    command.stage = stage;
    command.texture_handle = handle;
    command.reserved = 0;
    return emit_command(D9WG_OP_SET_TEXTURE, &command, sizeof(command))
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_get_texture_stage_state(IDirect3DDevice9 *iface,
        DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD *value)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetTextureStageState");
    if (!value || stage >= D9_MAX_TEXTURE_STAGES
            || (UINT)state >= D9_MAX_TEXTURE_STAGE_STATES)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *value = device_from_iface(iface)->texture_stage_states[stage][state];
    return D3D_OK;
}

static HRESULT WINAPI device_set_texture_stage_state(IDirect3DDevice9 *iface,
        DWORD stage, D3DTEXTURESTAGESTATETYPE state, DWORD value)
{
    D9Device *device = device_from_iface(iface);
    D9WGSetTextureStageState command;
    if (stage >= D9_MAX_TEXTURE_STAGES
            || (UINT)state >= D9_MAX_TEXTURE_STAGE_STATES)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (state_block_record_texture_stage_state(device, stage, (UINT)state, value)) return D3D_OK;
    if (device->texture_stage_states[stage][state] == value)
        return D3D_OK;
    device->texture_stage_states[stage][state] = value;
    command.device_handle = device->handle;
    command.stage = stage;
    command.state = state;
    command.value = value;
    return emit_command(D9WG_OP_SET_TEXTURE_STAGE_STATE, &command,
            sizeof(command)) ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_validate_device(IDirect3DDevice9 *iface,
        DWORD *passes)
{
    (void)iface;
    if (!passes)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *passes = 1;
    TRACE("OK Device.ValidateDevice passes=1");
    return D3D_OK;
}

/*
 * These two used to disagree: the setter returned D3D_OK and the getter always
 * answered FALSE, so an app that set software processing and read it back was
 * told its own call had not happened. That is worse than a refusal -- a
 * refusal is a fact the app can branch on, while a silent disagreement looks
 * like a driver bug in whatever renders wrong three frames later.
 *
 * D3D9's rule is that the mode is fixed at creation except on a mixed device,
 * where this switches between the two. The switch is honoured as bookkeeping:
 * every vertex is processed on the GPU either way, because that is the only
 * place this backend can process one. What that costs is the one guarantee
 * software processing carries that hardware does not -- no caps limits, so
 * shader versions and constant counts beyond what the device advertises --
 * which a title only reaches by first reading caps that already say otherwise.
 */
static HRESULT WINAPI device_set_software_vertex_processing(
        IDirect3DDevice9 *iface, WINBOOL software)
{
    D9Device *device = device_from_iface(iface);
    const DWORD behavior = device->creation.BehaviorFlags;
    if (behavior & D3DCREATE_MIXED_VERTEXPROCESSING) {
        device->software_vertex_processing = software ? TRUE : FALSE;
        return D3D_OK;
    }
    /* Not a mixed device: the only accepted call is the one that asks for the
     * mode the device already has. */
    if (!software == !(behavior & D3DCREATE_SOFTWARE_VERTEXPROCESSING))
        return D3D_OK;
    UNSUPPORTED("Device.SetSoftwareVertexProcessing on a non-mixed device");
    return TRACE_REFUSE(D3DERR_INVALIDCALL);
}

static WINBOOL WINAPI device_get_software_vertex_processing(
        IDirect3DDevice9 *iface)
{
    D9Device *device = device_from_iface(iface);
    if (device->creation.BehaviorFlags & D3DCREATE_MIXED_VERTEXPROCESSING)
        return device->software_vertex_processing;
    return (device->creation.BehaviorFlags
            & D3DCREATE_SOFTWARE_VERTEXPROCESSING) ? TRUE : FALSE;
}

/*
 * N-patch tessellation is not implemented, and returning D3D_OK said it was.
 * An engine that enables N-patches and is told "yes" draws its un-tessellated
 * meshes and has no way to discover why they look faceted; told "no", it keeps
 * its own LOD path. D3D9 returns D3DERR_INVALIDCALL when the device cannot do
 * it, so refusing is the documented answer rather than an approximation.
 *
 * Setting it back to 0.0 -- "no tessellation" -- is what every title does on
 * shutdown and is always honoured.
 */
/*
 * N-patch (PN triangle) tessellation. Every subsequent draw has each of its
 * triangles replaced by a cubic Bezier triangle built from the three vertices'
 * positions and normals -- Vlachos et al.'s construction, which is what
 * D3DRS_POSITIONDEGREE/NORMALDEGREE's defaults describe.
 *
 * Like the RT patches, it is evaluated in the guest and drawn as an ordinary
 * indexed triangle list, so nothing about it reaches the protocol. Unlike them
 * it sits on the draw path, which is why the whole thing is gated on a level
 * above 1.0: at the default it costs one float comparison per draw.
 */
static HRESULT WINAPI device_set_npatch_mode(IDirect3DDevice9 *iface,
        float segments)
{
    D9Device *device = device_from_iface(iface);
    if (segments > (float)D9_PATCH_MAX_SEGMENTS)
        segments = (float)D9_PATCH_MAX_SEGMENTS;
    if (state_block_record_npatch_mode(device, segments > 1.0f ? segments : 0.0f)) return D3D_OK;
    device->npatch_segments = segments > 1.0f ? segments : 0.0f;
    return D3D_OK;
}

static float WINAPI device_get_npatch_mode(IDirect3DDevice9 *iface)
{ return device_from_iface(iface)->npatch_segments; }

/* ---- IDirect3DDevice9: resources, streams, draws ---- */

static HRESULT WINAPI device_create_vertex_buffer(IDirect3DDevice9 *iface,
        UINT length, DWORD usage, DWORD fvf, D3DPOOL pool,
        IDirect3DVertexBuffer9 **buffer_out, HANDLE *shared_handle)
{
    D9Device *device = device_from_iface(iface);
    D9VertexBuffer *buffer;
    (void)shared_handle;
    TRACE_MARK_ENTER("Device.CreateVertexBuffer");
    if (!buffer_out) {
        TRACE("FAIL CreateVertexBuffer length=%lu missing output -> %08lX",
                length, (DWORD)D3DERR_INVALIDCALL);
        TRACE_MARK_EXIT("Device.CreateVertexBuffer", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    *buffer_out = NULL;
    if (!length || pool > D3DPOOL_SCRATCH) {
        TRACE("FAIL CreateVertexBuffer length=%lu usage=%08lX fvf=%08lX pool=%lu -> %08lX",
                length, usage, fvf, (DWORD)pool, (DWORD)D3DERR_INVALIDCALL);
        HOSTLOG_FAILED("CreateVertexBuffer refused: length=%lu usage=%08lX "
                "fvf=%08lX pool=%lu", length, usage, fvf, (DWORD)pool);
        TRACE_MARK_EXIT("Device.CreateVertexBuffer", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    buffer = (D9VertexBuffer *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*buffer));
    if (!buffer) {
        TRACE("FAIL CreateVertexBuffer object allocation length=%lu -> %08lX",
                length, (DWORD)E_OUTOFMEMORY);
        TRACE_MARK_EXIT("Device.CreateVertexBuffer", E_OUTOFMEMORY, NULL);
        TRACE_FLUSH();
        return E_OUTOFMEMORY;
    }
    buffer->shadow = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            length);
    if (!buffer->shadow) {
        HeapFree(GetProcessHeap(), 0, buffer);
        TRACE("FAIL CreateVertexBuffer shadow allocation length=%lu -> %08lX",
                length, (DWORD)E_OUTOFMEMORY);
        TRACE_MARK_EXIT("Device.CreateVertexBuffer", E_OUTOFMEMORY, NULL);
        TRACE_FLUSH();
        return E_OUTOFMEMORY;
    }
    buffer->iface.lpVtbl = &g_vb_vtbl;
    buffer->refcount = 1;
    buffer->device = device;
    device_child_add_ref(device);
    buffer->handle = allocate_handle();
    buffer->length = length;
    buffer->usage = usage;
    buffer->fvf = fvf;
    buffer->pool = pool;

    if (!emit_vertex_buffer_create(device, buffer)) {
        TRACE("FAIL CreateVertexBuffer transport length=%lu handle=%08lX -> %08lX",
                length, buffer->handle, (DWORD)D3DERR_DRIVERINTERNALERROR);
        device_child_release(device);
        HeapFree(GetProcessHeap(), 0, buffer->shadow);
        HeapFree(GetProcessHeap(), 0, buffer);
        TRACE_MARK_EXIT("Device.CreateVertexBuffer",
                D3DERR_DRIVERINTERNALERROR, NULL);
        TRACE_FLUSH();
        return D3DERR_DRIVERINTERNALERROR;
    }
    buffer->next_device_resource = device->vertex_buffers;
    device->vertex_buffers = buffer;
    *buffer_out = &buffer->iface;
    TRACE_BUFFER_CREATED(FALSE, length);
    TRACE_REGISTER_RANGE("VB_OBJECT", g_trace_vb_count, buffer, buffer,
            sizeof(*buffer));
    TRACE_REGISTER_RANGE("VB_SHADOW", g_trace_vb_count, buffer,
            buffer->shadow, buffer->length);
#ifdef D9WG_DIAGNOSTIC_TRACE
    if (g_trace_vb_count <= 4 || !(g_trace_vb_count & 127))
        TRACE("OK CreateVertexBuffer count=%ld bytes=%lu length=%lu usage=%08lX fvf=%08lX pool=%lu handle=%08lX object=%08lX shadow=%08lX shadow_end=%08lX",
                g_trace_vb_count, (DWORD)g_trace_vb_bytes, length, usage, fvf,
                (DWORD)pool, buffer->handle, (DWORD)(uintptr_t)*buffer_out,
                (DWORD)(uintptr_t)buffer->shadow,
                (DWORD)(uintptr_t)(buffer->shadow + buffer->length));
#endif
    TRACE_MARK_EXIT("Device.CreateVertexBuffer", D3D_OK, *buffer_out);
    return D3D_OK;
}

static HRESULT WINAPI device_create_index_buffer(IDirect3DDevice9 *iface,
        UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool,
        IDirect3DIndexBuffer9 **buffer_out, HANDLE *shared_handle)
{
    D9Device *device = device_from_iface(iface);
    D9IndexBuffer *buffer;
    UINT index_size;
    (void)shared_handle;

    TRACE_MARK_ENTER("Device.CreateIndexBuffer");
    if (!buffer_out) {
        TRACE("FAIL CreateIndexBuffer length=%lu missing output -> %08lX",
                length, (DWORD)D3DERR_INVALIDCALL);
        TRACE_MARK_EXIT("Device.CreateIndexBuffer", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    *buffer_out = NULL;
    if (!length || pool > D3DPOOL_SCRATCH) {
        TRACE("FAIL CreateIndexBuffer length=%lu usage=%08lX format=%08lX pool=%lu -> %08lX",
                length, usage, (DWORD)format, (DWORD)pool,
                (DWORD)D3DERR_INVALIDCALL);
        HOSTLOG_FAILED("CreateIndexBuffer refused: length=%lu usage=%08lX "
                "format=%08lX pool=%lu", length, usage, (DWORD)format,
                (DWORD)pool);
        TRACE_MARK_EXIT("Device.CreateIndexBuffer", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    if (format == D3DFMT_INDEX16) index_size = 2;
    else if (format == D3DFMT_INDEX32) index_size = 4;
    else {
        TRACE("FAIL CreateIndexBuffer format=%08lX -> %08lX",
                (DWORD)format, (DWORD)D3DERR_INVALIDCALL);
        TRACE_MARK_EXIT("Device.CreateIndexBuffer", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    if (length % index_size) {
        TRACE("FAIL CreateIndexBuffer unaligned length=%lu index_size=%lu -> %08lX",
                length, index_size, (DWORD)D3DERR_INVALIDCALL);
        TRACE_MARK_EXIT("Device.CreateIndexBuffer", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }

    buffer = (D9IndexBuffer *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*buffer));
    if (!buffer) {
        TRACE("FAIL CreateIndexBuffer object allocation length=%lu -> %08lX",
                length, (DWORD)E_OUTOFMEMORY);
        TRACE_MARK_EXIT("Device.CreateIndexBuffer", E_OUTOFMEMORY, NULL);
        TRACE_FLUSH();
        return E_OUTOFMEMORY;
    }
    buffer->shadow = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            length);
    if (!buffer->shadow) {
        HeapFree(GetProcessHeap(), 0, buffer);
        TRACE("FAIL CreateIndexBuffer shadow allocation length=%lu -> %08lX",
                length, (DWORD)E_OUTOFMEMORY);
        TRACE_MARK_EXIT("Device.CreateIndexBuffer", E_OUTOFMEMORY, NULL);
        TRACE_FLUSH();
        return E_OUTOFMEMORY;
    }
    buffer->iface.lpVtbl = &g_ib_vtbl;
    buffer->refcount = 1;
    buffer->device = device;
    device_child_add_ref(device);
    buffer->handle = allocate_handle();
    buffer->length = length;
    buffer->usage = usage;
    buffer->format = format;
    buffer->pool = pool;

    if (!emit_index_buffer_create(device, buffer)) {
        TRACE("FAIL CreateIndexBuffer transport length=%lu handle=%08lX -> %08lX",
                length, buffer->handle, (DWORD)D3DERR_DRIVERINTERNALERROR);
        device_child_release(device);
        HeapFree(GetProcessHeap(), 0, buffer->shadow);
        HeapFree(GetProcessHeap(), 0, buffer);
        TRACE_MARK_EXIT("Device.CreateIndexBuffer",
                D3DERR_DRIVERINTERNALERROR, NULL);
        TRACE_FLUSH();
        return D3DERR_DRIVERINTERNALERROR;
    }
    buffer->next_device_resource = device->index_buffers;
    device->index_buffers = buffer;
    *buffer_out = &buffer->iface;
    TRACE_BUFFER_CREATED(TRUE, length);
    TRACE_REGISTER_RANGE("IB_OBJECT", g_trace_ib_count, buffer, buffer,
            sizeof(*buffer));
    TRACE_REGISTER_RANGE("IB_SHADOW", g_trace_ib_count, buffer,
            buffer->shadow, buffer->length);
#ifdef D9WG_DIAGNOSTIC_TRACE
    if (g_trace_ib_count <= 4 || !(g_trace_ib_count & 127))
        TRACE("OK CreateIndexBuffer count=%ld bytes=%lu length=%lu usage=%08lX format=%08lX pool=%lu handle=%08lX object=%08lX shadow=%08lX shadow_end=%08lX",
                g_trace_ib_count, (DWORD)g_trace_ib_bytes, length, usage,
                (DWORD)format, (DWORD)pool, buffer->handle,
                (DWORD)(uintptr_t)*buffer_out,
                (DWORD)(uintptr_t)buffer->shadow,
                (DWORD)(uintptr_t)(buffer->shadow + buffer->length));
#endif
    TRACE_MARK_EXIT("Device.CreateIndexBuffer", D3D_OK, *buffer_out);
    return D3D_OK;
}

static HRESULT create_texture_object(D9Device *device,
        UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format,
        D3DPOOL pool, UINT api, IDirect3DTexture9 **texture_out)
{
    D9Texture *texture;
    UINT full_levels;
    UINT level;
    UINT level_width;
    UINT level_height;
    HRESULT failure = E_OUTOFMEMORY;
    HRESULT success = D3D_OK;
    BOOL is_depth = (usage & D3DUSAGE_DEPTHSTENCIL) != 0;

    TRACE("CALL CreateTexture %lux%lu levels=%lu usage=%08lX "
            "format=%08lX pool=%lu", width, height, levels, usage,
            (DWORD)format, (DWORD)pool);

    if (!texture_out) {
        TRACE("FAIL CreateTexture missing output -> %08lX",
                (DWORD)D3DERR_INVALIDCALL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    *texture_out = NULL;
    if (api == 8u) {
        if (!width || !height || width > 4096 || height > 4096
                || !supported_format8(format, FALSE)
                || (usage & D3DUSAGE_DEPTHSTENCIL)
                || ((usage & D3DUSAGE_RENDERTARGET)
                    && (pool != D3DPOOL_DEFAULT || levels != 1
                        || !supported_render_target_format8(format)))
                || pool > D3DPOOL_SCRATCH)
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
    } else {
        /*
         * D3DUSAGE_AUTOGENMIPMAP: the sublevels exist but only level 0 is the
         * app's. D3D9 reports GetLevelCount() == 1 for such a texture and hands out
         * no surface below the top, because the driver owns the rest -- so one
         * visible level here is not a simplification, it is the documented shape.
         *
         * The flag stays on `usage` rather than being stripped: it is what tells
         * the host to allocate the full chain behind that single visible level and
         * to refill it whenever level 0 changes. It is masked out only for the
         * capability predicate, which answers which resources can exist and has no
         * opinion on how their mips get filled.
         */
        if (!d9wg_autogen_creation_valid(levels, usage, pool))
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        if (usage & D3DUSAGE_AUTOGENMIPMAP)
            levels = 1;
        if (!width || !height || width > 8192 || height > 8192
                || !texture_create_supported(D3DRTYPE_TEXTURE,
                        usage & ~(DWORD)D3DUSAGE_AUTOGENMIPMAP, format, pool)) {

            TRACE("FAIL CreateTexture invalid arguments -> %08lX",
                    (DWORD)D3DERR_INVALIDCALL);
            HOSTLOG_FAILED("CreateTexture refused: %lux%lu levels=%lu usage=%08lX "
                    "format=%08lX pool=%lu", width, height, levels, usage,
                    (DWORD)format, (DWORD)pool);
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        }
    }
    full_levels = full_mip_level_count(width, height);
    if (!levels) levels = full_levels;
    if (levels > full_levels) {
        TRACE("FAIL CreateTexture levels=%lu exceeds full_levels=%lu -> %08lX",
                levels, full_levels, (DWORD)D3DERR_INVALIDCALL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }

    texture = (D9Texture *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*texture));
    if (!texture) {
        TRACE("FAIL CreateTexture object allocation -> %08lX",
                (DWORD)E_OUTOFMEMORY);
        return TRACE_REFUSE(E_OUTOFMEMORY);
    }
    texture->levels = (D9TextureLevel *)HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY, levels * sizeof(*texture->levels));
    if (!texture->levels) {
        HeapFree(GetProcessHeap(), 0, texture);
        TRACE("FAIL CreateTexture level allocation levels=%lu -> %08lX",
                levels, (DWORD)E_OUTOFMEMORY);
        return TRACE_REFUSE(E_OUTOFMEMORY);
    }
    if (api == 9u && (usage & D3DUSAGE_AUTOGENMIPMAP))
        texture->autogen_filter = (D3DTEXTUREFILTERTYPE)d9wg_autogen_default_filter(format,
                (d9wg_format_query_info(format) & 4u) ? runtime_host_features() : 0);
    texture->iface.lpVtbl = &g_texture_vtbl;
    texture->refcount = 1;
    texture->device = device;
    texture->handle = allocate_handle();
    texture->width = width;
    texture->height = height;
    texture->level_count = levels;
    texture->usage = usage;
    texture->format = format;
    texture->pool = pool;
    device_child_add_ref(device);

    level_width = width;
    level_height = height;
    for (level = 0; level < levels; ++level) {
        D9TextureLevel *level_data = &texture->levels[level];
        level_data->width = level_width;
        level_data->height = level_height;
        if (!is_depth && !d9wg_texture_level_layout(api, format, level_width, level_height,
                &level_data->row_pitch, &level_data->row_count,
                &level_data->byte_count))
            goto allocation_failed;
        /* Targets are not directly lockable.  A render target can acquire a
         * lazy CPU mirror later when Clear/ColorFill gives us exact contents;
         * texture_lock_level() therefore rejects the usage flags explicitly. */
        if (api == 9u && (usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL))) {
            if (level_width > 1) level_width >>= 1;
            if (level_height > 1) level_height >>= 1;
            continue;
        }
        level_data->shadow = (BYTE *)HeapAlloc(GetProcessHeap(),
                HEAP_ZERO_MEMORY, level_data->byte_count);
        if (!level_data->shadow)
            goto allocation_failed;
        level_data->shadow_valid = TRUE;
        if (level_width > 1) level_width >>= 1;
        if (level_height > 1) level_height >>= 1;
    }

    if (!emit_texture_create(device, texture)) {
        failure = D3DERR_DRIVERINTERNALERROR;
        goto allocation_failed;
    }
    texture->next_device_resource = device->texture_resources;
    device->texture_resources = texture;
    *texture_out = &texture->iface;
    TRACE_REGISTER_RANGE("TEXTURE_OBJECT", texture->handle, texture, texture,
            sizeof(*texture));
    TRACE_REGISTER_RANGE("TEXTURE_SHADOW0", texture->handle, texture,
            texture->levels[0].shadow, texture->levels[0].byte_count);
    TRACE("OK CreateTexture handle=%08lX object=%08lX %lux%lu levels=%lu "
            "usage=%08lX format=%08lX pool=%lu shadow0=%08lX "
            "shadow0_end=%08lX shadow0_bytes=%lu", texture->handle,
            (DWORD)(uintptr_t)*texture_out, width, height, levels, usage,
            (DWORD)format, (DWORD)pool,
            (DWORD)(uintptr_t)texture->levels[0].shadow,
            (DWORD)(uintptr_t)(texture->levels[0].shadow
                    ? texture->levels[0].shadow
                            + texture->levels[0].byte_count
                    : NULL),
            texture->levels[0].byte_count);
    return success;

allocation_failed:
    for (level = 0; level < levels; ++level) {
        if (texture->levels[level].shadow)
            HeapFree(GetProcessHeap(), 0, texture->levels[level].shadow);
    }
    device_child_release(device);
    HeapFree(GetProcessHeap(), 0, texture->levels);
    HeapFree(GetProcessHeap(), 0, texture);
    TRACE("FAIL CreateTexture %lux%lu levels=%lu usage=%08lX "
            "format=%08lX pool=%lu -> %08lX", width, height, levels, usage,
            (DWORD)format, (DWORD)pool, (DWORD)failure);
    HOSTLOG_FAILED("CreateTexture failed: %lux%lu levels=%lu usage=%08lX "
            "format=%08lX pool=%lu hr=%08lX", width, height, levels, usage,
            (DWORD)format, (DWORD)pool, (DWORD)failure);
    return failure;
}

static HRESULT WINAPI device_create_texture(IDirect3DDevice9 *iface,
        UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format,
        D3DPOOL pool, IDirect3DTexture9 **texture_out, HANDLE *shared_handle)
{
    (void)shared_handle;
    return create_texture_object(device_from_iface(iface), width, height,
            levels, usage, format, pool, 9u, texture_out);
}

/* Internal updates restore exact captured metadata, including NULL buffers.
 * The public SetStreamSource wrapper applies its live-metadata NULL rule. */
static HRESULT device_set_stream_binding(D9Device *device, UINT stream,
        D9VertexBuffer *buffer, UINT offset, UINT stride)
{
    D9WGSetStreamSource command;
    if (device->streams[stream].buffer == buffer
            && device->streams[stream].stride == stride
            && device->streams[stream].offset == offset)
        return D3D_OK;
    D9VertexBuffer *old_buffer = device->streams[stream].buffer;
    if (buffer) vb_binding_add_ref(buffer);
    device->streams[stream].buffer = buffer;
    device->streams[stream].stride = stride;
    device->streams[stream].offset = offset;
    if (old_buffer) vb_binding_release(old_buffer);
    command.device_handle = device->handle;
    command.stream = stream;
    command.buffer_handle = buffer ? buffer->handle : 0;
    command.stride = stride;
    command.offset_in_bytes = offset;
    command.reserved = 0;
    return emit_command(D9WG_OP_SET_STREAM_SOURCE, &command, sizeof(command))
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_set_stream_source(IDirect3DDevice9 *iface,
        UINT stream, IDirect3DVertexBuffer9 *buffer_iface, UINT offset,
        UINT stride)
{
    D9Device *device = device_from_iface(iface);
    D9VertexBuffer *buffer = buffer_iface ? vb_from_iface(buffer_iface) : NULL;
    if (stream >= D9_MAX_STREAMS) {

        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    if (buffer && (buffer_iface->lpVtbl != &g_vb_vtbl
            || buffer->device != device))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (buffer && offset >= buffer->length)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!buffer) {
        /* Retail keeps the current live offset/stride, even while recording. */
        offset = device->streams[stream].offset;
        stride = device->streams[stream].stride;
    }
    if (device->recording_result) {
        D9StateBlock *block = device->recording_result;
        D9VertexBuffer *old = block->streams[stream].buffer;
        if (buffer) vb_binding_add_ref(buffer);
        block->has_stream[stream] = TRUE;block->streams[stream].buffer = buffer;
        block->streams[stream].offset = offset;block->streams[stream].stride = stride;
        if (old) vb_binding_release(old);
        return D3D_OK;
    }
    return device_set_stream_binding(device, stream, buffer, offset, stride);
}

static HRESULT WINAPI device_get_stream_source(IDirect3DDevice9 *iface,
        UINT stream, IDirect3DVertexBuffer9 **buffer_out, UINT *offset_out,
        UINT *stride_out)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetStreamSource");
    D9Device *device = device_from_iface(iface);
    if (stream >= D9_MAX_STREAMS || !buffer_out || !stride_out)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (offset_out) *offset_out = device->streams[stream].offset;
    *stride_out = device->streams[stream].stride;
    *buffer_out = device->streams[stream].buffer
            ? &device->streams[stream].buffer->iface : NULL;
    if (*buffer_out)
        IDirect3DVertexBuffer9_AddRef(*buffer_out);
    return D3D_OK;
}

static HRESULT WINAPI device_set_indices(IDirect3DDevice9 *iface,
        IDirect3DIndexBuffer9 *buffer_iface)
{
    D9Device *device = device_from_iface(iface);
    D9IndexBuffer *buffer = buffer_iface ? ib_from_iface(buffer_iface) : NULL;
    D9WGSetIndices command;

    if (buffer && (buffer_iface->lpVtbl != &g_ib_vtbl
            || buffer->device != device))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (device->recording_result) {
        D9StateBlock *block = device->recording_result;
        D9IndexBuffer *old = block->index_buffer;
        if (buffer) ib_binding_add_ref(buffer);
        block->has_indices = TRUE;block->index_buffer = buffer;
        if (old) ib_binding_release(old);
        return D3D_OK;
    }
    if (device->index_buffer == buffer)
        return D3D_OK;
    D9IndexBuffer *old_buffer = device->index_buffer;
    if (buffer) ib_binding_add_ref(buffer);
    device->index_buffer = buffer;
    if (old_buffer) ib_binding_release(old_buffer);
    command.device_handle = device->handle;
    command.buffer_handle = buffer ? buffer->handle : 0;
    return emit_command(D9WG_OP_SET_INDICES, &command, sizeof(command))
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_get_indices(IDirect3DDevice9 *iface,
        IDirect3DIndexBuffer9 **buffer_out)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetIndices");
    D9Device *device = device_from_iface(iface);
    if (!buffer_out)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *buffer_out = device->index_buffer ? &device->index_buffer->iface : NULL;
    if (*buffer_out)
        IDirect3DIndexBuffer9_AddRef(*buffer_out);
    return D3D_OK;
}

static BOOL device_has_vertex_format(D9Device *device)
{
    return device->vertex_declaration != NULL || device->fvf != 0;
}

static BOOL d9_current_source_layout(D9Device *device, D9WGProcessLayout *layout);
static int d9_current_source_position_transformed(D9Device *device);
static int d9_clip_stream_draw(D9Device *device, uint32_t primitive_type, const D9WGClipDrawSource *draw);
static HRESULT d9_bound_draw_range(D9Device *device, UINT first, UINT count, INT base_vertex);
static HRESULT d9_draw_index_bounds(D9Device *device, INT base_vertex, D9WGDrawIndexBounds *bounds);
static HRESULT d9_prepare_stream_clip_status(D9Device *device, const D9WGClipDrawSource *source,
        D9WGClipStatus *next, BOOL *valid);


static int d9_clip_emit(void *context, uint32_t primitive_type,
        const void *vertices, uint32_t count, uint32_t stride)
{
    D9Device *device = context;
    D9WGDrawPrimitiveUP command;ZeroMemory(&command, sizeof(command));
    command.device_handle = device->handle;command.primitive_type = primitive_type;
    command.primitive_count = primitive_type == 1 ? count : primitive_type == 2 ? count/2u : count/3u;
    command.stride = stride;command.vertex_count = count;command.vertex_bytes = count*stride;
    /* Internal emission must preserve the application's bound VB and IB. */
    return emit_draw_primitive_up(&command, vertices);
}
static int d9_clip_mask(void *context, uint32_t mask)
{
    D9Device *device = context;
    D9WGSetRenderState command = { device->handle, D3DRS_CLIPPLANEENABLE, mask, 0 };
    return emit_command(D9WG_OP_SET_RENDER_STATE, &command, sizeof(command));
}
static int d9_clip_vb_draw(D9Device *device, uint32_t primitive_type,
        uint32_t first, uint32_t count, const uint8_t *indices,
        uint32_t index_bytes, uint32_t index_stride, int32_t base_vertex,
        uint32_t min_vertex, uint32_t vertex_limit)
{
    D9WGClipDrawSource submitted;ZeroMemory(&submitted, sizeof(submitted));
    submitted.first_vertex = first;submitted.count = count;submitted.indices = indices;
    submitted.index_bytes = index_bytes;submitted.index_stride = index_stride;submitted.base_vertex = base_vertex;
    submitted.min_vertex = min_vertex;submitted.vertex_count = vertex_limit;
    D9VertexBuffer *buffer = device->streams[0].buffer;
    if (!buffer) return d9_clip_stream_draw(device, primitive_type, &submitted);
    D9WGProcessLayout layout;
    d9wg_clip_buffer_commit(&buffer->clips, NULL, 0, 0, d9wg_vertex_windows_clip_release, NULL);
    if (!buffer->clips.head) return d9_clip_stream_draw(device, primitive_type, &submitted);
    if (!d9_current_source_layout(device, &layout)) return d9_clip_stream_draw(device, primitive_type, &submitted);
    if (!layout.position_transformed) return 0;
    if (!d9wg_process_canonical_output(&layout)) return d9_clip_stream_draw(device, primitive_type, &submitted);
    if (!device->streams[0].stride) return d9_clip_stream_draw(device, primitive_type, &submitted);
    if (device->streams[0].frequency != 1) return -1;
    D9WGClipDrawSource source;ZeroMemory(&source, sizeof(source));
    source.clips = &buffer->clips;source.vertices = buffer->shadow;source.length = buffer->length;
    source.base = device->streams[0].offset;source.stride = device->streams[0].stride;
    source.first_vertex = first;source.count = count;source.indices = indices;
    source.index_bytes = index_bytes;source.index_stride = index_stride;source.base_vertex = base_vertex;
    source.min_vertex = min_vertex;source.vertex_count = vertex_limit;
    source.primitive_type = primitive_type;source.layout = &layout;
    source.flat_shading = device->render_states[D3DRS_SHADEMODE] == D3DSHADE_FLAT;
    source.viewport = (D9WGProcessViewport){ (float)device->viewport.X, (float)device->viewport.Y,
            (float)device->viewport.Width, (float)device->viewport.Height,
            device->viewport.MinZ, device->viewport.MaxZ };
    return d9wg_vertex_windows_clip_draw_planes(&source, d9_clip_emit, d9_clip_mask,
            device->render_states[D3DRS_CLIPPLANEENABLE], device);
}

static BOOL d9_software_vertex_processing(D9Device *device)
{
    if (device->creation.BehaviorFlags & D3DCREATE_MIXED_VERTEXPROCESSING)
        return device->software_vertex_processing;
    return (device->creation.BehaviorFlags & D3DCREATE_SOFTWARE_VERTEXPROCESSING) != 0;
}
static HRESULT d9_prepare_clip_status(D9Device *device, const D9WGClipDrawSource *input, BOOL bound_stream,
        D9WGClipStatus *next, BOOL *valid)
{
    *next = device->clip_status;*valid = device->clip_status_valid;
    if (!d9_software_vertex_processing(device)) return D3D_OK;
    D9WGProcessLayout layout;
    BOOL supported_layout = d9_current_source_layout(device, &layout);
    if (input->indices && input->min_vertex >= input->vertex_count) {
        if (!input->status_screen && ((supported_layout && layout.position_transformed) ||
                d9_current_source_position_transformed(device) == 1)) return D3D_OK;
        /* No trustworthy software range: preserve rendering without reading
         * arbitrary VB contents or returning a stale ClipStatus query. */
        if (!device->render_states[D3DRS_CLIPPING]) {
            next->clip_union = next->clip_intersection = 0;*valid = TRUE;
        } else *valid = FALSE;
        return D3D_OK;
    }
    if (bound_stream && (!supported_layout || !device->streams[0].stride))
        return d9_prepare_stream_clip_status(device, input, next, valid);
    BOOL pretransformed = supported_layout && layout.position_transformed;
    if ((!pretransformed && device->vertex_shader) || !supported_layout ||
            (!pretransformed && bound_stream && device->streams[0].frequency != 1)) {
        /* Rendering still uses the supported GPU path. Until the shared CPU
         * pipeline reads this layout/program, refuse a stale status query. */
        if (!device->render_states[D3DRS_CLIPPING]) {
            next->clip_union = next->clip_intersection = 0;*valid = TRUE;
        } else *valid = FALSE;
        return D3D_OK;
    }
    D9WGClipDrawSource source = *input;source.layout = &layout;
    uint32_t error = d9wg_vertex_windows_clip_status(&source, device->render_states,
            device->transforms[D3DTS_WORLD], 256, 0, device->transforms[D3DTS_VIEW], device->transforms[D3DTS_PROJECTION], device->clip_planes, next);
    if (!error && (!layout.position_transformed || source.status_screen)) *valid = TRUE;
    return TRACE_REFUSE(error == 2 ? E_OUTOFMEMORY : error ? D3DERR_INVALIDCALL : D3D_OK);
}

static HRESULT WINAPI device_draw_primitive(IDirect3DDevice9 *iface,
        D3DPRIMITIVETYPE primitive_type, UINT start_vertex,
        UINT primitive_count)
{
    D9Device *device = device_from_iface(iface);
    D9WGDrawPrimitive command;
    UINT vertex_count = 0;

    if (!device_has_vertex_format(device)
            || !primitive_element_count(primitive_type, primitive_count,
                    &vertex_count)) {
        TRACE_FRAME_REJECT();
        return D3DERR_INVALIDCALL;
    }
    if (!primitive_count) return D3D_OK;
    if (device->npatch_segments > 1.0f && device->streams[0].buffer && device->streams[0].stride) {
        HRESULT tessellated = d9_draw_npatch(iface, device, primitive_type,
                primitive_count, NULL, 0, start_vertex);
        if (SUCCEEDED(tessellated))
            return tessellated;
        /* Not tessellatable -- no normals, a shader bound, too many output
         * vertices for 16-bit indices. Fall through and draw it flat, which is
         * what the mesh looked like before N-patches were switched on. */
    }
    HRESULT range_result = d9_bound_draw_range(device, start_vertex, vertex_count, 0);
    if (FAILED(range_result)) return TRACE_REFUSE(range_result);
    D9WGClipDrawSource status_source;ZeroMemory(&status_source, sizeof(status_source));
    if (device->streams[0].buffer) {
        status_source.vertices = device->streams[0].buffer->shadow;status_source.length = device->streams[0].buffer->length;
    }
    status_source.base = device->streams[0].offset;status_source.stride = device->streams[0].stride;
    status_source.first_vertex = start_vertex;status_source.count = status_source.vertex_count = vertex_count;
    D9WGClipStatus next_status;BOOL next_status_valid;
    HRESULT status_result = d9_prepare_clip_status(device, &status_source, TRUE, &next_status, &next_status_valid);
    if (FAILED(status_result)) return status_result;
    int clipped = d9_clip_vb_draw(device, primitive_type, start_vertex, vertex_count, NULL, 0, 0, 0, 0, vertex_count);
    if (clipped) {
        if (clipped < 0) return TRACE_REFUSE(clipped == -3 ? E_OUTOFMEMORY :
                clipped == -2 ? D3DERR_DRIVERINTERNALERROR : D3DERR_INVALIDCALL);
        device->clip_status = next_status;device->clip_status_valid = next_status_valid;
        TRACE_FRAME_DRAW(primitive_count);invalidate_render_target_mirrors(device);
        return D3D_OK;
    }
    if (!d9wg_flush_draw_buffers(device, FALSE))
        return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
    command.device_handle = device->handle;
    command.primitive_type = primitive_type;
    command.start_vertex = start_vertex;
    command.primitive_count = primitive_count;

    if (!emit_command(D9WG_OP_DRAW_PRIMITIVE, &command, sizeof(command)))
        return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
    device->clip_status = next_status;device->clip_status_valid = next_status_valid;
    TRACE_FRAME_DRAW(primitive_count);
    invalidate_render_target_mirrors(device);
    return D3D_OK;
}

static HRESULT WINAPI device_draw_indexed_primitive(IDirect3DDevice9 *iface,
        D3DPRIMITIVETYPE primitive_type, INT base_vertex_index,
        UINT min_vertex_index, UINT vertex_count, UINT start_index,
        UINT primitive_count)
{
    D9Device *device = device_from_iface(iface);
    D9WGDrawIndexedPrimitive command;
    UINT index_count = 0;
    UINT index_size;
    UINT available_indices;

    if (!device->index_buffer
            || !device_has_vertex_format(device)
            || !primitive_element_count(primitive_type, primitive_count,
                    &index_count)) {
        TRACE_FRAME_REJECT();
        return D3DERR_INVALIDCALL;
    }
    if (!primitive_count) return D3D_OK;
    index_size = device->index_buffer->format == D3DFMT_INDEX16 ? 2u : 4u;
    available_indices = device->index_buffer->length / index_size;
    if (start_index > available_indices
            || index_count > available_indices - start_index) {
        TRACE_FRAME_REJECT();
        return D3DERR_INVALIDCALL;
    }
    if (device->npatch_segments > 1.0f && base_vertex_index >= 0 &&
            device->streams[0].buffer && device->streams[0].stride) {
        HRESULT tessellated = d9_draw_npatch(iface, device, primitive_type,
                primitive_count,
                device->index_buffer->shadow + start_index * index_size,
                index_size, (UINT)base_vertex_index);
        if (SUCCEEDED(tessellated))
            return tessellated;
        /* See the note in device_draw_primitive: falling through draws the
         * mesh flat rather than dropping it. */
    }
    D9WGDrawIndexBounds bounds;
    HRESULT range_result = d9_draw_index_bounds(device, base_vertex_index, &bounds);
    if (FAILED(range_result)) return TRACE_REFUSE(range_result);

    D9WGClipDrawSource status_source;ZeroMemory(&status_source, sizeof(status_source));
    if (device->streams[0].buffer) {
        status_source.vertices = device->streams[0].buffer->shadow;status_source.length = device->streams[0].buffer->length;
    }
    status_source.base = device->streams[0].offset;status_source.stride = device->streams[0].stride;
    status_source.indices = device->index_buffer->shadow + start_index*index_size;
    status_source.index_bytes = index_count*index_size;status_source.index_stride = index_size;
    /* Only a supported software classification needs to read index contents. */
    UINT status_index_count = d9_software_vertex_processing(device) && device->render_states[D3DRS_CLIPPING] &&
            !device->vertex_shader && device->streams[0].frequency == 1u &&
            d9_current_source_position_transformed(device) == 0 ? index_count : 0;
    BOOL status_hints = d9_software_vertex_processing(device) &&
            d9wg_draw_status_hints(&bounds, min_vertex_index, vertex_count,
                    status_source.indices, status_index_count, index_size);
    status_source.count = index_count;status_source.min_vertex = status_hints ? min_vertex_index : 0;
    status_source.vertex_count = status_hints ? min_vertex_index + vertex_count : 0;
    status_source.base_vertex = base_vertex_index;
    D9WGClipStatus next_status;BOOL next_status_valid;
    HRESULT status_result = d9_prepare_clip_status(device, &status_source, TRUE, &next_status, &next_status_valid);
    if (FAILED(status_result)) return status_result;
    int clipped = d9_clip_vb_draw(device, primitive_type, 0, index_count, device->index_buffer->shadow + start_index*index_size,
            index_count*index_size, index_size, base_vertex_index,
            bounds.min_vertex, bounds.vertex_limit);
    if (clipped) {
        if (clipped < 0) return TRACE_REFUSE(clipped == -3 ? E_OUTOFMEMORY :
                clipped == -2 ? D3DERR_DRIVERINTERNALERROR : D3DERR_INVALIDCALL);
        device->clip_status = next_status;device->clip_status_valid = next_status_valid;
        TRACE_FRAME_DRAW(primitive_count);invalidate_render_target_mirrors(device);
        return D3D_OK;
    }
    if (!d9wg_flush_draw_buffers(device, TRUE))
        return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
    command.device_handle = device->handle;
    command.primitive_type = primitive_type;
    command.base_vertex_index = base_vertex_index;
    command.min_vertex_index = min_vertex_index;
    command.vertex_count = vertex_count;
    command.start_index = start_index;
    command.primitive_count = primitive_count;
    if (!emit_command(D9WG_OP_DRAW_INDEXED_PRIMITIVE, &command,
            sizeof(command)))
        return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
    device->clip_status = next_status;device->clip_status_valid = next_status_valid;
    TRACE_FRAME_DRAW(primitive_count);
    invalidate_render_target_mirrors(device);
    return D3D_OK;
}

static void clear_stream_zero_after_up(D9Device *device)
{
    D9VertexBuffer *old = device->streams[0].buffer;
    device->streams[0].buffer = NULL;
    device->streams[0].offset = 0;
    device->streams[0].stride = 0;
    if (old) vb_binding_release(old);
}

static void clear_indices_after_indexed_up(D9Device *device)
{
    D9IndexBuffer *old = device->index_buffer;
    device->index_buffer = NULL;
    if (old) ib_binding_release(old);
}

/* The missing-declaration failure detaches storage without the successful
 * UP path's zeroing of stream metadata. This modifies live state even during
 * recording, just like successful UP; internal patch draws never use it. */
static void clear_bindings_after_up_missing_format(D9Device *device, BOOL indexed)
{
    D9VertexBuffer *old = device->streams[0].buffer;
    device->streams[0].buffer = NULL;
    if (old) vb_binding_release(old);
    if (indexed) clear_indices_after_indexed_up(device);
}

static HRESULT WINAPI device_draw_primitive_up(IDirect3DDevice9 *iface,
        D3DPRIMITIVETYPE primitive_type, UINT primitive_count,
        const void *vertex_data, UINT stride)
{
    /* A nonzero stride makes a zero-primitive public UP a no-op before
     * format/data validation and before either successful/failure unbinding. */
    if (stride && !primitive_count) return D3D_OK;
    D9Device *device = device_from_iface(iface);
    D9WGDrawPrimitiveUP command;
    UINT vertex_count;
    UINT vertex_bytes;
    BOOL result;

    if (!vertex_data || !stride
            || !primitive_count
            || !primitive_element_count(primitive_type, primitive_count,
                    &vertex_count)
            || !multiply_u32(vertex_count, stride, &vertex_bytes)) {
        TRACE_FRAME_REJECT();
        return D3DERR_INVALIDCALL;
    }
    if (!device_has_vertex_format(device)) {
        clear_bindings_after_up_missing_format(device, FALSE);
        TRACE_FRAME_REJECT();
        return D3DERR_INVALIDCALL;
    }
    ZeroMemory(&command, sizeof(command));
    command.device_handle = device->handle;
    command.primitive_type = primitive_type;
    command.primitive_count = primitive_count;
    command.stride = stride;
    command.vertex_count = vertex_count;
    command.vertex_bytes = vertex_bytes;
    D9WGClipDrawSource status_source;ZeroMemory(&status_source, sizeof(status_source));
    status_source.vertices = vertex_data;status_source.length = command.vertex_bytes;status_source.stride = stride;
    status_source.count = status_source.vertex_count = vertex_count;
    status_source.status_screen = 1;
    status_source.viewport = (D9WGProcessViewport){(float)device->viewport.X,(float)device->viewport.Y,
            (float)device->viewport.Width,(float)device->viewport.Height,device->viewport.MinZ,device->viewport.MaxZ};
    D9WGClipStatus next_status;BOOL next_status_valid;
    HRESULT status_result = d9_prepare_clip_status(device, &status_source, FALSE, &next_status, &next_status_valid);
    if (FAILED(status_result)) return status_result;
    result = emit_draw_primitive_up(&command, vertex_data);
    if (result) {
        device->clip_status = next_status;device->clip_status_valid = next_status_valid;
        TRACE_FRAME_DRAW(primitive_count);
        invalidate_render_target_mirrors(device);
        clear_stream_zero_after_up(device);
    }
    return result ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

/*
 * The draw of DrawIndexedPrimitiveUP, without the API's side effect of
 * unbinding stream 0 and the index buffer (including missing-format failure): patch tessellation draws through
 * here, and must leave the application's bindings alone (review S2-28: every
 * draw after a DrawRectPatch/DrawTriPatch was refused for want of stream 0).
 */
static HRESULT draw_indexed_up(D9Device *device, D3DPRIMITIVETYPE primitive_type,
        UINT min_vertex_index, UINT vertex_count, UINT primitive_count,
        const void *index_data, D3DFORMAT index_format,
        const void *vertex_data, UINT stride, BOOL track_status, BOOL public_up)
{
    D9WGDrawIndexedPrimitiveUP command;
    UINT index_count;
    UINT index_size;
    UINT vertex_elements;
    BOOL result;

    if (!index_data || !vertex_data || !stride
            || !primitive_count
            || !vertex_count
            || !primitive_element_count(primitive_type, primitive_count,
                    &index_count)) {
        TRACE_FRAME_REJECT();
        return D3DERR_INVALIDCALL;
    }
    if (index_format == D3DFMT_INDEX16) index_size = 2;
    else if (index_format == D3DFMT_INDEX32) index_size = 4;
    else { TRACE_FRAME_REJECT(); return D3DERR_INVALIDCALL; }
    if (min_vertex_index > 0xFFFFFFFFu - vertex_count) {
        TRACE_FRAME_REJECT();
        return D3DERR_INVALIDCALL;
    }
    vertex_elements = min_vertex_index + vertex_count;
    ZeroMemory(&command, sizeof(command));
    if (!multiply_u32(index_count, index_size, &command.index_bytes)
            || !multiply_u32(vertex_elements, stride, &command.vertex_bytes)) {
        TRACE_FRAME_REJECT();
        return D3DERR_INVALIDCALL;
    }
    if (!device_has_vertex_format(device)) {
        if (public_up) clear_bindings_after_up_missing_format(device, TRUE);
        TRACE_FRAME_REJECT();
        return D3DERR_INVALIDCALL;
    }
    command.device_handle = device->handle;
    command.primitive_type = primitive_type;
    command.min_vertex_index = min_vertex_index;
    command.vertex_count = vertex_count;
    command.primitive_count = primitive_count;
    command.index_format = index_format;
    command.stride = stride;
    command.index_count = index_count;
    D9WGClipDrawSource status_source;ZeroMemory(&status_source, sizeof(status_source));
    status_source.vertices = vertex_data;status_source.length = command.vertex_bytes;status_source.stride = stride;
    status_source.indices = index_data;status_source.index_bytes = command.index_bytes;status_source.index_stride = index_size;
    status_source.count = index_count;status_source.min_vertex = min_vertex_index;status_source.vertex_count = vertex_elements;
    status_source.status_screen = 1;
    status_source.viewport = (D9WGProcessViewport){(float)device->viewport.X,(float)device->viewport.Y,
            (float)device->viewport.Width,(float)device->viewport.Height,device->viewport.MinZ,device->viewport.MaxZ};
    D9WGClipStatus next_status = device->clip_status;BOOL next_status_valid = device->clip_status_valid;
    if (track_status) {
        HRESULT status_result = d9_prepare_clip_status(device, &status_source, FALSE, &next_status, &next_status_valid);
        if (FAILED(status_result)) return status_result;
    }
    result = emit_draw_indexed_primitive_up(&command, index_data, vertex_data);
    if (!result) return D3DERR_DRIVERINTERNALERROR;
    if (track_status) { device->clip_status = next_status;device->clip_status_valid = next_status_valid; }
    TRACE_FRAME_DRAW(primitive_count);
    invalidate_render_target_mirrors(device);
    return D3D_OK;
}

static HRESULT WINAPI device_draw_indexed_primitive_up(
        IDirect3DDevice9 *iface, D3DPRIMITIVETYPE primitive_type,
        UINT min_vertex_index, UINT vertex_count, UINT primitive_count,
        const void *index_data, D3DFORMAT index_format,
        const void *vertex_data, UINT stride)
{
    /* A nonzero stride makes a zero-primitive public UP a no-op before
     * format/data validation and before either successful/failure unbinding. */
    if (stride && !primitive_count) return D3D_OK;
    D9Device *device = device_from_iface(iface);
    HRESULT result = draw_indexed_up(device, primitive_type, min_vertex_index,
            vertex_count, primitive_count, index_data, index_format,
            vertex_data, stride, TRUE, TRUE);
    if (result == D3D_OK) {
        clear_stream_zero_after_up(device);
        clear_indices_after_indexed_up(device);
    }
    return result;
}

/* Windows exposes these two POSITION-only user declarations as legacy FVFs.
 * Keep all other reverse mappings at zero rather than guessing a layout. */
static DWORD declaration_fvf(const D3DVERTEXELEMENT9 *elements, UINT count)
{
    if (count != 1 || !elements) return 0;
    const D3DVERTEXELEMENT9 *e = elements;
    if (e->Stream || e->Offset || e->Method != D3DDECLMETHOD_DEFAULT
            || e->UsageIndex) return 0;
    if (e->Type == D3DDECLTYPE_FLOAT3 && e->Usage == D3DDECLUSAGE_POSITION)
        return D3DFVF_XYZ;
    if (e->Type == D3DDECLTYPE_FLOAT4 && e->Usage == D3DDECLUSAGE_POSITIONT)
        return D3DFVF_XYZRHW;
    return 0;
}

static HRESULT WINAPI device_create_vertex_declaration(
        IDirect3DDevice9 *iface, const D3DVERTEXELEMENT9 *elements,
        IDirect3DVertexDeclaration9 **decl_out)
{
    D9Device *device = device_from_iface(iface);
    D9VertexDeclaration *decl;
    D9WGVertexElement wire[D3DMAXDECLLENGTH];
    UINT count;

    if (!decl_out || !elements)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *decl_out = NULL;
    if (!parse_vertex_declaration(elements, wire, &count)) {
        /* The single most likely silent killer for a real game: one
         * unsupported D3DDECLTYPE/usage anywhere in the array rejects the
         * whole declaration, and without this the app just never draws. */
        HOSTLOG_FAILED("CreateVertexDeclaration refused (see the "
                "element-level line above for which element and why)");
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }

    decl = (D9VertexDeclaration *)HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY, sizeof(*decl));
    if (!decl)
        return TRACE_REFUSE(E_OUTOFMEMORY);
    decl->iface.lpVtbl = &g_decl_vtbl;
    decl->refcount = 1;
    decl->device = device;
    decl->handle = allocate_handle();
    decl->element_count = count;
    decl->fvf = declaration_fvf(elements, count);
    CopyMemory(decl->elements, elements, count * sizeof(D3DVERTEXELEMENT9));
    device_child_add_ref(device);

    if (!emit_vertex_declaration_create(device, decl->handle, wire, count)) {
        device_child_release(device);
        HeapFree(GetProcessHeap(), 0, decl);
        return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
    }
    decl->next_device_resource = device->vertex_declarations;
    device->vertex_declarations = decl;
    *decl_out = &decl->iface;
    return D3D_OK;
}

/* Converted declarations are cached by exact FVF for the device lifetime.
 * A cache entry starts publicly at zero and never adds a device parent pin. */
static HRESULT device_fvf_declaration(D9Device *device, DWORD fvf,
        const D9WGVertexElement *wire, UINT count, D9VertexDeclaration **out)
{
    D9VertexDeclaration *decl;
    *out = NULL;
    for (decl = device->vertex_declarations; decl; decl = decl->next_device_resource) {
        if (!decl->fvf_converted || decl->fvf != fvf) continue;
        if (!decl->fvf_cache_ref) {
            decl_binding_add_ref(decl);
            decl->fvf_cache_ref = TRUE;
        }
        *out = decl;
        return D3D_OK;
    }
    decl = (D9VertexDeclaration *)HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY, sizeof(*decl));
    if (!decl) return E_OUTOFMEMORY;
    decl->iface.lpVtbl = &g_decl_vtbl;
    decl->device = device;
    decl->handle = allocate_handle();
    decl->element_count = count;
    decl->fvf = fvf;
    decl->fvf_converted = TRUE;
    decl->fvf_cache_ref = TRUE;
    decl->binding_refs = 1;
    /* SDK and protocol elements share the WORD/WORD/BYTE/BYTE/BYTE/BYTE ABI. */
    CopyMemory(decl->elements, wire, count * sizeof(D3DVERTEXELEMENT9));
    if (!emit_vertex_declaration_create(device, decl->handle, wire, count)) {
        HeapFree(GetProcessHeap(), 0, decl);
        return D3DERR_DRIVERINTERNALERROR;
    }
    decl->next_device_resource = device->vertex_declarations;
    device->vertex_declarations = decl;
    *out = decl;
    return D3D_OK;
}

static HRESULT WINAPI device_set_vertex_declaration(IDirect3DDevice9 *iface,
        IDirect3DVertexDeclaration9 *decl_iface)
{
    D9Device *device = device_from_iface(iface);
    D9VertexDeclaration *decl = decl_iface ? decl_from_iface(decl_iface) : NULL;
    D9WGSetVertexDeclaration command;

    if (decl && (decl_iface->lpVtbl != &g_decl_vtbl || decl->device != device))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (device->recording_result) {
        D9StateBlock *block = device->recording_result;
        D9VertexDeclaration *old = block->vertex_declaration;
        if (decl) decl_binding_add_ref(decl);
        block->has_vertex_format = TRUE;block->vertex_declaration = decl;
        block->fvf = decl ? decl->fvf : 0;
        if (old) decl_binding_release(old);
        return D3D_OK;
    }
    D9VertexDeclaration *old_decl = device->vertex_declaration;
    if (decl) decl_binding_add_ref(decl);
    device->vertex_declaration = decl;
    device->fvf = decl ? decl->fvf : 0;
    if (old_decl) decl_binding_release(old_decl);
    command.device_handle = device->handle;
    command.declaration_handle = decl ? decl->handle : 0;
    return emit_command(D9WG_OP_SET_VERTEX_DECLARATION, &command,
            sizeof(command)) ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_get_vertex_declaration(IDirect3DDevice9 *iface,
        IDirect3DVertexDeclaration9 **decl_out)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetVertexDeclaration");
    D9Device *device = device_from_iface(iface);
    if (!decl_out)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *decl_out = device->vertex_declaration
            ? &device->vertex_declaration->iface : NULL;
    if (*decl_out)
        IDirect3DVertexDeclaration9_AddRef(*decl_out);
    return D3D_OK;
}

static HRESULT WINAPI device_set_fvf(IDirect3DDevice9 *iface, DWORD fvf)
{
    D9Device *device = device_from_iface(iface);
    D9WGVertexElement wire[D3DMAXDECLLENGTH];
    UINT count;
    D9VertexDeclaration *decl;
    HRESULT result;

    /* Native treats zero as a successful no-op, including while recording. */
    if (!fvf) return D3D_OK;
    if (!fvf_to_declaration(fvf, wire, &count)) {
        /* Worse than a plain refusal: nothing here clears the declaration
         * already in force, so the app's next draw is laid out by the
         * *previous* format. That renders -- with the wrong attributes at the
         * wrong offsets -- rather than failing, which is the shape "the model
         * is one flat colour" takes when a texcoord goes missing this way. */
        HOSTLOG_FAILED("SetFVF refused: fvf=%08lX (the declaration already in "
                "force stays bound, so following draws use a stale layout)",
                fvf);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    result = device_fvf_declaration(device, fvf, wire, count, &decl);
    if (FAILED(result)) return TRACE_REFUSE(result);
    if (device->recording_result) {
        D9StateBlock *block = device->recording_result;
        D9VertexDeclaration *old = block->vertex_declaration;
        decl_binding_add_ref(decl);
        block->has_vertex_format = TRUE;block->vertex_declaration = decl;block->fvf = fvf;
        if (old) decl_binding_release(old);
        return D3D_OK;
    }
    D9VertexDeclaration *old_decl = device->vertex_declaration;
    decl_binding_add_ref(decl);
    device->vertex_declaration = decl;
    device->fvf = fvf;
    if (old_decl) decl_binding_release(old_decl);
    return emit_set_fvf(device, fvf, wire, count)
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_get_fvf(IDirect3DDevice9 *iface, DWORD *fvf)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetFVF");
    if (!fvf)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *fvf = device_from_iface(iface)->fvf;
    return D3D_OK;
}

/* ---- programmable shaders (M2) ----
 *
 * D3D9 gives SetVertexShader/SetPixelShader a real COM pointer type, unlike
 * D3D8's overloaded DWORD, so there is no FVF-vs-shader-handle ambiguity to
 * resolve. Passing NULL means "go back to fixed function", which stays a
 * fully supported mode.
 *
 * The guest does no semantic work on the bytecode: it counts tokens, hashes
 * them, keeps a shadow copy for GetFunction and Reset replay, and ships the
 * raw stream. Translation to WGSL is entirely host-side (plan 4.2), so a
 * shader this build cannot translate still creates successfully here and is
 * refused where the failure is visible and countable -- in the executor --
 * rather than being second-guessed from inside the guest.
 */
static D9Shader *vertex_shader_from_iface(IDirect3DVertexShader9 *iface)
{
    return (D9Shader *)iface;
}

static D9Shader *pixel_shader_from_iface(IDirect3DPixelShader9 *iface)
{
    return (D9Shader *)iface;
}

/* All bytecode entering here has already been validated. This single
 * allocation/emission path also stores8 programs expanded with local DEFs;
 * their original8192-token limit is checked before the trusted expansion. */
static HRESULT create_shader_object(D9Device *device, const DWORD *bytecode,
        UINT token_count, BOOL want_pixel, D9Shader **shader_out)
{
    D9Shader *shader;
    uint32_t code_bytes;
    *shader_out = NULL;
    code_bytes = (uint32_t)token_count * 4u;
    shader = (D9Shader *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*shader));
    if (!shader)
        return TRACE_REFUSE(E_OUTOFMEMORY);
    shader->code = (DWORD *)HeapAlloc(GetProcessHeap(), 0, code_bytes);
    if (!shader->code) {
        HeapFree(GetProcessHeap(), 0, shader);
        return TRACE_REFUSE(E_OUTOFMEMORY);
    }
    CopyMemory(shader->code, bytecode, code_bytes);
    if (want_pixel)
        shader->iface.pixel.lpVtbl = &g_pixel_shader_vtbl;
    else
        shader->iface.vertex.lpVtbl = &g_vertex_shader_vtbl;
    shader->refcount = 1;
    shader->device = device;
    shader->handle = allocate_shader_handle();
    shader->is_pixel = want_pixel;
    shader->token_count = token_count;
    shader_bytecode_hash(shader->code, token_count, &shader->hash_low,
            &shader->hash_high);
    /* Before the emit, so a shader whose CREATE command cannot be reserved
     * (DMA ring full) still contributes to the corpus. */
    dump_shader_bytecode(shader->code, token_count, want_pixel,
            shader->hash_low, shader->hash_high);
    device_child_add_ref(device);

    if (!emit_shader_create(device, shader)) {
        device_child_release(device);
        HeapFree(GetProcessHeap(), 0, shader->code);
        HeapFree(GetProcessHeap(), 0, shader);
        return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
    }

    shader->next_device_resource = device->shaders;
    device->shaders = shader;
    *shader_out = shader;
    TRACE("OK CreateShader version=%08lX pixel=%lu tokens=%lu handle=%08lX "
            "hash=%08lX%08lX", shader->code[0], (DWORD)want_pixel, token_count,
            shader->handle, shader->hash_high, shader->hash_low);
    return D3D_OK;
}

static HRESULT create_shader(D9Device *device, const DWORD *bytecode,
        BOOL want_pixel, D9Shader **shader_out)
{
    UINT token_count = 0;
    BOOL is_pixel = FALSE;

    *shader_out = NULL;
    if (!bytecode) {
        TRACE("FAIL CreateShader want_pixel=%lu bytecode=NULL -> %08lX",
                (DWORD)want_pixel, (DWORD)D3DERR_INVALIDCALL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    BOOL valid;
    if (device_compatibility_api(device) == 8) {
        const DWORD version = bytecode[0];
        const UINT minor = version & 0xffu;
        UINT body_count = 0;
        valid = (version >> 16) == (want_pixel ? 0xffffu : 0xfffeu)
                && ((version >> 8) & 0xffu) == 1u
                && minor <= (want_pixel ? 4u : 1u)
                && shader8_validate_body(bytecode + 1, D9WG_D3D8_SHADER_TOKENS,
                        want_pixel, minor, &body_count);
        if (valid) {
            token_count = body_count + 2u; /* version and END */
            is_pixel = want_pixel;
        }
    } else {
        valid = shader_token_count(bytecode, &token_count, &is_pixel);
    }
    if (!valid || is_pixel != want_pixel) {
        /* Rejected before anything is emitted, so without this line the call
         * leaves no record whatsoever: no CMD, no OK, no FAIL. An app whose
         * shader is turned down here sees only D3DERR_INVALIDCALL, and the
         * trace shows only the gap where the shader should have been. */
        TRACE("FAIL CreateShader version=%08lX want_pixel=%lu is_pixel=%lu "
                "tokens=%lu -> %08lX", bytecode[0], (DWORD)want_pixel,
                (DWORD)is_pixel, token_count, (DWORD)D3DERR_INVALIDCALL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }

    return create_shader_object(device, bytecode, token_count,
            want_pixel, shader_out);
}

static void shader_destroy_storage(D9Shader *shader)
{
    D9Shader **link = &shader->device->shaders;
    D9WGDestroyResource destroy;

    while (*link && *link != shader)
        link = &(*link)->next_device_resource;
    if (*link)
        *link = shader->next_device_resource;
    destroy.resource_handle = shader->handle;
    destroy.resource_kind = shader->is_pixel ? D9WG_RESOURCE_PIXEL_SHADER
            : D9WG_RESOURCE_VERTEX_SHADER;
    emit_command(D9WG_OP_DESTROY_RESOURCE, &destroy, sizeof(destroy));
    HeapFree(GetProcessHeap(), 0, shader->code);
    HeapFree(GetProcessHeap(), 0, shader);
}

static ULONG shader_public_add_ref(D9Shader *shader)
{
    ULONG refs = (ULONG)InterlockedIncrement(&shader->refcount);
    if (refs == 1) device_child_add_ref(shader->device);
    return refs;
}

static ULONG shader_public_release(D9Shader *shader)
{
    if (!InterlockedCompareExchange(&shader->refcount, 0, 0)) return 0;
    ULONG refs = (ULONG)InterlockedDecrement(&shader->refcount);
    if (!refs) {
        D9Device *device = shader->device;
        if (!InterlockedCompareExchange(&shader->binding_refs, 0, 0))
            shader_destroy_storage(shader);
        device_child_release(device);
    }
    return refs;
}

static HRESULT emit_set_shader(D9Device *device, D9Shader *shader,
        BOOL is_pixel)
{
    D9WGSetShader command;
    command.device_handle = device->handle;
    command.shader_handle = shader ? shader->handle : 0;
    return emit_command(is_pixel ? D9WG_OP_SET_PIXEL_SHADER
                    : D9WG_OP_SET_VERTEX_SHADER,
            &command, sizeof(command)) ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_create_vertex_shader(IDirect3DDevice9 *iface,
        const DWORD *bytecode, IDirect3DVertexShader9 **shader_out)
{
    D9Shader *shader;
    HRESULT hr;

    if (!shader_out)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *shader_out = NULL;
    hr = create_shader(device_from_iface(iface), bytecode, FALSE, &shader);
    if (FAILED(hr))
        return hr;
    *shader_out = &shader->iface.vertex;
    return D3D_OK;
}

static HRESULT WINAPI device_create_pixel_shader(IDirect3DDevice9 *iface,
        const DWORD *bytecode, IDirect3DPixelShader9 **shader_out)
{
    D9Shader *shader;
    HRESULT hr;

    if (!shader_out)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *shader_out = NULL;
    hr = create_shader(device_from_iface(iface), bytecode, TRUE, &shader);
    if (FAILED(hr))
        return hr;
    *shader_out = &shader->iface.pixel;
    return D3D_OK;
}

static HRESULT WINAPI device_set_vertex_shader(IDirect3DDevice9 *iface,
        IDirect3DVertexShader9 *shader_iface)
{
    D9Device *device = device_from_iface(iface);
    D9Shader *shader = shader_iface ? vertex_shader_from_iface(shader_iface) : NULL;

    if (shader && (shader_iface->lpVtbl != &g_vertex_shader_vtbl
            || shader->device != device))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (device->recording_result) {
        D9StateBlock *block = device->recording_result;
        D9Shader *old = block->vertex_shader;
        if (shader) shader_binding_add_ref(shader);
        block->has_vertex_shader = TRUE;block->vertex_shader = shader;
        if (old) shader_binding_release(old);
        return D3D_OK;
    }
    D9Shader *old_shader = device->vertex_shader;
    if (shader) shader_binding_add_ref(shader);
    device->vertex_shader = shader;
    if (old_shader) shader_binding_release(old_shader);
    return emit_set_shader(device, shader, FALSE);
}

static HRESULT WINAPI device_get_vertex_shader(IDirect3DDevice9 *iface,
        IDirect3DVertexShader9 **shader_out)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetVertexShader");
    D9Device *device = device_from_iface(iface);
    if (!shader_out)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *shader_out = device->vertex_shader ? &device->vertex_shader->iface.vertex
            : NULL;
    if (*shader_out)
        IDirect3DVertexShader9_AddRef(*shader_out);
    return D3D_OK;
}

static HRESULT WINAPI device_set_pixel_shader(IDirect3DDevice9 *iface,
        IDirect3DPixelShader9 *shader_iface)
{
    D9Device *device = device_from_iface(iface);
    D9Shader *shader = shader_iface ? pixel_shader_from_iface(shader_iface) : NULL;

    if (shader && (shader_iface->lpVtbl != &g_pixel_shader_vtbl
            || shader->device != device))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (device->recording_result) {
        D9StateBlock *block = device->recording_result;
        D9Shader *old = block->pixel_shader;
        if (shader) shader_binding_add_ref(shader);
        block->has_pixel_shader = TRUE;block->pixel_shader = shader;
        if (old) shader_binding_release(old);
        return D3D_OK;
    }
    D9Shader *old_shader = device->pixel_shader;
    if (shader) shader_binding_add_ref(shader);
    device->pixel_shader = shader;
    if (old_shader) shader_binding_release(old_shader);
    return emit_set_shader(device, shader, TRUE);
}

static HRESULT WINAPI device_get_pixel_shader(IDirect3DDevice9 *iface,
        IDirect3DPixelShader9 **shader_out)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetPixelShader");
    D9Device *device = device_from_iface(iface);
    if (!shader_out)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *shader_out = device->pixel_shader ? &device->pixel_shader->iface.pixel
            : NULL;
    if (*shader_out)
        IDirect3DPixelShader9_AddRef(*shader_out);
    return D3D_OK;
}

/* ---- shader constant registers ----
 *
 * Two things happen here beyond storing the value. First, the shadow makes
 * the redundant-set suppression the plan asks for in 7.5 possible: engines
 * routinely re-upload an entire constant bank every frame even when nothing
 * in it changed. Second, when something *has* changed, only the dirty
 * sub-range is sent -- a 32-register bone palette where one bone moved
 * becomes one float4 on the wire, not 32.
 */
static BOOL constant_range_valid(UINT start, UINT count, UINT capacity)
{
    return count != 0 && start < capacity && count <= capacity - start;
}

static HRESULT set_constant_f(D9Device *device, BOOL is_pixel, UINT start,
        const float *data, UINT count)
{
    float (*shadow)[4] = is_pixel ? device->ps_const_f : device->vs_const_f;
    UINT capacity = is_pixel ? D9_MAX_PS_CONST_F : D9_MAX_VS_CONST_F;
    UINT first_dirty = count;
    UINT last_dirty = 0;
    UINT index;

    if (!data || !constant_range_valid(start, count, capacity))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (state_block_record_constant(device, is_pixel, TRUE, FALSE, start, count, data)) return D3D_OK;
    for (index = 0; index < count; ++index) {
        const float *source = data + index * 4;
        float *target = shadow[start + index];
        if (target[0] == source[0] && target[1] == source[1]
                && target[2] == source[2] && target[3] == source[3])
            continue;
        target[0] = source[0]; target[1] = source[1];
        target[2] = source[2]; target[3] = source[3];
        if (index < first_dirty) first_dirty = index;
        last_dirty = index;
    }
    if (first_dirty > last_dirty)
        return D3D_OK; /* nothing actually changed */
    return emit_shader_constants(device,
            is_pixel ? D9WG_OP_SET_PIXEL_SHADER_CONSTANT_F
                    : D9WG_OP_SET_VERTEX_SHADER_CONSTANT_F,
            start + first_dirty, last_dirty - first_dirty + 1,
            shadow[start + first_dirty],
            (last_dirty - first_dirty + 1) * 16u)
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT set_constant_i(D9Device *device, BOOL is_pixel, UINT start,
        const int *data, UINT count)
{
    int (*shadow)[4] = is_pixel ? device->ps_const_i : device->vs_const_i;
    UINT first_dirty = count;
    UINT last_dirty = 0;
    UINT index;

    if (!data || !constant_range_valid(start, count, D9_MAX_CONST_I))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (state_block_record_constant(device, is_pixel, FALSE, FALSE, start, count, data)) return D3D_OK;
    for (index = 0; index < count; ++index) {
        const int *source = data + index * 4;
        int *target = shadow[start + index];
        if (target[0] == source[0] && target[1] == source[1]
                && target[2] == source[2] && target[3] == source[3])
            continue;
        target[0] = source[0]; target[1] = source[1];
        target[2] = source[2]; target[3] = source[3];
        if (index < first_dirty) first_dirty = index;
        last_dirty = index;
    }
    if (first_dirty > last_dirty)
        return D3D_OK;
    return emit_shader_constants(device,
            is_pixel ? D9WG_OP_SET_PIXEL_SHADER_CONSTANT_I
                    : D9WG_OP_SET_VERTEX_SHADER_CONSTANT_I,
            start + first_dirty, last_dirty - first_dirty + 1,
            shadow[start + first_dirty],
            (last_dirty - first_dirty + 1) * 16u)
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT set_constant_b(D9Device *device, BOOL is_pixel, UINT start,
        const WINBOOL *data, UINT count)
{
    BOOL *shadow = is_pixel ? device->ps_const_b : device->vs_const_b;
    uint32_t packed[D9_MAX_CONST_B];
    UINT first_dirty = count;
    UINT last_dirty = 0;
    UINT index;

    if (!data || !constant_range_valid(start, count, D9_MAX_CONST_B))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (state_block_record_constant(device, is_pixel, FALSE, TRUE, start, count, data)) return D3D_OK;
    for (index = 0; index < count; ++index) {
        BOOL value = data[index] ? TRUE : FALSE;
        if (shadow[start + index] == value)
            continue;
        shadow[start + index] = value;
        if (index < first_dirty) first_dirty = index;
        last_dirty = index;
    }
    if (first_dirty > last_dirty)
        return D3D_OK;
    /* One 32-bit slot per register on the wire: the host's uniform layout
     * (plan 9.7) has no packed-bit representation, and this is a handful of
     * bytes either way. */
    for (index = first_dirty; index <= last_dirty; ++index)
        packed[index - first_dirty] = shadow[start + index] ? 1u : 0u;
    return emit_shader_constants(device,
            is_pixel ? D9WG_OP_SET_PIXEL_SHADER_CONSTANT_B
                    : D9WG_OP_SET_VERTEX_SHADER_CONSTANT_B,
            start + first_dirty, last_dirty - first_dirty + 1, packed,
            (last_dirty - first_dirty + 1) * 4u)
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_set_vertex_shader_constant_f(
        IDirect3DDevice9 *iface, UINT start, const float *data, UINT count)
{ return set_constant_f(device_from_iface(iface), FALSE, start, data, count); }

static HRESULT WINAPI device_set_pixel_shader_constant_f(
        IDirect3DDevice9 *iface, UINT start, const float *data, UINT count)
{ return set_constant_f(device_from_iface(iface), TRUE, start, data, count); }

static HRESULT WINAPI device_set_vertex_shader_constant_i(
        IDirect3DDevice9 *iface, UINT start, const int *data, UINT count)
{ return set_constant_i(device_from_iface(iface), FALSE, start, data, count); }

static HRESULT WINAPI device_set_pixel_shader_constant_i(
        IDirect3DDevice9 *iface, UINT start, const int *data, UINT count)
{ return set_constant_i(device_from_iface(iface), TRUE, start, data, count); }

static HRESULT WINAPI device_set_vertex_shader_constant_b(
        IDirect3DDevice9 *iface, UINT start, const WINBOOL *data, UINT count)
{ return set_constant_b(device_from_iface(iface), FALSE, start, data, count); }

static HRESULT WINAPI device_set_pixel_shader_constant_b(
        IDirect3DDevice9 *iface, UINT start, const WINBOOL *data, UINT count)
{ return set_constant_b(device_from_iface(iface), TRUE, start, data, count); }

static HRESULT WINAPI device_get_vertex_shader_constant_f(
        IDirect3DDevice9 *iface, UINT start, float *data, UINT count)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetVertexShaderConstantF");
    D9Device *device = device_from_iface(iface);
    if (!data || !constant_range_valid(start, count, D9_MAX_VS_CONST_F))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    CopyMemory(data, device->vs_const_f[start], count * 16u);
    return D3D_OK;
}

static HRESULT WINAPI device_get_pixel_shader_constant_f(
        IDirect3DDevice9 *iface, UINT start, float *data, UINT count)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetPixelShaderConstantF");
    D9Device *device = device_from_iface(iface);
    if (!data || !constant_range_valid(start, count, D9_MAX_PS_CONST_F))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    CopyMemory(data, device->ps_const_f[start], count * 16u);
    return D3D_OK;
}

static HRESULT WINAPI device_get_vertex_shader_constant_i(
        IDirect3DDevice9 *iface, UINT start, int *data, UINT count)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetVertexShaderConstantI");
    D9Device *device = device_from_iface(iface);
    if (!data || !constant_range_valid(start, count, D9_MAX_CONST_I))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    CopyMemory(data, device->vs_const_i[start], count * 16u);
    return D3D_OK;
}

static HRESULT WINAPI device_get_pixel_shader_constant_i(
        IDirect3DDevice9 *iface, UINT start, int *data, UINT count)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetPixelShaderConstantI");
    D9Device *device = device_from_iface(iface);
    if (!data || !constant_range_valid(start, count, D9_MAX_CONST_I))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    CopyMemory(data, device->ps_const_i[start], count * 16u);
    return D3D_OK;
}

static HRESULT WINAPI device_get_vertex_shader_constant_b(
        IDirect3DDevice9 *iface, UINT start, WINBOOL *data, UINT count)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetVertexShaderConstantB");
    D9Device *device = device_from_iface(iface);
    UINT index;
    if (!data || !constant_range_valid(start, count, D9_MAX_CONST_B))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    for (index = 0; index < count; ++index)
        data[index] = device->vs_const_b[start + index];
    return D3D_OK;
}

static HRESULT WINAPI device_get_pixel_shader_constant_b(
        IDirect3DDevice9 *iface, UINT start, WINBOOL *data, UINT count)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetPixelShaderConstantB");
    D9Device *device = device_from_iface(iface);
    UINT index;
    if (!data || !constant_range_valid(start, count, D9_MAX_CONST_B))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    for (index = 0; index < count; ++index)
        data[index] = device->ps_const_b[start + index];
    return D3D_OK;
}

/* ---- Remaining legacy entry points not implemented by this profile.
 * Typed per-method stubs (rather than one variadic stub) keep stdcall stack
 * cleanup correct on 32-bit XP. Returning D3DERR_INVALIDCALL rather than
 * pretending to succeed matches the D3D8 path's established discipline. */
#define DEV_STUB0(name) \
    static HRESULT WINAPI device_##name(IDirect3DDevice9 *iface) \
    { (void)iface; return TRACE_REFUSE(D3DERR_INVALIDCALL); }
#define DEV_STUB(name, ...) \
    static HRESULT WINAPI device_##name(IDirect3DDevice9 *iface, __VA_ARGS__)

/* The refusals below all report themselves through UNSUPPORTED(), defined next
 * to HOSTLOG_REFUSED() above -- see the rationale there. */

/*
 * IDirect3DDevice9::CreateAdditionalSwapChain.
 *
 * The chain's back buffer is an ordinary render-target texture rather than a
 * second "the canvas" special case, which is what keeps the rest of the stack
 * unchanged: SetRenderTarget, StretchRect, GetRenderTargetData and the host's
 * render-pass builder already treat a texture as a target. The chain becomes
 * special only in the step that moves the finished image onto its own canvas.
 *
 * D3D9 permits additional chains on a windowed device only -- a fullscreen
 * device owns the display -- and each needs its own window.
 */
static HRESULT WINAPI device_create_additional_swap_chain(
        IDirect3DDevice9 *iface, D3DPRESENT_PARAMETERS *params,
        IDirect3DSwapChain9 **out)
{
    D9Device *device = device_from_iface(iface);
    const UINT api = device_compatibility_api(device);
    D9SwapChain *chain;
    D9WGCreateSwapChain command;
    IDirect3DSurface9 *back_buffer = NULL;
    D9Surface *surface;
    HWND window;
    UINT width;
    UINT height;
    D3DFORMAT format;
    RECT client;
    POINT origin;
    HRESULT hr;

    TRACE_MARK_ENTER("Device.CreateAdditionalSwapChain");
    if (out) *out = NULL;
    if (!params || !out) {
        TRACE_MARK_EXIT("Device.CreateAdditionalSwapChain",
                D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    if (api == 8 && (!params->Windowed
            || (params->MultiSampleType != D3DMULTISAMPLE_NONE
                && params->MultiSampleType != D3DMULTISAMPLE_4_SAMPLES)
            || params->BackBufferCount > 3
            || params->BackBufferWidth > 8192 || params->BackBufferHeight > 8192))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!d9wg_present_interval_valid(api, params->Windowed,
            params->PresentationInterval, D9WG_PRESENT_INTERVAL_MASK))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!d9wg_swap_effect_valid(api, params->SwapEffect, params->BackBufferCount))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!d9wg_present_multisample_valid(api, params->SwapEffect,
            params->MultiSampleType, params->MultiSampleQuality, params->Flags))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (api == 8 && params->BackBufferFormat != D3DFMT_UNKNOWN
            && !supported_render_target_format8(params->BackBufferFormat))
        return TRACE_REFUSE(D3DERR_NOTAVAILABLE);
    if (params->BackBufferCount > 3)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!params->Windowed || (api == 9 && !device->present.Windowed)) {
        HOSTLOG_REFUSED("CreateAdditionalSwapChain refused: D3D9 allows "
                "additional chains only on a windowed device, and only for a "
                "windowed chain");
        TRACE_MARK_EXIT("Device.CreateAdditionalSwapChain",
                D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }

    window = params->hDeviceWindow ? params->hDeviceWindow
            : device->creation.hFocusWindow;
    if (api == 9 && !window) {
        TRACE_MARK_EXIT("Device.CreateAdditionalSwapChain",
                D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    if (api == 8) {
        SetRect(&client, 0, 0, (int)device->display_mode.Width,
                (int)device->display_mode.Height);
        origin.x = origin.y = 0;
        if (window) GetClientRect(window, &client);
        width = d9wg_present_extent(params->BackBufferWidth,
                (UINT)(client.right - client.left), device->display_mode.Width);
        height = d9wg_present_extent(params->BackBufferHeight,
                (UINT)(client.bottom - client.top), device->display_mode.Height);
        if (width > 8192 || height > 8192)
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
    } else {
        /* D3D9 fills zero extents from the window probe. */
        probe_window_rect(window, (int)device->display_mode.Width,
                (int)device->display_mode.Height, &client, &origin);
        width = params->BackBufferWidth ? params->BackBufferWidth
                : (UINT)(client.right - client.left);
        height = params->BackBufferHeight ? params->BackBufferHeight
                : (UINT)(client.bottom - client.top);
        if (!width || !height) {
            TRACE_MARK_EXIT("Device.CreateAdditionalSwapChain", D3DERR_INVALIDCALL, NULL);
            return D3DERR_INVALIDCALL;
        }
    }
    format = api == 8
        ? (params->BackBufferFormat ? params->BackBufferFormat : device->display_mode.Format)
        : (D3DFORMAT)d9wg_display_mode_backbuffer_format(
        params->BackBufferFormat);

    if (!supported_multisample(format, params->MultiSampleType))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    hr = create_target_texture(device, width, height, format,
            D3DUSAGE_RENDERTARGET, params->MultiSampleType,
            params->MultiSampleQuality, &back_buffer);
    if (FAILED(hr)) {
        HOSTLOG_FAILED("CreateAdditionalSwapChain refused: its back buffer "
                "(%lux%lu format=%08lX) could not be created", width, height,
                (DWORD)format);
        TRACE_MARK_EXIT("Device.CreateAdditionalSwapChain", hr, NULL);
        return hr;
    }
    surface = surface_from_iface(back_buffer);
    surface->lockable_render_target = !!(params->Flags & D3DPRESENTFLAG_LOCKABLE_BACKBUFFER);

    chain = (D9SwapChain *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*chain));
    if (!chain) {
        IDirect3DSurface9_Release(back_buffer);
        TRACE_MARK_EXIT("Device.CreateAdditionalSwapChain", E_OUTOFMEMORY,
                NULL);
        return E_OUTOFMEMORY;
    }
    chain->iface.lpVtbl = &g_swap_chain_vtbl;
    chain->refcount = 1;
    chain->device = device;
    chain->handle = allocate_handle();
    chain->window = window;
    chain->back_buffer = back_buffer;
    d9wg_present_parameters_effective(&chain->present, params,
        width, height, format, window);

    if (api == 8 && window) ClientToScreen(window, &origin);
    command.device_handle = device->handle;
    command.swap_chain_handle = chain->handle;
    command.back_buffer_handle = surface->texture ? surface->texture->handle : 0;
    command.hwnd = (uint32_t)(uintptr_t)window;
    command.x = origin.x;
    command.y = origin.y;
    command.width = width;
    command.height = height;
    if (!command.back_buffer_handle
            || !emit_command(D9WG_OP_CREATE_SWAP_CHAIN, &command,
                    sizeof(command))) {
        IDirect3DSurface9_Release(back_buffer);
        HeapFree(GetProcessHeap(), 0, chain);
        TRACE_MARK_EXIT("Device.CreateAdditionalSwapChain",
                D3DERR_DRIVERINTERNALERROR, NULL);
        return D3DERR_DRIVERINTERNALERROR;
    }

    chain->next_device_chain = device->additional_swap_chains;
    device->additional_swap_chains = chain;
    /* Transfer the standalone surface's one device pin to the public chain.
     * Its private texture storage has no separate device pin. */
    surface->swap_chain = chain;
    surface->refcount = 0;
    d9wg_present_parameters_return(api, params, &chain->present);
    *out = &chain->iface;
    TRACE("OK CreateAdditionalSwapChain device=%08lX chain=%08lX back=%08lX "
            "%lux%lu window=%08lX", device->handle, chain->handle,
            command.back_buffer_handle, width, height,
            (DWORD)(uintptr_t)window);
    TRACE_MARK_EXIT("Device.CreateAdditionalSwapChain", D3D_OK, *out);
    return D3D_OK;
}

static HRESULT WINAPI device_get_swap_chain(IDirect3DDevice9 *iface,
        UINT index, IDirect3DSwapChain9 **out)
{
    D9Device *device = device_from_iface(iface);

    TRACE_MARK_ENTER("Device.GetSwapChain");
    TRACE("CALL GetSwapChain device=%08lX index=%lu outarg=%08lX",
            device->handle, index, (DWORD)(uintptr_t)out);
    if (!out) {
        TRACE("FAIL GetSwapChain index=%lu missing output -> %08lX", index,
                (DWORD)D3DERR_INVALIDCALL);
        TRACE_MARK_EXIT("Device.GetSwapChain", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    *out = NULL;
    if (index != 0) {
        TRACE("FAIL GetSwapChain index=%lu out-of-range -> %08lX", index,
                (DWORD)D3DERR_INVALIDCALL);
        TRACE_MARK_EXIT("Device.GetSwapChain", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }

    *out = &device->implicit_swap_chain.iface;
    IDirect3DSwapChain9_AddRef(*out);
    TRACE("OK GetSwapChain index=0 object=%08lX refs=%ld",
            (DWORD)(uintptr_t)*out,
            InterlockedCompareExchange(&device->implicit_swap_chain.refcount,
                    0, 0));
    TRACE_MARK_EXIT("Device.GetSwapChain", D3D_OK, *out);
    return D3D_OK;
}
/* War3 testing confirmed that engines can gate an entire render branch on
 * this succeeding even when they never read pixels back from the result.
 * StretchRect and GetRenderTargetData both understand the implicit back
 * buffer. Explicit LOCKABLE_BACKBUFFER permits linear non-MSAA locks; other
 * default-pool backbuffers keep returning INVALIDCALL. */
static HRESULT WINAPI device_get_back_buffer(IDirect3DDevice9 *iface,
        UINT swapchain, UINT index, D3DBACKBUFFER_TYPE type,
        IDirect3DSurface9 **out)
{
    D9Device *device = device_from_iface(iface);
    D9Surface *surface;
    TRACE_MARK_ENTER("Device.GetBackBuffer");
    TRACE("CALL GetBackBuffer device=%08lX chain=%lu index=%lu type=%lu outarg=%08lX",
            device->handle, swapchain, index, (DWORD)type,
            (DWORD)(uintptr_t)out);
    if (!out) {
        TRACE_MARK_EXIT("Device.GetBackBuffer", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    *out = NULL;
    (void)type;
    if (swapchain || index) {
        TRACE("FAIL GetBackBuffer chain=%lu index=%lu type=%lu -> %08lX",
                swapchain, index, (DWORD)type, (DWORD)D3DERR_INVALIDCALL);
        TRACE_MARK_EXIT("Device.GetBackBuffer", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    /* One object per device, handed out again on every call: an app that
     * caches the pointer -- RenderWare caches both this and the auto
     * depth-stencil -- must see the same surface it saw the first time. */
    surface = device->implicit_back_buffer;
    if (!surface) {
        surface = (D9Surface *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                sizeof(*surface));
        if (!surface) {
            TRACE("FAIL GetBackBuffer allocation -> %08lX",
                    (DWORD)E_OUTOFMEMORY);
            TRACE_MARK_EXIT("Device.GetBackBuffer", E_OUTOFMEMORY, NULL);
            return E_OUTOFMEMORY;
        }
        surface->iface.lpVtbl = &g_surface_vtbl;
        surface->device = device;
        surface->swap_chain = &device->implicit_swap_chain;
        surface->width = device->display_mode.Width;
        surface->height = device->display_mode.Height;
        surface->format = device->display_mode.Format;
        surface->multisample = device->present.MultiSampleType;
        surface->multisample_quality = device->present.MultiSampleQuality;
        device->implicit_back_buffer = surface;
        TRACE_REGISTER_RANGE("SURFACE_BACKBUFFER", 0, surface, surface,
                sizeof(*surface));
        TRACE("SURFACE CREATE kind=backbuffer object=%08lX vtbl=%08lX ref=%ld "
                "device=%08lX texture=00000000 swapchain=%08lX shadow=00000000 "
                "level=0 size=%lux%lu format=%08lX",
                (DWORD)(uintptr_t)&surface->iface,
                (DWORD)(uintptr_t)surface->iface.lpVtbl, surface->refcount,
                (DWORD)(uintptr_t)&device->iface,
                (DWORD)(uintptr_t)&device->implicit_swap_chain.iface,
                surface->width, surface->height, (DWORD)surface->format);
    }
    *out = &surface->iface;
    /* The first live surface reference pins the device. */
    IDirect3DSurface9_AddRef(*out);
    TRACE("OK GetBackBuffer object=%08lX %lux%lu fmt=%08lX refs=%ld",
            (DWORD)(uintptr_t)*out, surface->width, surface->height,
            (DWORD)surface->format,
            InterlockedCompareExchange(&surface->refcount, 0, 0));
    TRACE_MARK_EXIT("Device.GetBackBuffer", D3D_OK, *out);
    return D3D_OK;
}
static HRESULT WINAPI device_get_raster_status(IDirect3DDevice9 *iface,
        UINT swapchain, D3DRASTER_STATUS *status)
{
    D9Device *device = device_from_iface(iface);
    DWORD now, period, visible;
    if (swapchain || !status)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    /* D3D9 exposes this as advisory scan-out timing. Reconstruct a stable
     * 60-Hz raster from the guest monotonic clock so polling loops progress
     * and observe a real vblank interval instead of a permanent constant:
     * the last 1/17 of each frame is the blank.  present_clock(), because
     * GetTickCount's 10 ms steps land in a 1 ms blank once every 170 ms. */
    now = present_clock(device);
    period = device->present_clock.hz / 60u;
    visible = period * 16u / 17u;
    if (!visible) {
        status->InVBlank = FALSE;
        status->ScanLine = 0;
        return D3D_OK;
    }
    now %= period;
    status->InVBlank = now >= visible;
    status->ScanLine = status->InVBlank || !device->display_mode.Height
            ? 0u : now * device->display_mode.Height / visible;
    return D3D_OK;
}
static D9VolumeTexture *volume_texture_from_iface(
        IDirect3DVolumeTexture9 *iface)
{ return (D9VolumeTexture *)iface; }

static D9Volume *volume_from_iface(IDirect3DVolume9 *iface)
{ return (D9Volume *)iface; }

static UINT full_volume_mip_level_count(UINT width, UINT height, UINT depth)
{
    UINT levels = 1;
    while (width > 1 || height > 1 || depth > 1) {
        if (width > 1) width >>= 1;
        if (height > 1) height >>= 1;
        if (depth > 1) depth >>= 1;
        ++levels;
    }
    return levels;
}

static BOOL supported_volume_texture_format(D3DFORMAT format)
{
    return supported_texture_format(format)
            && format != D3DFMT_DXT1 && format != D3DFMT_DXT2
            && format != D3DFMT_DXT3 && format != D3DFMT_DXT4
            && format != D3DFMT_DXT5;
}

static BOOL emit_volume_texture_create(D9Device *device,
        D9VolumeTexture *texture)
{
    D9WGCreateTextureVolume command;
    ZeroMemory(&command, sizeof(command));
    command.device_handle = device->handle;
    command.resource_handle = texture->handle;
    command.width = texture->width;
    command.height = texture->height;
    command.depth = texture->depth;
    command.level_count = texture->level_count;
    command.format = texture->format;
    command.usage = texture->usage;
    command.pool = texture->pool;
    return emit_command(D9WG_OP_CREATE_TEXTURE_VOLUME, &command,
            sizeof(command));
}

static BOOL emit_volume_texture_update(D9VolumeTexture *texture, UINT level,
        const D3DBOX *box)
{
    D9TextureLevel *level_data = &texture->levels[level];
    D9TextureLevel slice = *level_data;
    RECT rect;
    UINT z;
    BOOL result = TRUE;
    SetRect(&rect, (INT)box->Left, (INT)box->Top, (INT)box->Right, (INT)box->Bottom);
    EnterCriticalSection(&g_transport_lock);
    for (z = box->Front; z < box->Back && result; ++z) {
        slice.shadow = level_data->shadow + z * level_data->slice_pitch;
        if (device_compatibility_api(texture->device) == 8u)
            result = emit_level_update_api(8u, texture->handle, texture->format,
                    &slice, level, z, &rect);
        else
            result = emit_level_update(texture->handle, texture->format,
                    &slice, level, z, &rect);
    }
    LeaveCriticalSection(&g_transport_lock);
    return result;
}

static BOOL supported_volume_texture_format8(D3DFORMAT format)
{
    return supported_format8(format, FALSE)
            && format != D3DFMT_DXT1 && format != D3DFMT_DXT2
            && format != D3DFMT_DXT3 && format != D3DFMT_DXT4
            && format != D3DFMT_DXT5;
}

static HRESULT create_volume_texture_object(D9Device *device,
        UINT width, UINT height, UINT depth, UINT levels, DWORD usage,
        D3DFORMAT format, D3DPOOL pool, UINT api, IDirect3DVolumeTexture9 **out)
{
    D9VolumeTexture *texture;
    UINT full_levels, level, level_width, level_height, level_depth;
    HRESULT failure = E_OUTOFMEMORY;
    HRESULT success = D3D_OK;
    TRACE("CALL CreateVolumeTexture %lux%lux%lu levels=%lu usage=%08lX "
            "format=%08lX pool=%lu", width, height, depth, levels, usage,
            (DWORD)format, (DWORD)pool);
    if (!out)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *out = NULL;
    if (api == 8u) {
        if (!width || !height || !depth || width > 2048 || height > 2048
                || depth > 2048 || !supported_volume_texture_format8(format)
                || (usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL))
                || pool > D3DPOOL_SCRATCH)
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
    } else {
        /* Unlike the 2D/cube NOAUTOGEN fallback, volume autogen is invalid. */
        if (usage & D3DUSAGE_AUTOGENMIPMAP)
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        if (!width || !height || !depth || width > 2048 || height > 2048
                || depth > 2048
                || !texture_create_supported(D3DRTYPE_VOLUMETEXTURE, usage, format,
                        pool)) {
            HOSTLOG_FAILED("CreateVolumeTexture refused: %lux%lux%lu levels=%lu "
                    "usage=%08lX format=%08lX pool=%lu", width, height, depth,
                    levels, usage, (DWORD)format, (DWORD)pool);
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        }
    }
    full_levels = full_volume_mip_level_count(width, height, depth);
    if (!levels) levels = full_levels;
    if (levels > full_levels)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    texture = (D9VolumeTexture *)HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY, sizeof(*texture));
    if (!texture)
        return TRACE_REFUSE(E_OUTOFMEMORY);
    texture->levels = (D9TextureLevel *)HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY, levels * sizeof(*texture->levels));
    if (!texture->levels) {
        HeapFree(GetProcessHeap(), 0, texture);
        return TRACE_REFUSE(E_OUTOFMEMORY);
    }
    texture->iface.lpVtbl = &g_volume_texture_vtbl;
    texture->refcount = 1;
    texture->device = device;
    texture->handle = allocate_handle();
    texture->width = width;
    texture->height = height;
    texture->depth = depth;
    texture->level_count = levels;
    texture->usage = usage;
    texture->format = format;
    texture->pool = pool;
    device_child_add_ref(device);
    level_width = width;
    level_height = height;
    level_depth = depth;
    for (level = 0; level < levels; ++level) {
        D9TextureLevel *level_data = &texture->levels[level];
        UINT slice_bytes;
        level_data->width = level_width;
        level_data->height = level_height;
        level_data->depth = level_depth;
        if (!d9wg_texture_level_layout(api, format, level_width, level_height,
                &level_data->row_pitch, &level_data->row_count,
                &slice_bytes)
                || !multiply_u32(slice_bytes, level_depth,
                    &level_data->byte_count))
            goto allocation_failed;
        level_data->slice_pitch = slice_bytes;
        level_data->shadow = (BYTE *)HeapAlloc(GetProcessHeap(),
                HEAP_ZERO_MEMORY, level_data->byte_count);
        if (api == 8u && !level_data->shadow)
            goto allocation_failed;
        /* API8 keeps its legacy lazy level-view allocation. Ordinary9 keeps
         * eager views and its existing allocation-failure ordering. */
        if (api == 9u) {
            level_data->level_volume = (D9Volume *)HeapAlloc(GetProcessHeap(),
                    HEAP_ZERO_MEMORY, sizeof(*level_data->level_volume));
            if (!level_data->shadow || !level_data->level_volume)
                goto allocation_failed;
            level_data->level_volume->iface.lpVtbl = &g_volume_vtbl;
            level_data->level_volume->texture = texture;
            level_data->level_volume->level = level;
        }
        if (level_width > 1) level_width >>= 1;
        if (level_height > 1) level_height >>= 1;
        if (level_depth > 1) level_depth >>= 1;
    }
    if (!emit_volume_texture_create(device, texture)) {
        failure = D3DERR_DRIVERINTERNALERROR;
        goto allocation_failed;
    }
    texture->next_device_resource = device->volume_textures;
    device->volume_textures = texture;
    *out = &texture->iface;
    return success;

allocation_failed:
    for (level = 0; level < levels; ++level) {
        HeapFree(GetProcessHeap(), 0, texture->levels[level].level_volume);
        HeapFree(GetProcessHeap(), 0, texture->levels[level].shadow);
    }
    device_child_release(device);
    HeapFree(GetProcessHeap(), 0, texture->levels);
    HeapFree(GetProcessHeap(), 0, texture);
    return TRACE_REFUSE(failure);
}

static HRESULT WINAPI device_create_volume_texture(IDirect3DDevice9 *iface,
        UINT width, UINT height, UINT depth, UINT levels, DWORD usage,
        D3DFORMAT format, D3DPOOL pool, IDirect3DVolumeTexture9 **out,
        HANDLE *shared)
{
    (void)shared;
    return create_volume_texture_object(device_from_iface(iface), width,
            height, depth, levels, usage, format, pool, 9u, out);
}

static HRESULT WINAPI volume_texture_query_interface(
        IDirect3DVolumeTexture9 *iface, REFIID iid, void **object)
{
    if (!object) return E_POINTER;
    *object = NULL;
    if (!iid || (!iid_is_unknown(iid)
            && !guid_equal(iid, &IID_IDirect3DResource9)
            && !guid_equal(iid, &IID_IDirect3DBaseTexture9)
            && !guid_equal(iid, &IID_IDirect3DVolumeTexture9)))
        return E_NOINTERFACE;
    *object = iface;
    IDirect3DVolumeTexture9_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI volume_texture_add_ref(IDirect3DVolumeTexture9 *iface)
{
    D9VolumeTexture *texture = volume_texture_from_iface(iface);
    ULONG refs = (ULONG)InterlockedIncrement(&texture->refcount);
    if (refs == 1)
        device_child_add_ref(texture->device);
    return refs;
}

static void volume_texture_destroy_storage(D9VolumeTexture *texture)
{
    D9VolumeTexture** link = &texture->device->volume_textures;
    D9WGDestroyResource destroy;
    UINT level;
    while (*link && *link != texture)
        link = &(*link)->next_device_resource;
    if (*link)
        *link = texture->next_device_resource;
    d9wg_private_clear(&texture->private_data, &d9wg_private_ops);
    destroy.resource_handle = texture->handle;
    destroy.resource_kind = D9WG_RESOURCE_TEXTURE_VOLUME;
    emit_command(D9WG_OP_DESTROY_RESOURCE, &destroy, sizeof(destroy));
    for (level = 0; level < texture->level_count; ++level) {
        d9wg_private_clear(&texture->levels[level].private_data, &d9wg_private_ops);
        core_view_retire8(texture->levels[level].level_volume);
        HeapFree(GetProcessHeap(), 0, texture->levels[level].level_volume);
        HeapFree(GetProcessHeap(), 0, texture->levels[level].shadow);
    }
    HeapFree(GetProcessHeap(), 0, texture->levels);
    core_view_retire8(texture);
    HeapFree(GetProcessHeap(), 0, texture);
}

static ULONG WINAPI volume_texture_release(IDirect3DVolumeTexture9* iface)
{
    D9VolumeTexture *texture = volume_texture_from_iface(iface);
    if (!InterlockedCompareExchange(&texture->refcount, 0, 0))
        return 0;
    ULONG refs = (ULONG)InterlockedDecrement(&texture->refcount);
    if (!refs) {
        D9Device *device = texture->device;
        if (!InterlockedCompareExchange(&texture->binding_refs, 0, 0))
            volume_texture_destroy_storage(texture);
        device_child_release(device);
    }
    return refs;
}

static HRESULT WINAPI volume_texture_get_device(IDirect3DVolumeTexture9 *iface,
        IDirect3DDevice9 **out)
{
    if (!out) return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *out = &volume_texture_from_iface(iface)->device->iface;
    IDirect3DDevice9_AddRef(*out);
    return D3D_OK;
}

static HRESULT WINAPI volume_texture_set_private_data(
    IDirect3DVolumeTexture9* iface, REFGUID guid, const void* data,
    DWORD size, DWORD flags)
{
    D9WGPrivateStore* store = &volume_texture_from_iface(iface)->private_data;
    return TRACE_REFUSE(d9wg_private_hresult(d9wg_private_set(store, guid, data, size, flags, &d9wg_private_ops)));
}
static HRESULT WINAPI volume_texture_get_private_data(
    IDirect3DVolumeTexture9* iface, REFGUID guid, void* data, DWORD* size)
{
    D9WGPrivateStore* store = &volume_texture_from_iface(iface)->private_data;
    return TRACE_REFUSE(d9wg_get_private_data(store, guid, data, size));
}
static HRESULT WINAPI volume_texture_free_private_data(IDirect3DVolumeTexture9* iface, REFGUID guid)
{
    D9WGPrivateStore* store = &volume_texture_from_iface(iface)->private_data;
    return TRACE_REFUSE(d9wg_private_hresult(d9wg_private_free(store, guid, &d9wg_private_ops)));
}

static DWORD WINAPI volume_texture_set_priority(
        IDirect3DVolumeTexture9 *iface, DWORD priority)
{ D9VolumeTexture *texture = volume_texture_from_iface(iface);
  DWORD previous = texture->priority; texture->priority = priority;
  return previous; }
static DWORD WINAPI volume_texture_get_priority(IDirect3DVolumeTexture9 *iface)
{ return volume_texture_from_iface(iface)->priority; }
static void WINAPI volume_texture_preload(IDirect3DVolumeTexture9 *iface)
{ (void)iface; }
static D3DRESOURCETYPE WINAPI volume_texture_get_type(
        IDirect3DVolumeTexture9 *iface)
{ (void)iface; return D3DRTYPE_VOLUMETEXTURE; }
static DWORD WINAPI volume_texture_set_lod(IDirect3DVolumeTexture9 *iface, DWORD lod)
{
    D9VolumeTexture *texture = volume_texture_from_iface(iface);
    return d9wg_set_texture_lod(texture->device->handle, texture->handle,
            texture->pool, texture->level_count, &texture->lod, lod);
}
static DWORD WINAPI volume_texture_get_lod(IDirect3DVolumeTexture9 *iface)
{ return volume_texture_from_iface(iface)->lod; }
static DWORD WINAPI volume_texture_get_level_count(
        IDirect3DVolumeTexture9 *iface)
{ return volume_texture_from_iface(iface)->level_count; }
static HRESULT WINAPI volume_texture_set_auto_gen_filter_type(
        IDirect3DVolumeTexture9 *iface, D3DTEXTUREFILTERTYPE type)
{ (void)iface; (void)type; return TRACE_REFUSE(D3DERR_INVALIDCALL); }
static D3DTEXTUREFILTERTYPE WINAPI volume_texture_get_auto_gen_filter_type(
        IDirect3DVolumeTexture9 *iface)
{ (void)iface; return D3DTEXF_NONE; }
static void WINAPI volume_texture_generate_mip_sublevels(
        IDirect3DVolumeTexture9 *iface)
{ (void)iface; }

static HRESULT WINAPI volume_texture_get_level_desc(
        IDirect3DVolumeTexture9 *iface, UINT level, D3DVOLUME_DESC *desc)
{
    D9VolumeTexture *texture = volume_texture_from_iface(iface);
    D9TextureLevel *level_data;
    if (!desc || level >= texture->level_count)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    level_data = &texture->levels[level];
    ZeroMemory(desc, sizeof(*desc));
    desc->Format = texture->format;
    desc->Type = D3DRTYPE_VOLUME;
    desc->Usage = texture->usage;
    desc->Pool = texture->pool;
    desc->Width = level_data->width;
    desc->Height = level_data->height;
    desc->Depth = level_data->depth;
    return D3D_OK;
}

static HRESULT WINAPI volume_texture_lock_box(IDirect3DVolumeTexture9 *iface,
        UINT level, D3DLOCKED_BOX *locked, const D3DBOX *box, DWORD flags)
{
    D9VolumeTexture *texture = volume_texture_from_iface(iface);
    D9TextureLevel *level_data;
    D3DBOX area;
    UINT block_width, block_height, block_bytes;
    if (!locked || level >= texture->level_count)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    level_data = &texture->levels[level];
    if (level_data->locked)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (box) area = *box;
    else {
        area.Left = area.Top = area.Front = 0;
        area.Right = level_data->width;
        area.Bottom = level_data->height;
        area.Back = level_data->depth;
    }
    if (area.Right <= area.Left || area.Bottom <= area.Top
            || area.Back <= area.Front || area.Right > level_data->width
            || area.Bottom > level_data->height
            || area.Back > level_data->depth
            || !d9wg_format_layout(device_compatibility_api(texture->device),
                    texture->format, &block_width,
                    &block_height, &block_bytes)
            || area.Left % block_width || area.Top % block_height)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (device_compatibility_api(texture->device) == 8u
            && ((area.Right != level_data->width && area.Right % block_width)
                || (area.Bottom != level_data->height && area.Bottom % block_height)))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    level_data->lock_box = area;
    level_data->lock_flags = flags;
    level_data->locked = TRUE;
    locked->RowPitch = (INT)level_data->row_pitch;
    locked->SlicePitch = (INT)level_data->slice_pitch;
    locked->pBits = level_data->shadow
            + area.Front * level_data->slice_pitch
            + (area.Top / block_height) * level_data->row_pitch
            + (area.Left / block_width) * block_bytes;
    return D3D_OK;
}

static HRESULT WINAPI volume_texture_unlock_box(
        IDirect3DVolumeTexture9 *iface, UINT level)
{
    D9VolumeTexture *texture = volume_texture_from_iface(iface);
    D9TextureLevel *level_data;
    BOOL result = TRUE;
    if (level >= texture->level_count)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    level_data = &texture->levels[level];
    if (!level_data->locked)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!(level_data->lock_flags & D3DLOCK_READONLY))
        result = emit_volume_texture_update(texture, level,
                &level_data->lock_box);
    level_data->locked = FALSE;
    level_data->lock_flags = 0;
    ZeroMemory(&level_data->lock_box, sizeof(level_data->lock_box));
    return result ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI volume_texture_get_volume_level(
        IDirect3DVolumeTexture9 *iface, UINT level, IDirect3DVolume9 **out)
{
    D9VolumeTexture *texture = volume_texture_from_iface(iface);
    if (!out) return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (device_compatibility_api(texture->device) == 8u) *out = NULL;
    if (level >= texture->level_count) return TRACE_REFUSE(D3DERR_INVALIDCALL);
    D9Volume *volume = texture->levels[level].level_volume;
    if (!volume) {
        volume = (D9Volume *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                sizeof(*volume));
        if (!volume) return TRACE_REFUSE(E_OUTOFMEMORY);
        volume->iface.lpVtbl = &g_volume_vtbl;
        volume->texture = texture;
        volume->level = level;
        texture->levels[level].level_volume = volume;
    }
    *out = &volume->iface;
    IDirect3DVolume9_AddRef(*out);
    return D3D_OK;
}

static HRESULT WINAPI volume_texture_add_dirty_box(
        IDirect3DVolumeTexture9 *iface, const D3DBOX *box)
{ (void)iface; (void)box; return D3D_OK; }

static IDirect3DVolumeTexture9Vtbl g_volume_texture_vtbl = {
    .QueryInterface = volume_texture_query_interface,
    .AddRef = volume_texture_add_ref,
    .Release = volume_texture_release,
    .GetDevice = volume_texture_get_device,
    .SetPrivateData = volume_texture_set_private_data,
    .GetPrivateData = volume_texture_get_private_data,
    .FreePrivateData = volume_texture_free_private_data,
    .SetPriority = volume_texture_set_priority,
    .GetPriority = volume_texture_get_priority,
    .PreLoad = volume_texture_preload,
    .GetType = volume_texture_get_type,
    .SetLOD = volume_texture_set_lod,
    .GetLOD = volume_texture_get_lod,
    .GetLevelCount = volume_texture_get_level_count,
    .SetAutoGenFilterType = volume_texture_set_auto_gen_filter_type,
    .GetAutoGenFilterType = volume_texture_get_auto_gen_filter_type,
    .GenerateMipSubLevels = volume_texture_generate_mip_sublevels,
    .GetLevelDesc = volume_texture_get_level_desc,
    .GetVolumeLevel = volume_texture_get_volume_level,
    .LockBox = volume_texture_lock_box,
    .UnlockBox = volume_texture_unlock_box,
    .AddDirtyBox = volume_texture_add_dirty_box
};

static HRESULT WINAPI volume_query_interface(IDirect3DVolume9 *iface,
        REFIID iid, void **object)
{
    D9Volume *volume = volume_from_iface(iface);
    if (!object) return E_POINTER;
    *object = NULL;
    if (!iid || (!iid_is_unknown(iid)
            && !guid_equal(iid, &IID_IDirect3DVolume9)))
        return E_NOINTERFACE;
    *object = iface;
    IDirect3DVolumeTexture9_AddRef(&volume->texture->iface);
    return S_OK;
}
static ULONG WINAPI volume_add_ref(IDirect3DVolume9 *iface)
{ D9Volume *volume = volume_from_iface(iface);
  return IDirect3DVolumeTexture9_AddRef(&volume->texture->iface); }
static ULONG WINAPI volume_release(IDirect3DVolume9 *iface)
{ D9Volume *volume = volume_from_iface(iface);
  return IDirect3DVolumeTexture9_Release(&volume->texture->iface); }
static HRESULT WINAPI volume_get_device(IDirect3DVolume9 *iface,
        IDirect3DDevice9 **out)
{ D9Volume *volume = volume_from_iface(iface);
  return IDirect3DVolumeTexture9_GetDevice(&volume->texture->iface, out); }
static HRESULT WINAPI volume_set_private_data(IDirect3DVolume9* iface,
    REFGUID guid, const void* data, DWORD size, DWORD flags)
{
    D9Volume* volume = volume_from_iface(iface);
    D9WGPrivateStore* store = &volume->texture->levels[volume->level].private_data;
    return TRACE_REFUSE(d9wg_private_hresult(d9wg_private_set(store, guid, data, size, flags, &d9wg_private_ops)));
}
static HRESULT WINAPI volume_get_private_data(IDirect3DVolume9* iface,
    REFGUID guid, void* data, DWORD* size)
{
    D9Volume* volume = volume_from_iface(iface);
    D9WGPrivateStore* store = &volume->texture->levels[volume->level].private_data;
    return TRACE_REFUSE(d9wg_get_private_data(store, guid, data, size));
}
static HRESULT WINAPI volume_free_private_data(IDirect3DVolume9* iface,
    REFGUID guid)
{
    D9Volume* volume = volume_from_iface(iface);
    D9WGPrivateStore* store = &volume->texture->levels[volume->level].private_data;
    return TRACE_REFUSE(d9wg_private_hresult(d9wg_private_free(store, guid, &d9wg_private_ops)));
}
static HRESULT WINAPI volume_get_container(IDirect3DVolume9 *iface,
        REFIID iid, void **object)
{ D9Volume *volume = volume_from_iface(iface);
  return IDirect3DVolumeTexture9_QueryInterface(&volume->texture->iface, iid,
          object); }
static HRESULT WINAPI volume_get_desc(IDirect3DVolume9 *iface,
        D3DVOLUME_DESC *desc)
{ D9Volume *volume = volume_from_iface(iface);
  return IDirect3DVolumeTexture9_GetLevelDesc(&volume->texture->iface,
          volume->level, desc); }
static HRESULT WINAPI volume_lock_box(IDirect3DVolume9 *iface,
        D3DLOCKED_BOX *locked, const D3DBOX *box, DWORD flags)
{ D9Volume *volume = volume_from_iface(iface);
  return IDirect3DVolumeTexture9_LockBox(&volume->texture->iface,
          volume->level, locked, box, flags); }
static HRESULT WINAPI volume_unlock_box(IDirect3DVolume9 *iface)
{ D9Volume *volume = volume_from_iface(iface);
  return IDirect3DVolumeTexture9_UnlockBox(&volume->texture->iface,
          volume->level); }

static IDirect3DVolume9Vtbl g_volume_vtbl = {
    .QueryInterface = volume_query_interface,
    .AddRef = volume_add_ref,
    .Release = volume_release,
    .GetDevice = volume_get_device,
    .SetPrivateData = volume_set_private_data,
    .GetPrivateData = volume_get_private_data,
    .FreePrivateData = volume_free_private_data,
    .GetContainer = volume_get_container,
    .GetDesc = volume_get_desc,
    .LockBox = volume_lock_box,
    .UnlockBox = volume_unlock_box
};
static HRESULT WINAPI device_update_surface(IDirect3DDevice9 *iface,
        IDirect3DSurface9 *src_iface, const RECT *src_rect,
        IDirect3DSurface9 *dst_iface, const POINT *dst_point)
{
    D9Device *device = device_from_iface(iface);
    D9Surface *source;
    D9Surface *destination;
    D9TextureLevel *source_level;
    D9TextureLevel *destination_level;
    const BYTE *source_shadow;
    UINT source_pitch;
    RECT source_area;
    RECT destination_area;
    UINT block_width, block_height, block_bytes;
    UINT source_block_x, source_block_y, destination_block_x;
    UINT destination_block_y, block_columns, block_rows, row_bytes, row;

    if (!src_iface || !dst_iface || src_iface->lpVtbl != &g_surface_vtbl
            || dst_iface->lpVtbl != &g_surface_vtbl)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    source = surface_from_iface(src_iface);
    destination = surface_from_iface(dst_iface);
    source_level = surface_texture_level(source);
    destination_level = surface_texture_level(destination);
    if (source->device != device || destination->device != device
            || source->format != destination->format
            || (source->texture ? source->texture->pool : source->pool)
                != D3DPOOL_SYSTEMMEM
            || (destination->texture ? destination->texture->pool
                    : destination->pool) != D3DPOOL_DEFAULT
            || (!destination->texture && !destination->cube_texture)
            || !destination_level
            || !texture_format_layout(source->format, &block_width,
                    &block_height, &block_bytes))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (source->shadow) {
        source_shadow = source->shadow;
        source_pitch = source->row_pitch;
    } else if (source_level && source_level->shadow) {
        source_shadow = source_level->shadow;
        source_pitch = source_level->row_pitch;
    } else {
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    if (!destination_level->shadow || (source_level && source_level->locked)
            || destination_level->locked)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (src_rect)
        source_area = *src_rect;
    else
        SetRect(&source_area, 0, 0, (int)source->width, (int)source->height);
    destination_area.left = dst_point ? dst_point->x : 0;
    destination_area.top = dst_point ? dst_point->y : 0;
    destination_area.right = destination_area.left
            + source_area.right - source_area.left;
    destination_area.bottom = destination_area.top
            + source_area.bottom - source_area.top;
    if (source_area.left < 0 || source_area.top < 0
            || source_area.right <= source_area.left
            || source_area.bottom <= source_area.top
            || source_area.right > (LONG)source->width
            || source_area.bottom > (LONG)source->height
            || destination_area.left < 0 || destination_area.top < 0
            || destination_area.right > (LONG)destination->width
            || destination_area.bottom > (LONG)destination->height
            || source_area.left % (LONG)block_width
            || source_area.top % (LONG)block_height
            || destination_area.left % (LONG)block_width
            || destination_area.top % (LONG)block_height)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);

    source_block_x = (UINT)source_area.left / block_width;
    source_block_y = (UINT)source_area.top / block_height;
    destination_block_x = (UINT)destination_area.left / block_width;
    destination_block_y = (UINT)destination_area.top / block_height;
    block_columns = ((UINT)(source_area.right - source_area.left)
            + block_width - 1u) / block_width;
    block_rows = ((UINT)(source_area.bottom - source_area.top)
            + block_height - 1u) / block_height;
    if (!multiply_u32(block_columns, block_bytes, &row_bytes))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    for (row = 0; row < block_rows; ++row)
        CopyMemory(destination_level->shadow
                    + (destination_block_y + row)
                        * destination_level->row_pitch
                    + destination_block_x * block_bytes,
                source_shadow + (source_block_y + row) * source_pitch
                    + source_block_x * block_bytes,
                row_bytes);
    destination_level->shadow_valid = destination_area.left == 0
            && destination_area.top == 0
            && destination_area.right == (LONG)destination_level->width
            && destination_area.bottom == (LONG)destination_level->height;
    if (destination->cube_texture)
        return emit_cube_texture_update(destination->cube_texture,
                destination->cube_face, destination->level,
                &destination_area) ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
    return emit_texture_update(destination->texture, destination->level,
            &destination_area) ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}
/*
 * The other classic upload route besides Lock/Unlock: fill a
 * D3DPOOL_SYSTEMMEM texture on the CPU, then blit it into the
 * D3DPOOL_DEFAULT one the device actually samples. Both sides already keep a
 * full per-level shadow here, so this is a shadow-to-shadow copy followed by
 * the same UPDATE_TEXTURE emission an unlock would have produced. Declared
 * after the texture helpers it calls; see the forward declaration above.
 */
#define D9WG_COPY_LEVEL D9TextureLevel
#define D9WG_COPY_2D_TYPE D9Texture
#define D9WG_COPY_CUBE_TYPE D9CubeTexture
#define D9WG_COPY_VOLUME_TYPE D9VolumeTexture
#define D9WG_COPY_MARK_VALID(level) ((level)->shadow_valid = TRUE)
#define D9WG_COPY_UPLOAD_2D(t,l,r) emit_texture_update((t),(l),(r))
#define D9WG_COPY_UPLOAD_CUBE(t,f,l,r) emit_cube_texture_update((t),(f),(l),(r))
#define D9WG_COPY_UPLOAD_VOLUME(t,l,b) emit_volume_texture_update((t),(l),(b))
#include "../runtime/d9wg_texture_copy_bridge.h"

static HRESULT WINAPI device_update_texture(IDirect3DDevice9 *iface,
        IDirect3DBaseTexture9 *src_iface, IDirect3DBaseTexture9 *dst_iface)
{
    D9Device *device = device_from_iface(iface);
    D9Texture *source = (D9Texture *)src_iface, *destination = (D9Texture *)dst_iface;
    if (!source || !destination || source->iface.lpVtbl != destination->iface.lpVtbl)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (source->iface.lpVtbl == (const void *)&g_volume_texture_vtbl) {
        D9VolumeTexture *from = (D9VolumeTexture *)src_iface, *to = (D9VolumeTexture *)dst_iface;
        if (from->device != device || to->device != device || !d9wg_texture_copy_pools(from->pool, to->pool) || from->format != to->format)
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        return TRACE_REFUSE(d9wg_update_texture_common(to, D9WG_COPY_VOLUME,
                from->levels, (D9WGTextureCopyExtent){from->width, from->height, from->depth, from->level_count},
                to->levels, (D9WGTextureCopyExtent){to->width, to->height, to->depth, to->level_count}));
    }
    if (source->iface.lpVtbl == (const void *)&g_cube_vtbl) {
        D9CubeTexture *from = (D9CubeTexture *)src_iface, *to = (D9CubeTexture *)dst_iface;
        if (from->device != device || to->device != device || !d9wg_texture_copy_pools(from->pool, to->pool) || from->format != to->format)
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        return TRACE_REFUSE(d9wg_update_texture_common(to, D9WG_COPY_CUBE,
                from->levels, (D9WGTextureCopyExtent){from->edge_length, from->edge_length, 1, from->level_count},
                to->levels, (D9WGTextureCopyExtent){to->edge_length, to->edge_length, 1, to->level_count}));
    }
    if (source->iface.lpVtbl != &g_texture_vtbl || source->device != device || destination->device != device ||
            !d9wg_texture_copy_pools(source->pool, destination->pool) || source->format != destination->format)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    return TRACE_REFUSE(d9wg_update_texture_common(destination, D9WG_COPY_2D,
            source->levels, (D9WGTextureCopyExtent){source->width, source->height, 1, source->level_count},
            destination->levels, (D9WGTextureCopyExtent){destination->width, destination->height, 1, destination->level_count}));
}

/*
 * Shared by GetRenderTargetData and GetFrontBufferData, which differ only in
 * how strict they are about the destination format.
 *
 * GetRenderTargetData is a straight copy and requires the two formats to be
 * identical. GetFrontBufferData is defined to hand back *the displayed image*
 * and D3D9 documents its destination as D3DFMT_A8R8G8B8 unconditionally --
 * whatever the swap chain's own format is. A back buffer created as
 * D3DFMT_X8R8G8B8 (which is what 3DMark06 asks for) therefore has to be
 * accepted against an A8R8G8B8 destination; treating that as a format mismatch
 * is what made the Image Quality test's frame dump fail with
 * "GetFrontBufferData failed: Invalid call".
 *
 * The two formats have the same 32-bit BGRA byte layout, so the copy itself is
 * unchanged and only the alpha channel's meaning differs: the front buffer is
 * what is on screen, and what is on screen is opaque.
 */
/* Mandatory reads must observe submission failure, including a capacity flush
 * of the draws preceding the request. Ordinary draw-drop continuation is not
 * a successful readback submission. The common reader already pins the map. */
static BOOL submit_readback_batch_locked(BOOL present)
{
    return d9wg_transport_submit(g_process, present);
}

static HRESULT copy_surface_to_system_memory(D9Device *device,
        IDirect3DSurface9 *rt_iface, IDirect3DSurface9 *dst_iface,
        BOOL front_buffer)
{
    D9Surface *rt;
    D9Surface *dst;
    D9TextureLevel *source_level;
    D9TextureLevel *destination_level = NULL;
    BYTE *destination;
    UINT destination_pitch;
    UINT source_row_bytes;
    UINT source_rows;
    UINT row;

    if (!rt_iface || !dst_iface || rt_iface->lpVtbl != &g_surface_vtbl
            || dst_iface->lpVtbl != &g_surface_vtbl)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    rt = surface_from_iface(rt_iface);
    dst = surface_from_iface(dst_iface);
    /*
     * One refusal per condition rather than one composite. TRACE_REFUSE records
     * the line, so a rejected call names *which* rule it broke in the trace --
     * a composite condition only says "invalid", which is what turned the first
     * attempt at this function into a second round trip through the VM.
     */
    if (rt->device != device || dst->device != device)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (rt->locked || dst->locked
            || (surface_texture_level(rt) && surface_texture_level(rt)->locked)
            || (surface_texture_level(dst) && surface_texture_level(dst)->locked))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (rt->width != dst->width || rt->height != dst->height)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!rt->swap_chain && (!rt->texture
            || !(rt->texture->usage & D3DUSAGE_RENDERTARGET)))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (front_buffer) {
        /*
         * SCRATCH as well as SYSTEMMEM. Both are plain CPU surfaces backed by a
         * shadow, the copy into them is identical, and SCRATCH is the more
         * natural choice for an image the app is only going to save to a file --
         * it is the pool that promises the device will never touch the surface.
         * 3DMark06's Image Quality dump picks exactly that, and it runs against
         * the real runtime, so refusing it here was this proxy being stricter
         * than D3D9 rather than D3D9 being strict.
         */
        if (dst->pool != D3DPOOL_SYSTEMMEM && dst->pool != D3DPOOL_SCRATCH)
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        /* The one conversion D3D9 promises here, and only between the two
         * layouts that are already byte-identical. Anything else would need a
         * real converter, and no caller asks for one. */
        if (dst->format != D3DFMT_A8R8G8B8
                || (rt->format != D3DFMT_A8R8G8B8
                    && rt->format != D3DFMT_X8R8G8B8))
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
    } else {
        /* GetRenderTargetData stays exactly as strict as D3D9 documents it:
         * SYSTEMMEM only, and a straight copy with no conversion. */
        if (dst->pool != D3DPOOL_SYSTEMMEM)
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        if (rt->format != dst->format)
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    source_level = surface_texture_level(rt);
    if (dst->shadow) {
        destination = dst->shadow;
        destination_pitch = dst->row_pitch;
    } else {
        destination_level = surface_texture_level(dst);
        if (!destination_level || !destination_level->shadow)
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        destination = destination_level->shadow;
        destination_pitch = destination_level->row_pitch;
    }
    if (!texture_level_layout(rt->format, rt->width, rt->height,
            &source_row_bytes, &source_rows, &row))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    (void)row;
    if (source_level && source_level->shadow && source_level->shadow_valid) {
        for (row = 0; row < source_rows; ++row)
            CopyMemory(destination + row * destination_pitch,
                    source_level->shadow + row * source_level->row_pitch,
                    source_row_bytes);
    } else {
        D9WGReadbackSurface request;
        D9WGReadbackDiagnostic detail;
        ZeroMemory(&request, sizeof(request));
        request.device_handle = device->handle;
        request.texture_handle = rt->texture ? rt->texture->handle : 0;
        request.level = rt->level;
        request.format = dst->format;
        request.width = rt->width;
        request.height = rt->height;
        request.destination_pitch = destination_pitch;
        UINT destination_bytes = dst->shadow ? dst->byte_count : destination_level->byte_count;
        int result = d9wg_surface_readback(g_process, &request, source_row_bytes,
                source_rows, destination, destination_bytes, submit_readback_batch_locked, &detail);
        if (result) {
            HOSTLOG_FAILED("readback failed: result=%d row=%lu request=%08lX response=%08lX "
                    "status=%lu bytes=%lu/%lu silence=%lums heartbeat=%08lX",
                    result, (DWORD)detail.first_row, (DWORD)detail.request_id,
                    (DWORD)detail.response_id, (DWORD)detail.status,
                    (DWORD)detail.actual_bytes, (DWORD)detail.expected_bytes,
                    (DWORD)detail.silent_ms, (DWORD)detail.heartbeat);
            return TRACE_REFUSE(result == D9WG_READBACK_INVALID
                    ? D3DERR_INVALIDCALL : D3DERR_DRIVERINTERNALERROR);
        }
    }
    if (front_buffer) {
        /* Opaque, whichever path filled the destination. The readback already
         * writes 0xFF for an X8R8G8B8 source, but the shadow fast path above is
         * a raw memcpy that would carry an X8 byte -- or a Clear's alpha --
         * straight into a channel the caller is entitled to read as alpha. An
         * image dump saved with alpha 0 is a fully transparent PNG. */
        UINT column;
        for (row = 0; row < source_rows; ++row) {
            BYTE *pixels = destination + row * destination_pitch;
            for (column = 0; column < rt->width; ++column)
                pixels[column * 4u + 3u] = 0xFFu;
        }
    }
    if (destination_level)
        destination_level->shadow_valid = TRUE;
    return D3D_OK;
}

static HRESULT WINAPI device_get_render_target_data(IDirect3DDevice9 *iface,
        IDirect3DSurface9 *rt_iface, IDirect3DSurface9 *dst_iface)
{
    return copy_surface_to_system_memory(device_from_iface(iface), rt_iface,
            dst_iface, FALSE);
}

static HRESULT WINAPI device_get_front_buffer_data(IDirect3DDevice9 *iface,
        UINT swapchain, IDirect3DSurface9 *dst)
{
    IDirect3DSurface9 *back_buffer;
    HRESULT result;
    if (swapchain || !dst)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    /* The back buffer holds the image the last Present put on screen, which is
     * what "front buffer" names; the host keeps it in an owned texture rather
     * than a canvas texture that expires (see d3d9_executor.js). */
    result = IDirect3DDevice9_GetBackBuffer(iface, 0, 0,
            D3DBACKBUFFER_TYPE_MONO, &back_buffer);
    if (FAILED(result)) return result;
    result = copy_surface_to_system_memory(device_from_iface(iface),
            back_buffer, dst, TRUE);
    IDirect3DSurface9_Release(back_buffer);
    return result;
}
/* A plain system-memory surface with no GPU resource behind it. Implemented
 * because it is how an application builds a cursor bitmap for
 * SetCursorProperties; it is deliberately limited to the 32-bit formats a
 * cursor may use, so nothing else starts depending on it as a general
 * offscreen surface. */
/* CPU surfaces share allocation/lifetime mechanics; their frontend selects
 * the format layout. API8 has linear formats absent from the9 layout. */
static HRESULT create_cpu_surface(D9Device *device, UINT width, UINT height,
        D3DFORMAT format, D3DPOOL pool, UINT api, IDirect3DSurface9 **out)
{
    D9Surface *surface;
    UINT row_pitch, row_count, byte_count;
    if (!d9wg_texture_level_layout(api, format, width, height,
            &row_pitch, &row_count, &byte_count))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    surface = (D9Surface *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*surface));
    if (!surface)
        return TRACE_REFUSE(E_OUTOFMEMORY);
    surface->shadow = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            byte_count);
    if (!surface->shadow) {
        HeapFree(GetProcessHeap(), 0, surface);
        return TRACE_REFUSE(E_OUTOFMEMORY);
    }
    surface->iface.lpVtbl = &g_surface_vtbl;
    surface->refcount = 1;
    surface->device = device;
    surface->width = width;
    surface->height = height;
    surface->format = format;
    surface->pool = pool;
    surface->row_pitch = row_pitch;
    surface->byte_count = byte_count;
    device_child_add_ref(device);

    *out = &surface->iface;
    TRACE_REGISTER_RANGE("SURFACE_OFFSCREEN", 0, surface, surface,
            sizeof(*surface));
    TRACE_REGISTER_RANGE("SURFACE_SHADOW", 0, surface, surface->shadow,
            surface->byte_count);
    TRACE("SURFACE CREATE kind=offscreen object=%08lX vtbl=%08lX ref=%ld "
            "device=%08lX texture=00000000 swapchain=00000000 shadow=%08lX "
            "level=0 size=%lux%lu format=%08lX bytes=%lu",
            (DWORD)(uintptr_t)*out,
            (DWORD)(uintptr_t)surface->iface.lpVtbl, surface->refcount,
            (DWORD)(uintptr_t)&device->iface,
            (DWORD)(uintptr_t)surface->shadow, surface->width,
            surface->height, (DWORD)surface->format, surface->byte_count);
    return D3D_OK;
}

static HRESULT WINAPI device_create_offscreen_plain_surface(
        IDirect3DDevice9 *iface, UINT width, UINT height, D3DFORMAT format,
        D3DPOOL pool, IDirect3DSurface9 **out, HANDLE *shared)
{
    D9Device *device = device_from_iface(iface);
    (void)shared;
    if (!out)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *out = NULL;
    if (!width || !height || pool == D3DPOOL_MANAGED
            || pool > D3DPOOL_SCRATCH || !supported_texture_format(format))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (pool == D3DPOOL_DEFAULT)
        return create_target_texture(device, width, height, format, 0u,
                D3DMULTISAMPLE_NONE, 0u, out);
    return create_cpu_surface(device, width, height, format, pool, 9u, out);
}

static HRESULT WINAPI device_multiply_transform(IDirect3DDevice9 *iface,
        D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix)
{
    D9Device *device = device_from_iface(iface);
    uint32_t slot;
    D3DMATRIX result;
    UINT row, column, k;
    const float *source;
    if (!matrix || !d9wg_state_block_transform_slot((UINT)state, &slot))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    source = state_block_transform_source(device, (UINT)state);
    for (row = 0; row < 4; ++row) {
        for (column = 0; column < 4; ++column) {
            float value = 0.0f;
            for (k = 0; k < 4; ++k)
                value += source[row * 4 + k]
                        * ((const float *)matrix)[k * 4 + column];
            ((float *)&result)[row * 4 + column] = value;
        }
    }
    return device_set_transform(iface, state, &result);
}
/*
 * SetMaterial/SetLight/LightEnable were emitted from M1 onwards but only
 * *stored* by the host; M3's fixed-function vertex stage consumes them for
 * real (d3d9_executor.js, lightingSignature/writeFixedVertexUniforms), which
 * is what finally makes the D3DVTXPCAPS_DIRECTIONALLIGHTS/POSITIONALLIGHTS
 * and MaxActiveLights = 8 that fill_caps() advertises true.
 */
static HRESULT WINAPI device_set_material(IDirect3DDevice9 *iface,
        const D3DMATERIAL9 *material)
{
    D9Device *device = device_from_iface(iface);
    D9WGSetMaterial command;
    if (!material)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (state_block_record_material(device, material)) return D3D_OK;
    device->material = *material;
    command.device_handle = device->handle;
    CopyMemory(command.diffuse, &material->Diffuse, sizeof(command.diffuse));
    CopyMemory(command.ambient, &material->Ambient, sizeof(command.ambient));
    CopyMemory(command.specular, &material->Specular, sizeof(command.specular));
    CopyMemory(command.emissive, &material->Emissive, sizeof(command.emissive));
    command.power = material->Power;
    return emit_command(D9WG_OP_SET_MATERIAL, &command, sizeof(command))
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_get_material(IDirect3DDevice9 *iface,
        D3DMATERIAL9 *material)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetMaterial");
    if (!material)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *material = device_from_iface(iface)->material;
    return D3D_OK;
}

static BOOL d9_emit_light(D9Device *device, DWORD index, const D3DLIGHT9 *light)
{
    D9WGSetLight command;command.device_handle = device->handle;command.index = index;
    CopyMemory(&command.type, light, sizeof(*light));
    return emit_command(D9WG_OP_SET_LIGHT, &command, sizeof(command));
}
static HRESULT WINAPI device_set_light(IDirect3DDevice9 *iface, DWORD index, const D3DLIGHT9 *light)
{
    D9Device *device = device_from_iface(iface);D9WGLightState *entry = NULL;
    if (!light || light->Type < D3DLIGHT_POINT || light->Type > D3DLIGHT_DIRECTIONAL)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (device->recording_result) {
        D9StateBlock *block = device->recording_result;
        if (index < D9_MAX_LIGHTS) {block->has_light[index] = TRUE;block->lights[index] = *light;}
        else {
            entry = d9wg_light_get(&block->extra_lights, index, TRUE);
            if (!entry) return TRACE_REFUSE(E_OUTOFMEMORY);
            entry->light_mask = TRUE;entry->light = *light;entry->set = TRUE;
        }
        return D3D_OK;
    }
    if (index >= D9_MAX_LIGHTS && !(entry = d9wg_light_get(&device->extra_lights, index, TRUE)))
        return TRACE_REFUSE(E_OUTOFMEMORY);
    if (entry) {entry->light = *light;entry->set = TRUE;}
    else {device->lights[index] = *light;device->light_set[index] = TRUE;}
    return d9_emit_light(device, index, light) ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}
static HRESULT WINAPI device_get_light(IDirect3DDevice9 *iface, DWORD index, D3DLIGHT9 *light)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetLight");
    D9Device *device = device_from_iface(iface);
    if (!light) return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (index < D9_MAX_LIGHTS) {
        if (!device->light_set[index]) return TRACE_REFUSE(D3DERR_INVALIDCALL);
        *light = device->lights[index];
    } else {
        D9WGLightState *entry = d9wg_light_find(device->extra_lights, index);
        if (!entry || !entry->set) return TRACE_REFUSE(D3DERR_INVALIDCALL);
        *light = entry->light;
    }
    return D3D_OK;
}
static HRESULT WINAPI device_light_enable(IDirect3DDevice9 *iface, DWORD index, WINBOOL enable)
{
    D9Device *device = device_from_iface(iface);D9WGLightState *entry = NULL;
    D3DLIGHT9 *light;BOOL *set, *enabled;
    if (device->recording_result) {
        D9StateBlock *block = device->recording_result;
        if (index < D9_MAX_LIGHTS) {
            if (!block->has_light[index] && !device->light_set[index]) {
                d9wg_light_default(&block->lights[index]);block->has_light[index] = TRUE;
            }
            block->has_light_enable[index] = TRUE;block->light_enabled[index] = !!enable;
        } else {
            D9WGLightState *current = d9wg_light_find(device->extra_lights, index);
            entry = d9wg_light_get(&block->extra_lights, index, TRUE);
            if (!entry) return TRACE_REFUSE(E_OUTOFMEMORY);
            if (!entry->light_mask && (!current || !current->set)) {
                d9wg_light_default(&entry->light);entry->light_mask = TRUE;entry->set = TRUE;
            }
            entry->enable_mask = TRUE;entry->enabled = !!enable;
        }
        return D3D_OK;
    }
    if (index < D9_MAX_LIGHTS) {light = device->lights + index;set = device->light_set + index;enabled = device->light_enabled + index;}
    else {
        entry = d9wg_light_get(&device->extra_lights, index, TRUE);
        if (!entry) return TRACE_REFUSE(E_OUTOFMEMORY);
        light = &entry->light;set = &entry->set;enabled = &entry->enabled;
    }
    if (!*set) {
        d9wg_light_default(light);*set = TRUE;
        if (!d9_emit_light(device, index, light)) return D3DERR_DRIVERINTERNALERROR;
    }
    *enabled = !!enable;
    D9WGLightEnable command = {device->handle, index, !!enable, 0};
    return emit_command(D9WG_OP_LIGHT_ENABLE, &command, sizeof(command)) ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}
static HRESULT WINAPI device_get_light_enable(IDirect3DDevice9 *iface,
        DWORD index, WINBOOL *enable)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetLightEnable");
    D9Device *device = device_from_iface(iface);
    BOOL known, enabled;
    if (index < D9_MAX_LIGHTS) {known = device->light_set[index];enabled = device->light_enabled[index];}
    else {
        D9WGLightState *entry = d9wg_light_find(device->extra_lights, index);
        known = entry && entry->set;enabled = entry ? entry->enabled : FALSE;
    }
    if (!d9wg_light_query_enable(known, enabled, enable))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    return D3D_OK;
}
static HRESULT WINAPI device_set_clip_plane(IDirect3DDevice9 *iface,
        DWORD index, const float *plane)
{
    D9Device *device = device_from_iface(iface);
    D9WGSetClipPlane command;
    if (!plane || index >= D9_MAX_CLIP_PLANES)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (state_block_record_clip_plane(device, index, plane)) return D3D_OK;
    CopyMemory(device->clip_planes[index], plane, sizeof(command.plane));
    command.device_handle = device->handle;
    command.index = index;
    CopyMemory(command.plane, plane, sizeof(command.plane));
    return emit_command(D9WG_OP_SET_CLIP_PLANE, &command, sizeof(command))
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}
static HRESULT WINAPI device_get_clip_plane(IDirect3DDevice9 *iface,
        DWORD index, float *plane)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetClipPlane");
    D9Device *device = device_from_iface(iface);
    if (!plane || index >= D9_MAX_CLIP_PLANES)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    CopyMemory(plane, device->clip_planes[index], sizeof(float) * 4u);
    return D3D_OK;
}
static HRESULT WINAPI device_set_clip_status(IDirect3DDevice9 *iface,
        const D3DCLIPSTATUS9 *status)
{
    if (!status) return TRACE_REFUSE(D3DERR_INVALIDCALL);
    device_from_iface(iface)->clip_status_valid = TRUE;
    CopyMemory(&device_from_iface(iface)->clip_status, status, sizeof(*status));
    return D3D_OK;
}
static HRESULT WINAPI device_get_clip_status(IDirect3DDevice9 *iface,
        D3DCLIPSTATUS9 *status)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetClipStatus");
    if (!status) return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!device_from_iface(iface)->clip_status_valid) return TRACE_REFUSE(D3DERR_INVALIDCALL);
    CopyMemory(status, &device_from_iface(iface)->clip_status, sizeof(*status));
    return D3D_OK;
}
static HRESULT WINAPI device_get_sampler_state(IDirect3DDevice9 *iface,
        DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD *value)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetSamplerState");
    D9Device *device = device_from_iface(iface);
    UINT slot;
    if (!value || !sampler_state_slot(sampler, &slot)
            || (UINT)type >= D9_MAX_SAMPLER_STATES)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *value = device->sampler_states[slot][type];
    return D3D_OK;
}

/* Drives the host's GPUSampler cache since M2 (plan 4.4/12): the parameter
 * tuple stored here is the cache key, so a stage's filtering follows the app's
 * state rather than the texture that happens to be bound to it. */
static HRESULT WINAPI device_set_sampler_state(IDirect3DDevice9 *iface,
        DWORD sampler, D3DSAMPLERSTATETYPE type, DWORD value)
{
    D9Device *device = device_from_iface(iface);
    D9WGSetSamplerState command;
    UINT slot;
    if (!sampler_state_slot(sampler, &slot)
            || (UINT)type >= D9_MAX_SAMPLER_STATES)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (state_block_record_sampler_state(device, slot, (UINT)type, value)) return D3D_OK;
    if (device->sampler_states[slot][type] == value)
        return D3D_OK;
    device->sampler_states[slot][type] = value;
    command.device_handle = device->handle;
    command.sampler = sampler;
    command.state = type;
    command.value = value;
    return emit_command(D9WG_OP_SET_SAMPLER_STATE, &command, sizeof(command))
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}
/*
 * D3D9 palettes. PALETTEENTRY is peRed/peGreen/peBlue/peFlags, and D3D9
 * reinterprets peFlags as alpha -- which is why the entries are stored as
 * D3DCOLOR here rather than passed through: the wire format is one unambiguous
 * A8R8G8B8 word per entry, and the reinterpretation happens once, at the edge.
 */
static HRESULT WINAPI device_set_palette_entries(IDirect3DDevice9 *iface,
        UINT index, const PALETTEENTRY *entries)
{
    D9Device *device = device_from_iface(iface);
    D9WGSetPalette command;
    uint8_t *payload;
    uint8_t *blob;
    BOOL sent = FALSE;

    if (!entries || index >= D9_MAX_PALETTES)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!d9wg_palette_set(&device->palettes, index, entries))
        return TRACE_REFUSE(E_OUTOFMEMORY);

    ZeroMemory(&command, sizeof(command));
    EnterCriticalSection(&g_transport_lock);
    if (reserve_command_locked(D9WG_OP_SET_PALETTE, sizeof(command),
            256u * 4u, NULL, &payload, &blob)) {
        command.device_handle = device->handle;
        command.palette_index = index;
        command.entry_count = 256u;
        command.data_offset = (uint32_t)(blob - batch_base());
        CopyMemory(payload, &command, sizeof(command));
        CopyMemory(blob, d9wg_palette_find(&device->palettes, index), 256u * 4u);
        sent = TRUE;
    }
    LeaveCriticalSection(&g_transport_lock);
    return sent ? D3D_OK : TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
}

static HRESULT WINAPI device_get_palette_entries(IDirect3DDevice9 *iface,
        UINT index, PALETTEENTRY *entries)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetPaletteEntries");
    D9Device *device = device_from_iface(iface);

    if (!entries || index >= D9_MAX_PALETTES)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    d9wg_palette_get(&device->palettes, index, entries);
    return D3D_OK;
}

static HRESULT WINAPI device_set_current_texture_palette(
        IDirect3DDevice9 *iface, UINT index)
{
    D9Device *device = device_from_iface(iface);
    D9WGSetCurrentTexturePalette command;

    if (index >= D9_MAX_PALETTES)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (state_block_record_palette(device, index)) return D3D_OK;
    if (device->current_palette == index) return D3D_OK;
    device->current_palette = index;
    command.device_handle = device->handle;
    command.palette_index = index;
    return emit_command(D9WG_OP_SET_CURRENT_TEXTURE_PALETTE, &command,
            sizeof(command)) ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_get_current_texture_palette(
        IDirect3DDevice9 *iface, UINT *index)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetCurrentTexturePalette");
    D9Device *device = device_from_iface(iface);
    if (!index)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *index = device->current_palette;
    return D3D_OK;
}
/*
 * ---- ProcessVertices ----
 *
 * Runs the fixed-function vertex pipeline over a source stream and writes
 * pre-transformed vertices into a destination vertex buffer, which the app
 * then draws as XYZRHW geometry or reads back.
 *
 * It has to happen guest-side. The whole point of the call is that the result
 * lands in guest-visible memory the app can Lock and read, and this stack's
 * host is asynchronous -- there is no synchronous GPU round trip that could
 * answer it. So this is a real software transform over the state the guest
 * already shadows for SetTransform/SetViewport/SetMaterial/SetLight, ported
 * from the D3D8 path's implementation, which has the same problem and solved
 * it the same way.
 *
 * The shared C kernel handles the supported fixed-function layouts. Bound
 * vertex shaders, unsupported packed inputs and multiple source
 * streams still require further software-pipeline work and are refused.
 */
#define D9_PV_MAX_TEXCOORDS 8

typedef D9WGProcessLayout D9ProcessLayout;

/* This DLL links no C runtime (see build.sh), so libm's sqrtf is unavailable.
 * Newton-Raphson from the classic bit-level estimate converges to float
 * precision well within four iterations for the magnitudes vertex normals and
 * light distances actually take. */


/* D3D matrices are row-major and its vectors are row vectors, so this is
 * `in * matrix`, not `matrix * in`. Getting the side wrong transposes every
 * transform, which looks like a broken camera rather than a broken multiply. */
static float d9_sqrt(float value)
{
    return d9wg_sqrt_positive(value);
}



static void d9_multiply_matrix(const D3DMATRIX *left, const D3DMATRIX *right,
        D3DMATRIX *out)
{
    d9wg_multiply_matrix(left, right, out);
}



static D3DCOLOR d9_pack_color(const float *rgba)
{
    return d9wg_pack_color(rgba);
}

static UINT d9_decl_type_bytes(BYTE type)
{
    switch (type) {
    case D3DDECLTYPE_FLOAT1: return 4;
    case D3DDECLTYPE_FLOAT2: return 8;
    case D3DDECLTYPE_FLOAT3: return 12;
    case D3DDECLTYPE_FLOAT4: return 16;
    case D3DDECLTYPE_D3DCOLOR: return 4;
    case D3DDECLTYPE_UBYTE4:
    case D3DDECLTYPE_UBYTE4N:
    case D3DDECLTYPE_SHORT2:
    case D3DDECLTYPE_SHORT2N:
    case D3DDECLTYPE_USHORT2N:
    case D3DDECLTYPE_UDEC3:
    case D3DDECLTYPE_DEC3N:
    case D3DDECLTYPE_FLOAT16_2: return 4;
    case D3DDECLTYPE_SHORT4:
    case D3DDECLTYPE_SHORT4N:
    case D3DDECLTYPE_USHORT4N:
    case D3DDECLTYPE_FLOAT16_4: return 8;
    default: return 0;
    }
}

/* Float output widths used by generated primitive declarations. */
static BOOL d9_decl_type_float_count(BYTE type, UINT *floats)
{
    switch (type) {
    case D3DDECLTYPE_FLOAT1: *floats = 1; return TRUE;
    case D3DDECLTYPE_FLOAT2: *floats = 2; return TRUE;
    case D3DDECLTYPE_FLOAT3: *floats = 3; return TRUE;
    case D3DDECLTYPE_FLOAT4: *floats = 4; return TRUE;
    default: return FALSE;
    }
}

static BOOL d9_build_process_layout(const D9WGVertexElement *elements,
        UINT count, D9ProcessLayout *layout)
{
    return d9wg_process_layout(elements, count, layout);
}

/*
 * The fixed-function lighting D3D9 specifies, in view space,
 * over the lights the guest has shadowed. Directional, point and spot lights
 * all reach here; the spot cone uses the same rho/theta/phi falloff the host's
 * generated vertex stage uses, so the two paths agree on the same scene.
 */
static BOOL d9_prepare_lighting(D9Device *device, const D3DMATRIX *world_view,
        D9WGTNLLighting *state)
{
    UINT index;
    d9wg_lighting_sdk_prepare(state, &device->material, device->render_states, world_view);
    if (!state->flags.lighting) return TRUE;
    for (index = 0; index < D9_MAX_LIGHTS; ++index) {
        if (device->light_set[index] && device->light_enabled[index] &&
                !d9wg_lighting_sdk_add(state, index, &device->lights[index], (const void *)device->transforms[D3DTS_VIEW])) return FALSE;
    }
    for (D9WGLightState *entry = device->extra_lights; entry; entry = entry->next)
        if (entry->set && entry->enabled && !d9wg_lighting_sdk_add(state, entry->index, &entry->light,
                (const void *)device->transforms[D3DTS_VIEW])) return FALSE;
    return TRUE;
}

/* The source layout is the declaration currently bound, whether it arrived as
 * a real IDirect3DVertexDeclaration9 or as an FVF. */
static BOOL d9_current_source_elements(D9Device *device, D9WGVertexElement *elements, UINT *element_count)
{
    UINT count = 0;

    if (device->vertex_declaration) {
        const D9VertexDeclaration *decl = device->vertex_declaration;
        UINT index;
        if (decl->element_count > D3DMAXDECLLENGTH)
            return FALSE;
        for (index = 0; index < decl->element_count; ++index) {
            elements[index].stream = (uint16_t)decl->elements[index].Stream;
            elements[index].offset = (uint16_t)decl->elements[index].Offset;
            elements[index].type = (uint8_t)decl->elements[index].Type;
            elements[index].method = (uint8_t)decl->elements[index].Method;
            elements[index].usage = (uint8_t)decl->elements[index].Usage;
            elements[index].usage_index =
                    (uint8_t)decl->elements[index].UsageIndex;
        }
        count = decl->element_count;
    } else if (device->fvf) {
        if (!fvf_to_declaration(device->fvf, elements, &count))
            return FALSE;
    } else {
        return FALSE;
    }
    *element_count = count;return TRUE;
}
static BOOL d9_current_source_layout(D9Device *device, D9ProcessLayout *layout)
{
    D9WGVertexElement elements[D3DMAXDECLLENGTH];UINT count;
    return d9_current_source_elements(device, elements, &count) &&
            d9_build_process_layout(elements, count, layout);
}

static int d9_current_source_position_transformed(D9Device *device)
{
    D9WGVertexElement elements[D3DMAXDECLLENGTH];UINT count;
    D9WGProcessStreamLayout layout;
    if (!d9_current_source_elements(device, elements, &count) ||
            !d9wg_process_stream_layout(elements, count, &layout)) return -1;
    return layout.vertex.position_transformed != 0;
}
static HRESULT d9_draw_index_bounds(D9Device *device, INT base_vertex, D9WGDrawIndexBounds *bounds)
{
    D9WGVertexElement elements[D3DMAXDECLLENGTH];UINT count;
    D9WGVertexStream streams[D9_MAX_STREAMS] = {{0}};
    uint32_t frequencies[D9_MAX_STREAMS];
    if (!d9_current_source_elements(device, elements, &count)) return D3DERR_INVALIDCALL;
    /* Programs without vertex inputs may use an empty declaration. */
    if (!count) {
        bounds->min_vertex = 0;bounds->vertex_limit = UINT32_MAX;return D3D_OK;
    }
    for (UINT i = 0; i < D9_MAX_STREAMS; ++i) frequencies[i] = device->streams[i].frequency;
    for (UINT i = 0; i < count; ++i) {
        UINT stream = elements[i].stream;
        if (stream >= D9_MAX_STREAMS) return D3DERR_INVALIDCALL;
        D9VertexBuffer *buffer = device->streams[stream].buffer;
        if (!buffer || buffer->device != device) return D3DERR_INVALIDCALL;
        streams[stream].length = buffer->length;streams[stream].base = device->streams[stream].offset;
        streams[stream].stride = device->streams[stream].stride;
    }
    return d9wg_draw_index_bounds(elements, count, streams, frequencies, base_vertex, bounds) ? D3D_OK : D3DERR_INVALIDCALL;
}

static HRESULT d9_bound_draw_range(D9Device *device, UINT first, UINT count, INT base_vertex)
{
    D9WGVertexElement elements[D3DMAXDECLLENGTH];UINT element_count;
    uint32_t extent[D9_MAX_STREAMS] = {0};
    if (!count || !d9_current_source_elements(device, elements, &element_count)) return D3DERR_INVALIDCALL;
    int64_t start = (int64_t)first + base_vertex;
    if (start < 0 || start > 0xffffffffu) return D3DERR_INVALIDCALL;
    for (UINT i = 0; i < element_count; ++i) {
        const D9WGVertexElement *element = elements + i;
        uint32_t bytes = d9wg_decl_bytes(element->type);
        if (element->stream >= D9_MAX_STREAMS || !bytes) return D3DERR_INVALIDCALL;
        uint32_t end = element->offset + bytes;
        if (extent[element->stream] < end) extent[element->stream] = end;
    }
    uint32_t frequency = device->streams[0].frequency;
    uint32_t instances = frequency & D3DSTREAMSOURCE_INDEXEDDATA ? frequency & D9_STREAMSOURCE_FREQUENCY_MASK : 1u;
    if (!instances) return D3DERR_INVALIDCALL;
    for (UINT i = 0; i < D9_MAX_STREAMS; ++i) {
        if (!extent[i]) continue;
        D9VertexBuffer *buffer = device->streams[i].buffer;
        if (!buffer || buffer->device != device) return D3DERR_INVALIDCALL;
        uint32_t vertex = (uint32_t)start, vertices = count, at;
        frequency = device->streams[i].frequency;
        if (frequency & D3DSTREAMSOURCE_INSTANCEDATA) {
            uint32_t divisor = frequency & D9_STREAMSOURCE_FREQUENCY_MASK;
            if (!divisor) return D3DERR_INVALIDCALL;
            vertex = 0;vertices = (instances - 1u)/divisor + 1u;
        }
        if (!d9wg_process_stream_range(buffer->length, device->streams[i].offset,
                device->streams[i].stride, extent[i], vertex, vertices, &at)) return D3DERR_INVALIDCALL;
    }
    return D3D_OK;
}

typedef struct D9ProcessInput {
    D9WGProcessStreamLayout layout;
    D9WGVertexStream streams[D9_MAX_STREAMS];
    D9ProcessLayout fast_layout;
    BOOL gather;
    BYTE *snapshot;
} D9ProcessInput;
static void d9_process_input_free(D9ProcessInput *input)
{
    if (input->snapshot) HeapFree(GetProcessHeap(), 0, input->snapshot);
    HeapFree(GetProcessHeap(), 0, input);
}
static HRESULT d9_process_input(D9Device *device, D9VertexBuffer *destination,
        UINT start, UINT count, BOOL allow_locked, D9ProcessInput **result)
{
    D9WGVertexElement elements[D3DMAXDECLLENGTH];UINT element_count;
    uint32_t first[D9_MAX_STREAMS];
    *result = NULL;
    if (!d9_current_source_elements(device, elements, &element_count)) return D3DERR_INVALIDCALL;
    D9ProcessInput *input = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*input));
    if (!input) return E_OUTOFMEMORY;
    HRESULT error = D3DERR_INVALIDCALL;
    if (!d9wg_process_stream_layout(elements, element_count, &input->layout) ||
            (destination && input->layout.vertex.position_transformed)) goto fail;
    for (UINT i = 0; i < D9_MAX_STREAMS; ++i) {
        if (!(input->layout.stream_mask & (1u << i))) continue;
        D9VertexBuffer *buffer = device->streams[i].buffer;
        if (!buffer || buffer->device != device || (buffer->locked && !allow_locked) || device->streams[i].frequency != 1u) goto fail;
        input->streams[i].vertices = buffer->shadow;
        input->streams[i].length = buffer->length;
        input->streams[i].base = device->streams[i].offset;
        input->streams[i].stride = device->streams[i].stride;
    }
    if (!d9wg_process_stream_validate(&input->layout, input->streams, start, count, first)) goto fail;
    input->gather = input->layout.stream_mask != 1u || !input->streams[0].stride;
    if (!input->gather && !d9_build_process_layout(elements, element_count, &input->fast_layout)) goto fail;
    /* Every alias observes the same pre-call bytes, including a constant
     * stream inside the destination. One snapshot covers all alias bindings. */
    for (UINT i = 0; i < D9_MAX_STREAMS; ++i) {
        if (!(input->layout.stream_mask & (1u << i)) || device->streams[i].buffer != destination) continue;
        if (!input->snapshot) {
            input->snapshot = HeapAlloc(GetProcessHeap(), 0, destination->length);
            if (!input->snapshot) { error = E_OUTOFMEMORY;goto fail; }
            CopyMemory(input->snapshot, destination->shadow, destination->length);
        }
        input->streams[i].vertices = input->snapshot;
    }
    *result = input;return D3D_OK;
fail:
    d9_process_input_free(input);return error;
}

/* Internal clipped UP emissions use one gathered stream. Only host wire
 * state changes: the application's declaration and all VB/IB shadows stay
 * bound, and the original host declaration is restored even after failure. */
typedef struct D9StreamClipEmit {
    D9Device *device;
    const D9WGVertexElement *elements;
    uint32_t count;
    BOOL remapped;
} D9StreamClipEmit;
static BOOL d9_emit_clip_layout(D9Device *device, DWORD fvf,
        const D9WGVertexElement *elements, uint32_t count, BOOL gather)
{
    BYTE payload[sizeof(D9WGSetFVF) + D3DMAXDECLLENGTH*sizeof(D9WGVertexElement)];
    D9WGSetFVF *command = (D9WGSetFVF *)payload;
    command->device_handle = device->handle;command->fvf = fvf;
    command->element_count = count;command->reserved = 0;
    D9WGVertexElement *output = (D9WGVertexElement *)(payload + sizeof(*command));
    for (uint32_t i = 0; i < count; ++i) {
        output[i] = elements[i];
        if (gather) { output[i].stream = 0;output[i].offset = (uint16_t)(i*16u); }
    }
    return emit_command(D9WG_OP_SET_FVF, payload, sizeof(*command) + count*sizeof(*output));
}
static int d9_clip_stream_emit(void *context, uint32_t primitive_type,
        const void *vertices, uint32_t count, uint32_t stride)
{
    D9StreamClipEmit *state = context;
    if (!state->remapped) {
        if (!d9_emit_clip_layout(state->device, 0, state->elements, state->count, TRUE)) return 0;
        state->remapped = TRUE;
    }
    return d9_clip_emit(state->device, primitive_type, vertices, count, stride);
}
static int d9_clip_stream_mask(void *context, uint32_t mask)
{ return d9_clip_mask(((D9StreamClipEmit *)context)->device, mask); }
static int d9_clip_stream_draw(D9Device *device, uint32_t primitive_type, const D9WGClipDrawSource *draw)
{
    UINT position_stream = 0, position_offset = 0;
    if (device->vertex_declaration) {
        BOOL found = FALSE;
        D9VertexDeclaration *declaration = device->vertex_declaration;
        for (UINT i = 0; i < declaration->element_count; ++i) {
            const D3DVERTEXELEMENT9 *element = declaration->elements + i;
            if (element->Usage == D3DDECLUSAGE_POSITIONT && !element->UsageIndex) {
                position_stream = element->Stream;position_offset = element->Offset;found = TRUE;break;
            }
        }
        if (!found) return 0;
    } else if ((device->fvf & 0x00eu) != D3DFVF_XYZRHW) return 0;
    if (position_stream >= D9_MAX_STREAMS) return -1;
    D9VertexBuffer *buffer = device->streams[position_stream].buffer;
    if (!buffer) return -1;
    d9wg_clip_buffer_commit(&buffer->clips, NULL, 0, 0, d9wg_vertex_windows_clip_release, NULL);
    if (!buffer->clips.head) return 0;
    BOOL live = FALSE;
    for (uint32_t vertex = 0; vertex < draw->count; ++vertex) {
        uint32_t index = d9wg_clip_read_index(draw, vertex);
        int64_t source = (int64_t)draw->first_vertex + draw->base_vertex + index;
        if (index < draw->min_vertex || index >= draw->vertex_count || source < 0 ||
                (source > 0xffffffffu && device->streams[position_stream].stride)) return -1;
        uint64_t at = device->streams[position_stream].offset + (uint64_t)source*device->streams[position_stream].stride + position_offset;
        if (at > buffer->length || buffer->length - at < 16u) return -1;
        const D9WGProcessViewport *viewport;
        if (d9wg_clip_buffer_lookup(&buffer->clips, (uint32_t)at, buffer->shadow + (uint32_t)at, &viewport)) live = TRUE;
    }
    d9wg_clip_buffer_commit(&buffer->clips, NULL, 0, 0, d9wg_vertex_windows_clip_release, NULL);
    if (!live) return 0;
    uint32_t begin = draw->indices ? draw->min_vertex : 0;
    uint32_t count = draw->indices ? draw->vertex_count - begin : draw->count;
    int64_t start = (int64_t)draw->first_vertex + draw->base_vertex + begin;
    if (start < 0 || start > 0xffffffffu) return -1;
    D9ProcessInput *input;
    HRESULT error = d9_process_input(device, NULL, (UINT)start, count, TRUE, &input);
    if (FAILED(error)) return error == E_OUTOFMEMORY ? -3 : -1;
    D9WGVertexElement output_elements[D3DMAXDECLLENGTH];D9WGProcessLayout output_layout;
    if (!d9wg_clip_stream_output(&input->layout, output_elements, &output_layout)) {
        d9_process_input_free(input);return -1;
    }
    D9WGClipDrawSource source = *draw;
    d9wg_clip_draw_streams_init(&source, &input->layout, input->streams);
    source.output_layout = &output_layout;
    if (source.stride < output_layout.stride) source.stride = output_layout.stride;
    source.clips = &buffer->clips;source.primitive_type = primitive_type;
    source.flat_shading = device->render_states[D3DRS_SHADEMODE] == D3DSHADE_FLAT;
    source.viewport = (D9WGProcessViewport){ (float)device->viewport.X, (float)device->viewport.Y,
            (float)device->viewport.Width, (float)device->viewport.Height, device->viewport.MinZ, device->viewport.MaxZ };
    D9StreamClipEmit state = { device, output_elements, input->layout.count, FALSE };
    int result = d9wg_vertex_windows_clip_draw_planes(&source, d9_clip_stream_emit, d9_clip_stream_mask,
            device->render_states[D3DRS_CLIPPLANEENABLE], &state);
    if (state.remapped) {
        BOOL restored;
        if (device->vertex_declaration) {
            D9WGSetVertexDeclaration command = { device->handle, device->vertex_declaration->handle };
            restored = emit_command(D9WG_OP_SET_VERTEX_DECLARATION, &command, sizeof(command));
        } else {
            D9WGVertexElement elements[D3DMAXDECLLENGTH];UINT element_count;
            restored = fvf_to_declaration(device->fvf, elements, &element_count) &&
                    d9_emit_clip_layout(device, device->fvf, elements, element_count, FALSE);
        }
        if (!restored) result = -2;
    }
    d9_process_input_free(input);return result;
}

static HRESULT d9_prepare_stream_clip_status(D9Device *device, const D9WGClipDrawSource *draw,
        D9WGClipStatus *next, BOOL *valid)
{
    int transformed = d9_current_source_position_transformed(device);
    if (transformed == 1) return D3D_OK;
    if (!device->render_states[D3DRS_CLIPPING]) {
        next->clip_union = next->clip_intersection = 0;*valid = TRUE;return D3D_OK;
    }
    if (transformed < 0 || device->vertex_shader) { *valid = FALSE;return D3D_OK; }
    uint32_t begin = draw->indices ? draw->min_vertex : 0;
    uint32_t count = draw->indices ? draw->vertex_count - begin : draw->count;
    int64_t start = (int64_t)draw->first_vertex + draw->base_vertex + begin;
    if (start < 0 || start > 0xffffffffu) return D3DERR_INVALIDCALL;
    D9ProcessInput *input;
    HRESULT error = d9_process_input(device, NULL, (UINT)start, count, TRUE, &input);
    if (error == D3DERR_INVALIDCALL) { *valid = FALSE;return D3D_OK; }
    if (FAILED(error)) return error;
    D9WGClipDrawSource source = *draw;source.layout = &input->layout.vertex;
    uint32_t result = d9wg_vertex_windows_clip_status_inputs(&source, &input->layout, input->streams,
            device->render_states, device->transforms[D3DTS_WORLD], 256, 0,
            device->transforms[D3DTS_VIEW], device->transforms[D3DTS_PROJECTION], device->clip_planes, next);
    d9_process_input_free(input);
    if (!result) *valid = TRUE;
    return result == 2 ? E_OUTOFMEMORY : result ? D3DERR_INVALIDCALL : D3D_OK;
}

static HRESULT process_vertices_layout(D9Device *device,
        D9VertexBuffer *destination, const D9ProcessLayout *destination_layout_ptr,
        UINT src_start, UINT dst_index, UINT count, DWORD flags)
{
    D3DMATRIX world_view;
    D3DMATRIX world_view_projection;
    BOOL lighting;
    BOOL copy_data;
    uint32_t validated_offset;

    /* D3D9 refuses to transform geometry that already is, and requires the
     * result to be pre-transformed. */
    if (!destination_layout_ptr->position_transformed ||
            !d9wg_process_canonical_output(destination_layout_ptr) ||
            !d9wg_process_range(destination->length, 0, destination_layout_ptr->stride,
                destination_layout_ptr->stride, dst_index, count, &validated_offset))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);

    d9_multiply_matrix((const D3DMATRIX *)device->transforms[D3DTS_WORLD],
            (const D3DMATRIX *)device->transforms[D3DTS_VIEW], &world_view);
    d9_multiply_matrix(&world_view,
            (const D3DMATRIX *)device->transforms[D3DTS_PROJECTION],
            &world_view_projection);
    D9WGTNLLighting light_state;
    if (!d9_prepare_lighting(device, &world_view, &light_state))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    D9WGVertexState vertex_state;
    d9wg_vertex_state_init(&vertex_state, device->render_states);
    if (!d9wg_vertex_textures_init(&vertex_state, device->texture_stage_states,
            sizeof(device->texture_stage_states[0]), device->transforms[D3DTS_TEXTURE0]))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    d9wg_vertex_clip_planes_init(&vertex_state, device->render_states[D3DRS_CLIPPLANEENABLE],
            device->clip_planes, device->transforms[D3DTS_WORLD]);
    lighting = light_state.flags.lighting != 0;
    (void)lighting;
    /* Transformed channels are always written; this flag only suppresses
     * passthrough data. */
    copy_data = (flags & D3DPV_DONOTCOPYDATA) == 0;

    {
        D9WGProcessViewport viewport = { (float)device->viewport.X, (float)device->viewport.Y,
                (float)device->viewport.Width, (float)device->viewport.Height,
                device->viewport.MinZ, device->viewport.MaxZ };
        D9ProcessInput *input;
        HRESULT input_error = d9_process_input(device, destination, src_start, count, FALSE, &input);
        if (FAILED(input_error)) return TRACE_REFUSE(input_error);
        uint32_t blend_error = d9wg_vertex_windows_blend(&vertex_state, device->render_states,
                device->transforms[D3DTS_WORLD], 256, 0, device->transforms[D3DTS_VIEW], device->transforms[D3DTS_PROJECTION]);
        if (blend_error) {
            d9_process_input_free(input);
            return TRACE_REFUSE(blend_error == 2 ? E_OUTOFMEMORY : D3DERR_INVALIDCALL);
        }
        D3DCAPS9 caps;fill_caps(&caps);
        float guard[4] = { caps.GuardBandLeft, caps.GuardBandTop, caps.GuardBandRight, caps.GuardBandBottom };
        D9WGClipBatch *clip_batch;
        uint32_t clip_error = d9wg_vertex_windows_clip(&vertex_state, device->render_states[D3DRS_CLIPPING],
                validated_offset + destination_layout_ptr->position, destination_layout_ptr->stride, count, &viewport, guard, &clip_batch);
        if (clip_error) {
            d9wg_vertex_windows_blend_free(&vertex_state);
            d9_process_input_free(input);
            return TRACE_REFUSE(clip_error == 2 ? E_OUTOFMEMORY : D3DERR_INVALIDCALL);
        }
        BOOL processed;
        if (input->gather) processed = d9wg_process_vertices_streams(&input->layout,
                input->streams, src_start, destination_layout_ptr, destination->shadow,
                destination->length, dst_index, count, &world_view, &world_view_projection,
                &viewport, &light_state, copy_data, &vertex_state);
        else processed = d9wg_process_vertices(&input->fast_layout,
                input->streams[0].vertices, input->streams[0].length,
                input->streams[0].base, input->streams[0].stride, src_start,
                destination_layout_ptr, destination->shadow, destination->length, dst_index, count,
                &world_view, &world_view_projection, &viewport, &light_state, copy_data, &vertex_state);
        d9wg_vertex_windows_blend_free(&vertex_state);
        d9_process_input_free(input);
        if (!processed) {
            if (clip_batch) HeapFree(GetProcessHeap(), 0, clip_batch);
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        }
        d9wg_clip_status_processed(&device->clip_status, clip_batch);
        if (!clip_batch) device->clip_status_valid = TRUE;
        d9wg_clip_buffer_commit(&destination->clips, clip_batch, validated_offset,
                count * destination_layout_ptr->stride, d9wg_vertex_windows_clip_release, NULL);
    }

    /* The transformed vertices live in the destination's guest shadow; the
     * host needs them too, since the app will draw straight from the buffer. */
    TRACE("OK ProcessVertices device=%08lX count=%lu stride=%lu lit=%lu",
            device->handle, count, destination_layout_ptr->stride,
            (DWORD)lighting);
    TRACE_MARK_EXIT("Device.ProcessVertices", D3D_OK, NULL);
    return emit_buffer_update(destination->handle,
            dst_index * destination_layout_ptr->stride,
            destination->shadow + dst_index * destination_layout_ptr->stride,
            count * destination_layout_ptr->stride, 0)
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_process_vertices(IDirect3DDevice9 *iface,
        UINT src_start, UINT dst_index, UINT count,
        IDirect3DVertexBuffer9 *dst, IDirect3DVertexDeclaration9 *decl,
        DWORD flags)
{
    D9Device *device = device_from_iface(iface);
    D9VertexBuffer *destination;
    D9ProcessLayout destination_layout;

    TRACE_MARK_ENTER("Device.ProcessVertices");
    if (!dst || dst->lpVtbl != &g_vb_vtbl) {
        TRACE_MARK_EXIT("Device.ProcessVertices", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    destination = vb_from_iface(dst);
    if (destination->device != device || destination->locked || !count) {
        TRACE_MARK_EXIT("Device.ProcessVertices", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    if (device->vertex_shader) {
        HOSTLOG_REFUSED("ProcessVertices refused: a vertex shader is bound, "
                "and running the fixed-function pipeline in its place would "
                "silently produce different geometry");
        TRACE_MARK_EXIT("Device.ProcessVertices", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    /* The output layout is the declaration D3D9 passes, or the destination
     * buffer's own FVF when it passes none. */
    if (decl) {
        D9VertexDeclaration *output = decl_from_iface(decl);
        D9WGVertexElement elements[D3DMAXDECLLENGTH];
        UINT index;
        if (output->device != device
                || output->element_count > D3DMAXDECLLENGTH) {
            TRACE_MARK_EXIT("Device.ProcessVertices", D3DERR_INVALIDCALL, NULL);
            return D3DERR_INVALIDCALL;
        }
        for (index = 0; index < output->element_count; ++index) {
            elements[index].stream = (uint16_t)output->elements[index].Stream;
            elements[index].offset = (uint16_t)output->elements[index].Offset;
            elements[index].type = (uint8_t)output->elements[index].Type;
            elements[index].method = (uint8_t)output->elements[index].Method;
            elements[index].usage = (uint8_t)output->elements[index].Usage;
            elements[index].usage_index =
                    (uint8_t)output->elements[index].UsageIndex;
        }
        if (!d9_build_process_layout(elements, output->element_count,
                &destination_layout)) {
            HOSTLOG_REFUSED("ProcessVertices refused: the output declaration "
                    "uses an element type this software vertex path cannot "
                    "write");
            TRACE_MARK_EXIT("Device.ProcessVertices", D3DERR_INVALIDCALL, NULL);
            return D3DERR_INVALIDCALL;
        }
    } else {
        D9WGVertexElement elements[D3DMAXDECLLENGTH];
        UINT element_count = 0;
        if (!destination->fvf
                || !fvf_to_declaration(destination->fvf, elements,
                        &element_count)
                || !d9_build_process_layout(elements, element_count,
                        &destination_layout)) {
            TRACE_MARK_EXIT("Device.ProcessVertices", D3DERR_INVALIDCALL, NULL);
            return D3DERR_INVALIDCALL;
        }
    }
    return process_vertices_layout(device, destination, &destination_layout,
            src_start, dst_index, count, flags);
}
static HRESULT WINAPI device_set_stream_source_freq(IDirect3DDevice9 *iface,
        UINT stream, UINT divider)
{
    D9Device *device = device_from_iface(iface);
    D9WGSetStreamSourceFreq command;
    DWORD mode;
    DWORD frequency;

    if (stream >= D9_MAX_STREAMS)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);

    mode = divider & (D3DSTREAMSOURCE_INDEXEDDATA |
            D3DSTREAMSOURCE_INSTANCEDATA);
    frequency = divider & D9_STREAMSOURCE_FREQUENCY_MASK;
    if (divider != 1u) {
        if (!frequency)
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        if ((stream == 0 && mode != D3DSTREAMSOURCE_INDEXEDDATA) ||
                (stream != 0 && mode != D3DSTREAMSOURCE_INSTANCEDATA))
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }

    if (state_block_record_stream_frequency(device, stream, divider)) return D3D_OK;
    if (device->streams[stream].frequency == divider)
        return D3D_OK;
    device->streams[stream].frequency = divider;
    command.device_handle = device->handle;
    command.stream = stream;
    command.divider = divider;
    return emit_command(D9WG_OP_SET_STREAM_SOURCE_FREQ, &command,
            sizeof(command)) ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_get_stream_source_freq(IDirect3DDevice9 *iface,
        UINT stream, UINT *divider)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetStreamSourceFreq");
    D9Device *device = device_from_iface(iface);
    if (stream >= D9_MAX_STREAMS || !divider)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *divider = device->streams[stream].frequency;
    return D3D_OK;
}
/*
 * ---- Higher-order primitives: rectangular and triangular patches ----
 *
 * WebGPU has no tessellation stage, so the surface is evaluated here and the
 * result is drawn as an ordinary indexed triangle list through
 * DrawIndexedPrimitiveUP. That needs no protocol opcode and no host change at
 * all: by the time anything crosses the wire it is plain geometry.
 *
 * Evaluating on the CPU is not a workaround for the missing stage so much as
 * the only place the work can happen with the right inputs -- the control
 * points live in a guest vertex buffer this DLL already shadows, and the
 * tessellation factor is a per-call argument rather than pipeline state.
 *
 * Scope, with every limit refused by name rather than approximated:
 *
 *   - D3DBASIS_BEZIER only. B-spline and Catmull-Rom bases have different
 *     control-point semantics; substituting Bezier for either draws a surface
 *     that is smooth, plausible and in the wrong place.
 *   - Degree up to cubic, which is what D3DDEVCAPS_RTPATCHES without
 *     D3DDEVCAPS_QUINTICRTPATCHES promises.
 *   - One patch per call: Width/Height are exactly Degree+1 control points.
 *   - Vertex components are FLOAT1..4 and D3DCOLOR. A packed or half-float
 *     component would be silently mis-decoded, which is geometry that is wrong
 *     rather than missing.
 */
#define D9_PATCH_MAX_CONTROL_POINTS 16u
#define D9_PATCH_MAX_COMPONENTS 64u

/* Integer power. This DLL links no C runtime, so there is no powf -- and the
 * exponents here are bounded by the patch degree, which makes a loop both
 * exact and shorter than any series would be. */
static float d9_powi(float base, UINT exponent)
{
    float result = 1.0f;
    while (exponent--) result *= base;
    return result;
}

/* The trinomial coefficient degree! / (i! j! k!) for a triangular Bernstein
 * basis. Degree is at most cubic here, so the factorials are a four-entry
 * table rather than a computation. */
static float d9_trinomial(UINT degree, UINT i, UINT j, UINT k)
{
    static const float factorial[6] = { 1.0f, 1.0f, 2.0f, 6.0f, 24.0f, 120.0f };
    if (degree > 5 || i > 5 || j > 5 || k > 5) return 0.0f;
    return factorial[degree] / (factorial[i] * factorial[j] * factorial[k]);
}

/* Bernstein basis of the given degree at t, into out[0..degree]. */
static void d9_bernstein(UINT degree, float t, float *out)
{
    float s = 1.0f - t;
    switch (degree) {
    case 1:
        out[0] = s;
        out[1] = t;
        break;
    case 2:
        out[0] = s * s;
        out[1] = 2.0f * s * t;
        out[2] = t * t;
        break;
    default: /* cubic */
        out[0] = s * s * s;
        out[1] = 3.0f * s * s * t;
        out[2] = 3.0f * s * t * t;
        out[3] = t * t * t;
        break;
    }
}

/*
 * A vertex decoded into plain floats, and the inverse. Interpolating a packed
 * D3DCOLOR as if it were a float is the classic way to turn a patch's vertex
 * colours into noise, so colours are unpacked to four floats, interpolated as
 * colours, and repacked.
 */
typedef struct D9PatchFormat {
    UINT element_count;
    UINT stride;
    UINT component_count;
    /* Indices into the decoded float vector, or -1. N-patch tessellation needs
     * both: PN triangles are built from positions and normals and from nothing
     * else. */
    int position_component;
    int normal_component;
    struct {
        UINT offset;
        UINT components;   /* floats this element contributes */
        BOOL packed_color;
    } elements[D3DMAXDECLLENGTH];
} D9PatchFormat;

static BOOL d9_patch_format(D9Device *device, D9PatchFormat *format)
{
    D9WGVertexElement elements[D3DMAXDECLLENGTH];
    UINT count = 0;
    UINT index;

    ZeroMemory(format, sizeof(*format));
    format->position_component = -1;
    format->normal_component = -1;
    if (device->vertex_declaration) {
        const D9VertexDeclaration *decl = device->vertex_declaration;
        if (decl->element_count > D3DMAXDECLLENGTH)
            return FALSE;
        for (index = 0; index < decl->element_count; ++index) {
            elements[index].stream = (uint16_t)decl->elements[index].Stream;
            elements[index].offset = (uint16_t)decl->elements[index].Offset;
            elements[index].type = (uint8_t)decl->elements[index].Type;
            elements[index].method = (uint8_t)decl->elements[index].Method;
            elements[index].usage = (uint8_t)decl->elements[index].Usage;
            elements[index].usage_index =
                    (uint8_t)decl->elements[index].UsageIndex;
        }
        count = decl->element_count;
    } else if (device->fvf) {
        if (!fvf_to_declaration(device->fvf, elements, &count))
            return FALSE;
    } else {
        return FALSE;
    }

    for (index = 0; index < count; ++index) {
        const D9WGVertexElement *e = &elements[index];
        UINT bytes = d9_decl_type_bytes(e->type);
        UINT components;
        BOOL packed_color = FALSE;
        UINT end;

        if (e->stream != 0 || !bytes || e->method != D3DDECLMETHOD_DEFAULT)
            return FALSE;
        if (e->type == D3DDECLTYPE_D3DCOLOR) {
            components = 4;
            packed_color = TRUE;
        } else if (!d9_decl_type_float_count(e->type, &components)) {
            return FALSE;
        }
        if (format->component_count + components > D9_PATCH_MAX_COMPONENTS)
            return FALSE;
        if (e->usage == D3DDECLUSAGE_POSITION && !e->usage_index
                && components >= 3)
            format->position_component = (int)format->component_count;
        else if (e->usage == D3DDECLUSAGE_NORMAL && !e->usage_index
                && components >= 3)
            format->normal_component = (int)format->component_count;
        format->elements[format->element_count].offset = e->offset;
        format->elements[format->element_count].components = components;
        format->elements[format->element_count].packed_color = packed_color;
        ++format->element_count;
        format->component_count += components;
        end = (UINT)e->offset + bytes;
        if (end > format->stride) format->stride = end;
    }
    return format->element_count > 0;
}

static void d9_patch_decode(const D9PatchFormat *format, const BYTE *vertex,
        float *out)
{
    UINT index;
    UINT cursor = 0;
    for (index = 0; index < format->element_count; ++index) {
        const BYTE *field = vertex + format->elements[index].offset;
        if (format->elements[index].packed_color) {
            DWORD color = *(const DWORD *)field;
            out[cursor++] = ((color >> 16) & 0xffu) / 255.0f;
            out[cursor++] = ((color >> 8) & 0xffu) / 255.0f;
            out[cursor++] = (color & 0xffu) / 255.0f;
            out[cursor++] = ((color >> 24) & 0xffu) / 255.0f;
        } else {
            UINT component;
            for (component = 0; component < format->elements[index].components;
                    ++component)
                out[cursor++] = ((const float *)field)[component];
        }
    }
}

static void d9_patch_encode(const D9PatchFormat *format, const float *in,
        BYTE *vertex)
{
    UINT index;
    UINT cursor = 0;
    for (index = 0; index < format->element_count; ++index) {
        BYTE *field = vertex + format->elements[index].offset;
        if (format->elements[index].packed_color) {
            float rgba[4];
            rgba[0] = in[cursor];
            rgba[1] = in[cursor + 1];
            rgba[2] = in[cursor + 2];
            rgba[3] = in[cursor + 3];
            cursor += 4;
            *(DWORD *)field = d9_pack_color(rgba);
        } else {
            UINT component;
            for (component = 0; component < format->elements[index].components;
                    ++component)
                ((float *)field)[component] = in[cursor++];
        }
    }
}

/* The uniform segment count this patch is tessellated at. D3D9 allows a
 * different count per edge; a uniform grid at the largest of them keeps the
 * shared edges of adjacent patches watertight, which differing counts would
 * only achieve with a stitching step nothing here does. */
static UINT d9_patch_segments(const float *segments, UINT count)
{
    UINT result = 1;
    UINT index;
    for (index = 0; index < count; ++index) {
        float value = segments ? segments[index] : 1.0f;
        UINT rounded;
        if (!(value > 1.0f)) continue;
        rounded = (UINT)(value + 0.5f);
        if (rounded > result) result = rounded;
    }
    if (result > D9_PATCH_MAX_SEGMENTS) result = D9_PATCH_MAX_SEGMENTS;
    return result;
}

static BOOL d9_patch_basis_supported(D3DBASISTYPE basis, D3DDEGREETYPE degree,
        const char *what)
{
    if (basis != D3DBASIS_BEZIER) {
        HOSTLOG_REFUSED("%s refused: only D3DBASIS_BEZIER is implemented; "
                "basis %lu has different control-point semantics and "
                "substituting Bezier would draw a plausible surface in the "
                "wrong place", what, (DWORD)basis);
        return FALSE;
    }
    if (degree != D3DDEGREE_LINEAR && degree != D3DDEGREE_QUADRATIC
            && degree != D3DDEGREE_CUBIC) {
        HOSTLOG_REFUSED("%s refused: degree %lu is outside LINEAR/QUADRATIC/"
                "CUBIC, which is what D3DDEVCAPS_RTPATCHES without "
                "QUINTICRTPATCHES promises", what, (DWORD)degree);
        return FALSE;
    }
    return TRUE;
}

/* The grid -> triangle list index buffer shared by both patch kinds. */
static void d9_patch_grid_indices(UINT segments, uint16_t *indices)
{
    UINT row;
    UINT column;
    UINT cursor = 0;
    const UINT pitch = segments + 1;
    for (row = 0; row < segments; ++row) {
        for (column = 0; column < segments; ++column) {
            uint16_t a = (uint16_t)(row * pitch + column);
            uint16_t b = (uint16_t)(a + 1);
            uint16_t c = (uint16_t)(a + pitch);
            uint16_t d = (uint16_t)(c + 1);
            indices[cursor++] = a;
            indices[cursor++] = b;
            indices[cursor++] = c;
            indices[cursor++] = c;
            indices[cursor++] = b;
            indices[cursor++] = d;
        }
    }
}

/*
 * ---- N-patches (PN triangles) ----
 *
 * Each source triangle becomes a cubic Bezier triangle built from its three
 * positions and normals -- Vlachos et al.'s construction, which is what
 * D3DRS_POSITIONDEGREE and D3DRS_NORMALDEGREE select, whose defaults are a
 * cubic position and a linear normal.
 * The point of it is that a low-polygon mesh gains curvature without new art:
 * the control net is derived from the normals the mesh already carries.
 *
 * Evaluated here and drawn as an ordinary indexed triangle list, so nothing
 * about it reaches the protocol. It sits on the draw path, which is why every
 * entry point gates on npatch_segments being non-zero before doing any work.
 */
static void d9_normalize3(float *v)
{
    float length = d9_sqrt(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (length < 1e-6f) return;
    v[0] /= length;
    v[1] /= length;
    v[2] /= length;
}

/* Barycentric grid layout: row r holds (level + 1 - r) points. */
static UINT d9_npatch_row_offset(UINT level, UINT row)
{
    /* sum over k < row of (level + 1 - k) */
    return row * (level + 1) - (row * (row - 1)) / 2;
}

static UINT d9_npatch_point_count(UINT level)
{
    return (level + 1) * (level + 2) / 2;
}

/*
 * One source triangle -> `out_vertices` grid vertices and `out_indices`
 * triangle indices, both appended at the caller's cursor. `corners` holds the
 * three decoded source vertices.
 */
static void d9_npatch_triangle(const D9PatchFormat *format, UINT level,
        const float *corners, float *scratch, BYTE *out_vertices,
        uint16_t *out_indices, UINT base_vertex, BOOL cubic_position,
        BOOL quadratic_normal)
{
    const UINT components = format->component_count;
    const int position = format->position_component;
    const int normal = format->normal_component;
    /* b300..b111: the ten cubic control points, positions only. */
    float b[10][3];
    float n[6][3];
    float centre[3];
    float edge_centre[3];
    UINT corner;
    UINT axis;
    UINT row;
    UINT column;
    UINT cursor = 0;

    for (corner = 0; corner < 3; ++corner) {
        for (axis = 0; axis < 3; ++axis) {
            b[corner][axis] = corners[corner * components + position + axis];
            n[corner][axis] = corners[corner * components + normal + axis];
        }
        d9_normalize3(n[corner]);
    }
    /* Six edge control points: each is the corner pushed a third of the way
     * along the edge and then projected back onto that corner's tangent plane,
     * which is the whole trick -- it is what turns a flat edge into one that
     * respects the normal. */
    {
        static const UINT from[6] = { 0, 1, 1, 2, 2, 0 };
        static const UINT to[6]   = { 1, 0, 2, 1, 0, 2 };
        UINT edge;
        for (edge = 0; edge < 6; ++edge) {
            const UINT i = from[edge];
            const UINT j = to[edge];
            float delta[3];
            float w;
            for (axis = 0; axis < 3; ++axis)
                delta[axis] = b[j][axis] - b[i][axis];
            w = delta[0] * n[i][0] + delta[1] * n[i][1] + delta[2] * n[i][2];
            for (axis = 0; axis < 3; ++axis) {
                b[3 + edge][axis] = (2.0f * b[i][axis] + b[j][axis]
                        - w * n[i][axis]) / 3.0f;
            }
        }
    }
    /* The interior point: the average of the six edge points, pushed half that
     * distance further from the corner average again. */
    for (axis = 0; axis < 3; ++axis) {
        UINT edge;
        edge_centre[axis] = 0.0f;
        for (edge = 0; edge < 6; ++edge)
            edge_centre[axis] += b[3 + edge][axis];
        edge_centre[axis] /= 6.0f;
        centre[axis] = (b[0][axis] + b[1][axis] + b[2][axis]) / 3.0f;
        b[9][axis] = edge_centre[axis]
                + (edge_centre[axis] - centre[axis]) * 0.5f;
    }
    /* Quadratic normal control points, so shading follows the new surface
     * rather than staying flat across each source triangle. */
    {
        static const UINT pair_i[3] = { 0, 1, 2 };
        static const UINT pair_j[3] = { 1, 2, 0 };
        UINT pair;
        for (pair = 0; pair < 3; ++pair) {
            const UINT i = pair_i[pair];
            const UINT j = pair_j[pair];
            float delta[3];
            float denominator;
            float v;
            for (axis = 0; axis < 3; ++axis)
                delta[axis] = b[j][axis] - b[i][axis];
            denominator = delta[0] * delta[0] + delta[1] * delta[1]
                    + delta[2] * delta[2];
            v = denominator > 1e-12f
                    ? 2.0f * ((delta[0] * (n[i][0] + n[j][0])
                        + delta[1] * (n[i][1] + n[j][1])
                        + delta[2] * (n[i][2] + n[j][2])) / denominator)
                    : 0.0f;
            for (axis = 0; axis < 3; ++axis)
                n[3 + pair][axis] = n[i][axis] + n[j][axis] - v * delta[axis];
            d9_normalize3(n[3 + pair]);
        }
    }

    for (row = 0; row <= level; ++row) {
        for (column = 0; column + row <= level; ++column) {
            const float w = level ? (float)(level - row - column) / (float)level : 1.0f;
            const float u = level ? (float)column / (float)level : 0.0f;
            const float v = level ? (float)row / (float)level : 0.0f;
            float position_out[3];
            float normal_out[3];
            UINT component;
            /* Everything that is not position or normal interpolates
             * linearly -- D3D9 does the same, and a cubic texture coordinate
             * would slide the texture across the surface. */
            for (component = 0; component < components; ++component) {
                scratch[component] = w * corners[component]
                        + u * corners[components + component]
                        + v * corners[2 * components + component];
            }
            for (axis = 0; axis < 3; ++axis) {
                position_out[axis] =
                        b[0][axis] * w * w * w
                        + b[1][axis] * u * u * u
                        + b[2][axis] * v * v * v
                        + b[3][axis] * 3.0f * w * w * u
                        + b[4][axis] * 3.0f * w * u * u
                        + b[5][axis] * 3.0f * u * u * v
                        + b[6][axis] * 3.0f * u * v * v
                        + b[7][axis] * 3.0f * v * v * w
                        + b[8][axis] * 3.0f * v * w * w
                        + b[9][axis] * 6.0f * w * u * v;
                normal_out[axis] =
                        n[0][axis] * w * w + n[1][axis] * u * u
                        + n[2][axis] * v * v
                        + n[3][axis] * 2.0f * w * u
                        + n[4][axis] * 2.0f * u * v
                        + n[5][axis] * 2.0f * v * w;
            }
            d9_normalize3(normal_out);
            /* D3DRS_POSITIONDEGREE and D3DRS_NORMALDEGREE choose the basis per
             * channel. Their defaults are cubic position and *linear* normal,
             * so a title that never sets them gets curved geometry with the
             * normals it already had -- which is what D3D9 gives it, and
             * quietly upgrading the normal would change lighting the app never
             * asked to change. The linear form is already in `scratch` from
             * the interpolation above, so this only overwrites what was
             * asked for. */
            for (axis = 0; axis < 3; ++axis) {
                if (cubic_position)
                    scratch[position + axis] = position_out[axis];
                if (quadratic_normal)
                    scratch[normal + axis] = normal_out[axis];
            }
            d9_patch_encode(format, scratch,
                    out_vertices + cursor * format->stride);
            ++cursor;
        }
    }

    cursor = 0;
    for (row = 0; row < level; ++row) {
        const UINT here = d9_npatch_row_offset(level, row);
        const UINT next = d9_npatch_row_offset(level, row + 1);
        for (column = 0; column + row < level; ++column) {
            out_indices[cursor++] = (uint16_t)(base_vertex + here + column);
            out_indices[cursor++] = (uint16_t)(base_vertex + here + column + 1);
            out_indices[cursor++] = (uint16_t)(base_vertex + next + column);
            if (column + row + 1 < level) {
                out_indices[cursor++] =
                        (uint16_t)(base_vertex + here + column + 1);
                out_indices[cursor++] =
                        (uint16_t)(base_vertex + next + column + 1);
                out_indices[cursor++] = (uint16_t)(base_vertex + next + column);
            }
        }
    }
}

/*
 * Replace one draw's triangles with their tessellated forms.
 *
 * `read_index` yields the source vertex index for the n-th triangle corner,
 * which is what lets the indexed and non-indexed paths share this. Returning
 * D3DERR_INVALIDCALL means "this draw cannot be tessellated"; the callers then
 * fall back to drawing it untessellated, which is the right failure: a mesh
 * missing normals should still appear, just flat.
 */
static UINT d9_npatch_source_index(const void *indices, UINT index_size,
        UINT position, UINT base_vertex)
{
    if (!indices) return base_vertex + position;
    if (index_size == 2)
        return base_vertex + ((const uint16_t *)indices)[position];
    return base_vertex + ((const uint32_t *)indices)[position];
}

static HRESULT d9_draw_npatch(IDirect3DDevice9 *iface, D9Device *device,
        D3DPRIMITIVETYPE primitive_type, UINT primitive_count,
        const void *indices, UINT index_size, UINT base_vertex)
{
    D9PatchFormat format;
    D9VertexBuffer *source = device->streams[0].buffer;
    const UINT stream_stride = device->streams[0].stride;
    UINT level;
    UINT per_triangle_points;
    UINT per_triangle_indices;
    UINT total_points;
    UINT total_indices;
    BYTE *vertices;
    uint16_t *out_indices;
    float *corners;
    float *scratch;
    UINT triangle;
    BOOL cubic_position;
    BOOL quadratic_normal;
    HRESULT result;

    if (primitive_type != D3DPT_TRIANGLELIST
            && primitive_type != D3DPT_TRIANGLESTRIP
            && primitive_type != D3DPT_TRIANGLEFAN)
        return D3DERR_INVALIDCALL;
    if (!source || !stream_stride || device->vertex_shader
            || !d9_patch_format(device, &format)
            || format.position_component < 0 || format.normal_component < 0
            || stream_stride < format.stride)
        return D3DERR_INVALIDCALL;

    cubic_position = (device->render_states[D3DRS_POSITIONDEGREE]
            ? device->render_states[D3DRS_POSITIONDEGREE]
            : (DWORD)D3DDEGREE_CUBIC) != (DWORD)D3DDEGREE_LINEAR;
    quadratic_normal = (device->render_states[D3DRS_NORMALDEGREE]
            ? device->render_states[D3DRS_NORMALDEGREE]
            : (DWORD)D3DDEGREE_LINEAR) != (DWORD)D3DDEGREE_LINEAR;
    /* Both channels linear is the identity: tessellating would multiply the
     * triangle count for a surface identical to the flat one. */
    if (!cubic_position && !quadratic_normal)
        return D3DERR_INVALIDCALL;
    level = (UINT)(device->npatch_segments + 0.5f);
    if (level < 1) level = 1;
    if (level > D9_PATCH_MAX_SEGMENTS) level = D9_PATCH_MAX_SEGMENTS;
    per_triangle_points = d9_npatch_point_count(level);
    per_triangle_indices = level * level * 3u;
    /* uint16 indices bound the whole draw, so a mesh that would overflow them
     * is left untessellated rather than drawn with wrapped indices. */
    if (!multiply_u32(primitive_count, per_triangle_points, &total_points)
            || total_points > 0xFFFFu
            || !multiply_u32(primitive_count, per_triangle_indices,
                    &total_indices))
        return D3DERR_INVALIDCALL;

    vertices = (BYTE *)HeapAlloc(GetProcessHeap(), 0,
            total_points * format.stride);
    out_indices = (uint16_t *)HeapAlloc(GetProcessHeap(), 0,
            total_indices * sizeof(uint16_t));
    corners = (float *)HeapAlloc(GetProcessHeap(), 0,
            3 * format.component_count * sizeof(float));
    scratch = (float *)HeapAlloc(GetProcessHeap(), 0,
            format.component_count * sizeof(float));
    if (!vertices || !out_indices || !corners || !scratch) {
        if (vertices) HeapFree(GetProcessHeap(), 0, vertices);
        if (out_indices) HeapFree(GetProcessHeap(), 0, out_indices);
        if (corners) HeapFree(GetProcessHeap(), 0, corners);
        if (scratch) HeapFree(GetProcessHeap(), 0, scratch);
        return E_OUTOFMEMORY;
    }

    for (triangle = 0; triangle < primitive_count; ++triangle) {
        UINT corner;
        UINT source_index[3];
        /* Strips alternate winding and fans pivot on vertex 0; unrolling here
         * is what lets the tessellator only ever see independent triangles. */
        if (primitive_type == D3DPT_TRIANGLELIST) {
            source_index[0] = triangle * 3;
            source_index[1] = triangle * 3 + 1;
            source_index[2] = triangle * 3 + 2;
        } else if (primitive_type == D3DPT_TRIANGLESTRIP) {
            source_index[0] = triangle;
            source_index[1] = triangle + (triangle & 1 ? 2 : 1);
            source_index[2] = triangle + (triangle & 1 ? 1 : 2);
        } else {
            source_index[0] = 0;
            source_index[1] = triangle + 1;
            source_index[2] = triangle + 2;
        }
        for (corner = 0; corner < 3; ++corner) {
            UINT vertex = d9_npatch_source_index(indices, index_size,
                    source_index[corner], base_vertex);
            UINT byte_offset;
            /* Vertices addressable by this draw start after the stream's
             * OffsetInBytes, not at the start of the buffer. */
            if (!multiply_u32(vertex, stream_stride, &byte_offset)
                    || byte_offset > 0xFFFFFFFFu - device->streams[0].offset
                    || (byte_offset += device->streams[0].offset,
                        byte_offset + format.stride > source->length)) {
                HeapFree(GetProcessHeap(), 0, vertices);
                HeapFree(GetProcessHeap(), 0, out_indices);
                HeapFree(GetProcessHeap(), 0, corners);
                HeapFree(GetProcessHeap(), 0, scratch);
                return D3DERR_INVALIDCALL;
            }
            d9_patch_decode(&format, source->shadow + byte_offset,
                    corners + corner * format.component_count);
        }
        d9_npatch_triangle(&format, level, corners, scratch,
                vertices + triangle * per_triangle_points * format.stride,
                out_indices + triangle * per_triangle_indices,
                triangle * per_triangle_points, cubic_position,
                quadratic_normal);
    }

    result = draw_indexed_up(device_from_iface(iface), D3DPT_TRIANGLELIST, 0,
            total_points, total_indices / 3u, out_indices, D3DFMT_INDEX16,
            vertices, format.stride, TRUE, FALSE);
    HeapFree(GetProcessHeap(), 0, vertices);
    HeapFree(GetProcessHeap(), 0, out_indices);
    HeapFree(GetProcessHeap(), 0, corners);
    HeapFree(GetProcessHeap(), 0, scratch);
    return result;
}

static HRESULT WINAPI device_draw_rect_patch(IDirect3DDevice9 *iface,
        UINT handle, const float *segments, const D3DRECTPATCH_INFO *info)
{
    D9Device *device = device_from_iface(iface);
    D9PatchFormat format;
    D9VertexBuffer *source;
    UINT source_stride;
    UINT segment_count;
    UINT grid;
    UINT degree_u;
    UINT degree_v;
    BYTE *vertices;
    uint16_t *indices;
    float *control;
    float *accumulator;
    UINT row;
    UINT column;
    UINT index;
    HRESULT result;

    TRACE_MARK_ENTER("Device.DrawRectPatch");
    /* A cached patch handle would let a later call redraw with no info; this
     * profile advertises D3DDEVCAPS_RTPATCHHANDLEZERO only, so every call has
     * to carry its own description. */
    if (!info) {
        HOSTLOG_REFUSED("DrawRectPatch refused: this profile advertises "
                "D3DDEVCAPS_RTPATCHHANDLEZERO only, so a cached patch handle "
                "cannot be redrawn without its D3DRECTPATCH_INFO");
        TRACE_MARK_EXIT("Device.DrawRectPatch", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    (void)handle;
    if (!d9_patch_basis_supported(info->Basis, info->Degree, "DrawRectPatch")) {
        TRACE_MARK_EXIT("Device.DrawRectPatch", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    degree_u = (UINT)info->Degree;
    degree_v = (UINT)info->Degree;
    if (info->Width != degree_u + 1 || info->Height != degree_v + 1) {
        HOSTLOG_REFUSED("DrawRectPatch refused: %lux%lu control points is more "
                "than the single patch this implementation evaluates "
                "(%lux%lu for degree %lu)", info->Width, info->Height,
                degree_u + 1, degree_v + 1, (DWORD)info->Degree);
        TRACE_MARK_EXIT("Device.DrawRectPatch", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    source = device->streams[0].buffer;
    source_stride = device->streams[0].stride;
    if (!source || !source_stride || device->vertex_shader
            || !d9_patch_format(device, &format)
            || source_stride < format.stride) {
        HOSTLOG_REFUSED("DrawRectPatch refused: stream 0 must hold the control "
                "points under a fixed-function declaration this path can "
                "decode");
        TRACE_MARK_EXIT("Device.DrawRectPatch", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    segment_count = d9_patch_segments(segments, 4);
    grid = segment_count + 1;

    {
        UINT vertex_bytes;
        UINT index_bytes;
        UINT control_bytes;
        if (!multiply_u32(grid * grid, format.stride, &vertex_bytes)
                || !multiply_u32(segment_count * segment_count * 6u,
                        sizeof(uint16_t), &index_bytes)
                || !multiply_u32(info->Width * info->Height,
                        format.component_count * sizeof(float),
                        &control_bytes)) {
            TRACE_MARK_EXIT("Device.DrawRectPatch", D3DERR_INVALIDCALL, NULL);
            return D3DERR_INVALIDCALL;
        }
        vertices = (BYTE *)HeapAlloc(GetProcessHeap(), 0, vertex_bytes);
        indices = (uint16_t *)HeapAlloc(GetProcessHeap(), 0, index_bytes);
        control = (float *)HeapAlloc(GetProcessHeap(), 0, control_bytes);
        accumulator = (float *)HeapAlloc(GetProcessHeap(), 0,
                format.component_count * sizeof(float));
        if (!vertices || !indices || !control || !accumulator) {
            if (vertices) HeapFree(GetProcessHeap(), 0, vertices);
            if (indices) HeapFree(GetProcessHeap(), 0, indices);
            if (control) HeapFree(GetProcessHeap(), 0, control);
            if (accumulator) HeapFree(GetProcessHeap(), 0, accumulator);
            TRACE_MARK_EXIT("Device.DrawRectPatch", E_OUTOFMEMORY, NULL);
            return E_OUTOFMEMORY;
        }
    }

    /* Decode the control net once. `Stride` is in vertices per row of the
     * source buffer, which is how D3D9 lets a patch be a window onto a larger
     * grid of control points. */
    for (row = 0; row < info->Height; ++row) {
        for (column = 0; column < info->Width; ++column) {
            UINT vertex = (info->StartVertexOffsetHeight + row) * info->Stride
                    + info->StartVertexOffsetWidth + column;
            UINT byte_offset;
            if (!multiply_u32(vertex, source_stride, &byte_offset)
                    || byte_offset > 0xFFFFFFFFu - device->streams[0].offset
                    || (byte_offset += device->streams[0].offset,
                        byte_offset + format.stride > source->length)) {
                HeapFree(GetProcessHeap(), 0, vertices);
                HeapFree(GetProcessHeap(), 0, indices);
                HeapFree(GetProcessHeap(), 0, control);
                HeapFree(GetProcessHeap(), 0, accumulator);
                TRACE_MARK_EXIT("Device.DrawRectPatch", D3DERR_INVALIDCALL,
                        NULL);
                return D3DERR_INVALIDCALL;
            }
            d9_patch_decode(&format, source->shadow + byte_offset,
                    control + (row * info->Width + column)
                            * format.component_count);
        }
    }

    for (row = 0; row < grid; ++row) {
        float basis_v[4];
        float v = segment_count ? (float)row / (float)segment_count : 0.0f;
        d9_bernstein(degree_v, v, basis_v);
        for (column = 0; column < grid; ++column) {
            float basis_u[4];
            float u = segment_count
                    ? (float)column / (float)segment_count : 0.0f;
            UINT i;
            UINT j;
            d9_bernstein(degree_u, u, basis_u);
            for (i = 0; i < format.component_count; ++i)
                accumulator[i] = 0.0f;
            for (j = 0; j <= degree_v; ++j) {
                for (i = 0; i <= degree_u; ++i) {
                    float weight = basis_u[i] * basis_v[j];
                    const float *point = control
                            + (j * info->Width + i) * format.component_count;
                    UINT c;
                    for (c = 0; c < format.component_count; ++c)
                        accumulator[c] += weight * point[c];
                }
            }
            d9_patch_encode(&format, accumulator,
                    vertices + (row * grid + column) * format.stride);
        }
    }

    d9_patch_grid_indices(segment_count, indices);
    index = segment_count * segment_count * 2u;
    result = draw_indexed_up(device_from_iface(iface), D3DPT_TRIANGLELIST, 0,
            grid * grid, index, indices, D3DFMT_INDEX16, vertices,
            format.stride, FALSE, FALSE);

    HeapFree(GetProcessHeap(), 0, vertices);
    HeapFree(GetProcessHeap(), 0, indices);
    HeapFree(GetProcessHeap(), 0, control);
    HeapFree(GetProcessHeap(), 0, accumulator);
    TRACE("%s DrawRectPatch device=%08lX segments=%lu triangles=%lu",
            SUCCEEDED(result) ? "OK" : "FAIL", device->handle, segment_count,
            index);
    TRACE_MARK_EXIT("Device.DrawRectPatch", result, NULL);
    return result;
}

/*
 * A triangular Bezier patch. Its control net is the triangular array of
 * (Degree+1)(Degree+2)/2 points D3D9 lays out row by row, and the surface is
 * the Bernstein sum over barycentric coordinates.
 *
 * The result is still evaluated onto a square grid and drawn as a triangle
 * list, with the barycentric pair clamped inside the triangle: that keeps one
 * index-buffer builder for both patch kinds, at the cost of the degenerate
 * triangles along the folded edge, which have zero area and rasterise to
 * nothing.
 */
static HRESULT WINAPI device_draw_tri_patch(IDirect3DDevice9 *iface,
        UINT handle, const float *segments, const D3DTRIPATCH_INFO *info)
{
    D9Device *device = device_from_iface(iface);
    D9PatchFormat format;
    D9VertexBuffer *source;
    UINT source_stride;
    UINT segment_count;
    UINT grid;
    UINT degree;
    UINT expected_points;
    BYTE *vertices;
    uint16_t *indices;
    float *control;
    float *accumulator;
    UINT row;
    UINT column;
    UINT triangles;
    HRESULT result;

    TRACE_MARK_ENTER("Device.DrawTriPatch");
    if (!info) {
        HOSTLOG_REFUSED("DrawTriPatch refused: this profile advertises "
                "D3DDEVCAPS_RTPATCHHANDLEZERO only, so a cached patch handle "
                "cannot be redrawn without its D3DTRIPATCH_INFO");
        TRACE_MARK_EXIT("Device.DrawTriPatch", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    (void)handle;
    if (!d9_patch_basis_supported(info->Basis, info->Degree, "DrawTriPatch")) {
        TRACE_MARK_EXIT("Device.DrawTriPatch", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    degree = (UINT)info->Degree;
    expected_points = (degree + 1) * (degree + 2) / 2;
    if (info->NumVertices != expected_points) {
        HOSTLOG_REFUSED("DrawTriPatch refused: degree %lu needs %lu control "
                "points and %lu were given", (DWORD)info->Degree,
                expected_points, info->NumVertices);
        TRACE_MARK_EXIT("Device.DrawTriPatch", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    source = device->streams[0].buffer;
    source_stride = device->streams[0].stride;
    if (!source || !source_stride || device->vertex_shader
            || !d9_patch_format(device, &format)
            || source_stride < format.stride) {
        HOSTLOG_REFUSED("DrawTriPatch refused: stream 0 must hold the control "
                "points under a fixed-function declaration this path can "
                "decode");
        TRACE_MARK_EXIT("Device.DrawTriPatch", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    segment_count = d9_patch_segments(segments, 3);
    grid = segment_count + 1;

    {
        UINT vertex_bytes;
        UINT index_bytes;
        UINT control_bytes;
        if (!multiply_u32(grid * grid, format.stride, &vertex_bytes)
                || !multiply_u32(segment_count * segment_count * 6u,
                        sizeof(uint16_t), &index_bytes)
                || !multiply_u32(expected_points,
                        format.component_count * sizeof(float),
                        &control_bytes)) {
            TRACE_MARK_EXIT("Device.DrawTriPatch", D3DERR_INVALIDCALL, NULL);
            return D3DERR_INVALIDCALL;
        }
        vertices = (BYTE *)HeapAlloc(GetProcessHeap(), 0, vertex_bytes);
        indices = (uint16_t *)HeapAlloc(GetProcessHeap(), 0, index_bytes);
        control = (float *)HeapAlloc(GetProcessHeap(), 0, control_bytes);
        accumulator = (float *)HeapAlloc(GetProcessHeap(), 0,
                format.component_count * sizeof(float));
        if (!vertices || !indices || !control || !accumulator) {
            if (vertices) HeapFree(GetProcessHeap(), 0, vertices);
            if (indices) HeapFree(GetProcessHeap(), 0, indices);
            if (control) HeapFree(GetProcessHeap(), 0, control);
            if (accumulator) HeapFree(GetProcessHeap(), 0, accumulator);
            TRACE_MARK_EXIT("Device.DrawTriPatch", E_OUTOFMEMORY, NULL);
            return E_OUTOFMEMORY;
        }
    }

    for (row = 0; row < expected_points; ++row) {
        UINT byte_offset;
        if (!multiply_u32(info->StartVertexOffset + row, source_stride,
                    &byte_offset)
                || byte_offset > 0xFFFFFFFFu - device->streams[0].offset
                || (byte_offset += device->streams[0].offset,
                    byte_offset + format.stride > source->length)) {
            HeapFree(GetProcessHeap(), 0, vertices);
            HeapFree(GetProcessHeap(), 0, indices);
            HeapFree(GetProcessHeap(), 0, control);
            HeapFree(GetProcessHeap(), 0, accumulator);
            TRACE_MARK_EXIT("Device.DrawTriPatch", D3DERR_INVALIDCALL, NULL);
            return D3DERR_INVALIDCALL;
        }
        d9_patch_decode(&format, source->shadow + byte_offset,
                control + row * format.component_count);
    }

    for (row = 0; row < grid; ++row) {
        float v = segment_count ? (float)row / (float)segment_count : 0.0f;
        for (column = 0; column < grid; ++column) {
            float u = segment_count
                    ? (float)column / (float)segment_count : 0.0f;
            float w;
            UINT i;
            UINT j;
            UINT point = 0;
            /* Fold the square onto the triangle: everything past the diagonal
             * collapses onto it, producing degenerate triangles that rasterise
             * to nothing rather than geometry outside the patch. */
            if (u + v > 1.0f) {
                float scale = 1.0f / (u + v);
                u *= scale;
                v *= scale;
            }
            w = 1.0f - u - v;
            if (w < 0.0f) w = 0.0f;
            for (i = 0; i < format.component_count; ++i)
                accumulator[i] = 0.0f;
            /* Bernstein over barycentric coordinates, walked in the same row
             * order D3D9 lays the control net out in: i counts along u, j
             * along v, and the remainder is w. */
            for (j = 0; j <= degree; ++j) {
                for (i = 0; i + j <= degree; ++i) {
                    UINT k = degree - i - j;
                    float weight = d9_trinomial(degree, i, j, k);
                    UINT c;
                    const float *source_point;
                    weight *= d9_powi(u, i) * d9_powi(v, j) * d9_powi(w, k);
                    source_point = control + point * format.component_count;
                    for (c = 0; c < format.component_count; ++c)
                        accumulator[c] += weight * source_point[c];
                    ++point;
                }
            }
            d9_patch_encode(&format, accumulator,
                    vertices + (row * grid + column) * format.stride);
        }
    }

    d9_patch_grid_indices(segment_count, indices);
    triangles = segment_count * segment_count * 2u;
    result = draw_indexed_up(device_from_iface(iface), D3DPT_TRIANGLELIST, 0,
            grid * grid, triangles, indices, D3DFMT_INDEX16, vertices,
            format.stride, FALSE, FALSE);

    HeapFree(GetProcessHeap(), 0, vertices);
    HeapFree(GetProcessHeap(), 0, indices);
    HeapFree(GetProcessHeap(), 0, control);
    HeapFree(GetProcessHeap(), 0, accumulator);
    TRACE("%s DrawTriPatch device=%08lX segments=%lu triangles=%lu",
            SUCCEEDED(result) ? "OK" : "FAIL", device->handle, segment_count,
            triangles);
    TRACE_MARK_EXIT("Device.DrawTriPatch", result, NULL);
    return result;
}

/*
 * Nothing is cached, so there is nothing to delete. D3D9 documents
 * DeletePatch as returning D3DERR_INVALIDCALL for a handle that was never
 * cached, which is every handle here -- and this profile advertises
 * D3DDEVCAPS_RTPATCHHANDLEZERO rather than the caching form, so an app that
 * reads caps never gets here with an expectation to disappoint.
 */
static HRESULT WINAPI device_delete_patch(IDirect3DDevice9 *iface, UINT handle)
{ (void)iface; (void)handle; return TRACE_REFUSE(D3DERR_INVALIDCALL); }

/* ---- M3: render targets, cube textures, scissor rect, queries ---- */

/*
 * A render target reaches the host as an ordinary CREATE_TEXTURE_2D carrying
 * D3DUSAGE_RENDERTARGET (or D3DUSAGE_DEPTHSTENCIL), not as its own resource
 * kind. That is not a shortcut: in D3D9 a render target *is* a surface of a
 * texture as often as it is a standalone surface, and the host needs a GPU
 * texture either way, so giving the standalone form its own opcode would only
 * mean two host code paths building the same object.
 *
 * CreateRenderTarget therefore builds a private D9Texture nobody else can see
 * and hands back its level-0 surface, then drops its own reference so the
 * surface's reference is the only one keeping it alive -- releasing the surface
 * releases the texture, which is exactly the app-visible lifetime D3D9 gives a
 * standalone render target.
 */
static HRESULT create_target_texture(D9Device *device, UINT width, UINT height,
        D3DFORMAT format, DWORD usage, D3DMULTISAMPLE_TYPE multisample,
        DWORD multisample_quality, IDirect3DSurface9 **surface_out)
{
    D9Texture *texture;
    D9Surface *surface;

    if (!surface_out)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *surface_out = NULL;
    if (!width || !height || width > 8192 || height > 8192)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);

    texture = (D9Texture *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*texture));
    if (!texture)
        return TRACE_REFUSE(E_OUTOFMEMORY);
    texture->levels = (D9TextureLevel *)HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY, sizeof(*texture->levels));
    if (!texture->levels) {
        HeapFree(GetProcessHeap(), 0, texture);
        return TRACE_REFUSE(E_OUTOFMEMORY);
    }
    texture->iface.lpVtbl = &g_texture_vtbl;
    texture->refcount = 1;
    texture->device = device;
    texture->private_back_buffer = TRUE;
    texture->handle = allocate_handle();
    texture->width = width;
    texture->height = height;
    texture->level_count = 1;
    texture->usage = usage;
    texture->format = format;
    texture->pool = D3DPOOL_DEFAULT;
    texture->multisample = multisample;
    texture->multisample_quality = multisample_quality;
    texture->levels[0].width = width;
    texture->levels[0].height = height;
    if (!(usage & D3DUSAGE_DEPTHSTENCIL)
            && !texture_level_layout(format, width, height,
            &texture->levels[0].row_pitch, &texture->levels[0].row_count,
            &texture->levels[0].byte_count)) {
        HeapFree(GetProcessHeap(), 0, texture->levels);
        HeapFree(GetProcessHeap(), 0, texture);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    /*
     * A render target's shadow storage stays lazy: Clear/ColorFill may make its
     * contents fully known for M4 readback, and a GPU draw invalidates that
     * knowledge, so there is nothing to hold until one of those happens.
     *
     * usage == 0 is the offscreen-plain form, and that one is different: D3D9
     * makes a D3DPOOL_DEFAULT off-screen plain surface lockable unconditionally
     * -- it is the only default-pool surface an app may write to directly, and
     * apps rely on it (3DMark06's SM3.0 particle test locks a 640x640
     * A32B32G32R32F offscreen plain surface to seed its particle state). Lazy
     * storage would make texture_lock_level() refuse the lock for want of a
     * shadow, so the offscreen-plain form gets its level 0 up front, exactly
     * like a lockable CreateTexture level. The zero fill matches the host
     * texture's own zero-initialised contents, so the mirror starts valid.
     */
    if (!usage) {
        texture->levels[0].shadow = (BYTE *)HeapAlloc(GetProcessHeap(),
                HEAP_ZERO_MEMORY, texture->levels[0].byte_count);
        if (!texture->levels[0].shadow) {
            HeapFree(GetProcessHeap(), 0, texture->levels);
            HeapFree(GetProcessHeap(), 0, texture);
            return TRACE_REFUSE(E_OUTOFMEMORY);
        }
        texture->levels[0].shadow_valid = TRUE;
    }
    if (!emit_texture_create(device, texture)) {
        if (texture->levels[0].shadow)
            HeapFree(GetProcessHeap(), 0, texture->levels[0].shadow);
        HeapFree(GetProcessHeap(), 0, texture->levels);
        HeapFree(GetProcessHeap(), 0, texture);
        return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
    }
    texture->next_device_resource = device->texture_resources;
    device->texture_resources = texture;

    surface = (D9Surface *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*surface));
    if (!surface) {
        IDirect3DTexture9_Release(&texture->iface);
        return TRACE_REFUSE(E_OUTOFMEMORY);
    }
    surface->iface.lpVtbl = &g_surface_vtbl;
    surface->refcount = 1;
    surface->device = device;
    surface->texture = texture;
    surface->level = 0;
    surface->width = width;
    surface->height = height;
    surface->format = format;
    surface->pool = D3DPOOL_DEFAULT;
    surface->multisample = multisample;
    surface->multisample_quality = multisample_quality;
    device_child_add_ref(device);
    /* The surface took over the creation reference rather than adding a second
     * one, so the texture disappears with the surface. */
    *surface_out = &surface->iface;
    TRACE_REGISTER_RANGE("SURFACE_TARGET", texture->handle, surface, surface,
            sizeof(*surface));
    TRACE("SURFACE CREATE kind=target object=%08lX vtbl=%08lX ref=%ld "
            "device=%08lX texture=%08lX texture_handle=%08lX "
            "swapchain=00000000 shadow=%08lX level=0 size=%lux%lu "
            "format=%08lX usage=%08lX",
            (DWORD)(uintptr_t)*surface_out,
            (DWORD)(uintptr_t)surface->iface.lpVtbl, surface->refcount,
            (DWORD)(uintptr_t)&device->iface,
            (DWORD)(uintptr_t)&texture->iface, texture->handle,
            (DWORD)(uintptr_t)texture->levels[0].shadow,
            surface->width, surface->height, (DWORD)surface->format, usage);
    return D3D_OK;
}

static HRESULT WINAPI device_create_render_target(IDirect3DDevice9 *iface,
        UINT width, UINT height, D3DFORMAT format,
        D3DMULTISAMPLE_TYPE multisample, DWORD multisample_quality,
        WINBOOL lockable, IDirect3DSurface9 **surface_out, HANDLE *shared)
{
    (void)shared;
    if (!supported_multisample(format, multisample)
            || multisample_quality != 0
            || (lockable && multisample != D3DMULTISAMPLE_NONE)) {
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    if (!supported_render_target_format(format))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    HRESULT hr = create_target_texture(device_from_iface(iface), width, height, format,
            D3DUSAGE_RENDERTARGET, multisample, multisample_quality, surface_out);
    if (SUCCEEDED(hr)) surface_from_iface(*surface_out)->lockable_render_target = !!lockable;
    return hr;
}

static HRESULT WINAPI device_create_depth_stencil_surface(
        IDirect3DDevice9 *iface, UINT width, UINT height, D3DFORMAT format,
        D3DMULTISAMPLE_TYPE multisample, DWORD multisample_quality,
        WINBOOL discard, IDirect3DSurface9 **surface_out, HANDLE *shared)
{
    (void)shared;
    (void)discard;
    if (!supported_multisample(format, multisample)
            || multisample_quality != 0)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!supported_depth_stencil_format(format))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    return create_target_texture(device_from_iface(iface), width, height, format,
            D3DUSAGE_DEPTHSTENCIL, multisample, multisample_quality,
            surface_out);
}

/* Returns the surface's backing texture handle plus the level within it, or
 * FALSE for a surface that names no host resource (the back buffer, or a
 * standalone CPU surface). Handle 0 is how the wire says "the back buffer",
 * which is why the caller has to distinguish "no handle" from "not resolvable". */
static BOOL surface_target_handle(D9Surface *surface, uint32_t *handle_out,
        uint32_t *level_out, uint32_t *face_out)
{
    if (!surface)
        return FALSE;
    if (surface->shadow)
        return FALSE; /* CPU-only offscreen surface: never a GPU target */
    if (surface->cube_texture) {
        *handle_out = surface->cube_texture->handle;
        *level_out = surface->level;
        *face_out = surface->cube_face;
        return TRUE;
    }
    *handle_out = surface->texture ? surface->texture->handle : 0u;
    *level_out = surface->texture ? surface->level : 0u;
    *face_out = 0u;
    return TRUE;
}

static HRESULT WINAPI device_set_render_target(IDirect3DDevice9 *iface,
        DWORD index, IDirect3DSurface9 *surface_iface)
{
    D9Device *device = device_from_iface(iface);
    D9Surface *surface = surface_iface ? surface_from_iface(surface_iface) : NULL;
    D9Surface *old_surface;
    D9WGSetRenderTarget command;
    uint32_t handle = 0;
    uint32_t level = 0;
    uint32_t face = 0;

    if (index >= D9_MAX_RENDER_TARGETS)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    /* D3D9 forbids unbinding slot 0: something always has to receive colour. */
    if (!surface_iface && index == 0)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    /* A host handle is valid only in the creating device's target state.
     * Reject before retaining/replacing bindings or changing viewport state. */
    if (surface_iface && (surface_iface->lpVtbl != &g_surface_vtbl
            || surface->device != device))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (surface_iface && !surface_target_handle(surface, &handle, &level, &face))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (surface_iface && surface->texture
            && !(surface->texture->usage & D3DUSAGE_RENDERTARGET))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (surface_iface && surface->cube_texture
            && !(surface->cube_texture->usage & D3DUSAGE_RENDERTARGET))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);

    old_surface = device->render_target_surfaces[index];
    TRACE("CALL SetRenderTarget index=%lu old=%08lX new=%08lX same=%lu "
            "old_refs=%ld new_refs=%ld", index,
            (DWORD)(uintptr_t)old_surface, (DWORD)(uintptr_t)surface,
            old_surface == surface,
            old_surface ? InterlockedCompareExchange(&old_surface->refcount,
                    0, 0) : 0,
            surface ? InterlockedCompareExchange(&surface->refcount, 0, 0)
                    : 0);
    /* Take the new reference before dropping the old one.  Apart from being
     * the usual COM replacement discipline, this keeps same-object rebinding
     * safe even when the caller is holding only a borrowed alias. */
    if (surface)
        surface_binding_add_ref(surface);
    device->render_target_surfaces[index] = surface;
    if (old_surface)
        surface_binding_release(old_surface);

    command.device_handle = device->handle;
    command.target_index = index;
    command.color_texture_handle = handle;
    command.color_level = level;
    command.color_face = face;
    if (!emit_command(D9WG_OP_SET_RENDER_TARGET, &command, sizeof(command)))
        return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);

    /* D3D9 resets the viewport to the full new target on every successful
     * SetRenderTarget(0). Leaving the old viewport in place is a classic
     * source of "the render-to-texture pass only fills a corner". */
    if (index == 0 && surface) {
        D3DVIEWPORT9 viewport;
        RECT scissor;
        HRESULT hr;
        viewport.X = 0;
        viewport.Y = 0;
        viewport.Width = surface->width;
        viewport.Height = surface->height;
        viewport.MinZ = 0.0f;
        viewport.MaxZ = 1.0f;
        /* ... and the scissor rect, or a render-to-texture pass is cut by the
         * previous target's rect while the scissor test is on (review S2-29;
         * wined3d's device.c resets both) */
        SetRect(&scissor, 0, 0, (int)surface->width, (int)surface->height);
        hr = IDirect3DDevice9_SetScissorRect(iface, &scissor);
        if (FAILED(hr)) return hr;
        return IDirect3DDevice9_SetViewport(iface, &viewport);
    }
    return D3D_OK;
}

static HRESULT WINAPI device_get_render_target(IDirect3DDevice9 *iface,
        DWORD index, IDirect3DSurface9 **surface_out)
{
    D9Device *device = device_from_iface(iface);

    if (!surface_out || index >= D9_MAX_RENDER_TARGETS)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *surface_out = NULL;
    if (device->render_target_surfaces[index]) {
        *surface_out = &device->render_target_surfaces[index]->iface;
        IDirect3DSurface9_AddRef(*surface_out);
        return D3D_OK;
    }
    /* Nothing explicitly bound: slot 0 is the implicit back buffer, and the
     * other slots are genuinely empty (D3DERR_NOTFOUND, as D3D9 reports). */
    if (index != 0)
        return TRACE_REFUSE(D3DERR_NOTFOUND);
    return IDirect3DDevice9_GetBackBuffer(iface, 0, 0,
            D3DBACKBUFFER_TYPE_MONO, surface_out);
}

static HRESULT WINAPI device_set_depth_stencil_surface(IDirect3DDevice9 *iface,
        IDirect3DSurface9 *surface_iface)
{
    D9Device *device = device_from_iface(iface);
    D9Surface *surface = surface_iface ? surface_from_iface(surface_iface) : NULL;
    D9Surface *old_surface;
    D9WGSetDepthStencilSurfaceLevel command;
    uint32_t handle = 0;
    uint32_t level = 0;

    if (surface && surface->auto_depth_stencil) {
        handle = D9WG_AUTO_DEPTH_STENCIL_HANDLE;
    } else if (surface_iface) {
        uint32_t face = 0;
        if (!surface_target_handle(surface, &handle, &level, &face)
                || face || !surface->texture
                || !(surface->texture->usage & D3DUSAGE_DEPTHSTENCIL))
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    old_surface = device->depth_stencil_surface;
    TRACE("CALL SetDepthStencilSurface old=%08lX new=%08lX same=%lu "
            "old_refs=%ld new_refs=%ld", (DWORD)(uintptr_t)old_surface,
            (DWORD)(uintptr_t)surface, old_surface == surface,
            old_surface ? InterlockedCompareExchange(&old_surface->refcount,
                    0, 0) : 0,
            surface ? InterlockedCompareExchange(&surface->refcount, 0, 0)
                    : 0);
    if (surface)
        surface_binding_add_ref(surface);
    device->depth_stencil_surface = surface;
    if (old_surface)
        surface_binding_release(old_surface);
    command.device_handle = device->handle;
    command.depth_texture_handle = handle;
    command.depth_level = level;
    command.width = surface ? surface->width : 0u;
    command.height = surface ? surface->height : 0u;
    device->depth_stencil_unbound = surface ? FALSE : TRUE;
    return emit_command(D9WG_OP_SET_DEPTH_STENCIL_SURFACE_LEVEL, &command,
            sizeof(command)) ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_get_depth_stencil_surface(IDirect3DDevice9 *iface,
        IDirect3DSurface9 **surface_out)
{
    D9Device *device = device_from_iface(iface);

    if (!surface_out)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *surface_out = NULL;
    if (device->depth_stencil_surface) {
        *surface_out = &device->depth_stencil_surface->iface;
        IDirect3DSurface9_AddRef(*surface_out);
        return D3D_OK;
    }
    if (device->depth_stencil_unbound || !device->present.EnableAutoDepthStencil)
        return TRACE_REFUSE(D3DERR_NOTFOUND);
    /* A handle onto the implicit auto depth-stencil. Refusing here used to look
     * harmless -- nothing reads a depth surface's pixels -- but it breaks the
     * standard render-to-texture sequence: an app that cannot *get* the current
     * depth surface cannot put it back afterwards, and then every draw after the
     * first RTT pass runs with no depth buffer. */
    {
        /* Cached for the device's lifetime, for the same reason as the back
         * buffer above: this pointer is what the app hands back to
         * SetDepthStencilSurface, possibly long after releasing it. */
        D9Surface *surface = device->implicit_depth_stencil;
        if (!surface) {
            surface = (D9Surface *)HeapAlloc(GetProcessHeap(),
                    HEAP_ZERO_MEMORY, sizeof(*surface));
            if (!surface)
                return TRACE_REFUSE(E_OUTOFMEMORY);
            surface->iface.lpVtbl = &g_surface_vtbl;
            surface->device = device;
            surface->auto_depth_stencil = TRUE;
            surface->width = device->present.BackBufferWidth;
            surface->height = device->present.BackBufferHeight;
            surface->format = device->present.AutoDepthStencilFormat;
            surface->multisample = device->present.MultiSampleType;
            surface->multisample_quality =
                    device->present.MultiSampleQuality;
            device->implicit_depth_stencil = surface;
            TRACE_REGISTER_RANGE("SURFACE_AUTO_DEPTH", 0, surface, surface,
                    sizeof(*surface));
            TRACE("SURFACE CREATE kind=auto_depth object=%08lX vtbl=%08lX "
                    "ref=%ld device=%08lX texture=00000000 swapchain=00000000 "
                    "shadow=00000000 level=0 size=%lux%lu format=%08lX",
                    (DWORD)(uintptr_t)&surface->iface,
                    (DWORD)(uintptr_t)surface->iface.lpVtbl, surface->refcount,
                    (DWORD)(uintptr_t)&device->iface, surface->width,
                    surface->height, (DWORD)surface->format);
        }
        *surface_out = &surface->iface;
        IDirect3DSurface9_AddRef(*surface_out);
    }
    return D3D_OK;
}

static void normalize_rect(const RECT *source, int width, int height, RECT *out)
{
    if (source) {
        *out = *source;
    } else {
        SetRect(out, 0, 0, width, height);
    }
}

static HRESULT WINAPI device_stretch_rect(IDirect3DDevice9 *iface,
        IDirect3DSurface9 *source_iface, const RECT *source_rect,
        IDirect3DSurface9 *destination_iface, const RECT *destination_rect,
        D3DTEXTUREFILTERTYPE filter)
{
    D9Device *device = device_from_iface(iface);
    D9Surface *source = source_iface ? surface_from_iface(source_iface) : NULL;
    D9Surface *destination = destination_iface
            ? surface_from_iface(destination_iface) : NULL;
    D9WGStretchRect command;
    RECT source_area;
    RECT destination_area;

    if (!source || !destination) {
        HOSTLOG_FAILED("StretchRect refused: %s surface is NULL",
                source ? "destination" : "source");
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    ZeroMemory(&command, sizeof(command));
    if (!surface_target_handle(source, &command.source_texture_handle,
                &command.source_level, &command.source_face)
            || !surface_target_handle(destination,
                &command.destination_texture_handle,
                &command.destination_level, &command.destination_face)) {
        /*
         * The only surface this rejects is a CPU-only offscreen plain one,
         * which has no GPU resource to blit with. Naming both operands matters
         * because the fix depends on the direction: a CPU *source* is an upload
         * of bytes we already hold, a CPU *destination* needs a readback, and
         * CPU-to-CPU is a memcpy that never has to reach the host at all.
         * Without this line the refusal is just "invalid call" again.
         */
        HOSTLOG_FAILED("StretchRect refused: no GPU resource behind %s "
                "(source %lux%lu fmt=%08lX pool=%lu cpu_only=%lu, "
                "destination %lux%lu fmt=%08lX pool=%lu cpu_only=%lu)",
                source->shadow
                    ? (destination->shadow ? "either surface" : "the source")
                    : "the destination",
                source->width, source->height, (DWORD)source->format,
                (DWORD)source->pool, source->shadow ? 1ul : 0ul,
                destination->width, destination->height,
                (DWORD)destination->format, (DWORD)destination->pool,
                destination->shadow ? 1ul : 0ul);
        TRACE("FAIL StretchRect source=%08lX shadow=%08lX pool=%lu "
                "destination=%08lX shadow=%08lX pool=%lu",
                (DWORD)(uintptr_t)source, (DWORD)(uintptr_t)source->shadow,
                (DWORD)source->pool, (DWORD)(uintptr_t)destination,
                (DWORD)(uintptr_t)destination->shadow,
                (DWORD)destination->pool);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    command.device_handle = device->handle;
    normalize_rect(source_rect, (int)source->width, (int)source->height,
            &source_area);
    command.source_left = source_area.left;
    command.source_top = source_area.top;
    command.source_right = source_area.right;
    command.source_bottom = source_area.bottom;
    normalize_rect(destination_rect, (int)destination->width,
            (int)destination->height, &destination_area);
    command.destination_left = destination_area.left;
    command.destination_top = destination_area.top;
    command.destination_right = destination_area.right;
    command.destination_bottom = destination_area.bottom;
    command.filter_point = (filter == D3DTEXF_NONE || filter == D3DTEXF_POINT)
            ? 1u : 0u;
    if (!emit_command(D9WG_OP_STRETCH_RECT, &command, sizeof(command)))
        return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
    mirror_target_copy(source, &source_area, destination, &destination_area);
    return D3D_OK;
}

static HRESULT WINAPI device_color_fill(IDirect3DDevice9 *iface,
        IDirect3DSurface9 *surface_iface, const RECT *rect, D3DCOLOR color)
{
    D9Device *device = device_from_iface(iface);
    D9Surface *surface = surface_iface ? surface_from_iface(surface_iface) : NULL;
    D9WGColorFill command;
    RECT area;

    if (!surface)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    ZeroMemory(&command, sizeof(command));
    if (!surface_target_handle(surface, &command.texture_handle, &command.level,
            &command.face))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    command.device_handle = device->handle;
    command.color = color;
    normalize_rect(rect, (int)surface->width, (int)surface->height, &area);
    command.left = area.left;
    command.top = area.top;
    command.right = area.right;
    command.bottom = area.bottom;
    if (!emit_command(D9WG_OP_COLOR_FILL, &command, sizeof(command)))
        return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
    mirror_target_color_fill(surface, &area, color);
    return D3D_OK;
}

static HRESULT WINAPI device_set_scissor_rect(IDirect3DDevice9 *iface,
        const RECT *rect)
{
    D9Device *device = device_from_iface(iface);
    D9WGSetScissorRect command;

    if (!rect)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (rect->left < 0 || rect->top < 0 || rect->right < rect->left
            || rect->bottom < rect->top)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (state_block_record_scissor(device, rect)) return D3D_OK;
    device->scissor_rect = *rect;
    device->scissor_set = TRUE;
    command.device_handle = device->handle;
    command.left = rect->left;
    command.top = rect->top;
    command.right = rect->right;
    command.bottom = rect->bottom;
    return emit_command(D9WG_OP_SET_SCISSOR_RECT, &command, sizeof(command))
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_get_scissor_rect(IDirect3DDevice9 *iface,
        RECT *rect)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetScissorRect");
    D9Device *device = device_from_iface(iface);

    if (!rect)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (device->scissor_set) {
        *rect = device->scissor_rect;
    } else {
        /* D3D9's default scissor rect is the whole render target. */
        SetRect(rect, 0, 0, (int)device->present.BackBufferWidth,
                (int)device->present.BackBufferHeight);
    }
    return D3D_OK;
}

/* ---- IDirect3DQuery9 ----
 * Query results use one persistent 16-byte slot in the DMA response tail.
 * Issue emits real host commands; the WebGPU executor writes the completed
 * value into that slot after resolving its GPUQuerySet / queue fence. */
typedef struct D9Query {
    IDirect3DQuery9 iface;
    LONG refcount;
    D9Device *device;
    D3DQUERYTYPE type;
    uint32_t handle;
    UINT response_slot;
    uint32_t response_offset;
    uint32_t request_id;
    BOOL issued;
    BOOL begun;
    /* GetData's deadline: the heartbeat last seen, and since when */
    uint32_t last_beat;
    DWORD silent_since;
    struct D9Query *next_device_resource;
} D9Query;

static IDirect3DQuery9Vtbl g_query_vtbl;
static uint32_t query_heartbeat(void);

static D9Query *query_from_iface(IDirect3DQuery9 *iface)
{
    return (D9Query *)iface;
}

static HRESULT WINAPI query_query_interface(IDirect3DQuery9 *iface, REFIID iid,
        void **object)
{
    if (!object)
        return E_POINTER;
    *object = NULL;
    if (!iid || (!iid_is_unknown(iid) && !guid_equal(iid, &IID_IDirect3DQuery9)))
        return E_NOINTERFACE;
    *object = iface;
    IDirect3DQuery9_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI query_add_ref(IDirect3DQuery9 *iface)
{
    return (ULONG)InterlockedIncrement(&query_from_iface(iface)->refcount);
}

static ULONG WINAPI query_release(IDirect3DQuery9 *iface)
{
    D9Query *query = query_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&query->refcount);
    if (!refs) {
        D9Query **link = &query->device->queries;
        D9WGDestroyResource destroy;
        while (*link && *link != query)
            link = &(*link)->next_device_resource;
        if (*link) *link = query->next_device_resource;
        destroy.resource_handle = query->handle;
        destroy.resource_kind = D9WG_RESOURCE_QUERY;
        emit_command(D9WG_OP_DESTROY_RESOURCE, &destroy, sizeof(destroy));
        free_query_slot(query->response_slot);
        device_child_release(query->device);
        HeapFree(GetProcessHeap(), 0, query);
    }
    return refs;
}

static HRESULT WINAPI query_get_device(IDirect3DQuery9 *iface,
        IDirect3DDevice9 **device_out)
{
    D9Query *query = query_from_iface(iface);
    if (!device_out)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *device_out = &query->device->iface;
    IDirect3DDevice9_AddRef(*device_out);
    return D3D_OK;
}

static D3DQUERYTYPE WINAPI query_get_type(IDirect3DQuery9 *iface)
{
    return query_from_iface(iface)->type;
}

static DWORD WINAPI query_get_data_size(IDirect3DQuery9 *iface)
{
    switch (query_from_iface(iface)->type) {
    case D3DQUERYTYPE_EVENT:
    case D3DQUERYTYPE_OCCLUSION:
    case D3DQUERYTYPE_TIMESTAMPDISJOINT:
        return (DWORD)sizeof(DWORD);
    case D3DQUERYTYPE_TIMESTAMP:
    case D3DQUERYTYPE_TIMESTAMPFREQ:
        return (DWORD)sizeof(ULONGLONG);
    default:
        return 0;
    }
}

static D9WGQueryResponse *query_response(D9Query *query)
{
    uint8_t *base = response_region();
    if (!base || query->response_offset + sizeof(D9WGQueryResponse)
            > D9WG_QUERY_REGION_BYTES)
        return NULL;
    return (D9WGQueryResponse *)(base + query->response_offset);
}

static BOOL emit_query_issue(D9Query *query, uint16_t opcode)
{
    D9WGQueryIssue command;
    command.device_handle = query->device->handle;
    command.resource_handle = query->handle;
    command.response_offset = query->response_offset;
    command.request_id = query->request_id;
    return emit_command(opcode, &command, sizeof(command));
}

static HRESULT WINAPI query_issue(IDirect3DQuery9 *iface, DWORD flags)
{
    D9Query *query = query_from_iface(iface);
    D9WGQueryResponse *response;
    BOOL begin = flags == D3DISSUE_BEGIN;
    BOOL end = flags == D3DISSUE_END;

    if (!begin && !end)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (begin && query->type != D3DQUERYTYPE_OCCLUSION
            && query->type != D3DQUERYTYPE_TIMESTAMPDISJOINT)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (begin) {
        if (query->begun)
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        query->request_id = (uint32_t)next_response_request_id();
        response = query_response(query);
        if (!response)
            return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
        response->request_id = query->request_id;
        response->value_low = response->value_high = 0;
        response->status = D9WG_RESPONSE_PENDING;
        MemoryBarrier();
        if (!emit_query_issue(query, D9WG_OP_BEGIN_QUERY))
            return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
        query->begun = TRUE;
        query->issued = FALSE;
        return D3D_OK;
    }

    if ((query->type == D3DQUERYTYPE_OCCLUSION
            || query->type == D3DQUERYTYPE_TIMESTAMPDISJOINT)
            && !query->begun)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!query->begun) {
        query->request_id = (uint32_t)next_response_request_id();
        response = query_response(query);
        if (!response)
            return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
        response->request_id = query->request_id;
        response->value_low = response->value_high = 0;
        response->status = D9WG_RESPONSE_PENDING;
        MemoryBarrier();
    }
    if (!emit_query_issue(query, D9WG_OP_END_QUERY))
        return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
    query->begun = FALSE;
    query->issued = TRUE;
    query->last_beat = query_heartbeat();
    query->silent_since = GetTickCount();
    return D3D_OK;
}

/* The host bumps this once per batch it finishes (D9WG_HEARTBEAT_OFFSET) */
static uint32_t query_heartbeat(void)
{
    uint8_t *base = response_region();
    return base ? *(volatile uint32_t *)(base + D9WG_HEARTBEAT_OFFSET) : 0;
}

/*
 * What GetData answers for a query the host reported as failed, or stopped
 * answering: the value that keeps an application going and drawing rather
 * than culling or spinning -- visible, signalled, disjoint (review S1-2:
 * `while (GetData() == S_FALSE)` used to spin forever when the host dropped
 * the frame the query was in).
 */
static ULONGLONG query_fallback_value(D9Query *query)
{
    switch (query->type) {
    case D3DQUERYTYPE_OCCLUSION:
        return (ULONGLONG)query->device->display_mode.Width
                * query->device->display_mode.Height;
    case D3DQUERYTYPE_TIMESTAMPFREQ:
        return 1000000000u;
    case D3DQUERYTYPE_TIMESTAMP:
        /* nanoseconds, as the host's TIMESTAMPFREQ says */
        return (ULONGLONG)GetTickCount() * 1000000u;
    default:   /* EVENT signalled, TIMESTAMPDISJOINT disjoint */
        return 1;
    }
}

static HRESULT WINAPI query_get_data(IDirect3DQuery9 *iface, void *data,
        DWORD size, DWORD flags)
{
    D9Query *query = query_from_iface(iface);
    D9WGQueryResponse *response;
    DWORD expected = query_get_data_size(iface);
    ULONGLONG value;

    if (!query->issued)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (data && size < expected)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);

    /* GetData must make progress even before Present.  D3DGETDATA_FLUSH asks
     * for this explicitly, but flushing on ordinary polling also avoids a
     * command sitting forever in our batching buffer. */
    EnterCriticalSection(&g_transport_lock);
    if (g_command_count)
        submit_batch_locked(FALSE);
    LeaveCriticalSection(&g_transport_lock);

    response = query_response(query);
    if (!response)
        return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
    MemoryBarrier();
    if (response->request_id != query->request_id
            || response->status == D9WG_RESPONSE_PENDING) {
        /* The deadline measures host silence, as the readback's does: a
         * host thousands of batches behind is slow, not gone */
        uint32_t beat = query_heartbeat();
        if (beat != query->last_beat) {
            query->last_beat = beat;
            query->silent_since = GetTickCount();
        }
        if (GetTickCount() - query->silent_since < 6000u)
            return S_FALSE;
        HOSTLOG_FAILED("GetData: the host has not answered query %08lX type %lu "
                "for %lums; answering it as visible/signalled",
                query->handle, (DWORD)query->type, GetTickCount() - query->silent_since);
        value = query_fallback_value(query);
    } else if (response->status != D9WG_RESPONSE_OK) {
        /* the host dropped the frame the query was in */
        TRACE("GetData query %08lX failed on the host; answering the fallback", query->handle);
        value = query_fallback_value(query);
    } else {
        value = ((ULONGLONG)response->value_high << 32) | response->value_low;
    }
    if (data) {
        if (expected == sizeof(ULONGLONG))
            *(ULONGLONG *)data = value;
        else
            *(DWORD *)data = response->value_low;
    }
    (void)flags;
    return S_OK;
}

static HRESULT WINAPI device_create_query(IDirect3DDevice9 *iface,
        D3DQUERYTYPE type, IDirect3DQuery9 **query_out)
{
    D9Device *device = device_from_iface(iface);
    D9Query *query;
    D9WGCreateQuery command;
    D9WGQueryResponse *response;
    int response_slot;

    if (type != D3DQUERYTYPE_OCCLUSION && type != D3DQUERYTYPE_EVENT
            && type != D3DQUERYTYPE_TIMESTAMP
            && type != D3DQUERYTYPE_TIMESTAMPDISJOINT
            && type != D3DQUERYTYPE_TIMESTAMPFREQ) {

        if (query_out) *query_out = NULL;
        return TRACE_REFUSE(D3DERR_NOTAVAILABLE);
    }
    /* A NULL out pointer is D3D9's "do you support this type?" probe. */
    if (!query_out)
        return D3D_OK;
    *query_out = NULL;
    response_slot = allocate_query_slot();
    if (response_slot < 0)
        return TRACE_REFUSE(D3DERR_OUTOFVIDEOMEMORY);
    query = (D9Query *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*query));
    if (!query) {
        free_query_slot((UINT)response_slot);
        return TRACE_REFUSE(E_OUTOFMEMORY);
    }
    query->iface.lpVtbl = &g_query_vtbl;
    query->refcount = 1;
    query->device = device;
    query->type = type;
    query->handle = allocate_handle();
    query->response_slot = (UINT)response_slot;
    query->response_offset = (UINT)response_slot * D9WG_QUERY_SLOT_BYTES;
    EnterCriticalSection(&g_transport_lock);
    if (!open_transport_locked()) {
        LeaveCriticalSection(&g_transport_lock);
        free_query_slot((UINT)response_slot);
        HeapFree(GetProcessHeap(), 0, query);
        return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
    }
    response = query_response(query);
    ZeroMemory(response, sizeof(*response));
    command.device_handle = device->handle;
    command.resource_handle = query->handle;
    command.query_type = type;
    command.response_offset = query->response_offset;
    if (!reserve_command_locked(D9WG_OP_CREATE_QUERY, sizeof(command), 0,
            NULL, (uint8_t **)&response, NULL)) {
        LeaveCriticalSection(&g_transport_lock);
        free_query_slot((UINT)response_slot);
        HeapFree(GetProcessHeap(), 0, query);
        return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
    }
    CopyMemory(response, &command, sizeof(command));
    LeaveCriticalSection(&g_transport_lock);
    device_child_add_ref(device);
    query->next_device_resource = device->queries;
    device->queries = query;
    *query_out = &query->iface;
    return D3D_OK;
}

static IDirect3DQuery9Vtbl g_query_vtbl = {
    .QueryInterface = query_query_interface,
    .AddRef = query_add_ref,
    .Release = query_release,
    .GetDevice = query_get_device,
    .GetType = query_get_type,
    .GetDataSize = query_get_data_size,
    .Issue = query_issue,
    .GetData = query_get_data
};

/* ---- IDirect3DCubeTexture9 ----
 *
 * Six square faces per mip level, stored as one flat level array indexed
 * face * level_count + level. On the wire a cube texture is a
 * CREATE_TEXTURE_CUBE plus ordinary UPDATE_TEXTURE commands whose `z` names the
 * face, which is the same field a volume texture uses for its slice -- in both
 * cases it selects the layer the upload lands in, and the host's WebGPU texture
 * is layered either way.
 */
static D9CubeTexture *cube_from_iface(IDirect3DCubeTexture9 *iface)
{
    return (D9CubeTexture *)iface;
}

static BOOL emit_cube_texture_create(D9Device *device, D9CubeTexture *texture)
{
    D9WGCreateTextureCube command;
    command.device_handle = device->handle;
    command.resource_handle = texture->handle;
    command.edge_length = texture->edge_length;
    command.level_count = texture->level_count;
    command.format = texture->format;
    command.usage = texture->usage;
    command.pool = texture->pool;
    command.reserved = 0;
    if (!emit_command(D9WG_OP_CREATE_TEXTURE_CUBE, &command, sizeof(command))) return FALSE;
    return device_compatibility_api(device) == 8u
        || !(texture->usage & D3DUSAGE_AUTOGENMIPMAP)
        || emit_generate_mips(device, texture->handle, texture->autogen_filter, TRUE);
}

/* Shares the 2D upload path's payload shape; only the resource handle and the
 * face index differ, so a bug in one cannot silently diverge from the other. */
static BOOL emit_cube_texture_update(D9CubeTexture *texture, UINT face,
        UINT level, const RECT *rect)
{
    return emit_level_update_api(device_compatibility_api(texture->device),
            texture->handle, texture->format,
            &texture->levels[face * texture->level_count + level], level, face, rect);
}

static HRESULT create_cube_texture_object(D9Device *device,
        UINT edge, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
        UINT api, IDirect3DCubeTexture9 **texture_out)
{
    D9CubeTexture *texture;
    UINT full_levels;
    UINT face;
    UINT level;
    UINT total;
    HRESULT failure = E_OUTOFMEMORY;
    HRESULT success = D3D_OK;

    /* This path used to refuse without recording a single argument, which is
     * how a 3DMark06 crash spent a whole trace being unattributable: the log
     * said only that a cube map had been rejected, never which of four
     * conditions rejected it. A refusal that cannot be explained cannot be
     * fixed, so the parameters are recorded before anything can reject them. */
    TRACE("CALL CreateCubeTexture edge=%lu levels=%lu usage=%08lX "
            "format=%08lX pool=%lu", edge, levels, usage, (DWORD)format,
            (DWORD)pool);

    if (!texture_out) {
        TRACE("FAIL CreateCubeTexture missing output -> %08lX",
                (DWORD)D3DERR_INVALIDCALL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    *texture_out = NULL;
    if (api == 8u) {
        if (!edge || edge > 4096 || !supported_format8(format, FALSE)
                || (usage & D3DUSAGE_DEPTHSTENCIL)
                || ((usage & D3DUSAGE_RENDERTARGET)
                    && (pool != D3DPOOL_DEFAULT
                        || !supported_render_target_format8(format)))
                || pool > D3DPOOL_SCRATCH)
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
    } else {
        /* See device_create_texture: one visible level, a full chain behind it. */
        if (!d9wg_autogen_creation_valid(levels, usage, pool))
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        if (usage & D3DUSAGE_AUTOGENMIPMAP)
            levels = 1;
        if (!edge || edge > 8192
                || !texture_create_supported(D3DRTYPE_CUBETEXTURE,
                        usage & ~(DWORD)D3DUSAGE_AUTOGENMIPMAP, format, pool)) {

            TRACE("FAIL CreateCubeTexture invalid arguments -> %08lX",
                    (DWORD)D3DERR_INVALIDCALL);
            HOSTLOG_FAILED("CreateCubeTexture refused: edge=%lu levels=%lu "
                    "usage=%08lX format=%08lX pool=%lu", edge, levels, usage,
                    (DWORD)format, (DWORD)pool);
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        }
    }
    full_levels = full_mip_level_count(edge, edge);
    if (!levels) levels = full_levels;
    if (levels > full_levels) {
        TRACE("FAIL CreateCubeTexture levels=%lu exceeds full_levels=%lu "
                "-> %08lX", levels, full_levels, (DWORD)D3DERR_INVALIDCALL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    if (!multiply_u32(levels, 6u, &total))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);

    texture = (D9CubeTexture *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*texture));
    if (!texture)
        return TRACE_REFUSE(E_OUTOFMEMORY);
    texture->levels = (D9TextureLevel *)HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY, total * sizeof(*texture->levels));
    if (!texture->levels) {
        HeapFree(GetProcessHeap(), 0, texture);
        return TRACE_REFUSE(E_OUTOFMEMORY);
    }
    if (api == 9u && (usage & D3DUSAGE_AUTOGENMIPMAP))
        texture->autogen_filter = (D3DTEXTUREFILTERTYPE)d9wg_autogen_default_filter(format,
                (d9wg_format_query_info(format) & 4u) ? runtime_host_features() : 0);
    texture->iface.lpVtbl = &g_cube_vtbl;
    texture->refcount = 1;
    texture->device = device;
    texture->handle = allocate_handle();
    texture->edge_length = edge;
    texture->level_count = levels;
    texture->usage = usage;
    texture->format = format;
    texture->pool = pool;
    device_child_add_ref(device);

    for (face = 0; face < 6; ++face) {
        UINT size = edge;
        for (level = 0; level < levels; ++level) {
            D9TextureLevel *level_data = &texture->levels[face * levels + level];
            level_data->width = size;
            level_data->height = size;
            if (!d9wg_texture_level_layout(api, format, size, size,
                    &level_data->row_pitch, &level_data->row_count,
                    &level_data->byte_count))
                goto allocation_failed;
            level_data->shadow = (BYTE *)HeapAlloc(GetProcessHeap(),
                    HEAP_ZERO_MEMORY, level_data->byte_count);
            if (!level_data->shadow)
                goto allocation_failed;
            if (size > 1) size >>= 1;
        }
    }
    if (!emit_cube_texture_create(device, texture)) {
        failure = D3DERR_DRIVERINTERNALERROR;
        goto allocation_failed;
    }
    texture->next_device_resource = device->cube_textures;
    device->cube_textures = texture;
    *texture_out = &texture->iface;
    TRACE("OK CreateCubeTexture handle=%08lX object=%08lX edge=%lu levels=%lu "
            "usage=%08lX format=%08lX pool=%lu -> %08lX", texture->handle,
            (DWORD)(uintptr_t)*texture_out, edge, levels, usage, (DWORD)format,
            (DWORD)pool, (DWORD)success);
    return success;

allocation_failed:
    for (face = 0; face < total; ++face) {
        if (texture->levels[face].shadow)
            HeapFree(GetProcessHeap(), 0, texture->levels[face].shadow);
    }
    device_child_release(device);
    HeapFree(GetProcessHeap(), 0, texture->levels);
    HeapFree(GetProcessHeap(), 0, texture);
    return failure;
}

static HRESULT WINAPI device_create_cube_texture(IDirect3DDevice9 *iface,
        UINT edge, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
        IDirect3DCubeTexture9 **texture_out, HANDLE *shared)
{
    (void)shared;
    return create_cube_texture_object(device_from_iface(iface), edge, levels,
            usage, format, pool, 9u, texture_out);
}

static HRESULT WINAPI cube_query_interface(IDirect3DCubeTexture9 *iface,
        REFIID iid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = NULL;
    if (!iid || (!iid_is_unknown(iid)
            && !guid_equal(iid, &IID_IDirect3DResource9)
            && !guid_equal(iid, &IID_IDirect3DBaseTexture9)
            && !guid_equal(iid, &IID_IDirect3DCubeTexture9)))
        return E_NOINTERFACE;
    *object = iface;
    IDirect3DCubeTexture9_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI cube_add_ref(IDirect3DCubeTexture9 *iface)
{
    D9CubeTexture *texture = cube_from_iface(iface);
    ULONG refs = (ULONG)InterlockedIncrement(&texture->refcount);
    if (refs == 1)
        device_child_add_ref(texture->device);
    return refs;
}

static void cube_destroy_storage(D9CubeTexture *texture)
{
    D9CubeTexture** link = &texture->device->cube_textures;
    D9WGDestroyResource destroy;
    UINT index;
    while (*link && *link != texture)
        link = &(*link)->next_device_resource;
    if (*link)
        *link = texture->next_device_resource;
    d9wg_private_clear(&texture->private_data, &d9wg_private_ops);
    destroy.resource_handle = texture->handle;
    destroy.resource_kind = D9WG_RESOURCE_TEXTURE_CUBE;
    emit_command(D9WG_OP_DESTROY_RESOURCE, &destroy, sizeof(destroy));
    for (index = 0; index < texture->level_count * 6u; ++index) {
        d9wg_private_clear(&texture->levels[index].private_data, &d9wg_private_ops);
        core_view_retire8(texture->levels[index].level_surface);
        HeapFree(GetProcessHeap(), 0,
            texture->levels[index].level_surface);
        HeapFree(GetProcessHeap(), 0, texture->levels[index].shadow);
    }
    HeapFree(GetProcessHeap(), 0, texture->levels);
    core_view_retire8(texture);
    HeapFree(GetProcessHeap(), 0, texture);
}

static ULONG WINAPI cube_release(IDirect3DCubeTexture9* iface)
{
    D9CubeTexture *texture = cube_from_iface(iface);
    if (!InterlockedCompareExchange(&texture->refcount, 0, 0))
        return 0;
    ULONG refs = (ULONG)InterlockedDecrement(&texture->refcount);
    if (!refs) {
        D9Device *device = texture->device;
        if (!InterlockedCompareExchange(&texture->binding_refs, 0, 0))
            cube_destroy_storage(texture);
        device_child_release(device);
    }
    return refs;
}

static HRESULT WINAPI cube_get_device(IDirect3DCubeTexture9 *iface,
        IDirect3DDevice9 **device_out)
{
    D9CubeTexture *texture = cube_from_iface(iface);
    if (!device_out)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *device_out = &texture->device->iface;
    IDirect3DDevice9_AddRef(*device_out);
    return D3D_OK;
}

static HRESULT WINAPI cube_set_private_data(IDirect3DCubeTexture9* iface,
    REFGUID guid, const void* data, DWORD size, DWORD flags)
{
    D9WGPrivateStore* store = &cube_from_iface(iface)->private_data;
    return TRACE_REFUSE(d9wg_private_hresult(d9wg_private_set(store, guid, data, size, flags, &d9wg_private_ops)));
}
static HRESULT WINAPI cube_get_private_data(IDirect3DCubeTexture9* iface,
    REFGUID guid, void* data, DWORD* size)
{
    D9WGPrivateStore* store = &cube_from_iface(iface)->private_data;
    return TRACE_REFUSE(d9wg_get_private_data(store, guid, data, size));
}
static HRESULT WINAPI cube_free_private_data(IDirect3DCubeTexture9* iface,
    REFGUID guid)
{
    D9WGPrivateStore* store = &cube_from_iface(iface)->private_data;
    return TRACE_REFUSE(d9wg_private_hresult(d9wg_private_free(store, guid, &d9wg_private_ops)));
}

static DWORD WINAPI cube_set_priority(IDirect3DCubeTexture9 *iface,
        DWORD priority)
{
    D9CubeTexture *texture = cube_from_iface(iface);
    DWORD previous = texture->priority;
    texture->priority = priority;
    return previous;
}

static DWORD WINAPI cube_get_priority(IDirect3DCubeTexture9 *iface)
{ return cube_from_iface(iface)->priority; }
static void WINAPI cube_preload(IDirect3DCubeTexture9 *iface) { (void)iface; }
static D3DRESOURCETYPE WINAPI cube_get_type(IDirect3DCubeTexture9 *iface)
{ (void)iface; return D3DRTYPE_CUBETEXTURE; }

static DWORD WINAPI cube_set_lod(IDirect3DCubeTexture9 *iface, DWORD lod)
{
    D9CubeTexture *texture = cube_from_iface(iface);
    return d9wg_set_texture_lod(texture->device->handle, texture->handle,
            texture->pool, texture->level_count, &texture->lod, lod);
}

static DWORD WINAPI cube_get_lod(IDirect3DCubeTexture9 *iface)
{ return cube_from_iface(iface)->lod; }
static DWORD WINAPI cube_get_level_count(IDirect3DCubeTexture9 *iface)
{ return cube_from_iface(iface)->level_count; }
/* Same contract as the 2D forms; see texture_set_auto_gen_filter_type. */
static HRESULT WINAPI cube_set_auto_gen_filter_type(
        IDirect3DCubeTexture9 *iface, D3DTEXTUREFILTERTYPE type)
{
    D9CubeTexture *texture = cube_from_iface(iface);
    if (!(texture->usage & D3DUSAGE_AUTOGENMIPMAP))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!d9wg_autogen_filter_supported(texture->format, type,
                (d9wg_format_query_info(texture->format) & 4u) ? runtime_host_features() : 0))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!emit_generate_mips(texture->device, texture->handle, type, TRUE))
        return D3DERR_DRIVERINTERNALERROR;
    texture->autogen_filter = type;
    return D3D_OK;
}
static D3DTEXTUREFILTERTYPE WINAPI cube_get_auto_gen_filter_type(
        IDirect3DCubeTexture9 *iface)
{
    D9CubeTexture *texture = cube_from_iface(iface);
    if (!(texture->usage & D3DUSAGE_AUTOGENMIPMAP))
        return D3DTEXF_NONE;
    return texture->autogen_filter ? texture->autogen_filter : D3DTEXF_LINEAR;
}
static void WINAPI cube_generate_mip_sublevels(IDirect3DCubeTexture9 *iface)
{
    D9CubeTexture *texture = cube_from_iface(iface);
    if (!(texture->usage & D3DUSAGE_AUTOGENMIPMAP))
        return;
    emit_generate_mips(texture->device, texture->handle, texture->autogen_filter, FALSE);
}

static HRESULT WINAPI cube_get_level_desc(IDirect3DCubeTexture9 *iface,
        UINT level, D3DSURFACE_DESC *desc)
{
    D9CubeTexture *texture = cube_from_iface(iface);
    if (!desc || level >= texture->level_count)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    ZeroMemory(desc, sizeof(*desc));
    desc->Format = texture->format;
    desc->Type = D3DRTYPE_SURFACE;
    desc->Usage = texture->usage;
    desc->Pool = texture->pool;
    desc->MultiSampleType = D3DMULTISAMPLE_NONE;
    desc->MultiSampleQuality = 0;
    desc->Width = texture->levels[level].width;
    desc->Height = texture->levels[level].height;
    return D3D_OK;
}

static HRESULT WINAPI cube_get_cube_map_surface(IDirect3DCubeTexture9 *iface,
        D3DCUBEMAP_FACES face, UINT level, IDirect3DSurface9 **surface_out)
{
    D9CubeTexture *texture = cube_from_iface(iface);
    D9TextureLevel *level_data;
    D9Surface *surface;
    if (!surface_out)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *surface_out = NULL;
    if ((UINT)face >= 6u || level >= texture->level_count)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    level_data = &texture->levels[(UINT)face * texture->level_count + level];
    surface = level_data->level_surface;
    if (!surface) {
        surface = (D9Surface *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                sizeof(*surface));
        if (!surface)
            return TRACE_REFUSE(E_OUTOFMEMORY);
        surface->iface.lpVtbl = &g_surface_vtbl;
        surface->device = texture->device;
        surface->cube_texture = texture;
        surface->cube_face = (UINT)face;
        surface->texture_child = TRUE;
        surface->level = level;
        surface->width = level_data->width;
        surface->height = level_data->height;
        surface->format = texture->format;
        surface->pool = texture->pool;
        surface->multisample = D3DMULTISAMPLE_NONE;
        level_data->level_surface = surface;
    }
    IDirect3DCubeTexture9_AddRef(iface);
    *surface_out = &surface->iface;
    return D3D_OK;
}

static HRESULT WINAPI cube_lock_rect(IDirect3DCubeTexture9 *iface,
        D3DCUBEMAP_FACES face, UINT level, D3DLOCKED_RECT *locked_rect,
        const RECT *rect, DWORD flags)
{
    D9CubeTexture *texture = cube_from_iface(iface);
    D9TextureLevel *level_data;
    RECT area;
    UINT block_width, block_height, block_bytes;

    if (!locked_rect || (UINT)face >= 6u || level >= texture->level_count)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    level_data = &texture->levels[(UINT)face * texture->level_count + level];
    if (level_data->locked)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (rect) {
        area = *rect;
    } else {
        SetRect(&area, 0, 0, (int)level_data->width, (int)level_data->height);
    }
    if (area.left < 0 || area.top < 0 || area.right <= area.left
            || area.bottom <= area.top
            || (UINT)area.right > level_data->width
            || (UINT)area.bottom > level_data->height
            || !d9wg_format_layout(device_compatibility_api(texture->device),
                    texture->format, &block_width,
                    &block_height, &block_bytes))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (device_compatibility_api(texture->device) == 8u && block_width > 1u
            && (((UINT)area.left % block_width)
                || ((UINT)area.top % block_height)
                || ((UINT)area.right != level_data->width
                    && (UINT)area.right % block_width)
                || ((UINT)area.bottom != level_data->height
                    && (UINT)area.bottom % block_height)))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    level_data->lock_rect = area;
    level_data->lock_flags = flags;
    level_data->locked = TRUE;
    locked_rect->Pitch = (INT)level_data->row_pitch;
    locked_rect->pBits = level_data->shadow
            + ((UINT)area.top / block_height) * level_data->row_pitch
            + ((UINT)area.left / block_width) * block_bytes;
    return D3D_OK;
}

static HRESULT WINAPI cube_unlock_rect(IDirect3DCubeTexture9 *iface,
        D3DCUBEMAP_FACES face, UINT level)
{
    D9CubeTexture *texture = cube_from_iface(iface);
    D9TextureLevel *level_data;
    BOOL result = TRUE;

    if ((UINT)face >= 6u || level >= texture->level_count)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    level_data = &texture->levels[(UINT)face * texture->level_count + level];
    if (!level_data->locked)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!(level_data->lock_flags & D3DLOCK_READONLY))
        result = emit_cube_texture_update(texture, (UINT)face, level,
                &level_data->lock_rect);
    level_data->locked = FALSE;
    level_data->lock_flags = 0;
    ZeroMemory(&level_data->lock_rect, sizeof(level_data->lock_rect));
    return result ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI cube_add_dirty_rect(IDirect3DCubeTexture9 *iface,
        D3DCUBEMAP_FACES face, const RECT *rect)
{ (void)iface; (void)face; (void)rect; return D3D_OK; }

static IDirect3DCubeTexture9Vtbl g_cube_vtbl = {
    .QueryInterface = cube_query_interface,
    .AddRef = cube_add_ref,
    .Release = cube_release,
    .GetDevice = cube_get_device,
    .SetPrivateData = cube_set_private_data,
    .GetPrivateData = cube_get_private_data,
    .FreePrivateData = cube_free_private_data,
    .SetPriority = cube_set_priority,
    .GetPriority = cube_get_priority,
    .PreLoad = cube_preload,
    .GetType = cube_get_type,
    .SetLOD = cube_set_lod,
    .GetLOD = cube_get_lod,
    .GetLevelCount = cube_get_level_count,
    .SetAutoGenFilterType = cube_set_auto_gen_filter_type,
    .GetAutoGenFilterType = cube_get_auto_gen_filter_type,
    .GenerateMipSubLevels = cube_generate_mip_sublevels,
    .GetLevelDesc = cube_get_level_desc,
    .GetCubeMapSurface = cube_get_cube_map_surface,
    .LockRect = cube_lock_rect,
    .UnlockRect = cube_unlock_rect,
    .AddDirtyRect = cube_add_dirty_rect
};

/* ---- IDirect3DStateBlock9 (M3) ----
 *
 * A state block is a snapshot of a subset of device state plus a mask saying
 * which entries the subset contains: a captured zero is not the same thing as
 * an uncaptured state, so both halves are needed.
 *
 * Apply() replays the snapshot through the ordinary setters, which means it
 * inherits their deduplication and their D9WG emitters for free -- there is no
 * second, parallel path that could disagree with the first about what a given
 * state does.
 *
 * Begin/End retains only explicitly recorded values and object references.
 * Setters validate first, then store into that block before any live mutation
 * or command emission. End publishes it without capturing or restoring state.
 */

/* Predefined render/texture/sampler classes are shared with D3D8 in
 * d9wg_state_block_scope.h. Explicit recorded masks remain frontend-owned. */
/* ALL and explicit recordings preserve every SDK world matrix. */


static IDirect3DStateBlock9Vtbl g_state_block_vtbl;

static struct D9StateBlock *state_block_from_iface(IDirect3DStateBlock9 *iface)
{
    return (struct D9StateBlock *)iface;
}

static BOOL state_block_record_palette(D9Device *device, UINT index)
{
    if (!device->recording_result) return FALSE;
    device->recording_result->has_palette = TRUE;
    device->recording_result->current_palette = index;
    return TRUE;
}

static BOOL state_block_record_render_state(D9Device *device, UINT state, DWORD value)
{
    D9StateBlock *block = device->recording_result;
    if (!block) return FALSE;
    block->has_render_state[state] = TRUE;block->render_states[state] = value;
    return TRUE;
}

static BOOL state_block_record_texture_stage_state(D9Device *device, UINT stage, UINT state, DWORD value)
{
    D9StateBlock *block = device->recording_result;
    if (!block) return FALSE;
    block->has_texture_stage_state[stage][state] = TRUE;block->texture_stage_states[stage][state] = value;
    return TRUE;
}

static BOOL state_block_record_sampler_state(D9Device *device, UINT sampler, UINT state, DWORD value)
{
    D9StateBlock *block = device->recording_result;
    if (!block) return FALSE;
    block->has_sampler_state[sampler][state] = TRUE;block->sampler_states[sampler][state] = value;
    return TRUE;
}

static BOOL state_block_record_transform(D9Device *device, UINT state, const D3DMATRIX *matrix)
{
    D9StateBlock *block = device->recording_result;
    if (!block) return FALSE;
    uint32_t slot;if (!d9wg_state_block_transform_slot(state, &slot)) return FALSE;
    block->has_transform[slot] = TRUE;CopyMemory(block->transforms[slot], matrix, 64);
    return TRUE;
}

static const float *state_block_transform_source(D9Device *device, UINT state)
{
    uint32_t slot;
    D9StateBlock *block = device->recording_result;
    if (block && d9wg_state_block_transform_slot(state, &slot) && block->has_transform[slot])
        return block->transforms[slot];
    return device->transforms[state];
}



static BOOL state_block_record_material(D9Device *device, const D3DMATERIAL9 *material)
{
    D9StateBlock *block = device->recording_result;
    if (!block) return FALSE;
    block->has_material = TRUE;block->material = *material;
    return TRUE;
}

static BOOL state_block_record_clip_plane(D9Device *device, UINT index, const float *plane)
{
    D9StateBlock *block = device->recording_result;
    if (!block) return FALSE;
    block->has_clip_plane[index] = TRUE;CopyMemory(block->clip_planes[index], plane, 16);
    return TRUE;
}
static BOOL state_block_record_npatch_mode(D9Device *device, float segments)
{
    D9StateBlock *block = device->recording_result;
    if (!block) return FALSE;
    block->has_npatch_mode = TRUE;block->npatch_segments = segments;
    return TRUE;
}
static BOOL state_block_record_stream_frequency(D9Device *device, UINT stream, DWORD divider)
{
    D9StateBlock *block = device->recording_result;
    if (!block) return FALSE;
    block->has_stream_frequency[stream] = TRUE;block->streams[stream].frequency = divider;
    return TRUE;
}



static BOOL state_block_record_viewport(D9Device *device, const D3DVIEWPORT9 *viewport)
{
    D9StateBlock *block = device->recording_result;
    if (!block) return FALSE;
    block->has_viewport = TRUE;block->viewport = *viewport;
    return TRUE;
}

static BOOL state_block_record_scissor(D9Device *device, const RECT *rect)
{
    D9StateBlock *block = device->recording_result;
    if (!block) return FALSE;
    block->has_scissor = TRUE;block->scissor_rect = *rect;
    return TRUE;
}





static BOOL state_block_record_constant(D9Device *device, BOOL is_pixel,
        BOOL is_float, BOOL is_bool, UINT start, UINT count, const void *data)
{
    D9StateBlock *block = device->recording_result;
    if (!block) return FALSE;
    for (UINT i = 0; i < count; ++i) {
        UINT index = start + i;
        if (is_float) {
            if (is_pixel) {block->has_ps_const_f[index] = TRUE;CopyMemory(block->ps_const_f[index], (const float *)data + i * 4, 16);}
            else {block->has_vs_const_f[index] = TRUE;CopyMemory(block->vs_const_f[index], (const float *)data + i * 4, 16);}
        } else if (is_bool) {
            BOOL value = ((const WINBOOL *)data)[i] ? TRUE : FALSE;
            if (is_pixel) {block->has_ps_const_b[index] = TRUE;block->ps_const_b[index] = value;}
            else {block->has_vs_const_b[index] = TRUE;block->vs_const_b[index] = value;}
        } else {
            if (is_pixel) {block->has_ps_const_i[index] = TRUE;CopyMemory(block->ps_const_i[index], (const int *)data + i * 4, 16);}
            else {block->has_vs_const_i[index] = TRUE;CopyMemory(block->vs_const_i[index], (const int *)data + i * 4, 16);}
        }
    }
    return TRUE;
}







/* State blocks retain captured storage independently of public COM refs.
 * Apply may revive bindings after the application has released every public
 * resource reference, without introducing resource/device reference cycles. */
static void state_block_release_references(struct D9StateBlock *block)
{
    UINT index;
    for (index = 0; index < D9_MAX_TEXTURE_BINDINGS; ++index) {
        D9Texture *texture = block->textures[index];
        D9CubeTexture *cube = block->cube_textures[index];
        D9VolumeTexture *volume = block->volume_textures[index];
        block->textures[index] = NULL;
        block->cube_textures[index] = NULL;
        block->volume_textures[index] = NULL;
        if (texture) texture_binding_release(texture);
        if (cube) cube_binding_release(cube);
        if (volume) volume_texture_binding_release(volume);
    }
    D9Shader *vertex_shader = block->vertex_shader;
    D9Shader *pixel_shader = block->pixel_shader;
    D9VertexDeclaration *decl = block->vertex_declaration;
    block->vertex_shader = NULL;
    block->pixel_shader = NULL;
    block->vertex_declaration = NULL;
    if (vertex_shader) shader_binding_release(vertex_shader);
    if (pixel_shader) shader_binding_release(pixel_shader);
    if (decl) decl_binding_release(decl);
    for (index = 0; index < D9_MAX_STREAMS; ++index) {
        D9VertexBuffer *buffer = block->streams[index].buffer;
        block->streams[index].buffer = NULL;
        if (buffer) vb_binding_release(buffer);
    }
    D9IndexBuffer *indices = block->index_buffer;
    block->index_buffer = NULL;
    if (indices) ib_binding_release(indices);
}

/* Copies the live device value into every slot the mask already marks. This is
 * both Capture() and the second half of block creation, so "which states does
 * this block hold" is decided in exactly one place per block type. */
static void state_block_capture(struct D9StateBlock *block)
{
    D9Device *device = block->device;
    UINT index, stage, slot;

    if (block->has_palette) block->current_palette = device->current_palette;
    state_block_release_references(block);

    for (index = 0; index < D9_MAX_RENDER_STATES; ++index) {
        if (block->has_render_state[index])
            block->render_states[index] = device->render_states[index];
    }
    for (stage = 0; stage < D9_MAX_TEXTURE_BINDINGS; ++stage) {
        if (stage < D9_MAX_TEXTURE_STAGES) {
            for (index = 0; index < D9_MAX_TEXTURE_STAGE_STATES; ++index) {
                if (block->has_texture_stage_state[stage][index])
                    block->texture_stage_states[stage][index] =
                            device->texture_stage_states[stage][index];
            }
        }
        if (block->has_texture[stage]) {
            block->textures[stage] = device->textures[stage];
            block->cube_textures[stage] = device->cube_bindings[stage];
            block->volume_textures[stage] = device->volume_bindings[stage];
            if (block->textures[stage])
                texture_binding_add_ref(block->textures[stage]);
            if (block->cube_textures[stage])
                cube_binding_add_ref(block->cube_textures[stage]);
            if (block->volume_textures[stage])
                volume_texture_binding_add_ref(block->volume_textures[stage]);
        }
    }
    for (slot = 0; slot < D9_MAX_SAMPLERS; ++slot) {
        for (index = 0; index < D9_MAX_SAMPLER_STATES; ++index) {
            if (block->has_sampler_state[slot][index])
                block->sampler_states[slot][index] =
                        device->sampler_states[slot][index];
        }
    }
    for (index = 0; index < D9_STATE_BLOCK_TRANSFORMS; ++index) {
        if (block->has_transform[index])
            CopyMemory(block->transforms[index],
                    device->transforms[d9wg_state_block_transform_state(index)],
                    sizeof(block->transforms[index]));
    }
    if (block->has_material) block->material = device->material;
    for (index = 0; index < D9_MAX_CLIP_PLANES; ++index)
        if (block->has_clip_plane[index]) CopyMemory(block->clip_planes[index], device->clip_planes[index], 16);
    if (block->has_npatch_mode) block->npatch_segments = device->npatch_segments;
    for (index = 0; index < D9_MAX_LIGHTS; ++index) {
        if (block->has_light[index]) block->lights[index] = device->lights[index];
        if (block->has_light_enable[index])
            block->light_enabled[index] = device->light_enabled[index];
    }
    for (D9WGLightState *entry = block->extra_lights; entry; entry = entry->next) {
        D9WGLightState *current = d9wg_light_find(device->extra_lights, entry->index);
        if (entry->light_mask && current && current->set) {entry->light = current->light;entry->set = TRUE;}
        if (entry->enable_mask) entry->enabled = current ? current->enabled : FALSE;
    }
    if (block->has_viewport) block->viewport = device->viewport;
    if (block->has_scissor) block->scissor_rect = device->scissor_rect;
    if (block->has_vertex_shader) {
        block->vertex_shader = device->vertex_shader;
        if (block->vertex_shader)
            shader_binding_add_ref(block->vertex_shader);
    }
    if (block->has_pixel_shader) {
        block->pixel_shader = device->pixel_shader;
        if (block->pixel_shader)
            shader_binding_add_ref(block->pixel_shader);
    }
    for (index = 0; index < D9_MAX_VS_CONST_F; ++index) {
        if (block->has_vs_const_f[index])
            CopyMemory(block->vs_const_f[index], device->vs_const_f[index], 16);
    }
    for (index = 0; index < D9_MAX_PS_CONST_F; ++index) {
        if (block->has_ps_const_f[index])
            CopyMemory(block->ps_const_f[index], device->ps_const_f[index], 16);
    }
    for (index = 0; index < D9_MAX_CONST_I; ++index) {
        if (block->has_vs_const_i[index])
            CopyMemory(block->vs_const_i[index], device->vs_const_i[index], 16);
        if (block->has_ps_const_i[index])
            CopyMemory(block->ps_const_i[index], device->ps_const_i[index], 16);
    }
    for (index = 0; index < D9_MAX_CONST_B; ++index) {
        if (block->has_vs_const_b[index])
            block->vs_const_b[index] = device->vs_const_b[index];
        if (block->has_ps_const_b[index])
            block->ps_const_b[index] = device->ps_const_b[index];
    }
    if (block->has_vertex_format) {
        block->fvf = device->fvf;
        block->vertex_declaration = device->vertex_declaration;
        if (block->vertex_declaration)
            decl_binding_add_ref(block->vertex_declaration);
    }
    for (index = 0; index < D9_MAX_STREAMS; ++index) {
        if (block->has_stream[index]) {
            block->streams[index] = device->streams[index];
            if (block->streams[index].buffer)
                vb_binding_add_ref(block->streams[index].buffer);
        }
    }
    for (index = 0; index < D9_MAX_STREAMS; ++index)
        if (block->has_stream_frequency[index]) block->streams[index].frequency = device->streams[index].frequency;
    if (block->has_indices) {
        block->index_buffer = device->index_buffer;
        if (block->index_buffer)
            ib_binding_add_ref(block->index_buffer);
    }
}

static void state_block_mark_pixel_states(struct D9StateBlock *block)
{
    UINT index, stage, slot;
    for (index = 0; index < sizeof(g_pixel_state_render_states) /
            sizeof(g_pixel_state_render_states[0]); ++index) {
        WORD state = g_pixel_state_render_states[index];
        if (state < D9_MAX_RENDER_STATES) block->has_render_state[state] = TRUE;
    }
    for (stage = 0; stage < D9_MAX_TEXTURE_STAGES; ++stage) {
        for (index = 0; index < D9_MAX_TEXTURE_STAGE_STATES; ++index) {
            block->has_texture_stage_state[stage][index] = d9wg_state_block_texture_state(D3DSBT_PIXELSTATE, index);
        }
    }
    for (slot = 0; slot < D9_MAX_SAMPLERS; ++slot) {
        for (index = 0; index < D9_MAX_SAMPLER_STATES; ++index)
            if (d9wg_state_block_sampler_state(D3DSBT_PIXELSTATE, index)) block->has_sampler_state[slot][index] = TRUE;
    }
    block->has_pixel_shader = TRUE;
    for (index = 0; index < D9_MAX_PS_CONST_F; ++index)
        block->has_ps_const_f[index] = TRUE;
    for (index = 0; index < D9_MAX_CONST_I; ++index)
        block->has_ps_const_i[index] = TRUE;
    for (index = 0; index < D9_MAX_CONST_B; ++index)
        block->has_ps_const_b[index] = TRUE;
}

static void state_block_mark_vertex_states(struct D9StateBlock *block)
{
    UINT index, stage, slot;
    for (index = 0; index < sizeof(g_vertex_state_render_states) /
            sizeof(g_vertex_state_render_states[0]); ++index) {
        WORD state = g_vertex_state_render_states[index];
        if (state < D9_MAX_RENDER_STATES) block->has_render_state[state] = TRUE;
    }
    for (stage = 0; stage < D9_MAX_TEXTURE_STAGES; ++stage) {
        block->has_texture_stage_state[stage][D3DTSS_TEXCOORDINDEX] = TRUE;
        block->has_texture_stage_state[stage][D3DTSS_TEXTURETRANSFORMFLAGS] = TRUE;
    }
    for (slot = 0; slot < D9_MAX_SAMPLERS; ++slot)
        for (index = 0; index < D9_MAX_SAMPLER_STATES; ++index)
            if (d9wg_state_block_sampler_state(D3DSBT_VERTEXSTATE, index)) block->has_sampler_state[slot][index] = TRUE;
    for (index = 0; index < D9_MAX_STREAMS; ++index) block->has_stream_frequency[index] = TRUE;
    block->has_npatch_mode = TRUE;
    for (index = 0; index < D9_MAX_LIGHTS; ++index) {
        block->has_light[index] = device_light_is_set(block->device, index);
        block->has_light_enable[index] = device_light_is_set(block->device, index);
    }
    for (D9WGLightState *light = block->device->extra_lights; light; light = light->next) {
        if (!light->set) continue;
        D9WGLightState *entry = d9wg_light_get(&block->extra_lights, light->index, TRUE);
        if (!entry) {block->light_allocation_failed = TRUE;break;}
        entry->light_mask = entry->enable_mask = TRUE;
    }
    block->has_vertex_shader = TRUE;
    block->has_vertex_format = TRUE;
    for (index = 0; index < D9_MAX_VS_CONST_F; ++index)
        block->has_vs_const_f[index] = TRUE;
    for (index = 0; index < D9_MAX_CONST_I; ++index)
        block->has_vs_const_i[index] = TRUE;
    for (index = 0; index < D9_MAX_CONST_B; ++index)
        block->has_vs_const_b[index] = TRUE;
}

static void state_block_mark_all(struct D9StateBlock *block)
{
    UINT index, stage;
    state_block_mark_pixel_states(block);
    state_block_mark_vertex_states(block);
    for (index = 0; index < D9_MAX_RENDER_STATES; ++index)
        block->has_render_state[index] = TRUE;
    for (stage = 0; stage < D9_MAX_TEXTURE_STAGES; ++stage) {
        for (index = 0; index < D9_MAX_TEXTURE_STAGE_STATES; ++index)
            block->has_texture_stage_state[stage][index] = TRUE;
        block->has_texture[stage] = TRUE;
    }
    for (; stage < D9_MAX_TEXTURE_BINDINGS; ++stage)
        block->has_texture[stage] = TRUE;
    for (index = 0; index < D9_STATE_BLOCK_TRANSFORMS; ++index)
        block->has_transform[index] = TRUE;
    block->has_palette = TRUE;
    block->has_material = TRUE;
    FillMemory(block->has_clip_plane, sizeof(block->has_clip_plane), 1);
    block->has_viewport = TRUE;
    block->has_scissor = TRUE;
    for (index = 0; index < D9_MAX_STREAMS; ++index)
        block->has_stream[index] = TRUE;
    block->has_indices = TRUE;
}

static struct D9StateBlock *state_block_alloc(D9Device *device, BOOL parent_ref)
{
    struct D9StateBlock *block = (struct D9StateBlock *)HeapAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*block));
    if (!block)
        return NULL;
    block->iface.lpVtbl = &g_state_block_vtbl;
    block->refcount = 1;
    block->device = device;
    block->device_parent_ref = parent_ref;
    if (parent_ref) device_child_add_ref(device);
    block->next_device_resource = device->state_blocks;
    device->state_blocks = block;
    return block;
}

/* Replay ordinary state through public setters. Streams use their shared
 * internal setter to restore exact captured NULL metadata without the public
 * wrapper substituting the current live offset/stride. */
static HRESULT state_block_apply(struct D9StateBlock *block)
{
    D9Device *device = block->device;
    IDirect3DDevice9 *iface = &device->iface;
    UINT index, stage, slot;

    if (block->has_palette) {
        HRESULT hr = device_set_current_texture_palette(iface, block->current_palette);
        if (FAILED(hr)) return hr;
    }
    for (index = 0; index < D9_MAX_RENDER_STATES; ++index) {
        if (block->has_render_state[index])
            IDirect3DDevice9_SetRenderState(iface, (D3DRENDERSTATETYPE)index,
                    block->render_states[index]);
    }
    for (stage = 0; stage < D9_MAX_TEXTURE_BINDINGS; ++stage) {
        if (stage < D9_MAX_TEXTURE_STAGES) {
            for (index = 0; index < D9_MAX_TEXTURE_STAGE_STATES; ++index) {
                if (block->has_texture_stage_state[stage][index])
                    IDirect3DDevice9_SetTextureStageState(iface, stage,
                            (D3DTEXTURESTAGESTATETYPE)index,
                            block->texture_stage_states[stage][index]);
            }
        }
        if (block->has_texture[stage]) {
            IDirect3DBaseTexture9 *texture = NULL;
            if (block->textures[stage])
                texture = (IDirect3DBaseTexture9 *)&block->textures[stage]->iface;
            else if (block->cube_textures[stage])
                texture = (IDirect3DBaseTexture9 *)&block->cube_textures[stage]->iface;
            else if (block->volume_textures[stage])
                texture = (IDirect3DBaseTexture9 *)&block->volume_textures[stage]->iface;
            IDirect3DDevice9_SetTexture(iface, texture_binding_stage(stage),
                    texture);
        }
    }
    for (slot = 0; slot < D9_MAX_SAMPLERS; ++slot) {
        for (index = 0; index < D9_MAX_SAMPLER_STATES; ++index) {
            if (block->has_sampler_state[slot][index])
                IDirect3DDevice9_SetSamplerState(iface,
                        sampler_state_index(slot),
                        (D3DSAMPLERSTATETYPE)index,
                        block->sampler_states[slot][index]);
        }
    }
    for (index = 0; index < D9_STATE_BLOCK_TRANSFORMS; ++index) {
        if (block->has_transform[index])
            IDirect3DDevice9_SetTransform(iface,
                    (D3DTRANSFORMSTATETYPE)d9wg_state_block_transform_state(index),
                    (const D3DMATRIX *)block->transforms[index]);
    }
    if (block->has_material)
        IDirect3DDevice9_SetMaterial(iface, &block->material);
    for (index = 0; index < D9_MAX_CLIP_PLANES; ++index)
        if (block->has_clip_plane[index]) IDirect3DDevice9_SetClipPlane(iface, index, block->clip_planes[index]);
    if (block->has_npatch_mode) IDirect3DDevice9_SetNPatchMode(iface, block->npatch_segments);
    for (index = 0; index < D9_MAX_LIGHTS; ++index) {
        if (block->has_light[index])
            IDirect3DDevice9_SetLight(iface, index, &block->lights[index]);
        if (block->has_light_enable[index])
            IDirect3DDevice9_LightEnable(iface, index,
                    block->light_enabled[index]);
    }
    for (D9WGLightState *entry = block->extra_lights; entry; entry = entry->next) {
        if (entry->light_mask && entry->set) IDirect3DDevice9_SetLight(iface, entry->index, &entry->light);
        if (entry->enable_mask) IDirect3DDevice9_LightEnable(iface, entry->index, entry->enabled);
    }
    if (block->has_viewport)
        IDirect3DDevice9_SetViewport(iface, &block->viewport);
    if (block->has_scissor)
        IDirect3DDevice9_SetScissorRect(iface, &block->scissor_rect);
    if (block->has_vertex_shader)
        IDirect3DDevice9_SetVertexShader(iface, block->vertex_shader
                ? &block->vertex_shader->iface.vertex : NULL);
    if (block->has_pixel_shader)
        IDirect3DDevice9_SetPixelShader(iface, block->pixel_shader
                ? &block->pixel_shader->iface.pixel : NULL);
    for (index = 0; index < D9_MAX_VS_CONST_F; ++index) {
        if (block->has_vs_const_f[index])
            IDirect3DDevice9_SetVertexShaderConstantF(iface, index,
                    block->vs_const_f[index], 1);
    }
    for (index = 0; index < D9_MAX_PS_CONST_F; ++index) {
        if (block->has_ps_const_f[index])
            IDirect3DDevice9_SetPixelShaderConstantF(iface, index,
                    block->ps_const_f[index], 1);
    }
    for (index = 0; index < D9_MAX_CONST_I; ++index) {
        if (block->has_vs_const_i[index])
            IDirect3DDevice9_SetVertexShaderConstantI(iface, index,
                    block->vs_const_i[index], 1);
        if (block->has_ps_const_i[index])
            IDirect3DDevice9_SetPixelShaderConstantI(iface, index,
                    block->ps_const_i[index], 1);
    }
    for (index = 0; index < D9_MAX_CONST_B; ++index) {
        if (block->has_vs_const_b[index])
            IDirect3DDevice9_SetVertexShaderConstantB(iface, index,
                    &block->vs_const_b[index], 1);
        if (block->has_ps_const_b[index])
            IDirect3DDevice9_SetPixelShaderConstantB(iface, index,
                    &block->ps_const_b[index], 1);
    }
    if (block->has_vertex_format) {
        /* A declaration and an FVF are mutually exclusive in D3D9: whichever
         * the device last had is the one to restore, and restoring both would
         * leave the loser silently overriding the winner. */
        if (block->vertex_declaration)
            IDirect3DDevice9_SetVertexDeclaration(iface,
                    &block->vertex_declaration->iface);
        else if (block->fvf)
            IDirect3DDevice9_SetFVF(iface, block->fvf);
        else
            IDirect3DDevice9_SetVertexDeclaration(iface, NULL);
    }
    for (index = 0; index < D9_MAX_STREAMS; ++index) {
        if (block->has_stream[index]) {
            device_set_stream_binding(device, index,
                    block->streams[index].buffer,
                    block->streams[index].offset, block->streams[index].stride);
        }
        if (block->has_stream_frequency[index]) IDirect3DDevice9_SetStreamSourceFreq(iface, index, block->streams[index].frequency);
    }
    if (block->has_indices)
        IDirect3DDevice9_SetIndices(iface, block->index_buffer
                ? &block->index_buffer->iface : NULL);
    return D3D_OK;
}

static HRESULT WINAPI state_block_query_interface(IDirect3DStateBlock9 *iface,
        REFIID iid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = NULL;
    if (!iid || (!iid_is_unknown(iid)
            && !guid_equal(iid, &IID_IDirect3DStateBlock9)))
        return E_NOINTERFACE;
    *object = iface;
    IDirect3DStateBlock9_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI state_block_add_ref(IDirect3DStateBlock9 *iface)
{
    return (ULONG)InterlockedIncrement(&state_block_from_iface(iface)->refcount);
}

static ULONG WINAPI state_block_release(IDirect3DStateBlock9 *iface)
{
    struct D9StateBlock *block = state_block_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&block->refcount);
    if (!refs) {
        struct D9StateBlock **link = &block->device->state_blocks;
        while (*link && *link != block)
            link = &(*link)->next_device_resource;
        if (*link) *link = block->next_device_resource;
        state_block_release_references(block);
        d9wg_light_free(&block->extra_lights);
        D9Device *device = block->device;
        BOOL parent_ref = block->device_parent_ref;
        HeapFree(GetProcessHeap(), 0, block);
        if (parent_ref) device_child_release(device);
    }
    return refs;
}

static HRESULT WINAPI state_block_get_device(IDirect3DStateBlock9 *iface,
        IDirect3DDevice9 **device_out)
{
    struct D9StateBlock *block = state_block_from_iface(iface);
    if (!device_out)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *device_out = &block->device->iface;
    IDirect3DDevice9_AddRef(*device_out);
    return D3D_OK;
}

static HRESULT WINAPI state_block_capture_method(IDirect3DStateBlock9 *iface)
{
    D9StateBlock *block = state_block_from_iface(iface);
    if (block->device->recording_result)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    state_block_capture(block);
    return D3D_OK;
}

static HRESULT WINAPI state_block_apply_method(IDirect3DStateBlock9 *iface)
{
    struct D9StateBlock *block = state_block_from_iface(iface);
    if (block->device->recording_result)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    return state_block_apply(block);
}

static IDirect3DStateBlock9Vtbl g_state_block_vtbl = {
    .QueryInterface = state_block_query_interface,
    .AddRef = state_block_add_ref,
    .Release = state_block_release,
    .GetDevice = state_block_get_device,
    .Capture = state_block_capture_method,
    .Apply = state_block_apply_method
};

static HRESULT WINAPI device_create_state_block(IDirect3DDevice9 *iface,
        D3DSTATEBLOCKTYPE type, IDirect3DStateBlock9 **block_out)
{
    D9Device *device = device_from_iface(iface);
    struct D9StateBlock *block;

    if (!block_out)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *block_out = NULL;
    if (device->recording_result)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (type != D3DSBT_ALL && type != D3DSBT_PIXELSTATE
            && type != D3DSBT_VERTEXSTATE)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    block = state_block_alloc(device, TRUE);
    if (!block)
        return TRACE_REFUSE(E_OUTOFMEMORY);
    if (type == D3DSBT_ALL) state_block_mark_all(block);
    else if (type == D3DSBT_PIXELSTATE) state_block_mark_pixel_states(block);
    else state_block_mark_vertex_states(block);
    if (block->light_allocation_failed) {IDirect3DStateBlock9_Release(&block->iface);return TRACE_REFUSE(E_OUTOFMEMORY);}
    state_block_capture(block);
    *block_out = &block->iface;
    return D3D_OK;
}

static HRESULT WINAPI device_begin_state_block(IDirect3DDevice9 *iface)
{
    D9Device *device = device_from_iface(iface);
    if (device->recording_result) return TRACE_REFUSE(D3DERR_INVALIDCALL);
    D9StateBlock *block = state_block_alloc(device, FALSE);
    if (!block) return TRACE_REFUSE(E_OUTOFMEMORY);
    device->recording_result = block;
    return D3D_OK;
}

static HRESULT WINAPI device_end_state_block(IDirect3DDevice9 *iface,
        IDirect3DStateBlock9 **block_out)
{
    D9Device *device = device_from_iface(iface);
    D9StateBlock *block = device->recording_result;
    if (!block || !block_out) return TRACE_REFUSE(D3DERR_INVALIDCALL);
    device->recording_result = NULL;
    block->device_parent_ref = TRUE;
    device_child_add_ref(device);
    *block_out = &block->iface;
    return D3D_OK;
}

/* Device-owned8 tokens keep block storage, not a public device reference.
 * Detach each token before resource finalizers can re-enter this device. */
static void device_clear_state_block_handles8(D9Device *device)
{
    for (;;) {
        D9StateBlock *block = device->state_blocks;
        while (block && !block->handle8) block = block->next_device_resource;
        if (!block) break;
        block->handle8 = 0;
        IDirect3DStateBlock9_Release(&block->iface);
    }
}

/* ---- IDirect3DVertexBuffer9 ---- */

static HRESULT WINAPI vb_query_interface(IDirect3DVertexBuffer9 *iface,
        REFIID iid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = NULL;
    if (!iid || (!iid_is_unknown(iid)
            && !guid_equal(iid, &IID_IDirect3DResource9)
            && !guid_equal(iid, &IID_IDirect3DVertexBuffer9)))
        return E_NOINTERFACE;
    *object = iface;
    IDirect3DVertexBuffer9_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI vb_add_ref(IDirect3DVertexBuffer9 *iface)
{
    D9VertexBuffer *buffer = vb_from_iface(iface);
    ULONG refs = (ULONG)InterlockedIncrement(&buffer->refcount);
    if (refs == 1)
        device_child_add_ref(buffer->device);
    return refs;
}

static void vb_destroy_storage(D9VertexBuffer *buffer)
{
    D9VertexBuffer **link = &buffer->device->vertex_buffers;
    D9WGDestroyResource destroy;
    while (*link && *link != buffer)
        link = &(*link)->next_device_resource;
    if (*link) *link = buffer->next_device_resource;
    d9wg_private_clear(&buffer->private_data, &d9wg_private_ops);
    destroy.resource_handle = buffer->handle;
    destroy.resource_kind = D9WG_RESOURCE_BUFFER_VERTEX;
    emit_command(D9WG_OP_DESTROY_RESOURCE, &destroy, sizeof(destroy));
    d9wg_clip_buffer_free(&buffer->clips, d9wg_vertex_windows_clip_release, NULL);
    HeapFree(GetProcessHeap(), 0, buffer->shadow);
    core_view_retire8(buffer);
    HeapFree(GetProcessHeap(), 0, buffer);
}

static ULONG WINAPI vb_release(IDirect3DVertexBuffer9 *iface)
{
    D9VertexBuffer *buffer = vb_from_iface(iface);
    if (!InterlockedCompareExchange(&buffer->refcount, 0, 0))
        return 0;
    ULONG refs = (ULONG)InterlockedDecrement(&buffer->refcount);
    if (!refs) {
        D9Device *device = buffer->device;
        if (!InterlockedCompareExchange(&buffer->binding_refs, 0, 0))
            vb_destroy_storage(buffer);
        device_child_release(device);
    }
    return refs;
}

static HRESULT WINAPI vb_get_device(IDirect3DVertexBuffer9 *iface,
        IDirect3DDevice9 **device_out)
{
    D9VertexBuffer *buffer = vb_from_iface(iface);
    if (!device_out)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *device_out = &buffer->device->iface;
    IDirect3DDevice9_AddRef(*device_out);
    return D3D_OK;
}

static HRESULT WINAPI vb_set_private_data(IDirect3DVertexBuffer9* iface,
    REFGUID guid, const void* data, DWORD size, DWORD flags)
{
    D9VertexBuffer* buffer = vb_from_iface(iface);
    return TRACE_REFUSE(d9wg_private_hresult(d9wg_private_set(&buffer->private_data, guid, data, size, flags, &d9wg_private_ops)));
}

static HRESULT WINAPI vb_get_private_data(IDirect3DVertexBuffer9* iface,
    REFGUID guid, void* data, DWORD* size)
{
    D9VertexBuffer* buffer = vb_from_iface(iface);
    return TRACE_REFUSE(d9wg_get_private_data(&buffer->private_data, guid, data, size));
}

static HRESULT WINAPI vb_free_private_data(IDirect3DVertexBuffer9* iface,
    REFGUID guid)
{
    D9VertexBuffer* buffer = vb_from_iface(iface);
    return TRACE_REFUSE(d9wg_private_hresult(d9wg_private_free(&buffer->private_data, guid, &d9wg_private_ops)));
}

static DWORD WINAPI vb_set_priority(IDirect3DVertexBuffer9 *iface,
        DWORD priority)
{
    D9VertexBuffer *buffer = vb_from_iface(iface);
    DWORD old = buffer->priority;
    buffer->priority = priority;
    return old;
}

static DWORD WINAPI vb_get_priority(IDirect3DVertexBuffer9 *iface)
{ return vb_from_iface(iface)->priority; }

static void WINAPI vb_preload(IDirect3DVertexBuffer9 *iface)
{ (void)iface; }

static D3DRESOURCETYPE WINAPI vb_get_type(IDirect3DVertexBuffer9 *iface)
{ (void)iface; return D3DRTYPE_VERTEXBUFFER; }

static HRESULT WINAPI vb_lock(IDirect3DVertexBuffer9 *iface, UINT offset,
        UINT size, void **data_out, DWORD flags)
{
    D9VertexBuffer *buffer = vb_from_iface(iface);
    if (!data_out || buffer->locked || offset > buffer->length)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!size)
        size = buffer->length - offset;
    if (size > buffer->length - offset)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    buffer->locked = TRUE;
    buffer->lock_offset = offset;
    buffer->lock_size = size;
    flags = d9wg_buffer_lock_flags(buffer->usage, buffer->pool, flags);
    buffer->lock_flags = flags;
    if (flags & D3DLOCK_DISCARD) {
        d9wg_clip_buffer_free(&buffer->clips, d9wg_vertex_windows_clip_release, NULL);
    }
    *data_out = buffer->shadow + offset;
    return D3D_OK;
}

static HRESULT WINAPI vb_unlock(IDirect3DVertexBuffer9 *iface)
{
    D9VertexBuffer *buffer = vb_from_iface(iface);
    BOOL result;
    if (!buffer->locked)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    result = (buffer->lock_flags & D3DLOCK_READONLY)
            || emit_buffer_update(buffer->handle, buffer->lock_offset,
                    buffer->shadow + buffer->lock_offset, buffer->lock_size,
                    buffer->lock_flags);
    buffer->locked = FALSE;
    buffer->lock_offset = 0;
    buffer->lock_size = 0;
    buffer->lock_flags = 0;
    return result ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI vb_get_desc(IDirect3DVertexBuffer9 *iface,
        D3DVERTEXBUFFER_DESC *desc)
{
    D9VertexBuffer *buffer = vb_from_iface(iface);
    if (!desc)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    ZeroMemory(desc, sizeof(*desc));
    desc->Format = D3DFMT_VERTEXDATA;
    desc->Type = D3DRTYPE_VERTEXBUFFER;
    desc->Usage = buffer->usage;
    desc->Pool = buffer->pool;
    desc->Size = buffer->length;
    desc->FVF = buffer->fvf;
    return D3D_OK;
}

/* ---- IDirect3DIndexBuffer9 ---- */

static HRESULT WINAPI ib_query_interface(IDirect3DIndexBuffer9 *iface,
        REFIID iid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = NULL;
    if (!iid || (!iid_is_unknown(iid)
            && !guid_equal(iid, &IID_IDirect3DResource9)
            && !guid_equal(iid, &IID_IDirect3DIndexBuffer9)))
        return E_NOINTERFACE;
    *object = iface;
    IDirect3DIndexBuffer9_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI ib_add_ref(IDirect3DIndexBuffer9 *iface)
{
    D9IndexBuffer *buffer = ib_from_iface(iface);
    ULONG refs = (ULONG)InterlockedIncrement(&buffer->refcount);
    if (refs == 1)
        device_child_add_ref(buffer->device);
    return refs;
}

static void ib_destroy_storage(D9IndexBuffer *buffer)
{
    D9IndexBuffer **link = &buffer->device->index_buffers;
    D9WGDestroyResource destroy;
    while (*link && *link != buffer)
        link = &(*link)->next_device_resource;
    if (*link) *link = buffer->next_device_resource;
    d9wg_private_clear(&buffer->private_data, &d9wg_private_ops);
    destroy.resource_handle = buffer->handle;
    destroy.resource_kind = D9WG_RESOURCE_BUFFER_INDEX;
    emit_command(D9WG_OP_DESTROY_RESOURCE, &destroy, sizeof(destroy));
    HeapFree(GetProcessHeap(), 0, buffer->shadow);
    core_view_retire8(buffer);
    HeapFree(GetProcessHeap(), 0, buffer);
}

static ULONG WINAPI ib_release(IDirect3DIndexBuffer9 *iface)
{
    D9IndexBuffer *buffer = ib_from_iface(iface);
    if (!InterlockedCompareExchange(&buffer->refcount, 0, 0))
        return 0;
    ULONG refs = (ULONG)InterlockedDecrement(&buffer->refcount);
    if (!refs) {
        D9Device *device = buffer->device;
        if (!InterlockedCompareExchange(&buffer->binding_refs, 0, 0))
            ib_destroy_storage(buffer);
        device_child_release(device);
    }
    return refs;
}

static HRESULT WINAPI ib_get_device(IDirect3DIndexBuffer9 *iface,
        IDirect3DDevice9 **device_out)
{
    D9IndexBuffer *buffer = ib_from_iface(iface);
    if (!device_out)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *device_out = &buffer->device->iface;
    IDirect3DDevice9_AddRef(*device_out);
    return D3D_OK;
}

static HRESULT WINAPI ib_set_private_data(IDirect3DIndexBuffer9* iface,
    REFGUID guid, const void* data, DWORD size, DWORD flags)
{
    D9IndexBuffer* buffer = ib_from_iface(iface);
    return TRACE_REFUSE(d9wg_private_hresult(d9wg_private_set(&buffer->private_data, guid, data, size, flags, &d9wg_private_ops)));
}

static HRESULT WINAPI ib_get_private_data(IDirect3DIndexBuffer9* iface,
    REFGUID guid, void* data, DWORD* size)
{
    D9IndexBuffer* buffer = ib_from_iface(iface);
    return TRACE_REFUSE(d9wg_get_private_data(&buffer->private_data, guid, data, size));
}

static HRESULT WINAPI ib_free_private_data(IDirect3DIndexBuffer9* iface,
    REFGUID guid)
{
    D9IndexBuffer* buffer = ib_from_iface(iface);
    return TRACE_REFUSE(d9wg_private_hresult(d9wg_private_free(&buffer->private_data, guid, &d9wg_private_ops)));
}

static DWORD WINAPI ib_set_priority(IDirect3DIndexBuffer9 *iface,
        DWORD priority)
{
    D9IndexBuffer *buffer = ib_from_iface(iface);
    DWORD old = buffer->priority;
    buffer->priority = priority;
    return old;
}

static DWORD WINAPI ib_get_priority(IDirect3DIndexBuffer9 *iface)
{ return ib_from_iface(iface)->priority; }

static void WINAPI ib_preload(IDirect3DIndexBuffer9 *iface)
{ (void)iface; }

static D3DRESOURCETYPE WINAPI ib_get_type(IDirect3DIndexBuffer9 *iface)
{ (void)iface; return D3DRTYPE_INDEXBUFFER; }

static HRESULT WINAPI ib_lock(IDirect3DIndexBuffer9 *iface, UINT offset,
        UINT size, void **data_out, DWORD flags)
{
    D9IndexBuffer *buffer = ib_from_iface(iface);
    if (!data_out || buffer->locked || offset > buffer->length)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!size)
        size = buffer->length - offset;
    if (size > buffer->length - offset)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    buffer->locked = TRUE;
    buffer->lock_offset = offset;
    buffer->lock_size = size;
    flags = d9wg_buffer_lock_flags(buffer->usage, buffer->pool, flags);
    buffer->lock_flags = flags;
    *data_out = buffer->shadow + offset;
    return D3D_OK;
}

static HRESULT WINAPI ib_unlock(IDirect3DIndexBuffer9 *iface)
{
    D9IndexBuffer *buffer = ib_from_iface(iface);
    BOOL result;
    if (!buffer->locked)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    result = (buffer->lock_flags & D3DLOCK_READONLY)
            || emit_buffer_update(buffer->handle, buffer->lock_offset,
                    buffer->shadow + buffer->lock_offset, buffer->lock_size,
                    buffer->lock_flags);
    buffer->locked = FALSE;
    buffer->lock_offset = 0;
    buffer->lock_size = 0;
    buffer->lock_flags = 0;
    return result ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI ib_get_desc(IDirect3DIndexBuffer9 *iface,
        D3DINDEXBUFFER_DESC *desc)
{
    D9IndexBuffer *buffer = ib_from_iface(iface);
    if (!desc)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    ZeroMemory(desc, sizeof(*desc));
    desc->Format = buffer->format;
    desc->Type = D3DRTYPE_INDEXBUFFER;
    desc->Usage = buffer->usage;
    desc->Pool = buffer->pool;
    desc->Size = buffer->length;
    return D3D_OK;
}

/* ---- IDirect3DTexture9 ---- */

static HRESULT texture_lock_level(D9Texture *texture, UINT level,
        D3DLOCKED_RECT *locked_rect, const RECT *rect, DWORD flags)
{
    D9TextureLevel *level_data;
    RECT area;
    UINT block_width;
    UINT block_height;
    UINT block_bytes;
    UINT block_x;
    UINT block_y;

    if (!locked_rect || level >= texture->level_count
            || (texture->usage & (D3DUSAGE_RENDERTARGET
                | D3DUSAGE_DEPTHSTENCIL)))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    level_data = &texture->levels[level];
    if (level_data->locked || !level_data->shadow)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (rect) {
        area = *rect;
    } else {
        SetRect(&area, 0, 0, (int)level_data->width, (int)level_data->height);
    }
    if (area.left < 0 || area.top < 0 || area.right <= area.left
            || area.bottom <= area.top
            || (UINT)area.right > level_data->width
            || (UINT)area.bottom > level_data->height
            || !d9wg_format_layout(device_compatibility_api(texture->device),
                    texture->format, &block_width,
                    &block_height, &block_bytes))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (block_width > 1
            && (((UINT)area.left % block_width)
                || ((UINT)area.top % block_height)
                || ((UINT)area.right != level_data->width
                    && (UINT)area.right % block_width)
                || ((UINT)area.bottom != level_data->height
                    && (UINT)area.bottom % block_height)))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);

    block_x = (UINT)area.left / block_width;
    block_y = (UINT)area.top / block_height;
    level_data->lock_rect = area;
    level_data->lock_flags = flags;
    level_data->locked = TRUE;
    locked_rect->Pitch = (INT)level_data->row_pitch;
    locked_rect->pBits = level_data->shadow
            + block_y * level_data->row_pitch + block_x * block_bytes;
    return D3D_OK;
}

static HRESULT texture_unlock_level(D9Texture *texture, UINT level)
{
    D9TextureLevel *level_data;
    BOOL result = TRUE;

    if (level >= texture->level_count)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    level_data = &texture->levels[level];
    if (!level_data->locked)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!(level_data->lock_flags & D3DLOCK_READONLY))
        result = emit_texture_update(texture, level, &level_data->lock_rect);
    level_data->locked = FALSE;
    level_data->lock_flags = 0;
    ZeroMemory(&level_data->lock_rect, sizeof(level_data->lock_rect));
    return result ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI texture_query_interface(IDirect3DTexture9 *iface,
        REFIID iid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = NULL;
    if (!iid || (!iid_is_unknown(iid)
            && !guid_equal(iid, &IID_IDirect3DResource9)
            && !guid_equal(iid, &IID_IDirect3DBaseTexture9)
            && !guid_equal(iid, &IID_IDirect3DTexture9)))
        return E_NOINTERFACE;
    *object = iface;
    IDirect3DTexture9_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI texture_add_ref(IDirect3DTexture9 *iface)
{
    D9Texture *texture = texture_from_iface(iface);
    ULONG refs = (ULONG)InterlockedIncrement(&texture->refcount);
    if (refs == 1 && !texture->private_back_buffer)
        device_child_add_ref(texture->device);
    return refs;
}

static void texture_destroy_storage(D9Texture *texture)
{
    D9Texture** link = &texture->device->texture_resources;
    D9WGDestroyResource destroy;
    UINT level;
    while (*link && *link != texture)
        link = &(*link)->next_device_resource;
    if (*link)
        *link = texture->next_device_resource;
    d9wg_private_clear(&texture->private_data, &d9wg_private_ops);
    destroy.resource_handle = texture->handle;
    destroy.resource_kind = D9WG_RESOURCE_TEXTURE_2D;
    emit_command(D9WG_OP_DESTROY_RESOURCE, &destroy, sizeof(destroy));
    for (level = 0; level < texture->level_count; ++level) {
        d9wg_private_clear(&texture->levels[level].private_data, &d9wg_private_ops);
        /* The level's surface sub-object dies with the texture; nothing
         * can still hold it, because every surface reference is one of the
         * texture references that just reached zero. */
        if (texture->levels[level].level_surface) {
            TRACE_SURFACE_FREED(
                &texture->levels[level].level_surface->iface);
            core_view_retire8(texture->levels[level].level_surface);
            HeapFree(GetProcessHeap(), 0,
                texture->levels[level].level_surface);
            texture->levels[level].level_surface = NULL;
        }
        HeapFree(GetProcessHeap(), 0, texture->levels[level].shadow);
    }
    HeapFree(GetProcessHeap(), 0, texture->levels);
    core_view_retire8(texture);
    HeapFree(GetProcessHeap(), 0, texture);
}

static ULONG WINAPI texture_release(IDirect3DTexture9* iface)
{
    D9Texture *texture = texture_from_iface(iface);
    if (!InterlockedCompareExchange(&texture->refcount, 0, 0))
        return 0;
    ULONG refs = (ULONG)InterlockedDecrement(&texture->refcount);
    if (!refs) {
        D9Device *device = texture->device;
        BOOL private_storage = texture->private_back_buffer;
        if (!InterlockedCompareExchange(&texture->binding_refs, 0, 0))
            texture_destroy_storage(texture);
        /* May destroy both objects through final binding cleanup. */
        if (!private_storage)
            device_child_release(device);
    }
    return refs;
}

static HRESULT WINAPI texture_get_device(IDirect3DTexture9 *iface,
        IDirect3DDevice9 **device_out)
{
    D9Texture *texture = texture_from_iface(iface);
    if (!device_out)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *device_out = &texture->device->iface;
    IDirect3DDevice9_AddRef(*device_out);
    return D3D_OK;
}

static HRESULT WINAPI texture_set_private_data(IDirect3DTexture9* iface,
    REFGUID guid, const void* data, DWORD size, DWORD flags)
{
    D9WGPrivateStore* store = &texture_from_iface(iface)->private_data;
    return TRACE_REFUSE(d9wg_private_hresult(d9wg_private_set(store, guid, data, size, flags, &d9wg_private_ops)));
}

static HRESULT WINAPI texture_get_private_data(IDirect3DTexture9* iface,
    REFGUID guid, void* data, DWORD* size)
{
    D9WGPrivateStore* store = &texture_from_iface(iface)->private_data;
    return TRACE_REFUSE(d9wg_get_private_data(store, guid, data, size));
}

static HRESULT WINAPI texture_free_private_data(IDirect3DTexture9* iface,
    REFGUID guid)
{
    D9WGPrivateStore* store = &texture_from_iface(iface)->private_data;
    return TRACE_REFUSE(d9wg_private_hresult(d9wg_private_free(store, guid, &d9wg_private_ops)));
}

static DWORD WINAPI texture_set_priority(IDirect3DTexture9 *iface,
        DWORD priority)
{
    D9Texture *texture = texture_from_iface(iface);
    DWORD old = texture->priority;
    texture->priority = priority;
    return old;
}

static DWORD WINAPI texture_get_priority(IDirect3DTexture9 *iface)
{ return texture_from_iface(iface)->priority; }

static void WINAPI texture_preload(IDirect3DTexture9 *iface)
{ (void)iface; }

static D3DRESOURCETYPE WINAPI texture_get_type(IDirect3DTexture9 *iface)
{ (void)iface; return D3DRTYPE_TEXTURE; }

static DWORD WINAPI texture_set_lod(IDirect3DTexture9 *iface, DWORD lod)
{
    D9Texture *texture = texture_from_iface(iface);
    return d9wg_set_texture_lod(texture->device->handle, texture->handle,
            texture->pool, texture->level_count, &texture->lod, lod);
}

static DWORD WINAPI texture_get_lod(IDirect3DTexture9 *iface)
{ return texture_from_iface(iface)->lod; }

static DWORD WINAPI texture_get_level_count(IDirect3DTexture9 *iface)
{ return texture_from_iface(iface)->level_count; }

/* One command carries both filter changes and explicit generation. Old
 * eight-byte commands retain their immediate-generation semantics. */
static BOOL emit_generate_mips(D9Device *device, uint32_t resource_handle,
        D3DTEXTUREFILTERTYPE filter, BOOL defer_generation)
{
    D9WGGenerateMips command;
    command.device_handle = device->handle;
    command.resource_handle = resource_handle;
    command.filter_type = filter;
    command.defer_generation = defer_generation ? 1u : 0u;
    return emit_command(D9WG_OP_GENERATE_MIPS, &command, sizeof(command));
}



/* Filtering follows the enabled per-format device support. Setters dirty
 * without immediately generating; creation and Reset replay the stored filter. */
static HRESULT WINAPI texture_set_auto_gen_filter_type(
        IDirect3DTexture9 *iface, D3DTEXTUREFILTERTYPE filter)
{
    D9Texture *texture = texture_from_iface(iface);
    if (!(texture->usage & D3DUSAGE_AUTOGENMIPMAP))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!d9wg_autogen_filter_supported(texture->format, filter,
                (d9wg_format_query_info(texture->format) & 4u) ? runtime_host_features() : 0))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!emit_generate_mips(texture->device, texture->handle, filter, TRUE))
        return D3DERR_DRIVERINTERNALERROR;
    texture->autogen_filter = filter;
    return D3D_OK;
}

static D3DTEXTUREFILTERTYPE WINAPI texture_get_auto_gen_filter_type(
        IDirect3DTexture9 *iface)
{
    D9Texture *texture = texture_from_iface(iface);
    if (!(texture->usage & D3DUSAGE_AUTOGENMIPMAP))
        return D3DTEXF_NONE;
    return texture->autogen_filter ? texture->autogen_filter : D3DTEXF_LINEAR;
}

static void WINAPI texture_generate_mip_sublevels(IDirect3DTexture9 *iface)
{
    D9Texture *texture = texture_from_iface(iface);
    if (!(texture->usage & D3DUSAGE_AUTOGENMIPMAP))
        return;
    emit_generate_mips(texture->device, texture->handle, texture->autogen_filter, FALSE);
}

static HRESULT WINAPI texture_get_level_desc(IDirect3DTexture9 *iface,
        UINT level, D3DSURFACE_DESC *desc)
{
    D9Texture *texture = texture_from_iface(iface);
    D9TextureLevel *level_data;
    if (!desc || level >= texture->level_count)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    level_data = &texture->levels[level];
    ZeroMemory(desc, sizeof(*desc));
    desc->Format = texture->format;
    desc->Type = D3DRTYPE_SURFACE;
    desc->Usage = texture->usage;
    desc->Pool = texture->pool;
    desc->MultiSampleType = texture->multisample;
    desc->MultiSampleQuality = texture->multisample_quality;
    desc->Width = level_data->width;
    desc->Height = level_data->height;
    return D3D_OK;
}

/*
 * Returns a surface view onto one texture level. Originally stubbed out on
 * the assumption that an app would always upload through
 * IDirect3DTexture9::LockRect directly -- Warcraft III does not: it takes the
 * level's surface and locks that, so failing here meant its textures were
 * created and bound but never received a single byte of pixel data (117
 * textures created, 0 uploads), rendering the whole scene black.
 *
 * The surface forwards Lock/Unlock to the same per-level shadow storage and
 * UPDATE_TEXTURE emitter the texture's own LockRect uses, so both upload
 * routes stay identical.
 *
 * D3D9 does not create a fresh surface per call: each level owns one surface
 * sub-object, returned by every GetSurfaceLevel for that level and kept alive
 * by the texture, with AddRef/Release forwarded to the texture's refcount.
 * Handing back a new self-owning surface each time instead is what crashed
 * Kart Rider -- it locks a level surface, releases its reference (legal, the
 * texture still owns the surface), then calls UnlockRect through the pointer
 * it kept.  Under the old code that Release freed the object, so UnlockRect
 * read a zeroed vtable and jumped through a null pointer.
 */
static HRESULT WINAPI texture_get_surface_level(IDirect3DTexture9 *iface,
        UINT level, IDirect3DSurface9 **surface_out)
{
    D9Texture *texture = texture_from_iface(iface);
    D9Surface *surface;

    TRACE_MARK_ENTER("Texture.GetSurfaceLevel");
    TRACE("ENTER Texture.GetSurfaceLevel texture=%08lX handle=%08lX "
            "ref=%ld level=%lu outarg=%08lX",
            (DWORD)(uintptr_t)iface, texture->handle,
            InterlockedCompareExchange(&texture->refcount, 0, 0), level,
            (DWORD)(uintptr_t)surface_out);
    if (!surface_out) {
        TRACE_MARK_EXIT("Texture.GetSurfaceLevel", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    *surface_out = NULL;
    if (level >= texture->level_count) {
        TRACE_MARK_EXIT("Texture.GetSurfaceLevel", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    surface = texture->levels[level].level_surface;
    if (!surface) {
        surface = (D9Surface *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                sizeof(*surface));
        if (!surface) {
            TRACE_MARK_EXIT("Texture.GetSurfaceLevel", E_OUTOFMEMORY, NULL);
            return E_OUTOFMEMORY;
        }
        surface->iface.lpVtbl = &g_surface_vtbl;
        /* Unused while texture_child is set: the texture's refcount is this
         * surface's refcount. */
        surface->refcount = 0;
        surface->device = texture->device;
        surface->texture = texture;
        surface->texture_child = TRUE;
        surface->level = level;
        surface->width = texture->levels[level].width;
        surface->height = texture->levels[level].height;
        surface->format = texture->format;
        surface->pool = texture->pool;
        surface->multisample = texture->multisample;
        surface->multisample_quality = texture->multisample_quality;
        texture->levels[level].level_surface = surface;
        TRACE_REGISTER_RANGE("SURFACE_TEXTURE_LEVEL", level, surface, surface,
                sizeof(*surface));
        TRACE("SURFACE CREATE kind=texture_level object=%08lX vtbl=%08lX "
                "device=%08lX texture=%08lX texture_handle=%08lX "
                "swapchain=00000000 shadow=%08lX level=%lu size=%lux%lu "
                "format=%08lX",
                (DWORD)(uintptr_t)&surface->iface,
                (DWORD)(uintptr_t)surface->iface.lpVtbl,
                (DWORD)(uintptr_t)&texture->device->iface,
                (DWORD)(uintptr_t)&texture->iface, texture->handle,
                (DWORD)(uintptr_t)texture->levels[level].shadow, level,
                surface->width, surface->height, (DWORD)surface->format);
    }
    /* The reference handed to the caller is a reference on the texture. */
    IDirect3DTexture9_AddRef(iface);
    *surface_out = &surface->iface;
    TRACE("SURFACE HANDOUT kind=texture_level object=%08lX texture=%08lX "
            "texture_handle=%08lX level=%lu texture_refs=%ld",
            (DWORD)(uintptr_t)*surface_out, (DWORD)(uintptr_t)&texture->iface,
            texture->handle, level,
            InterlockedCompareExchange(&texture->refcount, 0, 0));
    TRACE_MARK_EXIT("Texture.GetSurfaceLevel", D3D_OK, *surface_out);
    return D3D_OK;
}

static HRESULT WINAPI texture_lock_rect(IDirect3DTexture9 *iface, UINT level,
        D3DLOCKED_RECT *locked_rect, const RECT *rect, DWORD flags)
{
    return texture_lock_level(texture_from_iface(iface), level, locked_rect,
            rect, flags);
}

static HRESULT WINAPI texture_unlock_rect(IDirect3DTexture9 *iface,
        UINT level)
{
    return texture_unlock_level(texture_from_iface(iface), level);
}

static HRESULT WINAPI texture_add_dirty_rect(IDirect3DTexture9 *iface,
        const RECT *rect)
{
    D9Texture *texture = texture_from_iface(iface);
    if (rect && (rect->left < 0 || rect->top < 0
            || rect->right <= rect->left || rect->bottom <= rect->top
            || (UINT)rect->right > texture->width
            || (UINT)rect->bottom > texture->height))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    return D3D_OK;
}

/* ---- IDirect3DVertexDeclaration9 ---- */

static HRESULT WINAPI decl_query_interface(IDirect3DVertexDeclaration9 *iface,
        REFIID iid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = NULL;
    if (!iid || (!iid_is_unknown(iid)
            && !guid_equal(iid, &IID_IDirect3DVertexDeclaration9)))
        return E_NOINTERFACE;
    *object = iface;
    IDirect3DVertexDeclaration9_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI decl_add_ref(IDirect3DVertexDeclaration9 *iface)
{
    D9VertexDeclaration *decl = decl_from_iface(iface);
    ULONG refs = (ULONG)InterlockedIncrement(&decl->refcount);
    if (refs == 1) device_child_add_ref(decl->device);
    return refs;
}

static void decl_destroy_storage(D9VertexDeclaration *decl)
{
    D9VertexDeclaration **link = &decl->device->vertex_declarations;
    D9WGDestroyResource destroy;
    while (*link && *link != decl)
        link = &(*link)->next_device_resource;
    if (*link) *link = decl->next_device_resource;
    destroy.resource_handle = decl->handle;
    destroy.resource_kind = D9WG_RESOURCE_VERTEX_DECLARATION;
    emit_command(D9WG_OP_DESTROY_RESOURCE, &destroy, sizeof(destroy));
    HeapFree(GetProcessHeap(), 0, decl);
}

static ULONG WINAPI decl_release(IDirect3DVertexDeclaration9 *iface)
{
    D9VertexDeclaration *decl = decl_from_iface(iface);
    if (!InterlockedCompareExchange(&decl->refcount, 0, 0)) return 0;
    ULONG refs = (ULONG)InterlockedDecrement(&decl->refcount);
    if (!refs) {
        D9Device *device = decl->device;
        if (!InterlockedCompareExchange(&decl->binding_refs, 0, 0))
            decl_destroy_storage(decl);
        device_child_release(device);
    }
    return refs;
}

static HRESULT WINAPI decl_get_device(IDirect3DVertexDeclaration9 *iface,
        IDirect3DDevice9 **device_out)
{
    D9VertexDeclaration *decl = decl_from_iface(iface);
    if (!device_out)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *device_out = &decl->device->iface;
    IDirect3DDevice9_AddRef(*device_out);
    return D3D_OK;
}

static HRESULT WINAPI decl_get_declaration(IDirect3DVertexDeclaration9 *iface,
        D3DVERTEXELEMENT9 *elements, UINT *count_out)
{
    D9VertexDeclaration *decl = decl_from_iface(iface);
    UINT count = decl->element_count + 1; /* includes D3DDECL_END */

    if (!count_out)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);

    /* pNumElements is [out], not an in/out capacity. The D3D9 contract puts
     * responsibility for allocating MAXD3DDECLLENGTH elements on the caller;
     * Microsoft's own sample deliberately leaves this UINT uninitialised.
     * Reading it here made RenderWare's valid GetDeclaration call fail with
     * D3DERR_INVALIDCALL when GTA SA began streaming the first world sector,
     * after which the game dereferenced the declaration it expected back. */
    if (elements) {
        CopyMemory(elements, decl->elements,
                decl->element_count * sizeof(D3DVERTEXELEMENT9));
        {
            D3DVERTEXELEMENT9 end = D3DDECL_END();
            elements[decl->element_count] = end;
        }
    }
    *count_out = count;
    TRACE("OK VertexDeclaration.GetDeclaration object=%08lX elements=%08lX count=%lu",
            (DWORD)(uintptr_t)iface, (DWORD)(uintptr_t)elements, count);
    return D3D_OK;
}

/* ---- IDirect3DVertexShader9 / IDirect3DPixelShader9 ----
 *
 * Both interfaces are IUnknown + GetDevice + GetFunction with identical
 * layouts, so the bodies below are shared through D9Shader and only the
 * vtable entry points differ. GetFunction hands back the shadow copy taken
 * at creation, which is what a game's own shader-cache or effect-reload path
 * reads back. */

static HRESULT shader_query_interface(D9Shader *shader, REFIID iid,
        void **object, const void *interface_guid)
{
    if (!object)
        return E_POINTER;
    *object = NULL;
    if (!iid || (!iid_is_unknown(iid) && !guid_equal(iid, interface_guid)))
        return E_NOINTERFACE;
    *object = &shader->iface;
    shader_public_add_ref(shader);
    return S_OK;
}

static HRESULT shader_get_function(D9Shader *shader, void *data, UINT *size)
{
    UINT byte_count = shader->token_count * 4u;
    if (!size)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!data) {
        *size = byte_count;
        return D3D_OK;
    }
    if (*size < byte_count)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    CopyMemory(data, shader->code, byte_count);
    *size = byte_count;
    return D3D_OK;
}

static HRESULT WINAPI vertex_shader_query_interface(
        IDirect3DVertexShader9 *iface, REFIID iid, void **object)
{
    return shader_query_interface(vertex_shader_from_iface(iface), iid, object,
            &IID_IDirect3DVertexShader9);
}

static ULONG WINAPI vertex_shader_add_ref(IDirect3DVertexShader9 *iface)
{
    return shader_public_add_ref(vertex_shader_from_iface(iface));
}

static ULONG WINAPI vertex_shader_release(IDirect3DVertexShader9 *iface)
{
    return shader_public_release(vertex_shader_from_iface(iface));
}

static HRESULT WINAPI vertex_shader_get_device(IDirect3DVertexShader9 *iface,
        IDirect3DDevice9 **device_out)
{
    D9Shader *shader = vertex_shader_from_iface(iface);
    if (!device_out)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *device_out = &shader->device->iface;
    IDirect3DDevice9_AddRef(*device_out);
    return D3D_OK;
}

static HRESULT WINAPI vertex_shader_get_function(IDirect3DVertexShader9 *iface,
        void *data, UINT *size)
{
    return shader_get_function(vertex_shader_from_iface(iface), data, size);
}

static HRESULT WINAPI pixel_shader_query_interface(
        IDirect3DPixelShader9 *iface, REFIID iid, void **object)
{
    return shader_query_interface(pixel_shader_from_iface(iface), iid, object,
            &IID_IDirect3DPixelShader9);
}

static ULONG WINAPI pixel_shader_add_ref(IDirect3DPixelShader9 *iface)
{
    return shader_public_add_ref(pixel_shader_from_iface(iface));
}

static ULONG WINAPI pixel_shader_release(IDirect3DPixelShader9 *iface)
{
    return shader_public_release(pixel_shader_from_iface(iface));
}

static HRESULT WINAPI pixel_shader_get_device(IDirect3DPixelShader9 *iface,
        IDirect3DDevice9 **device_out)
{
    D9Shader *shader = pixel_shader_from_iface(iface);
    if (!device_out)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *device_out = &shader->device->iface;
    IDirect3DDevice9_AddRef(*device_out);
    return D3D_OK;
}

static HRESULT WINAPI pixel_shader_get_function(IDirect3DPixelShader9 *iface,
        void *data, UINT *size)
{
    return shader_get_function(pixel_shader_from_iface(iface), data, size);
}

/* ---- IDirect3DSurface9 (GetBackBuffer only; see the struct comment) ---- */

static HRESULT WINAPI surface_query_interface(IDirect3DSurface9 *iface,
        REFIID iid, void **object)
{
    if (!object)
        return E_POINTER;
    *object = NULL;
    if (!iid || (!iid_is_unknown(iid)
            && !guid_equal(iid, &IID_IDirect3DResource9)
            && !guid_equal(iid, &IID_IDirect3DSurface9)))
        return E_NOINTERFACE;
    *object = iface;
    IDirect3DSurface9_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI surface_add_ref(IDirect3DSurface9 *iface)
{
    D9Surface *surface = surface_from_iface(iface);
    LONG before = 0;
    ULONG refs;

    if (surface->swap_chain && surface->swap_chain->handle) {
        refs = (ULONG)InterlockedIncrement(&surface->refcount);
        if (refs == 1)
            device_child_add_ref(surface->device);
        return refs;
    }

    /* A texture level surface has no refcount of its own: it is a sub-object
     * of the texture and shares its parent's, so that dropping the last
     * surface reference cannot outlive-check the pointer the app kept. */
    if (surface->cube_texture) {
        TRACE_MARK_ENTER("Surface.AddRef");
        refs = IDirect3DCubeTexture9_AddRef(&surface->cube_texture->iface);
        TRACE_MARK_EXIT("Surface.AddRef", (HRESULT)refs, iface);
        return refs;
    }
    if (surface->texture_child) {
        TRACE_MARK_ENTER("Surface.AddRef");
        refs = IDirect3DTexture9_AddRef(&surface->texture->iface);
        TRACE("SURFACE ADDREF object=%08lX forwarded_to_texture=%08lX "
                "level=%lu texture_refs=%lu", (DWORD)(uintptr_t)iface,
                (DWORD)(uintptr_t)&surface->texture->iface, surface->level,
                refs);
        TRACE_MARK_EXIT("Surface.AddRef", (HRESULT)refs, iface);
        return refs;
    }
    /* The implicit back buffer and auto depth-stencil are sub-objects of the
     * device. Transitioning from zero to one surface reference pins that device;
     * subsequent AddRefs leave its count unchanged. The last surface reference
     * drops the pin, while the cached wrapper survives until device teardown. */
    if (surface_is_implicit(surface)) {
        TRACE_MARK_ENTER("Surface.AddRef");
        refs = (ULONG)InterlockedIncrement(&surface->refcount);
        if (refs == 1)
            device_child_add_ref(surface->device);
        TRACE("SURFACE ADDREF object=%08lX implicit=1 after=%lu device=%08lX "
                "device_refs=%ld", (DWORD)(uintptr_t)iface, refs,
                (DWORD)(uintptr_t)surface->device,
                InterlockedCompareExchange(&surface->device->refcount, 0, 0));
        TRACE_MARK_EXIT("Surface.AddRef", (HRESULT)refs, iface);
        return refs;
    }
#ifdef D9WG_DIAGNOSTIC_TRACE
    before = InterlockedCompareExchange(&surface->refcount, 0, 0);
#endif
    TRACE_MARK_ENTER("Surface.AddRef");
    refs = (ULONG)InterlockedIncrement(&surface->refcount);
    if (refs == 1)
        device_child_add_ref(surface->device);
    TRACE("SURFACE ADDREF object=%08lX vtbl=%08lX before=%ld after=%lu "
            "texture=%08lX shadow=%08lX level=%lu",
            (DWORD)(uintptr_t)iface,
            (DWORD)(uintptr_t)surface->iface.lpVtbl, before, refs,
            (DWORD)(uintptr_t)surface->texture,
            (DWORD)(uintptr_t)surface->shadow, surface->level);
    (void)before;
    TRACE_MARK_EXIT("Surface.AddRef", (HRESULT)refs, iface);
    return refs;
}

static void surface_destroy_storage(D9Surface *surface)
{
    void *iface = surface;
    (void)iface;
    D9Device *device = surface->device;
    (void)device;
    D9Texture *texture = surface->texture;
    BYTE *shadow = surface->shadow;

    TRACE("SURFACE FREE_BEGIN object=%08lX vtbl=%08lX device=%08lX "
            "texture=%08lX swapchain=%08lX shadow=%08lX level=%lu "
            "size=%lux%lu format=%08lX locked=%lu",
            (DWORD)(uintptr_t)iface,
            (DWORD)(uintptr_t)surface->iface.lpVtbl,
            (DWORD)(uintptr_t)device, (DWORD)(uintptr_t)texture,
            (DWORD)(uintptr_t)surface->swap_chain,
            (DWORD)(uintptr_t)shadow, surface->level, surface->width,
            surface->height, (DWORD)surface->format,
            (DWORD)surface->locked);
    /* Only a create_target_texture surface arrives here with a texture,
     * and it holds that texture's sole reference, so the texture goes with
     * it.  Texture level surfaces never reach this path at all: they are
     * sub-objects freed by texture_release.  The back-buffer, offscreen
     * and auto-depth surfaces have no texture and only hold the device. */
    d9wg_private_clear(&surface->private_data, &d9wg_private_ops);
    if (texture)
        IDirect3DTexture9_Release(&texture->iface);
    /* An app that releases a surface without releasing its DC leaks a GDI
     * object and a DC handle -- both process-wide and both scarce -- so
     * the teardown reclaims them rather than trusting the caller. The
     * contents are deliberately *not* copied back: a surface being freed
     * has nowhere for them to go. */
    if (surface->dc) {
        SelectObject(surface->dc, surface->dc_previous);
        DeleteDC(surface->dc);
        surface->dc = NULL;
    }
    if (surface->dib) {
        DeleteObject(surface->dib);
        surface->dib = NULL;
    }
    if (shadow)
        HeapFree(GetProcessHeap(), 0, shadow);
    TRACE("SURFACE FREE_END object=%08lX", (DWORD)(uintptr_t)iface);
    TRACE_SURFACE_FREED(iface);
    core_view_retire8(surface);
    HeapFree(GetProcessHeap(), 0, surface);
}

static ULONG WINAPI surface_release(IDirect3DSurface9 *iface)
{
    D9Surface *surface = surface_from_iface(iface);
    LONG before = 0;
    ULONG refs;

    if (surface->swap_chain && surface->swap_chain->handle) {
        if (!InterlockedCompareExchange(&surface->refcount, 0, 0))
            return 0;
        refs = (ULONG)InterlockedDecrement(&surface->refcount);
        if (!refs) {
            D9Device *device = surface->device;
            D9SwapChain *chain = surface->swap_chain;
            if (!InterlockedCompareExchange(&chain->refcount, 0, 0)
                    && !InterlockedCompareExchange(&surface->binding_refs, 0, 0))
                swap_chain_destroy_storage(chain);
            device_child_release(device);
        }
        return refs;
    }

    /* Mirror of surface_add_ref: the release lands on the texture, and the
     * surface object itself survives until the texture is destroyed.  This is
     * the whole point of the sub-object model -- the app is allowed to release
     * its surface reference and go on using the pointer. */
    if (surface->cube_texture) {
        D9CubeTexture *texture = surface->cube_texture;
        TRACE_MARK_ENTER("Surface.Release");
        refs = IDirect3DCubeTexture9_Release(&texture->iface);
        TRACE_MARK_EXIT("Surface.Release", (HRESULT)refs, iface);
        return refs;
    }
    if (surface->texture_child) {
        /* Read everything the trace needs first: this release can be the
         * texture's last, and the texture's teardown frees this surface. */
        D9Texture *texture = surface->texture;
        UINT level = surface->level;

        TRACE_MARK_ENTER("Surface.Release");
        refs = IDirect3DTexture9_Release(&texture->iface);
        TRACE("SURFACE RELEASE object=%08lX forwarded_to_texture=%08lX "
                "level=%lu texture_refs=%lu", (DWORD)(uintptr_t)iface,
                (DWORD)(uintptr_t)texture, level, refs);
        (void)level;
        TRACE_MARK_EXIT("Surface.Release", (HRESULT)refs, iface);
        return refs;
    }
    /* Mirror of the implicit branch in surface_add_ref. The object is never
     * freed here: it belongs to the device and is released with it, which is
     * precisely what lets the app go on using the pointer -- and what stops the
     * heap from handing this block out as some unrelated resource. */
    if (surface_is_implicit(surface)) {
        D9Device *device = surface->device;

        TRACE_MARK_ENTER("Surface.Release");
        if (!InterlockedCompareExchange(&surface->refcount, 0, 0)) {
            TRACE_MARK_EXIT("Surface.Release", 0, iface);
            return 0;
        }
        refs = (ULONG)InterlockedDecrement(&surface->refcount);
        TRACE("SURFACE RELEASE object=%08lX implicit=1 after=%lu device=%08lX "
                "device_refs_before=%ld", (DWORD)(uintptr_t)iface, refs,
                (DWORD)(uintptr_t)device,
                InterlockedCompareExchange(&device->refcount, 0, 0));
        TRACE_MARK_EXIT("Surface.Release", (HRESULT)refs, iface);
        /* This can destroy the device, which frees `surface`; touch neither
         * object afterwards. */
        if (!refs)
            device_child_release(device);
        return refs;
    }
#ifdef D9WG_DIAGNOSTIC_TRACE
    before = InterlockedCompareExchange(&surface->refcount, 0, 0);
#endif
    TRACE_MARK_ENTER("Surface.Release");
    if (!InterlockedCompareExchange(&surface->refcount, 0, 0)) {
        TRACE_MARK_EXIT("Surface.Release", 0, iface);
        return 0;
    }
    refs = (ULONG)InterlockedDecrement(&surface->refcount);
    TRACE("SURFACE RELEASE object=%08lX vtbl=%08lX before=%ld after=%lu "
            "device=%08lX texture=%08lX swapchain=%08lX shadow=%08lX "
            "level=%lu locked=%lu",
            (DWORD)(uintptr_t)iface,
            (DWORD)(uintptr_t)surface->iface.lpVtbl, before, refs,
            (DWORD)(uintptr_t)surface->device,
            (DWORD)(uintptr_t)surface->texture,
            (DWORD)(uintptr_t)surface->swap_chain,
            (DWORD)(uintptr_t)surface->shadow, surface->level,
            (DWORD)surface->locked);
    (void)before;
    if (!refs) {
        D9Device *device = surface->device;
        if (!InterlockedCompareExchange(&surface->binding_refs, 0, 0))
            surface_destroy_storage(surface);
        TRACE_MARK_EXIT("Surface.Release", 0, iface);
        /* This can destroy both objects through final binding cleanup. */
        device_child_release(device);
        return 0;
    }
    TRACE_MARK_EXIT("Surface.Release", (HRESULT)refs, iface);
    return refs;
}

static HRESULT WINAPI surface_get_device(IDirect3DSurface9 *iface,
        IDirect3DDevice9 **device_out)
{
    D9Surface *surface = surface_from_iface(iface);
    if (!device_out)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *device_out = &surface->device->iface;
    IDirect3DDevice9_AddRef(*device_out);
    return D3D_OK;
}

static D9WGPrivateStore* surface_private_store(D9Surface* surface)
{
    if (surface->cube_texture)
        return &surface->cube_texture->levels[surface->cube_face * surface->cube_texture->level_count + surface->level].private_data;
    if (surface->texture_child)
        return &surface->texture->levels[surface->level].private_data;
    return &surface->private_data;
}

static HRESULT WINAPI surface_set_private_data(IDirect3DSurface9* iface,
    REFGUID guid, const void* data, DWORD size, DWORD flags)
{
    D9WGPrivateStore* store = surface_private_store(surface_from_iface(iface));
    return TRACE_REFUSE(store ? d9wg_private_hresult(d9wg_private_set(store, guid, data, size, flags, &d9wg_private_ops)) : D3DERR_INVALIDCALL);
}

static HRESULT WINAPI surface_get_private_data(IDirect3DSurface9* iface,
    REFGUID guid, void* data, DWORD* size)
{
    D9WGPrivateStore* store = surface_private_store(surface_from_iface(iface));
    return TRACE_REFUSE(store ? d9wg_get_private_data(store, guid, data, size) : D3DERR_NOTFOUND);
}

static HRESULT WINAPI surface_free_private_data(IDirect3DSurface9* iface,
    REFGUID guid)
{
    D9WGPrivateStore* store = surface_private_store(surface_from_iface(iface));
    return TRACE_REFUSE(store ? d9wg_private_hresult(d9wg_private_free(store, guid, &d9wg_private_ops)) : D3DERR_NOTFOUND);
}

static DWORD WINAPI surface_set_priority(IDirect3DSurface9 *iface,
        DWORD priority)
{ (void)iface; (void)priority; return 0; }

static DWORD WINAPI surface_get_priority(IDirect3DSurface9 *iface)
{ (void)iface; return 0; }

static void WINAPI surface_preload(IDirect3DSurface9 *iface)
{ (void)iface; }

static D3DRESOURCETYPE WINAPI surface_get_type(IDirect3DSurface9 *iface)
{ (void)iface; return D3DRTYPE_SURFACE; }

static HRESULT WINAPI surface_get_container(IDirect3DSurface9 *iface,
        REFIID riid, void **container)
{
    D9Surface *surface = surface_from_iface(iface);
    if (!container)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *container = NULL;
    if (surface->swap_chain) {
        if (!riid || (!iid_is_unknown(riid)
                && !guid_equal(riid, &IID_IDirect3DSwapChain9)))
            return E_NOINTERFACE;
        *container = &surface->swap_chain->iface;
        IDirect3DSwapChain9_AddRef(&surface->swap_chain->iface);
        TRACE("OK Surface.GetContainer surface=%08lX swapchain=%08lX",
                (DWORD)(uintptr_t)iface, (DWORD)(uintptr_t)*container);
        return D3D_OK;
    }
    if (surface->cube_texture) {
        if (riid && !iid_is_unknown(riid)
                && !guid_equal(riid, &IID_IDirect3DResource9)
                && !guid_equal(riid, &IID_IDirect3DBaseTexture9)
                && !guid_equal(riid, &IID_IDirect3DCubeTexture9))
            return E_NOINTERFACE;
        *container = &surface->cube_texture->iface;
        IDirect3DCubeTexture9_AddRef(&surface->cube_texture->iface);
        return D3D_OK;
    }
    /* Standalone render/depth/offscreen surfaces have the device as their
     * public container. Their private backing texture is not an API object. */
    if (surface->auto_depth_stencil || !surface->texture_child) {
        if (!riid || (!iid_is_unknown(riid)
                && !guid_equal(riid, &IID_IDirect3DDevice9)))
            return E_NOINTERFACE;
        *container = &surface->device->iface;
        IDirect3DDevice9_AddRef(&surface->device->iface);
        return D3D_OK;
    }
    if (!surface->texture)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (riid && !iid_is_unknown(riid)
            && !guid_equal(riid, &IID_IDirect3DResource9)
            && !guid_equal(riid, &IID_IDirect3DBaseTexture9)
            && !guid_equal(riid, &IID_IDirect3DTexture9))
        return E_NOINTERFACE;
    *container = &surface->texture->iface;
    IDirect3DTexture9_AddRef(&surface->texture->iface);
    return D3D_OK;
}

static HRESULT WINAPI surface_get_desc(IDirect3DSurface9 *iface,
        D3DSURFACE_DESC *desc)
{
    D9Surface *surface = surface_from_iface(iface);
    if (!desc)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    ZeroMemory(desc, sizeof(*desc));
    desc->Format = surface->format;
    desc->Type = D3DRTYPE_SURFACE;
    desc->Usage = surface->texture ? surface->texture->usage
            : surface->cube_texture ? surface->cube_texture->usage
            : surface->swap_chain ? D3DUSAGE_RENDERTARGET
            : surface->auto_depth_stencil ? D3DUSAGE_DEPTHSTENCIL : 0;
    desc->Pool = surface->texture ? surface->texture->pool
            : surface->cube_texture ? surface->cube_texture->pool
            : surface->pool;
    desc->MultiSampleType = surface->multisample;
    desc->MultiSampleQuality = surface->multisample_quality;
    desc->Width = surface->width;
    desc->Height = surface->height;
    return D3D_OK;
}

/*
 * A texture-level surface locks the very same shadow storage as
 * IDirect3DTexture9::LockRect on that level, so an app can upload through
 * either route and the UPDATE_TEXTURE emitted on unlock is identical.
 *
 * A standalone system-memory surface locks its own shadow. A default-pool
 * render target remains non-lockable. An implicit backbuffer with the explicit
 * lockable flag uses the shared GPU readback/writeback adapter below.
 */
/* Explicitly lockable implicit/additional backbuffers and standalone targets
 * share the same pure bounds and upload bridge as API8. Read every successful lock from GPU; only the published lock rect
 * is written back, and READONLY never emits an upload. */
static HRESULT surface_lock_renderable(D9Surface *surface,
        D3DLOCKED_RECT *out, const RECT *rect, DWORD flags)
{
    D9WGSurfaceLockPlan plan;
    D9WGSurfaceLockRect requested;
    UINT bw, bh, pixel_bytes, pitch, rows, bytes;
    if (rect) requested = (D9WGSurfaceLockRect){rect->left, rect->top, rect->right, rect->bottom};
    if (!out || !texture_format_layout(surface->format, &bw, &bh, &pixel_bytes)
            || bw != 1 || bh != 1
            || !texture_level_layout(surface->format, surface->width, surface->height,
                &pitch, &rows, &bytes) || rows != surface->height
            || !d9wg_surface_lock_plan(surface->width, surface->height, pitch,
                pixel_bytes, surface->multisample,
                surface->texture ? surface->lockable_render_target
                    : !!(surface->device->present.Flags & D3DPRESENTFLAG_LOCKABLE_BACKBUFFER),
                surface->locked || (surface->texture && surface->texture->levels[surface->level].locked),
                rect ? &requested : NULL, flags, &plan))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!surface->shadow) {
        surface->shadow = HeapAlloc(GetProcessHeap(), 0, bytes);
        if (!surface->shadow) return TRACE_REFUSE(E_OUTOFMEMORY);
        surface->row_pitch = pitch;
        surface->byte_count = bytes;
    }
    D9WGReadbackSurface request = {0};
    D9WGReadbackDiagnostic detail;
    request.device_handle = surface->device->handle;
    request.texture_handle = surface->texture ? surface->texture->handle : 0;
    request.level = surface->level;
    request.format = surface->format;
    request.width = surface->width;
    request.height = surface->height;
    request.destination_pitch = pitch;
    int result = d9wg_surface_readback(g_process, &request,
            surface->width * pixel_bytes, surface->height, surface->shadow,
            surface->byte_count, submit_readback_batch_locked, &detail);
    if (result) return TRACE_REFUSE(result == D9WG_READBACK_INVALID
            ? D3DERR_INVALIDCALL : D3DERR_DRIVERINTERNALERROR);
    surface->lock_rect = (RECT){plan.area.left, plan.area.top, plan.area.right, plan.area.bottom};
    surface->lock_flags = flags;
    surface->locked = TRUE;
    if (surface->texture) surface->texture->levels[surface->level].locked = TRUE;
    out->Pitch = pitch;
    out->pBits = surface->shadow + plan.offset;
    return D3D_OK;
}

static HRESULT surface_unlock_renderable(D9Surface *surface)
{
    if (!surface->locked) return TRACE_REFUSE(D3DERR_INVALIDCALL);
    BOOL uploaded = TRUE;
    if (!(surface->lock_flags & D3DLOCK_READONLY)) {
        UINT bw, bh, pixel_bytes;
        const RECT *r = &surface->lock_rect;
        const D9WGSurfaceLockRect area = {r->left, r->top, r->right, r->bottom};
        uploaded = texture_format_layout(surface->format, &bw, &bh, &pixel_bytes)
                && d9wg_renderable_upload(surface->device->handle, allocate_handle(),
                    surface->format, surface->texture ? surface->texture->handle : 0,
                    surface->level, 0, &area, surface->shadow
                        + r->top * surface->row_pitch + r->left * pixel_bytes,
                    surface->row_pitch, pixel_bytes, emit_command, emit_texture_rows);
    }
    if (surface->texture) {
        D9TextureLevel *level = &surface->texture->levels[surface->level];
        level->locked = FALSE;
        /* A partial or failed write cannot leave an older Clear/ColorFill
         * mirror authoritative. The next read must observe GPU storage. */
        if (!(surface->lock_flags & D3DLOCK_READONLY)) level->shadow_valid = FALSE;
    }
    surface->locked = FALSE;
    surface->lock_flags = 0;
    ZeroMemory(&surface->lock_rect, sizeof(surface->lock_rect));
    return uploaded ? D3D_OK : TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
}

static HRESULT WINAPI surface_lock_rect(IDirect3DSurface9 *iface,
        D3DLOCKED_RECT *locked_rect, const RECT *rect, DWORD flags)
{
    D9Surface *surface = surface_from_iface(iface);
    HRESULT result;

    TRACE_MARK_ENTER("Surface.LockRect");
    TRACE("ENTER Surface.LockRect object=%08lX ref=%ld texture=%08lX "
            "shadow=%08lX level=%lu locked=%lu rect=%08lX flags=%08lX",
            (DWORD)(uintptr_t)iface,
            InterlockedCompareExchange(&surface->refcount, 0, 0),
            (DWORD)(uintptr_t)surface->texture,
            (DWORD)(uintptr_t)surface->shadow, surface->level,
            (DWORD)surface->locked, (DWORD)(uintptr_t)rect, flags);

    if ((surface->swap_chain && !surface->texture)
            || (surface->texture && !surface->texture_child
                && (surface->texture->usage & D3DUSAGE_RENDERTARGET))) {
        result = surface_lock_renderable(surface, locked_rect, rect, flags);
        goto done;
    }
    if (surface->shadow) {
        /* A standalone CPU surface. The whole surface is handed over
         * regardless of `rect`: there is no upload to narrow, so a sub-rect
         * lock only changes which pointer the app is given. */
        UINT x = rect ? (UINT)rect->left : 0;
        UINT y = rect ? (UINT)rect->top : 0;
        UINT block_width, block_height, block_bytes;
        if (!locked_rect || surface->locked) {
            result = D3DERR_INVALIDCALL;
            goto done;
        }
        if ((rect && (rect->left < 0 || rect->top < 0
                    || rect->right <= rect->left
                    || rect->bottom <= rect->top
                    || rect->right > (LONG)surface->width
                    || rect->bottom > (LONG)surface->height))
                || !d9wg_format_layout(device_compatibility_api(surface->device),
                    surface->format, &block_width, &block_height, &block_bytes)
                || x >= surface->width || y >= surface->height
                || x % block_width || y % block_height) {
            result = D3DERR_INVALIDCALL;
            goto done;
        }
        locked_rect->Pitch = (INT)surface->row_pitch;
        locked_rect->pBits = surface->shadow
                + (y / block_height) * surface->row_pitch
                + (x / block_width) * block_bytes;
        surface->locked = TRUE;
        (void)flags;
        result = D3D_OK;
        goto done;
    }
    if (surface->cube_texture)
        result = IDirect3DCubeTexture9_LockRect(
                &surface->cube_texture->iface,
                (D3DCUBEMAP_FACES)surface->cube_face, surface->level,
                locked_rect, rect, flags);
    else if (!surface->texture)
        result = D3DERR_INVALIDCALL;
    else
        result = texture_lock_level(surface->texture, surface->level,
                locked_rect, rect, flags);
done:
    TRACE("LEAVE Surface.LockRect object=%08lX hr=%08lX pBits=%08lX "
            "pitch=%ld", (DWORD)(uintptr_t)iface, (DWORD)result,
            (DWORD)(uintptr_t)(SUCCEEDED(result) && locked_rect
                    ? locked_rect->pBits : NULL),
            SUCCEEDED(result) && locked_rect ? locked_rect->Pitch : 0);
    TRACE_MARK_EXIT("Surface.LockRect", result,
            SUCCEEDED(result) && locked_rect ? locked_rect->pBits : NULL);
    return result;
}

static HRESULT WINAPI surface_unlock_rect(IDirect3DSurface9 *iface)
{
    D9Surface *surface = surface_from_iface(iface);
    HRESULT result;

    TRACE_MARK_ENTER("Surface.UnlockRect");
    TRACE("ENTER Surface.UnlockRect object=%08lX ref=%ld vtbl=%08lX "
            "texture=%08lX shadow=%08lX level=%lu locked=%lu",
            (DWORD)(uintptr_t)iface,
            InterlockedCompareExchange(&surface->refcount, 0, 0),
            (DWORD)(uintptr_t)surface->iface.lpVtbl,
            (DWORD)(uintptr_t)surface->texture,
            (DWORD)(uintptr_t)surface->shadow, surface->level,
            (DWORD)surface->locked);
    if ((surface->swap_chain && !surface->texture)
            || (surface->texture && !surface->texture_child
                && (surface->texture->usage & D3DUSAGE_RENDERTARGET))) {
        result = surface_unlock_renderable(surface);
    } else if (surface->shadow) {
        if (!surface->locked)
            result = D3DERR_INVALIDCALL;
        else {
            surface->locked = FALSE;
            result = D3D_OK;
        }
    } else if (surface->cube_texture) {
        result = IDirect3DCubeTexture9_UnlockRect(
                &surface->cube_texture->iface,
                (D3DCUBEMAP_FACES)surface->cube_face, surface->level);
    } else if (!surface->texture) {
        result = D3DERR_INVALIDCALL;
    } else {
        result = texture_unlock_level(surface->texture, surface->level);
    }
    TRACE("LEAVE Surface.UnlockRect object=%08lX hr=%08lX locked=%lu",
            (DWORD)(uintptr_t)iface, (DWORD)result,
            (DWORD)surface->locked);
    TRACE_MARK_EXIT("Surface.UnlockRect", result, iface);
    return result;
}

/*
 * GetDC/ReleaseDC is how an app draws GDI content -- text, a loaded bitmap, a
 * video frame -- straight onto a D3D surface, so refusing it was a whole class
 * of 2D art silently never arriving.
 *
 * It is implemented entirely on this side of the wire, on top of the LockRect
 * path that already exists: a DIB section is the one bitmap GDI will draw into
 * *and* hand back a raw pointer for, so the surface's bits are copied into a
 * DIB for the duration of the DC and copied back on release. That is also how
 * wined3d does it, and for the same reason -- there is no way to point GDI at
 * arbitrary memory with a known layout otherwise.
 *
 * Two bits of D3D9's contract shape this:
 *
 *   - Only the four uncompressed display formats are permitted (D3D9 returns
 *     D3DERR_INVALIDCALL for anything else, including every compressed and
 *     floating-point format), which is what makes a fixed DIB layout viable.
 *   - The surface is locked for as long as the DC is out, so LockRect during
 *     that window must fail. Reusing surface_lock_rect() gives that for free
 *     rather than needing a second flag to mean the same thing.
 *
 * GDI has no notion of the alpha channel: it writes whatever it likes into the
 * top byte. For an X8 format those bits are ignored anyway; for A8R8G8B8 the
 * copy back preserves what GDI produced, which is what D3D9 does too.
 */
static BOOL surface_dc_format_bytes(D3DFORMAT format, UINT *bytes,
        WORD *bit_count)
{
    switch (format) {
    case D3DFMT_X1R5G5B5:
    case D3DFMT_R5G6B5:
        *bytes = 2;
        /* GDI's 16-bit DIBs are 555 unless a BI_BITFIELDS mask says otherwise,
         * and 565 needs that mask. Both are two bytes per pixel either way. */
        *bit_count = 16;
        return TRUE;
    case D3DFMT_X8R8G8B8:
    case D3DFMT_A8R8G8B8:
        *bytes = 4;
        *bit_count = 32;
        return TRUE;
    default:
        return FALSE;
    }
}

static HRESULT WINAPI surface_get_dc(IDirect3DSurface9 *iface, HDC *hdc)
{
    D9Surface *surface = surface_from_iface(iface);
    D3DLOCKED_RECT locked;
    struct {
        BITMAPINFOHEADER header;
        DWORD masks[3];
    } info;
    UINT pixel_bytes;
    WORD bit_count;
    HRESULT result;
    UINT row;
    UINT copy_bytes;

    TRACE_MARK_ENTER("Surface.GetDC");
    if (!hdc) {
        TRACE_MARK_EXIT("Surface.GetDC", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    *hdc = NULL;
    if (surface->dc) {
        /* D3D9 allows exactly one outstanding DC per surface. */
        TRACE("FAIL GetDC object=%08lX already has a DC",
                (DWORD)(uintptr_t)iface);
        TRACE_MARK_EXIT("Surface.GetDC", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    if (!surface_dc_format_bytes(surface->format, &pixel_bytes, &bit_count)) {
        HOSTLOG_REFUSED("Surface.GetDC refused: format %08lX is not one of the "
                "four D3D9 permits a DC on", (DWORD)surface->format);
        TRACE_MARK_EXIT("Surface.GetDC", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    if (!surface->width || !surface->height) {
        TRACE_MARK_EXIT("Surface.GetDC", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }

    result = surface_lock_rect(iface, &locked, NULL, 0);
    if (FAILED(result)) {
        /* A default-pool render target is not lockable, and that is exactly
         * the case a caller most needs named: the DC would have "worked" and
         * drawn into nothing. */
        HOSTLOG_REFUSED("Surface.GetDC refused: the surface could not be "
                "locked (hr=%08lX); a default-pool render target has no "
                "CPU storage to draw into", (DWORD)result);
        TRACE_MARK_EXIT("Surface.GetDC", result, NULL);
        return result;
    }

    ZeroMemory(&info, sizeof(info));
    info.header.biSize = sizeof(info.header);
    info.header.biWidth = (LONG)surface->width;
    /* Negative height is a top-down DIB, which matches D3D's row order. A
     * bottom-up DIB would need every copy below to walk rows backwards. */
    info.header.biHeight = -(LONG)surface->height;
    info.header.biPlanes = 1;
    info.header.biBitCount = bit_count;
    if (surface->format == D3DFMT_R5G6B5) {
        info.header.biCompression = BI_BITFIELDS;
        info.masks[0] = 0xF800;
        info.masks[1] = 0x07E0;
        info.masks[2] = 0x001F;
    } else {
        info.header.biCompression = BI_RGB;
    }
    surface->dib = CreateDIBSection(NULL, (BITMAPINFO *)&info, DIB_RGB_COLORS,
            &surface->dib_bits, NULL, 0);
    if (!surface->dib || !surface->dib_bits) {
        surface->dib = NULL;
        surface->dib_bits = NULL;
        surface_unlock_rect(iface);
        TRACE_MARK_EXIT("Surface.GetDC", E_OUTOFMEMORY, NULL);
        return E_OUTOFMEMORY;
    }
    surface->dc = CreateCompatibleDC(NULL);
    if (!surface->dc) {
        DeleteObject(surface->dib);
        surface->dib = NULL;
        surface->dib_bits = NULL;
        surface_unlock_rect(iface);
        TRACE_MARK_EXIT("Surface.GetDC", E_OUTOFMEMORY, NULL);
        return E_OUTOFMEMORY;
    }
    surface->dc_previous = (HBITMAP)SelectObject(surface->dc, surface->dib);

    /* A DIB's rows are DWORD-aligned; the surface's are not necessarily, so
     * the two pitches are tracked separately and every copy goes row by row. */
    surface->dib_pitch = ((surface->width * pixel_bytes) + 3u) & ~3u;
    surface->dc_pitch = (UINT)locked.Pitch;
    surface->dc_bits = locked.pBits;
    copy_bytes = surface->width * pixel_bytes;
    for (row = 0; row < surface->height; ++row) {
        CopyMemory((BYTE *)surface->dib_bits + row * surface->dib_pitch,
                (const BYTE *)locked.pBits + row * surface->dc_pitch,
                copy_bytes);
    }
    surface->dc_pixel_bytes = pixel_bytes;

    *hdc = surface->dc;
    TRACE("OK GetDC object=%08lX dc=%08lX %lux%lu format=%08lX",
            (DWORD)(uintptr_t)iface, (DWORD)(uintptr_t)surface->dc,
            surface->width, surface->height, (DWORD)surface->format);
    TRACE_MARK_EXIT("Surface.GetDC", D3D_OK, surface->dc);
    return D3D_OK;
}

static HRESULT WINAPI surface_release_dc(IDirect3DSurface9 *iface, HDC hdc)
{
    D9Surface *surface = surface_from_iface(iface);
    UINT row;
    UINT copy_bytes;

    TRACE_MARK_ENTER("Surface.ReleaseDC");
    if (!surface->dc || hdc != surface->dc) {
        /* Releasing a DC this surface never handed out would otherwise tear
         * down the wrong state and leave the surface locked forever. */
        TRACE("FAIL ReleaseDC object=%08lX dc=%08lX does not match %08lX",
                (DWORD)(uintptr_t)iface, (DWORD)(uintptr_t)hdc,
                (DWORD)(uintptr_t)surface->dc);
        TRACE_MARK_EXIT("Surface.ReleaseDC", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }

    /* Anything GDI still has queued has to land in the DIB before it is read
     * back, or the last drawing operation is the one that goes missing. */
    GdiFlush();
    copy_bytes = surface->width * surface->dc_pixel_bytes;
    for (row = 0; row < surface->height; ++row) {
        CopyMemory((BYTE *)surface->dc_bits + row * surface->dc_pitch,
                (const BYTE *)surface->dib_bits + row * surface->dib_pitch,
                copy_bytes);
    }

    SelectObject(surface->dc, surface->dc_previous);
    DeleteDC(surface->dc);
    DeleteObject(surface->dib);
    surface->dc = NULL;
    surface->dc_previous = NULL;
    surface->dib = NULL;
    surface->dib_bits = NULL;
    surface->dc_bits = NULL;

    /* The unlock is what emits the upload, so the GDI drawing reaches the host
     * through exactly the path an ordinary LockRect write would. */
    TRACE("OK ReleaseDC object=%08lX", (DWORD)(uintptr_t)iface);
    TRACE_MARK_EXIT("Surface.ReleaseDC", D3D_OK, NULL);
    return surface_unlock_rect(iface);
}

/* ---- IDirect3DSwapChain9: the device's implicit primary chain ---- */

static HRESULT WINAPI swap_chain_query_interface(IDirect3DSwapChain9 *iface,
        REFIID iid, void **object)
{
    TRACE_MARK_ENTER("SwapChain.QueryInterface");
    if (!object) {
        TRACE_MARK_EXIT("SwapChain.QueryInterface", E_POINTER, NULL);
        return E_POINTER;
    }
    *object = NULL;
    if (!iid || (!iid_is_unknown(iid)
            && !guid_equal(iid, &IID_IDirect3DSwapChain9))) {
        TRACE_MARK_EXIT("SwapChain.QueryInterface", E_NOINTERFACE, NULL);
        return E_NOINTERFACE;
    }
    *object = iface;
    IDirect3DSwapChain9_AddRef(iface);
    TRACE_MARK_EXIT("SwapChain.QueryInterface", S_OK, iface);
    return S_OK;
}

static void swap_chain_destroy_storage(D9SwapChain *chain)
{
    D9Device *device = chain->device;
    D9Surface *surface = surface_from_iface(chain->back_buffer);
    D9SwapChain **link = &device->additional_swap_chains;
    D9WGDestroySwapChain destroy;

    while (*link && *link != chain)
        link = &(*link)->next_device_chain;
    if (*link)
        *link = chain->next_device_chain;
    destroy.device_handle = device->handle;
    destroy.swap_chain_handle = chain->handle;
    emit_command(D9WG_OP_DESTROY_SWAP_CHAIN, &destroy, sizeof(destroy));
    chain->back_buffer = NULL;
    surface->swap_chain = NULL;
    /* Public pins belong to the actual chain/surface Release caller;
     * binding-only destruction runs under the pending device pin. */
    surface_destroy_storage(surface);
    core_view_retire8(chain);
    HeapFree(GetProcessHeap(), 0, chain);
}

static ULONG WINAPI swap_chain_add_ref(IDirect3DSwapChain9 *iface)
{
    D9SwapChain *swap_chain = swap_chain_from_iface(iface);
    ULONG refs;

    TRACE_MARK_ENTER("SwapChain.AddRef");
    refs = (ULONG)InterlockedIncrement(&swap_chain->refcount);
    /* Public zero/nonzero edges pin the device for both chain kinds. */
    if (refs == 1)
        device_child_add_ref(swap_chain->device);
    TRACE("CALL SwapChain.AddRef object=%08lX refs=%lu device_refs=%ld",
            (DWORD)(uintptr_t)iface, refs,
            InterlockedCompareExchange(&swap_chain->device->refcount, 0, 0));
    TRACE_MARK_EXIT("SwapChain.AddRef", (HRESULT)refs, iface);
    return refs;
}

static ULONG WINAPI swap_chain_release(IDirect3DSwapChain9 *iface)
{
    D9SwapChain *swap_chain = swap_chain_from_iface(iface);
    D9Device *device = swap_chain->device;
    ULONG refs;

    TRACE_MARK_ENTER("SwapChain.Release");
    if (!InterlockedCompareExchange(&swap_chain->refcount, 0, 0))
        return 0;
    if (swap_chain->handle) {
        refs = (ULONG)InterlockedDecrement(&swap_chain->refcount);
        if (!refs) {
            D9Surface *surface = surface_from_iface(swap_chain->back_buffer);
            if (!InterlockedCompareExchange(&surface->refcount, 0, 0)
                    && !InterlockedCompareExchange(&surface->binding_refs, 0, 0))
                swap_chain_destroy_storage(swap_chain);
            device_child_release(device);
        }
        TRACE_MARK_EXIT("SwapChain.Release", (HRESULT)refs, NULL);
        return refs;
    }
    /* The cached implicit chain remains usable at public count zero. */
    if (!InterlockedCompareExchange(&swap_chain->refcount, 0, 0))
        return 0;
    refs = (ULONG)InterlockedDecrement(&swap_chain->refcount);
    TRACE("CALL SwapChain.Release object=%08lX refs=%lu device_refs_before=%ld",
            (DWORD)(uintptr_t)iface, refs,
            InterlockedCompareExchange(&device->refcount, 0, 0));
    /* This can free device (and therefore swap_chain); do not touch either
     * object after the call. */
    if (!refs)
        device_child_release(device);
    TRACE_MARK_EXIT("SwapChain.Release", (HRESULT)refs, NULL);
    return refs;
}

static HRESULT WINAPI swap_chain_present(IDirect3DSwapChain9* iface,
    const RECT* src_rect, const RECT* dst_rect, HWND override_window,
    const RGNDATA* dirty_region, DWORD flags)
{
    D9SwapChain* swap_chain = swap_chain_from_iface(iface);
    D9Device* device = swap_chain->device;
    D9WGPresentClock* clock = swap_chain->handle ? &swap_chain->present_clock : &device->present_clock;
    D9WGPresentPacing* pacing = swap_chain->handle ? &swap_chain->present_pacing : &device->present_pacing;
    DWORD interval = swap_chain->handle ? swap_chain->present.PresentationInterval : device->present.PresentationInterval;
    BOOL nonblocking = (flags & D3DPRESENT_DONOTWAIT) != 0;
    D9WGPresentPacing candidate = *pacing;
    HRESULT result;

    result = d9wg_reset_present_result(&device->reset_state);
    if (result != D3D_OK)
        return TRACE_REFUSE(result);
    TRACE_MARK_ENTER("SwapChain.Present");
    TRACE("CALL SwapChain.Present object=%08lX flags=%08lX override=%08lX",
        (DWORD)(uintptr_t)iface, flags, (DWORD)(uintptr_t)override_window);
    /* LINEAR_CONTENT is accepted but has no effect without the corresponding
     * caps bit. DONOTWAIT never sleeps or emits a frame while its virtual
     * deadline is pending. Prepare first, publish only after successful emit. */
    if (nonblocking && !d9wg_prepare_present(clock, &candidate, interval, device->display_mode.RefreshRate)) {
        result = D3DERR_WASSTILLDRAWING;
    } else if (swap_chain->handle || nonblocking) {
        BOOL ok = swap_chain->handle
            ? emit_present_swap_chain_and_flush(swap_chain, override_window)
            : emit_present_and_flush(device, override_window);
        result = ok ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
        if (ok) {
            if (nonblocking)
                *pacing = candidate;
            else
                d9wg_throttle_present(clock, pacing, interval, device->display_mode.RefreshRate);
        }
    } else {
        result = device_present(&device->iface, src_rect, dst_rect,
            override_window, dirty_region);
    }
    TRACE("%s SwapChain.Present object=%08lX -> %08lX",
        SUCCEEDED(result) ? "OK" : "FAIL", (DWORD)(uintptr_t)iface,
        (DWORD)result);
    TRACE_MARK_EXIT("SwapChain.Present", result, NULL);
    return result;
}

static HRESULT WINAPI swap_chain_get_front_buffer_data(
        IDirect3DSwapChain9 *iface, IDirect3DSurface9 *destination)
{
    D9SwapChain *swap_chain = swap_chain_from_iface(iface);
    HRESULT result;

    TRACE_MARK_ENTER("SwapChain.GetFrontBufferData");
    if (swap_chain->handle) {
        /* Forwarding to the device would read the *implicit* chain's pixels --
         * a different window's picture, returned as if it were this one's.
         * Wrong data is harder to attribute than a refusal, so refuse. */
        HOSTLOG_REFUSED("SwapChain.GetFrontBufferData is not implemented for "
                "an additional swap chain");
        TRACE_MARK_EXIT("SwapChain.GetFrontBufferData", D3DERR_INVALIDCALL,
                NULL);
        return D3DERR_INVALIDCALL;
    }
    result = device_get_front_buffer_data(&swap_chain->device->iface, 0,
            destination);
    TRACE("%s SwapChain.GetFrontBufferData object=%08lX dst=%08lX -> %08lX",
            SUCCEEDED(result) ? "OK" : "FAIL", (DWORD)(uintptr_t)iface,
            (DWORD)(uintptr_t)destination, (DWORD)result);
    TRACE_MARK_EXIT("SwapChain.GetFrontBufferData", result, destination);
    return result;
}

static HRESULT WINAPI swap_chain_get_back_buffer(IDirect3DSwapChain9 *iface,
        UINT index, D3DBACKBUFFER_TYPE type, IDirect3DSurface9 **out)
{
    D9SwapChain *swap_chain = swap_chain_from_iface(iface);
    HRESULT result;

    TRACE_MARK_ENTER("SwapChain.GetBackBuffer");
    /* Unlike Device.GetBackBuffer, a bad chain index preserves the output.
     * Native ignores the stereo selector for both chain kinds. */
    if (!out || index) {
        result = D3DERR_INVALIDCALL;
    } else if (swap_chain->handle) {
        *out = swap_chain->back_buffer;
        IDirect3DSurface9_AddRef(*out);
        result = D3D_OK;
    } else {
        result = device_get_back_buffer(&swap_chain->device->iface, 0, index,
                type, out);
    }
    TRACE("%s SwapChain.GetBackBuffer object=%08lX index=%lu type=%lu out=%08lX -> %08lX",
            SUCCEEDED(result) ? "OK" : "FAIL", (DWORD)(uintptr_t)iface,
            index, (DWORD)type,
            (DWORD)(uintptr_t)(out ? *out : NULL), (DWORD)result);
    TRACE_MARK_EXIT("SwapChain.GetBackBuffer", result, out ? *out : NULL);
    return result;
}

static HRESULT WINAPI swap_chain_get_raster_status(IDirect3DSwapChain9 *iface,
        D3DRASTER_STATUS *status)
{
    D9SwapChain *swap_chain = swap_chain_from_iface(iface);
    HRESULT result;

    TRACE_MARK_ENTER("SwapChain.GetRasterStatus");
    result = device_get_raster_status(&swap_chain->device->iface, 0, status);
    TRACE("%s SwapChain.GetRasterStatus object=%08lX -> %08lX",
            SUCCEEDED(result) ? "OK" : "FAIL", (DWORD)(uintptr_t)iface,
            (DWORD)result);
    TRACE_MARK_EXIT("SwapChain.GetRasterStatus", result, status);
    return result;
}

static HRESULT WINAPI swap_chain_get_display_mode(IDirect3DSwapChain9* iface,
    D3DDISPLAYMODE* mode)
{
    D9SwapChain* swap_chain = swap_chain_from_iface(iface);

    TRACE_MARK_ENTER("SwapChain.GetDisplayMode");
    if (!mode) {
        TRACE("FAIL SwapChain.GetDisplayMode object=%08lX missing output -> %08lX",
            (DWORD)(uintptr_t)iface, (DWORD)D3DERR_INVALIDCALL);
        TRACE_MARK_EXIT("SwapChain.GetDisplayMode", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    HRESULT result = d3d_get_adapter_display_mode(&swap_chain->device->parent->iface,
        swap_chain->device->creation.AdapterOrdinal, mode);
    if (SUCCEEDED(result))
        TRACE("OK SwapChain.GetDisplayMode object=%08lX %lux%lu fmt=%08lX refresh=%lu",
            (DWORD)(uintptr_t)iface, mode->Width, mode->Height,
            (DWORD)mode->Format, mode->RefreshRate);
    TRACE_MARK_EXIT("SwapChain.GetDisplayMode", result, mode);
    return result;
}

static HRESULT WINAPI swap_chain_get_device(IDirect3DSwapChain9 *iface,
        IDirect3DDevice9 **out)
{
    D9SwapChain *swap_chain = swap_chain_from_iface(iface);

    TRACE_MARK_ENTER("SwapChain.GetDevice");
    if (!out) {
        TRACE("FAIL SwapChain.GetDevice object=%08lX missing output -> %08lX",
                (DWORD)(uintptr_t)iface, (DWORD)D3DERR_INVALIDCALL);
        TRACE_MARK_EXIT("SwapChain.GetDevice", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    *out = &swap_chain->device->iface;
    IDirect3DDevice9_AddRef(*out);
    TRACE("OK SwapChain.GetDevice object=%08lX device=%08lX refs=%ld",
            (DWORD)(uintptr_t)iface, (DWORD)(uintptr_t)*out,
            InterlockedCompareExchange(&swap_chain->device->refcount, 0, 0));
    TRACE_MARK_EXIT("SwapChain.GetDevice", D3D_OK, *out);
    return D3D_OK;
}

static HRESULT WINAPI swap_chain_get_present_parameters(
        IDirect3DSwapChain9 *iface, D3DPRESENT_PARAMETERS *parameters)
{
    D9SwapChain *swap_chain = swap_chain_from_iface(iface);

    TRACE_MARK_ENTER("SwapChain.GetPresentParameters");
    if (!parameters) {
        TRACE("FAIL SwapChain.GetPresentParameters object=%08lX missing output -> %08lX",
                (DWORD)(uintptr_t)iface, (DWORD)D3DERR_INVALIDCALL);
        TRACE_MARK_EXIT("SwapChain.GetPresentParameters",
                D3DERR_INVALIDCALL, NULL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    if (swap_chain->handle) {
        *parameters = swap_chain->present;
        TRACE_MARK_EXIT("SwapChain.GetPresentParameters", D3D_OK, NULL);
        return D3D_OK;
    }
    *parameters = swap_chain->device->present;
    TRACE("OK SwapChain.GetPresentParameters object=%08lX %lux%lu fmt=%08lX count=%lu windowed=%lu",
            (DWORD)(uintptr_t)iface, parameters->BackBufferWidth,
            parameters->BackBufferHeight, (DWORD)parameters->BackBufferFormat,
            parameters->BackBufferCount, (DWORD)parameters->Windowed);
    TRACE_MARK_EXIT("SwapChain.GetPresentParameters", D3D_OK, parameters);
    return D3D_OK;
}

/* ---- vtables ---- */

static IDirect3D9Vtbl g_d3d_vtbl = {
    .QueryInterface = d3d_query_interface,
    .AddRef = d3d_add_ref,
    .Release = d3d_release,
    .RegisterSoftwareDevice = d3d_register_software_device,
    .GetAdapterCount = d3d_get_adapter_count,
    .GetAdapterIdentifier = d3d_get_adapter_identifier,
    .GetAdapterModeCount = d3d_get_adapter_mode_count,
    .EnumAdapterModes = d3d_enum_adapter_modes,
    .GetAdapterDisplayMode = d3d_get_adapter_display_mode,
    .CheckDeviceType = d3d_check_device_type,
    .CheckDeviceFormat = d3d_check_device_format,
    .CheckDeviceMultiSampleType = d3d_check_multisample,
    .CheckDepthStencilMatch = d3d_check_depth_stencil,
    .CheckDeviceFormatConversion = d3d_check_device_format_conversion,
    .GetDeviceCaps = d3d_get_device_caps,
    .GetAdapterMonitor = d3d_get_adapter_monitor,
    .CreateDevice = d3d_create_device
};

static IDirect3DSwapChain9Vtbl g_swap_chain_vtbl = {
    .QueryInterface = swap_chain_query_interface,
    .AddRef = swap_chain_add_ref,
    .Release = swap_chain_release,
    .Present = swap_chain_present,
    .GetFrontBufferData = swap_chain_get_front_buffer_data,
    .GetBackBuffer = swap_chain_get_back_buffer,
    .GetRasterStatus = swap_chain_get_raster_status,
    .GetDisplayMode = swap_chain_get_display_mode,
    .GetDevice = swap_chain_get_device,
    .GetPresentParameters = swap_chain_get_present_parameters
};

static IDirect3DDevice9Vtbl g_device_vtbl = {
    .QueryInterface = device_query_interface,
    .AddRef = device_add_ref,
    .Release = device_release,
    .TestCooperativeLevel = device_test_cooperative_level,
    .GetAvailableTextureMem = device_get_available_texture_mem,
    .EvictManagedResources = device_evict_managed_resources,
    .GetDirect3D = device_get_direct3d,
    .GetDeviceCaps = device_get_caps,
    .GetDisplayMode = device_get_display_mode,
    .GetCreationParameters = device_get_creation_parameters,
    .SetCursorProperties = device_set_cursor_properties,
    .SetCursorPosition = device_set_cursor_position,
    .ShowCursor = device_show_cursor,
    .CreateAdditionalSwapChain = device_create_additional_swap_chain,
    .GetSwapChain = device_get_swap_chain,
    .GetNumberOfSwapChains = device_get_number_of_swap_chains,
    .Reset = device_reset,
    .Present = device_present,
    .GetBackBuffer = device_get_back_buffer,
    .GetRasterStatus = device_get_raster_status,
    .SetDialogBoxMode = device_set_dialog_box_mode,
    .SetGammaRamp = device_set_gamma_ramp,
    .GetGammaRamp = device_get_gamma_ramp,
    .CreateTexture = device_create_texture,
    .CreateVolumeTexture = device_create_volume_texture,
    .CreateCubeTexture = device_create_cube_texture,
    .CreateVertexBuffer = device_create_vertex_buffer,
    .CreateIndexBuffer = device_create_index_buffer,
    .CreateRenderTarget = device_create_render_target,
    .CreateDepthStencilSurface = device_create_depth_stencil_surface,
    .UpdateSurface = device_update_surface,
    .UpdateTexture = device_update_texture,
    .GetRenderTargetData = device_get_render_target_data,
    .GetFrontBufferData = device_get_front_buffer_data,
    .StretchRect = device_stretch_rect,
    .ColorFill = device_color_fill,
    .CreateOffscreenPlainSurface = device_create_offscreen_plain_surface,
    .SetRenderTarget = device_set_render_target,
    .GetRenderTarget = device_get_render_target,
    .SetDepthStencilSurface = device_set_depth_stencil_surface,
    .GetDepthStencilSurface = device_get_depth_stencil_surface,
    .BeginScene = device_begin_scene,
    .EndScene = device_end_scene,
    .Clear = device_clear,
    .SetTransform = device_set_transform,
    .GetTransform = device_get_transform,
    .MultiplyTransform = device_multiply_transform,
    .SetViewport = device_set_viewport,
    .GetViewport = device_get_viewport,
    .SetMaterial = device_set_material,
    .GetMaterial = device_get_material,
    .SetLight = device_set_light,
    .GetLight = device_get_light,
    .LightEnable = device_light_enable,
    .GetLightEnable = device_get_light_enable,
    .SetClipPlane = device_set_clip_plane,
    .GetClipPlane = device_get_clip_plane,
    .SetRenderState = device_set_render_state,
    .GetRenderState = device_get_render_state,
    .CreateStateBlock = device_create_state_block,
    .BeginStateBlock = device_begin_state_block,
    .EndStateBlock = device_end_state_block,
    .SetClipStatus = device_set_clip_status,
    .GetClipStatus = device_get_clip_status,
    .GetTexture = device_get_texture,
    .SetTexture = device_set_texture,
    .GetTextureStageState = device_get_texture_stage_state,
    .SetTextureStageState = device_set_texture_stage_state,
    .GetSamplerState = device_get_sampler_state,
    .SetSamplerState = device_set_sampler_state,
    .ValidateDevice = device_validate_device,
    .SetPaletteEntries = device_set_palette_entries,
    .GetPaletteEntries = device_get_palette_entries,
    .SetCurrentTexturePalette = device_set_current_texture_palette,
    .GetCurrentTexturePalette = device_get_current_texture_palette,
    .SetScissorRect = device_set_scissor_rect,
    .GetScissorRect = device_get_scissor_rect,
    .SetSoftwareVertexProcessing = device_set_software_vertex_processing,
    .GetSoftwareVertexProcessing = device_get_software_vertex_processing,
    .SetNPatchMode = device_set_npatch_mode,
    .GetNPatchMode = device_get_npatch_mode,
    .DrawPrimitive = device_draw_primitive,
    .DrawIndexedPrimitive = device_draw_indexed_primitive,
    .DrawPrimitiveUP = device_draw_primitive_up,
    .DrawIndexedPrimitiveUP = device_draw_indexed_primitive_up,
    .ProcessVertices = device_process_vertices,
    .CreateVertexDeclaration = device_create_vertex_declaration,
    .SetVertexDeclaration = device_set_vertex_declaration,
    .GetVertexDeclaration = device_get_vertex_declaration,
    .SetFVF = device_set_fvf,
    .GetFVF = device_get_fvf,
    .CreateVertexShader = device_create_vertex_shader,
    .SetVertexShader = device_set_vertex_shader,
    .GetVertexShader = device_get_vertex_shader,
    .SetVertexShaderConstantF = device_set_vertex_shader_constant_f,
    .GetVertexShaderConstantF = device_get_vertex_shader_constant_f,
    .SetVertexShaderConstantI = device_set_vertex_shader_constant_i,
    .GetVertexShaderConstantI = device_get_vertex_shader_constant_i,
    .SetVertexShaderConstantB = device_set_vertex_shader_constant_b,
    .GetVertexShaderConstantB = device_get_vertex_shader_constant_b,
    .SetStreamSource = device_set_stream_source,
    .GetStreamSource = device_get_stream_source,
    .SetStreamSourceFreq = device_set_stream_source_freq,
    .GetStreamSourceFreq = device_get_stream_source_freq,
    .SetIndices = device_set_indices,
    .GetIndices = device_get_indices,
    .CreatePixelShader = device_create_pixel_shader,
    .SetPixelShader = device_set_pixel_shader,
    .GetPixelShader = device_get_pixel_shader,
    .SetPixelShaderConstantF = device_set_pixel_shader_constant_f,
    .GetPixelShaderConstantF = device_get_pixel_shader_constant_f,
    .SetPixelShaderConstantI = device_set_pixel_shader_constant_i,
    .GetPixelShaderConstantI = device_get_pixel_shader_constant_i,
    .SetPixelShaderConstantB = device_set_pixel_shader_constant_b,
    .GetPixelShaderConstantB = device_get_pixel_shader_constant_b,
    .DrawRectPatch = device_draw_rect_patch,
    .DrawTriPatch = device_draw_tri_patch,
    .DeletePatch = device_delete_patch,
    .CreateQuery = device_create_query
};

static IDirect3DVertexBuffer9Vtbl g_vb_vtbl = {
    .QueryInterface = vb_query_interface,
    .AddRef = vb_add_ref,
    .Release = vb_release,
    .GetDevice = vb_get_device,
    .SetPrivateData = vb_set_private_data,
    .GetPrivateData = vb_get_private_data,
    .FreePrivateData = vb_free_private_data,
    .SetPriority = vb_set_priority,
    .GetPriority = vb_get_priority,
    .PreLoad = vb_preload,
    .GetType = vb_get_type,
    .Lock = vb_lock,
    .Unlock = vb_unlock,
    .GetDesc = vb_get_desc
};

static IDirect3DIndexBuffer9Vtbl g_ib_vtbl = {
    .QueryInterface = ib_query_interface,
    .AddRef = ib_add_ref,
    .Release = ib_release,
    .GetDevice = ib_get_device,
    .SetPrivateData = ib_set_private_data,
    .GetPrivateData = ib_get_private_data,
    .FreePrivateData = ib_free_private_data,
    .SetPriority = ib_set_priority,
    .GetPriority = ib_get_priority,
    .PreLoad = ib_preload,
    .GetType = ib_get_type,
    .Lock = ib_lock,
    .Unlock = ib_unlock,
    .GetDesc = ib_get_desc
};

static IDirect3DTexture9Vtbl g_texture_vtbl = {
    .QueryInterface = texture_query_interface,
    .AddRef = texture_add_ref,
    .Release = texture_release,
    .GetDevice = texture_get_device,
    .SetPrivateData = texture_set_private_data,
    .GetPrivateData = texture_get_private_data,
    .FreePrivateData = texture_free_private_data,
    .SetPriority = texture_set_priority,
    .GetPriority = texture_get_priority,
    .PreLoad = texture_preload,
    .GetType = texture_get_type,
    .SetLOD = texture_set_lod,
    .GetLOD = texture_get_lod,
    .GetLevelCount = texture_get_level_count,
    .SetAutoGenFilterType = texture_set_auto_gen_filter_type,
    .GetAutoGenFilterType = texture_get_auto_gen_filter_type,
    .GenerateMipSubLevels = texture_generate_mip_sublevels,
    .GetLevelDesc = texture_get_level_desc,
    .GetSurfaceLevel = texture_get_surface_level,
    .LockRect = texture_lock_rect,
    .UnlockRect = texture_unlock_rect,
    .AddDirtyRect = texture_add_dirty_rect
};

static IDirect3DVertexDeclaration9Vtbl g_decl_vtbl = {
    .QueryInterface = decl_query_interface,
    .AddRef = decl_add_ref,
    .Release = decl_release,
    .GetDevice = decl_get_device,
    .GetDeclaration = decl_get_declaration
};

static IDirect3DVertexShader9Vtbl g_vertex_shader_vtbl = {
    .QueryInterface = vertex_shader_query_interface,
    .AddRef = vertex_shader_add_ref,
    .Release = vertex_shader_release,
    .GetDevice = vertex_shader_get_device,
    .GetFunction = vertex_shader_get_function
};

static IDirect3DPixelShader9Vtbl g_pixel_shader_vtbl = {
    .QueryInterface = pixel_shader_query_interface,
    .AddRef = pixel_shader_add_ref,
    .Release = pixel_shader_release,
    .GetDevice = pixel_shader_get_device,
    .GetFunction = pixel_shader_get_function
};

static IDirect3DSurface9Vtbl g_surface_vtbl = {
    .QueryInterface = surface_query_interface,
    .AddRef = surface_add_ref,
    .Release = surface_release,
    .GetDevice = surface_get_device,
    .SetPrivateData = surface_set_private_data,
    .GetPrivateData = surface_get_private_data,
    .FreePrivateData = surface_free_private_data,
    .SetPriority = surface_set_priority,
    .GetPriority = surface_get_priority,
    .PreLoad = surface_preload,
    .GetType = surface_get_type,
    .GetContainer = surface_get_container,
    .GetDesc = surface_get_desc,
    .LockRect = surface_lock_rect,
    .UnlockRect = surface_unlock_rect,
    .GetDC = surface_get_dc,
    .ReleaseDC = surface_release_dc
};

/*
 * Same lesson the D3D8 path already learned: both SDK version tokens seen in
 * the wild must be accepted, exactly as the real d3d9.dll does. Titles built
 * against the DirectX 9.0b SDK pass 31 (D3D9b_SDK_VERSION) and 9.0c titles
 * pass 32 (D3D_SDK_VERSION); GTA San Andreas is a 9.0b build and passes 31.
 * Rejecting it makes Direct3DCreate9 return NULL at the very first call, which
 * a game can only report as a generic "unable to initialize DirectX".
 *
 * The high bit is the D3D_DEBUG_INFO marker -- an app compiled with that macro
 * passes (version | 0x80000000) to request the debug runtime. There is no debug
 * runtime here, so mask it off rather than failing on it.
 */
#define D3D_SDK_VERSION_9B      31u
#define D3D_SDK_VERSION_9C      32u
#define D3D_SDK_VERSION_DEBUG_BIT 0x80000000u

static IDirect3D9 *d3d_create_root(UINT sdk_version, BOOL publish_hello, UINT compatibility_api)
{
    D9Direct3D *d3d;
    BOOL transport_ready;
    UINT sdk_base = sdk_version & ~D3D_SDK_VERSION_DEBUG_BIT;

#ifdef D9WG_DIAGNOSTIC_TRACE
    /* Earliest point that is reliably on the thread owning the game's window
     * and its message pump, so the hooks see the whole run rather than only
     * what happens after a device exists. */
    trace_install_message_hooks();
#endif
    TRACE("CALL Direct3DCreate9 sdk=%lu", sdk_version);
    if (sdk_base != D3D_SDK_VERSION_9B && sdk_base != D3D_SDK_VERSION_9C) {
        TRACE("FAIL Direct3DCreate9 sdk=%lu expected=%lu|%lu", sdk_version,
                (UINT)D3D_SDK_VERSION_9B, (UINT)D3D_SDK_VERSION_9C);
        return NULL;
    }
    EnterCriticalSection(&g_transport_lock);
    transport_ready = open_transport_locked();
    LeaveCriticalSection(&g_transport_lock);
    if (!transport_ready) {
        TRACE("FAIL Direct3DCreate9 transport");
        return NULL;
    }

    d3d = (D9Direct3D *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*d3d));
    if (!d3d) {
        TRACE("FAIL Direct3DCreate9 allocation");
        return NULL;
    }
    d3d->iface.lpVtbl = &g_d3d_vtbl;
    d3d->refcount = 1;
    d3d->compatibility_api = compatibility_api;
    if (publish_hello) emit_hello_once();
    TRACE("OK Direct3DCreate9 object=%08lX", (DWORD)(uintptr_t)&d3d->iface);
    return &d3d->iface;
}

IDirect3D9 *WINAPI Direct3DCreate9(UINT sdk_version)
{
    return d3d_create_root(sdk_version, TRUE, 9);
}

#ifdef D9WG_STATIC_CORE
/* The D3D8 wrapper publishes its own compatibility profile. Creating its
 * backend factory must not insert a D3D9 HELLO into the shared stream. */
/* D3D8 retains its Shader1.x policy in the adapter. Query the shared base
 * capabilities without a D3D9-only diagnostic/permissive profile. */
void WINAPI d9wg_d3d9_core_caps8(void *caps8)
{
    _Static_assert(__builtin_offsetof(D3DCAPS9, DevCaps2) == D9WG_D3D8_CAPS_BYTES,
            "D3D9 shared caps prefix size");
    D3DCAPS9 caps;
    fill_caps_for_profile(&caps, 0, FALSE);
    if (caps.RasterCaps & D3DPRASTERCAPS_DEPTHBIAS)
        caps.RasterCaps |= D9WG_D3D8_RASTER_ZBIAS;
    CopyMemory(caps8, &caps, D9WG_D3D8_CAPS_BYTES);
}

IDirect3D9 *WINAPI d9wg_d3d9_core_create8(void)
{
    return d3d_create_root(D3D_SDK_VERSION_9C, FALSE, 8);
}

D9WGProcess *WINAPI d9wg_d3d9_core_process(void)
{
    return g_process;
}

ULONG WINAPI d9wg_d3d9_core_root_release(IDirect3D9 *factory)
{
    return IDirect3D9_Release(factory);
}

UINT WINAPI d9wg_d3d9_core_adapter_count(IDirect3D9 *factory)
{
    return IDirect3D9_GetAdapterCount(factory);
}

HMONITOR WINAPI d9wg_d3d9_core_adapter_monitor(IDirect3D9 *factory, UINT adapter)
{
    return IDirect3D9_GetAdapterMonitor(factory, adapter);
}

HRESULT WINAPI d9wg_d3d9_core_register_software(IDirect3D9 *factory, void *initialize)
{
    return IDirect3D9_RegisterSoftwareDevice(factory, initialize);
}

HRESULT WINAPI d9wg_d3d9_core_adapter_identifier8(IDirect3D9 *factory,
        UINT adapter, DWORD flags, void *out, UINT bytes)
{
    _Static_assert(__builtin_offsetof(D3DADAPTER_IDENTIFIER9, DriverVersion) == 1056,
            "D3D9 adapter identity version offset");
    _Static_assert(sizeof(D3DADAPTER_IDENTIFIER9) == 1100, "D3D9 identity size");
    D3DADAPTER_IDENTIFIER9 identifier;
    HRESULT result;
    if (!out || bytes != D9WG_D3D8_IDENTIFIER_BYTES) return D3DERR_INVALIDCALL;
    result = IDirect3D9_GetAdapterIdentifier(factory, adapter, flags, &identifier);
    if (FAILED(result)) return result;
    CopyMemory(out, identifier.Driver, 1024);
    CopyMemory((BYTE *)out + 1024, &identifier.DriverVersion,
            D9WG_D3D8_IDENTIFIER_BYTES - 1024);
    return result;
}

HRESULT WINAPI d9wg_d3d9_core_adapter_display_mode(IDirect3D9 *factory,
        UINT adapter, void *out)
{
    _Static_assert(sizeof(D3DDISPLAYMODE) == 16, "shared adapter mode size");
    D3DDISPLAYMODE mode;
    HRESULT result;
    if (!out) return D3DERR_INVALIDCALL;
    result = IDirect3D9_GetAdapterDisplayMode(factory, adapter, &mode);
    if (SUCCEEDED(result)) CopyMemory(out, &mode, sizeof(mode));
    return result;
}

UINT WINAPI d9wg_d3d9_core_adapter_mode_count8(IDirect3D9 *factory, UINT adapter)
{
    return IDirect3D9_GetAdapterModeCount(factory, adapter, D3DFMT_UNKNOWN);
}

HRESULT WINAPI d9wg_d3d9_core_enum_adapter_modes8(IDirect3D9 *factory,
        UINT adapter, UINT index, void *out)
{
    _Static_assert(sizeof(D3DDISPLAYMODE) == 16, "shared enumerated mode size");
    D3DDISPLAYMODE mode;
    HRESULT result;
    if (!out) return D3DERR_INVALIDCALL;
    result = IDirect3D9_EnumAdapterModes(factory, adapter, D3DFMT_UNKNOWN, index, &mode);
    if (SUCCEEDED(result)) CopyMemory(out, &mode, sizeof(mode));
    return result;
}

HRESULT WINAPI d9wg_d3d9_core_check_type(IDirect3D9 *factory, UINT adapter,
        UINT type, UINT display, UINT backbuffer, BOOL windowed)
{
    return IDirect3D9_CheckDeviceType(factory, adapter, (D3DDEVTYPE)type,
            (D3DFORMAT)display, (D3DFORMAT)backbuffer, windowed);
}

HRESULT WINAPI d9wg_d3d9_core_check_multisample8(IDirect3D9 *factory, UINT adapter,
        UINT type, UINT format, BOOL windowed, UINT multisample)
{
    return IDirect3D9_CheckDeviceMultiSampleType(factory, adapter, (D3DDEVTYPE)type,
            (D3DFORMAT)format, windowed, (D3DMULTISAMPLE_TYPE)multisample, NULL);
}

HRESULT WINAPI d9wg_d3d9_core_check_depth(IDirect3D9 *factory, UINT adapter,
        UINT type, UINT display, UINT render, UINT depth)
{
    return IDirect3D9_CheckDepthStencilMatch(factory, adapter, (D3DDEVTYPE)type,
            (D3DFORMAT)display, (D3DFORMAT)render, (D3DFORMAT)depth);
}

HRESULT WINAPI d9wg_d3d9_core_check_format8(IDirect3D9 *factory, UINT adapter,
        UINT type, UINT display, DWORD usage, UINT resource_type, UINT format)
{
    return IDirect3D9_CheckDeviceFormat(factory, adapter, (D3DDEVTYPE)type,
            (D3DFORMAT)display, usage, (D3DRESOURCETYPE)resource_type, (D3DFORMAT)format);
}

/* This is a layout conversion for the API8 compatibility path. Version8
 * swap/interval/flag values stay intact; it is not ordinary9 CreateDevice
 * validation. All input is staged before output, including in-place callers. */
static void parameters8_to9(D3DPRESENT_PARAMETERS *requested, const void *requested8)
{
    CopyMemory(requested, requested8, 20);
    requested->MultiSampleQuality = 0;
    CopyMemory((BYTE *)requested + 24, (const BYTE *)requested8 + 20,
            D9WG_D3D8_PARAMETERS_BYTES - 20);
}

void WINAPI d9wg_d3d9_core_parameters_effective8(void *out8, const void *requested8,
        UINT width, UINT height, UINT format, HWND window)
{
    _Static_assert(sizeof(D3DPRESENT_PARAMETERS) == 56, "D3D9 parameter size");
    _Static_assert(__builtin_offsetof(D3DPRESENT_PARAMETERS, MultiSampleQuality) == 20,
            "D3D9 quality offset");
    _Static_assert(__builtin_offsetof(D3DPRESENT_PARAMETERS, SwapEffect) == 24,
            "D3D9 parameter tail offset");
    _Static_assert(__builtin_offsetof(D3DPRESENT_PARAMETERS, hDeviceWindow) == 28,
            "D3D9 parameter window offset");
    _Static_assert(__builtin_offsetof(D3DPRESENT_PARAMETERS, PresentationInterval) == 52,
            "D3D9 parameter interval offset");
    D3DPRESENT_PARAMETERS requested, effective;
    parameters8_to9(&requested, requested8);
    d9wg_present_parameters_effective(&effective, &requested,
            width, height, format, window);
    CopyMemory(out8, &effective, 20);
    CopyMemory((BYTE *)out8 + 20, (const BYTE *)&effective + 24,
            D9WG_D3D8_PARAMETERS_BYTES - 20);
}

/* Private constructor only. The embedding8 wrapper owns its public caller
 * publication,identity and remaining methods. Never mutates the52-byte input. */
HRESULT WINAPI d9wg_d3d9_core_create_device8(IDirect3D9 *factory, UINT adapter,
        UINT type, HWND focus, DWORD behavior, const void *parameters8,
        IDirect3DDevice9 **out)
{
    D3DPRESENT_PARAMETERS parameters;
    if (!out) return D3DERR_INVALIDCALL;
    *out = NULL;
    if (!factory || !parameters8 || d3d_from_iface(factory)->compatibility_api != 8)
        return D3DERR_INVALIDCALL;
    parameters8_to9(&parameters, parameters8);
    return IDirect3D9_CreateDevice(factory, adapter, (D3DDEVTYPE)type,
            focus, behavior, &parameters, out);
}

/* API8 wrapper performs its own public count-only publication after success.
 * NULL parameters still enter Reset so its cooperative state becomes pending. */
HRESULT WINAPI d9wg_d3d9_core_reset8(IDirect3DDevice9 *iface, const void *parameters8)
{
    D3DPRESENT_PARAMETERS parameters;
    if (!iface || device_compatibility_api(device_from_iface(iface)) != 8)
        return D3DERR_INVALIDCALL;
    if (!parameters8)
        return IDirect3DDevice9_Reset(iface, NULL);
    parameters8_to9(&parameters, parameters8);
    return IDirect3DDevice9_Reset(iface, &parameters);
}

/* Construction/metadata bridge. Surface locks and API8 public wrappers are
 * separate adapters; returned object is a real private IDirect3DSwapChain9. */
HRESULT WINAPI d9wg_d3d9_core_additional8(IDirect3DDevice9 *iface,
        const void *parameters8, IDirect3DSwapChain9 **out)
{
    D3DPRESENT_PARAMETERS parameters;
    if (!out) return D3DERR_INVALIDCALL;
    *out = NULL;
    if (!iface || !parameters8
            || device_compatibility_api(device_from_iface(iface)) != 8)
        return D3DERR_INVALIDCALL;
    parameters8_to9(&parameters, parameters8);
    return IDirect3DDevice9_CreateAdditionalSwapChain(iface, &parameters, out);
}

/* Public8 CreateRenderTarget validates samples/lockability before clearing
 * the caller's output, then applies its texture constructor's4096 ceiling.
 * Keep that adapter policy outside the ordinary9 constructor and reuse its
 * actual target storage, lock permission and lifetime implementation. */
HRESULT WINAPI d9wg_d3d9_core_render_target8(IDirect3DDevice9 *iface,
        UINT width, UINT height, UINT format, UINT multisample, BOOL lockable,
        IDirect3DSurface9 **out)
{
    if (!out || (multisample != D3DMULTISAMPLE_NONE
                && multisample != D3DMULTISAMPLE_4_SAMPLES)
            || (lockable && multisample != D3DMULTISAMPLE_NONE))
        return D3DERR_INVALIDCALL;
    *out = NULL;
    if (!iface || device_compatibility_api(device_from_iface(iface)) != 8
            || !width || !height || width > 4096 || height > 4096
            || !supported_render_target_format8((D3DFORMAT)format))
        return D3DERR_INVALIDCALL;
    return IDirect3DDevice9_CreateRenderTarget(iface, width, height,
            (D3DFORMAT)format, (D3DMULTISAMPLE_TYPE)multisample, 0,
            lockable, out, NULL);
}

/* API8 depth creation rejects unsupported formats/samples before touching
 * caller output. Its8192 size bound already matches create_target_texture.
 * API8 has no discard argument; preserve depth/stencil across target binds. */
HRESULT WINAPI d9wg_d3d9_core_depth_surface8(IDirect3DDevice9 *iface,
        UINT width, UINT height, UINT format, UINT multisample,
        IDirect3DSurface9 **out)
{
    if (!out || !supported_depth_stencil_format8((D3DFORMAT)format)
            || (multisample != D3DMULTISAMPLE_NONE
                && multisample != D3DMULTISAMPLE_4_SAMPLES))
        return D3DERR_INVALIDCALL;
    *out = NULL;
    if (!iface || device_compatibility_api(device_from_iface(iface)) != 8)
        return D3DERR_INVALIDCALL;
    return IDirect3DDevice9_CreateDepthStencilSurface(iface, width, height,
            (D3DFORMAT)format, (D3DMULTISAMPLE_TYPE)multisample, 0,
            FALSE, out, NULL);
}

/* API8 front readback binds the shared algorithm to actual core9 objects.
 * Backbuffer/standalone storage belongs to its cached surface; texture views
 * use their parent's level shadow. No temporary COM resource is introduced. */
static BYTE *front_pixels8(D9Surface *surface, UINT *pitch)
{
    *pitch = 0;
    D9TextureLevel *level = surface_texture_level(surface);
    if (level) {
        if (!level->shadow) {
            if (!level->byte_count && !d9wg_texture_level_layout(8u,
                    surface->format, level->width, level->height,
                    &level->row_pitch, &level->row_count, &level->byte_count))
                return NULL;
            level->shadow = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
                    level->byte_count);
        }
        *pitch = level->row_pitch;
        return level->shadow;
    }
    if (!surface->shadow) {
        UINT rows, bytes, row_pitch;
        if (!d9wg_texture_level_layout(8u, surface->format, surface->width,
                surface->height, &row_pitch, &rows, &bytes)) return NULL;
        surface->shadow = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
        if (!surface->shadow) return NULL;
        surface->row_pitch = row_pitch;
        surface->byte_count = bytes;
    }
    *pitch = surface->row_pitch;
    return surface->shadow;
}

static BOOL front_layout8(D3DFORMAT format, UINT *bw, UINT *bh, UINT *bytes)
{
    return d9wg_format_layout(8u, format, bw, bh, bytes);
}

static HRESULT readback_front8(D9Surface *surface, D3DFORMAT format,
        BYTE *pixels, UINT pitch, UINT bytes)
{
    D9WGReadbackSurface request = {0};
    D9WGReadbackDiagnostic detail;
    UINT bw, bh, pixel_bytes;
    if (!front_layout8(format, &bw, &bh, &pixel_bytes) || bw != 1 || bh != 1)
        return D3DERR_INVALIDCALL;
    request.device_handle = surface->device->handle;
    request.texture_handle = surface->texture ? surface->texture->handle : 0;
    request.level = surface->level;
    request.format = format;
    request.width = surface->width;
    request.height = surface->height;
    request.destination_pitch = pitch;
    int result = d9wg_surface_readback(g_process, &request,
            surface->width * pixel_bytes, surface->height, pixels, bytes,
            submit_readback_batch_locked, &detail);
    return !result ? D3D_OK : result == D9WG_READBACK_INVALID
            ? D3DERR_INVALIDCALL : D3DERR_DRIVERINTERNALERROR;
}

static BOOL upload_front8(D9Surface *surface, D3DFORMAT format,
        const RECT *area, const BYTE *pixels, UINT pitch, UINT pixel_bytes)
{
    if (surface->texture)
        return emit_texture_update(surface->texture, surface->level, area);
    if (surface->cube_texture)
        return emit_cube_texture_update(surface->cube_texture,
                surface->cube_face, surface->level, area);
    if (surface->swap_chain) {
        const D9WGSurfaceLockRect r = {area->left, area->top, area->right, area->bottom};
        return d9wg_renderable_upload(surface->device->handle, allocate_handle(),
                format, 0, 0, 0, &r, pixels + area->top * pitch
                    + area->left * pixel_bytes, pitch, pixel_bytes,
                emit_command, emit_texture_rows);
    }
    return TRUE;
}

#define D9WG_FRONT_DEVICE D9Device
#define D9WG_FRONT_IFACE IDirect3DSurface9
#define D9WG_FRONT_SURFACE D9Surface
#define D9WG_FRONT_LEVEL D9TextureLevel
#define D9WG_FRONT_DESC D3DSURFACE_DESC
#define D9WG_FRONT_TRACE TRACE_REFUSE
#define D9WG_FRONT_OWNER(s) ((s)->device)
#define D9WG_FRONT_GET_DESC surface_get_desc
#define D9WG_FRONT_LAYOUT front_layout8
#define D9WG_FRONT_PIXELS front_pixels8
#define D9WG_FRONT_READ readback_front8
#define D9WG_FRONT_UPLOAD upload_front8
#define D9WG_FRONT_DEPTH(s) ((s)->auto_depth_stencil || ((s)->texture && ((s)->texture->usage & D3DUSAGE_DEPTHSTENCIL)))
#define D9WG_FRONT_SOURCE(d,out) IDirect3DDevice9_GetBackBuffer(&(d)->iface, 0, 0, D3DBACKBUFFER_TYPE_MONO, (out))
#define D9WG_FRONT_SOURCE_BYTES(s,desc) ((s)->byte_count)
#define D9WG_FRONT_DIRTY(level,value) ((level)->shadow_valid = !(value))
#define D9WG_FRONT_RELEASE IDirect3DSurface9_Release
#include "../runtime/d9wg_front_buffer_bridge8.h"

HRESULT WINAPI d9wg_d3d9_core_get_front_buffer8(IDirect3DDevice9 *iface,
        IDirect3DSurface9 *destination)
{
    if (!iface || device_compatibility_api(device_from_iface(iface)) != 8u)
        return D3DERR_INVALIDCALL;
    return d9wg_front_buffer8_common(device_from_iface(iface), destination);
}

/* CopyRects can address a texture mip, a cube face or the implicit buffer.
 * CPU images deliberately have no GPU identity. */
static BOOL copy_location8(D9Surface *surface, uint32_t *handle,
        uint32_t *level, uint32_t *face)
{
    *handle = *level = *face = 0;
    if (surface->texture) {
        *handle = surface->texture->handle;
        *level = surface->level;
        return TRUE;
    }
    if (surface->cube_texture) {
        *handle = surface->cube_texture->handle;
        *level = surface->level;
        *face = surface->cube_face;
        return TRUE;
    }
    return surface->swap_chain != NULL;
}

static HRESULT refresh_copy8(D9Surface *surface)
{
    D3DSURFACE_DESC desc;
    D9TextureLevel *level = surface_texture_level(surface);
    if (FAILED(surface_get_desc(&surface->iface, &desc)))
        return D3DERR_INVALIDCALL;
    if (!surface->swap_chain && (desc.Pool != D3DPOOL_DEFAULT
            || (!(desc.Usage & D3DUSAGE_RENDERTARGET)
                && !(level && !level->shadow_valid))))
        return D3D_OK;
    UINT pitch;
    BYTE *pixels = front_pixels8(surface, &pitch);
    D9WGReadbackSurface request = {0};
    D9WGReadbackDiagnostic detail;
    UINT bw, bh, pixel_bytes;
    if (!pixels || !copy_location8(surface, &request.texture_handle,
                &request.level, &request.face)
            || !front_layout8(desc.Format, &bw, &bh, &pixel_bytes)
            || bw != 1 || bh != 1)
        return D3DERR_INVALIDCALL;
    request.device_handle = surface->device->handle;
    request.format = desc.Format;
    request.width = desc.Width;
    request.height = desc.Height;
    request.destination_pitch = pitch;
    int result = d9wg_surface_readback(g_process, &request,
            desc.Width * pixel_bytes, desc.Height, pixels,
            level ? level->byte_count : surface->byte_count,
            submit_readback_batch_locked, &detail);
    if (!result && level) level->shadow_valid = TRUE;
    return !result ? D3D_OK : result == D9WG_READBACK_INVALID
            ? D3DERR_INVALIDCALL : D3DERR_DRIVERINTERNALERROR;
}

#define D9WG_RECT_DEVICE D9Device
#define D9WG_RECT_SURFACE D9Surface
#define D9WG_RECT_LEVEL D9TextureLevel
#define D9WG_RECT_IFACE IDirect3DSurface9
#define D9WG_RECT_DESC D3DSURFACE_DESC
#define D9WG_RECT_TRACE TRACE_REFUSE
#define D9WG_RECT_OWNER(s) ((s)->device)
#define D9WG_RECT_GET_DESC surface_get_desc
#define D9WG_RECT_LAYOUT front_layout8
#define D9WG_RECT_LOCATION copy_location8
#define D9WG_RECT_REFRESH refresh_copy8
#define D9WG_RECT_PIXELS front_pixels8
#define D9WG_RECT_UPLOAD upload_front8
#define D9WG_RECT_DEPTH(s) ((s)->auto_depth_stencil)
#define D9WG_RECT_DIRTY(level,value) ((level)->shadow_valid = !(value))
#include "../runtime/d9wg_copy_rects_bridge8.h"

HRESULT WINAPI d9wg_d3d9_core_copy_rects8(IDirect3DDevice9 *iface,
        IDirect3DSurface9 *source, const RECT *rects, UINT count,
        IDirect3DSurface9 *destination, const POINT *points)
{
    if (!iface || device_compatibility_api(device_from_iface(iface)) != 8u)
        return D3DERR_INVALIDCALL;
    return d9wg_copy_rects8_common(device_from_iface(iface), source,
            rects, count, destination, points);
}

/* Same core9 mip/cube/volume copy machinery, now with the shared8/9 pool
 * rule. Ownership/type/format/lock/preflight validation stays in the core. */
HRESULT WINAPI d9wg_d3d9_core_update_texture8(IDirect3DDevice9 *iface,
        IDirect3DBaseTexture9 *source, IDirect3DBaseTexture9 *destination)
{
    if (!iface || device_compatibility_api(device_from_iface(iface)) != 8u)
        return D3DERR_INVALIDCALL;
    return IDirect3DDevice9_UpdateTexture(iface, source, destination);
}

/* These entries translate backend state only. The public8 wrapper retains
 * raw API8 Get/state-block bookkeeping before delegating to these methods. */
HRESULT WINAPI d9wg_d3d9_core_set_render_state8(IDirect3DDevice9 *iface,
        UINT state, DWORD value)
{
    D9WGState8 translated;
    if (!iface || device_compatibility_api(device_from_iface(iface)) != 8u
            || state >= D9WG_RENDER_STATES8)
        return D3DERR_INVALIDCALL;
    translated = d9wg_render_state8(state, value);
    if (translated.kind == D9WG_STATE8_DROP) return D3D_OK;
    return IDirect3DDevice9_SetRenderState(iface,
            (D3DRENDERSTATETYPE)translated.state, translated.value);
}

HRESULT WINAPI d9wg_d3d9_core_set_texture_stage_state8(IDirect3DDevice9 *iface,
        UINT stage, UINT state, DWORD value)
{
    D9WGState8 translated;
    if (!iface || device_compatibility_api(device_from_iface(iface)) != 8u
            || stage >= D9WG_TEXTURE_STAGES8
            || state >= D9WG_TEXTURE_STAGE_STATES8)
        return D3DERR_INVALIDCALL;
    translated = d9wg_texture_stage_state8(state, value);
    if (translated.kind == D9WG_STATE8_SAMPLER)
        return IDirect3DDevice9_SetSamplerState(iface, stage,
                (D3DSAMPLERSTATETYPE)translated.state, translated.value);
    return IDirect3DDevice9_SetTextureStageState(iface, stage,
            (D3DTEXTURESTAGESTATETYPE)translated.state, translated.value);
}

/* Buffer creation/lock/descriptor contracts already agree between the
 * legacy8 andordinary9 implementations. Keep the profile boundary explicit
 * and reuse the actual core objects;public8 interface wrappers follow later. */
HRESULT WINAPI d9wg_d3d9_core_vertex_buffer8(IDirect3DDevice9 *iface,
        UINT length, DWORD usage, DWORD fvf, UINT pool,
        IDirect3DVertexBuffer9 **out)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = NULL;
    if (!iface || device_compatibility_api(device_from_iface(iface)) != 8u)
        return D3DERR_INVALIDCALL;
    return IDirect3DDevice9_CreateVertexBuffer(iface, length, usage, fvf,
            (D3DPOOL)pool, out, NULL);
}

HRESULT WINAPI d9wg_d3d9_core_index_buffer8(IDirect3DDevice9 *iface,
        UINT length, DWORD usage, UINT format, UINT pool,
        IDirect3DIndexBuffer9 **out)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = NULL;
    if (!iface || device_compatibility_api(device_from_iface(iface)) != 8u)
        return D3DERR_INVALIDCALL;
    return IDirect3DDevice9_CreateIndexBuffer(iface, length, usage,
            (D3DFORMAT)format, (D3DPOOL)pool, out, NULL);
}

/* Private API8 volume construction and exact32-byte SDK8 descriptor. */
HRESULT WINAPI d9wg_d3d9_core_volume_texture8(IDirect3DDevice9 *iface,
        UINT width, UINT height, UINT depth, UINT levels, DWORD usage,
        UINT format, UINT pool, IDirect3DVolumeTexture9 **out)
{
    if (!iface || device_compatibility_api(device_from_iface(iface)) != 8u) {
        if (out) *out = NULL;
        return D3DERR_INVALIDCALL;
    }
    return create_volume_texture_object(device_from_iface(iface), width,
            height, depth, levels, usage, (D3DFORMAT)format, (D3DPOOL)pool, 8u, out);
}

static HRESULT core_pack_volume_desc8(const D3DVOLUME_DESC *desc, void *out)
{
    UINT pitch, rows, slice;
    if (!desc->Width || !desc->Height || !desc->Depth
            || !supported_volume_texture_format8(desc->Format)
            || !d9wg_texture_level_layout(8u, desc->Format, desc->Width, desc->Height,
                &pitch, &rows, &slice)
            || (uint64_t)slice * desc->Depth > UINT32_MAX)
        return D3DERR_INVALIDCALL;
    const UINT packed[8] = {desc->Format, desc->Type, desc->Usage, desc->Pool,
        slice * desc->Depth, desc->Width, desc->Height, desc->Depth};
    CopyMemory(out, packed, sizeof(packed));
    return D3D_OK;
}

HRESULT WINAPI d9wg_d3d9_core_volume_desc8(IDirect3DVolume9 *iface,
        void *out, UINT bytes)
{
    D3DVOLUME_DESC desc;
    if (!iface || iface->lpVtbl != &g_volume_vtbl || !out
            || bytes != D9WG_D3D8_VOLUME_DESC_BYTES)
        return D3DERR_INVALIDCALL;
    D9Volume *volume = volume_from_iface(iface);
    if (!volume->texture
            || device_compatibility_api(volume->texture->device) != 8u)
        return D3DERR_INVALIDCALL;
    HRESULT hr = IDirect3DVolume9_GetDesc(iface, &desc);
    if (FAILED(hr)) return hr;
    return core_pack_volume_desc8(&desc, out);
}

/* Private API8 cube creation;ordinary cube CPU lock/upload uses its owning
 * profile. Renderable cube readback/public8 wrapping remain separate work. */
HRESULT WINAPI d9wg_d3d9_core_cube_texture8(IDirect3DDevice9 *iface,
        UINT edge, UINT levels, DWORD usage, UINT format, UINT pool,
        IDirect3DCubeTexture9 **out)
{
    if (!iface || device_compatibility_api(device_from_iface(iface)) != 8u) {
        if (out) *out = NULL;
        return D3DERR_INVALIDCALL;
    }
    return create_cube_texture_object(device_from_iface(iface), edge, levels,
            usage, (D3DFORMAT)format, (D3DPOOL)pool, 8u, out);
}

/* API8 creation policy with actual common core texture storage/lifetime.
 * Returned SDK9 object stays private; public8 resource wrappers follow later. */
HRESULT WINAPI d9wg_d3d9_core_texture8(IDirect3DDevice9 *iface,
        UINT width, UINT height, UINT levels, DWORD usage, UINT format,
        UINT pool, IDirect3DTexture9 **out)
{
    if (!iface || device_compatibility_api(device_from_iface(iface)) != 8u) {
        if (out) *out = NULL;
        return D3DERR_INVALIDCALL;
    }
    return create_texture_object(device_from_iface(iface), width, height,
            levels, usage, (D3DFORMAT)format, (D3DPOOL)pool, 8u, out);
}

HRESULT WINAPI d9wg_d3d9_core_image_surface8(IDirect3DDevice9 *iface,
        UINT width, UINT height, UINT format, IDirect3DSurface9 **out)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = NULL;
    if (!iface || device_compatibility_api(device_from_iface(iface)) != 8
            || !width || !height || width > 8192 || height > 8192
            || !supported_format8((D3DFORMAT)format, TRUE))
        return D3DERR_INVALIDCALL;
    return create_cpu_surface(device_from_iface(iface), width, height,
            (D3DFORMAT)format, D3DPOOL_SYSTEMMEM, 8u, out);
}

static HRESULT WINAPI core_surface_lock_rect8(IDirect3DSurface9 *surface,
        void *locked, const RECT *rect, DWORD flags)
{
    return IDirect3DSurface9_LockRect(surface, (D3DLOCKED_RECT *)locked, rect, flags);
}

const D9WGSurfaceMethods8 *WINAPI d9wg_d3d9_core_surface_methods8(
        IDirect3DSurface9 *surface)
{
    static const D9WGSurfaceMethods8 methods = {
        surface_set_private_data, surface_get_private_data, surface_free_private_data,
        d9wg_d3d9_core_surface_desc8, core_surface_lock_rect8, surface_unlock_rect
    };
    if (!surface || surface->lpVtbl != &g_surface_vtbl
            || !core_view_device8((IUnknown *)surface))
        return NULL;
    return &methods;
}

/* Equal sizeof does not make these SDK descriptors interchangeable: API8
 * has Size at word4 and samples at word5; API9 puts samples/quality there.
 * Stage the complete eight-word result so failure never partly writes it. */
static HRESULT core_pack_surface_desc8(const D3DSURFACE_DESC *desc, void *out)
{
    UINT pitch, rows, size = 0;
    if (desc->Usage & D3DUSAGE_DEPTHSTENCIL) {
        if (!d9wg_depth_surface_layout8(desc->Format, desc->Width,
                desc->Height, &pitch, &size))
            return D3DERR_INVALIDCALL;
    } else if (!d9wg_texture_level_layout(8u, desc->Format,
            desc->Width, desc->Height, &pitch, &rows, &size))
        return D3DERR_INVALIDCALL;
    DWORD packed[8] = {(DWORD)desc->Format, (DWORD)desc->Type, desc->Usage,
            (DWORD)desc->Pool, size, (DWORD)desc->MultiSampleType,
            desc->Width, desc->Height};
    CopyMemory(out, packed, sizeof(packed));
    return D3D_OK;
}

HRESULT WINAPI d9wg_d3d9_core_surface_desc8(IDirect3DSurface9 *iface,
        void *out, UINT bytes)
{
    D9Surface *surface;
    D3DSURFACE_DESC desc;
    if (!iface || iface->lpVtbl != &g_surface_vtbl || !out
            || bytes != D9WG_D3D8_SURFACE_DESC_BYTES)
        return D3DERR_INVALIDCALL;
    surface = surface_from_iface(iface);
    if (device_compatibility_api(surface->device) != 8)
        return D3DERR_INVALIDCALL;
    HRESULT hr = IDirect3DSurface9_GetDesc(iface, &desc);
    if (FAILED(hr)) return hr;
    return core_pack_surface_desc8(&desc, out);
}

static HRESULT WINAPI core_swap_chain_present8(IDirect3DSwapChain9 *chain,
        const RECT *source, const RECT *destination, HWND window, const RGNDATA *dirty)
{
    return IDirect3DSwapChain9_Present(chain, source, destination, window, dirty, 0u);
}
static HRESULT WINAPI core_swap_chain_back_buffer8(IDirect3DSwapChain9 *chain,
        UINT index, UINT type, IDirect3DSurface9 **out)
{
    return IDirect3DSwapChain9_GetBackBuffer(chain, index, (D3DBACKBUFFER_TYPE)type, out);
}
const D9WGSwapChainMethods8 *WINAPI d9wg_d3d9_core_swap_chain_methods8(IDirect3DSwapChain9 *chain)
{
    static const D9WGSwapChainMethods8 methods = {
        core_swap_chain_present8, core_swap_chain_back_buffer8
    };
    if (!chain || chain->lpVtbl != &g_swap_chain_vtbl
            || !core_view_device8((IUnknown *)chain)) return NULL;
    return &methods;
}

/* Identical SDK8/9 buffer descriptor layouts and the existing lock/private-data
 * policy are owned by the real9 implementations. Only ABI signatures differ. */
#define D9WG_BUFFER_METHODS8(P, Type, Descriptor) \
static HRESULT WINAPI core_##P##8_set_private(IUnknown *p, const GUID *id, const void *data, DWORD size, DWORD flags) \
{ return P##_set_private_data((Type *)p, id, data, size, flags); } \
static HRESULT WINAPI core_##P##8_get_private(IUnknown *p, const GUID *id, void *data, DWORD *size) \
{ return P##_get_private_data((Type *)p, id, data, size); } \
static HRESULT WINAPI core_##P##8_free_private(IUnknown *p, const GUID *id) \
{ return P##_free_private_data((Type *)p, id); } \
static DWORD WINAPI core_##P##8_set_priority(IUnknown *p, DWORD value) \
{ return P##_set_priority((Type *)p, value); } \
static DWORD WINAPI core_##P##8_get_priority(IUnknown *p) \
{ return P##_get_priority((Type *)p); } \
static void WINAPI core_##P##8_preload(IUnknown *p) \
{ P##_preload((Type *)p); } \
static UINT WINAPI core_##P##8_get_type(IUnknown *p) \
{ return (UINT)P##_get_type((Type *)p); } \
static HRESULT WINAPI core_##P##8_lock(IUnknown *p, UINT offset, UINT size, void *out, DWORD flags) \
{ return P##_lock((Type *)p, offset, size, (void **)out, flags); } \
static HRESULT WINAPI core_##P##8_unlock(IUnknown *p) \
{ return P##_unlock((Type *)p); } \
static HRESULT WINAPI core_##P##8_get_desc(IUnknown *p, void *out, UINT bytes) \
{ if (!out || bytes != sizeof(Descriptor)) return D3DERR_INVALIDCALL; return P##_get_desc((Type *)p, (Descriptor *)out); }
D9WG_BUFFER_METHODS8(vb, IDirect3DVertexBuffer9, D3DVERTEXBUFFER_DESC)
D9WG_BUFFER_METHODS8(ib, IDirect3DIndexBuffer9, D3DINDEXBUFFER_DESC)
#undef D9WG_BUFFER_METHODS8
#define D9WG_BUFFER_TABLE8(P) \
    core_##P##8_set_private, core_##P##8_get_private, core_##P##8_free_private, \
    core_##P##8_set_priority, core_##P##8_get_priority, core_##P##8_preload, \
    core_##P##8_get_type, core_##P##8_lock, core_##P##8_unlock, core_##P##8_get_desc
const D9WGBufferMethods8 *WINAPI d9wg_d3d9_core_buffer_methods8(
        IUnknown *buffer, UINT expected_type)
{
    static const D9WGBufferMethods8 vertex_methods = {D9WG_BUFFER_TABLE8(vb)};
    static const D9WGBufferMethods8 index_methods = {D9WG_BUFFER_TABLE8(ib)};
    if (!buffer || !core_view_device8(buffer)) return NULL;
    if (expected_type == 6u && buffer->lpVtbl == (const IUnknownVtbl *)&g_vb_vtbl)
        return &vertex_methods;
    if (expected_type == 7u && buffer->lpVtbl == (const IUnknownVtbl *)&g_ib_vtbl)
        return &index_methods;
    return NULL;
}
#undef D9WG_BUFFER_TABLE8

static UINT WINAPI core_texture_type8(IDirect3DTexture9 *texture)
{
    return (UINT)IDirect3DTexture9_GetType(texture);
}
static HRESULT WINAPI core_texture_lock8(IDirect3DTexture9 *texture, UINT level,
        void *locked, const RECT *rect, DWORD flags)
{
    return IDirect3DTexture9_LockRect(texture, level, (D3DLOCKED_RECT *)locked, rect, flags);
}
static HRESULT WINAPI core_texture_desc8(IDirect3DTexture9 *texture, UINT level,
        void *out, UINT bytes)
{
    if (!out || bytes != D9WG_D3D8_SURFACE_DESC_BYTES) return D3DERR_INVALIDCALL;
    D3DSURFACE_DESC desc;
    HRESULT hr = IDirect3DTexture9_GetLevelDesc(texture, level, &desc);
    if (FAILED(hr)) return hr;
    return core_pack_surface_desc8(&desc, out);
}
const D9WGTextureMethods8 *WINAPI d9wg_d3d9_core_texture_methods8(IDirect3DTexture9 *texture)
{
    static const D9WGTextureMethods8 methods = {
        texture_set_private_data, texture_get_private_data, texture_free_private_data,
        texture_set_priority, texture_get_priority, texture_preload, core_texture_type8,
        texture_set_lod, texture_get_lod, texture_get_level_count, core_texture_desc8,
        texture_get_surface_level, core_texture_lock8, texture_unlock_rect, texture_add_dirty_rect
    };
    if (!texture || texture->lpVtbl != &g_texture_vtbl
            || !core_view_device8((IUnknown *)texture)) return NULL;
    return &methods;
}

static UINT WINAPI core_volume_texture_type8(IDirect3DVolumeTexture9 *texture)
{
    return (UINT)IDirect3DVolumeTexture9_GetType(texture);
}
static HRESULT WINAPI core_volume_texture_lock8(IDirect3DVolumeTexture9 *texture, UINT level,
        void *locked, const void *rect, DWORD flags)
{
    return IDirect3DVolumeTexture9_LockBox(texture, level, (D3DLOCKED_BOX *)locked, (const D3DBOX *)rect, flags);
}
static HRESULT WINAPI core_volume_texture_desc8(IDirect3DVolumeTexture9 *texture, UINT level,
        void *out, UINT bytes)
{
    if (!out || bytes != D9WG_D3D8_VOLUME_DESC_BYTES) return D3DERR_INVALIDCALL;
    D3DVOLUME_DESC desc;
    HRESULT hr = IDirect3DVolumeTexture9_GetLevelDesc(texture, level, &desc);
    if (FAILED(hr)) return hr;
    return core_pack_volume_desc8(&desc, out);
}
static HRESULT WINAPI core_volume_texture_dirty8(IDirect3DVolumeTexture9 *texture, const void *box)
{
    return IDirect3DVolumeTexture9_AddDirtyBox(texture, (const D3DBOX *)box);
}
const D9WGVolumeTextureMethods8 *WINAPI d9wg_d3d9_core_volume_texture_methods8(IDirect3DVolumeTexture9 *texture)
{
    static const D9WGVolumeTextureMethods8 methods = {
        volume_texture_set_private_data, volume_texture_get_private_data, volume_texture_free_private_data,
        volume_texture_set_priority, volume_texture_get_priority, volume_texture_preload, core_volume_texture_type8,
        volume_texture_set_lod, volume_texture_get_lod, volume_texture_get_level_count, core_volume_texture_desc8,
        volume_texture_get_volume_level, core_volume_texture_lock8, volume_texture_unlock_box, core_volume_texture_dirty8
    };
    if (!texture || texture->lpVtbl != &g_volume_texture_vtbl
            || !core_view_device8((IUnknown *)texture)) return NULL;
    return &methods;
}

static HRESULT WINAPI core_volume_lock8(IDirect3DVolume9 *volume,
        void *locked, const void *box, DWORD flags)
{
    return IDirect3DVolume9_LockBox(volume, (D3DLOCKED_BOX *)locked, (const D3DBOX *)box, flags);
}
const D9WGVolumeMethods8 *WINAPI d9wg_d3d9_core_volume_methods8(IDirect3DVolume9 *volume)
{
    static const D9WGVolumeMethods8 methods = {
        volume_set_private_data, volume_get_private_data, volume_free_private_data,
        d9wg_d3d9_core_volume_desc8, core_volume_lock8, volume_unlock_box
    };
    if (!volume || volume->lpVtbl != &g_volume_vtbl
            || !core_view_device8((IUnknown *)volume)) return NULL;
    return &methods;
}

static UINT WINAPI core_cube_texture_type8(IDirect3DCubeTexture9 *texture)
{
    return (UINT)IDirect3DCubeTexture9_GetType(texture);
}
static HRESULT WINAPI core_cube_texture_lock8(IDirect3DCubeTexture9 *texture, UINT face, UINT level,
        void *locked, const RECT *rect, DWORD flags)
{
    return IDirect3DCubeTexture9_LockRect(texture, (D3DCUBEMAP_FACES)face, level, (D3DLOCKED_RECT *)locked, rect, flags);
}
static HRESULT WINAPI core_cube_texture_desc8(IDirect3DCubeTexture9 *texture, UINT level,
        void *out, UINT bytes)
{
    if (!out || bytes != D9WG_D3D8_SURFACE_DESC_BYTES) return D3DERR_INVALIDCALL;
    D3DSURFACE_DESC desc;
    HRESULT hr = IDirect3DCubeTexture9_GetLevelDesc(texture, level, &desc);
    if (FAILED(hr)) return hr;
    return core_pack_surface_desc8(&desc, out);
}
static HRESULT WINAPI core_cube_texture_surface8(IDirect3DCubeTexture9 *texture,
        UINT face, UINT level, IDirect3DSurface9 **out)
{
    return IDirect3DCubeTexture9_GetCubeMapSurface(texture, (D3DCUBEMAP_FACES)face, level, out);
}
static HRESULT WINAPI core_cube_texture_unlock8(IDirect3DCubeTexture9 *texture, UINT face, UINT level)
{
    return IDirect3DCubeTexture9_UnlockRect(texture, (D3DCUBEMAP_FACES)face, level);
}
static HRESULT WINAPI core_cube_texture_dirty8(IDirect3DCubeTexture9 *texture, UINT face, const RECT *rect)
{
    return IDirect3DCubeTexture9_AddDirtyRect(texture, (D3DCUBEMAP_FACES)face, rect);
}
const D9WGCubeTextureMethods8 *WINAPI d9wg_d3d9_core_cube_texture_methods8(IDirect3DCubeTexture9 *texture)
{
    static const D9WGCubeTextureMethods8 methods = {
        cube_set_private_data, cube_get_private_data, cube_free_private_data,
        cube_set_priority, cube_get_priority, cube_preload, core_cube_texture_type8,
        cube_set_lod, cube_get_lod, cube_get_level_count, core_cube_texture_desc8,
        core_cube_texture_surface8, core_cube_texture_lock8, core_cube_texture_unlock8, core_cube_texture_dirty8
    };
    if (!texture || texture->lpVtbl != &g_cube_vtbl
            || !core_view_device8((IUnknown *)texture)) return NULL;
    return &methods;
}

/* Only the layout object is published here. Raw8 declaration/function and
 * local constant shadows stay with the shader-handle adapter. The shared
 * parser still validates every constant block before constructing storage. */
HRESULT WINAPI d9wg_d3d9_core_vertex_declaration8(IDirect3DDevice9 *iface,
        const DWORD *tokens, BOOL declaration_only,
        IDirect3DVertexDeclaration9 **out)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = NULL;
    if (!iface || !tokens
            || device_compatibility_api(device_from_iface(iface)) != 8u)
        return D3DERR_INVALIDCALL;
    D9WGVertexElement wire[32];
    DWORD constants[96 * 4];
    BYTE mask[96];
    UINT tokens_count, count, vectors;
    if (!d9wg_declaration8_validate(tokens, D9WG_D3D8_SHADER_TOKENS,
                &tokens_count)
            || !d9wg_declaration8_elements(tokens, tokens_count, wire, 32,
                &count, constants, 96, mask, &vectors))
        return D3DERR_INVALIDCALL;
    D3DVERTEXELEMENT9 elements[33];
    for (UINT i = 0; i < count; ++i) {
        const D9WGVertexElement *e = &wire[i];
        if (declaration_only && e->usage == D3D9DECLUSAGE_NORMAL
                && !e->usage_index && e->type != D3D9DECLTYPE_FLOAT3)
            return D3DERR_INVALIDCALL;
        elements[i] = (D3DVERTEXELEMENT9){e->stream, e->offset, e->type,
            e->method, e->usage, e->usage_index};
    }
    elements[count] = (D3DVERTEXELEMENT9)D3DDECL_END();
    return IDirect3DDevice9_CreateVertexDeclaration(iface, elements, out);
}

/* API8 FVFs must not enter the ordinary9 exact-FVF cache: XYZW and
 * LASTBETA_D3DCOLOR have different meanings. Keep a real9 user-layout object
 * with an8-only key and fvfassociation zero. State-block replay then binds its
 * actual elements instead of decoding the original8 number as an API9 FVF. */
static HRESULT fvf8_declaration(D9Device *device, DWORD fvf,
        const D9WGVertexElement *wire, UINT count, D9VertexDeclaration **out)
{
    *out = NULL;
    for (D9VertexDeclaration *decl = device->vertex_declarations; decl;
            decl = decl->next_device_resource) {
        if (decl->legacy_fvf8 != fvf) continue;
        if (!decl->fvf_cache_ref) {
            decl_binding_add_ref(decl);
            decl->fvf_cache_ref = TRUE;
        }
        *out = decl;
        return D3D_OK;
    }
    D3DVERTEXELEMENT9 elements[33];
    for (UINT i = 0; i < count; ++i) {
        const D9WGVertexElement *e = &wire[i];
        elements[i] = (D3DVERTEXELEMENT9){e->stream, e->offset, e->type,
            e->method, e->usage, e->usage_index};
    }
    elements[count] = (D3DVERTEXELEMENT9)D3DDECL_END();
    IDirect3DVertexDeclaration9 *iface = NULL;
    HRESULT hr = IDirect3DDevice9_CreateVertexDeclaration(&device->iface,
            elements, &iface);
    if (FAILED(hr)) return hr;
    D9VertexDeclaration *decl = decl_from_iface(iface);
    decl->legacy_fvf8 = fvf;
    decl->fvf = 0;
    decl_binding_add_ref(decl);
    decl->fvf_cache_ref = TRUE;
    /* Cache is private storage ownership, not a public device parent pin. */
    IDirect3DVertexDeclaration9_Release(iface);
    *out = decl;
    return D3D_OK;
}

/* Backend binding only. Frontend8 retains the raw FVF-vs-DWORD-handle Get
 * shadow and its public error publication. Zero clears both shader/layout;
 * ordinary9 SetFVF(0) keeps its native successful-no-op contract. */
HRESULT WINAPI d9wg_d3d9_core_set_fvf8(IDirect3DDevice9 *iface, DWORD fvf)
{
    if (!iface || device_compatibility_api(device_from_iface(iface)) != 8u)
        return D3DERR_INVALIDCALL;
    D9Device *device = device_from_iface(iface);
    D9VertexDeclaration *decl = NULL;
    if (fvf) {
        D9WGVertexElement wire[32];
        UINT count;
        if (!d9wg_fvf_decode(D9WG_FVF_D3D8, fvf, wire, 32, &count, NULL))
            return D3DERR_INVALIDCALL;
        HRESULT hr = fvf8_declaration(device, fvf, wire, count, &decl);
        if (FAILED(hr)) return hr;
    }
    if (!device->recording_result && !device->vertex_shader
            && device->vertex_declaration == decl)
        return D3D_OK;
    HRESULT hr = IDirect3DDevice9_SetVertexShader(iface, NULL);
    if (FAILED(hr)) return hr;
    return IDirect3DDevice9_SetVertexDeclaration(iface,
            decl ? &decl->iface : NULL);
}

/* Use8 destination FVF policy and the same actual core9 software pipeline.
 * No temporary declaration object or transport-side layout is required. */
HRESULT WINAPI d9wg_d3d9_core_process_vertices8(IDirect3DDevice9 *iface,
        UINT src_start, UINT dst_index, UINT count,
        IDirect3DVertexBuffer9 *dst, DWORD flags)
{
    if (!iface || device_compatibility_api(device_from_iface(iface)) != 8u)
        return D3DERR_INVALIDCALL;
    D9Device *device = device_from_iface(iface);
    if (!dst || dst->lpVtbl != &g_vb_vtbl)
        return D3DERR_INVALIDCALL;
    D9VertexBuffer *destination = vb_from_iface(dst);
    if (destination->device != device || destination->locked || !count
            || device->vertex_shader)
        return D3DERR_INVALIDCALL;
    D9ProcessLayout layout;
    if (!d9wg_process_fvf8_subset(destination->fvf, &layout))
        return D3DERR_INVALIDCALL;
    TRACE_MARK_ENTER("Device.ProcessVertices");
    return process_vertices_layout(device, destination, &layout,
            src_start, dst_index, count, flags);
}

/* Build the mapped core portion from an already selected8 snapshot. This
 * reuses actual9 recording instead of interpreting8 masks as9 predefined
 * scopes. The frontend owns raw8 shadow fields and DWORD publication. */
HRESULT WINAPI d9wg_d3d9_core_record_state_block8(IDirect3DDevice9 *iface,
        D9WGStateBlockRecorder8 recorder, const void *snapshot,
        IDirect3DStateBlock9 **out)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = NULL;
    if (!iface || !recorder
            || device_compatibility_api(device_from_iface(iface)) != 8u)
        return D3DERR_INVALIDCALL;
    HRESULT hr = IDirect3DDevice9_BeginStateBlock(iface);
    if (FAILED(hr)) return hr;
    hr = recorder(iface, snapshot);
    if (SUCCEEDED(hr)) hr = IDirect3DDevice9_EndStateBlock(iface, out);
    if (FAILED(hr)) device_cancel_recording(device_from_iface(iface));
    return hr;
}

/* A token belongs to one compatibility8 device and is never exposed as a
 * public9 COM block. Temporary operation references also retain only storage. */
static D9StateBlock *state_block_handle8(D9Device *device, DWORD token)
{
    if (!token || device_compatibility_api(device) != 8u
            || device->releasing_owned_refs)
        return NULL;
    D9StateBlock *block = device->state_blocks;
    while (block && block->handle8 != token) block = block->next_device_resource;
    return block;
}

static void state_block_adopt_handle8(D9StateBlock *block, DWORD token)
{
    block->handle8 = token;
    block->device_parent_ref = FALSE;
    device_child_release(block->device);
}

static DWORD state_block_publish_handle8(D9StateBlock *block)
{
    DWORD token;
    do { token = allocate_handle(); }
    while (state_block_handle8(block->device, token));
    state_block_adopt_handle8(block, token);
    return token;
}

/* Incremental8 recording uses the same actual9 recorder as mapped snapshots.
 * A failed End leaves that recording intact and clears only the caller output. */
HRESULT WINAPI d9wg_d3d9_core_begin_state_block8(IDirect3DDevice9 *iface)
{
    if (!iface || device_compatibility_api(device_from_iface(iface)) != 8u
            || device_from_iface(iface)->releasing_owned_refs)
        return D3DERR_INVALIDCALL;
    return IDirect3DDevice9_BeginStateBlock(iface);
}

HRESULT WINAPI d9wg_d3d9_core_end_state_block_handle8(IDirect3DDevice9 *iface,
        DWORD *out)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = 0;
    if (!iface || device_compatibility_api(device_from_iface(iface)) != 8u
            || device_from_iface(iface)->releasing_owned_refs)
        return D3DERR_INVALIDCALL;
    IDirect3DStateBlock9 *created = NULL;
    IDirect3DDevice9_AddRef(iface);
    HRESULT hr = IDirect3DDevice9_EndStateBlock(iface, &created);
    if (SUCCEEDED(hr))
        *out = state_block_publish_handle8(state_block_from_iface(created));
    IDirect3DDevice9_Release(iface);
    return hr;
}

HRESULT WINAPI d9wg_d3d9_core_create_state_block_handle8(IDirect3DDevice9 *iface,
        D9WGStateBlockRecorder8 recorder, const void *snapshot, DWORD *out)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = 0;
    if (!iface || !recorder
            || device_compatibility_api(device_from_iface(iface)) != 8u
            || device_from_iface(iface)->releasing_owned_refs)
        return D3DERR_INVALIDCALL;
    IDirect3DStateBlock9 *created = NULL;
    IDirect3DDevice9_AddRef(iface);
    HRESULT hr = d9wg_d3d9_core_record_state_block8(iface, recorder, snapshot, &created);
    if (SUCCEEDED(hr))
        *out = state_block_publish_handle8(state_block_from_iface(created));
    IDirect3DDevice9_Release(iface);
    return hr;
}

HRESULT WINAPI d9wg_d3d9_core_apply_state_block_handle8(IDirect3DDevice9 *iface,
        DWORD token)
{
    if (!iface) return D3DERR_INVALIDCALL;
    D9StateBlock *block = state_block_handle8(device_from_iface(iface), token);
    if (!block) return D3DERR_INVALIDCALL;
    IDirect3DDevice9_AddRef(iface);
    IDirect3DStateBlock9_AddRef(&block->iface);
    HRESULT hr = IDirect3DStateBlock9_Apply(&block->iface);
    IDirect3DStateBlock9_Release(&block->iface);
    IDirect3DDevice9_Release(iface);
    return hr;
}

/* Frontend Capture supplies a newly selected raw8 snapshot. Rebuild first;
 * callback failure leaves the old token/value untouched. A reentrant deletion
 * or replacement wins over this call, so a retired token cannot be revived. */
HRESULT WINAPI d9wg_d3d9_core_capture_state_block_handle8(IDirect3DDevice9 *iface,
        DWORD token, D9WGStateBlockRecorder8 recorder, const void *snapshot)
{
    if (!iface || !recorder) return D3DERR_INVALIDCALL;
    D9Device *device = device_from_iface(iface);
    D9StateBlock *block = state_block_handle8(device, token);
    if (!block) return D3DERR_INVALIDCALL;
    IDirect3DStateBlock9 *created = NULL;
    IDirect3DDevice9_AddRef(iface);
    IDirect3DStateBlock9_AddRef(&block->iface);
    HRESULT hr = d9wg_d3d9_core_record_state_block8(iface, recorder, snapshot, &created);
    if (SUCCEEDED(hr)) {
        if (state_block_handle8(device, token) != block) {
            IDirect3DStateBlock9_Release(created);
            hr = D3DERR_INVALIDCALL;
        } else {
            block->handle8 = 0;
            state_block_adopt_handle8(state_block_from_iface(created), token);
            IDirect3DStateBlock9_Release(&block->iface); /* token ownership */
        }
    }
    IDirect3DStateBlock9_Release(&block->iface); /* operation storage pin */
    IDirect3DDevice9_Release(iface);
    return hr;
}

HRESULT WINAPI d9wg_d3d9_core_delete_state_block_handle8(IDirect3DDevice9 *iface,
        DWORD token)
{
    if (!iface) return D3DERR_INVALIDCALL;
    D9StateBlock *block = state_block_handle8(device_from_iface(iface), token);
    if (!block) return D3DERR_INVALIDCALL;
    IDirect3DDevice9_AddRef(iface);
    block->handle8 = 0;
    IDirect3DStateBlock9_Release(&block->iface);
    IDirect3DDevice9_Release(iface);
    return D3D_OK;
}

/* D3D8 keeps the unsigned BaseVertexIndex with its index binding. The
 * frontend passes that raw DWORD on every indexed draw; core9 consumes the
 * same bits as a signed per-draw base, without changing SetIndices state. */
HRESULT WINAPI d9wg_d3d9_core_draw_indexed8(IDirect3DDevice9 *iface,
        UINT primitive_type, UINT base_vertex_index,
        UINT min_vertex_index, UINT vertex_count, UINT start_index,
        UINT primitive_count)
{
    if (!iface || device_compatibility_api(device_from_iface(iface)) != 8u)
        return D3DERR_INVALIDCALL;
    INT signed_base;
    CopyMemory(&signed_base, &base_vertex_index, sizeof(signed_base));
    return IDirect3DDevice9_DrawIndexedPrimitive(iface, (D3DPRIMITIVETYPE)primitive_type,
            signed_base, min_vertex_index, vertex_count, start_index,
            primitive_count);
}

/* A raw8 DWORD handle owns storage, not a public9 resource/device pin.
 * The frontend pairs these holds, drops constructor COM refs separately,
 * and releases the holds during its device metadata retirement callback. */
static HRESULT shader_storage_reference8(IDirect3DDevice9 *iface,
        IUnknown *object, BOOL retain)
{
    if (!iface || iface->lpVtbl != &g_device_vtbl || !object)
        return D3DERR_INVALIDCALL;
    D9Device *device = device_from_iface(iface);
    if (device_compatibility_api(device) != 8u) return D3DERR_INVALIDCALL;
    const void *vtbl = object->lpVtbl;
    if (vtbl == &g_decl_vtbl) {
        D9VertexDeclaration *decl = (D9VertexDeclaration *)object;
        if (decl->device != device || (!retain
                && !InterlockedCompareExchange(&decl->binding_refs, 0, 0)))
            return D3DERR_INVALIDCALL;
        if (retain) decl_binding_add_ref(decl);
        else decl_binding_release(decl);
        return D3D_OK;
    }
    if (vtbl == &g_vertex_shader_vtbl
            || vtbl == &g_pixel_shader_vtbl) {
        D9Shader *shader = (D9Shader *)object;
        if (shader->device != device || (!retain
                && !InterlockedCompareExchange(&shader->binding_refs, 0, 0)))
            return D3DERR_INVALIDCALL;
        if (retain) shader_binding_add_ref(shader);
        else shader_binding_release(shader);
        return D3D_OK;
    }
    return D3DERR_INVALIDCALL;
}

HRESULT WINAPI d9wg_d3d9_core_shader_storage_add_ref8(IDirect3DDevice9 *device,
        IUnknown *object)
{
    return shader_storage_reference8(device, object, TRUE);
}

HRESULT WINAPI d9wg_d3d9_core_shader_storage_release8(IDirect3DDevice9 *device,
        IUnknown *object)
{
    return shader_storage_reference8(device, object, FALSE);
}

/* Backend for an already resolved8 DWORD shader handle. Validate both
 * owners before either setter changes state; public raw-handle queries and
 * deletion remain frontend responsibilities. NULL/NULL is the8 zero bind. */
HRESULT WINAPI d9wg_d3d9_core_set_vertex_shader8(IDirect3DDevice9 *iface,
        IDirect3DVertexDeclaration9 *layout_iface,
        IDirect3DVertexShader9 *shader_iface)
{
    if (!iface || device_compatibility_api(device_from_iface(iface)) != 8u)
        return D3DERR_INVALIDCALL;
    if (!layout_iface)
        return shader_iface ? D3DERR_INVALIDCALL
                : d9wg_d3d9_core_set_fvf8(iface, 0);
    D9Device *device = device_from_iface(iface);
    D9VertexDeclaration *layout = decl_from_iface(layout_iface);
    D9Shader *shader = shader_iface ? vertex_shader_from_iface(shader_iface) : NULL;
    if (layout_iface->lpVtbl != &g_decl_vtbl || layout->device != device
            || (shader_iface && (shader_iface->lpVtbl != &g_vertex_shader_vtbl
                || shader->device != device)))
        return D3DERR_INVALIDCALL;
    if (!device->recording_result && device->vertex_declaration == layout
            && device->vertex_shader == shader)
        return D3D_OK;
    HRESULT hr = IDirect3DDevice9_SetVertexDeclaration(iface, layout_iface);
    if (FAILED(hr)) return hr;
    return IDirect3DDevice9_SetVertexShader(iface, shader_iface);
}

/* Raw8 GetDeclaration/GetFunction and DWORD handle ownership belong to
 * the frontend. Returned shader storage includes injected local DEFs and
 * END, while this entry validates the original unexpanded8 input bound. */
HRESULT WINAPI d9wg_d3d9_core_vertex_shader8(IDirect3DDevice9 *iface,
        const DWORD *declaration, const DWORD *function,
        IDirect3DVertexDeclaration9 **layout_out,
        IDirect3DVertexShader9 **shader_out)
{
    if (layout_out) *layout_out = NULL;
    if (shader_out) *shader_out = NULL;
    if (!layout_out || !shader_out || !iface || !declaration
            || device_compatibility_api(device_from_iface(iface)) != 8u)
        return D3DERR_INVALIDCALL;
    UINT body = 0;
    if (function && ((function[0] >> 16) != 0xfffeu
            || ((function[0] >> 8) & 0xffu) != 1u
            || (function[0] & 0xffu) > 1u
            || !shader8_validate_body(function + 1, D9WG_D3D8_SHADER_TOKENS,
                FALSE, function[0] & 0xffu, &body)))
        return D3DERR_INVALIDCALL;
    UINT tokens, elements, vectors;
    D9WGVertexElement wire[32];
    DWORD constants[96 * 4];
    BYTE mask[96];
    if (!d9wg_declaration8_validate(declaration, D9WG_D3D8_SHADER_TOKENS, &tokens)
            || !d9wg_declaration8_elements(declaration, tokens, wire, 32,
                &elements, constants, 96, mask, &vectors))
        return D3DERR_INVALIDCALL;
    IDirect3DVertexDeclaration9 *layout = NULL;
    HRESULT hr = d9wg_d3d9_core_vertex_declaration8(iface, declaration,
            function == NULL, &layout);
    if (FAILED(hr)) return hr;
    if (function) {
        UINT count = body + 2u + vectors * 6u;
        DWORD *expanded = HeapAlloc(GetProcessHeap(), 0, count * sizeof(DWORD));
        if (!expanded) {
            IDirect3DVertexDeclaration9_Release(layout);
            return E_OUTOFMEMORY;
        }
        d9wg_shader8_build_local_defs(expanded, function, body + 2u,
                constants, mask, 96);
        D9Shader *shader = NULL;
        hr = create_shader_object(device_from_iface(iface), expanded,
                count, FALSE, &shader);
        HeapFree(GetProcessHeap(), 0, expanded);
        if (FAILED(hr)) {
            IDirect3DVertexDeclaration9_Release(layout);
            return hr;
        }
        *shader_out = &shader->iface.vertex;
    }
    *layout_out = layout;
    return D3D_OK;
}

HRESULT WINAPI d9wg_d3d9_core_pixel_shader8(IDirect3DDevice9 *iface,
        const DWORD *function, IDirect3DPixelShader9 **out)
{
    if (!out) return D3DERR_INVALIDCALL;
    *out = NULL;
    if (!iface || device_compatibility_api(device_from_iface(iface)) != 8u)
        return D3DERR_INVALIDCALL;
    return IDirect3DDevice9_CreatePixelShader(iface, function, out);
}

BOOL WINAPI d9wg_d3d9_core_validate_shader_body8(const DWORD *tokens, UINT max_tokens,
        BOOL is_pixel, UINT minor, UINT *body_count)
{
    if (!tokens || !body_count) return FALSE;
    return shader8_validate_body(tokens, max_tokens, is_pixel, minor, body_count);
}

#endif

/*
 * Secondary d3d9.dll exports. A title that statically imports any of these
 * fails to LOAD against a DLL that omits them -- Direct3DCreate9 is never
 * reached, and the failure surfaces as a generic "unable to initialize
 * DirectX" with no diagnostic. The D3D8 path hit exactly this with Warcraft
 * III's ValidateVertexShader/ValidatePixelShader imports; D3DPERF_* (PIX
 * instrumentation hooks) and DebugSetMute are the D3D9-side equivalent risk,
 * commonly pulled in by profiling-instrumented engine builds even when the
 * game never calls them at runtime. All are harmless no-ops here.
 */
#ifndef D9WG_STATIC_CORE
void WINAPI DebugSetMute(void)
{
}

int WINAPI D3DPERF_BeginEvent(D3DCOLOR color, const WCHAR *name)
{
    (void)color; (void)name;
    return 0;
}

int WINAPI D3DPERF_EndEvent(void)
{
    return 0;
}

void WINAPI D3DPERF_SetMarker(D3DCOLOR color, const WCHAR *name)
{
    (void)color; (void)name;
}

void WINAPI D3DPERF_SetRegion(D3DCOLOR color, const WCHAR *name)
{
    (void)color; (void)name;
}

WINBOOL WINAPI D3DPERF_QueryRepeatFrame(void)
{
    return FALSE;
}

void WINAPI D3DPERF_SetOptions(DWORD options)
{
    (void)options;
}

DWORD WINAPI D3DPERF_GetStatus(void)
{
    return 0;
}

#endif

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_DETACH && (reserved || !g_process))
        return TRUE; /* OS handle cleanup ends a terminating process owner. */

    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
        g_process = d9wg_process_attach();
        if (!g_process) return FALSE;
        DisableThreadLibraryCalls(instance);
        g_module_instance = instance;
#ifdef D9WG_DIAGNOSTIC_TRACE
        trace_open(instance);
        g_trace_veh = AddVectoredExceptionHandler(1, trace_vectored_exception);
        TRACE("PROCESS_ATTACH diagnostic trace v5 build=%s "
                "pid=%lu module=%08lX veh=%08lX", D9_PROXY_BUILD,
                GetCurrentProcessId(), (DWORD)(uintptr_t)instance,
                (DWORD)(uintptr_t)g_trace_veh);
        TRACE("MODULE exe=%08lX path=%s proxy=%08lX path=%s",
                (DWORD)(uintptr_t)g_trace_exe_module, g_trace_exe_path,
                (DWORD)(uintptr_t)g_trace_self_module, g_trace_self_path);
        /* The baseline every later MEM sample is read against. */
        trace_memory_state("process_attach");
        TRACE_FLUSH();
#endif
    } else if (reason == DLL_PROCESS_DETACH) {
        TRACE("PROCESS_DETACH reserved=%08lX pending_commands=%lu",
                (DWORD)(uintptr_t)reserved, g_command_count);
#ifdef D9WG_DIAGNOSTIC_TRACE
        trace_memory_state("process_detach");
        trace_window_state();
        trace_last_call("process_detach");
#endif
        TRACE_FLUSH();
        EnterCriticalSection(&g_transport_lock);
        /* The host releases whatever this process left alive -- devices,
         * resources, its last frame -- instead of keeping them until the page
         * reloads (review S1-10; v86gl.sys's unmap covers a killed process) */
        if (g_process->references == 1 && g_dma_buffer) {
            D9WGSessionEnd end;
            uint8_t *destination;
            end.session_id_low = g_session_id_low;
            end.session_id_high = g_session_id_high;
            if (reserve_command_locked(D9WG_OP_SESSION_END, sizeof(end), 0, NULL,
                    &destination, NULL))
                CopyMemory(destination, &end, sizeof(end));
        }
        if (g_command_count)
            submit_batch_locked(FALSE);
        if (g_process->references == 1) close_transport_locked();
        LeaveCriticalSection(&g_transport_lock);
        d9wg_process_release(g_process);
        g_process = NULL;
#ifdef D9WG_DIAGNOSTIC_TRACE
        trace_remove_message_hooks();
        if (g_trace_veh) {
            RemoveVectoredExceptionHandler(g_trace_veh);
            g_trace_veh = NULL;
        }
        trace_close();
#endif
    }
    return TRUE;
}
