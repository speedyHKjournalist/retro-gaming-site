/* Original Microsoft D3D7 RGB object-default reference, RAM tools only. */
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
    line("READY native Microsoft DDraw7 object-default reference");
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

    for (unsigned generation = 0; generation < 2; ++generation) {
        D3DMATERIAL7 material;
        FillMemory(&material, sizeof(material), 0xcc);
        HRESULT hr = IDirect3DDevice7_GetMaterial(dev, &material);
        for (unsigned word = 0; word < sizeof(material) / 4; ++word) {
            DWORD value;
            CopyMemory(&value, (const BYTE*)&material + word * 4, 4);
            wsprintfA(t, "MATERIAL generation=%u word=%u value=%08lx hr=%08lx", generation, word, value, hr);
            line(t);
        }
        for (unsigned index = 0; index < 6; ++index) {
            float plane[4];
            FillMemory(plane, sizeof(plane), 0xcc);
            hr = IDirect3DDevice7_GetClipPlane(dev, index, plane);
            for (unsigned word = 0; word < 4; ++word) {
                DWORD value;
                CopyMemory(&value, (const BYTE*)plane + word * 4, 4);
                wsprintfA(t, "PLANE generation=%u index=%u word=%u value=%08lx hr=%08lx", generation, index, word, value, hr);
                line(t);
            }
        }
        if (!generation) {
            float changed[17];
            for (unsigned word = 0; word < 17; ++word)
                changed[word] = .25f;
            TRY(IDirect3DDevice7_SetMaterial(dev, (D3DMATERIAL7*)changed));
            float plane[4] = { 1, .5f, -.25f, .75f };
            for (unsigned index = 0; index < 6; ++index)
                TRY(IDirect3DDevice7_SetClipPlane(dev, index, plane));
            IDirect3DDevice7_Release(dev);
            dev = 0;
            TRY(IDirect3D7_CreateDevice(d3d, &IID_IDirect3DRGBDevice, target, &dev));
        }
    }
    IDirect3DDevice7_Release(dev);
    IDirectDrawSurface7_Release(depth);
    IDirectDrawSurface7_Release(target);
    IDirect3D7_Release(d3d);
    IDirectDraw7_Release(dd);
    DestroyWindow(window);
    FreeLibrary(lib);
    line("PASS native object default reference finished");
done:
    CloseHandle(logfile);
    ExitProcess(0);
}
