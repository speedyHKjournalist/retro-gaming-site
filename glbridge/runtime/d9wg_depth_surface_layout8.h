#ifndef D9WG_DEPTH_SURFACE_LAYOUT8_H
#define D9WG_DEPTH_SURFACE_LAYOUT8_H
#include <stdint.h>
#include "d9wg_format_layouts.h"
/* Logical API8 depth resource size, independent of CPU-shadow allocation and
 * MSAA sample count. Wine8 selects four-byte surface alignment. Only the six
 * legacy depth formats in the common registry are eligible; publish atomically. */
static inline int d9wg_depth_surface_layout8(uint32_t format, uint32_t width,
        uint32_t height, uint32_t *out_pitch, uint32_t *out_bytes)
{
    const D9WGFormatLayout *layout = d9wg_find_format(format);
    if (!out_pitch || !out_bytes || !width || !height || !layout
            || !layout->depth || !(layout->apis & 1u))
        return 0;
    uint64_t pitch = (uint64_t)width * layout->block_bytes;
    pitch = (pitch + 3u) & ~UINT64_C(3);
    if (pitch > UINT32_MAX)
        return 0;
    uint64_t bytes = pitch * height;
    if (bytes > UINT32_MAX)
        return 0;
    *out_pitch = (uint32_t)pitch;
    *out_bytes = (uint32_t)bytes;
    return 1;
}
#endif
