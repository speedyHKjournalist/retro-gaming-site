#ifndef D9WG_CLIP_H
#define D9WG_CLIP_H
#include <stdint.h>

typedef struct D9WGProcessViewport {
    float x, y, width, height, min_z, max_z;
} D9WGProcessViewport;
typedef struct D9WGClipVertex {
    float clip[4], screen[4], stored[4];
    uint32_t code, projected;
    uint32_t user_mask;
    float user_distance[6];
} D9WGClipVertex;
typedef struct D9WGClipStatus { uint32_t clip_union, clip_intersection; } D9WGClipStatus;
typedef struct D9WGClipLegacyStatus {
    uint32_t flags, status;
    float min_x, max_x, min_y, max_y, min_z, max_z;
} D9WGClipLegacyStatus;
void d9wg_clip_status_init(D9WGClipStatus *status);
void d9wg_clip_status_merge(D9WGClipStatus *status, uint32_t clip_union, uint32_t clip_intersection);
void d9wg_clip_legacy_init(D9WGClipLegacyStatus *status);
int d9wg_clip_legacy_set(D9WGClipLegacyStatus *status, const void *input);
void d9wg_clip_legacy_get(const D9WGClipLegacyStatus *status, void *output);

/* Public D3DCS bits, with z in [0,w]. Guard bands affect storage, not the
 * viewing-frustum outcode. Boundaries belong to the visible half space. */
uint32_t d9wg_clip_outcode(const float *clip);
void d9wg_clip_project(const float *clip, const D9WGProcessViewport *viewport,
        float *screen);
void d9wg_clip_unproject(const float *screen, const D9WGProcessViewport *viewport, float *clip);
/* guard_band is left, top, right, bottom in screen pixels. D3D7's CLIP
 * operation selects this behavior independently of its CLIPPING state.
 * Keep both positions: stored projection coordinates are public VB data;
 * neither these nor viewport-mapped screen z alone suffice for later clipping. */
void d9wg_clip_vertex(const float *clip, const D9WGProcessViewport *viewport,
        const float *guard_band, uint32_t clipping, D9WGClipVertex *vertex);
void d9wg_clip_user_vertex(D9WGClipVertex *vertex, const float *world,
        const float *planes, uint32_t mask);

/* Position occupies the first four floats; remaining attributes (including
 * depth before division) interpolate in homogeneous space. Scratch belongs
 * to the caller, keeping the CRT-free x86 implementation off the large stack. */
#define D9WG_CLIP_USER_FIRST 49u
#define D9WG_CLIP_MAX_FLOATS 56u
#define D9WG_CLIP_MAX_VERTICES 15u
#define D9WG_CLIP_MAX_DRAW_VERTICES ((D9WG_CLIP_MAX_VERTICES - 2u)*3u)
typedef struct D9WGClipPoint { float data[D9WG_CLIP_MAX_FLOATS]; } D9WGClipPoint;
typedef struct D9WGClipWork {
    D9WGClipPoint polygon[2][D9WG_CLIP_MAX_VERTICES];
} D9WGClipWork;
int d9wg_clip_triangle(const D9WGClipPoint *input, uint32_t floats, D9WGClipWork *work,
        const D9WGClipPoint **output, uint32_t *count);
int d9wg_clip_segment(const D9WGClipPoint *input, uint32_t floats,
        D9WGClipPoint *output, uint32_t *count);
/* User distances occupy USER_FIRST..USER_FIRST+5. They are computed before
 * projection in world space and interpolated with the other attributes. */
int d9wg_clip_triangle_planes(const D9WGClipPoint *input, uint32_t floats,
        uint32_t user_mask, D9WGClipWork *work, const D9WGClipPoint **output, uint32_t *count);
int d9wg_clip_segment_planes(const D9WGClipPoint *input, uint32_t floats,
        uint32_t user_mask, D9WGClipPoint *output, uint32_t *count);

/* Per-buffer processing records use byte positions, so different stream
 * offsets/strides cannot accidentally reuse one another's clipping state. */
#define D9WG_CLIP_INVALID 0xffffffffu
typedef struct D9WGClipBatch {
    struct D9WGClipBatch *next;
    uint32_t first, stride, count, live;
    D9WGProcessViewport viewport;
    D9WGClipVertex vertex[];
} D9WGClipBatch;
typedef struct D9WGClipBuffer { D9WGClipBatch *head; } D9WGClipBuffer;
void d9wg_clip_status_processed(D9WGClipStatus *status, const D9WGClipBatch *batch);
void d9wg_clip_legacy_processed(D9WGClipLegacyStatus *status, const D9WGClipBatch *batch,
        const void *positions, uint32_t stride, uint32_t count, uint32_t extents,
        const D9WGProcessViewport *viewport);
typedef void (*D9WGClipRelease)(void *context, void *allocation);
uint32_t d9wg_clip_batch_bytes(uint32_t count);
int d9wg_clip_batch_init(D9WGClipBatch *batch, uint32_t bytes, uint32_t first,
        uint32_t stride, uint32_t count, const D9WGProcessViewport *viewport);
void d9wg_clip_buffer_commit(D9WGClipBuffer *buffer, D9WGClipBatch *batch,
        uint32_t offset, uint32_t bytes, D9WGClipRelease release, void *context);
const D9WGClipVertex *d9wg_clip_buffer_lookup(D9WGClipBuffer *buffer, uint32_t position,
        const void *stored, const D9WGProcessViewport **viewport);
void d9wg_clip_buffer_free(D9WGClipBuffer *buffer, D9WGClipRelease release, void *context);
#endif
