#define WIN32_LEAN_AND_MEAN
#include <d3d8.h>
#include "../d3d9proxy/d3d9_core.h"
#include "../d3d8proxy/d3d8_core_surface.h"
_Static_assert(sizeof(D3DPRESENT_PARAMETERS)==52,"SDK8 PP");
struct IDirect3D9*WINAPI Core8CreateFactory(UINT sdk){return sdk==120||sdk==220?d9wg_d3d9_core_create8():NULL;}
HRESULT WINAPI Core8CreateDevice(struct IDirect3D9*r,UINT a,UINT t,HWND w,DWORD b,const D3DPRESENT_PARAMETERS*p,struct IDirect3DDevice9**d){return d9wg_d3d9_core_create_device8(r,a,t,w,b,p,d);}
HRESULT WINAPI Core8Reset(struct IDirect3DDevice9*d,const D3DPRESENT_PARAMETERS*p){return d9wg_d3d9_core_reset8(d,p);}
HRESULT WINAPI Core8ImageSurface(struct IDirect3DDevice9*d,UINT w,UINT h,D3DFORMAT f,struct IDirect3DSurface9**s){return d9wg_d3d9_core_image_surface8(d,w,h,f,s);}
HRESULT WINAPI Core8Texture(struct IDirect3DDevice9*d,UINT w,UINT h,UINT l,DWORD u,D3DFORMAT f,D3DPOOL p,struct IDirect3DTexture9**t){return d9wg_d3d9_core_texture8(d,w,h,l,u,f,p,t);}
HRESULT WINAPI Core8CubeTexture(struct IDirect3DDevice9*d,UINT e,UINT l,DWORD u,D3DFORMAT f,D3DPOOL p,struct IDirect3DCubeTexture9**t){return d9wg_d3d9_core_cube_texture8(d,e,l,u,f,p,t);}
HRESULT WINAPI Core8VertexShader(struct IDirect3DDevice9*d,const DWORD*t,const DWORD*c,struct IDirect3DVertexDeclaration9**layout,struct IDirect3DVertexShader9**out){return d9wg_d3d9_core_vertex_shader8(d,t,c,layout,out);}
HRESULT WINAPI Core8PixelShader(struct IDirect3DDevice9*d,const DWORD*c,struct IDirect3DPixelShader9**out){return d9wg_d3d9_core_pixel_shader8(d,c,out);}
HRESULT WINAPI Core8BindPair(struct IDirect3DDevice9*d,struct IDirect3DVertexDeclaration9*l,struct IDirect3DVertexShader9*s){return d9wg_d3d9_core_set_vertex_shader8(d,l,s);}
HRESULT WINAPI Core8Record(struct IDirect3DDevice9*d,D9WGStateBlockRecorder8 record,const void*snapshot,struct IDirect3DStateBlock9**out){return d9wg_d3d9_core_record_state_block8(d,record,snapshot,out);}
HRESULT WINAPI Core8State(struct IDirect3DDevice9*d,UINT s,DWORD v){return d9wg_d3d9_core_set_render_state8(d,s,v);}
HRESULT WINAPI Core8Stage(struct IDirect3DDevice9*d,UINT t,UINT s,DWORD v){return d9wg_d3d9_core_set_texture_stage_state8(d,t,s,v);}
BOOL WINAPI DllMain(HINSTANCE i,DWORD r,LPVOID p){return d9wg_d3d9_core_module(i,r,p);}

HRESULT WINAPI Core8TokenCreate(struct IDirect3DDevice9*d,D9WGStateBlockRecorder8 r,const void*s,DWORD*out){return d9wg_d3d9_core_create_state_block_handle8(d,r,s,out);}
HRESULT WINAPI Core8TokenCapture(struct IDirect3DDevice9*d,DWORD t,D9WGStateBlockRecorder8 r,const void*s){return d9wg_d3d9_core_capture_state_block_handle8(d,t,r,s);}
HRESULT WINAPI Core8TokenApply(struct IDirect3DDevice9*d,DWORD t){return d9wg_d3d9_core_apply_state_block_handle8(d,t);}
HRESULT WINAPI Core8TokenDelete(struct IDirect3DDevice9*d,DWORD t){return d9wg_d3d9_core_delete_state_block_handle8(d,t);}

HRESULT WINAPI Core8TokenBegin(struct IDirect3DDevice9*d){return d9wg_d3d9_core_begin_state_block8(d);}
HRESULT WINAPI Core8TokenEnd(struct IDirect3DDevice9*d,DWORD*out){return d9wg_d3d9_core_end_state_block_handle8(d,out);}

HRESULT WINAPI Core8ViewGet(struct IUnknown*object,void**out){return d9wg_d3d9_core_get_view8(object,out);}
HRESULT WINAPI Core8ViewAttach(struct IUnknown*object,void*view,D9WGCoreViewRetire8 retire){return d9wg_d3d9_core_attach_view8(object,view,retire);}

HRESULT WINAPI Core8WrapSurface(struct IDirect3DSurface9*s,IDirect3DDevice8*d,IUnknown*c,BOOL coupled,IDirect3DSurface8**out){return d8wg_core_wrap_surface8(s,d,c,coupled,out);}
HRESULT WINAPI Core8SetFVF(struct IDirect3DDevice9*d,DWORD f){return d9wg_d3d9_core_set_fvf8(d,f);}
