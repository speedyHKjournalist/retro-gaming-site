#ifndef D9WG_CLIP_DRAW_CASES_H
#define D9WG_CLIP_DRAW_CASES_H
#include "runtime/d9wg_clip_draw.h"
#ifdef _WIN32
#include <windows.h>
#define CLIP_DRAW_ALLOC(bytes) HeapAlloc(GetProcessHeap(), 0, (bytes))
#define CLIP_DRAW_FREE(p) HeapFree(GetProcessHeap(), 0, (p))
#else
#include <stdlib.h>
#define CLIP_DRAW_ALLOC(bytes) malloc(bytes)
#define CLIP_DRAW_FREE(p) free(p)
#endif
#define CLIP_DRAW_CHECK(x)                                                                                   \
    do                                                                                                       \
    {                                                                                                        \
        if (!(x))                                                                                            \
            return __LINE__;                                                                                 \
    } while (0)
static unsigned clip_draw_calls, clip_draw_vertices, clip_draw_expected_type = 4, clip_draw_fail_emit;
static uint8_t emitted[21 * 20];
static int clip_draw_emit(void* ctx, uint32_t type, const void* data, uint32_t count, uint32_t stride)
{
    (void)ctx;
    if (type != clip_draw_expected_type || stride != 20 || count > 21 || clip_draw_fail_emit)
        return 0;
    clip_draw_calls++;
    clip_draw_vertices += count;
    __builtin_memcpy(emitted, data, count * stride);
    return 1;
}
static void clip_draw_release(void* ctx, void* p)
{
    (void)ctx;
    CLIP_DRAW_FREE(p);
}
static float clip_draw_get(const void* p)
{
    float value;
    __builtin_memcpy(&value, p, 4);
    return value;
}
static int clip_draw_cases(void)
{
    clip_draw_calls = clip_draw_vertices = clip_draw_fail_emit = 0;
    clip_draw_expected_type = 4;
    D9WGProcessLayout layout = { 0 };
    layout.position = 0;
    layout.position_transformed = 1;
    layout.position_floats = 4;
    layout.diffuse = 16;
    layout.stride = 20;
    layout.normal = layout.psize = layout.specular = -1;
    for (unsigned i = 0; i < 8; i++)
        layout.texcoord[i] = -1;
    D9WGProcessViewport vp = { 7, 11, 100, 80, .25f, .75f };
    const float guard[4] = { -4096, -4096, 4095, 4095 };
    uint32_t bytes = d9wg_clip_batch_bytes(3);
    D9WGClipBatch* batch = CLIP_DRAW_ALLOC(bytes);
    CLIP_DRAW_CHECK(batch);
    CLIP_DRAW_CHECK(d9wg_clip_batch_init(batch, bytes, 0, 20, 3, &vp));
    const float point[3][4] = { { -.8f, -.7f, -.4f, 1 }, { .8f, -.7f, .5f, 1 }, { 0, .8f, .5f, 1 } };
    uint8_t source[80] = { 0 }, staging[21 * 20];
    const uint32_t colors[3] = { 0xffff0000u, 0xff00ff00u, 0xff0000ffu };
    for (unsigned v = 0; v < 3; v++)
    {
        d9wg_clip_vertex(point[v], &vp, guard, 1, &batch->vertex[v]);
        __builtin_memcpy(source + v * 20, batch->vertex[v].stored, 16);
        __builtin_memcpy(source + v * 20 + 16, &colors[v], 4);
    }
    D9WGClipBuffer buffer = { 0 };
    d9wg_clip_buffer_commit(&buffer, batch, 0, 60, clip_draw_release, 0);
    D9WGClipDrawSource draw = { 0 };
    draw.clips = &buffer;
    draw.vertices = source;
    draw.length = sizeof(source);
    draw.stride = 20;
    draw.vertex_count = 3;
    draw.count = 3;
    draw.primitive_type = 4;
    draw.layout = &layout;
    draw.viewport = vp;
    D9WGClipWork* work = CLIP_DRAW_ALLOC(sizeof(*work));
    CLIP_DRAW_CHECK(work);
    CLIP_DRAW_CHECK(d9wg_clip_draw(&draw, work, staging, sizeof(staging), clip_draw_emit, 0) == 1
        && clip_draw_calls == 1 && clip_draw_vertices == 6);
    unsigned near_vertices = 0;
    for (unsigned v = 0; v < clip_draw_vertices; v++)
    {
        const uint8_t* p = emitted + v * 20;
        CLIP_DRAW_CHECK(clip_draw_get(p) >= 7 && clip_draw_get(p) <= 107 && clip_draw_get(p + 4) >= 11
            && clip_draw_get(p + 4) <= 91);
        CLIP_DRAW_CHECK(
            clip_draw_get(p + 8) >= .25f && clip_draw_get(p + 8) <= .5f && clip_draw_get(p + 12) == 1);
        near_vertices += clip_draw_get(p + 8) == .25f;
    }
    CLIP_DRAW_CHECK(near_vertices >= 2);
    clip_draw_calls = clip_draw_vertices = 0;
    draw.flat_shading = 1;
    CLIP_DRAW_CHECK(d9wg_clip_draw(&draw, work, staging, sizeof(staging), clip_draw_emit, 0) == 1
        && clip_draw_vertices == 6);
    for (unsigned v = 0; v < clip_draw_vertices; v++)
    {
        uint32_t color;
        __builtin_memcpy(&color, emitted + v * 20 + 16, 4);
        CLIP_DRAW_CHECK(color == 0xffff0000u);
    }
    draw.flat_shading = 0;
    clip_draw_calls = clip_draw_vertices = 0;
    uint16_t indices[3] = { 0, 1, 3 };
    draw.indices = (const uint8_t*)indices;
    draw.index_stride = 2;
    draw.index_bytes = 6;
    CLIP_DRAW_CHECK(
        d9wg_clip_draw(&draw, work, staging, sizeof(staging), clip_draw_emit, 0) == -1 && !clip_draw_calls);
    draw.min_vertex = 1;
    indices[2] = 2;
    CLIP_DRAW_CHECK(
        d9wg_clip_draw(&draw, work, staging, sizeof(staging), clip_draw_emit, 0) == -1 && !clip_draw_calls);
    draw.min_vertex = 0;
    indices[2] = 2;
    CLIP_DRAW_CHECK(d9wg_clip_draw(&draw, work, staging, sizeof(staging), clip_draw_emit, 0) == 1
        && clip_draw_vertices == 6);
    clip_draw_calls = clip_draw_vertices = 0;
    draw.viewport.x = 21;
    draw.viewport.y = 27;
    draw.viewport.width = 80;
    draw.viewport.height = 60;
    draw.viewport.min_z = .6f;
    draw.viewport.max_z = .9f;
    CLIP_DRAW_CHECK(d9wg_clip_draw(&draw, work, staging, sizeof(staging), clip_draw_emit, 0) == 1
        && clip_draw_calls == 1 && clip_draw_vertices >= 3);
    for (unsigned v = 0; v < clip_draw_vertices; v++)
    {
        const uint8_t* p = emitted + v * 20;
        CLIP_DRAW_CHECK(clip_draw_get(p) >= 20.9999f && clip_draw_get(p) <= 101.0001f
            && clip_draw_get(p + 4) >= 26.9999f && clip_draw_get(p + 4) <= 87.0001f);
        CLIP_DRAW_CHECK(clip_draw_get(p + 8) < .6f);
    }
    clip_draw_calls = clip_draw_vertices = 0;
    draw.viewport = vp;
    draw.indices = 0;
    draw.index_stride = draw.index_bytes = 0;
    draw.primitive_type = 2;
    draw.count = 2;
    clip_draw_expected_type = 2;
    CLIP_DRAW_CHECK(d9wg_clip_draw(&draw, work, staging, sizeof(staging), clip_draw_emit, 0) == 1
        && clip_draw_calls == 1 && clip_draw_vertices == 2);
    CLIP_DRAW_CHECK(clip_draw_get(emitted + 8) == .25f && clip_draw_get(emitted + 28) == .5f);
    clip_draw_calls = clip_draw_vertices = 0;
    draw.primitive_type = 1;
    draw.count = 3;
    clip_draw_expected_type = 1;
    CLIP_DRAW_CHECK(d9wg_clip_draw(&draw, work, staging, sizeof(staging), clip_draw_emit, 0) == 1
        && clip_draw_calls == 1 && clip_draw_vertices == 2);
    for (unsigned v = 0; v < clip_draw_vertices; v++)
        CLIP_DRAW_CHECK(clip_draw_get(emitted + v * 20 + 8) == .5f);
    for (unsigned type = 5; type <= 6; type++)
    {
        clip_draw_calls = clip_draw_vertices = 0;
        clip_draw_expected_type = 4;
        draw.primitive_type = type;
        CLIP_DRAW_CHECK(d9wg_clip_draw(&draw, work, staging, sizeof(staging), clip_draw_emit, 0) == 1
            && clip_draw_calls == 1 && clip_draw_vertices == 6);
    }
    clip_draw_calls = clip_draw_vertices = 0;
    clip_draw_fail_emit = 1;
    CLIP_DRAW_CHECK(
        d9wg_clip_draw(&draw, work, staging, sizeof(staging), clip_draw_emit, 0) == -2 && !clip_draw_calls);
    clip_draw_fail_emit = 0;
    d9wg_clip_buffer_free(&buffer, clip_draw_release, 0);
    CLIP_DRAW_CHECK(d9wg_clip_draw(&draw, work, staging, sizeof(staging), clip_draw_emit, 0) == 0);
    CLIP_DRAW_FREE(work);
    return 0;
}

#undef CLIP_DRAW_CHECK
#undef CLIP_DRAW_ALLOC
#undef CLIP_DRAW_FREE
#endif
