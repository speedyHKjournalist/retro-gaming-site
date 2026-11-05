#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../d3d8proxy/d3d8_core_device.h"
struct IDirect3D9 *WINAPI Core8Factory(void) { return d9wg_d3d9_core_create8(); }
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{ return d9wg_d3d9_core_module(instance, reason, reserved); }
