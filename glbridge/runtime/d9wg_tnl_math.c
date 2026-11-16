#include "d9wg_tnl_math.h"
#include <stdint.h>

void d9wg_tnl_normalize3(float *vector, float fallback_z)
{
    float scale = 0.0f;
    for (unsigned i = 0; i < 3; ++i) {
        float magnitude = vector[i] < 0.0f ? -vector[i] : vector[i];
        if (magnitude > scale) scale = magnitude;
    }
    if (scale > 0.0f) {
        float length;
        for (unsigned i = 0; i < 3; ++i) vector[i] /= scale;
        length = d9wg_tnl_sqrt(vector[0]*vector[0] + vector[1]*vector[1] + vector[2]*vector[2]);
        for (unsigned i = 0; i < 3; ++i) vector[i] /= length;
    } else { vector[0] = vector[1] = 0.0f;vector[2] = fallback_z; }
}

float d9wg_tnl_sqrt(float value)
{
    if (!(value > 0.0f)) return 0.0f;
#if defined(__i386__)
    float result;
    __asm__ volatile ("flds %[value]\n\tfsqrt\n\tfstps %[result]"
        : [result] "=m" (result) : [value] "m" (value) : "st", "memory");
    return result;
#else
    union { float value; uint32_t bits; } estimate;
    float guess, scale = 1.0f;
    estimate.value = value;
    if (estimate.bits == 0x7f800000u) return value;
    if (!(estimate.bits & 0x7f800000u)) {
        value *= 16777216.0f;scale = 0.000244140625f;estimate.value = value;
    }
    estimate.bits = 0x1fbd1df5u + (estimate.bits >> 1);guess = estimate.value;
    for (unsigned i = 0; i < 6; ++i) guess = 0.5f * (guess + value / guess);
    return guess * scale;
#endif
}

float d9wg_tnl_pow(float base, float power)
{
    union { float value; uint32_t bits; } number;
    if (power == 0.0f || base == 1.0f) return 1.0f;
    if (!(base > 0.0f) || !(power >= 0.0f)) return 0.0f;
    number.value = base;
    if (number.bits == 0x7f800000u) return base;
    number.value = power;
    if (number.bits == 0x7f800000u) return base < 1.0f ? 0.0f : power;
    number.value = base;
#if defined(__i386__)
    float result;
    /* y*log2(x), split into integer/fractional parts, then exp2 and scale.
     * FRNDINT's four rounding modes keep the F2XM1 input within [-1, 1]. */
    __asm__ volatile (
        "flds %[power]\n\tflds %[base]\n\tfyl2x\n\tfld %%st(0)\n\tfrndint\n\t"
        "fxch %%st(1)\n\tfsub %%st(1), %%st(0)\n\tf2xm1\n\tfld1\n\tfaddp\n\t"
        "fscale\n\tfstp %%st(1)\n\tfstps %[result]"
        : [result] "=m" (result) : [base] "m" (base), [power] "m" (power)
        : "st", "st(1)", "st(2)", "memory");
    return result;
#else
    /* Portable native-test path. Reduce the mantissa around one so log2
     * does not lose relative accuracy for the highlights nearest one. */
    int exponent, whole;
    double mantissa, z, term, series, y, fraction, sum, t;
    if ((number.bits & 0x7f800000u) == 0) {
        base *= 16777216.0f;number.value = base;exponent = -24;
    } else exponent = 0;
    exponent += (int)((number.bits >> 23) & 255u) - 127;
    number.bits = (number.bits & 0x7fffffu) | 0x3f800000u;
    mantissa = number.value;
    if (mantissa > 1.4142135623730951) { mantissa *= 0.5;++exponent; }
    z = (mantissa - 1.0) / (mantissa + 1.0);term = z;series = z;
    for (unsigned i = 3; i <= 23; i += 2) { term *= z * z;series += term / i; }
    y = (double)power * (exponent + series * 2.8853900817779268);
    if (y >= 128.0) { number.bits = 0x7f800000u;return number.value; }
    if (y <= -150.0) return 0.0f;
    whole = (int)y;if (y < whole) --whole;
    fraction = y - whole;t = fraction * 0.6931471805599453;term = sum = 1.0;
    for (unsigned i = 1; i <= 12; ++i) { term *= t / i;sum += term; }
    if (whole >= -126) {
        number.bits = (uint32_t)(whole + 127) << 23;
        return (float)(sum * number.value);
    }
    number.bits = (uint32_t)(whole + 253) << 23;
    return (float)(sum * number.value * 0x1p-126);
#endif
}

float d9wg_tnl_cos_half(float angle)
{
#if defined(__i386__)
    float half = angle * 0.5f, result;
    __asm__ volatile ("flds %[half]\n\tfcos\n\tfstps %[result]"
        : [result] "=m" (result) : [half] "m" (half) : "st", "memory");
    return result;
#else
    double x = (double)angle * 0.5, x2, sum, term;
    if (x > 0.7853981633974483) {
        /* Evaluate sin(pi/2-x) near the outer cone boundary, avoiding the
         * cancellation in cos(x) at pi/2. */
        x = 1.5707963267948966 - x;x2 = x * x;sum = term = x;
        for (unsigned i = 1; i <= 7; ++i) {
            term *= -x2 / ((2u * i) * (2u * i + 1u));sum += term;
        }
    } else {
        x2 = x * x;sum = term = 1.0;
        for (unsigned i = 1; i <= 8; ++i) {
            term *= -x2 / ((2u * i - 1u) * (2u * i));sum += term;
        }
    }
    return (float)sum;
#endif
}
