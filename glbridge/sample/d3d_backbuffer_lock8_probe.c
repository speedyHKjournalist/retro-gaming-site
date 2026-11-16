#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d8.h>
typedef IDirect3DDevice8 BBDevice;typedef IDirect3DSurface8 BBSurface;
#define BB_FVF IDirect3DDevice8_SetVertexShader
#define BB_RS IDirect3DDevice8_SetRenderState
#define BB_TSS IDirect3DDevice8_SetTextureStageState
#define BB_BEGIN IDirect3DDevice8_BeginScene
#define BB_END IDirect3DDevice8_EndScene
#define BB_DRAW IDirect3DDevice8_DrawPrimitiveUP
#define BB_RELEASE_DEVICE IDirect3DDevice8_Release
#define BB_RELEASE_SURFACE IDirect3DSurface8_Release
#define BB_DESC IDirect3DSurface8_GetDesc
#define BB_LOCK IDirect3DSurface8_LockRect
#define BB_UNLOCK IDirect3DSurface8_UnlockRect
#define BB_COOPERATIVE IDirect3DDevice8_TestCooperativeLevel
#define BB_BACK(d,out) IDirect3DDevice8_GetBackBuffer(d,0,D3DBACKBUFFER_TYPE_MONO,out)
static HRESULT baseline_read8(BBDevice*d,BBSurface*back){BBSurface*cpu=NULL;HRESULT hr=IDirect3DDevice8_CreateImageSurface(d,8,8,D3DFMT_A8R8G8B8,&cpu);if(FAILED(hr))return hr;hr=IDirect3DDevice8_CopyRects(d,back,NULL,0,cpu,NULL);IDirect3DSurface8_Release(cpu);return hr;}
#define BB_BASELINE_READ baseline_read8
#include "d3d_backbuffer_lock_cases.h"
static HRESULT bb_reset_device(BBContext*c,UINT w,UINT h,DWORD flags){D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=w;pp.BackBufferHeight=h;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=c->window;pp.Windowed=TRUE;pp.Flags=flags;return IDirect3DDevice8_Reset(c->d,&pp);}
void bb_probe8(HMODULE module,HWND window,UINT sdk,BOOL baseline,UINT mode)
{
 IDirect3D8*(WINAPI*make)(UINT)=(void*)GetProcAddress(module,"Direct3DCreate8");bb_check(make!=NULL,"FAIL 8 export");IDirect3D8*root=make(sdk);bb_check(root!=NULL,"FAIL 8 root");D3DPRESENT_PARAMETERS pp;ZeroMemory(&pp,sizeof(pp));pp.BackBufferWidth=pp.BackBufferHeight=8;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.BackBufferCount=1;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.Windowed=TRUE;
 if(!baseline){pp.MultiSampleType=D3DMULTISAMPLE_4_SAMPLES;pp.Flags=1;BBDevice*temp=NULL;bb_expect(IDirect3D8_CreateDevice(root,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&temp),"FAIL MSAA8 construction");BBSurface*back=NULL;bb_expect(BB_BACK(temp,&back),"FAIL MSAA8 back");D3DLOCKED_RECT out={123,(void*)99};bb_check(BB_LOCK(back,&out,NULL,D3DLOCK_READONLY)==D3DERR_INVALIDCALL&&out.Pitch==123&&out.pBits==(void*)99,"FAIL MSAA8 lock guard");bb_check(!BB_RELEASE_SURFACE(back)&&!BB_RELEASE_DEVICE(temp),"FAIL MSAA8 refs");pp.MultiSampleType=0;pp.Flags=0;}
 BBContext c={.window=window,.module=module,.mode=mode,.api=8,.sdk=sdk,.baseline=baseline,.msaa=!baseline};pp.Flags=baseline?1:0;bb_expect(IDirect3D8_CreateDevice(root,0,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&c.d),"FAIL 8 device");bb_check(IDirect3D8_Release(root)==1,"FAIL early8 root release");bb_cases(&c);
}
