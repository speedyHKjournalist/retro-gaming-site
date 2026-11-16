#ifndef D9WG_CLIP_USER_CASES_H
#define D9WG_CLIP_USER_CASES_H
#include "../../runtime/d9wg_blend.h"
#include "../../runtime/d9wg_clip.h"
#include "../../runtime/d9wg_math.h"
#include "../../runtime/d9wg_vertex.h"
#include "../../runtime/d9wg_clip_draw.h"
#ifdef _WIN32
#include <windows.h>
#define USER_ALLOC(bytes) HeapAlloc(GetProcessHeap(), 0, bytes)
#define USER_FREE(p) HeapFree(GetProcessHeap(), 0, p)
#else
#include <stdlib.h>
#define USER_ALLOC(bytes) malloc(bytes)
#define USER_FREE(p) free(p)
#endif
#define USER_CHECK(condition) \
    do {                      \
        if (!(condition))     \
            return __LINE__;  \
    } while (0)
static float clip_user_abs(float f) { return f < 0 ? -f : f; }
static int clip_user_cases(uint32_t cases)
{
    D9WGClipWork* work = USER_ALLOC(sizeof(*work));
    USER_CHECK(work);
    D9WGClipPoint input[3] = { 0 };
    const D9WGClipPoint* output;
    uint32_t count;
    uint32_t random = 0x317aec4bu;
    static const float planes[6][4] = { { 1, 0, 0, .137f }, { -1, 0, 0, .271f }, { 0, 1, 0, .193f }, { 0, -1, 0, .311f }, { 0, 0, 1, -.217f }, { 0, 0, -1, .743f } };
    for (uint32_t n = 0; n < cases; ++n) {
        uint32_t mask = n % 64u;
        for (unsigned v = 0; v < 3; ++v) {
            float* p = input[v].data;
            for (unsigned c = 0; c < D9WG_CLIP_MAX_FLOATS; ++c)
                p[c] = 0;
            for (unsigned c = 0; c < 4; ++c) {
                random = random * 1664525u + 1013904223u;
                p[c] = ((int32_t)(random % 1025u) - 512) / 256.0f;
            }
            if (n % 4u == 0)
                p[3] = 1;
            p[4 + v] = 1;
            for (unsigned plane = 0; plane < 6; ++plane)
                p[D9WG_CLIP_USER_FIRST + plane] = p[0] * planes[plane][0] + p[1] * planes[plane][1] + p[2] * planes[plane][2] + p[3] * planes[plane][3];
        }
        USER_CHECK(d9wg_clip_triangle_planes(input, 55, mask, work, &output, &count));
        USER_CHECK(count <= 15);
        for (unsigned v = 0; v < count; ++v) {
            const float* p = output[v].data;
            USER_CHECK(!d9wg_clip_outcode(p) && p[3] > 0);
            USER_CHECK(clip_user_abs(p[4] + p[5] + p[6] - 1) < .00002f);
            for (unsigned c = 0; c < 4; ++c)
                USER_CHECK(clip_user_abs(p[c] - (p[4] * input[0].data[c] + p[5] * input[1].data[c] + p[6] * input[2].data[c])) < .0001f);
            for (unsigned plane = 0; plane < 6; ++plane)
                if (mask & (1u << plane)) {
                    float analytic = p[0] * planes[plane][0] + p[1] * planes[plane][1] + p[2] * planes[plane][2] + p[3] * planes[plane][3];
                    USER_CHECK(p[D9WG_CLIP_USER_FIRST + plane] >= -.00002f && analytic >= -.00002f);
                    USER_CHECK(clip_user_abs(analytic - p[D9WG_CLIP_USER_FIRST + plane]) < .0001f);
                }
        }
        D9WGClipPoint segment[2];
        USER_CHECK(d9wg_clip_segment_planes(input, 55, mask, segment, &count));
        USER_CHECK(count == 0 || count == 2);
        for (unsigned v = 0; v < count; ++v) {
            USER_CHECK(!d9wg_clip_outcode(segment[v].data));
            for (unsigned plane = 0; plane < 6; ++plane)
                if (mask & (1u << plane))
                    USER_CHECK(segment[v].data[D9WG_CLIP_USER_FIRST + plane] >= -.00002f);
        }
    }
    count = 123;
    output = input;
    USER_CHECK(!d9wg_clip_triangle_planes(input, 55, 64, work, &output, &count) && count == 123 && output == input);
    USER_CHECK(!d9wg_clip_triangle_planes(input, 54, 1, work, &output, &count) && count == 123 && output == input);
    uint32_t nan = 0x7fc12345u;
    __builtin_memcpy(&input[0].data[D9WG_CLIP_USER_FIRST], &nan, 4);
    USER_CHECK(!d9wg_clip_triangle_planes(input, 55, 1, work, &output, &count) && count == 123 && output == input);
    USER_FREE(work);

    /* Indexed blending must test a world-space plane against the weighted
     * world position, even with a translated view and singular projection. */
    float worlds[2][16] = { { 0 } }, view[16] = { 0 }, projection[16] = { 0 }, wv[16], wvp[16];
    for (unsigned i = 0; i < 4; ++i)
        worlds[0][i * 5] = worlds[1][i * 5] = view[i * 5] = projection[i * 5] = 1;
    worlds[0][12] = -1;
    worlds[1][12] = 1;
    view[12] = -.25f;
    projection[10] = 0;
    d9wg_multiply_matrix(worlds[0], view, wv);
    d9wg_multiply_matrix(wv, projection, wvp);
    uint32_t bytes = d9wg_blend_bytes(1, 1, 2);
    D9WGVertexBlend* blend = USER_ALLOC(bytes);
    USER_CHECK(blend);
    USER_CHECK(d9wg_blend_init(blend, bytes, 1, 1, worlds, 2, 0, view, projection));
    D9WGProcessLayout source, destination;
    USER_CHECK(d9wg_process_fvf(9, 0x1008u, &source) && d9wg_process_fvf(9, 4, &destination));
    struct {
        float position[3], weight;
        uint8_t indices[4];
    } vertex = { { 0, 0, .5f }, .25f, { 0, 1, 0, 0 } };
    uint32_t states[256] = { 0 };
    static D9WGVertexState state;
    d9wg_vertex_state_init(&state, states);
    float user[6][4] = { { -1, 0, 0, 0 } }, result[4];
    D9WGClipVertex record;
    d9wg_vertex_clip_planes_init(&state, 1, user, worlds[0]);
    state.blend = blend;
    state.clip_vertices = &record;
    state.clip_guard_band[0] = state.clip_guard_band[1] = -4096;
    state.clip_guard_band[2] = state.clip_guard_band[3] = 4095;
    D9WGProcessViewport vp = { 7, 11, 100, 80, .25f, .75f };
    static D9WGTNLLighting lighting;
    USER_CHECK(d9wg_process_vertices(&source, &vertex, sizeof(vertex), 0, sizeof(vertex), 0, &destination, result, sizeof(result), 0, 1, wv, wvp, &vp, &lighting, 1, &state));
    USER_CHECK(record.user_mask == 1 && record.user_distance[0] == -.5f && record.code == 64 && record.projected);
    USER_CHECK(record.clip[0] == .25f && record.clip[2] == 0 && result[0] == .625f && result[3] == 1);
    D9WGClipDrawSource draw = { 0 };
    draw.vertices = (const uint8_t *)&vertex;draw.length = sizeof(vertex);draw.stride = sizeof(vertex);
    draw.count = draw.vertex_count = 1;draw.layout = &source;
    D9WGClipStatus code_status;d9wg_clip_status_init(&code_status);
    USER_CHECK(d9wg_vertex_clip_codes(&draw, wvp, &state, &code_status));
    USER_CHECK(code_status.clip_union == 64 && code_status.clip_intersection == 64);
    vertex.indices[1] = 255;
    USER_CHECK(!d9wg_vertex_clip_codes(&draw, wvp, &state, &code_status));
    USER_CHECK(code_status.clip_union == 64 && code_status.clip_intersection == 64);
    USER_FREE(blend);
    return 0;
}
#undef USER_CHECK
#undef USER_ALLOC
#undef USER_FREE
#endif
