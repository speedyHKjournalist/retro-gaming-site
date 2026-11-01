#ifndef D9WG_VERTEX_H
#define D9WG_VERTEX_H
#include "d9wg_lighting.h"
#define D9WG_PV_DONOTCOPYDATA 1u

typedef struct D9WGProcessLayout {
    int32_t position, normal, diffuse, specular, psize;
    uint32_t position_transformed, position_floats, stride, texcoord_count;
    int32_t texcoord[8];
    uint32_t texcoord_floats[8];
    uint32_t has_blend;
} D9WGProcessLayout;
typedef struct D9WGProcessViewport {
    float x, y, width, height, min_z, max_z;
} D9WGProcessViewport;
typedef struct D9WGVertexState {
    uint32_t fog_mode, range_fog;
    float fog_start, fog_end, fog_density;
} D9WGVertexState;
void d9wg_vertex_state_init(D9WGVertexState *state, const void *render_states);
float d9wg_vertex_fog(const D9WGVertexState *state, const float *eye_position);

/* A canonical, SDK-independent subset for fixed-function software processing.
 * Packed programmable attributes and multiple streams remain explicit failures. */
int d9wg_process_layout(const D9WGVertexElement *elements, uint32_t count, D9WGProcessLayout *layout);
int d9wg_process_fvf(uint32_t api, uint32_t fvf, D9WGProcessLayout *layout);
/* Validate without adding/multiplying untrusted 32-bit indices first. The
 * final source vertex needs only its actual attributes, not trailing padding. */
int d9wg_process_range(uint32_t length, uint32_t base, uint32_t stride,
        uint32_t extent, uint32_t start, uint32_t count, uint32_t *first);
/* Source/destination storage must not overlap. All ranges are checked before
 * the first output write. The frontend snapshots an aliased source buffer. */
int d9wg_process_vertices(const D9WGProcessLayout *source_layout,
        const void *source, uint32_t source_length, uint32_t source_base,
        uint32_t source_stride, uint32_t source_start,
        const D9WGProcessLayout *output_layout, void *output, uint32_t output_length,
        uint32_t output_start, uint32_t count, const void *world_view,
        const void *world_view_projection, const D9WGProcessViewport *viewport,
        const D9WGTNLLighting *lighting, uint32_t copy_data, const D9WGVertexState *vertex_state);
#endif
