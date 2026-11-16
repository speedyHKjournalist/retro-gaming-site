static HRESULT copy_surface_rect(D8Surface* source, const RECT* source_rect,
    D8Surface* destination, const POINT* destination_point)
{
    D3DSURFACE_DESC from, to;
    D8TextureLevel *from_level = surface_texture_level(source), *to_level = surface_texture_level(destination);
    RECT area, target;
    POINT point = { 0, 0 };
    UINT block_width, block_height, pixel_bytes;
    if (FAILED(surface_get_desc(&source->iface, &from)) || FAILED(surface_get_desc(&destination->iface, &to)) || from.Format != to.Format || (from.Usage & D3DUSAGE_DEPTHSTENCIL) || (to.Usage & D3DUSAGE_DEPTHSTENCIL) || source->depth_stencil || destination->depth_stencil || source->locked || destination->locked || (from_level && from_level->locked) || (to_level && to_level->locked) || !texture_format_layout(from.Format, &block_width, &block_height, &pixel_bytes) || block_width != 1 || block_height != 1)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (source_rect)
        area = *source_rect;
    else
        SetRect(&area, 0, 0, (int)from.Width, (int)from.Height);
    if (destination_point)
        point = *destination_point;
    if (area.left < 0 || area.top < 0 || area.right <= area.left || area.bottom <= area.top || (UINT)area.right > from.Width || (UINT)area.bottom > from.Height || point.x < 0 || point.y < 0 || (UINT)point.x > to.Width || (UINT)point.y > to.Height || (UINT)(area.right - area.left) > to.Width - (UINT)point.x || (UINT)(area.bottom - area.top) > to.Height - (UINT)point.y)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    SetRect(&target, point.x, point.y, point.x + area.right - area.left, point.y + area.bottom - area.top);
    D9WGStretchRect copy;
    ZeroMemory(&copy, sizeof(copy));
    BOOL gpu_from = surface_gpu_location(source, &copy.source_texture_handle, &copy.source_level, &copy.source_face);
    BOOL gpu_to = surface_gpu_location(destination, &copy.destination_texture_handle, &copy.destination_level, &copy.destination_face);
    BOOL same = gpu_from && gpu_to && copy.source_texture_handle == copy.destination_texture_handle && copy.source_level == copy.destination_level && copy.source_face == copy.destination_face;
    if (from.Pool == D3DPOOL_DEFAULT && to.Pool == D3DPOOL_DEFAULT && gpu_from && gpu_to && !same) {
        copy.device_handle = surface_device(source)->handle;
        copy.source_left = area.left;
        copy.source_top = area.top;
        copy.source_right = area.right;
        copy.source_bottom = area.bottom;
        copy.destination_left = target.left;
        copy.destination_top = target.top;
        copy.destination_right = target.right;
        copy.destination_bottom = target.bottom;
        copy.filter_point = 1;
        if (!emit_command(D9WG_OP_STRETCH_RECT, &copy, sizeof(copy)))
            return D8WG_TRACE_ERROR(D3DERR_DRIVERINTERNALERROR);
        if (to_level)
            to_level->gpu_dirty = TRUE;
        return D3D_OK;
    }
    HRESULT result = refresh_surface_pixels(source);
    if (FAILED(result))
        return D8WG_TRACE_ERROR(result);
    UINT from_pitch, to_pitch;
    BYTE *input = surface_pixels(source, &from_pitch), *output = surface_pixels(destination, &to_pitch);
    if (!input || !output)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    UINT row_bytes = (area.right - area.left) * pixel_bytes, rows = area.bottom - area.top;
    BYTE* snapshot = NULL;
    if (input == output) {
        snapshot = HeapAlloc(GetProcessHeap(), 0, row_bytes * rows);
        if (!snapshot)
            return D8WG_TRACE_ERROR(E_OUTOFMEMORY);
        for (UINT y = 0; y < rows; ++y)
            CopyMemory(snapshot + y * row_bytes, input + (area.top + y) * from_pitch + area.left * pixel_bytes, row_bytes);
    }
    for (UINT y = 0; y < rows; ++y)
        CopyMemory(output + (target.top + y) * to_pitch + target.left * pixel_bytes,
            snapshot ? snapshot + y * row_bytes : input + (area.top + y) * from_pitch + area.left * pixel_bytes, row_bytes);
    if (snapshot)
        HeapFree(GetProcessHeap(), 0, snapshot);
    BOOL emitted = upload_surface_rect(destination, to.Format, &target, output, to_pitch, pixel_bytes);
    if (!emitted) {
        if (to_level)
            to_level->gpu_dirty = TRUE;
        return D8WG_TRACE_ERROR(D3DERR_DRIVERINTERNALERROR);
    }
    if (to_level && target.left == 0 && target.top == 0 && (UINT)target.right == to.Width && (UINT)target.bottom == to.Height)
        to_level->gpu_dirty = FALSE;
    return D3D_OK;
}

static HRESULT WINAPI device_copy_rects(IDirect3DDevice8* iface,
    IDirect3DSurface8* source_iface, const RECT* rects, UINT count,
    IDirect3DSurface8* destination_iface, const POINT* points)
{
    D8Device* device = device_from_iface(iface);
    D8Surface* source;
    D8Surface* destination;
    UINT index;
    if (!source_iface || !destination_iface
        || source_iface->lpVtbl != &g_surface_vtbl
        || destination_iface->lpVtbl != &g_surface_vtbl
        || (count && !rects))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    source = surface_from_iface(source_iface);
    destination = surface_from_iface(destination_iface);
    if (surface_device(source) != device || surface_device(destination) != device)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    D3DSURFACE_DESC source_desc, destination_desc;
    if (FAILED(surface_get_desc(source_iface, &source_desc)) || FAILED(surface_get_desc(destination_iface, &destination_desc)) || source_desc.Format != destination_desc.Format || (source_desc.Usage & D3DUSAGE_DEPTHSTENCIL) || (destination_desc.Usage & D3DUSAGE_DEPTHSTENCIL))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (!count)
        return rects || points ? D3D_OK : copy_surface_rect(source, NULL, destination, NULL);
    for (index = 0; index < count; ++index) {
        HRESULT hr = copy_surface_rect(source, &rects[index], destination,
            points ? &points[index] : NULL);
        if (FAILED(hr))
            return D8WG_TRACE_ERROR(hr);
    }
    return D3D_OK;
}
