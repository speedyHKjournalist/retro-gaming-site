#ifndef D9WG_CLIP_H
#define D9WG_CLIP_H
#include <stdint.h>

typedef struct D9WGProcessViewport {
    float x, y, width, height, min_z, max_z;
} D9WGProcessViewport;
typedef struct D9WGClipVertex {
    float clip[4], screen[4], stored[4];
    uint32_t code, projected;
} D9WGClipVertex;

/* Public D3DCS bits, with z in [0,w]. Guard bands affect storage, not the
 * viewing-frustum outcode. Boundaries belong to the visible half space. */
uint32_t d9wg_clip_outcode(const float *clip);
void d9wg_clip_project(const float *clip, const D9WGProcessViewport *viewport,
        float *screen);
/* guard_band is left, top, right, bottom in screen pixels. D3D7's CLIP
 * operation selects this behavior independently of its CLIPPING state.
 * Keep both positions: stored projection coordinates are public VB data;
 * neither these nor viewport-mapped screen z alone suffice for later clipping. */
void d9wg_clip_vertex(const float *clip, const D9WGProcessViewport *viewport,
        const float *guard_band, uint32_t clipping, D9WGClipVertex *vertex);
#endif
