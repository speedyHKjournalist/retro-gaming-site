#include "d9wg_clip.h"

uint32_t d9wg_clip_outcode(const float *clip)
{
    uint32_t code = 0;
    if (clip[0] < -clip[3]) code |= 1u;
    if (clip[0] > clip[3]) code |= 2u;
    if (clip[1] > clip[3]) code |= 4u;
    if (clip[1] < -clip[3]) code |= 8u;
    if (clip[2] < 0.0f) code |= 16u;
    if (clip[2] > clip[3]) code |= 32u;
    return code;
}
void d9wg_clip_project(const float *clip, const D9WGProcessViewport *viewport,
        float *screen)
{
    /* Preserve the established zero-w fallback until its native behavior is
     * independently measured; finite nonzero w must never be epsilon-clamped. */
    float rhw = clip[3] != 0.0f ? 1.0f / clip[3] : 1.0f;
    screen[0] = (clip[0]*rhw*0.5f + 0.5f)*viewport->width + viewport->x;
    screen[1] = (0.5f - clip[1]*rhw*0.5f)*viewport->height + viewport->y;
    screen[2] = viewport->min_z + clip[2]*rhw*(viewport->max_z - viewport->min_z);
    screen[3] = rhw;
}
void d9wg_clip_vertex(const float *clip, const D9WGProcessViewport *viewport,
        const float *guard_band, uint32_t clipping, D9WGClipVertex *vertex)
{
    d9wg_clip_project(clip, viewport, vertex->screen);
    vertex->code = d9wg_clip_outcode(clip);
    vertex->projected = clipping && ((vertex->code & 48u) || clip[3] <= 0.0f ||
            vertex->screen[0] < guard_band[0] || vertex->screen[1] < guard_band[1] ||
            vertex->screen[0] > guard_band[2] || vertex->screen[1] > guard_band[3]);
    for (unsigned i = 0; i < 4; ++i) {
        vertex->clip[i] = clip[i];vertex->stored[i] = vertex->screen[i];
    }
    if (vertex->projected) {
        vertex->stored[0] = (clip[0] + clip[3])*0.5f;
        vertex->stored[1] = (clip[1] + clip[3])*0.5f;
        vertex->stored[2] = clip[2];vertex->stored[3] = clip[3];
    }
}
