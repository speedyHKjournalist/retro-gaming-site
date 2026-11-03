/* One parent device pin per standalone surface, exact teardown and callback order. */
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
    ULONG expected_device_refs, observed_device_refs;
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
    if (!--t->refs) {
        t->finalized++;
        t->observed_device_refs = refs((IUnknown*)device);
        check(t->observed_device_refs == t->expected_device_refs, "FAIL private finalizer parent pin order");
    }
    return t->refs;
}
static IUnknownVtbl tracker_vtbl = { tracker_query, tracker_addref, tracker_release };
static HRESULT create_device(HWND window, D3DPRESENT_PARAMETERS* p, Device** out)
{
    return IDirect3D9_CreateDevice(factory,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE,p,out);
}
static Surface* create_profile(UINT kind, UINT width, UINT height)
{
    Surface* surface = NULL;
    HRESULT hr;
    if (kind < 2)
        hr = DEV(CreateRenderTarget)(device,width,height,D3DFMT_X8R8G8B8,kind ? D3DMULTISAMPLE_4_SAMPLES : D3DMULTISAMPLE_NONE,0,FALSE,&surface,NULL);
    else if (kind < 4)
        hr = DEV(CreateDepthStencilSurface)(device,width,height,D3DFMT_D24S8,kind == 3 ? D3DMULTISAMPLE_4_SAMPLES : D3DMULTISAMPLE_NONE,0,TRUE,&surface,NULL);
    else
        hr = DEV(CreateOffscreenPlainSurface)(device,width,height,D3DFMT_X8R8G8B8,kind == 4 ? D3DPOOL_DEFAULT : kind == 5 ? D3DPOOL_SYSTEMMEM : D3DPOOL_SCRATCH,&surface,NULL);
    check(hr == D3D_OK && surface && refs((IUnknown*)surface) == 1 && refs((IUnknown*)device) == 2, "FAIL exactly one standalone device pin");
    D3DSURFACE_DESC desc;
    UINT samples = kind == 1 || kind == 3 ? 4 : 0;
    check(SURFACE(GetDesc)(surface,&desc) == D3D_OK && desc.Width == width && desc.Height == height && desc.MultiSampleType == (D3DMULTISAMPLE_TYPE)samples, "FAIL standalone lifetime metadata");
    return surface;
}
static void attach_private(Surface* surface, ULONG expected_refs, DWORD marker)
{
    ZeroMemory(&tracker,sizeof(tracker));
    tracker.iface.lpVtbl = &tracker_vtbl;
    tracker.refs = 1;
    tracker.expected_device_refs = expected_refs;
    check(SURFACE(SetPrivateData)(surface,&object_key,&tracker.iface,sizeof(IUnknown*),D3DSPD_IUNKNOWN) == D3D_OK && IUnknown_Release(&tracker.iface) == 1 && !tracker.finalized, "FAIL standalone private tracker attach");
    check(SURFACE(SetPrivateData)(surface,&key,&marker,sizeof(marker),0) == D3D_OK, "FAIL standalone private marker");
}
static void target_cases(HWND window, const D3DPRESENT_PARAMETERS* base)
{
    check(refs((IUnknown*)device) == 1 && refs((IUnknown*)factory) == 2, "FAIL target lifetime initial roots");
    char line[190];
    for (UINT kind = 0; kind < 7; kind++) {
        Surface* surface = create_profile(kind,32,24);
        attach_private(surface,2,0x17283940 + kind);
        Device* queried = NULL;
        check(SURFACE(GetDevice)(surface,&queried) == D3D_OK && queried == device && refs((IUnknown*)device) == 3 && DEV(Release)(queried) == 2, "FAIL standalone GetDevice query reference");
        for (UINT query = 0; query < 2; query++) {
            IUnknown* container = NULL;
            check(SURFACE(GetContainer)(surface,query ? &IID_IDirect3DDevice9 : &unknown_id,(void**)&container) == D3D_OK && container == (IUnknown*)device && refs((IUnknown*)device) == 3 && IUnknown_Release(container) == 2, "FAIL standalone device container query reference");
        }
        for (UINT cycle = 0; cycle < 16; cycle++) {
            check(SURFACE(AddRef)(surface) == 2 && refs((IUnknown*)device) == 2 && SURFACE(Release)(surface) == 1 && refs((IUnknown*)device) == 2 && tracker.refs == 1 && !tracker.finalized, "FAIL standalone public AddRef parent pin drift");
            DWORD value = 0, size = sizeof(value);
            check(SURFACE(GetPrivateData)(surface,&key,&value,&size) == D3D_OK && value == 0x17283940 + kind && size == 4, "FAIL standalone cached private data");
        }
        check(SURFACE(Release)(surface) == 0 && refs((IUnknown*)device) == 1 && !tracker.refs && tracker.finalized == 1 && tracker.observed_device_refs == 2, "FAIL standalone final cleanup/pending device pin");
        wsprintfA(line,"TARGET PROFILE kind=%u samples=%u cycles=16 deviceRefs=2 surfaceRefs=1 queryRefs=3 privateFinalized=1 callbackRefs=2 remainingDeviceRefs=1",kind,kind == 1 || kind == 3 ? 4 : 0);
        report(line);
    }
    check(DEV(Release)(device) == 0 && refs((IUnknown*)factory) == 1, "FAIL main standalone device cleanup");
    device = NULL;
    const UINT last_profiles[] = {1,2,4};
    for (UINT i = 0; i < 3; i++) {
        D3DPRESENT_PARAMETERS p = *base;
        check(create_device(window,&p,&device) == D3D_OK && device && refs((IUnknown*)device) == 1, "FAIL last-view device create");
        Surface* surface = create_profile(last_profiles[i],32,24);
        attach_private(surface,1,0x28394050 + i);
        check(DEV(Release)(device) == 1 && tracker.refs == 1 && !tracker.finalized, "FAIL application-first device pin");
        Device* queried = NULL;
        check(SURFACE(GetDevice)(surface,&queried) == D3D_OK && queried == device && refs((IUnknown*)queried) == 2 && DEV(Release)(queried) == 1, "FAIL held-view device query");
        check(SURFACE(Release)(surface) == 0 && !tracker.refs && tracker.finalized == 1 && tracker.observed_device_refs == 1 && refs((IUnknown*)factory) == 1, "FAIL last-view storage/device order");
        device = NULL;
        wsprintfA(line,"TARGET LASTVIEW kind=%u applicationDeviceRefs=1 queryRefs=2 callbackRefs=1 privateFinalized=1 factoryRefs=1",last_profiles[i]);
        report(line);
    }
    const UINT bound_profiles[] = {1,2};
    for (UINT i = 0; i < 2; i++) {
        D3DPRESENT_PARAMETERS p = *base;
        check(create_device(window,&p,&device) == D3D_OK && device, "FAIL binding-only device create");
        Surface* surface = create_profile(bound_profiles[i],i ? 64 : 32,i ? 64 : 24);
        attach_private(surface,1,0x39405060 + i);
        check((i ? DEV(SetDepthStencilSurface)(device,surface) : DEV(SetRenderTarget)(device,0,surface)) == D3D_OK && refs((IUnknown*)device) == 2 && SURFACE(Release)(surface) == 1 && tracker.refs == 1 && !tracker.finalized, "FAIL binding-only target retention");
        check(DEV(Release)(device) == 0 && !tracker.refs && tracker.finalized == 1 && tracker.observed_device_refs == 1 && refs((IUnknown*)factory) == 1, "FAIL binding-only target/device cleanup");
        device = NULL;
        wsprintfA(line,"TARGET BOUND kind=%u callbackRefs=1 privateFinalized=1 deviceRefs=0 factoryRefs=1",bound_profiles[i]);
        report(line);
    }
    report("TARGET STATUS profiles=7 devices=6 privateFinalized=12 oneParentPin=1 deviceFirst=3 bindingOnly=2 factoryRefs=1");
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], path[MAX_PATH], name[100];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    UINT at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    wsprintfA(name, "TGT%u.TXT", D9WG_IMPLICIT_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    RECT client = { 0, 0, 64, 64 };
    check(AdjustWindowRectEx(&client, WS_POPUP, FALSE, 0), "FAIL window dimensions");
    HWND window = CreateWindowExA(0, "STATIC", "D3D9 standalone target lifetime", WS_POPUP, 30, 40, client.right - client.left, client.bottom - client.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
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
    target_cases(window,&pp);
    check(!device && refs((IUnknown*)factory) == 1, "FAIL final target roots");
    check(FACTORY_RELEASE(factory) == 0, "FAIL final revival factory references");
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS target lifetime API=%u", D9WG_IMPLICIT_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
