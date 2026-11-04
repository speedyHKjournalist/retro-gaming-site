static HRESULT WINAPI surface_lock_rect(IDirect3DSurface9 *iface,
        D3DLOCKED_RECT *locked_rect, const RECT *rect, DWORD flags)
{
    D9Surface *surface = surface_from_iface(iface);
    HRESULT result;

    TRACE_MARK_ENTER("Surface.LockRect");
    TRACE("ENTER Surface.LockRect object=%08lX ref=%ld texture=%08lX "
            "shadow=%08lX level=%lu locked=%lu rect=%08lX flags=%08lX",
            (DWORD)(uintptr_t)iface,
            InterlockedCompareExchange(&surface->refcount, 0, 0),
            (DWORD)(uintptr_t)surface->texture,
            (DWORD)(uintptr_t)surface->shadow, surface->level,
            (DWORD)surface->locked, (DWORD)(uintptr_t)rect, flags);

    if (surface->swap_chain && !surface->texture) {
        result = surface_lock_backbuffer(surface, locked_rect, rect, flags);
        goto done;
    }
    if (surface->shadow) {
        /* A standalone CPU surface. The whole surface is handed over
         * regardless of `rect`: there is no upload to narrow, so a sub-rect
         * lock only changes which pointer the app is given. */
        UINT x = rect ? (UINT)rect->left : 0;
        UINT y = rect ? (UINT)rect->top : 0;
        UINT block_width, block_height, block_bytes;
        if (!locked_rect || surface->locked) {
            result = D3DERR_INVALIDCALL;
            goto done;
        }
        if ((rect && (rect->left < 0 || rect->top < 0
                    || rect->right <= rect->left
                    || rect->bottom <= rect->top
                    || rect->right > (LONG)surface->width
                    || rect->bottom > (LONG)surface->height))
                || !texture_format_layout(surface->format, &block_width,
                    &block_height, &block_bytes)
                || x >= surface->width || y >= surface->height
                || x % block_width || y % block_height) {
            result = D3DERR_INVALIDCALL;
            goto done;
        }
        locked_rect->Pitch = (INT)surface->row_pitch;
        locked_rect->pBits = surface->shadow
                + (y / block_height) * surface->row_pitch
                + (x / block_width) * block_bytes;
        surface->locked = TRUE;
        (void)flags;
        result = D3D_OK;
        goto done;
    }
    if (surface->cube_texture)
        result = IDirect3DCubeTexture9_LockRect(
                &surface->cube_texture->iface,
                (D3DCUBEMAP_FACES)surface->cube_face, surface->level,
                locked_rect, rect, flags);
    else if (!surface->texture)
        result = D3DERR_INVALIDCALL;
    else
        result = texture_lock_level(surface->texture, surface->level,
                locked_rect, rect, flags);
done:
    TRACE("LEAVE Surface.LockRect object=%08lX hr=%08lX pBits=%08lX "
            "pitch=%ld", (DWORD)(uintptr_t)iface, (DWORD)result,
            (DWORD)(uintptr_t)(SUCCEEDED(result) && locked_rect
                    ? locked_rect->pBits : NULL),
            SUCCEEDED(result) && locked_rect ? locked_rect->Pitch : 0);
    TRACE_MARK_EXIT("Surface.LockRect", result,
            SUCCEEDED(result) && locked_rect ? locked_rect->pBits : NULL);
    return result;
}
