/* Actual2a43549 ordinary9 UpdateTexture before shared pool predicate. */
static HRESULT WINAPI device_update_texture(IDirect3DDevice9 *iface,
        IDirect3DBaseTexture9 *src_iface, IDirect3DBaseTexture9 *dst_iface)
{
    D9Device *device = device_from_iface(iface);
    D9Texture *source = (D9Texture *)src_iface, *destination = (D9Texture *)dst_iface;
    if (!source || !destination || source->iface.lpVtbl != destination->iface.lpVtbl)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (source->iface.lpVtbl == (const void *)&g_volume_texture_vtbl) {
        D9VolumeTexture *from = (D9VolumeTexture *)src_iface, *to = (D9VolumeTexture *)dst_iface;
        if (from->device != device || to->device != device || from->pool != D3DPOOL_SYSTEMMEM ||
                to->pool != D3DPOOL_DEFAULT || from->format != to->format)
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        return TRACE_REFUSE(d9wg_update_texture_common(to, D9WG_COPY_VOLUME,
                from->levels, (D9WGTextureCopyExtent){from->width, from->height, from->depth, from->level_count},
                to->levels, (D9WGTextureCopyExtent){to->width, to->height, to->depth, to->level_count}));
    }
    if (source->iface.lpVtbl == (const void *)&g_cube_vtbl) {
        D9CubeTexture *from = (D9CubeTexture *)src_iface, *to = (D9CubeTexture *)dst_iface;
        if (from->device != device || to->device != device || from->pool != D3DPOOL_SYSTEMMEM ||
                to->pool != D3DPOOL_DEFAULT || from->format != to->format)
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
        return TRACE_REFUSE(d9wg_update_texture_common(to, D9WG_COPY_CUBE,
                from->levels, (D9WGTextureCopyExtent){from->edge_length, from->edge_length, 1, from->level_count},
                to->levels, (D9WGTextureCopyExtent){to->edge_length, to->edge_length, 1, to->level_count}));
    }
    if (source->iface.lpVtbl != &g_texture_vtbl || source->device != device || destination->device != device ||
            source->pool != D3DPOOL_SYSTEMMEM || destination->pool != D3DPOOL_DEFAULT || source->format != destination->format)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    return TRACE_REFUSE(d9wg_update_texture_common(destination, D9WG_COPY_2D,
            source->levels, (D9WGTextureCopyExtent){source->width, source->height, 1, source->level_count},
            destination->levels, (D9WGTextureCopyExtent){destination->width, destination->height, 1, destination->level_count}));
}
