/* Actual ordinary9 shader creation,sitefcac34e. */
static HRESULT create_shader(D9Device *device, const DWORD *bytecode,
        BOOL want_pixel, D9Shader **shader_out)
{
    D9Shader *shader;
    UINT token_count = 0;
    BOOL is_pixel = FALSE;
    uint32_t code_bytes;

    *shader_out = NULL;
    if (!bytecode) {
        TRACE("FAIL CreateShader want_pixel=%lu bytecode=NULL -> %08lX",
                (DWORD)want_pixel, (DWORD)D3DERR_INVALIDCALL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    if (!shader_token_count(bytecode, &token_count, &is_pixel)
            || is_pixel != want_pixel) {
        /* Rejected before anything is emitted, so without this line the call
         * leaves no record whatsoever: no CMD, no OK, no FAIL. An app whose
         * shader is turned down here sees only D3DERR_INVALIDCALL, and the
         * trace shows only the gap where the shader should have been. */
        TRACE("FAIL CreateShader version=%08lX want_pixel=%lu is_pixel=%lu "
                "tokens=%lu -> %08lX", bytecode[0], (DWORD)want_pixel,
                (DWORD)is_pixel, token_count, (DWORD)D3DERR_INVALIDCALL);
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }

    code_bytes = (uint32_t)token_count * 4u;
    shader = (D9Shader *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*shader));
    if (!shader)
        return TRACE_REFUSE(E_OUTOFMEMORY);
    shader->code = (DWORD *)HeapAlloc(GetProcessHeap(), 0, code_bytes);
    if (!shader->code) {
        HeapFree(GetProcessHeap(), 0, shader);
        return TRACE_REFUSE(E_OUTOFMEMORY);
    }
    CopyMemory(shader->code, bytecode, code_bytes);
    if (want_pixel)
        shader->iface.pixel.lpVtbl = &g_pixel_shader_vtbl;
    else
        shader->iface.vertex.lpVtbl = &g_vertex_shader_vtbl;
    shader->refcount = 1;
    shader->device = device;
    shader->handle = allocate_shader_handle();
    shader->is_pixel = want_pixel;
    shader->token_count = token_count;
    shader_bytecode_hash(shader->code, token_count, &shader->hash_low,
            &shader->hash_high);
    /* Before the emit, so a shader whose CREATE command cannot be reserved
     * (DMA ring full) still contributes to the corpus. */
    dump_shader_bytecode(shader->code, token_count, want_pixel,
            shader->hash_low, shader->hash_high);
    device_child_add_ref(device);

    if (!emit_shader_create(device, shader)) {
        device_child_release(device);
        HeapFree(GetProcessHeap(), 0, shader->code);
        HeapFree(GetProcessHeap(), 0, shader);
        return TRACE_REFUSE(D3DERR_DRIVERINTERNALERROR);
    }

    shader->next_device_resource = device->shaders;
    device->shaders = shader;
    *shader_out = shader;
    TRACE("OK CreateShader version=%08lX pixel=%lu tokens=%lu handle=%08lX "
            "hash=%08lX%08lX", shader->code[0], (DWORD)want_pixel, token_count,
            shader->handle, shader->hash_high, shader->hash_low);
    return D3D_OK;
}
