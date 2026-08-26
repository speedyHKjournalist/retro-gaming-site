/*
 * Direct3D 7 immediate-mode frontend.
 *
 * This file is included by ddraw_proxy.c after the DirectDraw surface
 * implementation.  It therefore uses the same object graph, resource handles
 * and D9WG transport; a D3D texture/render target is literally the DDSurface
 * the application supplied rather than a wrapper around a second resource.
 */

#define D3D7_MAX_RENDER_STATES 256u
#define D3D7_MAX_TEXTURE_STAGES 8u
#define D3D7_MAX_TEXTURE_STAGE_STATES 33u
#define D3D7_MAX_TRANSFORMS 24u
#define D3D7_MAX_LIGHTS 8u
#define D3D7_MAX_CLIP_PLANES 6u
#define D3D7_MAX_VERTEX_ELEMENTS 24u

typedef struct D3D7VertexBuffer D3D7VertexBuffer;
typedef struct D3D7StateBlock D3D7StateBlock;

typedef struct D3D7FVFLayout {
    UINT stride;
    UINT position_offset;
    UINT position_bytes;
    UINT normal_offset;
    UINT diffuse_offset;
    UINT specular_offset;
    UINT texcoord_offset[8];
    UINT texcoord_bytes[8];
    UINT texcoord_count;
    BOOL has_position;
    BOOL has_normal;
    BOOL has_diffuse;
    BOOL has_specular;
} D3D7FVFLayout;

struct D3D7StateBlock {
    D3D7StateBlock *next;
    DWORD handle;
    D3DSTATEBLOCKTYPE type;
    BYTE render_mask[D3D7_MAX_RENDER_STATES];
    BYTE texture_stage_mask[D3D7_MAX_TEXTURE_STAGES]
            [D3D7_MAX_TEXTURE_STAGE_STATES];
    BYTE texture_mask[D3D7_MAX_TEXTURE_STAGES];
    BYTE transform_mask[D3D7_MAX_TRANSFORMS];
    BYTE light_mask[D3D7_MAX_LIGHTS];
    BYTE light_enable_mask[D3D7_MAX_LIGHTS];
    BYTE clip_plane_mask[D3D7_MAX_CLIP_PLANES];
    BOOL viewport_mask;
    BOOL material_mask;
    DWORD render_states[D3D7_MAX_RENDER_STATES];
    DWORD texture_stage_states[D3D7_MAX_TEXTURE_STAGES]
            [D3D7_MAX_TEXTURE_STAGE_STATES];
    DDSurface *textures[D3D7_MAX_TEXTURE_STAGES];
    D3DMATRIX transforms[D3D7_MAX_TRANSFORMS];
    D3DLIGHT7 lights[D3D7_MAX_LIGHTS];
    BOOL light_set[D3D7_MAX_LIGHTS];
    BOOL light_enabled[D3D7_MAX_LIGHTS];
    float clip_planes[D3D7_MAX_CLIP_PLANES][4];
    D3DVIEWPORT7 viewport;
    D3DMATERIAL7 material;
};

struct D3D7Device {
    const IDirect3DDevice7Vtbl *vtbl;
    const IDirect3DDevice3Vtbl *device3_vtbl;
    const IDirect3DDevice2Vtbl *device2_vtbl;
    const IDirect3DDeviceVtbl *device1_vtbl;
    LONG ref;
    DDrawObject *owner;
    DDSurface *render_target;
    BOOL in_scene;
    D3DVIEWPORT7 viewport;
    D3DMATERIAL7 material;
    D3DMATRIX transforms[D3D7_MAX_TRANSFORMS];
    DWORD render_states[D3D7_MAX_RENDER_STATES];
    DWORD texture_stage_states[D3D7_MAX_TEXTURE_STAGES]
            [D3D7_MAX_TEXTURE_STAGE_STATES];
    DDSurface *textures[D3D7_MAX_TEXTURE_STAGES];
    D3DLIGHT7 lights[D3D7_MAX_LIGHTS];
    BOOL light_set[D3D7_MAX_LIGHTS];
    BOOL light_enabled[D3D7_MAX_LIGHTS];
    float clip_planes[D3D7_MAX_CLIP_PLANES][4];
    D3DCLIPSTATUS clip_status;
    D3D7StateBlock *state_blocks;
    D3D7StateBlock *recording_state_block;
    DWORD next_state_block_handle;
    D3DLegacyViewport *legacy_viewports;
    D3DLegacyViewport *legacy_current_viewport;
    D3DLegacyMatrix *legacy_matrices;
    BYTE *legacy_vertices;
    WORD *legacy_indices;
    DWORD legacy_vertex_count;
    DWORD legacy_vertex_capacity;
    DWORD legacy_index_count;
    DWORD legacy_index_capacity;
    DWORD legacy_fvf;
    D3DPRIMITIVETYPE legacy_primitive_type;
    BOOL legacy_in_begin;
    D3DSTATS legacy_stats;
    D3DMATERIALHANDLE legacy_material_handle;
    D3DLegacyMaterial *legacy_material;
    UINT legacy_vertex_stride;
    D3DPICKRECORD *legacy_pick_records;
    DWORD legacy_pick_record_count;
    DWORD legacy_pick_record_capacity;
};

struct D3D7VertexBuffer {
    const IDirect3DVertexBuffer7Vtbl *vtbl;
    const IDirect3DVertexBufferVtbl *legacy_vtbl;
    LONG ref;
    DDrawObject *owner;
    uint32_t handle;
    D3DVERTEXBUFFERDESC desc;
    UINT stride;
    UINT byte_count;
    BYTE *shadow;
    BOOL locked;
    DWORD lock_flags;
};

static IDirect3DDevice7Vtbl g_d3d7_device_vtbl;
static IDirect3DVertexBuffer7Vtbl g_d3d7_vb_vtbl;
static IDirect3DDevice3Vtbl g_d3d3_device_vtbl;
static IDirect3DDevice2Vtbl g_d3d2_device_vtbl;
static IDirect3DDeviceVtbl g_d3d1_device_vtbl;
static IDirect3DVertexBufferVtbl g_d3d_vb_vtbl;
static BOOL d3d7_sync_initial_state(D3D7Device *device);
static float d3d7_sqrt(float value);
static void d3d_legacy_device_cleanup(D3D7Device *device);

static DDrawObject *d3d7_object_from_iface(IDirect3D7 *iface)
{
    return (DDrawObject *)((BYTE *)iface - offsetof(DDrawObject, d3d7_vtbl));
}

static D3D7Device *d3d7_device_from_iface(IDirect3DDevice7 *iface)
{
    return (D3D7Device *)iface;
}

static D3D7VertexBuffer *d3d7_vb_from_iface(IDirect3DVertexBuffer7 *iface)
{
    return (D3D7VertexBuffer *)iface;
}

static BOOL d3d7_multiply_u32(UINT left, UINT right, UINT *out)
{
    if (left && right > 0xffffffffu / left) return FALSE;
    *out = left * right;
    return TRUE;
}

static BOOL d3d7_primitive_counts(D3DPRIMITIVETYPE type, UINT element_count,
        UINT *primitive_count)
{
    switch (type) {
    case D3DPT_POINTLIST:
        *primitive_count = element_count;
        return element_count != 0;
    case D3DPT_LINELIST:
        *primitive_count = element_count / 2u;
        return *primitive_count != 0;
    case D3DPT_LINESTRIP:
        if (element_count < 2u) return FALSE;
        *primitive_count = element_count - 1u;
        return TRUE;
    case D3DPT_TRIANGLELIST:
        *primitive_count = element_count / 3u;
        return *primitive_count != 0;
    case D3DPT_TRIANGLESTRIP:
    case D3DPT_TRIANGLEFAN:
        if (element_count < 3u) return FALSE;
        *primitive_count = element_count - 2u;
        return TRUE;
    default:
        return FALSE;
    }
}

static BOOL d3d7_decode_fvf(DWORD fvf, D3D7FVFLayout *layout,
        D9WGVertexElement *elements, UINT *element_count)
{
    static const BYTE tex_types[4] = {
        D3D9DECLTYPE_FLOAT2, D3D9DECLTYPE_FLOAT3,
        D3D9DECLTYPE_FLOAT4, D3D9DECLTYPE_FLOAT1
    };
    static const UINT tex_bytes[4] = { 8u, 12u, 16u, 4u };
    DWORD position = fvf & DDWG_FVF_POSITION_MASK_D3D7;
    UINT offset = 0;
    UINT count = 0;
    UINT index;

    ZeroMemory(layout, sizeof(*layout));
    if (fvf & DDWG_FVF_RESERVED0) return FALSE;

#define D3D7_PUSH_ELEMENT(kind, use, use_index) do { \
    if (!elements || count >= D3D7_MAX_VERTEX_ELEMENTS || offset > 0xffffu) \
        return FALSE; \
    elements[count].stream = 0; \
    elements[count].offset = (uint16_t)offset; \
    elements[count].type = (uint8_t)(kind); \
    elements[count].method = D3D9DECLMETHOD_DEFAULT; \
    elements[count].usage = (uint8_t)(use); \
    elements[count].usage_index = (uint8_t)(use_index); \
    ++count; \
} while (0)

    layout->position_offset = offset;
    if (position == DDWG_FVF_XYZ) {
        layout->has_position = TRUE;
        layout->position_bytes = 12u;
        D3D7_PUSH_ELEMENT(D3D9DECLTYPE_FLOAT3, D3D9DECLUSAGE_POSITION, 0);
        offset += 12u;
    } else if (position == DDWG_FVF_XYZRHW) {
        layout->has_position = TRUE;
        layout->position_bytes = 16u;
        D3D7_PUSH_ELEMENT(D3D9DECLTYPE_FLOAT4, D3D9DECLUSAGE_POSITIONT, 0);
        offset += 16u;
    /* The advertised four-matrix vertex-blend limit covers XYZB1..XYZB4.
     * D3D7's XYZB5 is five float weights (not D3D8's indexed LASTBETA form),
     * so accepting it as blend indices would corrupt the following fields. */
    } else if (position >= DDWG_FVF_XYZB1 && position <= DDWG_FVF_XYZB4) {
        UINT blend_dwords = 1u + ((position - DDWG_FVF_XYZB1) >> 1);
        UINT weight_count = blend_dwords;
        layout->has_position = TRUE;
        layout->position_bytes = 12u + blend_dwords * 4u;
        D3D7_PUSH_ELEMENT(D3D9DECLTYPE_FLOAT3, D3D9DECLUSAGE_POSITION, 0);
        offset += 12u;
        if (weight_count) {
            D3D7_PUSH_ELEMENT(D3D9DECLTYPE_FLOAT1 + weight_count - 1u,
                    D3D9DECLUSAGE_BLENDWEIGHT, 0);
            offset += weight_count * 4u;
        }
    } else {
        return FALSE;
    }
    if (fvf & DDWG_FVF_NORMAL) {
        layout->has_normal = TRUE;
        layout->normal_offset = offset;
        D3D7_PUSH_ELEMENT(D3D9DECLTYPE_FLOAT3, D3D9DECLUSAGE_NORMAL, 0);
        offset += 12u;
    }
    /* D3DFVF_RESERVED1 is a real reserved DWORD in D3DLVERTEX. It must stay
     * in the stride even though it is deliberately absent from the host FVF. */
    if (fvf & DDWG_FVF_RESERVED1_D3D7) offset += 4u;
    if (fvf & DDWG_FVF_DIFFUSE) {
        layout->has_diffuse = TRUE;
        layout->diffuse_offset = offset;
        D3D7_PUSH_ELEMENT(D3D9DECLTYPE_D3DCOLOR, D3D9DECLUSAGE_COLOR, 0);
        offset += 4u;
    }
    if (fvf & DDWG_FVF_SPECULAR) {
        layout->has_specular = TRUE;
        layout->specular_offset = offset;
        D3D7_PUSH_ELEMENT(D3D9DECLTYPE_D3DCOLOR, D3D9DECLUSAGE_COLOR, 1);
        offset += 4u;
    }
    layout->texcoord_count = (fvf & DDWG_FVF_TEXCOUNT_MASK)
            >> DDWG_FVF_TEXCOUNT_SHIFT;
    if (layout->texcoord_count > 8u) return FALSE;
    for (index = 0; index < layout->texcoord_count; ++index) {
        UINT size_code = (fvf >> (16u + index * 2u)) & 3u;
        layout->texcoord_offset[index] = offset;
        layout->texcoord_bytes[index] = tex_bytes[size_code];
        D3D7_PUSH_ELEMENT(tex_types[size_code], D3D9DECLUSAGE_TEXCOORD,
                index);
        offset += tex_bytes[size_code];
    }
#undef D3D7_PUSH_ELEMENT

    layout->stride = offset;
    *element_count = count;
    return count != 0 && offset != 0;
}

static BOOL d3d7_emit_fvf(D3D7Device *device, DWORD fvf, UINT *stride_out)
{
    D3D7FVFLayout layout;
    D9WGVertexElement elements[D3D7_MAX_VERTEX_ELEMENTS];
    D9WGSetFVF command;
    UINT count = 0;
    UINT bytes;
    uint8_t *payload;
    uint8_t *blob;
    BOOL result;

    if (!d3d7_decode_fvf(fvf, &layout, elements, &count)) return FALSE;
    bytes = count * sizeof(elements[0]);
    ZeroMemory(&command, sizeof(command));
    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_SET_FVF, sizeof(command), bytes,
            &payload, &blob);
    if (result) {
        command.device_handle = device->owner->device_handle;
        command.fvf = ddraw_fvf_to_d3d9(fvf);
        command.element_count = count;
        CopyMemory(payload, &command, sizeof(command));
        CopyMemory(blob, elements, bytes);
    }
    LeaveCriticalSection(&g_transport_lock);
    if (stride_out) *stride_out = layout.stride;
    return result;
}

static BOOL d3d7_emit_draw_up(D3D7Device *device,
        D3DPRIMITIVETYPE primitive_type, DWORD fvf, const void *vertices,
        DWORD vertex_count)
{
    D9WGDrawPrimitiveUP command;
    UINT primitive_count;
    UINT stride;
    uint8_t *payload;
    uint8_t *blob;
    BOOL result;

    if (!vertices || !d3d7_primitive_counts(primitive_type, vertex_count,
            &primitive_count) || !d3d7_emit_fvf(device, fvf, &stride))
        return FALSE;
    ZeroMemory(&command, sizeof(command));
    if (!d3d7_multiply_u32(vertex_count, stride, &command.vertex_bytes))
        return FALSE;
    command.device_handle = device->owner->device_handle;
    command.primitive_type = primitive_type;
    command.primitive_count = primitive_count;
    command.stride = stride;
    command.vertex_count = vertex_count;
    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_DRAW_PRIMITIVE_UP,
            sizeof(command), command.vertex_bytes, &payload, &blob);
    if (result) {
        command.vertex_data_offset = (uint32_t)(blob - batch_base());
        CopyMemory(payload, &command, sizeof(command));
        CopyMemory(blob, vertices, command.vertex_bytes);
    }
    LeaveCriticalSection(&g_transport_lock);
    if (result && device->render_target)
        device->render_target->memory->gpu_dirty = TRUE;
    return result;
}

static BOOL d3d7_emit_draw_indexed_up(D3D7Device *device,
        D3DPRIMITIVETYPE primitive_type, DWORD fvf, const void *vertices,
        DWORD vertex_count, const WORD *indices, DWORD index_count)
{
    D9WGDrawIndexedPrimitiveUP command;
    UINT primitive_count;
    UINT stride;
    UINT extra_bytes;
    uint8_t *payload;
    uint8_t *blob;
    BOOL result;

    if (!vertices || !indices || !vertex_count ||
            !d3d7_primitive_counts(primitive_type, index_count,
                    &primitive_count) ||
            !d3d7_emit_fvf(device, fvf, &stride))
        return FALSE;
    ZeroMemory(&command, sizeof(command));
    if (!d3d7_multiply_u32(index_count, sizeof(WORD), &command.index_bytes) ||
            !d3d7_multiply_u32(vertex_count, stride, &command.vertex_bytes) ||
            command.index_bytes > 0xffffffffu - command.vertex_bytes)
        return FALSE;
    extra_bytes = command.index_bytes + command.vertex_bytes;
    command.device_handle = device->owner->device_handle;
    command.primitive_type = primitive_type;
    command.vertex_count = vertex_count;
    command.primitive_count = primitive_count;
    command.index_format = 101u; /* D3DFMT_INDEX16 */
    command.stride = stride;
    command.index_count = index_count;
    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_DRAW_INDEXED_PRIMITIVE_UP,
            sizeof(command), extra_bytes, &payload, &blob);
    if (result) {
        command.index_data_offset = (uint32_t)(blob - batch_base());
        command.vertex_data_offset = command.index_data_offset +
                command.index_bytes;
        CopyMemory(payload, &command, sizeof(command));
        CopyMemory(blob, indices, command.index_bytes);
        CopyMemory(blob + command.index_bytes, vertices, command.vertex_bytes);
    }
    LeaveCriticalSection(&g_transport_lock);
    if (result && device->render_target)
        device->render_target->memory->gpu_dirty = TRUE;
    return result;
}

static void d3d7_identity_matrix(D3DMATRIX *matrix)
{
    ZeroMemory(matrix, sizeof(*matrix));
    matrix->_11 = matrix->_22 = 1.0f;
    matrix->_33 = matrix->_44 = 1.0f;
}

static void d3d7_multiply_matrix(const D3DMATRIX *left,
        const D3DMATRIX *right, D3DMATRIX *out)
{
    D3DMATRIX result;
    UINT row, column;
    for (row = 0; row < 4u; ++row) {
        for (column = 0; column < 4u; ++column) {
            (&result._11)[row * 4u + column] =
                    (&left->_11)[row * 4u + 0u] *
                        (&right->_11)[0u * 4u + column] +
                    (&left->_11)[row * 4u + 1u] *
                        (&right->_11)[1u * 4u + column] +
                    (&left->_11)[row * 4u + 2u] *
                        (&right->_11)[2u * 4u + column] +
                    (&left->_11)[row * 4u + 3u] *
                        (&right->_11)[3u * 4u + column];
        }
    }
    *out = result;
}

static void d3d7_fill_prim_caps(D3DPRIMCAPS *caps)
{
    ZeroMemory(caps, sizeof(*caps));
    caps->dwSize = sizeof(*caps);
    caps->dwMiscCaps = D3DPMISCCAPS_MASKZ | D3DPMISCCAPS_CULLNONE |
            D3DPMISCCAPS_CULLCW | D3DPMISCCAPS_CULLCCW;
    caps->dwRasterCaps = D3DPRASTERCAPS_ZTEST | D3DPRASTERCAPS_FOGVERTEX |
            D3DPRASTERCAPS_FOGTABLE | D3DPRASTERCAPS_MIPMAPLODBIAS |
            D3DPRASTERCAPS_ZBIAS | D3DPRASTERCAPS_FOGRANGE |
            D3DPRASTERCAPS_ANISOTROPY | D3DPRASTERCAPS_WFOG;
    caps->dwZCmpCaps = caps->dwAlphaCmpCaps = 0xffu;
    caps->dwSrcBlendCaps = caps->dwDestBlendCaps = 0x1fffu;
    caps->dwShadeCaps = D3DPSHADECAPS_COLORFLATRGB |
            D3DPSHADECAPS_COLORGOURAUDRGB |
            D3DPSHADECAPS_SPECULARGOURAUDRGB |
            D3DPSHADECAPS_ALPHAFLATBLEND |
            D3DPSHADECAPS_ALPHAGOURAUDBLEND | D3DPSHADECAPS_FOGGOURAUD;
    caps->dwTextureCaps = D3DPTEXTURECAPS_PERSPECTIVE |
            D3DPTEXTURECAPS_ALPHA | D3DPTEXTURECAPS_ALPHAPALETTE |
            D3DPTEXTURECAPS_TRANSPARENCY | D3DPTEXTURECAPS_PROJECTED |
            D3DPTEXTURECAPS_CUBEMAP | D3DPTEXTURECAPS_COLORKEYBLEND;
    caps->dwTextureFilterCaps = D3DPTFILTERCAPS_NEAREST |
            D3DPTFILTERCAPS_LINEAR | D3DPTFILTERCAPS_MIPNEAREST |
            D3DPTFILTERCAPS_MIPLINEAR |
            D3DPTFILTERCAPS_LINEARMIPNEAREST |
            D3DPTFILTERCAPS_LINEARMIPLINEAR |
            D3DPTFILTERCAPS_MINFPOINT |
            D3DPTFILTERCAPS_MINFLINEAR | D3DPTFILTERCAPS_MAGFPOINT |
            D3DPTFILTERCAPS_MAGFLINEAR | D3DPTFILTERCAPS_MINFANISOTROPIC |
            D3DPTFILTERCAPS_MAGFANISOTROPIC |
            D3DPTFILTERCAPS_MIPFPOINT | D3DPTFILTERCAPS_MIPFLINEAR;
    caps->dwTextureBlendCaps = D3DPTBLENDCAPS_DECAL |
            D3DPTBLENDCAPS_MODULATE | D3DPTBLENDCAPS_DECALALPHA |
            D3DPTBLENDCAPS_MODULATEALPHA | D3DPTBLENDCAPS_COPY |
            D3DPTBLENDCAPS_ADD;
    caps->dwTextureAddressCaps = D3DPTADDRESSCAPS_WRAP |
            D3DPTADDRESSCAPS_MIRROR | D3DPTADDRESSCAPS_CLAMP |
            D3DPTADDRESSCAPS_BORDER | D3DPTADDRESSCAPS_INDEPENDENTUV;
}

static void d3d7_fill_caps(D3DDEVICEDESC7 *caps)
{
    ZeroMemory(caps, sizeof(*caps));
    caps->dwDevCaps = D3DDEVCAPS_HWRASTERIZATION |
            D3DDEVCAPS_HWTRANSFORMANDLIGHT | D3DDEVCAPS_DRAWPRIMTLVERTEX |
            D3DDEVCAPS_TEXTURESYSTEMMEMORY | D3DDEVCAPS_TEXTUREVIDEOMEMORY |
            D3DDEVCAPS_TLVERTEXSYSTEMMEMORY | D3DDEVCAPS_TLVERTEXVIDEOMEMORY |
            D3DDEVCAPS_CANRENDERAFTERFLIP | D3DDEVCAPS_DRAWPRIMITIVES2 |
            D3DDEVCAPS_DRAWPRIMITIVES2EX;
    d3d7_fill_prim_caps(&caps->dpcLineCaps);
    d3d7_fill_prim_caps(&caps->dpcTriCaps);
    caps->dwDeviceRenderBitDepth = DDBD_16 | DDBD_24 | DDBD_32;
    caps->dwDeviceZBufferBitDepth = DDBD_16 | DDBD_24 | DDBD_32;
    caps->dwMinTextureWidth = caps->dwMinTextureHeight = 1u;
    caps->dwMaxTextureWidth = caps->dwMaxTextureHeight = 8192u;
    caps->dwMaxTextureRepeat = caps->dwMaxTextureAspectRatio = 8192u;
    caps->dwMaxAnisotropy = 16u;
    caps->dvGuardBandLeft = caps->dvGuardBandTop = -32768.0f;
    caps->dvGuardBandRight = caps->dvGuardBandBottom = 32768.0f;
    caps->dwStencilCaps = D3DSTENCILCAPS_KEEP | D3DSTENCILCAPS_ZERO |
            D3DSTENCILCAPS_REPLACE | D3DSTENCILCAPS_INCRSAT |
            D3DSTENCILCAPS_DECRSAT | D3DSTENCILCAPS_INVERT |
            D3DSTENCILCAPS_INCR | D3DSTENCILCAPS_DECR;
    caps->dwFVFCaps = 8u | D3DFVFCAPS_DONOTSTRIPELEMENTS;
    caps->dwTextureOpCaps = D3DTEXOPCAPS_DISABLE |
            D3DTEXOPCAPS_SELECTARG1 | D3DTEXOPCAPS_SELECTARG2 |
            D3DTEXOPCAPS_MODULATE | D3DTEXOPCAPS_MODULATE2X |
            D3DTEXOPCAPS_MODULATE4X | D3DTEXOPCAPS_ADD |
            D3DTEXOPCAPS_ADDSIGNED | D3DTEXOPCAPS_ADDSIGNED2X |
            D3DTEXOPCAPS_SUBTRACT | D3DTEXOPCAPS_ADDSMOOTH |
            D3DTEXOPCAPS_BLENDDIFFUSEALPHA |
            D3DTEXOPCAPS_BLENDTEXTUREALPHA |
            D3DTEXOPCAPS_BLENDFACTORALPHA |
            D3DTEXOPCAPS_BLENDTEXTUREALPHAPM |
            D3DTEXOPCAPS_BLENDCURRENTALPHA | D3DTEXOPCAPS_DOTPRODUCT3;
    caps->wMaxTextureBlendStages = D3D7_MAX_TEXTURE_STAGES;
    caps->wMaxSimultaneousTextures = D3D7_MAX_TEXTURE_STAGES;
    caps->dwMaxActiveLights = D3D7_MAX_LIGHTS;
    caps->dvMaxVertexW = 1.0e10f;
    caps->deviceGUID = IID_IDirect3DTnLHalDevice;
    caps->wMaxUserClipPlanes = D3D7_MAX_CLIP_PLANES;
    caps->wMaxVertexBlendMatrices = 4u;
    caps->dwVertexProcessingCaps = D3DVTXPCAPS_TEXGEN |
            D3DVTXPCAPS_MATERIALSOURCE7 | D3DVTXPCAPS_VERTEXFOG |
            D3DVTXPCAPS_DIRECTIONALLIGHTS | D3DVTXPCAPS_POSITIONALLIGHTS |
            D3DVTXPCAPS_LOCALVIEWER;
}

static void d3d7_make_rgb_format(DDPIXELFORMAT *format, DWORD bits,
        DWORD red, DWORD green, DWORD blue, DWORD alpha)
{
    ZeroMemory(format, sizeof(*format));
    format->dwSize = sizeof(*format);
    format->dwFlags = DDPF_RGB | (alpha ? DDPF_ALPHAPIXELS : 0u);
    format->dwRGBBitCount = bits;
    format->dwRBitMask = red;
    format->dwGBitMask = green;
    format->dwBBitMask = blue;
    format->dwRGBAlphaBitMask = alpha;
}

static HRESULT d3d7_enum_texture_formats(LPD3DENUMPIXELFORMATSCALLBACK callback,
        void *context, BOOL depth_only)
{
    DDPIXELFORMAT format;
    UINT index;
    static const struct {
        DWORD bits, red, green, blue, alpha;
    } colors[] = {
        { 16u, 0xf800u, 0x07e0u, 0x001fu, 0u },
        { 16u, 0x7c00u, 0x03e0u, 0x001fu, 0u },
        { 16u, 0x7c00u, 0x03e0u, 0x001fu, 0x8000u },
        { 16u, 0x0f00u, 0x00f0u, 0x000fu, 0xf000u },
        { 24u, 0xff0000u, 0x00ff00u, 0x0000ffu, 0u },
        { 32u, 0xff0000u, 0x00ff00u, 0x0000ffu, 0u },
        { 32u, 0xff0000u, 0x00ff00u, 0x0000ffu, 0xff000000u },
    };

    if (!callback) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (depth_only) {
        static const struct { DWORD depth, stencil; } depths[] = {
            { 16u, 0u }, { 24u, 0u }, { 24u, 8u }, { 32u, 0u }
        };
        for (index = 0; index < sizeof(depths) / sizeof(depths[0]); ++index) {
            ZeroMemory(&format, sizeof(format));
            format.dwSize = sizeof(format);
            format.dwFlags = DDPF_ZBUFFER |
                    (depths[index].stencil ? DDPF_STENCILBUFFER : 0u);
            format.dwZBufferBitDepth = depths[index].depth;
            format.dwStencilBitDepth = depths[index].stencil;
            if (callback(&format, context) == D3DENUMRET_CANCEL) break;
        }
        return D3D_OK;
    }
    for (index = 0; index < sizeof(colors) / sizeof(colors[0]); ++index) {
        d3d7_make_rgb_format(&format, colors[index].bits, colors[index].red,
                colors[index].green, colors[index].blue, colors[index].alpha);
        if (callback(&format, context) == D3DENUMRET_CANCEL) return D3D_OK;
    }
    ZeroMemory(&format, sizeof(format));
    format.dwSize = sizeof(format);
    format.dwFlags = DDPF_RGB | DDPF_PALETTEINDEXED8;
    format.dwRGBBitCount = 8u;
    if (callback(&format, context) == D3DENUMRET_CANCEL) return D3D_OK;
    {
        static const DWORD compressed[] = {
            DDWG_FMT_DXT1, DDWG_FMT_DXT2, DDWG_FMT_DXT3,
            DDWG_FMT_DXT4, DDWG_FMT_DXT5,
        };
        for (index = 0; index < sizeof(compressed) / sizeof(compressed[0]);
                ++index) {
            ZeroMemory(&format, sizeof(format));
            format.dwSize = sizeof(format);
            format.dwFlags = DDPF_FOURCC;
            format.dwFourCC = compressed[index];
            if (callback(&format, context) == D3DENUMRET_CANCEL) break;
        }
    }
    return D3D_OK;
}

/* ------------------------------------------------------------------ *
 * IDirect3D7 factory
 * ------------------------------------------------------------------ */

static HRESULT WINAPI d3d7_QueryInterface(IDirect3D7 *iface, REFIID iid,
        void **out)
{
    DDrawObject *object = d3d7_object_from_iface(iface);
    /* IDirect3D7 is another interface on the DirectDraw object. Delegating
     * preserves COM's symmetric QI set and canonical IUnknown identity. */
    return ddraw_QueryInterface((IDirectDraw7 *)&object->vtbl7, iid, out);
}

static ULONG WINAPI d3d7_AddRef(IDirect3D7 *iface)
{
    return ddraw_AddRef((IDirectDraw7 *)&d3d7_object_from_iface(iface)->vtbl7);
}

static ULONG WINAPI d3d7_Release(IDirect3D7 *iface)
{
    return ddraw_Release((IDirectDraw7 *)&d3d7_object_from_iface(iface)->vtbl7);
}

static HRESULT WINAPI d3d7_EnumDevices(IDirect3D7 *iface,
        LPD3DENUMDEVICESCALLBACK7 callback, void *context)
{
    D3DDEVICEDESC7 caps;
    (void)iface;
    if (!callback) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    d3d7_fill_caps(&caps);
    callback((char *)"v86 WebGPU T&L HAL",
            (char *)"v86 WebGPU Direct3D 7", &caps, context);
    return D3D_OK;
}

static void d3d7_device_init_state(D3D7Device *device)
{
    UINT index, stage;
    for (index = 0; index < D3D7_MAX_TRANSFORMS; ++index)
        d3d7_identity_matrix(&device->transforms[index]);
    device->viewport.dwX = device->viewport.dwY = 0;
    device->viewport.dwWidth = device->render_target->width;
    device->viewport.dwHeight = device->render_target->height;
    device->viewport.dvMinZ = 0.0f;
    device->viewport.dvMaxZ = 1.0f;
    device->material.diffuse.r = device->material.diffuse.g =
            device->material.diffuse.b = device->material.diffuse.a = 1.0f;
    device->material.ambient = device->material.diffuse;
    device->render_states[D3DRENDERSTATE_ANTIALIAS] = D3DANTIALIAS_NONE;
    device->render_states[D3DRENDERSTATE_TEXTUREADDRESS] = D3DTADDRESS_WRAP;
    device->render_states[D3DRENDERSTATE_TEXTUREPERSPECTIVE] = TRUE;
    device->render_states[D3DRENDERSTATE_ZENABLE] =
            device->render_target->attached_depth ? D3DZB_TRUE : D3DZB_FALSE;
    device->render_states[D3DRENDERSTATE_FILLMODE] = D3DFILL_SOLID;
    device->render_states[D3DRENDERSTATE_ZWRITEENABLE] = TRUE;
    device->render_states[D3DRENDERSTATE_ZFUNC] = D3DCMP_LESSEQUAL;
    device->render_states[D3DRENDERSTATE_CULLMODE] = D3DCULL_CCW;
    device->render_states[D3DRENDERSTATE_SHADEMODE] = D3DSHADE_GOURAUD;
    device->render_states[D3DRENDERSTATE_LASTPIXEL] = TRUE;
    device->render_states[D3DRENDERSTATE_TEXTUREMAG] = D3D7FILTER_NEAREST;
    device->render_states[D3DRENDERSTATE_TEXTUREMIN] = D3D7FILTER_NEAREST;
    device->render_states[D3DRENDERSTATE_TEXTUREMAPBLEND] =
            D3D7TBLEND_MODULATE;
    device->render_states[D3DRENDERSTATE_ALPHAFUNC] = D3DCMP_ALWAYS;
    device->render_states[D3DRENDERSTATE_SRCBLEND] = D3DBLEND_ONE;
    device->render_states[D3DRENDERSTATE_DESTBLEND] = D3DBLEND_ZERO;
    device->render_states[D3DRENDERSTATE_TEXTUREFACTOR] = 0xffffffffu;
    device->render_states[D3DRENDERSTATE_FOGEND] = 0x3f800000u;
    device->render_states[D3DRENDERSTATE_FOGDENSITY] = 0x3f800000u;
    device->render_states[D3DRENDERSTATE_STENCILFAIL] = D3DSTENCILOP_KEEP;
    device->render_states[D3DRENDERSTATE_STENCILZFAIL] = D3DSTENCILOP_KEEP;
    device->render_states[D3DRENDERSTATE_STENCILPASS] = D3DSTENCILOP_KEEP;
    device->render_states[D3DRENDERSTATE_STENCILFUNC] = D3DCMP_ALWAYS;
    device->render_states[D3DRENDERSTATE_STENCILMASK] = 0xffffffffu;
    device->render_states[D3DRENDERSTATE_STENCILWRITEMASK] = 0xffffffffu;
    device->render_states[D3DRENDERSTATE_CLIPPING] = TRUE;
    device->render_states[D3DRENDERSTATE_LIGHTING] = TRUE;
    device->render_states[D3DRENDERSTATE_COLORVERTEX] = TRUE;
    device->render_states[D3DRENDERSTATE_LOCALVIEWER] = TRUE;
    device->render_states[D3DRENDERSTATE_DIFFUSEMATERIALSOURCE] = D3DMCS_COLOR1;
    device->render_states[D3DRENDERSTATE_SPECULARMATERIALSOURCE] = D3DMCS_COLOR2;
    device->render_states[D3DRENDERSTATE_AMBIENTMATERIALSOURCE] = D3DMCS_MATERIAL;
    device->render_states[D3DRENDERSTATE_EMISSIVEMATERIALSOURCE] = D3DMCS_MATERIAL;
    for (stage = 0; stage < D3D7_MAX_TEXTURE_STAGES; ++stage) {
        device->texture_stage_states[stage][D3DTSS_COLOROP] =
                stage ? D3DTOP_DISABLE : D3DTOP_MODULATE;
        device->texture_stage_states[stage][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
        device->texture_stage_states[stage][D3DTSS_COLORARG2] = D3DTA_CURRENT;
        device->texture_stage_states[stage][D3DTSS_ALPHAOP] =
                stage ? D3DTOP_DISABLE : D3DTOP_SELECTARG1;
        device->texture_stage_states[stage][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
        device->texture_stage_states[stage][D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
        device->texture_stage_states[stage][D3DTSS_TEXCOORDINDEX] = stage;
        device->texture_stage_states[stage][D3DTSS_ADDRESS] = D3DTADDRESS_WRAP;
        device->texture_stage_states[stage][D3DTSS_ADDRESSU] = D3DTADDRESS_WRAP;
        device->texture_stage_states[stage][D3DTSS_ADDRESSV] = D3DTADDRESS_WRAP;
        device->texture_stage_states[stage][D3DTSS_MAGFILTER] = D3DTFG_POINT;
        device->texture_stage_states[stage][D3DTSS_MINFILTER] = D3DTFN_POINT;
        device->texture_stage_states[stage][D3DTSS_MIPFILTER] = D3DTFP_NONE;
        device->texture_stage_states[stage][D3DTSS_MAXANISOTROPY] = 1u;
    }
    device->next_state_block_handle = 1u;
    device->legacy_stats.dwSize = sizeof(device->legacy_stats);
}

static BOOL d3d7_render_state_is_vertex(DWORD state)
{
    switch (state) {
    case D3DRENDERSTATE_FOGVERTEXMODE:
    case D3DRENDERSTATE_LIGHTING:
    case D3DRENDERSTATE_AMBIENT:
    case D3DRENDERSTATE_COLORVERTEX:
    case D3DRENDERSTATE_LOCALVIEWER:
    case D3DRENDERSTATE_NORMALIZENORMALS:
    case D3DRENDERSTATE_DIFFUSEMATERIALSOURCE:
    case D3DRENDERSTATE_SPECULARMATERIALSOURCE:
    case D3DRENDERSTATE_AMBIENTMATERIALSOURCE:
    case D3DRENDERSTATE_EMISSIVEMATERIALSOURCE:
    case D3DRENDERSTATE_VERTEXBLEND:
    case D3DRENDERSTATE_CLIPPLANEENABLE:
        return TRUE;
    default:
        return FALSE;
    }
}

static BOOL d3d7_render_state_valid(DWORD state)
{
    return (state >= 1u && state <= 41u) ||
            (state >= 43u && state <= 60u) ||
            (state >= 64u && state <= 95u) ||
            (state >= 128u && state <= 148u) || state == 151u || state == 152u;
}

static BOOL d3d7_transform_state_valid(DWORD state)
{
    return (state >= D3DTRANSFORMSTATE_WORLD &&
            state <= D3DTRANSFORMSTATE_WORLD3) ||
            (state >= D3DTRANSFORMSTATE_TEXTURE0 &&
             state <= D3DTRANSFORMSTATE_TEXTURE7);
}

static BOOL d3d7_texture_stage_state_valid(DWORD state)
{
    return state >= D3DTSS_COLOROP &&
            state <= D3DTSS_TEXTURETRANSFORMFLAGS;
}

static void d3d7_state_block_mark_type(D3D7StateBlock *block,
        D3DSTATEBLOCKTYPE type)
{
    UINT state, stage;
    if (type == D3DSBT_ALL || type == D3DSBT_PIXELSTATE) {
        for (state = 0; state < D3D7_MAX_RENDER_STATES; ++state) {
            if (d3d7_render_state_valid(state) &&
                    (type == D3DSBT_ALL ||
                     !d3d7_render_state_is_vertex(state)))
                block->render_mask[state] = 1;
        }
        for (stage = 0; stage < D3D7_MAX_TEXTURE_STAGES; ++stage) {
            block->texture_mask[stage] = 1;
            for (state = D3DTSS_COLOROP;
                    state <= D3DTSS_TEXTURETRANSFORMFLAGS; ++state)
                block->texture_stage_mask[stage][state] = 1;
        }
    }
    if (type == D3DSBT_ALL || type == D3DSBT_VERTEXSTATE) {
        for (state = 0; state < D3D7_MAX_RENDER_STATES; ++state) {
            if (d3d7_render_state_valid(state) &&
                    (type == D3DSBT_ALL ||
                     d3d7_render_state_is_vertex(state)))
                block->render_mask[state] = 1;
        }
        for (state = 0; state < D3D7_MAX_TRANSFORMS; ++state) {
            if (d3d7_transform_state_valid(state))
                block->transform_mask[state] = 1;
        }
        for (state = 0; state < D3D7_MAX_LIGHTS; ++state) {
            block->light_mask[state] = 1;
            block->light_enable_mask[state] = 1;
        }
        for (state = 0; state < D3D7_MAX_CLIP_PLANES; ++state)
            block->clip_plane_mask[state] = 1;
        block->viewport_mask = TRUE;
        block->material_mask = TRUE;
    }
}

static void d3d7_state_block_capture(D3D7Device *device,
        D3D7StateBlock *block)
{
    UINT state, stage;
    for (state = 0; state < D3D7_MAX_RENDER_STATES; ++state) {
        if (block->render_mask[state])
            block->render_states[state] = device->render_states[state];
    }
    for (stage = 0; stage < D3D7_MAX_TEXTURE_STAGES; ++stage) {
        if (block->texture_mask[stage]) {
            DDSurface *old = block->textures[stage];
            DDSurface *surface = device->textures[stage];
            if (surface) surface_AddRef((IDirectDrawSurface7 *)&surface->vtbl7);
            block->textures[stage] = surface;
            if (old) surface_Release((IDirectDrawSurface7 *)&old->vtbl7);
        }
        for (state = 0; state < D3D7_MAX_TEXTURE_STAGE_STATES; ++state) {
            if (block->texture_stage_mask[stage][state])
                block->texture_stage_states[stage][state] =
                        device->texture_stage_states[stage][state];
        }
    }
    for (state = 0; state < D3D7_MAX_TRANSFORMS; ++state) {
        if (block->transform_mask[state])
            block->transforms[state] = device->transforms[state];
    }
    for (state = 0; state < D3D7_MAX_LIGHTS; ++state) {
        if (block->light_mask[state]) {
            block->lights[state] = device->lights[state];
            block->light_set[state] = device->light_set[state];
        }
        if (block->light_enable_mask[state])
            block->light_enabled[state] = device->light_enabled[state];
    }
    for (state = 0; state < D3D7_MAX_CLIP_PLANES; ++state) {
        if (block->clip_plane_mask[state])
            CopyMemory(block->clip_planes[state], device->clip_planes[state],
                    sizeof(block->clip_planes[state]));
    }
    if (block->viewport_mask) block->viewport = device->viewport;
    if (block->material_mask) block->material = device->material;
}

static void d3d7_state_block_destroy(D3D7StateBlock *block)
{
    UINT stage;
    if (!block) return;
    for (stage = 0; stage < D3D7_MAX_TEXTURE_STAGES; ++stage) {
        if (block->textures[stage])
            surface_Release((IDirectDrawSurface7 *)&block->textures[stage]->vtbl7);
    }
    heap_free(block);
}

static D3D7StateBlock *d3d7_find_state_block(D3D7Device *device,
        DWORD handle)
{
    D3D7StateBlock *block;
    for (block = device->state_blocks; block; block = block->next) {
        if (block->handle == handle) return block;
    }
    return NULL;
}

static BOOL d3d7_emit_render_target(D3D7Device *device)
{
    D9WGSetRenderTarget color;
    D9WGSetDepthStencilSurfaceLevel depth;
    DDSurface *target = device->render_target;

    if (target && !prepare_surface_for_host(target))
        return FALSE;
    if (target && target->attached_depth &&
            !ensure_surface_storage(target->attached_depth))
        return FALSE;
    ZeroMemory(&color, sizeof(color));
    color.device_handle = device->owner->device_handle;
    color.color_texture_handle = target ? target->handle : 0u;
    color.color_level = target ? target->mip_level : 0u;
    color.color_face = target ? target->cube_face : 0u;
    if (!emit_command(D9WG_OP_SET_RENDER_TARGET, &color, sizeof(color)))
        return FALSE;
    ZeroMemory(&depth, sizeof(depth));
    depth.device_handle = device->owner->device_handle;
    if (target && target->attached_depth) {
        depth.depth_texture_handle = target->attached_depth->handle;
        depth.depth_level = target->attached_depth->mip_level;
        depth.width = target->attached_depth->width;
        depth.height = target->attached_depth->height;
    }
    return emit_command(D9WG_OP_SET_DEPTH_STENCIL_SURFACE_LEVEL, &depth,
            sizeof(depth));
}

static HRESULT WINAPI d3d7_CreateDevice(IDirect3D7 *iface, REFCLSID clsid,
        IDirectDrawSurface7 *surface_iface, IDirect3DDevice7 **out)
{
    DDrawObject *owner = d3d7_object_from_iface(iface);
    DDSurface *surface;
    D3D7Device *device;

    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = NULL;
    if (!surface_iface || surface_iface->lpVtbl != &g_surface_vtbl || !clsid)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (!guid_equal(clsid, &IID_IDirect3DHALDevice) &&
            !guid_equal(clsid, &IID_IDirect3DTnLHalDevice) &&
            !guid_equal(clsid, &IID_IDirect3DRGBDevice))
        return DDWG_TRACE_ERROR(D3DERR_INVALID_DEVICE);
    surface = surface_from_any(surface_iface, NULL);
    if (!surface || surface->owner != owner || format_is_depth(surface->format))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (owner->d3d7_device) return DDWG_TRACE_ERROR(D3DERR_INITFAILED);
    if (!ensure_device(owner)) return DDWG_TRACE_ERROR(D3DERR_INITFAILED);
    device = (D3D7Device *)heap_alloc_zero(sizeof(*device));
    if (!device) return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY);
    device->vtbl = &g_d3d7_device_vtbl;
    device->device3_vtbl = &g_d3d3_device_vtbl;
    device->device2_vtbl = &g_d3d2_device_vtbl;
    device->device1_vtbl = &g_d3d1_device_vtbl;
    device->ref = 1;
    device->owner = owner;
    device->render_target = surface;
    ddraw_AddRef((IDirectDraw7 *)&owner->vtbl7);
    surface_AddRef((IDirectDrawSurface7 *)&surface->vtbl7);
    d3d7_device_init_state(device);
    owner->d3d7_device = device;
    if (!d3d7_emit_render_target(device)) {
        owner->d3d7_device = NULL;
        surface_Release((IDirectDrawSurface7 *)&surface->vtbl7);
        ddraw_Release((IDirectDraw7 *)&owner->vtbl7);
        heap_free(device);
        return DDWG_TRACE_ERROR(D3DERR_INITFAILED);
    }
    if (!d3d7_sync_initial_state(device)) {
        owner->d3d7_device = NULL;
        surface_Release((IDirectDrawSurface7 *)&surface->vtbl7);
        ddraw_Release((IDirectDraw7 *)&owner->vtbl7);
        heap_free(device);
        return DDWG_TRACE_ERROR(D3DERR_INITFAILED);
    }
    *out = (IDirect3DDevice7 *)device;
    return D3D_OK;
}

static HRESULT WINAPI d3d7_CreateVertexBuffer(IDirect3D7 *iface,
        D3DVERTEXBUFFERDESC *description, IDirect3DVertexBuffer7 **out,
        DWORD flags)
{
    DDrawObject *owner = d3d7_object_from_iface(iface);
    D3D7VertexBuffer *buffer;
    D3D7FVFLayout layout;
    D9WGVertexElement elements[D3D7_MAX_VERTEX_ELEMENTS];
    D9WGCreateBuffer create;
    UINT count = 0;

    (void)flags;
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = NULL;
    if (!description || description->dwSize != sizeof(*description) ||
            !description->dwNumVertices ||
            !d3d7_decode_fvf(description->dwFVF, &layout, elements, &count))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    buffer = (D3D7VertexBuffer *)heap_alloc_zero(sizeof(*buffer));
    if (!buffer) return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY);
    if (!d3d7_multiply_u32(description->dwNumVertices, layout.stride,
            &buffer->byte_count)) {
        heap_free(buffer);
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    }
    buffer->shadow = (BYTE *)heap_alloc_zero(buffer->byte_count);
    if (!buffer->shadow) { heap_free(buffer); return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY); }
    buffer->vtbl = &g_d3d7_vb_vtbl;
    buffer->legacy_vtbl = &g_d3d_vb_vtbl;
    buffer->ref = 1;
    buffer->owner = owner;
    buffer->handle = allocate_handle();
    buffer->desc = *description;
    buffer->stride = layout.stride;
    ddraw_AddRef((IDirectDraw7 *)&owner->vtbl7);
    if (!ensure_device(owner)) {
        ddraw_Release((IDirectDraw7 *)&owner->vtbl7);
        heap_free(buffer->shadow);
        heap_free(buffer);
        return DDWG_TRACE_ERROR(D3DERR_VBUF_CREATE_FAILED);
    }
    ZeroMemory(&create, sizeof(create));
    create.device_handle = owner->device_handle;
    create.resource_handle = buffer->handle;
    create.resource_kind = D9WG_RESOURCE_BUFFER_VERTEX;
    create.byte_count = buffer->byte_count;
    create.fvf = ddraw_fvf_to_d3d9(description->dwFVF);
    create.pool = (description->dwCaps & D3DVBCAPS_SYSTEMMEMORY)
            ? DDWG_POOL_SYSTEMMEM : DDWG_POOL_DEFAULT;
    if (!emit_command(D9WG_OP_CREATE_BUFFER, &create, sizeof(create))) {
        ddraw_Release((IDirectDraw7 *)&owner->vtbl7);
        heap_free(buffer->shadow);
        heap_free(buffer);
        return DDWG_TRACE_ERROR(D3DERR_VBUF_CREATE_FAILED);
    }
    *out = (IDirect3DVertexBuffer7 *)buffer;
    return D3D_OK;
}

static HRESULT WINAPI d3d7_EnumZBufferFormats(IDirect3D7 *iface,
        REFCLSID device_iid, LPD3DENUMPIXELFORMATSCALLBACK callback,
        void *context)
{
    (void)iface;
    if (!device_iid || (!guid_equal(device_iid, &IID_IDirect3DHALDevice) &&
            !guid_equal(device_iid, &IID_IDirect3DTnLHalDevice) &&
            !guid_equal(device_iid, &IID_IDirect3DRGBDevice)))
        return DDWG_TRACE_ERROR(D3DERR_INVALID_DEVICE);
    return d3d7_enum_texture_formats(callback, context, TRUE);
}

static HRESULT WINAPI d3d7_EvictManagedTextures(IDirect3D7 *iface)
{
    (void)iface;
    /* All managed resources already have a canonical guest shadow. */
    return D3D_OK;
}

/* ------------------------------------------------------------------ *
 * IDirect3DDevice7 fixed-function state
 * ------------------------------------------------------------------ */

static HRESULT WINAPI d3d7_device_QueryInterface(IDirect3DDevice7 *iface,
        REFIID iid, void **out)
{
    D3D7Device *device = d3d7_device_from_iface(iface);
    if (!out) return DDWG_TRACE_ERROR(E_POINTER);
    *out = NULL;
    if (!iid) return DDWG_TRACE_ERROR(E_NOINTERFACE);
    if (iid_is_unknown(iid) || guid_equal(iid, &IID_IDirect3DDevice7))
        *out = &device->vtbl;
    else if (guid_equal(iid, &IID_IDirect3DDevice3))
        *out = &device->device3_vtbl;
    else if (guid_equal(iid, &IID_IDirect3DDevice2))
        *out = &device->device2_vtbl;
    else if (guid_equal(iid, &IID_IDirect3DDevice))
        *out = &device->device1_vtbl;
    else return DDWG_TRACE_ERROR(E_NOINTERFACE);
    IDirect3DDevice7_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI d3d7_device_AddRef(IDirect3DDevice7 *iface)
{
    return (ULONG)InterlockedIncrement(&d3d7_device_from_iface(iface)->ref);
}

static ULONG WINAPI d3d7_device_Release(IDirect3DDevice7 *iface)
{
    D3D7Device *device = d3d7_device_from_iface(iface);
    LONG remaining = InterlockedDecrement(&device->ref);
    if (remaining == 0) {
        DDrawObject *owner = device->owner;
        D3D7StateBlock *block;
        UINT stage;
        if (owner->d3d7_device == device) owner->d3d7_device = NULL;
        for (stage = 0; stage < D3D7_MAX_TEXTURE_STAGES; ++stage) {
            if (device->textures[stage])
                surface_Release((IDirectDrawSurface7 *)
                        &device->textures[stage]->vtbl7);
        }
        if (device->render_target)
            surface_Release((IDirectDrawSurface7 *)
                    &device->render_target->vtbl7);
        block = device->state_blocks;
        while (block) {
            D3D7StateBlock *next = block->next;
            d3d7_state_block_destroy(block);
            block = next;
        }
        d3d7_state_block_destroy(device->recording_state_block);
        d3d_legacy_device_cleanup(device);
        heap_free(device);
        ddraw_Release((IDirectDraw7 *)&owner->vtbl7);
    }
    return (ULONG)(remaining < 0 ? 0 : remaining);
}

static HRESULT WINAPI d3d7_device_GetCaps(IDirect3DDevice7 *iface,
        D3DDEVICEDESC7 *caps)
{
    (void)iface;
    if (!caps) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    d3d7_fill_caps(caps);
    return D3D_OK;
}

static HRESULT WINAPI d3d7_device_EnumTextureFormats(
        IDirect3DDevice7 *iface, LPD3DENUMPIXELFORMATSCALLBACK callback,
        void *context)
{
    (void)iface;
    return d3d7_enum_texture_formats(callback, context, FALSE);
}

static HRESULT WINAPI d3d7_device_BeginScene(IDirect3DDevice7 *iface)
{
    D3D7Device *device = d3d7_device_from_iface(iface);
    D9WGDeviceOnly command;
    if (device->in_scene) return DDWG_TRACE_ERROR(D3DERR_SCENE_IN_SCENE);
    device->in_scene = TRUE;
    command.device_handle = device->owner->device_handle;
    command.reserved = 0;
    if (!emit_command(D9WG_OP_BEGIN_SCENE, &command, sizeof(command))) {
        device->in_scene = FALSE;
        return DDWG_TRACE_ERROR(D3DERR_SCENE_BEGIN_FAILED);
    }
    return D3D_OK;
}

static HRESULT WINAPI d3d7_device_EndScene(IDirect3DDevice7 *iface)
{
    D3D7Device *device = d3d7_device_from_iface(iface);
    D9WGDeviceOnly command;
    if (!device->in_scene) return DDWG_TRACE_ERROR(D3DERR_SCENE_NOT_IN_SCENE);
    device->in_scene = FALSE;
    command.device_handle = device->owner->device_handle;
    command.reserved = 0;
    return emit_command(D9WG_OP_END_SCENE, &command, sizeof(command))
            ? D3D_OK : D3DERR_SCENE_END_FAILED;
}

static HRESULT WINAPI d3d7_device_GetDirect3D(IDirect3DDevice7 *iface,
        IDirect3D7 **out)
{
    D3D7Device *device = d3d7_device_from_iface(iface);
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = (IDirect3D7 *)&device->owner->d3d7_vtbl;
    d3d7_AddRef(*out);
    return D3D_OK;
}

static HRESULT WINAPI d3d7_device_SetRenderTarget(IDirect3DDevice7 *iface,
        IDirectDrawSurface7 *surface_iface, DWORD flags)
{
    D3D7Device *device = d3d7_device_from_iface(iface);
    DDSurface *surface;
    DDSurface *old;
    (void)flags;
    if (!surface_iface || surface_iface->lpVtbl != &g_surface_vtbl)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    surface = surface_from_any(surface_iface, NULL);
    if (!surface || surface->owner != device->owner ||
            format_is_depth(surface->format))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (surface == device->render_target) return D3D_OK;
    surface_AddRef((IDirectDrawSurface7 *)&surface->vtbl7);
    old = device->render_target;
    device->render_target = surface;
    if (!d3d7_emit_render_target(device)) {
        device->render_target = old;
        surface_Release((IDirectDrawSurface7 *)&surface->vtbl7);
        return DDWG_TRACE_ERROR(D3DERR_INITFAILED);
    }
    if (old) surface_Release((IDirectDrawSurface7 *)&old->vtbl7);
    return D3D_OK;
}

static HRESULT WINAPI d3d7_device_GetRenderTarget(IDirect3DDevice7 *iface,
        IDirectDrawSurface7 **out)
{
    DDSurface *surface = d3d7_device_from_iface(iface)->render_target;
    if (!out) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *out = surface ? (IDirectDrawSurface7 *)&surface->vtbl7 : NULL;
    if (surface) surface_AddRef(*out);
    return DDWG_TRACE_ERROR(surface ? D3D_OK : DDERR_NOTFOUND);
}

static HRESULT WINAPI d3d7_device_Clear(IDirect3DDevice7 *iface,
        DWORD rect_count, D3DRECT *rects, DWORD flags, D3DCOLOR color,
        D3DVALUE depth_value, DWORD stencil)
{
    D3D7Device *device = d3d7_device_from_iface(iface);
    D9WGClear command;
    UINT rect_bytes;
    uint8_t *payload;
    uint8_t *blob;
    BOOL result;
    if ((rect_count && !rects) ||
            !(flags & (D3DCLEAR_TARGET | D3DCLEAR_ZBUFFER |
                    D3DCLEAR_STENCIL)) ||
            !d3d7_multiply_u32(rect_count, sizeof(*rects), &rect_bytes))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if ((flags & D3DCLEAR_ZBUFFER) &&
            !device->render_target->attached_depth)
        return DDWG_TRACE_ERROR(D3DERR_ZBUFFER_NOTPRESENT);
    if ((flags & D3DCLEAR_STENCIL) &&
            (!device->render_target->attached_depth ||
             device->render_target->attached_depth->format != DDWG_FMT_D24S8))
        return DDWG_TRACE_ERROR(D3DERR_STENCILBUFFER_NOTPRESENT);
    ZeroMemory(&command, sizeof(command));
    command.device_handle = device->owner->device_handle;
    command.clear_flags = flags;
    command.color = color;
    command.depth = depth_value;
    command.stencil = stencil;
    command.rect_count = rect_count;
    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_CLEAR, sizeof(command), rect_bytes,
            &payload, &blob);
    if (result) {
        CopyMemory(payload, &command, sizeof(command));
        if (rect_bytes) CopyMemory(blob, rects, rect_bytes);
    }
    LeaveCriticalSection(&g_transport_lock);
    if (result && (flags & D3DCLEAR_TARGET))
        device->render_target->memory->gpu_dirty = TRUE;
    return DDWG_TRACE_ERROR(result ? D3D_OK : D3DERR_INITFAILED);
}

static HRESULT WINAPI d3d7_device_SetTransform(IDirect3DDevice7 *iface,
        D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix)
{
    D3D7Device *device = d3d7_device_from_iface(iface);
    D9WGSetTransform command;
    if (!matrix || !d3d7_transform_state_valid(state))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    ZeroMemory(&command, sizeof(command));
    command.device_handle = device->owner->device_handle;
    command.state = ddraw_transform_state_to_d3d9(state);
    CopyMemory(command.matrix, matrix, sizeof(command.matrix));
    if (!emit_command(D9WG_OP_SET_TRANSFORM, &command, sizeof(command)))
        return DDWG_TRACE_ERROR(D3DERR_INITFAILED);
    device->transforms[state] = *matrix;
    if (device->recording_state_block)
        device->recording_state_block->transform_mask[state] = 1;
    return D3D_OK;
}

static HRESULT WINAPI d3d7_device_GetTransform(IDirect3DDevice7 *iface,
        D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix)
{
    if (!matrix || !d3d7_transform_state_valid(state))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *matrix = d3d7_device_from_iface(iface)->transforms[state];
    return D3D_OK;
}

static HRESULT WINAPI d3d7_device_SetViewport(IDirect3DDevice7 *iface,
        D3DVIEWPORT7 *viewport)
{
    D3D7Device *device = d3d7_device_from_iface(iface);
    D9WGSetViewport command;
    if (!viewport || !viewport->dwWidth || !viewport->dwHeight ||
            viewport->dwX > device->render_target->width ||
            viewport->dwY > device->render_target->height ||
            viewport->dwWidth > device->render_target->width - viewport->dwX ||
            viewport->dwHeight > device->render_target->height - viewport->dwY ||
            viewport->dvMinZ < 0.0f || viewport->dvMaxZ > 1.0f ||
            viewport->dvMinZ > viewport->dvMaxZ)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    ZeroMemory(&command, sizeof(command));
    command.device_handle = device->owner->device_handle;
    command.x = viewport->dwX;
    command.y = viewport->dwY;
    command.width = viewport->dwWidth;
    command.height = viewport->dwHeight;
    command.min_z = viewport->dvMinZ;
    command.max_z = viewport->dvMaxZ;
    if (!emit_command(D9WG_OP_SET_VIEWPORT, &command, sizeof(command)))
        return DDWG_TRACE_ERROR(D3DERR_INITFAILED);
    device->viewport = *viewport;
    if (device->recording_state_block)
        device->recording_state_block->viewport_mask = TRUE;
    return D3D_OK;
}

static HRESULT WINAPI d3d7_device_MultiplyTransform(IDirect3DDevice7 *iface,
        D3DTRANSFORMSTATETYPE state, D3DMATRIX *matrix)
{
    D3D7Device *device = d3d7_device_from_iface(iface);
    D3DMATRIX result;
    if (!matrix || !d3d7_transform_state_valid(state))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    d3d7_multiply_matrix(&device->transforms[state], matrix, &result);
    return d3d7_device_SetTransform(iface, state, &result);
}

static HRESULT WINAPI d3d7_device_GetViewport(IDirect3DDevice7 *iface,
        D3DVIEWPORT7 *viewport)
{
    if (!viewport) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *viewport = d3d7_device_from_iface(iface)->viewport;
    return D3D_OK;
}

static HRESULT WINAPI d3d7_device_SetMaterial(IDirect3DDevice7 *iface,
        D3DMATERIAL7 *material)
{
    D3D7Device *device = d3d7_device_from_iface(iface);
    D9WGSetMaterial command;
    if (!material) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    ZeroMemory(&command, sizeof(command));
    command.device_handle = device->owner->device_handle;
    CopyMemory(command.diffuse, &material->diffuse, sizeof(command.diffuse));
    CopyMemory(command.ambient, &material->ambient, sizeof(command.ambient));
    CopyMemory(command.specular, &material->specular, sizeof(command.specular));
    CopyMemory(command.emissive, &material->emissive, sizeof(command.emissive));
    command.power = material->power;
    if (!emit_command(D9WG_OP_SET_MATERIAL, &command, sizeof(command)))
        return DDWG_TRACE_ERROR(D3DERR_MATERIAL_SETDATA_FAILED);
    device->material = *material;
    if (device->recording_state_block)
        device->recording_state_block->material_mask = TRUE;
    return D3D_OK;
}

static HRESULT WINAPI d3d7_device_GetMaterial(IDirect3DDevice7 *iface,
        D3DMATERIAL7 *material)
{
    if (!material) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *material = d3d7_device_from_iface(iface)->material;
    return D3D_OK;
}

static HRESULT WINAPI d3d7_device_SetLight(IDirect3DDevice7 *iface,
        DWORD index, D3DLIGHT7 *light)
{
    D3D7Device *device = d3d7_device_from_iface(iface);
    D9WGSetLight command;
    if (!light || index >= D3D7_MAX_LIGHTS) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (light->dltType < D3DLIGHT_POINT || light->dltType > D3DLIGHT_GLSPOT)
        return DDWG_TRACE_ERROR(D3DERR_LIGHT_SET_FAILED);
    ZeroMemory(&command, sizeof(command));
    command.device_handle = device->owner->device_handle;
    command.index = index;
    command.type = ddraw_light_type_to_d3d9(light->dltType);
    CopyMemory(command.diffuse, &light->dcvDiffuse, sizeof(command.diffuse));
    CopyMemory(command.specular, &light->dcvSpecular, sizeof(command.specular));
    CopyMemory(command.ambient, &light->dcvAmbient, sizeof(command.ambient));
    CopyMemory(command.position, &light->dvPosition, sizeof(command.position));
    CopyMemory(command.direction, &light->dvDirection, sizeof(command.direction));
    if (light->dltType == D3DLIGHT_PARALLELPOINT)
        CopyMemory(command.direction, &light->dvPosition,
                sizeof(command.direction));
    command.range = light->dvRange;
    command.falloff = light->dvFalloff;
    command.attenuation[0] = light->dvAttenuation0;
    command.attenuation[1] = light->dvAttenuation1;
    command.attenuation[2] = light->dvAttenuation2;
    command.theta = light->dvTheta;
    command.phi = light->dvPhi;
    if (!emit_command(D9WG_OP_SET_LIGHT, &command, sizeof(command)))
        return DDWG_TRACE_ERROR(D3DERR_LIGHT_SET_FAILED);
    device->lights[index] = *light;
    device->light_set[index] = TRUE;
    if (device->recording_state_block)
        device->recording_state_block->light_mask[index] = 1;
    return D3D_OK;
}

static HRESULT WINAPI d3d7_device_GetLight(IDirect3DDevice7 *iface,
        DWORD index, D3DLIGHT7 *light)
{
    D3D7Device *device = d3d7_device_from_iface(iface);
    if (!light || index >= D3D7_MAX_LIGHTS || !device->light_set[index])
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *light = device->lights[index];
    return D3D_OK;
}

static BOOL d3d7_emit_sampler_state(D3D7Device *device, DWORD stage,
        DWORD state, DWORD value)
{
    D9WGSetSamplerState command;
    command.device_handle = device->owner->device_handle;
    command.sampler = stage;
    command.state = state;
    command.value = value;
    return emit_command(D9WG_OP_SET_SAMPLER_STATE, &command, sizeof(command));
}

static BOOL d3d7_emit_texture_stage_state(D3D7Device *device, DWORD stage,
        DWORD state, DWORD value)
{
    D9WGSetTextureStageState command;
    command.device_handle = device->owner->device_handle;
    command.stage = stage;
    command.state = state;
    command.value = value;
    return emit_command(D9WG_OP_SET_TEXTURE_STAGE_STATE, &command,
            sizeof(command));
}

static BOOL d3d7_emit_render_state(D3D7Device *device, DWORD state,
        DWORD value)
{
    D9WGSetRenderState command;
    unsigned sampler_state;
    unsigned action = ddraw_render_state_action(state, &sampler_state);
    ZeroMemory(&command, sizeof(command));
    command.device_handle = device->owner->device_handle;

    if (action == DDRAW_STATE_SHADOW_ONLY) return TRUE;
    if (action == DDRAW_STATE_SAMPLER) {
        if (!d3d7_emit_sampler_state(device, 0, sampler_state, value))
            return FALSE;
        if (state == D3DRENDERSTATE_TEXTUREADDRESS)
            return d3d7_emit_sampler_state(device, 0, D3D9SAMP_ADDRESSV,
                    value);
        return TRUE;
    }
    if (action == DDRAW_STATE_PASS_THROUGH) {
        command.state = state;
        command.value = value;
        return emit_command(D9WG_OP_SET_RENDER_STATE, &command,
                sizeof(command));
    }

    switch (state) {
    case D3DRENDERSTATE_TEXTUREMAG: {
        unsigned base, mip;
        ddraw_filter_to_sampler(value, &base, &mip);
        return d3d7_emit_sampler_state(device, 0, D3D9SAMP_MAGFILTER, base);
    }
    case D3DRENDERSTATE_TEXTUREMIN: {
        unsigned base, mip;
        ddraw_filter_to_sampler(value, &base, &mip);
        return d3d7_emit_sampler_state(device, 0, D3D9SAMP_MINFILTER, base) &&
                d3d7_emit_sampler_state(device, 0, D3D9SAMP_MIPFILTER, mip);
    }
    case D3DRENDERSTATE_TEXTUREMAPBLEND: {
        DDrawBlendCascade cascade;
        ddraw_texture_map_blend_cascade(value, &cascade);
        if (!cascade.supported) return FALSE;
        device->texture_stage_states[0][D3DTSS_COLOROP] = cascade.color_op;
        device->texture_stage_states[0][D3DTSS_COLORARG1] = cascade.color_arg1;
        device->texture_stage_states[0][D3DTSS_COLORARG2] = cascade.color_arg2;
        device->texture_stage_states[0][D3DTSS_ALPHAOP] = cascade.alpha_op;
        device->texture_stage_states[0][D3DTSS_ALPHAARG1] = cascade.alpha_arg1;
        device->texture_stage_states[0][D3DTSS_ALPHAARG2] = cascade.alpha_arg2;
        return d3d7_emit_texture_stage_state(device, 0, D3DTSS_COLOROP,
                    cascade.color_op) &&
                d3d7_emit_texture_stage_state(device, 0, D3DTSS_COLORARG1,
                    cascade.color_arg1) &&
                d3d7_emit_texture_stage_state(device, 0, D3DTSS_COLORARG2,
                    cascade.color_arg2) &&
                d3d7_emit_texture_stage_state(device, 0, D3DTSS_ALPHAOP,
                    cascade.alpha_op) &&
                d3d7_emit_texture_stage_state(device, 0, D3DTSS_ALPHAARG1,
                    cascade.alpha_arg1) &&
                d3d7_emit_texture_stage_state(device, 0, D3DTSS_ALPHAARG2,
                    cascade.alpha_arg2);
    }
    case D3DRENDERSTATE_ZBIAS: {
        float bias = (float)(value > 16u ? 16u : value) *
                D3D8_ZBIAS_TO_DEPTHBIAS_STEP;
        command.state = D3D9RS_DEPTHBIAS;
        CopyMemory(&command.value, &bias, sizeof(command.value));
        return emit_command(D9WG_OP_SET_RENDER_STATE, &command,
                sizeof(command));
    }
    case D3DRENDERSTATE_WRAPU:
    case D3DRENDERSTATE_WRAPV: {
        DWORD wrap = device->render_states[D3DRENDERSTATE_WRAPU] ? 1u : 0u;
        if (device->render_states[D3DRENDERSTATE_WRAPV]) wrap |= 2u;
        command.state = D3D9RS_WRAP0;
        command.value = wrap;
        return emit_command(D9WG_OP_SET_RENDER_STATE, &command,
                sizeof(command));
    }
    case D3DRENDERSTATE_ANTIALIAS:
        command.state = D3D9RS_MULTISAMPLEANTIALIAS;
        command.value = value != D3DANTIALIAS_NONE;
        return emit_command(D9WG_OP_SET_RENDER_STATE, &command,
                sizeof(command));
    case D3DRENDERSTATE_TEXTUREHANDLE:
        /* Texture handles belong to IDirect3DTexture2 and are handled by the
         * D3D3 adapter. Device7 binds surfaces directly through SetTexture. */
        return value == 0;
    case D3DRENDERSTATE_COLORKEYENABLE:
    case D3DRENDERSTATE_COLORKEYBLENDENABLE:
        /* These legacy values do not exist in D3D9, but the shared executor
         * deliberately keeps unknown render-state ids.  The fixed-function
         * pixel signature consumes 41/144 together with each bound
         * DirectDraw surface's SRCBLT key. */
        command.state = state;
        command.value = value;
        return emit_command(D9WG_OP_SET_RENDER_STATE, &command,
                sizeof(command));
    default:
        return TRUE;
    }
}

static HRESULT WINAPI d3d7_device_SetRenderState(IDirect3DDevice7 *iface,
        D3DRENDERSTATETYPE state, DWORD value)
{
    D3D7Device *device = d3d7_device_from_iface(iface);
    DWORD previous;
    if (!d3d7_render_state_valid(state)) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    previous = device->render_states[state];
    device->render_states[state] = value;
    if (!d3d7_emit_render_state(device, state, value)) {
        device->render_states[state] = previous;
        return DDWG_TRACE_ERROR(D3DERR_UNSUPPORTEDTEXTUREFILTER);
    }
    if (device->recording_state_block)
        device->recording_state_block->render_mask[state] = 1;
    return D3D_OK;
}

static HRESULT WINAPI d3d7_device_GetRenderState(IDirect3DDevice7 *iface,
        D3DRENDERSTATETYPE state, DWORD *value)
{
    if (!value || !d3d7_render_state_valid(state))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *value = d3d7_device_from_iface(iface)->render_states[state];
    return D3D_OK;
}

static HRESULT WINAPI d3d7_device_BeginStateBlock(IDirect3DDevice7 *iface)
{
    D3D7Device *device = d3d7_device_from_iface(iface);
    if (device->recording_state_block) return DDWG_TRACE_ERROR(D3DERR_INBEGINSTATEBLOCK);
    device->recording_state_block = (D3D7StateBlock *)
            heap_alloc_zero(sizeof(D3D7StateBlock));
    if (!device->recording_state_block) return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY);
    device->recording_state_block->type = D3DSBT_ALL;
    return D3D_OK;
}

static HRESULT WINAPI d3d7_device_EndStateBlock(IDirect3DDevice7 *iface,
        DWORD *handle)
{
    D3D7Device *device = d3d7_device_from_iface(iface);
    D3D7StateBlock *block = device->recording_state_block;
    if (!handle) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *handle = 0;
    if (!block) return DDWG_TRACE_ERROR(D3DERR_NOTINBEGINSTATEBLOCK);
    device->recording_state_block = NULL;
    block->handle = device->next_state_block_handle++;
    if (!block->handle) block->handle = device->next_state_block_handle++;
    d3d7_state_block_capture(device, block);
    block->next = device->state_blocks;
    device->state_blocks = block;
    *handle = block->handle;
    return D3D_OK;
}

static HRESULT WINAPI d3d7_device_PreLoad(IDirect3DDevice7 *iface,
        IDirectDrawSurface7 *surface_iface)
{
    DDSurface *surface;
    (void)iface;
    if (!surface_iface || surface_iface->lpVtbl != &g_surface_vtbl)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    surface = surface_from_any(surface_iface, NULL);
    if (surface->owner != d3d7_device_from_iface(iface)->owner)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (surface->lock_depth) return DDWG_TRACE_ERROR(DDERR_SURFACEBUSY);
    if (surface->memory->gpu_dirty) return D3D_OK;
    return DDWG_TRACE_ERROR(emit_surface_upload(surface, NULL)
            ? D3D_OK : D3DERR_INITFAILED);
}

static HRESULT WINAPI d3d7_device_DrawPrimitive(IDirect3DDevice7 *iface,
        D3DPRIMITIVETYPE primitive_type, DWORD fvf, void *vertices,
        DWORD vertex_count, DWORD flags)
{
    (void)flags;
    return d3d7_emit_draw_up(d3d7_device_from_iface(iface), primitive_type,
            fvf, vertices, vertex_count) ? D3D_OK : D3DERR_INVALIDVERTEXFORMAT;
}

static HRESULT WINAPI d3d7_device_DrawIndexedPrimitive(
        IDirect3DDevice7 *iface, D3DPRIMITIVETYPE primitive_type, DWORD fvf,
        void *vertices, DWORD vertex_count, WORD *indices, DWORD index_count,
        DWORD flags)
{
    (void)flags;
    return d3d7_emit_draw_indexed_up(d3d7_device_from_iface(iface),
            primitive_type, fvf, vertices, vertex_count, indices, index_count)
            ? D3D_OK : D3DERR_INVALIDVERTEXFORMAT;
}

static HRESULT WINAPI d3d7_device_SetClipStatus(IDirect3DDevice7 *iface,
        D3DCLIPSTATUS *status)
{
    if (!status) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    d3d7_device_from_iface(iface)->clip_status = *status;
    return D3D_OK;
}

static HRESULT WINAPI d3d7_device_GetClipStatus(IDirect3DDevice7 *iface,
        D3DCLIPSTATUS *status)
{
    if (!status) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *status = d3d7_device_from_iface(iface)->clip_status;
    return D3D_OK;
}

static BYTE *d3d7_pack_strided(DWORD fvf,
        D3DDRAWPRIMITIVESTRIDEDDATA *data, DWORD vertex_count,
        UINT *byte_count_out)
{
    D3D7FVFLayout layout;
    D9WGVertexElement elements[D3D7_MAX_VERTEX_ELEMENTS];
    BYTE *packed;
    UINT element_count = 0;
    UINT bytes;
    UINT vertex, stage;
    if (!data || !vertex_count ||
            !d3d7_decode_fvf(fvf, &layout, elements, &element_count) ||
            !data->position.lpvData ||
            !d3d7_multiply_u32(vertex_count, layout.stride, &bytes))
        return NULL;
    if ((layout.has_normal && !data->normal.lpvData) ||
            (layout.has_diffuse && !data->diffuse.lpvData) ||
            (layout.has_specular && !data->specular.lpvData))
        return NULL;
    for (stage = 0; stage < layout.texcoord_count; ++stage) {
        if (!data->textureCoords[stage].lpvData) return NULL;
    }
    packed = (BYTE *)heap_alloc_zero(bytes);
    if (!packed) return NULL;
    for (vertex = 0; vertex < vertex_count; ++vertex) {
        BYTE *destination = packed + vertex * layout.stride;
#define D3D7_COPY_STRIDED(field, destination_offset, amount) \
        CopyMemory(destination + (destination_offset), \
                (BYTE *)data->field.lpvData + vertex * data->field.dwStride, \
                (amount))
        D3D7_COPY_STRIDED(position, layout.position_offset,
                layout.position_bytes);
        if (layout.has_normal)
            D3D7_COPY_STRIDED(normal, layout.normal_offset, 12u);
        if (layout.has_diffuse)
            D3D7_COPY_STRIDED(diffuse, layout.diffuse_offset, 4u);
        if (layout.has_specular)
            D3D7_COPY_STRIDED(specular, layout.specular_offset, 4u);
#undef D3D7_COPY_STRIDED
        for (stage = 0; stage < layout.texcoord_count; ++stage) {
            CopyMemory(destination + layout.texcoord_offset[stage],
                    (BYTE *)data->textureCoords[stage].lpvData +
                        vertex * data->textureCoords[stage].dwStride,
                    layout.texcoord_bytes[stage]);
        }
    }
    *byte_count_out = bytes;
    return packed;
}

static HRESULT WINAPI d3d7_device_DrawPrimitiveStrided(
        IDirect3DDevice7 *iface, D3DPRIMITIVETYPE primitive_type, DWORD fvf,
        D3DDRAWPRIMITIVESTRIDEDDATA *data, DWORD vertex_count, DWORD flags)
{
    BYTE *packed;
    UINT bytes;
    HRESULT result;
    (void)flags;
    packed = d3d7_pack_strided(fvf, data, vertex_count, &bytes);
    (void)bytes;
    if (!packed) return DDWG_TRACE_ERROR(D3DERR_INVALIDVERTEXFORMAT);
    result = d3d7_emit_draw_up(d3d7_device_from_iface(iface), primitive_type,
            fvf, packed, vertex_count) ? D3D_OK : D3DERR_INVALIDVERTEXFORMAT;
    heap_free(packed);
    return DDWG_TRACE_ERROR(result);
}

static HRESULT WINAPI d3d7_device_DrawIndexedPrimitiveStrided(
        IDirect3DDevice7 *iface, D3DPRIMITIVETYPE primitive_type, DWORD fvf,
        D3DDRAWPRIMITIVESTRIDEDDATA *data, DWORD vertex_count, WORD *indices,
        DWORD index_count, DWORD flags)
{
    BYTE *packed;
    UINT bytes;
    HRESULT result;
    (void)flags;
    packed = d3d7_pack_strided(fvf, data, vertex_count, &bytes);
    (void)bytes;
    if (!packed) return DDWG_TRACE_ERROR(D3DERR_INVALIDVERTEXFORMAT);
    result = d3d7_emit_draw_indexed_up(d3d7_device_from_iface(iface),
            primitive_type, fvf, packed, vertex_count, indices, index_count)
            ? D3D_OK : D3DERR_INVALIDVERTEXFORMAT;
    heap_free(packed);
    return DDWG_TRACE_ERROR(result);
}

static HRESULT WINAPI d3d7_device_DrawPrimitiveVB(IDirect3DDevice7 *iface,
        D3DPRIMITIVETYPE primitive_type, IDirect3DVertexBuffer7 *buffer_iface,
        DWORD start_vertex, DWORD vertex_count, DWORD flags)
{
    D3D7Device *device = d3d7_device_from_iface(iface);
    D3D7VertexBuffer *buffer;
    D9WGSetStreamSource stream;
    D9WGDrawPrimitive draw;
    UINT primitive_count;
    (void)flags;
    if (!buffer_iface || buffer_iface->lpVtbl != &g_d3d7_vb_vtbl)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    buffer = d3d7_vb_from_iface(buffer_iface);
    if (buffer->owner != device->owner || buffer->locked ||
            start_vertex > buffer->desc.dwNumVertices ||
            vertex_count > buffer->desc.dwNumVertices - start_vertex ||
            !d3d7_primitive_counts(primitive_type, vertex_count,
                    &primitive_count) ||
            !d3d7_emit_fvf(device, buffer->desc.dwFVF, NULL))
        return DDWG_TRACE_ERROR(D3DERR_INVALIDVERTEXFORMAT);
    ZeroMemory(&stream, sizeof(stream));
    stream.device_handle = device->owner->device_handle;
    stream.buffer_handle = buffer->handle;
    stream.stride = buffer->stride;
    if (!emit_command(D9WG_OP_SET_STREAM_SOURCE, &stream, sizeof(stream)))
        return DDWG_TRACE_ERROR(D3DERR_INITFAILED);
    draw.device_handle = device->owner->device_handle;
    draw.primitive_type = primitive_type;
    draw.start_vertex = start_vertex;
    draw.primitive_count = primitive_count;
    if (!emit_command(D9WG_OP_DRAW_PRIMITIVE, &draw, sizeof(draw)))
        return DDWG_TRACE_ERROR(D3DERR_INITFAILED);
    device->render_target->memory->gpu_dirty = TRUE;
    return D3D_OK;
}

static HRESULT WINAPI d3d7_device_DrawIndexedPrimitiveVB(
        IDirect3DDevice7 *iface, D3DPRIMITIVETYPE primitive_type,
        IDirect3DVertexBuffer7 *buffer_iface, DWORD start_vertex,
        DWORD vertex_count, WORD *indices, DWORD index_count, DWORD flags)
{
    D3D7Device *device = d3d7_device_from_iface(iface);
    D3D7VertexBuffer *buffer;
    (void)flags;
    if (!buffer_iface || buffer_iface->lpVtbl != &g_d3d7_vb_vtbl || !indices)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    buffer = d3d7_vb_from_iface(buffer_iface);
    if (buffer->owner != device->owner || buffer->locked ||
            start_vertex > buffer->desc.dwNumVertices ||
            vertex_count > buffer->desc.dwNumVertices - start_vertex)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    return d3d7_emit_draw_indexed_up(device, primitive_type,
            buffer->desc.dwFVF, buffer->shadow + start_vertex * buffer->stride,
            vertex_count, indices, index_count)
            ? D3D_OK : D3DERR_INVALIDVERTEXFORMAT;
}

static HRESULT WINAPI d3d7_device_ComputeSphereVisibility(
        IDirect3DDevice7 *iface, D3DVECTOR *centers, D3DVALUE *radii,
        DWORD sphere_count, DWORD flags, DWORD *results)
{
    D3D7Device *device = d3d7_device_from_iface(iface);
    D3DMATRIX world_view;
    D3DMATRIX world_view_projection;
    float planes[12][4];
    DWORD enabled_planes;
    DWORD index, plane;
    (void)flags;
    if ((sphere_count && (!centers || !radii || !results)))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);

    d3d7_multiply_matrix(&device->transforms[D3DTRANSFORMSTATE_WORLD],
            &device->transforms[D3DTRANSFORMSTATE_VIEW], &world_view);
    d3d7_multiply_matrix(&world_view,
            &device->transforms[D3DTRANSFORMSTATE_PROJECTION],
            &world_view_projection);

    /* Direct3D uses row vectors.  The clip inequalities are therefore the
     * fourth matrix column +/- the X/Y/Z column.  Z is [0,w], unlike the
     * OpenGL [-w,w] convention.  Plane order matches the D3DVIS bit pairs. */
#define D3D7_SET_PLANE(dst, sign, column) do { \
    planes[(dst)][0] = world_view_projection._14 sign \
            world_view_projection.column##1; \
    planes[(dst)][1] = world_view_projection._24 sign \
            world_view_projection.column##2; \
    planes[(dst)][2] = world_view_projection._34 sign \
            world_view_projection.column##3; \
    planes[(dst)][3] = world_view_projection._44 sign \
            world_view_projection.column##4; \
} while (0)
    D3D7_SET_PLANE(0, +, _1); /* left:   w + x */
    D3D7_SET_PLANE(1, -, _1); /* right:  w - x */
    D3D7_SET_PLANE(2, -, _2); /* top:    w - y */
    D3D7_SET_PLANE(3, +, _2); /* bottom: w + y */
#undef D3D7_SET_PLANE
    planes[4][0] = world_view_projection._13; /* near: z */
    planes[4][1] = world_view_projection._23;
    planes[4][2] = world_view_projection._33;
    planes[4][3] = world_view_projection._43;
    planes[5][0] = world_view_projection._14 -
            world_view_projection._13; /* far: w - z */
    planes[5][1] = world_view_projection._24 - world_view_projection._23;
    planes[5][2] = world_view_projection._34 - world_view_projection._33;
    planes[5][3] = world_view_projection._44 - world_view_projection._43;

    /* D3D7 returns D3DSTATUS_CLIPUNION* / CLIPINTERSECTION* bits, not the
     * two-bit D3DVIS_* fields used by IDirect3DDevice3. Enabled user clip
     * planes occupy the six GEN bits after the six frustum planes. */
    enabled_planes = 0x3fu |
        ((device->render_states[D3DRENDERSTATE_CLIPPLANEENABLE] & 0x3fu) << 6);
    for (plane = 6u; plane < 12u; ++plane)
        CopyMemory(planes[plane], device->clip_planes[plane - 6u],
                sizeof(planes[plane]));

    for (index = 0; index < sphere_count; ++index) {
        DWORD visibility = 0;
        for (plane = 0; plane < 12u; ++plane) {
            float normal_length = d3d7_sqrt(planes[plane][0] *
                    planes[plane][0] + planes[plane][1] * planes[plane][1] +
                    planes[plane][2] * planes[plane][2]);
            float distance;
            DWORD union_bit;
            if (!(enabled_planes & (1u << plane)) ||
                    !(normal_length > 0.0f))
                continue;
            distance = (planes[plane][0] * centers[index].x +
                    planes[plane][1] * centers[index].y +
                    planes[plane][2] * centers[index].z +
                    planes[plane][3]) / normal_length;
            union_bit = D3DSTATUS_CLIPUNIONLEFT << plane;
            if (distance > -radii[index] && distance < radii[index])
                visibility |= union_bit;
            else if (distance < -radii[index])
                visibility |= union_bit |
                    (D3DSTATUS_CLIPINTERSECTIONLEFT << plane);
        }
        results[index] = visibility;
    }
    return D3D_OK;
}

static HRESULT WINAPI d3d7_device_GetTexture(IDirect3DDevice7 *iface,
        DWORD stage, IDirectDrawSurface7 **out)
{
    DDSurface *surface;
    if (!out || stage >= D3D7_MAX_TEXTURE_STAGES) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    surface = d3d7_device_from_iface(iface)->textures[stage];
    *out = surface ? (IDirectDrawSurface7 *)&surface->vtbl7 : NULL;
    if (surface) surface_AddRef(*out);
    return D3D_OK;
}

static HRESULT WINAPI d3d7_device_SetTexture(IDirect3DDevice7 *iface,
        DWORD stage, IDirectDrawSurface7 *surface_iface)
{
    D3D7Device *device = d3d7_device_from_iface(iface);
    DDSurface *surface;
    DDSurface *old;
    D9WGSetTexture command;
    if (surface_iface && surface_iface->lpVtbl != &g_surface_vtbl)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    surface = surface_iface ? surface_from_any(surface_iface, NULL) : NULL;
    if (stage >= D3D7_MAX_TEXTURE_STAGES ||
            (surface && (surface->owner != device->owner ||
                    format_is_depth(surface->format))))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (surface) {
        if (!sync_external_surface_for_read(surface)) return DDWG_TRACE_ERROR(D3DERR_INITFAILED);
        if (!prepare_surface_for_host(surface)) return DDWG_TRACE_ERROR(D3DERR_INITFAILED);
        rebind_host_surface_state(surface);
    }
    if (device->textures[stage] == surface) {
        if (device->recording_state_block)
            device->recording_state_block->texture_mask[stage] = 1;
        return D3D_OK;
    }
    if (surface) surface_AddRef((IDirectDrawSurface7 *)&surface->vtbl7);
    old = device->textures[stage];
    device->textures[stage] = surface;
    ZeroMemory(&command, sizeof(command));
    command.device_handle = device->owner->device_handle;
    command.stage = stage;
    command.texture_handle = surface ? surface->handle : 0u;
    if (!emit_command(D9WG_OP_SET_TEXTURE, &command, sizeof(command))) {
        device->textures[stage] = old;
        if (surface) surface_Release((IDirectDrawSurface7 *)&surface->vtbl7);
        return DDWG_TRACE_ERROR(D3DERR_INITFAILED);
    }
    if (old) surface_Release((IDirectDrawSurface7 *)&old->vtbl7);
    if (device->recording_state_block)
        device->recording_state_block->texture_mask[stage] = 1;
    return D3D_OK;
}

static HRESULT WINAPI d3d7_device_GetTextureStageState(
        IDirect3DDevice7 *iface, DWORD stage, D3DTEXTURESTAGESTATETYPE state,
        DWORD *value)
{
    if (!value || stage >= D3D7_MAX_TEXTURE_STAGES ||
            !d3d7_texture_stage_state_valid(state))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *value = d3d7_device_from_iface(iface)->texture_stage_states[stage][state];
    return D3D_OK;
}

static BOOL d3d7_sampler_value(D3DTEXTURESTAGESTATETYPE state, DWORD value,
        DWORD *translated)
{
    *translated = value;
    switch (state) {
    case D3DTSS_MAGFILTER:
        if (value == D3DTFG_POINT || value == D3DTFG_LINEAR) return TRUE;
        if (value == D3DTFG_ANISOTROPIC) {
            *translated = D3D9TEXF_ANISOTROPIC;
            return TRUE;
        }
        return FALSE;
    case D3DTSS_MINFILTER:
        return value == D3DTFN_POINT || value == D3DTFN_LINEAR ||
                value == D3DTFN_ANISOTROPIC;
    case D3DTSS_MIPFILTER:
        if (value < D3DTFP_NONE || value > D3DTFP_LINEAR) return FALSE;
        *translated = value - D3DTFP_NONE;
        return TRUE;
    default:
        return TRUE;
    }
}

static HRESULT WINAPI d3d7_device_SetTextureStageState(
        IDirect3DDevice7 *iface, DWORD stage, D3DTEXTURESTAGESTATETYPE state,
        DWORD value)
{
    D3D7Device *device = d3d7_device_from_iface(iface);
    unsigned sampler_state;
    DWORD previous;
    DWORD translated_value;
    BOOL result;
    if (stage >= D3D7_MAX_TEXTURE_STAGES ||
            !d3d7_texture_stage_state_valid(state))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    previous = device->texture_stage_states[stage][state];
    device->texture_stage_states[stage][state] = value;
    sampler_state = ddraw_stage_state_to_sampler_state(state);
    if (sampler_state) {
        if (!d3d7_sampler_value(state, value, &translated_value)) {
            device->texture_stage_states[stage][state] = previous;
            return DDWG_TRACE_ERROR(D3DERR_UNSUPPORTEDTEXTUREFILTER);
        }
        result = d3d7_emit_sampler_state(device, stage, sampler_state,
                translated_value);
        if (result && (UINT)state == D3D7TSS_ADDRESS)
            result = d3d7_emit_sampler_state(device, stage,
                    D3D9SAMP_ADDRESSV, translated_value);
    } else {
        result = d3d7_emit_texture_stage_state(device, stage, state, value);
    }
    if (!result) {
        device->texture_stage_states[stage][state] = previous;
        return DDWG_TRACE_ERROR(D3DERR_INITFAILED);
    }
    if (device->recording_state_block)
        device->recording_state_block->texture_stage_mask[stage][state] = 1;
    return D3D_OK;
}

static HRESULT WINAPI d3d7_device_ValidateDevice(IDirect3DDevice7 *iface,
        DWORD *passes)
{
    (void)iface;
    if (!passes) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *passes = 1u;
    return D3D_OK;
}

static BOOL d3d7_sync_initial_state(D3D7Device *device)
{
    static const DWORD render_states[] = {
        D3DRENDERSTATE_ANTIALIAS, D3DRENDERSTATE_TEXTUREADDRESS,
        D3DRENDERSTATE_TEXTUREPERSPECTIVE,
        D3DRENDERSTATE_ZENABLE, D3DRENDERSTATE_ZWRITEENABLE,
        D3DRENDERSTATE_ZFUNC, D3DRENDERSTATE_FILLMODE,
        D3DRENDERSTATE_CULLMODE, D3DRENDERSTATE_SHADEMODE,
        D3DRENDERSTATE_LASTPIXEL, D3DRENDERSTATE_TEXTUREMAG,
        D3DRENDERSTATE_TEXTUREMIN, D3DRENDERSTATE_TEXTUREMAPBLEND,
        D3DRENDERSTATE_ALPHAFUNC,
        D3DRENDERSTATE_SRCBLEND, D3DRENDERSTATE_DESTBLEND,
        D3DRENDERSTATE_TEXTUREFACTOR, D3DRENDERSTATE_FOGEND,
        D3DRENDERSTATE_FOGDENSITY, D3DRENDERSTATE_STENCILFAIL,
        D3DRENDERSTATE_STENCILZFAIL, D3DRENDERSTATE_STENCILPASS,
        D3DRENDERSTATE_STENCILFUNC, D3DRENDERSTATE_STENCILMASK,
        D3DRENDERSTATE_STENCILWRITEMASK, D3DRENDERSTATE_LIGHTING,
        D3DRENDERSTATE_CLIPPING,
        D3DRENDERSTATE_COLORVERTEX, D3DRENDERSTATE_LOCALVIEWER,
        D3DRENDERSTATE_DIFFUSEMATERIALSOURCE,
        D3DRENDERSTATE_SPECULARMATERIALSOURCE,
        D3DRENDERSTATE_AMBIENTMATERIALSOURCE,
        D3DRENDERSTATE_EMISSIVEMATERIALSOURCE,
    };
    static const DWORD texture_states[] = {
        D3DTSS_COLOROP, D3DTSS_COLORARG1, D3DTSS_COLORARG2,
        D3DTSS_ALPHAOP, D3DTSS_ALPHAARG1, D3DTSS_ALPHAARG2,
        D3DTSS_TEXCOORDINDEX, D3DTSS_ADDRESS, D3DTSS_ADDRESSU,
        D3DTSS_ADDRESSV, D3DTSS_MAGFILTER, D3DTSS_MINFILTER,
        D3DTSS_MIPFILTER, D3DTSS_MAXANISOTROPY,
    };
    IDirect3DDevice7 *iface = (IDirect3DDevice7 *)device;
    UINT index, stage;
    if (FAILED(d3d7_device_SetViewport(iface, &device->viewport)) ||
            FAILED(d3d7_device_SetMaterial(iface, &device->material)))
        return FALSE;
    for (index = 0; index < D3D7_MAX_TRANSFORMS; ++index) {
        if (d3d7_transform_state_valid(index) &&
                FAILED(d3d7_device_SetTransform(iface,
                        (D3DTRANSFORMSTATETYPE)index,
                        &device->transforms[index])))
            return FALSE;
    }
    for (index = 0; index < sizeof(render_states) / sizeof(render_states[0]);
            ++index) {
        DWORD state = render_states[index];
        if (FAILED(d3d7_device_SetRenderState(iface,
                (D3DRENDERSTATETYPE)state, device->render_states[state])))
            return FALSE;
    }
    for (stage = 0; stage < D3D7_MAX_TEXTURE_STAGES; ++stage) {
        for (index = 0;
                index < sizeof(texture_states) / sizeof(texture_states[0]);
                ++index) {
            DWORD state = texture_states[index];
            if (FAILED(d3d7_device_SetTextureStageState(iface, stage,
                    (D3DTEXTURESTAGESTATETYPE)state,
                    device->texture_stage_states[stage][state])))
                return FALSE;
        }
    }
    return TRUE;
}

static HRESULT WINAPI d3d7_device_LightEnable(IDirect3DDevice7 *iface,
        DWORD index, WINBOOL enable);
static HRESULT WINAPI d3d7_device_SetClipPlane(IDirect3DDevice7 *iface,
        DWORD index, D3DVALUE *plane);

static HRESULT WINAPI d3d7_device_ApplyStateBlock(IDirect3DDevice7 *iface,
        DWORD handle)
{
    D3D7Device *device = d3d7_device_from_iface(iface);
    D3D7StateBlock *block = d3d7_find_state_block(device, handle);
    UINT state, stage;
    HRESULT result;
    if (!block) return DDWG_TRACE_ERROR(D3DERR_INVALIDSTATEBLOCK);
    if (device->recording_state_block) return DDWG_TRACE_ERROR(D3DERR_INBEGINSTATEBLOCK);
#define D3D7_APPLY(call) do { result = (call); if (FAILED(result)) return DDWG_TRACE_ERROR(result); } while (0)
    for (state = 0; state < D3D7_MAX_RENDER_STATES; ++state) {
        if (block->render_mask[state])
            D3D7_APPLY(d3d7_device_SetRenderState(iface,
                    (D3DRENDERSTATETYPE)state, block->render_states[state]));
    }
    for (stage = 0; stage < D3D7_MAX_TEXTURE_STAGES; ++stage) {
        if (block->texture_mask[stage])
            D3D7_APPLY(d3d7_device_SetTexture(iface, stage,
                    block->textures[stage] ? (IDirectDrawSurface7 *)
                        &block->textures[stage]->vtbl7 : NULL));
        for (state = 0; state < D3D7_MAX_TEXTURE_STAGE_STATES; ++state) {
            if (block->texture_stage_mask[stage][state])
                D3D7_APPLY(d3d7_device_SetTextureStageState(iface, stage,
                        (D3DTEXTURESTAGESTATETYPE)state,
                        block->texture_stage_states[stage][state]));
        }
    }
    for (state = 0; state < D3D7_MAX_TRANSFORMS; ++state) {
        if (block->transform_mask[state])
            D3D7_APPLY(d3d7_device_SetTransform(iface,
                    (D3DTRANSFORMSTATETYPE)state, &block->transforms[state]));
    }
    if (block->viewport_mask)
        D3D7_APPLY(d3d7_device_SetViewport(iface, &block->viewport));
    if (block->material_mask)
        D3D7_APPLY(d3d7_device_SetMaterial(iface, &block->material));
    for (state = 0; state < D3D7_MAX_LIGHTS; ++state) {
        if (block->light_mask[state] && block->light_set[state])
            D3D7_APPLY(d3d7_device_SetLight(iface, state,
                    &block->lights[state]));
        if (block->light_enable_mask[state] && block->light_set[state])
            D3D7_APPLY(d3d7_device_LightEnable(iface, state,
                    block->light_enabled[state]));
    }
    for (state = 0; state < D3D7_MAX_CLIP_PLANES; ++state) {
        if (block->clip_plane_mask[state])
            D3D7_APPLY(d3d7_device_SetClipPlane(iface, state,
                    block->clip_planes[state]));
    }
#undef D3D7_APPLY
    return D3D_OK;
}

static HRESULT WINAPI d3d7_device_CaptureStateBlock(IDirect3DDevice7 *iface,
        DWORD handle)
{
    D3D7Device *device = d3d7_device_from_iface(iface);
    D3D7StateBlock *block = d3d7_find_state_block(device, handle);
    if (!block) return DDWG_TRACE_ERROR(D3DERR_INVALIDSTATEBLOCK);
    if (device->recording_state_block) return DDWG_TRACE_ERROR(D3DERR_INBEGINSTATEBLOCK);
    d3d7_state_block_capture(device, block);
    return D3D_OK;
}

static HRESULT WINAPI d3d7_device_DeleteStateBlock(IDirect3DDevice7 *iface,
        DWORD handle)
{
    D3D7Device *device = d3d7_device_from_iface(iface);
    D3D7StateBlock **link = &device->state_blocks;
    while (*link && (*link)->handle != handle) link = &(*link)->next;
    if (!*link) return DDWG_TRACE_ERROR(D3DERR_INVALIDSTATEBLOCK);
    {
        D3D7StateBlock *block = *link;
        *link = block->next;
        d3d7_state_block_destroy(block);
    }
    return D3D_OK;
}

static HRESULT WINAPI d3d7_device_CreateStateBlock(IDirect3DDevice7 *iface,
        D3DSTATEBLOCKTYPE type, DWORD *handle)
{
    D3D7Device *device = d3d7_device_from_iface(iface);
    D3D7StateBlock *block;
    if (!handle) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *handle = 0;
    if (device->recording_state_block) return DDWG_TRACE_ERROR(D3DERR_INBEGINSTATEBLOCK);
    if (type != D3DSBT_ALL && type != D3DSBT_PIXELSTATE &&
            type != D3DSBT_VERTEXSTATE)
        return DDWG_TRACE_ERROR(D3DERR_INVALIDSTATEBLOCK);
    block = (D3D7StateBlock *)heap_alloc_zero(sizeof(*block));
    if (!block) return DDWG_TRACE_ERROR(DDERR_OUTOFMEMORY);
    block->type = type;
    block->handle = device->next_state_block_handle++;
    if (!block->handle) block->handle = device->next_state_block_handle++;
    d3d7_state_block_mark_type(block, type);
    d3d7_state_block_capture(device, block);
    block->next = device->state_blocks;
    device->state_blocks = block;
    *handle = block->handle;
    return D3D_OK;
}

static HRESULT WINAPI d3d7_device_Load(IDirect3DDevice7 *iface,
        IDirectDrawSurface7 *destination_iface, POINT *destination_point,
        IDirectDrawSurface7 *source_iface, RECT *source_rect, DWORD flags)
{
    DDSurface *destination;
    DDSurface *source;
    RECT destination_rect;
    (void)iface; (void)flags;
    if (!destination_iface || destination_iface->lpVtbl != &g_surface_vtbl ||
            !source_iface || source_iface->lpVtbl != &g_surface_vtbl)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    destination = surface_from_any(destination_iface, NULL);
    source = surface_from_any(source_iface, NULL);
    if (destination->owner != d3d7_device_from_iface(iface)->owner ||
            source->owner != d3d7_device_from_iface(iface)->owner)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (!source_rect) {
        destination_rect.left = destination_point ? destination_point->x : 0;
        destination_rect.top = destination_point ? destination_point->y : 0;
        destination_rect.right = destination_rect.left + (LONG)source->width;
        destination_rect.bottom = destination_rect.top + (LONG)source->height;
    } else {
        destination_rect.left = destination_point ? destination_point->x : 0;
        destination_rect.top = destination_point ? destination_point->y : 0;
        destination_rect.right = destination_rect.left +
                (source_rect->right - source_rect->left);
        destination_rect.bottom = destination_rect.top +
                (source_rect->bottom - source_rect->top);
    }
    return surface_Blt((IDirectDrawSurface7 *)&destination->vtbl7,
            &destination_rect, (IDirectDrawSurface7 *)&source->vtbl7,
            source_rect, DDBLT_WAIT, NULL);
}

static HRESULT WINAPI d3d7_device_LightEnable(IDirect3DDevice7 *iface,
        DWORD index, WINBOOL enable)
{
    D3D7Device *device = d3d7_device_from_iface(iface);
    D9WGLightEnable command;
    if (index >= D3D7_MAX_LIGHTS || !device->light_set[index])
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    ZeroMemory(&command, sizeof(command));
    command.device_handle = device->owner->device_handle;
    command.index = index;
    command.enable = enable != FALSE;
    if (!emit_command(D9WG_OP_LIGHT_ENABLE, &command, sizeof(command)))
        return DDWG_TRACE_ERROR(D3DERR_LIGHT_SET_FAILED);
    device->light_enabled[index] = enable != FALSE;
    if (device->recording_state_block)
        device->recording_state_block->light_enable_mask[index] = 1;
    return D3D_OK;
}

static HRESULT WINAPI d3d7_device_GetLightEnable(IDirect3DDevice7 *iface,
        DWORD index, WINBOOL *enable)
{
    D3D7Device *device = d3d7_device_from_iface(iface);
    if (!enable || index >= D3D7_MAX_LIGHTS || !device->light_set[index])
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *enable = device->light_enabled[index];
    return D3D_OK;
}

static HRESULT WINAPI d3d7_device_SetClipPlane(IDirect3DDevice7 *iface,
        DWORD index, D3DVALUE *plane)
{
    D3D7Device *device = d3d7_device_from_iface(iface);
    D9WGSetClipPlane command;
    if (!plane || index >= D3D7_MAX_CLIP_PLANES) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    command.device_handle = device->owner->device_handle;
    command.index = index;
    CopyMemory(command.plane, plane, sizeof(command.plane));
    if (!emit_command(D9WG_OP_SET_CLIP_PLANE, &command, sizeof(command)))
        return DDWG_TRACE_ERROR(D3DERR_INITFAILED);
    CopyMemory(device->clip_planes[index], plane, sizeof(command.plane));
    if (device->recording_state_block)
        device->recording_state_block->clip_plane_mask[index] = 1;
    return D3D_OK;
}

static HRESULT WINAPI d3d7_device_GetClipPlane(IDirect3DDevice7 *iface,
        DWORD index, D3DVALUE *plane)
{
    if (!plane || index >= D3D7_MAX_CLIP_PLANES) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    CopyMemory(plane, d3d7_device_from_iface(iface)->clip_planes[index],
            sizeof(float) * 4u);
    return D3D_OK;
}

static HRESULT WINAPI d3d7_device_GetInfo(IDirect3DDevice7 *iface,
        DWORD info_id, void *info, DWORD info_size)
{
    (void)iface; (void)info_id; (void)info; (void)info_size;
    /* The D3D7 contract explicitly permits S_FALSE for unsupported queries. */
    return S_FALSE;
}

static void d3d7_rebind_after_flip(DDrawObject *object)
{
    D3D7Device *device = object ? object->d3d7_device : NULL;
    UINT stage;
    if (!device) return;
    d3d7_emit_render_target(device);
    for (stage = 0; stage < D3D7_MAX_TEXTURE_STAGES; ++stage) {
        if (device->textures[stage]) {
            D9WGSetTexture command;
            if (!prepare_surface_for_host(device->textures[stage]))
                continue;
            ZeroMemory(&command, sizeof(command));
            command.device_handle = object->device_handle;
            command.stage = stage;
            command.texture_handle = device->textures[stage]->handle;
            emit_command(D9WG_OP_SET_TEXTURE, &command, sizeof(command));
        }
    }
}

/* ------------------------------------------------------------------ *
 * IDirect3DVertexBuffer7
 * ------------------------------------------------------------------ */

static HRESULT WINAPI d3d7_vb_QueryInterface(IDirect3DVertexBuffer7 *iface,
        REFIID iid, void **out)
{
    D3D7VertexBuffer *buffer = d3d7_vb_from_iface(iface);
    if (!out) return DDWG_TRACE_ERROR(E_POINTER);
    *out = NULL;
    if (!iid) return DDWG_TRACE_ERROR(E_NOINTERFACE);
    if (iid_is_unknown(iid) || guid_equal(iid, &IID_IDirect3DVertexBuffer7))
        *out = &buffer->vtbl;
    else if (guid_equal(iid, &IID_IDirect3DVertexBuffer))
        *out = &buffer->legacy_vtbl;
    else return DDWG_TRACE_ERROR(E_NOINTERFACE);
    IDirect3DVertexBuffer7_AddRef(iface);
    return S_OK;
}

static ULONG WINAPI d3d7_vb_AddRef(IDirect3DVertexBuffer7 *iface)
{
    return (ULONG)InterlockedIncrement(&d3d7_vb_from_iface(iface)->ref);
}

static ULONG WINAPI d3d7_vb_Release(IDirect3DVertexBuffer7 *iface)
{
    D3D7VertexBuffer *buffer = d3d7_vb_from_iface(iface);
    LONG remaining = InterlockedDecrement(&buffer->ref);
    if (remaining == 0) {
        D9WGDestroyResource destroy;
        DDrawObject *owner = buffer->owner;
        destroy.resource_handle = buffer->handle;
        destroy.resource_kind = D9WG_RESOURCE_BUFFER_VERTEX;
        emit_command(D9WG_OP_DESTROY_RESOURCE, &destroy, sizeof(destroy));
        heap_free(buffer->shadow);
        heap_free(buffer);
        ddraw_Release((IDirectDraw7 *)&owner->vtbl7);
    }
    return (ULONG)(remaining < 0 ? 0 : remaining);
}

static HRESULT WINAPI d3d7_vb_Lock(IDirect3DVertexBuffer7 *iface,
        DWORD flags, void **data, DWORD *data_size)
{
    D3D7VertexBuffer *buffer = d3d7_vb_from_iface(iface);
    if (!data) return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    if (buffer->locked) return DDWG_TRACE_ERROR(D3DERR_VERTEXBUFFERLOCKED);
    if (flags & DDLOCK_DISCARDCONTENTS)
        ZeroMemory(buffer->shadow, buffer->byte_count);
    buffer->locked = TRUE;
    buffer->lock_flags = flags;
    *data = buffer->shadow;
    if (data_size) *data_size = buffer->byte_count;
    return D3D_OK;
}

static HRESULT WINAPI d3d7_vb_Unlock(IDirect3DVertexBuffer7 *iface)
{
    D3D7VertexBuffer *buffer = d3d7_vb_from_iface(iface);
    D9WGUpdateBuffer update;
    uint8_t *payload;
    uint8_t *blob;
    BOOL result = TRUE;
    if (!buffer->locked) return DDWG_TRACE_ERROR(D3DERR_VERTEXBUFFERUNLOCKFAILED);
    if (!(buffer->lock_flags & DDLOCK_READONLY)) {
        ZeroMemory(&update, sizeof(update));
        EnterCriticalSection(&g_transport_lock);
        result = reserve_command_locked(D9WG_OP_UPDATE_BUFFER, sizeof(update),
                buffer->byte_count, &payload, &blob);
        if (result) {
            update.resource_handle = buffer->handle;
            update.byte_count = buffer->byte_count;
            update.data_offset = (uint32_t)(blob - batch_base());
            update.lock_flags = buffer->lock_flags;
            CopyMemory(payload, &update, sizeof(update));
            CopyMemory(blob, buffer->shadow, buffer->byte_count);
        }
        LeaveCriticalSection(&g_transport_lock);
    }
    buffer->locked = FALSE;
    buffer->lock_flags = 0;
    return DDWG_TRACE_ERROR(result ? D3D_OK
            : D3DERR_VERTEXBUFFERUNLOCKFAILED);
}

static float d3d7_sqrt(float value)
{
    union { float value; uint32_t bits; } estimate;
    UINT iteration;
    if (!(value > 0.0f)) return 0.0f;
    estimate.value = value;
    estimate.bits = 0x1fbd1df5u + (estimate.bits >> 1);
    for (iteration = 0; iteration < 4u; ++iteration)
        estimate.value = 0.5f * (estimate.value + value / estimate.value);
    return estimate.value;
}

static void d3d7_transform_vector(const D3DMATRIX *matrix,
        const float *input, float *output)
{
    UINT column;
    for (column = 0; column < 4u; ++column) {
        output[column] = input[0] * (&matrix->_11)[column] +
                input[1] * (&matrix->_21)[column] +
                input[2] * (&matrix->_31)[column] +
                input[3] * (&matrix->_41)[column];
    }
}

static float d3d7_saturate(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}

static DWORD d3d7_pack_color(const float *rgba)
{
    return ((DWORD)(d3d7_saturate(rgba[3]) * 255.0f + 0.5f) << 24) |
            ((DWORD)(d3d7_saturate(rgba[0]) * 255.0f + 0.5f) << 16) |
            ((DWORD)(d3d7_saturate(rgba[1]) * 255.0f + 0.5f) << 8) |
            (DWORD)(d3d7_saturate(rgba[2]) * 255.0f + 0.5f);
}

static void d3d7_light_vertex(D3D7Device *device, const float *eye_position,
        const float *eye_normal, float *rgba)
{
    DWORD ambient_state = device->render_states[D3DRENDERSTATE_AMBIENT];
    float scene_ambient[3];
    UINT index;
    scene_ambient[0] = ((ambient_state >> 16) & 0xffu) / 255.0f;
    scene_ambient[1] = ((ambient_state >> 8) & 0xffu) / 255.0f;
    scene_ambient[2] = (ambient_state & 0xffu) / 255.0f;
    rgba[0] = device->material.emissive.r +
            device->material.ambient.r * scene_ambient[0];
    rgba[1] = device->material.emissive.g +
            device->material.ambient.g * scene_ambient[1];
    rgba[2] = device->material.emissive.b +
            device->material.ambient.b * scene_ambient[2];
    rgba[3] = device->material.diffuse.a;
    for (index = 0; index < D3D7_MAX_LIGHTS; ++index) {
        const D3DLIGHT7 *light;
        float to_light[3];
        float attenuation = 1.0f;
        float length;
        float dot;
        if (!device->light_set[index] || !device->light_enabled[index])
            continue;
        light = &device->lights[index];
        if (light->dltType == D3DLIGHT_DIRECTIONAL ||
                light->dltType == D3DLIGHT_PARALLELPOINT) {
            float direction[4];
            float view_direction[4];
            const D3DVECTOR *source = light->dltType == D3DLIGHT_PARALLELPOINT
                    ? &light->dvPosition : &light->dvDirection;
            direction[0] = source->x;
            direction[1] = source->y;
            direction[2] = source->z;
            direction[3] = 0.0f;
            d3d7_transform_vector(&device->transforms[D3DTRANSFORMSTATE_VIEW],
                    direction, view_direction);
            length = d3d7_sqrt(view_direction[0] * view_direction[0] +
                    view_direction[1] * view_direction[1] +
                    view_direction[2] * view_direction[2]);
            if (length < 1.0e-6f) continue;
            to_light[0] = -view_direction[0] / length;
            to_light[1] = -view_direction[1] / length;
            to_light[2] = -view_direction[2] / length;
        } else {
            float position[4];
            float view_position[4];
            position[0] = light->dvPosition.x;
            position[1] = light->dvPosition.y;
            position[2] = light->dvPosition.z;
            position[3] = 1.0f;
            d3d7_transform_vector(&device->transforms[D3DTRANSFORMSTATE_VIEW],
                    position, view_position);
            to_light[0] = view_position[0] - eye_position[0];
            to_light[1] = view_position[1] - eye_position[1];
            to_light[2] = view_position[2] - eye_position[2];
            length = d3d7_sqrt(to_light[0] * to_light[0] +
                    to_light[1] * to_light[1] + to_light[2] * to_light[2]);
            if (length > light->dvRange) continue;
            if (length < 1.0e-6f) length = 1.0e-6f;
            to_light[0] /= length;
            to_light[1] /= length;
            to_light[2] /= length;
            attenuation = light->dvAttenuation0 +
                    light->dvAttenuation1 * length +
                    light->dvAttenuation2 * length * length;
            attenuation = attenuation < 1.0e-6f ? 1.0f : 1.0f / attenuation;
        }
        dot = eye_normal[0] * to_light[0] +
                eye_normal[1] * to_light[1] +
                eye_normal[2] * to_light[2];
        if (dot < 0.0f) dot = 0.0f;
        rgba[0] += attenuation *
                (device->material.ambient.r * light->dcvAmbient.r +
                 device->material.diffuse.r * light->dcvDiffuse.r * dot);
        rgba[1] += attenuation *
                (device->material.ambient.g * light->dcvAmbient.g +
                 device->material.diffuse.g * light->dcvDiffuse.g * dot);
        rgba[2] += attenuation *
                (device->material.ambient.b * light->dcvAmbient.b +
                 device->material.diffuse.b * light->dcvDiffuse.b * dot);
    }
}

static BOOL d3d7_upload_vertex_range(D3D7VertexBuffer *buffer,
        UINT offset, UINT byte_count)
{
    D9WGUpdateBuffer update;
    uint8_t *payload;
    uint8_t *blob;
    BOOL result;
    ZeroMemory(&update, sizeof(update));
    EnterCriticalSection(&g_transport_lock);
    result = reserve_command_locked(D9WG_OP_UPDATE_BUFFER, sizeof(update),
            byte_count, &payload, &blob);
    if (result) {
        update.resource_handle = buffer->handle;
        update.destination_offset = offset;
        update.byte_count = byte_count;
        update.data_offset = (uint32_t)(blob - batch_base());
        CopyMemory(payload, &update, sizeof(update));
        CopyMemory(blob, buffer->shadow + offset, byte_count);
    }
    LeaveCriticalSection(&g_transport_lock);
    return result;
}

static HRESULT d3d7_process_vertex_bytes(D3D7VertexBuffer *destination,
        DWORD vertex_op, DWORD destination_index, DWORD count,
        const BYTE *source, DWORD source_fvf, UINT source_stride,
        DWORD source_index, D3D7Device *device)
{
    D3D7FVFLayout source_layout, destination_layout;
    D9WGVertexElement source_elements[D3D7_MAX_VERTEX_ELEMENTS];
    D9WGVertexElement destination_elements[D3D7_MAX_VERTEX_ELEMENTS];
    UINT source_element_count = 0, destination_element_count = 0;
    D3DMATRIX world_view, world_view_projection;
    UINT vertex;
    if (!(vertex_op & D3DVOP_TRANSFORM) || !count || !source ||
            !d3d7_decode_fvf(source_fvf, &source_layout, source_elements,
                    &source_element_count) ||
            !d3d7_decode_fvf(destination->desc.dwFVF, &destination_layout,
                    destination_elements, &destination_element_count) ||
            source_layout.position_bytes != 12u ||
            destination_layout.position_bytes != 16u ||
            source_stride < source_layout.stride || destination->locked ||
            destination_index > destination->desc.dwNumVertices ||
            count > destination->desc.dwNumVertices - destination_index)
        return DDWG_TRACE_ERROR(D3DERR_INVALIDVERTEXFORMAT);
    d3d7_multiply_matrix(&device->transforms[D3DTRANSFORMSTATE_WORLD],
            &device->transforms[D3DTRANSFORMSTATE_VIEW], &world_view);
    d3d7_multiply_matrix(&world_view,
            &device->transforms[D3DTRANSFORMSTATE_PROJECTION],
            &world_view_projection);
    for (vertex = 0; vertex < count; ++vertex) {
        const BYTE *input = source +
                (source_index + vertex) * source_stride;
        BYTE *output = destination->shadow +
                (destination_index + vertex) * destination->stride;
        float position[4], clip[4], rhw;
        UINT stage;
        position[0] = ((const float *)(input + source_layout.position_offset))[0];
        position[1] = ((const float *)(input + source_layout.position_offset))[1];
        position[2] = ((const float *)(input + source_layout.position_offset))[2];
        position[3] = 1.0f;
        d3d7_transform_vector(&world_view_projection, position, clip);
        rhw = (clip[3] > 1.0e-6f || clip[3] < -1.0e-6f)
                ? 1.0f / clip[3] : 1.0f;
        ((float *)(output + destination_layout.position_offset))[0] =
                (clip[0] * rhw * 0.5f + 0.5f) * device->viewport.dwWidth +
                device->viewport.dwX;
        ((float *)(output + destination_layout.position_offset))[1] =
                (0.5f - clip[1] * rhw * 0.5f) * device->viewport.dwHeight +
                device->viewport.dwY;
        ((float *)(output + destination_layout.position_offset))[2] =
                device->viewport.dvMinZ + clip[2] * rhw *
                (device->viewport.dvMaxZ - device->viewport.dvMinZ);
        ((float *)(output + destination_layout.position_offset))[3] = rhw;
        if (destination_layout.has_diffuse) {
            DWORD color = 0xffffffffu;
            if ((vertex_op & D3DVOP_LIGHT) && source_layout.has_normal) {
                float eye_position[4], normal[4], eye_normal[4];
                float length, lit[4];
                d3d7_transform_vector(&world_view, position, eye_position);
                normal[0] = ((const float *)(input + source_layout.normal_offset))[0];
                normal[1] = ((const float *)(input + source_layout.normal_offset))[1];
                normal[2] = ((const float *)(input + source_layout.normal_offset))[2];
                normal[3] = 0.0f;
                d3d7_transform_vector(&world_view, normal, eye_normal);
                length = d3d7_sqrt(eye_normal[0] * eye_normal[0] +
                        eye_normal[1] * eye_normal[1] +
                        eye_normal[2] * eye_normal[2]);
                if (length > 1.0e-6f) {
                    eye_normal[0] /= length;
                    eye_normal[1] /= length;
                    eye_normal[2] /= length;
                }
                d3d7_light_vertex(device, eye_position, eye_normal, lit);
                color = d3d7_pack_color(lit);
            } else if (source_layout.has_diffuse) {
                color = *(const DWORD *)(input + source_layout.diffuse_offset);
            }
            *(DWORD *)(output + destination_layout.diffuse_offset) = color;
        }
        if (destination_layout.has_specular) {
            *(DWORD *)(output + destination_layout.specular_offset) =
                    source_layout.has_specular ?
                    *(const DWORD *)(input + source_layout.specular_offset) : 0u;
        }
        for (stage = 0; stage < destination_layout.texcoord_count; ++stage) {
            UINT bytes = destination_layout.texcoord_bytes[stage];
            if (stage < source_layout.texcoord_count &&
                    source_layout.texcoord_bytes[stage] < bytes)
                bytes = source_layout.texcoord_bytes[stage];
            if (stage < source_layout.texcoord_count)
                CopyMemory(output + destination_layout.texcoord_offset[stage],
                        input + source_layout.texcoord_offset[stage], bytes);
        }
    }
    return d3d7_upload_vertex_range(destination,
            destination_index * destination->stride,
            count * destination->stride) ? D3D_OK :
            D3DERR_VERTEXBUFFERUNLOCKFAILED;
}

static HRESULT WINAPI d3d7_vb_ProcessVertices(IDirect3DVertexBuffer7 *iface,
        DWORD vertex_op, DWORD destination_index, DWORD count,
        IDirect3DVertexBuffer7 *source_iface, DWORD source_index,
        IDirect3DDevice7 *device, DWORD flags)
{
    D3D7VertexBuffer *destination = d3d7_vb_from_iface(iface);
    D3D7VertexBuffer *source;
    D3D7Device *d3d_device;
    (void)flags;
    if (!source_iface || source_iface->lpVtbl != &g_d3d7_vb_vtbl ||
            !device || device->lpVtbl != &g_d3d7_device_vtbl)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    source = d3d7_vb_from_iface(source_iface);
    d3d_device = d3d7_device_from_iface(device);
    if (source->owner != destination->owner ||
            d3d_device->owner != destination->owner || source->locked ||
            source_index > source->desc.dwNumVertices ||
            count > source->desc.dwNumVertices - source_index)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    return d3d7_process_vertex_bytes(destination, vertex_op,
            destination_index, count, source->shadow, source->desc.dwFVF,
            source->stride, source_index, d3d_device);
}

static HRESULT WINAPI d3d7_vb_GetVertexBufferDesc(
        IDirect3DVertexBuffer7 *iface, D3DVERTEXBUFFERDESC *description)
{
    if (!description || description->dwSize != sizeof(*description))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    *description = d3d7_vb_from_iface(iface)->desc;
    return D3D_OK;
}

static HRESULT WINAPI d3d7_vb_Optimize(IDirect3DVertexBuffer7 *iface,
        IDirect3DDevice7 *device, DWORD flags)
{
    D3D7VertexBuffer *buffer = d3d7_vb_from_iface(iface);
    (void)flags;
    if (!device || d3d7_device_from_iface(device)->owner != buffer->owner)
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    buffer->desc.dwCaps |= D3DVBCAPS_OPTIMIZED;
    return D3D_OK;
}

static HRESULT WINAPI d3d7_vb_ProcessVerticesStrided(
        IDirect3DVertexBuffer7 *iface, DWORD vertex_op,
        DWORD destination_index, DWORD count,
        D3DDRAWPRIMITIVESTRIDEDDATA *data, DWORD fvf,
        IDirect3DDevice7 *device, DWORD flags)
{
    D3D7VertexBuffer *destination = d3d7_vb_from_iface(iface);
    D3D7Device *d3d_device;
    BYTE *packed;
    UINT bytes;
    D3D7FVFLayout layout;
    D9WGVertexElement elements[D3D7_MAX_VERTEX_ELEMENTS];
    UINT element_count = 0;
    HRESULT result;
    (void)flags;
    if (!device || device->lpVtbl != &g_d3d7_device_vtbl ||
            d3d7_device_from_iface(device)->owner != destination->owner ||
            !d3d7_decode_fvf(fvf, &layout, elements, &element_count))
        return DDWG_TRACE_ERROR(DDERR_INVALIDPARAMS);
    d3d_device = d3d7_device_from_iface(device);
    packed = d3d7_pack_strided(fvf, data, count, &bytes);
    (void)bytes;
    if (!packed) return DDWG_TRACE_ERROR(D3DERR_INVALIDVERTEXFORMAT);
    result = d3d7_process_vertex_bytes(destination, vertex_op,
            destination_index, count, packed, fvf, layout.stride, 0,
            d3d_device);
    heap_free(packed);
    return DDWG_TRACE_ERROR(result);
}

static IDirect3D7Vtbl g_d3d7_vtbl = {
    d3d7_QueryInterface, d3d7_AddRef, d3d7_Release, d3d7_EnumDevices,
    d3d7_CreateDevice, d3d7_CreateVertexBuffer, d3d7_EnumZBufferFormats,
    d3d7_EvictManagedTextures,
};

static IDirect3DDevice7Vtbl g_d3d7_device_vtbl = {
    d3d7_device_QueryInterface, d3d7_device_AddRef, d3d7_device_Release,
    d3d7_device_GetCaps, d3d7_device_EnumTextureFormats,
    d3d7_device_BeginScene, d3d7_device_EndScene,
    d3d7_device_GetDirect3D, d3d7_device_SetRenderTarget,
    d3d7_device_GetRenderTarget, d3d7_device_Clear,
    d3d7_device_SetTransform, d3d7_device_GetTransform,
    d3d7_device_SetViewport, d3d7_device_MultiplyTransform,
    d3d7_device_GetViewport, d3d7_device_SetMaterial,
    d3d7_device_GetMaterial, d3d7_device_SetLight, d3d7_device_GetLight,
    d3d7_device_SetRenderState, d3d7_device_GetRenderState,
    d3d7_device_BeginStateBlock, d3d7_device_EndStateBlock,
    d3d7_device_PreLoad, d3d7_device_DrawPrimitive,
    d3d7_device_DrawIndexedPrimitive, d3d7_device_SetClipStatus,
    d3d7_device_GetClipStatus, d3d7_device_DrawPrimitiveStrided,
    d3d7_device_DrawIndexedPrimitiveStrided, d3d7_device_DrawPrimitiveVB,
    d3d7_device_DrawIndexedPrimitiveVB,
    d3d7_device_ComputeSphereVisibility, d3d7_device_GetTexture,
    d3d7_device_SetTexture, d3d7_device_GetTextureStageState,
    d3d7_device_SetTextureStageState, d3d7_device_ValidateDevice,
    d3d7_device_ApplyStateBlock, d3d7_device_CaptureStateBlock,
    d3d7_device_DeleteStateBlock, d3d7_device_CreateStateBlock,
    d3d7_device_Load, d3d7_device_LightEnable,
    d3d7_device_GetLightEnable, d3d7_device_SetClipPlane,
    d3d7_device_GetClipPlane, d3d7_device_GetInfo,
};

static IDirect3DVertexBuffer7Vtbl g_d3d7_vb_vtbl = {
    d3d7_vb_QueryInterface, d3d7_vb_AddRef, d3d7_vb_Release,
    d3d7_vb_Lock, d3d7_vb_Unlock, d3d7_vb_ProcessVertices,
    d3d7_vb_GetVertexBufferDesc, d3d7_vb_Optimize,
    d3d7_vb_ProcessVerticesStrided,
};
