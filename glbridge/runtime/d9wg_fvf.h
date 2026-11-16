#ifndef D9WG_FVF_H
#define D9WG_FVF_H
#include <stdint.h>
#include "../d3d9proxy/d3d9_protocol.h"

/* API-specific FVF meanings stay explicit: D3D7's reserved DWORD is D3D8/9's
 * point size; D3D8 lacks XYZW and LASTBETA_D3DCOLOR; D3D7 has no indexed beta. */
#define D9WG_FVF_D3D7 7u
#define D9WG_FVF_D3D8 8u
#define D9WG_FVF_D3D9 9u

typedef struct D9WGFVFLayout {
    uint32_t stride, position_offset, position_bytes;
    uint32_t normal_offset, diffuse_offset, specular_offset;
    uint32_t texcoord_offset[8], texcoord_bytes[8], texcoord_count;
    uint32_t has_position, has_normal, has_diffuse, has_specular;
} D9WGFVFLayout;

/* Pure C, with no Windows types, API objects, allocation or CRT dependencies.
 * Layout is optional. Callers must ignore outputs when decoding fails. */
int d9wg_fvf_decode(uint32_t api, uint32_t fvf, D9WGVertexElement *elements,
        uint32_t capacity, uint32_t *element_count, D9WGFVFLayout *layout);
#endif
