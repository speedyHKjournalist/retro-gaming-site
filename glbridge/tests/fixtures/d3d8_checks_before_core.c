/* Actual previous implementation from site ccf680b, SDK8. Positive oracle only. */
static BOOL supported_render_target_format_before8(D3DFORMAT format)
{
    return format == D3DFMT_A8R8G8B8 || format == D3DFMT_X8R8G8B8
            || format == D3DFMT_R5G6B5 || format == D3DFMT_X1R5G5B5
            || format == D3DFMT_A1R5G5B5 || format == D3DFMT_A4R4G4B4;
}

static BOOL supported_depth_stencil_format_before8(D3DFORMAT format)
{
    return format == D3DFMT_D16 || format == D3DFMT_D24S8
            || format == D3DFMT_D32 || format == D3DFMT_D15S1
            || format == D3DFMT_D24X8 || format == D3DFMT_D24X4S4;
}

static BOOL supported_backbuffer_format_before8(D3DFORMAT format)
{
    return supported_render_target_format_before8(format);
}

static BOOL supported_multisample_type_before8(D3DMULTISAMPLE_TYPE multisample)
{
    return multisample == D3DMULTISAMPLE_NONE
            || multisample == D3DMULTISAMPLE_4_SAMPLES;
}

static HRESULT WINAPI d3d_check_device_type_before8(IDirect3D8 *iface, UINT adapter,
        D3DDEVTYPE type, D3DFORMAT display_format,
        D3DFORMAT backbuffer_format, WINBOOL windowed)
{
    (void)iface;
    (void)windowed;
    if (adapter || type != D3DDEVTYPE_HAL)
        return D8WG_TRACE_ERROR(D3DERR_NOTAVAILABLE);
    if ((display_format != D3DFMT_X8R8G8B8
            && display_format != D3DFMT_R5G6B5
            && display_format != D3DFMT_X1R5G5B5)
            || !supported_backbuffer_format_before8(backbuffer_format))
        return D8WG_TRACE_ERROR(D3DERR_NOTAVAILABLE);
    return D3D_OK;
}

static HRESULT WINAPI d3d_check_multisample_before8(IDirect3D8 *iface, UINT adapter,
        D3DDEVTYPE type, D3DFORMAT format, WINBOOL windowed,
        D3DMULTISAMPLE_TYPE multisample)
{
    (void)iface;
    (void)format;
    (void)windowed;
    return !adapter && type == D3DDEVTYPE_HAL
            && supported_multisample_type_before8(multisample)
            ? D3D_OK : D3DERR_NOTAVAILABLE;
}

static HRESULT WINAPI d3d_check_depth_stencil_before8(IDirect3D8 *iface,
        UINT adapter, D3DDEVTYPE type, D3DFORMAT adapter_format,
        D3DFORMAT render_format, D3DFORMAT depth_format)
{
    (void)iface;
    (void)adapter_format;
    (void)render_format;
    if (adapter || type != D3DDEVTYPE_HAL)
        return D8WG_TRACE_ERROR(D3DERR_NOTAVAILABLE);
    return supported_depth_stencil_format_before8(depth_format)
            ? D3D_OK : D3DERR_NOTAVAILABLE;
}
