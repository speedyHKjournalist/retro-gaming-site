#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <stdint.h>
#include <windows.h>
#include <d3d9.h>
void sc_check(BOOL,const char*);void sc_report(const char*);
static void ta9_hr(HRESULT hr,const char*message){sc_check(hr==D3D_OK,message);}
HRESULT WINAPI ta9_level(IDirect3DTexture9*t,IDirect3DSurface9**out){return IDirect3DTexture9_GetSurfaceLevel(t,0,out);}
HRESULT WINAPI ta9_cube_level(IDirect3DCubeTexture9*t,IDirect3DSurface9**out){return IDirect3DCubeTexture9_GetCubeMapSurface(t,0,0,out);}
HRESULT WINAPI ta9_back(IDirect3DDevice9*d,IDirect3DSurface9**out){return IDirect3DDevice9_GetBackBuffer(d,0,0,0,out);}
HRESULT WINAPI ta9_depth(IDirect3DDevice9*d,IDirect3DSurface9**out){return IDirect3DDevice9_GetDepthStencilSurface(d,out);}
HRESULT WINAPI ta9_rt(IDirect3DDevice9*d,IDirect3DSurface9**out){return IDirect3DDevice9_CreateRenderTarget(d,4,4,21,0,0,TRUE,out,NULL);}
HRESULT WINAPI ta9_chain(IDirect3DDevice9*d,HWND window,IDirect3DSwapChain9**out,IDirect3DSurface9**back){D3DPRESENT_PARAMETERS p;ZeroMemory(&p,sizeof(p));p.BackBufferWidth=p.BackBufferHeight=4;p.BackBufferCount=1;p.BackBufferFormat=21;p.SwapEffect=1;p.hDeviceWindow=window;p.Windowed=TRUE;HRESULT hr=IDirect3DDevice9_CreateAdditionalSwapChain(d,&p,out);return FAILED(hr)?hr:IDirect3DSwapChain9_GetBackBuffer(*out,0,0,back);}
void WINAPI ta9_draw(HMODULE module,IDirect3DDevice9*d,IDirect3DTexture9*t,DWORD color){
    HRESULT(WINAPI*fvf)(IDirect3DDevice9*,DWORD)=(void*)(uintptr_t)GetProcAddress(module,"Core8SetFVF");sc_check(fvf!=NULL,"FAIL surface FVF export");
    ta9_hr(IDirect3DDevice9_SetRenderState(d,7,0),"FAIL surface draw depth");ta9_hr(IDirect3DDevice9_SetRenderState(d,137,0),"FAIL surface draw lighting");ta9_hr(IDirect3DDevice9_SetRenderState(d,22,1),"FAIL surface draw cull");
    ta9_hr(IDirect3DDevice9_SetTextureStageState(d,0,1,2),"FAIL surface draw colorop");ta9_hr(IDirect3DDevice9_SetTextureStageState(d,0,2,2),"FAIL surface draw colorarg");ta9_hr(IDirect3DDevice9_SetTextureStageState(d,0,4,2),"FAIL surface draw alphaop");ta9_hr(IDirect3DDevice9_SetTextureStageState(d,0,5,2),"FAIL surface draw alphaarg");
    ta9_hr(IDirect3DDevice9_SetSamplerState(d,0,5,1),"FAIL surface draw mag");ta9_hr(IDirect3DDevice9_SetSamplerState(d,0,6,1),"FAIL surface draw min");ta9_hr(IDirect3DDevice9_SetSamplerState(d,0,1,3),"FAIL surface draw addressU");ta9_hr(IDirect3DDevice9_SetSamplerState(d,0,2,3),"FAIL surface draw addressV");
    ta9_hr(IDirect3DDevice9_SetTexture(d,0,(IDirect3DBaseTexture9*)t),"FAIL surface draw texture");ta9_hr(fvf(d,0x4144),"FAIL surface draw FVF8");
    struct Vertex{float x,y,z,w;DWORD color;float u,v;};const struct Vertex vertices[6]={{-1,-1,.5f,1,0xffffffffu,0,0},{9,-1,.5f,1,0xffffffffu,1,0},{-1,9,.5f,1,0xffffffffu,0,1},{-1,9,.5f,1,0xffffffffu,0,1},{9,-1,.5f,1,0xffffffffu,1,0},{9,9,.5f,1,0xffffffffu,1,1}};
    ta9_hr(IDirect3DDevice9_Clear(d,0,NULL,1,0xff010203u,1,0),"FAIL surface draw clear");ta9_hr(IDirect3DDevice9_BeginScene(d),"FAIL surface draw begin");ta9_hr(IDirect3DDevice9_DrawPrimitiveUP(d,4,2,vertices,sizeof(vertices[0])),"FAIL actual surface adapter upload draw");ta9_hr(IDirect3DDevice9_EndScene(d),"FAIL surface draw end");
    IDirect3DSurface9*cpu=NULL,*back=NULL;ta9_hr(IDirect3DDevice9_CreateOffscreenPlainSurface(d,8,8,21,2,&cpu,NULL),"FAIL surface draw read image");ta9_hr(IDirect3DDevice9_GetBackBuffer(d,0,0,0,&back),"FAIL surface draw read back");ta9_hr(IDirect3DDevice9_GetRenderTargetData(d,back,cpu),"FAIL surface draw readback");D3DLOCKED_RECT lock;ta9_hr(IDirect3DSurface9_LockRect(cpu,&lock,NULL,D3DLOCK_READONLY),"FAIL surface draw read lock");for(UINT y=0;y<8;y++)for(UINT x=0;x<8;x++)sc_check(((DWORD*)((BYTE*)lock.pBits+y*lock.Pitch))[x]==color,"FAIL surface adapter GPU pixels");ta9_hr(IDirect3DSurface9_UnlockRect(cpu),"FAIL surface draw read unlock");IDirect3DSurface9_Release(back);sc_check(!IDirect3DSurface9_Release(cpu),"FAIL surface draw read refs");
}
void WINAPI ta9_unbind(IDirect3DDevice9*d){ta9_hr(IDirect3DDevice9_SetTexture(d,0,NULL),"FAIL surface final texture clear");ta9_hr(IDirect3DDevice9_SetVertexShader(d,NULL),"FAIL surface final shader clear");ta9_hr(IDirect3DDevice9_SetVertexDeclaration(d,NULL),"FAIL surface final layout clear");}
