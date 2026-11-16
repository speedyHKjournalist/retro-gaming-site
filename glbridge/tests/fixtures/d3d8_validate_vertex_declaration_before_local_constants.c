static BOOL validate_vertex_declaration(const DWORD *tokens, UINT max_tokens,
        UINT *token_count)
{
    UINT offset = 0;
    while (offset < max_tokens) {
        DWORD token = tokens[offset];
        DWORD token_type;
        if (token == 0xFFFFFFFFu) {
            *token_count = offset;
            return TRUE;
        }
        token_type = (token >> D3DVSD_TOKENTYPESHIFT) & 0x7u;
        if (token_type > (DWORD)D3DVSD_TOKEN_END)
            return FALSE;
        ++offset;
    }
    return FALSE;
}
