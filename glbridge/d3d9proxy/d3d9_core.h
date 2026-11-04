#ifndef D9WG_D3D9_CORE_H
#define D9WG_D3D9_CORE_H
#include <windows.h>
/* Deliberately forward declared: a D3D8 frontend can include this boundary
 * without mixing incompatible D3D8 and D3D9 SDK declarations. Its adapter
 * implementation uses the D3D9 SDK in a separate compilation unit. */
struct IDirect3D9;
struct D9WGProcess;
/* One attach/detach pair per embedding frontend module, called from that
 * module's loader entry. Release all owned COM objects before explicit
 * detach. The common runtime coordinates one owner with other frontends. */
BOOL WINAPI d9wg_d3d9_core_module(HINSTANCE instance, DWORD reason, LPVOID reserved);
struct IDirect3D9 *WINAPI d9wg_d3d9_core_create(UINT sdk_version);
/* D3D8 owns the public wrapper; one private factory reference belongs to that
 * wrapper. Its module borrows this process attachment, never adds a second
 * runtime attachment, and delegates final flush/release to core_module. */
struct IDirect3D9 *WINAPI d9wg_d3d9_core_create8(void);
struct D9WGProcess *WINAPI d9wg_d3d9_core_process(void);
ULONG WINAPI d9wg_d3d9_core_root_release(struct IDirect3D9 *factory);
UINT WINAPI d9wg_d3d9_core_adapter_count(struct IDirect3D9 *factory);
HMONITOR WINAPI d9wg_d3d9_core_adapter_monitor(struct IDirect3D9 *factory, UINT adapter);
#endif
