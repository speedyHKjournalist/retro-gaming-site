#ifndef D9WG_TEXTURE_DIRTY2D_H
#define D9WG_TEXTURE_DIRTY2D_H
/* Exact CPU dirty rectangles. Never merge disjoint regions into a bounding
 * box: NO_DIRTY_UPDATE writes in the gap must remain invisible to the GPU. */
static void texture_dirty_clear(D9TextureLevel *level)
{
    while (level->dirty_rects) {
        D9TextureDirtyRect *node = level->dirty_rects;
        level->dirty_rects = node->next;
        HeapFree(GetProcessHeap(), 0, node);
    }
    level->dirty_full = FALSE;
}

static void texture_dirty_all(D9Texture *texture)
{
    for (UINT i = 0; i < texture->level_count; ++i) {
        texture_dirty_clear(&texture->levels[i]);
        texture->levels[i].dirty_full = TRUE;
    }
    texture->dirty_pending = TRUE;
    if (texture->pool == D3DPOOL_MANAGED)
        texture->device->managed_texture_dirty = TRUE;
}

static BOOL texture_mark_dirty(D9Texture *texture, UINT level, const RECT *rect)
{
    D9TextureDirtyRect *prepared[32] = {0};
    BOOL full[32] = {0};
    UINT bw, bh, bytes;
    if (texture->pool != D3DPOOL_MANAGED && texture->pool != D3DPOOL_SYSTEMMEM)
        return TRUE;
    /* UpdateTexture dirties only level zero; a managed sublevel can also
     * be uploaded independently. A level-zero region propagates to all mips. */
    if (level && texture->pool == D3DPOOL_SYSTEMMEM) return TRUE;
    if (level >= texture->level_count || texture->level_count > 32u
            || !d9wg_format_layout(device_compatibility_api(texture->device),
                texture->format, &bw, &bh, &bytes)) return FALSE;
    UINT end = level ? level + 1u : texture->level_count;
    RECT base = rect ? *rect : (RECT){0, 0,
        texture->levels[level].width, texture->levels[level].height};
    for (UINT i = level; i < end; ++i) {
        D9TextureLevel *data = &texture->levels[i];
        if (data->dirty_full) continue;
        UINT scale = 1u << (i - level);
        RECT area = {(UINT)base.left / scale, (UINT)base.top / scale,
            (UINT)base.right / scale + !!((UINT)base.right % scale),
            (UINT)base.bottom / scale + !!((UINT)base.bottom % scale)};
        area.left = (UINT)area.left / bw * bw;
        area.top = (UINT)area.top / bh * bh;
        area.right = ((UINT)area.right + bw - 1u) / bw * bw;
        area.bottom = ((UINT)area.bottom + bh - 1u) / bh * bh;
        if ((UINT)area.right > data->width) area.right = data->width;
        if ((UINT)area.bottom > data->height) area.bottom = data->height;
        full[i] = !area.left && !area.top && (UINT)area.right == data->width
                && (UINT)area.bottom == data->height;
        if (full[i]) continue;
        BOOL contained = FALSE;
        for (D9TextureDirtyRect *node = data->dirty_rects; node; node = node->next)
            if (area.left >= node->rect.left && area.top >= node->rect.top
                    && area.right <= node->rect.right && area.bottom <= node->rect.bottom)
                { contained = TRUE; break; }
        if (contained) continue;
        prepared[i] = HeapAlloc(GetProcessHeap(), 0, sizeof(*prepared[i]));
        if (!prepared[i]) {
            for (UINT j = level; j < i; ++j) HeapFree(GetProcessHeap(), 0, prepared[j]);
            return FALSE;
        }
        prepared[i]->rect = area;
    }
    /* All allocations succeeded: failure cannot partially dirty a mip chain. */
    for (UINT i = level; i < end; ++i) {
        if (full[i]) {
            texture_dirty_clear(&texture->levels[i]);
            texture->levels[i].dirty_full = TRUE;
        } else if (prepared[i]) {
            prepared[i]->next = texture->levels[i].dirty_rects;
            texture->levels[i].dirty_rects = prepared[i];
        }
    }
    texture->dirty_pending = TRUE;
    if (texture->pool == D3DPOOL_MANAGED)
        texture->device->managed_texture_dirty = TRUE;
    return TRUE;
}

static BOOL texture_flush_dirty(D9Texture *texture)
{
    if (!texture->dirty_pending) return TRUE;
    for (UINT i = 0; i < texture->level_count; ++i) {
        D9TextureLevel *data = &texture->levels[i];
        if (data->dirty_full) {
            RECT full = {0, 0, data->width, data->height};
            if (!emit_texture_update(texture, i, &full)) return FALSE;
            texture_dirty_clear(data);
        }
        while (data->dirty_rects) {
            D9TextureDirtyRect *node = data->dirty_rects;
            if (!emit_texture_update(texture, i, &node->rect)) return FALSE;
            data->dirty_rects = node->next;
            HeapFree(GetProcessHeap(), 0, node);
        }
    }
    texture->dirty_pending = FALSE;
    return TRUE;
}

static BOOL flush_managed_textures(D9Device *device)
{
    /* No sampler scan on draws with unchanged textures. */
    if (!device->managed_texture_dirty) return TRUE;
    for (UINT i = 0; i < D9_MAX_TEXTURE_BINDINGS; ++i) {
        D9Texture *texture = device->textures[i];
        if (texture && texture->pool == D3DPOOL_MANAGED && !texture_flush_dirty(texture))
            return FALSE;
    }
    device->managed_texture_dirty = FALSE;
    return TRUE;
}
#endif
