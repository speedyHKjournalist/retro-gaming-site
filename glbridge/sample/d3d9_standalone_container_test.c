/* Documented device containers for standalone D3D9 GPU and CPU surfaces. */
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
static const GUID bad_id = {0x12345678,1,2,{3,4,5,6,7,8,9,10}};
static const GUID* ids[] = {&IID_IUnknown,&IID_IDirect3DDevice9,&IID_IDirect3DResource9,&IID_IDirect3DBaseTexture9,&IID_IDirect3DTexture9,&IID_IDirect3DCubeTexture9,&IID_IDirect3DSurface9,&IID_IDirect3DSwapChain9,&bad_id};
static void container_cases(void)
{
    for (UINT kind = 0; kind < 5; kind++) {
        Surface* surface = NULL;
        HRESULT hr;
        if (kind == 0)
            hr = DEV(CreateRenderTarget)(device,32,24,D3DFMT_X8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&surface,NULL);
        else if (kind == 1)
            hr = DEV(CreateDepthStencilSurface)(device,32,24,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&surface,NULL);
        else
            hr = DEV(CreateOffscreenPlainSurface)(device,32,24,D3DFMT_X8R8G8B8,kind == 2 ? D3DPOOL_DEFAULT : kind == 3 ? D3DPOOL_SYSTEMMEM : D3DPOOL_SCRATCH,&surface,NULL);
        check(hr == D3D_OK && surface && refs((IUnknown*)surface) == 1, "FAIL standalone container create");
        ULONG before = refs((IUnknown*)device);
        for (UINT cycle = 0; cycle < 8; cycle++) for (UINT id = 0; id < 9; id++) {
            void* out = (void*)(uintptr_t)0x1234;
            HRESULT result = SURFACE(GetContainer)(surface, ids[id], &out);
            if (id < 2) {
                check(result == D3D_OK && out == device && refs((IUnknown*)device) == before + 1 && refs((IUnknown*)surface) == 1, "FAIL standalone device container identity/pin");
                check(IUnknown_Release((IUnknown*)out) == before, "FAIL standalone container query reference");
            } else
                check(result == E_NOINTERFACE && !out, "FAIL standalone hidden texture/interface exposure");
            check(refs((IUnknown*)device) == before && refs((IUnknown*)surface) == 1, "FAIL standalone container balance");
        }
        void* out = (void*)(uintptr_t)0x1234;
        check(SURFACE(GetContainer)(surface, NULL, &out) == E_NOINTERFACE && !out && SURFACE(GetContainer)(surface, &IID_IUnknown, NULL) == D3DERR_INVALIDCALL && refs((IUnknown*)device) == before, "FAIL standalone NULL outputs");
        check(SURFACE(Release)(surface) == 0 && refs((IUnknown*)device) == 1, "FAIL standalone container final cleanup");
        char line[160];
        wsprintfA(line,"SURFACE CONTAINER kind=%u rounds=8 deviceQueries=16 refused=56 nullGuards=2 balanced=1 deviceRefs=1",kind);
        report(line);
    }
    for (UINT cube = 0; cube < 2; cube++) {
        IDirect3DTexture9* texture = NULL;
        IDirect3DCubeTexture9* cubemap = NULL;
        Surface* surface = NULL;
        if (cube) {
            check(DEV(CreateCubeTexture)(device,8,1,0,D3DFMT_X8R8G8B8,D3DPOOL_MANAGED,&cubemap,NULL) == D3D_OK && cubemap, "FAIL cube container control create");
            check(IDirect3DCubeTexture9_GetCubeMapSurface(cubemap,D3DCUBEMAP_FACE_POSITIVE_X,0,&surface) == D3D_OK && surface, "FAIL cube container control surface");
        } else {
            check(DEV(CreateTexture)(device,8,8,1,0,D3DFMT_X8R8G8B8,D3DPOOL_MANAGED,&texture,NULL) == D3D_OK && texture, "FAIL texture container control create");
            check(IDirect3DTexture9_GetSurfaceLevel(texture,0,&surface) == D3D_OK && surface, "FAIL texture container control surface");
        }
        IUnknown* parent = cube ? (IUnknown*)cubemap : (IUnknown*)texture;
        ULONG before = refs(parent), before_device = refs((IUnknown*)device);
        check(before == 2, "FAIL texture/cube shared counter control");
        for (UINT id = 0; id < 9; id++) {
            void* out = (void*)(uintptr_t)0x1234;
            BOOL valid = id == 0 || id == 2 || id == 3 || id == (cube ? 5 : 4);
            HRESULT result = SURFACE(GetContainer)(surface,ids[id],&out);
            if (valid) {
                check(result == D3D_OK && out == parent && refs(parent) == before + 1, "FAIL texture/cube container control identity");
                check(IUnknown_Release((IUnknown*)out) == before, "FAIL texture/cube container control query ref");
            } else
                check(result == E_NOINTERFACE && !out, "FAIL texture/cube container control refused interface");
            check(refs(parent) == before && refs((IUnknown*)device) == before_device, "FAIL texture/cube container control device isolation");
        }
        SURFACE(Release)(surface);
        check(IUnknown_Release(parent) == 0 && refs((IUnknown*)device) == 1, "FAIL texture/cube container control cleanup");
        report(cube ? "SURFACE CONTROL kind=cube accepted=4 refused=5 shared=1 deviceRefs=1" : "SURFACE CONTROL kind=texture accepted=4 refused=5 shared=1 deviceRefs=1");
    }
    report("SURFACE STATUS profiles=5 deviceQueries=80 refused=280 nullGuards=10 controls=2 hiddenTextureExposed=0 deviceRefs=1");
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], path[MAX_PATH], name[100];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    UINT at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    wsprintfA(name, "SCN%u.TXT", D9WG_IMPLICIT_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    RECT client = { 0, 0, 64, 64 };
    check(AdjustWindowRectEx(&client, WS_POPUP, FALSE, 0), "FAIL window dimensions");
    HWND window = CreateWindowExA(0, "STATIC", "D3D9 standalone container probe", WS_POPUP, 30, 40, client.right - client.left, client.bottom - client.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
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
    container_cases();
    check(DEV(Release)(device) == 0, "FAIL device references");
    check(FACTORY_RELEASE(factory) == 0, "FAIL final container factory references");
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS standalone containers API=%u", D9WG_IMPLICIT_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
