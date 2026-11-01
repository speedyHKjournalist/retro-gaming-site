#include "d9wg_vertex.h"
#include "d9wg_fvf.h"
#include "d9wg_math.h"
#include "d9wg_tnl_math.h"
#include "d9wg_blend.h"

void d9wg_vertex_state_init(D9WGVertexState *state, const void *render_states)
{
    const uint8_t *bytes = render_states;
    state->texture_mask = 0;
    state->blend = 0;
    /* The public D3D7/8/9 render-state indices and DWORD storage agree. Fog
     * generation follows FOGVERTEXMODE; FOGENABLE controls later blending. */
    __builtin_memcpy(&state->fog_mode, bytes + 140u*4u, 4u);
    __builtin_memcpy(&state->range_fog, bytes + 48u*4u, 4u);
    __builtin_memcpy(&state->fog_start, bytes + 36u*4u, 4u);
    __builtin_memcpy(&state->fog_end, bytes + 37u*4u, 4u);
    __builtin_memcpy(&state->fog_density, bytes + 38u*4u, 4u);
}
int d9wg_vertex_texture_state(D9WGVertexState *state, uint32_t stage,
        uint32_t coordinate_index, uint32_t flags, const void *matrix)
{
    if (stage >= 8 || (coordinate_index & 0xffffu) >= 8 ||
            (coordinate_index >> 16) > 4 || (flags & 0xffu) > 4) return 0;
    state->texture_mask |= 1u << stage;
    state->texture[stage].coordinate_index = coordinate_index;
    state->texture[stage].flags = flags;
    for (unsigned i = 0; i < 16; ++i)
        __builtin_memcpy(&state->texture[stage].matrix[i], (const uint8_t *)matrix + i*4u, 4u);
    return 1;
}
int d9wg_vertex_textures_init(D9WGVertexState *state, const void *stage_states,
        uint32_t stage_bytes, const void *texture_matrices)
{
    if (stage_bytes < 25u*4u) return 0;
    for (unsigned i = 0; i < 8; ++i) {
        uint32_t coordinate_index, flags;
        const uint8_t *stage = (const uint8_t *)stage_states + i*stage_bytes;
        __builtin_memcpy(&coordinate_index, stage + 11u*4u, 4u);
        __builtin_memcpy(&flags, stage + 24u*4u, 4u);
        if (!d9wg_vertex_texture_state(state, i, coordinate_index, flags,
                (const uint8_t *)texture_matrices + i*64u)) return 0;
    }
    return 1;
}
float d9wg_vertex_fog(const D9WGVertexState *state, const float *eye_position)
{
    float distance = eye_position[2] < 0.0f ? -eye_position[2] : eye_position[2];
    float factor;
    if (!state->fog_mode || state->fog_mode > 3) return 1.0f;
    if (state->range_fog) distance = d9wg_tnl_sqrt(eye_position[0]*eye_position[0] +
            eye_position[1]*eye_position[1] + eye_position[2]*eye_position[2]);
    if (state->fog_mode == 3) {
        float denominator = state->fog_end - state->fog_start;
        factor = denominator != 0.0f ? (state->fog_end - distance) / denominator :
                distance < state->fog_end ? 1.0f : 0.0f;
    } else {
        float exponent = state->fog_density*distance;
        if (state->fog_mode == 2) exponent *= exponent;
        /* Only a clamped [0,1] factor is observable. The existing CRT-free
         * power primitive computes exp(-x) with a float e^-1 base. */
        factor = exponent > 0.0f ? d9wg_tnl_pow(0.36787944117144233f, exponent) : 1.0f;
    }
    if (factor < 0.0f) return 0.0f;
    if (factor > 1.0f) return 1.0f;
    return factor;
}

int d9wg_process_layout(const D9WGVertexElement *elements, uint32_t count, D9WGProcessLayout *layout)
{
    layout->position = layout->normal = layout->diffuse = layout->specular = layout->psize = -1;
    layout->blend_weight = layout->blend_indices = -1;
    layout->blend_weight_floats = layout->blend_weight_type = layout->blend_indices_type = 0;
    layout->position_transformed = layout->position_floats = layout->stride = layout->texcoord_count = layout->has_blend = 0;
    for (unsigned i = 0; i < 8; ++i) { layout->texcoord[i] = -1;layout->texcoord_floats[i] = 0; }
    if (!elements || !count) return 0;
    static const uint8_t type_bytes[17] = {4,8,12,16,4,4,4,8,4,4,8,4,8,4,4,4,8};
    for (uint32_t i = 0; i < count; ++i) {
        const D9WGVertexElement *e = &elements[i];
        uint32_t floats = e->type <= 3 ? e->type + 1u : 0u;
        uint32_t bytes = e->type < 17u ? type_bytes[e->type] : 0u;
        uint32_t end = e->offset + bytes;
        if (e->stream || e->method || !bytes) return 0;
        if (end > layout->stride) layout->stride = end;
        switch (e->usage) {
        case 0: case 9: /* POSITION / POSITIONT */
            if (e->usage_index) break;
            if (layout->position >= 0 || floats < 3 || (e->usage == 9 && floats != 4)) return 0;
            layout->position = e->offset;layout->position_floats = floats;
            layout->position_transformed = e->usage == 9;break;
        case 3: /* NORMAL */
            if (e->usage_index) break;
            if (floats < 3 || layout->normal >= 0) return 0;
            layout->normal = e->offset;break;
        case 4: /* PSIZE */
            if (e->usage_index || floats != 1 || layout->psize >= 0) return 0;
            layout->psize = e->offset;break;
        case 10: /* COLOR */
            if (e->type != 4) return 0;
            if (e->usage_index == 0) { if (layout->diffuse >= 0) return 0;layout->diffuse = e->offset; }
            if (e->usage_index == 1) { if (layout->specular >= 0) return 0;layout->specular = e->offset; }
            break;
        case 5: /* TEXCOORD */
            if (e->usage_index >= 8 || !floats || layout->texcoord[e->usage_index] >= 0) return 0;
            layout->texcoord[e->usage_index] = e->offset;layout->texcoord_floats[e->usage_index] = floats;
            if (e->usage_index + 1u > layout->texcoord_count) layout->texcoord_count = e->usage_index + 1u;
            break;
        case 1:
            layout->has_blend = 1;
            if (e->usage_index) break;
            if (layout->blend_weight >= 0) return 0;
            layout->blend_weight = e->offset;layout->blend_weight_floats = floats;layout->blend_weight_type = e->type;break;
        case 2:
            layout->has_blend = 1;
            if (e->usage_index) break;
            if (layout->blend_indices >= 0) return 0;
            layout->blend_indices = e->offset;layout->blend_indices_type = e->type;break;
        default: break;
        }
    }
    return layout->position >= 0;
}
int d9wg_process_fvf(uint32_t api, uint32_t fvf, D9WGProcessLayout *layout)
{
    D9WGVertexElement elements[18];uint32_t count;
    D9WGFVFLayout fvf_layout;
    if (!d9wg_fvf_decode(api, fvf, elements, 18, &count, &fvf_layout) ||
            !d9wg_process_layout(elements, count, layout)) return 0;
    /* Reserved D3D7 data still contributes to the actual FVF stride. */
    layout->stride = fvf_layout.stride;return 1;
}
int d9wg_process_range(uint32_t length, uint32_t base, uint32_t stride,
        uint32_t extent, uint32_t start, uint32_t count, uint32_t *first)
{
    uint32_t available;
    if (!count || !stride || !extent || extent > stride || base > length) return 0;
    available = length - base;
    if (start > available / stride) return 0;
    *first = base + start*stride;available = length - *first;
    if (extent > available || count - 1u > (available - extent) / stride) return 0;
    return 1;
}
static float read_float(const void *input)
{ float value;__builtin_memcpy(&value, input, sizeof(value));return value; }
static void write_float(void *output, float value)
{ __builtin_memcpy(output, &value, sizeof(value)); }
static void copy_bytes(void *destination, const void *source, uint32_t bytes)
{ for (uint32_t i = 0; i < bytes; ++i) ((uint8_t *)destination)[i] = ((const uint8_t *)source)[i]; }
int d9wg_process_vertices(const D9WGProcessLayout *s,
        const void *source, uint32_t source_length, uint32_t source_base,
        uint32_t source_stride, uint32_t source_start,
        const D9WGProcessLayout *d, void *output, uint32_t output_length,
        uint32_t output_start, uint32_t count, const void *world_view,
        const void *world_view_projection, const D9WGProcessViewport *viewport,
        const D9WGTNLLighting *lighting, uint32_t copy_data, const D9WGVertexState *vertex_state)
{
    uint32_t first, destination;
    if (!source || !output || s->position_transformed || !d->position_transformed ||
            !d9wg_process_range(source_length, source_base, source_stride, s->stride, source_start, count, &first) ||
            !d9wg_process_range(output_length, 0, d->stride, d->stride, output_start, count, &destination)) return 0;
    D9WGVertexBlend *blend = vertex_state ? vertex_state->blend : 0;
    if (blend) {
        if (!d9wg_blend_layout(blend, s)) return 0;
        for (uint32_t vertex = 0; vertex < count; ++vertex) {
            uint32_t indices[4];
            if (!d9wg_blend_indices(blend, s, (const uint8_t *)source + first + vertex*source_stride, indices)) return 0;
        }
    }
    for (uint32_t vertex = 0; vertex < count; ++vertex) {
        const uint8_t *in = (const uint8_t *)source + first + vertex*source_stride;
        uint8_t *out = (uint8_t *)output + destination + vertex*d->stride;
        float position[4] = {0,0,0,1}, clip[4], screen[4], rhw;
        float blended_eye[4], blended_normal[3];
        for (unsigned i = 0; i < s->position_floats; ++i) position[i] = read_float(in + s->position + i*4u);
        if (blend) {
            d9wg_blend_vertex(blend, s, in, position, blended_eye, clip, blended_normal);
            if (lighting->flags.normalize_normals) d9wg_tnl_normalize3(blended_normal, 0.0f);
        } else d9wg_transform_vector4(world_view_projection, position, clip);
        rhw = clip[3] != 0.0f ? 1.0f / clip[3] : 1.0f;
        screen[0] = (clip[0]*rhw*0.5f + 0.5f)*viewport->width + viewport->x;
        screen[1] = (0.5f - clip[1]*rhw*0.5f)*viewport->height + viewport->y;
        screen[2] = viewport->min_z + clip[2]*rhw*(viewport->max_z - viewport->min_z);screen[3] = rhw;
        for (unsigned i = 0; i < 4; ++i) write_float(out + d->position + i*4u, screen[i]);
        /* DONOTCOPYDATA preserves only unaffected attributes. Lighting still
         * computes diffuse/specular even when passthrough copying is disabled. */
        if ((copy_data || lighting->flags.lighting) && (d->diffuse >= 0 || d->specular >= 0)) {
            uint32_t diffuse, specular;
            if (blend) d9wg_lighting_vertex_view(lighting, blended_eye, blended_normal,
                    s->diffuse >= 0 ? in + s->diffuse : 0,
                    s->specular >= 0 ? in + s->specular : 0, &diffuse, &specular);
            else d9wg_lighting_vertex(lighting, world_view, position,
                    s->normal >= 0 ? in + s->normal : 0,
                    s->diffuse >= 0 ? in + s->diffuse : 0,
                    s->specular >= 0 ? in + s->specular : 0, &diffuse, &specular);
            if (d->diffuse >= 0) __builtin_memcpy(out + d->diffuse, &diffuse, 4u);
            if (d->specular >= 0) __builtin_memcpy(out + d->specular, &specular, 4u);
        }
        if (vertex_state && vertex_state->fog_mode && d->specular >= 0) {
            float eye[4];uint32_t specular;
            if (blend) for (unsigned i = 0; i < 4; ++i) eye[i] = blended_eye[i];
            else d9wg_transform_vector4(world_view, position, eye);
            __builtin_memcpy(&specular, out + d->specular, 4u);
            specular = (specular & 0x00ffffffu) |
                    (uint32_t)(d9wg_vertex_fog(vertex_state, eye)*255.0f + 0.5f) << 24;
            __builtin_memcpy(out + d->specular, &specular, 4u);
        }
        if (copy_data && s->normal >= 0 && d->normal >= 0) copy_bytes(out + d->normal, in + s->normal, 12u);
        if (copy_data && s->psize >= 0 && d->psize >= 0) copy_bytes(out + d->psize, in + s->psize, 4u);
        for (unsigned i = 0; i < 8; ++i) {
            uint32_t configured = vertex_state && (vertex_state->texture_mask & (1u << i));
            uint32_t index = configured ? vertex_state->texture[i].coordinate_index : i;
            uint32_t flags = configured ? vertex_state->texture[i].flags : 0;
            uint32_t generated = index >> 16, transformed = flags & 0xffu, source_set = index & 0xffffu;
            uint32_t floats = generated ? generated == 4 ? 2u : 3u : s->texcoord_floats[source_set];
            float coordinate[4] = {0,0,0,1}, converted[4];
            if (d->texcoord[i] < 0 || !floats || (!generated && s->texcoord[source_set] < 0)) continue;
            if (!copy_data && !generated && !transformed) continue;
            if (!generated) {
                for (unsigned component = 0; component < floats; ++component)
                    coordinate[component] = read_float(in + s->texcoord[source_set] + component*4u);
            } else {
                float eye[4], normal[3] = {0,0,0}, eye_normal[3];
                if (blend) {
                    for (unsigned component = 0; component < 4; ++component) eye[component] = blended_eye[component];
                    for (unsigned component = 0; component < 3; ++component) eye_normal[component] = blended_normal[component];
                } else {
                    d9wg_transform_vector4(world_view, position, eye);
                    if (s->normal >= 0) for (unsigned component = 0; component < 3; ++component)
                        normal[component] = read_float(in + s->normal + component*4u);
                    d9wg_lighting_normal(lighting, normal, eye_normal);
                }
                for (unsigned component = 0; component < 3; ++component)
                    coordinate[component] = generated == 1 ? eye_normal[component] : eye[component];
                if (generated >= 3) {
                    float dot;
                    d9wg_tnl_normalize3(coordinate, 0.0f);d9wg_tnl_normalize3(eye_normal, 0.0f);
                    dot = coordinate[0]*eye_normal[0] + coordinate[1]*eye_normal[1] + coordinate[2]*eye_normal[2];
                    for (unsigned component = 0; component < 3; ++component)
                        coordinate[component] -= 2.0f*dot*eye_normal[component];
                    if (generated == 4) {
                        float z = coordinate[2] + 1.0f;
                        float divisor = 2.0f*d9wg_tnl_sqrt(coordinate[0]*coordinate[0] + coordinate[1]*coordinate[1] + z*z);
                        if (divisor < 1e-6f) divisor = 1e-6f;
                        coordinate[0] = coordinate[0] / divisor + .5f;coordinate[1] = coordinate[1] / divisor + .5f;
                    }
                }
            }
            if (transformed) {
                if (floats < 4) {
                    coordinate[floats] = 1.0f;
                    for (unsigned component = floats + 1u; component < 4; ++component) coordinate[component] = 0.0f;
                }
                d9wg_transform_vector4(vertex_state->texture[i].matrix, coordinate, converted);
                for (unsigned component = 0; component < 4; ++component) coordinate[component] = converted[component];
                floats = transformed;
            }
            uint32_t written = floats < d->texcoord_floats[i] ? floats : d->texcoord_floats[i];
            /* PROJECTED is consumed by the rasterizer, after interpolation;
             * ProcessVertices stores the undivided coordinate including q. */
            for (unsigned component = 0; component < written; ++component)
                write_float(out + d->texcoord[i] + component*4u, coordinate[component]);
        }
    }
    return 1;
}
