/* Original Microsoft DDraw7 software rasterizer: compare direct XYZ with
 * ProcessVertices(CLIP) + DrawPrimitiveVB, preserving complete image bytes. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define DIRECTDRAW_VERSION 0x0700
#define DIRECT3D_VERSION 0x0700
#include <d3d.h>
#include <ddraw.h>
#include <windows.h>
static HANDLE logfile;
static char directory[MAX_PATH];
#ifdef D9WG_PROXY_CAPTURE
static IDirectDrawSurface7 *primary;
#define PROBE_READY "READY guest clipped primitive commands"
#define PROBE_PASS "PASS guest clipped primitive commands"
#else
#define PROBE_READY "READY native clipped primitive images"
#define PROBE_PASS "PASS native clipped primitive images"
#endif
static void report(const char* s)
{
    DWORD written;
    WriteFile(logfile, s, lstrlenA(s), &written, NULL);
    WriteFile(logfile, "\r\n", 2, &written, NULL);
    FlushFileBuffers(logfile);
}
static void error(const char* where, HRESULT hr)
{
    char text[180];
    wsprintfA(text, "FAIL %s hr=%08lx", where, hr);
    report(text);
}
#define TRY(call)                                                                                            \
    do                                                                                                       \
    {                                                                                                        \
        HRESULT hr = (call);                                                                                 \
        if (FAILED(hr))                                                                                      \
        {                                                                                                    \
            error(#call, hr);                                                                                \
            goto done;                                                                                       \
        }                                                                                                    \
    } while (0)
struct Input
{
    float x, y, z;
    DWORD color;
};
struct Scene
{
    struct Input vertices[3];
    float min_z, max_z;
    BOOL perspective;
};
#ifdef D9WG_USER_CLIP_CAPTURE
#include "d3d_user_clip_scenes.h"
#else
static const struct Scene scenes[] = {
    { { { -.8f, -.7f, -.4f, 0xffff0000 }, { .8f, -.7f, .5f, 0xff00ff00 }, { 0, .8f, .5f, 0xff0000ff } }, .25f,
        .75f, FALSE },
    { { { -.8f, -.7f, 1.4f, 0xffff0000 }, { .8f, -.7f, .5f, 0xff00ff00 }, { 0, .8f, .5f, 0xff0000ff } }, .25f,
        .75f, FALSE },
    { { { -.8f, -.7f, -.4f, 0xffff0000 }, { .8f, -.7f, .5f, 0xff00ff00 }, { 0, .8f, .5f, 0xff0000ff } }, 0, 0,
        FALSE },
    { { { -.2f, -.2f, -.5f, 0xffff0000 }, { .8f, -.7f, 1, 0xff00ff00 }, { 0, .8f, 1, 0xff0000ff } }, .25f,
        .75f, TRUE },
    { { { -.2f, -.2f, 0, 0xffff0000 }, { .8f, -.7f, 1, 0xff00ff00 }, { 0, .8f, 1, 0xff0000ff } }, .25f, .75f,
        TRUE },
    { { { -200, -.7f, .5f, 0xffff0000 }, { .8f, -.7f, .5f, 0xff00ff00 }, { 0, .8f, .5f, 0xff0000ff } }, .25f,
        .75f, FALSE },
};
#endif
static HRESULT save_surface(IDirectDrawSurface7* surface, unsigned index, unsigned processed)
{
#ifdef D9WG_PROXY_CAPTURE
    (void)index;(void)processed;
    /* A primary blit presents and submits the batch. The offline renderer
     * reads the original 32-bit target after each of these checkpoints. */
    return IDirectDrawSurface7_Blt(primary, NULL, surface, NULL, DDBLT_WAIT, NULL);
#else
    DDSURFACEDESC2 lock;
    ZeroMemory(&lock, sizeof(lock));
    lock.dwSize = sizeof(lock);
    HRESULT hr = IDirectDrawSurface7_Lock(surface, NULL, &lock, DDLOCK_WAIT | DDLOCK_READONLY, NULL);
    if (FAILED(hr))
        return hr;
    char path[MAX_PATH], name[20];
    lstrcpyA(path, directory);
    wsprintfA(name, "S%uP%u.BIN", index, processed);
    lstrcatA(path, name);
    HANDLE file
        = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, OPEN_EXISTING,
            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, NULL);
    if (file == INVALID_HANDLE_VALUE)
    {
        IDirectDrawSurface7_Unlock(surface, NULL);
        return HRESULT_FROM_WIN32(GetLastError());
    }
    for (unsigned y = 0; y < 128; y++)
    {
        DWORD written;
        if (!WriteFile(file, (BYTE*)lock.lpSurface + y * lock.lPitch, 512, &written, NULL) || written != 512)
        {
            CloseHandle(file);
            IDirectDrawSurface7_Unlock(surface, NULL);
            return E_FAIL;
        }
    }
    if (!FlushFileBuffers(file))
    {
        CloseHandle(file);
        IDirectDrawSurface7_Unlock(surface, NULL);
        return E_FAIL;
    }
    CloseHandle(file);
    return IDirectDrawSurface7_Unlock(surface, NULL);
#endif
}
void WINAPI WinMainCRTStartup(void)
{
    char path[MAX_PATH], text[100];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    unsigned at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    lstrcpyA(path, directory);
    lstrcatA(path, "CLIP.TXT");
    logfile
        = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (logfile == INVALID_HANDLE_VALUE)
        ExitProcess(2);
    report(PROBE_READY);
#ifdef D9WG_PROXY_CAPTURE
    lstrcpyA(path, directory);
#else
    GetSystemDirectoryA(path, sizeof(path));
    lstrcatA(path, "\\");
#endif
    lstrcatA(path, "ddraw.dll");
    HMODULE module = LoadLibraryA(path);
    if (!module)
    {
        error("system ddraw", HRESULT_FROM_WIN32(GetLastError()));
        goto done;
    }
    HRESULT(WINAPI * create)(GUID*, void**, REFIID, IUnknown*)
        = (void*)GetProcAddress(module, "DirectDrawCreateEx");
    if (!create)
    {
        error("system export", E_FAIL);
        goto done;
    }
    HWND window = CreateWindowExA(0, "STATIC", "Clip raster reference", WS_OVERLAPPEDWINDOW, 0, 0, 160, 160,
        NULL, NULL, GetModuleHandleA(NULL), NULL);
    IDirectDraw7* dd;
    IDirect3D7* d3d;
    IDirectDrawSurface7 *target, *depth;
    IDirect3DDevice7* device;
    IDirect3DVertexBuffer7 *source, *destination;
    TRY(create(NULL, (void**)&dd, &IID_IDirectDraw7, NULL));
    TRY(IDirectDraw7_SetCooperativeLevel(dd, window, DDSCL_NORMAL));
    TRY(IDirectDraw7_QueryInterface(dd, &IID_IDirect3D7, (void**)&d3d));
    DDSURFACEDESC2 desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    desc.dwWidth = desc.dwHeight = 128;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_3DDEVICE | DDSCAPS_SYSTEMMEMORY;
    desc.ddpfPixelFormat.dwSize = sizeof(desc.ddpfPixelFormat);
    desc.ddpfPixelFormat.dwFlags = DDPF_RGB;
    desc.ddpfPixelFormat.dwRGBBitCount = 32;
    desc.ddpfPixelFormat.dwRBitMask = 0xff0000;
    desc.ddpfPixelFormat.dwGBitMask = 0xff00;
    desc.ddpfPixelFormat.dwBBitMask = 0xff;
    TRY(IDirectDraw7_CreateSurface(dd, &desc, &target, NULL));
#ifdef D9WG_PROXY_CAPTURE
    DDSURFACEDESC2 pd;ZeroMemory(&pd,sizeof(pd));pd.dwSize=sizeof(pd);pd.dwFlags=DDSD_CAPS;
    pd.ddsCaps.dwCaps=DDSCAPS_PRIMARYSURFACE;
    TRY(IDirectDraw7_CreateSurface(dd,&pd,&primary,NULL));
#endif
    desc.ddsCaps.dwCaps = DDSCAPS_ZBUFFER | DDSCAPS_SYSTEMMEMORY;
    ZeroMemory(&desc.ddpfPixelFormat, sizeof(desc.ddpfPixelFormat));
    desc.ddpfPixelFormat.dwSize = sizeof(desc.ddpfPixelFormat);
    desc.ddpfPixelFormat.dwFlags = DDPF_ZBUFFER;
    desc.ddpfPixelFormat.dwZBufferBitDepth = 16;
    desc.ddpfPixelFormat.dwZBitMask = 0xffff;
    TRY(IDirectDraw7_CreateSurface(dd, &desc, &depth, NULL));
    TRY(IDirectDrawSurface7_AddAttachedSurface(target, depth));
    TRY(IDirect3D7_CreateDevice(d3d, &IID_IDirect3DRGBDevice, target, &device));
    D3DMATRIX identity;
    ZeroMemory(&identity, sizeof(identity));
    identity._11 = identity._22 = identity._33 = identity._44 = 1;
    TRY(IDirect3DDevice7_SetTransform(device, D3DTRANSFORMSTATE_WORLD, &identity));
    TRY(IDirect3DDevice7_SetTransform(device, D3DTRANSFORMSTATE_VIEW, &identity));
#ifdef D9WG_USER_CLIP_CAPTURE
    D3DMATRIX world = identity, view = identity;
    world._41 = .125f;world._22 = .75f;view._41 = -.25f;view._42 = .125f;
    TRY(IDirect3DDevice7_SetTransform(device, D3DTRANSFORMSTATE_WORLD, &world));
    TRY(IDirect3DDevice7_SetTransform(device, D3DTRANSFORMSTATE_VIEW, &view));
#endif
    TRY(IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_LIGHTING, FALSE));
    TRY(IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_CLIPPING, TRUE));
    TRY(IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_CULLMODE, D3DCULL_NONE));
    TRY(IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_ZENABLE, TRUE));
    D3DVERTEXBUFFERDESC vb = { sizeof(vb), D3DVBCAPS_SYSTEMMEMORY, D3DFVF_XYZ | D3DFVF_DIFFUSE, 3 };
    TRY(IDirect3D7_CreateVertexBuffer(d3d, &vb, &source, 0));
    vb.dwFVF = D3DFVF_XYZRHW | D3DFVF_DIFFUSE;
    TRY(IDirect3D7_CreateVertexBuffer(d3d, &vb, &destination, 0));
    D3DVIEWPORT7 full = { 0, 0, 128, 128, 0, 1 };
    TRY(IDirect3DDevice7_SetViewport(device, &full));
    TRY(IDirect3DDevice7_Clear(device, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff102030, 1, 0));
    for (unsigned scene = 0; scene < sizeof(scenes) / sizeof(scenes[0]); scene++)
    {
        const struct Scene* s = &scenes[scene];
#ifdef D9WG_USER_CLIP_CAPTURE
        TRY(IDirect3DDevice7_SetClipPlane(device, scene, (float *)user_planes[scene]));
        TRY(IDirect3DDevice7_SetRenderState(device, D3DRENDERSTATE_CLIPPLANEENABLE, 1u << scene));
#endif
        void* data;
        TRY(IDirect3DVertexBuffer7_Lock(source, 0, &data, NULL));
        CopyMemory(data, s->vertices, sizeof(s->vertices));
        TRY(IDirect3DVertexBuffer7_Unlock(source));
        D3DVIEWPORT7 vp = { 7, 11, 100, 80, s->min_z, s->max_z };
        TRY(IDirect3DDevice7_SetViewport(device, &vp));
        D3DMATRIX projection = identity;
        if (s->perspective)
        {
            projection._33 = .5f;
            projection._43 = .1f;
            projection._34 = 1;
            projection._44 = 0;
        }
        TRY(IDirect3DDevice7_SetTransform(device, D3DTRANSFORMSTATE_PROJECTION, &projection));
        TRY(IDirect3DVertexBuffer7_ProcessVertices(
            destination, D3DVOP_TRANSFORM | D3DVOP_CLIP, 0, 3, source, 0, device, 0));
        TRY(IDirect3DVertexBuffer7_Lock(destination, DDLOCK_READONLY, &data, NULL));
        for (unsigned vertex = 0; vertex < 3; vertex++)
        {
            DWORD* p = (DWORD*)data + 5 * vertex;
            wsprintfA(text, "VERTEX scene=%u v=%u words=%08lx,%08lx,%08lx,%08lx", scene, vertex, p[0], p[1],
                p[2], p[3]);
            report(text);
        }
        TRY(IDirect3DVertexBuffer7_Unlock(destination));
        for (unsigned processed = 0; processed <
#ifdef D9WG_USER_CLIP_CAPTURE
            4
#else
            3
#endif
            ; processed++)
        {
            TRY(IDirect3DDevice7_Clear(
                device, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff102030, 1, 0));
            TRY(IDirect3DDevice7_BeginScene(device));
            if (processed == 2)
            {
                WORD indices[3] = {0, 1, 2};
                TRY(IDirect3DDevice7_DrawIndexedPrimitiveVB(device, D3DPT_TRIANGLELIST,
                    destination, 0, 3, indices, 3, 0));
            }
            else if (processed && processed != 3)
            {
                TRY(IDirect3DDevice7_DrawPrimitiveVB(device, D3DPT_TRIANGLELIST, destination, 0, 3, 0));
            }
            else
            {
                TRY(IDirect3DDevice7_DrawPrimitive(
                    device, D3DPT_TRIANGLELIST, D3DFVF_XYZ | D3DFVF_DIFFUSE, (void*)s->vertices, 3, 0));
            }
            TRY(IDirect3DDevice7_EndScene(device));
            TRY(save_surface(target, scene, processed));
            wsprintfA(text, "IMAGE scene=%u processed=%u bytes=65536", scene, processed);
            report(text);
        }
    }
    IDirect3DVertexBuffer7_Release(source);
    IDirect3DVertexBuffer7_Release(destination);
    IDirect3DDevice7_Release(device);
    IDirectDrawSurface7_Release(depth);
    IDirectDrawSurface7_Release(target);
    IDirect3D7_Release(d3d);
    IDirectDraw7_Release(dd);
    DestroyWindow(window);
#ifdef D9WG_PROXY_CAPTURE
    IDirectDrawSurface7_Release(primary);
#endif
    report(PROBE_PASS);
done:
    CloseHandle(logfile);
    ExitProcess(0);
}
