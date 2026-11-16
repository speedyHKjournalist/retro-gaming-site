#ifndef D8WG_CORE_BUFFER_H
#define D8WG_CORE_BUFFER_H
#include <d3d8.h>
#include "../d3d9proxy/d3d9_core.h"
/* Both constructors consume one getter-owned core reference on every path.
 * Cached metadata is weak; public refs retain one core and frontend device pin. */
HRESULT WINAPI d8wg_core_wrap_vertex_buffer8(struct IDirect3DVertexBuffer9 *owned,
        IDirect3DDevice8 *device, IDirect3DVertexBuffer8 **out);
HRESULT WINAPI d8wg_core_wrap_index_buffer8(struct IDirect3DIndexBuffer9 *owned,
        IDirect3DDevice8 *device, IDirect3DIndexBuffer8 **out);
/* Borrowed while the matching frontend view/storage remains retained. */
struct IDirect3DVertexBuffer9 *WINAPI d8wg_core_vertex_buffer_backend8(IDirect3DVertexBuffer8 *iface);
struct IDirect3DIndexBuffer9 *WINAPI d8wg_core_index_buffer_backend8(IDirect3DIndexBuffer8 *iface);
#endif
