/* Actual SDK8 shader creation,sitefcac34e. */
static HRESULT WINAPI device_create_vertex_shader(IDirect3DDevice8* iface,
    const DWORD* declaration, const DWORD* function, DWORD* shader,
    DWORD usage)
{
    D8Device* device = device_from_iface(iface);
    D8Shader* entry;
    UINT decl_count = 0;
    UINT body_count = 0;
    DWORD version = 0;
    UINT minor = 0;
    (void)usage;

    if (shader)
        *shader = 0;
    if (!declaration || !shader)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (function) {
        version = function[0];
        minor = (UINT)D3DSHADER_VERSION_MINOR(version);
        /* A device that advertises vs_1_1 must also run vs_1_0: 1.1 only adds the
         * address register, so 1.0 is a strict subset and the runtime hands it
         * straight to the driver. Refusing it looks like a driver that cannot do
         * vertex shaders at all -- 3DMark 2001 assembles most of its shaders as
         * `vs.1.0`, does not check the HRESULT, and dereferences the handle it
         * never got. */
        if ((version & 0xFFFF0000u) != 0xFFFE0000u
            || (UINT)D3DSHADER_VERSION_MAJOR(version) != 1u || minor > 1u) {
            D8WG_TRACE("SHADER REFUSE kind=vertex stage=version version=%08lX",
                version);
            return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
        }
    }
    if (!validate_vertex_declaration(declaration, D8WG_MAX_SHADER_TOKENS,
            &decl_count)) {
        D8WG_TRACE("SHADER REFUSE kind=vertex stage=declaration "
                   "version=%08lX",
            version);
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    }
    if (function && !validate_shader_body(function + 1, D8WG_MAX_SHADER_TOKENS, FALSE, minor, &body_count)) {
        D8WG_TRACE("SHADER REFUSE kind=vertex stage=body version=%08lX",
            version);
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    }

    entry = (D8Shader*)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
        sizeof(*entry));
    if (!entry)
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    entry->declaration_tokens = decl_count
        ? (DWORD*)HeapAlloc(GetProcessHeap(), 0,
              decl_count * sizeof(DWORD))
        : NULL;
    entry->code_tokens = function ? (DWORD*)HeapAlloc(GetProcessHeap(), 0,
                                        (body_count + 1u) * sizeof(DWORD))
                                  : NULL;
    if ((decl_count && !entry->declaration_tokens) || (function && !entry->code_tokens)) {
        HeapFree(GetProcessHeap(), 0, entry->declaration_tokens);
        HeapFree(GetProcessHeap(), 0, entry->code_tokens);
        HeapFree(GetProcessHeap(), 0, entry);
        return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    }
    if (decl_count)
        CopyMemory(entry->declaration_tokens, declaration,
            decl_count * sizeof(DWORD));
    entry->declaration_token_count = decl_count;
    if (function)
        entry->code_tokens[0] = function[0];
    if (body_count)
        CopyMemory(entry->code_tokens + 1, function + 1,
            body_count * sizeof(DWORD));
    entry->code_token_count = function ? body_count + 1u : 0;
    entry->declaration_only = !function;
    BOOL declaration_valid = declaration_to_elements(entry->declaration_tokens,
        entry->declaration_token_count, entry->elements, D8WG_MAX_VERTEX_ELEMENTS,
        &entry->element_count, entry->const_data, D8WG_MAX_VS_CONSTANTS,
        &entry->const_start, &entry->const_vector_count);
    if (declaration_valid && entry->declaration_only) {
        for (UINT i = 0; i < entry->element_count; ++i) {
            const D9WGVertexElement* element = entry->elements + i;
            if (element->usage == D3D9DECLUSAGE_NORMAL && !element->usage_index && element->type != D3D9DECLTYPE_FLOAT3)
                declaration_valid = FALSE;
        }
    }
    if (!declaration_valid) {
        HeapFree(GetProcessHeap(), 0, entry->declaration_tokens);
        HeapFree(GetProcessHeap(), 0, entry->code_tokens);
        HeapFree(GetProcessHeap(), 0, entry);
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    }
    entry->is_pixel_shader = FALSE;
    entry->major_version = function ? 1 : 0;
    entry->minor_version = minor;
    if (function)
        shader_bytecode_hash(entry->code_tokens, entry->code_token_count,
            &entry->hash_low, &entry->hash_high);
    entry->handle = allocate_shader_handle();
    /* The declaration is a separate D9WG resource with its own handle, drawn
     * from the ordinary resource namespace rather than the shader one. */
    entry->declaration_handle = allocate_handle();

    if (!emit_create_vertex_shader(device, entry)) {
        HeapFree(GetProcessHeap(), 0, entry->declaration_tokens);
        HeapFree(GetProcessHeap(), 0, entry->code_tokens);
        HeapFree(GetProcessHeap(), 0, entry);
        return D8WG_TRACE_ERROR(D3DERR_DRIVERINTERNALERROR);
    }
    D8WG_TRACE("SHADER CREATE kind=vertex version=%08lX tokens=%u "
               "handle=%08lX hash=%08lX%08lX",
        version,
        entry->code_token_count, entry->handle,
        entry->hash_high, entry->hash_low);
    entry->device = device;
    entry->next = device->vertex_shaders;
    device->vertex_shaders = entry;
    *shader = entry->handle;
    return D3D_OK;
}
