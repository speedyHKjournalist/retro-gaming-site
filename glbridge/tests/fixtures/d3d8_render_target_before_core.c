static HRESULT WINAPI device_create_render_target(IDirect3DDevice8* iface,
    UINT width, UINT height, D3DFORMAT format, D3DMULTISAMPLE_TYPE ms,
    WINBOOL lockable, IDirect3DSurface8** surface_out)
{
    IDirect3DTexture8* texture_iface = NULL;
    HRESULT hr;
    if (!surface_out || !supported_multisample_type(ms))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    /* A multisampled surface has no single value per pixel to hand back, so
     * D3D8 forbids locking one -- and so does this path, rather than returning
     * a lock that reads one arbitrary sample. */
    if (lockable && ms != D3DMULTISAMPLE_NONE)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    *surface_out = NULL;
    hr = create_texture_internal(device_from_iface(iface), width, height, 1,
        D3DUSAGE_RENDERTARGET, format, D3DPOOL_DEFAULT, ms, &texture_iface);
    if (FAILED(hr))
        return D8WG_TRACE_ERROR(hr);
    texture_from_iface(texture_iface)->lockable_render_target = !!lockable;
    hr = IDirect3DTexture8_GetSurfaceLevel(texture_iface, 0, surface_out);
    if (SUCCEEDED(hr)) {
        surface_from_iface(*surface_out)->device_container = TRUE;
        surface_from_iface(*surface_out)->refcount = 1;
        texture_from_iface(texture_iface)->private_back_buffer = TRUE;
    }
    IDirect3DTexture8_Release(texture_iface);
    return D8WG_TRACE_ERROR(hr);
}
