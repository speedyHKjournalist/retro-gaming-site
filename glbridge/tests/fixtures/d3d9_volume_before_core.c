static BOOL emit_volume_texture_create(D9Device *device,
        D9VolumeTexture *texture)
{
    D9WGCreateTextureVolume command;
    ZeroMemory(&command, sizeof(command));
    command.device_handle = device->handle;
    command.resource_handle = texture->handle;
    command.width = texture->width;
    command.height = texture->height;
    command.depth = texture->depth;
    command.level_count = texture->level_count;
    command.format = texture->format;
    command.usage = texture->usage;
    command.pool = texture->pool;
    return emit_command(D9WG_OP_CREATE_TEXTURE_VOLUME, &command,
            sizeof(command));
}

static HRESULT WINAPI device_create_volume_texture(IDirect3DDevice9 *iface,
        UINT width, UINT height, UINT depth, UINT levels, DWORD usage,
        D3DFORMAT format, D3DPOOL pool, IDirect3DVolumeTexture9 **out,
        HANDLE *shared)
{
    D9Device *device = device_from_iface(iface);
    D9VolumeTexture *texture;
    UINT full_levels, level, level_width, level_height, level_depth;
    HRESULT failure = E_OUTOFMEMORY;
    HRESULT success = D3D_OK;
    (void)shared;
    TRACE("CALL CreateVolumeTexture %lux%lux%lu levels=%lu usage=%08lX "
            "format=%08lX pool=%lu", width, height, depth, levels, usage,
            (DWORD)format, (DWORD)pool);
    if (!out)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *out = NULL;
    /* Unlike the 2D/cube NOAUTOGEN fallback, volume autogen is invalid. */
    if (usage & D3DUSAGE_AUTOGENMIPMAP)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (!width || !height || !depth || width > 2048 || height > 2048
            || depth > 2048
            || !texture_create_supported(D3DRTYPE_VOLUMETEXTURE, usage, format,
                    pool)) {
        HOSTLOG_FAILED("CreateVolumeTexture refused: %lux%lux%lu levels=%lu "
                "usage=%08lX format=%08lX pool=%lu", width, height, depth,
                levels, usage, (DWORD)format, (DWORD)pool);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    full_levels = full_volume_mip_level_count(width, height, depth);
    if (!levels) levels = full_levels;
    if (levels > full_levels)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    texture = (D9VolumeTexture *)HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY, sizeof(*texture));
    if (!texture)
        return TRACE_REFUSE(E_OUTOFMEMORY);
    texture->levels = (D9TextureLevel *)HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY, levels * sizeof(*texture->levels));
    if (!texture->levels) {
        HeapFree(GetProcessHeap(), 0, texture);
        return TRACE_REFUSE(E_OUTOFMEMORY);
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
        D9TextureLevel *level_data = &texture->levels[level];
        UINT slice_bytes;
        level_data->width = level_width;
        level_data->height = level_height;
        level_data->depth = level_depth;
        if (!texture_level_layout(format, level_width, level_height,
                &level_data->row_pitch, &level_data->row_count,
                &slice_bytes)
                || !multiply_u32(slice_bytes, level_depth,
                    &level_data->byte_count))
            goto allocation_failed;
        level_data->slice_pitch = slice_bytes;
        level_data->shadow = (BYTE *)HeapAlloc(GetProcessHeap(),
                HEAP_ZERO_MEMORY, level_data->byte_count);
        level_data->level_volume = (D9Volume *)HeapAlloc(GetProcessHeap(),
                HEAP_ZERO_MEMORY, sizeof(*level_data->level_volume));
        if (!level_data->shadow || !level_data->level_volume)
            goto allocation_failed;
        level_data->level_volume->iface.lpVtbl = &g_volume_vtbl;
        level_data->level_volume->texture = texture;
        level_data->level_volume->level = level;
        if (level_width > 1) level_width >>= 1;
        if (level_height > 1) level_height >>= 1;
        if (level_depth > 1) level_depth >>= 1;
    }
    if (!emit_volume_texture_create(device, texture)) {
        failure = D3DERR_DRIVERINTERNALERROR;
        goto allocation_failed;
    }
    texture->next_device_resource = device->volume_textures;
    device->volume_textures = texture;
    *out = &texture->iface;
    return success;

allocation_failed:
    for (level = 0; level < levels; ++level) {
        HeapFree(GetProcessHeap(), 0, texture->levels[level].level_volume);
        HeapFree(GetProcessHeap(), 0, texture->levels[level].shadow);
    }
    device_child_release(device);
    HeapFree(GetProcessHeap(), 0, texture->levels);
    HeapFree(GetProcessHeap(), 0, texture);
    return TRACE_REFUSE(failure);
}
