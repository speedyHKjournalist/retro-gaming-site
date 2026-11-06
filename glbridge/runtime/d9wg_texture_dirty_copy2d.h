#ifndef D9WG_TEXTURE_DIRTY_COPY2D_H
#define D9WG_TEXTURE_DIRTY_COPY2D_H
static BOOL texture_copy_dirty_rect(D9Texture *source, D9Texture *destination,
        UINT source_level, UINT destination_level, const RECT *area)
{
    D9TextureLevel *from = &source->levels[source_level];
    D9TextureLevel *to = &destination->levels[destination_level];
    UINT bw, bh, bytes;
    if (!d9wg_format_layout(device_compatibility_api(source->device), source->format,
            &bw, &bh, &bytes)) return FALSE;
    UINT x = (UINT)area->left / bw * bytes;
    UINT rows = ((UINT)area->bottom + bh - 1u) / bh;
    UINT row_bytes = (((UINT)area->right + bw - 1u) / bw) * bytes - x;
    for (UINT y = (UINT)area->top / bh; y < rows; ++y)
        CopyMemory(to->shadow + y * to->row_pitch + x,
                from->shadow + y * from->row_pitch + x, row_bytes);
    if (!emit_texture_update(destination, destination_level, area)) return FALSE;
    if (!area->left && !area->top && (UINT)area->right == to->width
            && (UINT)area->bottom == to->height) to->shadow_valid = TRUE;
    return TRUE;
}

static HRESULT texture_update_dirty(D9Texture *source, D9Texture *destination)
{
    D9WGTextureCopyExtent from = {source->width, source->height, 1, source->level_count};
    D9WGTextureCopyExtent to = {destination->width, destination->height, 1, destination->level_count};
    D9WGTextureCopyPlan plan;
    if (!d9wg_texture_copy_plan(&from, &to, &plan)) return D3DERR_INVALIDCALL;
    D9WGTextureCopyContext context = {source->levels, destination->levels,
        source->level_count, destination->level_count, D9WG_COPY_2D, destination, TRUE};
    /* Validate the full operation even when no bytes are dirty. Keep native
     * locked-container behavior and fail before modifying destination pixels. */
    for (UINT i = 0; i < plan.levels; ++i)
        if (d9wg_validate_texture_copy_level(&context, 0, plan.source_first + i, i))
            return D3DERR_INVALIDCALL;
    for (UINT i = 0; i < plan.levels; ++i)
        if (d9wg_prepare_texture_copy_level(&context, 0, plan.source_first + i, i))
            return E_OUTOFMEMORY;
    for (UINT i = 0; i < plan.levels; ++i) {
        D9TextureLevel *data = &source->levels[plan.source_first + i];
        if (data->dirty_full) {
            RECT full = {0, 0, data->width, data->height};
            if (!texture_copy_dirty_rect(source, destination, plan.source_first + i, i, &full))
                return D3DERR_DRIVERINTERNALERROR;
        } else for (D9TextureDirtyRect *node = data->dirty_rects; node; node = node->next)
            if (!texture_copy_dirty_rect(source, destination, plan.source_first + i, i, &node->rect))
                return D3DERR_DRIVERINTERNALERROR;
    }
    for (UINT i = 0; i < source->level_count; ++i) texture_dirty_clear(&source->levels[i]);
    source->dirty_pending = FALSE;
    return D3D_OK;
}
#endif
