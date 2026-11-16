static HRESULT WINAPI device_create_render_target(IDirect3DDevice9 *iface,
        UINT width, UINT height, D3DFORMAT format,
        D3DMULTISAMPLE_TYPE multisample, DWORD multisample_quality,
        WINBOOL lockable, IDirect3DSurface9 **surface_out, HANDLE *shared)
{
    (void)shared;
    if (!supported_multisample(format, multisample)
            || multisample_quality != 0
            || (lockable && multisample != D3DMULTISAMPLE_NONE)) {
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    }
    if (!supported_render_target_format(format))
        return TRACE_REFUSE(D3DERR_INVALIDCALL);
    return create_target_texture(device_from_iface(iface), width, height, format,
            D3DUSAGE_RENDERTARGET, multisample, multisample_quality,
            surface_out);
}
