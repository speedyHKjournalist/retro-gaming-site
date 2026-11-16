static BOOL emit_create_vertex_shader(D8Device* device, D8Shader* shader)
{
    D9WGCreateVertexDeclaration declaration;
    D9WGCreateVertexShader command;
    uint8_t* payload;
    uint8_t* element_blob;
    uint8_t* code_blob;
    UINT wire_tokens = shader->code_token_count + shader->const_vector_count * 6u;
    UINT code_bytes = wire_tokens * (UINT)sizeof(DWORD);
    UINT element_bytes;
    BOOL result;

    element_bytes = shader->element_count * (UINT)sizeof(D9WGVertexElement);

    ZeroMemory(&declaration, sizeof(declaration));
    ZeroMemory(&command, sizeof(command));
    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_CREATE_VERTEX_DECLARATION,
        sizeof(declaration), element_bytes, NULL, &payload, &element_blob);
    if (result) {
        declaration.device_handle = device->handle;
        declaration.resource_handle = shader->declaration_handle;
        declaration.element_count = shader->element_count;
        CopyMemory(payload, &declaration, sizeof(declaration));
        CopyMemory(element_blob, shader->elements, element_bytes);
        if (!shader->declaration_only)
            result = reserve_command_locked(D9WG_OP_CREATE_VERTEX_SHADER,
                sizeof(command), code_bytes, NULL, &payload, &code_blob);
    }
    if (result && !shader->declaration_only) {
        command.device_handle = device->handle;
        command.resource_handle = shader->handle;
        DWORD *code = (DWORD *)code_blob;
        UINT at = 1;
        code[0] = shader->code_tokens[0];
        for (UINT reg = 0; reg < D8WG_MAX_VS_CONSTANTS; ++reg) {
            if (!shader->const_mask[reg]) continue;
            code[at++] = 0x00000051u; /* SM1 DEF */
            code[at++] = 0xa00f0000u | reg; /* cN.xyzw */
            CopyMemory(code + at, shader->const_data + reg * 4u, 16);
            at += 4;
        }
        CopyMemory(code + at, shader->code_tokens + 1,
                (shader->code_token_count - 1u) * sizeof(DWORD));
        shader_bytecode_hash(code, wire_tokens, &shader->hash_low,
                &shader->hash_high);
        command.instruction_token_count = wire_tokens;
        command.code_offset = (uint32_t)(code_blob - batch_base());
        command.bytecode_hash_low = shader->hash_low;
        command.bytecode_hash_high = shader->hash_high;
        CopyMemory(payload, &command, sizeof(command));
    }
    LeaveCriticalSection(&g_transport_lock);
    return result;
}
