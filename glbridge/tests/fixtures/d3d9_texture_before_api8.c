static BOOL emit_texture_create(D9Device *device, D9Texture *texture)
{
    D9WGCreateTexture2D command;
    ZeroMemory(&command, sizeof(command));
    command.device_handle = device->handle;
    command.resource_handle = texture->handle;
    command.width = texture->width;
    command.height = texture->height;
    command.level_count = texture->level_count;
    command.format = texture->format;
    command.usage = texture->usage;
    command.pool = texture->pool;
    command.multisample_type = texture->multisample;
    command.multisample_quality = texture->multisample_quality;
    if (!emit_command(D9WG_OP_CREATE_TEXTURE_2D, &command, sizeof(command))) return FALSE;
    return !(texture->usage & D3DUSAGE_AUTOGENMIPMAP)
        || emit_generate_mips(device, texture->handle, texture->autogen_filter, TRUE);
}

static HRESULT WINAPI device_create_texture(IDirect3DDevice9 *iface,
        UINT width, UINT height, UINT levels, DWORD usage, D3DFORMAT format,
        D3DPOOL pool, IDirect3DTexture9 **texture_out, HANDLE *shared_handle)
{
    D9Device *device = device_from_iface(iface);
    D9Texture *texture;
    UINT full_levels;
    UINT level;
    UINT level_width;
    UINT level_height;
    HRESULT failure = E_OUTOFMEMORY;
    HRESULT success = D3D_OK;
    BOOL is_depth = (usage & D3DUSAGE_DEPTHSTENCIL) != 0;
    (void)shared_handle;

    TRACE("CALL CreateTexture %lux%lu levels=%lu usage=%08lX "
            "format=%08lX pool=%lu", width, height, levels, usage,
            (DWORD)format, (DWORD)pool);

    if (!texture_out) {
        TRACE("FAIL CreateTexture missing output -> %08lX",
                (DWORD)D3DERR_INVALIDCALL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    *texture_out = NULL;
    /*
     * D3DUSAGE_AUTOGENMIPMAP: the sublevels exist but only level 0 is the
     * app's. D3D9 reports GetLevelCount() == 1 for such a texture and hands out
     * no surface below the top, because the driver owns the rest -- so one
     * visible level here is not a simplification, it is the documented shape.
     *
     * The flag stays on `usage` rather than being stripped: it is what tells
     * the host to allocate the full chain behind that single visible level and
     * to refill it whenever level 0 changes. It is masked out only for the
     * capability predicate, which answers which resources can exist and has no
     * opinion on how their mips get filled.
     */
    if (!d9wg_autogen_creation_valid(levels, usage, pool))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    if (usage & D3DUSAGE_AUTOGENMIPMAP)
        levels = 1;
    if (!width || !height || width > 8192 || height > 8192
            || !texture_create_supported(D3DRTYPE_TEXTURE,
                    usage & ~(DWORD)D3DUSAGE_AUTOGENMIPMAP, format, pool)) {

        TRACE("FAIL CreateTexture invalid arguments -> %08lX",
                (DWORD)D3DERR_INVALIDCALL);
        HOSTLOG_FAILED("CreateTexture refused: %lux%lu levels=%lu usage=%08lX "
                "format=%08lX pool=%lu", width, height, levels, usage,
                (DWORD)format, (DWORD)pool);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    full_levels = full_mip_level_count(width, height);
    if (!levels) levels = full_levels;
    if (levels > full_levels) {
        TRACE("FAIL CreateTexture levels=%lu exceeds full_levels=%lu -> %08lX",
                levels, full_levels, (DWORD)D3DERR_INVALIDCALL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }

    texture = (D9Texture *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*texture));
    if (!texture) {
        TRACE("FAIL CreateTexture object allocation -> %08lX",
                (DWORD)E_OUTOFMEMORY);
        return TRACE_REFUSE(E_OUTOFMEMORY);
    }
    texture->levels = (D9TextureLevel *)HeapAlloc(GetProcessHeap(),
            HEAP_ZERO_MEMORY, levels * sizeof(*texture->levels));
    if (!texture->levels) {
        HeapFree(GetProcessHeap(), 0, texture);
        TRACE("FAIL CreateTexture level allocation levels=%lu -> %08lX",
                levels, (DWORD)E_OUTOFMEMORY);
        return TRACE_REFUSE(E_OUTOFMEMORY);
    }
    if (usage & D3DUSAGE_AUTOGENMIPMAP)
        texture->autogen_filter = (D3DTEXTUREFILTERTYPE)d9wg_autogen_default_filter(format,
                (d9wg_format_query_info(format) & 4u) ? runtime_host_features() : 0);
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
    device_child_add_ref(device);

    level_width = width;
    level_height = height;
    for (level = 0; level < levels; ++level) {
        D9TextureLevel *level_data = &texture->levels[level];
        level_data->width = level_width;
        level_data->height = level_height;
        if (!is_depth && !texture_level_layout(format, level_width, level_height,
                &level_data->row_pitch, &level_data->row_count,
                &level_data->byte_count))
            goto allocation_failed;
        /* Targets are not directly lockable.  A render target can acquire a
         * lazy CPU mirror later when Clear/ColorFill gives us exact contents;
         * texture_lock_level() therefore rejects the usage flags explicitly. */
        if (usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL)) {
            if (level_width > 1) level_width >>= 1;
            if (level_height > 1) level_height >>= 1;
            continue;
        }
        level_data->shadow = (BYTE *)HeapAlloc(GetProcessHeap(),
                HEAP_ZERO_MEMORY, level_data->byte_count);
        if (!level_data->shadow)
            goto allocation_failed;
        level_data->shadow_valid = TRUE;
        if (level_width > 1) level_width >>= 1;
        if (level_height > 1) level_height >>= 1;
    }

    if (!emit_texture_create(device, texture)) {
        failure = D3DERR_DRIVERINTERNALERROR;
        goto allocation_failed;
    }
    texture->next_device_resource = device->texture_resources;
    device->texture_resources = texture;
    *texture_out = &texture->iface;
    TRACE_REGISTER_RANGE("TEXTURE_OBJECT", texture->handle, texture, texture,
            sizeof(*texture));
    TRACE_REGISTER_RANGE("TEXTURE_SHADOW0", texture->handle, texture,
            texture->levels[0].shadow, texture->levels[0].byte_count);
    TRACE("OK CreateTexture handle=%08lX object=%08lX %lux%lu levels=%lu "
            "usage=%08lX format=%08lX pool=%lu shadow0=%08lX "
            "shadow0_end=%08lX shadow0_bytes=%lu", texture->handle,
            (DWORD)(uintptr_t)*texture_out, width, height, levels, usage,
            (DWORD)format, (DWORD)pool,
            (DWORD)(uintptr_t)texture->levels[0].shadow,
            (DWORD)(uintptr_t)(texture->levels[0].shadow
                    ? texture->levels[0].shadow
                            + texture->levels[0].byte_count
                    : NULL),
            texture->levels[0].byte_count);
    return success;

allocation_failed:
    for (level = 0; level < levels; ++level) {
        if (texture->levels[level].shadow)
            HeapFree(GetProcessHeap(), 0, texture->levels[level].shadow);
    }
    device_child_release(device);
    HeapFree(GetProcessHeap(), 0, texture->levels);
    HeapFree(GetProcessHeap(), 0, texture);
    TRACE("FAIL CreateTexture %lux%lu levels=%lu usage=%08lX "
            "format=%08lX pool=%lu -> %08lX", width, height, levels, usage,
            (DWORD)format, (DWORD)pool, (DWORD)failure);
    HOSTLOG_FAILED("CreateTexture failed: %lux%lu levels=%lu usage=%08lX "
            "format=%08lX pool=%lu hr=%08lX", width, height, levels, usage,
            (DWORD)format, (DWORD)pool, (DWORD)failure);
    return failure;
}
