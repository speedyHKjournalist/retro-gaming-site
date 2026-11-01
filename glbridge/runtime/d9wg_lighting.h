#ifndef D9WG_LIGHTING_H
#define D9WG_LIGHTING_H
#include <stdint.h>
#include "../d3d9proxy/d3d9_protocol.h"
#define D9WG_TNL_MAX_LIGHTS 8u

typedef struct D9WGTNLLightingFlags {
    uint32_t lighting, color_vertex, specular_enable, local_viewer, normalize_normals;
    uint32_t diffuse_source, ambient_source, specular_source, emissive_source;
} D9WGTNLLightingFlags;
typedef struct D9WGTNLPreparedLight {
    uint32_t index, type;
    float diffuse[3], specular[3], ambient[3], position[3], direction[3];
    float range, falloff, attenuation[3], cos_theta, cos_phi;
} D9WGTNLPreparedLight;
typedef struct D9WGTNLLighting {
    D9WGSetMaterial material;
    D9WGTNLLightingFlags flags;
    float ambient[3], normal_matrix[16];
    uint32_t light_count;
    D9WGTNLPreparedLight lights[D9WG_TNL_MAX_LIGHTS];
} D9WGTNLLighting;

/* State is prepared once per software vertex operation, in view space. */
void d9wg_lighting_init(D9WGTNLLighting *state, const D9WGSetMaterial *material,
        uint32_t ambient_argb, const D9WGTNLLightingFlags *flags);
void d9wg_lighting_geometry(D9WGTNLLighting *state, const void *world_view);
void d9wg_lighting_normal(const D9WGTNLLighting *state, const float *normal, float *eye_normal);
void d9wg_lighting_vertex(const D9WGTNLLighting *state, const void *world_view, const float *position,
        const void *normal, const void *color0, const void *color1, uint32_t *diffuse, uint32_t *specular);
void d9wg_lighting_vertex_view(const D9WGTNLLighting *state, const float *eye_position,
        const float *eye_normal, const void *color0, const void *color1, uint32_t *diffuse, uint32_t *specular);
int d9wg_lighting_add(D9WGTNLLighting *state, const D9WGSetLight *light, const void *view_matrix);
void d9wg_lighting_color(uint32_t argb, float *rgba);
void d9wg_lighting_shade(const D9WGTNLLighting *state, const float *eye_position,
        const float *eye_normal, const float *color0, const float *color1,
        uint32_t color_mask, float *diffuse, float *specular);
#endif
