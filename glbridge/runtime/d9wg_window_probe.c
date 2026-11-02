#include <windows.h>
#include "d9wg_window_probe.h"
#include "../window_region.h"

static uint32_t window_span(LONG low, LONG high)
{
    return high > low ? (uint32_t)((int64_t)high - (int64_t)low) : 0;
}

void d9wg_window_probe_capture(uintptr_t value, uint32_t options,
        D9WGWindowProbe *probe)
{
    uint8_t *clear = (uint8_t *)probe;
    for (uint32_t i = 0; i < sizeof(*probe); ++i) clear[i] = 0;
    HWND window = (HWND)value;
    D9WGWindowState *state = &probe->state;
    state->hwnd = (uint32_t)value;
    if (options & (D9WG_WINDOW_PROBE_STATE | D9WG_WINDOW_PROBE_REGION))
        state->foreground_hwnd = (uint32_t)(uintptr_t)GetForegroundWindow();
    if ((options & D9WG_WINDOW_PROBE_IDENTITY_ONLY) || !window || !IsWindow(window)) return;
    state->flags = D9WG_WINDOW_IS_WINDOW;
    if (IsIconic(window)) state->flags |= D9WG_WINDOW_ICONIC;
    if (options & (D9WG_WINDOW_PROBE_STATE | D9WG_WINDOW_PROBE_REGION)) {
        if (IsWindowVisible(window)) state->flags |= D9WG_WINDOW_VISIBLE;
        if (state->hwnd == state->foreground_hwnd) state->flags |= D9WG_WINDOW_FOREGROUND;
    }
    RECT client = {0, 0, 0, 0};
    probe->client_succeeded = GetClientRect(window, &client) != 0;
    if (probe->client_succeeded) {
        state->client_width = window_span(client.left, client.right);
        state->client_height = window_span(client.top, client.bottom);
    }
    POINT origin = {0, 0};
    probe->origin_succeeded = ClientToScreen(window, &origin) != 0;
    if (probe->origin_succeeded) {state->window_x = origin.x;state->window_y = origin.y;}
    if ((options & D9WG_WINDOW_PROBE_OUTER) ||
            ((options & D9WG_WINDOW_PROBE_OUTER_IF_EMPTY) &&
                (!state->client_width || !state->client_height))) {
        RECT outer = {0, 0, 0, 0};
        probe->outer_succeeded = GetWindowRect(window, &outer) != 0;
        if (probe->outer_succeeded) {
            probe->outer_x = outer.left;probe->outer_y = outer.top;
            state->window_width = window_span(outer.left, outer.right);
            state->window_height = window_span(outer.top, outer.bottom);
        }
    }
    if ((options & D9WG_WINDOW_PROBE_REGION) && (state->flags & D9WG_WINDOW_VISIBLE)
            && !(state->flags & D9WG_WINDOW_ICONIC) && probe->client_succeeded
            && probe->origin_succeeded && state->client_width && state->client_height) {
        V86WindowRegion region;
        int kind = v86_window_visible_region_prepared(window, &client, &origin, &region);
        if (kind == V86_WINDOW_REGION_NONE) state->flags |= D9WG_WINDOW_OCCLUDED;
        if (kind == V86_WINDOW_REGION_PART) state->flags |= D9WG_WINDOW_REGION;
        probe->region.rect_count = region.count;
        for (uint32_t i = 0; i < region.count; ++i)
            for (uint32_t j = 0; j < 4; ++j) probe->region.rects[i][j] = region.rects[i][j];
    }
}
