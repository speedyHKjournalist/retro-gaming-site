/* Window-state acceptance against independent Win32 observations in real XP. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#if D9WG_WINDOW_API == 9
#include <d3d9.h>
#elif D9WG_WINDOW_API == 8
#include <d3d8.h>
#else
#include <ddraw.h>
#endif
#ifndef D9WG_WINDOW_WATCHERS
#define D9WG_WINDOW_WATCHERS 1
#endif
#ifndef D9WG_WINDOW_NO_PUMP
#define D9WG_WINDOW_NO_PUMP 0
#endif
static HANDLE logfile, phasefile;
#if D9WG_WINDOW_WATCHERS > 1
static HWND dummy_windows[D9WG_WINDOW_WATCHERS - 1];
#if D9WG_WINDOW_API == 9
static IDirect3DDevice9* dummy_devices[D9WG_WINDOW_WATCHERS - 1];
#elif D9WG_WINDOW_API == 8
static IDirect3DDevice8* dummy_devices[D9WG_WINDOW_WATCHERS - 1];
#else
static IDirectDraw7* dummy_factories[D9WG_WINDOW_WATCHERS - 1];
static IDirectDrawSurface7* dummy_primaries[D9WG_WINDOW_WATCHERS - 1];
#endif
#endif
static HWND main_window, auxiliary_a, auxiliary_b;
#if D9WG_WINDOW_API == 9
static IDirect3D9* factory;
static IDirect3DDevice9* device;
#elif D9WG_WINDOW_API == 8
static IDirect3D8* factory;
static IDirect3DDevice8* device;
#else
static IDirectDraw7* factory;
static IDirectDrawSurface7* primary;
#endif
static void report(const char* line)
{
    DWORD written;
    WriteFile(logfile, line, lstrlenA(line), &written, NULL);
    WriteFile(logfile, "\r\n", 2, &written, NULL);
    FlushFileBuffers(logfile);
}
static void check(BOOL good, const char* what)
{
    if (!good) {
        report(what);
        ExitProcess(1);
    }
}
static void phase(UINT value)
{
    DWORD written;
    SetFilePointer(phasefile, 0, NULL, FILE_BEGIN);
    check(WriteFile(phasefile, &value, sizeof(value), &written, NULL) && written == sizeof(value), "FAIL phase write");
    FlushFileBuffers(phasefile);
}
static void pump(void)
{
#if D9WG_WINDOW_NO_PUMP
    Sleep(250);
#else
    DWORD start = GetTickCount();
    MSG message;
    do {
        while (PeekMessageA(&message, NULL, 0, 0, PM_REMOVE)) {
            TranslateMessage(&message);
            DispatchMessageA(&message);
        }
        Sleep(1);
    } while ((DWORD)(GetTickCount() - start) < 250);
#endif
}
static void present(void)
{
#if D9WG_WINDOW_API == 9
    check(IDirect3DDevice9_Present(device, NULL, NULL, NULL, NULL) == D3D_OK, "FAIL Present9");
#elif D9WG_WINDOW_API == 8
    check(IDirect3DDevice8_Present(device, NULL, NULL, NULL, NULL) == D3D_OK, "FAIL Present8");
#else
    DDBLTFX effects;
    ZeroMemory(&effects, sizeof(effects));
    effects.dwSize = sizeof(effects);
    check(IDirectDrawSurface7_Blt(primary, NULL, NULL, NULL, DDBLT_COLORFILL | DDBLT_WAIT, &effects) == DD_OK, "FAIL Blt7");
#endif
}
static void snapshot(UINT step, BOOL no_surface)
{
    RECT client, outer;
    POINT origin = { 0, 0 };
    check(GetClientRect(main_window, &client) && GetWindowRect(main_window, &outer) && ClientToScreen(main_window, &origin), "FAIL independent Win32 geometry");
    char line[300];
    wsprintfA(line, "STEP %u %u %lu %lu %u %u %u %ld %ld %ld %ld %ld %ld %ld %ld %u", D9WG_WINDOW_API, step, (DWORD)(uintptr_t)main_window, (DWORD)(uintptr_t)GetForegroundWindow(), IsWindow(main_window) != 0, IsWindowVisible(main_window) != 0, IsIconic(main_window) != 0, origin.x, origin.y, client.right - client.left, client.bottom - client.top, outer.right - outer.left, outer.bottom - outer.top, outer.left, outer.top, no_surface != 0);
    report(line);
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], path[MAX_PATH], name[64];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    UINT at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    wsprintfA(name, "WINDOW%u.TXT", D9WG_WINDOW_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    lstrcpyA(path, directory);
    lstrcatA(path, "PHASE.DAT");
    phasefile = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
    check(phasefile != INVALID_HANDLE_VALUE, "FAIL phase file");
    phase(0);
    main_window = CreateWindowExA(0, "STATIC", "DirectX window report", WS_OVERLAPPEDWINDOW, 20, 20, 200, 150, NULL, NULL, GetModuleHandleA(NULL), NULL);
    auxiliary_a = CreateWindowExA(0, "STATIC", "Foreground A", WS_OVERLAPPEDWINDOW, 650, 30, 120, 90, NULL, NULL, GetModuleHandleA(NULL), NULL);
    auxiliary_b = CreateWindowExA(0, "STATIC", "Foreground B", WS_OVERLAPPEDWINDOW, 650, 180, 120, 90, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(main_window && auxiliary_a && auxiliary_b, "FAIL windows");
    ShowWindow(main_window, SW_SHOW);
    SetForegroundWindow(main_window);
    pump();
#if D9WG_WINDOW_API == 9
    lstrcpyA(path, directory);
    lstrcatA(path, "D3D9.DLL");
    HMODULE module = LoadLibraryA(path);
    check(module != NULL, "FAIL DLL9");
    IDirect3D9*(WINAPI * make)(UINT) = (void*)(uintptr_t)GetProcAddress(module, "Direct3DCreate9");
    check(make != NULL, "FAIL factory export9");
    factory = make(D3D_SDK_VERSION);
    check(factory != NULL, "FAIL factory9");
    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = main_window;
    pp.BackBufferWidth = 64;
    pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
#if D9WG_WINDOW_WATCHERS > 1
    for (UINT i = 0; i < D9WG_WINDOW_WATCHERS - 1; ++i) {
        dummy_windows[i] = CreateWindowExA(0, "STATIC", "Additional window subscriber", WS_POPUP, 400, 400, 64, 64, NULL, NULL, GetModuleHandleA(NULL), NULL);
        check(dummy_windows[i] != NULL, "FAIL extra window");
        D3DPRESENT_PARAMETERS extra = pp;
        extra.hDeviceWindow = dummy_windows[i];
        check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, dummy_windows[i], D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &extra, &dummy_devices[i]) == D3D_OK, "FAIL extra device9");
    }
#endif
    check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, main_window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device9");
#elif D9WG_WINDOW_API == 8
    lstrcpyA(path, directory);
    lstrcatA(path, "D3D8.DLL");
    HMODULE module = LoadLibraryA(path);
    check(module != NULL, "FAIL DLL8");
    IDirect3D8*(WINAPI * make)(UINT) = (void*)(uintptr_t)GetProcAddress(module, "Direct3DCreate8");
    check(make != NULL, "FAIL factory export8");
    factory = make(D3D_SDK_VERSION);
    check(factory != NULL, "FAIL factory8");
    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = main_window;
    pp.BackBufferWidth = 64;
    pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
#if D9WG_WINDOW_WATCHERS > 1
    for (UINT i = 0; i < D9WG_WINDOW_WATCHERS - 1; ++i) {
        dummy_windows[i] = CreateWindowExA(0, "STATIC", "Additional window subscriber", WS_POPUP, 400, 400, 64, 64, NULL, NULL, GetModuleHandleA(NULL), NULL);
        check(dummy_windows[i] != NULL, "FAIL extra window");
        D3DPRESENT_PARAMETERS extra = pp;
        extra.hDeviceWindow = dummy_windows[i];
        check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, dummy_windows[i], D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &extra, &dummy_devices[i]) == D3D_OK, "FAIL extra device8");
    }
#endif
    check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, main_window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device8");
#else
    lstrcpyA(path, directory);
    lstrcatA(path, "DDRAW.DLL");
    HMODULE module = LoadLibraryA(path);
    check(module != NULL, "FAIL DLL7");
    HRESULT(WINAPI * make)(GUID*, void**, REFIID, IUnknown*) = (void*)(uintptr_t)GetProcAddress(module, "DirectDrawCreateEx");
    check(make != NULL, "FAIL factory export7");
    DDSURFACEDESC2 desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS;
    desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
#if D9WG_WINDOW_WATCHERS > 1
    for (UINT i = 0; i < D9WG_WINDOW_WATCHERS - 1; ++i) {
        dummy_windows[i] = CreateWindowExA(0, "STATIC", "Additional window subscriber", WS_POPUP, 400, 400, 64, 64, NULL, NULL, GetModuleHandleA(NULL), NULL);
        check(dummy_windows[i] != NULL, "FAIL extra window");
        check(make(NULL, (void**)&dummy_factories[i], &IID_IDirectDraw7, NULL) == DD_OK, "FAIL extra factory7");
        check(IDirectDraw7_SetCooperativeLevel(dummy_factories[i], dummy_windows[i], DDSCL_NORMAL) == DD_OK, "FAIL extra cooperative7");
        check(IDirectDraw7_CreateSurface(dummy_factories[i], &desc, &dummy_primaries[i], NULL) == DD_OK, "FAIL extra primary7");
    }
#endif
    check(make(NULL, (void**)&factory, &IID_IDirectDraw7, NULL) == DD_OK, "FAIL factory7");
    check(IDirectDraw7_SetCooperativeLevel(factory, main_window, DDSCL_NORMAL) == DD_OK, "FAIL cooperative");
    check(IDirectDraw7_CreateSurface(factory, &desc, &primary, NULL) == DD_OK, "FAIL primary");
#endif
    wsprintfA(name, "WATCHED %u %u", D9WG_WINDOW_API, D9WG_WINDOW_WATCHERS);
    report(name);
    pump();
    for (UINT step = 1; step <= 7; ++step) {
        phase(step);
        present();
        switch (step) {
        case 1:
            check(SetWindowPos(main_window, NULL, 60, 60, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE), "FAIL move1");
            break;
        case 2:
            check(SetWindowPos(main_window, NULL, 100, 90, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE), "FAIL move2");
            break;
        case 3:
            check(SetWindowPos(main_window, NULL, 0, 0, 260, 190, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE), "FAIL resize");
            break;
        case 4:
            ShowWindow(auxiliary_a, SW_SHOW);
            SetForegroundWindow(auxiliary_a);
            break;
        case 5:
            ShowWindow(auxiliary_b, SW_SHOW);
            SetForegroundWindow(auxiliary_b);
            break;
        case 6:
            ShowWindow(main_window, SW_HIDE);
            break;
        case 7:
            ShowWindow(main_window, SW_SHOWNOACTIVATE);
            break;
        }
        pump();
        if (step == 4)
            check(GetForegroundWindow() == auxiliary_a, "FAIL foreground A");
        if (step == 5)
            check(GetForegroundWindow() == auxiliary_b, "FAIL foreground B");
        snapshot(step, FALSE);
    }
#if D9WG_WINDOW_API == 7
    phase(8);
    IDirectDrawSurface7_Release(primary);
    primary = NULL;
    pump();
    snapshot(8, TRUE);
#if D9WG_WINDOW_WATCHERS > 1
    for (UINT i = 0; i < D9WG_WINDOW_WATCHERS - 1; ++i) {
        IDirectDrawSurface7_Release(dummy_primaries[i]);
        IDirectDraw7_Release(dummy_factories[i]);
    }
#endif
    IDirectDraw7_Release(factory);
#elif D9WG_WINDOW_API == 8
    IDirect3DDevice8_Release(device);
#if D9WG_WINDOW_WATCHERS > 1
    for (UINT i = 0; i < D9WG_WINDOW_WATCHERS - 1; ++i)
        IDirect3DDevice8_Release(dummy_devices[i]);
#endif
    IDirect3D8_Release(factory);
#else
    IDirect3DDevice9_Release(device);
#if D9WG_WINDOW_WATCHERS > 1
    for (UINT i = 0; i < D9WG_WINDOW_WATCHERS - 1; ++i)
        IDirect3DDevice9_Release(dummy_devices[i]);
#endif
    IDirect3D9_Release(factory);
#endif
    FreeLibrary(module);
#if D9WG_WINDOW_NO_PUMP
    DWORD unload_start = GetTickCount();
    while (GetModuleHandleA(path) && (DWORD)(GetTickCount() - unload_start) < 2000)
        Sleep(1);
    check(GetModuleHandleA(path) == NULL, "FAIL event thread DLL unload");
    wsprintfA(name, "UNLOADED %u", D9WG_WINDOW_API);
    report(name);
#endif
#if D9WG_WINDOW_WATCHERS > 1
    for (UINT i = 0; i < D9WG_WINDOW_WATCHERS - 1; ++i)
        DestroyWindow(dummy_windows[i]);
#endif
    DestroyWindow(auxiliary_a);
    DestroyWindow(auxiliary_b);
    DestroyWindow(main_window);
    CloseHandle(phasefile);
    wsprintfA(name, "PASS window SDK API=%u no_pump=%u", D9WG_WINDOW_API, D9WG_WINDOW_NO_PUMP);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
