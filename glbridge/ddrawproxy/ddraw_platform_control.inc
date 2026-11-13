/* Native XP owns window messages and physical display ownership. Pixel data
 * and all drawing remain in the proxy. Never load the application DLL again. */
static HMODULE g_platform_ddraw;
static BOOL g_platform_ddraw_tried;

static void ddraw_platform_attach(DDrawObject *object)
{
    HMODULE module;
    IUnknown *native = NULL;
    HRESULT hr = E_FAIL;
    if (!object || !object->initialized || object->platform_ddraw) return;
    EnterCriticalSection(&g_object_lock);
    if (!g_platform_ddraw_tried) {
        char path[MAX_PATH];
        UINT length = GetSystemDirectoryA(path, MAX_PATH);
        g_platform_ddraw_tried = TRUE;
        if (length && length + sizeof("\\ddraw.dll") <= MAX_PATH) {
            lstrcatA(path, "\\ddraw.dll");
            module = LoadLibraryA(path);
            if (module == g_instance) {
                FreeLibrary(module);
                module = NULL;
            }
            g_platform_ddraw = module;
        }
    }
    module = g_platform_ddraw;
    LeaveCriticalSection(&g_object_lock);
    if (!module) return;
    InterlockedIncrement(&object->lifetime_ref);
    if (object->extended_factory) {
        HRESULT (WINAPI *create)(GUID *, void **, REFIID, IUnknown *) =
            (void *)GetProcAddress(module, "DirectDrawCreateEx");
        if (create) hr = create(NULL, (void **)&native, &IID_IDirectDraw7, NULL);
    } else {
        HRESULT (WINAPI *create)(GUID *, IDirectDraw **, IUnknown *) =
            (void *)GetProcAddress(module, "DirectDrawCreate");
        if (create) hr = create(NULL, (IDirectDraw **)&native, NULL);
    }
    if (SUCCEEDED(hr)) object->platform_ddraw = native;
    else if (native) IUnknown_Release(native);
    ddraw_lifetime_release(object);
}

static BOOL ddraw_platform_release(DDrawObject *object)
{
    IUnknown *native = object->platform_ddraw;
    object->platform_ddraw = NULL;
    if (!native) return FALSE;
    /* Legacy surfaces do not pin the public draw interface. Its control
     * ownership ends at public zero even while proxy storage remains private. */
    object->guest_mode_changed = FALSE;
    object->primary = NULL;
    IUnknown_Release(native);
    return TRUE;
}

static BOOL ddraw_platform_view(DDrawObject *object, unsigned version,
        IUnknown **view, HRESULT *hr)
{
    const GUID *iid = version == DD_VERSION_1 ? &IID_IDirectDraw :
        version == DD_VERSION_2 ? &IID_IDirectDraw2 :
        version == DD_VERSION_3 ? &IID_IDirectDraw3 :
        version == DD_VERSION_4 ? &IID_IDirectDraw4 : &IID_IDirectDraw7;
    *view = NULL;
    if (!object->platform_ddraw) return FALSE;
    *hr = IUnknown_QueryInterface(object->platform_ddraw, iid, (void **)view);
    return TRUE;
}

static BOOL ddraw_platform_cooperative(DDrawObject *object, unsigned version,
        HWND window, DWORD flags, HRESULT *hr)
{
    IUnknown *view;
    if (!ddraw_platform_view(object, version, &view, hr)) return FALSE;
    if (FAILED(*hr)) return TRUE;
    switch (version) {
    case DD_VERSION_1: *hr = IDirectDraw_SetCooperativeLevel((IDirectDraw *)view, window, flags); break;
    case DD_VERSION_2: *hr = IDirectDraw2_SetCooperativeLevel((IDirectDraw2 *)view, window, flags); break;
    case DD_VERSION_3: *hr = IDirectDraw3_SetCooperativeLevel((IDirectDraw3 *)view, window, flags); break;
    case DD_VERSION_4: *hr = IDirectDraw4_SetCooperativeLevel((IDirectDraw4 *)view, window, flags); break;
    default: *hr = IDirectDraw7_SetCooperativeLevel((IDirectDraw7 *)view, window, flags); break;
    }
    IUnknown_Release(view);
    return TRUE;
}

static BOOL ddraw_platform_set_mode(DDrawObject *object, unsigned version,
        DWORD width, DWORD height, DWORD bpp, DWORD refresh, DWORD flags,
        HRESULT *hr)
{
    IUnknown *view;
    if (!ddraw_platform_view(object, version, &view, hr)) return FALSE;
    if (FAILED(*hr)) return TRUE;
    switch (version) {
    case DD_VERSION_1: *hr = IDirectDraw_SetDisplayMode((IDirectDraw *)view, width, height, bpp); break;
    case DD_VERSION_2: *hr = IDirectDraw2_SetDisplayMode((IDirectDraw2 *)view, width, height, bpp, refresh, flags); break;
    case DD_VERSION_3: *hr = IDirectDraw3_SetDisplayMode((IDirectDraw3 *)view, width, height, bpp, refresh, flags); break;
    case DD_VERSION_4: *hr = IDirectDraw4_SetDisplayMode((IDirectDraw4 *)view, width, height, bpp, refresh, flags); break;
    default: *hr = IDirectDraw7_SetDisplayMode((IDirectDraw7 *)view, width, height, bpp, refresh, flags); break;
    }
    IUnknown_Release(view);
    return TRUE;
}

static BOOL ddraw_platform_restore(DDrawObject *object, unsigned version,
        HRESULT *hr)
{
    IUnknown *view;
    if (!ddraw_platform_view(object, version, &view, hr)) return FALSE;
    if (FAILED(*hr)) return TRUE;
    switch (version) {
    case DD_VERSION_1: *hr = IDirectDraw_RestoreDisplayMode((IDirectDraw *)view); break;
    case DD_VERSION_2: *hr = IDirectDraw2_RestoreDisplayMode((IDirectDraw2 *)view); break;
    case DD_VERSION_3: *hr = IDirectDraw3_RestoreDisplayMode((IDirectDraw3 *)view); break;
    case DD_VERSION_4: *hr = IDirectDraw4_RestoreDisplayMode((IDirectDraw4 *)view); break;
    default: *hr = IDirectDraw7_RestoreDisplayMode((IDirectDraw7 *)view); break;
    }
    IUnknown_Release(view);
    return TRUE;
}

static BOOL ddraw_platform_mode(DDrawObject *object, DDSURFACEDESC2 *mode,
        HRESULT *hr)
{
    IUnknown *view;
    if (object->mode_set && !object->guest_mode_changed) return FALSE;
    if (!ddraw_platform_view(object, DD_VERSION_7, &view, hr)) return FALSE;
    if (FAILED(*hr)) return TRUE;
    ZeroMemory(mode, sizeof(*mode));
    mode->dwSize = sizeof(*mode);
    *hr = IDirectDraw7_GetDisplayMode((IDirectDraw7 *)view, mode);
    IUnknown_Release(view);
    return TRUE;
}

static void ddraw_platform_sync(DDrawObject *object)
{
    DDSURFACEDESC2 mode;
    HRESULT hr;
    if (ddraw_platform_mode(object, &mode, &hr) && SUCCEEDED(hr)) {
        object->mode_width = mode.dwWidth;
        object->mode_height = mode.dwHeight;
        object->mode_bpp = mode.ddpfPixelFormat.dwRGBBitCount;
    }
}

static HRESULT ddraw_control_finish(DDrawObject *object, HRESULT hr)
{
    ddraw_lifetime_release(object);
    return DDWG_TRACE_RESULT(hr);
}

/* Roll back a rejected operation without overwriting state set by a nested
 * application callback (the caller checks control_serial first). */
typedef struct DDPlatformModeState {
    UINT width, height, bpp, refresh;
    BOOL set, changed;
} DDPlatformModeState;

static DDPlatformModeState ddraw_platform_save_mode(DDrawObject *object)
{
    DDPlatformModeState state = {object->mode_width, object->mode_height,
        object->mode_bpp, object->mode_refresh, object->mode_set,
        object->guest_mode_changed};
    return state;
}

static void ddraw_platform_load_mode(DDrawObject *object,
        const DDPlatformModeState *state)
{
    object->mode_width = state->width;
    object->mode_height = state->height;
    object->mode_bpp = state->bpp;
    object->mode_refresh = state->refresh;
    object->mode_set = state->set;
    object->guest_mode_changed = state->changed;
}

/* A NORMAL-to-EXCLUSIVE transition loses existing video allocations. Keep the
 * proxy storage until Restore; system and managed storage survive this change. */
static void ddraw_platform_lose_video_surfaces(DDrawObject *object)
{
    DDSurface *surface;
    EnterCriticalSection(&g_object_lock);
    for (surface = object->surfaces; surface; surface = surface->next) {
        if ((surface->desc.ddsCaps.dwCaps & DDSCAPS_VIDEOMEMORY) &&
                !(surface->desc.ddsCaps.dwCaps2 &
                (DDSCAPS2_TEXTUREMANAGE | DDSCAPS2_D3DTEXTUREMANAGE)))
            surface->cooperative_lost = TRUE;
    }
    LeaveCriticalSection(&g_object_lock);
}
