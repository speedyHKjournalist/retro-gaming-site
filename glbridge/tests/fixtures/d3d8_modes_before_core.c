/* Actual previous implementation from site 10ce33d, SDK8. Positive oracle only. */
static UINT WINAPI d3d_get_adapter_mode_count_before8(IDirect3D8 *iface, UINT adapter)
{
    (void)iface;
    return adapter ? 0 : 9;
}

static HRESULT WINAPI d3d_enum_adapter_modes_before8(IDirect3D8 *iface, UINT adapter,
        UINT index, D3DDISPLAYMODE *mode)
{
    static const struct {
        UINT width;
        UINT height;
        D3DFORMAT format;
    } modes[] = {
        { 640, 480, D3DFMT_X1R5G5B5 },
        { 640, 480, D3DFMT_R5G6B5 },
        { 640, 480, D3DFMT_X8R8G8B8 },
        { 800, 600, D3DFMT_X1R5G5B5 },
        { 800, 600, D3DFMT_R5G6B5 },
        { 800, 600, D3DFMT_X8R8G8B8 },
        { 1024, 768, D3DFMT_X1R5G5B5 },
        { 1024, 768, D3DFMT_R5G6B5 },
        { 1024, 768, D3DFMT_X8R8G8B8 }
    };
    (void)iface;
    if (adapter || !mode || index >= sizeof(modes) / sizeof(modes[0]))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    fill_display_mode(mode, modes[index].width, modes[index].height,
            modes[index].format);
    return D3D_OK;
}
