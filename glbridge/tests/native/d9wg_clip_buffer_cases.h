#ifndef D9WG_CLIP_BUFFER_CASES_H
#define D9WG_CLIP_BUFFER_CASES_H
#include "runtime/d9wg_clip.h"
#define CLIP_BUFFER_CHECK(x)                                                                                 \
    do                                                                                                       \
    {                                                                                                        \
        if (!(x))                                                                                            \
            return __LINE__;                                                                                 \
    } while (0)
static void clip_test_release(void* context, void* allocation)
{
    (void)allocation;
    ++*(uint32_t*)context;
}
static int clip_buffer_cases(void)
{
    union
    {
        uint64_t aligned;
        uint8_t bytes[sizeof(D9WGClipBatch) + 3 * sizeof(D9WGClipVertex)];
    } storage[4];
    D9WGClipBatch *a = (D9WGClipBatch*)storage[0].bytes, *b = (D9WGClipBatch*)storage[1].bytes;
    D9WGClipBatch *c = (D9WGClipBatch*)storage[2].bytes, *d = (D9WGClipBatch*)storage[3].bytes;
    D9WGProcessViewport vp = { 7, 11, 100, 80, .25f, .75f };
    const D9WGProcessViewport* found_vp = 0;
    D9WGClipBuffer buffer = { 0 };
    uint32_t released = 0, bytes = d9wg_clip_batch_bytes(3);
    CLIP_BUFFER_CHECK(!d9wg_clip_batch_bytes(0) && !d9wg_clip_batch_bytes(0xffffffffu));
    CLIP_BUFFER_CHECK(!d9wg_clip_batch_init(a, bytes - 1, 4, 24, 3, &vp));
    CLIP_BUFFER_CHECK(!d9wg_clip_batch_init(a, bytes, 0xfffffff0u, 16, 2, &vp));
    CLIP_BUFFER_CHECK(!d9wg_clip_batch_init(a, bytes, 0, 15, 3, &vp));
    CLIP_BUFFER_CHECK(d9wg_clip_batch_init(a, bytes, 4, 24, 3, &vp));
    for (unsigned v = 0; v < 3; v++)
    {
        a->vertex[v].code = 0;
        for (unsigned word = 0; word < 4; word++)
            a->vertex[v].stored[word] = (float)(v * 4 + word);
    }
    d9wg_clip_buffer_commit(&buffer, a, 0, 76, clip_test_release, &released);
    CLIP_BUFFER_CHECK(buffer.head == a && a->live == 3 && !released);
    CLIP_BUFFER_CHECK(d9wg_clip_buffer_lookup(&buffer, 4, a->vertex[0].stored, &found_vp) == &a->vertex[0]);
    CLIP_BUFFER_CHECK(found_vp == &a->viewport && found_vp->min_z == .25f);
    CLIP_BUFFER_CHECK(!d9wg_clip_buffer_lookup(&buffer, 5, a->vertex[0].stored, &found_vp));
    float changed[4] = { 4, 5, 6, 8 };
    CLIP_BUFFER_CHECK(!d9wg_clip_buffer_lookup(&buffer, 28, changed, &found_vp) && a->live == 2);
    CLIP_BUFFER_CHECK(!d9wg_clip_buffer_lookup(&buffer, 28, a->vertex[1].stored, &found_vp));
    vp.x = 19;
    CLIP_BUFFER_CHECK(d9wg_clip_batch_init(b, bytes, 52, 24, 1, &vp));
    b->vertex[0].code = 16;
    for (unsigned word = 0; word < 4; word++)
        b->vertex[0].stored[word] = (float)(19 + word);
    d9wg_clip_buffer_commit(&buffer, b, 52, 24, clip_test_release, &released);
    CLIP_BUFFER_CHECK(buffer.head == b && b->next == a && a->live == 1);
    CLIP_BUFFER_CHECK(d9wg_clip_buffer_lookup(&buffer, 52, b->vertex[0].stored, &found_vp) == &b->vertex[0]);
    CLIP_BUFFER_CHECK(found_vp->x == 19);
    CLIP_BUFFER_CHECK(!d9wg_clip_buffer_lookup(&buffer, 52, a->vertex[2].stored, &found_vp));
    d9wg_clip_buffer_commit(&buffer, 0, 8, 0, clip_test_release, &released);
    CLIP_BUFFER_CHECK(released == 1 && buffer.head == a && a->live == 1);
    d9wg_clip_buffer_commit(&buffer, 0, 8, 4, clip_test_release, &released);
    CLIP_BUFFER_CHECK(!buffer.head && released == 2);
    CLIP_BUFFER_CHECK(d9wg_clip_batch_init(c, bytes, 100, 20, 3, &vp));
    for (unsigned v = 0; v < 3; v++)
        c->vertex[v].code = 0;
    d9wg_clip_buffer_commit(&buffer, c, 100, 60, clip_test_release, &released);
    CLIP_BUFFER_CHECK(d9wg_clip_batch_init(d, bytes, 108, 16, 1, &vp));
    d->vertex[0].code = 0;
    d9wg_clip_buffer_commit(&buffer, d, 108, 16, clip_test_release, &released);
    CLIP_BUFFER_CHECK(c->live == 1 && c->vertex[2].code == 0 && d->live == 1);
    d9wg_clip_buffer_free(&buffer, clip_test_release, &released);
    CLIP_BUFFER_CHECK(!buffer.head && released == 4);
    d9wg_clip_buffer_free(&buffer, clip_test_release, &released);
    CLIP_BUFFER_CHECK(released == 4);
    return 0;
}
#undef CLIP_BUFFER_CHECK
#endif
