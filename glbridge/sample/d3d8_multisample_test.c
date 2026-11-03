/* Actual XP D3D8 MSAA creation/depth matching probe; no native HAL claim. */
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
static void verify_color(IDirect3DSurface8* image, UINT size, DWORD color)
{
    D3DLOCKED_RECT lock;
    check(IDirect3DSurface8_LockRect(image, &lock, NULL, D3DLOCK_READONLY) == D3D_OK, "FAIL image lock");
    for (UINT y = 0; y < size; ++y)
        for (UINT x = 0; x < size; ++x)
            check(((DWORD*)((BYTE*)lock.pBits + y * lock.Pitch))[x] == color, "FAIL MSAA pixels");
    check(IDirect3DSurface8_UnlockRect(image) == D3D_OK, "FAIL image unlock");
}
static void draw(UINT size, DWORD color)
{
    struct {
        float x, y, z, rhw;
        DWORD color;
    /* Extend beyond the viewport so every MSAA sample is covered. This
     * probe measures resolve/lifetime, not the edge coverage pattern. */
    } v[4] = { { -1, -1, .5f, 1, color }, { size + 1, -1, .5f, 1, color }, { -1, size + 1, .5f, 1, color }, { size + 1, size + 1, .5f, 1, color } };
    check(DEV(BeginScene)(device) == D3D_OK, "FAIL begin");
    check(DEV(DrawPrimitiveUP)(device, D3DPT_TRIANGLESTRIP, 2, v, sizeof(v[0])) == D3D_OK, "FAIL draw");
    check(DEV(EndScene)(device) == D3D_OK, "FAIL end");
}
static void targets(void)
{
    IDirect3DSurface8 *back = NULL, *implicit = NULL, *depth0 = NULL, *depth4 = NULL, *image = NULL, *front = NULL;
    D3DSURFACE_DESC desc;
    check(DEV(GetRenderTarget)(device, &back) == D3D_OK, "FAIL original target");
    check(IDirect3DSurface8_GetDesc(back, &desc) == D3D_OK && desc.MultiSampleType == D3DMULTISAMPLE_4_SAMPLES, "FAIL back sample descriptor");
    check(DEV(GetDepthStencilSurface)(device, &implicit) == D3D_OK, "FAIL implicit depth");
    check(IDirect3DSurface8_GetDesc(implicit, &desc) == D3D_OK && desc.MultiSampleType == D3DMULTISAMPLE_4_SAMPLES && desc.Width == 64 && desc.Height == 64, "FAIL implicit sample descriptor");
    check(DEV(Clear)(device, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff204060u, 1, 0) == D3D_OK, "FAIL back clear");
    check(DEV(CreateDepthStencilSurface)(device, 32, 32, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, &depth0) == D3D_OK, "FAIL depth0");
    check(DEV(CreateDepthStencilSurface)(device, 32, 32, D3DFMT_D24S8, D3DMULTISAMPLE_4_SAMPLES, &depth4) == D3D_OK, "FAIL depth4");
    check(DEV(CreateImageSurface)(device, 32, 32, D3DFMT_X8R8G8B8, &image) == D3D_OK, "FAIL image");
    check(DEV(CreateImageSurface)(device, 64, 64, D3DFMT_A8R8G8B8, &front) == D3D_OK, "FAIL front");
    IDirect3DSurface8* rejected = NULL;
    check(DEV(CreateRenderTarget)(device, 32, 32, D3DFMT_X8R8G8B8, D3DMULTISAMPLE_4_SAMPLES, TRUE, &rejected) == D3DERR_INVALIDCALL, "FAIL lockable4 accepted");
    DEV(SetRenderState)(device, D3DRS_LIGHTING, FALSE);
    DEV(SetRenderState)(device, D3DRS_ZENABLE, TRUE);
    DEV(SetRenderState)(device, D3DRS_ZWRITEENABLE, TRUE);
    DEV(SetRenderState)(device, D3DRS_ZFUNC, D3DCMP_LESSEQUAL);
    DEV(SetRenderState)(device, D3DRS_CULLMODE, D3DCULL_NONE);
    DEV(SetRenderState)(device, D3DRS_ALPHABLENDENABLE, FALSE);
    DEV(SetTextureStageState)(device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    DEV(SetTextureStageState)(device, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    DEV(SetTextureStageState)(device, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    DEV(SetTextureStageState)(device, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    check(DEV(SetVertexShader)(device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE) == D3D_OK, "FAIL FVF");
    for (UINT n = 0; n < 16; ++n) {
        D3DMULTISAMPLE_TYPE ms = n & 1 ? D3DMULTISAMPLE_4_SAMPLES : D3DMULTISAMPLE_NONE;
        IDirect3DSurface8 *target = NULL, *queried = NULL, *matching = ms ? depth4 : depth0, *wrong = ms ? depth0 : depth4;
        check(DEV(CreateRenderTarget)(device, 32, 32, D3DFMT_X8R8G8B8, ms, FALSE, &target) == D3D_OK, "FAIL target create");
        check(IDirect3DSurface8_GetDesc(target, &desc) == D3D_OK && desc.MultiSampleType == ms, "FAIL target sample descriptor");
        check(DEV(SetRenderTarget)(device, target, matching) == D3D_OK, "FAIL matching bind");
        check(DEV(GetRenderTarget)(device, &queried) == D3D_OK, "FAIL target query");
        check(IDirect3DSurface8_GetDesc(queried, &desc) == D3D_OK && desc.MultiSampleType == ms, "FAIL queried sample descriptor");
        IDirect3DSurface8_Release(queried);
        D3DVIEWPORT8 full = { 0, 0, 32, 32, 0, 1 }, custom = { 1, 2, 30, 28, .25f, .75f };
        check(DEV(SetViewport)(device, &custom) == D3D_OK, "FAIL custom viewport");
        check(DEV(SetRenderTarget)(device, target, wrong) == D3DERR_INVALIDCALL, "FAIL explicit sample mismatch accepted");
        check_viewport(&custom);
        check(DEV(SetRenderTarget)(device, NULL, wrong) == D3DERR_INVALIDCALL, "FAIL depth-only mismatch accepted");
        check_viewport(&custom);
        check(DEV(SetRenderTarget)(device, NULL, NULL) == D3D_OK, "FAIL depth disable");
        check(DEV(SetRenderTarget)(device, NULL, matching) == D3D_OK, "FAIL matching depth-only bind");
        check_viewport(&custom);
        check(DEV(SetViewport)(device, &full) == D3D_OK, "FAIL full viewport");
        check(DEV(Clear)(device, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff101010u, 1, 0) == D3D_OK, "FAIL clear");
        DWORD color = palette[n % 6];
        draw(32, color);
        check(DEV(CopyRects)(device, target, NULL, 0, image, NULL) == D3D_OK, "FAIL target resolve/readback");
        verify_color(image, 32, color);
        check(DEV(SetRenderTarget)(device, back, implicit) == D3D_OK, "FAIL restore MSAA back");
        IDirect3DSurface8_Release(target);
        char line[100];
        wsprintfA(line, "TARGET %u %u %08lX", n, (UINT)ms, color);
        report(line);
    }
    check(DEV(GetFrontBuffer)(device, front) == D3D_OK, "FAIL back preservation");
    verify_color(front, 64, 0xff204060u);
    report("CHECK MSAA descriptors, explicit/depth-only mismatch and repeated create/destroy");
    IDirect3DSurface8_Release(front);
    IDirect3DSurface8_Release(image);
    IDirect3DSurface8_Release(depth0);
    IDirect3DSurface8_Release(depth4);
    IDirect3DSurface8_Release(implicit);
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
    lstrcpyA(name, "MSAA.TXT");
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    HWND window = CreateWindowExA(0, "STATIC", "D3D8 MSAA probe", WS_OVERLAPPEDWINDOW, 30, 40, 160, 160, NULL, NULL, GetModuleHandleA(NULL), NULL);
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
    pp.MultiSampleType = D3DMULTISAMPLE_4_SAMPLES;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
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
    wsprintfA(name, "PASS D3D8 MSAA API=%u", D9WG_DRAW_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
