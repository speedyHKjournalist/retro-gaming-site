static HRESULT create_standalone_surface(D8Device *device, UINT width,
        UINT height, D3DFORMAT format, DWORD usage, D3DPOOL pool,
        BOOL lockable, BOOL backbuffer, BOOL depth_stencil,
        BOOL reset_blocker, IDirect3DSurface8 **surface_out)
{
    D8Surface *surface;
    UINT block_width;
    UINT block_height;
    UINT pixel_bytes;
    UINT row_pitch;
    UINT row_count;
    UINT byte_count;
    if (!surface_out)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *surface_out = NULL;
    if (!width || !height || width > 8192 || height > 8192)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (!depth_stencil) {
        if (!texture_format_layout(format, &block_width, &block_height,
                &pixel_bytes)
                || (lockable && (block_width != 1 || block_height != 1))
                || !texture_level_layout(format, width, height,
                    &row_pitch, &row_count, &byte_count))
            return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    }
    if (depth_stencil) {
        row_pitch = width * 4u;
        row_count = height;
        byte_count = row_pitch * row_count;
    }
    surface = (D8Surface *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY,
            sizeof(*surface));
    if (!surface) return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
    if (lockable) {
        surface->shadow = (BYTE *)HeapAlloc(GetProcessHeap(),
                HEAP_ZERO_MEMORY, byte_count);
        if (!surface->shadow) {
            HeapFree(GetProcessHeap(), 0, surface);
            return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
        }
    }
    surface->iface.lpVtbl = &g_surface_vtbl;
    surface->refcount = 1;
    surface->device = device;
    device_child_add_ref(device);
    surface->row_pitch = row_pitch;
    surface->backbuffer = backbuffer;
    surface->depth_stencil = depth_stencil;
    surface->reset_blocker = reset_blocker;
    if (reset_blocker) ++device->implicit_surface_count;
    ZeroMemory(&surface->desc, sizeof(surface->desc));
    surface->desc.Format = format;
    surface->desc.Type = D3DRTYPE_SURFACE;
    surface->desc.Usage = usage;
    surface->desc.Pool = pool;
    surface->desc.Size = byte_count;
    surface->desc.MultiSampleType = D3DMULTISAMPLE_NONE;
    surface->desc.Width = width;
    surface->desc.Height = height;
    *surface_out = &surface->iface;
    return D3D_OK;
}
static HRESULT WINAPI device_create_image_surface(IDirect3DDevice8 *iface,
        UINT width, UINT height, D3DFORMAT format,
        IDirect3DSurface8 **surface_out)
{
    if (!surface_out) return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *surface_out = NULL;
    return create_standalone_surface(device_from_iface(iface), width, height,
            format, 0, D3DPOOL_SYSTEMMEM, TRUE, FALSE, FALSE, FALSE,
            surface_out);
}
