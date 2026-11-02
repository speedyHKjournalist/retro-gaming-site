/* Real COM calls: the source position need not be in stream zero. */
static __attribute__((noinline)) void vertex_stream_cases(void)
{
    D3DMATRIX identity;
    ZeroMemory(&identity, sizeof(identity));
    identity._11 = identity._22 = identity._33 = identity._44 = 1;
    OK(CALL(SetTransform)(device, D3DTS_WORLD, &identity));
    OK(CALL(SetTransform)(device, D3DTS_VIEW, &identity));
    OK(CALL(SetTransform)(device, D3DTS_PROJECTION, &identity));
    OK(CALL(SetRenderState)(device, D3DRS_LIGHTING, FALSE));
    OK(CALL(SetRenderState)(device, D3DRS_CLIPPING, TRUE));
    Viewport viewport = { 7, 11, 100, 80, .25f, .75f };
    OK(CALL(SetViewport)(device, &viewport));
    D3DVERTEXELEMENT9 elements[] = { { 3, 4, D3DDECLTYPE_FLOAT3, 0, D3DDECLUSAGE_POSITION, 0 }, { 15, 4, D3DDECLTYPE_FLOAT4, 0, D3DDECLUSAGE_COLOR, 0 }, { 7, 0, D3DDECLTYPE_DEC3N, 0, D3DDECLUSAGE_NORMAL, 0 }, D3DDECL_END() };
    IDirect3DVertexDeclaration9* declaration;
    OK(CALL(CreateVertexDeclaration)(device, elements, &declaration));
    OK(CALL(SetVertexDeclaration)(device, declaration));
    Buffer *positions, *colors, *normals, *destination;
    void* mapped;
    OK(CALL(CreateVertexBuffer)(device, 68, D3DUSAGE_SOFTWAREPROCESSING, 0, D3DPOOL_SYSTEMMEM, &positions, NULL));
    OK(CALL(CreateVertexBuffer)(device, 24, D3DUSAGE_SOFTWAREPROCESSING, 0, D3DPOOL_SYSTEMMEM, &colors, NULL));
    OK(CALL(CreateVertexBuffer)(device, 12, D3DUSAGE_SOFTWAREPROCESSING, 0, D3DPOOL_SYSTEMMEM, &normals, NULL));
    OK(CALL(CreateVertexBuffer)(device, 128, D3DUSAGE_SOFTWAREPROCESSING, 0x54, D3DPOOL_SYSTEMMEM, &destination, NULL));
    float points[3][3] = { { -.5f, 0, .5f }, { 0, .5f, .25f }, { 2, 0, .5f } }, color[4] = { .25f, .5f, .75f, .5f };
    DWORD normal = 512u | (511u << 20);
    OK(BUF(Lock)(positions, 0, 68, &mapped, 0));
    FillMemory(mapped, 68, 0x63);
    for (unsigned i = 0; i < 3; ++i)
        CopyMemory((BYTE*)mapped + 8 + i * 24, points[i], 12);
    OK(BUF(Unlock)(positions));
    OK(BUF(Lock)(colors, 0, 24, &mapped, 0));
    FillMemory(mapped, 24, 0x63);
    CopyMemory((BYTE*)mapped + 8, color, 16);
    OK(BUF(Unlock)(colors));
    OK(BUF(Lock)(normals, 0, 12, &mapped, 0));
    for (unsigned i = 0; i < 3; ++i)
        CopyMemory((BYTE*)mapped + i * 4, &normal, 4);
    OK(BUF(Unlock)(normals));
    OK(CALL(SetStreamSource)(device, 0, NULL, 0, 0));
    OK(CALL(SetStreamSource)(device, 3, positions, 4, 24));
    OK(CALL(SetStreamSource)(device, 15, colors, 4, 0));
    OK(CALL(SetStreamSource)(device, 7, normals, 0, 4));
    for (unsigned copy = 0; copy < 2; ++copy) {
        OK(BUF(Lock)(destination, 0, 128, &mapped, 0));
        FillMemory(mapped, 128, 0xa5);
        OK(BUF(Unlock)(destination));
        D3DCLIPSTATUS9 status = { 0, 0xffffffffu };
        OK(CALL(SetClipStatus)(device, &status));
        OK(CALL(ProcessVertices)(device, 1, 1, 2, destination, NULL, copy ? 0 : D3DPV_DONOTCOPYDATA));
        OK(CALL(GetClipStatus)(device, &status));
        check(status.ClipUnion == 2 && status.ClipIntersection == 0, "multi-stream clip records");
        OK(BUF(Lock)(destination, 0, 128, &mapped, 0));
        const float *first = (const float*)((BYTE*)mapped + 32), *second = (const float*)((BYTE*)mapped + 64);
        check(first[0] == 57 && first[1] == 31 && first[2] == .375f && first[3] == 1, "nonzero position stream and source index");
        /* The advertised zero guard band clips at the viewport. The second
         * vertex intentionally retains the established homogeneous record. */
        check(second[0] == 1.5f && second[1] == .5f && second[2] == .5f && second[3] == 1, "second gathered vertex keeps clipping representation");
        if (copy) {
            check(first[4] == -1 && first[5] == 0 && first[6] == 1, "packed normal in separate stream");
            check(((const DWORD*)first)[7] == 0x804080bfu && ((const DWORD*)second)[7] == 0x804080bfu, "constant floating color stream");
        } else
            for (unsigned i = 4; i < 8; ++i)
                check(((const DWORD*)first)[i] == 0xa5a5a5a5u && ((const DWORD*)second)[i] == 0xa5a5a5a5u, "multi-stream copy suppression");
        for (unsigned i = 0; i < 32; ++i)
            check(((BYTE*)mapped)[i] == 0xa5, "multi-stream prefix guard");
        for (unsigned i = 96; i < 128; ++i)
            check(((BYTE*)mapped)[i] == 0xa5, "multi-stream suffix guard");
        OK(BUF(Unlock)(destination));
    }
    OK(BUF(Lock)(destination, 0, 128, &mapped, 0));
    FillMemory(mapped, 128, 0xa5);
    OK(BUF(Unlock)(destination));
    D3DCLIPSTATUS9 saved_status = { 0x80000000u, 0x80000000u };
    OK(CALL(SetClipStatus)(device, &saved_status));
    OK(CALL(SetStreamSource)(device, 15, colors, 5, 0));
    check(FAILED(CALL(ProcessVertices)(device, 1, 1, 2, destination, NULL, 0)), "late constant stream truncation");
    OK(CALL(SetStreamSource)(device, 15, colors, 4, 0));
    OK(BUF(Lock)(colors, 0, 24, &mapped, 0));
    check(FAILED(CALL(ProcessVertices)(device, 1, 1, 2, destination, NULL, 0)), "locked late stream");
    OK(BUF(Unlock)(colors));
    OK(CALL(SetStreamSourceFreq)(device, 15, D3DSTREAMSOURCE_INSTANCEDATA | 1));
    check(FAILED(CALL(ProcessVertices)(device, 1, 1, 2, destination, NULL, 0)), "instancing is not software gather");
    OK(CALL(SetStreamSourceFreq)(device, 15, 1));
    OK(BUF(Lock)(destination, 0, 128, &mapped, 0));
    for (unsigned i = 0; i < 128; ++i)
        check(((BYTE*)mapped)[i] == 0xa5, "atomic multi-stream refusal");
    OK(BUF(Unlock)(destination));
    OK(CALL(GetClipStatus)(device, &saved_status));
    check(saved_status.ClipUnion == 0x80000000u && saved_status.ClipIntersection == 0x80000000u, "failed gather preserves clip status");
    OK(CALL(SetStreamSource)(device, 3, positions, 4, 0));
    OK(CALL(SetStreamSource)(device, 7, normals, 0, 0));
    OK(CALL(ProcessVertices)(device, 0xffffffffu, 0, 2, destination, NULL, 0));
    OK(BUF(Lock)(destination, 0, 128, &mapped, 0));
    check(((float*)mapped)[0] == 32 && ((float*)mapped)[8] == 32, "zero stride ignores huge source index");
    OK(BUF(Unlock)(destination));
    /* Both position and color alias the destination. Vertex one's original
     * position and the constant color are overwritten by vertex zero. */
    OK(BUF(Lock)(destination, 0, 128, &mapped, 0));
    FillMemory(mapped, 128, 0xa5);
    CopyMemory(mapped, points[0], 12);
    CopyMemory((BYTE*)mapped + 32, points[1], 12);
    CopyMemory((BYTE*)mapped + 48, color, 16);
    OK(BUF(Unlock)(destination));
    elements[0].Offset = 0;
    elements[1].Offset = 0;
    IDirect3DVertexDeclaration9* alias;
    OK(CALL(CreateVertexDeclaration)(device, elements, &alias));
    OK(CALL(SetVertexDeclaration)(device, alias));
    OK(CALL(SetStreamSource)(device, 3, destination, 0, 32));
    OK(CALL(SetStreamSource)(device, 15, destination, 48, 0));
    OK(CALL(ProcessVertices)(device, 0, 1, 2, destination, NULL, 0));
    OK(BUF(Lock)(destination, 0, 128, &mapped, 0));
    const float *a = (const float*)((BYTE*)mapped + 32), *b = (const float*)((BYTE*)mapped + 64);
    check(a[0] == 32 && a[1] == 51 && b[0] == 57 && b[1] == 31, "all alias bindings use pre-call position bytes");
    check(((const DWORD*)a)[7] == 0x804080bfu && ((const DWORD*)b)[7] == 0x804080bfu, "aliased constant color snapshot");
    OK(BUF(Unlock)(destination));
    for (unsigned i = 0; i < 16; ++i)
        OK(CALL(SetStreamSource)(device, i, NULL, 0, 0));
    IDirect3DVertexDeclaration9_Release(alias);
    IDirect3DVertexDeclaration9_Release(declaration);
    BUF(Release)(positions);
    BUF(Release)(colors);
    BUF(Release)(normals);
    BUF(Release)(destination);
    report("PASS real XP multi-stream: nonzero position stream, offsets, packed normal, floating constant color, copy flags, clip records, all-constant huge index, multiple destination aliases and atomic refusals");
}
