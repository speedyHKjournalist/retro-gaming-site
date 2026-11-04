#ifndef D8WG_CORE_SURFACE_H
#define D8WG_CORE_SURFACE_H
#include <d3d8.h>
#include "../d3d9proxy/d3d9_core.h"
/* Consumes one owned core surface reference on every return path. Device and
 * container are frontend8 interfaces whose storage outlives the core surface.
 * Texture/cube levels use their container's public count. Other surfaces own
 * a public count and one frontend device pin across its zero/nonzero edges.
 * The weak core registry owns the wrapper metadata until storage retirement. */
HRESULT WINAPI d8wg_core_wrap_surface8(struct IDirect3DSurface9 *owned_surface,
        IDirect3DDevice8 *device, IUnknown *container, BOOL container_refcount,
        IDirect3DSurface8 **out);
#endif
