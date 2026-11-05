#ifndef D9WG_EXCLUSIVE_WINDOW_H
#define D9WG_EXCLUSIVE_WINDOW_H
#include <windows.h>
#include "d9wg_reset.h"
/* Fullscreen API8 tracks the focus window separately from the output window.
 * Subscriptions do not retain COM devices. Remove before freeing reset_state. */
typedef struct D9WGExclusiveWindow D9WGExclusiveWindow;
BOOL d9wg_exclusive_window_enter(D9WGExclusiveWindow **subscription,
    HWND focus, HWND output, UINT width, UINT height, D9WGResetState *state);
void d9wg_exclusive_window_leave(D9WGExclusiveWindow **subscription);
#endif
