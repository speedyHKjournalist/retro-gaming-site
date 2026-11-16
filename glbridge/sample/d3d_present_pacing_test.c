/* XP virtual Present pacing probe; no native HAL claim. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#if D9WG_PACING_API == 8
#include <d3d8.h>
typedef IDirect3D8 Factory;
typedef IDirect3DDevice8 Device;
typedef IDirect3DVertexBuffer8 VertexBuffer;
typedef IDirect3DIndexBuffer8 IndexBuffer;
#define VB_LOCK IDirect3DVertexBuffer8_Lock
#define IB_LOCK IDirect3DIndexBuffer8_Lock
#define VB_UNLOCK IDirect3DVertexBuffer8_Unlock
#define IB_UNLOCK IDirect3DIndexBuffer8_Unlock
#define VB_RELEASE IDirect3DVertexBuffer8_Release
#define IB_RELEASE IDirect3DIndexBuffer8_Release
#define DEV(method) IDirect3DDevice8_##method
#define FACTORY_RELEASE IDirect3D8_Release
#else
#include <d3d9.h>
typedef IDirect3D9 Factory;
typedef IDirect3DDevice9 Device;
typedef IDirect3DVertexBuffer9 VertexBuffer;
typedef IDirect3DIndexBuffer9 IndexBuffer;
#define VB_LOCK IDirect3DVertexBuffer9_Lock
#define IB_LOCK IDirect3DIndexBuffer9_Lock
#define VB_UNLOCK IDirect3DVertexBuffer9_Unlock
#define IB_UNLOCK IDirect3DIndexBuffer9_Unlock
#define VB_RELEASE IDirect3DVertexBuffer9_Release
#define IB_RELEASE IDirect3DIndexBuffer9_Release
#define DEV(method) IDirect3DDevice9_##method
#define FACTORY_RELEASE IDirect3D9_Release
#endif
static Factory* factory;
static Device* device;
static HANDLE logfile;
static void report(const char* text)
{
    DWORD written;
    WriteFile(logfile, text, lstrlenA(text), &written, NULL);
    WriteFile(logfile, "\r\n", 2, &written, NULL);
    FlushFileBuffers(logfile);
}
static void check(BOOL condition, const char* message)
{
    if (!condition) {
        report(message);
        ExitProcess(1);
    }
}

#define FRAMES 12u
static void pacing_cases(D3DPRESENT_PARAMETERS* pp)
{
    for (UINT mode = 0; mode < 5; mode++) {
        DWORD requested = mode == 2 ? D3DPRESENT_INTERVAL_ONE : mode == 3 ? D3DPRESENT_INTERVAL_IMMEDIATE
                                                                          : D3DPRESENT_INTERVAL_DEFAULT;
#if D9WG_PACING_API == 9
        if (mode == 4)
            requested = D3DPRESENT_INTERVAL_ONE;
#endif
        if (mode) {
            check(DEV(Release)(device) == 0, "FAIL prior device refs");
            pp->Windowed = mode == 4;
            pp->BackBufferWidth = mode == 4 ? 64 : 640;
            pp->BackBufferHeight = mode == 4 ? 64 : 480;
            pp->BackBufferCount = 1;
#if D9WG_PACING_API == 8
            pp->SwapEffect = mode == 4 ? D3DSWAPEFFECT_COPY_VSYNC : D3DSWAPEFFECT_DISCARD;
            pp->FullScreen_PresentationInterval = requested;
            check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, pp->hDeviceWindow, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, pp, &device) == D3D_OK, "FAIL paced device8");
#else
            pp->SwapEffect = mode == 4 ? D3DSWAPEFFECT_COPY : D3DSWAPEFFECT_DISCARD;
            pp->PresentationInterval = requested;
            check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, pp->hDeviceWindow, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, pp, &device) == D3D_OK, "FAIL paced device9");
#endif
        }
        DWORD effective = mode == 3 ? D3DPRESENT_INTERVAL_IMMEDIATE : mode == 2 || mode == 4 ? D3DPRESENT_INTERVAL_ONE
                                                                                             : D3DPRESENT_INTERVAL_DEFAULT;
#if D9WG_PACING_API == 8
        if (mode == 0)
            effective = D3DPRESENT_INTERVAL_IMMEDIATE;
#endif
        LARGE_INTEGER frequency, start_counter, end_counter;
        check(QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0 && frequency.HighPart == 0, "FAIL timing frequency");
        DWORD start = GetTickCount();
        check(QueryPerformanceCounter(&start_counter), "FAIL timing start");
        for (UINT frame = 0; frame < FRAMES; frame++) {
            DWORD color = 0xff204060u + mode * 0x00201008u + frame * 0x00000301u;
            check(DEV(Clear)(device, 0, NULL, D3DCLEAR_TARGET, color, 1, 0) == D3D_OK, "FAIL clear");
            check(DEV(Present)(device, NULL, NULL, NULL, NULL) == D3D_OK, "FAIL present");
        }
        check(QueryPerformanceCounter(&end_counter) && end_counter.QuadPart >= start_counter.QuadPart && (uint64_t)(end_counter.QuadPart - start_counter.QuadPart) <= 0xffffffffu, "FAIL timing end");
        DWORD elapsed = GetTickCount() - start, ticks = (DWORD)(end_counter.QuadPart - start_counter.QuadPart);
        char line[192];
        wsprintfA(line, "PACE %u %u frames=%u elapsed=%u qpc=%u hz=%u requested=%08X effective=%08X windowed=%u swap=%u", D9WG_PACING_API, mode, FRAMES, (UINT)elapsed, (UINT)ticks, (UINT)frequency.LowPart, (UINT)requested, (UINT)effective, (UINT)pp->Windowed, (UINT)pp->SwapEffect);
        report(line);
        if (effective != D3DPRESENT_INTERVAL_IMMEDIATE)
            check((uint64_t)ticks * 60u + (uint64_t)(frequency.LowPart / 200u) * 60u >= (uint64_t)frequency.LowPart * (FRAMES - 1), "FAIL virtual cadence too fast");
    }
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], path[MAX_PATH], name[40];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    UINT at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    wsprintfA(name, "PACE%u.TXT", D9WG_PACING_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    RECT client = { 0, 0, 64, 64 };
    check(AdjustWindowRectEx(&client, WS_POPUP, FALSE, 0), "FAIL window dimensions");
    HWND window = CreateWindowExA(0, "STATIC", "D3D pacing probe", WS_POPUP, 30, 40, client.right - client.left, client.bottom - client.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(window != NULL, "FAIL window");
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    RECT actual;
    check(GetClientRect(window, &actual) && actual.right == 64 && actual.bottom == 64, "FAIL exact client dimensions");
    wsprintfA(name, "D3D%u.DLL", D9WG_PACING_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    HMODULE module = LoadLibraryA(path);
    check(module != NULL, "FAIL DLL");
    wsprintfA(name, "Direct3DCreate%u", D9WG_PACING_API);
    Factory*(WINAPI * make)(UINT) = (void*)(uintptr_t)GetProcAddress(module, name);
    check(make != NULL, "FAIL factory export");
    factory = make(D3D_SDK_VERSION);
    check(factory != NULL, "FAIL factory");
    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window;
    pp.BackBufferWidth = 64;
    pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
#if D9WG_PACING_API == 8
    check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device8");
#else
    check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device9");
#endif
    pacing_cases(&pp);
    check(DEV(Release)(device) == 0, "FAIL final device references");
    FACTORY_RELEASE(factory);
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS pacing API=%u", D9WG_PACING_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
