#define WIN32_LEAN_AND_MEAN
#include <d3d8.h>
#include "../d3d9proxy/d3d9_core.h"
_Static_assert(sizeof(D3DPRESENT_PARAMETERS)==52,"SDK8 PP");
struct IDirect3D9*WINAPI Core8CreateFactory(UINT sdk){return sdk==120||sdk==220?d9wg_d3d9_core_create8():NULL;}
HRESULT WINAPI Core8CreateDevice(struct IDirect3D9*r,UINT a,UINT t,HWND w,DWORD b,const D3DPRESENT_PARAMETERS*p,struct IDirect3DDevice9**d){return d9wg_d3d9_core_create_device8(r,a,t,w,b,p,d);}
HRESULT WINAPI Core8Reset(struct IDirect3DDevice9*d,const D3DPRESENT_PARAMETERS*p){return d9wg_d3d9_core_reset8(d,p);}
HRESULT WINAPI Core8Texture(struct IDirect3DDevice9*d,UINT w,UINT h,UINT l,DWORD u,D3DFORMAT f,D3DPOOL p,struct IDirect3DTexture9**t){return d9wg_d3d9_core_texture8(d,w,h,l,u,f,p,t);}
HRESULT WINAPI Core8CubeTexture(struct IDirect3DDevice9*d,UINT e,UINT l,DWORD u,D3DFORMAT f,D3DPOOL p,struct IDirect3DCubeTexture9**t){return d9wg_d3d9_core_cube_texture8(d,e,l,u,f,p,t);}
HRESULT WINAPI Core8VolumeTexture(struct IDirect3DDevice9*d,UINT w,UINT h,UINT z,UINT l,DWORD u,D3DFORMAT f,D3DPOOL p,struct IDirect3DVolumeTexture9**t){return d9wg_d3d9_core_volume_texture8(d,w,h,z,l,u,f,p,t);}
HRESULT WINAPI Core8UpdateTexture(struct IDirect3DDevice9*d,struct IDirect3DBaseTexture9*s,struct IDirect3DBaseTexture9*t){return d9wg_d3d9_core_update_texture8(d,s,t);}
BOOL WINAPI DllMain(HINSTANCE i,DWORD r,LPVOID p){return d9wg_d3d9_core_module(i,r,p);}
