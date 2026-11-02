/* Original Microsoft D3D7 RGB state-scope and alias reference, RAM tools only. */
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

static void scope(IDirect3DDevice7* dev, unsigned kind, unsigned state, DWORD a, DWORD b)
{
    for (unsigned type = 0; type <= 3; ++type) {
        DWORD block = 0, first = 0xcccccccc, second = 0xcccccccc, applied = 0xcccccccc;
        HRESULT set_a = kind ? IDirect3DDevice7_SetTextureStageState(dev, 0, state, a) : IDirect3DDevice7_SetRenderState(dev, state, a);
        HRESULT get_a = kind ? IDirect3DDevice7_GetTextureStageState(dev, 0, state, &first) : IDirect3DDevice7_GetRenderState(dev, state, &first);
        HRESULT create = type ? IDirect3DDevice7_CreateStateBlock(dev, type, &block) : IDirect3DDevice7_BeginStateBlock(dev);
        HRESULT set_b = kind ? IDirect3DDevice7_SetTextureStageState(dev, 0, state, b) : IDirect3DDevice7_SetRenderState(dev, state, b);
        HRESULT get_b = kind ? IDirect3DDevice7_GetTextureStageState(dev, 0, state, &second) : IDirect3DDevice7_GetRenderState(dev, state, &second);
        DWORD ended = 0xcccccccc;
        HRESULT end_hr = type ? D3D_OK : IDirect3DDevice7_EndStateBlock(dev, &block);
        HRESULT end_get = kind ? IDirect3DDevice7_GetTextureStageState(dev, 0, state, &ended) : IDirect3DDevice7_GetRenderState(dev, state, &ended);
        HRESULT apply = FAILED(create) ? create : IDirect3DDevice7_ApplyStateBlock(dev, block);
        HRESULT get = kind ? IDirect3DDevice7_GetTextureStageState(dev, 0, state, &applied) : IDirect3DDevice7_GetRenderState(dev, state, &applied);
        if (block)
            IDirect3DDevice7_DeleteStateBlock(dev, block);
        char text[400];
        wsprintfA(text, "SCOPE kind=%u state=%u type=%u a=%08lx b=%08lx first=%08lx second=%08lx applied=%08lx ended=%08lx endhr=%08lx,%08lx hr=%08lx,%08lx,%08lx,%08lx,%08lx,%08lx,%08lx", kind, state, type, a, b, first, second, applied, ended, end_hr, end_get, set_a, get_a, create, set_b, get_b, apply, get);
        line(text);
    }
}
static const unsigned alias_rs[] = { 3, 44, 45, 17, 18, 21, 128, 5, 6 };
static const unsigned alias_tss[] = { 12, 13, 14, 16, 17, 18, 1, 2, 3, 4, 5, 6 };
static void alias_query(IDirect3DDevice7* dev, unsigned test, const char* phase)
{
    char text[200];
    DWORD value;
    HRESULT hr;
    for (unsigned i = 0; i < sizeof(alias_rs) / sizeof(alias_rs[0]); ++i) {
        value = 0xcccccccc;
        hr = IDirect3DDevice7_GetRenderState(dev, alias_rs[i], &value);
        wsprintfA(text, "ALIAS test=%u phase=%s kind=0 state=%u value=%08lx hr=%08lx", test, phase, alias_rs[i], value, hr);
        line(text);
    }
    for (unsigned i = 0; i < sizeof(alias_tss) / sizeof(alias_tss[0]); ++i) {
        value = 0xcccccccc;
        hr = IDirect3DDevice7_GetTextureStageState(dev, 0, alias_tss[i], &value);
        wsprintfA(text, "ALIAS test=%u phase=%s kind=1 state=%u value=%08lx hr=%08lx", test, phase, alias_tss[i], value, hr);
        line(text);
    }
}
#define ALIAS_TRY(x)      \
    do {                  \
        HRESULT hr = (x); \
        status(#x, hr);   \
    } while (0)
static HRESULT aliases(IDirect3DDevice7* dev)
{
    for (unsigned test = 0; test < 12; ++test) {
        ALIAS_TRY(IDirect3DDevice7_SetRenderState(dev, 3, 1));
        ALIAS_TRY(IDirect3DDevice7_SetRenderState(dev, 17, 1));
        ALIAS_TRY(IDirect3DDevice7_SetRenderState(dev, 18, 1));
        ALIAS_TRY(IDirect3DDevice7_SetRenderState(dev, 21, 2));
        ALIAS_TRY(IDirect3DDevice7_SetRenderState(dev, 128, 0));
        ALIAS_TRY(IDirect3DDevice7_SetTextureStageState(dev, 0, 16, 1));
        ALIAS_TRY(IDirect3DDevice7_SetTextureStageState(dev, 0, 17, 1));
        ALIAS_TRY(IDirect3DDevice7_SetTextureStageState(dev, 0, 18, 1));
        if (test >= 6)
            ALIAS_TRY(IDirect3DDevice7_BeginStateBlock(dev));
        switch (test % 6) {
        case 0:
            ALIAS_TRY(IDirect3DDevice7_SetRenderState(dev, 3, 3));
            ALIAS_TRY(IDirect3DDevice7_SetTextureStageState(dev, 0, 13, 2));
            break;
        case 1:
            ALIAS_TRY(IDirect3DDevice7_SetTextureStageState(dev, 0, 13, 2));
            ALIAS_TRY(IDirect3DDevice7_SetRenderState(dev, 3, 3));
            break;
        case 2:
            ALIAS_TRY(IDirect3DDevice7_SetRenderState(dev, 17, 2));
            ALIAS_TRY(IDirect3DDevice7_SetTextureStageState(dev, 0, 16, 1));
            ALIAS_TRY(IDirect3DDevice7_SetRenderState(dev, 18, 6));
            ALIAS_TRY(IDirect3DDevice7_SetTextureStageState(dev, 0, 17, 1));
            break;
        case 3:
            ALIAS_TRY(IDirect3DDevice7_SetTextureStageState(dev, 0, 16, 2));
            ALIAS_TRY(IDirect3DDevice7_SetRenderState(dev, 17, 1));
            ALIAS_TRY(IDirect3DDevice7_SetTextureStageState(dev, 0, 18, 3));
            ALIAS_TRY(IDirect3DDevice7_SetRenderState(dev, 18, 1));
            break;
        case 4:
            ALIAS_TRY(IDirect3DDevice7_SetRenderState(dev, 21, 2));
            ALIAS_TRY(IDirect3DDevice7_SetTextureStageState(dev, 0, 1, 7));
            break;
        case 5:
            ALIAS_TRY(IDirect3DDevice7_SetTextureStageState(dev, 0, 1, 7));
            ALIAS_TRY(IDirect3DDevice7_SetRenderState(dev, 21, 2));
            break;
        }
        alias_query(dev, test, "during");
        if (test >= 6) {
            DWORD block;
            ALIAS_TRY(IDirect3DDevice7_EndStateBlock(dev, &block));
            alias_query(dev, test, "end");
            ALIAS_TRY(IDirect3DDevice7_ApplyStateBlock(dev, block));
            alias_query(dev, test, "apply");
            ALIAS_TRY(IDirect3DDevice7_DeleteStateBlock(dev, block));
        }
    }
    return D3D_OK;
}
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
    line("READY native Microsoft DDraw7 state-block reference");
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
    D3DDEVICEDESC7 caps;
    TRY(IDirect3DDevice7_GetCaps(dev, &caps));
    wsprintfA(t, "CAPS maxActive=%lu", caps.dwMaxActiveLights);
    line(t);

    for (unsigned state = 2; state <= 152; ++state) {
        if (!((state <= 41) || (state >= 43 && state <= 60) || (state >= 64 && state <= 95) || (state >= 128 && state <= 148) || state == 151 || state == 152))
            continue;
        scope(dev, 0, state, 1, 2);
    }
    for (unsigned state = 1; state <= 24; ++state) {
        DWORD a = 1, b = 2;
        if (state == 1 || state == 4) {
            a = 2;
            b = 7;
        } else if (state == 2 || state == 3 || state == 5 || state == 6) {
            a = 2;
            b = 0;
        } else if ((state >= 7 && state <= 10) || state == 19 || state == 22 || state == 23) {
            a = 0x3e800000;
            b = 0x3f000000;
        } else if (state == 11 || state == 20 || state == 24) {
            a = 0;
            b = 2;
        } else if (state == 12 || state == 13 || state == 14) {
            a = 1;
            b = 3;
        } else if (state == 15) {
            a = 0xff102030;
            b = 0xff405060;
        } else if (state == 18) {
            a = 1;
            b = 3;
        }
        scope(dev, 1, state, a, b);
    }
    TRY(aliases(dev));
    IDirect3DDevice7_Release(dev);
    IDirectDrawSurface7_Release(depth);
    IDirectDrawSurface7_Release(target);
    IDirect3D7_Release(d3d);
    IDirectDraw7_Release(dd);
    DestroyWindow(window);
    FreeLibrary(lib);
    line("PASS native state-scope and alias reference finished");
done:
    CloseHandle(logfile);
    ExitProcess(0);
}
