/* D3D9 SDK stays in its own compilation unit while the main probe uses D3D8. */
#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <initguid.h>
#include <d3d9.h>
extern void factory_core_report(const char*message);
extern void factory_core_check(BOOL ok,const char*message);
static void expect(HRESULT hr,const char*message){factory_core_check(SUCCEEDED(hr),message);}
static HMODULE baseline_module;static IDirect3D9*baseline_root;
static BOOL same_bytes(const void*a,const void*b,UINT bytes){for(UINT i=0;i<bytes;i++){if(((const BYTE*)a)[i]!=((const BYTE*)b)[i])return FALSE;}return TRUE;}
static HMODULE module;static IDirect3D9*root;static IDirect3DDevice9*d;static IDirect3DSurface9*cpu;
void factory_core9_open(const char*directory,HWND window)
{
    char path[MAX_PATH];lstrcpyA(path,directory);lstrcatA(path,"D3D9.DLL");module=LoadLibraryA(path);factory_core_check(module!=NULL,"FAIL D9 module");IDirect3D9*(WINAPI*make)(UINT)=(void*)(uintptr_t)GetProcAddress(module,"Direct3DCreate9");factory_core_check(make!=NULL,"FAIL D9 factory export");root=make(32);factory_core_check(root!=NULL,"FAIL D9 factory");IUnknown*view=NULL;expect(IDirect3D9_QueryInterface(root,&IID_IUnknown,(void**)&view),"FAIL D9 QI");factory_core_check((void*)view==root&&IUnknown_Release(view)==1,"FAIL D9 root identity");
    lstrcpyA(path,directory);lstrcatA(path,"BASE9.DLL");baseline_module=LoadLibraryA(path);factory_core_check(baseline_module!=NULL,"FAIL D9 baseline module");IDirect3D9*(WINAPI*old_make)(UINT)=(void*)(uintptr_t)GetProcAddress(baseline_module,"Direct3DCreate9");factory_core_check(old_make!=NULL,"FAIL D9 baseline export");baseline_root=old_make(32);factory_core_check(baseline_root!=NULL,"FAIL D9 baseline root");
    const DWORD flags[]={0,D3DENUM_WHQL_LEVEL,0xffffffffu,0x80000000u};for(UINT i=0;i<4;i++){D3DADAPTER_IDENTIFIER9 actual,old;expect(IDirect3D9_GetAdapterIdentifier(root,0,flags[i],&actual),"FAIL D9 identifier");expect(IDirect3D9_GetAdapterIdentifier(baseline_root,0,flags[i],&old),"FAIL D9 baseline identifier");factory_core_check(same_bytes(&actual,&old,sizeof(actual)),"FAIL D9 identifier changed");}
    D3DDISPLAYMODE mode,old_mode;expect(IDirect3D9_GetAdapterDisplayMode(root,0,&mode),"FAIL D9 mode");expect(IDirect3D9_GetAdapterDisplayMode(baseline_root,0,&old_mode),"FAIL D9 baseline mode");factory_core_check(same_bytes(&mode,&old_mode,sizeof(mode)),"FAIL D9 mode changed");for(UINT i=0;i<2;i++){void*callback=(void*)(uintptr_t)i;factory_core_check(IDirect3D9_RegisterSoftwareDevice(root,callback)==D3DERR_INVALIDCALL&&IDirect3D9_RegisterSoftwareDevice(baseline_root,callback)==D3DERR_INVALIDCALL,"FAIL D9 software result");}factory_core_report("MODESCORE D9 identityQueries=4 modeQueries=1 softwareQueries=2 baseline=4K4 bytes=1100");
    D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=pp.BackBufferHeight=8;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.Windowed=TRUE;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;expect(IDirect3D9_CreateDevice(root,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&d),"FAIL D9 device");IDirect3D9*parent=NULL;expect(IDirect3DDevice9_GetDirect3D(d,&parent),"FAIL D9 parent");factory_core_check(parent==root&&IDirect3D9_Release(parent)==2,"FAIL D9 parent identity");expect(IDirect3DDevice9_CreateOffscreenPlainSurface(d,8,8,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&cpu,NULL),"FAIL D9 CPU");
    expect(IDirect3DDevice9_SetFVF(d,68),"FAIL D9 FVF");expect(IDirect3DDevice9_SetRenderState(d,D3DRS_ZENABLE,FALSE),"FAIL D9 Z");expect(IDirect3DDevice9_SetRenderState(d,D3DRS_LIGHTING,FALSE),"FAIL D9 lighting");expect(IDirect3DDevice9_SetRenderState(d,D3DRS_CULLMODE,D3DCULL_NONE),"FAIL D9 cull");expect(IDirect3DDevice9_SetTextureStageState(d,0,D3DTSS_COLOROP,D3DTOP_SELECTARG2),"FAIL D9 color op");expect(IDirect3DDevice9_SetTextureStageState(d,0,D3DTSS_COLORARG2,D3DTA_DIFFUSE),"FAIL D9 color arg");expect(IDirect3DDevice9_SetTextureStageState(d,0,D3DTSS_ALPHAOP,D3DTOP_SELECTARG2),"FAIL D9 alpha op");expect(IDirect3DDevice9_SetTextureStageState(d,0,D3DTSS_ALPHAARG2,D3DTA_DIFFUSE),"FAIL D9 alpha arg");
}
void factory_core9_draw(DWORD color)
{
    typedef struct{float x,y,z,w;DWORD color;}Vertex;Vertex quad[4]={{-.5f,-.5f,.5f,1,color},{7.5f,-.5f,.5f,1,color},{-.5f,7.5f,.5f,1,color},{7.5f,7.5f,.5f,1,color}},v[6];const UINT order[6]={0,1,2,2,1,3};for(UINT i=0;i<6;i++)v[i]=quad[order[i]];
    expect(IDirect3DDevice9_BeginScene(d),"FAIL D9 scene");expect(IDirect3DDevice9_DrawPrimitiveUP(d,D3DPT_TRIANGLELIST,2,v,20),"FAIL D9 draw");expect(IDirect3DDevice9_EndScene(d),"FAIL D9 end");IDirect3DSurface9*target=NULL;expect(IDirect3DDevice9_GetRenderTarget(d,0,&target),"FAIL D9 target");expect(IDirect3DDevice9_GetRenderTargetData(d,target,cpu),"FAIL D9 readback");IDirect3DSurface9_Release(target);D3DLOCKED_RECT lock;expect(IDirect3DSurface9_LockRect(cpu,&lock,NULL,D3DLOCK_READONLY),"FAIL D9 lock");expect(IDirect3DSurface9_UnlockRect(cpu),"FAIL D9 unlock");
}
void factory_core9_close(void)
{
    factory_core_check(!IDirect3DSurface9_Release(cpu)&&!IDirect3DDevice9_Release(d)&&!IDirect3D9_Release(root),"FAIL D9 final refs");factory_core_check(FreeLibrary(module),"FAIL D9 unload");factory_core_check(!IDirect3D9_Release(baseline_root)&&FreeLibrary(baseline_module),"FAIL D9 baseline final refs");
}
