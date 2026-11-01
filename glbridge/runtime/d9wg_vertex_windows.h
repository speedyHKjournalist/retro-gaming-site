#ifndef D9WG_VERTEX_WINDOWS_H
#define D9WG_VERTEX_WINDOWS_H
#include <windows.h>
#include "d9wg_blend.h"

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
#endif
