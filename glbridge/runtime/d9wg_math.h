#ifndef D9WG_MATH_H
#define D9WG_MATH_H
#include <stdint.h>
/* Shared guest software-vertex mechanics. Matrices use the SDK's contiguous
 * 16-float row-major layout. Vector multiplication is row-vector * matrix.
 * Input/output regions must not overlap. No Windows types or C runtime. */
float d9wg_sqrt_positive(float value);
void d9wg_transform_vector4(const void *matrix, const float *input, float *output);
void d9wg_multiply_matrix(const void *left, const void *right, void *output);
/* Inverse transpose of the upper 3x3, with identity for singular matrices. */
void d9wg_normal_matrix(const void *matrix, float *output);
/* Existing finite-color conversion: clamp each channel and round to nearest. */
uint32_t d9wg_pack_color(const float *rgba);
#endif
