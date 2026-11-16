/* Minimal embedding frontend: its loader delegates to the static D3D9 core,
 * and its single public export exposes the real D3D9 factory to the SDK probe. */
#define WIN32_LEAN_AND_MEAN
#include "../d3d9proxy/d3d9_core.h"
struct IDirect3D9 *WINAPI D9CoreCreate9(UINT sdk_version)
{
    return d9wg_d3d9_core_create(sdk_version);
}
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    return d9wg_d3d9_core_module(instance, reason, reserved);
}
