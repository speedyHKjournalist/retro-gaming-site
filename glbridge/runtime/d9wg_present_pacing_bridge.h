#ifndef D9WG_PRESENT_PACING_BRIDGE_H
#define D9WG_PRESENT_PACING_BRIDGE_H
#include "d9wg_present_pacing.h"

static uint32_t d9wg_present_clock_now(D9WGPresentClock* clock)
{
    LARGE_INTEGER value;
    if (!clock->hz) {
        clock->shift = 0;
        if (!QueryPerformanceFrequency(&value) || value.QuadPart < 1000) {
            clock->shift = 0xff;
            clock->hz = 1000;
        } else {
            while ((value.QuadPart >> clock->shift) > (1 << 20))
                ++clock->shift;
            clock->hz = (uint32_t)(value.QuadPart >> clock->shift);
        }
    }
    if (clock->shift != 0xff && QueryPerformanceCounter(&value))
        return (uint32_t)(value.QuadPart >> clock->shift);
    /* A failed QPC sample changes both the counter and its units. */
    clock->shift = 0xff;
    clock->hz = 1000;
    return GetTickCount();
}

static void d9wg_present_read_clock(void* context, uint32_t* now, uint32_t* hz)
{
    D9WGPresentClock* clock = context;
    *now = d9wg_present_clock_now(clock);
    *hz = clock->hz;
}

static void d9wg_present_sleep(void* context, uint32_t milliseconds)
{
    (void)context;
    Sleep(milliseconds);
}

static BOOL d9wg_present_throttle_disabled(void)
{
    static LONG cached = -1;
    LONG value = InterlockedCompareExchange(&cached, -1, -1);
    if (value < 0) {
        char text[16];
        DWORD size = GetEnvironmentVariableA("D9WG_PRESENT_NO_THROTTLE", text, sizeof(text));
        value = size && size < sizeof(text) && text[0] != '0';
        LONG previous = InterlockedCompareExchange(&cached, value, -1);
        if (previous >= 0)
            value = previous;
    }
    return value != 0;
}

static void d9wg_throttle_present(D9WGPresentClock* clock, D9WGPresentPacing* pacing,
    uint32_t interval, uint32_t refresh)
{
    const D9WGPresentPacingOps ops = { clock, d9wg_present_read_clock, d9wg_present_sleep };
    uint32_t frames = d9wg_present_throttle_disabled() ? 0 : d9wg_present_frames(interval);
    d9wg_present_pace(pacing, frames, refresh, &ops);
    if (frames && !pacing->ready && clock->shift != 0xff) {
        clock->shift = 0xff;
        clock->hz = 1000;
        d9wg_present_pace(pacing, frames, refresh, &ops);
    }
}
#endif
