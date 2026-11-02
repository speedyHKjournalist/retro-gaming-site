#ifndef D9WG_CLIP_DRAW_H
#define D9WG_CLIP_DRAW_H
#include "d9wg_vertex.h"
typedef struct D9WGClipDrawSource {
    D9WGClipBuffer *clips;
    const uint8_t *vertices, *indices;
    /* min_vertex/vertex_count bound raw indices [min, max); first/base_vertex
     * shift that range within the buffer. vertex_count is not a span length. */
    uint32_t length, base, stride, first_vertex, vertex_count, min_vertex;
    /* Immediate XYZRHW draws process clip codes; buffer XYZRHW draws reuse
     * earlier processing and leave ClipStatus alone. */
    uint32_t count, index_bytes, index_stride, primitive_type, flat_shading, status_screen;
    int32_t base_vertex;
    const D9WGProcessLayout *layout;
    D9WGProcessViewport viewport;
} D9WGClipDrawSource;
static __inline uint32_t d9wg_clip_read_index(const D9WGClipDrawSource *s, uint32_t i)
{
    uint32_t index = 0;
    if (s->indices) __builtin_memcpy(&index, s->indices + (uint64_t)i*s->index_stride, s->index_stride);
    else index = i;
    return index;
}
static __inline int d9wg_clip_vertex_offset(const D9WGClipDrawSource *s, uint32_t i, uint32_t *offset)
{
    uint32_t index = d9wg_clip_read_index(s, i);
    int64_t vertex = (int64_t)s->first_vertex + s->base_vertex + index;
    if (index < s->min_vertex || index >= s->vertex_count || vertex < 0) return 0;
    uint64_t at = (uint64_t)s->base + (uint64_t)vertex*s->stride;
    if (at > s->length || s->layout->stride > s->length - at) return 0;
    *offset = (uint32_t)at;return 1;
}
typedef int (*D9WGClipEmit)(void *context, uint32_t primitive_type,
        const void *vertices, uint32_t count, uint32_t stride);
/* 0: no live processing records, retain the ordinary draw path; 1: handled;
 * -1: invalid range/layout/geometry; -2: transport emission failed. */
int d9wg_clip_draw(const D9WGClipDrawSource *source, D9WGClipWork *work,
        void *staging, uint32_t staging_bytes, D9WGClipEmit emit, void *context);
#endif
