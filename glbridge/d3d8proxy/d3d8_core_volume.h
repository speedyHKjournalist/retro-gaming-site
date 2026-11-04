#ifndef D8WG_CORE_VOLUME_H
#define D8WG_CORE_VOLUME_H
#include <d3d8.h>
#include "../d3d9proxy/d3d9_core.h"
/* Consumes the getter-owned core volume reference on every return path.
 * The frontend VolumeTexture8 container owns all public level references;
 * weak metadata retires with the real core level storage. */
HRESULT WINAPI d8wg_core_wrap_volume8(struct IDirect3DVolume9 *owned,
        IDirect3DDevice8 *device, IUnknown *container, IDirect3DVolume8 **out);
#endif
