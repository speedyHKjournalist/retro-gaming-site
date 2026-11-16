/* Public backbuffer counts and private storage lifetime are independent. */
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
typedef struct {
    IUnknown iface;
    ULONG refs;
    UINT finalized;
} Tracker;
static Tracker tracker;
static HRESULT WINAPI tracker_query(IUnknown* iface, REFIID iid, void** out)
{
    if (!out)
        return E_POINTER;
    *out = NULL;
    if (!iid || iid->Data1 != unknown_id.Data1 || iid->Data2 != unknown_id.Data2 || iid->Data3 != unknown_id.Data3)
        return E_NOINTERFACE;
    for (UINT i = 0; i < 8; i++)
        if (iid->Data4[i] != unknown_id.Data4[i])
            return E_NOINTERFACE;
    *out = iface;
    IUnknown_AddRef(iface);
    return S_OK;
}
static ULONG WINAPI tracker_addref(IUnknown* iface) { return ++((Tracker*)iface)->refs; }
static ULONG WINAPI tracker_release(IUnknown* iface)
{
    Tracker* t = (Tracker*)iface;
    check(t->refs > 0, "FAIL private tracker underflow");
    if (!--t->refs)
        t->finalized++;
    return t->refs;
}
static IUnknownVtbl tracker_vtbl = { tracker_query, tracker_addref, tracker_release };
static D3DPRESENT_PARAMETERS parameters;
static HWND window;
static void make_device(void)
{
    D3DPRESENT_PARAMETERS p = parameters;
    check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window,
              D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &p, &device) == D3D_OK,
        "FAIL lifetime device");
    check(refs((IUnknown*)device) == 1 && refs((IUnknown*)factory) == 2, "FAIL initial device/factory references");
}
static IDirect3DSwapChain8* make_chain(IDirect3DSurface8** surface)
{
    D3DPRESENT_PARAMETERS p = parameters;
    p.BackBufferWidth = 32;
    p.BackBufferHeight = 24;
    IDirect3DSwapChain8* chain = NULL;
    check(IDirect3DDevice8_CreateAdditionalSwapChain(device, &p, &chain) == D3D_OK && chain,
        "FAIL lifetime chain");
    check(refs((IUnknown*)device) == 2 && refs((IUnknown*)chain) == 1, "FAIL one chain device pin");
    check(IDirect3DSwapChain8_GetBackBuffer(chain, 0, D3DBACKBUFFER_TYPE_MONO, surface) == D3D_OK
            && refs((IUnknown*)*surface) == 1 && refs((IUnknown*)device) == 3,
        "FAIL initial public surface/device pin");
    tracker.iface.lpVtbl = &tracker_vtbl;
    tracker.refs = 1;
    tracker.finalized = 0;
    DWORD marker = 0x10293847;
    check(IDirect3DSurface8_SetPrivateData(*surface, &private_key, &marker, 4, 0) == D3D_OK,
        "FAIL lifetime private marker");
    check(IDirect3DSurface8_SetPrivateData(*surface, &unknown_id, &tracker.iface,
              sizeof(IUnknown*), D3DSPD_IUNKNOWN) == D3D_OK
            && IUnknown_Release(&tracker.iface) == 1,
        "FAIL lifetime private object");
    return chain;
}
static void check_private(IDirect3DSurface8* surface)
{
    DWORD value = 0, size = 4;
    check(IDirect3DSurface8_GetPrivateData(surface, &private_key, &value, &size) == D3D_OK
            && value == 0x10293847 && tracker.refs == 1 && !tracker.finalized,
        "FAIL cached private lifetime");
}
static void revival_case(void)
{
    make_device();
    IDirect3DSurface8* surface = NULL;
    IDirect3DSwapChain8* chain = make_chain(&surface);
    check(IDirect3DSurface8_Release(surface) == 0 && refs((IUnknown*)device) == 2,
        "FAIL initial public zero edge");
    for (UINT cycle = 0; cycle < 32; cycle++) {
        IDirect3DSurface8 *a = NULL, *b = NULL;
        IUnknown* identity = NULL;
        check(IDirect3DSwapChain8_GetBackBuffer(chain, 0, D3DBACKBUFFER_TYPE_MONO, &a) == D3D_OK
                && a == surface && refs((IUnknown*)a) == 1 && refs((IUnknown*)device) == 3,
            "FAIL zero-count cached revival");
        check(IDirect3DSwapChain8_GetBackBuffer(chain, 0, D3DBACKBUFFER_TYPE_RIGHT, &b) == D3D_OK
                && b == surface && refs((IUnknown*)a) == 2 && refs((IUnknown*)device) == 3,
            "FAIL repeated public buffer reference");
        check(IDirect3DSurface8_QueryInterface(a, &unknown_id, (void**)&identity) == D3D_OK
                && identity == (IUnknown*)surface && IUnknown_Release(identity) == 2,
            "FAIL cached IUnknown identity/reference");
        check_private(a);
        check(IDirect3DSurface8_AddRef(a) == 3 && refs((IUnknown*)device) == 3,
            "FAIL public AddRef changed device pin");
        check(IDirect3DSurface8_Release(b) == 2 && IDirect3DSurface8_Release(a) == 1
                && refs((IUnknown*)device) == 3 && IDirect3DSurface8_Release(a) == 0
                && refs((IUnknown*)device) == 2,
            "FAIL public Release edges");
        check(IDirect3DSurface8_Release(surface) == 0 && IDirect3DSurface8_Release(surface) == 0
                && refs((IUnknown*)device) == 2,
            "FAIL repeated zero Release");
        check(IDirect3DSurface8_AddRef(surface) == 1 && refs((IUnknown*)device) == 3
                && IDirect3DSurface8_Release(surface) == 0 && refs((IUnknown*)device) == 2,
            "FAIL direct cached zero revival");
    }
    D3DPRESENT_PARAMETERS p = parameters;
    check(IDirect3DDevice8_Reset(device, &p) == D3DERR_DEVICELOST
            && IDirect3DDevice8_TestCooperativeLevel(device) == D3DERR_DEVICENOTRESET
            && tracker.refs == 1 && !tracker.finalized,
        "FAIL live chain Reset blocker");
    check(IDirect3DSwapChain8_Release(chain) == 0 && refs((IUnknown*)device) == 1
            && !tracker.refs && tracker.finalized == 1,
        "FAIL chain final storage/pin cleanup");
    p = parameters;
    check(IDirect3DDevice8_Reset(device, &p) == D3D_OK
            && IDirect3DDevice8_TestCooperativeLevel(device) == D3D_OK,
        "FAIL chain blocker Reset recovery");
    check(IDirect3DDevice8_Release(device) == 0 && refs((IUnknown*)factory) == 1,
        "FAIL revival device cleanup");
    report("REF8 case=0 cycles=32 publicZero=1 devicePin=1 failedReset=1 recoveredReset=1 privateFinalized=1");
}
static void last_view_case(void)
{
    make_device();
    IDirect3DSurface8* surface = NULL;
    IDirect3DSwapChain8* chain = make_chain(&surface);
    check(IDirect3DSurface8_AddRef(surface) == 2 && refs((IUnknown*)device) == 3,
        "FAIL retained view references");
    check(IDirect3DDevice8_Release(device) == 2, "FAIL application device release");
    check(IDirect3DSwapChain8_Release(chain) == 0 && refs((IUnknown*)device) == 1,
        "FAIL chain-first remaining device pin");
    IUnknown* container = NULL;
    check(IDirect3DSurface8_GetContainer(surface, &IID_IDirect3DDevice8, (void**)&container) == D3D_OK
            && container == (IUnknown*)device && refs((IUnknown*)device) == 2,
        "FAIL retained view device container");
    check(IUnknown_Release(container) == 1, "FAIL retained container release");
    check_private(surface);
    check(IDirect3DSurface8_Release(surface) == 1 && IDirect3DSurface8_Release(surface) == 0
            && !tracker.refs && tracker.finalized == 1 && refs((IUnknown*)factory) == 1,
        "FAIL last public view device/factory teardown");
    report("REF8 case=1 deviceFirst=1 chainFirst=1 lastViewDevice=0 privateFinalized=1");
}
static void binding_case(void)
{
    make_device();
    IDirect3DSurface8* surface = NULL;
    IDirect3DSwapChain8* chain = make_chain(&surface);
    check(IDirect3DDevice8_SetRenderTarget(device, surface, NULL) == D3D_OK
            && refs((IUnknown*)surface) == 1 && refs((IUnknown*)device) == 3,
        "FAIL binding exposed internal references");
    check(IDirect3DSwapChain8_Release(chain) == 0 && refs((IUnknown*)device) == 2
            && IDirect3DSurface8_Release(surface) == 0 && refs((IUnknown*)device) == 1
            && tracker.refs == 1 && !tracker.finalized,
        "FAIL binding-only storage/private retention");
    IDirect3DSurface8* bound = NULL;
    check(IDirect3DDevice8_GetRenderTarget(device, &bound) == D3D_OK && bound == surface
            && refs((IUnknown*)bound) == 1 && refs((IUnknown*)device) == 2,
        "FAIL bound view zero revival");
    check(IDirect3DSurface8_Release(bound) == 0 && refs((IUnknown*)device) == 1,
        "FAIL bound view zero edge");
    D3DPRESENT_PARAMETERS p = parameters;
    check(IDirect3DDevice8_Reset(device, &p) == D3D_OK
            && IDirect3DDevice8_TestCooperativeLevel(device) == D3D_OK
            && !tracker.refs && tracker.finalized == 1 && refs((IUnknown*)device) == 1,
        "FAIL binding-only Reset cleanup");
    check(IDirect3DDevice8_Release(device) == 0 && refs((IUnknown*)factory) == 1,
        "FAIL binding device cleanup");
    report("REF8 case=2 bindingOnly=1 surfacePublic=0 reset=1 privateFinalized=1 deviceRefs=0");
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
    lstrcatA(path, "REF8.TXT");
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    window = CreateWindowExA(0, "STATIC", "D3D8 public buffer lifetime probe", WS_POPUP,
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
    ZeroMemory(&parameters, sizeof(parameters));
    parameters.Windowed = TRUE;
    parameters.SwapEffect = D3DSWAPEFFECT_DISCARD;
    parameters.BackBufferFormat = D3DFMT_X8R8G8B8;
    parameters.BackBufferWidth = parameters.BackBufferHeight = 64;
    revival_case();
    last_view_case();
    binding_case();
    check(IDirect3D8_Release(factory) == 0, "FAIL final factory reference");
    FreeLibrary(module);
    DestroyWindow(window);
    report("PASS additional lifetime API=8");
    CloseHandle(logfile);
    ExitProcess(0);
}
