/* Public SDK capability queries with explicitly simulated host feature masks. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <d3d9.h>

#include "d3d_format_query_reference.h"
static HANDLE logfile;
static UINT queries;
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
static BOOL expected_query(const struct FormatQueryReference* r, UINT type, UINT profile, DWORD usage)
{
    if ((usage & D3DUSAGE_SOFTWAREPROCESSING) || (r->bc && !(D9WG_QUERY_HOST_MASK & 4)))
        return FALSE;
    BOOL texture = type == D3DRTYPE_TEXTURE;
    BOOL output = r->target;
    DWORD allowed = 0;
    if (texture && (r->color || r->depth) && (!r->float32 || (D9WG_QUERY_HOST_MASK & 1)))
        allowed |= D3DUSAGE_QUERY_FILTER;
    if (texture && r->srgb)
        allowed |= D3DUSAGE_QUERY_SRGBREAD;
    if (output && r->srgb)
        allowed |= D3DUSAGE_QUERY_SRGBWRITE;
    if (output && r->color && (!r->float32 || (D9WG_QUERY_HOST_MASK & 2)))
        allowed |= D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING;
    if (texture && profile == 3 && r->color)
        allowed |= D3DUSAGE_QUERY_VERTEXTEXTURE;
    if (texture && r->color)
        allowed |= D3DUSAGE_QUERY_WRAPANDMIP;
    DWORD requested = usage & 0x3f8000u;
    return !(requested & ~allowed);
}
void WINAPI WinMainCRTStartup(void)
{
    char dir[MAX_PATH], path[MAX_PATH];
    GetModuleFileNameA(NULL, dir, sizeof(dir));
    unsigned at = lstrlenA(dir);
    while (at && dir[at - 1] != '\\')
        --at;
    dir[at] = 0;
    lstrcpyA(path, dir);
    lstrcatA(path, "QUERY.TXT");
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    lstrcpyA(path, dir);
    lstrcatA(path, "D3D9.DLL");
    HMODULE module = LoadLibraryA(path);
    check(module != NULL, "FAIL DLL");
    IDirect3D9*(WINAPI * make)(UINT) = (void*)(uintptr_t)GetProcAddress(module, "Direct3DCreate9");
    check(make != NULL, "FAIL factory export");
    IDirect3D9* factory = make(D3D_SDK_VERSION);
    check(factory != NULL, "FAIL factory");
    D3DCAPS9 caps;
    check(SUCCEEDED(IDirect3D9_GetDeviceCaps(factory, 0, D3DDEVTYPE_HAL, &caps)), "FAIL caps");
    UINT profile = (caps.VertexShaderVersion >> 8) & 255;
    char selected[32];
    DWORD selected_length = GetEnvironmentVariableA("D9WG_CAPS_PROFILE", selected, sizeof(selected));
    UINT wanted_profile = selected_length && lstrcmpA(selected, "ffp") == 0 ? 0 : selected_length && lstrcmpA(selected, "sm2") == 0 ? 2
                                                                                                                                    : 3;
    check(profile == wanted_profile, "FAIL selected profile");
    const DWORD sdk_query_bits[] = { D3DUSAGE_QUERY_LEGACYBUMPMAP, D3DUSAGE_QUERY_SRGBREAD,
        D3DUSAGE_QUERY_FILTER, D3DUSAGE_QUERY_SRGBWRITE, D3DUSAGE_QUERY_POSTPIXELSHADER_BLENDING,
        D3DUSAGE_QUERY_VERTEXTEXTURE, D3DUSAGE_QUERY_WRAPANDMIP };
    for (UINT i = 0; i < 7; ++i)
        check(sdk_query_bits[i] == query_bit_reference[i], "FAIL SDK query flag reference");
    const DWORD requests[] = { 0, 0x8000, 0x10000, 0x20000, 0x40000, 0x80000, 0x100000, 0x200000,
        0xa0000, 0x30000, 0xc0000, 0x120000, 0x220000, 0x50000, 0x3f8000, 0x10 };
    const UINT types[] = { D3DRTYPE_SURFACE, D3DRTYPE_TEXTURE };
    for (UINT i = 0; i < sizeof(format_query_reference) / sizeof(format_query_reference[0]); ++i) {
        const struct FormatQueryReference* r = &format_query_reference[i];
        for (UINT t = 0; t < 2; ++t)
            for (UINT q = 0; q < sizeof(requests) / sizeof(requests[0]); ++q) {
                DWORD creation = r->depth || r->raw ? D3DUSAGE_DEPTHSTENCIL : r->format == 0x4c4c554e ? D3DUSAGE_RENDERTARGET
                                                                                                      : 0;
                DWORD usage = creation | requests[q];
                BOOL exists = r->color || r->depth || r->raw || r->format == 0x4c4c554e;
                BOOL expected = exists && expected_query(r, types[t], profile, usage);
                HRESULT actual = IDirect3D9_CheckDeviceFormat(factory, 0, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, usage, (D3DRESOURCETYPE)types[t], (D3DFORMAT)r->format);
                if (actual != (expected ? D3D_OK : D3DERR_NOTAVAILABLE)) {
                    char message[220];
                    wsprintfA(message, "FAIL query host=%u profile=%u format=%08lX type=%u usage=%08lX got=%08lX expected=%08lX", D9WG_QUERY_HOST_MASK, profile, r->format, types[t], usage, (DWORD)actual, (DWORD)(expected ? D3D_OK : D3DERR_NOTAVAILABLE));
                    report(message);
                    ExitProcess(1);
                }
                ++queries;
            }
    }
    IDirect3D9_Release(factory);
    FreeLibrary(module);
    char message[180];
    wsprintfA(message, "PASS D3D9 format queries: simulatedHost=%u profile=%u queries=%u; single/compound flags, surface/texture, depth/color and software refusal", D9WG_QUERY_HOST_MASK, profile, queries);
    report(message);
    CloseHandle(logfile);
    ExitProcess(0);
}
