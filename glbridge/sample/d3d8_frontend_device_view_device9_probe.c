#define WIN32_LEAN_AND_MEAN
#define COBJMACROS
#include <windows.h>
#include <d3d9.h>
static const GUID dv_private_tag={0x86456789,0x1234,0x5678,{1,2,3,4,5,6,7,8}};
HRESULT WINAPI dv9_private(IDirect3DSurface9*surface,IUnknown*object){return IDirect3DSurface9_SetPrivateData(surface,&dv_private_tag,object,sizeof(object),D3DSPD_IUNKNOWN);}
