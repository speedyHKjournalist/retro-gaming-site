#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <initguid.h>
#include <d3d9.h>
static HANDLE logfile;UINT dc_frame_index;
void dc_report(const char*s){DWORD n;WriteFile(logfile,s,lstrlenA(s),&n,NULL);WriteFile(logfile,"\r\n",2,&n,NULL);FlushFileBuffers(logfile);}
void dc_probe8(HMODULE,HWND,UINT);
typedef IDirect3DDevice9 DCDevice;typedef IDirect3DSurface9 DCSurface;
#define DC_RS IDirect3DDevice9_SetRenderState
#define DC_FVF IDirect3DDevice9_SetFVF
#define DC_TSS IDirect3DDevice9_SetTextureStageState
#define DC_BEGIN IDirect3DDevice9_BeginScene
#define DC_END IDirect3DDevice9_EndScene
#define DC_DRAW IDirect3DDevice9_DrawPrimitiveUP
#define DC_CLEAR IDirect3DDevice9_Clear
#define DC_DEPTH IDirect3DDevice9_SetDepthStencilSurface
#define DC_GET_DEPTH IDirect3DDevice9_GetDepthStencilSurface
#define DC_RELEASE_DEVICE IDirect3DDevice9_Release
#define DC_RELEASE_SURFACE IDirect3DSurface9_Release
#define DC_DESC IDirect3DSurface9_GetDesc
#define DC_PARENT IDirect3DSurface9_GetDevice
#define DC_LOCK IDirect3DSurface9_LockRect
#define DC_BACK(d,s) IDirect3DDevice9_GetBackBuffer(d,0,0,D3DBACKBUFFER_TYPE_MONO,s)
static HRESULT dc_bind(DCDevice*d,DCSurface*color,DCSurface*depth){HRESULT hr=IDirect3DDevice9_SetRenderTarget(d,0,color);return FAILED(hr)?hr:IDirect3DDevice9_SetDepthStencilSurface(d,depth);}
#define DC_BIND dc_bind
static HRESULT dc_read(DCDevice*d,DCSurface*target){DCSurface*cpu=NULL;HRESULT hr=IDirect3DDevice9_CreateOffscreenPlainSurface(d,8,8,21,D3DPOOL_SYSTEMMEM,&cpu,NULL);if(FAILED(hr))return hr;hr=IDirect3DDevice9_GetRenderTargetData(d,target,cpu);IDirect3DSurface9_Release(cpu);return hr;}
#define DC_READ dc_read
#include "d3d8_backend_depth_cases.h"
static HRESULT dc_make_depth(DCContext*c,UINT samples,DCSurface**out){if(c->core){HRESULT(WINAPI*make)(DCDevice*,UINT,UINT,UINT,UINT,DCSurface**)=(void*)(uintptr_t)GetProcAddress(c->module,"Core8DepthSurface");dc_check(make!=NULL,"FAIL private depth export");return make(c->d,8,8,75,samples,out);}return IDirect3DDevice9_CreateDepthStencilSurface(c->d,8,8,75,samples,0,FALSE,out,NULL);}
static HRESULT dc_make_target(DCContext*c,DCSurface**out){if(c->core){HRESULT(WINAPI*make)(DCDevice*,UINT,UINT,UINT,UINT,BOOL,DCSurface**)=(void*)(uintptr_t)GetProcAddress(c->module,"Core8RenderTarget");return make(c->d,8,8,21,0,FALSE,out);}return IDirect3DDevice9_CreateRenderTarget(c->d,8,8,21,0,0,FALSE,out,NULL);}
static HRESULT dc_reset(DCContext*c){if(c->core){HRESULT(WINAPI*reset)(DCDevice*,const void*)=(void*)(uintptr_t)GetProcAddress(c->module,"Core8Reset");DWORD p[13]={8,8,21,1,0,1,0,1,0,0,0,0,0};p[6]=(DWORD)(uintptr_t)c->window;return reset(c->d,p);}D3DPRESENT_PARAMETERS p;ZeroMemory(&p,sizeof(p));p.BackBufferWidth=p.BackBufferHeight=8;p.BackBufferCount=1;p.BackBufferFormat=21;p.SwapEffect=1;p.hDeviceWindow=c->window;p.Windowed=TRUE;return IDirect3DDevice9_Reset(c->d,&p);}
static void dc_probe9(HMODULE module,HWND window,UINT sdk,BOOL core,UINT mode){IDirect3D9*(WINAPI*make)(UINT)=(void*)(uintptr_t)GetProcAddress(module,core?"Core8CreateFactory":"Direct3DCreate9");dc_check(make!=NULL,"FAIL factory9 export");IDirect3D9*root=make(sdk);dc_check(root!=NULL,"FAIL root9");DCContext c={.window=window,.module=module,.mode=mode,.api=core?18:9,.sdk=sdk,.core=core};if(core){HRESULT(WINAPI*create)(IDirect3D9*,UINT,UINT,HWND,DWORD,const void*,DCDevice**)=(void*)(uintptr_t)GetProcAddress(module,"Core8CreateDevice");DWORD p[13]={8,8,21,1,0,1,0,1,0,0,0,0,0};p[6]=(DWORD)(uintptr_t)window;dc_expect(create(root,0,1,window,32,p,&c.d),"FAIL create core8");HRESULT(WINAPI*depth)(DCDevice*,UINT,UINT,UINT,UINT,DCSurface**)=(void*)(uintptr_t)GetProcAddress(module,"Core8DepthSurface");dc_check(depth!=NULL,"FAIL depth ABI export");for(UINT n=0;n<11;n++){UINT w=n==7?0:n==8?8193:8,h=n==9?0:n==10?8193:8,f=n==0?0:n==1?21:n==2?70:n==3?82:75,ms=n==4?2:n==5?16:0;DCSurface*out=(void*)99;dc_check(depth(c.d,w,h,f,ms,n==6?NULL:&out)==D3DERR_INVALIDCALL&&(n<=6?out==(void*)99:out==NULL),"FAIL depth guarded output");}}
else{D3DPRESENT_PARAMETERS p;ZeroMemory(&p,sizeof(p));p.BackBufferWidth=p.BackBufferHeight=8;p.BackBufferCount=1;p.BackBufferFormat=21;p.SwapEffect=1;p.hDeviceWindow=window;p.Windowed=TRUE;dc_expect(IDirect3D9_CreateDevice(root,0,1,window,32,&p,&c.d),"FAIL create9");}dc_check(IDirect3D9_Release(root)==1,"FAIL early root9");dc_cases(&c);}
void WINAPI WinMainCRTStartup(void){char dir[MAX_PATH],path[MAX_PATH];GetModuleFileNameA(NULL,dir,sizeof(dir));UINT at=lstrlenA(dir);while(at&&dir[at-1]!='\\')at--;dir[at]=0;lstrcpyA(path,dir);lstrcatA(path,"DEPTH8RT.TXT");logfile=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);dc_check(logfile!=INVALID_HANDLE_VALUE,"FAIL log");HWND window=CreateWindowExA(0,"STATIC","Core8 depth",WS_POPUP|WS_VISIBLE,0,0,8,8,NULL,NULL,GetModuleHandleA(NULL),NULL);dc_check(window!=NULL,"FAIL window");const char*names[]={"BASE8.DLL","D3D8.DLL","D3D9.DLL","CORE8.DLL"};HMODULE modules[4];for(UINT n=0;n<4;n++){lstrcpyA(path,dir);lstrcatA(path,names[n]);modules[n]=LoadLibraryA(path);dc_check(modules[n]!=NULL,"FAIL load");}dc_probe8(modules[0],window,0);dc_probe8(modules[1],window,1);dc_probe9(modules[2],window,32,FALSE,2);dc_probe9(modules[3],window,120,TRUE,3);dc_probe9(modules[3],window,220,TRUE,4);for(UINT n=0;n<4;n++)dc_check(FreeLibrary(modules[n]),"FAIL unload");dc_report("PASS core8 depth modes=5 devices=5 resets=5 frames=20 finalRefs=0");DestroyWindow(window);CloseHandle(logfile);ExitProcess(0);}
