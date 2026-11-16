#ifndef D9WG_RESET_H
#define D9WG_RESET_H
#include <stdint.h>

/* Failed Reset is retryable; inactive exclusive focus remains LOST until
 * activation. A successful Reset clears the pending recovery state. */
typedef struct D9WGResetState {
    int required;
    int lost;
} D9WGResetState;

static inline void d9wg_reset_begin(D9WGResetState* state)
{
    state->required = 1;
}

static inline void d9wg_reset_complete(D9WGResetState* state)
{
    state->required = 0;
}

static inline int32_t d9wg_reset_cooperative_result(const D9WGResetState* state)
{
    return state->lost ? (int32_t)UINT32_C(0x88760868)
        : state->required ? (int32_t)UINT32_C(0x88760869) : 0;
}

static inline int32_t d9wg_reset_present_result(const D9WGResetState* state)
{
    return state->required || state->lost ? (int32_t)UINT32_C(0x88760868) : 0;
}

/* DEFAULT resources must have no application references at Reset. Device
 * bindings are released by Reset itself. CPU pools survive even while mapped. */
static inline int d9wg_reset_resource_blocks(uint32_t pool, uint32_t references,
    uint32_t device_references, int locked)
{
    return pool == 0 && (locked || references > device_references);
}
#endif
