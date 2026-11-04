#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d8.h>
#include "d3d8_backend_states_cases.h"
void sc_probe8(HMODULE module,HWND window,UINT mode)
{
    sc_check(sc_sampler[0]==1,"FAIL test sampler table");
    IDirect3D8*(WINAPI*make)(UINT)=(void*)GetProcAddress(module,"Direct3DCreate8");
    sc_check(make!=NULL,"FAIL factory8 export");IDirect3D8*root=make(120);sc_check(root!=NULL,"FAIL factory8");
    D3DPRESENT_PARAMETERS p;ZeroMemory(&p,sizeof(p));p.BackBufferWidth=p.BackBufferHeight=8;p.BackBufferCount=1;p.BackBufferFormat=21;p.SwapEffect=1;p.hDeviceWindow=window;p.Windowed=TRUE;
    IDirect3DDevice8*d=NULL;sc_hr(IDirect3D8_CreateDevice(root,0,1,window,32,&p,&d),"FAIL device8");sc_check(IDirect3D8_Release(root)==1,"FAIL root8 release");DWORD value;
    for(UINT n=0;n<7;n++){sc_hr(IDirect3DDevice8_SetRenderState(d,sc_dropped[n],0x5aa50000u+n),"FAIL drop8");sc_hr(IDirect3DDevice8_GetRenderState(d,sc_dropped[n],&value),"FAIL drop8 query");sc_check(value==0x5aa50000u+n,"FAIL raw drop8");sc_hr(IDirect3DDevice8_SetRenderState(d,sc_dropped[n],value),"FAIL duplicate drop8");}
    for(UINT n=0;n<6;n++){sc_hr(IDirect3DDevice8_SetRenderState(d,D3DRS_ZBIAS,sc_bias[n]),"FAIL bias8");sc_hr(IDirect3DDevice8_GetRenderState(d,D3DRS_ZBIAS,&value),"FAIL bias8 query");sc_check(value==sc_bias[n],"FAIL raw bias8");sc_hr(IDirect3DDevice8_SetRenderState(d,D3DRS_ZBIAS,value),"FAIL duplicate bias8");}
    sc_hr(IDirect3DDevice8_SetRenderState(d,D3DRS_LIGHTING,0),"FAIL lighting8");sc_hr(IDirect3DDevice8_SetRenderState(d,D3DRS_CULLMODE,1),"FAIL cull8");sc_hr(IDirect3DDevice8_SetRenderState(d,D3DRS_TEXTUREFACTOR,0x11223344u),"FAIL factor8");
    for(UINT stage=0;stage<8;stage++)for(UINT n=0;n<10;n++){sc_hr(IDirect3DDevice8_SetTextureStageState(d,stage,sc_tss[n],sc_values[n]),"FAIL sampler8");sc_hr(IDirect3DDevice8_GetTextureStageState(d,stage,sc_tss[n],&value),"FAIL sampler8 query");sc_check(value==sc_values[n],"FAIL sampler8 raw");sc_hr(IDirect3DDevice8_SetTextureStageState(d,stage,sc_tss[n],value),"FAIL duplicate sampler8");}
    sc_hr(IDirect3DDevice8_SetTextureStageState(d,0,D3DTSS_COLOROP,3),"FAIL cascade8");
    sc_hr(IDirect3DDevice8_BeginStateBlock(d),"FAIL begin8");sc_hr(IDirect3DDevice8_SetRenderState(d,D3DRS_ZBIAS,9),"FAIL record bias8");sc_hr(IDirect3DDevice8_SetRenderState(d,D3DRS_ZENABLE,1),"FAIL record depth8");sc_hr(IDirect3DDevice8_SetRenderState(d,D3DRS_LINEPATTERN,0xaabb1234u),"FAIL record drop8");sc_hr(IDirect3DDevice8_SetTextureStageState(d,7,D3DTSS_ADDRESSW,2),"FAIL record sampler8");sc_hr(IDirect3DDevice8_SetTextureStageState(d,0,D3DTSS_COLOROP,2),"FAIL record cascade8");
    sc_hr(IDirect3DDevice8_GetRenderState(d,D3DRS_ZBIAS,&value),"FAIL live bias8 query");sc_check(value==0xffffffffu,"FAIL record changed live8");DWORD block=0;sc_hr(IDirect3DDevice8_EndStateBlock(d,&block),"FAIL end8");sc_check(block!=0,"FAIL block8 handle");
    sc_hr(IDirect3DDevice8_SetRenderState(d,D3DRS_ZBIAS,2),"FAIL change bias8");sc_hr(IDirect3DDevice8_SetTextureStageState(d,7,D3DTSS_ADDRESSW,1),"FAIL change sampler8");sc_hr(IDirect3DDevice8_SetTextureStageState(d,0,D3DTSS_COLOROP,4),"FAIL change cascade8");sc_hr(IDirect3DDevice8_ApplyStateBlock(d,block),"FAIL apply8");
    sc_hr(IDirect3DDevice8_GetRenderState(d,D3DRS_ZBIAS,&value),"FAIL applied bias8 query");sc_check(value==9,"FAIL applied bias8");sc_hr(IDirect3DDevice8_GetRenderState(d,D3DRS_LINEPATTERN,&value),"FAIL applied drop8 query");sc_check(value==0xaabb1234u,"FAIL applied drop8");
    sc_hr(IDirect3DDevice8_SetRenderState(d,D3DRS_ZBIAS,6),"FAIL capture bias8");sc_hr(IDirect3DDevice8_SetTextureStageState(d,7,D3DTSS_ADDRESSW,3),"FAIL capture sampler8");sc_hr(IDirect3DDevice8_CaptureStateBlock(d,block),"FAIL capture8");sc_hr(IDirect3DDevice8_SetRenderState(d,D3DRS_ZBIAS,2),"FAIL recapture bias8");sc_hr(IDirect3DDevice8_SetTextureStageState(d,7,D3DTSS_ADDRESSW,1),"FAIL recapture sampler8");sc_hr(IDirect3DDevice8_ApplyStateBlock(d,block),"FAIL captured apply8");sc_hr(IDirect3DDevice8_GetRenderState(d,D3DRS_ZBIAS,&value),"FAIL captured bias8 query");sc_check(value==6,"FAIL captured bias8");sc_hr(IDirect3DDevice8_DeleteStateBlock(d,block),"FAIL delete8");
    sc_check(IDirect3DDevice8_SetRenderState(d,256,1)==D3DERR_INVALIDCALL,"FAIL bound render8");sc_check(IDirect3DDevice8_SetTextureStageState(d,8,13,1)==D3DERR_INVALIDCALL,"FAIL bound stage8");sc_check(IDirect3DDevice8_SetTextureStageState(d,0,32,1)==D3DERR_INVALIDCALL,"FAIL bound TSS8");
    sc_hr(IDirect3DDevice8_Reset(d,&p),"FAIL reset8");sc_hr(IDirect3DDevice8_GetRenderState(d,D3DRS_ZBIAS,&value),"FAIL reset bias8 query");sc_check(value==0,"FAIL reset raw bias8");sc_check(IDirect3DDevice8_Release(d)==0,"FAIL final device8");
    sc_report(mode?"STATECORE CHECK mode=1 sevenDrops=1 zbias=6 samplers=80 recording=1 apply=1 capture=1 rawMetadata=1 reset=1 finalRefs=0":"STATECORE CHECK mode=0 sevenDrops=1 zbias=6 samplers=80 recording=1 apply=1 capture=1 rawMetadata=1 reset=1 finalRefs=0");
}
