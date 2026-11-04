/* Actual SDK8 shader creation,sitefcac34e. */
static HRESULT WINAPI device_create_pixel_shader(IDirect3DDevice8 *iface,
        const DWORD *function, DWORD *shader)
{
    D8Device *device = device_from_iface(iface);
    D8Shader *entry;
    UINT body_count = 0;
    UINT major;
    UINT minor;
    DWORD version;

    if (shader) *shader = 0;
    if (!function || !shader)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    version = function[0];
    major = (UINT)D3DSHADER_VERSION_MAJOR(version);
    minor = (UINT)D3DSHADER_VERSION_MINOR(version);
    /* ps_1_0 for the same reason vs_1_0 is accepted above: a ps_1_4 device
     * runs every earlier 1.x shader, and shader_opcode_supported() already
     * gates `cmp` (1.2+), `cnd` (up to 1.3) and the 1.4 forms on this minor,
     * so 1.0 validates as the subset it is. */
    if ((version & 0xFFFF0000u) != 0xFFFF0000u || major != 1u || minor > 4u) {
        D8WG_TRACE("SHADER REFUSE kind=pixel stage=version version=%08lX",
                version);
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    }
    if (!validate_shader_body(function + 1, D8WG_MAX_SHADER_TOKENS, TRUE,
            minor, &body_count)) {
        D8WG_TRACE("SHADER REFUSE kind=pixel stage=body version=%08lX",
                version);
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    }

    entry = (D8Shader *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*entry));
    if (!entry) return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    entry->code_tokens = (DWORD *)HeapAlloc(GetProcessHeap(), 0,
            (body_count + 1u) * sizeof(DWORD));
    if (!entry->code_tokens) {
        HeapFree(GetProcessHeap(), 0, entry);
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    }
    entry->code_tokens[0] = version;
    if (body_count)
        CopyMemory(entry->code_tokens + 1, function + 1,
                body_count * sizeof(DWORD));
    entry->code_token_count = body_count + 1u;
    entry->is_pixel_shader = TRUE;
    entry->major_version = major;
    entry->minor_version = minor;
    shader_bytecode_hash(entry->code_tokens, entry->code_token_count,
            &entry->hash_low, &entry->hash_high);
    entry->handle = allocate_shader_handle();

    if (!emit_create_pixel_shader(device, entry)) {
        HeapFree(GetProcessHeap(), 0, entry->code_tokens);
        HeapFree(GetProcessHeap(), 0, entry);
        return D8WG_TRACE_ERROR(D3DERR_DRIVERINTERNALERROR);
    }
    D8WG_TRACE("SHADER CREATE kind=pixel version=%08lX tokens=%u "
            "handle=%08lX hash=%08lX%08lX", version,
            entry->code_token_count, entry->handle,
            entry->hash_high, entry->hash_low);
    entry->device = device;
    entry->next = device->pixel_shaders;
    device->pixel_shaders = entry;
    *shader = entry->handle;
    return D3D_OK;
}
