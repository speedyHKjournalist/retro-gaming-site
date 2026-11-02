#ifndef D9WG_VERTEX_WINDOWS_H
#define D9WG_VERTEX_WINDOWS_H
#include <windows.h>
#include "d9wg_blend.h"
#include "d9wg_clip_draw.h"

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
static __inline void d9wg_vertex_windows_clip_release(void *context, void *allocation)
{ (void)context;HeapFree(GetProcessHeap(), 0, allocation); }
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
    *result = batch;return 0;
}
#endif
