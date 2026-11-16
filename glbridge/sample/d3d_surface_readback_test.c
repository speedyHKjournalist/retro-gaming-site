/* XP draw hints: real COM calls and signed-base geometry, without native HAL claims. */
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

static void verify_surface(IDirect3DSurface9* surface, DWORD color)
{
    D3DLOCKED_RECT lock;
    check(IDirect3DSurface9_LockRect(surface, &lock, NULL, D3DLOCK_READONLY) == D3D_OK, "FAIL read lock");
    for (UINT y = 0; y < 1024; ++y) {
        DWORD* row = (DWORD*)((BYTE*)lock.pBits + y * lock.Pitch);
        for (UINT x = 0; x < 1024; ++x)
            check(row[x] == color, "FAIL readback pixel or failed-read preservation");
    }
    check(IDirect3DSurface9_UnlockRect(surface) == D3D_OK, "FAIL read unlock");
}
static void initialize_surface(IDirect3DSurface9* surface)
{
    D3DLOCKED_RECT lock;
    check(IDirect3DSurface9_LockRect(surface, &lock, NULL, 0) == D3D_OK, "FAIL write lock");
    for (UINT y = 0; y < 1024; ++y) {
        DWORD* row = (DWORD*)((BYTE*)lock.pBits + y * lock.Pitch);
        for (UINT x = 0; x < 1024; ++x)
            row[x] = 0xff204060u;
    }
    check(IDirect3DSurface9_UnlockRect(surface) == D3D_OK, "FAIL write unlock");
}
static void readbacks(void)
{
    IDirect3DSurface9 *original = NULL, *target = NULL, *cpu = NULL;
    check(DEV(GetRenderTarget)(device, 0, &original) == D3D_OK, "FAIL original target");
    check(DEV(CreateRenderTarget)(device, 1024, 1024, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &target, NULL) == D3D_OK, "FAIL create target");
    check(DEV(CreateOffscreenPlainSurface)(device, 1024, 1024, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &cpu, NULL) == D3D_OK, "FAIL create cpu surface");
    check(DEV(SetRenderTarget)(device, 0, target) == D3D_OK, "FAIL target");
    D3DVIEWPORT9 viewport = { 0, 0, 1024, 1024, 0, 1 };
    check(DEV(SetViewport)(device, &viewport) == D3D_OK, "FAIL viewport");
    DEV(SetRenderState)(device, D3DRS_LIGHTING, FALSE);
    DEV(SetRenderState)(device, D3DRS_ZENABLE, FALSE);
    DEV(SetRenderState)(device, D3DRS_CULLMODE, D3DCULL_NONE);
    DEV(SetRenderState)(device, D3DRS_ALPHABLENDENABLE, FALSE);
    DEV(SetRenderState)(device, D3DRS_FOGENABLE, FALSE);
    DEV(SetTexture)(device, 0, NULL);
    DEV(SetTextureStageState)(device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    DEV(SetTextureStageState)(device, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE);
    DEV(SetTextureStageState)(device, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    DEV(SetTextureStageState)(device, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE);
    check(DEV(SetFVF)(device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE) == D3D_OK, "FAIL FVF");
    struct {
        float x, y, z, rhw;
        DWORD color;
    } vertices[4] = { { 0, 0, .5f, 1, 0xffd02060u }, { 1024, 0, .5f, 1, 0xffd02060u }, { 0, 1024, .5f, 1, 0xffd02060u }, { 1024, 1024, .5f, 1, 0xffd02060u } };
    check(DEV(Clear)(device, 0, NULL, D3DCLEAR_TARGET, 0xff101010u, 1, 0) == D3D_OK, "FAIL clear");
    check(DEV(BeginScene)(device) == D3D_OK, "FAIL begin");
    check(DEV(DrawPrimitiveUP)(device, D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(vertices[0])) == D3D_OK, "FAIL draw");
    check(DEV(EndScene)(device) == D3D_OK, "FAIL end");
    for (UINT phase = 0; phase < 5; ++phase) {
        initialize_surface(cpu);
        HRESULT expected = phase == 0 || phase == 4 ? D3D_OK : D3DERR_DRIVERINTERNALERROR;
        HRESULT result = DEV(GetRenderTargetData)(device, target, cpu);
        check(result == expected, "FAIL GetRenderTargetData result");
        verify_surface(cpu, expected == D3D_OK ? 0xffd02060u : 0xff204060u);
        char line[100];
        wsprintfA(line, "READ %u %08lX", phase, (DWORD)result);
        report(line);
    }
    check(DEV(SetRenderTarget)(device, 0, original) == D3D_OK, "FAIL restore target");
    IDirect3DSurface9_Release(original);
    IDirect3DSurface9_Release(target);
    IDirect3DSurface9_Release(cpu);
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], path[MAX_PATH], name[40];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    UINT at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    lstrcpyA(name, "READ.TXT");
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    HWND window = CreateWindowExA(0, "STATIC", "Shared surface readback probe", WS_OVERLAPPEDWINDOW, 30, 40, 160, 160, NULL, NULL, GetModuleHandleA(NULL), NULL);
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
    readbacks();
    DEV(Release)(device);
    FACTORY_RELEASE(factory);
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS shared readback API=%u", D9WG_DRAW_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
