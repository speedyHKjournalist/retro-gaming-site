/* D3D9 public-zero additional-chain private binding and physical lifetime. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <initguid.h>
#if D9WG_IMPLICIT_API == 8
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

#if D9WG_IMPLICIT_API == 8
typedef IDirect3DSurface8 Surface;
#define SURFACE(m) IDirect3DSurface8_##m
#else
typedef IDirect3DSurface9 Surface;
#define SURFACE(m) IDirect3DSurface9_##m
#endif
static ULONG refs(IUnknown* object)
{
    IUnknown_AddRef(object);
    return IUnknown_Release(object);
}
static const GUID key = { 0x71727374, 1, 2, { 3, 4, 5, 6, 7, 8, 9, 10 } }, object_key = { 0x81828384, 1, 2, { 3, 4, 5, 6, 7, 8, 9, 10 } }, unknown_id = { 0, 0, 0, { 0xc0, 0, 0, 0, 0, 0, 0, 0x46 } };
typedef struct {
    IUnknown iface;
    ULONG refs;
    UINT finalized;
} Tracker;
static Tracker tracker;
static UINT finalized_total;
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
    if (!--t->refs) {
        check(++t->finalized==1 && refs((IUnknown*)device)==1,"FAIL protected chain callback");
        finalized_total++;
    }
    return t->refs;
}
static IUnknownVtbl tracker_vtbl = { tracker_query, tracker_addref, tracker_release };
static void set_target(Surface* target)
{
#if D9WG_IMPLICIT_API == 8
    check(DEV(SetRenderTarget)(device, target, NULL) == D3D_OK, "FAIL select independent target");
#else
    check(DEV(SetRenderTarget)(device, 0, target) == D3D_OK && DEV(SetDepthStencilSurface)(device, NULL) == D3D_OK, "FAIL select independent target");
#endif
}
static Surface* current_target(void)
{
    Surface* out = NULL;
#if D9WG_IMPLICIT_API == 8
    check(DEV(GetRenderTarget)(device, &out) == D3D_OK && out, "FAIL current target");
#else
    check(DEV(GetRenderTarget)(device, 0, &out) == D3D_OK && out, "FAIL current target");
#endif
    return out;
}
static void clear_target(DWORD color) { check(DEV(Clear)(device, 0, NULL, D3DCLEAR_TARGET, color, 1, 0) == D3D_OK, "FAIL independent clear"); }

static void present_frame(Chain* chain, UINT step, DWORD color)
{
#if D9WG_IMPLICIT_API == 8
    check(CHAIN(Present)(chain, NULL, NULL, NULL, NULL) == D3D_OK, "FAIL revived chain present8");
#else
    check(CHAIN(Present)(chain, NULL, NULL, NULL, NULL, 0) == D3D_OK, "FAIL revived chain present9");
#endif
    char line[140];
    wsprintfA(line, "CHAIN BIND FRAME api=%u step=%u color=%08lX surfacePublicRefs=0 chainPublicRefs=1", D9WG_IMPLICIT_API, step, color);
    report(line);
}
static void create_device(HWND window,const D3DPRESENT_PARAMETERS* base)
{
    D3DPRESENT_PARAMETERS p=*base;
    check(IDirect3D9_CreateDevice(factory,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&p,&device)==D3D_OK && device && refs((IUnknown*)device)==1,"FAIL next chain device");
}
static void attach_private(Surface* surface)
{
    ZeroMemory(&tracker,sizeof(tracker));tracker.iface.lpVtbl=&tracker_vtbl;tracker.refs=1;
    DWORD marker=0x10293847;
    check(SURFACE(SetPrivateData)(surface,&key,&marker,4,0)==D3D_OK && SURFACE(SetPrivateData)(surface,&object_key,&tracker.iface,sizeof(IUnknown*),D3DSPD_IUNKNOWN)==D3D_OK && IUnknown_Release(&tracker.iface)==1,"FAIL chain private attach");
}
static Chain* create_chain(HWND window,const D3DPRESENT_PARAMETERS* base,UINT samples,Surface** out)
{
    D3DPRESENT_PARAMETERS p=*base;p.BackBufferWidth=32;p.BackBufferHeight=24;p.hDeviceWindow=window;p.MultiSampleType=(D3DMULTISAMPLE_TYPE)samples;p.MultiSampleQuality=0;
    Chain* chain=NULL;
    check(DEV(CreateAdditionalSwapChain)(device,&p,&chain)==D3D_OK && chain && refs((IUnknown*)chain)==1 && refs((IUnknown*)device)==2,"FAIL chain parent creation");
    check(CHAIN(GetBackBuffer)(chain,0,D3DBACKBUFFER_TYPE_MONO,out)==D3D_OK && *out && refs((IUnknown*)*out)==1 && refs((IUnknown*)device)==3,"FAIL surface parent creation");
    D3DSURFACE_DESC desc;D3DPRESENT_PARAMETERS actual;
    check(SURFACE(GetDesc)(*out,&desc)==D3D_OK && desc.Width==32 && desc.Height==24 && desc.MultiSampleType==(D3DMULTISAMPLE_TYPE)samples && !desc.MultiSampleQuality && CHAIN(GetPresentParameters)(chain,&actual)==D3D_OK && actual.MultiSampleType==(D3DMULTISAMPLE_TYPE)samples && !actual.MultiSampleQuality,"FAIL chain sample metadata");
    attach_private(*out);set_target(*out);
    check(refs((IUnknown*)*out)==1 && refs((IUnknown*)device)==3,"FAIL binding added public surface ref");
    check(SURFACE(Release)(*out)==0 && CHAIN(Release)(chain)==0 && refs((IUnknown*)device)==1 && !tracker.finalized && tracker.refs==1,"FAIL zero-public bound storage");
    return chain;
}
static void check_private(Surface* surface)
{
    DWORD marker=0,size=sizeof(marker);IUnknown* object=NULL;
    check(SURFACE(GetPrivateData)(surface,&key,&marker,&size)==D3D_OK && marker==0x10293847,"FAIL bound chain marker");
    size=sizeof(object);check(SURFACE(GetPrivateData)(surface,&object_key,&object,&size)==D3D_OK && object==&tracker.iface && IUnknown_Release(object)==1 && !tracker.finalized,"FAIL bound chain private object");
}
static void zero_public_frame(Chain* chain,UINT frame,DWORD color)
{
    check(refs((IUnknown*)device)==1,"FAIL pre-frame private parent refs");clear_target(color);
    check(CHAIN(AddRef)(chain)==1 && refs((IUnknown*)device)==2,"FAIL frame chain revival");present_frame(chain,frame,color);
    check(CHAIN(Release)(chain)==0 && refs((IUnknown*)device)==1 && !tracker.finalized,"FAIL frame chain return");
}
static void revival_cases(HWND primary,const D3DPRESENT_PARAMETERS* base)
{
    DWORD levels=0;check(IDirect3D9_CheckDeviceMultiSampleType(factory,0,D3DDEVTYPE_HAL,D3DFMT_X8R8G8B8,TRUE,D3DMULTISAMPLE_4_SAMPLES,&levels)==D3D_OK && levels==1,"FAIL chain four-sample capability");
    HWND secondary=CreateWindowExA(0,"STATIC","Additional private bindings",WS_POPUP,200,40,32,24,NULL,NULL,GetModuleHandleA(NULL),NULL);check(secondary!=NULL,"FAIL secondary window");ShowWindow(secondary,SW_SHOW);SetForegroundWindow(secondary);
    Surface* surface=NULL;Chain* chain=create_chain(secondary,base,4,&surface);
    report("CHAIN BIND BUFFER samples=4 width=32 height=24 surfacePublicRefs=0 chainPublicRefs=0 deviceRefs=1");
    zero_public_frame(chain,0,0xffb02010);
    for(UINT cycle=0;cycle<32;cycle++) {
        Surface* view=current_target();check(view==surface && refs((IUnknown*)view)==1 && refs((IUnknown*)device)==2,"FAIL bound public-zero view revival");check_private(view);
        Chain* owner=NULL;check(SURFACE(GetContainer)(view,&IID_IDirect3DSwapChain9,(void**)&owner)==D3D_OK && owner==chain && refs((IUnknown*)owner)==1 && refs((IUnknown*)device)==3,"FAIL zero-chain container revival");
        IUnknown* identity=NULL;check(CHAIN(QueryInterface)(owner,&unknown_id,(void**)&identity)==D3D_OK && identity==(IUnknown*)chain && IUnknown_Release(identity)==1 && refs((IUnknown*)device)==3,"FAIL chain IUnknown pin balance");
        Surface* again=NULL;check(CHAIN(GetBackBuffer)(owner,0,D3DBACKBUFFER_TYPE_RIGHT,&again)==D3D_OK && again==view && refs((IUnknown*)again)==2 && SURFACE(Release)(again)==1,"FAIL chain cached surface identity");
        check(SURFACE(Release)(view)==0 && CHAIN(Release)(owner)==0 && refs((IUnknown*)device)==1,"FAIL revival public pins return");
        set_target(surface);check(refs((IUnknown*)device)==1 && tracker.refs==1 && !tracker.finalized,"FAIL borrowed private rebind");
    }
    report("CHAIN BIND COUNTERS rounds=32 surfaceRevivals=32 containerRevivals=32 publicRefs=0 deviceRefs=1 privateRetained=1");
    zero_public_frame(chain,1,0xff1040c0);
    D3DPRESENT_PARAMETERS p=*base;
    check(CHAIN(AddRef)(chain)==1 && DEV(Reset)(device,&p)==D3DERR_INVALIDCALL && !tracker.finalized && CHAIN(Release)(chain)==0,"FAIL public chain Reset blocker");
    Surface* held=current_target();p=*base;
    check(held==surface && DEV(Reset)(device,&p)==D3DERR_INVALIDCALL && !tracker.finalized && SURFACE(Release)(held)==0,"FAIL public chain surface Reset blocker");
    p=*base;check(DEV(Reset)(device,&p)==D3D_OK && DEV(TestCooperativeLevel)(device)==D3D_OK && refs((IUnknown*)device)==1 && !tracker.refs && tracker.finalized==1,"FAIL private-only chain Reset cleanup");
    check(DEV(Release)(device)==0 && refs((IUnknown*)factory)==1,"FAIL post-Reset chain device cleanup");device=NULL;
    report("CHAIN BIND RESET refused=2 accepted=1 privateFinalized=1 callbackDeviceRefs=1 factoryRefs=1");
    create_device(primary,base);chain=create_chain(secondary,base,0,&surface);(void)chain;
    check(DEV(Release)(device)==0 && !tracker.refs && tracker.finalized==1 && refs((IUnknown*)factory)==1,"FAIL binding-only device cleanup");device=NULL;
    report("CHAIN BIND FINAL samples=0 privateFinalized=1 callbackDeviceRefs=1 factoryRefs=1");
    create_device(primary,base);
    Surface* back=NULL;check(DEV(GetBackBuffer)(device,0,0,D3DBACKBUFFER_TYPE_MONO,&back)==D3D_OK && SURFACE(Release)(back)==0 && refs((IUnknown*)device)==1,"FAIL borrowed primary cache");
    chain=create_chain(secondary,base,0,&surface);(void)chain;set_target(back);
    check(!tracker.refs && tracker.finalized==1 && refs((IUnknown*)device)==1,"FAIL final private unbind");
    check(DEV(Release)(device)==0 && refs((IUnknown*)factory)==1,"FAIL unbind device cleanup");device=NULL;
    report("CHAIN BIND UNBIND samples=0 privateFinalized=1 callbackDeviceRefs=1 factoryRefs=1");
    check(finalized_total==3,"FAIL total chain finalizers");
    report("CHAIN BIND STATUS devices=3 reset=1 refusedReset=2 frames=2 loopRevivals=64 privateFinalized=3 publicBindingRefs=0 factoryRefs=1");DestroyWindow(secondary);
}

void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], path[MAX_PATH], name[100];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    UINT at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    wsprintfA(name, "CBN%u.TXT", D9WG_IMPLICIT_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    RECT client = { 0, 0, 64, 64 };
    check(AdjustWindowRectEx(&client, WS_POPUP, FALSE, 0), "FAIL window dimensions");
    HWND window = CreateWindowExA(0, "STATIC", "D3D revived chain probe", WS_POPUP, 30, 40, client.right - client.left, client.bottom - client.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(window != NULL, "FAIL window");
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    RECT actual;
    check(GetClientRect(window, &actual) && actual.right == 64 && actual.bottom == 64, "FAIL exact client dimensions");
    wsprintfA(name, "D3D%u.DLL", D9WG_IMPLICIT_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    HMODULE module = LoadLibraryA(path);
    check(module != NULL, "FAIL DLL");
    wsprintfA(name, "Direct3DCreate%u", D9WG_IMPLICIT_API);
    Factory*(WINAPI * make)(UINT) = (void*)(uintptr_t)GetProcAddress(module, name);
    check(make != NULL, "FAIL factory export");
    factory = make(D3D_SDK_VERSION);
    check(factory != NULL, "FAIL factory");
    D3DPRESENT_PARAMETERS pp;
    ZeroMemory(&pp, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow = NULL;
    pp.BackBufferWidth = 0;
    pp.BackBufferHeight = 0;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.EnableAutoDepthStencil = FALSE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
#if D9WG_IMPLICIT_API == 8
    check(IDirect3D8_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device8");
#else
    check(IDirect3D9_CreateDevice(factory, 0, D3DDEVTYPE_HAL, window, D3DCREATE_SOFTWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device) == D3D_OK, "FAIL device9");
#endif
    check(DEV(TestCooperativeLevel)(device) == D3D_OK, "FAIL initial cooperative status");
    revival_cases(window,&pp);
    check(!device, "FAIL final device pointer");
    check(FACTORY_RELEASE(factory) == 0, "FAIL final revival factory references");
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS additional private bindings API=%u", D9WG_IMPLICIT_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
