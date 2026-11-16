/* Reset resource/status and managed-pixel SDK probe, without a C runtime. */
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
static const GUID key = { 0x10293847, 1, 2, { 3, 4, 5, 6, 7, 8, 9, 10 } };
static Texture* marker;
static UINT accepted, refused, scene_resets, refused_ends, fresh_pairs;
static HRESULT refusal_hr, cooperative_hr, lost_present_hr;
static void open_reset_scene(void)
{
    check(DEV(BeginScene)(device) == D3D_OK, "FAIL pre-Reset BeginScene");
    scene_resets++;
}
static void accept_reset(D3DPRESENT_PARAMETERS* parameters)
{
    D3DPRESENT_PARAMETERS p = *parameters;
    check(DEV(Reset)(device, &p) == D3D_OK, "FAIL accepted resource Reset");
    check(DEV(TestCooperativeLevel)(device) == D3D_OK, "FAIL successful Reset cooperative status");
    check(DEV(EndScene)(device) == D3DERR_INVALIDCALL, "FAIL Reset retained active Scene");
    refused_ends++;
    check(DEV(BeginScene)(device) == D3D_OK && DEV(EndScene)(device) == D3D_OK, "FAIL fresh Scene after Reset");
    fresh_pairs++;
    DWORD value = 0, size = 4;
    check(TEX(GetPrivateData)(marker, &key, &value, &size) == D3D_OK && size == 4 && value == 0x11223344u, "FAIL managed private data after Reset");
    accepted++;
}
static void refuse_reset(D3DPRESENT_PARAMETERS* parameters)
{
    D3DPRESENT_PARAMETERS p = *parameters;
#if D9WG_RESET_API == 8
    const HRESULT expected = D3DERR_DEVICELOST;
#else
    const HRESULT expected = D3DERR_INVALIDCALL;
#endif
    refusal_hr = DEV(Reset)(device, &p);
    check(refusal_hr == expected, "FAIL resource Reset HRESULT");
    cooperative_hr = DEV(TestCooperativeLevel)(device);
    check(cooperative_hr == D3DERR_DEVICENOTRESET
            && DEV(TestCooperativeLevel)(device) == D3DERR_DEVICENOTRESET,
        "FAIL refused Reset cooperative status");
    lost_present_hr = DEV(Present)(device, NULL, NULL, NULL, NULL);
    check(lost_present_hr == D3DERR_DEVICELOST,
        "FAIL lost Present HRESULT");
    check(DEV(TestCooperativeLevel)(device) == D3DERR_DEVICENOTRESET,
        "FAIL Present changed cooperative status");
    refused++;
}
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
static HRESULT get_implicit(UINT kind, Surface** out)
{
    if (kind)
        return DEV(GetDepthStencilSurface)(device, out);
#if D9WG_RESET_API == 8
    return DEV(GetBackBuffer)(device, 0, D3DBACKBUFFER_TYPE_MONO, out);
#else
    return DEV(GetBackBuffer)(device, 0, 0, D3DBACKBUFFER_TYPE_MONO, out);
#endif
}
static void resource_cases(D3DPRESENT_PARAMETERS* base)
{
    check(DEV(CreateTexture)(device, 4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &marker
#if D9WG_RESET_API == 9
              ,
              NULL
#endif
              )
            == D3D_OK,
        "FAIL managed marker create");
    DWORD value = 0x11223344u;
    check(TEX(SetPrivateData)(marker, &key, &value, 4, 0) == D3D_OK, "FAIL marker private data");
    D3DLOCKED_RECT map;
    check(TEX(LockRect)(marker, 0, &map, NULL, 0) == D3D_OK, "FAIL marker lock");
    for (UINT y = 0; y < 4; y++)
        for (UINT x = 0; x < 4; x++) {
            BYTE* p = (BYTE*)map.pBits + y * map.Pitch + x * 4;
            p[0] = 0x20;
            p[1] = 0x40;
            p[2] = 0xa0;
            p[3] = 0xff;
        }
    check(TEX(UnlockRect)(marker, 0) == D3D_OK, "FAIL marker unlock");
    for (UINT kind = 0; kind < 5; kind++) {
        IUnknown* resource = NULL;
        check(make_resource(kind, D3DPOOL_DEFAULT, &resource) == D3D_OK && resource, "FAIL held resource create");
        refuse_reset(base);
        IUnknown_Release(resource);
        check(DEV(TestCooperativeLevel)(device) == D3DERR_DEVICENOTRESET,
            "FAIL resource release recovered device without Reset");
        accept_reset(base);
    }
    for (UINT kind = 0; kind < 5; kind++) {
        IUnknown* resource = NULL;
        check(make_resource(kind, D3DPOOL_DEFAULT, &resource) == D3D_OK && resource && bind_resource(kind, resource) == D3D_OK, "FAIL device-owned resource bind");
        IUnknown_Release(resource);
        open_reset_scene();
        accept_reset(base);
    }
    for (UINT pool = D3DPOOL_SYSTEMMEM; pool <= D3DPOOL_SCRATCH; pool++) {
        IUnknown* resource = NULL;
        check(make_resource(2, (D3DPOOL)pool, &resource) == D3D_OK && resource, "FAIL CPU resource create");
        Texture* texture = (Texture*)resource;
        check(TEX(LockRect)(texture, 0, &map, NULL, 0) == D3D_OK && map.pBits, "FAIL CPU resource lock");
        BYTE* bytes = map.pBits;
        bytes[0] = (BYTE)(0x21u + pool);
        bytes[1] = 0x43;
        bytes[2] = 0x65;
        bytes[3] = 0x87;
        open_reset_scene();
        accept_reset(base);
        check(bytes[0] == (BYTE)(0x21u + pool) && bytes[1] == 0x43 && bytes[2] == 0x65 && bytes[3] == 0x87, "FAIL mapped CPU storage changed");
        check(TEX(UnlockRect)(texture, 0) == D3D_OK, "FAIL CPU lock state lost");
        IUnknown_Release(resource);
    }
    for (UINT kind = 0; kind < 2; kind++) {
        Surface* surface = NULL;
        check(get_implicit(kind, &surface) == D3D_OK && surface, "FAIL held implicit get");
        refuse_reset(base);
        SURFACE(Release)(surface);
        check(DEV(TestCooperativeLevel)(device) == D3DERR_DEVICENOTRESET,
            "FAIL implicit release recovered device without Reset");
        accept_reset(base);
    }
    Surface *target = NULL, *depth = NULL;
    check(DEV(CreateRenderTarget)(device, 4, 4, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_NONE,
#if D9WG_RESET_API == 9
              0,
#endif
              FALSE, &target
#if D9WG_RESET_API == 9
              ,
              NULL
#endif
              )
            == D3D_OK,
        "FAIL target create");
    check(DEV(CreateDepthStencilSurface)(device, 4, 4, D3DFMT_D24S8, D3DMULTISAMPLE_NONE,
#if D9WG_RESET_API == 9
              0, TRUE,
#endif
              &depth
#if D9WG_RESET_API == 9
              ,
              NULL
#endif
              )
            == D3D_OK,
        "FAIL depth create");
#if D9WG_RESET_API == 8
    check(DEV(SetRenderTarget)(device, target, depth) == D3D_OK, "FAIL explicit target bind");
#else
    check(DEV(SetRenderTarget)(device, 0, target) == D3D_OK && DEV(SetDepthStencilSurface)(device, depth) == D3D_OK, "FAIL explicit target bind");
#endif
    SURFACE(Release)(target);
    SURFACE(Release)(depth);
    open_reset_scene();
    accept_reset(base);
    check(get_implicit(0, &target) == D3D_OK && get_implicit(1, &depth) == D3D_OK, "FAIL restored implicit targets");
    D3DSURFACE_DESC desc;
    check(SURFACE(GetDesc)(target, &desc) == D3D_OK && desc.Width == 64 && desc.Height == 64, "FAIL restored target dimensions");
    SURFACE(Release)(target);
    SURFACE(Release)(depth);
    check(accepted == 15 && refused == 7, "FAIL resource scenario counts");
    check(TEX(LockRect)(marker, 0, &map, NULL, D3DLOCK_READONLY) == D3D_OK, "FAIL preserved marker lock");
    BYTE* bytes = map.pBits;
    check(bytes[0] == 0x20 && bytes[1] == 0x40 && bytes[2] == 0xa0 && bytes[3] == 0xff && TEX(UnlockRect)(marker, 0) == D3D_OK, "FAIL preserved marker bytes");
    check(DEV(SetTexture)(device, 0, (BaseTexture*)marker) == D3D_OK, "FAIL marker bind");
    check(DEV(SetRenderState)(device, D3DRS_ZENABLE, FALSE) == D3D_OK && DEV(SetRenderState)(device, D3DRS_LIGHTING, FALSE) == D3D_OK && DEV(SetRenderState)(device, D3DRS_CULLMODE, D3DCULL_NONE) == D3D_OK, "FAIL frame states");
    check(DEV(SetTextureStageState)(device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1) == D3D_OK && DEV(SetTextureStageState)(device, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE) == D3D_OK && DEV(SetTextureStageState)(device, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1) == D3D_OK && DEV(SetTextureStageState)(device, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE) == D3D_OK, "FAIL frame texture states");
    struct Vertex {
        float x, y, z, rhw;
        DWORD color;
        float u, v;
    } quad[4] = { { 16, 16, 0, 1, 0xffffffffu, .5f, .5f }, { 48, 16, 0, 1, 0xffffffffu, .5f, .5f }, { 16, 48, 0, 1, 0xffffffffu, .5f, .5f }, { 48, 48, 0, 1, 0xffffffffu, .5f, .5f } };
#if D9WG_RESET_API == 8
    check(DEV(SetVertexShader)(device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1) == D3D_OK, "FAIL FVF");
#else
    check(DEV(SetFVF)(device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE | D3DFVF_TEX1) == D3D_OK, "FAIL FVF");
#endif
    check(DEV(Clear)(device, 0, NULL, D3DCLEAR_TARGET, 0xff30a070u, 1, 0) == D3D_OK && DEV(BeginScene)(device) == D3D_OK && DEV(DrawPrimitiveUP)(device, D3DPT_TRIANGLESTRIP, 2, quad, sizeof(quad[0])) == D3D_OK && DEV(EndScene)(device) == D3D_OK && DEV(Present)(device, NULL, NULL, NULL, NULL) == D3D_OK, "FAIL post-Reset frame");
    TEX(Release)(marker);
    char line[224];
    wsprintfA(line, "RESET GRAPH %u accepted=15 refused=7 families=5 cpuMapped=2 implicit=2 ownedTargets=1 private=11223344 frame=1", D9WG_RESET_API);
    report(line);
    wsprintfA(line, "RESET STATUS %u refused=7 recovered=15 lostPresents=7 coopNotReset=28 coopReady=15 resetHR=%08lX coopHR=%08lX presentHR=%08lX", D9WG_RESET_API, (DWORD)refusal_hr, (DWORD)cooperative_hr, (DWORD)lost_present_hr);
    report(line);
    check(scene_resets == 8 && refused_ends == 15 && fresh_pairs == 15, "FAIL Reset Scene counts");
    wsprintfA(line, "RESET SCENE %u resetOpen=%u endRefused=%u freshPairs=%u", D9WG_RESET_API, scene_resets, refused_ends, fresh_pairs);
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
    wsprintfA(name, "RST%u.TXT", D9WG_RESET_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    RECT client = { 0, 0, 64, 64 };
    check(AdjustWindowRectEx(&client, WS_POPUP, FALSE, 0), "FAIL window dimensions");
    HWND window = CreateWindowExA(0, "STATIC", "D3D Reset resource graph probe", WS_POPUP, 30, 40, client.right - client.left, client.bottom - client.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
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
    resource_cases(&pp);
    check(DEV(Release)(device) == 0, "FAIL device references");
    FACTORY_RELEASE(factory);
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS reset API=%u", D9WG_RESET_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
