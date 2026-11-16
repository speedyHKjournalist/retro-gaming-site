#ifndef D9WG_COPY_RECTS_BRIDGE8_H
#define D9WG_COPY_RECTS_BRIDGE8_H
/* API8 rectangle policy, ordered partial success, GPU copies and CPU overlap
 * snapshots. SDK adapters provide object/storage/transport bindings; ordinary9
 * UpdateSurface/StretchRect/GetRenderTargetData keep their own API contracts. */
static HRESULT d9wg_copy_rect8_common(D9WG_RECT_SURFACE* source, const RECT* source_rect,
    D9WG_RECT_SURFACE* destination, const POINT* destination_point)
{
    D9WG_RECT_DESC from, to;
    D9WG_RECT_LEVEL *from_level = surface_texture_level(source), *to_level = surface_texture_level(destination);
    RECT area, target;
    POINT point = { 0, 0 };
    UINT block_width, block_height, pixel_bytes;
    if (FAILED(D9WG_RECT_GET_DESC(&source->iface, &from)) || FAILED(D9WG_RECT_GET_DESC(&destination->iface, &to)) || from.Format != to.Format || (from.Usage & D3DUSAGE_DEPTHSTENCIL) || (to.Usage & D3DUSAGE_DEPTHSTENCIL) || D9WG_RECT_DEPTH(source) || D9WG_RECT_DEPTH(destination) || source->locked || destination->locked || (from_level && from_level->locked) || (to_level && to_level->locked) || !D9WG_RECT_LAYOUT(from.Format, &block_width, &block_height, &pixel_bytes) || block_width != 1 || block_height != 1)
        return D9WG_RECT_TRACE(D3DERR_INVALIDCALL);
    if (source_rect)
        area = *source_rect;
    else
        SetRect(&area, 0, 0, (int)from.Width, (int)from.Height);
    if (destination_point)
        point = *destination_point;
    if (area.left < 0 || area.top < 0 || area.right <= area.left || area.bottom <= area.top || (UINT)area.right > from.Width || (UINT)area.bottom > from.Height || point.x < 0 || point.y < 0 || (UINT)point.x > to.Width || (UINT)point.y > to.Height || (UINT)(area.right - area.left) > to.Width - (UINT)point.x || (UINT)(area.bottom - area.top) > to.Height - (UINT)point.y)
        return D9WG_RECT_TRACE(D3DERR_INVALIDCALL);
    SetRect(&target, point.x, point.y, point.x + area.right - area.left, point.y + area.bottom - area.top);
    D9WGStretchRect copy;
    ZeroMemory(&copy, sizeof(copy));
    BOOL gpu_from = D9WG_RECT_LOCATION(source, &copy.source_texture_handle, &copy.source_level, &copy.source_face);
    BOOL gpu_to = D9WG_RECT_LOCATION(destination, &copy.destination_texture_handle, &copy.destination_level, &copy.destination_face);
    BOOL same = gpu_from && gpu_to && copy.source_texture_handle == copy.destination_texture_handle && copy.source_level == copy.destination_level && copy.source_face == copy.destination_face;
    if (from.Pool == D3DPOOL_DEFAULT && to.Pool == D3DPOOL_DEFAULT && gpu_from && gpu_to && !same) {
        copy.device_handle = D9WG_RECT_OWNER(source)->handle;
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
            return D9WG_RECT_TRACE(D3DERR_DRIVERINTERNALERROR);
        if (to_level)
            D9WG_RECT_DIRTY(to_level, TRUE);
        return D3D_OK;
    }
    HRESULT result = D9WG_RECT_REFRESH(source);
    if (FAILED(result))
        return D9WG_RECT_TRACE(result);
    UINT from_pitch, to_pitch;
    BYTE *input = D9WG_RECT_PIXELS(source, &from_pitch), *output = D9WG_RECT_PIXELS(destination, &to_pitch);
    if (!input || !output)
        return D9WG_RECT_TRACE(D3DERR_INVALIDCALL);
    UINT row_bytes = (area.right - area.left) * pixel_bytes, rows = area.bottom - area.top;
    BYTE* snapshot = NULL;
    if (input == output) {
        snapshot = HeapAlloc(GetProcessHeap(), 0, row_bytes * rows);
        if (!snapshot)
            return D9WG_RECT_TRACE(E_OUTOFMEMORY);
        for (UINT y = 0; y < rows; ++y)
            CopyMemory(snapshot + y * row_bytes, input + (area.top + y) * from_pitch + area.left * pixel_bytes, row_bytes);
    }
    for (UINT y = 0; y < rows; ++y)
        CopyMemory(output + (target.top + y) * to_pitch + target.left * pixel_bytes,
            snapshot ? snapshot + y * row_bytes : input + (area.top + y) * from_pitch + area.left * pixel_bytes, row_bytes);
    if (snapshot)
        HeapFree(GetProcessHeap(), 0, snapshot);
    BOOL emitted = D9WG_RECT_UPLOAD(destination, to.Format, &target, output, to_pitch, pixel_bytes);
    if (!emitted) {
        if (to_level)
            D9WG_RECT_DIRTY(to_level, TRUE);
        return D9WG_RECT_TRACE(D3DERR_DRIVERINTERNALERROR);
    }
    if (to_level && target.left == 0 && target.top == 0 && (UINT)target.right == to.Width && (UINT)target.bottom == to.Height)
        D9WG_RECT_DIRTY(to_level, FALSE);
    return D3D_OK;
}

static HRESULT d9wg_copy_rects8_common(D9WG_RECT_DEVICE* device,
    D9WG_RECT_IFACE* source_iface, const RECT* rects, UINT count,
    D9WG_RECT_IFACE* destination_iface, const POINT* points)
{
    D9WG_RECT_SURFACE* source;
    D9WG_RECT_SURFACE* destination;
    UINT index;
    if (!source_iface || !destination_iface
        || source_iface->lpVtbl != &g_surface_vtbl
        || destination_iface->lpVtbl != &g_surface_vtbl
        || (count && !rects))
        return D9WG_RECT_TRACE(D3DERR_INVALIDCALL);
    source = surface_from_iface(source_iface);
    destination = surface_from_iface(destination_iface);
    if (D9WG_RECT_OWNER(source) != device || D9WG_RECT_OWNER(destination) != device)
        return D9WG_RECT_TRACE(D3DERR_INVALIDCALL);
    D9WG_RECT_DESC source_desc, destination_desc;
    if (FAILED(D9WG_RECT_GET_DESC(source_iface, &source_desc)) || FAILED(D9WG_RECT_GET_DESC(destination_iface, &destination_desc)) || source_desc.Format != destination_desc.Format || (source_desc.Usage & D3DUSAGE_DEPTHSTENCIL) || (destination_desc.Usage & D3DUSAGE_DEPTHSTENCIL))
        return D9WG_RECT_TRACE(D3DERR_INVALIDCALL);
    if (!count)
        return rects || points ? D3D_OK : d9wg_copy_rect8_common(source, NULL, destination, NULL);
    for (index = 0; index < count; ++index) {
        HRESULT hr = d9wg_copy_rect8_common(source, &rects[index], destination,
            points ? &points[index] : NULL);
        if (FAILED(hr))
            return D9WG_RECT_TRACE(hr);
    }
    return D3D_OK;
}
#undef D9WG_RECT_DEVICE
#undef D9WG_RECT_SURFACE
#undef D9WG_RECT_LEVEL
#undef D9WG_RECT_IFACE
#undef D9WG_RECT_DESC
#undef D9WG_RECT_TRACE
#undef D9WG_RECT_OWNER
#undef D9WG_RECT_GET_DESC
#undef D9WG_RECT_LAYOUT
#undef D9WG_RECT_LOCATION
#undef D9WG_RECT_REFRESH
#undef D9WG_RECT_PIXELS
#undef D9WG_RECT_UPLOAD
#undef D9WG_RECT_DEPTH
#undef D9WG_RECT_DIRTY
#endif
