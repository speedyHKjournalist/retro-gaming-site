/* GDI's DirectDraw memory-surface DC has native DIBSECTION metadata and a
 * top-down pixel mapping. Ordinary CreateDIBSection has different metadata on
 * XP. Use the installed OS runtime only for this CPU/GDI surface; no Direct3D
 * device, draw, or presentation is delegated to it. */
typedef struct D9GdiMemory {
    HMODULE module;
    IDirectDraw *draw;
    IDirectDrawSurface *surface;
    HDC dc;
    UINT width, height, bytes;
} D9GdiMemory;

static HRESULT gdi_memory_close(D9GdiMemory *memory, BYTE *destination, UINT pitch)
{
    HRESULT result = D3D_OK;
    if (!memory) return result;
    if (memory->dc) {
        GdiFlush();
        result = IDirectDrawSurface_ReleaseDC(memory->surface, memory->dc);
        memory->dc = NULL;
    }
    if (destination && SUCCEEDED(result)) {
        DDSURFACEDESC map;
        ZeroMemory(&map, sizeof(map));
        map.dwSize = sizeof(map);
        result = IDirectDrawSurface_Lock(memory->surface, NULL, &map,
                DDLOCK_WAIT | DDLOCK_READONLY, NULL);
        if (SUCCEEDED(result)) {
            for (UINT y = 0; y < memory->height; ++y)
                CopyMemory(destination + y * pitch,
                        (BYTE *)map.lpSurface + y * map.lPitch,
                        memory->width * memory->bytes);
            result = IDirectDrawSurface_Unlock(memory->surface, map.lpSurface);
        }
    }
    if (memory->surface) IDirectDrawSurface_Release(memory->surface);
    if (memory->draw) IDirectDraw_Release(memory->draw);
    if (memory->module) FreeLibrary(memory->module);
    HeapFree(GetProcessHeap(), 0, memory);
    return result;
}

static HRESULT gdi_memory_open(UINT width, UINT height, D3DFORMAT format,
        const BYTE *source, UINT pitch, HMODULE self, D9GdiMemory **out)
{
    typedef HRESULT (WINAPI *CreateDraw)(GUID *, IDirectDraw **, IUnknown *);
    union { FARPROC generic; CreateDraw create; } entry;
    WCHAR path[MAX_PATH];
    static const WCHAR suffix[] = L"\\ddraw.dll";
    UINT length = GetSystemDirectoryW(path, MAX_PATH);
    HRESULT hr = D3DERR_NOTAVAILABLE;
    if (!length || length >= MAX_PATH
            || length > MAX_PATH - sizeof(suffix) / sizeof(*suffix)) return hr;
    D9GdiMemory *memory = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*memory));
    if (!memory) return E_OUTOFMEMORY;
    CopyMemory(path + length, suffix, sizeof(suffix));
    memory->module = LoadLibraryW(path);
    if (!memory->module || memory->module == self) goto failed;
    entry.generic = GetProcAddress(memory->module, "DirectDrawCreate");
    if (!entry.create) goto failed;
    hr = entry.create(NULL, &memory->draw, NULL);
    if (FAILED(hr)) goto failed;
    hr = IDirectDraw_SetCooperativeLevel(memory->draw, NULL, DDSCL_NORMAL);
    if (FAILED(hr)) goto failed;
    DDSURFACEDESC desc;
    ZeroMemory(&desc, sizeof(desc));
    desc.dwSize = sizeof(desc);
    desc.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
    desc.dwWidth = width;
    desc.dwHeight = height;
    desc.ddsCaps.dwCaps = DDSCAPS_SYSTEMMEMORY | DDSCAPS_OFFSCREENPLAIN;
    desc.ddpfPixelFormat.dwSize = sizeof(desc.ddpfPixelFormat);
    desc.ddpfPixelFormat.dwFlags = DDPF_RGB;
    BOOL sixteen = format == D3DFMT_R5G6B5 || format == D3DFMT_X1R5G5B5
            || format == D3DFMT_A1R5G5B5;
    memory->bytes = sixteen ? 2u : format == D3DFMT_R8G8B8 ? 3u : 4u;
    memory->width = width;
    memory->height = height;
    desc.ddpfPixelFormat.dwRGBBitCount = memory->bytes * 8u;
    desc.ddpfPixelFormat.dwRBitMask = sixteen ? (format == D3DFMT_R5G6B5 ? 0xf800u : 0x7c00u) : 0xff0000u;
    desc.ddpfPixelFormat.dwGBitMask = sixteen ? (format == D3DFMT_R5G6B5 ? 0x07e0u : 0x03e0u) : 0x00ff00u;
    desc.ddpfPixelFormat.dwBBitMask = sixteen ? 0x001fu : 0x0000ffu;
    if (format == D3DFMT_A8R8G8B8 || format == D3DFMT_A1R5G5B5) {
        desc.ddpfPixelFormat.dwFlags |= DDPF_ALPHAPIXELS;
        desc.ddpfPixelFormat.dwRGBAlphaBitMask = sixteen ? 0x8000u : 0xff000000u;
    }
    hr = IDirectDraw_CreateSurface(memory->draw, &desc, &memory->surface, NULL);
    if (FAILED(hr)) goto failed;
    DDSURFACEDESC map;
    ZeroMemory(&map, sizeof(map));
    map.dwSize = sizeof(map);
    hr = IDirectDrawSurface_Lock(memory->surface, NULL, &map, DDLOCK_WAIT, NULL);
    if (FAILED(hr)) goto failed;
    for (UINT y = 0; y < height; ++y)
        CopyMemory((BYTE *)map.lpSurface + y * map.lPitch, source + y * pitch,
                width * memory->bytes);
    hr = IDirectDrawSurface_Unlock(memory->surface, map.lpSurface);
    if (FAILED(hr)) goto failed;
    hr = IDirectDrawSurface_GetDC(memory->surface, &memory->dc);
    if (FAILED(hr)) goto failed;
    *out = memory;
    return D3D_OK;
failed:
    gdi_memory_close(memory, NULL, 0);
    return hr;
}
