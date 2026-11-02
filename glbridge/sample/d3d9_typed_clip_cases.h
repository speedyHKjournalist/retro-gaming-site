/* Input bytes have an independent decoded/pixel reference in d3d_typed_clip_reference.json. */
static const struct {
    BYTE type;
    BYTE bytes[16];
} typed_draw_inputs[] = {
    { 0, { 224, 45, 144, 58 } },
    { 1, { 224, 45, 144, 58, 75, 89, 134, 59 } },
    { 2, { 224, 45, 144, 58, 75, 89, 134, 59, 101, 25, 226, 59 } },
    { 3, { 224, 45, 144, 58, 75, 89, 134, 59, 101, 25, 226, 59, 0, 0, 0, 63 } },
    { 4, { 3, 2, 1, 128 } },
    { 5, { 0, 1, 0, 1 } },
    { 6, { 1, 0, 0, 0 } },
    { 7, { 0, 0, 1, 0, 0, 0, 1, 0 } },
    { 8, { 1, 2, 3, 128 } },
    { 9, { 64, 0, 128, 0 } },
    { 10, { 64, 0, 128, 0, 192, 0, 0, 64 } },
    { 11, { 128, 0, 0, 1 } },
    { 12, { 128, 0, 0, 1, 128, 1, 0, 128 } },
    { 13, { 1, 0, 0, 0 } },
    { 14, { 1, 8, 48, 0 } },
    { 15, { 0, 20, 0, 24 } },
    { 16, { 0, 20, 0, 24, 0, 26, 0, 56 } },
    { 3, { 224, 45, 144, 58, 75, 89, 134, 59, 101, 25, 226, 59, 0, 0, 0, 63 } },
    { 3, { 224, 45, 144, 58, 75, 89, 134, 59, 101, 25, 226, 59, 0, 0, 0, 63 } }
};
static __attribute__((noinline)) void typed_vertex_clip_cases(void)
{
    D3DMATRIX identity;
    ZeroMemory(&identity, sizeof(identity));
    identity._11 = identity._22 = identity._33 = identity._44 = 1;
    OK(CALL(SetTransform)(device, D3DTS_WORLD, &identity));
    OK(CALL(SetTransform)(device, D3DTS_VIEW, &identity));
    OK(CALL(SetTransform)(device, D3DTS_PROJECTION, &identity));
    OK(CALL(SetVertexShader)(device, NULL));
    OK(CALL(SetRenderState)(device, D3DRS_LIGHTING, FALSE));
    OK(CALL(SetRenderState)(device, D3DRS_SPECULARENABLE, FALSE));
    OK(CALL(SetRenderState)(device, D3DRS_ZENABLE, FALSE));
    OK(CALL(SetRenderState)(device, D3DRS_CULLMODE, D3DCULL_NONE));
    OK(CALL(SetRenderState)(device, D3DRS_ALPHABLENDENABLE, FALSE));
    OK(CALL(SetRenderState)(device, D3DRS_FOGENABLE, FALSE));
    OK(CALL(SetRenderState)(device, D3DRS_FOGVERTEXMODE, D3DFOG_NONE));
    OK(CALL(SetRenderState)(device, D3DRS_CLIPPLANEENABLE, 0));
    OK(CALL(SetRenderState)(device, D3DRS_CLIPPING, TRUE));
    OK(CALL(SetRenderState)(device, D3DRS_TEXTUREFACTOR, 0xffffffffu));
    DWORD size_bits = 0x40800000u, min_bits = 0x3f800000u, max_bits = 0x42800000u;
    OK(CALL(SetRenderState)(device, D3DRS_POINTSIZE, size_bits));
    OK(CALL(SetRenderState)(device, D3DRS_POINTSIZE_MIN, min_bits));
    OK(CALL(SetRenderState)(device, D3DRS_POINTSIZE_MAX, max_bits));
    OK(CALL(SetRenderState)(device, D3DRS_POINTSCALEENABLE, FALSE));
    OK(CALL(SetRenderState)(device, D3DRS_POINTSPRITEENABLE, FALSE));
    for (unsigned stage = 0; stage < 3; ++stage) {
        OK(CALL(SetTexture)(device, stage, NULL));
        OK(CALL(SetTextureStageState)(device, stage, D3DTSS_COLOROP, D3DTOP_MODULATE4X));
        OK(CALL(SetTextureStageState)(device, stage, D3DTSS_COLORARG1, stage ? D3DTA_CURRENT : D3DTA_DIFFUSE));
        OK(CALL(SetTextureStageState)(device, stage, D3DTSS_COLORARG2, D3DTA_TFACTOR));
        OK(CALL(SetTextureStageState)(device, stage, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1));
        OK(CALL(SetTextureStageState)(device, stage, D3DTSS_ALPHAARG1, stage ? D3DTA_CURRENT : D3DTA_DIFFUSE));
    }
    OK(CALL(SetTextureStageState)(device, 3, D3DTSS_COLOROP, D3DTOP_DISABLE));
    IDirect3DSurface9 *saved_target, *saved_depth, *target;
    OK(CALL(GetRenderTarget)(device, 0, &saved_target));
    OK(CALL(GetDepthStencilSurface)(device, &saved_depth));
    OK(CALL(CreateRenderTarget)(device, 128, 128, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &target, NULL));
    OK(CALL(SetRenderTarget)(device, 0, target));
    OK(CALL(SetDepthStencilSurface)(device, NULL));
    Viewport viewport = { 0, 0, 128, 128, 0, 1 };
    OK(CALL(SetViewport)(device, &viewport));
    Buffer *positions, *colors, *point_size, *processed;
    void* mapped;
    OK(CALL(CreateVertexBuffer)(device, 36, D3DUSAGE_SOFTWAREPROCESSING, 0, D3DPOOL_SYSTEMMEM, &positions, NULL));
    OK(CALL(CreateVertexBuffer)(device, 20, D3DUSAGE_SOFTWAREPROCESSING, 0, D3DPOOL_SYSTEMMEM, &colors, NULL));
    OK(CALL(CreateVertexBuffer)(device, 8, D3DUSAGE_SOFTWAREPROCESSING, 0, D3DPOOL_SYSTEMMEM, &point_size, NULL));
    OK(CALL(CreateVertexBuffer)(device, 48, D3DUSAGE_SOFTWAREPROCESSING, 4, D3DPOOL_SYSTEMMEM, &processed, NULL));
    OK(BUF(Lock)(point_size, 0, 8, &mapped, 0));
    DWORD half_size = 0x4840;
    ZeroMemory(mapped, 8);
    CopyMemory((BYTE*)mapped + 4, &half_size, 4);
    OK(BUF(Unlock)(point_size));
    const DWORD sizes[17] = { 4, 8, 12, 16, 4, 4, 4, 8, 4, 4, 8, 4, 8, 4, 4, 4, 8 };
    const DWORD pixel_code[] = { 0xffff0300u, 0x0200001fu, 0x8001000au, 0x900f0000u, 0x03000005u, 0x800f0800u, 0x90e40000u, 0xa0e40000u, 0xffffu };
    IDirect3DPixelShader9* specular_shader;
    OK(CALL(CreatePixelShader)(device, pixel_code, &specular_shader));
    float gain[4] = { 64, 64, 64, 1 };
    OK(CALL(SetPixelShaderConstantF)(device, 0, gain, 1));
    const D3DVERTEXELEMENT9 end = D3DDECL_END();
    for (unsigned row = 0; row < sizeof(typed_draw_inputs) / sizeof(typed_draw_inputs[0]); ++row) {
        BOOL point = row == 17, specular = row == 18;
        UINT count = point ? 1 : 3;
        D3DPRIMITIVETYPE primitive = point ? D3DPT_POINTLIST : D3DPT_TRIANGLELIST;
        float world[3][3] = { { -2, -.5f, .5f }, { .5f, -.5f, .5f }, { 0, .5f, -.5f } };
        if (point) {
            world[0][0] = world[0][1] = 0;
            world[0][2] = .5f;
        }
        OK(BUF(Lock)(positions, 0, 36, &mapped, 0));
        CopyMemory(mapped, world, 36);
        OK(BUF(Unlock)(positions));
        OK(BUF(Lock)(colors, 0, 20, &mapped, 0));
        ZeroMemory(mapped, 20);
        CopyMemory((BYTE*)mapped + 4, typed_draw_inputs[row].bytes, sizes[typed_draw_inputs[row].type]);
        OK(BUF(Unlock)(colors));
        D3DVERTEXELEMENT9 input_elements[4] = { { 3, 0, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_POSITION, 0 }, { 15, 4, 0, 0, D3DDECLUSAGE_COLOR, 0 }, { 7, 4, D3DDECLTYPE_FLOAT16_2, 0, D3DDECLUSAGE_PSIZE, 0 }, D3DDECL_END() };
        input_elements[1].Type = typed_draw_inputs[row].type;
        input_elements[1].UsageIndex = specular ? 1 : 0;
        if (!point)
            input_elements[2] = end;
        D3DVERTEXELEMENT9 output_elements[4];
        CopyMemory(output_elements, input_elements, sizeof(input_elements));
        output_elements[0].Type = D3DDECLTYPE_FLOAT4;
        output_elements[0].Usage = D3DDECLUSAGE_POSITIONT;
        IDirect3DVertexDeclaration9 *input, *output;
        OK(CALL(CreateVertexDeclaration)(device, input_elements, &input));
        OK(CALL(CreateVertexDeclaration)(device, output_elements, &output));
        OK(CALL(SetVertexDeclaration)(device, input));
        OK(CALL(SetStreamSource)(device, 0, NULL, 0, 0));
        OK(CALL(SetStreamSource)(device, 3, positions, 0, 12));
        OK(CALL(SetStreamSource)(device, 15, colors, 0, 0));
        OK(CALL(SetStreamSource)(device, 7, point ? point_size : NULL, 0, 0));
        OK(CALL(SetPixelShader)(device, specular ? specular_shader : NULL));
        OK(CALL(ProcessVertices)(device, 0, 0, count, processed, NULL, 0));
        for (unsigned frame = 0; frame < 4; ++frame) {
            if (frame == 1) {
                OK(CALL(SetVertexDeclaration)(device, output));
                OK(CALL(SetStreamSource)(device, 3, processed, 0, 16));
            }
            if (frame == 2) {
                float screen[3][4] = { { 0, 0, .5f, 1 }, { 128, 0, .5f, 1 }, { 0, 128, .5f, 1 } };
                if (point)
                    screen[0][0] = screen[0][1] = 64;
                OK(BUF(Lock)(processed, 0, count * 16, &mapped, 0));
                CopyMemory(mapped, screen, count * 16);
                OK(BUF(Unlock)(processed));
            }
            if (frame == 3)
                OK(CALL(SetVertexDeclaration)(device, output));
            OK(CALL(Clear)(device, 0, NULL, D3DCLEAR_TARGET, 0xff000000u, 1, 0));
            OK(CALL(BeginScene)(device));
            OK(CALL(DrawPrimitive)(device, primitive, 0, 1));
            OK(CALL(EndScene)(device));
            OK(CALL(Present)(device, NULL, NULL, NULL, NULL));
            char line[96];
            wsprintfA(line, "IMAGE typed case=%u frame=%u type=%u", row, frame, typed_draw_inputs[row].type);
            report(line);
        }
        IDirect3DVertexDeclaration9_Release(input);
        IDirect3DVertexDeclaration9_Release(output);
    }
    OK(CALL(SetPixelShader)(device, NULL));
    IDirect3DPixelShader9_Release(specular_shader);
    for (unsigned i = 0; i < 16; ++i)
        OK(CALL(SetStreamSource)(device, i, NULL, 0, 0));
    BUF(Release)(positions);
    BUF(Release)(colors);
    BUF(Release)(point_size);
    BUF(Release)(processed);
    OK(CALL(SetRenderTarget)(device, 0, saved_target));
    OK(CALL(SetDepthStencilSurface)(device, saved_depth));
    IDirect3DSurface9_Release(saved_target);
    IDirect3DSurface9_Release(saved_depth);
    IDirect3DSurface9_Release(target);
    report("PASS real XP typed clipping: 76 direct/processed/restored frames, 17 colour formats with 64x gain, half point size, floating specular and independent raw draw controls");
}
