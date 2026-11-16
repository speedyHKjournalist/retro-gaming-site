HRESULT WINAPI d9wg_d3d9_core_surface_desc8(IDirect3DSurface9 *iface,
        void *out, UINT bytes)
{
    D9Surface *surface;
    D3DSURFACE_DESC desc;
    UINT pitch, rows, size = 0;
    if (!iface || iface->lpVtbl != &g_surface_vtbl || !out
            || bytes != D9WG_D3D8_SURFACE_DESC_BYTES)
        return D3DERR_INVALIDCALL;
    surface = surface_from_iface(iface);
    if (device_compatibility_api(surface->device) != 8)
        return D3DERR_INVALIDCALL;
    HRESULT hr = IDirect3DSurface9_GetDesc(iface, &desc);
    if (FAILED(hr)) return hr;
    if (desc.Usage & D3DUSAGE_DEPTHSTENCIL) {
        if (!supported_depth_stencil_format8(desc.Format))
            return D3DERR_INVALIDCALL;
        /* Legacy standalone depth texture levels have no CPU byte_count;
         * its implicit depth surface used a four-byte metadata stride. */
        if (surface->auto_depth_stencil) {
            if ((uint64_t)desc.Width * desc.Height > UINT32_MAX / 4u)
                return D3DERR_INVALIDCALL;
            size = desc.Width * desc.Height * 4u;
        }
    } else if (!d9wg_texture_level_layout(8u, desc.Format,
            desc.Width, desc.Height, &pitch, &rows, &size))
        return D3DERR_INVALIDCALL;
    DWORD packed[8] = {(DWORD)desc.Format, (DWORD)desc.Type, desc.Usage,
            (DWORD)desc.Pool, size, (DWORD)desc.MultiSampleType,
            desc.Width, desc.Height};
    CopyMemory(out, packed, sizeof(packed));
    return D3D_OK;
}
