#ifndef D8WG_CORE_VOLUME_TEXTURE_H
#define D8WG_CORE_VOLUME_TEXTURE_H
#include <d3d8.h>
#include "../d3d9proxy/d3d9_core.h"
/* Consumes one owned core volume texture reference on every return path. Device8
 * metadata must outlive actual core resource storage. Level volumes use the
 * resulting VolumeTexture8 as their real frontend container andreference owner. */
HRESULT WINAPI d8wg_core_wrap_volume_texture8(struct IDirect3DVolumeTexture9 *owned,
        IDirect3DDevice8 *device, IDirect3DVolumeTexture8 **out);
/* Borrowed while the frontend view/storage is retained. */
struct IDirect3DVolumeTexture9 *WINAPI d8wg_core_volume_texture_backend8(IDirect3DVolumeTexture8 *texture);
#endif
