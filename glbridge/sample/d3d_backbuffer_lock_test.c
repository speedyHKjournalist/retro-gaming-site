#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <initguid.h>
#include <d3d9.h>
static HANDLE logfile;UINT bb_frame_index;
void bb_report(const char*s){DWORD n;WriteFile(logfile,s,lstrlenA(s),&n,NULL);WriteFile(logfile,"\r\n",2,&n,NULL);FlushFileBuffers(logfile);}
void bb_probe8(HMODULE,HWND,UINT,BOOL,UINT);
typedef IDirect3DDevice9 BBDevice;typedef IDirect3DSurface9 BBSurface;
#define BB_FVF IDirect3DDevice9_SetFVF
#define BB_RS IDirect3DDevice9_SetRenderState
#define BB_TSS IDirect3DDevice9_SetTextureStageState
#define BB_BEGIN IDirect3DDevice9_BeginScene
#define BB_END IDirect3DDevice9_EndScene
#define BB_DRAW IDirect3DDevice9_DrawPrimitiveUP
#define BB_RELEASE_DEVICE IDirect3DDevice9_Release
#define BB_RELEASE_SURFACE IDirect3DSurface9_Release
#define BB_DESC IDirect3DSurface9_GetDesc
#define BB_LOCK IDirect3DSurface9_LockRect
#define BB_UNLOCK IDirect3DSurface9_UnlockRect
#define BB_COOPERATIVE IDirect3DDevice9_TestCooperativeLevel
#define BB_BACK(d,out) IDirect3DDevice9_GetBackBuffer(d,0,0,D3DBACKBUFFER_TYPE_MONO,out)
static HRESULT baseline_read9(BBDevice*d,BBSurface*back){BBSurface*cpu=NULL;HRESULT hr=IDirect3DDevice9_CreateOffscreenPlainSurface(d,8,8,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&cpu,NULL);if(FAILED(hr))return hr;hr=IDirect3DDevice9_GetRenderTargetData(d,back,cpu);IDirect3DSurface9_Release(cpu);return hr;}
#define BB_BASELINE_READ baseline_read9
#include "d3d_backbuffer_lock_cases.h"
static HRESULT bb_reset_device(BBContext*c,UINT w,UINT h,DWORD flags)
{
 if(c->core){HRESULT(WINAPI*reset)(BBDevice*,const void*)=(void*)(uintptr_t)GetProcAddress(c->module,"Core8Reset");DWORD pp[13]={0};pp[0]=w;pp[1]=h;pp[2]=21;pp[3]=1;pp[5]=1;pp[6]=(DWORD)(uintptr_t)c->window;pp[7]=1;pp[10]=flags;return reset(c->d,pp);}
 D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=w;pp.BackBufferHeight=h;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=c->window;pp.Windowed=TRUE;pp.Flags=flags;return IDirect3DDevice9_Reset(c->d,&pp);
}
static void probe9(HMODULE module,HWND window,BOOL baseline,BOOL core,UINT mode)
{
 IDirect3D9*(WINAPI*make)(UINT)=(void*)(uintptr_t)GetProcAddress(module,core?"Core8CreateFactory":"Direct3DCreate9");bb_check(make!=NULL,"FAIL 9 factory export");IDirect3D9*root=make(core?220:D3D_SDK_VERSION);bb_check(root!=NULL,"FAIL 9 root");BBContext c={.window=window,.module=module,.mode=mode,.api=core?18:9,.sdk=core?220:D3D_SDK_VERSION,.baseline=baseline,.core=core,.msaa=core};
 if(core){HRESULT(WINAPI*create)(IDirect3D9*,UINT,UINT,HWND,DWORD,const void*,BBDevice**)=(void*)(uintptr_t)GetProcAddress(module,"Core8CreateDevice");bb_check(create!=NULL,"FAIL private create export");DWORD pp[13]={8,8,21,1,4,1,0,1,0,0,1,0,0};pp[6]=(DWORD)(uintptr_t)window;BBDevice*temp=NULL;bb_expect(create(root,0,1,window,32,pp,&temp),"FAIL core8 MSAA construction");BBSurface*back=NULL;bb_expect(BB_BACK(temp,&back),"FAIL core8 MSAA surface");D3DLOCKED_RECT out={123,(void*)99};bb_check(BB_LOCK(back,&out,NULL,D3DLOCK_READONLY)==D3DERR_INVALIDCALL&&out.Pitch==123&&out.pBits==(void*)99&&!BB_RELEASE_SURFACE(back)&&!BB_RELEASE_DEVICE(temp),"FAIL core8 MSAA lock/ref guard");pp[4]=pp[10]=0;bb_expect(create(root,0,1,window,32,pp,&c.d),"FAIL core8 main");}
 else{D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=pp.BackBufferHeight=8;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.Windowed=TRUE;pp.Flags=baseline?1:0;bb_expect(IDirect3D9_CreateDevice(root,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&c.d),"FAIL ordinary9 main");}
 bb_check(IDirect3D9_Release(root)==1,"FAIL early9 root release");bb_cases(&c);
}
void WINAPI WinMainCRTStartup(void)
{
 char directory[MAX_PATH],path[MAX_PATH];GetModuleFileNameA(NULL,directory,sizeof(directory));UINT at=lstrlenA(directory);while(at&&directory[at-1]!='\\')at--;directory[at]=0;lstrcpyA(path,directory);lstrcatA(path,"BBLOCK.TXT");logfile=CreateFileA(path,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);bb_check(logfile!=INVALID_HANDLE_VALUE,"FAIL log");HWND window=CreateWindowExA(0,"STATIC","Implicit backbuffer locks",WS_POPUP|WS_VISIBLE,0,0,8,8,NULL,NULL,GetModuleHandleA(NULL),NULL);bb_check(window!=NULL,"FAIL window");HMODULE modules[5];const char*names[]={"BASE8.DLL","BASE9.DLL","D3D8.DLL","D3D9.DLL","CORE8.DLL"};for(UINT i=0;i<5;i++){lstrcpyA(path,directory);lstrcatA(path,names[i]);modules[i]=LoadLibraryA(path);bb_check(modules[i]!=NULL,"FAIL module");}
 bb_probe8(modules[0],window,120,TRUE,0);bb_probe8(modules[0],window,220,TRUE,1);probe9(modules[1],window,TRUE,FALSE,2);bb_probe8(modules[2],window,120,FALSE,3);bb_probe8(modules[2],window,220,FALSE,4);probe9(modules[3],window,FALSE,FALSE,5);probe9(modules[4],window,FALSE,TRUE,6);for(UINT i=0;i<5;i++)bb_check(FreeLibrary(modules[i]),"FAIL module release");bb_report("PASS shared backbuffer locks modes=7 devices=10 resets=8 frames=27 finalRefs=0");DestroyWindow(window);CloseHandle(logfile);ExitProcess(0);
}
