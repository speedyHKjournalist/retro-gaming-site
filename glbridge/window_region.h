/*
 * The part of a window's client area that is on the guest's screen, for the
 * window-state reports of the graphics proxies (D9WG_WINDOW_REGION in
 * d3d9proxy/d3d9_protocol.h, V86GL_CTRL_WINDOW_STATE in the OpenGL proxy).
 *
 * The host shows a device's frames as a layer over the guest's desktop. Where
 * another guest window lies over the device window -- a message box, an IME
 * candidate list, the Alt+Tab switcher, a video popup -- the layer must not be
 * drawn, or it hides that window. Without a desktop compositor (Windows 2000
 * and XP) the system region of a window DC is exactly the part of the window
 * nothing else is drawn over; GetRandomRgn(SYSRGN) returns it in screen
 * coordinates.
 *
 * Windows 9x returns that region in other coordinates, and under a desktop
 * compositor (Vista and later) it is always the whole window; both report
 * only "all" or "none" (from GetClipBox). A region of more rectangles than a
 * report carries is reported as "all", which is how every window was shown
 * before regions were reported.
 *
 * Header-only and free of the C runtime: the proxies link with -nostdlib.
 */
#ifndef V86_WINDOW_REGION_H
#define V86_WINDOW_REGION_H

#include <windows.h>
#include <stdint.h>

#ifndef SYSRGN
#define SYSRGN 4
#endif

#define V86_WINDOW_REGION_MAX_RECTS 32u

/* All of the client area is on screen (or it cannot be told) */
#define V86_WINDOW_REGION_ALL  0
/* None of it: other windows cover it entirely */
#define V86_WINDOW_REGION_NONE 1
/* The rectangles in V86WindowRegion */
#define V86_WINDOW_REGION_PART 2

typedef struct V86WindowRegion {
    uint32_t count;
    /* left, top, right, bottom in client coordinates; disjoint, as
     * GetRegionData lists them (y-x banded) */
    int32_t rects[V86_WINDOW_REGION_MAX_RECTS][4];
} V86WindowRegion;

static BOOL v86_window_region_nt(void)
{
    return (GetVersion() & 0x80000000u) == 0;
}

/* "none" or "all" from the clip box, which every Windows version reports in
 * the DC's own coordinates */
static int v86_window_clip_box_kind(HWND window)
{
    RECT box;
    HDC dc = GetDC(window);
    int kind;

    if (!dc)
        return V86_WINDOW_REGION_ALL;
    kind = GetClipBox(dc, &box);
    ReleaseDC(window, dc);
    return kind == NULLREGION ? V86_WINDOW_REGION_NONE : V86_WINDOW_REGION_ALL;
}

/*
 * What of `window`'s client area shows: one of V86_WINDOW_REGION_*, with
 * `region` filled in for V86_WINDOW_REGION_PART (its count is 0 otherwise).
 * A hidden, minimised or empty window is "all": whether it shows at all is
 * reported separately (D9WG_WINDOW_VISIBLE, D9WG_WINDOW_ICONIC), and an empty
 * client rect is what some fullscreen windows report.
 */
static int v86_window_visible_region(HWND window, V86WindowRegion *region)
{
    struct {
        RGNDATAHEADER header;
        RECT rects[V86_WINDOW_REGION_MAX_RECTS];
    } data;
    RECT client;
    RECT box;
    POINT origin;
    HRGN visible;
    HRGN client_region;
    HDC dc;
    int kind;
    int result = V86_WINDOW_REGION_ALL;
    DWORD size;
    uint32_t i;

    region->count = 0;
    if (!window || !IsWindow(window) || !IsWindowVisible(window)
            || IsIconic(window))
        return V86_WINDOW_REGION_ALL;
    if (!GetClientRect(window, &client) || IsRectEmpty(&client))
        return V86_WINDOW_REGION_ALL;
    if (!v86_window_region_nt())
        return v86_window_clip_box_kind(window);

    dc = GetDC(window);
    if (!dc)
        return V86_WINDOW_REGION_ALL;
    visible = CreateRectRgn(0, 0, 0, 0);
    client_region = CreateRectRgnIndirect(&client);
    if (!visible || !client_region || GetRandomRgn(dc, visible, SYSRGN) != 1) {
        /* No system region to go by: the clip box still tells "none" */
        if (visible) DeleteObject(visible);
        if (client_region) DeleteObject(client_region);
        ReleaseDC(window, dc);
        return v86_window_clip_box_kind(window);
    }
    ReleaseDC(window, dc);

    origin.x = 0;
    origin.y = 0;
    ClientToScreen(window, &origin);
    OffsetRgn(visible, -origin.x, -origin.y);
    kind = CombineRgn(visible, visible, client_region, RGN_AND);
    if (kind == NULLREGION) {
        result = V86_WINDOW_REGION_NONE;
    } else if (kind == SIMPLEREGION) {
        GetRgnBox(visible, &box);
        if (!EqualRect(&box, &client)) {
            region->rects[0][0] = box.left;
            region->rects[0][1] = box.top;
            region->rects[0][2] = box.right;
            region->rects[0][3] = box.bottom;
            region->count = 1;
            result = V86_WINDOW_REGION_PART;
        }
    } else if (kind == COMPLEXREGION) {
        size = GetRegionData(visible, 0, NULL);
        if (size && size <= sizeof(data)
                && GetRegionData(visible, sizeof(data), (RGNDATA *)&data)
                && data.header.nCount <= V86_WINDOW_REGION_MAX_RECTS) {
            for (i = 0; i < data.header.nCount; ++i) {
                region->rects[i][0] = data.rects[i].left;
                region->rects[i][1] = data.rects[i].top;
                region->rects[i][2] = data.rects[i].right;
                region->rects[i][3] = data.rects[i].bottom;
            }
            region->count = data.header.nCount;
            result = V86_WINDOW_REGION_PART;
        }
    }
    DeleteObject(visible);
    DeleteObject(client_region);
    return result;
}

static BOOL v86_window_region_equal(const V86WindowRegion *a,
        const V86WindowRegion *b)
{
    uint32_t i;
    uint32_t j;

    if (a->count != b->count)
        return FALSE;
    for (i = 0; i < a->count; ++i)
        for (j = 0; j < 4; ++j)
            if (a->rects[i][j] != b->rects[i][j])
                return FALSE;
    return TRUE;
}

static void v86_window_region_copy(V86WindowRegion *to,
        const V86WindowRegion *from)
{
    uint32_t i;
    uint32_t j;

    to->count = from->count;
    for (i = 0; i < from->count; ++i)
        for (j = 0; j < 4; ++j)
            to->rects[i][j] = from->rects[i][j];
}

#endif
