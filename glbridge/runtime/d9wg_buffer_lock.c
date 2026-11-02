#include "d9wg_buffer_lock.h"

uint32_t d9wg_buffer_lock_flags(uint32_t usage, uint32_t pool, uint32_t requested)
{
    uint32_t streaming = requested & (D9WG_BUFFER_LOCK_DISCARD | D9WG_BUFFER_LOCK_NOOVERWRITE);
    /* A usable read-only access has neither a write-only resource nor a
     * streaming hint. Contradictory access hints fall back to a writable map. */
    if ((requested & D9WG_BUFFER_LOCK_READONLY)
        && !(usage & D9WG_BUFFER_USAGE_WRITEONLY) && !streaming)
        return D9WG_BUFFER_LOCK_READONLY;
    if (!(usage & D9WG_BUFFER_USAGE_DYNAMIC) || pool > 1u)
        return 0;
    /* Simultaneous streaming hints preserve existing storage (NOOVERWRITE). */
    if (streaming & D9WG_BUFFER_LOCK_NOOVERWRITE)
        return D9WG_BUFFER_LOCK_NOOVERWRITE;
    return streaming & D9WG_BUFFER_LOCK_DISCARD;
}
