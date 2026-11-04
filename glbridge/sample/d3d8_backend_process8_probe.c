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
#include "d3d8_backend_process_cases.h"

static void tc_read(TCContext*c){TCSurface*cpu=NULL,*back=NULL;tc_expect(IDirect3DDevice8_CreateImageSurface(c->d,8,8,21,&cpu),"FAIL CPU8");tc_expect(IDirect3DDevice8_GetBackBuffer(c->d,0,D3DBACKBUFFER_TYPE_MONO,&back),"FAIL back8");tc_expect(IDirect3DDevice8_CopyRects(c->d,back,NULL,0,cpu,NULL),"FAIL read8");;D3DLOCKED_RECT lock;tc_expect(TC_SLOCK(cpu,&lock,NULL,16),"FAIL texture CPU read");tc_check(lock.Pitch==32,"FAIL texture readback pitch");for(UINT y=0;y<8;y++)for(UINT x=0;x<8;x++){DWORD got;CopyMemory(&got,(BYTE*)lock.pBits+y*lock.Pitch+x*4,4);tc_check(got==c->expected,"FAIL processed pixels");c->words++;}tc_expect(TC_SUNLOCK(cpu),"FAIL texture CPU unlock");TC_SREL(cpu);TC_SREL(back);}
static HRESULT tc_reset(TCContext*c){D3DPRESENT_PARAMETERS p;ZeroMemory(&p,sizeof(p));p.BackBufferWidth=p.BackBufferHeight=8;p.BackBufferFormat=21;p.BackBufferCount=1;p.SwapEffect=1;p.Windowed=TRUE;p.hDeviceWindow=c->window;return IDirect3DDevice8_Reset(c->d,&p);}
static HRESULT bc_vb(TCContext*c,UINT length,DWORD usage,DWORD fvf,UINT pool,TCVB**out){return IDirect3DDevice8_CreateVertexBuffer(c->d,length,usage,fvf,pool,out);}

static HRESULT bc_stream(TCContext*c,TCVB*v,UINT stride){return IDirect3DDevice8_SetStreamSource(c->d,0,v,stride);}



static HRESULT bc_fvf(TCContext*c,DWORD fvf){return IDirect3DDevice8_SetVertexShader(c->d,fvf);}
static HRESULT bc_process(TCContext*c,UINT src,UINT dst,UINT count,TCVB*v,DWORD flags){return IDirect3DDevice8_ProcessVertices(c->d,src,dst,count,v,flags);}
static void bc_query_binding(TCContext*c,TCVB*vb,UINT stride){TCVB*got=NULL;UINT s=0;tc_expect(IDirect3DDevice8_GetStreamSource(c->d,0,&got,&s),"FAIL bound query8");tc_check(got==vb&&s==stride,"FAIL bound identity8");BC_VREL(got);}
void tc_probe8(HMODULE module,HWND window,UINT mode){IDirect3D8*(WINAPI*make)(UINT)=(void*)(uintptr_t)GetProcAddress(module,"Direct3DCreate8");tc_check(make!=NULL,"FAIL root8 export");IDirect3D8*root=make(220);tc_check(root!=NULL,"FAIL root8");D3DPRESENT_PARAMETERS p;ZeroMemory(&p,sizeof(p));p.BackBufferWidth=p.BackBufferHeight=8;p.BackBufferFormat=21;p.BackBufferCount=1;p.SwapEffect=1;p.hDeviceWindow=window;p.Windowed=TRUE;TCContext c={.module=module,.mode=mode,.window=window};tc_expect(IDirect3D8_CreateDevice(root,0,1,window,32,&p,&c.d),"FAIL create8");tc_check(IDirect3D8_Release(root)==1,"FAIL early root8");tc_cases(&c);tc_check(c.words==256&&c.vertex_words==160&&!TC_DREL(c.d),"FAIL final device8");}
