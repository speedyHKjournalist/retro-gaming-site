/* Actual2a43549 API8 UpdateTexture functions before native pool validation. */
static HRESULT update_cube_texture(D8Device *device,
        IDirect3DCubeTexture8 *source_iface, IDirect3DCubeTexture8 *destination_iface)
{
    D8CubeTexture *source = (D8CubeTexture *)source_iface;
    D8CubeTexture *destination = (D8CubeTexture *)destination_iface;
    if (source->iface.lpVtbl != &g_cube_texture_vtbl || destination->iface.lpVtbl != &g_cube_texture_vtbl ||
            source->device != device || destination->device != device || source->format != destination->format)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    return D8WG_TRACE_ERROR(d9wg_update_texture_common(destination, D9WG_COPY_CUBE,
            source->levels, (D9WGTextureCopyExtent){source->edge_length, source->edge_length, 1, source->level_count},
            destination->levels, (D9WGTextureCopyExtent){destination->edge_length, destination->edge_length, 1, destination->level_count}));
}

static HRESULT update_volume_texture(D8Device *device,
        IDirect3DVolumeTexture8 *source_iface, IDirect3DVolumeTexture8 *destination_iface)
{
    D8VolumeTexture *source = (D8VolumeTexture *)source_iface;
    D8VolumeTexture *destination = (D8VolumeTexture *)destination_iface;
    if (source->iface.lpVtbl != &g_volume_texture_vtbl || destination->iface.lpVtbl != &g_volume_texture_vtbl ||
            source->device != device || destination->device != device || source->format != destination->format)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    return D8WG_TRACE_ERROR(d9wg_update_texture_common(destination, D9WG_COPY_VOLUME,
            source->levels, (D9WGTextureCopyExtent){source->width, source->height, source->depth, source->level_count},
            destination->levels, (D9WGTextureCopyExtent){destination->width, destination->height, destination->depth, destination->level_count}));
}

static HRESULT WINAPI device_update_texture(IDirect3DDevice8 *iface,
        IDirect3DBaseTexture8 *source_iface,
        IDirect3DBaseTexture8 *destination_iface)
{
    D8Device *device = device_from_iface(iface);
    D8Texture *source;
    D8Texture *destination;

    if (!source_iface || !destination_iface)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    /* Both operands must be the same kind of base texture. The cube and
     * volume cases are handled first because the 2D path below reads fields
     * that only a D8Texture has. */
    if (source_iface->lpVtbl
            == (IDirect3DBaseTexture8Vtbl *)&g_cube_texture_vtbl
            || destination_iface->lpVtbl
                == (IDirect3DBaseTexture8Vtbl *)&g_cube_texture_vtbl)
        return update_cube_texture(device,
                (IDirect3DCubeTexture8 *)source_iface,
                (IDirect3DCubeTexture8 *)destination_iface);
    if (source_iface->lpVtbl
            == (IDirect3DBaseTexture8Vtbl *)&g_volume_texture_vtbl
            || destination_iface->lpVtbl
                == (IDirect3DBaseTexture8Vtbl *)&g_volume_texture_vtbl)
        return update_volume_texture(device,
                (IDirect3DVolumeTexture8 *)source_iface,
                (IDirect3DVolumeTexture8 *)destination_iface);
    source = (D8Texture *)source_iface;
    destination = (D8Texture *)destination_iface;
    if (source->iface.lpVtbl != &g_texture_vtbl
            || destination->iface.lpVtbl != &g_texture_vtbl
            || source->device != device || destination->device != device
            || source->format != destination->format)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    return D8WG_TRACE_ERROR(d9wg_update_texture_common(destination, D9WG_COPY_2D,
            source->levels, (D9WGTextureCopyExtent){source->width, source->height, 1, source->level_count},
            destination->levels, (D9WGTextureCopyExtent){destination->width, destination->height, 1, destination->level_count}));
}
