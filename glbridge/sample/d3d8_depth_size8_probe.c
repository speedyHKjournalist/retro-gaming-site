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
#include "d3d8_depth_size_cases.h"
static HRESULT dc_make_depth(DCContext*c,UINT samples,DCSurface**out){return IDirect3DDevice8_CreateDepthStencilSurface(c->d,8,8,D3DFMT_D24S8,samples,out);}
static HRESULT dc_make_target(DCContext*c,DCSurface**out){return IDirect3DDevice8_CreateRenderTarget(c->d,8,8,D3DFMT_A8R8G8B8,0,FALSE,out);}
static HRESULT dc_reset(DCContext*c){D3DPRESENT_PARAMETERS p;ZeroMemory(&p,sizeof(p));p.BackBufferWidth=p.BackBufferHeight=8;p.BackBufferCount=1;p.BackBufferFormat=21;p.SwapEffect=1;p.hDeviceWindow=c->window;p.Windowed=TRUE;return IDirect3DDevice8_Reset(c->d,&p);}
static UINT depth_size_expected(DCContext*c,UINT f,UINT w,UINT h,BOOL implicit){if(c->mode==0)return implicit?w*h*4u:0;UINT b=f==73||f==80?2:4;return ((w*b+3u)/4u*4u)*h;}
static void depth_size_line(DCContext*c,const char*kind,UINT f,UINT w,UINT h,UINT samples,UINT bytes){char line[180];wsprintfA(line,"DEPTHSIZE %s mode=%u format=%u width=%u height=%u samples=%u Size=%u",kind,c->mode,f,w,h,samples,bytes);dc_report(line);}
static void depth_size_check(DCContext*c,DCSurface*s,UINT f,UINT w,UINT h,UINT samples,BOOL implicit,const char*kind){D3DSURFACE_DESC d;dc_expect(DC_DESC(s,&d),"FAIL depth Size desc8");UINT bytes=depth_size_expected(c,f,w,h,implicit);dc_check(d.Format==f&&d.Type==D3DRTYPE_SURFACE&&d.Usage==2&&d.Pool==0&&d.Width==w&&d.Height==h&&d.MultiSampleType==samples&&d.Size==bytes,"FAIL depth Size/pitch-format metadata");depth_size_line(c,kind,f,w,h,samples,bytes);}
static void dc_depth_size_before(DCContext*c){DCSurface*s=NULL;dc_expect(DC_GET_DEPTH(c->d,&s),"FAIL initial implicit depth");depth_size_check(c,s,80,8,8,0,TRUE,"IMPLICIT");dc_check(!DC_RELEASE_SURFACE(s),"FAIL implicit metadata refs");const UINT formats[]={71,73,75,77,79,80};for(UINT f=0;f<6;f++)for(UINT ms=0;ms<2;ms++){dc_expect(IDirect3DDevice8_CreateDepthStencilSurface(c->d,3,2,(D3DFORMAT)formats[f],ms?4:0,&s),"FAIL metadata depth target");depth_size_check(c,s,formats[f],3,2,ms?4:0,FALSE,"TARGET");dc_check(!DC_RELEASE_SURFACE(s),"FAIL metadata target refs");}dc_expect(DC_RS(c->d,D3DRS_ZENABLE,FALSE),"FAIL restore pre-scene Z");}
static void dc_depth_size_reset(DCContext*c){D3DPRESENT_PARAMETERS p;ZeroMemory(&p,sizeof(p));p.BackBufferWidth=5;p.BackBufferHeight=3;p.BackBufferCount=1;p.BackBufferFormat=21;p.SwapEffect=1;p.hDeviceWindow=c->window;p.Windowed=TRUE;p.EnableAutoDepthStencil=TRUE;p.AutoDepthStencilFormat=80;dc_expect(IDirect3DDevice8_Reset(c->d,&p),"FAIL depth metadata resize");DCSurface*s=NULL;dc_expect(DC_GET_DEPTH(c->d,&s),"FAIL resized implicit depth");depth_size_check(c,s,80,5,3,0,TRUE,"RESET");dc_check(!DC_RELEASE_SURFACE(s),"FAIL resized implicit refs");}
void dc_probe8(HMODULE module,HWND window,UINT mode){IDirect3D8*(WINAPI*make)(UINT)=(void*)GetProcAddress(module,"Direct3DCreate8");dc_check(make!=NULL,"FAIL factory8 export");IDirect3D8*root=make(220);dc_check(root!=NULL,"FAIL root8");DCContext c={.window=window,.module=module,.mode=mode,.api=8,.sdk=220};D3DPRESENT_PARAMETERS p;ZeroMemory(&p,sizeof(p));p.BackBufferWidth=p.BackBufferHeight=8;p.BackBufferCount=1;p.BackBufferFormat=21;p.SwapEffect=1;p.hDeviceWindow=window;p.Windowed=TRUE;p.EnableAutoDepthStencil=TRUE;p.AutoDepthStencilFormat=80;dc_expect(IDirect3D8_CreateDevice(root,0,1,window,32,&p,&c.d),"FAIL create8");DCSurface*image=NULL;dc_expect(IDirect3DDevice8_CreateImageSurface(c.d,3,2,(D3DFORMAT)65,&image),"FAIL legacy65 create");D3DSURFACE_DESC desc;dc_expect(IDirect3DSurface8_GetDesc(image,&desc),"FAIL legacy image desc");dc_check(desc.Format==65&&desc.Size==24&&desc.Pool==2&&desc.Width==3&&desc.Height==2,"FAIL legacy65 descriptor");D3DLOCKED_RECT data;dc_expect(IDirect3DSurface8_LockRect(image,&data,NULL,0),"FAIL legacy65 lock");dc_check(data.Pitch==12,"FAIL legacy65 pitch");for(UINT y=0;y<2;y++)for(UINT x=0;x<3;x++){DWORD*p=(DWORD*)((BYTE*)data.pBits+y*data.Pitch);dc_check(!p[x],"FAIL legacy65 zero");p[x]=0x12340000u|(y<<8)|x;}dc_expect(IDirect3DSurface8_UnlockRect(image),"FAIL legacy65 unlock");const RECT area={1,0,3,2};dc_expect(IDirect3DSurface8_LockRect(image,&data,&area,D3DLOCK_READONLY),"FAIL legacy65 subrect");dc_check(data.Pitch==12,"FAIL legacy65 subrect pitch");for(UINT y=0;y<2;y++)for(UINT x=0;x<2;x++)dc_check(((DWORD*)((BYTE*)data.pBits+y*data.Pitch))[x]==(0x12340000u|(y<<8)|(x+1)),"FAIL legacy65 CPU bits");dc_expect(IDirect3DSurface8_UnlockRect(image),"FAIL legacy65 final unlock");dc_check(!IDirect3DSurface8_Release(image),"FAIL legacy65 refs");char line[160];wsprintfA(line,"IMAGE8 LEGACY mode=%u format65=1 Size=24 pitch=12 finalRefs=0",mode);dc_report(line);dc_depth_size_before(&c);dc_check(IDirect3D8_Release(root)==1,"FAIL early root8");dc_cases(&c);}
