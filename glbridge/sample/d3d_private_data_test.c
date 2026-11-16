/* Private-data API/COM probe, without a C runtime. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#if D9WG_PRIVATE_API == 8
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

static const GUID key = { 0x12345678, 0xabcd, 0xef01, { 2, 3, 4, 5, 6, 7, 8, 9 } }, other = { 0x87654321, 0xdcba, 0x10fe, { 9, 8, 7, 6, 5, 4, 3, 2 } };
typedef struct {
    IUnknown iface;
    LONG refs;
} Counter;
static HRESULT WINAPI query(IUnknown* iface, REFIID iid, void** out)
{
    (void)iface;
    (void)iid;
    if (out)
        *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI add(IUnknown* iface) { return (ULONG)InterlockedIncrement(&((Counter*)iface)->refs); }
static ULONG WINAPI drop(IUnknown* iface) { return (ULONG)InterlockedDecrement(&((Counter*)iface)->refs); }
static IUnknownVtbl counter_vtable = { query, add, drop };
/* Only the common IUnknown/Resource prefix is used; VB and IB layouts match. */
typedef struct {
    HRESULT(WINAPI* qi)(void*, REFIID, void**);
    ULONG(WINAPI* add)(void*);
    ULONG(WINAPI* release)(void*);
    HRESULT(WINAPI* device)(void*, void**);
    HRESULT(WINAPI* set)(void*, REFGUID, const void*, DWORD, DWORD);
    HRESULT(WINAPI* get)(void*, REFGUID, void*, DWORD*);
    HRESULT(WINAPI* free)(void*, REFGUID);
} ResourceMethods;
static void buffer_cases(IUnknown* buffer, UINT kind)
{
    const ResourceMethods* methods = (const void*)buffer->lpVtbl;
    DWORD size = 0xdeadbabe, out = 0xdeadbeef, input = 0x1234abcd;
    check(methods->get(buffer, &key, &out, &size) == D3DERR_NOTFOUND && out == 0xdeadbeef && size == 0xdeadbabe, "FAIL missing preserves output");
    check(methods->get(buffer, &key, NULL, NULL) == D3DERR_NOTFOUND && methods->free(buffer, &key) == D3DERR_NOTFOUND, "FAIL missing NULL query/free");
    check(methods->set(buffer, &key, &input, 4, 0) == D3D_OK, "FAIL raw set");
    input = 0;
    size = 1;
    check(methods->get(buffer, &key, NULL, &size) == D3D_OK && size == 4, "FAIL raw size query");
    size = 3;
    check(methods->get(buffer, &key, &out, &size) == D3DERR_MOREDATA && out == 0xdeadbeef && size == 4, "FAIL raw too small");
    size = 4;
    check(methods->get(buffer, &key, &out, &size) == D3D_OK && out == 0x1234abcd, "FAIL copied bytes");
    Counter object;
    object.iface.lpVtbl = &counter_vtable;
    object.refs = 1;
    check(methods->set(buffer, &key, &object, 0, D3DSPD_IUNKNOWN) == D3DERR_INVALIDCALL, "FAIL interface zero size");
    check(methods->set(buffer, &key, &object, 5, D3DSPD_IUNKNOWN) == D3DERR_INVALIDCALL, "FAIL interface five bytes");
    check(methods->set(buffer, &key, &object, 8, D3DSPD_IUNKNOWN) == D3DERR_INVALIDCALL, "FAIL interface double size");
    size = 4;
    check(methods->get(buffer, &key, &out, &size) == D3D_OK && out == 0x1234abcd, "FAIL failed replacement changed bytes");
    check(methods->set(buffer, &key, &object, sizeof(void*), D3DSPD_IUNKNOWN) == D3D_OK && object.refs == 2, "FAIL interface retain");
    size = 1;
    check(methods->get(buffer, &key, NULL, &size) == D3D_OK && size == 4 && object.refs == 2, "FAIL interface size-only");
    IUnknown* pointer = (void*)(uintptr_t)0xdeadbeef;
    size = 1;
    check(methods->get(buffer, &key, &pointer, &size) == D3DERR_MOREDATA && size == 4 && pointer == (void*)(uintptr_t)0xdeadbeef && object.refs == 2, "FAIL interface too small");
    size = 8;
    check(methods->get(buffer, &key, &pointer, &size) == D3D_OK && size == 4 && pointer == &object.iface && object.refs == 3, "FAIL interface get retain");
    IUnknown_Release(pointer);
    check(methods->set(buffer, &other, &input, 4, 0) == D3D_OK && object.refs == 2, "FAIL GUID isolation");
    check(methods->set(buffer, &key, &object, 4, D3DSPD_IUNKNOWN) == D3D_OK && object.refs == 2, "FAIL interface same replacement");
    check(methods->free(buffer, &key) == D3D_OK && object.refs == 1, "FAIL interface free release");
    check(methods->free(buffer, &key) == D3DERR_NOTFOUND, "FAIL duplicate free");
    check(methods->set(buffer, &key, &object, 4, D3DSPD_IUNKNOWN) == D3D_OK && object.refs == 2, "FAIL destruction setup");
    IUnknown_AddRef(buffer);
    check(IUnknown_Release(buffer) == 1 && object.refs == 2, "FAIL nonfinal release");
    check(IUnknown_Release(buffer) == 0 && object.refs == 1, "FAIL final release private reference");
    char line[80];
    wsprintfA(line, "CHECK PRIVATE %u %u raw=1234ABCD size=4 refs=1 released=0", D9WG_PRIVATE_API, kind);
    report(line);
}

#ifdef D9WG_PRIVATE_TEXTURES
static HRESULT get_child(IUnknown* parent, UINT kind, UINT index, IUnknown** out)
{
    if (kind == 0)
        return TEX(GetSurfaceLevel)((Texture*)parent, index, (Surface**)out);
    if (kind == 1)
        return CUBE(GetCubeMapSurface)((Cube*)parent, index == 1 ? 2 : 4, index == 2 ? 1 : 0, (Surface**)out);
    return VOLUME_TEX(GetVolumeLevel)((VolumeTexture*)parent, index, (Volume**)out);
}
static void texture_cases(UINT kind)
{
    IUnknown* parent = NULL;
    HRESULT hr;
    if (kind == 0)
        hr = DEV(CreateTexture)(device, 4, 4, 2, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, (Texture**)&parent
#if D9WG_PRIVATE_API == 9
            ,
            NULL
#endif
        );
    else if (kind == 1)
        hr = DEV(CreateCubeTexture)(device, 4, 2, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, (Cube**)&parent
#if D9WG_PRIVATE_API == 9
            ,
            NULL
#endif
        );
    else
        hr = DEV(CreateVolumeTexture)(device, 4, 4, 4, 2, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, (VolumeTexture**)&parent
#if D9WG_PRIVATE_API == 9
            ,
            NULL
#endif
        );
    check(hr == D3D_OK && parent, "FAIL texture create");
    const ResourceMethods* methods = (const void*)parent->lpVtbl;
    DWORD data = 0x10203040, size = 4, out = 0;
    check(methods->set(parent, &key, &data, 4, 0) == D3D_OK, "FAIL texture private set");
    Counter object;
    object.iface.lpVtbl = &counter_vtable;
    object.refs = 1;
    check(methods->set(parent, &other, &object, 4, D3DSPD_IUNKNOWN) == D3D_OK && object.refs == 2, "FAIL parent interface");
    IUnknown* children[3] = { NULL, NULL, NULL };
    UINT count = kind == 1 ? 3 : 2;
    for (UINT i = 0; i < count; i++) {
        check(get_child(parent, kind, i, &children[i]) == D3D_OK, "FAIL get child");
        const ResourceMethods* child = (const void*)children[i]->lpVtbl;
        size = 4;
        out = 0xdeadbeef;
        check(child->get(children[i], &key, &out, &size) == D3DERR_NOTFOUND && out == 0xdeadbeef && size == 4, "FAIL child inherits parent or other level");
        data = 0x33445500 + i;
        check(child->set(children[i], &key, &data, 4, 0) == D3D_OK, "FAIL child set");
        check(child->set(children[i], &other, &object, 4, D3DSPD_IUNKNOWN) == D3D_OK && object.refs == (LONG)(3 + i), "FAIL child retain");
    }
    for (UINT i = 0; i < count; i++) {
        const ResourceMethods* child = (const void*)children[i]->lpVtbl;
        size = 4;
        check(child->get(children[i], &key, &out, &size) == D3D_OK && out == 0x33445500 + i, "FAIL sibling/parent overwrite");
    }
    IUnknown* duplicate = NULL;
    check(get_child(parent, kind, 0, &duplicate) == D3D_OK, "FAIL duplicate child");
    const ResourceMethods* child = (const void*)duplicate->lpVtbl;
    size = 4;
    check(child->get(duplicate, &key, &out, &size) == D3D_OK && out == 0x33445500, "FAIL logical child identity");
    IUnknown_Release(duplicate);
    for (UINT i = 0; i < count; i++)
        IUnknown_Release(children[i]);
    check(object.refs == (LONG)(2 + count), "FAIL releasing child view cleared store");
    check(get_child(parent, kind, 0, &duplicate) == D3D_OK, "FAIL reacquire child");
    child = (const void*)duplicate->lpVtbl;
    size = 4;
    check(child->get(duplicate, &key, &out, &size) == D3D_OK && out == 0x33445500, "FAIL child data lost on reacquire");
    IUnknown* held = NULL;
    size = 4;
    check(child->get(duplicate, &other, &held, &size) == D3D_OK && held == &object.iface && object.refs == (LONG)(3 + count), "FAIL child interface query");
    IUnknown_Release(held);
    size = 4;
    check(methods->get(parent, &key, &out, &size) == D3D_OK && out == 0x10203040, "FAIL parent store changed");
    check(IUnknown_Release(parent) == 1 && object.refs == (LONG)(2 + count), "FAIL parent released before child");
    check(IUnknown_Release(duplicate) == 0 && object.refs == 1, "FAIL texture and logical child destruction refs");
    char line[100];
    wsprintfA(line, "CHECK TEXTURE PRIVATE %u %u children=%u parent=10203040 persistent=1 refs=1 released=0", D9WG_PRIVATE_API, kind, count);
    report(line);
}
#endif

#ifdef D9WG_PRIVATE_SURFACES
static Counter back_counter, depth_counter;
static void surface_private_checks(Surface* surface, UINT kind)
{
    const ResourceMethods* methods = (const void*)((IUnknown*)surface)->lpVtbl;
    DWORD value = 0x1234abcd, out = 0, size = 4;
    Counter object;
    object.iface.lpVtbl = &counter_vtable;
    object.refs = 1;
    check(methods->set(surface, &key, &value, 4, 0) == D3D_OK && methods->get(surface, &key, &out, &size) == D3D_OK && out == value, "FAIL standalone private bytes");
    check(methods->set(surface, &other, &object, 4, D3DSPD_IUNKNOWN) == D3D_OK && object.refs == 2, "FAIL standalone interface");
    check(SURFACE(Release)(surface) == 0 && object.refs == 1, "FAIL standalone destruction");
    char line[100];
    wsprintfA(line, "CHECK SURFACE PRIVATE %u %u refs=1 released=0", D9WG_PRIVATE_API, kind);
    report(line);
}
static HRESULT get_back(Surface** out)
{
    return DEV(GetBackBuffer)(device,
#if D9WG_PRIVATE_API == 9
        0,
#endif
        0, D3DBACKBUFFER_TYPE_MONO, out);
}
static void surface_cases(D3DPRESENT_PARAMETERS* pp)
{
    Surface *a = NULL, *b = NULL;
#if D9WG_PRIVATE_API == 8
    check(DEV(CreateImageSurface)(device, 4, 4, D3DFMT_A8R8G8B8, &a) == D3D_OK, "FAIL image surface create");
#else
    check(DEV(CreateOffscreenPlainSurface)(device, 4, 4, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &a, NULL) == D3D_OK, "FAIL offscreen create");
#endif
    surface_private_checks(a, 0);
    check(DEV(CreateRenderTarget)(device, 4, 4, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE,
#if D9WG_PRIVATE_API == 9
              0,
#endif
              FALSE, &a
#if D9WG_PRIVATE_API == 9
              ,
              NULL
#endif
              )
            == D3D_OK,
        "FAIL target create");
    surface_private_checks(a, 1);
    check(DEV(CreateDepthStencilSurface)(device, 4, 4, D3DFMT_D24S8, D3DMULTISAMPLE_NONE,
#if D9WG_PRIVATE_API == 9
              0, TRUE,
#endif
              &a
#if D9WG_PRIVATE_API == 9
              ,
              NULL
#endif
              )
            == D3D_OK,
        "FAIL depth create");
    surface_private_checks(a, 2);
    back_counter.iface.lpVtbl = &counter_vtable;
    back_counter.refs = 1;
    DWORD value = 0x55667788, out = 0, size = 4;
    check(get_back(&a) == D3D_OK, "FAIL implicit back");
    check(SURFACE(SetPrivateData)(a, &key, &value, 4, 0) == D3D_OK && SURFACE(SetPrivateData)(a, &other, &back_counter, 4, D3DSPD_IUNKNOWN) == D3D_OK && back_counter.refs == 2, "FAIL implicit back set");
    SURFACE(Release)(a);
    check(get_back(&b) == D3D_OK && SURFACE(GetPrivateData)(b, &key, &out, &size) == D3D_OK && out == value && back_counter.refs == 2, "FAIL back data lost after release");
    SURFACE(Release)(b);
    depth_counter.iface.lpVtbl = &counter_vtable;
    depth_counter.refs = 1;
    value = 0x778899aa;
    size = 4;
    check(DEV(GetDepthStencilSurface)(device, &a) == D3D_OK, "FAIL implicit depth");
    check(SURFACE(SetPrivateData)(a, &key, &value, 4, 0) == D3D_OK && SURFACE(SetPrivateData)(a, &other, &depth_counter, 4, D3DSPD_IUNKNOWN) == D3D_OK && depth_counter.refs == 2, "FAIL implicit depth set");
    SURFACE(Release)(a);
    check(DEV(GetDepthStencilSurface)(device, &b) == D3D_OK && SURFACE(GetPrivateData)(b, &key, &out, &size) == D3D_OK && out == value && depth_counter.refs == 2, "FAIL depth data lost after release");
    SURFACE(Release)(b);
    Chain* chain = NULL;
    check(DEV(CreateAdditionalSwapChain)(device, pp, &chain) == D3D_OK, "FAIL additional chain");
    check(CHAIN(GetBackBuffer)(chain, 0, D3DBACKBUFFER_TYPE_MONO, &a) == D3D_OK && CHAIN(GetBackBuffer)(chain, 0, D3DBACKBUFFER_TYPE_MONO, &b) == D3D_OK, "FAIL chain views");
    Counter object;
    object.iface.lpVtbl = &counter_vtable;
    object.refs = 1;
    value = 0x99aabbcc;
    check(SURFACE(SetPrivateData)(a, &key, &value, 4, 0) == D3D_OK && SURFACE(SetPrivateData)(a, &other, &object, 4, D3DSPD_IUNKNOWN) == D3D_OK && object.refs == 2, "FAIL chain private set");
    check(CHAIN(Release)(chain) == 0 && object.refs == 2, "FAIL early chain release");
    size = 4;
    check(SURFACE(GetPrivateData)(b, &key, &out, &size) == D3D_OK && out == value, "FAIL chain data lost");
    SURFACE(Release)(a);
    check(object.refs == 2, "FAIL first chain view cleared data");
    check(SURFACE(Release)(b) == 0 && object.refs == 1, "FAIL last chain view cleanup");
    check(get_back(&a) == D3D_OK, "FAIL back after chain");
    size = 4;
    check(SURFACE(GetPrivateData)(a, &key, &out, &size) == D3D_OK && out == 0x55667788, "FAIL chain aliases device back store");
    SURFACE(Release)(a);
    char line[100];
    wsprintfA(line, "CHECK SURFACE OWNERS %u back=2 depth=2 chain=1 earlyRelease=0 isolated=1", D9WG_PRIVATE_API);
    report(line);
}
#endif
static void private_cases(void)
{
    VertexBuffer* vb = NULL;
    IndexBuffer* ib = NULL;
    check(DEV(CreateVertexBuffer)(device, 64, 0, D3DFVF_XYZ, D3DPOOL_MANAGED, &vb
#if D9WG_PRIVATE_API == 9
              ,
              NULL
#endif
              )
            == D3D_OK,
        "FAIL vertex create");
    check(DEV(CreateIndexBuffer)(device, 32, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib
#if D9WG_PRIVATE_API == 9
              ,
              NULL
#endif
              )
            == D3D_OK,
        "FAIL index create");
    buffer_cases((IUnknown*)vb, 0);
    buffer_cases((IUnknown*)ib, 1);
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], path[MAX_PATH], name[100];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    UINT at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    wsprintfA(name, "PRIV%u.TXT", D9WG_PRIVATE_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    RECT client = { 0, 0, 64, 64 };
    check(AdjustWindowRectEx(&client, WS_POPUP, FALSE, 0), "FAIL window dimensions");
    HWND window = CreateWindowExA(0, "STATIC", "D3D private data probe", WS_POPUP, 30, 40, client.right - client.left, client.bottom - client.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(window != NULL, "FAIL window");
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    RECT actual;
    check(GetClientRect(window, &actual) && actual.right == 64 && actual.bottom == 64, "FAIL exact client dimensions");
    wsprintfA(name, "D3D%u.DLL", D9WG_PRIVATE_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    HMODULE module = LoadLibraryA(path);
    check(module != NULL, "FAIL DLL");
    wsprintfA(name, "Direct3DCreate%u", D9WG_PRIVATE_API);
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
#ifdef D9WG_PRIVATE_SURFACES
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
#endif
#if D9WG_PRIVATE_API == 8
    check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device8");
#else
    check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device9");
#endif
    private_cases();
#ifdef D9WG_PRIVATE_TEXTURES
    for (UINT kind = 0; kind < 3; kind++)
        texture_cases(kind);
#endif
#ifdef D9WG_PRIVATE_SURFACES
    surface_cases(&pp);
#endif
    check(DEV(Release)(device) == 0, "FAIL device references");
#ifdef D9WG_PRIVATE_SURFACES
    check(back_counter.refs == 1 && depth_counter.refs == 1, "FAIL implicit store device cleanup");
    wsprintfA(name, "CHECK SURFACE DEVICE %u back=1 depth=1 released=0", D9WG_PRIVATE_API);
    report(name);
#endif
    FACTORY_RELEASE(factory);
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS private API=%u", D9WG_PRIVATE_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
