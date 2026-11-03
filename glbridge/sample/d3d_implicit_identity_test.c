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
static const GUID unknown_id = { 0, 0, 0, { 0xc0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID key = { 0x10293847, 1, 2, { 3, 4, 5, 6, 7, 8, 9, 10 } };
static ULONG refs(IUnknown* object)
{
    IUnknown_AddRef(object);
    return IUnknown_Release(object);
}
static HRESULT get_surface(UINT kind, DWORD type, Surface** out)
{
    if (kind)
        return DEV(GetDepthStencilSurface)(device, out);
#if D9WG_IMPLICIT_API == 8
    return DEV(GetBackBuffer)(device, 0, (D3DBACKBUFFER_TYPE)type, out);
#else
    return DEV(GetBackBuffer)(device, 0, 0, (D3DBACKBUFFER_TYPE)type, out);
#endif
}
static HRESULT get_target(Surface** out)
{
#if D9WG_IMPLICIT_API == 8
    return DEV(GetRenderTarget)(device, out);
#else
    return DEV(GetRenderTarget)(device, 0, out);
#endif
}
static void implicit_cases(D3DPRESENT_PARAMETERS* base)
{
    for (UINT kind = 0; kind < 2; kind++) {
        Surface *a = NULL, *b = NULL;
        check(refs((IUnknown*)device) == 1, "FAIL device baseline count");
        check(get_surface(kind, 0, &a) == D3D_OK && a && refs((IUnknown*)a) == 1 && refs((IUnknown*)device) == 2, "FAIL initial implicit count");
        check(get_surface(kind, 0, &b) == D3D_OK && b == a && refs((IUnknown*)a) == 2 && refs((IUnknown*)device) == 2, "FAIL repeated implicit identity/pin");
        check(SURFACE(AddRef)(a) == 3 && refs((IUnknown*)device) == 2, "FAIL repeated implicit AddRef pin");
        check(SURFACE(Release)(a) == 2 && SURFACE(Release)(b) == 1 && refs((IUnknown*)device) == 2, "FAIL intermediate implicit Release pin");
        IUnknown *identity = NULL, *container = NULL;
        check(SURFACE(QueryInterface)(a, &unknown_id, (void**)&identity) == S_OK && identity == (IUnknown*)a && refs((IUnknown*)device) == 2 && IUnknown_Release(identity) == 1, "FAIL implicit IUnknown");
        check(SURFACE(GetContainer)(a, &unknown_id, (void**)&container) == D3D_OK && container, "FAIL implicit container");
#if D9WG_IMPLICIT_API == 9
        if (!kind) {
            Chain* implicit = NULL;
            check(DEV(GetSwapChain)(device, 0, &implicit) == D3D_OK && container == (IUnknown*)implicit, "FAIL back buffer swap-chain container");
            CHAIN(Release)(implicit);
        } else
#endif
            check(container == (IUnknown*)device, "FAIL implicit device container");
        IUnknown_Release(container);
        check(refs((IUnknown*)device) == 2, "FAIL container reference balance");
        DWORD value = 0x55667788u + kind, size = 4, out = 0;
        check(SURFACE(SetPrivateData)(a, &key, &value, 4, 0) == D3D_OK, "FAIL implicit private set");
        check(SURFACE(Release)(a) == 0 && refs((IUnknown*)device) == 1, "FAIL implicit final public release");
        check(SURFACE(Release)(a) == 0 && SURFACE(Release)(a) == 0 && refs((IUnknown*)device) == 1, "FAIL repeated zero release");
        check(SURFACE(AddRef)(a) == 1 && refs((IUnknown*)device) == 2 && SURFACE(Release)(a) == 0 && refs((IUnknown*)device) == 1, "FAIL zero-reference revival");
        check(get_surface(kind, 0, &b) == D3D_OK && b == a && SURFACE(GetPrivateData)(b, &key, &out, &size) == D3D_OK && out == value && SURFACE(Release)(b) == 0, "FAIL reacquired implicit identity/data");
        if (!kind) {
            const DWORD types[] = { 0, 1, 2, 0xdeadbeefu };
            for (UINT i = 0; i < 4; i++)
                check(get_surface(0, types[i], &b) == D3D_OK && b == a && SURFACE(Release)(b) == 0, "FAIL ignored stereo type");
            check(get_target(&b) == D3D_OK && b == a && SURFACE(Release)(b) == 0, "FAIL render-target/back-buffer identity");
        }
        char line[128];
        wsprintfA(line, "IMPLICIT %u kind=%u count=0 pin=1 live=1 revived=1 stereo=%u container=1", D9WG_IMPLICIT_API, kind, kind ? 0 : 1);
        report(line);
    }
    Surface *back = NULL, *depth = NULL;
    check(get_surface(0, 0, &back) == D3D_OK && get_surface(1, 0, &depth) == D3D_OK, "FAIL bound implicit setup");
#if D9WG_IMPLICIT_API == 8
    check(DEV(SetRenderTarget)(device, back, depth) == D3D_OK, "FAIL default target bind");
#else
    check(DEV(SetRenderTarget)(device, 0, back) == D3D_OK && DEV(SetDepthStencilSurface)(device, depth) == D3D_OK, "FAIL default target bind");
#endif
    SURFACE(Release)(back);
    SURFACE(Release)(depth);
    D3DPRESENT_PARAMETERS p = *base;
    p.BackBufferWidth = 80;
    p.BackBufferHeight = 72;
    check(DEV(Reset)(device, &p) == D3D_OK && refs((IUnknown*)device) == 1, "FAIL Reset after only device-owned implicit refs");
    for (UINT kind = 0; kind < 2; kind++) {
        Surface* a = NULL;
        D3DSURFACE_DESC desc;
        check(get_surface(kind, 0, &a) == D3D_OK && SURFACE(GetDesc)(a, &desc) == D3D_OK && desc.Width == 80 && desc.Height == 72 && desc.MultiSampleType == D3DMULTISAMPLE_4_SAMPLES && desc.Format == (kind ? D3DFMT_D24S8 : D3DFMT_X8R8G8B8), "FAIL resized cached description");
        check(SURFACE(Release)(a) == 0 && refs((IUnknown*)device) == 1, "FAIL resized implicit pin");
    }
    char line[128];
    wsprintfA(line, "RESET IMPLICIT %u width=80 height=72 samples=4 owned=1", D9WG_IMPLICIT_API);
    report(line);
    check(get_surface(0, 0, &back) == D3D_OK && get_surface(1, 0, &depth) == D3D_OK && refs((IUnknown*)device) == 3, "FAIL final implicit acquire");
    check(DEV(Release)(device) == 2 && SURFACE(Release)(back) == 0, "FAIL device released before surfaces");
    check(SURFACE(AddRef)(depth) == 2 && SURFACE(Release)(depth) == 1 && SURFACE(Release)(depth) == 0, "FAIL final surface releases device");
    wsprintfA(line, "FINAL IMPLICIT %u device=2 back=0 depth=0", D9WG_IMPLICIT_API);
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
    wsprintfA(name, "IMP%u.TXT", D9WG_IMPLICIT_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    RECT client = { 0, 0, 64, 64 };
    check(AdjustWindowRectEx(&client, WS_POPUP, FALSE, 0), "FAIL window dimensions");
    HWND window = CreateWindowExA(0, "STATIC", "D3D implicit surface identity probe", WS_POPUP, 30, 40, client.right - client.left, client.bottom - client.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
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
    pp.MultiSampleType = D3DMULTISAMPLE_4_SAMPLES;
#if D9WG_IMPLICIT_API == 8
    check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device8");
#else
    check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device9");
#endif
    implicit_cases(&pp);
    FACTORY_RELEASE(factory);
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS implicit API=%u", D9WG_IMPLICIT_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
