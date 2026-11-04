#ifndef D9WG_PROCESS_LAYOUT8_H
#define D9WG_PROCESS_LAYOUT8_H
#include "d9wg_vertex.h"
#include "d9wg_fvf.h"
/* Exact legacy8 fixed-function layout subset. In particular bit14 is not
 * part of the8 position mask, and transformed positions cannot carry NORMAL. */
static __inline int d9wg_process_fvf8_subset(uint32_t fvf,
        D9WGProcessLayout *layout)
{
    uint32_t position = fvf & 0x000eu;
    if (position != 2u && position != 4u) return 0;
    if (position == 4u && (fvf & 0x10u)) return 0;
    return d9wg_process_fvf(D9WG_FVF_D3D8, fvf, layout);
}
#endif
