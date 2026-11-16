/* XP retail buffer hints: COM results/data plus four deterministic draws.
 * Expected access policy is frozen separately from the production helper. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#if D9WG_LOCK_API == 8
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
#include "../tests/native/d3d_buffer_lock_reference.h"
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
#if D9WG_LOCK_API == 8
        check(DEV(CreateIndexBuffer)(device, length, usage, D3DFMT_INDEX16, pool, &buffer) == D3D_OK, "FAIL CreateIndexBuffer");
#else
        check(DEV(CreateIndexBuffer)(device, length, usage, D3DFMT_INDEX16, pool, &buffer, NULL) == D3D_OK, "FAIL CreateIndexBuffer");
#endif
        return buffer;
    }
    VertexBuffer* buffer = NULL;
#if D9WG_LOCK_API == 8
    check(DEV(CreateVertexBuffer)(device, length, usage, fvf, pool, &buffer) == D3D_OK, "FAIL CreateVertexBuffer");
#else
    check(DEV(CreateVertexBuffer)(device, length, usage, fvf, pool, &buffer, NULL) == D3D_OK, "FAIL CreateVertexBuffer");
#endif
    return buffer;
}
static HRESULT lock_buffer(void* buffer, BOOL index, UINT offset, UINT bytes, void** out, DWORD options)
{
#if D9WG_LOCK_API == 8
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
static void matrix(void)
{
    UINT count = 0;
    for (UINT c = 0; c < sizeof(contexts) / sizeof(contexts[0]); ++c) {
        for (UINT index = 0; index < 2; ++index) {
            void* buffer = create_buffer(index, 64, contexts[c].usage, (D3DPOOL)contexts[c].pool, 0);
            UINT ordinal = 0;
            for (UINT f = 0; f < sizeof(flags) / sizeof(flags[0]); ++f) {
                for (UINT a = 0; a < sizeof(advisory) / sizeof(advisory[0]); ++a) {
                    DWORD words[16];
                    ++ordinal;
                    DWORD cookie = ((DWORD)D9WG_LOCK_API << 24) | ((DWORD)index << 20) | ((DWORD)c << 12) | 0x4b1u;
                    for (UINT i = 0; i < 16; ++i)
                        words[i] = 0x11000000u + ordinal * 0x100u + i;
                    words[0] = cookie;
                    words[1] = ordinal;
                    void* data = NULL;
                    check(lock_buffer(buffer, index, 0, 64, &data, 0) == D3D_OK && data, "FAIL baseline Lock");
                    CopyMemory(data, words, sizeof(words));
                    check(unlock_buffer(buffer, index) == D3D_OK, "FAIL baseline Unlock");
                    DWORD requested = flags[f] | advisory[a];
                    DWORD effective = contexts[c].effective[f];
                    HRESULT result = lock_buffer(buffer, index, 8, 16, &data, requested);
                    if (result != D3D_OK || !data) {
                        char line[128];
                        wsprintfA(line, "FAIL Lock api=%u ib=%u context=%u flags=%08lX hr=%08lX", D9WG_LOCK_API, index, c, requested, (DWORD)result);
                        report(line);
                        ExitProcess(1);
                    }
                    DWORD* mapped = data;
                    if (!(effective & D3DLOCK_DISCARD))
                        for (UINT i = 0; i < 4; ++i)
                            check(mapped[i] == words[i + 2], "FAIL ignored hint destroyed buffer bytes");
                    if (!(effective & D3DLOCK_READONLY))
                        for (UINT i = 0; i < 4; ++i)
                            mapped[i] = 0xa0000000u | ((DWORD)D9WG_LOCK_API << 24) | ((DWORD)index << 23) | ((DWORD)c << 16) | ((DWORD)ordinal << 8) | i;
                    check(unlock_buffer(buffer, index) == D3D_OK, "FAIL retail Unlock");
                    ++count;
                }
            }
            release_buffer(buffer, index);
        }
    }
    for (UINT index = 0; index < 2; ++index) {
        void* buffer = create_buffer(index, 64, D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY, D3DPOOL_DEFAULT, 0);
        for (UINT i = 0; i < sizeof(nativeFlags) / sizeof(nativeFlags[0]); ++i) {
            void* data = NULL;
            check(lock_buffer(buffer, index, 0, 0, &data, nativeFlags[i]) == D3D_OK && data, "FAIL XP reference HRESULT");
            check(unlock_buffer(buffer, index) == D3D_OK, "FAIL XP reference Unlock");
        }
        release_buffer(buffer, index);
    }
    char line[100];
    wsprintfA(line, "MATRIX %u %u contexts=%u XP_HRESULTS=14", D9WG_LOCK_API, count, (UINT)(sizeof(contexts) / sizeof(contexts[0])));
    report(line);
}
typedef struct Vertex {
    float x, y, z, rhw;
    DWORD color;
} Vertex;
static void draws(void)
{
    static const DWORD left[] = { 0xffff0000u, 0xff0000ffu, 0xffff00ffu, 0xffffffffu };
    static const DWORD right[] = { 0xff00ff00u, 0xffffff00u, 0xff00ffffu, 0xffff8000u };
    static const DWORD options[] = { D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE, D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE, D3DLOCK_READONLY | D3DLOCK_DISCARD, D3DLOCK_READONLY };
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
#if D9WG_LOCK_API == 8
    check(DEV(SetVertexShader)(device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE) == D3D_OK, "FAIL FVF8");
#else
    check(DEV(SetFVF)(device, D3DFVF_XYZRHW | D3DFVF_DIFFUSE) == D3D_OK, "FAIL FVF9");
#endif
    for (UINT scene = 0; scene < 4; ++scene) {
        DWORD usage = scene == 0 ? 0 : scene == 3 ? D3DUSAGE_WRITEONLY
                                                  : D3DUSAGE_DYNAMIC | D3DUSAGE_WRITEONLY;
        Vertex vertices[6] = { { 0, 0, .5f, 1, 0 }, { 32, 0, .5f, 1, 0 }, { 0, 64, .5f, 1, 0 }, { 32, 0, .5f, 1, 0 }, { 64, 0, .5f, 1, 0 }, { 64, 64, .5f, 1, 0 } };
        WORD indices[6] = { 0, 1, 2, 0, 0, 0 };
        for (UINT i = 0; i < 6; ++i)
            vertices[i].color = i < 3 ? left[scene] : 0xff000000u;
        if (scene == 2)
            indices[0] = indices[1] = indices[2] = 0;
        VertexBuffer* vb = create_buffer(FALSE, sizeof(vertices), usage, D3DPOOL_DEFAULT, D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
        IndexBuffer* ib = create_buffer(TRUE, sizeof(indices), usage, D3DPOOL_DEFAULT, 0);
        void* data = NULL;
        check(lock_buffer(vb, FALSE, 0, 0, &data, 0) == D3D_OK, "FAIL draw baseline VB Lock");
        CopyMemory(data, vertices, sizeof(vertices));
        check(unlock_buffer(vb, FALSE) == D3D_OK, "FAIL baseline VB Unlock");
        check(lock_buffer(ib, TRUE, 0, 0, &data, 0) == D3D_OK, "FAIL draw baseline IB Lock");
        CopyMemory(data, indices, sizeof(indices));
        check(unlock_buffer(ib, TRUE) == D3D_OK, "FAIL baseline IB Unlock");
        for (UINT i = 0; i < 6; ++i)
            vertices[i].color = i < 3 ? left[scene] : right[scene];
        indices[0] = 0;
        indices[1] = 1;
        indices[2] = 2;
        indices[3] = 3;
        indices[4] = 4;
        indices[5] = 5;
        UINT first = scene == 2 ? 0 : 3;
        check(lock_buffer(vb, FALSE, first * sizeof(Vertex), (6 - first) * sizeof(Vertex), &data, options[scene]) == D3D_OK, "FAIL draw hinted VB Lock");
        CopyMemory(data, vertices + first, (6 - first) * sizeof(Vertex));
        check(unlock_buffer(vb, FALSE) == D3D_OK, "FAIL draw hinted VB Unlock");
        check(lock_buffer(ib, TRUE, first * sizeof(WORD), (6 - first) * sizeof(WORD), &data, options[scene]) == D3D_OK, "FAIL draw hinted IB Lock");
        CopyMemory(data, indices + first, (6 - first) * sizeof(WORD));
        check(unlock_buffer(ib, TRUE) == D3D_OK, "FAIL draw hinted IB Unlock");
        if (scene < 2) {
            /* Re-upload the mapped shadow to make incorrect full-shadow clears
             * observable in the untouched first triangle too. */
            check(lock_buffer(vb, FALSE, 0, 0, &data, 0) == D3D_OK, "FAIL refresh VB Lock");
            check(unlock_buffer(vb, FALSE) == D3D_OK, "FAIL refresh VB Unlock");
            check(lock_buffer(ib, TRUE, 0, 0, &data, 0) == D3D_OK, "FAIL refresh IB Lock");
            check(unlock_buffer(ib, TRUE) == D3D_OK, "FAIL refresh IB Unlock");
        }
#if D9WG_LOCK_API == 8
        check(DEV(SetStreamSource)(device, 0, vb, sizeof(Vertex)) == D3D_OK, "FAIL stream8");
        check(DEV(SetIndices)(device, ib, 0) == D3D_OK, "FAIL indices8");
#else
        check(DEV(SetStreamSource)(device, 0, vb, 0, sizeof(Vertex)) == D3D_OK, "FAIL stream9");
        check(DEV(SetIndices)(device, ib) == D3D_OK, "FAIL indices9");
#endif
        check(DEV(Clear)(device, 0, NULL, D3DCLEAR_TARGET, 0xff101010u, 1, 0) == D3D_OK, "FAIL Clear");
        check(DEV(BeginScene)(device) == D3D_OK, "FAIL BeginScene");
#if D9WG_LOCK_API == 8
        check(DEV(DrawIndexedPrimitive)(device, D3DPT_TRIANGLELIST, 0, 6, 0, 2) == D3D_OK, "FAIL DrawIndexed8");
#else
        check(DEV(DrawIndexedPrimitive)(device, D3DPT_TRIANGLELIST, 0, 0, 6, 0, 2) == D3D_OK, "FAIL DrawIndexed9");
#endif
        check(DEV(EndScene)(device) == D3D_OK, "FAIL EndScene");
        check(DEV(Present)(device, NULL, NULL, NULL, NULL) == D3D_OK, "FAIL Present");
        char line[100];
        wsprintfA(line, "FRAME %u %u %08lX %08lX", D9WG_LOCK_API, scene, left[scene], right[scene]);
        report(line);
        DEV(SetStreamSource)(device, 0, NULL,
#if D9WG_LOCK_API == 9
            0,
#endif
            0);
#if D9WG_LOCK_API == 8
        DEV(SetIndices)(device, NULL, 0);
#else
        DEV(SetIndices)(device, NULL);
#endif
        VB_RELEASE(vb);
        IB_RELEASE(ib);
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
    wsprintfA(name, "LOCK%u.TXT", D9WG_LOCK_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    HWND window = CreateWindowExA(0, "STATIC", "Retail buffer Lock probe", WS_OVERLAPPEDWINDOW, 30, 40, 160, 160, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(window != NULL, "FAIL window");
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    wsprintfA(name, "D3D%u.DLL", D9WG_LOCK_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    HMODULE module = LoadLibraryA(path);
    check(module != NULL, "FAIL DLL");
    wsprintfA(name, "Direct3DCreate%u", D9WG_LOCK_API);
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
#if D9WG_LOCK_API == 8
    check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device8");
#else
    check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device9");
#endif
    matrix();
    draws();
    DEV(Release)(device);
    FACTORY_RELEASE(factory);
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS buffer Lock API=%u", D9WG_LOCK_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
