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
static const DWORD colors[] = { 0xff2468ac, 0xff359bce, 0xff46acdf };
static void verify_geometry(UINT step, UINT width, UINT height, UINT requested_width, D3DPRESENT_PARAMETERS* pp, HWND window)
{
    check(pp->BackBufferCount == 1 && !pp->hDeviceWindow && pp->BackBufferFormat == D3DFMT_X8R8G8B8, "FAIL returned count/window/format");
#if D9WG_IMPLICIT_API == 8
    check(pp->BackBufferWidth == requested_width && !pp->BackBufferHeight, "FAIL D3D8 caller dimensions changed");
    D3DVIEWPORT8 viewport;
#else
    (void)requested_width;
    check(pp->BackBufferWidth == width && pp->BackBufferHeight == height, "FAIL D3D9 returned dimensions");
    D3DVIEWPORT9 viewport;
    RECT scissor;
    check(DEV(GetScissorRect)(device, &scissor) == D3D_OK && scissor.left == 0 && scissor.top == 0 && scissor.right == (LONG)width && scissor.bottom == (LONG)height, "FAIL effective scissor");
    Chain* chain = NULL;
    D3DPRESENT_PARAMETERS effective;
    check(DEV(GetSwapChain)(device, 0, &chain) == D3D_OK && chain && CHAIN(GetPresentParameters)(chain, &effective) == D3D_OK, "FAIL effective present query");
    check(effective.BackBufferWidth == width && effective.BackBufferHeight == height && effective.BackBufferCount == 1 && effective.BackBufferFormat == D3DFMT_X8R8G8B8 && effective.hDeviceWindow == window, "FAIL cached present dimensions/window");
    CHAIN(Release)(chain);
#endif
    (void)window;
    check(DEV(GetViewport)(device, &viewport) == D3D_OK && viewport.X == 0 && viewport.Y == 0 && viewport.Width == width && viewport.Height == height && viewport.MinZ == 0 && viewport.MaxZ == 1, "FAIL default effective viewport");
    check(DEV(SetViewport)(device, &viewport) == D3D_OK, "FAIL full-client viewport rejected");
    Surface *back = NULL, *target = NULL, *depth = NULL;
#if D9WG_IMPLICIT_API == 8
    check(DEV(GetBackBuffer)(device, 0, D3DBACKBUFFER_TYPE_MONO, &back) == D3D_OK && DEV(GetRenderTarget)(device, &target) == D3D_OK, "FAIL default color queries");
#else
    check(DEV(GetBackBuffer)(device, 0, 0, D3DBACKBUFFER_TYPE_MONO, &back) == D3D_OK && DEV(GetRenderTarget)(device, 0, &target) == D3D_OK, "FAIL default color queries");
#endif
    check(back && target == back && DEV(GetDepthStencilSurface)(device, &depth) == D3D_OK && depth, "FAIL implicit target identities");
    D3DSURFACE_DESC color_desc, depth_desc;
    check(SURFACE(GetDesc)(back, &color_desc) == D3D_OK && SURFACE(GetDesc)(depth, &depth_desc) == D3D_OK, "FAIL target descriptions");
    check(color_desc.Width == width && color_desc.Height == height && color_desc.Format == D3DFMT_X8R8G8B8 && depth_desc.Width == width && depth_desc.Height == height && depth_desc.Format == D3DFMT_D24S8, "FAIL guest color/depth geometry");
    SURFACE(Release)(back);
    SURFACE(Release)(target);
    SURFACE(Release)(depth);
    check(refs((IUnknown*)device) == 1, "FAIL geometry query reference balance");
    check(DEV(Clear)(device, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, colors[step], 1, 0) == D3D_OK && DEV(Present)(device, NULL, NULL, NULL, NULL) == D3D_OK, "FAIL geometry frame");
    char line[180];
    wsprintfA(line, "PRESENT GEOMETRY api=%u step=%u width=%u height=%u callerWidth=%u callerHeight=%u count=1 hWindowNull=1", D9WG_IMPLICIT_API, step, width, height, pp->BackBufferWidth, pp->BackBufferHeight);
    report(line);
}
static void geometry_cases(D3DPRESENT_PARAMETERS* initial, HWND window)
{
    verify_geometry(0, 128, 96, 0, initial, window);
    const UINT widths[] = { 160, 96 }, heights[] = { 112, 80 };
    for (UINT step = 1; step < 3; step++) {
        check(SetWindowPos(window, NULL, 30, 40, widths[step - 1], heights[step - 1], SWP_NOZORDER | SWP_NOACTIVATE), "FAIL resize client");
        RECT client;
        check(GetClientRect(window, &client) && client.right == (LONG)widths[step - 1] && client.bottom == (LONG)heights[step - 1], "FAIL resized exact client");
        D3DPRESENT_PARAMETERS p;
        ZeroMemory(&p, sizeof(p));
        p.Windowed = TRUE;
        p.SwapEffect = D3DSWAPEFFECT_DISCARD;
        p.BackBufferFormat = D3DFMT_X8R8G8B8;
        p.EnableAutoDepthStencil = TRUE;
        p.AutoDepthStencilFormat = D3DFMT_D24S8;
        p.BackBufferWidth = step == 2 ? 96 : 0;
        check(DEV(Reset)(device, &p) == D3D_OK && DEV(TestCooperativeLevel)(device) == D3D_OK, "FAIL geometry Reset");
        verify_geometry(step, widths[step - 1], heights[step - 1], step == 2 ? 96 : 0, &p, window);
    }
    report("PRESENT GEOMETRY STATUS frames=3 resets=2 viewport=3 color=3 depth=3 count=1");
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], path[MAX_PATH], name[100];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    UINT at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    wsprintfA(name, "GEO%u.TXT", D9WG_IMPLICIT_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    RECT client = { 0, 0, 128, 96 };
    check(AdjustWindowRectEx(&client, WS_POPUP, FALSE, 0), "FAIL window dimensions");
    HWND window = CreateWindowExA(0, "STATIC", "D3D presentation geometry probe", WS_POPUP, 30, 40, client.right - client.left, client.bottom - client.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(window != NULL, "FAIL window");
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    RECT actual;
    check(GetClientRect(window, &actual) && actual.right == 128 && actual.bottom == 96, "FAIL exact client dimensions");
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
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
#if D9WG_IMPLICIT_API == 8
    check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device8");
#else
    check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device9");
#endif
    check(DEV(TestCooperativeLevel)(device) == D3D_OK, "FAIL initial cooperative status");
    geometry_cases(&pp, window);
    check(DEV(Release)(device) == 0, "FAIL device references");
    FACTORY_RELEASE(factory);
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS present geometry API=%u", D9WG_IMPLICIT_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
