#ifndef D9WG_DDRAW_COLOR_KEY_H
#define D9WG_DDRAW_COLOR_KEY_H
#include <stdint.h>

typedef struct D9WGDDColorKeyValues {
    uint32_t low, high;
} D9WGDDColorKeyValues;

/* Surface keys are single values. Explicit ranges are refused; otherwise
 * the caller's high value is ignored. Blt override ranges use another path. */
static inline int d9wg_ddraw_color_key_normalize(uint32_t low, uint32_t high,
        int explicit_range, D9WGDDColorKeyValues *out)
{
    if (explicit_range && low != high)
        return 0;
    if (out) {
        out->low = low;
        out->high = low;
    }
    return 1;
}
#endif
