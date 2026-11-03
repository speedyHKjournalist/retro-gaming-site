/* D3D8 public-zero standalone target bindings and private storage lifetime. */
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
#include <d3d8.h>
typedef IDirect3D8 Factory;
typedef IDirect3DDevice8 Device;
typedef IDirect3DSwapChain8 Chain;
#define CHAIN(method) IDirect3DSwapChain8_##method
#define DEV(method) IDirect3DDevice8_##method
#define FACTORY_RELEASE IDirect3D8_Release
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
typedef IDirect3DSurface8 Surface;
#define SURFACE(m) IDirect3DSurface8_##m
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
static Tracker trackers[2];
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
    return IDirect3D8_CreateDevice(factory,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE,p,out);
}
static Surface* create_profile(UINT kind, UINT width, UINT height)
{
    ULONG device_before = refs((IUnknown*)device);
    Surface* surface = NULL;
    HRESULT hr;
    if (kind < 2)
        hr = DEV(CreateRenderTarget)(device,width,height,D3DFMT_X8R8G8B8,kind ? D3DMULTISAMPLE_4_SAMPLES : D3DMULTISAMPLE_NONE,FALSE,&surface);
    else if (kind < 4)
        hr = DEV(CreateDepthStencilSurface)(device,width,height,D3DFMT_D24S8,kind == 3 ? D3DMULTISAMPLE_4_SAMPLES : D3DMULTISAMPLE_NONE,&surface);
    else
        hr = DEV(CreateImageSurface)(device,width,height,D3DFMT_X8R8G8B8,&surface);
    check(hr == D3D_OK && surface && refs((IUnknown*)surface) == 1 && refs((IUnknown*)device) == device_before + 1, "FAIL exactly one standalone device pin");
    D3DSURFACE_DESC desc;
    UINT samples = kind == 1 || kind == 3 ? 4 : 0;
    check(SURFACE(GetDesc)(surface,&desc) == D3D_OK && desc.Width == width && desc.Height == height && desc.MultiSampleType == (D3DMULTISAMPLE_TYPE)samples, "FAIL standalone lifetime metadata");
    return surface;
}
static void attach_private(Tracker* t, Surface* surface, ULONG expected_refs, DWORD marker)
{
    ZeroMemory(t,sizeof(*t));
    t->iface.lpVtbl = &tracker_vtbl;
    t->refs = 1;
    t->expected_device_refs = expected_refs;
    check(SURFACE(SetPrivateData)(surface,&object_key,&t->iface,sizeof(IUnknown*),D3DSPD_IUNKNOWN) == D3D_OK && IUnknown_Release(&t->iface) == 1 && !t->finalized, "FAIL bound private tracker attach");
    check(SURFACE(SetPrivateData)(surface,&key,&marker,sizeof(marker),0) == D3D_OK, "FAIL bound private marker");
}
static void verify_revival(Surface* target, Surface* depth)
{
    Surface *a = NULL, *b = NULL;
    check(DEV(GetRenderTarget)(device,&a) == D3D_OK && a == target && DEV(GetDepthStencilSurface)(device,&b) == D3D_OK && b == depth, "FAIL bound zero-public cache identity");
    check(refs((IUnknown*)a) == 1 && refs((IUnknown*)b) == 1 && refs((IUnknown*)device) == 4, "FAIL bound zero-public revival parent pins");
    Surface* pair[] = {a,b};
    for (UINT i = 0; i < 2; i++) {
        IUnknown* same = NULL;
        check(SURFACE(QueryInterface)(pair[i],&unknown_id,(void**)&same) == D3D_OK && same == (IUnknown*)pair[i] && refs(same) == 2 && refs((IUnknown*)device) == 4 && IUnknown_Release(same) == 1, "FAIL bound IUnknown counter identity");
        IUnknown* container = NULL;
        check(SURFACE(GetContainer)(pair[i],&IID_IDirect3DDevice8,(void**)&container) == D3D_OK && container == (IUnknown*)device && refs(container) == 5 && IUnknown_Release(container) == 4, "FAIL bound device container");
        DWORD marker = 0, size = sizeof(marker);
        check(SURFACE(GetPrivateData)(pair[i],&key,&marker,&size) == D3D_OK && marker == 0x17283940 + i && size == 4, "FAIL bound private marker preservation");
        IUnknown* object = NULL;
        size = sizeof(object);
        check(SURFACE(GetPrivateData)(pair[i],&object_key,&object,&size) == D3D_OK && object == &trackers[i].iface && trackers[i].refs == 2 && IUnknown_Release(object) == 1 && !trackers[i].finalized, "FAIL bound private object preservation");
    }
    check(SURFACE(Release)(a) == 0 && SURFACE(Release)(b) == 0 && refs((IUnknown*)device) == 2, "FAIL bound public zero parent return");
}
static void target_cases(HWND window, const D3DPRESENT_PARAMETERS* base)
{
    Surface* back = NULL;
    check(DEV(GetBackBuffer)(device,0,D3DBACKBUFFER_TYPE_MONO,&back) == D3D_OK && back && refs((IUnknown*)device) == 2, "FAIL binding saved primary");
    Surface* target = create_profile(0,32,24);
    Surface* depth = create_profile(2,32,24);
    attach_private(&trackers[0],target,1,0x17283940);
    attach_private(&trackers[1],depth,1,0x17283941);
    check(DEV(SetRenderTarget)(device,target,depth) == D3D_OK && refs((IUnknown*)target) == 1 && refs((IUnknown*)depth) == 1 && refs((IUnknown*)device) == 4, "FAIL private binding changed public counter");
    check(SURFACE(Release)(target) == 0 && SURFACE(Release)(depth) == 0 && refs((IUnknown*)device) == 2, "FAIL private binding owns public parent pin");
    for (UINT cycle = 0; cycle < 32; cycle++) {
        verify_revival(target,depth);
        check(DEV(SetRenderTarget)(device,target,depth) == D3D_OK && refs((IUnknown*)target) == 0 && refs((IUnknown*)depth) == 0 && refs((IUnknown*)device) == 2, "FAIL borrowed same-object private rebind");
    }
    report("BIND REVIVAL rounds=32 surfaces=2 publicRefs=0 activeDeviceRefs=2 queriedDeviceRefs=4 containerDeviceRefs=5 privateRetained=2");
    const DWORD colors[] = {0xffb02010,0xff1040c0};
    for (UINT frame = 0; frame < 2; frame++) {
        if (frame) {
            check(DEV(SetRenderTarget)(device,target,depth) == D3D_OK, "FAIL second frame private restore");
            check(SURFACE(Release)(target) == 0 && SURFACE(Release)(depth) == 0 && refs((IUnknown*)device) == 2, "FAIL second frame public zero");
        }
        check(DEV(Clear)(device,0,NULL,D3DCLEAR_TARGET,colors[frame],1.0f,0) == D3D_OK, "FAIL private-only bound Clear");
        Surface *a = NULL, *b = NULL;
        check(DEV(GetRenderTarget)(device,&a) == D3D_OK && a == target && DEV(GetDepthStencilSurface)(device,&b) == D3D_OK && b == depth && refs((IUnknown*)device) == 4, "FAIL frame target revival");
        check(DEV(CopyRects)(device,a,NULL,0,back,NULL) == D3D_OK, "FAIL private target GPU CopyRects");
        check(DEV(SetRenderTarget)(device,back,NULL) == D3D_OK && refs((IUnknown*)a) == 1 && refs((IUnknown*)b) == 1, "FAIL frame retained public query");
        check(DEV(Present)(device,NULL,NULL,NULL,NULL) == D3D_OK, "FAIL private target frame Present");
        char line[180];
        wsprintfA(line,"BIND FRAME index=%u color=%08lX samples=0 clearPublicRefs=0 copiedToPrimary=1 presented=1 queryRefs=1",frame,colors[frame]);
        report(line);
    }
    check(DEV(SetRenderTarget)(device,target,depth) == D3D_OK && SURFACE(Release)(target) == 0 && SURFACE(Release)(depth) == 0, "FAIL Reset private-only bindings");
    check(SURFACE(Release)(back) == 0 && refs((IUnknown*)device) == 1 && !trackers[0].finalized && !trackers[1].finalized, "FAIL Reset private-only parent references");
    D3DPRESENT_PARAMETERS p = *base;
    check(DEV(Reset)(device,&p) == D3D_OK && DEV(TestCooperativeLevel)(device) == D3D_OK && refs((IUnknown*)device) == 1, "FAIL Reset blocked by private-only targets");
    for (UINT i = 0; i < 2; i++) check(!trackers[i].refs && trackers[i].finalized == 1 && trackers[i].observed_device_refs == 1, "FAIL Reset private-only finalizer order");
    check(DEV(Release)(device) == 0 && refs((IUnknown*)factory) == 1, "FAIL Reset binding device cleanup");
    device = NULL;
    report("BIND RESET publicRefs=0 deviceRefsBefore=1 accepted=1 privateFinalized=2 callbackRefs=1 factoryRefs=1");
    p = *base;
    check(create_device(window,&p,&device) == D3D_OK && device, "FAIL final binding device create");
    target = create_profile(1,32,24);
    depth = create_profile(3,32,24);
    attach_private(&trackers[0],target,1,0x28394050);
    attach_private(&trackers[1],depth,1,0x28394051);
    check(DEV(SetRenderTarget)(device,target,depth) == D3D_OK && SURFACE(Release)(target) == 0 && SURFACE(Release)(depth) == 0 && refs((IUnknown*)device) == 1, "FAIL final binding-only parent refs");
    check(DEV(Release)(device) == 0 && refs((IUnknown*)factory) == 1, "FAIL final private-only device cleanup");
    for (UINT i = 0; i < 2; i++) check(!trackers[i].refs && trackers[i].finalized == 1 && trackers[i].observed_device_refs == 1, "FAIL protected final device callback");
    device = NULL;
    report("BIND FINAL publicRefs=0 applicationDeviceRefs=1 returnedDeviceRefs=0 privateFinalized=2 callbackRefs=1 factoryRefs=1");
    p = *base;
    check(create_device(window,&p,&device) == D3D_OK && device, "FAIL final unbind device create");
    check(DEV(GetBackBuffer)(device,0,D3DBACKBUFFER_TYPE_MONO,&back) == D3D_OK && back, "FAIL final unbind primary");
    target = create_profile(0,32,24);
    attach_private(&trackers[0],target,2,0x39405060);
    check(DEV(SetRenderTarget)(device,target,NULL) == D3D_OK && SURFACE(Release)(target) == 0 && refs((IUnknown*)device) == 2 && !trackers[0].finalized, "FAIL final unbind retained storage");
    check(DEV(SetRenderTarget)(device,back,NULL) == D3D_OK && !trackers[0].refs && trackers[0].finalized == 1 && trackers[0].observed_device_refs == 2, "FAIL last internal unbind finalizer");
    SURFACE(Release)(back);
    check(DEV(Release)(device) == 0 && refs((IUnknown*)factory) == 1, "FAIL last internal unbind device cleanup");
    device = NULL;
    report("BIND UNBIND publicRefs=0 privateFinalized=1 callbackRefs=2 factoryRefs=1");
    report("BIND STATUS logicalDevices=3 reset=1 frames=2 revivals=64 privateFinalized=5 publicBindingRefs=0 factoryRefs=1");
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], path[MAX_PATH], name[100];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    UINT at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    wsprintfA(name, "BND%u.TXT", D9WG_IMPLICIT_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    RECT client = { 0, 0, 32, 24 };
    check(AdjustWindowRectEx(&client, WS_POPUP, FALSE, 0), "FAIL window dimensions");
    HWND window = CreateWindowExA(0, "STATIC", "D3D8 standalone private binding lifetime", WS_POPUP, 30, 40, client.right - client.left, client.bottom - client.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(window != NULL, "FAIL window");
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    RECT actual;
    check(GetClientRect(window, &actual) && actual.right == 32 && actual.bottom == 24, "FAIL exact client dimensions");
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
    pp.BackBufferWidth = 32;
    pp.BackBufferHeight = 24;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.EnableAutoDepthStencil = FALSE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
#if D9WG_IMPLICIT_API == 8
    check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device8");
#else
    check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device9");
#endif
    check(DEV(TestCooperativeLevel)(device) == D3D_OK, "FAIL initial cooperative status");
    target_cases(window,&pp);
    check(!device && refs((IUnknown*)factory) == 1, "FAIL final target roots");
    check(FACTORY_RELEASE(factory) == 0, "FAIL final revival factory references");
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS target bindings API=%u", D9WG_IMPLICIT_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
