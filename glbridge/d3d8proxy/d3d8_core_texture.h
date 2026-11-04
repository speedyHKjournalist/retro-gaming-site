#ifndef D8WG_CORE_TEXTURE_H
#define D8WG_CORE_TEXTURE_H
#include <d3d8.h>
#include "../d3d9proxy/d3d9_core.h"
/* Consumes one owned core texture reference on every return path. Device8
 * metadata must outlive actual core resource storage. Level surfaces use the
 * resulting Texture8 as their real frontend container andreference owner. */
HRESULT WINAPI d8wg_core_wrap_texture8(struct IDirect3DTexture9 *owned,
        IDirect3DDevice8 *device, IDirect3DTexture8 **out);
/* Borrowed while the frontend view/storage is retained. */
struct IDirect3DTexture9 *WINAPI d8wg_core_texture_backend8(IDirect3DTexture8 *texture);
#endif
