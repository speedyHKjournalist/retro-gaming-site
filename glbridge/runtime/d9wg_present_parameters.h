#ifndef D9WG_PRESENT_PARAMETERS_H
#define D9WG_PRESENT_PARAMETERS_H
#include <stdint.h>

/* The SDK defines D3DPRESENT_PARAMETERS before this shared header, using the
 * correct D3D8 or D3D9 layout in each frontend. No cross-layout casts. */
static inline uint32_t d9wg_present_extent(uint32_t requested,
    uint32_t client, uint32_t fallback)
{
    return requested ? requested : client ? client : fallback;
}

static inline void d9wg_present_parameters_effective(
    D3DPRESENT_PARAMETERS* effective, const D3DPRESENT_PARAMETERS* requested,
    uint32_t width, uint32_t height, uint32_t format, HWND window)
{
    *effective = *requested;
    effective->BackBufferWidth = width;
    effective->BackBufferHeight = height;
    effective->BackBufferFormat = format;
    effective->BackBufferCount = requested->BackBufferCount ? requested->BackBufferCount : 1;
    effective->hDeviceWindow = window;
}

static inline void d9wg_present_parameters_return(uint32_t api,
    D3DPRESENT_PARAMETERS* requested, const D3DPRESENT_PARAMETERS* effective)
{
    /* Wine's Windows tests preserve D3D8 caller dimensions. D3D9 returns the
     * effective dimensions/format; both return the normalized count. The
     * caller's hDeviceWindow stays as supplied, including NULL fallback. */
    requested->BackBufferCount = effective->BackBufferCount;
    if (api == 9) {
        requested->BackBufferWidth = effective->BackBufferWidth;
        requested->BackBufferHeight = effective->BackBufferHeight;
        requested->BackBufferFormat = effective->BackBufferFormat;
    }
}
#endif
