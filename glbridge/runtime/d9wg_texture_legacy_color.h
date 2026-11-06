#ifndef D9WG_TEXTURE_LEGACY_COLOR_H
#define D9WG_TEXTURE_LEGACY_COLOR_H

/* Retail D3D8 accepts these color conversions in UpdateTexture (Wine's
 * original visual test includes ARGB/XRGB and ARGB -> RGB565). D3D9 keeps
 * its same-format rule. Unknown / compressed format pairs remain rejected. */
static UINT legacy_color_bytes(D3DFORMAT format)
{
    return format == 21u || format == 22u ? 4u : format == 23u ? 2u : 0u;
}

static BOOL legacy_color_copy_supported(UINT api, D3DFORMAT from, D3DFORMAT to)
{
    return api == 8u && legacy_color_bytes(from) && legacy_color_bytes(to);
}

static BOOL legacy_color_layout(const D9TextureLevel *level, UINT bytes, BOOL source)
{
    return level && bytes && (!source || level->shadow) && level->width && level->height
            && (uint64_t)level->width * bytes <= level->row_pitch
            && level->row_count >= level->height
            && (uint64_t)level->row_pitch * (level->height - 1u)
                + (uint64_t)level->width * bytes <= level->byte_count;
}

static void legacy_color_pixel(const BYTE *from, UINT source_format,
        BYTE *to, UINT destination_format)
{
    UINT red, green, blue, alpha;
    if (source_format == 23u) {
        UINT color = from[0] | (UINT)from[1] << 8;
        red = (color >> 11) & 31u; red = (red << 3) | (red >> 2);
        green = (color >> 5) & 63u; green = (green << 2) | (green >> 4);
        blue = color & 31u; blue = (blue << 3) | (blue >> 2);
        alpha = 255u;
    } else {
        blue = from[0]; green = from[1]; red = from[2];
        alpha = source_format == 22u ? 255u : from[3];
    }
    if (destination_format == 23u) {
        UINT color = (red >> 3) << 11 | (green >> 2) << 5 | (blue >> 3);
        to[0] = (BYTE)color; to[1] = (BYTE)(color >> 8);
    } else {
        to[0] = (BYTE)blue; to[1] = (BYTE)green; to[2] = (BYTE)red;
        to[3] = destination_format == 22u ? 255u : (BYTE)alpha;
    }
}

static BOOL legacy_color_rect(void *destination, BOOL cube, UINT face, UINT level,
        const D9TextureLevel *from, D9TextureLevel *to,
        UINT source_format, UINT destination_format, RECT area)
{
    UINT width = from->width < to->width ? from->width : to->width;
    UINT height = from->height < to->height ? from->height : to->height;
    UINT source_bytes = legacy_color_bytes(source_format), destination_bytes = legacy_color_bytes(destination_format);
    if (area.left < 0) area.left = 0;
    if (area.top < 0) area.top = 0;
    if (area.right > (LONG)width) area.right = (LONG)width;
    if (area.bottom > (LONG)height) area.bottom = (LONG)height;
    if (area.right <= area.left || area.bottom <= area.top) return TRUE;
    for (UINT y = (UINT)area.top; y < (UINT)area.bottom; ++y) {
        const BYTE *s = from->shadow + y * from->row_pitch + (UINT)area.left * source_bytes;
        BYTE *d = to->shadow + y * to->row_pitch + (UINT)area.left * destination_bytes;
        if (source_format == destination_format)
            CopyMemory(d, s, ((UINT)area.right - (UINT)area.left) * source_bytes);
        else for (UINT x = (UINT)area.left; x < (UINT)area.right; ++x, s += source_bytes, d += destination_bytes)
            legacy_color_pixel(s, source_format, d, destination_format);
    }
    BOOL emitted = cube ? emit_cube_texture_update((D9CubeTexture *)destination, face, level, &area)
            : emit_texture_update((D9Texture *)destination, level, &area);
    if (emitted && !area.left && !area.top && (UINT)area.right == to->width && (UINT)area.bottom == to->height)
        to->shadow_valid = TRUE;
    return emitted;
}

static HRESULT texture_update_legacy_color(void *source, void *destination, BOOL cube)
{
    D9Texture *s2 = source, *d2 = destination;
    D9CubeTexture *sc = source, *dc = destination;
    UINT sf = cube ? sc->format : s2->format, df = cube ? dc->format : d2->format;
    D9Device *device = cube ? sc->device : s2->device;
    if (!legacy_color_copy_supported(device_compatibility_api(device), sf, df)) return D3DERR_INVALIDCALL;
    D9WGTextureCopyExtent from = cube
            ? (D9WGTextureCopyExtent){sc->edge_length, sc->edge_length, 1, sc->level_count}
            : (D9WGTextureCopyExtent){s2->width, s2->height, 1, s2->level_count};
    D9WGTextureCopyExtent to = cube
            ? (D9WGTextureCopyExtent){dc->edge_length, dc->edge_length, 1, dc->level_count}
            : (D9WGTextureCopyExtent){d2->width, d2->height, 1, d2->level_count};
    D9WGTextureCopyPlan plan;
    if (!d9wg_texture_copy_plan(&from, &to, &plan)) {
        /* Retail8 also accepts different aspect ratios. Match the largest
         * mip dimension and copy the intersecting texels; never access rows
         * outside either allocation to reproduce undefined native contents. */
        if (!from.width || !from.height || !to.width || !to.height || !from.levels
                || from.levels > 32u || !to.levels || to.levels > 32u) return D3DERR_INVALIDCALL;
        UINT size = from.width > from.height ? from.width : from.height;
        UINT target = to.width > to.height ? to.width : to.height;
        plan.source_first = 0;
        while (size > target) { size >>= 1; ++plan.source_first; }
        plan.levels = plan.source_first < from.levels ? from.levels - plan.source_first : 0;
        if (plan.levels > to.levels) plan.levels = to.levels;
    }
    D9TextureLevel *src = cube ? sc->levels : s2->levels, *dst = cube ? dc->levels : d2->levels;
    if (!src || !dst) return D3DERR_INVALIDCALL;
    UINT faces = cube ? 6u : 1u;
    /* Validate all faces/mips, then reserve all destination storage before
     * copying. A failed upload leaves 2D dirty regions intact for retry. */
    for (UINT face = 0; face < faces; ++face) for (UINT i = 0; i < plan.levels; ++i)
        if (!legacy_color_layout(&src[face * from.levels + plan.source_first + i], legacy_color_bytes(sf), TRUE)
                || !legacy_color_layout(&dst[face * to.levels + i], legacy_color_bytes(df), FALSE)) return D3DERR_INVALIDCALL;
    for (UINT face = 0; face < faces; ++face) for (UINT i = 0; i < plan.levels; ++i) {
        D9TextureLevel *level = &dst[face * to.levels + i];
        if (!level->shadow && !(level->shadow = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, level->byte_count)))
            return E_OUTOFMEMORY;
    }
    for (UINT face = 0; face < faces; ++face) for (UINT i = 0; i < plan.levels; ++i) {
        D9TextureLevel *s = &src[face * from.levels + plan.source_first + i], *d = &dst[face * to.levels + i];
        if (cube || s->dirty_full) {
            RECT full = {0, 0, (LONG)s->width, (LONG)s->height};
            if (!legacy_color_rect(destination, cube, face, i, s, d, sf, df, full)) return D3DERR_DRIVERINTERNALERROR;
        } else for (D9TextureDirtyRect *node = s->dirty_rects; node; node = node->next)
            if (!legacy_color_rect(destination, cube, face, i, s, d, sf, df, node->rect)) return D3DERR_DRIVERINTERNALERROR;
    }
    if (!cube) {
        for (UINT i = 0; i < from.levels; ++i) texture_dirty_clear(&src[i]);
        s2->dirty_pending = FALSE;
    }
    return D3D_OK;
}
#endif
