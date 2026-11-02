#include "runtime/d9wg_clip_draw.h"
#include "runtime/d9wg_vertex.h"
#include <stdint.h>
#define MDCHECK(x)           \
    do {                     \
        if (!(x))            \
            return __LINE__; \
    } while (0)
typedef struct StreamClipCapture {
    uint32_t words[64][7], count, calls;
    const D9WGProcessLayout* layout;
} StreamClipCapture;
static int stream_clip_capture(void* context, uint32_t type, const void* vertices, uint32_t count, uint32_t stride)
{
    StreamClipCapture* capture = context;
    const D9WGProcessLayout* layout = capture->layout;
    if ((type != 4 && type != 1) || count > 64 - capture->count)
        return 0;
    for (uint32_t v = 0; v < count; ++v) {
        const uint8_t* data = (const uint8_t*)vertices + v * stride;
        uint32_t* out = capture->words[capture->count++];
        __builtin_memcpy(out, data + layout->position, 16);
        __builtin_memcpy(out + 4, data + layout->diffuse, 4);
        __builtin_memcpy(out + 5, data + layout->texcoord[0], 8);
    }
    capture->calls++;
    return 1;
}
static __attribute__((noinline)) int vertex_stream_draw_cases(void)
{
    static D9WGProcessStreamLayout multi;
    D9WGProcessLayout single;
    D9WGVertexElement elements[] = { { 3, 4, 3, 0, 9, 0 }, { 15, 3, 4, 0, 10, 0 }, { 7, 0, 1, 0, 5, 0 } };
    MDCHECK(d9wg_process_stream_layout(elements, 3, &multi));
    MDCHECK(d9wg_process_fvf(9, 0x144, &single));
    D9WGProcessViewport vp = { 7, 11, 100, 80, .25f, .75f };
    float guard[4] = { -4096, -4096, 4095, 4095 };
    static union {
        uint64_t alignment;
        uint32_t words[256];
    } batches[2];
    D9WGClipBatch *original = (D9WGClipBatch*)batches[0].words, *split = (D9WGClipBatch*)batches[1].words;
    static uint8_t interleaved[100], positions[72], colors[12], textures[40];
    D9WGVertexStream streams[16] = { { 0 } };
    streams[3] = (D9WGVertexStream) { positions, sizeof(positions), 4, 24 };
    streams[15] = (D9WGVertexStream) { colors, sizeof(colors), 5, 0 };
    streams[7] = (D9WGVertexStream) { textures, sizeof(textures), 4, 12 };
    uint32_t color = 0xff4080bfu;
    __builtin_memcpy(colors + 8, &color, 4);
    float clip[3][4] = { { -2, -.5f, .5f, 1 }, { .5f, -.5f, .5f, 1 }, { 0, .75f, -.25f, 1 } }, tex[3][2] = { { 0, 0 }, { 1, 0 }, { 0, 1 } }, plane[24] = { 1, 0, 0, .2f };
    static D9WGClipWork work;
    static uint8_t staging[65536];
    static StreamClipCapture expected, actual;
    for (unsigned mode = 0; mode < 4; ++mode) {
        MDCHECK(d9wg_clip_batch_init(original, sizeof(batches[0].words), 4, 32, 3, &vp));
        MDCHECK(d9wg_clip_batch_init(split, sizeof(batches[1].words), 8, 24, 3, &vp));
        __builtin_memset(interleaved, 0x63, sizeof(interleaved));
        __builtin_memset(positions, 0x63, sizeof(positions));
        __builtin_memset(textures, 0x63, sizeof(textures));
        for (unsigned v = 0; v < 3; ++v) {
            d9wg_clip_vertex(clip[v], &vp, guard, 1, original->vertex + v);
            if (mode & 1)
                d9wg_clip_user_vertex(original->vertex + v, clip[v], plane, 1);
            split->vertex[v] = original->vertex[v];
            __builtin_memcpy(interleaved + 4 + v * 32, original->vertex[v].stored, 16);
            __builtin_memcpy(interleaved + 20 + v * 32, &color, 4);
            __builtin_memcpy(interleaved + 24 + v * 32, tex[v], 8);
            __builtin_memcpy(positions + 8 + v * 24, split->vertex[v].stored, 16);
            __builtin_memcpy(textures + 4 + v * 12, tex[v], 8);
        }
        original->live = split->live = 3;
        D9WGClipBuffer original_buffer = { original }, split_buffer = { split };
        D9WGClipDrawSource source = { 0 };
        source.clips = &original_buffer;
        source.vertices = interleaved;
        source.length = sizeof(interleaved);
        source.base = 4;
        source.stride = 32;
        source.count = source.vertex_count = 3;
        source.layout = &single;
        source.primitive_type = 4;
        source.viewport = vp;
        if (mode & 2)
            source.viewport = (D9WGProcessViewport) { 3, 5, 110, 100, .8f, .9f };
        __builtin_memset(&expected, 0, sizeof(expected));
        expected.layout = &single;
        MDCHECK(d9wg_clip_draw(&source, &work, staging, sizeof(staging), stream_clip_capture, &expected) == 1);
        MDCHECK(expected.count >= 3);
        D9WGClipDrawSource virtual_source = source;
        virtual_source.clips = &split_buffer;
        virtual_source.base = 0;
        d9wg_clip_draw_streams_init(&virtual_source, &multi, streams);
        __builtin_memset(&actual, 0, sizeof(actual));
        actual.layout = &multi.vertex;
        MDCHECK(d9wg_clip_draw(&virtual_source, &work, staging, sizeof(staging), stream_clip_capture, &actual) == 1);
        MDCHECK(actual.count == expected.count && actual.calls == expected.calls);
        for (unsigned v = 0; v < actual.count; ++v)
            for (unsigned c = 0; c < 7; ++c)
                MDCHECK(actual.words[v][c] == expected.words[v][c]);
        /* A failure in the last submitted vertex cannot emit earlier ones. */
        uint32_t saved = streams[7].length;
        streams[7].length = 35;
        actual.count = actual.calls = 0;
        MDCHECK(d9wg_clip_draw(&virtual_source, &work, staging, sizeof(staging), stream_clip_capture, &actual) == -1);
        MDCHECK(!actual.calls);
        streams[7].length = saved;
        /* Indices and a signed base address the same three original records. */
        uint16_t indices[3] = { 1, 2, 3 };
        virtual_source.indices = (const uint8_t*)indices;
        virtual_source.index_bytes = sizeof(indices);
        virtual_source.index_stride = 2;
        virtual_source.min_vertex = 1;
        virtual_source.vertex_count = 4;
        virtual_source.base_vertex = -1;
        actual.count = actual.calls = 0;
        MDCHECK(d9wg_clip_draw(&virtual_source, &work, staging, sizeof(staging), stream_clip_capture, &actual) == 1);
        MDCHECK(actual.count == expected.count);
        for (unsigned v = 0; v < actual.count; ++v)
            for (unsigned c = 0; c < 7; ++c)
                MDCHECK(actual.words[v][c] == expected.words[v][c]);
        indices[2] = 4;
        actual.count = actual.calls = 0;
        MDCHECK(d9wg_clip_draw(&virtual_source, &work, staging, sizeof(staging), stream_clip_capture, &actual) == -1);
        MDCHECK(!actual.calls);
    }
    /* A source index may wrap the 32-bit vertex domain only when every
     * attribute is constant: all byte addresses still identify record zero. */
    MDCHECK(d9wg_clip_batch_init(split, sizeof(batches[1]), 8, 24, 1, &vp));
    float origin[4] = { 0, 0, .5f, 1 };
    d9wg_clip_vertex(origin, &vp, guard, 1, split->vertex);
    split->live = 1;
    __builtin_memcpy(positions + 8, split->vertex[0].stored, 16);
    streams[3].stride = streams[7].stride = 0;
    D9WGClipBuffer constant_buffer = { split };
    D9WGClipDrawSource constant = { 0 };
    constant.clips = &constant_buffer;
    constant.first_vertex = 0xffffffffu;
    constant.count = constant.vertex_count = 2;
    constant.primitive_type = 1;
    constant.viewport = vp;
    d9wg_clip_draw_streams_init(&constant, &multi, streams);
    actual.count = actual.calls = 0;
    actual.layout = &multi.vertex;
    MDCHECK(d9wg_clip_draw(&constant, &work, staging, sizeof(staging), stream_clip_capture, &actual) == 1 && actual.count == 2);
    float screen[4] = { 57, 51, .5f, 1 };
    for (unsigned v = 0; v < 2; ++v) {
        for (unsigned c = 0; c < 4; ++c) {
            uint32_t bits;
            __builtin_memcpy(&bits, screen + c, 4);
            MDCHECK(actual.words[v][c] == bits);
        }
        MDCHECK(actual.words[v][4] == color);
    }
    streams[7].stride = 12;
    actual.count = actual.calls = 0;
    MDCHECK(d9wg_clip_draw(&constant, &work, staging, sizeof(staging), stream_clip_capture, &actual) == -1 && !actual.calls);
    return 0;
}
#undef MDCHECK
