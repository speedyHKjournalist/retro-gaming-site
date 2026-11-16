#ifndef D9WG_SWAP_EFFECT_H
#define D9WG_SWAP_EFFECT_H
#include <stdint.h>

/* D3D8: DISCARD=1, FLIP=2, COPY=3, COPY_VSYNC=4.
 * Non-Ex D3D9 only has the first three. A zero count means one buffer. */
static inline int d9wg_swap_effect_valid(uint32_t api, uint32_t effect,
    uint32_t count)
{
    if (api != 8 && api != 9)
        return 0;
    if (!effect || effect > (api == 8 ? 4u : 3u))
        return 0;
    if (effect == 3 || (api == 8 && effect == 4))
        return count <= 1;
    return 1;
}
#endif
