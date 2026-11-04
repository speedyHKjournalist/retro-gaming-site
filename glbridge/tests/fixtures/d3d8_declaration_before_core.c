/* ------------------------------------------------------------------ *
 * Vertex declarations
 * ------------------------------------------------------------------ *
 *
 * D3D8 declares vertex shader inputs as a D3DVSD_* token stream that binds
 * stream data to explicit vertex register numbers (v0..v15).  D3D9 declares
 * them as D3DVERTEXELEMENT9 entries that bind stream data to *usages*
 * (POSITION, NORMAL, TEXCOORD[n], ...), and the shader's own dcl_ statements
 * connect a usage to a register.
 *
 * vs_1_1 bytecode carries no dcl_ statements at all, so the register->usage
 * association it relies on is exactly the one its declaration established.
 * The guest therefore synthesises a usage for each declared register using
 * D3D8's own fixed register semantics (see the "Vertex Shader Registers"
 * table in the D3D8 SDK), which is what a D3D8 app's declaration means:
 */
#define D3D8_VSD_REG_POSITION 0u
#define D3D8_VSD_REG_BLENDWEIGHT 1u
#define D3D8_VSD_REG_BLENDINDICES 2u
#define D3D8_VSD_REG_NORMAL 3u
#define D3D8_VSD_REG_POINTSIZE 4u
#define D3D8_VSD_REG_DIFFUSE 5u
#define D3D8_VSD_REG_SPECULAR 6u
#define D3D8_VSD_REG_TEXCOORD0 7u
#define D3D8_VSD_REG_TEXCOORD7 14u
#define D3D8_VSD_REG_POSITION2 15u
#define D3D8_VSD_REG_NORMAL2 16u

/* D3DVSD token field layout (d3d8types.h). */
#define D3D8VSD_TOKENTYPESHIFT 29u
#define D3D8VSD_TOKENTYPEMASK (0x7u << D3D8VSD_TOKENTYPESHIFT)
#define D3D8VSD_TOKEN_NOP 0u
#define D3D8VSD_TOKEN_STREAM 1u
#define D3D8VSD_TOKEN_STREAMDATA 2u
#define D3D8VSD_TOKEN_TESSELLATOR 3u
#define D3D8VSD_TOKEN_CONSTMEM 4u
#define D3D8VSD_TOKEN_EXT 5u
#define D3D8VSD_TOKEN_END 7u
#define D3D8VSD_STREAMNUMBERMASK 0xFu
#define D3D8VSD_VERTEXREGMASK 0x1Fu
#define D3D8VSD_DATATYPESHIFT 16u
#define D3D8VSD_DATATYPEMASK (0xFu << D3D8VSD_DATATYPESHIFT)
/* D3DVSD_SKIP sets bit 28 of a STREAMDATA token; the DWORD count it skips
 * sits in the same field a REG token uses for its data type. */
#define D3D8VSD_SKIPFLAG 0x10000000u
#define D3D8VSD_STREAM_TESSFLAG 0x10000000u
#define D3D8VSD_SKIPCOUNTSHIFT 16u
#define D3D8VSD_SKIPCOUNTMASK (0xFu << D3D8VSD_SKIPCOUNTSHIFT)
#define D3D8VSD_CONSTCOUNTSHIFT 25u
#define D3D8VSD_CONSTCOUNTMASK (0xFu << D3D8VSD_CONSTCOUNTSHIFT)
#define D3D8VSD_CONSTADDRESSMASK 0x7Fu

/* D3DVSDT_* data types, and the D3DDECLTYPE_* they become. */
#define D3D8VSDT_FLOAT1 0u
#define D3D8VSDT_FLOAT2 1u
#define D3D8VSDT_FLOAT3 2u
#define D3D8VSDT_FLOAT4 3u
#define D3D8VSDT_D3DCOLOR 4u
#define D3D8VSDT_UBYTE4 5u
#define D3D8VSDT_SHORT2 6u
#define D3D8VSDT_SHORT4 7u

/* D3DDECLTYPE_*, named here because d3d8.h does not declare them. */
#define D3D9DECLTYPE_FLOAT1 0u
#define D3D9DECLTYPE_FLOAT2 1u
#define D3D9DECLTYPE_FLOAT3 2u
#define D3D9DECLTYPE_FLOAT4 3u
#define D3D9DECLTYPE_D3DCOLOR 4u
#define D3D9DECLTYPE_UBYTE4 5u
#define D3D9DECLTYPE_SHORT2 6u
#define D3D9DECLTYPE_SHORT4 7u
#define D3D9DECLTYPE_UNUSED 17u

#define D3D9DECLMETHOD_DEFAULT 0u

#define D3D9DECLUSAGE_POSITION 0u
#define D3D9DECLUSAGE_BLENDWEIGHT 1u
#define D3D9DECLUSAGE_BLENDINDICES 2u
#define D3D9DECLUSAGE_NORMAL 3u
#define D3D9DECLUSAGE_PSIZE 4u
#define D3D9DECLUSAGE_TEXCOORD 5u
#define D3D9DECLUSAGE_TANGENT 6u
#define D3D9DECLUSAGE_BINORMAL 7u
#define D3D9DECLUSAGE_TESSFACTOR 8u
#define D3D9DECLUSAGE_POSITIONT 9u
#define D3D9DECLUSAGE_COLOR 10u
#define D3D9DECLUSAGE_FOG 11u
#define D3D9DECLUSAGE_DEPTH 12u
#define D3D9DECLUSAGE_SAMPLE 13u

/* Byte size of each D3DVSDT_* type, indexed by the type value. */
static __inline unsigned d3d8_vsdt_size(unsigned type)
{
    switch (type) {
    case D3D8VSDT_FLOAT1: return 4u;
    case D3D8VSDT_FLOAT2: return 8u;
    case D3D8VSDT_FLOAT3: return 12u;
    case D3D8VSDT_FLOAT4: return 16u;
    case D3D8VSDT_D3DCOLOR: return 4u;
    case D3D8VSDT_UBYTE4: return 4u;
    case D3D8VSDT_SHORT2: return 4u;
    case D3D8VSDT_SHORT4: return 8u;
    default: return 0u;
    }
}

/*
 * D3DVSDT_* and D3DDECLTYPE_* happen to agree on 0..7, but that is a
 * coincidence of two independently-written enums rather than a guarantee, so
 * the mapping is spelled out instead of assumed.
 */
static __inline unsigned d3d8_vsdt_to_decltype(unsigned type)
{
    switch (type) {
    case D3D8VSDT_FLOAT1: return D3D9DECLTYPE_FLOAT1;
    case D3D8VSDT_FLOAT2: return D3D9DECLTYPE_FLOAT2;
    case D3D8VSDT_FLOAT3: return D3D9DECLTYPE_FLOAT3;
    case D3D8VSDT_FLOAT4: return D3D9DECLTYPE_FLOAT4;
    case D3D8VSDT_D3DCOLOR: return D3D9DECLTYPE_D3DCOLOR;
    case D3D8VSDT_UBYTE4: return D3D9DECLTYPE_UBYTE4;
    case D3D8VSDT_SHORT2: return D3D9DECLTYPE_SHORT2;
    case D3D8VSDT_SHORT4: return D3D9DECLTYPE_SHORT4;
    default: return D3D9DECLTYPE_UNUSED;
    }
}

/*
 * The register -> (usage, usage_index) association a D3D8 declaration implies.
 * Registers v15/v16 carry the second tween position/normal.  Anything else is given a TEXCOORD usage with a usage index that
 * cannot collide with a real texture coordinate set, so a shader that used a
 * register purely as an opaque input still receives its data.
 */
static __inline void d3d8_vsd_register_usage(unsigned reg,
        unsigned *usage, unsigned *usage_index)
{
    switch (reg) {
    case D3D8_VSD_REG_POSITION:
        *usage = D3D9DECLUSAGE_POSITION; *usage_index = 0u; return;
    case D3D8_VSD_REG_BLENDWEIGHT:
        *usage = D3D9DECLUSAGE_BLENDWEIGHT; *usage_index = 0u; return;
    case D3D8_VSD_REG_BLENDINDICES:
        *usage = D3D9DECLUSAGE_BLENDINDICES; *usage_index = 0u; return;
    case D3D8_VSD_REG_NORMAL:
        *usage = D3D9DECLUSAGE_NORMAL; *usage_index = 0u; return;
    case D3D8_VSD_REG_POINTSIZE:
        *usage = D3D9DECLUSAGE_PSIZE; *usage_index = 0u; return;
    case D3D8_VSD_REG_DIFFUSE:
        *usage = D3D9DECLUSAGE_COLOR; *usage_index = 0u; return;
    case D3D8_VSD_REG_SPECULAR:
        *usage = D3D9DECLUSAGE_COLOR; *usage_index = 1u; return;
    case D3D8_VSD_REG_POSITION2:
        *usage = D3D9DECLUSAGE_POSITION; *usage_index = 1u; return;
    case D3D8_VSD_REG_NORMAL2:
        *usage = D3D9DECLUSAGE_NORMAL; *usage_index = 1u; return;
    default:
        if (reg >= D3D8_VSD_REG_TEXCOORD0 && reg <= D3D8_VSD_REG_TEXCOORD7) {
            *usage = D3D9DECLUSAGE_TEXCOORD;
            *usage_index = reg - D3D8_VSD_REG_TEXCOORD0;
            return;
        }
        *usage = D3D9DECLUSAGE_TEXCOORD;
        *usage_index = 8u + (reg & 0x7u);
        return;
    }
}


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
        /* Literal DWORDs may themselves equal END or resemble declaration
         * tokens. They are data, so only inspect the next token after them. */
        UINT words = token_type == D3DVSD_TOKEN_CONSTMEM
                ? 1u + ((token & D3DVSD_CONSTCOUNTMASK)
                        >> D3DVSD_CONSTCOUNTSHIFT) * 4u : 1u;
        if (words > max_tokens - offset)
            return FALSE;
        offset += words;
    }
    return FALSE;
}

static BOOL declaration_to_elements(const DWORD *tokens, UINT token_count,
        D9WGVertexElement *elements, UINT max_elements, UINT *element_count,
        DWORD *const_data, UINT max_const_vectors, BYTE *const_mask,
        UINT *const_vector_count)
{
    UINT offsets[D8WG_MAX_STREAMS];
    UINT stream = 0;
    UINT count = 0;
    UINT i;

    ZeroMemory(offsets, sizeof(offsets));
    *element_count = 0;
    ZeroMemory(const_mask, max_const_vectors * sizeof(*const_mask));
    *const_vector_count = 0;

    for (i = 0; i < token_count; ++i) {
        DWORD token = tokens[i];
        DWORD type = (token & D3D8VSD_TOKENTYPEMASK) >> D3D8VSD_TOKENTYPESHIFT;

        if (token == 0xFFFFFFFFu)
            break;
        switch (type) {
        case D3D8VSD_TOKEN_NOP:
            break;
        case D3D8VSD_TOKEN_STREAM:
            /* The tessellator variant drives an N-patch source, which this
             * path does not implement; see fill_caps(). */
            if (token & D3D8VSD_STREAM_TESSFLAG)
                return FALSE;
            stream = token & D3D8VSD_STREAMNUMBERMASK;
            if (stream >= D8WG_MAX_STREAMS)
                return FALSE;
            break;
        case D3D8VSD_TOKEN_STREAMDATA:
            if (token & D3D8VSD_SKIPFLAG) {
                UINT skip = (token & D3D8VSD_SKIPCOUNTMASK)
                        >> D3D8VSD_SKIPCOUNTSHIFT;
                offsets[stream] += skip * (UINT)sizeof(DWORD);
            } else {
                UINT reg = token & D3D8VSD_VERTEXREGMASK;
                UINT data_type = (token & D3D8VSD_DATATYPEMASK)
                        >> D3D8VSD_DATATYPESHIFT;
                UINT size = d3d8_vsdt_size(data_type);
                unsigned usage = 0;
                unsigned usage_index = 0;

                if (!size || count >= max_elements
                        || offsets[stream] > 0xFFFFu)
                    return FALSE;
                d3d8_vsd_register_usage(reg, &usage, &usage_index);
                elements[count].stream = (uint16_t)stream;
                elements[count].offset = (uint16_t)offsets[stream];
                elements[count].type =
                        (uint8_t)d3d8_vsdt_to_decltype(data_type);
                elements[count].method = (uint8_t)D3D9DECLMETHOD_DEFAULT;
                elements[count].usage = (uint8_t)usage;
                elements[count].usage_index = (uint8_t)usage_index;
                ++count;
                offsets[stream] += size;
            }
            break;
        case D3D8VSD_TOKEN_CONSTMEM: {
            /*
             * Each block contributes shader-local constants. Keep disjoint
             * ranges and holes separately so no device constant is overwritten.
             * A repeated register takes the last declaration block's value.
             */
            UINT vectors = (token & D3D8VSD_CONSTCOUNTMASK)
                    >> D3D8VSD_CONSTCOUNTSHIFT;
            UINT dwords = vectors * 4u;

            if (!vectors || vectors > max_const_vectors ||
                    (token & D3D8VSD_CONSTADDRESSMASK) > max_const_vectors - vectors
                    || i + dwords >= token_count)
                return FALSE;
            UINT start = token & D3D8VSD_CONSTADDRESSMASK;
            for (UINT reg = start; reg < start + vectors; ++reg) {
                if (!const_mask[reg]) ++*const_vector_count;
                const_mask[reg] = TRUE;
            }
            CopyMemory(const_data + start * 4u, &tokens[i + 1],
                    dwords * sizeof(DWORD));
            i += dwords;
            break;
        }
        case D3D8VSD_TOKEN_TESSELLATOR:
        case D3D8VSD_TOKEN_EXT:
        default:
            return FALSE;
        }
    }
    if (!count)
        return FALSE;
    *element_count = count;
    return TRUE;
}
