/* Present interval validation and atomic failure probe. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#if D9WG_PARAMETERS_API == 8
#include <d3d8.h>
typedef IDirect3D8 Factory;
typedef IDirect3DDevice8 Device;
typedef IDirect3DSwapChain8 Chain;
#define CHAIN(method) IDirect3DSwapChain8_##method
#define DEV(method) IDirect3DDevice8_##method
#define FACTORY_RELEASE IDirect3D8_Release
#else
#include <d3d9.h>
typedef IDirect3D9 Factory;
typedef IDirect3DDevice9 Device;
typedef IDirect3DSwapChain9 Chain;
#define CHAIN(method) IDirect3DSwapChain9_##method
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

static UINT invalid_count, valid_creates, valid_resets, valid_chains;
static const DWORD intervals[] = { 0, 1, 2, 4, 8, 0x80000000u, 3, 5, 6, 9, 16, 0x40000000u, 0xffffffffu, 0x80000001u };
static BOOL valid_interval(BOOL windowed, DWORD interval)
{
#if D9WG_PARAMETERS_API == 8
    if (windowed)
        return interval == 0;
#else
    (void)windowed;
#endif
    return interval == 0 || interval == 1 || interval == 0x80000000u;
}
static void requested(D3DPRESENT_PARAMETERS* p, DWORD interval)
{
#if D9WG_PARAMETERS_API == 8
    p->FullScreen_PresentationInterval = interval;
#else
    p->PresentationInterval = interval;
#endif
}
static BOOL same(const void* a, const void* b, UINT n)
{
    const BYTE *x = a, *y = b;
    for (UINT i = 0; i < n; i++)
        if (x[i] != y[i])
            return FALSE;
    return TRUE;
}
static HRESULT make_device(HWND focus, D3DPRESENT_PARAMETERS* p, Device** out)
{
#if D9WG_PARAMETERS_API == 8
    return IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, focus, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, p, out);
#else
    return IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, focus, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, p, out);
#endif
}
static void frame(UINT index)
{
    check(DEV(Clear)(device, 0, NULL, D3DCLEAR_TARGET, 0xff304050u + index, 1, 0) == D3D_OK, "FAIL clear after parameter call");
    check(DEV(Present)(device, NULL, NULL, NULL, NULL) == D3D_OK, "FAIL present after parameter call");
}
static void parameter_cases(D3DPRESENT_PARAMETERS* base, HWND extra_window)
{
#if D9WG_PARAMETERS_API == 8
    D3DCAPS8 caps;
    D3DVIEWPORT8 viewport = { 1, 2, 30, 31, .25f, .75f }, out;
#else
    D3DCAPS9 caps;
    D3DVIEWPORT9 viewport = { 1, 2, 30, 31, .25f, .75f }, out;
#endif
    check(DEV(GetDeviceCaps)(device, &caps) == D3D_OK && caps.PresentationIntervals == 0x80000001u, "FAIL published interval mask");
    check(DEV(SetViewport)(device, &viewport) == D3D_OK, "FAIL viewport setup");
    for (UINT windowed = 0; windowed < 2; windowed++)
        for (UINT i = 0; i < sizeof(intervals) / sizeof(intervals[0]); i++) {
            DWORD interval = intervals[i];
            if (valid_interval(windowed, interval))
                continue;
            D3DPRESENT_PARAMETERS p = *base;
            p.Windowed = windowed;
            p.BackBufferWidth = windowed ? 64 : 640;
            p.BackBufferHeight = windowed ? 64 : 480;
            p.hDeviceWindow = extra_window;
            requested(&p, interval);
            D3DPRESENT_PARAMETERS before = p;
            Device* temporary = (void*)(uintptr_t)0xdeadbeef;
            HRESULT hr = make_device(extra_window, &p, &temporary);
            check(hr == D3DERR_INVALIDCALL && temporary == NULL && same(&p, &before, sizeof(p)), "FAIL invalid CreateDevice interval");
            invalid_count++;
            p.hDeviceWindow = base->hDeviceWindow;
            before = p;
            check(DEV(Reset)(device, &p) == D3DERR_INVALIDCALL && same(&p, &before, sizeof(p)), "FAIL invalid Reset interval");
            invalid_count++;
            check(DEV(GetViewport)(device, &out) == D3D_OK && same(&out, &viewport, sizeof(out)), "FAIL rejected Reset changed viewport");
            if (windowed) {
                p.hDeviceWindow = extra_window;
                before = p;
                Chain* chain = (void*)(uintptr_t)0xdeadbeef;
                check(DEV(CreateAdditionalSwapChain)(device, &p, &chain) == D3DERR_INVALIDCALL && chain == NULL && same(&p, &before, sizeof(p)), "FAIL invalid additional interval");
                invalid_count++;
            }
        }
    frame(0);
    for (UINT i = 0; i < sizeof(intervals) / sizeof(intervals[0]); i++) {
        DWORD interval = intervals[i];
        if (!valid_interval(TRUE, interval))
            continue;
        D3DPRESENT_PARAMETERS p = *base;
        p.hDeviceWindow = extra_window;
        requested(&p, interval);
        Device* temporary = NULL;
        check(make_device(extra_window, &p, &temporary) == D3D_OK && temporary != NULL, "FAIL valid interval create");
#if D9WG_PARAMETERS_API == 8
        check(IDirect3DDevice8_Release(temporary) == 0, "FAIL temporary device refs");
#else
        check(IDirect3DDevice9_Release(temporary) == 0, "FAIL temporary device refs");
#endif
        valid_creates++;
        p = *base;
        requested(&p, interval);
        check(DEV(Reset)(device, &p) == D3D_OK, "FAIL valid interval reset");
        valid_resets++;
        frame(valid_resets);
        p = *base;
        p.hDeviceWindow = extra_window;
        requested(&p, interval);
        Chain* chain = NULL;
        check(DEV(CreateAdditionalSwapChain)(device, &p, &chain) == D3D_OK && chain != NULL, "FAIL valid additional interval");
        check(CHAIN(Release)(chain) == 0, "FAIL chain refs");
        valid_chains++;
    }
    char line[160];
    wsprintfA(line, "PARAMETERS %u invalid=%u creates=%u resets=%u chains=%u caps=%08X viewport=preserved", D9WG_PARAMETERS_API, invalid_count, valid_creates, valid_resets, valid_chains, (UINT)caps.PresentationIntervals);
    report(line);
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], path[MAX_PATH], name[100];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    UINT at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    wsprintfA(name, "PAR%u.TXT", D9WG_PARAMETERS_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    RECT client = { 0, 0, 64, 64 };
    check(AdjustWindowRectEx(&client, WS_POPUP, FALSE, 0), "FAIL window dimensions");
    HWND window = CreateWindowExA(0, "STATIC", "D3D interval validation probe", WS_POPUP, 30, 40, client.right - client.left, client.bottom - client.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(window != NULL, "FAIL window");
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    RECT actual;
    check(GetClientRect(window, &actual) && actual.right == 64 && actual.bottom == 64, "FAIL exact client dimensions");
    wsprintfA(name, "D3D%u.DLL", D9WG_PARAMETERS_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    HMODULE module = LoadLibraryA(path);
    check(module != NULL, "FAIL DLL");
    wsprintfA(name, "Direct3DCreate%u", D9WG_PARAMETERS_API);
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
#if D9WG_PARAMETERS_API == 8
    check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device8");
#else
    check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device9");
#endif
    HWND extra_window = CreateWindowExA(0, "STATIC", "Additional interval target", WS_POPUP, 120, 140, 64, 64, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(extra_window != NULL, "FAIL extra window");
    ShowWindow(extra_window, SW_SHOW);
    parameter_cases(&pp, extra_window);
    DestroyWindow(extra_window);
    check(DEV(Release)(device) == 0, "FAIL device references");
    FACTORY_RELEASE(factory);
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS parameters API=%u", D9WG_PARAMETERS_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
