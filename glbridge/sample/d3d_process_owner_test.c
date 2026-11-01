/* XP test: app-local D3D9/D3D8/DDraw share one process transport. No rendering
 * or adapter capability result is mocked by this executable. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../runtime/d9wg_process.h"

typedef void *(WINAPI *CreateD3D)(UINT);
typedef HRESULT (WINAPI *CreateDDraw)(void *, void **, void *);
typedef ULONG (WINAPI *ReleaseObject)(void *);
static HANDLE log_file;
static char directory[MAX_PATH];

static void report(const char *text)
{
    DWORD written;
    WriteFile(log_file, text, (DWORD)lstrlenA(text), &written, NULL);
    WriteFile(log_file, "\r\n", 2, &written, NULL);
    FlushFileBuffers(log_file);
}

static void check(BOOL condition, const char *stage)
{
    if (!condition) {
        char message[192];
        wsprintfA(message, "FAIL %s error=%lu", stage, GetLastError());
        report(message);ExitProcess(1);
    }
}

static HMODULE load(const char *name)
{
    char path[MAX_PATH];
    HMODULE module;
    lstrcpyA(path, directory);lstrcatA(path, name);
    module = LoadLibraryA(path);check(module != NULL, name);return module;
}

static void release_object(void *object)
{
    void **vtable = *(void ***)object;
    ((ReleaseObject)(uintptr_t)vtable[2])(object);
}

static void run_order(BOOL draw_first)
{
    char name[] = "D9WG.Process.v1.00000000";
    const char hex[] = "0123456789ABCDEF";
    HMODULE nine, eight, draw;
    HANDLE mapping, transport;
    D9WGProcess *state;
    CreateD3D create_nine, create_eight;
    CreateDDraw create_draw;
    void *object_nine, *object_eight, *object_draw;
    DWORD flags, pid = GetCurrentProcessId();
    UINT index;
    if (draw_first) {
        draw = load("DDRAW.DLL");nine = load("D3D9.DLL");eight = load("D3D8.DLL");
    } else {
        nine = load("D3D9.DLL");eight = load("D3D8.DLL");draw = load("DDRAW.DLL");
    }
    for (index = 0; index < 8; ++index)
        name[sizeof(name) - 2u - index] = hex[(pid >> (index * 4u)) & 15u];
    mapping = OpenFileMappingA(FILE_MAP_READ, FALSE, name);check(mapping != NULL, "open shared metadata");
    state = MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(*state));check(state != NULL, "map shared metadata");
    check(state->references == 3 && state->bytes == sizeof(*state), "three frontend references");
    create_nine = (CreateD3D)(uintptr_t)GetProcAddress(nine, "Direct3DCreate9");
    create_eight = (CreateD3D)(uintptr_t)GetProcAddress(eight, "Direct3DCreate8");
    create_draw = (CreateDDraw)(uintptr_t)GetProcAddress(draw, "DirectDrawCreate");
    check(create_nine && create_eight && create_draw, "factory exports");
    object_nine = create_nine(32);check(object_nine != NULL, "D3D9 factory");
    object_eight = create_eight(220);check(object_eight != NULL, "D3D8 factory");
    check(SUCCEEDED(create_draw(NULL, &object_draw, NULL)) && object_draw != NULL, "DirectDraw factory");
    transport = state->transport;
    check(state->transport_generation == 1 && state->dma_buffer && transport != INVALID_HANDLE_VALUE && GetHandleInformation(transport, &flags), "one live transport");
    if (draw_first) {
        release_object(object_draw);check(FreeLibrary(draw), "unload creator DirectDraw");
    } else {
        release_object(object_nine);check(FreeLibrary(nine), "unload creator D3D9");
    }
    check(state->references == 2 && state->transport == transport && state->dma_buffer &&
            GetHandleInformation(transport, &flags), "creator unload preserves transport");
    release_object(object_eight);check(FreeLibrary(eight), "unload D3D8");
    check(state->references == 1 && state->transport == transport && GetHandleInformation(transport, &flags), "second unload preserves transport");
    if (draw_first) { release_object(object_nine);check(FreeLibrary(nine), "unload last D3D9"); }
    else { release_object(object_draw);check(FreeLibrary(draw), "unload last DirectDraw"); }
    check(!state->references && !state->canonical && !state->dma_buffer && state->transport == INVALID_HANDLE_VALUE,
            "last unload closes owner");
    UnmapViewOfFile(state);CloseHandle(mapping);
    report(draw_first ? "PASS DirectDraw creator unload order" : "PASS D3D9 creator unload order");
}

void WINAPI WinMainCRTStartup(void)
{
    char path[MAX_PATH];
    UINT index;
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    index = (UINT)lstrlenA(directory);
    while (index && directory[index - 1] != '\\') --index;
    directory[index] = 0;
    lstrcpyA(path, directory);lstrcatA(path, "OWNER.TXT");
    log_file = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (log_file == INVALID_HANDLE_VALUE) ExitProcess(2);
    report("READY real XP process owner probe");
    run_order(FALSE);run_order(TRUE);
    report("PASS all real XP frontend owner checks");
    CloseHandle(log_file);ExitProcess(0);
}
