#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d8.h>
typedef IDirect3DDevice8 RTDevice;typedef IDirect3DSurface8 RTSurface;typedef IDirect3DSwapChain8 RTChain;
#define RT_RELEASE_CHAIN IDirect3DSwapChain8_Release
#define RT_SET(d,s) IDirect3DDevice8_SetRenderTarget(d,s,NULL)
#define RT_GET(d,s) IDirect3DDevice8_GetRenderTarget(d,s)
#define RT_CLEAR(d) IDirect3DDevice8_Clear(d,0,NULL,D3DCLEAR_TARGET,0xffff0000,1,0)
#define RT_MAKE(d,out) IDirect3DDevice8_CreateRenderTarget(d,8,8,D3DFMT_A8R8G8B8,0,TRUE,out)
#define RT_FVF IDirect3DDevice8_SetVertexShader
#define RT_RS IDirect3DDevice8_SetRenderState
#define RT_TSS IDirect3DDevice8_SetTextureStageState
#define RT_BEGIN IDirect3DDevice8_BeginScene
#define RT_END IDirect3DDevice8_EndScene
#define RT_DRAW IDirect3DDevice8_DrawPrimitiveUP
#define RT_RELEASE_DEVICE IDirect3DDevice8_Release
#define RT_RELEASE_SURFACE IDirect3DSurface8_Release
#define RT_DESC IDirect3DSurface8_GetDesc
#define RT_LOCK IDirect3DSurface8_LockRect
#define RT_UNLOCK IDirect3DSurface8_UnlockRect
#define RT_COOPERATIVE IDirect3DDevice8_TestCooperativeLevel
#define RT_BACK(d,out) IDirect3DDevice8_GetBackBuffer(d,0,D3DBACKBUFFER_TYPE_MONO,out)
static HRESULT read8(RTDevice*d,RTSurface*back,BOOL locked){(void)locked;RTSurface*cpu=NULL;HRESULT hr=IDirect3DDevice8_CreateImageSurface(d,8,8,D3DFMT_A8R8G8B8,&cpu);if(FAILED(hr))return hr;hr=IDirect3DDevice8_CopyRects(d,back,NULL,0,cpu,NULL);if(SUCCEEDED(hr)){D3DLOCKED_RECT data;hr=IDirect3DSurface8_LockRect(cpu,&data,NULL,D3DLOCK_READONLY);if(SUCCEEDED(hr))hr=IDirect3DSurface8_UnlockRect(cpu);}IDirect3DSurface8_Release(cpu);return hr;}
#define RT_READ read8
#include "d3d_renderable_lock_cases.h"
static HRESULT rt_reset_device(RTContext*c,UINT w,UINT h,DWORD flags){D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=w;pp.BackBufferHeight=h;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=c->window;pp.Windowed=TRUE;pp.Flags=flags;return IDirect3DDevice8_Reset(c->d,&pp);}
static HRESULT rt_additional(RTContext*c,RTChain**out){D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=pp.BackBufferHeight=8;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=c->window;pp.Windowed=TRUE;pp.Flags=1;return IDirect3DDevice8_CreateAdditionalSwapChain(c->d,&pp,out);}
static HRESULT rt_chain_back(RTChain*s,RTSurface**out){return IDirect3DSwapChain8_GetBackBuffer(s,0,D3DBACKBUFFER_TYPE_MONO,out);}
void rt_probe8(HMODULE module,HWND window,UINT sdk,BOOL baseline,UINT mode)
{
 IDirect3D8*(WINAPI*make)(UINT)=(void*)GetProcAddress(module,"Direct3DCreate8");rt_check(make!=NULL,"FAIL 8 export");IDirect3D8*root=make(sdk);rt_check(root!=NULL,"FAIL 8 root");D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=pp.BackBufferHeight=8;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.Windowed=TRUE;
 RTContext c={.window=window,.module=module,.mode=mode,.api=8,.sdk=sdk,.baseline=baseline,};pp.Flags=0;rt_expect(IDirect3D8_CreateDevice(root,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&c.d),"FAIL 8 device");rt_check(IDirect3D8_Release(root)==1,"FAIL early8 root release");rt_cases(&c);
}
