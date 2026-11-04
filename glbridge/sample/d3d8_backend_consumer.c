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
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID reserved)
{
    return d9wg_d3d9_core_module(instance,reason,reserved);
}
