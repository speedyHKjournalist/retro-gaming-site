#ifndef D9WG_VERTEX_WINDOWS_H
#define D9WG_VERTEX_WINDOWS_H
#include <windows.h>
#include "d9wg_blend.h"
#include "d9wg_clip_draw.h"
#include "d9wg_math.h"

/* Windows allocation stays outside the native-testable math kernel. Return
 * 0 for ready/disabled, 1 for invalid state, 2 for allocation failure. */
static __inline uint32_t d9wg_vertex_windows_blend(D9WGVertexState *state, const void *render_states,
        const void *worlds, uint32_t world_count, uint32_t world_gap,
        const void *view, const void *projection)
{
    uint32_t mode, indexed, bytes;
    __builtin_memcpy(&mode, (const uint8_t *)render_states + 151u*4u, 4u);
    __builtin_memcpy(&indexed, (const uint8_t *)render_states + 167u*4u, 4u);
    if (!mode) return 0;
    bytes = d9wg_blend_bytes(mode, indexed, world_count);
    if (!bytes) return 1;
    state->blend = HeapAlloc(GetProcessHeap(), 0, bytes);
    if (!state->blend) return 2;
    if (!d9wg_blend_init(state->blend, bytes, mode, indexed, worlds, world_count, world_gap, view, projection)) {
        HeapFree(GetProcessHeap(), 0, state->blend);state->blend = NULL;return 1;
    }
    return 0;
}
static __inline void d9wg_vertex_windows_blend_free(D9WGVertexState *state)
{
    if (state->blend) { HeapFree(GetProcessHeap(), 0, state->blend);state->blend = NULL; }
}
static __inline uint32_t d9wg_vertex_windows_clip_status_inputs(const D9WGClipDrawSource *source,
        const D9WGProcessStreamLayout *stream_layout, const D9WGVertexStream *streams,
        const void *render_states, const void *worlds, uint32_t world_count, uint32_t world_gap,
        const void *view, const void *projection, const void *planes, D9WGClipStatus *status)
{
    uint32_t clipping, mask;
    __builtin_memcpy(&clipping, (const uint8_t *)render_states + 136u*4u, 4);
    if (source->layout->position_transformed && (!source->status_screen || !clipping)) return 0;
    if (!clipping) { status->clip_union = status->clip_intersection = 0;return 0; }
    __builtin_memcpy(&mask, (const uint8_t *)render_states + 152u*4u, 4);
    D9WGVertexState state;d9wg_vertex_state_init(&state, render_states);
    d9wg_vertex_clip_planes_init(&state, mask, planes, worlds);
    if (source->layout->position_transformed)
        return (stream_layout ? d9wg_vertex_stream_clip_codes(stream_layout, streams, source, projection, &state, status) :
                d9wg_vertex_clip_codes(source, projection, &state, status)) ? 0 : 1;
    uint32_t error = d9wg_vertex_windows_blend(&state, render_states, worlds, world_count, world_gap, view, projection);
    if (error) return error;
    float world_view[16], world_view_projection[16];
    d9wg_multiply_matrix(worlds, view, world_view);
    d9wg_multiply_matrix(world_view, projection, world_view_projection);
    int valid = stream_layout ? d9wg_vertex_stream_clip_codes(stream_layout, streams, source, world_view_projection, &state, status) :
            d9wg_vertex_clip_codes(source, world_view_projection, &state, status);
    d9wg_vertex_windows_blend_free(&state);return valid ? 0 : 1;
}
static __inline uint32_t d9wg_vertex_windows_clip_status(const D9WGClipDrawSource *source,
        const void *render_states, const void *worlds, uint32_t world_count, uint32_t world_gap,
        const void *view, const void *projection, const void *planes, D9WGClipStatus *status)
{
    return d9wg_vertex_windows_clip_status_inputs(source, NULL, NULL, render_states,
            worlds, world_count, world_gap, view, projection, planes, status);
}
static __inline void d9wg_vertex_windows_clip_release(void *context, void *allocation)
{ (void)context;HeapFree(GetProcessHeap(), 0, allocation); }
/* Scratch storage is shared by all three COM frontends. -3 is allocation
 * failure; the remaining return codes are those of d9wg_clip_draw. */
static __inline int d9wg_vertex_windows_clip_draw(const D9WGClipDrawSource *source,
        D9WGClipEmit emit, void *context)
{
    uint32_t bytes = 65536u;
    d9wg_clip_buffer_commit(source->clips, NULL, 0, 0, d9wg_vertex_windows_clip_release, NULL);
    if (!source->clips->head) return 0;
    if (source->stride > bytes/D9WG_CLIP_MAX_DRAW_VERTICES) {
        if (source->stride > 0xffffffffu/D9WG_CLIP_MAX_DRAW_VERTICES) return -3;
        bytes = source->stride*D9WG_CLIP_MAX_DRAW_VERTICES;
    }
    D9WGClipWork *work = HeapAlloc(GetProcessHeap(), 0, sizeof(*work));
    void *staging = HeapAlloc(GetProcessHeap(), 0, bytes);
    if (!work || !staging) {
        if (work) HeapFree(GetProcessHeap(), 0, work);
        if (staging) HeapFree(GetProcessHeap(), 0, staging);
        return -3;
    }
    int result = d9wg_clip_draw(source, work, staging, bytes, emit, context);
    HeapFree(GetProcessHeap(), 0, staging);HeapFree(GetProcessHeap(), 0, work);
    return result;
}
typedef int (*D9WGClipSetMask)(void *context, uint32_t mask);
typedef struct D9WGClipMaskedEmit {
    D9WGClipEmit emit;
    D9WGClipSetMask set_mask;
    void *context;
    uint32_t mask, suspended;
} D9WGClipMaskedEmit;
static __inline int d9wg_vertex_windows_clip_emit(void *context, uint32_t type,
        const void *vertices, uint32_t count, uint32_t stride)
{
    D9WGClipMaskedEmit *guard = context;
    if (!guard->suspended) {
        if (!guard->set_mask(guard->context, 0)) return 0;
        guard->suspended = 1;
    }
    return guard->emit(guard->context, type, vertices, count, stride);
}
static __inline int d9wg_vertex_windows_clip_draw_planes(const D9WGClipDrawSource *source,
        D9WGClipEmit emit, D9WGClipSetMask set_mask, uint32_t mask, void *context)
{
    /* The CPU has consumed the processing-time user planes. A second test
     * against the emitted screen coordinates would use a different space. */
    if (!mask) return d9wg_vertex_windows_clip_draw(source, emit, context);
    D9WGClipMaskedEmit guard = { emit, set_mask, context, mask, 0 };
    int result = d9wg_vertex_windows_clip_draw(source, d9wg_vertex_windows_clip_emit, &guard);
    if (guard.suspended && !set_mask(context, guard.mask)) result = -2;
    return result;
}
static __inline uint32_t d9wg_vertex_windows_clip(D9WGVertexState *state,
        uint32_t enabled, uint32_t first, uint32_t stride, uint32_t count,
        const D9WGProcessViewport *viewport, const float *guard_band,
        D9WGClipBatch **result)
{
    uint32_t bytes;*result = NULL;
    if (!enabled) return 0;
    bytes = d9wg_clip_batch_bytes(count);if (!bytes) return 1;
    D9WGClipBatch *batch = HeapAlloc(GetProcessHeap(), 0, bytes);
    if (!batch) return 2;
    if (!d9wg_clip_batch_init(batch, bytes, first, stride, count, viewport)) {
        HeapFree(GetProcessHeap(), 0, batch);return 1;
    }
    state->clip_vertices = batch->vertex;
    for (unsigned i = 0; i < 4; ++i) state->clip_guard_band[i] = guard_band[i];
    /* Zero guard-band caps mean clipping at the viewport, not a band whose
     * four screen coordinates all happen to be zero. */
    if (!guard_band[0] && !guard_band[1] && !guard_band[2] && !guard_band[3]) {
        state->clip_guard_band[0] = viewport->x;state->clip_guard_band[1] = viewport->y;
        state->clip_guard_band[2] = viewport->x + viewport->width;
        state->clip_guard_band[3] = viewport->y + viewport->height;
    }
    *result = batch;return 0;
}
#endif
