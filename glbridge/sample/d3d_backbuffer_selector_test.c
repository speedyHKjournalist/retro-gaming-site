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
static void check_route(Chain* chain, UINT route, UINT width)
{
    const DWORD types[] = { 0, 1, 2, 0xdeadbeef, 0xffffffff };
    ULONG before = refs((IUnknown*)device);
    for (UINT i = 0; i < 5; i++) {
        check(CHAIN(GetBackBuffer)(chain, 0, (D3DBACKBUFFER_TYPE)types[i], NULL) == D3DERR_INVALIDCALL, "FAIL NULL backbuffer output");
        for (UINT n = 0; n < 2; n++) {
            Surface* out = (void*)(uintptr_t)0x1234;
            check(CHAIN(GetBackBuffer)(chain, n ? 0xffffffffu : 1, (D3DBACKBUFFER_TYPE)types[i], &out) == D3DERR_INVALIDCALL && out == (void*)(uintptr_t)0x1234, "FAIL bad index changed output");
        }
        Surface* out = NULL;
        check(CHAIN(GetBackBuffer)(chain, 0, (D3DBACKBUFFER_TYPE)types[i], &out) == D3D_OK && out, "FAIL ignored backbuffer stereo type");
        D3DSURFACE_DESC desc;
        check(SURFACE(GetDesc)(out, &desc) == D3D_OK && desc.Width == width && desc.Height == 64, "FAIL selected backbuffer geometry");
        SURFACE(Release)(out);
        check(refs((IUnknown*)device) == before, "FAIL selector device reference balance");
    }
    char line[180];
    wsprintfA(line, "BACKBUFFER SELECTOR api=%u route=%u width=%u height=64 valid=5 invalid=10 null=5 preserved=10", D9WG_IMPLICIT_API, route, width);
    report(line);
}
static void selector_cases(D3DPRESENT_PARAMETERS* base)
{
    D3DPRESENT_PARAMETERS p = *base;
    p.BackBufferWidth = 96;
    p.BackBufferHeight = 64;
    p.EnableAutoDepthStencil = FALSE;
    Chain* chain = NULL;
    check(DEV(CreateAdditionalSwapChain)(device, &p, &chain) == D3D_OK && chain, "FAIL additional selector chain create");
    check_route(chain, 0, 96);
    CHAIN(Release)(chain);
    UINT routes = 1;
#if D9WG_IMPLICIT_API == 9
    check(DEV(GetSwapChain)(device, 0, &chain) == D3D_OK && chain, "FAIL implicit selector chain");
    check_route(chain, 1, 64);
    CHAIN(Release)(chain);
    routes++;
#endif
    check(refs((IUnknown*)device) == 1, "FAIL selector final device references");
    char line[160];
    wsprintfA(line, "SELECTOR STATUS api=%u routes=%u valid=%u invalid=%u null=%u preserved=%u deviceRefs=1", D9WG_IMPLICIT_API, routes, routes * 5, routes * 10, routes * 5, routes * 10);
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
    wsprintfA(name, "SEL%u.TXT", D9WG_IMPLICIT_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    RECT client = { 0, 0, 64, 64 };
    check(AdjustWindowRectEx(&client, WS_POPUP, FALSE, 0), "FAIL window dimensions");
    HWND window = CreateWindowExA(0, "STATIC", "D3D backbuffer selector probe", WS_POPUP, 30, 40, client.right - client.left, client.bottom - client.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(window != NULL, "FAIL window");
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    RECT actual;
    check(GetClientRect(window, &actual) && actual.right == 64 && actual.bottom == 64, "FAIL exact client dimensions");
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
    pp.hDeviceWindow = window;
    pp.BackBufferWidth = 64;
    pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
#if D9WG_IMPLICIT_API == 8
    check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device8");
#else
    check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device9");
#endif
    check(DEV(TestCooperativeLevel)(device) == D3D_OK, "FAIL initial cooperative status");
    selector_cases(&pp);
    check(DEV(Release)(device) == 0, "FAIL device references");
    FACTORY_RELEASE(factory);
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS backbuffer selectors API=%u", D9WG_IMPLICIT_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
