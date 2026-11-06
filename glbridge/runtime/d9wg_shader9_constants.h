#ifndef D9WG_SHADER9_CONSTANTS_H
#define D9WG_SHADER9_CONSTANTS_H
#include <stdint.h>

static __inline int d9wg_shader9_constant_register(uint32_t token,
        uint32_t major, uint32_t minor, int pixel, int software_vertex)
{
    uint32_t type = ((token >> 28) & 7u) | ((token >> 8) & 24u);
    uint32_t index = token & 0x7ffu;
    if (type == 2u || (type >= 11u && type <= 13u)) {
        if (type >= 11u) index += (type - 10u) * 2048u;
        uint32_t limit = pixel ? (major == 1u ? 8u : major == 2u ? 32u : 224u)
                : software_vertex ? 8192u : 256u;
        return index < limit;
    }
    if (type == 7u || type == 14u)
        return (!pixel || major >= 3u || (major == 2u && minor > 0u))
                && index < (!pixel && software_vertex ? 2048u : 16u);
    return 1;
}

/* Walk operands, not arbitrary DWORDs: literals and comment payloads may have
 * the same bits as register references. DCL's semantic word is not a register.
 * Native SWVP and MIXED devices admit the larger VS floating constant bank
 * even when the current mixed processing mode is hardware. */
static __inline int d9wg_shader9_constants_valid(const uint32_t *code,
        uint32_t count, int software_vertex)
{
    if (!code || count < 2u) return 0;
    uint32_t stage = code[0] >> 16, major = (code[0] >> 8) & 255u;
    if ((stage != 0xfffeu && stage != 0xffffu) || major < 1u || major > 3u) return 0;
    int pixel = stage == 0xffffu;
    uint32_t minor = code[0] & 255u;
    for (uint32_t i = 1u; i < count;) {
        uint32_t word = code[i++], op = word & 0xffffu;
        if (word & 0x80000000u) {
            if (!d9wg_shader9_constant_register(word, major, minor, pixel, software_vertex)) return 0;
            continue;
        }
        if (op == 0xffffu) return i == count;
        if (op == 0xfffeu) {
            uint32_t words = (word >> 16) & 0x7fffu;
            if (words > count - i) return 0;
            i += words;
            continue;
        }
        if (op == 81u || op == 48u || op == 47u) {
            uint32_t words = op == 47u ? 2u : 5u;
            if (words > count - i || !(code[i] & 0x80000000u)
                    || !d9wg_shader9_constant_register(code[i], major, minor, pixel, software_vertex)) return 0;
            i += words;
            continue;
        }
        if (major >= 2u) {
            uint32_t words = (word >> 24) & 15u;
            if (words > count - i) return 0;
            for (uint32_t j = op == 31u ? 1u : 0u; j < words; ++j)
                if ((code[i+j] & 0x80000000u)
                        && !d9wg_shader9_constant_register(code[i+j], major, minor, pixel, software_vertex)) return 0;
            i += words;
        }
    }
    return 0;
}
#endif
