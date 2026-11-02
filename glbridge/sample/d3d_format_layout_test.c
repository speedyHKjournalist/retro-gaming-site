/* Exact SDK texture pitch, mip storage and read-only locks, no rendering. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#if D9WG_FORMAT_API == 8
#include <d3d8.h>
typedef IDirect3D8 Factory;
typedef IDirect3DDevice8 Device;
typedef IDirect3DTexture8 Texture;
#define CALL(n) IDirect3DDevice8_##n
#define FACT(n) IDirect3D8_##n
#define TEX(n) IDirect3DTexture8_##n
#else
#include <d3d9.h>
typedef IDirect3D9 Factory;
typedef IDirect3DDevice9 Device;
typedef IDirect3DTexture9 Texture;
#define CALL(n) IDirect3DDevice9_##n
#define FACT(n) IDirect3D9_##n
#define TEX(n) IDirect3DTexture9_##n
#endif
#include "d3d_format_layout_reference.h"
static HANDLE logfile;
static unsigned textures, levels, compared_bytes;
static void report(const char* text)
{
    DWORD written;
    WriteFile(logfile, text, lstrlenA(text), &written, NULL);
    WriteFile(logfile, "\r\n", 2, &written, NULL);
    FlushFileBuffers(logfile);
}
static void check(BOOL yes, const char* text)
{
    if (!yes) {
        report(text);
        ExitProcess(1);
    }
}
#define OK(x) check(SUCCEEDED(x), "FAIL " #x)
void WINAPI WinMainCRTStartup(void)
{
    char dir[MAX_PATH], path[MAX_PATH], name[32];
    GetModuleFileNameA(NULL, dir, sizeof(dir));
    unsigned at = lstrlenA(dir);
    while (at && dir[at - 1] != '\\')
        --at;
    dir[at] = 0;
    lstrcpyA(path, dir);
    lstrcatA(path, "FORMAT.TXT");
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    lstrcpyA(path, dir);
    wsprintfA(name, "D3D%u.DLL", D9WG_FORMAT_API);
    lstrcatA(path, name);
    HMODULE module = LoadLibraryA(path);
    check(module != NULL, "FAIL DLL");
    wsprintfA(name, "Direct3DCreate%u", D9WG_FORMAT_API);
    Factory*(WINAPI * make)(UINT) = (void*)(uintptr_t)GetProcAddress(module, name);
    check(make != NULL, "FAIL export");
    Factory* factory = make(D3D_SDK_VERSION);
    check(factory != NULL, "FAIL factory");
    HWND window = CreateWindowExA(0, "STATIC", "Format layouts", WS_OVERLAPPEDWINDOW, 0, 0, 96, 96, NULL, NULL, GetModuleHandleA(NULL), NULL);
    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.BackBufferWidth = pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.Windowed = TRUE;
    pp.hDeviceWindow = window;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    Device* device = NULL;
    OK(FACT(CreateDevice)(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device));
    for (UINT i = 0; i < sizeof(format_layout_reference) / sizeof(format_layout_reference[0]); ++i) {
        const struct FormatLayoutReference* r = &format_layout_reference[i];
        if (!(r->apis & (1u << (D9WG_FORMAT_API - 7))))
            continue;
        OK(FACT(CheckDeviceFormat)(factory, 0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, 0, D3DRTYPE_TEXTURE, (D3DFORMAT)r->format));
        Texture* texture = NULL;
#if D9WG_FORMAT_API == 8
        OK(CALL(CreateTexture)(device, 8, 8, 4, 0, (D3DFORMAT)r->format, D3DPOOL_MANAGED, &texture));
#else
        OK(CALL(CreateTexture)(device, 8, 8, 4, 0, (D3DFORMAT)r->format, D3DPOOL_MANAGED, &texture, NULL));
#endif
        check(TEX(GetLevelCount)(texture) == 4, "FAIL mip count");
        for (UINT level = 0; level < 4; ++level) {
            D3DSURFACE_DESC desc;
            D3DLOCKED_RECT locked;
            UINT size = 8u >> level;
            UINT pitch = ((size + r->width - 1) / r->width) * r->bytes;
            UINT rows = (size + r->height - 1) / r->height;
            OK(TEX(GetLevelDesc)(texture, level, &desc));
            check(desc.Width == size && desc.Height == size && (DWORD)desc.Format == r->format, "FAIL mip descriptor");
            OK(TEX(LockRect)(texture, level, &locked, NULL, 0));
            check(locked.Pitch == (INT)pitch && locked.pBits != NULL, "FAIL mip pitch");
            for (UINT row = 0; row < rows; ++row)
                for (UINT col = 0; col < pitch; ++col)
                    ((BYTE*)locked.pBits)[row * pitch + col] = (BYTE)(D9WG_FORMAT_API + (r->format & 255) + level * 17 + row * 11 + col * 3);
            OK(TEX(UnlockRect)(texture, level));
            OK(TEX(LockRect)(texture, level, &locked, NULL, D3DLOCK_READONLY));
            check(locked.Pitch == (INT)pitch, "FAIL readonly pitch");
            for (UINT row = 0; row < rows; ++row)
                for (UINT col = 0; col < pitch; ++col) {
                    check(((const BYTE*)locked.pBits)[row * pitch + col] == (BYTE)(D9WG_FORMAT_API + (r->format & 255) + level * 17 + row * 11 + col * 3), "FAIL readonly storage byte");
                    ++compared_bytes;
                }
            OK(TEX(UnlockRect)(texture, level));
            ++levels;
        }
        TEX(Release)(texture);
        ++textures;
    }
    CALL(Release)(device);
    FACT(Release)(factory);
    FreeLibrary(module);
    DestroyWindow(window);
    char message[160];
    wsprintfA(message, "PASS D3D%u format layouts: textures=%u levels=%u exactBytes=%u; SDK pitch/descriptors/readonly storage", D9WG_FORMAT_API, textures, levels, compared_bytes);
    report(message);
    CloseHandle(logfile);
    ExitProcess(0);
}
