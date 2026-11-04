#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <d3d8.h>
typedef IDirect3DDevice8 RCDevice;typedef IDirect3DSurface8 RCSurface;typedef IDirect3DBaseTexture8 RCBase;
#define RC_CLEAR IDirect3DDevice8_Clear
#define RC_LOCK IDirect3DSurface8_LockRect
#define RC_UNLOCK IDirect3DSurface8_UnlockRect
#define RC_SREL IDirect3DSurface8_Release
#define RC_BREL IDirect3DBaseTexture8_Release
static HRESULT rc_copy(RCSurface*,const RECT*,UINT,RCSurface*,const POINT*);
static HRESULT rc_image(UINT,UINT,RCSurface**);
static HRESULT rc_texture(RCSurface**,RCBase**);
static HRESULT rc_cube(RCSurface**,RCBase**);
static HRESULT rc_back(RCSurface**);
#include "d3d8_backend_rect_cases.h"

static HRESULT rc_copy(RCSurface*s,const RECT*r,UINT n,RCSurface*t,const POINT*p){return IDirect3DDevice8_CopyRects(rc_device,s,r,n,t,p);}
static HRESULT rc_image(UINT w,UINT h,RCSurface**s){return IDirect3DDevice8_CreateImageSurface(rc_device,w,h,21,s);}
static HRESULT rc_texture(RCSurface**s,RCBase**p){IDirect3DTexture8*t=NULL;HRESULT hr=IDirect3DDevice8_CreateTexture(rc_device,8,8,1,0,21,0,&t);if(FAILED(hr))return hr;*p=(RCBase*)t;return IDirect3DTexture8_GetSurfaceLevel(t,0,s);}
static HRESULT rc_cube(RCSurface**s,RCBase**p){IDirect3DCubeTexture8*t=NULL;HRESULT hr=IDirect3DDevice8_CreateCubeTexture(rc_device,8,2,0,21,0,&t);if(FAILED(hr))return hr;*p=(RCBase*)t;return IDirect3DCubeTexture8_GetCubeMapSurface(t,4,1,s);}
static HRESULT rc_back(RCSurface**s){return IDirect3DDevice8_GetBackBuffer(rc_device,0,0,s);}
void rc_probe8(HMODULE module,HWND window,UINT mode){rc_module=module;IDirect3D8*(WINAPI*make)(UINT)=(void*)(uintptr_t)GetProcAddress(module,"Direct3DCreate8");rc_check(make!=NULL,"FAIL factory8 export");IDirect3D8*r=make(120);rc_check(r!=NULL,"FAIL root8");D3DPRESENT_PARAMETERS p;ZeroMemory(&p,sizeof(p));p.BackBufferWidth=p.BackBufferHeight=8;p.BackBufferCount=1;p.BackBufferFormat=21;p.SwapEffect=1;p.Windowed=TRUE;p.hDeviceWindow=window;rc_hr(IDirect3D8_CreateDevice(r,0,1,window,32,&p,&rc_device),"FAIL device8");rc_check(IDirect3D8_Release(r)==1,"FAIL root8 refs");rc_words=0;rc_cases();p.BackBufferWidth=5;p.BackBufferHeight=3;rc_hr(IDirect3DDevice8_Reset(rc_device,&p),"FAIL reset8");rc_after_reset();rc_check(!IDirect3DDevice8_Release(rc_device),"FAIL final refs8");rc_check(rc_words==287,"FAIL word count8");char text[120];wsprintfA(text,"RECTCORE CHECK mode=%u sdk=120 localWords=287 reset=1 finalRefs=0",mode);rc_report(text);}
