#ifndef D9WG_CLIP_CASES_H
#define D9WG_CLIP_CASES_H
#include "runtime/d9wg_clip.h"
#define CLIP_CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static int clip_near(float actual, double expected)
{
    double delta = actual - expected, scale = expected < 0 ? -expected : expected;
    if (delta < 0) delta = -delta;
    return delta <= 0.00002 + scale*0.000002;
}
static int clip_cases(uint32_t count)
{
    const float guard[4] = {-4096,-4096,4095,4095};
    D9WGProcessViewport vp = {7,11,100,80,.25f,.75f};
    D9WGClipVertex result;
    const float boundaries[][4] = {{-1,0,.5f,1},{1,0,.5f,1},{0,1,.5f,1},
        {0,-1,.5f,1},{0,0,0,1},{0,0,1,1}};
    for (unsigned i = 0; i < 6; ++i) CLIP_CHECK(d9wg_clip_outcode(boundaries[i]) == 0);
    const float outside[][4] = {{-2,0,.5f,1},{2,0,.5f,1},{0,2,.5f,1},
        {0,-2,.5f,1},{0,0,-.5f,1},{0,0,2,1}};
    for (unsigned i = 0; i < 6; ++i) {
        d9wg_clip_vertex(outside[i], &vp, guard, 1, &result);
        CLIP_CHECK(result.code == (1u << i));
        CLIP_CHECK(result.projected == (i >= 4));
    }
    const float tiny[4] = {1e-8f,-1e-8f,5e-9f,1e-8f};
    d9wg_clip_vertex(tiny, &vp, guard, 1, &result);
    CLIP_CHECK(!result.projected && clip_near(result.screen[0],107) && clip_near(result.screen[1],91));
    CLIP_CHECK(clip_near(result.screen[2],.5) && clip_near(result.screen[3],1e8));
    uint32_t random = 0x4d2c1a5u;
    for (uint32_t n = 0; n < count; ++n) {
        float point[4], expected[4];uint32_t code = 0, clipping = n & 1u;
        for (unsigned i = 0; i < 4; ++i) {
            random = random*1664525u + 1013904223u;
            point[i] = ((int32_t)(random % 20001u) - 10000) / 64.0f;
        }
        point[3] = .5f + (float)(random & 63u) / 16.0f;
        if (n % 3u == 0) point[3] = -point[3];
        /* Independent plane-dot and double-precision projection oracles. */
        const double distances[6] = {(double)point[3]+point[0],(double)point[3]-point[0],
            (double)point[3]-point[1],(double)point[3]+point[1],point[2],(double)point[3]-point[2]};
        for (unsigned i = 0; i < 6; ++i) if (distances[i] < 0) code |= 1u << i;
        double reciprocal = 1.0 / point[3];
        double screen[4] = {vp.x + vp.width*.5 + point[0]*reciprocal*vp.width*.5,
            vp.y + vp.height*.5 - point[1]*reciprocal*vp.height*.5,
            vp.min_z + point[2]*reciprocal*(vp.max_z-vp.min_z),reciprocal};
        uint32_t projected = clipping && (point[2] < 0 || point[2] > point[3] || point[3] <= 0 ||
            screen[0] < -4096 || screen[1] < -4096 || screen[0] > 4095 || screen[1] > 4095);
        d9wg_clip_vertex(point, &vp, guard, clipping, &result);
        CLIP_CHECK(result.code == code && result.projected == projected);
        for (unsigned i = 0; i < 4; ++i) {
            CLIP_CHECK(result.clip[i] == point[i] && clip_near(result.screen[i],screen[i]));
            expected[i] = projected ? i < 2 ? (point[i]+point[3])*.5f : point[i] : (float)screen[i];
            CLIP_CHECK(clip_near(result.stored[i],expected[i]));
        }
    }
    return 0;
}
#undef CLIP_CHECK
#endif
