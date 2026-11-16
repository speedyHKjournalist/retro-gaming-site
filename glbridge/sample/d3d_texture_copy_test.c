/* XP draw hints: real COM calls and signed-base geometry, without native HAL claims. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#if D9WG_COPY_API == 8
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
#include "../tests/native/d3d_texture_copy_reference.h"
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

#if D9WG_COPY_API == 8
typedef IDirect3DBaseTexture8 BaseTexture;
typedef IDirect3DTexture8 Texture;
typedef IDirect3DCubeTexture8 Cube;
typedef IDirect3DVolumeTexture8 Volume;
#define TEX(method) IDirect3DTexture8_##method
#define CUBE(method) IDirect3DCubeTexture8_##method
#define VOL(method) IDirect3DVolumeTexture8_##method
#define BASE(method) IDirect3DBaseTexture8_##method
#else
typedef IDirect3DBaseTexture9 BaseTexture;
typedef IDirect3DTexture9 Texture;
typedef IDirect3DCubeTexture9 Cube;
typedef IDirect3DVolumeTexture9 Volume;
#define TEX(method) IDirect3DTexture9_##method
#define CUBE(method) IDirect3DCubeTexture9_##method
#define VOL(method) IDirect3DVolumeTexture9_##method
#define BASE(method) IDirect3DBaseTexture9_##method
#endif
static UINT mip_size(UINT size, UINT mip)
{
    size >>= mip;
    return size ? size : 1;
}
static BaseTexture* create_texture(UINT kind, UINT w, UINT h, UINT d, UINT levels, DWORD usage, D3DPOOL pool)
{
    void* texture = NULL;
    HRESULT result;
    if (kind == 0)
        result = DEV(CreateTexture)(device, w, h, levels, usage, D3DFMT_A8R8G8B8, pool, (Texture**)&texture
#if D9WG_COPY_API == 9
            ,
            NULL
#endif
        );
    else if (kind == 1)
        result = DEV(CreateCubeTexture)(device, w, levels, usage, D3DFMT_A8R8G8B8, pool, (Cube**)&texture
#if D9WG_COPY_API == 9
            ,
            NULL
#endif
        );
    else
        result = DEV(CreateVolumeTexture)(device, w, h, d, levels, usage, D3DFMT_A8R8G8B8, pool, (Volume**)&texture
#if D9WG_COPY_API == 9
            ,
            NULL
#endif
        );
    check(result == D3D_OK && texture != NULL, "FAIL CreateTexture");
    return texture;
}
static void fill_texture(BaseTexture* texture, UINT kind, UINT w, UINT h, UINT d, UINT levels, BOOL sentinel)
{
    for (UINT face = 0; face < (kind == 1 ? 6u : 1u); ++face) {
        for (UINT mip = 0; mip < levels; ++mip) {
            D3DLOCKED_RECT rect;
            D3DLOCKED_BOX box;
            BYTE* data;
            UINT row_pitch, slice_pitch;
            if (kind == 2) {
                check(VOL(LockBox)((Volume*)texture, mip, &box, NULL, 0) == D3D_OK, "FAIL LockBox");
                data = box.pBits;
                row_pitch = box.RowPitch;
                slice_pitch = box.SlicePitch;
            } else {
                HRESULT hr = kind == 1 ? CUBE(LockRect)((Cube*)texture, (D3DCUBEMAP_FACES)face, mip, &rect, NULL, 0)
                                       : TEX(LockRect)((Texture*)texture, mip, &rect, NULL, 0);
                check(hr == D3D_OK, "FAIL LockRect");
                data = rect.pBits;
                row_pitch = rect.Pitch;
                slice_pitch = row_pitch * mip_size(h, mip);
            }
            for (UINT z = 0; z < (kind == 2 ? mip_size(d, mip) : 1u); ++z) {
                DWORD color = sentinel ? copy_sentinel : copy_palette[(mip + (kind == 1 ? face : kind == 2 ? z
                                                                                                           : 0))
                                                             % 6];
                for (UINT y = 0; y < mip_size(h, mip); ++y) {
                    DWORD* row = (DWORD*)(data + z * slice_pitch + y * row_pitch);
                    for (UINT x = 0; x < mip_size(w, mip); ++x)
                        row[x] = color;
                }
            }
            check((kind == 2          ? VOL(UnlockBox)((Volume*)texture, mip)
                          : kind == 1 ? CUBE(UnlockRect)((Cube*)texture, (D3DCUBEMAP_FACES)face, mip)
                                      : TEX(UnlockRect)((Texture*)texture, mip))
                    == D3D_OK,
                "FAIL unlock texture");
        }
    }
}
static void sampler(UINT state, DWORD value)
{
#if D9WG_COPY_API == 8
    check(DEV(SetTextureStageState)(device, 0, (D3DTEXTURESTAGESTATETYPE)state, value) == D3D_OK, "FAIL sampler8");
#else
    check(DEV(SetSamplerState)(device, 0, (D3DSAMPLERSTATETYPE)state, value) == D3D_OK, "FAIL sampler9");
#endif
}
#if D9WG_COPY_API == 8
#define SAMP(name) D3DTSS_##name
#else
#define SAMP(name) D3DSAMP_##name
#endif
typedef struct CopyVertex {
    float x, y, z, rhw, u, v, w;
} CopyVertex;
static void run_copy_cases(void)
{
    DEV(SetRenderState)(device, D3DRS_LIGHTING, FALSE);
    DEV(SetRenderState)(device, D3DRS_ZENABLE, FALSE);
    DEV(SetRenderState)(device, D3DRS_CULLMODE, D3DCULL_NONE);
    DEV(SetRenderState)(device, D3DRS_ALPHABLENDENABLE, FALSE);
    DEV(SetRenderState)(device, D3DRS_ALPHATESTENABLE, FALSE);
    DEV(SetRenderState)(device, D3DRS_FOGENABLE, FALSE);
    DEV(SetTextureStageState)(device, 0, D3DTSS_COLOROP, D3DTOP_SELECTARG1);
    DEV(SetTextureStageState)(device, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
    DEV(SetTextureStageState)(device, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1);
    DEV(SetTextureStageState)(device, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
    sampler(SAMP(MAGFILTER), D3DTEXF_POINT);
    sampler(SAMP(MINFILTER), D3DTEXF_POINT);
    /* MAXMIPLEVEL selects our test mip only with mip filtering enabled. */
    sampler(SAMP(MIPFILTER), D3DTEXF_POINT);
    sampler(SAMP(ADDRESSU), D3DTADDRESS_CLAMP);
    sampler(SAMP(ADDRESSV), D3DTADDRESS_CLAMP);
    sampler(SAMP(ADDRESSW), D3DTADDRESS_CLAMP);
#if D9WG_COPY_API == 8
    check(DEV(SetVertexShader)(device, D3DFVF_XYZRHW | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE3(0)) == D3D_OK, "FAIL FVF8");
#else
    check(DEV(SetFVF)(device, D3DFVF_XYZRHW | D3DFVF_TEX1 | D3DFVF_TEXCOORDSIZE3(0)) == D3D_OK, "FAIL FVF9");
#endif
    for (UINT scene = 0; scene < sizeof(copy_cases) / sizeof(copy_cases[0]); ++scene) {
        const struct TextureCopyCase* c = copy_cases + scene;
        BaseTexture* source = create_texture(c->kind, c->sw, c->sh, c->sd, c->sl, 0, D3DPOOL_SYSTEMMEM);
        BaseTexture* dest = create_texture(c->kind, c->dw, c->dh, c->dd, c->dl, c->usage, D3DPOOL_DEFAULT);
        BaseTexture* initial = create_texture(c->kind, c->dw, c->dh, c->dd, c->dl, 0, D3DPOOL_SYSTEMMEM);
        fill_texture(source, c->kind, c->sw, c->sh, c->sd, c->sl, FALSE);
        fill_texture(initial, c->kind, c->dw, c->dh, c->dd, c->dl, TRUE);
        check(DEV(UpdateTexture)(device, initial, dest) == D3D_OK, "FAIL sentinel UpdateTexture");
        BASE(Release)(initial);
        check(DEV(UpdateTexture)(device, source, dest) == D3D_OK, "FAIL mip UpdateTexture");
        BASE(Release)(source);
        check(DEV(SetTexture)(device, 0, dest) == D3D_OK, "FAIL bind texture");
        for (UINT i = 0; i < c->count; ++i) {
            const struct TextureCopySample* s = copy_samples + c->first + i;
            float u = .5f, v = .5f, w = 0;
            if (c->kind == 1) {
                static const float direction[6][3] = { { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 } };
                u = direction[s->face][0];
                v = direction[s->face][1];
                w = direction[s->face][2];
            } else if (c->kind == 2)
                w = (s->slice + .5f) / mip_size(c->dd, s->mip);
            CopyVertex vertices[4] = { { 0, 0, .5f, 1, u, v, w }, { 64, 0, .5f, 1, u, v, w }, { 0, 64, .5f, 1, u, v, w }, { 64, 64, .5f, 1, u, v, w } };
            sampler(SAMP(MAXMIPLEVEL), s->mip);
            check(DEV(Clear)(device, 0, NULL, D3DCLEAR_TARGET, 0xff101010u, 1, 0) == D3D_OK, "FAIL clear");
            check(DEV(BeginScene)(device) == D3D_OK, "FAIL begin");
            check(DEV(DrawPrimitiveUP)(device, D3DPT_TRIANGLESTRIP, 2, vertices, sizeof(CopyVertex)) == D3D_OK, "FAIL DrawPrimitiveUP");
            check(DEV(EndScene)(device) == D3D_OK, "FAIL end");
            check(DEV(Present)(device, NULL, NULL, NULL, NULL) == D3D_OK, "FAIL present");
            char line[120];
            wsprintfA(line, "FRAME %u %u %u %u %u %u %08lX", D9WG_COPY_API, scene, i, s->mip, s->face, s->slice, (DWORD)s->color);
            report(line);
        }
        check(DEV(SetTexture)(device, 0, NULL) == D3D_OK, "FAIL unbind texture");
        BASE(Release)(dest);
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
    wsprintfA(name, "COPY%u.TXT", D9WG_COPY_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    HWND window = CreateWindowExA(0, "STATIC", "Texture copy probe", WS_OVERLAPPEDWINDOW, 30, 40, 160, 160, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(window != NULL, "FAIL window");
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    wsprintfA(name, "D3D%u.DLL", D9WG_COPY_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    HMODULE module = LoadLibraryA(path);
    check(module != NULL, "FAIL DLL");
    wsprintfA(name, "Direct3DCreate%u", D9WG_COPY_API);
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
#if D9WG_COPY_API == 8
    check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device8");
#else
    check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device9");
#endif
    run_copy_cases();
    DEV(Release)(device);
    FACTORY_RELEASE(factory);
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS texture copy API=%u", D9WG_COPY_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
