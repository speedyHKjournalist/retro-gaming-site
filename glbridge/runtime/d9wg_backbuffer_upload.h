#ifndef D9WG_BACKBUFFER_UPLOAD_H
#define D9WG_BACKBUFFER_UPLOAD_H
#include "../d3d9proxy/d3d9_protocol.h"
#include "d9wg_runtime.h"
#include "d9wg_surface_lock.h"
/* A temporary texture carries only the locked rectangle. Its upload is ordered
 * before the renderable-surface blit, and accepted creation is retired even if upload
 * or blit fails. The caller supplies a fresh process-global resource handle. */
static inline int d9wg_renderable_upload(uint32_t device, uint32_t handle,
        uint32_t format, uint32_t destination, uint32_t level, uint32_t face,
        const D9WGSurfaceLockRect *area, const uint8_t *pixels,
        uint32_t pitch, uint32_t pixel_bytes,
        int (*emit)(uint16_t, const void *, uint32_t),
        int (*emit_rows)(const D9WGTextureRows *))
{
    D9WGCreateTexture2D create = {0};
    D9WGTextureRows rows = {0};
    D9WGStretchRect copy = {0};
    D9WGDestroyResource destroy;
    create.device_handle = device;
    create.resource_handle = handle;
    create.width = (uint32_t)(area->right - area->left);
    create.height = (uint32_t)(area->bottom - area->top);
    create.level_count = 1;
    create.format = format;
    if (!emit(D9WG_OP_CREATE_TEXTURE_2D, &create, sizeof(create)))
        return 0;
    rows.resource_handle = handle;
    rows.width = create.width;
    rows.height = create.height;
    rows.block_height = 1;
    rows.row_count = create.height;
    rows.row_bytes = create.width * pixel_bytes;
    rows.source_pitch = pitch;
    rows.source = pixels;
    int result = emit_rows(&rows);
    if (result) {
        copy.device_handle = device;
        copy.source_texture_handle = handle;
        copy.source_right = create.width;
        copy.source_bottom = create.height;
        copy.destination_texture_handle = destination;
        copy.destination_level = level;
        copy.destination_face = face;
        copy.destination_left = area->left;
        copy.destination_top = area->top;
        copy.destination_right = area->right;
        copy.destination_bottom = area->bottom;
        copy.filter_point = 1;
        result = emit(D9WG_OP_STRETCH_RECT, &copy, sizeof(copy));
    }
    destroy.resource_handle = handle;
    destroy.resource_kind = D9WG_RESOURCE_TEXTURE_2D;
    if (!emit(D9WG_OP_DESTROY_RESOURCE, &destroy, sizeof(destroy)))
        result = 0;
    return result;
}
/* Implicit backbuffer adapter retained for API8 and existing copy callers. */
static inline int d9wg_backbuffer_upload(uint32_t device, uint32_t handle,
        uint32_t format, const D9WGSurfaceLockRect *area, const uint8_t *pixels,
        uint32_t pitch, uint32_t pixel_bytes,
        int (*emit)(uint16_t, const void *, uint32_t),
        int (*emit_rows)(const D9WGTextureRows *))
{
    return d9wg_renderable_upload(device, handle, format, 0, 0, 0, area,
            pixels, pitch, pixel_bytes, emit, emit_rows);
}
#endif
