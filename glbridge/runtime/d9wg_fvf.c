#include "d9wg_fvf.h"

/* D3D wire declaration values, independent of a particular DirectX SDK. */
enum { FLOAT1 = 0, FLOAT2 = 1, FLOAT3 = 2, FLOAT4 = 3, COLOUR = 4, UBYTE4 = 5 };
enum { POSITION = 0, BLENDWEIGHT = 1, BLENDINDICES = 2, NORMAL = 3,
       PSIZE = 4, TEXCOORD = 5, POSITIONT = 9, COLOR = 10 };

int d9wg_fvf_decode(uint32_t api, uint32_t fvf, D9WGVertexElement *elements,
        uint32_t capacity, uint32_t *element_count, D9WGFVFLayout *layout)
{
    static const uint8_t tex_types[4] = { FLOAT2, FLOAT3, FLOAT4, FLOAT1 };
    static const uint32_t tex_bytes[4] = { 8u, 12u, 16u, 4u };
    uint32_t position = fvf & (api == D9WG_FVF_D3D9 ? 0x400eu : 0x000eu);
    uint32_t offset = 0, count = 0, index, tex_count;
    if (layout) {
        /* Avoid a C runtime memset in the private freestanding archive. */
        layout->stride = layout->position_offset = layout->position_bytes = 0;
        layout->normal_offset = layout->diffuse_offset = layout->specular_offset = 0;
        layout->texcoord_count = 0;
        layout->has_position = layout->has_normal = layout->has_diffuse = layout->has_specular = 0;
        for (index = 0; index < 8; ++index) layout->texcoord_offset[index] = layout->texcoord_bytes[index] = 0;
    }
    if (!elements || !element_count || (api != 7 && api != 8 && api != 9) || (fvf & 1u)) return 0;
#define PUSH(type_value, usage_value, usage_index_value) do { \
    if (count >= capacity || offset > 0xffffu) return 0; \
    elements[count].stream = 0;elements[count].offset = (uint16_t)offset; \
    elements[count].type = (uint8_t)(type_value);elements[count].method = 0; \
    elements[count].usage = (uint8_t)(usage_value);elements[count].usage_index = (uint8_t)(usage_index_value); \
    ++count; \
} while (0)
    if (position == 2u || position == 4u || (api == 9 && position == 0x4002u)) {
        uint32_t bytes = position == 2u ? 12u : 16u;
        if (layout) { layout->has_position = 1;layout->position_bytes = bytes; }
        PUSH(bytes == 12u ? FLOAT3 : FLOAT4, position == 4u ? POSITIONT : POSITION, 0);
        offset += bytes;
    } else if (position >= 6u && position <= (api == 7 ? 12u : 14u)) {
        uint32_t betas = 1u + ((position - 6u) >> 1);
        uint32_t indexed = api != 7 && ((fvf & 0x1000u) || (api == 9 && (fvf & 0x8000u)) || position == 14u);
        uint32_t weights = betas - (indexed ? 1u : 0u);
        if (layout) { layout->has_position = 1;layout->position_bytes = 12u + betas * 4u; }
        PUSH(FLOAT3, POSITION, 0);offset += 12u;
        if (weights) { PUSH(FLOAT1 + weights - 1u, BLENDWEIGHT, 0);offset += weights * 4u; }
        if (indexed) { PUSH(api == 9 && (fvf & 0x8000u) ? COLOUR : UBYTE4, BLENDINDICES, 0);offset += 4u; }
    } else if (api == 7) return 0;
    /* D3D8/9 programmable declarations may carry attributes without position. */
    if (fvf & 0x10u) {
        if (layout) { layout->has_normal = 1;layout->normal_offset = offset; }
        PUSH(FLOAT3, NORMAL, 0);offset += 12u;
    }
    if (fvf & 0x20u) {
        if (api != 7) { PUSH(FLOAT1, PSIZE, 0); }
        offset += 4u;
    }
    if (fvf & 0x40u) {
        if (layout) { layout->has_diffuse = 1;layout->diffuse_offset = offset; }
        PUSH(COLOUR, COLOR, 0);offset += 4u;
    }
    if (fvf & 0x80u) {
        if (layout) { layout->has_specular = 1;layout->specular_offset = offset; }
        PUSH(COLOUR, COLOR, 1);offset += 4u;
    }
    tex_count = (fvf >> 8u) & 15u;
    if (layout) layout->texcoord_count = tex_count;
    if (tex_count > 8u) return 0;
    for (index = 0; index < tex_count; ++index) {
        uint32_t code = (fvf >> (16u + index * 2u)) & 3u;
        if (layout) { layout->texcoord_offset[index] = offset;layout->texcoord_bytes[index] = tex_bytes[code]; }
        PUSH(tex_types[code], TEXCOORD, index);offset += tex_bytes[code];
    }
#undef PUSH
    if (!count) return 0;
    if (layout) layout->stride = offset;
    *element_count = count;
    return 1;
}
