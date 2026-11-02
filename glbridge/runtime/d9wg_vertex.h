#ifndef D9WG_VERTEX_H
#define D9WG_VERTEX_H
#include "d9wg_lighting.h"
#include "d9wg_clip.h"
#include "d9wg_vertex_format.h"
#define D9WG_PV_DONOTCOPYDATA 1u

typedef struct D9WGProcessLayout {
    int32_t position, normal, diffuse, specular, psize;
    uint32_t position_transformed, position_floats, stride, texcoord_count;
    uint32_t legacy_xyz_output, legacy_output, write_extent;
    uint32_t position_type, normal_type, psize_type, texcoord_type[8];
    uint32_t diffuse_type, specular_type;
    int32_t texcoord[8];
    uint32_t texcoord_floats[8];
    uint32_t has_blend;
    int32_t blend_weight, blend_indices;
    uint32_t blend_weight_floats, blend_weight_type, blend_indices_type;
} D9WGProcessLayout;
/* The current output writer and primitive clipper consume float geometry,
 * D3DCOLOR and float texture coordinates. Packed output is a separate path. */
static __inline int d9wg_process_canonical_output(const D9WGProcessLayout *layout)
{
    if (!layout || (layout->position_type != 3 && !layout->legacy_xyz_output) ||
            (layout->normal >= 0 && layout->normal_type != 2 && layout->normal_type != 3) ||
            (layout->psize >= 0 && layout->psize_type != 0) ||
            (layout->diffuse >= 0 && layout->diffuse_type != 4) ||
            (layout->specular >= 0 && layout->specular_type != 4)) return 0;
    for (unsigned i = 0; i < 8; ++i)
        if (layout->texcoord[i] >= 0 && layout->texcoord_type[i] > 3) return 0;
    return 1;
}
#define D9WG_VERTEX_MAX_STREAMS 16u
#define D9WG_VERTEX_MAX_ELEMENTS 64u
#define D9WG_VERTEX_GATHER_BYTES (D9WG_VERTEX_MAX_ELEMENTS*16u)
typedef struct D9WGVertexStream {
    const uint8_t *vertices;
    uint32_t length, base, stride;
} D9WGVertexStream;
typedef struct D9WGProcessStreamLayout {
    D9WGProcessLayout vertex;
    uint32_t count, stream_mask, extent[D9WG_VERTEX_MAX_STREAMS];
    D9WGVertexElement elements[D9WG_VERTEX_MAX_ELEMENTS];
} D9WGProcessStreamLayout;
/* Preserve each raw declaration type while gathering attributes from their
 * own streams into bounded 16-byte slots for the existing vertex kernel. */
int d9wg_process_stream_layout(const D9WGVertexElement *elements, uint32_t count,
        D9WGProcessStreamLayout *layout);
int d9wg_process_stream_range(uint32_t length, uint32_t base, uint32_t stride,
        uint32_t extent, uint32_t start, uint32_t count, uint32_t *first);
int d9wg_process_stream_validate(const D9WGProcessStreamLayout *layout,
        const D9WGVertexStream *streams, uint32_t start, uint32_t count, uint32_t *first);
int d9wg_process_stream_gather(const D9WGProcessStreamLayout *layout,
        const D9WGVertexStream *streams, uint32_t vertex, void *output);
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

/* SDK-independent fixed-function source geometry, including packed attributes.
 * Packed output remains an explicit failure; use the stream layout for multiple streams. */
int d9wg_process_layout(const D9WGVertexElement *elements, uint32_t count, D9WGProcessLayout *layout);
int d9wg_process_fvf(uint32_t api, uint32_t fvf, D9WGProcessLayout *layout);
/* D3D7 RGB writes compact XYZRHW / color / float2 texture output using
 * the nominal FVF stride, even for XYZ / NORMAL declarations. Native spill
 * writes stay bounded by output_length. */
int d9wg_process_legacy_output(uint32_t fvf, D9WGProcessLayout *layout);
/* Validate without adding/multiplying untrusted 32-bit indices first. The
 * final source vertex needs only its actual attributes, not trailing padding. */
int d9wg_process_range(uint32_t length, uint32_t base, uint32_t stride,
        uint32_t extent, uint32_t start, uint32_t count, uint32_t *first);
struct D9WGClipDrawSource;
int d9wg_vertex_clip_codes(const struct D9WGClipDrawSource *source,
        const void *world_view_projection, const D9WGVertexState *state, D9WGClipStatus *status);
int d9wg_clip_stream_output(const D9WGProcessStreamLayout *source,
        D9WGVertexElement *elements, D9WGProcessLayout *output);
void d9wg_clip_draw_streams_init(struct D9WGClipDrawSource *source,
        const D9WGProcessStreamLayout *layout, const D9WGVertexStream *streams);
int d9wg_vertex_stream_clip_codes(const D9WGProcessStreamLayout *layout,
        const D9WGVertexStream *streams, const struct D9WGClipDrawSource *draw,
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
int d9wg_process_vertices_streams(const D9WGProcessStreamLayout *source_layout,
        const D9WGVertexStream *streams, uint32_t source_start,
        const D9WGProcessLayout *output_layout, void *output, uint32_t output_length,
        uint32_t output_start, uint32_t count, const void *world_view,
        const void *world_view_projection, const D9WGProcessViewport *viewport,
        const D9WGTNLLighting *lighting, uint32_t copy_data, const D9WGVertexState *vertex_state);
#endif
