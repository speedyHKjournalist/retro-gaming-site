#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <ddraw.h>

static HANDLE output;
static UINT count, nonnull;
static HRESULT CALLBACK surface_cb(IDirectDrawSurface *surface,
        DDSURFACEDESC *desc, void *context)
{
    char text[240];
    (void)context;
    ++count;
    if (surface) { ++nonnull; IDirectDrawSurface_Release(surface); }
    wsprintfA(text, "ITEM n=%u object=%u flags=%08lX width=%lu height=%lu pitch=%ld pf=%08lX bits=%lu caps=%08lX\r\n",
        count, !!surface, desc->dwFlags, desc->dwWidth, desc->dwHeight,
        desc->lPitch, desc->ddpfPixelFormat.dwFlags,
        desc->ddpfPixelFormat.dwRGBBitCount, desc->ddsCaps.dwCaps);
    DWORD written;
    WriteFile(output, text, lstrlenA(text), &written, NULL);
    return DDENUMRET_OK;
}
static void probe(IDirectDraw *root, DWORD flags, DDSURFACEDESC *filter,
        const char *name)
{
    count = nonnull = 0;
    HRESULT hr = IDirectDraw_EnumSurfaces(root, flags, filter, NULL, surface_cb);
    char text[200]; DWORD written;
    wsprintfA(text, "ENUM %s flags=%08lX hr=%08lX count=%u objects=%u\r\n",
        name, flags, (DWORD)hr, count, nonnull);
    WriteFile(output, text, lstrlenA(text), &written, NULL);
    FlushFileBuffers(output);
}
void WINAPI WinMainCRTStartup(void)
{
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, sizeof(path));
    UINT end = lstrlenA(path);
    while (end && path[end - 1] != '\\') --end;
    lstrcpyA(path + end, "ENUM.TXT");
    output = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (output == INVALID_HANDLE_VALUE) ExitProcess(1);
    HMODULE module = LoadLibraryA("C:\\WINDOWS\\system32\\ddraw.dll");
    HRESULT (WINAPI *create)(GUID *, IDirectDraw **, IUnknown *) =
        (void *)GetProcAddress(module, "DirectDrawCreate");
    IDirectDraw *root = NULL;
    if (!create || FAILED(create(NULL, &root, NULL))) ExitProcess(2);
    IDirectDraw_SetCooperativeLevel(root, NULL, DDSCL_NORMAL);
    DDSURFACEDESC desc;
    IDirectDrawSurface *surfaces[2] = {NULL, NULL};
    for (UINT i = 0; i < 2; ++i) {
        ZeroMemory(&desc, sizeof(desc)); desc.dwSize = sizeof(desc);
        desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
        desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
        desc.dwWidth = 32u >> i; desc.dwHeight = 32;
        if (FAILED(IDirectDraw_CreateSurface(root, &desc, &surfaces[i], NULL)))
            ExitProcess(3);
    }
    for (DWORD flags = 0; flags < 32; ++flags)
        probe(root, flags, &desc, "flags");
    probe(root, DDENUMSURFACES_DOESEXIST | DDENUMSURFACES_MATCH, NULL, "null-match");
    probe(root, DDENUMSURFACES_DOESEXIST, NULL, "null-default");
    desc.dwFlags = DDSD_CAPS;
    desc.ddsCaps.dwCaps = DDSCAPS_SYSTEMMEMORY;
    probe(root, DDENUMSURFACES_DOESEXIST | DDENUMSURFACES_MATCH, &desc, "caps-subset");
    desc.dwFlags = DDSD_WIDTH; desc.dwWidth = 16;
    probe(root, DDENUMSURFACES_DOESEXIST | DDENUMSURFACES_MATCH, &desc, "width");
    probe(root, DDENUMSURFACES_DOESEXIST | DDENUMSURFACES_NOMATCH, &desc, "not-width");
    ZeroMemory(&desc, sizeof(desc)); desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT;
    desc.dwWidth = desc.dwHeight = 32;
    desc.ddsCaps.dwCaps = DDSCAPS_OFFSCREENPLAIN | DDSCAPS_SYSTEMMEMORY;
    probe(root, DDENUMSURFACES_CANBECREATED | DDENUMSURFACES_MATCH, &desc, "create-one");
    desc.dwFlags &= ~DDSD_HEIGHT;
    probe(root, DDENUMSURFACES_CANBECREATED | DDENUMSURFACES_MATCH, &desc, "create-width");
    for (UINT i = 0; i < 2; ++i) IDirectDrawSurface_Release(surfaces[i]);
    IDirectDraw_Release(root); FreeLibrary(module);
    const char done[] = "PASS native surface enumeration reference complete\r\n";
    DWORD written; WriteFile(output, done, sizeof(done) - 1, &written, NULL);
    CloseHandle(output); ExitProcess(0);
}
