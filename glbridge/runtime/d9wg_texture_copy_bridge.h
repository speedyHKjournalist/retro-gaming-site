#ifndef D9WG_TEXTURE_COPY_BRIDGE_H
#define D9WG_TEXTURE_COPY_BRIDGE_H
#include "d9wg_runtime.h"

enum { D9WG_COPY_2D, D9WG_COPY_CUBE, D9WG_COPY_VOLUME };
typedef struct D9WGTextureCopyContext {
    const D9WG_COPY_LEVEL *source;
    D9WG_COPY_LEVEL *destination;
    uint32_t source_stride, destination_stride, kind;
    void *texture;
} D9WGTextureCopyContext;

static int d9wg_validate_texture_copy_level(void *context, uint32_t layer,
        uint32_t source_level, uint32_t destination_level)
{
    D9WGTextureCopyContext *copy = context;
    const D9WG_COPY_LEVEL *from = copy->source + layer*copy->source_stride + source_level;
    const D9WG_COPY_LEVEL *to = copy->destination + layer*copy->destination_stride + destination_level;
    if (from->locked || to->locked || !from->shadow || !from->byte_count ||
            from->byte_count != to->byte_count || !to->width || !to->height ||
            from->width != to->width || from->height != to->height ||
            from->row_pitch != to->row_pitch || from->row_count != to->row_count ||
            from->slice_pitch != to->slice_pitch ||
            (copy->kind == D9WG_COPY_VOLUME && (!to->depth || from->depth != to->depth)))
        return D9WG_TEXTURE_COPY_INVALID;
    return D9WG_TEXTURE_COPY_OK;
}

static int d9wg_prepare_texture_copy_level(void *context, uint32_t layer,
        uint32_t source_level, uint32_t destination_level)
{
    D9WGTextureCopyContext *copy = context;
    D9WG_COPY_LEVEL *to = copy->destination + layer*copy->destination_stride + destination_level;
    (void)source_level;
    if (!to->shadow) {
        to->shadow = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, to->byte_count);
        if (!to->shadow) return D9WG_TEXTURE_COPY_OUT_OF_MEMORY;
    }
    return D9WG_TEXTURE_COPY_OK;
}

static int d9wg_commit_texture_copy_level(void *context, uint32_t layer,
        uint32_t source_level, uint32_t destination_level)
{
    D9WGTextureCopyContext *copy = context;
    const D9WG_COPY_LEVEL *from = copy->source + layer*copy->source_stride + source_level;
    D9WG_COPY_LEVEL *to = copy->destination + layer*copy->destination_stride + destination_level;
    CopyMemory(to->shadow, from->shadow, to->byte_count);
    D9WG_COPY_MARK_VALID(to);
    BOOL emitted;
    if (copy->kind == D9WG_COPY_VOLUME) {
        D3DBOX full;ZeroMemory(&full, sizeof(full));
        full.Right = to->width;full.Bottom = to->height;full.Back = to->depth;
        emitted = D9WG_COPY_UPLOAD_VOLUME((D9WG_COPY_VOLUME_TYPE *)copy->texture, destination_level, &full);
    } else {
        RECT full;SetRect(&full, 0, 0, (int)to->width, (int)to->height);
        if (copy->kind == D9WG_COPY_CUBE)
            emitted = D9WG_COPY_UPLOAD_CUBE((D9WG_COPY_CUBE_TYPE *)copy->texture, layer, destination_level, &full);
        else emitted = D9WG_COPY_UPLOAD_2D((D9WG_COPY_2D_TYPE *)copy->texture, destination_level, &full);
    }
    return emitted ? D9WG_TEXTURE_COPY_OK : D9WG_TEXTURE_COPY_EMIT_FAILED;
}

static HRESULT d9wg_update_texture_common(void *texture, uint32_t kind,
        const D9WG_COPY_LEVEL *source, D9WGTextureCopyExtent source_extent,
        D9WG_COPY_LEVEL *destination, D9WGTextureCopyExtent destination_extent)
{
    D9WGTextureCopyPlan plan;
    if (kind > D9WG_COPY_VOLUME || !d9wg_texture_copy_plan(&source_extent, &destination_extent, &plan))
        return D3DERR_INVALIDCALL;
    if (!plan.levels) return D3D_OK;
    if (!source || !destination) return D3DERR_INVALIDCALL;
    D9WGTextureCopyContext context = { source, destination, source_extent.levels,
        destination_extent.levels, kind, texture };
    int result = d9wg_texture_copy_levels(&plan, kind == D9WG_COPY_CUBE ? 6u : 1u,
            d9wg_validate_texture_copy_level, d9wg_prepare_texture_copy_level,
            d9wg_commit_texture_copy_level, &context);
    return result == D9WG_TEXTURE_COPY_INVALID ? D3DERR_INVALIDCALL :
            result == D9WG_TEXTURE_COPY_OUT_OF_MEMORY ? E_OUTOFMEMORY :
            result ? D3DERR_DRIVERINTERNALERROR : D3D_OK;
}
#undef D9WG_COPY_LEVEL
#undef D9WG_COPY_2D_TYPE
#undef D9WG_COPY_CUBE_TYPE
#undef D9WG_COPY_VOLUME_TYPE
#undef D9WG_COPY_MARK_VALID
#undef D9WG_COPY_UPLOAD_2D
#undef D9WG_COPY_UPLOAD_CUBE
#undef D9WG_COPY_UPLOAD_VOLUME
#endif
