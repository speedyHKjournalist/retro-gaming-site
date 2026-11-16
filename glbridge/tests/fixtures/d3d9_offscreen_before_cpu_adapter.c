static HRESULT WINAPI device_create_offscreen_plain_surface(
        IDirect3DDevice9 *iface, UINT width, UINT height, D3DFORMAT format,
        D3DPOOL pool, IDirect3DSurface9 **out, HANDLE *shared)
{
    D9Device *device = device_from_iface(iface);
    D9Surface *surface;
    UINT row_pitch;
    UINT byte_count;

    (void)shared;
    if (!out)
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    *out = NULL;
    if (!width || !height || pool == D3DPOOL_MANAGED
            || pool > D3DPOOL_SCRATCH || !supported_texture_format(format))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    /*
     * A D3DPOOL_DEFAULT offscreen surface lives on the GPU, and D3D9 lets it be
     * a StretchRect operand -- both operands have to be DEFAULT, and this is
     * how apps get the second one. Backing it with the same texture a render
     * target uses is what makes that true here; a CPU-only surface has no
     * handle to blit with, which is precisely how 3DMark06 ended up looking at
     * an "IDirect3DDevice9::StretchRect failed" box.
     *
     * SYSTEMMEM and SCRATCH keep the CPU-only form below. D3D9 refuses
     * StretchRect on those too, so refusing them costs nothing and they stay
     * cheap: no GPU allocation for a surface whose whole purpose is to be
     * locked.
     */
    if (pool == D3DPOOL_DEFAULT)
        return create_target_texture(device, width, height, format, 0u,
                D3DMULTISAMPLE_NONE, 0u, out);
    {
        UINT row_count;
        if (!texture_level_layout(format, width, height, &row_pitch,
                &row_count, &byte_count))
            return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }

    surface = (D9Surface *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*surface));
    if (!surface)
        return TRACE_REFUSE(E_OUTOFMEMORY);
    surface->shadow = (BYTE *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            byte_count);
    if (!surface->shadow) {
        HeapFree(GetProcessHeap(), 0, surface);
        return TRACE_REFUSE(E_OUTOFMEMORY);
    }
    surface->iface.lpVtbl = &g_surface_vtbl;
    surface->refcount = 1;
    surface->device = device;
    surface->width = width;
    surface->height = height;
    surface->format = format;
    surface->pool = pool;
    surface->row_pitch = row_pitch;
    surface->byte_count = byte_count;
    device_child_add_ref(device);

    *out = &surface->iface;
    TRACE_REGISTER_RANGE("SURFACE_OFFSCREEN", 0, surface, surface,
            sizeof(*surface));
    TRACE_REGISTER_RANGE("SURFACE_SHADOW", 0, surface, surface->shadow,
            surface->byte_count);
    TRACE("SURFACE CREATE kind=offscreen object=%08lX vtbl=%08lX ref=%ld "
            "device=%08lX texture=00000000 swapchain=00000000 shadow=%08lX "
            "level=0 size=%lux%lu format=%08lX bytes=%lu",
            (DWORD)(uintptr_t)*out,
            (DWORD)(uintptr_t)surface->iface.lpVtbl, surface->refcount,
            (DWORD)(uintptr_t)&device->iface,
            (DWORD)(uintptr_t)surface->shadow, surface->width,
            surface->height, (DWORD)surface->format, surface->byte_count);
    return D3D_OK;
}
