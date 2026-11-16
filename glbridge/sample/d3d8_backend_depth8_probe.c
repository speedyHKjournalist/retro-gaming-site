#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d8.h>
typedef IDirect3DDevice8 DCDevice;typedef IDirect3DSurface8 DCSurface;
#define DC_RS IDirect3DDevice8_SetRenderState
#define DC_FVF IDirect3DDevice8_SetVertexShader
#define DC_TSS IDirect3DDevice8_SetTextureStageState
#define DC_BEGIN IDirect3DDevice8_BeginScene
#define DC_END IDirect3DDevice8_EndScene
#define DC_DRAW IDirect3DDevice8_DrawPrimitiveUP
#define DC_CLEAR IDirect3DDevice8_Clear
#define DC_BIND IDirect3DDevice8_SetRenderTarget
#define DC_DEPTH(d,s) IDirect3DDevice8_SetRenderTarget(d,NULL,s)
#define DC_GET_DEPTH IDirect3DDevice8_GetDepthStencilSurface
#define DC_RELEASE_DEVICE IDirect3DDevice8_Release
#define DC_RELEASE_SURFACE IDirect3DSurface8_Release
#define DC_DESC IDirect3DSurface8_GetDesc
#define DC_PARENT IDirect3DSurface8_GetDevice
#define DC_LOCK IDirect3DSurface8_LockRect
#define DC_BACK(d,s) IDirect3DDevice8_GetBackBuffer(d,0,D3DBACKBUFFER_TYPE_MONO,s)
static HRESULT dc_read(DCDevice*d,DCSurface*target){DCSurface*cpu=NULL;HRESULT hr=IDirect3DDevice8_CreateImageSurface(d,8,8,D3DFMT_A8R8G8B8,&cpu);if(FAILED(hr))return hr;hr=IDirect3DDevice8_CopyRects(d,target,NULL,0,cpu,NULL);IDirect3DSurface8_Release(cpu);return hr;}
#define DC_READ dc_read
#include "d3d8_backend_depth_cases.h"
static HRESULT dc_make_depth(DCContext*c,UINT samples,DCSurface**out){return IDirect3DDevice8_CreateDepthStencilSurface(c->d,8,8,D3DFMT_D24S8,samples,out);}
static HRESULT dc_make_target(DCContext*c,DCSurface**out){return IDirect3DDevice8_CreateRenderTarget(c->d,8,8,D3DFMT_A8R8G8B8,0,FALSE,out);}
static HRESULT dc_reset(DCContext*c){D3DPRESENT_PARAMETERS p;ZeroMemory(&p,sizeof(p));p.BackBufferWidth=p.BackBufferHeight=8;p.BackBufferCount=1;p.BackBufferFormat=21;p.SwapEffect=1;p.hDeviceWindow=c->window;p.Windowed=TRUE;return IDirect3DDevice8_Reset(c->d,&p);}
void dc_probe8(HMODULE module,HWND window,UINT mode){IDirect3D8*(WINAPI*make)(UINT)=(void*)GetProcAddress(module,"Direct3DCreate8");dc_check(make!=NULL,"FAIL factory8 export");IDirect3D8*root=make(220);dc_check(root!=NULL,"FAIL root8");DCContext c={.window=window,.module=module,.mode=mode,.api=8,.sdk=220};D3DPRESENT_PARAMETERS p;ZeroMemory(&p,sizeof(p));p.BackBufferWidth=p.BackBufferHeight=8;p.BackBufferCount=1;p.BackBufferFormat=21;p.SwapEffect=1;p.hDeviceWindow=window;p.Windowed=TRUE;dc_expect(IDirect3D8_CreateDevice(root,0,1,window,32,&p,&c.d),"FAIL create8");dc_check(IDirect3D8_Release(root)==1,"FAIL early root8");dc_cases(&c);}
