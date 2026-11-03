/* Validate swap effects and COPY counts across all three SDK entry points. */
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
static BOOL same_parameters(const D3DPRESENT_PARAMETERS* a, const D3DPRESENT_PARAMETERS* b)
{
    const BYTE* aa = (const BYTE*)a;
    const BYTE* bb = (const BYTE*)b;
    for (UINT i = 0; i < sizeof(*a); i++)
        if (aa[i] != bb[i])
            return FALSE;
    return TRUE;
}
static HRESULT create_device(HWND window, D3DPRESENT_PARAMETERS* p, Device** out)
{
#if D9WG_IMPLICIT_API == 8
    return IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, p, out);
#else
    return IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, p, out);
#endif
}
static void swap_effect_cases(HWND window, const D3DPRESENT_PARAMETERS* base)
{
    const UINT last = D9WG_IMPLICIT_API == 8 ? 4 : 3;
    char line[180];
    for (UINT effect = 1; effect <= last; effect++) {
        D3DPRESENT_PARAMETERS p = *base;
        p.SwapEffect = (D3DSWAPEFFECT)effect;
        p.BackBufferCount = 0;
        Device* other = NULL;
        check(create_device(window, &p, &other) == D3D_OK && other && p.BackBufferCount == 1 && p.SwapEffect == (D3DSWAPEFFECT)effect, "FAIL accepted swap-effect constructor");
        check(DEV(Release)(other) == 0 && refs((IUnknown*)factory) == 2 && refs((IUnknown*)device) == 1, "FAIL swap-effect constructor references");
        p = *base;
        p.SwapEffect = (D3DSWAPEFFECT)effect;
        p.BackBufferCount = 0;
        check(DEV(Reset)(device, &p) == D3D_OK && DEV(TestCooperativeLevel)(device) == D3D_OK && p.BackBufferCount == 1 && p.SwapEffect == (D3DSWAPEFFECT)effect, "FAIL accepted swap-effect Reset");
        p = *base;
        p.SwapEffect = (D3DSWAPEFFECT)effect;
        p.BackBufferCount = 0;
        p.BackBufferWidth = 32;
        p.BackBufferHeight = 24;
        Chain* chain = NULL;
        check(DEV(CreateAdditionalSwapChain)(device, &p, &chain) == D3D_OK && chain && p.BackBufferCount == 1 && p.SwapEffect == (D3DSWAPEFFECT)effect, "FAIL accepted swap-effect additional chain");
        Surface* surface = NULL;
        Surface* cached = NULL;
        check(CHAIN(GetBackBuffer)(chain, 0, D3DBACKBUFFER_TYPE_MONO, &surface) == D3D_OK && surface && CHAIN(GetBackBuffer)(chain, 0, D3DBACKBUFFER_TYPE_RIGHT, &cached) == D3D_OK && cached == surface, "FAIL swap-effect cached backbuffer");
        D3DSURFACE_DESC desc;
        check(SURFACE(GetDesc)(surface, &desc) == D3D_OK && desc.Width == 32 && desc.Height == 24 && desc.Format == D3DFMT_X8R8G8B8, "FAIL swap-effect buffer geometry");
#if D9WG_IMPLICIT_API == 9
        D3DPRESENT_PARAMETERS actual;
        check(CHAIN(GetPresentParameters)(chain, &actual) == D3D_OK && actual.SwapEffect == (D3DSWAPEFFECT)effect && actual.BackBufferCount == 1, "FAIL cached swap-effect parameters");
#endif
        SURFACE(Release)(cached);
        SURFACE(Release)(surface);
        check(CHAIN(Release)(chain) == 0 && refs((IUnknown*)device) == 1, "FAIL swap-effect chain final references");
        wsprintfA(line, "SWAP ACCEPT api=%u effect=%u count=1 constructor=1 reset=1 additional=1 cached=1", D9WG_IMPLICIT_API, effect);
        report(line);
    }
    const struct {UINT effect, count;} bad[] = {{0,1},{5,1},{0xffffffffu,1},{3,2},{3,3},{4,2},{4,1}};
    const UINT failures = D9WG_IMPLICIT_API == 8 ? 6 : 7;
    for (UINT i = 0; i < failures; i++) {
        D3DPRESENT_PARAMETERS p = *base;
        p.SwapEffect = (D3DSWAPEFFECT)bad[i].effect;
        p.BackBufferCount = bad[i].count;
        D3DPRESENT_PARAMETERS before = p;
        Device* other = (void*)(uintptr_t)0x1234;
        check(create_device(window, &p, &other) == D3DERR_INVALIDCALL && !other && same_parameters(&p, &before) && refs((IUnknown*)factory) == 2, "FAIL invalid swap-effect constructor atomicity");
        Chain* chain = (void*)(uintptr_t)0x1234;
        check(DEV(CreateAdditionalSwapChain)(device, &p, &chain) == D3DERR_INVALIDCALL && !chain && same_parameters(&p, &before) && refs((IUnknown*)device) == 1 && DEV(TestCooperativeLevel)(device) == D3D_OK, "FAIL invalid swap-effect additional atomicity");
        check(DEV(Reset)(device, &p) == D3DERR_INVALIDCALL && same_parameters(&p, &before) && refs((IUnknown*)device) == 1 && DEV(TestCooperativeLevel)(device) == D3DERR_DEVICENOTRESET, "FAIL invalid swap-effect Reset status");
        check(DEV(Present)(device, NULL, NULL, NULL, NULL) == D3DERR_DEVICELOST, "FAIL invalid Reset lost Present");
        p = *base;
        check(DEV(Reset)(device, &p) == D3D_OK && DEV(TestCooperativeLevel)(device) == D3D_OK && refs((IUnknown*)device) == 1, "FAIL invalid swap-effect Reset recovery");
        wsprintfA(line, "SWAP REFUSE api=%u effect=%u count=%u routes=3 unchanged=1 nullOutputs=1 resetLost=1 recovered=1", D9WG_IMPLICIT_API, bad[i].effect, bad[i].count);
        report(line);
    }
    wsprintfA(line, "SWAP STATUS api=%u accepted=%u refused=%u successfulResets=10 cached=1 deviceRefs=1 factoryRefs=2", D9WG_IMPLICIT_API, last * 3, failures * 3);
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
    wsprintfA(name, "SFX%u.TXT", D9WG_IMPLICIT_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    RECT client = { 0, 0, 64, 64 };
    check(AdjustWindowRectEx(&client, WS_POPUP, FALSE, 0), "FAIL window dimensions");
    HWND window = CreateWindowExA(0, "STATIC", "D3D swap-effect probe", WS_POPUP, 30, 40, client.right - client.left, client.bottom - client.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
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
    swap_effect_cases(window, &pp);
    check(DEV(Release)(device) == 0, "FAIL device references");
    check(FACTORY_RELEASE(factory) == 0, "FAIL final factory references");
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS swap-effect validation API=%u", D9WG_IMPLICIT_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
