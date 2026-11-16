/* Foreign-device target rejection must preserve every binding and reference. */
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
static void verify_state(Device* target, Surface* back, Surface* depth)
{
    Surface* got = NULL;
    check(DEV(GetRenderTarget)(target,0,&got) == D3D_OK && got == back, "FAIL foreign target changed slot zero");
    SURFACE(Release)(got);
    for (UINT slot = 1; slot < 4; slot++) {
        got = (void*)(uintptr_t)0x1234;
        check(DEV(GetRenderTarget)(target,slot,&got) == D3DERR_NOTFOUND && !got, "FAIL foreign target changed empty slot");
    }
    check(DEV(GetDepthStencilSurface)(target,&got) == D3D_OK && got == depth, "FAIL foreign target changed depth binding");
    SURFACE(Release)(got);
    D3DVIEWPORT9 v;
    RECT r;
    check(DEV(GetViewport)(target,&v) == D3D_OK && v.X == 5 && v.Y == 6 && v.Width == 20 && v.Height == 18 && v.MinZ == 0.125f && v.MaxZ == 0.875f, "FAIL foreign target changed viewport");
    check(DEV(GetScissorRect)(target,&r) == D3D_OK && r.left == 7 && r.top == 8 && r.right == 12 && r.bottom == 14, "FAIL foreign target changed scissor");
}
static void owner_cases(HWND window, const D3DPRESENT_PARAMETERS* base)
{
    D3DPRESENT_PARAMETERS p = *base;
    Device* other = NULL;
    check(IDirect3D9_CreateDevice(factory,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE,&p,&other) == D3D_OK && other, "FAIL second target-owner device");
    Surface *back = NULL, *depth = NULL;
    check(DEV(GetRenderTarget)(device,0,&back) == D3D_OK && back && DEV(GetDepthStencilSurface)(device,&depth) == D3D_OK && depth, "FAIL target-owner saved implicit views");
    D3DVIEWPORT9 v = {5,6,20,18,0.125f,0.875f};
    RECT r = {7,8,12,14};
    check(DEV(SetViewport)(device,&v) == D3D_OK && DEV(SetScissorRect)(device,&r) == D3D_OK, "FAIL target-owner initial custom state");
    for (UINT kind = 0; kind < 8; kind++) {
        Surface* foreign = NULL;
        IDirect3DTexture9* texture = NULL;
        IDirect3DCubeTexture9* cube = NULL;
        Chain* chain = NULL;
        HRESULT hr;
        if (kind == 0)
            hr = DEV(GetBackBuffer)(other,0,0,D3DBACKBUFFER_TYPE_MONO,&foreign);
        else if (kind == 1)
            hr = DEV(CreateRenderTarget)(other,32,24,D3DFMT_X8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&foreign,NULL);
        else if (kind == 2) {
            check(DEV(CreateTexture)(other,32,24,1,D3DUSAGE_RENDERTARGET,D3DFMT_X8R8G8B8,D3DPOOL_DEFAULT,&texture,NULL) == D3D_OK && texture, "FAIL foreign texture create");
            hr = IDirect3DTexture9_GetSurfaceLevel(texture,0,&foreign);
        } else if (kind == 3) {
            check(DEV(CreateCubeTexture)(other,32,1,D3DUSAGE_RENDERTARGET,D3DFMT_X8R8G8B8,D3DPOOL_DEFAULT,&cube,NULL) == D3D_OK && cube, "FAIL foreign cube create");
            hr = IDirect3DCubeTexture9_GetCubeMapSurface(cube,D3DCUBEMAP_FACE_NEGATIVE_Z,0,&foreign);
        } else if (kind == 4) {
            D3DPRESENT_PARAMETERS additional = *base;
            additional.BackBufferWidth = 32;
            additional.BackBufferHeight = 24;
            additional.EnableAutoDepthStencil = FALSE;
            check(DEV(CreateAdditionalSwapChain)(other,&additional,&chain) == D3D_OK && chain, "FAIL foreign chain create");
            hr = CHAIN(GetBackBuffer)(chain,0,D3DBACKBUFFER_TYPE_MONO,&foreign);
        } else if (kind == 5)
            hr = DEV(CreateDepthStencilSurface)(other,64,64,D3DFMT_D24S8,D3DMULTISAMPLE_NONE,0,TRUE,&foreign,NULL);
        else if (kind == 6)
            hr = DEV(GetDepthStencilSurface)(other,&foreign);
        else
            hr = DEV(CreateOffscreenPlainSurface)(other,32,24,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,&foreign,NULL);
        check(hr == D3D_OK && foreign, "FAIL foreign target profile");
        ULONG before = refs((IUnknown*)foreign), a_refs = refs((IUnknown*)device), b_refs = refs((IUnknown*)other);
        for (UINT cycle = 0; cycle < 8; cycle++) for (UINT slot = 0; slot < 4; slot++) {
            check(DEV(SetRenderTarget)(device,slot,foreign) == D3DERR_INVALIDCALL, "FAIL foreign target accepted");
            verify_state(device,back,depth);
            check(refs((IUnknown*)foreign) == before && refs((IUnknown*)device) == a_refs && refs((IUnknown*)other) == b_refs, "FAIL foreign target changed references");
        }
        SURFACE(Release)(foreign);
        if (texture) check(IDirect3DTexture9_Release(texture) == 0, "FAIL foreign texture cleanup");
        if (cube) check(IDirect3DCubeTexture9_Release(cube) == 0, "FAIL foreign cube cleanup");
        if (chain) check(CHAIN(Release)(chain) == 0, "FAIL foreign chain cleanup");
        check(refs((IUnknown*)other) == 1, "FAIL foreign target profile device references");
        char line[180];
        wsprintfA(line,"OWNER FOREIGN kind=%u rounds=8 slots=4 refused=32 referencesUnchanged=1 stateUnchanged=1 sourceDeviceRefs=1",kind);
        report(line);
    }
    check(DEV(Release)(other) == 0 && refs((IUnknown*)factory) == 2, "FAIL foreign target device cleanup");
    Surface* local = NULL;
    check(DEV(CreateRenderTarget)(device,32,24,D3DFMT_X8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&local,NULL) == D3D_OK && local, "FAIL local target control create");
    check(DEV(SetRenderTarget)(device,0,local) == D3D_OK && DEV(SetRenderTarget)(device,0,local) == D3D_OK, "FAIL local target control bind/rebind");
    Surface* got = NULL;
    check(DEV(GetRenderTarget)(device,0,&got) == D3D_OK && got == local, "FAIL local target control identity");
    SURFACE(Release)(got);
    D3DVIEWPORT9 actual;
    RECT actual_rect;
    check(DEV(GetViewport)(device,&actual) == D3D_OK && actual.X == 0 && actual.Y == 0 && actual.Width == 32 && actual.Height == 24, "FAIL local target control viewport");
    check(DEV(GetScissorRect)(device,&actual_rect) == D3D_OK && actual_rect.left == 0 && actual_rect.top == 0 && actual_rect.right == 32 && actual_rect.bottom == 24, "FAIL local target control scissor");
    check(DEV(SetRenderTarget)(device,0,back) == D3D_OK && SURFACE(Release)(local) == 0, "FAIL local target control restore");
    IDirect3DCubeTexture9* local_cube = NULL;
    check(DEV(CreateCubeTexture)(device,32,1,D3DUSAGE_RENDERTARGET,D3DFMT_X8R8G8B8,D3DPOOL_DEFAULT,&local_cube,NULL) == D3D_OK && local_cube, "FAIL local cube control create");
    check(IDirect3DCubeTexture9_GetCubeMapSurface(local_cube,D3DCUBEMAP_FACE_NEGATIVE_Z,0,&local) == D3D_OK && local && DEV(SetRenderTarget)(device,3,local) == D3D_OK, "FAIL local cube control bind");
    check(DEV(GetRenderTarget)(device,3,&got) == D3D_OK && got == local, "FAIL local cube control identity");
    SURFACE(Release)(got);
    check(DEV(SetRenderTarget)(device,3,NULL) == D3D_OK, "FAIL local cube control unbind");
    SURFACE(Release)(local);
    check(IDirect3DCubeTexture9_Release(local_cube) == 0, "FAIL local cube control cleanup");
    SURFACE(Release)(back);
    SURFACE(Release)(depth);
    report("OWNER STATUS foreignProfiles=8 refused=256 rounds=8 slots=4 localBindings=5 viewportScissorControls=1 allResourcesReleased=1");
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], path[MAX_PATH], name[100];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    UINT at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    wsprintfA(name, "OWN%u.TXT", D9WG_IMPLICIT_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    RECT client = { 0, 0, 64, 64 };
    check(AdjustWindowRectEx(&client, WS_POPUP, FALSE, 0), "FAIL window dimensions");
    HWND window = CreateWindowExA(0, "STATIC", "D3D9 target owner probe", WS_POPUP, 30, 40, client.right - client.left, client.bottom - client.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
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
    owner_cases(window,&pp);
    check(DEV(Release)(device) == 0, "FAIL device references");
    check(FACTORY_RELEASE(factory) == 0, "FAIL final container factory references");
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS target owners API=%u", D9WG_IMPLICIT_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
