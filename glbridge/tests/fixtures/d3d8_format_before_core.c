/* Actual previous implementation from site 8509914, SDK8. Positive oracle only. */
static HRESULT WINAPI d3d_check_device_format_before8(IDirect3D8 *iface,
        UINT adapter, D3DDEVTYPE type, D3DFORMAT adapter_format,
        DWORD usage, D3DRESOURCETYPE resource_type, D3DFORMAT format)
{
    (void)iface;
    (void)adapter_format;
    if (adapter || type != D3DDEVTYPE_HAL)
        return D8WG_TRACE_ERROR(D3DERR_NOTAVAILABLE);
    if (resource_type == D3DRTYPE_TEXTURE
            && !(usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL))
            && supported_texture_format_before8(format))
        return D3D_OK;
    if (resource_type == D3DRTYPE_CUBETEXTURE
            && !(usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL))
            && supported_texture_format_before8(format))
        return D3D_OK;
    if (resource_type == D3DRTYPE_CUBETEXTURE
            && usage == D3DUSAGE_RENDERTARGET
            && supported_render_target_format_before8(format))
        return D3D_OK;
    /* Volumes have no block-compressed layout on this path, and are never
     * render targets -- device_create_volume_texture refuses both. */
    if (resource_type == D3DRTYPE_VOLUMETEXTURE
            && !(usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL))
            && format != D3DFMT_DXT1 && format != D3DFMT_DXT2
            && format != D3DFMT_DXT3 && format != D3DFMT_DXT4
            && format != D3DFMT_DXT5
            && supported_texture_format_before8(format))
        return D3D_OK;
    if (resource_type == D3DRTYPE_TEXTURE
            && usage == D3DUSAGE_RENDERTARGET
            && supported_render_target_format_before8(format))
        return D3D_OK;
    /*
     * 3DMark2001 validates every enumerated display mode with this exact
     * query before it exposes the adapter:
     *
     *   CheckDeviceFormat(display_format, D3DUSAGE_RENDERTARGET,
     *           D3DRTYPE_SURFACE, display_format)
     *
     * A render-target surface is the object returned by CreateRenderTarget,
     * which this frontend backs with the same one-level target texture as the
     * texture case above. Omitting SURFACE here therefore hid a capability the
     * device really has and made 3DMark reject every 32-bit display mode as
     * non-renderable before CreateDevice was ever reached.
     */
    if (resource_type == D3DRTYPE_SURFACE
            && usage == D3DUSAGE_RENDERTARGET
            && supported_render_target_format_before8(format))
        return D3D_OK;
    if (resource_type == D3DRTYPE_SURFACE
            && (usage & D3DUSAGE_DEPTHSTENCIL)
            && supported_depth_stencil_format_before8(format))
        return D3D_OK;
    if (resource_type == D3DRTYPE_SURFACE
            && !(usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL))
            && supported_image_surface_format_before8(format))
        return D3D_OK;
    D8WG_TRACE("CHECK_FORMAT REFUSE adapter=%lu type=%lu adapter_fmt=%08lX "
            "usage=%08lX resource_type=%lu format=%08lX", adapter,
            (DWORD)type, (DWORD)adapter_format, usage, (DWORD)resource_type,
            (DWORD)format);
    return D8WG_TRACE_ERROR(D3DERR_NOTAVAILABLE);
}

static BOOL texture_format_layout_before8(D3DFORMAT format, UINT *block_width,
        UINT *block_height, UINT *block_bytes)
{
    return d9wg_format_layout(8, (uint32_t)format, block_width, block_height, block_bytes);
}

static BOOL supported_image_surface_format_before8(D3DFORMAT format)
{
    UINT block_width;
    UINT block_height;
    UINT block_bytes;
    return texture_format_layout_before8(format, &block_width, &block_height,
            &block_bytes) && block_width == 1 && block_height == 1;
}

static BOOL supported_texture_format_before8(D3DFORMAT format)
{
    UINT block_width;
    UINT block_height;
    UINT block_bytes;
    return texture_format_layout_before8(format, &block_width, &block_height,
            &block_bytes);
}

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
