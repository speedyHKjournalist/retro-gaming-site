/* Independent XP descriptor observations. Load the system DLL dynamically. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>
static HANDLE output;
static BYTE client_bits[4096];
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
        DWORD flags, width, height, bits, zdepth, pf_flags, caps, pointer_is_null, pointer_is_client;
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
            caps = old.ddsCaps.dwCaps; pointer_is_null = old.lpSurface == NULL; pointer_is_client = old.lpSurface == client_bits;
        } else {
            modern.dwSize = sizeof(modern);
            hr = IDirectDrawSurface7_GetSurfaceDesc((IDirectDrawSurface7 *)view, &modern);
            flags = modern.dwFlags; width = modern.dwWidth; height = modern.dwHeight;
            pitch = modern.lPitch; bits = modern.ddpfPixelFormat.dwRGBBitCount;
            pf_flags = modern.ddpfPixelFormat.dwFlags; zdepth = modern.dwMipMapCount;
            caps = modern.ddsCaps.dwCaps; pointer_is_null = modern.lpSurface == NULL; pointer_is_client = modern.lpSurface == client_bits;
        }
        wsprintfA(s, "DESC name=%s view=%u hr=%08lX pfhr=%08lX flags=%08lX width=%lu height=%lu pitch=%ld bits=%lu union=%lu pf=%08lX directbits=%lu caps=%08lX null=%lu client=%lu",
            name, versions[i], (DWORD)hr, (DWORD)pfhr, flags, width, height,
            pitch, bits, zdepth, pf_flags, pf.dwRGBBitCount, caps, pointer_is_null, pointer_is_client);
        line(s);
        IDirectDrawSurface_Release(view);
    }
}
static BOOL reference(HMODULE module, HWND window)
{
    HRESULT (WINAPI *create)(GUID *, void **, REFIID, IUnknown *) =
        (void *)GetProcAddress(module, "DirectDrawCreateEx");
    IDirectDraw7 *draw = NULL;
    IDirectDrawSurface7 *surface = NULL;
    DDSURFACEDESC2 desc;
    HRESULT hr;
    if (!create || FAILED(create(NULL, (void **)&draw, &IID_IDirectDraw7, NULL))) return FALSE;
    hr = IDirectDraw7_SetCooperativeLevel(draw, window, DDSCL_NORMAL);
    result("normal", hr);
    if (FAILED(hr)) { IDirectDraw7_Release(draw); return FALSE; }
    ZeroMemory(&desc, sizeof(desc));desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    desc.dwWidth = desc.dwHeight = 8;
    desc.ddsCaps.dwCaps = DDSCAPS_SYSTEMMEMORY | DDSCAPS_OFFSCREENPLAIN;
    hr = IDirectDraw7_CreateSurface(draw, &desc, &surface, NULL);result("offscreen", hr);
    if (FAILED(hr)) { IDirectDraw7_Release(draw); return FALSE; }
    describe((IDirectDrawSurface *)surface, "initial");
    static const DWORD tail[][4] = {{0,0,0,0},{0,0xdeadbeef,0,0},{0,0,0xdeadbeef,0},
        {0,0,0,0xdeadbeef},{0,0xffffffff,0xffffffff,0xffffffff},
        {DDSCAPS_SYSTEMMEMORY,0,0,0},{DDSCAPS_SYSTEMMEMORY,0xdeadbeef,0,0}};
    for (unsigned i = 0; i < sizeof(tail)/sizeof(tail[0]); ++i) {
        char name[64];wsprintfA(name,"caps%u",i);
        ZeroMemory(&desc, sizeof(desc));desc.dwSize = sizeof(desc);
        desc.dwFlags = DDSD_CAPS | DDSD_LPSURFACE;desc.lpSurface = client_bits;
        desc.ddsCaps.dwCaps = tail[i][0];desc.ddsCaps.dwCaps2 = tail[i][1];
        desc.ddsCaps.dwCaps3 = tail[i][2];desc.ddsCaps.dwCaps4 = tail[i][3];
        result(name, IDirectDrawSurface7_SetSurfaceDesc(surface,&desc,0));
        describe((IDirectDrawSurface *)surface,name);
    }
    ZeroMemory(&desc,sizeof(desc));desc.dwSize=sizeof(desc);
    result("get_reapply",IDirectDrawSurface7_GetSurfaceDesc(surface,&desc));
    result("reapply",IDirectDrawSurface7_SetSurfaceDesc(surface,&desc,0));
    ZeroMemory(&desc,sizeof(desc));desc.dwSize=sizeof(desc);desc.dwFlags=DDSD_CAPS;
    desc.ddsCaps.dwCaps=DDSCAPS_OFFSCREENPLAIN;
    result("caps_without_pointer",IDirectDrawSurface7_SetSurfaceDesc(surface,&desc,0));
    describe((IDirectDrawSurface *)surface,"after_rejections");
    IDirectDrawSurface7_Release(surface);
    IDirectDraw7_Release(draw);
    line("COMPLETE surface memory descriptor reference");
    return TRUE;
}
void WINAPI WinMainCRTStartup(void)
{
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    unsigned at = lstrlenA(path);
    while (at && path[at - 1] != '\\') --at;
    path[at] = 0; lstrcatA(path, "MEMDESC.TXT");
    output = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL);
    if (output == INVALID_HANDLE_VALUE) ExitProcess(2);
    HWND window = CreateWindowA("static", "Native surface memory descriptor reference",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 320, 240, NULL, NULL, GetModuleHandleA(NULL), NULL);
#ifdef DDRAW_MEMORY_DESCRIPTOR_PROXY
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
