#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <initguid.h>
#include <d3d8.h>
extern UINT tc_frame_index;void tc_report(const char*);
#define TCDevice IDirect3DDevice8
#define TCTexture IDirect3DVolumeTexture8
#define TCSurface IDirect3DSurface8
#define TCDesc D3DSURFACE_DESC
#define TCView IDirect3DVolume8
#define TCVolumeDesc D3DVOLUME_DESC
#define TC_VPARENT IDirect3DVolume8_GetDevice
#define TC_VDESC IDirect3DVolume8_GetDesc
#define TC_VREL IDirect3DVolume8_Release
#define TC_VLOCK IDirect3DVolume8_LockBox
#define TC_VUNLOCK IDirect3DVolume8_UnlockBox
#define TC_VIEW IDirect3DVolumeTexture8_GetVolumeLevel
#define TC_PARENT IDirect3DSurface8_GetDevice
#define TC_DESC IDirect3DSurface8_GetDesc
#define TC_SREL IDirect3DSurface8_Release
#define TC_DREL IDirect3DDevice8_Release
#define TC_TREL IDirect3DVolumeTexture8_Release
#define TC_LEVELS IDirect3DVolumeTexture8_GetLevelCount
#define TC_LOCK IDirect3DVolumeTexture8_LockBox
#define TC_UNLOCK IDirect3DVolumeTexture8_UnlockBox
#define TC_SLOCK IDirect3DSurface8_LockRect
#define TC_SUNLOCK IDirect3DSurface8_UnlockRect
#define TC_RS IDirect3DDevice8_SetRenderState
#define TC_TSS IDirect3DDevice8_SetTextureStageState
#define TC_CLEAR IDirect3DDevice8_Clear
#define TC_BEGIN IDirect3DDevice8_BeginScene
#define TC_END IDirect3DDevice8_EndScene
#define TC_DRAW IDirect3DDevice8_DrawPrimitiveUP
#include "d3d8_backend_volume_cases.h"
static HRESULT tc_create(TCContext*c,UINT w,UINT h,UINT depth,UINT levels,DWORD usage,UINT format,UINT pool,TCTexture**out){return IDirect3DDevice8_CreateVolumeTexture(c->d,w,h,depth,levels,usage,(D3DFORMAT)format,(D3DPOOL)pool,out);}
static void tc_size(TCContext*c,TCView*s,UINT bytes){(void)c;D3DVOLUME_DESC d;tc_expect(IDirect3DVolume8_GetDesc(s,&d),"FAIL SDK8 desc");tc_check(d.Size==bytes,"FAIL SDK8 texture Size");}
static void tc_sampler(TCContext*c){tc_expect(TC_TSS(c->d,0,D3DTSS_MINFILTER,1),"FAIL min");tc_expect(TC_TSS(c->d,0,D3DTSS_MAGFILTER,1),"FAIL mag");tc_expect(TC_TSS(c->d,0,D3DTSS_MIPFILTER,0),"FAIL mip");}
static HRESULT tc_fvf(TCDevice*d){return IDirect3DDevice8_SetVertexShader(d,D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_TEX1|D3DFVF_TEXCOORDSIZE3(0));}
static HRESULT tc_bind(TCContext*c,TCTexture*t){return IDirect3DDevice8_SetTexture(c->d,0,(IDirect3DBaseTexture8*)t);}
static void tc_read(TCContext*c){TCSurface*cpu=NULL,*back=NULL;tc_expect(IDirect3DDevice8_CreateImageSurface(c->d,8,8,21,&cpu),"FAIL CPU8");tc_expect(IDirect3DDevice8_GetBackBuffer(c->d,0,D3DBACKBUFFER_TYPE_MONO,&back),"FAIL back8");tc_expect(IDirect3DDevice8_CopyRects(c->d,back,NULL,0,cpu,NULL),"FAIL read8");;D3DLOCKED_RECT lock;tc_expect(TC_SLOCK(cpu,&lock,NULL,16),"FAIL texture CPU read");tc_check(lock.Pitch==32,"FAIL texture readback pitch");tc_expect(TC_SUNLOCK(cpu),"FAIL texture CPU unlock");TC_SREL(cpu);TC_SREL(back);}
static HRESULT tc_reset(TCContext*c){D3DPRESENT_PARAMETERS p;ZeroMemory(&p,sizeof(p));p.BackBufferWidth=p.BackBufferHeight=8;p.BackBufferFormat=21;p.BackBufferCount=1;p.SwapEffect=1;p.Windowed=TRUE;return IDirect3DDevice8_Reset(c->d,&p);}
void tc_probe8(HMODULE module,HWND window,UINT mode){IDirect3D8*(WINAPI*make)(UINT)=(void*)(uintptr_t)GetProcAddress(module,"Direct3DCreate8");tc_check(make!=NULL,"FAIL root8 export");IDirect3D8*root=make(220);tc_check(root!=NULL,"FAIL root8");D3DPRESENT_PARAMETERS p;ZeroMemory(&p,sizeof(p));p.BackBufferWidth=p.BackBufferHeight=8;p.BackBufferFormat=21;p.BackBufferCount=1;p.SwapEffect=1;p.hDeviceWindow=window;p.Windowed=TRUE;TCContext c={.module=module,.mode=mode};tc_expect(IDirect3D8_CreateDevice(root,0,1,window,32,&p,&c.d),"FAIL create8");tc_check(IDirect3D8_Release(root)==1,"FAIL early root8");tc_cases(&c);tc_check(!TC_DREL(c.d),"FAIL final device8");}
