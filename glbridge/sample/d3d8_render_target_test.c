/* Actual XP D3D8 cube target/depth-only binding probe; no native HAL claim. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#if D9WG_DRAW_API == 8
#include <d3d8.h>
typedef IDirect3D8 Factory;
typedef IDirect3DDevice8 Device;
typedef IDirect3DVertexBuffer8 VertexBuffer;
typedef IDirect3DIndexBuffer8 IndexBuffer;
#define VB_LOCK IDirect3DVertexBuffer8_Lock
#define IB_LOCK IDirect3DIndexBuffer8_Lock
#define VB_UNLOCK IDirect3DVertexBuffer8_Unlock
#define IB_UNLOCK IDirect3DIndexBuffer8_Unlock
#define VB_RELEASE IDirect3DVertexBuffer8_Release
#define IB_RELEASE IDirect3DIndexBuffer8_Release
#define DEV(method) IDirect3DDevice8_##method
#define FACTORY_RELEASE IDirect3D8_Release
#else
#include <d3d9.h>
typedef IDirect3D9 Factory;
typedef IDirect3DDevice9 Device;
typedef IDirect3DVertexBuffer9 VertexBuffer;
typedef IDirect3DIndexBuffer9 IndexBuffer;
#define VB_LOCK IDirect3DVertexBuffer9_Lock
#define IB_LOCK IDirect3DIndexBuffer9_Lock
#define VB_UNLOCK IDirect3DVertexBuffer9_Unlock
#define IB_UNLOCK IDirect3DIndexBuffer9_Unlock
#define VB_RELEASE IDirect3DVertexBuffer9_Release
#define IB_RELEASE IDirect3DIndexBuffer9_Release
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

static const DWORD palette[6] = { 0xffff0000u, 0xff00ff00u, 0xff0000ffu, 0xffffff00u, 0xffff00ffu, 0xff00ffffu };
static void check_viewport(const D3DVIEWPORT8* expected)
{
    D3DVIEWPORT8 actual;
    check(DEV(GetViewport)(device, &actual) == D3D_OK && actual.X == expected->X && actual.Y == expected->Y && actual.Width == expected->Width && actual.Height == expected->Height && actual.MinZ == expected->MinZ && actual.MaxZ == expected->MaxZ, "FAIL viewport");
}
static void query_cube(IDirect3DCubeTexture8* cube, UINT width, IDirect3DSurface8** out)
{
    IDirect3DCubeTexture8* parent = NULL;
    D3DSURFACE_DESC desc;
    check(DEV(GetRenderTarget)(device, out) == D3D_OK, "FAIL current target query");
    check(IDirect3DSurface8_GetDesc(*out, &desc) == D3D_OK && desc.Width == width && desc.Height == width && desc.Format == D3DFMT_X8R8G8B8, "FAIL current target desc");
    check(IDirect3DSurface8_GetContainer(*out, &IID_IDirect3DCubeTexture8, (void**)&parent) == D3D_OK && parent == cube, "FAIL current target parent");
    IDirect3DCubeTexture8_Release(parent);
}
static void verify_color(IDirect3DSurface8* image, UINT size, DWORD color)
{
    D3DLOCKED_RECT lock;
    check(IDirect3DSurface8_LockRect(image, &lock, NULL, D3DLOCK_READONLY) == D3D_OK, "FAIL image lock");
    for (UINT y = 0; y < size; ++y)
        for (UINT x = 0; x < size; ++x)
            check(((DWORD*)((BYTE*)lock.pBits + y * lock.Pitch))[x] == color, "FAIL cube/back pixels");
    check(IDirect3DSurface8_UnlockRect(image) == D3D_OK, "FAIL image unlock");
}
static void draw(UINT size, DWORD color)
{
    struct {
        float x, y, z, rhw;
        DWORD color;
    } v[4] = { { 0, 0, .5f, 1, color }, { size, 0, .5f, 1, color }, { 0, size, .5f, 1, color }, { size, size, .5f, 1, color } };
    check(DEV(BeginScene)(device) == D3D_OK, "FAIL begin");
    check(DEV(DrawPrimitiveUP)(device, D3DPT_TRIANGLESTRIP, 2, v, sizeof(v[0])) == D3D_OK, "FAIL draw");
    check(DEV(EndScene)(device) == D3D_OK, "FAIL end");
}
static void targets(void)
{
    IDirect3DSurface8 *back = NULL, *depth64 = NULL, *depth128 = NULL, *depth16 = NULL, *image64 = NULL, *image32 = NULL, *front = NULL;
    IDirect3DCubeTexture8* cube = NULL;
    check(DEV(GetRenderTarget)(device, &back) == D3D_OK, "FAIL original target");
    check(DEV(Clear)(device, 0, NULL, D3DCLEAR_TARGET, 0xff204060u, 1, 0) == D3D_OK, "FAIL back clear");
    check(DEV(CreateDepthStencilSurface)(device, 64, 64, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, &depth64) == D3D_OK, "FAIL depth64");
    check(DEV(CreateDepthStencilSurface)(device, 128, 128, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, &depth128) == D3D_OK, "FAIL depth128");
    check(DEV(CreateDepthStencilSurface)(device, 16, 16, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, &depth16) == D3D_OK, "FAIL depth16");
    check(DEV(CreateCubeTexture)(device, 64, 2, D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &cube) == D3D_OK, "FAIL cube RT");
    check(DEV(CreateImageSurface)(device, 64, 64, D3DFMT_X8R8G8B8, &image64) == D3D_OK, "FAIL image64");
    check(DEV(CreateImageSurface)(device, 32, 32, D3DFMT_X8R8G8B8, &image32) == D3D_OK, "FAIL image32");
    check(DEV(CreateImageSurface)(device, 64, 64, D3DFMT_A8R8G8B8, &front) == D3D_OK, "FAIL front image");
    DEV(SetRenderState)(device, D3DRS_LIGHTING, FALSE);
    DEV(SetRenderState)(device, D3DRS_ZENABLE, TRUE);
    DEV(SetRenderState)(device, D3DRS_ZWRITEENABLE, TRUE);
    DEV(SetRenderState)(device, D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    DEV(SetRenderState)(device, D3DRS_CULLMODE, D3DCULL_NONE);
    DEV(SetRenderState)(device, D3DRS_ALPHABLENDENABLE, FALSE);
    DEV(SetRenderState)(device, D3DRS_FOGENABLE, FALSE);
    DEV(SetTexture)(device, 0, NULL);
    DEV(SetTextureStageState)(device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    DEV(SetTextureStageState)(device, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    DEV(SetTextureStageState)(device, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    DEV(SetTextureStageState)(device, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    check(DEV(SetVertexShader)(device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE) == D3D_OK, "FAIL FVF");
    for (UINT face = 0; face < 6; ++face)
        for (UINT mip = 0; mip < 2; ++mip) {
            UINT size = 64u >> mip;
            DWORD color = palette[(face + 2 * mip) % 6];
            IDirect3DSurface8 *surface = NULL, *queried = NULL, *queried_depth = NULL;
            check(IDirect3DCubeTexture8_GetCubeMapSurface(cube, (D3DCUBEMAP_FACES)face, mip, &surface) == D3D_OK, "FAIL cube surface");
            check(DEV(SetRenderTarget)(device, surface, depth64) == D3D_OK, "FAIL cube bind");
            D3DVIEWPORT8 full = { 0, 0, size, size, 0, 1 }, custom = { 1, 2, size - 2, size - 4, .25f, .75f };
            check_viewport(&full);
            check(DEV(SetViewport)(device, &custom) == D3D_OK, "FAIL custom viewport");
            check(DEV(SetRenderTarget)(device, NULL, depth128) == D3D_OK, "FAIL depth-only bind");
            check_viewport(&custom);
            check(DEV(SetRenderTarget)(device, NULL, NULL) == D3D_OK, "FAIL depth unbind");
            check_viewport(&custom);
            queried_depth = (void*)0x1234;
            check(DEV(GetDepthStencilSurface)(device, &queried_depth) == D3DERR_NOTFOUND && !queried_depth, "FAIL missing depth query");
            check(DEV(BeginStateBlock)(device) == D3D_OK, "FAIL begin recording");
            check(DEV(SetRenderTarget)(device, surface, depth64) == D3D_OK, "FAIL recorded-time cube bind");
            check_viewport(&full);
            DWORD block = 0;
            check(DEV(EndStateBlock)(device, &block) == D3D_OK, "FAIL end recording");
            check(DEV(SetViewport)(device, &custom) == D3D_OK, "FAIL second custom viewport");
            check(DEV(ApplyStateBlock)(device, block) == D3D_OK, "FAIL apply empty recorded block");
            check_viewport(&custom);
            check(DEV(DeleteStateBlock)(device, block) == D3D_OK, "FAIL delete block");
            check(DEV(SetRenderTarget)(device, NULL, depth128) == D3D_OK, "FAIL second depth-only bind");
            check(DEV(GetDepthStencilSurface)(device, &queried_depth) == D3D_OK && queried_depth == depth128, "FAIL explicit depth query");
            IDirect3DSurface8_Release(queried_depth);
            check(DEV(SetRenderTarget)(device, NULL, depth16) == D3DERR_INVALIDCALL, "FAIL undersized depth accepted");
            check_viewport(&custom);
            check(DEV(SetViewport)(device, &full) == D3D_OK, "FAIL full viewport");
            check(DEV(Clear)(device, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff101010u, 1, 0) == D3D_OK, "FAIL cube clear");
            draw(size, color);
            query_cube(cube, size, &queried);
            IDirect3DSurface8* image = mip ? image32 : image64;
            check(DEV(CopyRects)(device, queried, NULL, 0, image, NULL) == D3D_OK, "FAIL queried cube readback");
            verify_color(image, size, color);
            IDirect3DSurface8_Release(queried);
            IDirect3DSurface8_Release(surface);
            char line[100];
            wsprintfA(line, "FACE %u %u %u %08lX", face, mip, size, color);
            report(line);
        }
    IDirect3DCubeTexture8* original_cube = cube;
    IDirect3DCubeTexture8_Release(cube);
    check(DEV(SetRenderTarget)(device, NULL, NULL) == D3D_OK, "FAIL held cube depth unbind");
    IDirect3DSurface8* held = NULL;
    query_cube(original_cube, 32, &held);
    IDirect3DSurface8_Release(held);
    check(DEV(SetRenderTarget)(device, back, NULL) == D3D_OK, "FAIL restore back buffer");
    D3DVIEWPORT8 restored = { 0, 0, 64, 64, 0, 1 };
    check_viewport(&restored);
    check(DEV(GetFrontBuffer)(device, front) == D3D_OK, "FAIL back preservation read");
    verify_color(front, 64, 0xff204060u);
    report("CHECK cube lifetime, depth-only/recorded viewport and unchanged back buffer");
    IDirect3DSurface8_Release(front);
    IDirect3DSurface8_Release(image64);
    IDirect3DSurface8_Release(image32);
    IDirect3DSurface8_Release(depth64);
    IDirect3DSurface8_Release(depth128);
    IDirect3DSurface8_Release(depth16);
    IDirect3DSurface8_Release(back);
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], path[MAX_PATH], name[40];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    UINT at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    lstrcpyA(name, "TARGET.TXT");
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    HWND window = CreateWindowExA(0, "STATIC", "D3D8 render target probe", WS_OVERLAPPEDWINDOW, 30, 40, 160, 160, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(window != NULL, "FAIL window");
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    wsprintfA(name, "D3D%u.DLL", D9WG_DRAW_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    HMODULE module = LoadLibraryA(path);
    check(module != NULL, "FAIL DLL");
    wsprintfA(name, "Direct3DCreate%u", D9WG_DRAW_API);
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
#if D9WG_DRAW_API == 8
    check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device8");
#else
    check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device9");
#endif
    targets();
    DEV(Release)(device);
    FACTORY_RELEASE(factory);
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS D3D8 targets API=%u", D9WG_DRAW_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
