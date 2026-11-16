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
static const GUID key = { 0x71727374, 1, 2, { 3, 4, 5, 6, 7, 8, 9, 10 } }, object_key = { 0x81828384, 1, 2, { 3, 4, 5, 6, 7, 8, 9, 10 } }, unknown_id = { 0, 0, 0, { 0xc0, 0, 0, 0, 0, 0, 0, 0x46 } };
typedef struct {
    IUnknown iface;
    ULONG refs;
    UINT finalized;
} Tracker;
static Tracker tracker;
static HRESULT WINAPI tracker_query(IUnknown* iface, REFIID iid, void** out)
{
    if (!out)
        return E_POINTER;
    *out = NULL;
    if (!iid || iid->Data1 != unknown_id.Data1 || iid->Data2 != unknown_id.Data2 || iid->Data3 != unknown_id.Data3)
        return E_NOINTERFACE;
    for (UINT i = 0; i < 8; i++)
        if (iid->Data4[i] != unknown_id.Data4[i])
            return E_NOINTERFACE;
    *out = iface;
    IUnknown_AddRef(iface);
    return S_OK;
}
static ULONG WINAPI tracker_addref(IUnknown* iface) { return ++((Tracker*)iface)->refs; }
static ULONG WINAPI tracker_release(IUnknown* iface)
{
    Tracker* t = (Tracker*)iface;
    check(t->refs > 0, "FAIL private tracker underflow");
    if (!--t->refs)
        t->finalized++;
    return t->refs;
}
static IUnknownVtbl tracker_vtbl = { tracker_query, tracker_addref, tracker_release };
static void set_target(Surface* target)
{
#if D9WG_IMPLICIT_API == 8
    check(DEV(SetRenderTarget)(device, target, NULL) == D3D_OK, "FAIL select independent target");
#else
    check(DEV(SetRenderTarget)(device, 0, target) == D3D_OK && DEV(SetDepthStencilSurface)(device, NULL) == D3D_OK, "FAIL select independent target");
#endif
}
static Surface* current_target(void)
{
    Surface* out = NULL;
#if D9WG_IMPLICIT_API == 8
    check(DEV(GetRenderTarget)(device, &out) == D3D_OK && out, "FAIL current target");
#else
    check(DEV(GetRenderTarget)(device, 0, &out) == D3D_OK && out, "FAIL current target");
#endif
    return out;
}
static void clear_target(DWORD color) { check(DEV(Clear)(device, 0, NULL, D3DCLEAR_TARGET, color, 1, 0) == D3D_OK, "FAIL independent clear"); }
static void frame(Chain* chain, UINT step, UINT target, DWORD color)
{
    if (chain) {
#if D9WG_IMPLICIT_API == 8
        check(CHAIN(Present)(chain, NULL, NULL, NULL, NULL) == D3D_OK, "FAIL independent chain present");
#else
        check(CHAIN(Present)(chain, NULL, NULL, NULL, NULL, 0) == D3D_OK, "FAIL independent chain present");
#endif
    } else
        check(DEV(Present)(device, NULL, NULL, NULL, NULL) == D3D_OK, "FAIL independent primary present");
    char line[140];
    wsprintfA(line, "CHAIN FRAME api=%u step=%u target=%u color=%08lX", D9WG_IMPLICIT_API, step, target, color);
    report(line);
}
static Chain* make_chain(HWND window, UINT width, UINT height, D3DMULTISAMPLE_TYPE ms, Surface** surface)
{
    D3DPRESENT_PARAMETERS p;
    ZeroMemory(&p, sizeof(p));
    p.Windowed = TRUE;
    p.SwapEffect = D3DSWAPEFFECT_DISCARD;
    p.BackBufferFormat = D3DFMT_X8R8G8B8;
    p.BackBufferWidth = width;
    p.BackBufferHeight = height;
    p.hDeviceWindow = window;
    p.MultiSampleType = ms;
    Chain* c = NULL;
    Surface* again = NULL;
    D3DSURFACE_DESC desc;
    check(DEV(CreateAdditionalSwapChain)(device, &p, &c) == D3D_OK && c, "FAIL independent chain create");
    check(CHAIN(GetBackBuffer)(c, 0, D3DBACKBUFFER_TYPE_MONO, surface) == D3D_OK && *surface && CHAIN(GetBackBuffer)(c, 0, D3DBACKBUFFER_TYPE_LEFT, &again) == D3D_OK && again == *surface, "FAIL cached independent backbuffer identity");
    SURFACE(Release)(again);
    check(SURFACE(GetDesc)(*surface, &desc) == D3D_OK && desc.Width == width && desc.Height == height && desc.Format == D3DFMT_X8R8G8B8 && desc.MultiSampleType == ms, "FAIL independent backbuffer description");
    return c;
}
static void independent_cases(D3DPRESENT_PARAMETERS* base)
{
    check(refs((IUnknown*)factory) == 2, "FAIL active factory references");
    HWND first = CreateWindowExA(0, "STATIC", "chain A", WS_POPUP, 200, 40, 96, 64, NULL, NULL, GetModuleHandleA(NULL), NULL), second = CreateWindowExA(0, "STATIC", "chain B", WS_POPUP, 320, 40, 80, 96, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(first && second, "FAIL independent windows");
    ShowWindow(first, SW_SHOW);
    ShowWindow(second, SW_SHOW);
    Surface *primary = current_target(), *a = NULL, *b = NULL;
    Chain* ca = make_chain(first, 96, 64, D3DMULTISAMPLE_NONE, &a);
#if D9WG_IMPLICIT_API == 8
    const D3DMULTISAMPLE_TYPE samples = D3DMULTISAMPLE_4_SAMPLES;
#else
    const D3DMULTISAMPLE_TYPE samples = D3DMULTISAMPLE_NONE;
#endif
    Chain* cb = make_chain(second, 80, 96, samples, &b);
    check(a != b && a != primary && b != primary, "FAIL aliased chain targets");
    DWORD marker_a = 0x10293847, marker_b = 0x55667788, value = 0, size = 4;
    check(SURFACE(SetPrivateData)(a, &key, &marker_a, 4, 0) == D3D_OK && SURFACE(SetPrivateData)(b, &key, &marker_b, 4, 0) == D3D_OK, "FAIL independent private values");
    tracker.iface.lpVtbl = &tracker_vtbl;
    tracker.refs = 1;
    check(SURFACE(SetPrivateData)(a, &object_key, &tracker.iface, sizeof(IUnknown*), D3DSPD_IUNKNOWN) == D3D_OK && IUnknown_Release(&tracker.iface) == 1, "FAIL retained private tracker");
    char line[160];
    wsprintfA(line, "CHAIN BUFFER api=%u index=0 width=96 height=64 samples=0 cached=1", D9WG_IMPLICIT_API);
    report(line);
    wsprintfA(line, "CHAIN BUFFER api=%u index=1 width=80 height=96 samples=%u cached=1", D9WG_IMPLICIT_API, (UINT)samples);
    report(line);
    set_target(a);
    clear_target(0xffc02010);
    set_target(b);
    clear_target(0xff1030c0);
    set_target(primary);
    clear_target(0xff208040);
    frame(ca, 0, 1, 0xffc02010);
    frame(cb, 1, 2, 0xff1030c0);
    frame(NULL, 2, 0, 0xff208040);
    set_target(a);
    clear_target(0xffe06020);
    set_target(primary);
    frame(ca, 3, 1, 0xffe06020);
    frame(cb, 4, 2, 0xff1030c0);
    frame(NULL, 5, 0, 0xff208040);
    check(CHAIN(Release)(ca) == 0, "FAIL first chain release");
    size = 4;
    check(SURFACE(GetPrivateData)(a, &key, &value, &size) == D3D_OK && value == marker_a && tracker.refs == 1 && !tracker.finalized, "FAIL released chain lost live surface store");
    set_target(a);
    clear_target(0xffd0d020);
    Surface* bound = current_target();
    check(bound == a, "FAIL bound backbuffer cache identity");
    SURFACE(Release)(bound);
    SURFACE(Release)(a);
    check(tracker.refs == 1 && !tracker.finalized, "FAIL binding failed to retain private store");
    set_target(primary);
    check(!tracker.refs && tracker.finalized == 1, "FAIL last binding retained private store");
    frame(NULL, 6, 0, 0xff208040);
    size = 4;
    check(SURFACE(GetPrivateData)(b, &key, &value, &size) == D3D_OK && value == marker_b, "FAIL sibling private store changed");
    SURFACE(Release)(b);
    check(CHAIN(Release)(cb) == 0, "FAIL second chain release");
    SURFACE(Release)(primary);
    D3DPRESENT_PARAMETERS p = *base;
    check(DEV(Reset)(device, &p) == D3D_OK && DEV(TestCooperativeLevel)(device) == D3D_OK, "FAIL independent-chain cleanup Reset");
    wsprintfA(line, "CHAIN LIFETIME api=%u cached=1 isolatedPrivate=1 chainReleasedFirst=1 bindingRetained=1 privateFinalized=1 resetRecovered=1", D9WG_IMPLICIT_API);
    report(line);
    DestroyWindow(first);
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
    wsprintfA(name, "IND%u.TXT", D9WG_IMPLICIT_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    RECT client = { 0, 0, 128, 128 };
    check(AdjustWindowRectEx(&client, WS_POPUP, FALSE, 0), "FAIL window dimensions");
    HWND window = CreateWindowExA(0, "STATIC", "D3D independent chains probe", WS_POPUP, 30, 40, client.right - client.left, client.bottom - client.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(window != NULL, "FAIL window");
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    RECT actual;
    check(GetClientRect(window, &actual) && actual.right == 128 && actual.bottom == 128, "FAIL exact client dimensions");
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
    pp.EnableAutoDepthStencil = FALSE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
#if D9WG_IMPLICIT_API == 8
    check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device8");
#else
    check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device9");
#endif
    check(DEV(TestCooperativeLevel)(device) == D3D_OK, "FAIL initial cooperative status");
    independent_cases(&pp);
    check(DEV(Release)(device) == 0, "FAIL device references");
    FACTORY_RELEASE(factory);
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS independent chains API=%u", D9WG_IMPLICIT_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
