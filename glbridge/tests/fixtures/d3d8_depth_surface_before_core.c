static HRESULT WINAPI device_create_depth_surface(IDirect3DDevice8 *iface,
        UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE ms,
        IDirect3DSurface8 **surface_out)
{
    D8Texture *texture = NULL;
    HRESULT hr;
    if (!surface_out || !supported_depth_stencil_format(format)
            || !supported_multisample_type(ms))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *surface_out = NULL;
    hr = create_depth_stencil_texture(device_from_iface(iface), width, height,
            format, ms, &texture);
    if (FAILED(hr)) return hr;
    hr = IDirect3DTexture8_GetSurfaceLevel(&texture->iface, 0, surface_out);
    if (SUCCEEDED(hr)) {
        surface_from_iface(*surface_out)->device_container = TRUE;
        surface_from_iface(*surface_out)->refcount = 1;
        texture->private_back_buffer = TRUE;
    }
    IDirect3DTexture8_Release(&texture->iface);
    return D8WG_TRACE_ERROR(hr);
}
