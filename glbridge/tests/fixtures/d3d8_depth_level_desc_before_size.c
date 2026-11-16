static HRESULT WINAPI texture_get_level_desc(IDirect3DTexture8* iface,
    UINT level, D3DSURFACE_DESC* desc)
{
    D8Texture* texture = texture_from_iface(iface);
    D8TextureLevel* level_data;
    if (!desc || level >= texture->level_count)
        return D8WG_TRACE_ERROR(D3DERR_INVALIDCALL);
    level_data = &texture->levels[level];
    ZeroMemory(desc, sizeof(*desc));
    desc->Format = texture->format;
    desc->Type = D3DRTYPE_SURFACE;
    desc->Usage = texture->usage;
    desc->Pool = texture->pool;
    desc->Size = level_data->byte_count;
    desc->MultiSampleType = texture->multisample_type;
    desc->Width = level_data->width;
    desc->Height = level_data->height;
    return D3D_OK;
}
