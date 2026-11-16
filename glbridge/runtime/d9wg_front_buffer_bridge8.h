#ifndef D9WG_FRONT_BUFFER_BRIDGE8_H
#define D9WG_FRONT_BUFFER_BRIDGE8_H
/* API8 front readback policy and row preservation are defined once. SDK
 * adapters supply object access, storage, transport and upload bindings.
 * Ordinary9 GetFrontBufferData retains its own stricter destination rules. */
static HRESULT d9wg_front_buffer8_common(D9WG_FRONT_DEVICE *device,
        D9WG_FRONT_IFACE *destination_iface)
{
    if (!destination_iface || destination_iface->lpVtbl != &g_surface_vtbl)
        return D9WG_FRONT_TRACE(D3DERR_INVALIDCALL);
    D9WG_FRONT_SURFACE* destination = surface_from_iface(destination_iface);
    D9WG_FRONT_DESC desc;
    D9WG_FRONT_LEVEL* level = surface_texture_level(destination);
    if (D9WG_FRONT_OWNER(destination) != device || FAILED(D9WG_FRONT_GET_DESC(destination_iface, &desc)) || destination->locked || D9WG_FRONT_DEPTH(destination) || (level && level->locked))
        return D9WG_FRONT_TRACE(D3DERR_INVALIDCALL);
    D9WG_FRONT_IFACE* source_iface = NULL;
    HRESULT result = D9WG_FRONT_SOURCE(device, &source_iface);
    if (FAILED(result))
        return D9WG_FRONT_TRACE(result);
    D9WG_FRONT_SURFACE* source = surface_from_iface(source_iface);
    D9WG_FRONT_DESC source_desc;
    D9WG_FRONT_GET_DESC(source_iface, &source_desc);
    UINT bw, bh, pixel_bytes;
    if (desc.Width < source_desc.Width || desc.Height < source_desc.Height || (desc.Format != source_desc.Format && !((desc.Format == D3DFMT_A8R8G8B8 || desc.Format == D3DFMT_X8R8G8B8) && (source_desc.Format == D3DFMT_A8R8G8B8 || source_desc.Format == D3DFMT_X8R8G8B8))) || !D9WG_FRONT_LAYOUT(desc.Format, &bw, &bh, &pixel_bytes) || bw != 1 || bh != 1)
        result = D3DERR_INVALIDCALL;
    else {
        UINT source_pitch, destination_pitch;
        BYTE *input = D9WG_FRONT_PIXELS(source, &source_pitch), *output = D9WG_FRONT_PIXELS(destination, &destination_pitch);
        if (!output)
            result = D3DERR_INVALIDCALL;
        else {
            result = D9WG_FRONT_READ(source, desc.Format, input, source_pitch, D9WG_FRONT_SOURCE_BYTES(source, source_desc));
            if (SUCCEEDED(result)) {
                for (UINT y = 0; y < source_desc.Height; ++y) {
                    CopyMemory(output + y * destination_pitch, input + y * source_pitch, source_desc.Width * pixel_bytes);
                    if (desc.Format == D3DFMT_A8R8G8B8)
                        for (UINT x = 0; x < source_desc.Width; ++x)
                            output[y * destination_pitch + x * 4u + 3u] = 255;
                }
                RECT area;
                SetRect(&area, 0, 0, (int)source_desc.Width, (int)source_desc.Height);
                if (!D9WG_FRONT_UPLOAD(destination, desc.Format, &area, output, destination_pitch, pixel_bytes)) {
                    if (level)
                        D9WG_FRONT_DIRTY(level, TRUE);
                    result = D3DERR_DRIVERINTERNALERROR;
                } else if (level && desc.Width == source_desc.Width && desc.Height == source_desc.Height)
                    D9WG_FRONT_DIRTY(level, FALSE);
            }
        }
    }
    D9WG_FRONT_RELEASE(source_iface);
    return D9WG_FRONT_TRACE(result);
}
#undef D9WG_FRONT_DEVICE
#undef D9WG_FRONT_IFACE
#undef D9WG_FRONT_SURFACE
#undef D9WG_FRONT_LEVEL
#undef D9WG_FRONT_DESC
#undef D9WG_FRONT_TRACE
#undef D9WG_FRONT_OWNER
#undef D9WG_FRONT_GET_DESC
#undef D9WG_FRONT_LAYOUT
#undef D9WG_FRONT_PIXELS
#undef D9WG_FRONT_READ
#undef D9WG_FRONT_UPLOAD
#undef D9WG_FRONT_DEPTH
#undef D9WG_FRONT_SOURCE
#undef D9WG_FRONT_SOURCE_BYTES
#undef D9WG_FRONT_DIRTY
#undef D9WG_FRONT_RELEASE
#endif
