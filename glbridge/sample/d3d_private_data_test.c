/* Private-data API/COM probe, without a C runtime. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#if D9WG_PRIVATE_API == 8
#include <d3d8.h>
typedef IDirect3D8 Factory;
typedef IDirect3DDevice8 Device;
typedef IDirect3DVertexBuffer8 VertexBuffer;
typedef IDirect3DIndexBuffer8 IndexBuffer;
#define VB_LOCK IDirect3DVertexBuffer8_Lock
#define IB_LOCK IDirect3DIndexBuffer8_Lock
#define VB_UNLOCK IDirect3DVertexBuffer8_Unlock
#define IB_UNLOCK IDirect3DIndexBuffer8_Unlock
#define VB_RELEASE IDirect3DVertexBuffer8_Release
#define IB_RELEASE IDirect3DIndexBuffer8_Release
#define VB(method) IDirect3DVertexBuffer8_##method
#define IB(method) IDirect3DIndexBuffer8_##method
#define DEV(method) IDirect3DDevice8_##method
#define FACTORY_RELEASE IDirect3D8_Release
#else
#include <d3d9.h>
typedef IDirect3D9 Factory;
typedef IDirect3DDevice9 Device;
typedef IDirect3DVertexBuffer9 VertexBuffer;
typedef IDirect3DIndexBuffer9 IndexBuffer;
#define VB_LOCK IDirect3DVertexBuffer9_Lock
#define IB_LOCK IDirect3DIndexBuffer9_Lock
#define VB_UNLOCK IDirect3DVertexBuffer9_Unlock
#define IB_UNLOCK IDirect3DIndexBuffer9_Unlock
#define VB_RELEASE IDirect3DVertexBuffer9_Release
#define IB_RELEASE IDirect3DIndexBuffer9_Release
#define VB(method) IDirect3DVertexBuffer9_##method
#define IB(method) IDirect3DIndexBuffer9_##method
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

static const GUID key = { 0x12345678, 0xabcd, 0xef01, { 2, 3, 4, 5, 6, 7, 8, 9 } }, other = { 0x87654321, 0xdcba, 0x10fe, { 9, 8, 7, 6, 5, 4, 3, 2 } };
typedef struct {
    IUnknown iface;
    LONG refs;
} Counter;
static HRESULT WINAPI query(IUnknown* iface, REFIID iid, void** out)
{
    (void)iface;
    (void)iid;
    if (out)
        *out = NULL;
    return E_NOINTERFACE;
}
static ULONG WINAPI add(IUnknown* iface) { return (ULONG)InterlockedIncrement(&((Counter*)iface)->refs); }
static ULONG WINAPI drop(IUnknown* iface) { return (ULONG)InterlockedDecrement(&((Counter*)iface)->refs); }
static IUnknownVtbl counter_vtable = { query, add, drop };
/* Only the common IUnknown/Resource prefix is used; VB and IB layouts match. */
static void buffer_cases(IUnknown* buffer, UINT kind)
{
    typedef struct {
        HRESULT(WINAPI* qi)(void*, REFIID, void**);
        ULONG(WINAPI* add)(void*);
        ULONG(WINAPI* release)(void*);
        HRESULT(WINAPI* device)(void*, void**);
        HRESULT(WINAPI* set)(void*, REFGUID, const void*, DWORD, DWORD);
        HRESULT(WINAPI* get)(void*, REFGUID, void*, DWORD*);
        HRESULT(WINAPI* free)(void*, REFGUID);
    } ResourceMethods;
    const ResourceMethods* methods = (const void*)buffer->lpVtbl;
    DWORD size = 0xdeadbabe, out = 0xdeadbeef, input = 0x1234abcd;
    check(methods->get(buffer, &key, &out, &size) == D3DERR_NOTFOUND && out == 0xdeadbeef && size == 0xdeadbabe, "FAIL missing preserves output");
    check(methods->get(buffer, &key, NULL, NULL) == D3DERR_NOTFOUND && methods->free(buffer, &key) == D3DERR_NOTFOUND, "FAIL missing NULL query/free");
    check(methods->set(buffer, &key, &input, 4, 0) == D3D_OK, "FAIL raw set");
    input = 0;
    size = 1;
    check(methods->get(buffer, &key, NULL, &size) == D3D_OK && size == 4, "FAIL raw size query");
    size = 3;
    check(methods->get(buffer, &key, &out, &size) == D3DERR_MOREDATA && out == 0xdeadbeef && size == 4, "FAIL raw too small");
    size = 4;
    check(methods->get(buffer, &key, &out, &size) == D3D_OK && out == 0x1234abcd, "FAIL copied bytes");
    Counter object;
    object.iface.lpVtbl = &counter_vtable;
    object.refs = 1;
    check(methods->set(buffer, &key, &object, 0, D3DSPD_IUNKNOWN) == D3DERR_INVALIDCALL, "FAIL interface zero size");
    check(methods->set(buffer, &key, &object, 5, D3DSPD_IUNKNOWN) == D3DERR_INVALIDCALL, "FAIL interface five bytes");
    check(methods->set(buffer, &key, &object, 8, D3DSPD_IUNKNOWN) == D3DERR_INVALIDCALL, "FAIL interface double size");
    size = 4;
    check(methods->get(buffer, &key, &out, &size) == D3D_OK && out == 0x1234abcd, "FAIL failed replacement changed bytes");
    check(methods->set(buffer, &key, &object, sizeof(void*), D3DSPD_IUNKNOWN) == D3D_OK && object.refs == 2, "FAIL interface retain");
    size = 1;
    check(methods->get(buffer, &key, NULL, &size) == D3D_OK && size == 4 && object.refs == 2, "FAIL interface size-only");
    IUnknown* pointer = (void*)(uintptr_t)0xdeadbeef;
    size = 1;
    check(methods->get(buffer, &key, &pointer, &size) == D3DERR_MOREDATA && size == 4 && pointer == (void*)(uintptr_t)0xdeadbeef && object.refs == 2, "FAIL interface too small");
    size = 8;
    check(methods->get(buffer, &key, &pointer, &size) == D3D_OK && size == 4 && pointer == &object.iface && object.refs == 3, "FAIL interface get retain");
    IUnknown_Release(pointer);
    check(methods->set(buffer, &other, &input, 4, 0) == D3D_OK && object.refs == 2, "FAIL GUID isolation");
    check(methods->set(buffer, &key, &object, 4, D3DSPD_IUNKNOWN) == D3D_OK && object.refs == 2, "FAIL interface same replacement");
    check(methods->free(buffer, &key) == D3D_OK && object.refs == 1, "FAIL interface free release");
    check(methods->free(buffer, &key) == D3DERR_NOTFOUND, "FAIL duplicate free");
    check(methods->set(buffer, &key, &object, 4, D3DSPD_IUNKNOWN) == D3D_OK && object.refs == 2, "FAIL destruction setup");
    IUnknown_AddRef(buffer);
    check(IUnknown_Release(buffer) == 1 && object.refs == 2, "FAIL nonfinal release");
    check(IUnknown_Release(buffer) == 0 && object.refs == 1, "FAIL final release private reference");
    char line[80];
    wsprintfA(line, "CHECK PRIVATE %u %u raw=1234ABCD size=4 refs=1 released=0", D9WG_PRIVATE_API, kind);
    report(line);
}
static void private_cases(void)
{
    VertexBuffer* vb = NULL;
    IndexBuffer* ib = NULL;
    check(DEV(CreateVertexBuffer)(device, 64, 0, D3DFVF_XYZ, D3DPOOL_MANAGED, &vb
#if D9WG_PRIVATE_API == 9
              ,
              NULL
#endif
              )
            == D3D_OK,
        "FAIL vertex create");
    check(DEV(CreateIndexBuffer)(device, 32, 0, D3DFMT_INDEX16, D3DPOOL_MANAGED, &ib
#if D9WG_PRIVATE_API == 9
              ,
              NULL
#endif
              )
            == D3D_OK,
        "FAIL index create");
    buffer_cases((IUnknown*)vb, 0);
    buffer_cases((IUnknown*)ib, 1);
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], path[MAX_PATH], name[40];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    UINT at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    wsprintfA(name, "PRIV%u.TXT", D9WG_PRIVATE_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    RECT client = { 0, 0, 64, 64 };
    check(AdjustWindowRectEx(&client, WS_POPUP, FALSE, 0), "FAIL window dimensions");
    HWND window = CreateWindowExA(0, "STATIC", "D3D private data probe", WS_POPUP, 30, 40, client.right - client.left, client.bottom - client.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(window != NULL, "FAIL window");
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    RECT actual;
    check(GetClientRect(window, &actual) && actual.right == 64 && actual.bottom == 64, "FAIL exact client dimensions");
    wsprintfA(name, "D3D%u.DLL", D9WG_PRIVATE_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    HMODULE module = LoadLibraryA(path);
    check(module != NULL, "FAIL DLL");
    wsprintfA(name, "Direct3DCreate%u", D9WG_PRIVATE_API);
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
#if D9WG_PRIVATE_API == 8
    check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device8");
#else
    check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device9");
#endif
    private_cases();
    check(DEV(Release)(device) == 0, "FAIL device references");
    FACTORY_RELEASE(factory);
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS private API=%u", D9WG_PRIVATE_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
