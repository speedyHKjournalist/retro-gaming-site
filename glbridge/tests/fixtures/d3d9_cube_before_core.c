static BOOL emit_cube_texture_create(D9Device *device, D9CubeTexture *texture)
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
    if (!emit_command(D9WG_OP_CREATE_TEXTURE_CUBE, &command, sizeof(command))) return FALSE;
    return !(texture->usage & D3DUSAGE_AUTOGENMIPMAP)
        || emit_generate_mips(device, texture->handle, texture->autogen_filter, TRUE);
}

static HRESULT WINAPI device_create_cube_texture(IDirect3DDevice9 *iface,
        UINT edge, UINT levels, DWORD usage, D3DFORMAT format, D3DPOOL pool,
        IDirect3DCubeTexture9 **texture_out, HANDLE *shared)
{
    D9Device *device = device_from_iface(iface);
    D9CubeTexture *texture;
    UINT full_levels;
    UINT face;
    UINT level;
    UINT total;
    HRESULT failure = E_OUTOFMEMORY;
    HRESULT success = D3D_OK;
    (void)shared;

    /* This path used to refuse without recording a single argument, which is
     * how a 3DMark06 crash spent a whole trace being unattributable: the log
     * said only that a cube map had been rejected, never which of four
     * conditions rejected it. A refusal that cannot be explained cannot be
     * fixed, so the parameters are recorded before anything can reject them. */
    TRACE("CALL CreateCubeTexture edge=%lu levels=%lu usage=%08lX "
            "format=%08lX pool=%lu", edge, levels, usage, (DWORD)format,
            (DWORD)pool);

    if (!texture_out) {
        TRACE("FAIL CreateCubeTexture missing output -> %08lX",
                (DWORD)D3DERR_INVALIDCALL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    *texture_out = NULL;
    /* See device_create_texture: one visible level, a full chain behind it. */
    if (!d9wg_autogen_creation_valid(levels, usage, pool))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (usage & D3DUSAGE_AUTOGENMIPMAP)
        levels = 1;
    if (!edge || edge > 8192
            || !texture_create_supported(D3DRTYPE_CUBETEXTURE,
                    usage & ~(DWORD)D3DUSAGE_AUTOGENMIPMAP, format, pool)) {

        TRACE("FAIL CreateCubeTexture invalid arguments -> %08lX",
                (DWORD)D3DERR_INVALIDCALL);
        HOSTLOG_FAILED("CreateCubeTexture refused: edge=%lu levels=%lu "
                "usage=%08lX format=%08lX pool=%lu", edge, levels, usage,
                (DWORD)format, (DWORD)pool);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    full_levels = full_mip_level_count(edge, edge);
    if (!levels) levels = full_levels;
    if (levels > full_levels) {
        TRACE("FAIL CreateCubeTexture levels=%lu exceeds full_levels=%lu "
                "-> %08lX", levels, full_levels, (DWORD)D3DERR_INVALIDCALL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    if (!multiply_u32(levels, 6u, &total))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);

    texture = (D9CubeTexture *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*texture));
    if (!texture)
        return TRACE_REFUSE(E_OUTOFMEMORY);
    texture->levels = (D9TextureLevel *)HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY, total * sizeof(*texture->levels));
    if (!texture->levels) {
        HeapFree(GetProcessHeap(), 0, texture);
        return TRACE_REFUSE(E_OUTOFMEMORY);
    }
    if (usage & D3DUSAGE_AUTOGENMIPMAP)
        texture->autogen_filter = (D3DTEXTUREFILTERTYPE)d9wg_autogen_default_filter(format,
                (d9wg_format_query_info(format) & 4u) ? runtime_host_features() : 0);
    texture->iface.lpVtbl = &g_cube_vtbl;
    texture->refcount = 1;
    texture->device = device;
    texture->handle = allocate_handle();
    texture->edge_length = edge;
    texture->level_count = levels;
    texture->usage = usage;
    texture->format = format;
    texture->pool = pool;
    device_child_add_ref(device);

    for (face = 0; face < 6; ++face) {
        UINT size = edge;
        for (level = 0; level < levels; ++level) {
            D9TextureLevel *level_data = &texture->levels[face * levels + level];
            level_data->width = size;
            level_data->height = size;
            if (!texture_level_layout(format, size, size,
                    &level_data->row_pitch, &level_data->row_count,
                    &level_data->byte_count))
                goto allocation_failed;
            level_data->shadow = (BYTE *)HeapAlloc(GetProcessHeap(),
                    HEAP_ZERO_MEMORY, level_data->byte_count);
            if (!level_data->shadow)
                goto allocation_failed;
            if (size > 1) size >>= 1;
        }
    }
    if (!emit_cube_texture_create(device, texture)) {
        failure = D3DERR_DRIVERINTERNALERROR;
        goto allocation_failed;
    }
    texture->next_device_resource = device->cube_textures;
    device->cube_textures = texture;
    *texture_out = &texture->iface;
    TRACE("OK CreateCubeTexture handle=%08lX object=%08lX edge=%lu levels=%lu "
            "usage=%08lX format=%08lX pool=%lu -> %08lX", texture->handle,
            (DWORD)(uintptr_t)*texture_out, edge, levels, usage, (DWORD)format,
            (DWORD)pool, (DWORD)success);
    return success;

allocation_failed:
    for (face = 0; face < total; ++face) {
        if (texture->levels[face].shadow)
            HeapFree(GetProcessHeap(), 0, texture->levels[face].shadow);
    }
    device_child_release(device);
    HeapFree(GetProcessHeap(), 0, texture->levels);
    HeapFree(GetProcessHeap(), 0, texture);
    return failure;
}
