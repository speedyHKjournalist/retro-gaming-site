/* Original Microsoft RGB ProcessVertices output-layout reference.
 * Uses RAM tools storage and dumps only the nominal allocated vertex bytes. */
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
#define TRY(x)            \
    do {                  \
        HRESULT hr = (x); \
        status(#x, hr);   \
        if (FAILED(hr))   \
            goto done;    \
    } while (0)
void WINAPI WinMainCRTStartup(void)
{
    char p[MAX_PATH];
    GetModuleFileNameA(0, p, sizeof(p));
    int at = lstrlenA(p);
    while (at && p[at - 1] != '\\')
        --at;
    p[at] = 0;
    lstrcatA(p, "LAYOUT.TXT");
    logfile = CreateFileA(p, GENERIC_WRITE, FILE_SHARE_READ, 0, CREATE_ALWAYS, 0, 0);
    if (logfile == INVALID_HANDLE_VALUE)
        ExitProcess(2);
    line("READY native Microsoft DDraw7 output-layout reference");
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

    /* Original RGB hangs on OP_CLIP with bare XYZ output. Test clipping
     * only with XYZRHW; never write the final nominal vertex in native RGB. */
    static const DWORD formats[] = { 0x2, 0x42, 0x82, 0x12, 0xc2, 0x102,
        0x30102, 0x10102, 0x20102, 0x1d2, 0x1d4, 0x4 };
    static const unsigned strides[] = { 12, 16, 16, 24, 20, 20, 16, 24, 28, 40, 44, 16 };
    D3DVERTEXBUFFERDESC vb = { sizeof(vb), D3DVBCAPS_SYSTEMMEMORY, 2, 7 };
    static const DWORD source_formats[] = { 2, 0x1d2, 0x201d2 };
    static const unsigned source_strides[] = { 12, 40, 48 };
    unsigned test = 0;
    for (unsigned sf = 0; sf < 3; ++sf) {
        vb.dwFVF = source_formats[sf];
        TRY(IDirect3D7_CreateVertexBuffer(d3d, &vb, &src, 0));
        DWORD* data;
        TRY(IDirect3DVertexBuffer7_Lock(src, 0, (void**)&data, 0));
        for (unsigned i = 0; i < 7; ++i) {
            float input[12] = { .25f * ((int)i - 2), .25f * (3 - (int)i), .125f * i,
                .25f * i, -.5f, .75f, 0, 0, .125f * i, .25f, .5f, .75f };
            DWORD* v = data + i * source_strides[sf] / 4;
            CopyMemory(v, input, source_strides[sf]);
            if (sf) {
                v[6] = 0x81234560u + i;
                v[7] = 0x596789a0u + i;
            }
        }
        TRY(IDirect3DVertexBuffer7_Unlock(src));
        D3DMATRIX projection = ident;
        projection._44 = 2;
        TRY(IDirect3DDevice7_SetTransform(dev, D3DTRANSFORMSTATE_PROJECTION, &projection));
        for (unsigned f = 0; f < sizeof(formats) / sizeof(formats[0]); ++f) {
            vb.dwFVF = formats[f];
            TRY(IDirect3D7_CreateVertexBuffer(d3d, &vb, &dst, 0));
            for (unsigned mode = 0; mode < ((formats[f] & 14) == 4 ? 4u : 2u); ++mode)
                for (unsigned edge = 0; edge < 2; ++edge) {
                    TRY(IDirect3DVertexBuffer7_Lock(dst, 0, (void**)&data, 0));
                    for (unsigned word = 0; word < 7 * strides[f] / 4; ++word)
                        data[word] = 0xa5000000u + word;
                    TRY(IDirect3DVertexBuffer7_Unlock(dst));
                    DWORD start = edge ? 5 : 1, count = edge ? 1 : 3;
                    DWORD operation = D3DVOP_TRANSFORM | ((mode & 2) ? D3DVOP_CLIP : 0);
                    DWORD flags = (mode & 1) ? D3DPV_DONOTCOPYDATA : 0;
                    HRESULT hr = IDirect3DVertexBuffer7_ProcessVertices(dst, operation, start, count, src, 1, dev, flags);
                    char text[240];
                    wsprintfA(text, "CASE id=%u source=%u fvf=%08lx stride=%u mode=%u edge=%u hr=%08lx", test, sf, formats[f], strides[f], mode, edge, hr);
                    line(text);
                    TRY(IDirect3DVertexBuffer7_Lock(dst, 0, (void**)&data, 0));
                    for (unsigned word = 0; word < 7 * strides[f] / 4; word += 4) {
                        unsigned n = 7 * strides[f] / 4 - word;
                        if (n > 4)
                            n = 4;
                        for (unsigned j = 0; j < n; ++j) {
                            wsprintfA(text, "WORD id=%u offset=%u value=%08lx", test, (word + j) * 4, data[word + j]);
                            line(text);
                        }
                    }
                    TRY(IDirect3DVertexBuffer7_Unlock(dst));
                    ++test;
                }
            IDirect3DVertexBuffer7_Release(dst);
            dst = 0;
        }
        IDirect3DVertexBuffer7_Release(src);
        src = 0;
    }
    line("PASS native output-layout reference available");
    if (src)
        IDirect3DVertexBuffer7_Release(src);
    if (dev)
        IDirect3DDevice7_Release(dev);
    if (depth)
        IDirectDrawSurface7_Release(depth);
    if (target)
        IDirectDrawSurface7_Release(target);
    if (d3d)
        IDirect3D7_Release(d3d);
    if (dd)
        IDirectDraw7_Release(dd);
    line("PASS native output-layout reference finished");
done:
    CloseHandle(logfile);
    ExitProcess(0);
}
