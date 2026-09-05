/*
 * Shared, CRT-free diagnostic trace used by the legacy DirectX proxies.
 *
 * Include this only after windows.h and after defining both
 * V86WG_DIAGNOSTIC_COMPONENT and V86WG_DIAGNOSTIC_FILE_STEM.  Everything is
 * TU-local so each proxy gets its own trace state without adding another
 * object file to the deliberately tiny XP build.
 */
#ifndef V86WG_DIAGNOSTIC_TRACE_H
#define V86WG_DIAGNOSTIC_TRACE_H

#include <stdarg.h>

#ifndef V86WG_DIAGNOSTIC_COMPONENT
#error V86WG_DIAGNOSTIC_COMPONENT must name the proxy being traced
#endif
#ifndef V86WG_DIAGNOSTIC_FILE_STEM
#error V86WG_DIAGNOSTIC_FILE_STEM must name the trace file prefix
#endif

static HANDLE g_v86wg_trace_file = INVALID_HANDLE_VALUE;
static PVOID g_v86wg_trace_veh;
static volatile LONG g_v86wg_trace_in_exception;
static volatile LONG g_v86wg_trace_sequence;
static volatile LONG g_v86wg_trace_last_hr;
static volatile LONG g_v86wg_trace_last_line;
static const char *g_v86wg_trace_last_function = "none";
static HMODULE g_v86wg_trace_exe_module;
static HMODULE g_v86wg_trace_self_module;
static char g_v86wg_trace_exe_path[MAX_PATH];
static char g_v86wg_trace_self_path[MAX_PATH];
static char g_v86wg_trace_path[MAX_PATH];

/* OutputDebugString raises these first-chance exceptions when no debugger
 * consumes the string. They describe the tracing mechanism itself, not a
 * fault in the application, and recording them recursively drowns out the
 * DirectDraw call that matters. */
#ifndef DBG_PRINTEXCEPTION_C
#define DBG_PRINTEXCEPTION_C ((DWORD)0x40010006L)
#endif
#ifndef DBG_PRINTEXCEPTION_WIDE_C
#define DBG_PRINTEXCEPTION_WIDE_C ((DWORD)0x4001000AL)
#endif

static BOOL v86wg_diagnostic_directory_from_path(const char *path,
        char *directory)
{
    int index;
    int cut = -1;

    if (!path || !path[0])
        return FALSE;
    lstrcpynA(directory, path, MAX_PATH);
    directory[MAX_PATH - 1] = 0;
    for (index = 0; directory[index]; ++index) {
        if (directory[index] == '\\' || directory[index] == '/')
            cut = index;
    }
    if (cut < 0)
        return FALSE;
    directory[cut + 1] = 0;
    return TRUE;
}

static BOOL v86wg_diagnostic_try_directory(const char *directory)
{
    if (lstrlenA(directory) + (int)sizeof(V86WG_DIAGNOSTIC_FILE_STEM
            "_4294967295.log") > MAX_PATH)
        return FALSE;
    wsprintfA(g_v86wg_trace_path, "%s%s_%lu.log", directory,
            V86WG_DIAGNOSTIC_FILE_STEM, GetCurrentProcessId());
    /* A process may LoadLibrary/FreeLibrary the proxy several times. Keep all
     * attaches for the PID in one chronological file instead of letting the
     * later attach erase the call sequence that led to it. FILE_APPEND_DATA
     * also keeps every WriteFile at EOF if another reader opens the trace. */
    g_v86wg_trace_file = CreateFileA(g_v86wg_trace_path, FILE_APPEND_DATA,
            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS,
            FILE_ATTRIBUTE_NORMAL, NULL);
    return g_v86wg_trace_file != INVALID_HANDLE_VALUE;
}

static void v86wg_diagnostic_open(HINSTANCE instance)
{
    char directory[MAX_PATH];
    DWORD length;

    g_v86wg_trace_self_module = (HMODULE)instance;
    g_v86wg_trace_exe_module = GetModuleHandleA(NULL);
    ZeroMemory(g_v86wg_trace_exe_path, sizeof(g_v86wg_trace_exe_path));
    ZeroMemory(g_v86wg_trace_self_path, sizeof(g_v86wg_trace_self_path));
    ZeroMemory(g_v86wg_trace_path, sizeof(g_v86wg_trace_path));
    GetModuleFileNameA(g_v86wg_trace_exe_module, g_v86wg_trace_exe_path,
            MAX_PATH - 1);
    GetModuleFileNameA(g_v86wg_trace_self_module, g_v86wg_trace_self_path,
            MAX_PATH - 1);

    /* The executable directory is the game directory even if a launcher or a
     * compatibility layer loaded the proxy from somewhere else.  Put the
     * trace there first so it is discoverable beside the program the user is
     * testing, then try the DLL directory and finally the guest temp folder. */
    if (v86wg_diagnostic_directory_from_path(g_v86wg_trace_exe_path,
            directory) && v86wg_diagnostic_try_directory(directory))
        return;
    if (v86wg_diagnostic_directory_from_path(g_v86wg_trace_self_path,
            directory) && v86wg_diagnostic_try_directory(directory))
        return;

    /* Both application locations may be read-only. Keep diagnostics usable by falling
     * back to the guest's temporary directory rather than silently tracing
     * nowhere. */
    length = GetTempPathA(MAX_PATH, directory);
    if (!length || length >= MAX_PATH)
        return;
    v86wg_diagnostic_try_directory(directory);
}

static void v86wg_diagnostic_write(const char *format, ...)
{
    char message[768];
    char line[896];
    DWORD written;
    va_list arguments;

    if (g_v86wg_trace_file == INVALID_HANDLE_VALUE)
        return;
    va_start(arguments, format);
    wvsprintfA(message, format, arguments);
    va_end(arguments);
    wsprintfA(line, "[%08lX %lu:%lu #%ld] %s\r\n", GetTickCount(),
            GetCurrentProcessId(), GetCurrentThreadId(),
            InterlockedIncrement(&g_v86wg_trace_sequence), message);
    WriteFile(g_v86wg_trace_file, line, (DWORD)lstrlenA(line), &written,
            NULL);
}

static void v86wg_diagnostic_memory(const char *reason)
{
    MEMORYSTATUS status;

    ZeroMemory(&status, sizeof(status));
    status.dwLength = sizeof(status);
    GlobalMemoryStatus(&status);
    v86wg_diagnostic_write("MEM reason=%s load=%lu%% phys=%luK/%luK "
            "page=%luK/%luK virtual=%luK/%luK", reason,
            status.dwMemoryLoad, (DWORD)(status.dwAvailPhys / 1024u),
            (DWORD)(status.dwTotalPhys / 1024u),
            (DWORD)(status.dwAvailPageFile / 1024u),
            (DWORD)(status.dwTotalPageFile / 1024u),
            (DWORD)(status.dwAvailVirtual / 1024u),
            (DWORD)(status.dwTotalVirtual / 1024u));
}

static void v86wg_diagnostic_flush(void)
{
    if (g_v86wg_trace_file != INVALID_HANDLE_VALUE)
        FlushFileBuffers(g_v86wg_trace_file);
}

/* Unlike v86wg_diagnostic_hresult, this records successful returns too. It is
 * used only on the high-value display path so the diagnostic DLL remains
 * usable with a real game while still showing exactly where dxdiag stopped. */
static HRESULT __attribute__((unused)) v86wg_diagnostic_result(HRESULT result,
        const char *function, int line)
{
    DWORD win32_error = GetLastError();

    if (FAILED(result)) {
        g_v86wg_trace_last_function = function;
        InterlockedExchange(&g_v86wg_trace_last_line, line);
        InterlockedExchange(&g_v86wg_trace_last_hr, (LONG)result);
    }
    v86wg_diagnostic_write("RESULT function=%s line=%d hr=%08lX "
            "win32_last=%lu", function, line, (DWORD)result, win32_error);
    v86wg_diagnostic_flush();
    return result;
}

/* Not every proxy using this header exposes HRESULT-returning entry points. */
static HRESULT __attribute__((unused)) v86wg_diagnostic_hresult(HRESULT result,
        const char *function, int line)
{
    DWORD win32_error;

    if (SUCCEEDED(result))
        return result;
    win32_error = GetLastError();
    g_v86wg_trace_last_function = function;
    InterlockedExchange(&g_v86wg_trace_last_line, line);
    InterlockedExchange(&g_v86wg_trace_last_hr, (LONG)result);
    v86wg_diagnostic_write("HRESULT function=%s line=%d hr=%08lX "
            "win32_last=%lu", function, line, (DWORD)result, win32_error);
    v86wg_diagnostic_flush();
    return result;
}

static LONG WINAPI v86wg_diagnostic_exception(PEXCEPTION_POINTERS pointers)
{
    EXCEPTION_RECORD *record;
    CONTEXT *context;
    ULONG_PTR parameter0 = 0;
    ULONG_PTR parameter1 = 0;

    if (!pointers || !pointers->ExceptionRecord)
        return EXCEPTION_CONTINUE_SEARCH;
    if (pointers->ExceptionRecord->ExceptionCode == DBG_PRINTEXCEPTION_C ||
            pointers->ExceptionRecord->ExceptionCode ==
                    DBG_PRINTEXCEPTION_WIDE_C)
        return EXCEPTION_CONTINUE_SEARCH;
    if (InterlockedCompareExchange(&g_v86wg_trace_in_exception, 1, 0))
        return EXCEPTION_CONTINUE_SEARCH;
    record = pointers->ExceptionRecord;
    context = pointers->ContextRecord;
    if (record->NumberParameters > 0)
        parameter0 = record->ExceptionInformation[0];
    if (record->NumberParameters > 1)
        parameter1 = record->ExceptionInformation[1];
#if defined(__i386__) || defined(_M_IX86)
    if (context) {
        v86wg_diagnostic_write("EXCEPTION code=%08lX flags=%08lX "
                "address=%08lX params=%lu p0=%08lX p1=%08lX eip=%08lX "
                "esp=%08lX ebp=%08lX eax=%08lX ebx=%08lX ecx=%08lX "
                "edx=%08lX esi=%08lX edi=%08lX", record->ExceptionCode,
                record->ExceptionFlags,
                (DWORD)(uintptr_t)record->ExceptionAddress,
                record->NumberParameters, (DWORD)parameter0,
                (DWORD)parameter1, context->Eip, context->Esp, context->Ebp,
                context->Eax, context->Ebx, context->Ecx, context->Edx,
                context->Esi, context->Edi);
    } else
#endif
    {
        v86wg_diagnostic_write("EXCEPTION code=%08lX flags=%08lX "
                "address=%08lX params=%lu p0=%08lX p1=%08lX",
                record->ExceptionCode, record->ExceptionFlags,
                (DWORD)(uintptr_t)record->ExceptionAddress,
                record->NumberParameters, (DWORD)parameter0,
                (DWORD)parameter1);
    }
    v86wg_diagnostic_write("LAST_HRESULT function=%s line=%ld hr=%08lX",
            g_v86wg_trace_last_function,
            InterlockedCompareExchange(&g_v86wg_trace_last_line, 0, 0),
            (DWORD)InterlockedCompareExchange(&g_v86wg_trace_last_hr, 0, 0));
    if (g_v86wg_trace_file != INVALID_HANDLE_VALUE)
        FlushFileBuffers(g_v86wg_trace_file);
    InterlockedExchange(&g_v86wg_trace_in_exception, 0);
    return EXCEPTION_CONTINUE_SEARCH;
}

static void v86wg_diagnostic_process_attach(HINSTANCE instance)
{
    v86wg_diagnostic_open(instance);
    g_v86wg_trace_veh = AddVectoredExceptionHandler(1,
            v86wg_diagnostic_exception);
    v86wg_diagnostic_write("PROCESS_ATTACH component=%s pid=%lu "
            "module=%08lX veh=%08lX trace=%s", V86WG_DIAGNOSTIC_COMPONENT,
            GetCurrentProcessId(), (DWORD)(uintptr_t)instance,
            (DWORD)(uintptr_t)g_v86wg_trace_veh, g_v86wg_trace_path);
    v86wg_diagnostic_write("MODULE exe=%08lX path=%s proxy=%08lX path=%s",
            (DWORD)(uintptr_t)g_v86wg_trace_exe_module,
            g_v86wg_trace_exe_path,
            (DWORD)(uintptr_t)g_v86wg_trace_self_module,
            g_v86wg_trace_self_path);
    v86wg_diagnostic_memory("process_attach");
    if (g_v86wg_trace_file != INVALID_HANDLE_VALUE)
        FlushFileBuffers(g_v86wg_trace_file);
}

static void v86wg_diagnostic_process_detach(LPVOID reserved,
        DWORD pending_commands)
{
    v86wg_diagnostic_write("PROCESS_DETACH reserved=%08lX "
            "pending_commands=%lu", (DWORD)(uintptr_t)reserved,
            pending_commands);
    v86wg_diagnostic_memory("process_detach");
    v86wg_diagnostic_write("LAST_HRESULT function=%s line=%ld hr=%08lX",
            g_v86wg_trace_last_function,
            InterlockedCompareExchange(&g_v86wg_trace_last_line, 0, 0),
            (DWORD)InterlockedCompareExchange(&g_v86wg_trace_last_hr, 0, 0));
    if (g_v86wg_trace_file != INVALID_HANDLE_VALUE)
        FlushFileBuffers(g_v86wg_trace_file);
    if (g_v86wg_trace_veh) {
        RemoveVectoredExceptionHandler(g_v86wg_trace_veh);
        g_v86wg_trace_veh = NULL;
    }
    if (g_v86wg_trace_file != INVALID_HANDLE_VALUE) {
        CloseHandle(g_v86wg_trace_file);
        g_v86wg_trace_file = INVALID_HANDLE_VALUE;
    }
}

#endif /* V86WG_DIAGNOSTIC_TRACE_H */
