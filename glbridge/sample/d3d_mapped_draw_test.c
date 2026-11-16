/* XP draw hints: real COM calls and signed-base geometry, without native HAL claims. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#if D9WG_MAPPED_API == 8
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
#include "../tests/native/d3d_mapped_draw_reference.h"
static BOOL wide_index;
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
#if D9WG_MAPPED_API == 8
        check(DEV(CreateIndexBuffer)(device, length, usage, (wide_index ? D3DFMT_INDEX32 : D3DFMT_INDEX16), pool, &buffer) == D3D_OK, "FAIL CreateIndexBuffer");
#else
        check(DEV(CreateIndexBuffer)(device, length, usage, (wide_index ? D3DFMT_INDEX32 : D3DFMT_INDEX16), pool, &buffer, NULL) == D3D_OK, "FAIL CreateIndexBuffer");
#endif
        return buffer;
    }
    VertexBuffer* buffer = NULL;
#if D9WG_MAPPED_API == 8
    check(DEV(CreateVertexBuffer)(device, length, usage, fvf, pool, &buffer) == D3D_OK, "FAIL CreateVertexBuffer");
#else
    check(DEV(CreateVertexBuffer)(device, length, usage, fvf, pool, &buffer, NULL) == D3D_OK, "FAIL CreateVertexBuffer");
#endif
    return buffer;
}
static HRESULT lock_buffer(void* buffer, BOOL index, UINT offset, UINT bytes, void** out, DWORD options)
{
#if D9WG_MAPPED_API == 8
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
static void write_indices(void* mapped, const UINT* values, UINT count)
{
    for (UINT i = 0; i < count; ++i)
        if (wide_index)
            ((DWORD*)mapped)[i] = values[i];
        else
            ((WORD*)mapped)[i] = (WORD)values[i];
}
static void indexed_draw(UINT primitives)
{
#if D9WG_MAPPED_API == 8
    check(DEV(DrawIndexedPrimitive)(device, D3DPT_TRIANGLELIST, 0, 6, 0, primitives) == D3D_OK, "FAIL mapped indexed draw8");
#else
    check(DEV(DrawIndexedPrimitive)(device, D3DPT_TRIANGLELIST, 0, 0, 6, 0, primitives) == D3D_OK, "FAIL mapped indexed draw9");
#endif
}
static void mapped_draws(void)
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
#if D9WG_MAPPED_API == 8
    check(DEV(SetVertexShader)(device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE) == D3D_OK, "FAIL FVF8");
#else
    check(DEV(SetFVF)(device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE) == D3D_OK, "FAIL FVF9");
#endif
    for (UINT scene = 0; scene < sizeof(mapped_cases) / sizeof(mapped_cases[0]); ++scene) {
        const struct MappedDrawCase* c = mapped_cases + scene;
        wide_index = c->index32;
        UINT index_size = wide_index ? 4 : 2, first = c->partial ? 3 : 0;
        Vertex vertices[6] = { { 0, 0, .5f, 1, 0 }, { 32, 0, .5f, 1, 0 }, { 0, 64, .5f, 1, 0 }, { 32, 0, .5f, 1, 0 }, { 64, 0, .5f, 1, 0 }, { 64, 64, .5f, 1, 0 } };
        UINT indices[6] = { 0, 0, 0, 0, 0, 0 };
        for (UINT i = 0; i < 6; ++i) {
            vertices[i].color = c->readonly ? (i < 3 ? c->initial_left : c->initial_right) : (c->partial && i < 3 ? c->initial_left : 0xff000000u);
            if (c->readonly || (c->partial && i < 3))
                indices[i] = i;
        }
        VertexBuffer* vb = create_buffer(FALSE, sizeof(vertices), c->usage, D3DPOOL_DEFAULT, 0x44);
        IndexBuffer* ib = create_buffer(TRUE, 6 * index_size, c->usage, D3DPOOL_DEFAULT, 0);
        void* data = NULL;
        check(lock_buffer(vb, FALSE, 0, 0, &data, 0) == D3D_OK, "FAIL baseline VB map");
        CopyMemory(data, vertices, sizeof(vertices));
        check(unlock_buffer(vb, FALSE) == D3D_OK, "FAIL baseline VB unmap");
        check(lock_buffer(ib, TRUE, 0, 0, &data, 0) == D3D_OK, "FAIL baseline IB map");
        write_indices(data, indices, 6);
        check(unlock_buffer(ib, TRUE) == D3D_OK, "FAIL baseline IB unmap");
        void *vertex_map = NULL, *index_map = NULL;
        check(lock_buffer(vb, FALSE, first * sizeof(Vertex), (6 - first) * sizeof(Vertex), &vertex_map, c->flags) == D3D_OK, "FAIL open VB map");
        check(lock_buffer(ib, TRUE, first * index_size, (6 - first) * index_size, &index_map, c->flags) == D3D_OK, "FAIL open IB map");
        if (!c->readonly) {
            for (UINT i = 0; i < 6; ++i) {
                vertices[i].color = i < 3 ? c->initial_left : c->initial_right;
                indices[i] = i;
            }
            CopyMemory(vertex_map, vertices + first, (6 - first) * sizeof(Vertex));
            write_indices(index_map, indices + first, 6 - first);
        }
#if D9WG_MAPPED_API == 8
        check(DEV(SetStreamSource)(device, 0, vb, sizeof(Vertex)) == D3D_OK, "FAIL mapped stream8");
        check(DEV(SetIndices)(device, ib, 0) == D3D_OK, "FAIL mapped indices8");
#else
        check(DEV(SetStreamSource)(device, 0, vb, 0, sizeof(Vertex)) == D3D_OK, "FAIL mapped stream9");
        check(DEV(SetIndices)(device, ib) == D3D_OK, "FAIL mapped indices9");
#endif
        for (UINT phase = 0; phase < 3; ++phase) {
            if (phase == 2) {
                check(unlock_buffer(vb, FALSE) == D3D_OK, "FAIL retained VB Unlock");
                if (!c->mixed)
                    check(unlock_buffer(ib, TRUE) == D3D_OK, "FAIL retained IB Unlock");
            }
            check(DEV(Clear)(device, 0, NULL, D3DCLEAR_TARGET, 0xff101010u, 1, 0) == D3D_OK, "FAIL mapped Clear");
            check(DEV(BeginScene)(device) == D3D_OK, "FAIL mapped BeginScene");
            if (phase == 0) {
#if D9WG_MAPPED_API == 8
                check(DEV(DrawIndexedPrimitive)(device, D3DPT_TRIANGLELIST, 0xffffffffu, 0, 0xffffffffu, 0) == D3D_OK, "FAIL mapped indexed zero8");
#else
                check(DEV(DrawIndexedPrimitive)(device, D3DPT_TRIANGLELIST, (-2147483647 - 1), 0xffffffffu, 0, 0xffffffffu, 0) == D3D_OK, "FAIL mapped indexed zero9");
#endif
                check(DEV(DrawPrimitive)(device, D3DPT_TRIANGLELIST, 0xffffffffu, 0) == D3D_OK, "FAIL mapped nonindexed zero");
            }
            if (c->mixed && phase < 2)
                check(DEV(DrawPrimitive)(device, D3DPT_TRIANGLELIST, 0, 2) == D3D_OK, "FAIL mapped nonindexed draw");
            else
                indexed_draw(2);
            check(DEV(EndScene)(device) == D3D_OK, "FAIL mapped EndScene");
            check(DEV(Present)(device, NULL, NULL, NULL, NULL) == D3D_OK, "FAIL mapped Present");
            char line[120];
            wsprintfA(line, "FRAME %u %u %u %08lX %08lX", D9WG_MAPPED_API, scene, phase, (DWORD)c->left[phase], (DWORD)c->right[phase]);
            report(line);
            if (phase == 0 && c->rewrite) {
                for (UINT i = 0; i < 6; ++i) {
                    vertices[i].color = i < 3 ? c->next_left : c->next_right;
                    indices[i] = c->index_mode == 1 ? 3 + i % 3 : c->index_mode == 2 ? i % 3
                                                                                     : i;
                }
                CopyMemory(vertex_map, vertices + first, (6 - first) * sizeof(Vertex));
                write_indices(index_map, indices + first, 6 - first);
            }
        }
        if (c->mixed)
            check(unlock_buffer(ib, TRUE) == D3D_OK, "FAIL deferred IB Unlock");
        check(DEV(SetStreamSource)(device, 0, NULL,
#if D9WG_MAPPED_API == 9
                  0,
#endif
                  0)
                == D3D_OK,
            "FAIL mapped stream unbind");
#if D9WG_MAPPED_API == 8
        check(DEV(SetIndices)(device, NULL, 0) == D3D_OK, "FAIL mapped indices unbind8");
#else
        check(DEV(SetIndices)(device, NULL) == D3D_OK, "FAIL mapped indices unbind9");
#endif
        release_buffer(vb, FALSE);
        release_buffer(ib, TRUE);
    }
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], path[MAX_PATH], name[40];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    UINT at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    wsprintfA(name, "MAP%u.TXT", D9WG_MAPPED_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    HWND window = CreateWindowExA(0, "STATIC", "Mapped buffer draw probe", WS_OVERLAPPEDWINDOW, 30, 40, 160, 160, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(window != NULL, "FAIL window");
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    wsprintfA(name, "D3D%u.DLL", D9WG_MAPPED_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    HMODULE module = LoadLibraryA(path);
    check(module != NULL, "FAIL DLL");
    wsprintfA(name, "Direct3DCreate%u", D9WG_MAPPED_API);
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
#if D9WG_MAPPED_API == 8
    check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device8");
#else
    check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device9");
#endif
    mapped_draws();
    DEV(Release)(device);
    FACTORY_RELEASE(factory);
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS mapped draws API=%u", D9WG_MAPPED_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
