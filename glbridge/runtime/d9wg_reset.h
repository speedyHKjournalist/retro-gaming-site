#ifndef D9WG_RESET_H
#define D9WG_RESET_H
#include <stdint.h>

/* DEFAULT resources must have no application references at Reset. Device
 * bindings are released by Reset itself. CPU pools survive even while mapped. */
static inline int d9wg_reset_resource_blocks(uint32_t pool, uint32_t references,
    uint32_t device_references, int locked)
{
    return pool == 0 && (locked || references > device_references);
}
#endif
