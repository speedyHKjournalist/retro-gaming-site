#ifndef D9WG_HOST_CAPS_H
#define D9WG_HOST_CAPS_H
#include <stdint.h>
#include "../d3d9proxy/d3d9_protocol.h"

/* Nonblocking decode of the host's HELLO reply. The old heartbeat occupies
 * only the preceding DWORD. Invalid/missing replies leave output untouched. */
static __inline int d9wg_host_caps_read(const void *response_region, uint32_t *features)
{
    if (!response_region || !features) return 0;
    const volatile D9WGHostCapabilities *caps = (const volatile D9WGHostCapabilities *)
            ((const uint8_t *)response_region + D9WG_HOST_CAPS_OFFSET);
    if (caps->magic != D9WG_HOST_CAPS_MAGIC) return 0;
    if (caps->version != D9WG_HOST_CAPS_VERSION) return 0;
    uint32_t bits = caps->feature_bits;
    if (caps->magic != D9WG_HOST_CAPS_MAGIC) return 0;
    *features = bits & D9WG_HOST_FEATURE_MASK;
    return 1;
}
#endif
