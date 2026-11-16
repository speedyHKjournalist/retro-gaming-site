/* XP system DDraw creation/control reference. No drawing through native DDraw. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#define DIRECTDRAW_VERSION 0x0700
#include <windows.h>
#include <initguid.h>
#include <ddraw.h>
static HANDLE output;
static void line(const char *text)
{
    DWORD n;
    WriteFile(output, text, lstrlenA(text), &n, NULL);
    WriteFile(output, "\r\n", 2, &n, NULL);
    FlushFileBuffers(output);
}
static void record(unsigned version, const char *name, HRESULT hr, IUnknown *surface)
{
    char text[200];
    wsprintfA(text, "RESULT version=%u name=%s hr=%08lX output=%u", version,
            name, (DWORD)hr, surface == (void *)0xdeadbeef ? 2u : !!surface);
    line(text);
}
static HRESULT create_surface(IUnknown *draw, unsigned version,
        DWORD caps, BOOL null_desc, IUnknown **surface)
{
    DDSURFACEDESC2 desc;
    DDSURFACEDESC old;
    ZeroMemory(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS;
    desc.ddsCaps.dwCaps = caps;
    if (!(caps & DDSCAPS_PRIMARYSURFACE)) {
        desc.dwFlags |= DDSD_WIDTH | DDSD_HEIGHT;
        desc.dwWidth = desc.dwHeight = 32;
    }
    if (caps & DDSCAPS_FLIP) {
        desc.dwFlags |= DDSD_BACKBUFFERCOUNT;
        desc.dwBackBufferCount = 1;
    }
    ZeroMemory(&old, sizeof(old));
    old.dwSize = sizeof(old); old.dwFlags = desc.dwFlags;
    old.dwWidth = desc.dwWidth; old.dwHeight = desc.dwHeight;
    old.dwBackBufferCount = desc.dwBackBufferCount;
    old.ddsCaps.dwCaps = caps;
    switch (version) {
    case 1: return IDirectDraw_CreateSurface((IDirectDraw *)draw,
            null_desc ? NULL : &old, (void *)surface, NULL);
    case 2: return IDirectDraw2_CreateSurface((IDirectDraw2 *)draw,
            null_desc ? NULL : &old, (void *)surface, NULL);
    case 3: return IDirectDraw3_CreateSurface((IDirectDraw3 *)draw,
            null_desc ? NULL : &old, (void *)surface, NULL);
    case 4: return IDirectDraw4_CreateSurface((IDirectDraw4 *)draw,
            null_desc ? NULL : &desc, (void *)surface, NULL);
    default: return IDirectDraw7_CreateSurface((IDirectDraw7 *)draw,
            null_desc ? NULL : &desc, (void *)surface, NULL);
    }
}
static HRESULT cooperative(IUnknown *draw, unsigned version, HWND window, DWORD flags)
{
    /* SetCooperativeLevel has the same ABI in each version. */
    (void)version;
    return IDirectDraw_SetCooperativeLevel((IDirectDraw *)draw, window, flags);
}
static BOOL reference(HMODULE module, HWND window)
{
    static const unsigned versions[] = {1, 2, 3, 4, 7};
    const GUID *ids[] = {&IID_IDirectDraw, &IID_IDirectDraw2,
            &IID_IDirectDraw3, &IID_IDirectDraw4, &IID_IDirectDraw7};
    HRESULT (WINAPI *create)(GUID *, IDirectDraw **, IUnknown *) =
            (void *)GetProcAddress(module, "DirectDrawCreate");
    HRESULT (WINAPI *create_ex)(GUID *, void **, REFIID, IUnknown *) =
            (void *)GetProcAddress(module, "DirectDrawCreateEx");
    for (unsigned i = 0; i < 5; ++i) {
        unsigned version = versions[i];
        IUnknown *root = NULL, *draw = NULL, *surface;
        HRESULT hr = version == 7 ? create_ex(NULL, (void **)&root,
                &IID_IDirectDraw7, NULL) : create(NULL, (void *)&root, NULL);
        if (FAILED(hr)) return FALSE;
        hr = IUnknown_QueryInterface(root, ids[i], (void **)&draw);
        record(version, "query_view", hr, NULL);
        IUnknown_Release(root);
        if (FAILED(hr)) continue;
        const DWORD roles[] = {DDSCAPS_PRIMARYSURFACE,
                DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY,
                DDSCAPS_OFFSCREENPLAIN | DDSCAPS_VIDEOMEMORY};
        const char *names[] = {"before_primary", "before_system", "before_video"};
        for (unsigned n = 0; n < 3; ++n) {
            surface = (void *)0xdeadbeef;
            hr = create_surface(draw, version, roles[n], FALSE, &surface);
            record(version, names[n], hr, surface);
            if (SUCCEEDED(hr) && surface) IUnknown_Release(surface);
        }
        surface = (void *)0xdeadbeef;
        hr = create_surface(draw, version, 0, TRUE, &surface);
        record(version, "before_null", hr, surface);
        hr = cooperative(draw, version, NULL, DDSCL_NORMAL);
        record(version, "normal", hr, NULL);
        surface = (void *)0xdeadbeef;
        hr = create_surface(draw, version, 0, TRUE, &surface);
        record(version, "normal_null", hr, surface);
        hr = cooperative(draw, version, window, DDSCL_EXCLUSIVE | DDSCL_FULLSCREEN);
        record(version, "exclusive", hr, NULL);
        surface = NULL;
        hr = create_surface(draw, version,
                DDSCAPS_PRIMARYSURFACE | DDSCAPS_COMPLEX | DDSCAPS_FLIP,
                FALSE, &surface);
        record(version, "flip_create", hr, surface);
        if (SUCCEEDED(hr)) {
            record(version, "flip_valid", IDirectDrawSurface_IsLost((IDirectDrawSurface *)surface), NULL);
            hr = cooperative(draw, version, window, DDSCL_NORMAL);
            record(version, "flip_normal", hr, NULL);
            record(version, "flip_lost", IDirectDrawSurface_IsLost((IDirectDrawSurface *)surface), NULL);
            /* Frozen Wine avoids native Restore of a flippable primary in
             * NORMAL because the system DLL can crash. Do not make that call. */
            IUnknown_Release(surface);
        }
        cooperative(draw, version, NULL, DDSCL_NORMAL);
        IUnknown_Release(draw);
    }
    line("COMPLETE creation_reference=1");
    return TRUE;
}
void WINAPI WinMainCRTStartup(void)
{
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, sizeof(path));
    UINT at = lstrlenA(path);
    while (at && path[at - 1] != '\\') --at;
    path[at] = 0; lstrcatA(path, "CREATE.TXT");
    output = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
            CREATE_ALWAYS, 0, NULL);
    if (output == INVALID_HANDLE_VALUE) ExitProcess(2);
    HWND window = CreateWindowA("static", "DDraw creation reference",
            WS_OVERLAPPEDWINDOW | WS_VISIBLE, 0, 0, 320, 240,
            NULL, NULL, GetModuleHandleA(NULL), NULL);
    GetSystemDirectoryA(path, sizeof(path)); lstrcatA(path, "\\ddraw.dll");
    HMODULE module = LoadLibraryA(path);
    BOOL ok = window && module && reference(module, window);
    if (!ok) line("COMPLETE creation_reference=0");
    if (window) DestroyWindow(window);
    if (module) FreeLibrary(module);
    CloseHandle(output); ExitProcess(ok ? 0 : 1);
}
