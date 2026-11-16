/* Live desktop format selection for D3D9 windowed UNKNOWN requests. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <d3d9.h>
static IDirect3D9* factory;
static IDirect3DDevice9* device;
static HANDLE logfile;
static HWND window;
static DEVMODEA original;
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
        ChangeDisplaySettingsA(&original, CDS_FULLSCREEN);
        report(message);
        ExitProcess(1);
    }
}
static D3DFORMAT desktop_format(void)
{
    DEVMODEA mode;
    ZeroMemory(&mode, sizeof(mode));
    mode.dmSize = sizeof(mode);
    check(EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &mode), "FAIL current desktop query");
    check(mode.dmBitsPerPel == 16 || mode.dmBitsPerPel == 32, "FAIL expected desktop color depth");
    return mode.dmBitsPerPel == 16 ? D3DFMT_R5G6B5 : D3DFMT_X8R8G8B8;
}
static D3DPRESENT_PARAMETERS parameters(D3DFORMAT format)
{
    D3DPRESENT_PARAMETERS p;
    ZeroMemory(&p, sizeof(p));
    p.Windowed = TRUE;
    p.SwapEffect = D3DSWAPEFFECT_DISCARD;
    p.BackBufferWidth = 128;
    p.BackBufferHeight = 96;
    p.BackBufferFormat = format;
    return p;
}
static void create_device(D3DFORMAT format)
{
    D3DPRESENT_PARAMETERS p = parameters(format);
    device = NULL;
    check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window,
              D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &p, &device)
                == D3D_OK
            && device,
        "FAIL default-format device create");
    check(p.BackBufferFormat == (format ? format : desktop_format()) && !p.hDeviceWindow && p.BackBufferCount == 1,
        "FAIL constructor returned format");
}
static void log_format(const char* label, D3DFORMAT format)
{
    char line[160];
    wsprintfA(line, "DEFAULT FORMAT label=%s format=%u desktop=%u", label, (UINT)format, (UINT)desktop_format());
    report(line);
}
static void check_device_format(const char* label, D3DFORMAT expected)
{
    IDirect3DSurface9* back = NULL;
    D3DSURFACE_DESC desc;
    D3DDISPLAYMODE mode;
    check(IDirect3DDevice9_GetBackBuffer(device, 0, 0, D3DBACKBUFFER_TYPE_MONO, &back) == D3D_OK && back,
        "FAIL default-format backbuffer");
    check(IDirect3DSurface9_GetDesc(back, &desc) == D3D_OK && desc.Format == expected && desc.Width == 128 && desc.Height == 96,
        "FAIL default-format descriptor");
    IDirect3DSurface9_Release(back);
    check(IDirect3DDevice9_GetDisplayMode(device, 0, &mode) == D3D_OK && mode.Format == desktop_format(),
        "FAIL adapter query confused with backbuffer");
    IDirect3DSwapChain9* chain = NULL;
    D3DPRESENT_PARAMETERS p;
    check(IDirect3DDevice9_GetSwapChain(device, 0, &chain) == D3D_OK && chain
            && IDirect3DSwapChain9_GetPresentParameters(chain, &p) == D3D_OK
            && p.BackBufferFormat == expected && p.hDeviceWindow == window,
        "FAIL cached implicit format");
    IDirect3DSwapChain9_Release(chain);
    log_format(label, expected);
}
static void additional(const char* label, D3DFORMAT requested, D3DFORMAT expected)
{
    D3DPRESENT_PARAMETERS p = parameters(requested);
    p.BackBufferWidth = p.BackBufferHeight = 64;
    IDirect3DSwapChain9* chain = NULL;
    IDirect3DSurface9* back = NULL;
    D3DSURFACE_DESC desc;
    check(IDirect3DDevice9_CreateAdditionalSwapChain(device, &p, &chain) == D3D_OK && chain
            && p.BackBufferFormat == expected && !p.hDeviceWindow,
        "FAIL additional returned live format");
    check(IDirect3DSwapChain9_GetBackBuffer(chain, 0, D3DBACKBUFFER_TYPE_MONO, &back) == D3D_OK && back
            && IDirect3DSurface9_GetDesc(back, &desc) == D3D_OK
            && desc.Format == expected && desc.Width == 64 && desc.Height == 64,
        "FAIL additional live format descriptor");
    IDirect3DSurface9_Release(back);
    check(IDirect3DSwapChain9_Release(chain) == 0, "FAIL additional format chain release");
    log_format(label, expected);
}
static void reset_unknown(const char* label, D3DFORMAT expected)
{
    D3DPRESENT_PARAMETERS p = parameters(D3DFMT_UNKNOWN);
    check(IDirect3DDevice9_Reset(device, &p) == D3D_OK && p.BackBufferFormat == expected
            && IDirect3DDevice9_TestCooperativeLevel(device) == D3D_OK,
        "FAIL Reset live desktop format");
    check_device_format(label, expected);
}
static void sixteen_bit(void)
{
    DEVMODEA wanted;
    ZeroMemory(&wanted, sizeof(wanted));
    wanted.dmSize = sizeof(wanted);
    wanted.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
    wanted.dmPelsWidth = 640;
    wanted.dmPelsHeight = 480;
    wanted.dmBitsPerPel = 16;
    check(ChangeDisplaySettingsA(&wanted, CDS_FULLSCREEN) == DISP_CHANGE_SUCCESSFUL
            && desktop_format() == D3DFMT_R5G6B5,
        "FAIL select 16-bit desktop");
}
static void restore_desktop(void)
{
    check(ChangeDisplaySettingsA(&original, CDS_FULLSCREEN) == DISP_CHANGE_SUCCESSFUL
            && desktop_format() == D3DFMT_X8R8G8B8,
        "FAIL restore 32-bit desktop");
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], path[MAX_PATH];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    UINT at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        at--;
    directory[at] = 0;
    lstrcpyA(path, directory);
    lstrcatA(path, "FMT9.TXT");
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    ZeroMemory(&original, sizeof(original));
    original.dmSize = sizeof(original);
    check(logfile != INVALID_HANDLE_VALUE && EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &original)
            && original.dmBitsPerPel == 32,
        "FAIL original desktop/log");
    window = CreateWindowExA(0, "STATIC", "D3D9 default format", WS_POPUP, 30, 40, 128, 96, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(window != NULL, "FAIL format window");
    ShowWindow(window, SW_SHOW);
    lstrcpyA(path, directory);
    lstrcatA(path, "D3D9.DLL");
    HMODULE module = LoadLibraryA(path);
    check(module != NULL, "FAIL format DLL");
    IDirect3D9*(WINAPI * make)(UINT) = (void*)(uintptr_t)GetProcAddress(module, "Direct3DCreate9");
    check(make != NULL && (factory = make(D3D_SDK_VERSION)) != NULL, "FAIL format factory");
    create_device(D3DFMT_UNKNOWN);
    check_device_format("constructorUnknown32", D3DFMT_X8R8G8B8);
    check(IDirect3DDevice9_Release(device) == 0, "FAIL first format device release");
    create_device(D3DFMT_A8R8G8B8);
    check_device_format("explicitBase", D3DFMT_A8R8G8B8);
    additional("additionalUnknown32", D3DFMT_UNKNOWN, D3DFMT_X8R8G8B8);
    D3DPRESENT_PARAMETERS invalid = parameters(D3DFMT_UNKNOWN);
    invalid.Windowed = FALSE;
    invalid.BackBufferWidth = 640;
    invalid.BackBufferHeight = 480;
    check(IDirect3DDevice9_Reset(device, &invalid) == D3DERR_INVALIDCALL
            && IDirect3DDevice9_TestCooperativeLevel(device) == D3DERR_DEVICENOTRESET,
        "FAIL fullscreen UNKNOWN Reset refusal");
    reset_unknown("resetUnknown32", D3DFMT_X8R8G8B8);
    sixteen_bit();
    additional("additionalUnknown16", D3DFMT_UNKNOWN, D3DFMT_R5G6B5);
    reset_unknown("resetUnknown16", D3DFMT_R5G6B5);
    additional("additionalExplicit16", D3DFMT_A8R8G8B8, D3DFMT_A8R8G8B8);
    restore_desktop();
    reset_unknown("resetUnknownRestored", D3DFMT_X8R8G8B8);
    check(IDirect3DDevice9_Release(device) == 0, "FAIL second format device release");
    sixteen_bit();
    create_device(D3DFMT_UNKNOWN);
    check_device_format("constructorUnknown16", D3DFMT_R5G6B5);
    check(IDirect3DDevice9_Release(device) == 0, "FAIL third format device release");
    restore_desktop();
    IDirect3DDevice9* refused = (void*)(uintptr_t)0x1234;
    invalid = parameters(D3DFMT_UNKNOWN);
    invalid.Windowed = FALSE;
    invalid.BackBufferWidth = 640;
    invalid.BackBufferHeight = 480;
    check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window,
              D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &invalid, &refused)
                == D3DERR_INVALIDCALL
            && !refused,
        "FAIL fullscreen UNKNOWN constructor refusal");
    DEVMODEA final;
    ZeroMemory(&final, sizeof(final));
    final.dmSize = sizeof(final);
    check(EnumDisplaySettingsA(NULL, ENUM_CURRENT_SETTINGS, &final)
            && final.dmPelsWidth == original.dmPelsWidth && final.dmPelsHeight == original.dmPelsHeight
            && final.dmBitsPerPel == original.dmBitsPerPel,
        "FAIL original desktop changed by refused constructor");
    check(IDirect3D9_Release(factory) == 0, "FAIL final format factory references");
    FreeLibrary(module);
    DestroyWindow(window);
    report("DEFAULT FORMAT STATUS lifetimes=3 resets=3 additional=3 invalidFullscreenConstructor=1 invalidFullscreenReset=1 originalRestored=1");
    report("PASS default format");
    CloseHandle(logfile);
    ExitProcess(0);
}
