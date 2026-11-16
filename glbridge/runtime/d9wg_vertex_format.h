#ifndef D9WG_VERTEX_FORMAT_H
#define D9WG_VERTEX_FORMAT_H
#include <stdint.h>
/* D3DDECLTYPE values are part of the wire declaration, independent of SDK. */
static __inline uint32_t d9wg_decl_bytes(uint32_t type)
{
    static const uint8_t sizes[17] = {4,8,12,16,4,4,4,8,4,4,8,4,8,4,4,4,8};
    return type < 17 ? sizes[type] : 0;
}
static __inline uint32_t d9wg_decl_components(uint32_t type)
{
    static const uint8_t counts[17] = {1,2,3,4,4,4,2,4,4,2,4,2,4,3,3,2,4};
    return type < 17 ? counts[type] : 0;
}
static __inline uint32_t d9wg_half_bits(uint16_t half)
{
    uint32_t sign = (uint32_t)(half & 0x8000u) << 16;
    uint32_t exponent = half >> 10 & 31u, fraction = half & 1023u;
    if (exponent == 31) return sign | 0x7f800000u | fraction << 13 | (fraction ? 0x00400000u : 0);
    if (exponent) return sign | (exponent + 112u) << 23 | fraction << 13;
    if (!fraction) return sign;
    exponent = 113;
    while (!(fraction & 1024u)) { fraction <<= 1;--exponent; }
    return sign | exponent << 23 | (fraction & 1023u) << 13;
}
/* The caller validates the complete element range first. Reads are unaligned
 * and expand missing components to (0,0,0,1), including integer/half formats. */
static __inline int d9wg_decl_read(uint32_t type, const void *input, float *value)
{
    uint32_t count = d9wg_decl_components(type);
    if (!count || !input || !value) return 0;
    const uint8_t *bytes = input;
    value[0] = value[1] = value[2] = 0;value[3] = 1;
    for (uint32_t i = 0; i < count; ++i) {
        if (type <= 3) __builtin_memcpy(value + i, bytes + i*4u, 4);
        else if (type == 4 || type == 5 || type == 8) {
            static const uint8_t order[4] = {2,1,0,3};
            value[i] = bytes[type == 4 ? order[i] : i];
            if (type != 5) value[i] /= 255.0f;
        } else if (type == 13 || type == 14) {
            uint32_t word;__builtin_memcpy(&word, bytes, 4);
            uint32_t bits = word >> (i*10u) & 1023u;
            if (type == 13) value[i] = (float)bits;
            else {
                int32_t component = (int32_t)(bits ^ 512u) - 512;
                value[i] = component < -511 ? -1.0f : (float)component / 511.0f;
            }
        } else if (type == 15 || type == 16) {
            uint16_t half;__builtin_memcpy(&half, bytes + i*2u, 2);
            uint32_t bits = d9wg_half_bits(half);__builtin_memcpy(value + i, &bits, 4);
        } else {
            uint16_t word;__builtin_memcpy(&word, bytes + i*2u, 2);
            if (type == 11 || type == 12) value[i] = (float)word / 65535.0f;
            else {
                int32_t component = (int32_t)(word ^ 32768u) - 32768;
                value[i] = (float)component;
                if (type == 9 || type == 10)
                    value[i] = component == -32768 ? -1.0f : value[i] / 32767.0f;
            }
        }
    }
    return 1;
}
#endif
