#ifndef D9WG_CLIP_DRAW_H
#define D9WG_CLIP_DRAW_H
#include "d9wg_vertex.h"
typedef struct D9WGClipDrawSource {
    D9WGClipBuffer *clips;
    const uint8_t *vertices, *indices;
    uint32_t length, base, stride, first_vertex, vertex_count, min_vertex;
    uint32_t count, index_bytes, index_stride, primitive_type, flat_shading;
    int32_t base_vertex;
    const D9WGProcessLayout *layout;
    D9WGProcessViewport viewport;
} D9WGClipDrawSource;
typedef int (*D9WGClipEmit)(void *context, uint32_t primitive_type,
        const void *vertices, uint32_t count, uint32_t stride);
/* 0: no live processing records, retain the ordinary draw path; 1: handled;
 * -1: invalid range/layout/geometry; -2: transport emission failed. */
int d9wg_clip_draw(const D9WGClipDrawSource *source, D9WGClipWork *work,
        void *staging, uint32_t staging_bytes, D9WGClipEmit emit, void *context);
#endif
