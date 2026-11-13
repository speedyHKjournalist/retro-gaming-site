/* A native primary tracks Windows' persistent mode/focus loss, not game pixels.
 * Native surfaces are never blitted or presented by this frontend. */
static IUnknown *ddraw_platform_primary_ref(DDrawObject *object, IUnknown **draw)
{
    IUnknown *primary = NULL;
    *draw = NULL;
    EnterCriticalSection(&g_object_lock);
    if (object->platform_primary && object->platform_ddraw) {
        primary = object->platform_primary;
        *draw = object->platform_ddraw;
        IUnknown_AddRef(*draw);
        IUnknown_AddRef(primary);
    }
    LeaveCriticalSection(&g_object_lock);
    return primary;
}

static IUnknown *ddraw_platform_primary_take_locked(DDrawObject *object)
{
    IUnknown *primary = object->platform_primary;
    object->platform_primary = NULL;
    object->platform_primary_lost = FALSE;
    object->platform_exclusive_lost = FALSE;
    return primary;
}

static void ddraw_platform_primary_detach(DDrawObject *object)
{
    IUnknown *primary;
    EnterCriticalSection(&g_object_lock);
    primary = ddraw_platform_primary_take_locked(object);
    LeaveCriticalSection(&g_object_lock);
    if (primary) IUnknown_Release(primary);
}

static void ddraw_platform_primary_attach(DDrawObject *object, DDSurface *surface,
        unsigned version)
{
    IUnknown *view, *primary = NULL, *root = object->platform_ddraw;
    HRESULT hr;
    if (!ddraw_platform_view(object, version, &view, &hr)) return;
    if (FAILED(hr)) return;
    if (version < DD_VERSION_4) {
        DDSURFACEDESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.dwSize = sizeof(desc);
        desc.dwFlags = DDSD_CAPS;
        desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
        if (version == DD_VERSION_1)
            hr = IDirectDraw_CreateSurface((IDirectDraw *)view, &desc,
                    (IDirectDrawSurface **)&primary, NULL);
        else if (version == DD_VERSION_2)
            hr = IDirectDraw2_CreateSurface((IDirectDraw2 *)view, &desc,
                    (IDirectDrawSurface **)&primary, NULL);
        else
            hr = IDirectDraw3_CreateSurface((IDirectDraw3 *)view, &desc,
                    (IDirectDrawSurface **)&primary, NULL);
    } else {
        DDSURFACEDESC2 desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.dwSize = sizeof(desc);
        desc.dwFlags = DDSD_CAPS;
        desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
        if (version == DD_VERSION_4)
            hr = IDirectDraw4_CreateSurface((IDirectDraw4 *)view, &desc,
                    (IDirectDrawSurface4 **)&primary, NULL);
        else
            hr = IDirectDraw7_CreateSurface((IDirectDraw7 *)view, &desc,
                    (IDirectDrawSurface7 **)&primary, NULL);
    }
    if (SUCCEEDED(hr)) {
        EnterCriticalSection(&g_object_lock);
        if (object->platform_ddraw == root && object->primary == surface &&
                object->ref > 0 && !object->platform_primary) {
            object->platform_primary = primary;
            object->platform_primary_lost = FALSE;
            primary = NULL;
        }
        LeaveCriticalSection(&g_object_lock);
    }
    /* A callback may have retired the public owner during CreateSurface.
     * Drop its new surface before the temporary draw view can reach zero. */
    if (primary) IUnknown_Release(primary);
    IUnknown_Release(view);
}

static void ddraw_platform_lose_all_video_surfaces(void)
{
    DDrawObject *object;
    EnterCriticalSection(&g_object_lock);
    for (object = g_objects; object; object = object->next)
        ddraw_platform_lose_video_surfaces(object);
    LeaveCriticalSection(&g_object_lock);
}

static void ddraw_platform_poll_loss(DDrawObject *object)
{
    IUnknown *draw, *primary = ddraw_platform_primary_ref(object, &draw), *view;
    HRESULT hr;
    BOOL lost = FALSE, check_cooperative = primary == NULL;
    if (primary) {
        hr = IDirectDrawSurface_IsLost((IDirectDrawSurface *)primary);
        IUnknown_Release(primary);
        IUnknown_Release(draw);
        check_cooperative = hr == DDERR_SURFACELOST;
        if (hr == DDERR_SURFACELOST && !object->platform_primary_lost) {
            object->platform_primary_lost = TRUE;
            lost = TRUE;
        } else if (SUCCEEDED(hr)) object->platform_primary_lost = FALSE;
    }
    if (check_cooperative && (object->cooperative_flags & DDSCL_EXCLUSIVE) &&
            ddraw_platform_view(object, DD_VERSION_7, &view, &hr) && SUCCEEDED(hr)) {
        hr = IDirectDraw7_TestCooperativeLevel((IDirectDraw7 *)view);
        IUnknown_Release(view);
        if (hr == DDERR_NOEXCLUSIVEMODE) {
            if (!object->platform_exclusive_lost) lost = TRUE;
            object->platform_exclusive_lost = TRUE;
        } else if (SUCCEEDED(hr)) object->platform_exclusive_lost = FALSE;
    }
    if (lost) ddraw_platform_lose_video_surfaces(object);
}

static BOOL ddraw_platform_primary_format(DDrawObject *object, DDPIXELFORMAT *format)
{
    DDSURFACEDESC2 mode;
    HRESULT hr;
    if (object->mode_set && !object->guest_mode_changed) {
        mode_pixel_format(object->mode_bpp, format);
        return TRUE;
    }
    if (!ddraw_platform_mode(object, &mode, &hr) || FAILED(hr)) return FALSE;
    *format = mode.ddpfPixelFormat;
    return TRUE;
}

static HRESULT ddraw_platform_restore_surface(DDSurface *surface)
{
    DDrawObject *object = surface->owner;
    DDSurface *walk;
    IUnknown *primary, *draw, *view;
    HRESULT hr;
    ddraw_platform_poll_loss(object);
    if (!surface->cooperative_lost) return DD_OK;
    if (surface->primary && object->mode_set && !object->guest_mode_changed &&
            (surface->width != object->mode_width ||
             surface->height != object->mode_height ||
             surface->desc.ddpfPixelFormat.dwRGBBitCount != object->mode_bpp))
        return DDERR_WRONGMODE;
    primary = surface->primary ? ddraw_platform_primary_ref(object, &draw) : NULL;
    if (primary) {
        hr = IDirectDrawSurface_Restore((IDirectDrawSurface *)primary);
        IUnknown_Release(primary);
        IUnknown_Release(draw);
        if (FAILED(hr)) return hr;
        object->platform_primary_lost = FALSE;
    } else if ((object->cooperative_flags & DDSCL_EXCLUSIVE) &&
            ddraw_platform_view(object, DD_VERSION_7, &view, &hr) && SUCCEEDED(hr)) {
        hr = IDirectDraw7_TestCooperativeLevel((IDirectDraw7 *)view);
        IUnknown_Release(view);
        if (hr == DDERR_NOEXCLUSIVEMODE) return DDERR_WRONGMODE;
        if (FAILED(hr)) return hr;
    }
    EnterCriticalSection(&g_object_lock);
    surface->cooperative_lost = FALSE;
    for (walk = surface->flip_next; walk && walk != surface; walk = walk->flip_next)
        walk->cooperative_lost = FALSE;
    /* Mips, cube faces and duplicate interfaces share one allocation. */
    if (surface->storage)
        for (walk = object->surfaces; walk; walk = walk->next)
            if (walk->storage == surface->storage) walk->cooperative_lost = FALSE;
    LeaveCriticalSection(&g_object_lock);
    return DD_OK;
}
