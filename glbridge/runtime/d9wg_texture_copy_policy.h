#ifndef D9WG_TEXTURE_COPY_POLICY_H
#define D9WG_TEXTURE_COPY_POLICY_H
#include <stdint.h>
/* D3D8/9 UpdateTexture accepts SYSTEMMEM -> DEFAULT only. Pool validity is
 * independent of mip matching and must precede shadow writes or uploads. */
static __inline int d9wg_texture_copy_pools(uint32_t source, uint32_t destination)
{
    return source == 2u && destination == 0u;
}
#endif
