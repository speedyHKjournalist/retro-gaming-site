#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <initguid.h>
#include <d3d9.h>
static HANDLE logfile;UINT tc_frame_index;void tc_report(const char*s){DWORD n;WriteFile(logfile,s,lstrlenA(s),&n,NULL);WriteFile(logfile,"\r\n",2,&n,NULL);FlushFileBuffers(logfile);}
void tc_probe8(HMODULE,HWND,UINT);
#define TCDevice IDirect3DDevice9
#define TCTexture IDirect3DVolumeTexture9
#define TCSurface IDirect3DSurface9
#define TCDesc D3DSURFACE_DESC
#define TCView IDirect3DVolume9
#define TCVolumeDesc D3DVOLUME_DESC
#define TC_VPARENT IDirect3DVolume9_GetDevice
#define TC_VDESC IDirect3DVolume9_GetDesc
#define TC_VREL IDirect3DVolume9_Release
#define TC_VLOCK IDirect3DVolume9_LockBox
#define TC_VUNLOCK IDirect3DVolume9_UnlockBox
#define TC_VIEW IDirect3DVolumeTexture9_GetVolumeLevel
#define TC_PARENT IDirect3DSurface9_GetDevice
#define TC_DESC IDirect3DSurface9_GetDesc
#define TC_SREL IDirect3DSurface9_Release
#define TC_DREL IDirect3DDevice9_Release
#define TC_TREL IDirect3DVolumeTexture9_Release
#define TC_LEVELS IDirect3DVolumeTexture9_GetLevelCount
#define TC_LOCK IDirect3DVolumeTexture9_LockBox
#define TC_UNLOCK IDirect3DVolumeTexture9_UnlockBox
#define TC_SLOCK IDirect3DSurface9_LockRect
#define TC_SUNLOCK IDirect3DSurface9_UnlockRect
#define TC_RS IDirect3DDevice9_SetRenderState
#define TC_TSS IDirect3DDevice9_SetTextureStageState
#define TC_CLEAR IDirect3DDevice9_Clear
#define TC_BEGIN IDirect3DDevice9_BeginScene
#define TC_END IDirect3DDevice9_EndScene
#define TC_DRAW IDirect3DDevice9_DrawPrimitiveUP
#include "d3d8_backend_volume_cases.h"
static HRESULT tc_create(TCContext*c,UINT w,UINT h,UINT depth,UINT levels,DWORD usage,UINT format,UINT pool,TCTexture**out){if(c->core){HRESULT(WINAPI*make)(TCDevice*,UINT,UINT,UINT,UINT,DWORD,UINT,UINT,TCTexture**)=(void*)(uintptr_t)GetProcAddress(c->module,"Core8VolumeTexture");tc_check(make!=NULL,"FAIL private texture export");return make(c->d,w,h,depth,levels,usage,format,pool,out);}return IDirect3DDevice9_CreateVolumeTexture(c->d,w,h,depth,levels,usage,format,pool,out,NULL);}
static void tc_size(TCContext*c,TCView*s,UINT bytes){if(c->core){HRESULT(WINAPI*get)(TCView*,void*,UINT)=(void*)(uintptr_t)GetProcAddress(c->module,"Core8VolumeDesc");DWORD out[8];tc_check(get!=NULL,"FAIL descriptor8 export");tc_expect(get(s,out,32),"FAIL descriptor8");tc_check(out[4]==bytes,"FAIL core SDK8 texture Size");}else{(void)s;(void)bytes;}}
static void tc_sampler(TCContext*c){tc_expect(IDirect3DDevice9_SetSamplerState(c->d,0,D3DSAMP_MINFILTER,1),"FAIL min");tc_expect(IDirect3DDevice9_SetSamplerState(c->d,0,D3DSAMP_MAGFILTER,1),"FAIL mag");tc_expect(IDirect3DDevice9_SetSamplerState(c->d,0,D3DSAMP_MIPFILTER,0),"FAIL mip");}
static HRESULT tc_fvf(TCDevice*d){return IDirect3DDevice9_SetFVF(d,D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1|D3DFVF_TEXCOORDSIZE3(0));}
static HRESULT tc_bind(TCContext*c,TCTexture*t){return IDirect3DDevice9_SetTexture(c->d,0,(IDirect3DBaseTexture9*)t);}
static void tc_read(TCContext*c){TCSurface*cpu=NULL,*back=NULL;tc_expect(IDirect3DDevice9_CreateOffscreenPlainSurface(c->d,8,8,21,2,&cpu,NULL),"FAIL CPU9");tc_expect(IDirect3DDevice9_GetBackBuffer(c->d,0,0,D3DBACKBUFFER_TYPE_MONO,&back),"FAIL back9");tc_expect(IDirect3DDevice9_GetRenderTargetData(c->d,back,cpu),"FAIL read9");;D3DLOCKED_RECT lock;tc_expect(TC_SLOCK(cpu,&lock,NULL,16),"FAIL texture CPU read");tc_check(lock.Pitch==32,"FAIL texture readback pitch");tc_expect(TC_SUNLOCK(cpu),"FAIL texture CPU unlock");TC_SREL(cpu);TC_SREL(back);}
static HRESULT tc_reset(TCContext*c){if(c->core){HRESULT(WINAPI*reset)(TCDevice*,const void*)=(void*)(uintptr_t)GetProcAddress(c->module,"Core8Reset");DWORD p[13]={8,8,21,1,0,1,0,1,0,0,0,0,0};return reset(c->d,p);}D3DPRESENT_PARAMETERS p;ZeroMemory(&p,sizeof(p));p.BackBufferWidth=p.BackBufferHeight=8;p.BackBufferFormat=21;p.BackBufferCount=1;p.SwapEffect=1;p.Windowed=TRUE;return IDirect3DDevice9_Reset(c->d,&p);}
static void tc_probe9(HMODULE module,HWND window,UINT sdk,UINT mode){BOOL core=mode>2;IDirect3D9*(WINAPI*make)(UINT)=(void*)(uintptr_t)GetProcAddress(module,core?"Core8CreateFactory":"Direct3DCreate9");tc_check(make!=NULL,"FAIL root9 export");IDirect3D9*root=make(sdk);tc_check(root!=NULL,"FAIL root9");TCContext c={.module=module,.mode=mode,.core=core};if(core){HRESULT(WINAPI*create)(IDirect3D9*,UINT,UINT,HWND,DWORD,const void*,TCDevice**)=(void*)(uintptr_t)GetProcAddress(module,"Core8CreateDevice");tc_check(create!=NULL,"FAIL core device export");DWORD p[13]={8,8,21,1,0,1,0,1,0,0,0,0,0};p[6]=(DWORD)(uintptr_t)window;tc_expect(create(root,0,1,window,32,p,&c.d),"FAIL core device");}else{D3DPRESENT_PARAMETERS p;ZeroMemory(&p,sizeof(p));p.BackBufferWidth=p.BackBufferHeight=8;p.BackBufferFormat=21;p.BackBufferCount=1;p.SwapEffect=1;p.hDeviceWindow=window;p.Windowed=TRUE;tc_expect(IDirect3D9_CreateDevice(root,0,1,window,32,&p,&c.d),"FAIL create9");}tc_check(IDirect3D9_Release(root)==1,"FAIL early root9");tc_cases(&c);tc_check(!TC_DREL(c.d),"FAIL final device9");}
void WINAPI WinMainCRTStartup(void){char dir[MAX_PATH],file[MAX_PATH];GetModuleFileNameA(NULL,dir,sizeof(dir));UINT at=lstrlenA(dir);while(at&&dir[at-1]!='\\')at--;dir[at]=0;lstrcpyA(file,dir);lstrcatA(file,"VOLCORE.TXT");logfile=CreateFileA(file,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);tc_check(logfile!=INVALID_HANDLE_VALUE,"FAIL log");HWND window=CreateWindowExA(0,"STATIC","Core8 texture",WS_POPUP|WS_VISIBLE,0,0,8,8,NULL,NULL,GetModuleHandleA(NULL),NULL);tc_check(window!=NULL,"FAIL window");const char*names[]={"BASE8.DLL","D3D8.DLL","D3D9.DLL","CORE8.DLL"};HMODULE modules[4];for(UINT n=0;n<4;n++){lstrcpyA(file,dir);lstrcatA(file,names[n]);modules[n]=LoadLibraryA(file);tc_check(modules[n]!=NULL,"FAIL load");}tc_probe8(modules[0],window,0);tc_probe8(modules[1],window,1);tc_probe9(modules[2],window,32,2);tc_probe9(modules[3],window,120,3);tc_probe9(modules[3],window,220,4);for(UINT n=0;n<4;n++)tc_check(FreeLibrary(modules[n]),"FAIL unload");tc_report("PASS private volume8 modes=5 devices=5 resets=5 frames=30 finalRefs=0");DestroyWindow(window);CloseHandle(logfile);ExitProcess(0);}
