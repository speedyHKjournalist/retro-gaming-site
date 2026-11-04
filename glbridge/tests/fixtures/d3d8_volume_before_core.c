static BOOL allocate_texture_level(D8TextureLevel *level_data,
        D3DFORMAT format, UINT width, UINT height, UINT depth)
{
    level_data->width = width;
    level_data->height = height;
    level_data->depth = depth;
    if (!texture_level_layout(format, width, height, &level_data->row_pitch,
            &level_data->row_count, &level_data->slice_pitch))
        return FALSE;
    if (!multiply_u32(level_data->slice_pitch, depth, &level_data->byte_count))
        return FALSE;
    level_data->shadow = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            level_data->byte_count);
    return level_data->shadow != NULL;
}

static BOOL emit_volume_texture_create(D8Device *device,
        D8VolumeTexture *texture)
{
    D9WGCreateTextureVolume command;
    command.device_handle = device->handle;
    command.resource_handle = texture->handle;
    command.width = texture->width;
    command.height = texture->height;
    command.depth = texture->depth;
    command.level_count = texture->level_count;
    command.format = texture->format;
    command.usage = texture->usage;
    command.pool = texture->pool;
    command.reserved = 0;
    return emit_command(D9WG_OP_CREATE_TEXTURE_VOLUME, &command,
            sizeof(command));
}

static HRESULT WINAPI device_create_volume_texture(IDirect3DDevice8 *iface,
        UINT width, UINT height, UINT depth, UINT levels, DWORD usage,
        D3DFORMAT format, D3DPOOL pool, IDirect3DVolumeTexture8 **texture_out)
{
    D8Device *device = device_from_iface(iface);
    D8VolumeTexture *texture;
    UINT full_levels;
    UINT level;
    UINT level_width;
    UINT level_height;
    UINT level_depth;
    HRESULT failure = E_OUTOFMEMORY;

    if (!texture_out) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *texture_out = NULL;
    if (!width || !height || !depth || width > 2048 || height > 2048
            || depth > 2048
            || !supported_texture_format(format)
            /* A volume is never a render target or a depth buffer, and the
             * block-compressed formats have no 3D layout this path lays out. */
            || (usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL))
            || format == D3DFMT_DXT1 || format == D3DFMT_DXT2
            || format == D3DFMT_DXT3 || format == D3DFMT_DXT4
            || format == D3DFMT_DXT5
            || pool > D3DPOOL_SCRATCH)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    full_levels = full_mip_level_count(width,
            height > depth ? height : depth);
    if (!levels) levels = full_levels;
    if (levels > full_levels) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);

    texture = (D8VolumeTexture *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*texture));
    if (!texture) return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    texture->levels = (D8TextureLevel *)HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY, levels * sizeof(*texture->levels));
    if (!texture->levels) {
        HeapFree(GetProcessHeap(), 0, texture);
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    }
    texture->iface.lpVtbl = &g_volume_texture_vtbl;
    texture->refcount = 1;
    texture->device = device;
    texture->handle = allocate_handle();
    texture->width = width;
    texture->height = height;
    texture->depth = depth;
    texture->level_count = levels;
    texture->usage = usage;
    texture->format = format;
    texture->pool = pool;
    device_child_add_ref(device);

    level_width = width;
    level_height = height;
    level_depth = depth;
    for (level = 0; level < levels; ++level) {
        if (!allocate_texture_level(&texture->levels[level], format,
                level_width, level_height, level_depth))
            goto allocation_failed;
        if (level_width > 1) level_width >>= 1;
        if (level_height > 1) level_height >>= 1;
        if (level_depth > 1) level_depth >>= 1;
    }
    if (!emit_volume_texture_create(device, texture)) {
        failure = D3DERR_DRIVERINTERNALERROR;
        goto allocation_failed;
    }
    texture->next_device_resource = device->volume_resources;
    device->volume_resources = texture;
    *texture_out = &texture->iface;
    return D3D_OK;

allocation_failed:
    for (level = 0; level < levels; ++level)
        HeapFree(GetProcessHeap(), 0, texture->levels[level].shadow);
    device_child_release(device);
    HeapFree(GetProcessHeap(), 0, texture->levels);
    HeapFree(GetProcessHeap(), 0, texture);
    return failure;
}
