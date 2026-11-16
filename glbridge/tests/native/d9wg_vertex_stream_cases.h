#include "runtime/d9wg_blend.h"
#include "runtime/d9wg_vertex.h"
#include <stdint.h>
#define SCHECK(x)            \
    do {                     \
        if (!(x))            \
            return __LINE__; \
    } while (0)
static uint32_t stream_seed = 0x42e15a93u;
static uint32_t stream_random(void)
{
    stream_seed = stream_seed * 1664525u + 1013904223u;
    return stream_seed;
}
static uint32_t stream_word(const void* p)
{
    uint32_t v;
    __builtin_memcpy(&v, p, 4);
    return v;
}
static float stream_float(const void* p)
{
    float v;
    __builtin_memcpy(&v, p, 4);
    return v;
}
static __attribute__((noinline)) int vertex_stream_cases(unsigned iterations)
{
    uint32_t first;
    for (unsigned i = 0; i < iterations; ++i) {
        uint32_t length = stream_random(), base = stream_random(), stride = stream_random() & 1023u;
        uint32_t extent = stream_random() % 1100u, start = stream_random(), count = stream_random();
        if (i % 3 == 0)
            stride = 0;
        if (i & 1u) {
            base = length ? stream_random() % length : 0;
            start = stream_random() % 100;
            count = stream_random() % 100;
        }
        double begin = (double)base + (double)start * stride;
        double end = begin + (stride ? ((double)count - 1) * stride : 0) + extent;
        int expected = count && extent && (!stride || extent <= stride) && base <= length && begin <= length && end <= length;
        int got = d9wg_process_stream_range(length, base, stride, extent, start, count, &first);
        SCHECK(got == expected);
        if (got)
            SCHECK(first == (uint32_t)begin);
    }
    SCHECK(d9wg_process_stream_range(19, 3, 0, 16, 0xffffffffu, 0xffffffffu, &first) && first == 3);
    SCHECK(d9wg_process_stream_range(53, 3, 24, 2, 2, 1, &first) && first == 51);
    SCHECK(!d9wg_process_stream_range(52, 3, 24, 2, 2, 1, &first));
    float identity[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    D9WGProcessViewport vp = { 7, 11, 100, 80, .25f, .75f };
    static D9WGTNLLighting lighting;
    D9WGSetMaterial material = { 0 };
    D9WGTNLLightingFlags flags = { 0 };
    d9wg_lighting_init(&lighting, &material, 0, &flags);
    d9wg_lighting_geometry(&lighting, identity);
    static D9WGProcessStreamLayout layout;
    D9WGProcessLayout destination;
    SCHECK(d9wg_process_fvf(9, 0x1f4, &destination) && destination.stride == 48);
    static uint8_t buffers[16][192], output[220], interleaved[48 * 3], expected[220];
    static D9WGVertexStream streams[16];
    static D9WGVertexState state;
    D9WGClipVertex clips[3];
    for (unsigned rotation = 0; rotation < 16; ++rotation) {
        D9WGVertexElement elements[] = { { 3, 1, 16, 0, 0, 0 }, { 7, 2, 14, 0, 3, 0 }, { 15, 3, 3, 0, 10, 0 }, { 2, 1, 4, 0, 10, 1 }, { 9, 4, 15, 0, 5, 0 }, { 5, 1, 0, 0, 4, 0 } };
        __builtin_memset(buffers, 0x63, sizeof(buffers));
        __builtin_memset(streams, 0, sizeof(streams));
        for (unsigned i = 0; i < 6; ++i) {
            elements[i].stream = (elements[i].stream + rotation) % 16;
            unsigned s = elements[i].stream;
            streams[s].vertices = buffers[s];
            streams[s].base = 3 + s;
            streams[s].stride = i == 2 ? 0 : 32 + s;
            streams[s].length = streams[s].base + 2 * streams[s].stride + elements[i].offset + d9wg_decl_bytes(elements[i].type);
        }
        SCHECK(d9wg_process_stream_layout(elements, 6, &layout));
        uint16_t position[3][4] = { { 0x3400, 0xb800, 0x3800, 0x4000 }, { 0xbc00, 0, 0x3800, 0x3c00 }, { 0x4200, 0, 0x3800, 0x3c00 } };
        uint32_t normal = 512u | (511u << 20), specular = 0x40336699u;
        float color[4] = { .0011f, .0041f, .0069f, .5f }, size = 1.5f;
        uint16_t tex[2] = { 0x3400, 0x3a00 };
        for (unsigned v = 0; v < 3; ++v) {
            const void* values[] = { position[v], &normal, color, &specular, tex, &size };
            for (unsigned i = 0; i < 6; ++i) {
                D9WGVertexStream* s = streams + elements[i].stream;
                __builtin_memcpy(buffers[elements[i].stream] + s->base + v * s->stride + elements[i].offset, values[i], d9wg_decl_bytes(elements[i].type));
            }
        }
        __builtin_memset(&state, 0, sizeof(state));
        state.clip_vertices = clips;
        state.clip_guard_band[0] = state.clip_guard_band[1] = -4096;
        state.clip_guard_band[2] = state.clip_guard_band[3] = 4095;
        __builtin_memset(output, 0xa5, sizeof(output));
        SCHECK(d9wg_process_vertices_streams(&layout, streams, 1, &destination, output, sizeof(output), 1, 2, identity, identity, &vp, &lighting, 1, &state));
        SCHECK(stream_float(output + 48) == 7 && stream_float(output + 52) == 51 && stream_float(output + 56) == .5f && stream_float(output + 60) == 1);
        SCHECK(stream_float(output + 96) == 207 && stream_float(output + 100) == 51);
        SCHECK(stream_float(output + 64) == -1 && stream_float(output + 68) == 0 && stream_float(output + 72) == 1);
        SCHECK(stream_float(output + 76) == 1.5f && stream_word(output + 80) == 0x80000102u && stream_word(output + 84) == specular);
        SCHECK(stream_float(output + 88) == .25f && stream_float(output + 92) == .75f);
        SCHECK(clips[0].code == 0 && clips[1].code == 2 && clips[0].clip[0] == -1 && clips[1].clip[0] == 3);
        for (unsigned i = 0; i < 48; ++i)
            SCHECK(output[i] == 0xa5);
        for (unsigned i = 144; i < sizeof(output); ++i)
            SCHECK(output[i] == 0xa5);
        /* Independent interleaved input preserves the original packed bytes,
         * so every stream attribute and its late conversion are compared. */
        D9WGVertexElement flat[6];
        unsigned cursor = 0;
        for (unsigned i = 0; i < 6; ++i) {
            flat[i] = elements[i];
            flat[i].stream = 0;
            flat[i].offset = cursor;
            cursor += d9wg_decl_bytes(flat[i].type);
        }
        D9WGProcessLayout single;
        SCHECK(d9wg_process_layout(flat, 6, &single) && single.stride == 40);
        for (unsigned v = 0; v < 3; ++v)
            for (unsigned i = 0; i < 6; ++i) {
                D9WGVertexStream* s = streams + elements[i].stream;
                __builtin_memcpy(interleaved + v * 40 + flat[i].offset, s->vertices + s->base + v * s->stride + elements[i].offset, d9wg_decl_bytes(flat[i].type));
            }
        for (unsigned mode = 0; mode < 4; ++mode) {
            state.clip_vertices = 0;
            state.fog_mode = mode == 2 ? 3 : 0;
            state.fog_end = 1;
            lighting.flags.lighting = mode == 3;
            lighting.material.emissive[0] = .25f;
            __builtin_memset(output, 0xa5, sizeof(output));
            __builtin_memset(expected, 0xa5, sizeof(expected));
            SCHECK(d9wg_process_vertices_streams(&layout, streams, 1, &destination, output, sizeof(output), 1, 2, identity, identity, &vp, &lighting, mode != 1, &state));
            SCHECK(d9wg_process_vertices(&single, interleaved, 120, 0, 40, 1, &destination, expected, sizeof(expected), 1, 2, identity, identity, &vp, &lighting, mode != 1, &state));
            for (unsigned i = 0; i < sizeof(output); ++i)
                SCHECK(output[i] == expected[i]);
        }
        lighting.flags.lighting = 0;
        for (unsigned i = 0; i < 6; ++i) {
            unsigned s = elements[i].stream;
            uint32_t saved = streams[s].length;
            streams[s].length--;
            __builtin_memset(output, 0xa5, sizeof(output));
            SCHECK(!d9wg_process_vertices_streams(&layout, streams, 1, &destination, output, sizeof(output), 1, 2, identity, identity, &vp, &lighting, 1, 0));
            for (unsigned j = 0; j < sizeof(output); ++j)
                SCHECK(output[j] == 0xa5);
            streams[s].length = saved;
        }
    }
    D9WGVertexElement all[64];
    for (unsigned i = 0; i < 64; ++i) {
        all[i] = (D9WGVertexElement) { i % 16, (i / 16) * 16, 3, 0, 6, (uint8_t)i };
    }
    all[0].usage = 0;
    SCHECK(d9wg_process_stream_layout(all, 64, &layout) && layout.stream_mask == 65535 && layout.vertex.stride == 1024);
    SCHECK(!d9wg_process_stream_layout(all, 65, &layout));
    all[1].stream = 16;
    SCHECK(!d9wg_process_stream_layout(all, 64, &layout));
    all[1].stream = 1;
    all[1].method = 1;
    SCHECK(!d9wg_process_stream_layout(all, 64, &layout));
    all[1].method = 0;
    all[1].usage = 0;
    all[1].usage_index = 0;
    SCHECK(!d9wg_process_stream_layout(all, 64, &layout));
    /* The final vertex has an invalid palette entry. No earlier output or
     * clip record may change before the entire index range has passed. */
    D9WGVertexElement bones[] = { { 3, 0, 2, 0, 0, 0 }, { 7, 0, 0, 0, 1, 0 }, { 15, 0, 5, 0, 2, 0 } };
    SCHECK(d9wg_process_stream_layout(bones, 3, &layout));
    float xyz[2][3] = { { 0, 0, .5f }, { .25f, 0, .5f } }, weights[2] = { .25f, .75f };
    uint8_t indices[2][4] = { { 0, 1, 0, 0 }, { 1, 2, 0, 0 } };
    __builtin_memset(streams, 0, sizeof(streams));
    streams[3] = (D9WGVertexStream) { (const uint8_t*)xyz, sizeof(xyz), 0, 12 };
    streams[7] = (D9WGVertexStream) { (const uint8_t*)weights, sizeof(weights), 0, 4 };
    streams[15] = (D9WGVertexStream) { (const uint8_t*)indices, sizeof(indices), 0, 4 };
    static uint32_t scratch[256];
    static float worlds[32];
    __builtin_memcpy(worlds, identity, 64);
    __builtin_memcpy(worlds + 16, identity, 64);
    worlds[28] = .5f;
    D9WGVertexBlend* blend = (D9WGVertexBlend*)scratch;
    SCHECK(d9wg_blend_init(blend, sizeof(scratch), 1, 1, worlds, 2, 0, identity, identity));
    __builtin_memset(&state, 0, sizeof(state));
    state.blend = blend;
    state.clip_vertices = clips;
    __builtin_memset(output, 0xa5, sizeof(output));
    __builtin_memset(clips, 0x63, sizeof(clips));
    SCHECK(!d9wg_process_vertices_streams(&layout, streams, 0, &destination, output, sizeof(output), 0, 2, identity, identity, &vp, &lighting, 1, &state));
    for (unsigned i = 0; i < sizeof(output); ++i)
        SCHECK(output[i] == 0xa5);
    for (unsigned i = 0; i < sizeof(clips); ++i)
        SCHECK(((uint8_t*)clips)[i] == 0x63);
    indices[1][1] = 0;
    state.clip_guard_band[0] = state.clip_guard_band[1] = -4096;
    state.clip_guard_band[2] = state.clip_guard_band[3] = 4095;
    state.clip_plane_mask = 1;
    state.clip_planes[0][0] = -1;
    state.clip_planes[0][3] = .4f;
    SCHECK(d9wg_process_vertices_streams(&layout, streams, 0, &destination, output, sizeof(output), 0, 2, identity, identity, &vp, &lighting, 1, &state));
    SCHECK(stream_float(output) == 75.75f && clips[1].screen[0] == 88.25f && stream_float(output + 48) == .8125f);
    SCHECK(clips[0].user_mask == 1 && clips[0].user_distance[0] > .024f && clips[0].user_distance[0] < .026f);
    SCHECK(clips[1].user_distance[0] < -.224f && clips[1].user_distance[0] > -.226f);
    return 0;
}
#undef SCHECK
