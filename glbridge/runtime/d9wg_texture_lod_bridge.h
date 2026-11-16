#ifndef D9WG_TEXTURE_LOD_BRIDGE_H
#define D9WG_TEXTURE_LOD_BRIDGE_H

/* Both SDKs expose DWORD storage. Keep the typed pointer here rather than
 * aliasing it as uint32_t (MinGW defines the two types differently). The
 * caller provides emit_command and the SDK/protocol declarations. */
static DWORD d9wg_set_texture_lod(uint32_t device_handle,
    uint32_t resource_handle, D3DPOOL pool, uint32_t level_count,
    DWORD* stored_lod, DWORD requested)
{
    DWORD previous = *stored_lod;
    D9WGSetTextureLOD command;

    /* Wine's common texture implementation ignores this call on all pools
     * except MANAGED. A valid texture always has at least one level. */
    if (pool != D3DPOOL_MANAGED || !level_count)
        return 0;
    if (requested >= level_count)
        requested = level_count - 1;
    *stored_lod = requested;
    command.device_handle = device_handle;
    command.resource_handle = resource_handle;
    command.lod = requested;
    command.reserved = 0;
    /* SetLOD has no HRESULT. Retain local state even on transport failure;
     * repeated calls still emit, allowing a later call to reach the host. */
    emit_command(D9WG_OP_SET_TEXTURE_LOD, &command, sizeof(command));
    return previous;
}

#endif
