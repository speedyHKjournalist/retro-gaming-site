#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <d3d8.h>
typedef IDirect3DDevice8 SCDevice;
#define SC_RS IDirect3DDevice8_SetRenderState
#define SC_TSS IDirect3DDevice8_SetTextureStageState
#define SC_CLEAR IDirect3DDevice8_Clear
#define SC_BEGIN IDirect3DDevice8_BeginScene
#define SC_END IDirect3DDevice8_EndScene
#define SC_DRAW IDirect3DDevice8_DrawPrimitiveUP
static HRESULT sc_make(UINT,const DWORD*);static void sc_query(UINT);static HRESULT sc_bind(UINT);static HRESULT sc_unbind(void);static HRESULT sc_reset(void);static void sc_read(DWORD);static void sc_destroy(UINT);static HRESULT sc_set_constants(UINT,const float*,UINT);static void sc_bank(const float*);static void sc_fvf(void);static void sc_zero(void);static void sc_capture(void);static void sc_apply(void);static void sc_block_destroy(void);
#include "d3d8_backend_fvf_cases.h"

static DWORD sc_handles[3];static HWND sc_window;
static HRESULT sc_make(UINT index,const DWORD*code){return IDirect3DDevice8_CreateVertexShader(sc_device,sc_decl,code,&sc_handles[index],0);}
static void sc_query(UINT index){DWORD out[32],bytes=sizeof(out);sc_hr(IDirect3DDevice8_GetVertexShaderDeclaration(sc_device,sc_handles[index],out,&bytes),"FAIL raw declaration8");sc_check(bytes==sizeof(sc_decl)&&sc_equal(out,sc_decl,bytes),"FAIL raw declaration bytes8");bytes=sizeof(out);out[0]=0xccccccccu;sc_hr(IDirect3DDevice8_GetVertexShaderFunction(sc_device,sc_handles[index],out,&bytes),"FAIL raw function8");sc_check(index==2?bytes==0&&out[0]==0xccccccccu:bytes==sizeof(sc_red)&&sc_equal(out,index?sc_green:sc_red,bytes),"FAIL original function8");}

static HRESULT sc_bind(UINT index){HRESULT hr=IDirect3DDevice8_SetVertexShader(sc_device,sc_handles[index]);DWORD got=0;sc_hr(IDirect3DDevice8_GetVertexShader(sc_device,&got),"FAIL bound8");sc_check(got==sc_handles[index],"FAIL handle identity8");return hr;}
static HRESULT sc_unbind(void){return IDirect3DDevice8_SetVertexShader(sc_device,0);}
static void sc_destroy(UINT index){sc_hr(IDirect3DDevice8_DeleteVertexShader(sc_device,sc_handles[index]),"FAIL VS delete8");DWORD bytes=77;sc_check(IDirect3DDevice8_GetVertexShaderFunction(sc_device,sc_handles[index],NULL,&bytes)==D3DERR_INVALIDCALL&&bytes==77,"FAIL expired VS8");}

static DWORD sc_block;
static void sc_fvf(void){sc_hr(IDirect3DDevice8_SetVertexShader(sc_device,0x4042),"FAIL legacy FVF8");DWORD got=0;sc_hr(IDirect3DDevice8_GetVertexShader(sc_device,&got),"FAIL Get FVF8");sc_check(got==0x4042,"FAIL raw FVF query8");}
static void sc_zero(void){sc_hr(IDirect3DDevice8_SetVertexShader(sc_device,0),"FAIL zero8");DWORD got=77;sc_hr(IDirect3DDevice8_GetVertexShader(sc_device,&got),"FAIL zero query8");sc_check(got==0,"FAIL zero retained8");}
static void sc_capture(void){sc_hr(IDirect3DDevice8_CreateStateBlock(sc_device,3,&sc_block),"FAIL vertex block8");}
static void sc_apply(void){sc_hr(IDirect3DDevice8_ApplyStateBlock(sc_device,sc_block),"FAIL vertex apply8");DWORD got=0;sc_hr(IDirect3DDevice8_GetVertexShader(sc_device,&got),"FAIL restored FVF8");sc_check(got==0x4042,"FAIL restored raw FVF8");}
static void sc_block_destroy(void){sc_hr(IDirect3DDevice8_DeleteStateBlock(sc_device,sc_block),"FAIL block delete8");}
static HRESULT sc_set_constants(UINT reg,const float*p,UINT n){return IDirect3DDevice8_SetVertexShaderConstant(sc_device,reg,p,n);}
static void sc_bank(const float*p){float got[4];sc_hr(IDirect3DDevice8_GetVertexShaderConstant(sc_device,2,got,1),"FAIL Get API bank8");sc_check(sc_equal(got,p,16),"FAIL polluted API bank8");}
static HRESULT sc_reset(void){D3DPRESENT_PARAMETERS p;ZeroMemory(&p,sizeof(p));p.BackBufferWidth=p.BackBufferHeight=8;p.BackBufferCount=1;p.BackBufferFormat=21;p.SwapEffect=1;p.Windowed=TRUE;p.hDeviceWindow=sc_window;return IDirect3DDevice8_Reset(sc_device,&p);}
static void sc_read(DWORD color){IDirect3DSurface8 *cpu=NULL,*back=NULL;sc_hr(IDirect3DDevice8_CreateImageSurface(sc_device,8,8,21,&cpu),"FAIL image8");sc_hr(IDirect3DDevice8_GetBackBuffer(sc_device,0,0,&back),"FAIL back8");sc_hr(IDirect3DDevice8_CopyRects(sc_device,back,NULL,0,cpu,NULL),"FAIL read8");D3DLOCKED_RECT l;sc_hr(IDirect3DSurface8_LockRect(cpu,&l,NULL,16),"FAIL lock8");for(UINT y=0;y<8;y++)for(UINT x=0;x<8;x++){sc_check(((DWORD*)((BYTE*)l.pBits+y*l.Pitch))[x]==color,"FAIL localDEF pixels8");sc_words++;}sc_hr(IDirect3DSurface8_UnlockRect(cpu),"FAIL unlock8");IDirect3DSurface8_Release(back);sc_check(!IDirect3DSurface8_Release(cpu),"FAIL image refs8");}
void sc_probe8(HMODULE module,HWND window,UINT mode){sc_module=module;sc_mode=mode;sc_window=window;IDirect3D8*(WINAPI*make)(UINT)=(void*)(uintptr_t)GetProcAddress(module,"Direct3DCreate8");sc_check(make!=NULL,"FAIL root export8");IDirect3D8*r=make(120);sc_check(r!=NULL,"FAIL root8");D3DPRESENT_PARAMETERS p;ZeroMemory(&p,sizeof(p));p.BackBufferWidth=p.BackBufferHeight=8;p.BackBufferCount=1;p.BackBufferFormat=21;p.SwapEffect=1;p.Windowed=TRUE;p.hDeviceWindow=window;sc_hr(IDirect3D8_CreateDevice(r,0,1,window,32,&p,&sc_device),"FAIL device8");sc_check(IDirect3D8_Release(r)==1,"FAIL root refs8");sc_words=0;sc_cases();sc_check(sc_words==256&&!IDirect3DDevice8_Release(sc_device),"FAIL final refs8");char text[140];wsprintfA(text,"FVFCORE CHECK mode=%u sdk=120 localWords=256 legacyFVF=16450 zeroClears=1 blockRestore=1 reset=1 finalRefs=0",mode);sc_report(text);}
