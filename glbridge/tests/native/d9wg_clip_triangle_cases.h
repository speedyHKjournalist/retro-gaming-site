#ifndef D9WG_CLIP_TRIANGLE_CASES_H
#define D9WG_CLIP_TRIANGLE_CASES_H
#include "tests/native/d9wg_clip_cases.h"
#define TRIANGLE_CHECK(x)                                                                                    \
    do                                                                                                       \
    {                                                                                                        \
        if (!(x))                                                                                            \
            return __LINE__;                                                                                 \
    } while (0)
static int clip_triangle_cases(uint32_t cases)
{
    static D9WGClipWork work;
    D9WGClipPoint input[3]
        = { { { -.8f, -.7f, -.4f, 1 } }, { { .8f, -.7f, .5f, 1 } }, { { 0, .8f, .5f, 1 } } };
    const D9WGClipPoint* output;
    uint32_t count;
    /* A known near_vertices-plane cut produces a quadrilateral and the two analytic
     * intersections, with barycentric attributes interpolated before division. */
    for (unsigned v = 0; v < 3; ++v)
        for (unsigned c = 0; c < 3; ++c)
            input[v].data[4 + c] = v == c ? 1 : 0;
    TRIANGLE_CHECK(d9wg_clip_triangle(input, 7, &work, &output, &count) && count == 4);
    unsigned near_vertices = 0;
    for (unsigned v = 0; v < count; ++v)
        if (output[v].data[2] == 0)
        {
            ++near_vertices;
            TRIANGLE_CHECK(clip_near(output[v].data[4], 5.0 / 9));
            if (output[v].data[5] > 0)
            {
                TRIANGLE_CHECK(clip_near(output[v].data[0], -4.0 / 45));
                TRIANGLE_CHECK(clip_near(output[v].data[5], 4.0 / 9));
            }
            else
            {
                TRIANGLE_CHECK(clip_near(output[v].data[0], -4.0 / 9));
                TRIANGLE_CHECK(clip_near(output[v].data[6], 4.0 / 9));
            }
        }
    TRIANGLE_CHECK(near_vertices == 2);
    uint32_t random = 0x493abcdu;
    for (uint32_t n = 0; n < cases; ++n)
    {
        uint32_t combined = 63, total = 0;
        for (unsigned v = 0; v < 3; ++v)
        {
            for (unsigned c = 0; c < 4; ++c)
            {
                random = random * 1664525u + 1013904223u;
                input[v].data[c] = ((int32_t)(random % 1025u) - 512) / 64.0f;
            }
            /* Include W=0, negative W, exact clip corners and duplicate inputs. */
            if (n % 8u == 0)
                input[v].data[3] = 1;
            if (n % 32u == 0)
            {
                input[v].data[0] = v == 0 ? -1 : 1;
                input[v].data[1] = v == 2 ? -1 : 1;
                input[v].data[2] = .5f;
            }
            input[v].data[7]
                = input[v].data[0] * 2 + input[v].data[1] * 3 + input[v].data[2] * 4 + input[v].data[3] * 5;
            combined &= d9wg_clip_outcode(input[v].data);
            total |= d9wg_clip_outcode(input[v].data);
        }
        TRIANGLE_CHECK(d9wg_clip_triangle(input, 8, &work, &output, &count));
        TRIANGLE_CHECK(count <= 9);
        if (combined)
            TRIANGLE_CHECK(count == 0);
        if (!total && input[0].data[3] > 0 && input[1].data[3] > 0 && input[2].data[3] > 0)
            TRIANGLE_CHECK(count == 3);
        for (unsigned v = 0; v < count; ++v)
        {
            const float* p = output[v].data;
            TRIANGLE_CHECK(!d9wg_clip_outcode(p) && p[3] > 0);
            TRIANGLE_CHECK(clip_near(p[4] + p[5] + p[6], 1));
            TRIANGLE_CHECK(p[4] >= 0 && p[5] >= 0 && p[6] >= 0);
            for (unsigned c = 0; c < 4; ++c)
                TRIANGLE_CHECK(clip_near(
                    p[c], p[4] * input[0].data[c] + p[5] * input[1].data[c] + p[6] * input[2].data[c]));
            TRIANGLE_CHECK(clip_near(p[7], p[0] * 2 + p[1] * 3 + p[2] * 4 + p[3] * 5));
        }
    }
    uint32_t saved_count = 123;
    const D9WGClipPoint* saved_output = input;
    TRIANGLE_CHECK(!d9wg_clip_triangle(input, 3, &work, &saved_output, &saved_count));
    TRIANGLE_CHECK(saved_count == 123 && saved_output == input);
    uint32_t infinity = 0x7f800000u;
    __builtin_memcpy(&input[0].data[0], &infinity, 4);
    TRIANGLE_CHECK(!d9wg_clip_triangle(input, 8, &work, &saved_output, &saved_count));
    TRIANGLE_CHECK(saved_count == 123 && saved_output == input);
    return 0;
}
#undef TRIANGLE_CHECK
#endif
