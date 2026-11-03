/* Implicit surface identity, zero/nonzero pins and cached geometry probe. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#if D9WG_IMPLICIT_API == 8
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

#if D9WG_IMPLICIT_API == 8
typedef IDirect3DSurface8 Surface;
#define SURFACE(m) IDirect3DSurface8_##m
#else
typedef IDirect3DSurface9 Surface;
#define SURFACE(m) IDirect3DSurface9_##m
#endif
static ULONG refs(IUnknown* object)
{
    IUnknown_AddRef(object);
    return IUnknown_Release(object);
}
static void check_caller(D3DPRESENT_PARAMETERS* p, UINT width, UINT height, HWND supplied)
{
    check(p->BackBufferCount == 1 && p->hDeviceWindow == supplied && p->BackBufferFormat == D3DFMT_X8R8G8B8, "FAIL additional returned count/window/format");
#if D9WG_IMPLICIT_API == 8
    (void)width;
    (void)height;
    check(!p->BackBufferWidth && !p->BackBufferHeight, "FAIL additional D3D8 changed caller dimensions");
#else
    check(p->BackBufferWidth == width && p->BackBufferHeight == height, "FAIL additional D3D9 caller dimensions");
#endif
}
static void check_buffer(Chain* c, UINT width, UINT height, HWND selected)
{
#if D9WG_IMPLICIT_API == 9
    D3DPRESENT_PARAMETERS effective;
    check(CHAIN(GetPresentParameters)(c, &effective) == D3D_OK && effective.BackBufferWidth == width && effective.BackBufferHeight == height && effective.BackBufferCount == 1 && effective.hDeviceWindow == selected, "FAIL additional cached parameters");
#else
    (void)selected;
#endif
    Surface* surface = NULL;
    D3DSURFACE_DESC desc;
    check(CHAIN(GetBackBuffer)(c, 0, D3DBACKBUFFER_TYPE_MONO, &surface) == D3D_OK && surface, "FAIL additional backbuffer");
    check(SURFACE(GetDesc)(surface, &desc) == D3D_OK && desc.Width == width && desc.Height == height, "FAIL additional retained creation geometry");
    SURFACE(Release)(surface);
}
static void additional_cases(HWND window)
{
    HWND other = CreateWindowExA(0, "STATIC", "additional parameters", WS_POPUP, 300, 40, 96, 80, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(other != NULL, "FAIL secondary window");
    ShowWindow(other, SW_SHOW);
    for (UINT kind = 0; kind < 2; kind++) {
        D3DPRESENT_PARAMETERS p;
        ZeroMemory(&p, sizeof(p));
        p.Windowed = TRUE;
        p.SwapEffect = D3DSWAPEFFECT_DISCARD;
        p.BackBufferFormat = D3DFMT_X8R8G8B8;
        p.hDeviceWindow = kind ? NULL : other;
        UINT width = kind ? 128 : 96, height = kind ? 96 : 80;
        Chain* c = NULL;
        check(DEV(CreateAdditionalSwapChain)(device, &p, &c) == D3D_OK && c, "FAIL additional normalized create");
        check_caller(&p, width, height, kind ? NULL : other);
        if (!kind)
            check(SetWindowPos(other, NULL, 300, 40, 160, 112, SWP_NOZORDER | SWP_NOACTIVATE), "FAIL secondary resize");
        check_buffer(c, width, height, kind ? window : other);
        CHAIN(Release)(c);
        check(refs((IUnknown*)device) == 1, "FAIL additional final device references");
        char line[200];
        wsprintfA(line, "ADDITIONAL PARAMETERS api=%u case=%u width=%u height=%u callerWidth=%u callerHeight=%u count=1 callerNull=%u cached=1", D9WG_IMPLICIT_API, kind, width, height, p.BackBufferWidth, p.BackBufferHeight, kind);
        report(line);
    }
    D3DPRESENT_PARAMETERS p;
    ZeroMemory(&p, sizeof(p));
    p.Windowed = TRUE;
    p.SwapEffect = D3DSWAPEFFECT_DISCARD;
    p.BackBufferFormat = D3DFMT_X8R8G8B8;
    p.BackBufferCount = 4;
    p.hDeviceWindow = other;
    D3DPRESENT_PARAMETERS before = p;
    Chain* c = (void*)(uintptr_t)0x1234;
    check(DEV(CreateAdditionalSwapChain)(device, &p, &c) == D3DERR_INVALIDCALL && !c, "FAIL oversized additional count");
    const BYTE* actual = (const BYTE*)&p;
    const BYTE* expected = (const BYTE*)&before;
    for (UINT i = 0; i < sizeof(p); i++)
        check(actual[i] == expected[i], "FAIL refused additional changed parameters");
    check(DEV(TestCooperativeLevel)(device) == D3D_OK && refs((IUnknown*)device) == 1, "FAIL refused additional affected device");
    Surface* implicit = NULL;
    D3DSURFACE_DESC desc;
#if D9WG_IMPLICIT_API == 8
    check(DEV(GetBackBuffer)(device, 0, D3DBACKBUFFER_TYPE_MONO, &implicit) == D3D_OK, "FAIL implicit after additional");
#else
    check(DEV(GetBackBuffer)(device, 0, 0, D3DBACKBUFFER_TYPE_MONO, &implicit) == D3D_OK, "FAIL implicit after additional");
#endif
    check(SURFACE(GetDesc)(implicit, &desc) == D3D_OK && desc.Width == 128 && desc.Height == 96, "FAIL additional changed implicit geometry");
    SURFACE(Release)(implicit);
    DestroyWindow(other);
    char line[180];
    wsprintfA(line, "ADDITIONAL STATUS api=%u accepted=2 refused=1 implicit=128x96 unchangedFailure=1 deviceRefs=1", D9WG_IMPLICIT_API);
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
    wsprintfA(name, "ACP%u.TXT", D9WG_IMPLICIT_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    RECT client = { 0, 0, 128, 96 };
    check(AdjustWindowRectEx(&client, WS_POPUP, FALSE, 0), "FAIL window dimensions");
    HWND window = CreateWindowExA(0, "STATIC", "D3D additional parameters probe", WS_POPUP, 30, 40, client.right - client.left, client.bottom - client.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(window != NULL, "FAIL window");
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    RECT actual;
    check(GetClientRect(window, &actual) && actual.right == 128 && actual.bottom == 96, "FAIL exact client dimensions");
    wsprintfA(name, "D3D%u.DLL", D9WG_IMPLICIT_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    HMODULE module = LoadLibraryA(path);
    check(module != NULL, "FAIL DLL");
    wsprintfA(name, "Direct3DCreate%u", D9WG_IMPLICIT_API);
    Factory*(WINAPI * make)(UINT) = (void*)(uintptr_t)GetProcAddress(module, name);
    check(make != NULL, "FAIL factory export");
    factory = make(D3D_SDK_VERSION);
    check(factory != NULL, "FAIL factory");
    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = NULL;
    pp.BackBufferWidth = 0;
    pp.BackBufferHeight = 0;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
#if D9WG_IMPLICIT_API == 8
    check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device8");
#else
    check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device9");
#endif
    check(DEV(TestCooperativeLevel)(device) == D3D_OK, "FAIL initial cooperative status");
    additional_cases(window);
    check(DEV(Release)(device) == 0, "FAIL device references");
    FACTORY_RELEASE(factory);
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS additional parameters API=%u", D9WG_IMPLICIT_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
