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
static void read_clip_status(IDirect3DDevice7* device, const char* label, unsigned test, DWORD flags)
{
    D3DCLIPSTATUS value;
    FillMemory(&value, sizeof(value), 0xcc);
    value.dwFlags = flags;
    HRESULT hr = IDirect3DDevice7_GetClipStatus(device, &value);
    DWORD* p = (DWORD*)&value;
    char text[240];
    wsprintfA(text, "STATUS label=%s test=%u query=%lu hr=%08lx words=%08lx,%08lx,%08lx,%08lx,%08lx,%08lx,%08lx,%08lx", label, test, flags, hr, p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
    line(text);
}
#define TRY(x)            \
    do {                  \
        HRESULT hr = (x); \
        status(#x, hr);   \
        if (FAILED(hr))   \
            goto done;    \
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
    line("READY native Microsoft DDraw7 clip-status reference");
#ifdef D9WG_PROXY_CAPTURE
    GetModuleFileNameA(NULL, p, sizeof(p));
    at = lstrlenA(p);
    while (at && p[at - 1] != '\\')
        --at;
    p[at] = 0;
    lstrcatA(p, "ddraw.dll");
#else
    GetSystemDirectoryA(p, sizeof(p));
    lstrcatA(p, "\\ddraw.dll");
#endif
    HMODULE lib = LoadLibraryA(p);
    if (!lib) {
        status("load native ddraw", HRESULT_FROM_WIN32(GetLastError()));
        goto done;
    }
    HRESULT(WINAPI * create)(GUID*, void**, REFIID, IUnknown*)
        = (void*)GetProcAddress(lib, "DirectDrawCreateEx");
    if (!create) {
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
    for (unsigned query = 0; query < 8; ++query)
        read_clip_status(dev, "default", 0, query);
    static const float input[2][3][3] = { { { -2, -.5f, .5f }, { -3, .5f, .5f }, { -4, 0, .5f } }, { { -2, -.5f, .5f }, { .5f, -.5f, .5f }, { 0, .5f, .5f } } };
    D3DVERTEXBUFFERDESC vb = { sizeof(vb), D3DVBCAPS_SYSTEMMEMORY, D3DFVF_XYZ, 3 };
    TRY(IDirect3D7_CreateVertexBuffer(d3d, &vb, &src, 0));
    vb.dwFVF = D3DFVF_XYZRHW;
    TRY(IDirect3D7_CreateVertexBuffer(d3d, &vb, &dst, 0));
    for (unsigned test = 0; test < 12; ++test) {
        unsigned object = test % 2u, clipping = (test / 2u) % 2u, operation = test / 4u;
        void* data;
        TRY(IDirect3DVertexBuffer7_Lock(src, 0, &data, 0));
        CopyMemory(data, input[object], sizeof(input[object]));
        TRY(IDirect3DVertexBuffer7_Unlock(src));
        D3DCLIPSTATUS initial;
        ZeroMemory(&initial, sizeof(initial));
        initial.dwFlags = D3DCLIPSTATUS_STATUS | D3DCLIPSTATUS_EXTENTS2;
        initial.dwStatus = D3DSTATUS_CLIPINTERSECTIONALL;
        initial.minx = initial.miny = initial.minz = 10000;
        initial.maxx = initial.maxy = initial.maxz = -10000;
        TRY(IDirect3DDevice7_SetClipStatus(dev, &initial));
        read_clip_status(dev, "set", test, initial.dwFlags);
        TRY(IDirect3DDevice7_SetRenderState(dev, D3DRENDERSTATE_CLIPPING, clipping));
        DWORD op = D3DVOP_TRANSFORM | (operation ? D3DVOP_CLIP : 0) | (operation == 2 ? D3DVOP_EXTENTS : 0);
        TRY(IDirect3DVertexBuffer7_ProcessVertices(dst, op, 0, 3, src, 0, dev, 0));
        read_clip_status(dev, "processed", test, initial.dwFlags);
        TRY(IDirect3DDevice7_BeginScene(dev));
        TRY(IDirect3DDevice7_DrawPrimitiveVB(dev, D3DPT_TRIANGLELIST, dst, 0, 3, 0));
        TRY(IDirect3DDevice7_EndScene(dev));
        read_clip_status(dev, "buffer-draw", test, initial.dwFlags);
        TRY(IDirect3DDevice7_SetClipStatus(dev, &initial));
        TRY(IDirect3DDevice7_BeginScene(dev));
        TRY(IDirect3DDevice7_DrawPrimitiveVB(dev, D3DPT_TRIANGLELIST, dst, 0, 3, 0));
        TRY(IDirect3DDevice7_EndScene(dev));
        read_clip_status(dev, "buffer-reset-draw", test, initial.dwFlags);
        TRY(IDirect3DDevice7_SetClipStatus(dev, &initial));
        TRY(IDirect3DDevice7_BeginScene(dev));
        TRY(IDirect3DDevice7_DrawPrimitive(dev, D3DPT_TRIANGLELIST, D3DFVF_XYZ, (void*)input[object], 3, 0));
        TRY(IDirect3DDevice7_EndScene(dev));
        read_clip_status(dev, "direct-draw", test, initial.dwFlags);
    }
    for (unsigned flags = 0; flags < 8; ++flags) {
        D3DCLIPSTATUS initial = { flags, 0x01234567u, 10, 20, 30, 40, 50, 60 };
        HRESULT hr = IDirect3DDevice7_SetClipStatus(dev, &initial);
        wsprintfA(t, "SETFLAGS flags=%u hr=%08lx", flags, hr);
        line(t);
        read_clip_status(dev, "flags", flags, 7);
    }
    {
        D3DCLIPSTATUS initial = { D3DCLIPSTATUS_STATUS, D3DSTATUS_DEFAULT | D3DSTATUS_CLIPUNIONGEN5, 0, 0, 0, 0, 0, 0 };
        TRY(IDirect3DDevice7_SetClipStatus(dev, &initial));
        for (unsigned object = 0; object < 2; ++object) {
            float vertices[3][3];
            CopyMemory(vertices, input[0], sizeof(vertices));
            if (object)
                for (unsigned i = 0; i < 3; ++i)
                    vertices[i][0] = -vertices[i][0];
            void* data;
            TRY(IDirect3DVertexBuffer7_Lock(src, 0, &data, 0));
            CopyMemory(data, vertices, sizeof(vertices));
            TRY(IDirect3DVertexBuffer7_Unlock(src));
            TRY(IDirect3DVertexBuffer7_ProcessVertices(dst, D3DVOP_TRANSFORM | D3DVOP_CLIP, 0, 3, src, 0, dev, 0));
            read_clip_status(dev, "accumulate", object, 3);
        }
        TRY(IDirect3DDevice7_Clear(dev, 0, NULL, D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER, 0, 1, 0));
        TRY(IDirect3DDevice7_BeginScene(dev));
        TRY(IDirect3DDevice7_DrawPrimitive(dev, D3DPT_TRIANGLELIST, D3DFVF_XYZ, (void*)input[1], 3, 0));
        TRY(IDirect3DDevice7_EndScene(dev));
        read_clip_status(dev, "accumulate", 2, 3);
    }
    for (unsigned edge = 0; edge < 6; ++edge) {
        float vertices[3][3] = { { -.5f, -.5f, .5f }, { .5f, -.5f, .5f }, { 0, .5f, .5f } };
        for (unsigned i = 0; i < 3; ++i) {
            if (edge < 2)
                vertices[i][0] += edge ? 3 : -3;
            else if (edge < 4)
                vertices[i][1] += edge == 2 ? -3 : 3;
            else if (edge == 5) {
                vertices[i][0] = .137f;
                vertices[i][1] = -.173f;
            }
        }
        D3DCLIPSTATUS initial = { D3DCLIPSTATUS_STATUS | D3DCLIPSTATUS_EXTENTS2, D3DSTATUS_DEFAULT, 10000, -10000, 10000, -10000, 0, 0 };
        TRY(IDirect3DDevice7_SetClipStatus(dev, &initial));
        void* data;
        TRY(IDirect3DVertexBuffer7_Lock(src, 0, &data, 0));
        CopyMemory(data, vertices, sizeof(vertices));
        TRY(IDirect3DVertexBuffer7_Unlock(src));
        TRY(IDirect3DVertexBuffer7_ProcessVertices(dst, D3DVOP_TRANSFORM | D3DVOP_CLIP | D3DVOP_EXTENTS, 0, 3, src, 0, dev, 0));
        read_clip_status(dev, "extent", edge, 3);
    }
#ifdef D9WG_STATUS_DRAW_CASES
    {
        D3DCLIPSTATUS initial = { 3, 0x00fff000u, 10000, -10000, 10000, -10000, 0, 0 };
        float vertices[5][3] = { { -2, -.5f, .5f }, { -3, .5f, .5f }, { -4, 0, .5f }, { 0, 0, .5f }, { 3, 0, .5f } };
        WORD indices[3][3] = { { 0, 1, 2 }, { 3, 3, 3 }, { 2, 0, 1 } };
        TRY(IDirect3DDevice7_SetRenderState(dev, D3DRENDERSTATE_CLIPPING, TRUE));
        for (unsigned selection = 0; selection < 3; ++selection) {
            TRY(IDirect3DDevice7_SetClipStatus(dev, &initial));
            TRY(IDirect3DDevice7_BeginScene(dev));
            TRY(IDirect3DDevice7_DrawIndexedPrimitive(dev, D3DPT_TRIANGLELIST, D3DFVF_XYZ, vertices, 5, indices[selection], 3, 0));
            TRY(IDirect3DDevice7_EndScene(dev));
            read_clip_status(dev, "indexed-input", selection, 3);
        }
        void* data;
        TRY(IDirect3DVertexBuffer7_Lock(src, 0, &data, 0));
        CopyMemory(data, vertices, 36);
        TRY(IDirect3DVertexBuffer7_Unlock(src));
        for (unsigned clipping = 0; clipping < 2; ++clipping) {
            TRY(IDirect3DDevice7_SetRenderState(dev, D3DRENDERSTATE_CLIPPING, clipping));
            TRY(IDirect3DDevice7_SetClipStatus(dev, &initial));
            TRY(IDirect3DDevice7_BeginScene(dev));
            TRY(IDirect3DDevice7_DrawPrimitiveVB(dev, D3DPT_TRIANGLELIST, src, 0, 3, 0));
            TRY(IDirect3DDevice7_EndScene(dev));
            read_clip_status(dev, "source-vb", clipping, 3);
        }
        float screen[3][4] = { { -5, 40, .5f, 1 }, { -10, 60, .5f, 1 }, { -15, 50, .5f, 1 } };
        for (unsigned clipping = 0; clipping < 2; ++clipping) {
            TRY(IDirect3DDevice7_SetRenderState(dev, D3DRENDERSTATE_CLIPPING, clipping));
            TRY(IDirect3DDevice7_SetClipStatus(dev, &initial));
            TRY(IDirect3DDevice7_BeginScene(dev));
            TRY(IDirect3DDevice7_DrawPrimitive(dev, D3DPT_TRIANGLELIST, D3DFVF_XYZRHW, screen, 3, 0));
            TRY(IDirect3DDevice7_EndScene(dev));
            read_clip_status(dev, "screen-up", clipping, 3);
        }
        float plane[4] = { 1, 0, 0, 0 };
        screen[0][0] = 32;
        screen[1][0] = 55;
        screen[2][0] = 42;
        TRY(IDirect3DDevice7_SetClipPlane(dev, 0, plane));
        TRY(IDirect3DDevice7_SetRenderState(dev, D3DRENDERSTATE_CLIPPLANEENABLE, 1));
        TRY(IDirect3DDevice7_SetClipStatus(dev, &initial));
        TRY(IDirect3DDevice7_BeginScene(dev));
        TRY(IDirect3DDevice7_DrawPrimitive(dev, D3DPT_TRIANGLELIST, D3DFVF_XYZRHW, screen, 3, 0));
        TRY(IDirect3DDevice7_EndScene(dev));
        read_clip_status(dev, "screen-user", 0, 3);
        TRY(IDirect3DVertexBuffer7_Lock(dst, 0, &data, 0));
        CopyMemory(data, screen, sizeof(screen));
        TRY(IDirect3DVertexBuffer7_Unlock(dst));
        TRY(IDirect3DDevice7_SetClipStatus(dev, &initial));
        TRY(IDirect3DDevice7_BeginScene(dev));
        TRY(IDirect3DDevice7_DrawPrimitiveVB(dev, D3DPT_TRIANGLELIST, dst, 0, 3, 0));
        TRY(IDirect3DDevice7_EndScene(dev));
        read_clip_status(dev, "screen-vb", 0, 3);
        plane[0] = -1;
        TRY(IDirect3DDevice7_SetClipPlane(dev, 0, plane));
        TRY(IDirect3DDevice7_SetClipStatus(dev, &initial));
        TRY(IDirect3DDevice7_BeginScene(dev));
        TRY(IDirect3DDevice7_DrawPrimitive(dev, D3DPT_TRIANGLELIST, D3DFVF_XYZRHW, screen, 3, 0));
        TRY(IDirect3DDevice7_EndScene(dev));
        read_clip_status(dev, "screen-user", 1, 3);
        TRY(IDirect3DDevice7_SetRenderState(dev, D3DRENDERSTATE_CLIPPLANEENABLE, 0));
        const float depth_values[5] = { .1f, .9f, -.1f, 1.1f, .5f };
        for (unsigned depth_case = 0; depth_case < 5; ++depth_case) {
            for (unsigned i = 0; i < 3; ++i)
                screen[i][2] = depth_values[depth_case];
            if (depth_case == 4) {
                vp.dvMinZ = vp.dvMaxZ = 0;
                TRY(IDirect3DDevice7_SetViewport(dev, &vp));
            }
            TRY(IDirect3DDevice7_SetClipStatus(dev, &initial));
            TRY(IDirect3DDevice7_BeginScene(dev));
            TRY(IDirect3DDevice7_DrawPrimitive(dev, D3DPT_TRIANGLELIST, D3DFVF_XYZRHW, screen, 3, 0));
            TRY(IDirect3DDevice7_EndScene(dev));
            read_clip_status(dev, "screen-depth", depth_case, 3);
        }
    }
#endif
    IDirect3DVertexBuffer7_Release(src);
    IDirect3DVertexBuffer7_Release(dst);
    IDirect3DDevice7_Release(dev);
    IDirectDrawSurface7_Release(depth);
    IDirectDrawSurface7_Release(target);
    IDirect3D7_Release(d3d);
    IDirectDraw7_Release(dd);
    DestroyWindow(window);
    line("PASS native clip-status reference available");
done:
    line("PASS native clip-status reference finished");
    CloseHandle(logfile);
    ExitProcess(0);
}
