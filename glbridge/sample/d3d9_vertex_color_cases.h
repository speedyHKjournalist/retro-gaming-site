/* Public COM floating-color input; output remains the fixed-function ARGB FVF. */
static void vertex_color_fill(Buffer* buffer, const void* data, UINT bytes)
{
    void* mapped;
    OK(BUF(Lock)(buffer, 0, bytes, &mapped, 0));
    CopyMemory(mapped, data, bytes);
    OK(BUF(Unlock)(buffer));
}
static void vertex_color_expect(Buffer* buffer, DWORD diffuse, DWORD specular, const char* label)
{
    void* mapped;
    OK(BUF(Lock)(buffer, 0, 72, &mapped, 0));
    const DWORD* words = (const DWORD*)mapped;
    if (words[10] != diffuse || words[11] != specular) {
        char text[200];
        wsprintfA(text, "FAIL typed color %s got=%08lx,%08lx expected=%08lx,%08lx", label, words[10], words[11], diffuse, specular);
        report(text);
        ExitProcess(1);
    }
    OK(BUF(Unlock)(buffer));
}
static void vertex_color_cases(void)
{
    D3DMATRIX identity;
    ZeroMemory(&identity, sizeof(identity));
    identity._11 = identity._22 = identity._33 = identity._44 = 1;
    OK(CALL(SetTransform)(device, D3DTS_WORLD, &identity));
    OK(CALL(SetTransform)(device, D3DTS_VIEW, &identity));
    OK(CALL(SetTransform)(device, D3DTS_PROJECTION, &identity));
    Viewport viewport = { 0, 0, 64, 64, 0, 1 };
    OK(CALL(SetViewport)(device, &viewport));
    OK(CALL(SetRenderState)(device, D3DRS_LIGHTING, FALSE));
    OK(CALL(SetRenderState)(device, D3DRS_SPECULARENABLE, FALSE));
    OK(CALL(SetRenderState)(device, D3DRS_FOGVERTEXMODE, D3DFOG_NONE));
    OK(CALL(SetRenderState)(device, D3DRS_COLORVERTEX, TRUE));
    OK(CALL(SetRenderState)(device, D3DRS_DIFFUSEMATERIALSOURCE, D3DMCS_COLOR1));
    OK(CALL(SetRenderState)(device, D3DRS_SPECULARMATERIALSOURCE, D3DMCS_COLOR2));
    OK(CALL(SetRenderState)(device, D3DRS_AMBIENTMATERIALSOURCE, D3DMCS_MATERIAL));
    OK(CALL(SetRenderState)(device, D3DRS_EMISSIVEMATERIALSOURCE, D3DMCS_MATERIAL));
    OK(CALL(SetRenderState)(device, D3DRS_LOCALVIEWER, FALSE));
    D3DVERTEXELEMENT9 elements[] = { { 0, 0, 2, 0, D3DDECLUSAGE_POSITION, 0 }, { 0, 12, 2, 0, D3DDECLUSAGE_NORMAL, 0 }, { 0, 24, 3, 0, D3DDECLUSAGE_COLOR, 0 }, { 0, 40, 3, 0, D3DDECLUSAGE_COLOR, 1 }, D3DDECL_END() };
    IDirect3DVertexDeclaration9* declaration;
    OK(CALL(CreateVertexDeclaration)(device, elements, &declaration));
    OK(CALL(SetVertexDeclaration)(device, declaration));
    Buffer *source, *destination;
    OK(CALL(CreateVertexBuffer)(device, 56, D3DUSAGE_SOFTWAREPROCESSING, 0, D3DPOOL_SYSTEMMEM, &source, NULL));
    OK(CALL(CreateVertexBuffer)(device, 72, D3DUSAGE_SOFTWAREPROCESSING, 0xc4, D3DPOOL_SYSTEMMEM, &destination, NULL));
    const float input[14] = { 0, 0, .5f, 0, 0, -1, .0011f, .0041f, .0069f, .5f, .0022f, .0033f, .0088f, .25f };
    vertex_color_fill(source, input, sizeof(input));
    OK(CALL(SetStreamSource)(device, 0, source, 0, 56));
    OK(CALL(ProcessVertices)(device, 0, 1, 1, destination, NULL, 0));
    vertex_color_expect(destination, 0x80000102u, 0x40010102u, "float passthrough");
    D3DMATERIAL9 material;
    ZeroMemory(&material, sizeof(material));
    material.Diffuse.r = material.Diffuse.g = material.Diffuse.b = material.Diffuse.a = 1;
    material.Specular = material.Diffuse;
    material.Power = 1;
    OK(CALL(SetMaterial)(device, &material));
    D3DLIGHT9 light;
    ZeroMemory(&light, sizeof(light));
    light.Type = D3DLIGHT_DIRECTIONAL;
    light.Direction.z = 1;
    light.Diffuse.r = light.Diffuse.g = light.Diffuse.b = 100;
    OK(CALL(SetLight)(device, 0, &light));
    OK(CALL(LightEnable)(device, 0, TRUE));
    OK(CALL(SetRenderState)(device, D3DRS_LIGHTING, TRUE));
    OK(CALL(ProcessVertices)(device, 0, 1, 1, destination, NULL, D3DPV_DONOTCOPYDATA));
    vertex_color_expect(destination, 0x801c69b0u, 0x40000000u, "material color must not be quantized before lighting");
    light.Specular = light.Diffuse;
    light.Diffuse.r = light.Diffuse.g = light.Diffuse.b = 0;
    OK(CALL(SetLight)(device, 0, &light));
    OK(CALL(SetRenderState)(device, D3DRS_SPECULARENABLE, TRUE));
    OK(CALL(ProcessVertices)(device, 0, 1, 1, destination, NULL, 0));
    vertex_color_expect(destination, 0x80000000u, 0x403854e0u, "float specular source");
    BYTE sentinel[72];
    FillMemory(sentinel, sizeof(sentinel), 0x5a);
    vertex_color_fill(destination, sentinel, sizeof(sentinel));
    OK(CALL(SetRenderState)(device, D3DRS_LIGHTING, FALSE));
    OK(CALL(ProcessVertices)(device, 0, 1, 1, destination, NULL, D3DPV_DONOTCOPYDATA));
    vertex_color_expect(destination, 0x5a5a5a5au, 0x5a5a5a5au, "DONOTCOPYDATA preserves typed colors");
    float end = 1;
    DWORD end_bits;
    CopyMemory(&end_bits, &end, 4);
    OK(CALL(SetRenderState)(device, D3DRS_FOGENABLE, TRUE));
    OK(CALL(SetRenderState)(device, D3DRS_FOGVERTEXMODE, D3DFOG_LINEAR));
    OK(CALL(SetRenderState)(device, D3DRS_FOGSTART, 0));
    OK(CALL(SetRenderState)(device, D3DRS_FOGEND, end_bits));
    OK(CALL(ProcessVertices)(device, 0, 1, 1, destination, NULL, D3DPV_DONOTCOPYDATA));
    vertex_color_expect(destination, 0x5a5a5a5au, 0x805a5a5au, "fog updates only specular alpha");
    OK(CALL(SetRenderState)(device, D3DRS_FOGVERTEXMODE, D3DFOG_NONE));
    OK(CALL(SetRenderState)(device, D3DRS_FOGENABLE, FALSE));
    IDirect3DVertexDeclaration9_Release(declaration);
    elements[2].Type = elements[3].Type = D3DDECLTYPE_FLOAT16_4;
    elements[3].Offset = 32;
    OK(CALL(CreateVertexDeclaration)(device, elements, &declaration));
    OK(CALL(SetVertexDeclaration)(device, declaration));
    BYTE half_input[40];
    ZeroMemory(half_input, sizeof(half_input));
    CopyMemory(half_input, input, 24);
    const WORD half_color[8] = { 0x3800, 0x3c00, 0, 0x3c00, 0x3400, 0, 0x3c00, 0x3800 };
    CopyMemory(half_input + 24, half_color, sizeof(half_color));
    vertex_color_fill(source, half_input, sizeof(half_input));
    OK(CALL(SetStreamSource)(device, 0, source, 0, 40));
    OK(CALL(ProcessVertices)(device, 0, 1, 1, destination, NULL, 0));
    vertex_color_expect(destination, 0xff80ff00u, 0x804000ffu, "half colors");
    IDirect3DVertexDeclaration9_Release(declaration);
    elements[2].Type = elements[3].Type = D3DDECLTYPE_UBYTE4;
    elements[3].Offset = 28;
    OK(CALL(CreateVertexDeclaration)(device, elements, &declaration));
    OK(CALL(SetVertexDeclaration)(device, declaration));
    BYTE byte_input[32];
    ZeroMemory(byte_input, sizeof(byte_input));
    CopyMemory(byte_input, input, 24);
    const BYTE byte_color[8] = { 0, 1, 2, 1, 1, 0, 0, 0 };
    CopyMemory(byte_input + 24, byte_color, 8);
    vertex_color_fill(source, byte_input, sizeof(byte_input));
    OK(CALL(SetStreamSource)(device, 0, source, 0, 32));
    OK(CALL(ProcessVertices)(device, 0, 1, 1, destination, NULL, 0));
    vertex_color_expect(destination, 0xff00ffffu, 0x00ff0000u, "raw integer colors");
    IDirect3DVertexDeclaration9_Release(declaration);
    BUF(Release)(source);
    BUF(Release)(destination);
    report("PASS real XP floating color precision, specular source, fog, half/integer colors and copy flags");
}
