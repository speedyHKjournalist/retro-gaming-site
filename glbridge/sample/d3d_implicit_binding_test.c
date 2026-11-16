/* Cached implicit private-IUnknown callbacks run under a live device pin. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <initguid.h>
#if D9WG_IMPLICIT_API == 8
#include <d3d8.h>
typedef IDirect3D8 Factory;
typedef IDirect3DDevice8 Device;
typedef IDirect3DSurface8 Surface;
#define DEV(m) IDirect3DDevice8_##m
#define SURFACE(m) IDirect3DSurface8_##m
#define FACTORY_RELEASE IDirect3D8_Release
#else
#include <d3d9.h>
typedef IDirect3D9 Factory;
typedef IDirect3DDevice9 Device;
typedef IDirect3DSurface9 Surface;
#define DEV(m) IDirect3DDevice9_##m
#define SURFACE(m) IDirect3DSurface9_##m
#define FACTORY_RELEASE IDirect3D9_Release
#endif
static Factory* factory;
static Device* device;
static HANDLE logfile;
static void report(const char* text) { DWORD n; WriteFile(logfile,text,lstrlenA(text),&n,NULL); WriteFile(logfile,"\r\n",2,&n,NULL); FlushFileBuffers(logfile); }
static void check(BOOL condition,const char* message) { if(!condition) {report(message);ExitProcess(1);} }
static ULONG refs(IUnknown* object) { IUnknown_AddRef(object); return IUnknown_Release(object); }
static const GUID object_key = { 0x81828384, 1, 2, { 3, 4, 5, 6, 7, 8, 9, 10 } }, unknown_id = { 0, 0, 0, { 0xc0, 0, 0, 0, 0, 0, 0, 0x46 } };
typedef struct { IUnknown iface; ULONG refs; UINT finalized; } Tracker;
static Tracker trackers[3];
static HRESULT WINAPI tracker_query(IUnknown* iface, REFIID iid, void** out)
{
    if (!out) return E_POINTER;
    *out = NULL;
    if (!iid || iid->Data1 != unknown_id.Data1 || iid->Data2 != unknown_id.Data2 || iid->Data3 != unknown_id.Data3) return E_NOINTERFACE;
    for (UINT i=0;i<8;i++) if (iid->Data4[i]!=unknown_id.Data4[i]) return E_NOINTERFACE;
    *out=iface; IUnknown_AddRef(iface); return S_OK;
}
static ULONG WINAPI tracker_addref(IUnknown* iface) { return ++((Tracker*)iface)->refs; }
static ULONG WINAPI tracker_release(IUnknown* iface)
{
    Tracker* t=(Tracker*)iface;
    check(t->refs>0,"FAIL private tracker underflow");
    if(!--t->refs) { check(++t->finalized==1 && refs((IUnknown*)device)==1,"FAIL finalizer parent pin"); }
    return t->refs;
}
static IUnknownVtbl tracker_vtbl={tracker_query,tracker_addref,tracker_release};
static void create(HWND window,const D3DPRESENT_PARAMETERS* base)
{
    D3DPRESENT_PARAMETERS p=*base;
#if D9WG_IMPLICIT_API == 8
    check(IDirect3D8_CreateDevice(factory,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&p,&device)==D3D_OK,"FAIL create8");
#else
    check(IDirect3D9_CreateDevice(factory,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&p,&device)==D3D_OK,"FAIL create9");
#endif
    check(device && refs((IUnknown*)device)==1,"FAIL initial device refs");
}
static void pair(Surface** back,Surface** depth)
{
#if D9WG_IMPLICIT_API == 8
    check(DEV(GetBackBuffer)(device,0,D3DBACKBUFFER_TYPE_MONO,back)==D3D_OK,"FAIL back8");
#else
    check(DEV(GetBackBuffer)(device,0,0,D3DBACKBUFFER_TYPE_MONO,back)==D3D_OK,"FAIL back9");
#endif
    check(DEV(GetDepthStencilSurface)(device,depth)==D3D_OK && *back && *depth,"FAIL depth");
    check(refs((IUnknown*)*back)==1 && refs((IUnknown*)*depth)==1 && refs((IUnknown*)device)==3,"FAIL public pair refs");
    Surface* surfaces[]={*back,*depth};
    for(UINT i=0;i<2;i++) {
        ZeroMemory(&trackers[i],sizeof(trackers[i])); trackers[i].iface.lpVtbl=&tracker_vtbl; trackers[i].refs=1;
        check(SURFACE(SetPrivateData)(surfaces[i],&object_key,&trackers[i].iface,sizeof(IUnknown*),D3DSPD_IUNKNOWN)==D3D_OK && IUnknown_Release(&trackers[i].iface)==1,"FAIL private attach");
    }
}
static void bind(Surface* back,Surface* depth)
{
#if D9WG_IMPLICIT_API == 8
    check(DEV(SetRenderTarget)(device,back,depth)==D3D_OK,"FAIL bind8");
#else
    check(DEV(SetRenderTarget)(device,0,back)==D3D_OK && DEV(SetDepthStencilSurface)(device,depth)==D3D_OK,"FAIL bind9");
#endif
}
static void empty_public(Surface* back,Surface* depth)
{
    check(SURFACE(Release)(back)==0 && SURFACE(Release)(depth)==0 && refs((IUnknown*)device)==1,"FAIL private binding public zero");
}
static void finalized(void)
{
    for(UINT i=0;i<2;i++) check(!trackers[i].refs && trackers[i].finalized==1,"FAIL exact private finalizers");
}
static void callback_cases(HWND window,const D3DPRESENT_PARAMETERS* base)
{
    create(window,base); Surface *back=NULL,*depth=NULL; pair(&back,&depth);
    bind(back,depth);
    check(refs((IUnknown*)back)==1 && refs((IUnknown*)depth)==1 && refs((IUnknown*)device)==3,"FAIL implicit binding changes public refs");
    empty_public(back,depth);
    for(UINT cycle=0;cycle<32;cycle++) {
        Surface *a=NULL,*b=NULL;
#if D9WG_IMPLICIT_API == 8
        check(DEV(GetRenderTarget)(device,&a)==D3D_OK,"FAIL target revival8");
#else
        check(DEV(GetRenderTarget)(device,0,&a)==D3D_OK,"FAIL target revival9");
#endif
        check(DEV(GetDepthStencilSurface)(device,&b)==D3D_OK && a==back && b==depth && refs((IUnknown*)a)==1 && refs((IUnknown*)b)==1 && refs((IUnknown*)device)==3,"FAIL implicit revival identity/refs");
        Surface* surfaces[]={a,b};
        for(UINT i=0;i<2;i++) { IUnknown* object=NULL;DWORD size=sizeof(object);check(SURFACE(GetPrivateData)(surfaces[i],&object_key,&object,&size)==D3D_OK && object==&trackers[i].iface && IUnknown_Release(object)==1 && !trackers[i].finalized,"FAIL implicit private preservation"); }
        empty_public(a,b); bind(back,depth);
        check(refs((IUnknown*)back)==0 && refs((IUnknown*)depth)==0 && refs((IUnknown*)device)==1,"FAIL borrowed implicit rebind");
    }
    report("IMPLICIT BIND rounds=32 revivals=64 publicRefs=0 activeDeviceRefs=1 queryDeviceRefs=3 privateRetained=2");
    const DWORD colors[]={0xffb02010,0xff1040c0};
    for(UINT frame=0;frame<2;frame++) {
        check(DEV(Clear)(device,0,NULL,D3DCLEAR_TARGET,colors[frame],1.0f,0)==D3D_OK && DEV(Present)(device,NULL,NULL,NULL,NULL)==D3D_OK,"FAIL public-zero implicit frame");
        char line[160];wsprintfA(line,"IMPLICIT FRAME index=%u color=%08lX publicRefs=0 presented=1",frame,colors[frame]);report(line);
    }
    Surface *a=NULL,*b=NULL;
#if D9WG_IMPLICIT_API == 8
    check(DEV(GetBackBuffer)(device,0,D3DBACKBUFFER_TYPE_MONO,&a)==D3D_OK,"FAIL held back8");
#else
    check(DEV(GetBackBuffer)(device,0,0,D3DBACKBUFFER_TYPE_MONO,&a)==D3D_OK,"FAIL held back9");
#endif
    check(DEV(GetDepthStencilSurface)(device,&b)==D3D_OK && a==back && b==depth,"FAIL held cached pair");
    D3DPRESENT_PARAMETERS p=*base;
#if D9WG_IMPLICIT_API == 8
    const HRESULT blocked_reset=D3DERR_DEVICELOST;
#else
    const HRESULT blocked_reset=D3DERR_INVALIDCALL;
#endif
    check(DEV(Reset)(device,&p)==blocked_reset && !trackers[0].finalized && !trackers[1].finalized,"FAIL Reset accepts public cached pair");
    check(SURFACE(Release)(a)==0,"FAIL public back return"); p=*base;
    check(DEV(Reset)(device,&p)==blocked_reset && !trackers[0].finalized && !trackers[1].finalized,"FAIL Reset accepts public cached depth");
    check(SURFACE(Release)(b)==0 && refs((IUnknown*)device)==1,"FAIL public depth return"); p=*base;
    check(DEV(Reset)(device,&p)==D3D_OK && DEV(TestCooperativeLevel)(device)==D3D_OK,"FAIL Reset private-only bindings");
    check(!trackers[0].finalized && !trackers[1].finalized && trackers[0].refs==1 && trackers[1].refs==1,"FAIL existing Reset cached private retention");
#if D9WG_IMPLICIT_API == 8
    check(DEV(GetBackBuffer)(device,0,D3DBACKBUFFER_TYPE_MONO,&a)==D3D_OK,"FAIL Reset cached back8");
#else
    check(DEV(GetBackBuffer)(device,0,0,D3DBACKBUFFER_TYPE_MONO,&a)==D3D_OK,"FAIL Reset cached back9");
#endif
    check(DEV(GetDepthStencilSurface)(device,&b)==D3D_OK && a==back && b==depth,"FAIL existing Reset cache identity");
    Surface* preserved[]={a,b};
    for(UINT i=0;i<2;i++) { IUnknown* object=NULL;DWORD size=sizeof(object);check(SURFACE(GetPrivateData)(preserved[i],&object_key,&object,&size)==D3D_OK && object==&trackers[i].iface && IUnknown_Release(object)==1,"FAIL existing Reset private cache"); }
    empty_public(a,b);
    check(DEV(Release)(device)==0 && refs((IUnknown*)factory)==1,"FAIL Reset device cleanup"); finalized();device=NULL;
    report("IMPLICIT RESET refused=2 accepted=1 privateRetained=2 cachedIdentity=1 finalPrivateFinalized=2 callbackRefs=1 factoryRefs=1");
    create(window,base); pair(&back,&depth);bind(back,depth);empty_public(back,depth);
    check(DEV(Release)(device)==0 && refs((IUnknown*)factory)==1,"FAIL only-bound final cleanup");finalized();device=NULL;
    report("IMPLICIT FINAL publicRefs=0 privateFinalized=2 callbackRefs=1 factoryRefs=1");
    create(window,base); pair(&back,&depth);bind(back,depth);
    check(DEV(Release)(device)==2 && !trackers[0].finalized && !trackers[1].finalized,"FAIL device-first retained public pair");
    check(SURFACE(Release)(back)==0 && !trackers[0].finalized && !trackers[1].finalized,"FAIL last depth preserves stores");
    check(SURFACE(Release)(depth)==0 && refs((IUnknown*)factory)==1,"FAIL last public cleanup");finalized();device=NULL;
    report("IMPLICIT LAST publicRefs=0 privateFinalized=2 callbackRefs=1 factoryRefs=1");
    report("IMPLICIT BIND STATUS devices=3 reset=1 refusedReset=2 frames=2 revivals=64 privateFinalized=6 publicBindingRefs=0 factoryRefs=1");
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], path[MAX_PATH], name[100];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    UINT at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    wsprintfA(name, "IBD%u.TXT", D9WG_IMPLICIT_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    RECT client = { 0, 0, 32, 24 };
    check(AdjustWindowRectEx(&client, WS_POPUP, FALSE, 0), "FAIL window dimensions");
    HWND window = CreateWindowExA(0, "STATIC", "D3D implicit private binding lifetime", WS_POPUP, 30, 40, client.right - client.left, client.bottom - client.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
    check(window != NULL, "FAIL window");
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    RECT actual;
    check(GetClientRect(window, &actual) && actual.right == 32 && actual.bottom == 24, "FAIL exact client dimensions");
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
    pp.BackBufferWidth = 32;
    pp.BackBufferHeight = 24;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.EnableAutoDepthStencil = TRUE;
    pp.AutoDepthStencilFormat = D3DFMT_D24S8;
    callback_cases(window,&pp);
    check(!device && refs((IUnknown*)factory) == 1, "FAIL final target roots");
    check(FACTORY_RELEASE(factory) == 0, "FAIL final revival factory references");
    FreeLibrary(module);
    DestroyWindow(window);
    wsprintfA(name, "PASS implicit private bindings API=%u", D9WG_IMPLICIT_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
