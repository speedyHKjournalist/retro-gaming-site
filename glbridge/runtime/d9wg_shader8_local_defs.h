#ifndef D9WG_SHADER8_LOCAL_DEFS_H
#define D9WG_SHADER8_LOCAL_DEFS_H
#include <stdint.h>
#include <string.h>
/* Callers validate the original8 program and declaration first. Counts may
 * include END (core9 storage) or exclude it (legacy8 wire); both preserve the
 * original tail byte-for-byte. Local DEFs never write the API constant bank.
 * Byte copies avoid DWORD/uint32_t alias assumptions across SDK headers. */
static __inline uint32_t d9wg_shader8_build_local_defs(void *out,
        const void *original, uint32_t original_count,
        const void *const_data, const uint8_t *const_mask,
        uint32_t max_const_vectors)
{
    if (!original_count) return 0;
    uint32_t at = 1;
    memcpy(out, original, 4);
    for (uint32_t reg = 0; reg < max_const_vectors; ++reg) {
        if (!const_mask[reg]) continue;
        uint32_t opcode = 0x00000051u;
        uint32_t destination = 0xa00f0000u | reg;
        memcpy((uint8_t *)out + at++ * 4u, &opcode, 4);
        memcpy((uint8_t *)out + at++ * 4u, &destination, 4);
        memcpy((uint8_t *)out + at * 4u,
                (const uint8_t *)const_data + reg * 16u, 16);
        at += 4;
    }
    memcpy((uint8_t *)out + at * 4u, (const uint8_t *)original + 4,
            (original_count - 1u) * 4u);
    return at + original_count - 1u;
}
#endif
