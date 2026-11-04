#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <initguid.h>
#include <d3d8.h>
extern UINT tc_frame_index;void tc_report(const char*);
#define TCDevice IDirect3DDevice8
#define TCSurface IDirect3DSurface8
#define TCVB IDirect3DVertexBuffer8
#define TCIB IDirect3DIndexBuffer8
#define TCVBDesc D3DVERTEXBUFFER_DESC
#define TCIBDesc D3DINDEXBUFFER_DESC
#define BLockPointer BYTE*
#define BC_VLOCK IDirect3DVertexBuffer8_Lock
#define BC_VUNLOCK IDirect3DVertexBuffer8_Unlock
#define BC_ILOCK IDirect3DIndexBuffer8_Lock
#define BC_IUNLOCK IDirect3DIndexBuffer8_Unlock
#define BC_VDESC IDirect3DVertexBuffer8_GetDesc
#define BC_IDESC IDirect3DIndexBuffer8_GetDesc
#define BC_VPARENT IDirect3DVertexBuffer8_GetDevice
#define BC_IPARENT IDirect3DIndexBuffer8_GetDevice
#define BC_VREL IDirect3DVertexBuffer8_Release
#define BC_IREL IDirect3DIndexBuffer8_Release
#define TC_SREL IDirect3DSurface8_Release
#define TC_DREL IDirect3DDevice8_Release
#define TC_SLOCK IDirect3DSurface8_LockRect
#define TC_SUNLOCK IDirect3DSurface8_UnlockRect
#define TC_RS IDirect3DDevice8_SetRenderState
#define TC_TSS IDirect3DDevice8_SetTextureStageState
#define TC_CLEAR IDirect3DDevice8_Clear
#define TC_BEGIN IDirect3DDevice8_BeginScene
#define TC_END IDirect3DDevice8_EndScene
#define TC_DRAW IDirect3DDevice8_DrawPrimitive
#include "d3d8_backend_buffers_cases.h"
static HRESULT tc_fvf(TCDevice*d){return IDirect3DDevice8_SetVertexShader(d,D3DFVF_XYZRHW|D3DFVF_DIFFUSE);}
static void tc_read(TCContext*c){TCSurface*cpu=NULL,*back=NULL;tc_expect(IDirect3DDevice8_CreateImageSurface(c->d,8,8,21,&cpu),"FAIL CPU8");tc_expect(IDirect3DDevice8_GetBackBuffer(c->d,0,D3DBACKBUFFER_TYPE_MONO,&back),"FAIL back8");tc_expect(IDirect3DDevice8_CopyRects(c->d,back,NULL,0,cpu,NULL),"FAIL read8");;D3DLOCKED_RECT lock;tc_expect(TC_SLOCK(cpu,&lock,NULL,16),"FAIL texture CPU read");tc_check(lock.Pitch==32,"FAIL texture readback pitch");tc_expect(TC_SUNLOCK(cpu),"FAIL texture CPU unlock");TC_SREL(cpu);TC_SREL(back);}
static HRESULT tc_reset(TCContext*c){D3DPRESENT_PARAMETERS p;ZeroMemory(&p,sizeof(p));p.BackBufferWidth=p.BackBufferHeight=8;p.BackBufferFormat=21;p.BackBufferCount=1;p.SwapEffect=1;p.Windowed=TRUE;return IDirect3DDevice8_Reset(c->d,&p);}
static HRESULT bc_vb(TCContext*c,UINT length,DWORD usage,DWORD fvf,UINT pool,TCVB**out){return IDirect3DDevice8_CreateVertexBuffer(c->d,length,usage,fvf,pool,out);}
static HRESULT bc_ib(TCContext*c,UINT length,DWORD usage,UINT format,UINT pool,TCIB**out){return IDirect3DDevice8_CreateIndexBuffer(c->d,length,usage,format,pool,out);}
static HRESULT bc_stream(TCContext*c,TCVB*v){return IDirect3DDevice8_SetStreamSource(c->d,0,v,20);}
static HRESULT bc_indices(TCContext*c,TCIB*i){return IDirect3DDevice8_SetIndices(c->d,i,0);}
static HRESULT bc_indexed(TCContext*c){return IDirect3DDevice8_DrawIndexedPrimitive(c->d,D3DPT_TRIANGLELIST,0,4,0,2);}
static void bc_bind_queries(TCContext*c,TCVB*vb,TCIB*ib){TCVB*v=NULL;TCIB*i=NULL;UINT stride=0;UINT base=99;tc_expect(IDirect3DDevice8_GetStreamSource(c->d,0,&v,&stride),"FAIL stream query");tc_expect(IDirect3DDevice8_GetIndices(c->d,&i,&base),"FAIL indices query");tc_check(base==0,"FAIL API8 index base");tc_check(v==vb&&i==ib&&stride==20,"FAIL binding identity/stride");BC_VREL(v);BC_IREL(i);}
void tc_probe8(HMODULE module,HWND window,UINT mode){IDirect3D8*(WINAPI*make)(UINT)=(void*)(uintptr_t)GetProcAddress(module,"Direct3DCreate8");tc_check(make!=NULL,"FAIL root8 export");IDirect3D8*root=make(220);tc_check(root!=NULL,"FAIL root8");D3DPRESENT_PARAMETERS p;ZeroMemory(&p,sizeof(p));p.BackBufferWidth=p.BackBufferHeight=8;p.BackBufferFormat=21;p.BackBufferCount=1;p.SwapEffect=1;p.hDeviceWindow=window;p.Windowed=TRUE;TCContext c={.module=module,.mode=mode};tc_expect(IDirect3D8_CreateDevice(root,0,1,window,32,&p,&c.d),"FAIL create8");tc_check(IDirect3D8_Release(root)==1,"FAIL early root8");tc_cases(&c);tc_check(!TC_DREL(c.d),"FAIL final device8");}
