#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <initguid.h>
#include <d3d9.h>
static HANDLE logfile;UINT rt_frame_index;
void rt_report(const char*s){DWORD n;WriteFile(logfile,s,lstrlenA(s),&n,NULL);WriteFile(logfile,"\r\n",2,&n,NULL);FlushFileBuffers(logfile);}
void rt_probe8(HMODULE,HWND,UINT,BOOL,UINT);
typedef IDirect3DDevice9 RTDevice;typedef IDirect3DSurface9 RTSurface;typedef IDirect3DSwapChain9 RTChain;
#define RT_RELEASE_CHAIN IDirect3DSwapChain9_Release
#define RT_SET(d,s) IDirect3DDevice9_SetRenderTarget(d,0,s)
#define RT_GET(d,s) IDirect3DDevice9_GetRenderTarget(d,0,s)
#define RT_CLEAR(d) IDirect3DDevice9_Clear(d,0,NULL,D3DCLEAR_TARGET,0xffff0000,1,0)
static HRESULT(WINAPI*core_target)(RTDevice*,UINT,UINT,UINT,UINT,BOOL,RTSurface**);
static HRESULT make_target(RTDevice*d,RTSurface**out){return core_target?core_target(d,8,8,21,0,TRUE,out):IDirect3DDevice9_CreateRenderTarget(d,8,8,D3DFMT_A8R8G8B8,0,0,TRUE,out,NULL);}
#define RT_MAKE make_target
#define RT_FVF IDirect3DDevice9_SetFVF
#define RT_RS IDirect3DDevice9_SetRenderState
#define RT_TSS IDirect3DDevice9_SetTextureStageState
#define RT_BEGIN IDirect3DDevice9_BeginScene
#define RT_END IDirect3DDevice9_EndScene
#define RT_DRAW IDirect3DDevice9_DrawPrimitiveUP
#define RT_RELEASE_DEVICE IDirect3DDevice9_Release
#define RT_RELEASE_SURFACE IDirect3DSurface9_Release
#define RT_DESC IDirect3DSurface9_GetDesc
#define RT_LOCK IDirect3DSurface9_LockRect
#define RT_UNLOCK IDirect3DSurface9_UnlockRect
#define RT_COOPERATIVE IDirect3DDevice9_TestCooperativeLevel
#define RT_BACK(d,out) IDirect3DDevice9_GetBackBuffer(d,0,0,D3DBACKBUFFER_TYPE_MONO,out)
static HRESULT read9(RTDevice*d,RTSurface*back,BOOL locked){RTSurface*cpu=NULL;HRESULT hr=IDirect3DDevice9_CreateOffscreenPlainSurface(d,8,8,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&cpu,NULL);if(FAILED(hr))return hr;hr=IDirect3DDevice9_GetRenderTargetData(d,back,cpu);if(!locked&&SUCCEEDED(hr)){D3DLOCKED_RECT data;hr=IDirect3DSurface9_LockRect(cpu,&data,NULL,D3DLOCK_READONLY);if(SUCCEEDED(hr))hr=IDirect3DSurface9_UnlockRect(cpu);}IDirect3DSurface9_Release(cpu);return hr;}
#define RT_READ read9
#include "d3d_renderable_lock_cases.h"
static HRESULT rt_reset_device(RTContext*c,UINT w,UINT h,DWORD flags)
{
 if(c->core){HRESULT(WINAPI*reset)(RTDevice*,const void*)=(void*)(uintptr_t)GetProcAddress(c->module,"Core8Reset");DWORD pp[13]={0};pp[0]=w;pp[1]=h;pp[2]=21;pp[3]=1;pp[5]=1;pp[6]=(DWORD)(uintptr_t)c->window;pp[7]=1;pp[10]=flags;return reset(c->d,pp);}
 D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=w;pp.BackBufferHeight=h;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=c->window;pp.Windowed=TRUE;pp.Flags=flags;return IDirect3DDevice9_Reset(c->d,&pp);
}
static HRESULT rt_additional(RTContext*c,RTChain**out){if(c->core){HRESULT(WINAPI*create)(RTDevice*,const void*,RTChain**)=(void*)(uintptr_t)GetProcAddress(c->module,"Core8Additional");DWORD pp[13]={8,8,21,1,0,1,0,1,0,0,1,0,0};pp[6]=(DWORD)(uintptr_t)c->window;return create(c->d,pp,out);}D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=pp.BackBufferHeight=8;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=c->window;pp.Windowed=TRUE;pp.Flags=1;return IDirect3DDevice9_CreateAdditionalSwapChain(c->d,&pp,out);}
static HRESULT rt_chain_back(RTChain*s,RTSurface**out){return IDirect3DSwapChain9_GetBackBuffer(s,0,D3DBACKBUFFER_TYPE_MONO,out);}
static void probe9(HMODULE module,HWND window,BOOL baseline,BOOL core,UINT mode)
{
 IDirect3D9*(WINAPI*make)(UINT)=(void*)(uintptr_t)GetProcAddress(module,core?"Core8CreateFactory":"Direct3DCreate9");rt_check(make!=NULL,"FAIL 9 factory export");IDirect3D9*root=make(core?220:D3D_SDK_VERSION);rt_check(root!=NULL,"FAIL 9 root");RTContext c={.window=window,.module=module,.mode=mode,.api=core?18:9,.sdk=core?220:D3D_SDK_VERSION,.baseline=baseline,.core=core};
 if(core){HRESULT(WINAPI*create)(IDirect3D9*,UINT,UINT,HWND,DWORD,const void*,RTDevice**)=(void*)(uintptr_t)GetProcAddress(module,"Core8CreateDevice");DWORD pp[13]={8,8,21,1,0,1,0,1,0,0,0,0,0};pp[6]=(DWORD)(uintptr_t)window;rt_expect(create(root,0,1,window,32,pp,&c.d),"FAIL core8 device");}
 else{D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=pp.BackBufferHeight=8;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.Windowed=TRUE;rt_expect(IDirect3D9_CreateDevice(root,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&c.d),"FAIL ordinary9 device");}
 if(core){core_target=(void*)(uintptr_t)GetProcAddress(module,"Core8RenderTarget");rt_check(core_target!=NULL,"FAIL private target export");for(UINT n=0;n<13;n++){UINT w=n==0?0:n==1?4097:n==2?0xffffffffu:8,h=n==3?0:n==4?4097:8,f=n==5?0:n==6?113:n==7?200:21,ms=n==9?2:n==10?16:n>=11?4:0;BOOL lockable=n==12?-1:TRUE;RTSurface*out=(void*)99;rt_check(core_target(c.d,w,h,f,ms,lockable,n==8?NULL:&out)==D3DERR_INVALIDCALL&&(n>=8?out==(void*)99:out==NULL),"FAIL private target parameter/output guard");}rt_report("RTCORE CHECK invalidCases=13 sdkABI=8 profile=8 earlyOutput=1");}
 rt_check(IDirect3D9_Release(root)==1,"FAIL early9 root release");rt_cases(&c);core_target=NULL;
}
void WINAPI WinMainCRTStartup(void)
{
 char directory[MAX_PATH],path[MAX_PATH];GetModuleFileNameA(NULL,directory,sizeof(directory));UINT at=lstrlenA(directory);while(at&&directory[at-1]!='\\')at--;directory[at]=0;lstrcpyA(path,directory);lstrcatA(path,"RTLOCK.TXT");logfile=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);rt_check(logfile!=INVALID_HANDLE_VALUE,"FAIL log");HWND window=CreateWindowExA(0,"STATIC","Renderable target locks",WS_POPUP|WS_VISIBLE,0,0,8,8,NULL,NULL,GetModuleHandleA(NULL),NULL);rt_check(window!=NULL,"FAIL window");HMODULE modules[5];const char*names[]={"BASE8.DLL","BASE9.DLL","D3D8.DLL","D3D9.DLL","CORE8.DLL"};for(UINT i=0;i<5;i++){lstrcpyA(path,directory);lstrcatA(path,names[i]);modules[i]=LoadLibraryA(path);rt_check(modules[i]!=NULL,"FAIL module");}
 rt_probe8(modules[0],window,220,TRUE,0);probe9(modules[1],window,TRUE,FALSE,1);rt_probe8(modules[2],window,220,FALSE,2);probe9(modules[3],window,FALSE,FALSE,3);probe9(modules[4],window,FALSE,TRUE,4);for(UINT i=0;i<5;i++)rt_check(FreeLibrary(modules[i]),"FAIL module release");rt_report("PASS renderable locks modes=5 devices=5 resets=4 frames=46 finalRefs=0");DestroyWindow(window);CloseHandle(logfile);ExitProcess(0);
}
