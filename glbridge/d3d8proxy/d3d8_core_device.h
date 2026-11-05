#ifndef D8WG_CORE_DEVICE_H
#define D8WG_CORE_DEVICE_H
#include <d3d8.h>
#include "../d3d9proxy/d3d9_core.h"
/* The frontend owns a root8 reference and one aggregate public core reference.
 * Resource storage, GPU state and transport belong exclusively to core9. */
HRESULT WINAPI d8wg_core_create_device8(IDirect3D8 *parent,
        struct IDirect3D9 *backend_parent, UINT adapter, D3DDEVTYPE type,
        HWND focus, DWORD behavior, D3DPRESENT_PARAMETERS *parameters,
        IDirect3DDevice8 **out);
ULONG WINAPI d8wg_core_device_child_add8(IDirect3DDevice8 *device);
ULONG WINAPI d8wg_core_device_child_release8(IDirect3DDevice8 *device);
#endif
