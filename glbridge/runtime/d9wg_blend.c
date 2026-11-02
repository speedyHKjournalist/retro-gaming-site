#include "d9wg_blend.h"
#include "d9wg_math.h"
#include <stddef.h>
static const uint8_t color_order[4] = {2,1,0,3};

uint32_t d9wg_blend_bytes(uint32_t mode, uint32_t indexed, uint32_t world_count)
{
    uint32_t matrices = mode == 256 ? 1u : mode + 1u;
    if ((!mode || mode > 3) && mode != 256) return 0;
    if (!world_count || world_count > 256 || matrices > world_count) return 0;
    return (uint32_t)offsetof(D9WGVertexBlend, matrix) +
            (indexed ? world_count : matrices)*sizeof(D9WGVertexBlendMatrix);
}
int d9wg_blend_init(D9WGVertexBlend *blend, uint32_t bytes, uint32_t mode,
        uint32_t indexed, const void *worlds, uint32_t world_count, uint32_t world_gap,
        const void *view, const void *projection)
{
    uint32_t required = d9wg_blend_bytes(mode, indexed, world_count);
    if (!blend || !required || bytes < required || !worlds || !view || !projection || world_gap > 2) return 0;
    blend->matrix_count = mode == 256 ? 1u : mode + 1u;blend->indexed = indexed != 0;
    blend->world_count = world_count;blend->world_gap = world_gap;blend->worlds = worlds;
    blend->palette_count = indexed ? world_count : blend->matrix_count;
    for (unsigned i = 0; i < 16; ++i) {
        __builtin_memcpy(&blend->view[i], (const uint8_t *)view + i*4u, 4u);
        __builtin_memcpy(&blend->projection[i], (const uint8_t *)projection + i*4u, 4u);
    }
    for (uint32_t i = 0; i < blend->palette_count; ++i) blend->matrix[i].ready = 0;
    return 1;
}
int d9wg_blend_layout(const D9WGVertexBlend *blend, const D9WGProcessLayout *layout)
{
    uint32_t weights = blend->matrix_count - 1u;
    uint32_t available = layout->blend_weight_type == 4 || layout->blend_weight_type == 8 ? 4u : layout->blend_weight_floats;
    if (weights && (layout->blend_weight < 0 || available < weights)) return 0;
    if (blend->indexed && (layout->blend_indices < 0 || (layout->blend_indices_type > 5 && layout->blend_indices_type != 8))) return 0;
    return 1;
}
int d9wg_blend_indices(const D9WGVertexBlend *blend, const D9WGProcessLayout *layout,
        const void *input, uint32_t *indices)
{
    for (unsigned i = 0; i < blend->matrix_count; ++i) {
        if (blend->indexed && layout->blend_indices_type < 4) {
            float index = i == 3 ? 1.0f : 0.0f;
            if (i <= layout->blend_indices_type)
                __builtin_memcpy(&index, (const uint8_t *)input + layout->blend_indices + i*4u, 4u);
            if (!(index >= 0.0f) || index >= (float)blend->world_count) return 0;
            indices[i] = (uint32_t)index;
        } else indices[i] = blend->indexed ? ((const uint8_t *)input)[layout->blend_indices +
                (layout->blend_indices_type == 4 ? color_order[i] : i)] : i;
        if (indices[i] >= blend->world_count) return 0;
    }
    return 1;
}
static const void *blend_world_matrix(const D9WGVertexBlend *blend, uint32_t index)
{ return blend->worlds + (index + (index ? blend->world_gap : 0u))*64u; }
static void prepare_matrix(D9WGVertexBlend *blend, uint32_t index)
{
    D9WGVertexBlendMatrix *matrix = &blend->matrix[index];
    const void *world = blend_world_matrix(blend, index);
    if (matrix->ready) return;
    d9wg_multiply_matrix(world, blend->view, matrix->world_view);
    d9wg_multiply_matrix(matrix->world_view, blend->projection, matrix->world_view_projection);
    d9wg_normal_matrix(matrix->world_view, matrix->normal);matrix->ready = 1;
}
void d9wg_blend_vertex_world(D9WGVertexBlend *blend, const D9WGProcessLayout *layout,
        const void *input, const float *position, float *eye, float *clip, float *eye_normal, float *world)
{
    uint32_t indices[4];float weights[4], normal[4] = {0,0,0,0}, remainder = 1.0f;
    /* The caller validates layout and every vertex index before any writes. */
    d9wg_blend_indices(blend, layout, input, indices);
    for (unsigned i = 0; i + 1u < blend->matrix_count; ++i) {
        if (layout->blend_weight_type == 4 || layout->blend_weight_type == 8)
            weights[i] = ((const uint8_t *)input)[layout->blend_weight +
                    (layout->blend_weight_type == 4 ? color_order[i] : i)] / 255.0f;
        else __builtin_memcpy(&weights[i], (const uint8_t *)input + layout->blend_weight + i*4u, 4u);
        remainder -= weights[i];
    }
    weights[blend->matrix_count - 1u] = remainder;
    if (layout->normal >= 0) for (unsigned i = 0; i < 3; ++i)
        __builtin_memcpy(&normal[i], (const uint8_t *)input + layout->normal + i*4u, 4u);
    for (unsigned i = 0; i < 4; ++i) { eye[i] = clip[i] = 0.0f;if (world) world[i] = 0.0f; }
    for (unsigned i = 0; i < 3; ++i) eye_normal[i] = 0.0f;
    for (unsigned bone = 0; bone < blend->matrix_count; ++bone) {
        float transformed_eye[4], transformed_clip[4], transformed_normal[4];
        prepare_matrix(blend, indices[bone]);
        const D9WGVertexBlendMatrix *matrix = &blend->matrix[indices[bone]];
        if (world) {
            float transformed[4];uint32_t index = indices[bone];
            const void *bone_world = blend_world_matrix(blend, index);
            d9wg_transform_vector4(bone_world, position, transformed);
            for (unsigned i = 0; i < 4; ++i) world[i] += transformed[i]*weights[bone];
        }
        d9wg_transform_vector4(matrix->world_view, position, transformed_eye);
        d9wg_transform_vector4(matrix->world_view_projection, position, transformed_clip);
        d9wg_transform_vector4(matrix->normal, normal, transformed_normal);
        for (unsigned i = 0; i < 4; ++i) {
            eye[i] += transformed_eye[i]*weights[bone];clip[i] += transformed_clip[i]*weights[bone];
        }
        for (unsigned i = 0; i < 3; ++i) eye_normal[i] += transformed_normal[i]*weights[bone];
    }
}

void d9wg_blend_vertex(D9WGVertexBlend *blend, const D9WGProcessLayout *layout,
        const void *input, const float *position, float *eye, float *clip, float *eye_normal)
{ d9wg_blend_vertex_world(blend, layout, input, position, eye, clip, eye_normal, 0); }
