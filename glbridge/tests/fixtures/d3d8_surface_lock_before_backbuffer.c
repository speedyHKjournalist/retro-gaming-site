static HRESULT WINAPI surface_lock_rect(IDirect3DSurface8 *iface,
        D3DLOCKED_RECT *locked_rect, const RECT *rect, DWORD flags)
{
    D8Surface *surface = surface_from_iface(iface);
    RECT area;
    if (surface->texture)
        return texture_lock_level(surface->texture, surface->level,
                locked_rect, rect, flags);
    if (surface->cube)
        return cube_lock_face(surface->cube,
                (D3DCUBEMAP_FACES)surface->face, surface->level,
                locked_rect, rect, flags);
    if (!locked_rect || !surface->shadow || surface->locked
            || surface->backbuffer || surface->depth_stencil)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    if (rect) area = *rect;
    else SetRect(&area, 0, 0, (int)surface->desc.Width,
            (int)surface->desc.Height);
    if (area.left < 0 || area.top < 0 || area.right <= area.left
            || area.bottom <= area.top
            || (UINT)area.right > surface->desc.Width
            || (UINT)area.bottom > surface->desc.Height)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    surface->locked = TRUE;
    surface->lock_rect = area;
    surface->lock_flags = flags;
    locked_rect->Pitch = surface->row_pitch;
    locked_rect->pBits = surface->shadow + area.top * surface->row_pitch
            + area.left * (surface->row_pitch / surface->desc.Width);
    return D3D_OK;
}
