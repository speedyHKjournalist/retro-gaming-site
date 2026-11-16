/* SDK-level autogen queries, resource shape and filter state. No draw claim. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <d3d9.h>
#include "d3d_autogen_reference.h"
static HANDLE logfile;
static UINT observations, objects;
static void report(const char* text)
{
    DWORD written;
    WriteFile(logfile, text, lstrlenA(text), &written, NULL);
    WriteFile(logfile, "\r\n", 2, &written, NULL);
    FlushFileBuffers(logfile);
}
static void check(BOOL yes, const char* what)
{
    ++observations;
    if (!yes) {
        report(what);
        ExitProcess(1);
    }
}
static void filters(IDirect3DBaseTexture9* texture, BOOL fp32)
{
    BOOL linear = !fp32 || (D9WG_AUTOGEN_HOST_MASK & 1);
    D3DTEXTUREFILTERTYPE initial = linear ? D3DTEXF_LINEAR : D3DTEXF_POINT;
    check(IDirect3DBaseTexture9_GetAutoGenFilterType(texture) == initial, "FAIL default filter");
    check(IDirect3DBaseTexture9_SetAutoGenFilterType(texture, D3DTEXF_NONE) == D3DERR_INVALIDCALL, "FAIL NONE filter");
    check(IDirect3DBaseTexture9_GetAutoGenFilterType(texture) == initial, "FAIL invalid filter changed state");
    check(IDirect3DBaseTexture9_SetAutoGenFilterType(texture, D3DTEXF_POINT) == D3D_OK, "FAIL POINT setter");
    check(IDirect3DBaseTexture9_GetAutoGenFilterType(texture) == D3DTEXF_POINT, "FAIL POINT getter");
    IDirect3DBaseTexture9_GenerateMipSubLevels(texture);
    check(IDirect3DBaseTexture9_SetAutoGenFilterType(texture, D3DTEXF_LINEAR) == (linear ? D3D_OK : D3DERR_INVALIDCALL), "FAIL LINEAR setter");
    check(IDirect3DBaseTexture9_SetAutoGenFilterType(texture, D3DTEXF_ANISOTROPIC) == (linear ? D3D_OK : D3DERR_INVALIDCALL), "FAIL ANISOTROPIC setter");
    check(IDirect3DBaseTexture9_GetAutoGenFilterType(texture) == (linear ? D3DTEXF_ANISOTROPIC : D3DTEXF_POINT), "FAIL selected filter");
    check(IDirect3DBaseTexture9_SetAutoGenFilterType(texture, (D3DTEXTUREFILTERTYPE)0xffffffff) == D3DERR_INVALIDCALL, "FAIL invalid filter");
}
void WINAPI WinMainCRTStartup(void)
{
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, sizeof(path));
    UINT at = lstrlenA(path);
    while (at && path[at - 1] != '\\')
        --at;
    path[at] = 0;
    lstrcatA(path, "AUTO.TXT");
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    GetModuleFileNameA(NULL, path, sizeof(path));
    at = lstrlenA(path);
    while (at && path[at - 1] != '\\')
        --at;
    path[at] = 0;
    lstrcatA(path, "D3D9.DLL");
    HMODULE module = LoadLibraryA(path);
    check(module != NULL, "FAIL DLL");
    IDirect3D9*(WINAPI * make)(UINT) = (void*)(uintptr_t)GetProcAddress(module, "Direct3DCreate9");
    check(make != NULL, "FAIL export");
    IDirect3D9* factory = make(D3D_SDK_VERSION);
    check(factory != NULL, "FAIL factory");
    HWND window = CreateWindowExA(0, "STATIC", "Autogen SDK probe", WS_OVERLAPPEDWINDOW, 0, 0, 160, 120, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(window != NULL, "FAIL window");
    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = window;
    pp.BackBufferWidth = 64;
    pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    IDirect3DDevice9* device = NULL;
    check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device) == D3D_OK && device, "FAIL device");
    for (UINT i = 0; i < sizeof(autogen_reference) / sizeof(autogen_reference[0]); ++i) {
        const struct AutoGenReference* r = &autogen_reference[i];
        D3DFORMAT format = (D3DFORMAT)r->format;
        for (UINT cube = 0; cube < 2; ++cube) {
            D3DRESOURCETYPE type = cube ? D3DRTYPE_CUBETEXTURE : D3DRTYPE_TEXTURE;
            HRESULT hr = IDirect3D9_CheckDeviceFormat(factory, 0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_AUTOGENMIPMAP, type, format);
            if (hr != (r->generates ? D3D_OK : D3DOK_NOAUTOGEN)) {
                char line[160];
                wsprintfA(line, "FAIL autogen query format=%08lX cube=%u got=%08lX", r->format, cube, (DWORD)hr);
                report(line);
                ExitProcess(1);
            }
            ++observations;
            check(IDirect3D9_CheckDeviceFormat(factory, 0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, D3DUSAGE_AUTOGENMIPMAP | D3DUSAGE_DYNAMIC, type, format) == D3DERR_NOTAVAILABLE, "FAIL dynamic query");
            IDirect3DTexture9* texture = NULL;
            IDirect3DCubeTexture9* ct = NULL;
            hr = cube ? IDirect3DDevice9_CreateCubeTexture(device, 4, 0, D3DUSAGE_AUTOGENMIPMAP, format, D3DPOOL_MANAGED, &ct, NULL) : IDirect3DDevice9_CreateTexture(device, 4, 4, 0, D3DUSAGE_AUTOGENMIPMAP, format, D3DPOOL_MANAGED, &texture, NULL);
            check(hr == D3D_OK && (cube ? (BOOL)(ct != NULL) : (BOOL)(texture != NULL)), "FAIL create HRESULT");
            ++objects;
            IDirect3DBaseTexture9* base = cube ? (IDirect3DBaseTexture9*)ct : (IDirect3DBaseTexture9*)texture;
            check(IDirect3DBaseTexture9_GetLevelCount(base) == 1, "FAIL visible levels");
            D3DSURFACE_DESC desc;
            ZeroMemory(&desc, sizeof(desc));
            hr = cube ? IDirect3DCubeTexture9_GetLevelDesc(ct, 0, &desc) : IDirect3DTexture9_GetLevelDesc(texture, 0, &desc);
            check(hr == D3D_OK && desc.Usage == D3DUSAGE_AUTOGENMIPMAP && desc.Width == 4 && desc.Height == 4, "FAIL level description");
            hr = cube ? IDirect3DCubeTexture9_GetLevelDesc(ct, 1, &desc) : IDirect3DTexture9_GetLevelDesc(texture, 1, &desc);
            check(hr == D3DERR_INVALIDCALL, "FAIL hidden level description");
            IDirect3DSurface9* surface = (void*)(uintptr_t)1;
            hr = cube ? IDirect3DCubeTexture9_GetCubeMapSurface(ct, D3DCUBEMAP_FACE_POSITIVE_X, 1, &surface) : IDirect3DTexture9_GetSurfaceLevel(texture, 1, &surface);
            check(hr == D3DERR_INVALIDCALL && !surface, "FAIL hidden level surface");
            filters(base, r->float32);
            IDirect3DBaseTexture9_Release(base);
            for (UINT kind = 0; kind < 5; ++kind) {
                UINT levels = kind == 0 ? 2 : 1;
                D3DPOOL pool = kind == 1 ? D3DPOOL_SYSTEMMEM : kind == 2 ? D3DPOOL_SCRATCH
                                                                         : D3DPOOL_DEFAULT;
                DWORD usage = D3DUSAGE_AUTOGENMIPMAP | (kind == 3 ? D3DUSAGE_DYNAMIC : 0);
                texture = NULL;
                ct = NULL;
                hr = cube ? IDirect3DDevice9_CreateCubeTexture(device, 4, levels, usage, format, pool, &ct, NULL) : IDirect3DDevice9_CreateTexture(device, 4, 4, levels, usage, format, pool, &texture, NULL);
                check(hr == (kind == 4 ? D3D_OK : D3DERR_INVALIDCALL), "FAIL creation levels/pool/dynamic");
                if (kind == 4) {
                    ++objects;
                    IDirect3DBaseTexture9_Release(cube ? (IDirect3DBaseTexture9*)ct : (IDirect3DBaseTexture9*)texture);
                } else
                    check(!texture && !ct, "FAIL invalid create output");
            }
            texture = NULL;
            ct = NULL;
            hr = cube ? IDirect3DDevice9_CreateCubeTexture(device, 4, 1, 0, format, D3DPOOL_MANAGED, &ct, NULL) : IDirect3DDevice9_CreateTexture(device, 4, 4, 1, 0, format, D3DPOOL_MANAGED, &texture, NULL);
            check(hr == D3D_OK, "FAIL plain texture");
            ++objects;
            base = cube ? (IDirect3DBaseTexture9*)ct : (IDirect3DBaseTexture9*)texture;
            check(IDirect3DBaseTexture9_GetAutoGenFilterType(base) == D3DTEXF_NONE, "FAIL plain filter getter");
            check(IDirect3DBaseTexture9_SetAutoGenFilterType(base, D3DTEXF_POINT) == D3DERR_INVALIDCALL, "FAIL plain filter setter");
            IDirect3DBaseTexture9_GenerateMipSubLevels(base);
            IDirect3DBaseTexture9_Release(base);
        }
    }
    IDirect3DVolumeTexture9* volume = (void*)(uintptr_t)1;
    check(IDirect3DDevice9_CreateVolumeTexture(device, 4, 4, 4, 0, D3DUSAGE_AUTOGENMIPMAP, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &volume, NULL) == D3DERR_INVALIDCALL && !volume, "FAIL volume autogen");
    check(IDirect3DDevice9_CreateVolumeTexture(device, 4, 4, 4, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &volume, NULL) == D3D_OK, "FAIL plain volume");
    ++objects;
    check(IDirect3DVolumeTexture9_GetAutoGenFilterType(volume) == D3DTEXF_NONE, "FAIL volume filter getter");
    IDirect3DVolumeTexture9_Release(volume);
    for (UINT cube = 0; cube < 2; ++cube) {
        IDirect3DTexture9* texture = NULL;
        IDirect3DCubeTexture9* ct = NULL;
        HRESULT hr = cube ? IDirect3DDevice9_CreateCubeTexture(device, 4, 1, D3DUSAGE_AUTOGENMIPMAP, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &ct, NULL) : IDirect3DDevice9_CreateTexture(device, 4, 4, 1, D3DUSAGE_AUTOGENMIPMAP, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, NULL);
        check(hr == D3D_OK, "FAIL reset texture");
        ++objects;
        IDirect3DBaseTexture9* base = cube ? (IDirect3DBaseTexture9*)ct : (IDirect3DBaseTexture9*)texture;
        check(IDirect3DBaseTexture9_SetAutoGenFilterType(base, D3DTEXF_ANISOTROPIC) == D3D_OK, "FAIL reset filter");
        check(IDirect3DDevice9_Reset(device, &pp) == D3D_OK, "FAIL reset");
        check(IDirect3DBaseTexture9_GetAutoGenFilterType(base) == D3DTEXF_ANISOTROPIC, "FAIL retained filter");
        IDirect3DBaseTexture9_Release(base);
    }
    IDirect3DDevice9_Release(device);
    IDirect3D9_Release(factory);
    FreeLibrary(module);
    DestroyWindow(window);
    char line[180];
    wsprintfA(line, "PASS autogen SDK: simulatedHost=%u formats=48 objects=%u observations=%u; queries, fallback, filters, invalid creation, volume and Reset", D9WG_AUTOGEN_HOST_MASK, objects, observations);
    report(line);
    CloseHandle(logfile);
    ExitProcess(0);
}
