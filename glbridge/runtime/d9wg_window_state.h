#ifndef D9WG_WINDOW_STATE_H
#define D9WG_WINDOW_STATE_H
#include "../d3d9proxy/d3d9_protocol.h"

/* Pure report mechanics. Win32 probing, API lifetime and flush policy stay
 * with the caller. Cache only a report that was successfully queued/sent. */
typedef struct D9WGWindowCache {
    uint32_t valid, bytes;
    D9WGWindowStateWithRegion report;
} D9WGWindowCache;

/* Input rectangles are bounded by D9WG_WINDOW_REGION_MAX_RECTS and already
 * clipped by window_region.h. Unused tail bytes never enter the comparison. */
void d9wg_window_report_region(D9WGWindowStateWithRegion *report,
        uint32_t count, const int32_t rects[][4]);
int d9wg_window_cache_matches(const D9WGWindowCache *cache,
        const D9WGWindowStateWithRegion *report);
void d9wg_window_cache_commit(D9WGWindowCache *cache,
        const D9WGWindowStateWithRegion *report);
void d9wg_window_cache_invalidate(D9WGWindowCache *cache);
#endif
