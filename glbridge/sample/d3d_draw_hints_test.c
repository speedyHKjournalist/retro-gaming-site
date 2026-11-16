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
#include "../tests/native/d3d_draw_hints_reference.h"
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
static void* create_buffer(BOOL index, UINT length, DWORD usage, D3DPOOL pool, DWORD fvf)
{
    if (index) {
        IndexBuffer* buffer = NULL;
#if D9WG_DRAW_API == 8
        check(DEV(CreateIndexBuffer)(device, length, usage, D3DFMT_INDEX16, pool, &buffer) == D3D_OK, "FAIL CreateIndexBuffer");
#else
        check(DEV(CreateIndexBuffer)(device, length, usage, D3DFMT_INDEX16, pool, &buffer, NULL) == D3D_OK, "FAIL CreateIndexBuffer");
#endif
        return buffer;
    }
    VertexBuffer* buffer = NULL;
#if D9WG_DRAW_API == 8
    check(DEV(CreateVertexBuffer)(device, length, usage, fvf, pool, &buffer) == D3D_OK, "FAIL CreateVertexBuffer");
#else
    check(DEV(CreateVertexBuffer)(device, length, usage, fvf, pool, &buffer, NULL) == D3D_OK, "FAIL CreateVertexBuffer");
#endif
    return buffer;
}
static HRESULT lock_buffer(void* buffer, BOOL index, UINT offset, UINT bytes, void** out, DWORD options)
{
#if D9WG_DRAW_API == 8
    BYTE* data = NULL;
#else
    void* data = NULL;
#endif
    HRESULT result = index ? IB_LOCK((IndexBuffer*)buffer, offset, bytes, &data, options)
                           : VB_LOCK((VertexBuffer*)buffer, offset, bytes, &data, options);
    *out = data;
    return result;
}
static HRESULT unlock_buffer(void* buffer, BOOL index)
{
    return index ? IB_UNLOCK((IndexBuffer*)buffer) : VB_UNLOCK((VertexBuffer*)buffer);
}
static void release_buffer(void* buffer, BOOL index)
{
    if (index)
        IB_RELEASE((IndexBuffer*)buffer);
    else
        VB_RELEASE((VertexBuffer*)buffer);
}
typedef struct Vertex {
    float x, y, z, rhw;
    DWORD color;
} Vertex;
static void draws(void)
{
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
#if D9WG_DRAW_API == 8
    check(DEV(SetVertexShader)(device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE) == D3D_OK, "FAIL FVF8");
#else
    check(DEV(SetFVF)(device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE) == D3D_OK, "FAIL FVF9");
#endif
    Vertex vertices[6] = { { 0, 0, .5f, 1, 0xffff0000u }, { 32, 0, .5f, 1, 0xffff0000u }, { 0, 64, .5f, 1, 0xffff0000u }, { 32, 0, .5f, 1, 0xff00ff00u }, { 64, 0, .5f, 1, 0xff00ff00u }, { 64, 64, .5f, 1, 0xff00ff00u } };
    WORD indices[6] = { 0, 1, 2, 3, 4, 5 };
    VertexBuffer* vb = create_buffer(FALSE, sizeof(vertices), 0, D3DPOOL_DEFAULT, 0x44);
    IndexBuffer* ib = create_buffer(TRUE, sizeof(indices), 0, D3DPOOL_DEFAULT, 0);
    void* data = NULL;
    check(lock_buffer(vb, FALSE, 0, 0, &data, 0) == D3D_OK, "FAIL VB lock");
    CopyMemory(data, vertices, sizeof(vertices));
    check(unlock_buffer(vb, FALSE) == D3D_OK, "FAIL VB unlock");
    check(lock_buffer(ib, TRUE, 0, 0, &data, 0) == D3D_OK, "FAIL IB lock");
    CopyMemory(data, indices, sizeof(indices));
    check(unlock_buffer(ib, TRUE) == D3D_OK, "FAIL IB unlock");
#if D9WG_DRAW_API == 8
    check(DEV(SetStreamSource)(device, 0, vb, sizeof(Vertex)) == D3D_OK, "FAIL stream8");
#else
    check(DEV(SetStreamSource)(device, 0, vb, 0, sizeof(Vertex)) == D3D_OK, "FAIL stream9");
    check(DEV(SetIndices)(device, ib) == D3D_OK, "FAIL indices9");
#endif
    for (UINT scene = 0; scene < sizeof(draw_cases) / sizeof(draw_cases[0]); ++scene) {
        const struct DrawHintCase* c = draw_cases + scene;
#if D9WG_DRAW_API == 8
        check(DEV(SetIndices)(device, ib, (UINT)c->base) == D3D_OK, "FAIL indices8");
        IndexBuffer* queried = NULL;
        UINT queried_base = 0;
        check(DEV(GetIndices)(device, &queried, &queried_base) == D3D_OK && queried == ib && queried_base == (UINT)c->base, "FAIL indices8 query bits");
        IB_RELEASE(queried);
#endif
        check(DEV(Clear)(device, 0, NULL, D3DCLEAR_TARGET, 0xff101010u, 1, 0) == D3D_OK, "FAIL clear");
        check(DEV(BeginScene)(device) == D3D_OK, "FAIL begin");
        if (c->mode == 2)
            check(DEV(DrawPrimitive)(device, D3DPT_TRIANGLELIST, c->start, c->primitives) == D3D_OK, "FAIL nonindexed draw");
        else {
            UINT type = c->mode == 1 ? 1 : 4, last = c->mode == 1 ? 6 : 4;
            for (; type <= last; ++type) {
#if D9WG_DRAW_API == 8
                check(DEV(DrawIndexedPrimitive)(device, (D3DPRIMITIVETYPE)type, c->first, c->count, c->start, c->primitives) == D3D_OK, "FAIL indexed draw8");
#else
                check(DEV(DrawIndexedPrimitive)(device, (D3DPRIMITIVETYPE)type, c->base, c->first, c->count, c->start, c->primitives) == D3D_OK, "FAIL indexed draw9");
#endif
                if (c->mode == 1)
                    check(DEV(DrawPrimitive)(device, (D3DPRIMITIVETYPE)type, 0xffffffffu, 0) == D3D_OK, "FAIL zero nonindexed draw");
            }
        }
        check(DEV(EndScene)(device) == D3D_OK, "FAIL end");
        check(DEV(Present)(device, NULL, NULL, NULL, NULL) == D3D_OK, "FAIL present");
        char line[100];
        wsprintfA(line, "FRAME %u %u %08lX %08lX", D9WG_DRAW_API, scene, (DWORD)c->left, (DWORD)c->right);
        report(line);
    }
    check(DEV(SetStreamSource)(device, 0, NULL,
#if D9WG_DRAW_API == 9
              0,
#endif
              0)
            == D3D_OK,
        "FAIL unbind stream");
#if D9WG_DRAW_API == 8
    check(DEV(SetIndices)(device, NULL, 0) == D3D_OK, "FAIL unbind8");
#else
    check(DEV(SetIndices)(device, NULL) == D3D_OK, "FAIL unbind9");
#endif
    release_buffer(vb, FALSE);
    release_buffer(ib, TRUE);
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], path[MAX_PATH], name[40];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    UINT at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    wsprintfA(name, "DRAW%u.TXT", D9WG_DRAW_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    HWND window = CreateWindowExA(0, "STATIC", "Retail draw hint probe", WS_OVERLAPPEDWINDOW, 30, 40, 160, 160, NULL, NULL, GetModuleHandleA(NULL), NULL);
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
    draws();
    DEV(Release)(device);
    FACTORY_RELEASE(factory);
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS draw hints API=%u", D9WG_DRAW_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
