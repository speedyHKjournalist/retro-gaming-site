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

static BOOL emit_cube_texture_create(D8Device *device, D8CubeTexture *texture)
{
    D9WGCreateTextureCube command;
    command.device_handle = device->handle;
    command.resource_handle = texture->handle;
    command.edge_length = texture->edge_length;
    command.level_count = texture->level_count;
    command.format = texture->format;
    command.usage = texture->usage;
    command.pool = texture->pool;
    command.reserved = 0;
    return emit_command(D9WG_OP_CREATE_TEXTURE_CUBE, &command,
            sizeof(command));
}

static HRESULT WINAPI device_create_cube_texture(IDirect3DDevice8 *iface,
        UINT edge_length, UINT levels, DWORD usage, D3DFORMAT format,
        D3DPOOL pool, IDirect3DCubeTexture8 **texture_out)
{
    D8Device *device = device_from_iface(iface);
    D8CubeTexture *texture;
    UINT full_levels;
    UINT level;
    UINT face;
    UINT level_edge;
    HRESULT failure = E_OUTOFMEMORY;

    if (!texture_out) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *texture_out = NULL;
    if (!edge_length || edge_length > 4096
            || !supported_texture_format(format)
            || (usage & D3DUSAGE_DEPTHSTENCIL)
            || ((usage & D3DUSAGE_RENDERTARGET)
                && (pool != D3DPOOL_DEFAULT
                    || !supported_render_target_format(format)))
            || pool > D3DPOOL_SCRATCH)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    full_levels = full_mip_level_count(edge_length, edge_length);
    if (!levels) levels = full_levels;
    if (levels > full_levels) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);

    texture = (D8CubeTexture *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*texture));
    if (!texture) return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    texture->levels = (D8TextureLevel *)HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY, levels * 6u * sizeof(*texture->levels));
    if (!texture->levels) {
        HeapFree(GetProcessHeap(), 0, texture);
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    }
    texture->iface.lpVtbl = &g_cube_texture_vtbl;
    texture->refcount = 1;
    texture->device = device;
    texture->handle = allocate_handle();
    texture->edge_length = edge_length;
    texture->level_count = levels;
    texture->usage = usage;
    texture->format = format;
    texture->pool = pool;
    device_child_add_ref(device);

    for (face = 0; face < 6u; ++face) {
        level_edge = edge_length;
        for (level = 0; level < levels; ++level) {
            if (!allocate_texture_level(
                    &texture->levels[face * levels + level], format,
                    level_edge, level_edge, 1u))
                goto allocation_failed;
            if (level_edge > 1) level_edge >>= 1;
        }
    }
    if (!emit_cube_texture_create(device, texture)) {
        failure = D3DERR_DRIVERINTERNALERROR;
        goto allocation_failed;
    }
    texture->next_device_resource = device->cube_resources;
    device->cube_resources = texture;
    *texture_out = &texture->iface;
    return D3D_OK;

allocation_failed:
    for (level = 0; level < levels * 6u; ++level)
        HeapFree(GetProcessHeap(), 0, texture->levels[level].shadow);
    device_child_release(device);
    HeapFree(GetProcessHeap(), 0, texture->levels);
    HeapFree(GetProcessHeap(), 0, texture);
    return failure;
}
