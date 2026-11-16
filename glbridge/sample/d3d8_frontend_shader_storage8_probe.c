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
static HRESULT sc_make(UINT,const DWORD*);static void sc_query(UINT);static void sc_ps_cases(void);static HRESULT sc_bind(UINT);static HRESULT sc_unbind(void);static HRESULT sc_reset(void);static void sc_read(DWORD);static void sc_destroy(UINT);static HRESULT sc_set_constants(UINT,const float*,UINT);static void sc_bank(const float*);static void sc_record(void);static void sc_restore(void);static void sc_capture(void);static void sc_block_destroy(void);static void sc_metadata_live(DWORD,DWORD,DWORD);static void sc_metadata_check(DWORD,DWORD,DWORD);
#include "d3d8_frontend_shader_storage_cases.h"

static DWORD sc_handles[3];static HWND sc_window;
static HRESULT sc_make(UINT index,const DWORD*code){return IDirect3DDevice8_CreateVertexShader(sc_device,sc_decl,code,&sc_handles[index],0);}
static void sc_query(UINT index){DWORD out[32],bytes=sizeof(out);sc_hr(IDirect3DDevice8_GetVertexShaderDeclaration(sc_device,sc_handles[index],out,&bytes),"FAIL raw declaration8");sc_check(bytes==sizeof(sc_decl)&&sc_equal(out,sc_decl,bytes),"FAIL raw declaration bytes8");bytes=sizeof(out);out[0]=0xccccccccu;sc_hr(IDirect3DDevice8_GetVertexShaderFunction(sc_device,sc_handles[index],out,&bytes),"FAIL raw function8");sc_check(index==2?bytes==0&&out[0]==0xccccccccu:bytes==sizeof(sc_red)&&sc_equal(out,index?sc_green:sc_red,bytes),"FAIL original function8");}
static void sc_ps_cases(void){for(UINT minor=0;minor<=4;minor++){DWORD code[]={0xffff0100u|minor,1,0x800f0000u,0x90e40000u,0xffffu},out[5],bytes=sizeof(out),handle=0;sc_hr(IDirect3DDevice8_CreatePixelShader(sc_device,code,&handle),"FAIL PS8");sc_hr(IDirect3DDevice8_GetPixelShaderFunction(sc_device,handle,out,&bytes),"FAIL PS function8");sc_check(bytes==sizeof(code)&&sc_equal(out,code,bytes),"FAIL PS bytes8");sc_hr(IDirect3DDevice8_DeletePixelShader(sc_device,handle),"FAIL PS delete8");bytes=77;sc_check(IDirect3DDevice8_GetPixelShaderFunction(sc_device,handle,NULL,&bytes)==D3DERR_INVALIDCALL&&bytes==77,"FAIL expired PS8");}}
static HRESULT sc_bind(UINT index){HRESULT hr=IDirect3DDevice8_SetVertexShader(sc_device,sc_handles[index]);DWORD got=0;sc_hr(IDirect3DDevice8_GetVertexShader(sc_device,&got),"FAIL bound8");sc_check(got==sc_handles[index],"FAIL handle identity8");return hr;}
static HRESULT sc_unbind(void){return IDirect3DDevice8_SetVertexShader(sc_device,0);}
static void sc_destroy(UINT index){sc_hr(IDirect3DDevice8_DeleteVertexShader(sc_device,sc_handles[index]),"FAIL VS delete8");DWORD bytes=77;sc_check(IDirect3DDevice8_GetVertexShaderFunction(sc_device,sc_handles[index],NULL,&bytes)==D3DERR_INVALIDCALL&&bytes==77,"FAIL expired VS8");}

static DWORD sc_block;





static DWORD sc_bias=111,sc_address=3,sc_position=42;
static void sc_metadata_live(DWORD bias,DWORD address,DWORD position){sc_hr(IDirect3DDevice8_SetRenderState(sc_device,47,bias),"FAIL raw bias8");sc_hr(IDirect3DDevice8_SetRenderState(sc_device,172,position),"FAIL raw position8");sc_hr(IDirect3DDevice8_SetTextureStageState(sc_device,0,13,address),"FAIL raw address8");}
static void sc_metadata_check(DWORD bias,DWORD address,DWORD position){DWORD got=0;sc_hr(IDirect3DDevice8_GetRenderState(sc_device,47,&got),"FAIL raw bias query8");sc_check(got==bias,"FAIL raw bias8 preserved");sc_hr(IDirect3DDevice8_GetRenderState(sc_device,172,&got),"FAIL raw position query8");sc_check(got==position,"FAIL raw dropped state8 preserved");sc_hr(IDirect3DDevice8_GetTextureStageState(sc_device,0,13,&got),"FAIL raw address query8");sc_check(got==address,"FAIL raw address8 preserved");}
static void sc_fill_snapshot(void){sc_hr(IDirect3DDevice8_SetVertexShader(sc_device,sc_handles[1]),"FAIL snapshot binding8");sc_metadata_live(sc_bias,sc_address,sc_position);}
static void sc_record(void){sc_bias=111;sc_address=3;sc_position=42;DWORD bad=0;sc_hr(IDirect3DDevice8_BeginStateBlock(sc_device),"FAIL aborted begin8");sc_fill_snapshot();sc_hr(IDirect3DDevice8_EndStateBlock(sc_device,&bad),"FAIL abandoned end8");sc_hr(IDirect3DDevice8_DeleteStateBlock(sc_device,bad),"FAIL abandoned block8");sc_hr(IDirect3DDevice8_BeginStateBlock(sc_device),"FAIL snapshot begin8");sc_fill_snapshot();sc_hr(IDirect3DDevice8_EndStateBlock(sc_device,&sc_block),"FAIL snapshot end8");DWORD got=0;sc_hr(IDirect3DDevice8_GetVertexShader(sc_device,&got),"FAIL unchanged shader8");sc_check(got==sc_handles[0],"FAIL snapshot changed live8");sc_metadata_check(0,1,0);}
static void sc_restore(void){sc_hr(IDirect3DDevice8_ApplyStateBlock(sc_device,sc_block),"FAIL snapshot apply8");sc_metadata_check(sc_bias,sc_address,sc_position);DWORD got=0;sc_hr(IDirect3DDevice8_GetVertexShader(sc_device,&got),"FAIL restored program8");sc_check(got==sc_handles[1],"FAIL captured program8");}
static void sc_capture(void){sc_bias=2;sc_address=2;sc_position=7;sc_metadata_live(sc_bias,sc_address,sc_position);sc_hr(IDirect3DDevice8_CaptureStateBlock(sc_device,sc_block),"FAIL raw Capture8");}
static void sc_block_destroy(void){sc_hr(IDirect3DDevice8_DeleteStateBlock(sc_device,sc_block),"FAIL snapshot delete8");}
static HRESULT sc_set_constants(UINT reg,const float*p,UINT n){return IDirect3DDevice8_SetVertexShaderConstant(sc_device,reg,p,n);}
static void sc_bank(const float*p){float got[4];sc_hr(IDirect3DDevice8_GetVertexShaderConstant(sc_device,2,got,1),"FAIL Get API bank8");sc_check(sc_equal(got,p,16),"FAIL polluted API bank8");}
static HRESULT sc_reset(void){D3DPRESENT_PARAMETERS p;ZeroMemory(&p,sizeof(p));p.BackBufferWidth=p.BackBufferHeight=8;p.BackBufferCount=1;p.BackBufferFormat=21;p.SwapEffect=1;p.Windowed=TRUE;p.hDeviceWindow=sc_window;return IDirect3DDevice8_Reset(sc_device,&p);}
static void sc_read(DWORD color){IDirect3DSurface8 *cpu=NULL,*back=NULL;sc_hr(IDirect3DDevice8_CreateImageSurface(sc_device,8,8,21,&cpu),"FAIL image8");sc_hr(IDirect3DDevice8_GetBackBuffer(sc_device,0,0,&back),"FAIL back8");sc_hr(IDirect3DDevice8_CopyRects(sc_device,back,NULL,0,cpu,NULL),"FAIL read8");D3DLOCKED_RECT l;sc_hr(IDirect3DSurface8_LockRect(cpu,&l,NULL,16),"FAIL lock8");for(UINT y=0;y<8;y++)for(UINT x=0;x<8;x++){sc_check(((DWORD*)((BYTE*)l.pBits+y*l.Pitch))[x]==color,"FAIL localDEF pixels8");sc_words++;}sc_hr(IDirect3DSurface8_UnlockRect(cpu),"FAIL unlock8");IDirect3DSurface8_Release(back);sc_check(!IDirect3DSurface8_Release(cpu),"FAIL image refs8");}
void sc_probe8(HMODULE module,HWND window,UINT mode){sc_module=module;sc_mode=mode;sc_window=window;IDirect3D8*(WINAPI*make)(UINT)=(void*)(uintptr_t)GetProcAddress(module,"Direct3DCreate8");sc_check(make!=NULL,"FAIL root export8");IDirect3D8*r=make(120);sc_check(r!=NULL,"FAIL root8");D3DPRESENT_PARAMETERS p;ZeroMemory(&p,sizeof(p));p.BackBufferWidth=p.BackBufferHeight=8;p.BackBufferCount=1;p.BackBufferFormat=21;p.SwapEffect=1;p.Windowed=TRUE;p.hDeviceWindow=window;sc_hr(IDirect3D8_CreateDevice(r,0,1,window,32,&p,&sc_device),"FAIL device8");sc_check(IDirect3D8_Release(r)==1,"FAIL root refs8");sc_words=0;sc_cases();sc_check(sc_words==256&&!IDirect3DDevice8_Release(sc_device),"FAIL final refs8");char text[220];wsprintfA(text,"STOCORE CHECK mode=%u sdk=120 localWords=256 PSVersions=5 rawQueries=1 callbackFailure=1 snapshotDetached=1 rebuildCapture=1 raw8Metadata=1 reset=1 finalRefs=0",mode);sc_report(text);}
