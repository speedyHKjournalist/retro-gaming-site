#include "d9wg_window_probe.h"

void d9wg_window_probe_geometry(const D9WGWindowProbe *probe, uint32_t policy,
        uint32_t fallback_width, uint32_t fallback_height,
        D9WGWindowGeometry *geometry)
{
    const D9WGWindowState *state = &probe->state;
    geometry->x = geometry->y = 0;
    geometry->width = geometry->height = 0;
    if (policy == D9WG_WINDOW_GEOMETRY_D3D8) {
        if (!(state->flags & D9WG_WINDOW_IS_WINDOW)
                || (state->flags & D9WG_WINDOW_ICONIC)
                || !probe->client_succeeded || !probe->origin_succeeded) return;
        geometry->x = state->window_x;geometry->y = state->window_y;
        geometry->width = state->client_width;geometry->height = state->client_height;
        return;
    }
    geometry->width = fallback_width;geometry->height = fallback_height;
    if (policy == D9WG_WINDOW_GEOMETRY_DDRAW) {
        if (probe->client_succeeded) {
            if (probe->origin_succeeded) {
                geometry->x = state->window_x;geometry->y = state->window_y;
            }
            if (state->client_width) geometry->width = state->client_width;
            if (state->client_height) geometry->height = state->client_height;
        }
        return;
    }
    geometry->x = state->window_x;geometry->y = state->window_y;
    if (probe->client_succeeded) {
        geometry->width = state->client_width;geometry->height = state->client_height;
    }
    if ((!geometry->width || !geometry->height) && probe->outer_succeeded
            && state->window_width && state->window_height) {
        geometry->x = probe->outer_x;geometry->y = probe->outer_y;
        geometry->width = state->window_width;geometry->height = state->window_height;
    }
}

void d9wg_window_probe_report(const D9WGWindowProbe *probe, uint32_t device,
        int fullscreen, uint32_t policy, D9WGWindowStateWithRegion *report)
{
    report->state = probe->state;
    report->state.device_handle = device;
    if (fullscreen) report->state.flags |= D9WG_WINDOW_FULLSCREEN;
    uint32_t count = probe->region.rect_count;
    if (policy & D9WG_WINDOW_REPORT_CLIENT_SIZE) {
        D9WGWindowGeometry geometry;
        d9wg_window_probe_geometry(probe, D9WG_WINDOW_GEOMETRY_D3D8, 0, 0, &geometry);
        report->state.window_x = geometry.x;report->state.window_y = geometry.y;
        report->state.window_width = report->state.client_width = geometry.width;
        report->state.window_height = report->state.client_height = geometry.height;
        if (!(probe->state.flags & D9WG_WINDOW_IS_WINDOW)) report->state.flags = 0;
    }
    if (policy & D9WG_WINDOW_REPORT_HIDDEN) {
        report->state.flags = 0;
        report->state.window_x = report->state.window_y = 0;
        report->state.window_width = report->state.window_height = 0;
        report->state.client_width = report->state.client_height = 0;
        count = 0;
    }
    if (policy & D9WG_WINDOW_REPORT_NO_SURFACE) {
        report->state.flags = (report->state.flags &
            ~(D9WG_WINDOW_VISIBLE | D9WG_WINDOW_OCCLUDED | D9WG_WINDOW_REGION)) | D9WG_WINDOW_NO_SURFACE;
        report->state.window_x = probe->outer_x;report->state.window_y = probe->outer_y;
        count = 0;
    }
    if (policy & D9WG_WINDOW_REPORT_ZERO_ORIGIN)
        report->state.window_x = report->state.window_y = 0;
    d9wg_window_report_region(report, count, probe->region.rects);
}
