/* Validate presentation MSAA combinations, quality and versioned lock rules. */
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
static void refuse_buffer_locks(Surface* surface, UINT samples, UINT width, UINT height)
{
    D3DSURFACE_DESC desc;
    check(SURFACE(GetDesc)(surface, &desc) == D3D_OK && desc.Width == width && desc.Height == height && desc.MultiSampleType == (D3DMULTISAMPLE_TYPE)samples, "FAIL MSAA buffer metadata");
#if D9WG_IMPLICIT_API == 9
    check(desc.MultiSampleQuality == 0, "FAIL MSAA quality metadata");
#endif
    for (UINT read = 0; read < 2; read++) {
        D3DLOCKED_RECT lock;
        lock.Pitch = 0x3456;
        lock.pBits = (void*)(uintptr_t)0x1234;
        check(SURFACE(LockRect)(surface, &lock, NULL, read ? D3DLOCK_READONLY : 0) == D3DERR_INVALIDCALL && lock.Pitch == 0x3456 && lock.pBits == (void*)(uintptr_t)0x1234, "FAIL forbidden MSAA/render-target lock mutated output");
    }
}
static void multisample_cases(HWND window, const D3DPRESENT_PARAMETERS* base)
{
    D3DPRESENT_PARAMETERS p = *base;
    p.MultiSampleType = D3DMULTISAMPLE_4_SAMPLES;
#if D9WG_IMPLICIT_API == 8
    p.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
#endif
    Device* other = NULL;
    check(create_device(window, &p, &other) == D3D_OK && other, "FAIL valid MSAA constructor");
    check(DEV(Release)(other) == 0 && refs((IUnknown*)factory) == 2 && refs((IUnknown*)device) == 1, "FAIL valid MSAA constructor references");
    p = *base;
    p.MultiSampleType = D3DMULTISAMPLE_4_SAMPLES;
#if D9WG_IMPLICIT_API == 8
    p.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
#endif
    check(DEV(Reset)(device, &p) == D3D_OK && DEV(TestCooperativeLevel)(device) == D3D_OK, "FAIL valid MSAA Reset");
    Surface* surface = NULL;
#if D9WG_IMPLICIT_API == 8
    check(DEV(GetBackBuffer)(device, 0, D3DBACKBUFFER_TYPE_MONO, &surface) == D3D_OK && surface, "FAIL MSAA primary buffer8");
#else
    check(DEV(GetBackBuffer)(device, 0, 0, D3DBACKBUFFER_TYPE_MONO, &surface) == D3D_OK && surface, "FAIL MSAA primary buffer9");
#endif
    refuse_buffer_locks(surface, 4, 64, 64);
    SURFACE(Release)(surface);
    p = *base;
    p.BackBufferWidth = 32;
    p.BackBufferHeight = 24;
#if D9WG_IMPLICIT_API == 8
    p.MultiSampleType = D3DMULTISAMPLE_4_SAMPLES;
    p.Flags = D3DPRESENTFLAG_LOCKABLE_BACKBUFFER;
#endif
    Chain* chain = NULL;
    check(DEV(CreateAdditionalSwapChain)(device, &p, &chain) == D3D_OK && chain, "FAIL valid MSAA/ordinary additional chain");
    Surface* cached = NULL;
    check(CHAIN(GetBackBuffer)(chain, 0, D3DBACKBUFFER_TYPE_MONO, &surface) == D3D_OK && surface && CHAIN(GetBackBuffer)(chain, 0, D3DBACKBUFFER_TYPE_RIGHT, &cached) == D3D_OK && cached == surface, "FAIL MSAA cached buffer");
    SURFACE(Release)(cached);
    refuse_buffer_locks(surface, D9WG_IMPLICIT_API == 8 ? 4 : 0, 32, 24);
    SURFACE(Release)(surface);
    check(CHAIN(Release)(chain) == 0 && refs((IUnknown*)device) == 1, "FAIL MSAA chain cleanup");
    char line[190];
    wsprintfA(line, "MSAA ACCEPT api=%u constructorSamples=4 resetSamples=4 additionalSamples=%u flags=%u locksRefused=4", D9WG_IMPLICIT_API, D9WG_IMPLICIT_API == 8 ? 4 : 0, D9WG_IMPLICIT_API == 8 ? 1 : 0);
    report(line);
#if D9WG_IMPLICIT_API == 9
    p = *base;
    p.MultiSampleType = D3DMULTISAMPLE_4_SAMPLES;
    D3DPRESENT_PARAMETERS unsupported = p;
    chain = (void*)(uintptr_t)0x1234;
    check(DEV(CreateAdditionalSwapChain)(device, &p, &chain) == D3DERR_INVALIDCALL && !chain && same_parameters(&p, &unsupported) && refs((IUnknown*)device) == 1 && DEV(TestCooperativeLevel)(device) == D3D_OK, "FAIL retained additional MSAA refusal");
    report("MSAA ADDITIONAL api=9 samples=4 unsupported=1 unchanged=1 refs=1");
#endif
    const struct {UINT effect, samples, quality, flags;} bad[] = {
        {2,4,0,0}, {3,4,0,0},
#if D9WG_IMPLICIT_API == 8
        {4,4,0,0},
#else
        {1,4,1,0}, {1,0,1,0}, {1,4,0,1}, {1,4,0,3},
#endif
    };
    const UINT failures = sizeof(bad) / sizeof(*bad);
    for (UINT i = 0; i < failures; i++) {
        p = *base;
        p.SwapEffect = (D3DSWAPEFFECT)bad[i].effect;
        p.MultiSampleType = (D3DMULTISAMPLE_TYPE)bad[i].samples;
        p.Flags = bad[i].flags;
#if D9WG_IMPLICIT_API == 9
        p.MultiSampleQuality = bad[i].quality;
#endif
        D3DPRESENT_PARAMETERS before = p;
        other = (void*)(uintptr_t)0x1234;
        check(create_device(window, &p, &other) == D3DERR_INVALIDCALL && !other && same_parameters(&p, &before) && refs((IUnknown*)factory) == 2, "FAIL invalid MSAA constructor atomicity");
        chain = (void*)(uintptr_t)0x1234;
        check(DEV(CreateAdditionalSwapChain)(device, &p, &chain) == D3DERR_INVALIDCALL && !chain && same_parameters(&p, &before) && refs((IUnknown*)device) == 1 && DEV(TestCooperativeLevel)(device) == D3D_OK, "FAIL invalid MSAA additional atomicity");
        check(DEV(Reset)(device, &p) == D3DERR_INVALIDCALL && same_parameters(&p, &before) && refs((IUnknown*)device) == 1 && DEV(TestCooperativeLevel)(device) == D3DERR_DEVICENOTRESET, "FAIL invalid MSAA Reset status");
        check(DEV(Present)(device, NULL, NULL, NULL, NULL) == D3DERR_DEVICELOST, "FAIL invalid MSAA Reset lost Present");
        p = *base;
        check(DEV(Reset)(device, &p) == D3D_OK && DEV(TestCooperativeLevel)(device) == D3D_OK && refs((IUnknown*)device) == 1, "FAIL invalid MSAA Reset recovery");
        wsprintfA(line, "MSAA REFUSE api=%u effect=%u samples=%u quality=%u flags=%u routes=3 unchanged=1 resetLost=1 recovered=1", D9WG_IMPLICIT_API, bad[i].effect, bad[i].samples, bad[i].quality, bad[i].flags);
        report(line);
    }
    wsprintfA(line, "MSAA STATUS api=%u acceptedMatrixCalls=3 refusedMatrixCalls=%u successfulResets=%u extraUnsupported=%u locksRefused=4 deviceRefs=1", D9WG_IMPLICIT_API, failures * 3, failures + 1, D9WG_IMPLICIT_API == 9 ? 1 : 0);
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
    wsprintfA(name, "MSP%u.TXT", D9WG_IMPLICIT_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    RECT client = { 0, 0, 64, 64 };
    check(AdjustWindowRectEx(&client, WS_POPUP, FALSE, 0), "FAIL window dimensions");
    HWND window = CreateWindowExA(0, "STATIC", "D3D MSAA parameter probe", WS_POPUP, 30, 40, client.right - client.left, client.bottom - client.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
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
    pp.EnableAutoDepthStencil = FALSE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
#if D9WG_IMPLICIT_API == 8
    check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device8");
#else
    check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device9");
#endif
    check(DEV(TestCooperativeLevel)(device) == D3D_OK, "FAIL initial cooperative status");
    multisample_cases(window, &pp);
    check(DEV(Release)(device) == 0, "FAIL device references");
    check(FACTORY_RELEASE(factory) == 0, "FAIL final factory references");
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS MSAA validation API=%u", D9WG_IMPLICIT_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
