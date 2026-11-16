/* Live adapter query probe; API8/API9 use the same independent Win32 oracle. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <stdint.h>
#if D9WG_DISPLAY_API == 8
#include <d3d8.h>
typedef IDirect3D8 Factory;
typedef IDirect3DDevice8 Device;
typedef IDirect3DSurface8 Surface;
#define FACTORY(method) IDirect3D8_##method
#define DEV(method) IDirect3DDevice8_##method
#define SURFACE(method) IDirect3DSurface8_##method
#else
#include <d3d9.h>
typedef IDirect3D9 Factory;
typedef IDirect3DDevice9 Device;
typedef IDirect3DSurface9 Surface;
#define FACTORY(method) IDirect3D9_##method
#define DEV(method) IDirect3DDevice9_##method
#define SURFACE(method) IDirect3DSurface9_##method
static IDirect3DSwapChain9* additional;
#endif
static Factory* factory;
static Device* device;
static HANDLE logfile;
static HWND window;
static UINT lifetimes, snapshots;
static void report(const char* text)
{
    DWORD written;
    WriteFile(logfile, text, lstrlenA(text), &written, NULL);
    WriteFile(logfile, "\r\n", 2, &written, NULL);
    FlushFileBuffers(logfile);
}
static void check(BOOL good, const char* text)
{
    if (!good) {
        report(text);
        ExitProcess(1);
    }
}
static DEVMODEA current_mode(void)
{
    DEVMODEA mode;
    ZeroMemory(&mode, sizeof(mode));
    mode.dmSize = sizeof(mode);
    check(EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &mode), "FAIL Win32 adapter mode");
    return mode;
}
static void set_mode(UINT width, UINT height, UINT bits)
{
    DEVMODEA mode;
    ZeroMemory(&mode, sizeof(mode));
    mode.dmSize = sizeof(mode);
    mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
    mode.dmPelsWidth = width;
    mode.dmPelsHeight = height;
    mode.dmBitsPerPel = bits;
    check(ChangeDisplaySettingsA(&mode, CDS_FULLSCREEN) == DISP_CHANGE_SUCCESSFUL, "FAIL query test mode switch");
}
static void create_device(void)
{
    DEVMODEA mode = current_mode();
    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.Windowed = TRUE;
    pp.BackBufferWidth = pp.BackBufferHeight = 128;
    pp.BackBufferFormat = mode.dmBitsPerPel == 16 ? D3DFMT_R5G6B5 : D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window;
    check(FACTORY(CreateDevice)(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL display query device");
#if D9WG_DISPLAY_API == 9
    pp.BackBufferWidth = 96;
    pp.BackBufferHeight = 64;
    check(DEV(CreateAdditionalSwapChain)(device, &pp, &additional) == D3D_OK, "FAIL query additional chain");
#endif
    lifetimes++;
}
static void release_device(void)
{
#if D9WG_DISPLAY_API == 9
    IDirect3DSwapChain9_Release(additional);
    additional = NULL;
#endif
    check(DEV(Release)(device) == 0, "FAIL display query device references");
    device = NULL;
}
static void compare_mode(const D3DDISPLAYMODE* mode, const DEVMODEA* expected)
{
    check(mode->Width == expected->dmPelsWidth && mode->Height == expected->dmPelsHeight
            && mode->Format == (expected->dmBitsPerPel == 16 ? D3DFMT_R5G6B5 : D3DFMT_X8R8G8B8)
            && mode->RefreshRate == (expected->dmDisplayFrequency > 1 ? expected->dmDisplayFrequency : 60),
        "FAIL public adapter mode differs from Win32");
}
static void snapshot(UINT step)
{
    DEVMODEA expected = current_mode();
    D3DDISPLAYMODE factory_mode, answer;
    check(FACTORY(GetAdapterDisplayMode)(factory, 0, &factory_mode) == D3D_OK, "FAIL factory display query");
    compare_mode(&factory_mode, &expected);
    check(DEV(GetDisplayMode)(device,
#if D9WG_DISPLAY_API == 9
              0,
#endif
              &answer)
            == D3D_OK,
        "FAIL device display query");
    compare_mode(&answer, &expected);
#if D9WG_DISPLAY_API == 9
    IDirect3DSwapChain9* implicit = NULL;
    check(DEV(GetSwapChain)(device, 0, &implicit) == D3D_OK && implicit, "FAIL implicit chain query");
    check(IDirect3DSwapChain9_GetDisplayMode(implicit, &answer) == D3D_OK, "FAIL implicit display query");
    compare_mode(&answer, &expected);
    IDirect3DSwapChain9_Release(implicit);
    check(IDirect3DSwapChain9_GetDisplayMode(additional, &answer) == D3D_OK, "FAIL additional display query");
    compare_mode(&answer, &expected);
    /* Device ordinals count implicit chains, even while an additional chain lives. */
    check(DEV(GetNumberOfSwapChains)(device) == 1 && DEV(GetDisplayMode)(device, 1, &answer) == D3DERR_INVALIDCALL, "FAIL implicit swap-chain ordinal boundary");
#endif
    Surface* back = NULL;
    check(DEV(GetBackBuffer)(device,
#if D9WG_DISPLAY_API == 9
              0,
#endif
              0, D3DBACKBUFFER_TYPE_MONO, &back)
            == D3D_OK,
        "FAIL query backbuffer");
    D3DSURFACE_DESC desc;
    check(SURFACE(GetDesc)(back, &desc) == D3D_OK && desc.Width == 128 && desc.Height == 128, "FAIL query changed backbuffer geometry");
    SURFACE(Release)(back);
    char line[192];
    wsprintfA(line, "DISPLAY QUERY api=%u step=%u width=%u height=%u format=%u refresh=%u backWidth=128 backHeight=128", D9WG_DISPLAY_API, step, factory_mode.Width, factory_mode.Height, factory_mode.Format, factory_mode.RefreshRate);
    report(line);
    snapshots++;
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], file[MAX_PATH], name[96];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    UINT at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    wsprintfA(name, "DSP%u.TXT", D9WG_DISPLAY_API);
    lstrcpyA(file, directory);
    lstrcatA(file, name);
    logfile = CreateFileA(file, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL display query log");
    DEVMODEA desktop = current_mode();
    window = CreateWindowExA(0, "STATIC", "Live display mode query", WS_POPUP, 20, 30, 128, 128, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(window != NULL, "FAIL display query window");
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    wsprintfA(name, "D3D%u.DLL", D9WG_DISPLAY_API);
    lstrcpyA(file, directory);
    lstrcatA(file, name);
    HMODULE module = LoadLibraryA(file);
    check(module != NULL, "FAIL display query DLL");
    wsprintfA(name, "Direct3DCreate%u", D9WG_DISPLAY_API);
    Factory*(WINAPI * make)(UINT) = (void*)(uintptr_t)GetProcAddress(module, name);
    check(make != NULL, "FAIL display query export");
    factory = make(D3D_SDK_VERSION);
    check(factory != NULL, "FAIL display query factory");
    create_device();
    snapshot(0);
    release_device();
    set_mode(640, 480, 32);
    create_device();
    snapshot(1);
    set_mode(800, 600, 32);
    snapshot(2);
    release_device();
    set_mode(640, 480, 16);
    create_device();
    snapshot(3);
    set_mode(800, 600, 16);
    snapshot(4);
    release_device();
    check(ChangeDisplaySettingsA(&desktop, 0) == DISP_CHANGE_SUCCESSFUL, "FAIL restore original query desktop");
    create_device();
    snapshot(5);
    release_device();
    check(lifetimes == 4 && snapshots == 6, "FAIL display query counts");
    FACTORY(Release)(factory);
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "DISPLAY STATUS api=%u lifetimes=4 snapshots=6 unchangedBuffers=6 restored=1", D9WG_DISPLAY_API);
    report(name);
    report("PASS live display queries");
    CloseHandle(logfile);
    ExitProcess(0);
}
