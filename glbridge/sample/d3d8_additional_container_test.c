/* Additional backbuffers expose the D3D8 device, not their private texture. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <initguid.h>
#include <d3d8.h>
#include <stdint.h>
static IDirect3D8* factory;
static IDirect3DDevice8* device;
static HANDLE logfile;
static const GUID unknown_id = { 0, 0, 0, { 0xc0, 0, 0, 0, 0, 0, 0, 0x46 } };
static const GUID private_key = { 0x72638495, 1, 2, { 3, 4, 5, 6, 7, 8, 9, 10 } };
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
static ULONG refs(IUnknown* object)
{
    IUnknown_AddRef(object);
    return IUnknown_Release(object);
}
static void device_container(IDirect3DSurface8* surface, UINT phase)
{
    const GUID* valid[] = { &unknown_id, &IID_IDirect3DDevice8 };
    const GUID* refused[] = { &IID_IDirect3DResource8, &IID_IDirect3DBaseTexture8,
        &IID_IDirect3DTexture8, &IID_IDirect3DCubeTexture8, &IID_IDirect3DSurface8,
        &IID_IDirect3DSwapChain8, NULL };
    ULONG before = refs((IUnknown*)device);
    for (UINT i = 0; i < sizeof(valid) / sizeof(valid[0]); i++) {
        IUnknown* out = NULL;
        check(IDirect3DSurface8_GetContainer(surface, valid[i], (void**)&out) == D3D_OK
                && out == (IUnknown*)device && refs((IUnknown*)device) == before + 1,
            "FAIL exact device container/reference");
        IUnknown_Release(out);
        check(refs((IUnknown*)device) == before, "FAIL leaked container reference");
    }
    for (UINT i = 0; i < sizeof(refused) / sizeof(refused[0]); i++) {
        void* out = (void*)(uintptr_t)0xdeadbeef;
        check(IDirect3DSurface8_GetContainer(surface, refused[i], &out) == E_NOINTERFACE
                && !out && refs((IUnknown*)device) == before,
            "FAIL unsupported container/output");
    }
    check(IDirect3DSurface8_GetContainer(surface, &IID_IDirect3DDevice8, NULL) == E_POINTER
            && refs((IUnknown*)device) == before,
        "FAIL NULL container output");
    char line[140];
    wsprintfA(line, "CONTAINER8 phase=%u device=2 refused=7 null=1 exactRef=1", phase);
    report(line);
}
static void ordinary_containers(void)
{
    IDirect3DTexture8* texture = NULL;
    IDirect3DCubeTexture8* cube = NULL;
    IDirect3DSurface8* surface = NULL;
    IUnknown* out = NULL;
    check(IDirect3DDevice8_CreateTexture(device, 4, 4, 1, 0, D3DFMT_A8R8G8B8,
              D3DPOOL_MANAGED, &texture) == D3D_OK,
        "FAIL ordinary texture");
    check(IDirect3DTexture8_GetSurfaceLevel(texture, 0, &surface) == D3D_OK, "FAIL ordinary texture level");
    ULONG before = refs((IUnknown*)texture);
    check(IDirect3DSurface8_GetContainer(surface, &IID_IDirect3DTexture8, (void**)&out) == D3D_OK
            && out == (IUnknown*)texture && refs((IUnknown*)texture) == before + 1,
        "FAIL ordinary texture container");
    IUnknown_Release(out);
    out = (void*)(uintptr_t)0xdeadbeef;
    check(IDirect3DSurface8_GetContainer(surface, &IID_IDirect3DDevice8, (void**)&out) == E_NOINTERFACE
            && !out && refs((IUnknown*)texture) == before,
        "FAIL ordinary texture became device container");
    IDirect3DSurface8_Release(surface);
    check(IDirect3DTexture8_Release(texture) == 0, "FAIL ordinary texture references");
    check(IDirect3DDevice8_CreateCubeTexture(device, 4, 1, 0, D3DFMT_A8R8G8B8,
              D3DPOOL_MANAGED, &cube) == D3D_OK,
        "FAIL ordinary cube");
    check(IDirect3DCubeTexture8_GetCubeMapSurface(cube, D3DCUBEMAP_FACE_NEGATIVE_Z, 0, &surface) == D3D_OK, "FAIL ordinary cube face");
    before = refs((IUnknown*)cube);
    check(IDirect3DSurface8_GetContainer(surface, &IID_IDirect3DCubeTexture8, (void**)&out) == D3D_OK
            && out == (IUnknown*)cube && refs((IUnknown*)cube) == before + 1,
        "FAIL ordinary cube container");
    IUnknown_Release(out);
    out = (void*)(uintptr_t)0xdeadbeef;
    check(IDirect3DSurface8_GetContainer(surface, &IID_IDirect3DDevice8, (void**)&out) == E_NOINTERFACE
            && !out && refs((IUnknown*)cube) == before,
        "FAIL ordinary cube became device container");
    IDirect3DSurface8_Release(surface);
    check(IDirect3DCubeTexture8_Release(cube) == 0, "FAIL ordinary cube references");
    report("CONTAINER8 TEXTURE kinds=2 valid=2 rejectedDevice=2 exactRef=1");
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], path[MAX_PATH];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    UINT at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    lstrcpyA(path, directory);
    lstrcatA(path, "CNT8.TXT");
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    HWND window = CreateWindowExA(0, "STATIC", "D3D8 container probe", WS_POPUP,
        30, 40, 64, 64, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(window != NULL, "FAIL window");
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    lstrcpyA(path, directory);
    lstrcatA(path, "D3D8.DLL");
    HMODULE module = LoadLibraryA(path);
    check(module != NULL, "FAIL DLL");
    IDirect3D8*(WINAPI * make)(UINT) = (void*)(uintptr_t)GetProcAddress(module, "Direct3DCreate8");
    check(make != NULL, "FAIL factory export");
    factory = make(D3D_SDK_VERSION);
    check(factory != NULL, "FAIL factory");
    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window,
              D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK,
        "FAIL device");
    check(refs((IUnknown*)factory) == 2, "FAIL initial factory reference");
    D3DPRESENT_PARAMETERS extra = pp;
    extra.BackBufferWidth = 32;
    extra.BackBufferHeight = 24;
    IDirect3DSwapChain8* chain = NULL;
    IDirect3DSurface8 *surface = NULL, *again = NULL, *primary = NULL;
    check(IDirect3DDevice8_CreateAdditionalSwapChain(device, &extra, &chain) == D3D_OK && chain, "FAIL additional chain");
    check(IDirect3DSwapChain8_GetBackBuffer(chain, 0, D3DBACKBUFFER_TYPE_MONO, &surface) == D3D_OK
            && IDirect3DSwapChain8_GetBackBuffer(chain, 0, D3DBACKBUFFER_TYPE_LEFT, &again) == D3D_OK
            && surface == again,
        "FAIL cached additional buffer");
    IDirect3DSurface8_Release(again);
    DWORD marker = 0x10293847, value = 0, size = 4;
    check(IDirect3DSurface8_SetPrivateData(surface, &private_key, &marker, 4, 0) == D3D_OK, "FAIL private marker");
    device_container(surface, 0);
    check(IDirect3DSwapChain8_Release(chain) == 0, "FAIL chain release");
    device_container(surface, 1);
    check(IDirect3DSurface8_GetPrivateData(surface, &private_key, &value, &size) == D3D_OK && value == marker, "FAIL retained private marker");
    IDirect3DSurface8_Release(surface);
    check(IDirect3DDevice8_GetBackBuffer(device, 0, D3DBACKBUFFER_TYPE_MONO, &primary) == D3D_OK, "FAIL primary buffer");
    device_container(primary, 2);
    check(IDirect3DSurface8_Release(primary) == 0, "FAIL primary public reference");
    ordinary_containers();
    check(IDirect3DDevice8_TestCooperativeLevel(device) == D3D_OK && refs((IUnknown*)device) == 1, "FAIL final device reference/status");
    check(IDirect3DDevice8_Release(device) == 0 && refs((IUnknown*)factory) == 1, "FAIL final device cleanup");
    check(IDirect3D8_Release(factory) == 0, "FAIL final factory cleanup");
    FreeLibrary(module);
    DestroyWindow(window);
    report("CONTAINER8 LIFETIME cached=1 chainReleasedFirst=1 privateRetained=1 deviceRefs=0 factoryRefs=0");
    report("PASS additional container API=8");
    CloseHandle(logfile);
    ExitProcess(0);
}
