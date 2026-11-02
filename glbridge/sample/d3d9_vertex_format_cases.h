#ifdef D9WG_VERTEX_STREAM_CASES
#include "d3d9_vertex_stream_cases.h"
#endif
#ifdef D9WG_VERTEX_COLOR_CASES
#include "d3d9_vertex_color_cases.h"
#endif
/* D3D9 public COM typed-input processing, with independent literal values. */
static void vertex_format_cases(void)
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
    D3DVERTEXELEMENT9 input_elements[] = { { 0, 0, D3DDECLTYPE_FLOAT16_4, 0, D3DDECLUSAGE_POSITION, 0 }, { 0, 8, D3DDECLTYPE_DEC3N, 0, D3DDECLUSAGE_NORMAL, 0 }, { 0, 12, D3DDECLTYPE_SHORT2N, 0, D3DDECLUSAGE_TEXCOORD, 0 }, { 0, 16, D3DDECLTYPE_FLOAT16_2, 0, D3DDECLUSAGE_PSIZE, 0 }, D3DDECL_END() };
    IDirect3DVertexDeclaration9* declaration;
    OK(CALL(CreateVertexDeclaration)(device, input_elements, &declaration));
    OK(CALL(SetVertexDeclaration)(device, declaration));
    Buffer *source, *destination;
    OK(CALL(CreateVertexBuffer)(device, 44, D3DUSAGE_SOFTWAREPROCESSING, 0, D3DPOOL_SYSTEMMEM, &source, NULL));
    OK(CALL(CreateVertexBuffer)(device, 168, D3DUSAGE_SOFTWAREPROCESSING, 0x201f4, D3DPOOL_SYSTEMMEM, &destination, NULL));
    BYTE input[44];
    ZeroMemory(input, sizeof(input));
    const WORD position[4] = { 0x3400, 0xb800, 0x3800, 0x4000 }, texture[2] = { 0x8000, 16384 }, size[2] = { 0x3e00, 0 };
    DWORD normal = 512u | (511u << 20);
    for (unsigned i = 0; i < 2; ++i) {
        CopyMemory(input + 4 + i * 20, position, 8);
        CopyMemory(input + 12 + i * 20, &normal, 4);
        CopyMemory(input + 16 + i * 20, texture, 4);
        CopyMemory(input + 20 + i * 20, size, 4);
    }
    void* mapped;
    OK(BUF(Lock)(source, 0, sizeof(input), &mapped, 0));
    CopyMemory(mapped, input, sizeof(input));
    OK(BUF(Unlock)(source));
    OK(CALL(SetStreamSource)(device, 0, source, 4, 20));
    for (unsigned copy = 0; copy < 2; ++copy) {
        OK(BUF(Lock)(destination, 0, 168, &mapped, 0));
        FillMemory(mapped, 168, 0xa5);
        OK(BUF(Unlock)(destination));
        D3DCLIPSTATUS9 status = { 0, 0xffffffffu };
        OK(CALL(SetClipStatus)(device, &status));
        OK(CALL(ProcessVertices)(device, 1, 2, 1, destination, NULL, copy ? 0 : D3DPV_DONOTCOPYDATA));
        OK(CALL(GetClipStatus)(device, &status));
        check(status.ClipUnion == 0 && status.ClipIntersection == 0, "packed position clip status");
        OK(BUF(Lock)(destination, 0, 168, &mapped, 0));
        const float* values = (const float*)((const BYTE*)mapped + 112);
        check(values[0] == 63.25f && values[1] == 61 && values[2] == .375f && values[3] == .5f, "half position and stream offset");
        const DWORD* words = (const DWORD*)values;
        for (unsigned i = 0; i < 112; ++i)
            check(((const BYTE*)mapped)[i] == 0xa5, "destination offset guard");
        if (copy) {
            check(values[4] == -1 && values[5] == 0 && values[6] == 1, "DEC3N normal");
            check(values[7] == 1.5f, "half point size");
            check(words[8] == 0xffffffffu && words[9] == 0, "generated default colors");
            check(values[10] == -1 && values[11] == 16384.0f / 32767.0f, "normalized short coordinates");
            check(words[12] == 0xa5a5a5a5u && words[13] == 0xa5a5a5a5u, "retained destination texture components");
        } else
            for (unsigned i = 4; i < 14; ++i)
                check(words[i] == 0xa5a5a5a5u, "DONOTCOPYDATA typed attributes");
        OK(BUF(Unlock)(destination));
    }
    /* A typed declaration cannot make an unsupported output partially write. */
    D3DVERTEXELEMENT9 output_elements[] = { { 0, 0, D3DDECLTYPE_FLOAT4, 0, D3DDECLUSAGE_POSITIONT, 0 }, { 0, 16, D3DDECLTYPE_FLOAT16_4, 0, D3DDECLUSAGE_TEXCOORD, 0 }, D3DDECL_END() };
    IDirect3DVertexDeclaration9* output;
    OK(CALL(CreateVertexDeclaration)(device, output_elements, &output));
    OK(BUF(Lock)(destination, 0, 168, &mapped, 0));
    FillMemory(mapped, 168, 0xa5);
    OK(BUF(Unlock)(destination));
    check(FAILED(CALL(ProcessVertices)(device, 1, 0, 1, destination, output, 0)), "packed output explicitly refused");
    OK(CALL(SetStreamSource)(device, 0, source, 40, 20));
    check(FAILED(CALL(ProcessVertices)(device, 0, 0, 1, destination, NULL, 0)), "truncated packed source refused");
    OK(BUF(Lock)(destination, 0, 168, &mapped, 0));
    for (unsigned i = 0; i < 168; ++i)
        check(((const BYTE*)mapped)[i] == 0xa5, "atomic format/range failure");
    OK(BUF(Unlock)(destination));
    IDirect3DVertexDeclaration9_Release(output);
    IDirect3DVertexDeclaration9_Release(declaration);
    BUF(Release)(source);
    BUF(Release)(destination);
#ifdef D9WG_VERTEX_COLOR_CASES
    vertex_color_cases();
#endif
#ifdef D9WG_VERTEX_STREAM_CASES
    vertex_stream_cases();
#endif
    report("PASS real XP typed input: half positions, DEC3N normals, normalized coordinates, half size, stream/destination offsets, retained components, copy flags, clip status and atomic refusals");
}
