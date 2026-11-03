#ifndef D9WG_LEGACY_VIEWPORT_H
#define D9WG_LEGACY_VIEWPORT_H
#include <stdint.h>
typedef struct D9WGLegacyViewportClip {
    float scale[3], offset[3];
} D9WGLegacyViewportClip;
/* The D3D7 viewport remains at depth [0,1]. Legacy viewports modify clip
 * space after the application's logical projection, before division by W. */
static inline D9WGLegacyViewportClip d9wg_legacy_viewport1_clip(
        uint32_t width, uint32_t height, float scale_x, float scale_y)
{
    D9WGLegacyViewportClip clip = {{2.0f * scale_x / width,
        2.0f * scale_y / height, 1.0f}, {0.0f, 0.0f, 0.0f}};
    return clip;
}
static inline D9WGLegacyViewportClip d9wg_legacy_viewport2_clip(float x,
        float y, float width, float height, float min_z, float max_z)
{
    D9WGLegacyViewportClip clip = {{2.0f / width, 2.0f / height,
        1.0f / (max_z - min_z)}, {-2.0f * x / width - 1.0f,
        -2.0f * y / height + 1.0f, -min_z / (max_z - min_z)}};
    return clip;
}
#endif
