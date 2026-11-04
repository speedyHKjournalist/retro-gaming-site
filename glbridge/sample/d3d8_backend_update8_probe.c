#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <d3d8.h>
typedef IDirect3DDevice8 UPDevice;
typedef IDirect3DBaseTexture8 UPBase;
#include "d3d8_backend_update_cases.h"
static HRESULT up_lock(UPBase*t,UINT kind,UINT face,DWORD flags,void**bits)
{
    HRESULT hr;if(kind==0){D3DLOCKED_RECT r;hr=IDirect3DTexture8_LockRect((IDirect3DTexture8*)t,0,&r,NULL,flags);if(SUCCEEDED(hr)){up_check(r.Pitch==4,"FAIL 2D pitch");*bits=r.pBits;}}
    else if(kind==1){D3DLOCKED_RECT r;hr=IDirect3DCubeTexture8_LockRect((IDirect3DCubeTexture8*)t,face,0,&r,NULL,flags);if(SUCCEEDED(hr)){up_check(r.Pitch==4,"FAIL cube pitch");*bits=r.pBits;}}
    else {D3DLOCKED_BOX b;hr=IDirect3DVolumeTexture8_LockBox((IDirect3DVolumeTexture8*)t,0,&b,NULL,flags);if(SUCCEEDED(hr)){up_check(b.RowPitch==4&&b.SlicePitch==4,"FAIL volume pitch");*bits=b.pBits;}}return hr;
}
static HRESULT up_unlock(UPBase*t,UINT kind,UINT face){return kind==0?IDirect3DTexture8_UnlockRect((IDirect3DTexture8*)t,0):kind==1?IDirect3DCubeTexture8_UnlockRect((IDirect3DCubeTexture8*)t,face,0):IDirect3DVolumeTexture8_UnlockBox((IDirect3DVolumeTexture8*)t,0);}
static void up_drop(UPBase*t){up_check(IDirect3DBaseTexture8_Release(t)==0,"FAIL texture final refs");}
static HRESULT up_make(UPContext*c,UINT kind,UINT pool,UPBase**out){DWORD usage=pool==0?D3DUSAGE_DYNAMIC:0;return kind==0?IDirect3DDevice8_CreateTexture(c->d,1,1,1,usage,21,pool,(IDirect3DTexture8**)out):kind==1?IDirect3DDevice8_CreateCubeTexture(c->d,1,1,usage,21,pool,(IDirect3DCubeTexture8**)out):IDirect3DDevice8_CreateVolumeTexture(c->d,1,1,1,1,usage,21,pool,(IDirect3DVolumeTexture8**)out);}
static HRESULT up_update(UPContext*c,UPBase*s,UPBase*t){return IDirect3DDevice8_UpdateTexture(c->d,s,t);}
void up_probe8(HMODULE module,HWND window,UINT mode){IDirect3D8*(WINAPI*make)(UINT)=(void*)(uintptr_t)GetProcAddress(module,"Direct3DCreate8");up_check(make!=NULL,"FAIL factory8 export");IDirect3D8*r=make(120);up_check(r!=NULL,"FAIL factory8");D3DPRESENT_PARAMETERS p;ZeroMemory(&p,sizeof(p));p.BackBufferWidth=p.BackBufferHeight=8;p.BackBufferCount=1;p.BackBufferFormat=21;p.SwapEffect=1;p.Windowed=TRUE;p.hDeviceWindow=window;UPContext c={.module=module,.mode=mode};up_hr(IDirect3D8_CreateDevice(r,0,1,window,32,&p,&c.d),"FAIL device8");up_check(IDirect3D8_Release(r)==1,"FAIL root8 release");up_cases(&c);up_hr(IDirect3DDevice8_Reset(c.d,&p),"FAIL Reset8");up_check(IDirect3DDevice8_Release(c.d)==0,"FAIL final device8");up_log_result(&c);}
