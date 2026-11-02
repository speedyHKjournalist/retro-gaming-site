/* Microsoft system DDraw7 RGB reference, never loads the app-local proxy.
 * Records public ProcessVertices words and independently draws XYZRHW depth.
 * The XP runner writes only to its RAM-backed tools volume. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define DIRECTDRAW_VERSION 0x0700
#define DIRECT3D_VERSION 0x0700
#include <d3d.h>
#include <ddraw.h>
#include <windows.h>
static HANDLE logfile;
static void line(const char* s)
{
    DWORD n;
    WriteFile(logfile, s, lstrlenA(s), &n, 0);
    WriteFile(logfile, "\r\n", 2, &n, 0);
    FlushFileBuffers(logfile);
}
static void status(const char* s, HRESULT hr)
{
    char t[160];
    wsprintfA(t, "%s hr=%08lx", s, hr);
    line(t);
}
#define TRY(x)                                                                                               \
    do                                                                                                       \
    {                                                                                                        \
        HRESULT hr = (x);                                                                                    \
        status(#x, hr);                                                                                      \
        if (FAILED(hr))                                                                                      \
            goto done;                                                                                       \
    } while (0)
void WINAPI WinMainCRTStartup(void)
{
    char p[MAX_PATH], t[200];
    GetModuleFileNameA(0, p, sizeof(p));
    int at = lstrlenA(p);
    while (at && p[at - 1] != '\\')
        --at;
    p[at] = 0;
    lstrcatA(p, "CLIP.TXT");
    logfile = CreateFileA(p, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
    if (logfile == INVALID_HANDLE_VALUE)
        ExitProcess(2);
    line("READY native Microsoft DDraw7 clipping reference");
    GetSystemDirectoryA(p, sizeof(p));
    lstrcatA(p, "\\ddraw.dll");
    HMODULE lib = LoadLibraryA(p);
    if (!lib)
    {
        status("load native ddraw", HRESULT_FROM_WIN32(GetLastError()));
        goto done;
    }
    HRESULT(WINAPI * create)(GUID*, void**, REFIID, IUnknown*)
        = (void*)GetProcAddress(lib, "DirectDrawCreateEx");
    if (!create)
    {
        status("native DirectDrawCreateEx export", HRESULT_FROM_WIN32(GetLastError()));
        goto done;
    }
    IDirectDraw7* dd = 0;
    IDirect3D7* d3d = 0;
    IDirectDrawSurface7 *target = 0, *depth = 0;
    IDirect3DDevice7* dev = 0;
    IDirect3DVertexBuffer7 *src = 0, *dst = 0;
    HWND window = CreateWindowExA(
        0, "STATIC", "Clip reference", WS_OVERLAPPEDWINDOW, 0, 0, 128, 128, 0, 0, GetModuleHandleA(0), 0);
    TRY(create(0, (void**)&dd, &IID_IDirectDraw7, 0));
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
    TRY(IDirectDraw7_CreateSurface(dd, &desc, &target, 0));
    DDSURFACEDESC2 zd;
    ZeroMemory(&zd, sizeof(zd));
    zd.dwSize = sizeof(zd);
    zd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    zd.dwWidth = zd.dwHeight = 128;
    zd.ddsCaps.dwCaps = DDSCAPS_ZBUFFER | DDSCAPS_SYSTEMMEMORY;
    zd.ddpfPixelFormat.dwSize = sizeof(zd.ddpfPixelFormat);
    zd.ddpfPixelFormat.dwFlags = DDPF_ZBUFFER;
    zd.ddpfPixelFormat.dwZBufferBitDepth = 16;
    zd.ddpfPixelFormat.dwZBitMask = 0xffff;
    TRY(IDirectDraw7_CreateSurface(dd, &zd, &depth, 0));
    TRY(IDirectDrawSurface7_AddAttachedSurface(target, depth));
    TRY(IDirect3D7_CreateDevice(d3d, &IID_IDirect3DRGBDevice, target, &dev));
    D3DMATRIX ident;
    ZeroMemory(&ident, sizeof(ident));
    ident._11 = ident._22 = ident._33 = ident._44 = 1;
    TRY(IDirect3DDevice7_SetTransform(dev, D3DTRANSFORMSTATE_WORLD, &ident));
    TRY(IDirect3DDevice7_SetTransform(dev, D3DTRANSFORMSTATE_VIEW, &ident));
    TRY(IDirect3DDevice7_SetTransform(dev, D3DTRANSFORMSTATE_PROJECTION, &ident));
    D3DVIEWPORT7 vp = { 7, 11, 100, 80, .25f, .75f };
    TRY(IDirect3DDevice7_SetViewport(dev, &vp));
    TRY(IDirect3DDevice7_SetRenderState(dev, D3DRENDERSTATE_LIGHTING, FALSE));
    D3DDEVICEDESC7 caps;
    TRY(IDirect3DDevice7_GetCaps(dev, &caps));
    DWORD gb[4];
    CopyMemory(gb, &caps.dvGuardBandLeft, 16);
    wsprintfA(t, "GUARD words=%08lx,%08lx,%08lx,%08lx", gb[0], gb[1], gb[2], gb[3]);
    line(t);
    static const float input[][3] = { { 0, 0, .5f }, { -2, 0, .5f }, { 2, 0, .5f }, { 0, -2, .5f },
        { 0, 2, .5f }, { 0, 0, -.5f }, { 0, 0, 2 }, { -1, 0, .5f }, { 1, 0, .5f }, { 0, -1, .5f },
        { 0, 1, .5f }, { 0, 0, 0 }, { 0, 0, 1 }, { -100000, 0, .5f }, { 100000, 0, .5f }, { 0, -100000, .5f },
        { 0, 100000, .5f }, { -128, 0, .5f }, { 128, 0, .5f }, { -65536, 0, .5f }, { 65536, 0, .5f } };
    D3DVERTEXBUFFERDESC vb = { sizeof(vb), D3DVBCAPS_SYSTEMMEMORY, D3DFVF_XYZ, sizeof(input) / 12 };
    TRY(IDirect3D7_CreateVertexBuffer(d3d, &vb, &src, 0));
    vb.dwFVF = D3DFVF_XYZRHW;
    TRY(IDirect3D7_CreateVertexBuffer(d3d, &vb, &dst, 0));
    void* data;
    TRY(IDirect3DVertexBuffer7_Lock(src, 0, &data, 0));
    CopyMemory(data, input, sizeof(input));
    TRY(IDirect3DVertexBuffer7_Unlock(src));
    for (unsigned projection = 0; projection < 3; projection++)
    {
        ident._44 = projection == 0 ? 1 : projection == 1 ? 2 : -2;
        TRY(IDirect3DDevice7_SetTransform(dev, D3DTRANSFORMSTATE_PROJECTION, &ident));
        for (unsigned clipping = 0; clipping < 2; clipping++)
            for (unsigned op = 0; op < 2; op++)
            {
                TRY(IDirect3DDevice7_SetRenderState(dev, D3DRENDERSTATE_CLIPPING, clipping));
                TRY(IDirect3DVertexBuffer7_ProcessVertices(
                    dst, D3DVOP_TRANSFORM | (op ? D3DVOP_CLIP : 0), 0, sizeof(input) / 12, src, 0, dev, 0));
                TRY(IDirect3DVertexBuffer7_Lock(dst, DDLOCK_READONLY, &data, 0));
                for (unsigned i = 0; i < sizeof(input) / 12; i++)
                {
                    DWORD* v = (DWORD*)data + 4 * i;
                    wsprintfA(t,
                        "RESULT projection=%u clipping=%u op=%u vertex=%u words=%08lx,%08lx,%08lx,%08lx",
                        projection, clipping, op, i, v[0], v[1], v[2], v[3]);
                    line(t);
                }
                TRY(IDirect3DVertexBuffer7_Unlock(dst));
            }
    }
    static const float ranges[][2] = { { .25f, .75f }, { .5f, .5f }, { 0, 0 }, { 1, 1 }, { .8f, 1 } };
    struct TL
    {
        float x, y, z, rhw;
        DWORD color;
    } vertices[6] = { { 7, 11, 0, 1, 0xff00ff00 }, { 107, 11, 0, 1, 0xff00ff00 }, { 7, 91, 0, 1, 0xff00ff00 },
        { 107, 11, 0, 1, 0xff00ff00 }, { 107, 91, 0, 1, 0xff00ff00 }, { 7, 91, 0, 1, 0xff00ff00 } };
    TRY(IDirect3DDevice7_SetRenderState(dev, D3DRENDERSTATE_ZENABLE, TRUE));
    TRY(IDirect3DDevice7_SetRenderState(dev, D3DRENDERSTATE_ZWRITEENABLE, TRUE));
    TRY(IDirect3DDevice7_SetRenderState(dev, D3DRENDERSTATE_CULLMODE, D3DCULL_NONE));
    for (unsigned r = 0; r < 5; r++)
        for (unsigned z = 0; z < 2; z++)
        {
            vp.dvMinZ = ranges[r][0];
            vp.dvMaxZ = ranges[r][1];
            TRY(IDirect3DDevice7_SetViewport(dev, &vp));
            for (unsigned v = 0; v < 6; v++)
                vertices[v].z = z ? .9f : .1f;
            TRY(IDirect3DDevice7_SetRenderState(dev, D3DRENDERSTATE_ZFUNC, z ? D3DCMP_GREATER : D3DCMP_LESS));
            TRY(IDirect3DDevice7_Clear(
                dev, 0, 0, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0xff000000, z ? .8f : .2f, 0));
            TRY(IDirect3DDevice7_BeginScene(dev));
            TRY(IDirect3DDevice7_DrawPrimitive(
                dev, D3DPT_TRIANGLELIST, D3DFVF_XYZRHW | D3DFVF_DIFFUSE, vertices, 6, 0));
            TRY(IDirect3DDevice7_EndScene(dev));
            DDSURFACEDESC2 lock;
            ZeroMemory(&lock, sizeof(lock));
            lock.dwSize = sizeof(lock);
            TRY(IDirectDrawSurface7_Lock(target, 0, &lock, DDLOCK_WAIT | DDLOCK_READONLY, 0));
            DWORD pixel;
            CopyMemory(&pixel, (BYTE*)lock.lpSurface + 51 * lock.lPitch + 57 * 4, 4);
            wsprintfA(t, "DEPTH range=%u test=%u pixel=%08lx", r, z, pixel);
            line(t);
            TRY(IDirectDrawSurface7_Unlock(target, 0));
            if ((pixel & 0xffffffu) != 0xff00u)
            {
                line("FAIL native XYZRHW depth expectation");
                goto done;
            }
        }
    IDirectDrawSurface7_Release(depth);
    IDirect3DVertexBuffer7_Release(src);
    IDirect3DVertexBuffer7_Release(dst);
    IDirect3DDevice7_Release(dev);
    IDirectDrawSurface7_Release(target);
    IDirect3D7_Release(d3d);
    IDirectDraw7_Release(dd);
    line("PASS native clipping reference available");
done:
    line("PASS native clipping reference finished");
    CloseHandle(logfile);
    ExitProcess(0);
}
