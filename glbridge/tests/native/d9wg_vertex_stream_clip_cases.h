#include "runtime/d9wg_blend.h"
#include "runtime/d9wg_clip_draw.h"
#include "runtime/d9wg_vertex.h"
#include <stdint.h>
#define MSCHECK(x)           \
    do {                     \
        if (!(x))            \
            return __LINE__; \
    } while (0)
static __attribute__((noinline)) int vertex_stream_clip_cases(void)
{
    float identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    D9WGVertexElement elements[] = { { 3, 0, 2, 0, 0, 0 }, { 15, 4, 3, 0, 10, 0 } };
    static D9WGProcessStreamLayout layout;
    static D9WGVertexState state;
    MSCHECK(d9wg_process_stream_layout(elements, 2, &layout));
    float position[6][3] = { { 7, 0, .5f }, { -2, 0, .5f }, { 0, 3, .5f }, { .25f, 0, .5f }, { 0, 0, .5f }, { 0, 0, 2 } };
    uint8_t colors[24] = { 0 };
    D9WGVertexStream streams[16] = { { 0 } };
    streams[3] = (D9WGVertexStream) { (const uint8_t*)position, sizeof(position), 0, 12 };
    streams[15] = (D9WGVertexStream) { colors, sizeof(colors), 4, 0 };
    uint16_t indices[] = { 4, 4, 4 };
    D9WGClipDrawSource draw = { 0 };
    draw.indices = (const uint8_t*)indices;
    draw.index_bytes = sizeof(indices);
    draw.index_stride = 2;
    draw.count = 3;
    draw.min_vertex = 2;
    draw.vertex_count = 5;
    draw.base_vertex = -1;
    D9WGClipStatus status = { 0, 0xffffffffu };
    __builtin_memset(&state, 0, sizeof(state));
    MSCHECK(d9wg_vertex_stream_clip_codes(&layout, streams, &draw, identity, &state, &status));
    MSCHECK(status.clip_union == 5 && status.clip_intersection == 0); /* unreferenced left/top vertices */
    D9WGClipStatus saved = status;
    streams[3].length = 47;
    MSCHECK(!d9wg_vertex_stream_clip_codes(&layout, streams, &draw, identity, &state, &status));
    MSCHECK(status.clip_union == saved.clip_union && status.clip_intersection == saved.clip_intersection);
    streams[3].length = sizeof(position);
    draw.index_bytes = 5;
    MSCHECK(!d9wg_vertex_stream_clip_codes(&layout, streams, &draw, identity, &state, &status));
    draw.index_bytes = sizeof(indices);
    draw.index_stride = 1;
    MSCHECK(!d9wg_vertex_stream_clip_codes(&layout, streams, &draw, identity, &state, &status));
    draw.index_stride = 2;

    indices[2] = 5;
    MSCHECK(!d9wg_vertex_stream_clip_codes(&layout, streams, &draw, identity, &state, &status));
    MSCHECK(status.clip_union == saved.clip_union && status.clip_intersection == saved.clip_intersection);
    indices[2] = 1;
    MSCHECK(!d9wg_vertex_stream_clip_codes(&layout, streams, &draw, identity, &state, &status));
    indices[2] = 4;
    draw.base_vertex = -3;
    MSCHECK(!d9wg_vertex_stream_clip_codes(&layout, streams, &draw, identity, &state, &status));
    draw.base_vertex = -1;
    streams[15].length = 23;
    MSCHECK(!d9wg_vertex_stream_clip_codes(&layout, streams, &draw, identity, &state, &status));
    MSCHECK(status.clip_union == saved.clip_union && status.clip_intersection == saved.clip_intersection);
    streams[15].length = 24;
    draw.indices = 0;
    draw.first_vertex = 4;
    draw.base_vertex = 0;
    draw.count = 2;
    status = (D9WGClipStatus) { 0, 0xffffffffu };
    MSCHECK(d9wg_vertex_stream_clip_codes(&layout, streams, &draw, identity, &state, &status));
    MSCHECK(status.clip_union == 32 && status.clip_intersection == 0);
    draw.first_vertex = 0xffffffffu;
    streams[3].base = 12;
    streams[3].stride = 0;
    status = (D9WGClipStatus) { 0, 0xffffffffu };
    MSCHECK(d9wg_vertex_stream_clip_codes(&layout, streams, &draw, identity, &state, &status));
    MSCHECK(status.clip_union == 1 && status.clip_intersection == 1);
    streams[3].base = 36;
    draw.first_vertex = 0;
    state.clip_plane_mask = 1;
    __builtin_memcpy(state.clip_world, identity, 64);
    state.clip_planes[0][0] = -1;
    status = (D9WGClipStatus) { 0, 0xffffffffu };
    MSCHECK(d9wg_vertex_stream_clip_codes(&layout, streams, &draw, identity, &state, &status));
    MSCHECK(status.clip_union == 64 && status.clip_intersection == 64);
    /* A later out-of-range palette index cannot publish earlier status. */
    D9WGVertexElement bones[] = { { 3, 0, 2, 0, 0, 0 }, { 7, 0, 0, 0, 1, 0 }, { 15, 0, 5, 0, 2, 0 } };
    MSCHECK(d9wg_process_stream_layout(bones, 3, &layout));
    float weights[2] = { .5f, .5f };
    uint8_t palette[2][4] = { { 0, 1, 0, 0 }, { 0, 2, 0, 0 } };
    streams[3] = (D9WGVertexStream) { (const uint8_t*)position, 24, 0, 12 };
    streams[7] = (D9WGVertexStream) { (const uint8_t*)weights, sizeof(weights), 0, 4 };
    streams[15] = (D9WGVertexStream) { (const uint8_t*)palette, sizeof(palette), 0, 4 };
    static uint32_t scratch[256];
    static float worlds[32];
    __builtin_memcpy(worlds, identity, 64);
    __builtin_memcpy(worlds + 16, identity, 64);
    MSCHECK(d9wg_blend_init((D9WGVertexBlend*)scratch, sizeof(scratch), 1, 1, worlds, 2, 0, identity, identity));
    state.blend = (D9WGVertexBlend*)scratch;
    state.clip_plane_mask = 0;
    status = (D9WGClipStatus) { 0x80000000u, 0x40000000u };
    MSCHECK(!d9wg_vertex_stream_clip_codes(&layout, streams, &draw, identity, &state, &status));
    MSCHECK(status.clip_union == 0x80000000u && status.clip_intersection == 0x40000000u);
    palette[1][1] = 1;
    MSCHECK(d9wg_vertex_stream_clip_codes(&layout, streams, &draw, identity, &state, &status));
    MSCHECK(status.clip_union == 0x80000003u && status.clip_intersection == 0);
    /* Buffer POSITIONT skips software processing; immediate POSITIONT uses
     * viewport unprojection and continues to ignore world user planes. */
    elements[0] = (D9WGVertexElement) { 3, 0, 3, 0, 9, 0 };
    MSCHECK(d9wg_process_stream_layout(elements, 1, &layout));
    float screen[2][4] = { { -1, 10, .5f, 1 }, { 70, 10, .5f, 1 } };
    streams[3] = (D9WGVertexStream) { (const uint8_t*)screen, sizeof(screen), 0, 16 };
    draw.viewport = (D9WGProcessViewport) { 0, 0, 64, 64, .25f, .75f };
    __builtin_memset(&state, 0, sizeof(state));
    status = (D9WGClipStatus) { 4, 8 };
    MSCHECK(d9wg_vertex_stream_clip_codes(&layout, streams, &draw, identity, &state, &status));
    MSCHECK(status.clip_union == 4 && status.clip_intersection == 8);
    draw.status_screen = 1;
    status = (D9WGClipStatus) { 0, 0xffffffffu };
    MSCHECK(d9wg_vertex_stream_clip_codes(&layout, streams, &draw, identity, &state, &status));
    MSCHECK(status.clip_union == 3 && status.clip_intersection == 0);
    return 0;
}
#undef MSCHECK
