#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <d3d9.h>
typedef IDirect3DDevice9 UPDevice;
typedef IDirect3DBaseTexture9 UPBase;
#include "d3d8_backend_update_cases.h"
static HRESULT up_lock(UPBase*t,UINT kind,UINT face,DWORD flags,void**bits)
{
    HRESULT hr;if(kind==0){D3DLOCKED_RECT r;hr=IDirect3DTexture9_LockRect((IDirect3DTexture9*)t,0,&r,NULL,flags);if(SUCCEEDED(hr)){up_check(r.Pitch==4,"FAIL 2D pitch");*bits=r.pBits;}}
    else if(kind==1){D3DLOCKED_RECT r;hr=IDirect3DCubeTexture9_LockRect((IDirect3DCubeTexture9*)t,face,0,&r,NULL,flags);if(SUCCEEDED(hr)){up_check(r.Pitch==4,"FAIL cube pitch");*bits=r.pBits;}}
    else {D3DLOCKED_BOX b;hr=IDirect3DVolumeTexture9_LockBox((IDirect3DVolumeTexture9*)t,0,&b,NULL,flags);if(SUCCEEDED(hr)){up_check(b.RowPitch==4&&b.SlicePitch==4,"FAIL volume pitch");*bits=b.pBits;}}return hr;
}
static HRESULT up_unlock(UPBase*t,UINT kind,UINT face){return kind==0?IDirect3DTexture9_UnlockRect((IDirect3DTexture9*)t,0):kind==1?IDirect3DCubeTexture9_UnlockRect((IDirect3DCubeTexture9*)t,face,0):IDirect3DVolumeTexture9_UnlockBox((IDirect3DVolumeTexture9*)t,0);}
static void up_drop(UPBase*t){up_check(IDirect3DBaseTexture9_Release(t)==0,"FAIL texture final refs");}
static HRESULT(WINAPI*private_update)(UPDevice*,UPBase*,UPBase*);
static HRESULT up_make(UPContext*c,UINT kind,UINT pool,UPBase**out)
{
    DWORD usage=pool==0?D3DUSAGE_DYNAMIC:0;
    if(c->mode>2){if(kind==0){HRESULT(WINAPI*make)(UPDevice*,UINT,UINT,UINT,DWORD,UINT,UINT,UPBase**)=(void*)(uintptr_t)GetProcAddress(c->module,"Core8Texture");up_check(make!=NULL,"FAIL texture export");return make(c->d,1,1,1,usage,21,pool,out);}if(kind==1){HRESULT(WINAPI*make)(UPDevice*,UINT,UINT,DWORD,UINT,UINT,UPBase**)=(void*)(uintptr_t)GetProcAddress(c->module,"Core8CubeTexture");up_check(make!=NULL,"FAIL cube export");return make(c->d,1,1,usage,21,pool,out);}HRESULT(WINAPI*make)(UPDevice*,UINT,UINT,UINT,UINT,DWORD,UINT,UINT,UPBase**)=(void*)(uintptr_t)GetProcAddress(c->module,"Core8VolumeTexture");up_check(make!=NULL,"FAIL volume export");return make(c->d,1,1,1,1,usage,21,pool,out);}
    return kind==0?IDirect3DDevice9_CreateTexture(c->d,1,1,1,usage,21,pool,(IDirect3DTexture9**)out,NULL):kind==1?IDirect3DDevice9_CreateCubeTexture(c->d,1,1,usage,21,pool,(IDirect3DCubeTexture9**)out,NULL):IDirect3DDevice9_CreateVolumeTexture(c->d,1,1,1,1,usage,21,pool,(IDirect3DVolumeTexture9**)out,NULL);
}
static HRESULT up_update(UPContext*c,UPBase*s,UPBase*t){if(c->mode>2)return private_update(c->d,s,t);up_check(private_update(c->d,s,t)==D3DERR_INVALIDCALL,"FAIL private UpdateTexture wrong profile");return IDirect3DDevice9_UpdateTexture(c->d,s,t);}
static void up_probe9(HMODULE module,HWND window,UINT sdk,UINT mode){IDirect3D9*(WINAPI*make)(UINT)=(void*)(uintptr_t)GetProcAddress(module,mode>2?"Core8CreateFactory":"Direct3DCreate9");up_check(make!=NULL,"FAIL factory9 export");IDirect3D9*r=make(sdk);up_check(r!=NULL,"FAIL factory9");UPContext c={.module=module,.mode=mode};D3DPRESENT_PARAMETERS p;ZeroMemory(&p,sizeof(p));p.BackBufferWidth=p.BackBufferHeight=8;p.BackBufferCount=1;p.BackBufferFormat=21;p.SwapEffect=1;p.Windowed=TRUE;p.hDeviceWindow=window;DWORD p8[13]={8,8,21,1,0,1,0,1,0,0,0,0,0};p8[6]=(DWORD)(uintptr_t)window;if(mode>2){HRESULT(WINAPI*create)(IDirect3D9*,UINT,UINT,HWND,DWORD,const void*,UPDevice**)=(void*)(uintptr_t)GetProcAddress(module,"Core8CreateDevice");up_check(create!=NULL,"FAIL device export");up_hr(create(r,0,1,window,32,p8,&c.d),"FAIL core8 device");}else up_hr(IDirect3D9_CreateDevice(r,0,1,window,32,&p,&c.d),"FAIL create9");up_check(IDirect3D9_Release(r)==1,"FAIL root9 release");up_cases(&c);if(mode>2){HRESULT(WINAPI*reset)(UPDevice*,const void*)=(void*)(uintptr_t)GetProcAddress(module,"Core8Reset");up_check(reset!=NULL,"FAIL reset export");up_hr(reset(c.d,p8),"FAIL Reset core");}else up_hr(IDirect3DDevice9_Reset(c.d,&p),"FAIL Reset9");up_check(IDirect3DDevice9_Release(c.d)==0,"FAIL final device9");up_log_result(&c);}
static HANDLE logfile;
void up_report(const char*s){DWORD n;WriteFile(logfile,s,lstrlenA(s),&n,NULL);WriteFile(logfile,"\r\n",2,&n,NULL);FlushFileBuffers(logfile);}
void up_check(BOOL ok,const char*s){if(!ok){up_report(s);ExitProcess(1);}}
void up_hr(HRESULT hr,const char*s){up_check(hr==D3D_OK,s);}
void up_probe8(HMODULE,HWND,UINT);
void WINAPI WinMainCRTStartup(void)
{
    char dir[MAX_PATH],file[MAX_PATH];GetModuleFileNameA(NULL,dir,sizeof(dir));UINT at=lstrlenA(dir);while(at&&dir[at-1]!='\\')at--;dir[at]=0;lstrcpyA(file,dir);lstrcatA(file,"UPCORE.TXT");logfile=CreateFileA(file,GENERIC_WRITE,FILE_SHARE_READ,NULL,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,NULL);up_check(logfile!=INVALID_HANDLE_VALUE,"FAIL log");HWND window=CreateWindowExA(0,"STATIC","Core8 UpdateTexture",WS_POPUP|WS_VISIBLE,0,0,8,8,NULL,NULL,GetModuleHandleA(NULL),NULL);up_check(window!=NULL,"FAIL window");const char*names[]={"BASE8.DLL","D3D8.DLL","D3D9.DLL","CORE8.DLL"};HMODULE modules[4];for(UINT n=0;n<4;n++){lstrcpyA(file,dir);lstrcatA(file,names[n]);modules[n]=LoadLibraryA(file);up_check(modules[n]!=NULL,"FAIL load");}private_update=(void*)(uintptr_t)GetProcAddress(modules[3],"Core8UpdateTexture");up_check(private_update!=NULL,"FAIL private update export");up_check(private_update(NULL,NULL,NULL)==D3DERR_INVALIDCALL,"FAIL null update device");up_probe8(modules[0],window,0);up_probe8(modules[1],window,1);up_probe9(modules[2],window,32,2);up_probe9(modules[3],window,120,3);up_probe9(modules[3],window,220,4);for(UINT n=0;n<4;n++)up_check(FreeLibrary(modules[n]),"FAIL unload");BOOL loaded=TRUE;for(UINT wait=0;wait<200&&loaded;wait++){loaded=FALSE;for(UINT n=0;n<4;n++){lstrcpyA(file,dir);lstrcatA(file,names[n]);if(GetModuleHandleA(file))loaded=TRUE;}if(loaded)Sleep(10);}up_check(!loaded,"FAIL modules still retained");up_report("PASS private UpdateTexture modes=5 devices=5 resets=5 poolCases=240 legacyWrongPools=45 correctedRejected=180 localWords=640 finalRefs=0");DestroyWindow(window);CloseHandle(logfile);ExitProcess(0);
}
