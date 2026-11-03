/* Independent additional-chain pacing and nonblocking presentation probe. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#if D9WG_CHAIN_API == 8
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

#define FRAMES 12u
static HRESULT present_chain(Chain* chain, DWORD flags)
{
#if D9WG_CHAIN_API == 8
    (void)flags;
    return CHAIN(Present)(chain, NULL, NULL, NULL, NULL);
#else
    return CHAIN(Present)(chain, NULL, NULL, NULL, NULL, flags);
#endif
}
static void chain_cases(D3DPRESENT_PARAMETERS* base, HWND first)
{
#if D9WG_CHAIN_API == 9
    D3DCAPS9 caps;
    check(DEV(GetDeviceCaps)(device, &caps) == D3D_OK && !(caps.Caps3 & D3DCAPS3_LINEAR_TO_SRGB_PRESENTATION), "FAIL unsupported linear capability");
#endif
    HWND second = CreateWindowExA(0, "STATIC", "Second paced chain", WS_POPUP, 220, 140, 64, 64, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(second != NULL, "FAIL second window");
    ShowWindow(second, SW_SHOW);
    for (UINT mode = 0; mode < 2; mode++) {
        Chain* chains[2] = { NULL, NULL };
        for (UINT i = 0; i < 2; i++) {
            D3DPRESENT_PARAMETERS p = *base;
            p.hDeviceWindow = i ? second : first;
#if D9WG_CHAIN_API == 8
            p.SwapEffect = mode ? D3DSWAPEFFECT_COPY_VSYNC : D3DSWAPEFFECT_DISCARD;
#else
            p.PresentationInterval = mode ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE;
#endif
            check(DEV(CreateAdditionalSwapChain)(device, &p, &chains[i]) == D3D_OK && chains[i], "FAIL chain create");
        }
        check(DEV(Clear)(device, 0, NULL, D3DCLEAR_TARGET, 0xff507080u + mode, 1, 0) == D3D_OK, "FAIL clear");
        LARGE_INTEGER frequency, start, end;
        check(QueryPerformanceFrequency(&frequency) && frequency.QuadPart > 0 && frequency.HighPart == 0, "FAIL timing frequency");
        check(QueryPerformanceCounter(&start), "FAIL timing start");
        for (UINT frame = 0; frame < FRAMES; frame++)
            for (UINT i = 0; i < 2; i++)
                check(present_chain(chains[i], 0) == D3D_OK, "FAIL chain present");
        check(QueryPerformanceCounter(&end) && end.QuadPart >= start.QuadPart && (uint64_t)(end.QuadPart - start.QuadPart) <= 0xffffffffu, "FAIL timing end");
        DWORD ticks = (DWORD)(end.QuadPart - start.QuadPart);
        char line[160];
        wsprintfA(line, "CHAIN %u %u frames=%u qpc=%u hz=%u effective=%08X", D9WG_CHAIN_API, mode, FRAMES * 2, (UINT)ticks, (UINT)frequency.LowPart, mode ? 1u : 0x80000000u);
        report(line);
        if (mode)
            check((uint64_t)ticks * 60u + (uint64_t)(frequency.LowPart / 200u) * 60u >= (uint64_t)frequency.LowPart * (FRAMES - 1), "FAIL additional cadence too fast");
#if D9WG_CHAIN_API == 9
        if (mode) {
            check(present_chain(chains[0], 0) == D3D_OK, "FAIL nonblocking setup");
            check(present_chain(chains[0], D3DPRESENT_DONOTWAIT) == D3DERR_WASSTILLDRAWING, "FAIL additional nonblocking busy");
            Sleep(40);
            check(present_chain(chains[0], D3DPRESENT_DONOTWAIT | D3DPRESENT_LINEAR_CONTENT) == D3D_OK, "FAIL additional nonblocking ready");
            Chain* implicit = NULL;
            check(DEV(GetSwapChain)(device, 0, &implicit) == D3D_OK && implicit, "FAIL implicit chain");
            check(DEV(Present)(device, NULL, NULL, NULL, NULL) == D3D_OK, "FAIL implicit setup");
            check(present_chain(implicit, D3DPRESENT_DONOTWAIT) == D3DERR_WASSTILLDRAWING, "FAIL implicit nonblocking busy");
            Sleep(40);
            check(present_chain(implicit, D3DPRESENT_DONOTWAIT | D3DPRESENT_LINEAR_CONTENT) == D3D_OK, "FAIL implicit nonblocking ready");
            check(CHAIN(Release)(implicit) == 0, "FAIL implicit chain refs");
            report("NONBLOCK 9 additional=1 implicit=1 linear=accepted");
        }
#endif
        for (UINT i = 0; i < 2; i++)
            check(CHAIN(Release)(chains[i]) == 0, "FAIL additional chain refs");
    }
    DestroyWindow(second);
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], path[MAX_PATH], name[100];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    UINT at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    wsprintfA(name, "CHN%u.TXT", D9WG_CHAIN_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    RECT client = { 0, 0, 64, 64 };
    check(AdjustWindowRectEx(&client, WS_POPUP, FALSE, 0), "FAIL window dimensions");
    HWND window = CreateWindowExA(0, "STATIC", "D3D chain pacing probe", WS_POPUP, 30, 40, client.right - client.left, client.bottom - client.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(window != NULL, "FAIL window");
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    RECT actual;
    check(GetClientRect(window, &actual) && actual.right == 64 && actual.bottom == 64, "FAIL exact client dimensions");
    wsprintfA(name, "D3D%u.DLL", D9WG_CHAIN_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    HMODULE module = LoadLibraryA(path);
    check(module != NULL, "FAIL DLL");
    wsprintfA(name, "Direct3DCreate%u", D9WG_CHAIN_API);
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
#if D9WG_CHAIN_API == 9
    pp.PresentationInterval = D3DPRESENT_INTERVAL_ONE;
#endif
#if D9WG_CHAIN_API == 8
    check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device8");
#else
    check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device9");
#endif
    HWND extra_window = CreateWindowExA(0, "STATIC", "First paced chain", WS_POPUP, 120, 140, 64, 64, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(extra_window != NULL, "FAIL extra window");
    ShowWindow(extra_window, SW_SHOW);
    chain_cases(&pp, extra_window);
    DestroyWindow(extra_window);
    check(DEV(Release)(device) == 0, "FAIL device references");
    FACTORY_RELEASE(factory);
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS chain API=%u", D9WG_CHAIN_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
