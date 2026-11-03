#ifndef D9WG_PRESENT_MULTISAMPLE_H
#define D9WG_PRESENT_MULTISAMPLE_H
#include <stdint.h>

/* DISCARD=1 is required for multisampling. The backend exposes one quality
 * level, indexed zero. D3D8 permits LOCKABLE_BACKBUFFER=1 with MSAA but still
 * refuses locks; D3D9 refuses that presentation parameter combination. */
static inline int d9wg_present_multisample_valid(uint32_t api,
    uint32_t effect, uint32_t samples, uint32_t quality, uint32_t flags)
{
    if (api != 8 && api != 9)
        return 0;
    if (samples && effect != 1)
        return 0;
    if (api == 9 && (quality || (samples && (flags & 1u))))
        return 0;
    return 1;
}
#endif
