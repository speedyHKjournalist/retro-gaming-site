#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>

static HANDLE output;
static UINT api;
static void line(const char *text)
{
    DWORD written;
    WriteFile(output, text, lstrlenA(text), &written, NULL);
    WriteFile(output, "\r\n", 2, &written, NULL);
    FlushFileBuffers(output);
}
static HRESULT create(IDirectDraw7 *root, IDirectDraw4 *old,
        DDSURFACEDESC2 *desc, void **surface)
{
    return api == 4 ? IDirectDraw4_CreateSurface(old, desc,
            (IDirectDrawSurface4 **)surface, NULL)
        : IDirectDraw7_CreateSurface(root, desc,
            (IDirectDrawSurface7 **)surface, NULL);
}
static HRESULT lock(void *surface, UINT view, RECT *rect, void *desc)
{
    if (view == 1) return IDirectDrawSurface_Lock((IDirectDrawSurface *)surface,
            rect, (DDSURFACEDESC *)desc, DDLOCK_WAIT, NULL);
    if (view == 3) return IDirectDrawSurface3_Lock((IDirectDrawSurface3 *)surface,
            rect, (DDSURFACEDESC *)desc, DDLOCK_WAIT, NULL);
    if (view == 4) return IDirectDrawSurface4_Lock((IDirectDrawSurface4 *)surface,
            rect, (DDSURFACEDESC2 *)desc, DDLOCK_WAIT, NULL);
    return IDirectDrawSurface7_Lock((IDirectDrawSurface7 *)surface,
            rect, (DDSURFACEDESC2 *)desc, DDLOCK_WAIT, NULL);
}
static void unlock(void *surface, UINT view)
{
    if (view == 1) IDirectDrawSurface_Unlock((IDirectDrawSurface *)surface, NULL);
    else if (view == 3) IDirectDrawSurface3_Unlock((IDirectDrawSurface3 *)surface, NULL);
    else if (view == 4) IDirectDrawSurface4_Unlock((IDirectDrawSurface4 *)surface, NULL);
    else IDirectDrawSurface7_Unlock((IDirectDrawSurface7 *)surface, NULL);
}
void WINAPI WinMainCRTStartup(void)
{
    char path[MAX_PATH], text[512];
    UINT end;
    HMODULE module;
    HRESULT (WINAPI *factory)(GUID *, IDirectDraw **, IUnknown *);
    IDirectDraw *legacy = NULL;
    IDirectDraw7 *root = NULL;
    IDirectDraw4 *old = NULL;
    static const DWORD sizes[] = {108, 124, 109, 125, 216, 248, 107, 123,
            54, 62, 0, 1, 12, 512, 924};
    static const struct {DWORD caps, caps2;} resources[] = {
        {DDSCAPS_SYSTEMMEMORY | DDSCAPS_OFFSCREENPLAIN, 0},
        {DDSCAPS_SYSTEMMEMORY | DDSCAPS_TEXTURE, 0},
        {DDSCAPS_VIDEOMEMORY | DDSCAPS_TEXTURE, 0},
        {DDSCAPS_TEXTURE, DDSCAPS2_TEXTUREMANAGE},
    };
    static RECT rectangles[] = {{68,60,60,68}, {60,68,68,60}, {-8,60,0,68},
            {60,-8,68,0}, {-16,60,-8,68}, {60,-16,68,-8}, {60,60,136,68},
            {60,60,68,136}, {136,60,144,68}, {60,136,68,144}};
    GetModuleFileNameA(NULL, path, sizeof(path));
    end = lstrlenA(path);
    while (end && path[end - 1] != '\\') --end;
    lstrcpyA(path + end, "LOCKOLD.TXT");
    output = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
            CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (output == INVALID_HANDLE_VALUE) ExitProcess(1);
    module = LoadLibraryA("C:\\WINDOWS\\system32\\ddraw.dll");
    factory = (void *)GetProcAddress(module, "DirectDrawCreate");
    if (!factory || FAILED(factory(NULL, &legacy, NULL))) ExitProcess(2);
    if (FAILED(IDirectDraw_QueryInterface(legacy, &IID_IDirectDraw4, (void **)&old))) ExitProcess(3);
    IDirectDraw_Release(legacy);
    IDirectDraw4_SetCooperativeLevel(old, NULL, DDSCL_NORMAL);
    for (api = 4; api <= 4; api += 3) for (UINT r = 0; r < 4; ++r) {
        DDSURFACEDESC2 desc;
        void *surface = NULL;
        HRESULT hr;
        ZeroMemory(&desc, sizeof(desc));
        desc.dwSize = sizeof(desc);
        desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
        desc.dwWidth = desc.dwHeight = 128;
        desc.ddsCaps.dwCaps = resources[r].caps;
        desc.ddsCaps.dwCaps2 = resources[r].caps2;
        desc.ddpfPixelFormat.dwSize = sizeof(desc.ddpfPixelFormat);
        desc.ddpfPixelFormat.dwFlags = DDPF_RGB;
        desc.ddpfPixelFormat.dwRGBBitCount = 32;
        desc.ddpfPixelFormat.dwRBitMask = 0xff0000;
        desc.ddpfPixelFormat.dwGBitMask = 0x00ff00;
        desc.ddpfPixelFormat.dwBBitMask = 0x0000ff;
        hr = create(root, old, &desc, &surface);
        wsprintfA(text, "CREATE api=%u role=%u hr=%08lX", api, r, (DWORD)hr);
        line(text);
        if (FAILED(hr)) continue;
        for (UINT v = 0; v < 3; ++v) {
            UINT view = v == 0 ? 1 : v == 1 ? 3 : api;
            REFIID iid = view == 1 ? &IID_IDirectDrawSurface
                : view == 3 ? &IID_IDirectDrawSurface3
                : view == 4 ? &IID_IDirectDrawSurface4 : &IID_IDirectDrawSurface7;
            void *iface = NULL;
            IUnknown_QueryInterface((IUnknown *)surface, iid, &iface);
            if (!iface) ExitProcess(4);
            for (UINT i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i) {
                union {DDSURFACEDESC2 desc; BYTE bytes[1024];} blob;
                ZeroMemory(&blob, sizeof(blob));
                blob.desc.dwSize = sizes[i];
                blob.desc.dwTextureStage = 0xdeadbeef;
                blob.bytes[124] = 0xef;
                hr = lock(iface, view, NULL, &blob);
                wsprintfA(text, "SIZE api=%u role=%u view=%u input=%lu hr=%08lX size=%lu width=%lu height=%lu stage=%08lX guard=%02X", api, r, view, sizes[i], (DWORD)hr, blob.desc.dwSize, blob.desc.dwWidth, blob.desc.dwHeight, blob.desc.dwTextureStage, blob.bytes[124]);
                line(text);
                if (SUCCEEDED(hr)) unlock(iface, view);
            }
            for (UINT i = 0; i < sizeof(rectangles) / sizeof(rectangles[0]); ++i) {
                DDSURFACEDESC2 blob;
                ZeroMemory(&blob, sizeof(blob));
                blob.dwSize = sizeof(blob);
                blob.lpSurface = (void *)0x1010101;
                hr = lock(iface, view, &rectangles[i], &blob);
                wsprintfA(text, "RECT api=%u role=%u view=%u case=%u hr=%08lX pointer=%u", api, r, view, i, (DWORD)hr, !!blob.lpSurface);
                line(text);
                if (SUCCEEDED(hr)) unlock(iface, view);
            }
            IUnknown_Release((IUnknown *)iface);
        }
        IUnknown_Release((IUnknown *)surface);
    }
    IDirectDraw4_Release(old);

    FreeLibrary(module);
    line("PASS native legacy surface lock reference complete");
    CloseHandle(output);
    ExitProcess(0);
}
