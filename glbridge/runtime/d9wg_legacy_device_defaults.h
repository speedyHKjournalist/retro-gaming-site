#ifndef D9WG_LEGACY_DEVICE_DEFAULTS_H
#define D9WG_LEGACY_DEVICE_DEFAULTS_H
#include <stdint.h>
typedef struct D9WGLegacyDeviceDefaults {
    uint32_t version, color_key, specular, normalize_normals;
    uint32_t texture_map_blend, legacy_texture_blending;
} D9WGLegacyDeviceDefaults;
static inline D9WGLegacyDeviceDefaults d9wg_legacy_device_defaults(uint32_t api)
{
    D9WGLegacyDeviceDefaults defaults;
    defaults.version = api == 1u || api == 2u || api == 3u ? api : 7u;
    defaults.color_key = defaults.version == 1u;
    defaults.specular = defaults.version == 2u;
    defaults.normalize_normals = defaults.version < 7u;
    defaults.texture_map_blend = defaults.version < 7u ? 2u : 0u;
    defaults.legacy_texture_blending = defaults.version < 7u;
    return defaults;
}
/* D3DTOP_SELECTARG1 / SELECTARG2. Legacy MODULATE selects texture alpha
 * when available (also when unbound), otherwise the current diffuse alpha. */
static inline uint32_t d9wg_legacy_modulate_alpha_op(int bound, int alpha_mask)
{
    return !bound || alpha_mask ? 2u : 3u;
}
#endif
