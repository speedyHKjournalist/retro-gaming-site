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

typedef struct DDPlatformPrimary {
    IUnknown *surface, *view, *root;
    DDSURFACEDESC2 desc;
    BOOL metadata;
} DDPlatformPrimary;

static void ddraw_platform_primary_dispose(DDPlatformPrimary *prepared)
{
    /* Legacy surfaces do not keep their public draw parent alive. */
    if (prepared->surface) IUnknown_Release(prepared->surface);
    if (prepared->view) IUnknown_Release(prepared->view);
    ZeroMemory(prepared, sizeof(*prepared));
}

static void ddraw_platform_primary_prepare(DDrawObject *object, unsigned version,
        BOOL flip, DWORD back_buffers, DDPlatformPrimary *prepared)
{
    IUnknown *view, *primary = NULL;
    HRESULT hr;
    ZeroMemory(prepared, sizeof(*prepared));
    prepared->root = object->platform_ddraw;
    if (!ddraw_platform_view(object, version, &view, &hr) || FAILED(hr)) return;
    prepared->view = view;
    if (version < DD_VERSION_4) {
        DDSURFACEDESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.dwSize = sizeof(desc);
        desc.dwFlags = DDSD_CAPS;
        desc.ddsCaps.dwCaps = DDSCAPS_PRIMARYSURFACE;
        if (flip) {
            desc.dwFlags |= DDSD_BACKBUFFERCOUNT;
            desc.ddsCaps.dwCaps |= DDSCAPS_COMPLEX | DDSCAPS_FLIP;
            desc.dwBackBufferCount = back_buffers;
        }
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
        if (flip) {
            desc.dwFlags |= DDSD_BACKBUFFERCOUNT;
            desc.ddsCaps.dwCaps |= DDSCAPS_COMPLEX | DDSCAPS_FLIP;
            desc.dwBackBufferCount = back_buffers;
        }
        if (version == DD_VERSION_4)
            hr = IDirectDraw4_CreateSurface((IDirectDraw4 *)view, &desc,
                    (IDirectDrawSurface4 **)&primary, NULL);
        else
            hr = IDirectDraw7_CreateSurface((IDirectDraw7 *)view, &desc,
                    (IDirectDrawSurface7 **)&primary, NULL);
    }
    if (FAILED(hr)) {
        if (primary) IUnknown_Release(primary);
        return;
    }
    prepared->surface = primary;
    if (version < DD_VERSION_4) {
        DDSURFACEDESC desc;
        ZeroMemory(&desc, sizeof(desc));
        desc.dwSize = sizeof(desc);
        hr = IDirectDrawSurface_GetSurfaceDesc((IDirectDrawSurface *)primary, &desc);
        if (SUCCEEDED(hr)) {
            prepared->desc.dwWidth = desc.dwWidth;
            prepared->desc.dwHeight = desc.dwHeight;
            prepared->desc.ddpfPixelFormat = desc.ddpfPixelFormat;
        }
    } else {
        prepared->desc.dwSize = sizeof(prepared->desc);
        hr = IDirectDrawSurface7_GetSurfaceDesc((IDirectDrawSurface7 *)primary,
                &prepared->desc);
    }
    prepared->metadata = SUCCEEDED(hr) && prepared->desc.dwWidth &&
            prepared->desc.dwHeight;
}

static void ddraw_platform_primary_attach(DDrawObject *object, DDSurface *surface,
        DDPlatformPrimary *prepared)
{
    EnterCriticalSection(&g_object_lock);
    if (prepared->surface && object->platform_ddraw == prepared->root &&
            object->primary == surface && object->ref > 0 &&
            !object->platform_primary) {
        object->platform_primary = prepared->surface;
        object->platform_primary_lost = FALSE;
        prepared->surface = NULL;
    }
    LeaveCriticalSection(&g_object_lock);
    ddraw_platform_primary_dispose(prepared);
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
    if (surface->primary) {
        UINT width, height, bpp;
        BOOL known;
        if (object->mode_set && !object->guest_mode_changed) {
            width = object->mode_width;
            height = object->mode_height;
            bpp = object->mode_bpp;
            known = TRUE;
        } else known = ddraw_platform_desktop_size(&width, &height, &bpp);
        /* The private control allocation can have different dimensions.
         * Its successful Restore cannot validate the proxy's pixel storage. */
        if (known && (surface->width != width || surface->height != height ||
                surface->desc.ddpfPixelFormat.dwRGBBitCount != bpp))
            return DDWG_TRACE_ERROR(DDERR_WRONGMODE);
    }
    /* Native Restore of a flip primary in NORMAL can crash on XP. Its
     * allocation cannot be restored until exclusive ownership returns. */
    if (surface->primary && surface->front_of_chain &&
            !(object->cooperative_flags & DDSCL_EXCLUSIVE))
        return DDWG_TRACE_ERROR(DDERR_WRONGMODE);
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
        if (hr == DDERR_NOEXCLUSIVEMODE) return DDWG_TRACE_ERROR(DDERR_WRONGMODE);
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
