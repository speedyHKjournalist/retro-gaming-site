#ifndef D9WG_VERTEX_H
#define D9WG_VERTEX_H
#include "d9wg_lighting.h"
#include "d9wg_clip.h"
#define D9WG_PV_DONOTCOPYDATA 1u

typedef struct D9WGProcessLayout {
    int32_t position, normal, diffuse, specular, psize;
    uint32_t position_transformed, position_floats, stride, texcoord_count;
    int32_t texcoord[8];
    uint32_t texcoord_floats[8];
    uint32_t has_blend;
    int32_t blend_weight, blend_indices;
    uint32_t blend_weight_floats, blend_weight_type, blend_indices_type;
} D9WGProcessLayout;
typedef struct D9WGVertexBlend D9WGVertexBlend;
typedef struct D9WGVertexState {
    uint32_t fog_mode, range_fog;
    float fog_start, fog_end, fog_density;
    uint32_t texture_mask;
    struct {
        uint32_t coordinate_index, flags;
        float matrix[16];
    } texture[8];
    D9WGVertexBlend *blend;
    D9WGClipVertex *clip_vertices;
    float clip_guard_band[4];
    uint32_t clip_plane_mask;
    float clip_world[16], clip_planes[6][4];
} D9WGVertexState;
void d9wg_vertex_state_init(D9WGVertexState *state, const void *render_states);
void d9wg_vertex_clip_planes_init(D9WGVertexState *state, uint32_t mask,
        const void *planes, const void *world);
int d9wg_vertex_texture_state(D9WGVertexState *state, uint32_t stage,
        uint32_t coordinate_index, uint32_t flags, const void *matrix);
int d9wg_vertex_textures_init(D9WGVertexState *state, const void *stage_states,
        uint32_t stage_bytes, const void *texture_matrices);
float d9wg_vertex_fog(const D9WGVertexState *state, const float *eye_position);

/* A canonical, SDK-independent subset for fixed-function software processing.
 * Packed programmable attributes and multiple streams remain explicit failures. */
int d9wg_process_layout(const D9WGVertexElement *elements, uint32_t count, D9WGProcessLayout *layout);
int d9wg_process_fvf(uint32_t api, uint32_t fvf, D9WGProcessLayout *layout);
/* Validate without adding/multiplying untrusted 32-bit indices first. The
 * final source vertex needs only its actual attributes, not trailing padding. */
int d9wg_process_range(uint32_t length, uint32_t base, uint32_t stride,
        uint32_t extent, uint32_t start, uint32_t count, uint32_t *first);
struct D9WGClipDrawSource;
int d9wg_vertex_clip_codes(const struct D9WGClipDrawSource *source,
        const void *world_view_projection, const D9WGVertexState *state, D9WGClipStatus *status);
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
