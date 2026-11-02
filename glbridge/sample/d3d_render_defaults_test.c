/* Fresh/ALL/Reset defaults through both actual SDK COM frontends. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#if D9WG_DEFAULTS_API == 8
#include <d3d8.h>
typedef IDirect3D8 Factory;
typedef IDirect3DDevice8 Device;
typedef D3DCAPS8 Caps;
typedef DWORD Block;
#define CALL(n) IDirect3DDevice8_##n
#define FACT(n) IDirect3D8_##n
#else
#include <d3d9.h>
typedef IDirect3D9 Factory;
typedef IDirect3DDevice9 Device;
typedef D3DCAPS9 Caps;
typedef IDirect3DStateBlock9* Block;
#define CALL(n) IDirect3DDevice9_##n
#define FACT(n) IDirect3D9_##n
#endif
#include "d3d_render_default_reference.h"
static HANDLE logfile;
static Device* device;
static unsigned queries;
static void report(const char* s)
{
    DWORD n;
    WriteFile(logfile, s, lstrlenA(s), &n, NULL);
    WriteFile(logfile, "\r\n", 2, &n, NULL);
    FlushFileBuffers(logfile);
}
static void check(BOOL yes, const char* s)
{
    if (!yes) {
        report(s);
        ExitProcess(1);
    }
}
#define OK(x) check(SUCCEEDED(x), "FAIL " #x)
static void verify(BOOL depth)
{
    Caps caps;
    OK(CALL(GetDeviceCaps)(device, &caps));
    float maximum = caps.MaxPointSize > 1 ? caps.MaxPointSize : 64;
    DWORD maximum_bits;
    CopyMemory(&maximum_bits, &maximum, 4);
    for (unsigned i = 0; i < sizeof(render_default_reference) / sizeof(render_default_reference[0]); ++i) {
        const struct RenderDefaultReference* r = &render_default_reference[i];
        DWORD value = 0xcccccccc, expected = r->context == 1 ? (depth ? 1u : 0u) : r->context == 2 ? maximum_bits
                                                                                                   : r->value;
        OK(CALL(GetRenderState)(device, (D3DRENDERSTATETYPE)r->state, &value));
        if (value != expected) {
            char s[140];
            wsprintfA(s, "FAIL API %u default state %u actual=%08lX expected=%08lX depth=%u", D9WG_DEFAULTS_API, r->state, value, expected, depth);
            report(s);
            ExitProcess(1);
        }
        queries++;
    }
}
static void change(void)
{
    OK(CALL(SetRenderState)(device, D3DRS_ZENABLE, TRUE));
    OK(CALL(SetRenderState)(device, D3DRS_ZWRITEENABLE, FALSE));
    OK(CALL(SetRenderState)(device, D3DRS_CLIPPING, FALSE));
    OK(CALL(SetRenderState)(device, D3DRS_FOGEND, 0x40800000));
    OK(CALL(SetRenderState)(device, D3DRS_POINTSIZE_MAX, 0x42800000));
    DWORD v;
    OK(CALL(GetRenderState)(device, D3DRS_CLIPPING, &v));
    check(v == FALSE, "FAIL changed clipping");
}
void WINAPI WinMainCRTStartup(void)
{
    char dir[MAX_PATH], path[MAX_PATH], name[32];
    GetModuleFileNameA(NULL, dir, sizeof(dir));
    unsigned at = lstrlenA(dir);
    while (at && dir[at - 1] != '\\')
        --at;
    dir[at] = 0;
    lstrcpyA(path, dir);
    lstrcatA(path, "DEFAULT.TXT");
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    lstrcpyA(path, dir);
    wsprintfA(name, "D3D%u.DLL", D9WG_DEFAULTS_API);
    lstrcatA(path, name);
    HMODULE module = LoadLibraryA(path);
    check(module != NULL, "FAIL DLL");
    wsprintfA(name, "Direct3DCreate%u", D9WG_DEFAULTS_API);
    Factory*(WINAPI * make)(UINT) = (void*)(uintptr_t)GetProcAddress(module, name);
    check(make != NULL, "FAIL export");
    Factory* factory = make(D3D_SDK_VERSION);
    check(factory != NULL, "FAIL factory");
    HWND window = CreateWindowExA(0, "STATIC", "Default states", WS_OVERLAPPEDWINDOW, 0, 0, 96, 96, NULL, NULL, GetModuleHandleA(NULL), NULL);
    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.BackBufferWidth = pp.BackBufferHeight = 64;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferCount = 1;
    pp.Windowed = TRUE;
    pp.hDeviceWindow = window;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    OK(FACT(CreateDevice)(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device));
    verify(FALSE);
    Block baseline;
    OK(CALL(CreateStateBlock)(device, D3DSBT_ALL, &baseline));
    change();
#if D9WG_DEFAULTS_API == 8
    OK(CALL(ApplyStateBlock)(device, baseline));
    OK(CALL(DeleteStateBlock)(device, baseline));
#else
    OK(IDirect3DStateBlock9_Apply(baseline));
    IDirect3DStateBlock9_Release(baseline);
#endif
    verify(FALSE);
    change();
    pp.EnableAutoDepthStencil = TRUE;
    OK(CALL(Reset)(device, &pp));
    verify(TRUE);
    change();
    pp.EnableAutoDepthStencil = FALSE;
    OK(CALL(Reset)(device, &pp));
    verify(FALSE);
    CALL(Release)(device);
    FACT(Release)(factory);
    FreeLibrary(module);
    DestroyWindow(window);
    char message[160];
    wsprintfA(message, "PASS D3D%u render defaults: %u exact queries, fresh/ALL/reset, conditional depth, reported point maximum and SDK version differences", D9WG_DEFAULTS_API, queries);
    report(message);
    CloseHandle(logfile);
    ExitProcess(0);
}
