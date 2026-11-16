/* Present and revive true 4x additional buffers for both APIs. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <initguid.h>
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

static void present_frame(Chain* chain, UINT step, DWORD color)
{
#if D9WG_IMPLICIT_API == 8
    check(CHAIN(Present)(chain, NULL, NULL, NULL, NULL) == D3D_OK, "FAIL revived chain present8");
#else
    check(CHAIN(Present)(chain, NULL, NULL, NULL, NULL, 0) == D3D_OK, "FAIL revived chain present9");
#endif
    char line[140];
    wsprintfA(line, "MSCHAIN FRAME api=%u step=%u color=%08lX", D9WG_IMPLICIT_API, step, color);
    report(line);
}
static void revival_cases(const D3DPRESENT_PARAMETERS* base)
{
    check(refs((IUnknown*)factory) == 2 && refs((IUnknown*)device) == 1, "FAIL initial revival references");
#if D9WG_IMPLICIT_API == 8
    check(IDirect3D8_CheckDeviceMultiSampleType(factory, 0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, TRUE, D3DMULTISAMPLE_4_SAMPLES) == D3D_OK, "FAIL chain multisample capability8");
    report("MSCHAIN CAPS api=8 samples=4 available=1");
#else
    DWORD levels = 0;
    check(IDirect3D9_CheckDeviceMultiSampleType(factory, 0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, TRUE, D3DMULTISAMPLE_4_SAMPLES, &levels) == D3D_OK && levels == 1, "FAIL chain multisample capability/quality9");
    report("MSCHAIN CAPS api=9 samples=4 available=1 levels=1");
#endif

    HWND secondary = CreateWindowExA(0, "STATIC", "Revived additional chain", WS_POPUP,
        200, 40, 32, 24, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(secondary != NULL, "FAIL secondary revival window");
    ShowWindow(secondary, SW_SHOW);
    SetForegroundWindow(secondary);
    D3DPRESENT_PARAMETERS p = *base;
    p.BackBufferWidth = 32;
    p.BackBufferHeight = 24;
    p.hDeviceWindow = secondary;
    p.MultiSampleType = D3DMULTISAMPLE_4_SAMPLES;
    Chain* chain = NULL;
    Surface* surface = NULL;
    check(DEV(CreateAdditionalSwapChain)(device, &p, &chain) == D3D_OK && chain
            && refs((IUnknown*)device) == 2,
        "FAIL one revival chain device pin");
    check(CHAIN(GetBackBuffer)(chain, 0, D3DBACKBUFFER_TYPE_MONO, &surface) == D3D_OK
            && surface && refs((IUnknown*)surface) == 1 && refs((IUnknown*)device) == 3,
        "FAIL revival buffer public pin");
    D3DSURFACE_DESC desc;
    check(SURFACE(GetDesc)(surface, &desc) == D3D_OK && desc.Width == 32 && desc.Height == 24
            && desc.Format == D3DFMT_X8R8G8B8 && desc.MultiSampleType == p.MultiSampleType,
        "FAIL revival buffer geometry/samples");
#if D9WG_IMPLICIT_API == 9
    D3DPRESENT_PARAMETERS actual;
    check(SURFACE(GetDesc)(surface, &desc) == D3D_OK && desc.MultiSampleQuality == 0 && CHAIN(GetPresentParameters)(chain, &actual) == D3D_OK && actual.MultiSampleType == D3DMULTISAMPLE_4_SAMPLES && !actual.MultiSampleQuality, "FAIL cached multisample metadata9");
#endif
    char line[160];
    wsprintfA(line, "MSCHAIN BUFFER api=%u width=32 height=24 samples=%u", D9WG_IMPLICIT_API, (UINT)p.MultiSampleType);
    report(line);
    DWORD marker = 0x10293847;
    check(SURFACE(SetPrivateData)(surface, &key, &marker, 4, 0) == D3D_OK, "FAIL revival marker");
    tracker.iface.lpVtbl = &tracker_vtbl;
    tracker.refs = 1;
    check(SURFACE(SetPrivateData)(surface, &object_key, &tracker.iface, sizeof(IUnknown*), D3DSPD_IUNKNOWN) == D3D_OK
            && IUnknown_Release(&tracker.iface) == 1,
        "FAIL revival private object");
    set_target(surface);
    clear_target(0xffb02010);
    present_frame(chain, 0, 0xffb02010);
    for (UINT cycle = 0; cycle < 32; cycle++) {
        ULONG surface_refs = refs((IUnknown*)surface);
        check(CHAIN(Release)(chain) == 0 && refs((IUnknown*)device) == 2
                && tracker.refs == 1 && !tracker.finalized,
            "FAIL held buffer lost zero chain");
        Chain* revived = NULL;
#if D9WG_IMPLICIT_API == 8
        revived = chain;
        check(CHAIN(AddRef)(revived) == 1, "FAIL raw zero chain revival8");
#else
        check(SURFACE(GetContainer)(surface, &IID_IDirect3DSwapChain9, (void**)&revived) == D3D_OK
                && revived == chain,
            "FAIL zero chain container revival9");
#endif
        check(refs((IUnknown*)revived) == 1 && refs((IUnknown*)surface) == surface_refs
                && refs((IUnknown*)device) == 3,
            "FAIL revived chain independent references");
        IUnknown* identity = NULL;
        check(CHAIN(QueryInterface)(revived, &unknown_id, (void**)&identity) == D3D_OK
                && identity == (IUnknown*)chain && IUnknown_Release(identity) == 1,
            "FAIL revived chain IUnknown identity");
        Surface* again = NULL;
        check(CHAIN(GetBackBuffer)(revived, 0, D3DBACKBUFFER_TYPE_RIGHT, &again) == D3D_OK
                && again == surface,
            "FAIL revived chain cached buffer");
        SURFACE(Release)(again);
        DWORD value = 0, size = 4;
        check(SURFACE(GetPrivateData)(surface, &key, &value, &size) == D3D_OK && value == marker
                && refs((IUnknown*)surface) == surface_refs && refs((IUnknown*)device) == 3,
            "FAIL revived chain cached store/references");
    }
    wsprintfA(line, "MSCHAIN COUNTERS api=%u cycles=32 independent=1 cached=1 privateRetained=1", D9WG_IMPLICIT_API);
    report(line);
    clear_target(0xff1040c0);
    present_frame(chain, 1, 0xff1040c0);
    check(CHAIN(Release)(chain) == 0, "FAIL revived chain final public release");
    SURFACE(Release)(surface);
    check(tracker.refs == 1 && !tracker.finalized, "FAIL binding-only revival storage");
    Surface* bound = current_target();
    check(bound == surface, "FAIL binding-only cached revival view");
    SURFACE(Release)(bound);
    p = *base;
    check(DEV(Reset)(device, &p) == D3D_OK && DEV(TestCooperativeLevel)(device) == D3D_OK
            && refs((IUnknown*)device) == 1 && !tracker.refs && tracker.finalized == 1,
        "FAIL revival binding-only Reset/finalization");
    wsprintfA(line, "MSCHAIN CLEANUP api=%u bindingRetained=1 reset=1 privateFinalized=1 deviceRefs=1", D9WG_IMPLICIT_API);
    report(line);
    DestroyWindow(secondary);
}

void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], path[MAX_PATH], name[100];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    UINT at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    wsprintfA(name, "MSC%u.TXT", D9WG_IMPLICIT_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    RECT client = { 0, 0, 64, 64 };
    check(AdjustWindowRectEx(&client, WS_POPUP, FALSE, 0), "FAIL window dimensions");
    HWND window = CreateWindowExA(0, "STATIC", "D3D revived chain probe", WS_POPUP, 30, 40, client.right - client.left, client.bottom - client.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
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
    revival_cases(&pp);
    check(DEV(Release)(device) == 0, "FAIL device references");
    check(FACTORY_RELEASE(factory) == 0, "FAIL final revival factory references");
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS multisample chains API=%u", D9WG_IMPLICIT_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
