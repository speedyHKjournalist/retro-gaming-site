#include "d9wg_exclusive_window.h"
#define EXCLUSIVE_PROPERTY "D9WG.ExclusiveFocus.v1"

typedef struct D9WGFocusHook D9WGFocusHook;
struct D9WGExclusiveWindow {
    struct D9WGExclusiveWindow *next;
    D9WGFocusHook *hook;
    LONG refs;
    HWND output;
    UINT width, height, flags;
    D9WGResetState *state;
    BOOL mode_owned;
    unsigned changing;
};
struct D9WGFocusHook {
    HWND window;
    WNDPROC previous;
    BOOL unicode;
    LONG refs;
    unsigned suppress;
    D9WGExclusiveWindow *head;
};
static void release_hook(D9WGFocusHook *hook)
{
    if (!InterlockedDecrement(&hook->refs)) HeapFree(GetProcessHeap(), 0, hook);
}
static void release_subscription(D9WGExclusiveWindow *entry)
{
    if (!InterlockedDecrement(&entry->refs)) HeapFree(GetProcessHeap(), 0, entry);
}
static LRESULT forward(D9WGFocusHook *hook, HWND window, UINT msg, WPARAM wp, LPARAM lp)
{
    return hook->unicode ? CallWindowProcW(hook->previous, window, msg, wp, lp)
                         : CallWindowProcA(hook->previous, window, msg, wp, lp);
}
static LRESULT CALLBACK exclusive_proc(HWND window, UINT msg, WPARAM wp, LPARAM lp)
{
    D9WGFocusHook *hook = (D9WGFocusHook *)GetPropA(window, EXCLUSIVE_PROPERTY);
    if (!hook) return IsWindowUnicode(window) ? DefWindowProcW(window, msg, wp, lp) : DefWindowProcA(window, msg, wp, lp);
    InterlockedIncrement(&hook->refs);
    if (hook->suppress && msg != WM_DISPLAYCHANGE && msg != WM_NCDESTROY) {
        LRESULT result = hook->unicode ? DefWindowProcW(window, msg, wp, lp) : DefWindowProcA(window, msg, wp, lp);
        release_hook(hook);
        return result;
    }
    /* XP can activate another window of this thread while minimizing the
     * output. Restoring the focus window then sends WM_ACTIVATE without a new
     * WM_ACTIVATEAPP. Reconcile that lost-to-active edge only after Windows has
     * actually made our focus window foreground, and deliver the same ordered
     * output-position/application notification as the ordinary activation path. */
    if (msg == WM_ACTIVATE && LOWORD(wp) != WA_INACTIVE && !IsIconic(window)
            && GetForegroundWindow() == window) {
        for (D9WGExclusiveWindow *candidate = hook->head; candidate; candidate = candidate->next) {
            if (candidate->state && candidate->state->lost && !candidate->changing) {
                exclusive_proc(window, WM_ACTIVATEAPP, TRUE,
                    lp ? GetWindowThreadProcessId((HWND)lp, NULL) : 0);
                break;
            }
        }
    }
    D9WGExclusiveWindow *entry = hook->head;
    if (entry) InterlockedIncrement(&entry->refs);
    while (entry) {
        D9WGExclusiveWindow *next = entry->next;
        if (next) InterlockedIncrement(&next->refs);
        if (entry->state && !entry->changing) {
            if (msg == WM_DISPLAYCHANGE && ((UINT)LOWORD(lp) != entry->width || (UINT)HIWORD(lp) != entry->height)) {
                entry->state->required = 1;
            } else if (msg == WM_ACTIVATEAPP && (!wp || !IsIconic(window)
                    || (entry->flags & D9WG_EXCLUSIVE_D3D9))) {
                /* A thread-level activation while the focus window is still
                 * minimized is not fullscreen recovery. XP may send it during
                 * ShowWindow(SW_SHOWMINNOACTIVE); wait for the window restore. */
                ++entry->changing;
                ++hook->suppress;
                if (msg == WM_ACTIVATEAPP && !wp) {
                    /* During synchronous mode/size messages native reports
                     * NOTRESET; only the final ACTIVATEAPP sees DEVICELOST. */
                    entry->state->required = 1;
                    entry->state->lost = 0;
                    if (entry->mode_owned) {
                        entry->mode_owned = FALSE;
                        ChangeDisplaySettingsA(NULL, CDS_FULLSCREEN);
                    }
                    if (entry->state && !(entry->flags & D9WG_EXCLUSIVE_NO_WINDOW_CHANGES)
                            && IsWindowVisible(entry->output)) {
                        if (entry->flags & D9WG_EXCLUSIVE_D3D9)
                            SetWindowPos(entry->output, HWND_TOPMOST, 0, 0, 0, 0,
                                SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
                        if (entry->state) ShowWindow(entry->output,
                            (entry->flags & D9WG_EXCLUSIVE_D3D9) ? SW_SHOWMINIMIZED : SW_MINIMIZE);
                    }
                    if (entry->state) entry->state->lost = 1;
                } else {
                    entry->state->lost = 0;
                    if (entry->output != window && !(entry->flags & D9WG_EXCLUSIVE_NO_WINDOW_CHANGES)) {
                        if (entry->state) SetWindowPos(entry->output, NULL, 0, 0, entry->width, entry->height,
                            SWP_NOACTIVATE | SWP_NOZORDER);
                    }
                }
                --hook->suppress;
                --entry->changing;
            }
        }
        release_subscription(entry);
        entry = next;
    }
    /* Native handles restore before calling the application procedure. Other
     * system commands retain ordinary application-first dispatch. */
    if (msg == WM_SYSCOMMAND && (wp & 0xfff0u) == SC_RESTORE) {
        if (hook->unicode) DefWindowProcW(window, msg, wp, lp);
        else DefWindowProcA(window, msg, wp, lp);
    }
    LRESULT result = forward(hook, window, msg, wp, lp);
    if (msg == WM_NCDESTROY && GetPropA(window, EXCLUSIVE_PROPERTY) == (HANDLE)hook) {
        RemovePropA(window, EXCLUSIVE_PROPERTY);
        for (entry = hook->head; entry; entry = entry->next) {
            entry->hook = NULL;
            if (entry->state) {entry->state->required = 1;entry->state->lost = 1;}
        }
        hook->head = NULL;
        release_hook(hook); /* property reference */
    }
    release_hook(hook);
    return result;
}

static BOOL attach(D9WGExclusiveWindow *entry, HWND focus)
{
    if (!IsWindow(focus) || GetWindowThreadProcessId(focus, NULL) != GetCurrentThreadId()) return FALSE;
    D9WGFocusHook *hook = (D9WGFocusHook *)GetPropA(focus, EXCLUSIVE_PROPERTY);
    if (!hook) {
        hook = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*hook));
        if (!hook) return FALSE;
        hook->window = focus;hook->unicode = IsWindowUnicode(focus);hook->refs = 1;
        hook->previous = (WNDPROC)(LONG_PTR)(hook->unicode ? GetWindowLongPtrW(focus, GWLP_WNDPROC) : GetWindowLongPtrA(focus, GWLP_WNDPROC));
        if (!hook->previous || !SetPropA(focus, EXCLUSIVE_PROPERTY, (HANDLE)hook)) {HeapFree(GetProcessHeap(), 0, hook);return FALSE;}
        SetLastError(0);
        LONG_PTR previous = hook->unicode ? SetWindowLongPtrW(focus, GWLP_WNDPROC, (LONG_PTR)exclusive_proc)
                                        : SetWindowLongPtrA(focus, GWLP_WNDPROC, (LONG_PTR)exclusive_proc);
        if (!previous && GetLastError()) {
            RemovePropA(focus, EXCLUSIVE_PROPERTY);HeapFree(GetProcessHeap(), 0, hook);return FALSE;
        }
    }
    entry->hook = hook;entry->next = hook->head;hook->head = entry;
    return TRUE;
}

void d9wg_exclusive_window_leave(D9WGExclusiveWindow **subscription)
{
    D9WGExclusiveWindow *entry = *subscription;
    if (!entry) return;
    *subscription = NULL;
    D9WGFocusHook *hook = entry->hook;
    /* Detach the device state before any application callback can free it. */
    if (entry->state) entry->state->lost = 0;
    entry->state = NULL;
    if (hook) {
        InterlockedIncrement(&hook->refs);
        ++hook->suppress;
    }
    if (entry->mode_owned) {entry->mode_owned = FALSE;ChangeDisplaySettingsA(NULL, CDS_FULLSCREEN);}
    if (hook) {
        --hook->suppress;
        D9WGExclusiveWindow **link = &hook->head;
        while (*link && *link != entry) link = &(*link)->next;
        if (*link) *link = entry->next;
        entry->next = NULL;
        if (!hook->head && GetPropA(hook->window, EXCLUSIVE_PROPERTY) == (HANDLE)hook) {
            WNDPROC current = (WNDPROC)(LONG_PTR)(hook->unicode ? GetWindowLongPtrW(hook->window, GWLP_WNDPROC) : GetWindowLongPtrA(hook->window, GWLP_WNDPROC));
            if (current == exclusive_proc) {
                if (hook->unicode) SetWindowLongPtrW(hook->window, GWLP_WNDPROC, (LONG_PTR)hook->previous);
                else SetWindowLongPtrA(hook->window, GWLP_WNDPROC, (LONG_PTR)hook->previous);
            }
            RemovePropA(hook->window, EXCLUSIVE_PROPERTY);release_hook(hook);
        }
        release_hook(hook);
    }
    entry->hook = NULL;
    release_subscription(entry);
}

BOOL d9wg_exclusive_window_enter_ex(D9WGExclusiveWindow **subscription,
    HWND focus, HWND output, UINT width, UINT height, D9WGResetState *state, UINT flags)
{
    D9WGExclusiveWindow *entry = *subscription;
    BOOL initial = !entry;
    if (!entry) {
        entry = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*entry));
        if (!entry) return FALSE;
        entry->refs = 1;
        if (!attach(entry, focus)) {release_subscription(entry);return FALSE;}
        *subscription = entry;
    }
    if (!entry->hook) return FALSE;
    D9WGFocusHook *hook = entry->hook;
    InterlockedIncrement(&entry->refs);InterlockedIncrement(&hook->refs);
    entry->output = output;entry->width = width;entry->height = height;entry->state = state;entry->flags = flags;
    ++entry->changing;
    /* Focus and output windows are different roles. Raising the output steals
     * keyboard focus in applications that render through a secondary window. */
    if (initial && GetForegroundWindow() != focus) {
        SetWindowPos(focus, HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE);
        SetForegroundWindow(focus);SetFocus(focus);
    }
    ++hook->suppress;
    DEVMODEA mode;
    for (UINT i = 0; i < sizeof(mode); ++i) ((volatile BYTE *)&mode)[i] = 0;
    mode.dmSize = sizeof(mode);
    mode.dmPelsWidth = width;mode.dmPelsHeight = height;mode.dmBitsPerPel = 32;
    mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
    BOOL success = ChangeDisplaySettingsA(&mode, CDS_FULLSCREEN) == DISP_CHANGE_SUCCESSFUL;
    if (success && entry->state) {
        entry->mode_owned = TRUE;
        if (!(flags & D9WG_EXCLUSIVE_NO_WINDOW_CHANGES)) {
            /* XP keeps the application's window styles. A previously visible
             * D3D9 output retains its existing topmost style; a hidden output becomes topmost. */
            HWND after = (flags & D9WG_EXCLUSIVE_D3D9) && IsWindowVisible(output)
                    ? HWND_TOP : HWND_TOPMOST;
            SetWindowPos(output, after, 0, 0, width, height, SWP_NOACTIVATE);
        }
        if (entry->state) {
            if (!(flags & D9WG_EXCLUSIVE_NO_WINDOW_CHANGES)) ShowWindow(output, SW_SHOW);
            if (GetForegroundWindow() != focus) SetForegroundWindow(focus);
            SetFocus(focus);
        }
    }
    success = success && entry->state && entry->hook;
    --hook->suppress;--entry->changing;
    release_hook(hook);release_subscription(entry);
    return success;
}

BOOL d9wg_exclusive_window_enter(D9WGExclusiveWindow **subscription,
    HWND focus, HWND output, UINT width, UINT height, D9WGResetState *state)
{
    return d9wg_exclusive_window_enter_ex(subscription, focus, output, width, height, state, 0);
}
