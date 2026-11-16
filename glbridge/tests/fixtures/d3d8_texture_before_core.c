static BOOL emit_texture_create(D8Device *device, D8Texture *texture)
{
    D9WGCreateTexture2D command;
    command.device_handle = device->handle;
    command.resource_handle = texture->handle;
    command.width = texture->width;
    command.height = texture->height;
    command.level_count = texture->level_count;
    command.format = texture->format;
    command.usage = texture->usage;
    command.pool = texture->pool;
    /* CreateRenderTarget is the only entry point that can set this; see
     * supported_multisample_type() for which values reach here. Quality stays
     * 0: WebGPU has no quality levels and the host refuses a non-zero one. */
    command.multisample_type = texture->multisample_type;
    command.multisample_quality = 0;
    return emit_command(D9WG_OP_CREATE_TEXTURE_2D, &command, sizeof(command));
}

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

static HRESULT create_texture_internal(D8Device* device,
    UINT width, UINT height, UINT levels, DWORD usage,
    D3DFORMAT format, D3DPOOL pool, D3DMULTISAMPLE_TYPE ms,
    IDirect3DTexture8** texture_out)
{
    D8Texture* texture;
    UINT full_levels;
    UINT level;
    UINT level_width;
    UINT level_height;
    HRESULT failure = E_OUTOFMEMORY;

    if (!texture_out)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *texture_out = NULL;
    if (!width || !height || width > 4096 || height > 4096
        || !supported_texture_format(format)
        || !supported_multisample_type(ms)
        || (ms != D3DMULTISAMPLE_NONE && !(usage & D3DUSAGE_RENDERTARGET))
        || (usage & D3DUSAGE_DEPTHSTENCIL)
        || (usage & D3DUSAGE_RENDERTARGET
            && (pool != D3DPOOL_DEFAULT || levels != 1
                || !supported_render_target_format(format)))
        || pool > D3DPOOL_SCRATCH)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    full_levels = full_mip_level_count(width, height);
    if (!levels)
        levels = full_levels;
    if (levels > full_levels)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);

    texture = (D8Texture*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
        sizeof(*texture));
    if (!texture)
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    texture->levels = (D8TextureLevel*)HeapAlloc(GetProcessHeap(),
        HEAP_ZERO_MEMORY, levels * sizeof(*texture->levels));
    if (!texture->levels) {
        HeapFree(GetProcessHeap(), 0, texture);
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    }
    texture->iface.lpVtbl = &g_texture_vtbl;
    texture->refcount = 1;
    texture->device = device;
    texture->handle = allocate_handle();
    texture->width = width;
    texture->height = height;
    texture->level_count = levels;
    texture->usage = usage;
    texture->format = format;
    texture->pool = pool;
    texture->multisample_type = ms;
    device_child_add_ref(device);

    level_width = width;
    level_height = height;
    for (level = 0; level < levels; ++level) {
        D8TextureLevel* level_data = &texture->levels[level];
        if (!allocate_texture_level(level_data, format, level_width,
                level_height, 1u))
            goto allocation_failed;
        if (level_width > 1)
            level_width >>= 1;
        if (level_height > 1)
            level_height >>= 1;
    }

    if (!emit_texture_create(device, texture)) {
        failure = D3DERR_DRIVERINTERNALERROR;
        goto allocation_failed;
    }
    texture->next_device_resource = device->texture_resources;
    device->texture_resources = texture;
    *texture_out = &texture->iface;
    return D3D_OK;

allocation_failed:
    for (level = 0; level < levels; ++level) {
        if (texture->levels[level].shadow)
            HeapFree(GetProcessHeap(), 0, texture->levels[level].shadow);
    }
    device_child_release(device);
    HeapFree(GetProcessHeap(), 0, texture->levels);
    HeapFree(GetProcessHeap(), 0, texture);
    return failure;
}

static HRESULT WINAPI device_create_texture(IDirect3DDevice8* iface,
    UINT width, UINT height, UINT levels, DWORD usage,
    D3DFORMAT format, D3DPOOL pool, IDirect3DTexture8** texture_out)
{
    return D8WG_TRACE_ERROR(create_texture_internal(device_from_iface(iface),
        width, height, levels, usage, format, pool,
        D3DMULTISAMPLE_NONE, texture_out));
}
