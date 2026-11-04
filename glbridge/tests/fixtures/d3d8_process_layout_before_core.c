static BOOL decode_fvf_layout(DWORD fvf, D8FVFLayout *layout)
{
    DWORD position = fvf & D3DFVF_POSITION_MASK;
    if (position != D3DFVF_XYZ && position != D3DFVF_XYZRHW) return FALSE;
    if (position == D3DFVF_XYZRHW && (fvf & D3DFVF_NORMAL)) return FALSE;
    return d9wg_process_fvf(D9WG_FVF_D3D8, fvf, layout);
}
