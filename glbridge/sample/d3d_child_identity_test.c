/* Contained child COM identity and shared parent count probe. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#if D9WG_IDENTITY_API == 8
#include <d3d8.h>
typedef IDirect3D8 Factory;
typedef IDirect3DDevice8 Device;
typedef IDirect3DSwapChain8 Chain;
#define CHAIN(method) IDirect3DSwapChain8_##method
#define DEV(method) IDirect3DDevice8_##method
#define FACTORY_RELEASE IDirect3D8_Release
#else
#include <d3d9.h>
typedef IDirect3D9 Factory;
typedef IDirect3DDevice9 Device;
typedef IDirect3DSwapChain9 Chain;
#define CHAIN(method) IDirect3DSwapChain9_##method
#define DEV(method) IDirect3DDevice9_##method
#define FACTORY_RELEASE IDirect3D9_Release
#endif
static Factory* factory;
static Device* device;
static HANDLE logfile;
static void report(const char* text)
{
    DWORD written;
    WriteFile(logfile, text, lstrlenA(text), &written, NULL);
    WriteFile(logfile, "\r\n", 2, &written, NULL);
    FlushFileBuffers(logfile);
}
static void check(BOOL condition, const char* message)
{
    if (!condition) {
        report(message);
        ExitProcess(1);
    }
}

#if D9WG_IDENTITY_API == 8
typedef IDirect3DTexture8 Texture;
typedef IDirect3DCubeTexture8 Cube;
typedef IDirect3DVolumeTexture8 VolumeTexture;
typedef IDirect3DSurface8 Surface;
typedef IDirect3DVolume8 Volume;
#define TEX(m) IDirect3DTexture8_##m
#define CUBE(m) IDirect3DCubeTexture8_##m
#define VOLUME_TEX(m) IDirect3DVolumeTexture8_##m
#define SURFACE(m) IDirect3DSurface8_##m
#define VOLUME(m) IDirect3DVolume8_##m
#else
typedef IDirect3DTexture9 Texture;
typedef IDirect3DCubeTexture9 Cube;
typedef IDirect3DVolumeTexture9 VolumeTexture;
typedef IDirect3DSurface9 Surface;
typedef IDirect3DVolume9 Volume;
#define TEX(m) IDirect3DTexture9_##m
#define CUBE(m) IDirect3DCubeTexture9_##m
#define VOLUME_TEX(m) IDirect3DVolumeTexture9_##m
#define SURFACE(m) IDirect3DSurface9_##m
#define VOLUME(m) IDirect3DVolume9_##m
#endif
static const GUID unknown_id = { 0, 0, 0, { 0xc0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID key = { 0x10293847, 1, 2, { 3, 4, 5, 6, 7, 8, 9, 10 } };
static HRESULT get_child(IUnknown* parent, UINT kind, UINT index, IUnknown** out)
{
    if (!kind)
        return TEX(GetSurfaceLevel)((Texture*)parent, index, (Surface**)out);
    if (kind == 1)
        return CUBE(GetCubeMapSurface)((Cube*)parent, (D3DCUBEMAP_FACES)(index / 2), index % 2, (Surface**)out);
    return VOLUME_TEX(GetVolumeLevel)((VolumeTexture*)parent, index, (Volume**)out);
}
static ULONG refs(IUnknown* object)
{
    IUnknown_AddRef(object);
    return IUnknown_Release(object);
}
static void identity_cases(void)
{
    for (UINT kind = 0; kind < 3; kind++) {
        IUnknown* parent = NULL;
        HRESULT hr;
        if (!kind)
            hr = DEV(CreateTexture)(device, 4, 4, 2, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, (Texture**)&parent
#if D9WG_IDENTITY_API == 9
                ,
                NULL
#endif
            );
        else if (kind == 1)
            hr = DEV(CreateCubeTexture)(device, 4, 2, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, (Cube**)&parent
#if D9WG_IDENTITY_API == 9
                ,
                NULL
#endif
            );
        else
            hr = DEV(CreateVolumeTexture)(device, 4, 4, 4, 2, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, (VolumeTexture**)&parent
#if D9WG_IDENTITY_API == 9
                ,
                NULL
#endif
            );
        check(hr == D3D_OK && parent && refs(parent) == 1, "FAIL identity parent create");
        UINT count = kind == 1 ? 12 : 2;
        IUnknown* saved[12] = { NULL };
        for (UINT i = 0; i < count; i++) {
            IUnknown *a = NULL, *b = NULL, *q = NULL, *container = NULL;
            check(get_child(parent, kind, i, &a) == D3D_OK && a && refs(parent) == 2 && refs(a) == 2, "FAIL first child shared refs");
            saved[i] = a;
            check(get_child(parent, kind, i, &b) == D3D_OK && a == b && refs(parent) == 3 && refs(a) == 3, "FAIL live child identity/count");
            check(IUnknown_AddRef(a) == 4 && refs(parent) == 4, "FAIL child AddRef forwarding");
            check(IUnknown_Release(b) == 3 && IUnknown_Release(a) == 2, "FAIL child Release forwarding");
            check(IUnknown_QueryInterface(a, &unknown_id, (void**)&q) == S_OK && q == a && refs(parent) == 3 && IUnknown_Release(q) == 2, "FAIL child IUnknown identity");
            hr = kind == 2 ? VOLUME(GetContainer)((Volume*)a, &unknown_id, (void**)&container) : SURFACE(GetContainer)((Surface*)a, &unknown_id, (void**)&container);
            check(hr == D3D_OK && container == parent && refs(parent) == 3 && IUnknown_Release(container) == 2, "FAIL child container identity");
            DWORD value = 0x50607000u + kind * 0x100u + i, size = 4, out = 0;
            hr = kind == 2 ? VOLUME(SetPrivateData)((Volume*)a, &key, &value, 4, 0) : SURFACE(SetPrivateData)((Surface*)a, &key, &value, 4, 0);
            check(hr == D3D_OK, "FAIL child data");
            check(IUnknown_Release(a) == 1 && refs(parent) == 1, "FAIL cached child final public ref");
            check(get_child(parent, kind, i, &a) == D3D_OK && a == saved[i] && refs(a) == 2, "FAIL reacquired identity");
            hr = kind == 2 ? VOLUME(GetPrivateData)((Volume*)a, &key, &out, &size) : SURFACE(GetPrivateData)((Surface*)a, &key, &out, &size);
            check(hr == D3D_OK && size == 4 && out == value, "FAIL cached private data");
            check(IUnknown_Release(a) == 1, "FAIL reacquired shared count");
            for (UINT j = 0; j < i; j++)
                check(saved[i] != saved[j], "FAIL different child identity aliases");
        }
        IUnknown* child = NULL;
        check(get_child(parent, kind, 0, &child) == D3D_OK && child == saved[0] && refs(child) == 2, "FAIL final child acquire");
        check(IUnknown_Release(parent) == 1 && IUnknown_AddRef(child) == 2 && IUnknown_Release(child) == 1 && IUnknown_Release(child) == 0, "FAIL final child parent teardown");
        char line[120];
        wsprintfA(line, "IDENTITY %u kind=%u views=%u shared=1 live=1 reacquired=1 unknown=1 container=1 final=0", D9WG_IDENTITY_API, kind, count);
        report(line);
    }
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], path[MAX_PATH], name[100];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    UINT at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    wsprintfA(name, "ID%u.TXT", D9WG_IDENTITY_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    RECT client = { 0, 0, 64, 64 };
    check(AdjustWindowRectEx(&client, WS_POPUP, FALSE, 0), "FAIL window dimensions");
    HWND window = CreateWindowExA(0, "STATIC", "D3D contained COM identity probe", WS_POPUP, 30, 40, client.right - client.left, client.bottom - client.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(window != NULL, "FAIL window");
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    RECT actual;
    check(GetClientRect(window, &actual) && actual.right == 64 && actual.bottom == 64, "FAIL exact client dimensions");
    wsprintfA(name, "D3D%u.DLL", D9WG_IDENTITY_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    HMODULE module = LoadLibraryA(path);
    check(module != NULL, "FAIL DLL");
    wsprintfA(name, "Direct3DCreate%u", D9WG_IDENTITY_API);
    Factory*(WINAPI * make)(UINT) = (void*)(uintptr_t)GetProcAddress(module, name);
    check(make != NULL, "FAIL factory export");
    factory = make(D3D_SDK_VERSION);
    check(factory != NULL, "FAIL factory");
    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window;
    pp.BackBufferWidth = 64;
    pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
#if D9WG_IDENTITY_API == 8
    check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device8");
#else
    check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device9");
#endif
    identity_cases();
    check(DEV(Release)(device) == 0, "FAIL device references");
    FACTORY_RELEASE(factory);
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS identity API=%u", D9WG_IDENTITY_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
