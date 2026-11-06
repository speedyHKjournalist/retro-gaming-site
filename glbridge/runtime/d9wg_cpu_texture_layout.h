#ifndef D9WG_CPU_TEXTURE_LAYOUT_H
#define D9WG_CPU_TEXTURE_LAYOUT_H
#include "d9wg_format_layouts.h"

/* ATI FOURCC mapping preserves the native byte-per-pixel public pitch,
 * independently of the compressed block pitch used for GPU transfers. */
static __inline int d9wg_cpu_texture_broken_pitch(uint32_t api, uint32_t format)
{
    return api == 9u && (format == 0x31495441u || format == 0x32495441u);
}

static __inline int d9wg_cpu_texture_span(uint32_t api, uint32_t format,
        uint32_t width, uint32_t height, uint32_t bytes, uint32_t *span)
{
    if (d9wg_cpu_texture_broken_pitch(api, format)) {
        if (height && width > UINT32_MAX / height) return 0;
        *span = width * height;
    } else *span = bytes;
    return 1;
}

static __inline int d9wg_cpu_texture_level_layout(uint32_t api, uint32_t format,
        uint32_t width, uint32_t height, uint32_t *pitch, uint32_t *rows, uint32_t *bytes)
{
    uint32_t span;
    if (!d9wg_texture_level_layout(api, format, width, height, pitch, rows, bytes)
            || !d9wg_cpu_texture_span(api, format, width, height, *bytes, &span))
        return 0;
    /* Both the advertised map and a full compressed block must fit. */
    if (span > *bytes) *bytes = span;
    return 1;
}
#endif
