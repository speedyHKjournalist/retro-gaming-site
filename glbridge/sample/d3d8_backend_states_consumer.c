#define WIN32_LEAN_AND_MEAN
#include <d3d8.h>
#include "../d3d9proxy/d3d9_core.h"
_Static_assert(sizeof(D3DPRESENT_PARAMETERS)==52,"SDK8 PP");
struct IDirect3D9 *WINAPI Core8CreateFactory(UINT sdk)
{
    return sdk==120||sdk==220?d9wg_d3d9_core_create8():NULL;
}
HRESULT WINAPI Core8CreateDevice(struct IDirect3D9 *root, UINT adapter, UINT type,
        HWND window, DWORD behavior, const D3DPRESENT_PARAMETERS *p,
        struct IDirect3DDevice9 **out)
{
    return d9wg_d3d9_core_create_device8(root,adapter,type,window,behavior,p,out);
}
HRESULT WINAPI Core8Reset(struct IDirect3DDevice9 *d,const D3DPRESENT_PARAMETERS*p)
{
    return d9wg_d3d9_core_reset8(d,p);
}
HRESULT WINAPI Core8RenderState(struct IDirect3DDevice9*d,D3DRENDERSTATETYPE state,DWORD value)
{
    return d9wg_d3d9_core_set_render_state8(d,state,value);
}
HRESULT WINAPI Core8TextureStageState(struct IDirect3DDevice9*d,UINT stage,
        D3DTEXTURESTAGESTATETYPE state,DWORD value)
{
    return d9wg_d3d9_core_set_texture_stage_state8(d,stage,state,value);
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID reserved)
{
    return d9wg_d3d9_core_module(instance,reason,reserved);
}
