/* Four frames per scene: raw single, clipped single, clipped split, raw split.
 * The last frame deliberately issues no declaration change after the internal
 * clip emission, so the GPU capture verifies restoration of the host layout. */
static __attribute__((noinline)) void vertex_stream_clipping_cases(void)
{
    D3DMATRIX identity;
    ZeroMemory(&identity, sizeof(identity));
    identity._11 = identity._22 = identity._33 = identity._44 = 1;
    OK(CALL(SetTransform)(device, D3DTS_WORLD, &identity));
    OK(CALL(SetTransform)(device, D3DTS_VIEW, &identity));
    OK(CALL(SetTransform)(device, D3DTS_PROJECTION, &identity));
    OK(CALL(SetRenderState)(device, D3DRS_LIGHTING, FALSE));
    OK(CALL(SetRenderState)(device, D3DRS_ZENABLE, FALSE));
    OK(CALL(SetRenderState)(device, D3DRS_CULLMODE, D3DCULL_NONE));
    OK(CALL(SetRenderState)(device, D3DRS_FOGENABLE, FALSE));
    OK(CALL(SetRenderState)(device, D3DRS_FOGVERTEXMODE, D3DFOG_NONE));
    OK(CALL(SetRenderState)(device, D3DRS_ALPHABLENDENABLE, FALSE));
    OK(CALL(SetTextureStageState)(device, 0, D3DTSS_COLOROP, D3DTOP_MODULATE));
    OK(CALL(SetTextureStageState)(device, 0, D3DTSS_COLORARG1, D3DTA_DIFFUSE));
    OK(CALL(SetTextureStageState)(device, 0, D3DTSS_COLORARG2, D3DTA_TEXTURE));
    OK(CALL(SetTextureStageState)(device, 0, D3DTSS_ALPHAOP, D3DTOP_SELECTARG1));
    OK(CALL(SetTextureStageState)(device, 0, D3DTSS_ALPHAARG1, D3DTA_DIFFUSE));
    OK(CALL(SetTextureStageState)(device, 0, D3DTSS_TEXCOORDINDEX, 0));
    OK(CALL(SetTextureStageState)(device, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE));
    OK(CALL(SetSamplerState)(device, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP));
    OK(CALL(SetSamplerState)(device, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP));
    OK(CALL(SetSamplerState)(device, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT));
    OK(CALL(SetSamplerState)(device, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT));
    OK(CALL(SetSamplerState)(device, 0, D3DSAMP_MIPFILTER, D3DTEXF_NONE));
    IDirect3DTexture9* texture;
    OK(CALL(CreateTexture)(device, 2, 2, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &texture, NULL));
    D3DLOCKED_RECT locked;
    OK(IDirect3DTexture9_LockRect(texture, 0, &locked, NULL, 0));
    const DWORD texels[4] = { 0xffff4010u, 0xff2080ffu, 0xff80ff20u, 0xff4020c0u };
    for (unsigned y = 0; y < 2; ++y)
        CopyMemory((BYTE*)locked.pBits + y * locked.Pitch, texels + y * 2, 8);
    OK(IDirect3DTexture9_UnlockRect(texture, 0));
    OK(CALL(SetTexture)(device, 0, (IDirect3DBaseTexture9*)texture));
    Buffer *positions, *float_color, *word_color, *coordinates, *split, *control, *raw;
    Indices* indices;
    void* mapped;
    OK(CALL(CreateVertexBuffer)(device, 80, D3DUSAGE_SOFTWAREPROCESSING, 0, D3DPOOL_SYSTEMMEM, &positions, NULL));
    OK(CALL(CreateVertexBuffer)(device, 24, D3DUSAGE_SOFTWAREPROCESSING, 0, D3DPOOL_SYSTEMMEM, &float_color, NULL));
    OK(CALL(CreateVertexBuffer)(device, 12, D3DUSAGE_SOFTWAREPROCESSING, 0, D3DPOOL_SYSTEMMEM, &word_color, NULL));
    OK(CALL(CreateVertexBuffer)(device, 48, D3DUSAGE_SOFTWAREPROCESSING, 0, D3DPOOL_SYSTEMMEM, &coordinates, NULL));
    OK(CALL(CreateVertexBuffer)(device, 64, D3DUSAGE_SOFTWAREPROCESSING, 4, D3DPOOL_SYSTEMMEM, &split, NULL));
    OK(CALL(CreateVertexBuffer)(device, 112, D3DUSAGE_SOFTWAREPROCESSING, 0x144, D3DPOOL_SYSTEMMEM, &control, NULL));
    OK(CALL(CreateVertexBuffer)(device, 84, D3DUSAGE_SOFTWAREPROCESSING, 0x144, D3DPOOL_SYSTEMMEM, &raw, NULL));
    OK(CALL(CreateIndexBuffer)(device, 6, D3DUSAGE_SOFTWAREPROCESSING, D3DFMT_INDEX16, D3DPOOL_SYSTEMMEM, &indices, NULL));
    float points[4][3] = { { -2, -.5f, .5f }, { .5f, -.5f, .5f }, { 0, .75f, -.25f }, { .25f, .25f, .5f } }, uv[4][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 }, { .5f, .5f } }, color[4] = { 128.0f / 255, 192.0f / 255, 1, 1 }, screen[3][4] = { { 0, 0, .5f, 1 }, { 128, 0, .5f, 1 }, { 0, 128, .5f, 1 } };
    DWORD packed = 0xff80c0ffu;
    OK(BUF(Lock)(positions, 0, 80, &mapped, 0));
    FillMemory(mapped, 80, 0x63);
    for (unsigned i = 0; i < 4; ++i)
        CopyMemory((BYTE*)mapped + 8 + i * 20, points[i], 12);
    OK(BUF(Unlock)(positions));
    OK(BUF(Lock)(float_color, 0, 24, &mapped, 0));
    FillMemory(mapped, 24, 0x63);
    CopyMemory((BYTE*)mapped + 8, color, 16);
    OK(BUF(Unlock)(float_color));
    OK(BUF(Lock)(word_color, 0, 12, &mapped, 0));
    FillMemory(mapped, 12, 0x63);
    CopyMemory((BYTE*)mapped + 8, &packed, 4);
    OK(BUF(Unlock)(word_color));
    OK(BUF(Lock)(coordinates, 0, 48, &mapped, 0));
    FillMemory(mapped, 48, 0x63);
    for (unsigned i = 0; i < 4; ++i)
        CopyMemory((BYTE*)mapped + 4 + i * 12, uv[i], 8);
    OK(BUF(Unlock)(coordinates));
    OK(BUF(Lock)(raw, 0, 84, &mapped, 0));
    for (unsigned i = 0; i < 3; ++i) {
        CopyMemory((BYTE*)mapped + i * 28, screen[i], 16);
        CopyMemory((BYTE*)mapped + i * 28 + 16, &packed, 4);
        CopyMemory((BYTE*)mapped + i * 28 + 20, uv[i], 8);
    }
    OK(BUF(Unlock)(raw));
    OK(IB(Lock)(indices, 0, 6, &mapped, 0));
    for (unsigned i = 0; i < 3; ++i)
        ((WORD*)mapped)[i] = i + 1;
    OK(IB(Unlock)(indices));
    D3DVERTEXELEMENT9 source_elements[] = { { 3, 4, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_POSITION, 0 }, { 15, 4, D3DDECLTYPE_FLOAT4, 0, D3DDECLUSAGE_COLOR, 0 }, { 7, 0, D3DDECLTYPE_FLOAT2, 0, D3DDECLUSAGE_TEXCOORD, 0 }, D3DDECL_END() };
    D3DVERTEXELEMENT9 screen_elements[] = { { 3, 0, D3DDECLTYPE_FLOAT4, 0, D3DDECLUSAGE_POSITIONT, 0 }, { 15, 4, D3DDECLTYPE_D3DCOLOR, 0, D3DDECLUSAGE_COLOR, 0 }, { 7, 0, D3DDECLTYPE_FLOAT2, 0, D3DDECLUSAGE_TEXCOORD, 0 }, D3DDECL_END() };
    IDirect3DVertexDeclaration9 *input, *output;
    OK(CALL(CreateVertexDeclaration)(device, source_elements, &input));
    OK(CALL(CreateVertexDeclaration)(device, screen_elements, &output));
    for (unsigned scene = 0; scene < 3; ++scene) {
        Viewport process_vp = { 0, 0, 128, 128, .25f, .75f }, draw_vp = process_vp;
        if (scene == 1)
            draw_vp = (Viewport) { 3, 5, 120, 116, .8f, .9f };
        OK(CALL(SetViewport)(device, &process_vp));
        OK(CALL(SetRenderState)(device, D3DRS_CLIPPING, TRUE));
        float plane[4] = { 1, 0, 0, .2f };
        OK(CALL(SetClipPlane)(device, 0, plane));
        OK(CALL(SetRenderState)(device, D3DRS_CLIPPLANEENABLE, scene ? 1 : 0));
        OK(CALL(SetVertexDeclaration)(device, input));
        OK(CALL(SetStreamSource)(device, 0, NULL, 0, 0));
        OK(CALL(SetStreamSource)(device, 3, positions, 4, 20));
        OK(CALL(SetStreamSource)(device, 15, float_color, 4, 0));
        OK(CALL(SetStreamSource)(device, 7, coordinates, 4, 12));
        OK(CALL(ProcessVertices)(device, 0, 0, 4, split, NULL, 0));
        OK(CALL(ProcessVertices)(device, 0, 0, 4, control, NULL, 0));
        OK(CALL(SetViewport)(device, &draw_vp));
        for (unsigned frame = 0; frame < 4; ++frame) {
            if (frame < 2) {
                OK(CALL(SetFVF)(device, 0x144));
                OK(CALL(SetStreamSource)(device, 0, frame ? control : raw, 0, 28));
            }
            if (frame == 2) {
                OK(CALL(SetVertexDeclaration)(device, output));
                OK(CALL(SetStreamSource)(device, 0, NULL, 0, 0));
                OK(CALL(SetStreamSource)(device, 3, split, 0, 16));
                OK(CALL(SetStreamSource)(device, 15, word_color, 4, 0));
            }
            if (frame == 3) {
                OK(BUF(Lock)(split, 0, 48, &mapped, 0));
                for (unsigned i = 0; i < 3; ++i)
                    CopyMemory((BYTE*)mapped + i * 16, screen[i], 16);
                OK(BUF(Unlock)(split));
            }
            OK(CALL(Clear)(device, 0, NULL, D3DCLEAR_TARGET, 0xff000000u, 1, 0));
            OK(CALL(BeginScene)(device));
            if (scene == 2) {
                OK(CALL(SetIndices)(device, indices));
                OK(CALL(DrawIndexedPrimitive)(device, D3DPT_TRIANGLELIST, -1, 1, 3, 0, 1));
            } else
                OK(CALL(DrawPrimitive)(device, D3DPT_TRIANGLELIST, 0, 1));
            OK(CALL(EndScene)(device));
            OK(CALL(Present)(device, NULL, NULL, NULL, NULL));
            char line[96];
            wsprintfA(line, "IMAGE stream scene=%u frame=%u", scene, frame);
            report(line);
            if (frame == 2) {
                IDirect3DVertexDeclaration9* bound;
                OK(CALL(GetVertexDeclaration)(device, &bound));
                check(bound == output, "clip emission retains guest declaration identity");
                IDirect3DVertexDeclaration9_Release(bound);
                Buffer* bound_buffer;
                UINT offset, stride;
                OK(CALL(GetStreamSource)(device, 3, &bound_buffer, &offset, &stride));
                check(bound_buffer == split && offset == 0 && stride == 16, "clip emission retains guest stream bindings");
                BUF(Release)(bound_buffer);
            }
        }
    }
    OK(CALL(SetTexture)(device, 0, NULL));
    OK(CALL(SetIndices)(device, NULL));
    for (unsigned i = 0; i < 16; ++i)
        OK(CALL(SetStreamSource)(device, i, NULL, 0, 0));
    IDirect3DVertexDeclaration9_Release(input);
    IDirect3DVertexDeclaration9_Release(output);
    BUF(Release)(positions);
    BUF(Release)(float_color);
    BUF(Release)(word_color);
    BUF(Release)(coordinates);
    BUF(Release)(split);
    BUF(Release)(control);
    BUF(Release)(raw);
    IB(Release)(indices);
    IDirect3DTexture9_Release(texture);
    report("PASS real XP multi-stream clipping: twelve paired raw/clipped frames, ARGB and coordinates, signed indices, processing/drawing viewports, world planes, stale records and preserved guest bindings");
}
