#ifndef D9WG_LIGHTING_BRIDGE_H
#define D9WG_LIGHTING_BRIDGE_H
#include <stddef.h>
#include "d9wg_lighting.h"
/* All three public SDKs use the same material/light data layout. Keep this
 * translation checked against each frontend's actual SDK types. */
#define D9WG_SDK_ASSERT(name, condition) typedef char name[(condition) ? 1 : -1]
D9WG_SDK_ASSERT(d9wg_material_sdk_size, sizeof(D9WG_LIGHTING_SDK_MATERIAL) == sizeof(D9WGSetMaterial)-4u);
D9WG_SDK_ASSERT(d9wg_light_sdk_size, sizeof(D9WG_LIGHTING_SDK_LIGHT) == sizeof(D9WGSetLight)-8u);
#ifdef D9WG_LIGHTING_LEGACY7
D9WG_SDK_ASSERT(d9wg_material_sdk_order, offsetof(D9WG_LIGHTING_SDK_MATERIAL, power) == 64u);
D9WG_SDK_ASSERT(d9wg_light_sdk_order, offsetof(D9WG_LIGHTING_SDK_LIGHT, dvPosition) == 52u &&
    offsetof(D9WG_LIGHTING_SDK_LIGHT, dcvAmbient) == 36u && offsetof(D9WG_LIGHTING_SDK_LIGHT, dvPhi) == 100u);
#else
D9WG_SDK_ASSERT(d9wg_material_sdk_order, offsetof(D9WG_LIGHTING_SDK_MATERIAL, Power) == 64u);
D9WG_SDK_ASSERT(d9wg_light_sdk_order, offsetof(D9WG_LIGHTING_SDK_LIGHT, Position) == 52u &&
    offsetof(D9WG_LIGHTING_SDK_LIGHT, Ambient) == 36u && offsetof(D9WG_LIGHTING_SDK_LIGHT, Phi) == 100u);
#endif
#undef D9WG_SDK_ASSERT

static __inline uint32_t d9wg_lighting_sdk_word(const void *source, uint32_t index)
{
    uint32_t value;__builtin_memcpy(&value, (const uint8_t *)source + index*4u, 4u);return value;
}
static __inline void d9wg_lighting_sdk_copy(void *output, const void *input, uint32_t bytes)
{
    for (uint32_t at = 0; at < bytes; at += 4u) {
        uint32_t value;__builtin_memcpy(&value, (const uint8_t *)input + at, 4u);
        __builtin_memcpy((uint8_t *)output + at, &value, 4u);
    }
}
static __inline void d9wg_lighting_sdk_prepare(D9WGTNLLighting *output, const void *material,
        const void *render_states, const void *world_view)
{
    D9WGSetMaterial wire;
    D9WGTNLLightingFlags flags;
    wire.device_handle = 0;
    d9wg_lighting_sdk_copy(wire.diffuse, material, sizeof(wire)-4u);
    flags.lighting = d9wg_lighting_sdk_word(render_states, 137);
    flags.color_vertex = d9wg_lighting_sdk_word(render_states, 141);
    flags.specular_enable = d9wg_lighting_sdk_word(render_states, 29);
    flags.local_viewer = d9wg_lighting_sdk_word(render_states, 142);
    flags.normalize_normals = d9wg_lighting_sdk_word(render_states, 143);
    flags.diffuse_source = d9wg_lighting_sdk_word(render_states, 145);
    flags.ambient_source = d9wg_lighting_sdk_word(render_states, 147);
    flags.specular_source = d9wg_lighting_sdk_word(render_states, 146);
    flags.emissive_source = d9wg_lighting_sdk_word(render_states, 148);
    d9wg_lighting_init(output, &wire, d9wg_lighting_sdk_word(render_states, 139), &flags);
    d9wg_lighting_geometry(output, world_view);
}
static __inline int d9wg_lighting_sdk_add(D9WGTNLLighting *state, uint32_t index, const void *light, const void *view)
{
    D9WGSetLight wire;
    wire.device_handle = 0;wire.index = index;
    d9wg_lighting_sdk_copy(&wire.type, light, sizeof(wire)-8u);
    return d9wg_lighting_add(state, &wire, view);
}
#endif
