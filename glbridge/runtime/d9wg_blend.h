#ifndef D9WG_BLEND_H
#define D9WG_BLEND_H
#include "d9wg_vertex.h"
typedef struct D9WGVertexBlendMatrix {
    uint32_t ready;
    float world_view[16], world_view_projection[16], normal[16];
} D9WGVertexBlendMatrix;
struct D9WGVertexBlend {
    uint32_t matrix_count, indexed, palette_count, world_count, world_gap, tween;
    float tween_factor;
    const uint8_t *worlds;
    float view[16], projection[16];
    D9WGVertexBlendMatrix matrix[];
};
/* Caller-owned scratch memory, with lazy per-matrix preparation. world_gap
 * accounts for D3D7's view/projection slots between WORLD and WORLD1. */
uint32_t d9wg_blend_bytes(uint32_t mode, uint32_t indexed, uint32_t world_count);
int d9wg_blend_init(D9WGVertexBlend *blend, uint32_t bytes, uint32_t mode,
        uint32_t indexed, const void *worlds, uint32_t world_count, uint32_t world_gap,
        const void *view, const void *projection);
int d9wg_blend_layout(const D9WGVertexBlend *blend, const D9WGProcessLayout *layout);
int d9wg_blend_indices(const D9WGVertexBlend *blend, const D9WGProcessLayout *layout,
        const void *input, uint32_t *indices);
void d9wg_blend_vertex(D9WGVertexBlend *blend, const D9WGProcessLayout *layout,
        const void *input, const float *position, float *eye, float *clip, float *eye_normal);
void d9wg_blend_vertex_world(D9WGVertexBlend *blend, const D9WGProcessLayout *layout,
        const void *input, const float *position, float *eye, float *clip, float *eye_normal, float *world);
#endif
