/* Reset cancellation of active recording and retained-resource SDK probe, without a C runtime. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#if D9WG_RESET_API == 8
#include <d3d8.h>
typedef IDirect3D8 Factory;
typedef IDirect3DDevice8 Device;
typedef IDirect3DVertexBuffer8 VertexBuffer;
typedef IDirect3DIndexBuffer8 IndexBuffer;
typedef IDirect3DTexture8 Texture;
typedef IDirect3DCubeTexture8 Cube;
typedef IDirect3DVolumeTexture8 VolumeTexture;
typedef IDirect3DSurface8 Surface;
typedef IDirect3DVolume8 Volume;
typedef IDirect3DSwapChain8 Chain;
#define SURFACE(method) IDirect3DSurface8_##method
#define CHAIN(method) IDirect3DSwapChain8_##method
#define TEX(method) IDirect3DTexture8_##method
#define CUBE(method) IDirect3DCubeTexture8_##method
#define VOLUME_TEX(method) IDirect3DVolumeTexture8_##method
#define VB_LOCK IDirect3DVertexBuffer8_Lock
#define IB_LOCK IDirect3DIndexBuffer8_Lock
#define VB_UNLOCK IDirect3DVertexBuffer8_Unlock
#define IB_UNLOCK IDirect3DIndexBuffer8_Unlock
#define VB_RELEASE IDirect3DVertexBuffer8_Release
#define IB_RELEASE IDirect3DIndexBuffer8_Release
#define VB(method) IDirect3DVertexBuffer8_##method
#define IB(method) IDirect3DIndexBuffer8_##method
#define DEV(method) IDirect3DDevice8_##method
#define FACTORY_RELEASE IDirect3D8_Release
#else
#include <d3d9.h>
typedef IDirect3D9 Factory;
typedef IDirect3DDevice9 Device;
typedef IDirect3DVertexBuffer9 VertexBuffer;
typedef IDirect3DIndexBuffer9 IndexBuffer;
typedef IDirect3DTexture9 Texture;
typedef IDirect3DCubeTexture9 Cube;
typedef IDirect3DVolumeTexture9 VolumeTexture;
typedef IDirect3DSurface9 Surface;
typedef IDirect3DVolume9 Volume;
typedef IDirect3DSwapChain9 Chain;
#define SURFACE(method) IDirect3DSurface9_##method
#define CHAIN(method) IDirect3DSwapChain9_##method
#define TEX(method) IDirect3DTexture9_##method
#define CUBE(method) IDirect3DCubeTexture9_##method
#define VOLUME_TEX(method) IDirect3DVolumeTexture9_##method
#define VB_LOCK IDirect3DVertexBuffer9_Lock
#define IB_LOCK IDirect3DIndexBuffer9_Lock
#define VB_UNLOCK IDirect3DVertexBuffer9_Unlock
#define IB_UNLOCK IDirect3DIndexBuffer9_Unlock
#define VB_RELEASE IDirect3DVertexBuffer9_Release
#define IB_RELEASE IDirect3DIndexBuffer9_Release
#define VB(method) IDirect3DVertexBuffer9_##method
#define IB(method) IDirect3DIndexBuffer9_##method
#define DEV(method) IDirect3DDevice9_##method
#define FACTORY_RELEASE IDirect3D9_Release
#endif
static Factory* factory;
static Device* device;
static HANDLE logfile;
static UINT accepted, refused, finalized, recordings, ended_invalid;
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
#if D9WG_RESET_API == 8
typedef IDirect3DBaseTexture8 BaseTexture;
#else
typedef IDirect3DBaseTexture9 BaseTexture;
#endif
static const GUID key = { 0x51203040, 1, 2, { 3, 4, 5, 6, 7, 8, 9, 10 } };
static const GUID iid_unknown = { 0, 0, 0, { 0xc0, 0, 0, 0, 0, 0, 0, 0x46 } };
typedef struct {
    IUnknown iface;
    ULONG refs;
    UINT destroyed;
} Tracker;
static Tracker tracker;
static HRESULT WINAPI tracker_query(IUnknown* iface, REFIID iid, void** out)
{
    if (!out)
        return E_POINTER;
    *out = NULL;
    if (!iid || iid->Data1 != iid_unknown.Data1 || iid->Data2 != iid_unknown.Data2 || iid->Data3 != iid_unknown.Data3)
        return E_NOINTERFACE;
    for (UINT i = 0; i < 8; i++)
        if (iid->Data4[i] != iid_unknown.Data4[i])
            return E_NOINTERFACE;
    *out = iface;
    IUnknown_AddRef(iface);
    return S_OK;
}
static ULONG WINAPI tracker_addref(IUnknown* iface)
{
    Tracker* t = (Tracker*)iface;
    return ++t->refs;
}
static ULONG WINAPI tracker_release(IUnknown* iface)
{
    Tracker* t = (Tracker*)iface;
    check(t->refs > 0, "FAIL tracker reference underflow");
    if (!--t->refs) {
        t->destroyed++;
        finalized++;
    }
    return t->refs;
}
static IUnknownVtbl tracker_vtbl = { tracker_query, tracker_addref, tracker_release };
static HRESULT make_resource(UINT kind, D3DPOOL pool, IUnknown** out)
{
    if (!kind)
        return DEV(CreateVertexBuffer)(device, 16, 0, D3DFVF_XYZ, pool, (VertexBuffer**)out
#if D9WG_RESET_API == 9
            ,
            NULL
#endif
        );
    if (kind == 1)
        return DEV(CreateIndexBuffer)(device, 16, 0, D3DFMT_INDEX16, pool, (IndexBuffer**)out
#if D9WG_RESET_API == 9
            ,
            NULL
#endif
        );
    if (kind == 2)
        return DEV(CreateTexture)(device, 4, 4, 2, 0, D3DFMT_A8R8G8B8, pool, (Texture**)out
#if D9WG_RESET_API == 9
            ,
            NULL
#endif
        );
    if (kind == 3)
        return DEV(CreateCubeTexture)(device, 4, 2, 0, D3DFMT_A8R8G8B8, pool, (Cube**)out
#if D9WG_RESET_API == 9
            ,
            NULL
#endif
        );
    return DEV(CreateVolumeTexture)(device, 4, 4, 4, 2, 0, D3DFMT_A8R8G8B8, pool, (VolumeTexture**)out
#if D9WG_RESET_API == 9
        ,
        NULL
#endif
    );
}
static HRESULT bind_resource(UINT kind, IUnknown* object)
{
    if (!kind)
        return DEV(SetStreamSource)(device, 0, (VertexBuffer*)object,
#if D9WG_RESET_API == 9
            0,
#endif
            12);
    if (kind == 1)
        return DEV(SetIndices)(device, (IndexBuffer*)object
#if D9WG_RESET_API == 8
            ,
            0
#endif
        );
    return DEV(SetTexture)(device, 0, (BaseTexture*)object);
}

static void attach_tracker(UINT kind, IUnknown* object)
{
    tracker.iface.lpVtbl = &tracker_vtbl;
    tracker.refs = 1;
    tracker.destroyed = 0;
    HRESULT hr;
    if (!kind)
        hr = VB(SetPrivateData)((VertexBuffer*)object, &key, &tracker.iface, sizeof(IUnknown*), D3DSPD_IUNKNOWN);
    else if (kind == 1)
        hr = IB(SetPrivateData)((IndexBuffer*)object, &key, &tracker.iface, sizeof(IUnknown*), D3DSPD_IUNKNOWN);
    else if (kind == 2)
        hr = TEX(SetPrivateData)((Texture*)object, &key, &tracker.iface, sizeof(IUnknown*), D3DSPD_IUNKNOWN);
    else if (kind == 3)
        hr = CUBE(SetPrivateData)((Cube*)object, &key, &tracker.iface, sizeof(IUnknown*), D3DSPD_IUNKNOWN);
    else
        hr = VOLUME_TEX(SetPrivateData)((VolumeTexture*)object, &key, &tracker.iface, sizeof(IUnknown*), D3DSPD_IUNKNOWN);
    check(hr == D3D_OK && tracker.refs == 2, "FAIL private tracker retained");
    check(IUnknown_Release(&tracker.iface) == 1, "FAIL private tracker store-only reference");
}
static void begin_recording(void)
{
    check(DEV(BeginStateBlock)(device) == D3D_OK, "FAIL BeginStateBlock");
    recordings++;
}
static HRESULT end_recording(void)
{
#if D9WG_RESET_API == 8
    DWORD token = 0;
    HRESULT hr = DEV(EndStateBlock)(device, &token);
    if (hr == D3D_OK)
        check(DEV(DeleteStateBlock)(device, token) == D3D_OK, "FAIL delete fresh state block");
#else
    IDirect3DStateBlock9* block = NULL;
    HRESULT hr = DEV(EndStateBlock)(device, &block);
    if (hr == D3D_OK)
        check(block && IDirect3DStateBlock9_Release(block) == 0, "FAIL release fresh state block");
#endif
    return hr;
}
static void accept_reset(D3DPRESENT_PARAMETERS* base)
{
    D3DPRESENT_PARAMETERS p = *base;
    check(DEV(Reset)(device, &p) == D3D_OK, "FAIL recording Reset success");
    accepted++;
    check(DEV(TestCooperativeLevel)(device) == D3D_OK, "FAIL ready after Reset");
    check(end_recording() == D3DERR_INVALIDCALL, "FAIL Reset retained recording");
    ended_invalid++;
    begin_recording();
    check(end_recording() == D3D_OK, "FAIL fresh recording after Reset");
}
static void refuse_reset(D3DPRESENT_PARAMETERS* base, HRESULT expected)
{
    D3DPRESENT_PARAMETERS p = *base;
    check(DEV(Reset)(device, &p) == expected, "FAIL recording Reset refusal");
    refused++;
    check(DEV(TestCooperativeLevel)(device) == D3DERR_DEVICENOTRESET, "FAIL recording Reset NOTRESET");
}
static void recording_cases(D3DPRESENT_PARAMETERS* base)
{
    begin_recording();
    check(DEV(SetRenderState)(device, D3DRS_ZENABLE, FALSE) == D3D_OK, "FAIL scalar record");
    accept_reset(base);
    for (UINT kind = 0; kind < 5; kind++)
        for (UINT held = 0; held < 2; held++) {
            IUnknown* object = NULL;
            check(make_resource(kind, D3DPOOL_DEFAULT, &object) == D3D_OK && object, "FAIL recorded resource create");
            attach_tracker(kind, object);
            begin_recording();
            check(bind_resource(kind, object) == D3D_OK, "FAIL recorded resource binding");
            if (!held) {
                IUnknown_Release(object);
                check(tracker.refs == 1 && !tracker.destroyed, "FAIL recording lost resource early");
                accept_reset(base);
            } else {
#if D9WG_RESET_API == 8
                refuse_reset(base, D3DERR_DEVICELOST);
#else
                refuse_reset(base, D3DERR_INVALIDCALL);
#endif
                check(tracker.refs == 1 && !tracker.destroyed, "FAIL held resource finalized before app release");
                IUnknown_Release(object);
                check(!tracker.refs && tracker.destroyed == 1, "FAIL failed Reset retained recording reference");
                accept_reset(base);
            }
            check(!tracker.refs && tracker.destroyed == 1, "FAIL recording resource finalization");
            char line[100];
            wsprintfA(line, "RECORDING CASE api=%u family=%u held=%u finalized=1", D9WG_RESET_API, kind, held);
            report(line);
        }
    for (UINT bad = 0; bad < 2; bad++) {
        IUnknown* object = NULL;
        check(make_resource(2, D3DPOOL_DEFAULT, &object) == D3D_OK, "FAIL invalid-input resource create");
        attach_tracker(2, object);
        begin_recording();
        check(bind_resource(2, object) == D3D_OK, "FAIL invalid-input record");
        IUnknown_Release(object);
        D3DPRESENT_PARAMETERS p = *base;
        if (bad)
            p.BackBufferWidth = 8193;
#if D9WG_RESET_API == 8
        else
            p.FullScreen_PresentationInterval = 3;
#else
        else
            p.PresentationInterval = 3;
#endif
        refuse_reset(&p, D3DERR_INVALIDCALL);
        check(tracker.refs == 1 && !tracker.destroyed, "FAIL invalid parameters cancelled recording");
        accept_reset(base);
        check(!tracker.refs && tracker.destroyed == 1, "FAIL repaired Reset failed to cancel recording");
        char line[100];
        wsprintfA(line, "RECORDING INPUT api=%u invalid=%u preserved=1 finalized=1", D9WG_RESET_API, bad);
        report(line);
    }
    check(accepted == 13 && refused == 7 && finalized == 12 && recordings == 26 && ended_invalid == 13, "FAIL recording totals");
    char line[220];
    wsprintfA(line, "RESET RECORDING api=%u accepted=%u refused=%u finalized=%u recordings=%u endInvalid=%u families=5 held=5 sole=5 invalidPreserved=2", D9WG_RESET_API, accepted, refused, finalized, recordings, ended_invalid);
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
    wsprintfA(name, "REC%u.TXT", D9WG_RESET_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    RECT client = { 0, 0, 64, 64 };
    check(AdjustWindowRectEx(&client, WS_POPUP, FALSE, 0), "FAIL window dimensions");
    HWND window = CreateWindowExA(0, "STATIC", "D3D Reset recording probe", WS_POPUP, 30, 40, client.right - client.left, client.bottom - client.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(window != NULL, "FAIL window");
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    RECT actual;
    check(GetClientRect(window, &actual) && actual.right == 64 && actual.bottom == 64, "FAIL exact client dimensions");
    wsprintfA(name, "D3D%u.DLL", D9WG_RESET_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    HMODULE module = LoadLibraryA(path);
    check(module != NULL, "FAIL DLL");
    wsprintfA(name, "Direct3DCreate%u", D9WG_RESET_API);
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
#if D9WG_RESET_API == 8
    check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device8");
#else
    check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device9");
#endif
    check(DEV(TestCooperativeLevel)(device) == D3D_OK, "FAIL initial cooperative status");
    recording_cases(&pp);
    check(DEV(Release)(device) == 0, "FAIL device references");
    FACTORY_RELEASE(factory);
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS reset recording API=%u", D9WG_RESET_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
