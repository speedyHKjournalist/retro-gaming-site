#ifndef D9WG_PRESENT_PACING_H
#define D9WG_PRESENT_PACING_H
#include <stdint.h>

#define D9WG_PRESENT_INTERVAL_MASK 0x80000001u

typedef struct D9WGPresentClock {
    uint32_t hz;
    uint8_t shift;
} D9WGPresentClock;

typedef struct D9WGPresentPacing {
    uint32_t deadline, hz, refresh, frames, fraction;
    int ready;
} D9WGPresentPacing;

typedef struct D9WGPresentPacingOps {
    void* context;
    void (*read_clock)(void*, uint32_t* now, uint32_t* hz);
    void (*sleep)(void*, uint32_t milliseconds);
} D9WGPresentPacingOps;

uint32_t d9wg_present_frames(uint32_t interval);
uint32_t d9wg_present_interval8(uint32_t requested, int windowed, uint32_t swap_effect);
int d9wg_present_interval_valid(uint32_t api, int windowed, uint32_t requested, uint32_t supported);
/* Prepare a candidate for a nonblocking submission. Busy leaves it unchanged;
 * the caller publishes the candidate only when the submission succeeds. */
int d9wg_present_try(D9WGPresentPacing*, uint32_t frames, uint32_t refresh,
    uint32_t now, uint32_t hz);
void d9wg_present_pacing_reset(D9WGPresentPacing*);
/* Call only after a successful presentation, without holding a transport lock.
 * Ops supply the clock's current units on each sample, including fallback.
 * Millisecond sleeps retain sub-millisecond debt in the next deadline. */
void d9wg_present_pace(D9WGPresentPacing*, uint32_t frames, uint32_t refresh,
    const D9WGPresentPacingOps*);
#endif
