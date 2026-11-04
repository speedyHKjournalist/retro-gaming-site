#ifndef D9WG_SURFACE_LOCK_H
#define D9WG_SURFACE_LOCK_H
#include <stdint.h>
#define D9WG_SURFACE_LOCK_READONLY 0x10u
/* SDK-independent linear-surface lock planning. Publish nothing on failure;
 * callers perform GPU readback before publishing their pointer/lock state. */
typedef struct D9WGSurfaceLockRect { int32_t left, top, right, bottom; } D9WGSurfaceLockRect;
typedef struct D9WGSurfaceLockPlan {
    D9WGSurfaceLockRect area;
    uint32_t offset, readonly;
} D9WGSurfaceLockPlan;
static inline int d9wg_surface_lock_plan(uint32_t width, uint32_t height,
        uint32_t pitch, uint32_t pixel_bytes, uint32_t multisample,
        int lockable, int locked, const D9WGSurfaceLockRect *rect,
        uint32_t flags, D9WGSurfaceLockPlan *out)
{
    D9WGSurfaceLockPlan plan;
    if (!out || !lockable || locked || multisample || !width || !height
            || width > INT32_MAX || height > INT32_MAX || !pixel_bytes
            || (uint64_t)width * pixel_bytes > pitch
            || (uint64_t)pitch * height > UINT32_MAX)
        return 0;
    plan.area = rect ? *rect : (D9WGSurfaceLockRect){0, 0, (int32_t)width, (int32_t)height};
    if (plan.area.left < 0 || plan.area.top < 0
            || plan.area.right <= plan.area.left || plan.area.bottom <= plan.area.top
            || (uint32_t)plan.area.right > width || (uint32_t)plan.area.bottom > height)
        return 0;
    plan.offset = (uint32_t)plan.area.top * pitch + (uint32_t)plan.area.left * pixel_bytes;
    plan.readonly = !!(flags & D9WG_SURFACE_LOCK_READONLY);
    *out = plan;
    return 1;
}
#endif
