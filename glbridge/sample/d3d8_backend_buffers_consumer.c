/* Test-only SDK8 embedding frontend for the actual private core constructor. */
#define WIN32_LEAN_AND_MEAN
#include <d3d8.h>
#include "../d3d9proxy/d3d9_core.h"
_Static_assert(sizeof(D3DPRESENT_PARAMETERS)==D9WG_D3D8_PARAMETERS_BYTES,"PP8 bytes");
_Static_assert(__builtin_offsetof(D3DPRESENT_PARAMETERS,SwapEffect)==20,"PP8 swap");
_Static_assert(__builtin_offsetof(D3DPRESENT_PARAMETERS,hDeviceWindow)==24,"PP8 HWND");
struct IDirect3D9 *WINAPI Core8CreateFactory(UINT sdk)
{
    return sdk==120||sdk==220?d9wg_d3d9_core_create8():NULL;
}
HRESULT WINAPI Core8CreateDevice(struct IDirect3D9 *root,UINT adapter,UINT type,
        HWND window,DWORD behavior,const D3DPRESENT_PARAMETERS *parameters,
        struct IDirect3DDevice9 **out)
{
    return d9wg_d3d9_core_create_device8(root,adapter,type,window,behavior,parameters,out);
}
HRESULT WINAPI Core8Reset(struct IDirect3DDevice9 *device,
        const D3DPRESENT_PARAMETERS *parameters)
{
    return d9wg_d3d9_core_reset8(device,parameters);
}
HRESULT WINAPI Core8Additional(struct IDirect3DDevice9 *device,
        const D3DPRESENT_PARAMETERS *parameters,struct IDirect3DSwapChain9 **out)
{
    return d9wg_d3d9_core_additional8(device,parameters,out);
}
HRESULT WINAPI Core8RenderTarget(struct IDirect3DDevice9 *device,
        UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE samples,
        BOOL lockable, struct IDirect3DSurface9 **out)
{
    return d9wg_d3d9_core_render_target8(device, width, height, format,
            samples, lockable, out);
}
HRESULT WINAPI Core8DepthSurface(struct IDirect3DDevice9 *device,
        UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE samples,
        struct IDirect3DSurface9 **out)
{
    return d9wg_d3d9_core_depth_surface8(device, width, height, format, samples, out);
}
_Static_assert(sizeof(D3DSURFACE_DESC)==D9WG_D3D8_SURFACE_DESC_BYTES,"DESC8 bytes");
_Static_assert(__builtin_offsetof(D3DSURFACE_DESC,Size)==16,"DESC8 size offset");
_Static_assert(__builtin_offsetof(D3DSURFACE_DESC,MultiSampleType)==20,"DESC8 samples offset");
HRESULT WINAPI Core8ImageSurface(struct IDirect3DDevice9 *device,
        UINT width, UINT height, D3DFORMAT format, struct IDirect3DSurface9 **out)
{
    return d9wg_d3d9_core_image_surface8(device, width, height, format, out);
}
HRESULT WINAPI Core8SurfaceDesc(struct IDirect3DSurface9 *surface,
        D3DSURFACE_DESC *out, UINT bytes)
{
    return d9wg_d3d9_core_surface_desc8(surface, out, bytes);
}
HRESULT WINAPI Core8Texture(struct IDirect3DDevice9 *device,
        UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format,
        D3DPOOL pool, struct IDirect3DTexture9 **out)
{
    return d9wg_d3d9_core_texture8(device,width,height,levels,usage,format,pool,out);
}
HRESULT WINAPI Core8CubeTexture(struct IDirect3DDevice9 *device,
        UINT edge, UINT levels, DWORD usage, D3DFORMAT format,
        D3DPOOL pool, struct IDirect3DCubeTexture9 **out)
{
    return d9wg_d3d9_core_cube_texture8(device,edge,levels,usage,format,pool,out);
}
_Static_assert(sizeof(D3DVOLUME_DESC)==D9WG_D3D8_VOLUME_DESC_BYTES,"VOLUME8 bytes");
_Static_assert(__builtin_offsetof(D3DVOLUME_DESC,Size)==16,"VOLUME8 Size");
_Static_assert(__builtin_offsetof(D3DVOLUME_DESC,Depth)==28,"VOLUME8 Depth");
HRESULT WINAPI Core8VolumeTexture(struct IDirect3DDevice9 *device,
        UINT width, UINT height, UINT depth, UINT levels, DWORD usage,
        D3DFORMAT format, D3DPOOL pool, struct IDirect3DVolumeTexture9 **out)
{
    return d9wg_d3d9_core_volume_texture8(device,width,height,depth,levels,
            usage,format,pool,out);
}
HRESULT WINAPI Core8VolumeDesc(struct IDirect3DVolume9 *volume,
        D3DVOLUME_DESC *out, UINT bytes)
{
    return d9wg_d3d9_core_volume_desc8(volume,out,bytes);
}
_Static_assert(sizeof(D3DVERTEXBUFFER_DESC)==24,"VB8 desc bytes");
_Static_assert(sizeof(D3DINDEXBUFFER_DESC)==20,"IB8 desc bytes");
_Static_assert(__builtin_offsetof(D3DVERTEXBUFFER_DESC,FVF)==20,"VB8 FVF offset");
HRESULT WINAPI Core8VertexBuffer(struct IDirect3DDevice9 *device,
        UINT length, DWORD usage, DWORD fvf, D3DPOOL pool,
        struct IDirect3DVertexBuffer9 **out)
{
    return d9wg_d3d9_core_vertex_buffer8(device,length,usage,fvf,pool,out);
}
HRESULT WINAPI Core8IndexBuffer(struct IDirect3DDevice9 *device,
        UINT length, DWORD usage, D3DFORMAT format, D3DPOOL pool,
        struct IDirect3DIndexBuffer9 **out)
{
    return d9wg_d3d9_core_index_buffer8(device,length,usage,format,pool,out);
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID reserved)
{
    return d9wg_d3d9_core_module(instance,reason,reserved);
}
