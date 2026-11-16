#ifndef D9WG_DECLARATION8_H
#define D9WG_DECLARATION8_H
#include <stdint.h>
#include <string.h>
#include "../d3d9proxy/d3d9_protocol.h"
#include "d9wg_declaration8_policy.h"
/* DWORD and uint32_t can be distinct C types in the Win32 headers. Load
 * token words through bytes, and copy constants as bytes, so SDK8/9 callers
 * share this implementation without casts or strict-aliasing assumptions. */
static __inline uint32_t d9wg_declaration8_word(const void *bytes, uint32_t index)
{
    uint32_t word;
    memcpy(&word, (const uint8_t *)bytes + index * 4u, sizeof(word));
    return word;
}
/* Legacy8 declaration parsing and layout conversion. Counts exclude END;
 * literal constant DWORDs are never scanned as declaration tokens. Callers
 * validate pointers and retain the raw8 tokens/constant metadata they own. */
static __inline int d9wg_declaration8_validate(const void *tokens, uint32_t max_tokens,
        uint32_t *token_count)
{
    uint32_t offset = 0;
    while (offset < max_tokens) {
        uint32_t token = d9wg_declaration8_word(tokens, offset);
        uint32_t token_type;
        if (token == 0xFFFFFFFFu) {
            *token_count = offset;
            return 1;
        }
        token_type = (token >> D3D8VSD_TOKENTYPESHIFT) & 0x7u;
        if (token_type > (uint32_t)D3D8VSD_TOKEN_END)
            return 0;
        /* Literal DWORDs may themselves equal END or resemble declaration
         * tokens. They are data, so only inspect the next token after them. */
        uint32_t words = token_type == D3D8VSD_TOKEN_CONSTMEM
                ? 1u + ((token & D3D8VSD_CONSTCOUNTMASK)
                        >> D3D8VSD_CONSTCOUNTSHIFT) * 4u : 1u;
        if (words > max_tokens - offset)
            return 0;
        offset += words;
    }
    return 0;
}

static __inline int d9wg_declaration8_elements(const void *tokens, uint32_t token_count,
        D9WGVertexElement *elements, uint32_t max_elements, uint32_t *element_count,
        void *const_data, uint32_t max_const_vectors, uint8_t *const_mask,
        uint32_t *const_vector_count)
{
    uint32_t offsets[16u];
    uint32_t stream = 0;
    uint32_t count = 0;
    uint32_t i;

    memset(offsets, 0, sizeof(offsets));
    *element_count = 0;
    memset(const_mask, 0, max_const_vectors * sizeof(*const_mask));
    *const_vector_count = 0;

    for (i = 0; i < token_count; ++i) {
        uint32_t token = d9wg_declaration8_word(tokens, i);
        uint32_t type = (token & D3D8VSD_TOKENTYPEMASK) >> D3D8VSD_TOKENTYPESHIFT;

        if (token == 0xFFFFFFFFu)
            break;
        switch (type) {
        case D3D8VSD_TOKEN_NOP:
            break;
        case D3D8VSD_TOKEN_STREAM:
            /* The tessellator variant drives an N-patch source, which this
             * path does not implement; see fill_caps(). */
            if (token & D3D8VSD_STREAM_TESSFLAG)
                return 0;
            stream = token & D3D8VSD_STREAMNUMBERMASK;
            if (stream >= 16u)
                return 0;
            break;
        case D3D8VSD_TOKEN_STREAMDATA:
            if (token & D3D8VSD_SKIPFLAG) {
                uint32_t skip = (token & D3D8VSD_SKIPCOUNTMASK)
                        >> D3D8VSD_SKIPCOUNTSHIFT;
                offsets[stream] += skip * (uint32_t)sizeof(uint32_t);
            } else {
                uint32_t reg = token & D3D8VSD_VERTEXREGMASK;
                uint32_t data_type = (token & D3D8VSD_DATATYPEMASK)
                        >> D3D8VSD_DATATYPESHIFT;
                uint32_t size = d3d8_vsdt_size(data_type);
                unsigned usage = 0;
                unsigned usage_index = 0;

                if (!size || count >= max_elements
                        || offsets[stream] > 0xFFFFu)
                    return 0;
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
            uint32_t vectors = (token & D3D8VSD_CONSTCOUNTMASK)
                    >> D3D8VSD_CONSTCOUNTSHIFT;
            uint32_t dwords = vectors * 4u;

            if (!vectors || vectors > max_const_vectors ||
                    (token & D3D8VSD_CONSTADDRESSMASK) > max_const_vectors - vectors
                    || i + dwords >= token_count)
                return 0;
            uint32_t start = token & D3D8VSD_CONSTADDRESSMASK;
            for (uint32_t reg = start; reg < start + vectors; ++reg) {
                if (!const_mask[reg]) ++*const_vector_count;
                const_mask[reg] = 1;
            }
            memcpy((uint8_t *)const_data + start * 16u,
                    (const uint8_t *)tokens + (i + 1u) * 4u,
                    dwords * sizeof(uint32_t));
            i += dwords;
            break;
        }
        case D3D8VSD_TOKEN_TESSELLATOR:
        case D3D8VSD_TOKEN_EXT:
        default:
            return 0;
        }
    }
    if (!count)
        return 0;
    *element_count = count;
    return 1;
}
#endif
