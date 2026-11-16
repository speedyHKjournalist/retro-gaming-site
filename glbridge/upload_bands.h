#ifndef V86WG_UPLOAD_BANDS_H
#define V86WG_UPLOAD_BANDS_H
#include <stdint.h>
/* Count complete units which fit after framing, without overflow. The GL
 * transport uses byte units; D3D texture transfers use complete block rows. */
static inline uint32_t v86wg_upload_band(uint32_t capacity, uint32_t overhead,
        uint32_t unit_bytes, uint32_t remaining)
{
    uint32_t count;
    if (!unit_bytes || capacity <= overhead) return 0;
    count = (capacity - overhead) / unit_bytes;
    return count < remaining ? count : remaining;
}
#endif
