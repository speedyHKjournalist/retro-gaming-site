#include "d9wg_lighting.h"
#include "d9wg_math.h"
#include "d9wg_tnl_math.h"

static float clamp01(float value)
{
    if (value < 0.0f) return 0.0f;
    if (value > 1.0f) return 1.0f;
    return value;
}
static float maxf(float a, float b) { return a > b ? a : b; }
static float dot3(const float *a, const float *b) { return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }

void d9wg_lighting_color(uint32_t argb, float *rgba)
{
    rgba[0] = ((argb >> 16) & 255u) / 255.0f;rgba[1] = ((argb >> 8) & 255u) / 255.0f;
    rgba[2] = (argb & 255u) / 255.0f;rgba[3] = (argb >> 24) / 255.0f;
}
void d9wg_lighting_init(D9WGTNLLighting *state, const D9WGSetMaterial *material,
        uint32_t ambient_argb, const D9WGTNLLightingFlags *flags)
{
    float ambient[4];
    state->material = *material;state->flags = *flags;state->light_count = 0;
    d9wg_lighting_color(ambient_argb, ambient);
    for (unsigned i = 0; i < 3; ++i) state->ambient[i] = ambient[i];
}
int d9wg_lighting_add(D9WGTNLLighting *state, const D9WGSetLight *light, const void *view_matrix)
{
    uint32_t at = 0;
    D9WGTNLPreparedLight *prepared;
    float position[4], direction[4], transformed[4];
    if (light->type < 1 || light->type > 4) return 0;
    while (at < state->light_count && state->lights[at].index < light->index) ++at;
    if (at == state->light_count || state->lights[at].index != light->index) {
        if (state->light_count >= D9WG_TNL_MAX_LIGHTS) return 0;
        for (uint32_t i = state->light_count; i > at; --i) state->lights[i] = state->lights[i - 1];
        ++state->light_count;
    }
    prepared = &state->lights[at];prepared->index = light->index;prepared->type = light->type;
    for (unsigned i = 0; i < 3; ++i) {
        prepared->diffuse[i] = light->diffuse[i];prepared->specular[i] = light->specular[i];prepared->ambient[i] = light->ambient[i];
        position[i] = light->position[i];direction[i] = light->direction[i];prepared->attenuation[i] = light->attenuation[i];
    }
    position[3] = 1.0f;direction[3] = 0.0f;
    d9wg_transform_vector4(view_matrix, position, transformed);
    for (unsigned i = 0; i < 3; ++i) prepared->position[i] = transformed[i];
    d9wg_transform_vector4(view_matrix, direction, transformed);
    for (unsigned i = 0; i < 3; ++i) prepared->direction[i] = transformed[i];
    d9wg_tnl_normalize3(prepared->direction, 1.0f);
    if (light->type == 4) d9wg_tnl_normalize3(prepared->position, 1.0f); // D3D7 parallel point.
    prepared->range = light->range;prepared->falloff = light->falloff;
    prepared->cos_theta = d9wg_tnl_cos_half(light->theta);prepared->cos_phi = d9wg_tnl_cos_half(light->phi);
    return 1;
}
static const float *material_source(const D9WGTNLLighting *state, uint32_t source,
        const float *material, const float *color0, const float *color1, uint32_t mask)
{
    if (state->flags.color_vertex) {
        if (source == 1 && (mask & 1u)) return color0;
        if (source == 2 && (mask & 2u)) return color1;
    }
    return material;
}
void d9wg_lighting_shade(const D9WGTNLLighting *state, const float *eye_position,
        const float *eye_normal, const float *color0, const float *color1,
        uint32_t color_mask, float *diffuse, float *specular)
{
    const float *mat_diffuse, *mat_specular, *mat_ambient, *mat_emissive;
    float total_diffuse[3] = {0,0,0}, total_specular[3] = {0,0,0}, ambient[3], eye[3];
    if (!state->flags.lighting) {
        for (unsigned i = 0; i < 4; ++i) {
            diffuse[i] = color_mask & 1u ? color0[i] : 1.0f;
            specular[i] = color_mask & 2u ? color1[i] : 0.0f;
        }
        return;
    }
    mat_diffuse = material_source(state, state->flags.diffuse_source, state->material.diffuse, color0, color1, color_mask);
    mat_ambient = material_source(state, state->flags.ambient_source, state->material.ambient, color0, color1, color_mask);
    mat_specular = material_source(state, state->flags.specular_source, state->material.specular, color0, color1, color_mask);
    mat_emissive = material_source(state, state->flags.emissive_source, state->material.emissive, color0, color1, color_mask);
    for (unsigned i = 0; i < 3; ++i) { ambient[i] = state->ambient[i];eye[i] = -eye_position[i]; }
    if (state->flags.local_viewer) d9wg_tnl_normalize3(eye, -1.0f);
    else { eye[0] = eye[1] = 0.0f;eye[2] = -1.0f; }
    for (uint32_t index = 0; index < state->light_count; ++index) {
        const D9WGTNLPreparedLight *light = &state->lights[index];
        float to_light[3], attenuation = 1.0f, n_dot_l;
        if (light->type == 3 || light->type == 4) {
            for (unsigned i = 0; i < 3; ++i) to_light[i] = light->type == 3 ? -light->direction[i] : light->position[i];
        } else {
            float distance, denominator;
            for (unsigned i = 0; i < 3; ++i) to_light[i] = light->position[i] - eye_position[i];
            distance = d9wg_tnl_sqrt(dot3(to_light, to_light));
            denominator = maxf(light->attenuation[0] + light->attenuation[1]*distance + light->attenuation[2]*distance*distance, 1e-6f);
            attenuation = 1.0f / denominator;
            if (distance > light->range) attenuation = 0.0f;
            if (distance > 0.0f) for (unsigned i = 0; i < 3; ++i) to_light[i] /= maxf(distance, 1e-6f);
            else { to_light[0] = to_light[1] = 0.0f;to_light[2] = 1.0f; }
            if (light->type == 2) {
                float rho = -dot3(light->direction, to_light), spot = 0.0f;
                if (rho > light->cos_theta) spot = 1.0f;
                else if (rho > light->cos_phi) {
                    float base = maxf((rho-light->cos_phi) / maxf(light->cos_theta-light->cos_phi, 1e-6f), 0.0f);
                    spot = d9wg_tnl_pow(base, maxf(light->falloff, 1e-4f));
                }
                attenuation *= spot;
            }
        }
        n_dot_l = maxf(dot3(eye_normal, to_light), 0.0f);
        for (unsigned i = 0; i < 3; ++i) {
            total_diffuse[i] += light->diffuse[i] * (n_dot_l * attenuation);
            ambient[i] += light->ambient[i] * attenuation;
        }
        if (state->flags.specular_enable && n_dot_l > 0.0f) {
            float half[3], length, highlight = 0.0f;
            for (unsigned i = 0; i < 3; ++i) half[i] = to_light[i] + eye[i];
            length = d9wg_tnl_sqrt(dot3(half, half));
            if (length > 0.0f) {
                for (unsigned i = 0; i < 3; ++i) half[i] /= length;
                highlight = d9wg_tnl_pow(maxf(dot3(eye_normal, half), 0.0f), maxf(state->material.power, 1e-4f));
            }
            for (unsigned i = 0; i < 3; ++i) total_specular[i] += light->specular[i] * (highlight * attenuation);
        }
    }
    for (unsigned i = 0; i < 3; ++i) {
        diffuse[i] = clamp01(ambient[i]*mat_ambient[i] + total_diffuse[i]*mat_diffuse[i] + mat_emissive[i]);
        specular[i] = clamp01(total_specular[i]*mat_specular[i]);
    }
    diffuse[3] = clamp01(mat_diffuse[3]);specular[3] = 0.0f;
}

void d9wg_lighting_geometry(D9WGTNLLighting *state, const void *world_view)
{
    d9wg_normal_matrix(world_view, state->normal_matrix);
}
void d9wg_lighting_normal(const D9WGTNLLighting *state, const float *normal, float *eye_normal)
{
    float input[4] = {0,0,0,0}, output[4];
    if (normal) for (unsigned i = 0; i < 3; ++i) input[i] = normal[i];
    d9wg_transform_vector4(state->normal_matrix, input, output);
    if (state->flags.normalize_normals) d9wg_tnl_normalize3(output, 0.0f);
    for (unsigned i = 0; i < 3; ++i) eye_normal[i] = output[i];
}
void d9wg_lighting_vertex_view_float(const D9WGTNLLighting *state, const float *eye_position,
        const float *eye_normal, const float *color0, const float *color1, uint32_t *diffuse, uint32_t *specular)
{
    const float default0[4] = {1,1,1,1}, default1[4] = {0,0,0,0};
    float out_diffuse[4], out_specular[4];
    d9wg_lighting_shade(state, eye_position, eye_normal,
            color0 ? color0 : default0, color1 ? color1 : default1,
            (color0 ? 1u : 0u) | (color1 ? 2u : 0u), out_diffuse, out_specular);
    /* Fog-alpha generation is separate. Preserve the source specular alpha
     * through lighting without first quantizing a floating point declaration. */
    out_specular[3] = color1 ? color1[3] : 0.0f;
    *diffuse = d9wg_pack_color(out_diffuse);*specular = d9wg_pack_color(out_specular);
}
void d9wg_lighting_vertex_view(const D9WGTNLLighting *state, const float *eye_position,
        const float *eye_normal, const void *color0, const void *color1, uint32_t *diffuse, uint32_t *specular)
{
    uint32_t source0 = 0xffffffffu, source1 = 0;
    float rgba0[4], rgba1[4];
    if (color0) __builtin_memcpy(&source0, color0, sizeof(source0));
    if (color1) __builtin_memcpy(&source1, color1, sizeof(source1));
    if (!state->flags.lighting) { *diffuse = source0;*specular = source1;return; }
    d9wg_lighting_color(source0, rgba0);d9wg_lighting_color(source1, rgba1);
    d9wg_lighting_vertex_view_float(state, eye_position, eye_normal,
            color0 ? rgba0 : 0, color1 ? rgba1 : 0, diffuse, specular);
}
static void lighting_vertex_geometry(const D9WGTNLLighting *state, const void *world_view,
        const float *position, const void *normal, float *eye_position, float *eye_normal)
{
    float source_normal[3] = {0,0,0};
    for (unsigned i = 0; i < 4; ++i) eye_position[i] = 0;
    for (unsigned i = 0; i < 3; ++i) eye_normal[i] = 0;
    if (state->flags.lighting) {
        if (normal) for (unsigned i = 0; i < 3; ++i)
            __builtin_memcpy(&source_normal[i], (const uint8_t *)normal + i*sizeof(float), sizeof(float));
        d9wg_transform_vector4(world_view, position, eye_position);
        d9wg_lighting_normal(state, normal ? source_normal : 0, eye_normal);
    }
}
void d9wg_lighting_vertex(const D9WGTNLLighting *state, const void *world_view, const float *position,
        const void *normal, const void *color0, const void *color1, uint32_t *diffuse, uint32_t *specular)
{
    float eye_position[4], eye_normal[3];
    lighting_vertex_geometry(state, world_view, position, normal, eye_position, eye_normal);
    d9wg_lighting_vertex_view(state, eye_position, eye_normal, color0, color1, diffuse, specular);
}
void d9wg_lighting_vertex_float(const D9WGTNLLighting *state, const void *world_view, const float *position,
        const void *normal, const float *color0, const float *color1, uint32_t *diffuse, uint32_t *specular)
{
    float eye_position[4], eye_normal[3];
    lighting_vertex_geometry(state, world_view, position, normal, eye_position, eye_normal);
    d9wg_lighting_vertex_view_float(state, eye_position, eye_normal, color0, color1, diffuse, specular);
}
