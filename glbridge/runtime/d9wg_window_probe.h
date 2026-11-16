#ifndef D9WG_WINDOW_PROBE_H
#define D9WG_WINDOW_PROBE_H
#include "d9wg_window_state.h"

/* One Win32 observation, reusable by a Present and its state report. The
 * geometry selectors below preserve each frontend's explicit legacy policy. */
#define D9WG_WINDOW_PROBE_STATE 1u
#define D9WG_WINDOW_PROBE_REGION 2u
#define D9WG_WINDOW_PROBE_OUTER 4u
#define D9WG_WINDOW_PROBE_OUTER_IF_EMPTY 8u
#define D9WG_WINDOW_PROBE_IDENTITY_ONLY 16u

typedef struct D9WGWindowProbe {
    D9WGWindowState state;
    D9WGWindowRegion region;
    int32_t outer_x, outer_y;
    uint32_t client_succeeded, origin_succeeded, outer_succeeded;
} D9WGWindowProbe;

typedef struct D9WGWindowGeometry {
    int32_t x, y;
    uint32_t width, height;
} D9WGWindowGeometry;

/* REGION implies STATE; geometry-only capture does no DC/region work. */
void d9wg_window_probe_capture(uintptr_t window, uint32_t options,
    D9WGWindowProbe* probe);

#define D9WG_WINDOW_GEOMETRY_D3D9 0u
#define D9WG_WINDOW_GEOMETRY_D3D8 1u
#define D9WG_WINDOW_GEOMETRY_DDRAW 2u
void d9wg_window_probe_geometry(const D9WGWindowProbe* probe, uint32_t policy,
    uint32_t fallback_width, uint32_t fallback_height,
    D9WGWindowGeometry* geometry);

#define D9WG_WINDOW_REPORT_CLIENT_SIZE 1u
#define D9WG_WINDOW_REPORT_HIDDEN 2u
#define D9WG_WINDOW_REPORT_NO_SURFACE 4u
#define D9WG_WINDOW_REPORT_ZERO_ORIGIN 8u
/* A state-capable probe is required; fullscreen and API-specific shape are
 * explicit inputs, while all raw window facts come from the same capture. */
void d9wg_window_probe_report(const D9WGWindowProbe* probe, uint32_t device,
    int fullscreen, uint32_t policy, D9WGWindowStateWithRegion* report);
#endif
