#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <d3d8.h>
typedef IDirect3DDevice8 DCDevice;typedef IDirect3DVertexBuffer8 DCBuffer;
#define DC_RS IDirect3DDevice8_SetRenderState
#define DC_TSS IDirect3DDevice8_SetTextureStageState
#define DC_VLOCK(b,o,n,p,f) IDirect3DVertexBuffer8_Lock((b),(o),(n),(BYTE**)(p),(f))
#define DC_VUNLOCK IDirect3DVertexBuffer8_Unlock
#define DC_VREL IDirect3DVertexBuffer8_Release
#define DC_CLEAR IDirect3DDevice8_Clear
#define DC_BEGIN IDirect3DDevice8_BeginScene
#define DC_END IDirect3DDevice8_EndScene
#define DC_DRAW IDirect3DDevice8_DrawPrimitive
static HRESULT dc_buffer(UINT,DCBuffer**);static HRESULT dc_create(const DWORD*,BOOL);static HRESULT dc_invalid(const DWORD*);static void dc_query(void);static HRESULT dc_bind(void);static HRESULT dc_stream(UINT,DCBuffer*,UINT);static HRESULT dc_reset(void);static void dc_read(DWORD);static void dc_destroy(void);static HRESULT dc_set_constant(const DWORD*);static HRESULT dc_get_constant(DWORD*);
#include "d3d8_backend_decl_cases.h"

static DWORD dc_decl;static HWND dc_window;
static HRESULT dc_buffer(UINT bytes,DCBuffer**out){return IDirect3DDevice8_CreateVertexBuffer(dc_device,bytes,0,0,1,out);}
static HRESULT dc_create(const DWORD*t,BOOL unused){(void)unused;return IDirect3DDevice8_CreateVertexShader(dc_device,t,NULL,&dc_decl,0);}
static HRESULT dc_invalid(const DWORD*t){DWORD out=77;HRESULT hr=IDirect3DDevice8_CreateVertexShader(dc_device,t,NULL,&out,0);dc_check(out==0,"FAIL invalid output8");return hr;}
static void dc_query(void){DWORD raw[12],size=sizeof(raw);dc_hr(IDirect3DDevice8_GetVertexShaderDeclaration(dc_device,dc_decl,raw,&size),"FAIL raw declaration8");dc_check(size==sizeof(dc_tokens),"FAIL raw size8");for(UINT i=0;i<12;i++)dc_check(raw[i]==dc_tokens[i],"FAIL literal/raw token8");size=77;dc_hr(IDirect3DDevice8_GetVertexShaderFunction(dc_device,dc_decl,NULL,&size),"FAIL empty function8");dc_check(!size,"FAIL function size8");}
static HRESULT dc_bind(void){HRESULT hr=IDirect3DDevice8_SetVertexShader(dc_device,dc_decl);DWORD got=0;dc_hr(IDirect3DDevice8_GetVertexShader(dc_device,&got),"FAIL bound query8");dc_check(got==dc_decl,"FAIL handle identity8");return hr;}
static HRESULT dc_stream(UINT stream,DCBuffer*b,UINT stride){return IDirect3DDevice8_SetStreamSource(dc_device,stream,b,stride);}
static HRESULT dc_reset(void){D3DPRESENT_PARAMETERS p;ZeroMemory(&p,sizeof(p));p.BackBufferWidth=p.BackBufferHeight=8;p.BackBufferCount=1;p.BackBufferFormat=21;p.SwapEffect=1;p.Windowed=TRUE;p.hDeviceWindow=dc_window;return IDirect3DDevice8_Reset(dc_device,&p);}
static HRESULT dc_set_constant(const DWORD*p){return IDirect3DDevice8_SetVertexShaderConstant(dc_device,2,p,1);}
static HRESULT dc_get_constant(DWORD*p){return IDirect3DDevice8_GetVertexShaderConstant(dc_device,2,p,1);}
static void dc_read(DWORD color){IDirect3DSurface8 *cpu=NULL,*back=NULL;dc_hr(IDirect3DDevice8_CreateImageSurface(dc_device,8,8,21,&cpu),"FAIL image8");dc_hr(IDirect3DDevice8_GetBackBuffer(dc_device,0,0,&back),"FAIL back8");dc_hr(IDirect3DDevice8_CopyRects(dc_device,back,NULL,0,cpu,NULL),"FAIL read8");D3DLOCKED_RECT l;dc_hr(IDirect3DSurface8_LockRect(cpu,&l,NULL,16),"FAIL lock8");for(UINT y=0;y<8;y++)for(UINT x=0;x<8;x++){dc_check(((DWORD*)((BYTE*)l.pBits+y*l.Pitch))[x]==color,"FAIL draw pixels8");dc_words++;}dc_hr(IDirect3DSurface8_UnlockRect(cpu),"FAIL unlock8");IDirect3DSurface8_Release(back);dc_check(!IDirect3DSurface8_Release(cpu),"FAIL image refs8");}
static void dc_destroy(void){dc_hr(IDirect3DDevice8_SetVertexShader(dc_device,0),"FAIL unbind8");dc_hr(IDirect3DDevice8_DeleteVertexShader(dc_device,dc_decl),"FAIL delete8");DWORD size=77;dc_check(IDirect3DDevice8_GetVertexShaderDeclaration(dc_device,dc_decl,NULL,&size)==D3DERR_INVALIDCALL&&size==77,"FAIL expired raw8");}
void dc_probe8(HMODULE module,HWND window,UINT mode){dc_module=module;dc_mode=mode;dc_window=window;IDirect3D8*(WINAPI*make)(UINT)=(void*)(uintptr_t)GetProcAddress(module,"Direct3DCreate8");dc_check(make!=NULL,"FAIL root export8");IDirect3D8*r=make(120);dc_check(r!=NULL,"FAIL root8");D3DPRESENT_PARAMETERS p;ZeroMemory(&p,sizeof(p));p.BackBufferWidth=p.BackBufferHeight=8;p.BackBufferCount=1;p.BackBufferFormat=21;p.SwapEffect=1;p.Windowed=TRUE;p.hDeviceWindow=window;dc_hr(IDirect3D8_CreateDevice(r,0,1,window,32,&p,&dc_device),"FAIL device8");dc_check(IDirect3D8_Release(r)==1,"FAIL root refs8");dc_words=0;dc_cases();dc_check(dc_words==128&&!IDirect3DDevice8_Release(dc_device),"FAIL final refs8");char line[120];wsprintfA(line,"DECLCORE CHECK mode=%u sdk=120 invalid=4 localWords=128 reset=1 finalRefs=0",mode);dc_report(line);}
