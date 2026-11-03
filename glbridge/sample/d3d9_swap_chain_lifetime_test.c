/* Swap-chain COM identity, stable parent pins and final-owner SDK probe. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <d3d9.h>
#define D9WG_RESET_API 9
typedef IDirect3D9 Factory;
typedef IDirect3DDevice9 Device;
typedef IDirect3DSwapChain9 Chain;
#define CHAIN(m) IDirect3DSwapChain9_##m
#define DEV(m) IDirect3DDevice9_##m
#define FACTORY_RELEASE IDirect3D9_Release
static Factory* factory;
static Device* device;
static HANDLE logfile;
static const GUID unknown_id = { 0, 0, 0, { 0xc0, 0, 0, 0, 0, 0, 0, 0x46 } };
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
static void create_device(D3DPRESENT_PARAMETERS* base)
{
    D3DPRESENT_PARAMETERS p = *base;
    device = NULL;
    check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, p.hDeviceWindow, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &p, &device) == D3D_OK && device, "FAIL lifetime device create");
}
static Chain* create_additional(D3DPRESENT_PARAMETERS* base)
{
    D3DPRESENT_PARAMETERS p = *base;
    p.BackBufferWidth = 96;
    p.BackBufferHeight = 64;
    p.EnableAutoDepthStencil = FALSE;
    Chain* c = NULL;
    check(DEV(CreateAdditionalSwapChain)(device, &p, &c) == D3D_OK && c, "FAIL additional chain create");
    return c;
}
static void chain_cases(D3DPRESENT_PARAMETERS* base)
{
    check(refs((IUnknown*)device) == 1, "FAIL initial device references");
    Chain* cache = NULL;
    for (UINT cycle = 0; cycle < 32; cycle++) {
        Chain *a = NULL, *b = NULL;
        IUnknown* identity = NULL;
        Device* parent = NULL;
        check(DEV(GetSwapChain)(device, 0, &a) == D3D_OK && a && refs((IUnknown*)device) == 2, "FAIL implicit first pin");
        if (cache)
            check(a == cache, "FAIL implicit identity after revival");
        cache = a;
        check(DEV(GetSwapChain)(device, 0, &b) == D3D_OK && b == a && refs((IUnknown*)a) == 2 && refs((IUnknown*)device) == 2, "FAIL implicit repeat pin");
        check(CHAIN(AddRef)(a) == 3 && refs((IUnknown*)device) == 2, "FAIL implicit additional AddRef pin");
        check(CHAIN(QueryInterface)(a, &unknown_id, (void**)&identity) == D3D_OK && identity == (IUnknown*)a && refs((IUnknown*)a) == 4 && refs((IUnknown*)device) == 2, "FAIL implicit identity pin");
        check(CHAIN(GetDevice)(a, &parent) == D3D_OK && parent == device && refs((IUnknown*)device) == 3 && DEV(Release)(parent) == 2, "FAIL GetDevice reference");
        check(IUnknown_Release(identity) == 3 && CHAIN(Release)(b) == 2 && CHAIN(Release)(a) == 1 && refs((IUnknown*)device) == 2, "FAIL implicit intermediate Release");
        check(CHAIN(Release)(a) == 0 && refs((IUnknown*)device) == 1, "FAIL implicit final public Release");
        check(CHAIN(Release)(a) == 0 && CHAIN(Release)(a) == 0 && refs((IUnknown*)device) == 1, "FAIL implicit repeated zero Release");
        check(CHAIN(AddRef)(a) == 1 && refs((IUnknown*)device) == 2 && CHAIN(Release)(a) == 0 && refs((IUnknown*)device) == 1, "FAIL implicit zero revival");
    }
    Chain* chains[3];
    for (UINT i = 0; i < 3; i++)
        chains[i] = create_additional(base);
    ULONG before = refs((IUnknown*)device);
    check(DEV(GetNumberOfSwapChains)(device) == 1, "FAIL additional implicit ordinal count");
    for (UINT i = 0; i < 3; i++) {
        Chain* c = chains[i];
        IUnknown* identity = NULL;
        for (UINT n = 2; n <= 5; n++)
            check(CHAIN(AddRef)(c) == n && refs((IUnknown*)device) == before, "FAIL additional repeated AddRef pin");
        check(CHAIN(QueryInterface)(c, &unknown_id, (void**)&identity) == D3D_OK && identity == (IUnknown*)c && refs((IUnknown*)device) == before, "FAIL additional QueryInterface pin");
        check(IUnknown_Release(identity) == 5 && refs((IUnknown*)device) == before, "FAIL additional identity Release pin");
        for (UINT n = 4; n > 0; n--)
            check(CHAIN(Release)(c) == n && refs((IUnknown*)device) == before, "FAIL additional intermediate Release pin");
    }
    for (UINT i = 0; i < 3; i++) {
        UINT index = i == 0 ? 1 : i == 1 ? 0
                                         : 2;
        check(CHAIN(Release)(chains[index]) == 0, "FAIL final additional chain Release");
        char line[100];
        wsprintfA(line, "CHAIN PIN index=%u stable=1 retired=1", index);
        report(line);
    }
    check(refs((IUnknown*)device) == 1, "FAIL additional chain leaked parent reference");
}
static void last_owner_cases(D3DPRESENT_PARAMETERS* base)
{
    check(refs((IUnknown*)factory) == 1, "FAIL ordinary device retained factory");
    create_device(base);
    Chain* c = NULL;
    check(DEV(GetSwapChain)(device, 0, &c) == D3D_OK, "FAIL final-owner implicit chain");
    check(CHAIN(AddRef)(c) == 2 && CHAIN(AddRef)(c) == 3 && refs((IUnknown*)device) == 2, "FAIL final implicit stable pin");
    check(DEV(Release)(device) == 1 && refs((IUnknown*)factory) == 2, "FAIL premature implicit device release");
    check(CHAIN(Release)(c) == 2 && CHAIN(Release)(c) == 1 && CHAIN(Release)(c) == 0 && refs((IUnknown*)factory) == 1, "FAIL final implicit owner teardown");
    create_device(base);
    c = create_additional(base);
    ULONG before = refs((IUnknown*)device);
    check(CHAIN(AddRef)(c) == 2 && CHAIN(AddRef)(c) == 3 && refs((IUnknown*)device) == before, "FAIL final additional stable pin");
    check(DEV(Release)(device) == before - 1 && refs((IUnknown*)factory) == 2, "FAIL premature additional device release");
    check(CHAIN(Release)(c) == 2 && CHAIN(Release)(c) == 1 && CHAIN(Release)(c) == 0 && refs((IUnknown*)factory) == 1, "FAIL final additional owner teardown");
    report("SWAPCHAIN LIFETIME api=9 implicitCycles=32 additional=4 deviceLifetimes=3 zeroRelease=64 revivals=32 devicePinDeltas=0 lastOwners=2 factoryFinal=1");
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], path[MAX_PATH], name[100];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    UINT at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    wsprintfA(name, "CHN%u.TXT", D9WG_RESET_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    RECT client = { 0, 0, 64, 64 };
    check(AdjustWindowRectEx(&client, WS_POPUP, FALSE, 0), "FAIL window dimensions");
    HWND window = CreateWindowExA(0, "STATIC", "D3D9 swap-chain lifetime probe", WS_POPUP, 30, 40, client.right - client.left, client.bottom - client.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(window != NULL, "FAIL window");
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    RECT actual;
    check(GetClientRect(window, &actual) && actual.right == 64 && actual.bottom == 64, "FAIL exact client dimensions");
    wsprintfA(name, "D3D%u.DLL", D9WG_RESET_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    HMODULE module = LoadLibraryA(path);
    check(module != NULL, "FAIL DLL");
    wsprintfA(name, "Direct3DCreate%u", D9WG_RESET_API);
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
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
#if D9WG_RESET_API == 8
    check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device8");
#else
    check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device9");
#endif
    check(DEV(TestCooperativeLevel)(device) == D3D_OK, "FAIL initial cooperative status");
    chain_cases(&pp);
    check(DEV(Release)(device) == 0, "FAIL device references");
    last_owner_cases(&pp);
    FACTORY_RELEASE(factory);
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS swap-chain lifetime API=%u", D9WG_RESET_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
