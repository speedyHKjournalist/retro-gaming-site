#include "d9wg_window_state.h"

void d9wg_window_report_region(D9WGWindowStateWithRegion *report,
        uint32_t count, const int32_t rects[][4])
{
    report->region.rect_count = count;
    for (uint32_t i = 0; i < count; ++i)
        for (uint32_t j = 0; j < 4; ++j)
            report->region.rects[i][j] = rects[i][j];
}

int d9wg_window_cache_matches(const D9WGWindowCache *cache,
        const D9WGWindowStateWithRegion *report)
{
    uint32_t bytes = (uint32_t)D9WG_WINDOW_STATE_BYTES(report);
    if (!cache->valid || cache->bytes != bytes) return 0;
    const uint8_t *a = (const uint8_t *)&cache->report;
    const uint8_t *b = (const uint8_t *)report;
    for (uint32_t i = 0; i < bytes; ++i)
        if (a[i] != b[i]) return 0;
    return 1;
}

void d9wg_window_cache_commit(D9WGWindowCache *cache,
        const D9WGWindowStateWithRegion *report)
{
    uint32_t bytes = (uint32_t)D9WG_WINDOW_STATE_BYTES(report);
    uint8_t *to = (uint8_t *)&cache->report;
    const uint8_t *from = (const uint8_t *)report;
    for (uint32_t i = 0; i < bytes; ++i) to[i] = from[i];
    cache->bytes = bytes;
    cache->valid = 1;
}

void d9wg_window_cache_invalidate(D9WGWindowCache *cache)
{
    cache->valid = 0;
}
