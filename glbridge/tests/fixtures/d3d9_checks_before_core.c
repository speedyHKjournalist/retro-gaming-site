/* Actual previous implementation from site ccf680b, SDK9. Positive oracle only. */
static BOOL supported_render_target_format_before9(D3DFORMAT format)
{
    switch (format) {
    case D3DFMT_A8R8G8B8:
    case D3DFMT_X8R8G8B8:
    case D3DFMT_R5G6B5:
    case D3DFMT_X1R5G5B5:
    case D3DFMT_A1R5G5B5:
    case D3DFMT_A4R4G4B4:
    case D3DFMT_A2B10G10R10:
    case D3DFMT_A2R10G10B10:
    case D3DFMT_R16F:
    case D3DFMT_G16R16F:
    case D3DFMT_A16B16G16R16F:
    case D3DFMT_R32F:
    case D3DFMT_G32R32F:
    case D3DFMT_A32B32G32R32F:
        return TRUE;
    default:
        /* The NULL render target: a colour attachment whose contents nothing
         * ever reads, so a depth-only pass does not have to allocate one it
         * will not use. It is a render target and nothing else -- never a
         * sampled texture -- which is why it appears here and in no other
         * format predicate. */
        return format == D9FMT_NULL;
    }
}

static BOOL supported_depth_stencil_format_before9(D3DFORMAT format)
{
    switch (format) {
    case D3DFMT_D16:
    case D3DFMT_D16_LOCKABLE:
    case D3DFMT_D15S1:
    case D3DFMT_D24S8:
    case D3DFMT_D24X8:
    case D3DFMT_D24X4S4:
    case D3DFMT_D32:
        return TRUE;
    default:
        /* Outside the switch because a FOURCC is not a D3DFORMAT enumerator,
         * and -Wswitch is right to say so. */
        if (format == D9FMT_RAWZ) {
            HOSTLOG_REFUSED("the RAWZ depth-as-texture format is not "
                    "implemented: it hands the shader the depth buffer's bytes "
                    "for the app to reassemble, and serving clean depth "
                    "instead would make that decode produce nonsense. Use "
                    "INTZ, which is implemented");
            return FALSE;
        }
        return format == D9FMT_DF16 || format == D9FMT_DF24
                || format == D9FMT_INTZ;
    }
}

static BOOL supported_backbuffer_format_before9(D3DFORMAT format)
{
    return format == D3DFMT_A8R8G8B8 || format == D3DFMT_X8R8G8B8
            || format == D3DFMT_R5G6B5;
}

static BOOL supported_multisample_before9(D3DFORMAT format,
        D3DMULTISAMPLE_TYPE multisample)
{
    if (multisample == D3DMULTISAMPLE_NONE)
        return TRUE;
    if (multisample != D3DMULTISAMPLE_4_SAMPLES)
        return FALSE;
    return supported_backbuffer_format_before9(format)
            || supported_render_target_format_before9(format)
            || supported_depth_stencil_format_before9(format);
}

static HRESULT WINAPI d3d_check_device_type_before9(IDirect3D9 *iface, UINT adapter,
        D3DDEVTYPE type, D3DFORMAT display_format,
        D3DFORMAT backbuffer_format, WINBOOL windowed)
{
    BOOL ok;
    (void)iface; (void)windowed;
    ok = !adapter && type == D3DDEVTYPE_HAL
            && (display_format == D3DFMT_X8R8G8B8
                || display_format == D3DFMT_R5G6B5)
            && supported_backbuffer_format_before9(backbuffer_format);

    TRACE("OK CheckDeviceType adapter=%lu type=%lu display=%08lX backbuffer=%08lX "
            "windowed=%lu -> %08lX", adapter, (DWORD)type,
            (DWORD)display_format, (DWORD)backbuffer_format, (DWORD)windowed,
            (DWORD)(ok ? D3D_OK : D3DERR_NOTAVAILABLE));
    return ok ? D3D_OK : D3DERR_NOTAVAILABLE;
}

static HRESULT WINAPI d3d_check_multisample_before9(IDirect3D9 *iface, UINT adapter,
        D3DDEVTYPE type, D3DFORMAT format, WINBOOL windowed,
        D3DMULTISAMPLE_TYPE multisample, DWORD *quality_levels)
{
    BOOL ok;
    (void)iface; (void)windowed;
    ok = !adapter && type == D3DDEVTYPE_HAL
            && supported_multisample_before9(format, multisample);
    if (quality_levels) *quality_levels = ok ? 1u : 0u;

    TRACE("OK CheckDeviceMultiSampleType adapter=%lu type=%lu format=%08lX "
            "multisample=%lu -> %08lX", adapter, (DWORD)type, (DWORD)format,
            (DWORD)multisample, (DWORD)(ok ? D3D_OK : D3DERR_NOTAVAILABLE));
    return ok ? D3D_OK : D3DERR_NOTAVAILABLE;
}

static HRESULT WINAPI d3d_check_depth_stencil_before9(IDirect3D9 *iface,
        UINT adapter, D3DDEVTYPE type, D3DFORMAT adapter_format,
        D3DFORMAT render_format, D3DFORMAT depth_format)
{
    BOOL ok;
    (void)iface; (void)adapter_format; (void)render_format;
    ok = !adapter && type == D3DDEVTYPE_HAL
            && supported_depth_stencil_format_before9(depth_format);

    TRACE("OK CheckDepthStencilMatch adapter=%lu type=%lu adapter_fmt=%08lX "
            "render_fmt=%08lX depth_fmt=%08lX -> %08lX", adapter, (DWORD)type,
            (DWORD)adapter_format, (DWORD)render_format, (DWORD)depth_format,
            (DWORD)(ok ? D3D_OK : D3DERR_NOTAVAILABLE));
    return ok ? D3D_OK : D3DERR_NOTAVAILABLE;
}
