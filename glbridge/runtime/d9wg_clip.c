#include "d9wg_clip.h"
#include <stddef.h>

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

static double clip_distance(const D9WGClipPoint *point, unsigned plane)
{
    const float *p = point->data;
    switch (plane) {
    case 0: return (double)p[3] + p[0];
    case 1: return (double)p[3] - p[0];
    case 2: return (double)p[3] - p[1];
    case 3: return (double)p[3] + p[1];
    case 4: return p[2];
    default: return (double)p[3] - p[2];
    }
}
static void clip_copy(D9WGClipPoint *output, const D9WGClipPoint *input, uint32_t floats)
{
    for (uint32_t i = 0; i < floats; ++i) output->data[i] = input->data[i];
}
static void clip_intersect(D9WGClipPoint *output, const D9WGClipPoint *a,
        const D9WGClipPoint *b, double distance_a, double distance_b,
        uint32_t floats, unsigned plane)
{
    double t = distance_a / (distance_a - distance_b);
    for (uint32_t i = 0; i < floats; ++i)
        output->data[i] = (float)((double)a->data[i] + ((double)b->data[i] - a->data[i])*t);
    /* Keep each generated point exactly on its defining plane. */
    if (plane < 4) output->data[plane < 2 ? 0 : 1] =
            plane == 0 || plane == 3 ? -output->data[3] : output->data[3];
    else output->data[2] = plane == 4 ? 0.0f : output->data[3];
}
int d9wg_clip_triangle(const D9WGClipPoint *input, uint32_t floats, D9WGClipWork *work,
        const D9WGClipPoint **output, uint32_t *count)
{
    uint32_t vertices = 3, bank = 0;
    if (!input || !work || !output || !count || floats < 4 || floats > D9WG_CLIP_MAX_FLOATS) return 0;
    for (unsigned vertex = 0; vertex < 3; ++vertex) {
        for (unsigned component = 0; component < 4; ++component) {
            uint32_t bits;__builtin_memcpy(&bits, &input[vertex].data[component], 4);
            if ((bits & 0x7f800000u) == 0x7f800000u) return 0;
        }
        clip_copy(&work->polygon[bank][vertex], &input[vertex], floats);
    }
    for (unsigned plane = 0; plane < 6 && vertices; ++plane) {
        uint32_t next = bank ^ 1u, written = 0;
        const D9WGClipPoint *previous = &work->polygon[bank][vertices - 1u];
        double previous_distance = clip_distance(previous, plane);
        for (uint32_t vertex = 0; vertex < vertices; ++vertex) {
            const D9WGClipPoint *current = &work->polygon[bank][vertex];
            double distance = clip_distance(current, plane);
            if ((distance > 0 && previous_distance < 0) ||
                    (distance < 0 && previous_distance > 0)) {
                if (written >= D9WG_CLIP_MAX_VERTICES) return 0;
                clip_intersect(&work->polygon[next][written++], previous, current,
                        previous_distance, distance, floats, plane);
            }
            if (distance >= 0) {
                if (written >= D9WG_CLIP_MAX_VERTICES) return 0;
                clip_copy(&work->polygon[next][written++], current, floats);
            }
            previous = current;previous_distance = distance;
        }
        vertices = written;bank = next;
    }
    /* The homogeneous origin has no projected area. Keeping it would require
     * division by zero and could turn an otherwise clipped triangle into NaNs. */
    uint32_t written = 0;
    for (uint32_t vertex = 0; vertex < vertices; ++vertex)
        if (work->polygon[bank][vertex].data[3] > 0)
            clip_copy(&work->polygon[bank][written++], &work->polygon[bank][vertex], floats);
    *output = work->polygon[bank];*count = written;
    return 1;
}

int d9wg_clip_segment(const D9WGClipPoint *input, uint32_t floats,
        D9WGClipPoint *output, uint32_t *count)
{
    double begin = 0, end = 1;int begin_plane = -1, end_plane = -1;
    if (!input || !output || !count || floats < 4 || floats > D9WG_CLIP_MAX_FLOATS) return 0;
    for (unsigned v = 0; v < 2; ++v) for (unsigned c = 0; c < 4; ++c) {
        uint32_t bits;__builtin_memcpy(&bits, &input[v].data[c], 4);
        if ((bits & 0x7f800000u) == 0x7f800000u) return 0;
    }
    for (unsigned plane = 0; plane < 6; ++plane) {
        double a = clip_distance(&input[0], plane), b = clip_distance(&input[1], plane);
        if (a < 0 && b < 0) { *count = 0;return 1; }
        if (a < 0 || b < 0) {
            double t = a / (a - b);
            if (a < 0 && t > begin) { begin = t;begin_plane = (int)plane; }
            if (b < 0 && t < end) { end = t;end_plane = (int)plane; }
        }
    }
    if (begin > end) { *count = 0;return 1; }
    for (unsigned v = 0; v < 2; ++v) {
        int plane = v ? end_plane : begin_plane;
        if (plane < 0) clip_copy(&output[v], &input[v], floats);
        else clip_intersect(&output[v], &input[0], &input[1],
                clip_distance(&input[0], (unsigned)plane),
                clip_distance(&input[1], (unsigned)plane), floats, (unsigned)plane);
    }
    *count = output[0].data[3] > 0 && output[1].data[3] > 0 ? 2u : 0u;
    return 1;
}

uint32_t d9wg_clip_batch_bytes(uint32_t count)
{
    if (!count || count > (0xffffffffu - offsetof(D9WGClipBatch, vertex))/sizeof(D9WGClipVertex)) return 0;
    return (uint32_t)offsetof(D9WGClipBatch, vertex) + count*sizeof(D9WGClipVertex);
}
int d9wg_clip_batch_init(D9WGClipBatch *batch, uint32_t bytes, uint32_t first,
        uint32_t stride, uint32_t count, const D9WGProcessViewport *viewport)
{
    uint32_t required = d9wg_clip_batch_bytes(count);
    if (!batch || !viewport || !required || bytes < required || stride < 16 ||
            (uint64_t)first + (uint64_t)(count - 1u)*stride + 16u > 0x100000000ull) return 0;
    batch->next = 0;batch->first = first;batch->stride = stride;
    batch->count = count;batch->live = 0;batch->viewport = *viewport;
    for (uint32_t i = 0; i < count; ++i) batch->vertex[i].code = D9WG_CLIP_INVALID;
    return 1;
}
void d9wg_clip_buffer_commit(D9WGClipBuffer *buffer, D9WGClipBatch *batch,
        uint32_t offset, uint32_t bytes, D9WGClipRelease release, void *context)
{
    D9WGClipBatch **link = &buffer->head;
    while (*link) {
        D9WGClipBatch *old = *link;
        for (uint32_t i = 0; i < old->count; ++i) {
            uint64_t first = (uint64_t)old->first + (uint64_t)i*old->stride;
            if (bytes && old->vertex[i].code != D9WG_CLIP_INVALID &&
                    first < (uint64_t)offset + bytes && offset < first + 16u) {
                old->vertex[i].code = D9WG_CLIP_INVALID;--old->live;
            }
        }
        if (!old->live) { *link = old->next;release(context, old); }
        else link = &old->next;
    }
    if (batch) {
        for (uint32_t i = 0; i < batch->count; ++i)
            if (batch->vertex[i].code != D9WG_CLIP_INVALID) ++batch->live;
        if (!batch->live) release(context, batch);
        else { batch->next = buffer->head;buffer->head = batch; }
    }
}
const D9WGClipVertex *d9wg_clip_buffer_lookup(D9WGClipBuffer *buffer, uint32_t position,
        const void *stored, const D9WGProcessViewport **viewport)
{
    for (D9WGClipBatch *batch = buffer->head; batch; batch = batch->next) {
        if (position < batch->first) continue;
        uint32_t delta = position - batch->first, vertex = delta / batch->stride;
        if (delta % batch->stride || vertex >= batch->count ||
                batch->vertex[vertex].code == D9WG_CLIP_INVALID) continue;
        unsigned word = 0;
        for (; word < 4; ++word) {
            uint32_t actual, expected;
            __builtin_memcpy(&actual, (const uint8_t *)stored + word*4u, 4);
            __builtin_memcpy(&expected, &batch->vertex[vertex].stored[word], 4);
            if (actual != expected) break;
        }
        if (word == 4) { *viewport = &batch->viewport;return &batch->vertex[vertex]; }
        batch->vertex[vertex].code = D9WG_CLIP_INVALID;--batch->live;
    }
    return 0;
}
void d9wg_clip_buffer_free(D9WGClipBuffer *buffer, D9WGClipRelease release, void *context)
{
    while (buffer->head) {
        D9WGClipBatch *batch = buffer->head;buffer->head = batch->next;release(context, batch);
    }
}
