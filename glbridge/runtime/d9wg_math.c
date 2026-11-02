#include "d9wg_math.h"

/* The legacy D3D7 SDK spells a matrix as sixteen individual float members;
 * newer SDKs also expose m[4][4]. Constant-size builtin copies access both
 * layouts without float-array aliasing assumptions or a C runtime call. */
static float matrix_value(const void *matrix, uint32_t row, uint32_t column)
{
    float value;
    __builtin_memcpy(&value, (const uint8_t *)matrix + (row * 4u + column) * sizeof(value), sizeof(value));
    return value;
}

float d9wg_sqrt_positive(float value)
{
    union { float number; uint32_t bits; } estimate;
    float guess;
    uint32_t iteration;
    if (!(value > 0.0f)) return 0.0f;
    estimate.number = value;
    estimate.bits = 0x1fbd1df5u + (estimate.bits >> 1);
    guess = estimate.number;
    for (iteration = 0; iteration < 4u; ++iteration)
        guess = 0.5f * (guess + value / guess);
    return guess;
}

void d9wg_transform_vector4(const void *matrix, const float *input, float *output)
{
    uint32_t column;
    for (column = 0; column < 4u; ++column) {
        output[column] = input[0] * matrix_value(matrix, 0, column)
                + input[1] * matrix_value(matrix, 1, column)
                + input[2] * matrix_value(matrix, 2, column)
                + input[3] * matrix_value(matrix, 3, column);
    }
}

void d9wg_multiply_matrix(const void *left, const void *right, void *output)
{
    uint32_t row, column;
    for (row = 0; row < 4u; ++row) {
        for (column = 0; column < 4u; ++column) {
            float value = matrix_value(left, row, 0) * matrix_value(right, 0, column)
                    + matrix_value(left, row, 1) * matrix_value(right, 1, column)
                    + matrix_value(left, row, 2) * matrix_value(right, 2, column)
                    + matrix_value(left, row, 3) * matrix_value(right, 3, column);
            __builtin_memcpy((uint8_t *)output + (row * 4u + column) * sizeof(value), &value, sizeof(value));
        }
    }
}

void d9wg_normal_matrix(const void *matrix, float *output)
{
    double a = matrix_value(matrix, 0, 0), b = matrix_value(matrix, 0, 1), c = matrix_value(matrix, 0, 2);
    double d = matrix_value(matrix, 1, 0), e = matrix_value(matrix, 1, 1), f = matrix_value(matrix, 1, 2);
    double g = matrix_value(matrix, 2, 0), h = matrix_value(matrix, 2, 1), i = matrix_value(matrix, 2, 2);
    double c00 = e*i - f*h, c01 = f*g - d*i, c02 = d*h - e*g;
    double determinant = a*c00 + b*c01 + c*c02;
    for (unsigned index = 0; index < 16; ++index) output[index] = 0.0f;
    output[15] = 1.0f;
    if (!determinant || !__builtin_isfinite(determinant)) {
        output[0] = output[5] = output[10] = 1.0f;return;
    }
    double scale = 1.0 / determinant;
    output[0] = (float)(c00*scale);output[1] = (float)(c01*scale);output[2] = (float)(c02*scale);
    output[4] = (float)((c*h-b*i)*scale);output[5] = (float)((a*i-c*g)*scale);output[6] = (float)((b*g-a*h)*scale);
    output[8] = (float)((b*f-c*e)*scale);output[9] = (float)((c*d-a*f)*scale);output[10] = (float)((a*e-b*d)*scale);
}

uint32_t d9wg_color_byte(float value)
{
    /* Define nonfinite color packing before the integer conversion. Native
     * GPU NaN rendering is not portable; the CPU must still avoid C UB. */
    if (!(value > 0.0f)) return 0;
    if (value >= 1.0f) return 255;
    return (uint32_t)(value * 255.0f + 0.5f);
}

uint32_t d9wg_pack_color(const float *rgba)
{
    return d9wg_color_byte(rgba[3]) << 24 | d9wg_color_byte(rgba[0]) << 16
            | d9wg_color_byte(rgba[1]) << 8 | d9wg_color_byte(rgba[2]);
}
