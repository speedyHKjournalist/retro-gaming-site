#ifndef D9WG_WINDOW_MODE_H
#define D9WG_WINDOW_MODE_H

/* Include after Win32 and d9wg_window_reset.h. Typed field bindings let both
 * frontends retain their private layouts while sharing every mode decision. */
typedef struct D9WGWindowModeBindings {
    HWND* styled_window;
    LONG* saved_window_style;
    LONG* saved_window_exstyle;
    BOOL* display_mode_changed;
    DWORD* last_foreground_claim;
} D9WGWindowModeBindings;

static inline void d9wg_window_mode_raise(HWND window)
{
    HWND foreground = GetForegroundWindow();
    DWORD foreground_thread = foreground ? GetWindowThreadProcessId(foreground, NULL) : 0;
    DWORD our_thread = GetCurrentThreadId();
    BOOL attached = FALSE;
    if (foreground_thread && foreground_thread != our_thread)
        attached = AttachThreadInput(our_thread, foreground_thread, TRUE);
    BringWindowToTop(window);
    SetForegroundWindow(window);
    SetActiveWindow(window);
    SetFocus(window);
    if (attached)
        AttachThreadInput(our_thread, foreground_thread, FALSE);
}

static inline void d9wg_window_mode_restore_style(D9WGWindowModeBindings* bindings)
{
    HWND window = *bindings->styled_window;
    if (!window)
        return;
    *bindings->styled_window = NULL;
    if (!IsWindow(window))
        return;
    SetWindowLongA(window, GWL_STYLE, *bindings->saved_window_style);
    SetWindowLongA(window, GWL_EXSTYLE, *bindings->saved_window_exstyle);
    SetWindowPos(window, NULL, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

static inline BOOL d9wg_window_mode_restore(D9WGWindowModeBindings* bindings)
{
    d9wg_window_mode_restore_style(bindings);
    if (!*bindings->display_mode_changed)
        return TRUE;
    if (ChangeDisplaySettingsA(NULL, 0) != DISP_CHANGE_SUCCESSFUL)
        return FALSE;
    *bindings->display_mode_changed = FALSE;
    return TRUE;
}

static inline BOOL d9wg_window_mode_enter(D9WGWindowModeBindings* bindings,
    HWND window, UINT width, UINT height)
{
    DEVMODEA mode;
    ZeroMemory(&mode, sizeof(mode));
    mode.dmSize = sizeof(mode);
    mode.dmPelsWidth = width;
    mode.dmPelsHeight = height;
    mode.dmBitsPerPel = 32;
    mode.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
    LONG result = ChangeDisplaySettingsA(&mode, CDS_FULLSCREEN);
    if (result != DISP_CHANGE_SUCCESSFUL) {
        mode.dmBitsPerPel = 16;
        result = ChangeDisplaySettingsA(&mode, CDS_FULLSCREEN);
    }
    if (result != DISP_CHANGE_SUCCESSFUL)
        return FALSE;
    *bindings->display_mode_changed = TRUE;
    if (!window || !IsWindow(window))
        return TRUE;
    if (IsIconic(window))
        ShowWindow(window, SW_RESTORE);
    if (*bindings->styled_window != window) {
        LONG style = GetWindowLongA(window, GWL_STYLE);
        LONG exstyle = GetWindowLongA(window, GWL_EXSTYLE);
        LONG bare_style = (style & ~(WS_CAPTION | WS_THICKFRAME | WS_BORDER | WS_DLGFRAME | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_SYSMENU))
            | WS_POPUP;
        LONG bare_exstyle = exstyle & ~(WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME | WS_EX_STATICEDGE);
        *bindings->styled_window = window;
        *bindings->saved_window_style = style;
        *bindings->saved_window_exstyle = exstyle;
        if (bare_style != style)
            SetWindowLongA(window, GWL_STYLE, bare_style);
        if (bare_exstyle != exstyle)
            SetWindowLongA(window, GWL_EXSTYLE, bare_exstyle);
    }
    SetWindowPos(window, HWND_TOP, 0, 0, (int)width, (int)height,
        SWP_SHOWWINDOW | SWP_FRAMECHANGED);
    d9wg_window_mode_raise(window);
    *bindings->last_foreground_claim = GetTickCount();
    return TRUE;
}

static inline BOOL d9wg_window_mode_apply(D9WGWindowModeBindings* bindings,
    BOOL windowed, HWND window, HWND focus_window, UINT width, UINT height)
{
    D9WGWindowResetScope scopes[3];
    HWND windows[3] = { *bindings->styled_window, window, focus_window };
    UINT prepared = 0;
    BOOL result = FALSE;
    if (windowed && !*bindings->styled_window && !*bindings->display_mode_changed)
        return TRUE;
    for (UINT i = 0; i < 3; ++i) {
        HWND candidate = windows[i];
        for (UINT j = 0; j < i; ++j)
            if (candidate == windows[j])
                candidate = NULL;
        if (!d9wg_window_reset_begin(&scopes[i], candidate))
            goto done;
        ++prepared;
    }
    if (windowed) {
        result = d9wg_window_mode_restore(bindings);
    } else {
        if (*bindings->styled_window && *bindings->styled_window != window)
            d9wg_window_mode_restore_style(bindings);
        result = d9wg_window_mode_enter(bindings, window, width, height);
    }
done:
    while (prepared)
        d9wg_window_reset_end(&scopes[--prepared]);
    return result;
}
#endif
