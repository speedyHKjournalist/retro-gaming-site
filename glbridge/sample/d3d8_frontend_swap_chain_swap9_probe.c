#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
void sc_check(BOOL,const char*);
static void sa9_hr(HRESULT hr,const char*m){sc_check(hr==D3D_OK,m);}
void WINAPI sa9_read(IDirect3DDevice9*d,IDirect3DSwapChain9*chain,DWORD color){IDirect3DSurface9*back=NULL,*cpu=NULL;sa9_hr(IDirect3DSwapChain9_GetBackBuffer(chain,0,0,&back),"FAIL Swap8 reference back");sa9_hr(IDirect3DDevice9_CreateOffscreenPlainSurface(d,4,4,21,2,&cpu,NULL),"FAIL Swap8 reference CPU image");sa9_hr(IDirect3DDevice9_GetRenderTargetData(d,back,cpu),"FAIL Swap8 actual GPU readback");D3DLOCKED_RECT lock;sa9_hr(IDirect3DSurface9_LockRect(cpu,&lock,NULL,D3DLOCK_READONLY),"FAIL Swap8 reference read lock");for(UINT y=0;y<4;y++)for(UINT x=0;x<4;x++)sc_check(((DWORD*)((BYTE*)lock.pBits+y*lock.Pitch))[x]==color,"FAIL Swap8 readback pixels");sa9_hr(IDirect3DSurface9_UnlockRect(cpu),"FAIL Swap8 reference read unlock");IDirect3DSurface9_Release(back);sc_check(!IDirect3DSurface9_Release(cpu),"FAIL Swap8 reference CPU refs");}
