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
static Device *device, *kept_device;
static Surface* cached_back;
static UINT order, queries, total_finalized, retained, reinserted;
static HANDLE logfile;
static void report(const char* text)
{
    DWORD written;
    WriteFile(logfile,text,lstrlenA(text),&written,NULL);
    WriteFile(logfile,"\r\n",2,&written,NULL);
    FlushFileBuffers(logfile);
}
static void check(BOOL condition, const char* message)
{
    if (!condition) { report(message); ExitProcess(1); }
}
static ULONG refs(IUnknown* object)
{
    IUnknown_AddRef(object);
    return IUnknown_Release(object);
}
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
    if (!--t->refs) {
        check(++t->finalized==1,"FAIL duplicate private finalizer");
        check(refs((IUnknown*)device)==1 && DEV(TestCooperativeLevel)(device)==D3D_OK,"FAIL protected finalizer device query");
        queries++; total_finalized++;
        if (t==&trackers[0] && order==3) {
            check(!kept_device && DEV(AddRef)(device)==2,"FAIL retained parent pin");
            kept_device=device; retained++;
        }
        if (t==&trackers[0] && order==4) {
            check(SURFACE(SetPrivateData)(cached_back,&object_key,&trackers[2].iface,sizeof(IUnknown*),D3DSPD_IUNKNOWN)==D3D_OK && IUnknown_Release(&trackers[2].iface)==1,"FAIL reentrant cache insertion");
            reinserted++;
        }
    }
    return t->refs;
}
static IUnknownVtbl tracker_vtbl={tracker_query,tracker_addref,tracker_release};
static void attach(Tracker* t, Surface* s)
{
    check(SURFACE(SetPrivateData)(s,&object_key,&t->iface,sizeof(IUnknown*),D3DSPD_IUNKNOWN)==D3D_OK && IUnknown_Release(&t->iface)==1,"FAIL private attach");
}
static void callback_cases(HWND window, const D3DPRESENT_PARAMETERS* base)
{
    for(order=0;order<5;order++) {
        D3DPRESENT_PARAMETERS p=*base;
#if D9WG_IMPLICIT_API == 8
        check(IDirect3D8_CreateDevice(factory,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&p,&device)==D3D_OK,"FAIL device8");
#else
        check(IDirect3D9_CreateDevice(factory,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&p,&device)==D3D_OK,"FAIL device9");
#endif
        check(device && refs((IUnknown*)device)==1,"FAIL initial device refs");
        Surface *back=NULL,*depth=NULL;
#if D9WG_IMPLICIT_API == 8
        check(DEV(GetBackBuffer)(device,0,D3DBACKBUFFER_TYPE_MONO,&back)==D3D_OK,"FAIL cached back8");
#else
        check(DEV(GetBackBuffer)(device,0,0,D3DBACKBUFFER_TYPE_MONO,&back)==D3D_OK,"FAIL cached back9");
#endif
        check(DEV(GetDepthStencilSurface)(device,&depth)==D3D_OK && back && depth,"FAIL cached depth");
        for(UINT i=0;i<3;i++) {
            ZeroMemory(&trackers[i],sizeof(trackers[i]));
            trackers[i].iface.lpVtbl=&tracker_vtbl; trackers[i].refs=1;
        }
        attach(&trackers[0],back); attach(&trackers[1],depth);
        cached_back=back;
        if(order==1) {
#if D9WG_IMPLICIT_API == 8
            check(DEV(SetRenderTarget)(device,back,depth)==D3D_OK,"FAIL owned implicit bindings8");
#else
            check(DEV(SetRenderTarget)(device,0,back)==D3D_OK && DEV(SetDepthStencilSurface)(device,depth)==D3D_OK,"FAIL owned implicit bindings9");
#endif
        }
        SURFACE(Release)(depth);
        if(order!=2) SURFACE(Release)(back);
        if(order==2) {
            check(DEV(Release)(device)==1 && !trackers[0].finalized && !trackers[1].finalized,"FAIL last public surface preserves cache");
            check(SURFACE(Release)(back)==0,"FAIL last public surface cleanup");
        } else if(order==3) {
            check(DEV(Release)(device)==1 && kept_device==device && trackers[0].finalized==1 && !trackers[1].finalized && trackers[1].refs==1 && refs((IUnknown*)factory)==2,"FAIL deferred depth store");
            Surface* preserved=NULL;
            check(DEV(GetDepthStencilSurface)(device,&preserved)==D3D_OK && preserved==depth,"FAIL retained cache identity");
            IUnknown* object=NULL; DWORD size=sizeof(object);
            check(SURFACE(GetPrivateData)(preserved,&object_key,&object,&size)==D3D_OK && object==&trackers[1].iface && IUnknown_Release(object)==1,"FAIL retained private data");
            SURFACE(Release)(preserved);
            check(DEV(Release)(kept_device)==0,"FAIL retained parent final cleanup"); kept_device=NULL;
        } else check(DEV(Release)(device)==0,"FAIL final cached device cleanup");
        check(!trackers[0].refs && !trackers[1].refs && trackers[0].finalized==1 && trackers[1].finalized==1,"FAIL exact cache private finalizers");
        if(order==4) check(!trackers[2].refs && trackers[2].finalized==1,"FAIL reentrant private finalizer");
        check(refs((IUnknown*)factory)==1,"FAIL final factory refs");
        device=NULL; cached_back=NULL;
        char line[200];
        wsprintfA(line,"IMPLICIT CALLBACK order=%u finalized=%u queryDeviceRefs=1 retainedParent=%u reinserted=%u factoryRefs=1",order,order==4?3:2,order==3?1:0,order==4?1:0); report(line);
    }
    check(total_finalized==11 && queries==11 && retained==1 && reinserted==1,"FAIL callback totals");
    report("IMPLICIT STATUS devices=5 privateFinalized=11 protectedQueries=11 retainedParent=1 reinserted=1 factoryRefs=1");
}
void WINAPI WinMainCRTStartup(void)
{
    char directory[MAX_PATH], path[MAX_PATH], name[100];
    GetModuleFileNameA(NULL, directory, sizeof(directory));
    UINT at = lstrlenA(directory);
    while (at && directory[at - 1] != '\\')
        --at;
    directory[at] = 0;
    wsprintfA(name, "IPC%u.TXT", D9WG_IMPLICIT_API);
    lstrcpyA(path, directory);
    lstrcatA(path, name);
    logfile = CreateFileA(path, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    check(logfile != INVALID_HANDLE_VALUE, "FAIL log");
    RECT client = { 0, 0, 32, 24 };
    check(AdjustWindowRectEx(&client, WS_POPUP, FALSE, 0), "FAIL window dimensions");
    HWND window = CreateWindowExA(0, "STATIC", "D3D implicit private callback lifetime", WS_POPUP, 30, 40, client.right - client.left, client.bottom - client.top, NULL, NULL, GetModuleHandleA(NULL), NULL);
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
    wsprintfA(name, "PASS implicit private callbacks API=%u", D9WG_IMPLICIT_API);
    report(name);
    CloseHandle(logfile);
    ExitProcess(0);
}
