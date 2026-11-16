#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <d3d9.h>
typedef IDirect3DDevice9 RCDevice;typedef IDirect3DSurface9 RCSurface;typedef IDirect3DBaseTexture9 RCBase;
#define RC_CLEAR IDirect3DDevice9_Clear
#define RC_LOCK IDirect3DSurface9_LockRect
#define RC_UNLOCK IDirect3DSurface9_UnlockRect
#define RC_SREL IDirect3DSurface9_Release
#define RC_BREL IDirect3DBaseTexture9_Release
static HRESULT rc_copy(RCSurface*,const RECT*,UINT,RCSurface*,const POINT*);
static HRESULT rc_image(UINT,UINT,RCSurface**);
static HRESULT rc_texture(RCSurface**,RCBase**);
static HRESULT rc_cube(RCSurface**,RCBase**);
static HRESULT rc_back(RCSurface**);
#include "d3d8_backend_rect_cases.h"

static HRESULT rc_copy(RCSurface*s,const RECT*r,UINT n,RCSurface*t,const POINT*p){HRESULT(WINAPI*f)(RCDevice*,RCSurface*,const RECT*,UINT,RCSurface*,const POINT*)=(void*)(uintptr_t)GetProcAddress(rc_module,"Core8CopyRects");rc_check(f!=NULL,"FAIL copy export");return f(rc_device,s,r,n,t,p);}
static HRESULT rc_image(UINT w,UINT h,RCSurface**s){HRESULT(WINAPI*f)(RCDevice*,UINT,UINT,UINT,RCSurface**)=(void*)(uintptr_t)GetProcAddress(rc_module,"Core8ImageSurface");rc_check(f!=NULL,"FAIL image export");return f(rc_device,w,h,21,s);}
static HRESULT rc_texture(RCSurface**s,RCBase**p){HRESULT(WINAPI*f)(RCDevice*,UINT,UINT,UINT,DWORD,UINT,UINT,IDirect3DTexture9**)=(void*)(uintptr_t)GetProcAddress(rc_module,"Core8Texture");rc_check(f!=NULL,"FAIL texture export");IDirect3DTexture9*t=NULL;HRESULT hr=f(rc_device,8,8,1,0,21,0,&t);if(FAILED(hr))return hr;*p=(RCBase*)t;return IDirect3DTexture9_GetSurfaceLevel(t,0,s);}
static HRESULT rc_cube(RCSurface**s,RCBase**p){HRESULT(WINAPI*f)(RCDevice*,UINT,UINT,DWORD,UINT,UINT,IDirect3DCubeTexture9**)=(void*)(uintptr_t)GetProcAddress(rc_module,"Core8CubeTexture");rc_check(f!=NULL,"FAIL cube export");IDirect3DCubeTexture9*t=NULL;HRESULT hr=f(rc_device,8,2,0,21,0,&t);if(FAILED(hr))return hr;*p=(RCBase*)t;return IDirect3DCubeTexture9_GetCubeMapSurface(t,4,1,s);}
static HRESULT rc_back(RCSurface**s){return IDirect3DDevice9_GetBackBuffer(rc_device,0,0,0,s);}
static void rc_probe9(HMODULE module,HWND window,UINT sdk,UINT mode){rc_module=module;IDirect3D9*(WINAPI*make)(UINT)=(void*)(uintptr_t)GetProcAddress(module,"Core8CreateFactory");rc_check(make!=NULL,"FAIL factory9 export");IDirect3D9*r=make(sdk);rc_check(r!=NULL,"FAIL root9");DWORD p[13]={8,8,21,1,0,1,0,1,0,0,0,0,0};p[6]=(DWORD)(uintptr_t)window;HRESULT(WINAPI*create)(IDirect3D9*,UINT,UINT,HWND,DWORD,const void*,RCDevice**)=(void*)(uintptr_t)GetProcAddress(module,"Core8CreateDevice");rc_check(create!=NULL,"FAIL device export");rc_hr(create(r,0,1,window,32,p,&rc_device),"FAIL device9");rc_check(IDirect3D9_Release(r)==1,"FAIL root9 refs");rc_words=0;rc_cases();p[0]=5;p[1]=3;HRESULT(WINAPI*reset)(RCDevice*,const void*)=(void*)(uintptr_t)GetProcAddress(module,"Core8Reset");rc_check(reset!=NULL,"FAIL reset export");rc_hr(reset(rc_device,p),"FAIL reset9");rc_after_reset();rc_check(!IDirect3DDevice9_Release(rc_device),"FAIL final refs9");rc_check(rc_words==287,"FAIL word count9");char text[120];wsprintfA(text,"RECTCORE CHECK mode=%u sdk=%u localWords=287 reset=1 finalRefs=0",mode,sdk);rc_report(text);}
static HANDLE logfile;UINT rc_words;
void rc_report(const char*s){DWORD n;WriteFile(logfile,s,lstrlenA(s),&n,NULL);WriteFile(logfile,"\r\n",2,&n,NULL);FlushFileBuffers(logfile);}
void rc_check(BOOL c,const char*s){if(!c){rc_report(s);ExitProcess(1);}}
void rc_probe8(HMODULE,HWND,UINT);
void WINAPI WinMainCRTStartup(void){char dir[MAX_PATH],path[MAX_PATH];GetModuleFileNameA(NULL,dir,sizeof(dir));UINT at=lstrlenA(dir);while(at&&dir[at-1]!='\\')at--;dir[at]=0;lstrcpyA(path,dir);lstrcatA(path,"RECTCORE.TXT");logfile=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);rc_check(logfile!=INVALID_HANDLE_VALUE,"FAIL log");HWND w=CreateWindowExA(0,"STATIC","CopyRects core",WS_POPUP|WS_VISIBLE,0,0,8,8,NULL,NULL,GetModuleHandleA(NULL),NULL);rc_check(w!=NULL,"FAIL window");const char*names[]={"BASE8.DLL","D3D8.DLL","CORE8.DLL"};HMODULE modules[3];for(UINT n=0;n<3;n++){lstrcpyA(path,dir);lstrcatA(path,names[n]);modules[n]=LoadLibraryA(path);rc_check(modules[n]!=NULL,"FAIL load");}rc_probe8(modules[0],w,0);rc_probe8(modules[1],w,1);rc_probe9(modules[2],w,120,2);rc_probe9(modules[2],w,220,3);for(UINT n=0;n<3;n++)rc_check(FreeLibrary(modules[n]),"FAIL unload");BOOL loaded=TRUE;for(UINT i=0;i<200&&loaded;i++){loaded=FALSE;for(UINT n=0;n<3;n++){lstrcpyA(path,dir);lstrcatA(path,names[n]);if(GetModuleHandleA(path))loaded=TRUE;}if(loaded)Sleep(10);}rc_check(!loaded,"FAIL retained module");rc_report("PASS private CopyRects modes=4 devices=4 resets=4 localWords=1148 finalRefs=0");DestroyWindow(w);CloseHandle(logfile);ExitProcess(0);}
