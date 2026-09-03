/*
 * App-local Direct3D 9 frontend for Windows XP guests running in v86.
 *
 * Independent from d3d8.dll: separate COM objects, separate D9WG protocol
 * (d3d9_protocol.h), separate host executor. A game directory loads exactly
 * one of d3d8.dll/d3d9.dll/opengl32.dll -- never more than one.
 *
 * Current scope: device/resource lifecycle, vertex declarations and FVF,
 * vertex/index buffers, 2D/cube/volume textures, integer and floating-point
 * render targets, four-sample MSAA resolves, the fixed-function draw path,
 * shader model 1.1-3.0 (including vertex texture fetch), instancing, user clip
 * planes, state blocks, asynchronous GPU queries, and GPU render-target
 * readback through the response tail of the shared DMA arena.
 *
 * Legacy entry points which do not map to the advertised drawing profile are
 * still rejected explicitly: ProcessVertices, additional swap chains,
 * palettized textures, higher-order patches, and Surface::GetDC/ReleaseDC.
 *
 * The discipline throughout: an entry point either does what D3D9 says or
 * fails, and GetDeviceCaps only claims what is actually implemented. Where an
 * approximation is unavoidable it is exposed through the host's bounded
 * getStats() counters.
 */

#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <d3d9.h>
#include <stdint.h>
#ifdef D9WG_DIAGNOSTIC_TRACE
#include <stdarg.h>
#endif
#include "../openglproxy/v86gl_ioctl.h"
#include "d3d9_protocol.h"

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
 * build_diagnostic.sh; the ordinary d3d9.dll has no file I/O or VEH overhead.
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
    if (length + sizeof("d3d9_trace_4294967295.log") > MAX_PATH)
        return;
    wsprintfA(path + length, "d3d9_trace_%lu.log", GetCurrentProcessId());
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
#define D9_MAX_PALETTES 16u

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
#define D9_VGL2_RECORD_HEADER_BYTES 8u
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

struct D9Direct3D {
    IDirect3D9 iface;
    LONG refcount;
};

/*
 * The primary swap chain is created implicitly with the device.  Keep its
 * COM identity embedded in D9Device so repeated GetSwapChain(0) calls return
 * the same object, just as the native runtime does.  The device itself does
 * not own a COM reference to this object (that would form a cycle); every
 * external swap-chain reference holds one ordinary device reference instead.
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
    D3DVIEWPORT9 viewport;
    float transforms[D9_MAX_TRANSFORMS][16];
    DWORD render_states[D9_MAX_RENDER_STATES];
    DWORD texture_stage_states[D9_MAX_TEXTURE_STAGES][D9_MAX_TEXTURE_STAGE_STATES];
    DWORD sampler_states[D9_MAX_SAMPLERS][D9_MAX_SAMPLER_STATES];
    D3DMATERIAL9 material;
    /* The last ramp SetGammaRamp was given, kept so GetGammaRamp answers with
     * it rather than with zeroes. D3D9 has no "no ramp set" state -- a device
     * starts at identity -- so `gamma_ramp_set` only records whether the host
     * has been told, which decides whether Reset has to tell it again. */
    D3DGAMMARAMP gamma_ramp;
    BOOL gamma_ramp_set;
    /* Only meaningful on a D3DCREATE_MIXED_VERTEXPROCESSING device; the other
     * two modes are fixed at creation and answer from BehaviorFlags. */
    BOOL software_vertex_processing;
    /* SetNPatchMode's level, or 0 when tessellation is off. Zero is the gate
     * the draw path checks, so the default costs one comparison. */
    float npatch_segments;
    D3DLIGHT9 lights[D9_MAX_LIGHTS];
    BOOL light_set[D9_MAX_LIGHTS];
    BOOL light_enabled[D9_MAX_LIGHTS];
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
    /* Non-NULL between BeginStateBlock and EndStateBlock: `recording` is the
     * pre-Begin snapshot used to restore live device state, while
     * `recording_result` receives an explicit mask entry for every Set* call.
     * Keeping the call mask is essential: setting a state to its current value
     * is still part of a D3D9 state block. */
    D9StateBlock *recording;
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
    D3DCLIPSTATUS9 clip_status;
    D9Shader *vertex_shader;
    D9Shader *pixel_shader;
    BOOL cursor_ready;
    BOOL cursor_visible;
    /* Set once the application drives the D3D9 hardware cursor itself, which
     * takes precedence over the GDI capture fallback. */
    BOOL app_cursor;
    HCURSOR system_cursor;
    BOOL display_mode_changed;
    BOOL window_state_sent;
    /* Saved across the borderless override an exclusive-fullscreen device
     * applies to its window, so the app's own frame comes back on teardown. */
    HWND styled_window;
    LONG saved_window_style;
    LONG saved_window_exstyle;
    /* Reconstructs the vertical-retrace wait Present owes the app; see
     * throttle_to_presentation_interval(). */
    DWORD last_present_tick;
    /* Rate limiting for maintain_fullscreen_foreground(). */
    DWORD last_foreground_claim;
    DWORD foreground_claims;
    uint32_t last_window_flags;
    uint32_t last_foreground;
    /*
     * D3D9's constant registers are device state, not shader state: they
     * survive SetVertexShader and are what the app expects to still be there
     * after a Reset. Holding the authoritative copy here (rather than only on
     * the host) is what lets set_shader_constant_f() suppress the very common
     * "set the same matrix palette again every frame" traffic and lets
     * recreate_device_resources() replay the live values after a Reset.
     */
    float vs_const_f[D9_MAX_VS_CONST_F][4];
    int vs_const_i[D9_MAX_CONST_I][4];
    BOOL vs_const_b[D9_MAX_CONST_B];
    float ps_const_f[D9_MAX_PS_CONST_F][4];
    int ps_const_i[D9_MAX_CONST_I][4];
    BOOL ps_const_b[D9_MAX_CONST_B];
    /*
     * Texture palettes, held here for the same reason as the constant
     * registers: D3D9 lets an app read its palette back, and a palette is
     * device state that outlives any one texture. The count is a local limit
     * rather than a D3D9 one -- the API takes an arbitrary UINT index -- but a
     * palettized title uses a handful, and a sparse map would cost more than
     * the table it replaced.
     */
    DWORD palettes[D9_MAX_PALETTES][256];
    BOOL palette_valid[D9_MAX_PALETTES];
    UINT current_palette;
    BOOL current_palette_set;
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
    D9Device *device;
    uint32_t handle;
    BYTE *shadow;
    UINT length;
    DWORD usage;
    DWORD fvf;
    D3DPOOL pool;
    DWORD priority;
    UINT lock_offset;
    UINT lock_size;
    DWORD lock_flags;
    BOOL locked;
    D9VertexBuffer *next_device_resource;
};

struct D9IndexBuffer {
    IDirect3DIndexBuffer9 iface;
    LONG refcount;
    D9Device *device;
    uint32_t handle;
    BYTE *shadow;
    UINT length;
    DWORD usage;
    D3DFORMAT format;
    D3DPOOL pool;
    DWORD priority;
    UINT lock_offset;
    UINT lock_size;
    DWORD lock_flags;
    BOOL locked;
    D9IndexBuffer *next_device_resource;
};

struct D9Texture {
    IDirect3DTexture9 iface;
    LONG refcount;
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
    D9TextureLevel *levels;
    D9Texture *next_device_resource;
    /* D3DUSAGE_AUTOGENMIPMAP only: the filter the app asked the driver to
     * downsample with. Zero means it never said, which D3D9 reports as
     * D3DTEXF_LINEAR. */
    D3DTEXTUREFILTERTYPE autogen_filter;
};

struct D9CubeTexture {
    IDirect3DCubeTexture9 iface;
    LONG refcount;
    D9Device *device;
    uint32_t handle;
    UINT edge_length;
    UINT level_count;
    DWORD usage;
    D3DFORMAT format;
    D3DPOOL pool;
    DWORD priority;
    DWORD lod;
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
    D9TextureLevel *levels;
    D9VolumeTexture *next_device_resource;
};

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
    D9Device *device;
    uint32_t handle;
    UINT element_count;
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
 * Such a surface behaves like a texture level surface: AddRef/Release adjust
 * the parent's reference count, and the object itself survives to the parent's
 * teardown, so an app is free to drop its reference and keep using the pointer.
 */
static BOOL surface_is_implicit(const D9Surface *surface)
{
    return surface->swap_chain != NULL || surface->auto_depth_stencil;
}

/* BeginStateBlock records calls, including calls which write the value the
 * device already has.  The block structure itself lives near its COM methods;
 * these helpers keep its layout out of the ordinary Set* implementations. */
static void state_block_record_render_state(D9Device *device, UINT state);
static void state_block_record_texture_stage_state(D9Device *device,
        UINT stage, UINT state);
static void state_block_record_sampler_state(D9Device *device,
        UINT sampler, UINT state);
static void state_block_record_transform(D9Device *device, UINT state);
static void state_block_record_texture(D9Device *device, UINT stage);
static void state_block_record_material(D9Device *device);
static void state_block_record_light(D9Device *device, UINT index);
static void state_block_record_light_enable(D9Device *device, UINT index);
static void state_block_record_viewport(D9Device *device);
static void state_block_record_scissor(D9Device *device);
static void state_block_record_vertex_shader(D9Device *device);
static void state_block_record_pixel_shader(D9Device *device);
static void state_block_record_constant(D9Device *device, BOOL is_pixel,
        BOOL is_float, BOOL is_bool, UINT start, UINT count);
static void state_block_record_vertex_format(D9Device *device);
static void state_block_record_stream(D9Device *device, UINT stream);
static void state_block_record_indices(D9Device *device);

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
static BOOL recreate_device_resources(D9Device *device);
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
static void emit_window_state(D9Device *device, HWND window);
static void claim_fullscreen_foreground(D9Device *device, HWND window);
static void maintain_fullscreen_foreground(D9Device *device, HWND window);
static void restore_display_mode(D9Device *device);

static D9Direct3D *d3d_from_iface(IDirect3D9 *iface)
{
    return (D9Direct3D *)iface;
}

static D9Device *device_from_iface(IDirect3DDevice9 *iface)
{
    return (D9Device *)iface;
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

static uint32_t g_next_handle = D9_HANDLE_GENERATION_ONE;

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
static uint32_t g_next_shader_handle = D9WG_SHADER_HANDLE_BASE;

static uint32_t allocate_shader_handle(void)
{
    return (uint32_t)InterlockedExchangeAdd((LONG *)&g_next_shader_handle, 2);
}

/* ---- VGL2 transport / D9WG batch buffer ---- */

static HANDLE g_transport = INVALID_HANDLE_VALUE;
static uint8_t *g_dma_buffer;
static uint32_t g_dma_capacity;
static uint32_t g_batch_bytes;
static uint32_t g_command_count;
static uint32_t g_frame_id = 1;
static uint32_t g_sequence = 1;
static uint32_t g_session_id_low;
static uint32_t g_session_id_high;
static BOOL g_transport_failed;
static BOOL g_hello_emitted;
static CRITICAL_SECTION g_transport_lock;
static CRITICAL_SECTION g_readback_lock;
static volatile LONG g_query_slots[D9WG_QUERY_SLOT_COUNT];
static volatile LONG g_response_request_id;

static uint8_t *batch_base(void)
{
    return g_dma_buffer + sizeof(V86GLDMADesc) + D9_VGL2_RECORD_HEADER_BYTES;
}

static uint32_t batch_capacity(void)
{
    if (g_dma_capacity <= D9WG_RESPONSE_REGION_BYTES
            + sizeof(V86GLDMADesc) + D9_VGL2_RECORD_HEADER_BYTES)
        return 0;
    return g_dma_capacity - (uint32_t)sizeof(V86GLDMADesc)
            - D9_VGL2_RECORD_HEADER_BYTES - D9WG_RESPONSE_REGION_BYTES;
}

static uint8_t *response_region(void)
{
    if (!g_dma_buffer || g_dma_capacity < D9WG_RESPONSE_REGION_BYTES)
        return NULL;
    return g_dma_buffer + g_dma_capacity - D9WG_RESPONSE_REGION_BYTES;
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

static BOOL open_transport_locked(void)
{
    V86GLMapBuffer mapping;
    DWORD returned = 0;

    if (g_dma_buffer)
        return TRUE;
    if (g_transport_failed)
        return FALSE;

    g_transport = CreateFileA(V86GL_DEVICE_DOS_NAME,
            GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL, NULL);
    if (g_transport == INVALID_HANDLE_VALUE) {
        g_transport_failed = TRUE;
        return FALSE;
    }

    ZeroMemory(&mapping, sizeof(mapping));
    if (!DeviceIoControl(g_transport, V86GL_IOCTL_MAP_BUFFER,
            NULL, 0, &mapping, sizeof(mapping), &returned, NULL)
            || returned != sizeof(mapping)
            || !mapping.user_address
            || mapping.buffer_bytes < sizeof(V86GLDMADesc)
                    + D9_VGL2_RECORD_HEADER_BYTES
                    + sizeof(D9WGBatchHeader)
                    + sizeof(D9WGCommandHeader)) {
        close_transport_locked();
        g_transport_failed = TRUE;
        return FALSE;
    }

    g_dma_buffer = (uint8_t *)(uintptr_t)mapping.user_address;
    g_dma_capacity = mapping.buffer_bytes;
    if (g_dma_capacity <= D9WG_RESPONSE_REGION_BYTES
            + sizeof(V86GLDMADesc) + D9_VGL2_RECORD_HEADER_BYTES
            + sizeof(D9WGBatchHeader) + sizeof(D9WGCommandHeader)) {
        close_transport_locked();
        g_transport_failed = TRUE;
        return FALSE;
    }
    ZeroMemory(response_region(), D9WG_RESPONSE_REGION_BYTES);
    reset_batch_locked();
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

    batch = (D9WGBatchHeader *)batch_base();
    batch->frame_id = g_frame_id;
    batch->flags = present ? D9WG_BATCH_FLAG_PRESENT : 0;
    batch->command_count = g_command_count;
    batch->command_bytes = g_batch_bytes - sizeof(*batch);
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

    outer_bytes = D9_VGL2_RECORD_HEADER_BYTES + g_batch_bytes;
    descriptor = (V86GLDMADesc *)g_dma_buffer;
    descriptor->magic = V86GL_MAGIC;
    descriptor->version = V86GL_VERSION;
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
        close_transport_locked();
        g_transport_failed = TRUE;
        return FALSE;
    }

    if (present)
        ++g_frame_id;
    reset_batch_locked();
    return TRUE;
}

static BOOL reserve_command_locked(uint16_t opcode, uint32_t payload_bytes,
        uint32_t extra_bytes, D9WGCommandHeader **command_out,
        uint8_t **payload_out, uint8_t **extra_out)
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
    if (command_out)
        *command_out = command;
    if (payload_out)
        *payload_out = (uint8_t *)(command + 1);
    if (extra_out)
        *extra_out = (uint8_t *)(command + 1) + payload_bytes;
    g_batch_bytes += record_size;
    ++g_command_count;
    /*
     * This is the single chokepoint every host-bound call passes through, so
     *
     *     TRACE("CMD opcode=%04lX seq=%lu bytes=%lu pending=%lu",
     *             (DWORD)opcode, (DWORD)command->sequence, record_size,
     *             g_command_count);
     *
     * here covers in one line the ~100 device methods that have no trace of
     * their own -- SetRenderState, SetTextureStageState, SetTransform and the
     * rest -- which is what identified where GTA San Andreas stopped during
     * startup. It is not compiled in because it is one WriteFile per command:
     * invaluable up to the first frame, unusable once a title is drawing.
     * Opcodes are numeric; d3d9_protocol.h decodes them.
     */
    return TRUE;
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
#define D9_PROXY_BUILD "adapter-mode-boundary-20260903"

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

static BOOL emit_buffer_update(uint32_t handle, uint32_t destination_offset,
        const void *data, uint32_t byte_count, uint32_t lock_flags)
{
    D9WGUpdateBuffer update;
    uint8_t *payload;
    uint8_t *blob;
    BOOL result;

    ZeroMemory(&update, sizeof(update));
    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_UPDATE_BUFFER,
            sizeof(update), byte_count, NULL, &payload, &blob);
    if (result) {
        update.resource_handle = handle;
        update.destination_offset = destination_offset;
        update.byte_count = byte_count;
        update.data_offset = (uint32_t)(blob - batch_base());
        update.lock_flags = lock_flags;
        CopyMemory(payload, &update, sizeof(update));
        if (byte_count)
            CopyMemory(blob, data, byte_count);
    }
    LeaveCriticalSection(&g_transport_lock);
    return result;
}

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
    *block_width = 1;
    *block_height = 1;
    switch (format) {
    case D3DFMT_R8G8B8:
        *block_bytes = 3;
        return TRUE;
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
    case D3DFMT_A8B8G8R8:
    case D3DFMT_X8B8G8R8:
    case D3DFMT_X8L8V8U8:
    case D3DFMT_Q8W8V8U8:
    case D3DFMT_V16U16:
    case D3DFMT_A2W10V10U10:
    case D3DFMT_A2B10G10R10:
    case D3DFMT_G16R16:
    case D3DFMT_A2R10G10B10:
    case D3DFMT_G16R16F:
    case D3DFMT_R32F:
        *block_bytes = 4;
        return TRUE;
    case D3DFMT_A16B16G16R16:
    case D3DFMT_A16B16G16R16F:
    case D3DFMT_G32R32F:
        *block_bytes = 8;
        return TRUE;
    case D3DFMT_A32B32G32R32F:
        *block_bytes = 16;
        return TRUE;
    case D3DFMT_R5G6B5:
    case D3DFMT_X1R5G5B5:
    case D3DFMT_A1R5G5B5:
    case D3DFMT_A4R4G4B4:
    case D3DFMT_A8R3G3B2:
    case D3DFMT_X4R4G4B4:
    case D3DFMT_A8L8:
    case D3DFMT_V8U8:
    case D3DFMT_L6V5U5:
    case D3DFMT_CxV8U8:
    case D3DFMT_L16:
    case D3DFMT_R16F:
        *block_bytes = 2;
        return TRUE;
    case D3DFMT_R3G3B2:
    case D3DFMT_L8:
    case D3DFMT_A8:
    case D3DFMT_A4L4:
    case D3DFMT_P8:
        *block_bytes = 1;
        return TRUE;
    case D3DFMT_A8P8:
        *block_bytes = 2;
        return TRUE;
    case D3DFMT_Q16W16V16U16:
        *block_bytes = 8;
        return TRUE;
    /* Two texels share one 32-bit block, so the block is 2x1 rather than the
     * 4x4 a BCn block is -- the same mechanism, a different shape. D3D9
     * requires an even width for these, which a 2-wide block enforces. */
    case D3DFMT_UYVY:
    case D3DFMT_YUY2:
    case D3DFMT_R8G8_B8G8:
    case D3DFMT_G8R8_G8B8:
        *block_width = 2;
        *block_bytes = 4;
        return TRUE;
    case D3DFMT_DXT1:
        *block_width = 4;
        *block_height = 4;
        *block_bytes = 8;
        return TRUE;
    case D3DFMT_DXT2:
    case D3DFMT_DXT3:
    case D3DFMT_DXT4:
    case D3DFMT_DXT5:
        *block_width = 4;
        *block_height = 4;
        *block_bytes = 16;
        return TRUE;
    default:
        /* Outside the switch because a FOURCC is not a D3DFORMAT enumerator.
         * ATI1N is one BC4 block (8 bytes) and ATI2N two of them (16). */
        if (format == D9FMT_ATI1N) {
            *block_width = 4;
            *block_height = 4;
            *block_bytes = 8;
            return TRUE;
        }
        if (format == D9FMT_ATI2N) {
            *block_width = 4;
            *block_height = 4;
            *block_bytes = 16;
            return TRUE;
        }
        return FALSE;
    }
}

static BOOL texture_level_layout(D3DFORMAT format, UINT width, UINT height,
        UINT *row_pitch, UINT *row_count, UINT *byte_count)
{
    UINT block_width;
    UINT block_height;
    UINT block_bytes;
    UINT columns;

    if (!texture_format_layout(format, &block_width, &block_height,
            &block_bytes))
        return FALSE;
    columns = (width + block_width - 1u) / block_width;
    *row_count = (height + block_height - 1u) / block_height;
    return multiply_u32(columns, block_bytes, row_pitch)
            && multiply_u32(*row_pitch, *row_count, byte_count);
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
static BOOL emit_generate_mips(D9Device *device, uint32_t resource_handle);
/* N-patch tessellation, defined with the other higher-order primitive code far
 * below but reached from the draw path above it. Returns D3DERR_INVALIDCALL for
 * a draw it cannot tessellate, which the callers treat as "draw it flat"
 * rather than as a failure: a mesh without normals should still appear. */
static HRESULT d9_draw_npatch(IDirect3DDevice9 *iface, D9Device *device,
        D3DPRIMITIVETYPE primitive_type, UINT primitive_count,
        const void *indices, UINT index_size, UINT base_vertex);
/*
 * IDirect3DBaseTexture9::SetLOD. D3D9 defines it as a no-op on anything but a
 * MANAGED texture, so the three callers below apply it under the same
 * condition and only then tell the host -- a host that clamped a DEFAULT-pool
 * texture's LOD would be honouring a call D3D9 ignores.
 */
static BOOL emit_texture_lod(D9Device *device, uint32_t resource_handle,
        DWORD lod);
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

static BOOL emit_texture_update(D9Texture *texture, UINT level,
        const RECT *rect)
{
    D9TextureLevel *level_data = &texture->levels[level];
    D9WGUpdateTexture update;
    UINT block_width;
    UINT block_height;
    UINT block_bytes;
    UINT block_x;
    UINT block_y;
    UINT row_bytes;
    UINT row_count;
    UINT data_bytes;
    UINT row;
    UINT band_start;
    UINT rows_per_band;
    uint32_t capacity;
    uint32_t overhead;
    uint8_t *payload;
    uint8_t *blob;
    BOOL result;

    if (!texture_format_layout(texture->format, &block_width, &block_height,
            &block_bytes))
        return FALSE;
    block_x = (UINT)rect->left / block_width;
    block_y = (UINT)rect->top / block_height;
    if (!multiply_u32(((UINT)(rect->right - rect->left)
            + block_width - 1u) / block_width, block_bytes, &row_bytes))
        return FALSE;
    row_count = ((UINT)(rect->bottom - rect->top)
            + block_height - 1u) / block_height;
    /* Only an overflow guard now that the upload is banded: no single record
     * carries the whole rectangle, but the total still has to be addressable. */
    if (!multiply_u32(row_bytes, row_count, &data_bytes))
        return FALSE;
    (void)data_bytes;

    /*
     * One record has to fit in the DMA arena whole, and a single mip level can
     * be larger than the arena is: 2048x2048 A8R8G8B8 is exactly 16 MiB, which
     * is the arena's entire size before its own headers are counted. That is an
     * ordinary texture -- the caps advertise 8192x8192 -- so the upload is sent
     * as bands of whole rows rather than refused. Refusing it was silent in the
     * only way that matters: UnlockRect returned a failure the app ignored, the
     * level was never uploaded, and the first symptom was the host warning that
     * a bound texture samples levels nobody ever filled.
     *
     * Bands need nothing from the wire format -- UPDATE_TEXTURE already names a
     * sub-rectangle -- and the transport lock is held across all of them so no
     * other thread can interleave commands into the middle of one upload.
     */
    if (!row_bytes)
        return FALSE;
    EnterCriticalSection(&g_transport_lock);
    capacity = batch_capacity();
    overhead = (uint32_t)(sizeof(D9WGBatchHeader) + sizeof(D9WGCommandHeader)
            + sizeof(update) + 8u /* D9WG_ALIGN8 slack */);
    rows_per_band = capacity > overhead ? (capacity - overhead) / row_bytes : 0;
    if (!rows_per_band) {
        /* A single row does not fit; there is nothing left to split. */
        LeaveCriticalSection(&g_transport_lock);
        return FALSE;
    }
    if (rows_per_band > row_count)
        rows_per_band = row_count;

    result = TRUE;
    for (band_start = 0; band_start < row_count && result;
            band_start += rows_per_band) {
        UINT band_rows = row_count - band_start;
        LONG band_top;
        LONG band_bottom;

        if (band_rows > rows_per_band)
            band_rows = rows_per_band;
        /* Texel coordinates, so a block format's band spans block_height rows
         * of blocks; the final band stops at the rect rather than past it. */
        band_top = rect->top + (LONG)(band_start * block_height);
        band_bottom = band_top + (LONG)(band_rows * block_height);
        if (band_bottom > rect->bottom)
            band_bottom = rect->bottom;

        ZeroMemory(&update, sizeof(update));
        update.resource_handle = texture->handle;
        update.level = level;
        update.x = (uint32_t)rect->left;
        update.y = (uint32_t)band_top;
        update.z = 0;
        update.width = (uint32_t)(rect->right - rect->left);
        update.height = (uint32_t)(band_bottom - band_top);
        update.depth = 1;
        update.row_pitch = row_bytes;
        update.slice_pitch = 0;
        update.data_bytes = row_bytes * band_rows;

        result = reserve_command_locked(D9WG_OP_UPDATE_TEXTURE,
                sizeof(update), update.data_bytes, NULL, &payload, &blob);
        if (result) {
            update.data_offset = (uint32_t)(blob - batch_base());
            CopyMemory(payload, &update, sizeof(update));
            for (row = 0; row < band_rows; ++row) {
                CopyMemory(blob + row * row_bytes,
                        level_data->shadow
                        + (block_y + band_start + row) * level_data->row_pitch
                        + block_x * block_bytes, row_bytes);
            }
        }
    }
    LeaveCriticalSection(&g_transport_lock);
    return result;
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
/* D3DFVF_XYZB1..XYZB5 are the five blended position codes, contiguous in steps
 * of 2 between XYZB1 (0x0006) and XYZB5 (0x000e). XYZRHW (0x0004) sits below
 * them and XYZW (0x4002) carries a bit outside this range, so both are excluded
 * before the range test rather than by it. */
static BOOL fvf_position_is_blended(DWORD fvf)
{
    DWORD position = fvf & D3DFVF_POSITION_MASK;
    return position >= D3DFVF_XYZB1 && position <= D3DFVF_XYZB5;
}

/* Whether the last of a D3DFVF_XYZBn block's DWORDs holds matrix indices
 * rather than another weight. Either LASTBETA flag says so outright; XYZB5 says
 * so implicitly, because five float weights have no D3DDECLTYPE to be declared
 * as and D3D9 has always read that last DWORD as indices. Same rule wined3d
 * applies, and getting it wrong shifts every following element's offset. */
static BOOL fvf_has_blend_indices(DWORD fvf)
{
    if (fvf & (D3DFVF_LASTBETA_UBYTE4 | D3DFVF_LASTBETA_D3DCOLOR))
        return TRUE;
    return (fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZB5;
}

static BOOL fvf_to_declaration(DWORD fvf, D9WGVertexElement *elements,
        UINT *element_count)
{
    UINT count = 0;
    UINT offset = 0;
    UINT tex_count;
    UINT i;

    if (fvf & D3DFVF_RESERVED0)
        return FALSE;
    if ((fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZ) {
        elements[count].stream = 0;
        elements[count].offset = (uint16_t)offset;
        elements[count].type = D3DDECLTYPE_FLOAT3;
        elements[count].method = D3DDECLMETHOD_DEFAULT;
        elements[count].usage = D3DDECLUSAGE_POSITION;
        elements[count].usage_index = 0;
        ++count;
        offset += 12;
    } else if ((fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZRHW) {
        elements[count].stream = 0;
        elements[count].offset = (uint16_t)offset;
        elements[count].type = D3DDECLTYPE_FLOAT4;
        elements[count].method = D3DDECLMETHOD_DEFAULT;
        elements[count].usage = D3DDECLUSAGE_POSITIONT;
        elements[count].usage_index = 0;
        ++count;
        offset += 16;
    } else if ((fvf & D3DFVF_POSITION_MASK) == D3DFVF_XYZW) {
        /* A four-component untransformed position, which only a vertex shader
         * consumes -- unlike XYZRHW it is not pre-transformed. */
        elements[count].stream = 0;
        elements[count].offset = (uint16_t)offset;
        elements[count].type = D3DDECLTYPE_FLOAT4;
        elements[count].method = D3DDECLMETHOD_DEFAULT;
        elements[count].usage = D3DDECLUSAGE_POSITION;
        elements[count].usage_index = 0;
        ++count;
        offset += 16;
    } else if (fvf_position_is_blended(fvf)) {
        /* D3DFVF_XYZBn: a float3 position followed by n DWORDs of skinning
         * data. This is the FVF spelling of the same BLENDWEIGHT/BLENDINDICES
         * a vertex declaration states directly, and it used to be refused
         * outright -- so SetFVF was the one entry point where the host's
         * D3DRS_VERTEXBLEND support could not be reached. */
        DWORD blend_dwords = 1u + (((fvf & D3DFVF_POSITION_MASK)
                - D3DFVF_XYZB1) >> 1);
        BOOL has_indices = fvf_has_blend_indices(fvf);
        DWORD weight_count = blend_dwords - (has_indices ? 1u : 0u);

        elements[count].stream = 0;
        elements[count].offset = (uint16_t)offset;
        elements[count].type = D3DDECLTYPE_FLOAT3;
        elements[count].method = D3DDECLMETHOD_DEFAULT;
        elements[count].usage = D3DDECLUSAGE_POSITION;
        elements[count].usage_index = 0;
        ++count;
        offset += 12;

        if (weight_count) {
            /* FLOAT1..FLOAT4 are D3DDECLTYPE 0..3, so the count maps straight
             * onto the enum. weight_count cannot exceed 4: XYZB5 always
             * spends its last DWORD on indices (see fvf_has_blend_indices). */
            elements[count].stream = 0;
            elements[count].offset = (uint16_t)offset;
            elements[count].type = (uint8_t)(D3DDECLTYPE_FLOAT1
                    + (weight_count - 1u));
            elements[count].method = D3DDECLMETHOD_DEFAULT;
            elements[count].usage = D3DDECLUSAGE_BLENDWEIGHT;
            elements[count].usage_index = 0;
            ++count;
            offset += weight_count * 4u;
        }
        if (has_indices) {
            elements[count].stream = 0;
            elements[count].offset = (uint16_t)offset;
            elements[count].type = (fvf & D3DFVF_LASTBETA_D3DCOLOR)
                    ? D3DDECLTYPE_D3DCOLOR : D3DDECLTYPE_UBYTE4;
            elements[count].method = D3DDECLMETHOD_DEFAULT;
            elements[count].usage = D3DDECLUSAGE_BLENDINDICES;
            elements[count].usage_index = 0;
            ++count;
            offset += 4;
        }
    }
    /*
     * No position bits at all is the remaining case, and it is deliberately
     * not an error. A vs_3_0 shader may take its position from somewhere other
     * than the vertex stream -- 3DMark06's SM3.0 particle test drives every
     * particle from a vertex texture fetch and its stream carries nothing but
     * two texture coordinates, so the FVF describing that stream has no
     * position element to declare. Refusing it left the *previous* pass's
     * declaration bound, and the draw was then laid out with that pass's
     * offsets against this pass's 12-byte stride.
     *
     * Only an FVF that describes no attribute whatsoever is refused, below.
     */
    if (fvf & D3DFVF_NORMAL) {
        elements[count].stream = 0;
        elements[count].offset = (uint16_t)offset;
        elements[count].type = D3DDECLTYPE_FLOAT3;
        elements[count].method = D3DDECLMETHOD_DEFAULT;
        elements[count].usage = D3DDECLUSAGE_NORMAL;
        elements[count].usage_index = 0;
        ++count;
        offset += 12;
    }
    if (fvf & D3DFVF_PSIZE) {
        elements[count].stream = 0;
        elements[count].offset = (uint16_t)offset;
        elements[count].type = D3DDECLTYPE_FLOAT1;
        elements[count].method = D3DDECLMETHOD_DEFAULT;
        elements[count].usage = D3DDECLUSAGE_PSIZE;
        elements[count].usage_index = 0;
        ++count;
        offset += 4;
    }
    if (fvf & D3DFVF_DIFFUSE) {
        elements[count].stream = 0;
        elements[count].offset = (uint16_t)offset;
        elements[count].type = D3DDECLTYPE_D3DCOLOR;
        elements[count].method = D3DDECLMETHOD_DEFAULT;
        elements[count].usage = D3DDECLUSAGE_COLOR;
        elements[count].usage_index = 0;
        ++count;
        offset += 4;
    }
    if (fvf & D3DFVF_SPECULAR) {
        elements[count].stream = 0;
        elements[count].offset = (uint16_t)offset;
        elements[count].type = D3DDECLTYPE_D3DCOLOR;
        elements[count].method = D3DDECLMETHOD_DEFAULT;
        elements[count].usage = D3DDECLUSAGE_COLOR;
        elements[count].usage_index = 1;
        ++count;
        offset += 4;
    }
    tex_count = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    if (tex_count > 8)
        return FALSE;
    for (i = 0; i < tex_count; ++i) {
        /*
         * The two bits per set in the FVF's high half are D3DFVF_TEXTUREFORMAT2
         * /3/4/1 in that order -- the encoding is not the component count, so
         * these tables carry it rather than arithmetic. A set is two floats
         * only by default; 1D sets index a lookup table and 3D/4D sets carry
         * projective or cube coordinates, and all three are ordinary D3D9.
         */
        static const uint8_t texcoord_type[4] = {
            D3DDECLTYPE_FLOAT2, D3DDECLTYPE_FLOAT3,
            D3DDECLTYPE_FLOAT4, D3DDECLTYPE_FLOAT1
        };
        static const UINT texcoord_bytes[4] = { 8, 12, 16, 4 };
        DWORD size_bits = (fvf >> (16 + i * 2)) & 0x3u;
        elements[count].stream = 0;
        elements[count].offset = (uint16_t)offset;
        elements[count].type = texcoord_type[size_bits];
        elements[count].method = D3DDECLMETHOD_DEFAULT;
        elements[count].usage = D3DDECLUSAGE_TEXCOORD;
        elements[count].usage_index = (uint8_t)i;
        ++count;
        offset += texcoord_bytes[size_bits];
    }
    if (!count)
        return FALSE;
    *element_count = count;
    return TRUE;
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
    return emit_command(D9WG_OP_CREATE_TEXTURE_2D, &command, sizeof(command));
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
        g_session_id_high = 0xD9A80001u;
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

static void fill_caps(D3DCAPS9 *caps)
{
    UINT shader_model = shader_model_version();

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
    caps->PresentationIntervals = D3DPRESENT_INTERVAL_IMMEDIATE
            | D3DPRESENT_INTERVAL_ONE;
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
            | D3DVTXPCAPS_TEXGEN_SPHEREMAP;
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
     * D3DVTXPCAPS_TWEENING is deliberately NOT set alongside it. Tweening
     * interpolates two vertex streams by D3DRS_TWEENFACTOR rather than blending
     * matrices, and nothing implements that -- it is a separate feature that
     * happens to share the D3DVERTEXBLENDFLAGS enum. */
    caps->MaxVertexBlendMatrices = 4;
    caps->MaxVertexBlendMatrixIndex = 255;
    /* The tessellation ceiling d9_draw_npatch() clamps to. It bounds the
     * scratch each draw allocates; 16-bit output indices bound it further for
     * a large mesh, and a draw that would overflow them is left flat. */
    caps->MaxNpatchTessellationLevel = (float)D9_PATCH_MAX_SEGMENTS;
    apply_permissive_caps(caps);
    /* Render targets and MRT: the host binds up to four colour attachments and
     * a translated pixel shader's oC0..oC3 reach them (M4 work brought forward
     * because a 2005-era D3D9 game renders most of its frame into textures). */
    caps->NumSimultaneousRTs = D9_MAX_RENDER_TARGETS;
}

static BOOL device_has_reset_blockers(D9Device *device)
{
    D9VertexBuffer *vb;
    D9IndexBuffer *ib;
    D9Texture *texture;
    D9CubeTexture *cube;
    D9VolumeTexture *volume;
    UINT level;

    if (device->in_scene)
        return TRUE;
    for (vb = device->vertex_buffers; vb; vb = vb->next_device_resource) {
        if (vb->pool == D3DPOOL_DEFAULT || vb->locked)
            return TRUE;
    }
    for (ib = device->index_buffers; ib; ib = ib->next_device_resource) {
        if (ib->pool == D3DPOOL_DEFAULT || ib->locked)
            return TRUE;
    }
    for (texture = device->texture_resources; texture;
            texture = texture->next_device_resource) {
        if (texture->pool == D3DPOOL_DEFAULT)
            return TRUE;
        for (level = 0; level < texture->level_count; ++level) {
            if (texture->levels[level].locked)
                return TRUE;
        }
    }
    for (cube = device->cube_textures; cube; cube = cube->next_device_resource) {
        if (cube->pool == D3DPOOL_DEFAULT)
            return TRUE;
        for (level = 0; level < cube->level_count * 6u; ++level) {
            if (cube->levels[level].locked)
                return TRUE;
        }
    }
    for (volume = device->volume_textures; volume;
            volume = volume->next_device_resource) {
        if (volume->pool == D3DPOOL_DEFAULT)
            return TRUE;
        for (level = 0; level < volume->level_count; ++level) {
            if (volume->levels[level].locked)
                return TRUE;
        }
    }
    /* An open state-block recording spans the Reset, and the block it would
     * produce describes resources the Reset is about to invalidate. */
    if (device->recording)
        return TRUE;
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
        if (buffer) IDirect3DVertexBuffer9_Release(&buffer->iface);
    }
    if (device->index_buffer) {
        D9IndexBuffer *buffer = device->index_buffer;
        device->index_buffer = NULL;
        IDirect3DIndexBuffer9_Release(&buffer->iface);
    }
    for (index = 0; index < D9_MAX_TEXTURE_BINDINGS; ++index) {
        D9Texture *texture = device->textures[index];
        D9CubeTexture *cube = device->cube_bindings[index];
        D9VolumeTexture *volume = device->volume_bindings[index];
        device->textures[index] = NULL;
        device->cube_bindings[index] = NULL;
        device->volume_bindings[index] = NULL;
        if (texture) IDirect3DTexture9_Release(&texture->iface);
        if (cube) IDirect3DCubeTexture9_Release(&cube->iface);
        if (volume) IDirect3DVolumeTexture9_Release(&volume->iface);
    }
    for (index = 0; index < D9_MAX_RENDER_TARGETS; ++index) {
        D9Surface *surface = device->render_target_surfaces[index];
        device->render_target_surfaces[index] = NULL;
        if (surface) IDirect3DSurface9_Release(&surface->iface);
    }
    if (device->depth_stencil_surface) {
        D9Surface *surface = device->depth_stencil_surface;
        device->depth_stencil_surface = NULL;
        IDirect3DSurface9_Release(&surface->iface);
    }
    device->depth_stencil_unbound = FALSE;
    if (device->vertex_declaration) {
        D9VertexDeclaration *decl = device->vertex_declaration;
        device->vertex_declaration = NULL;
        IDirect3DVertexDeclaration9_Release(&decl->iface);
    }
    if (device->vertex_shader) {
        D9Shader *shader = device->vertex_shader;
        device->vertex_shader = NULL;
        IDirect3DVertexShader9_Release(&shader->iface.vertex);
    }
    if (device->pixel_shader) {
        D9Shader *shader = device->pixel_shader;
        device->pixel_shader = NULL;
        IDirect3DPixelShader9_Release(&shader->iface.pixel);
    }
    device->fvf = 0;
}

static void device_release_owned_references(D9Device *device)
{
    TRACE("ENTER Device.ReleaseOwnedReferences device=%08lX refs=%ld child_refs=%ld",
            device->handle,
            InterlockedCompareExchange(&device->refcount, 0, 0),
            InterlockedCompareExchange(&device->child_parent_refs, 0, 0));
    device_clear_bindings(device);
    TRACE("LEAVE Device.ReleaseOwnedReferences device=%08lX refs=%ld child_refs=%ld",
            device->handle,
            InterlockedCompareExchange(&device->refcount, 0, 0),
            InterlockedCompareExchange(&device->child_parent_refs, 0, 0));
}

static BOOL recreate_device_resources(D9Device *device)
{
    D9VertexBuffer *vb;
    D9IndexBuffer *ib;
    D9Texture *texture;
    D9CubeTexture *cube;
    D9VolumeTexture *volume;
    D9VertexDeclaration *decl;
    D9Shader *shader;
    UINT level;

    for (vb = device->vertex_buffers; vb; vb = vb->next_device_resource) {
        vb->handle = allocate_handle();
        if (!emit_vertex_buffer_create(device, vb)
                || !emit_buffer_update(vb->handle, 0, vb->shadow,
                        vb->length, 0))
            return FALSE;
    }
    for (ib = device->index_buffers; ib; ib = ib->next_device_resource) {
        ib->handle = allocate_handle();
        if (!emit_index_buffer_create(device, ib)
                || !emit_buffer_update(ib->handle, 0, ib->shadow,
                        ib->length, 0))
            return FALSE;
    }
    for (texture = device->texture_resources; texture;
            texture = texture->next_device_resource) {
        texture->handle = allocate_handle();
        if (!emit_texture_create(device, texture)) return FALSE;
        for (level = 0; level < texture->level_count; ++level) {
            RECT full;
            SetRect(&full, 0, 0, (int)texture->levels[level].width,
                    (int)texture->levels[level].height);
            if (!emit_texture_update(texture, level, &full)) return FALSE;
        }
    }
    for (cube = device->cube_textures; cube; cube = cube->next_device_resource) {
        UINT face;
        cube->handle = allocate_handle();
        if (!emit_cube_texture_create(device, cube)) return FALSE;
        for (face = 0; face < 6u; ++face) {
            for (level = 0; level < cube->level_count; ++level) {
                D9TextureLevel *level_data =
                        &cube->levels[face * cube->level_count + level];
                RECT full;
                SetRect(&full, 0, 0, (int)level_data->width,
                        (int)level_data->height);
                if (!emit_cube_texture_update(cube, face, level, &full))
                    return FALSE;
            }
        }
    }
    for (volume = device->volume_textures; volume;
            volume = volume->next_device_resource) {
        volume->handle = allocate_handle();
        if (!emit_volume_texture_create(device, volume)) return FALSE;
        for (level = 0; level < volume->level_count; ++level) {
            D9TextureLevel *level_data = &volume->levels[level];
            D3DBOX full;
            full.Left = full.Top = full.Front = 0;
            full.Right = level_data->width;
            full.Bottom = level_data->height;
            full.Back = level_data->depth;
            if (!emit_volume_texture_update(volume, level, &full))
                return FALSE;
        }
    }
    for (decl = device->vertex_declarations; decl;
            decl = decl->next_device_resource) {
        D9WGVertexElement wire[D3DMAXDECLLENGTH];
        UINT i;
        decl->handle = allocate_handle();
        for (i = 0; i < decl->element_count; ++i) {
            wire[i].stream = decl->elements[i].Stream;
            wire[i].offset = decl->elements[i].Offset;
            wire[i].type = decl->elements[i].Type;
            wire[i].method = decl->elements[i].Method;
            wire[i].usage = decl->elements[i].Usage;
            wire[i].usage_index = decl->elements[i].UsageIndex;
        }
        if (!emit_vertex_declaration_create(device, decl->handle, wire,
                decl->element_count))
            return FALSE;
    }
    /*
     * Shaders and their constant registers are not pool-bound resources and
     * so survive Reset from the app's point of view -- but the host's
     * resource table is rebuilt from scratch, so both have to be replayed
     * here or the first draw after a Reset binds a handle the host has never
     * heard of. The constants go out unconditionally (bypassing the
     * dirty-range suppression in set_constant_*, which compares against a
     * shadow the host no longer shares) because after a Reset the host's
     * copy is empty, not stale.
     */
    for (shader = device->shaders; shader; shader = shader->next_device_resource) {
        shader->handle = allocate_shader_handle();
        if (!emit_shader_create(device, shader))
            return FALSE;
    }
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

static void device_init_states(D9Device *device)
{
    UINT slot;
    UINT stage;

    /*
     * These arrays are not merely Get*State backing stores: every Set*State
     * path suppresses a command when the cached value already equals the new
     * one.  Zero-initialising them therefore is only correct for states whose
     * D3D9 default is actually zero.  In particular, a game commonly starts a
     * UI/fog/shadow pass with ZENABLE=FALSE and ZWRITEENABLE=FALSE.  With an
     * all-zero cache those first calls were discarded, while the host (quite
     * correctly) retained D3D9's default enabled depth state.  The result was
     * scene depth hiding UI glyphs/icons and depth-writing overlay passes
     * corrupting fog and projected shadows.
     *
     * Keep this table aligned with the fallback values in
     * d3d9_executor.js.  Initial state is not replayed: both ends begin from
     * these same D3D9 defaults, and subsequent Set calls carry every change.
     */
    ZeroMemory(device->render_states, sizeof(device->render_states));
    ZeroMemory(device->texture_stage_states,
            sizeof(device->texture_stage_states));
    ZeroMemory(device->sampler_states, sizeof(device->sampler_states));

    device->render_states[D3DRS_ZENABLE] = D3DZB_TRUE;
    device->render_states[D3DRS_FILLMODE] = D3DFILL_SOLID;
    device->render_states[D3DRS_SHADEMODE] = D3DSHADE_GOURAUD;
    device->render_states[D3DRS_ZWRITEENABLE] = TRUE;
    device->render_states[D3DRS_LASTPIXEL] = TRUE;
    device->render_states[D3DRS_SRCBLEND] = D3DBLEND_ONE;
    device->render_states[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
    device->render_states[D3DRS_CULLMODE] = D3DCULL_CCW;
    device->render_states[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
    device->render_states[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
    device->render_states[D3DRS_FOGEND] = 0x3F800000u;
    device->render_states[D3DRS_FOGDENSITY] = 0x3F800000u;
    device->render_states[D3DRS_STENCILFAIL] = D3DSTENCILOP_KEEP;
    device->render_states[D3DRS_STENCILZFAIL] = D3DSTENCILOP_KEEP;
    device->render_states[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP;
    device->render_states[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
    device->render_states[D3DRS_STENCILMASK] = 0xFFFFFFFFu;
    device->render_states[D3DRS_STENCILWRITEMASK] = 0xFFFFFFFFu;
    device->render_states[D3DRS_TEXTUREFACTOR] = 0xFFFFFFFFu;
    device->render_states[D3DRS_CLIPPING] = TRUE;
    device->render_states[D3DRS_LIGHTING] = TRUE;
    device->render_states[D3DRS_COLORVERTEX] = TRUE;
    device->render_states[D3DRS_LOCALVIEWER] = TRUE;
    device->render_states[D3DRS_DIFFUSEMATERIALSOURCE] = D3DMCS_COLOR1;
    device->render_states[D3DRS_SPECULARMATERIALSOURCE] = D3DMCS_COLOR2;
    device->render_states[D3DRS_AMBIENTMATERIALSOURCE] = D3DMCS_MATERIAL;
    device->render_states[D3DRS_EMISSIVEMATERIALSOURCE] = D3DMCS_MATERIAL;
    device->render_states[D3DRS_POINTSIZE] = 0x3F800000u;
    device->render_states[D3DRS_POINTSIZE_MIN] = 0x3F800000u;
    device->render_states[D3DRS_POINTSCALE_A] = 0x3F800000u;
    device->render_states[D3DRS_POINTSIZE_MAX] = 0x42800000u; /* 64.0f */
    device->render_states[D3DRS_MULTISAMPLEANTIALIAS] = TRUE;
    device->render_states[D3DRS_MULTISAMPLEMASK] = 0xFFFFFFFFu;
    device->render_states[D3DRS_PATCHEDGESTYLE] = D3DPATCHEDGE_DISCRETE;
    device->render_states[D3DRS_COLORWRITEENABLE] = 0xFu;
    device->render_states[D3DRS_BLENDOP] = D3DBLENDOP_ADD;
    device->render_states[D3DRS_CCW_STENCILFAIL] = D3DSTENCILOP_KEEP;
    device->render_states[D3DRS_CCW_STENCILZFAIL] = D3DSTENCILOP_KEEP;
    device->render_states[D3DRS_CCW_STENCILPASS] = D3DSTENCILOP_KEEP;
    device->render_states[D3DRS_CCW_STENCILFUNC] = D3DCMP_ALWAYS;
    device->render_states[D3DRS_COLORWRITEENABLE1] = 0xFu;
    device->render_states[D3DRS_COLORWRITEENABLE2] = 0xFu;
    device->render_states[D3DRS_COLORWRITEENABLE3] = 0xFu;
    device->render_states[D3DRS_BLENDFACTOR] = 0xFFFFFFFFu;
    device->render_states[D3DRS_SRCBLENDALPHA] = D3DBLEND_ONE;
    device->render_states[D3DRS_DESTBLENDALPHA] = D3DBLEND_ZERO;
    device->render_states[D3DRS_BLENDOPALPHA] = D3DBLENDOP_ADD;

    for (stage = 0; stage < D9_MAX_TEXTURE_STAGES; ++stage) {
        device->texture_stage_states[stage][D3DTSS_COLOROP] = stage == 0
                ? D3DTOP_MODULATE : D3DTOP_DISABLE;
        device->texture_stage_states[stage][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
        device->texture_stage_states[stage][D3DTSS_COLORARG2] = D3DTA_CURRENT;
        device->texture_stage_states[stage][D3DTSS_ALPHAOP] = stage == 0
                ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
        device->texture_stage_states[stage][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
        device->texture_stage_states[stage][D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
        device->texture_stage_states[stage][D3DTSS_TEXCOORDINDEX] = stage;
        device->texture_stage_states[stage][D3DTSS_COLORARG0] = D3DTA_CURRENT;
        device->texture_stage_states[stage][D3DTSS_ALPHAARG0] = D3DTA_CURRENT;
        device->texture_stage_states[stage][D3DTSS_RESULTARG] = D3DTA_CURRENT;
    }
    for (slot = 0; slot < D9_MAX_SAMPLERS; ++slot) {
        device->sampler_states[slot][D3DSAMP_ADDRESSU] = D3DTADDRESS_WRAP;
        device->sampler_states[slot][D3DSAMP_ADDRESSV] = D3DTADDRESS_WRAP;
        device->sampler_states[slot][D3DSAMP_ADDRESSW] = D3DTADDRESS_WRAP;
        device->sampler_states[slot][D3DSAMP_MAGFILTER] = D3DTEXF_POINT;
        device->sampler_states[slot][D3DSAMP_MINFILTER] = D3DTEXF_POINT;
        device->sampler_states[slot][D3DSAMP_MIPFILTER] = D3DTEXF_NONE;
        device->sampler_states[slot][D3DSAMP_MAXANISOTROPY] = 1u;
    }
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

static UINT WINAPI d3d_get_adapter_mode_count(IDirect3D9 *iface, UINT adapter,
        D3DFORMAT format)
{
    UINT count;
    (void)iface;

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
    (void)iface;

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

static HRESULT WINAPI d3d_get_adapter_display_mode(IDirect3D9 *iface,
        UINT adapter, D3DDISPLAYMODE *mode)
{
    (void)iface;

    if (adapter || !mode)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    /*
     * The guest's actual desktop mode, not a fixed 1024x768. This is what the
     * question asks -- "what is the display doing right now" -- and answering
     * it with a constant meant reporting a resolution the guest was not in:
     * a title that sizes a windowed device to the desktop, or compares the
     * mode against its saved settings, is then working from a number that was
     * never true. The reference implementation reports the real mode.
     */
    {
        DEVMODEA current;
        ZeroMemory(&current, sizeof(current));
        current.dmSize = sizeof(current);
        if (EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &current)
                && current.dmPelsWidth && current.dmPelsHeight) {
            fill_display_mode(mode, current.dmPelsWidth, current.dmPelsHeight,
                    current.dmBitsPerPel == 16 ? D3DFMT_R5G6B5
                            : D3DFMT_X8R8G8B8);
            if (current.dmDisplayFrequency > 1)
                mode->RefreshRate = current.dmDisplayFrequency;
        } else {
            fill_display_mode(mode, 1024, 768, D3DFMT_X8R8G8B8);
        }
    }
    TRACE("OK GetAdapterDisplayMode -> %lux%lu format=%08lX refresh=%lu",
            mode->Width, mode->Height, (DWORD)mode->Format, mode->RefreshRate);
    return D3D_OK;
}

static HRESULT WINAPI d3d_check_device_type(IDirect3D9 *iface, UINT adapter,
        D3DDEVTYPE type, D3DFORMAT display_format,
        D3DFORMAT backbuffer_format, WINBOOL windowed)
{
    BOOL ok;
    (void)iface; (void)windowed;
    ok = !adapter && type == D3DDEVTYPE_HAL
            && (display_format == D3DFMT_X8R8G8B8
                || display_format == D3DFMT_R5G6B5)
            && supported_backbuffer_format(backbuffer_format);

    TRACE("OK CheckDeviceType adapter=%lu type=%lu display=%08lX backbuffer=%08lX "
            "windowed=%lu -> %08lX", adapter, (DWORD)type,
            (DWORD)display_format, (DWORD)backbuffer_format, (DWORD)windowed,
            (DWORD)(ok ? D3D_OK : D3DERR_NOTAVAILABLE));
    return ok ? D3D_OK : D3DERR_NOTAVAILABLE;
}

static HRESULT WINAPI d3d_check_device_format(IDirect3D9 *iface,
        UINT adapter, D3DDEVTYPE type, D3DFORMAT adapter_format,
        DWORD usage, D3DRESOURCETYPE resource_type, D3DFORMAT format)
{
    BOOL ok = FALSE;
    HRESULT result;
    (void)iface; (void)adapter_format;
    if (!adapter && type == D3DDEVTYPE_HAL) {
        /* D3DUSAGE_QUERY_* bits exist only in this call and never reach a
         * Create*, so they are answered here and masked off before the shared
         * creation predicate sees the usage. D3DUSAGE_AUTOGENMIPMAP is masked
         * for the same reason from the other direction: the create paths
         * accept it and report D3DOK_NOAUTOGEN, so it cannot make an otherwise
         * valid format unavailable. */
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
                | ((resource_type == D3DRTYPE_VOLUMETEXTURE)
                    ? D3DUSAGE_AUTOGENMIPMAP : 0u);
        const DWORD creation_usage = usage
                & ~(query_bits | D3DUSAGE_AUTOGENMIPMAP
                    | D3DUSAGE_SOFTWAREPROCESSING);
        if (!(usage & unimplemented)
                && texture_create_supported(resource_type, creation_usage,
                        format, D3DPOOL_DEFAULT))
            ok = TRUE;
    }
    result = ok ? D3D_OK : D3DERR_NOTAVAILABLE;
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
    (void)iface; (void)windowed;
    ok = !adapter && type == D3DDEVTYPE_HAL
            && supported_multisample(format, multisample);
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
    (void)iface; (void)adapter_format; (void)render_format;
    ok = !adapter && type == D3DDEVTYPE_HAL
            && supported_depth_stencil_format(depth_format);

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

static HRESULT WINAPI d3d_create_device(IDirect3D9 *iface, UINT adapter,
        D3DDEVTYPE type, HWND focus_window, DWORD behavior,
        D3DPRESENT_PARAMETERS *parameters,
        IDirect3DDevice9 **device_out)
{
    D9Direct3D *d3d = d3d_from_iface(iface);
    D9Device *device;
    D9WGCreateDevice command;
    HWND window;
    RECT client;
    POINT origin;
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
    backbuffer_format = parameters->BackBufferFormat == D3DFMT_UNKNOWN
            ? D3DFMT_X8R8G8B8 : parameters->BackBufferFormat;
    if (!supported_multisample(backbuffer_format,
            parameters->MultiSampleType)
            || parameters->MultiSampleQuality != 0) {
        TRACE("FAIL CreateDevice unsupported multisample=%lu quality=%lu "
                "format=%08lX -> %08lX",
                (DWORD)parameters->MultiSampleType,
                parameters->MultiSampleQuality, (DWORD)backbuffer_format,
                (DWORD)D3DERR_NOTAVAILABLE);
        return TRACE_REFUSE(D3DERR_NOTAVAILABLE);
    }
    if (parameters->EnableAutoDepthStencil
            && !supported_depth_stencil_format(
                    parameters->AutoDepthStencilFormat)) {
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
            && !supported_backbuffer_format(parameters->BackBufferFormat)) {
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
    {
        const DWORD vertex_processing = behavior
                & (D3DCREATE_SOFTWARE_VERTEXPROCESSING
                        | D3DCREATE_HARDWARE_VERTEXPROCESSING
                        | D3DCREATE_MIXED_VERTEXPROCESSING);
        if (vertex_processing != D3DCREATE_SOFTWARE_VERTEXPROCESSING
                && vertex_processing != D3DCREATE_HARDWARE_VERTEXPROCESSING
                && vertex_processing != D3DCREATE_MIXED_VERTEXPROCESSING) {
            TRACE("FAIL CreateDevice behavior=%08lX names %s vertex processing "
                    "mode -> %08lX", behavior,
                    vertex_processing ? "more than one" : "no",
                    (DWORD)D3DERR_INVALIDCALL);
            HOSTLOG_FAILED("CreateDevice refused: BehaviorFlags %08lX must "
                    "name exactly one vertex-processing mode", behavior);
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        }
        if ((behavior & D3DCREATE_PUREDEVICE)
                && vertex_processing != D3DCREATE_HARDWARE_VERTEXPROCESSING) {
            TRACE("FAIL CreateDevice behavior=%08lX pairs PUREDEVICE with "
                    "non-hardware vertex processing -> %08lX", behavior,
                    (DWORD)D3DERR_INVALIDCALL);
            HOSTLOG_FAILED("CreateDevice refused: D3DCREATE_PUREDEVICE "
                    "requires D3DCREATE_HARDWARE_VERTEXPROCESSING");
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        }
    }

    device = (D9Device *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*device));
    if (!device) {
        TRACE("FAIL CreateDevice allocation -> %08lX", (DWORD)E_OUTOFMEMORY);
        return E_OUTOFMEMORY;
    }
    device->iface.lpVtbl = &g_device_vtbl;
    device->implicit_swap_chain.iface.lpVtbl = &g_swap_chain_vtbl;
    device->implicit_swap_chain.device = device;
    device->refcount = 1;
    device->parent = d3d;
    IDirect3D9_AddRef(iface);
    device->handle = allocate_handle();
    device->present = *parameters;
    device->creation.AdapterOrdinal = adapter;
    device->creation.DeviceType = type;
    device->creation.hFocusWindow = focus_window;
    device->creation.BehaviorFlags = behavior;
    fill_display_mode(&device->display_mode,
            parameters->BackBufferWidth ? parameters->BackBufferWidth : 640,
            parameters->BackBufferHeight ? parameters->BackBufferHeight : 480,
            parameters->BackBufferFormat == D3DFMT_UNKNOWN
                    ? D3DFMT_X8R8G8B8 : parameters->BackBufferFormat);
    device->viewport.X = 0;
    device->viewport.Y = 0;
    device->viewport.Width = device->display_mode.Width;
    device->viewport.Height = device->display_mode.Height;
    device->viewport.MinZ = 0.0f;
    device->viewport.MaxZ = 1.0f;
    device_init_states(device);

    window = parameters->hDeviceWindow ? parameters->hDeviceWindow
            : focus_window;
    SetRect(&client, 0, 0, (int)device->display_mode.Width,
            (int)device->display_mode.Height);
    origin.x = 0;
    origin.y = 0;
    if (window) {
        GetClientRect(window, &client);
        ClientToScreen(window, &origin);
    }
    ZeroMemory(&command, sizeof(command));
    command.device_handle = device->handle;
    command.hwnd = (uint32_t)(uintptr_t)window;
    command.x = origin.x;
    command.y = origin.y;
    command.width = parameters->BackBufferWidth
            ? parameters->BackBufferWidth : (uint32_t)(client.right - client.left);
    command.height = parameters->BackBufferHeight
            ? parameters->BackBufferHeight : (uint32_t)(client.bottom - client.top);
    if (!command.width) command.width = 640;
    if (!command.height) command.height = 480;
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
    claim_fullscreen_foreground(device, window);
    emit_window_state(device, window);
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
    if ((LONG)refs == InterlockedCompareExchange(
            &device->child_parent_refs, 0, 0)) {
        TRACE("Device.Release cycle-break object=%08lX refs=%lu child_refs=%ld",
                (DWORD)(uintptr_t)iface, refs,
                InterlockedCompareExchange(&device->child_parent_refs, 0, 0));
        device->releasing_owned_refs = TRUE;
        device_release_owned_references(device);
        device->releasing_owned_refs = FALSE;
        refs = (ULONG)InterlockedCompareExchange(&device->refcount, 0, 0);
    }
    if (!refs) {
        D9WGDestroyResource destroy;
        TRACE("Device.Release destroy object=%08lX handle=%08lX pending=%lu",
                (DWORD)(uintptr_t)iface, device->handle, g_command_count);
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
        IDirect3D9_Release(&device->parent->iface);
        /* The implicit surfaces outlive every public reference to them by
         * design, so the device is what finally frees them. Reaching here means
         * their refcounts are zero: each public reference held one of the
         * device references that had to reach zero to get this far. */
        if (device->implicit_back_buffer)
            HeapFree(GetProcessHeap(), 0, device->implicit_back_buffer);
        if (device->implicit_depth_stencil)
            HeapFree(GetProcessHeap(), 0, device->implicit_depth_stencil);
        HeapFree(GetProcessHeap(), 0, device);
        return 0;
    }
    TRACE("LEAVE Device.Release object=%08lX refs=%lu child_refs=%ld",
            (DWORD)(uintptr_t)iface, refs,
            InterlockedCompareExchange(&device->child_parent_refs, 0, 0));
    TRACE_MARK_EXIT("Device.Release", (HRESULT)refs, NULL);
    return refs;
}

static HRESULT WINAPI device_test_cooperative_level(IDirect3DDevice9 *iface)
{
    /* Deliberately untraced: most titles call this every frame. */
    (void)iface;
    return D3D_OK;
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

static HRESULT WINAPI device_get_display_mode(IDirect3DDevice9 *iface,
        UINT swapchain, D3DDISPLAYMODE *mode)
{
    D9Device *device = device_from_iface(iface);
    if (swapchain || !mode)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *mode = device->display_mode;
    TRACE("OK Device.GetDisplayMode -> %lux%lu format=%08lX refresh=%lu",
            mode->Width, mode->Height, (DWORD)mode->Format, mode->RefreshRate);
    return D3D_OK;
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
    D9Surface *surface;
    D9WGSetCursorProperties command;
    const BYTE *pixels;
    UINT row_pitch;
    uint8_t *payload;
    uint8_t *blob;
    UINT byte_count;
    BOOL result;

    if (!bitmap || bitmap->lpVtbl != &g_surface_vtbl) {
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    surface = surface_from_iface(bitmap);
    if (surface->shadow) {
        pixels = surface->shadow;
        row_pitch = surface->row_pitch;
    } else if (surface->texture
            && surface->level < surface->texture->level_count
            && surface->texture->levels[surface->level].shadow) {
        /* A cursor built as a texture level rather than a plain surface. */
        pixels = surface->texture->levels[surface->level].shadow;
        row_pitch = surface->texture->levels[surface->level].row_pitch;
    } else {

        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    if (surface->format != D3DFMT_A8R8G8B8 && surface->format != D3DFMT_X8R8G8B8) {

        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    if (!multiply_u32(surface->width * 4u, surface->height, &byte_count))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);

    ZeroMemory(&command, sizeof(command));
    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_SET_CURSOR_PROPERTIES,
            sizeof(command), byte_count, NULL, &payload, &blob);
    if (result) {
        UINT row;
        command.device_handle = device->handle;
        command.hotspot_x = hotspot_x;
        command.hotspot_y = hotspot_y;
        command.width = surface->width;
        command.height = surface->height;
        command.data_bytes = byte_count;
        command.data_offset = (uint32_t)(blob - batch_base());
        CopyMemory(payload, &command, sizeof(command));
        /* Repack to a tight width*4 stride; the source pitch is whatever the
         * surface was allocated with. */
        for (row = 0; row < surface->height; ++row)
            CopyMemory(blob + row * surface->width * 4u,
                    pixels + row * row_pitch, surface->width * 4u);
    }
    LeaveCriticalSection(&g_transport_lock);
    if (!result)
        return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
    device->cursor_ready = TRUE;
    device->app_cursor = TRUE;

    return D3D_OK;
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
static void emit_window_state(D9Device *device, HWND window)
{
    D9WGWindowState command;
    HWND foreground = GetForegroundWindow();
    RECT window_rect;
    RECT client;
    uint32_t flags = 0;

    SetRect(&window_rect, 0, 0, 0, 0);
    SetRect(&client, 0, 0, 0, 0);
    if (window && IsWindow(window)) {
        flags |= D9WG_WINDOW_IS_WINDOW;
        if (IsWindowVisible(window)) flags |= D9WG_WINDOW_VISIBLE;
        if (IsIconic(window)) flags |= D9WG_WINDOW_ICONIC;
        if (window == foreground) flags |= D9WG_WINDOW_FOREGROUND;
        GetWindowRect(window, &window_rect);
        GetClientRect(window, &client);
    }
    if (!device->present.Windowed)
        flags |= D9WG_WINDOW_FULLSCREEN;

    command.device_handle = device->handle;
    command.hwnd = (uint32_t)(uintptr_t)window;
    command.foreground_hwnd = (uint32_t)(uintptr_t)foreground;
    command.flags = flags;
    command.window_x = window_rect.left;
    command.window_y = window_rect.top;
    command.window_width = (uint32_t)(window_rect.right - window_rect.left);
    command.window_height = (uint32_t)(window_rect.bottom - window_rect.top);
    command.client_width = (uint32_t)(client.right - client.left);
    command.client_height = (uint32_t)(client.bottom - client.top);

    if (device->window_state_sent
            && device->last_window_flags == flags
            && device->last_foreground == command.foreground_hwnd)
        return;
    device->window_state_sent = TRUE;
    device->last_window_flags = flags;
    device->last_foreground = command.foreground_hwnd;
    emit_command(D9WG_OP_WINDOW_STATE, &command, sizeof(command));
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
    HWND foreground = GetForegroundWindow();
    DWORD foreground_thread = foreground
            ? GetWindowThreadProcessId(foreground, NULL) : 0;
    DWORD our_thread = GetCurrentThreadId();
    BOOL attached = FALSE;

    if (foreground_thread && foreground_thread != our_thread)
        attached = AttachThreadInput(our_thread, foreground_thread, TRUE);
    BringWindowToTop(window);
    SetForegroundWindow(window);
    SetActiveWindow(window);
    SetFocus(window);
    if (attached)
        AttachThreadInput(our_thread, foreground_thread, FALSE);
}

static void claim_fullscreen_foreground(D9Device *device, HWND window)
{
    DEVMODEA mode;
    LONG result;

    if (device->present.Windowed)
        return;

    /*
     * Switch the guest's display mode to the one the device asked for.
     *
     * This is the other half of what "exclusive fullscreen" means, and it is
     * not cosmetic. Warcraft III creates an 800x600 fullscreen device on a
     * 1024x768 desktop; with no mode change Windows keeps reporting a
     * 1024x768 screen, so the guest's pointer coordinates, the window
     * geometry and the game's own 800x600 layout all disagree -- the picture
     * looks right while a click lands somewhere else entirely. Doing the mode
     * change makes the guest's idea of the screen match what the game
     * renders, which is what every other part of this path already assumes.
     */
    ZeroMemory(&mode, sizeof(mode));
    mode.dmSize = sizeof(mode);
    mode.dmPelsWidth = device->display_mode.Width;
    mode.dmPelsHeight = device->display_mode.Height;
    mode.dmBitsPerPel = 32;
    mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
    result = ChangeDisplaySettingsA(&mode, CDS_FULLSCREEN);
    if (result != DISP_CHANGE_SUCCESSFUL) {
        /* 16bpp is the other mode a v86 guest is likely to actually have. */
        mode.dmBitsPerPel = 16;
        result = ChangeDisplaySettingsA(&mode, CDS_FULLSCREEN);
    }
    device->display_mode_changed = (result == DISP_CHANGE_SUCCESSFUL);

    if (!window || !IsWindow(window))
        return;
    if (IsIconic(window))
        ShowWindow(window, SW_RESTORE);
    /*
     * Size the device window to the mode as well: testing showed War3's window
     * reporting an empty rect to both GetClientRect and GetWindowRect, so
     * nothing downstream can discover where the game's output belongs.
     *
     * Strip the frame first. SetWindowPos sizes the *window*, so on a window
     * that still has a caption and border the client area comes out smaller
     * than the mode -- an 800x600 window is a 792x573 client -- and the WM_SIZE
     * this call sends tells the application that smaller number, from inside
     * CreateDevice, while its engine is still starting. GTA San Andreas took
     * exactly that WM_SIZE (792x573 at client origin 4,23) and then destroyed
     * its window and exited. An exclusive-fullscreen window is borderless and
     * covers the mode, which is also what makes client == back buffer true.
     */
    if (device->styled_window != window) {
        LONG style = GetWindowLongA(window, GWL_STYLE);
        LONG exstyle = GetWindowLongA(window, GWL_EXSTYLE);
        LONG bare_style = (style & ~(WS_CAPTION | WS_THICKFRAME | WS_BORDER
                | WS_DLGFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU))
                | WS_POPUP;
        LONG bare_exstyle = exstyle & ~(WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE
                | WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE);

        device->styled_window = window;
        device->saved_window_style = style;
        device->saved_window_exstyle = exstyle;
        if (bare_style != style)
            SetWindowLongA(window, GWL_STYLE, bare_style);
        if (bare_exstyle != exstyle)
            SetWindowLongA(window, GWL_EXSTYLE, bare_exstyle);
    }
    /* SWP_FRAMECHANGED so the stripped style is recalculated now rather than
     * at some later, unrelated window event. */
    SetWindowPos(window, HWND_TOP, 0, 0, (int)device->display_mode.Width,
            (int)device->display_mode.Height,
            SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    raise_window_to_foreground(window);
    device->last_foreground_claim = GetTickCount();

}

/* Undoes the borderless override, so a window that outlives the device (a
 * Reset back to windowed, or a title that keeps running after releasing it)
 * gets its own frame back rather than staying stripped. */
static void restore_window_style(D9Device *device)
{
    HWND window = device->styled_window;

    if (!window)
        return;
    device->styled_window = NULL;
    if (!IsWindow(window))
        return;
    SetWindowLongA(window, GWL_STYLE, device->saved_window_style);
    SetWindowLongA(window, GWL_EXSTYLE, device->saved_window_exstyle);
    SetWindowPos(window, NULL, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE
            | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
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
static void maintain_fullscreen_foreground(D9Device *device, HWND window)
{
    DWORD now;

    if (device->present.Windowed || !window || !IsWindow(window))
        return;
    if (GetForegroundWindow() == window)
        return;
    now = GetTickCount();
    /* GetTickCount wraps every ~49 days; the subtraction is unsigned so the
     * wrap is harmless, but a zero initial value must not look like "claimed
     * just now" either -- hence the explicit first-time case. */
    if (device->foreground_claims &&
            (DWORD)(now - device->last_foreground_claim) < 500u)
        return;
    device->last_foreground_claim = now;
    ++device->foreground_claims;
    if (IsIconic(window))
        ShowWindow(window, SW_RESTORE);
    raise_window_to_foreground(window);

}

/* Undo the mode change when the device goes away, so a crashed or closed game
 * does not leave the guest desktop stuck at the game's resolution. */
static void restore_display_mode(D9Device *device)
{
    restore_window_style(device);
    if (!device->display_mode_changed)
        return;
    device->display_mode_changed = FALSE;
    ChangeDisplaySettingsA(NULL, 0);
}

static void emit_cursor_position(D9Device *device, int x, int y, DWORD flags)
{
    D9WGSetCursorPosition command;
    command.device_handle = device->handle;
    command.x = x;
    command.y = y;
    command.flags = flags;
    emit_command(D9WG_OP_SET_CURSOR_POSITION, &command, sizeof(command));
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
    POINT point;

    /* D3D9 takes screen coordinates here; the host draws into back-buffer
     * space, so convert through the device window the same way
     * emit_present_and_flush() does. */
    point.x = x;
    point.y = y;
    if (device->present.hDeviceWindow)
        ScreenToClient(device->present.hDeviceWindow, &point);
    emit_cursor_position(device, point.x, point.y, flags);
}

static WINBOOL WINAPI device_show_cursor(IDirect3DDevice9 *iface,
        WINBOOL show)
{
    D9Device *device = device_from_iface(iface);
    D9WGShowCursor command;
    WINBOOL previous = device->cursor_visible;

    /* Worth tracing on its own: ShowCursor(FALSE) from a game that never
     * called SetCursorProperties means it intends to draw the pointer itself,
     * as ordinary geometry. A missing cursor would then be a lost draw, not a
     * missing cursor feature -- a completely different bug. */

    device->cursor_visible = show ? TRUE : FALSE;
    command.device_handle = device->handle;
    command.show = device->cursor_visible ? 1u : 0u;
    emit_command(D9WG_OP_SHOW_CURSOR, &command, sizeof(command));
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

static HRESULT WINAPI device_reset(IDirect3DDevice9 *iface,
        D3DPRESENT_PARAMETERS *parameters)
{
    D9Device *device = device_from_iface(iface);
    D9WGResetDevice reset;
    RECT client;
    POINT origin;
    HWND window;

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
    if (!parameters || parameters->BackBufferCount > 1
            || device_has_reset_blockers(device)) {
        TRACE("FAIL Reset parameters=%lu multisample=%lu count=%lu "
                "blockers=%lu -> %08lX", (DWORD)(parameters != NULL),
                parameters ? (DWORD)parameters->MultiSampleType : 0,
                parameters ? parameters->BackBufferCount : 0,
                (DWORD)device_has_reset_blockers(device),
                (DWORD)D3DERR_INVALIDCALL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    if (!supported_multisample(
            parameters->BackBufferFormat == D3DFMT_UNKNOWN
                    ? device->display_mode.Format
                    : parameters->BackBufferFormat,
            parameters->MultiSampleType)
            || parameters->MultiSampleQuality != 0) {
        TRACE("FAIL Reset unsupported multisample=%lu quality=%lu -> %08lX",
                (DWORD)parameters->MultiSampleType,
                parameters->MultiSampleQuality,
                (DWORD)D3DERR_NOTAVAILABLE);
        return TRACE_REFUSE(D3DERR_NOTAVAILABLE);
    }
    if (parameters->EnableAutoDepthStencil
            && !supported_depth_stencil_format(
                    parameters->AutoDepthStencilFormat)) {
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
            && !supported_backbuffer_format(parameters->BackBufferFormat)) {
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
    reset.width = parameters->BackBufferWidth
            ? parameters->BackBufferWidth : (uint32_t)(client.right - client.left);
    reset.height = parameters->BackBufferHeight
            ? parameters->BackBufferHeight : (uint32_t)(client.bottom - client.top);
    if (!reset.width) reset.width = device->display_mode.Width;
    if (!reset.height) reset.height = device->display_mode.Height;
    reset.backbuffer_format = parameters->BackBufferFormat == D3DFMT_UNKNOWN
            ? device->display_mode.Format : parameters->BackBufferFormat;
    reset.windowed = parameters->Windowed;
    reset.behavior_flags = device->creation.BehaviorFlags;
    reset.enable_auto_depth_stencil = parameters->EnableAutoDepthStencil;
    reset.auto_depth_stencil_format = parameters->AutoDepthStencilFormat;
    reset.multisample_type = parameters->MultiSampleType;
    reset.multisample_quality = parameters->MultiSampleQuality;
    if (reset.width > 8192 || reset.height > 8192)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!emit_command(D9WG_OP_RESET, &reset, sizeof(reset)))
        return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
    device_clear_bindings(device);
    device->handle = reset.new_device_handle;
    ++device->reset_epoch;
    device->present = *parameters;
    fill_display_mode(&device->display_mode, reset.width, reset.height,
            reset.backbuffer_format);
    /* A Reset resizes the implicit surfaces rather than replacing them -- the
     * app keeps the same pointers across it -- so the cached objects have to
     * pick up the new geometry here or GetDesc keeps reporting the old size. */
    if (device->implicit_back_buffer) {
        device->implicit_back_buffer->width = device->display_mode.Width;
        device->implicit_back_buffer->height = device->display_mode.Height;
        device->implicit_back_buffer->format = device->display_mode.Format;
        device->implicit_back_buffer->multisample =
                parameters->MultiSampleType;
        device->implicit_back_buffer->multisample_quality =
                parameters->MultiSampleQuality;
    }
    if (device->implicit_depth_stencil) {
        device->implicit_depth_stencil->width = reset.width;
        device->implicit_depth_stencil->height = reset.height;
        device->implicit_depth_stencil->format =
                parameters->AutoDepthStencilFormat;
        device->implicit_depth_stencil->multisample =
                parameters->MultiSampleType;
        device->implicit_depth_stencil->multisample_quality =
                parameters->MultiSampleQuality;
    }
    device_init_states(device);
    device->viewport.X = device->viewport.Y = 0;
    device->viewport.Width = reset.width;
    device->viewport.Height = reset.height;
    device->viewport.MinZ = 0.0f;
    device->viewport.MaxZ = 1.0f;
    if (!recreate_device_resources(device)) {
        TRACE("FAIL Reset recreate_device_resources -> %08lX",
                (DWORD)D3DERR_DRIVERINTERNALERROR);
        return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
    }
    TRACE("OK Reset old_handle=%08lX new_handle=%08lX %lux%lu epoch=%lu",
            reset.old_device_handle, device->handle, reset.width, reset.height,
            device->reset_epoch);

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
    SetRect(client, 0, 0, fallback_width, fallback_height);
    origin->x = 0;
    origin->y = 0;
    if (window) {
        GetClientRect(window, client);
        ClientToScreen(window, origin);
    }
    if (client->right - client->left > 0 && client->bottom - client->top > 0)
        return;
    {
        RECT window_rect;
        if (window && GetWindowRect(window, &window_rect)
                && window_rect.right > window_rect.left
                && window_rect.bottom > window_rect.top) {
            origin->x = window_rect.left;
            origin->y = window_rect.top;
            SetRect(client, 0, 0, window_rect.right - window_rect.left,
                    window_rect.bottom - window_rect.top);
        }
    }
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
    RECT client;
    POINT origin;
    uint8_t *payload;
    BOOL result;

    if (!window)
        window = device->creation.hFocusWindow;
    probe_window_rect(window, (int)device->display_mode.Width,
            (int)device->display_mode.Height, &client, &origin);
    present.device_handle = device->handle;
    present.hwnd = (uint32_t)(uintptr_t)window;
    present.x = origin.x;
    present.y = origin.y;
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
    present.width = (uint32_t)(client.right - client.left);
    present.height = (uint32_t)(client.bottom - client.top);

    /* D3D9's hardware cursor follows the OS pointer on its own, so the app is
     * under no obligation to call SetCursorPosition -- most never do. Sample
     * it here instead, once per frame, and only while a cursor is actually
     * armed and visible. */
    maintain_fullscreen_foreground(device, window);
    emit_window_state(device, window);
    if (device->app_cursor) {
        if (device->cursor_visible) {
            POINT pointer;
            if (GetCursorPos(&pointer)) {
                if (window)
                    ScreenToClient(window, &pointer);
                emit_cursor_position(device, pointer.x, pointer.y, 0);
            }
        }
    } else {
        /* The application is leaving the pointer to Windows; capture it out
         * of GDI instead (see emit_system_cursor_bitmap). */
        update_system_cursor(device, window);
    }

    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_PRESENT, sizeof(present), 0,
            NULL, &payload, NULL);
    if (result) {
        CopyMemory(payload, &present, sizeof(present));
        result = submit_batch_locked(TRUE);
    }
    LeaveCriticalSection(&g_transport_lock);
    return result;
}

/*
 * D3DPRESENT_INTERVAL_ONE..FOUR ask Present to block until the Nth vertical
 * retrace.  That wait is the only back-pressure a D3D9 title has on its own
 * frame rate: the render loop is `while (running) { render(); Present(); }` and
 * nothing else in it sleeps.  Returning immediately -- what this proxy did
 * until now, whatever the app requested -- lets the guest spin as fast as the
 * emulator can retire the command stream.  San Andreas' loading screen reached
 * roughly 1000 Present/s that way, which starves every other thread in the
 * process of timeslices and spends the emulator's budget on frames nobody sees.
 *
 * There is no retrace to wait on here, so the wait is reconstructed from the
 * clock: sleep out whatever is left of the interval since the previous Present.
 * When the guest is already the bottleneck the elapsed time exceeds the
 * interval and this costs nothing, so the throttle only engages on the frames
 * that were running too fast.
 */
static BOOL present_throttle_disabled(void)
{
    static LONG cached = -1;

    if (cached < 0)
        cached = env_flag_enabled("D9WG_PRESENT_NO_THROTTLE") ? 1 : 0;
    return cached != 0;
}

static void throttle_to_presentation_interval(D9Device *device)
{
    DWORD frames, refresh, interval_ms, elapsed;

    if (present_throttle_disabled())
        return;

    switch (device->present.PresentationInterval) {
    /* DEFAULT is documented as equivalent to ONE, not as "do not wait". */
    case D3DPRESENT_INTERVAL_DEFAULT:
    case D3DPRESENT_INTERVAL_ONE:   frames = 1; break;
    case D3DPRESENT_INTERVAL_TWO:   frames = 2; break;
    case D3DPRESENT_INTERVAL_THREE: frames = 3; break;
    case D3DPRESENT_INTERVAL_FOUR:  frames = 4; break;
    default:                        return;  /* IMMEDIATE */
    }

    refresh = device->display_mode.RefreshRate;
    if (refresh < 20u || refresh > 240u)
        refresh = 60u;
    interval_ms = (1000u * frames) / refresh;
    if (!interval_ms)
        return;

    /* Unsigned wraparound makes this correct across GetTickCount()'s 49.7-day
     * rollover.  The very first Present subtracts a zero timestamp and so
     * measures the time since boot, which never sleeps -- exactly what a first
     * frame with no predecessor should do. */
    elapsed = GetTickCount() - device->last_present_tick;
    if (elapsed < interval_ms)
        Sleep(interval_ms - elapsed);
    device->last_present_tick = GetTickCount();
}

static HRESULT WINAPI device_present(IDirect3DDevice9 *iface,
        const RECT *src_rect, const RECT *dst_rect, HWND override_window,
        const RGNDATA *dirty_region)
{
    BOOL ok;
    (void)src_rect; (void)dst_rect; (void)dirty_region;

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
    throttle_to_presentation_interval(device_from_iface(iface));
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
    D9WGSetGammaRamp command;
    UINT index;

    TRACE("CALL SetGammaRamp device=%08lX chain=%lu flags=%08lX ramp=%08lX",
            device->handle, swapchain, flags, (DWORD)(uintptr_t)ramp);
    if (!ramp || swapchain)
        return;
    device->gamma_ramp = *ramp;
    device->gamma_ramp_set = TRUE;
    command.device_handle = device->handle;
    command.swap_chain = swapchain;
    command.flags = flags;
    command.reserved = 0;
    for (index = 0; index < 256; ++index) {
        command.ramp[index] = (uint16_t)ramp->red[index];
        command.ramp[256 + index] = (uint16_t)ramp->green[index];
        command.ramp[512 + index] = (uint16_t)ramp->blue[index];
    }
    emit_command(D9WG_OP_SET_GAMMA_RAMP, &command, sizeof(command));
}

static void WINAPI device_get_gamma_ramp(IDirect3DDevice9 *iface,
        UINT swapchain, D3DGAMMARAMP *ramp)
{
    D9Device *device = device_from_iface(iface);
    UINT index;
    if (!ramp)
        return;
    if (swapchain) {
        ZeroMemory(ramp, sizeof(*ramp));
        return;
    }
    if (device->gamma_ramp_set) {
        *ramp = device->gamma_ramp;
        return;
    }
    /* A device that has never been given a ramp is at identity, not at zero.
     * Answering zero tells a title the display is black, and one that reads the
     * ramp before writing it -- to restore it on exit -- would then "restore"
     * the screen to black. */
    for (index = 0; index < 256; ++index) {
        WORD level = (WORD)((index << 8) | index);
        ramp->red[index] = level;
        ramp->green[index] = level;
        ramp->blue[index] = level;
    }
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
    D9WGSetTransform command;
    if (!matrix || (UINT)state >= D9_MAX_TRANSFORMS)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    state_block_record_transform(device, (UINT)state);
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
    if (!matrix || (UINT)state >= D9_MAX_TRANSFORMS)
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
    state_block_record_viewport(device);
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
    state_block_record_render_state(device, (UINT)state);
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
    state_block_record_texture(device, slot);
    if (device->textures[slot] == texture && device->cube_bindings[slot] == cube
            && device->volume_bindings[slot] == volume)
        return D3D_OK;
    if (texture) IDirect3DTexture9_AddRef(&texture->iface);
    if (cube) IDirect3DCubeTexture9_AddRef(&cube->iface);
    if (volume) IDirect3DVolumeTexture9_AddRef(&volume->iface);
    if (device->textures[slot])
        IDirect3DTexture9_Release(&device->textures[slot]->iface);
    if (device->cube_bindings[slot])
        IDirect3DCubeTexture9_Release(&device->cube_bindings[slot]->iface);
    if (device->volume_bindings[slot])
        IDirect3DVolumeTexture9_Release(&device->volume_bindings[slot]->iface);
    device->textures[slot] = texture;
    device->cube_bindings[slot] = cube;
    device->volume_bindings[slot] = volume;
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
    state_block_record_texture_stage_state(device, stage, (UINT)state);
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

static HRESULT WINAPI device_create_texture(IDirect3DDevice9 *iface,
        UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format,
        D3DPOOL pool, IDirect3DTexture9 **texture_out, HANDLE *shared_handle)
{
    D9Device *device = device_from_iface(iface);
    D9Texture *texture;
    UINT full_levels;
    UINT level;
    UINT level_width;
    UINT level_height;
    HRESULT failure = E_OUTOFMEMORY;
    HRESULT success = D3D_OK;
    BOOL is_depth = (usage & D3DUSAGE_DEPTHSTENCIL) != 0;
    (void)shared_handle;

    TRACE("CALL CreateTexture %lux%lu levels=%lu usage=%08lX "
            "format=%08lX pool=%lu", width, height, levels, usage,
            (DWORD)format, (DWORD)pool);

    if (!texture_out) {
        TRACE("FAIL CreateTexture missing output -> %08lX",
                (DWORD)D3DERR_INVALIDCALL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    *texture_out = NULL;
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
        if (!is_depth && !texture_level_layout(format, level_width, level_height,
                &level_data->row_pitch, &level_data->row_count,
                &level_data->byte_count))
            goto allocation_failed;
        /* Targets are not directly lockable.  A render target can acquire a
         * lazy CPU mirror later when Clear/ColorFill gives us exact contents;
         * texture_lock_level() therefore rejects the usage flags explicitly. */
        if (usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)) {
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

static HRESULT WINAPI device_set_stream_source(IDirect3DDevice9 *iface,
        UINT stream, IDirect3DVertexBuffer9 *buffer_iface, UINT offset,
        UINT stride)
{
    D9Device *device = device_from_iface(iface);
    D9VertexBuffer *buffer = buffer_iface ? vb_from_iface(buffer_iface) : NULL;
    D9WGSetStreamSource command;
    if (stream >= D9_MAX_STREAMS) {

        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    if (buffer && (buffer_iface->lpVtbl != &g_vb_vtbl
            || buffer->device != device))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (buffer && offset >= buffer->length)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    state_block_record_stream(device, stream);
    if (device->streams[stream].buffer == buffer
            && device->streams[stream].stride == stride
            && device->streams[stream].offset == offset)
        return D3D_OK;
    if (buffer)
        IDirect3DVertexBuffer9_AddRef(buffer_iface);
    if (device->streams[stream].buffer)
        IDirect3DVertexBuffer9_Release(&device->streams[stream].buffer->iface);
    device->streams[stream].buffer = buffer;
    device->streams[stream].stride = stride;
    device->streams[stream].offset = offset;
    command.device_handle = device->handle;
    command.stream = stream;
    command.buffer_handle = buffer ? buffer->handle : 0;
    command.stride = stride;
    command.offset_in_bytes = offset;
    command.reserved = 0;
    return emit_command(D9WG_OP_SET_STREAM_SOURCE, &command, sizeof(command))
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
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
    state_block_record_indices(device);
    if (device->index_buffer == buffer)
        return D3D_OK;
    if (buffer)
        IDirect3DIndexBuffer9_AddRef(buffer_iface);
    if (device->index_buffer)
        IDirect3DIndexBuffer9_Release(&device->index_buffer->iface);
    device->index_buffer = buffer;
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

static HRESULT WINAPI device_draw_primitive(IDirect3DDevice9 *iface,
        D3DPRIMITIVETYPE primitive_type, UINT start_vertex,
        UINT primitive_count)
{
    D9Device *device = device_from_iface(iface);
    D9WGDrawPrimitive command;
    UINT vertex_count = 0;
    UINT available_vertices;

    if (!device->streams[0].buffer || !device->streams[0].stride
            || device->streams[0].buffer->locked
            || !device_has_vertex_format(device) || !primitive_count
            || !primitive_element_count(primitive_type, primitive_count,
                    &vertex_count)) {
        TRACE_FRAME_REJECT();
        return D3DERR_INVALIDCALL;
    }
    if (device->npatch_segments > 1.0f) {
        HRESULT tessellated = d9_draw_npatch(iface, device, primitive_type,
                primitive_count, NULL, 0, start_vertex);
        if (SUCCEEDED(tessellated))
            return tessellated;
        /* Not tessellatable -- no normals, a shader bound, too many output
         * vertices for 16-bit indices. Fall through and draw it flat, which is
         * what the mesh looked like before N-patches were switched on. */
    }
    /* Vertices addressable by this draw start after the stream's
     * OffsetInBytes, not at the start of the buffer. */
    available_vertices = (device->streams[0].buffer->length
            - device->streams[0].offset) / device->streams[0].stride;
    if (start_vertex > available_vertices
            || vertex_count > available_vertices - start_vertex) {
        TRACE_FRAME_REJECT();
        return D3DERR_INVALIDCALL;
    }
    command.device_handle = device->handle;
    command.primitive_type = primitive_type;
    command.start_vertex = start_vertex;
    command.primitive_count = primitive_count;

    if (!emit_command(D9WG_OP_DRAW_PRIMITIVE, &command, sizeof(command)))
        return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
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
    UINT available_vertices;

    if (!device->streams[0].buffer || !device->streams[0].stride
            || device->streams[0].buffer->locked || !device->index_buffer
            || device->index_buffer->locked || !device_has_vertex_format(device)
            || !primitive_count || !vertex_count
            || !primitive_element_count(primitive_type, primitive_count,
                    &index_count)) {
        TRACE_FRAME_REJECT();
        return D3DERR_INVALIDCALL;
    }
    index_size = device->index_buffer->format == D3DFMT_INDEX16 ? 2u : 4u;
    available_indices = device->index_buffer->length / index_size;
    if (start_index > available_indices
            || index_count > available_indices - start_index) {
        TRACE_FRAME_REJECT();
        return D3DERR_INVALIDCALL;
    }
    if (device->npatch_segments > 1.0f && base_vertex_index >= 0) {
        HRESULT tessellated = d9_draw_npatch(iface, device, primitive_type,
                primitive_count,
                device->index_buffer->shadow + start_index * index_size,
                index_size, (UINT)base_vertex_index);
        if (SUCCEEDED(tessellated))
            return tessellated;
        /* See the note in device_draw_primitive: falling through draws the
         * mesh flat rather than dropping it. */
    }
    /* Vertices addressable by this draw start after the stream's
     * OffsetInBytes, not at the start of the buffer. */
    available_vertices = (device->streams[0].buffer->length
            - device->streams[0].offset) / device->streams[0].stride;
    if (min_vertex_index > available_vertices
            || vertex_count > available_vertices - min_vertex_index) {
        TRACE_FRAME_REJECT();
        return D3DERR_INVALIDCALL;
    }

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
    TRACE_FRAME_DRAW(primitive_count);
    invalidate_render_target_mirrors(device);
    return D3D_OK;
}

static void clear_stream_zero_after_up(D9Device *device)
{
    D9VertexBuffer *old = device->streams[0].buffer;
    device->streams[0].buffer = NULL;
    device->streams[0].stride = 0;
    if (old) IDirect3DVertexBuffer9_Release(&old->iface);
}

static void clear_indices_after_indexed_up(D9Device *device)
{
    D9IndexBuffer *old = device->index_buffer;
    device->index_buffer = NULL;
    if (old) IDirect3DIndexBuffer9_Release(&old->iface);
}

static HRESULT WINAPI device_draw_primitive_up(IDirect3DDevice9 *iface,
        D3DPRIMITIVETYPE primitive_type, UINT primitive_count,
        const void *vertex_data, UINT stride)
{
    D9Device *device = device_from_iface(iface);
    D9WGDrawPrimitiveUP command;
    UINT vertex_count;
    UINT vertex_bytes;
    BOOL result;

    if (!vertex_data || !stride || !device_has_vertex_format(device)
            || !primitive_count
            || !primitive_element_count(primitive_type, primitive_count,
                    &vertex_count)
            || !multiply_u32(vertex_count, stride, &vertex_bytes)) {
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
    result = emit_draw_primitive_up(&command, vertex_data);
    if (result) {
        TRACE_FRAME_DRAW(primitive_count);
        invalidate_render_target_mirrors(device);
        clear_stream_zero_after_up(device);
    }
    return result ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_draw_indexed_primitive_up(
        IDirect3DDevice9 *iface, D3DPRIMITIVETYPE primitive_type,
        UINT min_vertex_index, UINT vertex_count, UINT primitive_count,
        const void *index_data, D3DFORMAT index_format,
        const void *vertex_data, UINT stride)
{
    D9Device *device = device_from_iface(iface);
    D9WGDrawIndexedPrimitiveUP command;
    UINT index_count;
    UINT index_size;
    UINT vertex_elements;
    BOOL result;

    if (!index_data || !vertex_data || !stride
            || !device_has_vertex_format(device) || !primitive_count
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
    command.device_handle = device->handle;
    command.primitive_type = primitive_type;
    command.min_vertex_index = min_vertex_index;
    command.vertex_count = vertex_count;
    command.primitive_count = primitive_count;
    command.index_format = index_format;
    command.stride = stride;
    command.index_count = index_count;
    result = emit_draw_indexed_primitive_up(&command, index_data, vertex_data);
    if (result) {
        TRACE_FRAME_DRAW(primitive_count);
        invalidate_render_target_mirrors(device);
        clear_stream_zero_after_up(device);
        clear_indices_after_indexed_up(device);
    }
    return result ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
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

static HRESULT WINAPI device_set_vertex_declaration(IDirect3DDevice9 *iface,
        IDirect3DVertexDeclaration9 *decl_iface)
{
    D9Device *device = device_from_iface(iface);
    D9VertexDeclaration *decl = decl_iface ? decl_from_iface(decl_iface) : NULL;
    D9WGSetVertexDeclaration command;

    if (decl && (decl_iface->lpVtbl != &g_decl_vtbl || decl->device != device))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    state_block_record_vertex_format(device);
    if (decl)
        IDirect3DVertexDeclaration9_AddRef(decl_iface);
    if (device->vertex_declaration)
        IDirect3DVertexDeclaration9_Release(
                &device->vertex_declaration->iface);
    device->vertex_declaration = decl;
    device->fvf = 0;
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
    state_block_record_vertex_format(device);
    if (device->vertex_declaration) {
        IDirect3DVertexDeclaration9_Release(
                &device->vertex_declaration->iface);
        device->vertex_declaration = NULL;
    }
    device->fvf = fvf;
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

static HRESULT create_shader(D9Device *device, const DWORD *bytecode,
        BOOL want_pixel, D9Shader **shader_out)
{
    D9Shader *shader;
    UINT token_count = 0;
    BOOL is_pixel = FALSE;
    uint32_t code_bytes;

    *shader_out = NULL;
    if (!bytecode) {
        TRACE("FAIL CreateShader want_pixel=%lu bytecode=NULL -> %08lX",
                (DWORD)want_pixel, (DWORD)D3DERR_INVALIDCALL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    if (!shader_token_count(bytecode, &token_count, &is_pixel)
            || is_pixel != want_pixel) {
        /* Rejected before anything is emitted, so without this line the call
         * leaves no record whatsoever: no CMD, no OK, no FAIL. An app whose
         * shader is turned down here sees only D3DERR_INVALIDCALL, and the
         * trace shows only the gap where the shader should have been. */
        TRACE("FAIL CreateShader version=%08lX want_pixel=%lu is_pixel=%lu "
                "tokens=%lu -> %08lX", bytecode[0], (DWORD)want_pixel,
                (DWORD)is_pixel, token_count, (DWORD)D3DERR_INVALIDCALL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }

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

static void shader_destroy(D9Shader *shader)
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
    device_child_release(shader->device);
    HeapFree(GetProcessHeap(), 0, shader->code);
    HeapFree(GetProcessHeap(), 0, shader);
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
    state_block_record_vertex_shader(device);
    if (shader)
        IDirect3DVertexShader9_AddRef(shader_iface);
    if (device->vertex_shader)
        IDirect3DVertexShader9_Release(&device->vertex_shader->iface.vertex);
    device->vertex_shader = shader;
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
    state_block_record_pixel_shader(device);
    if (shader)
        IDirect3DPixelShader9_AddRef(shader_iface);
    if (device->pixel_shader)
        IDirect3DPixelShader9_Release(&device->pixel_shader->iface.pixel);
    device->pixel_shader = shader;
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
    state_block_record_constant(device, is_pixel, TRUE, FALSE, start, count);
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
    state_block_record_constant(device, is_pixel, FALSE, FALSE, start, count);
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
    state_block_record_constant(device, is_pixel, FALSE, TRUE, start, count);
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
    if (!params->Windowed || !device->present.Windowed) {
        HOSTLOG_REFUSED("CreateAdditionalSwapChain refused: D3D9 allows "
                "additional chains only on a windowed device, and only for a "
                "windowed chain");
        TRACE_MARK_EXIT("Device.CreateAdditionalSwapChain",
                D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    if (params->MultiSampleType != D3DMULTISAMPLE_NONE) {
        /* The chain's back buffer would have to be a multisampled texture that
         * also resolves at present; nothing here does that resolve, and a
         * silently single-sampled chain is a difference the app cannot see. */
        HOSTLOG_REFUSED("CreateAdditionalSwapChain refused: multisample=%lu is "
                "not implemented for an additional chain",
                (DWORD)params->MultiSampleType);
        TRACE_MARK_EXIT("Device.CreateAdditionalSwapChain",
                D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }

    window = params->hDeviceWindow ? params->hDeviceWindow
            : device->creation.hFocusWindow;
    if (!window) {
        TRACE_MARK_EXIT("Device.CreateAdditionalSwapChain",
                D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    /* D3D9 fills a zero width/height from the window's client area. */
    probe_window_rect(window, (int)device->display_mode.Width,
            (int)device->display_mode.Height, &client, &origin);
    width = params->BackBufferWidth ? params->BackBufferWidth
            : (UINT)(client.right - client.left);
    height = params->BackBufferHeight ? params->BackBufferHeight
            : (UINT)(client.bottom - client.top);
    if (!width || !height) {
        TRACE_MARK_EXIT("Device.CreateAdditionalSwapChain",
                D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    format = params->BackBufferFormat == D3DFMT_UNKNOWN
            ? device->display_mode.Format : params->BackBufferFormat;

    hr = create_target_texture(device, width, height, format,
            D3DUSAGE_RENDERTARGET, D3DMULTISAMPLE_NONE, 0, &back_buffer);
    if (FAILED(hr)) {
        HOSTLOG_FAILED("CreateAdditionalSwapChain refused: its back buffer "
                "(%lux%lu format=%08lX) could not be created", width, height,
                (DWORD)format);
        TRACE_MARK_EXIT("Device.CreateAdditionalSwapChain", hr, NULL);
        return hr;
    }
    surface = surface_from_iface(back_buffer);

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
    chain->present = *params;
    chain->present.BackBufferWidth = width;
    chain->present.BackBufferHeight = height;
    chain->present.BackBufferFormat = format;
    chain->present.BackBufferCount = params->BackBufferCount
            ? params->BackBufferCount : 1;
    chain->present.hDeviceWindow = window;

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
    device_child_add_ref(device);
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
 * buffer; LockRect remains invalid because a default-pool render target is not
 * a lockable surface in D3D9. */
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
    if (swapchain || index || type != D3DBACKBUFFER_TYPE_MONO) {
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
    /* Takes the device reference standing behind this public reference. */
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
    DWORD phase;
    if (swapchain || !status)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    /* D3D9 exposes this as advisory scan-out timing. Reconstruct a stable
     * 60-Hz raster from the guest monotonic clock so polling loops progress
     * and observe a real vblank interval instead of a permanent constant. */
    phase = GetTickCount() % 17u;
    status->InVBlank = phase >= 16u;
    status->ScanLine = status->InVBlank || !device->display_mode.Height
            ? 0u : phase * device->display_mode.Height / 16u;
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
    D9WGUpdateTexture update;
    UINT block_width, block_height, block_bytes;
    UINT block_x, block_y, row_bytes, row_count, slice_bytes, data_bytes;
    UINT slice, row;
    uint8_t *payload, *blob;
    BOOL result;

    if (!texture_format_layout(texture->format, &block_width, &block_height,
            &block_bytes)
            || !multiply_u32(((box->Right - box->Left) + block_width - 1u)
                    / block_width, block_bytes, &row_bytes))
        return FALSE;
    block_x = box->Left / block_width;
    block_y = box->Top / block_height;
    row_count = ((box->Bottom - box->Top) + block_height - 1u)
            / block_height;
    if (!multiply_u32(row_bytes, row_count, &slice_bytes)
            || !multiply_u32(slice_bytes, box->Back - box->Front,
                    &data_bytes))
        return FALSE;
    ZeroMemory(&update, sizeof(update));
    update.resource_handle = texture->handle;
    update.level = level;
    update.x = box->Left;
    update.y = box->Top;
    update.z = box->Front;
    update.width = box->Right - box->Left;
    update.height = box->Bottom - box->Top;
    update.depth = box->Back - box->Front;
    update.row_pitch = row_bytes;
    update.slice_pitch = slice_bytes;
    update.data_bytes = data_bytes;
    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_UPDATE_TEXTURE, sizeof(update),
            data_bytes, NULL, &payload, &blob);
    if (result) {
        update.data_offset = (uint32_t)(blob - batch_base());
        CopyMemory(payload, &update, sizeof(update));
        for (slice = 0; slice < update.depth; ++slice) {
            for (row = 0; row < row_count; ++row) {
                CopyMemory(blob + slice * slice_bytes + row * row_bytes,
                        level_data->shadow
                            + (box->Front + slice) * level_data->slice_pitch
                            + (block_y + row) * level_data->row_pitch
                            + block_x * block_bytes,
                        row_bytes);
            }
        }
    }
    LeaveCriticalSection(&g_transport_lock);
    return result;
}

static HRESULT WINAPI device_create_volume_texture(IDirect3DDevice9 *iface,
        UINT width, UINT height, UINT depth, UINT levels, DWORD usage,
        D3DFORMAT format, D3DPOOL pool, IDirect3DVolumeTexture9 **out,
        HANDLE *shared)
{
    D9Device *device = device_from_iface(iface);
    D9VolumeTexture *texture;
    UINT full_levels, level, level_width, level_height, level_depth;
    HRESULT failure = E_OUTOFMEMORY;
    HRESULT success = D3D_OK;
    (void)shared;
    TRACE("CALL CreateVolumeTexture %lux%lux%lu levels=%lu usage=%08lX "
            "format=%08lX pool=%lu", width, height, depth, levels, usage,
            (DWORD)format, (DWORD)pool);
    if (!out)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *out = NULL;
    /* D3D9 does not support automatic mip generation for volume textures at
     * all, so the flag is a request no driver honours: report D3DOK_NOAUTOGEN
     * and build the texture, which is what the runtime itself does. */
    if (usage & D3DUSAGE_AUTOGENMIPMAP) {
        usage &= ~(DWORD)D3DUSAGE_AUTOGENMIPMAP;
        levels = 1;
        success = D3DOK_NOAUTOGEN;
    }
    if (!width || !height || !depth || width > 2048 || height > 2048
            || depth > 2048
            || !texture_create_supported(D3DRTYPE_VOLUMETEXTURE, usage, format,
                    pool)) {
        HOSTLOG_FAILED("CreateVolumeTexture refused: %lux%lux%lu levels=%lu "
                "usage=%08lX format=%08lX pool=%lu", width, height, depth,
                levels, usage, (DWORD)format, (DWORD)pool);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
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
        if (!texture_level_layout(format, level_width, level_height,
                &level_data->row_pitch, &level_data->row_count,
                &slice_bytes)
                || !multiply_u32(slice_bytes, level_depth,
                    &level_data->byte_count))
            goto allocation_failed;
        level_data->slice_pitch = slice_bytes;
        level_data->shadow = (BYTE *)HeapAlloc(GetProcessHeap(),
                HEAP_ZERO_MEMORY, level_data->byte_count);
        level_data->level_volume = (D9Volume *)HeapAlloc(GetProcessHeap(),
                HEAP_ZERO_MEMORY, sizeof(*level_data->level_volume));
        if (!level_data->shadow || !level_data->level_volume)
            goto allocation_failed;
        level_data->level_volume->iface.lpVtbl = &g_volume_vtbl;
        level_data->level_volume->texture = texture;
        level_data->level_volume->level = level;
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
{ return (ULONG)InterlockedIncrement(
        &volume_texture_from_iface(iface)->refcount); }

static ULONG WINAPI volume_texture_release(IDirect3DVolumeTexture9 *iface)
{
    D9VolumeTexture *texture = volume_texture_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&texture->refcount);
    if (!refs) {
        D9VolumeTexture **link = &texture->device->volume_textures;
        D9WGDestroyResource destroy;
        UINT level;
        while (*link && *link != texture)
            link = &(*link)->next_device_resource;
        if (*link) *link = texture->next_device_resource;
        destroy.resource_handle = texture->handle;
        destroy.resource_kind = D9WG_RESOURCE_TEXTURE_VOLUME;
        emit_command(D9WG_OP_DESTROY_RESOURCE, &destroy, sizeof(destroy));
        for (level = 0; level < texture->level_count; ++level) {
            HeapFree(GetProcessHeap(), 0, texture->levels[level].level_volume);
            HeapFree(GetProcessHeap(), 0, texture->levels[level].shadow);
        }
        HeapFree(GetProcessHeap(), 0, texture->levels);
        device_child_release(texture->device);
        HeapFree(GetProcessHeap(), 0, texture);
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

#define VOLUME_TEX_PRIVATE_STUB(name, result) \
static HRESULT WINAPI volume_texture_##name(IDirect3DVolumeTexture9 *iface, \
        REFGUID guid) \
{ (void)iface; (void)guid; return result; }

static HRESULT WINAPI volume_texture_set_private_data(
        IDirect3DVolumeTexture9 *iface, REFGUID guid, const void *data,
        DWORD size, DWORD flags)
{ (void)iface; (void)guid; (void)data; (void)size; (void)flags;
  return TRACE_REFUSE(D3DERR_INVALIDCALL); }
static HRESULT WINAPI volume_texture_get_private_data(
        IDirect3DVolumeTexture9 *iface, REFGUID guid, void *data, DWORD *size)
{ (void)iface; (void)guid; (void)data; (void)size;
  return TRACE_REFUSE(D3DERR_NOTFOUND); }
VOLUME_TEX_PRIVATE_STUB(free_private_data, D3DERR_NOTFOUND)

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
static DWORD WINAPI volume_texture_set_lod(IDirect3DVolumeTexture9 *iface,
        DWORD lod)
{ D9VolumeTexture *texture = volume_texture_from_iface(iface);
  DWORD previous = texture->lod;
  if (texture->pool != D3DPOOL_MANAGED) return previous;
  if (lod >= texture->level_count) lod = texture->level_count - 1;
  texture->lod = lod;
  emit_texture_lod(texture->device, texture->handle, lod);
  return previous; }
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
{ (void)iface; return D3DTEXF_LINEAR; }
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
            || !texture_format_layout(texture->format, &block_width,
                    &block_height, &block_bytes)
            || area.Left % block_width || area.Top % block_height)
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
    if (!out || level >= texture->level_count)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *out = &texture->levels[level].level_volume->iface;
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
static HRESULT WINAPI volume_set_private_data(IDirect3DVolume9 *iface,
        REFGUID guid, const void *data, DWORD size, DWORD flags)
{ (void)iface; (void)guid; (void)data; (void)size; (void)flags;
  return TRACE_REFUSE(D3DERR_INVALIDCALL); }
static HRESULT WINAPI volume_get_private_data(IDirect3DVolume9 *iface,
        REFGUID guid, void *data, DWORD *size)
{ (void)iface; (void)guid; (void)data; (void)size;
  return TRACE_REFUSE(D3DERR_NOTFOUND); }
static HRESULT WINAPI volume_free_private_data(IDirect3DVolume9 *iface,
        REFGUID guid)
{ (void)iface; (void)guid; return TRACE_REFUSE(D3DERR_NOTFOUND); }
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
static HRESULT WINAPI device_update_texture(IDirect3DDevice9 *iface,
        IDirect3DBaseTexture9 *src_iface, IDirect3DBaseTexture9 *dst_iface)
{
    D9Device *device = device_from_iface(iface);
    D9Texture *source = (D9Texture *)src_iface;
    D9Texture *destination = (D9Texture *)dst_iface;
    const void *source_vtbl;
    const void *destination_vtbl;
    UINT level;

    if (!source || !destination)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    source_vtbl = source->iface.lpVtbl;
    destination_vtbl = destination->iface.lpVtbl;
    if (source_vtbl != destination_vtbl)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (source_vtbl == &g_volume_texture_vtbl) {
        D9VolumeTexture *volume_source = (D9VolumeTexture *)src_iface;
        D9VolumeTexture *volume_destination = (D9VolumeTexture *)dst_iface;
        if (volume_source->device != device
                || volume_destination->device != device
                || volume_source->pool != D3DPOOL_SYSTEMMEM
                || volume_destination->pool != D3DPOOL_DEFAULT
                || volume_source->format != volume_destination->format
                || volume_source->width != volume_destination->width
                || volume_source->height != volume_destination->height
                || volume_source->depth != volume_destination->depth)
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        for (level = 0; level < volume_source->level_count
                && level < volume_destination->level_count; ++level) {
            D9TextureLevel *from = &volume_source->levels[level];
            D9TextureLevel *to = &volume_destination->levels[level];
            D3DBOX full;
            if (from->locked || to->locked
                    || from->byte_count != to->byte_count)
                return TRACE_REFUSE(D3DERR_INVALIDCALL);
            CopyMemory(to->shadow, from->shadow, to->byte_count);
            full.Left = full.Top = full.Front = 0;
            full.Right = to->width;
            full.Bottom = to->height;
            full.Back = to->depth;
            if (!emit_volume_texture_update(volume_destination, level, &full))
                return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
        }
        return D3D_OK;
    }
    if (source_vtbl == &g_cube_vtbl) {
        D9CubeTexture *cube_source = (D9CubeTexture *)src_iface;
        D9CubeTexture *cube_destination = (D9CubeTexture *)dst_iface;
        UINT face;
        if (cube_source->device != device
                || cube_destination->device != device
                || cube_source->pool != D3DPOOL_SYSTEMMEM
                || cube_destination->pool != D3DPOOL_DEFAULT
                || cube_source->format != cube_destination->format
                || cube_source->edge_length != cube_destination->edge_length)
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        for (face = 0; face < 6u; ++face) {
            for (level = 0; level < cube_source->level_count
                    && level < cube_destination->level_count; ++level) {
                D9TextureLevel *from = &cube_source->levels[
                        face * cube_source->level_count + level];
                D9TextureLevel *to = &cube_destination->levels[
                        face * cube_destination->level_count + level];
                RECT full;
                if (from->locked || to->locked
                        || from->byte_count != to->byte_count)
                    return TRACE_REFUSE(D3DERR_INVALIDCALL);
                CopyMemory(to->shadow, from->shadow, to->byte_count);
                SetRect(&full, 0, 0, (int)to->width, (int)to->height);
                if (!emit_cube_texture_update(cube_destination, face, level,
                        &full))
                    return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
            }
        }
        return D3D_OK;
    }
    if (source_vtbl != &g_texture_vtbl
            || source->device != device || destination->device != device
            || source->pool != D3DPOOL_SYSTEMMEM
            || destination->pool != D3DPOOL_DEFAULT
            || source->format != destination->format
            || source->width != destination->width
            || source->height != destination->height)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    /* D3D9 allows the source to carry fewer levels than the destination;
     * only the levels both actually have can be copied. */
    for (level = 0; level < source->level_count
            && level < destination->level_count; ++level) {
        D9TextureLevel *from = &source->levels[level];
        D9TextureLevel *to = &destination->levels[level];
        RECT full;
        if (from->locked || to->locked)
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        if (from->byte_count != to->byte_count)
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        CopyMemory(to->shadow, from->shadow, to->byte_count);
        to->shadow_valid = TRUE;
        SetRect(&full, 0, 0, (int)to->width, (int)to->height);
        if (!emit_texture_update(destination, level, &full))
            return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
    }
    return D3D_OK;
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
        D9WGReadbackSurface command;
        D9WGReadbackResponse *response;
        volatile uint32_t *heartbeat;
        uint32_t last_beat;
        uint8_t *base;
        UINT first_row = 0;
        UINT rows_per_chunk;
        UINT capacity;
        HRESULT result = D3D_OK;

        EnterCriticalSection(&g_readback_lock);
        EnterCriticalSection(&g_transport_lock);
        if (!open_transport_locked()) {
            LeaveCriticalSection(&g_transport_lock);
            LeaveCriticalSection(&g_readback_lock);
            return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
        }
        base = response_region();
        response = (D9WGReadbackResponse *)(base +
                D9WG_READBACK_REGION_OFFSET);
        heartbeat = (volatile uint32_t *)(base + D9WG_HEARTBEAT_OFFSET);
        capacity = D9WG_RESPONSE_REGION_BYTES - D9WG_READBACK_REGION_OFFSET
                - (UINT)sizeof(*response) - D9WG_HEARTBEAT_BYTES;
        rows_per_chunk = destination_pitch ? capacity / destination_pitch : 0;
        TRACE("READBACK BEGIN source=%08lX texture=%08lX dst_format=%08lX "
                "src_format=%08lX %lux%lu rows=%lu pitch=%lu rows_per_chunk=%lu "
                "capacity=%lu front=%lu",
                (DWORD)(uintptr_t)rt_iface,
                rt->texture ? rt->texture->handle : 0,
                (DWORD)dst->format, (DWORD)rt->format, rt->width, rt->height,
                source_rows, destination_pitch, rows_per_chunk, capacity,
                (DWORD)front_buffer);
        if (!rows_per_chunk) {
            LeaveCriticalSection(&g_transport_lock);
            LeaveCriticalSection(&g_readback_lock);
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        }
        LeaveCriticalSection(&g_transport_lock);

        while (first_row < source_rows) {
            uint32_t request_id = (uint32_t)next_response_request_id();
            UINT chunk_rows = source_rows - first_row;
            DWORD start;
            BOOL emitted;
            uint8_t *payload;
            if (chunk_rows > rows_per_chunk) chunk_rows = rows_per_chunk;

            ZeroMemory(response, sizeof(*response));
            response->request_id = request_id;
            response->status = D9WG_RESPONSE_PENDING;
            MemoryBarrier();
            ZeroMemory(&command, sizeof(command));
            command.device_handle = device->handle;
            command.texture_handle = rt->texture ? rt->texture->handle : 0;
            command.level = rt->level;
            command.format = dst->format;
            command.width = rt->width;
            command.height = rt->height;
            command.first_row = first_row;
            command.row_count = chunk_rows;
            command.destination_pitch = destination_pitch;
            command.destination_bytes = chunk_rows * destination_pitch;
            command.response_offset = D9WG_READBACK_REGION_OFFSET;
            command.request_id = request_id;

            EnterCriticalSection(&g_transport_lock);
            emitted = reserve_command_locked(D9WG_OP_READBACK_SURFACE,
                    sizeof(command), 0, NULL, &payload, NULL);
            if (emitted) {
                CopyMemory(payload, &command, sizeof(command));
                emitted = submit_batch_locked(FALSE);
            }
            LeaveCriticalSection(&g_transport_lock);
            if (!emitted) {
                TRACE("READBACK FAIL emit first_row=%lu rows=%lu bytes=%lu",
                        first_row, chunk_rows, command.destination_bytes);
                result = D3DERR_DRIVERINTERNALERROR;
                break;
            }

            /*
             * The deadline measures host *silence*, not elapsed time. Every
             * batch the host finishes bumps the heartbeat, so a host that is
             * simply thousands of batches behind keeps this loop waiting; only
             * a host that has stopped answering entirely trips the cap. The
             * host bounds its own side of the handshake (readbackTimeoutMs in
             * d3d9_executor.js) and reports a failure rather than going quiet,
             * so reaching this cap means the host went away.
             */
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
                /* status 0 with the elapsed time at the cap is a timeout; 2 is
                 * the host reporting a failure, and its own message names the
                 * JS error. A mismatched request id means a stale response. */
                TRACE("READBACK FAIL response first_row=%lu rows=%lu "
                        "want_id=%08lX got_id=%08lX status=%lu want_bytes=%lu "
                        "got_bytes=%lu silent_for=%lu beat=%08lX",
                        first_row, chunk_rows, request_id,
                        response->request_id, response->status,
                        command.destination_bytes, response->byte_count,
                        GetTickCount() - start, last_beat);
                HOSTLOG_FAILED("readback failed: status=%lu host silent for "
                        "%lums (heartbeat=%08lX) rows=%lu pitch=%lu (0=the "
                        "host stopped answering, 2=it reported a failure -- "
                        "see its own console message)",
                        response->status, GetTickCount() - start,
                        last_beat, chunk_rows, destination_pitch);
                result = D3DERR_DRIVERINTERNALERROR;
                break;
            }
            CopyMemory(destination + first_row * destination_pitch,
                    response + 1, command.destination_bytes);
            first_row += chunk_rows;
        }
        LeaveCriticalSection(&g_readback_lock);
        if (FAILED(result))
            return TRACE_REFUSE(result);
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
static HRESULT WINAPI device_create_offscreen_plain_surface(
        IDirect3DDevice9 *iface, UINT width, UINT height, D3DFORMAT format,
        D3DPOOL pool, IDirect3DSurface9 **out, HANDLE *shared)
{
    D9Device *device = device_from_iface(iface);
    D9Surface *surface;
    UINT row_pitch;
    UINT byte_count;

    (void)shared;
    if (!out)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *out = NULL;
    if (!width || !height || pool == D3DPOOL_MANAGED
            || pool > D3DPOOL_SCRATCH || !supported_texture_format(format))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    /*
     * A D3DPOOL_DEFAULT offscreen surface lives on the GPU, and D3D9 lets it be
     * a StretchRect operand -- both operands have to be DEFAULT, and this is
     * how apps get the second one. Backing it with the same texture a render
     * target uses is what makes that true here; a CPU-only surface has no
     * handle to blit with, which is precisely how 3DMark06 ended up looking at
     * an "IDirect3DDevice9::StretchRect failed" box.
     *
     * SYSTEMMEM and SCRATCH keep the CPU-only form below. D3D9 refuses
     * StretchRect on those too, so refusing them costs nothing and they stay
     * cheap: no GPU allocation for a surface whose whole purpose is to be
     * locked.
     */
    if (pool == D3DPOOL_DEFAULT)
        return create_target_texture(device, width, height, format, 0u,
                D3DMULTISAMPLE_NONE, 0u, out);
    {
        UINT row_count;
        if (!texture_level_layout(format, width, height, &row_pitch,
                &row_count, &byte_count))
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }

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
static HRESULT WINAPI device_multiply_transform(IDirect3DDevice9 *iface,
        D3DTRANSFORMSTATETYPE state, const D3DMATRIX *matrix)
{
    D9Device *device = device_from_iface(iface);
    D3DMATRIX result;
    UINT row, column, k;
    if (!matrix || (UINT)state >= D9_MAX_TRANSFORMS)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    for (row = 0; row < 4; ++row) {
        for (column = 0; column < 4; ++column) {
            float value = 0.0f;
            for (k = 0; k < 4; ++k)
                value += device->transforms[state][row * 4 + k]
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
    state_block_record_material(device);
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

static HRESULT WINAPI device_set_light(IDirect3DDevice9 *iface, DWORD index,
        const D3DLIGHT9 *light)
{
    D9Device *device = device_from_iface(iface);
    D9WGSetLight command;
    if (!light || index >= D9_MAX_LIGHTS)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    state_block_record_light(device, index);
    device->lights[index] = *light;
    device->light_set[index] = TRUE;
    command.device_handle = device->handle;
    command.index = index;
    command.type = (uint32_t)light->Type;
    CopyMemory(command.diffuse, &light->Diffuse, sizeof(command.diffuse));
    CopyMemory(command.specular, &light->Specular, sizeof(command.specular));
    CopyMemory(command.ambient, &light->Ambient, sizeof(command.ambient));
    CopyMemory(command.position, &light->Position, sizeof(command.position));
    CopyMemory(command.direction, &light->Direction, sizeof(command.direction));
    command.range = light->Range;
    command.falloff = light->Falloff;
    command.attenuation[0] = light->Attenuation0;
    command.attenuation[1] = light->Attenuation1;
    command.attenuation[2] = light->Attenuation2;
    command.theta = light->Theta;
    command.phi = light->Phi;
    return emit_command(D9WG_OP_SET_LIGHT, &command, sizeof(command))
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_get_light(IDirect3DDevice9 *iface, DWORD index,
        D3DLIGHT9 *light)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetLight");
    D9Device *device = device_from_iface(iface);
    if (!light || index >= D9_MAX_LIGHTS || !device->light_set[index])
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *light = device->lights[index];
    return D3D_OK;
}

static HRESULT WINAPI device_light_enable(IDirect3DDevice9 *iface,
        DWORD index, WINBOOL enable)
{
    D9Device *device = device_from_iface(iface);
    D9WGLightEnable command;
    if (index >= D9_MAX_LIGHTS)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    state_block_record_light_enable(device, index);
    device->light_enabled[index] = enable ? TRUE : FALSE;
    command.device_handle = device->handle;
    command.index = index;
    command.enable = enable ? 1u : 0u;
    command.reserved = 0;
    return emit_command(D9WG_OP_LIGHT_ENABLE, &command, sizeof(command))
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
}

static HRESULT WINAPI device_get_light_enable(IDirect3DDevice9 *iface,
        DWORD index, WINBOOL *enable)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetLightEnable");
    D9Device *device = device_from_iface(iface);
    if (!enable || index >= D9_MAX_LIGHTS)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *enable = device->light_enabled[index];
    return D3D_OK;
}
static HRESULT WINAPI device_set_clip_plane(IDirect3DDevice9 *iface,
        DWORD index, const float *plane)
{
    D9Device *device = device_from_iface(iface);
    D9WGSetClipPlane command;
    if (!plane || index >= D9_MAX_CLIP_PLANES)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
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
    device_from_iface(iface)->clip_status = *status;
    return D3D_OK;
}
static HRESULT WINAPI device_get_clip_status(IDirect3DDevice9 *iface,
        D3DCLIPSTATUS9 *status)
{
    PURE_DEVICE_REFUSE(device_from_iface(iface), "Device.GetClipStatus");
    if (!status) return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *status = device_from_iface(iface)->clip_status;
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
    state_block_record_sampler_state(device, slot, (UINT)type);
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
    UINT entry;

    if (!entries || index >= D9_MAX_PALETTES)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    for (entry = 0; entry < 256u; ++entry) {
        device->palettes[index][entry] =
                ((DWORD)entries[entry].peFlags << 24)
                | ((DWORD)entries[entry].peRed << 16)
                | ((DWORD)entries[entry].peGreen << 8)
                | (DWORD)entries[entry].peBlue;
    }
    device->palette_valid[index] = TRUE;

    ZeroMemory(&command, sizeof(command));
    EnterCriticalSection(&g_transport_lock);
    if (reserve_command_locked(D9WG_OP_SET_PALETTE, sizeof(command),
            256u * 4u, NULL, &payload, &blob)) {
        command.device_handle = device->handle;
        command.palette_index = index;
        command.entry_count = 256u;
        command.data_offset = (uint32_t)(blob - batch_base());
        CopyMemory(payload, &command, sizeof(command));
        CopyMemory(blob, device->palettes[index], 256u * 4u);
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
    UINT entry;

    if (!entries || index >= D9_MAX_PALETTES || !device->palette_valid[index])
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    for (entry = 0; entry < 256u; ++entry) {
        const DWORD value = device->palettes[index][entry];
        entries[entry].peRed = (BYTE)((value >> 16) & 0xFF);
        entries[entry].peGreen = (BYTE)((value >> 8) & 0xFF);
        entries[entry].peBlue = (BYTE)(value & 0xFF);
        entries[entry].peFlags = (BYTE)((value >> 24) & 0xFF);
    }
    return D3D_OK;
}

static HRESULT WINAPI device_set_current_texture_palette(
        IDirect3DDevice9 *iface, UINT index)
{
    D9Device *device = device_from_iface(iface);
    D9WGSetCurrentTexturePalette command;

    /* D3D9 requires the palette to have been filled first: selecting an unset
     * one would sample colours the app never chose. */
    if (index >= D9_MAX_PALETTES || !device->palette_valid[index])
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (device->current_palette_set && device->current_palette == index)
        return D3D_OK;
    device->current_palette = index;
    device->current_palette_set = TRUE;
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
    if (!index || !device->current_palette_set)
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
 * Two deliberate limits, both named rather than silent:
 *
 *   - A bound vertex shader means programmable processing, and running the
 *     fixed-function pipeline instead would silently produce different
 *     geometry. Refused.
 *   - Lighting computes the ambient and diffuse terms. D3DRS_SPECULARENABLE's
 *     highlight is not computed; a destination asking for SPECULAR gets the
 *     source's specular copied through, which is what the D3D8 path does.
 */
#define D9_PV_MAX_TEXCOORDS 8

typedef struct D9ProcessLayout {
    int position;
    BOOL position_transformed;
    int normal;
    int diffuse;
    int specular;
    int psize;
    int texcoord[D9_PV_MAX_TEXCOORDS];
    UINT texcoord_floats[D9_PV_MAX_TEXCOORDS];
    UINT stride;
} D9ProcessLayout;

/* This DLL links no C runtime (see build.sh), so libm's sqrtf is unavailable.
 * Newton-Raphson from the classic bit-level estimate converges to float
 * precision well within four iterations for the magnitudes vertex normals and
 * light distances actually take. */
static float d9_sqrt(float value)
{
    union { float number; uint32_t bits; } convert;
    float guess;
    UINT iteration;

    if (!(value > 0.0f)) return 0.0f;
    convert.number = value;
    convert.bits = 0x1fbd1df5u + (convert.bits >> 1);
    guess = convert.number;
    for (iteration = 0; iteration < 4u; ++iteration)
        guess = 0.5f * (guess + value / guess);
    return guess;
}

/* D3D matrices are row-major and its vectors are row vectors, so this is
 * `in * matrix`, not `matrix * in`. Getting the side wrong transposes every
 * transform, which looks like a broken camera rather than a broken multiply. */
static void d9_transform_vector4(const D3DMATRIX *matrix, const float *in,
        float *out)
{
    UINT row;
    for (row = 0; row < 4; ++row) {
        out[row] = in[0] * matrix->m[0][row] + in[1] * matrix->m[1][row]
                + in[2] * matrix->m[2][row] + in[3] * matrix->m[3][row];
    }
}

static void d9_multiply_matrix(const D3DMATRIX *left, const D3DMATRIX *right,
        D3DMATRIX *out)
{
    UINT row;
    UINT column;
    for (row = 0; row < 4; ++row) {
        for (column = 0; column < 4; ++column) {
            out->m[row][column] =
                    left->m[row][0] * right->m[0][column]
                    + left->m[row][1] * right->m[1][column]
                    + left->m[row][2] * right->m[2][column]
                    + left->m[row][3] * right->m[3][column];
        }
    }
}

static float d9_saturate(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static D3DCOLOR d9_pack_color(const float *rgba)
{
    return ((DWORD)(d9_saturate(rgba[3]) * 255.0f + 0.5f) << 24)
            | ((DWORD)(d9_saturate(rgba[0]) * 255.0f + 0.5f) << 16)
            | ((DWORD)(d9_saturate(rgba[1]) * 255.0f + 0.5f) << 8)
            | (DWORD)(d9_saturate(rgba[2]) * 255.0f + 0.5f);
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

/*
 * The subset of D3DDECLTYPE this software path reads and writes directly.
 * Everything else -- the packed and half-float formats -- is rejected rather
 * than approximated, because a silently mis-decoded position is geometry that
 * is wrong rather than missing.
 */
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
    UINT index;

    ZeroMemory(layout, sizeof(*layout));
    layout->position = -1;
    layout->normal = -1;
    layout->diffuse = -1;
    layout->specular = -1;
    layout->psize = -1;
    for (index = 0; index < D9_PV_MAX_TEXCOORDS; ++index)
        layout->texcoord[index] = -1;

    for (index = 0; index < count; ++index) {
        const D9WGVertexElement *e = &elements[index];
        UINT bytes = d9_decl_type_bytes(e->type);
        UINT end;
        UINT floats;

        /* Only stream 0 participates. A declaration spreading its attributes
         * over several streams is legal D3D9 and this path cannot serve it
         * without also tracking every stream's stride and base -- refusing is
         * the honest answer rather than reading stream 0 at the wrong offset. */
        if (e->stream != 0)
            return FALSE;
        if (!bytes || e->method != D3DDECLMETHOD_DEFAULT)
            return FALSE;
        end = (UINT)e->offset + bytes;
        if (end > layout->stride) layout->stride = end;

        switch (e->usage) {
        case D3DDECLUSAGE_POSITION:
            if (e->usage_index) break;
            if (!d9_decl_type_float_count(e->type, &floats) || floats < 3)
                return FALSE;
            layout->position = (int)e->offset;
            layout->position_transformed = FALSE;
            break;
        case D3DDECLUSAGE_POSITIONT:
            if (e->usage_index) break;
            if (e->type != D3DDECLTYPE_FLOAT4)
                return FALSE;
            layout->position = (int)e->offset;
            layout->position_transformed = TRUE;
            break;
        case D3DDECLUSAGE_NORMAL:
            if (e->usage_index) break;
            if (!d9_decl_type_float_count(e->type, &floats) || floats < 3)
                return FALSE;
            layout->normal = (int)e->offset;
            break;
        case D3DDECLUSAGE_COLOR:
            if (e->type != D3DDECLTYPE_D3DCOLOR)
                return FALSE;
            if (e->usage_index == 0) layout->diffuse = (int)e->offset;
            else if (e->usage_index == 1) layout->specular = (int)e->offset;
            break;
        case D3DDECLUSAGE_PSIZE:
            if (e->usage_index || e->type != D3DDECLTYPE_FLOAT1)
                return FALSE;
            layout->psize = (int)e->offset;
            break;
        case D3DDECLUSAGE_TEXCOORD:
            if (e->usage_index >= D9_PV_MAX_TEXCOORDS)
                return FALSE;
            if (!d9_decl_type_float_count(e->type, &floats))
                return FALSE;
            layout->texcoord[e->usage_index] = (int)e->offset;
            layout->texcoord_floats[e->usage_index] = floats;
            break;
        default:
            /* BLENDWEIGHT/BLENDINDICES and the rest are simply not carried
             * through; their bytes still count toward the stride above. */
            break;
        }
    }
    return layout->position >= 0;
}

/*
 * The fixed-function ambient and diffuse terms D3D9 specifies, in view space,
 * over the lights the guest has shadowed. Directional, point and spot lights
 * all reach here; the spot cone uses the same rho/theta/phi falloff the host's
 * generated vertex stage uses, so the two paths agree on the same scene.
 */
static void d9_light_vertex(D9Device *device, const float *eye_position,
        const float *eye_normal, float *out_rgba)
{
    const D3DMATERIAL9 *material = &device->material;
    DWORD ambient_state = device->render_states[D3DRS_AMBIENT];
    float ambient[3];
    UINT index;

    ambient[0] = ((ambient_state >> 16) & 0xffu) / 255.0f;
    ambient[1] = ((ambient_state >> 8) & 0xffu) / 255.0f;
    ambient[2] = (ambient_state & 0xffu) / 255.0f;

    out_rgba[0] = material->Emissive.r + material->Ambient.r * ambient[0];
    out_rgba[1] = material->Emissive.g + material->Ambient.g * ambient[1];
    out_rgba[2] = material->Emissive.b + material->Ambient.b * ambient[2];
    out_rgba[3] = material->Diffuse.a;

    for (index = 0; index < D9_MAX_LIGHTS; ++index) {
        const D3DLIGHT9 *light;
        float to_light[3];
        float attenuation = 1.0f;
        float n_dot_l;
        float length;

        if (!device->light_set[index] || !device->light_enabled[index])
            continue;
        light = &device->lights[index];
        if (light->Type == D3DLIGHT_DIRECTIONAL) {
            float view_direction[4];
            float direction[4];
            direction[0] = light->Direction.x;
            direction[1] = light->Direction.y;
            direction[2] = light->Direction.z;
            direction[3] = 0.0f;
            d9_transform_vector4(
                    (const D3DMATRIX *)device->transforms[D3DTS_VIEW],
                    direction, view_direction);
            length = d9_sqrt(view_direction[0] * view_direction[0]
                    + view_direction[1] * view_direction[1]
                    + view_direction[2] * view_direction[2]);
            if (length < 1e-6f) continue;
            to_light[0] = -view_direction[0] / length;
            to_light[1] = -view_direction[1] / length;
            to_light[2] = -view_direction[2] / length;
        } else {
            float view_position[4];
            float position[4];
            position[0] = light->Position.x;
            position[1] = light->Position.y;
            position[2] = light->Position.z;
            position[3] = 1.0f;
            d9_transform_vector4(
                    (const D3DMATRIX *)device->transforms[D3DTS_VIEW],
                    position, view_position);
            to_light[0] = view_position[0] - eye_position[0];
            to_light[1] = view_position[1] - eye_position[1];
            to_light[2] = view_position[2] - eye_position[2];
            length = d9_sqrt(to_light[0] * to_light[0]
                    + to_light[1] * to_light[1] + to_light[2] * to_light[2]);
            if (length > light->Range) continue;
            if (length < 1e-6f) length = 1e-6f;
            to_light[0] /= length;
            to_light[1] /= length;
            to_light[2] /= length;
            attenuation = light->Attenuation0
                    + light->Attenuation1 * length
                    + light->Attenuation2 * length * length;
            attenuation = attenuation < 1e-6f ? 1.0f : 1.0f / attenuation;
            if (light->Type == D3DLIGHT_SPOT) {
                float spot_direction[4];
                float direction[4];
                float rho;
                float spot = 0.0f;
                direction[0] = light->Direction.x;
                direction[1] = light->Direction.y;
                direction[2] = light->Direction.z;
                direction[3] = 0.0f;
                d9_transform_vector4(
                        (const D3DMATRIX *)device->transforms[D3DTS_VIEW],
                        direction, spot_direction);
                length = d9_sqrt(spot_direction[0] * spot_direction[0]
                        + spot_direction[1] * spot_direction[1]
                        + spot_direction[2] * spot_direction[2]);
                if (length < 1e-6f) continue;
                /* rho is the cosine between the cone axis and the direction
                 * *to the vertex*, which is the negation of to_light. */
                rho = (-to_light[0] * spot_direction[0]
                        - to_light[1] * spot_direction[1]
                        - to_light[2] * spot_direction[2]) / length;
                if (rho > light->Theta) {
                    spot = 1.0f;
                } else if (rho > light->Phi) {
                    /* Falloff is an exponent D3D9 defaults to 1.0, and this
                     * DLL has no pow(); the linear case is exact and anything
                     * else is treated as linear rather than approximated with
                     * a series that would be wrong in a different way. */
                    spot = (rho - light->Phi)
                            / (light->Theta - light->Phi > 1e-6f
                                ? light->Theta - light->Phi : 1e-6f);
                }
                attenuation *= spot;
            }
        }
        n_dot_l = eye_normal[0] * to_light[0] + eye_normal[1] * to_light[1]
                + eye_normal[2] * to_light[2];
        if (n_dot_l < 0.0f) n_dot_l = 0.0f;
        out_rgba[0] += attenuation * (material->Ambient.r * light->Ambient.r
                + material->Diffuse.r * light->Diffuse.r * n_dot_l);
        out_rgba[1] += attenuation * (material->Ambient.g * light->Ambient.g
                + material->Diffuse.g * light->Diffuse.g * n_dot_l);
        out_rgba[2] += attenuation * (material->Ambient.b * light->Ambient.b
                + material->Diffuse.b * light->Diffuse.b * n_dot_l);
    }
}

/* The source layout is the declaration currently bound, whether it arrived as
 * a real IDirect3DVertexDeclaration9 or as an FVF. */
static BOOL d9_current_source_layout(D9Device *device, D9ProcessLayout *layout)
{
    D9WGVertexElement elements[D3DMAXDECLLENGTH];
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
    return d9_build_process_layout(elements, count, layout);
}

static HRESULT WINAPI device_process_vertices(IDirect3DDevice9 *iface,
        UINT src_start, UINT dst_index, UINT count,
        IDirect3DVertexBuffer9 *dst, IDirect3DVertexDeclaration9 *decl,
        DWORD flags)
{
    D9Device *device = device_from_iface(iface);
    D9VertexBuffer *destination;
    D9VertexBuffer *source;
    D9ProcessLayout source_layout;
    D9ProcessLayout destination_layout;
    D3DMATRIX world_view;
    D3DMATRIX world_view_projection;
    BOOL lighting;
    BOOL copy_data;
    UINT vertex;
    UINT source_stride;

    TRACE_MARK_ENTER("Device.ProcessVertices");
    if (!dst || dst->lpVtbl != &g_vb_vtbl) {
        TRACE_MARK_EXIT("Device.ProcessVertices", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    destination = vb_from_iface(dst);
    source = device->streams[0].buffer;
    source_stride = device->streams[0].stride;
    if (destination->device != device || !source || destination->locked
            || source->locked || !count) {
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
    if (!d9_current_source_layout(device, &source_layout)) {
        HOSTLOG_REFUSED("ProcessVertices refused: the source declaration uses "
                "a stream, element type or method this software vertex path "
                "does not read");
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
    /* D3D9 refuses to transform geometry that already is, and requires the
     * result to be pre-transformed. */
    if (source_layout.position_transformed
            || !destination_layout.position_transformed) {
        HOSTLOG_REFUSED("ProcessVertices refused: the source must be "
                "untransformed (POSITION) and the destination pre-transformed "
                "(POSITIONT)");
        TRACE_MARK_EXIT("Device.ProcessVertices", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    if (!source_stride || source_stride < source_layout.stride) {
        TRACE_MARK_EXIT("Device.ProcessVertices", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    {
        UINT source_needed;
        UINT destination_needed;
        if (!multiply_u32(src_start + count, source_stride, &source_needed)
                || !multiply_u32(dst_index + count, destination_layout.stride,
                        &destination_needed)
                || source_needed > source->length
                || destination_needed > destination->length) {
            TRACE_MARK_EXIT("Device.ProcessVertices", D3DERR_INVALIDCALL, NULL);
            return D3DERR_INVALIDCALL;
        }
    }

    d9_multiply_matrix((const D3DMATRIX *)device->transforms[D3DTS_WORLD],
            (const D3DMATRIX *)device->transforms[D3DTS_VIEW], &world_view);
    d9_multiply_matrix(&world_view,
            (const D3DMATRIX *)device->transforms[D3DTS_PROJECTION],
            &world_view_projection);
    lighting = device->render_states[D3DRS_LIGHTING]
            && source_layout.normal >= 0;
    /* D3DPV_DONOTCOPYDATA says the caller only wants the transform; everything
     * but position keeps whatever the destination already held. */
    copy_data = (flags & D3DPV_DONOTCOPYDATA) == 0;

    for (vertex = 0; vertex < count; ++vertex) {
        const BYTE *in = source->shadow
                + (src_start + vertex) * source_stride;
        BYTE *out = destination->shadow
                + (dst_index + vertex) * destination_layout.stride;
        float position[4];
        float clip[4];
        float rhw;
        UINT index;

        position[0] = ((const float *)(in + source_layout.position))[0];
        position[1] = ((const float *)(in + source_layout.position))[1];
        position[2] = ((const float *)(in + source_layout.position))[2];
        position[3] = 1.0f;
        d9_transform_vector4(&world_view_projection, position, clip);
        rhw = (clip[3] > 1e-6f || clip[3] < -1e-6f) ? 1.0f / clip[3] : 1.0f;
        /* Clip space -> the viewport's pixel rectangle, which is what makes
         * the result drawable as XYZRHW. */
        ((float *)(out + destination_layout.position))[0] =
                (clip[0] * rhw * 0.5f + 0.5f) * (float)device->viewport.Width
                + (float)device->viewport.X;
        ((float *)(out + destination_layout.position))[1] =
                (0.5f - clip[1] * rhw * 0.5f) * (float)device->viewport.Height
                + (float)device->viewport.Y;
        ((float *)(out + destination_layout.position))[2] =
                device->viewport.MinZ + clip[2] * rhw
                * (device->viewport.MaxZ - device->viewport.MinZ);
        ((float *)(out + destination_layout.position))[3] = rhw;
        if (!copy_data)
            continue;

        if (destination_layout.diffuse >= 0) {
            if (lighting) {
                float eye_position[4];
                float normal[4];
                float eye_normal[4];
                float length;
                float lit[4];
                d9_transform_vector4(&world_view, position, eye_position);
                normal[0] = ((const float *)(in + source_layout.normal))[0];
                normal[1] = ((const float *)(in + source_layout.normal))[1];
                normal[2] = ((const float *)(in + source_layout.normal))[2];
                normal[3] = 0.0f;
                d9_transform_vector4(&world_view, normal, eye_normal);
                length = d9_sqrt(eye_normal[0] * eye_normal[0]
                        + eye_normal[1] * eye_normal[1]
                        + eye_normal[2] * eye_normal[2]);
                if (length > 1e-6f) {
                    eye_normal[0] /= length;
                    eye_normal[1] /= length;
                    eye_normal[2] /= length;
                }
                d9_light_vertex(device, eye_position, eye_normal, lit);
                *(D3DCOLOR *)(out + destination_layout.diffuse) =
                        d9_pack_color(lit);
            } else if (source_layout.diffuse >= 0) {
                *(D3DCOLOR *)(out + destination_layout.diffuse) =
                        *(const D3DCOLOR *)(in + source_layout.diffuse);
            } else {
                *(D3DCOLOR *)(out + destination_layout.diffuse) = 0xffffffffu;
            }
        }
        if (destination_layout.specular >= 0) {
            /* Copied, never computed: see the note above about
             * D3DRS_SPECULARENABLE. */
            *(D3DCOLOR *)(out + destination_layout.specular) =
                    source_layout.specular >= 0
                    ? *(const D3DCOLOR *)(in + source_layout.specular) : 0;
        }
        if (destination_layout.psize >= 0) {
            *(float *)(out + destination_layout.psize) =
                    source_layout.psize >= 0
                    ? *(const float *)(in + source_layout.psize) : 1.0f;
        }
        for (index = 0; index < D9_PV_MAX_TEXCOORDS; ++index) {
            UINT floats = destination_layout.texcoord_floats[index];
            UINT component;
            if (destination_layout.texcoord[index] < 0) continue;
            for (component = 0; component < floats; ++component) {
                float value = 0.0f;
                if (source_layout.texcoord[index] >= 0
                        && component < source_layout.texcoord_floats[index]) {
                    value = ((const float *)(in
                            + source_layout.texcoord[index]))[component];
                }
                ((float *)(out
                        + destination_layout.texcoord[index]))[component] =
                        value;
            }
        }
    }

    /* The transformed vertices live in the destination's guest shadow; the
     * host needs them too, since the app will draw straight from the buffer. */
    TRACE("OK ProcessVertices device=%08lX count=%lu stride=%lu lit=%lu",
            device->handle, count, destination_layout.stride,
            (DWORD)lighting);
    TRACE_MARK_EXIT("Device.ProcessVertices", D3D_OK, NULL);
    return emit_buffer_update(destination->handle,
            dst_index * destination_layout.stride,
            destination->shadow + dst_index * destination_layout.stride,
            count * destination_layout.stride, 0)
            ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
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

    state_block_record_stream(device, stream);
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

    result = device_draw_indexed_primitive_up(iface, D3DPT_TRIANGLELIST, 0,
            total_points, total_indices / 3u, out_indices, D3DFMT_INDEX16,
            vertices, format.stride);
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
    result = device_draw_indexed_primitive_up(iface, D3DPT_TRIANGLELIST, 0,
            grid * grid, index, indices, D3DFMT_INDEX16, vertices,
            format.stride);

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
    result = device_draw_indexed_primitive_up(iface, D3DPT_TRIANGLELIST, 0,
            grid * grid, triangles, indices, D3DFMT_INDEX16, vertices,
            format.stride);

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
    device_child_add_ref(device);
    if (!emit_texture_create(device, texture)) {
        device_child_release(device);
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
    return create_target_texture(device_from_iface(iface), width, height, format,
            D3DUSAGE_RENDERTARGET, multisample, multisample_quality,
            surface_out);
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
        IDirect3DSurface9_AddRef(surface_iface);
    device->render_target_surfaces[index] = surface;
    if (old_surface)
        IDirect3DSurface9_Release(&old_surface->iface);

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
        viewport.X = 0;
        viewport.Y = 0;
        viewport.Width = surface->width;
        viewport.Height = surface->height;
        viewport.MinZ = 0.0f;
        viewport.MaxZ = 1.0f;
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
        IDirect3DSurface9_AddRef(surface_iface);
    device->depth_stencil_surface = surface;
    if (old_surface)
        IDirect3DSurface9_Release(&old_surface->iface);
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
    state_block_record_scissor(device);
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
    struct D9Query *next_device_resource;
} D9Query;

static IDirect3DQuery9Vtbl g_query_vtbl;

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
    return D3D_OK;
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
            || response->status == D9WG_RESPONSE_PENDING)
        return S_FALSE;
    if (response->status != D9WG_RESPONSE_OK)
        return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);

    value = ((ULONGLONG)response->value_high << 32) | response->value_low;
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
    return emit_command(D9WG_OP_CREATE_TEXTURE_CUBE, &command, sizeof(command));
}

/* Shares the 2D upload path's payload shape; only the resource handle and the
 * face index differ, so a bug in one cannot silently diverge from the other. */
static BOOL emit_cube_texture_update(D9CubeTexture *texture, UINT face,
        UINT level, const RECT *rect)
{
    D9TextureLevel *level_data = &texture->levels[face * texture->level_count + level];
    D9WGUpdateTexture command;
    UINT block_width, block_height, block_bytes;
    UINT block_x, block_y, block_columns, block_rows;
    uint8_t *payload;
    uint8_t *blob;
    uint32_t data_bytes;
    BOOL result;

    if (!texture_format_layout(texture->format, &block_width, &block_height,
            &block_bytes))
        return FALSE;
    block_x = (UINT)rect->left / block_width;
    block_y = (UINT)rect->top / block_height;
    block_columns = ((UINT)rect->right - (UINT)rect->left + block_width - 1)
            / block_width;
    block_rows = ((UINT)rect->bottom - (UINT)rect->top + block_height - 1)
            / block_height;
    if (!block_columns || !block_rows)
        return TRUE;
    data_bytes = block_rows * block_columns * block_bytes;

    ZeroMemory(&command, sizeof(command));
    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_UPDATE_TEXTURE, sizeof(command),
            data_bytes, NULL, &payload, &blob);
    if (result) {
        UINT row;
        command.resource_handle = texture->handle;
        command.level = level;
        command.x = (UINT)rect->left;
        command.y = (UINT)rect->top;
        command.z = face;
        command.width = (UINT)rect->right - (UINT)rect->left;
        command.height = (UINT)rect->bottom - (UINT)rect->top;
        command.depth = 1;
        command.row_pitch = block_columns * block_bytes;
        command.slice_pitch = 0;
        command.data_bytes = data_bytes;
        command.data_offset = (uint32_t)(blob - batch_base());
        CopyMemory(payload, &command, sizeof(command));
        for (row = 0; row < block_rows; ++row) {
            CopyMemory(blob + row * block_columns * block_bytes,
                    level_data->shadow + (block_y + row) * level_data->row_pitch
                        + block_x * block_bytes,
                    block_columns * block_bytes);
        }
    }
    LeaveCriticalSection(&g_transport_lock);
    return result;
}

static HRESULT WINAPI device_create_cube_texture(IDirect3DDevice9 *iface,
        UINT edge, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
        IDirect3DCubeTexture9 **texture_out, HANDLE *shared)
{
    D9Device *device = device_from_iface(iface);
    D9CubeTexture *texture;
    UINT full_levels;
    UINT face;
    UINT level;
    UINT total;
    HRESULT failure = E_OUTOFMEMORY;
    HRESULT success = D3D_OK;
    (void)shared;

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
    /* See device_create_texture: one visible level, a full chain behind it. */
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
            if (!texture_level_layout(format, size, size,
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
    return (ULONG)InterlockedIncrement(&cube_from_iface(iface)->refcount);
}

static ULONG WINAPI cube_release(IDirect3DCubeTexture9 *iface)
{
    D9CubeTexture *texture = cube_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&texture->refcount);
    if (!refs) {
        D9CubeTexture **link = &texture->device->cube_textures;
        D9WGDestroyResource destroy;
        UINT index;
        while (*link && *link != texture)
            link = &(*link)->next_device_resource;
        if (*link) *link = texture->next_device_resource;
        destroy.resource_handle = texture->handle;
        destroy.resource_kind = D9WG_RESOURCE_TEXTURE_CUBE;
        emit_command(D9WG_OP_DESTROY_RESOURCE, &destroy, sizeof(destroy));
        for (index = 0; index < texture->level_count * 6u; ++index) {
            HeapFree(GetProcessHeap(), 0,
                    texture->levels[index].level_surface);
            HeapFree(GetProcessHeap(), 0, texture->levels[index].shadow);
        }
        HeapFree(GetProcessHeap(), 0, texture->levels);
        device_child_release(texture->device);
        HeapFree(GetProcessHeap(), 0, texture);
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

static HRESULT WINAPI cube_set_private_data(IDirect3DCubeTexture9 *iface,
        REFGUID guid, const void *data, DWORD size, DWORD flags)
{ (void)iface; (void)guid; (void)data; (void)size; (void)flags;
  return TRACE_REFUSE(D3DERR_INVALIDCALL); }
static HRESULT WINAPI cube_get_private_data(IDirect3DCubeTexture9 *iface,
        REFGUID guid, void *data, DWORD *size)
{ (void)iface; (void)guid; (void)data; (void)size; return TRACE_REFUSE(D3DERR_NOTFOUND); }
static HRESULT WINAPI cube_free_private_data(IDirect3DCubeTexture9 *iface,
        REFGUID guid)
{ (void)iface; (void)guid; return TRACE_REFUSE(D3DERR_NOTFOUND); }

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
    DWORD previous = texture->lod;
    if (texture->pool != D3DPOOL_MANAGED) return previous;
    if (lod >= texture->level_count) lod = texture->level_count - 1;
    texture->lod = lod;
    emit_texture_lod(texture->device, texture->handle, lod);
    return previous;
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
    if (type != D3DTEXF_POINT && type != D3DTEXF_LINEAR
            && type != D3DTEXF_NONE)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
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
    emit_generate_mips(texture->device, texture->handle);
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
            || !texture_format_layout(texture->format, &block_width,
                    &block_height, &block_bytes))
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
 * BeginStateBlock/EndStateBlock keeps both a pre-Begin snapshot and a separate
 * call mask. Every implemented Set* entry marks the latter before its normal
 * redundant-state suppression, so setting an existing value and setting then
 * reverting are recorded exactly as D3D9 requires. Setters run while recording
 * and End restores the snapshot; drawing during recording is invalid in D3D9,
 * so no draw can observe the temporary live values.
 */

/* D3DSBT_PIXELSTATE's documented render-state list. Capturing more than D3D9
 * does is not a safe simplification: Apply() would then restore states the app
 * deliberately kept, which shows up as an unrelated render mode snapping back. */
static const WORD g_pixel_state_render_states[] = {
    D3DRS_ZENABLE, D3DRS_FILLMODE, D3DRS_SHADEMODE, D3DRS_ZWRITEENABLE,
    D3DRS_ALPHATESTENABLE, D3DRS_LASTPIXEL, D3DRS_SRCBLEND, D3DRS_DESTBLEND,
    D3DRS_ZFUNC, D3DRS_ALPHAREF, D3DRS_ALPHAFUNC, D3DRS_DITHERENABLE,
    D3DRS_FOGSTART, D3DRS_FOGEND, D3DRS_FOGDENSITY, D3DRS_ALPHABLENDENABLE,
    D3DRS_DEPTHBIAS, D3DRS_STENCILENABLE, D3DRS_STENCILFAIL,
    D3DRS_STENCILZFAIL, D3DRS_STENCILPASS, D3DRS_STENCILFUNC,
    D3DRS_STENCILREF, D3DRS_STENCILMASK, D3DRS_STENCILWRITEMASK,
    D3DRS_TEXTUREFACTOR, D3DRS_WRAP0, D3DRS_WRAP1, D3DRS_WRAP2, D3DRS_WRAP3,
    D3DRS_WRAP4, D3DRS_WRAP5, D3DRS_WRAP6, D3DRS_WRAP7, D3DRS_WRAP8,
    D3DRS_WRAP9, D3DRS_WRAP10, D3DRS_WRAP11, D3DRS_WRAP12, D3DRS_WRAP13,
    D3DRS_WRAP14, D3DRS_WRAP15, D3DRS_COLORWRITEENABLE, D3DRS_BLENDOP,
    D3DRS_SCISSORTESTENABLE, D3DRS_SLOPESCALEDEPTHBIAS,
    D3DRS_ANTIALIASEDLINEENABLE, D3DRS_TWOSIDEDSTENCILMODE,
    D3DRS_CCW_STENCILFAIL, D3DRS_CCW_STENCILZFAIL, D3DRS_CCW_STENCILPASS,
    D3DRS_CCW_STENCILFUNC, D3DRS_COLORWRITEENABLE1, D3DRS_COLORWRITEENABLE2,
    D3DRS_COLORWRITEENABLE3, D3DRS_BLENDFACTOR, D3DRS_SRGBWRITEENABLE,
    D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_SRCBLENDALPHA,
    D3DRS_DESTBLENDALPHA, D3DRS_BLENDOPALPHA
};

static const WORD g_vertex_state_render_states[] = {
    D3DRS_CULLMODE, D3DRS_FOGENABLE, D3DRS_FOGCOLOR, D3DRS_FOGTABLEMODE,
    D3DRS_FOGSTART, D3DRS_FOGEND, D3DRS_FOGDENSITY, D3DRS_RANGEFOGENABLE,
    D3DRS_AMBIENT, D3DRS_COLORVERTEX, D3DRS_FOGVERTEXMODE, D3DRS_CLIPPING,
    D3DRS_LIGHTING, D3DRS_LOCALVIEWER, D3DRS_EMISSIVEMATERIALSOURCE,
    D3DRS_AMBIENTMATERIALSOURCE, D3DRS_DIFFUSEMATERIALSOURCE,
    D3DRS_SPECULARMATERIALSOURCE, D3DRS_VERTEXBLEND, D3DRS_CLIPPLANEENABLE,
    D3DRS_POINTSIZE, D3DRS_POINTSIZE_MIN, D3DRS_POINTSPRITEENABLE,
    D3DRS_POINTSCALEENABLE, D3DRS_POINTSCALE_A, D3DRS_POINTSCALE_B,
    D3DRS_POINTSCALE_C, D3DRS_MULTISAMPLEANTIALIAS, D3DRS_MULTISAMPLEMASK,
    D3DRS_PATCHEDGESTYLE, D3DRS_POINTSIZE_MAX,
    D3DRS_INDEXEDVERTEXBLENDENABLE, D3DRS_TWEENFACTOR,
    D3DRS_NORMALIZENORMALS, D3DRS_SPECULARENABLE, D3DRS_SHADEMODE
};

/* The transform indices a state block tracks. D3D9's full D3DTRANSFORMSTATETYPE
 * space is 512 entries wide, mostly the 256 world matrices vertex blending
 * uses, and snapshotting all of them would cost 32 KB per block.
 *
 * D3DTS_WORLDMATRIX(1..3) are here because unindexed vertex blending reads
 * exactly those: fill_caps() reports MaxVertexBlendMatrices = 4, so a block
 * that captured only D3DTS_WORLD would restore a skinned draw's pose from one
 * matrix and three stale ones. Indexed blending can name any of the other 252,
 * which a block still does not capture -- an app that records a state block
 * around an indexed-skinned pass and expects the palette restored gets the
 * palette as the pass left it. That is a bounded, stated limit rather than an
 * unnoticed one; lifting it means paying the 32 KB. */
static const WORD g_state_block_transforms[] = {
    D3DTS_VIEW, D3DTS_PROJECTION, D3DTS_WORLD,
    D3DTS_WORLDMATRIX(1), D3DTS_WORLDMATRIX(2), D3DTS_WORLDMATRIX(3),
    D3DTS_TEXTURE0, D3DTS_TEXTURE1, D3DTS_TEXTURE2, D3DTS_TEXTURE3,
    D3DTS_TEXTURE4, D3DTS_TEXTURE5, D3DTS_TEXTURE6, D3DTS_TEXTURE7
};
#define D9_STATE_BLOCK_TRANSFORMS \
    (sizeof(g_state_block_transforms) / sizeof(g_state_block_transforms[0]))

struct D9StateBlock {
    IDirect3DStateBlock9 iface;
    LONG refcount;
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
    BOOL has_material;
    D3DMATERIAL9 material;
    BOOL has_light[D9_MAX_LIGHTS];
    D3DLIGHT9 lights[D9_MAX_LIGHTS];
    BOOL has_light_enable[D9_MAX_LIGHTS];
    BOOL light_enabled[D9_MAX_LIGHTS];
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
    D9StreamBinding streams[D9_MAX_STREAMS];
    BOOL has_indices;
    D9IndexBuffer *index_buffer;
    struct D9StateBlock *next_device_resource;
};

static IDirect3DStateBlock9Vtbl g_state_block_vtbl;

static struct D9StateBlock *state_block_from_iface(IDirect3DStateBlock9 *iface)
{
    return (struct D9StateBlock *)iface;
}

static void state_block_record_render_state(D9Device *device, UINT state)
{
    if (device->recording_result && state < D9_MAX_RENDER_STATES)
        device->recording_result->has_render_state[state] = TRUE;
}

static void state_block_record_texture_stage_state(D9Device *device,
        UINT stage, UINT state)
{
    if (device->recording_result && stage < D9_MAX_TEXTURE_STAGES
            && state < D9_MAX_TEXTURE_STAGE_STATES)
        device->recording_result->has_texture_stage_state[stage][state] = TRUE;
}

static void state_block_record_sampler_state(D9Device *device,
        UINT sampler, UINT state)
{
    if (device->recording_result && sampler < D9_MAX_SAMPLERS
            && state < D9_MAX_SAMPLER_STATES)
        device->recording_result->has_sampler_state[sampler][state] = TRUE;
}

static void state_block_record_transform(D9Device *device, UINT state)
{
    UINT index;
    if (!device->recording_result)
        return;
    for (index = 0; index < D9_STATE_BLOCK_TRANSFORMS; ++index) {
        if (g_state_block_transforms[index] == state) {
            device->recording_result->has_transform[index] = TRUE;
            return;
        }
    }
}

static void state_block_record_texture(D9Device *device, UINT stage)
{
    if (device->recording_result && stage < D9_MAX_TEXTURE_BINDINGS)
        device->recording_result->has_texture[stage] = TRUE;
}

static void state_block_record_material(D9Device *device)
{
    if (device->recording_result)
        device->recording_result->has_material = TRUE;
}

static void state_block_record_light(D9Device *device, UINT index)
{
    if (device->recording_result && index < D9_MAX_LIGHTS)
        device->recording_result->has_light[index] = TRUE;
}

static void state_block_record_light_enable(D9Device *device, UINT index)
{
    if (device->recording_result && index < D9_MAX_LIGHTS)
        device->recording_result->has_light_enable[index] = TRUE;
}

static void state_block_record_viewport(D9Device *device)
{
    if (device->recording_result)
        device->recording_result->has_viewport = TRUE;
}

static void state_block_record_scissor(D9Device *device)
{
    if (device->recording_result)
        device->recording_result->has_scissor = TRUE;
}

static void state_block_record_vertex_shader(D9Device *device)
{
    if (device->recording_result)
        device->recording_result->has_vertex_shader = TRUE;
}

static void state_block_record_pixel_shader(D9Device *device)
{
    if (device->recording_result)
        device->recording_result->has_pixel_shader = TRUE;
}

static void state_block_record_constant(D9Device *device, BOOL is_pixel,
        BOOL is_float, BOOL is_bool, UINT start, UINT count)
{
    struct D9StateBlock *block = device->recording_result;
    UINT index;
    if (!block)
        return;
    for (index = start; index < start + count; ++index) {
        if (is_float) {
            if (is_pixel) block->has_ps_const_f[index] = TRUE;
            else block->has_vs_const_f[index] = TRUE;
        } else if (is_bool) {
            if (is_pixel) block->has_ps_const_b[index] = TRUE;
            else block->has_vs_const_b[index] = TRUE;
        } else {
            if (is_pixel) block->has_ps_const_i[index] = TRUE;
            else block->has_vs_const_i[index] = TRUE;
        }
    }
}

static void state_block_record_vertex_format(D9Device *device)
{
    if (device->recording_result)
        device->recording_result->has_vertex_format = TRUE;
}

static void state_block_record_stream(D9Device *device, UINT stream)
{
    if (device->recording_result && stream < D9_MAX_STREAMS)
        device->recording_result->has_stream[stream] = TRUE;
}

static void state_block_record_indices(D9Device *device)
{
    if (device->recording_result)
        device->recording_result->has_indices = TRUE;
}

/* D3D9 state blocks keep every captured COM object alive.  Without these
 * references an engine may release the texture/shader it used to build a UI
 * block, leaving Apply() with a dangling pointer and the host with a destroyed
 * resource handle. */
static void state_block_release_references(struct D9StateBlock *block)
{
    UINT index;
    for (index = 0; index < D9_MAX_TEXTURE_BINDINGS; ++index) {
        if (block->textures[index])
            IDirect3DTexture9_Release(&block->textures[index]->iface);
        if (block->cube_textures[index])
            IDirect3DCubeTexture9_Release(&block->cube_textures[index]->iface);
        if (block->volume_textures[index])
            IDirect3DVolumeTexture9_Release(
                    &block->volume_textures[index]->iface);
        block->textures[index] = NULL;
        block->cube_textures[index] = NULL;
        block->volume_textures[index] = NULL;
    }
    if (block->vertex_shader)
        IDirect3DVertexShader9_Release(&block->vertex_shader->iface.vertex);
    if (block->pixel_shader)
        IDirect3DPixelShader9_Release(&block->pixel_shader->iface.pixel);
    block->vertex_shader = NULL;
    block->pixel_shader = NULL;
    if (block->vertex_declaration)
        IDirect3DVertexDeclaration9_Release(&block->vertex_declaration->iface);
    block->vertex_declaration = NULL;
    for (index = 0; index < D9_MAX_STREAMS; ++index) {
        if (block->streams[index].buffer)
            IDirect3DVertexBuffer9_Release(&block->streams[index].buffer->iface);
        block->streams[index].buffer = NULL;
    }
    if (block->index_buffer)
        IDirect3DIndexBuffer9_Release(&block->index_buffer->iface);
    block->index_buffer = NULL;
}

/* Copies the live device value into every slot the mask already marks. This is
 * both Capture() and the second half of block creation, so "which states does
 * this block hold" is decided in exactly one place per block type. */
static void state_block_capture(struct D9StateBlock *block)
{
    D9Device *device = block->device;
    UINT index, stage, slot;

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
                IDirect3DTexture9_AddRef(&block->textures[stage]->iface);
            if (block->cube_textures[stage])
                IDirect3DCubeTexture9_AddRef(&block->cube_textures[stage]->iface);
            if (block->volume_textures[stage])
                IDirect3DVolumeTexture9_AddRef(
                        &block->volume_textures[stage]->iface);
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
                    device->transforms[g_state_block_transforms[index]],
                    sizeof(block->transforms[index]));
    }
    if (block->has_material) block->material = device->material;
    for (index = 0; index < D9_MAX_LIGHTS; ++index) {
        if (block->has_light[index]) block->lights[index] = device->lights[index];
        if (block->has_light_enable[index])
            block->light_enabled[index] = device->light_enabled[index];
    }
    if (block->has_viewport) block->viewport = device->viewport;
    if (block->has_scissor) block->scissor_rect = device->scissor_rect;
    if (block->has_vertex_shader) {
        block->vertex_shader = device->vertex_shader;
        if (block->vertex_shader)
            IDirect3DVertexShader9_AddRef(&block->vertex_shader->iface.vertex);
    }
    if (block->has_pixel_shader) {
        block->pixel_shader = device->pixel_shader;
        if (block->pixel_shader)
            IDirect3DPixelShader9_AddRef(&block->pixel_shader->iface.pixel);
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
            IDirect3DVertexDeclaration9_AddRef(
                    &block->vertex_declaration->iface);
    }
    for (index = 0; index < D9_MAX_STREAMS; ++index) {
        if (block->has_stream[index]) {
            block->streams[index] = device->streams[index];
            if (block->streams[index].buffer)
                IDirect3DVertexBuffer9_AddRef(
                        &block->streams[index].buffer->iface);
        }
    }
    if (block->has_indices) {
        block->index_buffer = device->index_buffer;
        if (block->index_buffer)
            IDirect3DIndexBuffer9_AddRef(&block->index_buffer->iface);
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
            /* TEXCOORDINDEX and TEXTURETRANSFORMFLAGS belong to vertex state:
             * they decide which coordinates a stage gets, not how it blends. */
            if (index == D3DTSS_TEXCOORDINDEX ||
                    index == D3DTSS_TEXTURETRANSFORMFLAGS)
                continue;
            block->has_texture_stage_state[stage][index] = TRUE;
        }
    }
    for (slot = 0; slot < D9_MAX_SAMPLERS; ++slot) {
        for (index = 0; index < D9_MAX_SAMPLER_STATES; ++index)
            block->has_sampler_state[slot][index] = TRUE;
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
    UINT index, stage;
    for (index = 0; index < sizeof(g_vertex_state_render_states) /
            sizeof(g_vertex_state_render_states[0]); ++index) {
        WORD state = g_vertex_state_render_states[index];
        if (state < D9_MAX_RENDER_STATES) block->has_render_state[state] = TRUE;
    }
    for (stage = 0; stage < D9_MAX_TEXTURE_STAGES; ++stage) {
        block->has_texture_stage_state[stage][D3DTSS_TEXCOORDINDEX] = TRUE;
        block->has_texture_stage_state[stage][D3DTSS_TEXTURETRANSFORMFLAGS] = TRUE;
    }
    block->has_material = TRUE;
    for (index = 0; index < D9_MAX_LIGHTS; ++index) {
        block->has_light[index] = device_light_is_set(block->device, index);
        block->has_light_enable[index] = TRUE;
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
    block->has_viewport = TRUE;
    block->has_scissor = TRUE;
    for (index = 0; index < D9_MAX_STREAMS; ++index)
        block->has_stream[index] = TRUE;
    block->has_indices = TRUE;
}

static struct D9StateBlock *state_block_alloc(D9Device *device)
{
    struct D9StateBlock *block = (struct D9StateBlock *)HeapAlloc(
            GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*block));
    if (!block)
        return NULL;
    block->iface.lpVtbl = &g_state_block_vtbl;
    block->refcount = 1;
    block->device = device;
    device_child_add_ref(device);
    block->next_device_resource = device->state_blocks;
    device->state_blocks = block;
    return block;
}

/* Replays every captured entry through the public setters, so a state block can
 * never apply a state by a route the app's own equivalent call would not take. */
static HRESULT state_block_apply(struct D9StateBlock *block)
{
    D9Device *device = block->device;
    IDirect3DDevice9 *iface = &device->iface;
    UINT index, stage, slot;

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
                    (D3DTRANSFORMSTATETYPE)g_state_block_transforms[index],
                    (const D3DMATRIX *)block->transforms[index]);
    }
    if (block->has_material)
        IDirect3DDevice9_SetMaterial(iface, &block->material);
    for (index = 0; index < D9_MAX_LIGHTS; ++index) {
        if (block->has_light[index])
            IDirect3DDevice9_SetLight(iface, index, &block->lights[index]);
        if (block->has_light_enable[index])
            IDirect3DDevice9_LightEnable(iface, index,
                    block->light_enabled[index]);
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
            IDirect3DDevice9_SetStreamSource(iface, index,
                    block->streams[index].buffer
                        ? &block->streams[index].buffer->iface : NULL,
                    block->streams[index].offset, block->streams[index].stride);
            IDirect3DDevice9_SetStreamSourceFreq(iface, index,
                    block->streams[index].frequency);
        }
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
        device_child_release(block->device);
        HeapFree(GetProcessHeap(), 0, block);
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
    state_block_capture(state_block_from_iface(iface));
    return D3D_OK;
}

static HRESULT WINAPI state_block_apply_method(IDirect3DStateBlock9 *iface)
{
    struct D9StateBlock *block = state_block_from_iface(iface);
    if (block->device->recording)
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
    if (device->recording)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (type != D3DSBT_ALL && type != D3DSBT_PIXELSTATE
            && type != D3DSBT_VERTEXSTATE)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    block = state_block_alloc(device);
    if (!block)
        return TRACE_REFUSE(E_OUTOFMEMORY);
    if (type == D3DSBT_ALL) state_block_mark_all(block);
    else if (type == D3DSBT_PIXELSTATE) state_block_mark_pixel_states(block);
    else state_block_mark_vertex_states(block);
    state_block_capture(block);
    *block_out = &block->iface;
    return D3D_OK;
}

static HRESULT WINAPI device_begin_state_block(IDirect3DDevice9 *iface)
{
    D9Device *device = device_from_iface(iface);
    struct D9StateBlock *baseline;
    struct D9StateBlock *result;

    if (device->recording)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    baseline = state_block_alloc(device);
    if (!baseline)
        return TRACE_REFUSE(E_OUTOFMEMORY);
    result = state_block_alloc(device);
    if (!result) {
        IDirect3DStateBlock9_Release(&baseline->iface);
        return TRACE_REFUSE(E_OUTOFMEMORY);
    }
    state_block_mark_all(baseline);
    state_block_capture(baseline);
    device->recording = baseline;
    device->recording_result = result;
    return D3D_OK;
}

static HRESULT WINAPI device_end_state_block(IDirect3DDevice9 *iface,
        IDirect3DStateBlock9 **block_out)
{
    D9Device *device = device_from_iface(iface);
    struct D9StateBlock *baseline = device->recording;
    struct D9StateBlock *block = device->recording_result;

    if (!baseline || !block)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!block_out) {
        device->recording = NULL;
        device->recording_result = NULL;
        state_block_apply(baseline);
        IDirect3DStateBlock9_Release(&block->iface);
        IDirect3DStateBlock9_Release(&baseline->iface);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    *block_out = NULL;
    /* Capture only the explicitly touched mask. This preserves same-value and
     * write-then-revert calls which a final-value diff necessarily loses. */
    state_block_capture(block);
    device->recording = NULL;
    device->recording_result = NULL;
    /* Put the device back the way the app left it before Begin. */
    state_block_apply(baseline);
    IDirect3DStateBlock9_Release(&baseline->iface);
    *block_out = &block->iface;
    return D3D_OK;
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
    return (ULONG)InterlockedIncrement(&vb_from_iface(iface)->refcount);
}

static ULONG WINAPI vb_release(IDirect3DVertexBuffer9 *iface)
{
    D9VertexBuffer *buffer = vb_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&buffer->refcount);
    if (!refs) {
        D9VertexBuffer **link = &buffer->device->vertex_buffers;
        D9WGDestroyResource destroy;
        while (*link && *link != buffer)
            link = &(*link)->next_device_resource;
        if (*link) *link = buffer->next_device_resource;
        destroy.resource_handle = buffer->handle;
        destroy.resource_kind = D9WG_RESOURCE_BUFFER_VERTEX;
        emit_command(D9WG_OP_DESTROY_RESOURCE, &destroy, sizeof(destroy));
        HeapFree(GetProcessHeap(), 0, buffer->shadow);
        device_child_release(buffer->device);
        HeapFree(GetProcessHeap(), 0, buffer);
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

static HRESULT WINAPI vb_set_private_data(IDirect3DVertexBuffer9 *iface,
        REFGUID guid, const void *data, DWORD size, DWORD flags)
{ (void)iface; (void)guid; (void)data; (void)size; (void)flags;
  return TRACE_REFUSE(D3DERR_INVALIDCALL); }

static HRESULT WINAPI vb_get_private_data(IDirect3DVertexBuffer9 *iface,
        REFGUID guid, void *data, DWORD *size)
{ (void)iface; (void)guid; (void)data; (void)size; return TRACE_REFUSE(D3DERR_NOTFOUND); }

static HRESULT WINAPI vb_free_private_data(IDirect3DVertexBuffer9 *iface,
        REFGUID guid)
{ (void)iface; (void)guid; return TRACE_REFUSE(D3DERR_NOTFOUND); }

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
    if ((flags & D3DLOCK_DISCARD) && (flags & D3DLOCK_NOOVERWRITE))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if ((flags & (D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE))
            && !(buffer->usage & D3DUSAGE_DYNAMIC))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if ((flags & D3DLOCK_READONLY)
            && (flags & (D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE)))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!size)
        size = buffer->length - offset;
    if (size > buffer->length - offset)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    buffer->locked = TRUE;
    buffer->lock_offset = offset;
    buffer->lock_size = size;
    buffer->lock_flags = flags;
    if (flags & D3DLOCK_DISCARD)
        ZeroMemory(buffer->shadow, buffer->length);
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
    return (ULONG)InterlockedIncrement(&ib_from_iface(iface)->refcount);
}

static ULONG WINAPI ib_release(IDirect3DIndexBuffer9 *iface)
{
    D9IndexBuffer *buffer = ib_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&buffer->refcount);
    if (!refs) {
        D9IndexBuffer **link = &buffer->device->index_buffers;
        D9WGDestroyResource destroy;
        while (*link && *link != buffer)
            link = &(*link)->next_device_resource;
        if (*link) *link = buffer->next_device_resource;
        destroy.resource_handle = buffer->handle;
        destroy.resource_kind = D9WG_RESOURCE_BUFFER_INDEX;
        emit_command(D9WG_OP_DESTROY_RESOURCE, &destroy, sizeof(destroy));
        HeapFree(GetProcessHeap(), 0, buffer->shadow);
        device_child_release(buffer->device);
        HeapFree(GetProcessHeap(), 0, buffer);
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

static HRESULT WINAPI ib_set_private_data(IDirect3DIndexBuffer9 *iface,
        REFGUID guid, const void *data, DWORD size, DWORD flags)
{ (void)iface; (void)guid; (void)data; (void)size; (void)flags;
  return TRACE_REFUSE(D3DERR_INVALIDCALL); }

static HRESULT WINAPI ib_get_private_data(IDirect3DIndexBuffer9 *iface,
        REFGUID guid, void *data, DWORD *size)
{ (void)iface; (void)guid; (void)data; (void)size; return TRACE_REFUSE(D3DERR_NOTFOUND); }

static HRESULT WINAPI ib_free_private_data(IDirect3DIndexBuffer9 *iface,
        REFGUID guid)
{ (void)iface; (void)guid; return TRACE_REFUSE(D3DERR_NOTFOUND); }

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
    if ((flags & D3DLOCK_DISCARD) && (flags & D3DLOCK_NOOVERWRITE))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if ((flags & (D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE))
            && !(buffer->usage & D3DUSAGE_DYNAMIC))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!size)
        size = buffer->length - offset;
    if (size > buffer->length - offset)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    buffer->locked = TRUE;
    buffer->lock_offset = offset;
    buffer->lock_size = size;
    buffer->lock_flags = flags;
    if (flags & D3DLOCK_DISCARD)
        ZeroMemory(buffer->shadow, buffer->length);
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
            || !texture_format_layout(texture->format, &block_width,
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
    return (ULONG)InterlockedIncrement(&texture_from_iface(iface)->refcount);
}

static ULONG WINAPI texture_release(IDirect3DTexture9 *iface)
{
    D9Texture *texture = texture_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&texture->refcount);
    if (!refs) {
        D9Texture **link = &texture->device->texture_resources;
        D9WGDestroyResource destroy;
        UINT level;
        while (*link && *link != texture)
            link = &(*link)->next_device_resource;
        if (*link) *link = texture->next_device_resource;
        destroy.resource_handle = texture->handle;
        destroy.resource_kind = D9WG_RESOURCE_TEXTURE_2D;
        emit_command(D9WG_OP_DESTROY_RESOURCE, &destroy, sizeof(destroy));
        for (level = 0; level < texture->level_count; ++level) {
            /* The level's surface sub-object dies with the texture; nothing
             * can still hold it, because every surface reference is one of the
             * texture references that just reached zero. */
            if (texture->levels[level].level_surface) {
                TRACE_SURFACE_FREED(
                        &texture->levels[level].level_surface->iface);
                HeapFree(GetProcessHeap(), 0,
                        texture->levels[level].level_surface);
                texture->levels[level].level_surface = NULL;
            }
            HeapFree(GetProcessHeap(), 0, texture->levels[level].shadow);
        }
        HeapFree(GetProcessHeap(), 0, texture->levels);
        device_child_release(texture->device);
        HeapFree(GetProcessHeap(), 0, texture);
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

static HRESULT WINAPI texture_set_private_data(IDirect3DTexture9 *iface,
        REFGUID guid, const void *data, DWORD size, DWORD flags)
{ (void)iface; (void)guid; (void)data; (void)size; (void)flags;
  return TRACE_REFUSE(D3DERR_INVALIDCALL); }

static HRESULT WINAPI texture_get_private_data(IDirect3DTexture9 *iface,
        REFGUID guid, void *data, DWORD *size)
{ (void)iface; (void)guid; (void)data; (void)size; return TRACE_REFUSE(D3DERR_NOTFOUND); }

static HRESULT WINAPI texture_free_private_data(IDirect3DTexture9 *iface,
        REFGUID guid)
{ (void)iface; (void)guid; return TRACE_REFUSE(D3DERR_NOTFOUND); }

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
    DWORD old = texture->lod;
    if (texture->pool != D3DPOOL_MANAGED)
        return old;
    if (lod >= texture->level_count)
        lod = texture->level_count - 1;
    texture->lod = lod;
    emit_texture_lod(texture->device, texture->handle, lod);
    return old;
}

static DWORD WINAPI texture_get_lod(IDirect3DTexture9 *iface)
{ return texture_from_iface(iface)->lod; }

static DWORD WINAPI texture_get_level_count(IDirect3DTexture9 *iface)
{ return texture_from_iface(iface)->level_count; }

/* Auto mipmap generation is not implemented in M1 (CreateTexture already
 * rejects D3DUSAGE_AUTOGENMIPMAP); these three exist only to keep the vtable
 * complete for titles that probe the capability defensively. */
static BOOL emit_generate_mips(D9Device *device, uint32_t resource_handle)
{
    D9WGGenerateMips command;
    command.device_handle = device->handle;
    command.resource_handle = resource_handle;
    return emit_command(D9WG_OP_GENERATE_MIPS, &command, sizeof(command));
}

static BOOL emit_texture_lod(D9Device *device, uint32_t resource_handle,
        DWORD lod)
{
    D9WGSetTextureLOD command;
    command.device_handle = device->handle;
    command.resource_handle = resource_handle;
    command.lod = (uint32_t)lod;
    command.reserved = 0;
    return emit_command(D9WG_OP_SET_TEXTURE_LOD, &command, sizeof(command));
}

/*
 * D3D9 exposes the filter used for automatic mip generation, and defaults it to
 * D3DTEXF_LINEAR. Reporting D3DTEXF_NONE was consistent while nothing generated
 * anything; now that the chain is real, the getter has to describe what the
 * host actually does -- a linear box downsample per level.
 *
 * The setter accepts the filters that describe that and refuses the ones it
 * would have to silently substitute for. D3D9's own contract here is that an
 * unsupported filter returns D3DERR_INVALIDCALL, so refusing is the documented
 * answer rather than an approximation.
 */
static HRESULT WINAPI texture_set_auto_gen_filter_type(
        IDirect3DTexture9 *iface, D3DTEXTUREFILTERTYPE filter)
{
    D9Texture *texture = texture_from_iface(iface);
    if (!(texture->usage & D3DUSAGE_AUTOGENMIPMAP))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (filter != D3DTEXF_POINT && filter != D3DTEXF_LINEAR
            && filter != D3DTEXF_NONE)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
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
    emit_generate_mips(texture->device, texture->handle);
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
    return (ULONG)InterlockedIncrement(&decl_from_iface(iface)->refcount);
}

static ULONG WINAPI decl_release(IDirect3DVertexDeclaration9 *iface)
{
    D9VertexDeclaration *decl = decl_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&decl->refcount);
    if (!refs) {
        D9VertexDeclaration **link = &decl->device->vertex_declarations;
        D9WGDestroyResource destroy;
        while (*link && *link != decl)
            link = &(*link)->next_device_resource;
        if (*link) *link = decl->next_device_resource;
        destroy.resource_handle = decl->handle;
        destroy.resource_kind = D9WG_RESOURCE_VERTEX_DECLARATION;
        emit_command(D9WG_OP_DESTROY_RESOURCE, &destroy, sizeof(destroy));
        device_child_release(decl->device);
        HeapFree(GetProcessHeap(), 0, decl);
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
    InterlockedIncrement(&shader->refcount);
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
    return (ULONG)InterlockedIncrement(
            &vertex_shader_from_iface(iface)->refcount);
}

static ULONG WINAPI vertex_shader_release(IDirect3DVertexShader9 *iface)
{
    D9Shader *shader = vertex_shader_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&shader->refcount);
    if (!refs)
        shader_destroy(shader);
    return refs;
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
    return (ULONG)InterlockedIncrement(
            &pixel_shader_from_iface(iface)->refcount);
}

static ULONG WINAPI pixel_shader_release(IDirect3DPixelShader9 *iface)
{
    D9Shader *shader = pixel_shader_from_iface(iface);
    ULONG refs = (ULONG)InterlockedDecrement(&shader->refcount);
    if (!refs)
        shader_destroy(shader);
    return refs;
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
     * device in the same way, so each public reference on them is backed by one
     * device reference -- that is what stops a device from being torn down
     * while the app still holds one of its implicit surfaces. */
    if (surface_is_implicit(surface)) {
        TRACE_MARK_ENTER("Surface.AddRef");
        refs = (ULONG)InterlockedIncrement(&surface->refcount);
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

static ULONG WINAPI surface_release(IDirect3DSurface9 *iface)
{
    D9Surface *surface = surface_from_iface(iface);
    LONG before = 0;
    ULONG refs;

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
        refs = (ULONG)InterlockedDecrement(&surface->refcount);
        TRACE("SURFACE RELEASE object=%08lX implicit=1 after=%lu device=%08lX "
                "device_refs_before=%ld", (DWORD)(uintptr_t)iface, refs,
                (DWORD)(uintptr_t)device,
                InterlockedCompareExchange(&device->refcount, 0, 0));
        TRACE_MARK_EXIT("Surface.Release", (HRESULT)refs, iface);
        /* This can destroy the device, which frees `surface`; touch neither
         * object afterwards. */
        device_child_release(device);
        return refs;
    }
#ifdef D9WG_DIAGNOSTIC_TRACE
    before = InterlockedCompareExchange(&surface->refcount, 0, 0);
#endif
    TRACE_MARK_ENTER("Surface.Release");
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
        device_child_release(device);
        TRACE("SURFACE FREE_END object=%08lX", (DWORD)(uintptr_t)iface);
        TRACE_MARK_EXIT("Surface.Release", 0, iface);
        TRACE_SURFACE_FREED(iface);
        HeapFree(GetProcessHeap(), 0, surface);
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

static HRESULT WINAPI surface_set_private_data(IDirect3DSurface9 *iface,
        REFGUID guid, const void *data, DWORD size, DWORD flags)
{ (void)iface; (void)guid; (void)data; (void)size; (void)flags;
  return TRACE_REFUSE(D3DERR_INVALIDCALL); }

static HRESULT WINAPI surface_get_private_data(IDirect3DSurface9 *iface,
        REFGUID guid, void *data, DWORD *size)
{ (void)iface; (void)guid; (void)data; (void)size; return TRACE_REFUSE(D3DERR_NOTFOUND); }

static HRESULT WINAPI surface_free_private_data(IDirect3DSurface9 *iface,
        REFGUID guid)
{ (void)iface; (void)guid; return TRACE_REFUSE(D3DERR_NOTFOUND); }

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
 * render target/back buffer remains non-lockable; callers use the implemented
 * GetRenderTargetData path to transfer its GPU contents first.
 */
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
                || !texture_format_layout(surface->format, &block_width,
                    &block_height, &block_bytes)
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
    if (surface->shadow) {
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

static ULONG WINAPI swap_chain_add_ref(IDirect3DSwapChain9 *iface)
{
    D9SwapChain *swap_chain = swap_chain_from_iface(iface);
    ULONG refs;

    TRACE_MARK_ENTER("SwapChain.AddRef");
    /* A live swap-chain pointer must keep its embedded storage alive. */
    IDirect3DDevice9_AddRef(&swap_chain->device->iface);
    refs = (ULONG)InterlockedIncrement(&swap_chain->refcount);
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
    ULONG device_refs;
    ULONG refs;

    TRACE_MARK_ENTER("SwapChain.Release");
    if (swap_chain->handle) {
        /* An additional chain is an ordinary heap object with its own
         * lifetime, unlike the implicit one, which is a member of the device
         * and whose refcount is really the device's. */
        refs = (ULONG)InterlockedDecrement(&swap_chain->refcount);
        if (!refs) {
            D9SwapChain **link = &device->additional_swap_chains;
            D9WGDestroySwapChain destroy;
            while (*link && *link != swap_chain)
                link = &(*link)->next_device_chain;
            if (*link) *link = swap_chain->next_device_chain;
            destroy.device_handle = device->handle;
            destroy.swap_chain_handle = swap_chain->handle;
            emit_command(D9WG_OP_DESTROY_SWAP_CHAIN, &destroy,
                    sizeof(destroy));
            if (swap_chain->back_buffer)
                IDirect3DSurface9_Release(swap_chain->back_buffer);
            HeapFree(GetProcessHeap(), 0, swap_chain);
            device_child_release(device);
        }
        TRACE_MARK_EXIT("SwapChain.Release", (HRESULT)refs, iface);
        return refs;
    }
    refs = (ULONG)InterlockedDecrement(&swap_chain->refcount);
    TRACE("CALL SwapChain.Release object=%08lX refs=%lu device_refs_before=%ld",
            (DWORD)(uintptr_t)iface, refs,
            InterlockedCompareExchange(&device->refcount, 0, 0));
    /* This can free device (and therefore swap_chain); do not touch either
     * object after the call. */
    device_refs = IDirect3DDevice9_Release(&device->iface);
    TRACE_MARK_EXIT("SwapChain.Release", (HRESULT)refs, NULL);
    TRACE("LEAVE SwapChain.Release object=%08lX refs=%lu device_refs=%lu",
            (DWORD)(uintptr_t)iface, refs, device_refs);
    (void)device_refs;
    return refs;
}

static HRESULT WINAPI swap_chain_present(IDirect3DSwapChain9 *iface,
        const RECT *src_rect, const RECT *dst_rect, HWND override_window,
        const RGNDATA *dirty_region, DWORD flags)
{
    D9SwapChain *swap_chain = swap_chain_from_iface(iface);
    HRESULT result;

    TRACE_MARK_ENTER("SwapChain.Present");
    TRACE("CALL SwapChain.Present object=%08lX flags=%08lX override=%08lX",
            (DWORD)(uintptr_t)iface, flags,
            (DWORD)(uintptr_t)override_window);
    (void)flags;
    (void)src_rect;
    (void)dst_rect;
    (void)dirty_region;
    /* The bridge presents synchronously, so DONOTWAIT cannot make it return
     * WASSTILLDRAWING.  LINEAR_CONTENT does not change the guest-side work;
     * both flags can otherwise use the device's normal presentation path. */
    if (swap_chain->handle) {
        result = emit_present_swap_chain_and_flush(swap_chain, override_window)
                ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
    } else {
        result = device_present(&swap_chain->device->iface, src_rect, dst_rect,
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
    if (swap_chain->handle) {
        /* An additional chain owns a real surface rather than borrowing the
         * device's implicit one. */
        if (!out) {
            result = D3DERR_INVALIDCALL;
        } else if (index || type != D3DBACKBUFFER_TYPE_MONO) {
            *out = NULL;
            result = D3DERR_INVALIDCALL;
        } else {
            *out = swap_chain->back_buffer;
            IDirect3DSurface9_AddRef(*out);
            result = D3D_OK;
        }
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

static HRESULT WINAPI swap_chain_get_display_mode(IDirect3DSwapChain9 *iface,
        D3DDISPLAYMODE *mode)
{
    D9SwapChain *swap_chain = swap_chain_from_iface(iface);

    TRACE_MARK_ENTER("SwapChain.GetDisplayMode");
    if (!mode) {
        TRACE("FAIL SwapChain.GetDisplayMode object=%08lX missing output -> %08lX",
                (DWORD)(uintptr_t)iface, (DWORD)D3DERR_INVALIDCALL);
        TRACE_MARK_EXIT("SwapChain.GetDisplayMode", D3DERR_INVALIDCALL, NULL);
        return D3DERR_INVALIDCALL;
    }
    *mode = swap_chain->device->display_mode;
    TRACE("OK SwapChain.GetDisplayMode object=%08lX %lux%lu fmt=%08lX refresh=%lu",
            (DWORD)(uintptr_t)iface, mode->Width, mode->Height,
            (DWORD)mode->Format, mode->RefreshRate);
    TRACE_MARK_EXIT("SwapChain.GetDisplayMode", D3D_OK, mode);
    return D3D_OK;
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

IDirect3D9 *WINAPI Direct3DCreate9(UINT sdk_version)
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
    emit_hello_once();
    TRACE("OK Direct3DCreate9 object=%08lX", (DWORD)(uintptr_t)&d3d->iface);
    return &d3d->iface;
}

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

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)reserved;
    if (reason == DLL_PROCESS_ATTACH) {
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
        initialize_session_id(instance);
        InitializeCriticalSection(&g_transport_lock);
        InitializeCriticalSection(&g_readback_lock);
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
        if (g_command_count)
            submit_batch_locked(FALSE);
        close_transport_locked();
        LeaveCriticalSection(&g_transport_lock);
        DeleteCriticalSection(&g_transport_lock);
        DeleteCriticalSection(&g_readback_lock);
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
