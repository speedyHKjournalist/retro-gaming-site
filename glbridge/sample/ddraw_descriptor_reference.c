/* Independent XP descriptor observations. Load the system DLL dynamically. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
static HANDLE output;
static void line(const char *s)
{
    DWORD n;
    WriteFile(output, s, lstrlenA(s), &n, NULL);
    WriteFile(output, "\r\n", 2, &n, NULL);
    FlushFileBuffers(output);
}
static void result(const char *name, HRESULT hr)
{
    char s[160];
    wsprintfA(s, "RESULT name=%s hr=%08lX", name, (DWORD)hr);
    line(s);
}
static void describe(IDirectDrawSurface *surface, const char *name)
{
    const GUID *ids[] = {&IID_IDirectDrawSurface, &IID_IDirectDrawSurface2,
        &IID_IDirectDrawSurface3, &IID_IDirectDrawSurface4, &IID_IDirectDrawSurface7};
    const unsigned versions[] = {1, 2, 3, 4, 7};
    unsigned i;
    for (i = 0; i < 5; ++i) {
        IDirectDrawSurface *view = NULL;
        HRESULT qi = IDirectDrawSurface_QueryInterface(surface, ids[i], (void **)&view);
        char s[360];
        if (FAILED(qi)) {
            wsprintfA(s, "QUERY name=%s view=%u hr=%08lX", name, versions[i], (DWORD)qi);
            line(s);
            continue;
        }
        DDSURFACEDESC old;
        DDSURFACEDESC2 modern;
        DDPIXELFORMAT pf;
        DWORD flags, width, height, bits, zdepth, pf_flags, caps;
        LONG pitch;
        HRESULT hr;
        ZeroMemory(&old, sizeof(old));
        ZeroMemory(&modern, sizeof(modern));
        ZeroMemory(&pf, sizeof(pf));
        pf.dwSize = sizeof(pf);
        HRESULT pfhr = IDirectDrawSurface_GetPixelFormat(view, &pf);
        if (versions[i] < 4) {
            old.dwSize = sizeof(old);
            hr = IDirectDrawSurface_GetSurfaceDesc(view, &old);
            flags = old.dwFlags; width = old.dwWidth; height = old.dwHeight;
            pitch = old.lPitch; bits = old.ddpfPixelFormat.dwRGBBitCount;
            pf_flags = old.ddpfPixelFormat.dwFlags; zdepth = old.dwZBufferBitDepth;
            caps = old.ddsCaps.dwCaps;
        } else {
            modern.dwSize = sizeof(modern);
            hr = IDirectDrawSurface7_GetSurfaceDesc((IDirectDrawSurface7 *)view, &modern);
            flags = modern.dwFlags; width = modern.dwWidth; height = modern.dwHeight;
            pitch = modern.lPitch; bits = modern.ddpfPixelFormat.dwRGBBitCount;
            pf_flags = modern.ddpfPixelFormat.dwFlags; zdepth = modern.dwMipMapCount;
            caps = modern.ddsCaps.dwCaps;
        }
        wsprintfA(s, "DESC name=%s view=%u hr=%08lX pfhr=%08lX flags=%08lX width=%lu height=%lu pitch=%ld bits=%lu union=%lu pf=%08lX directbits=%lu caps=%08lX",
            name, versions[i], (DWORD)hr, (DWORD)pfhr, flags, width, height,
            pitch, bits, zdepth, pf_flags, pf.dwRGBBitCount, caps);
        line(s);
        IDirectDrawSurface_Release(view);
    }
}
static BOOL reference(HMODULE module, HWND window)
{
    HRESULT (WINAPI *create)(GUID *, IDirectDraw **, IUnknown *) =
        (void *)GetProcAddress(module, "DirectDrawCreate");
    IDirectDraw *draw = NULL;
    IDirectDrawSurface *surface = NULL;
    DDSURFACEDESC desc;
    HRESULT hr;
    if (!create || FAILED(create(NULL, &draw, NULL))) return FALSE;
    hr = IDirectDraw_SetCooperativeLevel(draw, window, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN);
    result("exclusive", hr);
    if (FAILED(hr)) { IDirectDraw_Release(draw); return FALSE; }
    ZeroMemory(&desc, sizeof(desc)); desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS; desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
    hr = IDirectDraw_CreateSurface(draw, &desc, &surface, NULL);
    result("primary", hr);
    if (SUCCEEDED(hr)) {
        describe(surface, "primary_initial");
        result("mode16", IDirectDraw_SetDisplayMode(draw, 640, 480, 16));
        describe(surface, "primary_mode16");
        result("mode32", IDirectDraw_SetDisplayMode(draw, 800, 600, 32));
        describe(surface, "primary_mode32");
        IDirectDrawSurface_Release(surface);
    }
    result("restore", IDirectDraw_RestoreDisplayMode(draw));
    result("normal", IDirectDraw_SetCooperativeLevel(draw, window, DDSCL_NORMAL));
    for (unsigned explicit_pf = 0; explicit_pf < 2; ++explicit_pf)
        for (unsigned bits = 16; bits <= 32; bits += 16) {
            char name[64];
            wsprintfA(name, "depth%u_pf%u", bits, explicit_pf);
            ZeroMemory(&desc, sizeof(desc)); desc.dwSize = sizeof(desc);
            desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
            desc.dwWidth = desc.dwHeight = 16;
            desc.ddsCaps.dwCaps = DDSCAPS_ZBUFFER | DDSCAPS_SYSTEMMEMORY;
            if (explicit_pf) {
                desc.dwFlags |= DDSD_PIXELFORMAT;
                desc.ddpfPixelFormat.dwSize = sizeof(desc.ddpfPixelFormat);
                desc.ddpfPixelFormat.dwFlags = DDPF_ZBUFFER;
                desc.ddpfPixelFormat.dwZBufferBitDepth = bits;
                desc.ddpfPixelFormat.dwZBitMask = bits == 16 ? 0xffff : 0xffffffff;
            } else {
                desc.dwFlags |= DDSD_ZBUFFERBITDEPTH; desc.dwZBufferBitDepth = bits;
            }
            surface = NULL;
            hr = IDirectDraw_CreateSurface(draw, &desc, &surface, NULL);
            result(name, hr);
            if (SUCCEEDED(hr)) { describe(surface, name); IDirectDrawSurface_Release(surface); }
        }
    IDirectDraw_Release(draw);
    line("COMPLETE descriptor reference");
    return TRUE;
}
void WINAPI WinMainCRTStartup(void)
{
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    unsigned at = lstrlenA(path);
    while (at && path[at - 1] != '\\') --at;
    path[at] = 0; lstrcatA(path, "DESCREF.TXT");
    output = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (output == INVALID_HANDLE_VALUE) ExitProcess(2);
    HWND window = CreateWindowA("static", "Native descriptor reference",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 320, 240, NULL, NULL, GetModuleHandleA(NULL), NULL);
#ifdef DDRAW_DESCRIPTOR_PROXY
    GetModuleFileNameA(NULL, path, MAX_PATH);
    at = lstrlenA(path);
    while (at && path[at - 1] != 92) --at;
    path[at] = 0; lstrcatA(path, "ddraw.dll");
#else
    GetSystemDirectoryA(path, MAX_PATH); lstrcatA(path, "\\ddraw.dll");
#endif
    HMODULE module = LoadLibraryA(path);
    BOOL ok = window && module && reference(module, window);
    if (!ok) line("FAIL descriptor reference setup");
    if (window) DestroyWindow(window);
    if (module) FreeLibrary(module);
    CloseHandle(output);
    ExitProcess(ok ? 0 : 1);
}
