#include "d9wg_present_pacing.h"

uint32_t d9wg_present_frames(uint32_t interval)
{
    switch (interval) {
    case 0:
    case 1:
        return 1;
    case 2:
        return 2;
    case 4:
        return 3;
    case 8:
        return 4;
    default:
        return 0;
    }
}

/* D3D8 windowed swap effects carry the synchronization choice. */
uint32_t d9wg_present_interval8(uint32_t requested, int windowed, uint32_t swap_effect)
{
    if (swap_effect == 4 && (windowed || requested == 0x80000000u))
        return 1;
    return windowed ? 0x80000000u : requested;
}

int d9wg_present_interval_valid(uint32_t api, int windowed, uint32_t requested, uint32_t supported)
{
    if (api != 8 && api != 9)
        return 0;
    if (api == 8 && windowed)
        return requested == 0;
    if (!requested)
        return 1;
    if (requested != 1 && requested != 2 && requested != 4 && requested != 8 && requested != 0x80000000u)
        return 0;
    if (windowed && requested != 1 && requested != 0x80000000u)
        return 0;
    return (supported & requested) == requested;
}

void d9wg_present_pacing_reset(D9WGPresentPacing* state)
{
    if (state) {
        state->ready = 0;
        state->fraction = 0;
    }
}

void d9wg_present_pace(D9WGPresentPacing* state, uint32_t frames,
    uint32_t refresh, const D9WGPresentPacingOps* ops)
{
    if (!state)
        return;
    if (!frames || frames > 4 || !ops || !ops->read_clock || !ops->sleep) {
        d9wg_present_pacing_reset(state);
        return;
    }
    if (refresh < 20 || refresh > 240)
        refresh = 60;
    uint32_t now, hz;
    ops->read_clock(ops->context, &now, &hz);
    uint32_t minimum_wait = 0;
    for (unsigned sample = 0; sample < 8; ++sample) {
        if (hz < 1000 || hz > (1u << 20) || hz * frames < refresh) {
            d9wg_present_pacing_reset(state);
            return;
        }
        uint32_t numerator = hz * frames;
        uint32_t maximum = (numerator + refresh - 1) / refresh;
        /* A previous Present may have returned less than a millisecond early.
         * Its next deadline includes that debt; it is not a clock jump. */
        uint32_t tolerance = (hz + 999u) / 1000u;
        uint32_t future_limit = maximum + tolerance;
        if (state->hz != hz || state->refresh != refresh || state->frames != frames)
            d9wg_present_pacing_reset(state);
        state->hz = hz;
        state->refresh = refresh;
        state->frames = frames;
        int32_t remaining = (int32_t)(state->deadline - now);
        if (state->ready && remaining > 0 && (uint32_t)remaining <= future_limit) {
            uint32_t wait = (uint32_t)remaining * 1000u / hz;
            if ((uint32_t)remaining > tolerance) {
                if (wait < minimum_wait)
                    wait = minimum_wait;
                uint32_t previous_now = now, previous_hz = hz;
                ops->sleep(ops->context, wait);
                ops->read_clock(ops->context, &now, &hz);
                /* Coarse timers can return early or make no visible progress.
                 * Increase a stalled short wait instead of spinning on it. */
                uint32_t ceiling = maximum * 2000u / previous_hz + 1;
                minimum_wait = hz == previous_hz && (uint32_t)(now - previous_now) < tolerance
                    ? (wait <= ceiling / 2 ? wait * 2 : ceiling)
                    : 0;
                continue;
            }
        }
        if (!state->ready || (int32_t)(now - state->deadline) >= (int32_t)maximum
            || remaining > (int32_t)future_limit) {
            state->deadline = now;
            state->fraction = 0;
        }
        uint32_t interval = numerator / refresh;
        state->fraction += numerator % refresh;
        if (state->fraction >= refresh) {
            ++interval;
            state->fraction -= refresh;
        }
        state->deadline += interval;
        state->ready = 1;
        return;
    }
    d9wg_present_pacing_reset(state);
}
