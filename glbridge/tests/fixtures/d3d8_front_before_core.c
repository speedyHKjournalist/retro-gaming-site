/* Actual c5221bb public8 front readback before common bridge. */
static HRESULT WINAPI device_get_front_buffer(IDirect3DDevice8* iface,
    IDirect3DSurface8* destination_iface)
{
    D8Device* device = device_from_iface(iface);
    if (!destination_iface || destination_iface->lpVtbl != &g_surface_vtbl)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    D8Surface* destination = surface_from_iface(destination_iface);
    D3DSURFACE_DESC desc;
    D8TextureLevel* level = surface_texture_level(destination);
    if (surface_device(destination) != device || FAILED(surface_get_desc(destination_iface, &desc)) || destination->locked || destination->depth_stencil || (level && level->locked))
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    IDirect3DSurface8* source_iface = NULL;
    HRESULT result = create_backbuffer_surface(device, &device->present, FALSE, &source_iface);
    if (FAILED(result))
        return D8WG_TRACE_ERROR(result);
    D8Surface* source = surface_from_iface(source_iface);
    D3DSURFACE_DESC source_desc;
    surface_get_desc(source_iface, &source_desc);
    UINT bw, bh, pixel_bytes;
    if (desc.Width < source_desc.Width || desc.Height < source_desc.Height || (desc.Format != source_desc.Format && !((desc.Format == D3DFMT_A8R8G8B8 || desc.Format == D3DFMT_X8R8G8B8) && (source_desc.Format == D3DFMT_A8R8G8B8 || source_desc.Format == D3DFMT_X8R8G8B8))) || !texture_format_layout(desc.Format, &bw, &bh, &pixel_bytes) || bw != 1 || bh != 1)
        result = D3DERR_INVALIDCALL;
    else {
        UINT source_pitch, destination_pitch;
        BYTE *input = surface_pixels(source, &source_pitch), *output = surface_pixels(destination, &destination_pitch);
        if (!output)
            result = D3DERR_INVALIDCALL;
        else {
            result = readback_surface_pixels(source, desc.Format, input, source_pitch, source_desc.Size);
            if (SUCCEEDED(result)) {
                for (UINT y = 0; y < source_desc.Height; ++y) {
                    CopyMemory(output + y * destination_pitch, input + y * source_pitch, source_desc.Width * pixel_bytes);
                    if (desc.Format == D3DFMT_A8R8G8B8)
                        for (UINT x = 0; x < source_desc.Width; ++x)
                            output[y * destination_pitch + x * 4u + 3u] = 255;
                }
                RECT area;
                SetRect(&area, 0, 0, (int)source_desc.Width, (int)source_desc.Height);
                if (!upload_surface_rect(destination, desc.Format, &area, output, destination_pitch, pixel_bytes)) {
                    if (level)
                        level->gpu_dirty = TRUE;
                    result = D3DERR_DRIVERINTERNALERROR;
                } else if (level && desc.Width == source_desc.Width && desc.Height == source_desc.Height)
                    level->gpu_dirty = FALSE;
            }
        }
    }
    IDirect3DSurface8_Release(source_iface);
    return D8WG_TRACE_ERROR(result);
}
